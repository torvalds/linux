/****************************************************************************/

/*
 *	fec.h  --  Fast Ethernet Controller for Motorola ColdFire SoC
 *		   processors.
 *
 *	(C) Copyright 2000-2005, Greg Ungerer (gerg@snapgear.com)
 *	(C) Copyright 2000-2001, Lineo (www.lineo.com)
 */

/****************************************************************************/
#ifndef FEC_H
#define	FEC_H
/****************************************************************************/

#if defined(CONFIG_M523x) || defined(CONFIG_M527x) || defined(CONFIG_M528x) || \
    defined(CONFIG_M520x) || defined(CONFIG_M532x) || \
    defined(CONFIG_ARCH_MXC) || defined(CONFIG_SOC_IMX28)
/*
 *	Just figures, Motorola would have to change the offsets for
 *	registers in the same peripheral device on different models
 *	of the ColdFire!
 */
#define FEC_IEVENT		0x004 /* Interrupt event reg */
#define FEC_IMASK		0x008 /* Interrupt mask reg */
#define FEC_R_DES_ACTIVE	0x010 /* Receive descriptor reg */
#define FEC_X_DES_ACTIVE	0x014 /* Transmit descriptor reg */
#define FEC_ECNTRL		0x024 /* Ethernet control reg */
#define FEC_MII_DATA		0x040 /* MII manage frame reg */
#define FEC_MII_SPEED		0x044 /* MII speed control reg */
#define FEC_MIB_CTRLSTAT	0x064 /* MIB control/status reg */
#define FEC_R_CNTRL		0x084 /* Receive control reg */
#define FEC_X_CNTRL		0x0c4 /* Transmit Control reg */
#define FEC_ADDR_LOW		0x0e4 /* Low 32bits MAC address */
#define FEC_ADDR_HIGH		0x0e8 /* High 16bits MAC address */
#define FEC_OPD			0x0ec /* Opcode + Pause duration */
#define FEC_HASH_TABLE_HIGH	0x118 /* High 32bits hash table */
#define FEC_HASH_TABLE_LOW	0x11c /* Low 32bits hash table */
#define FEC_GRP_HASH_TABLE_HIGH	0x120 /* High 32bits hash table */
#define FEC_GRP_HASH_TABLE_LOW	0x124 /* Low 32bits hash table */
#define FEC_X_WMRK		0x144 /* FIFO transmit water mark */
#define FEC_R_BOUND		0x14c /* FIFO receive bound reg */
#define FEC_R_FSTART		0x150 /* FIFO receive start reg */
#define FEC_R_DES_START		0x180 /* Receive descriptor ring */
#define FEC_X_DES_START		0x184 /* Transmit descriptor ring */
#define FEC_R_BUFF_SIZE		0x188 /* Maximum receive buff size */
#define FEC_MIIGSK_CFGR		0x300 /* MIIGSK Configuration reg */
#define FEC_MIIGSK_ENR		0x308 /* MIIGSK Enable reg */

#else

#define FEC_ECNTRL		0x000 /* Ethernet control reg */
#define FEC_IEVENT		0x004 /* Interrupt even reg */
#define FEC_IMASK		0x008 /* Interrupt mask reg */
#define FEC_IVEC		0x00c /* Interrupt vec status reg */
#define FEC_R_DES_ACTIVE	0x010 /* Receive descriptor reg */
#define FEC_X_DES_ACTIVE	0x014 /* Transmit descriptor reg */
#define FEC_MII_DATA		0x040 /* MII manage frame reg */
#define FEC_MII_SPEED		0x044 /* MII speed control reg */
#define FEC_R_BOUND		0x08c /* FIFO receive bound reg */
#define FEC_R_FSTART		0x090 /* FIFO receive start reg */
#define FEC_X_WMRK		0x0a4 /* FIFO transmit water mark */
#define FEC_X_FSTART		0x0ac /* FIFO transmit start reg */
#define FEC_R_CNTRL		0x104 /* Receive control reg */
#define FEC_MAX_FRM_LEN		0x108 /* Maximum frame length reg */
#define FEC_X_CNTRL		0x144 /* Transmit Control reg */
#define FEC_ADDR_LOW		0x3c0 /* Low 32bits MAC address */
#define FEC_ADDR_HIGH		0x3c4 /* High 16bits MAC address */
#define FEC_GRP_HASH_TABLE_HIGH	0x3c8 /* High 32bits hash table */
#define FEC_GRP_HASH_TABLE_LOW	0x3cc /* Low 32bits hash table */
#define FEC_R_DES_START		0x3d0 /* Receive descriptor ring */
#define FEC_X_DES_START		0x3d4 /* Transmit descriptor ring */
#define FEC_R_BUFF_SIZE		0x3d8 /* Maximum receive buff size */
#define FEC_FIFO_RAM		0x400 /* FIFO RAM buffer */

#endif /* CONFIG_M5272 */


/*
 *	Define the buffer descriptor structure.
 */
#if defined(CONFIG_ARCH_MXC) || defined(CONFIG_SOC_IMX28)
struct bufdesc {
	unsigned short cbd_datlen;	/* Data length */
	unsigned short cbd_sc;	/* Control and status info */
	unsigned long cbd_bufaddr;	/* Buffer address */
};
#else
struct bufdesc {
	unsigned short	cbd_sc;			/* Control and status info */
	unsigned short	cbd_datlen;		/* Data length */
	unsigned long	cbd_bufaddr;		/* Buffer address */
};
#endif

/*
 *	The following definitions courtesy of commproc.h, which where
 *	Copyright (c) 1997 Dan Malek (dmalek@jlc.net).
 */
#define BD_SC_EMPTY     ((ushort)0x8000)        /* Receive is empty */
#define BD_SC_READY     ((ushort)0x8000)        /* Transmit is ready */
#define BD_SC_WRAP      ((ushort)0x2000)        /* Last buffer descriptor */
#define BD_SC_INTRPT    ((ushort)0x1000)        /* Interrupt on change */
#define BD_SC_CM        ((ushort)0x0200)        /* Continuous mode */
#define BD_SC_ID        ((ushort)0x0100)        /* Rec'd too many idles */
#define BD_SC_P         ((ushort)0x0100)        /* xmt preamble */
#define BD_SC_BR        ((ushort)0x0020)        /* Break received */
#define BD_SC_FR        ((ushort)0x0010)        /* Framing error */
#define BD_SC_PR        ((ushort)0x0008)        /* Parity error */
#define BD_SC_OV        ((ushort)0x0002)        /* Overrun */
#define BD_SC_CD        ((ushort)0x0001)        /* ?? */

/* Buffer descriptor control/status used by Ethernet receive.
*/
#define BD_ENET_RX_EMPTY        ((ushort)0x8000)
#define BD_ENET_RX_WRAP         ((ushort)0x2000)
#define BD_ENET_RX_INTR         ((ushort)0x1000)
#define BD_ENET_RX_LAST         ((ushort)0x0800)
#define BD_ENET_RX_FIRST        ((ushort)0x0400)
#define BD_ENET_RX_MISS         ((ushort)0x0100)
#define BD_ENET_RX_LG           ((ushort)0x0020)
#define BD_ENET_RX_NO           ((ushort)0x0010)
#define BD_ENET_RX_SH           ((ushort)0x0008)
#define BD_ENET_RX_CR           ((ushort)0x0004)
#define BD_ENET_RX_OV           ((ushort)0x0002)
#define BD_ENET_RX_CL           ((ushort)0x0001)
#define BD_ENET_RX_STATS        ((ushort)0x013f)        /* All status bits */

/* Buffer descriptor control/status used by Ethernet transmit.
*/
#define BD_ENET_TX_READY        ((ushort)0x8000)
#define BD_ENET_TX_PAD          ((ushort)0x4000)
#define BD_ENET_TX_WRAP         ((ushort)0x2000)
#define BD_ENET_TX_INTR         ((ushort)0x1000)
#define BD_ENET_TX_LAST         ((ushort)0x0800)
#define BD_ENET_TX_TC           ((ushort)0x0400)
#define BD_ENET_TX_DEF          ((ushort)0x0200)
#define BD_ENET_TX_HB           ((ushort)0x0100)
#define BD_ENET_TX_LC           ((ushort)0x0080)
#define BD_ENET_TX_RL           ((ushort)0x0040)
#define BD_ENET_TX_RCMASK       ((ushort)0x003c)
#define BD_ENET_TX_UN           ((ushort)0x0002)
#define BD_ENET_TX_CSL          ((ushort)0x0001)
#define BD_ENET_TX_STATS        ((ushort)0x03ff)        /* All status bits */


/****************************************************************************/
#endif /* FEC_H */
