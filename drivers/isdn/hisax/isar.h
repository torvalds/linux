/* $Id: isar.h,v 1.11.2.2 2004/01/12 22:52:27 keil Exp $
 *
 * ISAR (Siemens PSB 7110) specific defines
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#define ISAR_IRQMSK	0x04
#define ISAR_IRQSTA	0x04
#define ISAR_IRQBIT	0x75
#define ISAR_CTRL_H	0x61
#define ISAR_CTRL_L	0x60
#define ISAR_IIS	0x58
#define ISAR_IIA	0x58
#define ISAR_HIS	0x50
#define ISAR_HIA	0x50
#define ISAR_MBOX	0x4c
#define ISAR_WADR	0x4a
#define ISAR_RADR	0x48

#define ISAR_HIS_VNR		0x14
#define ISAR_HIS_DKEY		0x02
#define ISAR_HIS_FIRM		0x1e
#define ISAR_HIS_STDSP		0x08
#define ISAR_HIS_DIAG		0x05
#define ISAR_HIS_WAITSTATE	0x27
#define ISAR_HIS_TIMERIRQ	0x25
#define ISAR_HIS_P0CFG		0x3c
#define ISAR_HIS_P12CFG		0x24
#define ISAR_HIS_SARTCFG	0x25
#define ISAR_HIS_PUMPCFG	0x26
#define ISAR_HIS_PUMPCTRL	0x2a
#define ISAR_HIS_IOM2CFG	0x27
#define ISAR_HIS_IOM2REQ	0x07
#define ISAR_HIS_IOM2CTRL	0x2b
#define ISAR_HIS_BSTREQ		0x0c
#define ISAR_HIS_PSTREQ		0x0e
#define ISAR_HIS_SDATA		0x20
#define ISAR_HIS_DPS1		0x40
#define ISAR_HIS_DPS2		0x80
#define SET_DPS(x)		((x << 6) & 0xc0)

#define ISAR_CMD_TIMERIRQ_OFF	0x20
#define ISAR_CMD_TIMERIRQ_ON	0x21


#define ISAR_IIS_MSCMSD		0x3f
#define ISAR_IIS_VNR		0x15
#define ISAR_IIS_DKEY		0x03
#define ISAR_IIS_FIRM		0x1f
#define ISAR_IIS_STDSP		0x09
#define ISAR_IIS_DIAG		0x25
#define ISAR_IIS_GSTEV		0x00
#define ISAR_IIS_BSTEV		0x28
#define ISAR_IIS_BSTRSP		0x2c
#define ISAR_IIS_PSTRSP		0x2e
#define ISAR_IIS_PSTEV		0x2a
#define ISAR_IIS_IOM2RSP	0x27
#define ISAR_IIS_RDATA		0x20
#define ISAR_IIS_INVMSG		0x3f

#define ISAR_CTRL_SWVER	0x10
#define ISAR_CTRL_STST	0x40

#define ISAR_MSG_HWVER	{0x20, 0, 1}

#define ISAR_DP1_USE	1
#define ISAR_DP2_USE	2
#define ISAR_RATE_REQ	3

#define PMOD_DISABLE	0
#define PMOD_FAX	1
#define PMOD_DATAMODEM	2
#define PMOD_HALFDUPLEX	3
#define PMOD_V110	4
#define PMOD_DTMF	5
#define PMOD_DTMF_TRANS	6
#define PMOD_BYPASS	7

#define PCTRL_ORIG	0x80
#define PV32P2_V23R	0x40
#define PV32P2_V22A	0x20
#define PV32P2_V22B	0x10
#define PV32P2_V22C	0x08
#define PV32P2_V21	0x02
#define PV32P2_BEL	0x01

// LSB MSB in ISAR doc wrong !!! Arghhh
#define PV32P3_AMOD	0x80
#define PV32P3_V32B	0x02
#define PV32P3_V23B	0x01
#define PV32P4_48	0x11
#define PV32P5_48	0x05
#define PV32P4_UT48	0x11
#define PV32P5_UT48	0x0d
#define PV32P4_96	0x11
#define PV32P5_96	0x03
#define PV32P4_UT96	0x11
#define PV32P5_UT96	0x0f
#define PV32P4_B96	0x91
#define PV32P5_B96	0x0b
#define PV32P4_UTB96	0xd1
#define PV32P5_UTB96	0x0f
#define PV32P4_120	0xb1
#define PV32P5_120	0x09
#define PV32P4_UT120	0xf1
#define PV32P5_UT120	0x0f
#define PV32P4_144	0x99
#define PV32P5_144	0x09
#define PV32P4_UT144	0xf9
#define PV32P5_UT144	0x0f
#define PV32P6_CTN	0x01
#define PV32P6_ATN	0x02

#define PFAXP2_CTN	0x01
#define PFAXP2_ATN	0x04

#define PSEV_10MS_TIMER	0x02
#define PSEV_CON_ON	0x18
#define PSEV_CON_OFF	0x19
#define PSEV_V24_OFF	0x20
#define PSEV_CTS_ON	0x21
#define PSEV_CTS_OFF	0x22
#define PSEV_DCD_ON	0x23
#define PSEV_DCD_OFF	0x24
#define PSEV_DSR_ON	0x25
#define PSEV_DSR_OFF	0x26
#define PSEV_REM_RET	0xcc
#define PSEV_REM_REN	0xcd
#define PSEV_GSTN_CLR	0xd4

#define PSEV_RSP_READY	0xbc
#define PSEV_LINE_TX_H	0xb3
#define PSEV_LINE_TX_B	0xb2
#define PSEV_LINE_RX_H	0xb1
#define PSEV_LINE_RX_B	0xb0
#define PSEV_RSP_CONN	0xb5
#define PSEV_RSP_DISC	0xb7
#define PSEV_RSP_FCERR	0xb9
#define PSEV_RSP_SILDET	0xbe
#define PSEV_RSP_SILOFF	0xab
#define PSEV_FLAGS_DET	0xba

#define PCTRL_CMD_FTH	0xa7
#define PCTRL_CMD_FRH	0xa5
#define PCTRL_CMD_FTM	0xa8
#define PCTRL_CMD_FRM	0xa6
#define PCTRL_CMD_SILON	0xac
#define PCTRL_CMD_CONT	0xa2
#define PCTRL_CMD_ESC	0xa4
#define PCTRL_CMD_SILOFF 0xab
#define PCTRL_CMD_HALT	0xa9

#define PCTRL_LOC_RET	0xcf
#define PCTRL_LOC_REN	0xce

#define SMODE_DISABLE	0
#define SMODE_V14	2
#define SMODE_HDLC	3
#define SMODE_BINARY	4
#define SMODE_FSK_V14	5

#define SCTRL_HDMC_BOTH	0x00
#define SCTRL_HDMC_DTX	0x80
#define SCTRL_HDMC_DRX	0x40
#define S_P1_OVSP	0x40
#define S_P1_SNP	0x20
#define S_P1_EOP	0x10
#define S_P1_EDP	0x08
#define S_P1_NSB	0x04
#define S_P1_CHS_8	0x03
#define S_P1_CHS_7	0x02
#define S_P1_CHS_6	0x01
#define S_P1_CHS_5	0x00

#define S_P2_BFT_DEF	0x10

#define IOM_CTRL_ENA	0x80
#define IOM_CTRL_NOPCM	0x00
#define IOM_CTRL_ALAW	0x02
#define IOM_CTRL_ULAW	0x04
#define IOM_CTRL_RCV	0x01

#define IOM_P1_TXD	0x10

#define HDLC_FED	0x40
#define HDLC_FSD	0x20
#define HDLC_FST	0x20
#define HDLC_ERROR	0x1c
#define HDLC_ERR_FAD	0x10
#define HDLC_ERR_RER	0x08
#define HDLC_ERR_CER	0x04
#define SART_NMD	0x01

#define BSTAT_RDM0	0x1
#define BSTAT_RDM1	0x2
#define BSTAT_RDM2	0x4
#define BSTAT_RDM3	0x8
#define BSTEV_TBO	0x1f
#define BSTEV_RBO	0x2f

/* FAX State Machine */
#define STFAX_NULL	0
#define STFAX_READY	1
#define STFAX_LINE	2
#define STFAX_CONT	3
#define STFAX_ACTIV	4
#define STFAX_ESCAPE	5
#define STFAX_SILDET	6

#define ISDN_FAXPUMP_HALT	100

extern int ISARVersion(struct IsdnCardState *cs, char *s);
extern void isar_int_main(struct IsdnCardState *cs);
extern void initisar(struct IsdnCardState *cs);
extern void isar_fill_fifo(struct BCState *bcs);
extern int isar_auxcmd(struct IsdnCardState *cs, isdn_ctrl *ic);
