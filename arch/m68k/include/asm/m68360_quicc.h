/***********************************
 * $Id: m68360_quicc.h,v 1.1 2002/03/02 15:01:07 gerg Exp $
 ***********************************
 *
 ***************************************
 * Definitions of QUICC memory structures
 ***************************************
 */

#ifndef __M68360_QUICC_H
#define __M68360_QUICC_H

/*
 * include registers and
 * parameter ram definitions files
 */
#include <asm/m68360_regs.h>
#include <asm/m68360_pram.h>



/* Buffer Descriptors */
typedef struct quicc_bd {
    volatile unsigned short     status;
    volatile unsigned short     length;
    volatile unsigned char      *buf;     /* WARNING: This is only true if *char is 32 bits */
} QUICC_BD;


#ifdef MOTOROLA_ORIGINAL
struct user_data {
    /* BASE + 0x000: user data memory */
    volatile unsigned char      udata_bd_ucode[0x400]; /*user data bd's Ucode*/
    volatile unsigned char      udata_bd[0x200];       /*user data Ucode     */
    volatile unsigned char      ucode_ext[0x100];      /*Ucode Extension ram */
    volatile unsigned char      RESERVED1[0x500];      /* Reserved area      */
};
#else
struct user_data {
    /* BASE + 0x000: user data memory */
    volatile unsigned char      udata_bd_ucode[0x400]; /* user data, bds, Ucode*/
    volatile unsigned char      udata_bd1[0x200];       /* user, bds */
    volatile unsigned char      ucode_bd_scratch[0x100]; /* user, bds, ucode scratch */
    volatile unsigned char      udata_bd2[0x100];       /* user, bds */
    volatile unsigned char      RESERVED1[0x400];      /* Reserved area      */
};
#endif


/*
 * internal ram
 */
typedef struct quicc {
	union {
		struct quicc32_pram ch_pram_tbl[32];	/* 32*64(bytes) per channel */	
		struct user_data		u;
	}ch_or_u;	/* multipul or user space */

    /* BASE + 0xc00: PARAMETER RAM */
	union {
		struct scc_pram {
			union {
				struct hdlc_pram        h;
				struct uart_pram        u;
				struct bisync_pram      b;
				struct transparent_pram t;
				unsigned char   RESERVED66[0x70];
			} pscc;               /* scc parameter area (protocol dependent) */
			union {
				struct {
					unsigned char       RESERVED70[0x10];
					struct spi_pram     spi;
					unsigned char       RESERVED72[0x8];
					struct timer_pram   timer;
				} timer_spi;
				struct {
					struct idma_pram idma;
					unsigned char       RESERVED67[0x4];
					union {
						struct smc_uart_pram u;
						struct smc_trnsp_pram t;
					} psmc;
				} idma_smc;
			} pothers;
		} scc;
		struct ethernet_pram    enet_scc;
		struct global_multi_pram        m;
		unsigned char   pr[0x100];
	} pram[4];

    /* reserved */

    /* BASE + 0x1000: INTERNAL REGISTERS */
    /* SIM */
    volatile unsigned long      sim_mcr;        /* module configuration reg */
    volatile unsigned short     sim_simtr;      /* module test register     */
    volatile unsigned char      RESERVED2[0x2]; /* Reserved area            */
    volatile unsigned char      sim_avr;        /* auto vector reg          */
    volatile unsigned char      sim_rsr;        /* reset status reg         */
    volatile unsigned char      RESERVED3[0x2]; /* Reserved area            */
    volatile unsigned char      sim_clkocr;     /* CLCO control register    */
    volatile unsigned char      RESERVED62[0x3];        /* Reserved area    */
    volatile unsigned short     sim_pllcr;      /* PLL control register     */
    volatile unsigned char      RESERVED63[0x2];        /* Reserved area    */
    volatile unsigned short     sim_cdvcr;      /* Clock devider control register */
    volatile unsigned short     sim_pepar;      /* Port E pin assignment register */
    volatile unsigned char      RESERVED64[0xa];        /* Reserved area    */
    volatile unsigned char      sim_sypcr;      /* system protection control*/
    volatile unsigned char      sim_swiv;       /* software interrupt vector*/
    volatile unsigned char      RESERVED6[0x2]; /* Reserved area            */
    volatile unsigned short     sim_picr;       /* periodic interrupt control reg */
    volatile unsigned char      RESERVED7[0x2]; /* Reserved area            */
    volatile unsigned short     sim_pitr;       /* periodic interrupt timing reg */
    volatile unsigned char      RESERVED8[0x3]; /* Reserved area            */
    volatile unsigned char      sim_swsr;       /* software service         */
    volatile unsigned long      sim_bkar;       /* breakpoint address register*/
    volatile unsigned long      sim_bkcr;       /* breakpoint control register*/
    volatile unsigned char      RESERVED10[0x8];        /* Reserved area    */
    /* MEMC */
    volatile unsigned long      memc_gmr;       /* Global memory register   */
    volatile unsigned short     memc_mstat;     /* MEMC status register     */
    volatile unsigned char      RESERVED11[0xa];        /* Reserved area    */
    volatile unsigned long      memc_br0;       /* base register 0          */
    volatile unsigned long      memc_or0;       /* option register 0        */
    volatile unsigned char      RESERVED12[0x8];        /* Reserved area    */
    volatile unsigned long      memc_br1;       /* base register 1          */
    volatile unsigned long      memc_or1;       /* option register 1        */
    volatile unsigned char      RESERVED13[0x8];        /* Reserved area    */
    volatile unsigned long      memc_br2;       /* base register 2          */
    volatile unsigned long      memc_or2;       /* option register 2        */
    volatile unsigned char      RESERVED14[0x8];        /* Reserved area    */
    volatile unsigned long      memc_br3;       /* base register 3          */
    volatile unsigned long      memc_or3;       /* option register 3        */
    volatile unsigned char      RESERVED15[0x8];        /* Reserved area    */
    volatile unsigned long      memc_br4;       /* base register 3          */
    volatile unsigned long      memc_or4;       /* option register 3        */
    volatile unsigned char      RESERVED16[0x8];        /* Reserved area    */
    volatile unsigned long      memc_br5;       /* base register 3          */
    volatile unsigned long      memc_or5;       /* option register 3        */
    volatile unsigned char      RESERVED17[0x8];        /* Reserved area    */
    volatile unsigned long      memc_br6;       /* base register 3          */
    volatile unsigned long      memc_or6;       /* option register 3        */
    volatile unsigned char      RESERVED18[0x8];        /* Reserved area    */
    volatile unsigned long      memc_br7;       /* base register 3          */
    volatile unsigned long      memc_or7;       /* option register 3        */
    volatile unsigned char      RESERVED9[0x28];        /* Reserved area    */
    /* TEST */
    volatile unsigned short     test_tstmra;    /* master shift a           */
    volatile unsigned short     test_tstmrb;    /* master shift b           */
    volatile unsigned short     test_tstsc;     /* shift count              */
    volatile unsigned short     test_tstrc;     /* repetition counter       */
    volatile unsigned short     test_creg;      /* control                  */
    volatile unsigned short     test_dreg;      /* destributed register     */
    volatile unsigned char      RESERVED58[0x404];      /* Reserved area    */
    /* IDMA1 */
    volatile unsigned short     idma_iccr;      /* channel configuration reg*/
    volatile unsigned char      RESERVED19[0x2];        /* Reserved area    */
    volatile unsigned short     idma1_cmr;      /* dma mode reg             */
    volatile unsigned char      RESERVED68[0x2];        /* Reserved area    */
    volatile unsigned long      idma1_sapr;     /* dma source addr ptr      */
    volatile unsigned long      idma1_dapr;     /* dma destination addr ptr */
    volatile unsigned long      idma1_bcr;      /* dma byte count reg       */
    volatile unsigned char      idma1_fcr;      /* function code reg        */
    volatile unsigned char      RESERVED20;     /* Reserved area            */
    volatile unsigned char      idma1_cmar;     /* channel mask reg         */
    volatile unsigned char      RESERVED21;     /* Reserved area            */
    volatile unsigned char      idma1_csr;      /* channel status reg       */
    volatile unsigned char      RESERVED22[0x3];        /* Reserved area    */
    /* SDMA */
    volatile unsigned char      sdma_sdsr;      /* status reg               */
    volatile unsigned char      RESERVED23;     /* Reserved area            */
    volatile unsigned short     sdma_sdcr;      /* configuration reg        */
    volatile unsigned long      sdma_sdar;      /* address reg              */
    /* IDMA2 */
    volatile unsigned char      RESERVED69[0x2];        /* Reserved area    */
    volatile unsigned short     idma2_cmr;      /* dma mode reg             */
    volatile unsigned long      idma2_sapr;     /* dma source addr ptr      */
    volatile unsigned long      idma2_dapr;     /* dma destination addr ptr */
    volatile unsigned long      idma2_bcr;      /* dma byte count reg       */
    volatile unsigned char      idma2_fcr;      /* function code reg        */
    volatile unsigned char      RESERVED24;     /* Reserved area            */
    volatile unsigned char      idma2_cmar;     /* channel mask reg         */
    volatile unsigned char      RESERVED25;     /* Reserved area            */
    volatile unsigned char      idma2_csr;      /* channel status reg       */
    volatile unsigned char      RESERVED26[0x7];        /* Reserved area    */
    /* Interrupt Controller */
    volatile unsigned long      intr_cicr;      /* CP interrupt configuration reg*/
    volatile unsigned long      intr_cipr;      /* CP interrupt pending reg */
    volatile unsigned long      intr_cimr;      /* CP interrupt mask reg    */
    volatile unsigned long      intr_cisr;      /* CP interrupt in service reg*/
    /* Parallel I/O */
    volatile unsigned short     pio_padir;      /* port A data direction reg */
    volatile unsigned short     pio_papar;      /* port A pin assignment reg */
    volatile unsigned short     pio_paodr;      /* port A open drain reg    */
    volatile unsigned short     pio_padat;      /* port A data register     */
    volatile unsigned char      RESERVED28[0x8];        /* Reserved area    */
    volatile unsigned short     pio_pcdir;      /* port C data direction reg*/
    volatile unsigned short     pio_pcpar;      /* port C pin assignment reg*/
    volatile unsigned short     pio_pcso;       /* port C special options   */
    volatile unsigned short     pio_pcdat;      /* port C data register     */
    volatile unsigned short     pio_pcint;      /* port C interrupt cntrl reg */
    volatile unsigned char      RESERVED29[0x16];       /* Reserved area    */
    /* Timer */
    volatile unsigned short     timer_tgcr;     /* timer global configuration reg */
    volatile unsigned char      RESERVED30[0xe];        /* Reserved area    */
    volatile unsigned short     timer_tmr1;     /* timer 1 mode reg         */
    volatile unsigned short     timer_tmr2;     /* timer 2 mode reg         */
    volatile unsigned short     timer_trr1;     /* timer 1 referance reg    */
    volatile unsigned short     timer_trr2;     /* timer 2 referance reg    */
    volatile unsigned short     timer_tcr1;     /* timer 1 capture reg      */
    volatile unsigned short     timer_tcr2;     /* timer 2 capture reg      */
    volatile unsigned short     timer_tcn1;     /* timer 1 counter reg      */
    volatile unsigned short     timer_tcn2;     /* timer 2 counter reg      */
    volatile unsigned short     timer_tmr3;     /* timer 3 mode reg         */
    volatile unsigned short     timer_tmr4;     /* timer 4 mode reg         */
    volatile unsigned short     timer_trr3;     /* timer 3 referance reg    */
    volatile unsigned short     timer_trr4;     /* timer 4 referance reg    */
    volatile unsigned short     timer_tcr3;     /* timer 3 capture reg      */
    volatile unsigned short     timer_tcr4;     /* timer 4 capture reg      */
    volatile unsigned short     timer_tcn3;     /* timer 3 counter reg      */
    volatile unsigned short     timer_tcn4;     /* timer 4 counter reg      */
    volatile unsigned short     timer_ter1;     /* timer 1 event reg        */
    volatile unsigned short     timer_ter2;     /* timer 2 event reg        */
    volatile unsigned short     timer_ter3;     /* timer 3 event reg        */
    volatile unsigned short     timer_ter4;     /* timer 4 event reg        */
    volatile unsigned char      RESERVED34[0x8];        /* Reserved area    */
    /* CP */
    volatile unsigned short     cp_cr;          /* command register         */
    volatile unsigned char      RESERVED35[0x2];        /* Reserved area    */
    volatile unsigned short     cp_rccr;        /* main configuration reg   */
    volatile unsigned char      RESERVED37;     /* Reserved area            */
    volatile unsigned char      cp_rmds;        /* development support status reg */
    volatile unsigned long      cp_rmdr;        /* development support control reg */
    volatile unsigned short     cp_rctr1;       /* ram break register 1     */
    volatile unsigned short     cp_rctr2;       /* ram break register 2     */
    volatile unsigned short     cp_rctr3;       /* ram break register 3     */
    volatile unsigned short     cp_rctr4;       /* ram break register 4     */
    volatile unsigned char      RESERVED59[0x2];        /* Reserved area    */
    volatile unsigned short     cp_rter;        /* RISC timers event reg    */
    volatile unsigned char      RESERVED38[0x2];        /* Reserved area    */
    volatile unsigned short     cp_rtmr;        /* RISC timers mask reg     */
    volatile unsigned char      RESERVED39[0x14];       /* Reserved area    */
    /* BRG */
    union {
        volatile unsigned long l;
        struct {
            volatile unsigned short BRGC_RESERV:14;
            volatile unsigned short rst:1;
            volatile unsigned short en:1;
            volatile unsigned short extc:2;
            volatile unsigned short atb:1;
            volatile unsigned short cd:12;
            volatile unsigned short div16:1;
        } b;
    } brgc[4];                                  /* BRG1-BRG4 configuration regs*/
    /* SCC registers */
    struct scc_regs {
        union {
            struct {
                /* Low word. */
                volatile unsigned short GSMR_RESERV2:1;
                volatile unsigned short edge:2;
                volatile unsigned short tci:1;
                volatile unsigned short tsnc:2;
                volatile unsigned short rinv:1;
                volatile unsigned short tinv:1;
                volatile unsigned short tpl:3;
                volatile unsigned short tpp:2;
                volatile unsigned short tend:1;
                volatile unsigned short tdcr:2;
                volatile unsigned short rdcr:2;
                volatile unsigned short renc:3;
                volatile unsigned short tenc:3;
                volatile unsigned short diag:2;
                volatile unsigned short enr:1;
                volatile unsigned short ent:1;
                volatile unsigned short mode:4;
                /* High word. */
                volatile unsigned short GSMR_RESERV1:14;
                volatile unsigned short pri:1;
                volatile unsigned short gde:1;
                volatile unsigned short tcrc:2;
                volatile unsigned short revd:1;
                volatile unsigned short trx:1;
                volatile unsigned short ttx:1;
                volatile unsigned short cdp:1;
                volatile unsigned short ctsp:1;
                volatile unsigned short cds:1;
                volatile unsigned short ctss:1;
                volatile unsigned short tfl:1;
                volatile unsigned short rfw:1;
                volatile unsigned short txsy:1;
                volatile unsigned short synl:2;
                volatile unsigned short rtsm:1;
                volatile unsigned short rsyn:1;
            } b;
            struct {
                volatile unsigned long low;
                volatile unsigned long high;
            } w;
        } scc_gsmr;                         /* SCC general mode reg         */
        volatile unsigned short scc_psmr;   /* protocol specific mode reg   */
        volatile unsigned char  RESERVED42[0x2]; /* Reserved area           */
        volatile unsigned short scc_todr; /* SCC transmit on demand         */
        volatile unsigned short scc_dsr;        /* SCC data sync reg        */
        volatile unsigned short scc_scce;       /* SCC event reg            */
        volatile unsigned char  RESERVED43[0x2];/* Reserved area            */
        volatile unsigned short scc_sccm;       /* SCC mask reg             */
        volatile unsigned char  RESERVED44[0x1];/* Reserved area            */
        volatile unsigned char  scc_sccs;       /* SCC status reg           */
        volatile unsigned char  RESERVED45[0x8]; /* Reserved area           */
    } scc_regs[4];
    /* SMC */
    struct smc_regs {
        volatile unsigned char  RESERVED46[0x2]; /* Reserved area           */
        volatile unsigned short smc_smcmr;       /* SMC mode reg            */
        volatile unsigned char  RESERVED60[0x2]; /* Reserved area           */
        volatile unsigned char  smc_smce;        /* SMC event reg           */
        volatile unsigned char  RESERVED47[0x3]; /* Reserved area           */
        volatile unsigned char  smc_smcm;        /* SMC mask reg            */
        volatile unsigned char  RESERVED48[0x5]; /* Reserved area           */
    } smc_regs[2];
    /* SPI */
    volatile unsigned short     spi_spmode;     /* SPI mode reg             */
    volatile unsigned char      RESERVED51[0x4];        /* Reserved area    */
    volatile unsigned char      spi_spie;       /* SPI event reg            */
    volatile unsigned char      RESERVED52[0x3];        /* Reserved area    */
    volatile unsigned char      spi_spim;       /* SPI mask reg             */
    volatile unsigned char      RESERVED53[0x2];        /* Reserved area    */
    volatile unsigned char      spi_spcom;      /* SPI command reg          */
    volatile unsigned char      RESERVED54[0x4];        /* Reserved area    */
    /* PIP */
    volatile unsigned short     pip_pipc;       /* pip configuration reg    */
    volatile unsigned char      RESERVED65[0x2];        /* Reserved area    */
    volatile unsigned short     pip_ptpr;       /* pip timing parameters reg */
    volatile unsigned long      pip_pbdir;      /* port b data direction reg */
    volatile unsigned long      pip_pbpar;      /* port b pin assignment reg */
    volatile unsigned long      pip_pbodr;      /* port b open drain reg    */
    volatile unsigned long      pip_pbdat;      /* port b data reg          */
    volatile unsigned char      RESERVED71[0x18];       /* Reserved area    */
    /* Serial Interface */
    volatile unsigned long      si_simode;      /* SI mode register         */
    volatile unsigned char      si_sigmr;       /* SI global mode register  */
    volatile unsigned char      RESERVED55;     /* Reserved area            */
    volatile unsigned char      si_sistr;       /* SI status register       */
    volatile unsigned char      si_sicmr;       /* SI command register      */
    volatile unsigned char      RESERVED56[0x4]; /* Reserved area           */
    volatile unsigned long      si_sicr;        /* SI clock routing         */
    volatile unsigned long      si_sirp;        /* SI ram pointers          */
    volatile unsigned char      RESERVED57[0xc]; /* Reserved area           */
    volatile unsigned short     si_siram[0x80]; /* SI routing ram          */
} QUICC;

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
