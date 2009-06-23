/* Intel 7 core  Memory Controller kernel module (Nehalem)
 *
 * This file may be distributed under the terms of the
 * GNU General Public License version 2 only.
 *
 * Copyright (c) 2009 by:
 *	 Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * Red Hat Inc. http://www.redhat.com
 *
 * Forked and adapted from the i5400_edac driver
 *
 * Based on the following public Intel datasheets:
 * Intel Core i7 Processor Extreme Edition and Intel Core i7 Processor
 * Datasheet, Volume 2:
 *	http://download.intel.com/design/processor/datashts/320835.pdf
 * Intel Xeon Processor 5500 Series Datasheet Volume 2
 *	http://www.intel.com/Assets/PDF/datasheet/321322.pdf
 * also available at:
 * 	http://www.arrownac.com/manufacturers/intel/s/nehalem/5500-datasheet-v2.pdf
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/edac.h>
#include <linux/mmzone.h>

#include "edac_core.h"

/* To use the new pci_[read/write]_config_qword instead of two dword */
#define USE_QWORD 1

/*
 * Alter this version for the module when modifications are made
 */
#define I7CORE_REVISION    " Ver: 1.0.0 " __DATE__
#define EDAC_MOD_STR      "i7core_edac"

/* HACK: temporary, just to enable all logs, for now */
#undef debugf0
#define debugf0(fmt, arg...)  edac_printk(KERN_INFO, "i7core", fmt, ##arg)

/*
 * Debug macros
 */
#define i7core_printk(level, fmt, arg...)			\
	edac_printk(level, "i7core", fmt, ##arg)

#define i7core_mc_printk(mci, level, fmt, arg...)		\
	edac_mc_chipset_printk(mci, level, "i7core", fmt, ##arg)

/*
 * i7core Memory Controller Registers
 */

	/* OFFSETS for Device 3 Function 0 */

#define MC_CONTROL	0x48
#define MC_STATUS	0x4c
#define MC_MAX_DOD	0x64

/*
 * OFFSETS for Device 3 Function 4, as inicated on Xeon 5500 datasheet:
 * http://www.arrownac.com/manufacturers/intel/s/nehalem/5500-datasheet-v2.pdf
 */

#define MC_TEST_ERR_RCV1	0x60
  #define DIMM2_COR_ERR(r)			((r) & 0x7fff)

#define MC_TEST_ERR_RCV0	0x64
  #define DIMM1_COR_ERR(r)			(((r) >> 16) & 0x7fff)
  #define DIMM0_COR_ERR(r)			((r) & 0x7fff)

	/* OFFSETS for Devices 4,5 and 6 Function 0 */

#define MC_CHANNEL_DIMM_INIT_PARAMS 0x58
  #define THREE_DIMMS_PRESENT		(1 << 24)
  #define SINGLE_QUAD_RANK_PRESENT	(1 << 23)
  #define QUAD_RANK_PRESENT		(1 << 22)
  #define REGISTERED_DIMM		(1 << 15)

#define MC_CHANNEL_MAPPER	0x60
  #define RDLCH(r, ch)		((((r) >> (3 + (ch * 6))) & 0x07) - 1)
  #define WRLCH(r, ch)		((((r) >> (ch * 6)) & 0x07) - 1)

#define MC_CHANNEL_RANK_PRESENT 0x7c
  #define RANK_PRESENT_MASK		0xffff

#define MC_CHANNEL_ADDR_MATCH	0xf0
#define MC_CHANNEL_ERROR_MASK	0xf8
#define MC_CHANNEL_ERROR_INJECT	0xfc
  #define INJECT_ADDR_PARITY	0x10
  #define INJECT_ECC		0x08
  #define MASK_CACHELINE	0x06
  #define MASK_FULL_CACHELINE	0x06
  #define MASK_MSB32_CACHELINE	0x04
  #define MASK_LSB32_CACHELINE	0x02
  #define NO_MASK_CACHELINE	0x00
  #define REPEAT_EN		0x01

	/* OFFSETS for Devices 4,5 and 6 Function 1 */
#define MC_DOD_CH_DIMM0		0x48
#define MC_DOD_CH_DIMM1		0x4c
#define MC_DOD_CH_DIMM2		0x50
  #define RANKOFFSET_MASK	((1 << 12) | (1 << 11) | (1 << 10))
  #define RANKOFFSET(x)		((x & RANKOFFSET_MASK) >> 10)
  #define DIMM_PRESENT_MASK	(1 << 9)
  #define DIMM_PRESENT(x)	(((x) & DIMM_PRESENT_MASK) >> 9)
  #define NUMBANK_MASK		((1 << 8) | (1 << 7))
  #define NUMBANK(x)		(((x) & NUMBANK_MASK) >> 7)
  #define NUMRANK_MASK		((1 << 6) | (1 << 5))
  #define NUMRANK(x)		(((x) & NUMRANK_MASK) >> 5)
  #define NUMROW_MASK		((1 << 4) | (1 << 3))
  #define NUMROW(x)		(((x) & NUMROW_MASK) >> 3)
  #define NUMCOL_MASK		3
  #define NUMCOL(x)		((x) & NUMCOL_MASK)

#define MC_RANK_PRESENT		0x7c

#define MC_SAG_CH_0	0x80
#define MC_SAG_CH_1	0x84
#define MC_SAG_CH_2	0x88
#define MC_SAG_CH_3	0x8c
#define MC_SAG_CH_4	0x90
#define MC_SAG_CH_5	0x94
#define MC_SAG_CH_6	0x98
#define MC_SAG_CH_7	0x9c

#define MC_RIR_LIMIT_CH_0	0x40
#define MC_RIR_LIMIT_CH_1	0x44
#define MC_RIR_LIMIT_CH_2	0x48
#define MC_RIR_LIMIT_CH_3	0x4C
#define MC_RIR_LIMIT_CH_4	0x50
#define MC_RIR_LIMIT_CH_5	0x54
#define MC_RIR_LIMIT_CH_6	0x58
#define MC_RIR_LIMIT_CH_7	0x5C
#define MC_RIR_LIMIT_MASK	((1 << 10) - 1)

#define MC_RIR_WAY_CH		0x80
  #define MC_RIR_WAY_OFFSET_MASK	(((1 << 14) - 1) & ~0x7)
  #define MC_RIR_WAY_RANK_MASK		0x7

/*
 * i7core structs
 */

#define NUM_CHANS 3
#define MAX_DIMMS 3		/* Max DIMMS per channel */
#define MAX_MCR_FUNC  4
#define MAX_CHAN_FUNC 3

struct i7core_info {
	u32	mc_control;
	u32	mc_status;
	u32	max_dod;
	u32	ch_map;
};


struct i7core_inject {
	int	enable;

	u32	section;
	u32	type;
	u32	eccmask;

	/* Error address mask */
	int channel, dimm, rank, bank, page, col;
};

struct i7core_channel {
	u32		ranks;
	u32		dimms;
};

struct pci_id_descr {
	int		dev;
	int		func;
	int 		dev_id;
	struct pci_dev	*pdev;
};

struct i7core_pvt {
	struct pci_dev		*pci_mcr[MAX_MCR_FUNC + 1];
	struct pci_dev		*pci_ch[NUM_CHANS][MAX_CHAN_FUNC + 1];
	struct i7core_info	info;
	struct i7core_inject	inject;
	struct i7core_channel	channel[NUM_CHANS];
	int			channels; /* Number of active channels */

	int		ce_count_available;
	unsigned long	ce_count[MAX_DIMMS];	/* ECC corrected errors counts per dimm */
	int		last_ce_count[MAX_DIMMS];

};

/* Device name and register DID (Device ID) */
struct i7core_dev_info {
	const char *ctl_name;	/* name for this device */
	u16 fsb_mapping_errors;	/* DID for the branchmap,control */
};

#define PCI_DESCR(device, function, device_id)	\
	.dev = (device),			\
	.func = (function),			\
	.dev_id = (device_id)

struct pci_id_descr pci_devs[] = {
		/* Memory controller */
	{ PCI_DESCR(3, 0, PCI_DEVICE_ID_INTEL_I7_MCR)     },
	{ PCI_DESCR(3, 1, PCI_DEVICE_ID_INTEL_I7_MC_TAD)  },
	{ PCI_DESCR(3, 2, PCI_DEVICE_ID_INTEL_I7_MC_RAS)  }, /* if RDIMM is supported */
	{ PCI_DESCR(3, 4, PCI_DEVICE_ID_INTEL_I7_MC_TEST) },

		/* Channel 0 */
	{ PCI_DESCR(4, 0, PCI_DEVICE_ID_INTEL_I7_MC_CH0_CTRL) },
	{ PCI_DESCR(4, 1, PCI_DEVICE_ID_INTEL_I7_MC_CH0_ADDR) },
	{ PCI_DESCR(4, 2, PCI_DEVICE_ID_INTEL_I7_MC_CH0_RANK) },
	{ PCI_DESCR(4, 3, PCI_DEVICE_ID_INTEL_I7_MC_CH0_TC)   },

		/* Channel 1 */
	{ PCI_DESCR(5, 0, PCI_DEVICE_ID_INTEL_I7_MC_CH1_CTRL) },
	{ PCI_DESCR(5, 1, PCI_DEVICE_ID_INTEL_I7_MC_CH1_ADDR) },
	{ PCI_DESCR(5, 2, PCI_DEVICE_ID_INTEL_I7_MC_CH1_RANK) },
	{ PCI_DESCR(5, 3, PCI_DEVICE_ID_INTEL_I7_MC_CH1_TC)   },

		/* Channel 2 */
	{ PCI_DESCR(6, 0, PCI_DEVICE_ID_INTEL_I7_MC_CH2_CTRL) },
	{ PCI_DESCR(6, 1, PCI_DEVICE_ID_INTEL_I7_MC_CH2_ADDR) },
	{ PCI_DESCR(6, 2, PCI_DEVICE_ID_INTEL_I7_MC_CH2_RANK) },
	{ PCI_DESCR(6, 3, PCI_DEVICE_ID_INTEL_I7_MC_CH2_TC)   },
};
#define N_DEVS ARRAY_SIZE(pci_devs)

/*
 *	pci_device_id	table for which devices we are looking for
 * This should match the first device at pci_devs table
 */
static const struct pci_device_id i7core_pci_tbl[] __devinitdata = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_I7_MCR)},
	{0,}			/* 0 terminated list. */
};


/* Table of devices attributes supported by this driver */
static const struct i7core_dev_info i7core_devs[] = {
	{
		.ctl_name = "i7 Core",
		.fsb_mapping_errors = PCI_DEVICE_ID_INTEL_I7_MCR,
	},
};

static struct edac_pci_ctl_info *i7core_pci;

/****************************************************************************
			Anciliary status routines
 ****************************************************************************/

	/* MC_CONTROL bits */
#define CH_ACTIVE(pvt, ch)	((pvt)->info.mc_control & (1 << (8 + ch)))
#define ECCx8(pvt)		((pvt)->info.mc_control & (1 << 1))

	/* MC_STATUS bits */
#define ECC_ENABLED(pvt)	((pvt)->info.mc_status & (1 << 3))
#define CH_DISABLED(pvt, ch)	((pvt)->info.mc_status & (1 << ch))

	/* MC_MAX_DOD read functions */
static inline int maxnumdimms(struct i7core_pvt *pvt)
{
	return (pvt->info.max_dod & 0x3) + 1;
}

static inline int maxnumrank(struct i7core_pvt *pvt)
{
	static int ranks[4] = { 1, 2, 4, -EINVAL };

	return ranks[(pvt->info.max_dod >> 2) & 0x3];
}

static inline int maxnumbank(struct i7core_pvt *pvt)
{
	static int banks[4] = { 4, 8, 16, -EINVAL };

	return banks[(pvt->info.max_dod >> 4) & 0x3];
}

static inline int maxnumrow(struct i7core_pvt *pvt)
{
	static int rows[8] = {
		1 << 12, 1 << 13, 1 << 14, 1 << 15,
		1 << 16, -EINVAL, -EINVAL, -EINVAL,
	};

	return rows[((pvt->info.max_dod >> 6) & 0x7)];
}

static inline int maxnumcol(struct i7core_pvt *pvt)
{
	static int cols[8] = {
		1 << 10, 1 << 11, 1 << 12, -EINVAL,
	};
	return cols[((pvt->info.max_dod >> 9) & 0x3) << 12];
}


/****************************************************************************
			Memory check routines
 ****************************************************************************/
static int i7core_get_active_channels(int *channels)
{
	struct pci_dev *pdev = NULL;
	int i;
	u32 status, control;

	*channels = 0;

	for (i = 0; i < N_DEVS; i++) {
		if (!pci_devs[i].pdev)
			continue;

		if (PCI_SLOT(pci_devs[i].pdev->devfn) == 3 &&
		    PCI_FUNC(pci_devs[i].pdev->devfn) == 0) {
			pdev = pci_devs[i].pdev;
			break;
		}
	}

	if (!pdev)
		return -ENODEV;

	/* Device 3 function 0 reads */
	pci_read_config_dword(pdev, MC_STATUS, &status);
	pci_read_config_dword(pdev, MC_CONTROL, &control);

	for (i = 0; i < NUM_CHANS; i++) {
		/* Check if the channel is active */
		if (!(control & (1 << (8 + i))))
			continue;

		/* Check if the channel is disabled */
		if (status & (1 << i)) {
			continue;
		}

		(*channels)++;
	}

	debugf0("Number of active channels: %d\n", *channels);

	return 0;
}

static int get_dimm_config(struct mem_ctl_info *mci)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	struct csrow_info *csr;
	int i, csrow = 0;
	enum edac_type mode;

	if (!pvt->pci_mcr[0])
		return -ENODEV;

	/* Device 3 function 0 reads */
	pci_read_config_dword(pvt->pci_mcr[0], MC_CONTROL,
					       &pvt->info.mc_control);
	pci_read_config_dword(pvt->pci_mcr[0], MC_STATUS,
					       &pvt->info.mc_status);
	pci_read_config_dword(pvt->pci_mcr[0], MC_MAX_DOD,
					       &pvt->info.max_dod);
	pci_read_config_dword(pvt->pci_mcr[0], MC_CHANNEL_MAPPER,
					       &pvt->info.ch_map);

	debugf0("MC control=0x%08x status=0x%08x dod=0x%08x map=0x%08x\n",
		pvt->info.mc_control, pvt->info.mc_status,
		pvt->info.max_dod, pvt->info.ch_map);

	if (ECC_ENABLED(pvt)) {
		debugf0("ECC enabled with x%d SDCC\n", ECCx8(pvt)?8:4);
		if (ECCx8(pvt))
			mode = EDAC_S8ECD8ED;
		else
			mode = EDAC_S4ECD4ED;
	} else {
		debugf0("ECC disabled\n");
		mode = EDAC_NONE;
	}

	/* FIXME: need to handle the error codes */
	debugf0("DOD Maximum limits: DIMMS: %d, %d-ranked, %d-banked\n",
		maxnumdimms(pvt), maxnumrank(pvt), maxnumbank(pvt));
	debugf0("DOD Maximum rows x colums = 0x%x x 0x%x\n",
		maxnumrow(pvt), maxnumcol(pvt));

	debugf0("Memory channel configuration:\n");

	for (i = 0; i < NUM_CHANS; i++) {
		u32 data;

		if (!CH_ACTIVE(pvt, i)) {
			debugf0("Channel %i is not active\n", i);
			continue;
		}
		if (CH_DISABLED(pvt, i)) {
			debugf0("Channel %i is disabled\n", i);
			continue;
		}

		/* Devices 4-6 function 0 */
		pci_read_config_dword(pvt->pci_ch[i][0],
				MC_CHANNEL_DIMM_INIT_PARAMS, &data);

		pvt->channel[i].ranks = (data & QUAD_RANK_PRESENT)? 4 : 2;

		if (data & THREE_DIMMS_PRESENT)
			pvt->channel[i].dimms = 3;
		else if (data & SINGLE_QUAD_RANK_PRESENT)
			pvt->channel[i].dimms = 1;
		else
			pvt->channel[i].dimms = 2;

		debugf0("Ch%d phy rd%d, wr%d (0x%08x): "
			"%d ranks, %d %cDIMMs, offset = %d\n",
			i,
			RDLCH(pvt->info.ch_map, i), WRLCH(pvt->info.ch_map, i),
			data,
			pvt->channel[i].ranks, pvt->channel[i].dimms,
			(data & REGISTERED_DIMM)? 'R' : 'U',
			RANKOFFSET(data));

		csr = &mci->csrows[csrow];
		csr->first_page = 0;
		csr->last_page = 0;
		csr->page_mask = 0;
		csr->nr_pages = 0;
		csr->grain = 0;
		csr->csrow_idx = csrow;
		csr->dtype = DEV_X8;	/* FIXME: check this */

		if (data & REGISTERED_DIMM)
			csr->mtype = MEM_RDDR3;
		else
			csr->mtype = MEM_DDR3;
		csr->edac_mode = mode;

		csrow++;
	}

	return 0;
}

/****************************************************************************
			Error insertion routines
 ****************************************************************************/

/* The i7core has independent error injection features per channel.
   However, to have a simpler code, we don't allow enabling error injection
   on more than one channel.
   Also, since a change at an inject parameter will be applied only at enable,
   we're disabling error injection on all write calls to the sysfs nodes that
   controls the error code injection.
 */
static int disable_inject(struct mem_ctl_info *mci)
{
	struct i7core_pvt *pvt = mci->pvt_info;

	pvt->inject.enable = 0;

	if (!pvt->pci_ch[pvt->inject.channel][0])
		return -ENODEV;

	pci_write_config_dword(pvt->pci_ch[pvt->inject.channel][0],
				MC_CHANNEL_ERROR_MASK, 0);

	return 0;
}

/*
 * i7core inject inject.section
 *
 *	accept and store error injection inject.section value
 *	bit 0 - refers to the lower 32-byte half cacheline
 *	bit 1 - refers to the upper 32-byte half cacheline
 */
static ssize_t i7core_inject_section_store(struct mem_ctl_info *mci,
					   const char *data, size_t count)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	unsigned long value;
	int rc;

	if (pvt->inject.enable)
		 disable_inject(mci);

	rc = strict_strtoul(data, 10, &value);
	if ((rc < 0) || (value > 3))
		return 0;

	pvt->inject.section = (u32) value;
	return count;
}

static ssize_t i7core_inject_section_show(struct mem_ctl_info *mci,
					      char *data)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	return sprintf(data, "0x%08x\n", pvt->inject.section);
}

/*
 * i7core inject.type
 *
 *	accept and store error injection inject.section value
 *	bit 0 - repeat enable - Enable error repetition
 *	bit 1 - inject ECC error
 *	bit 2 - inject parity error
 */
static ssize_t i7core_inject_type_store(struct mem_ctl_info *mci,
					const char *data, size_t count)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	unsigned long value;
	int rc;

	if (pvt->inject.enable)
		 disable_inject(mci);

	rc = strict_strtoul(data, 10, &value);
	if ((rc < 0) || (value > 7))
		return 0;

	pvt->inject.type = (u32) value;
	return count;
}

static ssize_t i7core_inject_type_show(struct mem_ctl_info *mci,
					      char *data)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	return sprintf(data, "0x%08x\n", pvt->inject.type);
}

/*
 * i7core_inject_inject.eccmask_store
 *
 * The type of error (UE/CE) will depend on the inject.eccmask value:
 *   Any bits set to a 1 will flip the corresponding ECC bit
 *   Correctable errors can be injected by flipping 1 bit or the bits within
 *   a symbol pair (2 consecutive aligned 8-bit pairs - i.e. 7:0 and 15:8 or
 *   23:16 and 31:24). Flipping bits in two symbol pairs will cause an
 *   uncorrectable error to be injected.
 */
static ssize_t i7core_inject_eccmask_store(struct mem_ctl_info *mci,
					const char *data, size_t count)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	unsigned long value;
	int rc;

	if (pvt->inject.enable)
		 disable_inject(mci);

	rc = strict_strtoul(data, 10, &value);
	if (rc < 0)
		return 0;

	pvt->inject.eccmask = (u32) value;
	return count;
}

static ssize_t i7core_inject_eccmask_show(struct mem_ctl_info *mci,
					      char *data)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	return sprintf(data, "0x%08x\n", pvt->inject.eccmask);
}

/*
 * i7core_addrmatch
 *
 * The type of error (UE/CE) will depend on the inject.eccmask value:
 *   Any bits set to a 1 will flip the corresponding ECC bit
 *   Correctable errors can be injected by flipping 1 bit or the bits within
 *   a symbol pair (2 consecutive aligned 8-bit pairs - i.e. 7:0 and 15:8 or
 *   23:16 and 31:24). Flipping bits in two symbol pairs will cause an
 *   uncorrectable error to be injected.
 */
static ssize_t i7core_inject_addrmatch_store(struct mem_ctl_info *mci,
					const char *data, size_t count)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	char *cmd, *val;
	long value;
	int rc;

	if (pvt->inject.enable)
		 disable_inject(mci);

	do {
		cmd = strsep((char **) &data, ":");
		if (!cmd)
			break;
		val = strsep((char **) &data, " \n\t");
		if (!val)
			return cmd - data;

		if (!strcasecmp(val,"any"))
			value = -1;
		else {
			rc = strict_strtol(val, 10, &value);
			if ((rc < 0) || (value < 0))
				return cmd - data;
		}

		if (!strcasecmp(cmd,"channel")) {
			if (value < 3)
				pvt->inject.channel = value;
			else
				return cmd - data;
		} else if (!strcasecmp(cmd,"dimm")) {
			if (value < 4)
				pvt->inject.dimm = value;
			else
				return cmd - data;
		} else if (!strcasecmp(cmd,"rank")) {
			if (value < 4)
				pvt->inject.rank = value;
			else
				return cmd - data;
		} else if (!strcasecmp(cmd,"bank")) {
			if (value < 4)
				pvt->inject.bank = value;
			else
				return cmd - data;
		} else if (!strcasecmp(cmd,"page")) {
			if (value <= 0xffff)
				pvt->inject.page = value;
			else
				return cmd - data;
		} else if (!strcasecmp(cmd,"col") ||
			   !strcasecmp(cmd,"column")) {
			if (value <= 0x3fff)
				pvt->inject.col = value;
			else
				return cmd - data;
		}
	} while (1);

	return count;
}

static ssize_t i7core_inject_addrmatch_show(struct mem_ctl_info *mci,
					      char *data)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	char channel[4], dimm[4], bank[4], rank[4], page[7], col[7];

	if (pvt->inject.channel < 0)
		sprintf(channel, "any");
	else
		sprintf(channel, "%d", pvt->inject.channel);
	if (pvt->inject.dimm < 0)
		sprintf(dimm, "any");
	else
		sprintf(dimm, "%d", pvt->inject.dimm);
	if (pvt->inject.bank < 0)
		sprintf(bank, "any");
	else
		sprintf(bank, "%d", pvt->inject.bank);
	if (pvt->inject.rank < 0)
		sprintf(rank, "any");
	else
		sprintf(rank, "%d", pvt->inject.rank);
	if (pvt->inject.page < 0)
		sprintf(page, "any");
	else
		sprintf(page, "0x%04x", pvt->inject.page);
	if (pvt->inject.col < 0)
		sprintf(col, "any");
	else
		sprintf(col, "0x%04x", pvt->inject.col);

	return sprintf(data, "channel: %s\ndimm: %s\nbank: %s\n"
			     "rank: %s\npage: %s\ncolumn: %s\n",
		       channel, dimm, bank, rank, page, col);
}

/*
 * This routine prepares the Memory Controller for error injection.
 * The error will be injected when some process tries to write to the
 * memory that matches the given criteria.
 * The criteria can be set in terms of a mask where dimm, rank, bank, page
 * and col can be specified.
 * A -1 value for any of the mask items will make the MCU to ignore
 * that matching criteria for error injection.
 *
 * It should be noticed that the error will only happen after a write operation
 * on a memory that matches the condition. if REPEAT_EN is not enabled at
 * inject mask, then it will produce just one error. Otherwise, it will repeat
 * until the injectmask would be cleaned.
 *
 * FIXME: This routine assumes that MAXNUMDIMMS value of MC_MAX_DOD
 *    is reliable enough to check if the MC is using the
 *    three channels. However, this is not clear at the datasheet.
 */
static ssize_t i7core_inject_enable_store(struct mem_ctl_info *mci,
				       const char *data, size_t count)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	u32 injectmask;
	u64 mask = 0;
	int  rc;
	long enable;

	if (!pvt->pci_ch[pvt->inject.channel][0])
		return 0;

	rc = strict_strtoul(data, 10, &enable);
	if ((rc < 0))
		return 0;

	if (enable) {
		pvt->inject.enable = 1;
	} else {
		disable_inject(mci);
		return count;
	}

	/* Sets pvt->inject.dimm mask */
	if (pvt->inject.dimm < 0)
		mask |= 1L << 41;
	else {
		if (pvt->channel[pvt->inject.channel].dimms > 2)
			mask |= (pvt->inject.dimm & 0x3L) << 35;
		else
			mask |= (pvt->inject.dimm & 0x1L) << 36;
	}

	/* Sets pvt->inject.rank mask */
	if (pvt->inject.rank < 0)
		mask |= 1L << 40;
	else {
		if (pvt->channel[pvt->inject.channel].dimms > 2)
			mask |= (pvt->inject.rank & 0x1L) << 34;
		else
			mask |= (pvt->inject.rank & 0x3L) << 34;
	}

	/* Sets pvt->inject.bank mask */
	if (pvt->inject.bank < 0)
		mask |= 1L << 39;
	else
		mask |= (pvt->inject.bank & 0x15L) << 30;

	/* Sets pvt->inject.page mask */
	if (pvt->inject.page < 0)
		mask |= 1L << 38;
	else
		mask |= (pvt->inject.page & 0xffffL) << 14;

	/* Sets pvt->inject.column mask */
	if (pvt->inject.col < 0)
		mask |= 1L << 37;
	else
		mask |= (pvt->inject.col & 0x3fffL);

#if USE_QWORD
	pci_write_config_qword(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ADDR_MATCH, mask);
#else
	pci_write_config_dword(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ADDR_MATCH, mask);
	pci_write_config_dword(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ADDR_MATCH + 4, mask >> 32L);
#endif

#if 1
#if USE_QWORD
	u64 rdmask;
	pci_read_config_qword(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ADDR_MATCH, &rdmask);
	debugf0("Inject addr match write 0x%016llx, read: 0x%016llx\n",
		mask, rdmask);
#else
	u32 rdmask1, rdmask2;

	pci_read_config_dword(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ADDR_MATCH, &rdmask1);
	pci_read_config_dword(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ADDR_MATCH + 4, &rdmask2);

	debugf0("Inject addr match write 0x%016llx, read: 0x%08x%08x\n",
		mask, rdmask1, rdmask2);
#endif
#endif

	pci_write_config_dword(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ERROR_MASK, pvt->inject.eccmask);

	/*
	 * bit    0: REPEAT_EN
	 * bits 1-2: MASK_HALF_CACHELINE
	 * bit    3: INJECT_ECC
	 * bit    4: INJECT_ADDR_PARITY
	 */

	injectmask = (pvt->inject.type & 1) |
		     (pvt->inject.section & 0x3) << 1 |
		     (pvt->inject.type & 0x6) << (3 - 1);

	pci_write_config_dword(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ERROR_MASK, injectmask);

	debugf0("Error inject addr match 0x%016llx, ecc 0x%08x, inject 0x%08x\n",
		mask, pvt->inject.eccmask, injectmask);



	return count;
}

static ssize_t i7core_inject_enable_show(struct mem_ctl_info *mci,
					char *data)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	u32 injectmask;

	pci_read_config_dword(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ERROR_MASK, &injectmask);

	debugf0("Inject error read: 0x%018x\n", injectmask);

	if (injectmask & 0x0c)
		pvt->inject.enable = 1;

	return sprintf(data, "%d\n", pvt->inject.enable);
}

static ssize_t i7core_ce_regs_show(struct mem_ctl_info *mci, char *data)
{
	struct i7core_pvt *pvt = mci->pvt_info;

	if (!pvt->ce_count_available)
		return sprintf(data, "unavailable\n");

	return sprintf(data, "dimm0: %lu\ndimm1: %lu\ndimm2: %lu\n",
			pvt->ce_count[0],
			pvt->ce_count[1],
			pvt->ce_count[2]);
}

/*
 * Sysfs struct
 */
static struct mcidev_sysfs_attribute i7core_inj_attrs[] = {

	{
		.attr = {
			.name = "inject_section",
			.mode = (S_IRUGO | S_IWUSR)
		},
		.show  = i7core_inject_section_show,
		.store = i7core_inject_section_store,
	}, {
		.attr = {
			.name = "inject_type",
			.mode = (S_IRUGO | S_IWUSR)
		},
		.show  = i7core_inject_type_show,
		.store = i7core_inject_type_store,
	}, {
		.attr = {
			.name = "inject_eccmask",
			.mode = (S_IRUGO | S_IWUSR)
		},
		.show  = i7core_inject_eccmask_show,
		.store = i7core_inject_eccmask_store,
	}, {
		.attr = {
			.name = "inject_addrmatch",
			.mode = (S_IRUGO | S_IWUSR)
		},
		.show  = i7core_inject_addrmatch_show,
		.store = i7core_inject_addrmatch_store,
	}, {
		.attr = {
			.name = "inject_enable",
			.mode = (S_IRUGO | S_IWUSR)
		},
		.show  = i7core_inject_enable_show,
		.store = i7core_inject_enable_store,
	}, {
		.attr = {
			.name = "corrected_error_counts",
			.mode = (S_IRUGO | S_IWUSR)
		},
		.show  = i7core_ce_regs_show,
		.store = NULL,
	},
};

/****************************************************************************
	Device initialization routines: put/get, init/exit
 ****************************************************************************/

/*
 *	i7core_put_devices	'put' all the devices that we have
 *				reserved via 'get'
 */
static void i7core_put_devices(void)
{
	int i;

	for (i = 0; i < N_DEVS; i++)
		pci_dev_put(pci_devs[i].pdev);
}

/*
 *	i7core_get_devices	Find and perform 'get' operation on the MCH's
 *			device/functions we want to reference for this driver
 *
 *			Need to 'get' device 16 func 1 and func 2
 */
static int i7core_get_devices(void)
{
	int rc, i;
	struct pci_dev *pdev = NULL;

	for (i = 0; i < N_DEVS; i++) {
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
					pci_devs[i].dev_id, NULL);
		if (likely(pdev))
			pci_devs[i].pdev = pdev;
		else {
			i7core_printk(KERN_ERR,
				"Device not found: PCI ID %04x:%04x "
				"(dev %d, func %d)\n",
				PCI_VENDOR_ID_INTEL, pci_devs[i].dev_id,
				pci_devs[i].dev,pci_devs[i].func);

			/* Dev 3 function 2 only exists on chips with RDIMMs */
			if ((pci_devs[i].dev == 3) && (pci_devs[i].func == 2))
				continue;

			/* End of list, leave */
			rc = -ENODEV;
			goto error;
		}

		/* Sanity check */
		if (unlikely(PCI_SLOT(pdev->devfn) != pci_devs[i].dev ||
			     PCI_FUNC(pdev->devfn) != pci_devs[i].func)) {
			i7core_printk(KERN_ERR,
				"Device PCI ID %04x:%04x "
				"has fn %d.%d instead of fn %d.%d\n",
				PCI_VENDOR_ID_INTEL, pci_devs[i].dev_id,
				PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
				pci_devs[i].dev, pci_devs[i].func);
			rc = -EINVAL;
			goto error;
		}

		/* Be sure that the device is enabled */
		rc = pci_enable_device(pdev);
		if (unlikely(rc < 0)) {
			i7core_printk(KERN_ERR,
				"Couldn't enable PCI ID %04x:%04x "
				"fn %d.%d\n",
				PCI_VENDOR_ID_INTEL, pci_devs[i].dev_id,
				PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
			goto error;
		}

		i7core_printk(KERN_INFO,
				"Registered device %0x:%0x fn %d.%d\n",
				PCI_VENDOR_ID_INTEL, pci_devs[i].dev_id,
				PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
	}

	return 0;

error:
	i7core_put_devices();
	return -EINVAL;
}

static int mci_bind_devs(struct mem_ctl_info *mci)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	struct pci_dev *pdev;
	int i, func, slot;

	for (i = 0; i < N_DEVS; i++) {
		pdev = pci_devs[i].pdev;
		if (!pdev)
			continue;

		func = PCI_FUNC(pdev->devfn);
		slot = PCI_SLOT(pdev->devfn);
		if (slot == 3) {
			if (unlikely(func > MAX_MCR_FUNC))
				goto error;
			pvt->pci_mcr[func] = pdev;
		} else if (likely(slot >= 4 && slot < 4 + NUM_CHANS)) {
			if (unlikely(func > MAX_CHAN_FUNC))
				goto error;
			pvt->pci_ch[slot - 4][func] = pdev;
		} else
			goto error;

		debugf0("Associated fn %d.%d, dev = %p\n",
			PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn), pdev);
	}
	return 0;

error:
	i7core_printk(KERN_ERR, "Device %d, function %d "
		      "is out of the expected range\n",
		      slot, func);
	return -EINVAL;
}

/****************************************************************************
			Error check routines
 ****************************************************************************/

/* This function is based on the device 3 function 4 registers as described on:
 * Intel Xeon Processor 5500 Series Datasheet Volume 2
 *	http://www.intel.com/Assets/PDF/datasheet/321322.pdf
 * also available at:
 * 	http://www.arrownac.com/manufacturers/intel/s/nehalem/5500-datasheet-v2.pdf
 */
static void check_mc_test_err(struct mem_ctl_info *mci)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	u32 rcv1, rcv0;
	int new0, new1, new2;

	if (!pvt->pci_mcr[4]) {
		debugf0("%s MCR registers not found\n",__func__);
		return;
	}

	/* Corrected error reads */
	pci_read_config_dword(pvt->pci_mcr[4], MC_TEST_ERR_RCV1, &rcv1);
	pci_read_config_dword(pvt->pci_mcr[4], MC_TEST_ERR_RCV0, &rcv0);

	/* Store the new values */
	new2 = DIMM2_COR_ERR(rcv1);
	new1 = DIMM1_COR_ERR(rcv0);
	new0 = DIMM0_COR_ERR(rcv0);

	debugf2("%s CE rcv1=0x%08x rcv0=0x%08x, %d %d %d\n",
		(pvt->ce_count_available ? "UPDATE" : "READ"),
		rcv1, rcv0, new0, new1, new2);

	/* Updates CE counters if it is not the first time here */
	if (pvt->ce_count_available) {
		/* Updates CE counters */
		int add0, add1, add2;

		add2 = new2 - pvt->last_ce_count[2];
		add1 = new1 - pvt->last_ce_count[1];
		add0 = new0 - pvt->last_ce_count[0];

		if (add2 < 0)
			add2 += 0x7fff;
		pvt->ce_count[2] += add2;

		if (add1 < 0)
			add1 += 0x7fff;
		pvt->ce_count[1] += add1;

		if (add0 < 0)
			add0 += 0x7fff;
		pvt->ce_count[0] += add0;
	} else
		pvt->ce_count_available = 1;

	/* Store the new values */
	pvt->last_ce_count[2] = new2;
	pvt->last_ce_count[1] = new1;
	pvt->last_ce_count[0] = new0;
}

/*
 *	i7core_check_error	Retrieve and process errors reported by the
 *				hardware. Called by the Core module.
 */
static void i7core_check_error(struct mem_ctl_info *mci)
{
	check_mc_test_err(mci);
}

/*
 *	i7core_probe	Probe for ONE instance of device to see if it is
 *			present.
 *	return:
 *		0 for FOUND a device
 *		< 0 for error code
 */
static int __devinit i7core_probe(struct pci_dev *pdev,
				  const struct pci_device_id *id)
{
	struct mem_ctl_info *mci;
	struct i7core_pvt *pvt;
	int num_channels = 0;
	int num_csrows;
	int dev_idx = id->driver_data;

	if (unlikely(dev_idx >= ARRAY_SIZE(i7core_devs)))
		return -EINVAL;

	/* get the pci devices we want to reserve for our use */
	if (unlikely(i7core_get_devices() < 0))
		return -ENODEV;

	/* Check the number of active and not disabled channels */
	if (unlikely (i7core_get_active_channels(&num_channels)) < 0)
		goto fail0;

	/* FIXME: we currently don't know the number of csrows */
	num_csrows = num_channels;

	/* allocate a new MC control structure */
	mci = edac_mc_alloc(sizeof(*pvt), num_csrows, num_channels, 0);
	if (unlikely (!mci))
		return -ENOMEM;

	debugf0("MC: " __FILE__ ": %s(): mci = %p\n", __func__, mci);

	mci->dev = &pdev->dev;	/* record ptr to the generic device */

	pvt = mci->pvt_info;
	memset(pvt, 0, sizeof(*pvt));

	mci->mc_idx = 0;
	mci->mtype_cap = MEM_FLAG_DDR3;		/* FIXME: how to handle RDDR3? */
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = "i7core_edac.c";
	mci->mod_ver = I7CORE_REVISION;
	mci->ctl_name = i7core_devs[dev_idx].ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->ctl_page_to_phys = NULL;
	mci->mc_driver_sysfs_attributes = i7core_inj_attrs;
	/* Set the function pointer to an actual operation function */
	mci->edac_check = i7core_check_error;

	/* Store pci devices at mci for faster access */
	if (unlikely (mci_bind_devs(mci)) < 0)
		goto fail1;

	/* Get dimm basic config */
	get_dimm_config(mci);

	/* add this new MC control structure to EDAC's list of MCs */
	if (unlikely(edac_mc_add_mc(mci)) < 0) {
		debugf0("MC: " __FILE__
			": %s(): failed edac_mc_add_mc()\n", __func__);
		/* FIXME: perhaps some code should go here that disables error
		 * reporting if we just enabled it
		 */
		goto fail1;
	}

	/* allocating generic PCI control info */
	i7core_pci = edac_pci_create_generic_ctl(&pdev->dev, EDAC_MOD_STR);
	if (unlikely (!i7core_pci)) {
		printk(KERN_WARNING
			"%s(): Unable to create PCI control\n",
			__func__);
		printk(KERN_WARNING
			"%s(): PCI error report via EDAC not setup\n",
			__func__);
	}

	/* Default error mask is any memory */
	pvt->inject.channel = 0;
	pvt->inject.dimm = -1;
	pvt->inject.rank = -1;
	pvt->inject.bank = -1;
	pvt->inject.page = -1;
	pvt->inject.col = -1;

	i7core_printk(KERN_INFO, "Driver loaded.\n");

	return 0;

fail1:
	i7core_put_devices();

fail0:
	edac_mc_free(mci);
	return -ENODEV;
}

/*
 *	i7core_remove	destructor for one instance of device
 *
 */
static void __devexit i7core_remove(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;

	debugf0(__FILE__ ": %s()\n", __func__);

	if (i7core_pci)
		edac_pci_release_generic_ctl(i7core_pci);

	mci = edac_mc_del_mc(&pdev->dev);

	if (!mci)
		return;

	/* retrieve references to resources, and free those resources */
	i7core_put_devices();

	edac_mc_free(mci);
}

MODULE_DEVICE_TABLE(pci, i7core_pci_tbl);

/*
 *	i7core_driver	pci_driver structure for this module
 *
 */
static struct pci_driver i7core_driver = {
	.name     = "i7core_edac",
	.probe    = i7core_probe,
	.remove   = __devexit_p(i7core_remove),
	.id_table = i7core_pci_tbl,
};

/*
 *	i7core_init		Module entry function
 *			Try to initialize this module for its devices
 */
static int __init i7core_init(void)
{
	int pci_rc;

	debugf2("MC: " __FILE__ ": %s()\n", __func__);

	/* Ensure that the OPSTATE is set correctly for POLL or NMI */
	opstate_init();

	pci_rc = pci_register_driver(&i7core_driver);

	return (pci_rc < 0) ? pci_rc : 0;
}

/*
 *	i7core_exit()	Module exit function
 *			Unregister the driver
 */
static void __exit i7core_exit(void)
{
	debugf2("MC: " __FILE__ ": %s()\n", __func__);
	pci_unregister_driver(&i7core_driver);
}

module_init(i7core_init);
module_exit(i7core_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_AUTHOR("Red Hat Inc. (http://www.redhat.com)");
MODULE_DESCRIPTION("MC Driver for Intel i7 Core memory controllers - "
		   I7CORE_REVISION);

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");
