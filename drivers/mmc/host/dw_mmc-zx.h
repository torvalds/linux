/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DW_MMC_ZX_H_
#define _DW_MMC_ZX_H_

/* ZX296718 SoC specific DLL register offset. */
#define LB_AON_EMMC_CFG_REG0  0x1B0
#define LB_AON_EMMC_CFG_REG1  0x1B4
#define LB_AON_EMMC_CFG_REG2  0x1B8

/* LB_AON_EMMC_CFG_REG0 register defines */
#define PARA_DLL_START(x)	((x) & 0xFF)
#define PARA_DLL_START_MASK	0xFF
#define DLL_REG_SET		BIT(8)
#define PARA_DLL_LOCK_NUM(x)	(((x) & 7) << 16)
#define PARA_DLL_LOCK_NUM_MASK  (7 << 16)
#define PARA_PHASE_DET_SEL(x)	(((x) & 7) << 20)
#define PARA_PHASE_DET_SEL_MASK	(7 << 20)
#define PARA_DLL_BYPASS_MODE	BIT(23)
#define PARA_HALF_CLK_MODE	BIT(24)

/* LB_AON_EMMC_CFG_REG1 register defines */
#define READ_DQS_DELAY(x)	((x) & 0x7F)
#define READ_DQS_DELAY_MASK	(0x7F)
#define READ_DQS_BYPASS_MODE	BIT(7)
#define CLK_SAMP_DELAY(x)	(((x) & 0x7F) << 8)
#define CLK_SAMP_DELAY_MASK	(0x7F << 8)
#define CLK_SAMP_BYPASS_MODE	BIT(15)

/* LB_AON_EMMC_CFG_REG2 register defines */
#define ZX_DLL_LOCKED		BIT(2)

#endif /* _DW_MMC_ZX_H_ */
