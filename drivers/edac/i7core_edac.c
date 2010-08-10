/* Intel i7 core/Nehalem Memory Controller kernel module
 *
 * This driver supports yhe memory controllers found on the Intel
 * processor families i7core, i7core 7xx/8xx, i5core, Xeon 35xx,
 * Xeon 55xx and Xeon 56xx also known as Nehalem, Nehalem-EP, Lynnfield
 * and Westmere-EP.
 *
 * This file may be distributed under the terms of the
 * GNU General Public License version 2 only.
 *
 * Copyright (c) 2009-2010 by:
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
#include <linux/delay.h>
#include <linux/edac.h>
#include <linux/mmzone.h>
#include <linux/edac_mce.h>
#include <linux/smp.h>
#include <asm/processor.h>

#include "edac_core.h"

/*
 * This is used for Nehalem-EP and Nehalem-EX devices, where the non-core
 * registers start at bus 255, and are not reported by BIOS.
 * We currently find devices with only 2 sockets. In order to support more QPI
 * Quick Path Interconnect, just increment this number.
 */
#define MAX_SOCKET_BUSES	2


/*
 * Alter this version for the module when modifications are made
 */
#define I7CORE_REVISION    " Ver: 1.0.0 " __DATE__
#define EDAC_MOD_STR      "i7core_edac"

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

/* OFFSETS for Device 3 Function 2, as inicated on Xeon 5500 datasheet */
#define MC_COR_ECC_CNT_0	0x80
#define MC_COR_ECC_CNT_1	0x84
#define MC_COR_ECC_CNT_2	0x88
#define MC_COR_ECC_CNT_3	0x8c
#define MC_COR_ECC_CNT_4	0x90
#define MC_COR_ECC_CNT_5	0x94

#define DIMM_TOP_COR_ERR(r)			(((r) >> 16) & 0x7fff)
#define DIMM_BOT_COR_ERR(r)			((r) & 0x7fff)


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
	int			dev;
	int			func;
	int 			dev_id;
	int			optional;
};

struct pci_id_table {
	struct pci_id_descr	*descr;
	int			n_devs;
};

struct i7core_dev {
	struct list_head	list;
	u8			socket;
	struct pci_dev		**pdev;
	int			n_devs;
	struct mem_ctl_info	*mci;
};

struct i7core_pvt {
	struct pci_dev	*pci_noncore;
	struct pci_dev	*pci_mcr[MAX_MCR_FUNC + 1];
	struct pci_dev	*pci_ch[NUM_CHANS][MAX_CHAN_FUNC + 1];

	struct i7core_dev *i7core_dev;

	struct i7core_info	info;
	struct i7core_inject	inject;
	struct i7core_channel	channel[NUM_CHANS];

	int		channels; /* Number of active channels */

	int		ce_count_available;
	int 		csrow_map[NUM_CHANS][MAX_DIMMS];

			/* ECC corrected errors counts per udimm */
	unsigned long	udimm_ce_count[MAX_DIMMS];
	int		udimm_last_ce_count[MAX_DIMMS];
			/* ECC corrected errors counts per rdimm */
	unsigned long	rdimm_ce_count[NUM_CHANS][MAX_DIMMS];
	int		rdimm_last_ce_count[NUM_CHANS][MAX_DIMMS];

	unsigned int	is_registered;

	/* mcelog glue */
	struct edac_mce		edac_mce;

	/* Fifo double buffers */
	struct mce		mce_entry[MCE_LOG_LEN];
	struct mce		mce_outentry[MCE_LOG_LEN];

	/* Fifo in/out counters */
	unsigned		mce_in, mce_out;

	/* Count indicator to show errors not got */
	unsigned		mce_overrun;
};

/* Static vars */
static LIST_HEAD(i7core_edac_list);
static DEFINE_MUTEX(i7core_edac_lock);

#define PCI_DESCR(device, function, device_id)	\
	.dev = (device),			\
	.func = (function),			\
	.dev_id = (device_id)

struct pci_id_descr pci_dev_descr_i7core_nehalem[] = {
		/* Memory controller */
	{ PCI_DESCR(3, 0, PCI_DEVICE_ID_INTEL_I7_MCR)     },
	{ PCI_DESCR(3, 1, PCI_DEVICE_ID_INTEL_I7_MC_TAD)  },
			/* Exists only for RDIMM */
	{ PCI_DESCR(3, 2, PCI_DEVICE_ID_INTEL_I7_MC_RAS), .optional = 1  },
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
	{ PCI_DESCR(0, 0, PCI_DEVICE_ID_INTEL_I7_NONCORE)  },

};

struct pci_id_descr pci_dev_descr_lynnfield[] = {
	{ PCI_DESCR( 3, 0, PCI_DEVICE_ID_INTEL_LYNNFIELD_MCR)         },
	{ PCI_DESCR( 3, 1, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_TAD)      },
	{ PCI_DESCR( 3, 4, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_TEST)     },

	{ PCI_DESCR( 4, 0, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH0_CTRL) },
	{ PCI_DESCR( 4, 1, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH0_ADDR) },
	{ PCI_DESCR( 4, 2, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH0_RANK) },
	{ PCI_DESCR( 4, 3, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH0_TC)   },

	{ PCI_DESCR( 5, 0, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH1_CTRL) },
	{ PCI_DESCR( 5, 1, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH1_ADDR) },
	{ PCI_DESCR( 5, 2, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH1_RANK) },
	{ PCI_DESCR( 5, 3, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH1_TC)   },

	/*
	 * This is the PCI device has an alternate address on some
	 * processors like Core i7 860
	 */
	{ PCI_DESCR( 0, 0, PCI_DEVICE_ID_INTEL_LYNNFIELD_NONCORE)     },
};

struct pci_id_descr pci_dev_descr_i7core_westmere[] = {
		/* Memory controller */
	{ PCI_DESCR(3, 0, PCI_DEVICE_ID_INTEL_LYNNFIELD_MCR_REV2)     },
	{ PCI_DESCR(3, 1, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_TAD_REV2)  },
			/* Exists only for RDIMM */
	{ PCI_DESCR(3, 2, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_RAS_REV2), .optional = 1  },
	{ PCI_DESCR(3, 4, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_TEST_REV2) },

		/* Channel 0 */
	{ PCI_DESCR(4, 0, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH0_CTRL_REV2) },
	{ PCI_DESCR(4, 1, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH0_ADDR_REV2) },
	{ PCI_DESCR(4, 2, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH0_RANK_REV2) },
	{ PCI_DESCR(4, 3, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH0_TC_REV2)   },

		/* Channel 1 */
	{ PCI_DESCR(5, 0, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH1_CTRL_REV2) },
	{ PCI_DESCR(5, 1, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH1_ADDR_REV2) },
	{ PCI_DESCR(5, 2, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH1_RANK_REV2) },
	{ PCI_DESCR(5, 3, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH1_TC_REV2)   },

		/* Channel 2 */
	{ PCI_DESCR(6, 0, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH2_CTRL_REV2) },
	{ PCI_DESCR(6, 1, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH2_ADDR_REV2) },
	{ PCI_DESCR(6, 2, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH2_RANK_REV2) },
	{ PCI_DESCR(6, 3, PCI_DEVICE_ID_INTEL_LYNNFIELD_MC_CH2_TC_REV2)   },

		/* Generic Non-core registers */
	{ PCI_DESCR(0, 0, PCI_DEVICE_ID_INTEL_LYNNFIELD_NONCORE_REV2)  },

};

#define PCI_ID_TABLE_ENTRY(A) { A, ARRAY_SIZE(A) }
struct pci_id_table pci_dev_table[] = {
	PCI_ID_TABLE_ENTRY(pci_dev_descr_i7core_nehalem),
	PCI_ID_TABLE_ENTRY(pci_dev_descr_lynnfield),
	PCI_ID_TABLE_ENTRY(pci_dev_descr_i7core_westmere),
};

/*
 *	pci_device_id	table for which devices we are looking for
 */
static const struct pci_device_id i7core_pci_tbl[] __devinitdata = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_X58_HUB_MGMT)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_LYNNFIELD_QPI_LINK0)},
	{0,}			/* 0 terminated list. */
};

static struct edac_pci_ctl_info *i7core_pci;

/****************************************************************************
			Anciliary status routines
 ****************************************************************************/

	/* MC_CONTROL bits */
#define CH_ACTIVE(pvt, ch)	((pvt)->info.mc_control & (1 << (8 + ch)))
#define ECCx8(pvt)		((pvt)->info.mc_control & (1 << 1))

	/* MC_STATUS bits */
#define ECC_ENABLED(pvt)	((pvt)->info.mc_status & (1 << 4))
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

static struct i7core_dev *get_i7core_dev(u8 socket)
{
	struct i7core_dev *i7core_dev;

	list_for_each_entry(i7core_dev, &i7core_edac_list, list) {
		if (i7core_dev->socket == socket)
			return i7core_dev;
	}

	return NULL;
}

/****************************************************************************
			Memory check routines
 ****************************************************************************/
static struct pci_dev *get_pdev_slot_func(u8 socket, unsigned slot,
					  unsigned func)
{
	struct i7core_dev *i7core_dev = get_i7core_dev(socket);
	int i;

	if (!i7core_dev)
		return NULL;

	for (i = 0; i < i7core_dev->n_devs; i++) {
		if (!i7core_dev->pdev[i])
			continue;

		if (PCI_SLOT(i7core_dev->pdev[i]->devfn) == slot &&
		    PCI_FUNC(i7core_dev->pdev[i]->devfn) == func) {
			return i7core_dev->pdev[i];
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

static int get_dimm_config(struct mem_ctl_info *mci, int *csrow)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	struct csrow_info *csr;
	struct pci_dev *pdev;
	int i, j;
	unsigned long last_page = 0;
	enum edac_type mode;
	enum mem_type mtype;

	/* Get data from the MC register, function 0 */
	pdev = pvt->pci_mcr[0];
	if (!pdev)
		return -ENODEV;

	/* Device 3 function 0 reads */
	pci_read_config_dword(pdev, MC_CONTROL, &pvt->info.mc_control);
	pci_read_config_dword(pdev, MC_STATUS, &pvt->info.mc_status);
	pci_read_config_dword(pdev, MC_MAX_DOD, &pvt->info.max_dod);
	pci_read_config_dword(pdev, MC_CHANNEL_MAPPER, &pvt->info.ch_map);

	debugf0("QPI %d control=0x%08x status=0x%08x dod=0x%08x map=0x%08x\n",
		pvt->i7core_dev->socket, pvt->info.mc_control, pvt->info.mc_status,
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

		if (!pvt->pci_ch[i][0])
			continue;

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

		pvt->channel[i].ranks = (data & QUAD_RANK_PRESENT) ?
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
		pci_read_config_dword(pvt->pci_ch[i][1],
				MC_DOD_CH_DIMM0, &dimm_dod[0]);
		pci_read_config_dword(pvt->pci_ch[i][1],
				MC_DOD_CH_DIMM1, &dimm_dod[1]);
		pci_read_config_dword(pvt->pci_ch[i][1],
				MC_DOD_CH_DIMM2, &dimm_dod[2]);

		debugf0("Ch%d phy rd%d, wr%d (0x%08x): "
			"%d ranks, %cDIMMs\n",
			i,
			RDLCH(pvt->info.ch_map, i), WRLCH(pvt->info.ch_map, i),
			data,
			pvt->channel[i].ranks,
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

			pvt->channel[i].dimms++;

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

			pvt->csrow_map[i][j] = *csrow;

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

	if (!pvt->pci_ch[pvt->inject.channel][0])
		return -ENODEV;

	pci_write_config_dword(pvt->pci_ch[pvt->inject.channel][0],
				MC_CHANNEL_ERROR_INJECT, 0);

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

#define DECLARE_ADDR_MATCH(param, limit)			\
static ssize_t i7core_inject_store_##param(			\
		struct mem_ctl_info *mci,			\
		const char *data, size_t count)			\
{								\
	struct i7core_pvt *pvt;					\
	long value;						\
	int rc;							\
								\
	debugf1("%s()\n", __func__);				\
	pvt = mci->pvt_info;					\
								\
	if (pvt->inject.enable)					\
		disable_inject(mci);				\
								\
	if (!strcasecmp(data, "any") || !strcasecmp(data, "any\n"))\
		value = -1;					\
	else {							\
		rc = strict_strtoul(data, 10, &value);		\
		if ((rc < 0) || (value >= limit))		\
			return -EIO;				\
	}							\
								\
	pvt->inject.param = value;				\
								\
	return count;						\
}								\
								\
static ssize_t i7core_inject_show_##param(			\
		struct mem_ctl_info *mci,			\
		char *data)					\
{								\
	struct i7core_pvt *pvt;					\
								\
	pvt = mci->pvt_info;					\
	debugf1("%s() pvt=%p\n", __func__, pvt);		\
	if (pvt->inject.param < 0)				\
		return sprintf(data, "any\n");			\
	else							\
		return sprintf(data, "%d\n", pvt->inject.param);\
}

#define ATTR_ADDR_MATCH(param)					\
	{							\
		.attr = {					\
			.name = #param,				\
			.mode = (S_IRUGO | S_IWUSR)		\
		},						\
		.show  = i7core_inject_show_##param,		\
		.store = i7core_inject_store_##param,		\
	}

DECLARE_ADDR_MATCH(channel, 3);
DECLARE_ADDR_MATCH(dimm, 3);
DECLARE_ADDR_MATCH(rank, 4);
DECLARE_ADDR_MATCH(bank, 32);
DECLARE_ADDR_MATCH(page, 0x10000);
DECLARE_ADDR_MATCH(col, 0x4000);

static int write_and_test(struct pci_dev *dev, int where, u32 val)
{
	u32 read;
	int count;

	debugf0("setting pci %02x:%02x.%x reg=%02x value=%08x\n",
		dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
		where, val);

	for (count = 0; count < 10; count++) {
		if (count)
			msleep(100);
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
		mask |= 1LL << 41;
	else {
		if (pvt->channel[pvt->inject.channel].dimms > 2)
			mask |= (pvt->inject.dimm & 0x3LL) << 35;
		else
			mask |= (pvt->inject.dimm & 0x1LL) << 36;
	}

	/* Sets pvt->inject.rank mask */
	if (pvt->inject.rank < 0)
		mask |= 1LL << 40;
	else {
		if (pvt->channel[pvt->inject.channel].dimms > 2)
			mask |= (pvt->inject.rank & 0x1LL) << 34;
		else
			mask |= (pvt->inject.rank & 0x3LL) << 34;
	}

	/* Sets pvt->inject.bank mask */
	if (pvt->inject.bank < 0)
		mask |= 1LL << 39;
	else
		mask |= (pvt->inject.bank & 0x15LL) << 30;

	/* Sets pvt->inject.page mask */
	if (pvt->inject.page < 0)
		mask |= 1LL << 38;
	else
		mask |= (pvt->inject.page & 0xffff) << 14;

	/* Sets pvt->inject.column mask */
	if (pvt->inject.col < 0)
		mask |= 1LL << 37;
	else
		mask |= (pvt->inject.col & 0x3fff);

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
	pci_write_config_dword(pvt->pci_noncore,
			       MC_CFG_CONTROL, 0x2);

	write_and_test(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ADDR_MATCH, mask);
	write_and_test(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ADDR_MATCH + 4, mask >> 32L);

	write_and_test(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ERROR_MASK, pvt->inject.eccmask);

	write_and_test(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ERROR_INJECT, injectmask);

	/*
	 * This is something undocumented, based on my tests
	 * Without writing 8 to this register, errors aren't injected. Not sure
	 * why.
	 */
	pci_write_config_dword(pvt->pci_noncore,
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

	if (!pvt->pci_ch[pvt->inject.channel][0])
		return 0;

	pci_read_config_dword(pvt->pci_ch[pvt->inject.channel][0],
			       MC_CHANNEL_ERROR_INJECT, &injectmask);

	debugf0("Inject error read: 0x%018x\n", injectmask);

	if (injectmask & 0x0c)
		pvt->inject.enable = 1;

	return sprintf(data, "%d\n", pvt->inject.enable);
}

#define DECLARE_COUNTER(param)					\
static ssize_t i7core_show_counter_##param(			\
		struct mem_ctl_info *mci,			\
		char *data)					\
{								\
	struct i7core_pvt *pvt = mci->pvt_info;			\
								\
	debugf1("%s() \n", __func__);				\
	if (!pvt->ce_count_available || (pvt->is_registered))	\
		return sprintf(data, "data unavailable\n");	\
	return sprintf(data, "%lu\n",				\
			pvt->udimm_ce_count[param]);		\
}

#define ATTR_COUNTER(param)					\
	{							\
		.attr = {					\
			.name = __stringify(udimm##param),	\
			.mode = (S_IRUGO | S_IWUSR)		\
		},						\
		.show  = i7core_show_counter_##param		\
	}

DECLARE_COUNTER(0);
DECLARE_COUNTER(1);
DECLARE_COUNTER(2);

/*
 * Sysfs struct
 */


static struct mcidev_sysfs_attribute i7core_addrmatch_attrs[] = {
	ATTR_ADDR_MATCH(channel),
	ATTR_ADDR_MATCH(dimm),
	ATTR_ADDR_MATCH(rank),
	ATTR_ADDR_MATCH(bank),
	ATTR_ADDR_MATCH(page),
	ATTR_ADDR_MATCH(col),
	{ .attr = { .name = NULL } }
};

static struct mcidev_sysfs_group i7core_inject_addrmatch = {
	.name  = "inject_addrmatch",
	.mcidev_attr = i7core_addrmatch_attrs,
};

static struct mcidev_sysfs_attribute i7core_udimm_counters_attrs[] = {
	ATTR_COUNTER(0),
	ATTR_COUNTER(1),
	ATTR_COUNTER(2),
};

static struct mcidev_sysfs_group i7core_udimm_counters = {
	.name  = "all_channel_counts",
	.mcidev_attr = i7core_udimm_counters_attrs,
};

static struct mcidev_sysfs_attribute i7core_sysfs_attrs[] = {
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
		.grp = &i7core_inject_addrmatch,
	}, {
		.attr = {
			.name = "inject_enable",
			.mode = (S_IRUGO | S_IWUSR)
		},
		.show  = i7core_inject_enable_show,
		.store = i7core_inject_enable_store,
	},
	{ .attr = { .name = NULL } },	/* Reserved for udimm counters */
	{ .attr = { .name = NULL } }
};

/****************************************************************************
	Device initialization routines: put/get, init/exit
 ****************************************************************************/

/*
 *	i7core_put_devices	'put' all the devices that we have
 *				reserved via 'get'
 */
static void i7core_put_devices(struct i7core_dev *i7core_dev)
{
	int i;

	debugf0(__FILE__ ": %s()\n", __func__);
	for (i = 0; i < i7core_dev->n_devs; i++) {
		struct pci_dev *pdev = i7core_dev->pdev[i];
		if (!pdev)
			continue;
		debugf0("Removing dev %02x:%02x.%d\n",
			pdev->bus->number,
			PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
		pci_dev_put(pdev);
	}
	kfree(i7core_dev->pdev);
	list_del(&i7core_dev->list);
	kfree(i7core_dev);
}

static void i7core_put_all_devices(void)
{
	struct i7core_dev *i7core_dev, *tmp;

	list_for_each_entry_safe(i7core_dev, tmp, &i7core_edac_list, list)
		i7core_put_devices(i7core_dev);
}

static void __init i7core_xeon_pci_fixup(struct pci_id_table *table)
{
	struct pci_dev *pdev = NULL;
	int i;
	/*
	 * On Xeon 55xx, the Intel Quckpath Arch Generic Non-core pci buses
	 * aren't announced by acpi. So, we need to use a legacy scan probing
	 * to detect them
	 */
	while (table && table->descr) {
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL, table->descr[0].dev_id, NULL);
		if (unlikely(!pdev)) {
			for (i = 0; i < MAX_SOCKET_BUSES; i++)
				pcibios_scan_specific_bus(255-i);
		}
		pci_dev_put(pdev);
		table++;
	}
}

static unsigned i7core_pci_lastbus(void)
{
	int last_bus = 0, bus;
	struct pci_bus *b = NULL;

	while ((b = pci_find_next_bus(b)) != NULL) {
		bus = b->number;
		debugf0("Found bus %d\n", bus);
		if (bus > last_bus)
			last_bus = bus;
	}

	debugf0("Last bus %d\n", last_bus);

	return last_bus;
}

/*
 *	i7core_get_devices	Find and perform 'get' operation on the MCH's
 *			device/functions we want to reference for this driver
 *
 *			Need to 'get' device 16 func 1 and func 2
 */
int i7core_get_onedevice(struct pci_dev **prev, int devno,
			 struct pci_id_descr *dev_descr, unsigned n_devs,
			 unsigned last_bus)
{
	struct i7core_dev *i7core_dev;

	struct pci_dev *pdev = NULL;
	u8 bus = 0;
	u8 socket = 0;

	pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
			      dev_descr->dev_id, *prev);

	/*
	 * On Xeon 55xx, the Intel Quckpath Arch Generic Non-core regs
	 * is at addr 8086:2c40, instead of 8086:2c41. So, we need
	 * to probe for the alternate address in case of failure
	 */
	if (dev_descr->dev_id == PCI_DEVICE_ID_INTEL_I7_NONCORE && !pdev)
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
				      PCI_DEVICE_ID_INTEL_I7_NONCORE_ALT, *prev);

	if (dev_descr->dev_id == PCI_DEVICE_ID_INTEL_LYNNFIELD_NONCORE && !pdev)
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
				      PCI_DEVICE_ID_INTEL_LYNNFIELD_NONCORE_ALT,
				      *prev);

	if (!pdev) {
		if (*prev) {
			*prev = pdev;
			return 0;
		}

		if (dev_descr->optional)
			return 0;

		if (devno == 0)
			return -ENODEV;

		i7core_printk(KERN_INFO,
			"Device not found: dev %02x.%d PCI ID %04x:%04x\n",
			dev_descr->dev, dev_descr->func,
			PCI_VENDOR_ID_INTEL, dev_descr->dev_id);

		/* End of list, leave */
		return -ENODEV;
	}
	bus = pdev->bus->number;

	socket = last_bus - bus;

	i7core_dev = get_i7core_dev(socket);
	if (!i7core_dev) {
		i7core_dev = kzalloc(sizeof(*i7core_dev), GFP_KERNEL);
		if (!i7core_dev)
			return -ENOMEM;
		i7core_dev->pdev = kzalloc(sizeof(*i7core_dev->pdev) * n_devs,
					   GFP_KERNEL);
		if (!i7core_dev->pdev) {
			kfree(i7core_dev);
			return -ENOMEM;
		}
		i7core_dev->socket = socket;
		i7core_dev->n_devs = n_devs;
		list_add_tail(&i7core_dev->list, &i7core_edac_list);
	}

	if (i7core_dev->pdev[devno]) {
		i7core_printk(KERN_ERR,
			"Duplicated device for "
			"dev %02x:%02x.%d PCI ID %04x:%04x\n",
			bus, dev_descr->dev, dev_descr->func,
			PCI_VENDOR_ID_INTEL, dev_descr->dev_id);
		pci_dev_put(pdev);
		return -ENODEV;
	}

	i7core_dev->pdev[devno] = pdev;

	/* Sanity check */
	if (unlikely(PCI_SLOT(pdev->devfn) != dev_descr->dev ||
			PCI_FUNC(pdev->devfn) != dev_descr->func)) {
		i7core_printk(KERN_ERR,
			"Device PCI ID %04x:%04x "
			"has dev %02x:%02x.%d instead of dev %02x:%02x.%d\n",
			PCI_VENDOR_ID_INTEL, dev_descr->dev_id,
			bus, PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
			bus, dev_descr->dev, dev_descr->func);
		return -ENODEV;
	}

	/* Be sure that the device is enabled */
	if (unlikely(pci_enable_device(pdev) < 0)) {
		i7core_printk(KERN_ERR,
			"Couldn't enable "
			"dev %02x:%02x.%d PCI ID %04x:%04x\n",
			bus, dev_descr->dev, dev_descr->func,
			PCI_VENDOR_ID_INTEL, dev_descr->dev_id);
		return -ENODEV;
	}

	debugf0("Detected socket %d dev %02x:%02x.%d PCI ID %04x:%04x\n",
		socket, bus, dev_descr->dev,
		dev_descr->func,
		PCI_VENDOR_ID_INTEL, dev_descr->dev_id);

	*prev = pdev;

	return 0;
}

static int i7core_get_devices(struct pci_id_table *table)
{
	int i, rc, last_bus;
	struct pci_dev *pdev = NULL;
	struct pci_id_descr *dev_descr;

	last_bus = i7core_pci_lastbus();

	while (table && table->descr) {
		dev_descr = table->descr;
		for (i = 0; i < table->n_devs; i++) {
			pdev = NULL;
			do {
				rc = i7core_get_onedevice(&pdev, i,
							  &dev_descr[i],
							  table->n_devs,
							  last_bus);
				if (rc < 0) {
					if (i == 0) {
						i = table->n_devs;
						break;
					}
					i7core_put_all_devices();
					return -ENODEV;
				}
			} while (pdev);
		}
		table++;
	}

	return 0;
	return 0;
}

static int mci_bind_devs(struct mem_ctl_info *mci,
			 struct i7core_dev *i7core_dev)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	struct pci_dev *pdev;
	int i, func, slot;

	/* Associates i7core_dev and mci for future usage */
	pvt->i7core_dev = i7core_dev;
	i7core_dev->mci = mci;

	pvt->is_registered = 0;
	for (i = 0; i < i7core_dev->n_devs; i++) {
		pdev = i7core_dev->pdev[i];
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
		} else if (!slot && !func)
			pvt->pci_noncore = pdev;
		else
			goto error;

		debugf0("Associated fn %d.%d, dev = %p, socket %d\n",
			PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
			pdev, i7core_dev->socket);

		if (PCI_SLOT(pdev->devfn) == 3 &&
			PCI_FUNC(pdev->devfn) == 2)
			pvt->is_registered = 1;
	}

	/*
	 * Add extra nodes to count errors on udimm
	 * For registered memory, this is not needed, since the counters
	 * are already displayed at the standard locations
	 */
	if (!pvt->is_registered)
		i7core_sysfs_attrs[ARRAY_SIZE(i7core_sysfs_attrs)-2].grp =
			&i7core_udimm_counters;

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
static void i7core_rdimm_update_csrow(struct mem_ctl_info *mci,
					 int chan, int dimm, int add)
{
	char *msg;
	struct i7core_pvt *pvt = mci->pvt_info;
	int row = pvt->csrow_map[chan][dimm], i;

	for (i = 0; i < add; i++) {
		msg = kasprintf(GFP_KERNEL, "Corrected error "
				"(Socket=%d channel=%d dimm=%d)",
				pvt->i7core_dev->socket, chan, dimm);

		edac_mc_handle_fbd_ce(mci, row, 0, msg);
		kfree (msg);
	}
}

static void i7core_rdimm_update_ce_count(struct mem_ctl_info *mci,
			int chan, int new0, int new1, int new2)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	int add0 = 0, add1 = 0, add2 = 0;
	/* Updates CE counters if it is not the first time here */
	if (pvt->ce_count_available) {
		/* Updates CE counters */

		add2 = new2 - pvt->rdimm_last_ce_count[chan][2];
		add1 = new1 - pvt->rdimm_last_ce_count[chan][1];
		add0 = new0 - pvt->rdimm_last_ce_count[chan][0];

		if (add2 < 0)
			add2 += 0x7fff;
		pvt->rdimm_ce_count[chan][2] += add2;

		if (add1 < 0)
			add1 += 0x7fff;
		pvt->rdimm_ce_count[chan][1] += add1;

		if (add0 < 0)
			add0 += 0x7fff;
		pvt->rdimm_ce_count[chan][0] += add0;
	} else
		pvt->ce_count_available = 1;

	/* Store the new values */
	pvt->rdimm_last_ce_count[chan][2] = new2;
	pvt->rdimm_last_ce_count[chan][1] = new1;
	pvt->rdimm_last_ce_count[chan][0] = new0;

	/*updated the edac core */
	if (add0 != 0)
		i7core_rdimm_update_csrow(mci, chan, 0, add0);
	if (add1 != 0)
		i7core_rdimm_update_csrow(mci, chan, 1, add1);
	if (add2 != 0)
		i7core_rdimm_update_csrow(mci, chan, 2, add2);

}

static void i7core_rdimm_check_mc_ecc_err(struct mem_ctl_info *mci)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	u32 rcv[3][2];
	int i, new0, new1, new2;

	/*Read DEV 3: FUN 2:  MC_COR_ECC_CNT regs directly*/
	pci_read_config_dword(pvt->pci_mcr[2], MC_COR_ECC_CNT_0,
								&rcv[0][0]);
	pci_read_config_dword(pvt->pci_mcr[2], MC_COR_ECC_CNT_1,
								&rcv[0][1]);
	pci_read_config_dword(pvt->pci_mcr[2], MC_COR_ECC_CNT_2,
								&rcv[1][0]);
	pci_read_config_dword(pvt->pci_mcr[2], MC_COR_ECC_CNT_3,
								&rcv[1][1]);
	pci_read_config_dword(pvt->pci_mcr[2], MC_COR_ECC_CNT_4,
								&rcv[2][0]);
	pci_read_config_dword(pvt->pci_mcr[2], MC_COR_ECC_CNT_5,
								&rcv[2][1]);
	for (i = 0 ; i < 3; i++) {
		debugf3("MC_COR_ECC_CNT%d = 0x%x; MC_COR_ECC_CNT%d = 0x%x\n",
			(i * 2), rcv[i][0], (i * 2) + 1, rcv[i][1]);
		/*if the channel has 3 dimms*/
		if (pvt->channel[i].dimms > 2) {
			new0 = DIMM_BOT_COR_ERR(rcv[i][0]);
			new1 = DIMM_TOP_COR_ERR(rcv[i][0]);
			new2 = DIMM_BOT_COR_ERR(rcv[i][1]);
		} else {
			new0 = DIMM_TOP_COR_ERR(rcv[i][0]) +
					DIMM_BOT_COR_ERR(rcv[i][0]);
			new1 = DIMM_TOP_COR_ERR(rcv[i][1]) +
					DIMM_BOT_COR_ERR(rcv[i][1]);
			new2 = 0;
		}

		i7core_rdimm_update_ce_count(mci, i, new0, new1, new2);
	}
}

/* This function is based on the device 3 function 4 registers as described on:
 * Intel Xeon Processor 5500 Series Datasheet Volume 2
 *	http://www.intel.com/Assets/PDF/datasheet/321322.pdf
 * also available at:
 * 	http://www.arrownac.com/manufacturers/intel/s/nehalem/5500-datasheet-v2.pdf
 */
static void i7core_udimm_check_mc_ecc_err(struct mem_ctl_info *mci)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	u32 rcv1, rcv0;
	int new0, new1, new2;

	if (!pvt->pci_mcr[4]) {
		debugf0("%s MCR registers not found\n", __func__);
		return;
	}

	/* Corrected test errors */
	pci_read_config_dword(pvt->pci_mcr[4], MC_TEST_ERR_RCV1, &rcv1);
	pci_read_config_dword(pvt->pci_mcr[4], MC_TEST_ERR_RCV0, &rcv0);

	/* Store the new values */
	new2 = DIMM2_COR_ERR(rcv1);
	new1 = DIMM1_COR_ERR(rcv0);
	new0 = DIMM0_COR_ERR(rcv0);

	/* Updates CE counters if it is not the first time here */
	if (pvt->ce_count_available) {
		/* Updates CE counters */
		int add0, add1, add2;

		add2 = new2 - pvt->udimm_last_ce_count[2];
		add1 = new1 - pvt->udimm_last_ce_count[1];
		add0 = new0 - pvt->udimm_last_ce_count[0];

		if (add2 < 0)
			add2 += 0x7fff;
		pvt->udimm_ce_count[2] += add2;

		if (add1 < 0)
			add1 += 0x7fff;
		pvt->udimm_ce_count[1] += add1;

		if (add0 < 0)
			add0 += 0x7fff;
		pvt->udimm_ce_count[0] += add0;

		if (add0 | add1 | add2)
			i7core_printk(KERN_ERR, "New Corrected error(s): "
				      "dimm0: +%d, dimm1: +%d, dimm2 +%d\n",
				      add0, add1, add2);
	} else
		pvt->ce_count_available = 1;

	/* Store the new values */
	pvt->udimm_last_ce_count[2] = new2;
	pvt->udimm_last_ce_count[1] = new1;
	pvt->udimm_last_ce_count[0] = new0;
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
	struct i7core_pvt *pvt = mci->pvt_info;
	char *type, *optype, *err, *msg;
	unsigned long error = m->status & 0x1ff0000l;
	u32 optypenum = (m->status >> 4) & 0x07;
	u32 core_err_cnt = (m->status >> 38) && 0x7fff;
	u32 dimm = (m->misc >> 16) & 0x3;
	u32 channel = (m->misc >> 18) & 0x3;
	u32 syndrome = m->misc >> 32;
	u32 errnum = find_first_bit(&error, 32);
	int csrow;

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
		"%s (addr = 0x%08llx, cpu=%d, Dimm=%d, Channel=%d, "
		"syndrome=0x%08x, count=%d, Err=%08llx:%08llx (%s: %s))\n",
		type, (long long) m->addr, m->cpu, dimm, channel,
		syndrome, core_err_cnt, (long long)m->status,
		(long long)m->misc, optype, err);

	debugf0("%s", msg);

	csrow = pvt->csrow_map[channel][dimm];

	/* Call the helper to output message */
	if (m->mcgstatus & 1)
		edac_mc_handle_fbd_ue(mci, csrow, 0,
				0 /* FIXME: should be channel here */, msg);
	else if (!pvt->is_registered)
		edac_mc_handle_fbd_ce(mci, csrow,
				0 /* FIXME: should be channel here */, msg);

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
	struct mce *m;

	/*
	 * MCE first step: Copy all mce errors into a temporary buffer
	 * We use a double buffering here, to reduce the risk of
	 * loosing an error.
	 */
	smp_rmb();
	count = (pvt->mce_out + MCE_LOG_LEN - pvt->mce_in)
		% MCE_LOG_LEN;
	if (!count)
		goto check_ce_error;

	m = pvt->mce_outentry;
	if (pvt->mce_in + count > MCE_LOG_LEN) {
		unsigned l = MCE_LOG_LEN - pvt->mce_in;

		memcpy(m, &pvt->mce_entry[pvt->mce_in], sizeof(*m) * l);
		smp_wmb();
		pvt->mce_in = 0;
		count -= l;
		m += l;
	}
	memcpy(m, &pvt->mce_entry[pvt->mce_in], sizeof(*m) * count);
	smp_wmb();
	pvt->mce_in += count;

	smp_rmb();
	if (pvt->mce_overrun) {
		i7core_printk(KERN_ERR, "Lost %d memory errors\n",
			      pvt->mce_overrun);
		smp_wmb();
		pvt->mce_overrun = 0;
	}

	/*
	 * MCE second step: parse errors and display
	 */
	for (i = 0; i < count; i++)
		i7core_mce_output_error(mci, &pvt->mce_outentry[i]);

	/*
	 * Now, let's increment CE error counts
	 */
check_ce_error:
	if (!pvt->is_registered)
		i7core_udimm_check_mc_ecc_err(mci);
	else
		i7core_rdimm_check_mc_ecc_err(mci);
}

/*
 * i7core_mce_check_error	Replicates mcelog routine to get errors
 *				This routine simply queues mcelog errors, and
 *				return. The error itself should be handled later
 *				by i7core_check_error.
 * WARNING: As this routine should be called at NMI time, extra care should
 * be taken to avoid deadlocks, and to be as fast as possible.
 */
static int i7core_mce_check_error(void *priv, struct mce *mce)
{
	struct mem_ctl_info *mci = priv;
	struct i7core_pvt *pvt = mci->pvt_info;

	/*
	 * Just let mcelog handle it if the error is
	 * outside the memory controller
	 */
	if (((mce->status & 0xffff) >> 7) != 1)
		return 0;

	/* Bank 8 registers are the only ones that we know how to handle */
	if (mce->bank != 8)
		return 0;

#ifdef CONFIG_SMP
	/* Only handle if it is the right mc controller */
	if (cpu_data(mce->cpu).phys_proc_id != pvt->i7core_dev->socket)
		return 0;
#endif

	smp_rmb();
	if ((pvt->mce_out + 1) % MCE_LOG_LEN == pvt->mce_in) {
		smp_wmb();
		pvt->mce_overrun++;
		return 0;
	}

	/* Copy memory error at the ringbuffer */
	memcpy(&pvt->mce_entry[pvt->mce_out], mce, sizeof(*mce));
	smp_wmb();
	pvt->mce_out = (pvt->mce_out + 1) % MCE_LOG_LEN;

	/* Handle fatal errors immediately */
	if (mce->mcgstatus & 1)
		i7core_check_error(mci);

	/* Advice mcelog that the error were handled */
	return 1;
}

static int i7core_register_mci(struct i7core_dev *i7core_dev,
			       int num_channels, int num_csrows)
{
	struct mem_ctl_info *mci;
	struct i7core_pvt *pvt;
	int csrow = 0;
	int rc;

	/* allocate a new MC control structure */
	mci = edac_mc_alloc(sizeof(*pvt), num_csrows, num_channels,
			    i7core_dev->socket);
	if (unlikely(!mci))
		return -ENOMEM;

	debugf0("MC: " __FILE__ ": %s(): mci = %p\n", __func__, mci);

	/* record ptr to the generic device */
	mci->dev = &i7core_dev->pdev[0]->dev;

	pvt = mci->pvt_info;
	memset(pvt, 0, sizeof(*pvt));

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
	mci->ctl_name = kasprintf(GFP_KERNEL, "i7 core #%d",
				  i7core_dev->socket);
	mci->dev_name = pci_name(i7core_dev->pdev[0]);
	mci->ctl_page_to_phys = NULL;
	mci->mc_driver_sysfs_attributes = i7core_sysfs_attrs;
	/* Set the function pointer to an actual operation function */
	mci->edac_check = i7core_check_error;

	/* Store pci devices at mci for faster access */
	rc = mci_bind_devs(mci, i7core_dev);
	if (unlikely(rc < 0))
		goto fail;

	/* Get dimm basic config */
	get_dimm_config(mci, &csrow);

	/* add this new MC control structure to EDAC's list of MCs */
	if (unlikely(edac_mc_add_mc(mci))) {
		debugf0("MC: " __FILE__
			": %s(): failed edac_mc_add_mc()\n", __func__);
		/* FIXME: perhaps some code should go here that disables error
		 * reporting if we just enabled it
		 */

		rc = -EINVAL;
		goto fail;
	}

	/* allocating generic PCI control info */
	i7core_pci = edac_pci_create_generic_ctl(&i7core_dev->pdev[0]->dev,
						 EDAC_MOD_STR);
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

	rc = edac_mce_register(&pvt->edac_mce);
	if (unlikely(rc < 0)) {
		debugf0("MC: " __FILE__
			": %s(): failed edac_mce_register()\n", __func__);
	}

fail:
	if (rc < 0)
		edac_mc_free(mci);
	return rc;
}

/*
 *	i7core_probe	Probe for ONE instance of device to see if it is
 *			present.
 *	return:
 *		0 for FOUND a device
 *		< 0 for error code
 */

static int probed = 0;

static int __devinit i7core_probe(struct pci_dev *pdev,
				  const struct pci_device_id *id)
{
	int rc;
	struct i7core_dev *i7core_dev;

	/* get the pci devices we want to reserve for our use */
	mutex_lock(&i7core_edac_lock);

	/*
	 * All memory controllers are allocated at the first pass.
	 */
	if (unlikely(probed >= 1)) {
		mutex_unlock(&i7core_edac_lock);
		return -EINVAL;
	}
	probed++;

	rc = i7core_get_devices(pci_dev_table);
	if (unlikely(rc < 0))
		goto fail0;

	list_for_each_entry(i7core_dev, &i7core_edac_list, list) {
		int channels;
		int csrows;

		/* Check the number of active and not disabled channels */
		rc = i7core_get_active_channels(i7core_dev->socket,
						&channels, &csrows);
		if (unlikely(rc < 0))
			goto fail1;

		rc = i7core_register_mci(i7core_dev, channels, csrows);
		if (unlikely(rc < 0))
			goto fail1;
	}

	i7core_printk(KERN_INFO, "Driver loaded.\n");

	mutex_unlock(&i7core_edac_lock);
	return 0;

fail1:
	i7core_put_all_devices();
fail0:
	mutex_unlock(&i7core_edac_lock);
	return rc;
}

/*
 *	i7core_remove	destructor for one instance of device
 *
 */
static void __devexit i7core_remove(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;
	struct i7core_dev *i7core_dev, *tmp;

	debugf0(__FILE__ ": %s()\n", __func__);

	if (i7core_pci)
		edac_pci_release_generic_ctl(i7core_pci);

	/*
	 * we have a trouble here: pdev value for removal will be wrong, since
	 * it will point to the X58 register used to detect that the machine
	 * is a Nehalem or upper design. However, due to the way several PCI
	 * devices are grouped together to provide MC functionality, we need
	 * to use a different method for releasing the devices
	 */

	mutex_lock(&i7core_edac_lock);
	list_for_each_entry_safe(i7core_dev, tmp, &i7core_edac_list, list) {
		mci = edac_mc_del_mc(&i7core_dev->pdev[0]->dev);
		if (mci) {
			struct i7core_pvt *pvt = mci->pvt_info;

			i7core_dev = pvt->i7core_dev;
			edac_mce_unregister(&pvt->edac_mce);
			kfree(mci->ctl_name);
			edac_mc_free(mci);
			i7core_put_devices(i7core_dev);
		} else {
			i7core_printk(KERN_ERR,
				      "Couldn't find mci for socket %d\n",
				      i7core_dev->socket);
		}
	}
	probed--;

	mutex_unlock(&i7core_edac_lock);
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

	i7core_xeon_pci_fixup(pci_dev_table);

	pci_rc = pci_register_driver(&i7core_driver);

	if (pci_rc >= 0)
		return 0;

	i7core_printk(KERN_ERR, "Failed to register device with error %d.\n",
		      pci_rc);

	return pci_rc;
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
