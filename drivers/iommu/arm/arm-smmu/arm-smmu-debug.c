// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/device.h>
#include "arm-smmu.h"
#include "arm-smmu-debug.h"


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

u32 arm_smmu_debug_tcu_testbus_select(void __iomem *base,
		void __iomem *tcu_base, enum tcu_testbus testbus,
		bool write, u32 val)
{
	int offset;

	if (testbus == CLK_TESTBUS) {
		base = tcu_base;
		offset = ARM_SMMU_TESTBUS_SEL_HLOS1_NS;
	} else {
		offset = ARM_SMMU_TESTBUS_SEL;
	}

	if (write) {
		writel_relaxed(val, base + offset);
		/* Make sure tcu select register is written to */
		wmb();
	} else {
		return readl_relaxed(base + offset);
	}

	return 0;
}

u32 arm_smmu_debug_tcu_testbus_output(void __iomem *base)
{
	return readl_relaxed(base + ARM_SMMU_TESTBUS);
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
	reg = (reg & ~TCU_PTW_QUEUE_MASK) | tbu_testbus;
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
}

static void arm_smmu_debug_program_tcu_testbus(struct device *dev,
		void __iomem *base, void __iomem *tcu_base,
		unsigned long mask, int start, int end, int shift,
		bool print)
{
	u32 reg;
	int i;

	for (i = start; i < end; i++) {
		reg = arm_smmu_debug_tcu_testbus_select(base, tcu_base,
				PTW_AND_CACHE_TESTBUS, READ, 0);
		reg &= mask;
		reg |= i << shift;
		arm_smmu_debug_tcu_testbus_select(base, tcu_base,
				PTW_AND_CACHE_TESTBUS, WRITE, reg);
		if (print)
			dev_info(dev, "testbus_sel: 0x%lx Index: %d val: 0x%lx\n",
				 arm_smmu_debug_tcu_testbus_select(base,
				 tcu_base, PTW_AND_CACHE_TESTBUS, READ, 0),
				 i, arm_smmu_debug_tcu_testbus_output(base));
	}
}

void arm_smmu_debug_dump_tcu_testbus(struct device *dev, void __iomem *base,
			void __iomem *tcu_base, int tcu_testbus_sel)
{
	int i;

	if (tcu_testbus_sel & TCU_CACHE_TESTBUS_SEL) {
		dev_info(dev, "Dumping TCU cache testbus:\n");
		arm_smmu_debug_program_tcu_testbus(dev, base, tcu_base,
				TCU_CACHE_TESTBUS, 0, 1, 0, false);
		arm_smmu_debug_program_tcu_testbus(dev, base, tcu_base,
						   ~TCU_PTW_QUEUE_MASK, 0,
						   TCU_CACHE_LOOKUP_QUEUE_SIZE,
						   2, true);
	}

	if (tcu_testbus_sel & TCU_PTW_TESTBUS_SEL) {
		dev_info(dev, "Dumping TCU PTW test bus:\n");
		arm_smmu_debug_program_tcu_testbus(dev, base, tcu_base, 1,
				TCU_PTW_TESTBUS, TCU_PTW_TESTBUS + 1, 0, false);

		arm_smmu_debug_program_tcu_testbus(dev, base, tcu_base,
						~TCU_PTW_INTERNAL_STATES_MASK,
						   0, TCU_PTW_INTERNAL_STATES,
						   2, true);

		for (i = TCU_PTW_QUEUE_START;
			i < TCU_PTW_QUEUE_START + TCU_PTW_QUEUE_SIZE; ++i) {
			arm_smmu_debug_program_tcu_testbus(dev, base, tcu_base,
					~TCU_PTW_QUEUE_MASK, i, i + 1, 2, true);
			arm_smmu_debug_program_tcu_testbus(dev, base, tcu_base,
					~TCU_PTW_TESTBUS_SEL2_MASK, TCU_PTW_TESTBUS_SEL2,
					TCU_PTW_TESTBUS_SEL2 + 1, 0, false);
			dev_info(dev, "testbus_sel: 0x%lx Index: %d val: 0x%lx\n",
				 arm_smmu_debug_tcu_testbus_select(base,
				 tcu_base, PTW_AND_CACHE_TESTBUS, READ, 0),
				 i, arm_smmu_debug_tcu_testbus_output(base));
		}
	}

	/* program ARM_SMMU_TESTBUS_SEL_HLOS1_NS to select TCU clk testbus*/
	arm_smmu_debug_tcu_testbus_select(base, tcu_base,
			CLK_TESTBUS, WRITE, TCU_CLK_TESTBUS_SEL);
	dev_info(dev, "Programming Tcu clk gate controller: testbus_sel: 0x%lx\n",
		arm_smmu_debug_tcu_testbus_select(base, tcu_base,
						CLK_TESTBUS, READ, 0));
}
