/*
 * MPC8xx Internal Memory Map
 * Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * The I/O on the MPC860 is comprised of blocks of special registers
 * and the dual port ram for the Communication Processor Module.
 * Within this space are functional units such as the SIU, memory
 * controller, system timers, and other control functions.  It is
 * a combination that I found difficult to separate into logical
 * functional files.....but anyone else is welcome to try.  -- Dan
 */
#ifdef __KERNEL__
#ifndef __IMMAP_8XX__
#define __IMMAP_8XX__

/* System configuration registers.
*/
typedef	struct sys_conf {
	uint	sc_siumcr;
	uint	sc_sypcr;
	uint	sc_swt;
	char	res1[2];
	ushort	sc_swsr;
	uint	sc_sipend;
	uint	sc_simask;
	uint	sc_siel;
	uint	sc_sivec;
	uint	sc_tesr;
	char	res2[0xc];
	uint	sc_sdcr;
	char	res3[0x4c];
} sysconf8xx_t;

/* PCMCIA configuration registers.
*/
typedef struct pcmcia_conf {
	uint	pcmc_pbr0;
	uint	pcmc_por0;
	uint	pcmc_pbr1;
	uint	pcmc_por1;
	uint	pcmc_pbr2;
	uint	pcmc_por2;
	uint	pcmc_pbr3;
	uint	pcmc_por3;
	uint	pcmc_pbr4;
	uint	pcmc_por4;
	uint	pcmc_pbr5;
	uint	pcmc_por5;
	uint	pcmc_pbr6;
	uint	pcmc_por6;
	uint	pcmc_pbr7;
	uint	pcmc_por7;
	char	res1[0x20];
	uint	pcmc_pgcra;
	uint	pcmc_pgcrb;
	uint	pcmc_pscr;
	char	res2[4];
	uint	pcmc_pipr;
	char	res3[4];
	uint	pcmc_per;
	char	res4[4];
} pcmconf8xx_t;

/* Memory controller registers.
*/
typedef struct	mem_ctlr {
	uint	memc_br0;
	uint	memc_or0;
	uint	memc_br1;
	uint	memc_or1;
	uint	memc_br2;
	uint	memc_or2;
	uint	memc_br3;
	uint	memc_or3;
	uint	memc_br4;
	uint	memc_or4;
	uint	memc_br5;
	uint	memc_or5;
	uint	memc_br6;
	uint	memc_or6;
	uint	memc_br7;
	uint	memc_or7;
	char	res1[0x24];
	uint	memc_mar;
	uint	memc_mcr;
	char	res2[4];
	uint	memc_mamr;
	uint	memc_mbmr;
	ushort	memc_mstat;
	ushort	memc_mptpr;
	uint	memc_mdr;
	char	res3[0x80];
} memctl8xx_t;

/*-----------------------------------------------------------------------
 * BR - Memory Controller: Base Register					16-9
 */
#define BR_BA_MSK	0xffff8000	/* Base Address Mask			*/
#define BR_AT_MSK	0x00007000	/* Address Type Mask			*/
#define BR_PS_MSK	0x00000c00	/* Port Size Mask			*/
#define BR_PS_32	0x00000000	/* 32 bit port size			*/
#define BR_PS_16	0x00000800	/* 16 bit port size			*/
#define BR_PS_8		0x00000400	/*  8 bit port size			*/
#define BR_PARE		0x00000200	/* Parity Enable			*/
#define BR_WP		0x00000100	/* Write Protect			*/
#define BR_MS_MSK	0x000000c0	/* Machine Select Mask			*/
#define BR_MS_GPCM	0x00000000	/* G.P.C.M. Machine Select		*/
#define BR_MS_UPMA	0x00000080	/* U.P.M.A Machine Select		*/
#define BR_MS_UPMB	0x000000c0	/* U.P.M.B Machine Select		*/
#define BR_V		0x00000001	/* Bank Valid				*/

/*-----------------------------------------------------------------------
 * OR - Memory Controller: Option Register				16-11
 */
#define OR_AM_MSK	0xffff8000	/* Address Mask Mask			*/
#define OR_ATM_MSK	0x00007000	/* Address Type Mask Mask		*/
#define OR_CSNT_SAM	0x00000800	/* Chip Select Negation Time/ Start	*/
					/* Address Multiplex			*/
#define OR_ACS_MSK	0x00000600	/* Address to Chip Select Setup mask	*/
#define OR_ACS_DIV1	0x00000000	/* CS is output at the same time	*/
#define OR_ACS_DIV4	0x00000400	/* CS is output 1/4 a clock later	*/
#define OR_ACS_DIV2	0x00000600	/* CS is output 1/2 a clock later	*/
#define OR_G5LA		0x00000400	/* Output #GPL5 on #GPL_A5		*/
#define OR_G5LS		0x00000200	/* Drive #GPL high on falling edge of...*/
#define OR_BI		0x00000100	/* Burst inhibit			*/
#define OR_SCY_MSK	0x000000f0	/* Cycle Length in Clocks		*/
#define OR_SCY_0_CLK	0x00000000	/* 0 clock cycles wait states		*/
#define OR_SCY_1_CLK	0x00000010	/* 1 clock cycles wait states		*/
#define OR_SCY_2_CLK	0x00000020	/* 2 clock cycles wait states		*/
#define OR_SCY_3_CLK	0x00000030	/* 3 clock cycles wait states		*/
#define OR_SCY_4_CLK	0x00000040	/* 4 clock cycles wait states		*/
#define OR_SCY_5_CLK	0x00000050	/* 5 clock cycles wait states		*/
#define OR_SCY_6_CLK	0x00000060	/* 6 clock cycles wait states		*/
#define OR_SCY_7_CLK	0x00000070	/* 7 clock cycles wait states		*/
#define OR_SCY_8_CLK	0x00000080	/* 8 clock cycles wait states		*/
#define OR_SCY_9_CLK	0x00000090	/* 9 clock cycles wait states		*/
#define OR_SCY_10_CLK	0x000000a0	/* 10 clock cycles wait states		*/
#define OR_SCY_11_CLK	0x000000b0	/* 11 clock cycles wait states		*/
#define OR_SCY_12_CLK	0x000000c0	/* 12 clock cycles wait states		*/
#define OR_SCY_13_CLK	0x000000d0	/* 13 clock cycles wait states		*/
#define OR_SCY_14_CLK	0x000000e0	/* 14 clock cycles wait states		*/
#define OR_SCY_15_CLK	0x000000f0	/* 15 clock cycles wait states		*/
#define OR_SETA		0x00000008	/* External Transfer Acknowledge	*/
#define OR_TRLX		0x00000004	/* Timing Relaxed			*/
#define OR_EHTR		0x00000002	/* Extended Hold Time on Read		*/

/* System Integration Timers.
*/
typedef struct	sys_int_timers {
	ushort	sit_tbscr;
	char	res0[0x02];
	uint	sit_tbreff0;
	uint	sit_tbreff1;
	char	res1[0x14];
	ushort	sit_rtcsc;
	char	res2[0x02];
	uint	sit_rtc;
	uint	sit_rtsec;
	uint	sit_rtcal;
	char	res3[0x10];
	ushort	sit_piscr;
	char	res4[2];
	uint	sit_pitc;
	uint	sit_pitr;
	char	res5[0x34];
} sit8xx_t;

#define TBSCR_TBIRQ_MASK	((ushort)0xff00)
#define TBSCR_REFA		((ushort)0x0080)
#define TBSCR_REFB		((ushort)0x0040)
#define TBSCR_REFAE		((ushort)0x0008)
#define TBSCR_REFBE		((ushort)0x0004)
#define TBSCR_TBF		((ushort)0x0002)
#define TBSCR_TBE		((ushort)0x0001)

#define RTCSC_RTCIRQ_MASK	((ushort)0xff00)
#define RTCSC_SEC		((ushort)0x0080)
#define RTCSC_ALR		((ushort)0x0040)
#define RTCSC_38K		((ushort)0x0010)
#define RTCSC_SIE		((ushort)0x0008)
#define RTCSC_ALE		((ushort)0x0004)
#define RTCSC_RTF		((ushort)0x0002)
#define RTCSC_RTE		((ushort)0x0001)

#define PISCR_PIRQ_MASK		((ushort)0xff00)
#define PISCR_PS		((ushort)0x0080)
#define PISCR_PIE		((ushort)0x0004)
#define PISCR_PTF		((ushort)0x0002)
#define PISCR_PTE		((ushort)0x0001)

/* Clocks and Reset.
*/
typedef struct clk_and_reset {
	uint	car_sccr;
	uint	car_plprcr;
	uint	car_rsr;
	char	res[0x74];        /* Reserved area                  */
} car8xx_t;

/* System Integration Timers keys.
*/
typedef struct sitk {
	uint	sitk_tbscrk;
	uint	sitk_tbreff0k;
	uint	sitk_tbreff1k;
	uint	sitk_tbk;
	char	res1[0x10];
	uint	sitk_rtcsck;
	uint	sitk_rtck;
	uint	sitk_rtseck;
	uint	sitk_rtcalk;
	char	res2[0x10];
	uint	sitk_piscrk;
	uint	sitk_pitck;
	char	res3[0x38];
} sitk8xx_t;

/* Clocks and reset keys.
*/
typedef struct cark {
	uint	cark_sccrk;
	uint	cark_plprcrk;
	uint	cark_rsrk;
	char	res[0x474];
} cark8xx_t;

/* The key to unlock registers maintained by keep-alive power.
*/
#define KAPWR_KEY	((unsigned int)0x55ccaa33)

/* Video interface.  MPC823 Only.
*/
typedef struct vid823 {
	ushort	vid_vccr;
	ushort	res1;
	u_char	vid_vsr;
	u_char	res2;
	u_char	vid_vcmr;
	u_char	res3;
	uint	vid_vbcb;
	uint	res4;
	uint	vid_vfcr0;
	uint	vid_vfaa0;
	uint	vid_vfba0;
	uint	vid_vfcr1;
	uint	vid_vfaa1;
	uint	vid_vfba1;
	u_char	res5[0x18];
} vid823_t;

/* LCD interface.  823 Only.
*/
typedef struct lcd {
	uint	lcd_lccr;
	uint	lcd_lchcr;
	uint	lcd_lcvcr;
	char	res1[4];
	uint	lcd_lcfaa;
	uint	lcd_lcfba;
	char	lcd_lcsr;
	char	res2[0x7];
} lcd823_t;

/* I2C
*/
typedef struct i2c {
	u_char	i2c_i2mod;
	char	res1[3];
	u_char	i2c_i2add;
	char	res2[3];
	u_char	i2c_i2brg;
	char	res3[3];
	u_char	i2c_i2com;
	char	res4[3];
	u_char	i2c_i2cer;
	char	res5[3];
	u_char	i2c_i2cmr;
	char	res6[0x8b];
} i2c8xx_t;

/* DMA control/status registers.
*/
typedef struct sdma_csr {
	char	res1[4];
	uint	sdma_sdar;
	u_char	sdma_sdsr;
	char	res3[3];
	u_char	sdma_sdmr;
	char	res4[3];
	u_char	sdma_idsr1;
	char	res5[3];
	u_char	sdma_idmr1;
	char	res6[3];
	u_char	sdma_idsr2;
	char	res7[3];
	u_char	sdma_idmr2;
	char	res8[0x13];
} sdma8xx_t;

/* Communication Processor Module Interrupt Controller.
*/
typedef struct cpm_ic {
	ushort	cpic_civr;
	char	res[0xe];
	uint	cpic_cicr;
	uint	cpic_cipr;
	uint	cpic_cimr;
	uint	cpic_cisr;
} cpic8xx_t;

/* Input/Output Port control/status registers.
*/
typedef struct io_port {
	ushort	iop_padir;
	ushort	iop_papar;
	ushort	iop_paodr;
	ushort	iop_padat;
	char	res1[8];
	ushort	iop_pcdir;
	ushort	iop_pcpar;
	ushort	iop_pcso;
	ushort	iop_pcdat;
	ushort	iop_pcint;
	char	res2[6];
	ushort	iop_pddir;
	ushort	iop_pdpar;
	char	res3[2];
	ushort	iop_pddat;
	uint	utmode;
	char	res4[4];
} iop8xx_t;

/* Communication Processor Module Timers
*/
typedef struct cpm_timers {
	ushort	cpmt_tgcr;
	char	res1[0xe];
	ushort	cpmt_tmr1;
	ushort	cpmt_tmr2;
	ushort	cpmt_trr1;
	ushort	cpmt_trr2;
	ushort	cpmt_tcr1;
	ushort	cpmt_tcr2;
	ushort	cpmt_tcn1;
	ushort	cpmt_tcn2;
	ushort	cpmt_tmr3;
	ushort	cpmt_tmr4;
	ushort	cpmt_trr3;
	ushort	cpmt_trr4;
	ushort	cpmt_tcr3;
	ushort	cpmt_tcr4;
	ushort	cpmt_tcn3;
	ushort	cpmt_tcn4;
	ushort	cpmt_ter1;
	ushort	cpmt_ter2;
	ushort	cpmt_ter3;
	ushort	cpmt_ter4;
	char	res2[8];
} cpmtimer8xx_t;

/* Finally, the Communication Processor stuff.....
*/
typedef struct scc {		/* Serial communication channels */
	uint	scc_gsmrl;
	uint	scc_gsmrh;
	ushort	scc_psmr;
	char	res1[2];
	ushort	scc_todr;
	ushort	scc_dsr;
	ushort	scc_scce;
	char	res2[2];
	ushort	scc_sccm;
	char	res3;
	u_char	scc_sccs;
	char	res4[8];
} scc_t;

typedef struct smc {		/* Serial management channels */
	char	res1[2];
	ushort	smc_smcmr;
	char	res2[2];
	u_char	smc_smce;
	char	res3[3];
	u_char	smc_smcm;
	char	res4[5];
} smc_t;

/* MPC860T Fast Ethernet Controller.  It isn't part of the CPM, but
 * it fits within the address space.
 */

typedef struct fec {
	uint	fec_addr_low;		/* lower 32 bits of station address	*/
	ushort	fec_addr_high;		/* upper 16 bits of station address	*/
	ushort	res1;			/* reserved				*/
	uint	fec_hash_table_high;	/* upper 32-bits of hash table		*/
	uint	fec_hash_table_low;	/* lower 32-bits of hash table		*/
	uint	fec_r_des_start;	/* beginning of Rx descriptor ring	*/
	uint	fec_x_des_start;	/* beginning of Tx descriptor ring	*/
	uint	fec_r_buff_size;	/* Rx buffer size			*/
	uint	res2[9];		/* reserved				*/
	uint	fec_ecntrl;		/* ethernet control register		*/
	uint	fec_ievent;		/* interrupt event register		*/
	uint	fec_imask;		/* interrupt mask register		*/
	uint	fec_ivec;		/* interrupt level and vector status	*/
	uint	fec_r_des_active;	/* Rx ring updated flag			*/
	uint	fec_x_des_active;	/* Tx ring updated flag			*/
	uint	res3[10];		/* reserved				*/
	uint	fec_mii_data;		/* MII data register			*/
	uint	fec_mii_speed;		/* MII speed control register		*/
	uint	res4[17];		/* reserved				*/
	uint	fec_r_bound;		/* end of RAM (read-only)		*/
	uint	fec_r_fstart;		/* Rx FIFO start address		*/
	uint	res5[6];		/* reserved				*/
	uint	fec_x_fstart;		/* Tx FIFO start address		*/
	uint	res6[17];		/* reserved				*/
	uint	fec_fun_code;		/* fec SDMA function code		*/
	uint	res7[3];		/* reserved				*/
	uint	fec_r_cntrl;		/* Rx control register			*/
	uint	fec_r_hash;		/* Rx hash register			*/
	uint	res8[14];		/* reserved				*/
	uint	fec_x_cntrl;		/* Tx control register			*/
	uint	res9[0x1e];		/* reserved				*/
} fec_t;

/* The FEC and LCD color map share the same address space....
 * I guess we will never see an 823T :-).
 */
union fec_lcd {
	fec_t	fl_un_fec;
	u_char	fl_un_cmap[0x200];
};

typedef struct comm_proc {
	/* General control and status registers.
	*/
	ushort	cp_cpcr;
	u_char	res1[2];
	ushort	cp_rccr;
	u_char	res2;
	u_char	cp_rmds;
	u_char	res3[4];
	ushort	cp_cpmcr1;
	ushort	cp_cpmcr2;
	ushort	cp_cpmcr3;
	ushort	cp_cpmcr4;
	u_char	res4[2];
	ushort	cp_rter;
	u_char	res5[2];
	ushort	cp_rtmr;
	u_char	res6[0x14];

	/* Baud rate generators.
	*/
	uint	cp_brgc1;
	uint	cp_brgc2;
	uint	cp_brgc3;
	uint	cp_brgc4;

	/* Serial Communication Channels.
	*/
	scc_t	cp_scc[4];

	/* Serial Management Channels.
	*/
	smc_t	cp_smc[2];

	/* Serial Peripheral Interface.
	*/
	ushort	cp_spmode;
	u_char	res7[4];
	u_char	cp_spie;
	u_char	res8[3];
	u_char	cp_spim;
	u_char	res9[2];
	u_char	cp_spcom;
	u_char	res10[2];

	/* Parallel Interface Port.
	*/
	u_char	res11[2];
	ushort	cp_pipc;
	u_char	res12[2];
	ushort	cp_ptpr;
	uint	cp_pbdir;
	uint	cp_pbpar;
	u_char	res13[2];
	ushort	cp_pbodr;
	uint	cp_pbdat;

	/* Port E - MPC87x/88x only.
	 */
	uint	cp_pedir;
	uint	cp_pepar;
	uint	cp_peso;
	uint	cp_peodr;
	uint	cp_pedat;

	/* Communications Processor Timing Register -
	   Contains RMII Timing for the FECs on MPC87x/88x only.
	*/
	uint	cp_cptr;

	/* Serial Interface and Time Slot Assignment.
	*/
	uint	cp_simode;
	u_char	cp_sigmr;
	u_char	res15;
	u_char	cp_sistr;
	u_char	cp_sicmr;
	u_char	res16[4];
	uint	cp_sicr;
	uint	cp_sirp;
	u_char	res17[0xc];

	/* 256 bytes of MPC823 video controller RAM array.
	*/
	u_char	cp_vcram[0x100];
	u_char	cp_siram[0x200];

	/* The fast ethernet controller is not really part of the CPM,
	 * but it resides in the address space.
	 * The LCD color map is also here.
	 */
	union	fec_lcd	fl_un;
#define cp_fec		fl_un.fl_un_fec
#define lcd_cmap	fl_un.fl_un_cmap
	char	res18[0xE00];

	/* The DUET family has a second FEC here */
	fec_t	cp_fec2;
#define cp_fec1	cp_fec	/* consistency macro */

	/* Dual Ported RAM follows.
	 * There are many different formats for this memory area
	 * depending upon the devices used and options chosen.
	 * Some processors don't have all of it populated.
	 */
	u_char	cp_dpmem[0x1C00];	/* BD / Data / ucode */
	u_char	cp_dparam[0x400];	/* Parameter RAM */
} cpm8xx_t;

/* Internal memory map.
*/
typedef struct immap {
	sysconf8xx_t	im_siu_conf;	/* SIU Configuration */
	pcmconf8xx_t	im_pcmcia;	/* PCMCIA Configuration */
	memctl8xx_t	im_memctl;	/* Memory Controller */
	sit8xx_t	im_sit;		/* System integration timers */
	car8xx_t	im_clkrst;	/* Clocks and reset */
	sitk8xx_t	im_sitk;	/* Sys int timer keys */
	cark8xx_t	im_clkrstk;	/* Clocks and reset keys */
	vid823_t	im_vid;		/* Video (823 only) */
	lcd823_t	im_lcd;		/* LCD (823 only) */
	i2c8xx_t	im_i2c;		/* I2C control/status */
	sdma8xx_t	im_sdma;	/* SDMA control/status */
	cpic8xx_t	im_cpic;	/* CPM Interrupt Controller */
	iop8xx_t	im_ioport;	/* IO Port control/status */
	cpmtimer8xx_t	im_cpmtimer;	/* CPM timers */
	cpm8xx_t	im_cpm;		/* Communication processor */
} immap_t;

#endif /* __IMMAP_8XX__ */
#endif /* __KERNEL__ */
