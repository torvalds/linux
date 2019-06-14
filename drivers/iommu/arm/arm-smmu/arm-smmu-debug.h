/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */

#define ARM_SMMU_TESTBUS_SEL			0x25E4
#define ARM_SMMU_TESTBUS			0x25E8
#define ARM_SMMU_TESTBUS_SEL_HLOS1_NS		0x8
#define DEBUG_TESTBUS_SEL_TBU			0x50
#define DEBUG_TESTBUS_TBU			0x58

#define TCU_PTW_TESTBUS				BIT(8)
#define TCU_CACHE_TESTBUS			~TCU_PTW_TESTBUS
#define TCU_PTW_TESTBUS_SEL			BIT(1)
#define TCU_PTW_INTERNAL_STATES			3
#define TCU_PTW_INTERNAL_STATES_MASK		GENMASK(7, 2)
#define TCU_PTW_TESTBUS_SEL2			3
#define TCU_PTW_TESTBUS_SEL2_MASK		GENMASK(1, 0)
#define TCU_PTW_QUEUE_START			32
#define TCU_PTW_QUEUE_SIZE			32
#define TCU_PTW_QUEUE_MASK			GENMASK(7, 0)
#define TCU_CACHE_TESTBUS_SEL			0x1
#define TCU_CACHE_LOOKUP_QUEUE_SIZE		32
#define TCU_CLK_TESTBUS_SEL			0x200


#define TBU_CLK_GATE_CONTROLLER_TESTBUS_SEL	0x1
#define TBU_QNS4_A2Q_TESTBUS_SEL		BIT(1)
#define TBU_QNS4_Q2A_TESTBUS_SEL		BIT(2)
#define TBU_MULTIMASTER_QCHANNEL_TESTBUS_SEL	BIT(3)
#define TBU_CLK_GATE_CONTROLLER_TESTBUS		BIT(6)
#define TBU_QNS4_A2Q_TESTBUS			BIT(7)
#define TBU_QNS4_Q2A_TESTBUS			(BIT(5) | BIT(7))
#define TBU_MULTIMASTER_QCHANNEL_TESTBUS	GENMASK(7, 6)
#define TBU_QNS4_BRIDGE_SIZE			32
#define TBU_QNS4_BRIDGE_MASK			GENMASK(4, 0)

extern int tbu_testbus_sel;
extern int tcu_testbus_sel;

enum tcu_testbus {
	PTW_AND_CACHE_TESTBUS,
	CLK_TESTBUS,
};

enum testbus_sel {
	SEL_TCU,
	SEL_TBU,
};

enum testbus_ops {
	TESTBUS_SELECT,
	TESTBUS_OUTPUT,
};

#if IS_ENABLED(CONFIG_ARM_SMMU)

u32 arm_smmu_debug_tbu_testbus_select(void __iomem *tbu_base,
					bool write, u32 val);
u32 arm_smmu_debug_tbu_testbus_output(void __iomem *tbu_base);
u32 arm_smmu_debug_tcu_testbus_select(void __iomem *base,
		void __iomem *tcu_base, enum tcu_testbus testbus,
		bool write, u32 val);
u32 arm_smmu_debug_tcu_testbus_output(void __iomem *base);
void arm_smmu_debug_dump_tbu_testbus(struct device *dev, void __iomem *tbu_base,
			int tbu_testbus_sel);
void arm_smmu_debug_dump_tcu_testbus(struct device *dev, void __iomem *base,
			void __iomem *tcu_base, int tcu_testbus_sel);

#else
static inline u32 arm_smmu_debug_tbu_testbus_select(void __iomem *tbu_base,
				bool write, u32 val)
{
	return 0;
}
static inline u32 arm_smmu_debug_tbu_testbus_output(void __iomem *tbu_base)
{
	return 0;
}
u32 arm_smmu_debug_tcu_testbus_select(void __iomem *base,
		void __iomem *tcu_base, enum tcu_testbus testbus,
		bool write, u32 val)
{
}
static inline u32 arm_smmu_debug_tcu_testbus_output(void __iomem *base)
{
	return 0;
}
static inline void arm_smmu_debug_dump_tbu_testbus(struct device *dev,
			void __iomem *tbu_base, int tbu_testbus_sel)
{
}
static inline void arm_smmu_debug_dump_tcu_testbus(struct device *dev,
			void __iomem *base, void __iomem *tcu_base,
			int tcu_testbus_sel)
{
}
#endif

