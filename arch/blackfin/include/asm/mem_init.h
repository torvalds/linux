/*
 * arch/blackfin/include/asm/mem_init.h - reprogram clocks / memory
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __MEM_INIT_H__
#define __MEM_INIT_H__

#if defined(EBIU_SDGCTL)
#if defined(CONFIG_MEM_MT48LC16M16A2TG_75) || \
    defined(CONFIG_MEM_MT48LC64M4A2FB_7E) || \
    defined(CONFIG_MEM_MT48LC16M8A2TG_75) || \
    defined(CONFIG_MEM_MT48LC32M8A2_75) || \
    defined(CONFIG_MEM_MT48LC8M32B2B5_7) || \
    defined(CONFIG_MEM_MT48LC32M16A2TG_75) || \
    defined(CONFIG_MEM_MT48LC32M8A2_75)
#if (CONFIG_SCLK_HZ > 119402985)
#define SDRAM_tRP       TRP_2
#define SDRAM_tRP_num   2
#define SDRAM_tRAS      TRAS_7
#define SDRAM_tRAS_num  7
#define SDRAM_tRCD      TRCD_2
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 104477612) && (CONFIG_SCLK_HZ <= 119402985)
#define SDRAM_tRP       TRP_2
#define SDRAM_tRP_num   2
#define SDRAM_tRAS      TRAS_6
#define SDRAM_tRAS_num  6
#define SDRAM_tRCD      TRCD_2
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 89552239) && (CONFIG_SCLK_HZ <= 104477612)
#define SDRAM_tRP       TRP_2
#define SDRAM_tRP_num   2
#define SDRAM_tRAS      TRAS_5
#define SDRAM_tRAS_num  5
#define SDRAM_tRCD      TRCD_2
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 74626866) && (CONFIG_SCLK_HZ <= 89552239)
#define SDRAM_tRP       TRP_2
#define SDRAM_tRP_num   2
#define SDRAM_tRAS      TRAS_4
#define SDRAM_tRAS_num  4
#define SDRAM_tRCD      TRCD_2
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 66666667) && (CONFIG_SCLK_HZ <= 74626866)
#define SDRAM_tRP       TRP_2
#define SDRAM_tRP_num   2
#define SDRAM_tRAS      TRAS_3
#define SDRAM_tRAS_num  3
#define SDRAM_tRCD      TRCD_2
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 59701493) && (CONFIG_SCLK_HZ <= 66666667)
#define SDRAM_tRP       TRP_1
#define SDRAM_tRP_num   1
#define SDRAM_tRAS      TRAS_4
#define SDRAM_tRAS_num  4
#define SDRAM_tRCD      TRCD_1
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 44776119) && (CONFIG_SCLK_HZ <= 59701493)
#define SDRAM_tRP       TRP_1
#define SDRAM_tRP_num   1
#define SDRAM_tRAS      TRAS_3
#define SDRAM_tRAS_num  3
#define SDRAM_tRCD      TRCD_1
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 29850746) && (CONFIG_SCLK_HZ <= 44776119)
#define SDRAM_tRP       TRP_1
#define SDRAM_tRP_num   1
#define SDRAM_tRAS      TRAS_2
#define SDRAM_tRAS_num  2
#define SDRAM_tRCD      TRCD_1
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ <= 29850746)
#define SDRAM_tRP       TRP_1
#define SDRAM_tRP_num   1
#define SDRAM_tRAS      TRAS_1
#define SDRAM_tRAS_num  1
#define SDRAM_tRCD      TRCD_1
#define SDRAM_tWR       TWR_2
#endif
#endif

/*
 * The BF526-EZ-Board changed SDRAM chips between revisions,
 * so we use below timings to accommodate both.
 */
#if defined(CONFIG_MEM_MT48H32M16LFCJ_75)
#if (CONFIG_SCLK_HZ > 119402985)
#define SDRAM_tRP       TRP_2
#define SDRAM_tRP_num   2
#define SDRAM_tRAS      TRAS_8
#define SDRAM_tRAS_num  8
#define SDRAM_tRCD      TRCD_2
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 104477612) && (CONFIG_SCLK_HZ <= 119402985)
#define SDRAM_tRP       TRP_2
#define SDRAM_tRP_num   2
#define SDRAM_tRAS      TRAS_7
#define SDRAM_tRAS_num  7
#define SDRAM_tRCD      TRCD_2
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 89552239) && (CONFIG_SCLK_HZ <= 104477612)
#define SDRAM_tRP       TRP_2
#define SDRAM_tRP_num   2
#define SDRAM_tRAS      TRAS_6
#define SDRAM_tRAS_num  6
#define SDRAM_tRCD      TRCD_2
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 74626866) && (CONFIG_SCLK_HZ <= 89552239)
#define SDRAM_tRP       TRP_2
#define SDRAM_tRP_num   2
#define SDRAM_tRAS      TRAS_5
#define SDRAM_tRAS_num  5
#define SDRAM_tRCD      TRCD_2
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 66666667) && (CONFIG_SCLK_HZ <= 74626866)
#define SDRAM_tRP       TRP_2
#define SDRAM_tRP_num   2
#define SDRAM_tRAS      TRAS_4
#define SDRAM_tRAS_num  4
#define SDRAM_tRCD      TRCD_2
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 59701493) && (CONFIG_SCLK_HZ <= 66666667)
#define SDRAM_tRP       TRP_2
#define SDRAM_tRP_num   2
#define SDRAM_tRAS      TRAS_4
#define SDRAM_tRAS_num  4
#define SDRAM_tRCD      TRCD_1
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 44776119) && (CONFIG_SCLK_HZ <= 59701493)
#define SDRAM_tRP       TRP_2
#define SDRAM_tRP_num   2
#define SDRAM_tRAS      TRAS_3
#define SDRAM_tRAS_num  3
#define SDRAM_tRCD      TRCD_1
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ > 29850746) && (CONFIG_SCLK_HZ <= 44776119)
#define SDRAM_tRP       TRP_1
#define SDRAM_tRP_num   1
#define SDRAM_tRAS      TRAS_3
#define SDRAM_tRAS_num  3
#define SDRAM_tRCD      TRCD_1
#define SDRAM_tWR       TWR_2
#endif
#if (CONFIG_SCLK_HZ <= 29850746)
#define SDRAM_tRP       TRP_1
#define SDRAM_tRP_num   1
#define SDRAM_tRAS      TRAS_2
#define SDRAM_tRAS_num  2
#define SDRAM_tRCD      TRCD_1
#define SDRAM_tWR       TWR_2
#endif
#endif

#if defined(CONFIG_MEM_MT48LC16M8A2TG_75) || \
    defined(CONFIG_MEM_MT48LC8M32B2B5_7)
  /*SDRAM INFORMATION: */
#define SDRAM_Tref  64		/* Refresh period in milliseconds   */
#define SDRAM_NRA   4096	/* Number of row addresses in SDRAM */
#define SDRAM_CL    CL_3
#endif

#if defined(CONFIG_MEM_MT48LC32M8A2_75) || \
    defined(CONFIG_MEM_MT48LC64M4A2FB_7E) || \
    defined(CONFIG_MEM_MT48LC32M16A2TG_75) || \
    defined(CONFIG_MEM_MT48LC16M16A2TG_75) || \
    defined(CONFIG_MEM_MT48LC32M8A2_75)
  /*SDRAM INFORMATION: */
#define SDRAM_Tref  64		/* Refresh period in milliseconds   */
#define SDRAM_NRA   8192	/* Number of row addresses in SDRAM */
#define SDRAM_CL    CL_3
#endif

#if defined(CONFIG_MEM_MT48H32M16LFCJ_75)
  /*SDRAM INFORMATION: */
#define SDRAM_Tref  64		/* Refresh period in milliseconds   */
#define SDRAM_NRA   8192	/* Number of row addresses in SDRAM */
#define SDRAM_CL    CL_2
#endif


#ifdef CONFIG_BFIN_KERNEL_CLOCK_MEMINIT_CALC
/* Equation from section 17 (p17-46) of BF533 HRM */
#define mem_SDRRC       (((CONFIG_SCLK_HZ / 1000) * SDRAM_Tref) / SDRAM_NRA) - (SDRAM_tRAS_num + SDRAM_tRP_num)

/* Enable SCLK Out */
#define mem_SDGCTL        (SCTLE | SDRAM_CL | SDRAM_tRAS | SDRAM_tRP | SDRAM_tRCD | SDRAM_tWR | PSS)
#else
#define mem_SDRRC 	CONFIG_MEM_SDRRC
#define mem_SDGCTL	CONFIG_MEM_SDGCTL
#endif
#endif


#if defined(EBIU_DDRCTL0)
#define MIN_DDR_SCLK(x)	(x*(CONFIG_SCLK_HZ/1000/1000)/1000 + 1)
#define MAX_DDR_SCLK(x)	(x*(CONFIG_SCLK_HZ/1000/1000)/1000)
#define DDR_CLK_HZ(x)	(1000*1000*1000/x)

#if defined(CONFIG_MEM_MT46V32M16_6T)
#define DDR_SIZE	DEVSZ_512
#define DDR_WIDTH	DEVWD_16
#define DDR_MAX_tCK	13

#define DDR_tRC		DDR_TRC(MIN_DDR_SCLK(60))
#define DDR_tRAS	DDR_TRAS(MIN_DDR_SCLK(42))
#define DDR_tRP		DDR_TRP(MIN_DDR_SCLK(15))
#define DDR_tRFC	DDR_TRFC(MIN_DDR_SCLK(72))
#define DDR_tREFI	DDR_TREFI(MAX_DDR_SCLK(7800))

#define DDR_tRCD	DDR_TRCD(MIN_DDR_SCLK(15))
#define DDR_tWTR	DDR_TWTR(1)
#define DDR_tMRD	DDR_TMRD(MIN_DDR_SCLK(12))
#define DDR_tWR		DDR_TWR(MIN_DDR_SCLK(15))
#endif

#if defined(CONFIG_MEM_MT46V32M16_5B)
#define DDR_SIZE	DEVSZ_512
#define DDR_WIDTH	DEVWD_16
#define DDR_MAX_tCK	13

#define DDR_tRC		DDR_TRC(MIN_DDR_SCLK(55))
#define DDR_tRAS	DDR_TRAS(MIN_DDR_SCLK(40))
#define DDR_tRP		DDR_TRP(MIN_DDR_SCLK(15))
#define DDR_tRFC	DDR_TRFC(MIN_DDR_SCLK(70))
#define DDR_tREFI	DDR_TREFI(MAX_DDR_SCLK(7800))

#define DDR_tRCD	DDR_TRCD(MIN_DDR_SCLK(15))
#define DDR_tWTR	DDR_TWTR(2)
#define DDR_tMRD	DDR_TMRD(MIN_DDR_SCLK(10))
#define DDR_tWR		DDR_TWR(MIN_DDR_SCLK(15))
#endif

#if (CONFIG_SCLK_HZ < DDR_CLK_HZ(DDR_MAX_tCK))
# error "CONFIG_SCLK_HZ is too small (<DDR_CLK_HZ(DDR_MAX_tCK) Hz)."
#elif(CONFIG_SCLK_HZ <= 133333333)
# define	DDR_CL		CL_2
#else
# error "CONFIG_SCLK_HZ is too large (>133333333 Hz)."
#endif

#ifdef CONFIG_BFIN_KERNEL_CLOCK_MEMINIT_CALC
#define mem_DDRCTL0	(DDR_tRP | DDR_tRAS | DDR_tRC | DDR_tRFC | DDR_tREFI)
#define mem_DDRCTL1	(DDR_DATWIDTH | EXTBANK_1 | DDR_SIZE | DDR_WIDTH | DDR_tWTR \
			| DDR_tMRD | DDR_tWR | DDR_tRCD)
#define mem_DDRCTL2	DDR_CL
#else
#define mem_DDRCTL0	CONFIG_MEM_DDRCTL0
#define mem_DDRCTL1	CONFIG_MEM_DDRCTL1
#define mem_DDRCTL2	CONFIG_MEM_DDRCTL2
#endif
#endif

#if defined CONFIG_CLKIN_HALF
#define CLKIN_HALF       1
#else
#define CLKIN_HALF       0
#endif

#if defined CONFIG_PLL_BYPASS
#define PLL_BYPASS      1
#else
#define PLL_BYPASS       0
#endif

#ifdef CONFIG_BF60x

/* DMC status bits */
#define IDLE			0x1
#define MEMINITDONE		0x4
#define SRACK			0x8
#define PDACK			0x10
#define DPDACK			0x20
#define DLLCALDONE		0x2000
#define PENDREF			0xF0000
#define PHYRDPHASE		0xF00000
#define PHYRDPHASE_OFFSET	20

/* DMC control bits */
#define LPDDR			0x2
#define INIT			0x4
#define	SRREQ			0x8
#define PDREQ			0x10
#define DPDREQ			0x20
#define PREC			0x40
#define ADDRMODE		0x100
#define RDTOWR			0xE00
#define PPREF			0x1000
#define DLLCAL			0x2000

/* DMC DLL control bits */
#define DLLCALRDCNT		0xFF
#define DATACYC			0xF00
#define DATACYC_OFFSET		8

/* CGU Divisor bits */
#define CSEL_OFFSET		0
#define S0SEL_OFFSET		5
#define SYSSEL_OFFSET		8
#define S1SEL_OFFSET		13
#define DSEL_OFFSET		16
#define OSEL_OFFSET		22
#define ALGN			0x20000000
#define UPDT			0x40000000
#define LOCK			0x80000000

/* CGU Status bits */
#define PLLEN			0x1
#define PLLBP			0x2
#define PLOCK			0x4
#define CLKSALGN		0x8

/* CGU Control bits */
#define MSEL_MASK		0x7F00
#define DF_MASK			0x1

struct ddr_config {
	u32 ddr_clk;
	u32 dmc_ddrctl;
	u32 dmc_effctl;
	u32 dmc_ddrcfg;
	u32 dmc_ddrtr0;
	u32 dmc_ddrtr1;
	u32 dmc_ddrtr2;
	u32 dmc_ddrmr;
	u32 dmc_ddrmr1;
};

#if defined(CONFIG_MEM_MT47H64M16)
static struct ddr_config ddr_config_table[] __attribute__((section(".data_l1"))) = {
	[0] = {
		.ddr_clk    = 125,
		.dmc_ddrctl = 0x00000904,
		.dmc_effctl = 0x004400C0,
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
		.dmc_effctl = 0x004400C0,
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
		.dmc_effctl = 0x004400C0,
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
		.dmc_effctl = 0x004400C0,
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
		.dmc_effctl = 0x004400C0,
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
		.dmc_effctl = 0x004400C0,
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
		.dmc_effctl = 0x004400C0,
		.dmc_ddrcfg = 0x00000422,
		.dmc_ddrtr0 = 0x20E0A424,
		.dmc_ddrtr1 = 0x3020079E,
		.dmc_ddrtr2 = 0x0032050D,
		.dmc_ddrmr  = 0x00000842,
		.dmc_ddrmr1 = 0x4,
	},
};
#endif

static inline void dmc_enter_self_refresh(void)
{
	if (bfin_read_DMC0_STAT() & MEMINITDONE) {
		bfin_write_DMC0_CTL(bfin_read_DMC0_CTL() | SRREQ);
		while (!(bfin_read_DMC0_STAT() & SRACK))
			continue;
	}
}

static inline void dmc_exit_self_refresh(void)
{
	if (bfin_read_DMC0_STAT() & MEMINITDONE) {
		bfin_write_DMC0_CTL(bfin_read_DMC0_CTL() & ~SRREQ);
		while (bfin_read_DMC0_STAT() & SRACK)
			continue;
	}
}

static inline void init_cgu(u32 cgu_div, u32 cgu_ctl)
{
	dmc_enter_self_refresh();

	/* Don't set the same value of MSEL and DF to CGU_CTL */
	if ((bfin_read32(CGU0_CTL) & (MSEL_MASK | DF_MASK))
		!= cgu_ctl) {
		bfin_write32(CGU0_DIV, cgu_div);
		bfin_write32(CGU0_CTL, cgu_ctl);
		while ((bfin_read32(CGU0_STAT) & (CLKSALGN | PLLBP)) ||
			!(bfin_read32(CGU0_STAT) & PLOCK))
			continue;
	}

	bfin_write32(CGU0_DIV, cgu_div | UPDT);
	while (bfin_read32(CGU0_STAT) & CLKSALGN)
		continue;

	dmc_exit_self_refresh();
}

static inline void init_dmc(u32 dmc_clk)
{
	int i, dlldatacycle, dll_ctl;

	for (i = 0; i < 7; i++) {
		if (ddr_config_table[i].ddr_clk == dmc_clk) {
			bfin_write_DMC0_CFG(ddr_config_table[i].dmc_ddrcfg);
			bfin_write_DMC0_TR0(ddr_config_table[i].dmc_ddrtr0);
			bfin_write_DMC0_TR1(ddr_config_table[i].dmc_ddrtr1);
			bfin_write_DMC0_TR2(ddr_config_table[i].dmc_ddrtr2);
			bfin_write_DMC0_MR(ddr_config_table[i].dmc_ddrmr);
			bfin_write_DMC0_EMR1(ddr_config_table[i].dmc_ddrmr1);
			bfin_write_DMC0_EFFCTL(ddr_config_table[i].dmc_effctl);
			bfin_write_DMC0_CTL(ddr_config_table[i].dmc_ddrctl);
			break;
		}
	}

	while (!(bfin_read_DMC0_STAT() & MEMINITDONE))
		continue;

	dlldatacycle = (bfin_read_DMC0_STAT() & PHYRDPHASE) >> PHYRDPHASE_OFFSET;
	dll_ctl = bfin_read_DMC0_DLLCTL();
	dll_ctl &= ~DATACYC;
	bfin_write_DMC0_DLLCTL(dll_ctl | (dlldatacycle << DATACYC_OFFSET));

	while (!(bfin_read_DMC0_STAT() & DLLCALDONE))
		continue;
}
#endif

#endif /*__MEM_INIT_H__*/

