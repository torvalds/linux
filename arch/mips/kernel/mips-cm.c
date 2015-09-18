/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/errno.h>

#include <asm/mips-cm.h>
#include <asm/mipsregs.h>

void __iomem *mips_cm_base;
void __iomem *mips_cm_l2sync_base;
int mips_cm_is64;

static char *cm2_tr[8] = {
	"mem",	"gcr",	"gic",	"mmio",
	"0x04", "cpc", "0x06", "0x07"
};

/* CM3 Tag ECC transation type */
static char *cm3_tr[16] = {
	[0x0] = "ReqNoData",
	[0x1] = "0x1",
	[0x2] = "ReqWData",
	[0x3] = "0x3",
	[0x4] = "IReqNoResp",
	[0x5] = "IReqWResp",
	[0x6] = "IReqNoRespDat",
	[0x7] = "IReqWRespDat",
	[0x8] = "RespNoData",
	[0x9] = "RespDataFol",
	[0xa] = "RespWData",
	[0xb] = "RespDataOnly",
	[0xc] = "IRespNoData",
	[0xd] = "IRespDataFol",
	[0xe] = "IRespWData",
	[0xf] = "IRespDataOnly"
};

static char *cm2_cmd[32] = {
	[0x00] = "0x00",
	[0x01] = "Legacy Write",
	[0x02] = "Legacy Read",
	[0x03] = "0x03",
	[0x04] = "0x04",
	[0x05] = "0x05",
	[0x06] = "0x06",
	[0x07] = "0x07",
	[0x08] = "Coherent Read Own",
	[0x09] = "Coherent Read Share",
	[0x0a] = "Coherent Read Discard",
	[0x0b] = "Coherent Ready Share Always",
	[0x0c] = "Coherent Upgrade",
	[0x0d] = "Coherent Writeback",
	[0x0e] = "0x0e",
	[0x0f] = "0x0f",
	[0x10] = "Coherent Copyback",
	[0x11] = "Coherent Copyback Invalidate",
	[0x12] = "Coherent Invalidate",
	[0x13] = "Coherent Write Invalidate",
	[0x14] = "Coherent Completion Sync",
	[0x15] = "0x15",
	[0x16] = "0x16",
	[0x17] = "0x17",
	[0x18] = "0x18",
	[0x19] = "0x19",
	[0x1a] = "0x1a",
	[0x1b] = "0x1b",
	[0x1c] = "0x1c",
	[0x1d] = "0x1d",
	[0x1e] = "0x1e",
	[0x1f] = "0x1f"
};

/* CM3 Tag ECC command type */
static char *cm3_cmd[16] = {
	[0x0] = "Legacy Read",
	[0x1] = "Legacy Write",
	[0x2] = "Coherent Read Own",
	[0x3] = "Coherent Read Share",
	[0x4] = "Coherent Read Discard",
	[0x5] = "Coherent Evicted",
	[0x6] = "Coherent Upgrade",
	[0x7] = "Coherent Upgrade for Store Conditional",
	[0x8] = "Coherent Writeback",
	[0x9] = "Coherent Write Invalidate",
	[0xa] = "0xa",
	[0xb] = "0xb",
	[0xc] = "0xc",
	[0xd] = "0xd",
	[0xe] = "0xe",
	[0xf] = "0xf"
};

/* CM3 Tag ECC command group */
static char *cm3_cmd_group[8] = {
	[0x0] = "Normal",
	[0x1] = "Registers",
	[0x2] = "TLB",
	[0x3] = "0x3",
	[0x4] = "L1I",
	[0x5] = "L1D",
	[0x6] = "L3",
	[0x7] = "L2"
};

static char *cm2_core[8] = {
	"Invalid/OK",	"Invalid/Data",
	"Shared/OK",	"Shared/Data",
	"Modified/OK",	"Modified/Data",
	"Exclusive/OK", "Exclusive/Data"
};

static char *cm2_causes[32] = {
	"None", "GC_WR_ERR", "GC_RD_ERR", "COH_WR_ERR",
	"COH_RD_ERR", "MMIO_WR_ERR", "MMIO_RD_ERR", "0x07",
	"0x08", "0x09", "0x0a", "0x0b",
	"0x0c", "0x0d", "0x0e", "0x0f",
	"0x10", "0x11", "0x12", "0x13",
	"0x14", "0x15", "0x16", "INTVN_WR_ERR",
	"INTVN_RD_ERR", "0x19", "0x1a", "0x1b",
	"0x1c", "0x1d", "0x1e", "0x1f"
};

static char *cm3_causes[32] = {
	"0x0", "MP_CORRECTABLE_ECC_ERR", "MP_REQUEST_DECODE_ERR",
	"MP_UNCORRECTABLE_ECC_ERR", "MP_PARITY_ERR", "MP_COHERENCE_ERR",
	"CMBIU_REQUEST_DECODE_ERR", "CMBIU_PARITY_ERR", "CMBIU_AXI_RESP_ERR",
	"0x9", "RBI_BUS_ERR", "0xb", "0xc", "0xd", "0xe", "0xf", "0x10",
	"0x11", "0x12", "0x13", "0x14", "0x15", "0x16", "0x17", "0x18",
	"0x19", "0x1a", "0x1b", "0x1c", "0x1d", "0x1e", "0x1f"
};

phys_addr_t __mips_cm_phys_base(void)
{
	u32 config3 = read_c0_config3();
	unsigned long cmgcr;

	/* Check the CMGCRBase register is implemented */
	if (!(config3 & MIPS_CONF3_CMGCR))
		return 0;

	/* Read the address from CMGCRBase */
	cmgcr = read_c0_cmgcrbase();
	return (cmgcr & MIPS_CMGCRF_BASE) << (36 - 32);
}

phys_addr_t mips_cm_phys_base(void)
	__attribute__((weak, alias("__mips_cm_phys_base")));

phys_addr_t __mips_cm_l2sync_phys_base(void)
{
	u32 base_reg;

	/*
	 * If the L2-only sync region is already enabled then leave it at it's
	 * current location.
	 */
	base_reg = read_gcr_l2_only_sync_base();
	if (base_reg & CM_GCR_L2_ONLY_SYNC_BASE_SYNCEN_MSK)
		return base_reg & CM_GCR_L2_ONLY_SYNC_BASE_SYNCBASE_MSK;

	/* Default to following the CM */
	return mips_cm_phys_base() + MIPS_CM_GCR_SIZE;
}

phys_addr_t mips_cm_l2sync_phys_base(void)
	__attribute__((weak, alias("__mips_cm_l2sync_phys_base")));

static void mips_cm_probe_l2sync(void)
{
	unsigned major_rev;
	phys_addr_t addr;

	/* L2-only sync was introduced with CM major revision 6 */
	major_rev = (read_gcr_rev() & CM_GCR_REV_MAJOR_MSK) >>
		CM_GCR_REV_MAJOR_SHF;
	if (major_rev < 6)
		return;

	/* Find a location for the L2 sync region */
	addr = mips_cm_l2sync_phys_base();
	BUG_ON((addr & CM_GCR_L2_ONLY_SYNC_BASE_SYNCBASE_MSK) != addr);
	if (!addr)
		return;

	/* Set the region base address & enable it */
	write_gcr_l2_only_sync_base(addr | CM_GCR_L2_ONLY_SYNC_BASE_SYNCEN_MSK);

	/* Map the region */
	mips_cm_l2sync_base = ioremap_nocache(addr, MIPS_CM_L2SYNC_SIZE);
}

int mips_cm_probe(void)
{
	phys_addr_t addr;
	u32 base_reg;

	/*
	 * No need to probe again if we have already been
	 * here before.
	 */
	if (mips_cm_base)
		return 0;

	addr = mips_cm_phys_base();
	BUG_ON((addr & CM_GCR_BASE_GCRBASE_MSK) != addr);
	if (!addr)
		return -ENODEV;

	mips_cm_base = ioremap_nocache(addr, MIPS_CM_GCR_SIZE);
	if (!mips_cm_base)
		return -ENXIO;

	/* sanity check that we're looking at a CM */
	base_reg = read_gcr_base();
	if ((base_reg & CM_GCR_BASE_GCRBASE_MSK) != addr) {
		pr_err("GCRs appear to have been moved (expected them at 0x%08lx)!\n",
		       (unsigned long)addr);
		mips_cm_base = NULL;
		return -ENODEV;
	}

	/* set default target to memory */
	base_reg &= ~CM_GCR_BASE_CMDEFTGT_MSK;
	base_reg |= CM_GCR_BASE_CMDEFTGT_MEM;
	write_gcr_base(base_reg);

	/* disable CM regions */
	write_gcr_reg0_base(CM_GCR_REGn_BASE_BASEADDR_MSK);
	write_gcr_reg0_mask(CM_GCR_REGn_MASK_ADDRMASK_MSK);
	write_gcr_reg1_base(CM_GCR_REGn_BASE_BASEADDR_MSK);
	write_gcr_reg1_mask(CM_GCR_REGn_MASK_ADDRMASK_MSK);
	write_gcr_reg2_base(CM_GCR_REGn_BASE_BASEADDR_MSK);
	write_gcr_reg2_mask(CM_GCR_REGn_MASK_ADDRMASK_MSK);
	write_gcr_reg3_base(CM_GCR_REGn_BASE_BASEADDR_MSK);
	write_gcr_reg3_mask(CM_GCR_REGn_MASK_ADDRMASK_MSK);

	/* probe for an L2-only sync region */
	mips_cm_probe_l2sync();

	/* determine register width for this CM */
	mips_cm_is64 = config_enabled(CONFIG_64BIT) && (mips_cm_revision() >= CM_REV_CM3);

	return 0;
}

void mips_cm_error_report(void)
{
	unsigned long revision = mips_cm_revision();
	/*
	 * CM3 has a 64-bit Error cause register with 0:57 containing the error
	 * info and 63:58 the error type. For old CMs, everything is contained
	 * in a single 32-bit register (0:26 and 31:27 respectively). Even
	 * though the cm_error is u64, we will simply ignore the upper word
	 * for CM2.
	 */
	u64 cm_error = read_gcr_error_cause();
	int cm_error_cause_sft = CM_GCR_ERROR_CAUSE_ERRTYPE_SHF +
				 ((revision >= CM_REV_CM3) ? 31 : 0);
	unsigned long cm_addr = read_gcr_error_addr();
	unsigned long cm_other = read_gcr_error_mult();
	int ocause, cause;
	char buf[256];

	if (!mips_cm_present())
		return;

	cause = cm_error >> cm_error_cause_sft;

	if (!cause)
		/* All good */
		return;

	ocause = cm_other >> CM_GCR_ERROR_MULT_ERR2ND_SHF;
	if (revision < CM_REV_CM3) { /* CM2 */
		if (cause < 16) {
			unsigned long cca_bits = (cm_error >> 15) & 7;
			unsigned long tr_bits = (cm_error >> 12) & 7;
			unsigned long cmd_bits = (cm_error >> 7) & 0x1f;
			unsigned long stag_bits = (cm_error >> 3) & 15;
			unsigned long sport_bits = (cm_error >> 0) & 7;

			snprintf(buf, sizeof(buf),
				 "CCA=%lu TR=%s MCmd=%s STag=%lu "
				 "SPort=%lu\n", cca_bits, cm2_tr[tr_bits],
				 cm2_cmd[cmd_bits], stag_bits, sport_bits);
		} else {
			/* glob state & sresp together */
			unsigned long c3_bits = (cm_error >> 18) & 7;
			unsigned long c2_bits = (cm_error >> 15) & 7;
			unsigned long c1_bits = (cm_error >> 12) & 7;
			unsigned long c0_bits = (cm_error >> 9) & 7;
			unsigned long sc_bit = (cm_error >> 8) & 1;
			unsigned long cmd_bits = (cm_error >> 3) & 0x1f;
			unsigned long sport_bits = (cm_error >> 0) & 7;

			snprintf(buf, sizeof(buf),
				 "C3=%s C2=%s C1=%s C0=%s SC=%s "
				 "MCmd=%s SPort=%lu\n",
				 cm2_core[c3_bits], cm2_core[c2_bits],
				 cm2_core[c1_bits], cm2_core[c0_bits],
				 sc_bit ? "True" : "False",
				 cm2_cmd[cmd_bits], sport_bits);
		}
			pr_err("CM_ERROR=%08llx %s <%s>\n", cm_error,
			       cm2_causes[cause], buf);
		pr_err("CM_ADDR =%08lx\n", cm_addr);
		pr_err("CM_OTHER=%08lx %s\n", cm_other, cm2_causes[ocause]);
	} else { /* CM3 */
	/* Used by cause == {1,2,3} */
		unsigned long core_id_bits = (cm_error >> 22) & 0xf;
		unsigned long vp_id_bits = (cm_error >> 18) & 0xf;
		unsigned long cmd_bits = (cm_error >> 14) & 0xf;
		unsigned long cmd_group_bits = (cm_error >> 11) & 0xf;
		unsigned long cm3_cca_bits = (cm_error >> 8) & 7;
		unsigned long mcp_bits = (cm_error >> 5) & 0xf;
		unsigned long cm3_tr_bits = (cm_error >> 1) & 0xf;
		unsigned long sched_bit = cm_error & 0x1;

		if (cause == 1 || cause == 3) { /* Tag ECC */
			unsigned long tag_ecc = (cm_error >> 57) & 0x1;
			unsigned long tag_way_bits = (cm_error >> 29) & 0xffff;
			unsigned long dword_bits = (cm_error >> 49) & 0xff;
			unsigned long data_way_bits = (cm_error >> 45) & 0xf;
			unsigned long data_sets_bits = (cm_error >> 29) & 0xfff;
			unsigned long bank_bit = (cm_error >> 28) & 0x1;
			snprintf(buf, sizeof(buf),
				 "%s ECC Error: Way=%lu (DWORD=%lu, Sets=%lu)"
				 "Bank=%lu CoreID=%lu VPID=%lu Command=%s"
				 "Command Group=%s CCA=%lu MCP=%d"
				 "Transaction type=%s Scheduler=%lu\n",
				 tag_ecc ? "TAG" : "DATA",
				 tag_ecc ? (unsigned long)ffs(tag_way_bits) - 1 :
				 data_way_bits, bank_bit, dword_bits,
				 data_sets_bits,
				 core_id_bits, vp_id_bits,
				 cm3_cmd[cmd_bits],
				 cm3_cmd_group[cmd_group_bits],
				 cm3_cca_bits, 1 << mcp_bits,
				 cm3_tr[cm3_tr_bits], sched_bit);
		} else if (cause == 2) {
			unsigned long data_error_type = (cm_error >> 41) & 0xfff;
			unsigned long data_decode_cmd = (cm_error >> 37) & 0xf;
			unsigned long data_decode_group = (cm_error >> 34) & 0x7;
			unsigned long data_decode_destination_id = (cm_error >> 28) & 0x3f;

			snprintf(buf, sizeof(buf),
				 "Decode Request Error: Type=%lu, Command=%lu"
				 "Command Group=%lu Destination ID=%lu"
				 "CoreID=%lu VPID=%lu Command=%s"
				 "Command Group=%s CCA=%lu MCP=%d"
				 "Transaction type=%s Scheduler=%lu\n",
				 data_error_type, data_decode_cmd,
				 data_decode_group, data_decode_destination_id,
				 core_id_bits, vp_id_bits,
				 cm3_cmd[cmd_bits],
				 cm3_cmd_group[cmd_group_bits],
				 cm3_cca_bits, 1 << mcp_bits,
				 cm3_tr[cm3_tr_bits], sched_bit);
		}

		pr_err("CM_ERROR=%llx %s <%s>\n", cm_error,
		       cm3_causes[cause], buf);
		pr_err("CM_ADDR =%lx\n", cm_addr);
		pr_err("CM_OTHER=%lx %s\n", cm_other, cm3_causes[ocause]);
	}

	/* reprime cause register */
	write_gcr_error_cause(0);
}
