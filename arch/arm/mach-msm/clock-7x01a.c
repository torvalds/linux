/* arch/arm/mach-msm/clock-7x01a.c
 *
 * Clock tables for MSM7X01A
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2007 QUALCOMM Incorporated
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include "clock.h"
#include "devices.h"

/* clock IDs used by the modem processor */

#define ACPU_CLK	0   /* Applications processor clock */
#define ADM_CLK		1   /* Applications data mover clock */
#define ADSP_CLK	2   /* ADSP clock */
#define EBI1_CLK	3   /* External bus interface 1 clock */
#define EBI2_CLK	4   /* External bus interface 2 clock */
#define ECODEC_CLK	5   /* External CODEC clock */
#define EMDH_CLK	6   /* External MDDI host clock */
#define GP_CLK		7   /* General purpose clock */
#define GRP_CLK		8   /* Graphics clock */
#define I2C_CLK		9   /* I2C clock */
#define ICODEC_RX_CLK	10  /* Internal CODEX RX clock */
#define ICODEC_TX_CLK	11  /* Internal CODEX TX clock */
#define IMEM_CLK	12  /* Internal graphics memory clock */
#define MDC_CLK		13  /* MDDI client clock */
#define MDP_CLK		14  /* Mobile display processor clock */
#define PBUS_CLK	15  /* Peripheral bus clock */
#define PCM_CLK		16  /* PCM clock */
#define PMDH_CLK	17  /* Primary MDDI host clock */
#define SDAC_CLK	18  /* Stereo DAC clock */
#define SDC1_CLK	19  /* Secure Digital Card clocks */
#define SDC1_PCLK	20
#define SDC2_CLK	21
#define SDC2_PCLK	22
#define SDC3_CLK	23
#define SDC3_PCLK	24
#define SDC4_CLK	25
#define SDC4_PCLK	26
#define TSIF_CLK	27  /* Transport Stream Interface clocks */
#define TSIF_REF_CLK	28
#define TV_DAC_CLK	29  /* TV clocks */
#define TV_ENC_CLK	30
#define UART1_CLK	31  /* UART clocks */
#define UART2_CLK	32
#define UART3_CLK	33
#define UART1DM_CLK	34
#define UART2DM_CLK	35
#define USB_HS_CLK	36  /* High speed USB core clock */
#define USB_HS_PCLK	37  /* High speed USB pbus clock */
#define USB_OTG_CLK	38  /* Full speed USB clock */
#define VDC_CLK		39  /* Video controller clock */
#define VFE_CLK		40  /* Camera / Video Front End clock */
#define VFE_MDC_CLK	41  /* VFE MDDI client clock */

#define NR_CLKS		42

#define CLOCK(clk_name, clk_id, clk_dev, clk_flags) {	\
	.name = clk_name, \
	.id = clk_id, \
	.flags = clk_flags, \
	.dev = clk_dev, \
	}

#define OFF CLKFLAG_AUTO_OFF
#define MINMAX CLKFLAG_USE_MIN_MAX_TO_SET

struct clk msm_clocks[] = {
	CLOCK("adm_clk",	ADM_CLK,	NULL, 0),
	CLOCK("adsp_clk",	ADSP_CLK,	NULL, 0),
	CLOCK("ebi1_clk",	EBI1_CLK,	NULL, 0),
	CLOCK("ebi2_clk",	EBI2_CLK,	NULL, 0),
	CLOCK("ecodec_clk",	ECODEC_CLK,	NULL, 0),
	CLOCK("emdh_clk",	EMDH_CLK,	NULL, OFF),
	CLOCK("gp_clk",		GP_CLK,		NULL, 0),
	CLOCK("grp_clk",	GRP_CLK,	NULL, OFF),
	CLOCK("i2c_clk",	I2C_CLK,	&msm_device_i2c.dev, 0),
	CLOCK("icodec_rx_clk",	ICODEC_RX_CLK,	NULL, 0),
	CLOCK("icodec_tx_clk",	ICODEC_TX_CLK,	NULL, 0),
	CLOCK("imem_clk",	IMEM_CLK,	NULL, OFF),
	CLOCK("mdc_clk",	MDC_CLK,	NULL, 0),
	CLOCK("mdp_clk",	MDP_CLK,	NULL, OFF),
	CLOCK("pbus_clk",	PBUS_CLK,	NULL, 0),
	CLOCK("pcm_clk",	PCM_CLK,	NULL, 0),
	CLOCK("pmdh_clk",	PMDH_CLK,	NULL, OFF | MINMAX),
	CLOCK("sdac_clk",	SDAC_CLK,	NULL, OFF),
	CLOCK("sdc_clk",	SDC1_CLK,	&msm_device_sdc1.dev, OFF),
	CLOCK("sdc_pclk",	SDC1_PCLK,	&msm_device_sdc1.dev, OFF),
	CLOCK("sdc_clk",	SDC2_CLK,	&msm_device_sdc2.dev, OFF),
	CLOCK("sdc_pclk",	SDC2_PCLK,	&msm_device_sdc2.dev, OFF),
	CLOCK("sdc_clk",	SDC3_CLK,	&msm_device_sdc3.dev, OFF),
	CLOCK("sdc_pclk",	SDC3_PCLK,	&msm_device_sdc3.dev, OFF),
	CLOCK("sdc_clk",	SDC4_CLK,	&msm_device_sdc4.dev, OFF),
	CLOCK("sdc_pclk",	SDC4_PCLK,	&msm_device_sdc4.dev, OFF),
	CLOCK("tsif_clk",	TSIF_CLK,	NULL, 0),
	CLOCK("tsif_ref_clk",	TSIF_REF_CLK,	NULL, 0),
	CLOCK("tv_dac_clk",	TV_DAC_CLK,	NULL, 0),
	CLOCK("tv_enc_clk",	TV_ENC_CLK,	NULL, 0),
	CLOCK("uart_clk",	UART1_CLK,	&msm_device_uart1.dev, OFF),
	CLOCK("uart_clk",	UART2_CLK,	&msm_device_uart2.dev, 0),
	CLOCK("uart_clk",	UART3_CLK,	&msm_device_uart3.dev, OFF),
	CLOCK("uart1dm_clk",	UART1DM_CLK,	NULL, OFF),
	CLOCK("uart2dm_clk",	UART2DM_CLK,	NULL, 0),
	CLOCK("usb_hs_clk",	USB_HS_CLK,	&msm_device_hsusb.dev, OFF),
	CLOCK("usb_hs_pclk",	USB_HS_PCLK,	&msm_device_hsusb.dev, OFF),
	CLOCK("usb_otg_clk",	USB_OTG_CLK,	NULL, 0),
	CLOCK("vdc_clk",	VDC_CLK,	NULL, OFF | MINMAX),
	CLOCK("vfe_clk",	VFE_CLK,	NULL, OFF),
	CLOCK("vfe_mdc_clk",	VFE_MDC_CLK,	NULL, OFF),
};

unsigned msm_num_clocks = ARRAY_SIZE(msm_clocks);
