
/*
 *	Index to functions.
 */

static int  el1_probe1(struct net_device *dev, int ioaddr);
static int  el_open(struct net_device *dev);
static void el_timeout(struct net_device *dev);
static int  el_start_xmit(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t el_interrupt(int irq, void *dev_id);
static void el_receive(struct net_device *dev);
static void el_reset(struct net_device *dev);
static int  el1_close(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);
static const struct ethtool_ops netdev_ethtool_ops;

#define EL1_IO_EXTENT	16

#ifndef EL_DEBUG
#define EL_DEBUG  0	/* use 0 for production, 1 for devel., >2 for debug */
#endif			/* Anything above 5 is wordy death! */
#define debug el_debug
static int el_debug = EL_DEBUG;

/*
 *	Board-specific info in netdev_priv(dev).
 */

struct net_local
{
	int		tx_pkt_start;	/* The length of the current Tx packet. */
	int		collisions;	/* Tx collisions this packet */
	int		loading;	/* Spot buffer load collisions */
	int		txing;		/* True if card is in TX mode */
	spinlock_t	lock;		/* Serializing lock */
};


#define RX_STATUS (ioaddr + 0x06)
#define RX_CMD	  RX_STATUS
#define TX_STATUS (ioaddr + 0x07)
#define TX_CMD	  TX_STATUS
#define GP_LOW 	  (ioaddr + 0x08)
#define GP_HIGH   (ioaddr + 0x09)
#define RX_BUF_CLR (ioaddr + 0x0A)
#define RX_LOW	  (ioaddr + 0x0A)
#define RX_HIGH   (ioaddr + 0x0B)
#define SAPROM	  (ioaddr + 0x0C)
#define AX_STATUS (ioaddr + 0x0E)
#define AX_CMD	  AX_STATUS
#define DATAPORT  (ioaddr + 0x0F)
#define TX_RDY 0x08		/* In TX_STATUS */

#define EL1_DATAPTR	0x08
#define EL1_RXPTR	0x0A
#define EL1_SAPROM	0x0C
#define EL1_DATAPORT 	0x0f

/*
 *	Writes to the ax command register.
 */

#define AX_OFF	0x00			/* Irq off, buffer access on */
#define AX_SYS  0x40			/* Load the buffer */
#define AX_XMIT 0x44			/* Transmit a packet */
#define AX_RX	0x48			/* Receive a packet */
#define AX_LOOP	0x0C			/* Loopback mode */
#define AX_RESET 0x80

/*
 *	Normal receive mode written to RX_STATUS.  We must intr on short packets
 *	to avoid bogus rx lockups.
 */

#define RX_NORM 0xA8		/* 0x68 == all addrs, 0xA8 only to me. */
#define RX_PROM 0x68		/* Senior Prom, uhmm promiscuous mode. */
#define RX_MULT 0xE8		/* Accept multicast packets. */
#define TX_NORM 0x0A		/* Interrupt on everything that might hang the chip */

/*
 *	TX_STATUS register.
 */

#define TX_COLLISION 0x02
#define TX_16COLLISIONS 0x04
#define TX_READY 0x08

#define RX_RUNT 0x08
#define RX_MISSED 0x01		/* Missed a packet due to 3c501 braindamage. */
#define RX_GOOD	0x30		/* Good packet 0x20, or simple overflow 0x10. */

