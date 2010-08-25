/*
 * Intel 7300 class Memory Controllers kernel module (Clarksboro)
 *
 * This file may be distributed under the terms of the
 * GNU General Public License version 2 only.
 *
 * Copyright (c) 2010 by:
 *	 Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * Red Hat Inc. http://www.redhat.com
 *
 * Intel 7300 Chipset Memory Controller Hub (MCH) - Datasheet
 *	http://www.intel.com/Assets/PDF/datasheet/318082.pdf
 *
 * TODO: The chipset allow checking for PCI Express errors also. Currently,
 *	 the driver covers only memory error errors
 *
 * This driver uses "csrows" EDAC attribute to represent DIMM slot#
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/edac.h>
#include <linux/mmzone.h>

#include "edac_core.h"

/*
 * Alter this version for the I7300 module when modifications are made
 */
#define I7300_REVISION    " Ver: 1.0.0 " __DATE__

#define EDAC_MOD_STR      "i7300_edac"

#define i7300_printk(level, fmt, arg...) \
	edac_printk(level, "i7300", fmt, ##arg)

#define i7300_mc_printk(mci, level, fmt, arg...) \
	edac_mc_chipset_printk(mci, level, "i7300", fmt, ##arg)

/*
 * Memory topology is organized as:
 *	Branch 0 - 2 channels: channels 0 and 1 (FDB0 PCI dev 21.0)
 *	Branch 1 - 2 channels: channels 2 and 3 (FDB1 PCI dev 22.0)
 * Each channel can have to 8 DIMM sets (called as SLOTS)
 * Slots should generally be filled in pairs
 *	Except on Single Channel mode of operation
 *		just slot 0/channel0 filled on this mode
 *	On normal operation mode, the two channels on a branch should be
		filled together for the same SLOT#
 * When in mirrored mode, Branch 1 replicate memory at Branch 0, so, the four
 *		channels on both branches should be filled
 */

/* Limits for i7300 */
#define MAX_SLOTS		8
#define MAX_BRANCHES		2
#define MAX_CH_PER_BRANCH	2
#define MAX_CHANNELS		(MAX_CH_PER_BRANCH * MAX_BRANCHES)
#define MAX_MIR			3

#define to_channel(ch, branch)	((((branch)) << 1) | (ch))

#define to_csrow(slot, ch, branch)					\
		(to_channel(ch, branch) | ((slot) << 2))


/* Device 16,
 * Function 0: System Address (not documented)
 * Function 1: Memory Branch Map, Control, Errors Register
 * Function 2: FSB Error Registers
 *
 * All 3 functions of Device 16 (0,1,2) share the SAME DID and
 * uses PCI_DEVICE_ID_INTEL_I7300_MCH_ERR for device 16 (0,1,2),
 * PCI_DEVICE_ID_INTEL_I7300_MCH_FB0 and PCI_DEVICE_ID_INTEL_I7300_MCH_FB1
 * for device 21 (0,1).
 */

	/* OFFSETS for Function 0 */
#define		AMBASE			0x48 /* AMB Mem Mapped Reg Region Base */
#define		MAXCH			0x56 /* Max Channel Number */
#define		MAXDIMMPERCH		0x57 /* Max DIMM PER Channel Number */

	/* OFFSETS for Function 1 */
#define		TOLM			0x6C
#define		REDMEMB			0x7C

#define		MIR0			0x80
#define		MIR1			0x84
#define		MIR2			0x88

#if 0
#define		AMIR0			0x8c
#define		AMIR1			0x90
#define		AMIR2			0x94

/*TODO: double check it */
#define			REC_ECC_LOCATOR_ODD(x)	((x) & 0x3fe00) /* bits [17:9] indicate ODD, [8:0]  indicate EVEN */

	/* Fatal error registers */
#define		FERR_FAT_FBD		0x98

/*TODO: double check it */
#define			FERR_FAT_FBDCHAN (3<<28)	/* channel index where the highest-order error occurred */

#define		NERR_FAT_FBD		0x9c
#define		FERR_NF_FBD		0xa0

	/* Non-fatal error register */
#define		NERR_NF_FBD		0xa4

	/* Enable error mask */
#define		EMASK_FBD		0xa8

#define		ERR0_FBD		0xac
#define		ERR1_FBD		0xb0
#define		ERR2_FBD		0xb4
#define		MCERR_FBD		0xb8

#endif

/* TODO: Dev 16 fn1 allows memory error injection - offsets 0x100-0x10b */

	/* TODO: OFFSETS for Device 16 Function 2 */

/*
 * Device 21,
 * Function 0: Memory Map Branch 0
 *
 * Device 22,
 * Function 0: Memory Map Branch 1
 */

	/* OFFSETS for Function 0 */

/*
 * Note: Other Intel EDAC drivers use AMBPRESENT to identify if the available
 * memory. From datasheet item 7.3.1 (FB-DIMM technology & organization), it
 * seems that we cannot use this information directly for the same usage.
 * Each memory slot may have up to 2 AMB interfaces, one for income and another
 * for outcome interface to the next slot.
 * For now, the driver just stores the AMB present registers, but rely only at
 * the MTR info to detect memory.
 * Datasheet is also not clear about how to map each AMBPRESENT registers to
 * one of the 4 available channels.
 */
#define AMBPRESENT_0	0x64
#define AMBPRESENT_1	0x66

const static u16 mtr_regs [MAX_SLOTS] = {
	0x80, 0x84, 0x88, 0x8c,
	0x82, 0x86, 0x8a, 0x8e
};

/* Defines to extract the vaious fields from the
 *	MTRx - Memory Technology Registers
 */
#define MTR_DIMMS_PRESENT(mtr)		((mtr) & (1 << 8))
#define MTR_DIMMS_ETHROTTLE(mtr)	((mtr) & (1 << 7))
#define MTR_DRAM_WIDTH(mtr)		(((mtr) & (1 << 6)) ? 8 : 4)
#define MTR_DRAM_BANKS(mtr)		(((mtr) & (1 << 5)) ? 8 : 4)
#define MTR_DIMM_RANKS(mtr)		(((mtr) & (1 << 4)) ? 1 : 0)
#define MTR_DIMM_ROWS(mtr)		(((mtr) >> 2) & 0x3)
#define MTR_DRAM_BANKS_ADDR_BITS	2
#define MTR_DIMM_ROWS_ADDR_BITS(mtr)	(MTR_DIMM_ROWS(mtr) + 13)
#define MTR_DIMM_COLS(mtr)		((mtr) & 0x3)
#define MTR_DIMM_COLS_ADDR_BITS(mtr)	(MTR_DIMM_COLS(mtr) + 10)

#if 0
	/* OFFSETS for Function 1 */

/* TODO */
#define NRECFGLOG		0x74
#define RECFGLOG		0x78
#define NRECMEMA		0xbe
#define NRECMEMB		0xc0
#define NRECFB_DIMMA		0xc4
#define NRECFB_DIMMB		0xc8
#define NRECFB_DIMMC		0xcc
#define NRECFB_DIMMD		0xd0
#define NRECFB_DIMME		0xd4
#define NRECFB_DIMMF		0xd8
#define REDMEMA			0xdC
#define RECMEMA			0xf0
#define RECMEMB			0xf4
#define RECFB_DIMMA		0xf8
#define RECFB_DIMMB		0xec
#define RECFB_DIMMC		0xf0
#define RECFB_DIMMD		0xf4
#define RECFB_DIMME		0xf8
#define RECFB_DIMMF		0xfC

/* This applies to FERR_NF_FB-DIMM as well as FERR_FAT_FB-DIMM */
static inline int extract_fbdchan_indx(u32 x)
{
	return (x>>28) & 0x3;
}
#endif

#ifdef CONFIG_EDAC_DEBUG
/* MTR NUMROW */
static const char *numrow_toString[] = {
	"8,192 - 13 rows",
	"16,384 - 14 rows",
	"32,768 - 15 rows",
	"65,536 - 16 rows"
};

/* MTR NUMCOL */
static const char *numcol_toString[] = {
	"1,024 - 10 columns",
	"2,048 - 11 columns",
	"4,096 - 12 columns",
	"reserved"
};
#endif

#if 0

/*
 * Error indicator bits and masks
 * Error masks are according with Table 5-17 of i7300 datasheet
 */

enum error_mask {
	EMASK_M1  = 1<<0,  /* Memory Write error on non-redundant retry */
	EMASK_M2  = 1<<1,  /* Memory or FB-DIMM configuration CRC read error */
	EMASK_M3  = 1<<2,  /* Reserved */
	EMASK_M4  = 1<<3,  /* Uncorrectable Data ECC on Replay */
	EMASK_M5  = 1<<4,  /* Aliased Uncorrectable Non-Mirrored Demand Data ECC */
	EMASK_M6  = 1<<5,  /* Unsupported on i7300 */
	EMASK_M7  = 1<<6,  /* Aliased Uncorrectable Resilver- or Spare-Copy Data ECC */
	EMASK_M8  = 1<<7,  /* Aliased Uncorrectable Patrol Data ECC */
	EMASK_M9  = 1<<8,  /* Non-Aliased Uncorrectable Non-Mirrored Demand Data ECC */
	EMASK_M10 = 1<<9,  /* Unsupported on i7300 */
	EMASK_M11 = 1<<10, /* Non-Aliased Uncorrectable Resilver- or Spare-Copy Data ECC  */
	EMASK_M12 = 1<<11, /* Non-Aliased Uncorrectable Patrol Data ECC */
	EMASK_M13 = 1<<12, /* Memory Write error on first attempt */
	EMASK_M14 = 1<<13, /* FB-DIMM Configuration Write error on first attempt */
	EMASK_M15 = 1<<14, /* Memory or FB-DIMM configuration CRC read error */
	EMASK_M16 = 1<<15, /* Channel Failed-Over Occurred */
	EMASK_M17 = 1<<16, /* Correctable Non-Mirrored Demand Data ECC */
	EMASK_M18 = 1<<17, /* Unsupported on i7300 */
	EMASK_M19 = 1<<18, /* Correctable Resilver- or Spare-Copy Data ECC */
	EMASK_M20 = 1<<19, /* Correctable Patrol Data ECC */
	EMASK_M21 = 1<<20, /* FB-DIMM Northbound parity error on FB-DIMM Sync Status */
	EMASK_M22 = 1<<21, /* SPD protocol Error */
	EMASK_M23 = 1<<22, /* Non-Redundant Fast Reset Timeout */
	EMASK_M24 = 1<<23, /* Refresh error */
	EMASK_M25 = 1<<24, /* Memory Write error on redundant retry */
	EMASK_M26 = 1<<25, /* Redundant Fast Reset Timeout */
	EMASK_M27 = 1<<26, /* Correctable Counter Threshold Exceeded */
	EMASK_M28 = 1<<27, /* DIMM-Spare Copy Completed */
	EMASK_M29 = 1<<28, /* DIMM-Isolation Completed */
};

/*
 * Names to translate bit error into something useful
 */
static const char *error_name[] = {
	[0]  = "Memory Write error on non-redundant retry",
	[1]  = "Memory or FB-DIMM configuration CRC read error",
	/* Reserved */
	[3]  = "Uncorrectable Data ECC on Replay",
	[4]  = "Aliased Uncorrectable Non-Mirrored Demand Data ECC",
	/* M6 Unsupported on i7300 */
	[6]  = "Aliased Uncorrectable Resilver- or Spare-Copy Data ECC",
	[7]  = "Aliased Uncorrectable Patrol Data ECC",
	[8]  = "Non-Aliased Uncorrectable Non-Mirrored Demand Data ECC",
	/* M10 Unsupported on i7300 */
	[10] = "Non-Aliased Uncorrectable Resilver- or Spare-Copy Data ECC",
	[11] = "Non-Aliased Uncorrectable Patrol Data ECC",
	[12] = "Memory Write error on first attempt",
	[13] = "FB-DIMM Configuration Write error on first attempt",
	[14] = "Memory or FB-DIMM configuration CRC read error",
	[15] = "Channel Failed-Over Occurred",
	[16] = "Correctable Non-Mirrored Demand Data ECC",
	/* M18 Unsupported on i7300 */
	[18] = "Correctable Resilver- or Spare-Copy Data ECC",
	[19] = "Correctable Patrol Data ECC",
	[20] = "FB-DIMM Northbound parity error on FB-DIMM Sync Status",
	[21] = "SPD protocol Error",
	[22] = "Non-Redundant Fast Reset Timeout",
	[23] = "Refresh error",
	[24] = "Memory Write error on redundant retry",
	[25] = "Redundant Fast Reset Timeout",
	[26] = "Correctable Counter Threshold Exceeded",
	[27] = "DIMM-Spare Copy Completed",
	[28] = "DIMM-Isolation Completed",
};

/* Fatal errors */
#define ERROR_FAT_MASK		(EMASK_M1 | \
				 EMASK_M2 | \
				 EMASK_M23)

/* Correctable errors */
#define ERROR_NF_CORRECTABLE	(EMASK_M27 | \
				 EMASK_M20 | \
				 EMASK_M19 | \
				 EMASK_M18 | \
				 EMASK_M17 | \
				 EMASK_M16)
#define ERROR_NF_DIMM_SPARE	(EMASK_M29 | \
				 EMASK_M28)
#define ERROR_NF_SPD_PROTOCOL	(EMASK_M22)
#define ERROR_NF_NORTH_CRC	(EMASK_M21)

/* Recoverable errors */
#define ERROR_NF_RECOVERABLE	(EMASK_M26 | \
				 EMASK_M25 | \
				 EMASK_M24 | \
				 EMASK_M15 | \
				 EMASK_M14 | \
				 EMASK_M13 | \
				 EMASK_M12 | \
				 EMASK_M11 | \
				 EMASK_M9  | \
				 EMASK_M8  | \
				 EMASK_M7  | \
				 EMASK_M5)

/* uncorrectable errors */
#define ERROR_NF_UNCORRECTABLE	(EMASK_M4)

/* mask to all non-fatal errors */
#define ERROR_NF_MASK		(ERROR_NF_CORRECTABLE   | \
				 ERROR_NF_UNCORRECTABLE | \
				 ERROR_NF_RECOVERABLE   | \
				 ERROR_NF_DIMM_SPARE    | \
				 ERROR_NF_SPD_PROTOCOL  | \
				 ERROR_NF_NORTH_CRC)

/*
 * Define error masks for the several registers
 */

/* Enable all fatal and non fatal errors */
#define ENABLE_EMASK_ALL	(ERROR_FAT_MASK | ERROR_NF_MASK)

/* mask for fatal error registers */
#define FERR_FAT_MASK ERROR_FAT_MASK

/* masks for non-fatal error register */
static inline int to_nf_mask(unsigned int mask)
{
	return (mask & EMASK_M29) | (mask >> 3);
};

static inline int from_nf_ferr(unsigned int mask)
{
	return (mask & EMASK_M29) |		/* Bit 28 */
	       (mask & ((1 << 28) - 1) << 3);	/* Bits 0 to 27 */
};

#define FERR_NF_MASK		to_nf_mask(ERROR_NF_MASK)
#define FERR_NF_CORRECTABLE	to_nf_mask(ERROR_NF_CORRECTABLE)
#define FERR_NF_DIMM_SPARE	to_nf_mask(ERROR_NF_DIMM_SPARE)
#define FERR_NF_SPD_PROTOCOL	to_nf_mask(ERROR_NF_SPD_PROTOCOL)
#define FERR_NF_NORTH_CRC	to_nf_mask(ERROR_NF_NORTH_CRC)
#define FERR_NF_RECOVERABLE	to_nf_mask(ERROR_NF_RECOVERABLE)
#define FERR_NF_UNCORRECTABLE	to_nf_mask(ERROR_NF_UNCORRECTABLE)

#endif

/* Device name and register DID (Device ID) */
struct i7300_dev_info {
	const char *ctl_name;	/* name for this device */
	u16 fsb_mapping_errors;	/* DID for the branchmap,control */
};

/* Table of devices attributes supported by this driver */
static const struct i7300_dev_info i7300_devs[] = {
	{
		.ctl_name = "I7300",
		.fsb_mapping_errors = PCI_DEVICE_ID_INTEL_I7300_MCH_ERR,
	},
};

struct i7300_dimm_info {
	int megabytes;		/* size, 0 means not present  */
};

/* driver private data structure */
struct i7300_pvt {
	struct pci_dev *system_address;		/* 16.0 */
	struct pci_dev *branchmap_werrors;	/* 16.1 */
	struct pci_dev *fsb_error_regs;		/* 16.2 */
	struct pci_dev *branch_pci[MAX_BRANCHES];	/* 21.0  and 22.0 */

	u16 tolm;				/* top of low memory */
	u64 ambase;				/* AMB BAR */

	u16 mir[MAX_MIR];

	u16 mtr[MAX_SLOTS][MAX_BRANCHES];		/* Memory Technlogy Reg */
	u16 ambpresent[MAX_CHANNELS];		/* AMB present regs */

	/* DIMM information matrix, allocating architecture maximums */
	struct i7300_dimm_info dimm_info[MAX_SLOTS][MAX_CHANNELS];
};

#if 0
/* I7300 MCH error information retrieved from Hardware */
struct i7300_error_info {
	/* These registers are always read from the MC */
	u32 ferr_fat_fbd;	/* First Errors Fatal */
	u32 nerr_fat_fbd;	/* Next Errors Fatal */
	u32 ferr_nf_fbd;	/* First Errors Non-Fatal */
	u32 nerr_nf_fbd;	/* Next Errors Non-Fatal */

	/* These registers are input ONLY if there was a Recoverable Error */
	u32 redmemb;		/* Recoverable Mem Data Error log B */
	u16 recmema;		/* Recoverable Mem Error log A */
	u32 recmemb;		/* Recoverable Mem Error log B */

	/* These registers are input ONLY if there was a Non-Rec Error */
	u16 nrecmema;		/* Non-Recoverable Mem log A */
	u16 nrecmemb;		/* Non-Recoverable Mem log B */

};
#endif

/* FIXME: Why do we need to have this static? */
static struct edac_pci_ctl_info *i7300_pci;


#if 0
/* note that nrec_rdwr changed from NRECMEMA to NRECMEMB between the 5000 and
   5400 better to use an inline function than a macro in this case */
static inline int nrec_bank(struct i7300_error_info *info)
{
	return ((info->nrecmema) >> 12) & 0x7;
}
static inline int nrec_rank(struct i7300_error_info *info)
{
	return ((info->nrecmema) >> 8) & 0xf;
}
static inline int nrec_buf_id(struct i7300_error_info *info)
{
	return ((info->nrecmema)) & 0xff;
}
static inline int nrec_rdwr(struct i7300_error_info *info)
{
	return (info->nrecmemb) >> 31;
}
/* This applies to both NREC and REC string so it can be used with nrec_rdwr
   and rec_rdwr */
static inline const char *rdwr_str(int rdwr)
{
	return rdwr ? "Write" : "Read";
}
static inline int nrec_cas(struct i7300_error_info *info)
{
	return ((info->nrecmemb) >> 16) & 0x1fff;
}
static inline int nrec_ras(struct i7300_error_info *info)
{
	return (info->nrecmemb) & 0xffff;
}
static inline int rec_bank(struct i7300_error_info *info)
{
	return ((info->recmema) >> 12) & 0x7;
}
static inline int rec_rank(struct i7300_error_info *info)
{
	return ((info->recmema) >> 8) & 0xf;
}
static inline int rec_rdwr(struct i7300_error_info *info)
{
	return (info->recmemb) >> 31;
}
static inline int rec_cas(struct i7300_error_info *info)
{
	return ((info->recmemb) >> 16) & 0x1fff;
}
static inline int rec_ras(struct i7300_error_info *info)
{
	return (info->recmemb) & 0xffff;
}

/*
 *	i7300_get_error_info	Retrieve the hardware error information from
 *				the hardware and cache it in the 'info'
 *				structure
 */
static void i7300_get_error_info(struct mem_ctl_info *mci,
				 struct i7300_error_info *info)
{
	struct i7300_pvt *pvt;
	u32 value;

	pvt = mci->pvt_info;

	/* read in the 1st FATAL error register */
	pci_read_config_dword(pvt->branchmap_werrors, FERR_FAT_FBD, &value);

	/* Mask only the bits that the doc says are valid
	 */
	value &= (FERR_FAT_FBDCHAN | FERR_FAT_MASK);

	/* If there is an error, then read in the
	   NEXT FATAL error register and the Memory Error Log Register A
	 */
	if (value & FERR_FAT_MASK) {
		info->ferr_fat_fbd = value;

		/* harvest the various error data we need */
		pci_read_config_dword(pvt->branchmap_werrors,
				NERR_FAT_FBD, &info->nerr_fat_fbd);
		pci_read_config_word(pvt->branchmap_werrors,
				NRECMEMA, &info->nrecmema);
		pci_read_config_word(pvt->branchmap_werrors,
				NRECMEMB, &info->nrecmemb);

		/* Clear the error bits, by writing them back */
		pci_write_config_dword(pvt->branchmap_werrors,
				FERR_FAT_FBD, value);
	} else {
		info->ferr_fat_fbd = 0;
		info->nerr_fat_fbd = 0;
		info->nrecmema = 0;
		info->nrecmemb = 0;
	}

	/* read in the 1st NON-FATAL error register */
	pci_read_config_dword(pvt->branchmap_werrors, FERR_NF_FBD, &value);

	/* If there is an error, then read in the 1st NON-FATAL error
	 * register as well */
	if (value & FERR_NF_MASK) {
		info->ferr_nf_fbd = value;

		/* harvest the various error data we need */
		pci_read_config_dword(pvt->branchmap_werrors,
				NERR_NF_FBD, &info->nerr_nf_fbd);
		pci_read_config_word(pvt->branchmap_werrors,
				RECMEMA, &info->recmema);
		pci_read_config_dword(pvt->branchmap_werrors,
				RECMEMB, &info->recmemb);
		pci_read_config_dword(pvt->branchmap_werrors,
				REDMEMB, &info->redmemb);

		/* Clear the error bits, by writing them back */
		pci_write_config_dword(pvt->branchmap_werrors,
				FERR_NF_FBD, value);
	} else {
		info->ferr_nf_fbd = 0;
		info->nerr_nf_fbd = 0;
		info->recmema = 0;
		info->recmemb = 0;
		info->redmemb = 0;
	}
}

/*
 * i7300_proccess_non_recoverable_info(struct mem_ctl_info *mci,
 * 					struct i7300_error_info *info,
 * 					int handle_errors);
 *
 *	handle the Intel FATAL and unrecoverable errors, if any
 */
static void i7300_proccess_non_recoverable_info(struct mem_ctl_info *mci,
				    struct i7300_error_info *info,
				    unsigned long allErrors)
{
	char msg[EDAC_MC_LABEL_LEN + 1 + 90 + 80];
	int branch;
	int channel;
	int bank;
	int buf_id;
	int rank;
	int rdwr;
	int ras, cas;
	int errnum;
	char *type = NULL;

	if (!allErrors)
		return;		/* if no error, return now */

	if (allErrors &  ERROR_FAT_MASK)
		type = "FATAL";
	else if (allErrors & FERR_NF_UNCORRECTABLE)
		type = "NON-FATAL uncorrected";
	else
		type = "NON-FATAL recoverable";

	/* ONLY ONE of the possible error bits will be set, as per the docs */

	branch = extract_fbdchan_indx(info->ferr_fat_fbd);
	channel = branch;

	/* Use the NON-Recoverable macros to extract data */
	bank = nrec_bank(info);
	rank = nrec_rank(info);
	buf_id = nrec_buf_id(info);
	rdwr = nrec_rdwr(info);
	ras = nrec_ras(info);
	cas = nrec_cas(info);

	debugf0("\t\tCSROW= %d  Channels= %d,%d  (Branch= %d "
		"DRAM Bank= %d Buffer ID = %d rdwr= %s ras= %d cas= %d)\n",
		rank, channel, channel + 1, branch >> 1, bank,
		buf_id, rdwr_str(rdwr), ras, cas);

	/* Only 1 bit will be on */
	errnum = find_first_bit(&allErrors, ARRAY_SIZE(error_name));

	/* Form out message */
	snprintf(msg, sizeof(msg),
		 "%s (Branch=%d DRAM-Bank=%d Buffer ID = %d RDWR=%s "
		 "RAS=%d CAS=%d %s Err=0x%lx (%s))",
		 type, branch >> 1, bank, buf_id, rdwr_str(rdwr), ras, cas,
		 type, allErrors, error_name[errnum]);

	/* Call the helper to output message */
	edac_mc_handle_fbd_ue(mci, rank, channel, channel + 1, msg);
}

/*
 * i7300_process_fatal_error_info(struct mem_ctl_info *mci,
 * 				struct i7300_error_info *info,
 * 				int handle_errors);
 *
 *	handle the Intel NON-FATAL errors, if any
 */
static void i7300_process_nonfatal_error_info(struct mem_ctl_info *mci,
					struct i7300_error_info *info)
{
	char msg[EDAC_MC_LABEL_LEN + 1 + 90 + 80];
	unsigned long allErrors;
	int branch;
	int channel;
	int bank;
	int rank;
	int rdwr;
	int ras, cas;
	int errnum;

	/* mask off the Error bits that are possible */
	allErrors = from_nf_ferr(info->ferr_nf_fbd & FERR_NF_MASK);
	if (!allErrors)
		return;		/* if no error, return now */

	/* ONLY ONE of the possible error bits will be set, as per the docs */

	if (allErrors & (ERROR_NF_UNCORRECTABLE | ERROR_NF_RECOVERABLE)) {
		i7300_proccess_non_recoverable_info(mci, info, allErrors);
		return;
	}

	/* Correctable errors */
	if (allErrors & ERROR_NF_CORRECTABLE) {
		debugf0("\tCorrected bits= 0x%lx\n", allErrors);

		branch = extract_fbdchan_indx(info->ferr_nf_fbd);

		channel = 0;
		if (REC_ECC_LOCATOR_ODD(info->redmemb))
			channel = 1;

		/* Convert channel to be based from zero, instead of
		 * from branch base of 0 */
		channel += branch;

		bank = rec_bank(info);
		rank = rec_rank(info);
		rdwr = rec_rdwr(info);
		ras = rec_ras(info);
		cas = rec_cas(info);

		/* Only 1 bit will be on */
		errnum = find_first_bit(&allErrors, ARRAY_SIZE(error_name));

		debugf0("\t\tCSROW= %d Channel= %d  (Branch %d "
			"DRAM Bank= %d rdwr= %s ras= %d cas= %d)\n",
			rank, channel, branch >> 1, bank,
			rdwr_str(rdwr), ras, cas);

		/* Form out message */
		snprintf(msg, sizeof(msg),
			 "Corrected error (Branch=%d DRAM-Bank=%d RDWR=%s "
			 "RAS=%d CAS=%d, CE Err=0x%lx (%s))",
			 branch >> 1, bank, rdwr_str(rdwr), ras, cas,
			 allErrors, error_name[errnum]);

		/* Call the helper to output message */
		edac_mc_handle_fbd_ce(mci, rank, channel, msg);

		return;
	}

	/* Miscelaneous errors */
	errnum = find_first_bit(&allErrors, ARRAY_SIZE(error_name));

	branch = extract_fbdchan_indx(info->ferr_nf_fbd);

	i7300_mc_printk(mci, KERN_EMERG,
			"Non-Fatal misc error (Branch=%d Err=%#lx (%s))",
			branch >> 1, allErrors, error_name[errnum]);
}

/*
 *	i7300_process_error_info	Process the error info that is
 *	in the 'info' structure, previously retrieved from hardware
 */
static void i7300_process_error_info(struct mem_ctl_info *mci,
				struct i7300_error_info *info)
{	u32 allErrors;

	/* First handle any fatal errors that occurred */
	allErrors = (info->ferr_fat_fbd & FERR_FAT_MASK);
	i7300_proccess_non_recoverable_info(mci, info, allErrors);

	/* now handle any non-fatal errors that occurred */
	i7300_process_nonfatal_error_info(mci, info);
}

/*
 *	i7300_clear_error	Retrieve any error from the hardware
 *				but do NOT process that error.
 *				Used for 'clearing' out of previous errors
 *				Called by the Core module.
 */
static void i7300_clear_error(struct mem_ctl_info *mci)
{
	struct i7300_error_info info;

	i7300_get_error_info(mci, &info);
}

/*
 *	i7300_check_error	Retrieve and process errors reported by the
 *				hardware. Called by the Core module.
 */
static void i7300_check_error(struct mem_ctl_info *mci)
{
	struct i7300_error_info info;
	debugf4("MC%d: " __FILE__ ": %s()\n", mci->mc_idx, __func__);
	i7300_get_error_info(mci, &info);
	i7300_process_error_info(mci, &info);
}

/*
 *	i7300_enable_error_reporting
 *			Turn on the memory reporting features of the hardware
 */
static void i7300_enable_error_reporting(struct mem_ctl_info *mci)
{
	struct i7300_pvt *pvt;
	u32 fbd_error_mask;

	pvt = mci->pvt_info;

	/* Read the FBD Error Mask Register */
	pci_read_config_dword(pvt->branchmap_werrors, EMASK_FBD,
			&fbd_error_mask);

	/* Enable with a '0' */
	fbd_error_mask &= ~(ENABLE_EMASK_ALL);

	pci_write_config_dword(pvt->branchmap_werrors, EMASK_FBD,
			fbd_error_mask);
}
#endif

/*
 * determine_mtr(pvt, csrow, channel)
 *
 * return the proper MTR register as determine by the csrow and desired channel
 */
static int decode_mtr(struct i7300_pvt *pvt,
		      int slot, int ch, int branch,
		      struct i7300_dimm_info *dinfo,
		      struct csrow_info *p_csrow)
{
	int mtr, ans, addrBits, channel;

	channel = to_channel(ch, branch);

	mtr = pvt->mtr[slot][branch];
	ans = MTR_DIMMS_PRESENT(mtr) ? 1 : 0;

	debugf2("\tMTR%d CH%d: DIMMs are %s (mtr)\n",
		slot, channel,
		ans ? "Present" : "NOT Present");

	/* Determine if there is a DIMM present in this DIMM slot */

#if 0
	if (!amb_present || !ans)
		return 0;
#else
	if (!ans)
		return 0;
#endif

	/* Start with the number of bits for a Bank
	* on the DRAM */
	addrBits = MTR_DRAM_BANKS_ADDR_BITS;
	/* Add thenumber of ROW bits */
	addrBits += MTR_DIMM_ROWS_ADDR_BITS(mtr);
	/* add the number of COLUMN bits */
	addrBits += MTR_DIMM_COLS_ADDR_BITS(mtr);
	/* add the number of RANK bits */
	addrBits += MTR_DIMM_RANKS(mtr);

	addrBits += 6;	/* add 64 bits per DIMM */
	addrBits -= 20;	/* divide by 2^^20 */
	addrBits -= 3;	/* 8 bits per bytes */

	dinfo->megabytes = 1 << addrBits;

	debugf2("\t\tWIDTH: x%d\n", MTR_DRAM_WIDTH(mtr));

	debugf2("\t\tELECTRICAL THROTTLING is %s\n",
		MTR_DIMMS_ETHROTTLE(mtr) ? "enabled" : "disabled");

	debugf2("\t\tNUMBANK: %d bank(s)\n", MTR_DRAM_BANKS(mtr));
	debugf2("\t\tNUMRANK: %s\n", MTR_DIMM_RANKS(mtr) ? "double" : "single");
	debugf2("\t\tNUMROW: %s\n", numrow_toString[MTR_DIMM_ROWS(mtr)]);
	debugf2("\t\tNUMCOL: %s\n", numcol_toString[MTR_DIMM_COLS(mtr)]);
	debugf2("\t\tSIZE: %d MB\n", dinfo->megabytes);

	p_csrow->grain = 8;
	p_csrow->nr_pages = dinfo->megabytes << 8;
	p_csrow->mtype = MEM_FB_DDR2;
	p_csrow->edac_mode = EDAC_S8ECD8ED;

	/* ask what device type on this row */
	if (MTR_DRAM_WIDTH(mtr))
		p_csrow->dtype = DEV_X8;
	else
		p_csrow->dtype = DEV_X4;

	return mtr;
}

/*
 *	print_dimm_size
 *
 *	also will output a DIMM matrix map, if debug is enabled, for viewing
 *	how the DIMMs are populated
 */
static void print_dimm_size(struct i7300_pvt *pvt)
{
	struct i7300_dimm_info *dinfo;
	char *p, *mem_buffer;
	int space, n;
	int channel, slot;

	space = PAGE_SIZE;
	mem_buffer = p = kmalloc(space, GFP_KERNEL);
	if (p == NULL) {
		i7300_printk(KERN_ERR, "MC: %s:%s() kmalloc() failed\n",
			__FILE__, __func__);
		return;
	}

	n = snprintf(p, space, "              ");
	p += n;
	space -= n;
	for (channel = 0; channel < MAX_CHANNELS; channel++) {
		n = snprintf(p, space, "channel %d | ", channel);
		p += n;
		space -= n;
	}
	debugf2("%s\n", mem_buffer);
	p = mem_buffer;
	space = PAGE_SIZE;
	n = snprintf(p, space, "-------------------------------"
		               "------------------------------");
	p += n;
	space -= n;
	debugf2("%s\n", mem_buffer);
	p = mem_buffer;
	space = PAGE_SIZE;

	for (slot = 0; slot < MAX_SLOTS; slot++) {
		n = snprintf(p, space, "csrow/SLOT %d  ", slot);
		p += n;
		space -= n;

		for (channel = 0; channel < MAX_CHANNELS; channel++) {
			dinfo = &pvt->dimm_info[slot][channel];
			n = snprintf(p, space, "%4d MB   | ", dinfo->megabytes);
			p += n;
			space -= n;
		}

		debugf2("%s\n", mem_buffer);
		p = mem_buffer;
		space = PAGE_SIZE;
	}

	n = snprintf(p, space, "-------------------------------"
		               "------------------------------");
	p += n;
	space -= n;
	debugf2("%s\n", mem_buffer);
	p = mem_buffer;
	space = PAGE_SIZE;

	kfree(mem_buffer);
}

/*
 *	i7300_init_csrows	Initialize the 'csrows' table within
 *				the mci control	structure with the
 *				addressing of memory.
 *
 *	return:
 *		0	success
 *		1	no actual memory found on this MC
 */
static int i7300_init_csrows(struct mem_ctl_info *mci)
{
	struct i7300_pvt *pvt;
	struct i7300_dimm_info *dinfo;
	struct csrow_info *p_csrow;
	int empty;
	int mtr;
	int ch, branch, slot, channel;

	pvt = mci->pvt_info;

	empty = 1;		/* Assume NO memory */

	debugf2("Memory Technology Registers:\n");

	/* Get the AMB present registers for the four channels */
	for (branch = 0; branch < MAX_BRANCHES; branch++) {
		/* Read and dump branch 0's MTRs */
		channel = to_channel(0, branch);
		pci_read_config_word(pvt->branch_pci[branch], AMBPRESENT_0,
				&pvt->ambpresent[channel]);
		debugf2("\t\tAMB-present CH%d = 0x%x:\n",
			channel, pvt->ambpresent[channel]);

		channel = to_channel(1, branch);
		pci_read_config_word(pvt->branch_pci[branch], AMBPRESENT_1,
				&pvt->ambpresent[channel]);
		debugf2("\t\tAMB-present CH%d = 0x%x:\n",
			channel, pvt->ambpresent[channel]);
	}

	/* Get the set of MTR[0-7] regs by each branch */
	for (slot = 0; slot < MAX_SLOTS; slot++) {
		int where = mtr_regs[slot];
		for (branch = 0; branch < MAX_BRANCHES; branch++) {
			pci_read_config_word(pvt->branch_pci[branch],
					where,
					&pvt->mtr[slot][branch]);
			for (ch = 0; ch < MAX_BRANCHES; ch++) {
				int channel = to_channel(ch, branch);

				dinfo = &pvt->dimm_info[slot][channel];
				p_csrow = &mci->csrows[slot];

				mtr = decode_mtr(pvt, slot, ch, branch,
							dinfo, p_csrow);
				/* if no DIMMS on this row, continue */
				if (!MTR_DIMMS_PRESENT(mtr))
					continue;

				p_csrow->csrow_idx = slot;

				/* FAKE OUT VALUES, FIXME */
				p_csrow->first_page = 0 + slot * 20;
				p_csrow->last_page = 9 + slot * 20;
				p_csrow->page_mask = 0xfff;

				empty = 0;
			}
		}
	}

	return empty;
}

static void decode_mir(int mir_no, u16 mir[MAX_MIR])
{
	if (mir[mir_no] & 3)
		debugf2("MIR%d: limit= 0x%x Branch(es) that participate: %s %s\n",
			mir_no,
			(mir[mir_no] >> 4) & 0xfff,
			(mir[mir_no] & 1) ? "B0" : "",
			(mir[mir_no] & 2) ? "B1": "");
}

/*
 *	i7300_get_mc_regs	read in the necessary registers and
 *				cache locally
 *
 *			Fills in the private data members
 */
static int i7300_get_mc_regs(struct mem_ctl_info *mci)
{
	struct i7300_pvt *pvt;
	u32 actual_tolm;
	int i, rc;

	pvt = mci->pvt_info;

	pci_read_config_dword(pvt->system_address, AMBASE,
			(u32 *) &pvt->ambase);

	debugf2("AMBASE= 0x%lx\n", (long unsigned int)pvt->ambase);

	/* Get the Branch Map regs */
	pci_read_config_word(pvt->branchmap_werrors, TOLM, &pvt->tolm);
	pvt->tolm >>= 12;
	debugf2("TOLM (number of 256M regions) =%u (0x%x)\n", pvt->tolm,
		pvt->tolm);

	actual_tolm = (u32) ((1000l * pvt->tolm) >> (30 - 28));
	debugf2("Actual TOLM byte addr=%u.%03u GB (0x%x)\n",
		actual_tolm/1000, actual_tolm % 1000, pvt->tolm << 28);

	pci_read_config_word(pvt->branchmap_werrors, MIR0, &pvt->mir[0]);
	pci_read_config_word(pvt->branchmap_werrors, MIR1, &pvt->mir[1]);
	pci_read_config_word(pvt->branchmap_werrors, MIR2, &pvt->mir[2]);

	/* Decode the MIR regs */
	for (i = 0; i < MAX_MIR; i++)
		decode_mir(i, pvt->mir);

	rc = i7300_init_csrows(mci);
	if (rc < 0)
		return rc;

	/* Go and determine the size of each DIMM and place in an
	 * orderly matrix */
	print_dimm_size(pvt);

	return 0;
}

/*
 *	i7300_put_devices	'put' all the devices that we have
 *				reserved via 'get'
 */
static void i7300_put_devices(struct mem_ctl_info *mci)
{
	struct i7300_pvt *pvt;
	int branch;

	pvt = mci->pvt_info;

	/* Decrement usage count for devices */
	for (branch = 0; branch < MAX_CH_PER_BRANCH; branch++)
		pci_dev_put(pvt->branch_pci[branch]);
	pci_dev_put(pvt->fsb_error_regs);
	pci_dev_put(pvt->branchmap_werrors);
}

/*
 *	i7300_get_devices	Find and perform 'get' operation on the MCH's
 *			device/functions we want to reference for this driver
 *
 *			Need to 'get' device 16 func 1 and func 2
 */
static int i7300_get_devices(struct mem_ctl_info *mci, int dev_idx)
{
	struct i7300_pvt *pvt;
	struct pci_dev *pdev;

	pvt = mci->pvt_info;

	/* Attempt to 'get' the MCH register we want */
	pdev = NULL;
	while (!pvt->branchmap_werrors || !pvt->fsb_error_regs) {
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
				      PCI_DEVICE_ID_INTEL_I7300_MCH_ERR, pdev);
		if (!pdev) {
			/* End of list, leave */
			i7300_printk(KERN_ERR,
				"'system address,Process Bus' "
				"device not found:"
				"vendor 0x%x device 0x%x ERR funcs "
				"(broken BIOS?)\n",
				PCI_VENDOR_ID_INTEL,
				PCI_DEVICE_ID_INTEL_I7300_MCH_ERR);
			goto error;
		}

		/* Store device 16 funcs 1 and 2 */
		switch (PCI_FUNC(pdev->devfn)) {
		case 1:
			pvt->branchmap_werrors = pdev;
			break;
		case 2:
			pvt->fsb_error_regs = pdev;
			break;
		}
	}

	debugf1("System Address, processor bus- PCI Bus ID: %s  %x:%x\n",
		pci_name(pvt->system_address),
		pvt->system_address->vendor, pvt->system_address->device);
	debugf1("Branchmap, control and errors - PCI Bus ID: %s  %x:%x\n",
		pci_name(pvt->branchmap_werrors),
		pvt->branchmap_werrors->vendor, pvt->branchmap_werrors->device);
	debugf1("FSB Error Regs - PCI Bus ID: %s  %x:%x\n",
		pci_name(pvt->fsb_error_regs),
		pvt->fsb_error_regs->vendor, pvt->fsb_error_regs->device);

	pvt->branch_pci[0] = pci_get_device(PCI_VENDOR_ID_INTEL,
				            PCI_DEVICE_ID_INTEL_I7300_MCH_FB0,
					    NULL);
	if (!pvt->branch_pci[0]) {
		i7300_printk(KERN_ERR,
			"MC: 'BRANCH 0' device not found:"
			"vendor 0x%x device 0x%x Func 0 (broken BIOS?)\n",
			PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_I7300_MCH_FB0);
		goto error;
	}

	pvt->branch_pci[1] = pci_get_device(PCI_VENDOR_ID_INTEL,
					    PCI_DEVICE_ID_INTEL_I7300_MCH_FB1,
					    NULL);
	if (!pvt->branch_pci[1]) {
		i7300_printk(KERN_ERR,
			"MC: 'BRANCH 1' device not found:"
			"vendor 0x%x device 0x%x Func 0 "
			"(broken BIOS?)\n",
			PCI_VENDOR_ID_INTEL,
			PCI_DEVICE_ID_INTEL_I7300_MCH_FB1);
		goto error;
	}

	return 0;

error:
	i7300_put_devices(mci);
	return -ENODEV;
}

/*
 *	i7300_probe1	Probe for ONE instance of device to see if it is
 *			present.
 *	return:
 *		0 for FOUND a device
 *		< 0 for error code
 */
static int i7300_probe1(struct pci_dev *pdev, int dev_idx)
{
	struct mem_ctl_info *mci;
	struct i7300_pvt *pvt;
	int num_channels;
	int num_dimms_per_channel;
	int num_csrows;

	if (dev_idx >= ARRAY_SIZE(i7300_devs))
		return -EINVAL;

	debugf0("MC: " __FILE__ ": %s(), pdev bus %u dev=0x%x fn=0x%x\n",
		__func__,
		pdev->bus->number,
		PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

	/* We only are looking for func 0 of the set */
	if (PCI_FUNC(pdev->devfn) != 0)
		return -ENODEV;

	/* As we don't have a motherboard identification routine to determine
	 * actual number of slots/dimms per channel, we thus utilize the
	 * resource as specified by the chipset. Thus, we might have
	 * have more DIMMs per channel than actually on the mobo, but this
	 * allows the driver to support upto the chipset max, without
	 * some fancy mobo determination.
	 */
	num_dimms_per_channel = MAX_SLOTS;
	num_channels = MAX_CHANNELS;
	num_csrows = MAX_SLOTS * MAX_CHANNELS;

	debugf0("MC: %s(): Number of - Channels= %d  DIMMS= %d  CSROWS= %d\n",
		__func__, num_channels, num_dimms_per_channel, num_csrows);

	/* allocate a new MC control structure */
	mci = edac_mc_alloc(sizeof(*pvt), num_csrows, num_channels, 0);

	if (mci == NULL)
		return -ENOMEM;

	debugf0("MC: " __FILE__ ": %s(): mci = %p\n", __func__, mci);

	mci->dev = &pdev->dev;	/* record ptr  to the generic device */

	pvt = mci->pvt_info;
	pvt->system_address = pdev;	/* Record this device in our private */

	/* 'get' the pci devices we want to reserve for our use */
	if (i7300_get_devices(mci, dev_idx))
		goto fail0;

	mci->mc_idx = 0;
	mci->mtype_cap = MEM_FLAG_FB_DDR2;
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = "i7300_edac.c";
	mci->mod_ver = I7300_REVISION;
	mci->ctl_name = i7300_devs[dev_idx].ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->ctl_page_to_phys = NULL;

#if 0
	/* Set the function pointer to an actual operation function */
	mci->edac_check = i7300_check_error;
#endif

	/* initialize the MC control structure 'csrows' table
	 * with the mapping and control information */
	if (i7300_get_mc_regs(mci)) {
		debugf0("MC: Setting mci->edac_cap to EDAC_FLAG_NONE\n"
			"    because i7300_init_csrows() returned nonzero "
			"value\n");
		mci->edac_cap = EDAC_FLAG_NONE;	/* no csrows found */
	} else {
#if 0
		debugf1("MC: Enable error reporting now\n");
		i7300_enable_error_reporting(mci);
#endif
	}

	/* add this new MC control structure to EDAC's list of MCs */
	if (edac_mc_add_mc(mci)) {
		debugf0("MC: " __FILE__
			": %s(): failed edac_mc_add_mc()\n", __func__);
		/* FIXME: perhaps some code should go here that disables error
		 * reporting if we just enabled it
		 */
		goto fail1;
	}

#if 0
	i7300_clear_error(mci);
#endif

	/* allocating generic PCI control info */
	i7300_pci = edac_pci_create_generic_ctl(&pdev->dev, EDAC_MOD_STR);
	if (!i7300_pci) {
		printk(KERN_WARNING
			"%s(): Unable to create PCI control\n",
			__func__);
		printk(KERN_WARNING
			"%s(): PCI error report via EDAC not setup\n",
			__func__);
	}

	return 0;

	/* Error exit unwinding stack */
fail1:

	i7300_put_devices(mci);

fail0:
	edac_mc_free(mci);
	return -ENODEV;
}

/*
 *	i7300_init_one	constructor for one instance of device
 *
 * 	returns:
 *		negative on error
 *		count (>= 0)
 */
static int __devinit i7300_init_one(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	int rc;

	debugf0("MC: " __FILE__ ": %s()\n", __func__);

	/* wake up device */
	rc = pci_enable_device(pdev);
	if (rc == -EIO)
		return rc;

	/* now probe and enable the device */
	return i7300_probe1(pdev, id->driver_data);
}

/*
 *	i7300_remove_one	destructor for one instance of device
 *
 */
static void __devexit i7300_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;

	debugf0(__FILE__ ": %s()\n", __func__);

	if (i7300_pci)
		edac_pci_release_generic_ctl(i7300_pci);

	mci = edac_mc_del_mc(&pdev->dev);
	if (!mci)
		return;

	/* retrieve references to resources, and free those resources */
	i7300_put_devices(mci);

	edac_mc_free(mci);
}

/*
 *	pci_device_id	table for which devices we are looking for
 *
 *	The "E500P" device is the first device supported.
 */
static const struct pci_device_id i7300_pci_tbl[] __devinitdata = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_I7300_MCH_ERR)},
	{0,}			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, i7300_pci_tbl);

/*
 *	i7300_driver	pci_driver structure for this module
 *
 */
static struct pci_driver i7300_driver = {
	.name = "i7300_edac",
	.probe = i7300_init_one,
	.remove = __devexit_p(i7300_remove_one),
	.id_table = i7300_pci_tbl,
};

/*
 *	i7300_init		Module entry function
 *			Try to initialize this module for its devices
 */
static int __init i7300_init(void)
{
	int pci_rc;

	debugf2("MC: " __FILE__ ": %s()\n", __func__);

	/* Ensure that the OPSTATE is set correctly for POLL or NMI */
	opstate_init();

	pci_rc = pci_register_driver(&i7300_driver);

	return (pci_rc < 0) ? pci_rc : 0;
}

/*
 *	i7300_exit()	Module exit function
 *			Unregister the driver
 */
static void __exit i7300_exit(void)
{
	debugf2("MC: " __FILE__ ": %s()\n", __func__);
	pci_unregister_driver(&i7300_driver);
}

module_init(i7300_init);
module_exit(i7300_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_AUTHOR("Red Hat Inc. (http://www.redhat.com)");
MODULE_DESCRIPTION("MC Driver for Intel I7300 memory controllers - "
		   I7300_REVISION);

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");
