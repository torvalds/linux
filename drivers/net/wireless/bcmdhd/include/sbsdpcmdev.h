/*
 * Broadcom SiliconBackplane SDIO/PCMCIA hardware-specific
 * device core support
 *
 * Copyright (C) 1999-2013, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: sbsdpcmdev.h 336848 2012-06-05 11:28:07Z $
 */

#ifndef	_sbsdpcmdev_h_
#define	_sbsdpcmdev_h_

/* cpp contortions to concatenate w/arg prescan */
#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif	/* PAD */


typedef volatile struct {
	dma64regs_t	xmt;		/* dma tx */
	uint32 PAD[2];
	dma64regs_t	rcv;		/* dma rx */
	uint32 PAD[2];
} dma64p_t;

/* dma64 sdiod corerev >= 1 */
typedef volatile struct {
	dma64p_t dma64regs[2];
	dma64diag_t dmafifo;		/* DMA Diagnostic Regs, 0x280-0x28c */
	uint32 PAD[92];
} sdiodma64_t;

/* dma32 sdiod corerev == 0 */
typedef volatile struct {
	dma32regp_t dma32regs[2];	/* dma tx & rx, 0x200-0x23c */
	dma32diag_t dmafifo;		/* DMA Diagnostic Regs, 0x240-0x24c */
	uint32 PAD[108];
} sdiodma32_t;

/* dma32 regs for pcmcia core */
typedef volatile struct {
	dma32regp_t dmaregs;		/* DMA Regs, 0x200-0x21c, rev8 */
	dma32diag_t dmafifo;		/* DMA Diagnostic Regs, 0x220-0x22c */
	uint32 PAD[116];
} pcmdma32_t;

/* core registers */
typedef volatile struct {
	uint32 corecontrol;		/* CoreControl, 0x000, rev8 */
	uint32 corestatus;		/* CoreStatus, 0x004, rev8  */
	uint32 PAD[1];
	uint32 biststatus;		/* BistStatus, 0x00c, rev8  */

	/* PCMCIA access */
	uint16 pcmciamesportaladdr;	/* PcmciaMesPortalAddr, 0x010, rev8   */
	uint16 PAD[1];
	uint16 pcmciamesportalmask;	/* PcmciaMesPortalMask, 0x014, rev8   */
	uint16 PAD[1];
	uint16 pcmciawrframebc;		/* PcmciaWrFrameBC, 0x018, rev8   */
	uint16 PAD[1];
	uint16 pcmciaunderflowtimer;	/* PcmciaUnderflowTimer, 0x01c, rev8   */
	uint16 PAD[1];

	/* interrupt */
	uint32 intstatus;		/* IntStatus, 0x020, rev8   */
	uint32 hostintmask;		/* IntHostMask, 0x024, rev8   */
	uint32 intmask;			/* IntSbMask, 0x028, rev8   */
	uint32 sbintstatus;		/* SBIntStatus, 0x02c, rev8   */
	uint32 sbintmask;		/* SBIntMask, 0x030, rev8   */
	uint32 funcintmask;		/* SDIO Function Interrupt Mask, SDIO rev4 */
	uint32 PAD[2];
	uint32 tosbmailbox;		/* ToSBMailbox, 0x040, rev8   */
	uint32 tohostmailbox;		/* ToHostMailbox, 0x044, rev8   */
	uint32 tosbmailboxdata;		/* ToSbMailboxData, 0x048, rev8   */
	uint32 tohostmailboxdata;	/* ToHostMailboxData, 0x04c, rev8   */

	/* synchronized access to registers in SDIO clock domain */
	uint32 sdioaccess;		/* SdioAccess, 0x050, rev8   */
	uint32 PAD[3];

	/* PCMCIA frame control */
	uint8 pcmciaframectrl;		/* pcmciaFrameCtrl, 0x060, rev8   */
	uint8 PAD[3];
	uint8 pcmciawatermark;		/* pcmciaWaterMark, 0x064, rev8   */
	uint8 PAD[155];

	/* interrupt batching control */
	uint32 intrcvlazy;		/* IntRcvLazy, 0x100, rev8 */
	uint32 PAD[3];

	/* counters */
	uint32 cmd52rd;			/* Cmd52RdCount, 0x110, rev8, SDIO: cmd52 reads */
	uint32 cmd52wr;			/* Cmd52WrCount, 0x114, rev8, SDIO: cmd52 writes */
	uint32 cmd53rd;			/* Cmd53RdCount, 0x118, rev8, SDIO: cmd53 reads */
	uint32 cmd53wr;			/* Cmd53WrCount, 0x11c, rev8, SDIO: cmd53 writes */
	uint32 abort;			/* AbortCount, 0x120, rev8, SDIO: aborts */
	uint32 datacrcerror;		/* DataCrcErrorCount, 0x124, rev8, SDIO: frames w/bad CRC */
	uint32 rdoutofsync;		/* RdOutOfSyncCount, 0x128, rev8, SDIO/PCMCIA: Rd Frm OOS */
	uint32 wroutofsync;		/* RdOutOfSyncCount, 0x12c, rev8, SDIO/PCMCIA: Wr Frm OOS */
	uint32 writebusy;		/* WriteBusyCount, 0x130, rev8, SDIO: dev asserted "busy" */
	uint32 readwait;		/* ReadWaitCount, 0x134, rev8, SDIO: read: no data avail */
	uint32 readterm;		/* ReadTermCount, 0x138, rev8, SDIO: rd frm terminates */
	uint32 writeterm;		/* WriteTermCount, 0x13c, rev8, SDIO: wr frm terminates */
	uint32 PAD[40];
	uint32 clockctlstatus;		/* ClockCtlStatus, 0x1e0, rev8 */
	uint32 PAD[7];

	/* DMA engines */
	volatile union {
		pcmdma32_t pcm32;
		sdiodma32_t sdiod32;
		sdiodma64_t sdiod64;
	} dma;

	/* SDIO/PCMCIA CIS region */
	char cis[512];			/* 512 byte CIS, 0x400-0x5ff, rev6 */

	/* PCMCIA function control registers */
	char pcmciafcr[256];		/* PCMCIA FCR, 0x600-6ff, rev6 */
	uint16 PAD[55];

	/* PCMCIA backplane access */
	uint16 backplanecsr;		/* BackplaneCSR, 0x76E, rev6 */
	uint16 backplaneaddr0;		/* BackplaneAddr0, 0x770, rev6 */
	uint16 backplaneaddr1;		/* BackplaneAddr1, 0x772, rev6 */
	uint16 backplaneaddr2;		/* BackplaneAddr2, 0x774, rev6 */
	uint16 backplaneaddr3;		/* BackplaneAddr3, 0x776, rev6 */
	uint16 backplanedata0;		/* BackplaneData0, 0x778, rev6 */
	uint16 backplanedata1;		/* BackplaneData1, 0x77a, rev6 */
	uint16 backplanedata2;		/* BackplaneData2, 0x77c, rev6 */
	uint16 backplanedata3;		/* BackplaneData3, 0x77e, rev6 */
	uint16 PAD[31];

	/* sprom "size" & "blank" info */
	uint16 spromstatus;		/* SPROMStatus, 0x7BE, rev2 */
	uint32 PAD[464];

	/* Sonics SiliconBackplane registers */
	sbconfig_t sbconfig;		/* SbConfig Regs, 0xf00-0xfff, rev8 */
} sdpcmd_regs_t;

/* corecontrol */
#define CC_CISRDY		(1 << 0)	/* CIS Ready */
#define CC_BPRESEN		(1 << 1)	/* CCCR RES signal causes backplane reset */
#define CC_F2RDY		(1 << 2)	/* set CCCR IOR2 bit */
#define CC_CLRPADSISO		(1 << 3)	/* clear SDIO pads isolation bit (rev 11) */
#define CC_XMTDATAAVAIL_MODE	(1 << 4)	/* data avail generates an interrupt */
#define CC_XMTDATAAVAIL_CTRL	(1 << 5)	/* data avail interrupt ctrl */

/* corestatus */
#define CS_PCMCIAMODE	(1 << 0)	/* Device Mode; 0=SDIO, 1=PCMCIA */
#define CS_SMARTDEV	(1 << 1)	/* 1=smartDev enabled */
#define CS_F2ENABLED	(1 << 2)	/* 1=host has enabled the device */

#define PCMCIA_MES_PA_MASK	0x7fff	/* PCMCIA Message Portal Address Mask */
#define PCMCIA_MES_PM_MASK	0x7fff	/* PCMCIA Message Portal Mask Mask */
#define PCMCIA_WFBC_MASK	0xffff	/* PCMCIA Write Frame Byte Count Mask */
#define PCMCIA_UT_MASK		0x07ff	/* PCMCIA Underflow Timer Mask */

/* intstatus */
#define I_SMB_SW0	(1 << 0)	/* To SB Mail S/W interrupt 0 */
#define I_SMB_SW1	(1 << 1)	/* To SB Mail S/W interrupt 1 */
#define I_SMB_SW2	(1 << 2)	/* To SB Mail S/W interrupt 2 */
#define I_SMB_SW3	(1 << 3)	/* To SB Mail S/W interrupt 3 */
#define I_SMB_SW_MASK	0x0000000f	/* To SB Mail S/W interrupts mask */
#define I_SMB_SW_SHIFT	0		/* To SB Mail S/W interrupts shift */
#define I_HMB_SW0	(1 << 4)	/* To Host Mail S/W interrupt 0 */
#define I_HMB_SW1	(1 << 5)	/* To Host Mail S/W interrupt 1 */
#define I_HMB_SW2	(1 << 6)	/* To Host Mail S/W interrupt 2 */
#define I_HMB_SW3	(1 << 7)	/* To Host Mail S/W interrupt 3 */
#define I_HMB_SW_MASK	0x000000f0	/* To Host Mail S/W interrupts mask */
#define I_HMB_SW_SHIFT	4		/* To Host Mail S/W interrupts shift */
#define I_WR_OOSYNC	(1 << 8)	/* Write Frame Out Of Sync */
#define I_RD_OOSYNC	(1 << 9)	/* Read Frame Out Of Sync */
#define	I_PC		(1 << 10)	/* descriptor error */
#define	I_PD		(1 << 11)	/* data error */
#define	I_DE		(1 << 12)	/* Descriptor protocol Error */
#define	I_RU		(1 << 13)	/* Receive descriptor Underflow */
#define	I_RO		(1 << 14)	/* Receive fifo Overflow */
#define	I_XU		(1 << 15)	/* Transmit fifo Underflow */
#define	I_RI		(1 << 16)	/* Receive Interrupt */
#define I_BUSPWR	(1 << 17)	/* SDIO Bus Power Change (rev 9) */
#define I_XMTDATA_AVAIL (1 << 23)	/* bits in fifo */
#define	I_XI		(1 << 24)	/* Transmit Interrupt */
#define I_RF_TERM	(1 << 25)	/* Read Frame Terminate */
#define I_WF_TERM	(1 << 26)	/* Write Frame Terminate */
#define I_PCMCIA_XU	(1 << 27)	/* PCMCIA Transmit FIFO Underflow */
#define I_SBINT		(1 << 28)	/* sbintstatus Interrupt */
#define I_CHIPACTIVE	(1 << 29)	/* chip transitioned from doze to active state */
#define I_SRESET	(1 << 30)	/* CCCR RES interrupt */
#define I_IOE2		(1U << 31)	/* CCCR IOE2 Bit Changed */
#define	I_ERRORS	(I_PC | I_PD | I_DE | I_RU | I_RO | I_XU)	/* DMA Errors */
#define I_DMA		(I_RI | I_XI | I_ERRORS)

/* sbintstatus */
#define I_SB_SERR	(1 << 8)	/* Backplane SError (write) */
#define I_SB_RESPERR	(1 << 9)	/* Backplane Response Error (read) */
#define I_SB_SPROMERR	(1 << 10)	/* Error accessing the sprom */

/* sdioaccess */
#define SDA_DATA_MASK	0x000000ff	/* Read/Write Data Mask */
#define SDA_ADDR_MASK	0x000fff00	/* Read/Write Address Mask */
#define SDA_ADDR_SHIFT	8		/* Read/Write Address Shift */
#define SDA_WRITE	0x01000000	/* Write bit  */
#define SDA_READ	0x00000000	/* Write bit cleared for Read */
#define SDA_BUSY	0x80000000	/* Busy bit */

/* sdioaccess-accessible register address spaces */
#define SDA_CCCR_SPACE		0x000	/* sdioAccess CCCR register space */
#define SDA_F1_FBR_SPACE	0x100	/* sdioAccess F1 FBR register space */
#define SDA_F2_FBR_SPACE	0x200	/* sdioAccess F2 FBR register space */
#define SDA_F1_REG_SPACE	0x300	/* sdioAccess F1 core-specific register space */

/* SDA_F1_REG_SPACE sdioaccess-accessible F1 reg space register offsets */
#define SDA_CHIPCONTROLDATA	0x006	/* ChipControlData */
#define SDA_CHIPCONTROLENAB	0x007	/* ChipControlEnable */
#define SDA_F2WATERMARK		0x008	/* Function 2 Watermark */
#define SDA_DEVICECONTROL	0x009	/* DeviceControl */
#define SDA_SBADDRLOW		0x00a	/* SbAddrLow */
#define SDA_SBADDRMID		0x00b	/* SbAddrMid */
#define SDA_SBADDRHIGH		0x00c	/* SbAddrHigh */
#define SDA_FRAMECTRL		0x00d	/* FrameCtrl */
#define SDA_CHIPCLOCKCSR	0x00e	/* ChipClockCSR */
#define SDA_SDIOPULLUP		0x00f	/* SdioPullUp */
#define SDA_SDIOWRFRAMEBCLOW	0x019	/* SdioWrFrameBCLow */
#define SDA_SDIOWRFRAMEBCHIGH	0x01a	/* SdioWrFrameBCHigh */
#define SDA_SDIORDFRAMEBCLOW	0x01b	/* SdioRdFrameBCLow */
#define SDA_SDIORDFRAMEBCHIGH	0x01c	/* SdioRdFrameBCHigh */

/* SDA_F2WATERMARK */
#define SDA_F2WATERMARK_MASK	0x7f	/* F2Watermark Mask */

/* SDA_SBADDRLOW */
#define SDA_SBADDRLOW_MASK	0x80	/* SbAddrLow Mask */

/* SDA_SBADDRMID */
#define SDA_SBADDRMID_MASK	0xff	/* SbAddrMid Mask */

/* SDA_SBADDRHIGH */
#define SDA_SBADDRHIGH_MASK	0xff	/* SbAddrHigh Mask */

/* SDA_FRAMECTRL */
#define SFC_RF_TERM	(1 << 0)	/* Read Frame Terminate */
#define SFC_WF_TERM	(1 << 1)	/* Write Frame Terminate */
#define SFC_CRC4WOOS	(1 << 2)	/* HW reports CRC error for write out of sync */
#define SFC_ABORTALL	(1 << 3)	/* Abort cancels all in-progress frames */

/* pcmciaframectrl */
#define PFC_RF_TERM	(1 << 0)	/* Read Frame Terminate */
#define PFC_WF_TERM	(1 << 1)	/* Write Frame Terminate */

/* intrcvlazy */
#define	IRL_TO_MASK	0x00ffffff	/* timeout */
#define	IRL_FC_MASK	0xff000000	/* frame count */
#define	IRL_FC_SHIFT	24		/* frame count */

/* rx header */
typedef volatile struct {
	uint16 len;
	uint16 flags;
} sdpcmd_rxh_t;

/* rx header flags */
#define RXF_CRC		0x0001		/* CRC error detected */
#define RXF_WOOS	0x0002		/* write frame out of sync */
#define RXF_WF_TERM	0x0004		/* write frame terminated */
#define RXF_ABORT	0x0008		/* write frame aborted */
#define RXF_DISCARD	(RXF_CRC | RXF_WOOS | RXF_WF_TERM | RXF_ABORT)	/* bad frame */

/* HW frame tag */
#define SDPCM_FRAMETAG_LEN	4	/* HW frametag: 2 bytes len, 2 bytes check val */

#define SDPCM_HWEXT_LEN	8

#endif	/* _sbsdpcmdev_h_ */
