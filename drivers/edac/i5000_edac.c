/*
 * Intel 5000(P/V/X) class Memory Controllers kernel module
 *
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written by Douglas Thompson Linux Networx (http://lnxi.com)
 *	norsk5@xmission.com
 *
 * This module is based on the following document:
 *
 * Intel 5000X Chipset Memory Controller Hub (MCH) - Datasheet
 * 	http://developer.intel.com/design/chipsets/datashts/313070.htm
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/edac.h>
#include <asm/mmzone.h>

#include "edac_core.h"

/*
 * Alter this version for the I5000 module when modifications are made
 */
#define I5000_REVISION    " Ver: 2.0.12"
#define EDAC_MOD_STR      "i5000_edac"

#define i5000_printk(level, fmt, arg...) \
        edac_printk(level, "i5000", fmt, ##arg)

#define i5000_mc_printk(mci, level, fmt, arg...) \
        edac_mc_chipset_printk(mci, level, "i5000", fmt, ##arg)

#ifndef PCI_DEVICE_ID_INTEL_FBD_0
#define PCI_DEVICE_ID_INTEL_FBD_0	0x25F5
#endif
#ifndef PCI_DEVICE_ID_INTEL_FBD_1
#define PCI_DEVICE_ID_INTEL_FBD_1	0x25F6
#endif

/* Device 16,
 * Function 0: System Address
 * Function 1: Memory Branch Map, Control, Errors Register
 * Function 2: FSB Error Registers
 *
 * All 3 functions of Device 16 (0,1,2) share the SAME DID
 */
#define	PCI_DEVICE_ID_INTEL_I5000_DEV16	0x25F0

/* OFFSETS for Function 0 */

/* OFFSETS for Function 1 */
#define		AMBASE			0x48
#define		MAXCH			0x56
#define		MAXDIMMPERCH		0x57
#define		TOLM			0x6C
#define		REDMEMB			0x7C
#define			RED_ECC_LOCATOR(x)	((x) & 0x3FFFF)
#define			REC_ECC_LOCATOR_EVEN(x)	((x) & 0x001FF)
#define			REC_ECC_LOCATOR_ODD(x)	((x) & 0x3FE00)
#define		MIR0			0x80
#define		MIR1			0x84
#define		MIR2			0x88
#define		AMIR0			0x8C
#define		AMIR1			0x90
#define		AMIR2			0x94

#define		FERR_FAT_FBD		0x98
#define		NERR_FAT_FBD		0x9C
#define			EXTRACT_FBDCHAN_INDX(x)	(((x)>>28) & 0x3)
#define			FERR_FAT_FBDCHAN 0x30000000
#define			FERR_FAT_M3ERR	0x00000004
#define			FERR_FAT_M2ERR	0x00000002
#define			FERR_FAT_M1ERR	0x00000001
#define			FERR_FAT_MASK	(FERR_FAT_M1ERR | \
						FERR_FAT_M2ERR | \
						FERR_FAT_M3ERR)

#define		FERR_NF_FBD		0xA0

/* Thermal and SPD or BFD errors */
#define			FERR_NF_M28ERR	0x01000000
#define			FERR_NF_M27ERR	0x00800000
#define			FERR_NF_M26ERR	0x00400000
#define			FERR_NF_M25ERR	0x00200000
#define			FERR_NF_M24ERR	0x00100000
#define			FERR_NF_M23ERR	0x00080000
#define			FERR_NF_M22ERR	0x00040000
#define			FERR_NF_M21ERR	0x00020000

/* Correctable errors */
#define			FERR_NF_M20ERR	0x00010000
#define			FERR_NF_M19ERR	0x00008000
#define			FERR_NF_M18ERR	0x00004000
#define			FERR_NF_M17ERR	0x00002000

/* Non-Retry or redundant Retry errors */
#define			FERR_NF_M16ERR	0x00001000
#define			FERR_NF_M15ERR	0x00000800
#define			FERR_NF_M14ERR	0x00000400
#define			FERR_NF_M13ERR	0x00000200

/* Uncorrectable errors */
#define			FERR_NF_M12ERR	0x00000100
#define			FERR_NF_M11ERR	0x00000080
#define			FERR_NF_M10ERR	0x00000040
#define			FERR_NF_M9ERR	0x00000020
#define			FERR_NF_M8ERR	0x00000010
#define			FERR_NF_M7ERR	0x00000008
#define			FERR_NF_M6ERR	0x00000004
#define			FERR_NF_M5ERR	0x00000002
#define			FERR_NF_M4ERR	0x00000001

#define			FERR_NF_UNCORRECTABLE	(FERR_NF_M12ERR | \
							FERR_NF_M11ERR | \
							FERR_NF_M10ERR | \
							FERR_NF_M9ERR | \
							FERR_NF_M8ERR | \
							FERR_NF_M7ERR | \
							FERR_NF_M6ERR | \
							FERR_NF_M5ERR | \
							FERR_NF_M4ERR)
#define			FERR_NF_CORRECTABLE	(FERR_NF_M20ERR | \
							FERR_NF_M19ERR | \
							FERR_NF_M18ERR | \
							FERR_NF_M17ERR)
#define			FERR_NF_DIMM_SPARE	(FERR_NF_M27ERR | \
							FERR_NF_M28ERR)
#define			FERR_NF_THERMAL		(FERR_NF_M26ERR | \
							FERR_NF_M25ERR | \
							FERR_NF_M24ERR | \
							FERR_NF_M23ERR)
#define			FERR_NF_SPD_PROTOCOL	(FERR_NF_M22ERR)
#define			FERR_NF_NORTH_CRC	(FERR_NF_M21ERR)
#define			FERR_NF_NON_RETRY	(FERR_NF_M13ERR | \
							FERR_NF_M14ERR | \
							FERR_NF_M15ERR)

#define		NERR_NF_FBD		0xA4
#define			FERR_NF_MASK		(FERR_NF_UNCORRECTABLE | \
							FERR_NF_CORRECTABLE | \
							FERR_NF_DIMM_SPARE | \
							FERR_NF_THERMAL | \
							FERR_NF_SPD_PROTOCOL | \
							FERR_NF_NORTH_CRC | \
							FERR_NF_NON_RETRY)

#define		EMASK_FBD		0xA8
#define			EMASK_FBD_M28ERR	0x08000000
#define			EMASK_FBD_M27ERR	0x04000000
#define			EMASK_FBD_M26ERR	0x02000000
#define			EMASK_FBD_M25ERR	0x01000000
#define			EMASK_FBD_M24ERR	0x00800000
#define			EMASK_FBD_M23ERR	0x00400000
#define			EMASK_FBD_M22ERR	0x00200000
#define			EMASK_FBD_M21ERR	0x00100000
#define			EMASK_FBD_M20ERR	0x00080000
#define			EMASK_FBD_M19ERR	0x00040000
#define			EMASK_FBD_M18ERR	0x00020000
#define			EMASK_FBD_M17ERR	0x00010000

#define			EMASK_FBD_M15ERR	0x00004000
#define			EMASK_FBD_M14ERR	0x00002000
#define			EMASK_FBD_M13ERR	0x00001000
#define			EMASK_FBD_M12ERR	0x00000800
#define			EMASK_FBD_M11ERR	0x00000400
#define			EMASK_FBD_M10ERR	0x00000200
#define			EMASK_FBD_M9ERR		0x00000100
#define			EMASK_FBD_M8ERR		0x00000080
#define			EMASK_FBD_M7ERR		0x00000040
#define			EMASK_FBD_M6ERR		0x00000020
#define			EMASK_FBD_M5ERR		0x00000010
#define			EMASK_FBD_M4ERR		0x00000008
#define			EMASK_FBD_M3ERR		0x00000004
#define			EMASK_FBD_M2ERR		0x00000002
#define			EMASK_FBD_M1ERR		0x00000001

#define			ENABLE_EMASK_FBD_FATAL_ERRORS	(EMASK_FBD_M1ERR | \
							EMASK_FBD_M2ERR | \
							EMASK_FBD_M3ERR)

#define 		ENABLE_EMASK_FBD_UNCORRECTABLE	(EMASK_FBD_M4ERR | \
							EMASK_FBD_M5ERR | \
							EMASK_FBD_M6ERR | \
							EMASK_FBD_M7ERR | \
							EMASK_FBD_M8ERR | \
							EMASK_FBD_M9ERR | \
							EMASK_FBD_M10ERR | \
							EMASK_FBD_M11ERR | \
							EMASK_FBD_M12ERR)
#define 		ENABLE_EMASK_FBD_CORRECTABLE	(EMASK_FBD_M17ERR | \
							EMASK_FBD_M18ERR | \
							EMASK_FBD_M19ERR | \
							EMASK_FBD_M20ERR)
#define			ENABLE_EMASK_FBD_DIMM_SPARE	(EMASK_FBD_M27ERR | \
							EMASK_FBD_M28ERR)
#define			ENABLE_EMASK_FBD_THERMALS	(EMASK_FBD_M26ERR | \
							EMASK_FBD_M25ERR | \
							EMASK_FBD_M24ERR | \
							EMASK_FBD_M23ERR)
#define			ENABLE_EMASK_FBD_SPD_PROTOCOL	(EMASK_FBD_M22ERR)
#define			ENABLE_EMASK_FBD_NORTH_CRC	(EMASK_FBD_M21ERR)
#define			ENABLE_EMASK_FBD_NON_RETRY	(EMASK_FBD_M15ERR | \
							EMASK_FBD_M14ERR | \
							EMASK_FBD_M13ERR)

#define		ENABLE_EMASK_ALL	(ENABLE_EMASK_FBD_NON_RETRY | \
					ENABLE_EMASK_FBD_NORTH_CRC | \
					ENABLE_EMASK_FBD_SPD_PROTOCOL | \
					ENABLE_EMASK_FBD_THERMALS | \
					ENABLE_EMASK_FBD_DIMM_SPARE | \
					ENABLE_EMASK_FBD_FATAL_ERRORS | \
					ENABLE_EMASK_FBD_CORRECTABLE | \
					ENABLE_EMASK_FBD_UNCORRECTABLE)

#define		ERR0_FBD		0xAC
#define		ERR1_FBD		0xB0
#define		ERR2_FBD		0xB4
#define		MCERR_FBD		0xB8
#define		NRECMEMA		0xBE
#define			NREC_BANK(x)		(((x)>>12) & 0x7)
#define			NREC_RDWR(x)		(((x)>>11) & 1)
#define			NREC_RANK(x)		(((x)>>8) & 0x7)
#define		NRECMEMB		0xC0
#define			NREC_CAS(x)		(((x)>>16) & 0xFFFFFF)
#define			NREC_RAS(x)		((x) & 0x7FFF)
#define		NRECFGLOG		0xC4
#define		NREEECFBDA		0xC8
#define		NREEECFBDB		0xCC
#define		NREEECFBDC		0xD0
#define		NREEECFBDD		0xD4
#define		NREEECFBDE		0xD8
#define		REDMEMA			0xDC
#define		RECMEMA			0xE2
#define			REC_BANK(x)		(((x)>>12) & 0x7)
#define			REC_RDWR(x)		(((x)>>11) & 1)
#define			REC_RANK(x)		(((x)>>8) & 0x7)
#define		RECMEMB			0xE4
#define			REC_CAS(x)		(((x)>>16) & 0xFFFFFF)
#define			REC_RAS(x)		((x) & 0x7FFF)
#define		RECFGLOG		0xE8
#define		RECFBDA			0xEC
#define		RECFBDB			0xF0
#define		RECFBDC			0xF4
#define		RECFBDD			0xF8
#define		RECFBDE			0xFC

/* OFFSETS for Function 2 */

/*
 * Device 21,
 * Function 0: Memory Map Branch 0
 *
 * Device 22,
 * Function 0: Memory Map Branch 1
 */
#define PCI_DEVICE_ID_I5000_BRANCH_0	0x25F5
#define PCI_DEVICE_ID_I5000_BRANCH_1	0x25F6

#define AMB_PRESENT_0	0x64
#define AMB_PRESENT_1	0x66
#define MTR0		0x80
#define MTR1		0x84
#define MTR2		0x88
#define MTR3		0x8C

#define NUM_MTRS		4
#define CHANNELS_PER_BRANCH	2
#define MAX_BRANCHES		2

/* Defines to extract the various fields from the
 *	MTRx - Memory Technology Registers
 */
#define MTR_DIMMS_PRESENT(mtr)		((mtr) & (0x1 << 8))
#define MTR_DRAM_WIDTH(mtr)		((((mtr) >> 6) & 0x1) ? 8 : 4)
#define MTR_DRAM_BANKS(mtr)		((((mtr) >> 5) & 0x1) ? 8 : 4)
#define MTR_DRAM_BANKS_ADDR_BITS(mtr)	((MTR_DRAM_BANKS(mtr) == 8) ? 3 : 2)
#define MTR_DIMM_RANK(mtr)		(((mtr) >> 4) & 0x1)
#define MTR_DIMM_RANK_ADDR_BITS(mtr)	(MTR_DIMM_RANK(mtr) ? 2 : 1)
#define MTR_DIMM_ROWS(mtr)		(((mtr) >> 2) & 0x3)
#define MTR_DIMM_ROWS_ADDR_BITS(mtr)	(MTR_DIMM_ROWS(mtr) + 13)
#define MTR_DIMM_COLS(mtr)		((mtr) & 0x3)
#define MTR_DIMM_COLS_ADDR_BITS(mtr)	(MTR_DIMM_COLS(mtr) + 10)

/* enables the report of miscellaneous messages as CE errors - default off */
static int misc_messages;

/* Enumeration of supported devices */
enum i5000_chips {
	I5000P = 0,
	I5000V = 1,		/* future */
	I5000X = 2		/* future */
};

/* Device name and register DID (Device ID) */
struct i5000_dev_info {
	const char *ctl_name;	/* name for this device */
	u16 fsb_mapping_errors;	/* DID for the branchmap,control */
};

/* Table of devices attributes supported by this driver */
static const struct i5000_dev_info i5000_devs[] = {
	[I5000P] = {
		.ctl_name = "I5000",
		.fsb_mapping_errors = PCI_DEVICE_ID_INTEL_I5000_DEV16,
	},
};

struct i5000_dimm_info {
	int megabytes;		/* size, 0 means not present  */
	int dual_rank;
};

#define	MAX_CHANNELS	6	/* max possible channels */
#define MAX_CSROWS	(8*2)	/* max possible csrows per channel */

/* driver private data structure */
struct i5000_pvt {
	struct pci_dev *system_address;	/* 16.0 */
	struct pci_dev *branchmap_werrors;	/* 16.1 */
	struct pci_dev *fsb_error_regs;	/* 16.2 */
	struct pci_dev *branch_0;	/* 21.0 */
	struct pci_dev *branch_1;	/* 22.0 */

	u16 tolm;		/* top of low memory */
	union {
		u64 ambase;		/* AMB BAR */
		struct {
			u32 ambase_bottom;
			u32 ambase_top;
		} u __packed;
	};

	u16 mir0, mir1, mir2;

	u16 b0_mtr[NUM_MTRS];	/* Memory Technlogy Reg */
	u16 b0_ambpresent0;	/* Branch 0, Channel 0 */
	u16 b0_ambpresent1;	/* Brnach 0, Channel 1 */

	u16 b1_mtr[NUM_MTRS];	/* Memory Technlogy Reg */
	u16 b1_ambpresent0;	/* Branch 1, Channel 8 */
	u16 b1_ambpresent1;	/* Branch 1, Channel 1 */

	/* DIMM information matrix, allocating architecture maximums */
	struct i5000_dimm_info dimm_info[MAX_CSROWS][MAX_CHANNELS];

	/* Actual values for this controller */
	int maxch;		/* Max channels */
	int maxdimmperch;	/* Max DIMMs per channel */
};

/* I5000 MCH error information retrieved from Hardware */
struct i5000_error_info {

	/* These registers are always read from the MC */
	u32 ferr_fat_fbd;	/* First Errors Fatal */
	u32 nerr_fat_fbd;	/* Next Errors Fatal */
	u32 ferr_nf_fbd;	/* First Errors Non-Fatal */
	u32 nerr_nf_fbd;	/* Next Errors Non-Fatal */

	/* These registers are input ONLY if there was a Recoverable  Error */
	u32 redmemb;		/* Recoverable Mem Data Error log B */
	u16 recmema;		/* Recoverable Mem Error log A */
	u32 recmemb;		/* Recoverable Mem Error log B */

	/* These registers are input ONLY if there was a
	 * Non-Recoverable Error */
	u16 nrecmema;		/* Non-Recoverable Mem log A */
	u16 nrecmemb;		/* Non-Recoverable Mem log B */

};

static struct edac_pci_ctl_info *i5000_pci;

/*
 *	i5000_get_error_info	Retrieve the hardware error information from
 *				the hardware and cache it in the 'info'
 *				structure
 */
static void i5000_get_error_info(struct mem_ctl_info *mci,
				 struct i5000_error_info *info)
{
	struct i5000_pvt *pvt;
	u32 value;

	pvt = mci->pvt_info;

	/* read in the 1st FATAL error register */
	pci_read_config_dword(pvt->branchmap_werrors, FERR_FAT_FBD, &value);

	/* Mask only the bits that the doc says are valid
	 */
	value &= (FERR_FAT_FBDCHAN | FERR_FAT_MASK);

	/* If there is an error, then read in the */
	/* NEXT FATAL error register and the Memory Error Log Register A */
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
 * i5000_process_fatal_error_info(struct mem_ctl_info *mci,
 * 					struct i5000_error_info *info,
 * 					int handle_errors);
 *
 *	handle the Intel FATAL errors, if any
 */
static void i5000_process_fatal_error_info(struct mem_ctl_info *mci,
					struct i5000_error_info *info,
					int handle_errors)
{
	char msg[EDAC_MC_LABEL_LEN + 1 + 160];
	char *specific = NULL;
	u32 allErrors;
	int channel;
	int bank;
	int rank;
	int rdwr;
	int ras, cas;

	/* mask off the Error bits that are possible */
	allErrors = (info->ferr_fat_fbd & FERR_FAT_MASK);
	if (!allErrors)
		return;		/* if no error, return now */

	channel = EXTRACT_FBDCHAN_INDX(info->ferr_fat_fbd);

	/* Use the NON-Recoverable macros to extract data */
	bank = NREC_BANK(info->nrecmema);
	rank = NREC_RANK(info->nrecmema);
	rdwr = NREC_RDWR(info->nrecmema);
	ras = NREC_RAS(info->nrecmemb);
	cas = NREC_CAS(info->nrecmemb);

	edac_dbg(0, "\t\tCSROW= %d  Channel= %d (DRAM Bank= %d rdwr= %s ras= %d cas= %d)\n",
		 rank, channel, bank,
		 rdwr ? "Write" : "Read", ras, cas);

	/* Only 1 bit will be on */
	switch (allErrors) {
	case FERR_FAT_M1ERR:
		specific = "Alert on non-redundant retry or fast "
				"reset timeout";
		break;
	case FERR_FAT_M2ERR:
		specific = "Northbound CRC error on non-redundant "
				"retry";
		break;
	case FERR_FAT_M3ERR:
		{
		static int done;

		/*
		 * This error is generated to inform that the intelligent
		 * throttling is disabled and the temperature passed the
		 * specified middle point. Since this is something the BIOS
		 * should take care of, we'll warn only once to avoid
		 * worthlessly flooding the log.
		 */
		if (done)
			return;
		done++;

		specific = ">Tmid Thermal event with intelligent "
			   "throttling disabled";
		}
		break;
	}

	/* Form out message */
	snprintf(msg, sizeof(msg),
		 "Bank=%d RAS=%d CAS=%d FATAL Err=0x%x (%s)",
		 bank, ras, cas, allErrors, specific);

	/* Call the helper to output message */
	edac_mc_handle_error(HW_EVENT_ERR_FATAL, mci, 1, 0, 0, 0,
			     channel >> 1, channel & 1, rank,
			     rdwr ? "Write error" : "Read error",
			     msg);
}

/*
 * i5000_process_fatal_error_info(struct mem_ctl_info *mci,
 * 				struct i5000_error_info *info,
 * 				int handle_errors);
 *
 *	handle the Intel NON-FATAL errors, if any
 */
static void i5000_process_nonfatal_error_info(struct mem_ctl_info *mci,
					struct i5000_error_info *info,
					int handle_errors)
{
	char msg[EDAC_MC_LABEL_LEN + 1 + 170];
	char *specific = NULL;
	u32 allErrors;
	u32 ue_errors;
	u32 ce_errors;
	u32 misc_errors;
	int branch;
	int channel;
	int bank;
	int rank;
	int rdwr;
	int ras, cas;

	/* mask off the Error bits that are possible */
	allErrors = (info->ferr_nf_fbd & FERR_NF_MASK);
	if (!allErrors)
		return;		/* if no error, return now */

	/* ONLY ONE of the possible error bits will be set, as per the docs */
	ue_errors = allErrors & FERR_NF_UNCORRECTABLE;
	if (ue_errors) {
		edac_dbg(0, "\tUncorrected bits= 0x%x\n", ue_errors);

		branch = EXTRACT_FBDCHAN_INDX(info->ferr_nf_fbd);

		/*
		 * According with i5000 datasheet, bit 28 has no significance
		 * for errors M4Err-M12Err and M17Err-M21Err, on FERR_NF_FBD
		 */
		channel = branch & 2;

		bank = NREC_BANK(info->nrecmema);
		rank = NREC_RANK(info->nrecmema);
		rdwr = NREC_RDWR(info->nrecmema);
		ras = NREC_RAS(info->nrecmemb);
		cas = NREC_CAS(info->nrecmemb);

		edac_dbg(0, "\t\tCSROW= %d  Channels= %d,%d  (Branch= %d DRAM Bank= %d rdwr= %s ras= %d cas= %d)\n",
			 rank, channel, channel + 1, branch >> 1, bank,
			 rdwr ? "Write" : "Read", ras, cas);

		switch (ue_errors) {
		case FERR_NF_M12ERR:
			specific = "Non-Aliased Uncorrectable Patrol Data ECC";
			break;
		case FERR_NF_M11ERR:
			specific = "Non-Aliased Uncorrectable Spare-Copy "
					"Data ECC";
			break;
		case FERR_NF_M10ERR:
			specific = "Non-Aliased Uncorrectable Mirrored Demand "
					"Data ECC";
			break;
		case FERR_NF_M9ERR:
			specific = "Non-Aliased Uncorrectable Non-Mirrored "
					"Demand Data ECC";
			break;
		case FERR_NF_M8ERR:
			specific = "Aliased Uncorrectable Patrol Data ECC";
			break;
		case FERR_NF_M7ERR:
			specific = "Aliased Uncorrectable Spare-Copy Data ECC";
			break;
		case FERR_NF_M6ERR:
			specific = "Aliased Uncorrectable Mirrored Demand "
					"Data ECC";
			break;
		case FERR_NF_M5ERR:
			specific = "Aliased Uncorrectable Non-Mirrored Demand "
					"Data ECC";
			break;
		case FERR_NF_M4ERR:
			specific = "Uncorrectable Data ECC on Replay";
			break;
		}

		/* Form out message */
		snprintf(msg, sizeof(msg),
			 "Rank=%d Bank=%d RAS=%d CAS=%d, UE Err=0x%x (%s)",
			 rank, bank, ras, cas, ue_errors, specific);

		/* Call the helper to output message */
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1, 0, 0, 0,
				channel >> 1, -1, rank,
				rdwr ? "Write error" : "Read error",
				msg);
	}

	/* Check correctable errors */
	ce_errors = allErrors & FERR_NF_CORRECTABLE;
	if (ce_errors) {
		edac_dbg(0, "\tCorrected bits= 0x%x\n", ce_errors);

		branch = EXTRACT_FBDCHAN_INDX(info->ferr_nf_fbd);

		channel = 0;
		if (REC_ECC_LOCATOR_ODD(info->redmemb))
			channel = 1;

		/* Convert channel to be based from zero, instead of
		 * from branch base of 0 */
		channel += branch;

		bank = REC_BANK(info->recmema);
		rank = REC_RANK(info->recmema);
		rdwr = REC_RDWR(info->recmema);
		ras = REC_RAS(info->recmemb);
		cas = REC_CAS(info->recmemb);

		edac_dbg(0, "\t\tCSROW= %d Channel= %d  (Branch %d DRAM Bank= %d rdwr= %s ras= %d cas= %d)\n",
			 rank, channel, branch >> 1, bank,
			 rdwr ? "Write" : "Read", ras, cas);

		switch (ce_errors) {
		case FERR_NF_M17ERR:
			specific = "Correctable Non-Mirrored Demand Data ECC";
			break;
		case FERR_NF_M18ERR:
			specific = "Correctable Mirrored Demand Data ECC";
			break;
		case FERR_NF_M19ERR:
			specific = "Correctable Spare-Copy Data ECC";
			break;
		case FERR_NF_M20ERR:
			specific = "Correctable Patrol Data ECC";
			break;
		}

		/* Form out message */
		snprintf(msg, sizeof(msg),
			 "Rank=%d Bank=%d RDWR=%s RAS=%d "
			 "CAS=%d, CE Err=0x%x (%s))", branch >> 1, bank,
			 rdwr ? "Write" : "Read", ras, cas, ce_errors,
			 specific);

		/* Call the helper to output message */
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1, 0, 0, 0,
				channel >> 1, channel % 2, rank,
				rdwr ? "Write error" : "Read error",
				msg);
	}

	if (!misc_messages)
		return;

	misc_errors = allErrors & (FERR_NF_NON_RETRY | FERR_NF_NORTH_CRC |
				   FERR_NF_SPD_PROTOCOL | FERR_NF_DIMM_SPARE);
	if (misc_errors) {
		switch (misc_errors) {
		case FERR_NF_M13ERR:
			specific = "Non-Retry or Redundant Retry FBD Memory "
					"Alert or Redundant Fast Reset Timeout";
			break;
		case FERR_NF_M14ERR:
			specific = "Non-Retry or Redundant Retry FBD "
					"Configuration Alert";
			break;
		case FERR_NF_M15ERR:
			specific = "Non-Retry or Redundant Retry FBD "
					"Northbound CRC error on read data";
			break;
		case FERR_NF_M21ERR:
			specific = "FBD Northbound CRC error on "
					"FBD Sync Status";
			break;
		case FERR_NF_M22ERR:
			specific = "SPD protocol error";
			break;
		case FERR_NF_M27ERR:
			specific = "DIMM-spare copy started";
			break;
		case FERR_NF_M28ERR:
			specific = "DIMM-spare copy completed";
			break;
		}
		branch = EXTRACT_FBDCHAN_INDX(info->ferr_nf_fbd);

		/* Form out message */
		snprintf(msg, sizeof(msg),
			 "Err=%#x (%s)", misc_errors, specific);

		/* Call the helper to output message */
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1, 0, 0, 0,
				branch >> 1, -1, -1,
				"Misc error", msg);
	}
}

/*
 *	i5000_process_error_info	Process the error info that is
 *	in the 'info' structure, previously retrieved from hardware
 */
static void i5000_process_error_info(struct mem_ctl_info *mci,
				struct i5000_error_info *info,
				int handle_errors)
{
	/* First handle any fatal errors that occurred */
	i5000_process_fatal_error_info(mci, info, handle_errors);

	/* now handle any non-fatal errors that occurred */
	i5000_process_nonfatal_error_info(mci, info, handle_errors);
}

/*
 *	i5000_clear_error	Retrieve any error from the hardware
 *				but do NOT process that error.
 *				Used for 'clearing' out of previous errors
 *				Called by the Core module.
 */
static void i5000_clear_error(struct mem_ctl_info *mci)
{
	struct i5000_error_info info;

	i5000_get_error_info(mci, &info);
}

/*
 *	i5000_check_error	Retrieve and process errors reported by the
 *				hardware. Called by the Core module.
 */
static void i5000_check_error(struct mem_ctl_info *mci)
{
	struct i5000_error_info info;
	edac_dbg(4, "MC%d\n", mci->mc_idx);
	i5000_get_error_info(mci, &info);
	i5000_process_error_info(mci, &info, 1);
}

/*
 *	i5000_get_devices	Find and perform 'get' operation on the MCH's
 *			device/functions we want to reference for this driver
 *
 *			Need to 'get' device 16 func 1 and func 2
 */
static int i5000_get_devices(struct mem_ctl_info *mci, int dev_idx)
{
	//const struct i5000_dev_info *i5000_dev = &i5000_devs[dev_idx];
	struct i5000_pvt *pvt;
	struct pci_dev *pdev;

	pvt = mci->pvt_info;

	/* Attempt to 'get' the MCH register we want */
	pdev = NULL;
	while (1) {
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
				PCI_DEVICE_ID_INTEL_I5000_DEV16, pdev);

		/* End of list, leave */
		if (pdev == NULL) {
			i5000_printk(KERN_ERR,
				"'system address,Process Bus' "
				"device not found:"
				"vendor 0x%x device 0x%x FUNC 1 "
				"(broken BIOS?)\n",
				PCI_VENDOR_ID_INTEL,
				PCI_DEVICE_ID_INTEL_I5000_DEV16);

			return 1;
		}

		/* Scan for device 16 func 1 */
		if (PCI_FUNC(pdev->devfn) == 1)
			break;
	}

	pvt->branchmap_werrors = pdev;

	/* Attempt to 'get' the MCH register we want */
	pdev = NULL;
	while (1) {
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
				PCI_DEVICE_ID_INTEL_I5000_DEV16, pdev);

		if (pdev == NULL) {
			i5000_printk(KERN_ERR,
				"MC: 'branchmap,control,errors' "
				"device not found:"
				"vendor 0x%x device 0x%x Func 2 "
				"(broken BIOS?)\n",
				PCI_VENDOR_ID_INTEL,
				PCI_DEVICE_ID_INTEL_I5000_DEV16);

			pci_dev_put(pvt->branchmap_werrors);
			return 1;
		}

		/* Scan for device 16 func 1 */
		if (PCI_FUNC(pdev->devfn) == 2)
			break;
	}

	pvt->fsb_error_regs = pdev;

	edac_dbg(1, "System Address, processor bus- PCI Bus ID: %s  %x:%x\n",
		 pci_name(pvt->system_address),
		 pvt->system_address->vendor, pvt->system_address->device);
	edac_dbg(1, "Branchmap, control and errors - PCI Bus ID: %s  %x:%x\n",
		 pci_name(pvt->branchmap_werrors),
		 pvt->branchmap_werrors->vendor,
		 pvt->branchmap_werrors->device);
	edac_dbg(1, "FSB Error Regs - PCI Bus ID: %s  %x:%x\n",
		 pci_name(pvt->fsb_error_regs),
		 pvt->fsb_error_regs->vendor, pvt->fsb_error_regs->device);

	pdev = NULL;
	pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
			PCI_DEVICE_ID_I5000_BRANCH_0, pdev);

	if (pdev == NULL) {
		i5000_printk(KERN_ERR,
			"MC: 'BRANCH 0' device not found:"
			"vendor 0x%x device 0x%x Func 0 (broken BIOS?)\n",
			PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_I5000_BRANCH_0);

		pci_dev_put(pvt->branchmap_werrors);
		pci_dev_put(pvt->fsb_error_regs);
		return 1;
	}

	pvt->branch_0 = pdev;

	/* If this device claims to have more than 2 channels then
	 * fetch Branch 1's information
	 */
	if (pvt->maxch >= CHANNELS_PER_BRANCH) {
		pdev = NULL;
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
				PCI_DEVICE_ID_I5000_BRANCH_1, pdev);

		if (pdev == NULL) {
			i5000_printk(KERN_ERR,
				"MC: 'BRANCH 1' device not found:"
				"vendor 0x%x device 0x%x Func 0 "
				"(broken BIOS?)\n",
				PCI_VENDOR_ID_INTEL,
				PCI_DEVICE_ID_I5000_BRANCH_1);

			pci_dev_put(pvt->branchmap_werrors);
			pci_dev_put(pvt->fsb_error_regs);
			pci_dev_put(pvt->branch_0);
			return 1;
		}

		pvt->branch_1 = pdev;
	}

	return 0;
}

/*
 *	i5000_put_devices	'put' all the devices that we have
 *				reserved via 'get'
 */
static void i5000_put_devices(struct mem_ctl_info *mci)
{
	struct i5000_pvt *pvt;

	pvt = mci->pvt_info;

	pci_dev_put(pvt->branchmap_werrors);	/* FUNC 1 */
	pci_dev_put(pvt->fsb_error_regs);	/* FUNC 2 */
	pci_dev_put(pvt->branch_0);	/* DEV 21 */

	/* Only if more than 2 channels do we release the second branch */
	if (pvt->maxch >= CHANNELS_PER_BRANCH)
		pci_dev_put(pvt->branch_1);	/* DEV 22 */
}

/*
 *	determine_amb_resent
 *
 *		the information is contained in NUM_MTRS different registers
 *		determineing which of the NUM_MTRS requires knowing
 *		which channel is in question
 *
 *	2 branches, each with 2 channels
 *		b0_ambpresent0 for channel '0'
 *		b0_ambpresent1 for channel '1'
 *		b1_ambpresent0 for channel '2'
 *		b1_ambpresent1 for channel '3'
 */
static int determine_amb_present_reg(struct i5000_pvt *pvt, int channel)
{
	int amb_present;

	if (channel < CHANNELS_PER_BRANCH) {
		if (channel & 0x1)
			amb_present = pvt->b0_ambpresent1;
		else
			amb_present = pvt->b0_ambpresent0;
	} else {
		if (channel & 0x1)
			amb_present = pvt->b1_ambpresent1;
		else
			amb_present = pvt->b1_ambpresent0;
	}

	return amb_present;
}

/*
 * determine_mtr(pvt, csrow, channel)
 *
 *	return the proper MTR register as determine by the csrow and channel desired
 */
static int determine_mtr(struct i5000_pvt *pvt, int slot, int channel)
{
	int mtr;

	if (channel < CHANNELS_PER_BRANCH)
		mtr = pvt->b0_mtr[slot];
	else
		mtr = pvt->b1_mtr[slot];

	return mtr;
}

/*
 */
static void decode_mtr(int slot_row, u16 mtr)
{
	int ans;

	ans = MTR_DIMMS_PRESENT(mtr);

	edac_dbg(2, "\tMTR%d=0x%x:  DIMMs are %sPresent\n",
		 slot_row, mtr, ans ? "" : "NOT ");
	if (!ans)
		return;

	edac_dbg(2, "\t\tWIDTH: x%d\n", MTR_DRAM_WIDTH(mtr));
	edac_dbg(2, "\t\tNUMBANK: %d bank(s)\n", MTR_DRAM_BANKS(mtr));
	edac_dbg(2, "\t\tNUMRANK: %s\n",
		 MTR_DIMM_RANK(mtr) ? "double" : "single");
	edac_dbg(2, "\t\tNUMROW: %s\n",
		 MTR_DIMM_ROWS(mtr) == 0 ? "8,192 - 13 rows" :
		 MTR_DIMM_ROWS(mtr) == 1 ? "16,384 - 14 rows" :
		 MTR_DIMM_ROWS(mtr) == 2 ? "32,768 - 15 rows" :
		 "reserved");
	edac_dbg(2, "\t\tNUMCOL: %s\n",
		 MTR_DIMM_COLS(mtr) == 0 ? "1,024 - 10 columns" :
		 MTR_DIMM_COLS(mtr) == 1 ? "2,048 - 11 columns" :
		 MTR_DIMM_COLS(mtr) == 2 ? "4,096 - 12 columns" :
		 "reserved");
}

static void handle_channel(struct i5000_pvt *pvt, int slot, int channel,
			struct i5000_dimm_info *dinfo)
{
	int mtr;
	int amb_present_reg;
	int addrBits;

	mtr = determine_mtr(pvt, slot, channel);
	if (MTR_DIMMS_PRESENT(mtr)) {
		amb_present_reg = determine_amb_present_reg(pvt, channel);

		/* Determine if there is a DIMM present in this DIMM slot */
		if (amb_present_reg) {
			dinfo->dual_rank = MTR_DIMM_RANK(mtr);

			/* Start with the number of bits for a Bank
				* on the DRAM */
			addrBits = MTR_DRAM_BANKS_ADDR_BITS(mtr);
			/* Add the number of ROW bits */
			addrBits += MTR_DIMM_ROWS_ADDR_BITS(mtr);
			/* add the number of COLUMN bits */
			addrBits += MTR_DIMM_COLS_ADDR_BITS(mtr);

			/* Dual-rank memories have twice the size */
			if (dinfo->dual_rank)
				addrBits++;

			addrBits += 6;	/* add 64 bits per DIMM */
			addrBits -= 20;	/* divide by 2^^20 */
			addrBits -= 3;	/* 8 bits per bytes */

			dinfo->megabytes = 1 << addrBits;
		}
	}
}

/*
 *	calculate_dimm_size
 *
 *	also will output a DIMM matrix map, if debug is enabled, for viewing
 *	how the DIMMs are populated
 */
static void calculate_dimm_size(struct i5000_pvt *pvt)
{
	struct i5000_dimm_info *dinfo;
	int slot, channel, branch;
	char *p, *mem_buffer;
	int space, n;

	/* ================= Generate some debug output ================= */
	space = PAGE_SIZE;
	mem_buffer = p = kmalloc(space, GFP_KERNEL);
	if (p == NULL) {
		i5000_printk(KERN_ERR, "MC: %s:%s() kmalloc() failed\n",
			__FILE__, __func__);
		return;
	}

	/* Scan all the actual slots
	 * and calculate the information for each DIMM
	 * Start with the highest slot first, to display it first
	 * and work toward the 0th slot
	 */
	for (slot = pvt->maxdimmperch - 1; slot >= 0; slot--) {

		/* on an odd slot, first output a 'boundary' marker,
		 * then reset the message buffer  */
		if (slot & 0x1) {
			n = snprintf(p, space, "--------------------------"
				"--------------------------------");
			p += n;
			space -= n;
			edac_dbg(2, "%s\n", mem_buffer);
			p = mem_buffer;
			space = PAGE_SIZE;
		}
		n = snprintf(p, space, "slot %2d    ", slot);
		p += n;
		space -= n;

		for (channel = 0; channel < pvt->maxch; channel++) {
			dinfo = &pvt->dimm_info[slot][channel];
			handle_channel(pvt, slot, channel, dinfo);
			if (dinfo->megabytes)
				n = snprintf(p, space, "%4d MB %dR| ",
					     dinfo->megabytes, dinfo->dual_rank + 1);
			else
				n = snprintf(p, space, "%4d MB   | ", 0);
			p += n;
			space -= n;
		}
		p += n;
		space -= n;
		edac_dbg(2, "%s\n", mem_buffer);
		p = mem_buffer;
		space = PAGE_SIZE;
	}

	/* Output the last bottom 'boundary' marker */
	n = snprintf(p, space, "--------------------------"
		"--------------------------------");
	p += n;
	space -= n;
	edac_dbg(2, "%s\n", mem_buffer);
	p = mem_buffer;
	space = PAGE_SIZE;

	/* now output the 'channel' labels */
	n = snprintf(p, space, "           ");
	p += n;
	space -= n;
	for (channel = 0; channel < pvt->maxch; channel++) {
		n = snprintf(p, space, "channel %d | ", channel);
		p += n;
		space -= n;
	}
	edac_dbg(2, "%s\n", mem_buffer);
	p = mem_buffer;
	space = PAGE_SIZE;

	n = snprintf(p, space, "           ");
	p += n;
	for (branch = 0; branch < MAX_BRANCHES; branch++) {
		n = snprintf(p, space, "       branch %d       | ", branch);
		p += n;
		space -= n;
	}

	/* output the last message and free buffer */
	edac_dbg(2, "%s\n", mem_buffer);
	kfree(mem_buffer);
}

/*
 *	i5000_get_mc_regs	read in the necessary registers and
 *				cache locally
 *
 *			Fills in the private data members
 */
static void i5000_get_mc_regs(struct mem_ctl_info *mci)
{
	struct i5000_pvt *pvt;
	u32 actual_tolm;
	u16 limit;
	int slot_row;
	int maxch;
	int maxdimmperch;
	int way0, way1;

	pvt = mci->pvt_info;

	pci_read_config_dword(pvt->system_address, AMBASE,
			&pvt->u.ambase_bottom);
	pci_read_config_dword(pvt->system_address, AMBASE + sizeof(u32),
			&pvt->u.ambase_top);

	maxdimmperch = pvt->maxdimmperch;
	maxch = pvt->maxch;

	edac_dbg(2, "AMBASE= 0x%lx  MAXCH= %d  MAX-DIMM-Per-CH= %d\n",
		 (long unsigned int)pvt->ambase, pvt->maxch, pvt->maxdimmperch);

	/* Get the Branch Map regs */
	pci_read_config_word(pvt->branchmap_werrors, TOLM, &pvt->tolm);
	pvt->tolm >>= 12;
	edac_dbg(2, "TOLM (number of 256M regions) =%u (0x%x)\n",
		 pvt->tolm, pvt->tolm);

	actual_tolm = pvt->tolm << 28;
	edac_dbg(2, "Actual TOLM byte addr=%u (0x%x)\n",
		 actual_tolm, actual_tolm);

	pci_read_config_word(pvt->branchmap_werrors, MIR0, &pvt->mir0);
	pci_read_config_word(pvt->branchmap_werrors, MIR1, &pvt->mir1);
	pci_read_config_word(pvt->branchmap_werrors, MIR2, &pvt->mir2);

	/* Get the MIR[0-2] regs */
	limit = (pvt->mir0 >> 4) & 0x0FFF;
	way0 = pvt->mir0 & 0x1;
	way1 = pvt->mir0 & 0x2;
	edac_dbg(2, "MIR0: limit= 0x%x  WAY1= %u  WAY0= %x\n",
		 limit, way1, way0);
	limit = (pvt->mir1 >> 4) & 0x0FFF;
	way0 = pvt->mir1 & 0x1;
	way1 = pvt->mir1 & 0x2;
	edac_dbg(2, "MIR1: limit= 0x%x  WAY1= %u  WAY0= %x\n",
		 limit, way1, way0);
	limit = (pvt->mir2 >> 4) & 0x0FFF;
	way0 = pvt->mir2 & 0x1;
	way1 = pvt->mir2 & 0x2;
	edac_dbg(2, "MIR2: limit= 0x%x  WAY1= %u  WAY0= %x\n",
		 limit, way1, way0);

	/* Get the MTR[0-3] regs */
	for (slot_row = 0; slot_row < NUM_MTRS; slot_row++) {
		int where = MTR0 + (slot_row * sizeof(u32));

		pci_read_config_word(pvt->branch_0, where,
				&pvt->b0_mtr[slot_row]);

		edac_dbg(2, "MTR%d where=0x%x B0 value=0x%x\n",
			 slot_row, where, pvt->b0_mtr[slot_row]);

		if (pvt->maxch >= CHANNELS_PER_BRANCH) {
			pci_read_config_word(pvt->branch_1, where,
					&pvt->b1_mtr[slot_row]);
			edac_dbg(2, "MTR%d where=0x%x B1 value=0x%x\n",
				 slot_row, where, pvt->b1_mtr[slot_row]);
		} else {
			pvt->b1_mtr[slot_row] = 0;
		}
	}

	/* Read and dump branch 0's MTRs */
	edac_dbg(2, "Memory Technology Registers:\n");
	edac_dbg(2, "   Branch 0:\n");
	for (slot_row = 0; slot_row < NUM_MTRS; slot_row++) {
		decode_mtr(slot_row, pvt->b0_mtr[slot_row]);
	}
	pci_read_config_word(pvt->branch_0, AMB_PRESENT_0,
			&pvt->b0_ambpresent0);
	edac_dbg(2, "\t\tAMB-Branch 0-present0 0x%x:\n", pvt->b0_ambpresent0);
	pci_read_config_word(pvt->branch_0, AMB_PRESENT_1,
			&pvt->b0_ambpresent1);
	edac_dbg(2, "\t\tAMB-Branch 0-present1 0x%x:\n", pvt->b0_ambpresent1);

	/* Only if we have 2 branchs (4 channels) */
	if (pvt->maxch < CHANNELS_PER_BRANCH) {
		pvt->b1_ambpresent0 = 0;
		pvt->b1_ambpresent1 = 0;
	} else {
		/* Read and dump  branch 1's MTRs */
		edac_dbg(2, "   Branch 1:\n");
		for (slot_row = 0; slot_row < NUM_MTRS; slot_row++) {
			decode_mtr(slot_row, pvt->b1_mtr[slot_row]);
		}
		pci_read_config_word(pvt->branch_1, AMB_PRESENT_0,
				&pvt->b1_ambpresent0);
		edac_dbg(2, "\t\tAMB-Branch 1-present0 0x%x:\n",
			 pvt->b1_ambpresent0);
		pci_read_config_word(pvt->branch_1, AMB_PRESENT_1,
				&pvt->b1_ambpresent1);
		edac_dbg(2, "\t\tAMB-Branch 1-present1 0x%x:\n",
			 pvt->b1_ambpresent1);
	}

	/* Go and determine the size of each DIMM and place in an
	 * orderly matrix */
	calculate_dimm_size(pvt);
}

/*
 *	i5000_init_csrows	Initialize the 'csrows' table within
 *				the mci control	structure with the
 *				addressing of memory.
 *
 *	return:
 *		0	success
 *		1	no actual memory found on this MC
 */
static int i5000_init_csrows(struct mem_ctl_info *mci)
{
	struct i5000_pvt *pvt;
	struct dimm_info *dimm;
	int empty, channel_count;
	int max_csrows;
	int mtr;
	int csrow_megs;
	int channel;
	int slot;

	pvt = mci->pvt_info;

	channel_count = pvt->maxch;
	max_csrows = pvt->maxdimmperch * 2;

	empty = 1;		/* Assume NO memory */

	/*
	 * FIXME: The memory layout used to map slot/channel into the
	 * real memory architecture is weird: branch+slot are "csrows"
	 * and channel is channel. That required an extra array (dimm_info)
	 * to map the dimms. A good cleanup would be to remove this array,
	 * and do a loop here with branch, channel, slot
	 */
	for (slot = 0; slot < max_csrows; slot++) {
		for (channel = 0; channel < pvt->maxch; channel++) {

			mtr = determine_mtr(pvt, slot, channel);

			if (!MTR_DIMMS_PRESENT(mtr))
				continue;

			dimm = EDAC_DIMM_PTR(mci->layers, mci->dimms, mci->n_layers,
				       channel / MAX_BRANCHES,
				       channel % MAX_BRANCHES, slot);

			csrow_megs = pvt->dimm_info[slot][channel].megabytes;
			dimm->grain = 8;

			/* Assume DDR2 for now */
			dimm->mtype = MEM_FB_DDR2;

			/* ask what device type on this row */
			if (MTR_DRAM_WIDTH(mtr) == 8)
				dimm->dtype = DEV_X8;
			else
				dimm->dtype = DEV_X4;

			dimm->edac_mode = EDAC_S8ECD8ED;
			dimm->nr_pages = csrow_megs << 8;
		}

		empty = 0;
	}

	return empty;
}

/*
 *	i5000_enable_error_reporting
 *			Turn on the memory reporting features of the hardware
 */
static void i5000_enable_error_reporting(struct mem_ctl_info *mci)
{
	struct i5000_pvt *pvt;
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

/*
 * i5000_get_dimm_and_channel_counts(pdev, &nr_csrows, &num_channels)
 *
 *	ask the device how many channels are present and how many CSROWS
 *	 as well
 */
static void i5000_get_dimm_and_channel_counts(struct pci_dev *pdev,
					int *num_dimms_per_channel,
					int *num_channels)
{
	u8 value;

	/* Need to retrieve just how many channels and dimms per channel are
	 * supported on this memory controller
	 */
	pci_read_config_byte(pdev, MAXDIMMPERCH, &value);
	*num_dimms_per_channel = (int)value;

	pci_read_config_byte(pdev, MAXCH, &value);
	*num_channels = (int)value;
}

/*
 *	i5000_probe1	Probe for ONE instance of device to see if it is
 *			present.
 *	return:
 *		0 for FOUND a device
 *		< 0 for error code
 */
static int i5000_probe1(struct pci_dev *pdev, int dev_idx)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[3];
	struct i5000_pvt *pvt;
	int num_channels;
	int num_dimms_per_channel;

	edac_dbg(0, "MC: pdev bus %u dev=0x%x fn=0x%x\n",
		 pdev->bus->number,
		 PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

	/* We only are looking for func 0 of the set */
	if (PCI_FUNC(pdev->devfn) != 0)
		return -ENODEV;

	/* Ask the devices for the number of CSROWS and CHANNELS so
	 * that we can calculate the memory resources, etc
	 *
	 * The Chipset will report what it can handle which will be greater
	 * or equal to what the motherboard manufacturer will implement.
	 *
	 * As we don't have a motherboard identification routine to determine
	 * actual number of slots/dimms per channel, we thus utilize the
	 * resource as specified by the chipset. Thus, we might have
	 * have more DIMMs per channel than actually on the mobo, but this
	 * allows the driver to support up to the chipset max, without
	 * some fancy mobo determination.
	 */
	i5000_get_dimm_and_channel_counts(pdev, &num_dimms_per_channel,
					&num_channels);

	edac_dbg(0, "MC: Number of Branches=2 Channels= %d  DIMMS= %d\n",
		 num_channels, num_dimms_per_channel);

	/* allocate a new MC control structure */

	layers[0].type = EDAC_MC_LAYER_BRANCH;
	layers[0].size = MAX_BRANCHES;
	layers[0].is_virt_csrow = false;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = num_channels / MAX_BRANCHES;
	layers[1].is_virt_csrow = false;
	layers[2].type = EDAC_MC_LAYER_SLOT;
	layers[2].size = num_dimms_per_channel;
	layers[2].is_virt_csrow = true;
	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers, sizeof(*pvt));
	if (mci == NULL)
		return -ENOMEM;

	edac_dbg(0, "MC: mci = %p\n", mci);

	mci->pdev = &pdev->dev;	/* record ptr  to the generic device */

	pvt = mci->pvt_info;
	pvt->system_address = pdev;	/* Record this device in our private */
	pvt->maxch = num_channels;
	pvt->maxdimmperch = num_dimms_per_channel;

	/* 'get' the pci devices we want to reserve for our use */
	if (i5000_get_devices(mci, dev_idx))
		goto fail0;

	/* Time to get serious */
	i5000_get_mc_regs(mci);	/* retrieve the hardware registers */

	mci->mc_idx = 0;
	mci->mtype_cap = MEM_FLAG_FB_DDR2;
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = "i5000_edac.c";
	mci->mod_ver = I5000_REVISION;
	mci->ctl_name = i5000_devs[dev_idx].ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->ctl_page_to_phys = NULL;

	/* Set the function pointer to an actual operation function */
	mci->edac_check = i5000_check_error;

	/* initialize the MC control structure 'csrows' table
	 * with the mapping and control information */
	if (i5000_init_csrows(mci)) {
		edac_dbg(0, "MC: Setting mci->edac_cap to EDAC_FLAG_NONE because i5000_init_csrows() returned nonzero value\n");
		mci->edac_cap = EDAC_FLAG_NONE;	/* no csrows found */
	} else {
		edac_dbg(1, "MC: Enable error reporting now\n");
		i5000_enable_error_reporting(mci);
	}

	/* add this new MC control structure to EDAC's list of MCs */
	if (edac_mc_add_mc(mci)) {
		edac_dbg(0, "MC: failed edac_mc_add_mc()\n");
		/* FIXME: perhaps some code should go here that disables error
		 * reporting if we just enabled it
		 */
		goto fail1;
	}

	i5000_clear_error(mci);

	/* allocating generic PCI control info */
	i5000_pci = edac_pci_create_generic_ctl(&pdev->dev, EDAC_MOD_STR);
	if (!i5000_pci) {
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

	i5000_put_devices(mci);

fail0:
	edac_mc_free(mci);
	return -ENODEV;
}

/*
 *	i5000_init_one	constructor for one instance of device
 *
 * 	returns:
 *		negative on error
 *		count (>= 0)
 */
static int i5000_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int rc;

	edac_dbg(0, "MC:\n");

	/* wake up device */
	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	/* now probe and enable the device */
	return i5000_probe1(pdev, id->driver_data);
}

/*
 *	i5000_remove_one	destructor for one instance of device
 *
 */
static void i5000_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;

	edac_dbg(0, "\n");

	if (i5000_pci)
		edac_pci_release_generic_ctl(i5000_pci);

	if ((mci = edac_mc_del_mc(&pdev->dev)) == NULL)
		return;

	/* retrieve references to resources, and free those resources */
	i5000_put_devices(mci);
	edac_mc_free(mci);
}

/*
 *	pci_device_id	table for which devices we are looking for
 *
 *	The "E500P" device is the first device supported.
 */
static const struct pci_device_id i5000_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_I5000_DEV16),
	 .driver_data = I5000P},

	{0,}			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, i5000_pci_tbl);

/*
 *	i5000_driver	pci_driver structure for this module
 *
 */
static struct pci_driver i5000_driver = {
	.name = KBUILD_BASENAME,
	.probe = i5000_init_one,
	.remove = i5000_remove_one,
	.id_table = i5000_pci_tbl,
};

/*
 *	i5000_init		Module entry function
 *			Try to initialize this module for its devices
 */
static int __init i5000_init(void)
{
	int pci_rc;

	edac_dbg(2, "MC:\n");

       /* Ensure that the OPSTATE is set correctly for POLL or NMI */
       opstate_init();

	pci_rc = pci_register_driver(&i5000_driver);

	return (pci_rc < 0) ? pci_rc : 0;
}

/*
 *	i5000_exit()	Module exit function
 *			Unregister the driver
 */
static void __exit i5000_exit(void)
{
	edac_dbg(2, "MC:\n");
	pci_unregister_driver(&i5000_driver);
}

module_init(i5000_init);
module_exit(i5000_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR
    ("Linux Networx (http://lnxi.com) Doug Thompson <norsk5@xmission.com>");
MODULE_DESCRIPTION("MC Driver for Intel I5000 memory controllers - "
		I5000_REVISION);

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");
module_param(misc_messages, int, 0444);
MODULE_PARM_DESC(misc_messages, "Log miscellaneous non fatal messages");

