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
    defined(CONFIG_M520x)
/*
 *	Just figures, Motorola would have to change the offsets for
 *	registers in the same peripheral device on different models
 *	of the ColdFire!
 */
typedef struct fec {
	unsigned long	fec_reserved0;
	unsigned long	fec_ievent;		/* Interrupt event reg */
	unsigned long	fec_imask;		/* Interrupt mask reg */
	unsigned long	fec_reserved1;
	unsigned long	fec_r_des_active;	/* Receive descriptor reg */
	unsigned long	fec_x_des_active;	/* Transmit descriptor reg */
	unsigned long	fec_reserved2[3];
	unsigned long	fec_ecntrl;		/* Ethernet control reg */
	unsigned long	fec_reserved3[6];
	unsigned long	fec_mii_data;		/* MII manage frame reg */
	unsigned long	fec_mii_speed;		/* MII speed control reg */
	unsigned long	fec_reserved4[7];
	unsigned long	fec_mib_ctrlstat;	/* MIB control/status reg */
	unsigned long	fec_reserved5[7];
	unsigned long	fec_r_cntrl;		/* Receive control reg */
	unsigned long	fec_reserved6[15];
	unsigned long	fec_x_cntrl;		/* Transmit Control reg */
	unsigned long	fec_reserved7[7];
	unsigned long	fec_addr_low;		/* Low 32bits MAC address */
	unsigned long	fec_addr_high;		/* High 16bits MAC address */
	unsigned long	fec_opd;		/* Opcode + Pause duration */
	unsigned long	fec_reserved8[10];
	unsigned long	fec_hash_table_high;	/* High 32bits hash table */
	unsigned long	fec_hash_table_low;	/* Low 32bits hash table */
	unsigned long	fec_grp_hash_table_high;/* High 32bits hash table */
	unsigned long	fec_grp_hash_table_low;	/* Low 32bits hash table */
	unsigned long	fec_reserved9[7];
	unsigned long	fec_x_wmrk;		/* FIFO transmit water mark */
	unsigned long	fec_reserved10;
	unsigned long	fec_r_bound;		/* FIFO receive bound reg */
	unsigned long	fec_r_fstart;		/* FIFO receive start reg */
	unsigned long	fec_reserved11[11];
	unsigned long	fec_r_des_start;	/* Receive descriptor ring */
	unsigned long	fec_x_des_start;	/* Transmit descriptor ring */
	unsigned long	fec_r_buff_size;	/* Maximum receive buff size */
} fec_t;

#else

/*
 *	Define device register set address map.
 */
typedef struct fec {
	unsigned long	fec_ecntrl;		/* Ethernet control reg */
	unsigned long	fec_ievent;		/* Interrupt even reg */
	unsigned long	fec_imask;		/* Interrupt mask reg */
	unsigned long	fec_ivec;		/* Interrupt vec status reg */
	unsigned long	fec_r_des_active;	/* Receive descriptor reg */
	unsigned long	fec_x_des_active;	/* Transmit descriptor reg */
	unsigned long	fec_reserved1[10];
	unsigned long	fec_mii_data;		/* MII manage frame reg */
	unsigned long	fec_mii_speed;		/* MII speed control reg */
	unsigned long	fec_reserved2[17];
	unsigned long	fec_r_bound;		/* FIFO receive bound reg */
	unsigned long	fec_r_fstart;		/* FIFO receive start reg */
	unsigned long	fec_reserved3[4];
	unsigned long	fec_x_wmrk;		/* FIFO transmit water mark */
	unsigned long	fec_reserved4;
	unsigned long	fec_x_fstart;		/* FIFO transmit start reg */
	unsigned long	fec_reserved5[21];
	unsigned long	fec_r_cntrl;		/* Receive control reg */
	unsigned long	fec_max_frm_len;	/* Maximum frame length reg */
	unsigned long	fec_reserved6[14];
	unsigned long	fec_x_cntrl;		/* Transmit Control reg */
	unsigned long	fec_reserved7[158];
	unsigned long	fec_addr_low;		/* Low 32bits MAC address */
	unsigned long	fec_addr_high;		/* High 16bits MAC address */
	unsigned long	fec_hash_table_high;	/* High 32bits hash table */
	unsigned long	fec_hash_table_low;	/* Low 32bits hash table */
	unsigned long	fec_r_des_start;	/* Receive descriptor ring */
	unsigned long	fec_x_des_start;	/* Transmit descriptor ring */
	unsigned long	fec_r_buff_size;	/* Maximum receive buff size */
	unsigned long	reserved8[9];
	unsigned long	fec_fifo_ram[112];	/* FIFO RAM buffer */
} fec_t;

#endif /* CONFIG_M5272 */


/*
 *	Define the buffer descriptor structure.
 */
typedef struct bufdesc {
	unsigned short	cbd_sc;			/* Control and status info */
	unsigned short	cbd_datlen;		/* Data length */
	unsigned long	cbd_bufaddr;		/* Buffer address */
} cbd_t;


/*
 *	The following definitions courtesy of commproc.h, which where
 *	Copyright (c) 1997 Dan Malek (dmalek@jlc.net).
 */
#define BD_SC_EMPTY     ((ushort)0x8000)        /* Recieve is empty */
#define BD_SC_READY     ((ushort)0x8000)        /* Transmit is ready */
#define BD_SC_WRAP      ((ushort)0x2000)        /* Last buffer descriptor */
#define BD_SC_INTRPT    ((ushort)0x1000)        /* Interrupt on change */
#define BD_SC_CM        ((ushort)0x0200)        /* Continous mode */
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
