/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifdef CONFIG_ARM_SMMU_TESTBUS_DUMP_GEN3AUTO
#define ARM_SMMU_TESTBUS_SEL                    0x2524
#define ARM_SMMU_TESTBUS                        0x2528
#define ARM_SMMU_TCU_TESTBUS_HLOS1_NS		ARM_SMMU_TESTBUS_SEL_HLOS1_NS
#else
#define ARM_SMMU_TESTBUS_SEL			0x25E4
#define ARM_SMMU_TESTBUS			0x25E8
#define ARM_SMMU_TCU_TESTBUS_HLOS1_NS		0x28
#endif
#define ARM_SMMU_TESTBUS_SEL_HLOS1_NS		0x8
#define DEBUG_TESTBUS_SEL_TBU			0x50
#define DEBUG_TESTBUS_TBU			0x58

#define DebugChainQTB_debug_Load		0x8
#define DebugChainQTB_debug_Dump_Low		0x10
#define DebugChainQTB_debug_Dump_High		0x14
#define DebugChainQTB_debug_ShiftRegLen		0x18
#define Qtb500_QtbNsDbgQsmStatus		0xc00
#define Qtb500_QtbNsDbgIdleStatus		0xc08


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
#define TCU_CLK_TESTBUS_SEL			0x300
#define TCU_CD_TESTBUS_SEL			BIT(2)
#define TCU_CD_TESTBUS				BIT(8)
#define TCU_CD_TESTBUS_SHIFT			3


#define TBU_CLK_GATE_CONTROLLER_TESTBUS_SEL	0x1
#define TBU_QNS4_A2Q_TESTBUS_SEL		BIT(1)
#define TBU_QNS4_Q2A_TESTBUS_SEL		BIT(2)
#define TBU_MULTIMASTER_QCHANNEL_TESTBUS_SEL	BIT(3)
#define TBU_CLK_GATE_CONTROLLER_EXT_TESTBUS_SEL	BIT(4)
#define TBU_LOW_POWER_STATUS_TESTBUS_SEL	BIT(5)
#define TBU_QNS4_VLD_RDY_SEL			BIT(6)
#define TBU_CLK_GATE_CONTROLLER_TESTBUS		BIT(6)
#define TBU_QNS4_A2Q_TESTBUS			BIT(7)
#define TBU_QNS4_Q2A_TESTBUS			(BIT(5) | BIT(7))
#define TBU_MULTIMASTER_QCHANNEL_TESTBUS	GENMASK(7, 6)
#define TBU_CLK_GATE_CONTROLLER_EXT_TESTBUS	BIT(8)
#define TBU_LOW_POWER_STATUS_TESTBUS		(BIT(8) | BIT(6))
#define TBU_QNS4_VLD_RDY			(BIT(8) | BIT(7))
#define TBU_MASK				GENMASK(8, 0)
#define TBU_QNS4_BRIDGE_SIZE			32
#define TBU_QNS4_BRIDGE_MASK			GENMASK(4, 0)

extern int tbu_testbus_sel;
extern int tcu_testbus_sel;

#define TNX_TCR_CNTL			0x130
#define TNX_TCR_CNTL_TBU_OT_CAPTURE_EN	BIT(18)
#define TNX_TCR_CNTL_ALWAYS_CAPTURE	BIT(15)
#define TNX_TCR_CNTL_MATCH_MASK_UPD	BIT(7)
#define TNX_TCR_CNTL_MATCH_MASK_VALID	BIT(6)

#define CAPTURE1_SNAPSHOT_1		0x138

#define TNX_TCR_CNTL_2			0x178
#define TNX_TCR_CNTL_2_CAP1_VALID	BIT(0)

#define ARM_SMMU_CAPTURE1_MASK(i)	(0x100 + (0x8 * (i-1)))
#define ARM_SMMU_CAPTURE1_MATCH(i)	(0x118 + (0x8 * (i-1)))
#define ARM_SMMU_CAPTURE_SNAPSHOT(i, j)	(CAPTURE1_SNAPSHOT_1 + \
					 (0x10 * (i)) + ((j) * 0x8))

#define NO_OF_MASK_AND_MATCH		0x3
#define NO_OF_CAPTURE_POINTS		0x4
#define REGS_PER_CAPTURE_POINT		0x2
#define INTR_CLR			BIT(0)
#define RESET_VALID			BIT(7)

#define TTQTB_SET				0x1
#define TTQTB_RESET_VAL				0x0
#define TTQTB_LogAsstEn				BIT(8)
#define TTQTB_LogAll				BIT(7)
#define TTQTB_GlbEn				BIT(0)
#define TTQTB_IgnoreCtiTrigIn0			BIT(5)
#define TransTrackerQTB_MainCtl                 0x8
#define TransTrackerQTB_AlarmEn                 0x10
#define TransTrackerQTB_AlarmStatus             0x18
#define TransTrackerQTB_AlarmClr                0x20
#define TransTrackerQTB_LogClr			0x30
#define TransTrackerQTB_LogInVld_Low		0x38
#define TransTrackerQTB_LogOutVld_Low		0x40
#define TransTrackerQTB_LogOutErr_Low		0x48

#define TTQTB_Capture_Points			0x8
#define TTQTB_Regs_Per_Capture_Points		0x4

#define TTQTB_TimeStamp				0xa0
#define TransTrackerQTB_TimeStamp(i)		(TTQTB_TimeStamp + \
						(0x8 * (i)))

#define TTQTB_Latency				0x50
#define TransTrackerQTB_Latency(i)		(TTQTB_Latency + \
						(0x8 * (i)))

#define TTQTB_LogIn_Low				0x200
#define TTQTB_LogIn_High			0x204
#define TransTrackerQTB_LogIn_Low(i, j)  (TTQTB_LogIn_Low + \
						(0x20 * (i)) + (0x8 * (j)))
#define TransTrackerQTB_LogIn_High(i, j) (TTQTB_LogIn_High + \
						(0x20 * (i)) + (0x8 * (j)))

#define TTQTB_LogOut_Low			0x300
#define TTQTB_LogOut_High			0x304
#define TransTrackerQTB_LogOut_Low(i, j)  (TTQTB_LogIn_Low + \
						(0x20 * (i)) + (0x8 * (j)))
#define TransTrackerQTB_LogOut_High(i, j) (TTQTB_LogIn_High + \
						(0x20 * (i)) + (0x8 * (j)))

#define TTQTB_Filter_DevNeEn			BIT(0)
#define TTQTB_Filter_DevEEn			BIT(1)
#define TTQTB_Filter_NormalEn			BIT(2)
#define TTQTB_Filter_CachedEn			BIT(3)
#define TTQTB_Filter_SharedEn			BIT(4)
#define TTQTB_Filter_PostedEn			BIT(5)
#define TTQTB_Filter_OpCode_Set_Val		0x1f
#define TTQTB_Filter_Alloc_Set_Val		0xe3ef
#define TTQTB_Filter_Length_Set_Val		0x1ff
#define TransTrackerQTB_Filter_TrType		0x168
#define TransTrackerQTB_Filter_Addr_Min_Low	0x120
#define TransTrackerQTB_Filter_Addr_Min_High	0x124
#define TransTrackerQTB_Filter_Addr_Max_Low	0x128
#define TransTrackerQTB_Filter_Addr_Max_High	0x12C
#define TransTrackerQTB_gfx_Filter_Addr_Min	0x120
#define TransTrackerQTB_gfx_Filter_Addr_Max	0x128
#define TransTrackerQTB_Filter_OpCode		0x138
#define TransTrackerQTB_Filter_ReqUser_Base	0x148
#define TransTrackerQTB_Filter_ReqUser_Mask	0x150
#define TransTrackerQTB_Filter_LogUser_Base	0x158
#define TransTrackerQTB_Filter_LogUser_Mask	0x160
#define TransTrackerQTB_Filter_Alloc		0x170
#define TransTrackerQTB_Filter_ExtId_Base	0x178
#define TransTrackerQTB_Filter_ExtId_Mask	0x180
#define TransTrackerQTB_Filter_Length		0x188
#define TransTrackerQTB_Filter_Urgency		0x190
#define TransTrackerQTB_Filter_CacheIndex_Base	0x198
#define TransTrackerQTB_Filter_CacheIndex_Mask	0x1A0


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

u32 arm_smmu_debug_qtb_debugchain_load(void __iomem *debugchain_base);
u64 arm_smmu_debug_qtb_debugchain_dump(void __iomem *debugchain_base);
void arm_smmu_debug_dump_debugchain(struct device *dev, void __iomem *debugchain_base);
void arm_smmu_debug_dump_qtb_regs(struct device *dev, void __iomem *tbu_base);
void arm_smmu_debug_qtb_transtracker_set_config(void __iomem *transactiontracker_base, u64 sel);
u64 arm_smmu_debug_qtb_transtracker_get_config(void __iomem *transactiontracker_base);
void arm_smmu_debug_qtb_transtracker_setfilter(void __iomem *transactiontracker_base,
		u64 sel, u64 filter, int qtb_type);
void arm_smmu_debug_qtb_transtracker_getfilter(void __iomem *transactiontracker_base,
		u64 filter[3], int qtb_type);
void arm_smmu_debug_qtb_transtrac_reset(void __iomem *transactiontracker_base);
void arm_smmu_debug_qtb_transtrac_collect(void __iomem *transactiontracker_base,
		u64 gfxttlogs[TTQTB_Capture_Points][2*TTQTB_Regs_Per_Capture_Points],
		u64 ttlogs[TTQTB_Capture_Points][4*TTQTB_Regs_Per_Capture_Points],
		u64 ttlogs_time[2*TTQTB_Capture_Points], int qtb_type);
u32 arm_smmu_debug_tbu_testbus_select(void __iomem *tbu_base,
					bool write, u32 val);
u32 arm_smmu_debug_tbu_testbus_output(void __iomem *tbu_base);
u32 arm_smmu_debug_tcu_testbus_select(phys_addr_t phys_addr,
		void __iomem *tcu_base, enum tcu_testbus testbus,
		bool write, u32 val);
u32 arm_smmu_debug_tcu_testbus_output(phys_addr_t phys_addr);
void arm_smmu_debug_dump_tbu_testbus(struct device *dev, void __iomem *tbu_base,
			int tbu_testbus_sel);
void arm_smmu_debug_dump_tcu_testbus(struct device *dev, phys_addr_t phys_addr,
			void __iomem *tcu_base, int tcu_testbus_sel);
void arm_smmu_debug_set_tnx_tcr_cntl(void __iomem *tbu_base, u64 val);
u64 arm_smmu_debug_get_tnx_tcr_cntl(void __iomem *tbu_base);
void arm_smmu_debug_set_mask_and_match(void __iomem *tbu_base, u64 sel,
					u64 mask, u64 match);
void arm_smmu_debug_get_mask_and_match(void __iomem *tbu_base,
					u64 *mask, u64 *match);
void arm_smmu_debug_get_capture_snapshot(void __iomem *tbu_base,
		u64 snapshot[NO_OF_CAPTURE_POINTS][REGS_PER_CAPTURE_POINT]);
void arm_smmu_debug_clear_intr_and_validbits(void __iomem *tbu_base);
#else
u32 arm_smmu_debug_qtb_debugchain_load(void __iomem *debugchain_base);
{
	return 0;
}
u64 arm_smmu_debug_qtb_debugchain_dump(void __iomem *debugchain_base);
{
	return 0;
}
void arm_smmu_debug_dump_debugchain(struct device *dev, void __iomem *debugchain_base);
{
}
void arm_smmu_debug_dump_qtb_regs(struct device *dev, void __iomem *tbu_base)
{
}
u64 arm_smmu_debug_transtracker_get_config(void __iomem *transactiontracker_base)
{
	return 0;
}
void arm_smmu_debug_transtracker_setfilter(void __iomem *transactiontracker_base,
		u64 sel, u64 filter, int qtb_type)
{
}
void arm_smmu_debug_transtracker_getfilter(void __iomem *transactiontracker_base,
		u64 filter[3], int qtb_type)
{
}
void arm_smmu_debug_qtb_transtrac_reset(void __iomem *transactiontracker_base)
{
}
void arm_smmu_debug_configure_transtracker_custom(void __iomem *transactiontracker_base,
		u64 trtype, u64 addrmax)
{
}
void arm_smmu_debug_qtb_transtrac_collect(void __iomem *transactiontracker_base,
		u64 gfxttlogs[TTQTB_Capture_Points][2*TTQTB_Regs_Per_Capture_Points],
		u64 ttlogs[TTQTB_Capture_Points][4*TTQTB_Regs_Per_Capture_Points],
		u64 ttlogs_time[2*TTQTB_Capture_Points], int qtb_type)
{
}
static inline u32 arm_smmu_debug_tbu_testbus_select(void __iomem *tbu_base,
				bool write, u32 val)
{
	return 0;
}
static inline u32 arm_smmu_debug_tbu_testbus_output(void __iomem *tbu_base)
{
	return 0;
}
u32 arm_smmu_debug_tcu_testbus_select(phys_addr_t phys_addr,
		void __iomem *tcu_base, enum tcu_testbus testbus,
		bool write, u32 val)
{
}
static inline u32 arm_smmu_debug_tcu_testbus_output(phys_addr_t phys_addr)
{
	return 0;
}
static inline void arm_smmu_debug_dump_tbu_testbus(struct device *dev,
			void __iomem *tbu_base, int tbu_testbus_sel)
{
}
static inline void arm_smmu_debug_dump_tcu_testbus(struct device *dev,
			phys_addr_t phys_addr, void __iomem *tcu_base,
			int tcu_testbus_sel)
{
}
static inline void arm_smmu_debug_set_tnx_tcr_cntl(void __iomem *tbu_base,
						   u64 val)
{
}
static inline u64 arm_smmu_debug_get_tnx_tcr_cntl(void __iomem *tbu_base)
{
	return 0;
}

static inline void arm_smmu_debug_set_mask_and_match(void __iomem *tbu_base, u64
						     sel, u64 mask, u64 match)
{
}
static inline void arm_smmu_debug_get_mask_and_match(void __iomem *tbu_base,
					u64 *mask, u64 *match)
{
}
static inline void arm_smmu_debug_get_capture_snapshot(void __iomem *tbu_base,
		u64 snapshot[NO_OF_CAPTURE_POINTS][REGS_PER_CAPTURE_POINT])
{
}
static inline void arm_smmu_debug_clear_intr_and_validbits(void __iomem
							   *tbu_base)
{
}
#endif

