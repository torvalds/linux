#ifndef BCM43xx_DMA_H_
#define BCM43xx_DMA_H_

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/linkage.h>
#include <asm/atomic.h>


/* DMA-Interrupt reasons. */
#define BCM43xx_DMAIRQ_FATALMASK	((1 << 10) | (1 << 11) | (1 << 12) \
					 | (1 << 14) | (1 << 15))
#define BCM43xx_DMAIRQ_NONFATALMASK	(1 << 13)
#define BCM43xx_DMAIRQ_RX_DONE		(1 << 16)

/* DMA controller register offsets. (relative to BCM43xx_DMA#_BASE) */
#define BCM43xx_DMA_TX_CONTROL		0x00
#define BCM43xx_DMA_TX_DESC_RING	0x04
#define BCM43xx_DMA_TX_DESC_INDEX	0x08
#define BCM43xx_DMA_TX_STATUS		0x0c
#define BCM43xx_DMA_RX_CONTROL		0x10
#define BCM43xx_DMA_RX_DESC_RING	0x14
#define BCM43xx_DMA_RX_DESC_INDEX	0x18
#define BCM43xx_DMA_RX_STATUS		0x1c

/* DMA controller channel control word values. */
#define BCM43xx_DMA_TXCTRL_ENABLE		(1 << 0)
#define BCM43xx_DMA_TXCTRL_SUSPEND		(1 << 1)
#define BCM43xx_DMA_TXCTRL_LOOPBACK		(1 << 2)
#define BCM43xx_DMA_TXCTRL_FLUSH		(1 << 4)
#define BCM43xx_DMA_RXCTRL_ENABLE		(1 << 0)
#define BCM43xx_DMA_RXCTRL_FRAMEOFF_MASK	0x000000fe
#define BCM43xx_DMA_RXCTRL_FRAMEOFF_SHIFT	1
#define BCM43xx_DMA_RXCTRL_PIO			(1 << 8)
/* DMA controller channel status word values. */
#define BCM43xx_DMA_TXSTAT_DPTR_MASK		0x00000fff
#define BCM43xx_DMA_TXSTAT_STAT_MASK		0x0000f000
#define BCM43xx_DMA_TXSTAT_STAT_DISABLED	0x00000000
#define BCM43xx_DMA_TXSTAT_STAT_ACTIVE		0x00001000
#define BCM43xx_DMA_TXSTAT_STAT_IDLEWAIT	0x00002000
#define BCM43xx_DMA_TXSTAT_STAT_STOPPED		0x00003000
#define BCM43xx_DMA_TXSTAT_STAT_SUSP		0x00004000
#define BCM43xx_DMA_TXSTAT_ERROR_MASK		0x000f0000
#define BCM43xx_DMA_TXSTAT_FLUSHED		(1 << 20)
#define BCM43xx_DMA_RXSTAT_DPTR_MASK		0x00000fff
#define BCM43xx_DMA_RXSTAT_STAT_MASK		0x0000f000
#define BCM43xx_DMA_RXSTAT_STAT_DISABLED	0x00000000
#define BCM43xx_DMA_RXSTAT_STAT_ACTIVE		0x00001000
#define BCM43xx_DMA_RXSTAT_STAT_IDLEWAIT	0x00002000
#define BCM43xx_DMA_RXSTAT_STAT_RESERVED	0x00003000
#define BCM43xx_DMA_RXSTAT_STAT_ERRORS		0x00004000
#define BCM43xx_DMA_RXSTAT_ERROR_MASK		0x000f0000

/* DMA descriptor control field values. */
#define BCM43xx_DMADTOR_BYTECNT_MASK		0x00001fff
#define BCM43xx_DMADTOR_DTABLEEND		(1 << 28) /* End of descriptor table */
#define BCM43xx_DMADTOR_COMPIRQ			(1 << 29) /* IRQ on completion request */
#define BCM43xx_DMADTOR_FRAMEEND		(1 << 30)
#define BCM43xx_DMADTOR_FRAMESTART		(1 << 31)

/* Misc DMA constants */
#define BCM43xx_DMA_RINGMEMSIZE		PAGE_SIZE
#define BCM43xx_DMA_BUSADDRMAX		0x3FFFFFFF
#define BCM43xx_DMA_DMABUSADDROFFSET	(1 << 30)
#define BCM43xx_DMA1_RX_FRAMEOFFSET	30
#define BCM43xx_DMA4_RX_FRAMEOFFSET	0

/* DMA engine tuning knobs */
#define BCM43xx_TXRING_SLOTS		512
#define BCM43xx_RXRING_SLOTS		64
#define BCM43xx_DMA1_RXBUFFERSIZE	(2304 + 100)
#define BCM43xx_DMA4_RXBUFFERSIZE	16
/* Suspend the tx queue, if less than this percent slots are free. */
#define BCM43xx_TXSUSPEND_PERCENT	20
/* Resume the tx queue, if more than this percent slots are free. */
#define BCM43xx_TXRESUME_PERCENT	50



#ifdef CONFIG_BCM43XX_DMA


struct sk_buff;
struct bcm43xx_private;
struct bcm43xx_xmitstatus;


struct bcm43xx_dmadesc {
	__le32 _control;
	__le32 _address;
} __attribute__((__packed__));

/* Macros to access the bcm43xx_dmadesc struct */
#define get_desc_ctl(desc)		le32_to_cpu((desc)->_control)
#define set_desc_ctl(desc, ctl)		do { (desc)->_control = cpu_to_le32(ctl); } while (0)
#define get_desc_addr(desc)		le32_to_cpu((desc)->_address)
#define set_desc_addr(desc, addr)	do { (desc)->_address = cpu_to_le32(addr); } while (0)

struct bcm43xx_dmadesc_meta {
	/* The kernel DMA-able buffer. */
	struct sk_buff *skb;
	/* DMA base bus-address of the descriptor buffer. */
	dma_addr_t dmaaddr;
};

struct bcm43xx_dmaring {
	struct bcm43xx_private *bcm;
	/* Kernel virtual base address of the ring memory. */
	struct bcm43xx_dmadesc *vbase;
	/* DMA memory offset */
	dma_addr_t memoffset;
	/* (Unadjusted) DMA base bus-address of the ring memory. */
	dma_addr_t dmabase;
	/* Meta data about all descriptors. */
	struct bcm43xx_dmadesc_meta *meta;
	/* Number of descriptor slots in the ring. */
	int nr_slots;
	/* Number of used descriptor slots. */
	int used_slots;
	/* Currently used slot in the ring. */
	int current_slot;
	/* Marks to suspend/resume the queue. */
	int suspend_mark;
	int resume_mark;
	/* Frameoffset in octets. */
	u32 frameoffset;
	/* Descriptor buffer size. */
	u16 rx_buffersize;
	/* The MMIO base register of the DMA controller, this
	 * ring is posted to.
	 */
	u16 mmio_base;
	u8 tx:1,	/* TRUE, if this is a TX ring. */
	   suspended:1;	/* TRUE, if transfers are suspended on this ring. */
#ifdef CONFIG_BCM43XX_DEBUG
	/* Maximum number of used slots. */
	int max_used_slots;
#endif /* CONFIG_BCM43XX_DEBUG*/
};


static inline
u32 bcm43xx_dma_read(struct bcm43xx_dmaring *ring,
		     u16 offset)
{
	return bcm43xx_read32(ring->bcm, ring->mmio_base + offset);
}

static inline
void bcm43xx_dma_write(struct bcm43xx_dmaring *ring,
		       u16 offset, u32 value)
{
	bcm43xx_write32(ring->bcm, ring->mmio_base + offset, value);
}


int bcm43xx_dma_init(struct bcm43xx_private *bcm);
void bcm43xx_dma_free(struct bcm43xx_private *bcm);

int bcm43xx_dmacontroller_rx_reset(struct bcm43xx_private *bcm,
				   u16 dmacontroller_mmio_base);
int bcm43xx_dmacontroller_tx_reset(struct bcm43xx_private *bcm,
				   u16 dmacontroller_mmio_base);

void bcm43xx_dma_tx_suspend(struct bcm43xx_dmaring *ring);
void bcm43xx_dma_tx_resume(struct bcm43xx_dmaring *ring);

void bcm43xx_dma_handle_xmitstatus(struct bcm43xx_private *bcm,
				   struct bcm43xx_xmitstatus *status);

int bcm43xx_dma_tx(struct bcm43xx_private *bcm,
		   struct ieee80211_txb *txb);
void bcm43xx_dma_rx(struct bcm43xx_dmaring *ring);


#else /* CONFIG_BCM43XX_DMA */


static inline
int bcm43xx_dma_init(struct bcm43xx_private *bcm)
{
	return 0;
}
static inline
void bcm43xx_dma_free(struct bcm43xx_private *bcm)
{
}
static inline
int bcm43xx_dmacontroller_rx_reset(struct bcm43xx_private *bcm,
				   u16 dmacontroller_mmio_base)
{
	return 0;
}
static inline
int bcm43xx_dmacontroller_tx_reset(struct bcm43xx_private *bcm,
				   u16 dmacontroller_mmio_base)
{
	return 0;
}
static inline
int bcm43xx_dma_tx(struct bcm43xx_private *bcm,
		   struct ieee80211_txb *txb)
{
	return 0;
}
static inline
void bcm43xx_dma_handle_xmitstatus(struct bcm43xx_private *bcm,
				   struct bcm43xx_xmitstatus *status)
{
}
static inline
void bcm43xx_dma_rx(struct bcm43xx_dmaring *ring)
{
}

#endif /* CONFIG_BCM43XX_DMA */
#endif /* BCM43xx_DMA_H_ */
