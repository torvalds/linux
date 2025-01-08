// SPDX-License-Identifier: GPL-2.0-only
#include <linux/ras.h>
#include "amd64_edac.h"
#include <asm/amd_nb.h>

static struct edac_pci_ctl_info *pci_ctl;

/*
 * Set by command line parameter. If BIOS has enabled the ECC, this override is
 * cleared to prevent re-enabling the hardware by this driver.
 */
static int ecc_enable_override;
module_param(ecc_enable_override, int, 0644);

static struct msr __percpu *msrs;

static inline u32 get_umc_reg(struct amd64_pvt *pvt, u32 reg)
{
	if (!pvt->flags.zn_regs_v2)
		return reg;

	switch (reg) {
	case UMCCH_ADDR_MASK_SEC:	return UMCCH_ADDR_MASK_SEC_DDR5;
	case UMCCH_DIMM_CFG:		return UMCCH_DIMM_CFG_DDR5;
	}

	WARN_ONCE(1, "%s: unknown register 0x%x", __func__, reg);
	return 0;
}

/* Per-node stuff */
static struct ecc_settings **ecc_stngs;

/* Device for the PCI component */
static struct device *pci_ctl_dev;

/*
 * Valid scrub rates for the K8 hardware memory scrubber. We map the scrubbing
 * bandwidth to a valid bit pattern. The 'set' operation finds the 'matching-
 * or higher value'.
 *
 *FIXME: Produce a better mapping/linearisation.
 */
static const struct scrubrate {
       u32 scrubval;           /* bit pattern for scrub rate */
       u32 bandwidth;          /* bandwidth consumed (bytes/sec) */
} scrubrates[] = {
	{ 0x01, 1600000000UL},
	{ 0x02, 800000000UL},
	{ 0x03, 400000000UL},
	{ 0x04, 200000000UL},
	{ 0x05, 100000000UL},
	{ 0x06, 50000000UL},
	{ 0x07, 25000000UL},
	{ 0x08, 12284069UL},
	{ 0x09, 6274509UL},
	{ 0x0A, 3121951UL},
	{ 0x0B, 1560975UL},
	{ 0x0C, 781440UL},
	{ 0x0D, 390720UL},
	{ 0x0E, 195300UL},
	{ 0x0F, 97650UL},
	{ 0x10, 48854UL},
	{ 0x11, 24427UL},
	{ 0x12, 12213UL},
	{ 0x13, 6101UL},
	{ 0x14, 3051UL},
	{ 0x15, 1523UL},
	{ 0x16, 761UL},
	{ 0x00, 0UL},        /* scrubbing off */
};

int __amd64_read_pci_cfg_dword(struct pci_dev *pdev, int offset,
			       u32 *val, const char *func)
{
	int err = 0;

	err = pci_read_config_dword(pdev, offset, val);
	if (err)
		amd64_warn("%s: error reading F%dx%03x.\n",
			   func, PCI_FUNC(pdev->devfn), offset);

	return pcibios_err_to_errno(err);
}

int __amd64_write_pci_cfg_dword(struct pci_dev *pdev, int offset,
				u32 val, const char *func)
{
	int err = 0;

	err = pci_write_config_dword(pdev, offset, val);
	if (err)
		amd64_warn("%s: error writing to F%dx%03x.\n",
			   func, PCI_FUNC(pdev->devfn), offset);

	return pcibios_err_to_errno(err);
}

/*
 * Select DCT to which PCI cfg accesses are routed
 */
static void f15h_select_dct(struct amd64_pvt *pvt, u8 dct)
{
	u32 reg = 0;

	amd64_read_pci_cfg(pvt->F1, DCT_CFG_SEL, &reg);
	reg &= (pvt->model == 0x30) ? ~3 : ~1;
	reg |= dct;
	amd64_write_pci_cfg(pvt->F1, DCT_CFG_SEL, reg);
}

/*
 *
 * Depending on the family, F2 DCT reads need special handling:
 *
 * K8: has a single DCT only and no address offsets >= 0x100
 *
 * F10h: each DCT has its own set of regs
 *	DCT0 -> F2x040..
 *	DCT1 -> F2x140..
 *
 * F16h: has only 1 DCT
 *
 * F15h: we select which DCT we access using F1x10C[DctCfgSel]
 */
static inline int amd64_read_dct_pci_cfg(struct amd64_pvt *pvt, u8 dct,
					 int offset, u32 *val)
{
	switch (pvt->fam) {
	case 0xf:
		if (dct || offset >= 0x100)
			return -EINVAL;
		break;

	case 0x10:
		if (dct) {
			/*
			 * Note: If ganging is enabled, barring the regs
			 * F2x[1,0]98 and F2x[1,0]9C; reads reads to F2x1xx
			 * return 0. (cf. Section 2.8.1 F10h BKDG)
			 */
			if (dct_ganging_enabled(pvt))
				return 0;

			offset += 0x100;
		}
		break;

	case 0x15:
		/*
		 * F15h: F2x1xx addresses do not map explicitly to DCT1.
		 * We should select which DCT we access using F1x10C[DctCfgSel]
		 */
		dct = (dct && pvt->model == 0x30) ? 3 : dct;
		f15h_select_dct(pvt, dct);
		break;

	case 0x16:
		if (dct)
			return -EINVAL;
		break;

	default:
		break;
	}
	return amd64_read_pci_cfg(pvt->F2, offset, val);
}

/*
 * Memory scrubber control interface. For K8, memory scrubbing is handled by
 * hardware and can involve L2 cache, dcache as well as the main memory. With
 * F10, this is extended to L3 cache scrubbing on CPU models sporting that
 * functionality.
 *
 * This causes the "units" for the scrubbing speed to vary from 64 byte blocks
 * (dram) over to cache lines. This is nasty, so we will use bandwidth in
 * bytes/sec for the setting.
 *
 * Currently, we only do dram scrubbing. If the scrubbing is done in software on
 * other archs, we might not have access to the caches directly.
 */

/*
 * Scan the scrub rate mapping table for a close or matching bandwidth value to
 * issue. If requested is too big, then use last maximum value found.
 */
static int __set_scrub_rate(struct amd64_pvt *pvt, u32 new_bw, u32 min_rate)
{
	u32 scrubval;
	int i;

	/*
	 * map the configured rate (new_bw) to a value specific to the AMD64
	 * memory controller and apply to register. Search for the first
	 * bandwidth entry that is greater or equal than the setting requested
	 * and program that. If at last entry, turn off DRAM scrubbing.
	 *
	 * If no suitable bandwidth is found, turn off DRAM scrubbing entirely
	 * by falling back to the last element in scrubrates[].
	 */
	for (i = 0; i < ARRAY_SIZE(scrubrates) - 1; i++) {
		/*
		 * skip scrub rates which aren't recommended
		 * (see F10 BKDG, F3x58)
		 */
		if (scrubrates[i].scrubval < min_rate)
			continue;

		if (scrubrates[i].bandwidth <= new_bw)
			break;
	}

	scrubval = scrubrates[i].scrubval;

	if (pvt->fam == 0x15 && pvt->model == 0x60) {
		f15h_select_dct(pvt, 0);
		pci_write_bits32(pvt->F2, F15H_M60H_SCRCTRL, scrubval, 0x001F);
		f15h_select_dct(pvt, 1);
		pci_write_bits32(pvt->F2, F15H_M60H_SCRCTRL, scrubval, 0x001F);
	} else {
		pci_write_bits32(pvt->F3, SCRCTRL, scrubval, 0x001F);
	}

	if (scrubval)
		return scrubrates[i].bandwidth;

	return 0;
}

static int set_scrub_rate(struct mem_ctl_info *mci, u32 bw)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	u32 min_scrubrate = 0x5;

	if (pvt->fam == 0xf)
		min_scrubrate = 0x0;

	if (pvt->fam == 0x15) {
		/* Erratum #505 */
		if (pvt->model < 0x10)
			f15h_select_dct(pvt, 0);

		if (pvt->model == 0x60)
			min_scrubrate = 0x6;
	}
	return __set_scrub_rate(pvt, bw, min_scrubrate);
}

static int get_scrub_rate(struct mem_ctl_info *mci)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	int i, retval = -EINVAL;
	u32 scrubval = 0;

	if (pvt->fam == 0x15) {
		/* Erratum #505 */
		if (pvt->model < 0x10)
			f15h_select_dct(pvt, 0);

		if (pvt->model == 0x60)
			amd64_read_pci_cfg(pvt->F2, F15H_M60H_SCRCTRL, &scrubval);
		else
			amd64_read_pci_cfg(pvt->F3, SCRCTRL, &scrubval);
	} else {
		amd64_read_pci_cfg(pvt->F3, SCRCTRL, &scrubval);
	}

	scrubval = scrubval & 0x001F;

	for (i = 0; i < ARRAY_SIZE(scrubrates); i++) {
		if (scrubrates[i].scrubval == scrubval) {
			retval = scrubrates[i].bandwidth;
			break;
		}
	}
	return retval;
}

/*
 * returns true if the SysAddr given by sys_addr matches the
 * DRAM base/limit associated with node_id
 */
static bool base_limit_match(struct amd64_pvt *pvt, u64 sys_addr, u8 nid)
{
	u64 addr;

	/* The K8 treats this as a 40-bit value.  However, bits 63-40 will be
	 * all ones if the most significant implemented address bit is 1.
	 * Here we discard bits 63-40.  See section 3.4.2 of AMD publication
	 * 24592: AMD x86-64 Architecture Programmer's Manual Volume 1
	 * Application Programming.
	 */
	addr = sys_addr & 0x000000ffffffffffull;

	return ((addr >= get_dram_base(pvt, nid)) &&
		(addr <= get_dram_limit(pvt, nid)));
}

/*
 * Attempt to map a SysAddr to a node. On success, return a pointer to the
 * mem_ctl_info structure for the node that the SysAddr maps to.
 *
 * On failure, return NULL.
 */
static struct mem_ctl_info *find_mc_by_sys_addr(struct mem_ctl_info *mci,
						u64 sys_addr)
{
	struct amd64_pvt *pvt;
	u8 node_id;
	u32 intlv_en, bits;

	/*
	 * Here we use the DRAM Base (section 3.4.4.1) and DRAM Limit (section
	 * 3.4.4.2) registers to map the SysAddr to a node ID.
	 */
	pvt = mci->pvt_info;

	/*
	 * The value of this field should be the same for all DRAM Base
	 * registers.  Therefore we arbitrarily choose to read it from the
	 * register for node 0.
	 */
	intlv_en = dram_intlv_en(pvt, 0);

	if (intlv_en == 0) {
		for (node_id = 0; node_id < DRAM_RANGES; node_id++) {
			if (base_limit_match(pvt, sys_addr, node_id))
				goto found;
		}
		goto err_no_match;
	}

	if (unlikely((intlv_en != 0x01) &&
		     (intlv_en != 0x03) &&
		     (intlv_en != 0x07))) {
		amd64_warn("DRAM Base[IntlvEn] junk value: 0x%x, BIOS bug?\n", intlv_en);
		return NULL;
	}

	bits = (((u32) sys_addr) >> 12) & intlv_en;

	for (node_id = 0; ; ) {
		if ((dram_intlv_sel(pvt, node_id) & intlv_en) == bits)
			break;	/* intlv_sel field matches */

		if (++node_id >= DRAM_RANGES)
			goto err_no_match;
	}

	/* sanity test for sys_addr */
	if (unlikely(!base_limit_match(pvt, sys_addr, node_id))) {
		amd64_warn("%s: sys_addr 0x%llx falls outside base/limit address"
			   "range for node %d with node interleaving enabled.\n",
			   __func__, sys_addr, node_id);
		return NULL;
	}

found:
	return edac_mc_find((int)node_id);

err_no_match:
	edac_dbg(2, "sys_addr 0x%lx doesn't match any node\n",
		 (unsigned long)sys_addr);

	return NULL;
}

/*
 * compute the CS base address of the @csrow on the DRAM controller @dct.
 * For details see F2x[5C:40] in the processor's BKDG
 */
static void get_cs_base_and_mask(struct amd64_pvt *pvt, int csrow, u8 dct,
				 u64 *base, u64 *mask)
{
	u64 csbase, csmask, base_bits, mask_bits;
	u8 addr_shift;

	if (pvt->fam == 0xf && pvt->ext_model < K8_REV_F) {
		csbase		= pvt->csels[dct].csbases[csrow];
		csmask		= pvt->csels[dct].csmasks[csrow];
		base_bits	= GENMASK_ULL(31, 21) | GENMASK_ULL(15, 9);
		mask_bits	= GENMASK_ULL(29, 21) | GENMASK_ULL(15, 9);
		addr_shift	= 4;

	/*
	 * F16h and F15h, models 30h and later need two addr_shift values:
	 * 8 for high and 6 for low (cf. F16h BKDG).
	 */
	} else if (pvt->fam == 0x16 ||
		  (pvt->fam == 0x15 && pvt->model >= 0x30)) {
		csbase          = pvt->csels[dct].csbases[csrow];
		csmask          = pvt->csels[dct].csmasks[csrow >> 1];

		*base  = (csbase & GENMASK_ULL(15,  5)) << 6;
		*base |= (csbase & GENMASK_ULL(30, 19)) << 8;

		*mask = ~0ULL;
		/* poke holes for the csmask */
		*mask &= ~((GENMASK_ULL(15, 5)  << 6) |
			   (GENMASK_ULL(30, 19) << 8));

		*mask |= (csmask & GENMASK_ULL(15, 5))  << 6;
		*mask |= (csmask & GENMASK_ULL(30, 19)) << 8;

		return;
	} else {
		csbase		= pvt->csels[dct].csbases[csrow];
		csmask		= pvt->csels[dct].csmasks[csrow >> 1];
		addr_shift	= 8;

		if (pvt->fam == 0x15)
			base_bits = mask_bits =
				GENMASK_ULL(30,19) | GENMASK_ULL(13,5);
		else
			base_bits = mask_bits =
				GENMASK_ULL(28,19) | GENMASK_ULL(13,5);
	}

	*base  = (csbase & base_bits) << addr_shift;

	*mask  = ~0ULL;
	/* poke holes for the csmask */
	*mask &= ~(mask_bits << addr_shift);
	/* OR them in */
	*mask |= (csmask & mask_bits) << addr_shift;
}

#define for_each_chip_select(i, dct, pvt) \
	for (i = 0; i < pvt->csels[dct].b_cnt; i++)

#define chip_select_base(i, dct, pvt) \
	pvt->csels[dct].csbases[i]

#define for_each_chip_select_mask(i, dct, pvt) \
	for (i = 0; i < pvt->csels[dct].m_cnt; i++)

#define for_each_umc(i) \
	for (i = 0; i < pvt->max_mcs; i++)

/*
 * @input_addr is an InputAddr associated with the node given by mci. Return the
 * csrow that input_addr maps to, or -1 on failure (no csrow claims input_addr).
 */
static int input_addr_to_csrow(struct mem_ctl_info *mci, u64 input_addr)
{
	struct amd64_pvt *pvt;
	int csrow;
	u64 base, mask;

	pvt = mci->pvt_info;

	for_each_chip_select(csrow, 0, pvt) {
		if (!csrow_enabled(csrow, 0, pvt))
			continue;

		get_cs_base_and_mask(pvt, csrow, 0, &base, &mask);

		mask = ~mask;

		if ((input_addr & mask) == (base & mask)) {
			edac_dbg(2, "InputAddr 0x%lx matches csrow %d (node %d)\n",
				 (unsigned long)input_addr, csrow,
				 pvt->mc_node_id);

			return csrow;
		}
	}
	edac_dbg(2, "no matching csrow for InputAddr 0x%lx (MC node %d)\n",
		 (unsigned long)input_addr, pvt->mc_node_id);

	return -1;
}

/*
 * Obtain info from the DRAM Hole Address Register (section 3.4.8, pub #26094)
 * for the node represented by mci. Info is passed back in *hole_base,
 * *hole_offset, and *hole_size.  Function returns 0 if info is valid or 1 if
 * info is invalid. Info may be invalid for either of the following reasons:
 *
 * - The revision of the node is not E or greater.  In this case, the DRAM Hole
 *   Address Register does not exist.
 *
 * - The DramHoleValid bit is cleared in the DRAM Hole Address Register,
 *   indicating that its contents are not valid.
 *
 * The values passed back in *hole_base, *hole_offset, and *hole_size are
 * complete 32-bit values despite the fact that the bitfields in the DHAR
 * only represent bits 31-24 of the base and offset values.
 */
static int get_dram_hole_info(struct mem_ctl_info *mci, u64 *hole_base,
			      u64 *hole_offset, u64 *hole_size)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	/* only revE and later have the DRAM Hole Address Register */
	if (pvt->fam == 0xf && pvt->ext_model < K8_REV_E) {
		edac_dbg(1, "  revision %d for node %d does not support DHAR\n",
			 pvt->ext_model, pvt->mc_node_id);
		return 1;
	}

	/* valid for Fam10h and above */
	if (pvt->fam >= 0x10 && !dhar_mem_hoist_valid(pvt)) {
		edac_dbg(1, "  Dram Memory Hoisting is DISABLED on this system\n");
		return 1;
	}

	if (!dhar_valid(pvt)) {
		edac_dbg(1, "  Dram Memory Hoisting is DISABLED on this node %d\n",
			 pvt->mc_node_id);
		return 1;
	}

	/* This node has Memory Hoisting */

	/* +------------------+--------------------+--------------------+-----
	 * | memory           | DRAM hole          | relocated          |
	 * | [0, (x - 1)]     | [x, 0xffffffff]    | addresses from     |
	 * |                  |                    | DRAM hole          |
	 * |                  |                    | [0x100000000,      |
	 * |                  |                    |  (0x100000000+     |
	 * |                  |                    |   (0xffffffff-x))] |
	 * +------------------+--------------------+--------------------+-----
	 *
	 * Above is a diagram of physical memory showing the DRAM hole and the
	 * relocated addresses from the DRAM hole.  As shown, the DRAM hole
	 * starts at address x (the base address) and extends through address
	 * 0xffffffff.  The DRAM Hole Address Register (DHAR) relocates the
	 * addresses in the hole so that they start at 0x100000000.
	 */

	*hole_base = dhar_base(pvt);
	*hole_size = (1ULL << 32) - *hole_base;

	*hole_offset = (pvt->fam > 0xf) ? f10_dhar_offset(pvt)
					: k8_dhar_offset(pvt);

	edac_dbg(1, "  DHAR info for node %d base 0x%lx offset 0x%lx size 0x%lx\n",
		 pvt->mc_node_id, (unsigned long)*hole_base,
		 (unsigned long)*hole_offset, (unsigned long)*hole_size);

	return 0;
}

#ifdef CONFIG_EDAC_DEBUG
#define EDAC_DCT_ATTR_SHOW(reg)						\
static ssize_t reg##_show(struct device *dev,				\
			 struct device_attribute *mattr, char *data)	\
{									\
	struct mem_ctl_info *mci = to_mci(dev);				\
	struct amd64_pvt *pvt = mci->pvt_info;				\
									\
	return sprintf(data, "0x%016llx\n", (u64)pvt->reg);		\
}

EDAC_DCT_ATTR_SHOW(dhar);
EDAC_DCT_ATTR_SHOW(dbam0);
EDAC_DCT_ATTR_SHOW(top_mem);
EDAC_DCT_ATTR_SHOW(top_mem2);

static ssize_t dram_hole_show(struct device *dev, struct device_attribute *mattr,
			      char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	u64 hole_base = 0;
	u64 hole_offset = 0;
	u64 hole_size = 0;

	get_dram_hole_info(mci, &hole_base, &hole_offset, &hole_size);

	return sprintf(data, "%llx %llx %llx\n", hole_base, hole_offset,
						 hole_size);
}

/*
 * update NUM_DBG_ATTRS in case you add new members
 */
static DEVICE_ATTR(dhar, S_IRUGO, dhar_show, NULL);
static DEVICE_ATTR(dbam, S_IRUGO, dbam0_show, NULL);
static DEVICE_ATTR(topmem, S_IRUGO, top_mem_show, NULL);
static DEVICE_ATTR(topmem2, S_IRUGO, top_mem2_show, NULL);
static DEVICE_ATTR_RO(dram_hole);

static struct attribute *dbg_attrs[] = {
	&dev_attr_dhar.attr,
	&dev_attr_dbam.attr,
	&dev_attr_topmem.attr,
	&dev_attr_topmem2.attr,
	&dev_attr_dram_hole.attr,
	NULL
};

static const struct attribute_group dbg_group = {
	.attrs = dbg_attrs,
};

static ssize_t inject_section_show(struct device *dev,
				   struct device_attribute *mattr, char *buf)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct amd64_pvt *pvt = mci->pvt_info;
	return sprintf(buf, "0x%x\n", pvt->injection.section);
}

/*
 * store error injection section value which refers to one of 4 16-byte sections
 * within a 64-byte cacheline
 *
 * range: 0..3
 */
static ssize_t inject_section_store(struct device *dev,
				    struct device_attribute *mattr,
				    const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct amd64_pvt *pvt = mci->pvt_info;
	unsigned long value;
	int ret;

	ret = kstrtoul(data, 10, &value);
	if (ret < 0)
		return ret;

	if (value > 3) {
		amd64_warn("%s: invalid section 0x%lx\n", __func__, value);
		return -EINVAL;
	}

	pvt->injection.section = (u32) value;
	return count;
}

static ssize_t inject_word_show(struct device *dev,
				struct device_attribute *mattr, char *buf)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct amd64_pvt *pvt = mci->pvt_info;
	return sprintf(buf, "0x%x\n", pvt->injection.word);
}

/*
 * store error injection word value which refers to one of 9 16-bit word of the
 * 16-byte (128-bit + ECC bits) section
 *
 * range: 0..8
 */
static ssize_t inject_word_store(struct device *dev,
				 struct device_attribute *mattr,
				 const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct amd64_pvt *pvt = mci->pvt_info;
	unsigned long value;
	int ret;

	ret = kstrtoul(data, 10, &value);
	if (ret < 0)
		return ret;

	if (value > 8) {
		amd64_warn("%s: invalid word 0x%lx\n", __func__, value);
		return -EINVAL;
	}

	pvt->injection.word = (u32) value;
	return count;
}

static ssize_t inject_ecc_vector_show(struct device *dev,
				      struct device_attribute *mattr,
				      char *buf)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct amd64_pvt *pvt = mci->pvt_info;
	return sprintf(buf, "0x%x\n", pvt->injection.bit_map);
}

/*
 * store 16 bit error injection vector which enables injecting errors to the
 * corresponding bit within the error injection word above. When used during a
 * DRAM ECC read, it holds the contents of the of the DRAM ECC bits.
 */
static ssize_t inject_ecc_vector_store(struct device *dev,
				       struct device_attribute *mattr,
				       const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct amd64_pvt *pvt = mci->pvt_info;
	unsigned long value;
	int ret;

	ret = kstrtoul(data, 16, &value);
	if (ret < 0)
		return ret;

	if (value & 0xFFFF0000) {
		amd64_warn("%s: invalid EccVector: 0x%lx\n", __func__, value);
		return -EINVAL;
	}

	pvt->injection.bit_map = (u32) value;
	return count;
}

/*
 * Do a DRAM ECC read. Assemble staged values in the pvt area, format into
 * fields needed by the injection registers and read the NB Array Data Port.
 */
static ssize_t inject_read_store(struct device *dev,
				 struct device_attribute *mattr,
				 const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct amd64_pvt *pvt = mci->pvt_info;
	unsigned long value;
	u32 section, word_bits;
	int ret;

	ret = kstrtoul(data, 10, &value);
	if (ret < 0)
		return ret;

	/* Form value to choose 16-byte section of cacheline */
	section = F10_NB_ARRAY_DRAM | SET_NB_ARRAY_ADDR(pvt->injection.section);

	amd64_write_pci_cfg(pvt->F3, F10_NB_ARRAY_ADDR, section);

	word_bits = SET_NB_DRAM_INJECTION_READ(pvt->injection);

	/* Issue 'word' and 'bit' along with the READ request */
	amd64_write_pci_cfg(pvt->F3, F10_NB_ARRAY_DATA, word_bits);

	edac_dbg(0, "section=0x%x word_bits=0x%x\n", section, word_bits);

	return count;
}

/*
 * Do a DRAM ECC write. Assemble staged values in the pvt area and format into
 * fields needed by the injection registers.
 */
static ssize_t inject_write_store(struct device *dev,
				  struct device_attribute *mattr,
				  const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct amd64_pvt *pvt = mci->pvt_info;
	u32 section, word_bits, tmp;
	unsigned long value;
	int ret;

	ret = kstrtoul(data, 10, &value);
	if (ret < 0)
		return ret;

	/* Form value to choose 16-byte section of cacheline */
	section = F10_NB_ARRAY_DRAM | SET_NB_ARRAY_ADDR(pvt->injection.section);

	amd64_write_pci_cfg(pvt->F3, F10_NB_ARRAY_ADDR, section);

	word_bits = SET_NB_DRAM_INJECTION_WRITE(pvt->injection);

	pr_notice_once("Don't forget to decrease MCE polling interval in\n"
			"/sys/bus/machinecheck/devices/machinecheck<CPUNUM>/check_interval\n"
			"so that you can get the error report faster.\n");

	on_each_cpu(disable_caches, NULL, 1);

	/* Issue 'word' and 'bit' along with the READ request */
	amd64_write_pci_cfg(pvt->F3, F10_NB_ARRAY_DATA, word_bits);

 retry:
	/* wait until injection happens */
	amd64_read_pci_cfg(pvt->F3, F10_NB_ARRAY_DATA, &tmp);
	if (tmp & F10_NB_ARR_ECC_WR_REQ) {
		cpu_relax();
		goto retry;
	}

	on_each_cpu(enable_caches, NULL, 1);

	edac_dbg(0, "section=0x%x word_bits=0x%x\n", section, word_bits);

	return count;
}

/*
 * update NUM_INJ_ATTRS in case you add new members
 */

static DEVICE_ATTR_RW(inject_section);
static DEVICE_ATTR_RW(inject_word);
static DEVICE_ATTR_RW(inject_ecc_vector);
static DEVICE_ATTR_WO(inject_write);
static DEVICE_ATTR_WO(inject_read);

static struct attribute *inj_attrs[] = {
	&dev_attr_inject_section.attr,
	&dev_attr_inject_word.attr,
	&dev_attr_inject_ecc_vector.attr,
	&dev_attr_inject_write.attr,
	&dev_attr_inject_read.attr,
	NULL
};

static umode_t inj_is_visible(struct kobject *kobj, struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct mem_ctl_info *mci = container_of(dev, struct mem_ctl_info, dev);
	struct amd64_pvt *pvt = mci->pvt_info;

	/* Families which have that injection hw */
	if (pvt->fam >= 0x10 && pvt->fam <= 0x16)
		return attr->mode;

	return 0;
}

static const struct attribute_group inj_group = {
	.attrs = inj_attrs,
	.is_visible = inj_is_visible,
};
#endif /* CONFIG_EDAC_DEBUG */

/*
 * Return the DramAddr that the SysAddr given by @sys_addr maps to.  It is
 * assumed that sys_addr maps to the node given by mci.
 *
 * The first part of section 3.4.4 (p. 70) shows how the DRAM Base (section
 * 3.4.4.1) and DRAM Limit (section 3.4.4.2) registers are used to translate a
 * SysAddr to a DramAddr. If the DRAM Hole Address Register (DHAR) is enabled,
 * then it is also involved in translating a SysAddr to a DramAddr. Sections
 * 3.4.8 and 3.5.8.2 describe the DHAR and how it is used for memory hoisting.
 * These parts of the documentation are unclear. I interpret them as follows:
 *
 * When node n receives a SysAddr, it processes the SysAddr as follows:
 *
 * 1. It extracts the DRAMBase and DRAMLimit values from the DRAM Base and DRAM
 *    Limit registers for node n. If the SysAddr is not within the range
 *    specified by the base and limit values, then node n ignores the Sysaddr
 *    (since it does not map to node n). Otherwise continue to step 2 below.
 *
 * 2. If the DramHoleValid bit of the DHAR for node n is clear, the DHAR is
 *    disabled so skip to step 3 below. Otherwise see if the SysAddr is within
 *    the range of relocated addresses (starting at 0x100000000) from the DRAM
 *    hole. If not, skip to step 3 below. Else get the value of the
 *    DramHoleOffset field from the DHAR. To obtain the DramAddr, subtract the
 *    offset defined by this value from the SysAddr.
 *
 * 3. Obtain the base address for node n from the DRAMBase field of the DRAM
 *    Base register for node n. To obtain the DramAddr, subtract the base
 *    address from the SysAddr, as shown near the start of section 3.4.4 (p.70).
 */
static u64 sys_addr_to_dram_addr(struct mem_ctl_info *mci, u64 sys_addr)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	u64 dram_base, hole_base, hole_offset, hole_size, dram_addr;
	int ret;

	dram_base = get_dram_base(pvt, pvt->mc_node_id);

	ret = get_dram_hole_info(mci, &hole_base, &hole_offset, &hole_size);
	if (!ret) {
		if ((sys_addr >= (1ULL << 32)) &&
		    (sys_addr < ((1ULL << 32) + hole_size))) {
			/* use DHAR to translate SysAddr to DramAddr */
			dram_addr = sys_addr - hole_offset;

			edac_dbg(2, "using DHAR to translate SysAddr 0x%lx to DramAddr 0x%lx\n",
				 (unsigned long)sys_addr,
				 (unsigned long)dram_addr);

			return dram_addr;
		}
	}

	/*
	 * Translate the SysAddr to a DramAddr as shown near the start of
	 * section 3.4.4 (p. 70).  Although sys_addr is a 64-bit value, the k8
	 * only deals with 40-bit values.  Therefore we discard bits 63-40 of
	 * sys_addr below.  If bit 39 of sys_addr is 1 then the bits we
	 * discard are all 1s.  Otherwise the bits we discard are all 0s.  See
	 * section 3.4.2 of AMD publication 24592: AMD x86-64 Architecture
	 * Programmer's Manual Volume 1 Application Programming.
	 */
	dram_addr = (sys_addr & GENMASK_ULL(39, 0)) - dram_base;

	edac_dbg(2, "using DRAM Base register to translate SysAddr 0x%lx to DramAddr 0x%lx\n",
		 (unsigned long)sys_addr, (unsigned long)dram_addr);
	return dram_addr;
}

/*
 * @intlv_en is the value of the IntlvEn field from a DRAM Base register
 * (section 3.4.4.1).  Return the number of bits from a SysAddr that are used
 * for node interleaving.
 */
static int num_node_interleave_bits(unsigned intlv_en)
{
	static const int intlv_shift_table[] = { 0, 1, 0, 2, 0, 0, 0, 3 };
	int n;

	BUG_ON(intlv_en > 7);
	n = intlv_shift_table[intlv_en];
	return n;
}

/* Translate the DramAddr given by @dram_addr to an InputAddr. */
static u64 dram_addr_to_input_addr(struct mem_ctl_info *mci, u64 dram_addr)
{
	struct amd64_pvt *pvt;
	int intlv_shift;
	u64 input_addr;

	pvt = mci->pvt_info;

	/*
	 * See the start of section 3.4.4 (p. 70, BKDG #26094, K8, revA-E)
	 * concerning translating a DramAddr to an InputAddr.
	 */
	intlv_shift = num_node_interleave_bits(dram_intlv_en(pvt, 0));
	input_addr = ((dram_addr >> intlv_shift) & GENMASK_ULL(35, 12)) +
		      (dram_addr & 0xfff);

	edac_dbg(2, "  Intlv Shift=%d DramAddr=0x%lx maps to InputAddr=0x%lx\n",
		 intlv_shift, (unsigned long)dram_addr,
		 (unsigned long)input_addr);

	return input_addr;
}

/*
 * Translate the SysAddr represented by @sys_addr to an InputAddr.  It is
 * assumed that @sys_addr maps to the node given by mci.
 */
static u64 sys_addr_to_input_addr(struct mem_ctl_info *mci, u64 sys_addr)
{
	u64 input_addr;

	input_addr =
	    dram_addr_to_input_addr(mci, sys_addr_to_dram_addr(mci, sys_addr));

	edac_dbg(2, "SysAddr 0x%lx translates to InputAddr 0x%lx\n",
		 (unsigned long)sys_addr, (unsigned long)input_addr);

	return input_addr;
}

/* Map the Error address to a PAGE and PAGE OFFSET. */
static inline void error_address_to_page_and_offset(u64 error_address,
						    struct err_info *err)
{
	err->page = (u32) (error_address >> PAGE_SHIFT);
	err->offset = ((u32) error_address) & ~PAGE_MASK;
}

/*
 * @sys_addr is an error address (a SysAddr) extracted from the MCA NB Address
 * Low (section 3.6.4.5) and MCA NB Address High (section 3.6.4.6) registers
 * of a node that detected an ECC memory error.  mci represents the node that
 * the error address maps to (possibly different from the node that detected
 * the error).  Return the number of the csrow that sys_addr maps to, or -1 on
 * error.
 */
static int sys_addr_to_csrow(struct mem_ctl_info *mci, u64 sys_addr)
{
	int csrow;

	csrow = input_addr_to_csrow(mci, sys_addr_to_input_addr(mci, sys_addr));

	if (csrow == -1)
		amd64_mc_err(mci, "Failed to translate InputAddr to csrow for "
				  "address 0x%lx\n", (unsigned long)sys_addr);
	return csrow;
}

/*
 * See AMD PPR DF::LclNodeTypeMap
 *
 * This register gives information for nodes of the same type within a system.
 *
 * Reading this register from a GPU node will tell how many GPU nodes are in the
 * system and what the lowest AMD Node ID value is for the GPU nodes. Use this
 * info to fixup the Linux logical "Node ID" value set in the AMD NB code and EDAC.
 */
static struct local_node_map {
	u16 node_count;
	u16 base_node_id;
} gpu_node_map;

#define PCI_DEVICE_ID_AMD_MI200_DF_F1		0x14d1
#define REG_LOCAL_NODE_TYPE_MAP			0x144

/* Local Node Type Map (LNTM) fields */
#define LNTM_NODE_COUNT				GENMASK(27, 16)
#define LNTM_BASE_NODE_ID			GENMASK(11, 0)

static int gpu_get_node_map(struct amd64_pvt *pvt)
{
	struct pci_dev *pdev;
	int ret;
	u32 tmp;

	/*
	 * Mapping of nodes from hardware-provided AMD Node ID to a
	 * Linux logical one is applicable for MI200 models. Therefore,
	 * return early for other heterogeneous systems.
	 */
	if (pvt->F3->device != PCI_DEVICE_ID_AMD_MI200_DF_F3)
		return 0;

	/*
	 * Node ID 0 is reserved for CPUs. Therefore, a non-zero Node ID
	 * means the values have been already cached.
	 */
	if (gpu_node_map.base_node_id)
		return 0;

	pdev = pci_get_device(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_MI200_DF_F1, NULL);
	if (!pdev) {
		ret = -ENODEV;
		goto out;
	}

	ret = pci_read_config_dword(pdev, REG_LOCAL_NODE_TYPE_MAP, &tmp);
	if (ret) {
		ret = pcibios_err_to_errno(ret);
		goto out;
	}

	gpu_node_map.node_count = FIELD_GET(LNTM_NODE_COUNT, tmp);
	gpu_node_map.base_node_id = FIELD_GET(LNTM_BASE_NODE_ID, tmp);

out:
	pci_dev_put(pdev);
	return ret;
}

static int fixup_node_id(int node_id, struct mce *m)
{
	/* MCA_IPID[InstanceIdHi] give the AMD Node ID for the bank. */
	u8 nid = (m->ipid >> 44) & 0xF;

	if (smca_get_bank_type(m->extcpu, m->bank) != SMCA_UMC_V2)
		return node_id;

	/* Nodes below the GPU base node are CPU nodes and don't need a fixup. */
	if (nid < gpu_node_map.base_node_id)
		return node_id;

	/* Convert the hardware-provided AMD Node ID to a Linux logical one. */
	return nid - gpu_node_map.base_node_id + 1;
}

static int get_channel_from_ecc_syndrome(struct mem_ctl_info *, u16);

/*
 * Determine if the DIMMs have ECC enabled. ECC is enabled ONLY if all the DIMMs
 * are ECC capable.
 */
static unsigned long dct_determine_edac_cap(struct amd64_pvt *pvt)
{
	unsigned long edac_cap = EDAC_FLAG_NONE;
	u8 bit;

	bit = (pvt->fam > 0xf || pvt->ext_model >= K8_REV_F)
		? 19
		: 17;

	if (pvt->dclr0 & BIT(bit))
		edac_cap = EDAC_FLAG_SECDED;

	return edac_cap;
}

static unsigned long umc_determine_edac_cap(struct amd64_pvt *pvt)
{
	u8 i, umc_en_mask = 0, dimm_ecc_en_mask = 0;
	unsigned long edac_cap = EDAC_FLAG_NONE;

	for_each_umc(i) {
		if (!(pvt->umc[i].sdp_ctrl & UMC_SDP_INIT))
			continue;

		umc_en_mask |= BIT(i);

		/* UMC Configuration bit 12 (DimmEccEn) */
		if (pvt->umc[i].umc_cfg & BIT(12))
			dimm_ecc_en_mask |= BIT(i);
	}

	if (umc_en_mask == dimm_ecc_en_mask)
		edac_cap = EDAC_FLAG_SECDED;

	return edac_cap;
}

/*
 * debug routine to display the memory sizes of all logical DIMMs and its
 * CSROWs
 */
static void dct_debug_display_dimm_sizes(struct amd64_pvt *pvt, u8 ctrl)
{
	u32 *dcsb = ctrl ? pvt->csels[1].csbases : pvt->csels[0].csbases;
	u32 dbam  = ctrl ? pvt->dbam1 : pvt->dbam0;
	int dimm, size0, size1;

	if (pvt->fam == 0xf) {
		/* K8 families < revF not supported yet */
		if (pvt->ext_model < K8_REV_F)
			return;

		WARN_ON(ctrl != 0);
	}

	if (pvt->fam == 0x10) {
		dbam = (ctrl && !dct_ganging_enabled(pvt)) ? pvt->dbam1
							   : pvt->dbam0;
		dcsb = (ctrl && !dct_ganging_enabled(pvt)) ?
				 pvt->csels[1].csbases :
				 pvt->csels[0].csbases;
	} else if (ctrl) {
		dbam = pvt->dbam0;
		dcsb = pvt->csels[1].csbases;
	}
	edac_dbg(1, "F2x%d80 (DRAM Bank Address Mapping): 0x%08x\n",
		 ctrl, dbam);

	edac_printk(KERN_DEBUG, EDAC_MC, "DCT%d chip selects:\n", ctrl);

	/* Dump memory sizes for DIMM and its CSROWs */
	for (dimm = 0; dimm < 4; dimm++) {
		size0 = 0;
		if (dcsb[dimm * 2] & DCSB_CS_ENABLE)
			/*
			 * For F15m60h, we need multiplier for LRDIMM cs_size
			 * calculation. We pass dimm value to the dbam_to_cs
			 * mapper so we can find the multiplier from the
			 * corresponding DCSM.
			 */
			size0 = pvt->ops->dbam_to_cs(pvt, ctrl,
						     DBAM_DIMM(dimm, dbam),
						     dimm);

		size1 = 0;
		if (dcsb[dimm * 2 + 1] & DCSB_CS_ENABLE)
			size1 = pvt->ops->dbam_to_cs(pvt, ctrl,
						     DBAM_DIMM(dimm, dbam),
						     dimm);

		amd64_info(EDAC_MC ": %d: %5dMB %d: %5dMB\n",
			   dimm * 2,     size0,
			   dimm * 2 + 1, size1);
	}
}


static void debug_dump_dramcfg_low(struct amd64_pvt *pvt, u32 dclr, int chan)
{
	edac_dbg(1, "F2x%d90 (DRAM Cfg Low): 0x%08x\n", chan, dclr);

	if (pvt->dram_type == MEM_LRDDR3) {
		u32 dcsm = pvt->csels[chan].csmasks[0];
		/*
		 * It's assumed all LRDIMMs in a DCT are going to be of
		 * same 'type' until proven otherwise. So, use a cs
		 * value of '0' here to get dcsm value.
		 */
		edac_dbg(1, " LRDIMM %dx rank multiply\n", (dcsm & 0x3));
	}

	edac_dbg(1, "All DIMMs support ECC:%s\n",
		    (dclr & BIT(19)) ? "yes" : "no");


	edac_dbg(1, "  PAR/ERR parity: %s\n",
		 (dclr & BIT(8)) ?  "enabled" : "disabled");

	if (pvt->fam == 0x10)
		edac_dbg(1, "  DCT 128bit mode width: %s\n",
			 (dclr & BIT(11)) ?  "128b" : "64b");

	edac_dbg(1, "  x4 logical DIMMs present: L0: %s L1: %s L2: %s L3: %s\n",
		 (dclr & BIT(12)) ?  "yes" : "no",
		 (dclr & BIT(13)) ?  "yes" : "no",
		 (dclr & BIT(14)) ?  "yes" : "no",
		 (dclr & BIT(15)) ?  "yes" : "no");
}

#define CS_EVEN_PRIMARY		BIT(0)
#define CS_ODD_PRIMARY		BIT(1)
#define CS_EVEN_SECONDARY	BIT(2)
#define CS_ODD_SECONDARY	BIT(3)
#define CS_3R_INTERLEAVE	BIT(4)

#define CS_EVEN			(CS_EVEN_PRIMARY | CS_EVEN_SECONDARY)
#define CS_ODD			(CS_ODD_PRIMARY | CS_ODD_SECONDARY)

static int umc_get_cs_mode(int dimm, u8 ctrl, struct amd64_pvt *pvt)
{
	u8 base, count = 0;
	int cs_mode = 0;

	if (csrow_enabled(2 * dimm, ctrl, pvt))
		cs_mode |= CS_EVEN_PRIMARY;

	if (csrow_enabled(2 * dimm + 1, ctrl, pvt))
		cs_mode |= CS_ODD_PRIMARY;

	/* Asymmetric dual-rank DIMM support. */
	if (csrow_sec_enabled(2 * dimm + 1, ctrl, pvt))
		cs_mode |= CS_ODD_SECONDARY;

	/*
	 * 3 Rank inteleaving support.
	 * There should be only three bases enabled and their two masks should
	 * be equal.
	 */
	for_each_chip_select(base, ctrl, pvt)
		count += csrow_enabled(base, ctrl, pvt);

	if (count == 3 &&
	    pvt->csels[ctrl].csmasks[0] == pvt->csels[ctrl].csmasks[1]) {
		edac_dbg(1, "3R interleaving in use.\n");
		cs_mode |= CS_3R_INTERLEAVE;
	}

	return cs_mode;
}

static int __addr_mask_to_cs_size(u32 addr_mask_orig, unsigned int cs_mode,
				  int csrow_nr, int dimm)
{
	u32 msb, weight, num_zero_bits;
	u32 addr_mask_deinterleaved;
	int size = 0;

	/*
	 * The number of zero bits in the mask is equal to the number of bits
	 * in a full mask minus the number of bits in the current mask.
	 *
	 * The MSB is the number of bits in the full mask because BIT[0] is
	 * always 0.
	 *
	 * In the special 3 Rank interleaving case, a single bit is flipped
	 * without swapping with the most significant bit. This can be handled
	 * by keeping the MSB where it is and ignoring the single zero bit.
	 */
	msb = fls(addr_mask_orig) - 1;
	weight = hweight_long(addr_mask_orig);
	num_zero_bits = msb - weight - !!(cs_mode & CS_3R_INTERLEAVE);

	/* Take the number of zero bits off from the top of the mask. */
	addr_mask_deinterleaved = GENMASK_ULL(msb - num_zero_bits, 1);

	edac_dbg(1, "CS%d DIMM%d AddrMasks:\n", csrow_nr, dimm);
	edac_dbg(1, "  Original AddrMask: 0x%x\n", addr_mask_orig);
	edac_dbg(1, "  Deinterleaved AddrMask: 0x%x\n", addr_mask_deinterleaved);

	/* Register [31:1] = Address [39:9]. Size is in kBs here. */
	size = (addr_mask_deinterleaved >> 2) + 1;

	/* Return size in MBs. */
	return size >> 10;
}

static int umc_addr_mask_to_cs_size(struct amd64_pvt *pvt, u8 umc,
				    unsigned int cs_mode, int csrow_nr)
{
	int cs_mask_nr = csrow_nr;
	u32 addr_mask_orig;
	int dimm, size = 0;

	/* No Chip Selects are enabled. */
	if (!cs_mode)
		return size;

	/* Requested size of an even CS but none are enabled. */
	if (!(cs_mode & CS_EVEN) && !(csrow_nr & 1))
		return size;

	/* Requested size of an odd CS but none are enabled. */
	if (!(cs_mode & CS_ODD) && (csrow_nr & 1))
		return size;

	/*
	 * Family 17h introduced systems with one mask per DIMM,
	 * and two Chip Selects per DIMM.
	 *
	 *	CS0 and CS1 -> MASK0 / DIMM0
	 *	CS2 and CS3 -> MASK1 / DIMM1
	 *
	 * Family 19h Model 10h introduced systems with one mask per Chip Select,
	 * and two Chip Selects per DIMM.
	 *
	 *	CS0 -> MASK0 -> DIMM0
	 *	CS1 -> MASK1 -> DIMM0
	 *	CS2 -> MASK2 -> DIMM1
	 *	CS3 -> MASK3 -> DIMM1
	 *
	 * Keep the mask number equal to the Chip Select number for newer systems,
	 * and shift the mask number for older systems.
	 */
	dimm = csrow_nr >> 1;

	if (!pvt->flags.zn_regs_v2)
		cs_mask_nr >>= 1;

	/* Asymmetric dual-rank DIMM support. */
	if ((csrow_nr & 1) && (cs_mode & CS_ODD_SECONDARY))
		addr_mask_orig = pvt->csels[umc].csmasks_sec[cs_mask_nr];
	else
		addr_mask_orig = pvt->csels[umc].csmasks[cs_mask_nr];

	return __addr_mask_to_cs_size(addr_mask_orig, cs_mode, csrow_nr, dimm);
}

static void umc_debug_display_dimm_sizes(struct amd64_pvt *pvt, u8 ctrl)
{
	int dimm, size0, size1, cs0, cs1, cs_mode;

	edac_printk(KERN_DEBUG, EDAC_MC, "UMC%d chip selects:\n", ctrl);

	for (dimm = 0; dimm < 2; dimm++) {
		cs0 = dimm * 2;
		cs1 = dimm * 2 + 1;

		cs_mode = umc_get_cs_mode(dimm, ctrl, pvt);

		size0 = umc_addr_mask_to_cs_size(pvt, ctrl, cs_mode, cs0);
		size1 = umc_addr_mask_to_cs_size(pvt, ctrl, cs_mode, cs1);

		amd64_info(EDAC_MC ": %d: %5dMB %d: %5dMB\n",
				cs0,	size0,
				cs1,	size1);
	}
}

static void umc_dump_misc_regs(struct amd64_pvt *pvt)
{
	struct amd64_umc *umc;
	u32 i;

	for_each_umc(i) {
		umc = &pvt->umc[i];

		edac_dbg(1, "UMC%d DIMM cfg: 0x%x\n", i, umc->dimm_cfg);
		edac_dbg(1, "UMC%d UMC cfg: 0x%x\n", i, umc->umc_cfg);
		edac_dbg(1, "UMC%d SDP ctrl: 0x%x\n", i, umc->sdp_ctrl);
		edac_dbg(1, "UMC%d ECC ctrl: 0x%x\n", i, umc->ecc_ctrl);
		edac_dbg(1, "UMC%d UMC cap high: 0x%x\n", i, umc->umc_cap_hi);

		edac_dbg(1, "UMC%d ECC capable: %s, ChipKill ECC capable: %s\n",
				i, (umc->umc_cap_hi & BIT(30)) ? "yes" : "no",
				    (umc->umc_cap_hi & BIT(31)) ? "yes" : "no");
		edac_dbg(1, "UMC%d All DIMMs support ECC: %s\n",
				i, (umc->umc_cfg & BIT(12)) ? "yes" : "no");
		edac_dbg(1, "UMC%d x4 DIMMs present: %s\n",
				i, (umc->dimm_cfg & BIT(6)) ? "yes" : "no");
		edac_dbg(1, "UMC%d x16 DIMMs present: %s\n",
				i, (umc->dimm_cfg & BIT(7)) ? "yes" : "no");

		umc_debug_display_dimm_sizes(pvt, i);
	}
}

static void dct_dump_misc_regs(struct amd64_pvt *pvt)
{
	edac_dbg(1, "F3xE8 (NB Cap): 0x%08x\n", pvt->nbcap);

	edac_dbg(1, "  NB two channel DRAM capable: %s\n",
		 (pvt->nbcap & NBCAP_DCT_DUAL) ? "yes" : "no");

	edac_dbg(1, "  ECC capable: %s, ChipKill ECC capable: %s\n",
		 (pvt->nbcap & NBCAP_SECDED) ? "yes" : "no",
		 (pvt->nbcap & NBCAP_CHIPKILL) ? "yes" : "no");

	debug_dump_dramcfg_low(pvt, pvt->dclr0, 0);

	edac_dbg(1, "F3xB0 (Online Spare): 0x%08x\n", pvt->online_spare);

	edac_dbg(1, "F1xF0 (DRAM Hole Address): 0x%08x, base: 0x%08x, offset: 0x%08x\n",
		 pvt->dhar, dhar_base(pvt),
		 (pvt->fam == 0xf) ? k8_dhar_offset(pvt)
				   : f10_dhar_offset(pvt));

	dct_debug_display_dimm_sizes(pvt, 0);

	/* everything below this point is Fam10h and above */
	if (pvt->fam == 0xf)
		return;

	dct_debug_display_dimm_sizes(pvt, 1);

	/* Only if NOT ganged does dclr1 have valid info */
	if (!dct_ganging_enabled(pvt))
		debug_dump_dramcfg_low(pvt, pvt->dclr1, 1);

	edac_dbg(1, "  DramHoleValid: %s\n", dhar_valid(pvt) ? "yes" : "no");

	amd64_info("using x%u syndromes.\n", pvt->ecc_sym_sz);
}

/*
 * See BKDG, F2x[1,0][5C:40], F2[1,0][6C:60]
 */
static void dct_prep_chip_selects(struct amd64_pvt *pvt)
{
	if (pvt->fam == 0xf && pvt->ext_model < K8_REV_F) {
		pvt->csels[0].b_cnt = pvt->csels[1].b_cnt = 8;
		pvt->csels[0].m_cnt = pvt->csels[1].m_cnt = 8;
	} else if (pvt->fam == 0x15 && pvt->model == 0x30) {
		pvt->csels[0].b_cnt = pvt->csels[1].b_cnt = 4;
		pvt->csels[0].m_cnt = pvt->csels[1].m_cnt = 2;
	} else {
		pvt->csels[0].b_cnt = pvt->csels[1].b_cnt = 8;
		pvt->csels[0].m_cnt = pvt->csels[1].m_cnt = 4;
	}
}

static void umc_prep_chip_selects(struct amd64_pvt *pvt)
{
	int umc;

	for_each_umc(umc) {
		pvt->csels[umc].b_cnt = 4;
		pvt->csels[umc].m_cnt = pvt->flags.zn_regs_v2 ? 4 : 2;
	}
}

static void umc_read_base_mask(struct amd64_pvt *pvt)
{
	u32 umc_base_reg, umc_base_reg_sec;
	u32 umc_mask_reg, umc_mask_reg_sec;
	u32 base_reg, base_reg_sec;
	u32 mask_reg, mask_reg_sec;
	u32 *base, *base_sec;
	u32 *mask, *mask_sec;
	int cs, umc;
	u32 tmp;

	for_each_umc(umc) {
		umc_base_reg = get_umc_base(umc) + UMCCH_BASE_ADDR;
		umc_base_reg_sec = get_umc_base(umc) + UMCCH_BASE_ADDR_SEC;

		for_each_chip_select(cs, umc, pvt) {
			base = &pvt->csels[umc].csbases[cs];
			base_sec = &pvt->csels[umc].csbases_sec[cs];

			base_reg = umc_base_reg + (cs * 4);
			base_reg_sec = umc_base_reg_sec + (cs * 4);

			if (!amd_smn_read(pvt->mc_node_id, base_reg, &tmp)) {
				*base = tmp;
				edac_dbg(0, "  DCSB%d[%d]=0x%08x reg: 0x%x\n",
					 umc, cs, *base, base_reg);
			}

			if (!amd_smn_read(pvt->mc_node_id, base_reg_sec, &tmp)) {
				*base_sec = tmp;
				edac_dbg(0, "    DCSB_SEC%d[%d]=0x%08x reg: 0x%x\n",
					 umc, cs, *base_sec, base_reg_sec);
			}
		}

		umc_mask_reg = get_umc_base(umc) + UMCCH_ADDR_MASK;
		umc_mask_reg_sec = get_umc_base(umc) + get_umc_reg(pvt, UMCCH_ADDR_MASK_SEC);

		for_each_chip_select_mask(cs, umc, pvt) {
			mask = &pvt->csels[umc].csmasks[cs];
			mask_sec = &pvt->csels[umc].csmasks_sec[cs];

			mask_reg = umc_mask_reg + (cs * 4);
			mask_reg_sec = umc_mask_reg_sec + (cs * 4);

			if (!amd_smn_read(pvt->mc_node_id, mask_reg, &tmp)) {
				*mask = tmp;
				edac_dbg(0, "  DCSM%d[%d]=0x%08x reg: 0x%x\n",
					 umc, cs, *mask, mask_reg);
			}

			if (!amd_smn_read(pvt->mc_node_id, mask_reg_sec, &tmp)) {
				*mask_sec = tmp;
				edac_dbg(0, "    DCSM_SEC%d[%d]=0x%08x reg: 0x%x\n",
					 umc, cs, *mask_sec, mask_reg_sec);
			}
		}
	}
}

/*
 * Function 2 Offset F10_DCSB0; read in the DCS Base and DCS Mask registers
 */
static void dct_read_base_mask(struct amd64_pvt *pvt)
{
	int cs;

	for_each_chip_select(cs, 0, pvt) {
		int reg0   = DCSB0 + (cs * 4);
		int reg1   = DCSB1 + (cs * 4);
		u32 *base0 = &pvt->csels[0].csbases[cs];
		u32 *base1 = &pvt->csels[1].csbases[cs];

		if (!amd64_read_dct_pci_cfg(pvt, 0, reg0, base0))
			edac_dbg(0, "  DCSB0[%d]=0x%08x reg: F2x%x\n",
				 cs, *base0, reg0);

		if (pvt->fam == 0xf)
			continue;

		if (!amd64_read_dct_pci_cfg(pvt, 1, reg0, base1))
			edac_dbg(0, "  DCSB1[%d]=0x%08x reg: F2x%x\n",
				 cs, *base1, (pvt->fam == 0x10) ? reg1
							: reg0);
	}

	for_each_chip_select_mask(cs, 0, pvt) {
		int reg0   = DCSM0 + (cs * 4);
		int reg1   = DCSM1 + (cs * 4);
		u32 *mask0 = &pvt->csels[0].csmasks[cs];
		u32 *mask1 = &pvt->csels[1].csmasks[cs];

		if (!amd64_read_dct_pci_cfg(pvt, 0, reg0, mask0))
			edac_dbg(0, "    DCSM0[%d]=0x%08x reg: F2x%x\n",
				 cs, *mask0, reg0);

		if (pvt->fam == 0xf)
			continue;

		if (!amd64_read_dct_pci_cfg(pvt, 1, reg0, mask1))
			edac_dbg(0, "    DCSM1[%d]=0x%08x reg: F2x%x\n",
				 cs, *mask1, (pvt->fam == 0x10) ? reg1
							: reg0);
	}
}

static void umc_determine_memory_type(struct amd64_pvt *pvt)
{
	struct amd64_umc *umc;
	u32 i;

	for_each_umc(i) {
		umc = &pvt->umc[i];

		if (!(umc->sdp_ctrl & UMC_SDP_INIT)) {
			umc->dram_type = MEM_EMPTY;
			continue;
		}

		/*
		 * Check if the system supports the "DDR Type" field in UMC Config
		 * and has DDR5 DIMMs in use.
		 */
		if (pvt->flags.zn_regs_v2 && ((umc->umc_cfg & GENMASK(2, 0)) == 0x1)) {
			if (umc->dimm_cfg & BIT(5))
				umc->dram_type = MEM_LRDDR5;
			else if (umc->dimm_cfg & BIT(4))
				umc->dram_type = MEM_RDDR5;
			else
				umc->dram_type = MEM_DDR5;
		} else {
			if (umc->dimm_cfg & BIT(5))
				umc->dram_type = MEM_LRDDR4;
			else if (umc->dimm_cfg & BIT(4))
				umc->dram_type = MEM_RDDR4;
			else
				umc->dram_type = MEM_DDR4;
		}

		edac_dbg(1, "  UMC%d DIMM type: %s\n", i, edac_mem_types[umc->dram_type]);
	}
}

static void dct_determine_memory_type(struct amd64_pvt *pvt)
{
	u32 dram_ctrl, dcsm;

	switch (pvt->fam) {
	case 0xf:
		if (pvt->ext_model >= K8_REV_F)
			goto ddr3;

		pvt->dram_type = (pvt->dclr0 & BIT(18)) ? MEM_DDR : MEM_RDDR;
		return;

	case 0x10:
		if (pvt->dchr0 & DDR3_MODE)
			goto ddr3;

		pvt->dram_type = (pvt->dclr0 & BIT(16)) ? MEM_DDR2 : MEM_RDDR2;
		return;

	case 0x15:
		if (pvt->model < 0x60)
			goto ddr3;

		/*
		 * Model 0x60h needs special handling:
		 *
		 * We use a Chip Select value of '0' to obtain dcsm.
		 * Theoretically, it is possible to populate LRDIMMs of different
		 * 'Rank' value on a DCT. But this is not the common case. So,
		 * it's reasonable to assume all DIMMs are going to be of same
		 * 'type' until proven otherwise.
		 */
		amd64_read_dct_pci_cfg(pvt, 0, DRAM_CONTROL, &dram_ctrl);
		dcsm = pvt->csels[0].csmasks[0];

		if (((dram_ctrl >> 8) & 0x7) == 0x2)
			pvt->dram_type = MEM_DDR4;
		else if (pvt->dclr0 & BIT(16))
			pvt->dram_type = MEM_DDR3;
		else if (dcsm & 0x3)
			pvt->dram_type = MEM_LRDDR3;
		else
			pvt->dram_type = MEM_RDDR3;

		return;

	case 0x16:
		goto ddr3;

	default:
		WARN(1, KERN_ERR "%s: Family??? 0x%x\n", __func__, pvt->fam);
		pvt->dram_type = MEM_EMPTY;
	}

	edac_dbg(1, "  DIMM type: %s\n", edac_mem_types[pvt->dram_type]);
	return;

ddr3:
	pvt->dram_type = (pvt->dclr0 & BIT(16)) ? MEM_DDR3 : MEM_RDDR3;
}

/* On F10h and later ErrAddr is MC4_ADDR[47:1] */
static u64 get_error_address(struct amd64_pvt *pvt, struct mce *m)
{
	u16 mce_nid = topology_amd_node_id(m->extcpu);
	struct mem_ctl_info *mci;
	u8 start_bit = 1;
	u8 end_bit   = 47;
	u64 addr;

	mci = edac_mc_find(mce_nid);
	if (!mci)
		return 0;

	pvt = mci->pvt_info;

	if (pvt->fam == 0xf) {
		start_bit = 3;
		end_bit   = 39;
	}

	addr = m->addr & GENMASK_ULL(end_bit, start_bit);

	/*
	 * Erratum 637 workaround
	 */
	if (pvt->fam == 0x15) {
		u64 cc6_base, tmp_addr;
		u32 tmp;
		u8 intlv_en;

		if ((addr & GENMASK_ULL(47, 24)) >> 24 != 0x00fdf7)
			return addr;


		amd64_read_pci_cfg(pvt->F1, DRAM_LOCAL_NODE_LIM, &tmp);
		intlv_en = tmp >> 21 & 0x7;

		/* add [47:27] + 3 trailing bits */
		cc6_base  = (tmp & GENMASK_ULL(20, 0)) << 3;

		/* reverse and add DramIntlvEn */
		cc6_base |= intlv_en ^ 0x7;

		/* pin at [47:24] */
		cc6_base <<= 24;

		if (!intlv_en)
			return cc6_base | (addr & GENMASK_ULL(23, 0));

		amd64_read_pci_cfg(pvt->F1, DRAM_LOCAL_NODE_BASE, &tmp);

							/* faster log2 */
		tmp_addr  = (addr & GENMASK_ULL(23, 12)) << __fls(intlv_en + 1);

		/* OR DramIntlvSel into bits [14:12] */
		tmp_addr |= (tmp & GENMASK_ULL(23, 21)) >> 9;

		/* add remaining [11:0] bits from original MC4_ADDR */
		tmp_addr |= addr & GENMASK_ULL(11, 0);

		return cc6_base | tmp_addr;
	}

	return addr;
}

static struct pci_dev *pci_get_related_function(unsigned int vendor,
						unsigned int device,
						struct pci_dev *related)
{
	struct pci_dev *dev = NULL;

	while ((dev = pci_get_device(vendor, device, dev))) {
		if (pci_domain_nr(dev->bus) == pci_domain_nr(related->bus) &&
		    (dev->bus->number == related->bus->number) &&
		    (PCI_SLOT(dev->devfn) == PCI_SLOT(related->devfn)))
			break;
	}

	return dev;
}

static void read_dram_base_limit_regs(struct amd64_pvt *pvt, unsigned range)
{
	struct amd_northbridge *nb;
	struct pci_dev *f1 = NULL;
	unsigned int pci_func;
	int off = range << 3;
	u32 llim;

	amd64_read_pci_cfg(pvt->F1, DRAM_BASE_LO + off,  &pvt->ranges[range].base.lo);
	amd64_read_pci_cfg(pvt->F1, DRAM_LIMIT_LO + off, &pvt->ranges[range].lim.lo);

	if (pvt->fam == 0xf)
		return;

	if (!dram_rw(pvt, range))
		return;

	amd64_read_pci_cfg(pvt->F1, DRAM_BASE_HI + off,  &pvt->ranges[range].base.hi);
	amd64_read_pci_cfg(pvt->F1, DRAM_LIMIT_HI + off, &pvt->ranges[range].lim.hi);

	/* F15h: factor in CC6 save area by reading dst node's limit reg */
	if (pvt->fam != 0x15)
		return;

	nb = node_to_amd_nb(dram_dst_node(pvt, range));
	if (WARN_ON(!nb))
		return;

	if (pvt->model == 0x60)
		pci_func = PCI_DEVICE_ID_AMD_15H_M60H_NB_F1;
	else if (pvt->model == 0x30)
		pci_func = PCI_DEVICE_ID_AMD_15H_M30H_NB_F1;
	else
		pci_func = PCI_DEVICE_ID_AMD_15H_NB_F1;

	f1 = pci_get_related_function(nb->misc->vendor, pci_func, nb->misc);
	if (WARN_ON(!f1))
		return;

	amd64_read_pci_cfg(f1, DRAM_LOCAL_NODE_LIM, &llim);

	pvt->ranges[range].lim.lo &= GENMASK_ULL(15, 0);

				    /* {[39:27],111b} */
	pvt->ranges[range].lim.lo |= ((llim & 0x1fff) << 3 | 0x7) << 16;

	pvt->ranges[range].lim.hi &= GENMASK_ULL(7, 0);

				    /* [47:40] */
	pvt->ranges[range].lim.hi |= llim >> 13;

	pci_dev_put(f1);
}

static void k8_map_sysaddr_to_csrow(struct mem_ctl_info *mci, u64 sys_addr,
				    struct err_info *err)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	error_address_to_page_and_offset(sys_addr, err);

	/*
	 * Find out which node the error address belongs to. This may be
	 * different from the node that detected the error.
	 */
	err->src_mci = find_mc_by_sys_addr(mci, sys_addr);
	if (!err->src_mci) {
		amd64_mc_err(mci, "failed to map error addr 0x%lx to a node\n",
			     (unsigned long)sys_addr);
		err->err_code = ERR_NODE;
		return;
	}

	/* Now map the sys_addr to a CSROW */
	err->csrow = sys_addr_to_csrow(err->src_mci, sys_addr);
	if (err->csrow < 0) {
		err->err_code = ERR_CSROW;
		return;
	}

	/* CHIPKILL enabled */
	if (pvt->nbcfg & NBCFG_CHIPKILL) {
		err->channel = get_channel_from_ecc_syndrome(mci, err->syndrome);
		if (err->channel < 0) {
			/*
			 * Syndrome didn't map, so we don't know which of the
			 * 2 DIMMs is in error. So we need to ID 'both' of them
			 * as suspect.
			 */
			amd64_mc_warn(err->src_mci, "unknown syndrome 0x%04x - "
				      "possible error reporting race\n",
				      err->syndrome);
			err->err_code = ERR_CHANNEL;
			return;
		}
	} else {
		/*
		 * non-chipkill ecc mode
		 *
		 * The k8 documentation is unclear about how to determine the
		 * channel number when using non-chipkill memory.  This method
		 * was obtained from email communication with someone at AMD.
		 * (Wish the email was placed in this comment - norsk)
		 */
		err->channel = ((sys_addr & BIT(3)) != 0);
	}
}

static int ddr2_cs_size(unsigned i, bool dct_width)
{
	unsigned shift = 0;

	if (i <= 2)
		shift = i;
	else if (!(i & 0x1))
		shift = i >> 1;
	else
		shift = (i + 1) >> 1;

	return 128 << (shift + !!dct_width);
}

static int k8_dbam_to_chip_select(struct amd64_pvt *pvt, u8 dct,
				  unsigned cs_mode, int cs_mask_nr)
{
	u32 dclr = dct ? pvt->dclr1 : pvt->dclr0;

	if (pvt->ext_model >= K8_REV_F) {
		WARN_ON(cs_mode > 11);
		return ddr2_cs_size(cs_mode, dclr & WIDTH_128);
	}
	else if (pvt->ext_model >= K8_REV_D) {
		unsigned diff;
		WARN_ON(cs_mode > 10);

		/*
		 * the below calculation, besides trying to win an obfuscated C
		 * contest, maps cs_mode values to DIMM chip select sizes. The
		 * mappings are:
		 *
		 * cs_mode	CS size (mb)
		 * =======	============
		 * 0		32
		 * 1		64
		 * 2		128
		 * 3		128
		 * 4		256
		 * 5		512
		 * 6		256
		 * 7		512
		 * 8		1024
		 * 9		1024
		 * 10		2048
		 *
		 * Basically, it calculates a value with which to shift the
		 * smallest CS size of 32MB.
		 *
		 * ddr[23]_cs_size have a similar purpose.
		 */
		diff = cs_mode/3 + (unsigned)(cs_mode > 5);

		return 32 << (cs_mode - diff);
	}
	else {
		WARN_ON(cs_mode > 6);
		return 32 << cs_mode;
	}
}

static int ddr3_cs_size(unsigned i, bool dct_width)
{
	unsigned shift = 0;
	int cs_size = 0;

	if (i == 0 || i == 3 || i == 4)
		cs_size = -1;
	else if (i <= 2)
		shift = i;
	else if (i == 12)
		shift = 7;
	else if (!(i & 0x1))
		shift = i >> 1;
	else
		shift = (i + 1) >> 1;

	if (cs_size != -1)
		cs_size = (128 * (1 << !!dct_width)) << shift;

	return cs_size;
}

static int ddr3_lrdimm_cs_size(unsigned i, unsigned rank_multiply)
{
	unsigned shift = 0;
	int cs_size = 0;

	if (i < 4 || i == 6)
		cs_size = -1;
	else if (i == 12)
		shift = 7;
	else if (!(i & 0x1))
		shift = i >> 1;
	else
		shift = (i + 1) >> 1;

	if (cs_size != -1)
		cs_size = rank_multiply * (128 << shift);

	return cs_size;
}

static int ddr4_cs_size(unsigned i)
{
	int cs_size = 0;

	if (i == 0)
		cs_size = -1;
	else if (i == 1)
		cs_size = 1024;
	else
		/* Min cs_size = 1G */
		cs_size = 1024 * (1 << (i >> 1));

	return cs_size;
}

static int f10_dbam_to_chip_select(struct amd64_pvt *pvt, u8 dct,
				   unsigned cs_mode, int cs_mask_nr)
{
	u32 dclr = dct ? pvt->dclr1 : pvt->dclr0;

	WARN_ON(cs_mode > 11);

	if (pvt->dchr0 & DDR3_MODE || pvt->dchr1 & DDR3_MODE)
		return ddr3_cs_size(cs_mode, dclr & WIDTH_128);
	else
		return ddr2_cs_size(cs_mode, dclr & WIDTH_128);
}

/*
 * F15h supports only 64bit DCT interfaces
 */
static int f15_dbam_to_chip_select(struct amd64_pvt *pvt, u8 dct,
				   unsigned cs_mode, int cs_mask_nr)
{
	WARN_ON(cs_mode > 12);

	return ddr3_cs_size(cs_mode, false);
}

/* F15h M60h supports DDR4 mapping as well.. */
static int f15_m60h_dbam_to_chip_select(struct amd64_pvt *pvt, u8 dct,
					unsigned cs_mode, int cs_mask_nr)
{
	int cs_size;
	u32 dcsm = pvt->csels[dct].csmasks[cs_mask_nr];

	WARN_ON(cs_mode > 12);

	if (pvt->dram_type == MEM_DDR4) {
		if (cs_mode > 9)
			return -1;

		cs_size = ddr4_cs_size(cs_mode);
	} else if (pvt->dram_type == MEM_LRDDR3) {
		unsigned rank_multiply = dcsm & 0xf;

		if (rank_multiply == 3)
			rank_multiply = 4;
		cs_size = ddr3_lrdimm_cs_size(cs_mode, rank_multiply);
	} else {
		/* Minimum cs size is 512mb for F15hM60h*/
		if (cs_mode == 0x1)
			return -1;

		cs_size = ddr3_cs_size(cs_mode, false);
	}

	return cs_size;
}

/*
 * F16h and F15h model 30h have only limited cs_modes.
 */
static int f16_dbam_to_chip_select(struct amd64_pvt *pvt, u8 dct,
				unsigned cs_mode, int cs_mask_nr)
{
	WARN_ON(cs_mode > 12);

	if (cs_mode == 6 || cs_mode == 8 ||
	    cs_mode == 9 || cs_mode == 12)
		return -1;
	else
		return ddr3_cs_size(cs_mode, false);
}

static void read_dram_ctl_register(struct amd64_pvt *pvt)
{

	if (pvt->fam == 0xf)
		return;

	if (!amd64_read_pci_cfg(pvt->F2, DCT_SEL_LO, &pvt->dct_sel_lo)) {
		edac_dbg(0, "F2x110 (DCTSelLow): 0x%08x, High range addrs at: 0x%x\n",
			 pvt->dct_sel_lo, dct_sel_baseaddr(pvt));

		edac_dbg(0, "  DCTs operate in %s mode\n",
			 (dct_ganging_enabled(pvt) ? "ganged" : "unganged"));

		if (!dct_ganging_enabled(pvt))
			edac_dbg(0, "  Address range split per DCT: %s\n",
				 (dct_high_range_enabled(pvt) ? "yes" : "no"));

		edac_dbg(0, "  data interleave for ECC: %s, DRAM cleared since last warm reset: %s\n",
			 (dct_data_intlv_enabled(pvt) ? "enabled" : "disabled"),
			 (dct_memory_cleared(pvt) ? "yes" : "no"));

		edac_dbg(0, "  channel interleave: %s, "
			 "interleave bits selector: 0x%x\n",
			 (dct_interleave_enabled(pvt) ? "enabled" : "disabled"),
			 dct_sel_interleave_addr(pvt));
	}

	amd64_read_pci_cfg(pvt->F2, DCT_SEL_HI, &pvt->dct_sel_hi);
}

/*
 * Determine channel (DCT) based on the interleaving mode (see F15h M30h BKDG,
 * 2.10.12 Memory Interleaving Modes).
 */
static u8 f15_m30h_determine_channel(struct amd64_pvt *pvt, u64 sys_addr,
				     u8 intlv_en, int num_dcts_intlv,
				     u32 dct_sel)
{
	u8 channel = 0;
	u8 select;

	if (!(intlv_en))
		return (u8)(dct_sel);

	if (num_dcts_intlv == 2) {
		select = (sys_addr >> 8) & 0x3;
		channel = select ? 0x3 : 0;
	} else if (num_dcts_intlv == 4) {
		u8 intlv_addr = dct_sel_interleave_addr(pvt);
		switch (intlv_addr) {
		case 0x4:
			channel = (sys_addr >> 8) & 0x3;
			break;
		case 0x5:
			channel = (sys_addr >> 9) & 0x3;
			break;
		}
	}
	return channel;
}

/*
 * Determine channel (DCT) based on the interleaving mode: F10h BKDG, 2.8.9 Memory
 * Interleaving Modes.
 */
static u8 f1x_determine_channel(struct amd64_pvt *pvt, u64 sys_addr,
				bool hi_range_sel, u8 intlv_en)
{
	u8 dct_sel_high = (pvt->dct_sel_lo >> 1) & 1;

	if (dct_ganging_enabled(pvt))
		return 0;

	if (hi_range_sel)
		return dct_sel_high;

	/*
	 * see F2x110[DctSelIntLvAddr] - channel interleave mode
	 */
	if (dct_interleave_enabled(pvt)) {
		u8 intlv_addr = dct_sel_interleave_addr(pvt);

		/* return DCT select function: 0=DCT0, 1=DCT1 */
		if (!intlv_addr)
			return sys_addr >> 6 & 1;

		if (intlv_addr & 0x2) {
			u8 shift = intlv_addr & 0x1 ? 9 : 6;
			u32 temp = hweight_long((u32) ((sys_addr >> 16) & 0x1F)) & 1;

			return ((sys_addr >> shift) & 1) ^ temp;
		}

		if (intlv_addr & 0x4) {
			u8 shift = intlv_addr & 0x1 ? 9 : 8;

			return (sys_addr >> shift) & 1;
		}

		return (sys_addr >> (12 + hweight8(intlv_en))) & 1;
	}

	if (dct_high_range_enabled(pvt))
		return ~dct_sel_high & 1;

	return 0;
}

/* Convert the sys_addr to the normalized DCT address */
static u64 f1x_get_norm_dct_addr(struct amd64_pvt *pvt, u8 range,
				 u64 sys_addr, bool hi_rng,
				 u32 dct_sel_base_addr)
{
	u64 chan_off;
	u64 dram_base		= get_dram_base(pvt, range);
	u64 hole_off		= f10_dhar_offset(pvt);
	u64 dct_sel_base_off	= (u64)(pvt->dct_sel_hi & 0xFFFFFC00) << 16;

	if (hi_rng) {
		/*
		 * if
		 * base address of high range is below 4Gb
		 * (bits [47:27] at [31:11])
		 * DRAM address space on this DCT is hoisted above 4Gb	&&
		 * sys_addr > 4Gb
		 *
		 *	remove hole offset from sys_addr
		 * else
		 *	remove high range offset from sys_addr
		 */
		if ((!(dct_sel_base_addr >> 16) ||
		     dct_sel_base_addr < dhar_base(pvt)) &&
		    dhar_valid(pvt) &&
		    (sys_addr >= BIT_64(32)))
			chan_off = hole_off;
		else
			chan_off = dct_sel_base_off;
	} else {
		/*
		 * if
		 * we have a valid hole		&&
		 * sys_addr > 4Gb
		 *
		 *	remove hole
		 * else
		 *	remove dram base to normalize to DCT address
		 */
		if (dhar_valid(pvt) && (sys_addr >= BIT_64(32)))
			chan_off = hole_off;
		else
			chan_off = dram_base;
	}

	return (sys_addr & GENMASK_ULL(47,6)) - (chan_off & GENMASK_ULL(47,23));
}

/*
 * checks if the csrow passed in is marked as SPARED, if so returns the new
 * spare row
 */
static int f10_process_possible_spare(struct amd64_pvt *pvt, u8 dct, int csrow)
{
	int tmp_cs;

	if (online_spare_swap_done(pvt, dct) &&
	    csrow == online_spare_bad_dramcs(pvt, dct)) {

		for_each_chip_select(tmp_cs, dct, pvt) {
			if (chip_select_base(tmp_cs, dct, pvt) & 0x2) {
				csrow = tmp_cs;
				break;
			}
		}
	}
	return csrow;
}

/*
 * Iterate over the DRAM DCT "base" and "mask" registers looking for a
 * SystemAddr match on the specified 'ChannelSelect' and 'NodeID'
 *
 * Return:
 *	-EINVAL:  NOT FOUND
 *	0..csrow = Chip-Select Row
 */
static int f1x_lookup_addr_in_dct(u64 in_addr, u8 nid, u8 dct)
{
	struct mem_ctl_info *mci;
	struct amd64_pvt *pvt;
	u64 cs_base, cs_mask;
	int cs_found = -EINVAL;
	int csrow;

	mci = edac_mc_find(nid);
	if (!mci)
		return cs_found;

	pvt = mci->pvt_info;

	edac_dbg(1, "input addr: 0x%llx, DCT: %d\n", in_addr, dct);

	for_each_chip_select(csrow, dct, pvt) {
		if (!csrow_enabled(csrow, dct, pvt))
			continue;

		get_cs_base_and_mask(pvt, csrow, dct, &cs_base, &cs_mask);

		edac_dbg(1, "    CSROW=%d CSBase=0x%llx CSMask=0x%llx\n",
			 csrow, cs_base, cs_mask);

		cs_mask = ~cs_mask;

		edac_dbg(1, "    (InputAddr & ~CSMask)=0x%llx (CSBase & ~CSMask)=0x%llx\n",
			 (in_addr & cs_mask), (cs_base & cs_mask));

		if ((in_addr & cs_mask) == (cs_base & cs_mask)) {
			if (pvt->fam == 0x15 && pvt->model >= 0x30) {
				cs_found =  csrow;
				break;
			}
			cs_found = f10_process_possible_spare(pvt, dct, csrow);

			edac_dbg(1, " MATCH csrow=%d\n", cs_found);
			break;
		}
	}
	return cs_found;
}

/*
 * See F2x10C. Non-interleaved graphics framebuffer memory under the 16G is
 * swapped with a region located at the bottom of memory so that the GPU can use
 * the interleaved region and thus two channels.
 */
static u64 f1x_swap_interleaved_region(struct amd64_pvt *pvt, u64 sys_addr)
{
	u32 swap_reg, swap_base, swap_limit, rgn_size, tmp_addr;

	if (pvt->fam == 0x10) {
		/* only revC3 and revE have that feature */
		if (pvt->model < 4 || (pvt->model < 0xa && pvt->stepping < 3))
			return sys_addr;
	}

	amd64_read_pci_cfg(pvt->F2, SWAP_INTLV_REG, &swap_reg);

	if (!(swap_reg & 0x1))
		return sys_addr;

	swap_base	= (swap_reg >> 3) & 0x7f;
	swap_limit	= (swap_reg >> 11) & 0x7f;
	rgn_size	= (swap_reg >> 20) & 0x7f;
	tmp_addr	= sys_addr >> 27;

	if (!(sys_addr >> 34) &&
	    (((tmp_addr >= swap_base) &&
	     (tmp_addr <= swap_limit)) ||
	     (tmp_addr < rgn_size)))
		return sys_addr ^ (u64)swap_base << 27;

	return sys_addr;
}

/* For a given @dram_range, check if @sys_addr falls within it. */
static int f1x_match_to_this_node(struct amd64_pvt *pvt, unsigned range,
				  u64 sys_addr, int *chan_sel)
{
	int cs_found = -EINVAL;
	u64 chan_addr;
	u32 dct_sel_base;
	u8 channel;
	bool high_range = false;

	u8 node_id    = dram_dst_node(pvt, range);
	u8 intlv_en   = dram_intlv_en(pvt, range);
	u32 intlv_sel = dram_intlv_sel(pvt, range);

	edac_dbg(1, "(range %d) SystemAddr= 0x%llx Limit=0x%llx\n",
		 range, sys_addr, get_dram_limit(pvt, range));

	if (dhar_valid(pvt) &&
	    dhar_base(pvt) <= sys_addr &&
	    sys_addr < BIT_64(32)) {
		amd64_warn("Huh? Address is in the MMIO hole: 0x%016llx\n",
			    sys_addr);
		return -EINVAL;
	}

	if (intlv_en && (intlv_sel != ((sys_addr >> 12) & intlv_en)))
		return -EINVAL;

	sys_addr = f1x_swap_interleaved_region(pvt, sys_addr);

	dct_sel_base = dct_sel_baseaddr(pvt);

	/*
	 * check whether addresses >= DctSelBaseAddr[47:27] are to be used to
	 * select between DCT0 and DCT1.
	 */
	if (dct_high_range_enabled(pvt) &&
	   !dct_ganging_enabled(pvt) &&
	   ((sys_addr >> 27) >= (dct_sel_base >> 11)))
		high_range = true;

	channel = f1x_determine_channel(pvt, sys_addr, high_range, intlv_en);

	chan_addr = f1x_get_norm_dct_addr(pvt, range, sys_addr,
					  high_range, dct_sel_base);

	/* Remove node interleaving, see F1x120 */
	if (intlv_en)
		chan_addr = ((chan_addr >> (12 + hweight8(intlv_en))) << 12) |
			    (chan_addr & 0xfff);

	/* remove channel interleave */
	if (dct_interleave_enabled(pvt) &&
	   !dct_high_range_enabled(pvt) &&
	   !dct_ganging_enabled(pvt)) {

		if (dct_sel_interleave_addr(pvt) != 1) {
			if (dct_sel_interleave_addr(pvt) == 0x3)
				/* hash 9 */
				chan_addr = ((chan_addr >> 10) << 9) |
					     (chan_addr & 0x1ff);
			else
				/* A[6] or hash 6 */
				chan_addr = ((chan_addr >> 7) << 6) |
					     (chan_addr & 0x3f);
		} else
			/* A[12] */
			chan_addr = ((chan_addr >> 13) << 12) |
				     (chan_addr & 0xfff);
	}

	edac_dbg(1, "   Normalized DCT addr: 0x%llx\n", chan_addr);

	cs_found = f1x_lookup_addr_in_dct(chan_addr, node_id, channel);

	if (cs_found >= 0)
		*chan_sel = channel;

	return cs_found;
}

static int f15_m30h_match_to_this_node(struct amd64_pvt *pvt, unsigned range,
					u64 sys_addr, int *chan_sel)
{
	int cs_found = -EINVAL;
	int num_dcts_intlv = 0;
	u64 chan_addr, chan_offset;
	u64 dct_base, dct_limit;
	u32 dct_cont_base_reg, dct_cont_limit_reg, tmp;
	u8 channel, alias_channel, leg_mmio_hole, dct_sel, dct_offset_en;

	u64 dhar_offset		= f10_dhar_offset(pvt);
	u8 intlv_addr		= dct_sel_interleave_addr(pvt);
	u8 node_id		= dram_dst_node(pvt, range);
	u8 intlv_en		= dram_intlv_en(pvt, range);

	amd64_read_pci_cfg(pvt->F1, DRAM_CONT_BASE, &dct_cont_base_reg);
	amd64_read_pci_cfg(pvt->F1, DRAM_CONT_LIMIT, &dct_cont_limit_reg);

	dct_offset_en		= (u8) ((dct_cont_base_reg >> 3) & BIT(0));
	dct_sel			= (u8) ((dct_cont_base_reg >> 4) & 0x7);

	edac_dbg(1, "(range %d) SystemAddr= 0x%llx Limit=0x%llx\n",
		 range, sys_addr, get_dram_limit(pvt, range));

	if (!(get_dram_base(pvt, range)  <= sys_addr) &&
	    !(get_dram_limit(pvt, range) >= sys_addr))
		return -EINVAL;

	if (dhar_valid(pvt) &&
	    dhar_base(pvt) <= sys_addr &&
	    sys_addr < BIT_64(32)) {
		amd64_warn("Huh? Address is in the MMIO hole: 0x%016llx\n",
			    sys_addr);
		return -EINVAL;
	}

	/* Verify sys_addr is within DCT Range. */
	dct_base = (u64) dct_sel_baseaddr(pvt);
	dct_limit = (dct_cont_limit_reg >> 11) & 0x1FFF;

	if (!(dct_cont_base_reg & BIT(0)) &&
	    !(dct_base <= (sys_addr >> 27) &&
	      dct_limit >= (sys_addr >> 27)))
		return -EINVAL;

	/* Verify number of dct's that participate in channel interleaving. */
	num_dcts_intlv = (int) hweight8(intlv_en);

	if (!(num_dcts_intlv % 2 == 0) || (num_dcts_intlv > 4))
		return -EINVAL;

	if (pvt->model >= 0x60)
		channel = f1x_determine_channel(pvt, sys_addr, false, intlv_en);
	else
		channel = f15_m30h_determine_channel(pvt, sys_addr, intlv_en,
						     num_dcts_intlv, dct_sel);

	/* Verify we stay within the MAX number of channels allowed */
	if (channel > 3)
		return -EINVAL;

	leg_mmio_hole = (u8) (dct_cont_base_reg >> 1 & BIT(0));

	/* Get normalized DCT addr */
	if (leg_mmio_hole && (sys_addr >= BIT_64(32)))
		chan_offset = dhar_offset;
	else
		chan_offset = dct_base << 27;

	chan_addr = sys_addr - chan_offset;

	/* remove channel interleave */
	if (num_dcts_intlv == 2) {
		if (intlv_addr == 0x4)
			chan_addr = ((chan_addr >> 9) << 8) |
						(chan_addr & 0xff);
		else if (intlv_addr == 0x5)
			chan_addr = ((chan_addr >> 10) << 9) |
						(chan_addr & 0x1ff);
		else
			return -EINVAL;

	} else if (num_dcts_intlv == 4) {
		if (intlv_addr == 0x4)
			chan_addr = ((chan_addr >> 10) << 8) |
							(chan_addr & 0xff);
		else if (intlv_addr == 0x5)
			chan_addr = ((chan_addr >> 11) << 9) |
							(chan_addr & 0x1ff);
		else
			return -EINVAL;
	}

	if (dct_offset_en) {
		amd64_read_pci_cfg(pvt->F1,
				   DRAM_CONT_HIGH_OFF + (int) channel * 4,
				   &tmp);
		chan_addr +=  (u64) ((tmp >> 11) & 0xfff) << 27;
	}

	f15h_select_dct(pvt, channel);

	edac_dbg(1, "   Normalized DCT addr: 0x%llx\n", chan_addr);

	/*
	 * Find Chip select:
	 * if channel = 3, then alias it to 1. This is because, in F15 M30h,
	 * there is support for 4 DCT's, but only 2 are currently functional.
	 * They are DCT0 and DCT3. But we have read all registers of DCT3 into
	 * pvt->csels[1]. So we need to use '1' here to get correct info.
	 * Refer F15 M30h BKDG Section 2.10 and 2.10.3 for clarifications.
	 */
	alias_channel =  (channel == 3) ? 1 : channel;

	cs_found = f1x_lookup_addr_in_dct(chan_addr, node_id, alias_channel);

	if (cs_found >= 0)
		*chan_sel = alias_channel;

	return cs_found;
}

static int f1x_translate_sysaddr_to_cs(struct amd64_pvt *pvt,
					u64 sys_addr,
					int *chan_sel)
{
	int cs_found = -EINVAL;
	unsigned range;

	for (range = 0; range < DRAM_RANGES; range++) {
		if (!dram_rw(pvt, range))
			continue;

		if (pvt->fam == 0x15 && pvt->model >= 0x30)
			cs_found = f15_m30h_match_to_this_node(pvt, range,
							       sys_addr,
							       chan_sel);

		else if ((get_dram_base(pvt, range)  <= sys_addr) &&
			 (get_dram_limit(pvt, range) >= sys_addr)) {
			cs_found = f1x_match_to_this_node(pvt, range,
							  sys_addr, chan_sel);
			if (cs_found >= 0)
				break;
		}
	}
	return cs_found;
}

/*
 * For reference see "2.8.5 Routing DRAM Requests" in F10 BKDG. This code maps
 * a @sys_addr to NodeID, DCT (channel) and chip select (CSROW).
 *
 * The @sys_addr is usually an error address received from the hardware
 * (MCX_ADDR).
 */
static void f1x_map_sysaddr_to_csrow(struct mem_ctl_info *mci, u64 sys_addr,
				     struct err_info *err)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	error_address_to_page_and_offset(sys_addr, err);

	err->csrow = f1x_translate_sysaddr_to_cs(pvt, sys_addr, &err->channel);
	if (err->csrow < 0) {
		err->err_code = ERR_CSROW;
		return;
	}

	/*
	 * We need the syndromes for channel detection only when we're
	 * ganged. Otherwise @chan should already contain the channel at
	 * this point.
	 */
	if (dct_ganging_enabled(pvt))
		err->channel = get_channel_from_ecc_syndrome(mci, err->syndrome);
}

/*
 * These are tables of eigenvectors (one per line) which can be used for the
 * construction of the syndrome tables. The modified syndrome search algorithm
 * uses those to find the symbol in error and thus the DIMM.
 *
 * Algorithm courtesy of Ross LaFetra from AMD.
 */
static const u16 x4_vectors[] = {
	0x2f57, 0x1afe, 0x66cc, 0xdd88,
	0x11eb, 0x3396, 0x7f4c, 0xeac8,
	0x0001, 0x0002, 0x0004, 0x0008,
	0x1013, 0x3032, 0x4044, 0x8088,
	0x106b, 0x30d6, 0x70fc, 0xe0a8,
	0x4857, 0xc4fe, 0x13cc, 0x3288,
	0x1ac5, 0x2f4a, 0x5394, 0xa1e8,
	0x1f39, 0x251e, 0xbd6c, 0x6bd8,
	0x15c1, 0x2a42, 0x89ac, 0x4758,
	0x2b03, 0x1602, 0x4f0c, 0xca08,
	0x1f07, 0x3a0e, 0x6b04, 0xbd08,
	0x8ba7, 0x465e, 0x244c, 0x1cc8,
	0x2b87, 0x164e, 0x642c, 0xdc18,
	0x40b9, 0x80de, 0x1094, 0x20e8,
	0x27db, 0x1eb6, 0x9dac, 0x7b58,
	0x11c1, 0x2242, 0x84ac, 0x4c58,
	0x1be5, 0x2d7a, 0x5e34, 0xa718,
	0x4b39, 0x8d1e, 0x14b4, 0x28d8,
	0x4c97, 0xc87e, 0x11fc, 0x33a8,
	0x8e97, 0x497e, 0x2ffc, 0x1aa8,
	0x16b3, 0x3d62, 0x4f34, 0x8518,
	0x1e2f, 0x391a, 0x5cac, 0xf858,
	0x1d9f, 0x3b7a, 0x572c, 0xfe18,
	0x15f5, 0x2a5a, 0x5264, 0xa3b8,
	0x1dbb, 0x3b66, 0x715c, 0xe3f8,
	0x4397, 0xc27e, 0x17fc, 0x3ea8,
	0x1617, 0x3d3e, 0x6464, 0xb8b8,
	0x23ff, 0x12aa, 0xab6c, 0x56d8,
	0x2dfb, 0x1ba6, 0x913c, 0x7328,
	0x185d, 0x2ca6, 0x7914, 0x9e28,
	0x171b, 0x3e36, 0x7d7c, 0xebe8,
	0x4199, 0x82ee, 0x19f4, 0x2e58,
	0x4807, 0xc40e, 0x130c, 0x3208,
	0x1905, 0x2e0a, 0x5804, 0xac08,
	0x213f, 0x132a, 0xadfc, 0x5ba8,
	0x19a9, 0x2efe, 0xb5cc, 0x6f88,
};

static const u16 x8_vectors[] = {
	0x0145, 0x028a, 0x2374, 0x43c8, 0xa1f0, 0x0520, 0x0a40, 0x1480,
	0x0211, 0x0422, 0x0844, 0x1088, 0x01b0, 0x44e0, 0x23c0, 0xed80,
	0x1011, 0x0116, 0x022c, 0x0458, 0x08b0, 0x8c60, 0x2740, 0x4e80,
	0x0411, 0x0822, 0x1044, 0x0158, 0x02b0, 0x2360, 0x46c0, 0xab80,
	0x0811, 0x1022, 0x012c, 0x0258, 0x04b0, 0x4660, 0x8cc0, 0x2780,
	0x2071, 0x40e2, 0xa0c4, 0x0108, 0x0210, 0x0420, 0x0840, 0x1080,
	0x4071, 0x80e2, 0x0104, 0x0208, 0x0410, 0x0820, 0x1040, 0x2080,
	0x8071, 0x0102, 0x0204, 0x0408, 0x0810, 0x1020, 0x2040, 0x4080,
	0x019d, 0x03d6, 0x136c, 0x2198, 0x50b0, 0xb2e0, 0x0740, 0x0e80,
	0x0189, 0x03ea, 0x072c, 0x0e58, 0x1cb0, 0x56e0, 0x37c0, 0xf580,
	0x01fd, 0x0376, 0x06ec, 0x0bb8, 0x1110, 0x2220, 0x4440, 0x8880,
	0x0163, 0x02c6, 0x1104, 0x0758, 0x0eb0, 0x2be0, 0x6140, 0xc280,
	0x02fd, 0x01c6, 0x0b5c, 0x1108, 0x07b0, 0x25a0, 0x8840, 0x6180,
	0x0801, 0x012e, 0x025c, 0x04b8, 0x1370, 0x26e0, 0x57c0, 0xb580,
	0x0401, 0x0802, 0x015c, 0x02b8, 0x22b0, 0x13e0, 0x7140, 0xe280,
	0x0201, 0x0402, 0x0804, 0x01b8, 0x11b0, 0x31a0, 0x8040, 0x7180,
	0x0101, 0x0202, 0x0404, 0x0808, 0x1010, 0x2020, 0x4040, 0x8080,
	0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
	0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000,
};

static int decode_syndrome(u16 syndrome, const u16 *vectors, unsigned num_vecs,
			   unsigned v_dim)
{
	unsigned int i, err_sym;

	for (err_sym = 0; err_sym < num_vecs / v_dim; err_sym++) {
		u16 s = syndrome;
		unsigned v_idx =  err_sym * v_dim;
		unsigned v_end = (err_sym + 1) * v_dim;

		/* walk over all 16 bits of the syndrome */
		for (i = 1; i < (1U << 16); i <<= 1) {

			/* if bit is set in that eigenvector... */
			if (v_idx < v_end && vectors[v_idx] & i) {
				u16 ev_comp = vectors[v_idx++];

				/* ... and bit set in the modified syndrome, */
				if (s & i) {
					/* remove it. */
					s ^= ev_comp;

					if (!s)
						return err_sym;
				}

			} else if (s & i)
				/* can't get to zero, move to next symbol */
				break;
		}
	}

	edac_dbg(0, "syndrome(%x) not found\n", syndrome);
	return -1;
}

static int map_err_sym_to_channel(int err_sym, int sym_size)
{
	if (sym_size == 4)
		switch (err_sym) {
		case 0x20:
		case 0x21:
			return 0;
		case 0x22:
		case 0x23:
			return 1;
		default:
			return err_sym >> 4;
		}
	/* x8 symbols */
	else
		switch (err_sym) {
		/* imaginary bits not in a DIMM */
		case 0x10:
			WARN(1, KERN_ERR "Invalid error symbol: 0x%x\n",
					  err_sym);
			return -1;
		case 0x11:
			return 0;
		case 0x12:
			return 1;
		default:
			return err_sym >> 3;
		}
	return -1;
}

static int get_channel_from_ecc_syndrome(struct mem_ctl_info *mci, u16 syndrome)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	int err_sym = -1;

	if (pvt->ecc_sym_sz == 8)
		err_sym = decode_syndrome(syndrome, x8_vectors,
					  ARRAY_SIZE(x8_vectors),
					  pvt->ecc_sym_sz);
	else if (pvt->ecc_sym_sz == 4)
		err_sym = decode_syndrome(syndrome, x4_vectors,
					  ARRAY_SIZE(x4_vectors),
					  pvt->ecc_sym_sz);
	else {
		amd64_warn("Illegal syndrome type: %u\n", pvt->ecc_sym_sz);
		return err_sym;
	}

	return map_err_sym_to_channel(err_sym, pvt->ecc_sym_sz);
}

static void __log_ecc_error(struct mem_ctl_info *mci, struct err_info *err,
			    u8 ecc_type)
{
	enum hw_event_mc_err_type err_type;
	const char *string;

	if (ecc_type == 2)
		err_type = HW_EVENT_ERR_CORRECTED;
	else if (ecc_type == 1)
		err_type = HW_EVENT_ERR_UNCORRECTED;
	else if (ecc_type == 3)
		err_type = HW_EVENT_ERR_DEFERRED;
	else {
		WARN(1, "Something is rotten in the state of Denmark.\n");
		return;
	}

	switch (err->err_code) {
	case DECODE_OK:
		string = "";
		break;
	case ERR_NODE:
		string = "Failed to map error addr to a node";
		break;
	case ERR_CSROW:
		string = "Failed to map error addr to a csrow";
		break;
	case ERR_CHANNEL:
		string = "Unknown syndrome - possible error reporting race";
		break;
	case ERR_SYND:
		string = "MCA_SYND not valid - unknown syndrome and csrow";
		break;
	case ERR_NORM_ADDR:
		string = "Cannot decode normalized address";
		break;
	default:
		string = "WTF error";
		break;
	}

	edac_mc_handle_error(err_type, mci, 1,
			     err->page, err->offset, err->syndrome,
			     err->csrow, err->channel, -1,
			     string, "");
}

static inline void decode_bus_error(int node_id, struct mce *m)
{
	struct mem_ctl_info *mci;
	struct amd64_pvt *pvt;
	u8 ecc_type = (m->status >> 45) & 0x3;
	u8 xec = XEC(m->status, 0x1f);
	u16 ec = EC(m->status);
	u64 sys_addr;
	struct err_info err;

	mci = edac_mc_find(node_id);
	if (!mci)
		return;

	pvt = mci->pvt_info;

	/* Bail out early if this was an 'observed' error */
	if (PP(ec) == NBSL_PP_OBS)
		return;

	/* Do only ECC errors */
	if (xec && xec != F10_NBSL_EXT_ERR_ECC)
		return;

	memset(&err, 0, sizeof(err));

	sys_addr = get_error_address(pvt, m);

	if (ecc_type == 2)
		err.syndrome = extract_syndrome(m->status);

	pvt->ops->map_sysaddr_to_csrow(mci, sys_addr, &err);

	__log_ecc_error(mci, &err, ecc_type);
}

/*
 * To find the UMC channel represented by this bank we need to match on its
 * instance_id. The instance_id of a bank is held in the lower 32 bits of its
 * IPID.
 *
 * Currently, we can derive the channel number by looking at the 6th nibble in
 * the instance_id. For example, instance_id=0xYXXXXX where Y is the channel
 * number.
 *
 * For DRAM ECC errors, the Chip Select number is given in bits [2:0] of
 * the MCA_SYND[ErrorInformation] field.
 */
static void umc_get_err_info(struct mce *m, struct err_info *err)
{
	err->channel = (m->ipid & GENMASK(31, 0)) >> 20;
	err->csrow = m->synd & 0x7;
}

static void decode_umc_error(int node_id, struct mce *m)
{
	u8 ecc_type = (m->status >> 45) & 0x3;
	struct mem_ctl_info *mci;
	unsigned long sys_addr;
	struct amd64_pvt *pvt;
	struct atl_err a_err;
	struct err_info err;

	node_id = fixup_node_id(node_id, m);

	mci = edac_mc_find(node_id);
	if (!mci)
		return;

	pvt = mci->pvt_info;

	memset(&err, 0, sizeof(err));

	if (m->status & MCI_STATUS_DEFERRED)
		ecc_type = 3;

	if (!(m->status & MCI_STATUS_SYNDV)) {
		err.err_code = ERR_SYND;
		goto log_error;
	}

	if (ecc_type == 2) {
		u8 length = (m->synd >> 18) & 0x3f;

		if (length)
			err.syndrome = (m->synd >> 32) & GENMASK(length - 1, 0);
		else
			err.err_code = ERR_CHANNEL;
	}

	pvt->ops->get_err_info(m, &err);

	a_err.addr = m->addr;
	a_err.ipid = m->ipid;
	a_err.cpu  = m->extcpu;

	sys_addr = amd_convert_umc_mca_addr_to_sys_addr(&a_err);
	if (IS_ERR_VALUE(sys_addr)) {
		err.err_code = ERR_NORM_ADDR;
		goto log_error;
	}

	error_address_to_page_and_offset(sys_addr, &err);

log_error:
	__log_ecc_error(mci, &err, ecc_type);
}

/*
 * Use pvt->F3 which contains the F3 CPU PCI device to get the related
 * F1 (AddrMap) and F2 (Dct) devices. Return negative value on error.
 */
static int
reserve_mc_sibling_devs(struct amd64_pvt *pvt, u16 pci_id1, u16 pci_id2)
{
	/* Reserve the ADDRESS MAP Device */
	pvt->F1 = pci_get_related_function(pvt->F3->vendor, pci_id1, pvt->F3);
	if (!pvt->F1) {
		edac_dbg(1, "F1 not found: device 0x%x\n", pci_id1);
		return -ENODEV;
	}

	/* Reserve the DCT Device */
	pvt->F2 = pci_get_related_function(pvt->F3->vendor, pci_id2, pvt->F3);
	if (!pvt->F2) {
		pci_dev_put(pvt->F1);
		pvt->F1 = NULL;

		edac_dbg(1, "F2 not found: device 0x%x\n", pci_id2);
		return -ENODEV;
	}

	if (!pci_ctl_dev)
		pci_ctl_dev = &pvt->F2->dev;

	edac_dbg(1, "F1: %s\n", pci_name(pvt->F1));
	edac_dbg(1, "F2: %s\n", pci_name(pvt->F2));
	edac_dbg(1, "F3: %s\n", pci_name(pvt->F3));

	return 0;
}

static void determine_ecc_sym_sz(struct amd64_pvt *pvt)
{
	pvt->ecc_sym_sz = 4;

	if (pvt->fam >= 0x10) {
		u32 tmp;

		amd64_read_pci_cfg(pvt->F3, EXT_NB_MCA_CFG, &tmp);
		/* F16h has only DCT0, so no need to read dbam1. */
		if (pvt->fam != 0x16)
			amd64_read_dct_pci_cfg(pvt, 1, DBAM0, &pvt->dbam1);

		/* F10h, revD and later can do x8 ECC too. */
		if ((pvt->fam > 0x10 || pvt->model > 7) && tmp & BIT(25))
			pvt->ecc_sym_sz = 8;
	}
}

/*
 * Retrieve the hardware registers of the memory controller.
 */
static void umc_read_mc_regs(struct amd64_pvt *pvt)
{
	u8 nid = pvt->mc_node_id;
	struct amd64_umc *umc;
	u32 i, tmp, umc_base;

	/* Read registers from each UMC */
	for_each_umc(i) {

		umc_base = get_umc_base(i);
		umc = &pvt->umc[i];

		if (!amd_smn_read(nid, umc_base + get_umc_reg(pvt, UMCCH_DIMM_CFG), &tmp))
			umc->dimm_cfg = tmp;

		if (!amd_smn_read(nid, umc_base + UMCCH_UMC_CFG, &tmp))
			umc->umc_cfg = tmp;

		if (!amd_smn_read(nid, umc_base + UMCCH_SDP_CTRL, &tmp))
			umc->sdp_ctrl = tmp;

		if (!amd_smn_read(nid, umc_base + UMCCH_ECC_CTRL, &tmp))
			umc->ecc_ctrl = tmp;

		if (!amd_smn_read(nid, umc_base + UMCCH_UMC_CAP_HI, &tmp))
			umc->umc_cap_hi = tmp;
	}
}

/*
 * Retrieve the hardware registers of the memory controller (this includes the
 * 'Address Map' and 'Misc' device regs)
 */
static void dct_read_mc_regs(struct amd64_pvt *pvt)
{
	unsigned int range;
	u64 msr_val;

	/*
	 * Retrieve TOP_MEM and TOP_MEM2; no masking off of reserved bits since
	 * those are Read-As-Zero.
	 */
	rdmsrl(MSR_K8_TOP_MEM1, pvt->top_mem);
	edac_dbg(0, "  TOP_MEM:  0x%016llx\n", pvt->top_mem);

	/* Check first whether TOP_MEM2 is enabled: */
	rdmsrl(MSR_AMD64_SYSCFG, msr_val);
	if (msr_val & BIT(21)) {
		rdmsrl(MSR_K8_TOP_MEM2, pvt->top_mem2);
		edac_dbg(0, "  TOP_MEM2: 0x%016llx\n", pvt->top_mem2);
	} else {
		edac_dbg(0, "  TOP_MEM2 disabled\n");
	}

	amd64_read_pci_cfg(pvt->F3, NBCAP, &pvt->nbcap);

	read_dram_ctl_register(pvt);

	for (range = 0; range < DRAM_RANGES; range++) {
		u8 rw;

		/* read settings for this DRAM range */
		read_dram_base_limit_regs(pvt, range);

		rw = dram_rw(pvt, range);
		if (!rw)
			continue;

		edac_dbg(1, "  DRAM range[%d], base: 0x%016llx; limit: 0x%016llx\n",
			 range,
			 get_dram_base(pvt, range),
			 get_dram_limit(pvt, range));

		edac_dbg(1, "   IntlvEn=%s; Range access: %s%s IntlvSel=%d DstNode=%d\n",
			 dram_intlv_en(pvt, range) ? "Enabled" : "Disabled",
			 (rw & 0x1) ? "R" : "-",
			 (rw & 0x2) ? "W" : "-",
			 dram_intlv_sel(pvt, range),
			 dram_dst_node(pvt, range));
	}

	amd64_read_pci_cfg(pvt->F1, DHAR, &pvt->dhar);
	amd64_read_dct_pci_cfg(pvt, 0, DBAM0, &pvt->dbam0);

	amd64_read_pci_cfg(pvt->F3, F10_ONLINE_SPARE, &pvt->online_spare);

	amd64_read_dct_pci_cfg(pvt, 0, DCLR0, &pvt->dclr0);
	amd64_read_dct_pci_cfg(pvt, 0, DCHR0, &pvt->dchr0);

	if (!dct_ganging_enabled(pvt)) {
		amd64_read_dct_pci_cfg(pvt, 1, DCLR0, &pvt->dclr1);
		amd64_read_dct_pci_cfg(pvt, 1, DCHR0, &pvt->dchr1);
	}

	determine_ecc_sym_sz(pvt);
}

/*
 * NOTE: CPU Revision Dependent code
 *
 * Input:
 *	@csrow_nr ChipSelect Row Number (0..NUM_CHIPSELECTS-1)
 *	k8 private pointer to -->
 *			DRAM Bank Address mapping register
 *			node_id
 *			DCL register where dual_channel_active is
 *
 * The DBAM register consists of 4 sets of 4 bits each definitions:
 *
 * Bits:	CSROWs
 * 0-3		CSROWs 0 and 1
 * 4-7		CSROWs 2 and 3
 * 8-11		CSROWs 4 and 5
 * 12-15	CSROWs 6 and 7
 *
 * Values range from: 0 to 15
 * The meaning of the values depends on CPU revision and dual-channel state,
 * see relevant BKDG more info.
 *
 * The memory controller provides for total of only 8 CSROWs in its current
 * architecture. Each "pair" of CSROWs normally represents just one DIMM in
 * single channel or two (2) DIMMs in dual channel mode.
 *
 * The following code logic collapses the various tables for CSROW based on CPU
 * revision.
 *
 * Returns:
 *	The number of PAGE_SIZE pages on the specified CSROW number it
 *	encompasses
 *
 */
static u32 dct_get_csrow_nr_pages(struct amd64_pvt *pvt, u8 dct, int csrow_nr)
{
	u32 dbam = dct ? pvt->dbam1 : pvt->dbam0;
	u32 cs_mode, nr_pages;

	csrow_nr >>= 1;
	cs_mode = DBAM_DIMM(csrow_nr, dbam);

	nr_pages   = pvt->ops->dbam_to_cs(pvt, dct, cs_mode, csrow_nr);
	nr_pages <<= 20 - PAGE_SHIFT;

	edac_dbg(0, "csrow: %d, channel: %d, DBAM idx: %d\n",
		    csrow_nr, dct,  cs_mode);
	edac_dbg(0, "nr_pages/channel: %u\n", nr_pages);

	return nr_pages;
}

static u32 umc_get_csrow_nr_pages(struct amd64_pvt *pvt, u8 dct, int csrow_nr_orig)
{
	int csrow_nr = csrow_nr_orig;
	u32 cs_mode, nr_pages;

	cs_mode = umc_get_cs_mode(csrow_nr >> 1, dct, pvt);

	nr_pages   = umc_addr_mask_to_cs_size(pvt, dct, cs_mode, csrow_nr);
	nr_pages <<= 20 - PAGE_SHIFT;

	edac_dbg(0, "csrow: %d, channel: %d, cs_mode %d\n",
		 csrow_nr_orig, dct,  cs_mode);
	edac_dbg(0, "nr_pages/channel: %u\n", nr_pages);

	return nr_pages;
}

static void umc_init_csrows(struct mem_ctl_info *mci)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	enum edac_type edac_mode = EDAC_NONE;
	enum dev_type dev_type = DEV_UNKNOWN;
	struct dimm_info *dimm;
	u8 umc, cs;

	if (mci->edac_ctl_cap & EDAC_FLAG_S16ECD16ED) {
		edac_mode = EDAC_S16ECD16ED;
		dev_type = DEV_X16;
	} else if (mci->edac_ctl_cap & EDAC_FLAG_S8ECD8ED) {
		edac_mode = EDAC_S8ECD8ED;
		dev_type = DEV_X8;
	} else if (mci->edac_ctl_cap & EDAC_FLAG_S4ECD4ED) {
		edac_mode = EDAC_S4ECD4ED;
		dev_type = DEV_X4;
	} else if (mci->edac_ctl_cap & EDAC_FLAG_SECDED) {
		edac_mode = EDAC_SECDED;
	}

	for_each_umc(umc) {
		for_each_chip_select(cs, umc, pvt) {
			if (!csrow_enabled(cs, umc, pvt))
				continue;

			dimm = mci->csrows[cs]->channels[umc]->dimm;

			edac_dbg(1, "MC node: %d, csrow: %d\n",
					pvt->mc_node_id, cs);

			dimm->nr_pages = umc_get_csrow_nr_pages(pvt, umc, cs);
			dimm->mtype = pvt->umc[umc].dram_type;
			dimm->edac_mode = edac_mode;
			dimm->dtype = dev_type;
			dimm->grain = 64;
		}
	}
}

/*
 * Initialize the array of csrow attribute instances, based on the values
 * from pci config hardware registers.
 */
static void dct_init_csrows(struct mem_ctl_info *mci)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	enum edac_type edac_mode = EDAC_NONE;
	struct csrow_info *csrow;
	struct dimm_info *dimm;
	int nr_pages = 0;
	int i, j;
	u32 val;

	amd64_read_pci_cfg(pvt->F3, NBCFG, &val);

	pvt->nbcfg = val;

	edac_dbg(0, "node %d, NBCFG=0x%08x[ChipKillEccCap: %d|DramEccEn: %d]\n",
		 pvt->mc_node_id, val,
		 !!(val & NBCFG_CHIPKILL), !!(val & NBCFG_ECC_ENABLE));

	/*
	 * We iterate over DCT0 here but we look at DCT1 in parallel, if needed.
	 */
	for_each_chip_select(i, 0, pvt) {
		bool row_dct0 = !!csrow_enabled(i, 0, pvt);
		bool row_dct1 = false;

		if (pvt->fam != 0xf)
			row_dct1 = !!csrow_enabled(i, 1, pvt);

		if (!row_dct0 && !row_dct1)
			continue;

		csrow = mci->csrows[i];

		edac_dbg(1, "MC node: %d, csrow: %d\n",
			    pvt->mc_node_id, i);

		if (row_dct0) {
			nr_pages = dct_get_csrow_nr_pages(pvt, 0, i);
			csrow->channels[0]->dimm->nr_pages = nr_pages;
		}

		/* K8 has only one DCT */
		if (pvt->fam != 0xf && row_dct1) {
			int row_dct1_pages = dct_get_csrow_nr_pages(pvt, 1, i);

			csrow->channels[1]->dimm->nr_pages = row_dct1_pages;
			nr_pages += row_dct1_pages;
		}

		edac_dbg(1, "Total csrow%d pages: %u\n", i, nr_pages);

		/* Determine DIMM ECC mode: */
		if (pvt->nbcfg & NBCFG_ECC_ENABLE) {
			edac_mode = (pvt->nbcfg & NBCFG_CHIPKILL)
					? EDAC_S4ECD4ED
					: EDAC_SECDED;
		}

		for (j = 0; j < pvt->max_mcs; j++) {
			dimm = csrow->channels[j]->dimm;
			dimm->mtype = pvt->dram_type;
			dimm->edac_mode = edac_mode;
			dimm->grain = 64;
		}
	}
}

/* get all cores on this DCT */
static void get_cpus_on_this_dct_cpumask(struct cpumask *mask, u16 nid)
{
	int cpu;

	for_each_online_cpu(cpu)
		if (topology_amd_node_id(cpu) == nid)
			cpumask_set_cpu(cpu, mask);
}

/* check MCG_CTL on all the cpus on this node */
static bool nb_mce_bank_enabled_on_node(u16 nid)
{
	cpumask_var_t mask;
	int cpu, nbe;
	bool ret = false;

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL)) {
		amd64_warn("%s: Error allocating mask\n", __func__);
		return false;
	}

	get_cpus_on_this_dct_cpumask(mask, nid);

	rdmsr_on_cpus(mask, MSR_IA32_MCG_CTL, msrs);

	for_each_cpu(cpu, mask) {
		struct msr *reg = per_cpu_ptr(msrs, cpu);
		nbe = reg->l & MSR_MCGCTL_NBE;

		edac_dbg(0, "core: %u, MCG_CTL: 0x%llx, NB MSR is %s\n",
			 cpu, reg->q,
			 (nbe ? "enabled" : "disabled"));

		if (!nbe)
			goto out;
	}
	ret = true;

out:
	free_cpumask_var(mask);
	return ret;
}

static int toggle_ecc_err_reporting(struct ecc_settings *s, u16 nid, bool on)
{
	cpumask_var_t cmask;
	int cpu;

	if (!zalloc_cpumask_var(&cmask, GFP_KERNEL)) {
		amd64_warn("%s: error allocating mask\n", __func__);
		return -ENOMEM;
	}

	get_cpus_on_this_dct_cpumask(cmask, nid);

	rdmsr_on_cpus(cmask, MSR_IA32_MCG_CTL, msrs);

	for_each_cpu(cpu, cmask) {

		struct msr *reg = per_cpu_ptr(msrs, cpu);

		if (on) {
			if (reg->l & MSR_MCGCTL_NBE)
				s->flags.nb_mce_enable = 1;

			reg->l |= MSR_MCGCTL_NBE;
		} else {
			/*
			 * Turn off NB MCE reporting only when it was off before
			 */
			if (!s->flags.nb_mce_enable)
				reg->l &= ~MSR_MCGCTL_NBE;
		}
	}
	wrmsr_on_cpus(cmask, MSR_IA32_MCG_CTL, msrs);

	free_cpumask_var(cmask);

	return 0;
}

static bool enable_ecc_error_reporting(struct ecc_settings *s, u16 nid,
				       struct pci_dev *F3)
{
	bool ret = true;
	u32 value, mask = 0x3;		/* UECC/CECC enable */

	if (toggle_ecc_err_reporting(s, nid, ON)) {
		amd64_warn("Error enabling ECC reporting over MCGCTL!\n");
		return false;
	}

	amd64_read_pci_cfg(F3, NBCTL, &value);

	s->old_nbctl   = value & mask;
	s->nbctl_valid = true;

	value |= mask;
	amd64_write_pci_cfg(F3, NBCTL, value);

	amd64_read_pci_cfg(F3, NBCFG, &value);

	edac_dbg(0, "1: node %d, NBCFG=0x%08x[DramEccEn: %d]\n",
		 nid, value, !!(value & NBCFG_ECC_ENABLE));

	if (!(value & NBCFG_ECC_ENABLE)) {
		amd64_warn("DRAM ECC disabled on this node, enabling...\n");

		s->flags.nb_ecc_prev = 0;

		/* Attempt to turn on DRAM ECC Enable */
		value |= NBCFG_ECC_ENABLE;
		amd64_write_pci_cfg(F3, NBCFG, value);

		amd64_read_pci_cfg(F3, NBCFG, &value);

		if (!(value & NBCFG_ECC_ENABLE)) {
			amd64_warn("Hardware rejected DRAM ECC enable,"
				   "check memory DIMM configuration.\n");
			ret = false;
		} else {
			amd64_info("Hardware accepted DRAM ECC Enable\n");
		}
	} else {
		s->flags.nb_ecc_prev = 1;
	}

	edac_dbg(0, "2: node %d, NBCFG=0x%08x[DramEccEn: %d]\n",
		 nid, value, !!(value & NBCFG_ECC_ENABLE));

	return ret;
}

static void restore_ecc_error_reporting(struct ecc_settings *s, u16 nid,
					struct pci_dev *F3)
{
	u32 value, mask = 0x3;		/* UECC/CECC enable */

	if (!s->nbctl_valid)
		return;

	amd64_read_pci_cfg(F3, NBCTL, &value);
	value &= ~mask;
	value |= s->old_nbctl;

	amd64_write_pci_cfg(F3, NBCTL, value);

	/* restore previous BIOS DRAM ECC "off" setting we force-enabled */
	if (!s->flags.nb_ecc_prev) {
		amd64_read_pci_cfg(F3, NBCFG, &value);
		value &= ~NBCFG_ECC_ENABLE;
		amd64_write_pci_cfg(F3, NBCFG, value);
	}

	/* restore the NB Enable MCGCTL bit */
	if (toggle_ecc_err_reporting(s, nid, OFF))
		amd64_warn("Error restoring NB MCGCTL settings!\n");
}

static bool dct_ecc_enabled(struct amd64_pvt *pvt)
{
	u16 nid = pvt->mc_node_id;
	bool nb_mce_en = false;
	u8 ecc_en = 0;
	u32 value;

	amd64_read_pci_cfg(pvt->F3, NBCFG, &value);

	ecc_en = !!(value & NBCFG_ECC_ENABLE);

	nb_mce_en = nb_mce_bank_enabled_on_node(nid);
	if (!nb_mce_en)
		edac_dbg(0, "NB MCE bank disabled, set MSR 0x%08x[4] on node %d to enable.\n",
			 MSR_IA32_MCG_CTL, nid);

	edac_dbg(3, "Node %d: DRAM ECC %s.\n", nid, (ecc_en ? "enabled" : "disabled"));

	if (!ecc_en || !nb_mce_en)
		return false;
	else
		return true;
}

static bool umc_ecc_enabled(struct amd64_pvt *pvt)
{
	struct amd64_umc *umc;
	bool ecc_en = false;
	int i;

	/* Check whether at least one UMC is enabled: */
	for_each_umc(i) {
		umc = &pvt->umc[i];

		if (umc->sdp_ctrl & UMC_SDP_INIT &&
		    umc->umc_cap_hi & UMC_ECC_ENABLED) {
			ecc_en = true;
			break;
		}
	}

	edac_dbg(3, "Node %d: DRAM ECC %s.\n", pvt->mc_node_id, (ecc_en ? "enabled" : "disabled"));

	return ecc_en;
}

static inline void
umc_determine_edac_ctl_cap(struct mem_ctl_info *mci, struct amd64_pvt *pvt)
{
	u8 i, ecc_en = 1, cpk_en = 1, dev_x4 = 1, dev_x16 = 1;

	for_each_umc(i) {
		if (pvt->umc[i].sdp_ctrl & UMC_SDP_INIT) {
			ecc_en &= !!(pvt->umc[i].umc_cap_hi & UMC_ECC_ENABLED);
			cpk_en &= !!(pvt->umc[i].umc_cap_hi & UMC_ECC_CHIPKILL_CAP);

			dev_x4  &= !!(pvt->umc[i].dimm_cfg & BIT(6));
			dev_x16 &= !!(pvt->umc[i].dimm_cfg & BIT(7));
		}
	}

	/* Set chipkill only if ECC is enabled: */
	if (ecc_en) {
		mci->edac_ctl_cap |= EDAC_FLAG_SECDED;

		if (!cpk_en)
			return;

		if (dev_x4)
			mci->edac_ctl_cap |= EDAC_FLAG_S4ECD4ED;
		else if (dev_x16)
			mci->edac_ctl_cap |= EDAC_FLAG_S16ECD16ED;
		else
			mci->edac_ctl_cap |= EDAC_FLAG_S8ECD8ED;
	}
}

static void dct_setup_mci_misc_attrs(struct mem_ctl_info *mci)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	mci->mtype_cap		= MEM_FLAG_DDR2 | MEM_FLAG_RDDR2;
	mci->edac_ctl_cap	= EDAC_FLAG_NONE;

	if (pvt->nbcap & NBCAP_SECDED)
		mci->edac_ctl_cap |= EDAC_FLAG_SECDED;

	if (pvt->nbcap & NBCAP_CHIPKILL)
		mci->edac_ctl_cap |= EDAC_FLAG_S4ECD4ED;

	mci->edac_cap		= dct_determine_edac_cap(pvt);
	mci->mod_name		= EDAC_MOD_STR;
	mci->ctl_name		= pvt->ctl_name;
	mci->dev_name		= pci_name(pvt->F3);
	mci->ctl_page_to_phys	= NULL;

	/* memory scrubber interface */
	mci->set_sdram_scrub_rate = set_scrub_rate;
	mci->get_sdram_scrub_rate = get_scrub_rate;

	dct_init_csrows(mci);
}

static void umc_setup_mci_misc_attrs(struct mem_ctl_info *mci)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	mci->mtype_cap		= MEM_FLAG_DDR4 | MEM_FLAG_RDDR4;
	mci->edac_ctl_cap	= EDAC_FLAG_NONE;

	umc_determine_edac_ctl_cap(mci, pvt);

	mci->edac_cap		= umc_determine_edac_cap(pvt);
	mci->mod_name		= EDAC_MOD_STR;
	mci->ctl_name		= pvt->ctl_name;
	mci->dev_name		= pci_name(pvt->F3);
	mci->ctl_page_to_phys	= NULL;

	umc_init_csrows(mci);
}

static int dct_hw_info_get(struct amd64_pvt *pvt)
{
	int ret = reserve_mc_sibling_devs(pvt, pvt->f1_id, pvt->f2_id);

	if (ret)
		return ret;

	dct_prep_chip_selects(pvt);
	dct_read_base_mask(pvt);
	dct_read_mc_regs(pvt);
	dct_determine_memory_type(pvt);

	return 0;
}

static int umc_hw_info_get(struct amd64_pvt *pvt)
{
	pvt->umc = kcalloc(pvt->max_mcs, sizeof(struct amd64_umc), GFP_KERNEL);
	if (!pvt->umc)
		return -ENOMEM;

	umc_prep_chip_selects(pvt);
	umc_read_base_mask(pvt);
	umc_read_mc_regs(pvt);
	umc_determine_memory_type(pvt);

	return 0;
}

/*
 * The CPUs have one channel per UMC, so UMC number is equivalent to a
 * channel number. The GPUs have 8 channels per UMC, so the UMC number no
 * longer works as a channel number.
 *
 * The channel number within a GPU UMC is given in MCA_IPID[15:12].
 * However, the IDs are split such that two UMC values go to one UMC, and
 * the channel numbers are split in two groups of four.
 *
 * Refer to comment on gpu_get_umc_base().
 *
 * For example,
 * UMC0 CH[3:0] = 0x0005[3:0]000
 * UMC0 CH[7:4] = 0x0015[3:0]000
 * UMC1 CH[3:0] = 0x0025[3:0]000
 * UMC1 CH[7:4] = 0x0035[3:0]000
 */
static void gpu_get_err_info(struct mce *m, struct err_info *err)
{
	u8 ch = (m->ipid & GENMASK(31, 0)) >> 20;
	u8 phy = ((m->ipid >> 12) & 0xf);

	err->channel = ch % 2 ? phy + 4 : phy;
	err->csrow = phy;
}

static int gpu_addr_mask_to_cs_size(struct amd64_pvt *pvt, u8 umc,
				    unsigned int cs_mode, int csrow_nr)
{
	u32 addr_mask_orig = pvt->csels[umc].csmasks[csrow_nr];

	return __addr_mask_to_cs_size(addr_mask_orig, cs_mode, csrow_nr, csrow_nr >> 1);
}

static void gpu_debug_display_dimm_sizes(struct amd64_pvt *pvt, u8 ctrl)
{
	int size, cs_mode, cs = 0;

	edac_printk(KERN_DEBUG, EDAC_MC, "UMC%d chip selects:\n", ctrl);

	cs_mode = CS_EVEN_PRIMARY | CS_ODD_PRIMARY;

	for_each_chip_select(cs, ctrl, pvt) {
		size = gpu_addr_mask_to_cs_size(pvt, ctrl, cs_mode, cs);
		amd64_info(EDAC_MC ": %d: %5dMB\n", cs, size);
	}
}

static void gpu_dump_misc_regs(struct amd64_pvt *pvt)
{
	struct amd64_umc *umc;
	u32 i;

	for_each_umc(i) {
		umc = &pvt->umc[i];

		edac_dbg(1, "UMC%d UMC cfg: 0x%x\n", i, umc->umc_cfg);
		edac_dbg(1, "UMC%d SDP ctrl: 0x%x\n", i, umc->sdp_ctrl);
		edac_dbg(1, "UMC%d ECC ctrl: 0x%x\n", i, umc->ecc_ctrl);
		edac_dbg(1, "UMC%d All HBMs support ECC: yes\n", i);

		gpu_debug_display_dimm_sizes(pvt, i);
	}
}

static u32 gpu_get_csrow_nr_pages(struct amd64_pvt *pvt, u8 dct, int csrow_nr)
{
	u32 nr_pages;
	int cs_mode = CS_EVEN_PRIMARY | CS_ODD_PRIMARY;

	nr_pages   = gpu_addr_mask_to_cs_size(pvt, dct, cs_mode, csrow_nr);
	nr_pages <<= 20 - PAGE_SHIFT;

	edac_dbg(0, "csrow: %d, channel: %d\n", csrow_nr, dct);
	edac_dbg(0, "nr_pages/channel: %u\n", nr_pages);

	return nr_pages;
}

static void gpu_init_csrows(struct mem_ctl_info *mci)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	struct dimm_info *dimm;
	u8 umc, cs;

	for_each_umc(umc) {
		for_each_chip_select(cs, umc, pvt) {
			if (!csrow_enabled(cs, umc, pvt))
				continue;

			dimm = mci->csrows[umc]->channels[cs]->dimm;

			edac_dbg(1, "MC node: %d, csrow: %d\n",
				 pvt->mc_node_id, cs);

			dimm->nr_pages = gpu_get_csrow_nr_pages(pvt, umc, cs);
			dimm->edac_mode = EDAC_SECDED;
			dimm->mtype = pvt->dram_type;
			dimm->dtype = DEV_X16;
			dimm->grain = 64;
		}
	}
}

static void gpu_setup_mci_misc_attrs(struct mem_ctl_info *mci)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	mci->mtype_cap		= MEM_FLAG_HBM2;
	mci->edac_ctl_cap	= EDAC_FLAG_SECDED;

	mci->edac_cap		= EDAC_FLAG_EC;
	mci->mod_name		= EDAC_MOD_STR;
	mci->ctl_name		= pvt->ctl_name;
	mci->dev_name		= pci_name(pvt->F3);
	mci->ctl_page_to_phys	= NULL;

	gpu_init_csrows(mci);
}

/* ECC is enabled by default on GPU nodes */
static bool gpu_ecc_enabled(struct amd64_pvt *pvt)
{
	return true;
}

static inline u32 gpu_get_umc_base(struct amd64_pvt *pvt, u8 umc, u8 channel)
{
	/*
	 * On CPUs, there is one channel per UMC, so UMC numbering equals
	 * channel numbering. On GPUs, there are eight channels per UMC,
	 * so the channel numbering is different from UMC numbering.
	 *
	 * On CPU nodes channels are selected in 6th nibble
	 * UMC chY[3:0]= [(chY*2 + 1) : (chY*2)]50000;
	 *
	 * On GPU nodes channels are selected in 3rd nibble
	 * HBM chX[3:0]= [Y  ]5X[3:0]000;
	 * HBM chX[7:4]= [Y+1]5X[3:0]000
	 *
	 * On MI300 APU nodes, same as GPU nodes but channels are selected
	 * in the base address of 0x90000
	 */
	umc *= 2;

	if (channel >= 4)
		umc++;

	return pvt->gpu_umc_base + (umc << 20) + ((channel % 4) << 12);
}

static void gpu_read_mc_regs(struct amd64_pvt *pvt)
{
	u8 nid = pvt->mc_node_id;
	struct amd64_umc *umc;
	u32 i, tmp, umc_base;

	/* Read registers from each UMC */
	for_each_umc(i) {
		umc_base = gpu_get_umc_base(pvt, i, 0);
		umc = &pvt->umc[i];

		if (!amd_smn_read(nid, umc_base + UMCCH_UMC_CFG, &tmp))
			umc->umc_cfg = tmp;

		if (!amd_smn_read(nid, umc_base + UMCCH_SDP_CTRL, &tmp))
			umc->sdp_ctrl = tmp;

		if (!amd_smn_read(nid, umc_base + UMCCH_ECC_CTRL, &tmp))
			umc->ecc_ctrl = tmp;
	}
}

static void gpu_read_base_mask(struct amd64_pvt *pvt)
{
	u32 base_reg, mask_reg;
	u32 *base, *mask;
	int umc, cs;

	for_each_umc(umc) {
		for_each_chip_select(cs, umc, pvt) {
			base_reg = gpu_get_umc_base(pvt, umc, cs) + UMCCH_BASE_ADDR;
			base = &pvt->csels[umc].csbases[cs];

			if (!amd_smn_read(pvt->mc_node_id, base_reg, base)) {
				edac_dbg(0, "  DCSB%d[%d]=0x%08x reg: 0x%x\n",
					 umc, cs, *base, base_reg);
			}

			mask_reg = gpu_get_umc_base(pvt, umc, cs) + UMCCH_ADDR_MASK;
			mask = &pvt->csels[umc].csmasks[cs];

			if (!amd_smn_read(pvt->mc_node_id, mask_reg, mask)) {
				edac_dbg(0, "  DCSM%d[%d]=0x%08x reg: 0x%x\n",
					 umc, cs, *mask, mask_reg);
			}
		}
	}
}

static void gpu_prep_chip_selects(struct amd64_pvt *pvt)
{
	int umc;

	for_each_umc(umc) {
		pvt->csels[umc].b_cnt = 8;
		pvt->csels[umc].m_cnt = 8;
	}
}

static int gpu_hw_info_get(struct amd64_pvt *pvt)
{
	int ret;

	ret = gpu_get_node_map(pvt);
	if (ret)
		return ret;

	pvt->umc = kcalloc(pvt->max_mcs, sizeof(struct amd64_umc), GFP_KERNEL);
	if (!pvt->umc)
		return -ENOMEM;

	gpu_prep_chip_selects(pvt);
	gpu_read_base_mask(pvt);
	gpu_read_mc_regs(pvt);

	return 0;
}

static void hw_info_put(struct amd64_pvt *pvt)
{
	pci_dev_put(pvt->F1);
	pci_dev_put(pvt->F2);
	kfree(pvt->umc);
}

static struct low_ops umc_ops = {
	.hw_info_get			= umc_hw_info_get,
	.ecc_enabled			= umc_ecc_enabled,
	.setup_mci_misc_attrs		= umc_setup_mci_misc_attrs,
	.dump_misc_regs			= umc_dump_misc_regs,
	.get_err_info			= umc_get_err_info,
};

static struct low_ops gpu_ops = {
	.hw_info_get			= gpu_hw_info_get,
	.ecc_enabled			= gpu_ecc_enabled,
	.setup_mci_misc_attrs		= gpu_setup_mci_misc_attrs,
	.dump_misc_regs			= gpu_dump_misc_regs,
	.get_err_info			= gpu_get_err_info,
};

/* Use Family 16h versions for defaults and adjust as needed below. */
static struct low_ops dct_ops = {
	.map_sysaddr_to_csrow		= f1x_map_sysaddr_to_csrow,
	.dbam_to_cs			= f16_dbam_to_chip_select,
	.hw_info_get			= dct_hw_info_get,
	.ecc_enabled			= dct_ecc_enabled,
	.setup_mci_misc_attrs		= dct_setup_mci_misc_attrs,
	.dump_misc_regs			= dct_dump_misc_regs,
};

static int per_family_init(struct amd64_pvt *pvt)
{
	pvt->ext_model  = boot_cpu_data.x86_model >> 4;
	pvt->stepping	= boot_cpu_data.x86_stepping;
	pvt->model	= boot_cpu_data.x86_model;
	pvt->fam	= boot_cpu_data.x86;
	pvt->max_mcs	= 2;

	/*
	 * Decide on which ops group to use here and do any family/model
	 * overrides below.
	 */
	if (pvt->fam >= 0x17)
		pvt->ops = &umc_ops;
	else
		pvt->ops = &dct_ops;

	switch (pvt->fam) {
	case 0xf:
		pvt->ctl_name				= (pvt->ext_model >= K8_REV_F) ?
							  "K8 revF or later" : "K8 revE or earlier";
		pvt->f1_id				= PCI_DEVICE_ID_AMD_K8_NB_ADDRMAP;
		pvt->f2_id				= PCI_DEVICE_ID_AMD_K8_NB_MEMCTL;
		pvt->ops->map_sysaddr_to_csrow		= k8_map_sysaddr_to_csrow;
		pvt->ops->dbam_to_cs			= k8_dbam_to_chip_select;
		break;

	case 0x10:
		pvt->ctl_name				= "F10h";
		pvt->f1_id				= PCI_DEVICE_ID_AMD_10H_NB_MAP;
		pvt->f2_id				= PCI_DEVICE_ID_AMD_10H_NB_DRAM;
		pvt->ops->dbam_to_cs			= f10_dbam_to_chip_select;
		break;

	case 0x15:
		switch (pvt->model) {
		case 0x30:
			pvt->ctl_name			= "F15h_M30h";
			pvt->f1_id			= PCI_DEVICE_ID_AMD_15H_M30H_NB_F1;
			pvt->f2_id			= PCI_DEVICE_ID_AMD_15H_M30H_NB_F2;
			break;
		case 0x60:
			pvt->ctl_name			= "F15h_M60h";
			pvt->f1_id			= PCI_DEVICE_ID_AMD_15H_M60H_NB_F1;
			pvt->f2_id			= PCI_DEVICE_ID_AMD_15H_M60H_NB_F2;
			pvt->ops->dbam_to_cs		= f15_m60h_dbam_to_chip_select;
			break;
		case 0x13:
			/* Richland is only client */
			return -ENODEV;
		default:
			pvt->ctl_name			= "F15h";
			pvt->f1_id			= PCI_DEVICE_ID_AMD_15H_NB_F1;
			pvt->f2_id			= PCI_DEVICE_ID_AMD_15H_NB_F2;
			pvt->ops->dbam_to_cs		= f15_dbam_to_chip_select;
			break;
		}
		break;

	case 0x16:
		switch (pvt->model) {
		case 0x30:
			pvt->ctl_name			= "F16h_M30h";
			pvt->f1_id			= PCI_DEVICE_ID_AMD_16H_M30H_NB_F1;
			pvt->f2_id			= PCI_DEVICE_ID_AMD_16H_M30H_NB_F2;
			break;
		default:
			pvt->ctl_name			= "F16h";
			pvt->f1_id			= PCI_DEVICE_ID_AMD_16H_NB_F1;
			pvt->f2_id			= PCI_DEVICE_ID_AMD_16H_NB_F2;
			break;
		}
		break;

	case 0x17:
		switch (pvt->model) {
		case 0x10 ... 0x2f:
			pvt->ctl_name			= "F17h_M10h";
			break;
		case 0x30 ... 0x3f:
			pvt->ctl_name			= "F17h_M30h";
			pvt->max_mcs			= 8;
			break;
		case 0x60 ... 0x6f:
			pvt->ctl_name			= "F17h_M60h";
			break;
		case 0x70 ... 0x7f:
			pvt->ctl_name			= "F17h_M70h";
			break;
		default:
			pvt->ctl_name			= "F17h";
			break;
		}
		break;

	case 0x18:
		pvt->ctl_name				= "F18h";
		break;

	case 0x19:
		switch (pvt->model) {
		case 0x00 ... 0x0f:
			pvt->ctl_name			= "F19h";
			pvt->max_mcs			= 8;
			break;
		case 0x10 ... 0x1f:
			pvt->ctl_name			= "F19h_M10h";
			pvt->max_mcs			= 12;
			pvt->flags.zn_regs_v2		= 1;
			break;
		case 0x20 ... 0x2f:
			pvt->ctl_name			= "F19h_M20h";
			break;
		case 0x30 ... 0x3f:
			if (pvt->F3->device == PCI_DEVICE_ID_AMD_MI200_DF_F3) {
				pvt->ctl_name		= "MI200";
				pvt->max_mcs		= 4;
				pvt->dram_type		= MEM_HBM2;
				pvt->gpu_umc_base	= 0x50000;
				pvt->ops		= &gpu_ops;
			} else {
				pvt->ctl_name		= "F19h_M30h";
				pvt->max_mcs		= 8;
			}
			break;
		case 0x50 ... 0x5f:
			pvt->ctl_name			= "F19h_M50h";
			break;
		case 0x60 ... 0x6f:
			pvt->ctl_name			= "F19h_M60h";
			pvt->flags.zn_regs_v2		= 1;
			break;
		case 0x70 ... 0x7f:
			pvt->ctl_name			= "F19h_M70h";
			pvt->flags.zn_regs_v2		= 1;
			break;
		case 0x90 ... 0x9f:
			pvt->ctl_name			= "F19h_M90h";
			pvt->max_mcs			= 4;
			pvt->dram_type			= MEM_HBM3;
			pvt->gpu_umc_base		= 0x90000;
			pvt->ops			= &gpu_ops;
			break;
		case 0xa0 ... 0xaf:
			pvt->ctl_name			= "F19h_MA0h";
			pvt->max_mcs			= 12;
			pvt->flags.zn_regs_v2		= 1;
			break;
		}
		break;

	case 0x1A:
		switch (pvt->model) {
		case 0x00 ... 0x1f:
			pvt->ctl_name           = "F1Ah";
			pvt->max_mcs            = 12;
			pvt->flags.zn_regs_v2   = 1;
			break;
		case 0x40 ... 0x4f:
			pvt->ctl_name           = "F1Ah_M40h";
			pvt->flags.zn_regs_v2   = 1;
			break;
		}
		break;

	default:
		amd64_err("Unsupported family!\n");
		return -ENODEV;
	}

	return 0;
}

static const struct attribute_group *amd64_edac_attr_groups[] = {
#ifdef CONFIG_EDAC_DEBUG
	&dbg_group,
	&inj_group,
#endif
	NULL
};

/*
 * For heterogeneous and APU models EDAC CHIP_SELECT and CHANNEL layers
 * should be swapped to fit into the layers.
 */
static unsigned int get_layer_size(struct amd64_pvt *pvt, u8 layer)
{
	bool is_gpu = (pvt->ops == &gpu_ops);

	if (!layer)
		return is_gpu ? pvt->max_mcs
			      : pvt->csels[0].b_cnt;
	else
		return is_gpu ? pvt->csels[0].b_cnt
			      : pvt->max_mcs;
}

static int init_one_instance(struct amd64_pvt *pvt)
{
	struct mem_ctl_info *mci = NULL;
	struct edac_mc_layer layers[2];
	int ret = -ENOMEM;

	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = get_layer_size(pvt, 0);
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = get_layer_size(pvt, 1);
	layers[1].is_virt_csrow = false;

	mci = edac_mc_alloc(pvt->mc_node_id, ARRAY_SIZE(layers), layers, 0);
	if (!mci)
		return ret;

	mci->pvt_info = pvt;
	mci->pdev = &pvt->F3->dev;

	pvt->ops->setup_mci_misc_attrs(mci);

	ret = -ENODEV;
	if (edac_mc_add_mc_with_groups(mci, amd64_edac_attr_groups)) {
		edac_dbg(1, "failed edac_mc_add_mc()\n");
		edac_mc_free(mci);
		return ret;
	}

	return 0;
}

static bool instance_has_memory(struct amd64_pvt *pvt)
{
	bool cs_enabled = false;
	int cs = 0, dct = 0;

	for (dct = 0; dct < pvt->max_mcs; dct++) {
		for_each_chip_select(cs, dct, pvt)
			cs_enabled |= csrow_enabled(cs, dct, pvt);
	}

	return cs_enabled;
}

static int probe_one_instance(unsigned int nid)
{
	struct pci_dev *F3 = node_to_amd_nb(nid)->misc;
	struct amd64_pvt *pvt = NULL;
	struct ecc_settings *s;
	int ret;

	ret = -ENOMEM;
	s = kzalloc(sizeof(struct ecc_settings), GFP_KERNEL);
	if (!s)
		goto err_out;

	ecc_stngs[nid] = s;

	pvt = kzalloc(sizeof(struct amd64_pvt), GFP_KERNEL);
	if (!pvt)
		goto err_settings;

	pvt->mc_node_id	= nid;
	pvt->F3 = F3;

	ret = per_family_init(pvt);
	if (ret < 0)
		goto err_enable;

	ret = pvt->ops->hw_info_get(pvt);
	if (ret < 0)
		goto err_enable;

	ret = 0;
	if (!instance_has_memory(pvt)) {
		amd64_info("Node %d: No DIMMs detected.\n", nid);
		goto err_enable;
	}

	if (!pvt->ops->ecc_enabled(pvt)) {
		ret = -ENODEV;

		if (!ecc_enable_override)
			goto err_enable;

		if (boot_cpu_data.x86 >= 0x17) {
			amd64_warn("Forcing ECC on is not recommended on newer systems. Please enable ECC in BIOS.");
			goto err_enable;
		} else
			amd64_warn("Forcing ECC on!\n");

		if (!enable_ecc_error_reporting(s, nid, F3))
			goto err_enable;
	}

	ret = init_one_instance(pvt);
	if (ret < 0) {
		amd64_err("Error probing instance: %d\n", nid);

		if (boot_cpu_data.x86 < 0x17)
			restore_ecc_error_reporting(s, nid, F3);

		goto err_enable;
	}

	amd64_info("%s detected (node %d).\n", pvt->ctl_name, pvt->mc_node_id);

	/* Display and decode various registers for debug purposes. */
	pvt->ops->dump_misc_regs(pvt);

	return ret;

err_enable:
	hw_info_put(pvt);
	kfree(pvt);

err_settings:
	kfree(s);
	ecc_stngs[nid] = NULL;

err_out:
	return ret;
}

static void remove_one_instance(unsigned int nid)
{
	struct pci_dev *F3 = node_to_amd_nb(nid)->misc;
	struct ecc_settings *s = ecc_stngs[nid];
	struct mem_ctl_info *mci;
	struct amd64_pvt *pvt;

	/* Remove from EDAC CORE tracking list */
	mci = edac_mc_del_mc(&F3->dev);
	if (!mci)
		return;

	pvt = mci->pvt_info;

	restore_ecc_error_reporting(s, nid, F3);

	kfree(ecc_stngs[nid]);
	ecc_stngs[nid] = NULL;

	/* Free the EDAC CORE resources */
	mci->pvt_info = NULL;

	hw_info_put(pvt);
	kfree(pvt);
	edac_mc_free(mci);
}

static void setup_pci_device(void)
{
	if (pci_ctl)
		return;

	pci_ctl = edac_pci_create_generic_ctl(pci_ctl_dev, EDAC_MOD_STR);
	if (!pci_ctl) {
		pr_warn("%s(): Unable to create PCI control\n", __func__);
		pr_warn("%s(): PCI error report via EDAC not set\n", __func__);
	}
}

static const struct x86_cpu_id amd64_cpuids[] = {
	X86_MATCH_VENDOR_FAM(AMD,	0x0F, NULL),
	X86_MATCH_VENDOR_FAM(AMD,	0x10, NULL),
	X86_MATCH_VENDOR_FAM(AMD,	0x15, NULL),
	X86_MATCH_VENDOR_FAM(AMD,	0x16, NULL),
	X86_MATCH_VENDOR_FAM(AMD,	0x17, NULL),
	X86_MATCH_VENDOR_FAM(HYGON,	0x18, NULL),
	X86_MATCH_VENDOR_FAM(AMD,	0x19, NULL),
	X86_MATCH_VENDOR_FAM(AMD,	0x1A, NULL),
	{ }
};
MODULE_DEVICE_TABLE(x86cpu, amd64_cpuids);

static int __init amd64_edac_init(void)
{
	const char *owner;
	int err = -ENODEV;
	int i;

	if (ghes_get_devices())
		return -EBUSY;

	owner = edac_get_owner();
	if (owner && strncmp(owner, EDAC_MOD_STR, sizeof(EDAC_MOD_STR)))
		return -EBUSY;

	if (!x86_match_cpu(amd64_cpuids))
		return -ENODEV;

	if (!amd_nb_num())
		return -ENODEV;

	opstate_init();

	err = -ENOMEM;
	ecc_stngs = kcalloc(amd_nb_num(), sizeof(ecc_stngs[0]), GFP_KERNEL);
	if (!ecc_stngs)
		goto err_free;

	msrs = msrs_alloc();
	if (!msrs)
		goto err_free;

	for (i = 0; i < amd_nb_num(); i++) {
		err = probe_one_instance(i);
		if (err) {
			/* unwind properly */
			while (--i >= 0)
				remove_one_instance(i);

			goto err_pci;
		}
	}

	if (!edac_has_mcs()) {
		err = -ENODEV;
		goto err_pci;
	}

	/* register stuff with EDAC MCE */
	if (boot_cpu_data.x86 >= 0x17) {
		amd_register_ecc_decoder(decode_umc_error);
	} else {
		amd_register_ecc_decoder(decode_bus_error);
		setup_pci_device();
	}

#ifdef CONFIG_X86_32
	amd64_err("%s on 32-bit is unsupported. USE AT YOUR OWN RISK!\n", EDAC_MOD_STR);
#endif

	return 0;

err_pci:
	pci_ctl_dev = NULL;

	msrs_free(msrs);
	msrs = NULL;

err_free:
	kfree(ecc_stngs);
	ecc_stngs = NULL;

	return err;
}

static void __exit amd64_edac_exit(void)
{
	int i;

	if (pci_ctl)
		edac_pci_release_generic_ctl(pci_ctl);

	/* unregister from EDAC MCE */
	if (boot_cpu_data.x86 >= 0x17)
		amd_unregister_ecc_decoder(decode_umc_error);
	else
		amd_unregister_ecc_decoder(decode_bus_error);

	for (i = 0; i < amd_nb_num(); i++)
		remove_one_instance(i);

	kfree(ecc_stngs);
	ecc_stngs = NULL;

	pci_ctl_dev = NULL;

	msrs_free(msrs);
	msrs = NULL;
}

module_init(amd64_edac_init);
module_exit(amd64_edac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SoftwareBitMaker: Doug Thompson, Dave Peterson, Thayne Harbaugh; AMD");
MODULE_DESCRIPTION("MC support for AMD64 memory controllers");

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");
