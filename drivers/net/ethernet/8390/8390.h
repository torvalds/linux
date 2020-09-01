/* Generic NS8390 register definitions. */

/* This file is part of Donald Becker's 8390 drivers, and is distributed
 * under the same license. Auto-loading of 8390.o only in v2.2 - Paul G.
 * Some of these names and comments originated from the Crynwr
 * packet drivers, which are distributed under the GPL.
 */

#ifndef _8390_h
#define _8390_h

#include <linux/if_ether.h>
#include <linux/ioport.h>
#include <linux/irqreturn.h>
#include <linux/skbuff.h>

#define TX_PAGES 12	/* Two Tx slots */

/* The 8390 specific per-packet-header format. */
struct e8390_pkt_hdr {
	unsigned char status; /* status */
	unsigned char next;   /* pointer to next packet. */
	unsigned short count; /* header + packet length in bytes */
};

#ifdef CONFIG_NET_POLL_CONTROLLER
void ei_poll(struct net_device *dev);
void eip_poll(struct net_device *dev);
#endif


/* Without I/O delay - non ISA or later chips */
void NS8390_init(struct net_device *dev, int startp);
int ei_open(struct net_device *dev);
int ei_close(struct net_device *dev);
irqreturn_t ei_interrupt(int irq, void *dev_id);
void ei_tx_timeout(struct net_device *dev, unsigned int txqueue);
netdev_tx_t ei_start_xmit(struct sk_buff *skb, struct net_device *dev);
void ei_set_multicast_list(struct net_device *dev);
struct net_device_stats *ei_get_stats(struct net_device *dev);

extern const struct net_device_ops ei_netdev_ops;

struct net_device *__alloc_ei_netdev(int size);
static inline struct net_device *alloc_ei_netdev(void)
{
	return __alloc_ei_netdev(0);
}

/* With I/O delay form */
void NS8390p_init(struct net_device *dev, int startp);
int eip_open(struct net_device *dev);
int eip_close(struct net_device *dev);
irqreturn_t eip_interrupt(int irq, void *dev_id);
void eip_tx_timeout(struct net_device *dev, unsigned int txqueue);
netdev_tx_t eip_start_xmit(struct sk_buff *skb, struct net_device *dev);
void eip_set_multicast_list(struct net_device *dev);
struct net_device_stats *eip_get_stats(struct net_device *dev);

extern const struct net_device_ops eip_netdev_ops;

struct net_device *__alloc_eip_netdev(int size);
static inline struct net_device *alloc_eip_netdev(void)
{
	return __alloc_eip_netdev(0);
}

/* You have one of these per-board */
struct ei_device {
	const char *name;
	void (*reset_8390)(struct net_device *dev);
	void (*get_8390_hdr)(struct net_device *dev,
			     struct e8390_pkt_hdr *hdr, int ring_page);
	void (*block_output)(struct net_device *dev, int count,
			     const unsigned char *buf, int start_page);
	void (*block_input)(struct net_device *dev, int count,
			    struct sk_buff *skb, int ring_offset);
	unsigned long rmem_start;
	unsigned long rmem_end;
	void __iomem *mem;
	unsigned char mcfilter[8];
	unsigned open:1;
	unsigned word16:1;		/* We have the 16-bit (vs 8-bit)
					 * version of the card.
					 */
	unsigned bigendian:1;		/* 16-bit big endian mode. Do NOT
					 * set this on random 8390 clones!
					 */
	unsigned txing:1;		/* Transmit Active */
	unsigned irqlock:1;		/* 8390's intrs disabled when '1'. */
	unsigned dmaing:1;		/* Remote DMA Active */
	unsigned char tx_start_page, rx_start_page, stop_page;
	unsigned char current_page;	/* Read pointer in buffer  */
	unsigned char interface_num;	/* Net port (AUI, 10bT.) to use. */
	unsigned char txqueue;		/* Tx Packet buffer queue length. */
	short tx1, tx2;			/* Packet lengths for ping-pong tx. */
	short lasttx;			/* Alpha version consistency check. */
	unsigned char reg0;		/* Register '0' in a WD8013 */
	unsigned char reg5;		/* Register '5' in a WD8013 */
	unsigned char saved_irq;	/* Original dev->irq value. */
	u32 *reg_offset;		/* Register mapping table */
	spinlock_t page_lock;		/* Page register locks */
	unsigned long priv;		/* Private field to store bus IDs etc. */
	u32 msg_enable;			/* debug message level */
#ifdef AX88796_PLATFORM
	unsigned char rxcr_base;	/* default value for RXCR */
#endif
};

/* The maximum number of 8390 interrupt service routines called per IRQ. */
#define MAX_SERVICE 12

/* The maximum time waited (in jiffies) before assuming a Tx failed. (20ms) */
#define TX_TIMEOUT (20*HZ/100)

#define ei_status (*(struct ei_device *)netdev_priv(dev))

/* Some generic ethernet register configurations. */
#define E8390_TX_IRQ_MASK	0xa	/* For register EN0_ISR */
#define E8390_RX_IRQ_MASK	0x5

#ifdef AX88796_PLATFORM
#define E8390_RXCONFIG		(ei_status.rxcr_base | 0x04)
#define E8390_RXOFF		(ei_status.rxcr_base | 0x20)
#else
/* EN0_RXCR: broadcasts, no multicast,errors */
#define E8390_RXCONFIG		0x4
/* EN0_RXCR: Accept no packets */
#define E8390_RXOFF		0x20
#endif

/* EN0_TXCR: Normal transmit mode */
#define E8390_TXCONFIG		0x00
/* EN0_TXCR: Transmitter off */
#define E8390_TXOFF		0x02


/*  Register accessed at EN_CMD, the 8390 base addr.  */
#define E8390_STOP	0x01	/* Stop and reset the chip */
#define E8390_START	0x02	/* Start the chip, clear reset */
#define E8390_TRANS	0x04	/* Transmit a frame */
#define E8390_RREAD	0x08	/* Remote read */
#define E8390_RWRITE	0x10	/* Remote write  */
#define E8390_NODMA	0x20	/* Remote DMA */
#define E8390_PAGE0	0x00	/* Select page chip registers */
#define E8390_PAGE1	0x40	/* using the two high-order bits */
#define E8390_PAGE2	0x80	/* Page 3 is invalid. */

/* Only generate indirect loads given a machine that needs them.
 * - removed AMIGA_PCMCIA from this list, handled as ISA io now
 * - the _p for generates no delay by default 8390p.c overrides this.
 */

#ifndef ei_inb
#define ei_inb(_p)	inb(_p)
#define ei_outb(_v, _p)	outb(_v, _p)
#define ei_inb_p(_p)	inb(_p)
#define ei_outb_p(_v, _p) outb(_v, _p)
#endif

#ifndef EI_SHIFT
#define EI_SHIFT(x)	(x)
#endif

#define E8390_CMD	EI_SHIFT(0x00)  /* The command register (for all pages) */
/* Page 0 register offsets. */
#define EN0_CLDALO	EI_SHIFT(0x01)	/* Low byte of current local dma addr RD */
#define EN0_STARTPG	EI_SHIFT(0x01)	/* Starting page of ring bfr WR */
#define EN0_CLDAHI	EI_SHIFT(0x02)	/* High byte of current local dma addr RD */
#define EN0_STOPPG	EI_SHIFT(0x02)	/* Ending page +1 of ring bfr WR */
#define EN0_BOUNDARY	EI_SHIFT(0x03)	/* Boundary page of ring bfr RD WR */
#define EN0_TSR		EI_SHIFT(0x04)	/* Transmit status reg RD */
#define EN0_TPSR	EI_SHIFT(0x04)	/* Transmit starting page WR */
#define EN0_NCR		EI_SHIFT(0x05)	/* Number of collision reg RD */
#define EN0_TCNTLO	EI_SHIFT(0x05)	/* Low  byte of tx byte count WR */
#define EN0_FIFO	EI_SHIFT(0x06)	/* FIFO RD */
#define EN0_TCNTHI	EI_SHIFT(0x06)	/* High byte of tx byte count WR */
#define EN0_ISR		EI_SHIFT(0x07)	/* Interrupt status reg RD WR */
#define EN0_CRDALO	EI_SHIFT(0x08)	/* low byte of current remote dma address RD */
#define EN0_RSARLO	EI_SHIFT(0x08)	/* Remote start address reg 0 */
#define EN0_CRDAHI	EI_SHIFT(0x09)	/* high byte, current remote dma address RD */
#define EN0_RSARHI	EI_SHIFT(0x09)	/* Remote start address reg 1 */
#define EN0_RCNTLO	EI_SHIFT(0x0a)	/* Remote byte count reg WR */
#define EN0_RCNTHI	EI_SHIFT(0x0b)	/* Remote byte count reg WR */
#define EN0_RSR		EI_SHIFT(0x0c)	/* rx status reg RD */
#define EN0_RXCR	EI_SHIFT(0x0c)	/* RX configuration reg WR */
#define EN0_TXCR	EI_SHIFT(0x0d)	/* TX configuration reg WR */
#define EN0_COUNTER0	EI_SHIFT(0x0d)	/* Rcv alignment error counter RD */
#define EN0_DCFG	EI_SHIFT(0x0e)	/* Data configuration reg WR */
#define EN0_COUNTER1	EI_SHIFT(0x0e)	/* Rcv CRC error counter RD */
#define EN0_IMR		EI_SHIFT(0x0f)	/* Interrupt mask reg WR */
#define EN0_COUNTER2	EI_SHIFT(0x0f)	/* Rcv missed frame error counter RD */

/* Bits in EN0_ISR - Interrupt status register */
#define ENISR_RX	0x01	/* Receiver, no error */
#define ENISR_TX	0x02	/* Transmitter, no error */
#define ENISR_RX_ERR	0x04	/* Receiver, with error */
#define ENISR_TX_ERR	0x08	/* Transmitter, with error */
#define ENISR_OVER	0x10	/* Receiver overwrote the ring */
#define ENISR_COUNTERS	0x20	/* Counters need emptying */
#define ENISR_RDC	0x40	/* remote dma complete */
#define ENISR_RESET	0x80	/* Reset completed */
#define ENISR_ALL	0x3f	/* Interrupts we will enable */

/* Bits in EN0_DCFG - Data config register */
#define ENDCFG_WTS	0x01	/* word transfer mode selection */
#define ENDCFG_BOS	0x02	/* byte order selection */

/* Page 1 register offsets. */
#define EN1_PHYS   EI_SHIFT(0x01)	/* This board's physical enet addr RD WR */
#define EN1_PHYS_SHIFT(i)  EI_SHIFT(i+1) /* Get and set mac address */
#define EN1_CURPAG EI_SHIFT(0x07)	/* Current memory page RD WR */
#define EN1_MULT   EI_SHIFT(0x08)	/* Multicast filter mask array (8 bytes) RD WR */
#define EN1_MULT_SHIFT(i)  EI_SHIFT(8+i) /* Get and set multicast filter */

/* Bits in received packet status byte and EN0_RSR*/
#define ENRSR_RXOK	0x01	/* Received a good packet */
#define ENRSR_CRC	0x02	/* CRC error */
#define ENRSR_FAE	0x04	/* frame alignment error */
#define ENRSR_FO	0x08	/* FIFO overrun */
#define ENRSR_MPA	0x10	/* missed pkt */
#define ENRSR_PHY	0x20	/* physical/multicast address */
#define ENRSR_DIS	0x40	/* receiver disable. set in monitor mode */
#define ENRSR_DEF	0x80	/* deferring */

/* Transmitted packet status, EN0_TSR. */
#define ENTSR_PTX 0x01	/* Packet transmitted without error */
#define ENTSR_ND  0x02	/* The transmit wasn't deferred. */
#define ENTSR_COL 0x04	/* The transmit collided at least once. */
#define ENTSR_ABT 0x08  /* The transmit collided 16 times, and was deferred. */
#define ENTSR_CRS 0x10	/* The carrier sense was lost. */
#define ENTSR_FU  0x20  /* A "FIFO underrun" occurred during transmit. */
#define ENTSR_CDH 0x40	/* The collision detect "heartbeat" signal was lost. */
#define ENTSR_OWC 0x80  /* There was an out-of-window collision. */

#endif /* _8390_h */
