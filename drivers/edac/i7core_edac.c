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
#include <linux/edac_mce.h>
#include <linux/spinlock.h>

#include "edac_core.h"

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

	/* OFFSETS for Device 0 Function 0 */

#define MC_CFG_CONTROL	0x90

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
  #define MC_DOD_NUMBANK_MASK		((1 << 8) | (1 << 7))
  #define MC_DOD_NUMBANK(x)		(((x) & MC_DOD_NUMBANK_MASK) >> 7)
  #define MC_DOD_NUMRANK_MASK		((1 << 6) | (1 << 5))
  #define MC_DOD_NUMRANK(x)		(((x) & MC_DOD_NUMRANK_MASK) >> 5)
  #define MC_DOD_NUMROW_MASK		((1 << 4) | (1 << 3) | (1 << 2))
  #define MC_DOD_NUMROW(x)		(((x) & MC_DOD_NUMROW_MASK) >> 2)
  #define MC_DOD_NUMCOL_MASK		3
  #define MC_DOD_NUMCOL(x)		((x) & MC_DOD_NUMCOL_MASK)

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
#define NUM_SOCKETS 2		/* Max number of MC sockets */
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

	u8	socket;
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
	struct pci_dev	*pdev[NUM_SOCKETS];
};

struct i7core_pvt {
	struct pci_dev	*pci_noncore[NUM_SOCKETS];
	struct pci_dev	*pci_mcr[NUM_SOCKETS][MAX_MCR_FUNC + 1];
	struct pci_dev	*pci_ch[NUM_SOCKETS][NUM_CHANS][MAX_CHAN_FUNC + 1];

	struct i7core_info	info;
	struct i7core_inject	inject;
	struct i7core_channel	channel[NUM_SOCKETS][NUM_CHANS];

	int			sockets; /* Number of sockets */
	int			channels; /* Number of active channels */

	int		ce_count_available[NUM_SOCKETS];
			/* ECC corrected errors counts per dimm */
	unsigned long	ce_count[NUM_SOCKETS][MAX_DIMMS];
	int		last_ce_count[NUM_SOCKETS][MAX_DIMMS];

	/* mcelog glue */
	struct edac_mce		edac_mce;
	struct mce		mce_entry[MCE_LOG_LEN];
	unsigned		mce_count;
	spinlock_t		mce_lock;
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

		/* Generic Non-core registers */
	/*
	 * This is the PCI device on i7core and on Xeon 35xx (8086:2c41)
	 * On Xeon 55xx, however, it has a different id (8086:2c40). So,
	 * the probing code needs to test for the other address in case of
	 * failure of this one
	 */
	{ PCI_DESCR(0, 0, PCI_DEVICE_ID_INTEL_I7_NOCORE)  },

};
#define N_DEVS ARRAY_SIZE(pci_devs)

/*
 *	pci_device_id	table for which devices we are looking for
 * This should match the first device at pci_devs table
 */
static const struct pci_device_id i7core_pci_tbl[] __devinitdata = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_X58_HUB_MGMT)},
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
static inline int numdimms(u32 dimms)
{
	return (dimms & 0x3) + 1;
}

static inline int numrank(u32 rank)
{
	static int ranks[4] = { 1, 2, 4, -EINVAL };

	return ranks[rank & 0x3];
}

static inline int numbank(u32 bank)
{
	static int banks[4] = { 4, 8, 16, -EINVAL };

	return banks[bank & 0x3];
}

static inline int numrow(u32 row)
{
	static int rows[8] = {
		1 << 12, 1 << 13, 1 << 14, 1 << 15,
		1 << 16, -EINVAL, -EINVAL, -EINVAL,
	};

	return rows[row & 0x7];
}

static inline int numcol(u32 col)
{
	static int cols[8] = {
		1 << 10, 1 << 11, 1 << 12, -EINVAL,
	};
	return cols[col & 0x3];
}

/****************************************************************************
			Memory check routines
 ****************************************************************************/
static struct pci_dev *get_pdev_slot_func(u8 socket, unsigned slot,
					  unsigned func)
{
	int i;

	for (i = 0; i < N_DEVS; i++) {
		if (!pci_devs[i].pdev[socket])
			continue;

		if (PCI_SLOT(pci_devs[i].pdev[socket]->devfn) == slot &&
		    PCI_FUNC(pci_devs[i].pdev[socket]->devfn) == func) {
			return pci_devs[i].pdev[socket];
		}
	}

	return NULL;
}

/**
 * i7core_get_active_channels() - gets the number of channels and csrows
 * @socket:	Quick Path Interconnect socket
 * @channels:	Number of channels that will be returned
 * @csrows:	Number of csrows found
 *
 * Since EDAC core needs to know in advance the number of available channels
 * and csrows, in order to allocate memory for csrows/channels, it is needed
 * to run two similar steps. At the first step, implemented on this function,
 * it checks the number of csrows/channels present at one socket.
 * this is used in order to properly allocate the size of mci components.
 *
 * It should be noticed that none of the current available datasheets explain
 * or even mention how csrows are seen by the memory controller. So, we need
 * to add a fake description for csrows.
 * So, this driver is attributing one DIMM memory for one csrow.
 */
static int i7core_get_active_channels(u8 socket, unsigned *channels,
				      unsigned *csrows)
{
	struct pci_dev *pdev = NULL;
	int i, j;
	u32 status, control;

	*channels = 0;
	*csrows = 0;

	pdev = get_pdev_slot_func(socket, 3, 0);
	if (!pdev) {
		i7core_printk(KERN_ERR, "Couldn't find socket %d fn 3.0!!!\n",
			      socket);
		return -ENODEV;
	}

	/* Device 3 function 0 reads */
	pci_read_config_dword(pdev, MC_STATUS, &status);
	pci_read_config_dword(pdev, MC_CONTROL, &control);

	for (i = 0; i < NUM_CHANS; i++) {
		u32 dimm_dod[3];
		/* Check if the channel is active */
		if (!(control & (1 << (8 + i))))
			continue;

		/* Check if the channel is disabled */
		if (status & (1 << i))
			continue;

		pdev = get_pdev_slot_func(socket, i + 4, 1);
		if (!pdev) {
			i7core_printk(KERN_ERR, "Couldn't find socket %d "
						"fn %d.%d!!!\n",
						socket, i + 4, 1);
			return -ENODEV;
		}
		/* Devices 4-6 function 1 */
		pci_read_config_dword(pdev,
				MC_DOD_CH_DIMM0, &dimm_dod[0]);
		pci_read_config_dword(pdev,
				MC_DOD_CH_DIMM1, &dimm_dod[1]);
		pci_read_config_dword(pdev,
				MC_DOD_CH_DIMM2, &dimm_dod[2]);

		(*channels)++;

		for (j = 0; j < 3; j++) {
			if (!DIMM_PRESENT(dimm_dod[j]))
				continue;
			(*csrows)++;
		}
	}

	debugf0("Number of active channels on socket %d: %d\n",
		socket, *channels);

	return 0;
}

static int get_dimm_config(struct mem_ctl_info *mci, int *csrow, u8 socket)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	struct csrow_info *csr;
	struct pci_dev *pdev;
	int i, j;
	unsigned long last_page = 0;
	enum edac_type mode;
	enum mem_type mtype;

	/* Get data from the MC register, function 0 */
	pdev = pvt->pci_mcr[socket][0];
	if (!pdev)
		return -ENODEV;

	/* Device 3 function 0 reads */
	pci_read_config_dword(pdev, MC_CONTROL, &pvt->info.mc_control);
	pci_read_config_dword(pdev, MC_STATUS, &pvt->info.mc_status);
	pci_read_config_dword(pdev, MC_MAX_DOD, &pvt->info.max_dod);
	pci_read_config_dword(pdev, MC_CHANNEL_MAPPER, &pvt->info.ch_map);

	debugf0("QPI %d control=0x%08x status=0x%08x dod=0x%08x map=0x%08x\n",
		socket, pvt->info.mc_control, pvt->info.mc_status,
		pvt->info.max_dod, pvt->info.ch_map);

	if (ECC_ENABLED(pvt)) {
		debugf0("ECC enabled with x%d SDCC\n", ECCx8(pvt) ? 8 : 4);
		if (ECCx8(pvt))
			mode = EDAC_S8ECD8ED;
		else
			mode = EDAC_S4ECD4ED;
	} else {
		debugf0("ECC disabled\n");
		mode = EDAC_NONE;
	}

	/* FIXME: need to handle the error codes */
	debugf0("DOD Max limits: DIMMS: %d, %d-ranked, %d-banked "
		"x%x x 0x%x\n",
		numdimms(pvt->info.max_dod),
		numrank(pvt->info.max_dod >> 2),
		numbank(pvt->info.max_dod >> 4),
		numrow(pvt->info.max_dod >> 6),
		numcol(pvt->info.max_dod >> 9));

	for (i = 0; i < NUM_CHANS; i++) {
		u32 data, dimm_dod[3], value[8];

		if (!CH_ACTIVE(pvt, i)) {
			debugf0("Channel %i is not active\n", i);
			continue;
		}
		if (CH_DISABLED(pvt, i)) {
			debugf0("Channel %i is disabled\n", i);
			continue;
		}

		/* Devices 4-6 function 0 */
		pci_read_config_dword(pvt->pci_ch[socket][i][0],
				MC_CHANNEL_DIMM_INIT_PARAMS, &data);

		pvt->channel[socket][i].ranks = (data & QUAD_RANK_PRESENT) ?
						4 : 2;

		if (data & REGISTERED_DIMM)
			mtype = MEM_RDDR3;
		else
			mtype = MEM_DDR3;
#if 0
		if (data & THREE_DIMMS_PRESENT)
			pvt->channel[i].dimms = 3;
		else if (data & SINGLE_QUAD_RANK_PRESENT)
			pvt->channel[i].dimms = 1;
		else
			pvt->channel[i].dimms = 2;
#endif

		/* Devices 4-6 function 1 */
		pci_read_config_dword(pvt->pci_ch[socket][i][1],
				MC_DOD_CH_DIMM0, &dimm_dod[0]);
		pci_read_config_dword(pvt->pci_ch[socket][i][1],
				MC_DOD_CH_DIMM1, &dimm_dod[1]);
		pci_read_config_dword(pvt->pci_ch[socket][i][1],
				MC_DOD_CH_DIMM2, &dimm_dod[2]);

		debugf0("Ch%d phy rd%d, wr%d (0x%08x): "
			"%d ranks, %cDIMMs\n",
			i,
			RDLCH(pvt->info.ch_map, i), WRLCH(pvt->info.ch_map, i),
			data,
			pvt->channel[socket][i].ranks,
			(data & REGISTERED_DIMM) ? 'R' : 'U');

		for (j = 0; j < 3; j++) {
			u32 banks, ranks, rows, cols;
			u32 size, npages;

			if (!DIMM_PRESENT(dimm_dod[j]))
				continue;

			banks = numbank(MC_DOD_NUMBANK(dimm_dod[j]));
			ranks = numrank(MC_DOD_NUMRANK(dimm_dod[j]));
			rows = numrow(MC_DOD_NUMROW(dimm_dod[j]));
			cols = numcol(MC_DOD_NUMCOL(dimm_dod[j]));

			/* DDR3 has 8 I/O banks */
			size = (rows * cols * banks * ranks) >> (20 - 3);

			pvt->channel[socket][i].dimms++;

			debugf0("\tdimm %d %d Mb offset: %x, "
				"bank: %d, rank: %d, row: %#x, col: %#x\n",
				j, size,
				RANKOFFSET(dimm_dod[j]),
				banks, ranks, rows, cols);

#if PAGE_SHIFT > 20
			npages = size >> (PAGE_SHIFT - 20);
#else
			npages = size << (20 - PAGE_SHIFT);
#endif

			csr = &mci->csrows[*csrow];
			csr->first_page = last_page + 1;
			last_page += npages;
			csr->last_page = last_page;
			csr->nr_pages = npages;

			csr->page_mask = 0;
			csr->grain = 8;
			csr->csrow_idx = *csrow;
			csr->nr_channels = 1;

			csr->channels[0].chan_idx = i;
			csr->channels[0].ce_count = 0;

			switch (banks) {
			case 4:
				csr->dtype = DEV_X4;
				break;
			case 8:
				csr->dtype = DEV_X8;
				break;
			case 16:
				csr->dtype = DEV_X16;
				break;
			default:
				csr->dtype = DEV_UNKNOWN;
			}

			csr->edac_mode = mode;
			csr->mtype = mtype;

			(*csrow)++;
		}

		pci_read_config_dword(pdev, MC_SAG_CH_0, &value[0]);
		pci_read_config_dword(pdev, MC_SAG_CH_1, &value[1]);
		pci_read_config_dword(pdev, MC_SAG_CH_2, &value[2]);
		pci_read_config_dword(pdev, MC_SAG_CH_3, &value[3]);
		pci_read_config_dword(pdev, MC_SAG_CH_4, &value[4]);
		pci_read_config_dword(pdev, MC_SAG_CH_5, &value[5]);
		pci_read_config_dword(pdev, MC_SAG_CH_6, &value[6]);
		pci_read_config_dword(pdev, MC_SAG_CH_7, &value[7]);
		debugf1("\t[%i] DIVBY3\tREMOVED\tOFFSET\n", i);
		for (j = 0; j < 8; j++)
			debugf1("\t\t%#x\t%#x\t%#x\n",
				(value[j] >> 27) & 0x1,
				(value[j] >> 24) & 0x7,
				(value[j] && ((1 << 24) - 1)));
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

	if (!pvt->pci_ch[pvt->inject.socket][pvt->inject.channel][0])
		return -ENODEV;

	pci_write_config_dword(pvt->pci_ch[pvt->inject.socket][pvt->inject.channel][0],
				MC_CHANNEL_ERROR_INJECT, 0);

	return 0;
}

/*
 * i7core inject inject.socket
 *
 *	accept and store error injection inject.socket value
 */
static ssize_t i7core_inject_socket_store(struct mem_ctl_info *mci,
					   const char *data, size_t count)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	unsigned long value;
	int rc;

	rc = strict_strtoul(data, 10, &value);
	if ((rc < 0) || (value >= pvt->sockets))
		return -EIO;

	pvt->inject.socket = (u32) value;
	return count;
}

static ssize_t i7core_inject_socket_show(struct mem_ctl_info *mci,
					      char *data)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	return sprintf(data, "%d\n", pvt->inject.socket);
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
		return -EIO;

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
		return -EIO;

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
		return -EIO;

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

		if (!strcasecmp(val, "any"))
			value = -1;
		else {
			rc = strict_strtol(val, 10, &value);
			if ((rc < 0) || (value < 0))
				return cmd - data;
		}

		if (!strcasecmp(cmd, "channel")) {
			if (value < 3)
				pvt->inject.channel = value;
			else
				return cmd - data;
		} else if (!strcasecmp(cmd, "dimm")) {
			if (value < 3)
				pvt->inject.dimm = value;
			else
				return cmd - data;
		} else if (!strcasecmp(cmd, "rank")) {
			if (value < 4)
				pvt->inject.rank = value;
			else
				return cmd - data;
		} else if (!strcasecmp(cmd, "bank")) {
			if (value < 32)
				pvt->inject.bank = value;
			else
				return cmd - data;
		} else if (!strcasecmp(cmd, "page")) {
			if (value <= 0xffff)
				pvt->inject.page = value;
			else
				return cmd - data;
		} else if (!strcasecmp(cmd, "col") ||
			   !strcasecmp(cmd, "column")) {
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

static int write_and_test(struct pci_dev *dev, int where, u32 val)
{
	u32 read;
	int count;

	debugf0("setting pci %02x:%02x.%x reg=%02x value=%08x\n",
		dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
		where, val);

	for (count = 0; count < 10; count++) {
		if (count)
			msleep (100);
		pci_write_config_dword(dev, where, val);
		pci_read_config_dword(dev, where, &read);

		if (read == val)
			return 0;
	}

	i7core_printk(KERN_ERR, "Error during set pci %02x:%02x.%x reg=%02x "
		"write=%08x. Read=%08x\n",
		dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
		where, val, read);

	return -EINVAL;
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

	if (!pvt->pci_ch[pvt->inject.socket][pvt->inject.channel][0])
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
		if (pvt->channel[pvt->inject.socket][pvt->inject.channel].dimms > 2)
			mask |= (pvt->inject.dimm & 0x3L) << 35;
		else
			mask |= (pvt->inject.dimm & 0x1L) << 36;
	}

	/* Sets pvt->inject.rank mask */
	if (pvt->inject.rank < 0)
		mask |= 1L << 40;
	else {
		if (pvt->channel[pvt->inject.socket][pvt->inject.channel].dimms > 2)
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

	/*
	 * bit    0: REPEAT_EN
	 * bits 1-2: MASK_HALF_CACHELINE
	 * bit    3: INJECT_ECC
	 * bit    4: INJECT_ADDR_PARITY
	 */

	injectmask = (pvt->inject.type & 1) |
		     (pvt->inject.section & 0x3) << 1 |
		     (pvt->inject.type & 0x6) << (3 - 1);

	/* Unlock writes to registers - this register is write only */
	pci_write_config_dword(pvt->pci_noncore[pvt->inject.socket],
			       MC_CFG_CONTROL, 0x2);

	write_and_test(pvt->pci_ch[pvt->inject.socket][pvt->inject.channel][0],
			       MC_CHANNEL_ADDR_MATCH, mask);
	write_and_test(pvt->pci_ch[pvt->inject.socket][pvt->inject.channel][0],
			       MC_CHANNEL_ADDR_MATCH + 4, mask >> 32L);

	write_and_test(pvt->pci_ch[pvt->inject.socket][pvt->inject.channel][0],
			       MC_CHANNEL_ERROR_MASK, pvt->inject.eccmask);

	write_and_test(pvt->pci_ch[pvt->inject.socket][pvt->inject.channel][0],
			       MC_CHANNEL_ERROR_INJECT, injectmask);

	/*
	 * This is something undocumented, based on my tests
	 * Without writing 8 to this register, errors aren't injected. Not sure
	 * why.
	 */
	pci_write_config_dword(pvt->pci_noncore[pvt->inject.socket],
			       MC_CFG_CONTROL, 8);

	debugf0("Error inject addr match 0x%016llx, ecc 0x%08x,"
		" inject 0x%08x\n",
		mask, pvt->inject.eccmask, injectmask);


	return count;
}

static ssize_t i7core_inject_enable_show(struct mem_ctl_info *mci,
					char *data)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	u32 injectmask;

	pci_read_config_dword(pvt->pci_ch[pvt->inject.socket][pvt->inject.channel][0],
			       MC_CHANNEL_ERROR_INJECT, &injectmask);

	debugf0("Inject error read: 0x%018x\n", injectmask);

	if (injectmask & 0x0c)
		pvt->inject.enable = 1;

	return sprintf(data, "%d\n", pvt->inject.enable);
}

static ssize_t i7core_ce_regs_show(struct mem_ctl_info *mci, char *data)
{
	unsigned i, count, total = 0;
	struct i7core_pvt *pvt = mci->pvt_info;

	for (i = 0; i < pvt->sockets; i++) {
		if (!pvt->ce_count_available[i])
			count = sprintf(data, "socket 0 data unavailable\n");
		else
			count = sprintf(data, "socket %d, dimm0: %lu\n"
					      "dimm1: %lu\ndimm2: %lu\n",
					i,
					pvt->ce_count[i][0],
					pvt->ce_count[i][1],
					pvt->ce_count[i][2]);
		data  += count;
		total += count;
	}

	return total;
}

/*
 * Sysfs struct
 */
static struct mcidev_sysfs_attribute i7core_inj_attrs[] = {
	{
		.attr = {
			.name = "inject_socket",
			.mode = (S_IRUGO | S_IWUSR)
		},
		.show  = i7core_inject_socket_show,
		.store = i7core_inject_socket_store,
	}, {
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
	int i, j;

	for (i = 0; i < NUM_SOCKETS; i++)
		for (j = 0; j < N_DEVS; j++)
			pci_dev_put(pci_devs[j].pdev[i]);
}

/*
 *	i7core_get_devices	Find and perform 'get' operation on the MCH's
 *			device/functions we want to reference for this driver
 *
 *			Need to 'get' device 16 func 1 and func 2
 */
int i7core_get_onedevice(struct pci_dev **prev, int devno)
{
	struct pci_dev *pdev = NULL;
	u8 bus = 0;
	u8 socket = 0;

	pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
			      pci_devs[devno].dev_id, *prev);

	/*
	 * On Xeon 55xx, the Intel Quckpath Arch Generic Non-core pci buses
	 * aren't announced by acpi. So, we need to use a legacy scan probing
	 * to detect them
	 */
	if (unlikely(!pdev && !devno && !prev)) {
		pcibios_scan_specific_bus(254);
		pcibios_scan_specific_bus(255);

		pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
				      pci_devs[devno].dev_id, *prev);
	}

	/*
	 * On Xeon 55xx, the Intel Quckpath Arch Generic Non-core regs
	 * is at addr 8086:2c40, instead of 8086:2c41. So, we need
	 * to probe for the alternate address in case of failure
	 */
	if (pci_devs[devno].dev_id == PCI_DEVICE_ID_INTEL_I7_NOCORE && !pdev)
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
				      PCI_DEVICE_ID_INTEL_I7_NOCORE_ALT, *prev);

	if (!pdev) {
		if (*prev) {
			*prev = pdev;
			return 0;
		}

		/*
		 * Dev 3 function 2 only exists on chips with RDIMMs
		 * so, it is ok to not found it
		 */
		if ((pci_devs[devno].dev == 3) && (pci_devs[devno].func == 2)) {
			*prev = pdev;
			return 0;
		}

		i7core_printk(KERN_ERR,
			"Device not found: dev %02x.%d PCI ID %04x:%04x\n",
			pci_devs[devno].dev, pci_devs[devno].func,
			PCI_VENDOR_ID_INTEL, pci_devs[devno].dev_id);

		/* End of list, leave */
		return -ENODEV;
	}
	bus = pdev->bus->number;

	if (bus == 0x3f)
		socket = 0;
	else
		socket = 255 - bus;

	if (socket >= NUM_SOCKETS) {
		i7core_printk(KERN_ERR,
			"Unexpected socket for "
			"dev %02x:%02x.%d PCI ID %04x:%04x\n",
			bus, pci_devs[devno].dev, pci_devs[devno].func,
			PCI_VENDOR_ID_INTEL, pci_devs[devno].dev_id);
		pci_dev_put(pdev);
		return -ENODEV;
	}

	if (pci_devs[devno].pdev[socket]) {
		i7core_printk(KERN_ERR,
			"Duplicated device for "
			"dev %02x:%02x.%d PCI ID %04x:%04x\n",
			bus, pci_devs[devno].dev, pci_devs[devno].func,
			PCI_VENDOR_ID_INTEL, pci_devs[devno].dev_id);
		pci_dev_put(pdev);
		return -ENODEV;
	}

	pci_devs[devno].pdev[socket] = pdev;

	/* Sanity check */
	if (unlikely(PCI_SLOT(pdev->devfn) != pci_devs[devno].dev ||
			PCI_FUNC(pdev->devfn) != pci_devs[devno].func)) {
		i7core_printk(KERN_ERR,
			"Device PCI ID %04x:%04x "
			"has dev %02x:%02x.%d instead of dev %02x:%02x.%d\n",
			PCI_VENDOR_ID_INTEL, pci_devs[devno].dev_id,
			bus, PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
			bus, pci_devs[devno].dev, pci_devs[devno].func);
		return -ENODEV;
	}

	/* Be sure that the device is enabled */
	if (unlikely(pci_enable_device(pdev) < 0)) {
		i7core_printk(KERN_ERR,
			"Couldn't enable "
			"dev %02x:%02x.%d PCI ID %04x:%04x\n",
			bus, pci_devs[devno].dev, pci_devs[devno].func,
			PCI_VENDOR_ID_INTEL, pci_devs[devno].dev_id);
		return -ENODEV;
	}

	i7core_printk(KERN_INFO,
			"Registered socket %d "
			"dev %02x:%02x.%d PCI ID %04x:%04x\n",
			socket, bus, pci_devs[devno].dev, pci_devs[devno].func,
			PCI_VENDOR_ID_INTEL, pci_devs[devno].dev_id);

	*prev = pdev;

	return 0;
}

static int i7core_get_devices(void)
{
	int i;
	struct pci_dev *pdev = NULL;

	for (i = 0; i < N_DEVS; i++) {
		pdev = NULL;
		do {
			if (i7core_get_onedevice(&pdev, i) < 0) {
				i7core_put_devices();
				return -ENODEV;
			}
		} while (pdev);
	}
	return 0;
}

static int mci_bind_devs(struct mem_ctl_info *mci)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	struct pci_dev *pdev;
	int i, j, func, slot;

	for (i = 0; i < pvt->sockets; i++) {
		for (j = 0; j < N_DEVS; j++) {
			pdev = pci_devs[j].pdev[i];
			if (!pdev)
				continue;

			func = PCI_FUNC(pdev->devfn);
			slot = PCI_SLOT(pdev->devfn);
			if (slot == 3) {
				if (unlikely(func > MAX_MCR_FUNC))
					goto error;
				pvt->pci_mcr[i][func] = pdev;
			} else if (likely(slot >= 4 && slot < 4 + NUM_CHANS)) {
				if (unlikely(func > MAX_CHAN_FUNC))
					goto error;
				pvt->pci_ch[i][slot - 4][func] = pdev;
			} else if (!slot && !func)
				pvt->pci_noncore[i] = pdev;
			else
				goto error;

			debugf0("Associated fn %d.%d, dev = %p, socket %d\n",
				PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
				pdev, i);
		}
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
static void check_mc_test_err(struct mem_ctl_info *mci, u8 socket)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	u32 rcv1, rcv0;
	int new0, new1, new2;

	if (!pvt->pci_mcr[socket][4]) {
		debugf0("%s MCR registers not found\n",__func__);
		return;
	}

	/* Corrected error reads */
	pci_read_config_dword(pvt->pci_mcr[socket][4], MC_TEST_ERR_RCV1, &rcv1);
	pci_read_config_dword(pvt->pci_mcr[socket][4], MC_TEST_ERR_RCV0, &rcv0);

	/* Store the new values */
	new2 = DIMM2_COR_ERR(rcv1);
	new1 = DIMM1_COR_ERR(rcv0);
	new0 = DIMM0_COR_ERR(rcv0);

#if 0
	debugf2("%s CE rcv1=0x%08x rcv0=0x%08x, %d %d %d\n",
		(pvt->ce_count_available ? "UPDATE" : "READ"),
		rcv1, rcv0, new0, new1, new2);
#endif

	/* Updates CE counters if it is not the first time here */
	if (pvt->ce_count_available[socket]) {
		/* Updates CE counters */
		int add0, add1, add2;

		add2 = new2 - pvt->last_ce_count[socket][2];
		add1 = new1 - pvt->last_ce_count[socket][1];
		add0 = new0 - pvt->last_ce_count[socket][0];

		if (add2 < 0)
			add2 += 0x7fff;
		pvt->ce_count[socket][2] += add2;

		if (add1 < 0)
			add1 += 0x7fff;
		pvt->ce_count[socket][1] += add1;

		if (add0 < 0)
			add0 += 0x7fff;
		pvt->ce_count[socket][0] += add0;
	} else
		pvt->ce_count_available[socket] = 1;

	/* Store the new values */
	pvt->last_ce_count[socket][2] = new2;
	pvt->last_ce_count[socket][1] = new1;
	pvt->last_ce_count[socket][0] = new0;
}

/*
 * According with tables E-11 and E-12 of chapter E.3.3 of Intel 64 and IA-32
 * Architectures Software Developer’s Manual Volume 3B.
 * Nehalem are defined as family 0x06, model 0x1a
 *
 * The MCA registers used here are the following ones:
 *     struct mce field	MCA Register
 *     m->status	MSR_IA32_MC8_STATUS
 *     m->addr		MSR_IA32_MC8_ADDR
 *     m->misc		MSR_IA32_MC8_MISC
 * In the case of Nehalem, the error information is masked at .status and .misc
 * fields
 */
static void i7core_mce_output_error(struct mem_ctl_info *mci,
				    struct mce *m)
{
	char *type, *optype, *err, *msg;
	unsigned long error = m->status & 0x1ff0000l;
	u32 optypenum = (m->status >> 4) & 0x07;
	u32 core_err_cnt = (m->status >> 38) && 0x7fff;
	u32 dimm = (m->misc >> 16) & 0x3;
	u32 channel = (m->misc >> 18) & 0x3;
	u32 syndrome = m->misc >> 32;
	u32 errnum = find_first_bit(&error, 32);

	if (m->mcgstatus & 1)
		type = "FATAL";
	else
		type = "NON_FATAL";

	switch (optypenum) {
		case 0:
			optype = "generic undef request";
			break;
		case 1:
			optype = "read error";
			break;
		case 2:
			optype = "write error";
			break;
		case 3:
			optype = "addr/cmd error";
			break;
		case 4:
			optype = "scrubbing error";
			break;
		default:
			optype = "reserved";
			break;
	}

	switch (errnum) {
	case 16:
		err = "read ECC error";
		break;
	case 17:
		err = "RAS ECC error";
		break;
	case 18:
		err = "write parity error";
		break;
	case 19:
		err = "redundacy loss";
		break;
	case 20:
		err = "reserved";
		break;
	case 21:
		err = "memory range error";
		break;
	case 22:
		err = "RTID out of range";
		break;
	case 23:
		err = "address parity error";
		break;
	case 24:
		err = "byte enable parity error";
		break;
	default:
		err = "unknown";
	}

	/* FIXME: should convert addr into bank and rank information */
	msg = kasprintf(GFP_ATOMIC,
		"%s (addr = 0x%08llx, socket=%d, Dimm=%d, Channel=%d, "
		"syndrome=0x%08x, count=%d, Err=%08llx:%08llx (%s: %s))\n",
		type, (long long) m->addr, m->cpu, dimm, channel,
		syndrome, core_err_cnt, (long long)m->status,
		(long long)m->misc, optype, err);

	debugf0("%s", msg);

	/* Call the helper to output message */
	edac_mc_handle_fbd_ue(mci, 0 /* FIXME: should be rank here */,
			      0, 0 /* FIXME: should be channel here */, msg);

	kfree(msg);
}

/*
 *	i7core_check_error	Retrieve and process errors reported by the
 *				hardware. Called by the Core module.
 */
static void i7core_check_error(struct mem_ctl_info *mci)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	int i;
	unsigned count = 0;
	struct mce *m = NULL;
	unsigned long flags;

	/* Copy all mce errors into a temporary buffer */
	spin_lock_irqsave(&pvt->mce_lock, flags);
	if (pvt->mce_count) {
		m = kmalloc(sizeof(*m) * pvt->mce_count, GFP_ATOMIC);
		if (m) {
			count = pvt->mce_count;
			memcpy(m, &pvt->mce_entry, sizeof(*m) * count);
		}
		pvt->mce_count = 0;
	}
	spin_unlock_irqrestore(&pvt->mce_lock, flags);

	/* proccess mcelog errors */
	for (i = 0; i < count; i++)
		i7core_mce_output_error(mci, &m[i]);

	kfree(m);

	/* check memory count errors */
	for (i = 0; i < pvt->sockets; i++)
		check_mc_test_err(mci, i);
}

/*
 * i7core_mce_check_error	Replicates mcelog routine to get errors
 *				This routine simply queues mcelog errors, and
 *				return. The error itself should be handled later
 *				by i7core_check_error.
 */
static int i7core_mce_check_error(void *priv, struct mce *mce)
{
	struct mem_ctl_info *mci = priv;
	struct i7core_pvt *pvt = mci->pvt_info;
	unsigned long flags;

	/*
	 * Just let mcelog handle it if the error is
	 * outside the memory controller
	 */
	if (((mce->status & 0xffff) >> 7) != 1)
		return 0;

	/* Bank 8 registers are the only ones that we know how to handle */
	if (mce->bank != 8)
		return 0;

	spin_lock_irqsave(&pvt->mce_lock, flags);
	if (pvt->mce_count < MCE_LOG_LEN) {
		memcpy(&pvt->mce_entry[pvt->mce_count], mce, sizeof(*mce));
		pvt->mce_count++;
	}
	spin_unlock_irqrestore(&pvt->mce_lock, flags);

	/* Handle fatal errors immediately */
	if (mce->mcgstatus & 1)
		i7core_check_error(mci);

	/* Advice mcelog that the error were handled */
	return 1;
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
	int num_csrows = 0;
	int csrow = 0;
	int dev_idx = id->driver_data;
	int rc, i;
	u8 sockets;

	if (unlikely(dev_idx >= ARRAY_SIZE(i7core_devs)))
		return -EINVAL;

	/* get the pci devices we want to reserve for our use */
	rc = i7core_get_devices();
	if (unlikely(rc < 0))
		return rc;

	sockets = 1;
	for (i = NUM_SOCKETS - 1; i > 0; i--)
		if (pci_devs[0].pdev[i]) {
			sockets = i + 1;
			break;
		}

	for (i = 0; i < sockets; i++) {
		int channels;
		int csrows;

		/* Check the number of active and not disabled channels */
		rc = i7core_get_active_channels(i, &channels, &csrows);
		if (unlikely(rc < 0))
			goto fail0;

		num_channels += channels;
		num_csrows += csrows;
	}

	/* allocate a new MC control structure */
	mci = edac_mc_alloc(sizeof(*pvt), num_csrows, num_channels, 0);
	if (unlikely(!mci)) {
		rc = -ENOMEM;
		goto fail0;
	}

	debugf0("MC: " __FILE__ ": %s(): mci = %p\n", __func__, mci);

	mci->dev = &pdev->dev;	/* record ptr to the generic device */
	pvt = mci->pvt_info;
	memset(pvt, 0, sizeof(*pvt));
	pvt->sockets = sockets;
	mci->mc_idx = 0;

	/*
	 * FIXME: how to handle RDDR3 at MCI level? It is possible to have
	 * Mixed RDDR3/UDDR3 with Nehalem, provided that they are on different
	 * memory channels
	 */
	mci->mtype_cap = MEM_FLAG_DDR3;
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
	rc = mci_bind_devs(mci);
	if (unlikely(rc < 0))
		goto fail1;

	/* Get dimm basic config */
	for (i = 0; i < sockets; i++)
		get_dimm_config(mci, &csrow, i);

	/* add this new MC control structure to EDAC's list of MCs */
	if (unlikely(edac_mc_add_mc(mci))) {
		debugf0("MC: " __FILE__
			": %s(): failed edac_mc_add_mc()\n", __func__);
		/* FIXME: perhaps some code should go here that disables error
		 * reporting if we just enabled it
		 */

		rc = -EINVAL;
		goto fail1;
	}

	/* allocating generic PCI control info */
	i7core_pci = edac_pci_create_generic_ctl(&pdev->dev, EDAC_MOD_STR);
	if (unlikely(!i7core_pci)) {
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

	/* Registers on edac_mce in order to receive memory errors */
	pvt->edac_mce.priv = mci;
	pvt->edac_mce.check_error = i7core_mce_check_error;
	spin_lock_init(&pvt->mce_lock);

	rc = edac_mce_register(&pvt->edac_mce);
	if (unlikely (rc < 0)) {
		debugf0("MC: " __FILE__
			": %s(): failed edac_mce_register()\n", __func__);
		goto fail1;
	}

	i7core_printk(KERN_INFO, "Driver loaded.\n");

	return 0;

fail1:
	edac_mc_free(mci);

fail0:
	i7core_put_devices();
	return rc;
}

/*
 *	i7core_remove	destructor for one instance of device
 *
 */
static void __devexit i7core_remove(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;
	struct i7core_pvt *pvt;

	debugf0(__FILE__ ": %s()\n", __func__);

	if (i7core_pci)
		edac_pci_release_generic_ctl(i7core_pci);


	mci = edac_mc_del_mc(&pdev->dev);
	if (!mci)
		return;

	/* Unregisters on edac_mce in order to receive memory errors */
	pvt = mci->pvt_info;
	edac_mce_unregister(&pvt->edac_mce);

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
