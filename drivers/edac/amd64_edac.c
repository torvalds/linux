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
	u64 dram_base, hole_base, hole_offset, hole_size, dram_addr;
	int ret = 0;

	dram_base = get_dram_base(mci);

	ret = amd64_get_dram_hole_info(mci, &hole_base, &hole_offset,
				      &hole_size);
	if (!ret) {
		if ((sys_addr >= (1ull << 32)) &&
		    (sys_addr < ((1ull << 32) + hole_size))) {
			/* use DHAR to translate SysAddr to DramAddr */
			dram_addr = sys_addr - hole_offset;

			debugf2("using DHAR to translate SysAddr 0x%lx to "
				"DramAddr 0x%lx\n",
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
	dram_addr = (sys_addr & 0xffffffffffull) - dram_base;

	debugf2("using DRAM Base register to translate SysAddr 0x%lx to "
		"DramAddr 0x%lx\n", (unsigned long)sys_addr,
		(unsigned long)dram_addr);
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
	intlv_shift = num_node_interleave_bits(pvt->dram_IntlvEn[0]);
	input_addr = ((dram_addr >> intlv_shift) & 0xffffff000ull) +
	    (dram_addr & 0xfff);

	debugf2("  Intlv Shift=%d DramAddr=0x%lx maps to InputAddr=0x%lx\n",
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

	debugf2("SysAdddr 0x%lx translates to InputAddr 0x%lx\n",
		(unsigned long)sys_addr, (unsigned long)input_addr);

	return input_addr;
}


/*
 * @input_addr is an InputAddr associated with the node represented by mci.
 * Translate @input_addr to a DramAddr and return the result.
 */
static u64 input_addr_to_dram_addr(struct mem_ctl_info *mci, u64 input_addr)
{
	struct amd64_pvt *pvt;
	int node_id, intlv_shift;
	u64 bits, dram_addr;
	u32 intlv_sel;

	/*
	 * Near the start of section 3.4.4 (p. 70, BKDG #26094, K8, revA-E)
	 * shows how to translate a DramAddr to an InputAddr. Here we reverse
	 * this procedure. When translating from a DramAddr to an InputAddr, the
	 * bits used for node interleaving are discarded.  Here we recover these
	 * bits from the IntlvSel field of the DRAM Limit register (section
	 * 3.4.4.2) for the node that input_addr is associated with.
	 */
	pvt = mci->pvt_info;
	node_id = pvt->mc_node_id;
	BUG_ON((node_id < 0) || (node_id > 7));

	intlv_shift = num_node_interleave_bits(pvt->dram_IntlvEn[0]);

	if (intlv_shift == 0) {
		debugf1("    InputAddr 0x%lx translates to DramAddr of "
			"same value\n",	(unsigned long)input_addr);

		return input_addr;
	}

	bits = ((input_addr & 0xffffff000ull) << intlv_shift) +
	    (input_addr & 0xfff);

	intlv_sel = pvt->dram_IntlvSel[node_id] & ((1 << intlv_shift) - 1);
	dram_addr = bits + (intlv_sel << 12);

	debugf1("InputAddr 0x%lx translates to DramAddr 0x%lx "
		"(%d node interleave bits)\n", (unsigned long)input_addr,
		(unsigned long)dram_addr, intlv_shift);

	return dram_addr;
}

/*
 * @dram_addr is a DramAddr that maps to the node represented by mci. Convert
 * @dram_addr to a SysAddr.
 */
static u64 dram_addr_to_sys_addr(struct mem_ctl_info *mci, u64 dram_addr)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	u64 hole_base, hole_offset, hole_size, base, limit, sys_addr;
	int ret = 0;

	ret = amd64_get_dram_hole_info(mci, &hole_base, &hole_offset,
				      &hole_size);
	if (!ret) {
		if ((dram_addr >= hole_base) &&
		    (dram_addr < (hole_base + hole_size))) {
			sys_addr = dram_addr + hole_offset;

			debugf1("using DHAR to translate DramAddr 0x%lx to "
				"SysAddr 0x%lx\n", (unsigned long)dram_addr,
				(unsigned long)sys_addr);

			return sys_addr;
		}
	}

	amd64_get_base_and_limit(pvt, pvt->mc_node_id, &base, &limit);
	sys_addr = dram_addr + base;

	/*
	 * The sys_addr we have computed up to this point is a 40-bit value
	 * because the k8 deals with 40-bit values.  However, the value we are
	 * supposed to return is a full 64-bit physical address.  The AMD
	 * x86-64 architecture specifies that the most significant implemented
	 * address bit through bit 63 of a physical address must be either all
	 * 0s or all 1s.  Therefore we sign-extend the 40-bit sys_addr to a
	 * 64-bit value below.  See section 3.4.2 of AMD publication 24592:
	 * AMD x86-64 Architecture Programmer's Manual Volume 1 Application
	 * Programming.
	 */
	sys_addr |= ~((sys_addr & (1ull << 39)) - 1);

	debugf1("    Node %d, DramAddr 0x%lx to SysAddr 0x%lx\n",
		pvt->mc_node_id, (unsigned long)dram_addr,
		(unsigned long)sys_addr);

	return sys_addr;
}

/*
 * @input_addr is an InputAddr associated with the node given by mci. Translate
 * @input_addr to a SysAddr.
 */
static inline u64 input_addr_to_sys_addr(struct mem_ctl_info *mci,
					 u64 input_addr)
{
	return dram_addr_to_sys_addr(mci,
				     input_addr_to_dram_addr(mci, input_addr));
}

/*
 * Find the minimum and maximum InputAddr values that map to the given @csrow.
 * Pass back these values in *input_addr_min and *input_addr_max.
 */
static void find_csrow_limits(struct mem_ctl_info *mci, int csrow,
			      u64 *input_addr_min, u64 *input_addr_max)
{
	struct amd64_pvt *pvt;
	u64 base, mask;

	pvt = mci->pvt_info;
	BUG_ON((csrow < 0) || (csrow >= CHIPSELECT_COUNT));

	base = base_from_dct_base(pvt, csrow);
	mask = mask_from_dct_mask(pvt, csrow);

	*input_addr_min = base & ~mask;
	*input_addr_max = base | mask | pvt->dcs_mask_notused;
}

/*
 * Extract error address from MCA NB Address Low (section 3.6.4.5) and MCA NB
 * Address High (section 3.6.4.6) register values and return the result. Address
 * is located in the info structure (nbeah and nbeal), the encoding is device
 * specific.
 */
static u64 extract_error_address(struct mem_ctl_info *mci,
				 struct amd64_error_info_regs *info)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	return pvt->ops->get_error_address(mci, info);
}


/* Map the Error address to a PAGE and PAGE OFFSET. */
static inline void error_address_to_page_and_offset(u64 error_address,
						    u32 *page, u32 *offset)
{
	*page = (u32) (error_address >> PAGE_SHIFT);
	*offset = ((u32) error_address) & ~PAGE_MASK;
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
		amd64_mc_printk(mci, KERN_ERR,
			     "Failed to translate InputAddr to csrow for "
			     "address 0x%lx\n", (unsigned long)sys_addr);
	return csrow;
}

static int get_channel_from_ecc_syndrome(unsigned short syndrome);

static void amd64_cpu_display_info(struct amd64_pvt *pvt)
{
	if (boot_cpu_data.x86 == 0x11)
		edac_printk(KERN_DEBUG, EDAC_MC, "F11h CPU detected\n");
	else if (boot_cpu_data.x86 == 0x10)
		edac_printk(KERN_DEBUG, EDAC_MC, "F10h CPU detected\n");
	else if (boot_cpu_data.x86 == 0xf)
		edac_printk(KERN_DEBUG, EDAC_MC, "%s detected\n",
			(pvt->ext_model >= OPTERON_CPU_REV_F) ?
			"Rev F or later" : "Rev E or earlier");
	else
		/* we'll hardly ever ever get here */
		edac_printk(KERN_ERR, EDAC_MC, "Unknown cpu!\n");
}

/*
 * Determine if the DIMMs have ECC enabled. ECC is enabled ONLY if all the DIMMs
 * are ECC capable.
 */
static enum edac_type amd64_determine_edac_cap(struct amd64_pvt *pvt)
{
	int bit;
	enum dev_type edac_cap = EDAC_NONE;

	bit = (boot_cpu_data.x86 > 0xf || pvt->ext_model >= OPTERON_CPU_REV_F)
		? 19
		: 17;

	if (pvt->dclr0 >> BIT(bit))
		edac_cap = EDAC_FLAG_SECDED;

	return edac_cap;
}


static void f10_debug_display_dimm_sizes(int ctrl, struct amd64_pvt *pvt,
					 int ganged);

/* Display and decode various NB registers for debug purposes. */
static void amd64_dump_misc_regs(struct amd64_pvt *pvt)
{
	int ganged;

	debugf1("  nbcap:0x%8.08x DctDualCap=%s DualNode=%s 8-Node=%s\n",
		pvt->nbcap,
		(pvt->nbcap & K8_NBCAP_DCT_DUAL) ? "True" : "False",
		(pvt->nbcap & K8_NBCAP_DUAL_NODE) ? "True" : "False",
		(pvt->nbcap & K8_NBCAP_8_NODE) ? "True" : "False");
	debugf1("    ECC Capable=%s   ChipKill Capable=%s\n",
		(pvt->nbcap & K8_NBCAP_SECDED) ? "True" : "False",
		(pvt->nbcap & K8_NBCAP_CHIPKILL) ? "True" : "False");
	debugf1("  DramCfg0-low=0x%08x DIMM-ECC=%s Parity=%s Width=%s\n",
		pvt->dclr0,
		(pvt->dclr0 & BIT(19)) ?  "Enabled" : "Disabled",
		(pvt->dclr0 & BIT(8)) ?  "Enabled" : "Disabled",
		(pvt->dclr0 & BIT(11)) ?  "128b" : "64b");
	debugf1("    DIMM x4 Present: L0=%s L1=%s L2=%s L3=%s  DIMM Type=%s\n",
		(pvt->dclr0 & BIT(12)) ?  "Y" : "N",
		(pvt->dclr0 & BIT(13)) ?  "Y" : "N",
		(pvt->dclr0 & BIT(14)) ?  "Y" : "N",
		(pvt->dclr0 & BIT(15)) ?  "Y" : "N",
		(pvt->dclr0 & BIT(16)) ?  "UN-Buffered" : "Buffered");


	debugf1("  online-spare: 0x%8.08x\n", pvt->online_spare);

	if (boot_cpu_data.x86 == 0xf) {
		debugf1("  dhar: 0x%8.08x Base=0x%08x Offset=0x%08x\n",
			pvt->dhar, dhar_base(pvt->dhar),
			k8_dhar_offset(pvt->dhar));
		debugf1("      DramHoleValid=%s\n",
			(pvt->dhar & DHAR_VALID) ?  "True" : "False");

		debugf1("  dbam-dkt: 0x%8.08x\n", pvt->dbam0);

		/* everything below this point is Fam10h and above */
		return;

	} else {
		debugf1("  dhar: 0x%8.08x Base=0x%08x Offset=0x%08x\n",
			pvt->dhar, dhar_base(pvt->dhar),
			f10_dhar_offset(pvt->dhar));
		debugf1("    DramMemHoistValid=%s DramHoleValid=%s\n",
			(pvt->dhar & F10_DRAM_MEM_HOIST_VALID) ?
			"True" : "False",
			(pvt->dhar & DHAR_VALID) ?
			"True" : "False");
	}

	/* Only if NOT ganged does dcl1 have valid info */
	if (!dct_ganging_enabled(pvt)) {
		debugf1("  DramCfg1-low=0x%08x DIMM-ECC=%s Parity=%s "
			"Width=%s\n", pvt->dclr1,
			(pvt->dclr1 & BIT(19)) ?  "Enabled" : "Disabled",
			(pvt->dclr1 & BIT(8)) ?  "Enabled" : "Disabled",
			(pvt->dclr1 & BIT(11)) ?  "128b" : "64b");
		debugf1("    DIMM x4 Present: L0=%s L1=%s L2=%s L3=%s  "
			"DIMM Type=%s\n",
			(pvt->dclr1 & BIT(12)) ?  "Y" : "N",
			(pvt->dclr1 & BIT(13)) ?  "Y" : "N",
			(pvt->dclr1 & BIT(14)) ?  "Y" : "N",
			(pvt->dclr1 & BIT(15)) ?  "Y" : "N",
			(pvt->dclr1 & BIT(16)) ?  "UN-Buffered" : "Buffered");
	}

	/*
	 * Determine if ganged and then dump memory sizes for first controller,
	 * and if NOT ganged dump info for 2nd controller.
	 */
	ganged = dct_ganging_enabled(pvt);

	f10_debug_display_dimm_sizes(0, pvt, ganged);

	if (!ganged)
		f10_debug_display_dimm_sizes(1, pvt, ganged);
}

/* Read in both of DBAM registers */
static void amd64_read_dbam_reg(struct amd64_pvt *pvt)
{
	int err = 0;
	unsigned int reg;

	reg = DBAM0;
	err = pci_read_config_dword(pvt->dram_f2_ctl, reg, &pvt->dbam0);
	if (err)
		goto err_reg;

	if (boot_cpu_data.x86 >= 0x10) {
		reg = DBAM1;
		err = pci_read_config_dword(pvt->dram_f2_ctl, reg, &pvt->dbam1);

		if (err)
			goto err_reg;
	}

err_reg:
	debugf0("Error reading F2x%03x.\n", reg);
}

/*
 * NOTE: CPU Revision Dependent code: Rev E and Rev F
 *
 * Set the DCSB and DCSM mask values depending on the CPU revision value. Also
 * set the shift factor for the DCSB and DCSM values.
 *
 * ->dcs_mask_notused, RevE:
 *
 * To find the max InputAddr for the csrow, start with the base address and set
 * all bits that are "don't care" bits in the test at the start of section
 * 3.5.4 (p. 84).
 *
 * The "don't care" bits are all set bits in the mask and all bits in the gaps
 * between bit ranges [35:25] and [19:13]. The value REV_E_DCS_NOTUSED_BITS
 * represents bits [24:20] and [12:0], which are all bits in the above-mentioned
 * gaps.
 *
 * ->dcs_mask_notused, RevF and later:
 *
 * To find the max InputAddr for the csrow, start with the base address and set
 * all bits that are "don't care" bits in the test at the start of NPT section
 * 4.5.4 (p. 87).
 *
 * The "don't care" bits are all set bits in the mask and all bits in the gaps
 * between bit ranges [36:27] and [21:13].
 *
 * The value REV_F_F1Xh_DCS_NOTUSED_BITS represents bits [26:22] and [12:0],
 * which are all bits in the above-mentioned gaps.
 */
static void amd64_set_dct_base_and_mask(struct amd64_pvt *pvt)
{
	if (pvt->ext_model >= OPTERON_CPU_REV_F) {
		pvt->dcsb_base		= REV_F_F1Xh_DCSB_BASE_BITS;
		pvt->dcsm_mask		= REV_F_F1Xh_DCSM_MASK_BITS;
		pvt->dcs_mask_notused	= REV_F_F1Xh_DCS_NOTUSED_BITS;
		pvt->dcs_shift		= REV_F_F1Xh_DCS_SHIFT;

		switch (boot_cpu_data.x86) {
		case 0xf:
			pvt->num_dcsm = REV_F_DCSM_COUNT;
			break;

		case 0x10:
			pvt->num_dcsm = F10_DCSM_COUNT;
			break;

		case 0x11:
			pvt->num_dcsm = F11_DCSM_COUNT;
			break;

		default:
			amd64_printk(KERN_ERR, "Unsupported family!\n");
			break;
		}
	} else {
		pvt->dcsb_base		= REV_E_DCSB_BASE_BITS;
		pvt->dcsm_mask		= REV_E_DCSM_MASK_BITS;
		pvt->dcs_mask_notused	= REV_E_DCS_NOTUSED_BITS;
		pvt->dcs_shift		= REV_E_DCS_SHIFT;
		pvt->num_dcsm		= REV_E_DCSM_COUNT;
	}
}

/*
 * Function 2 Offset F10_DCSB0; read in the DCS Base and DCS Mask hw registers
 */
static void amd64_read_dct_base_mask(struct amd64_pvt *pvt)
{
	int cs, reg, err = 0;

	amd64_set_dct_base_and_mask(pvt);

	for (cs = 0; cs < CHIPSELECT_COUNT; cs++) {
		reg = K8_DCSB0 + (cs * 4);
		err = pci_read_config_dword(pvt->dram_f2_ctl, reg,
						&pvt->dcsb0[cs]);
		if (unlikely(err))
			debugf0("Reading K8_DCSB0[%d] failed\n", cs);
		else
			debugf0("  DCSB0[%d]=0x%08x reg: F2x%x\n",
				cs, pvt->dcsb0[cs], reg);

		/* If DCT are NOT ganged, then read in DCT1's base */
		if (boot_cpu_data.x86 >= 0x10 && !dct_ganging_enabled(pvt)) {
			reg = F10_DCSB1 + (cs * 4);
			err = pci_read_config_dword(pvt->dram_f2_ctl, reg,
							&pvt->dcsb1[cs]);
			if (unlikely(err))
				debugf0("Reading F10_DCSB1[%d] failed\n", cs);
			else
				debugf0("  DCSB1[%d]=0x%08x reg: F2x%x\n",
					cs, pvt->dcsb1[cs], reg);
		} else {
			pvt->dcsb1[cs] = 0;
		}
	}

	for (cs = 0; cs < pvt->num_dcsm; cs++) {
		reg = K8_DCSB0 + (cs * 4);
		err = pci_read_config_dword(pvt->dram_f2_ctl, reg,
					&pvt->dcsm0[cs]);
		if (unlikely(err))
			debugf0("Reading K8_DCSM0 failed\n");
		else
			debugf0("    DCSM0[%d]=0x%08x reg: F2x%x\n",
				cs, pvt->dcsm0[cs], reg);

		/* If DCT are NOT ganged, then read in DCT1's mask */
		if (boot_cpu_data.x86 >= 0x10 && !dct_ganging_enabled(pvt)) {
			reg = F10_DCSM1 + (cs * 4);
			err = pci_read_config_dword(pvt->dram_f2_ctl, reg,
					&pvt->dcsm1[cs]);
			if (unlikely(err))
				debugf0("Reading F10_DCSM1[%d] failed\n", cs);
			else
				debugf0("    DCSM1[%d]=0x%08x reg: F2x%x\n",
					cs, pvt->dcsm1[cs], reg);
		} else
			pvt->dcsm1[cs] = 0;
	}
}

static enum mem_type amd64_determine_memory_type(struct amd64_pvt *pvt)
{
	enum mem_type type;

	if (boot_cpu_data.x86 >= 0x10 || pvt->ext_model >= OPTERON_CPU_REV_F) {
		/* Rev F and later */
		type = (pvt->dclr0 & BIT(16)) ? MEM_DDR2 : MEM_RDDR2;
	} else {
		/* Rev E and earlier */
		type = (pvt->dclr0 & BIT(18)) ? MEM_DDR : MEM_RDDR;
	}

	debugf1("  Memory type is: %s\n",
		(type == MEM_DDR2) ? "MEM_DDR2" :
		(type == MEM_RDDR2) ? "MEM_RDDR2" :
		(type == MEM_DDR) ? "MEM_DDR" : "MEM_RDDR");

	return type;
}

