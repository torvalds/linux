/*
 * U300 clock implementation
 * Copyright (C) 2007-2012 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/spinlock.h>
#include <linux/of.h>

/* APP side SYSCON registers */
/* CLK Control Register 16bit (R/W) */
#define U300_SYSCON_CCR						(0x0000)
#define U300_SYSCON_CCR_I2S1_USE_VCXO				(0x0040)
#define U300_SYSCON_CCR_I2S0_USE_VCXO				(0x0020)
#define U300_SYSCON_CCR_TURN_VCXO_ON				(0x0008)
#define U300_SYSCON_CCR_CLKING_PERFORMANCE_MASK			(0x0007)
#define U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER		(0x04)
#define U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW			(0x03)
#define U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE		(0x02)
#define U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH			(0x01)
#define U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST			(0x00)
/* CLK Status Register 16bit (R/W) */
#define U300_SYSCON_CSR						(0x0004)
#define U300_SYSCON_CSR_PLL208_LOCK_IND				(0x0002)
#define U300_SYSCON_CSR_PLL13_LOCK_IND				(0x0001)
/* Reset lines for SLOW devices 16bit (R/W) */
#define U300_SYSCON_RSR						(0x0014)
#define U300_SYSCON_RSR_PPM_RESET_EN				(0x0200)
#define U300_SYSCON_RSR_ACC_TMR_RESET_EN			(0x0100)
#define U300_SYSCON_RSR_APP_TMR_RESET_EN			(0x0080)
#define U300_SYSCON_RSR_RTC_RESET_EN				(0x0040)
#define U300_SYSCON_RSR_KEYPAD_RESET_EN				(0x0020)
#define U300_SYSCON_RSR_GPIO_RESET_EN				(0x0010)
#define U300_SYSCON_RSR_EH_RESET_EN				(0x0008)
#define U300_SYSCON_RSR_BTR_RESET_EN				(0x0004)
#define U300_SYSCON_RSR_UART_RESET_EN				(0x0002)
#define U300_SYSCON_RSR_SLOW_BRIDGE_RESET_EN			(0x0001)
/* Reset lines for FAST devices 16bit (R/W) */
#define U300_SYSCON_RFR						(0x0018)
#define U300_SYSCON_RFR_UART1_RESET_ENABLE			(0x0080)
#define U300_SYSCON_RFR_SPI_RESET_ENABLE			(0x0040)
#define U300_SYSCON_RFR_MMC_RESET_ENABLE			(0x0020)
#define U300_SYSCON_RFR_PCM_I2S1_RESET_ENABLE			(0x0010)
#define U300_SYSCON_RFR_PCM_I2S0_RESET_ENABLE			(0x0008)
#define U300_SYSCON_RFR_I2C1_RESET_ENABLE			(0x0004)
#define U300_SYSCON_RFR_I2C0_RESET_ENABLE			(0x0002)
#define U300_SYSCON_RFR_FAST_BRIDGE_RESET_ENABLE		(0x0001)
/* Reset lines for the rest of the peripherals 16bit (R/W) */
#define U300_SYSCON_RRR						(0x001c)
#define U300_SYSCON_RRR_CDS_RESET_EN				(0x4000)
#define U300_SYSCON_RRR_ISP_RESET_EN				(0x2000)
#define U300_SYSCON_RRR_INTCON_RESET_EN				(0x1000)
#define U300_SYSCON_RRR_MSPRO_RESET_EN				(0x0800)
#define U300_SYSCON_RRR_XGAM_RESET_EN				(0x0100)
#define U300_SYSCON_RRR_XGAM_VC_SYNC_RESET_EN			(0x0080)
#define U300_SYSCON_RRR_NANDIF_RESET_EN				(0x0040)
#define U300_SYSCON_RRR_EMIF_RESET_EN				(0x0020)
#define U300_SYSCON_RRR_DMAC_RESET_EN				(0x0010)
#define U300_SYSCON_RRR_CPU_RESET_EN				(0x0008)
#define U300_SYSCON_RRR_APEX_RESET_EN				(0x0004)
#define U300_SYSCON_RRR_AHB_RESET_EN				(0x0002)
#define U300_SYSCON_RRR_AAIF_RESET_EN				(0x0001)
/* Clock enable for SLOW peripherals 16bit (R/W) */
#define U300_SYSCON_CESR					(0x0020)
#define U300_SYSCON_CESR_PPM_CLK_EN				(0x0200)
#define U300_SYSCON_CESR_ACC_TMR_CLK_EN				(0x0100)
#define U300_SYSCON_CESR_APP_TMR_CLK_EN				(0x0080)
#define U300_SYSCON_CESR_KEYPAD_CLK_EN				(0x0040)
#define U300_SYSCON_CESR_GPIO_CLK_EN				(0x0010)
#define U300_SYSCON_CESR_EH_CLK_EN				(0x0008)
#define U300_SYSCON_CESR_BTR_CLK_EN				(0x0004)
#define U300_SYSCON_CESR_UART_CLK_EN				(0x0002)
#define U300_SYSCON_CESR_SLOW_BRIDGE_CLK_EN			(0x0001)
/* Clock enable for FAST peripherals 16bit (R/W) */
#define U300_SYSCON_CEFR					(0x0024)
#define U300_SYSCON_CEFR_UART1_CLK_EN				(0x0200)
#define U300_SYSCON_CEFR_I2S1_CORE_CLK_EN			(0x0100)
#define U300_SYSCON_CEFR_I2S0_CORE_CLK_EN			(0x0080)
#define U300_SYSCON_CEFR_SPI_CLK_EN				(0x0040)
#define U300_SYSCON_CEFR_MMC_CLK_EN				(0x0020)
#define U300_SYSCON_CEFR_I2S1_CLK_EN				(0x0010)
#define U300_SYSCON_CEFR_I2S0_CLK_EN				(0x0008)
#define U300_SYSCON_CEFR_I2C1_CLK_EN				(0x0004)
#define U300_SYSCON_CEFR_I2C0_CLK_EN				(0x0002)
#define U300_SYSCON_CEFR_FAST_BRIDGE_CLK_EN			(0x0001)
/* Clock enable for the rest of the peripherals 16bit (R/W) */
#define U300_SYSCON_CERR					(0x0028)
#define U300_SYSCON_CERR_CDS_CLK_EN				(0x2000)
#define U300_SYSCON_CERR_ISP_CLK_EN				(0x1000)
#define U300_SYSCON_CERR_MSPRO_CLK_EN				(0x0800)
#define U300_SYSCON_CERR_AHB_SUBSYS_BRIDGE_CLK_EN		(0x0400)
#define U300_SYSCON_CERR_SEMI_CLK_EN				(0x0200)
#define U300_SYSCON_CERR_XGAM_CLK_EN				(0x0100)
#define U300_SYSCON_CERR_VIDEO_ENC_CLK_EN			(0x0080)
#define U300_SYSCON_CERR_NANDIF_CLK_EN				(0x0040)
#define U300_SYSCON_CERR_EMIF_CLK_EN				(0x0020)
#define U300_SYSCON_CERR_DMAC_CLK_EN				(0x0010)
#define U300_SYSCON_CERR_CPU_CLK_EN				(0x0008)
#define U300_SYSCON_CERR_APEX_CLK_EN				(0x0004)
#define U300_SYSCON_CERR_AHB_CLK_EN				(0x0002)
#define U300_SYSCON_CERR_AAIF_CLK_EN				(0x0001)
/* Single block clock enable 16bit (-/W) */
#define U300_SYSCON_SBCER					(0x002c)
#define U300_SYSCON_SBCER_PPM_CLK_EN				(0x0009)
#define U300_SYSCON_SBCER_ACC_TMR_CLK_EN			(0x0008)
#define U300_SYSCON_SBCER_APP_TMR_CLK_EN			(0x0007)
#define U300_SYSCON_SBCER_KEYPAD_CLK_EN				(0x0006)
#define U300_SYSCON_SBCER_GPIO_CLK_EN				(0x0004)
#define U300_SYSCON_SBCER_EH_CLK_EN				(0x0003)
#define U300_SYSCON_SBCER_BTR_CLK_EN				(0x0002)
#define U300_SYSCON_SBCER_UART_CLK_EN				(0x0001)
#define U300_SYSCON_SBCER_SLOW_BRIDGE_CLK_EN			(0x0000)
#define U300_SYSCON_SBCER_UART1_CLK_EN				(0x0019)
#define U300_SYSCON_SBCER_I2S1_CORE_CLK_EN			(0x0018)
#define U300_SYSCON_SBCER_I2S0_CORE_CLK_EN			(0x0017)
#define U300_SYSCON_SBCER_SPI_CLK_EN				(0x0016)
#define U300_SYSCON_SBCER_MMC_CLK_EN				(0x0015)
#define U300_SYSCON_SBCER_I2S1_CLK_EN				(0x0014)
#define U300_SYSCON_SBCER_I2S0_CLK_EN				(0x0013)
#define U300_SYSCON_SBCER_I2C1_CLK_EN				(0x0012)
#define U300_SYSCON_SBCER_I2C0_CLK_EN				(0x0011)
#define U300_SYSCON_SBCER_FAST_BRIDGE_CLK_EN			(0x0010)
#define U300_SYSCON_SBCER_CDS_CLK_EN				(0x002D)
#define U300_SYSCON_SBCER_ISP_CLK_EN				(0x002C)
#define U300_SYSCON_SBCER_MSPRO_CLK_EN				(0x002B)
#define U300_SYSCON_SBCER_AHB_SUBSYS_BRIDGE_CLK_EN		(0x002A)
#define U300_SYSCON_SBCER_SEMI_CLK_EN				(0x0029)
#define U300_SYSCON_SBCER_XGAM_CLK_EN				(0x0028)
#define U300_SYSCON_SBCER_VIDEO_ENC_CLK_EN			(0x0027)
#define U300_SYSCON_SBCER_NANDIF_CLK_EN				(0x0026)
#define U300_SYSCON_SBCER_EMIF_CLK_EN				(0x0025)
#define U300_SYSCON_SBCER_DMAC_CLK_EN				(0x0024)
#define U300_SYSCON_SBCER_CPU_CLK_EN				(0x0023)
#define U300_SYSCON_SBCER_APEX_CLK_EN				(0x0022)
#define U300_SYSCON_SBCER_AHB_CLK_EN				(0x0021)
#define U300_SYSCON_SBCER_AAIF_CLK_EN				(0x0020)
/* Single block clock disable 16bit (-/W) */
#define U300_SYSCON_SBCDR					(0x0030)
/* Same values as above for SBCER */
/* Clock force SLOW peripherals 16bit (R/W) */
#define U300_SYSCON_CFSR					(0x003c)
#define U300_SYSCON_CFSR_PPM_CLK_FORCE_EN			(0x0200)
#define U300_SYSCON_CFSR_ACC_TMR_CLK_FORCE_EN			(0x0100)
#define U300_SYSCON_CFSR_APP_TMR_CLK_FORCE_EN			(0x0080)
#define U300_SYSCON_CFSR_KEYPAD_CLK_FORCE_EN			(0x0020)
#define U300_SYSCON_CFSR_GPIO_CLK_FORCE_EN			(0x0010)
#define U300_SYSCON_CFSR_EH_CLK_FORCE_EN			(0x0008)
#define U300_SYSCON_CFSR_BTR_CLK_FORCE_EN			(0x0004)
#define U300_SYSCON_CFSR_UART_CLK_FORCE_EN			(0x0002)
#define U300_SYSCON_CFSR_SLOW_BRIDGE_CLK_FORCE_EN		(0x0001)
/* Clock force FAST peripherals 16bit (R/W) */
#define U300_SYSCON_CFFR					(0x40)
/* Values not defined. Define if you want to use them. */
/* Clock force the rest of the peripherals 16bit (R/W) */
#define U300_SYSCON_CFRR					(0x44)
#define U300_SYSCON_CFRR_CDS_CLK_FORCE_EN			(0x2000)
#define U300_SYSCON_CFRR_ISP_CLK_FORCE_EN			(0x1000)
#define U300_SYSCON_CFRR_MSPRO_CLK_FORCE_EN			(0x0800)
#define U300_SYSCON_CFRR_AHB_SUBSYS_BRIDGE_CLK_FORCE_EN		(0x0400)
#define U300_SYSCON_CFRR_SEMI_CLK_FORCE_EN			(0x0200)
#define U300_SYSCON_CFRR_XGAM_CLK_FORCE_EN			(0x0100)
#define U300_SYSCON_CFRR_VIDEO_ENC_CLK_FORCE_EN			(0x0080)
#define U300_SYSCON_CFRR_NANDIF_CLK_FORCE_EN			(0x0040)
#define U300_SYSCON_CFRR_EMIF_CLK_FORCE_EN			(0x0020)
#define U300_SYSCON_CFRR_DMAC_CLK_FORCE_EN			(0x0010)
#define U300_SYSCON_CFRR_CPU_CLK_FORCE_EN			(0x0008)
#define U300_SYSCON_CFRR_APEX_CLK_FORCE_EN			(0x0004)
#define U300_SYSCON_CFRR_AHB_CLK_FORCE_EN			(0x0002)
#define U300_SYSCON_CFRR_AAIF_CLK_FORCE_EN			(0x0001)
/* PLL208 Frequency Control 16bit (R/W) */
#define U300_SYSCON_PFCR					(0x48)
#define U300_SYSCON_PFCR_DPLL_MULT_NUM				(0x000F)
/* Power Management Control 16bit (R/W) */
#define U300_SYSCON_PMCR					(0x50)
#define U300_SYSCON_PMCR_DCON_ENABLE				(0x0002)
#define U300_SYSCON_PMCR_PWR_MGNT_ENABLE			(0x0001)
/* Reset Out 16bit (R/W) */
#define U300_SYSCON_RCR						(0x6c)
#define U300_SYSCON_RCR_RESOUT0_RST_N_DISABLE			(0x0001)
/* EMIF Slew Rate Control 16bit (R/W) */
#define U300_SYSCON_SRCLR					(0x70)
#define U300_SYSCON_SRCLR_MASK					(0x03FF)
#define U300_SYSCON_SRCLR_VALUE					(0x03FF)
#define U300_SYSCON_SRCLR_EMIF_1_SLRC_5_B			(0x0200)
#define U300_SYSCON_SRCLR_EMIF_1_SLRC_5_A			(0x0100)
#define U300_SYSCON_SRCLR_EMIF_1_SLRC_4_B			(0x0080)
#define U300_SYSCON_SRCLR_EMIF_1_SLRC_4_A			(0x0040)
#define U300_SYSCON_SRCLR_EMIF_1_SLRC_3_B			(0x0020)
#define U300_SYSCON_SRCLR_EMIF_1_SLRC_3_A			(0x0010)
#define U300_SYSCON_SRCLR_EMIF_1_SLRC_2_B			(0x0008)
#define U300_SYSCON_SRCLR_EMIF_1_SLRC_2_A			(0x0004)
#define U300_SYSCON_SRCLR_EMIF_1_SLRC_1_B			(0x0002)
#define U300_SYSCON_SRCLR_EMIF_1_SLRC_1_A			(0x0001)
/* EMIF Clock Control Register 16bit (R/W) */
#define U300_SYSCON_ECCR					(0x0078)
#define U300_SYSCON_ECCR_MASK					(0x000F)
#define U300_SYSCON_ECCR_EMIF_1_STATIC_CLK_EN_N_DISABLE		(0x0008)
#define U300_SYSCON_ECCR_EMIF_1_RET_OUT_CLK_EN_N_DISABLE	(0x0004)
#define U300_SYSCON_ECCR_EMIF_MEMCLK_RET_EN_N_DISABLE		(0x0002)
#define U300_SYSCON_ECCR_EMIF_SDRCLK_RET_EN_N_DISABLE		(0x0001)
/* MMC/MSPRO frequency divider register 0 16bit (R/W) */
#define U300_SYSCON_MMF0R					(0x90)
#define U300_SYSCON_MMF0R_MASK					(0x00FF)
#define U300_SYSCON_MMF0R_FREQ_0_HIGH_MASK			(0x00F0)
#define U300_SYSCON_MMF0R_FREQ_0_LOW_MASK			(0x000F)
/* MMC/MSPRO frequency divider register 1 16bit (R/W) */
#define U300_SYSCON_MMF1R					(0x94)
#define U300_SYSCON_MMF1R_MASK					(0x00FF)
#define U300_SYSCON_MMF1R_FREQ_1_HIGH_MASK			(0x00F0)
#define U300_SYSCON_MMF1R_FREQ_1_LOW_MASK			(0x000F)
/* Clock control for the MMC and MSPRO blocks 16bit (R/W) */
#define U300_SYSCON_MMCR					(0x9C)
#define U300_SYSCON_MMCR_MASK					(0x0003)
#define U300_SYSCON_MMCR_MMC_FB_CLK_SEL_ENABLE			(0x0002)
#define U300_SYSCON_MMCR_MSPRO_FREQSEL_ENABLE			(0x0001)
/* SYS_0_CLK_CONTROL first clock control 16bit (R/W) */
#define U300_SYSCON_S0CCR					(0x120)
#define U300_SYSCON_S0CCR_FIELD_MASK				(0x43FF)
#define U300_SYSCON_S0CCR_CLOCK_REQ				(0x4000)
#define U300_SYSCON_S0CCR_CLOCK_REQ_MONITOR			(0x2000)
#define U300_SYSCON_S0CCR_CLOCK_INV				(0x0200)
#define U300_SYSCON_S0CCR_CLOCK_FREQ_MASK			(0x01E0)
#define U300_SYSCON_S0CCR_CLOCK_SELECT_MASK			(0x001E)
#define U300_SYSCON_S0CCR_CLOCK_ENABLE				(0x0001)
#define U300_SYSCON_S0CCR_SEL_MCLK				(0x8<<1)
#define U300_SYSCON_S0CCR_SEL_ACC_FSM_CLK			(0xA<<1)
#define U300_SYSCON_S0CCR_SEL_PLL60_48_CLK			(0xC<<1)
#define U300_SYSCON_S0CCR_SEL_PLL60_60_CLK			(0xD<<1)
#define U300_SYSCON_S0CCR_SEL_ACC_PLL208_CLK			(0xE<<1)
#define U300_SYSCON_S0CCR_SEL_APP_PLL13_CLK			(0x0<<1)
#define U300_SYSCON_S0CCR_SEL_APP_FSM_CLK			(0x2<<1)
#define U300_SYSCON_S0CCR_SEL_RTC_CLK				(0x4<<1)
#define U300_SYSCON_S0CCR_SEL_APP_PLL208_CLK			(0x6<<1)
/* SYS_1_CLK_CONTROL second clock control 16 bit (R/W) */
#define U300_SYSCON_S1CCR					(0x124)
#define U300_SYSCON_S1CCR_FIELD_MASK				(0x43FF)
#define U300_SYSCON_S1CCR_CLOCK_REQ				(0x4000)
#define U300_SYSCON_S1CCR_CLOCK_REQ_MONITOR			(0x2000)
#define U300_SYSCON_S1CCR_CLOCK_INV				(0x0200)
#define U300_SYSCON_S1CCR_CLOCK_FREQ_MASK			(0x01E0)
#define U300_SYSCON_S1CCR_CLOCK_SELECT_MASK			(0x001E)
#define U300_SYSCON_S1CCR_CLOCK_ENABLE				(0x0001)
#define U300_SYSCON_S1CCR_SEL_MCLK				(0x8<<1)
#define U300_SYSCON_S1CCR_SEL_ACC_FSM_CLK			(0xA<<1)
#define U300_SYSCON_S1CCR_SEL_PLL60_48_CLK			(0xC<<1)
#define U300_SYSCON_S1CCR_SEL_PLL60_60_CLK			(0xD<<1)
#define U300_SYSCON_S1CCR_SEL_ACC_PLL208_CLK			(0xE<<1)
#define U300_SYSCON_S1CCR_SEL_ACC_PLL13_CLK			(0x0<<1)
#define U300_SYSCON_S1CCR_SEL_APP_FSM_CLK			(0x2<<1)
#define U300_SYSCON_S1CCR_SEL_RTC_CLK				(0x4<<1)
#define U300_SYSCON_S1CCR_SEL_APP_PLL208_CLK			(0x6<<1)
/* SYS_2_CLK_CONTROL third clock contol 16 bit (R/W) */
#define U300_SYSCON_S2CCR					(0x128)
#define U300_SYSCON_S2CCR_FIELD_MASK				(0xC3FF)
#define U300_SYSCON_S2CCR_CLK_STEAL				(0x8000)
#define U300_SYSCON_S2CCR_CLOCK_REQ				(0x4000)
#define U300_SYSCON_S2CCR_CLOCK_REQ_MONITOR			(0x2000)
#define U300_SYSCON_S2CCR_CLOCK_INV				(0x0200)
#define U300_SYSCON_S2CCR_CLOCK_FREQ_MASK			(0x01E0)
#define U300_SYSCON_S2CCR_CLOCK_SELECT_MASK			(0x001E)
#define U300_SYSCON_S2CCR_CLOCK_ENABLE				(0x0001)
#define U300_SYSCON_S2CCR_SEL_MCLK				(0x8<<1)
#define U300_SYSCON_S2CCR_SEL_ACC_FSM_CLK			(0xA<<1)
#define U300_SYSCON_S2CCR_SEL_PLL60_48_CLK			(0xC<<1)
#define U300_SYSCON_S2CCR_SEL_PLL60_60_CLK			(0xD<<1)
#define U300_SYSCON_S2CCR_SEL_ACC_PLL208_CLK			(0xE<<1)
#define U300_SYSCON_S2CCR_SEL_ACC_PLL13_CLK			(0x0<<1)
#define U300_SYSCON_S2CCR_SEL_APP_FSM_CLK			(0x2<<1)
#define U300_SYSCON_S2CCR_SEL_RTC_CLK				(0x4<<1)
#define U300_SYSCON_S2CCR_SEL_APP_PLL208_CLK			(0x6<<1)
/* SC_PLL_IRQ_CONTROL 16bit (R/W) */
#define U300_SYSCON_PICR					(0x0130)
#define U300_SYSCON_PICR_MASK					(0x00FF)
#define U300_SYSCON_PICR_FORCE_PLL208_LOCK_LOW_ENABLE		(0x0080)
#define U300_SYSCON_PICR_FORCE_PLL208_LOCK_HIGH_ENABLE		(0x0040)
#define U300_SYSCON_PICR_FORCE_PLL13_LOCK_LOW_ENABLE		(0x0020)
#define U300_SYSCON_PICR_FORCE_PLL13_LOCK_HIGH_ENABLE		(0x0010)
#define U300_SYSCON_PICR_IRQMASK_PLL13_UNLOCK_ENABLE		(0x0008)
#define U300_SYSCON_PICR_IRQMASK_PLL13_LOCK_ENABLE		(0x0004)
#define U300_SYSCON_PICR_IRQMASK_PLL208_UNLOCK_ENABLE		(0x0002)
#define U300_SYSCON_PICR_IRQMASK_PLL208_LOCK_ENABLE		(0x0001)
/* SC_PLL_IRQ_STATUS 16 bit (R/-) */
#define U300_SYSCON_PISR					(0x0134)
#define U300_SYSCON_PISR_MASK					(0x000F)
#define U300_SYSCON_PISR_PLL13_UNLOCK_IND			(0x0008)
#define U300_SYSCON_PISR_PLL13_LOCK_IND				(0x0004)
#define U300_SYSCON_PISR_PLL208_UNLOCK_IND			(0x0002)
#define U300_SYSCON_PISR_PLL208_LOCK_IND			(0x0001)
/* SC_PLL_IRQ_CLEAR 16 bit (-/W) */
#define U300_SYSCON_PICLR					(0x0138)
#define U300_SYSCON_PICLR_MASK					(0x000F)
#define U300_SYSCON_PICLR_RWMASK				(0x0000)
#define U300_SYSCON_PICLR_PLL13_UNLOCK_SC			(0x0008)
#define U300_SYSCON_PICLR_PLL13_LOCK_SC				(0x0004)
#define U300_SYSCON_PICLR_PLL208_UNLOCK_SC			(0x0002)
#define U300_SYSCON_PICLR_PLL208_LOCK_SC			(0x0001)
/* Clock activity observability register 0 */
#define U300_SYSCON_C0OAR					(0x140)
#define U300_SYSCON_C0OAR_MASK					(0xFFFF)
#define U300_SYSCON_C0OAR_VALUE					(0xFFFF)
#define U300_SYSCON_C0OAR_BT_H_CLK				(0x8000)
#define U300_SYSCON_C0OAR_ASPB_P_CLK				(0x4000)
#define U300_SYSCON_C0OAR_APP_SEMI_H_CLK			(0x2000)
#define U300_SYSCON_C0OAR_APP_SEMI_CLK				(0x1000)
#define U300_SYSCON_C0OAR_APP_MMC_MSPRO_CLK			(0x0800)
#define U300_SYSCON_C0OAR_APP_I2S1_CLK				(0x0400)
#define U300_SYSCON_C0OAR_APP_I2S0_CLK				(0x0200)
#define U300_SYSCON_C0OAR_APP_CPU_CLK				(0x0100)
#define U300_SYSCON_C0OAR_APP_52_CLK				(0x0080)
#define U300_SYSCON_C0OAR_APP_208_CLK				(0x0040)
#define U300_SYSCON_C0OAR_APP_104_CLK				(0x0020)
#define U300_SYSCON_C0OAR_APEX_CLK				(0x0010)
#define U300_SYSCON_C0OAR_AHPB_M_H_CLK				(0x0008)
#define U300_SYSCON_C0OAR_AHB_CLK				(0x0004)
#define U300_SYSCON_C0OAR_AFPB_P_CLK				(0x0002)
#define U300_SYSCON_C0OAR_AAIF_CLK				(0x0001)
/* Clock activity observability register 1 */
#define U300_SYSCON_C1OAR					(0x144)
#define U300_SYSCON_C1OAR_MASK					(0x3FFE)
#define U300_SYSCON_C1OAR_VALUE					(0x3FFE)
#define U300_SYSCON_C1OAR_NFIF_F_CLK				(0x2000)
#define U300_SYSCON_C1OAR_MSPRO_CLK				(0x1000)
#define U300_SYSCON_C1OAR_MMC_P_CLK				(0x0800)
#define U300_SYSCON_C1OAR_MMC_CLK				(0x0400)
#define U300_SYSCON_C1OAR_KP_P_CLK				(0x0200)
#define U300_SYSCON_C1OAR_I2C1_P_CLK				(0x0100)
#define U300_SYSCON_C1OAR_I2C0_P_CLK				(0x0080)
#define U300_SYSCON_C1OAR_GPIO_CLK				(0x0040)
#define U300_SYSCON_C1OAR_EMIF_MPMC_CLK				(0x0020)
#define U300_SYSCON_C1OAR_EMIF_H_CLK				(0x0010)
#define U300_SYSCON_C1OAR_EVHIST_CLK				(0x0008)
#define U300_SYSCON_C1OAR_PPM_CLK				(0x0004)
#define U300_SYSCON_C1OAR_DMA_CLK				(0x0002)
/* Clock activity observability register 2 */
#define U300_SYSCON_C2OAR					(0x148)
#define U300_SYSCON_C2OAR_MASK					(0x0FFF)
#define U300_SYSCON_C2OAR_VALUE					(0x0FFF)
#define U300_SYSCON_C2OAR_XGAM_CDI_CLK				(0x0800)
#define U300_SYSCON_C2OAR_XGAM_CLK				(0x0400)
#define U300_SYSCON_C2OAR_VC_H_CLK				(0x0200)
#define U300_SYSCON_C2OAR_VC_CLK				(0x0100)
#define U300_SYSCON_C2OAR_UA_P_CLK				(0x0080)
#define U300_SYSCON_C2OAR_TMR1_CLK				(0x0040)
#define U300_SYSCON_C2OAR_TMR0_CLK				(0x0020)
#define U300_SYSCON_C2OAR_SPI_P_CLK				(0x0010)
#define U300_SYSCON_C2OAR_PCM_I2S1_CORE_CLK			(0x0008)
#define U300_SYSCON_C2OAR_PCM_I2S1_CLK				(0x0004)
#define U300_SYSCON_C2OAR_PCM_I2S0_CORE_CLK			(0x0002)
#define U300_SYSCON_C2OAR_PCM_I2S0_CLK				(0x0001)


/*
 * The clocking hierarchy currently looks like this.
 * NOTE: the idea is NOT to show how the clocks are routed on the chip!
 * The ideas is to show dependencies, so a clock higher up in the
 * hierarchy has to be on in order for another clock to be on. Now,
 * both CPU and DMA can actually be on top of the hierarchy, and that
 * is not modeled currently. Instead we have the backbone AMBA bus on
 * top. This bus cannot be programmed in any way but conceptually it
 * needs to be active for the bridges and devices to transport data.
 *
 * Please be aware that a few clocks are hw controlled, which mean that
 * the hw itself can turn on/off or change the rate of the clock when
 * needed!
 *
 *  AMBA bus
 *  |
 *  +- CPU
 *  +- FSMC NANDIF NAND Flash interface
 *  +- SEMI Shared Memory interface
 *  +- ISP Image Signal Processor (U335 only)
 *  +- CDS (U335 only)
 *  +- DMA Direct Memory Access Controller
 *  +- AAIF APP/ACC Inteface (Mobile Scalable Link, MSL)
 *  +- APEX
 *  +- VIDEO_ENC AVE2/3 Video Encoder
 *  +- XGAM Graphics Accelerator Controller
 *  +- AHB
 *  |
 *  +- ahb:0 AHB Bridge
 *  |  |
 *  |  +- ahb:1 INTCON Interrupt controller
 *  |  +- ahb:3 MSPRO  Memory Stick Pro controller
 *  |  +- ahb:4 EMIF   External Memory interface
 *  |
 *  +- fast:0 FAST bridge
 *  |  |
 *  |  +- fast:1 MMCSD MMC/SD card reader controller
 *  |  +- fast:2 I2S0  PCM I2S channel 0 controller
 *  |  +- fast:3 I2S1  PCM I2S channel 1 controller
 *  |  +- fast:4 I2C0  I2C channel 0 controller
 *  |  +- fast:5 I2C1  I2C channel 1 controller
 *  |  +- fast:6 SPI   SPI controller
 *  |  +- fast:7 UART1 Secondary UART (U335 only)
 *  |
 *  +- slow:0 SLOW bridge
 *     |
 *     +- slow:1 SYSCON (not possible to control)
 *     +- slow:2 WDOG Watchdog
 *     +- slow:3 UART0 primary UART
 *     +- slow:4 TIMER_APP Application timer - used in Linux
 *     +- slow:5 KEYPAD controller
 *     +- slow:6 GPIO controller
 *     +- slow:7 RTC controller
 *     +- slow:8 BT Bus Tracer (not used currently)
 *     +- slow:9 EH Event Handler (not used currently)
 *     +- slow:a TIMER_ACC Access style timer (not used currently)
 *     +- slow:b PPM (U335 only, what is that?)
 */

/* Global syscon virtual base */
static void __iomem *syscon_vbase;

/**
 * struct clk_syscon - U300 syscon clock
 * @hw: corresponding clock hardware entry
 * @hw_ctrld: whether this clock is hardware controlled (for refcount etc)
 *	and does not need any magic pokes to be enabled/disabled
 * @reset: state holder, whether this block's reset line is asserted or not
 * @res_reg: reset line enable/disable flag register
 * @res_bit: bit for resetting or taking this consumer out of reset
 * @en_reg: clock line enable/disable flag register
 * @en_bit: bit for enabling/disabling this consumer clock line
 * @clk_val: magic value to poke in the register to enable/disable
 *	this one clock
 */
struct clk_syscon {
	struct clk_hw hw;
	bool hw_ctrld;
	bool reset;
	void __iomem *res_reg;
	u8 res_bit;
	void __iomem *en_reg;
	u8 en_bit;
	u16 clk_val;
};

#define to_syscon(_hw) container_of(_hw, struct clk_syscon, hw)

static DEFINE_SPINLOCK(syscon_resetreg_lock);

/*
 * Reset control functions. We remember if a block has been
 * taken out of reset and don't remove the reset assertion again
 * and vice versa. Currently we only remove resets so the
 * enablement function is defined out.
 */
static void syscon_block_reset_enable(struct clk_syscon *sclk)
{
	unsigned long iflags;
	u16 val;

	/* Not all blocks support resetting */
	if (!sclk->res_reg)
		return;
	spin_lock_irqsave(&syscon_resetreg_lock, iflags);
	val = readw(sclk->res_reg);
	val |= BIT(sclk->res_bit);
	writew(val, sclk->res_reg);
	spin_unlock_irqrestore(&syscon_resetreg_lock, iflags);
	sclk->reset = true;
}

static void syscon_block_reset_disable(struct clk_syscon *sclk)
{
	unsigned long iflags;
	u16 val;

	/* Not all blocks support resetting */
	if (!sclk->res_reg)
		return;
	spin_lock_irqsave(&syscon_resetreg_lock, iflags);
	val = readw(sclk->res_reg);
	val &= ~BIT(sclk->res_bit);
	writew(val, sclk->res_reg);
	spin_unlock_irqrestore(&syscon_resetreg_lock, iflags);
	sclk->reset = false;
}

static int syscon_clk_prepare(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);

	/* If the block is in reset, bring it out */
	if (sclk->reset)
		syscon_block_reset_disable(sclk);
	return 0;
}

static void syscon_clk_unprepare(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);

	/* Please don't force the console into reset */
	if (sclk->clk_val == U300_SYSCON_SBCER_UART_CLK_EN)
		return;
	/* When unpreparing, force block into reset */
	if (!sclk->reset)
		syscon_block_reset_enable(sclk);
}

static int syscon_clk_enable(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);

	/* Don't touch the hardware controlled clocks */
	if (sclk->hw_ctrld)
		return 0;
	/* These cannot be controlled */
	if (sclk->clk_val == 0xFFFFU)
		return 0;

	writew(sclk->clk_val, syscon_vbase + U300_SYSCON_SBCER);
	return 0;
}

static void syscon_clk_disable(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);

	/* Don't touch the hardware controlled clocks */
	if (sclk->hw_ctrld)
		return;
	if (sclk->clk_val == 0xFFFFU)
		return;
	/* Please don't disable the console port */
	if (sclk->clk_val == U300_SYSCON_SBCER_UART_CLK_EN)
		return;

	writew(sclk->clk_val, syscon_vbase + U300_SYSCON_SBCDR);
}

static int syscon_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_syscon *sclk = to_syscon(hw);
	u16 val;

	/* If no enable register defined, it's always-on */
	if (!sclk->en_reg)
		return 1;

	val = readw(sclk->en_reg);
	val &= BIT(sclk->en_bit);

	return val ? 1 : 0;
}

static u16 syscon_get_perf(void)
{
	u16 val;

	val = readw(syscon_vbase + U300_SYSCON_CCR);
	val &= U300_SYSCON_CCR_CLKING_PERFORMANCE_MASK;
	return val;
}

static unsigned long
syscon_clk_recalc_rate(struct clk_hw *hw,
		       unsigned long parent_rate)
{
	struct clk_syscon *sclk = to_syscon(hw);
	u16 perf = syscon_get_perf();

	switch(sclk->clk_val) {
	case U300_SYSCON_SBCER_FAST_BRIDGE_CLK_EN:
	case U300_SYSCON_SBCER_I2C0_CLK_EN:
	case U300_SYSCON_SBCER_I2C1_CLK_EN:
	case U300_SYSCON_SBCER_MMC_CLK_EN:
	case U300_SYSCON_SBCER_SPI_CLK_EN:
		/* The FAST clocks have one progression */
		switch(perf) {
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
			return 13000000;
		default:
			return parent_rate; /* 26 MHz */
		}
	case U300_SYSCON_SBCER_DMAC_CLK_EN:
	case U300_SYSCON_SBCER_NANDIF_CLK_EN:
	case U300_SYSCON_SBCER_XGAM_CLK_EN:
		/* AMBA interconnect peripherals */
		switch(perf) {
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
			return 6500000;
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
			return 26000000;
		default:
			return parent_rate; /* 52 MHz */
		}
	case U300_SYSCON_SBCER_SEMI_CLK_EN:
	case U300_SYSCON_SBCER_EMIF_CLK_EN:
		/* EMIF speeds */
		switch(perf) {
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
			return 13000000;
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
			return 52000000;
		default:
			return 104000000;
		}
	case U300_SYSCON_SBCER_CPU_CLK_EN:
		/* And the fast CPU clock */
		switch(perf) {
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
			return 13000000;
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
			return 52000000;
		case U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH:
			return 104000000;
		default:
			return parent_rate; /* 208 MHz */
		}
	default:
		/*
		 * The SLOW clocks and default just inherit the rate of
		 * their parent (typically PLL13 13 MHz).
		 */
		return parent_rate;
	}
}

static long
syscon_clk_round_rate(struct clk_hw *hw, unsigned long rate,
		      unsigned long *prate)
{
	struct clk_syscon *sclk = to_syscon(hw);

	if (sclk->clk_val != U300_SYSCON_SBCER_CPU_CLK_EN)
		return *prate;
	/* We really only support setting the rate of the CPU clock */
	if (rate <= 13000000)
		return 13000000;
	if (rate <= 52000000)
		return 52000000;
	if (rate <= 104000000)
		return 104000000;
	return 208000000;
}

static int syscon_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct clk_syscon *sclk = to_syscon(hw);
	u16 val;

	/* We only support setting the rate of the CPU clock */
	if (sclk->clk_val != U300_SYSCON_SBCER_CPU_CLK_EN)
		return -EINVAL;
	switch (rate) {
	case 13000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER;
		break;
	case 52000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE;
		break;
	case 104000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH;
		break;
	case 208000000:
		val = U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST;
		break;
	default:
		return -EINVAL;
	}
	val |= readw(syscon_vbase + U300_SYSCON_CCR) &
		~U300_SYSCON_CCR_CLKING_PERFORMANCE_MASK ;
	writew(val, syscon_vbase + U300_SYSCON_CCR);
	return 0;
}

static const struct clk_ops syscon_clk_ops = {
	.prepare = syscon_clk_prepare,
	.unprepare = syscon_clk_unprepare,
	.enable = syscon_clk_enable,
	.disable = syscon_clk_disable,
	.is_enabled = syscon_clk_is_enabled,
	.recalc_rate = syscon_clk_recalc_rate,
	.round_rate = syscon_clk_round_rate,
	.set_rate = syscon_clk_set_rate,
};

static struct clk * __init
syscon_clk_register(struct device *dev, const char *name,
		    const char *parent_name, unsigned long flags,
		    bool hw_ctrld,
		    void __iomem *res_reg, u8 res_bit,
		    void __iomem *en_reg, u8 en_bit,
		    u16 clk_val)
{
	struct clk *clk;
	struct clk_syscon *sclk;
	struct clk_init_data init;

	sclk = kzalloc(sizeof(struct clk_syscon), GFP_KERNEL);
	if (!sclk) {
		pr_err("could not allocate syscon clock %s\n",
			name);
		return ERR_PTR(-ENOMEM);
	}
	init.name = name;
	init.ops = &syscon_clk_ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	sclk->hw.init = &init;
	sclk->hw_ctrld = hw_ctrld;
	/* Assume the block is in reset at registration */
	sclk->reset = true;
	sclk->res_reg = res_reg;
	sclk->res_bit = res_bit;
	sclk->en_reg = en_reg;
	sclk->en_bit = en_bit;
	sclk->clk_val = clk_val;

	clk = clk_register(dev, &sclk->hw);
	if (IS_ERR(clk))
		kfree(sclk);

	return clk;
}

#define U300_CLK_TYPE_SLOW 0
#define U300_CLK_TYPE_FAST 1
#define U300_CLK_TYPE_REST 2

/**
 * struct u300_clock - defines the bits and pieces for a certain clock
 * @type: the clock type, slow fast or rest
 * @id: the bit in the slow/fast/rest register for this clock
 * @hw_ctrld: whether the clock is hardware controlled
 * @clk_val: a value to poke in the one-write enable/disable registers
 */
struct u300_clock {
	u8 type;
	u8 id;
	bool hw_ctrld;
	u16 clk_val;
};

struct u300_clock const __initconst u300_clk_lookup[] = {
	{
		.type = U300_CLK_TYPE_REST,
		.id = 3,
		.hw_ctrld = true,
		.clk_val = U300_SYSCON_SBCER_CPU_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 4,
		.hw_ctrld = true,
		.clk_val = U300_SYSCON_SBCER_DMAC_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 5,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_EMIF_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 6,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_NANDIF_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 8,
		.hw_ctrld = true,
		.clk_val = U300_SYSCON_SBCER_XGAM_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 9,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_SEMI_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 10,
		.hw_ctrld = true,
		.clk_val = U300_SYSCON_SBCER_AHB_SUBSYS_BRIDGE_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_REST,
		.id = 12,
		.hw_ctrld = false,
		/* INTCON: cannot be enabled, just taken out of reset */
		.clk_val = 0xFFFFU,
	},
	{
		.type = U300_CLK_TYPE_FAST,
		.id = 0,
		.hw_ctrld = true,
		.clk_val = U300_SYSCON_SBCER_FAST_BRIDGE_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_FAST,
		.id = 1,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_I2C0_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_FAST,
		.id = 2,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_I2C1_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_FAST,
		.id = 5,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_MMC_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_FAST,
		.id = 6,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_SPI_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_SLOW,
		.id = 0,
		.hw_ctrld = true,
		.clk_val = U300_SYSCON_SBCER_SLOW_BRIDGE_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_SLOW,
		.id = 1,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_UART_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_SLOW,
		.id = 4,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_GPIO_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_SLOW,
		.id = 6,
		.hw_ctrld = true,
		/* No clock enable register bit */
		.clk_val = 0xFFFFU,
	},
	{
		.type = U300_CLK_TYPE_SLOW,
		.id = 7,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_APP_TMR_CLK_EN,
	},
	{
		.type = U300_CLK_TYPE_SLOW,
		.id = 8,
		.hw_ctrld = false,
		.clk_val = U300_SYSCON_SBCER_ACC_TMR_CLK_EN,
	},
};

static void __init of_u300_syscon_clk_init(struct device_node *np)
{
	struct clk *clk = ERR_PTR(-EINVAL);
	const char *clk_name = np->name;
	const char *parent_name;
	void __iomem *res_reg;
	void __iomem *en_reg;
	u32 clk_type;
	u32 clk_id;
	int i;

	if (of_property_read_u32(np, "clock-type", &clk_type)) {
		pr_err("%s: syscon clock \"%s\" missing clock-type property\n",
		       __func__, clk_name);
		return;
	}
	if (of_property_read_u32(np, "clock-id", &clk_id)) {
		pr_err("%s: syscon clock \"%s\" missing clock-id property\n",
		       __func__, clk_name);
		return;
	}
	parent_name = of_clk_get_parent_name(np, 0);

	switch (clk_type) {
	case U300_CLK_TYPE_SLOW:
		res_reg = syscon_vbase + U300_SYSCON_RSR;
		en_reg = syscon_vbase + U300_SYSCON_CESR;
		break;
	case U300_CLK_TYPE_FAST:
		res_reg = syscon_vbase + U300_SYSCON_RFR;
		en_reg = syscon_vbase + U300_SYSCON_CEFR;
		break;
	case U300_CLK_TYPE_REST:
		res_reg = syscon_vbase + U300_SYSCON_RRR;
		en_reg = syscon_vbase + U300_SYSCON_CERR;
		break;
	default:
		pr_err("unknown clock type %x specified\n", clk_type);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(u300_clk_lookup); i++) {
		const struct u300_clock *u3clk = &u300_clk_lookup[i];

		if (u3clk->type == clk_type && u3clk->id == clk_id)
			clk = syscon_clk_register(NULL,
						  clk_name, parent_name,
						  0, u3clk->hw_ctrld,
						  res_reg, u3clk->id,
						  en_reg, u3clk->id,
						  u3clk->clk_val);
	}

	if (!IS_ERR(clk)) {
		of_clk_add_provider(np, of_clk_src_simple_get, clk);

		/*
		 * Some few system clocks - device tree does not
		 * represent clocks without a corresponding device node.
		 * for now we add these three clocks here.
		 */
		if (clk_type == U300_CLK_TYPE_REST && clk_id == 5)
			clk_register_clkdev(clk, NULL, "pl172");
		if (clk_type == U300_CLK_TYPE_REST && clk_id == 9)
			clk_register_clkdev(clk, NULL, "semi");
		if (clk_type == U300_CLK_TYPE_REST && clk_id == 12)
			clk_register_clkdev(clk, NULL, "intcon");
	}
}

/**
 * struct clk_mclk - U300 MCLK clock (MMC/SD clock)
 * @hw: corresponding clock hardware entry
 * @is_mspro: if this is the memory stick clock rather than MMC/SD
 */
struct clk_mclk {
	struct clk_hw hw;
	bool is_mspro;
};

#define to_mclk(_hw) container_of(_hw, struct clk_mclk, hw)

static int mclk_clk_prepare(struct clk_hw *hw)
{
	struct clk_mclk *mclk = to_mclk(hw);
	u16 val;

	/* The MMC and MSPRO clocks need some special set-up */
	if (!mclk->is_mspro) {
		/* Set default MMC clock divisor to 18.9 MHz */
		writew(0x0054U, syscon_vbase + U300_SYSCON_MMF0R);
		val = readw(syscon_vbase + U300_SYSCON_MMCR);
		/* Disable the MMC feedback clock */
		val &= ~U300_SYSCON_MMCR_MMC_FB_CLK_SEL_ENABLE;
		/* Disable MSPRO frequency */
		val &= ~U300_SYSCON_MMCR_MSPRO_FREQSEL_ENABLE;
		writew(val, syscon_vbase + U300_SYSCON_MMCR);
	} else {
		val = readw(syscon_vbase + U300_SYSCON_MMCR);
		/* Disable the MMC feedback clock */
		val &= ~U300_SYSCON_MMCR_MMC_FB_CLK_SEL_ENABLE;
		/* Enable MSPRO frequency */
		val |= U300_SYSCON_MMCR_MSPRO_FREQSEL_ENABLE;
		writew(val, syscon_vbase + U300_SYSCON_MMCR);
	}

	return 0;
}

static unsigned long
mclk_clk_recalc_rate(struct clk_hw *hw,
		     unsigned long parent_rate)
{
	u16 perf = syscon_get_perf();

	switch (perf) {
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW_POWER:
		/*
		 * Here, the 208 MHz PLL gets shut down and the always
		 * on 13 MHz PLL used for RTC etc kicks into use
		 * instead.
		 */
		return 13000000;
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_LOW:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_INTERMEDIATE:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_HIGH:
	case U300_SYSCON_CCR_CLKING_PERFORMANCE_BEST:
	{
		/*
		 * This clock is under program control. The register is
		 * divided in two nybbles, bit 7-4 gives cycles-1 to count
		 * high, bit 3-0 gives cycles-1 to count low. Distribute
		 * these with no more than 1 cycle difference between
		 * low and high and add low and high to get the actual
		 * divisor. The base PLL is 208 MHz. Writing 0x00 will
		 * divide by 1 and 1 so the highest frequency possible
		 * is 104 MHz.
		 *
		 * e.g. 0x54 =>
		 * f = 208 / ((5+1) + (4+1)) = 208 / 11 = 18.9 MHz
		 */
		u16 val = readw(syscon_vbase + U300_SYSCON_MMF0R) &
			U300_SYSCON_MMF0R_MASK;
		switch (val) {
		case 0x0054:
			return 18900000;
		case 0x0044:
			return 20800000;
		case 0x0043:
			return 23100000;
		case 0x0033:
			return 26000000;
		case 0x0032:
			return 29700000;
		case 0x0022:
			return 34700000;
		case 0x0021:
			return 41600000;
		case 0x0011:
			return 52000000;
		case 0x0000:
			return 104000000;
		default:
			break;
		}
	}
	default:
		break;
	}
	return parent_rate;
}

static long
mclk_clk_round_rate(struct clk_hw *hw, unsigned long rate,
		    unsigned long *prate)
{
	if (rate <= 18900000)
		return 18900000;
	if (rate <= 20800000)
		return 20800000;
	if (rate <= 23100000)
		return 23100000;
	if (rate <= 26000000)
		return 26000000;
	if (rate <= 29700000)
		return 29700000;
	if (rate <= 34700000)
		return 34700000;
	if (rate <= 41600000)
		return 41600000;
	/* Highest rate */
	return 52000000;
}

static int mclk_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	u16 val;
	u16 reg;

	switch (rate) {
	case 18900000:
		val = 0x0054;
		break;
	case 20800000:
		val = 0x0044;
		break;
	case 23100000:
		val = 0x0043;
		break;
	case 26000000:
		val = 0x0033;
		break;
	case 29700000:
		val = 0x0032;
		break;
	case 34700000:
		val = 0x0022;
		break;
	case 41600000:
		val = 0x0021;
		break;
	case 52000000:
		val = 0x0011;
		break;
	case 104000000:
		val = 0x0000;
		break;
	default:
		return -EINVAL;
	}

	reg = readw(syscon_vbase + U300_SYSCON_MMF0R) &
		~U300_SYSCON_MMF0R_MASK;
	writew(reg | val, syscon_vbase + U300_SYSCON_MMF0R);
	return 0;
}

static const struct clk_ops mclk_ops = {
	.prepare = mclk_clk_prepare,
	.recalc_rate = mclk_clk_recalc_rate,
	.round_rate = mclk_clk_round_rate,
	.set_rate = mclk_clk_set_rate,
};

static struct clk * __init
mclk_clk_register(struct device *dev, const char *name,
		  const char *parent_name, bool is_mspro)
{
	struct clk *clk;
	struct clk_mclk *mclk;
	struct clk_init_data init;

	mclk = kzalloc(sizeof(struct clk_mclk), GFP_KERNEL);
	if (!mclk) {
		pr_err("could not allocate MMC/SD clock %s\n",
		       name);
		return ERR_PTR(-ENOMEM);
	}
	init.name = "mclk";
	init.ops = &mclk_ops;
	init.flags = 0;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	mclk->hw.init = &init;
	mclk->is_mspro = is_mspro;

	clk = clk_register(dev, &mclk->hw);
	if (IS_ERR(clk))
		kfree(mclk);

	return clk;
}

static void __init of_u300_syscon_mclk_init(struct device_node *np)
{
	struct clk *clk = ERR_PTR(-EINVAL);
	const char *clk_name = np->name;
	const char *parent_name;

	parent_name = of_clk_get_parent_name(np, 0);
	clk = mclk_clk_register(NULL, clk_name, parent_name, false);
	if (!IS_ERR(clk))
		of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static const __initconst struct of_device_id u300_clk_match[] = {
	{
		.compatible = "fixed-clock",
		.data = of_fixed_clk_setup,
	},
	{
		.compatible = "fixed-factor-clock",
		.data = of_fixed_factor_clk_setup,
	},
	{
		.compatible = "stericsson,u300-syscon-clk",
		.data = of_u300_syscon_clk_init,
	},
	{
		.compatible = "stericsson,u300-syscon-mclk",
		.data = of_u300_syscon_mclk_init,
	},
};


void __init u300_clk_init(void __iomem *base)
{
	u16 val;

	syscon_vbase = base;

	/* Set system to run at PLL208, max performance, a known state. */
	val = readw(syscon_vbase + U300_SYSCON_CCR);
	val &= ~U300_SYSCON_CCR_CLKING_PERFORMANCE_MASK;
	writew(val, syscon_vbase + U300_SYSCON_CCR);
	/* Wait for the PLL208 to lock if not locked in yet */
	while (!(readw(syscon_vbase + U300_SYSCON_CSR) &
		 U300_SYSCON_CSR_PLL208_LOCK_IND));

	/* Power management enable */
	val = readw(syscon_vbase + U300_SYSCON_PMCR);
	val |= U300_SYSCON_PMCR_PWR_MGNT_ENABLE;
	writew(val, syscon_vbase + U300_SYSCON_PMCR);

	of_clk_init(u300_clk_match);
}
