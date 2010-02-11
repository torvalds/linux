/*
 * arch/blackfin/include/asm/mem_init.h - reprogram clocks / memory
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#if defined(EBIU_SDGCTL)
#if defined(CONFIG_MEM_MT48LC16M16A2TG_75) || \
    defined(CONFIG_MEM_MT48LC64M4A2FB_7E) || \
    defined(CONFIG_MEM_MT48LC16M8A2TG_75) || \
    defined(CONFIG_MEM_GENERIC_BOARD) || \
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
    defined(CONFIG_MEM_GENERIC_BOARD) || \
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

#if defined(CONFIG_MEM_GENERIC_BOARD)
#define DDR_SIZE	DEVSZ_512
#define DDR_WIDTH	DEVWD_16
#define DDR_MAX_tCK	13

#define DDR_tRCD	DDR_TRCD(3)
#define DDR_tWTR	DDR_TWTR(2)
#define DDR_tWR		DDR_TWR(2)
#define DDR_tMRD	DDR_TMRD(2)
#define DDR_tRP		DDR_TRP(3)
#define DDR_tRAS	DDR_TRAS(7)
#define DDR_tRC		DDR_TRC(10)
#define DDR_tRFC	DDR_TRFC(12)
#define DDR_tREFI	DDR_TREFI(1288)
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
