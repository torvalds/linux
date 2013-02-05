#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/of.h>

#include "clk.h"
#include "common.h"
#include "hardware.h"

#define IO_ADDR_CCM(off)	(MX27_IO_ADDRESS(MX27_CCM_BASE_ADDR + (off)))

/* Register offsets */
#define CCM_CSCR		IO_ADDR_CCM(0x0)
#define CCM_MPCTL0		IO_ADDR_CCM(0x4)
#define CCM_MPCTL1		IO_ADDR_CCM(0x8)
#define CCM_SPCTL0		IO_ADDR_CCM(0xc)
#define CCM_SPCTL1		IO_ADDR_CCM(0x10)
#define CCM_OSC26MCTL		IO_ADDR_CCM(0x14)
#define CCM_PCDR0		IO_ADDR_CCM(0x18)
#define CCM_PCDR1		IO_ADDR_CCM(0x1c)
#define CCM_PCCR0		IO_ADDR_CCM(0x20)
#define CCM_PCCR1		IO_ADDR_CCM(0x24)
#define CCM_CCSR		IO_ADDR_CCM(0x28)
#define CCM_PMCTL		IO_ADDR_CCM(0x2c)
#define CCM_PMCOUNT		IO_ADDR_CCM(0x30)
#define CCM_WKGDCTL		IO_ADDR_CCM(0x34)

#define CCM_CSCR_UPDATE_DIS	(1 << 31)
#define CCM_CSCR_SSI2		(1 << 23)
#define CCM_CSCR_SSI1		(1 << 22)
#define CCM_CSCR_VPU		(1 << 21)
#define CCM_CSCR_MSHC           (1 << 20)
#define CCM_CSCR_SPLLRES        (1 << 19)
#define CCM_CSCR_MPLLRES        (1 << 18)
#define CCM_CSCR_SP             (1 << 17)
#define CCM_CSCR_MCU            (1 << 16)
#define CCM_CSCR_OSC26MDIV      (1 << 4)
#define CCM_CSCR_OSC26M         (1 << 3)
#define CCM_CSCR_FPM            (1 << 2)
#define CCM_CSCR_SPEN           (1 << 1)
#define CCM_CSCR_MPEN           (1 << 0)

/* i.MX27 TO 2+ */
#define CCM_CSCR_ARM_SRC        (1 << 15)

#define CCM_SPCTL1_LF           (1 << 15)
#define CCM_SPCTL1_BRMO         (1 << 6)

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

enum mx27_clks {
	dummy, ckih, ckil, mpll, spll, mpll_main2, ahb, ipg, nfc_div, per1_div,
	per2_div, per3_div, per4_div, vpu_sel, vpu_div, usb_div, cpu_sel,
	clko_sel, cpu_div, clko_div, ssi1_sel, ssi2_sel, ssi1_div, ssi2_div,
	clko_en, ssi2_ipg_gate, ssi1_ipg_gate, slcdc_ipg_gate, sdhc3_ipg_gate,
	sdhc2_ipg_gate, sdhc1_ipg_gate, scc_ipg_gate, sahara_ipg_gate,
	rtc_ipg_gate, pwm_ipg_gate, owire_ipg_gate, lcdc_ipg_gate,
	kpp_ipg_gate, iim_ipg_gate, i2c2_ipg_gate, i2c1_ipg_gate,
	gpt6_ipg_gate, gpt5_ipg_gate, gpt4_ipg_gate, gpt3_ipg_gate,
	gpt2_ipg_gate, gpt1_ipg_gate, gpio_ipg_gate, fec_ipg_gate,
	emma_ipg_gate, dma_ipg_gate, cspi3_ipg_gate, cspi2_ipg_gate,
	cspi1_ipg_gate, nfc_baud_gate, ssi2_baud_gate, ssi1_baud_gate,
	vpu_baud_gate, per4_gate, per3_gate, per2_gate, per1_gate,
	usb_ahb_gate, slcdc_ahb_gate, sahara_ahb_gate, lcdc_ahb_gate,
	vpu_ahb_gate, fec_ahb_gate, emma_ahb_gate, emi_ahb_gate, dma_ahb_gate,
	csi_ahb_gate, brom_ahb_gate, ata_ahb_gate, wdog_ipg_gate, usb_ipg_gate,
	uart6_ipg_gate, uart5_ipg_gate, uart4_ipg_gate, uart3_ipg_gate,
	uart2_ipg_gate, uart1_ipg_gate, ckih_div1p5, fpm, mpll_osc_sel,
	mpll_sel, spll_gate, clk_max
};

static struct clk *clk[clk_max];

int __init mx27_clocks_init(unsigned long fref)
{
	int i;

	clk[dummy] = imx_clk_fixed("dummy", 0);
	clk[ckih] = imx_clk_fixed("ckih", fref);
	clk[ckil] = imx_clk_fixed("ckil", 32768);
	clk[fpm] = imx_clk_fixed_factor("fpm", "ckil", 1024, 1);
	clk[ckih_div1p5] = imx_clk_fixed_factor("ckih_div1p5", "ckih", 2, 3);

	clk[mpll_osc_sel] = imx_clk_mux("mpll_osc_sel", CCM_CSCR, 4, 1,
			mpll_osc_sel_clks,
			ARRAY_SIZE(mpll_osc_sel_clks));
	clk[mpll_sel] = imx_clk_mux("mpll_sel", CCM_CSCR, 16, 1, mpll_sel_clks,
			ARRAY_SIZE(mpll_sel_clks));
	clk[mpll] = imx_clk_pllv1("mpll", "mpll_sel", CCM_MPCTL0);
	clk[spll] = imx_clk_pllv1("spll", "ckih", CCM_SPCTL0);
	clk[spll_gate] = imx_clk_gate("spll_gate", "spll", CCM_CSCR, 1);
	clk[mpll_main2] = imx_clk_fixed_factor("mpll_main2", "mpll", 2, 3);

	if (mx27_revision() >= IMX_CHIP_REVISION_2_0) {
		clk[ahb] = imx_clk_divider("ahb", "mpll_main2", CCM_CSCR, 8, 2);
		clk[ipg] = imx_clk_fixed_factor("ipg", "ahb", 1, 2);
	} else {
		clk[ahb] = imx_clk_divider("ahb", "mpll_main2", CCM_CSCR, 9, 4);
		clk[ipg] = imx_clk_divider("ipg", "ahb", CCM_CSCR, 8, 1);
	}

	clk[nfc_div] = imx_clk_divider("nfc_div", "ahb", CCM_PCDR0, 6, 4);
	clk[per1_div] = imx_clk_divider("per1_div", "mpll_main2", CCM_PCDR1, 0, 6);
	clk[per2_div] = imx_clk_divider("per2_div", "mpll_main2", CCM_PCDR1, 8, 6);
	clk[per3_div] = imx_clk_divider("per3_div", "mpll_main2", CCM_PCDR1, 16, 6);
	clk[per4_div] = imx_clk_divider("per4_div", "mpll_main2", CCM_PCDR1, 24, 6);
	clk[vpu_sel] = imx_clk_mux("vpu_sel", CCM_CSCR, 21, 1, vpu_sel_clks, ARRAY_SIZE(vpu_sel_clks));
	clk[vpu_div] = imx_clk_divider("vpu_div", "vpu_sel", CCM_PCDR0, 10, 6);
	clk[usb_div] = imx_clk_divider("usb_div", "spll_gate", CCM_CSCR, 28, 3);
	clk[cpu_sel] = imx_clk_mux("cpu_sel", CCM_CSCR, 15, 1, cpu_sel_clks, ARRAY_SIZE(cpu_sel_clks));
	clk[clko_sel] = imx_clk_mux("clko_sel", CCM_CCSR, 0, 5, clko_sel_clks, ARRAY_SIZE(clko_sel_clks));
	if (mx27_revision() >= IMX_CHIP_REVISION_2_0)
		clk[cpu_div] = imx_clk_divider("cpu_div", "cpu_sel", CCM_CSCR, 12, 2);
	else
		clk[cpu_div] = imx_clk_divider("cpu_div", "cpu_sel", CCM_CSCR, 13, 3);
	clk[clko_div] = imx_clk_divider("clko_div", "clko_sel", CCM_PCDR0, 22, 3);
	clk[ssi1_sel] = imx_clk_mux("ssi1_sel", CCM_CSCR, 22, 1, ssi_sel_clks, ARRAY_SIZE(ssi_sel_clks));
	clk[ssi2_sel] = imx_clk_mux("ssi2_sel", CCM_CSCR, 23, 1, ssi_sel_clks, ARRAY_SIZE(ssi_sel_clks));
	clk[ssi1_div] = imx_clk_divider("ssi1_div", "ssi1_sel", CCM_PCDR0, 16, 6);
	clk[ssi2_div] = imx_clk_divider("ssi2_div", "ssi2_sel", CCM_PCDR0, 26, 6);
	clk[clko_en] = imx_clk_gate("clko_en", "clko_div", CCM_PCCR0, 0);
	clk[ssi2_ipg_gate] = imx_clk_gate("ssi2_ipg_gate", "ipg", CCM_PCCR0, 0);
	clk[ssi1_ipg_gate] = imx_clk_gate("ssi1_ipg_gate", "ipg", CCM_PCCR0, 1);
	clk[slcdc_ipg_gate] = imx_clk_gate("slcdc_ipg_gate", "ipg", CCM_PCCR0, 2);
	clk[sdhc3_ipg_gate] = imx_clk_gate("sdhc3_ipg_gate", "ipg", CCM_PCCR0, 3);
	clk[sdhc2_ipg_gate] = imx_clk_gate("sdhc2_ipg_gate", "ipg", CCM_PCCR0, 4);
	clk[sdhc1_ipg_gate] = imx_clk_gate("sdhc1_ipg_gate", "ipg", CCM_PCCR0, 5);
	clk[scc_ipg_gate] = imx_clk_gate("scc_ipg_gate", "ipg", CCM_PCCR0, 6);
	clk[sahara_ipg_gate] = imx_clk_gate("sahara_ipg_gate", "ipg", CCM_PCCR0, 7);
	clk[rtc_ipg_gate] = imx_clk_gate("rtc_ipg_gate", "ipg", CCM_PCCR0, 9);
	clk[pwm_ipg_gate] = imx_clk_gate("pwm_ipg_gate", "ipg", CCM_PCCR0, 11);
	clk[owire_ipg_gate] = imx_clk_gate("owire_ipg_gate", "ipg", CCM_PCCR0, 12);
	clk[lcdc_ipg_gate] = imx_clk_gate("lcdc_ipg_gate", "ipg", CCM_PCCR0, 14);
	clk[kpp_ipg_gate] = imx_clk_gate("kpp_ipg_gate", "ipg", CCM_PCCR0, 15);
	clk[iim_ipg_gate] = imx_clk_gate("iim_ipg_gate", "ipg", CCM_PCCR0, 16);
	clk[i2c2_ipg_gate] = imx_clk_gate("i2c2_ipg_gate", "ipg", CCM_PCCR0, 17);
	clk[i2c1_ipg_gate] = imx_clk_gate("i2c1_ipg_gate", "ipg", CCM_PCCR0, 18);
	clk[gpt6_ipg_gate] = imx_clk_gate("gpt6_ipg_gate", "ipg", CCM_PCCR0, 19);
	clk[gpt5_ipg_gate] = imx_clk_gate("gpt5_ipg_gate", "ipg", CCM_PCCR0, 20);
	clk[gpt4_ipg_gate] = imx_clk_gate("gpt4_ipg_gate", "ipg", CCM_PCCR0, 21);
	clk[gpt3_ipg_gate] = imx_clk_gate("gpt3_ipg_gate", "ipg", CCM_PCCR0, 22);
	clk[gpt2_ipg_gate] = imx_clk_gate("gpt2_ipg_gate", "ipg", CCM_PCCR0, 23);
	clk[gpt1_ipg_gate] = imx_clk_gate("gpt1_ipg_gate", "ipg", CCM_PCCR0, 24);
	clk[gpio_ipg_gate] = imx_clk_gate("gpio_ipg_gate", "ipg", CCM_PCCR0, 25);
	clk[fec_ipg_gate] = imx_clk_gate("fec_ipg_gate", "ipg", CCM_PCCR0, 26);
	clk[emma_ipg_gate] = imx_clk_gate("emma_ipg_gate", "ipg", CCM_PCCR0, 27);
	clk[dma_ipg_gate] = imx_clk_gate("dma_ipg_gate", "ipg", CCM_PCCR0, 28);
	clk[cspi3_ipg_gate] = imx_clk_gate("cspi3_ipg_gate", "ipg", CCM_PCCR0, 29);
	clk[cspi2_ipg_gate] = imx_clk_gate("cspi2_ipg_gate", "ipg", CCM_PCCR0, 30);
	clk[cspi1_ipg_gate] = imx_clk_gate("cspi1_ipg_gate", "ipg", CCM_PCCR0, 31);
	clk[nfc_baud_gate] = imx_clk_gate("nfc_baud_gate", "nfc_div", CCM_PCCR1,  3);
	clk[ssi2_baud_gate] = imx_clk_gate("ssi2_baud_gate", "ssi2_div", CCM_PCCR1,  4);
	clk[ssi1_baud_gate] = imx_clk_gate("ssi1_baud_gate", "ssi1_div", CCM_PCCR1,  5);
	clk[vpu_baud_gate] = imx_clk_gate("vpu_baud_gate", "vpu_div", CCM_PCCR1,  6);
	clk[per4_gate] = imx_clk_gate("per4_gate", "per4_div", CCM_PCCR1,  7);
	clk[per3_gate] = imx_clk_gate("per3_gate", "per3_div", CCM_PCCR1,  8);
	clk[per2_gate] = imx_clk_gate("per2_gate", "per2_div", CCM_PCCR1,  9);
	clk[per1_gate] = imx_clk_gate("per1_gate", "per1_div", CCM_PCCR1, 10);
	clk[usb_ahb_gate] = imx_clk_gate("usb_ahb_gate", "ahb", CCM_PCCR1, 11);
	clk[slcdc_ahb_gate] = imx_clk_gate("slcdc_ahb_gate", "ahb", CCM_PCCR1, 12);
	clk[sahara_ahb_gate] = imx_clk_gate("sahara_ahb_gate", "ahb", CCM_PCCR1, 13);
	clk[lcdc_ahb_gate] = imx_clk_gate("lcdc_ahb_gate", "ahb", CCM_PCCR1, 15);
	clk[vpu_ahb_gate] = imx_clk_gate("vpu_ahb_gate", "ahb", CCM_PCCR1, 16);
	clk[fec_ahb_gate] = imx_clk_gate("fec_ahb_gate", "ahb", CCM_PCCR1, 17);
	clk[emma_ahb_gate] = imx_clk_gate("emma_ahb_gate", "ahb", CCM_PCCR1, 18);
	clk[emi_ahb_gate] = imx_clk_gate("emi_ahb_gate", "ahb", CCM_PCCR1, 19);
	clk[dma_ahb_gate] = imx_clk_gate("dma_ahb_gate", "ahb", CCM_PCCR1, 20);
	clk[csi_ahb_gate] = imx_clk_gate("csi_ahb_gate", "ahb", CCM_PCCR1, 21);
	clk[brom_ahb_gate] = imx_clk_gate("brom_ahb_gate", "ahb", CCM_PCCR1, 22);
	clk[ata_ahb_gate] = imx_clk_gate("ata_ahb_gate", "ahb", CCM_PCCR1, 23);
	clk[wdog_ipg_gate] = imx_clk_gate("wdog_ipg_gate", "ipg", CCM_PCCR1, 24);
	clk[usb_ipg_gate] = imx_clk_gate("usb_ipg_gate", "ipg", CCM_PCCR1, 25);
	clk[uart6_ipg_gate] = imx_clk_gate("uart6_ipg_gate", "ipg", CCM_PCCR1, 26);
	clk[uart5_ipg_gate] = imx_clk_gate("uart5_ipg_gate", "ipg", CCM_PCCR1, 27);
	clk[uart4_ipg_gate] = imx_clk_gate("uart4_ipg_gate", "ipg", CCM_PCCR1, 28);
	clk[uart3_ipg_gate] = imx_clk_gate("uart3_ipg_gate", "ipg", CCM_PCCR1, 29);
	clk[uart2_ipg_gate] = imx_clk_gate("uart2_ipg_gate", "ipg", CCM_PCCR1, 30);
	clk[uart1_ipg_gate] = imx_clk_gate("uart1_ipg_gate", "ipg", CCM_PCCR1, 31);

	for (i = 0; i < ARRAY_SIZE(clk); i++)
		if (IS_ERR(clk[i]))
			pr_err("i.MX27 clk %d: register failed with %ld\n",
				i, PTR_ERR(clk[i]));

	clk_register_clkdev(clk[uart1_ipg_gate], "ipg", "imx21-uart.0");
	clk_register_clkdev(clk[per1_gate], "per", "imx21-uart.0");
	clk_register_clkdev(clk[uart2_ipg_gate], "ipg", "imx21-uart.1");
	clk_register_clkdev(clk[per1_gate], "per", "imx21-uart.1");
	clk_register_clkdev(clk[uart3_ipg_gate], "ipg", "imx21-uart.2");
	clk_register_clkdev(clk[per1_gate], "per", "imx21-uart.2");
	clk_register_clkdev(clk[uart4_ipg_gate], "ipg", "imx21-uart.3");
	clk_register_clkdev(clk[per1_gate], "per", "imx21-uart.3");
	clk_register_clkdev(clk[uart5_ipg_gate], "ipg", "imx21-uart.4");
	clk_register_clkdev(clk[per1_gate], "per", "imx21-uart.4");
	clk_register_clkdev(clk[uart6_ipg_gate], "ipg", "imx21-uart.5");
	clk_register_clkdev(clk[per1_gate], "per", "imx21-uart.5");
	clk_register_clkdev(clk[gpt1_ipg_gate], "ipg", "imx-gpt.0");
	clk_register_clkdev(clk[per1_gate], "per", "imx-gpt.0");
	clk_register_clkdev(clk[gpt2_ipg_gate], "ipg", "imx-gpt.1");
	clk_register_clkdev(clk[per1_gate], "per", "imx-gpt.1");
	clk_register_clkdev(clk[gpt3_ipg_gate], "ipg", "imx-gpt.2");
	clk_register_clkdev(clk[per1_gate], "per", "imx-gpt.2");
	clk_register_clkdev(clk[gpt4_ipg_gate], "ipg", "imx-gpt.3");
	clk_register_clkdev(clk[per1_gate], "per", "imx-gpt.3");
	clk_register_clkdev(clk[gpt5_ipg_gate], "ipg", "imx-gpt.4");
	clk_register_clkdev(clk[per1_gate], "per", "imx-gpt.4");
	clk_register_clkdev(clk[gpt6_ipg_gate], "ipg", "imx-gpt.5");
	clk_register_clkdev(clk[per1_gate], "per", "imx-gpt.5");
	clk_register_clkdev(clk[pwm_ipg_gate], NULL, "mxc_pwm.0");
	clk_register_clkdev(clk[per2_gate], "per", "imx21-mmc.0");
	clk_register_clkdev(clk[sdhc1_ipg_gate], "ipg", "imx21-mmc.0");
	clk_register_clkdev(clk[per2_gate], "per", "imx21-mmc.1");
	clk_register_clkdev(clk[sdhc2_ipg_gate], "ipg", "imx21-mmc.1");
	clk_register_clkdev(clk[per2_gate], "per", "imx21-mmc.2");
	clk_register_clkdev(clk[sdhc2_ipg_gate], "ipg", "imx21-mmc.2");
	clk_register_clkdev(clk[cspi1_ipg_gate], NULL, "imx27-cspi.0");
	clk_register_clkdev(clk[cspi2_ipg_gate], NULL, "imx27-cspi.1");
	clk_register_clkdev(clk[cspi3_ipg_gate], NULL, "imx27-cspi.2");
	clk_register_clkdev(clk[per3_gate], "per", "imx21-fb.0");
	clk_register_clkdev(clk[lcdc_ipg_gate], "ipg", "imx21-fb.0");
	clk_register_clkdev(clk[lcdc_ahb_gate], "ahb", "imx21-fb.0");
	clk_register_clkdev(clk[csi_ahb_gate], "ahb", "imx27-camera.0");
	clk_register_clkdev(clk[per4_gate], "per", "imx27-camera.0");
	clk_register_clkdev(clk[usb_div], "per", "imx-udc-mx27");
	clk_register_clkdev(clk[usb_ipg_gate], "ipg", "imx-udc-mx27");
	clk_register_clkdev(clk[usb_ahb_gate], "ahb", "imx-udc-mx27");
	clk_register_clkdev(clk[usb_div], "per", "mxc-ehci.0");
	clk_register_clkdev(clk[usb_ipg_gate], "ipg", "mxc-ehci.0");
	clk_register_clkdev(clk[usb_ahb_gate], "ahb", "mxc-ehci.0");
	clk_register_clkdev(clk[usb_div], "per", "mxc-ehci.1");
	clk_register_clkdev(clk[usb_ipg_gate], "ipg", "mxc-ehci.1");
	clk_register_clkdev(clk[usb_ahb_gate], "ahb", "mxc-ehci.1");
	clk_register_clkdev(clk[usb_div], "per", "mxc-ehci.2");
	clk_register_clkdev(clk[usb_ipg_gate], "ipg", "mxc-ehci.2");
	clk_register_clkdev(clk[usb_ahb_gate], "ahb", "mxc-ehci.2");
	clk_register_clkdev(clk[ssi1_ipg_gate], NULL, "imx-ssi.0");
	clk_register_clkdev(clk[ssi2_ipg_gate], NULL, "imx-ssi.1");
	clk_register_clkdev(clk[nfc_baud_gate], NULL, "imx27-nand.0");
	clk_register_clkdev(clk[vpu_baud_gate], "per", "coda-imx27.0");
	clk_register_clkdev(clk[vpu_ahb_gate], "ahb", "coda-imx27.0");
	clk_register_clkdev(clk[dma_ahb_gate], "ahb", "imx27-dma");
	clk_register_clkdev(clk[dma_ipg_gate], "ipg", "imx27-dma");
	clk_register_clkdev(clk[fec_ipg_gate], "ipg", "imx27-fec.0");
	clk_register_clkdev(clk[fec_ahb_gate], "ahb", "imx27-fec.0");
	clk_register_clkdev(clk[wdog_ipg_gate], NULL, "imx2-wdt.0");
	clk_register_clkdev(clk[i2c1_ipg_gate], NULL, "imx21-i2c.0");
	clk_register_clkdev(clk[i2c2_ipg_gate], NULL, "imx21-i2c.1");
	clk_register_clkdev(clk[owire_ipg_gate], NULL, "mxc_w1.0");
	clk_register_clkdev(clk[kpp_ipg_gate], NULL, "imx-keypad");
	clk_register_clkdev(clk[emma_ahb_gate], "emma-ahb", "imx27-camera.0");
	clk_register_clkdev(clk[emma_ipg_gate], "emma-ipg", "imx27-camera.0");
	clk_register_clkdev(clk[emma_ahb_gate], "ahb", "m2m-emmaprp.0");
	clk_register_clkdev(clk[emma_ipg_gate], "ipg", "m2m-emmaprp.0");
	clk_register_clkdev(clk[iim_ipg_gate], "iim", NULL);
	clk_register_clkdev(clk[gpio_ipg_gate], "gpio", NULL);
	clk_register_clkdev(clk[brom_ahb_gate], "brom", NULL);
	clk_register_clkdev(clk[ata_ahb_gate], "ata", NULL);
	clk_register_clkdev(clk[rtc_ipg_gate], NULL, "imx21-rtc");
	clk_register_clkdev(clk[scc_ipg_gate], "scc", NULL);
	clk_register_clkdev(clk[cpu_div], "cpu", NULL);
	clk_register_clkdev(clk[emi_ahb_gate], "emi_ahb" , NULL);
	clk_register_clkdev(clk[ssi1_baud_gate], "bitrate" , "imx-ssi.0");
	clk_register_clkdev(clk[ssi2_baud_gate], "bitrate" , "imx-ssi.1");

	mxc_timer_init(MX27_IO_ADDRESS(MX27_GPT1_BASE_ADDR), MX27_INT_GPT1);

	clk_prepare_enable(clk[emi_ahb_gate]);

	imx_print_silicon_rev("i.MX27", mx27_revision());

	return 0;
}

#ifdef CONFIG_OF
int __init mx27_clocks_init_dt(void)
{
	struct device_node *np;
	u32 fref = 26000000; /* default */

	for_each_compatible_node(np, NULL, "fixed-clock") {
		if (!of_device_is_compatible(np, "fsl,imx-osc26m"))
			continue;

		if (!of_property_read_u32(np, "clock-frequency", &fref))
			break;
	}

	return mx27_clocks_init(fref);
}
#endif
