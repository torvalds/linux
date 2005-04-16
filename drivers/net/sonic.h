/*
 * Helpfile for sonic.c
 *
 * (C) Waldorf Electronics, Germany
 * Written by Andreas Busse
 *
 * NOTE: most of the structure definitions here are endian dependent.
 * If you want to use this driver on big endian machines, the data
 * and pad structure members must be exchanged. Also, the structures
 * need to be changed accordingly to the bus size. 
 *
 * 981229 MSch:	did just that for the 68k Mac port (32 bit, big endian),
 *		see CONFIG_MACSONIC branch below.
 *
 */
#ifndef SONIC_H
#define SONIC_H

#include <linux/config.h>

/*
 * SONIC register offsets
 */

#define SONIC_CMD              0x00
#define SONIC_DCR              0x01
#define SONIC_RCR              0x02
#define SONIC_TCR              0x03
#define SONIC_IMR              0x04
#define SONIC_ISR              0x05

#define SONIC_UTDA             0x06
#define SONIC_CTDA             0x07

#define SONIC_URDA             0x0d
#define SONIC_CRDA             0x0e
#define SONIC_EOBC             0x13
#define SONIC_URRA             0x14
#define SONIC_RSA              0x15
#define SONIC_REA              0x16
#define SONIC_RRP              0x17
#define SONIC_RWP              0x18
#define SONIC_RSC              0x2b

#define SONIC_CEP              0x21
#define SONIC_CAP2             0x22
#define SONIC_CAP1             0x23
#define SONIC_CAP0             0x24
#define SONIC_CE               0x25
#define SONIC_CDP              0x26
#define SONIC_CDC              0x27

#define SONIC_WT0              0x29
#define SONIC_WT1              0x2a

#define SONIC_SR               0x28


/* test-only registers */

#define SONIC_TPS		0x08
#define SONIC_TFC		0x09
#define SONIC_TSA0		0x0a
#define SONIC_TSA1		0x0b
#define SONIC_TFS		0x0c

#define SONIC_CRBA0		0x0f
#define SONIC_CRBA1		0x10
#define SONIC_RBWC0		0x11
#define SONIC_RBWC1		0x12
#define SONIC_TTDA		0x20
#define SONIC_MDT		0x2f

#define SONIC_TRBA0		0x19
#define SONIC_TRBA1		0x1a
#define SONIC_TBWC0		0x1b
#define SONIC_TBWC1		0x1c
#define SONIC_LLFA		0x1f

#define SONIC_ADDR0		0x1d
#define SONIC_ADDR1		0x1e

/*
 * Error counters
 */
#define SONIC_CRCT              0x2c
#define SONIC_FAET              0x2d
#define SONIC_MPT               0x2e

#define SONIC_DCR2              0x3f

/*
 * SONIC command bits
 */

#define SONIC_CR_LCAM           0x0200
#define SONIC_CR_RRRA           0x0100
#define SONIC_CR_RST            0x0080
#define SONIC_CR_ST             0x0020
#define SONIC_CR_STP            0x0010
#define SONIC_CR_RXEN           0x0008
#define SONIC_CR_RXDIS          0x0004
#define SONIC_CR_TXP            0x0002
#define SONIC_CR_HTX            0x0001

/*
 * SONIC data configuration bits
 */

#define SONIC_DCR_EXBUS         0x8000
#define SONIC_DCR_LBR           0x2000
#define SONIC_DCR_PO1           0x1000
#define SONIC_DCR_PO0           0x0800
#define SONIC_DCR_SBUS          0x0400
#define SONIC_DCR_USR1          0x0200
#define SONIC_DCR_USR0          0x0100
#define SONIC_DCR_WC1           0x0080
#define SONIC_DCR_WC0           0x0040
#define SONIC_DCR_DW            0x0020
#define SONIC_DCR_BMS           0x0010
#define SONIC_DCR_RFT1          0x0008
#define SONIC_DCR_RFT0          0x0004
#define SONIC_DCR_TFT1          0x0002
#define SONIC_DCR_TFT0          0x0001

/*
 * Constants for the SONIC receive control register.
 */

#define SONIC_RCR_ERR           0x8000
#define SONIC_RCR_RNT           0x4000
#define SONIC_RCR_BRD           0x2000
#define SONIC_RCR_PRO           0x1000
#define SONIC_RCR_AMC           0x0800
#define SONIC_RCR_LB1           0x0400
#define SONIC_RCR_LB0           0x0200

#define SONIC_RCR_MC            0x0100
#define SONIC_RCR_BC            0x0080
#define SONIC_RCR_LPKT          0x0040
#define SONIC_RCR_CRS           0x0020
#define SONIC_RCR_COL           0x0010
#define SONIC_RCR_CRCR          0x0008
#define SONIC_RCR_FAER          0x0004
#define SONIC_RCR_LBK           0x0002
#define SONIC_RCR_PRX           0x0001

#define SONIC_RCR_LB_OFF        0
#define SONIC_RCR_LB_MAC        SONIC_RCR_LB0
#define SONIC_RCR_LB_ENDEC      SONIC_RCR_LB1
#define SONIC_RCR_LB_TRANS      (SONIC_RCR_LB0 | SONIC_RCR_LB1)

/* default RCR setup */

#define SONIC_RCR_DEFAULT       (SONIC_RCR_BRD)


/*
 * SONIC Transmit Control register bits
 */

#define SONIC_TCR_PINTR         0x8000
#define SONIC_TCR_POWC          0x4000
#define SONIC_TCR_CRCI          0x2000
#define SONIC_TCR_EXDIS         0x1000
#define SONIC_TCR_EXD           0x0400
#define SONIC_TCR_DEF           0x0200
#define SONIC_TCR_NCRS          0x0100
#define SONIC_TCR_CRLS          0x0080
#define SONIC_TCR_EXC           0x0040
#define SONIC_TCR_PMB           0x0008
#define SONIC_TCR_FU            0x0004
#define SONIC_TCR_BCM           0x0002
#define SONIC_TCR_PTX           0x0001

#define SONIC_TCR_DEFAULT       0x0000

/* 
 * Constants for the SONIC_INTERRUPT_MASK and
 * SONIC_INTERRUPT_STATUS registers.
 */

#define SONIC_INT_BR		0x4000
#define SONIC_INT_HBL		0x2000
#define SONIC_INT_LCD           0x1000
#define SONIC_INT_PINT          0x0800
#define SONIC_INT_PKTRX         0x0400
#define SONIC_INT_TXDN          0x0200
#define SONIC_INT_TXER          0x0100
#define SONIC_INT_TC            0x0080
#define SONIC_INT_RDE           0x0040
#define SONIC_INT_RBE           0x0020
#define SONIC_INT_RBAE		0x0010
#define SONIC_INT_CRC		0x0008
#define SONIC_INT_FAE		0x0004
#define SONIC_INT_MP		0x0002
#define SONIC_INT_RFO		0x0001


/*
 * The interrupts we allow.
 */

#define SONIC_IMR_DEFAULT	(SONIC_INT_BR | \
				SONIC_INT_LCD | \
                                SONIC_INT_PINT | \
                                SONIC_INT_PKTRX | \
                                SONIC_INT_TXDN | \
                                SONIC_INT_TXER | \
                                SONIC_INT_RDE | \
                                SONIC_INT_RBE | \
                                SONIC_INT_RBAE | \
                                SONIC_INT_CRC | \
                                SONIC_INT_FAE | \
                                SONIC_INT_MP)


#define	SONIC_END_OF_LINKS	0x0001


#ifdef CONFIG_MACSONIC
/*
 * Big endian like structures on 680x0 Macs
 */

typedef struct {
	u32 rx_bufadr_l;	/* receive buffer ptr */
	u32 rx_bufadr_h;

	u32 rx_bufsize_l;	/* no. of words in the receive buffer */
	u32 rx_bufsize_h;
} sonic_rr_t;

/*
 * Sonic receive descriptor. Receive descriptors are
 * kept in a linked list of these structures.
 */

typedef struct {
	SREGS_PAD(pad0);
	u16 rx_status;		/* status after reception of a packet */
	 SREGS_PAD(pad1);
	u16 rx_pktlen;		/* length of the packet incl. CRC */

	/*
	 * Pointers to the location in the receive buffer area (RBA)
	 * where the packet resides. A packet is always received into
	 * a contiguous piece of memory.
	 */
	 SREGS_PAD(pad2);
	u16 rx_pktptr_l;
	 SREGS_PAD(pad3);
	u16 rx_pktptr_h;

	 SREGS_PAD(pad4);
	u16 rx_seqno;		/* sequence no. */

	 SREGS_PAD(pad5);
	u16 link;		/* link to next RDD (end if EOL bit set) */

	/*
	 * Owner of this descriptor, 0= driver, 1=sonic
	 */

	 SREGS_PAD(pad6);
	u16 in_use;

	caddr_t rda_next;	/* pointer to next RD */
} sonic_rd_t;


/*
 * Describes a Transmit Descriptor
 */
typedef struct {
	SREGS_PAD(pad0);
	u16 tx_status;		/* status after transmission of a packet */
	 SREGS_PAD(pad1);
	u16 tx_config;		/* transmit configuration for this packet */
	 SREGS_PAD(pad2);
	u16 tx_pktsize;		/* size of the packet to be transmitted */
	 SREGS_PAD(pad3);
	u16 tx_frag_count;	/* no. of fragments */

	 SREGS_PAD(pad4);
	u16 tx_frag_ptr_l;
	 SREGS_PAD(pad5);
	u16 tx_frag_ptr_h;
	 SREGS_PAD(pad6);
	u16 tx_frag_size;

	 SREGS_PAD(pad7);
	u16 link;		/* ptr to next descriptor */
} sonic_td_t;


/*
 * Describes an entry in the CAM Descriptor Area.
 */

typedef struct {
	SREGS_PAD(pad0);
	u16 cam_entry_pointer;
	 SREGS_PAD(pad1);
	u16 cam_cap0;
	 SREGS_PAD(pad2);
	u16 cam_cap1;
	 SREGS_PAD(pad3);
	u16 cam_cap2;
} sonic_cd_t;

#define CAM_DESCRIPTORS 16


typedef struct {
	sonic_cd_t cam_desc[CAM_DESCRIPTORS];
	 SREGS_PAD(pad);
	u16 cam_enable;
} sonic_cda_t;

#else				/* original declarations, little endian 32 bit */

/*
 * structure definitions
 */

typedef struct {
	u32 rx_bufadr_l;	/* receive buffer ptr */
	u32 rx_bufadr_h;

	u32 rx_bufsize_l;	/* no. of words in the receive buffer */
	u32 rx_bufsize_h;
} sonic_rr_t;

/*
 * Sonic receive descriptor. Receive descriptors are
 * kept in a linked list of these structures.
 */

typedef struct {
	u16 rx_status;		/* status after reception of a packet */
	 SREGS_PAD(pad0);
	u16 rx_pktlen;		/* length of the packet incl. CRC */
	 SREGS_PAD(pad1);

	/*
	 * Pointers to the location in the receive buffer area (RBA)
	 * where the packet resides. A packet is always received into
	 * a contiguous piece of memory.
	 */
	u16 rx_pktptr_l;
	 SREGS_PAD(pad2);
	u16 rx_pktptr_h;
	 SREGS_PAD(pad3);

	u16 rx_seqno;		/* sequence no. */
	 SREGS_PAD(pad4);

	u16 link;		/* link to next RDD (end if EOL bit set) */
	 SREGS_PAD(pad5);

	/*
	 * Owner of this descriptor, 0= driver, 1=sonic
	 */

	u16 in_use;
	 SREGS_PAD(pad6);

	caddr_t rda_next;	/* pointer to next RD */
} sonic_rd_t;


/*
 * Describes a Transmit Descriptor
 */
typedef struct {
	u16 tx_status;		/* status after transmission of a packet */
	 SREGS_PAD(pad0);
	u16 tx_config;		/* transmit configuration for this packet */
	 SREGS_PAD(pad1);
	u16 tx_pktsize;		/* size of the packet to be transmitted */
	 SREGS_PAD(pad2);
	u16 tx_frag_count;	/* no. of fragments */
	 SREGS_PAD(pad3);

	u16 tx_frag_ptr_l;
	 SREGS_PAD(pad4);
	u16 tx_frag_ptr_h;
	 SREGS_PAD(pad5);
	u16 tx_frag_size;
	 SREGS_PAD(pad6);

	u16 link;		/* ptr to next descriptor */
	 SREGS_PAD(pad7);
} sonic_td_t;


/*
 * Describes an entry in the CAM Descriptor Area.
 */

typedef struct {
	u16 cam_entry_pointer;
	 SREGS_PAD(pad0);
	u16 cam_cap0;
	 SREGS_PAD(pad1);
	u16 cam_cap1;
	 SREGS_PAD(pad2);
	u16 cam_cap2;
	 SREGS_PAD(pad3);
} sonic_cd_t;

#define CAM_DESCRIPTORS 16


typedef struct {
	sonic_cd_t cam_desc[CAM_DESCRIPTORS];
	u16 cam_enable;
	 SREGS_PAD(pad);
} sonic_cda_t;
#endif				/* endianness */

/*
 * Some tunables for the buffer areas. Power of 2 is required
 * the current driver uses one receive buffer for each descriptor.
 *
 * MSch: use more buffer space for the slow m68k Macs!
 */
#ifdef CONFIG_MACSONIC
#define SONIC_NUM_RRS    32	/* number of receive resources */
#define SONIC_NUM_RDS    SONIC_NUM_RRS	/* number of receive descriptors */
#define SONIC_NUM_TDS    32	/* number of transmit descriptors */
#else
#define SONIC_NUM_RRS    16	/* number of receive resources */
#define SONIC_NUM_RDS    SONIC_NUM_RRS	/* number of receive descriptors */
#define SONIC_NUM_TDS    16	/* number of transmit descriptors */
#endif
#define SONIC_RBSIZE   1520	/* size of one resource buffer */

#define SONIC_RDS_MASK   (SONIC_NUM_RDS-1)
#define SONIC_TDS_MASK   (SONIC_NUM_TDS-1)


/* Information that need to be kept for each board. */
struct sonic_local {
	sonic_cda_t cda;	/* virtual CPU address of CDA */
	sonic_td_t tda[SONIC_NUM_TDS];	/* transmit descriptor area */
	sonic_rr_t rra[SONIC_NUM_RRS];	/* receive resource area */
	sonic_rd_t rda[SONIC_NUM_RDS];	/* receive descriptor area */
	struct sk_buff *tx_skb[SONIC_NUM_TDS];	/* skbuffs for packets to transmit */
	unsigned int tx_laddr[SONIC_NUM_TDS];	/* logical DMA address fro skbuffs */
	unsigned char *rba;	/* start of receive buffer areas */
	unsigned int cda_laddr;	/* logical DMA address of CDA */
	unsigned int tda_laddr;	/* logical DMA address of TDA */
	unsigned int rra_laddr;	/* logical DMA address of RRA */
	unsigned int rda_laddr;	/* logical DMA address of RDA */
	unsigned int rba_laddr;	/* logical DMA address of RBA */
	unsigned int cur_rra;	/* current indexes to resource areas */
	unsigned int cur_rx;
	unsigned int cur_tx;
	unsigned int dirty_tx;	/* last unacked transmit packet */
	char tx_full;
	struct net_device_stats stats;
};

#define TX_TIMEOUT 6

/* Index to functions, as function prototypes. */

static int sonic_open(struct net_device *dev);
static int sonic_send_packet(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t sonic_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void sonic_rx(struct net_device *dev);
static int sonic_close(struct net_device *dev);
static struct net_device_stats *sonic_get_stats(struct net_device *dev);
static void sonic_multicast_list(struct net_device *dev);
static int sonic_init(struct net_device *dev);
static void sonic_tx_timeout(struct net_device *dev);

static const char *version =
    "sonic.c:v0.92 20.9.98 tsbogend@alpha.franken.de\n";

#endif /* SONIC_H */
