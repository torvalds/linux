// SPDX-License-Identifier: GPL-2.0+
/*
 * Actions Semi Owl S700 Pinctrl driver
 *
 * Copyright (c) 2014 Actions Semi Inc.
 * Author: David Liu <liuwei@actions-semi.com>
 *
 * Author: Pathiban Nallathambi <pn@denx.de>
 * Author: Saravanan Sekar <sravanhome@gmail.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include "pinctrl-owl.h"

/* Pinctrl registers offset */
#define MFCTL0			(0x0040)
#define MFCTL1			(0x0044)
#define MFCTL2			(0x0048)
#define MFCTL3			(0x004C)
#define PAD_PULLCTL0		(0x0060)
#define PAD_PULLCTL1		(0x0064)
#define PAD_PULLCTL2		(0x0068)
#define PAD_ST0			(0x006C)
#define PAD_ST1			(0x0070)
#define PAD_CTL			(0x0074)
#define PAD_DRV0		(0x0080)
#define PAD_DRV1		(0x0084)
#define PAD_DRV2		(0x0088)

/*
 * Most pins affected by the pinmux can also be GPIOs. Define these first.
 * These must match how the GPIO driver names/numbers its pins.
 */
#define _GPIOA(offset)		(offset)
#define _GPIOB(offset)		(32 + (offset))
#define _GPIOC(offset)		(64 + (offset))
#define _GPIOD(offset)		(96 + (offset))
#define _GPIOE(offset)		(128 + (offset))

/* All non-GPIO pins follow */
#define NUM_GPIOS		(_GPIOE(7) + 1)
#define _PIN(offset)		(NUM_GPIOS + (offset))

/* Ethernet MAC */
#define ETH_TXD0		_GPIOA(14)
#define ETH_TXD1		_GPIOA(15)
#define ETH_TXD2		_GPIOE(4)
#define ETH_TXD3		_GPIOE(5)
#define ETH_TXEN		_GPIOA(16)
#define ETH_RXER		_GPIOA(17)
#define ETH_CRS_DV		_GPIOA(18)
#define ETH_RXD1		_GPIOA(19)
#define ETH_RXD0		_GPIOA(20)
#define ETH_RXD2		_GPIOE(6)
#define ETH_RXD3		_GPIOE(7)
#define ETH_REF_CLK		_GPIOA(21)
#define ETH_MDC			_GPIOA(22)
#define ETH_MDIO		_GPIOA(23)

/* SIRQ */
#define SIRQ0			_GPIOA(24)
#define SIRQ1			_GPIOA(25)
#define SIRQ2			_GPIOA(26)

/* I2S */
#define I2S_D0			_GPIOA(27)
#define I2S_BCLK0		_GPIOA(28)
#define I2S_LRCLK0		_GPIOA(29)
#define I2S_MCLK0		_GPIOA(30)
#define I2S_D1			_GPIOA(31)
#define I2S_BCLK1		_GPIOB(0)
#define I2S_LRCLK1		_GPIOB(1)
#define I2S_MCLK1		_GPIOB(2)

/* PCM1 */
#define PCM1_IN			_GPIOD(28)
#define PCM1_CLK		_GPIOD(29)
#define PCM1_SYNC		_GPIOD(30)
#define PCM1_OUT		_GPIOD(31)

/* KEY */
#define KS_IN0			_GPIOB(3)
#define KS_IN1			_GPIOB(4)
#define KS_IN2			_GPIOB(5)
#define KS_IN3			_GPIOB(6)
#define KS_OUT0			_GPIOB(7)
#define KS_OUT1			_GPIOB(8)
#define KS_OUT2			_GPIOB(9)

/* LVDS */
#define LVDS_OEP		_GPIOB(10)
#define LVDS_OEN		_GPIOB(11)
#define LVDS_ODP		_GPIOB(12)
#define LVDS_ODN		_GPIOB(13)
#define LVDS_OCP		_GPIOB(14)
#define LVDS_OCN		_GPIOB(15)
#define LVDS_OBP		_GPIOB(16)
#define LVDS_OBN		_GPIOB(17)
#define LVDS_OAP		_GPIOB(18)
#define LVDS_OAN		_GPIOB(19)
#define LVDS_EEP		_GPIOB(20)
#define LVDS_EEN		_GPIOB(21)
#define LVDS_EDP		_GPIOB(22)
#define LVDS_EDN		_GPIOB(23)
#define LVDS_ECP		_GPIOB(24)
#define LVDS_ECN		_GPIOB(25)
#define LVDS_EBP		_GPIOB(26)
#define LVDS_EBN		_GPIOB(27)
#define LVDS_EAP		_GPIOB(28)
#define LVDS_EAN		_GPIOB(29)
#define LCD0_D18		_GPIOB(30)
#define LCD0_D2			_GPIOB(31)

/* DSI */
#define DSI_DP3			_GPIOC(0)
#define DSI_DN3			_GPIOC(1)
#define DSI_DP1			_GPIOC(2)
#define DSI_DN1			_GPIOC(3)
#define DSI_CP			_GPIOC(4)
#define DSI_CN			_GPIOC(5)
#define DSI_DP0			_GPIOC(6)
#define DSI_DN0			_GPIOC(7)
#define DSI_DP2			_GPIOC(8)
#define DSI_DN2			_GPIOC(9)

/* SD */
#define SD0_D0			_GPIOC(10)
#define SD0_D1			_GPIOC(11)
#define SD0_D2			_GPIOC(12)
#define SD0_D3			_GPIOC(13)
#define SD0_D4			_GPIOC(14)
#define SD0_D5			_GPIOC(15)
#define SD0_D6			_GPIOC(16)
#define SD0_D7			_GPIOC(17)
#define SD0_CMD			_GPIOC(18)
#define SD0_CLK			_GPIOC(19)
#define SD1_CMD			_GPIOC(20)
#define SD1_CLK			_GPIOC(21)
#define SD1_D0			SD0_D4
#define SD1_D1			SD0_D5
#define SD1_D2			SD0_D6
#define SD1_D3			SD0_D7

/* SPI */
#define SPI0_SS			_GPIOC(23)
#define SPI0_MISO		_GPIOC(24)

/* UART for console */
#define UART0_RX		_GPIOC(26)
#define UART0_TX		_GPIOC(27)

/* UART for Bluetooth */
#define UART2_RX		_GPIOD(18)
#define UART2_TX		_GPIOD(19)
#define UART2_RTSB		_GPIOD(20)
#define UART2_CTSB		_GPIOD(21)

/* UART for 3G */
#define UART3_RX		_GPIOD(22)
#define UART3_TX		_GPIOD(23)
#define UART3_RTSB		_GPIOD(24)
#define UART3_CTSB		_GPIOD(25)

/* I2C */
#define I2C0_SCLK		_GPIOC(28)
#define I2C0_SDATA		_GPIOC(29)
#define I2C1_SCLK		_GPIOE(0)
#define I2C1_SDATA		_GPIOE(1)
#define I2C2_SCLK		_GPIOE(2)
#define I2C2_SDATA		_GPIOE(3)

/* CSI*/
#define CSI_DN0			_PIN(0)
#define CSI_DP0			_PIN(1)
#define CSI_DN1			_PIN(2)
#define CSI_DP1			_PIN(3)
#define CSI_CN			_PIN(4)
#define CSI_CP			_PIN(5)
#define CSI_DN2			_PIN(6)
#define CSI_DP2			_PIN(7)
#define CSI_DN3			_PIN(8)
#define CSI_DP3			_PIN(9)

/* Sensor */
#define SENSOR0_PCLK		_GPIOC(31)
#define SENSOR0_CKOUT		_GPIOD(10)

/* NAND (1.8v / 3.3v) */
#define DNAND_D0		_PIN(10)
#define DNAND_D1		_PIN(11)
#define DNAND_D2		_PIN(12)
#define DNAND_D3		_PIN(13)
#define DNAND_D4		_PIN(14)
#define DNAND_D5		_PIN(15)
#define DNAND_D6		_PIN(16)
#define DNAND_D7		_PIN(17)
#define DNAND_WRB		_PIN(18)
#define DNAND_RDB		_PIN(19)
#define DNAND_RDBN		_PIN(20)
#define DNAND_DQS		_GPIOA(12)
#define DNAND_DQSN		_GPIOA(13)
#define DNAND_RB0		_PIN(21)
#define DNAND_ALE		_GPIOD(12)
#define DNAND_CLE		_GPIOD(13)
#define DNAND_CEB0		_GPIOD(14)
#define DNAND_CEB1		_GPIOD(15)
#define DNAND_CEB2		_GPIOD(16)
#define DNAND_CEB3		_GPIOD(17)

/* System */
#define PORB			_PIN(22)
#define CLKO_25M		_PIN(23)
#define BSEL			_PIN(24)
#define PKG0			_PIN(25)
#define PKG1			_PIN(26)
#define PKG2			_PIN(27)
#define PKG3			_PIN(28)

#define _FIRSTPAD		_GPIOA(0)
#define _LASTPAD		PKG3
#define NUM_PADS		(_PIN(28) + 1)

/* Pad names for the pinmux subsystem */
static const struct pinctrl_pin_desc s700_pads[] = {
	PINCTRL_PIN(ETH_TXD0, "eth_txd0"),
	PINCTRL_PIN(ETH_TXD1, "eth_txd1"),
	PINCTRL_PIN(ETH_TXD2, "eth_txd2"),
	PINCTRL_PIN(ETH_TXD3, "eth_txd3"),
	PINCTRL_PIN(ETH_TXEN, "eth_txen"),
	PINCTRL_PIN(ETH_RXER, "eth_rxer"),
	PINCTRL_PIN(ETH_CRS_DV, "eth_crs_dv"),
	PINCTRL_PIN(ETH_RXD1, "eth_rxd1"),
	PINCTRL_PIN(ETH_RXD0, "eth_rxd0"),
	PINCTRL_PIN(ETH_RXD2, "eth_rxd2"),
	PINCTRL_PIN(ETH_RXD3, "eth_rxd3"),
	PINCTRL_PIN(ETH_REF_CLK, "eth_ref_clk"),
	PINCTRL_PIN(ETH_MDC, "eth_mdc"),
	PINCTRL_PIN(ETH_MDIO, "eth_mdio"),
	PINCTRL_PIN(SIRQ0, "sirq0"),
	PINCTRL_PIN(SIRQ1, "sirq1"),
	PINCTRL_PIN(SIRQ2, "sirq2"),
	PINCTRL_PIN(I2S_D0, "i2s_d0"),
	PINCTRL_PIN(I2S_BCLK0, "i2s_bclk0"),
	PINCTRL_PIN(I2S_LRCLK0, "i2s_lrclk0"),
	PINCTRL_PIN(I2S_MCLK0, "i2s_mclk0"),
	PINCTRL_PIN(I2S_D1, "i2s_d1"),
	PINCTRL_PIN(I2S_BCLK1, "i2s_bclk1"),
	PINCTRL_PIN(I2S_LRCLK1, "i2s_lrclk1"),
	PINCTRL_PIN(I2S_MCLK1, "i2s_mclk1"),
	PINCTRL_PIN(PCM1_IN, "pcm1_in"),
	PINCTRL_PIN(PCM1_CLK, "pcm1_clk"),
	PINCTRL_PIN(PCM1_SYNC, "pcm1_sync"),
	PINCTRL_PIN(PCM1_OUT, "pcm1_out"),
	PINCTRL_PIN(KS_IN0, "ks_in0"),
	PINCTRL_PIN(KS_IN1, "ks_in1"),
	PINCTRL_PIN(KS_IN2, "ks_in2"),
	PINCTRL_PIN(KS_IN3, "ks_in3"),
	PINCTRL_PIN(KS_OUT0, "ks_out0"),
	PINCTRL_PIN(KS_OUT1, "ks_out1"),
	PINCTRL_PIN(KS_OUT2, "ks_out2"),
	PINCTRL_PIN(LVDS_OEP, "lvds_oep"),
	PINCTRL_PIN(LVDS_OEN, "lvds_oen"),
	PINCTRL_PIN(LVDS_ODP, "lvds_odp"),
	PINCTRL_PIN(LVDS_ODN, "lvds_odn"),
	PINCTRL_PIN(LVDS_OCP, "lvds_ocp"),
	PINCTRL_PIN(LVDS_OCN, "lvds_ocn"),
	PINCTRL_PIN(LVDS_OBP, "lvds_obp"),
	PINCTRL_PIN(LVDS_OBN, "lvds_obn"),
	PINCTRL_PIN(LVDS_OAP, "lvds_oap"),
	PINCTRL_PIN(LVDS_OAN, "lvds_oan"),
	PINCTRL_PIN(LVDS_EEP, "lvds_eep"),
	PINCTRL_PIN(LVDS_EEN, "lvds_een"),
	PINCTRL_PIN(LVDS_EDP, "lvds_edp"),
	PINCTRL_PIN(LVDS_EDN, "lvds_edn"),
	PINCTRL_PIN(LVDS_ECP, "lvds_ecp"),
	PINCTRL_PIN(LVDS_ECN, "lvds_ecn"),
	PINCTRL_PIN(LVDS_EBP, "lvds_ebp"),
	PINCTRL_PIN(LVDS_EBN, "lvds_ebn"),
	PINCTRL_PIN(LVDS_EAP, "lvds_eap"),
	PINCTRL_PIN(LVDS_EAN, "lvds_ean"),
	PINCTRL_PIN(LCD0_D18, "lcd0_d18"),
	PINCTRL_PIN(LCD0_D2, "lcd0_d2"),
	PINCTRL_PIN(DSI_DP3, "dsi_dp3"),
	PINCTRL_PIN(DSI_DN3, "dsi_dn3"),
	PINCTRL_PIN(DSI_DP1, "dsi_dp1"),
	PINCTRL_PIN(DSI_DN1, "dsi_dn1"),
	PINCTRL_PIN(DSI_CP, "dsi_cp"),
	PINCTRL_PIN(DSI_CN, "dsi_cn"),
	PINCTRL_PIN(DSI_DP0, "dsi_dp0"),
	PINCTRL_PIN(DSI_DN0, "dsi_dn0"),
	PINCTRL_PIN(DSI_DP2, "dsi_dp2"),
	PINCTRL_PIN(DSI_DN2, "dsi_dn2"),
	PINCTRL_PIN(SD0_D0, "sd0_d0"),
	PINCTRL_PIN(SD0_D1, "sd0_d1"),
	PINCTRL_PIN(SD0_D2, "sd0_d2"),
	PINCTRL_PIN(SD0_D3, "sd0_d3"),
	PINCTRL_PIN(SD1_D0, "sd1_d0"),
	PINCTRL_PIN(SD1_D1, "sd1_d1"),
	PINCTRL_PIN(SD1_D2, "sd1_d2"),
	PINCTRL_PIN(SD1_D3, "sd1_d3"),
	PINCTRL_PIN(SD0_CMD, "sd0_cmd"),
	PINCTRL_PIN(SD0_CLK, "sd0_clk"),
	PINCTRL_PIN(SD1_CMD, "sd1_cmd"),
	PINCTRL_PIN(SD1_CLK, "sd1_clk"),
	PINCTRL_PIN(SPI0_SS, "spi0_ss"),
	PINCTRL_PIN(SPI0_MISO, "spi0_miso"),
	PINCTRL_PIN(UART0_RX, "uart0_rx"),
	PINCTRL_PIN(UART0_TX, "uart0_tx"),
	PINCTRL_PIN(UART2_RX, "uart2_rx"),
	PINCTRL_PIN(UART2_TX, "uart2_tx"),
	PINCTRL_PIN(UART2_RTSB, "uart2_rtsb"),
	PINCTRL_PIN(UART2_CTSB, "uart2_ctsb"),
	PINCTRL_PIN(UART3_RX, "uart3_rx"),
	PINCTRL_PIN(UART3_TX, "uart3_tx"),
	PINCTRL_PIN(UART3_RTSB, "uart3_rtsb"),
	PINCTRL_PIN(UART3_CTSB, "uart3_ctsb"),
	PINCTRL_PIN(I2C0_SCLK, "i2c0_sclk"),
	PINCTRL_PIN(I2C0_SDATA, "i2c0_sdata"),
	PINCTRL_PIN(I2C1_SCLK, "i2c1_sclk"),
	PINCTRL_PIN(I2C1_SDATA, "i2c1_sdata"),
	PINCTRL_PIN(I2C2_SCLK, "i2c2_sclk"),
	PINCTRL_PIN(I2C2_SDATA, "i2c2_sdata"),
	PINCTRL_PIN(CSI_DN0, "csi_dn0"),
	PINCTRL_PIN(CSI_DP0, "csi_dp0"),
	PINCTRL_PIN(CSI_DN1, "csi_dn1"),
	PINCTRL_PIN(CSI_DP1, "csi_dp1"),
	PINCTRL_PIN(CSI_CN, "csi_cn"),
	PINCTRL_PIN(CSI_CP, "csi_cp"),
	PINCTRL_PIN(CSI_DN2, "csi_dn2"),
	PINCTRL_PIN(CSI_DP2, "csi_dp2"),
	PINCTRL_PIN(CSI_DN3, "csi_dn3"),
	PINCTRL_PIN(CSI_DP3, "csi_dp3"),
	PINCTRL_PIN(SENSOR0_PCLK, "sensor0_pclk"),
	PINCTRL_PIN(SENSOR0_CKOUT, "sensor0_ckout"),
	PINCTRL_PIN(DNAND_D0, "dnand_d0"),
	PINCTRL_PIN(DNAND_D1, "dnand_d1"),
	PINCTRL_PIN(DNAND_D2, "dnand_d2"),
	PINCTRL_PIN(DNAND_D3, "dnand_d3"),
	PINCTRL_PIN(DNAND_D4, "dnand_d4"),
	PINCTRL_PIN(DNAND_D5, "dnand_d5"),
	PINCTRL_PIN(DNAND_D6, "dnand_d6"),
	PINCTRL_PIN(DNAND_D7, "dnand_d7"),
	PINCTRL_PIN(DNAND_WRB, "dnand_wrb"),
	PINCTRL_PIN(DNAND_RDB, "dnand_rdb"),
	PINCTRL_PIN(DNAND_RDBN, "dnand_rdbn"),
	PINCTRL_PIN(DNAND_DQS, "dnand_dqs"),
	PINCTRL_PIN(DNAND_DQSN, "dnand_dqsn"),
	PINCTRL_PIN(DNAND_RB0, "dnand_rb0"),
	PINCTRL_PIN(DNAND_ALE, "dnand_ale"),
	PINCTRL_PIN(DNAND_CLE, "dnand_cle"),
	PINCTRL_PIN(DNAND_CEB0, "dnand_ceb0"),
	PINCTRL_PIN(DNAND_CEB1, "dnand_ceb1"),
	PINCTRL_PIN(DNAND_CEB2, "dnand_ceb2"),
	PINCTRL_PIN(DNAND_CEB3, "dnand_ceb3"),
	PINCTRL_PIN(PORB, "porb"),
	PINCTRL_PIN(CLKO_25M, "clko_25m"),
	PINCTRL_PIN(BSEL, "bsel"),
	PINCTRL_PIN(PKG0, "pkg0"),
	PINCTRL_PIN(PKG1, "pkg1"),
	PINCTRL_PIN(PKG2, "pkg2"),
	PINCTRL_PIN(PKG3, "pkg3"),
};

enum s700_pinmux_functions {
	S700_MUX_NOR,
	S700_MUX_ETH_RGMII,
	S700_MUX_ETH_SGMII,
	S700_MUX_SPI0,
	S700_MUX_SPI1,
	S700_MUX_SPI2,
	S700_MUX_SPI3,
	S700_MUX_SENS0,
	S700_MUX_SENS1,
	S700_MUX_UART0,
	S700_MUX_UART1,
	S700_MUX_UART2,
	S700_MUX_UART3,
	S700_MUX_UART4,
	S700_MUX_UART5,
	S700_MUX_UART6,
	S700_MUX_I2S0,
	S700_MUX_I2S1,
	S700_MUX_PCM1,
	S700_MUX_PCM0,
	S700_MUX_KS,
	S700_MUX_JTAG,
	S700_MUX_PWM0,
	S700_MUX_PWM1,
	S700_MUX_PWM2,
	S700_MUX_PWM3,
	S700_MUX_PWM4,
	S700_MUX_PWM5,
	S700_MUX_P0,
	S700_MUX_SD0,
	S700_MUX_SD1,
	S700_MUX_SD2,
	S700_MUX_I2C0,
	S700_MUX_I2C1,
	S700_MUX_I2C2,
	S700_MUX_I2C3,
	S700_MUX_DSI,
	S700_MUX_LVDS,
	S700_MUX_USB30,
	S700_MUX_CLKO_25M,
	S700_MUX_MIPI_CSI,
	S700_MUX_NAND,
	S700_MUX_SPDIF,
	S700_MUX_SIRQ0,
	S700_MUX_SIRQ1,
	S700_MUX_SIRQ2,
	S700_MUX_BT,
	S700_MUX_LCD0,
	S700_MUX_RESERVED,
};

/* mfp0_31_30 reserved */

/* rgmii_txd23 */
static unsigned int  rgmii_txd23_mfp_pads[]		= { ETH_TXD2, ETH_TXD3};
static unsigned int  rgmii_txd23_mfp_funcs[]		= { S700_MUX_ETH_RGMII,
							    S700_MUX_I2C1,
							    S700_MUX_UART3 };
/* rgmii_rxd2 */
static unsigned int  rgmii_rxd2_mfp_pads[]		= { ETH_RXD2 };
static unsigned int  rgmii_rxd2_mfp_funcs[]		= { S700_MUX_ETH_RGMII,
							    S700_MUX_PWM0,
							    S700_MUX_UART3 };
/* rgmii_rxd3 */
static unsigned int  rgmii_rxd3_mfp_pads[]		= { ETH_RXD3};
static unsigned int  rgmii_rxd3_mfp_funcs[]		= { S700_MUX_ETH_RGMII,
							    S700_MUX_PWM2,
							    S700_MUX_UART3 };
/* lcd0_d18 */
static unsigned int  lcd0_d18_mfp_pads[]		= { LCD0_D18 };
static unsigned int  lcd0_d18_mfp_funcs[]		= { S700_MUX_NOR,
							    S700_MUX_SENS1,
							    S700_MUX_PWM2,
							    S700_MUX_PWM4,
							    S700_MUX_LCD0 };
/* rgmii_txd01 */
static unsigned int  rgmii_txd01_mfp_pads[]		= { ETH_CRS_DV };
static unsigned int  rgmii_txd01_mfp_funcs[]		= { S700_MUX_ETH_RGMII,
							    S700_MUX_RESERVED,
							    S700_MUX_SPI2,
							    S700_MUX_UART4,
							    S700_MUX_PWM4 };
/* rgmii_txd0 */
static unsigned int  rgmii_txd0_mfp_pads[]		= { ETH_TXD0 };
static unsigned int  rgmii_txd0_mfp_funcs[]		= { S700_MUX_ETH_RGMII,
							    S700_MUX_ETH_SGMII,
							    S700_MUX_SPI2,
							    S700_MUX_UART6,
							    S700_MUX_PWM4 };
/* rgmii_txd1 */
static unsigned int  rgmii_txd1_mfp_pads[]		= { ETH_TXD1 };
static unsigned int  rgmii_txd1_mfp_funcs[]		= { S700_MUX_ETH_RGMII,
							    S700_MUX_ETH_SGMII,
							    S700_MUX_SPI2,
							    S700_MUX_UART6,
							    S700_MUX_PWM5 };
/* rgmii_txen */
static unsigned int  rgmii_txen_mfp_pads[]		= { ETH_TXEN };
static unsigned int  rgmii_txen_mfp_funcs[]		= { S700_MUX_ETH_RGMII,
							    S700_MUX_UART2,
							    S700_MUX_SPI3,
							    S700_MUX_PWM0 };
/* rgmii_rxen */
static unsigned int  rgmii_rxen_mfp_pads[]		= { ETH_RXER };
static unsigned int  rgmii_rxen_mfp_funcs[]		= { S700_MUX_ETH_RGMII,
							    S700_MUX_UART2,
							    S700_MUX_SPI3,
							    S700_MUX_PWM1 };
/* mfp0_12_11 reserved */
/* rgmii_rxd1*/
static unsigned int  rgmii_rxd1_mfp_pads[]		= { ETH_RXD1 };
static unsigned int  rgmii_rxd1_mfp_funcs[]		= { S700_MUX_ETH_RGMII,
							    S700_MUX_UART2,
							    S700_MUX_SPI3,
							    S700_MUX_PWM2,
							    S700_MUX_UART5,
							    S700_MUX_ETH_SGMII };
/* rgmii_rxd0 */
static unsigned int  rgmii_rxd0_mfp_pads[]		= { ETH_RXD0 };
static unsigned int  rgmii_rxd0_mfp_funcs[]		= { S700_MUX_ETH_RGMII,
							    S700_MUX_UART2,
							    S700_MUX_SPI3,
							    S700_MUX_PWM3,
							    S700_MUX_UART5,
							    S700_MUX_ETH_SGMII };
/* rgmii_ref_clk */
static unsigned int  rgmii_ref_clk_mfp_pads[]		= { ETH_REF_CLK };
static unsigned int  rgmii_ref_clk_mfp_funcs[]		= { S700_MUX_ETH_RGMII,
							    S700_MUX_UART4,
							    S700_MUX_SPI2,
							    S700_MUX_RESERVED,
							    S700_MUX_ETH_SGMII };
/* i2s_d0 */
static unsigned int  i2s_d0_mfp_pads[]			= { I2S_D0 };
static unsigned int  i2s_d0_mfp_funcs[]			= { S700_MUX_I2S0,
							    S700_MUX_NOR };
/* i2s_pcm1 */
static unsigned int  i2s_pcm1_mfp_pads[]		= { I2S_LRCLK0,
							    I2S_MCLK0 };
static unsigned int  i2s_pcm1_mfp_funcs[]		= { S700_MUX_I2S0,
							    S700_MUX_NOR,
							    S700_MUX_PCM1,
							    S700_MUX_BT };
/* i2s0_pcm0 */
static unsigned int  i2s0_pcm0_mfp_pads[]		= { I2S_BCLK0 };
static unsigned int  i2s0_pcm0_mfp_funcs[]		= { S700_MUX_I2S0,
							    S700_MUX_NOR,
							    S700_MUX_PCM0,
							    S700_MUX_BT };
/* i2s1_pcm0 */
static unsigned int  i2s1_pcm0_mfp_pads[]		= { I2S_BCLK1,
							    I2S_LRCLK1,
							    I2S_MCLK1 };

static unsigned int  i2s1_pcm0_mfp_funcs[]		= { S700_MUX_I2S1,
							    S700_MUX_NOR,
							    S700_MUX_PCM0,
							    S700_MUX_BT };
/* i2s_d1 */
static unsigned int  i2s_d1_mfp_pads[]			= { I2S_D1 };
static unsigned int  i2s_d1_mfp_funcs[]			= { S700_MUX_I2S1,
							    S700_MUX_NOR };
/* ks_in2 */
static unsigned int  ks_in2_mfp_pads[]			= { KS_IN2 };
static unsigned int  ks_in2_mfp_funcs[]			= { S700_MUX_KS,
							    S700_MUX_JTAG,
							    S700_MUX_NOR,
							    S700_MUX_BT,
							    S700_MUX_PWM0,
							    S700_MUX_SENS1,
							    S700_MUX_PWM0,
							    S700_MUX_P0 };
/* ks_in1 */
static unsigned int  ks_in1_mfp_pads[]			= { KS_IN1 };
static unsigned int  ks_in1_mfp_funcs[]			= { S700_MUX_KS,
							    S700_MUX_JTAG,
							    S700_MUX_NOR,
							    S700_MUX_BT,
							    S700_MUX_PWM5,
							    S700_MUX_SENS1,
							    S700_MUX_PWM1,
							    S700_MUX_USB30 };
/* ks_in0 */
static unsigned int  ks_in0_mfp_pads[]			= { KS_IN0 };
static unsigned int  ks_in0_mfp_funcs[]			= { S700_MUX_KS,
							    S700_MUX_JTAG,
							    S700_MUX_NOR,
							    S700_MUX_BT,
							    S700_MUX_PWM4,
							    S700_MUX_SENS1,
							    S700_MUX_PWM4,
							    S700_MUX_P0 };
/* ks_in3 */
static unsigned int  ks_in3_mfp_pads[]			= { KS_IN3 };
static unsigned int  ks_in3_mfp_funcs[]			= { S700_MUX_KS,
							    S700_MUX_JTAG,
							    S700_MUX_NOR,
							    S700_MUX_PWM1,
							    S700_MUX_BT,
							    S700_MUX_SENS1 };
/* ks_out0 */
static unsigned int  ks_out0_mfp_pads[]			= { KS_OUT0 };
static unsigned int  ks_out0_mfp_funcs[]		= { S700_MUX_KS,
							    S700_MUX_UART5,
							    S700_MUX_NOR,
							    S700_MUX_PWM2,
							    S700_MUX_BT,
							    S700_MUX_SENS1,
							    S700_MUX_SD0,
							    S700_MUX_UART4 };

/* ks_out1 */
static unsigned int  ks_out1_mfp_pads[]			= { KS_OUT1 };
static unsigned int  ks_out1_mfp_funcs[]		= { S700_MUX_KS,
							    S700_MUX_JTAG,
							    S700_MUX_NOR,
							    S700_MUX_PWM3,
							    S700_MUX_BT,
							    S700_MUX_SENS1,
							    S700_MUX_SD0,
							    S700_MUX_UART4 };
/* ks_out2 */
static unsigned int  ks_out2_mfp_pads[]			= { KS_OUT2 };
static unsigned int  ks_out2_mfp_funcs[]		= { S700_MUX_SD0,
							    S700_MUX_KS,
							    S700_MUX_NOR,
							    S700_MUX_PWM2,
							    S700_MUX_UART5,
							    S700_MUX_SENS1,
							    S700_MUX_BT };
/* lvds_o_pn */
static unsigned int  lvds_o_pn_mfp_pads[]		= { LVDS_OEP,
							    LVDS_OEN,
							    LVDS_ODP,
							    LVDS_ODN,
							    LVDS_OCP,
							    LVDS_OCN,
							    LVDS_OBP,
							    LVDS_OBN,
							    LVDS_OAP,
							    LVDS_OAN };

static unsigned int  lvds_o_pn_mfp_funcs[]		= { S700_MUX_LVDS,
							    S700_MUX_BT,
							    S700_MUX_LCD0 };

/* dsi_dn0 */
static unsigned int  dsi_dn0_mfp_pads[]			= { DSI_DN0 };
static unsigned int  dsi_dn0_mfp_funcs[]		= { S700_MUX_DSI,
							    S700_MUX_UART2,
							    S700_MUX_SPI0 };
/* dsi_dp2 */
static unsigned int  dsi_dp2_mfp_pads[]			= { DSI_DP2 };
static unsigned int  dsi_dp2_mfp_funcs[]		= { S700_MUX_DSI,
							    S700_MUX_UART2,
							    S700_MUX_SPI0,
							    S700_MUX_SD1 };
/* lcd0_d2 */
static unsigned int  lcd0_d2_mfp_pads[]			= { LCD0_D2 };
static unsigned int  lcd0_d2_mfp_funcs[]		= { S700_MUX_NOR,
							    S700_MUX_SD0,
							    S700_MUX_RESERVED,
							    S700_MUX_PWM3,
							    S700_MUX_LCD0 };
/* dsi_dp3 */
static unsigned int  dsi_dp3_mfp_pads[]			= { DSI_DP3 };
static unsigned int  dsi_dp3_mfp_funcs[]		= { S700_MUX_DSI,
							    S700_MUX_SD0,
							    S700_MUX_SD1,
							    S700_MUX_LCD0 };
/* dsi_dn3 */
static unsigned int  dsi_dn3_mfp_pads[]			= { DSI_DN3 };
static unsigned int  dsi_dn3_mfp_funcs[]		= { S700_MUX_DSI,
							    S700_MUX_SD0,
							    S700_MUX_SD1,
							    S700_MUX_LCD0 };
/* dsi_dp0 */
static unsigned int  dsi_dp0_mfp_pads[]			= { DSI_DP0 };
static unsigned int  dsi_dp0_mfp_funcs[]		= { S700_MUX_DSI,
							    S700_MUX_RESERVED,
							    S700_MUX_SD0,
							    S700_MUX_UART2,
							    S700_MUX_SPI0 };
/* lvds_ee_pn */
static unsigned int  lvds_ee_pn_mfp_pads[]		= { LVDS_EEP,
							    LVDS_EEN };
static unsigned int  lvds_ee_pn_mfp_funcs[]		= { S700_MUX_LVDS,
							    S700_MUX_NOR,
							    S700_MUX_BT,
							    S700_MUX_LCD0 };
/* uart2_rx_tx */
static unsigned int  uart2_rx_tx_mfp_pads[]		= { UART2_RX,
							    UART2_TX };
static unsigned int  uart2_rx_tx_mfp_funcs[]		= { S700_MUX_UART2,
							    S700_MUX_NOR,
							    S700_MUX_SPI0,
							    S700_MUX_PCM0 };
/* spi0_i2c_pcm */
static unsigned int  spi0_i2c_pcm_mfp_pads[]		= { SPI0_SS,
							    SPI0_MISO };
static unsigned int  spi0_i2c_pcm_mfp_funcs[]		= { S700_MUX_SPI0,
							    S700_MUX_NOR,
							    S700_MUX_I2S1,
							    S700_MUX_PCM1,
							    S700_MUX_PCM0,
							    S700_MUX_I2C2 };
/* mfp2_31 reserved */

/* dsi_dnp1_cp_d2 */
static unsigned int  dsi_dnp1_cp_d2_mfp_pads[]		= { DSI_DP1,
							    DSI_CP,
							    DSI_CN };
static unsigned int  dsi_dnp1_cp_d2_mfp_funcs[]		= { S700_MUX_DSI,
							    S700_MUX_LCD0,
							    S700_MUX_RESERVED };
/* dsi_dnp1_cp_d17 */
static unsigned int  dsi_dnp1_cp_d17_mfp_pads[]		= { DSI_DP1,
							    DSI_CP,
							    DSI_CN };

static unsigned int  dsi_dnp1_cp_d17_mfp_funcs[]	= { S700_MUX_DSI,
							    S700_MUX_RESERVED,
							    S700_MUX_LCD0 };
/* lvds_e_pn */
static unsigned int  lvds_e_pn_mfp_pads[]		= { LVDS_EDP,
							    LVDS_EDN,
							    LVDS_ECP,
							    LVDS_ECN,
							    LVDS_EBP,
							    LVDS_EBN,
							    LVDS_EAP,
							    LVDS_EAN };

static unsigned int  lvds_e_pn_mfp_funcs[]		= { S700_MUX_LVDS,
							    S700_MUX_NOR,
							    S700_MUX_LCD0 };
/* dsi_dn2 */
static unsigned int  dsi_dn2_mfp_pads[]			= { DSI_DN2 };
static unsigned int  dsi_dn2_mfp_funcs[]		= { S700_MUX_DSI,
							    S700_MUX_RESERVED,
							    S700_MUX_SD1,
							    S700_MUX_UART2,
							    S700_MUX_SPI0 };
/* uart2_rtsb */
static unsigned int  uart2_rtsb_mfp_pads[]		= { UART2_RTSB };
static unsigned int  uart2_rtsb_mfp_funcs[]		= { S700_MUX_UART2,
							    S700_MUX_UART0 };

/* uart2_ctsb */
static unsigned int  uart2_ctsb_mfp_pads[]		= { UART2_CTSB };
static unsigned int  uart2_ctsb_mfp_funcs[]		= { S700_MUX_UART2,
							    S700_MUX_UART0 };
/* uart3_rtsb */
static unsigned int  uart3_rtsb_mfp_pads[]		= { UART3_RTSB };
static unsigned int  uart3_rtsb_mfp_funcs[]		= { S700_MUX_UART3,
							    S700_MUX_UART5 };

/* uart3_ctsb */
static unsigned int  uart3_ctsb_mfp_pads[]		= { UART3_CTSB };
static unsigned int  uart3_ctsb_mfp_funcs[]		= { S700_MUX_UART3,
							    S700_MUX_UART5 };
/* sd0_d0 */
static unsigned int  sd0_d0_mfp_pads[]			= { SD0_D0 };
static unsigned int  sd0_d0_mfp_funcs[]			= { S700_MUX_SD0,
							    S700_MUX_NOR,
							    S700_MUX_RESERVED,
							    S700_MUX_JTAG,
							    S700_MUX_UART2,
							    S700_MUX_UART5 };
/* sd0_d1 */
static unsigned int  sd0_d1_mfp_pads[]			= { SD0_D1 };
static unsigned int  sd0_d1_mfp_funcs[]			= { S700_MUX_SD0,
							    S700_MUX_NOR,
							    S700_MUX_RESERVED,
							    S700_MUX_RESERVED,
							    S700_MUX_UART2,
							    S700_MUX_UART5 };
/* sd0_d2_d3 */
static unsigned int  sd0_d2_d3_mfp_pads[]		= { SD0_D2,
							    SD0_D3 };
static unsigned int  sd0_d2_d3_mfp_funcs[]		= { S700_MUX_SD0,
							    S700_MUX_NOR,
							    S700_MUX_RESERVED,
							    S700_MUX_JTAG,
							    S700_MUX_UART2,
							    S700_MUX_UART1 };

/* sd1_d0_d3 */
static unsigned int  sd1_d0_d3_mfp_pads[]		= { SD1_D0,
							    SD1_D1,
							    SD1_D2,
							    SD1_D3 };
static unsigned int  sd1_d0_d3_mfp_funcs[]		= { S700_MUX_SD0,
							    S700_MUX_NOR,
							    S700_MUX_RESERVED,
							    S700_MUX_SD1 };

/* sd0_cmd */
static unsigned int  sd0_cmd_mfp_pads[]			= { SD0_CMD };
static unsigned int  sd0_cmd_mfp_funcs[]		= { S700_MUX_SD0,
							    S700_MUX_NOR,
							    S700_MUX_RESERVED,
							    S700_MUX_JTAG };
/* sd0_clk */
static unsigned int  sd0_clk_mfp_pads[]			= { SD0_CLK };
static unsigned int  sd0_clk_mfp_funcs[]		= { S700_MUX_SD0,
							    S700_MUX_RESERVED,
							    S700_MUX_JTAG };
/* sd1_cmd */
static unsigned int  sd1_cmd_mfp_pads[]			= { SD1_CMD };
static unsigned int  sd1_cmd_mfp_funcs[]		= { S700_MUX_SD1,
							    S700_MUX_NOR };
/* uart0_rx */
static unsigned int  uart0_rx_mfp_pads[]		= { UART0_RX };
static unsigned int  uart0_rx_mfp_funcs[]		= { S700_MUX_UART0,
							    S700_MUX_UART2,
							    S700_MUX_SPI1,
							    S700_MUX_I2C0,
							    S700_MUX_PCM1,
							    S700_MUX_I2S1 };
/* dnand_data_wr1 reserved */

/* clko_25m */
static unsigned int  clko_25m_mfp_pads[]		= { CLKO_25M };
static unsigned int  clko_25m_mfp_funcs[]		= { S700_MUX_RESERVED,
							    S700_MUX_CLKO_25M };
/* csi_cn_cp */
static unsigned int  csi_cn_cp_mfp_pads[]		= { CSI_CN,
							    CSI_CP };
static unsigned int  csi_cn_cp_mfp_funcs[]		= { S700_MUX_MIPI_CSI,
							    S700_MUX_SENS0 };
/* dnand_acle_ce07_24 reserved */

/* sens0_ckout */
static unsigned int  sens0_ckout_mfp_pads[]		= { SENSOR0_CKOUT };
static unsigned int  sens0_ckout_mfp_funcs[]		= { S700_MUX_SENS0,
							    S700_MUX_NOR,
							    S700_MUX_SENS1,
							    S700_MUX_PWM1 };
/* uart0_tx */
static unsigned int  uart0_tx_mfp_pads[]		= { UART0_TX };
static unsigned int  uart0_tx_mfp_funcs[]		= { S700_MUX_UART0,
							    S700_MUX_UART2,
							    S700_MUX_SPI1,
							    S700_MUX_I2C0,
							    S700_MUX_SPDIF,
							    S700_MUX_PCM1,
							    S700_MUX_I2S1 };
/* i2c0_mfp */
static unsigned int  i2c0_mfp_pads[]		= { I2C0_SCLK,
							    I2C0_SDATA };
static unsigned int  i2c0_mfp_funcs[]		= { S700_MUX_I2C0,
							    S700_MUX_UART2,
							    S700_MUX_I2C1,
							    S700_MUX_UART1,
							    S700_MUX_SPI1 };
/* csi_dn_dp */
static unsigned int  csi_dn_dp_mfp_pads[]		= { CSI_DN0,
							    CSI_DN1,
							    CSI_DN2,
							    CSI_DN3,
							    CSI_DP0,
							    CSI_DP1,
							    CSI_DP2,
							    CSI_DP3 };
static unsigned int  csi_dn_dp_mfp_funcs[]		= { S700_MUX_MIPI_CSI,
							    S700_MUX_SENS0 };
/* sen0_pclk */
static unsigned int  sen0_pclk_mfp_pads[]		= { SENSOR0_PCLK };
static unsigned int  sen0_pclk_mfp_funcs[]		= { S700_MUX_SENS0,
							    S700_MUX_NOR,
							    S700_MUX_PWM0 };
/* pcm1_in */
static unsigned int  pcm1_in_mfp_pads[]			= { PCM1_IN };
static unsigned int  pcm1_in_mfp_funcs[]		= { S700_MUX_PCM1,
							    S700_MUX_SENS1,
							    S700_MUX_BT,
							    S700_MUX_PWM4 };
/* pcm1_clk */
static unsigned int  pcm1_clk_mfp_pads[]		= { PCM1_CLK };
static unsigned int  pcm1_clk_mfp_funcs[]		= { S700_MUX_PCM1,
							    S700_MUX_SENS1,
							    S700_MUX_BT,
							    S700_MUX_PWM5 };
/* pcm1_sync */
static unsigned int  pcm1_sync_mfp_pads[]		= { PCM1_SYNC };
static unsigned int  pcm1_sync_mfp_funcs[]		= { S700_MUX_PCM1,
							    S700_MUX_SENS1,
							    S700_MUX_BT,
							    S700_MUX_I2C3 };
/* pcm1_out */
static unsigned int  pcm1_out_mfp_pads[]		= { PCM1_OUT };
static unsigned int  pcm1_out_mfp_funcs[]		= { S700_MUX_PCM1,
							    S700_MUX_SENS1,
							    S700_MUX_BT,
							    S700_MUX_I2C3 };
/* dnand_data_wr */
static unsigned int  dnand_data_wr_mfp_pads[]		= { DNAND_D0,
							    DNAND_D1,
							    DNAND_D2,
							    DNAND_D3,
							    DNAND_D4,
							    DNAND_D5,
							    DNAND_D6,
							    DNAND_D7,
							    DNAND_RDB,
							    DNAND_RDBN };
static unsigned int  dnand_data_wr_mfp_funcs[]		= { S700_MUX_NAND,
							    S700_MUX_SD2 };
/* dnand_acle_ce0 */
static unsigned int  dnand_acle_ce0_mfp_pads[]		= { DNAND_ALE,
							    DNAND_CLE,
							    DNAND_CEB0,
							    DNAND_CEB1 };
static unsigned int  dnand_acle_ce0_mfp_funcs[]		= { S700_MUX_NAND,
							    S700_MUX_SPI2 };

/* nand_ceb2 */
static unsigned int  nand_ceb2_mfp_pads[]		= { DNAND_CEB2 };
static unsigned int  nand_ceb2_mfp_funcs[]		= { S700_MUX_NAND,
							    S700_MUX_PWM5 };
/* nand_ceb3 */
static unsigned int  nand_ceb3_mfp_pads[]		= { DNAND_CEB3 };
static unsigned int  nand_ceb3_mfp_funcs[]		= { S700_MUX_NAND,
							    S700_MUX_PWM4 };
/*****End MFP group data****************************/

/*****PADDRV group data****************************/

/*PAD_DRV0*/
static unsigned int  sirq_drv_pads[]			= { SIRQ0,
							    SIRQ1,
							    SIRQ2 };

static unsigned int  rgmii_txd23_drv_pads[]		= { ETH_TXD2,
							    ETH_TXD3 };

static unsigned int  rgmii_rxd23_drv_pads[]		= { ETH_RXD2,
							    ETH_RXD3 };

static unsigned int  rgmii_txd01_txen_drv_pads[]	= { ETH_TXD0,
							    ETH_TXD1,
							    ETH_TXEN };

static unsigned int  rgmii_rxer_drv_pads[]		= { ETH_RXER };

static unsigned int  rgmii_crs_drv_pads[]		= { ETH_CRS_DV };

static unsigned int  rgmii_rxd10_drv_pads[]		= { ETH_RXD0,
							    ETH_RXD1 };

static unsigned int  rgmii_ref_clk_drv_pads[]		= { ETH_REF_CLK };

static unsigned int  smi_mdc_mdio_drv_pads[]		= { ETH_MDC,
							    ETH_MDIO };

static unsigned int  i2s_d0_drv_pads[]			= { I2S_D0 };

static unsigned int  i2s_bclk0_drv_pads[]		= { I2S_BCLK0 };

static unsigned int  i2s3_drv_pads[]			= { I2S_LRCLK0,
							    I2S_MCLK0,
							    I2S_D1 };

static unsigned int  i2s13_drv_pads[]			= { I2S_BCLK1,
							    I2S_LRCLK1,
							    I2S_MCLK1 };

static unsigned int  pcm1_drv_pads[]			= { PCM1_IN,
							    PCM1_CLK,
							    PCM1_SYNC,
							    PCM1_OUT };

static unsigned int  ks_in_drv_pads[]			= { KS_IN0,
							    KS_IN1,
							    KS_IN2,
							    KS_IN3 };

/*PAD_DRV1*/
static unsigned int  ks_out_drv_pads[]			= { KS_OUT0,
							    KS_OUT1,
							    KS_OUT2 };

static unsigned int  lvds_all_drv_pads[]		= { LVDS_OEP,
							    LVDS_OEN,
							    LVDS_ODP,
							    LVDS_ODN,
							    LVDS_OCP,
							    LVDS_OCN,
							    LVDS_OBP,
							    LVDS_OBN,
							    LVDS_OAP,
							    LVDS_OAN,
							    LVDS_EEP,
							    LVDS_EEN,
							    LVDS_EDP,
							    LVDS_EDN,
							    LVDS_ECP,
							    LVDS_ECN,
							    LVDS_EBP,
							    LVDS_EBN,
							    LVDS_EAP,
							    LVDS_EAN };

static unsigned int  lcd_d18_d2_drv_pads[]		= { LCD0_D18,
							    LCD0_D2 };

static unsigned int  dsi_all_drv_pads[]			= { DSI_DP0,
							    DSI_DN0,
							    DSI_DP2,
							    DSI_DN2,
							    DSI_DP3,
							    DSI_DN3,
							    DSI_DP1,
							    DSI_DN1,
							    DSI_CP,
							    DSI_CN };

static unsigned int  sd0_d0_d3_drv_pads[]		= { SD0_D0,
							    SD0_D1,
							    SD0_D2,
							    SD0_D3 };

static unsigned int  sd0_cmd_drv_pads[]			= { SD0_CMD };

static unsigned int  sd0_clk_drv_pads[]			= { SD0_CLK };

static unsigned int  spi0_all_drv_pads[]		= { SPI0_SS,
							    SPI0_MISO };

/*PAD_DRV2*/
static unsigned int  uart0_rx_drv_pads[]		= { UART0_RX };

static unsigned int  uart0_tx_drv_pads[]		= { UART0_TX };

static unsigned int  uart2_all_drv_pads[]		= { UART2_RX,
							    UART2_TX,
							    UART2_RTSB,
							    UART2_CTSB };

static unsigned int  i2c0_all_drv_pads[]		= { I2C0_SCLK,
							    I2C0_SDATA };

static unsigned int  i2c12_all_drv_pads[]		= { I2C1_SCLK,
							    I2C1_SDATA,
							    I2C2_SCLK,
							    I2C2_SDATA };

static unsigned int  sens0_pclk_drv_pads[]		= { SENSOR0_PCLK };

static unsigned int  sens0_ckout_drv_pads[]		= { SENSOR0_CKOUT };

static unsigned int  uart3_all_drv_pads[]		= { UART3_RX,
							    UART3_TX,
							    UART3_RTSB,
							    UART3_CTSB };

/* all pinctrl groups of S700 board */
static const struct owl_pingroup s700_groups[] = {
	MUX_PG(rgmii_txd23_mfp, 0, 28, 2),
	MUX_PG(rgmii_rxd2_mfp, 0, 26, 2),
	MUX_PG(rgmii_rxd3_mfp, 0, 26, 2),
	MUX_PG(lcd0_d18_mfp, 0, 23, 3),
	MUX_PG(rgmii_txd01_mfp, 0, 20, 3),
	MUX_PG(rgmii_txd0_mfp, 0, 16, 3),
	MUX_PG(rgmii_txd1_mfp, 0, 16, 3),
	MUX_PG(rgmii_txen_mfp, 0, 13, 3),
	MUX_PG(rgmii_rxen_mfp, 0, 13, 3),
	MUX_PG(rgmii_rxd1_mfp, 0, 8, 3),
	MUX_PG(rgmii_rxd0_mfp, 0, 8, 3),
	MUX_PG(rgmii_ref_clk_mfp, 0, 6, 2),
	MUX_PG(i2s_d0_mfp, 0, 5, 1),
	MUX_PG(i2s_pcm1_mfp, 0, 3, 2),
	MUX_PG(i2s0_pcm0_mfp, 0, 1, 2),
	MUX_PG(i2s1_pcm0_mfp, 0, 1, 2),
	MUX_PG(i2s_d1_mfp, 0, 0, 1),
	MUX_PG(ks_in2_mfp, 1, 29, 3),
	MUX_PG(ks_in1_mfp, 1, 29, 3),
	MUX_PG(ks_in0_mfp, 1, 29, 3),
	MUX_PG(ks_in3_mfp, 1, 26, 3),
	MUX_PG(ks_out0_mfp, 1, 26, 3),
	MUX_PG(ks_out1_mfp, 1, 26, 3),
	MUX_PG(ks_out2_mfp, 1, 23, 3),
	MUX_PG(lvds_o_pn_mfp, 1, 21, 2),
	MUX_PG(dsi_dn0_mfp, 1, 19, 2),
	MUX_PG(dsi_dp2_mfp, 1, 17, 2),
	MUX_PG(lcd0_d2_mfp, 1, 14, 3),
	MUX_PG(dsi_dp3_mfp, 1, 12, 2),
	MUX_PG(dsi_dn3_mfp, 1, 10, 2),
	MUX_PG(dsi_dp0_mfp, 1, 7, 3),
	MUX_PG(lvds_ee_pn_mfp, 1, 5, 2),
	MUX_PG(uart2_rx_tx_mfp, 1, 3, 2),
	MUX_PG(spi0_i2c_pcm_mfp, 1, 0, 3),
	MUX_PG(dsi_dnp1_cp_d2_mfp, 2, 29, 2),
	MUX_PG(dsi_dnp1_cp_d17_mfp, 2, 29, 2),
	MUX_PG(lvds_e_pn_mfp, 2, 27, 2),
	MUX_PG(dsi_dn2_mfp, 2, 24, 3),
	MUX_PG(uart2_rtsb_mfp, 2, 23, 1),
	MUX_PG(uart2_ctsb_mfp, 2, 22, 1),
	MUX_PG(uart3_rtsb_mfp, 2, 21, 1),
	MUX_PG(uart3_ctsb_mfp, 2, 20, 1),
	MUX_PG(sd0_d0_mfp, 2, 17, 3),
	MUX_PG(sd0_d1_mfp, 2, 14, 3),
	MUX_PG(sd0_d2_d3_mfp, 2, 11, 3),
	MUX_PG(sd1_d0_d3_mfp, 2, 9, 2),
	MUX_PG(sd0_cmd_mfp, 2, 7, 2),
	MUX_PG(sd0_clk_mfp, 2, 5, 2),
	MUX_PG(sd1_cmd_mfp, 2, 3, 2),
	MUX_PG(uart0_rx_mfp, 2, 0, 3),
	MUX_PG(clko_25m_mfp, 3, 30, 1),
	MUX_PG(csi_cn_cp_mfp, 3, 28, 2),
	MUX_PG(sens0_ckout_mfp, 3, 22, 2),
	MUX_PG(uart0_tx_mfp, 3, 19, 3),
	MUX_PG(i2c0_mfp, 3, 16, 3),
	MUX_PG(csi_dn_dp_mfp, 3, 14, 2),
	MUX_PG(sen0_pclk_mfp, 3, 12, 2),
	MUX_PG(pcm1_in_mfp, 3, 10, 2),
	MUX_PG(pcm1_clk_mfp, 3, 8, 2),
	MUX_PG(pcm1_sync_mfp, 3, 6, 2),
	MUX_PG(pcm1_out_mfp, 3, 4, 2),
	MUX_PG(dnand_data_wr_mfp, 3, 3, 1),
	MUX_PG(dnand_acle_ce0_mfp, 3, 2, 1),
	MUX_PG(nand_ceb2_mfp, 3, 0, 2),
	MUX_PG(nand_ceb3_mfp, 3, 0, 2),

	DRV_PG(sirq_drv, 0, 28, 2),
	DRV_PG(rgmii_txd23_drv, 0, 26, 2),
	DRV_PG(rgmii_rxd23_drv, 0, 24, 2),
	DRV_PG(rgmii_txd01_txen_drv, 0, 22, 2),
	DRV_PG(rgmii_rxer_drv, 0, 20, 2),
	DRV_PG(rgmii_crs_drv, 0, 18, 2),
	DRV_PG(rgmii_rxd10_drv, 0, 16, 2),
	DRV_PG(rgmii_ref_clk_drv, 0, 14, 2),
	DRV_PG(smi_mdc_mdio_drv, 0, 12, 2),
	DRV_PG(i2s_d0_drv, 0, 10, 2),
	DRV_PG(i2s_bclk0_drv, 0, 8, 2),
	DRV_PG(i2s3_drv, 0, 6, 2),
	DRV_PG(i2s13_drv, 0, 4, 2),
	DRV_PG(pcm1_drv, 0, 2, 2),
	DRV_PG(ks_in_drv, 0, 0, 2),
	DRV_PG(ks_out_drv, 1, 30, 2),
	DRV_PG(lvds_all_drv, 1, 28, 2),
	DRV_PG(lcd_d18_d2_drv, 1, 26, 2),
	DRV_PG(dsi_all_drv, 1, 24, 2),
	DRV_PG(sd0_d0_d3_drv, 1, 22, 2),
	DRV_PG(sd0_cmd_drv, 1, 18, 2),
	DRV_PG(sd0_clk_drv, 1, 16, 2),
	DRV_PG(spi0_all_drv, 1, 10, 2),
	DRV_PG(uart0_rx_drv, 2, 30, 2),
	DRV_PG(uart0_tx_drv, 2, 28, 2),
	DRV_PG(uart2_all_drv, 2, 26, 2),
	DRV_PG(i2c0_all_drv, 2, 23, 2),
	DRV_PG(i2c12_all_drv, 2, 21, 2),
	DRV_PG(sens0_pclk_drv, 2, 18, 2),
	DRV_PG(sens0_ckout_drv, 2, 12, 2),
	DRV_PG(uart3_all_drv, 2, 2, 2),
};

static const char * const nor_groups[] = {
	"lcd0_d18",
	"i2s_d0",
	"i2s0_pcm0",
	"i2s1_pcm0",
	"i2s_d1",
	"ks_in2",
	"ks_in1",
	"ks_in0",
	"ks_in3",
	"ks_out0",
	"ks_out1",
	"ks_out2",
	"lcd0_d2",
	"lvds_ee_pn",
	"uart2_rx_tx",
	"spi0_i2c_pcm",
	"lvds_e_pn",
	"sd0_d0",
	"sd0_d1",
	"sd0_d2_d3",
	"sd1_d0_d3",
	"sd0_cmd",
	"sd1_cmd",
	"sens0_ckout",
	"sen0_pclk",
};

static const char * const eth_rmii_groups[] = {
	"rgmii_txd23",
	"rgmii_rxd2",
	"rgmii_rxd3",
	"rgmii_txd01",
	"rgmii_txd0",
	"rgmii_txd1",
	"rgmii_txen",
	"rgmii_rxen",
	"rgmii_rxd1",
	"rgmii_rxd0",
	"rgmii_ref_clk",
	"eth_smi_dummy",
};

static const char * const eth_smii_groups[] = {
	"rgmii_txd0",
	"rgmii_txd1",
	"rgmii_rxd0",
	"rgmii_rxd1",
	"rgmii_ref_clk",
	"eth_smi_dummy",
};

static const char * const spi0_groups[] = {
	"dsi_dn0",
	"dsi_dp2",
	"dsi_dp0",
	"uart2_rx_tx",
	"spi0_i2c_pcm",
	"dsi_dn2",
};

static const char * const spi1_groups[] = {
	"uart0_rx",
	"uart0_tx",
	"i2c0_mfp",
};

static const char * const spi2_groups[] = {
	"rgmii_txd01",
	"rgmii_txd0",
	"rgmii_txd1",
	"rgmii_ref_clk",
	"dnand_acle_ce0",
};

static const char * const spi3_groups[] = {
	"rgmii_txen",
	"rgmii_rxen",
	"rgmii_rxd1",
	"rgmii_rxd0",
};

static const char * const sens0_groups[] = {
	"csi_cn_cp",
	"sens0_ckout",
	"csi_dn_dp",
	"sen0_pclk",
};

static const char * const sens1_groups[] = {
	"lcd0_d18",
	"ks_in2",
	"ks_in1",
	"ks_in0",
	"ks_in3",
	"ks_out0",
	"ks_out1",
	"ks_out2",
	"sens0_ckout",
	"pcm1_in",
	"pcm1_clk",
	"pcm1_sync",
	"pcm1_out",
};

static const char * const uart0_groups[] = {
	"uart2_rtsb",
	"uart2_ctsb",
	"uart0_rx",
	"uart0_tx",
};

static const char * const uart1_groups[] = {
	"sd0_d2_d3",
	"i2c0_mfp",
};

static const char * const uart2_groups[] = {
	"rgmii_txen",
	"rgmii_rxen",
	"rgmii_rxd1",
	"rgmii_rxd0",
	"dsi_dn0",
	"dsi_dp2",
	"dsi_dp0",
	"uart2_rx_tx",
	"dsi_dn2",
	"uart2_rtsb",
	"uart2_ctsb",
	"sd0_d0",
	"sd0_d1",
	"sd0_d2_d3",
	"uart0_rx",
	"uart0_tx",
	"i2c0_mfp",
	"uart2_dummy"
};

static const char * const uart3_groups[] = {
	"rgmii_txd23",
	"rgmii_rxd2",
	"rgmii_rxd3",
	"uart3_rtsb",
	"uart3_ctsb",
	"uart3_dummy"
};

static const char * const uart4_groups[] = {
	"rgmii_txd01",
	"rgmii_ref_clk",
	"ks_out0",
	"ks_out1",
};

static const char * const uart5_groups[] = {
	"rgmii_rxd1",
	"rgmii_rxd0",
	"ks_out0",
	"ks_out2",
	"uart3_rtsb",
	"uart3_ctsb",
	"sd0_d0",
	"sd0_d1",
};

static const char * const uart6_groups[] = {
	"rgmii_txd0",
	"rgmii_txd1",
};

static const char * const i2s0_groups[] = {
	"i2s_d0",
	"i2s_pcm1",
	"i2s0_pcm0",
};

static const char * const i2s1_groups[] = {
	"i2s1_pcm0",
	"i2s_d1",
	"i2s1_dummy",
	"spi0_i2c_pcm",
	"uart0_rx",
	"uart0_tx",
};

static const char * const pcm1_groups[] = {
	"i2s_pcm1",
	"spi0_i2c_pcm",
	"uart0_rx",
	"uart0_tx",
	"pcm1_in",
	"pcm1_clk",
	"pcm1_sync",
	"pcm1_out",
};

static const char * const pcm0_groups[] = {
	"i2s0_pcm0",
	"i2s1_pcm0",
	"uart2_rx_tx",
	"spi0_i2c_pcm",
};

static const char * const ks_groups[] = {
	"ks_in2",
	"ks_in1",
	"ks_in0",
	"ks_in3",
	"ks_out0",
	"ks_out1",
	"ks_out2",
};

static const char * const jtag_groups[] = {
	"ks_in2",
	"ks_in1",
	"ks_in0",
	"ks_in3",
	"ks_out1",
	"sd0_d0",
	"sd0_d2_d3",
	"sd0_cmd",
	"sd0_clk",
};

static const char * const pwm0_groups[] = {
	"rgmii_rxd2",
	"rgmii_txen",
	"ks_in2",
	"sen0_pclk",
};

static const char * const pwm1_groups[] = {
	"rgmii_rxen",
	"ks_in1",
	"ks_in3",
	"sens0_ckout",
};

static const char * const pwm2_groups[] = {
	"lcd0_d18",
	"rgmii_rxd3",
	"rgmii_rxd1",
	"ks_out0",
	"ks_out2",
};

static const char * const pwm3_groups[] = {
	"rgmii_rxd0",
	"ks_out1",
	"lcd0_d2",
};

static const char * const pwm4_groups[] = {
	"lcd0_d18",
	"rgmii_txd01",
	"rgmii_txd0",
	"ks_in0",
	"pcm1_in",
	"nand_ceb3",
};

static const char * const pwm5_groups[] = {
	"rgmii_txd1",
	"ks_in1",
	"pcm1_clk",
	"nand_ceb2",
};

static const char * const p0_groups[] = {
	"ks_in2",
	"ks_in0",
};

static const char * const sd0_groups[] = {
	"ks_out0",
	"ks_out1",
	"ks_out2",
	"lcd0_d2",
	"dsi_dp3",
	"dsi_dp0",
	"sd0_d0",
	"sd0_d1",
	"sd0_d2_d3",
	"sd1_d0_d3",
	"sd0_cmd",
	"sd0_clk",
};

static const char * const sd1_groups[] = {
	"dsi_dp2",
	"mfp1_16_14",
	"lcd0_d2",
	"mfp1_16_14_d17",
	"dsi_dp3",
	"dsi_dn3",
	"dsi_dnp1_cp_d2",
	"dsi_dnp1_cp_d17",
	"dsi_dn2",
	"sd1_d0_d3",
	"sd1_cmd",
	"sd1_dummy",
};

static const char * const sd2_groups[] = {
	"dnand_data_wr",
};

static const char * const i2c0_groups[] = {
	"uart0_rx",
	"uart0_tx",
	"i2c0_mfp",
};

static const char * const i2c1_groups[] = {
	"i2c0_mfp",
	"i2c1_dummy"
};

static const char * const i2c2_groups[] = {
	"i2c2_dummy"
};

static const char * const i2c3_groups[] = {
	"uart2_rx_tx",
	"pcm1_sync",
	"pcm1_out",
};

static const char * const lvds_groups[] = {
	"lvds_o_pn",
	"lvds_ee_pn",
	"lvds_e_pn",
};

static const char * const bt_groups[] = {
	"i2s_pcm1",
	"i2s0_pcm0",
	"i2s1_pcm0",
	"ks_in2",
	"ks_in1",
	"ks_in0",
	"ks_in3",
	"ks_out0",
	"ks_out1",
	"ks_out2",
	"lvds_o_pn",
	"lvds_ee_pn",
	"pcm1_in",
	"pcm1_clk",
	"pcm1_sync",
	"pcm1_out",
};

static const char * const lcd0_groups[] = {
	"lcd0_d18",
	"lcd0_d2",
	"mfp1_16_14_d17",
	"lvds_o_pn",
	"dsi_dp3",
	"dsi_dn3",
	"lvds_ee_pn",
	"dsi_dnp1_cp_d2",
	"dsi_dnp1_cp_d17",
	"lvds_e_pn",
};


static const char * const usb30_groups[] = {
	"ks_in1",
};

static const char * const clko_25m_groups[] = {
	"clko_25m",
};

static const char * const mipi_csi_groups[] = {
	"csi_cn_cp",
	"csi_dn_dp",
};

static const char * const dsi_groups[] = {
	"dsi_dn0",
	"dsi_dp2",
	"dsi_dp3",
	"dsi_dn3",
	"dsi_dp0",
	"dsi_dnp1_cp_d2",
	"dsi_dnp1_cp_d17",
	"dsi_dn2",
	"dsi_dummy",
};

static const char * const nand_groups[] = {
	"dnand_data_wr",
	"dnand_acle_ce0",
	"nand_ceb2",
	"nand_ceb3",
	"nand_dummy",
};

static const char * const spdif_groups[] = {
	"uart0_tx",
};

static const char * const sirq0_groups[] = {
	"sirq0_dummy",
};

static const char * const sirq1_groups[] = {
	"sirq1_dummy",
};

static const char * const sirq2_groups[] = {
	"sirq2_dummy",
};

static const struct owl_pinmux_func s700_functions[] = {
	[S700_MUX_NOR] = FUNCTION(nor),
	[S700_MUX_ETH_RGMII] = FUNCTION(eth_rmii),
	[S700_MUX_ETH_SGMII] = FUNCTION(eth_smii),
	[S700_MUX_SPI0] = FUNCTION(spi0),
	[S700_MUX_SPI1] = FUNCTION(spi1),
	[S700_MUX_SPI2] = FUNCTION(spi2),
	[S700_MUX_SPI3] = FUNCTION(spi3),
	[S700_MUX_SENS0] = FUNCTION(sens0),
	[S700_MUX_SENS1] = FUNCTION(sens1),
	[S700_MUX_UART0] = FUNCTION(uart0),
	[S700_MUX_UART1] = FUNCTION(uart1),
	[S700_MUX_UART2] = FUNCTION(uart2),
	[S700_MUX_UART3] = FUNCTION(uart3),
	[S700_MUX_UART4] = FUNCTION(uart4),
	[S700_MUX_UART5] = FUNCTION(uart5),
	[S700_MUX_UART6] = FUNCTION(uart6),
	[S700_MUX_I2S0] = FUNCTION(i2s0),
	[S700_MUX_I2S1] = FUNCTION(i2s1),
	[S700_MUX_PCM1] = FUNCTION(pcm1),
	[S700_MUX_PCM0] = FUNCTION(pcm0),
	[S700_MUX_KS] = FUNCTION(ks),
	[S700_MUX_JTAG] = FUNCTION(jtag),
	[S700_MUX_PWM0] = FUNCTION(pwm0),
	[S700_MUX_PWM1] = FUNCTION(pwm1),
	[S700_MUX_PWM2] = FUNCTION(pwm2),
	[S700_MUX_PWM3] = FUNCTION(pwm3),
	[S700_MUX_PWM4] = FUNCTION(pwm4),
	[S700_MUX_PWM5] = FUNCTION(pwm5),
	[S700_MUX_P0] = FUNCTION(p0),
	[S700_MUX_SD0] = FUNCTION(sd0),
	[S700_MUX_SD1] = FUNCTION(sd1),
	[S700_MUX_SD2] = FUNCTION(sd2),
	[S700_MUX_I2C0] = FUNCTION(i2c0),
	[S700_MUX_I2C1] = FUNCTION(i2c1),
	[S700_MUX_I2C2] = FUNCTION(i2c2),
	[S700_MUX_I2C3] = FUNCTION(i2c3),
	[S700_MUX_DSI] = FUNCTION(dsi),
	[S700_MUX_LVDS] = FUNCTION(lvds),
	[S700_MUX_USB30] = FUNCTION(usb30),
	[S700_MUX_CLKO_25M] = FUNCTION(clko_25m),
	[S700_MUX_MIPI_CSI] = FUNCTION(mipi_csi),
	[S700_MUX_DSI] = FUNCTION(dsi),
	[S700_MUX_NAND] = FUNCTION(nand),
	[S700_MUX_SPDIF] = FUNCTION(spdif),
	[S700_MUX_SIRQ0] = FUNCTION(sirq0),
	[S700_MUX_SIRQ1] = FUNCTION(sirq1),
	[S700_MUX_SIRQ2] = FUNCTION(sirq2),
	[S700_MUX_BT] = FUNCTION(bt),
	[S700_MUX_LCD0] = FUNCTION(lcd0),
};

/* PAD_ST0 */
static PAD_ST_CONF(UART2_TX, 0, 31, 1);
static PAD_ST_CONF(I2C0_SDATA, 0, 30, 1);
static PAD_ST_CONF(UART0_RX, 0, 29, 1);
static PAD_ST_CONF(I2S_MCLK1, 0, 23, 1);
static PAD_ST_CONF(ETH_REF_CLK, 0, 22, 1);
static PAD_ST_CONF(ETH_TXEN, 0, 21, 1);
static PAD_ST_CONF(ETH_TXD0, 0, 20, 1);
static PAD_ST_CONF(I2S_LRCLK1, 0, 19, 1);
static PAD_ST_CONF(DSI_DP0, 0, 16, 1);
static PAD_ST_CONF(DSI_DN0, 0, 15, 1);
static PAD_ST_CONF(UART0_TX, 0, 14, 1);
static PAD_ST_CONF(SD0_CLK, 0, 12, 1);
static PAD_ST_CONF(KS_IN0, 0, 11, 1);
static PAD_ST_CONF(SENSOR0_PCLK, 0, 9, 1);
static PAD_ST_CONF(I2C0_SCLK, 0, 7, 1);
static PAD_ST_CONF(KS_OUT0, 0, 6, 1);
static PAD_ST_CONF(KS_OUT1, 0, 5, 1);
static PAD_ST_CONF(KS_OUT2, 0, 4, 1);
static PAD_ST_CONF(ETH_TXD3, 0, 3, 1);
static PAD_ST_CONF(ETH_TXD2, 0, 2, 1);

/* PAD_ST1 */
static PAD_ST_CONF(DSI_DP2, 1, 31, 1);
static PAD_ST_CONF(DSI_DN2, 1, 30, 1);
static PAD_ST_CONF(I2S_LRCLK0, 1, 29, 1);
static PAD_ST_CONF(UART3_CTSB, 1, 27, 1);
static PAD_ST_CONF(UART3_RTSB, 1, 26, 1);
static PAD_ST_CONF(UART3_RX, 1, 25, 1);
static PAD_ST_CONF(UART2_RTSB, 1, 24, 1);
static PAD_ST_CONF(UART2_CTSB, 1, 23, 1);
static PAD_ST_CONF(UART2_RX, 1, 22, 1);
static PAD_ST_CONF(ETH_RXD0, 1, 21, 1);
static PAD_ST_CONF(ETH_RXD1, 1, 20, 1);
static PAD_ST_CONF(ETH_CRS_DV, 1, 19, 1);
static PAD_ST_CONF(ETH_RXER, 1, 18, 1);
static PAD_ST_CONF(ETH_TXD1, 1, 17, 1);
static PAD_ST_CONF(LVDS_OAP, 1, 12, 1);
static PAD_ST_CONF(PCM1_CLK, 1, 11, 1);
static PAD_ST_CONF(PCM1_IN, 1, 10, 1);
static PAD_ST_CONF(PCM1_SYNC, 1, 9, 1);
static PAD_ST_CONF(I2C1_SCLK, 1, 8, 1);
static PAD_ST_CONF(I2C1_SDATA, 1, 7, 1);
static PAD_ST_CONF(I2C2_SCLK, 1, 6, 1);
static PAD_ST_CONF(I2C2_SDATA, 1, 5, 1);

static PAD_ST_CONF(SPI0_MISO, 1, 3, 1);
static PAD_ST_CONF(SPI0_SS, 1, 2, 1);
static PAD_ST_CONF(I2S_BCLK0, 1, 1, 1);
static PAD_ST_CONF(I2S_MCLK0, 1, 0, 1);

/* PAD_PULLCTL0 */
static PAD_PULLCTL_CONF(PCM1_SYNC, 0, 30, 1);
static PAD_PULLCTL_CONF(PCM1_OUT, 0, 29, 1);
static PAD_PULLCTL_CONF(KS_OUT2, 0, 28, 1);
static PAD_PULLCTL_CONF(LCD0_D2, 0, 27, 1);
static PAD_PULLCTL_CONF(DSI_DN3, 0, 26, 1);
static PAD_PULLCTL_CONF(ETH_RXER, 0, 16, 1);
static PAD_PULLCTL_CONF(SIRQ0, 0, 14, 2);
static PAD_PULLCTL_CONF(SIRQ1, 0, 12, 2);
static PAD_PULLCTL_CONF(SIRQ2, 0, 10, 2);
static PAD_PULLCTL_CONF(I2C0_SDATA, 0, 9, 1);
static PAD_PULLCTL_CONF(I2C0_SCLK, 0, 8, 1);
static PAD_PULLCTL_CONF(KS_IN0, 0, 7, 1);
static PAD_PULLCTL_CONF(KS_IN1, 0, 6, 1);
static PAD_PULLCTL_CONF(KS_IN2, 0, 5, 1);
static PAD_PULLCTL_CONF(KS_IN3, 0, 4, 1);
static PAD_PULLCTL_CONF(KS_OUT0, 0, 2, 1);
static PAD_PULLCTL_CONF(KS_OUT1, 0, 1, 1);
static PAD_PULLCTL_CONF(DSI_DP1, 0, 0, 1);

/* PAD_PULLCTL1 */
static PAD_PULLCTL_CONF(SD0_D0, 1, 17, 1);
static PAD_PULLCTL_CONF(SD0_D1, 1, 16, 1);
static PAD_PULLCTL_CONF(SD0_D2, 1, 15, 1);
static PAD_PULLCTL_CONF(SD0_D3, 1, 14, 1);
static PAD_PULLCTL_CONF(SD0_CMD, 1, 13, 1);
static PAD_PULLCTL_CONF(SD0_CLK, 1, 12, 1);
static PAD_PULLCTL_CONF(UART0_RX, 1, 2, 1);
static PAD_PULLCTL_CONF(UART0_TX, 1, 1, 1);
static PAD_PULLCTL_CONF(CLKO_25M, 1, 0, 1);

/* PAD_PULLCTL2 */
static PAD_PULLCTL_CONF(ETH_TXD2, 2, 18, 1);
static PAD_PULLCTL_CONF(ETH_TXD3, 2, 17, 1);
static PAD_PULLCTL_CONF(SPI0_SS, 2, 16, 1);
static PAD_PULLCTL_CONF(SPI0_MISO, 2, 15, 1);
static PAD_PULLCTL_CONF(I2C1_SDATA, 2, 10, 1);
static PAD_PULLCTL_CONF(I2C1_SCLK, 2, 9, 1);
static PAD_PULLCTL_CONF(I2C2_SDATA, 2, 8, 1);
static PAD_PULLCTL_CONF(I2C2_SCLK, 2, 7, 1);

/* Pad info table for the pinmux subsystem */
static struct owl_padinfo s700_padinfo[NUM_PADS] = {
	[ETH_TXD0] = PAD_INFO_ST(ETH_TXD0),
	[ETH_TXD1] = PAD_INFO_ST(ETH_TXD1),
	[ETH_TXEN] = PAD_INFO_ST(ETH_TXEN),
	[ETH_RXER] = PAD_INFO_PULLCTL_ST(ETH_RXER),
	[ETH_CRS_DV] = PAD_INFO_ST(ETH_CRS_DV),
	[ETH_RXD1] = PAD_INFO_ST(ETH_RXD1),
	[ETH_RXD0] = PAD_INFO_ST(ETH_RXD0),
	[ETH_REF_CLK] = PAD_INFO_ST(ETH_REF_CLK),
	[ETH_MDC] = PAD_INFO(ETH_MDC),
	[ETH_MDIO] = PAD_INFO(ETH_MDIO),
	[SIRQ0] = PAD_INFO_PULLCTL(SIRQ0),
	[SIRQ1] = PAD_INFO_PULLCTL(SIRQ1),
	[SIRQ2] = PAD_INFO_PULLCTL(SIRQ2),
	[I2S_D0] = PAD_INFO(I2S_D0),
	[I2S_BCLK0] = PAD_INFO_ST(I2S_BCLK0),
	[I2S_LRCLK0] = PAD_INFO_ST(I2S_LRCLK0),
	[I2S_MCLK0] = PAD_INFO_ST(I2S_MCLK0),
	[I2S_D1] = PAD_INFO(I2S_D1),
	[I2S_BCLK1] = PAD_INFO(I2S_BCLK1),
	[I2S_LRCLK1] = PAD_INFO_ST(I2S_LRCLK1),
	[I2S_MCLK1] = PAD_INFO_ST(I2S_MCLK1),
	[KS_IN0] = PAD_INFO_PULLCTL_ST(KS_IN0),
	[KS_IN1] = PAD_INFO_PULLCTL(KS_IN1),
	[KS_IN2] = PAD_INFO_PULLCTL(KS_IN2),
	[KS_IN3] = PAD_INFO_PULLCTL(KS_IN3),
	[KS_OUT0] = PAD_INFO_PULLCTL_ST(KS_OUT0),
	[KS_OUT1] = PAD_INFO_PULLCTL_ST(KS_OUT1),
	[KS_OUT2] = PAD_INFO_PULLCTL_ST(KS_OUT2),
	[LVDS_OEP] = PAD_INFO(LVDS_OEP),
	[LVDS_OEN] = PAD_INFO(LVDS_OEN),
	[LVDS_ODP] = PAD_INFO(LVDS_ODP),
	[LVDS_ODN] = PAD_INFO(LVDS_ODN),
	[LVDS_OCP] = PAD_INFO(LVDS_OCP),
	[LVDS_OCN] = PAD_INFO(LVDS_OCN),
	[LVDS_OBP] = PAD_INFO(LVDS_OBP),
	[LVDS_OBN] = PAD_INFO(LVDS_OBN),
	[LVDS_OAP] = PAD_INFO_ST(LVDS_OAP),
	[LVDS_OAN] = PAD_INFO(LVDS_OAN),
	[LVDS_EEP] = PAD_INFO(LVDS_EEP),
	[LVDS_EEN] = PAD_INFO(LVDS_EEN),
	[LVDS_EDP] = PAD_INFO(LVDS_EDP),
	[LVDS_EDN] = PAD_INFO(LVDS_EDN),
	[LVDS_ECP] = PAD_INFO(LVDS_ECP),
	[LVDS_ECN] = PAD_INFO(LVDS_ECN),
	[LVDS_EBP] = PAD_INFO(LVDS_EBP),
	[LVDS_EBN] = PAD_INFO(LVDS_EBN),
	[LVDS_EAP] = PAD_INFO(LVDS_EAP),
	[LVDS_EAN] = PAD_INFO(LVDS_EAN),
	[LCD0_D18] = PAD_INFO(LCD0_D18),
	[LCD0_D2] = PAD_INFO_PULLCTL(LCD0_D2),
	[DSI_DP3] = PAD_INFO(DSI_DP3),
	[DSI_DN3] = PAD_INFO_PULLCTL(DSI_DN3),
	[DSI_DP1] = PAD_INFO_PULLCTL(DSI_DP1),
	[DSI_DN1] = PAD_INFO(DSI_DN1),
	[DSI_DP0] = PAD_INFO_ST(DSI_DP0),
	[DSI_DN0] = PAD_INFO_ST(DSI_DN0),
	[DSI_DP2] = PAD_INFO_ST(DSI_DP2),
	[DSI_DN2] = PAD_INFO_ST(DSI_DN2),
	[SD0_D0] = PAD_INFO_PULLCTL(SD0_D0),
	[SD0_D1] = PAD_INFO_PULLCTL(SD0_D1),
	[SD0_D2] = PAD_INFO_PULLCTL(SD0_D2),
	[SD0_D3] = PAD_INFO_PULLCTL(SD0_D3),
	[SD0_CMD] = PAD_INFO_PULLCTL(SD0_CMD),
	[SD0_CLK] = PAD_INFO_PULLCTL_ST(SD0_CLK),
	[SD1_CLK] = PAD_INFO(SD1_CLK),
	[SPI0_SS] = PAD_INFO_PULLCTL_ST(SPI0_SS),
	[SPI0_MISO] = PAD_INFO_PULLCTL_ST(SPI0_MISO),
	[UART0_RX] = PAD_INFO_PULLCTL_ST(UART0_RX),
	[UART0_TX] = PAD_INFO_PULLCTL_ST(UART0_TX),
	[I2C0_SCLK] = PAD_INFO_PULLCTL_ST(I2C0_SCLK),
	[I2C0_SDATA] = PAD_INFO_PULLCTL_ST(I2C0_SDATA),
	[SENSOR0_PCLK] = PAD_INFO_ST(SENSOR0_PCLK),
	[SENSOR0_CKOUT] = PAD_INFO(SENSOR0_CKOUT),
	[DNAND_ALE] = PAD_INFO(DNAND_ALE),
	[DNAND_CLE] = PAD_INFO(DNAND_CLE),
	[DNAND_CEB0] = PAD_INFO(DNAND_CEB0),
	[DNAND_CEB1] = PAD_INFO(DNAND_CEB1),
	[DNAND_CEB2] = PAD_INFO(DNAND_CEB2),
	[DNAND_CEB3] = PAD_INFO(DNAND_CEB3),
	[UART2_RX] = PAD_INFO_ST(UART2_RX),
	[UART2_TX] = PAD_INFO_ST(UART2_TX),
	[UART2_RTSB] = PAD_INFO_ST(UART2_RTSB),
	[UART2_CTSB] = PAD_INFO_ST(UART2_CTSB),
	[UART3_RX] = PAD_INFO_ST(UART3_RX),
	[UART3_TX] = PAD_INFO(UART3_TX),
	[UART3_RTSB] = PAD_INFO_ST(UART3_RTSB),
	[UART3_CTSB] = PAD_INFO_ST(UART3_CTSB),
	[PCM1_IN] = PAD_INFO_ST(PCM1_IN),
	[PCM1_CLK] = PAD_INFO_ST(PCM1_CLK),
	[PCM1_SYNC] = PAD_INFO_PULLCTL_ST(PCM1_SYNC),
	[PCM1_OUT] = PAD_INFO_PULLCTL(PCM1_OUT),
	[I2C1_SCLK] = PAD_INFO_PULLCTL_ST(I2C1_SCLK),
	[I2C1_SDATA] = PAD_INFO_PULLCTL_ST(I2C1_SDATA),
	[I2C2_SCLK] = PAD_INFO_PULLCTL_ST(I2C2_SCLK),
	[I2C2_SDATA] = PAD_INFO_PULLCTL_ST(I2C2_SDATA),
	[CSI_DN0] = PAD_INFO(CSI_DN0),
	[CSI_DP0] = PAD_INFO(CSI_DP0),
	[CSI_DN1] = PAD_INFO(CSI_DN1),
	[CSI_DP1] = PAD_INFO(CSI_DP1),
	[CSI_CN] = PAD_INFO(CSI_CN),
	[CSI_CP] = PAD_INFO(CSI_CP),
	[CSI_DN2] = PAD_INFO(CSI_DN2),
	[CSI_DP2] = PAD_INFO(CSI_DP2),
	[CSI_DN3] = PAD_INFO(CSI_DN3),
	[CSI_DP3] = PAD_INFO(CSI_DP3),
	[DNAND_WRB] = PAD_INFO(DNAND_WRB),
	[DNAND_RDB] = PAD_INFO(DNAND_RDB),
	[DNAND_RB0] = PAD_INFO(DNAND_RB0),
	[PORB] = PAD_INFO(PORB),
	[CLKO_25M] = PAD_INFO_PULLCTL(CLKO_25M),
	[BSEL] = PAD_INFO(BSEL),
	[PKG0] = PAD_INFO(PKG0),
	[PKG1] = PAD_INFO(PKG1),
	[PKG2] = PAD_INFO(PKG2),
	[PKG3] = PAD_INFO(PKG3),
	[ETH_TXD2] = PAD_INFO_PULLCTL_ST(ETH_TXD2),
	[ETH_TXD3] = PAD_INFO_PULLCTL_ST(ETH_TXD3),
};

static const struct owl_gpio_port s700_gpio_ports[] = {
	OWL_GPIO_PORT(A, 0x0000, 32, 0x0, 0x4, 0x8, 0x204, 0x208, 0x20C, 0x230, 0),
	OWL_GPIO_PORT(B, 0x000C, 32, 0x0, 0x4, 0x8, 0x204, 0x210, 0x214, 0x238, 1),
	OWL_GPIO_PORT(C, 0x0018, 32, 0x0, 0x4, 0x8, 0x204, 0x218, 0x21C, 0x240, 2),
	OWL_GPIO_PORT(D, 0x0024, 32, 0x0, 0x4, 0x8, 0x204, 0x220, 0x224, 0x248, 3),
	/* 0x24C (INTC_GPIOD_TYPE1) used to tweak the driver to handle generic */
	OWL_GPIO_PORT(E, 0x0030, 8, 0x0, 0x4, 0x8, 0x204, 0x228, 0x22C, 0x24C, 4),
};

enum s700_pinconf_pull {
	OWL_PINCONF_PULL_DOWN,
	OWL_PINCONF_PULL_UP,
};

static int s700_pad_pinconf_arg2val(const struct owl_padinfo *info,
				unsigned int param,
				u32 *arg)
{
	switch (param) {
	case PIN_CONFIG_BIAS_PULL_DOWN:
		*arg = OWL_PINCONF_PULL_DOWN;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		*arg = OWL_PINCONF_PULL_UP;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		*arg = (*arg >= 1 ? 1 : 0);
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int s700_pad_pinconf_val2arg(const struct owl_padinfo *padinfo,
				unsigned int param,
				u32 *arg)
{
	switch (param) {
	case PIN_CONFIG_BIAS_PULL_DOWN:
		*arg = *arg == OWL_PINCONF_PULL_DOWN;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		*arg = *arg == OWL_PINCONF_PULL_UP;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		*arg = *arg == 1;
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static struct owl_pinctrl_soc_data s700_pinctrl_data = {
	.padinfo = s700_padinfo,
	.pins = (const struct pinctrl_pin_desc *)s700_pads,
	.npins = ARRAY_SIZE(s700_pads),
	.functions = s700_functions,
	.nfunctions = ARRAY_SIZE(s700_functions),
	.groups = s700_groups,
	.ngroups = ARRAY_SIZE(s700_groups),
	.ngpios = NUM_GPIOS,
	.ports = s700_gpio_ports,
	.nports = ARRAY_SIZE(s700_gpio_ports),
	.padctl_arg2val = s700_pad_pinconf_arg2val,
	.padctl_val2arg = s700_pad_pinconf_val2arg,
};

static int s700_pinctrl_probe(struct platform_device *pdev)
{
	return owl_pinctrl_probe(pdev, &s700_pinctrl_data);
}

static const struct of_device_id s700_pinctrl_of_match[] = {
	{ .compatible = "actions,s700-pinctrl", },
	{}
};

static struct platform_driver s700_pinctrl_driver = {
	.probe = s700_pinctrl_probe,
	.driver = {
		.name = "pinctrl-s700",
		.of_match_table = of_match_ptr(s700_pinctrl_of_match),
	},
};

static int __init s700_pinctrl_init(void)
{
	return platform_driver_register(&s700_pinctrl_driver);
}
arch_initcall(s700_pinctrl_init);

static void __exit s700_pinctrl_exit(void)
{
	platform_driver_unregister(&s700_pinctrl_driver);
}
module_exit(s700_pinctrl_exit);

MODULE_AUTHOR("Actions Semi Inc.");
MODULE_DESCRIPTION("Actions Semi S700 Soc Pinctrl Driver");
MODULE_LICENSE("GPL");
