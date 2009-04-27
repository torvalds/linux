#include "amd64_edac.h"

static struct edac_pci_ctl_info *amd64_ctl_pci;

static int report_gart_errors;
module_param(report_gart_errors, int, 0644);

/*
 * Set by command line parameter. If BIOS has enabled the ECC, this override is
 * cleared to prevent re-enabling the hardware by this driver.
 */
static int ecc_enable_override;
module_param(ecc_enable_override, int, 0644);

/* Lookup table for all possible MC control instances */
struct amd64_pvt;
static struct mem_ctl_info *mci_lookup[MAX_NUMNODES];
static struct amd64_pvt *pvt_lookup[MAX_NUMNODES];

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
 * scan the scrub rate mapping table for a close or matching bandwidth value to
 * issue. If requested is too big, then use last maximum value found.
 */
static int amd64_search_set_scrub_rate(struct pci_dev *ctl, u32 new_bw,
				       u32 min_scrubrate)
{
	u32 scrubval;
	int i;

	/*
	 * map the configured rate (new_bw) to a value specific to the AMD64
	 * memory controller and apply to register. Search for the first
	 * bandwidth entry that is greater or equal than the setting requested
	 * and program that. If at last entry, turn off DRAM scrubbing.
	 */
	for (i = 0; i < ARRAY_SIZE(scrubrates); i++) {
		/*
		 * skip scrub rates which aren't recommended
		 * (see F10 BKDG, F3x58)
		 */
		if (scrubrates[i].scrubval < min_scrubrate)
			continue;

		if (scrubrates[i].bandwidth <= new_bw)
			break;

		/*
		 * if no suitable bandwidth found, turn off DRAM scrubbing
		 * entirely by falling back to the last element in the
		 * scrubrates array.
		 */
	}

	scrubval = scrubrates[i].scrubval;
	if (scrubval)
		edac_printk(KERN_DEBUG, EDAC_MC,
			    "Setting scrub rate bandwidth: %u\n",
			    scrubrates[i].bandwidth);
	else
		edac_printk(KERN_DEBUG, EDAC_MC, "Turning scrubbing off.\n");

	pci_write_bits32(ctl, K8_SCRCTRL, scrubval, 0x001F);

	return 0;
}

static int amd64_set_scrub_rate(struct mem_ctl_info *mci, u32 *bandwidth)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	u32 min_scrubrate = 0x0;

	switch (boot_cpu_data.x86) {
	case 0xf:
		min_scrubrate = K8_MIN_SCRUB_RATE_BITS;
		break;
	case 0x10:
		min_scrubrate = F10_MIN_SCRUB_RATE_BITS;
		break;
	case 0x11:
		min_scrubrate = F11_MIN_SCRUB_RATE_BITS;
		break;

	default:
		amd64_printk(KERN_ERR, "Unsupported family!\n");
		break;
	}
	return amd64_search_set_scrub_rate(pvt->misc_f3_ctl, *bandwidth,
			min_scrubrate);
}

static int amd64_get_scrub_rate(struct mem_ctl_info *mci, u32 *bw)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	u32 scrubval = 0;
	int status = -1, i, ret = 0;

	ret = pci_read_config_dword(pvt->misc_f3_ctl, K8_SCRCTRL, &scrubval);
	if (ret)
		debugf0("Reading K8_SCRCTRL failed\n");

	scrubval = scrubval & 0x001F;

	edac_printk(KERN_DEBUG, EDAC_MC,
		    "pci-read, sdram scrub control value: %d \n", scrubval);

	for (i = 0; ARRAY_SIZE(scrubrates); i++) {
		if (scrubrates[i].scrubval == scrubval) {
			*bw = scrubrates[i].bandwidth;
			status = 0;
			break;
		}
	}

	return status;
}

/* Map from a CSROW entry to the mask entry that operates on it */
static inline u32 amd64_map_to_dcs_mask(struct amd64_pvt *pvt, int csrow)
{
	return csrow >> (pvt->num_dcsm >> 3);
}

/* return the 'base' address the i'th CS entry of the 'dct' DRAM controller */
static u32 amd64_get_dct_base(struct amd64_pvt *pvt, int dct, int csrow)
{
	if (dct == 0)
		return pvt->dcsb0[csrow];
	else
		return pvt->dcsb1[csrow];
}

/*
 * Return the 'mask' address the i'th CS entry. This function is needed because
 * there number of DCSM registers on Rev E and prior vs Rev F and later is
 * different.
 */
static u32 amd64_get_dct_mask(struct amd64_pvt *pvt, int dct, int csrow)
{
	if (dct == 0)
		return pvt->dcsm0[amd64_map_to_dcs_mask(pvt, csrow)];
	else
		return pvt->dcsm1[amd64_map_to_dcs_mask(pvt, csrow)];
}


/*
 * In *base and *limit, pass back the full 40-bit base and limit physical
 * addresses for the node given by node_id.  This information is obtained from
 * DRAM Base (section 3.4.4.1) and DRAM Limit (section 3.4.4.2) registers. The
 * base and limit addresses are of type SysAddr, as defined at the start of
 * section 3.4.4 (p. 70).  They are the lowest and highest physical addresses
 * in the address range they represent.
 */
static void amd64_get_base_and_limit(struct amd64_pvt *pvt, int node_id,
			       u64 *base, u64 *limit)
{
	*base = pvt->dram_base[node_id];
	*limit = pvt->dram_limit[node_id];
}

/*
 * Return 1 if the SysAddr given by sys_addr matches the base/limit associated
 * with node_id
 */
static int amd64_base_limit_match(struct amd64_pvt *pvt,
					u64 sys_addr, int node_id)
{
	u64 base, limit, addr;

	amd64_get_base_and_limit(pvt, node_id, &base, &limit);

	/* The K8 treats this as a 40-bit value.  However, bits 63-40 will be
	 * all ones if the most significant implemented address bit is 1.
	 * Here we discard bits 63-40.  See section 3.4.2 of AMD publication
	 * 24592: AMD x86-64 Architecture Programmer's Manual Volume 1
	 * Application Programming.
	 */
	addr = sys_addr & 0x000000ffffffffffull;

	return (addr >= base) && (addr <= limit);
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
	int node_id;
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
	intlv_en = pvt->dram_IntlvEn[0];

	if (intlv_en == 0) {
		for (node_id = 0; ; ) {
			if (amd64_base_limit_match(pvt, sys_addr, node_id))
				break;

			if (++node_id >= DRAM_REG_COUNT)
				goto err_no_match;
		}
		goto found;
	}

	if (unlikely((intlv_en != (0x01 << 8)) &&
		     (intlv_en != (0x03 << 8)) &&
		     (intlv_en != (0x07 << 8)))) {
		amd64_printk(KERN_WARNING, "junk value of 0x%x extracted from "
			     "IntlvEn field of DRAM Base Register for node 0: "
			     "This probably indicates a BIOS bug.\n", intlv_en);
		return NULL;
	}

	bits = (((u32) sys_addr) >> 12) & intlv_en;

	for (node_id = 0; ; ) {
		if ((pvt->dram_limit[node_id] & intlv_en) == bits)
			break;	/* intlv_sel field matches */

		if (++node_id >= DRAM_REG_COUNT)
			goto err_no_match;
	}

	/* sanity test for sys_addr */
	if (unlikely(!amd64_base_limit_match(pvt, sys_addr, node_id))) {
		amd64_printk(KERN_WARNING,
			  "%s(): sys_addr 0x%lx falls outside base/limit "
			  "address range for node %d with node interleaving "
			  "enabled.\n", __func__, (unsigned long)sys_addr,
			  node_id);
		return NULL;
	}

found:
	return edac_mc_find(node_id);

err_no_match:
	debugf2("sys_addr 0x%lx doesn't match any node\n",
		(unsigned long)sys_addr);

	return NULL;
}

/*
 * Extract the DRAM CS base address from selected csrow register.
 */
static u64 base_from_dct_base(struct amd64_pvt *pvt, int csrow)
{
	return ((u64) (amd64_get_dct_base(pvt, 0, csrow) & pvt->dcsb_base)) <<
				pvt->dcs_shift;
}

/*
 * Extract the mask from the dcsb0[csrow] entry in a CPU revision-specific way.
 */
static u64 mask_from_dct_mask(struct amd64_pvt *pvt, int csrow)
{
	u64 dcsm_bits, other_bits;
	u64 mask;

	/* Extract bits from DRAM CS Mask. */
	dcsm_bits = amd64_get_dct_mask(pvt, 0, csrow) & pvt->dcsm_mask;

	other_bits = pvt->dcsm_mask;
	other_bits = ~(other_bits << pvt->dcs_shift);

	/*
	 * The extracted bits from DCSM belong in the spaces represented by
	 * the cleared bits in other_bits.
	 */
	mask = (dcsm_bits << pvt->dcs_shift) | other_bits;

	return mask;
}

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

	/*
	 * Here we use the DRAM CS Base and DRAM CS Mask registers. For each CS
	 * base/mask register pair, test the condition shown near the start of
	 * section 3.5.4 (p. 84, BKDG #26094, K8, revA-E).
	 */
	for (csrow = 0; csrow < CHIPSELECT_COUNT; csrow++) {

		/* This DRAM chip select is disabled on this node */
		if ((pvt->dcsb0[csrow] & K8_DCSB_CS_ENABLE) == 0)
			continue;

		base = base_from_dct_base(pvt, csrow);
		mask = ~mask_from_dct_mask(pvt, csrow);

		if ((input_addr & mask) == (base & mask)) {
			debugf2("InputAddr 0x%lx matches csrow %d (node %d)\n",
				(unsigned long)input_addr, csrow,
				pvt->mc_node_id);

			return csrow;
		}
	}

	debugf2("no matching csrow for InputAddr 0x%lx (MC node %d)\n",
		(unsigned long)input_addr, pvt->mc_node_id);

	return -1;
}

/*
 * Return the base value defined by the DRAM Base register for the node
 * represented by mci.  This function returns the full 40-bit value despite the
 * fact that the register only stores bits 39-24 of the value. See section
 * 3.4.4.1 (BKDG #26094, K8, revA-E)
 */
static inline u64 get_dram_base(struct mem_ctl_info *mci)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	return pvt->dram_base[pvt->mc_node_id];
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
int amd64_get_dram_hole_info(struct mem_ctl_info *mci, u64 *hole_base,
			     u64 *hole_offset, u64 *hole_size)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	u64 base;

	/* only revE and later have the DRAM Hole Address Register */
	if (boot_cpu_data.x86 == 0xf && pvt->ext_model < OPTERON_CPU_REV_E) {
		debugf1("  revision %d for node %d does not support DHAR\n",
			pvt->ext_model, pvt->mc_node_id);
		return 1;
	}

	/* only valid for Fam10h */
	if (boot_cpu_data.x86 == 0x10 &&
	    (pvt->dhar & F10_DRAM_MEM_HOIST_VALID) == 0) {
		debugf1("  Dram Memory Hoisting is DISABLED on this system\n");
		return 1;
	}

	if ((pvt->dhar & DHAR_VALID) == 0) {
		debugf1("  Dram Memory Hoisting is DISABLED on this node %d\n",
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

	base = dhar_base(pvt->dhar);

	*hole_base = base;
	*hole_size = (0x1ull << 32) - base;

	if (boot_cpu_data.x86 > 0xf)
		*hole_offset = f10_dhar_offset(pvt->dhar);
	else
		*hole_offset = k8_dhar_offset(pvt->dhar);

	debugf1("  DHAR info for node %d base 0x%lx offset 0x%lx size 0x%lx\n",
		pvt->mc_node_id, (unsigned long)*hole_base,
		(unsigned long)*hole_offset, (unsigned long)*hole_size);

	return 0;
}
EXPORT_SYMBOL_GPL(amd64_get_dram_hole_info);


