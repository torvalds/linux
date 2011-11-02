/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/div64.h>
#include <asm/mach/map.h>
#include <mach/clock.h>
#include <mach/common.h>
#include <mach/hardware.h>

#define PLL_BASE		IMX_IO_ADDRESS(MX6Q_ANATOP_BASE_ADDR)
#define PLL1_SYS		(PLL_BASE + 0x000)
#define PLL2_BUS		(PLL_BASE + 0x030)
#define PLL3_USB_OTG		(PLL_BASE + 0x010)
#define PLL4_AUDIO		(PLL_BASE + 0x070)
#define PLL5_VIDEO		(PLL_BASE + 0x0a0)
#define PLL6_MLB		(PLL_BASE + 0x0d0)
#define PLL7_USB_HOST		(PLL_BASE + 0x020)
#define PLL8_ENET		(PLL_BASE + 0x0e0)
#define PFD_480			(PLL_BASE + 0x0f0)
#define PFD_528			(PLL_BASE + 0x100)
#define PLL_NUM_OFFSET		0x010
#define PLL_DENOM_OFFSET	0x020

#define PFD0			7
#define PFD1			15
#define PFD2			23
#define PFD3			31
#define PFD_FRAC_MASK		0x3f

#define BM_PLL_BYPASS			(0x1 << 16)
#define BM_PLL_ENABLE			(0x1 << 13)
#define BM_PLL_POWER_DOWN		(0x1 << 12)
#define BM_PLL_LOCK			(0x1 << 31)
#define BP_PLL_SYS_DIV_SELECT		0
#define BM_PLL_SYS_DIV_SELECT		(0x7f << 0)
#define BP_PLL_BUS_DIV_SELECT		0
#define BM_PLL_BUS_DIV_SELECT		(0x1 << 0)
#define BP_PLL_USB_DIV_SELECT		0
#define BM_PLL_USB_DIV_SELECT		(0x3 << 0)
#define BP_PLL_AV_DIV_SELECT		0
#define BM_PLL_AV_DIV_SELECT		(0x7f << 0)
#define BP_PLL_ENET_DIV_SELECT		0
#define BM_PLL_ENET_DIV_SELECT		(0x3 << 0)
#define BM_PLL_ENET_EN_PCIE		(0x1 << 19)
#define BM_PLL_ENET_EN_SATA		(0x1 << 20)

#define CCM_BASE	IMX_IO_ADDRESS(MX6Q_CCM_BASE_ADDR)
#define CCR		(CCM_BASE + 0x00)
#define CCDR		(CCM_BASE + 0x04)
#define CSR		(CCM_BASE + 0x08)
#define CCSR		(CCM_BASE + 0x0c)
#define CACRR		(CCM_BASE + 0x10)
#define CBCDR		(CCM_BASE + 0x14)
#define CBCMR		(CCM_BASE + 0x18)
#define CSCMR1		(CCM_BASE + 0x1c)
#define CSCMR2		(CCM_BASE + 0x20)
#define CSCDR1		(CCM_BASE + 0x24)
#define CS1CDR		(CCM_BASE + 0x28)
#define CS2CDR		(CCM_BASE + 0x2c)
#define CDCDR		(CCM_BASE + 0x30)
#define CHSCCDR		(CCM_BASE + 0x34)
#define CSCDR2		(CCM_BASE + 0x38)
#define CSCDR3		(CCM_BASE + 0x3c)
#define CSCDR4		(CCM_BASE + 0x40)
#define CWDR		(CCM_BASE + 0x44)
#define CDHIPR		(CCM_BASE + 0x48)
#define CDCR		(CCM_BASE + 0x4c)
#define CTOR		(CCM_BASE + 0x50)
#define CLPCR		(CCM_BASE + 0x54)
#define CISR		(CCM_BASE + 0x58)
#define CIMR		(CCM_BASE + 0x5c)
#define CCOSR		(CCM_BASE + 0x60)
#define CGPR		(CCM_BASE + 0x64)
#define CCGR0		(CCM_BASE + 0x68)
#define CCGR1		(CCM_BASE + 0x6c)
#define CCGR2		(CCM_BASE + 0x70)
#define CCGR3		(CCM_BASE + 0x74)
#define CCGR4		(CCM_BASE + 0x78)
#define CCGR5		(CCM_BASE + 0x7c)
#define CCGR6		(CCM_BASE + 0x80)
#define CCGR7		(CCM_BASE + 0x84)
#define CMEOR		(CCM_BASE + 0x88)

#define CG0		0
#define CG1		2
#define CG2		4
#define CG3		6
#define CG4		8
#define CG5		10
#define CG6		12
#define CG7		14
#define CG8		16
#define CG9		18
#define CG10		20
#define CG11		22
#define CG12		24
#define CG13		26
#define CG14		28
#define CG15		30

#define BM_CCSR_PLL1_SW_SEL		(0x1 << 2)
#define BM_CCSR_STEP_SEL		(0x1 << 8)

#define BP_CACRR_ARM_PODF		0
#define BM_CACRR_ARM_PODF		(0x7 << 0)

#define BP_CBCDR_PERIPH2_CLK2_PODF	0
#define BM_CBCDR_PERIPH2_CLK2_PODF	(0x7 << 0)
#define BP_CBCDR_MMDC_CH1_AXI_PODF	3
#define BM_CBCDR_MMDC_CH1_AXI_PODF	(0x7 << 3)
#define BP_CBCDR_AXI_SEL		6
#define BM_CBCDR_AXI_SEL		(0x3 << 6)
#define BP_CBCDR_IPG_PODF		8
#define BM_CBCDR_IPG_PODF		(0x3 << 8)
#define BP_CBCDR_AHB_PODF		10
#define BM_CBCDR_AHB_PODF		(0x7 << 10)
#define BP_CBCDR_AXI_PODF		16
#define BM_CBCDR_AXI_PODF		(0x7 << 16)
#define BP_CBCDR_MMDC_CH0_AXI_PODF	19
#define BM_CBCDR_MMDC_CH0_AXI_PODF	(0x7 << 19)
#define BP_CBCDR_PERIPH_CLK_SEL		25
#define BM_CBCDR_PERIPH_CLK_SEL		(0x1 << 25)
#define BP_CBCDR_PERIPH2_CLK_SEL	26
#define BM_CBCDR_PERIPH2_CLK_SEL	(0x1 << 26)
#define BP_CBCDR_PERIPH_CLK2_PODF	27
#define BM_CBCDR_PERIPH_CLK2_PODF	(0x7 << 27)

#define BP_CBCMR_GPU2D_AXI_SEL		0
#define BM_CBCMR_GPU2D_AXI_SEL		(0x1 << 0)
#define BP_CBCMR_GPU3D_AXI_SEL		1
#define BM_CBCMR_GPU3D_AXI_SEL		(0x1 << 1)
#define BP_CBCMR_GPU3D_CORE_SEL		4
#define BM_CBCMR_GPU3D_CORE_SEL		(0x3 << 4)
#define BP_CBCMR_GPU3D_SHADER_SEL	8
#define BM_CBCMR_GPU3D_SHADER_SEL	(0x3 << 8)
#define BP_CBCMR_PCIE_AXI_SEL		10
#define BM_CBCMR_PCIE_AXI_SEL		(0x1 << 10)
#define BP_CBCMR_VDO_AXI_SEL		11
#define BM_CBCMR_VDO_AXI_SEL		(0x1 << 11)
#define BP_CBCMR_PERIPH_CLK2_SEL	12
#define BM_CBCMR_PERIPH_CLK2_SEL	(0x3 << 12)
#define BP_CBCMR_VPU_AXI_SEL		14
#define BM_CBCMR_VPU_AXI_SEL		(0x3 << 14)
#define BP_CBCMR_GPU2D_CORE_SEL		16
#define BM_CBCMR_GPU2D_CORE_SEL		(0x3 << 16)
#define BP_CBCMR_PRE_PERIPH_CLK_SEL	18
#define BM_CBCMR_PRE_PERIPH_CLK_SEL	(0x3 << 18)
#define BP_CBCMR_PERIPH2_CLK2_SEL	20
#define BM_CBCMR_PERIPH2_CLK2_SEL	(0x1 << 20)
#define BP_CBCMR_PRE_PERIPH2_CLK_SEL	21
#define BM_CBCMR_PRE_PERIPH2_CLK_SEL	(0x3 << 21)
#define BP_CBCMR_GPU2D_CORE_PODF	23
#define BM_CBCMR_GPU2D_CORE_PODF	(0x7 << 23)
#define BP_CBCMR_GPU3D_CORE_PODF	26
#define BM_CBCMR_GPU3D_CORE_PODF	(0x7 << 26)
#define BP_CBCMR_GPU3D_SHADER_PODF	29
#define BM_CBCMR_GPU3D_SHADER_PODF	(0x7 << 29)

#define BP_CSCMR1_PERCLK_PODF		0
#define BM_CSCMR1_PERCLK_PODF		(0x3f << 0)
#define BP_CSCMR1_SSI1_SEL		10
#define BM_CSCMR1_SSI1_SEL		(0x3 << 10)
#define BP_CSCMR1_SSI2_SEL		12
#define BM_CSCMR1_SSI2_SEL		(0x3 << 12)
#define BP_CSCMR1_SSI3_SEL		14
#define BM_CSCMR1_SSI3_SEL		(0x3 << 14)
#define BP_CSCMR1_USDHC1_SEL		16
#define BM_CSCMR1_USDHC1_SEL		(0x1 << 16)
#define BP_CSCMR1_USDHC2_SEL		17
#define BM_CSCMR1_USDHC2_SEL		(0x1 << 17)
#define BP_CSCMR1_USDHC3_SEL		18
#define BM_CSCMR1_USDHC3_SEL		(0x1 << 18)
#define BP_CSCMR1_USDHC4_SEL		19
#define BM_CSCMR1_USDHC4_SEL		(0x1 << 19)
#define BP_CSCMR1_EMI_PODF		20
#define BM_CSCMR1_EMI_PODF		(0x7 << 20)
#define BP_CSCMR1_EMI_SLOW_PODF		23
#define BM_CSCMR1_EMI_SLOW_PODF		(0x7 << 23)
#define BP_CSCMR1_EMI_SEL		27
#define BM_CSCMR1_EMI_SEL		(0x3 << 27)
#define BP_CSCMR1_EMI_SLOW_SEL		29
#define BM_CSCMR1_EMI_SLOW_SEL		(0x3 << 29)

#define BP_CSCMR2_CAN_PODF		2
#define BM_CSCMR2_CAN_PODF		(0x3f << 2)
#define BM_CSCMR2_LDB_DI0_IPU_DIV	(0x1 << 10)
#define BM_CSCMR2_LDB_DI1_IPU_DIV	(0x1 << 11)
#define BP_CSCMR2_ESAI_SEL		19
#define BM_CSCMR2_ESAI_SEL		(0x3 << 19)

#define BP_CSCDR1_UART_PODF		0
#define BM_CSCDR1_UART_PODF		(0x3f << 0)
#define BP_CSCDR1_USDHC1_PODF		11
#define BM_CSCDR1_USDHC1_PODF		(0x7 << 11)
#define BP_CSCDR1_USDHC2_PODF		16
#define BM_CSCDR1_USDHC2_PODF		(0x7 << 16)
#define BP_CSCDR1_USDHC3_PODF		19
#define BM_CSCDR1_USDHC3_PODF		(0x7 << 19)
#define BP_CSCDR1_USDHC4_PODF		22
#define BM_CSCDR1_USDHC4_PODF		(0x7 << 22)
#define BP_CSCDR1_VPU_AXI_PODF		25
#define BM_CSCDR1_VPU_AXI_PODF		(0x7 << 25)

#define BP_CS1CDR_SSI1_PODF		0
#define BM_CS1CDR_SSI1_PODF		(0x3f << 0)
#define BP_CS1CDR_SSI1_PRED		6
#define BM_CS1CDR_SSI1_PRED		(0x7 << 6)
#define BP_CS1CDR_ESAI_PRED		9
#define BM_CS1CDR_ESAI_PRED		(0x7 << 9)
#define BP_CS1CDR_SSI3_PODF		16
#define BM_CS1CDR_SSI3_PODF		(0x3f << 16)
#define BP_CS1CDR_SSI3_PRED		22
#define BM_CS1CDR_SSI3_PRED		(0x7 << 22)
#define BP_CS1CDR_ESAI_PODF		25
#define BM_CS1CDR_ESAI_PODF		(0x7 << 25)

#define BP_CS2CDR_SSI2_PODF		0
#define BM_CS2CDR_SSI2_PODF		(0x3f << 0)
#define BP_CS2CDR_SSI2_PRED		6
#define BM_CS2CDR_SSI2_PRED		(0x7 << 6)
#define BP_CS2CDR_LDB_DI0_SEL		9
#define BM_CS2CDR_LDB_DI0_SEL		(0x7 << 9)
#define BP_CS2CDR_LDB_DI1_SEL		12
#define BM_CS2CDR_LDB_DI1_SEL		(0x7 << 12)
#define BP_CS2CDR_ENFC_SEL		16
#define BM_CS2CDR_ENFC_SEL		(0x3 << 16)
#define BP_CS2CDR_ENFC_PRED		18
#define BM_CS2CDR_ENFC_PRED		(0x7 << 18)
#define BP_CS2CDR_ENFC_PODF		21
#define BM_CS2CDR_ENFC_PODF		(0x3f << 21)

#define BP_CDCDR_ASRC_SERIAL_SEL	7
#define BM_CDCDR_ASRC_SERIAL_SEL	(0x3 << 7)
#define BP_CDCDR_ASRC_SERIAL_PODF	9
#define BM_CDCDR_ASRC_SERIAL_PODF	(0x7 << 9)
#define BP_CDCDR_ASRC_SERIAL_PRED	12
#define BM_CDCDR_ASRC_SERIAL_PRED	(0x7 << 12)
#define BP_CDCDR_SPDIF_SEL		20
#define BM_CDCDR_SPDIF_SEL		(0x3 << 20)
#define BP_CDCDR_SPDIF_PODF		22
#define BM_CDCDR_SPDIF_PODF		(0x7 << 22)
#define BP_CDCDR_SPDIF_PRED		25
#define BM_CDCDR_SPDIF_PRED		(0x7 << 25)
#define BP_CDCDR_HSI_TX_PODF		29
#define BM_CDCDR_HSI_TX_PODF		(0x7 << 29)
#define BP_CDCDR_HSI_TX_SEL		28
#define BM_CDCDR_HSI_TX_SEL		(0x1 << 28)

#define BP_CHSCCDR_IPU1_DI0_SEL		0
#define BM_CHSCCDR_IPU1_DI0_SEL		(0x7 << 0)
#define BP_CHSCCDR_IPU1_DI0_PRE_PODF	3
#define BM_CHSCCDR_IPU1_DI0_PRE_PODF	(0x7 << 3)
#define BP_CHSCCDR_IPU1_DI0_PRE_SEL	6
#define BM_CHSCCDR_IPU1_DI0_PRE_SEL	(0x7 << 6)
#define BP_CHSCCDR_IPU1_DI1_SEL		9
#define BM_CHSCCDR_IPU1_DI1_SEL		(0x7 << 9)
#define BP_CHSCCDR_IPU1_DI1_PRE_PODF	12
#define BM_CHSCCDR_IPU1_DI1_PRE_PODF	(0x7 << 12)
#define BP_CHSCCDR_IPU1_DI1_PRE_SEL	15
#define BM_CHSCCDR_IPU1_DI1_PRE_SEL	(0x7 << 15)

#define BP_CSCDR2_IPU2_DI0_SEL		0
#define BM_CSCDR2_IPU2_DI0_SEL		(0x7)
#define BP_CSCDR2_IPU2_DI0_PRE_PODF	3
#define BM_CSCDR2_IPU2_DI0_PRE_PODF	(0x7 << 3)
#define BP_CSCDR2_IPU2_DI0_PRE_SEL	6
#define BM_CSCDR2_IPU2_DI0_PRE_SEL	(0x7 << 6)
#define BP_CSCDR2_IPU2_DI1_SEL		9
#define BM_CSCDR2_IPU2_DI1_SEL		(0x7 << 9)
#define BP_CSCDR2_IPU2_DI1_PRE_PODF	12
#define BM_CSCDR2_IPU2_DI1_PRE_PODF	(0x7 << 12)
#define BP_CSCDR2_IPU2_DI1_PRE_SEL	15
#define BM_CSCDR2_IPU2_DI1_PRE_SEL	(0x7 << 15)
#define BP_CSCDR2_ECSPI_CLK_PODF	19
#define BM_CSCDR2_ECSPI_CLK_PODF	(0x3f << 19)

#define BP_CSCDR3_IPU1_HSP_SEL		9
#define BM_CSCDR3_IPU1_HSP_SEL		(0x3 << 9)
#define BP_CSCDR3_IPU1_HSP_PODF		11
#define BM_CSCDR3_IPU1_HSP_PODF		(0x7 << 11)
#define BP_CSCDR3_IPU2_HSP_SEL		14
#define BM_CSCDR3_IPU2_HSP_SEL		(0x3 << 14)
#define BP_CSCDR3_IPU2_HSP_PODF		16
#define BM_CSCDR3_IPU2_HSP_PODF		(0x7 << 16)

#define BM_CDHIPR_AXI_PODF_BUSY		(0x1 << 0)
#define BM_CDHIPR_AHB_PODF_BUSY		(0x1 << 1)
#define BM_CDHIPR_MMDC_CH1_PODF_BUSY	(0x1 << 2)
#define BM_CDHIPR_PERIPH2_SEL_BUSY	(0x1 << 3)
#define BM_CDHIPR_MMDC_CH0_PODF_BUSY	(0x1 << 4)
#define BM_CDHIPR_PERIPH_SEL_BUSY	(0x1 << 5)
#define BM_CDHIPR_ARM_PODF_BUSY		(0x1 << 16)

#define BP_CLPCR_LPM			0
#define BM_CLPCR_LPM			(0x3 << 0)
#define BM_CLPCR_BYPASS_PMIC_READY	(0x1 << 2)
#define BM_CLPCR_ARM_CLK_DIS_ON_LPM	(0x1 << 5)
#define BM_CLPCR_SBYOS			(0x1 << 6)
#define BM_CLPCR_DIS_REF_OSC		(0x1 << 7)
#define BM_CLPCR_VSTBY			(0x1 << 8)
#define BP_CLPCR_STBY_COUNT		9
#define BM_CLPCR_STBY_COUNT		(0x3 << 9)
#define BM_CLPCR_COSC_PWRDOWN		(0x1 << 11)
#define BM_CLPCR_WB_PER_AT_LPM		(0x1 << 16)
#define BM_CLPCR_WB_CORE_AT_LPM		(0x1 << 17)
#define BM_CLPCR_BYP_MMDC_CH0_LPM_HS	(0x1 << 19)
#define BM_CLPCR_BYP_MMDC_CH1_LPM_HS	(0x1 << 21)
#define BM_CLPCR_MASK_CORE0_WFI		(0x1 << 22)
#define BM_CLPCR_MASK_CORE1_WFI		(0x1 << 23)
#define BM_CLPCR_MASK_CORE2_WFI		(0x1 << 24)
#define BM_CLPCR_MASK_CORE3_WFI		(0x1 << 25)
#define BM_CLPCR_MASK_SCU_IDLE		(0x1 << 26)
#define BM_CLPCR_MASK_L2CC_IDLE		(0x1 << 27)

#define FREQ_480M	480000000
#define FREQ_528M	528000000
#define FREQ_594M	594000000
#define FREQ_650M	650000000
#define FREQ_1300M	1300000000

static struct clk pll1_sys;
static struct clk pll2_bus;
static struct clk pll3_usb_otg;
static struct clk pll4_audio;
static struct clk pll5_video;
static struct clk pll6_mlb;
static struct clk pll7_usb_host;
static struct clk pll8_enet;
static struct clk apbh_dma_clk;
static struct clk arm_clk;
static struct clk ipg_clk;
static struct clk ahb_clk;
static struct clk axi_clk;
static struct clk mmdc_ch0_axi_clk;
static struct clk mmdc_ch1_axi_clk;
static struct clk periph_clk;
static struct clk periph_pre_clk;
static struct clk periph_clk2_clk;
static struct clk periph2_clk;
static struct clk periph2_pre_clk;
static struct clk periph2_clk2_clk;
static struct clk gpu2d_core_clk;
static struct clk gpu3d_core_clk;
static struct clk gpu3d_shader_clk;
static struct clk ipg_perclk;
static struct clk emi_clk;
static struct clk emi_slow_clk;
static struct clk can1_clk;
static struct clk uart_clk;
static struct clk usdhc1_clk;
static struct clk usdhc2_clk;
static struct clk usdhc3_clk;
static struct clk usdhc4_clk;
static struct clk vpu_clk;
static struct clk hsi_tx_clk;
static struct clk ipu1_di0_pre_clk;
static struct clk ipu1_di1_pre_clk;
static struct clk ipu2_di0_pre_clk;
static struct clk ipu2_di1_pre_clk;
static struct clk ipu1_clk;
static struct clk ipu2_clk;
static struct clk ssi1_clk;
static struct clk ssi3_clk;
static struct clk esai_clk;
static struct clk ssi2_clk;
static struct clk spdif_clk;
static struct clk asrc_serial_clk;
static struct clk gpu2d_axi_clk;
static struct clk gpu3d_axi_clk;
static struct clk pcie_clk;
static struct clk vdo_axi_clk;
static struct clk ldb_di0_clk;
static struct clk ldb_di1_clk;
static struct clk ipu1_di0_clk;
static struct clk ipu1_di1_clk;
static struct clk ipu2_di0_clk;
static struct clk ipu2_di1_clk;
static struct clk enfc_clk;
static struct clk dummy_clk = {};

static unsigned long external_high_reference;
static unsigned long external_low_reference;
static unsigned long oscillator_reference;

static unsigned long get_oscillator_reference_clock_rate(struct clk *clk)
{
	return oscillator_reference;
}

static unsigned long get_high_reference_clock_rate(struct clk *clk)
{
	return external_high_reference;
}

static unsigned long get_low_reference_clock_rate(struct clk *clk)
{
	return external_low_reference;
}

static struct clk ckil_clk = {
	.get_rate = get_low_reference_clock_rate,
};

static struct clk ckih_clk = {
	.get_rate = get_high_reference_clock_rate,
};

static struct clk osc_clk = {
	.get_rate = get_oscillator_reference_clock_rate,
};

static inline void __iomem *pll_get_reg_addr(struct clk *pll)
{
	if (pll == &pll1_sys)
		return PLL1_SYS;
	else if (pll == &pll2_bus)
		return PLL2_BUS;
	else if (pll == &pll3_usb_otg)
		return PLL3_USB_OTG;
	else if (pll == &pll4_audio)
		return PLL4_AUDIO;
	else if (pll == &pll5_video)
		return PLL5_VIDEO;
	else if (pll == &pll6_mlb)
		return PLL6_MLB;
	else if (pll == &pll7_usb_host)
		return PLL7_USB_HOST;
	else if (pll == &pll8_enet)
		return PLL8_ENET;
	else
		BUG();

	return NULL;
}

static int pll_enable(struct clk *clk)
{
	int timeout = 0x100000;
	void __iomem *reg;
	u32 val;

	reg = pll_get_reg_addr(clk);
	val = readl_relaxed(reg);
	val &= ~BM_PLL_BYPASS;
	val &= ~BM_PLL_POWER_DOWN;
	/* 480MHz PLLs have the opposite definition for power bit */
	if (clk == &pll3_usb_otg || clk == &pll7_usb_host)
		val |= BM_PLL_POWER_DOWN;
	writel_relaxed(val, reg);

	/* Wait for PLL to lock */
	while (!(readl_relaxed(reg) & BM_PLL_LOCK) && --timeout)
		cpu_relax();

	if (unlikely(!timeout))
		return -EBUSY;

	/* Enable the PLL output now */
	val = readl_relaxed(reg);
	val |= BM_PLL_ENABLE;
	writel_relaxed(val, reg);

	return 0;
}

static void pll_disable(struct clk *clk)
{
	void __iomem *reg;
	u32 val;

	reg = pll_get_reg_addr(clk);
	val = readl_relaxed(reg);
	val &= ~BM_PLL_ENABLE;
	val |= BM_PLL_BYPASS;
	val |= BM_PLL_POWER_DOWN;
	if (clk == &pll3_usb_otg || clk == &pll7_usb_host)
		val &= ~BM_PLL_POWER_DOWN;
	writel_relaxed(val, reg);
}

static unsigned long pll1_sys_get_rate(struct clk *clk)
{
	u32 div = (readl_relaxed(PLL1_SYS) & BM_PLL_SYS_DIV_SELECT) >>
		  BP_PLL_SYS_DIV_SELECT;

	return clk_get_rate(clk->parent) * div / 2;
}

static int pll1_sys_set_rate(struct clk *clk, unsigned long rate)
{
	u32 val, div;

	if (rate < FREQ_650M || rate > FREQ_1300M)
		return -EINVAL;

	div = rate * 2 / clk_get_rate(clk->parent);
	val = readl_relaxed(PLL1_SYS);
	val &= ~BM_PLL_SYS_DIV_SELECT;
	val |= div << BP_PLL_SYS_DIV_SELECT;
	writel_relaxed(val, PLL1_SYS);

	return 0;
}

static unsigned long pll8_enet_get_rate(struct clk *clk)
{
	u32 div = (readl_relaxed(PLL8_ENET) & BM_PLL_ENET_DIV_SELECT) >>
		  BP_PLL_ENET_DIV_SELECT;

	switch (div) {
	case 0:
		return 25000000;
	case 1:
		return 50000000;
	case 2:
		return 100000000;
	case 3:
		return 125000000;
	}

	return 0;
}

static int pll8_enet_set_rate(struct clk *clk, unsigned long rate)
{
	u32 val, div;

	switch (rate) {
	case 25000000:
		div = 0;
		break;
	case 50000000:
		div = 1;
		break;
	case 100000000:
		div = 2;
		break;
	case 125000000:
		div = 3;
		break;
	default:
		return -EINVAL;
	}

	val = readl_relaxed(PLL8_ENET);
	val &= ~BM_PLL_ENET_DIV_SELECT;
	val |= div << BP_PLL_ENET_DIV_SELECT;
	writel_relaxed(val, PLL8_ENET);

	return 0;
}

static unsigned long pll_av_get_rate(struct clk *clk)
{
	void __iomem *reg = (clk == &pll4_audio) ? PLL4_AUDIO : PLL5_VIDEO;
	unsigned long parent_rate = clk_get_rate(clk->parent);
	u32 mfn = readl_relaxed(reg + PLL_NUM_OFFSET);
	u32 mfd = readl_relaxed(reg + PLL_DENOM_OFFSET);
	u32 div = (readl_relaxed(reg) & BM_PLL_AV_DIV_SELECT) >>
		  BP_PLL_AV_DIV_SELECT;

	return (parent_rate * div) + ((parent_rate / mfd) * mfn);
}

static int pll_av_set_rate(struct clk *clk, unsigned long rate)
{
	void __iomem *reg = (clk == &pll4_audio) ? PLL4_AUDIO : PLL5_VIDEO;
	unsigned int parent_rate = clk_get_rate(clk->parent);
	u32 val, div;
	u32 mfn, mfd = 1000000;
	s64 temp64;

	if (rate < FREQ_650M || rate > FREQ_1300M)
		return -EINVAL;

	div = rate / parent_rate;
	temp64 = (u64) (rate - div * parent_rate);
	temp64 *= mfd;
	do_div(temp64, parent_rate);
	mfn = temp64;

	val = readl_relaxed(reg);
	val &= ~BM_PLL_AV_DIV_SELECT;
	val |= div << BP_PLL_AV_DIV_SELECT;
	writel_relaxed(val, reg);
	writel_relaxed(mfn, reg + PLL_NUM_OFFSET);
	writel_relaxed(mfd, reg + PLL_DENOM_OFFSET);

	return 0;
}

static void __iomem *pll_get_div_reg_bit(struct clk *clk, u32 *bp, u32 *bm)
{
	void __iomem *reg;

	if (clk == &pll2_bus) {
		reg = PLL2_BUS;
		*bp = BP_PLL_BUS_DIV_SELECT;
		*bm = BM_PLL_BUS_DIV_SELECT;
	} else if (clk == &pll3_usb_otg) {
		reg = PLL3_USB_OTG;
		*bp = BP_PLL_USB_DIV_SELECT;
		*bm = BM_PLL_USB_DIV_SELECT;
	} else if (clk == &pll7_usb_host) {
		reg = PLL7_USB_HOST;
		*bp = BP_PLL_USB_DIV_SELECT;
		*bm = BM_PLL_USB_DIV_SELECT;
	} else {
		BUG();
	}

	return reg;
}

static unsigned long pll_get_rate(struct clk *clk)
{
	void __iomem *reg;
	u32 div, bp, bm;

	reg = pll_get_div_reg_bit(clk, &bp, &bm);
	div = (readl_relaxed(reg) & bm) >> bp;

	return (div == 1) ? clk_get_rate(clk->parent) * 22 :
			    clk_get_rate(clk->parent) * 20;
}

static int pll_set_rate(struct clk *clk, unsigned long rate)
{
	void __iomem *reg;
	u32 val, div, bp, bm;

	if (rate == FREQ_528M)
		div = 1;
	else if (rate == FREQ_480M)
		div = 0;
	else
		return -EINVAL;

	reg = pll_get_div_reg_bit(clk, &bp, &bm);
	val = readl_relaxed(reg);
	val &= ~bm;
	val |= div << bp;
	writel_relaxed(val, reg);

	return 0;
}

#define pll2_bus_get_rate	pll_get_rate
#define pll2_bus_set_rate	pll_set_rate
#define pll3_usb_otg_get_rate	pll_get_rate
#define pll3_usb_otg_set_rate	pll_set_rate
#define pll7_usb_host_get_rate	pll_get_rate
#define pll7_usb_host_set_rate	pll_set_rate
#define pll4_audio_get_rate	pll_av_get_rate
#define pll4_audio_set_rate	pll_av_set_rate
#define pll5_video_get_rate	pll_av_get_rate
#define pll5_video_set_rate	pll_av_set_rate
#define pll6_mlb_get_rate	NULL
#define pll6_mlb_set_rate	NULL

#define DEF_PLL(name)					\
	static struct clk name = {			\
		.enable		= pll_enable,		\
		.disable	= pll_disable,		\
		.get_rate	= name##_get_rate,	\
		.set_rate	= name##_set_rate,	\
		.parent		= &osc_clk,		\
	}

DEF_PLL(pll1_sys);
DEF_PLL(pll2_bus);
DEF_PLL(pll3_usb_otg);
DEF_PLL(pll4_audio);
DEF_PLL(pll5_video);
DEF_PLL(pll6_mlb);
DEF_PLL(pll7_usb_host);
DEF_PLL(pll8_enet);

static unsigned long pfd_get_rate(struct clk *clk)
{
	u64 tmp = (u64) clk_get_rate(clk->parent) * 18;
	u32 frac, bp_frac;

	if (apbh_dma_clk.usecount == 0)
		apbh_dma_clk.enable(&apbh_dma_clk);

	bp_frac = clk->enable_shift - 7;
	frac = readl_relaxed(clk->enable_reg) >> bp_frac & PFD_FRAC_MASK;
	do_div(tmp, frac);

	return tmp;
}

static int pfd_set_rate(struct clk *clk, unsigned long rate)
{
	u32 val, frac, bp_frac;
	u64 tmp = (u64) clk_get_rate(clk->parent) * 18;

	if (apbh_dma_clk.usecount == 0)
		apbh_dma_clk.enable(&apbh_dma_clk);

	/*
	 * Round up the divider so that we don't set a rate
	 * higher than what is requested
	 */
	tmp += rate / 2;
	do_div(tmp, rate);
	frac = tmp;
	frac = (frac < 12) ? 12 : frac;
	frac = (frac > 35) ? 35 : frac;

	/*
	 * The frac field always starts from 7 bits lower
	 * position of enable bit
	 */
	bp_frac = clk->enable_shift - 7;
	val = readl_relaxed(clk->enable_reg);
	val &= ~(PFD_FRAC_MASK << bp_frac);
	val |= frac << bp_frac;
	writel_relaxed(val, clk->enable_reg);

	tmp = (u64) clk_get_rate(clk->parent) * 18;
	do_div(tmp, frac);

	if (apbh_dma_clk.usecount == 0)
		apbh_dma_clk.disable(&apbh_dma_clk);

	return 0;
}

static unsigned long pfd_round_rate(struct clk *clk, unsigned long rate)
{
	u32 frac;
	u64 tmp;

	tmp = (u64) clk_get_rate(clk->parent) * 18;
	tmp += rate / 2;
	do_div(tmp, rate);
	frac = tmp;
	frac = (frac < 12) ? 12 : frac;
	frac = (frac > 35) ? 35 : frac;
	tmp = (u64) clk_get_rate(clk->parent) * 18;
	do_div(tmp, frac);

	return tmp;
}

static int pfd_enable(struct clk *clk)
{
	u32 val;

	if (apbh_dma_clk.usecount == 0)
		apbh_dma_clk.enable(&apbh_dma_clk);

	val = readl_relaxed(clk->enable_reg);
	val &= ~(1 << clk->enable_shift);
	writel_relaxed(val, clk->enable_reg);

	if (apbh_dma_clk.usecount == 0)
		apbh_dma_clk.disable(&apbh_dma_clk);

	return 0;
}

static void pfd_disable(struct clk *clk)
{
	u32 val;

	if (apbh_dma_clk.usecount == 0)
		apbh_dma_clk.enable(&apbh_dma_clk);

	val = readl_relaxed(clk->enable_reg);
	val |= 1 << clk->enable_shift;
	writel_relaxed(val, clk->enable_reg);

	if (apbh_dma_clk.usecount == 0)
		apbh_dma_clk.disable(&apbh_dma_clk);
}

#define DEF_PFD(name, er, es, p)			\
	static struct clk name = {			\
		.enable_reg	= er,			\
		.enable_shift	= es,			\
		.enable		= pfd_enable,		\
		.disable	= pfd_disable,		\
		.get_rate	= pfd_get_rate,		\
		.set_rate	= pfd_set_rate,		\
		.round_rate	= pfd_round_rate,	\
		.parent		= p,			\
	}

DEF_PFD(pll2_pfd_352m, PFD_528, PFD0, &pll2_bus);
DEF_PFD(pll2_pfd_594m, PFD_528, PFD1, &pll2_bus);
DEF_PFD(pll2_pfd_400m, PFD_528, PFD2, &pll2_bus);
DEF_PFD(pll3_pfd_720m, PFD_480, PFD0, &pll3_usb_otg);
DEF_PFD(pll3_pfd_540m, PFD_480, PFD1, &pll3_usb_otg);
DEF_PFD(pll3_pfd_508m, PFD_480, PFD2, &pll3_usb_otg);
DEF_PFD(pll3_pfd_454m, PFD_480, PFD3, &pll3_usb_otg);

static unsigned long pll2_200m_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / 2;
}

static struct clk pll2_200m = {
	.parent = &pll2_pfd_400m,
	.get_rate = pll2_200m_get_rate,
};

static unsigned long pll3_120m_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / 4;
}

static struct clk pll3_120m = {
	.parent = &pll3_usb_otg,
	.get_rate = pll3_120m_get_rate,
};

static unsigned long pll3_80m_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / 6;
}

static struct clk pll3_80m = {
	.parent = &pll3_usb_otg,
	.get_rate = pll3_80m_get_rate,
};

static unsigned long pll3_60m_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / 8;
}

static struct clk pll3_60m = {
	.parent = &pll3_usb_otg,
	.get_rate = pll3_60m_get_rate,
};

static int pll1_sw_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 val = readl_relaxed(CCSR);

	if (parent == &pll1_sys) {
		val &= ~BM_CCSR_PLL1_SW_SEL;
		val &= ~BM_CCSR_STEP_SEL;
	} else if (parent == &osc_clk) {
		val |= BM_CCSR_PLL1_SW_SEL;
		val &= ~BM_CCSR_STEP_SEL;
	} else if (parent == &pll2_pfd_400m) {
		val |= BM_CCSR_PLL1_SW_SEL;
		val |= BM_CCSR_STEP_SEL;
	} else {
		return -EINVAL;
	}

	writel_relaxed(val, CCSR);

	return 0;
}

static struct clk pll1_sw_clk = {
	.parent = &pll1_sys,
	.set_parent = pll1_sw_clk_set_parent,
};

static void calc_pred_podf_dividers(u32 div, u32 *pred, u32 *podf)
{
	u32 min_pred, temp_pred, old_err, err;

	if (div >= 512) {
		*pred = 8;
		*podf = 64;
	} else if (div >= 8) {
		min_pred = (div - 1) / 64 + 1;
		old_err = 8;
		for (temp_pred = 8; temp_pred >= min_pred; temp_pred--) {
			err = div % temp_pred;
			if (err == 0) {
				*pred = temp_pred;
				break;
			}
			err = temp_pred - err;
			if (err < old_err) {
				old_err = err;
				*pred = temp_pred;
			}
		}
		*podf = (div + *pred - 1) / *pred;
	} else if (div < 8) {
		*pred = div;
		*podf = 1;
	}
}

static int _clk_enable(struct clk *clk)
{
	u32 reg;
	reg = readl_relaxed(clk->enable_reg);
	reg |= 0x3 << clk->enable_shift;
	writel_relaxed(reg, clk->enable_reg);

	return 0;
}

static void _clk_disable(struct clk *clk)
{
	u32 reg;
	reg = readl_relaxed(clk->enable_reg);
	reg &= ~(0x3 << clk->enable_shift);
	writel_relaxed(reg, clk->enable_reg);
}

struct divider {
	struct clk *clk;
	void __iomem *reg;
	u32 bp_pred;
	u32 bm_pred;
	u32 bp_podf;
	u32 bm_podf;
};

#define DEF_CLK_DIV1(d, c, r, b)				\
	static struct divider d = {				\
		.clk = c,					\
		.reg = r,					\
		.bp_podf = BP_##r##_##b##_PODF,			\
		.bm_podf = BM_##r##_##b##_PODF,			\
	}

DEF_CLK_DIV1(arm_div,		&arm_clk,		CACRR,	ARM);
DEF_CLK_DIV1(ipg_div,		&ipg_clk,		CBCDR,	IPG);
DEF_CLK_DIV1(ahb_div,		&ahb_clk,		CBCDR,	AHB);
DEF_CLK_DIV1(axi_div,		&axi_clk,		CBCDR,	AXI);
DEF_CLK_DIV1(mmdc_ch0_axi_div,	&mmdc_ch0_axi_clk,	CBCDR,	MMDC_CH0_AXI);
DEF_CLK_DIV1(mmdc_ch1_axi_div,	&mmdc_ch1_axi_clk,	CBCDR,	MMDC_CH1_AXI);
DEF_CLK_DIV1(periph_clk2_div,	&periph_clk2_clk,	CBCDR,	PERIPH_CLK2);
DEF_CLK_DIV1(periph2_clk2_div,	&periph2_clk2_clk,	CBCDR,	PERIPH2_CLK2);
DEF_CLK_DIV1(gpu2d_core_div,	&gpu2d_core_clk,	CBCMR,	GPU2D_CORE);
DEF_CLK_DIV1(gpu3d_core_div,	&gpu3d_core_clk,	CBCMR,	GPU3D_CORE);
DEF_CLK_DIV1(gpu3d_shader_div,	&gpu3d_shader_clk,	CBCMR,	GPU3D_SHADER);
DEF_CLK_DIV1(ipg_perclk_div,	&ipg_perclk,		CSCMR1,	PERCLK);
DEF_CLK_DIV1(emi_div,		&emi_clk,		CSCMR1,	EMI);
DEF_CLK_DIV1(emi_slow_div,	&emi_slow_clk,		CSCMR1,	EMI_SLOW);
DEF_CLK_DIV1(can_div,		&can1_clk,		CSCMR2,	CAN);
DEF_CLK_DIV1(uart_div,		&uart_clk,		CSCDR1,	UART);
DEF_CLK_DIV1(usdhc1_div,	&usdhc1_clk,		CSCDR1,	USDHC1);
DEF_CLK_DIV1(usdhc2_div,	&usdhc2_clk,		CSCDR1,	USDHC2);
DEF_CLK_DIV1(usdhc3_div,	&usdhc3_clk,		CSCDR1,	USDHC3);
DEF_CLK_DIV1(usdhc4_div,	&usdhc4_clk,		CSCDR1,	USDHC4);
DEF_CLK_DIV1(vpu_div,		&vpu_clk,		CSCDR1,	VPU_AXI);
DEF_CLK_DIV1(hsi_tx_div,	&hsi_tx_clk,		CDCDR,	HSI_TX);
DEF_CLK_DIV1(ipu1_di0_pre_div,	&ipu1_di0_pre_clk,	CHSCCDR, IPU1_DI0_PRE);
DEF_CLK_DIV1(ipu1_di1_pre_div,	&ipu1_di1_pre_clk,	CHSCCDR, IPU1_DI1_PRE);
DEF_CLK_DIV1(ipu2_di0_pre_div,	&ipu2_di0_pre_clk,	CSCDR2,	IPU2_DI0_PRE);
DEF_CLK_DIV1(ipu2_di1_pre_div,	&ipu2_di1_pre_clk,	CSCDR2,	IPU2_DI1_PRE);
DEF_CLK_DIV1(ipu1_div,		&ipu1_clk,		CSCDR3,	IPU1_HSP);
DEF_CLK_DIV1(ipu2_div,		&ipu2_clk,		CSCDR3,	IPU2_HSP);

#define DEF_CLK_DIV2(d, c, r, b)				\
	static struct divider d = {				\
		.clk = c,					\
		.reg = r,					\
		.bp_pred = BP_##r##_##b##_PRED,			\
		.bm_pred = BM_##r##_##b##_PRED,			\
		.bp_podf = BP_##r##_##b##_PODF,			\
		.bm_podf = BM_##r##_##b##_PODF,			\
	}

DEF_CLK_DIV2(ssi1_div,		&ssi1_clk,		CS1CDR,	SSI1);
DEF_CLK_DIV2(ssi3_div,		&ssi3_clk,		CS1CDR,	SSI3);
DEF_CLK_DIV2(esai_div,		&esai_clk,		CS1CDR,	ESAI);
DEF_CLK_DIV2(ssi2_div,		&ssi2_clk,		CS2CDR,	SSI2);
DEF_CLK_DIV2(enfc_div,		&enfc_clk,		CS2CDR,	ENFC);
DEF_CLK_DIV2(spdif_div,		&spdif_clk,		CDCDR,	SPDIF);
DEF_CLK_DIV2(asrc_serial_div,	&asrc_serial_clk,	CDCDR,	ASRC_SERIAL);

static struct divider *dividers[] = {
	&arm_div,
	&ipg_div,
	&ahb_div,
	&axi_div,
	&mmdc_ch0_axi_div,
	&mmdc_ch1_axi_div,
	&periph_clk2_div,
	&periph2_clk2_div,
	&gpu2d_core_div,
	&gpu3d_core_div,
	&gpu3d_shader_div,
	&ipg_perclk_div,
	&emi_div,
	&emi_slow_div,
	&can_div,
	&uart_div,
	&usdhc1_div,
	&usdhc2_div,
	&usdhc3_div,
	&usdhc4_div,
	&vpu_div,
	&hsi_tx_div,
	&ipu1_di0_pre_div,
	&ipu1_di1_pre_div,
	&ipu2_di0_pre_div,
	&ipu2_di1_pre_div,
	&ipu1_div,
	&ipu2_div,
	&ssi1_div,
	&ssi3_div,
	&esai_div,
	&ssi2_div,
	&enfc_div,
	&spdif_div,
	&asrc_serial_div,
};

static unsigned long ldb_di_clk_get_rate(struct clk *clk)
{
	u32 val = readl_relaxed(CSCMR2);

	val &= (clk == &ldb_di0_clk) ? BM_CSCMR2_LDB_DI0_IPU_DIV :
				       BM_CSCMR2_LDB_DI1_IPU_DIV;
	if (val)
		return clk_get_rate(clk->parent) / 7;
	else
		return clk_get_rate(clk->parent) * 2 / 7;
}

static int ldb_di_clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	u32 val = readl_relaxed(CSCMR2);

	if (rate * 7 <= parent_rate + parent_rate / 20)
		val |= BM_CSCMR2_LDB_DI0_IPU_DIV;
	else
		val &= ~BM_CSCMR2_LDB_DI0_IPU_DIV;

	writel_relaxed(val, CSCMR2);

	return 0;
}

static unsigned long ldb_di_clk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);

	if (rate * 7 <= parent_rate + parent_rate / 20)
		return parent_rate / 7;
	else
		return 2 * parent_rate / 7;
}

static unsigned long _clk_get_rate(struct clk *clk)
{
	struct divider *d;
	u32 val, pred, podf;
	int i, num;

	if (clk == &ldb_di0_clk || clk == &ldb_di1_clk)
		return ldb_di_clk_get_rate(clk);

	num = ARRAY_SIZE(dividers);
	for (i = 0; i < num; i++)
		if (dividers[i]->clk == clk) {
			d = dividers[i];
			break;
		}
	if (i == num)
		return clk_get_rate(clk->parent);

	val = readl_relaxed(d->reg);
	pred = ((val & d->bm_pred) >> d->bp_pred) + 1;
	podf = ((val & d->bm_podf) >> d->bp_podf) + 1;

	return clk_get_rate(clk->parent) / (pred * podf);
}

static int clk_busy_wait(struct clk *clk)
{
	int timeout = 0x100000;
	u32 bm;

	if (clk == &axi_clk)
		bm = BM_CDHIPR_AXI_PODF_BUSY;
	else if (clk == &ahb_clk)
		bm = BM_CDHIPR_AHB_PODF_BUSY;
	else if (clk == &mmdc_ch0_axi_clk)
		bm = BM_CDHIPR_MMDC_CH0_PODF_BUSY;
	else if (clk == &periph_clk)
		bm = BM_CDHIPR_PERIPH_SEL_BUSY;
	else if (clk == &arm_clk)
		bm = BM_CDHIPR_ARM_PODF_BUSY;
	else
		return -EINVAL;

	while ((readl_relaxed(CDHIPR) & bm) && --timeout)
		cpu_relax();

	if (unlikely(!timeout))
		return -EBUSY;

	return 0;
}

static int _clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	struct divider *d;
	u32 val, div, max_div, pred = 0, podf;
	int i, num;

	if (clk == &ldb_di0_clk || clk == &ldb_di1_clk)
		return ldb_di_clk_set_rate(clk, rate);

	num = ARRAY_SIZE(dividers);
	for (i = 0; i < num; i++)
		if (dividers[i]->clk == clk) {
			d = dividers[i];
			break;
		}
	if (i == num)
		return -EINVAL;

	max_div = ((d->bm_pred >> d->bp_pred) + 1) *
		  ((d->bm_pred >> d->bp_pred) + 1);

	div = parent_rate / rate;
	if (div == 0)
		div++;

	if ((parent_rate / div != rate) || div > max_div)
		return -EINVAL;

	if (d->bm_pred) {
		calc_pred_podf_dividers(div, &pred, &podf);
	} else {
		pred = 1;
		podf = div;
	}

	val = readl_relaxed(d->reg);
	val &= ~(d->bm_pred | d->bm_podf);
	val |= (pred - 1) << d->bp_pred | (podf - 1) << d->bp_podf;
	writel_relaxed(val, d->reg);

	if (clk == &axi_clk || clk == &ahb_clk ||
	    clk == &mmdc_ch0_axi_clk || clk == &arm_clk)
		return clk_busy_wait(clk);

	return 0;
}

static unsigned long _clk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	u32 div = parent_rate / rate;
	u32 div_max, pred = 0, podf;
	struct divider *d;
	int i, num;

	if (clk == &ldb_di0_clk || clk == &ldb_di1_clk)
		return ldb_di_clk_round_rate(clk, rate);

	num = ARRAY_SIZE(dividers);
	for (i = 0; i < num; i++)
		if (dividers[i]->clk == clk) {
			d = dividers[i];
			break;
		}
	if (i == num)
		return -EINVAL;

	if (div == 0 || parent_rate % rate)
		div++;

	if (d->bm_pred) {
		calc_pred_podf_dividers(div, &pred, &podf);
		div = pred * podf;
	} else {
		div_max = (d->bm_podf >> d->bp_podf) + 1;
		if (div > div_max)
			div = div_max;
	}

	return parent_rate / div;
}

struct multiplexer {
	struct clk *clk;
	void __iomem *reg;
	u32 bp;
	u32 bm;
	int pnum;
	struct clk *parents[];
};

static struct multiplexer axi_mux = {
	.clk = &axi_clk,
	.reg = CBCDR,
	.bp = BP_CBCDR_AXI_SEL,
	.bm = BM_CBCDR_AXI_SEL,
	.parents = {
		&periph_clk,
		&pll2_pfd_400m,
		&pll3_pfd_540m,
		NULL
	},
};

static struct multiplexer periph_mux = {
	.clk = &periph_clk,
	.reg = CBCDR,
	.bp = BP_CBCDR_PERIPH_CLK_SEL,
	.bm = BM_CBCDR_PERIPH_CLK_SEL,
	.parents = {
		&periph_pre_clk,
		&periph_clk2_clk,
		NULL
	},
};

static struct multiplexer periph_pre_mux = {
	.clk = &periph_pre_clk,
	.reg = CBCMR,
	.bp = BP_CBCMR_PRE_PERIPH_CLK_SEL,
	.bm = BM_CBCMR_PRE_PERIPH_CLK_SEL,
	.parents = {
		&pll2_bus,
		&pll2_pfd_400m,
		&pll2_pfd_352m,
		&pll2_200m,
		NULL
	},
};

static struct multiplexer periph_clk2_mux = {
	.clk = &periph_clk2_clk,
	.reg = CBCMR,
	.bp = BP_CBCMR_PERIPH_CLK2_SEL,
	.bm = BM_CBCMR_PERIPH_CLK2_SEL,
	.parents = {
		&pll3_usb_otg,
		&osc_clk,
		NULL
	},
};

static struct multiplexer periph2_mux = {
	.clk = &periph2_clk,
	.reg = CBCDR,
	.bp = BP_CBCDR_PERIPH2_CLK_SEL,
	.bm = BM_CBCDR_PERIPH2_CLK_SEL,
	.parents = {
		&periph2_pre_clk,
		&periph2_clk2_clk,
		NULL
	},
};

static struct multiplexer periph2_pre_mux = {
	.clk = &periph2_pre_clk,
	.reg = CBCMR,
	.bp = BP_CBCMR_PRE_PERIPH2_CLK_SEL,
	.bm = BM_CBCMR_PRE_PERIPH2_CLK_SEL,
	.parents = {
		&pll2_bus,
		&pll2_pfd_400m,
		&pll2_pfd_352m,
		&pll2_200m,
		NULL
	},
};

static struct multiplexer periph2_clk2_mux = {
	.clk = &periph2_clk2_clk,
	.reg = CBCMR,
	.bp = BP_CBCMR_PERIPH2_CLK2_SEL,
	.bm = BM_CBCMR_PERIPH2_CLK2_SEL,
	.parents = {
		&pll3_usb_otg,
		&osc_clk,
		NULL
	},
};

static struct multiplexer gpu2d_axi_mux = {
	.clk = &gpu2d_axi_clk,
	.reg = CBCMR,
	.bp = BP_CBCMR_GPU2D_AXI_SEL,
	.bm = BM_CBCMR_GPU2D_AXI_SEL,
	.parents = {
		&axi_clk,
		&ahb_clk,
		NULL
	},
};

static struct multiplexer gpu3d_axi_mux = {
	.clk = &gpu3d_axi_clk,
	.reg = CBCMR,
	.bp = BP_CBCMR_GPU3D_AXI_SEL,
	.bm = BM_CBCMR_GPU3D_AXI_SEL,
	.parents = {
		&axi_clk,
		&ahb_clk,
		NULL
	},
};

static struct multiplexer gpu3d_core_mux = {
	.clk = &gpu3d_core_clk,
	.reg = CBCMR,
	.bp = BP_CBCMR_GPU3D_CORE_SEL,
	.bm = BM_CBCMR_GPU3D_CORE_SEL,
	.parents = {
		&mmdc_ch0_axi_clk,
		&pll3_usb_otg,
		&pll2_pfd_594m,
		&pll2_pfd_400m,
		NULL
	},
};

static struct multiplexer gpu3d_shader_mux = {
	.clk = &gpu3d_shader_clk,
	.reg = CBCMR,
	.bp = BP_CBCMR_GPU3D_SHADER_SEL,
	.bm = BM_CBCMR_GPU3D_SHADER_SEL,
	.parents = {
		&mmdc_ch0_axi_clk,
		&pll3_usb_otg,
		&pll2_pfd_594m,
		&pll3_pfd_720m,
		NULL
	},
};

static struct multiplexer pcie_axi_mux = {
	.clk = &pcie_clk,
	.reg = CBCMR,
	.bp = BP_CBCMR_PCIE_AXI_SEL,
	.bm = BM_CBCMR_PCIE_AXI_SEL,
	.parents = {
		&axi_clk,
		&ahb_clk,
		NULL
	},
};

static struct multiplexer vdo_axi_mux = {
	.clk = &vdo_axi_clk,
	.reg = CBCMR,
	.bp = BP_CBCMR_VDO_AXI_SEL,
	.bm = BM_CBCMR_VDO_AXI_SEL,
	.parents = {
		&axi_clk,
		&ahb_clk,
		NULL
	},
};

static struct multiplexer vpu_axi_mux = {
	.clk = &vpu_clk,
	.reg = CBCMR,
	.bp = BP_CBCMR_VPU_AXI_SEL,
	.bm = BM_CBCMR_VPU_AXI_SEL,
	.parents = {
		&axi_clk,
		&pll2_pfd_400m,
		&pll2_pfd_352m,
		NULL
	},
};

static struct multiplexer gpu2d_core_mux = {
	.clk = &gpu2d_core_clk,
	.reg = CBCMR,
	.bp = BP_CBCMR_GPU2D_CORE_SEL,
	.bm = BM_CBCMR_GPU2D_CORE_SEL,
	.parents = {
		&axi_clk,
		&pll3_usb_otg,
		&pll2_pfd_352m,
		&pll2_pfd_400m,
		NULL
	},
};

#define DEF_SSI_MUX(id)							\
	static struct multiplexer ssi##id##_mux = {			\
		.clk = &ssi##id##_clk,					\
		.reg = CSCMR1,						\
		.bp = BP_CSCMR1_SSI##id##_SEL,				\
		.bm = BM_CSCMR1_SSI##id##_SEL,				\
		.parents = {						\
			&pll3_pfd_508m,					\
			&pll3_pfd_454m,					\
			&pll4_audio,					\
			NULL						\
		},							\
	}

DEF_SSI_MUX(1);
DEF_SSI_MUX(2);
DEF_SSI_MUX(3);

#define DEF_USDHC_MUX(id)						\
	static struct multiplexer usdhc##id##_mux = {			\
		.clk = &usdhc##id##_clk,				\
		.reg = CSCMR1,						\
		.bp = BP_CSCMR1_USDHC##id##_SEL,			\
		.bm = BM_CSCMR1_USDHC##id##_SEL,			\
		.parents = {						\
			&pll2_pfd_400m,					\
			&pll2_pfd_352m,					\
			NULL						\
		},							\
	}

DEF_USDHC_MUX(1);
DEF_USDHC_MUX(2);
DEF_USDHC_MUX(3);
DEF_USDHC_MUX(4);

static struct multiplexer emi_mux = {
	.clk = &emi_clk,
	.reg = CSCMR1,
	.bp = BP_CSCMR1_EMI_SEL,
	.bm = BM_CSCMR1_EMI_SEL,
	.parents = {
		&axi_clk,
		&pll3_usb_otg,
		&pll2_pfd_400m,
		&pll2_pfd_352m,
		NULL
	},
};

static struct multiplexer emi_slow_mux = {
	.clk = &emi_slow_clk,
	.reg = CSCMR1,
	.bp = BP_CSCMR1_EMI_SLOW_SEL,
	.bm = BM_CSCMR1_EMI_SLOW_SEL,
	.parents = {
		&axi_clk,
		&pll3_usb_otg,
		&pll2_pfd_400m,
		&pll2_pfd_352m,
		NULL
	},
};

static struct multiplexer esai_mux = {
	.clk = &esai_clk,
	.reg = CSCMR2,
	.bp = BP_CSCMR2_ESAI_SEL,
	.bm = BM_CSCMR2_ESAI_SEL,
	.parents = {
		&pll4_audio,
		&pll3_pfd_508m,
		&pll3_pfd_454m,
		&pll3_usb_otg,
		NULL
	},
};

#define DEF_LDB_DI_MUX(id)						\
	static struct multiplexer ldb_di##id##_mux = {			\
		.clk = &ldb_di##id##_clk,				\
		.reg = CS2CDR,						\
		.bp = BP_CS2CDR_LDB_DI##id##_SEL,			\
		.bm = BM_CS2CDR_LDB_DI##id##_SEL,			\
		.parents = {						\
			&pll5_video,					\
			&pll2_pfd_352m,					\
			&pll2_pfd_400m,					\
			&pll3_pfd_540m,					\
			&pll3_usb_otg,					\
			NULL						\
		},							\
	}

DEF_LDB_DI_MUX(0);
DEF_LDB_DI_MUX(1);

static struct multiplexer enfc_mux = {
	.clk = &enfc_clk,
	.reg = CS2CDR,
	.bp = BP_CS2CDR_ENFC_SEL,
	.bm = BM_CS2CDR_ENFC_SEL,
	.parents = {
		&pll2_pfd_352m,
		&pll2_bus,
		&pll3_usb_otg,
		&pll2_pfd_400m,
		NULL
	},
};

static struct multiplexer spdif_mux = {
	.clk = &spdif_clk,
	.reg = CDCDR,
	.bp = BP_CDCDR_SPDIF_SEL,
	.bm = BM_CDCDR_SPDIF_SEL,
	.parents = {
		&pll4_audio,
		&pll3_pfd_508m,
		&pll3_pfd_454m,
		&pll3_usb_otg,
		NULL
	},
};

static struct multiplexer asrc_serial_mux = {
	.clk = &asrc_serial_clk,
	.reg = CDCDR,
	.bp = BP_CDCDR_ASRC_SERIAL_SEL,
	.bm = BM_CDCDR_ASRC_SERIAL_SEL,
	.parents = {
		&pll4_audio,
		&pll3_pfd_508m,
		&pll3_pfd_454m,
		&pll3_usb_otg,
		NULL
	},
};

static struct multiplexer hsi_tx_mux = {
	.clk = &hsi_tx_clk,
	.reg = CDCDR,
	.bp = BP_CDCDR_HSI_TX_SEL,
	.bm = BM_CDCDR_HSI_TX_SEL,
	.parents = {
		&pll3_120m,
		&pll2_pfd_400m,
		NULL
	},
};

#define DEF_IPU_DI_PRE_MUX(r, i, d)					\
	static struct multiplexer ipu##i##_di##d##_pre_mux = {		\
		.clk = &ipu##i##_di##d##_pre_clk,			\
		.reg = r,						\
		.bp = BP_##r##_IPU##i##_DI##d##_PRE_SEL,		\
		.bm = BM_##r##_IPU##i##_DI##d##_PRE_SEL,		\
		.parents = {						\
			&mmdc_ch0_axi_clk,				\
			&pll3_usb_otg,					\
			&pll5_video,					\
			&pll2_pfd_352m,					\
			&pll2_pfd_400m,					\
			&pll3_pfd_540m,					\
			NULL						\
		},							\
	}

DEF_IPU_DI_PRE_MUX(CHSCCDR, 1, 0);
DEF_IPU_DI_PRE_MUX(CHSCCDR, 1, 1);
DEF_IPU_DI_PRE_MUX(CSCDR2, 2, 0);
DEF_IPU_DI_PRE_MUX(CSCDR2, 2, 1);

#define DEF_IPU_DI_MUX(r, i, d)						\
	static struct multiplexer ipu##i##_di##d##_mux = {		\
		.clk = &ipu##i##_di##d##_clk,				\
		.reg = r,						\
		.bp = BP_##r##_IPU##i##_DI##d##_SEL,			\
		.bm = BM_##r##_IPU##i##_DI##d##_SEL,			\
		.parents = {						\
			&ipu##i##_di##d##_pre_clk,			\
			&dummy_clk,					\
			&dummy_clk,					\
			&ldb_di0_clk,					\
			&ldb_di1_clk,					\
			NULL						\
		},							\
	}

DEF_IPU_DI_MUX(CHSCCDR, 1, 0);
DEF_IPU_DI_MUX(CHSCCDR, 1, 1);
DEF_IPU_DI_MUX(CSCDR2, 2, 0);
DEF_IPU_DI_MUX(CSCDR2, 2, 1);

#define DEF_IPU_MUX(id)							\
	static struct multiplexer ipu##id##_mux = {			\
		.clk = &ipu##id##_clk,					\
		.reg = CSCDR3,						\
		.bp = BP_CSCDR3_IPU##id##_HSP_SEL,			\
		.bm = BM_CSCDR3_IPU##id##_HSP_SEL,			\
		.parents = {						\
			&mmdc_ch0_axi_clk,				\
			&pll2_pfd_400m,					\
			&pll3_120m,					\
			&pll3_pfd_540m,					\
			NULL						\
		},							\
	}

DEF_IPU_MUX(1);
DEF_IPU_MUX(2);

static struct multiplexer *multiplexers[] = {
	&axi_mux,
	&periph_mux,
	&periph_pre_mux,
	&periph_clk2_mux,
	&periph2_mux,
	&periph2_pre_mux,
	&periph2_clk2_mux,
	&gpu2d_axi_mux,
	&gpu3d_axi_mux,
	&gpu3d_core_mux,
	&gpu3d_shader_mux,
	&pcie_axi_mux,
	&vdo_axi_mux,
	&vpu_axi_mux,
	&gpu2d_core_mux,
	&ssi1_mux,
	&ssi2_mux,
	&ssi3_mux,
	&usdhc1_mux,
	&usdhc2_mux,
	&usdhc3_mux,
	&usdhc4_mux,
	&emi_mux,
	&emi_slow_mux,
	&esai_mux,
	&ldb_di0_mux,
	&ldb_di1_mux,
	&enfc_mux,
	&spdif_mux,
	&asrc_serial_mux,
	&hsi_tx_mux,
	&ipu1_di0_pre_mux,
	&ipu1_di0_mux,
	&ipu1_di1_pre_mux,
	&ipu1_di1_mux,
	&ipu2_di0_pre_mux,
	&ipu2_di0_mux,
	&ipu2_di1_pre_mux,
	&ipu2_di1_mux,
	&ipu1_mux,
	&ipu2_mux,
};

static int _clk_set_parent(struct clk *clk, struct clk *parent)
{
	struct multiplexer *m;
	int i, num;
	u32 val;

	num = ARRAY_SIZE(multiplexers);
	for (i = 0; i < num; i++)
		if (multiplexers[i]->clk == clk) {
			m = multiplexers[i];
			break;
		}
	if (i == num)
		return -EINVAL;

	i = 0;
	while (m->parents[i]) {
		if (parent == m->parents[i])
			break;
		i++;
	}
	if (!m->parents[i])
		return -EINVAL;

	val = readl_relaxed(m->reg);
	val &= ~m->bm;
	val |= i << m->bp;
	writel_relaxed(val, m->reg);

	if (clk == &periph_clk)
		return clk_busy_wait(clk);

	return 0;
}

#define DEF_NG_CLK(name, p)				\
	static struct clk name = {			\
		.get_rate	= _clk_get_rate,	\
		.set_rate	= _clk_set_rate,	\
		.round_rate	= _clk_round_rate,	\
		.set_parent	= _clk_set_parent,	\
		.parent		= p,			\
	}

DEF_NG_CLK(periph_clk2_clk,	&osc_clk);
DEF_NG_CLK(periph_pre_clk,	&pll2_bus);
DEF_NG_CLK(periph_clk,		&periph_pre_clk);
DEF_NG_CLK(periph2_clk2_clk,	&osc_clk);
DEF_NG_CLK(periph2_pre_clk,	&pll2_bus);
DEF_NG_CLK(periph2_clk,		&periph2_pre_clk);
DEF_NG_CLK(axi_clk,		&periph_clk);
DEF_NG_CLK(emi_clk,		&axi_clk);
DEF_NG_CLK(arm_clk,		&pll1_sw_clk);
DEF_NG_CLK(ahb_clk,		&periph_clk);
DEF_NG_CLK(ipg_clk,		&ahb_clk);
DEF_NG_CLK(ipg_perclk,		&ipg_clk);
DEF_NG_CLK(ipu1_di0_pre_clk,	&pll3_pfd_540m);
DEF_NG_CLK(ipu1_di1_pre_clk,	&pll3_pfd_540m);
DEF_NG_CLK(ipu2_di0_pre_clk,	&pll3_pfd_540m);
DEF_NG_CLK(ipu2_di1_pre_clk,	&pll3_pfd_540m);
DEF_NG_CLK(asrc_serial_clk,	&pll3_usb_otg);

#define DEF_CLK(name, er, es, p, s)			\
	static struct clk name = {			\
		.enable_reg	= er,			\
		.enable_shift	= es,			\
		.enable		= _clk_enable,		\
		.disable	= _clk_disable,		\
		.get_rate	= _clk_get_rate,	\
		.set_rate	= _clk_set_rate,	\
		.round_rate	= _clk_round_rate,	\
		.set_parent	= _clk_set_parent,	\
		.parent		= p,			\
		.secondary	= s,			\
	}

DEF_CLK(aips_tz1_clk,	  CCGR0, CG0,  &ahb_clk,	  NULL);
DEF_CLK(aips_tz2_clk,	  CCGR0, CG1,  &ahb_clk,	  NULL);
DEF_CLK(apbh_dma_clk,	  CCGR0, CG2,  &ahb_clk,	  NULL);
DEF_CLK(asrc_clk,	  CCGR0, CG3,  &pll4_audio,	  NULL);
DEF_CLK(can1_serial_clk,  CCGR0, CG8,  &pll3_usb_otg,	  NULL);
DEF_CLK(can1_clk,	  CCGR0, CG7,  &pll3_usb_otg,	  &can1_serial_clk);
DEF_CLK(can2_serial_clk,  CCGR0, CG10, &pll3_usb_otg,	  NULL);
DEF_CLK(can2_clk,	  CCGR0, CG9,  &pll3_usb_otg,	  &can2_serial_clk);
DEF_CLK(ecspi1_clk,	  CCGR1, CG0,  &pll3_60m,	  NULL);
DEF_CLK(ecspi2_clk,	  CCGR1, CG1,  &pll3_60m,	  NULL);
DEF_CLK(ecspi3_clk,	  CCGR1, CG2,  &pll3_60m,	  NULL);
DEF_CLK(ecspi4_clk,	  CCGR1, CG3,  &pll3_60m,	  NULL);
DEF_CLK(ecspi5_clk,	  CCGR1, CG4,  &pll3_60m,	  NULL);
DEF_CLK(enet_clk,	  CCGR1, CG5,  &ipg_clk,	  NULL);
DEF_CLK(esai_clk,	  CCGR1, CG8,  &pll3_usb_otg,	  NULL);
DEF_CLK(gpt_serial_clk,	  CCGR1, CG11, &ipg_perclk,	  NULL);
DEF_CLK(gpt_clk,	  CCGR1, CG10, &ipg_perclk,	  &gpt_serial_clk);
DEF_CLK(gpu2d_core_clk,	  CCGR1, CG12, &pll2_pfd_352m,	  &gpu2d_axi_clk);
DEF_CLK(gpu3d_core_clk,	  CCGR1, CG13, &pll2_pfd_594m,	  &gpu3d_axi_clk);
DEF_CLK(gpu3d_shader_clk, CCGR1, CG13, &pll3_pfd_720m,	  &gpu3d_axi_clk);
DEF_CLK(hdmi_iahb_clk,	  CCGR2, CG0,  &ahb_clk,	  NULL);
DEF_CLK(hdmi_isfr_clk,	  CCGR2, CG2,  &pll3_pfd_540m,	  &hdmi_iahb_clk);
DEF_CLK(i2c1_clk,	  CCGR2, CG3,  &ipg_perclk,	  NULL);
DEF_CLK(i2c2_clk,	  CCGR2, CG4,  &ipg_perclk,	  NULL);
DEF_CLK(i2c3_clk,	  CCGR2, CG5,  &ipg_perclk,	  NULL);
DEF_CLK(iim_clk,	  CCGR2, CG6,  &ipg_clk,	  NULL);
DEF_CLK(enfc_clk,	  CCGR2, CG7,  &pll2_pfd_352m,	  NULL);
DEF_CLK(ipu1_clk,	  CCGR3, CG0,  &mmdc_ch0_axi_clk, NULL);
DEF_CLK(ipu1_di0_clk,	  CCGR3, CG1,  &ipu1_di0_pre_clk, NULL);
DEF_CLK(ipu1_di1_clk,	  CCGR3, CG2,  &ipu1_di1_pre_clk, NULL);
DEF_CLK(ipu2_clk,	  CCGR3, CG3,  &mmdc_ch0_axi_clk, NULL);
DEF_CLK(ipu2_di0_clk,	  CCGR3, CG4,  &ipu2_di0_pre_clk, NULL);
DEF_CLK(ipu2_di1_clk,	  CCGR3, CG5,  &ipu2_di1_pre_clk, NULL);
DEF_CLK(ldb_di0_clk,	  CCGR3, CG6,  &pll3_pfd_540m,	  NULL);
DEF_CLK(ldb_di1_clk,	  CCGR3, CG7,  &pll3_pfd_540m,	  NULL);
DEF_CLK(hsi_tx_clk,	  CCGR3, CG8,  &pll2_pfd_400m,	  NULL);
DEF_CLK(mlb_clk,	  CCGR3, CG9,  &pll6_mlb,	  NULL);
DEF_CLK(mmdc_ch0_ipg_clk, CCGR3, CG12, &ipg_clk,	  NULL);
DEF_CLK(mmdc_ch0_axi_clk, CCGR3, CG10, &periph_clk,	  &mmdc_ch0_ipg_clk);
DEF_CLK(mmdc_ch1_ipg_clk, CCGR3, CG13, &ipg_clk,	  NULL);
DEF_CLK(mmdc_ch1_axi_clk, CCGR3, CG11, &periph2_clk,	  &mmdc_ch1_ipg_clk);
DEF_CLK(openvg_axi_clk,   CCGR3, CG13, &axi_clk,	  NULL);
DEF_CLK(pwm1_clk,	  CCGR4, CG8,  &ipg_perclk,	  NULL);
DEF_CLK(pwm2_clk,	  CCGR4, CG9,  &ipg_perclk,	  NULL);
DEF_CLK(pwm3_clk,	  CCGR4, CG10, &ipg_perclk,	  NULL);
DEF_CLK(pwm4_clk,	  CCGR4, CG11, &ipg_perclk,	  NULL);
DEF_CLK(gpmi_bch_apb_clk, CCGR4, CG12, &usdhc3_clk,	  NULL);
DEF_CLK(gpmi_bch_clk,	  CCGR4, CG13, &usdhc4_clk,	  &gpmi_bch_apb_clk);
DEF_CLK(gpmi_apb_clk,	  CCGR4, CG15, &usdhc3_clk,	  &gpmi_bch_clk);
DEF_CLK(gpmi_io_clk,	  CCGR4, CG14, &enfc_clk,	  &gpmi_apb_clk);
DEF_CLK(sdma_clk,	  CCGR5, CG3,  &ahb_clk,	  NULL);
DEF_CLK(spba_clk,	  CCGR5, CG6,  &ipg_clk,	  NULL);
DEF_CLK(spdif_clk,	  CCGR5, CG7,  &pll3_usb_otg,	  &spba_clk);
DEF_CLK(ssi1_clk,	  CCGR5, CG9,  &pll3_pfd_508m,	  NULL);
DEF_CLK(ssi2_clk,	  CCGR5, CG10, &pll3_pfd_508m,	  NULL);
DEF_CLK(ssi3_clk,	  CCGR5, CG11, &pll3_pfd_508m,	  NULL);
DEF_CLK(uart_serial_clk,  CCGR5, CG13, &pll3_usb_otg,	  NULL);
DEF_CLK(uart_clk,	  CCGR5, CG12, &pll3_80m,	  &uart_serial_clk);
DEF_CLK(usboh3_clk,	  CCGR6, CG0,  &ipg_clk,	  NULL);
DEF_CLK(usdhc1_clk,	  CCGR6, CG1,  &pll2_pfd_400m,	  NULL);
DEF_CLK(usdhc2_clk,	  CCGR6, CG2,  &pll2_pfd_400m,	  NULL);
DEF_CLK(usdhc3_clk,	  CCGR6, CG3,  &pll2_pfd_400m,	  NULL);
DEF_CLK(usdhc4_clk,	  CCGR6, CG4,  &pll2_pfd_400m,	  NULL);
DEF_CLK(emi_slow_clk,	  CCGR6, CG5,  &axi_clk,	  NULL);
DEF_CLK(vdo_axi_clk,	  CCGR6, CG6,  &axi_clk,	  NULL);
DEF_CLK(vpu_clk,	  CCGR6, CG7,  &axi_clk,	  NULL);

static int pcie_clk_enable(struct clk *clk)
{
	u32 val;

	val = readl_relaxed(PLL8_ENET);
	val |= BM_PLL_ENET_EN_PCIE;
	writel_relaxed(val, PLL8_ENET);

	return _clk_enable(clk);
}

static void pcie_clk_disable(struct clk *clk)
{
	u32 val;

	_clk_disable(clk);

	val = readl_relaxed(PLL8_ENET);
	val &= BM_PLL_ENET_EN_PCIE;
	writel_relaxed(val, PLL8_ENET);
}

static struct clk pcie_clk = {
	.enable_reg = CCGR4,
	.enable_shift = CG0,
	.enable = pcie_clk_enable,
	.disable = pcie_clk_disable,
	.set_parent = _clk_set_parent,
	.parent = &axi_clk,
	.secondary = &pll8_enet,
};

static int sata_clk_enable(struct clk *clk)
{
	u32 val;

	val = readl_relaxed(PLL8_ENET);
	val |= BM_PLL_ENET_EN_SATA;
	writel_relaxed(val, PLL8_ENET);

	return _clk_enable(clk);
}

static void sata_clk_disable(struct clk *clk)
{
	u32 val;

	_clk_disable(clk);

	val = readl_relaxed(PLL8_ENET);
	val &= BM_PLL_ENET_EN_SATA;
	writel_relaxed(val, PLL8_ENET);
}

static struct clk sata_clk = {
	.enable_reg = CCGR5,
	.enable_shift = CG2,
	.enable = sata_clk_enable,
	.disable = sata_clk_disable,
	.parent = &ipg_clk,
	.secondary = &pll8_enet,
};

#define _REGISTER_CLOCK(d, n, c) \
	{ \
		.dev_id = d, \
		.con_id = n, \
		.clk = &c, \
	}

static struct clk_lookup lookups[] = {
	_REGISTER_CLOCK("2020000.uart", NULL, uart_clk),
	_REGISTER_CLOCK("21e8000.uart", NULL, uart_clk),
	_REGISTER_CLOCK("21ec000.uart", NULL, uart_clk),
	_REGISTER_CLOCK("21f0000.uart", NULL, uart_clk),
	_REGISTER_CLOCK("21f4000.uart", NULL, uart_clk),
	_REGISTER_CLOCK("2188000.enet", NULL, enet_clk),
	_REGISTER_CLOCK("2190000.usdhc", NULL, usdhc1_clk),
	_REGISTER_CLOCK("2194000.usdhc", NULL, usdhc2_clk),
	_REGISTER_CLOCK("2198000.usdhc", NULL, usdhc3_clk),
	_REGISTER_CLOCK("219c000.usdhc", NULL, usdhc4_clk),
	_REGISTER_CLOCK("21a0000.i2c", NULL, i2c1_clk),
	_REGISTER_CLOCK("21a4000.i2c", NULL, i2c2_clk),
	_REGISTER_CLOCK("21a8000.i2c", NULL, i2c3_clk),
	_REGISTER_CLOCK("2008000.ecspi", NULL, ecspi1_clk),
	_REGISTER_CLOCK("200c000.ecspi", NULL, ecspi2_clk),
	_REGISTER_CLOCK("2010000.ecspi", NULL, ecspi3_clk),
	_REGISTER_CLOCK("2014000.ecspi", NULL, ecspi4_clk),
	_REGISTER_CLOCK("2018000.ecspi", NULL, ecspi5_clk),
	_REGISTER_CLOCK("20ec000.sdma", NULL, sdma_clk),
	_REGISTER_CLOCK("20bc000.wdog", NULL, dummy_clk),
	_REGISTER_CLOCK("20c0000.wdog", NULL, dummy_clk),
	_REGISTER_CLOCK(NULL, "ckih", ckih_clk),
	_REGISTER_CLOCK(NULL, "ckil_clk", ckil_clk),
	_REGISTER_CLOCK(NULL, "aips_tz1_clk", aips_tz1_clk),
	_REGISTER_CLOCK(NULL, "aips_tz2_clk", aips_tz2_clk),
	_REGISTER_CLOCK(NULL, "asrc_clk", asrc_clk),
	_REGISTER_CLOCK(NULL, "can2_clk", can2_clk),
	_REGISTER_CLOCK(NULL, "hdmi_isfr_clk", hdmi_isfr_clk),
	_REGISTER_CLOCK(NULL, "iim_clk", iim_clk),
	_REGISTER_CLOCK(NULL, "mlb_clk", mlb_clk),
	_REGISTER_CLOCK(NULL, "openvg_axi_clk", openvg_axi_clk),
	_REGISTER_CLOCK(NULL, "pwm1_clk", pwm1_clk),
	_REGISTER_CLOCK(NULL, "pwm2_clk", pwm2_clk),
	_REGISTER_CLOCK(NULL, "pwm3_clk", pwm3_clk),
	_REGISTER_CLOCK(NULL, "pwm4_clk", pwm4_clk),
	_REGISTER_CLOCK(NULL, "gpmi_io_clk", gpmi_io_clk),
	_REGISTER_CLOCK(NULL, "usboh3_clk", usboh3_clk),
	_REGISTER_CLOCK(NULL, "sata_clk", sata_clk),
};

int imx6q_set_lpm(enum mxc_cpu_pwr_mode mode)
{
	u32 val = readl_relaxed(CLPCR);

	val &= ~BM_CLPCR_LPM;
	switch (mode) {
	case WAIT_CLOCKED:
		break;
	case WAIT_UNCLOCKED:
		val |= 0x1 << BP_CLPCR_LPM;
		break;
	case STOP_POWER_ON:
		val |= 0x2 << BP_CLPCR_LPM;
		break;
	case WAIT_UNCLOCKED_POWER_OFF:
		val |= 0x1 << BP_CLPCR_LPM;
		val &= ~BM_CLPCR_VSTBY;
		val &= ~BM_CLPCR_SBYOS;
		val |= BM_CLPCR_BYP_MMDC_CH1_LPM_HS;
		break;
	case STOP_POWER_OFF:
		val |= 0x2 << BP_CLPCR_LPM;
		val |= 0x3 << BP_CLPCR_STBY_COUNT;
		val |= BM_CLPCR_VSTBY;
		val |= BM_CLPCR_SBYOS;
		val |= BM_CLPCR_BYP_MMDC_CH1_LPM_HS;
		break;
	default:
		return -EINVAL;
	}
	writel_relaxed(val, CLPCR);

	return 0;
}

static struct map_desc imx6q_clock_desc[] = {
	imx_map_entry(MX6Q, CCM, MT_DEVICE),
	imx_map_entry(MX6Q, ANATOP, MT_DEVICE),
};

int __init mx6q_clocks_init(void)
{
	struct device_node *np;
	void __iomem *base;
	int i, irq;

	iotable_init(imx6q_clock_desc, ARRAY_SIZE(imx6q_clock_desc));

	/* retrieve the freqency of fixed clocks from device tree */
	for_each_compatible_node(np, NULL, "fixed-clock") {
		u32 rate;
		if (of_property_read_u32(np, "clock-frequency", &rate))
			continue;

		if (of_device_is_compatible(np, "fsl,imx-ckil"))
			external_low_reference = rate;
		else if (of_device_is_compatible(np, "fsl,imx-ckih1"))
			external_high_reference = rate;
		else if (of_device_is_compatible(np, "fsl,imx-osc"))
			oscillator_reference = rate;
	}

	for (i = 0; i < ARRAY_SIZE(lookups); i++)
		clkdev_add(&lookups[i]);

	/* only keep necessary clocks on */
	writel_relaxed(0x3 << CG0  | 0x3 << CG1  | 0x3 << CG2,	CCGR0);
	writel_relaxed(0x3 << CG8  | 0x3 << CG9  | 0x3 << CG10,	CCGR2);
	writel_relaxed(0x3 << CG10 | 0x3 << CG12,		CCGR3);
	writel_relaxed(0x3 << CG4  | 0x3 << CG6  | 0x3 << CG7,	CCGR4);
	writel_relaxed(0x3 << CG0,				CCGR5);
	writel_relaxed(0,					CCGR6);
	writel_relaxed(0,					CCGR7);

	clk_enable(&uart_clk);
	clk_enable(&mmdc_ch0_axi_clk);

	clk_set_rate(&pll4_audio, FREQ_650M);
	clk_set_rate(&pll5_video, FREQ_650M);
	clk_set_parent(&ipu1_di0_clk, &ipu1_di0_pre_clk);
	clk_set_parent(&ipu1_di0_pre_clk, &pll5_video);
	clk_set_parent(&gpu3d_shader_clk, &pll2_pfd_594m);
	clk_set_rate(&gpu3d_shader_clk, FREQ_594M);
	clk_set_parent(&gpu3d_core_clk, &mmdc_ch0_axi_clk);
	clk_set_rate(&gpu3d_core_clk, FREQ_528M);
	clk_set_parent(&asrc_serial_clk, &pll3_usb_otg);
	clk_set_rate(&asrc_serial_clk, 1500000);
	clk_set_rate(&enfc_clk, 11000000);

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-gpt");
	base = of_iomap(np, 0);
	WARN_ON(!base);
	irq = irq_of_parse_and_map(np, 0);
	mxc_timer_init(&gpt_clk, base, irq);

	return 0;
}
