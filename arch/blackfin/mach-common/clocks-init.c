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
#define CSEL_P			0
#define S0SEL_P			5
#define SYSSEL_P		8
#define S1SEL_P			13
#define DSEL_P			16
#define OSEL_P			22
#define ALGN_P			29
#define UPDT_P			30
#define LOCK_P			31

#define CGU_CTL_VAL ((CONFIG_VCO_MULT << 8) | CLKIN_HALF)
#define CGU_DIV_VAL \
	((CONFIG_CCLK_DIV   << CSEL_P)   | \
	(CONFIG_SCLK_DIV << SYSSEL_P)   | \
	(CONFIG_SCLK0_DIV  << S0SEL_P)  | \
	(CONFIG_SCLK1_DIV  << S1SEL_P)  | \
	(CONFIG_DCLK_DIV   << DSEL_P))

#define CONFIG_BFIN_DCLK (((CONFIG_CLKIN_HZ * CONFIG_VCO_MULT) / CONFIG_DCLK_DIV) / 1000000)
#if ((CONFIG_BFIN_DCLK != 125) && \
	(CONFIG_BFIN_DCLK != 133) && (CONFIG_BFIN_DCLK != 150) && \
	(CONFIG_BFIN_DCLK != 166) && (CONFIG_BFIN_DCLK != 200) && \
	(CONFIG_BFIN_DCLK != 225) && (CONFIG_BFIN_DCLK != 250))
#error "DCLK must be in (125, 133, 150, 166, 200, 225, 250)MHz"
#endif
struct ddr_config {
	u32 ddr_clk;
	u32 dmc_ddrctl;
	u32 dmc_ddrcfg;
	u32 dmc_ddrtr0;
	u32 dmc_ddrtr1;
	u32 dmc_ddrtr2;
	u32 dmc_ddrmr;
	u32 dmc_ddrmr1;
};

struct ddr_config ddr_config_table[] __attribute__((section(".data_l1"))) = {
	[0] = {
		.ddr_clk    = 125,
		.dmc_ddrctl = 0x00000904,
		.dmc_ddrcfg = 0x00000422,
		.dmc_ddrtr0 = 0x20705212,
		.dmc_ddrtr1 = 0x201003CF,
		.dmc_ddrtr2 = 0x00320107,
		.dmc_ddrmr  = 0x00000422,
		.dmc_ddrmr1 = 0x4,
	},
	[1] = {
		.ddr_clk    = 133,
		.dmc_ddrctl = 0x00000904,
		.dmc_ddrcfg = 0x00000422,
		.dmc_ddrtr0 = 0x20806313,
		.dmc_ddrtr1 = 0x2013040D,
		.dmc_ddrtr2 = 0x00320108,
		.dmc_ddrmr  = 0x00000632,
		.dmc_ddrmr1 = 0x4,
	},
	[2] = {
		.ddr_clk    = 150,
		.dmc_ddrctl = 0x00000904,
		.dmc_ddrcfg = 0x00000422,
		.dmc_ddrtr0 = 0x20A07323,
		.dmc_ddrtr1 = 0x20160492,
		.dmc_ddrtr2 = 0x00320209,
		.dmc_ddrmr  = 0x00000632,
		.dmc_ddrmr1 = 0x4,
	},
	[3] = {
		.ddr_clk    = 166,
		.dmc_ddrctl = 0x00000904,
		.dmc_ddrcfg = 0x00000422,
		.dmc_ddrtr0 = 0x20A07323,
		.dmc_ddrtr1 = 0x2016050E,
		.dmc_ddrtr2 = 0x00320209,
		.dmc_ddrmr  = 0x00000632,
		.dmc_ddrmr1 = 0x4,
	},
	[4] = {
		.ddr_clk    = 200,
		.dmc_ddrctl = 0x00000904,
		.dmc_ddrcfg = 0x00000422,
		.dmc_ddrtr0 = 0x20a07323,
		.dmc_ddrtr1 = 0x2016050f,
		.dmc_ddrtr2 = 0x00320509,
		.dmc_ddrmr  = 0x00000632,
		.dmc_ddrmr1 = 0x4,
	},
	[5] = {
		.ddr_clk    = 225,
		.dmc_ddrctl = 0x00000904,
		.dmc_ddrcfg = 0x00000422,
		.dmc_ddrtr0 = 0x20E0A424,
		.dmc_ddrtr1 = 0x302006DB,
		.dmc_ddrtr2 = 0x0032020D,
		.dmc_ddrmr  = 0x00000842,
		.dmc_ddrmr1 = 0x4,
	},
	[6] = {
		.ddr_clk    = 250,
		.dmc_ddrctl = 0x00000904,
		.dmc_ddrcfg = 0x00000422,
		.dmc_ddrtr0 = 0x20E0A424,
		.dmc_ddrtr1 = 0x3020079E,
		.dmc_ddrtr2 = 0x0032020D,
		.dmc_ddrmr  = 0x00000842,
		.dmc_ddrmr1 = 0x4,
	},
};
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
	int i, dlldatacycle, dll_ctl;
	bfin_write32(CGU0_DIV, CGU_DIV_VAL);
	bfin_write32(CGU0_CTL, CGU_CTL_VAL);
	while ((bfin_read32(CGU0_STAT) & 0x8) || !(bfin_read32(CGU0_STAT) & 0x4))
		continue;

	bfin_write32(CGU0_DIV, CGU_DIV_VAL | (1 << UPDT_P));
	while (bfin_read32(CGU0_STAT) & (1 << 3))
		continue;

	for (i = 0; i < 7; i++) {
		if (ddr_config_table[i].ddr_clk == CONFIG_BFIN_DCLK) {
			bfin_write_DDR0_CFG(ddr_config_table[i].dmc_ddrcfg);
			bfin_write_DDR0_TR0(ddr_config_table[i].dmc_ddrtr0);
			bfin_write_DDR0_TR1(ddr_config_table[i].dmc_ddrtr1);
			bfin_write_DDR0_TR2(ddr_config_table[i].dmc_ddrtr2);
			bfin_write_DDR0_MR(ddr_config_table[i].dmc_ddrmr);
			bfin_write_DDR0_EMR1(ddr_config_table[i].dmc_ddrmr1);
			bfin_write_DDR0_CTL(ddr_config_table[i].dmc_ddrctl);
			break;
		}
	}

	do_sync();
	while (!(bfin_read_DDR0_STAT() & 0x4))
		continue;

	dlldatacycle = (bfin_read_DDR0_STAT() & 0x00f00000) >> 20;
	dll_ctl = bfin_read_DDR0_DLLCTL();
	dll_ctl &= 0x0ff;
	bfin_write_DDR0_DLLCTL(dll_ctl | (dlldatacycle << 8));

	do_sync();
	while (!(bfin_read_DDR0_STAT() & 0x2000))
		continue;
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
