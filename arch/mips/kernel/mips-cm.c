// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 */

#include <linux/errno.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>

#include <asm/mips-cps.h>
#include <asm/mipsregs.h>

void __iomem *mips_gcr_base;
void __iomem *mips_cm_l2sync_base;
int mips_cm_is64;

static char *cm2_tr[8] = {
	"mem",	"gcr",	"gic",	"mmio",
	"0x04", "cpc", "0x06", "0x07"
};

/* CM3 Tag ECC transaction type */
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

static char *cm2_l2_type[4] = {
	[0x0] = "None",
	[0x1] = "Tag RAM single/double ECC error",
	[0x2] = "Data RAM single/double ECC error",
	[0x3] = "WS RAM uncorrectable dirty parity"
};

static char *cm2_l2_instr[32] = {
	[0x00] = "L2_NOP",
	[0x01] = "L2_ERR_CORR",
	[0x02] = "L2_TAG_INV",
	[0x03] = "L2_WS_CLEAN",
	[0x04] = "L2_RD_MDYFY_WR",
	[0x05] = "L2_WS_MRU",
	[0x06] = "L2_EVICT_LN2",
	[0x07] = "0x07",
	[0x08] = "L2_EVICT",
	[0x09] = "L2_REFL",
	[0x0a] = "L2_RD",
	[0x0b] = "L2_WR",
	[0x0c] = "L2_EVICT_MRU",
	[0x0d] = "L2_SYNC",
	[0x0e] = "L2_REFL_ERR",
	[0x0f] = "0x0f",
	[0x10] = "L2_INDX_WB_INV",
	[0x11] = "L2_INDX_LD_TAG",
	[0x12] = "L2_INDX_ST_TAG",
	[0x13] = "L2_INDX_ST_DATA",
	[0x14] = "L2_INDX_ST_ECC",
	[0x15] = "0x15",
	[0x16] = "0x16",
	[0x17] = "0x17",
	[0x18] = "L2_FTCH_AND_LCK",
	[0x19] = "L2_HIT_INV",
	[0x1a] = "L2_HIT_WB_INV",
	[0x1b] = "L2_HIT_WB",
	[0x1c] = "0x1c",
	[0x1d] = "0x1d",
	[0x1e] = "0x1e",
	[0x1f] = "0x1f"
};

static char *cm2_causes[32] = {
	"None", "GC_WR_ERR", "GC_RD_ERR", "COH_WR_ERR",
	"COH_RD_ERR", "MMIO_WR_ERR", "MMIO_RD_ERR", "0x07",
	"0x08", "0x09", "0x0a", "0x0b",
	"0x0c", "0x0d", "0x0e", "0x0f",
	"0x10", "INTVN_WR_ERR", "INTVN_RD_ERR", "0x13",
	"0x14", "0x15", "0x16", "0x17",
	"L2_RD_UNCORR", "L2_WR_UNCORR", "L2_CORR", "0x1b",
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

static DEFINE_PER_CPU_ALIGNED(spinlock_t, cm_core_lock);
static DEFINE_PER_CPU_ALIGNED(unsigned long, cm_core_lock_flags);

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
	if (base_reg & CM_GCR_L2_ONLY_SYNC_BASE_SYNCEN)
		return base_reg & CM_GCR_L2_ONLY_SYNC_BASE_SYNCBASE;

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
	major_rev = (read_gcr_rev() & CM_GCR_REV_MAJOR) >>
		__ffs(CM_GCR_REV_MAJOR);
	if (major_rev < 6)
		return;

	/* Find a location for the L2 sync region */
	addr = mips_cm_l2sync_phys_base();
	BUG_ON((addr & CM_GCR_L2_ONLY_SYNC_BASE_SYNCBASE) != addr);
	if (!addr)
		return;

	/* Set the region base address & enable it */
	write_gcr_l2_only_sync_base(addr | CM_GCR_L2_ONLY_SYNC_BASE_SYNCEN);

	/* Map the region */
	mips_cm_l2sync_base = ioremap(addr, MIPS_CM_L2SYNC_SIZE);
}

int mips_cm_probe(void)
{
	phys_addr_t addr;
	u32 base_reg;
	unsigned cpu;

	/*
	 * No need to probe again if we have already been
	 * here before.
	 */
	if (mips_gcr_base)
		return 0;

	addr = mips_cm_phys_base();
	BUG_ON((addr & CM_GCR_BASE_GCRBASE) != addr);
	if (!addr)
		return -ENODEV;

	mips_gcr_base = ioremap(addr, MIPS_CM_GCR_SIZE);
	if (!mips_gcr_base)
		return -ENXIO;

	/* sanity check that we're looking at a CM */
	base_reg = read_gcr_base();
	if ((base_reg & CM_GCR_BASE_GCRBASE) != addr) {
		pr_err("GCRs appear to have been moved (expected them at 0x%08lx)!\n",
		       (unsigned long)addr);
		mips_gcr_base = NULL;
		return -ENODEV;
	}

	/* set default target to memory */
	change_gcr_base(CM_GCR_BASE_CMDEFTGT, CM_GCR_BASE_CMDEFTGT_MEM);

	/* disable CM regions */
	write_gcr_reg0_base(CM_GCR_REGn_BASE_BASEADDR);
	write_gcr_reg0_mask(CM_GCR_REGn_MASK_ADDRMASK);
	write_gcr_reg1_base(CM_GCR_REGn_BASE_BASEADDR);
	write_gcr_reg1_mask(CM_GCR_REGn_MASK_ADDRMASK);
	write_gcr_reg2_base(CM_GCR_REGn_BASE_BASEADDR);
	write_gcr_reg2_mask(CM_GCR_REGn_MASK_ADDRMASK);
	write_gcr_reg3_base(CM_GCR_REGn_BASE_BASEADDR);
	write_gcr_reg3_mask(CM_GCR_REGn_MASK_ADDRMASK);

	/* probe for an L2-only sync region */
	mips_cm_probe_l2sync();

	/* determine register width for this CM */
	mips_cm_is64 = IS_ENABLED(CONFIG_64BIT) && (mips_cm_revision() >= CM_REV_CM3);

	for_each_possible_cpu(cpu)
		spin_lock_init(&per_cpu(cm_core_lock, cpu));

	return 0;
}

void mips_cm_lock_other(unsigned int cluster, unsigned int core,
			unsigned int vp, unsigned int block)
{
	unsigned int curr_core, cm_rev;
	u32 val;

	cm_rev = mips_cm_revision();
	preempt_disable();

	if (cm_rev >= CM_REV_CM3) {
		val = core << __ffs(CM3_GCR_Cx_OTHER_CORE);
		val |= vp << __ffs(CM3_GCR_Cx_OTHER_VP);

		if (cm_rev >= CM_REV_CM3_5) {
			val |= CM_GCR_Cx_OTHER_CLUSTER_EN;
			val |= cluster << __ffs(CM_GCR_Cx_OTHER_CLUSTER);
			val |= block << __ffs(CM_GCR_Cx_OTHER_BLOCK);
		} else {
			WARN_ON(cluster != 0);
			WARN_ON(block != CM_GCR_Cx_OTHER_BLOCK_LOCAL);
		}

		/*
		 * We need to disable interrupts in SMP systems in order to
		 * ensure that we don't interrupt the caller with code which
		 * may modify the redirect register. We do so here in a
		 * slightly obscure way by using a spin lock, since this has
		 * the neat property of also catching any nested uses of
		 * mips_cm_lock_other() leading to a deadlock or a nice warning
		 * with lockdep enabled.
		 */
		spin_lock_irqsave(this_cpu_ptr(&cm_core_lock),
				  *this_cpu_ptr(&cm_core_lock_flags));
	} else {
		WARN_ON(cluster != 0);
		WARN_ON(block != CM_GCR_Cx_OTHER_BLOCK_LOCAL);

		/*
		 * We only have a GCR_CL_OTHER per core in systems with
		 * CM 2.5 & older, so have to ensure other VP(E)s don't
		 * race with us.
		 */
		curr_core = cpu_core(&current_cpu_data);
		spin_lock_irqsave(&per_cpu(cm_core_lock, curr_core),
				  per_cpu(cm_core_lock_flags, curr_core));

		val = core << __ffs(CM_GCR_Cx_OTHER_CORENUM);
	}

	write_gcr_cl_other(val);

	/*
	 * Ensure the core-other region reflects the appropriate core &
	 * VP before any accesses to it occur.
	 */
	mb();
}

void mips_cm_unlock_other(void)
{
	unsigned int curr_core;

	if (mips_cm_revision() < CM_REV_CM3) {
		curr_core = cpu_core(&current_cpu_data);
		spin_unlock_irqrestore(&per_cpu(cm_core_lock, curr_core),
				       per_cpu(cm_core_lock_flags, curr_core));
	} else {
		spin_unlock_irqrestore(this_cpu_ptr(&cm_core_lock),
				       *this_cpu_ptr(&cm_core_lock_flags));
	}

	preempt_enable();
}

void mips_cm_error_report(void)
{
	u64 cm_error, cm_addr, cm_other;
	unsigned long revision;
	int ocause, cause;
	char buf[256];

	if (!mips_cm_present())
		return;

	revision = mips_cm_revision();
	cm_error = read_gcr_error_cause();
	cm_addr = read_gcr_error_addr();
	cm_other = read_gcr_error_mult();

	if (revision < CM_REV_CM3) { /* CM2 */
		cause = cm_error >> __ffs(CM_GCR_ERROR_CAUSE_ERRTYPE);
		ocause = cm_other >> __ffs(CM_GCR_ERROR_MULT_ERR2ND);

		if (!cause)
			return;

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
		} else if (cause < 24) {
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
		} else {
			unsigned long muc_bit = (cm_error >> 23) & 1;
			unsigned long ins_bits = (cm_error >> 18) & 0x1f;
			unsigned long arr_bits = (cm_error >> 16) & 3;
			unsigned long dw_bits = (cm_error >> 12) & 15;
			unsigned long way_bits = (cm_error >> 9) & 7;
			unsigned long mway_bit = (cm_error >> 8) & 1;
			unsigned long syn_bits = (cm_error >> 0) & 0xFF;

			snprintf(buf, sizeof(buf),
				 "Type=%s%s Instr=%s DW=%lu Way=%lu "
				 "MWay=%s Syndrome=0x%02lx",
				 muc_bit ? "Multi-UC " : "",
				 cm2_l2_type[arr_bits],
				 cm2_l2_instr[ins_bits], dw_bits, way_bits,
				 mway_bit ? "True" : "False", syn_bits);
		}
		pr_err("CM_ERROR=%08llx %s <%s>\n", cm_error,
		       cm2_causes[cause], buf);
		pr_err("CM_ADDR =%08llx\n", cm_addr);
		pr_err("CM_OTHER=%08llx %s\n", cm_other, cm2_causes[ocause]);
	} else { /* CM3 */
		ulong core_id_bits, vp_id_bits, cmd_bits, cmd_group_bits;
		ulong cm3_cca_bits, mcp_bits, cm3_tr_bits, sched_bit;

		cause = cm_error >> __ffs64(CM3_GCR_ERROR_CAUSE_ERRTYPE);
		ocause = cm_other >> __ffs(CM_GCR_ERROR_MULT_ERR2ND);

		if (!cause)
			return;

		/* Used by cause == {1,2,3} */
		core_id_bits = (cm_error >> 22) & 0xf;
		vp_id_bits = (cm_error >> 18) & 0xf;
		cmd_bits = (cm_error >> 14) & 0xf;
		cmd_group_bits = (cm_error >> 11) & 0xf;
		cm3_cca_bits = (cm_error >> 8) & 7;
		mcp_bits = (cm_error >> 5) & 0xf;
		cm3_tr_bits = (cm_error >> 1) & 0xf;
		sched_bit = cm_error & 0x1;

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
		} else {
			buf[0] = 0;
		}

		pr_err("CM_ERROR=%llx %s <%s>\n", cm_error,
		       cm3_causes[cause], buf);
		pr_err("CM_ADDR =%llx\n", cm_addr);
		pr_err("CM_OTHER=%llx %s\n", cm_other, cm3_causes[ocause]);
	}

	/* reprime cause register */
	write_gcr_error_cause(cm_error);
}
