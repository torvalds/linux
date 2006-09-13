/*
    Written 1994 by David C. Davies.

    Copyright 1994 David C. Davies. This software may be used and distributed
    according to the terms of the GNU General Public License, incorporated herein by
    reference.
*/

/*
** I/O addresses. Note that the 2k buffer option is not supported in
** this driver.
*/
#define DEPCA_NICSR ioaddr+0x00   /* Network interface CSR */
#define DEPCA_RBI   ioaddr+0x02   /* RAM buffer index (2k buffer mode) */
#define DEPCA_DATA  ioaddr+0x04   /* LANCE registers' data port */
#define DEPCA_ADDR  ioaddr+0x06   /* LANCE registers' address port */
#define DEPCA_HBASE ioaddr+0x08   /* EISA high memory base address reg. */
#define DEPCA_PROM  ioaddr+0x0c   /* Ethernet address ROM data port */
#define DEPCA_CNFG  ioaddr+0x0c   /* EISA Configuration port */
#define DEPCA_RBSA  ioaddr+0x0e   /* RAM buffer starting address (2k buff.) */

/*
** These are LANCE registers addressable through DEPCA_ADDR
*/
#define CSR0       0
#define CSR1       1
#define CSR2       2
#define CSR3       3

/*
** NETWORK INTERFACE CSR (NI_CSR) bit definitions
*/

#define TO       	0x0100	/* Time Out for remote boot */
#define SHE      	0x0080  /* SHadow memory Enable */
#define BS       	0x0040  /* Bank Select */
#define BUF      	0x0020	/* BUFfer size (1->32k, 0->64k) */
#define RBE      	0x0010	/* Remote Boot Enable (1->net boot) */
#define AAC      	0x0008  /* Address ROM Address Counter (1->enable) */
#define _128KB      	0x0008  /* 128kB Network RAM (1->enable) */
#define IM       	0x0004	/* Interrupt Mask (1->mask) */
#define IEN      	0x0002	/* Interrupt tristate ENable (1->enable) */
#define LED      	0x0001	/* LED control */

/*
** Control and Status Register 0 (CSR0) bit definitions
*/

#define ERR     	0x8000 	/* Error summary */
#define BABL    	0x4000 	/* Babble transmitter timeout error  */
#define CERR    	0x2000 	/* Collision Error */
#define MISS    	0x1000 	/* Missed packet */
#define MERR    	0x0800 	/* Memory Error */
#define RINT    	0x0400 	/* Receiver Interrupt */
#define TINT    	0x0200 	/* Transmit Interrupt */
#define IDON    	0x0100 	/* Initialization Done */
#define INTR    	0x0080 	/* Interrupt Flag */
#define INEA    	0x0040 	/* Interrupt Enable */
#define RXON    	0x0020 	/* Receiver on */
#define TXON    	0x0010 	/* Transmitter on */
#define TDMD    	0x0008 	/* Transmit Demand */
#define STOP    	0x0004 	/* Stop */
#define STRT    	0x0002 	/* Start */
#define INIT    	0x0001 	/* Initialize */
#define INTM            0xff00  /* Interrupt Mask */
#define INTE            0xfff0  /* Interrupt Enable */

/*
** CONTROL AND STATUS REGISTER 3 (CSR3)
*/

#define BSWP    	0x0004	/* Byte SWaP */
#define ACON    	0x0002	/* ALE control */
#define BCON    	0x0001	/* Byte CONtrol */

/*
** Initialization Block Mode Register
*/

#define PROM       	0x8000 	/* Promiscuous Mode */
#define EMBA       	0x0080	/* Enable Modified Back-off Algorithm */
#define INTL       	0x0040 	/* Internal Loopback */
#define DRTY       	0x0020 	/* Disable Retry */
#define COLL       	0x0010 	/* Force Collision */
#define DTCR       	0x0008 	/* Disable Transmit CRC */
#define LOOP       	0x0004 	/* Loopback */
#define DTX        	0x0002 	/* Disable the Transmitter */
#define DRX        	0x0001 	/* Disable the Receiver */

/*
** Receive Message Descriptor 1 (RMD1) bit definitions.
*/

#define R_OWN       0x80000000 	/* Owner bit 0 = host, 1 = lance */
#define R_ERR     	0x4000 	/* Error Summary */
#define R_FRAM    	0x2000 	/* Framing Error */
#define R_OFLO    	0x1000 	/* Overflow Error */
#define R_CRC     	0x0800 	/* CRC Error */
#define R_BUFF    	0x0400 	/* Buffer Error */
#define R_STP     	0x0200 	/* Start of Packet */
#define R_ENP     	0x0100 	/* End of Packet */

/*
** Transmit Message Descriptor 1 (TMD1) bit definitions.
*/

#define T_OWN       0x80000000 	/* Owner bit 0 = host, 1 = lance */
#define T_ERR     	0x4000 	/* Error Summary */
#define T_ADD_FCS 	0x2000 	/* More the 1 retry needed to Xmit */
#define T_MORE    	0x1000	/* >1 retry to transmit packet */
#define T_ONE     	0x0800 	/* 1 try needed to transmit the packet */
#define T_DEF     	0x0400 	/* Deferred */
#define T_STP       0x02000000 	/* Start of Packet */
#define T_ENP       0x01000000	/* End of Packet */
#define T_FLAGS     0xff000000  /* TX Flags Field */

/*
** Transmit Message Descriptor 3 (TMD3) bit definitions.
*/

#define TMD3_BUFF    0x8000	/* BUFFer error */
#define TMD3_UFLO    0x4000	/* UnderFLOw error */
#define TMD3_RES     0x2000	/* REServed */
#define TMD3_LCOL    0x1000	/* Late COLlision */
#define TMD3_LCAR    0x0800	/* Loss of CARrier */
#define TMD3_RTRY    0x0400	/* ReTRY error */

/*
** EISA configuration Register (CNFG) bit definitions
*/

#define TIMEOUT       	0x0100	/* 0:2.5 mins, 1: 30 secs */
#define REMOTE      	0x0080  /* Remote Boot Enable -> 1 */
#define IRQ11       	0x0040  /* Enable -> 1 */
#define IRQ10    	0x0020	/* Enable -> 1 */
#define IRQ9    	0x0010	/* Enable -> 1 */
#define IRQ5      	0x0008  /* Enable -> 1 */
#define BUFF     	0x0004	/* 0: 64kB or 128kB, 1: 32kB */
#define PADR16   	0x0002	/* RAM on 64kB boundary */
#define PADR17    	0x0001	/* RAM on 128kB boundary */

/*
** Miscellaneous
*/
#define HASH_TABLE_LEN   64           /* Bits */
#define HASH_BITS        0x003f       /* 6 LS bits */

#define MASK_INTERRUPTS   1
#define UNMASK_INTERRUPTS 0

#define EISA_EN         0x0001        /* Enable EISA bus buffers */
#define EISA_ID         iobase+0x0080 /* ID long word for EISA card */
#define EISA_CTRL       iobase+0x0084 /* Control word for EISA card */

/*
** Include the IOCTL stuff
*/
#include <linux/sockios.h>

#define	DEPCAIOCTL	SIOCDEVPRIVATE

struct depca_ioctl {
	unsigned short cmd;                /* Command to run */
	unsigned short len;                /* Length of the data buffer */
	unsigned char  __user *data;       /* Pointer to the data buffer */
};

/*
** Recognised commands for the driver
*/
#define DEPCA_GET_HWADDR	0x01 /* Get the hardware address */
#define DEPCA_SET_HWADDR	0x02 /* Get the hardware address */
#define DEPCA_SET_PROM  	0x03 /* Set Promiscuous Mode */
#define DEPCA_CLR_PROM  	0x04 /* Clear Promiscuous Mode */
#define DEPCA_SAY_BOO	        0x05 /* Say "Boo!" to the kernel log file */
#define DEPCA_GET_MCA   	0x06 /* Get a multicast address */
#define DEPCA_SET_MCA   	0x07 /* Set a multicast address */
#define DEPCA_CLR_MCA    	0x08 /* Clear a multicast address */
#define DEPCA_MCA_EN    	0x09 /* Enable a multicast address group */
#define DEPCA_GET_STATS  	0x0a /* Get the driver statistics */
#define DEPCA_CLR_STATS 	0x0b /* Zero out the driver statistics */
#define DEPCA_GET_REG   	0x0c /* Get the Register contents */
#define DEPCA_SET_REG   	0x0d /* Set the Register contents */
#define DEPCA_DUMP              0x0f /* Dump the DEPCA Status */

