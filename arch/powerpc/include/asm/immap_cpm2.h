/*
 * CPM2 Internal Memory Map
 * Copyright (c) 1999 Dan Malek (dmalek@jlc.net)
 *
 * The Internal Memory Map for devices with CPM2 on them.  This
 * is the superset of all CPM2 devices (8260, 8266, 8280, 8272,
 * 8560).
 */
#ifdef __KERNEL__
#ifndef __IMMAP_CPM2__
#define __IMMAP_CPM2__

#include <linux/types.h>

/* System configuration registers.
*/
typedef	struct sys_82xx_conf {
	u32	sc_siumcr;
	u32	sc_sypcr;
	u8	res1[6];
	u16	sc_swsr;
	u8	res2[20];
	u32	sc_bcr;
	u8	sc_ppc_acr;
	u8	res3[3];
	u32	sc_ppc_alrh;
	u32	sc_ppc_alrl;
	u8	sc_lcl_acr;
	u8	res4[3];
	u32	sc_lcl_alrh;
	u32	sc_lcl_alrl;
	u32	sc_tescr1;
	u32	sc_tescr2;
	u32	sc_ltescr1;
	u32	sc_ltescr2;
	u32	sc_pdtea;
	u8	sc_pdtem;
	u8	res5[3];
	u32	sc_ldtea;
	u8	sc_ldtem;
	u8	res6[163];
} sysconf_82xx_cpm2_t;

typedef	struct sys_85xx_conf {
	u32	sc_cear;
	u16	sc_ceer;
	u16	sc_cemr;
	u8	res1[70];
	u32	sc_smaer;
	u8	res2[4];
	u32	sc_smevr;
	u32	sc_smctr;
	u32	sc_lmaer;
	u8	res3[4];
	u32	sc_lmevr;
	u32	sc_lmctr;
	u8	res4[144];
} sysconf_85xx_cpm2_t;

typedef union sys_conf {
	sysconf_82xx_cpm2_t	siu_82xx;
	sysconf_85xx_cpm2_t	siu_85xx;
} sysconf_cpm2_t;



/* Memory controller registers.
*/
typedef struct	mem_ctlr {
	u32	memc_br0;
	u32	memc_or0;
	u32	memc_br1;
	u32	memc_or1;
	u32	memc_br2;
	u32	memc_or2;
	u32	memc_br3;
	u32	memc_or3;
	u32	memc_br4;
	u32	memc_or4;
	u32	memc_br5;
	u32	memc_or5;
	u32	memc_br6;
	u32	memc_or6;
	u32	memc_br7;
	u32	memc_or7;
	u32	memc_br8;
	u32	memc_or8;
	u32	memc_br9;
	u32	memc_or9;
	u32	memc_br10;
	u32	memc_or10;
	u32	memc_br11;
	u32	memc_or11;
	u8	res1[8];
	u32	memc_mar;
	u8	res2[4];
	u32	memc_mamr;
	u32	memc_mbmr;
	u32	memc_mcmr;
	u8	res3[8];
	u16	memc_mptpr;
	u8	res4[2];
	u32	memc_mdr;
	u8	res5[4];
	u32	memc_psdmr;
	u32	memc_lsdmr;
	u8	memc_purt;
	u8	res6[3];
	u8	memc_psrt;
	u8	res7[3];
	u8	memc_lurt;
	u8	res8[3];
	u8	memc_lsrt;
	u8	res9[3];
	u32	memc_immr;
	u32	memc_pcibr0;
	u32	memc_pcibr1;
	u8	res10[16];
	u32	memc_pcimsk0;
	u32	memc_pcimsk1;
	u8	res11[52];
} memctl_cpm2_t;

/* System Integration Timers.
*/
typedef struct	sys_int_timers {
	u8	res1[32];
	u16	sit_tmcntsc;
	u8	res2[2];
	u32	sit_tmcnt;
	u8	res3[4];
	u32	sit_tmcntal;
	u8	res4[16];
	u16	sit_piscr;
	u8	res5[2];
	u32	sit_pitc;
	u32	sit_pitr;
	u8      res6[94];
	u8	res7[390];
} sit_cpm2_t;

#define PISCR_PIRQ_MASK		((u16)0xff00)
#define PISCR_PS		((u16)0x0080)
#define PISCR_PIE		((u16)0x0004)
#define PISCR_PTF		((u16)0x0002)
#define PISCR_PTE		((u16)0x0001)

/* PCI Controller.
*/
typedef struct pci_ctlr {
	u32	pci_omisr;
	u32	pci_omimr;
	u8	res1[8];
	u32	pci_ifqpr;
	u32	pci_ofqpr;
	u8	res2[8];
	u32	pci_imr0;
	u32	pci_imr1;
	u32	pci_omr0;
	u32	pci_omr1;
	u32	pci_odr;
	u8	res3[4];
	u32	pci_idr;
	u8	res4[20];
	u32	pci_imisr;
	u32	pci_imimr;
	u8	res5[24];
	u32	pci_ifhpr;
	u8	res6[4];
	u32	pci_iftpr;
	u8	res7[4];
	u32	pci_iphpr;
	u8	res8[4];
	u32	pci_iptpr;
	u8	res9[4];
	u32	pci_ofhpr;
	u8	res10[4];
	u32	pci_oftpr;
	u8	res11[4];
	u32	pci_ophpr;
	u8	res12[4];
	u32	pci_optpr;
	u8	res13[8];
	u32	pci_mucr;
	u8	res14[8];
	u32	pci_qbar;
	u8	res15[12];
	u32	pci_dmamr0;
	u32	pci_dmasr0;
	u32	pci_dmacdar0;
	u8	res16[4];
	u32	pci_dmasar0;
	u8	res17[4];
	u32	pci_dmadar0;
	u8	res18[4];
	u32	pci_dmabcr0;
	u32	pci_dmandar0;
	u8	res19[86];
	u32	pci_dmamr1;
	u32	pci_dmasr1;
	u32	pci_dmacdar1;
	u8	res20[4];
	u32	pci_dmasar1;
	u8	res21[4];
	u32	pci_dmadar1;
	u8	res22[4];
	u32	pci_dmabcr1;
	u32	pci_dmandar1;
	u8	res23[88];
	u32	pci_dmamr2;
	u32	pci_dmasr2;
	u32	pci_dmacdar2;
	u8	res24[4];
	u32	pci_dmasar2;
	u8	res25[4];
	u32	pci_dmadar2;
	u8	res26[4];
	u32	pci_dmabcr2;
	u32	pci_dmandar2;
	u8	res27[88];
	u32	pci_dmamr3;
	u32	pci_dmasr3;
	u32	pci_dmacdar3;
	u8	res28[4];
	u32	pci_dmasar3;
	u8	res29[4];
	u32	pci_dmadar3;
	u8	res30[4];
	u32	pci_dmabcr3;
	u32	pci_dmandar3;
	u8	res31[344];
	u32	pci_potar0;
	u8	res32[4];
	u32	pci_pobar0;
	u8	res33[4];
	u32	pci_pocmr0;
	u8	res34[4];
	u32	pci_potar1;
	u8	res35[4];
	u32	pci_pobar1;
	u8	res36[4];
	u32	pci_pocmr1;
	u8	res37[4];
	u32	pci_potar2;
	u8	res38[4];
	u32	pci_pobar2;
	u8	res39[4];
	u32	pci_pocmr2;
	u8	res40[50];
	u32	pci_ptcr;
	u32	pci_gpcr;
	u32	pci_gcr;
	u32	pci_esr;
	u32	pci_emr;
	u32	pci_ecr;
	u32	pci_eacr;
	u8	res41[4];
	u32	pci_edcr;
	u8	res42[4];
	u32	pci_eccr;
	u8	res43[44];
	u32	pci_pitar1;
	u8	res44[4];
	u32	pci_pibar1;
	u8	res45[4];
	u32	pci_picmr1;
	u8	res46[4];
	u32	pci_pitar0;
	u8	res47[4];
	u32	pci_pibar0;
	u8	res48[4];
	u32	pci_picmr0;
	u8	res49[4];
	u32	pci_cfg_addr;
	u32	pci_cfg_data;
	u32	pci_int_ack;
	u8	res50[756];
} pci_cpm2_t;

/* Interrupt Controller.
*/
typedef struct interrupt_controller {
	u16	ic_sicr;
	u8	res1[2];
	u32	ic_sivec;
	u32	ic_sipnrh;
	u32	ic_sipnrl;
	u32	ic_siprr;
	u32	ic_scprrh;
	u32	ic_scprrl;
	u32	ic_simrh;
	u32	ic_simrl;
	u32	ic_siexr;
	u8	res2[88];
} intctl_cpm2_t;

/* Clocks and Reset.
*/
typedef struct clk_and_reset {
	u32	car_sccr;
	u8	res1[4];
	u32	car_scmr;
	u8	res2[4];
	u32	car_rsr;
	u32	car_rmr;
	u8	res[104];
} car_cpm2_t;

/* Input/Output Port control/status registers.
 * Names consistent with processor manual, although they are different
 * from the original 8xx names.......
 */
typedef struct io_port {
	u32	iop_pdira;
	u32	iop_ppara;
	u32	iop_psora;
	u32	iop_podra;
	u32	iop_pdata;
	u8	res1[12];
	u32	iop_pdirb;
	u32	iop_pparb;
	u32	iop_psorb;
	u32	iop_podrb;
	u32	iop_pdatb;
	u8	res2[12];
	u32	iop_pdirc;
	u32	iop_pparc;
	u32	iop_psorc;
	u32	iop_podrc;
	u32	iop_pdatc;
	u8	res3[12];
	u32	iop_pdird;
	u32	iop_ppard;
	u32	iop_psord;
	u32	iop_podrd;
	u32	iop_pdatd;
	u8	res4[12];
} iop_cpm2_t;

/* Communication Processor Module Timers
*/
typedef struct cpm_timers {
	u8	cpmt_tgcr1;
	u8	res1[3];
	u8	cpmt_tgcr2;
	u8	res2[11];
	u16	cpmt_tmr1;
	u16	cpmt_tmr2;
	u16	cpmt_trr1;
	u16	cpmt_trr2;
	u16	cpmt_tcr1;
	u16	cpmt_tcr2;
	u16	cpmt_tcn1;
	u16	cpmt_tcn2;
	u16	cpmt_tmr3;
	u16	cpmt_tmr4;
	u16	cpmt_trr3;
	u16	cpmt_trr4;
	u16	cpmt_tcr3;
	u16	cpmt_tcr4;
	u16	cpmt_tcn3;
	u16	cpmt_tcn4;
	u16	cpmt_ter1;
	u16	cpmt_ter2;
	u16	cpmt_ter3;
	u16	cpmt_ter4;
	u8	res3[584];
} cpmtimer_cpm2_t;

/* DMA control/status registers.
*/
typedef struct sdma_csr {
	u8	res0[24];
	u8	sdma_sdsr;
	u8	res1[3];
	u8	sdma_sdmr;
	u8	res2[3];
	u8	sdma_idsr1;
	u8	res3[3];
	u8	sdma_idmr1;
	u8	res4[3];
	u8	sdma_idsr2;
	u8	res5[3];
	u8	sdma_idmr2;
	u8	res6[3];
	u8	sdma_idsr3;
	u8	res7[3];
	u8	sdma_idmr3;
	u8	res8[3];
	u8	sdma_idsr4;
	u8	res9[3];
	u8	sdma_idmr4;
	u8	res10[707];
} sdma_cpm2_t;

/* Fast controllers
*/
typedef struct fcc {
	u32	fcc_gfmr;
	u32	fcc_fpsmr;
	u16	fcc_ftodr;
	u8	res1[2];
	u16	fcc_fdsr;
	u8	res2[2];
	u16	fcc_fcce;
	u8	res3[2];
	u16	fcc_fccm;
	u8	res4[2];
	u8	fcc_fccs;
	u8	res5[3];
	u8	fcc_ftirr_phy[4];
} fcc_t;

/* Fast controllers continued
 */
typedef struct fcc_c {
	u32	fcc_firper;
	u32	fcc_firer;
	u32	fcc_firsr_hi;
	u32	fcc_firsr_lo;
	u8	fcc_gfemr;
	u8	res1[15];
} fcc_c_t;

/* TC Layer
 */
typedef struct tclayer {
	u16	tc_tcmode;
	u16	tc_cdsmr;
	u16	tc_tcer;
	u16	tc_rcc;
	u16	tc_tcmr;
	u16	tc_fcc;
	u16	tc_ccc;
	u16	tc_icc;
	u16	tc_tcc;
	u16	tc_ecc;
	u8	res1[12];
} tclayer_t;


/* I2C
*/
typedef struct i2c {
	u8	i2c_i2mod;
	u8	res1[3];
	u8	i2c_i2add;
	u8	res2[3];
	u8	i2c_i2brg;
	u8	res3[3];
	u8	i2c_i2com;
	u8	res4[3];
	u8	i2c_i2cer;
	u8	res5[3];
	u8	i2c_i2cmr;
	u8	res6[331];
} i2c_cpm2_t;

typedef struct scc {		/* Serial communication channels */
	u32	scc_gsmrl;
	u32	scc_gsmrh;
	u16	scc_psmr;
	u8	res1[2];
	u16	scc_todr;
	u16	scc_dsr;
	u16	scc_scce;
	u8	res2[2];
	u16	scc_sccm;
	u8	res3;
	u8	scc_sccs;
	u8	res4[8];
} scc_t;

typedef struct smc {		/* Serial management channels */
	u8	res1[2];
	u16	smc_smcmr;
	u8	res2[2];
	u8	smc_smce;
	u8	res3[3];
	u8	smc_smcm;
	u8	res4[5];
} smc_t;

/* Serial Peripheral Interface.
*/
typedef struct spi_ctrl {
	u16	spi_spmode;
	u8	res1[4];
	u8	spi_spie;
	u8	res2[3];
	u8	spi_spim;
	u8	res3[2];
	u8	spi_spcom;
	u8	res4[82];
} spictl_cpm2_t;

/* CPM Mux.
*/
typedef struct cpmux {
	u8	cmx_si1cr;
	u8	res1;
	u8	cmx_si2cr;
	u8	res2;
	u32	cmx_fcr;
	u32	cmx_scr;
	u8	cmx_smr;
	u8	res3;
	u16	cmx_uar;
	u8	res4[16];
} cpmux_t;

/* SIRAM control
*/
typedef struct siram {
	u16	si_amr;
	u16	si_bmr;
	u16	si_cmr;
	u16	si_dmr;
	u8	si_gmr;
	u8	res1;
	u8	si_cmdr;
	u8	res2;
	u8	si_str;
	u8	res3;
	u16	si_rsr;
} siramctl_t;

typedef struct mcc {
	u16	mcc_mcce;
	u8	res1[2];
	u16	mcc_mccm;
	u8	res2[2];
	u8	mcc_mccf;
	u8	res3[7];
} mcc_t;

typedef struct comm_proc {
	u32	cp_cpcr;
	u32	cp_rccr;
	u8	res1[14];
	u16	cp_rter;
	u8	res2[2];
	u16	cp_rtmr;
	u16	cp_rtscr;
	u8	res3[2];
	u32	cp_rtsr;
	u8	res4[12];
} cpm_cpm2_t;

/* USB Controller.
*/
typedef struct cpm_usb_ctlr {
	u8	usb_usmod;
	u8	usb_usadr;
	u8	usb_uscom;
	u8	res1[1];
	__be16  usb_usep[4];
	u8	res2[4];
	__be16  usb_usber;
	u8	res3[2];
	__be16  usb_usbmr;
	u8	usb_usbs;
	u8	res4[7];
} usb_cpm2_t;

/* ...and the whole thing wrapped up....
*/

typedef struct immap {
	/* Some references are into the unique and known dpram spaces,
	 * others are from the generic base.
	 */
#define im_dprambase	im_dpram1
	u8		im_dpram1[16*1024];
	u8		res1[16*1024];
	u8		im_dpram2[4*1024];
	u8		res2[8*1024];
	u8		im_dpram3[4*1024];
	u8		res3[16*1024];

	sysconf_cpm2_t	im_siu_conf;	/* SIU Configuration */
	memctl_cpm2_t	im_memctl;	/* Memory Controller */
	sit_cpm2_t	im_sit;		/* System Integration Timers */
	pci_cpm2_t	im_pci;		/* PCI Controller */
	intctl_cpm2_t	im_intctl;	/* Interrupt Controller */
	car_cpm2_t	im_clkrst;	/* Clocks and reset */
	iop_cpm2_t	im_ioport;	/* IO Port control/status */
	cpmtimer_cpm2_t	im_cpmtimer;	/* CPM timers */
	sdma_cpm2_t	im_sdma;	/* SDMA control/status */

	fcc_t		im_fcc[3];	/* Three FCCs */
	u8		res4z[32];
	fcc_c_t		im_fcc_c[3];	/* Continued FCCs */

	u8		res4[32];

	tclayer_t	im_tclayer[8];	/* Eight TCLayers */
	u16		tc_tcgsr;
	u16		tc_tcger;

	/* First set of baud rate generators.
	*/
	u8		res[236];
	u32		im_brgc5;
	u32		im_brgc6;
	u32		im_brgc7;
	u32		im_brgc8;

	u8		res5[608];

	i2c_cpm2_t	im_i2c;		/* I2C control/status */
	cpm_cpm2_t	im_cpm;		/* Communication processor */

	/* Second set of baud rate generators.
	*/
	u32		im_brgc1;
	u32		im_brgc2;
	u32		im_brgc3;
	u32		im_brgc4;

	scc_t		im_scc[4];	/* Four SCCs */
	smc_t		im_smc[2];	/* Couple of SMCs */
	spictl_cpm2_t	im_spi;		/* A SPI */
	cpmux_t		im_cpmux;	/* CPM clock route mux */
	siramctl_t	im_siramctl1;	/* First SI RAM Control */
	mcc_t		im_mcc1;	/* First MCC */
	siramctl_t	im_siramctl2;	/* Second SI RAM Control */
	mcc_t		im_mcc2;	/* Second MCC */
	usb_cpm2_t	im_usb;		/* USB Controller */

	u8		res6[1153];

	u16		im_si1txram[256];
	u8		res7[512];
	u16		im_si1rxram[256];
	u8		res8[512];
	u16		im_si2txram[256];
	u8		res9[512];
	u16		im_si2rxram[256];
	u8		res10[512];
	u8		res11[4096];
} cpm2_map_t;

extern cpm2_map_t __iomem *cpm2_immr;

#endif /* __IMMAP_CPM2__ */
#endif /* __KERNEL__ */
