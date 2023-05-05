// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/device.h>
#include "arm-smmu.h"
#include "arm-smmu-debug.h"
#include <linux/qcom_scm.h>

u32 arm_smmu_debug_qtb_debugchain_load(void __iomem *debugchain_base)
{
	u32 shiftreglen = 0;

	/* Reading the debugchain_load register will start the debugchain sequence */
	readl_relaxed(debugchain_base + DebugChainQTB_debug_Load);
	shiftreglen = readl_relaxed(debugchain_base + DebugChainQTB_debug_ShiftRegLen);
	return (((shiftreglen * 2)/64 + ((shiftreglen * 2)%64 == 0 ? 0 : 1) + 1));
}

u64 arm_smmu_debug_qtb_debugchain_dump(void __iomem *debugchain_base)
{
	u64 dump;

	dump = readl_relaxed(debugchain_base + DebugChainQTB_debug_Dump_Low);
	dump = (dump | (readl_relaxed(debugchain_base + DebugChainQTB_debug_Dump_High) << 31));

	return dump;
}

void arm_smmu_debug_qtb_transtracker_set_config(void __iomem *transactiontracker_base, u64 sel)
{
	u64 val = 0;

	if (sel) {
		val |= TTQTB_GlbEn | TTQTB_IgnoreCtiTrigIn0 | TTQTB_LogAsstEn;
		writel_relaxed(val, transactiontracker_base + TransTrackerQTB_MainCtl);
		writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base + TransTrackerQTB_LogClr);
	} else {
		/*By default All transactions through QTB are captured*/
		val |= TTQTB_GlbEn | TTQTB_IgnoreCtiTrigIn0 | TTQTB_LogAll;
		writel_relaxed(val, transactiontracker_base + TransTrackerQTB_MainCtl);
		writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base + TransTrackerQTB_LogClr);
	}
}

u64 arm_smmu_debug_qtb_transtracker_get_config(void __iomem *transactiontracker_base)
{
	return readl_relaxed(transactiontracker_base + TransTrackerQTB_MainCtl);
}

void arm_smmu_debug_qtb_transtracker_setfilter(void __iomem *transactiontracker_base,
		u64 sel, u64 filter, int qtb_type)
{
	u64 val = 0;

	val = TTQTB_RESET_VAL | TTQTB_Filter_DevNeEn | TTQTB_Filter_DevEEn;

	if (sel == 1) {
		if (filter == 2)
			val |= TTQTB_Filter_NormalEn;
		else if (filter == 3)
			val |= TTQTB_Filter_CachedEn;
		else if (filter == 4)
			val |= TTQTB_Filter_SharedEn;
		else if (filter == 5)
			val |= TTQTB_Filter_PostedEn;
		writel_relaxed(val, transactiontracker_base + TransTrackerQTB_Filter_TrType);
	} else if (sel == 2) {
		if (qtb_type == 1)
			writeq_relaxed(filter, transactiontracker_base +
					TransTrackerQTB_gfx_Filter_Addr_Min);
		else if (qtb_type == 2)
			writeq_relaxed(filter, transactiontracker_base +
					TransTrackerQTB_Filter_Addr_Min_Low);
	} else if (sel == 3) {
		if (qtb_type == 1)
			writel_relaxed(filter, transactiontracker_base +
					TransTrackerQTB_gfx_Filter_Addr_Max);
		else if (qtb_type == 2)
			writeq_relaxed(filter, transactiontracker_base +
					TransTrackerQTB_Filter_Addr_Max_Low);
	}
	writel_relaxed(TTQTB_Filter_OpCode_Set_Val, transactiontracker_base +
			TransTrackerQTB_Filter_OpCode);
	writel_relaxed(TTQTB_Filter_Alloc_Set_Val, transactiontracker_base +
			TransTrackerQTB_Filter_Alloc);
	writel_relaxed(TTQTB_Filter_Length_Set_Val, transactiontracker_base +
			TransTrackerQTB_Filter_Length);
}

void arm_smmu_debug_qtb_transtracker_getfilter(void __iomem *transactiontracker_base,
		u64 filter[3], int qtb_type)
{
	int i = 0;

	if (qtb_type == 1) {
		filter[i] = readl_relaxed(transactiontracker_base + TransTrackerQTB_Filter_TrType);
		filter[i+1] = readq_relaxed(transactiontracker_base +
				TransTrackerQTB_gfx_Filter_Addr_Min);
		filter[i+2] = readq_relaxed(transactiontracker_base +
				TransTrackerQTB_gfx_Filter_Addr_Max);
	} else if (qtb_type == 2) {
		filter[i] = readl_relaxed(transactiontracker_base + TransTrackerQTB_Filter_TrType);
		filter[i+1] = (readl_relaxed(transactiontracker_base +
					TransTrackerQTB_Filter_Addr_Min_Low) |
				readl_relaxed(transactiontracker_base +
					TransTrackerQTB_Filter_Addr_Min_High));
		filter[i+2] = (readl_relaxed(transactiontracker_base +
					TransTrackerQTB_Filter_Addr_Max_Low) |
				readl_relaxed(transactiontracker_base +
					TransTrackerQTB_Filter_Addr_Max_High));
	}
}

void arm_smmu_debug_qtb_transtrac_collect(void __iomem *transactiontracker_base,
		u64 gfxttlogs[TTQTB_Capture_Points][2*TTQTB_Regs_Per_Capture_Points],
		u64 ttlogs[TTQTB_Capture_Points][4*TTQTB_Regs_Per_Capture_Points],
		u64 ttlogs_time[2*TTQTB_Capture_Points], int qtb_type)
{
	int i, j, x, y;

	for (i = 0, x = 0; i < TTQTB_Capture_Points && x < 2*TTQTB_Capture_Points; ++i, x += 2) {
		ttlogs_time[x] = readl_relaxed(transactiontracker_base +
				TransTrackerQTB_Latency(i));
		ttlogs_time[x+1] = readl_relaxed(transactiontracker_base +
				TransTrackerQTB_TimeStamp(i));
		if (qtb_type == 1) {
			for (j = 0, y = 0; j < TTQTB_Regs_Per_Capture_Points &&
					y < 2*TTQTB_Regs_Per_Capture_Points; ++j, y += 2) {
				gfxttlogs[i][y] = readl_relaxed(transactiontracker_base +
						TransTrackerQTB_LogIn_Low(i, j)) |
					readl_relaxed(transactiontracker_base +
							TransTrackerQTB_LogIn_High(i, j));
				gfxttlogs[i][y+1] = readl_relaxed(transactiontracker_base +
						TransTrackerQTB_LogOut_Low(i, j)) |
					readl_relaxed(transactiontracker_base +
							TransTrackerQTB_LogOut_High(i, j));
			}
		} else if (qtb_type == 2) {
			for (j = 0, y = 0; j < TTQTB_Regs_Per_Capture_Points &&
					y < 4*TTQTB_Regs_Per_Capture_Points; ++j, y += 4) {
				ttlogs[i][y] = readl_relaxed(transactiontracker_base +
						TransTrackerQTB_LogIn_Low(i, j));
				ttlogs[i][y+1] = readl_relaxed(transactiontracker_base +
						TransTrackerQTB_LogIn_High(i, j));
				ttlogs[i][y+2] = readl_relaxed(transactiontracker_base +
						TransTrackerQTB_LogOut_Low(i, j));
				ttlogs[i][y+3] = readl_relaxed(transactiontracker_base +
						TransTrackerQTB_LogOut_High(i, j));
			}
		}
	}
}

void arm_smmu_debug_qtb_transtrac_reset(void __iomem *transactiontracker_base)
{
	/* reset the transaction tracker once called after each read */
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base + TransTrackerQTB_MainCtl);
	writel_relaxed(TTQTB_SET, transactiontracker_base + TransTrackerQTB_LogClr);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_TrType);
	writeq_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_gfx_Filter_Addr_Min);
	writeq_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_gfx_Filter_Addr_Max);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_OpCode);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_ReqUser_Base);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_ReqUser_Mask);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_LogUser_Base);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_LogUser_Mask);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_Alloc);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_ExtId_Base);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_ExtId_Mask);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_Length);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_Urgency);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_CacheIndex_Base);
	writel_relaxed(TTQTB_RESET_VAL, transactiontracker_base +
			TransTrackerQTB_Filter_CacheIndex_Mask);

}

void arm_smmu_debug_dump_debugchain(struct device *dev, void __iomem *debugchain_base)
{
	long chain_length = 0, index = 0;
	u64 val;

	chain_length = arm_smmu_debug_qtb_debugchain_load(debugchain_base);
	dev_info(dev, "Dumping Debug chain: Length : %d\n", chain_length);
	/* First read is to dump away the 0xDEADBEEF value */
	arm_smmu_debug_qtb_debugchain_dump(debugchain_base);
	do {
		val = arm_smmu_debug_qtb_debugchain_dump(debugchain_base);
		dev_info(dev, "Debug chain: Index :%ld, val : 0x%lx\n", index++, val);
	} while (chain_length--);
}

void arm_smmu_debug_dump_qtb_regs(struct device *dev, void __iomem *tbu_base)
{
	dev_info(dev, "QSMSTATUS: 0x%lx IDLESTATUS: 0x%lx\n",
			readl_relaxed(tbu_base + Qtb500_QtbNsDbgQsmStatus),
			readl_relaxed(tbu_base + Qtb500_QtbNsDbgIdleStatus));
}

u32 arm_smmu_debug_tbu_testbus_select(void __iomem *tbu_base,
		bool write, u32 val)
{
	if (write) {
		writel_relaxed(val, tbu_base + DEBUG_TESTBUS_SEL_TBU);
		/* Make sure tbu select register is written to */
		wmb();
	} else {
		return readl_relaxed(tbu_base + DEBUG_TESTBUS_SEL_TBU);
	}
	return 0;
}

u32 arm_smmu_debug_tbu_testbus_output(void __iomem *tbu_base)
{
	return readl_relaxed(tbu_base + DEBUG_TESTBUS_TBU);
}

u32 arm_smmu_debug_tcu_testbus_select(phys_addr_t phys_addr,
		void __iomem *tcu_base, enum tcu_testbus testbus,
		bool write, u32 val)
{
	int offset;
	u32 testbus_sel;
	int ret = 0;

	if (testbus == CLK_TESTBUS) {
		if (write) {
			offset = ARM_SMMU_TESTBUS_SEL_HLOS1_NS;
			writel_relaxed(val, tcu_base + offset);
			/* Make sure tcu select register is written to */
			wmb();
		} else {
			offset = ARM_SMMU_TCU_TESTBUS_HLOS1_NS;
			return readl_relaxed(tcu_base + offset);
		}
	} else {
		offset = ARM_SMMU_TESTBUS_SEL;
		if (write) {
			ret = qcom_scm_io_writel((phys_addr + offset), val);
			if (ret)
				pr_err_ratelimited("SCM write of TESTBUS SEL fails: %d\n",
				       ret);

			/* Make sure tcu select register is written to */
			wmb();
		} else {
			ret = qcom_scm_io_readl(phys_addr + offset,
						&testbus_sel);
			if (ret)
				pr_err_ratelimited("SCM write of TESTBUS SEL fails: %d\n",
				       ret);
			else
				return testbus_sel;
		}
	}

	return 0;
}

u32 arm_smmu_debug_tcu_testbus_output(phys_addr_t phys_addr)
{
	u32 testbus_output;
	int ret;

	ret = qcom_scm_io_readl(phys_addr + ARM_SMMU_TESTBUS, &testbus_output);
	if (!ret)
		return testbus_output;

	pr_err_ratelimited("SCM write of TESTBUS output fails: %d\n", ret);

	return 0;
}

static void arm_smmu_debug_dump_tbu_qns4_testbus(struct device *dev,
					void __iomem *tbu_base)
{
	int i;
	u32 reg;

	for (i = 0 ; i < TBU_QNS4_BRIDGE_SIZE; ++i) {
		reg = arm_smmu_debug_tbu_testbus_select(tbu_base, READ, 0);
		reg = (reg & ~TBU_QNS4_BRIDGE_MASK) | i << 0;
		arm_smmu_debug_tbu_testbus_select(tbu_base, WRITE, reg);
		dev_info(dev, "testbus_sel: 0x%lx Index: %d val: 0x%llx\n",
			arm_smmu_debug_tbu_testbus_select(tbu_base,
						READ, 0), i,
			arm_smmu_debug_tbu_testbus_output(tbu_base));
	}
}

static void arm_smmu_debug_program_tbu_testbus(void __iomem *tbu_base,
					int tbu_testbus)
{
	u32 reg;

	reg = arm_smmu_debug_tbu_testbus_select(tbu_base, READ, 0);
	reg = (reg & ~TBU_MASK) | tbu_testbus;
	arm_smmu_debug_tbu_testbus_select(tbu_base, WRITE, reg);
}

void arm_smmu_debug_dump_tbu_testbus(struct device *dev, void __iomem *tbu_base,
			int tbu_testbus_sel)
{
	if (tbu_testbus_sel & TBU_CLK_GATE_CONTROLLER_TESTBUS_SEL) {
		dev_info(dev, "Dumping TBU clk gate controller:\n");
		arm_smmu_debug_program_tbu_testbus(tbu_base,
				TBU_CLK_GATE_CONTROLLER_TESTBUS);
		dev_info(dev, "testbus_sel: 0x%lx val: 0x%llx\n",
			arm_smmu_debug_tbu_testbus_select(tbu_base,
						READ, 0),
			arm_smmu_debug_tbu_testbus_output(tbu_base));
	}

	if (tbu_testbus_sel & TBU_QNS4_A2Q_TESTBUS_SEL) {
		dev_info(dev, "Dumping TBU qns4 a2q test bus:\n");
		arm_smmu_debug_program_tbu_testbus(tbu_base,
				TBU_QNS4_A2Q_TESTBUS);
		arm_smmu_debug_dump_tbu_qns4_testbus(dev, tbu_base);
	}

	if (tbu_testbus_sel & TBU_QNS4_Q2A_TESTBUS_SEL) {
		dev_info(dev, "Dumping qns4 q2a test bus:\n");
		arm_smmu_debug_program_tbu_testbus(tbu_base,
				TBU_QNS4_Q2A_TESTBUS);
		arm_smmu_debug_dump_tbu_qns4_testbus(dev, tbu_base);
	}

	if (tbu_testbus_sel & TBU_MULTIMASTER_QCHANNEL_TESTBUS_SEL) {
		dev_info(dev, "Dumping multi master qchannel:\n");
		arm_smmu_debug_program_tbu_testbus(tbu_base,
				TBU_MULTIMASTER_QCHANNEL_TESTBUS);
		dev_info(dev, "testbus_sel: 0x%lx val: 0x%llx\n",
			arm_smmu_debug_tbu_testbus_select(tbu_base,
						READ, 0),
			arm_smmu_debug_tbu_testbus_output(tbu_base));
	}

	if (tbu_testbus_sel & TBU_CLK_GATE_CONTROLLER_EXT_TESTBUS_SEL) {
		dev_info(dev, "Dumping tbu clk gate controller ext:\n");
		arm_smmu_debug_program_tbu_testbus(tbu_base,
				TBU_CLK_GATE_CONTROLLER_EXT_TESTBUS);
		dev_info(dev, "testbus_sel: 0x%lx val: 0x%llx\n",
			arm_smmu_debug_tbu_testbus_select(tbu_base,
						READ, 0),
			arm_smmu_debug_tbu_testbus_output(tbu_base));
	}

	if (tbu_testbus_sel & TBU_LOW_POWER_STATUS_TESTBUS_SEL) {
		dev_info(dev, "Dumping tbu low power status:\n");
		arm_smmu_debug_program_tbu_testbus(tbu_base,
				TBU_LOW_POWER_STATUS_TESTBUS);
		dev_info(dev, "testbus_sel: 0x%lx val: 0x%llx\n",
			arm_smmu_debug_tbu_testbus_select(tbu_base,
						READ, 0),
			arm_smmu_debug_tbu_testbus_output(tbu_base));
	}

	if (tbu_testbus_sel & TBU_QNS4_VLD_RDY_SEL) {
		dev_info(dev, "Dumping tbu qns4 vld rdy:\n");
		arm_smmu_debug_program_tbu_testbus(tbu_base,
				TBU_QNS4_VLD_RDY);
		dev_info(dev, "testbus_sel: 0x%lx val: 0x%llx\n",
			arm_smmu_debug_tbu_testbus_select(tbu_base,
						READ, 0),
			arm_smmu_debug_tbu_testbus_output(tbu_base));
	}
}

static void arm_smmu_debug_program_tcu_testbus(struct device *dev,
		phys_addr_t phys_addr, void __iomem *tcu_base,
		unsigned long mask, int start, int end, int shift,
		bool print)
{
	u32 reg;
	int i;

	for (i = start; i < end; i++) {
		reg = arm_smmu_debug_tcu_testbus_select(phys_addr, tcu_base,
				PTW_AND_CACHE_TESTBUS, READ, 0);
		reg &= mask;
		reg |= i << shift;
		arm_smmu_debug_tcu_testbus_select(phys_addr, tcu_base,
				PTW_AND_CACHE_TESTBUS, WRITE, reg);
		if (print)
			dev_info(dev, "testbus_sel: 0x%lx Index: %d val: 0x%lx\n",
				 arm_smmu_debug_tcu_testbus_select(phys_addr,
				 tcu_base, PTW_AND_CACHE_TESTBUS, READ, 0), i,
				 arm_smmu_debug_tcu_testbus_output(phys_addr));
	}
}

void arm_smmu_debug_dump_tcu_testbus(struct device *dev, phys_addr_t phys_addr,
			void __iomem *tcu_base,	int tcu_testbus_sel)
{
	int i;

	if (tcu_testbus_sel & TCU_CACHE_TESTBUS_SEL) {
		dev_info(dev, "Dumping TCU cache testbus:\n");
		arm_smmu_debug_program_tcu_testbus(dev, phys_addr, tcu_base,
				TCU_CACHE_TESTBUS, 0, 1, 0, false);
		arm_smmu_debug_program_tcu_testbus(dev, phys_addr, tcu_base,
						   ~TCU_PTW_QUEUE_MASK, 0,
						   TCU_CACHE_LOOKUP_QUEUE_SIZE,
						   2, true);
	}

	if (tcu_testbus_sel & TCU_PTW_TESTBUS_SEL) {
		dev_info(dev, "Dumping TCU PTW test bus:\n");
		arm_smmu_debug_program_tcu_testbus(dev, phys_addr, tcu_base, 1,
				TCU_PTW_TESTBUS, TCU_PTW_TESTBUS + 1, 0, false);

		arm_smmu_debug_program_tcu_testbus(dev, phys_addr, tcu_base,
						~TCU_PTW_INTERNAL_STATES_MASK,
						   0, TCU_PTW_INTERNAL_STATES,
						   2, true);

		for (i = TCU_PTW_QUEUE_START;
			i < TCU_PTW_QUEUE_START + TCU_PTW_QUEUE_SIZE; ++i) {
			arm_smmu_debug_program_tcu_testbus(dev, phys_addr,
							   tcu_base,
							   ~TCU_PTW_QUEUE_MASK,
							   i, i + 1, 2, true);
			arm_smmu_debug_program_tcu_testbus(dev, phys_addr,
						tcu_base,
						~TCU_PTW_TESTBUS_SEL2_MASK,
						TCU_PTW_TESTBUS_SEL2,
						TCU_PTW_TESTBUS_SEL2 + 1, 0,
						false);
			dev_info(dev, "testbus_sel: 0x%lx Index: %d val: 0x%lx\n",
				 arm_smmu_debug_tcu_testbus_select(phys_addr,
				 tcu_base, PTW_AND_CACHE_TESTBUS, READ, 0), i,
				 arm_smmu_debug_tcu_testbus_output(phys_addr));
		}
	}

	if (tcu_testbus_sel & TCU_CD_TESTBUS_SEL) {
		dev_info(dev, "Dumping TCU CD testbus:\n");
		arm_smmu_debug_program_tcu_testbus(dev, phys_addr, tcu_base,
				TCU_CD_TESTBUS, 0, 1, 0, false);
		arm_smmu_debug_program_tcu_testbus(dev, phys_addr, tcu_base,
						   ~TCU_PTW_QUEUE_MASK, 1,
						   2, TCU_CD_TESTBUS_SHIFT, true);
	}

	/* program ARM_SMMU_TESTBUS_SEL_HLOS1_NS to select TCU clk testbus*/
	arm_smmu_debug_tcu_testbus_select(phys_addr, tcu_base,
			CLK_TESTBUS, WRITE, TCU_CLK_TESTBUS_SEL);
	dev_info(dev, "Programming Tcu clk gate controller: testbus_sel: 0x%lx\n",
		arm_smmu_debug_tcu_testbus_select(phys_addr, tcu_base,
						CLK_TESTBUS, READ, 0));
}

void arm_smmu_debug_set_tnx_tcr_cntl(void __iomem *tbu_base, u64 val)
{
	u64 tcr_cntl_val = readq_relaxed(tbu_base + TNX_TCR_CNTL);

	/* Don't override OT_CAPTURE configuration*/
	if (!(tcr_cntl_val & TNX_TCR_CNTL_TBU_OT_CAPTURE_EN))
		writeq_relaxed(val, tbu_base + TNX_TCR_CNTL);
	else
		pr_err_ratelimited("OT capture enbl, skip TCR CNTL write\n");
}

u64 arm_smmu_debug_get_tnx_tcr_cntl(void __iomem *tbu_base)
{
	return readq_relaxed(tbu_base + TNX_TCR_CNTL);
}

void arm_smmu_debug_set_mask_and_match(void __iomem *tbu_base, u64 sel,
					u64 mask, u64 match)
{
	writeq_relaxed(mask, tbu_base + ARM_SMMU_CAPTURE1_MASK(sel));
	writeq_relaxed(match, tbu_base + ARM_SMMU_CAPTURE1_MATCH(sel));
}

void arm_smmu_debug_get_mask_and_match(void __iomem *tbu_base, u64 *mask,
					u64 *match)
{
	int i;

	for (i = 0; i < NO_OF_MASK_AND_MATCH; ++i) {
		mask[i] = readq_relaxed(tbu_base +
				ARM_SMMU_CAPTURE1_MASK(i+1));
		match[i] = readq_relaxed(tbu_base +
				ARM_SMMU_CAPTURE1_MATCH(i+1));
	}
}

void arm_smmu_debug_get_capture_snapshot(void __iomem *tbu_base,
		u64 snapshot[NO_OF_CAPTURE_POINTS][REGS_PER_CAPTURE_POINT])
{
	int  i, j;
	u64 valid;

	valid = readl_relaxed(tbu_base + TNX_TCR_CNTL_2);

	for (i = 0; i < NO_OF_CAPTURE_POINTS ; ++i) {
		if (valid & BIT(i))
			for (j = 0; j < REGS_PER_CAPTURE_POINT; ++j)
				snapshot[i][j] = readq_relaxed(tbu_base +
					ARM_SMMU_CAPTURE_SNAPSHOT(i, j));
		else
			for (j = 0; j < REGS_PER_CAPTURE_POINT; ++j)
				snapshot[i][j] = 0xdededede;
	}
}

void arm_smmu_debug_clear_intr_and_validbits(void __iomem *tbu_base)
{
	u64 val = 0;

	val |= INTR_CLR | RESET_VALID;
	writeq_relaxed(val, tbu_base + TNX_TCR_CNTL);
}
