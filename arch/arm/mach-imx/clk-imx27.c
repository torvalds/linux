#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <dt-bindings/clock/imx27-clock.h>

#include "clk.h"
#include "common.h"
#include "hardware.h"

static void __iomem *ccm __initdata;

/* Register offsets */
#define CCM_CSCR		(ccm + 0x00)
#define CCM_MPCTL0		(ccm + 0x04)
#define CCM_MPCTL1		(ccm + 0x08)
#define CCM_SPCTL0		(ccm + 0x0c)
#define CCM_SPCTL1		(ccm + 0x10)
#define CCM_PCDR0		(ccm + 0x18)
#define CCM_PCDR1		(ccm + 0x1c)
#define CCM_PCCR0		(ccm + 0x20)
#define CCM_PCCR1		(ccm + 0x24)
#define CCM_CCSR		(ccm + 0x28)

static const char *vpu_sel_clks[] = { "spll", "mpll_main2", };
static const char *cpu_sel_clks[] = { "mpll_main2", "mpll", };
static const char *mpll_sel_clks[] = { "fpm", "mpll_osc_sel", };
static const char *mpll_osc_sel_clks[] = { "ckih", "ckih_div1p5", };
static const char *clko_sel_clks[] = {
	"ckil", "fpm", "ckih", "ckih",
	"ckih", "mpll", "spll", "cpu_div",
	"ahb", "ipg", "per1_div", "per2_div",
	"per3_div", "per4_div", "ssi1_div", "ssi2_div",
	"nfc_div", "mshc_div", "vpu_div", "60m",
	"32k", "usb_div", "dptc",
};

static const char *ssi_sel_clks[] = { "spll_gate", "mpll", };

static struct clk *clk[IMX27_CLK_MAX];
static struct clk_onecell_data clk_data;

static void __init _mx27_clocks_init(unsigned long fref)
{
	BUG_ON(!ccm);

	clk[IMX27_CLK_DUMMY] = imx_clk_fixed("dummy", 0);
	clk[IMX27_CLK_CKIH] = imx_clk_fixed("ckih", fref);
	clk[IMX27_CLK_CKIL] = imx_clk_fixed("ckil", 32768);
	clk[IMX27_CLK_FPM] = imx_clk_fixed_factor("fpm", "ckil", 1024, 1);
	clk[IMX27_CLK_CKIH_DIV1P5] = imx_clk_fixed_factor("ckih_div1p5", "ckih", 2, 3);
	clk[IMX27_CLK_MPLL_OSC_SEL] = imx_clk_mux("mpll_osc_sel", CCM_CSCR, 4, 1, mpll_osc_sel_clks, ARRAY_SIZE(mpll_osc_sel_clks));
	clk[IMX27_CLK_MPLL_SEL] = imx_clk_mux("mpll_sel", CCM_CSCR, 16, 1, mpll_sel_clks, ARRAY_SIZE(mpll_sel_clks));
	clk[IMX27_CLK_MPLL] = imx_clk_pllv1("mpll", "mpll_sel", CCM_MPCTL0);
	clk[IMX27_CLK_SPLL] = imx_clk_pllv1("spll", "ckih", CCM_SPCTL0);
	clk[IMX27_CLK_SPLL_GATE] = imx_clk_gate("spll_gate", "spll", CCM_CSCR, 1);
	clk[IMX27_CLK_MPLL_MAIN2] = imx_clk_fixed_factor("mpll_main2", "mpll", 2, 3);

	if (mx27_revision() >= IMX_CHIP_REVISION_2_0) {
		clk[IMX27_CLK_AHB] = imx_clk_divider("ahb", "mpll_main2", CCM_CSCR, 8, 2);
		clk[IMX27_CLK_IPG] = imx_clk_fixed_factor("ipg", "ahb", 1, 2);
	} else {
		clk[IMX27_CLK_AHB] = imx_clk_divider("ahb", "mpll_main2", CCM_CSCR, 9, 4);
		clk[IMX27_CLK_IPG] = imx_clk_divider("ipg", "ahb", CCM_CSCR, 8, 1);
	}

	clk[IMX27_CLK_MSHC_DIV] = imx_clk_divider("mshc_div", "ahb", CCM_PCDR0, 0, 6);
	clk[IMX27_CLK_NFC_DIV] = imx_clk_divider("nfc_div", "ahb", CCM_PCDR0, 6, 4);
	clk[IMX27_CLK_PER1_DIV] = imx_clk_divider("per1_div", "mpll_main2", CCM_PCDR1, 0, 6);
	clk[IMX27_CLK_PER2_DIV] = imx_clk_divider("per2_div", "mpll_main2", CCM_PCDR1, 8, 6);
	clk[IMX27_CLK_PER3_DIV] = imx_clk_divider("per3_div", "mpll_main2", CCM_PCDR1, 16, 6);
	clk[IMX27_CLK_PER4_DIV] = imx_clk_divider("per4_div", "mpll_main2", CCM_PCDR1, 24, 6);
	clk[IMX27_CLK_VPU_SEL] = imx_clk_mux("vpu_sel", CCM_CSCR, 21, 1, vpu_sel_clks, ARRAY_SIZE(vpu_sel_clks));
	clk[IMX27_CLK_VPU_DIV] = imx_clk_divider("vpu_div", "vpu_sel", CCM_PCDR0, 10, 6);
	clk[IMX27_CLK_USB_DIV] = imx_clk_divider("usb_div", "spll_gate", CCM_CSCR, 28, 3);
	clk[IMX27_CLK_CPU_SEL] = imx_clk_mux("cpu_sel", CCM_CSCR, 15, 1, cpu_sel_clks, ARRAY_SIZE(cpu_sel_clks));
	clk[IMX27_CLK_CLKO_SEL] = imx_clk_mux("clko_sel", CCM_CCSR, 0, 5, clko_sel_clks, ARRAY_SIZE(clko_sel_clks));

	if (mx27_revision() >= IMX_CHIP_REVISION_2_0)
		clk[IMX27_CLK_CPU_DIV] = imx_clk_divider("cpu_div", "cpu_sel", CCM_CSCR, 12, 2);
	else
		clk[IMX27_CLK_CPU_DIV] = imx_clk_divider("cpu_div", "cpu_sel", CCM_CSCR, 13, 3);

	clk[IMX27_CLK_CLKO_DIV] = imx_clk_divider("clko_div", "clko_sel", CCM_PCDR0, 22, 3);
	clk[IMX27_CLK_SSI1_SEL] = imx_clk_mux("ssi1_sel", CCM_CSCR, 22, 1, ssi_sel_clks, ARRAY_SIZE(ssi_sel_clks));
	clk[IMX27_CLK_SSI2_SEL] = imx_clk_mux("ssi2_sel", CCM_CSCR, 23, 1, ssi_sel_clks, ARRAY_SIZE(ssi_sel_clks));
	clk[IMX27_CLK_SSI1_DIV] = imx_clk_divider("ssi1_div", "ssi1_sel", CCM_PCDR0, 16, 6);
	clk[IMX27_CLK_SSI2_DIV] = imx_clk_divider("ssi2_div", "ssi2_sel", CCM_PCDR0, 26, 6);
	clk[IMX27_CLK_CLKO_EN] = imx_clk_gate("clko_en", "clko_div", CCM_PCCR0, 0);
	clk[IMX27_CLK_SSI2_IPG_GATE] = imx_clk_gate("ssi2_ipg_gate", "ipg", CCM_PCCR0, 0);
	clk[IMX27_CLK_SSI1_IPG_GATE] = imx_clk_gate("ssi1_ipg_gate", "ipg", CCM_PCCR0, 1);
	clk[IMX27_CLK_SLCDC_IPG_GATE] = imx_clk_gate("slcdc_ipg_gate", "ipg", CCM_PCCR0, 2);
	clk[IMX27_CLK_SDHC3_IPG_GATE] = imx_clk_gate("sdhc3_ipg_gate", "ipg", CCM_PCCR0, 3);
	clk[IMX27_CLK_SDHC2_IPG_GATE] = imx_clk_gate("sdhc2_ipg_gate", "ipg", CCM_PCCR0, 4);
	clk[IMX27_CLK_SDHC1_IPG_GATE] = imx_clk_gate("sdhc1_ipg_gate", "ipg", CCM_PCCR0, 5);
	clk[IMX27_CLK_SCC_IPG_GATE] = imx_clk_gate("scc_ipg_gate", "ipg", CCM_PCCR0, 6);
	clk[IMX27_CLK_SAHARA_IPG_GATE] = imx_clk_gate("sahara_ipg_gate", "ipg", CCM_PCCR0, 7);
	clk[IMX27_CLK_RTIC_IPG_GATE] = imx_clk_gate("rtic_ipg_gate", "ipg", CCM_PCCR0, 8);
	clk[IMX27_CLK_RTC_IPG_GATE] = imx_clk_gate("rtc_ipg_gate", "ipg", CCM_PCCR0, 9);
	clk[IMX27_CLK_PWM_IPG_GATE] = imx_clk_gate("pwm_ipg_gate", "ipg", CCM_PCCR0, 11);
	clk[IMX27_CLK_OWIRE_IPG_GATE] = imx_clk_gate("owire_ipg_gate", "ipg", CCM_PCCR0, 12);
	clk[IMX27_CLK_MSHC_IPG_GATE] = imx_clk_gate("mshc_ipg_gate", "ipg", CCM_PCCR0, 13);
	clk[IMX27_CLK_LCDC_IPG_GATE] = imx_clk_gate("lcdc_ipg_gate", "ipg", CCM_PCCR0, 14);
	clk[IMX27_CLK_KPP_IPG_GATE] = imx_clk_gate("kpp_ipg_gate", "ipg", CCM_PCCR0, 15);
	clk[IMX27_CLK_IIM_IPG_GATE] = imx_clk_gate("iim_ipg_gate", "ipg", CCM_PCCR0, 16);
	clk[IMX27_CLK_I2C2_IPG_GATE] = imx_clk_gate("i2c2_ipg_gate", "ipg", CCM_PCCR0, 17);
	clk[IMX27_CLK_I2C1_IPG_GATE] = imx_clk_gate("i2c1_ipg_gate", "ipg", CCM_PCCR0, 18);
	clk[IMX27_CLK_GPT6_IPG_GATE] = imx_clk_gate("gpt6_ipg_gate", "ipg", CCM_PCCR0, 19);
	clk[IMX27_CLK_GPT5_IPG_GATE] = imx_clk_gate("gpt5_ipg_gate", "ipg", CCM_PCCR0, 20);
	clk[IMX27_CLK_GPT4_IPG_GATE] = imx_clk_gate("gpt4_ipg_gate", "ipg", CCM_PCCR0, 21);
	clk[IMX27_CLK_GPT3_IPG_GATE] = imx_clk_gate("gpt3_ipg_gate", "ipg", CCM_PCCR0, 22);
	clk[IMX27_CLK_GPT2_IPG_GATE] = imx_clk_gate("gpt2_ipg_gate", "ipg", CCM_PCCR0, 23);
	clk[IMX27_CLK_GPT1_IPG_GATE] = imx_clk_gate("gpt1_ipg_gate", "ipg", CCM_PCCR0, 24);
	clk[IMX27_CLK_GPIO_IPG_GATE] = imx_clk_gate("gpio_ipg_gate", "ipg", CCM_PCCR0, 25);
	clk[IMX27_CLK_FEC_IPG_GATE] = imx_clk_gate("fec_ipg_gate", "ipg", CCM_PCCR0, 26);
	clk[IMX27_CLK_EMMA_IPG_GATE] = imx_clk_gate("emma_ipg_gate", "ipg", CCM_PCCR0, 27);
	clk[IMX27_CLK_DMA_IPG_GATE] = imx_clk_gate("dma_ipg_gate", "ipg", CCM_PCCR0, 28);
	clk[IMX27_CLK_CSPI3_IPG_GATE] = imx_clk_gate("cspi3_ipg_gate", "ipg", CCM_PCCR0, 29);
	clk[IMX27_CLK_CSPI2_IPG_GATE] = imx_clk_gate("cspi2_ipg_gate", "ipg", CCM_PCCR0, 30);
	clk[IMX27_CLK_CSPI1_IPG_GATE] = imx_clk_gate("cspi1_ipg_gate", "ipg", CCM_PCCR0, 31);
	clk[IMX27_CLK_MSHC_BAUD_GATE] = imx_clk_gate("mshc_baud_gate", "mshc_div", CCM_PCCR1, 2);
	clk[IMX27_CLK_NFC_BAUD_GATE] = imx_clk_gate("nfc_baud_gate", "nfc_div", CCM_PCCR1,  3);
	clk[IMX27_CLK_SSI2_BAUD_GATE] = imx_clk_gate("ssi2_baud_gate", "ssi2_div", CCM_PCCR1,  4);
	clk[IMX27_CLK_SSI1_BAUD_GATE] = imx_clk_gate("ssi1_baud_gate", "ssi1_div", CCM_PCCR1,  5);
	clk[IMX27_CLK_VPU_BAUD_GATE] = imx_clk_gate("vpu_baud_gate", "vpu_div", CCM_PCCR1,  6);
	clk[IMX27_CLK_PER4_GATE] = imx_clk_gate("per4_gate", "per4_div", CCM_PCCR1,  7);
	clk[IMX27_CLK_PER3_GATE] = imx_clk_gate("per3_gate", "per3_div", CCM_PCCR1,  8);
	clk[IMX27_CLK_PER2_GATE] = imx_clk_gate("per2_gate", "per2_div", CCM_PCCR1,  9);
	clk[IMX27_CLK_PER1_GATE] = imx_clk_gate("per1_gate", "per1_div", CCM_PCCR1, 10);
	clk[IMX27_CLK_USB_AHB_GATE] = imx_clk_gate("usb_ahb_gate", "ahb", CCM_PCCR1, 11);
	clk[IMX27_CLK_SLCDC_AHB_GATE] = imx_clk_gate("slcdc_ahb_gate", "ahb", CCM_PCCR1, 12);
	clk[IMX27_CLK_SAHARA_AHB_GATE] = imx_clk_gate("sahara_ahb_gate", "ahb", CCM_PCCR1, 13);
	clk[IMX27_CLK_RTIC_AHB_GATE] = imx_clk_gate("rtic_ahb_gate", "ahb", CCM_PCCR1, 14);
	clk[IMX27_CLK_LCDC_AHB_GATE] = imx_clk_gate("lcdc_ahb_gate", "ahb", CCM_PCCR1, 15);
	clk[IMX27_CLK_VPU_AHB_GATE] = imx_clk_gate("vpu_ahb_gate", "ahb", CCM_PCCR1, 16);
	clk[IMX27_CLK_FEC_AHB_GATE] = imx_clk_gate("fec_ahb_gate", "ahb", CCM_PCCR1, 17);
	clk[IMX27_CLK_EMMA_AHB_GATE] = imx_clk_gate("emma_ahb_gate", "ahb", CCM_PCCR1, 18);
	clk[IMX27_CLK_EMI_AHB_GATE] = imx_clk_gate("emi_ahb_gate", "ahb", CCM_PCCR1, 19);
	clk[IMX27_CLK_DMA_AHB_GATE] = imx_clk_gate("dma_ahb_gate", "ahb", CCM_PCCR1, 20);
	clk[IMX27_CLK_CSI_AHB_GATE] = imx_clk_gate("csi_ahb_gate", "ahb", CCM_PCCR1, 21);
	clk[IMX27_CLK_BROM_AHB_GATE] = imx_clk_gate("brom_ahb_gate", "ahb", CCM_PCCR1, 22);
	clk[IMX27_CLK_ATA_AHB_GATE] = imx_clk_gate("ata_ahb_gate", "ahb", CCM_PCCR1, 23);
	clk[IMX27_CLK_WDOG_IPG_GATE] = imx_clk_gate("wdog_ipg_gate", "ipg", CCM_PCCR1, 24);
	clk[IMX27_CLK_USB_IPG_GATE] = imx_clk_gate("usb_ipg_gate", "ipg", CCM_PCCR1, 25);
	clk[IMX27_CLK_UART6_IPG_GATE] = imx_clk_gate("uart6_ipg_gate", "ipg", CCM_PCCR1, 26);
	clk[IMX27_CLK_UART5_IPG_GATE] = imx_clk_gate("uart5_ipg_gate", "ipg", CCM_PCCR1, 27);
	clk[IMX27_CLK_UART4_IPG_GATE] = imx_clk_gate("uart4_ipg_gate", "ipg", CCM_PCCR1, 28);
	clk[IMX27_CLK_UART3_IPG_GATE] = imx_clk_gate("uart3_ipg_gate", "ipg", CCM_PCCR1, 29);
	clk[IMX27_CLK_UART2_IPG_GATE] = imx_clk_gate("uart2_ipg_gate", "ipg", CCM_PCCR1, 30);
	clk[IMX27_CLK_UART1_IPG_GATE] = imx_clk_gate("uart1_ipg_gate", "ipg", CCM_PCCR1, 31);

	imx_check_clocks(clk, ARRAY_SIZE(clk));

	clk_register_clkdev(clk[IMX27_CLK_CPU_DIV], NULL, "cpu0");

	clk_prepare_enable(clk[IMX27_CLK_EMI_AHB_GATE]);

	imx_print_silicon_rev("i.MX27", mx27_revision());
}

int __init mx27_clocks_init(unsigned long fref)
{
	ccm = ioremap(MX27_CCM_BASE_ADDR, SZ_4K);

	_mx27_clocks_init(fref);

	clk_register_clkdev(clk[IMX27_CLK_UART1_IPG_GATE], "ipg", "imx21-uart.0");
	clk_register_clkdev(clk[IMX27_CLK_PER1_GATE], "per", "imx21-uart.0");
	clk_register_clkdev(clk[IMX27_CLK_UART2_IPG_GATE], "ipg", "imx21-uart.1");
	clk_register_clkdev(clk[IMX27_CLK_PER1_GATE], "per", "imx21-uart.1");
	clk_register_clkdev(clk[IMX27_CLK_UART3_IPG_GATE], "ipg", "imx21-uart.2");
	clk_register_clkdev(clk[IMX27_CLK_PER1_GATE], "per", "imx21-uart.2");
	clk_register_clkdev(clk[IMX27_CLK_UART4_IPG_GATE], "ipg", "imx21-uart.3");
	clk_register_clkdev(clk[IMX27_CLK_PER1_GATE], "per", "imx21-uart.3");
	clk_register_clkdev(clk[IMX27_CLK_UART5_IPG_GATE], "ipg", "imx21-uart.4");
	clk_register_clkdev(clk[IMX27_CLK_PER1_GATE], "per", "imx21-uart.4");
	clk_register_clkdev(clk[IMX27_CLK_UART6_IPG_GATE], "ipg", "imx21-uart.5");
	clk_register_clkdev(clk[IMX27_CLK_PER1_GATE], "per", "imx21-uart.5");
	clk_register_clkdev(clk[IMX27_CLK_GPT1_IPG_GATE], "ipg", "imx-gpt.0");
	clk_register_clkdev(clk[IMX27_CLK_PER1_GATE], "per", "imx-gpt.0");
	clk_register_clkdev(clk[IMX27_CLK_PER2_GATE], "per", "imx21-mmc.0");
	clk_register_clkdev(clk[IMX27_CLK_SDHC1_IPG_GATE], "ipg", "imx21-mmc.0");
	clk_register_clkdev(clk[IMX27_CLK_PER2_GATE], "per", "imx21-mmc.1");
	clk_register_clkdev(clk[IMX27_CLK_SDHC2_IPG_GATE], "ipg", "imx21-mmc.1");
	clk_register_clkdev(clk[IMX27_CLK_PER2_GATE], "per", "imx21-mmc.2");
	clk_register_clkdev(clk[IMX27_CLK_SDHC2_IPG_GATE], "ipg", "imx21-mmc.2");
	clk_register_clkdev(clk[IMX27_CLK_PER2_GATE], "per", "imx27-cspi.0");
	clk_register_clkdev(clk[IMX27_CLK_CSPI1_IPG_GATE], "ipg", "imx27-cspi.0");
	clk_register_clkdev(clk[IMX27_CLK_PER2_GATE], "per", "imx27-cspi.1");
	clk_register_clkdev(clk[IMX27_CLK_CSPI2_IPG_GATE], "ipg", "imx27-cspi.1");
	clk_register_clkdev(clk[IMX27_CLK_PER2_GATE], "per", "imx27-cspi.2");
	clk_register_clkdev(clk[IMX27_CLK_CSPI3_IPG_GATE], "ipg", "imx27-cspi.2");
	clk_register_clkdev(clk[IMX27_CLK_PER3_GATE], "per", "imx21-fb.0");
	clk_register_clkdev(clk[IMX27_CLK_LCDC_IPG_GATE], "ipg", "imx21-fb.0");
	clk_register_clkdev(clk[IMX27_CLK_LCDC_AHB_GATE], "ahb", "imx21-fb.0");
	clk_register_clkdev(clk[IMX27_CLK_CSI_AHB_GATE], "ahb", "imx27-camera.0");
	clk_register_clkdev(clk[IMX27_CLK_PER4_GATE], "per", "imx27-camera.0");
	clk_register_clkdev(clk[IMX27_CLK_USB_DIV], "per", "imx-udc-mx27");
	clk_register_clkdev(clk[IMX27_CLK_USB_IPG_GATE], "ipg", "imx-udc-mx27");
	clk_register_clkdev(clk[IMX27_CLK_USB_AHB_GATE], "ahb", "imx-udc-mx27");
	clk_register_clkdev(clk[IMX27_CLK_USB_DIV], "per", "mxc-ehci.0");
	clk_register_clkdev(clk[IMX27_CLK_USB_IPG_GATE], "ipg", "mxc-ehci.0");
	clk_register_clkdev(clk[IMX27_CLK_USB_AHB_GATE], "ahb", "mxc-ehci.0");
	clk_register_clkdev(clk[IMX27_CLK_USB_DIV], "per", "mxc-ehci.1");
	clk_register_clkdev(clk[IMX27_CLK_USB_IPG_GATE], "ipg", "mxc-ehci.1");
	clk_register_clkdev(clk[IMX27_CLK_USB_AHB_GATE], "ahb", "mxc-ehci.1");
	clk_register_clkdev(clk[IMX27_CLK_USB_DIV], "per", "mxc-ehci.2");
	clk_register_clkdev(clk[IMX27_CLK_USB_IPG_GATE], "ipg", "mxc-ehci.2");
	clk_register_clkdev(clk[IMX27_CLK_USB_AHB_GATE], "ahb", "mxc-ehci.2");
	clk_register_clkdev(clk[IMX27_CLK_SSI1_IPG_GATE], NULL, "imx-ssi.0");
	clk_register_clkdev(clk[IMX27_CLK_SSI2_IPG_GATE], NULL, "imx-ssi.1");
	clk_register_clkdev(clk[IMX27_CLK_NFC_BAUD_GATE], NULL, "imx27-nand.0");
	clk_register_clkdev(clk[IMX27_CLK_VPU_BAUD_GATE], "per", "coda-imx27.0");
	clk_register_clkdev(clk[IMX27_CLK_VPU_AHB_GATE], "ahb", "coda-imx27.0");
	clk_register_clkdev(clk[IMX27_CLK_DMA_AHB_GATE], "ahb", "imx27-dma");
	clk_register_clkdev(clk[IMX27_CLK_DMA_IPG_GATE], "ipg", "imx27-dma");
	clk_register_clkdev(clk[IMX27_CLK_FEC_IPG_GATE], "ipg", "imx27-fec.0");
	clk_register_clkdev(clk[IMX27_CLK_FEC_AHB_GATE], "ahb", "imx27-fec.0");
	clk_register_clkdev(clk[IMX27_CLK_WDOG_IPG_GATE], NULL, "imx2-wdt.0");
	clk_register_clkdev(clk[IMX27_CLK_I2C1_IPG_GATE], NULL, "imx21-i2c.0");
	clk_register_clkdev(clk[IMX27_CLK_I2C2_IPG_GATE], NULL, "imx21-i2c.1");
	clk_register_clkdev(clk[IMX27_CLK_OWIRE_IPG_GATE], NULL, "mxc_w1.0");
	clk_register_clkdev(clk[IMX27_CLK_KPP_IPG_GATE], NULL, "imx-keypad");
	clk_register_clkdev(clk[IMX27_CLK_EMMA_AHB_GATE], "emma-ahb", "imx27-camera.0");
	clk_register_clkdev(clk[IMX27_CLK_EMMA_IPG_GATE], "emma-ipg", "imx27-camera.0");
	clk_register_clkdev(clk[IMX27_CLK_EMMA_AHB_GATE], "ahb", "m2m-emmaprp.0");
	clk_register_clkdev(clk[IMX27_CLK_EMMA_IPG_GATE], "ipg", "m2m-emmaprp.0");

	mxc_timer_init(MX27_IO_ADDRESS(MX27_GPT1_BASE_ADDR), MX27_INT_GPT1);

	return 0;
}

static void __init mx27_clocks_init_dt(struct device_node *np)
{
	struct device_node *refnp;
	u32 fref = 26000000; /* default */

	for_each_compatible_node(refnp, NULL, "fixed-clock") {
		if (!of_device_is_compatible(refnp, "fsl,imx-osc26m"))
			continue;

		if (!of_property_read_u32(refnp, "clock-frequency", &fref))
			break;
	}

	ccm = of_iomap(np, 0);

	_mx27_clocks_init(fref);

	clk_data.clks = clk;
	clk_data.clk_num = ARRAY_SIZE(clk);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	mxc_timer_init_dt(of_find_compatible_node(NULL, NULL, "fsl,imx1-gpt"));
}
CLK_OF_DECLARE(imx27_ccm, "fsl,imx27-ccm", mx27_clocks_init_dt);
