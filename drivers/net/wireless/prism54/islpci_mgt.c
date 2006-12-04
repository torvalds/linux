/*
 *  Copyright (C) 2002 Intersil Americas Inc.
 *  Copyright 2004 Jens Maurer <Jens.Maurer@gmx.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/system.h>
#include <linux/if_arp.h>

#include "prismcompat.h"
#include "isl_38xx.h"
#include "islpci_mgt.h"
#include "isl_oid.h"		/* additional types and defs for isl38xx fw */
#include "isl_ioctl.h"

#include <net/iw_handler.h>

/******************************************************************************
        Global variable definition section
******************************************************************************/
int pc_debug = VERBOSE;
module_param(pc_debug, int, 0);

/******************************************************************************
    Driver general functions
******************************************************************************/
#if VERBOSE > SHOW_ERROR_MESSAGES
void
display_buffer(char *buffer, int length)
{
	if ((pc_debug & SHOW_BUFFER_CONTENTS) == 0)
		return;

	while (length > 0) {
		printk("[%02x]", *buffer & 255);
		length--;
		buffer++;
	}

	printk("\n");
}
#endif

/*****************************************************************************
    Queue handling for management frames
******************************************************************************/

/*
 * Helper function to create a PIMFOR management frame header.
 */
static void
pimfor_encode_header(int operation, u32 oid, u32 length, pimfor_header_t *h)
{
	h->version = PIMFOR_VERSION;
	h->operation = operation;
	h->device_id = PIMFOR_DEV_ID_MHLI_MIB;
	h->flags = 0;
	h->oid = cpu_to_be32(oid);
	h->length = cpu_to_be32(length);
}

/*
 * Helper function to analyze a PIMFOR management frame header.
 */
static pimfor_header_t *
pimfor_decode_header(void *data, int len)
{
	pimfor_header_t *h = data;

	while ((void *) h < data + len) {
		if (h->flags & PIMFOR_FLAG_LITTLE_ENDIAN) {
			le32_to_cpus(&h->oid);
			le32_to_cpus(&h->length);
		} else {
			be32_to_cpus(&h->oid);
			be32_to_cpus(&h->length);
		}
		if (h->oid != OID_INL_TUNNEL)
			return h;
		h++;
	}
	return NULL;
}

/*
 * Fill the receive queue for management frames with fresh buffers.
 */
int
islpci_mgmt_rx_fill(struct net_device *ndev)
{
	islpci_private *priv = netdev_priv(ndev);
	isl38xx_control_block *cb =	/* volatile not needed */
	    (isl38xx_control_block *) priv->control_block;
	u32 curr = le32_to_cpu(cb->driver_curr_frag[ISL38XX_CB_RX_MGMTQ]);

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_FUNCTION_CALLS, "islpci_mgmt_rx_fill \n");
#endif

	while (curr - priv->index_mgmt_rx < ISL38XX_CB_MGMT_QSIZE) {
		u32 index = curr % ISL38XX_CB_MGMT_QSIZE;
		struct islpci_membuf *buf = &priv->mgmt_rx[index];
		isl38xx_fragment *frag = &cb->rx_data_mgmt[index];

		if (buf->mem == NULL) {
			buf->mem = kmalloc(MGMT_FRAME_SIZE, GFP_ATOMIC);
			if (!buf->mem) {
				printk(KERN_WARNING
				       "Error allocating management frame.\n");
				return -ENOMEM;
			}
			buf->size = MGMT_FRAME_SIZE;
		}
		if (buf->pci_addr == 0) {
			buf->pci_addr = pci_map_single(priv->pdev, buf->mem,
						       MGMT_FRAME_SIZE,
						       PCI_DMA_FROMDEVICE);
			if (!buf->pci_addr) {
				printk(KERN_WARNING
				       "Failed to make memory DMA'able.\n");
				return -ENOMEM;
			}
		}

		/* be safe: always reset control block information */
		frag->size = cpu_to_le16(MGMT_FRAME_SIZE);
		frag->flags = 0;
		frag->address = cpu_to_le32(buf->pci_addr);
		curr++;

		/* The fragment address in the control block must have
		 * been written before announcing the frame buffer to
		 * device */
		wmb();
		cb->driver_curr_frag[ISL38XX_CB_RX_MGMTQ] = cpu_to_le32(curr);
	}
	return 0;
}

/*
 * Create and transmit a management frame using "operation" and "oid",
 * with arguments data/length.
 * We either return an error and free the frame, or we return 0 and
 * islpci_mgt_cleanup_transmit() frees the frame in the tx-done
 * interrupt.
 */
static int
islpci_mgt_transmit(struct net_device *ndev, int operation, unsigned long oid,
		    void *data, int length)
{
	islpci_private *priv = netdev_priv(ndev);
	isl38xx_control_block *cb =
	    (isl38xx_control_block *) priv->control_block;
	void *p;
	int err = -EINVAL;
	unsigned long flags;
	isl38xx_fragment *frag;
	struct islpci_membuf buf;
	u32 curr_frag;
	int index;
	int frag_len = length + PIMFOR_HEADER_SIZE;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_FUNCTION_CALLS, "islpci_mgt_transmit\n");
#endif

	if (frag_len > MGMT_FRAME_SIZE) {
		printk(KERN_DEBUG "%s: mgmt frame too large %d\n",
		       ndev->name, frag_len);
		goto error;
	}

	err = -ENOMEM;
	p = buf.mem = kmalloc(frag_len, GFP_KERNEL);
	if (!buf.mem) {
		printk(KERN_DEBUG "%s: cannot allocate mgmt frame\n",
		       ndev->name);
		goto error;
	}
	buf.size = frag_len;

	/* create the header directly in the fragment data area */
	pimfor_encode_header(operation, oid, length, (pimfor_header_t *) p);
	p += PIMFOR_HEADER_SIZE;

	if (data)
		memcpy(p, data, length);
	else
		memset(p, 0, length);

#if VERBOSE > SHOW_ERROR_MESSAGES
	{
		pimfor_header_t *h = buf.mem;
		DEBUG(SHOW_PIMFOR_FRAMES,
		      "PIMFOR: op %i, oid 0x%08lx, device %i, flags 0x%x length 0x%x \n",
		      h->operation, oid, h->device_id, h->flags, length);

		/* display the buffer contents for debugging */
		display_buffer((char *) h, sizeof (pimfor_header_t));
		display_buffer(p, length);
	}
#endif

	err = -ENOMEM;
	buf.pci_addr = pci_map_single(priv->pdev, buf.mem, frag_len,
				      PCI_DMA_TODEVICE);
	if (!buf.pci_addr) {
		printk(KERN_WARNING "%s: cannot map PCI memory for mgmt\n",
		       ndev->name);
		goto error_free;
	}

	/* Protect the control block modifications against interrupts. */
	spin_lock_irqsave(&priv->slock, flags);
	curr_frag = le32_to_cpu(cb->driver_curr_frag[ISL38XX_CB_TX_MGMTQ]);
	if (curr_frag - priv->index_mgmt_tx >= ISL38XX_CB_MGMT_QSIZE) {
		printk(KERN_WARNING "%s: mgmt tx queue is still full\n",
		       ndev->name);
		goto error_unlock;
	}

	/* commit the frame to the tx device queue */
	index = curr_frag % ISL38XX_CB_MGMT_QSIZE;
	priv->mgmt_tx[index] = buf;
	frag = &cb->tx_data_mgmt[index];
	frag->size = cpu_to_le16(frag_len);
	frag->flags = 0;	/* for any other than the last fragment, set to 1 */
	frag->address = cpu_to_le32(buf.pci_addr);

	/* The fragment address in the control block must have
	 * been written before announcing the frame buffer to
	 * device */
	wmb();
	cb->driver_curr_frag[ISL38XX_CB_TX_MGMTQ] = cpu_to_le32(curr_frag + 1);
	spin_unlock_irqrestore(&priv->slock, flags);

	/* trigger the device */
	islpci_trigger(priv);
	return 0;

      error_unlock:
	spin_unlock_irqrestore(&priv->slock, flags);
      error_free:
	kfree(buf.mem);
      error:
	return err;
}

/*
 * Receive a management frame from the device.
 * This can be an arbitrary number of traps, and at most one response
 * frame for a previous request sent via islpci_mgt_transmit().
 */
int
islpci_mgt_receive(struct net_device *ndev)
{
	islpci_private *priv = netdev_priv(ndev);
	isl38xx_control_block *cb =
	    (isl38xx_control_block *) priv->control_block;
	u32 curr_frag;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_FUNCTION_CALLS, "islpci_mgt_receive \n");
#endif

	/* Only once per interrupt, determine fragment range to
	 * process.  This avoids an endless loop (i.e. lockup) if
	 * frames come in faster than we can process them. */
	curr_frag = le32_to_cpu(cb->device_curr_frag[ISL38XX_CB_RX_MGMTQ]);
	barrier();

	for (; priv->index_mgmt_rx < curr_frag; priv->index_mgmt_rx++) {
		pimfor_header_t *header;
		u32 index = priv->index_mgmt_rx % ISL38XX_CB_MGMT_QSIZE;
		struct islpci_membuf *buf = &priv->mgmt_rx[index];
		u16 frag_len;
		int size;
		struct islpci_mgmtframe *frame;

		/* I have no idea (and no documentation) if flags != 0
		 * is possible.  Drop the frame, reuse the buffer. */
		if (le16_to_cpu(cb->rx_data_mgmt[index].flags) != 0) {
			printk(KERN_WARNING "%s: unknown flags 0x%04x\n",
			       ndev->name,
			       le16_to_cpu(cb->rx_data_mgmt[index].flags));
			continue;
		}

		/* The device only returns the size of the header(s) here. */
		frag_len = le16_to_cpu(cb->rx_data_mgmt[index].size);

		/*
		 * We appear to have no way to tell the device the
		 * size of a receive buffer.  Thus, if this check
		 * triggers, we likely have kernel heap corruption. */
		if (frag_len > MGMT_FRAME_SIZE) {
			printk(KERN_WARNING
				"%s: Bogus packet size of %d (%#x).\n",
				ndev->name, frag_len, frag_len);
			frag_len = MGMT_FRAME_SIZE;
		}

		/* Ensure the results of device DMA are visible to the CPU. */
		pci_dma_sync_single_for_cpu(priv->pdev, buf->pci_addr,
					    buf->size, PCI_DMA_FROMDEVICE);

		/* Perform endianess conversion for PIMFOR header in-place. */
		header = pimfor_decode_header(buf->mem, frag_len);
		if (!header) {
			printk(KERN_WARNING "%s: no PIMFOR header found\n",
			       ndev->name);
			continue;
		}

		/* The device ID from the PIMFOR packet received from
		 * the MVC is always 0.  We forward a sensible device_id.
		 * Not that anyone upstream would care... */
		header->device_id = priv->ndev->ifindex;

#if VERBOSE > SHOW_ERROR_MESSAGES
		DEBUG(SHOW_PIMFOR_FRAMES,
		      "PIMFOR: op %i, oid 0x%08x, device %i, flags 0x%x length 0x%x \n",
		      header->operation, header->oid, header->device_id,
		      header->flags, header->length);

		/* display the buffer contents for debugging */
		display_buffer((char *) header, PIMFOR_HEADER_SIZE);
		display_buffer((char *) header + PIMFOR_HEADER_SIZE,
			       header->length);
#endif

		/* nobody sends these */
		if (header->flags & PIMFOR_FLAG_APPLIC_ORIGIN) {
			printk(KERN_DEBUG
			       "%s: errant PIMFOR application frame\n",
			       ndev->name);
			continue;
		}

		/* Determine frame size, skipping OID_INL_TUNNEL headers. */
		size = PIMFOR_HEADER_SIZE + header->length;
		frame = kmalloc(sizeof (struct islpci_mgmtframe) + size,
				GFP_ATOMIC);
		if (!frame) {
			printk(KERN_WARNING
			       "%s: Out of memory, cannot handle oid 0x%08x\n",
			       ndev->name, header->oid);
			continue;
		}
		frame->ndev = ndev;
		memcpy(&frame->buf, header, size);
		frame->header = (pimfor_header_t *) frame->buf;
		frame->data = frame->buf + PIMFOR_HEADER_SIZE;

#if VERBOSE > SHOW_ERROR_MESSAGES
		DEBUG(SHOW_PIMFOR_FRAMES,
		      "frame: header: %p, data: %p, size: %d\n",
		      frame->header, frame->data, size);
#endif

		if (header->operation == PIMFOR_OP_TRAP) {
#if VERBOSE > SHOW_ERROR_MESSAGES
			printk(KERN_DEBUG
			       "TRAP: oid 0x%x, device %i, flags 0x%x length %i\n",
			       header->oid, header->device_id, header->flags,
			       header->length);
#endif

			/* Create work to handle trap out of interrupt
			 * context. */
			INIT_WORK(&frame->ws, prism54_process_trap, frame);
			schedule_work(&frame->ws);

		} else {
			/* Signal the one waiting process that a response
			 * has been received. */
			if ((frame = xchg(&priv->mgmt_received, frame)) != NULL) {
				printk(KERN_WARNING
				       "%s: mgmt response not collected\n",
				       ndev->name);
				kfree(frame);
			}
#if VERBOSE > SHOW_ERROR_MESSAGES
			DEBUG(SHOW_TRACING, "Wake up Mgmt Queue\n");
#endif
			wake_up(&priv->mgmt_wqueue);
		}

	}

	return 0;
}

/*
 * Cleanup the transmit queue by freeing all frames handled by the device.
 */
void
islpci_mgt_cleanup_transmit(struct net_device *ndev)
{
	islpci_private *priv = netdev_priv(ndev);
	isl38xx_control_block *cb =	/* volatile not needed */
	    (isl38xx_control_block *) priv->control_block;
	u32 curr_frag;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_FUNCTION_CALLS, "islpci_mgt_cleanup_transmit\n");
#endif

	/* Only once per cleanup, determine fragment range to
	 * process.  This avoids an endless loop (i.e. lockup) if
	 * the device became confused, incrementing device_curr_frag
	 * rapidly. */
	curr_frag = le32_to_cpu(cb->device_curr_frag[ISL38XX_CB_TX_MGMTQ]);
	barrier();

	for (; priv->index_mgmt_tx < curr_frag; priv->index_mgmt_tx++) {
		int index = priv->index_mgmt_tx % ISL38XX_CB_MGMT_QSIZE;
		struct islpci_membuf *buf = &priv->mgmt_tx[index];
		pci_unmap_single(priv->pdev, buf->pci_addr, buf->size,
				 PCI_DMA_TODEVICE);
		buf->pci_addr = 0;
		kfree(buf->mem);
		buf->mem = NULL;
		buf->size = 0;
	}
}

/*
 * Perform one request-response transaction to the device.
 */
int
islpci_mgt_transaction(struct net_device *ndev,
		       int operation, unsigned long oid,
		       void *senddata, int sendlen,
		       struct islpci_mgmtframe **recvframe)
{
	islpci_private *priv = netdev_priv(ndev);
	const long wait_cycle_jiffies = msecs_to_jiffies(ISL38XX_WAIT_CYCLE * 10);
	long timeout_left = ISL38XX_MAX_WAIT_CYCLES * wait_cycle_jiffies;
	int err;
	DEFINE_WAIT(wait);

	*recvframe = NULL;

	if (down_interruptible(&priv->mgmt_sem))
		return -ERESTARTSYS;

	prepare_to_wait(&priv->mgmt_wqueue, &wait, TASK_UNINTERRUPTIBLE);
	err = islpci_mgt_transmit(ndev, operation, oid, senddata, sendlen);
	if (err)
		goto out;

	err = -ETIMEDOUT;
	while (timeout_left > 0) {
		int timeleft;
		struct islpci_mgmtframe *frame;

		timeleft = schedule_timeout_uninterruptible(wait_cycle_jiffies);
		frame = xchg(&priv->mgmt_received, NULL);
		if (frame) {
			if (frame->header->oid == oid) {
				*recvframe = frame;
				err = 0;
				goto out;
			} else {
				printk(KERN_DEBUG
				       "%s: expecting oid 0x%x, received 0x%x.\n",
				       ndev->name, (unsigned int) oid,
				       frame->header->oid);
				kfree(frame);
				frame = NULL;
			}
		}
		if (timeleft == 0) {
			printk(KERN_DEBUG
				"%s: timeout waiting for mgmt response %lu, "
				"triggering device\n",
				ndev->name, timeout_left);
			islpci_trigger(priv);
		}
		timeout_left += timeleft - wait_cycle_jiffies;
	}
	printk(KERN_WARNING "%s: timeout waiting for mgmt response\n",
	       ndev->name);

	/* TODO: we should reset the device here */
 out:
	finish_wait(&priv->mgmt_wqueue, &wait);
	up(&priv->mgmt_sem);
	return err;
}

