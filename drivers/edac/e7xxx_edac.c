/*
 * Intel e7xxx Memory Controller kernel module
 * (C) 2003 Linux Networx (http://lnxi.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * See "enum e7xxx_chips" below for supported chipsets
 *
 * Written by Thayne Harbaugh
 * Based on work by Dan Hollis <goemon at anime dot net> and others.
 *	http://www.anime.net/~goemon/linux-ecc/
 *
 * Datasheet:
 *	http://www.intel.com/content/www/us/en/chipsets/e7501-chipset-memory-controller-hub-datasheet.html
 *
 * Contributors:
 *	Eric Biederman (Linux Networx)
 *	Tom Zimmerman (Linux Networx)
 *	Jim Garlick (Lawrence Livermore National Labs)
 *	Dave Peterson (Lawrence Livermore National Labs)
 *	That One Guy (Some other place)
 *	Wang Zhenyu (intel.com)
 *
 * $Id: edac_e7xxx.c,v 1.5.2.9 2005/10/05 00:43:44 dsp_llnl Exp $
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/edac.h>
#include "edac_module.h"

#define	EDAC_MOD_STR	"e7xxx_edac"

#define e7xxx_printk(level, fmt, arg...) \
	edac_printk(level, "e7xxx", fmt, ##arg)

#define e7xxx_mc_printk(mci, level, fmt, arg...) \
	edac_mc_chipset_printk(mci, level, "e7xxx", fmt, ##arg)

#ifndef PCI_DEVICE_ID_INTEL_7205_0
#define PCI_DEVICE_ID_INTEL_7205_0	0x255d
#endif				/* PCI_DEVICE_ID_INTEL_7205_0 */

#ifndef PCI_DEVICE_ID_INTEL_7205_1_ERR
#define PCI_DEVICE_ID_INTEL_7205_1_ERR	0x2551
#endif				/* PCI_DEVICE_ID_INTEL_7205_1_ERR */

#ifndef PCI_DEVICE_ID_INTEL_7500_0
#define PCI_DEVICE_ID_INTEL_7500_0	0x2540
#endif				/* PCI_DEVICE_ID_INTEL_7500_0 */

#ifndef PCI_DEVICE_ID_INTEL_7500_1_ERR
#define PCI_DEVICE_ID_INTEL_7500_1_ERR	0x2541
#endif				/* PCI_DEVICE_ID_INTEL_7500_1_ERR */

#ifndef PCI_DEVICE_ID_INTEL_7501_0
#define PCI_DEVICE_ID_INTEL_7501_0	0x254c
#endif				/* PCI_DEVICE_ID_INTEL_7501_0 */

#ifndef PCI_DEVICE_ID_INTEL_7501_1_ERR
#define PCI_DEVICE_ID_INTEL_7501_1_ERR	0x2541
#endif				/* PCI_DEVICE_ID_INTEL_7501_1_ERR */

#ifndef PCI_DEVICE_ID_INTEL_7505_0
#define PCI_DEVICE_ID_INTEL_7505_0	0x2550
#endif				/* PCI_DEVICE_ID_INTEL_7505_0 */

#ifndef PCI_DEVICE_ID_INTEL_7505_1_ERR
#define PCI_DEVICE_ID_INTEL_7505_1_ERR	0x2551
#endif				/* PCI_DEVICE_ID_INTEL_7505_1_ERR */

#define E7XXX_NR_CSROWS		8	/* number of csrows */
#define E7XXX_NR_DIMMS		8	/* 2 channels, 4 dimms/channel */

/* E7XXX register addresses - device 0 function 0 */
#define E7XXX_DRB		0x60	/* DRAM row boundary register (8b) */
#define E7XXX_DRA		0x70	/* DRAM row attribute register (8b) */
					/*
					 * 31   Device width row 7 0=x8 1=x4
					 * 27   Device width row 6
					 * 23   Device width row 5
					 * 19   Device width row 4
					 * 15   Device width row 3
					 * 11   Device width row 2
					 *  7   Device width row 1
					 *  3   Device width row 0
					 */
#define E7XXX_DRC		0x7C	/* DRAM controller mode reg (32b) */
					/*
					 * 22    Number channels 0=1,1=2
					 * 19:18 DRB Granularity 32/64MB
					 */
#define E7XXX_TOLM		0xC4	/* DRAM top of low memory reg (16b) */
#define E7XXX_REMAPBASE		0xC6	/* DRAM remap base address reg (16b) */
#define E7XXX_REMAPLIMIT	0xC8	/* DRAM remap limit address reg (16b) */

/* E7XXX register addresses - device 0 function 1 */
#define E7XXX_DRAM_FERR		0x80	/* DRAM first error register (8b) */
#define E7XXX_DRAM_NERR		0x82	/* DRAM next error register (8b) */
#define E7XXX_DRAM_CELOG_ADD	0xA0	/* DRAM first correctable memory */
					/*     error address register (32b) */
					/*
					 * 31:28 Reserved
					 * 27:6  CE address (4k block 33:12)
					 *  5:0  Reserved
					 */
#define E7XXX_DRAM_UELOG_ADD	0xB0	/* DRAM first uncorrectable memory */
					/*     error address register (32b) */
					/*
					 * 31:28 Reserved
					 * 27:6  CE address (4k block 33:12)
					 *  5:0  Reserved
					 */
#define E7XXX_DRAM_CELOG_SYNDROME 0xD0	/* DRAM first correctable memory */
					/*     error syndrome register (16b) */

enum e7xxx_chips {
	E7500 = 0,
	E7501,
	E7505,
	E7205,
};

struct e7xxx_pvt {
	struct pci_dev *bridge_ck;
	u32 tolm;
	u32 remapbase;
	u32 remaplimit;
	const struct e7xxx_dev_info *dev_info;
};

struct e7xxx_dev_info {
	u16 err_dev;
	const char *ctl_name;
};

struct e7xxx_error_info {
	u8 dram_ferr;
	u8 dram_nerr;
	u32 dram_celog_add;
	u16 dram_celog_syndrome;
	u32 dram_uelog_add;
};

static struct edac_pci_ctl_info *e7xxx_pci;

static const struct e7xxx_dev_info e7xxx_devs[] = {
	[E7500] = {
		.err_dev = PCI_DEVICE_ID_INTEL_7500_1_ERR,
		.ctl_name = "E7500"},
	[E7501] = {
		.err_dev = PCI_DEVICE_ID_INTEL_7501_1_ERR,
		.ctl_name = "E7501"},
	[E7505] = {
		.err_dev = PCI_DEVICE_ID_INTEL_7505_1_ERR,
		.ctl_name = "E7505"},
	[E7205] = {
		.err_dev = PCI_DEVICE_ID_INTEL_7205_1_ERR,
		.ctl_name = "E7205"},
};

/* FIXME - is this valid for both SECDED and S4ECD4ED? */
static inline int e7xxx_find_channel(u16 syndrome)
{
	edac_dbg(3, "\n");

	if ((syndrome & 0xff00) == 0)
		return 0;

	if ((syndrome & 0x00ff) == 0)
		return 1;

	if ((syndrome & 0xf000) == 0 || (syndrome & 0x0f00) == 0)
		return 0;

	return 1;
}

static unsigned long ctl_page_to_phys(struct mem_ctl_info *mci,
				unsigned long page)
{
	u32 remap;
	struct e7xxx_pvt *pvt = (struct e7xxx_pvt *)mci->pvt_info;

	edac_dbg(3, "\n");

	if ((page < pvt->tolm) ||
		((page >= 0x100000) && (page < pvt->remapbase)))
		return page;

	remap = (page - pvt->tolm) + pvt->remapbase;

	if (remap < pvt->remaplimit)
		return remap;

	e7xxx_printk(KERN_ERR, "Invalid page %lx - out of range\n", page);
	return pvt->tolm - 1;
}

static void process_ce(struct mem_ctl_info *mci, struct e7xxx_error_info *info)
{
	u32 error_1b, page;
	u16 syndrome;
	int row;
	int channel;

	edac_dbg(3, "\n");
	/* read the error address */
	error_1b = info->dram_celog_add;
	/* FIXME - should use PAGE_SHIFT */
	page = error_1b >> 6;	/* convert the address to 4k page */
	/* read the syndrome */
	syndrome = info->dram_celog_syndrome;
	/* FIXME - check for -1 */
	row = edac_mc_find_csrow_by_page(mci, page);
	/* convert syndrome to channel */
	channel = e7xxx_find_channel(syndrome);
	edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1, page, 0, syndrome,
			     row, channel, -1, "e7xxx CE", "");
}

static void process_ce_no_info(struct mem_ctl_info *mci)
{
	edac_dbg(3, "\n");
	edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1, 0, 0, 0, -1, -1, -1,
			     "e7xxx CE log register overflow", "");
}

static void process_ue(struct mem_ctl_info *mci, struct e7xxx_error_info *info)
{
	u32 error_2b, block_page;
	int row;

	edac_dbg(3, "\n");
	/* read the error address */
	error_2b = info->dram_uelog_add;
	/* FIXME - should use PAGE_SHIFT */
	block_page = error_2b >> 6;	/* convert to 4k address */
	row = edac_mc_find_csrow_by_page(mci, block_page);

	edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1, block_page, 0, 0,
			     row, -1, -1, "e7xxx UE", "");
}

static void process_ue_no_info(struct mem_ctl_info *mci)
{
	edac_dbg(3, "\n");

	edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1, 0, 0, 0, -1, -1, -1,
			     "e7xxx UE log register overflow", "");
}

static void e7xxx_get_error_info(struct mem_ctl_info *mci,
				 struct e7xxx_error_info *info)
{
	struct e7xxx_pvt *pvt;

	pvt = (struct e7xxx_pvt *)mci->pvt_info;
	pci_read_config_byte(pvt->bridge_ck, E7XXX_DRAM_FERR, &info->dram_ferr);
	pci_read_config_byte(pvt->bridge_ck, E7XXX_DRAM_NERR, &info->dram_nerr);

	if ((info->dram_ferr & 1) || (info->dram_nerr & 1)) {
		pci_read_config_dword(pvt->bridge_ck, E7XXX_DRAM_CELOG_ADD,
				&info->dram_celog_add);
		pci_read_config_word(pvt->bridge_ck,
				E7XXX_DRAM_CELOG_SYNDROME,
				&info->dram_celog_syndrome);
	}

	if ((info->dram_ferr & 2) || (info->dram_nerr & 2))
		pci_read_config_dword(pvt->bridge_ck, E7XXX_DRAM_UELOG_ADD,
				&info->dram_uelog_add);

	if (info->dram_ferr & 3)
		pci_write_bits8(pvt->bridge_ck, E7XXX_DRAM_FERR, 0x03, 0x03);

	if (info->dram_nerr & 3)
		pci_write_bits8(pvt->bridge_ck, E7XXX_DRAM_NERR, 0x03, 0x03);
}

static int e7xxx_process_error_info(struct mem_ctl_info *mci,
				struct e7xxx_error_info *info,
				int handle_errors)
{
	int error_found;

	error_found = 0;

	/* decode and report errors */
	if (info->dram_ferr & 1) {	/* check first error correctable */
		error_found = 1;

		if (handle_errors)
			process_ce(mci, info);
	}

	if (info->dram_ferr & 2) {	/* check first error uncorrectable */
		error_found = 1;

		if (handle_errors)
			process_ue(mci, info);
	}

	if (info->dram_nerr & 1) {	/* check next error correctable */
		error_found = 1;

		if (handle_errors) {
			if (info->dram_ferr & 1)
				process_ce_no_info(mci);
			else
				process_ce(mci, info);
		}
	}

	if (info->dram_nerr & 2) {	/* check next error uncorrectable */
		error_found = 1;

		if (handle_errors) {
			if (info->dram_ferr & 2)
				process_ue_no_info(mci);
			else
				process_ue(mci, info);
		}
	}

	return error_found;
}

static void e7xxx_check(struct mem_ctl_info *mci)
{
	struct e7xxx_error_info info;

	e7xxx_get_error_info(mci, &info);
	e7xxx_process_error_info(mci, &info, 1);
}

/* Return 1 if dual channel mode is active.  Else return 0. */
static inline int dual_channel_active(u32 drc, int dev_idx)
{
	return (dev_idx == E7501) ? ((drc >> 22) & 0x1) : 1;
}

/* Return DRB granularity (0=32mb, 1=64mb). */
static inline int drb_granularity(u32 drc, int dev_idx)
{
	/* only e7501 can be single channel */
	return (dev_idx == E7501) ? ((drc >> 18) & 0x3) : 1;
}

static void e7xxx_init_csrows(struct mem_ctl_info *mci, struct pci_dev *pdev,
			int dev_idx, u32 drc)
{
	unsigned long last_cumul_size;
	int index, j;
	u8 value;
	u32 dra, cumul_size, nr_pages;
	int drc_chan, drc_drbg, drc_ddim, mem_dev;
	struct csrow_info *csrow;
	struct dimm_info *dimm;
	enum edac_type edac_mode;

	pci_read_config_dword(pdev, E7XXX_DRA, &dra);
	drc_chan = dual_channel_active(drc, dev_idx);
	drc_drbg = drb_granularity(drc, dev_idx);
	drc_ddim = (drc >> 20) & 0x3;
	last_cumul_size = 0;

	/* The dram row boundary (DRB) reg values are boundary address
	 * for each DRAM row with a granularity of 32 or 64MB (single/dual
	 * channel operation).  DRB regs are cumulative; therefore DRB7 will
	 * contain the total memory contained in all eight rows.
	 */
	for (index = 0; index < mci->nr_csrows; index++) {
		/* mem_dev 0=x8, 1=x4 */
		mem_dev = (dra >> (index * 4 + 3)) & 0x1;
		csrow = mci->csrows[index];

		pci_read_config_byte(pdev, E7XXX_DRB + index, &value);
		/* convert a 64 or 32 MiB DRB to a page size. */
		cumul_size = value << (25 + drc_drbg - PAGE_SHIFT);
		edac_dbg(3, "(%d) cumul_size 0x%x\n", index, cumul_size);
		if (cumul_size == last_cumul_size)
			continue;	/* not populated */

		csrow->first_page = last_cumul_size;
		csrow->last_page = cumul_size - 1;
		nr_pages = cumul_size - last_cumul_size;
		last_cumul_size = cumul_size;

		/*
		* if single channel or x8 devices then SECDED
		* if dual channel and x4 then S4ECD4ED
		*/
		if (drc_ddim) {
			if (drc_chan && mem_dev) {
				edac_mode = EDAC_S4ECD4ED;
				mci->edac_cap |= EDAC_FLAG_S4ECD4ED;
			} else {
				edac_mode = EDAC_SECDED;
				mci->edac_cap |= EDAC_FLAG_SECDED;
			}
		} else
			edac_mode = EDAC_NONE;

		for (j = 0; j < drc_chan + 1; j++) {
			dimm = csrow->channels[j]->dimm;

			dimm->nr_pages = nr_pages / (drc_chan + 1);
			dimm->grain = 1 << 12;	/* 4KiB - resolution of CELOG */
			dimm->mtype = MEM_RDDR;	/* only one type supported */
			dimm->dtype = mem_dev ? DEV_X4 : DEV_X8;
			dimm->edac_mode = edac_mode;
		}
	}
}

static int e7xxx_probe1(struct pci_dev *pdev, int dev_idx)
{
	u16 pci_data;
	struct mem_ctl_info *mci = NULL;
	struct edac_mc_layer layers[2];
	struct e7xxx_pvt *pvt = NULL;
	u32 drc;
	int drc_chan;
	struct e7xxx_error_info discard;

	edac_dbg(0, "mci\n");

	pci_read_config_dword(pdev, E7XXX_DRC, &drc);

	drc_chan = dual_channel_active(drc, dev_idx);
	/*
	 * According with the datasheet, this device has a maximum of
	 * 4 DIMMS per channel, either single-rank or dual-rank. So, the
	 * total amount of dimms is 8 (E7XXX_NR_DIMMS).
	 * That means that the DIMM is mapped as CSROWs, and the channel
	 * will map the rank. So, an error to either channel should be
	 * attributed to the same dimm.
	 */
	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = E7XXX_NR_CSROWS;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = drc_chan + 1;
	layers[1].is_virt_csrow = false;
	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers, sizeof(*pvt));
	if (mci == NULL)
		return -ENOMEM;

	edac_dbg(3, "init mci\n");
	mci->mtype_cap = MEM_FLAG_RDDR;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED |
		EDAC_FLAG_S4ECD4ED;
	/* FIXME - what if different memory types are in different csrows? */
	mci->mod_name = EDAC_MOD_STR;
	mci->pdev = &pdev->dev;
	edac_dbg(3, "init pvt\n");
	pvt = (struct e7xxx_pvt *)mci->pvt_info;
	pvt->dev_info = &e7xxx_devs[dev_idx];
	pvt->bridge_ck = pci_get_device(PCI_VENDOR_ID_INTEL,
					pvt->dev_info->err_dev, pvt->bridge_ck);

	if (!pvt->bridge_ck) {
		e7xxx_printk(KERN_ERR, "error reporting device not found:"
			"vendor %x device 0x%x (broken BIOS?)\n",
			PCI_VENDOR_ID_INTEL, e7xxx_devs[dev_idx].err_dev);
		goto fail0;
	}

	edac_dbg(3, "more mci init\n");
	mci->ctl_name = pvt->dev_info->ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->edac_check = e7xxx_check;
	mci->ctl_page_to_phys = ctl_page_to_phys;
	e7xxx_init_csrows(mci, pdev, dev_idx, drc);
	mci->edac_cap |= EDAC_FLAG_NONE;
	edac_dbg(3, "tolm, remapbase, remaplimit\n");
	/* load the top of low memory, remap base, and remap limit vars */
	pci_read_config_word(pdev, E7XXX_TOLM, &pci_data);
	pvt->tolm = ((u32) pci_data) << 4;
	pci_read_config_word(pdev, E7XXX_REMAPBASE, &pci_data);
	pvt->remapbase = ((u32) pci_data) << 14;
	pci_read_config_word(pdev, E7XXX_REMAPLIMIT, &pci_data);
	pvt->remaplimit = ((u32) pci_data) << 14;
	e7xxx_printk(KERN_INFO,
		"tolm = %x, remapbase = %x, remaplimit = %x\n", pvt->tolm,
		pvt->remapbase, pvt->remaplimit);

	/* clear any pending errors, or initial state bits */
	e7xxx_get_error_info(mci, &discard);

	/* Here we assume that we will never see multiple instances of this
	 * type of memory controller.  The ID is therefore hardcoded to 0.
	 */
	if (edac_mc_add_mc(mci)) {
		edac_dbg(3, "failed edac_mc_add_mc()\n");
		goto fail1;
	}

	/* allocating generic PCI control info */
	e7xxx_pci = edac_pci_create_generic_ctl(&pdev->dev, EDAC_MOD_STR);
	if (!e7xxx_pci) {
		printk(KERN_WARNING
			"%s(): Unable to create PCI control\n",
			__func__);
		printk(KERN_WARNING
			"%s(): PCI error report via EDAC not setup\n",
			__func__);
	}

	/* get this far and it's successful */
	edac_dbg(3, "success\n");
	return 0;

fail1:
	pci_dev_put(pvt->bridge_ck);

fail0:
	edac_mc_free(mci);

	return -ENODEV;
}

/* returns count (>= 0), or negative on error */
static int e7xxx_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	edac_dbg(0, "\n");

	/* wake up and enable device */
	return pci_enable_device(pdev) ?
		-EIO : e7xxx_probe1(pdev, ent->driver_data);
}

static void e7xxx_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;
	struct e7xxx_pvt *pvt;

	edac_dbg(0, "\n");

	if (e7xxx_pci)
		edac_pci_release_generic_ctl(e7xxx_pci);

	if ((mci = edac_mc_del_mc(&pdev->dev)) == NULL)
		return;

	pvt = (struct e7xxx_pvt *)mci->pvt_info;
	pci_dev_put(pvt->bridge_ck);
	edac_mc_free(mci);
}

static const struct pci_device_id e7xxx_pci_tbl[] = {
	{
	 PCI_VEND_DEV(INTEL, 7205_0), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 E7205},
	{
	 PCI_VEND_DEV(INTEL, 7500_0), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 E7500},
	{
	 PCI_VEND_DEV(INTEL, 7501_0), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 E7501},
	{
	 PCI_VEND_DEV(INTEL, 7505_0), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 E7505},
	{
	 0,
	 }			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, e7xxx_pci_tbl);

static struct pci_driver e7xxx_driver = {
	.name = EDAC_MOD_STR,
	.probe = e7xxx_init_one,
	.remove = e7xxx_remove_one,
	.id_table = e7xxx_pci_tbl,
};

static int __init e7xxx_init(void)
{
       /* Ensure that the OPSTATE is set correctly for POLL or NMI */
       opstate_init();

	return pci_register_driver(&e7xxx_driver);
}

static void __exit e7xxx_exit(void)
{
	pci_unregister_driver(&e7xxx_driver);
}

module_init(e7xxx_init);
module_exit(e7xxx_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linux Networx (http://lnxi.com) Thayne Harbaugh et al\n"
		"Based on.work by Dan Hollis et al");
MODULE_DESCRIPTION("MC support for Intel e7xxx memory controllers");
module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");
