/*
 * Header file for sonic.c
 *
 * (C) Waldorf Electronics, Germany
 * Written by Andreas Busse
 *
 * NOTE: most of the structure definitions here are endian dependent.
 * If you want to use this driver on big endian machines, the data
 * and pad structure members must be exchanged. Also, the structures
 * need to be changed accordingly to the bus size.
 *
 * 981229 MSch:	did just that for the 68k Mac port (32 bit, big endian)
 *
 * 990611 David Huggins-Daines <dhd@debian.org>: This machine abstraction
 * does not cope with 16-bit bus sizes very well.  Therefore I have
 * rewritten it with ugly macros and evil inlines.
 *
 * 050625 Finn Thain: introduced more 32-bit cards and dhd's support
 *        for 16-bit cards (from the mac68k project).
 */

#ifndef SONIC_H
#define SONIC_H


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
#define SONIC_INT_LCD		0x1000
#define SONIC_INT_PINT		0x0800
#define SONIC_INT_PKTRX		0x0400
#define SONIC_INT_TXDN		0x0200
#define SONIC_INT_TXER		0x0100
#define SONIC_INT_TC		0x0080
#define SONIC_INT_RDE		0x0040
#define SONIC_INT_RBE		0x0020
#define SONIC_INT_RBAE		0x0010
#define SONIC_INT_CRC		0x0008
#define SONIC_INT_FAE		0x0004
#define SONIC_INT_MP		0x0002
#define SONIC_INT_RFO		0x0001


/*
 * The interrupts we allow.
 */

#define SONIC_IMR_DEFAULT     ( SONIC_INT_BR | \
                                SONIC_INT_LCD | \
                                SONIC_INT_RFO | \
                                SONIC_INT_PKTRX | \
                                SONIC_INT_TXDN | \
                                SONIC_INT_TXER | \
                                SONIC_INT_RDE | \
                                SONIC_INT_RBAE | \
                                SONIC_INT_CRC | \
                                SONIC_INT_FAE | \
                                SONIC_INT_MP)


#define SONIC_EOL       0x0001
#define CAM_DESCRIPTORS 16

/* Offsets in the various DMA buffers accessed by the SONIC */

#define SONIC_BITMODE16 0
#define SONIC_BITMODE32 1
#define SONIC_BUS_SCALE(bitmode) ((bitmode) ? 4 : 2)
/* Note!  These are all measured in bus-size units, so use SONIC_BUS_SCALE */
#define SIZEOF_SONIC_RR 4
#define SONIC_RR_BUFADR_L  0
#define SONIC_RR_BUFADR_H  1
#define SONIC_RR_BUFSIZE_L 2
#define SONIC_RR_BUFSIZE_H 3

#define SIZEOF_SONIC_RD 7
#define SONIC_RD_STATUS   0
#define SONIC_RD_PKTLEN   1
#define SONIC_RD_PKTPTR_L 2
#define SONIC_RD_PKTPTR_H 3
#define SONIC_RD_SEQNO    4
#define SONIC_RD_LINK     5
#define SONIC_RD_IN_USE   6

#define SIZEOF_SONIC_TD 8
#define SONIC_TD_STATUS       0
#define SONIC_TD_CONFIG       1
#define SONIC_TD_PKTSIZE      2
#define SONIC_TD_FRAG_COUNT   3
#define SONIC_TD_FRAG_PTR_L   4
#define SONIC_TD_FRAG_PTR_H   5
#define SONIC_TD_FRAG_SIZE    6
#define SONIC_TD_LINK         7

#define SIZEOF_SONIC_CD 4
#define SONIC_CD_ENTRY_POINTER 0
#define SONIC_CD_CAP0          1
#define SONIC_CD_CAP1          2
#define SONIC_CD_CAP2          3

#define SIZEOF_SONIC_CDA ((CAM_DESCRIPTORS * SIZEOF_SONIC_CD) + 1)
#define SONIC_CDA_CAM_ENABLE   (CAM_DESCRIPTORS * SIZEOF_SONIC_CD)

/*
 * Some tunables for the buffer areas. Power of 2 is required
 * the current driver uses one receive buffer for each descriptor.
 *
 * MSch: use more buffer space for the slow m68k Macs!
 */
#define SONIC_NUM_RRS   16            /* number of receive resources */
#define SONIC_NUM_RDS   SONIC_NUM_RRS /* number of receive descriptors */
#define SONIC_NUM_TDS   16            /* number of transmit descriptors */

#define SONIC_RDS_MASK  (SONIC_NUM_RDS-1)
#define SONIC_TDS_MASK  (SONIC_NUM_TDS-1)

#define SONIC_RBSIZE	1520          /* size of one resource buffer */

/* Again, measured in bus size units! */
#define SIZEOF_SONIC_DESC (SIZEOF_SONIC_CDA	\
	+ (SIZEOF_SONIC_TD * SONIC_NUM_TDS)	\
	+ (SIZEOF_SONIC_RD * SONIC_NUM_RDS)	\
	+ (SIZEOF_SONIC_RR * SONIC_NUM_RRS))

/* Information that need to be kept for each board. */
struct sonic_local {
	/* Bus size.  0 == 16 bits, 1 == 32 bits. */
	int dma_bitmode;
	/* Register offset within the longword (independent of endianness,
	   and varies from one type of Macintosh SONIC to another
	   (Aarrgh)) */
	int reg_offset;
	void *descriptors;
	/* Crud.  These areas have to be within the same 64K.  Therefore
       we allocate a desriptors page, and point these to places within it. */
	void *cda;  /* CAM descriptor area */
	void *tda;  /* Transmit descriptor area */
	void *rra;  /* Receive resource area */
	void *rda;  /* Receive descriptor area */
	struct sk_buff* volatile rx_skb[SONIC_NUM_RRS];	/* packets to be received */
	struct sk_buff* volatile tx_skb[SONIC_NUM_TDS];	/* packets to be transmitted */
	unsigned int tx_len[SONIC_NUM_TDS]; /* lengths of tx DMA mappings */
	/* Logical DMA addresses on MIPS, bus addresses on m68k
	 * (so "laddr" is a bit misleading) */
	dma_addr_t descriptors_laddr;
	u32 cda_laddr;              /* logical DMA address of CDA */
	u32 tda_laddr;              /* logical DMA address of TDA */
	u32 rra_laddr;              /* logical DMA address of RRA */
	u32 rda_laddr;              /* logical DMA address of RDA */
	dma_addr_t rx_laddr[SONIC_NUM_RRS]; /* logical DMA addresses of rx skbuffs */
	dma_addr_t tx_laddr[SONIC_NUM_TDS]; /* logical DMA addresses of tx skbuffs */
	unsigned int rra_end;
	unsigned int cur_rwp;
	unsigned int cur_rx;
	unsigned int cur_tx;           /* first unacked transmit packet */
	unsigned int eol_rx;
	unsigned int eol_tx;           /* last unacked transmit packet */
	unsigned int next_tx;          /* next free TD */
	struct device *device;         /* generic device */
	struct net_device_stats stats;
};

#define TX_TIMEOUT (3 * HZ)

/* Index to functions, as function prototypes. */

static int sonic_open(struct net_device *dev);
static int sonic_send_packet(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t sonic_interrupt(int irq, void *dev_id);
static void sonic_rx(struct net_device *dev);
static int sonic_close(struct net_device *dev);
static struct net_device_stats *sonic_get_stats(struct net_device *dev);
static void sonic_multicast_list(struct net_device *dev);
static int sonic_init(struct net_device *dev);
static void sonic_tx_timeout(struct net_device *dev);

/* Internal inlines for reading/writing DMA buffers.  Note that bus
   size and endianness matter here, whereas they don't for registers,
   as far as we can tell. */
/* OpenBSD calls this "SWO".  I'd like to think that sonic_buf_put()
   is a much better name. */
static inline void sonic_buf_put(void* base, int bitmode,
				 int offset, __u16 val)
{
	if (bitmode)
#ifdef __BIG_ENDIAN
		((__u16 *) base + (offset*2))[1] = val;
#else
		((__u16 *) base + (offset*2))[0] = val;
#endif
	else
	 	((__u16 *) base)[offset] = val;
}

static inline __u16 sonic_buf_get(void* base, int bitmode,
				  int offset)
{
	if (bitmode)
#ifdef __BIG_ENDIAN
		return ((volatile __u16 *) base + (offset*2))[1];
#else
		return ((volatile __u16 *) base + (offset*2))[0];
#endif
	else
		return ((volatile __u16 *) base)[offset];
}

/* Inlines that you should actually use for reading/writing DMA buffers */
static inline void sonic_cda_put(struct net_device* dev, int entry,
				 int offset, __u16 val)
{
	struct sonic_local *lp = netdev_priv(dev);
	sonic_buf_put(lp->cda, lp->dma_bitmode,
		      (entry * SIZEOF_SONIC_CD) + offset, val);
}

static inline __u16 sonic_cda_get(struct net_device* dev, int entry,
				  int offset)
{
	struct sonic_local *lp = netdev_priv(dev);
	return sonic_buf_get(lp->cda, lp->dma_bitmode,
			     (entry * SIZEOF_SONIC_CD) + offset);
}

static inline void sonic_set_cam_enable(struct net_device* dev, __u16 val)
{
	struct sonic_local *lp = netdev_priv(dev);
	sonic_buf_put(lp->cda, lp->dma_bitmode, SONIC_CDA_CAM_ENABLE, val);
}

static inline __u16 sonic_get_cam_enable(struct net_device* dev)
{
	struct sonic_local *lp = netdev_priv(dev);
	return sonic_buf_get(lp->cda, lp->dma_bitmode, SONIC_CDA_CAM_ENABLE);
}

static inline void sonic_tda_put(struct net_device* dev, int entry,
				 int offset, __u16 val)
{
	struct sonic_local *lp = netdev_priv(dev);
	sonic_buf_put(lp->tda, lp->dma_bitmode,
		      (entry * SIZEOF_SONIC_TD) + offset, val);
}

static inline __u16 sonic_tda_get(struct net_device* dev, int entry,
				  int offset)
{
	struct sonic_local *lp = netdev_priv(dev);
	return sonic_buf_get(lp->tda, lp->dma_bitmode,
			     (entry * SIZEOF_SONIC_TD) + offset);
}

static inline void sonic_rda_put(struct net_device* dev, int entry,
				 int offset, __u16 val)
{
	struct sonic_local *lp = netdev_priv(dev);
	sonic_buf_put(lp->rda, lp->dma_bitmode,
		      (entry * SIZEOF_SONIC_RD) + offset, val);
}

static inline __u16 sonic_rda_get(struct net_device* dev, int entry,
				  int offset)
{
	struct sonic_local *lp = netdev_priv(dev);
	return sonic_buf_get(lp->rda, lp->dma_bitmode,
			     (entry * SIZEOF_SONIC_RD) + offset);
}

static inline void sonic_rra_put(struct net_device* dev, int entry,
				 int offset, __u16 val)
{
	struct sonic_local *lp = netdev_priv(dev);
	sonic_buf_put(lp->rra, lp->dma_bitmode,
		      (entry * SIZEOF_SONIC_RR) + offset, val);
}

static inline __u16 sonic_rra_get(struct net_device* dev, int entry,
				  int offset)
{
	struct sonic_local *lp = netdev_priv(dev);
	return sonic_buf_get(lp->rra, lp->dma_bitmode,
			     (entry * SIZEOF_SONIC_RR) + offset);
}

static const char version[] =
    "sonic.c:v0.92 20.9.98 tsbogend@alpha.franken.de\n";

#endif /* SONIC_H */
