/*
 * dm9000 Ethernet
 */

#ifndef _DM9000X_H_
#define _DM9000X_H_

#define DM9000_ID		0x90000A46

/* although the registers are 16 bit, they are 32-bit aligned.
 */

#define DM9000_NCR             0x00
#define DM9000_NSR             0x01
#define DM9000_TCR             0x02
#define DM9000_TSR1            0x03
#define DM9000_TSR2            0x04
#define DM9000_RCR             0x05
#define DM9000_RSR             0x06
#define DM9000_ROCR            0x07
#define DM9000_BPTR            0x08
#define DM9000_FCTR            0x09
#define DM9000_FCR             0x0A
#define DM9000_EPCR            0x0B
#define DM9000_EPAR            0x0C
#define DM9000_EPDRL           0x0D
#define DM9000_EPDRH           0x0E
#define DM9000_WCR             0x0F

#define DM9000_PAR             0x10
#define DM9000_MAR             0x16

#define DM9000_GPCR	       0x1e
#define DM9000_GPR             0x1f
#define DM9000_TRPAL           0x22
#define DM9000_TRPAH           0x23
#define DM9000_RWPAL           0x24
#define DM9000_RWPAH           0x25

#define DM9000_VIDL            0x28
#define DM9000_VIDH            0x29
#define DM9000_PIDL            0x2A
#define DM9000_PIDH            0x2B

#define DM9000_CHIPR           0x2C
#define DM9000_SMCR            0x2F

#define DM9000_ETXCSR          0x30
#define DM9000_TCCR	       0x31
#define DM9000_RCSR	       0x32

#define CHIPR_DM9000A	       0x19
#define CHIPR_DM9000B	       0x1A

#define DM9000_MRCMDX          0xF0
#define DM9000_MRCMD           0xF2
#define DM9000_MRRL            0xF4
#define DM9000_MRRH            0xF5
#define DM9000_MWCMDX          0xF6
#define DM9000_MWCMD           0xF8
#define DM9000_MWRL            0xFA
#define DM9000_MWRH            0xFB
#define DM9000_TXPLL           0xFC
#define DM9000_TXPLH           0xFD
#define DM9000_ISR             0xFE
#define DM9000_IMR             0xFF

#define NCR_EXT_PHY         (1<<7)
#define NCR_WAKEEN          (1<<6)
#define NCR_FCOL            (1<<4)
#define NCR_FDX             (1<<3)

#define NCR_RESERVED        (3<<1)
#define NCR_MAC_LBK         (1<<1)
#define NCR_RST	            (1<<0)

#define NSR_SPEED           (1<<7)
#define NSR_LINKST          (1<<6)
#define NSR_WAKEST          (1<<5)
#define NSR_TX2END          (1<<3)
#define NSR_TX1END          (1<<2)
#define NSR_RXOV            (1<<1)

#define TCR_TJDIS           (1<<6)
#define TCR_EXCECM          (1<<5)
#define TCR_PAD_DIS2        (1<<4)
#define TCR_CRC_DIS2        (1<<3)
#define TCR_PAD_DIS1        (1<<2)
#define TCR_CRC_DIS1        (1<<1)
#define TCR_TXREQ           (1<<0)

#define TSR_TJTO            (1<<7)
#define TSR_LC              (1<<6)
#define TSR_NC              (1<<5)
#define TSR_LCOL            (1<<4)
#define TSR_COL             (1<<3)
#define TSR_EC              (1<<2)

#define RCR_WTDIS           (1<<6)
#define RCR_DIS_LONG        (1<<5)
#define RCR_DIS_CRC         (1<<4)
#define RCR_ALL	            (1<<3)
#define RCR_RUNT            (1<<2)
#define RCR_PRMSC           (1<<1)
#define RCR_RXEN            (1<<0)

#define RSR_RF              (1<<7)
#define RSR_MF              (1<<6)
#define RSR_LCS             (1<<5)
#define RSR_RWTO            (1<<4)
#define RSR_PLE             (1<<3)
#define RSR_AE              (1<<2)
#define RSR_CE              (1<<1)
#define RSR_FOE             (1<<0)

#define WCR_LINKEN		(1 << 5)
#define WCR_SAMPLEEN		(1 << 4)
#define WCR_MAGICEN		(1 << 3)
#define WCR_LINKST		(1 << 2)
#define WCR_SAMPLEST		(1 << 1)
#define WCR_MAGICST		(1 << 0)

#define FCTR_HWOT(ot)	(( ot & 0xf ) << 4 )
#define FCTR_LWOT(ot)	( ot & 0xf )

#define IMR_PAR             (1<<7)
#define IMR_ROOM            (1<<3)
#define IMR_ROM             (1<<2)
#define IMR_PTM             (1<<1)
#define IMR_PRM             (1<<0)

#define ISR_ROOS            (1<<3)
#define ISR_ROS             (1<<2)
#define ISR_PTS             (1<<1)
#define ISR_PRS             (1<<0)
#define ISR_CLR_STATUS      (ISR_ROOS | ISR_ROS | ISR_PTS | ISR_PRS)

#define EPCR_REEP           (1<<5)
#define EPCR_WEP            (1<<4)
#define EPCR_EPOS           (1<<3)
#define EPCR_ERPRR          (1<<2)
#define EPCR_ERPRW          (1<<1)
#define EPCR_ERRE           (1<<0)

#define GPCR_GEP_CNTL       (1<<0)

#define TCCR_IP		    (1<<0)
#define TCCR_TCP	    (1<<1)
#define TCCR_UDP	    (1<<2)

#define RCSR_UDP_BAD	    (1<<7)
#define RCSR_TCP_BAD	    (1<<6)
#define RCSR_IP_BAD	    (1<<5)
#define RCSR_UDP	    (1<<4)
#define RCSR_TCP	    (1<<3)
#define RCSR_IP		    (1<<2)
#define RCSR_CSUM	    (1<<1)
#define RCSR_DISCARD	    (1<<0)

#define DM9000_PKT_RDY		0x01	/* Packet ready to receive */
#define DM9000_PKT_ERR		0x02
#define DM9000_PKT_MAX		1536	/* Received packet max size */

/* DM9000A / DM9000B definitions */

#define IMR_LNKCHNG		(1<<5)
#define IMR_UNDERRUN		(1<<4)

#define ISR_LNKCHNG		(1<<5)
#define ISR_UNDERRUN		(1<<4)

/* Davicom MII registers.
 */

#define MII_DM_DSPCR		0x1b    /* DSP Control Register */

#define DSPCR_INIT_PARAM	0xE100	/* DSP init parameter */

#endif /* _DM9000X_H_ */

