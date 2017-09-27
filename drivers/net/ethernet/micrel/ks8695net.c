/*
 * Micrel KS8695 (Centaur) Ethernet.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * Copyright 2008 Simtec Electronics
 *		  Daniel Silverstone <dsilvers@simtec.co.uk>
 *		  Vincent Sanders <vince@simtec.co.uk>
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <asm/irq.h>

#include <mach/regs-switch.h>
#include <mach/regs-misc.h>
#include <asm/mach/irq.h>
#include <mach/regs-irq.h>

#include "ks8695net.h"

#define MODULENAME	"ks8695_ether"
#define MODULEVERSION	"1.02"

/*
 * Transmit and device reset timeout, default 5 seconds.
 */
static int watchdog = 5000;

/* Hardware structures */

/**
 *	struct rx_ring_desc - Receive descriptor ring element
 *	@status: The status of the descriptor element (E.g. who owns it)
 *	@length: The number of bytes in the block pointed to by data_ptr
 *	@data_ptr: The physical address of the data block to receive into
 *	@next_desc: The physical address of the next descriptor element.
 */
struct rx_ring_desc {
	__le32	status;
	__le32	length;
	__le32	data_ptr;
	__le32	next_desc;
};

/**
 *	struct tx_ring_desc - Transmit descriptor ring element
 *	@owner: Who owns the descriptor
 *	@status: The number of bytes in the block pointed to by data_ptr
 *	@data_ptr: The physical address of the data block to receive into
 *	@next_desc: The physical address of the next descriptor element.
 */
struct tx_ring_desc {
	__le32	owner;
	__le32	status;
	__le32	data_ptr;
	__le32	next_desc;
};

/**
 *	struct ks8695_skbuff - sk_buff wrapper for rx/tx rings.
 *	@skb: The buffer in the ring
 *	@dma_ptr: The mapped DMA pointer of the buffer
 *	@length: The number of bytes mapped to dma_ptr
 */
struct ks8695_skbuff {
	struct sk_buff	*skb;
	dma_addr_t	dma_ptr;
	u32		length;
};

/* Private device structure */

#define MAX_TX_DESC 8
#define MAX_TX_DESC_MASK 0x7
#define MAX_RX_DESC 16
#define MAX_RX_DESC_MASK 0xf

/*napi_weight have better more than rx DMA buffers*/
#define NAPI_WEIGHT   64

#define MAX_RXBUF_SIZE 0x700

#define TX_RING_DMA_SIZE (sizeof(struct tx_ring_desc) * MAX_TX_DESC)
#define RX_RING_DMA_SIZE (sizeof(struct rx_ring_desc) * MAX_RX_DESC)
#define RING_DMA_SIZE (TX_RING_DMA_SIZE + RX_RING_DMA_SIZE)

/**
 *	enum ks8695_dtype - Device type
 *	@KS8695_DTYPE_WAN: This device is a WAN interface
 *	@KS8695_DTYPE_LAN: This device is a LAN interface
 *	@KS8695_DTYPE_HPNA: This device is an HPNA interface
 */
enum ks8695_dtype {
	KS8695_DTYPE_WAN,
	KS8695_DTYPE_LAN,
	KS8695_DTYPE_HPNA,
};

/**
 *	struct ks8695_priv - Private data for the KS8695 Ethernet
 *	@in_suspend: Flag to indicate if we're suspending/resuming
 *	@ndev: The net_device for this interface
 *	@dev: The platform device object for this interface
 *	@dtype: The type of this device
 *	@io_regs: The ioremapped registers for this interface
 *      @napi : Add support NAPI for Rx
 *	@rx_irq_name: The textual name of the RX IRQ from the platform data
 *	@tx_irq_name: The textual name of the TX IRQ from the platform data
 *	@link_irq_name: The textual name of the link IRQ from the
 *			platform data if available
 *	@rx_irq: The IRQ number for the RX IRQ
 *	@tx_irq: The IRQ number for the TX IRQ
 *	@link_irq: The IRQ number for the link IRQ if available
 *	@regs_req: The resource request for the registers region
 *	@phyiface_req: The resource request for the phy/switch region
 *		       if available
 *	@phyiface_regs: The ioremapped registers for the phy/switch if available
 *	@ring_base: The base pointer of the dma coherent memory for the rings
 *	@ring_base_dma: The DMA mapped equivalent of ring_base
 *	@tx_ring: The pointer in ring_base of the TX ring
 *	@tx_ring_used: The number of slots in the TX ring which are occupied
 *	@tx_ring_next_slot: The next slot to fill in the TX ring
 *	@tx_ring_dma: The DMA mapped equivalent of tx_ring
 *	@tx_buffers: The sk_buff mappings for the TX ring
 *	@txq_lock: A lock to protect the tx_buffers tx_ring_used etc variables
 *	@rx_ring: The pointer in ring_base of the RX ring
 *	@rx_ring_dma: The DMA mapped equivalent of rx_ring
 *	@rx_buffers: The sk_buff mappings for the RX ring
 *	@next_rx_desc_read: The next RX descriptor to read from on IRQ
 *      @rx_lock: A lock to protect Rx irq function
 *	@msg_enable: The flags for which messages to emit
 */
struct ks8695_priv {
	int in_suspend;
	struct net_device *ndev;
	struct device *dev;
	enum ks8695_dtype dtype;
	void __iomem *io_regs;

	struct napi_struct	napi;

	const char *rx_irq_name, *tx_irq_name, *link_irq_name;
	int rx_irq, tx_irq, link_irq;

	struct resource *regs_req, *phyiface_req;
	void __iomem *phyiface_regs;

	void *ring_base;
	dma_addr_t ring_base_dma;

	struct tx_ring_desc *tx_ring;
	int tx_ring_used;
	int tx_ring_next_slot;
	dma_addr_t tx_ring_dma;
	struct ks8695_skbuff tx_buffers[MAX_TX_DESC];
	spinlock_t txq_lock;

	struct rx_ring_desc *rx_ring;
	dma_addr_t rx_ring_dma;
	struct ks8695_skbuff rx_buffers[MAX_RX_DESC];
	int next_rx_desc_read;
	spinlock_t rx_lock;

	int msg_enable;
};

/* Register access */

/**
 *	ks8695_readreg - Read from a KS8695 ethernet register
 *	@ksp: The device to read from
 *	@reg: The register to read
 */
static inline u32
ks8695_readreg(struct ks8695_priv *ksp, int reg)
{
	return readl(ksp->io_regs + reg);
}

/**
 *	ks8695_writereg - Write to a KS8695 ethernet register
 *	@ksp: The device to write to
 *	@reg: The register to write
 *	@value: The value to write to the register
 */
static inline void
ks8695_writereg(struct ks8695_priv *ksp, int reg, u32 value)
{
	writel(value, ksp->io_regs + reg);
}

/* Utility functions */

/**
 *	ks8695_port_type - Retrieve port-type as user-friendly string
 *	@ksp: The device to return the type for
 *
 *	Returns a string indicating which of the WAN, LAN or HPNA
 *	ports this device is likely to represent.
 */
static const char *
ks8695_port_type(struct ks8695_priv *ksp)
{
	switch (ksp->dtype) {
	case KS8695_DTYPE_LAN:
		return "LAN";
	case KS8695_DTYPE_WAN:
		return "WAN";
	case KS8695_DTYPE_HPNA:
		return "HPNA";
	}

	return "UNKNOWN";
}

/**
 *	ks8695_update_mac - Update the MAC registers in the device
 *	@ksp: The device to update
 *
 *	Updates the MAC registers in the KS8695 device from the address in the
 *	net_device structure associated with this interface.
 */
static void
ks8695_update_mac(struct ks8695_priv *ksp)
{
	/* Update the HW with the MAC from the net_device */
	struct net_device *ndev = ksp->ndev;
	u32 machigh, maclow;

	maclow	= ((ndev->dev_addr[2] << 24) | (ndev->dev_addr[3] << 16) |
		   (ndev->dev_addr[4] <<  8) | (ndev->dev_addr[5] <<  0));
	machigh = ((ndev->dev_addr[0] <<  8) | (ndev->dev_addr[1] <<  0));

	ks8695_writereg(ksp, KS8695_MAL, maclow);
	ks8695_writereg(ksp, KS8695_MAH, machigh);

}

/**
 *	ks8695_refill_rxbuffers - Re-fill the RX buffer ring
 *	@ksp: The device to refill
 *
 *	Iterates the RX ring of the device looking for empty slots.
 *	For each empty slot, we allocate and map a new SKB and give it
 *	to the hardware.
 *	This can be called from interrupt context safely.
 */
static void
ks8695_refill_rxbuffers(struct ks8695_priv *ksp)
{
	/* Run around the RX ring, filling in any missing sk_buff's */
	int buff_n;

	for (buff_n = 0; buff_n < MAX_RX_DESC; ++buff_n) {
		if (!ksp->rx_buffers[buff_n].skb) {
			struct sk_buff *skb =
				netdev_alloc_skb(ksp->ndev, MAX_RXBUF_SIZE);
			dma_addr_t mapping;

			ksp->rx_buffers[buff_n].skb = skb;
			if (skb == NULL) {
				/* Failed to allocate one, perhaps
				 * we'll try again later.
				 */
				break;
			}

			mapping = dma_map_single(ksp->dev, skb->data,
						 MAX_RXBUF_SIZE,
						 DMA_FROM_DEVICE);
			if (unlikely(dma_mapping_error(ksp->dev, mapping))) {
				/* Failed to DMA map this SKB, try later */
				dev_kfree_skb_irq(skb);
				ksp->rx_buffers[buff_n].skb = NULL;
				break;
			}
			ksp->rx_buffers[buff_n].dma_ptr = mapping;
			ksp->rx_buffers[buff_n].length = MAX_RXBUF_SIZE;

			/* Record this into the DMA ring */
			ksp->rx_ring[buff_n].data_ptr = cpu_to_le32(mapping);
			ksp->rx_ring[buff_n].length =
				cpu_to_le32(MAX_RXBUF_SIZE);

			wmb();

			/* And give ownership over to the hardware */
			ksp->rx_ring[buff_n].status = cpu_to_le32(RDES_OWN);
		}
	}
}

/* Maximum number of multicast addresses which the KS8695 HW supports */
#define KS8695_NR_ADDRESSES	16

/**
 *	ks8695_init_partial_multicast - Init the mcast addr registers
 *	@ksp: The device to initialise
 *	@addr: The multicast address list to use
 *	@nr_addr: The number of addresses in the list
 *
 *	This routine is a helper for ks8695_set_multicast - it writes
 *	the additional-address registers in the KS8695 ethernet device
 *	and cleans up any others left behind.
 */
static void
ks8695_init_partial_multicast(struct ks8695_priv *ksp,
			      struct net_device *ndev)
{
	u32 low, high;
	int i;
	struct netdev_hw_addr *ha;

	i = 0;
	netdev_for_each_mc_addr(ha, ndev) {
		/* Ran out of space in chip? */
		BUG_ON(i == KS8695_NR_ADDRESSES);

		low = (ha->addr[2] << 24) | (ha->addr[3] << 16) |
		      (ha->addr[4] << 8) | (ha->addr[5]);
		high = (ha->addr[0] << 8) | (ha->addr[1]);

		ks8695_writereg(ksp, KS8695_AAL_(i), low);
		ks8695_writereg(ksp, KS8695_AAH_(i), AAH_E | high);
		i++;
	}

	/* Clear the remaining Additional Station Addresses */
	for (; i < KS8695_NR_ADDRESSES; i++) {
		ks8695_writereg(ksp, KS8695_AAL_(i), 0);
		ks8695_writereg(ksp, KS8695_AAH_(i), 0);
	}
}

/* Interrupt handling */

/**
 *	ks8695_tx_irq - Transmit IRQ handler
 *	@irq: The IRQ which went off (ignored)
 *	@dev_id: The net_device for the interrupt
 *
 *	Process the TX ring, clearing out any transmitted slots.
 *	Allows the net_device to pass us new packets once slots are
 *	freed.
 */
static irqreturn_t
ks8695_tx_irq(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct ks8695_priv *ksp = netdev_priv(ndev);
	int buff_n;

	for (buff_n = 0; buff_n < MAX_TX_DESC; ++buff_n) {
		if (ksp->tx_buffers[buff_n].skb &&
		    !(ksp->tx_ring[buff_n].owner & cpu_to_le32(TDES_OWN))) {
			rmb();
			/* An SKB which is not owned by HW is present */
			/* Update the stats for the net_device */
			ndev->stats.tx_packets++;
			ndev->stats.tx_bytes += ksp->tx_buffers[buff_n].length;

			/* Free the packet from the ring */
			ksp->tx_ring[buff_n].data_ptr = 0;

			/* Free the sk_buff */
			dma_unmap_single(ksp->dev,
					 ksp->tx_buffers[buff_n].dma_ptr,
					 ksp->tx_buffers[buff_n].length,
					 DMA_TO_DEVICE);
			dev_kfree_skb_irq(ksp->tx_buffers[buff_n].skb);
			ksp->tx_buffers[buff_n].skb = NULL;
			ksp->tx_ring_used--;
		}
	}

	netif_wake_queue(ndev);

	return IRQ_HANDLED;
}

/**
 *	ks8695_get_rx_enable_bit - Get rx interrupt enable/status bit
 *	@ksp: Private data for the KS8695 Ethernet
 *
 *    For KS8695 document:
 *    Interrupt Enable Register (offset 0xE204)
 *        Bit29 : WAN MAC Receive Interrupt Enable
 *        Bit16 : LAN MAC Receive Interrupt Enable
 *    Interrupt Status Register (Offset 0xF208)
 *        Bit29: WAN MAC Receive Status
 *        Bit16: LAN MAC Receive Status
 *    So, this Rx interrupt enable/status bit number is equal
 *    as Rx IRQ number.
 */
static inline u32 ks8695_get_rx_enable_bit(struct ks8695_priv *ksp)
{
	return ksp->rx_irq;
}

/**
 *	ks8695_rx_irq - Receive IRQ handler
 *	@irq: The IRQ which went off (ignored)
 *	@dev_id: The net_device for the interrupt
 *
 *	Inform NAPI that packet reception needs to be scheduled
 */

static irqreturn_t
ks8695_rx_irq(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct ks8695_priv *ksp = netdev_priv(ndev);

	spin_lock(&ksp->rx_lock);

	if (napi_schedule_prep(&ksp->napi)) {
		unsigned long status = readl(KS8695_IRQ_VA + KS8695_INTEN);
		unsigned long mask_bit = 1 << ks8695_get_rx_enable_bit(ksp);
		/*disable rx interrupt*/
		status &= ~mask_bit;
		writel(status , KS8695_IRQ_VA + KS8695_INTEN);
		__napi_schedule(&ksp->napi);
	}

	spin_unlock(&ksp->rx_lock);
	return IRQ_HANDLED;
}

/**
 *	ks8695_rx - Receive packets called by NAPI poll method
 *	@ksp: Private data for the KS8695 Ethernet
 *	@budget: Number of packets allowed to process
 */
static int ks8695_rx(struct ks8695_priv *ksp, int budget)
{
	struct net_device *ndev = ksp->ndev;
	struct sk_buff *skb;
	int buff_n;
	u32 flags;
	int pktlen;
	int received = 0;

	buff_n = ksp->next_rx_desc_read;
	while (received < budget
			&& ksp->rx_buffers[buff_n].skb
			&& (!(ksp->rx_ring[buff_n].status &
					cpu_to_le32(RDES_OWN)))) {
			rmb();
			flags = le32_to_cpu(ksp->rx_ring[buff_n].status);

			/* Found an SKB which we own, this means we
			 * received a packet
			 */
			if ((flags & (RDES_FS | RDES_LS)) !=
			    (RDES_FS | RDES_LS)) {
				/* This packet is not the first and
				 * the last segment.  Therefore it is
				 * a "spanning" packet and we can't
				 * handle it
				 */
				goto rx_failure;
			}

			if (flags & (RDES_ES | RDES_RE)) {
				/* It's an error packet */
				ndev->stats.rx_errors++;
				if (flags & RDES_TL)
					ndev->stats.rx_length_errors++;
				if (flags & RDES_RF)
					ndev->stats.rx_length_errors++;
				if (flags & RDES_CE)
					ndev->stats.rx_crc_errors++;
				if (flags & RDES_RE)
					ndev->stats.rx_missed_errors++;

				goto rx_failure;
			}

			pktlen = flags & RDES_FLEN;
			pktlen -= 4; /* Drop the CRC */

			/* Retrieve the sk_buff */
			skb = ksp->rx_buffers[buff_n].skb;

			/* Clear it from the ring */
			ksp->rx_buffers[buff_n].skb = NULL;
			ksp->rx_ring[buff_n].data_ptr = 0;

			/* Unmap the SKB */
			dma_unmap_single(ksp->dev,
					 ksp->rx_buffers[buff_n].dma_ptr,
					 ksp->rx_buffers[buff_n].length,
					 DMA_FROM_DEVICE);

			/* Relinquish the SKB to the network layer */
			skb_put(skb, pktlen);
			skb->protocol = eth_type_trans(skb, ndev);
			napi_gro_receive(&ksp->napi, skb);

			/* Record stats */
			ndev->stats.rx_packets++;
			ndev->stats.rx_bytes += pktlen;
			goto rx_finished;

rx_failure:
			/* This ring entry is an error, but we can
			 * re-use the skb
			 */
			/* Give the ring entry back to the hardware */
			ksp->rx_ring[buff_n].status = cpu_to_le32(RDES_OWN);
rx_finished:
			received++;
			buff_n = (buff_n + 1) & MAX_RX_DESC_MASK;
	}

	/* And note which RX descriptor we last did */
	ksp->next_rx_desc_read = buff_n;

	/* And refill the buffers */
	ks8695_refill_rxbuffers(ksp);

	/* Kick the RX DMA engine, in case it became suspended */
	ks8695_writereg(ksp, KS8695_DRSC, 0);

	return received;
}


/**
 *	ks8695_poll - Receive packet by NAPI poll method
 *	@ksp: Private data for the KS8695 Ethernet
 *	@budget: The remaining number packets for network subsystem
 *
 *     Invoked by the network core when it requests for new
 *     packets from the driver
 */
static int ks8695_poll(struct napi_struct *napi, int budget)
{
	struct ks8695_priv *ksp = container_of(napi, struct ks8695_priv, napi);
	unsigned long isr = readl(KS8695_IRQ_VA + KS8695_INTEN);
	unsigned long mask_bit = 1 << ks8695_get_rx_enable_bit(ksp);
	int work_done;

	work_done = ks8695_rx(ksp, budget);

	if (work_done < budget && napi_complete_done(napi, work_done)) {
		unsigned long flags;

		spin_lock_irqsave(&ksp->rx_lock, flags);
		/* enable rx interrupt */
		writel(isr | mask_bit, KS8695_IRQ_VA + KS8695_INTEN);
		spin_unlock_irqrestore(&ksp->rx_lock, flags);
	}
	return work_done;
}

/**
 *	ks8695_link_irq - Link change IRQ handler
 *	@irq: The IRQ which went off (ignored)
 *	@dev_id: The net_device for the interrupt
 *
 *	The WAN interface can generate an IRQ when the link changes,
 *	report this to the net layer and the user.
 */
static irqreturn_t
ks8695_link_irq(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct ks8695_priv *ksp = netdev_priv(ndev);
	u32 ctrl;

	ctrl = readl(ksp->phyiface_regs + KS8695_WMC);
	if (ctrl & WMC_WLS) {
		netif_carrier_on(ndev);
		if (netif_msg_link(ksp))
			dev_info(ksp->dev,
				 "%s: Link is now up (10%sMbps/%s-duplex)\n",
				 ndev->name,
				 (ctrl & WMC_WSS) ? "0" : "",
				 (ctrl & WMC_WDS) ? "Full" : "Half");
	} else {
		netif_carrier_off(ndev);
		if (netif_msg_link(ksp))
			dev_info(ksp->dev, "%s: Link is now down.\n",
				 ndev->name);
	}

	return IRQ_HANDLED;
}


/* KS8695 Device functions */

/**
 *	ks8695_reset - Reset a KS8695 ethernet interface
 *	@ksp: The interface to reset
 *
 *	Perform an engine reset of the interface and re-program it
 *	with sensible defaults.
 */
static void
ks8695_reset(struct ks8695_priv *ksp)
{
	int reset_timeout = watchdog;
	/* Issue the reset via the TX DMA control register */
	ks8695_writereg(ksp, KS8695_DTXC, DTXC_TRST);
	while (reset_timeout--) {
		if (!(ks8695_readreg(ksp, KS8695_DTXC) & DTXC_TRST))
			break;
		msleep(1);
	}

	if (reset_timeout < 0) {
		dev_crit(ksp->dev,
			 "Timeout waiting for DMA engines to reset\n");
		/* And blithely carry on */
	}

	/* Definitely wait long enough before attempting to program
	 * the engines
	 */
	msleep(10);

	/* RX: unicast and broadcast */
	ks8695_writereg(ksp, KS8695_DRXC, DRXC_RU | DRXC_RB);
	/* TX: pad and add CRC */
	ks8695_writereg(ksp, KS8695_DTXC, DTXC_TEP | DTXC_TAC);
}

/**
 *	ks8695_shutdown - Shut down a KS8695 ethernet interface
 *	@ksp: The interface to shut down
 *
 *	This disables packet RX/TX, cleans up IRQs, drains the rings,
 *	and basically places the interface into a clean shutdown
 *	state.
 */
static void
ks8695_shutdown(struct ks8695_priv *ksp)
{
	u32 ctrl;
	int buff_n;

	/* Disable packet transmission */
	ctrl = ks8695_readreg(ksp, KS8695_DTXC);
	ks8695_writereg(ksp, KS8695_DTXC, ctrl & ~DTXC_TE);

	/* Disable packet reception */
	ctrl = ks8695_readreg(ksp, KS8695_DRXC);
	ks8695_writereg(ksp, KS8695_DRXC, ctrl & ~DRXC_RE);

	/* Release the IRQs */
	free_irq(ksp->rx_irq, ksp->ndev);
	free_irq(ksp->tx_irq, ksp->ndev);
	if (ksp->link_irq != -1)
		free_irq(ksp->link_irq, ksp->ndev);

	/* Throw away any pending TX packets */
	for (buff_n = 0; buff_n < MAX_TX_DESC; ++buff_n) {
		if (ksp->tx_buffers[buff_n].skb) {
			/* Remove this SKB from the TX ring */
			ksp->tx_ring[buff_n].owner = 0;
			ksp->tx_ring[buff_n].status = 0;
			ksp->tx_ring[buff_n].data_ptr = 0;

			/* Unmap and bin this SKB */
			dma_unmap_single(ksp->dev,
					 ksp->tx_buffers[buff_n].dma_ptr,
					 ksp->tx_buffers[buff_n].length,
					 DMA_TO_DEVICE);
			dev_kfree_skb_irq(ksp->tx_buffers[buff_n].skb);
			ksp->tx_buffers[buff_n].skb = NULL;
		}
	}

	/* Purge the RX buffers */
	for (buff_n = 0; buff_n < MAX_RX_DESC; ++buff_n) {
		if (ksp->rx_buffers[buff_n].skb) {
			/* Remove the SKB from the RX ring */
			ksp->rx_ring[buff_n].status = 0;
			ksp->rx_ring[buff_n].data_ptr = 0;

			/* Unmap and bin the SKB */
			dma_unmap_single(ksp->dev,
					 ksp->rx_buffers[buff_n].dma_ptr,
					 ksp->rx_buffers[buff_n].length,
					 DMA_FROM_DEVICE);
			dev_kfree_skb_irq(ksp->rx_buffers[buff_n].skb);
			ksp->rx_buffers[buff_n].skb = NULL;
		}
	}
}


/**
 *	ks8695_setup_irq - IRQ setup helper function
 *	@irq: The IRQ number to claim
 *	@irq_name: The name to give the IRQ claimant
 *	@handler: The function to call to handle the IRQ
 *	@ndev: The net_device to pass in as the dev_id argument to the handler
 *
 *	Return 0 on success.
 */
static int
ks8695_setup_irq(int irq, const char *irq_name,
		 irq_handler_t handler, struct net_device *ndev)
{
	int ret;

	ret = request_irq(irq, handler, IRQF_SHARED, irq_name, ndev);

	if (ret) {
		dev_err(&ndev->dev, "failure to request IRQ %d\n", irq);
		return ret;
	}

	return 0;
}

/**
 *	ks8695_init_net - Initialise a KS8695 ethernet interface
 *	@ksp: The interface to initialise
 *
 *	This routine fills the RX ring, initialises the DMA engines,
 *	allocates the IRQs and then starts the packet TX and RX
 *	engines.
 */
static int
ks8695_init_net(struct ks8695_priv *ksp)
{
	int ret;
	u32 ctrl;

	ks8695_refill_rxbuffers(ksp);

	/* Initialise the DMA engines */
	ks8695_writereg(ksp, KS8695_RDLB, (u32) ksp->rx_ring_dma);
	ks8695_writereg(ksp, KS8695_TDLB, (u32) ksp->tx_ring_dma);

	/* Request the IRQs */
	ret = ks8695_setup_irq(ksp->rx_irq, ksp->rx_irq_name,
			       ks8695_rx_irq, ksp->ndev);
	if (ret)
		return ret;
	ret = ks8695_setup_irq(ksp->tx_irq, ksp->tx_irq_name,
			       ks8695_tx_irq, ksp->ndev);
	if (ret)
		return ret;
	if (ksp->link_irq != -1) {
		ret = ks8695_setup_irq(ksp->link_irq, ksp->link_irq_name,
				       ks8695_link_irq, ksp->ndev);
		if (ret)
			return ret;
	}

	/* Set up the ring indices */
	ksp->next_rx_desc_read = 0;
	ksp->tx_ring_next_slot = 0;
	ksp->tx_ring_used = 0;

	/* Bring up transmission */
	ctrl = ks8695_readreg(ksp, KS8695_DTXC);
	/* Enable packet transmission */
	ks8695_writereg(ksp, KS8695_DTXC, ctrl | DTXC_TE);

	/* Bring up the reception */
	ctrl = ks8695_readreg(ksp, KS8695_DRXC);
	/* Enable packet reception */
	ks8695_writereg(ksp, KS8695_DRXC, ctrl | DRXC_RE);
	/* And start the DMA engine */
	ks8695_writereg(ksp, KS8695_DRSC, 0);

	/* All done */
	return 0;
}

/**
 *	ks8695_release_device - HW resource release for KS8695 e-net
 *	@ksp: The device to be freed
 *
 *	This unallocates io memory regions, dma-coherent regions etc
 *	which were allocated in ks8695_probe.
 */
static void
ks8695_release_device(struct ks8695_priv *ksp)
{
	/* Unmap the registers */
	iounmap(ksp->io_regs);
	if (ksp->phyiface_regs)
		iounmap(ksp->phyiface_regs);

	/* And release the request */
	release_resource(ksp->regs_req);
	kfree(ksp->regs_req);
	if (ksp->phyiface_req) {
		release_resource(ksp->phyiface_req);
		kfree(ksp->phyiface_req);
	}

	/* Free the ring buffers */
	dma_free_coherent(ksp->dev, RING_DMA_SIZE,
			  ksp->ring_base, ksp->ring_base_dma);
}

/* Ethtool support */

/**
 *	ks8695_get_msglevel - Get the messages enabled for emission
 *	@ndev: The network device to read from
 */
static u32
ks8695_get_msglevel(struct net_device *ndev)
{
	struct ks8695_priv *ksp = netdev_priv(ndev);

	return ksp->msg_enable;
}

/**
 *	ks8695_set_msglevel - Set the messages enabled for emission
 *	@ndev: The network device to configure
 *	@value: The messages to set for emission
 */
static void
ks8695_set_msglevel(struct net_device *ndev, u32 value)
{
	struct ks8695_priv *ksp = netdev_priv(ndev);

	ksp->msg_enable = value;
}

/**
 *	ks8695_wan_get_link_ksettings - Get device-specific settings.
 *	@ndev: The network device to read settings from
 *	@cmd: The ethtool structure to read into
 */
static int
ks8695_wan_get_link_ksettings(struct net_device *ndev,
			      struct ethtool_link_ksettings *cmd)
{
	struct ks8695_priv *ksp = netdev_priv(ndev);
	u32 ctrl;
	u32 supported, advertising;

	/* All ports on the KS8695 support these... */
	supported = (SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full |
			  SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full |
			  SUPPORTED_TP | SUPPORTED_MII);

	advertising = ADVERTISED_TP | ADVERTISED_MII;
	cmd->base.port = PORT_MII;
	supported |= (SUPPORTED_Autoneg | SUPPORTED_Pause);
	cmd->base.phy_address = 0;

	ctrl = readl(ksp->phyiface_regs + KS8695_WMC);
	if ((ctrl & WMC_WAND) == 0) {
		/* auto-negotiation is enabled */
		advertising |= ADVERTISED_Autoneg;
		if (ctrl & WMC_WANA100F)
			advertising |= ADVERTISED_100baseT_Full;
		if (ctrl & WMC_WANA100H)
			advertising |= ADVERTISED_100baseT_Half;
		if (ctrl & WMC_WANA10F)
			advertising |= ADVERTISED_10baseT_Full;
		if (ctrl & WMC_WANA10H)
			advertising |= ADVERTISED_10baseT_Half;
		if (ctrl & WMC_WANAP)
			advertising |= ADVERTISED_Pause;
		cmd->base.autoneg = AUTONEG_ENABLE;

		cmd->base.speed = (ctrl & WMC_WSS) ? SPEED_100 : SPEED_10;
		cmd->base.duplex = (ctrl & WMC_WDS) ?
			DUPLEX_FULL : DUPLEX_HALF;
	} else {
		/* auto-negotiation is disabled */
		cmd->base.autoneg = AUTONEG_DISABLE;

		cmd->base.speed = (ctrl & WMC_WANF100) ?
					    SPEED_100 : SPEED_10;
		cmd->base.duplex = (ctrl & WMC_WANFF) ?
			DUPLEX_FULL : DUPLEX_HALF;
	}

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	return 0;
}

/**
 *	ks8695_wan_set_link_ksettings - Set device-specific settings.
 *	@ndev: The network device to configure
 *	@cmd: The settings to configure
 */
static int
ks8695_wan_set_link_ksettings(struct net_device *ndev,
			      const struct ethtool_link_ksettings *cmd)
{
	struct ks8695_priv *ksp = netdev_priv(ndev);
	u32 ctrl;
	u32 advertising;

	ethtool_convert_link_mode_to_legacy_u32(&advertising,
						cmd->link_modes.advertising);

	if ((cmd->base.speed != SPEED_10) && (cmd->base.speed != SPEED_100))
		return -EINVAL;
	if ((cmd->base.duplex != DUPLEX_HALF) &&
	    (cmd->base.duplex != DUPLEX_FULL))
		return -EINVAL;
	if (cmd->base.port != PORT_MII)
		return -EINVAL;
	if ((cmd->base.autoneg != AUTONEG_DISABLE) &&
	    (cmd->base.autoneg != AUTONEG_ENABLE))
		return -EINVAL;

	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		if ((advertising & (ADVERTISED_10baseT_Half |
				ADVERTISED_10baseT_Full |
				ADVERTISED_100baseT_Half |
				ADVERTISED_100baseT_Full)) == 0)
			return -EINVAL;

		ctrl = readl(ksp->phyiface_regs + KS8695_WMC);

		ctrl &= ~(WMC_WAND | WMC_WANA100F | WMC_WANA100H |
			  WMC_WANA10F | WMC_WANA10H);
		if (advertising & ADVERTISED_100baseT_Full)
			ctrl |= WMC_WANA100F;
		if (advertising & ADVERTISED_100baseT_Half)
			ctrl |= WMC_WANA100H;
		if (advertising & ADVERTISED_10baseT_Full)
			ctrl |= WMC_WANA10F;
		if (advertising & ADVERTISED_10baseT_Half)
			ctrl |= WMC_WANA10H;

		/* force a re-negotiation */
		ctrl |= WMC_WANR;
		writel(ctrl, ksp->phyiface_regs + KS8695_WMC);
	} else {
		ctrl = readl(ksp->phyiface_regs + KS8695_WMC);

		/* disable auto-negotiation */
		ctrl |= WMC_WAND;
		ctrl &= ~(WMC_WANF100 | WMC_WANFF);

		if (cmd->base.speed == SPEED_100)
			ctrl |= WMC_WANF100;
		if (cmd->base.duplex == DUPLEX_FULL)
			ctrl |= WMC_WANFF;

		writel(ctrl, ksp->phyiface_regs + KS8695_WMC);
	}

	return 0;
}

/**
 *	ks8695_wan_nwayreset - Restart the autonegotiation on the port.
 *	@ndev: The network device to restart autoneotiation on
 */
static int
ks8695_wan_nwayreset(struct net_device *ndev)
{
	struct ks8695_priv *ksp = netdev_priv(ndev);
	u32 ctrl;

	ctrl = readl(ksp->phyiface_regs + KS8695_WMC);

	if ((ctrl & WMC_WAND) == 0)
		writel(ctrl | WMC_WANR,
		       ksp->phyiface_regs + KS8695_WMC);
	else
		/* auto-negotiation not enabled */
		return -EINVAL;

	return 0;
}

/**
 *	ks8695_wan_get_pause - Retrieve network pause/flow-control advertising
 *	@ndev: The device to retrieve settings from
 *	@param: The structure to fill out with the information
 */
static void
ks8695_wan_get_pause(struct net_device *ndev, struct ethtool_pauseparam *param)
{
	struct ks8695_priv *ksp = netdev_priv(ndev);
	u32 ctrl;

	ctrl = readl(ksp->phyiface_regs + KS8695_WMC);

	/* advertise Pause */
	param->autoneg = (ctrl & WMC_WANAP);

	/* current Rx Flow-control */
	ctrl = ks8695_readreg(ksp, KS8695_DRXC);
	param->rx_pause = (ctrl & DRXC_RFCE);

	/* current Tx Flow-control */
	ctrl = ks8695_readreg(ksp, KS8695_DTXC);
	param->tx_pause = (ctrl & DTXC_TFCE);
}

/**
 *	ks8695_get_drvinfo - Retrieve driver information
 *	@ndev: The network device to retrieve info about
 *	@info: The info structure to fill out.
 */
static void
ks8695_get_drvinfo(struct net_device *ndev, struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, MODULENAME, sizeof(info->driver));
	strlcpy(info->version, MODULEVERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(ndev->dev.parent),
		sizeof(info->bus_info));
}

static const struct ethtool_ops ks8695_ethtool_ops = {
	.get_msglevel	= ks8695_get_msglevel,
	.set_msglevel	= ks8695_set_msglevel,
	.get_drvinfo	= ks8695_get_drvinfo,
};

static const struct ethtool_ops ks8695_wan_ethtool_ops = {
	.get_msglevel	= ks8695_get_msglevel,
	.set_msglevel	= ks8695_set_msglevel,
	.nway_reset	= ks8695_wan_nwayreset,
	.get_link	= ethtool_op_get_link,
	.get_pauseparam = ks8695_wan_get_pause,
	.get_drvinfo	= ks8695_get_drvinfo,
	.get_link_ksettings = ks8695_wan_get_link_ksettings,
	.set_link_ksettings = ks8695_wan_set_link_ksettings,
};

/* Network device interface functions */

/**
 *	ks8695_set_mac - Update MAC in net dev and HW
 *	@ndev: The network device to update
 *	@addr: The new MAC address to set
 */
static int
ks8695_set_mac(struct net_device *ndev, void *addr)
{
	struct ks8695_priv *ksp = netdev_priv(ndev);
	struct sockaddr *address = addr;

	if (!is_valid_ether_addr(address->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(ndev->dev_addr, address->sa_data, ndev->addr_len);

	ks8695_update_mac(ksp);

	dev_dbg(ksp->dev, "%s: Updated MAC address to %pM\n",
		ndev->name, ndev->dev_addr);

	return 0;
}

/**
 *	ks8695_set_multicast - Set up the multicast behaviour of the interface
 *	@ndev: The net_device to configure
 *
 *	This routine, called by the net layer, configures promiscuity
 *	and multicast reception behaviour for the interface.
 */
static void
ks8695_set_multicast(struct net_device *ndev)
{
	struct ks8695_priv *ksp = netdev_priv(ndev);
	u32 ctrl;

	ctrl = ks8695_readreg(ksp, KS8695_DRXC);

	if (ndev->flags & IFF_PROMISC) {
		/* enable promiscuous mode */
		ctrl |= DRXC_RA;
	} else if (ndev->flags & ~IFF_PROMISC) {
		/* disable promiscuous mode */
		ctrl &= ~DRXC_RA;
	}

	if (ndev->flags & IFF_ALLMULTI) {
		/* enable all multicast mode */
		ctrl |= DRXC_RM;
	} else if (netdev_mc_count(ndev) > KS8695_NR_ADDRESSES) {
		/* more specific multicast addresses than can be
		 * handled in hardware
		 */
		ctrl |= DRXC_RM;
	} else {
		/* enable specific multicasts */
		ctrl &= ~DRXC_RM;
		ks8695_init_partial_multicast(ksp, ndev);
	}

	ks8695_writereg(ksp, KS8695_DRXC, ctrl);
}

/**
 *	ks8695_timeout - Handle a network tx/rx timeout.
 *	@ndev: The net_device which timed out.
 *
 *	A network transaction timed out, reset the device.
 */
static void
ks8695_timeout(struct net_device *ndev)
{
	struct ks8695_priv *ksp = netdev_priv(ndev);

	netif_stop_queue(ndev);
	ks8695_shutdown(ksp);

	ks8695_reset(ksp);

	ks8695_update_mac(ksp);

	/* We ignore the return from this since it managed to init
	 * before it probably will be okay to init again.
	 */
	ks8695_init_net(ksp);

	/* Reconfigure promiscuity etc */
	ks8695_set_multicast(ndev);

	/* And start the TX queue once more */
	netif_start_queue(ndev);
}

/**
 *	ks8695_start_xmit - Start a packet transmission
 *	@skb: The packet to transmit
 *	@ndev: The network device to send the packet on
 *
 *	This routine, called by the net layer, takes ownership of the
 *	sk_buff and adds it to the TX ring. It then kicks the TX DMA
 *	engine to ensure transmission begins.
 */
static int
ks8695_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct ks8695_priv *ksp = netdev_priv(ndev);
	int buff_n;
	dma_addr_t dmap;

	spin_lock_irq(&ksp->txq_lock);

	if (ksp->tx_ring_used == MAX_TX_DESC) {
		/* Somehow we got entered when we have no room */
		spin_unlock_irq(&ksp->txq_lock);
		return NETDEV_TX_BUSY;
	}

	buff_n = ksp->tx_ring_next_slot;

	BUG_ON(ksp->tx_buffers[buff_n].skb);

	dmap = dma_map_single(ksp->dev, skb->data, skb->len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(ksp->dev, dmap))) {
		/* Failed to DMA map this SKB, give it back for now */
		spin_unlock_irq(&ksp->txq_lock);
		dev_dbg(ksp->dev, "%s: Could not map DMA memory for "\
			"transmission, trying later\n", ndev->name);
		return NETDEV_TX_BUSY;
	}

	ksp->tx_buffers[buff_n].dma_ptr = dmap;
	/* Mapped okay, store the buffer pointer and length for later */
	ksp->tx_buffers[buff_n].skb = skb;
	ksp->tx_buffers[buff_n].length = skb->len;

	/* Fill out the TX descriptor */
	ksp->tx_ring[buff_n].data_ptr =
		cpu_to_le32(ksp->tx_buffers[buff_n].dma_ptr);
	ksp->tx_ring[buff_n].status =
		cpu_to_le32(TDES_IC | TDES_FS | TDES_LS |
			    (skb->len & TDES_TBS));

	wmb();

	/* Hand it over to the hardware */
	ksp->tx_ring[buff_n].owner = cpu_to_le32(TDES_OWN);

	if (++ksp->tx_ring_used == MAX_TX_DESC)
		netif_stop_queue(ndev);

	/* Kick the TX DMA in case it decided to go IDLE */
	ks8695_writereg(ksp, KS8695_DTSC, 0);

	/* And update the next ring slot */
	ksp->tx_ring_next_slot = (buff_n + 1) & MAX_TX_DESC_MASK;

	spin_unlock_irq(&ksp->txq_lock);
	return NETDEV_TX_OK;
}

/**
 *	ks8695_stop - Stop (shutdown) a KS8695 ethernet interface
 *	@ndev: The net_device to stop
 *
 *	This disables the TX queue and cleans up a KS8695 ethernet
 *	device.
 */
static int
ks8695_stop(struct net_device *ndev)
{
	struct ks8695_priv *ksp = netdev_priv(ndev);

	netif_stop_queue(ndev);
	napi_disable(&ksp->napi);

	ks8695_shutdown(ksp);

	return 0;
}

/**
 *	ks8695_open - Open (bring up) a KS8695 ethernet interface
 *	@ndev: The net_device to open
 *
 *	This resets, configures the MAC, initialises the RX ring and
 *	DMA engines and starts the TX queue for a KS8695 ethernet
 *	device.
 */
static int
ks8695_open(struct net_device *ndev)
{
	struct ks8695_priv *ksp = netdev_priv(ndev);
	int ret;

	ks8695_reset(ksp);

	ks8695_update_mac(ksp);

	ret = ks8695_init_net(ksp);
	if (ret) {
		ks8695_shutdown(ksp);
		return ret;
	}

	napi_enable(&ksp->napi);
	netif_start_queue(ndev);

	return 0;
}

/* Platform device driver */

/**
 *	ks8695_init_switch - Init LAN switch to known good defaults.
 *	@ksp: The device to initialise
 *
 *	This initialises the LAN switch in the KS8695 to a known-good
 *	set of defaults.
 */
static void
ks8695_init_switch(struct ks8695_priv *ksp)
{
	u32 ctrl;

	/* Default value for SEC0 according to datasheet */
	ctrl = 0x40819e00;

	/* LED0 = Speed	 LED1 = Link/Activity */
	ctrl &= ~(SEC0_LLED1S | SEC0_LLED0S);
	ctrl |= (LLED0S_LINK | LLED1S_LINK_ACTIVITY);

	/* Enable Switch */
	ctrl |= SEC0_ENABLE;

	writel(ctrl, ksp->phyiface_regs + KS8695_SEC0);

	/* Defaults for SEC1 */
	writel(0x9400100, ksp->phyiface_regs + KS8695_SEC1);
}

/**
 *	ks8695_init_wan_phy - Initialise the WAN PHY to sensible defaults
 *	@ksp: The device to initialise
 *
 *	This initialises a KS8695's WAN phy to sensible values for
 *	autonegotiation etc.
 */
static void
ks8695_init_wan_phy(struct ks8695_priv *ksp)
{
	u32 ctrl;

	/* Support auto-negotiation */
	ctrl = (WMC_WANAP | WMC_WANA100F | WMC_WANA100H |
		WMC_WANA10F | WMC_WANA10H);

	/* LED0 = Activity , LED1 = Link */
	ctrl |= (WLED0S_ACTIVITY | WLED1S_LINK);

	/* Restart Auto-negotiation */
	ctrl |= WMC_WANR;

	writel(ctrl, ksp->phyiface_regs + KS8695_WMC);

	writel(0, ksp->phyiface_regs + KS8695_WPPM);
	writel(0, ksp->phyiface_regs + KS8695_PPS);
}

static const struct net_device_ops ks8695_netdev_ops = {
	.ndo_open		= ks8695_open,
	.ndo_stop		= ks8695_stop,
	.ndo_start_xmit		= ks8695_start_xmit,
	.ndo_tx_timeout		= ks8695_timeout,
	.ndo_set_mac_address	= ks8695_set_mac,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= ks8695_set_multicast,
};

/**
 *	ks8695_probe - Probe and initialise a KS8695 ethernet interface
 *	@pdev: The platform device to probe
 *
 *	Initialise a KS8695 ethernet device from platform data.
 *
 *	This driver requires at least one IORESOURCE_MEM for the
 *	registers and two IORESOURCE_IRQ for the RX and TX IRQs
 *	respectively. It can optionally take an additional
 *	IORESOURCE_MEM for the switch or phy in the case of the lan or
 *	wan ports, and an IORESOURCE_IRQ for the link IRQ for the wan
 *	port.
 */
static int
ks8695_probe(struct platform_device *pdev)
{
	struct ks8695_priv *ksp;
	struct net_device *ndev;
	struct resource *regs_res, *phyiface_res;
	struct resource *rxirq_res, *txirq_res, *linkirq_res;
	int ret = 0;
	int buff_n;
	bool inv_mac_addr = false;
	u32 machigh, maclow;

	/* Initialise a net_device */
	ndev = alloc_etherdev(sizeof(struct ks8695_priv));
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &pdev->dev);

	dev_dbg(&pdev->dev, "ks8695_probe() called\n");

	/* Configure our private structure a little */
	ksp = netdev_priv(ndev);

	ksp->dev = &pdev->dev;
	ksp->ndev = ndev;
	ksp->msg_enable = NETIF_MSG_LINK;

	/* Retrieve resources */
	regs_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	phyiface_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);

	rxirq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	txirq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	linkirq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 2);

	if (!(regs_res && rxirq_res && txirq_res)) {
		dev_err(ksp->dev, "insufficient resources\n");
		ret = -ENOENT;
		goto failure;
	}

	ksp->regs_req = request_mem_region(regs_res->start,
					   resource_size(regs_res),
					   pdev->name);

	if (!ksp->regs_req) {
		dev_err(ksp->dev, "cannot claim register space\n");
		ret = -EIO;
		goto failure;
	}

	ksp->io_regs = ioremap(regs_res->start, resource_size(regs_res));

	if (!ksp->io_regs) {
		dev_err(ksp->dev, "failed to ioremap registers\n");
		ret = -EINVAL;
		goto failure;
	}

	if (phyiface_res) {
		ksp->phyiface_req =
			request_mem_region(phyiface_res->start,
					   resource_size(phyiface_res),
					   phyiface_res->name);

		if (!ksp->phyiface_req) {
			dev_err(ksp->dev,
				"cannot claim switch register space\n");
			ret = -EIO;
			goto failure;
		}

		ksp->phyiface_regs = ioremap(phyiface_res->start,
					     resource_size(phyiface_res));

		if (!ksp->phyiface_regs) {
			dev_err(ksp->dev,
				"failed to ioremap switch registers\n");
			ret = -EINVAL;
			goto failure;
		}
	}

	ksp->rx_irq = rxirq_res->start;
	ksp->rx_irq_name = rxirq_res->name ? rxirq_res->name : "Ethernet RX";
	ksp->tx_irq = txirq_res->start;
	ksp->tx_irq_name = txirq_res->name ? txirq_res->name : "Ethernet TX";
	ksp->link_irq = (linkirq_res ? linkirq_res->start : -1);
	ksp->link_irq_name = (linkirq_res && linkirq_res->name) ?
		linkirq_res->name : "Ethernet Link";

	/* driver system setup */
	ndev->netdev_ops = &ks8695_netdev_ops;
	ndev->watchdog_timeo	 = msecs_to_jiffies(watchdog);

	netif_napi_add(ndev, &ksp->napi, ks8695_poll, NAPI_WEIGHT);

	/* Retrieve the default MAC addr from the chip. */
	/* The bootloader should have left it in there for us. */

	machigh = ks8695_readreg(ksp, KS8695_MAH);
	maclow = ks8695_readreg(ksp, KS8695_MAL);

	ndev->dev_addr[0] = (machigh >> 8) & 0xFF;
	ndev->dev_addr[1] = machigh & 0xFF;
	ndev->dev_addr[2] = (maclow >> 24) & 0xFF;
	ndev->dev_addr[3] = (maclow >> 16) & 0xFF;
	ndev->dev_addr[4] = (maclow >> 8) & 0xFF;
	ndev->dev_addr[5] = maclow & 0xFF;

	if (!is_valid_ether_addr(ndev->dev_addr))
		inv_mac_addr = true;

	/* In order to be efficient memory-wise, we allocate both
	 * rings in one go.
	 */
	ksp->ring_base = dma_alloc_coherent(&pdev->dev, RING_DMA_SIZE,
					    &ksp->ring_base_dma, GFP_KERNEL);
	if (!ksp->ring_base) {
		ret = -ENOMEM;
		goto failure;
	}

	/* Specify the TX DMA ring buffer */
	ksp->tx_ring = ksp->ring_base;
	ksp->tx_ring_dma = ksp->ring_base_dma;

	/* And initialise the queue's lock */
	spin_lock_init(&ksp->txq_lock);
	spin_lock_init(&ksp->rx_lock);

	/* Specify the RX DMA ring buffer */
	ksp->rx_ring = ksp->ring_base + TX_RING_DMA_SIZE;
	ksp->rx_ring_dma = ksp->ring_base_dma + TX_RING_DMA_SIZE;

	/* Zero the descriptor rings */
	memset(ksp->tx_ring, 0, TX_RING_DMA_SIZE);
	memset(ksp->rx_ring, 0, RX_RING_DMA_SIZE);

	/* Build the rings */
	for (buff_n = 0; buff_n < MAX_TX_DESC; ++buff_n) {
		ksp->tx_ring[buff_n].next_desc =
			cpu_to_le32(ksp->tx_ring_dma +
				    (sizeof(struct tx_ring_desc) *
				     ((buff_n + 1) & MAX_TX_DESC_MASK)));
	}

	for (buff_n = 0; buff_n < MAX_RX_DESC; ++buff_n) {
		ksp->rx_ring[buff_n].next_desc =
			cpu_to_le32(ksp->rx_ring_dma +
				    (sizeof(struct rx_ring_desc) *
				     ((buff_n + 1) & MAX_RX_DESC_MASK)));
	}

	/* Initialise the port (physically) */
	if (ksp->phyiface_regs && ksp->link_irq == -1) {
		ks8695_init_switch(ksp);
		ksp->dtype = KS8695_DTYPE_LAN;
		ndev->ethtool_ops = &ks8695_ethtool_ops;
	} else if (ksp->phyiface_regs && ksp->link_irq != -1) {
		ks8695_init_wan_phy(ksp);
		ksp->dtype = KS8695_DTYPE_WAN;
		ndev->ethtool_ops = &ks8695_wan_ethtool_ops;
	} else {
		/* No initialisation since HPNA does not have a PHY */
		ksp->dtype = KS8695_DTYPE_HPNA;
		ndev->ethtool_ops = &ks8695_ethtool_ops;
	}

	/* And bring up the net_device with the net core */
	platform_set_drvdata(pdev, ndev);
	ret = register_netdev(ndev);

	if (ret == 0) {
		if (inv_mac_addr)
			dev_warn(ksp->dev, "%s: Invalid ethernet MAC address. Please set using ip\n",
				 ndev->name);
		dev_info(ksp->dev, "ks8695 ethernet (%s) MAC: %pM\n",
			 ks8695_port_type(ksp), ndev->dev_addr);
	} else {
		/* Report the failure to register the net_device */
		dev_err(ksp->dev, "ks8695net: failed to register netdev.\n");
		goto failure;
	}

	/* All is well */
	return 0;

	/* Error exit path */
failure:
	ks8695_release_device(ksp);
	free_netdev(ndev);

	return ret;
}

/**
 *	ks8695_drv_suspend - Suspend a KS8695 ethernet platform device.
 *	@pdev: The device to suspend
 *	@state: The suspend state
 *
 *	This routine detaches and shuts down a KS8695 ethernet device.
 */
static int
ks8695_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ks8695_priv *ksp = netdev_priv(ndev);

	ksp->in_suspend = 1;

	if (netif_running(ndev)) {
		netif_device_detach(ndev);
		ks8695_shutdown(ksp);
	}

	return 0;
}

/**
 *	ks8695_drv_resume - Resume a KS8695 ethernet platform device.
 *	@pdev: The device to resume
 *
 *	This routine re-initialises and re-attaches a KS8695 ethernet
 *	device.
 */
static int
ks8695_drv_resume(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ks8695_priv *ksp = netdev_priv(ndev);

	if (netif_running(ndev)) {
		ks8695_reset(ksp);
		ks8695_init_net(ksp);
		ks8695_set_multicast(ndev);
		netif_device_attach(ndev);
	}

	ksp->in_suspend = 0;

	return 0;
}

/**
 *	ks8695_drv_remove - Remove a KS8695 net device on driver unload.
 *	@pdev: The platform device to remove
 *
 *	This unregisters and releases a KS8695 ethernet device.
 */
static int
ks8695_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ks8695_priv *ksp = netdev_priv(ndev);

	netif_napi_del(&ksp->napi);

	unregister_netdev(ndev);
	ks8695_release_device(ksp);
	free_netdev(ndev);

	dev_dbg(&pdev->dev, "released and freed device\n");
	return 0;
}

static struct platform_driver ks8695_driver = {
	.driver = {
		.name	= MODULENAME,
	},
	.probe		= ks8695_probe,
	.remove		= ks8695_drv_remove,
	.suspend	= ks8695_drv_suspend,
	.resume		= ks8695_drv_resume,
};

module_platform_driver(ks8695_driver);

MODULE_AUTHOR("Simtec Electronics");
MODULE_DESCRIPTION("Micrel KS8695 (Centaur) Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" MODULENAME);

module_param(watchdog, int, 0400);
MODULE_PARM_DESC(watchdog, "transmit timeout in milliseconds");
