/*
 * Intel 82975X Memory Controller kernel module
 * (C) 2007 aCarLab (India) Pvt. Ltd. (http://acarlab.com)
 * (C) 2007 jetzbroadband (http://jetzbroadband.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written by Arvind R.
 *   Copied from i82875p_edac.c source:
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/edac.h>
#include "edac_core.h"

#define I82975X_REVISION	" Ver: 1.0.0"
#define EDAC_MOD_STR		"i82975x_edac"

#define i82975x_printk(level, fmt, arg...) \
	edac_printk(level, "i82975x", fmt, ##arg)

#define i82975x_mc_printk(mci, level, fmt, arg...) \
	edac_mc_chipset_printk(mci, level, "i82975x", fmt, ##arg)

#ifndef PCI_DEVICE_ID_INTEL_82975_0
#define PCI_DEVICE_ID_INTEL_82975_0	0x277c
#endif				/* PCI_DEVICE_ID_INTEL_82975_0 */

#define I82975X_NR_DIMMS		8
#define I82975X_NR_CSROWS(nr_chans)	(I82975X_NR_DIMMS / (nr_chans))

/* Intel 82975X register addresses - device 0 function 0 - DRAM Controller */
#define I82975X_EAP		0x58	/* Dram Error Address Pointer (32b)
					 *
					 * 31:7  128 byte cache-line address
					 * 6:1   reserved
					 * 0     0: CH0; 1: CH1
					 */

#define I82975X_DERRSYN		0x5c	/* Dram Error SYNdrome (8b)
					 *
					 *  7:0  DRAM ECC Syndrome
					 */

#define I82975X_DES		0x5d	/* Dram ERRor DeSTination (8b)
					 * 0h:    Processor Memory Reads
					 * 1h:7h  reserved
					 * More - See Page 65 of Intel DocSheet.
					 */

#define I82975X_ERRSTS		0xc8	/* Error Status Register (16b)
					 *
					 * 15:12 reserved
					 * 11    Thermal Sensor Event
					 * 10    reserved
					 *  9    non-DRAM lock error (ndlock)
					 *  8    Refresh Timeout
					 *  7:2  reserved
					 *  1    ECC UE (multibit DRAM error)
					 *  0    ECC CE (singlebit DRAM error)
					 */

/* Error Reporting is supported by 3 mechanisms:
  1. DMI SERR generation  ( ERRCMD )
  2. SMI DMI  generation  ( SMICMD )
  3. SCI DMI  generation  ( SCICMD )
NOTE: Only ONE of the three must be enabled
*/
#define I82975X_ERRCMD		0xca	/* Error Command (16b)
					 *
					 * 15:12 reserved
					 * 11    Thermal Sensor Event
					 * 10    reserved
					 *  9    non-DRAM lock error (ndlock)
					 *  8    Refresh Timeout
					 *  7:2  reserved
					 *  1    ECC UE (multibit DRAM error)
					 *  0    ECC CE (singlebit DRAM error)
					 */

#define I82975X_SMICMD		0xcc	/* Error Command (16b)
					 *
					 * 15:2  reserved
					 *  1    ECC UE (multibit DRAM error)
					 *  0    ECC CE (singlebit DRAM error)
					 */

#define I82975X_SCICMD		0xce	/* Error Command (16b)
					 *
					 * 15:2  reserved
					 *  1    ECC UE (multibit DRAM error)
					 *  0    ECC CE (singlebit DRAM error)
					 */

#define I82975X_XEAP	0xfc	/* Extended Dram Error Address Pointer (8b)
					 *
					 * 7:1   reserved
					 * 0     Bit32 of the Dram Error Address
					 */

#define I82975X_MCHBAR		0x44	/*
					 *
					 * 31:14 Base Addr of 16K memory-mapped
					 *	configuration space
					 * 13:1  reserverd
					 *  0    mem-mapped config space enable
					 */

/* NOTE: Following addresses have to indexed using MCHBAR offset (44h, 32b) */
/* Intel 82975x memory mapped register space */

#define I82975X_DRB_SHIFT 25	/* fixed 32MiB grain */

#define I82975X_DRB		0x100	/* DRAM Row Boundary (8b x 8)
					 *
					 * 7   set to 1 in highest DRB of
					 *	channel if 4GB in ch.
					 * 6:2 upper boundary of rank in
					 *	32MB grains
					 * 1:0 set to 0
					 */
#define I82975X_DRB_CH0R0		0x100
#define I82975X_DRB_CH0R1		0x101
#define I82975X_DRB_CH0R2		0x102
#define I82975X_DRB_CH0R3		0x103
#define I82975X_DRB_CH1R0		0x180
#define I82975X_DRB_CH1R1		0x181
#define I82975X_DRB_CH1R2		0x182
#define I82975X_DRB_CH1R3		0x183


#define I82975X_DRA		0x108	/* DRAM Row Attribute (4b x 8)
					 *  defines the PAGE SIZE to be used
					 *	for the rank
					 *  7    reserved
					 *  6:4  row attr of odd rank, i.e. 1
					 *  3    reserved
					 *  2:0  row attr of even rank, i.e. 0
					 *
					 * 000 = unpopulated
					 * 001 = reserved
					 * 010 = 4KiB
					 * 011 = 8KiB
					 * 100 = 16KiB
					 * others = reserved
					 */
#define I82975X_DRA_CH0R01		0x108
#define I82975X_DRA_CH0R23		0x109
#define I82975X_DRA_CH1R01		0x188
#define I82975X_DRA_CH1R23		0x189


#define I82975X_BNKARC	0x10e /* Type of device in each rank - Bank Arch (16b)
					 *
					 * 15:8  reserved
					 * 7:6  Rank 3 architecture
					 * 5:4  Rank 2 architecture
					 * 3:2  Rank 1 architecture
					 * 1:0  Rank 0 architecture
					 *
					 * 00 => 4 banks
					 * 01 => 8 banks
					 */
#define I82975X_C0BNKARC	0x10e
#define I82975X_C1BNKARC	0x18e



#define I82975X_DRC		0x120 /* DRAM Controller Mode0 (32b)
					 *
					 * 31:30 reserved
					 * 29    init complete
					 * 28:11 reserved, according to Intel
					 *    22:21 number of channels
					 *		00=1 01=2 in 82875
					 *		seems to be ECC mode
					 *		bits in 82975 in Asus
					 *		P5W
					 *	 19:18 Data Integ Mode
					 *		00=none 01=ECC in 82875
					 * 10:8  refresh mode
					 *  7    reserved
					 *  6:4  mode select
					 *  3:2  reserved
					 *  1:0  DRAM type 10=Second Revision
					 *		DDR2 SDRAM
					 *         00, 01, 11 reserved
					 */
#define I82975X_DRC_CH0M0		0x120
#define I82975X_DRC_CH1M0		0x1A0


#define I82975X_DRC_M1	0x124 /* DRAM Controller Mode1 (32b)
					 * 31	0=Standard Address Map
					 *	1=Enhanced Address Map
					 * 30:0	reserved
					 */

#define I82975X_DRC_CH0M1		0x124
#define I82975X_DRC_CH1M1		0x1A4

enum i82975x_chips {
	I82975X = 0,
};

struct i82975x_pvt {
	void __iomem *mch_window;
};

struct i82975x_dev_info {
	const char *ctl_name;
};

struct i82975x_error_info {
	u16 errsts;
	u32 eap;
	u8 des;
	u8 derrsyn;
	u16 errsts2;
	u8 chan;		/* the channel is bit 0 of EAP */
	u8 xeap;		/* extended eap bit */
};

static const struct i82975x_dev_info i82975x_devs[] = {
	[I82975X] = {
		.ctl_name = "i82975x"
	},
};

static struct pci_dev *mci_pdev;	/* init dev: in case that AGP code has
					 * already registered driver
					 */

static int i82975x_registered = 1;

static void i82975x_get_error_info(struct mem_ctl_info *mci,
		struct i82975x_error_info *info)
{
	struct pci_dev *pdev;

	pdev = to_pci_dev(mci->pdev);

	/*
	 * This is a mess because there is no atomic way to read all the
	 * registers at once and the registers can transition from CE being
	 * overwritten by UE.
	 */
	pci_read_config_word(pdev, I82975X_ERRSTS, &info->errsts);
	pci_read_config_dword(pdev, I82975X_EAP, &info->eap);
	pci_read_config_byte(pdev, I82975X_XEAP, &info->xeap);
	pci_read_config_byte(pdev, I82975X_DES, &info->des);
	pci_read_config_byte(pdev, I82975X_DERRSYN, &info->derrsyn);
	pci_read_config_word(pdev, I82975X_ERRSTS, &info->errsts2);

	pci_write_bits16(pdev, I82975X_ERRSTS, 0x0003, 0x0003);

	/*
	 * If the error is the same then we can for both reads then
	 * the first set of reads is valid.  If there is a change then
	 * there is a CE no info and the second set of reads is valid
	 * and should be UE info.
	 */
	if (!(info->errsts2 & 0x0003))
		return;

	if ((info->errsts ^ info->errsts2) & 0x0003) {
		pci_read_config_dword(pdev, I82975X_EAP, &info->eap);
		pci_read_config_byte(pdev, I82975X_XEAP, &info->xeap);
		pci_read_config_byte(pdev, I82975X_DES, &info->des);
		pci_read_config_byte(pdev, I82975X_DERRSYN,
				&info->derrsyn);
	}
}

static int i82975x_process_error_info(struct mem_ctl_info *mci,
		struct i82975x_error_info *info, int handle_errors)
{
	int row, chan;
	unsigned long offst, page;

	if (!(info->errsts2 & 0x0003))
		return 0;

	if (!handle_errors)
		return 1;

	if ((info->errsts ^ info->errsts2) & 0x0003) {
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1, 0, 0, 0,
				     -1, -1, -1, "UE overwrote CE", "");
		info->errsts = info->errsts2;
	}

	page = (unsigned long) info->eap;
	page >>= 1;
	if (info->xeap & 1)
		page |= 0x80000000;
	page >>= (PAGE_SHIFT - 1);
	row = edac_mc_find_csrow_by_page(mci, page);

	if (row == -1)	{
		i82975x_mc_printk(mci, KERN_ERR, "error processing EAP:\n"
			"\tXEAP=%u\n"
			"\t EAP=0x%08x\n"
			"\tPAGE=0x%08x\n",
			(info->xeap & 1) ? 1 : 0, info->eap, (unsigned int) page);
		return 0;
	}
	chan = (mci->csrows[row]->nr_channels == 1) ? 0 : info->eap & 1;
	offst = info->eap
			& ((1 << PAGE_SHIFT) -
			   (1 << mci->csrows[row]->channels[chan]->dimm->grain));

	if (info->errsts & 0x0002)
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1,
				     page, offst, 0,
				     row, -1, -1,
				     "i82975x UE", "");
	else
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1,
				     page, offst, info->derrsyn,
				     row, chan ? chan : 0, -1,
				     "i82975x CE", "");

	return 1;
}

static void i82975x_check(struct mem_ctl_info *mci)
{
	struct i82975x_error_info info;

	edac_dbg(1, "MC%d\n", mci->mc_idx);
	i82975x_get_error_info(mci, &info);
	i82975x_process_error_info(mci, &info, 1);
}

/* Return 1 if dual channel mode is active.  Else return 0. */
static int dual_channel_active(void __iomem *mch_window)
{
	/*
	 * We treat interleaved-symmetric configuration as dual-channel - EAP's
	 * bit-0 giving the channel of the error location.
	 *
	 * All other configurations are treated as single channel - the EAP's
	 * bit-0 will resolve ok in symmetric area of mixed
	 * (symmetric/asymmetric) configurations
	 */
	u8	drb[4][2];
	int	row;
	int    dualch;

	for (dualch = 1, row = 0; dualch && (row < 4); row++) {
		drb[row][0] = readb(mch_window + I82975X_DRB + row);
		drb[row][1] = readb(mch_window + I82975X_DRB + row + 0x80);
		dualch = dualch && (drb[row][0] == drb[row][1]);
	}
	return dualch;
}

static enum dev_type i82975x_dram_type(void __iomem *mch_window, int rank)
{
	/*
	 * ECC is possible on i92975x ONLY with DEV_X8
	 */
	return DEV_X8;
}

static void i82975x_init_csrows(struct mem_ctl_info *mci,
		struct pci_dev *pdev, void __iomem *mch_window)
{
	struct csrow_info *csrow;
	unsigned long last_cumul_size;
	u8 value;
	u32 cumul_size, nr_pages;
	int index, chan;
	struct dimm_info *dimm;
	enum dev_type dtype;

	last_cumul_size = 0;

	/*
	 * 82875 comment:
	 * The dram row boundary (DRB) reg values are boundary address
	 * for each DRAM row with a granularity of 32 or 64MB (single/dual
	 * channel operation).  DRB regs are cumulative; therefore DRB7 will
	 * contain the total memory contained in all rows.
	 *
	 */

	for (index = 0; index < mci->nr_csrows; index++) {
		csrow = mci->csrows[index];

		value = readb(mch_window + I82975X_DRB + index +
					((index >= 4) ? 0x80 : 0));
		cumul_size = value;
		cumul_size <<= (I82975X_DRB_SHIFT - PAGE_SHIFT);
		/*
		 * Adjust cumul_size w.r.t number of channels
		 *
		 */
		if (csrow->nr_channels > 1)
			cumul_size <<= 1;
		edac_dbg(3, "(%d) cumul_size 0x%x\n", index, cumul_size);

		nr_pages = cumul_size - last_cumul_size;
		if (!nr_pages)
			continue;

		/*
		 * Initialise dram labels
		 * index values:
		 *   [0-7] for single-channel; i.e. csrow->nr_channels = 1
		 *   [0-3] for dual-channel; i.e. csrow->nr_channels = 2
		 */
		dtype = i82975x_dram_type(mch_window, index);
		for (chan = 0; chan < csrow->nr_channels; chan++) {
			dimm = mci->csrows[index]->channels[chan]->dimm;

			dimm->nr_pages = nr_pages / csrow->nr_channels;

			snprintf(csrow->channels[chan]->dimm->label, EDAC_MC_LABEL_LEN, "DIMM %c%d",
				 (chan == 0) ? 'A' : 'B',
				 index);
			dimm->grain = 1 << 7;	/* 128Byte cache-line resolution */
			dimm->dtype = i82975x_dram_type(mch_window, index);
			dimm->mtype = MEM_DDR2; /* I82975x supports only DDR2 */
			dimm->edac_mode = EDAC_SECDED; /* only supported */
		}

		csrow->first_page = last_cumul_size;
		csrow->last_page = cumul_size - 1;
		last_cumul_size = cumul_size;
	}
}

/* #define  i82975x_DEBUG_IOMEM */

#ifdef i82975x_DEBUG_IOMEM
static void i82975x_print_dram_timings(void __iomem *mch_window)
{
	/*
	 * The register meanings are from Intel specs;
	 * (shows 13-5-5-5 for 800-DDR2)
	 * Asus P5W Bios reports 15-5-4-4
	 * What's your religion?
	 */
	static const int caslats[4] = { 5, 4, 3, 6 };
	u32	dtreg[2];

	dtreg[0] = readl(mch_window + 0x114);
	dtreg[1] = readl(mch_window + 0x194);
	i82975x_printk(KERN_INFO, "DRAM Timings :     Ch0    Ch1\n"
		"                RAS Active Min = %d     %d\n"
		"                CAS latency    =  %d      %d\n"
		"                RAS to CAS     =  %d      %d\n"
		"                RAS precharge  =  %d      %d\n",
		(dtreg[0] >> 19 ) & 0x0f,
			(dtreg[1] >> 19) & 0x0f,
		caslats[(dtreg[0] >> 8) & 0x03],
			caslats[(dtreg[1] >> 8) & 0x03],
		((dtreg[0] >> 4) & 0x07) + 2,
			((dtreg[1] >> 4) & 0x07) + 2,
		(dtreg[0] & 0x07) + 2,
			(dtreg[1] & 0x07) + 2
	);

}
#endif

static int i82975x_probe1(struct pci_dev *pdev, int dev_idx)
{
	int rc = -ENODEV;
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct i82975x_pvt *pvt;
	void __iomem *mch_window;
	u32 mchbar;
	u32 drc[2];
	struct i82975x_error_info discard;
	int	chans;
#ifdef i82975x_DEBUG_IOMEM
	u8 c0drb[4];
	u8 c1drb[4];
#endif

	edac_dbg(0, "\n");

	pci_read_config_dword(pdev, I82975X_MCHBAR, &mchbar);
	if (!(mchbar & 1)) {
		edac_dbg(3, "failed, MCHBAR disabled!\n");
		goto fail0;
	}
	mchbar &= 0xffffc000;	/* bits 31:14 used for 16K window */
	mch_window = ioremap_nocache(mchbar, 0x1000);

#ifdef i82975x_DEBUG_IOMEM
	i82975x_printk(KERN_INFO, "MCHBAR real = %0x, remapped = %p\n",
					mchbar, mch_window);

	c0drb[0] = readb(mch_window + I82975X_DRB_CH0R0);
	c0drb[1] = readb(mch_window + I82975X_DRB_CH0R1);
	c0drb[2] = readb(mch_window + I82975X_DRB_CH0R2);
	c0drb[3] = readb(mch_window + I82975X_DRB_CH0R3);
	c1drb[0] = readb(mch_window + I82975X_DRB_CH1R0);
	c1drb[1] = readb(mch_window + I82975X_DRB_CH1R1);
	c1drb[2] = readb(mch_window + I82975X_DRB_CH1R2);
	c1drb[3] = readb(mch_window + I82975X_DRB_CH1R3);
	i82975x_printk(KERN_INFO, "DRBCH0R0 = 0x%02x\n", c0drb[0]);
	i82975x_printk(KERN_INFO, "DRBCH0R1 = 0x%02x\n", c0drb[1]);
	i82975x_printk(KERN_INFO, "DRBCH0R2 = 0x%02x\n", c0drb[2]);
	i82975x_printk(KERN_INFO, "DRBCH0R3 = 0x%02x\n", c0drb[3]);
	i82975x_printk(KERN_INFO, "DRBCH1R0 = 0x%02x\n", c1drb[0]);
	i82975x_printk(KERN_INFO, "DRBCH1R1 = 0x%02x\n", c1drb[1]);
	i82975x_printk(KERN_INFO, "DRBCH1R2 = 0x%02x\n", c1drb[2]);
	i82975x_printk(KERN_INFO, "DRBCH1R3 = 0x%02x\n", c1drb[3]);
#endif

	drc[0] = readl(mch_window + I82975X_DRC_CH0M0);
	drc[1] = readl(mch_window + I82975X_DRC_CH1M0);
#ifdef i82975x_DEBUG_IOMEM
	i82975x_printk(KERN_INFO, "DRC_CH0 = %0x, %s\n", drc[0],
			((drc[0] >> 21) & 3) == 1 ?
				"ECC enabled" : "ECC disabled");
	i82975x_printk(KERN_INFO, "DRC_CH1 = %0x, %s\n", drc[1],
			((drc[1] >> 21) & 3) == 1 ?
				"ECC enabled" : "ECC disabled");

	i82975x_printk(KERN_INFO, "C0 BNKARC = %0x\n",
		readw(mch_window + I82975X_C0BNKARC));
	i82975x_printk(KERN_INFO, "C1 BNKARC = %0x\n",
		readw(mch_window + I82975X_C1BNKARC));
	i82975x_print_dram_timings(mch_window);
	goto fail1;
#endif
	if (!(((drc[0] >> 21) & 3) == 1 || ((drc[1] >> 21) & 3) == 1)) {
		i82975x_printk(KERN_INFO, "ECC disabled on both channels.\n");
		goto fail1;
	}

	chans = dual_channel_active(mch_window) + 1;

	/* assuming only one controller, index thus is 0 */
	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = I82975X_NR_DIMMS;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = I82975X_NR_CSROWS(chans);
	layers[1].is_virt_csrow = false;
	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers, sizeof(*pvt));
	if (!mci) {
		rc = -ENOMEM;
		goto fail1;
	}

	edac_dbg(3, "init mci\n");
	mci->pdev = &pdev->dev;
	mci->mtype_cap = MEM_FLAG_DDR2;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->mod_name = EDAC_MOD_STR;
	mci->mod_ver = I82975X_REVISION;
	mci->ctl_name = i82975x_devs[dev_idx].ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->edac_check = i82975x_check;
	mci->ctl_page_to_phys = NULL;
	edac_dbg(3, "init pvt\n");
	pvt = (struct i82975x_pvt *) mci->pvt_info;
	pvt->mch_window = mch_window;
	i82975x_init_csrows(mci, pdev, mch_window);
	mci->scrub_mode = SCRUB_HW_SRC;
	i82975x_get_error_info(mci, &discard);  /* clear counters */

	/* finalize this instance of memory controller with edac core */
	if (edac_mc_add_mc(mci)) {
		edac_dbg(3, "failed edac_mc_add_mc()\n");
		goto fail2;
	}

	/* get this far and it's successful */
	edac_dbg(3, "success\n");
	return 0;

fail2:
	edac_mc_free(mci);

fail1:
	iounmap(mch_window);
fail0:
	return rc;
}

/* returns count (>= 0), or negative on error */
static int i82975x_init_one(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	int rc;

	edac_dbg(0, "\n");

	if (pci_enable_device(pdev) < 0)
		return -EIO;

	rc = i82975x_probe1(pdev, ent->driver_data);

	if (mci_pdev == NULL)
		mci_pdev = pci_dev_get(pdev);

	return rc;
}

static void i82975x_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;
	struct i82975x_pvt *pvt;

	edac_dbg(0, "\n");

	mci = edac_mc_del_mc(&pdev->dev);
	if (mci  == NULL)
		return;

	pvt = mci->pvt_info;
	if (pvt->mch_window)
		iounmap( pvt->mch_window );

	edac_mc_free(mci);
}

static DEFINE_PCI_DEVICE_TABLE(i82975x_pci_tbl) = {
	{
		PCI_VEND_DEV(INTEL, 82975_0), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		I82975X
	},
	{
		0,
	}	/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, i82975x_pci_tbl);

static struct pci_driver i82975x_driver = {
	.name = EDAC_MOD_STR,
	.probe = i82975x_init_one,
	.remove = i82975x_remove_one,
	.id_table = i82975x_pci_tbl,
};

static int __init i82975x_init(void)
{
	int pci_rc;

	edac_dbg(3, "\n");

       /* Ensure that the OPSTATE is set correctly for POLL or NMI */
       opstate_init();

	pci_rc = pci_register_driver(&i82975x_driver);
	if (pci_rc < 0)
		goto fail0;

	if (mci_pdev == NULL) {
		mci_pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
				PCI_DEVICE_ID_INTEL_82975_0, NULL);

		if (!mci_pdev) {
			edac_dbg(0, "i82975x pci_get_device fail\n");
			pci_rc = -ENODEV;
			goto fail1;
		}

		pci_rc = i82975x_init_one(mci_pdev, i82975x_pci_tbl);

		if (pci_rc < 0) {
			edac_dbg(0, "i82975x init fail\n");
			pci_rc = -ENODEV;
			goto fail1;
		}
	}

	return 0;

fail1:
	pci_unregister_driver(&i82975x_driver);

fail0:
	if (mci_pdev != NULL)
		pci_dev_put(mci_pdev);

	return pci_rc;
}

static void __exit i82975x_exit(void)
{
	edac_dbg(3, "\n");

	pci_unregister_driver(&i82975x_driver);

	if (!i82975x_registered) {
		i82975x_remove_one(mci_pdev);
		pci_dev_put(mci_pdev);
	}
}

module_init(i82975x_init);
module_exit(i82975x_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arvind R. <arvino55@gmail.com>");
MODULE_DESCRIPTION("MC support for Intel 82975 memory hub controllers");

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");
