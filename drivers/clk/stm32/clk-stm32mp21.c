// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) STMicroelectronics 2023 - All Rights Reserved
 * Author: Gabriel Fernandez <gabriel.fernandez@foss.st.com> for STMicroelectronics.
 */

#include <linux/bus/stm32_firewall_device.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include "clk-stm32-core.h"
#include "reset-stm32.h"
#include "stm32mp21_rcc.h"

#include <dt-bindings/clock/st,stm32mp21-rcc.h>
#include <dt-bindings/reset/st,stm32mp21-rcc.h>

/* Max clock binding value */
#define STM32MP21_LAST_CLK	CK_SCMI_KER_ETR

/* Clock security definition */
#define SECF_NONE		-1

#define RCC_REG_SIZE	32
#define RCC_SECCFGR(x)	(((x) / RCC_REG_SIZE) * 0x4 + RCC_SECCFGR0)
#define RCC_CIDCFGR(x)	((x) * 0x8 + RCC_R0CIDCFGR)
#define RCC_SEMCR(x)	((x) * 0x8 + RCC_R0SEMCR)
#define RCC_CID1	1

/* Register: RIFSC_CIDCFGR */
#define RCC_CIDCFGR_CFEN	BIT(0)
#define RCC_CIDCFGR_SEM_EN	BIT(1)
#define RCC_CIDCFGR_SEMWLC1_EN	BIT(17)
#define RCC_CIDCFGR_SCID_MASK	GENMASK(6, 4)

/* Register: RIFSC_SEMCR */
#define RCC_SEMCR_SEMCID_MASK	GENMASK(6, 4)

#define MP21_RIF_RCC_MCO1		108
#define MP21_RIF_RCC_MCO2		109

#define SEC_RIFSC_FLAG		BIT(31)
#define SEC_RIFSC(_id)		((_id) | SEC_RIFSC_FLAG)

enum {
	HSE,
	HSI,
	MSI,
	LSE,
	LSI,
	HSE_DIV2,
	ICN_HS_MCU,
	ICN_LS_MCU,
	ICN_SDMMC,
	ICN_DDR,
	ICN_DISPLAY,
	ICN_HSL,
	ICN_NIC,
	FLEXGEN_07,
	FLEXGEN_08,
	FLEXGEN_09,
	FLEXGEN_10,
	FLEXGEN_11,
	FLEXGEN_12,
	FLEXGEN_13,
	FLEXGEN_14,
	FLEXGEN_16,
	FLEXGEN_17,
	FLEXGEN_18,
	FLEXGEN_19,
	FLEXGEN_20,
	FLEXGEN_21,
	FLEXGEN_22,
	FLEXGEN_23,
	FLEXGEN_24,
	FLEXGEN_25,
	FLEXGEN_26,
	FLEXGEN_27,
	FLEXGEN_29,
	FLEXGEN_30,
	FLEXGEN_31,
	FLEXGEN_33,
	FLEXGEN_36,
	FLEXGEN_37,
	FLEXGEN_38,
	FLEXGEN_39,
	FLEXGEN_40,
	FLEXGEN_41,
	FLEXGEN_42,
	FLEXGEN_43,
	FLEXGEN_44,
	FLEXGEN_45,
	FLEXGEN_46,
	FLEXGEN_47,
	FLEXGEN_48,
	FLEXGEN_50,
	FLEXGEN_51,
	FLEXGEN_52,
	FLEXGEN_53,
	FLEXGEN_54,
	FLEXGEN_55,
	FLEXGEN_56,
	FLEXGEN_57,
	FLEXGEN_58,
	FLEXGEN_61,
	FLEXGEN_62,
	FLEXGEN_63,
	ICN_APB1,
	ICN_APB2,
	ICN_APB3,
	ICN_APB4,
	ICN_APB5,
	ICN_APBDBG,
	TIMG1,
	TIMG2,
};

static const struct clk_parent_data adc1_src[] = {
	{ .index = FLEXGEN_46 },
	{ .index = ICN_LS_MCU },
};

static const struct clk_parent_data adc2_src[] = {
	{ .index = FLEXGEN_47 },
	{ .index = ICN_LS_MCU },
	{ .index = FLEXGEN_46 },
};

static const struct clk_parent_data usb2phy1_src[] = {
	{ .index = FLEXGEN_57 },
	{ .index = HSE_DIV2 },
};

static const struct clk_parent_data usb2phy2_src[] = {
	{ .index = FLEXGEN_58 },
	{ .index = HSE_DIV2 },
};

static const struct clk_parent_data dts_src[] = {
	{ .index = HSI },
	{ .index = HSE },
	{ .index = MSI },
};

static const struct clk_parent_data mco1_src[] = {
	{ .index = FLEXGEN_61 },
};

static const struct clk_parent_data mco2_src[] = {
	{ .index = FLEXGEN_62 },
};

enum enum_mux_cfg {
	MUX_ADC1,
	MUX_ADC2,
	MUX_DTS,
	MUX_MCO1,
	MUX_MCO2,
	MUX_USB2PHY1,
	MUX_USB2PHY2,
	MUX_NB
};

#define MUX_CFG(id, _offset, _shift, _width)	\
	[id] = {				\
		.offset		= (_offset),	\
		.shift		= (_shift),	\
		.width		= (_width),	\
	}

static const struct stm32_mux_cfg stm32mp21_muxes[MUX_NB] = {
	MUX_CFG(MUX_ADC1,		RCC_ADC1CFGR,		12,	1),
	MUX_CFG(MUX_ADC2,		RCC_ADC2CFGR,		12,	2),
	MUX_CFG(MUX_DTS,		RCC_DTSCFGR,		12,	2),
	MUX_CFG(MUX_MCO1,		RCC_MCO1CFGR,		0,	1),
	MUX_CFG(MUX_MCO2,		RCC_MCO2CFGR,		0,	1),
	MUX_CFG(MUX_USB2PHY1,		RCC_USB2PHY1CFGR,	15,	1),
	MUX_CFG(MUX_USB2PHY2,		RCC_USB2PHY2CFGR,	15,	1),
};

enum enum_gate_cfg {
	GATE_ADC1,
	GATE_ADC2,
	GATE_CRC,
	GATE_CRYP1,
	GATE_CRYP2,
	GATE_CSI,
	GATE_DCMIPP,
	GATE_DCMIPSSI,
	GATE_DDRPERFM,
	GATE_DTS,
	GATE_ETH1,
	GATE_ETH1MAC,
	GATE_ETH1RX,
	GATE_ETH1STP,
	GATE_ETH1TX,
	GATE_ETH2,
	GATE_ETH2MAC,
	GATE_ETH2RX,
	GATE_ETH2STP,
	GATE_ETH2TX,
	GATE_FDCAN,
	GATE_HASH1,
	GATE_HASH2,
	GATE_HDP,
	GATE_I2C1,
	GATE_I2C2,
	GATE_I2C3,
	GATE_I3C1,
	GATE_I3C2,
	GATE_I3C3,
	GATE_IWDG1,
	GATE_IWDG2,
	GATE_IWDG3,
	GATE_IWDG4,
	GATE_LPTIM1,
	GATE_LPTIM2,
	GATE_LPTIM3,
	GATE_LPTIM4,
	GATE_LPTIM5,
	GATE_LPUART1,
	GATE_LTDC,
	GATE_MCO1,
	GATE_MCO2,
	GATE_MDF1,
	GATE_OTG,
	GATE_PKA,
	GATE_RNG1,
	GATE_RNG2,
	GATE_SAES,
	GATE_SAI1,
	GATE_SAI2,
	GATE_SAI3,
	GATE_SAI4,
	GATE_SDMMC1,
	GATE_SDMMC2,
	GATE_SDMMC3,
	GATE_SERC,
	GATE_SPDIFRX,
	GATE_SPI1,
	GATE_SPI2,
	GATE_SPI3,
	GATE_SPI4,
	GATE_SPI5,
	GATE_SPI6,
	GATE_TIM1,
	GATE_TIM10,
	GATE_TIM11,
	GATE_TIM12,
	GATE_TIM13,
	GATE_TIM14,
	GATE_TIM15,
	GATE_TIM16,
	GATE_TIM17,
	GATE_TIM2,
	GATE_TIM3,
	GATE_TIM4,
	GATE_TIM5,
	GATE_TIM6,
	GATE_TIM7,
	GATE_TIM8,
	GATE_UART4,
	GATE_UART5,
	GATE_UART7,
	GATE_USART1,
	GATE_USART2,
	GATE_USART3,
	GATE_USART6,
	GATE_USB2PHY1,
	GATE_USB2PHY2,
	GATE_USBH,
	GATE_VREF,
	GATE_WWDG1,
	GATE_NB
};

#define GATE_CFG(id, _offset, _bit_idx, _offset_clr)	\
	[id] = {					\
		.offset		= (_offset),		\
		.bit_idx	= (_bit_idx),		\
		.set_clr	= (_offset_clr),	\
	}

static const struct stm32_gate_cfg stm32mp21_gates[GATE_NB] = {
	GATE_CFG(GATE_ADC1,		RCC_ADC1CFGR,		1,	0),
	GATE_CFG(GATE_ADC2,		RCC_ADC2CFGR,		1,	0),
	GATE_CFG(GATE_CRC,		RCC_CRCCFGR,		1,	0),
	GATE_CFG(GATE_CRYP1,		RCC_CRYP1CFGR,		1,	0),
	GATE_CFG(GATE_CRYP2,		RCC_CRYP2CFGR,		1,	0),
	GATE_CFG(GATE_CSI,		RCC_CSICFGR,		1,	0),
	GATE_CFG(GATE_DCMIPP,		RCC_DCMIPPCFGR,		1,	0),
	GATE_CFG(GATE_DCMIPSSI,		RCC_DCMIPSSICFGR,	1,	0),
	GATE_CFG(GATE_DDRPERFM,		RCC_DDRPERFMCFGR,	1,	0),
	GATE_CFG(GATE_DTS,		RCC_DTSCFGR,		1,	0),
	GATE_CFG(GATE_ETH1,		RCC_ETH1CFGR,		5,	0),
	GATE_CFG(GATE_ETH1MAC,		RCC_ETH1CFGR,		1,	0),
	GATE_CFG(GATE_ETH1RX,		RCC_ETH1CFGR,		10,	0),
	GATE_CFG(GATE_ETH1STP,		RCC_ETH1CFGR,		4,	0),
	GATE_CFG(GATE_ETH1TX,		RCC_ETH1CFGR,		8,	0),
	GATE_CFG(GATE_ETH2,		RCC_ETH2CFGR,		5,	0),
	GATE_CFG(GATE_ETH2MAC,		RCC_ETH2CFGR,		1,	0),
	GATE_CFG(GATE_ETH2RX,		RCC_ETH2CFGR,		10,	0),
	GATE_CFG(GATE_ETH2STP,		RCC_ETH2CFGR,		4,	0),
	GATE_CFG(GATE_ETH2TX,		RCC_ETH2CFGR,		8,	0),
	GATE_CFG(GATE_FDCAN,		RCC_FDCANCFGR,		1,	0),
	GATE_CFG(GATE_HASH1,		RCC_HASH1CFGR,		1,	0),
	GATE_CFG(GATE_HASH2,		RCC_HASH2CFGR,		1,	0),
	GATE_CFG(GATE_HDP,		RCC_HDPCFGR,		1,	0),
	GATE_CFG(GATE_I2C1,		RCC_I2C1CFGR,		1,	0),
	GATE_CFG(GATE_I2C2,		RCC_I2C2CFGR,		1,	0),
	GATE_CFG(GATE_I2C3,		RCC_I2C3CFGR,		1,	0),
	GATE_CFG(GATE_I3C1,		RCC_I3C1CFGR,		1,	0),
	GATE_CFG(GATE_I3C2,		RCC_I3C2CFGR,		1,	0),
	GATE_CFG(GATE_I3C3,		RCC_I3C3CFGR,		1,	0),
	GATE_CFG(GATE_IWDG1,		RCC_IWDG1CFGR,		1,	0),
	GATE_CFG(GATE_IWDG2,		RCC_IWDG2CFGR,		1,	0),
	GATE_CFG(GATE_IWDG3,		RCC_IWDG3CFGR,		1,	0),
	GATE_CFG(GATE_IWDG4,		RCC_IWDG4CFGR,		1,	0),
	GATE_CFG(GATE_LPTIM1,		RCC_LPTIM1CFGR,		1,	0),
	GATE_CFG(GATE_LPTIM2,		RCC_LPTIM2CFGR,		1,	0),
	GATE_CFG(GATE_LPTIM3,		RCC_LPTIM3CFGR,		1,	0),
	GATE_CFG(GATE_LPTIM4,		RCC_LPTIM4CFGR,		1,	0),
	GATE_CFG(GATE_LPTIM5,		RCC_LPTIM5CFGR,		1,	0),
	GATE_CFG(GATE_LPUART1,		RCC_LPUART1CFGR,	1,	0),
	GATE_CFG(GATE_LTDC,		RCC_LTDCCFGR,		1,	0),
	GATE_CFG(GATE_MCO1,		RCC_MCO1CFGR,		8,	0),
	GATE_CFG(GATE_MCO2,		RCC_MCO2CFGR,		8,	0),
	GATE_CFG(GATE_MDF1,		RCC_MDF1CFGR,		1,	0),
	GATE_CFG(GATE_OTG,		RCC_OTGCFGR,		1,	0),
	GATE_CFG(GATE_PKA,		RCC_PKACFGR,		1,	0),
	GATE_CFG(GATE_RNG1,		RCC_RNG1CFGR,		1,	0),
	GATE_CFG(GATE_RNG2,		RCC_RNG2CFGR,		1,	0),
	GATE_CFG(GATE_SAES,		RCC_SAESCFGR,		1,	0),
	GATE_CFG(GATE_SAI1,		RCC_SAI1CFGR,		1,	0),
	GATE_CFG(GATE_SAI2,		RCC_SAI2CFGR,		1,	0),
	GATE_CFG(GATE_SAI3,		RCC_SAI3CFGR,		1,	0),
	GATE_CFG(GATE_SAI4,		RCC_SAI4CFGR,		1,	0),
	GATE_CFG(GATE_SDMMC1,		RCC_SDMMC1CFGR,		1,	0),
	GATE_CFG(GATE_SDMMC2,		RCC_SDMMC2CFGR,		1,	0),
	GATE_CFG(GATE_SDMMC3,		RCC_SDMMC3CFGR,		1,	0),
	GATE_CFG(GATE_SERC,		RCC_SERCCFGR,		1,	0),
	GATE_CFG(GATE_SPDIFRX,		RCC_SPDIFRXCFGR,	1,	0),
	GATE_CFG(GATE_SPI1,		RCC_SPI1CFGR,		1,	0),
	GATE_CFG(GATE_SPI2,		RCC_SPI2CFGR,		1,	0),
	GATE_CFG(GATE_SPI3,		RCC_SPI3CFGR,		1,	0),
	GATE_CFG(GATE_SPI4,		RCC_SPI4CFGR,		1,	0),
	GATE_CFG(GATE_SPI5,		RCC_SPI5CFGR,		1,	0),
	GATE_CFG(GATE_SPI6,		RCC_SPI6CFGR,		1,	0),
	GATE_CFG(GATE_TIM1,		RCC_TIM1CFGR,		1,	0),
	GATE_CFG(GATE_TIM10,		RCC_TIM10CFGR,		1,	0),
	GATE_CFG(GATE_TIM11,		RCC_TIM11CFGR,		1,	0),
	GATE_CFG(GATE_TIM12,		RCC_TIM12CFGR,		1,	0),
	GATE_CFG(GATE_TIM13,		RCC_TIM13CFGR,		1,	0),
	GATE_CFG(GATE_TIM14,		RCC_TIM14CFGR,		1,	0),
	GATE_CFG(GATE_TIM15,		RCC_TIM15CFGR,		1,	0),
	GATE_CFG(GATE_TIM16,		RCC_TIM16CFGR,		1,	0),
	GATE_CFG(GATE_TIM17,		RCC_TIM17CFGR,		1,	0),
	GATE_CFG(GATE_TIM2,		RCC_TIM2CFGR,		1,	0),
	GATE_CFG(GATE_TIM3,		RCC_TIM3CFGR,		1,	0),
	GATE_CFG(GATE_TIM4,		RCC_TIM4CFGR,		1,	0),
	GATE_CFG(GATE_TIM5,		RCC_TIM5CFGR,		1,	0),
	GATE_CFG(GATE_TIM6,		RCC_TIM6CFGR,		1,	0),
	GATE_CFG(GATE_TIM7,		RCC_TIM7CFGR,		1,	0),
	GATE_CFG(GATE_TIM8,		RCC_TIM8CFGR,		1,	0),
	GATE_CFG(GATE_UART4,		RCC_UART4CFGR,		1,	0),
	GATE_CFG(GATE_UART5,		RCC_UART5CFGR,		1,	0),
	GATE_CFG(GATE_UART7,		RCC_UART7CFGR,		1,	0),
	GATE_CFG(GATE_USART1,		RCC_USART1CFGR,		1,	0),
	GATE_CFG(GATE_USART2,		RCC_USART2CFGR,		1,	0),
	GATE_CFG(GATE_USART3,		RCC_USART3CFGR,		1,	0),
	GATE_CFG(GATE_USART6,		RCC_USART6CFGR,		1,	0),
	GATE_CFG(GATE_USB2PHY1,		RCC_USB2PHY1CFGR,	1,	0),
	GATE_CFG(GATE_USB2PHY2,		RCC_USB2PHY2CFGR,	1,	0),
	GATE_CFG(GATE_USBH,		RCC_USBHCFGR,		1,	0),
	GATE_CFG(GATE_VREF,		RCC_VREFCFGR,		1,	0),
	GATE_CFG(GATE_WWDG1,		RCC_WWDG1CFGR,		1,	0),
};

#define CLK_HW_INIT_INDEX(_name, _parent, _ops, _flags)		\
	(&(struct clk_init_data) {					\
		.flags		= _flags,				\
		.name		= _name,				\
		.parent_data	= (const struct clk_parent_data[]) {	\
					{ .index = _parent },		\
				  },					\
		.num_parents	= 1,					\
		.ops		= _ops,					\
	})

/* ADC */
static struct clk_stm32_gate ck_icn_p_adc1 = {
	.gate_id = GATE_ADC1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_adc1", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_composite ck_ker_adc1 = {
	.gate_id = GATE_ADC1,
	.mux_id = MUX_ADC1,
	.div_id = NO_STM32_DIV,
	.hw.init = CLK_HW_INIT_PARENTS_DATA("ck_ker_adc1", adc1_src, &clk_stm32_composite_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_adc2 = {
	.gate_id = GATE_ADC2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_adc2", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_composite ck_ker_adc2 = {
	.gate_id = GATE_ADC2,
	.mux_id = MUX_ADC2,
	.div_id = NO_STM32_DIV,
	.hw.init = CLK_HW_INIT_PARENTS_DATA("ck_ker_adc2", adc2_src, &clk_stm32_composite_ops, 0),
};

/* CSI-HOST */
static struct clk_stm32_gate ck_icn_p_csi = {
	.gate_id = GATE_CSI,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_csi", ICN_APB4, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_csi = {
	.gate_id = GATE_CSI,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_csi", FLEXGEN_29, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_csitxesc = {
	.gate_id = GATE_CSI,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_csitxesc", FLEXGEN_30, &clk_stm32_gate_ops, 0),
};

/* CSI-PHY */
static struct clk_stm32_gate ck_ker_csiphy = {
	.gate_id = GATE_CSI,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_csiphy", FLEXGEN_31, &clk_stm32_gate_ops, 0),
};

/* DCMIPP */
static struct clk_stm32_gate ck_icn_p_dcmipp = {
	.gate_id = GATE_DCMIPP,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_dcmipp", ICN_APB4, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_dcmipssi = {
	.gate_id = GATE_DCMIPSSI,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_dcmipssi", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

/* DDRPERMF */
static struct clk_stm32_gate ck_icn_p_ddrperfm = {
	.gate_id = GATE_DDRPERFM,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_ddrperfm", ICN_APB4, &clk_stm32_gate_ops, 0),
};

/* CRC */
static struct clk_stm32_gate ck_icn_p_crc = {
	.gate_id = GATE_CRC,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_crc", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

/* CRYP */
static struct clk_stm32_gate ck_icn_p_cryp1 = {
	.gate_id = GATE_CRYP1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_cryp1", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_cryp2 = {
	.gate_id = GATE_CRYP2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_cryp2", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

/* DBG & TRACE */
/* Trace and debug clocks are managed by SCMI */

/* LTDC */
static struct clk_stm32_gate ck_icn_p_ltdc = {
	.gate_id = GATE_LTDC,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_ltdc", ICN_APB4, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_ltdc = {
	.gate_id = GATE_LTDC,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_ltdc", FLEXGEN_27, &clk_stm32_gate_ops,
				       CLK_SET_RATE_PARENT),
};

/* DTS */
static struct clk_stm32_composite ck_ker_dts = {
	.gate_id = GATE_DTS,
	.mux_id = MUX_DTS,
	.div_id = NO_STM32_DIV,
	.hw.init = CLK_HW_INIT_PARENTS_DATA("ck_ker_dts", dts_src,
					    &clk_stm32_composite_ops, 0),
};

/* ETHERNET */
static struct clk_stm32_gate ck_icn_p_eth1 = {
	.gate_id = GATE_ETH1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_eth1", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_eth1stp = {
	.gate_id = GATE_ETH1STP,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_eth1stp", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_eth1 = {
	.gate_id = GATE_ETH1,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_eth1", FLEXGEN_54, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_eth1ptp = {
	.gate_id = GATE_ETH1,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_eth1ptp", FLEXGEN_56, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_eth1mac = {
	.gate_id = GATE_ETH1MAC,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_eth1mac", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_eth1tx = {
	.gate_id = GATE_ETH1TX,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_eth1tx", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_eth1rx = {
	.gate_id = GATE_ETH1RX,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_eth1rx", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_eth2 = {
	.gate_id = GATE_ETH2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_eth2", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_eth2stp = {
	.gate_id = GATE_ETH2STP,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_eth2stp", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_eth2 = {
	.gate_id = GATE_ETH2,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_eth2", FLEXGEN_55, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_eth2ptp = {
	.gate_id = GATE_ETH2,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_eth2ptp", FLEXGEN_56, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_eth2mac = {
	.gate_id = GATE_ETH2MAC,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_eth2mac", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_eth2tx = {
	.gate_id = GATE_ETH2TX,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_eth2tx", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_eth2rx = {
	.gate_id = GATE_ETH2RX,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_eth2rx", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

/* FDCAN */
static struct clk_stm32_gate ck_icn_p_fdcan = {
	.gate_id = GATE_FDCAN,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_fdcan", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_fdcan = {
	.gate_id = GATE_FDCAN,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_fdcan", FLEXGEN_26, &clk_stm32_gate_ops, 0),
};

/* HASH */
static struct clk_stm32_gate ck_icn_p_hash1 = {
	.gate_id = GATE_HASH1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_hash1", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_hash2 = {
	.gate_id = GATE_HASH2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_hash2", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

/* HDP */
static struct clk_stm32_gate ck_icn_p_hdp = {
	.gate_id = GATE_HDP,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_hdp", ICN_APB3, &clk_stm32_gate_ops, 0),
};

/* I2C */
static struct clk_stm32_gate ck_icn_p_i2c1 = {
	.gate_id = GATE_I2C1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_i2c1", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_i2c2 = {
	.gate_id = GATE_I2C2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_i2c2", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_i2c3 = {
	.gate_id = GATE_I2C3,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_i2c3", ICN_APB5, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_i2c1 = {
	.gate_id = GATE_I2C1,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_i2c1", FLEXGEN_13, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_i2c2 = {
	.gate_id = GATE_I2C2,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_i2c2", FLEXGEN_13, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_i2c3 = {
	.gate_id = GATE_I2C3,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_i2c3", FLEXGEN_38, &clk_stm32_gate_ops, 0),
};

/* I3C */
static struct clk_stm32_gate ck_icn_p_i3c1 = {
	.gate_id = GATE_I3C1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_i3c1", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_i3c2 = {
	.gate_id = GATE_I3C2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_i3c2", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_i3c3 = {
	.gate_id = GATE_I3C3,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_i3c3", ICN_APB5, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_i3c1 = {
	.gate_id = GATE_I3C1,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_i3c1", FLEXGEN_14, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_i3c2 = {
	.gate_id = GATE_I3C2,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_i3c2", FLEXGEN_14, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_i3c3 = {
	.gate_id = GATE_I3C3,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_i3c3", FLEXGEN_36, &clk_stm32_gate_ops, 0),
};

/* IWDG */
static struct clk_stm32_gate ck_icn_p_iwdg1 = {
	.gate_id = GATE_IWDG1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_iwdg1", ICN_APB3, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_iwdg2 = {
	.gate_id = GATE_IWDG2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_iwdg2", ICN_APB3, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_iwdg3 = {
	.gate_id = GATE_IWDG3,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_iwdg3", ICN_APB3, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_iwdg4 = {
	.gate_id = GATE_IWDG4,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_iwdg4", ICN_APB3, &clk_stm32_gate_ops, 0),
};

/* LPTIM */
static struct clk_stm32_gate ck_icn_p_lptim1 = {
	.gate_id = GATE_LPTIM1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_lptim1", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_lptim2 = {
	.gate_id = GATE_LPTIM2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_lptim2", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_lptim3 = {
	.gate_id = GATE_LPTIM3,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_lptim3", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_lptim4 = {
	.gate_id = GATE_LPTIM4,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_lptim4", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_lptim5 = {
	.gate_id = GATE_LPTIM5,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_lptim5", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_lptim1 = {
	.gate_id = GATE_LPTIM1,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_lptim1", FLEXGEN_07, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_lptim2 = {
	.gate_id = GATE_LPTIM2,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_lptim2", FLEXGEN_07, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_lptim3 = {
	.gate_id = GATE_LPTIM3,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_lptim3", FLEXGEN_40, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_lptim4 = {
	.gate_id = GATE_LPTIM4,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_lptim4", FLEXGEN_41, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_lptim5 = {
	.gate_id = GATE_LPTIM5,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_lptim5", FLEXGEN_42, &clk_stm32_gate_ops, 0),
};

/* LPUART */
static struct clk_stm32_gate ck_icn_p_lpuart1 = {
	.gate_id = GATE_LPUART1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_lpuart1", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_lpuart1 = {
	.gate_id = GATE_LPUART1,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_lpuart1", FLEXGEN_39, &clk_stm32_gate_ops, 0),
};

/* MCO1 & MCO2 */
static struct clk_stm32_composite ck_mco1 = {
	.gate_id = GATE_MCO1,
	.mux_id = MUX_MCO1,
	.div_id = NO_STM32_DIV,
	.hw.init = CLK_HW_INIT_PARENTS_DATA("ck_mco1", mco1_src, &clk_stm32_composite_ops, 0),
};

static struct clk_stm32_composite ck_mco2 = {
	.gate_id = GATE_MCO2,
	.mux_id = MUX_MCO2,
	.div_id = NO_STM32_DIV,
	.hw.init = CLK_HW_INIT_PARENTS_DATA("ck_mco2", mco2_src, &clk_stm32_composite_ops, 0),
};

/* MDF */
static struct clk_stm32_gate ck_icn_p_mdf1 = {
	.gate_id = GATE_MDF1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_mdf1", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_mdf1 = {
	.gate_id = GATE_MDF1,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_mdf1", FLEXGEN_21, &clk_stm32_gate_ops, 0),
};

/* OTG */
static struct clk_stm32_gate ck_icn_m_otg = {
	.gate_id = GATE_OTG,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_m_otg", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

/* PKA */
static struct clk_stm32_gate ck_icn_p_pka = {
	.gate_id = GATE_PKA,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_pka", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

/* RNG */
static struct clk_stm32_gate ck_icn_p_rng1 = {
	.gate_id = GATE_RNG1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_rng1", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_rng2 = {
	.gate_id = GATE_RNG2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_rng2", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

/* SAES */
static struct clk_stm32_gate ck_icn_p_saes = {
	.gate_id = GATE_SAES,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_saes", ICN_LS_MCU, &clk_stm32_gate_ops, 0),
};

/* SAI */
static struct clk_stm32_gate ck_icn_p_sai1 = {
	.gate_id = GATE_SAI1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_sai1", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_sai2 = {
	.gate_id = GATE_SAI2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_sai2", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_sai3 = {
	.gate_id = GATE_SAI3,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_sai3", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_sai4 = {
	.gate_id = GATE_SAI4,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_sai4", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_sai1 = {
	.gate_id = GATE_SAI1,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_sai1", FLEXGEN_22, &clk_stm32_gate_ops,
				       CLK_SET_RATE_PARENT),
};

static struct clk_stm32_gate ck_ker_sai2 = {
	.gate_id = GATE_SAI2,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_sai2", FLEXGEN_23, &clk_stm32_gate_ops,
				       CLK_SET_RATE_PARENT),
};

static struct clk_stm32_gate ck_ker_sai3 = {
	.gate_id = GATE_SAI3,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_sai3", FLEXGEN_24, &clk_stm32_gate_ops,
				       CLK_SET_RATE_PARENT),
};

static struct clk_stm32_gate ck_ker_sai4 = {
	.gate_id = GATE_SAI4,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_sai4", FLEXGEN_25, &clk_stm32_gate_ops,
				       CLK_SET_RATE_PARENT),
};

/* SDMMC */
static struct clk_stm32_gate ck_icn_m_sdmmc1 = {
	.gate_id = GATE_SDMMC1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_m_sdmmc1", ICN_SDMMC, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_m_sdmmc2 = {
	.gate_id = GATE_SDMMC2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_m_sdmmc2", ICN_SDMMC, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_m_sdmmc3 = {
	.gate_id = GATE_SDMMC3,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_m_sdmmc3", ICN_SDMMC, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_sdmmc1 = {
	.gate_id = GATE_SDMMC1,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_sdmmc1", FLEXGEN_51, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_sdmmc2 = {
	.gate_id = GATE_SDMMC2,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_sdmmc2", FLEXGEN_52, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_sdmmc3 = {
	.gate_id = GATE_SDMMC3,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_sdmmc3", FLEXGEN_53, &clk_stm32_gate_ops, 0),
};

/* SERC */
static struct clk_stm32_gate ck_icn_p_serc = {
	.gate_id = GATE_SERC,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_serc", ICN_APB3, &clk_stm32_gate_ops, 0),
};

/* SPDIF */
static struct clk_stm32_gate ck_icn_p_spdifrx = {
	.gate_id = GATE_SPDIFRX,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_spdifrx", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_spdifrx = {
	.gate_id = GATE_SPDIFRX,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_spdifrx", FLEXGEN_12, &clk_stm32_gate_ops, 0),
};

/* SPI */
static struct clk_stm32_gate ck_icn_p_spi1 = {
	.gate_id = GATE_SPI1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_spi1", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_spi2 = {
	.gate_id = GATE_SPI2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_spi2", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_spi3 = {
	.gate_id = GATE_SPI3,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_spi3", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_spi4 = {
	.gate_id = GATE_SPI4,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_spi4", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_spi5 = {
	.gate_id = GATE_SPI5,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_spi5", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_spi6 = {
	.gate_id = GATE_SPI6,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_spi6", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_spi1 = {
	.gate_id = GATE_SPI1,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_spi1", FLEXGEN_16, &clk_stm32_gate_ops,
				     CLK_SET_RATE_PARENT),
};

static struct clk_stm32_gate ck_ker_spi2 = {
	.gate_id = GATE_SPI2,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_spi2", FLEXGEN_10, &clk_stm32_gate_ops,
				       CLK_SET_RATE_PARENT),
};

static struct clk_stm32_gate ck_ker_spi3 = {
	.gate_id = GATE_SPI3,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_spi3", FLEXGEN_11, &clk_stm32_gate_ops,
				       CLK_SET_RATE_PARENT),
};

static struct clk_stm32_gate ck_ker_spi4 = {
	.gate_id = GATE_SPI4,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_spi4", FLEXGEN_17, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_spi5 = {
	.gate_id = GATE_SPI5,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_spi5", FLEXGEN_17, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_spi6 = {
	.gate_id = GATE_SPI6,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_spi6", FLEXGEN_37, &clk_stm32_gate_ops, 0),
};

/* Timers */
static struct clk_stm32_gate ck_icn_p_tim2 = {
	.gate_id = GATE_TIM2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim2", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim3 = {
	.gate_id = GATE_TIM3,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim3", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim4 = {
	.gate_id = GATE_TIM4,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim4", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim5 = {
	.gate_id = GATE_TIM5,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim5", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim6 = {
	.gate_id = GATE_TIM6,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim6", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim7 = {
	.gate_id = GATE_TIM7,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim7", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim10 = {
	.gate_id = GATE_TIM10,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim10", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim11 = {
	.gate_id = GATE_TIM11,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim11", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim12 = {
	.gate_id = GATE_TIM12,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim12", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim13 = {
	.gate_id = GATE_TIM13,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim13", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim14 = {
	.gate_id = GATE_TIM14,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim14", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim1 = {
	.gate_id = GATE_TIM1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim1", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim8 = {
	.gate_id = GATE_TIM8,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim8", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim15 = {
	.gate_id = GATE_TIM15,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim15", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim16 = {
	.gate_id = GATE_TIM16,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim16", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_tim17 = {
	.gate_id = GATE_TIM17,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_tim17", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim2 = {
	.gate_id = GATE_TIM2,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim2", TIMG1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim3 = {
	.gate_id = GATE_TIM3,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim3", TIMG1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim4 = {
	.gate_id = GATE_TIM4,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim4", TIMG1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim5 = {
	.gate_id = GATE_TIM5,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim5", TIMG1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim6 = {
	.gate_id = GATE_TIM6,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim6", TIMG1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim7 = {
	.gate_id = GATE_TIM7,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim7", TIMG1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim10 = {
	.gate_id = GATE_TIM10,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim10", TIMG1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim11 = {
	.gate_id = GATE_TIM11,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim11", TIMG1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim12 = {
	.gate_id = GATE_TIM12,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim12", TIMG1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim13 = {
	.gate_id = GATE_TIM13,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim13", TIMG1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim14 = {
	.gate_id = GATE_TIM14,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim14", TIMG1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim1 = {
	.gate_id = GATE_TIM1,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim1", TIMG2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim8 = {
	.gate_id = GATE_TIM8,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim8", TIMG2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim15 = {
	.gate_id = GATE_TIM15,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim15", TIMG2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim16 = {
	.gate_id = GATE_TIM16,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim16", TIMG2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_tim17 = {
	.gate_id = GATE_TIM17,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_tim17", TIMG2, &clk_stm32_gate_ops, 0),
};

/* UART/USART */
static struct clk_stm32_gate ck_icn_p_usart2 = {
	.gate_id = GATE_USART2,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_usart2", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_usart3 = {
	.gate_id = GATE_USART3,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_usart3", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_uart4 = {
	.gate_id = GATE_UART4,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_uart4", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_uart5 = {
	.gate_id = GATE_UART5,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_uart5", ICN_APB1, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_usart1 = {
	.gate_id = GATE_USART1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_usart1", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_usart6 = {
	.gate_id = GATE_USART6,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_usart6", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_p_uart7 = {
	.gate_id = GATE_UART7,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_uart7", ICN_APB2, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_usart2 = {
	.gate_id = GATE_USART2,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_usart2", FLEXGEN_08, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_uart4 = {
	.gate_id = GATE_UART4,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_uart4", FLEXGEN_08, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_usart3 = {
	.gate_id = GATE_USART3,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_usart3", FLEXGEN_09, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_uart5 = {
	.gate_id = GATE_UART5,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_uart5", FLEXGEN_09, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_usart1 = {
	.gate_id = GATE_USART1,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_usart1", FLEXGEN_18, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_usart6 = {
	.gate_id = GATE_USART6,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_usart6", FLEXGEN_19, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_ker_uart7 = {
	.gate_id = GATE_UART7,
	.hw.init = CLK_HW_INIT_INDEX("ck_ker_uart7", FLEXGEN_20, &clk_stm32_gate_ops, 0),
};

/* USB2PHY1 */
static struct clk_stm32_composite ck_ker_usb2phy1 = {
	.gate_id = GATE_USB2PHY1,
	.mux_id = MUX_USB2PHY1,
	.div_id = NO_STM32_DIV,
	.hw.init = CLK_HW_INIT_PARENTS_DATA("ck_ker_usb2phy1", usb2phy1_src,
					    &clk_stm32_composite_ops, 0),
};

/* USBH */
static struct clk_stm32_gate ck_icn_m_usbhehci = {
	.gate_id = GATE_USBH,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_m_usbhehci", ICN_HSL, &clk_stm32_gate_ops, 0),
};

static struct clk_stm32_gate ck_icn_m_usbhohci = {
	.gate_id = GATE_USBH,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_m_usbhohci", ICN_HSL, &clk_stm32_gate_ops, 0),
};

/* USB2PHY2 */
static struct clk_stm32_composite ck_ker_usb2phy2_en = {
	.gate_id = GATE_USB2PHY2,
	.mux_id = MUX_USB2PHY2,
	.div_id = NO_STM32_DIV,
	.hw.init = CLK_HW_INIT_PARENTS_DATA("ck_ker_usb2phy2_en", usb2phy2_src,
					    &clk_stm32_composite_ops, 0),
};

/* VREF */
static struct clk_stm32_gate ck_icn_p_vref = {
	.gate_id = GATE_VREF,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_vref", ICN_APB3, &clk_stm32_gate_ops, 0),
};

/* WWDG */
static struct clk_stm32_gate ck_icn_p_wwdg1 = {
	.gate_id = GATE_WWDG1,
	.hw.init = CLK_HW_INIT_INDEX("ck_icn_p_wwdg1", ICN_APB3, &clk_stm32_gate_ops, 0),
};

static int stm32_rcc_get_access(void __iomem *base, u32 index)
{
	u32 seccfgr, cidcfgr, semcr;
	int bit, cid;

	bit = index % RCC_REG_SIZE;

	seccfgr = readl(base + RCC_SECCFGR(index));
	if (seccfgr & BIT(bit))
		return -EACCES;

	cidcfgr = readl(base + RCC_CIDCFGR(index));
	if (!(cidcfgr & RCC_CIDCFGR_CFEN))
		/* CID filtering is turned off: access granted */
		return 0;

	if (!(cidcfgr & RCC_CIDCFGR_SEM_EN)) {
		/* Static CID mode */
		cid = FIELD_GET(RCC_CIDCFGR_SCID_MASK, cidcfgr);
		if (cid != RCC_CID1)
			return -EACCES;
		return 0;
	}

	/* Pass-list with semaphore mode */
	if (!(cidcfgr & RCC_CIDCFGR_SEMWLC1_EN))
		return -EACCES;

	semcr = readl(base + RCC_SEMCR(index));

	cid = FIELD_GET(RCC_SEMCR_SEMCID_MASK, semcr);
	if (cid != RCC_CID1)
		return -EACCES;

	return 0;
}

static int stm32mp21_check_security(struct device_node *np, void __iomem *base,
				    const struct clock_config *cfg)
{
	int ret = 0;

	if (cfg->sec_id != SECF_NONE) {
		struct stm32_firewall firewall;
		u32 index = (u32)cfg->sec_id;

		if (index & SEC_RIFSC_FLAG) {
			ret = stm32_firewall_get_firewall(np, &firewall, 1);
			if (ret)
				return ret;
			ret = stm32_firewall_grant_access_by_id(&firewall, index & ~SEC_RIFSC_FLAG);
		} else {
			ret = stm32_rcc_get_access(base, cfg->sec_id & ~SEC_RIFSC_FLAG);
		}
	}

	return ret;
}

static const struct clock_config stm32mp21_clock_cfg[] = {
	STM32_GATE_CFG(CK_BUS_ETH1,		ck_icn_p_eth1,		SEC_RIFSC(60)),
	STM32_GATE_CFG(CK_BUS_ETH2,		ck_icn_p_eth2,		SEC_RIFSC(61)),
	STM32_GATE_CFG(CK_BUS_ADC1,		ck_icn_p_adc1,		SEC_RIFSC(58)),
	STM32_GATE_CFG(CK_BUS_ADC2,		ck_icn_p_adc2,		SEC_RIFSC(59)),
	STM32_GATE_CFG(CK_BUS_CRC,		ck_icn_p_crc,		SEC_RIFSC(109)),
	STM32_GATE_CFG(CK_BUS_MDF1,		ck_icn_p_mdf1,		SEC_RIFSC(54)),
	STM32_GATE_CFG(CK_BUS_HASH1,		ck_icn_p_hash1,		SEC_RIFSC(96)),
	STM32_GATE_CFG(CK_BUS_HASH2,		ck_icn_p_hash2,		SEC_RIFSC(97)),
	STM32_GATE_CFG(CK_BUS_RNG1,		ck_icn_p_rng1,		SEC_RIFSC(92)),
	STM32_GATE_CFG(CK_BUS_RNG2,		ck_icn_p_rng2,		SEC_RIFSC(93)),
	STM32_GATE_CFG(CK_BUS_CRYP1,		ck_icn_p_cryp1,		SEC_RIFSC(98)),
	STM32_GATE_CFG(CK_BUS_CRYP2,		ck_icn_p_cryp2,		SEC_RIFSC(99)),
	STM32_GATE_CFG(CK_BUS_SAES,		ck_icn_p_saes,		SEC_RIFSC(95)),
	STM32_GATE_CFG(CK_BUS_PKA,		ck_icn_p_pka,		SEC_RIFSC(94)),
	STM32_GATE_CFG(CK_BUS_LPUART1,		ck_icn_p_lpuart1,	SEC_RIFSC(40)),
	STM32_GATE_CFG(CK_BUS_LPTIM3,		ck_icn_p_lptim3,	SEC_RIFSC(19)),
	STM32_GATE_CFG(CK_BUS_LPTIM4,		ck_icn_p_lptim4,	SEC_RIFSC(20)),
	STM32_GATE_CFG(CK_BUS_LPTIM5,		ck_icn_p_lptim5,	SEC_RIFSC(21)),
	STM32_GATE_CFG(CK_BUS_SDMMC1,		ck_icn_m_sdmmc1,	SEC_RIFSC(76)),
	STM32_GATE_CFG(CK_BUS_SDMMC2,		ck_icn_m_sdmmc2,	SEC_RIFSC(77)),
	STM32_GATE_CFG(CK_BUS_SDMMC3,		ck_icn_m_sdmmc3,	SEC_RIFSC(78)),
	STM32_GATE_CFG(CK_BUS_USBHOHCI,		ck_icn_m_usbhohci,	SEC_RIFSC(63)),
	STM32_GATE_CFG(CK_BUS_USBHEHCI,		ck_icn_m_usbhehci,	SEC_RIFSC(63)),
	STM32_GATE_CFG(CK_BUS_OTG,		ck_icn_m_otg,		SEC_RIFSC(66)),
	STM32_GATE_CFG(CK_BUS_TIM2,		ck_icn_p_tim2,		SEC_RIFSC(1)),
	STM32_GATE_CFG(CK_BUS_TIM3,		ck_icn_p_tim3,		SEC_RIFSC(2)),
	STM32_GATE_CFG(CK_BUS_TIM4,		ck_icn_p_tim4,		SEC_RIFSC(3)),
	STM32_GATE_CFG(CK_BUS_TIM5,		ck_icn_p_tim5,		SEC_RIFSC(4)),
	STM32_GATE_CFG(CK_BUS_TIM6,		ck_icn_p_tim6,		SEC_RIFSC(5)),
	STM32_GATE_CFG(CK_BUS_TIM7,		ck_icn_p_tim7,		SEC_RIFSC(6)),
	STM32_GATE_CFG(CK_BUS_TIM10,		ck_icn_p_tim10,		SEC_RIFSC(8)),
	STM32_GATE_CFG(CK_BUS_TIM11,		ck_icn_p_tim11,		SEC_RIFSC(9)),
	STM32_GATE_CFG(CK_BUS_TIM12,		ck_icn_p_tim12,		SEC_RIFSC(10)),
	STM32_GATE_CFG(CK_BUS_TIM13,		ck_icn_p_tim13,		SEC_RIFSC(11)),
	STM32_GATE_CFG(CK_BUS_TIM14,		ck_icn_p_tim14,		SEC_RIFSC(12)),
	STM32_GATE_CFG(CK_BUS_LPTIM1,		ck_icn_p_lptim1,	SEC_RIFSC(17)),
	STM32_GATE_CFG(CK_BUS_LPTIM2,		ck_icn_p_lptim2,	SEC_RIFSC(18)),
	STM32_GATE_CFG(CK_BUS_SPI2,		ck_icn_p_spi2,		SEC_RIFSC(23)),
	STM32_GATE_CFG(CK_BUS_SPI3,		ck_icn_p_spi3,		SEC_RIFSC(24)),
	STM32_GATE_CFG(CK_BUS_SPDIFRX,		ck_icn_p_spdifrx,	SEC_RIFSC(30)),
	STM32_GATE_CFG(CK_BUS_USART2,		ck_icn_p_usart2,	SEC_RIFSC(32)),
	STM32_GATE_CFG(CK_BUS_USART3,		ck_icn_p_usart3,	SEC_RIFSC(33)),
	STM32_GATE_CFG(CK_BUS_UART4,		ck_icn_p_uart4,		SEC_RIFSC(34)),
	STM32_GATE_CFG(CK_BUS_UART5,		ck_icn_p_uart5,		SEC_RIFSC(35)),
	STM32_GATE_CFG(CK_BUS_I2C1,		ck_icn_p_i2c1,		SEC_RIFSC(41)),
	STM32_GATE_CFG(CK_BUS_I2C2,		ck_icn_p_i2c2,		SEC_RIFSC(42)),
	STM32_GATE_CFG(CK_BUS_I2C3,		ck_icn_p_i2c3,		SEC_RIFSC(43)),
	STM32_GATE_CFG(CK_BUS_I3C1,		ck_icn_p_i3c1,		SEC_RIFSC(114)),
	STM32_GATE_CFG(CK_BUS_I3C2,		ck_icn_p_i3c2,		SEC_RIFSC(115)),
	STM32_GATE_CFG(CK_BUS_I3C3,		ck_icn_p_i3c3,		SEC_RIFSC(116)),
	STM32_GATE_CFG(CK_BUS_TIM1,		ck_icn_p_tim1,		SEC_RIFSC(0)),
	STM32_GATE_CFG(CK_BUS_TIM8,		ck_icn_p_tim8,		SEC_RIFSC(7)),
	STM32_GATE_CFG(CK_BUS_TIM15,		ck_icn_p_tim15,		SEC_RIFSC(13)),
	STM32_GATE_CFG(CK_BUS_TIM16,		ck_icn_p_tim16,		SEC_RIFSC(14)),
	STM32_GATE_CFG(CK_BUS_TIM17,		ck_icn_p_tim17,		SEC_RIFSC(15)),
	STM32_GATE_CFG(CK_BUS_SAI1,		ck_icn_p_sai1,		SEC_RIFSC(49)),
	STM32_GATE_CFG(CK_BUS_SAI2,		ck_icn_p_sai2,		SEC_RIFSC(50)),
	STM32_GATE_CFG(CK_BUS_SAI3,		ck_icn_p_sai3,		SEC_RIFSC(51)),
	STM32_GATE_CFG(CK_BUS_SAI4,		ck_icn_p_sai4,		SEC_RIFSC(52)),
	STM32_GATE_CFG(CK_BUS_USART1,		ck_icn_p_usart1,	SEC_RIFSC(31)),
	STM32_GATE_CFG(CK_BUS_USART6,		ck_icn_p_usart6,	SEC_RIFSC(36)),
	STM32_GATE_CFG(CK_BUS_UART7,		ck_icn_p_uart7,		SEC_RIFSC(37)),
	STM32_GATE_CFG(CK_BUS_FDCAN,		ck_icn_p_fdcan,		SEC_RIFSC(56)),
	STM32_GATE_CFG(CK_BUS_SPI1,		ck_icn_p_spi1,		SEC_RIFSC(22)),
	STM32_GATE_CFG(CK_BUS_SPI4,		ck_icn_p_spi4,		SEC_RIFSC(25)),
	STM32_GATE_CFG(CK_BUS_SPI5,		ck_icn_p_spi5,		SEC_RIFSC(26)),
	STM32_GATE_CFG(CK_BUS_SPI6,		ck_icn_p_spi6,		SEC_RIFSC(27)),
	STM32_GATE_CFG(CK_BUS_IWDG1,		ck_icn_p_iwdg1,		SEC_RIFSC(100)),
	STM32_GATE_CFG(CK_BUS_IWDG2,		ck_icn_p_iwdg2,		SEC_RIFSC(101)),
	STM32_GATE_CFG(CK_BUS_IWDG3,		ck_icn_p_iwdg3,		SEC_RIFSC(102)),
	STM32_GATE_CFG(CK_BUS_IWDG4,		ck_icn_p_iwdg4,		SEC_RIFSC(103)),
	STM32_GATE_CFG(CK_BUS_WWDG1,		ck_icn_p_wwdg1,		SEC_RIFSC(104)),
	STM32_GATE_CFG(CK_BUS_VREF,		ck_icn_p_vref,		SEC_RIFSC(106)),
	STM32_GATE_CFG(CK_BUS_SERC,		ck_icn_p_serc,		SEC_RIFSC(110)),
	STM32_GATE_CFG(CK_BUS_HDP,		ck_icn_p_hdp,		SEC_RIFSC(57)),
	STM32_GATE_CFG(CK_BUS_LTDC,		ck_icn_p_ltdc,		SEC_RIFSC(80)),
	STM32_GATE_CFG(CK_BUS_CSI,		ck_icn_p_csi,		SEC_RIFSC(86)),
	STM32_GATE_CFG(CK_BUS_DCMIPP,		ck_icn_p_dcmipp,	SEC_RIFSC(87)),
	STM32_GATE_CFG(CK_BUS_DCMIPSSI,		ck_icn_p_dcmipssi,	SEC_RIFSC(88)),
	STM32_GATE_CFG(CK_BUS_DDRPERFM,		ck_icn_p_ddrperfm,	SEC_RIFSC(67)),
	STM32_GATE_CFG(CK_KER_TIM2,		ck_ker_tim2,		SEC_RIFSC(1)),
	STM32_GATE_CFG(CK_KER_TIM3,		ck_ker_tim3,		SEC_RIFSC(2)),
	STM32_GATE_CFG(CK_KER_TIM4,		ck_ker_tim4,		SEC_RIFSC(3)),
	STM32_GATE_CFG(CK_KER_TIM5,		ck_ker_tim5,		SEC_RIFSC(4)),
	STM32_GATE_CFG(CK_KER_TIM6,		ck_ker_tim6,		SEC_RIFSC(5)),
	STM32_GATE_CFG(CK_KER_TIM7,		ck_ker_tim7,		SEC_RIFSC(6)),
	STM32_GATE_CFG(CK_KER_TIM10,		ck_ker_tim10,		SEC_RIFSC(8)),
	STM32_GATE_CFG(CK_KER_TIM11,		ck_ker_tim11,		SEC_RIFSC(9)),
	STM32_GATE_CFG(CK_KER_TIM12,		ck_ker_tim12,		SEC_RIFSC(10)),
	STM32_GATE_CFG(CK_KER_TIM13,		ck_ker_tim13,		SEC_RIFSC(11)),
	STM32_GATE_CFG(CK_KER_TIM14,		ck_ker_tim14,		SEC_RIFSC(12)),
	STM32_GATE_CFG(CK_KER_TIM1,		ck_ker_tim1,		SEC_RIFSC(0)),
	STM32_GATE_CFG(CK_KER_TIM8,		ck_ker_tim8,		SEC_RIFSC(7)),
	STM32_GATE_CFG(CK_KER_TIM15,		ck_ker_tim15,		SEC_RIFSC(13)),
	STM32_GATE_CFG(CK_KER_TIM16,		ck_ker_tim16,		SEC_RIFSC(14)),
	STM32_GATE_CFG(CK_KER_TIM17,		ck_ker_tim17,		SEC_RIFSC(15)),
	STM32_GATE_CFG(CK_KER_LPTIM1,		ck_ker_lptim1,		SEC_RIFSC(17)),
	STM32_GATE_CFG(CK_KER_LPTIM2,		ck_ker_lptim2,		SEC_RIFSC(18)),
	STM32_GATE_CFG(CK_KER_USART2,		ck_ker_usart2,		SEC_RIFSC(32)),
	STM32_GATE_CFG(CK_KER_UART4,		ck_ker_uart4,		SEC_RIFSC(34)),
	STM32_GATE_CFG(CK_KER_USART3,		ck_ker_usart3,		SEC_RIFSC(33)),
	STM32_GATE_CFG(CK_KER_UART5,		ck_ker_uart5,		SEC_RIFSC(35)),
	STM32_GATE_CFG(CK_KER_SPI2,		ck_ker_spi2,		SEC_RIFSC(23)),
	STM32_GATE_CFG(CK_KER_SPI3,		ck_ker_spi3,		SEC_RIFSC(24)),
	STM32_GATE_CFG(CK_KER_SPDIFRX,		ck_ker_spdifrx,		SEC_RIFSC(30)),
	STM32_GATE_CFG(CK_KER_I2C1,		ck_ker_i2c1,		SEC_RIFSC(41)),
	STM32_GATE_CFG(CK_KER_I2C2,		ck_ker_i2c2,		SEC_RIFSC(42)),
	STM32_GATE_CFG(CK_KER_I3C1,		ck_ker_i3c1,		SEC_RIFSC(114)),
	STM32_GATE_CFG(CK_KER_I3C2,		ck_ker_i3c2,		SEC_RIFSC(115)),
	STM32_GATE_CFG(CK_KER_I2C3,		ck_ker_i2c3,		SEC_RIFSC(43)),
	STM32_GATE_CFG(CK_KER_I3C3,		ck_ker_i3c3,		SEC_RIFSC(116)),
	STM32_GATE_CFG(CK_KER_SPI1,		ck_ker_spi1,		SEC_RIFSC(22)),
	STM32_GATE_CFG(CK_KER_SPI4,		ck_ker_spi4,		SEC_RIFSC(25)),
	STM32_GATE_CFG(CK_KER_SPI5,		ck_ker_spi5,		SEC_RIFSC(26)),
	STM32_GATE_CFG(CK_KER_SPI6,		ck_ker_spi6,		SEC_RIFSC(27)),
	STM32_GATE_CFG(CK_KER_USART1,		ck_ker_usart1,		SEC_RIFSC(31)),
	STM32_GATE_CFG(CK_KER_USART6,		ck_ker_usart6,		SEC_RIFSC(36)),
	STM32_GATE_CFG(CK_KER_UART7,		ck_ker_uart7,		SEC_RIFSC(37)),
	STM32_GATE_CFG(CK_KER_MDF1,		ck_ker_mdf1,		SEC_RIFSC(54)),
	STM32_GATE_CFG(CK_KER_SAI1,		ck_ker_sai1,		SEC_RIFSC(49)),
	STM32_GATE_CFG(CK_KER_SAI2,		ck_ker_sai2,		SEC_RIFSC(50)),
	STM32_GATE_CFG(CK_KER_SAI3,		ck_ker_sai3,		SEC_RIFSC(51)),
	STM32_GATE_CFG(CK_KER_SAI4,		ck_ker_sai4,		SEC_RIFSC(52)),
	STM32_GATE_CFG(CK_KER_FDCAN,		ck_ker_fdcan,		SEC_RIFSC(56)),
	STM32_GATE_CFG(CK_KER_CSI,		ck_ker_csi,		SEC_RIFSC(86)),
	STM32_GATE_CFG(CK_KER_CSITXESC,		ck_ker_csitxesc,	SEC_RIFSC(86)),
	STM32_GATE_CFG(CK_KER_CSIPHY,		ck_ker_csiphy,		SEC_RIFSC(86)),
	STM32_GATE_CFG(CK_KER_LPUART1,		ck_ker_lpuart1,		SEC_RIFSC(40)),
	STM32_GATE_CFG(CK_KER_LPTIM3,		ck_ker_lptim3,		SEC_RIFSC(19)),
	STM32_GATE_CFG(CK_KER_LPTIM4,		ck_ker_lptim4,		SEC_RIFSC(20)),
	STM32_GATE_CFG(CK_KER_LPTIM5,		ck_ker_lptim5,		SEC_RIFSC(21)),
	STM32_GATE_CFG(CK_KER_SDMMC1,		ck_ker_sdmmc1,		SEC_RIFSC(76)),
	STM32_GATE_CFG(CK_KER_SDMMC2,		ck_ker_sdmmc2,		SEC_RIFSC(77)),
	STM32_GATE_CFG(CK_KER_SDMMC3,		ck_ker_sdmmc3,		SEC_RIFSC(78)),
	STM32_GATE_CFG(CK_KER_ETH1,		ck_ker_eth1,		SEC_RIFSC(60)),
	STM32_GATE_CFG(CK_ETH1_STP,		ck_ker_eth1stp,		SEC_RIFSC(60)),
	STM32_GATE_CFG(CK_KER_ETH2,		ck_ker_eth2,		SEC_RIFSC(61)),
	STM32_GATE_CFG(CK_ETH2_STP,		ck_ker_eth2stp,		SEC_RIFSC(61)),
	STM32_GATE_CFG(CK_KER_ETH1PTP,		ck_ker_eth1ptp,		SEC_RIFSC(60)),
	STM32_GATE_CFG(CK_KER_ETH2PTP,		ck_ker_eth2ptp,		SEC_RIFSC(61)),
	STM32_GATE_CFG(CK_ETH1_MAC,		ck_ker_eth1mac,		SEC_RIFSC(60)),
	STM32_GATE_CFG(CK_ETH1_TX,		ck_ker_eth1tx,		SEC_RIFSC(60)),
	STM32_GATE_CFG(CK_ETH1_RX,		ck_ker_eth1rx,		SEC_RIFSC(60)),
	STM32_GATE_CFG(CK_ETH2_MAC,		ck_ker_eth2mac,		SEC_RIFSC(61)),
	STM32_GATE_CFG(CK_ETH2_TX,		ck_ker_eth2tx,		SEC_RIFSC(61)),
	STM32_GATE_CFG(CK_ETH2_RX,		ck_ker_eth2rx,		SEC_RIFSC(61)),
	STM32_COMPOSITE_CFG(CK_MCO1,		ck_mco1,		MP21_RIF_RCC_MCO1),
	STM32_COMPOSITE_CFG(CK_MCO2,		ck_mco2,		MP21_RIF_RCC_MCO2),
	STM32_COMPOSITE_CFG(CK_KER_ADC1,	ck_ker_adc1,		SEC_RIFSC(58)),
	STM32_COMPOSITE_CFG(CK_KER_ADC2,	ck_ker_adc2,		SEC_RIFSC(59)),
	STM32_COMPOSITE_CFG(CK_KER_USB2PHY1,	ck_ker_usb2phy1,	SEC_RIFSC(63)),
	STM32_COMPOSITE_CFG(CK_KER_USB2PHY2EN,	ck_ker_usb2phy2_en,	SEC_RIFSC(66)),
	STM32_COMPOSITE_CFG(CK_KER_DTS,		ck_ker_dts,		SEC_RIFSC(107)),
	STM32_GATE_CFG(CK_KER_LTDC,		ck_ker_ltdc,		SEC_RIFSC(80)),
};

#define RESET_MP21(id, _offset, _bit_idx, _set_clr)	\
	[id] = &(struct stm32_reset_cfg){		\
		.offset		= (_offset),		\
		.bit_idx	= (_bit_idx),		\
		.set_clr	= (_set_clr),		\
	}

static const struct stm32_reset_cfg *stm32mp21_reset_cfg[] = {
	RESET_MP21(TIM1_R,		RCC_TIM1CFGR,		0,	0),
	RESET_MP21(TIM2_R,		RCC_TIM2CFGR,		0,	0),
	RESET_MP21(TIM3_R,		RCC_TIM3CFGR,		0,	0),
	RESET_MP21(TIM4_R,		RCC_TIM4CFGR,		0,	0),
	RESET_MP21(TIM5_R,		RCC_TIM5CFGR,		0,	0),
	RESET_MP21(TIM6_R,		RCC_TIM6CFGR,		0,	0),
	RESET_MP21(TIM7_R,		RCC_TIM7CFGR,		0,	0),
	RESET_MP21(TIM8_R,		RCC_TIM8CFGR,		0,	0),
	RESET_MP21(TIM10_R,		RCC_TIM10CFGR,		0,	0),
	RESET_MP21(TIM11_R,		RCC_TIM11CFGR,		0,	0),
	RESET_MP21(TIM12_R,		RCC_TIM12CFGR,		0,	0),
	RESET_MP21(TIM13_R,		RCC_TIM13CFGR,		0,	0),
	RESET_MP21(TIM14_R,		RCC_TIM14CFGR,		0,	0),
	RESET_MP21(TIM15_R,		RCC_TIM15CFGR,		0,	0),
	RESET_MP21(TIM16_R,		RCC_TIM16CFGR,		0,	0),
	RESET_MP21(TIM17_R,		RCC_TIM17CFGR,		0,	0),
	RESET_MP21(LPTIM1_R,		RCC_LPTIM1CFGR,		0,	0),
	RESET_MP21(LPTIM2_R,		RCC_LPTIM2CFGR,		0,	0),
	RESET_MP21(LPTIM3_R,		RCC_LPTIM3CFGR,		0,	0),
	RESET_MP21(LPTIM4_R,		RCC_LPTIM4CFGR,		0,	0),
	RESET_MP21(LPTIM5_R,		RCC_LPTIM5CFGR,		0,	0),
	RESET_MP21(SPI1_R,		RCC_SPI1CFGR,		0,	0),
	RESET_MP21(SPI2_R,		RCC_SPI2CFGR,		0,	0),
	RESET_MP21(SPI3_R,		RCC_SPI3CFGR,		0,	0),
	RESET_MP21(SPI4_R,		RCC_SPI4CFGR,		0,	0),
	RESET_MP21(SPI5_R,		RCC_SPI5CFGR,		0,	0),
	RESET_MP21(SPI6_R,		RCC_SPI6CFGR,		0,	0),
	RESET_MP21(SPDIFRX_R,		RCC_SPDIFRXCFGR,	0,	0),
	RESET_MP21(USART1_R,		RCC_USART1CFGR,		0,	0),
	RESET_MP21(USART2_R,		RCC_USART2CFGR,		0,	0),
	RESET_MP21(USART3_R,		RCC_USART3CFGR,		0,	0),
	RESET_MP21(UART4_R,		RCC_UART4CFGR,		0,	0),
	RESET_MP21(UART5_R,		RCC_UART5CFGR,		0,	0),
	RESET_MP21(USART6_R,		RCC_USART6CFGR,		0,	0),
	RESET_MP21(UART7_R,		RCC_UART7CFGR,		0,	0),
	RESET_MP21(LPUART1_R,		RCC_LPUART1CFGR,	0,	0),
	RESET_MP21(I2C1_R,		RCC_I2C1CFGR,		0,	0),
	RESET_MP21(I2C2_R,		RCC_I2C2CFGR,		0,	0),
	RESET_MP21(I2C3_R,		RCC_I2C3CFGR,		0,	0),
	RESET_MP21(SAI1_R,		RCC_SAI1CFGR,		0,	0),
	RESET_MP21(SAI2_R,		RCC_SAI2CFGR,		0,	0),
	RESET_MP21(SAI3_R,		RCC_SAI3CFGR,		0,	0),
	RESET_MP21(SAI4_R,		RCC_SAI4CFGR,		0,	0),
	RESET_MP21(MDF1_R,		RCC_MDF1CFGR,		0,	0),
	RESET_MP21(FDCAN_R,		RCC_FDCANCFGR,		0,	0),
	RESET_MP21(HDP_R,		RCC_HDPCFGR,		0,	0),
	RESET_MP21(ADC1_R,		RCC_ADC1CFGR,		0,	0),
	RESET_MP21(ADC2_R,		RCC_ADC2CFGR,		0,	0),
	RESET_MP21(ETH1_R,		RCC_ETH1CFGR,		0,	0),
	RESET_MP21(ETH2_R,		RCC_ETH2CFGR,		0,	0),
	RESET_MP21(OTG_R,		RCC_OTGCFGR,		0,	0),
	RESET_MP21(USBH_R,		RCC_USBHCFGR,		0,	0),
	RESET_MP21(USB2PHY1_R,		RCC_USB2PHY1CFGR,	0,	0),
	RESET_MP21(USB2PHY2_R,		RCC_USB2PHY2CFGR,	0,	0),
	RESET_MP21(SDMMC1_R,		RCC_SDMMC1CFGR,		0,	0),
	RESET_MP21(SDMMC1DLL_R,		RCC_SDMMC1CFGR,		16,	0),
	RESET_MP21(SDMMC2_R,		RCC_SDMMC2CFGR,		0,	0),
	RESET_MP21(SDMMC2DLL_R,		RCC_SDMMC2CFGR,		16,	0),
	RESET_MP21(SDMMC3_R,		RCC_SDMMC3CFGR,		0,	0),
	RESET_MP21(SDMMC3DLL_R,		RCC_SDMMC3CFGR,		16,	0),
	RESET_MP21(LTDC_R,		RCC_LTDCCFGR,		0,	0),
	RESET_MP21(CSI_R,		RCC_CSICFGR,		0,	0),
	RESET_MP21(DCMIPP_R,		RCC_DCMIPPCFGR,		0,	0),
	RESET_MP21(DCMIPSSI_R,		RCC_DCMIPSSICFGR,	0,	0),
	RESET_MP21(WWDG1_R,		RCC_WWDG1CFGR,		0,	0),
	RESET_MP21(VREF_R,		RCC_VREFCFGR,		0,	0),
	RESET_MP21(DTS_R,		RCC_DTSCFGR,		0,	0),
	RESET_MP21(CRC_R,		RCC_CRCCFGR,		0,	0),
	RESET_MP21(SERC_R,		RCC_SERCCFGR,		0,	0),
	RESET_MP21(I3C1_R,		RCC_I3C1CFGR,		0,	0),
	RESET_MP21(I3C2_R,		RCC_I3C2CFGR,		0,	0),
	RESET_MP21(IWDG2_KER_R,		RCC_IWDGC1CFGSETR,	18,	1),
	RESET_MP21(IWDG4_KER_R,		RCC_IWDGC2CFGSETR,	18,	1),
	RESET_MP21(RNG1_R,		RCC_RNG1CFGR,		0,	0),
	RESET_MP21(RNG2_R,		RCC_RNG2CFGR,		0,	0),
	RESET_MP21(PKA_R,		RCC_PKACFGR,		0,	0),
	RESET_MP21(SAES_R,		RCC_SAESCFGR,		0,	0),
	RESET_MP21(HASH1_R,		RCC_HASH1CFGR,		0,	0),
	RESET_MP21(HASH2_R,		RCC_HASH2CFGR,		0,	0),
	RESET_MP21(CRYP1_R,		RCC_CRYP1CFGR,		0,	0),
	RESET_MP21(CRYP2_R,		RCC_CRYP2CFGR,		0,	0),
};

static u16 stm32mp21_cpt_gate[GATE_NB];

static struct clk_stm32_clock_data stm32mp21_clock_data = {
	.gate_cpt	= stm32mp21_cpt_gate,
	.gates		= stm32mp21_gates,
	.muxes		= stm32mp21_muxes,
};

static struct clk_stm32_reset_data stm32mp21_reset_data = {
	.reset_lines	= stm32mp21_reset_cfg,
	.nr_lines	= ARRAY_SIZE(stm32mp21_reset_cfg),
};

static const struct stm32_rcc_match_data stm32mp21_data = {
	.tab_clocks	= stm32mp21_clock_cfg,
	.num_clocks	= ARRAY_SIZE(stm32mp21_clock_cfg),
	.maxbinding	= STM32MP21_LAST_CLK,
	.clock_data	= &stm32mp21_clock_data,
	.reset_data	= &stm32mp21_reset_data,
	.check_security = &stm32mp21_check_security,
};

static const struct of_device_id stm32mp21_match_data[] = {
	{ .compatible = "st,stm32mp21-rcc", .data = &stm32mp21_data, },
	{ }
};
MODULE_DEVICE_TABLE(of, stm32mp21_match_data);

static int stm32mp21_rcc_clocks_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *base;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(base)))
		return PTR_ERR(base);

	return stm32_rcc_init(dev, stm32mp21_match_data, base);
}

static struct platform_driver stm32mp21_rcc_clocks_driver = {
	.driver	= {
		.name = "stm32mp21_rcc",
		.of_match_table = stm32mp21_match_data,
	},
	.probe = stm32mp21_rcc_clocks_probe,
};

static int __init stm32mp21_clocks_init(void)
{
	return platform_driver_register(&stm32mp21_rcc_clocks_driver);
}

core_initcall(stm32mp21_clocks_init);

