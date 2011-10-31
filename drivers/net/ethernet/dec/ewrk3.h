/*
    Written 1994 by David C. Davies.

    Copyright 1994 Digital Equipment Corporation.

    This software may be used and distributed according to  the terms of the
    GNU General Public License, incorporated herein by reference.

    The author may    be  reached as davies@wanton.lkg.dec.com  or   Digital
    Equipment Corporation, 550 King Street, Littleton MA 01460.

    =========================================================================
*/

/*
** I/O Address Register Map
*/
#define EWRK3_CSR    iobase+0x00   /* Control and Status Register */
#define EWRK3_CR     iobase+0x01   /* Control Register */
#define EWRK3_ICR    iobase+0x02   /* Interrupt Control Register */
#define EWRK3_TSR    iobase+0x03   /* Transmit Status Register */
#define EWRK3_RSVD1  iobase+0x04   /* RESERVED */
#define EWRK3_RSVD2  iobase+0x05   /* RESERVED */
#define EWRK3_FMQ    iobase+0x06   /* Free Memory Queue */
#define EWRK3_FMQC   iobase+0x07   /* Free Memory Queue Counter */
#define EWRK3_RQ     iobase+0x08   /* Receive Queue */
#define EWRK3_RQC    iobase+0x09   /* Receive Queue Counter */
#define EWRK3_TQ     iobase+0x0a   /* Transmit Queue */
#define EWRK3_TQC    iobase+0x0b   /* Transmit Queue Counter */
#define EWRK3_TDQ    iobase+0x0c   /* Transmit Done Queue */
#define EWRK3_TDQC   iobase+0x0d   /* Transmit Done Queue Counter */
#define EWRK3_PIR1   iobase+0x0e   /* Page Index Register 1 */
#define EWRK3_PIR2   iobase+0x0f   /* Page Index Register 2 */
#define EWRK3_DATA   iobase+0x10   /* Data Register */
#define EWRK3_IOPR   iobase+0x11   /* I/O Page Register */
#define EWRK3_IOBR   iobase+0x12   /* I/O Base Register */
#define EWRK3_MPR    iobase+0x13   /* Memory Page Register */
#define EWRK3_MBR    iobase+0x14   /* Memory Base Register */
#define EWRK3_APROM  iobase+0x15   /* Address PROM */
#define EWRK3_EPROM1 iobase+0x16   /* EEPROM Data Register 1 */
#define EWRK3_EPROM2 iobase+0x17   /* EEPROM Data Register 2 */
#define EWRK3_PAR0   iobase+0x18   /* Physical Address Register 0 */
#define EWRK3_PAR1   iobase+0x19   /* Physical Address Register 1 */
#define EWRK3_PAR2   iobase+0x1a   /* Physical Address Register 2 */
#define EWRK3_PAR3   iobase+0x1b   /* Physical Address Register 3 */
#define EWRK3_PAR4   iobase+0x1c   /* Physical Address Register 4 */
#define EWRK3_PAR5   iobase+0x1d   /* Physical Address Register 5 */
#define EWRK3_CMR    iobase+0x1e   /* Configuration/Management Register */

/*
** Control Page Map
*/
#define PAGE0_FMQ     0x000         /* Free Memory Queue */
#define PAGE0_RQ      0x080         /* Receive Queue */
#define PAGE0_TQ      0x100         /* Transmit Queue */
#define PAGE0_TDQ     0x180         /* Transmit Done Queue */
#define PAGE0_HTE     0x200         /* Hash Table Entries */
#define PAGE0_RSVD    0x240         /* RESERVED */
#define PAGE0_USRD    0x600         /* User Data */

/*
** Control and Status Register bit definitions (EWRK3_CSR)
*/
#define CSR_RA		0x80	    /* Runt Accept */
#define CSR_PME		0x40	    /* Promiscuous Mode Enable */
#define CSR_MCE		0x20	    /* Multicast Enable */
#define CSR_TNE		0x08	    /* TX Done Queue Not Empty */
#define CSR_RNE		0x04	    /* RX Queue Not Empty */
#define CSR_TXD		0x02	    /* TX Disable */
#define CSR_RXD		0x01	    /* RX Disable */

/*
** Control Register bit definitions (EWRK3_CR)
*/
#define CR_APD		0x80	/* Auto Port Disable */
#define CR_PSEL		0x40	/* Port Select (0->TP port) */
#define CR_LBCK		0x20	/* LoopBaCK enable */
#define CR_FDUP		0x10	/* Full DUPlex enable */
#define CR_FBUS		0x08	/* Fast BUS enable (ISA clk > 8.33MHz) */
#define CR_EN_16	0x04	/* ENable 16 bit memory accesses */
#define CR_LED		0x02	/* LED (1-> turn on) */

/*
** Interrupt Control Register bit definitions (EWRK3_ICR)
*/
#define ICR_IE		0x80	/* Interrupt Enable */
#define ICR_IS		0x60	/* Interrupt Selected */
#define ICR_TNEM	0x08	/* TNE Mask (0->mask) */
#define ICR_RNEM	0x04	/* RNE Mask (0->mask) */
#define ICR_TXDM	0x02	/* TXD Mask (0->mask) */
#define ICR_RXDM	0x01	/* RXD Mask (0->mask) */

/*
** Transmit Status Register bit definitions (EWRK3_TSR)
*/
#define TSR_NCL		0x80	/* No Carrier Loopback */
#define TSR_ID		0x40	/* Initially Deferred */
#define TSR_LCL		0x20	/* Late CoLlision */
#define TSR_ECL		0x10	/* Excessive CoLlisions */
#define TSR_RCNTR	0x0f	/* Retries CouNTeR */

/*
** I/O Page Register bit definitions (EWRK3_IOPR)
*/
#define EEPROM_INIT	0xc0	/* EEPROM INIT command */
#define EEPROM_WR_EN	0xc8	/* EEPROM WRITE ENABLE command */
#define EEPROM_WR	0xd0	/* EEPROM WRITE command */
#define EEPROM_WR_DIS	0xd8	/* EEPROM WRITE DISABLE command */
#define EEPROM_RD	0xe0	/* EEPROM READ command */

/*
** I/O Base Register bit definitions (EWRK3_IOBR)
*/
#define EISA_REGS_EN	0x20	/* Enable EISA ID and Control Registers */
#define EISA_IOB        0x1f	/* Compare bits for I/O Base Address */

/*
** I/O Configuration/Management Register bit definitions (EWRK3_CMR)
*/
#define CMR_RA          0x80    /* Read Ahead */
#define CMR_WB          0x40    /* Write Behind */
#define CMR_LINK        0x20	/* 0->TP */
#define CMR_POLARITY    0x10	/* Informational */
#define CMR_NO_EEPROM	0x0c	/* NO_EEPROM<1:0> pin status */
#define CMR_HS          0x08	/* Hard Strapped pin status (LeMAC2) */
#define CMR_PNP         0x04    /* Plug 'n Play */
#define CMR_DRAM        0x02	/* 0-> 1DRAM, 1-> 2 DRAM on board */
#define CMR_0WS         0x01    /* Zero Wait State */

/*
** MAC Receive Status Register bit definitions
*/

#define R_ROK     	0x80 	/* Receive OK summary */
#define R_IAM     	0x10 	/* Individual Address Match */
#define R_MCM     	0x08 	/* MultiCast Match */
#define R_DBE     	0x04 	/* Dribble Bit Error */
#define R_CRC     	0x02 	/* CRC error */
#define R_PLL     	0x01 	/* Phase Lock Lost */

/*
** MAC Transmit Control Register bit definitions
*/

#define TCR_SQEE    	0x40 	/* SQE Enable - look for heartbeat  */
#define TCR_SED     	0x20 	/* Stop when Error Detected */
#define TCR_QMODE     	0x10 	/* Q_MODE */
#define TCR_LAB         0x08 	/* Less Aggressive Backoff */
#define TCR_PAD     	0x04 	/* PAD Runt Packets */
#define TCR_IFC     	0x02 	/* Insert Frame Check */
#define TCR_ISA     	0x01 	/* Insert Source Address */

/*
** MAC Transmit Status Register bit definitions
*/

#define T_VSTS    	0x80 	/* Valid STatuS */
#define T_CTU     	0x40 	/* Cut Through Used */
#define T_SQE     	0x20 	/* Signal Quality Error */
#define T_NCL     	0x10 	/* No Carrier Loopback */
#define T_LCL           0x08 	/* Late Collision */
#define T_ID      	0x04 	/* Initially Deferred */
#define T_COLL     	0x03 	/* COLLision status */
#define T_XCOLL         0x03    /* Excessive Collisions */
#define T_MCOLL         0x02    /* Multiple Collisions */
#define T_OCOLL         0x01    /* One Collision */
#define T_NOCOLL        0x00    /* No Collisions */
#define T_XUR           0x03    /* Excessive Underruns */
#define T_TXE           0x7f    /* TX Errors */

/*
** EISA Configuration Register bit definitions
*/

#define EISA_ID       iobase + 0x0c80  /* EISA ID Registers */
#define EISA_ID0      iobase + 0x0c80  /* EISA ID Register 0 */
#define EISA_ID1      iobase + 0x0c81  /* EISA ID Register 1 */
#define EISA_ID2      iobase + 0x0c82  /* EISA ID Register 2 */
#define EISA_ID3      iobase + 0x0c83  /* EISA ID Register 3 */
#define EISA_CR       iobase + 0x0c84  /* EISA Control Register */

/*
** EEPROM BYTES
*/
#define EEPROM_MEMB     0x00
#define EEPROM_IOB      0x01
#define EEPROM_EISA_ID0 0x02
#define EEPROM_EISA_ID1 0x03
#define EEPROM_EISA_ID2 0x04
#define EEPROM_EISA_ID3 0x05
#define EEPROM_MISC0    0x06
#define EEPROM_MISC1    0x07
#define EEPROM_PNAME7   0x08
#define EEPROM_PNAME6   0x09
#define EEPROM_PNAME5   0x0a
#define EEPROM_PNAME4   0x0b
#define EEPROM_PNAME3   0x0c
#define EEPROM_PNAME2   0x0d
#define EEPROM_PNAME1   0x0e
#define EEPROM_PNAME0   0x0f
#define EEPROM_SWFLAGS  0x10
#define EEPROM_HWCAT    0x11
#define EEPROM_NETMAN2  0x12
#define EEPROM_REVLVL   0x13
#define EEPROM_NETMAN0  0x14
#define EEPROM_NETMAN1  0x15
#define EEPROM_CHIPVER  0x16
#define EEPROM_SETUP    0x17
#define EEPROM_PADDR0   0x18
#define EEPROM_PADDR1   0x19
#define EEPROM_PADDR2   0x1a
#define EEPROM_PADDR3   0x1b
#define EEPROM_PADDR4   0x1c
#define EEPROM_PADDR5   0x1d
#define EEPROM_PA_CRC   0x1e
#define EEPROM_CHKSUM   0x1f

/*
** EEPROM bytes for checksumming
*/
#define EEPROM_MAX      32             /* bytes */

/*
** EEPROM MISCELLANEOUS FLAGS
*/
#define RBE_SHADOW	0x0100	/* Remote Boot Enable Shadow */
#define READ_AHEAD      0x0080  /* Read Ahead feature */
#define IRQ_SEL2        0x0070  /* IRQ line selection (LeMAC2) */
#define IRQ_SEL         0x0060  /* IRQ line selection */
#define FAST_BUS        0x0008  /* ISA Bus speeds > 8.33MHz */
#define ENA_16          0x0004  /* Enables 16 bit memory transfers */
#define WRITE_BEHIND    0x0002  /* Write Behind feature */
#define _0WS_ENA        0x0001  /* Zero Wait State Enable */

/*
** EEPROM NETWORK MANAGEMENT FLAGS
*/
#define NETMAN_POL      0x04    /* Polarity defeat */
#define NETMAN_LINK     0x02    /* Link defeat */
#define NETMAN_CCE      0x01    /* Custom Counters Enable */

/*
** EEPROM SW FLAGS
*/
#define SW_SQE		0x10	/* Signal Quality Error */
#define SW_LAB		0x08	/* Less Aggressive Backoff */
#define SW_INIT		0x04	/* Initialized */
#define SW_TIMEOUT     	0x02	/* 0:2.5 mins, 1: 30 secs */
#define SW_REMOTE      	0x01    /* Remote Boot Enable -> 1 */

/*
** EEPROM SETUP FLAGS
*/
#define SETUP_APD	0x80	/* AutoPort Disable */
#define SETUP_PS	0x40	/* Port Select */
#define SETUP_MP	0x20	/* MultiPort */
#define SETUP_1TP	0x10	/* 1 port, TP */
#define SETUP_1COAX	0x00	/* 1 port, Coax */
#define SETUP_DRAM	0x02	/* Number of DRAMS on board */

/*
** EEPROM MANAGEMENT FLAGS
*/
#define MGMT_CCE	0x01	/* Custom Counters Enable */

/*
** EEPROM VERSIONS
*/
#define LeMAC           0x11
#define LeMAC2          0x12

/*
** Miscellaneous
*/

#define EEPROM_WAIT_TIME 1000    /* Number of microseconds */
#define EISA_EN         0x0001   /* Enable EISA bus buffers */

#define HASH_TABLE_LEN   512     /* Bits */

#define XCT 0x80                 /* Transmit Cut Through */
#define PRELOAD 16               /* 4 long words */

#define MASK_INTERRUPTS   1
#define UNMASK_INTERRUPTS 0

#define EEPROM_OFFSET(a) ((u_short)((u_long)(a)))

/*
** Include the IOCTL stuff
*/
#include <linux/sockios.h>

#define	EWRK3IOCTL	SIOCDEVPRIVATE

struct ewrk3_ioctl {
	unsigned short cmd;                /* Command to run */
	unsigned short len;                /* Length of the data buffer */
	unsigned char  __user *data;       /* Pointer to the data buffer */
};

/*
** Recognised commands for the driver
*/
#define EWRK3_GET_HWADDR	0x01 /* Get the hardware address */
#define EWRK3_SET_HWADDR	0x02 /* Get the hardware address */
#define EWRK3_SET_PROM  	0x03 /* Set Promiscuous Mode */
#define EWRK3_CLR_PROM  	0x04 /* Clear Promiscuous Mode */
#define EWRK3_SAY_BOO	        0x05 /* Say "Boo!" to the kernel log file */
#define EWRK3_GET_MCA   	0x06 /* Get a multicast address */
#define EWRK3_SET_MCA   	0x07 /* Set a multicast address */
#define EWRK3_CLR_MCA    	0x08 /* Clear a multicast address */
#define EWRK3_MCA_EN    	0x09 /* Enable a multicast address group */
#define EWRK3_GET_STATS  	0x0a /* Get the driver statistics */
#define EWRK3_CLR_STATS 	0x0b /* Zero out the driver statistics */
#define EWRK3_GET_CSR   	0x0c /* Get the CSR Register contents */
#define EWRK3_SET_CSR   	0x0d /* Set the CSR Register contents */
#define EWRK3_GET_EEPROM   	0x0e /* Get the EEPROM contents */
#define EWRK3_SET_EEPROM	0x0f /* Set the EEPROM contents */
#define EWRK3_GET_CMR   	0x10 /* Get the CMR Register contents */
#define EWRK3_CLR_TX_CUT_THRU  	0x11 /* Clear the TX cut through mode */
#define EWRK3_SET_TX_CUT_THRU	0x12 /* Set the TX cut through mode */
