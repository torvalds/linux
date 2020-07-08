// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/slab.h>

#include <asm/cpu.h>

#include "mce_amd.h"

static struct amd_decoder_ops fam_ops;

static u8 xec_mask	 = 0xf;

static void (*decode_dram_ecc)(int node_id, struct mce *m);

void amd_register_ecc_decoder(void (*f)(int, struct mce *))
{
	decode_dram_ecc = f;
}
EXPORT_SYMBOL_GPL(amd_register_ecc_decoder);

void amd_unregister_ecc_decoder(void (*f)(int, struct mce *))
{
	if (decode_dram_ecc) {
		WARN_ON(decode_dram_ecc != f);

		decode_dram_ecc = NULL;
	}
}
EXPORT_SYMBOL_GPL(amd_unregister_ecc_decoder);

/*
 * string representation for the different MCA reported error types, see F3x48
 * or MSR0000_0411.
 */

/* transaction type */
static const char * const tt_msgs[] = { "INSN", "DATA", "GEN", "RESV" };

/* cache level */
static const char * const ll_msgs[] = { "RESV", "L1", "L2", "L3/GEN" };

/* memory transaction type */
static const char * const rrrr_msgs[] = {
       "GEN", "RD", "WR", "DRD", "DWR", "IRD", "PRF", "EV", "SNP"
};

/* participating processor */
const char * const pp_msgs[] = { "SRC", "RES", "OBS", "GEN" };
EXPORT_SYMBOL_GPL(pp_msgs);

/* request timeout */
static const char * const to_msgs[] = { "no timeout", "timed out" };

/* memory or i/o */
static const char * const ii_msgs[] = { "MEM", "RESV", "IO", "GEN" };

/* internal error type */
static const char * const uu_msgs[] = { "RESV", "RESV", "HWA", "RESV" };

static const char * const f15h_mc1_mce_desc[] = {
	"UC during a demand linefill from L2",
	"Parity error during data load from IC",
	"Parity error for IC valid bit",
	"Main tag parity error",
	"Parity error in prediction queue",
	"PFB data/address parity error",
	"Parity error in the branch status reg",
	"PFB promotion address error",
	"Tag error during probe/victimization",
	"Parity error for IC probe tag valid bit",
	"PFB non-cacheable bit parity error",
	"PFB valid bit parity error",			/* xec = 0xd */
	"Microcode Patch Buffer",			/* xec = 010 */
	"uop queue",
	"insn buffer",
	"predecode buffer",
	"fetch address FIFO",
	"dispatch uop queue"
};

static const char * const f15h_mc2_mce_desc[] = {
	"Fill ECC error on data fills",			/* xec = 0x4 */
	"Fill parity error on insn fills",
	"Prefetcher request FIFO parity error",
	"PRQ address parity error",
	"PRQ data parity error",
	"WCC Tag ECC error",
	"WCC Data ECC error",
	"WCB Data parity error",
	"VB Data ECC or parity error",
	"L2 Tag ECC error",				/* xec = 0x10 */
	"Hard L2 Tag ECC error",
	"Multiple hits on L2 tag",
	"XAB parity error",
	"PRB address parity error"
};

static const char * const mc4_mce_desc[] = {
	"DRAM ECC error detected on the NB",
	"CRC error detected on HT link",
	"Link-defined sync error packets detected on HT link",
	"HT Master abort",
	"HT Target abort",
	"Invalid GART PTE entry during GART table walk",
	"Unsupported atomic RMW received from an IO link",
	"Watchdog timeout due to lack of progress",
	"DRAM ECC error detected on the NB",
	"SVM DMA Exclusion Vector error",
	"HT data error detected on link",
	"Protocol error (link, L3, probe filter)",
	"NB internal arrays parity error",
	"DRAM addr/ctl signals parity error",
	"IO link transmission error",
	"L3 data cache ECC error",			/* xec = 0x1c */
	"L3 cache tag error",
	"L3 LRU parity bits error",
	"ECC Error in the Probe Filter directory"
};

static const char * const mc5_mce_desc[] = {
	"CPU Watchdog timer expire",
	"Wakeup array dest tag",
	"AG payload array",
	"EX payload array",
	"IDRF array",
	"Retire dispatch queue",
	"Mapper checkpoint array",
	"Physical register file EX0 port",
	"Physical register file EX1 port",
	"Physical register file AG0 port",
	"Physical register file AG1 port",
	"Flag register file",
	"DE error occurred",
	"Retire status queue"
};

static const char * const mc6_mce_desc[] = {
	"Hardware Assertion",
	"Free List",
	"Physical Register File",
	"Retire Queue",
	"Scheduler table",
	"Status Register File",
};

/* Scalable MCA error strings */
static const char * const smca_ls_mce_desc[] = {
	"Load queue parity error",
	"Store queue parity error",
	"Miss address buffer payload parity error",
	"Level 1 TLB parity error",
	"DC Tag error type 5",
	"DC Tag error type 6",
	"DC Tag error type 1",
	"Internal error type 1",
	"Internal error type 2",
	"System Read Data Error Thread 0",
	"System Read Data Error Thread 1",
	"DC Tag error type 2",
	"DC Data error type 1 and poison consumption",
	"DC Data error type 2",
	"DC Data error type 3",
	"DC Tag error type 4",
	"Level 2 TLB parity error",
	"PDC parity error",
	"DC Tag error type 3",
	"DC Tag error type 5",
	"L2 Fill Data error",
};

static const char * const smca_ls2_mce_desc[] = {
	"An ECC error was detected on a data cache read by a probe or victimization",
	"An ECC error or L2 poison was detected on a data cache read by a load",
	"An ECC error was detected on a data cache read-modify-write by a store",
	"An ECC error or poison bit mismatch was detected on a tag read by a probe or victimization",
	"An ECC error or poison bit mismatch was detected on a tag read by a load",
	"An ECC error or poison bit mismatch was detected on a tag read by a store",
	"An ECC error was detected on an EMEM read by a load",
	"An ECC error was detected on an EMEM read-modify-write by a store",
	"A parity error was detected in an L1 TLB entry by any access",
	"A parity error was detected in an L2 TLB entry by any access",
	"A parity error was detected in a PWC entry by any access",
	"A parity error was detected in an STQ entry by any access",
	"A parity error was detected in an LDQ entry by any access",
	"A parity error was detected in a MAB entry by any access",
	"A parity error was detected in an SCB entry state field by any access",
	"A parity error was detected in an SCB entry address field by any access",
	"A parity error was detected in an SCB entry data field by any access",
	"A parity error was detected in a WCB entry by any access",
	"A poisoned line was detected in an SCB entry by any access",
	"A SystemReadDataError error was reported on read data returned from L2 for a load",
	"A SystemReadDataError error was reported on read data returned from L2 for an SCB store",
	"A SystemReadDataError error was reported on read data returned from L2 for a WCB store",
	"A hardware assertion error was reported",
	"A parity error was detected in an STLF, SCB EMEM entry or SRB store data by any access",
};

static const char * const smca_if_mce_desc[] = {
	"Op Cache Microtag Probe Port Parity Error",
	"IC Microtag or Full Tag Multi-hit Error",
	"IC Full Tag Parity Error",
	"IC Data Array Parity Error",
	"Decoupling Queue PhysAddr Parity Error",
	"L0 ITLB Parity Error",
	"L1 ITLB Parity Error",
	"L2 ITLB Parity Error",
	"BPQ Thread 0 Snoop Parity Error",
	"BPQ Thread 1 Snoop Parity Error",
	"L1 BTB Multi-Match Error",
	"L2 BTB Multi-Match Error",
	"L2 Cache Response Poison Error",
	"System Read Data Error",
	"Hardware Assertion Error",
	"L1-TLB Multi-Hit",
	"L2-TLB Multi-Hit",
	"BSR Parity Error",
	"CT MCE",
};

static const char * const smca_l2_mce_desc[] = {
	"L2M Tag Multiple-Way-Hit error",
	"L2M Tag or State Array ECC Error",
	"L2M Data Array ECC Error",
	"Hardware Assert Error",
};

static const char * const smca_de_mce_desc[] = {
	"Micro-op cache tag parity error",
	"Micro-op cache data parity error",
	"Instruction buffer parity error",
	"Micro-op queue parity error",
	"Instruction dispatch queue parity error",
	"Fetch address FIFO parity error",
	"Patch RAM data parity error",
	"Patch RAM sequencer parity error",
	"Micro-op buffer parity error",
	"Hardware Assertion MCA Error",
};

static const char * const smca_ex_mce_desc[] = {
	"Watchdog Timeout error",
	"Physical register file parity error",
	"Flag register file parity error",
	"Immediate displacement register file parity error",
	"Address generator payload parity error",
	"EX payload parity error",
	"Checkpoint queue parity error",
	"Retire dispatch queue parity error",
	"Retire status queue parity error",
	"Scheduling queue parity error",
	"Branch buffer queue parity error",
	"Hardware Assertion error",
	"Spec Map parity error",
	"Retire Map parity error",
};

static const char * const smca_fp_mce_desc[] = {
	"Physical register file (PRF) parity error",
	"Freelist (FL) parity error",
	"Schedule queue parity error",
	"NSQ parity error",
	"Retire queue (RQ) parity error",
	"Status register file (SRF) parity error",
	"Hardware assertion",
};

static const char * const smca_l3_mce_desc[] = {
	"Shadow Tag Macro ECC Error",
	"Shadow Tag Macro Multi-way-hit Error",
	"L3M Tag ECC Error",
	"L3M Tag Multi-way-hit Error",
	"L3M Data ECC Error",
	"SDP Parity Error or SystemReadDataError from XI",
	"L3 Victim Queue Parity Error",
	"L3 Hardware Assertion",
};

static const char * const smca_cs_mce_desc[] = {
	"Illegal Request",
	"Address Violation",
	"Security Violation",
	"Illegal Response",
	"Unexpected Response",
	"Request or Probe Parity Error",
	"Read Response Parity Error",
	"Atomic Request Parity Error",
	"Probe Filter ECC Error",
};

static const char * const smca_cs2_mce_desc[] = {
	"Illegal Request",
	"Address Violation",
	"Security Violation",
	"Illegal Response",
	"Unexpected Response",
	"Request or Probe Parity Error",
	"Read Response Parity Error",
	"Atomic Request Parity Error",
	"SDP read response had no match in the CS queue",
	"Probe Filter Protocol Error",
	"Probe Filter ECC Error",
	"SDP read response had an unexpected RETRY error",
	"Counter overflow error",
	"Counter underflow error",
};

static const char * const smca_pie_mce_desc[] = {
	"Hardware Assert",
	"Register security violation",
	"Link Error",
	"Poison data consumption",
	"A deferred error was detected in the DF"
};

static const char * const smca_umc_mce_desc[] = {
	"DRAM ECC error",
	"Data poison error",
	"SDP parity error",
	"Advanced peripheral bus error",
	"Address/Command parity error",
	"Write data CRC error",
	"DCQ SRAM ECC error",
	"AES SRAM ECC error",
};

static const char * const smca_pb_mce_desc[] = {
	"An ECC error in the Parameter Block RAM array",
};

static const char * const smca_psp_mce_desc[] = {
	"An ECC or parity error in a PSP RAM instance",
};

static const char * const smca_psp2_mce_desc[] = {
	"High SRAM ECC or parity error",
	"Low SRAM ECC or parity error",
	"Instruction Cache Bank 0 ECC or parity error",
	"Instruction Cache Bank 1 ECC or parity error",
	"Instruction Tag Ram 0 parity error",
	"Instruction Tag Ram 1 parity error",
	"Data Cache Bank 0 ECC or parity error",
	"Data Cache Bank 1 ECC or parity error",
	"Data Cache Bank 2 ECC or parity error",
	"Data Cache Bank 3 ECC or parity error",
	"Data Tag Bank 0 parity error",
	"Data Tag Bank 1 parity error",
	"Data Tag Bank 2 parity error",
	"Data Tag Bank 3 parity error",
	"Dirty Data Ram parity error",
	"TLB Bank 0 parity error",
	"TLB Bank 1 parity error",
	"System Hub Read Buffer ECC or parity error",
};

static const char * const smca_smu_mce_desc[] = {
	"An ECC or parity error in an SMU RAM instance",
};

static const char * const smca_smu2_mce_desc[] = {
	"High SRAM ECC or parity error",
	"Low SRAM ECC or parity error",
	"Data Cache Bank A ECC or parity error",
	"Data Cache Bank B ECC or parity error",
	"Data Tag Cache Bank A ECC or parity error",
	"Data Tag Cache Bank B ECC or parity error",
	"Instruction Cache Bank A ECC or parity error",
	"Instruction Cache Bank B ECC or parity error",
	"Instruction Tag Cache Bank A ECC or parity error",
	"Instruction Tag Cache Bank B ECC or parity error",
	"System Hub Read Buffer ECC or parity error",
	"PHY RAM ECC error",
};

static const char * const smca_mp5_mce_desc[] = {
	"High SRAM ECC or parity error",
	"Low SRAM ECC or parity error",
	"Data Cache Bank A ECC or parity error",
	"Data Cache Bank B ECC or parity error",
	"Data Tag Cache Bank A ECC or parity error",
	"Data Tag Cache Bank B ECC or parity error",
	"Instruction Cache Bank A ECC or parity error",
	"Instruction Cache Bank B ECC or parity error",
	"Instruction Tag Cache Bank A ECC or parity error",
	"Instruction Tag Cache Bank B ECC or parity error",
};

static const char * const smca_nbio_mce_desc[] = {
	"ECC or Parity error",
	"PCIE error",
	"SDP ErrEvent error",
	"SDP Egress Poison Error",
	"IOHC Internal Poison Error",
};

static const char * const smca_pcie_mce_desc[] = {
	"CCIX PER Message logging",
	"CCIX Read Response with Status: Non-Data Error",
	"CCIX Write Response with Status: Non-Data Error",
	"CCIX Read Response with Status: Data Error",
	"CCIX Non-okay write response with data error",
};

struct smca_mce_desc {
	const char * const *descs;
	unsigned int num_descs;
};

static struct smca_mce_desc smca_mce_descs[] = {
	[SMCA_LS]	= { smca_ls_mce_desc,	ARRAY_SIZE(smca_ls_mce_desc)	},
	[SMCA_LS_V2]	= { smca_ls2_mce_desc,	ARRAY_SIZE(smca_ls2_mce_desc)	},
	[SMCA_IF]	= { smca_if_mce_desc,	ARRAY_SIZE(smca_if_mce_desc)	},
	[SMCA_L2_CACHE]	= { smca_l2_mce_desc,	ARRAY_SIZE(smca_l2_mce_desc)	},
	[SMCA_DE]	= { smca_de_mce_desc,	ARRAY_SIZE(smca_de_mce_desc)	},
	[SMCA_EX]	= { smca_ex_mce_desc,	ARRAY_SIZE(smca_ex_mce_desc)	},
	[SMCA_FP]	= { smca_fp_mce_desc,	ARRAY_SIZE(smca_fp_mce_desc)	},
	[SMCA_L3_CACHE]	= { smca_l3_mce_desc,	ARRAY_SIZE(smca_l3_mce_desc)	},
	[SMCA_CS]	= { smca_cs_mce_desc,	ARRAY_SIZE(smca_cs_mce_desc)	},
	[SMCA_CS_V2]	= { smca_cs2_mce_desc,	ARRAY_SIZE(smca_cs2_mce_desc)	},
	[SMCA_PIE]	= { smca_pie_mce_desc,	ARRAY_SIZE(smca_pie_mce_desc)	},
	[SMCA_UMC]	= { smca_umc_mce_desc,	ARRAY_SIZE(smca_umc_mce_desc)	},
	[SMCA_PB]	= { smca_pb_mce_desc,	ARRAY_SIZE(smca_pb_mce_desc)	},
	[SMCA_PSP]	= { smca_psp_mce_desc,	ARRAY_SIZE(smca_psp_mce_desc)	},
	[SMCA_PSP_V2]	= { smca_psp2_mce_desc,	ARRAY_SIZE(smca_psp2_mce_desc)	},
	[SMCA_SMU]	= { smca_smu_mce_desc,	ARRAY_SIZE(smca_smu_mce_desc)	},
	[SMCA_SMU_V2]	= { smca_smu2_mce_desc,	ARRAY_SIZE(smca_smu2_mce_desc)	},
	[SMCA_MP5]	= { smca_mp5_mce_desc,	ARRAY_SIZE(smca_mp5_mce_desc)	},
	[SMCA_NBIO]	= { smca_nbio_mce_desc,	ARRAY_SIZE(smca_nbio_mce_desc)	},
	[SMCA_PCIE]	= { smca_pcie_mce_desc,	ARRAY_SIZE(smca_pcie_mce_desc)	},
};

static bool f12h_mc0_mce(u16 ec, u8 xec)
{
	bool ret = false;

	if (MEM_ERROR(ec)) {
		u8 ll = LL(ec);
		ret = true;

		if (ll == LL_L2)
			pr_cont("during L1 linefill from L2.\n");
		else if (ll == LL_L1)
			pr_cont("Data/Tag %s error.\n", R4_MSG(ec));
		else
			ret = false;
	}
	return ret;
}

static bool f10h_mc0_mce(u16 ec, u8 xec)
{
	if (R4(ec) == R4_GEN && LL(ec) == LL_L1) {
		pr_cont("during data scrub.\n");
		return true;
	}
	return f12h_mc0_mce(ec, xec);
}

static bool k8_mc0_mce(u16 ec, u8 xec)
{
	if (BUS_ERROR(ec)) {
		pr_cont("during system linefill.\n");
		return true;
	}

	return f10h_mc0_mce(ec, xec);
}

static bool cat_mc0_mce(u16 ec, u8 xec)
{
	u8 r4	 = R4(ec);
	bool ret = true;

	if (MEM_ERROR(ec)) {

		if (TT(ec) != TT_DATA || LL(ec) != LL_L1)
			return false;

		switch (r4) {
		case R4_DRD:
		case R4_DWR:
			pr_cont("Data/Tag parity error due to %s.\n",
				(r4 == R4_DRD ? "load/hw prf" : "store"));
			break;
		case R4_EVICT:
			pr_cont("Copyback parity error on a tag miss.\n");
			break;
		case R4_SNOOP:
			pr_cont("Tag parity error during snoop.\n");
			break;
		default:
			ret = false;
		}
	} else if (BUS_ERROR(ec)) {

		if ((II(ec) != II_MEM && II(ec) != II_IO) || LL(ec) != LL_LG)
			return false;

		pr_cont("System read data error on a ");

		switch (r4) {
		case R4_RD:
			pr_cont("TLB reload.\n");
			break;
		case R4_DWR:
			pr_cont("store.\n");
			break;
		case R4_DRD:
			pr_cont("load.\n");
			break;
		default:
			ret = false;
		}
	} else {
		ret = false;
	}

	return ret;
}

static bool f15h_mc0_mce(u16 ec, u8 xec)
{
	bool ret = true;

	if (MEM_ERROR(ec)) {

		switch (xec) {
		case 0x0:
			pr_cont("Data Array access error.\n");
			break;

		case 0x1:
			pr_cont("UC error during a linefill from L2/NB.\n");
			break;

		case 0x2:
		case 0x11:
			pr_cont("STQ access error.\n");
			break;

		case 0x3:
			pr_cont("SCB access error.\n");
			break;

		case 0x10:
			pr_cont("Tag error.\n");
			break;

		case 0x12:
			pr_cont("LDQ access error.\n");
			break;

		default:
			ret = false;
		}
	} else if (BUS_ERROR(ec)) {

		if (!xec)
			pr_cont("System Read Data Error.\n");
		else
			pr_cont(" Internal error condition type %d.\n", xec);
	} else if (INT_ERROR(ec)) {
		if (xec <= 0x1f)
			pr_cont("Hardware Assert.\n");
		else
			ret = false;

	} else
		ret = false;

	return ret;
}

static void decode_mc0_mce(struct mce *m)
{
	u16 ec = EC(m->status);
	u8 xec = XEC(m->status, xec_mask);

	pr_emerg(HW_ERR "MC0 Error: ");

	/* TLB error signatures are the same across families */
	if (TLB_ERROR(ec)) {
		if (TT(ec) == TT_DATA) {
			pr_cont("%s TLB %s.\n", LL_MSG(ec),
				((xec == 2) ? "locked miss"
					    : (xec ? "multimatch" : "parity")));
			return;
		}
	} else if (fam_ops.mc0_mce(ec, xec))
		;
	else
		pr_emerg(HW_ERR "Corrupted MC0 MCE info?\n");
}

static bool k8_mc1_mce(u16 ec, u8 xec)
{
	u8 ll	 = LL(ec);
	bool ret = true;

	if (!MEM_ERROR(ec))
		return false;

	if (ll == 0x2)
		pr_cont("during a linefill from L2.\n");
	else if (ll == 0x1) {
		switch (R4(ec)) {
		case R4_IRD:
			pr_cont("Parity error during data load.\n");
			break;

		case R4_EVICT:
			pr_cont("Copyback Parity/Victim error.\n");
			break;

		case R4_SNOOP:
			pr_cont("Tag Snoop error.\n");
			break;

		default:
			ret = false;
			break;
		}
	} else
		ret = false;

	return ret;
}

static bool cat_mc1_mce(u16 ec, u8 xec)
{
	u8 r4    = R4(ec);
	bool ret = true;

	if (!MEM_ERROR(ec))
		return false;

	if (TT(ec) != TT_INSTR)
		return false;

	if (r4 == R4_IRD)
		pr_cont("Data/tag array parity error for a tag hit.\n");
	else if (r4 == R4_SNOOP)
		pr_cont("Tag error during snoop/victimization.\n");
	else if (xec == 0x0)
		pr_cont("Tag parity error from victim castout.\n");
	else if (xec == 0x2)
		pr_cont("Microcode patch RAM parity error.\n");
	else
		ret = false;

	return ret;
}

static bool f15h_mc1_mce(u16 ec, u8 xec)
{
	bool ret = true;

	if (!MEM_ERROR(ec))
		return false;

	switch (xec) {
	case 0x0 ... 0xa:
		pr_cont("%s.\n", f15h_mc1_mce_desc[xec]);
		break;

	case 0xd:
		pr_cont("%s.\n", f15h_mc1_mce_desc[xec-2]);
		break;

	case 0x10:
		pr_cont("%s.\n", f15h_mc1_mce_desc[xec-4]);
		break;

	case 0x11 ... 0x15:
		pr_cont("Decoder %s parity error.\n", f15h_mc1_mce_desc[xec-4]);
		break;

	default:
		ret = false;
	}
	return ret;
}

static void decode_mc1_mce(struct mce *m)
{
	u16 ec = EC(m->status);
	u8 xec = XEC(m->status, xec_mask);

	pr_emerg(HW_ERR "MC1 Error: ");

	if (TLB_ERROR(ec))
		pr_cont("%s TLB %s.\n", LL_MSG(ec),
			(xec ? "multimatch" : "parity error"));
	else if (BUS_ERROR(ec)) {
		bool k8 = (boot_cpu_data.x86 == 0xf && (m->status & BIT_64(58)));

		pr_cont("during %s.\n", (k8 ? "system linefill" : "NB data read"));
	} else if (INT_ERROR(ec)) {
		if (xec <= 0x3f)
			pr_cont("Hardware Assert.\n");
		else
			goto wrong_mc1_mce;
	} else if (fam_ops.mc1_mce(ec, xec))
		;
	else
		goto wrong_mc1_mce;

	return;

wrong_mc1_mce:
	pr_emerg(HW_ERR "Corrupted MC1 MCE info?\n");
}

static bool k8_mc2_mce(u16 ec, u8 xec)
{
	bool ret = true;

	if (xec == 0x1)
		pr_cont(" in the write data buffers.\n");
	else if (xec == 0x3)
		pr_cont(" in the victim data buffers.\n");
	else if (xec == 0x2 && MEM_ERROR(ec))
		pr_cont(": %s error in the L2 cache tags.\n", R4_MSG(ec));
	else if (xec == 0x0) {
		if (TLB_ERROR(ec))
			pr_cont("%s error in a Page Descriptor Cache or Guest TLB.\n",
				TT_MSG(ec));
		else if (BUS_ERROR(ec))
			pr_cont(": %s/ECC error in data read from NB: %s.\n",
				R4_MSG(ec), PP_MSG(ec));
		else if (MEM_ERROR(ec)) {
			u8 r4 = R4(ec);

			if (r4 >= 0x7)
				pr_cont(": %s error during data copyback.\n",
					R4_MSG(ec));
			else if (r4 <= 0x1)
				pr_cont(": %s parity/ECC error during data "
					"access from L2.\n", R4_MSG(ec));
			else
				ret = false;
		} else
			ret = false;
	} else
		ret = false;

	return ret;
}

static bool f15h_mc2_mce(u16 ec, u8 xec)
{
	bool ret = true;

	if (TLB_ERROR(ec)) {
		if (xec == 0x0)
			pr_cont("Data parity TLB read error.\n");
		else if (xec == 0x1)
			pr_cont("Poison data provided for TLB fill.\n");
		else
			ret = false;
	} else if (BUS_ERROR(ec)) {
		if (xec > 2)
			ret = false;

		pr_cont("Error during attempted NB data read.\n");
	} else if (MEM_ERROR(ec)) {
		switch (xec) {
		case 0x4 ... 0xc:
			pr_cont("%s.\n", f15h_mc2_mce_desc[xec - 0x4]);
			break;

		case 0x10 ... 0x14:
			pr_cont("%s.\n", f15h_mc2_mce_desc[xec - 0x7]);
			break;

		default:
			ret = false;
		}
	} else if (INT_ERROR(ec)) {
		if (xec <= 0x3f)
			pr_cont("Hardware Assert.\n");
		else
			ret = false;
	}

	return ret;
}

static bool f16h_mc2_mce(u16 ec, u8 xec)
{
	u8 r4 = R4(ec);

	if (!MEM_ERROR(ec))
		return false;

	switch (xec) {
	case 0x04 ... 0x05:
		pr_cont("%cBUFF parity error.\n", (r4 == R4_RD) ? 'I' : 'O');
		break;

	case 0x09 ... 0x0b:
	case 0x0d ... 0x0f:
		pr_cont("ECC error in L2 tag (%s).\n",
			((r4 == R4_GEN)   ? "BankReq" :
			((r4 == R4_SNOOP) ? "Prb"     : "Fill")));
		break;

	case 0x10 ... 0x19:
	case 0x1b:
		pr_cont("ECC error in L2 data array (%s).\n",
			(((r4 == R4_RD) && !(xec & 0x3)) ? "Hit"  :
			((r4 == R4_GEN)   ? "Attr" :
			((r4 == R4_EVICT) ? "Vict" : "Fill"))));
		break;

	case 0x1c ... 0x1d:
	case 0x1f:
		pr_cont("Parity error in L2 attribute bits (%s).\n",
			((r4 == R4_RD)  ? "Hit"  :
			((r4 == R4_GEN) ? "Attr" : "Fill")));
		break;

	default:
		return false;
	}

	return true;
}

static void decode_mc2_mce(struct mce *m)
{
	u16 ec = EC(m->status);
	u8 xec = XEC(m->status, xec_mask);

	pr_emerg(HW_ERR "MC2 Error: ");

	if (!fam_ops.mc2_mce(ec, xec))
		pr_cont(HW_ERR "Corrupted MC2 MCE info?\n");
}

static void decode_mc3_mce(struct mce *m)
{
	u16 ec = EC(m->status);
	u8 xec = XEC(m->status, xec_mask);

	if (boot_cpu_data.x86 >= 0x14) {
		pr_emerg("You shouldn't be seeing MC3 MCE on this cpu family,"
			 " please report on LKML.\n");
		return;
	}

	pr_emerg(HW_ERR "MC3 Error");

	if (xec == 0x0) {
		u8 r4 = R4(ec);

		if (!BUS_ERROR(ec) || (r4 != R4_DRD && r4 != R4_DWR))
			goto wrong_mc3_mce;

		pr_cont(" during %s.\n", R4_MSG(ec));
	} else
		goto wrong_mc3_mce;

	return;

 wrong_mc3_mce:
	pr_emerg(HW_ERR "Corrupted MC3 MCE info?\n");
}

static void decode_mc4_mce(struct mce *m)
{
	unsigned int fam = x86_family(m->cpuid);
	int node_id = amd_get_nb_id(m->extcpu);
	u16 ec = EC(m->status);
	u8 xec = XEC(m->status, 0x1f);
	u8 offset = 0;

	pr_emerg(HW_ERR "MC4 Error (node %d): ", node_id);

	switch (xec) {
	case 0x0 ... 0xe:

		/* special handling for DRAM ECCs */
		if (xec == 0x0 || xec == 0x8) {
			/* no ECCs on F11h */
			if (fam == 0x11)
				goto wrong_mc4_mce;

			pr_cont("%s.\n", mc4_mce_desc[xec]);

			if (decode_dram_ecc)
				decode_dram_ecc(node_id, m);
			return;
		}
		break;

	case 0xf:
		if (TLB_ERROR(ec))
			pr_cont("GART Table Walk data error.\n");
		else if (BUS_ERROR(ec))
			pr_cont("DMA Exclusion Vector Table Walk error.\n");
		else
			goto wrong_mc4_mce;
		return;

	case 0x19:
		if (fam == 0x15 || fam == 0x16)
			pr_cont("Compute Unit Data Error.\n");
		else
			goto wrong_mc4_mce;
		return;

	case 0x1c ... 0x1f:
		offset = 13;
		break;

	default:
		goto wrong_mc4_mce;
	}

	pr_cont("%s.\n", mc4_mce_desc[xec - offset]);
	return;

 wrong_mc4_mce:
	pr_emerg(HW_ERR "Corrupted MC4 MCE info?\n");
}

static void decode_mc5_mce(struct mce *m)
{
	unsigned int fam = x86_family(m->cpuid);
	u16 ec = EC(m->status);
	u8 xec = XEC(m->status, xec_mask);

	if (fam == 0xf || fam == 0x11)
		goto wrong_mc5_mce;

	pr_emerg(HW_ERR "MC5 Error: ");

	if (INT_ERROR(ec)) {
		if (xec <= 0x1f) {
			pr_cont("Hardware Assert.\n");
			return;
		} else
			goto wrong_mc5_mce;
	}

	if (xec == 0x0 || xec == 0xc)
		pr_cont("%s.\n", mc5_mce_desc[xec]);
	else if (xec <= 0xd)
		pr_cont("%s parity error.\n", mc5_mce_desc[xec]);
	else
		goto wrong_mc5_mce;

	return;

 wrong_mc5_mce:
	pr_emerg(HW_ERR "Corrupted MC5 MCE info?\n");
}

static void decode_mc6_mce(struct mce *m)
{
	u8 xec = XEC(m->status, xec_mask);

	pr_emerg(HW_ERR "MC6 Error: ");

	if (xec > 0x5)
		goto wrong_mc6_mce;

	pr_cont("%s parity error.\n", mc6_mce_desc[xec]);
	return;

 wrong_mc6_mce:
	pr_emerg(HW_ERR "Corrupted MC6 MCE info?\n");
}

/* Decode errors according to Scalable MCA specification */
static void decode_smca_error(struct mce *m)
{
	struct smca_hwid *hwid;
	enum smca_bank_types bank_type;
	const char *ip_name;
	u8 xec = XEC(m->status, xec_mask);

	if (m->bank >= ARRAY_SIZE(smca_banks))
		return;

	hwid = smca_banks[m->bank].hwid;
	if (!hwid)
		return;

	bank_type = hwid->bank_type;

	if (bank_type == SMCA_RESERVED) {
		pr_emerg(HW_ERR "Bank %d is reserved.\n", m->bank);
		return;
	}

	ip_name = smca_get_long_name(bank_type);

	pr_emerg(HW_ERR "%s Ext. Error Code: %d", ip_name, xec);

	/* Only print the decode of valid error codes */
	if (xec < smca_mce_descs[bank_type].num_descs &&
			(hwid->xec_bitmap & BIT_ULL(xec))) {
		pr_cont(", %s.\n", smca_mce_descs[bank_type].descs[xec]);
	}

	if (bank_type == SMCA_UMC && xec == 0 && decode_dram_ecc)
		decode_dram_ecc(cpu_to_node(m->extcpu), m);
}

static inline void amd_decode_err_code(u16 ec)
{
	if (INT_ERROR(ec)) {
		pr_emerg(HW_ERR "internal: %s\n", UU_MSG(ec));
		return;
	}

	pr_emerg(HW_ERR "cache level: %s", LL_MSG(ec));

	if (BUS_ERROR(ec))
		pr_cont(", mem/io: %s", II_MSG(ec));
	else
		pr_cont(", tx: %s", TT_MSG(ec));

	if (MEM_ERROR(ec) || BUS_ERROR(ec)) {
		pr_cont(", mem-tx: %s", R4_MSG(ec));

		if (BUS_ERROR(ec))
			pr_cont(", part-proc: %s (%s)", PP_MSG(ec), TO_MSG(ec));
	}

	pr_cont("\n");
}

static const char *decode_error_status(struct mce *m)
{
	if (m->status & MCI_STATUS_UC) {
		if (m->status & MCI_STATUS_PCC)
			return "System Fatal error.";
		if (m->mcgstatus & MCG_STATUS_RIPV)
			return "Uncorrected, software restartable error.";
		return "Uncorrected, software containable error.";
	}

	if (m->status & MCI_STATUS_DEFERRED)
		return "Deferred error, no action required.";

	return "Corrected error, no action required.";
}

static int
amd_decode_mce(struct notifier_block *nb, unsigned long val, void *data)
{
	struct mce *m = (struct mce *)data;
	unsigned int fam = x86_family(m->cpuid);
	int ecc;

	if (m->kflags & MCE_HANDLED_CEC)
		return NOTIFY_DONE;

	pr_emerg(HW_ERR "%s\n", decode_error_status(m));

	pr_emerg(HW_ERR "CPU:%d (%x:%x:%x) MC%d_STATUS[%s|%s|%s|%s|%s",
		m->extcpu,
		fam, x86_model(m->cpuid), x86_stepping(m->cpuid),
		m->bank,
		((m->status & MCI_STATUS_OVER)	? "Over"  : "-"),
		((m->status & MCI_STATUS_UC)	? "UE"	  :
		 (m->status & MCI_STATUS_DEFERRED) ? "-"  : "CE"),
		((m->status & MCI_STATUS_MISCV)	? "MiscV" : "-"),
		((m->status & MCI_STATUS_ADDRV)	? "AddrV" : "-"),
		((m->status & MCI_STATUS_PCC)	? "PCC"	  : "-"));

	if (boot_cpu_has(X86_FEATURE_SMCA)) {
		u32 low, high;
		u32 addr = MSR_AMD64_SMCA_MCx_CONFIG(m->bank);

		if (!rdmsr_safe(addr, &low, &high) &&
		    (low & MCI_CONFIG_MCAX))
			pr_cont("|%s", ((m->status & MCI_STATUS_TCC) ? "TCC" : "-"));

		pr_cont("|%s", ((m->status & MCI_STATUS_SYNDV) ? "SyndV" : "-"));
	}

	/* do the two bits[14:13] together */
	ecc = (m->status >> 45) & 0x3;
	if (ecc)
		pr_cont("|%sECC", ((ecc == 2) ? "C" : "U"));

	if (fam >= 0x15) {
		pr_cont("|%s", (m->status & MCI_STATUS_DEFERRED ? "Deferred" : "-"));

		/* F15h, bank4, bit 43 is part of McaStatSubCache. */
		if (fam != 0x15 || m->bank != 4)
			pr_cont("|%s", (m->status & MCI_STATUS_POISON ? "Poison" : "-"));
	}

	if (fam >= 0x17)
		pr_cont("|%s", (m->status & MCI_STATUS_SCRUB ? "Scrub" : "-"));

	pr_cont("]: 0x%016llx\n", m->status);

	if (m->status & MCI_STATUS_ADDRV)
		pr_emerg(HW_ERR "Error Addr: 0x%016llx\n", m->addr);

	if (m->ppin)
		pr_emerg(HW_ERR "PPIN: 0x%016llx\n", m->ppin);

	if (boot_cpu_has(X86_FEATURE_SMCA)) {
		pr_emerg(HW_ERR "IPID: 0x%016llx", m->ipid);

		if (m->status & MCI_STATUS_SYNDV)
			pr_cont(", Syndrome: 0x%016llx", m->synd);

		pr_cont("\n");

		decode_smca_error(m);
		goto err_code;
	}

	if (m->tsc)
		pr_emerg(HW_ERR "TSC: %llu\n", m->tsc);

	/* Doesn't matter which member to test. */
	if (!fam_ops.mc0_mce)
		goto err_code;

	switch (m->bank) {
	case 0:
		decode_mc0_mce(m);
		break;

	case 1:
		decode_mc1_mce(m);
		break;

	case 2:
		decode_mc2_mce(m);
		break;

	case 3:
		decode_mc3_mce(m);
		break;

	case 4:
		decode_mc4_mce(m);
		break;

	case 5:
		decode_mc5_mce(m);
		break;

	case 6:
		decode_mc6_mce(m);
		break;

	default:
		break;
	}

 err_code:
	amd_decode_err_code(m->status & 0xffff);

	m->kflags |= MCE_HANDLED_EDAC;
	return NOTIFY_OK;
}

static struct notifier_block amd_mce_dec_nb = {
	.notifier_call	= amd_decode_mce,
	.priority	= MCE_PRIO_EDAC,
};

static int __init mce_amd_init(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if (c->x86_vendor != X86_VENDOR_AMD &&
	    c->x86_vendor != X86_VENDOR_HYGON)
		return -ENODEV;

	if (boot_cpu_has(X86_FEATURE_SMCA)) {
		xec_mask = 0x3f;
		goto out;
	}

	switch (c->x86) {
	case 0xf:
		fam_ops.mc0_mce = k8_mc0_mce;
		fam_ops.mc1_mce = k8_mc1_mce;
		fam_ops.mc2_mce = k8_mc2_mce;
		break;

	case 0x10:
		fam_ops.mc0_mce = f10h_mc0_mce;
		fam_ops.mc1_mce = k8_mc1_mce;
		fam_ops.mc2_mce = k8_mc2_mce;
		break;

	case 0x11:
		fam_ops.mc0_mce = k8_mc0_mce;
		fam_ops.mc1_mce = k8_mc1_mce;
		fam_ops.mc2_mce = k8_mc2_mce;
		break;

	case 0x12:
		fam_ops.mc0_mce = f12h_mc0_mce;
		fam_ops.mc1_mce = k8_mc1_mce;
		fam_ops.mc2_mce = k8_mc2_mce;
		break;

	case 0x14:
		fam_ops.mc0_mce = cat_mc0_mce;
		fam_ops.mc1_mce = cat_mc1_mce;
		fam_ops.mc2_mce = k8_mc2_mce;
		break;

	case 0x15:
		xec_mask = c->x86_model == 0x60 ? 0x3f : 0x1f;

		fam_ops.mc0_mce = f15h_mc0_mce;
		fam_ops.mc1_mce = f15h_mc1_mce;
		fam_ops.mc2_mce = f15h_mc2_mce;
		break;

	case 0x16:
		xec_mask = 0x1f;
		fam_ops.mc0_mce = cat_mc0_mce;
		fam_ops.mc1_mce = cat_mc1_mce;
		fam_ops.mc2_mce = f16h_mc2_mce;
		break;

	case 0x17:
	case 0x18:
		pr_warn_once("Decoding supported only on Scalable MCA processors.\n");
		return -EINVAL;

	default:
		printk(KERN_WARNING "Huh? What family is it: 0x%x?!\n", c->x86);
		return -EINVAL;
	}

out:
	pr_info("MCE: In-kernel MCE decoding enabled.\n");

	mce_register_decode_chain(&amd_mce_dec_nb);

	return 0;
}
early_initcall(mce_amd_init);

#ifdef MODULE
static void __exit mce_amd_exit(void)
{
	mce_unregister_decode_chain(&amd_mce_dec_nb);
}

MODULE_DESCRIPTION("AMD MCE decoder");
MODULE_ALIAS("edac-mce-amd");
MODULE_LICENSE("GPL");
module_exit(mce_amd_exit);
#endif
