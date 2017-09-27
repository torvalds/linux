#ifndef B43legacy_PIO_H_
#define B43legacy_PIO_H_

#include "b43legacy.h"

#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/skbuff.h>


#define B43legacy_PIO_TXCTL		0x00
#define B43legacy_PIO_TXDATA		0x02
#define B43legacy_PIO_TXQBUFSIZE	0x04
#define B43legacy_PIO_RXCTL		0x08
#define B43legacy_PIO_RXDATA		0x0A

#define B43legacy_PIO_TXCTL_WRITELO	(1 << 0)
#define B43legacy_PIO_TXCTL_WRITEHI	(1 << 1)
#define B43legacy_PIO_TXCTL_COMPLETE	(1 << 2)
#define B43legacy_PIO_TXCTL_INIT	(1 << 3)
#define B43legacy_PIO_TXCTL_SUSPEND	(1 << 7)

#define B43legacy_PIO_RXCTL_DATAAVAILABLE	(1 << 0)
#define B43legacy_PIO_RXCTL_READY		(1 << 1)

/* PIO constants */
#define B43legacy_PIO_MAXTXDEVQPACKETS	31
#define B43legacy_PIO_TXQADJUST		80

/* PIO tuning knobs */
#define B43legacy_PIO_MAXTXPACKETS	256



#ifdef CONFIG_B43LEGACY_PIO


struct b43legacy_pioqueue;
struct b43legacy_xmitstatus;

struct b43legacy_pio_txpacket {
	struct b43legacy_pioqueue *queue;
	struct sk_buff *skb;
	struct list_head list;
};

#define pio_txpacket_getindex(packet) ((int)((packet) - \
			      (packet)->queue->tx_packets_cache))

struct b43legacy_pioqueue {
	struct b43legacy_wldev *dev;
	u16 mmio_base;

	bool tx_suspended;
	bool tx_frozen;
	bool need_workarounds; /* Workarounds needed for core.rev < 3 */

	/* Adjusted size of the device internal TX buffer. */
	u16 tx_devq_size;
	/* Used octets of the device internal TX buffer. */
	u16 tx_devq_used;
	/* Used packet slots in the device internal TX buffer. */
	u8 tx_devq_packets;
	/* Packets from the txfree list can
	 * be taken on incoming TX requests.
	 */
	struct list_head txfree;
	unsigned int nr_txfree;
	/* Packets on the txqueue are queued,
	 * but not completely written to the chip, yet.
	 */
	struct list_head txqueue;
	/* Packets on the txrunning queue are completely
	 * posted to the device. We are waiting for the txstatus.
	 */
	struct list_head txrunning;
	struct tasklet_struct txtask;
	struct b43legacy_pio_txpacket
			 tx_packets_cache[B43legacy_PIO_MAXTXPACKETS];
};

static inline
u16 b43legacy_pio_read(struct b43legacy_pioqueue *queue,
		     u16 offset)
{
	return b43legacy_read16(queue->dev, queue->mmio_base + offset);
}

static inline
void b43legacy_pio_write(struct b43legacy_pioqueue *queue,
		       u16 offset, u16 value)
{
	b43legacy_write16(queue->dev, queue->mmio_base + offset, value);
	mmiowb();
}


int b43legacy_pio_init(struct b43legacy_wldev *dev);
void b43legacy_pio_free(struct b43legacy_wldev *dev);

int b43legacy_pio_tx(struct b43legacy_wldev *dev,
		   struct sk_buff *skb);
void b43legacy_pio_handle_txstatus(struct b43legacy_wldev *dev,
				 const struct b43legacy_txstatus *status);
void b43legacy_pio_rx(struct b43legacy_pioqueue *queue);

/* Suspend TX queue in hardware. */
void b43legacy_pio_tx_suspend(struct b43legacy_pioqueue *queue);
void b43legacy_pio_tx_resume(struct b43legacy_pioqueue *queue);
/* Suspend (freeze) the TX tasklet (software level). */
void b43legacy_pio_freeze_txqueues(struct b43legacy_wldev *dev);
void b43legacy_pio_thaw_txqueues(struct b43legacy_wldev *dev);

#else /* CONFIG_B43LEGACY_PIO */

static inline
int b43legacy_pio_init(struct b43legacy_wldev *dev)
{
	return 0;
}
static inline
void b43legacy_pio_free(struct b43legacy_wldev *dev)
{
}
static inline
int b43legacy_pio_tx(struct b43legacy_wldev *dev,
		   struct sk_buff *skb)
{
	return 0;
}
static inline
void b43legacy_pio_handle_txstatus(struct b43legacy_wldev *dev,
				 const struct b43legacy_txstatus *status)
{
}
static inline
void b43legacy_pio_rx(struct b43legacy_pioqueue *queue)
{
}
static inline
void b43legacy_pio_tx_suspend(struct b43legacy_pioqueue *queue)
{
}
static inline
void b43legacy_pio_tx_resume(struct b43legacy_pioqueue *queue)
{
}
static inline
void b43legacy_pio_freeze_txqueues(struct b43legacy_wldev *dev)
{
}
static inline
void b43legacy_pio_thaw_txqueues(struct b43legacy_wldev *dev)
{
}

#endif /* CONFIG_B43LEGACY_PIO */
#endif /* B43legacy_PIO_H_ */
