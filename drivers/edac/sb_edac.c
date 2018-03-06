/* Intel Sandy Bridge -EN/-EP/-EX Memory Controller kernel module
 *
 * This driver supports the memory controllers found on the Intel
 * processor family Sandy Bridge.
 *
 * This file may be distributed under the terms of the
 * GNU General Public License version 2 only.
 *
 * Copyright (c) 2011 by:
 *	 Mauro Carvalho Chehab
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/edac.h>
#include <linux/mmzone.h>
#include <linux/smp.h>
#include <linux/bitmap.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/processor.h>
#include <asm/mce.h>

#include "edac_module.h"

/* Static vars */
static LIST_HEAD(sbridge_edac_list);

/*
 * Alter this version for the module when modifications are made
 */
#define SBRIDGE_REVISION    " Ver: 1.1.2 "
#define EDAC_MOD_STR	    "sb_edac"

/*
 * Debug macros
 */
#define sbridge_printk(level, fmt, arg...)			\
	edac_printk(level, "sbridge", fmt, ##arg)

#define sbridge_mc_printk(mci, level, fmt, arg...)		\
	edac_mc_chipset_printk(mci, level, "sbridge", fmt, ##arg)

/*
 * Get a bit field at register value <v>, from bit <lo> to bit <hi>
 */
#define GET_BITFIELD(v, lo, hi)	\
	(((v) & GENMASK_ULL(hi, lo)) >> (lo))

/* Devices 12 Function 6, Offsets 0x80 to 0xcc */
static const u32 sbridge_dram_rule[] = {
	0x80, 0x88, 0x90, 0x98, 0xa0,
	0xa8, 0xb0, 0xb8, 0xc0, 0xc8,
};

static const u32 ibridge_dram_rule[] = {
	0x60, 0x68, 0x70, 0x78, 0x80,
	0x88, 0x90, 0x98, 0xa0,	0xa8,
	0xb0, 0xb8, 0xc0, 0xc8, 0xd0,
	0xd8, 0xe0, 0xe8, 0xf0, 0xf8,
};

static const u32 knl_dram_rule[] = {
	0x60, 0x68, 0x70, 0x78, 0x80, /* 0-4 */
	0x88, 0x90, 0x98, 0xa0, 0xa8, /* 5-9 */
	0xb0, 0xb8, 0xc0, 0xc8, 0xd0, /* 10-14 */
	0xd8, 0xe0, 0xe8, 0xf0, 0xf8, /* 15-19 */
	0x100, 0x108, 0x110, 0x118,   /* 20-23 */
};

#define DRAM_RULE_ENABLE(reg)	GET_BITFIELD(reg, 0,  0)
#define A7MODE(reg)		GET_BITFIELD(reg, 26, 26)

static char *show_dram_attr(u32 attr)
{
	switch (attr) {
		case 0:
			return "DRAM";
		case 1:
			return "MMCFG";
		case 2:
			return "NXM";
		default:
			return "unknown";
	}
}

static const u32 sbridge_interleave_list[] = {
	0x84, 0x8c, 0x94, 0x9c, 0xa4,
	0xac, 0xb4, 0xbc, 0xc4, 0xcc,
};

static const u32 ibridge_interleave_list[] = {
	0x64, 0x6c, 0x74, 0x7c, 0x84,
	0x8c, 0x94, 0x9c, 0xa4, 0xac,
	0xb4, 0xbc, 0xc4, 0xcc, 0xd4,
	0xdc, 0xe4, 0xec, 0xf4, 0xfc,
};

static const u32 knl_interleave_list[] = {
	0x64, 0x6c, 0x74, 0x7c, 0x84, /* 0-4 */
	0x8c, 0x94, 0x9c, 0xa4, 0xac, /* 5-9 */
	0xb4, 0xbc, 0xc4, 0xcc, 0xd4, /* 10-14 */
	0xdc, 0xe4, 0xec, 0xf4, 0xfc, /* 15-19 */
	0x104, 0x10c, 0x114, 0x11c,   /* 20-23 */
};

struct interleave_pkg {
	unsigned char start;
	unsigned char end;
};

static const struct interleave_pkg sbridge_interleave_pkg[] = {
	{ 0, 2 },
	{ 3, 5 },
	{ 8, 10 },
	{ 11, 13 },
	{ 16, 18 },
	{ 19, 21 },
	{ 24, 26 },
	{ 27, 29 },
};

static const struct interleave_pkg ibridge_interleave_pkg[] = {
	{ 0, 3 },
	{ 4, 7 },
	{ 8, 11 },
	{ 12, 15 },
	{ 16, 19 },
	{ 20, 23 },
	{ 24, 27 },
	{ 28, 31 },
};

static inline int sad_pkg(const struct interleave_pkg *table, u32 reg,
			  int interleave)
{
	return GET_BITFIELD(reg, table[interleave].start,
			    table[interleave].end);
}

/* Devices 12 Function 7 */

#define TOLM		0x80
#define TOHM		0x84
#define HASWELL_TOLM	0xd0
#define HASWELL_TOHM_0	0xd4
#define HASWELL_TOHM_1	0xd8
#define KNL_TOLM	0xd0
#define KNL_TOHM_0	0xd4
#define KNL_TOHM_1	0xd8

#define GET_TOLM(reg)		((GET_BITFIELD(reg, 0,  3) << 28) | 0x3ffffff)
#define GET_TOHM(reg)		((GET_BITFIELD(reg, 0, 20) << 25) | 0x3ffffff)

/* Device 13 Function 6 */

#define SAD_TARGET	0xf0

#define SOURCE_ID(reg)		GET_BITFIELD(reg, 9, 11)

#define SOURCE_ID_KNL(reg)	GET_BITFIELD(reg, 12, 14)

#define SAD_CONTROL	0xf4

/* Device 14 function 0 */

static const u32 tad_dram_rule[] = {
	0x40, 0x44, 0x48, 0x4c,
	0x50, 0x54, 0x58, 0x5c,
	0x60, 0x64, 0x68, 0x6c,
};
#define MAX_TAD	ARRAY_SIZE(tad_dram_rule)

#define TAD_LIMIT(reg)		((GET_BITFIELD(reg, 12, 31) << 26) | 0x3ffffff)
#define TAD_SOCK(reg)		GET_BITFIELD(reg, 10, 11)
#define TAD_CH(reg)		GET_BITFIELD(reg,  8,  9)
#define TAD_TGT3(reg)		GET_BITFIELD(reg,  6,  7)
#define TAD_TGT2(reg)		GET_BITFIELD(reg,  4,  5)
#define TAD_TGT1(reg)		GET_BITFIELD(reg,  2,  3)
#define TAD_TGT0(reg)		GET_BITFIELD(reg,  0,  1)

/* Device 15, function 0 */

#define MCMTR			0x7c
#define KNL_MCMTR		0x624

#define IS_ECC_ENABLED(mcmtr)		GET_BITFIELD(mcmtr, 2, 2)
#define IS_LOCKSTEP_ENABLED(mcmtr)	GET_BITFIELD(mcmtr, 1, 1)
#define IS_CLOSE_PG(mcmtr)		GET_BITFIELD(mcmtr, 0, 0)

/* Device 15, function 1 */

#define RASENABLES		0xac
#define IS_MIRROR_ENABLED(reg)		GET_BITFIELD(reg, 0, 0)

/* Device 15, functions 2-5 */

static const int mtr_regs[] = {
	0x80, 0x84, 0x88,
};

static const int knl_mtr_reg = 0xb60;

#define RANK_DISABLE(mtr)		GET_BITFIELD(mtr, 16, 19)
#define IS_DIMM_PRESENT(mtr)		GET_BITFIELD(mtr, 14, 14)
#define RANK_CNT_BITS(mtr)		GET_BITFIELD(mtr, 12, 13)
#define RANK_WIDTH_BITS(mtr)		GET_BITFIELD(mtr, 2, 4)
#define COL_WIDTH_BITS(mtr)		GET_BITFIELD(mtr, 0, 1)

static const u32 tad_ch_nilv_offset[] = {
	0x90, 0x94, 0x98, 0x9c,
	0xa0, 0xa4, 0xa8, 0xac,
	0xb0, 0xb4, 0xb8, 0xbc,
};
#define CHN_IDX_OFFSET(reg)		GET_BITFIELD(reg, 28, 29)
#define TAD_OFFSET(reg)			(GET_BITFIELD(reg,  6, 25) << 26)

static const u32 rir_way_limit[] = {
	0x108, 0x10c, 0x110, 0x114, 0x118,
};
#define MAX_RIR_RANGES ARRAY_SIZE(rir_way_limit)

#define IS_RIR_VALID(reg)	GET_BITFIELD(reg, 31, 31)
#define RIR_WAY(reg)		GET_BITFIELD(reg, 28, 29)

#define MAX_RIR_WAY	8

static const u32 rir_offset[MAX_RIR_RANGES][MAX_RIR_WAY] = {
	{ 0x120, 0x124, 0x128, 0x12c, 0x130, 0x134, 0x138, 0x13c },
	{ 0x140, 0x144, 0x148, 0x14c, 0x150, 0x154, 0x158, 0x15c },
	{ 0x160, 0x164, 0x168, 0x16c, 0x170, 0x174, 0x178, 0x17c },
	{ 0x180, 0x184, 0x188, 0x18c, 0x190, 0x194, 0x198, 0x19c },
	{ 0x1a0, 0x1a4, 0x1a8, 0x1ac, 0x1b0, 0x1b4, 0x1b8, 0x1bc },
};

#define RIR_RNK_TGT(type, reg) (((type) == BROADWELL) ? \
	GET_BITFIELD(reg, 20, 23) : GET_BITFIELD(reg, 16, 19))

#define RIR_OFFSET(type, reg) (((type) == HASWELL || (type) == BROADWELL) ? \
	GET_BITFIELD(reg,  2, 15) : GET_BITFIELD(reg,  2, 14))

/* Device 16, functions 2-7 */

/*
 * FIXME: Implement the error count reads directly
 */

static const u32 correrrcnt[] = {
	0x104, 0x108, 0x10c, 0x110,
};

#define RANK_ODD_OV(reg)		GET_BITFIELD(reg, 31, 31)
#define RANK_ODD_ERR_CNT(reg)		GET_BITFIELD(reg, 16, 30)
#define RANK_EVEN_OV(reg)		GET_BITFIELD(reg, 15, 15)
#define RANK_EVEN_ERR_CNT(reg)		GET_BITFIELD(reg,  0, 14)

static const u32 correrrthrsld[] = {
	0x11c, 0x120, 0x124, 0x128,
};

#define RANK_ODD_ERR_THRSLD(reg)	GET_BITFIELD(reg, 16, 30)
#define RANK_EVEN_ERR_THRSLD(reg)	GET_BITFIELD(reg,  0, 14)


/* Device 17, function 0 */

#define SB_RANK_CFG_A		0x0328

#define IB_RANK_CFG_A		0x0320

/*
 * sbridge structs
 */

#define NUM_CHANNELS		6	/* Max channels per MC */
#define MAX_DIMMS		3	/* Max DIMMS per channel */
#define KNL_MAX_CHAS		38	/* KNL max num. of Cache Home Agents */
#define KNL_MAX_CHANNELS	6	/* KNL max num. of PCI channels */
#define KNL_MAX_EDCS		8	/* Embedded DRAM controllers */
#define CHANNEL_UNSPECIFIED	0xf	/* Intel IA32 SDM 15-14 */

enum type {
	SANDY_BRIDGE,
	IVY_BRIDGE,
	HASWELL,
	BROADWELL,
	KNIGHTS_LANDING,
};

enum domain {
	IMC0 = 0,
	IMC1,
	SOCK,
};

enum mirroring_mode {
	NON_MIRRORING,
	ADDR_RANGE_MIRRORING,
	FULL_MIRRORING,
};

struct sbridge_pvt;
struct sbridge_info {
	enum type	type;
	u32		mcmtr;
	u32		rankcfgr;
	u64		(*get_tolm)(struct sbridge_pvt *pvt);
	u64		(*get_tohm)(struct sbridge_pvt *pvt);
	u64		(*rir_limit)(u32 reg);
	u64		(*sad_limit)(u32 reg);
	u32		(*interleave_mode)(u32 reg);
	u32		(*dram_attr)(u32 reg);
	const u32	*dram_rule;
	const u32	*interleave_list;
	const struct interleave_pkg *interleave_pkg;
	u8		max_sad;
	u8		max_interleave;
	u8		(*get_node_id)(struct sbridge_pvt *pvt);
	enum mem_type	(*get_memory_type)(struct sbridge_pvt *pvt);
	enum dev_type	(*get_width)(struct sbridge_pvt *pvt, u32 mtr);
	struct pci_dev	*pci_vtd;
};

struct sbridge_channel {
	u32		ranks;
	u32		dimms;
};

struct pci_id_descr {
	int			dev_id;
	int			optional;
	enum domain		dom;
};

struct pci_id_table {
	const struct pci_id_descr	*descr;
	int				n_devs_per_imc;
	int				n_devs_per_sock;
	int				n_imcs_per_sock;
	enum type			type;
};

struct sbridge_dev {
	struct list_head	list;
	u8			bus, mc;
	u8			node_id, source_id;
	struct pci_dev		**pdev;
	enum domain		dom;
	int			n_devs;
	int			i_devs;
	struct mem_ctl_info	*mci;
};

struct knl_pvt {
	struct pci_dev          *pci_cha[KNL_MAX_CHAS];
	struct pci_dev          *pci_channel[KNL_MAX_CHANNELS];
	struct pci_dev          *pci_mc0;
	struct pci_dev          *pci_mc1;
	struct pci_dev          *pci_mc0_misc;
	struct pci_dev          *pci_mc1_misc;
	struct pci_dev          *pci_mc_info; /* tolm, tohm */
};

struct sbridge_pvt {
	/* Devices per socket */
	struct pci_dev		*pci_ddrio;
	struct pci_dev		*pci_sad0, *pci_sad1;
	struct pci_dev		*pci_br0, *pci_br1;
	/* Devices per memory controller */
	struct pci_dev		*pci_ha, *pci_ta, *pci_ras;
	struct pci_dev		*pci_tad[NUM_CHANNELS];

	struct sbridge_dev	*sbridge_dev;

	struct sbridge_info	info;
	struct sbridge_channel	channel[NUM_CHANNELS];

	/* Memory type detection */
	bool			is_cur_addr_mirrored, is_lockstep, is_close_pg;
	bool			is_chan_hash;
	enum mirroring_mode	mirror_mode;

	/* Memory description */
	u64			tolm, tohm;
	struct knl_pvt knl;
};

#define PCI_DESCR(device_id, opt, domain)	\
	.dev_id = (device_id),		\
	.optional = opt,	\
	.dom = domain

static const struct pci_id_descr pci_dev_descr_sbridge[] = {
		/* Processor Home Agent */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_HA0,   0, IMC0) },

		/* Memory controller */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TA,    0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_RAS,   0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD0,  0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD1,  0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD2,  0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD3,  0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_DDRIO, 1, SOCK) },

		/* System Address Decoder */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_SAD0,      0, SOCK) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_SAD1,      0, SOCK) },

		/* Broadcast Registers */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_BR,        0, SOCK) },
};

#define PCI_ID_TABLE_ENTRY(A, N, M, T) {	\
	.descr = A,			\
	.n_devs_per_imc = N,	\
	.n_devs_per_sock = ARRAY_SIZE(A),	\
	.n_imcs_per_sock = M,	\
	.type = T			\
}

static const struct pci_id_table pci_dev_descr_sbridge_table[] = {
	PCI_ID_TABLE_ENTRY(pci_dev_descr_sbridge, ARRAY_SIZE(pci_dev_descr_sbridge), 1, SANDY_BRIDGE),
	{0,}			/* 0 terminated list. */
};

/* This changes depending if 1HA or 2HA:
 * 1HA:
 *	0x0eb8 (17.0) is DDRIO0
 * 2HA:
 *	0x0ebc (17.4) is DDRIO0
 */
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_1HA_DDRIO0	0x0eb8
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_2HA_DDRIO0	0x0ebc

/* pci ids */
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0		0x0ea0
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TA		0x0ea8
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_RAS		0x0e71
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD0	0x0eaa
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD1	0x0eab
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD2	0x0eac
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD3	0x0ead
#define PCI_DEVICE_ID_INTEL_IBRIDGE_SAD			0x0ec8
#define PCI_DEVICE_ID_INTEL_IBRIDGE_BR0			0x0ec9
#define PCI_DEVICE_ID_INTEL_IBRIDGE_BR1			0x0eca
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1		0x0e60
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TA		0x0e68
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_RAS		0x0e79
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD0	0x0e6a
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD1	0x0e6b
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD2	0x0e6c
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD3	0x0e6d

static const struct pci_id_descr pci_dev_descr_ibridge[] = {
		/* Processor Home Agent */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0,        0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1,        1, IMC1) },

		/* Memory controller */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TA,     0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_RAS,    0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD0,   0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD1,   0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD2,   0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD3,   0, IMC0) },

		/* Optional, mode 2HA */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TA,     1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_RAS,    1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD0,   1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD1,   1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD2,   1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD3,   1, IMC1) },

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_1HA_DDRIO0, 1, SOCK) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_2HA_DDRIO0, 1, SOCK) },

		/* System Address Decoder */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_SAD,            0, SOCK) },

		/* Broadcast Registers */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_BR0,            1, SOCK) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_BR1,            0, SOCK) },

};

static const struct pci_id_table pci_dev_descr_ibridge_table[] = {
	PCI_ID_TABLE_ENTRY(pci_dev_descr_ibridge, 12, 2, IVY_BRIDGE),
	{0,}			/* 0 terminated list. */
};

/* Haswell support */
/* EN processor:
 *	- 1 IMC
 *	- 3 DDR3 channels, 2 DPC per channel
 * EP processor:
 *	- 1 or 2 IMC
 *	- 4 DDR4 channels, 3 DPC per channel
 * EP 4S processor:
 *	- 2 IMC
 *	- 4 DDR4 channels, 3 DPC per channel
 * EX processor:
 *	- 2 IMC
 *	- each IMC interfaces with a SMI 2 channel
 *	- each SMI channel interfaces with a scalable memory buffer
 *	- each scalable memory buffer supports 4 DDR3/DDR4 channels, 3 DPC
 */
#define HASWELL_DDRCRCLKCONTROLS 0xa10 /* Ditto on Broadwell */
#define HASWELL_HASYSDEFEATURE2 0x84
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_VTD_MISC 0x2f28
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0	0x2fa0
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1	0x2f60
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TA	0x2fa8
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TM	0x2f71
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TA	0x2f68
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TM	0x2f79
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_CBO_SAD0 0x2ffc
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_CBO_SAD1 0x2ffd
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD0 0x2faa
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD1 0x2fab
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD2 0x2fac
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD3 0x2fad
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD0 0x2f6a
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD1 0x2f6b
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD2 0x2f6c
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD3 0x2f6d
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO0 0x2fbd
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO1 0x2fbf
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO2 0x2fb9
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO3 0x2fbb
static const struct pci_id_descr pci_dev_descr_haswell[] = {
	/* first item must be the HA */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0,      0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1,      1, IMC1) },

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TA,   0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TM,   0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD0, 0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD1, 0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD2, 1, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD3, 1, IMC0) },

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TA,   1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TM,   1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD0, 1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD1, 1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD2, 1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD3, 1, IMC1) },

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_CBO_SAD0, 0, SOCK) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_CBO_SAD1, 0, SOCK) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO0,   1, SOCK) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO1,   1, SOCK) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO2,   1, SOCK) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO3,   1, SOCK) },
};

static const struct pci_id_table pci_dev_descr_haswell_table[] = {
	PCI_ID_TABLE_ENTRY(pci_dev_descr_haswell, 13, 2, HASWELL),
	{0,}			/* 0 terminated list. */
};

/* Knight's Landing Support */
/*
 * KNL's memory channels are swizzled between memory controllers.
 * MC0 is mapped to CH3,4,5 and MC1 is mapped to CH0,1,2
 */
#define knl_channel_remap(mc, chan) ((mc) ? (chan) : (chan) + 3)

/* Memory controller, TAD tables, error injection - 2-8-0, 2-9-0 (2 of these) */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_MC       0x7840
/* DRAM channel stuff; bank addrs, dimmmtr, etc.. 2-8-2 - 2-9-4 (6 of these) */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_CHAN     0x7843
/* kdrwdbu TAD limits/offsets, MCMTR - 2-10-1, 2-11-1 (2 of these) */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_TA       0x7844
/* CHA broadcast registers, dram rules - 1-29-0 (1 of these) */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_SAD0     0x782a
/* SAD target - 1-29-1 (1 of these) */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_SAD1     0x782b
/* Caching / Home Agent */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_CHA      0x782c
/* Device with TOLM and TOHM, 0-5-0 (1 of these) */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_TOLHM    0x7810

/*
 * KNL differs from SB, IB, and Haswell in that it has multiple
 * instances of the same device with the same device ID, so we handle that
 * by creating as many copies in the table as we expect to find.
 * (Like device ID must be grouped together.)
 */

static const struct pci_id_descr pci_dev_descr_knl[] = {
	[0 ... 1]   = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_MC,    0, IMC0)},
	[2 ... 7]   = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_CHAN,  0, IMC0) },
	[8]	    = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_TA,    0, IMC0) },
	[9]	    = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_TOLHM, 0, IMC0) },
	[10]	    = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_SAD0,  0, SOCK) },
	[11]	    = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_SAD1,  0, SOCK) },
	[12 ... 49] = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_CHA,   0, SOCK) },
};

static const struct pci_id_table pci_dev_descr_knl_table[] = {
	PCI_ID_TABLE_ENTRY(pci_dev_descr_knl, ARRAY_SIZE(pci_dev_descr_knl), 1, KNIGHTS_LANDING),
	{0,}
};

/*
 * Broadwell support
 *
 * DE processor:
 *	- 1 IMC
 *	- 2 DDR3 channels, 2 DPC per channel
 * EP processor:
 *	- 1 or 2 IMC
 *	- 4 DDR4 channels, 3 DPC per channel
 * EP 4S processor:
 *	- 2 IMC
 *	- 4 DDR4 channels, 3 DPC per channel
 * EX processor:
 *	- 2 IMC
 *	- each IMC interfaces with a SMI 2 channel
 *	- each SMI channel interfaces with a scalable memory buffer
 *	- each scalable memory buffer supports 4 DDR3/DDR4 channels, 3 DPC
 */
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_VTD_MISC 0x6f28
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0	0x6fa0
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1	0x6f60
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TA	0x6fa8
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TM	0x6f71
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TA	0x6f68
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TM	0x6f79
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_CBO_SAD0 0x6ffc
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_CBO_SAD1 0x6ffd
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD0 0x6faa
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD1 0x6fab
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD2 0x6fac
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD3 0x6fad
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD0 0x6f6a
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD1 0x6f6b
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD2 0x6f6c
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD3 0x6f6d
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_DDRIO0 0x6faf

static const struct pci_id_descr pci_dev_descr_broadwell[] = {
	/* first item must be the HA */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0,      0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1,      1, IMC1) },

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TA,   0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TM,   0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD0, 0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD1, 0, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD2, 1, IMC0) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD3, 1, IMC0) },

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TA,   1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TM,   1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD0, 1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD1, 1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD2, 1, IMC1) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD3, 1, IMC1) },

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_CBO_SAD0, 0, SOCK) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_CBO_SAD1, 0, SOCK) },
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_DDRIO0,   1, SOCK) },
};

static const struct pci_id_table pci_dev_descr_broadwell_table[] = {
	PCI_ID_TABLE_ENTRY(pci_dev_descr_broadwell, 10, 2, BROADWELL),
	{0,}			/* 0 terminated list. */
};


/****************************************************************************
			Ancillary status routines
 ****************************************************************************/

static inline int numrank(enum type type, u32 mtr)
{
	int ranks = (1 << RANK_CNT_BITS(mtr));
	int max = 4;

	if (type == HASWELL || type == BROADWELL || type == KNIGHTS_LANDING)
		max = 8;

	if (ranks > max) {
		edac_dbg(0, "Invalid number of ranks: %d (max = %i) raw value = %x (%04x)\n",
			 ranks, max, (unsigned int)RANK_CNT_BITS(mtr), mtr);
		return -EINVAL;
	}

	return ranks;
}

static inline int numrow(u32 mtr)
{
	int rows = (RANK_WIDTH_BITS(mtr) + 12);

	if (rows < 13 || rows > 18) {
		edac_dbg(0, "Invalid number of rows: %d (should be between 14 and 17) raw value = %x (%04x)\n",
			 rows, (unsigned int)RANK_WIDTH_BITS(mtr), mtr);
		return -EINVAL;
	}

	return 1 << rows;
}

static inline int numcol(u32 mtr)
{
	int cols = (COL_WIDTH_BITS(mtr) + 10);

	if (cols > 12) {
		edac_dbg(0, "Invalid number of cols: %d (max = 4) raw value = %x (%04x)\n",
			 cols, (unsigned int)COL_WIDTH_BITS(mtr), mtr);
		return -EINVAL;
	}

	return 1 << cols;
}

static struct sbridge_dev *get_sbridge_dev(u8 bus, enum domain dom, int multi_bus,
					   struct sbridge_dev *prev)
{
	struct sbridge_dev *sbridge_dev;

	/*
	 * If we have devices scattered across several busses that pertain
	 * to the same memory controller, we'll lump them all together.
	 */
	if (multi_bus) {
		return list_first_entry_or_null(&sbridge_edac_list,
				struct sbridge_dev, list);
	}

	sbridge_dev = list_entry(prev ? prev->list.next
				      : sbridge_edac_list.next, struct sbridge_dev, list);

	list_for_each_entry_from(sbridge_dev, &sbridge_edac_list, list) {
		if (sbridge_dev->bus == bus && (dom == SOCK || dom == sbridge_dev->dom))
			return sbridge_dev;
	}

	return NULL;
}

static struct sbridge_dev *alloc_sbridge_dev(u8 bus, enum domain dom,
					     const struct pci_id_table *table)
{
	struct sbridge_dev *sbridge_dev;

	sbridge_dev = kzalloc(sizeof(*sbridge_dev), GFP_KERNEL);
	if (!sbridge_dev)
		return NULL;

	sbridge_dev->pdev = kcalloc(table->n_devs_per_imc,
				    sizeof(*sbridge_dev->pdev),
				    GFP_KERNEL);
	if (!sbridge_dev->pdev) {
		kfree(sbridge_dev);
		return NULL;
	}

	sbridge_dev->bus = bus;
	sbridge_dev->dom = dom;
	sbridge_dev->n_devs = table->n_devs_per_imc;
	list_add_tail(&sbridge_dev->list, &sbridge_edac_list);

	return sbridge_dev;
}

static void free_sbridge_dev(struct sbridge_dev *sbridge_dev)
{
	list_del(&sbridge_dev->list);
	kfree(sbridge_dev->pdev);
	kfree(sbridge_dev);
}

static u64 sbridge_get_tolm(struct sbridge_pvt *pvt)
{
	u32 reg;

	/* Address range is 32:28 */
	pci_read_config_dword(pvt->pci_sad1, TOLM, &reg);
	return GET_TOLM(reg);
}

static u64 sbridge_get_tohm(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->pci_sad1, TOHM, &reg);
	return GET_TOHM(reg);
}

static u64 ibridge_get_tolm(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->pci_br1, TOLM, &reg);

	return GET_TOLM(reg);
}

static u64 ibridge_get_tohm(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->pci_br1, TOHM, &reg);

	return GET_TOHM(reg);
}

static u64 rir_limit(u32 reg)
{
	return ((u64)GET_BITFIELD(reg,  1, 10) << 29) | 0x1fffffff;
}

static u64 sad_limit(u32 reg)
{
	return (GET_BITFIELD(reg, 6, 25) << 26) | 0x3ffffff;
}

static u32 interleave_mode(u32 reg)
{
	return GET_BITFIELD(reg, 1, 1);
}

static u32 dram_attr(u32 reg)
{
	return GET_BITFIELD(reg, 2, 3);
}

static u64 knl_sad_limit(u32 reg)
{
	return (GET_BITFIELD(reg, 7, 26) << 26) | 0x3ffffff;
}

static u32 knl_interleave_mode(u32 reg)
{
	return GET_BITFIELD(reg, 1, 2);
}

static const char * const knl_intlv_mode[] = {
	"[8:6]", "[10:8]", "[14:12]", "[32:30]"
};

static const char *get_intlv_mode_str(u32 reg, enum type t)
{
	if (t == KNIGHTS_LANDING)
		return knl_intlv_mode[knl_interleave_mode(reg)];
	else
		return interleave_mode(reg) ? "[8:6]" : "[8:6]XOR[18:16]";
}

static u32 dram_attr_knl(u32 reg)
{
	return GET_BITFIELD(reg, 3, 4);
}


static enum mem_type get_memory_type(struct sbridge_pvt *pvt)
{
	u32 reg;
	enum mem_type mtype;

	if (pvt->pci_ddrio) {
		pci_read_config_dword(pvt->pci_ddrio, pvt->info.rankcfgr,
				      &reg);
		if (GET_BITFIELD(reg, 11, 11))
			/* FIXME: Can also be LRDIMM */
			mtype = MEM_RDDR3;
		else
			mtype = MEM_DDR3;
	} else
		mtype = MEM_UNKNOWN;

	return mtype;
}

static enum mem_type haswell_get_memory_type(struct sbridge_pvt *pvt)
{
	u32 reg;
	bool registered = false;
	enum mem_type mtype = MEM_UNKNOWN;

	if (!pvt->pci_ddrio)
		goto out;

	pci_read_config_dword(pvt->pci_ddrio,
			      HASWELL_DDRCRCLKCONTROLS, &reg);
	/* Is_Rdimm */
	if (GET_BITFIELD(reg, 16, 16))
		registered = true;

	pci_read_config_dword(pvt->pci_ta, MCMTR, &reg);
	if (GET_BITFIELD(reg, 14, 14)) {
		if (registered)
			mtype = MEM_RDDR4;
		else
			mtype = MEM_DDR4;
	} else {
		if (registered)
			mtype = MEM_RDDR3;
		else
			mtype = MEM_DDR3;
	}

out:
	return mtype;
}

static enum dev_type knl_get_width(struct sbridge_pvt *pvt, u32 mtr)
{
	/* for KNL value is fixed */
	return DEV_X16;
}

static enum dev_type sbridge_get_width(struct sbridge_pvt *pvt, u32 mtr)
{
	/* there's no way to figure out */
	return DEV_UNKNOWN;
}

static enum dev_type __ibridge_get_width(u32 mtr)
{
	enum dev_type type;

	switch (mtr) {
	case 3:
		type = DEV_UNKNOWN;
		break;
	case 2:
		type = DEV_X16;
		break;
	case 1:
		type = DEV_X8;
		break;
	case 0:
		type = DEV_X4;
		break;
	}

	return type;
}

static enum dev_type ibridge_get_width(struct sbridge_pvt *pvt, u32 mtr)
{
	/*
	 * ddr3_width on the documentation but also valid for DDR4 on
	 * Haswell
	 */
	return __ibridge_get_width(GET_BITFIELD(mtr, 7, 8));
}

static enum dev_type broadwell_get_width(struct sbridge_pvt *pvt, u32 mtr)
{
	/* ddr3_width on the documentation but also valid for DDR4 */
	return __ibridge_get_width(GET_BITFIELD(mtr, 8, 9));
}

static enum mem_type knl_get_memory_type(struct sbridge_pvt *pvt)
{
	/* DDR4 RDIMMS and LRDIMMS are supported */
	return MEM_RDDR4;
}

static u8 get_node_id(struct sbridge_pvt *pvt)
{
	u32 reg;
	pci_read_config_dword(pvt->pci_br0, SAD_CONTROL, &reg);
	return GET_BITFIELD(reg, 0, 2);
}

static u8 haswell_get_node_id(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->pci_sad1, SAD_CONTROL, &reg);
	return GET_BITFIELD(reg, 0, 3);
}

static u8 knl_get_node_id(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->pci_sad1, SAD_CONTROL, &reg);
	return GET_BITFIELD(reg, 0, 2);
}


static u64 haswell_get_tolm(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->info.pci_vtd, HASWELL_TOLM, &reg);
	return (GET_BITFIELD(reg, 26, 31) << 26) | 0x3ffffff;
}

static u64 haswell_get_tohm(struct sbridge_pvt *pvt)
{
	u64 rc;
	u32 reg;

	pci_read_config_dword(pvt->info.pci_vtd, HASWELL_TOHM_0, &reg);
	rc = GET_BITFIELD(reg, 26, 31);
	pci_read_config_dword(pvt->info.pci_vtd, HASWELL_TOHM_1, &reg);
	rc = ((reg << 6) | rc) << 26;

	return rc | 0x1ffffff;
}

static u64 knl_get_tolm(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->knl.pci_mc_info, KNL_TOLM, &reg);
	return (GET_BITFIELD(reg, 26, 31) << 26) | 0x3ffffff;
}

static u64 knl_get_tohm(struct sbridge_pvt *pvt)
{
	u64 rc;
	u32 reg_lo, reg_hi;

	pci_read_config_dword(pvt->knl.pci_mc_info, KNL_TOHM_0, &reg_lo);
	pci_read_config_dword(pvt->knl.pci_mc_info, KNL_TOHM_1, &reg_hi);
	rc = ((u64)reg_hi << 32) | reg_lo;
	return rc | 0x3ffffff;
}


static u64 haswell_rir_limit(u32 reg)
{
	return (((u64)GET_BITFIELD(reg,  1, 11) + 1) << 29) - 1;
}

static inline u8 sad_pkg_socket(u8 pkg)
{
	/* on Ivy Bridge, nodeID is SASS, where A is HA and S is node id */
	return ((pkg >> 3) << 2) | (pkg & 0x3);
}

static inline u8 sad_pkg_ha(u8 pkg)
{
	return (pkg >> 2) & 0x1;
}

static int haswell_chan_hash(int idx, u64 addr)
{
	int i;

	/*
	 * XOR even bits from 12:26 to bit0 of idx,
	 *     odd bits from 13:27 to bit1
	 */
	for (i = 12; i < 28; i += 2)
		idx ^= (addr >> i) & 3;

	return idx;
}

/* Low bits of TAD limit, and some metadata. */
static const u32 knl_tad_dram_limit_lo[] = {
	0x400, 0x500, 0x600, 0x700,
	0x800, 0x900, 0xa00, 0xb00,
};

/* Low bits of TAD offset. */
static const u32 knl_tad_dram_offset_lo[] = {
	0x404, 0x504, 0x604, 0x704,
	0x804, 0x904, 0xa04, 0xb04,
};

/* High 16 bits of TAD limit and offset. */
static const u32 knl_tad_dram_hi[] = {
	0x408, 0x508, 0x608, 0x708,
	0x808, 0x908, 0xa08, 0xb08,
};

/* Number of ways a tad entry is interleaved. */
static const u32 knl_tad_ways[] = {
	8, 6, 4, 3, 2, 1,
};

/*
 * Retrieve the n'th Target Address Decode table entry
 * from the memory controller's TAD table.
 *
 * @pvt:	driver private data
 * @entry:	which entry you want to retrieve
 * @mc:		which memory controller (0 or 1)
 * @offset:	output tad range offset
 * @limit:	output address of first byte above tad range
 * @ways:	output number of interleave ways
 *
 * The offset value has curious semantics.  It's a sort of running total
 * of the sizes of all the memory regions that aren't mapped in this
 * tad table.
 */
static int knl_get_tad(const struct sbridge_pvt *pvt,
		const int entry,
		const int mc,
		u64 *offset,
		u64 *limit,
		int *ways)
{
	u32 reg_limit_lo, reg_offset_lo, reg_hi;
	struct pci_dev *pci_mc;
	int way_id;

	switch (mc) {
	case 0:
		pci_mc = pvt->knl.pci_mc0;
		break;
	case 1:
		pci_mc = pvt->knl.pci_mc1;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	pci_read_config_dword(pci_mc,
			knl_tad_dram_limit_lo[entry], &reg_limit_lo);
	pci_read_config_dword(pci_mc,
			knl_tad_dram_offset_lo[entry], &reg_offset_lo);
	pci_read_config_dword(pci_mc,
			knl_tad_dram_hi[entry], &reg_hi);

	/* Is this TAD entry enabled? */
	if (!GET_BITFIELD(reg_limit_lo, 0, 0))
		return -ENODEV;

	way_id = GET_BITFIELD(reg_limit_lo, 3, 5);

	if (way_id < ARRAY_SIZE(knl_tad_ways)) {
		*ways = knl_tad_ways[way_id];
	} else {
		*ways = 0;
		sbridge_printk(KERN_ERR,
				"Unexpected value %d in mc_tad_limit_lo wayness field\n",
				way_id);
		return -ENODEV;
	}

	/*
	 * The least significant 6 bits of base and limit are truncated.
	 * For limit, we fill the missing bits with 1s.
	 */
	*offset = ((u64) GET_BITFIELD(reg_offset_lo, 6, 31) << 6) |
				((u64) GET_BITFIELD(reg_hi, 0,  15) << 32);
	*limit = ((u64) GET_BITFIELD(reg_limit_lo,  6, 31) << 6) | 63 |
				((u64) GET_BITFIELD(reg_hi, 16, 31) << 32);

	return 0;
}

/* Determine which memory controller is responsible for a given channel. */
static int knl_channel_mc(int channel)
{
	WARN_ON(channel < 0 || channel >= 6);

	return channel < 3 ? 1 : 0;
}

/*
 * Get the Nth entry from EDC_ROUTE_TABLE register.
 * (This is the per-tile mapping of logical interleave targets to
 *  physical EDC modules.)
 *
 * entry 0: 0:2
 *       1: 3:5
 *       2: 6:8
 *       3: 9:11
 *       4: 12:14
 *       5: 15:17
 *       6: 18:20
 *       7: 21:23
 * reserved: 24:31
 */
static u32 knl_get_edc_route(int entry, u32 reg)
{
	WARN_ON(entry >= KNL_MAX_EDCS);
	return GET_BITFIELD(reg, entry*3, (entry*3)+2);
}

/*
 * Get the Nth entry from MC_ROUTE_TABLE register.
 * (This is the per-tile mapping of logical interleave targets to
 *  physical DRAM channels modules.)
 *
 * entry 0: mc 0:2   channel 18:19
 *       1: mc 3:5   channel 20:21
 *       2: mc 6:8   channel 22:23
 *       3: mc 9:11  channel 24:25
 *       4: mc 12:14 channel 26:27
 *       5: mc 15:17 channel 28:29
 * reserved: 30:31
 *
 * Though we have 3 bits to identify the MC, we should only see
 * the values 0 or 1.
 */

static u32 knl_get_mc_route(int entry, u32 reg)
{
	int mc, chan;

	WARN_ON(entry >= KNL_MAX_CHANNELS);

	mc = GET_BITFIELD(reg, entry*3, (entry*3)+2);
	chan = GET_BITFIELD(reg, (entry*2) + 18, (entry*2) + 18 + 1);

	return knl_channel_remap(mc, chan);
}

/*
 * Render the EDC_ROUTE register in human-readable form.
 * Output string s should be at least KNL_MAX_EDCS*2 bytes.
 */
static void knl_show_edc_route(u32 reg, char *s)
{
	int i;

	for (i = 0; i < KNL_MAX_EDCS; i++) {
		s[i*2] = knl_get_edc_route(i, reg) + '0';
		s[i*2+1] = '-';
	}

	s[KNL_MAX_EDCS*2 - 1] = '\0';
}

/*
 * Render the MC_ROUTE register in human-readable form.
 * Output string s should be at least KNL_MAX_CHANNELS*2 bytes.
 */
static void knl_show_mc_route(u32 reg, char *s)
{
	int i;

	for (i = 0; i < KNL_MAX_CHANNELS; i++) {
		s[i*2] = knl_get_mc_route(i, reg) + '0';
		s[i*2+1] = '-';
	}

	s[KNL_MAX_CHANNELS*2 - 1] = '\0';
}

#define KNL_EDC_ROUTE 0xb8
#define KNL_MC_ROUTE 0xb4

/* Is this dram rule backed by regular DRAM in flat mode? */
#define KNL_EDRAM(reg) GET_BITFIELD(reg, 29, 29)

/* Is this dram rule cached? */
#define KNL_CACHEABLE(reg) GET_BITFIELD(reg, 28, 28)

/* Is this rule backed by edc ? */
#define KNL_EDRAM_ONLY(reg) GET_BITFIELD(reg, 29, 29)

/* Is this rule backed by DRAM, cacheable in EDRAM? */
#define KNL_CACHEABLE(reg) GET_BITFIELD(reg, 28, 28)

/* Is this rule mod3? */
#define KNL_MOD3(reg) GET_BITFIELD(reg, 27, 27)

/*
 * Figure out how big our RAM modules are.
 *
 * The DIMMMTR register in KNL doesn't tell us the size of the DIMMs, so we
 * have to figure this out from the SAD rules, interleave lists, route tables,
 * and TAD rules.
 *
 * SAD rules can have holes in them (e.g. the 3G-4G hole), so we have to
 * inspect the TAD rules to figure out how large the SAD regions really are.
 *
 * When we know the real size of a SAD region and how many ways it's
 * interleaved, we know the individual contribution of each channel to
 * TAD is size/ways.
 *
 * Finally, we have to check whether each channel participates in each SAD
 * region.
 *
 * Fortunately, KNL only supports one DIMM per channel, so once we know how
 * much memory the channel uses, we know the DIMM is at least that large.
 * (The BIOS might possibly choose not to map all available memory, in which
 * case we will underreport the size of the DIMM.)
 *
 * In theory, we could try to determine the EDC sizes as well, but that would
 * only work in flat mode, not in cache mode.
 *
 * @mc_sizes: Output sizes of channels (must have space for KNL_MAX_CHANNELS
 *            elements)
 */
static int knl_get_dimm_capacity(struct sbridge_pvt *pvt, u64 *mc_sizes)
{
	u64 sad_base, sad_size, sad_limit = 0;
	u64 tad_base, tad_size, tad_limit, tad_deadspace, tad_livespace;
	int sad_rule = 0;
	int tad_rule = 0;
	int intrlv_ways, tad_ways;
	u32 first_pkg, pkg;
	int i;
	u64 sad_actual_size[2]; /* sad size accounting for holes, per mc */
	u32 dram_rule, interleave_reg;
	u32 mc_route_reg[KNL_MAX_CHAS];
	u32 edc_route_reg[KNL_MAX_CHAS];
	int edram_only;
	char edc_route_string[KNL_MAX_EDCS*2];
	char mc_route_string[KNL_MAX_CHANNELS*2];
	int cur_reg_start;
	int mc;
	int channel;
	int participants[KNL_MAX_CHANNELS];

	for (i = 0; i < KNL_MAX_CHANNELS; i++)
		mc_sizes[i] = 0;

	/* Read the EDC route table in each CHA. */
	cur_reg_start = 0;
	for (i = 0; i < KNL_MAX_CHAS; i++) {
		pci_read_config_dword(pvt->knl.pci_cha[i],
				KNL_EDC_ROUTE, &edc_route_reg[i]);

		if (i > 0 && edc_route_reg[i] != edc_route_reg[i-1]) {
			knl_show_edc_route(edc_route_reg[i-1],
					edc_route_string);
			if (cur_reg_start == i-1)
				edac_dbg(0, "edc route table for CHA %d: %s\n",
					cur_reg_start, edc_route_string);
			else
				edac_dbg(0, "edc route table for CHA %d-%d: %s\n",
					cur_reg_start, i-1, edc_route_string);
			cur_reg_start = i;
		}
	}
	knl_show_edc_route(edc_route_reg[i-1], edc_route_string);
	if (cur_reg_start == i-1)
		edac_dbg(0, "edc route table for CHA %d: %s\n",
			cur_reg_start, edc_route_string);
	else
		edac_dbg(0, "edc route table for CHA %d-%d: %s\n",
			cur_reg_start, i-1, edc_route_string);

	/* Read the MC route table in each CHA. */
	cur_reg_start = 0;
	for (i = 0; i < KNL_MAX_CHAS; i++) {
		pci_read_config_dword(pvt->knl.pci_cha[i],
			KNL_MC_ROUTE, &mc_route_reg[i]);

		if (i > 0 && mc_route_reg[i] != mc_route_reg[i-1]) {
			knl_show_mc_route(mc_route_reg[i-1], mc_route_string);
			if (cur_reg_start == i-1)
				edac_dbg(0, "mc route table for CHA %d: %s\n",
					cur_reg_start, mc_route_string);
			else
				edac_dbg(0, "mc route table for CHA %d-%d: %s\n",
					cur_reg_start, i-1, mc_route_string);
			cur_reg_start = i;
		}
	}
	knl_show_mc_route(mc_route_reg[i-1], mc_route_string);
	if (cur_reg_start == i-1)
		edac_dbg(0, "mc route table for CHA %d: %s\n",
			cur_reg_start, mc_route_string);
	else
		edac_dbg(0, "mc route table for CHA %d-%d: %s\n",
			cur_reg_start, i-1, mc_route_string);

	/* Process DRAM rules */
	for (sad_rule = 0; sad_rule < pvt->info.max_sad; sad_rule++) {
		/* previous limit becomes the new base */
		sad_base = sad_limit;

		pci_read_config_dword(pvt->pci_sad0,
			pvt->info.dram_rule[sad_rule], &dram_rule);

		if (!DRAM_RULE_ENABLE(dram_rule))
			break;

		edram_only = KNL_EDRAM_ONLY(dram_rule);

		sad_limit = pvt->info.sad_limit(dram_rule)+1;
		sad_size = sad_limit - sad_base;

		pci_read_config_dword(pvt->pci_sad0,
			pvt->info.interleave_list[sad_rule], &interleave_reg);

		/*
		 * Find out how many ways this dram rule is interleaved.
		 * We stop when we see the first channel again.
		 */
		first_pkg = sad_pkg(pvt->info.interleave_pkg,
						interleave_reg, 0);
		for (intrlv_ways = 1; intrlv_ways < 8; intrlv_ways++) {
			pkg = sad_pkg(pvt->info.interleave_pkg,
						interleave_reg, intrlv_ways);

			if ((pkg & 0x8) == 0) {
				/*
				 * 0 bit means memory is non-local,
				 * which KNL doesn't support
				 */
				edac_dbg(0, "Unexpected interleave target %d\n",
					pkg);
				return -1;
			}

			if (pkg == first_pkg)
				break;
		}
		if (KNL_MOD3(dram_rule))
			intrlv_ways *= 3;

		edac_dbg(3, "dram rule %d (base 0x%llx, limit 0x%llx), %d way interleave%s\n",
			sad_rule,
			sad_base,
			sad_limit,
			intrlv_ways,
			edram_only ? ", EDRAM" : "");

		/*
		 * Find out how big the SAD region really is by iterating
		 * over TAD tables (SAD regions may contain holes).
		 * Each memory controller might have a different TAD table, so
		 * we have to look at both.
		 *
		 * Livespace is the memory that's mapped in this TAD table,
		 * deadspace is the holes (this could be the MMIO hole, or it
		 * could be memory that's mapped by the other TAD table but
		 * not this one).
		 */
		for (mc = 0; mc < 2; mc++) {
			sad_actual_size[mc] = 0;
			tad_livespace = 0;
			for (tad_rule = 0;
					tad_rule < ARRAY_SIZE(
						knl_tad_dram_limit_lo);
					tad_rule++) {
				if (knl_get_tad(pvt,
						tad_rule,
						mc,
						&tad_deadspace,
						&tad_limit,
						&tad_ways))
					break;

				tad_size = (tad_limit+1) -
					(tad_livespace + tad_deadspace);
				tad_livespace += tad_size;
				tad_base = (tad_limit+1) - tad_size;

				if (tad_base < sad_base) {
					if (tad_limit > sad_base)
						edac_dbg(0, "TAD region overlaps lower SAD boundary -- TAD tables may be configured incorrectly.\n");
				} else if (tad_base < sad_limit) {
					if (tad_limit+1 > sad_limit) {
						edac_dbg(0, "TAD region overlaps upper SAD boundary -- TAD tables may be configured incorrectly.\n");
					} else {
						/* TAD region is completely inside SAD region */
						edac_dbg(3, "TAD region %d 0x%llx - 0x%llx (%lld bytes) table%d\n",
							tad_rule, tad_base,
							tad_limit, tad_size,
							mc);
						sad_actual_size[mc] += tad_size;
					}
				}
				tad_base = tad_limit+1;
			}
		}

		for (mc = 0; mc < 2; mc++) {
			edac_dbg(3, " total TAD DRAM footprint in table%d : 0x%llx (%lld bytes)\n",
				mc, sad_actual_size[mc], sad_actual_size[mc]);
		}

		/* Ignore EDRAM rule */
		if (edram_only)
			continue;

		/* Figure out which channels participate in interleave. */
		for (channel = 0; channel < KNL_MAX_CHANNELS; channel++)
			participants[channel] = 0;

		/* For each channel, does at least one CHA have
		 * this channel mapped to the given target?
		 */
		for (channel = 0; channel < KNL_MAX_CHANNELS; channel++) {
			int target;
			int cha;

			for (target = 0; target < KNL_MAX_CHANNELS; target++) {
				for (cha = 0; cha < KNL_MAX_CHAS; cha++) {
					if (knl_get_mc_route(target,
						mc_route_reg[cha]) == channel
						&& !participants[channel]) {
						participants[channel] = 1;
						break;
					}
				}
			}
		}

		for (channel = 0; channel < KNL_MAX_CHANNELS; channel++) {
			mc = knl_channel_mc(channel);
			if (participants[channel]) {
				edac_dbg(4, "mc channel %d contributes %lld bytes via sad entry %d\n",
					channel,
					sad_actual_size[mc]/intrlv_ways,
					sad_rule);
				mc_sizes[channel] +=
					sad_actual_size[mc]/intrlv_ways;
			}
		}
	}

	return 0;
}

static void get_source_id(struct mem_ctl_info *mci)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	u32 reg;

	if (pvt->info.type == HASWELL || pvt->info.type == BROADWELL ||
	    pvt->info.type == KNIGHTS_LANDING)
		pci_read_config_dword(pvt->pci_sad1, SAD_TARGET, &reg);
	else
		pci_read_config_dword(pvt->pci_br0, SAD_TARGET, &reg);

	if (pvt->info.type == KNIGHTS_LANDING)
		pvt->sbridge_dev->source_id = SOURCE_ID_KNL(reg);
	else
		pvt->sbridge_dev->source_id = SOURCE_ID(reg);
}

static int __populate_dimms(struct mem_ctl_info *mci,
			    u64 knl_mc_sizes[KNL_MAX_CHANNELS],
			    enum edac_type mode)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	int channels = pvt->info.type == KNIGHTS_LANDING ? KNL_MAX_CHANNELS
							 : NUM_CHANNELS;
	unsigned int i, j, banks, ranks, rows, cols, npages;
	struct dimm_info *dimm;
	enum mem_type mtype;
	u64 size;

	mtype = pvt->info.get_memory_type(pvt);
	if (mtype == MEM_RDDR3 || mtype == MEM_RDDR4)
		edac_dbg(0, "Memory is registered\n");
	else if (mtype == MEM_UNKNOWN)
		edac_dbg(0, "Cannot determine memory type\n");
	else
		edac_dbg(0, "Memory is unregistered\n");

	if (mtype == MEM_DDR4 || mtype == MEM_RDDR4)
		banks = 16;
	else
		banks = 8;

	for (i = 0; i < channels; i++) {
		u32 mtr;

		int max_dimms_per_channel;

		if (pvt->info.type == KNIGHTS_LANDING) {
			max_dimms_per_channel = 1;
			if (!pvt->knl.pci_channel[i])
				continue;
		} else {
			max_dimms_per_channel = ARRAY_SIZE(mtr_regs);
			if (!pvt->pci_tad[i])
				continue;
		}

		for (j = 0; j < max_dimms_per_channel; j++) {
			dimm = EDAC_DIMM_PTR(mci->layers, mci->dimms, mci->n_layers, i, j, 0);
			if (pvt->info.type == KNIGHTS_LANDING) {
				pci_read_config_dword(pvt->knl.pci_channel[i],
					knl_mtr_reg, &mtr);
			} else {
				pci_read_config_dword(pvt->pci_tad[i],
					mtr_regs[j], &mtr);
			}
			edac_dbg(4, "Channel #%d  MTR%d = %x\n", i, j, mtr);
			if (IS_DIMM_PRESENT(mtr)) {
				if (!IS_ECC_ENABLED(pvt->info.mcmtr)) {
					sbridge_printk(KERN_ERR, "CPU SrcID #%d, Ha #%d, Channel #%d has DIMMs, but ECC is disabled\n",
						       pvt->sbridge_dev->source_id,
						       pvt->sbridge_dev->dom, i);
					return -ENODEV;
				}
				pvt->channel[i].dimms++;

				ranks = numrank(pvt->info.type, mtr);

				if (pvt->info.type == KNIGHTS_LANDING) {
					/* For DDR4, this is fixed. */
					cols = 1 << 10;
					rows = knl_mc_sizes[i] /
						((u64) cols * ranks * banks * 8);
				} else {
					rows = numrow(mtr);
					cols = numcol(mtr);
				}

				size = ((u64)rows * cols * banks * ranks) >> (20 - 3);
				npages = MiB_TO_PAGES(size);

				edac_dbg(0, "mc#%d: ha %d channel %d, dimm %d, %lld Mb (%d pages) bank: %d, rank: %d, row: %#x, col: %#x\n",
					 pvt->sbridge_dev->mc, pvt->sbridge_dev->dom, i, j,
					 size, npages,
					 banks, ranks, rows, cols);

				dimm->nr_pages = npages;
				dimm->grain = 32;
				dimm->dtype = pvt->info.get_width(pvt, mtr);
				dimm->mtype = mtype;
				dimm->edac_mode = mode;
				snprintf(dimm->label, sizeof(dimm->label),
						 "CPU_SrcID#%u_Ha#%u_Chan#%u_DIMM#%u",
						 pvt->sbridge_dev->source_id, pvt->sbridge_dev->dom, i, j);
			}
		}
	}

	return 0;
}

static int get_dimm_config(struct mem_ctl_info *mci)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	u64 knl_mc_sizes[KNL_MAX_CHANNELS];
	enum edac_type mode;
	u32 reg;

	pvt->sbridge_dev->node_id = pvt->info.get_node_id(pvt);
	edac_dbg(0, "mc#%d: Node ID: %d, source ID: %d\n",
		 pvt->sbridge_dev->mc,
		 pvt->sbridge_dev->node_id,
		 pvt->sbridge_dev->source_id);

	/* KNL doesn't support mirroring or lockstep,
	 * and is always closed page
	 */
	if (pvt->info.type == KNIGHTS_LANDING) {
		mode = EDAC_S4ECD4ED;
		pvt->mirror_mode = NON_MIRRORING;
		pvt->is_cur_addr_mirrored = false;

		if (knl_get_dimm_capacity(pvt, knl_mc_sizes) != 0)
			return -1;
		if (pci_read_config_dword(pvt->pci_ta, KNL_MCMTR, &pvt->info.mcmtr)) {
			edac_dbg(0, "Failed to read KNL_MCMTR register\n");
			return -ENODEV;
		}
	} else {
		if (pvt->info.type == HASWELL || pvt->info.type == BROADWELL) {
			if (pci_read_config_dword(pvt->pci_ha, HASWELL_HASYSDEFEATURE2, &reg)) {
				edac_dbg(0, "Failed to read HASWELL_HASYSDEFEATURE2 register\n");
				return -ENODEV;
			}
			pvt->is_chan_hash = GET_BITFIELD(reg, 21, 21);
			if (GET_BITFIELD(reg, 28, 28)) {
				pvt->mirror_mode = ADDR_RANGE_MIRRORING;
				edac_dbg(0, "Address range partial memory mirroring is enabled\n");
				goto next;
			}
		}
		if (pci_read_config_dword(pvt->pci_ras, RASENABLES, &reg)) {
			edac_dbg(0, "Failed to read RASENABLES register\n");
			return -ENODEV;
		}
		if (IS_MIRROR_ENABLED(reg)) {
			pvt->mirror_mode = FULL_MIRRORING;
			edac_dbg(0, "Full memory mirroring is enabled\n");
		} else {
			pvt->mirror_mode = NON_MIRRORING;
			edac_dbg(0, "Memory mirroring is disabled\n");
		}

next:
		if (pci_read_config_dword(pvt->pci_ta, MCMTR, &pvt->info.mcmtr)) {
			edac_dbg(0, "Failed to read MCMTR register\n");
			return -ENODEV;
		}
		if (IS_LOCKSTEP_ENABLED(pvt->info.mcmtr)) {
			edac_dbg(0, "Lockstep is enabled\n");
			mode = EDAC_S8ECD8ED;
			pvt->is_lockstep = true;
		} else {
			edac_dbg(0, "Lockstep is disabled\n");
			mode = EDAC_S4ECD4ED;
			pvt->is_lockstep = false;
		}
		if (IS_CLOSE_PG(pvt->info.mcmtr)) {
			edac_dbg(0, "address map is on closed page mode\n");
			pvt->is_close_pg = true;
		} else {
			edac_dbg(0, "address map is on open page mode\n");
			pvt->is_close_pg = false;
		}
	}

	return __populate_dimms(mci, knl_mc_sizes, mode);
}

static void get_memory_layout(const struct mem_ctl_info *mci)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	int i, j, k, n_sads, n_tads, sad_interl;
	u32 reg;
	u64 limit, prv = 0;
	u64 tmp_mb;
	u32 gb, mb;
	u32 rir_way;

	/*
	 * Step 1) Get TOLM/TOHM ranges
	 */

	pvt->tolm = pvt->info.get_tolm(pvt);
	tmp_mb = (1 + pvt->tolm) >> 20;

	gb = div_u64_rem(tmp_mb, 1024, &mb);
	edac_dbg(0, "TOLM: %u.%03u GB (0x%016Lx)\n",
		gb, (mb*1000)/1024, (u64)pvt->tolm);

	/* Address range is already 45:25 */
	pvt->tohm = pvt->info.get_tohm(pvt);
	tmp_mb = (1 + pvt->tohm) >> 20;

	gb = div_u64_rem(tmp_mb, 1024, &mb);
	edac_dbg(0, "TOHM: %u.%03u GB (0x%016Lx)\n",
		gb, (mb*1000)/1024, (u64)pvt->tohm);

	/*
	 * Step 2) Get SAD range and SAD Interleave list
	 * TAD registers contain the interleave wayness. However, it
	 * seems simpler to just discover it indirectly, with the
	 * algorithm bellow.
	 */
	prv = 0;
	for (n_sads = 0; n_sads < pvt->info.max_sad; n_sads++) {
		/* SAD_LIMIT Address range is 45:26 */
		pci_read_config_dword(pvt->pci_sad0, pvt->info.dram_rule[n_sads],
				      &reg);
		limit = pvt->info.sad_limit(reg);

		if (!DRAM_RULE_ENABLE(reg))
			continue;

		if (limit <= prv)
			break;

		tmp_mb = (limit + 1) >> 20;
		gb = div_u64_rem(tmp_mb, 1024, &mb);
		edac_dbg(0, "SAD#%d %s up to %u.%03u GB (0x%016Lx) Interleave: %s reg=0x%08x\n",
			 n_sads,
			 show_dram_attr(pvt->info.dram_attr(reg)),
			 gb, (mb*1000)/1024,
			 ((u64)tmp_mb) << 20L,
			 get_intlv_mode_str(reg, pvt->info.type),
			 reg);
		prv = limit;

		pci_read_config_dword(pvt->pci_sad0, pvt->info.interleave_list[n_sads],
				      &reg);
		sad_interl = sad_pkg(pvt->info.interleave_pkg, reg, 0);
		for (j = 0; j < 8; j++) {
			u32 pkg = sad_pkg(pvt->info.interleave_pkg, reg, j);
			if (j > 0 && sad_interl == pkg)
				break;

			edac_dbg(0, "SAD#%d, interleave #%d: %d\n",
				 n_sads, j, pkg);
		}
	}

	if (pvt->info.type == KNIGHTS_LANDING)
		return;

	/*
	 * Step 3) Get TAD range
	 */
	prv = 0;
	for (n_tads = 0; n_tads < MAX_TAD; n_tads++) {
		pci_read_config_dword(pvt->pci_ha, tad_dram_rule[n_tads], &reg);
		limit = TAD_LIMIT(reg);
		if (limit <= prv)
			break;
		tmp_mb = (limit + 1) >> 20;

		gb = div_u64_rem(tmp_mb, 1024, &mb);
		edac_dbg(0, "TAD#%d: up to %u.%03u GB (0x%016Lx), socket interleave %d, memory interleave %d, TGT: %d, %d, %d, %d, reg=0x%08x\n",
			 n_tads, gb, (mb*1000)/1024,
			 ((u64)tmp_mb) << 20L,
			 (u32)(1 << TAD_SOCK(reg)),
			 (u32)TAD_CH(reg) + 1,
			 (u32)TAD_TGT0(reg),
			 (u32)TAD_TGT1(reg),
			 (u32)TAD_TGT2(reg),
			 (u32)TAD_TGT3(reg),
			 reg);
		prv = limit;
	}

	/*
	 * Step 4) Get TAD offsets, per each channel
	 */
	for (i = 0; i < NUM_CHANNELS; i++) {
		if (!pvt->channel[i].dimms)
			continue;
		for (j = 0; j < n_tads; j++) {
			pci_read_config_dword(pvt->pci_tad[i],
					      tad_ch_nilv_offset[j],
					      &reg);
			tmp_mb = TAD_OFFSET(reg) >> 20;
			gb = div_u64_rem(tmp_mb, 1024, &mb);
			edac_dbg(0, "TAD CH#%d, offset #%d: %u.%03u GB (0x%016Lx), reg=0x%08x\n",
				 i, j,
				 gb, (mb*1000)/1024,
				 ((u64)tmp_mb) << 20L,
				 reg);
		}
	}

	/*
	 * Step 6) Get RIR Wayness/Limit, per each channel
	 */
	for (i = 0; i < NUM_CHANNELS; i++) {
		if (!pvt->channel[i].dimms)
			continue;
		for (j = 0; j < MAX_RIR_RANGES; j++) {
			pci_read_config_dword(pvt->pci_tad[i],
					      rir_way_limit[j],
					      &reg);

			if (!IS_RIR_VALID(reg))
				continue;

			tmp_mb = pvt->info.rir_limit(reg) >> 20;
			rir_way = 1 << RIR_WAY(reg);
			gb = div_u64_rem(tmp_mb, 1024, &mb);
			edac_dbg(0, "CH#%d RIR#%d, limit: %u.%03u GB (0x%016Lx), way: %d, reg=0x%08x\n",
				 i, j,
				 gb, (mb*1000)/1024,
				 ((u64)tmp_mb) << 20L,
				 rir_way,
				 reg);

			for (k = 0; k < rir_way; k++) {
				pci_read_config_dword(pvt->pci_tad[i],
						      rir_offset[j][k],
						      &reg);
				tmp_mb = RIR_OFFSET(pvt->info.type, reg) << 6;

				gb = div_u64_rem(tmp_mb, 1024, &mb);
				edac_dbg(0, "CH#%d RIR#%d INTL#%d, offset %u.%03u GB (0x%016Lx), tgt: %d, reg=0x%08x\n",
					 i, j, k,
					 gb, (mb*1000)/1024,
					 ((u64)tmp_mb) << 20L,
					 (u32)RIR_RNK_TGT(pvt->info.type, reg),
					 reg);
			}
		}
	}
}

static struct mem_ctl_info *get_mci_for_node_id(u8 node_id, u8 ha)
{
	struct sbridge_dev *sbridge_dev;

	list_for_each_entry(sbridge_dev, &sbridge_edac_list, list) {
		if (sbridge_dev->node_id == node_id && sbridge_dev->dom == ha)
			return sbridge_dev->mci;
	}
	return NULL;
}

static int get_memory_error_data(struct mem_ctl_info *mci,
				 u64 addr,
				 u8 *socket, u8 *ha,
				 long *channel_mask,
				 u8 *rank,
				 char **area_type, char *msg)
{
	struct mem_ctl_info	*new_mci;
	struct sbridge_pvt *pvt = mci->pvt_info;
	struct pci_dev		*pci_ha;
	int			n_rir, n_sads, n_tads, sad_way, sck_xch;
	int			sad_interl, idx, base_ch;
	int			interleave_mode, shiftup = 0;
	unsigned		sad_interleave[pvt->info.max_interleave];
	u32			reg, dram_rule;
	u8			ch_way, sck_way, pkg, sad_ha = 0;
	u32			tad_offset;
	u32			rir_way;
	u32			mb, gb;
	u64			ch_addr, offset, limit = 0, prv = 0;


	/*
	 * Step 0) Check if the address is at special memory ranges
	 * The check bellow is probably enough to fill all cases where
	 * the error is not inside a memory, except for the legacy
	 * range (e. g. VGA addresses). It is unlikely, however, that the
	 * memory controller would generate an error on that range.
	 */
	if ((addr > (u64) pvt->tolm) && (addr < (1LL << 32))) {
		sprintf(msg, "Error at TOLM area, on addr 0x%08Lx", addr);
		return -EINVAL;
	}
	if (addr >= (u64)pvt->tohm) {
		sprintf(msg, "Error at MMIOH area, on addr 0x%016Lx", addr);
		return -EINVAL;
	}

	/*
	 * Step 1) Get socket
	 */
	for (n_sads = 0; n_sads < pvt->info.max_sad; n_sads++) {
		pci_read_config_dword(pvt->pci_sad0, pvt->info.dram_rule[n_sads],
				      &reg);

		if (!DRAM_RULE_ENABLE(reg))
			continue;

		limit = pvt->info.sad_limit(reg);
		if (limit <= prv) {
			sprintf(msg, "Can't discover the memory socket");
			return -EINVAL;
		}
		if  (addr <= limit)
			break;
		prv = limit;
	}
	if (n_sads == pvt->info.max_sad) {
		sprintf(msg, "Can't discover the memory socket");
		return -EINVAL;
	}
	dram_rule = reg;
	*area_type = show_dram_attr(pvt->info.dram_attr(dram_rule));
	interleave_mode = pvt->info.interleave_mode(dram_rule);

	pci_read_config_dword(pvt->pci_sad0, pvt->info.interleave_list[n_sads],
			      &reg);

	if (pvt->info.type == SANDY_BRIDGE) {
		sad_interl = sad_pkg(pvt->info.interleave_pkg, reg, 0);
		for (sad_way = 0; sad_way < 8; sad_way++) {
			u32 pkg = sad_pkg(pvt->info.interleave_pkg, reg, sad_way);
			if (sad_way > 0 && sad_interl == pkg)
				break;
			sad_interleave[sad_way] = pkg;
			edac_dbg(0, "SAD interleave #%d: %d\n",
				 sad_way, sad_interleave[sad_way]);
		}
		edac_dbg(0, "mc#%d: Error detected on SAD#%d: address 0x%016Lx < 0x%016Lx, Interleave [%d:6]%s\n",
			 pvt->sbridge_dev->mc,
			 n_sads,
			 addr,
			 limit,
			 sad_way + 7,
			 !interleave_mode ? "" : "XOR[18:16]");
		if (interleave_mode)
			idx = ((addr >> 6) ^ (addr >> 16)) & 7;
		else
			idx = (addr >> 6) & 7;
		switch (sad_way) {
		case 1:
			idx = 0;
			break;
		case 2:
			idx = idx & 1;
			break;
		case 4:
			idx = idx & 3;
			break;
		case 8:
			break;
		default:
			sprintf(msg, "Can't discover socket interleave");
			return -EINVAL;
		}
		*socket = sad_interleave[idx];
		edac_dbg(0, "SAD interleave index: %d (wayness %d) = CPU socket %d\n",
			 idx, sad_way, *socket);
	} else if (pvt->info.type == HASWELL || pvt->info.type == BROADWELL) {
		int bits, a7mode = A7MODE(dram_rule);

		if (a7mode) {
			/* A7 mode swaps P9 with P6 */
			bits = GET_BITFIELD(addr, 7, 8) << 1;
			bits |= GET_BITFIELD(addr, 9, 9);
		} else
			bits = GET_BITFIELD(addr, 6, 8);

		if (interleave_mode == 0) {
			/* interleave mode will XOR {8,7,6} with {18,17,16} */
			idx = GET_BITFIELD(addr, 16, 18);
			idx ^= bits;
		} else
			idx = bits;

		pkg = sad_pkg(pvt->info.interleave_pkg, reg, idx);
		*socket = sad_pkg_socket(pkg);
		sad_ha = sad_pkg_ha(pkg);

		if (a7mode) {
			/* MCChanShiftUpEnable */
			pci_read_config_dword(pvt->pci_ha, HASWELL_HASYSDEFEATURE2, &reg);
			shiftup = GET_BITFIELD(reg, 22, 22);
		}

		edac_dbg(0, "SAD interleave package: %d = CPU socket %d, HA %i, shiftup: %i\n",
			 idx, *socket, sad_ha, shiftup);
	} else {
		/* Ivy Bridge's SAD mode doesn't support XOR interleave mode */
		idx = (addr >> 6) & 7;
		pkg = sad_pkg(pvt->info.interleave_pkg, reg, idx);
		*socket = sad_pkg_socket(pkg);
		sad_ha = sad_pkg_ha(pkg);
		edac_dbg(0, "SAD interleave package: %d = CPU socket %d, HA %d\n",
			 idx, *socket, sad_ha);
	}

	*ha = sad_ha;

	/*
	 * Move to the proper node structure, in order to access the
	 * right PCI registers
	 */
	new_mci = get_mci_for_node_id(*socket, sad_ha);
	if (!new_mci) {
		sprintf(msg, "Struct for socket #%u wasn't initialized",
			*socket);
		return -EINVAL;
	}
	mci = new_mci;
	pvt = mci->pvt_info;

	/*
	 * Step 2) Get memory channel
	 */
	prv = 0;
	pci_ha = pvt->pci_ha;
	for (n_tads = 0; n_tads < MAX_TAD; n_tads++) {
		pci_read_config_dword(pci_ha, tad_dram_rule[n_tads], &reg);
		limit = TAD_LIMIT(reg);
		if (limit <= prv) {
			sprintf(msg, "Can't discover the memory channel");
			return -EINVAL;
		}
		if  (addr <= limit)
			break;
		prv = limit;
	}
	if (n_tads == MAX_TAD) {
		sprintf(msg, "Can't discover the memory channel");
		return -EINVAL;
	}

	ch_way = TAD_CH(reg) + 1;
	sck_way = TAD_SOCK(reg);

	if (ch_way == 3)
		idx = addr >> 6;
	else {
		idx = (addr >> (6 + sck_way + shiftup)) & 0x3;
		if (pvt->is_chan_hash)
			idx = haswell_chan_hash(idx, addr);
	}
	idx = idx % ch_way;

	/*
	 * FIXME: Shouldn't we use CHN_IDX_OFFSET() here, when ch_way == 3 ???
	 */
	switch (idx) {
	case 0:
		base_ch = TAD_TGT0(reg);
		break;
	case 1:
		base_ch = TAD_TGT1(reg);
		break;
	case 2:
		base_ch = TAD_TGT2(reg);
		break;
	case 3:
		base_ch = TAD_TGT3(reg);
		break;
	default:
		sprintf(msg, "Can't discover the TAD target");
		return -EINVAL;
	}
	*channel_mask = 1 << base_ch;

	pci_read_config_dword(pvt->pci_tad[base_ch], tad_ch_nilv_offset[n_tads], &tad_offset);

	if (pvt->mirror_mode == FULL_MIRRORING ||
	    (pvt->mirror_mode == ADDR_RANGE_MIRRORING && n_tads == 0)) {
		*channel_mask |= 1 << ((base_ch + 2) % 4);
		switch(ch_way) {
		case 2:
		case 4:
			sck_xch = (1 << sck_way) * (ch_way >> 1);
			break;
		default:
			sprintf(msg, "Invalid mirror set. Can't decode addr");
			return -EINVAL;
		}

		pvt->is_cur_addr_mirrored = true;
	} else {
		sck_xch = (1 << sck_way) * ch_way;
		pvt->is_cur_addr_mirrored = false;
	}

	if (pvt->is_lockstep)
		*channel_mask |= 1 << ((base_ch + 1) % 4);

	offset = TAD_OFFSET(tad_offset);

	edac_dbg(0, "TAD#%d: address 0x%016Lx < 0x%016Lx, socket interleave %d, channel interleave %d (offset 0x%08Lx), index %d, base ch: %d, ch mask: 0x%02lx\n",
		 n_tads,
		 addr,
		 limit,
		 sck_way,
		 ch_way,
		 offset,
		 idx,
		 base_ch,
		 *channel_mask);

	/* Calculate channel address */
	/* Remove the TAD offset */

	if (offset > addr) {
		sprintf(msg, "Can't calculate ch addr: TAD offset 0x%08Lx is too high for addr 0x%08Lx!",
			offset, addr);
		return -EINVAL;
	}

	ch_addr = addr - offset;
	ch_addr >>= (6 + shiftup);
	ch_addr /= sck_xch;
	ch_addr <<= (6 + shiftup);
	ch_addr |= addr & ((1 << (6 + shiftup)) - 1);

	/*
	 * Step 3) Decode rank
	 */
	for (n_rir = 0; n_rir < MAX_RIR_RANGES; n_rir++) {
		pci_read_config_dword(pvt->pci_tad[base_ch], rir_way_limit[n_rir], &reg);

		if (!IS_RIR_VALID(reg))
			continue;

		limit = pvt->info.rir_limit(reg);
		gb = div_u64_rem(limit >> 20, 1024, &mb);
		edac_dbg(0, "RIR#%d, limit: %u.%03u GB (0x%016Lx), way: %d\n",
			 n_rir,
			 gb, (mb*1000)/1024,
			 limit,
			 1 << RIR_WAY(reg));
		if  (ch_addr <= limit)
			break;
	}
	if (n_rir == MAX_RIR_RANGES) {
		sprintf(msg, "Can't discover the memory rank for ch addr 0x%08Lx",
			ch_addr);
		return -EINVAL;
	}
	rir_way = RIR_WAY(reg);

	if (pvt->is_close_pg)
		idx = (ch_addr >> 6);
	else
		idx = (ch_addr >> 13);	/* FIXME: Datasheet says to shift by 15 */
	idx %= 1 << rir_way;

	pci_read_config_dword(pvt->pci_tad[base_ch], rir_offset[n_rir][idx], &reg);
	*rank = RIR_RNK_TGT(pvt->info.type, reg);

	edac_dbg(0, "RIR#%d: channel address 0x%08Lx < 0x%08Lx, RIR interleave %d, index %d\n",
		 n_rir,
		 ch_addr,
		 limit,
		 rir_way,
		 idx);

	return 0;
}

/****************************************************************************
	Device initialization routines: put/get, init/exit
 ****************************************************************************/

/*
 *	sbridge_put_all_devices	'put' all the devices that we have
 *				reserved via 'get'
 */
static void sbridge_put_devices(struct sbridge_dev *sbridge_dev)
{
	int i;

	edac_dbg(0, "\n");
	for (i = 0; i < sbridge_dev->n_devs; i++) {
		struct pci_dev *pdev = sbridge_dev->pdev[i];
		if (!pdev)
			continue;
		edac_dbg(0, "Removing dev %02x:%02x.%d\n",
			 pdev->bus->number,
			 PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
		pci_dev_put(pdev);
	}
}

static void sbridge_put_all_devices(void)
{
	struct sbridge_dev *sbridge_dev, *tmp;

	list_for_each_entry_safe(sbridge_dev, tmp, &sbridge_edac_list, list) {
		sbridge_put_devices(sbridge_dev);
		free_sbridge_dev(sbridge_dev);
	}
}

static int sbridge_get_onedevice(struct pci_dev **prev,
				 u8 *num_mc,
				 const struct pci_id_table *table,
				 const unsigned devno,
				 const int multi_bus)
{
	struct sbridge_dev *sbridge_dev = NULL;
	const struct pci_id_descr *dev_descr = &table->descr[devno];
	struct pci_dev *pdev = NULL;
	u8 bus = 0;
	int i = 0;

	sbridge_printk(KERN_DEBUG,
		"Seeking for: PCI ID %04x:%04x\n",
		PCI_VENDOR_ID_INTEL, dev_descr->dev_id);

	pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
			      dev_descr->dev_id, *prev);

	if (!pdev) {
		if (*prev) {
			*prev = pdev;
			return 0;
		}

		if (dev_descr->optional)
			return 0;

		/* if the HA wasn't found */
		if (devno == 0)
			return -ENODEV;

		sbridge_printk(KERN_INFO,
			"Device not found: %04x:%04x\n",
			PCI_VENDOR_ID_INTEL, dev_descr->dev_id);

		/* End of list, leave */
		return -ENODEV;
	}
	bus = pdev->bus->number;

next_imc:
	sbridge_dev = get_sbridge_dev(bus, dev_descr->dom, multi_bus, sbridge_dev);
	if (!sbridge_dev) {
		/* If the HA1 wasn't found, don't create EDAC second memory controller */
		if (dev_descr->dom == IMC1 && devno != 1) {
			edac_dbg(0, "Skip IMC1: %04x:%04x (since HA1 was absent)\n",
				 PCI_VENDOR_ID_INTEL, dev_descr->dev_id);
			pci_dev_put(pdev);
			return 0;
		}

		if (dev_descr->dom == SOCK)
			goto out_imc;

		sbridge_dev = alloc_sbridge_dev(bus, dev_descr->dom, table);
		if (!sbridge_dev) {
			pci_dev_put(pdev);
			return -ENOMEM;
		}
		(*num_mc)++;
	}

	if (sbridge_dev->pdev[sbridge_dev->i_devs]) {
		sbridge_printk(KERN_ERR,
			"Duplicated device for %04x:%04x\n",
			PCI_VENDOR_ID_INTEL, dev_descr->dev_id);
		pci_dev_put(pdev);
		return -ENODEV;
	}

	sbridge_dev->pdev[sbridge_dev->i_devs++] = pdev;

	/* pdev belongs to more than one IMC, do extra gets */
	if (++i > 1)
		pci_dev_get(pdev);

	if (dev_descr->dom == SOCK && i < table->n_imcs_per_sock)
		goto next_imc;

out_imc:
	/* Be sure that the device is enabled */
	if (unlikely(pci_enable_device(pdev) < 0)) {
		sbridge_printk(KERN_ERR,
			"Couldn't enable %04x:%04x\n",
			PCI_VENDOR_ID_INTEL, dev_descr->dev_id);
		return -ENODEV;
	}

	edac_dbg(0, "Detected %04x:%04x\n",
		 PCI_VENDOR_ID_INTEL, dev_descr->dev_id);

	/*
	 * As stated on drivers/pci/search.c, the reference count for
	 * @from is always decremented if it is not %NULL. So, as we need
	 * to get all devices up to null, we need to do a get for the device
	 */
	pci_dev_get(pdev);

	*prev = pdev;

	return 0;
}

/*
 * sbridge_get_all_devices - Find and perform 'get' operation on the MCH's
 *			     devices we want to reference for this driver.
 * @num_mc: pointer to the memory controllers count, to be incremented in case
 *	    of success.
 * @table: model specific table
 *
 * returns 0 in case of success or error code
 */
static int sbridge_get_all_devices(u8 *num_mc,
					const struct pci_id_table *table)
{
	int i, rc;
	struct pci_dev *pdev = NULL;
	int allow_dups = 0;
	int multi_bus = 0;

	if (table->type == KNIGHTS_LANDING)
		allow_dups = multi_bus = 1;
	while (table && table->descr) {
		for (i = 0; i < table->n_devs_per_sock; i++) {
			if (!allow_dups || i == 0 ||
					table->descr[i].dev_id !=
						table->descr[i-1].dev_id) {
				pdev = NULL;
			}
			do {
				rc = sbridge_get_onedevice(&pdev, num_mc,
							   table, i, multi_bus);
				if (rc < 0) {
					if (i == 0) {
						i = table->n_devs_per_sock;
						break;
					}
					sbridge_put_all_devices();
					return -ENODEV;
				}
			} while (pdev && !allow_dups);
		}
		table++;
	}

	return 0;
}

/*
 * Device IDs for {SBRIDGE,IBRIDGE,HASWELL,BROADWELL}_IMC_HA0_TAD0 are in
 * the format: XXXa. So we can convert from a device to the corresponding
 * channel like this
 */
#define TAD_DEV_TO_CHAN(dev) (((dev) & 0xf) - 0xa)

static int sbridge_mci_bind_devs(struct mem_ctl_info *mci,
				 struct sbridge_dev *sbridge_dev)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	struct pci_dev *pdev;
	u8 saw_chan_mask = 0;
	int i;

	for (i = 0; i < sbridge_dev->n_devs; i++) {
		pdev = sbridge_dev->pdev[i];
		if (!pdev)
			continue;

		switch (pdev->device) {
		case PCI_DEVICE_ID_INTEL_SBRIDGE_SAD0:
			pvt->pci_sad0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_SAD1:
			pvt->pci_sad1 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_BR:
			pvt->pci_br0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_HA0:
			pvt->pci_ha = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TA:
			pvt->pci_ta = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_RAS:
			pvt->pci_ras = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD0:
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD1:
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD2:
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD3:
		{
			int id = TAD_DEV_TO_CHAN(pdev->device);
			pvt->pci_tad[id] = pdev;
			saw_chan_mask |= 1 << id;
		}
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_DDRIO:
			pvt->pci_ddrio = pdev;
			break;
		default:
			goto error;
		}

		edac_dbg(0, "Associated PCI %02x:%02x, bus %d with dev = %p\n",
			 pdev->vendor, pdev->device,
			 sbridge_dev->bus,
			 pdev);
	}

	/* Check if everything were registered */
	if (!pvt->pci_sad0 || !pvt->pci_sad1 || !pvt->pci_ha ||
	    !pvt->pci_ras || !pvt->pci_ta)
		goto enodev;

	if (saw_chan_mask != 0x0f)
		goto enodev;
	return 0;

enodev:
	sbridge_printk(KERN_ERR, "Some needed devices are missing\n");
	return -ENODEV;

error:
	sbridge_printk(KERN_ERR, "Unexpected device %02x:%02x\n",
		       PCI_VENDOR_ID_INTEL, pdev->device);
	return -EINVAL;
}

static int ibridge_mci_bind_devs(struct mem_ctl_info *mci,
				 struct sbridge_dev *sbridge_dev)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	struct pci_dev *pdev;
	u8 saw_chan_mask = 0;
	int i;

	for (i = 0; i < sbridge_dev->n_devs; i++) {
		pdev = sbridge_dev->pdev[i];
		if (!pdev)
			continue;

		switch (pdev->device) {
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0:
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1:
			pvt->pci_ha = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TA:
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TA:
			pvt->pci_ta = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_RAS:
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_RAS:
			pvt->pci_ras = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD0:
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD1:
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD2:
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD3:
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD0:
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD1:
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD2:
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD3:
		{
			int id = TAD_DEV_TO_CHAN(pdev->device);
			pvt->pci_tad[id] = pdev;
			saw_chan_mask |= 1 << id;
		}
			break;
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_2HA_DDRIO0:
			pvt->pci_ddrio = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_1HA_DDRIO0:
			pvt->pci_ddrio = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_IBRIDGE_SAD:
			pvt->pci_sad0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_IBRIDGE_BR0:
			pvt->pci_br0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_IBRIDGE_BR1:
			pvt->pci_br1 = pdev;
			break;
		default:
			goto error;
		}

		edac_dbg(0, "Associated PCI %02x.%02d.%d with dev = %p\n",
			 sbridge_dev->bus,
			 PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
			 pdev);
	}

	/* Check if everything were registered */
	if (!pvt->pci_sad0 || !pvt->pci_ha || !pvt->pci_br0 ||
	    !pvt->pci_br1 || !pvt->pci_ras || !pvt->pci_ta)
		goto enodev;

	if (saw_chan_mask != 0x0f && /* -EN/-EX */
	    saw_chan_mask != 0x03)   /* -EP */
		goto enodev;
	return 0;

enodev:
	sbridge_printk(KERN_ERR, "Some needed devices are missing\n");
	return -ENODEV;

error:
	sbridge_printk(KERN_ERR,
		       "Unexpected device %02x:%02x\n", PCI_VENDOR_ID_INTEL,
			pdev->device);
	return -EINVAL;
}

static int haswell_mci_bind_devs(struct mem_ctl_info *mci,
				 struct sbridge_dev *sbridge_dev)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	struct pci_dev *pdev;
	u8 saw_chan_mask = 0;
	int i;

	/* there's only one device per system; not tied to any bus */
	if (pvt->info.pci_vtd == NULL)
		/* result will be checked later */
		pvt->info.pci_vtd = pci_get_device(PCI_VENDOR_ID_INTEL,
						   PCI_DEVICE_ID_INTEL_HASWELL_IMC_VTD_MISC,
						   NULL);

	for (i = 0; i < sbridge_dev->n_devs; i++) {
		pdev = sbridge_dev->pdev[i];
		if (!pdev)
			continue;

		switch (pdev->device) {
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_CBO_SAD0:
			pvt->pci_sad0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_CBO_SAD1:
			pvt->pci_sad1 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1:
			pvt->pci_ha = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TA:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TA:
			pvt->pci_ta = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TM:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TM:
			pvt->pci_ras = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD0:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD1:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD2:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD3:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD0:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD1:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD2:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD3:
		{
			int id = TAD_DEV_TO_CHAN(pdev->device);
			pvt->pci_tad[id] = pdev;
			saw_chan_mask |= 1 << id;
		}
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO0:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO1:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO2:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO3:
			if (!pvt->pci_ddrio)
				pvt->pci_ddrio = pdev;
			break;
		default:
			break;
		}

		edac_dbg(0, "Associated PCI %02x.%02d.%d with dev = %p\n",
			 sbridge_dev->bus,
			 PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
			 pdev);
	}

	/* Check if everything were registered */
	if (!pvt->pci_sad0 || !pvt->pci_ha || !pvt->pci_sad1 ||
	    !pvt->pci_ras  || !pvt->pci_ta || !pvt->info.pci_vtd)
		goto enodev;

	if (saw_chan_mask != 0x0f && /* -EN/-EX */
	    saw_chan_mask != 0x03)   /* -EP */
		goto enodev;
	return 0;

enodev:
	sbridge_printk(KERN_ERR, "Some needed devices are missing\n");
	return -ENODEV;
}

static int broadwell_mci_bind_devs(struct mem_ctl_info *mci,
				 struct sbridge_dev *sbridge_dev)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	struct pci_dev *pdev;
	u8 saw_chan_mask = 0;
	int i;

	/* there's only one device per system; not tied to any bus */
	if (pvt->info.pci_vtd == NULL)
		/* result will be checked later */
		pvt->info.pci_vtd = pci_get_device(PCI_VENDOR_ID_INTEL,
						   PCI_DEVICE_ID_INTEL_BROADWELL_IMC_VTD_MISC,
						   NULL);

	for (i = 0; i < sbridge_dev->n_devs; i++) {
		pdev = sbridge_dev->pdev[i];
		if (!pdev)
			continue;

		switch (pdev->device) {
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_CBO_SAD0:
			pvt->pci_sad0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_CBO_SAD1:
			pvt->pci_sad1 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1:
			pvt->pci_ha = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TA:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TA:
			pvt->pci_ta = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TM:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TM:
			pvt->pci_ras = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD0:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD1:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD2:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD3:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD0:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD1:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD2:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD3:
		{
			int id = TAD_DEV_TO_CHAN(pdev->device);
			pvt->pci_tad[id] = pdev;
			saw_chan_mask |= 1 << id;
		}
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_DDRIO0:
			pvt->pci_ddrio = pdev;
			break;
		default:
			break;
		}

		edac_dbg(0, "Associated PCI %02x.%02d.%d with dev = %p\n",
			 sbridge_dev->bus,
			 PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
			 pdev);
	}

	/* Check if everything were registered */
	if (!pvt->pci_sad0 || !pvt->pci_ha || !pvt->pci_sad1 ||
	    !pvt->pci_ras  || !pvt->pci_ta || !pvt->info.pci_vtd)
		goto enodev;

	if (saw_chan_mask != 0x0f && /* -EN/-EX */
	    saw_chan_mask != 0x03)   /* -EP */
		goto enodev;
	return 0;

enodev:
	sbridge_printk(KERN_ERR, "Some needed devices are missing\n");
	return -ENODEV;
}

static int knl_mci_bind_devs(struct mem_ctl_info *mci,
			struct sbridge_dev *sbridge_dev)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	struct pci_dev *pdev;
	int dev, func;

	int i;
	int devidx;

	for (i = 0; i < sbridge_dev->n_devs; i++) {
		pdev = sbridge_dev->pdev[i];
		if (!pdev)
			continue;

		/* Extract PCI device and function. */
		dev = (pdev->devfn >> 3) & 0x1f;
		func = pdev->devfn & 0x7;

		switch (pdev->device) {
		case PCI_DEVICE_ID_INTEL_KNL_IMC_MC:
			if (dev == 8)
				pvt->knl.pci_mc0 = pdev;
			else if (dev == 9)
				pvt->knl.pci_mc1 = pdev;
			else {
				sbridge_printk(KERN_ERR,
					"Memory controller in unexpected place! (dev %d, fn %d)\n",
					dev, func);
				continue;
			}
			break;

		case PCI_DEVICE_ID_INTEL_KNL_IMC_SAD0:
			pvt->pci_sad0 = pdev;
			break;

		case PCI_DEVICE_ID_INTEL_KNL_IMC_SAD1:
			pvt->pci_sad1 = pdev;
			break;

		case PCI_DEVICE_ID_INTEL_KNL_IMC_CHA:
			/* There are one of these per tile, and range from
			 * 1.14.0 to 1.18.5.
			 */
			devidx = ((dev-14)*8)+func;

			if (devidx < 0 || devidx >= KNL_MAX_CHAS) {
				sbridge_printk(KERN_ERR,
					"Caching and Home Agent in unexpected place! (dev %d, fn %d)\n",
					dev, func);
				continue;
			}

			WARN_ON(pvt->knl.pci_cha[devidx] != NULL);

			pvt->knl.pci_cha[devidx] = pdev;
			break;

		case PCI_DEVICE_ID_INTEL_KNL_IMC_CHAN:
			devidx = -1;

			/*
			 *  MC0 channels 0-2 are device 9 function 2-4,
			 *  MC1 channels 3-5 are device 8 function 2-4.
			 */

			if (dev == 9)
				devidx = func-2;
			else if (dev == 8)
				devidx = 3 + (func-2);

			if (devidx < 0 || devidx >= KNL_MAX_CHANNELS) {
				sbridge_printk(KERN_ERR,
					"DRAM Channel Registers in unexpected place! (dev %d, fn %d)\n",
					dev, func);
				continue;
			}

			WARN_ON(pvt->knl.pci_channel[devidx] != NULL);
			pvt->knl.pci_channel[devidx] = pdev;
			break;

		case PCI_DEVICE_ID_INTEL_KNL_IMC_TOLHM:
			pvt->knl.pci_mc_info = pdev;
			break;

		case PCI_DEVICE_ID_INTEL_KNL_IMC_TA:
			pvt->pci_ta = pdev;
			break;

		default:
			sbridge_printk(KERN_ERR, "Unexpected device %d\n",
				pdev->device);
			break;
		}
	}

	if (!pvt->knl.pci_mc0  || !pvt->knl.pci_mc1 ||
	    !pvt->pci_sad0     || !pvt->pci_sad1    ||
	    !pvt->pci_ta) {
		goto enodev;
	}

	for (i = 0; i < KNL_MAX_CHANNELS; i++) {
		if (!pvt->knl.pci_channel[i]) {
			sbridge_printk(KERN_ERR, "Missing channel %d\n", i);
			goto enodev;
		}
	}

	for (i = 0; i < KNL_MAX_CHAS; i++) {
		if (!pvt->knl.pci_cha[i]) {
			sbridge_printk(KERN_ERR, "Missing CHA %d\n", i);
			goto enodev;
		}
	}

	return 0;

enodev:
	sbridge_printk(KERN_ERR, "Some needed devices are missing\n");
	return -ENODEV;
}

/****************************************************************************
			Error check routines
 ****************************************************************************/

/*
 * While Sandy Bridge has error count registers, SMI BIOS read values from
 * and resets the counters. So, they are not reliable for the OS to read
 * from them. So, we have no option but to just trust on whatever MCE is
 * telling us about the errors.
 */
static void sbridge_mce_output_error(struct mem_ctl_info *mci,
				    const struct mce *m)
{
	struct mem_ctl_info *new_mci;
	struct sbridge_pvt *pvt = mci->pvt_info;
	enum hw_event_mc_err_type tp_event;
	char *type, *optype, msg[256];
	bool ripv = GET_BITFIELD(m->mcgstatus, 0, 0);
	bool overflow = GET_BITFIELD(m->status, 62, 62);
	bool uncorrected_error = GET_BITFIELD(m->status, 61, 61);
	bool recoverable;
	u32 core_err_cnt = GET_BITFIELD(m->status, 38, 52);
	u32 mscod = GET_BITFIELD(m->status, 16, 31);
	u32 errcode = GET_BITFIELD(m->status, 0, 15);
	u32 channel = GET_BITFIELD(m->status, 0, 3);
	u32 optypenum = GET_BITFIELD(m->status, 4, 6);
	long channel_mask, first_channel;
	u8  rank, socket, ha;
	int rc, dimm;
	char *area_type = NULL;

	if (pvt->info.type != SANDY_BRIDGE)
		recoverable = true;
	else
		recoverable = GET_BITFIELD(m->status, 56, 56);

	if (uncorrected_error) {
		if (ripv) {
			type = "FATAL";
			tp_event = HW_EVENT_ERR_FATAL;
		} else {
			type = "NON_FATAL";
			tp_event = HW_EVENT_ERR_UNCORRECTED;
		}
	} else {
		type = "CORRECTED";
		tp_event = HW_EVENT_ERR_CORRECTED;
	}

	/*
	 * According with Table 15-9 of the Intel Architecture spec vol 3A,
	 * memory errors should fit in this mask:
	 *	000f 0000 1mmm cccc (binary)
	 * where:
	 *	f = Correction Report Filtering Bit. If 1, subsequent errors
	 *	    won't be shown
	 *	mmm = error type
	 *	cccc = channel
	 * If the mask doesn't match, report an error to the parsing logic
	 */
	if (! ((errcode & 0xef80) == 0x80)) {
		optype = "Can't parse: it is not a mem";
	} else {
		switch (optypenum) {
		case 0:
			optype = "generic undef request error";
			break;
		case 1:
			optype = "memory read error";
			break;
		case 2:
			optype = "memory write error";
			break;
		case 3:
			optype = "addr/cmd error";
			break;
		case 4:
			optype = "memory scrubbing error";
			break;
		default:
			optype = "reserved";
			break;
		}
	}

	/* Only decode errors with an valid address (ADDRV) */
	if (!GET_BITFIELD(m->status, 58, 58))
		return;

	if (pvt->info.type == KNIGHTS_LANDING) {
		if (channel == 14) {
			edac_dbg(0, "%s%s err_code:%04x:%04x EDRAM bank %d\n",
				overflow ? " OVERFLOW" : "",
				(uncorrected_error && recoverable)
				? " recoverable" : "",
				mscod, errcode,
				m->bank);
		} else {
			char A = *("A");

			/*
			 * Reported channel is in range 0-2, so we can't map it
			 * back to mc. To figure out mc we check machine check
			 * bank register that reported this error.
			 * bank15 means mc0 and bank16 means mc1.
			 */
			channel = knl_channel_remap(m->bank == 16, channel);
			channel_mask = 1 << channel;

			snprintf(msg, sizeof(msg),
				"%s%s err_code:%04x:%04x channel:%d (DIMM_%c)",
				overflow ? " OVERFLOW" : "",
				(uncorrected_error && recoverable)
				? " recoverable" : " ",
				mscod, errcode, channel, A + channel);
			edac_mc_handle_error(tp_event, mci, core_err_cnt,
				m->addr >> PAGE_SHIFT, m->addr & ~PAGE_MASK, 0,
				channel, 0, -1,
				optype, msg);
		}
		return;
	} else {
		rc = get_memory_error_data(mci, m->addr, &socket, &ha,
				&channel_mask, &rank, &area_type, msg);
	}

	if (rc < 0)
		goto err_parsing;
	new_mci = get_mci_for_node_id(socket, ha);
	if (!new_mci) {
		strcpy(msg, "Error: socket got corrupted!");
		goto err_parsing;
	}
	mci = new_mci;
	pvt = mci->pvt_info;

	first_channel = find_first_bit(&channel_mask, NUM_CHANNELS);

	if (rank < 4)
		dimm = 0;
	else if (rank < 8)
		dimm = 1;
	else
		dimm = 2;


	/*
	 * FIXME: On some memory configurations (mirror, lockstep), the
	 * Memory Controller can't point the error to a single DIMM. The
	 * EDAC core should be handling the channel mask, in order to point
	 * to the group of dimm's where the error may be happening.
	 */
	if (!pvt->is_lockstep && !pvt->is_cur_addr_mirrored && !pvt->is_close_pg)
		channel = first_channel;

	snprintf(msg, sizeof(msg),
		 "%s%s area:%s err_code:%04x:%04x socket:%d ha:%d channel_mask:%ld rank:%d",
		 overflow ? " OVERFLOW" : "",
		 (uncorrected_error && recoverable) ? " recoverable" : "",
		 area_type,
		 mscod, errcode,
		 socket, ha,
		 channel_mask,
		 rank);

	edac_dbg(0, "%s\n", msg);

	/* FIXME: need support for channel mask */

	if (channel == CHANNEL_UNSPECIFIED)
		channel = -1;

	/* Call the helper to output message */
	edac_mc_handle_error(tp_event, mci, core_err_cnt,
			     m->addr >> PAGE_SHIFT, m->addr & ~PAGE_MASK, 0,
			     channel, dimm, -1,
			     optype, msg);
	return;
err_parsing:
	edac_mc_handle_error(tp_event, mci, core_err_cnt, 0, 0, 0,
			     -1, -1, -1,
			     msg, "");

}

/*
 * Check that logging is enabled and that this is the right type
 * of error for us to handle.
 */
static int sbridge_mce_check_error(struct notifier_block *nb, unsigned long val,
				   void *data)
{
	struct mce *mce = (struct mce *)data;
	struct mem_ctl_info *mci;
	struct sbridge_pvt *pvt;
	char *type;

	if (edac_get_report_status() == EDAC_REPORTING_DISABLED)
		return NOTIFY_DONE;

	mci = get_mci_for_node_id(mce->socketid, IMC0);
	if (!mci)
		return NOTIFY_DONE;
	pvt = mci->pvt_info;

	/*
	 * Just let mcelog handle it if the error is
	 * outside the memory controller. A memory error
	 * is indicated by bit 7 = 1 and bits = 8-11,13-15 = 0.
	 * bit 12 has an special meaning.
	 */
	if ((mce->status & 0xefff) >> 7 != 1)
		return NOTIFY_DONE;

	if (mce->mcgstatus & MCG_STATUS_MCIP)
		type = "Exception";
	else
		type = "Event";

	sbridge_mc_printk(mci, KERN_DEBUG, "HANDLING MCE MEMORY ERROR\n");

	sbridge_mc_printk(mci, KERN_DEBUG, "CPU %d: Machine Check %s: %Lx "
			  "Bank %d: %016Lx\n", mce->extcpu, type,
			  mce->mcgstatus, mce->bank, mce->status);
	sbridge_mc_printk(mci, KERN_DEBUG, "TSC %llx ", mce->tsc);
	sbridge_mc_printk(mci, KERN_DEBUG, "ADDR %llx ", mce->addr);
	sbridge_mc_printk(mci, KERN_DEBUG, "MISC %llx ", mce->misc);

	sbridge_mc_printk(mci, KERN_DEBUG, "PROCESSOR %u:%x TIME %llu SOCKET "
			  "%u APIC %x\n", mce->cpuvendor, mce->cpuid,
			  mce->time, mce->socketid, mce->apicid);

	sbridge_mce_output_error(mci, mce);

	/* Advice mcelog that the error were handled */
	return NOTIFY_STOP;
}

static struct notifier_block sbridge_mce_dec = {
	.notifier_call	= sbridge_mce_check_error,
	.priority	= MCE_PRIO_EDAC,
};

/****************************************************************************
			EDAC register/unregister logic
 ****************************************************************************/

static void sbridge_unregister_mci(struct sbridge_dev *sbridge_dev)
{
	struct mem_ctl_info *mci = sbridge_dev->mci;
	struct sbridge_pvt *pvt;

	if (unlikely(!mci || !mci->pvt_info)) {
		edac_dbg(0, "MC: dev = %p\n", &sbridge_dev->pdev[0]->dev);

		sbridge_printk(KERN_ERR, "Couldn't find mci handler\n");
		return;
	}

	pvt = mci->pvt_info;

	edac_dbg(0, "MC: mci = %p, dev = %p\n",
		 mci, &sbridge_dev->pdev[0]->dev);

	/* Remove MC sysfs nodes */
	edac_mc_del_mc(mci->pdev);

	edac_dbg(1, "%s: free mci struct\n", mci->ctl_name);
	kfree(mci->ctl_name);
	edac_mc_free(mci);
	sbridge_dev->mci = NULL;
}

static int sbridge_register_mci(struct sbridge_dev *sbridge_dev, enum type type)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct sbridge_pvt *pvt;
	struct pci_dev *pdev = sbridge_dev->pdev[0];
	int rc;

	/* allocate a new MC control structure */
	layers[0].type = EDAC_MC_LAYER_CHANNEL;
	layers[0].size = type == KNIGHTS_LANDING ?
		KNL_MAX_CHANNELS : NUM_CHANNELS;
	layers[0].is_virt_csrow = false;
	layers[1].type = EDAC_MC_LAYER_SLOT;
	layers[1].size = type == KNIGHTS_LANDING ? 1 : MAX_DIMMS;
	layers[1].is_virt_csrow = true;
	mci = edac_mc_alloc(sbridge_dev->mc, ARRAY_SIZE(layers), layers,
			    sizeof(*pvt));

	if (unlikely(!mci))
		return -ENOMEM;

	edac_dbg(0, "MC: mci = %p, dev = %p\n",
		 mci, &pdev->dev);

	pvt = mci->pvt_info;
	memset(pvt, 0, sizeof(*pvt));

	/* Associate sbridge_dev and mci for future usage */
	pvt->sbridge_dev = sbridge_dev;
	sbridge_dev->mci = mci;

	mci->mtype_cap = type == KNIGHTS_LANDING ?
		MEM_FLAG_DDR4 : MEM_FLAG_DDR3;
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = EDAC_MOD_STR;
	mci->dev_name = pci_name(pdev);
	mci->ctl_page_to_phys = NULL;

	pvt->info.type = type;
	switch (type) {
	case IVY_BRIDGE:
		pvt->info.rankcfgr = IB_RANK_CFG_A;
		pvt->info.get_tolm = ibridge_get_tolm;
		pvt->info.get_tohm = ibridge_get_tohm;
		pvt->info.dram_rule = ibridge_dram_rule;
		pvt->info.get_memory_type = get_memory_type;
		pvt->info.get_node_id = get_node_id;
		pvt->info.rir_limit = rir_limit;
		pvt->info.sad_limit = sad_limit;
		pvt->info.interleave_mode = interleave_mode;
		pvt->info.dram_attr = dram_attr;
		pvt->info.max_sad = ARRAY_SIZE(ibridge_dram_rule);
		pvt->info.interleave_list = ibridge_interleave_list;
		pvt->info.max_interleave = ARRAY_SIZE(ibridge_interleave_list);
		pvt->info.interleave_pkg = ibridge_interleave_pkg;
		pvt->info.get_width = ibridge_get_width;

		/* Store pci devices at mci for faster access */
		rc = ibridge_mci_bind_devs(mci, sbridge_dev);
		if (unlikely(rc < 0))
			goto fail0;
		get_source_id(mci);
		mci->ctl_name = kasprintf(GFP_KERNEL, "Ivy Bridge SrcID#%d_Ha#%d",
			pvt->sbridge_dev->source_id, pvt->sbridge_dev->dom);
		break;
	case SANDY_BRIDGE:
		pvt->info.rankcfgr = SB_RANK_CFG_A;
		pvt->info.get_tolm = sbridge_get_tolm;
		pvt->info.get_tohm = sbridge_get_tohm;
		pvt->info.dram_rule = sbridge_dram_rule;
		pvt->info.get_memory_type = get_memory_type;
		pvt->info.get_node_id = get_node_id;
		pvt->info.rir_limit = rir_limit;
		pvt->info.sad_limit = sad_limit;
		pvt->info.interleave_mode = interleave_mode;
		pvt->info.dram_attr = dram_attr;
		pvt->info.max_sad = ARRAY_SIZE(sbridge_dram_rule);
		pvt->info.interleave_list = sbridge_interleave_list;
		pvt->info.max_interleave = ARRAY_SIZE(sbridge_interleave_list);
		pvt->info.interleave_pkg = sbridge_interleave_pkg;
		pvt->info.get_width = sbridge_get_width;

		/* Store pci devices at mci for faster access */
		rc = sbridge_mci_bind_devs(mci, sbridge_dev);
		if (unlikely(rc < 0))
			goto fail0;
		get_source_id(mci);
		mci->ctl_name = kasprintf(GFP_KERNEL, "Sandy Bridge SrcID#%d_Ha#%d",
			pvt->sbridge_dev->source_id, pvt->sbridge_dev->dom);
		break;
	case HASWELL:
		/* rankcfgr isn't used */
		pvt->info.get_tolm = haswell_get_tolm;
		pvt->info.get_tohm = haswell_get_tohm;
		pvt->info.dram_rule = ibridge_dram_rule;
		pvt->info.get_memory_type = haswell_get_memory_type;
		pvt->info.get_node_id = haswell_get_node_id;
		pvt->info.rir_limit = haswell_rir_limit;
		pvt->info.sad_limit = sad_limit;
		pvt->info.interleave_mode = interleave_mode;
		pvt->info.dram_attr = dram_attr;
		pvt->info.max_sad = ARRAY_SIZE(ibridge_dram_rule);
		pvt->info.interleave_list = ibridge_interleave_list;
		pvt->info.max_interleave = ARRAY_SIZE(ibridge_interleave_list);
		pvt->info.interleave_pkg = ibridge_interleave_pkg;
		pvt->info.get_width = ibridge_get_width;

		/* Store pci devices at mci for faster access */
		rc = haswell_mci_bind_devs(mci, sbridge_dev);
		if (unlikely(rc < 0))
			goto fail0;
		get_source_id(mci);
		mci->ctl_name = kasprintf(GFP_KERNEL, "Haswell SrcID#%d_Ha#%d",
			pvt->sbridge_dev->source_id, pvt->sbridge_dev->dom);
		break;
	case BROADWELL:
		/* rankcfgr isn't used */
		pvt->info.get_tolm = haswell_get_tolm;
		pvt->info.get_tohm = haswell_get_tohm;
		pvt->info.dram_rule = ibridge_dram_rule;
		pvt->info.get_memory_type = haswell_get_memory_type;
		pvt->info.get_node_id = haswell_get_node_id;
		pvt->info.rir_limit = haswell_rir_limit;
		pvt->info.sad_limit = sad_limit;
		pvt->info.interleave_mode = interleave_mode;
		pvt->info.dram_attr = dram_attr;
		pvt->info.max_sad = ARRAY_SIZE(ibridge_dram_rule);
		pvt->info.interleave_list = ibridge_interleave_list;
		pvt->info.max_interleave = ARRAY_SIZE(ibridge_interleave_list);
		pvt->info.interleave_pkg = ibridge_interleave_pkg;
		pvt->info.get_width = broadwell_get_width;

		/* Store pci devices at mci for faster access */
		rc = broadwell_mci_bind_devs(mci, sbridge_dev);
		if (unlikely(rc < 0))
			goto fail0;
		get_source_id(mci);
		mci->ctl_name = kasprintf(GFP_KERNEL, "Broadwell SrcID#%d_Ha#%d",
			pvt->sbridge_dev->source_id, pvt->sbridge_dev->dom);
		break;
	case KNIGHTS_LANDING:
		/* pvt->info.rankcfgr == ??? */
		pvt->info.get_tolm = knl_get_tolm;
		pvt->info.get_tohm = knl_get_tohm;
		pvt->info.dram_rule = knl_dram_rule;
		pvt->info.get_memory_type = knl_get_memory_type;
		pvt->info.get_node_id = knl_get_node_id;
		pvt->info.rir_limit = NULL;
		pvt->info.sad_limit = knl_sad_limit;
		pvt->info.interleave_mode = knl_interleave_mode;
		pvt->info.dram_attr = dram_attr_knl;
		pvt->info.max_sad = ARRAY_SIZE(knl_dram_rule);
		pvt->info.interleave_list = knl_interleave_list;
		pvt->info.max_interleave = ARRAY_SIZE(knl_interleave_list);
		pvt->info.interleave_pkg = ibridge_interleave_pkg;
		pvt->info.get_width = knl_get_width;

		rc = knl_mci_bind_devs(mci, sbridge_dev);
		if (unlikely(rc < 0))
			goto fail0;
		get_source_id(mci);
		mci->ctl_name = kasprintf(GFP_KERNEL, "Knights Landing SrcID#%d_Ha#%d",
			pvt->sbridge_dev->source_id, pvt->sbridge_dev->dom);
		break;
	}

	if (!mci->ctl_name) {
		rc = -ENOMEM;
		goto fail0;
	}

	/* Get dimm basic config and the memory layout */
	rc = get_dimm_config(mci);
	if (rc < 0) {
		edac_dbg(0, "MC: failed to get_dimm_config()\n");
		goto fail;
	}
	get_memory_layout(mci);

	/* record ptr to the generic device */
	mci->pdev = &pdev->dev;

	/* add this new MC control structure to EDAC's list of MCs */
	if (unlikely(edac_mc_add_mc(mci))) {
		edac_dbg(0, "MC: failed edac_mc_add_mc()\n");
		rc = -EINVAL;
		goto fail;
	}

	return 0;

fail:
	kfree(mci->ctl_name);
fail0:
	edac_mc_free(mci);
	sbridge_dev->mci = NULL;
	return rc;
}

#define ICPU(model, table) \
	{ X86_VENDOR_INTEL, 6, model, 0, (unsigned long)&table }

static const struct x86_cpu_id sbridge_cpuids[] = {
	ICPU(INTEL_FAM6_SANDYBRIDGE_X,	  pci_dev_descr_sbridge_table),
	ICPU(INTEL_FAM6_IVYBRIDGE_X,	  pci_dev_descr_ibridge_table),
	ICPU(INTEL_FAM6_HASWELL_X,	  pci_dev_descr_haswell_table),
	ICPU(INTEL_FAM6_BROADWELL_X,	  pci_dev_descr_broadwell_table),
	ICPU(INTEL_FAM6_BROADWELL_XEON_D, pci_dev_descr_broadwell_table),
	ICPU(INTEL_FAM6_XEON_PHI_KNL,	  pci_dev_descr_knl_table),
	ICPU(INTEL_FAM6_XEON_PHI_KNM,	  pci_dev_descr_knl_table),
	{ }
};
MODULE_DEVICE_TABLE(x86cpu, sbridge_cpuids);

/*
 *	sbridge_probe	Get all devices and register memory controllers
 *			present.
 *	return:
 *		0 for FOUND a device
 *		< 0 for error code
 */

static int sbridge_probe(const struct x86_cpu_id *id)
{
	int rc = -ENODEV;
	u8 mc, num_mc = 0;
	struct sbridge_dev *sbridge_dev;
	struct pci_id_table *ptable = (struct pci_id_table *)id->driver_data;

	/* get the pci devices we want to reserve for our use */
	rc = sbridge_get_all_devices(&num_mc, ptable);

	if (unlikely(rc < 0)) {
		edac_dbg(0, "couldn't get all devices\n");
		goto fail0;
	}

	mc = 0;

	list_for_each_entry(sbridge_dev, &sbridge_edac_list, list) {
		edac_dbg(0, "Registering MC#%d (%d of %d)\n",
			 mc, mc + 1, num_mc);

		sbridge_dev->mc = mc++;
		rc = sbridge_register_mci(sbridge_dev, ptable->type);
		if (unlikely(rc < 0))
			goto fail1;
	}

	sbridge_printk(KERN_INFO, "%s\n", SBRIDGE_REVISION);

	return 0;

fail1:
	list_for_each_entry(sbridge_dev, &sbridge_edac_list, list)
		sbridge_unregister_mci(sbridge_dev);

	sbridge_put_all_devices();
fail0:
	return rc;
}

/*
 *	sbridge_remove	cleanup
 *
 */
static void sbridge_remove(void)
{
	struct sbridge_dev *sbridge_dev;

	edac_dbg(0, "\n");

	list_for_each_entry(sbridge_dev, &sbridge_edac_list, list)
		sbridge_unregister_mci(sbridge_dev);

	/* Release PCI resources */
	sbridge_put_all_devices();
}

/*
 *	sbridge_init		Module entry function
 *			Try to initialize this module for its devices
 */
static int __init sbridge_init(void)
{
	const struct x86_cpu_id *id;
	const char *owner;
	int rc;

	edac_dbg(2, "\n");

	owner = edac_get_owner();
	if (owner && strncmp(owner, EDAC_MOD_STR, sizeof(EDAC_MOD_STR)))
		return -EBUSY;

	id = x86_match_cpu(sbridge_cpuids);
	if (!id)
		return -ENODEV;

	/* Ensure that the OPSTATE is set correctly for POLL or NMI */
	opstate_init();

	rc = sbridge_probe(id);

	if (rc >= 0) {
		mce_register_decode_chain(&sbridge_mce_dec);
		if (edac_get_report_status() == EDAC_REPORTING_DISABLED)
			sbridge_printk(KERN_WARNING, "Loading driver, error reporting disabled.\n");
		return 0;
	}

	sbridge_printk(KERN_ERR, "Failed to register device with error %d.\n",
		      rc);

	return rc;
}

/*
 *	sbridge_exit()	Module exit function
 *			Unregister the driver
 */
static void __exit sbridge_exit(void)
{
	edac_dbg(2, "\n");
	sbridge_remove();
	mce_unregister_decode_chain(&sbridge_mce_dec);
}

module_init(sbridge_init);
module_exit(sbridge_exit);

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_AUTHOR("Red Hat Inc. (http://www.redhat.com)");
MODULE_DESCRIPTION("MC Driver for Intel Sandy Bridge and Ivy Bridge memory controllers - "
		   SBRIDGE_REVISION);
