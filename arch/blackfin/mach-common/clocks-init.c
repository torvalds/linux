/*
 * arch/blackfin/mach-common/clocks-init.c - reprogram clocks / memory
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/blackfin.h>

#include <asm/dma.h>
#include <asm/clocks.h>
#include <asm/mem_init.h>
#include <asm/dpmc.h>

#ifdef CONFIG_BF60x

#define CGU_CTL_VAL ((CONFIG_VCO_MULT << 8) | CLKIN_HALF)
#define CGU_DIV_VAL \
	((CONFIG_CCLK_DIV   << CSEL_OFFSET)   | \
	(CONFIG_SCLK_DIV << SYSSEL_OFFSET)   | \
	(CONFIG_SCLK0_DIV  << S0SEL_OFFSET)  | \
	(CONFIG_SCLK1_DIV  << S1SEL_OFFSET)  | \
	(CONFIG_DCLK_DIV   << DSEL_OFFSET))

#define CONFIG_BFIN_DCLK (((CONFIG_CLKIN_HZ * CONFIG_VCO_MULT) / CONFIG_DCLK_DIV) / 1000000)
#if ((CONFIG_BFIN_DCLK != 125) && \
	(CONFIG_BFIN_DCLK != 133) && (CONFIG_BFIN_DCLK != 150) && \
	(CONFIG_BFIN_DCLK != 166) && (CONFIG_BFIN_DCLK != 200) && \
	(CONFIG_BFIN_DCLK != 225) && (CONFIG_BFIN_DCLK != 250))
#error "DCLK must be in (125, 133, 150, 166, 200, 225, 250)MHz"
#endif

#else
#define SDGCTL_WIDTH (1 << 31)	/* SDRAM external data path width */
#define PLL_CTL_VAL \
	(((CONFIG_VCO_MULT & 63) << 9) | CLKIN_HALF | \
		(PLL_BYPASS << 8) | (ANOMALY_05000305 ? 0 : 0x8000))
#endif

__attribute__((l1_text))
static void do_sync(void)
{
	__builtin_bfin_ssync();
}

__attribute__((l1_text))
void init_clocks(void)
{
	/* Kill any active DMAs as they may trigger external memory accesses
	 * in the middle of reprogramming things, and that'll screw us up.
	 * For example, any automatic DMAs left by U-Boot for splash screens.
	 */
#ifdef CONFIG_BF60x
	init_cgu(CGU_DIV_VAL, CGU_CTL_VAL);
	init_dmc(CONFIG_BFIN_DCLK);
#else
	size_t i;
	for (i = 0; i < MAX_DMA_CHANNELS; ++i) {
		struct dma_register *dma = dma_io_base_addr[i];
		dma->cfg = 0;
	}

	do_sync();

#ifdef SIC_IWR0
	bfin_write_SIC_IWR0(IWR_ENABLE(0));
# ifdef SIC_IWR1
	/* BF52x system reset does not properly reset SIC_IWR1 which
	 * will screw up the bootrom as it relies on MDMA0/1 waking it
	 * up from IDLE instructions.  See this report for more info:
	 * http://blackfin.uclinux.org/gf/tracker/4323
	 */
	if (ANOMALY_05000435)
		bfin_write_SIC_IWR1(IWR_ENABLE(10) | IWR_ENABLE(11));
	else
		bfin_write_SIC_IWR1(IWR_DISABLE_ALL);
# endif
# ifdef SIC_IWR2
	bfin_write_SIC_IWR2(IWR_DISABLE_ALL);
# endif
#else
	bfin_write_SIC_IWR(IWR_ENABLE(0));
#endif
	do_sync();
#ifdef EBIU_SDGCTL
	bfin_write_EBIU_SDGCTL(bfin_read_EBIU_SDGCTL() | SRFS);
	do_sync();
#endif

#ifdef CLKBUFOE
	bfin_write16(VR_CTL, bfin_read_VR_CTL() | CLKBUFOE);
	do_sync();
	__asm__ __volatile__("IDLE;");
#endif
	bfin_write_PLL_LOCKCNT(0x300);
	do_sync();
	/* We always write PLL_CTL thus avoiding Anomaly 05000242 */
	bfin_write16(PLL_CTL, PLL_CTL_VAL);
	__asm__ __volatile__("IDLE;");
	bfin_write_PLL_DIV(CONFIG_CCLK_ACT_DIV | CONFIG_SCLK_DIV);
#ifdef EBIU_SDGCTL
	bfin_write_EBIU_SDRRC(mem_SDRRC);
	bfin_write_EBIU_SDGCTL((bfin_read_EBIU_SDGCTL() & SDGCTL_WIDTH) | mem_SDGCTL);
#else
	bfin_write_EBIU_RSTCTL(bfin_read_EBIU_RSTCTL() & ~(SRREQ));
	do_sync();
	bfin_write_EBIU_RSTCTL(bfin_read_EBIU_RSTCTL() | 0x1);
	bfin_write_EBIU_DDRCTL0(mem_DDRCTL0);
	bfin_write_EBIU_DDRCTL1(mem_DDRCTL1);
	bfin_write_EBIU_DDRCTL2(mem_DDRCTL2);
#ifdef CONFIG_MEM_EBIU_DDRQUE
	bfin_write_EBIU_DDRQUE(CONFIG_MEM_EBIU_DDRQUE);
#endif
#endif
#endif
	do_sync();
	bfin_read16(0);

}
