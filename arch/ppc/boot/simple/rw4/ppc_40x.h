/*----------------------------------------------------------------------------+
|       This source code has been made available to you by IBM on an AS-IS
|       basis.  Anyone receiving this source is licensed under IBM
|       copyrights to use it in any way he or she deems fit, including
|       copying it, modifying it, compiling it, and redistributing it either
|       with or without modifications.  No license under IBM patents or
|       patent applications is to be implied by the copyright license.
|
|       Any user of this software should understand that IBM cannot provide
|       technical support for this software and will not be responsible for
|       any consequences resulting from the use of this software.
|
|       Any person who transfers this source code or any derivative work
|       must include the IBM copyright notice, this paragraph, and the
|       preceding two paragraphs in the transferred software.
|
|       COPYRIGHT   I B M   CORPORATION 1997
|       LICENSED MATERIAL  -  PROGRAM PROPERTY OF I B M
+----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------+
| Author:    Tony J. Cerreto
| Component: Assembler include file.
| File:      ppc_40x.h
| Purpose:   Include file containing PPC DCR defines.
|
| Changes:
| Date       Author  Comment
| ---------  ------  --------------------------------------------------------
| 01-Mar-00  tjc     Created
+----------------------------------------------------------------------------*/
/* added by linguohui*/
#define MW
/*----------------------------------------------------------------------------+
| PPC Special purpose registers Numbers
+----------------------------------------------------------------------------*/
#define ccr0            0x3b3               /* core configuration reg        */
#define ctr             0x009               /* count register                */
#define ctrreg          0x009               /* count register                */
#define dbcr0           0x3f2               /* debug control register 0      */
#define dbcr1           0x3bd               /* debug control register 1      */
#define dbsr            0x3f0               /* debug status register         */
#define dccr            0x3fa               /* data cache control reg.       */
#define dcwr            0x3ba               /* data cache write-thru reg     */
#define dear            0x3d5               /* data exception address reg    */
#define esr             0x3d4               /* exception syndrome register   */
#define evpr            0x3d6               /* exception vector prefix reg   */
#define iccr            0x3fb               /* instruction cache cntrl re    */
#define icdbdr          0x3d3               /* instr cache dbug data reg     */
#define lrreg           0x008               /* link register                 */
#define pid             0x3b1               /* process id reg                */
#define pit             0x3db               /* programmable interval time    */
#define pvr             0x11f               /* processor version register    */
#define sgr             0x3b9               /* storage guarded reg           */
#define sler            0x3bb               /* storage little endian reg     */
#define sprg0           0x110               /* special general purpose 0     */
#define sprg1           0x111               /* special general purpose 1     */
#define sprg2           0x112               /* special general purpose 2     */
#define sprg3           0x113               /* special general purpose 3     */
#define sprg4           0x114               /* special general purpose 4     */
#define sprg5           0x115               /* special general purpose 5     */
#define sprg6           0x116               /* special general purpose 6     */
#define sprg7           0x117               /* special general purpose 7     */
#define srr0            0x01a               /* save/restore register 0       */
#define srr1            0x01b               /* save/restore register 1       */
#define srr2            0x3de               /* save/restore register 2       */
#define srr3            0x3df               /* save/restore register 3       */
#define tbhi            0x11D
#define tblo            0x11C
#define tcr             0x3da               /* timer control register        */
#define tsr             0x3d8               /* timer status register         */
#define xerreg          0x001               /* fixed point exception         */
#define xer             0x001               /* fixed point exception         */
#define zpr             0x3b0               /* zone protection reg           */

/*----------------------------------------------------------------------------+
| Decompression Controller
+----------------------------------------------------------------------------*/
#define kiar            0x014               /* Decompression cntl addr reg   */
#define kidr            0x015               /* Decompression cntl data reg   */
#define kitor0          0x00                /* index table origin Reg 0      */
#define kitor1          0x01                /* index table origin Reg 1      */
#define kitor2          0x02                /* index table origin Reg 2      */
#define kitor3          0x03                /* index table origin Reg 3      */
#define kaddr0          0x04                /* addr decode Definition Reg 0  */
#define kaddr1          0x05                /* addr decode Definition Reg 1  */
#define kconf           0x40                /* Decompression cntl config reg */
#define kid             0x41                /* Decompression cntl id reg     */
#define kver            0x42                /* Decompression cntl ver number */
#define kpear           0x50                /* bus error addr reg (PLB)      */
#define kbear           0x51                /* bus error addr reg (DCP-EBC)  */
#define kesr0           0x52                /* bus error status reg 0        */

/*----------------------------------------------------------------------------+
| Romeo Specific Device Control Register Numbers.
+----------------------------------------------------------------------------*/
#ifndef VESTA
#define cdbcr           0x3d7                   /* cache debug cntrl reg     */

#define a_latcnt        0x1a9                   /* PLB Latency count         */
#define a_tgval         0x1ac                   /* tone generation value     */
#define a_plb_pr        0x1bf                   /* PLB priority              */

#define cic_sel1        0x031                   /* select register 1         */
#define cic_sel2        0x032                   /* select register 2         */

#define clkgcrst        0x122                   /* chip reset register */

#define cp_cpmsr        0x100                   /*rstatus register           */
#define cp_cpmer        0x101                   /* enable register           */

#define dcp_kiar        0x190                   /* indirect address register */
#define dcp_kidr        0x191                   /* indirect data register    */

#define hsmc_mcgr       0x1c0                   /* HSMC global register      */
#define hsmc_mcbesr     0x1c1                   /* bus error status register */
#define hsmc_mcbear     0x1c2                   /* bus error address register*/
#define hsmc_mcbr0      0x1c4                   /* SDRAM sub-ctrl bank reg 0 */
#define hsmc_mccr0      0x1c5                   /* SDRAM sub-ctrl ctrl reg 0 */
#define hsmc_mcbr1      0x1c7                   /* SDRAM sub-ctrl bank reg 1 */
#define hsmc_mccr1      0x1c8                   /* SDRAM sub-ctrl ctrl reg 1 */
#define hsmc_sysr       0x1d1                   /* system register           */
#define hsmc_data       0x1d2                   /* data register             */
#define hsmc_mccrr      0x1d3                   /* refresh register          */

#define ocm_pbar        0x1E0                   /* base address register     */

#define plb0_pacr0      0x057                   /* PLB arbiter control reg   */
#define plb1_pacr1      0x067                   /* PLB arbiter control reg   */

#define v_displb        0x157                   /* set left border of display*/
#define v_disptb        0x158                   /* top border of display     */
#define v_osd_la        0x159                   /* first link address for OSD*/
#define v_ptsdlta       0x15E                   /* PTS delta register        */
#define v_v0base        0x16C                   /* base mem add for VBI-0    */
#define v_v1base        0x16D                   /* base mem add for VBI-1    */
#define v_osbase        0x16E                   /* base mem add for OSD data */
#endif

/*----------------------------------------------------------------------------+
| Vesta Device Control Register Numbers.
+----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------+
| Cross bar switch.
+----------------------------------------------------------------------------*/
#define cbs0_cr         0x010               /* CBS configuration register    */

/*----------------------------------------------------------------------------+
| DCR external master (DCRX).
+----------------------------------------------------------------------------*/
#define dcrx0_icr       0x020               /* internal control register     */
#define dcrx0_isr       0x021               /* internal status register      */
#define dcrx0_ecr       0x022               /* external control register     */
#define dcrx0_esr       0x023               /* external status register      */
#define dcrx0_tar       0x024               /* target address register       */
#define dcrx0_tdr       0x025               /* target data register          */
#define dcrx0_igr       0x026               /* interrupt generation register */
#define dcrx0_bcr       0x027               /* buffer control register       */

/*----------------------------------------------------------------------------+
| Chip interconnect configuration.
+----------------------------------------------------------------------------*/
#define cic0_cr         0x030               /* CIC control register          */
#define cic0_vcr        0x033               /* video macro control reg       */
#define cic0_sel3       0x035               /* select register 3             */

/*----------------------------------------------------------------------------+
| Chip interconnect configuration.
+----------------------------------------------------------------------------*/
#define sgpo0_sgpO      0x036               /* simplified GPIO output        */
#define sgpo0_gpod      0x037               /* simplified GPIO open drain    */
#define sgpo0_gptc      0x038               /* simplified GPIO tristate cntl */
#define sgpo0_gpi       0x039               /* simplified GPIO input         */

/*----------------------------------------------------------------------------+
| Universal interrupt controller.
+----------------------------------------------------------------------------*/
#define uic0_sr         0x040               /* status register               */
#define uic0_srs        0x041               /* status register set           */
#define uic0_er         0x042               /* enable register               */
#define uic0_cr         0x043               /* critical register             */
#define uic0_pr         0x044               /* parity register               */
#define uic0_tr         0x045               /* triggering register           */
#define uic0_msr        0x046               /* masked status register        */
#define uic0_vr         0x047               /* vector register               */
#define uic0_vcr        0x048               /* enable config register        */

/*----------------------------------------------------------------------------+
| PLB 0 and 1.
+----------------------------------------------------------------------------*/
#define pb0_pesr        0x054               /* PLB error status reg 0        */
#define pb0_pesrs       0x055               /* PLB error status reg 0 set    */
#define pb0_pear        0x056               /* PLB error address reg         */

#define pb1_pesr        0x064               /* PLB error status reg 1        */
#define pb1_pesrs       0x065               /* PLB error status reg 1 set    */
#define pb1_pear        0x066               /* PLB error address reg         */

/*----------------------------------------------------------------------------+
| EBIU DCR registers.
+----------------------------------------------------------------------------*/
#define ebiu0_brcrh0    0x070               /* bus region register 0 high    */
#define ebiu0_brcrh1    0x071               /* bus region register 1 high    */
#define ebiu0_brcrh2    0x072               /* bus region register 2 high    */
#define ebiu0_brcrh3    0x073               /* bus region register 3 high    */
#define ebiu0_brcrh4    0x074               /* bus region register 4 high    */
#define ebiu0_brcrh5    0x075               /* bus region register 5 high    */
#define ebiu0_brcrh6    0x076               /* bus region register 6 high    */
#define ebiu0_brcrh7    0x077               /* bus region register 7 high    */
#define ebiu0_brcr0     0x080               /* bus region register 0         */
#define ebiu0_brcr1     0x081               /* bus region register 1         */
#define ebiu0_brcr2     0x082               /* bus region register 2         */
#define ebiu0_brcr3     0x083               /* bus region register 3         */
#define ebiu0_brcr4     0x084               /* bus region register 4         */
#define ebiu0_brcr5     0x085               /* bus region register 5         */
#define ebiu0_brcr6     0x086               /* bus region register 6         */
#define ebiu0_brcr7     0x087               /* bus region register 7         */
#define ebiu0_bear      0x090               /* bus error address register    */
#define ebiu0_besr      0x091               /* bus error syndrome reg        */
#define ebiu0_besr0s    0x093               /* bus error syndrome reg        */
#define ebiu0_biucr     0x09a               /* bus interface control reg     */

/*----------------------------------------------------------------------------+
| OPB bridge.
+----------------------------------------------------------------------------*/
#define opbw0_gesr      0x0b0               /* error status reg              */
#define opbw0_gesrs     0x0b1               /* error status reg              */
#define opbw0_gear      0x0b2               /* error address reg             */

/*----------------------------------------------------------------------------+
| DMA.
+----------------------------------------------------------------------------*/
#define dma0_cr0        0x0c0               /* DMA channel control reg 0     */
#define dma0_ct0        0x0c1               /* DMA count register 0          */
#define dma0_da0        0x0c2               /* DMA destination addr reg 0    */
#define dma0_sa0        0x0c3               /* DMA source addr register 0    */
#define dma0_cc0        0x0c4               /* DMA chained count 0           */
#define dma0_cr1        0x0c8               /* DMA channel control reg 1     */
#define dma0_ct1        0x0c9               /* DMA count register 1          */
#define dma0_da1        0x0ca               /* DMA destination addr reg 1    */
#define dma0_sa1        0x0cb               /* DMA source addr register 1    */
#define dma0_cc1        0x0cc               /* DMA chained count 1           */
#define dma0_cr2        0x0d0               /* DMA channel control reg 2     */
#define dma0_ct2        0x0d1               /* DMA count register 2          */
#define dma0_da2        0x0d2               /* DMA destination addr reg 2    */
#define dma0_sa2        0x0d3               /* DMA source addr register 2    */
#define dma0_cc2        0x0d4               /* DMA chained count 2           */
#define dma0_cr3        0x0d8               /* DMA channel control reg 3     */
#define dma0_ct3        0x0d9               /* DMA count register 3          */
#define dma0_da3        0x0da               /* DMA destination addr reg 3    */
#define dma0_sa3        0x0db               /* DMA source addr register 3    */
#define dma0_cc3        0x0dc               /* DMA chained count 3           */
#define dma0_sr         0x0e0               /* DMA status register           */
#define dma0_srs        0x0e1               /* DMA status register           */
#define dma0_s1         0x031               /* DMA select1 register          */
#define dma0_s2         0x032               /* DMA select2 register          */

/*---------------------------------------------------------------------------+
| Clock and power management.
+----------------------------------------------------------------------------*/
#define cpm0_fr         0x102               /* force register                */

/*----------------------------------------------------------------------------+
| Serial Clock Control.
+----------------------------------------------------------------------------*/
#define ser0_ccr        0x120               /* serial clock control register */

/*----------------------------------------------------------------------------+
| Audio Clock Control.
+----------------------------------------------------------------------------*/
#define aud0_apcr       0x121               /* audio clock ctrl register     */

/*----------------------------------------------------------------------------+
| DENC.
+----------------------------------------------------------------------------*/
#define denc0_idr       0x130               /* DENC ID register              */
#define denc0_cr1       0x131               /* control register 1            */
#define denc0_rr1       0x132               /* microvision 1 (reserved 1)    */
#define denc0_cr2       0x133               /* control register 2            */
#define denc0_rr2       0x134               /* microvision 2 (reserved 2)    */
#define denc0_rr3       0x135               /* microvision 3 (reserved 3)    */
#define denc0_rr4       0x136               /* microvision 4 (reserved 4)    */
#define denc0_rr5       0x137               /* microvision 5 (reserved 5)    */
#define denc0_ccdr      0x138               /* closed caption data           */
#define denc0_cccr      0x139               /* closed caption control        */
#define denc0_trr       0x13A               /* teletext request register     */
#define denc0_tosr      0x13B               /* teletext odd field line se    */
#define denc0_tesr      0x13C               /* teletext even field line s    */
#define denc0_rlsr      0x13D               /* RGB rhift left register       */
#define denc0_vlsr      0x13E               /* video level shift register    */
#define denc0_vsr       0x13F               /* video scaling register        */

/*----------------------------------------------------------------------------+
| Video decoder.  Suspect 0x179, 0x169, 0x16a, 0x152 (rc).
+----------------------------------------------------------------------------*/
#define vid0_ccntl      0x140               /* control decoder operation     */
#define vid0_cmode      0x141               /* video operational mode        */
#define vid0_sstc0      0x142               /* STC high order bits 31:0      */
#define vid0_sstc1      0x143               /* STC low order bit 32          */
#define vid0_spts0      0x144               /* PTS high order bits 31:0      */
#define vid0_spts1      0x145               /* PTS low order bit 32          */
#define vid0_fifo       0x146               /* FIFO data port                */
#define vid0_fifos      0x147               /* FIFO status                   */
#define vid0_cmd        0x148               /* send command to decoder       */
#define vid0_cmdd       0x149               /* port for command params       */
#define vid0_cmdst      0x14A               /* command status                */
#define vid0_cmdad      0x14B               /* command address               */
#define vid0_procia     0x14C               /* instruction store             */
#define vid0_procid     0x14D               /* data port for I_Store         */
#define vid0_osdm       0x151               /* OSD mode control              */
#define vid0_hosti      0x152               /* base interrupt register       */
#define vid0_mask       0x153               /* interrupt mask register       */
#define vid0_dispm      0x154               /* operational mode for Disp     */
#define vid0_dispd      0x155               /* setting for 'Sync' delay      */
#define vid0_vbctl      0x156               /* VBI                           */
#define vid0_ttxctl     0x157               /* teletext control              */
#define vid0_disptb     0x158               /* display left/top border       */
#define vid0_osdgla     0x159               /* Graphics plane link addr      */
#define vid0_osdila     0x15A               /* Image plane link addr         */
#define vid0_rbthr      0x15B               /* rate buffer threshold         */
#define vid0_osdcla     0x15C               /* Cursor link addr              */
#define vid0_stcca      0x15D               /* STC common address            */
#define vid0_ptsctl     0x15F               /* PTS Control                   */
#define vid0_wprot      0x165               /* write protect for I_Store     */
#define vid0_vcqa       0x167               /* video clip queued block Ad    */
#define vid0_vcql       0x168               /* video clip queued block Le    */
#define vid0_blksz      0x169               /* block size bytes for copy op  */
#define vid0_srcad      0x16a               /* copy source address bits 6-31 */
#define vid0_udbas      0x16B               /* base mem add for user data    */
#define vid0_vbibas     0x16C               /* base mem add for VBI 0/1      */
#define vid0_osdibas    0x16D               /* Image plane base address      */
#define vid0_osdgbas    0x16E               /* Graphic plane base address    */
#define vid0_rbbase     0x16F               /* base mem add for video buf    */
#define vid0_dramad     0x170               /* DRAM address                  */
#define vid0_dramdt     0x171               /* data port for DRAM access     */
#define vid0_dramcs     0x172               /* DRAM command and statusa      */
#define vid0_vcwa       0x173               /* v clip work address           */
#define vid0_vcwl       0x174               /* v clip work length            */
#define vid0_mseg0      0x175               /* segment address 0             */
#define vid0_mseg1      0x176               /* segment address 1             */
#define vid0_mseg2      0x177               /* segment address 2             */
#define vid0_mseg3      0x178               /* segment address 3             */
#define vid0_fbbase     0x179               /* frame buffer base memory      */
#define vid0_osdcbas    0x17A               /* Cursor base addr              */
#define vid0_lboxtb     0x17B               /* top left border               */
#define vid0_trdly      0x17C               /* transparency gate delay       */
#define vid0_sbord      0x17D               /* left/top small pict. bord.    */
#define vid0_zoffs      0x17E               /* hor/ver zoom window           */
#define vid0_rbsz       0x17F               /* rate buffer size read         */

/*----------------------------------------------------------------------------+
| Transport demultiplexer.
+----------------------------------------------------------------------------*/
#define xpt0_lr         0x180               /* demux location register       */
#define xpt0_data       0x181               /* demux data register           */
#define xpt0_ir         0x182               /* demux interrupt register      */

#define xpt0_config1    0x0000              /* configuration 1               */
#define xpt0_control1   0x0001              /* control 1                     */
#define xpt0_festat     0x0002              /* Front-end status              */
#define xpt0_feimask    0x0003              /* Front_end interrupt Mask      */
#define xpt0_ocmcnfg    0x0004              /* OCM Address                   */
#define xpt0_settapi    0x0005              /* Set TAP Interrupt             */

#define xpt0_pcrhi      0x0010              /* PCR High                      */
#define xpt0_pcrlow     0x0011              /* PCR Low                       */
#define xpt0_lstchi     0x0012              /* Latched STC High              */
#define xpt0_lstclow    0x0013              /* Latched STC Low               */
#define xpt0_stchi      0x0014              /* STC High                      */
#define xpt0_stclow     0x0015              /* STC Low                       */
#define xpt0_pwm        0x0016              /* PWM                           */
#define xpt0_pcrstct    0x0017              /* PCR-STC Threshold             */
#define xpt0_pcrstcd    0x0018              /* PCR-STC Delta                 */
#define xpt0_stccomp    0x0019              /* STC Compare                   */
#define xpt0_stccmpd    0x001a              /* STC Compare Disarm            */

#define xpt0_dsstat     0x0048              /* Descrambler Status            */
#define xpt0_dsimask    0x0049              /* Descrambler Interrupt Mask    */

#define xpt0_vcchng     0x01f0              /* Video Channel Change          */
#define xpt0_acchng     0x01f1              /* Audio Channel Change          */
#define xpt0_axenable   0x01fe              /* Aux PID Enables               */
#define xpt0_pcrpid     0x01ff              /* PCR PID                       */

#define xpt0_config2    0x1000              /* Configuration 2               */
#define xpt0_pbuflvl    0x1002              /* Packet Buffer Level           */
#define xpt0_intmask    0x1003              /* Interrupt Mask                */
#define xpt0_plbcnfg    0x1004              /* PLB Configuration             */

#define xpt0_qint       0x1010              /* Queues Interrupts             */
#define xpt0_qintmsk    0x1011              /* Queues Interrupts Mask        */
#define xpt0_astatus    0x1012              /* Audio Status                  */
#define xpt0_aintmask   0x1013              /* Audio Interrupt Mask          */
#define xpt0_vstatus    0x1014              /* Video Status                  */
#define xpt0_vintmask   0x1015              /* Video Interrupt Mask          */

#define xpt0_qbase      0x1020              /* Queue Base                    */
#define xpt0_bucketq    0x1021              /* Bucket Queue                  */
#define xpt0_qstops     0x1024              /* Queue Stops                   */
#define xpt0_qresets    0x1025              /* Queue Resets                  */
#define xpt0_sfchng     0x1026              /* Section Filter Change         */

/*----------------------------------------------------------------------------+
| Audio decoder. Suspect 0x1ad, 0x1b4, 0x1a3, 0x1a5 (read/write status)
+----------------------------------------------------------------------------*/
#define aud0_ctrl0      0x1a0               /* control 0                     */
#define aud0_ctrl1      0x1a1               /* control 1                     */
#define aud0_ctrl2      0x1a2               /* control 2                     */
#define aud0_cmd        0x1a3               /* command register              */
#define aud0_isr        0x1a4               /* interrupt status register     */
#define aud0_imr        0x1a5               /* interrupt mask register       */
#define aud0_dsr        0x1a6               /* decoder status register       */
#define aud0_stc        0x1a7               /* system time clock             */
#define aud0_csr        0x1a8               /* channel status register       */
#define aud0_lcnt       0x1a9               /* queued address register 2     */
#define aud0_pts        0x1aa               /* presentation time stamp       */
#define aud0_tgctrl     0x1ab               /* tone generation control       */
#define aud0_qlr2       0x1ac               /* queued length register 2      */
#define aud0_auxd       0x1ad               /* aux data                      */
#define aud0_strmid     0x1ae               /* stream ID                     */
#define aud0_qar        0x1af               /* queued address register       */
#define aud0_dsps       0x1b0               /* DSP status                    */
#define aud0_qlr        0x1b1               /* queued len address            */
#define aud0_dspc       0x1b2               /* DSP control                   */
#define aud0_wlr2       0x1b3               /* working length register 2     */
#define aud0_instd      0x1b4               /* instruction download          */
#define aud0_war        0x1b5               /* working address register      */
#define aud0_seg1       0x1b6               /* segment 1 base register       */
#define aud0_seg2       0x1b7               /* segment 2 base register       */
#define aud0_avf        0x1b9               /* audio att value front         */
#define aud0_avr        0x1ba               /* audio att value rear          */
#define aud0_avc        0x1bb               /* audio att value center        */
#define aud0_seg3       0x1bc               /* segment 3 base register       */
#define aud0_offset     0x1bd               /* offset address                */
#define aud0_wrl        0x1be               /* working length register       */
#define aud0_war2       0x1bf               /* working address register 2    */

/*----------------------------------------------------------------------------+
| High speed memory controller 0 and 1.
+----------------------------------------------------------------------------*/
#define hsmc0_gr        0x1e0               /* HSMC global register          */
#define hsmc0_besr      0x1e1               /* bus error status register     */
#define hsmc0_bear      0x1e2               /* bus error address register    */
#define hsmc0_br0       0x1e4               /* SDRAM sub-ctrl bank reg 0     */
#define hsmc0_cr0       0x1e5               /* SDRAM sub-ctrl ctrl reg 0     */
#define hsmc0_br1       0x1e7               /* SDRAM sub-ctrl bank reg 1     */
#define hsmc0_cr1       0x1e8               /* SDRAM sub-ctrl ctrl reg 1     */
#define hsmc0_sysr      0x1f1               /* system register               */
#define hsmc0_data      0x1f2               /* data register                 */
#define hsmc0_crr       0x1f3               /* refresh register              */

#define hsmc1_gr        0x1c0               /* HSMC global register          */
#define hsmc1_besr      0x1c1               /* bus error status register     */
#define hsmc1_bear      0x1c2               /* bus error address register    */
#define hsmc1_br0       0x1c4               /* SDRAM sub-ctrl bank reg 0     */
#define hsmc1_cr0       0x1c5               /* SDRAM sub-ctrl ctrl reg 0     */
#define hsmc1_br1       0x1c7               /* SDRAM sub-ctrl bank reg 1     */
#define hsmc1_cr1       0x1c8               /* SDRAM sub-ctrl ctrl reg 1     */
#define hsmc1_sysr      0x1d1               /* system register               */
#define hsmc1_data      0x1d2               /* data register                 */
#define hsmc1_crr       0x1d3               /* refresh register              */

/*----------------------------------------------------------------------------+
| Machine State Register bit definitions.
+----------------------------------------------------------------------------*/
#define msr_ape         0x00100000
#define msr_apa         0x00080000
#define msr_we          0x00040000
#define msr_ce          0x00020000
#define msr_ile         0x00010000
#define msr_ee          0x00008000
#define msr_pr          0x00004000
#define msr_me          0x00001000
#define msr_de          0x00000200
#define msr_ir          0x00000020
#define msr_dr          0x00000010
#define msr_le          0x00000001

/*----------------------------------------------------------------------------+
| Used during interrupt processing.
+----------------------------------------------------------------------------*/
#define stack_reg_image_size            160

/*----------------------------------------------------------------------------+
| Function prolog definition and other Metaware (EABI) defines.
+----------------------------------------------------------------------------*/
#ifdef MW

#define r0              0
#define r1              1
#define r2              2
#define r3              3
#define r4              4
#define r5              5
#define r6              6
#define r7              7
#define r8              8
#define r9              9
#define r10             10
#define r11             11
#define r12             12
#define r13             13
#define r14             14
#define r15             15
#define r16             16
#define r17             17
#define r18             18
#define r19             19
#define r20             20
#define r21             21
#define r22             22
#define r23             23
#define r24             24
#define r25             25
#define r26             26
#define r27             27
#define r28             28
#define r29             29
#define r30             30
#define r31             31

#define cr0             0
#define cr1             1
#define cr2             2
#define cr3             3
#define cr4             4
#define cr5             5
#define cr6             6
#define cr7             7

#define function_prolog(func_name)      .text; \
                                        .align  2; \
                                        .globl  func_name; \
                                        func_name:
#define function_epilog(func_name)      .type func_name,@function; \
                                        .size func_name,.-func_name

#define function_call(func_name)        bl func_name

#define stack_frame_min                 8
#define stack_frame_bc                  0
#define stack_frame_lr                  4
#define stack_neg_off                   0

#endif

/*----------------------------------------------------------------------------+
| Function prolog definition and other DIAB (Elf) defines.
+----------------------------------------------------------------------------*/
#ifdef ELF_DIAB

fprolog:        macro   f_name
                .text
                .align  2
                .globl  f_name
f_name:
                endm

fepilog:        macro   f_name
                .type   f_name,@function
                .size   f_name,.-f_name
                endm

#define function_prolog(func_name)      fprolog func_name
#define function_epilog(func_name)      fepilog func_name
#define function_call(func_name)        bl func_name

#define stack_frame_min                 8
#define stack_frame_bc                  0
#define stack_frame_lr                  4
#define stack_neg_off                   0

#endif

/*----------------------------------------------------------------------------+
| Function prolog definition and other Xlc (XCOFF) defines.
+----------------------------------------------------------------------------*/
#ifdef XCOFF

.machine "403ga"

#define r0              0
#define r1              1
#define r2              2
#define r3              3
#define r4              4
#define r5              5
#define r6              6
#define r7              7
#define r8              8
#define r9              9
#define r10             10
#define r11             11
#define r12             12
#define r13             13
#define r14             14
#define r15             15
#define r16             16
#define r17             17
#define r18             18
#define r19             19
#define r20             20
#define r21             21
#define r22             22
#define r23             23
#define r24             24
#define r25             25
#define r26             26
#define r27             27
#define r28             28
#define r29             29
#define r30             30
#define r31             31

#define cr0             0
#define cr1             1
#define cr2             2
#define cr3             3
#define cr4             4
#define cr5             5
#define cr6             6
#define cr7             7

#define function_prolog(func_name)      .csect .func_name[PR]; \
                                        .globl .func_name[PR]; \
                                        func_name:

#define function_epilog(func_name)      .toc; \
                                        .csect  func_name[DS]; \
                                        .globl  func_name[DS]; \
                                        .long   .func_name[PR]; \
                                        .long   TOC[tc0]

#define function_call(func_name)        .extern .func_name[PR]; \
                                        stw     r2,stack_frame_toc(r1); \
                                        mfspr   r2,sprg0; \
                                        bl      .func_name[PR]; \
                                        lwz     r2,stack_frame_toc(r1)

#define stack_frame_min                 56
#define stack_frame_bc                  0
#define stack_frame_lr                  8
#define stack_frame_toc                 20
#define stack_neg_off                   276

#endif
#define function_prolog(func_name)      .text; \
                                        .align  2; \
                                        .globl  func_name; \
                                        func_name:
#define function_epilog(func_name)      .type func_name,@function; \
                                        .size func_name,.-func_name

#define function_call(func_name)        bl func_name

/*----------------------------------------------------------------------------+
| Function prolog definition for GNU
+----------------------------------------------------------------------------*/
#ifdef _GNU_TOOL

#define function_prolog(func_name)      .globl  func_name; \
                                        func_name:
#define function_epilog(func_name)

#endif
