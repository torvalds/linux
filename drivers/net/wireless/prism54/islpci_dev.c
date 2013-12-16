/*
 *  Copyright (C) 2002 Intersil Americas Inc.
 *  Copyright (C) 2003 Herbert Valerio Riedel <hvr@gnu.org>
 *  Copyright (C) 2003 Luis R. Rodriguez <mcgrof@ruslug.rutgers.edu>
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

#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/if_arp.h>

#include <asm/io.h>

#include "prismcompat.h"
#include "isl_38xx.h"
#include "isl_ioctl.h"
#include "islpci_dev.h"
#include "islpci_mgt.h"
#include "islpci_eth.h"
#include "oid_mgt.h"

#define ISL3877_IMAGE_FILE	"isl3877"
#define ISL3886_IMAGE_FILE	"isl3886"
#define ISL3890_IMAGE_FILE	"isl3890"
MODULE_FIRMWARE(ISL3877_IMAGE_FILE);
MODULE_FIRMWARE(ISL3886_IMAGE_FILE);
MODULE_FIRMWARE(ISL3890_IMAGE_FILE);

static int prism54_bring_down(islpci_private *);
static int islpci_alloc_memory(islpci_private *);

/* Temporary dummy MAC address to use until firmware is loaded.
 * The idea there is that some tools (such as nameif) may query
 * the MAC address before the netdev is 'open'. By using a valid
 * OUI prefix, they can process the netdev properly.
 * Of course, this is not the final/real MAC address. It doesn't
 * matter, as you are suppose to be able to change it anytime via
 * ndev->set_mac_address. Jean II */
static const unsigned char	dummy_mac[6] = { 0x00, 0x30, 0xB4, 0x00, 0x00, 0x00 };

static int
isl_upload_firmware(islpci_private *priv)
{
	u32 reg, rc;
	void __iomem *device_base = priv->device_base;

	/* clear the RAMBoot and the Reset bit */
	reg = readl(device_base + ISL38XX_CTRL_STAT_REG);
	reg &= ~ISL38XX_CTRL_STAT_RESET;
	reg &= ~ISL38XX_CTRL_STAT_RAMBOOT;
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	wmb();
	udelay(ISL38XX_WRITEIO_DELAY);

	/* set the Reset bit without reading the register ! */
	reg |= ISL38XX_CTRL_STAT_RESET;
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	wmb();
	udelay(ISL38XX_WRITEIO_DELAY);

	/* clear the Reset bit */
	reg &= ~ISL38XX_CTRL_STAT_RESET;
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	wmb();

	/* wait a while for the device to reboot */
	mdelay(50);

	{
		const struct firmware *fw_entry = NULL;
		long fw_len;
		const u32 *fw_ptr;

		rc = request_firmware(&fw_entry, priv->firmware, PRISM_FW_PDEV);
		if (rc) {
			printk(KERN_ERR
			       "%s: request_firmware() failed for '%s'\n",
			       "prism54", priv->firmware);
			return rc;
		}
		/* prepare the Direct Memory Base register */
		reg = ISL38XX_DEV_FIRMWARE_ADDRES;

		fw_ptr = (u32 *) fw_entry->data;
		fw_len = fw_entry->size;

		if (fw_len % 4) {
			printk(KERN_ERR
			       "%s: firmware '%s' size is not multiple of 32bit, aborting!\n",
			       "prism54", priv->firmware);
			release_firmware(fw_entry);
			return -EILSEQ; /* Illegal byte sequence  */;
		}

		while (fw_len > 0) {
			long _fw_len =
			    (fw_len >
			     ISL38XX_MEMORY_WINDOW_SIZE) ?
			    ISL38XX_MEMORY_WINDOW_SIZE : fw_len;
			u32 __iomem *dev_fw_ptr = device_base + ISL38XX_DIRECT_MEM_WIN;

			/* set the card's base address for writing the data */
			isl38xx_w32_flush(device_base, reg,
					  ISL38XX_DIR_MEM_BASE_REG);
			wmb();	/* be paranoid */

			/* increment the write address for next iteration */
			reg += _fw_len;
			fw_len -= _fw_len;

			/* write the data to the Direct Memory Window 32bit-wise */
			/* memcpy_toio() doesn't guarantee 32bit writes :-| */
			while (_fw_len > 0) {
				/* use non-swapping writel() */
				__raw_writel(*fw_ptr, dev_fw_ptr);
				fw_ptr++, dev_fw_ptr++;
				_fw_len -= 4;
			}

			/* flush PCI posting */
			(void) readl(device_base + ISL38XX_PCI_POSTING_FLUSH);
			wmb();	/* be paranoid again */

			BUG_ON(_fw_len != 0);
		}

		BUG_ON(fw_len != 0);

		/* Firmware version is at offset 40 (also for "newmac") */
		printk(KERN_DEBUG "%s: firmware version: %.8s\n",
		       priv->ndev->name, fw_entry->data + 40);

		release_firmware(fw_entry);
	}

	/* now reset the device
	 * clear the Reset & ClkRun bit, set the RAMBoot bit */
	reg = readl(device_base + ISL38XX_CTRL_STAT_REG);
	reg &= ~ISL38XX_CTRL_STAT_CLKRUN;
	reg &= ~ISL38XX_CTRL_STAT_RESET;
	reg |= ISL38XX_CTRL_STAT_RAMBOOT;
	isl38xx_w32_flush(device_base, reg, ISL38XX_CTRL_STAT_REG);
	wmb();
	udelay(ISL38XX_WRITEIO_DELAY);

	/* set the reset bit latches the host override and RAMBoot bits
	 * into the device for operation when the reset bit is reset */
	reg |= ISL38XX_CTRL_STAT_RESET;
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	/* don't do flush PCI posting here! */
	wmb();
	udelay(ISL38XX_WRITEIO_DELAY);

	/* clear the reset bit should start the whole circus */
	reg &= ~ISL38XX_CTRL_STAT_RESET;
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	/* don't do flush PCI posting here! */
	wmb();
	udelay(ISL38XX_WRITEIO_DELAY);

	return 0;
}

/******************************************************************************
    Device Interrupt Handler
******************************************************************************/

irqreturn_t
islpci_interrupt(int irq, void *config)
{
	u32 reg;
	islpci_private *priv = config;
	struct net_device *ndev = priv->ndev;
	void __iomem *device = priv->device_base;
	int powerstate = ISL38XX_PSM_POWERSAVE_STATE;

	/* lock the interrupt handler */
	spin_lock(&priv->slock);

	/* received an interrupt request on a shared IRQ line
	 * first check whether the device is in sleep mode */
	reg = readl(device + ISL38XX_CTRL_STAT_REG);
	if (reg & ISL38XX_CTRL_STAT_SLEEPMODE)
		/* device is in sleep mode, IRQ was generated by someone else */
	{
#if VERBOSE > SHOW_ERROR_MESSAGES
		DEBUG(SHOW_TRACING, "Assuming someone else called the IRQ\n");
#endif
		spin_unlock(&priv->slock);
		return IRQ_NONE;
	}


	/* check whether there is any source of interrupt on the device */
	reg = readl(device + ISL38XX_INT_IDENT_REG);

	/* also check the contents of the Interrupt Enable Register, because this
	 * will filter out interrupt sources from other devices on the same irq ! */
	reg &= readl(device + ISL38XX_INT_EN_REG);
	reg &= ISL38XX_INT_SOURCES;

	if (reg != 0) {
		if (islpci_get_state(priv) != PRV_STATE_SLEEP)
			powerstate = ISL38XX_PSM_ACTIVE_STATE;

		/* reset the request bits in the Identification register */
		isl38xx_w32_flush(device, reg, ISL38XX_INT_ACK_REG);

#if VERBOSE > SHOW_ERROR_MESSAGES
		DEBUG(SHOW_FUNCTION_CALLS,
		      "IRQ: Identification register 0x%p 0x%x\n", device, reg);
#endif

		/* check for each bit in the register separately */
		if (reg & ISL38XX_INT_IDENT_UPDATE) {
#if VERBOSE > SHOW_ERROR_MESSAGES
			/* Queue has been updated */
			DEBUG(SHOW_TRACING, "IRQ: Update flag\n");

			DEBUG(SHOW_QUEUE_INDEXES,
			      "CB drv Qs: [%i][%i][%i][%i][%i][%i]\n",
			      le32_to_cpu(priv->control_block->
					  driver_curr_frag[0]),
			      le32_to_cpu(priv->control_block->
					  driver_curr_frag[1]),
			      le32_to_cpu(priv->control_block->
					  driver_curr_frag[2]),
			      le32_to_cpu(priv->control_block->
					  driver_curr_frag[3]),
			      le32_to_cpu(priv->control_block->
					  driver_curr_frag[4]),
			      le32_to_cpu(priv->control_block->
					  driver_curr_frag[5])
			    );

			DEBUG(SHOW_QUEUE_INDEXES,
			      "CB dev Qs: [%i][%i][%i][%i][%i][%i]\n",
			      le32_to_cpu(priv->control_block->
					  device_curr_frag[0]),
			      le32_to_cpu(priv->control_block->
					  device_curr_frag[1]),
			      le32_to_cpu(priv->control_block->
					  device_curr_frag[2]),
			      le32_to_cpu(priv->control_block->
					  device_curr_frag[3]),
			      le32_to_cpu(priv->control_block->
					  device_curr_frag[4]),
			      le32_to_cpu(priv->control_block->
					  device_curr_frag[5])
			    );
#endif

			/* cleanup the data low transmit queue */
			islpci_eth_cleanup_transmit(priv, priv->control_block);

			/* device is in active state, update the
			 * powerstate flag if necessary */
			powerstate = ISL38XX_PSM_ACTIVE_STATE;

			/* check all three queues in priority order
			 * call the PIMFOR receive function until the
			 * queue is empty */
			if (isl38xx_in_queue(priv->control_block,
						ISL38XX_CB_RX_MGMTQ) != 0) {
#if VERBOSE > SHOW_ERROR_MESSAGES
				DEBUG(SHOW_TRACING,
				      "Received frame in Management Queue\n");
#endif
				islpci_mgt_receive(ndev);

				islpci_mgt_cleanup_transmit(ndev);

				/* Refill slots in receive queue */
				islpci_mgmt_rx_fill(ndev);

				/* no need to trigger the device, next
                                   islpci_mgt_transaction does it */
			}

			while (isl38xx_in_queue(priv->control_block,
						ISL38XX_CB_RX_DATA_LQ) != 0) {
#if VERBOSE > SHOW_ERROR_MESSAGES
				DEBUG(SHOW_TRACING,
				      "Received frame in Data Low Queue\n");
#endif
				islpci_eth_receive(priv);
			}

			/* check whether the data transmit queues were full */
			if (priv->data_low_tx_full) {
				/* check whether the transmit is not full anymore */
				if (ISL38XX_CB_TX_QSIZE -
				    isl38xx_in_queue(priv->control_block,
						     ISL38XX_CB_TX_DATA_LQ) >=
				    ISL38XX_MIN_QTHRESHOLD) {
					/* nope, the driver is ready for more network frames */
					netif_wake_queue(priv->ndev);

					/* reset the full flag */
					priv->data_low_tx_full = 0;
				}
			}
		}

		if (reg & ISL38XX_INT_IDENT_INIT) {
			/* Device has been initialized */
#if VERBOSE > SHOW_ERROR_MESSAGES
			DEBUG(SHOW_TRACING,
			      "IRQ: Init flag, device initialized\n");
#endif
			wake_up(&priv->reset_done);
		}

		if (reg & ISL38XX_INT_IDENT_SLEEP) {
			/* Device intends to move to powersave state */
#if VERBOSE > SHOW_ERROR_MESSAGES
			DEBUG(SHOW_TRACING, "IRQ: Sleep flag\n");
#endif
			isl38xx_handle_sleep_request(priv->control_block,
						     &powerstate,
						     priv->device_base);
		}

		if (reg & ISL38XX_INT_IDENT_WAKEUP) {
			/* Device has been woken up to active state */
#if VERBOSE > SHOW_ERROR_MESSAGES
			DEBUG(SHOW_TRACING, "IRQ: Wakeup flag\n");
#endif

			isl38xx_handle_wakeup(priv->control_block,
					      &powerstate, priv->device_base);
		}
	} else {
#if VERBOSE > SHOW_ERROR_MESSAGES
		DEBUG(SHOW_TRACING, "Assuming someone else called the IRQ\n");
#endif
		spin_unlock(&priv->slock);
		return IRQ_NONE;
	}

	/* sleep -> ready */
	if (islpci_get_state(priv) == PRV_STATE_SLEEP
	    && powerstate == ISL38XX_PSM_ACTIVE_STATE)
		islpci_set_state(priv, PRV_STATE_READY);

	/* !sleep -> sleep */
	if (islpci_get_state(priv) != PRV_STATE_SLEEP
	    && powerstate == ISL38XX_PSM_POWERSAVE_STATE)
		islpci_set_state(priv, PRV_STATE_SLEEP);

	/* unlock the interrupt handler */
	spin_unlock(&priv->slock);

	return IRQ_HANDLED;
}

/******************************************************************************
    Network Interface Control & Statistical functions
******************************************************************************/
static int
islpci_open(struct net_device *ndev)
{
	u32 rc;
	islpci_private *priv = netdev_priv(ndev);

	/* reset data structures, upload firmware and reset device */
	rc = islpci_reset(priv,1);
	if (rc) {
		prism54_bring_down(priv);
		return rc; /* Returns informative message */
	}

	netif_start_queue(ndev);

	/* Turn off carrier if in STA or Ad-hoc mode. It will be turned on
	 * once the firmware receives a trap of being associated
	 * (GEN_OID_LINKSTATE). In other modes (AP or WDS or monitor) we
	 * should just leave the carrier on as its expected the firmware
	 * won't send us a trigger. */
	if (priv->iw_mode == IW_MODE_INFRA || priv->iw_mode == IW_MODE_ADHOC)
		netif_carrier_off(ndev);
	else
		netif_carrier_on(ndev);

	return 0;
}

static int
islpci_close(struct net_device *ndev)
{
	islpci_private *priv = netdev_priv(ndev);

	printk(KERN_DEBUG "%s: islpci_close ()\n", ndev->name);

	netif_stop_queue(ndev);

	return prism54_bring_down(priv);
}

static int
prism54_bring_down(islpci_private *priv)
{
	void __iomem *device_base = priv->device_base;
	u32 reg;
	/* we are going to shutdown the device */
	islpci_set_state(priv, PRV_STATE_PREBOOT);

	/* disable all device interrupts in case they weren't */
	isl38xx_disable_interrupts(priv->device_base);

	/* For safety reasons, we may want to ensure that no DMA transfer is
	 * currently in progress by emptying the TX and RX queues. */

	/* wait until interrupts have finished executing on other CPUs */
	synchronize_irq(priv->pdev->irq);

	reg = readl(device_base + ISL38XX_CTRL_STAT_REG);
	reg &= ~(ISL38XX_CTRL_STAT_RESET | ISL38XX_CTRL_STAT_RAMBOOT);
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	wmb();
	udelay(ISL38XX_WRITEIO_DELAY);

	reg |= ISL38XX_CTRL_STAT_RESET;
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	wmb();
	udelay(ISL38XX_WRITEIO_DELAY);

	/* clear the Reset bit */
	reg &= ~ISL38XX_CTRL_STAT_RESET;
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	wmb();

	/* wait a while for the device to reset */
	schedule_timeout_uninterruptible(msecs_to_jiffies(50));

	return 0;
}

static int
islpci_upload_fw(islpci_private *priv)
{
	islpci_state_t old_state;
	u32 rc;

	old_state = islpci_set_state(priv, PRV_STATE_BOOT);

	printk(KERN_DEBUG "%s: uploading firmware...\n", priv->ndev->name);

	rc = isl_upload_firmware(priv);
	if (rc) {
		/* error uploading the firmware */
		printk(KERN_ERR "%s: could not upload firmware ('%s')\n",
		       priv->ndev->name, priv->firmware);

		islpci_set_state(priv, old_state);
		return rc;
	}

	printk(KERN_DEBUG "%s: firmware upload complete\n",
	       priv->ndev->name);

	islpci_set_state(priv, PRV_STATE_POSTBOOT);

	return 0;
}

static int
islpci_reset_if(islpci_private *priv)
{
	long remaining;
	int result = -ETIME;
	int count;

	DEFINE_WAIT(wait);
	prepare_to_wait(&priv->reset_done, &wait, TASK_UNINTERRUPTIBLE);

	/* now the last step is to reset the interface */
	isl38xx_interface_reset(priv->device_base, priv->device_host_address);
	islpci_set_state(priv, PRV_STATE_PREINIT);

        for(count = 0; count < 2 && result; count++) {
		/* The software reset acknowledge needs about 220 msec here.
		 * Be conservative and wait for up to one second. */

		remaining = schedule_timeout_uninterruptible(HZ);

		if(remaining > 0) {
			result = 0;
			break;
		}

		/* If we're here it's because our IRQ hasn't yet gone through.
		 * Retry a bit more...
		 */
		printk(KERN_ERR "%s: no 'reset complete' IRQ seen - retrying\n",
			priv->ndev->name);
	}

	finish_wait(&priv->reset_done, &wait);

	if (result) {
		printk(KERN_ERR "%s: interface reset failure\n", priv->ndev->name);
		return result;
	}

	islpci_set_state(priv, PRV_STATE_INIT);

	/* Now that the device is 100% up, let's allow
	 * for the other interrupts --
	 * NOTE: this is not *yet* true since we've only allowed the
	 * INIT interrupt on the IRQ line. We can perhaps poll
	 * the IRQ line until we know for sure the reset went through */
	isl38xx_enable_common_interrupts(priv->device_base);

	down_write(&priv->mib_sem);
	result = mgt_commit(priv);
	if (result) {
		printk(KERN_ERR "%s: interface reset failure\n", priv->ndev->name);
		up_write(&priv->mib_sem);
		return result;
	}
	up_write(&priv->mib_sem);

	islpci_set_state(priv, PRV_STATE_READY);

	printk(KERN_DEBUG "%s: interface reset complete\n", priv->ndev->name);
	return 0;
}

int
islpci_reset(islpci_private *priv, int reload_firmware)
{
	isl38xx_control_block *cb =    /* volatile not needed */
		(isl38xx_control_block *) priv->control_block;
	unsigned counter;
	int rc;

	if (reload_firmware)
		islpci_set_state(priv, PRV_STATE_PREBOOT);
	else
		islpci_set_state(priv, PRV_STATE_POSTBOOT);

	printk(KERN_DEBUG "%s: resetting device...\n", priv->ndev->name);

	/* disable all device interrupts in case they weren't */
	isl38xx_disable_interrupts(priv->device_base);

	/* flush all management queues */
	priv->index_mgmt_tx = 0;
	priv->index_mgmt_rx = 0;

	/* clear the indexes in the frame pointer */
	for (counter = 0; counter < ISL38XX_CB_QCOUNT; counter++) {
		cb->driver_curr_frag[counter] = cpu_to_le32(0);
		cb->device_curr_frag[counter] = cpu_to_le32(0);
	}

	/* reset the mgmt receive queue */
	for (counter = 0; counter < ISL38XX_CB_MGMT_QSIZE; counter++) {
		isl38xx_fragment *frag = &cb->rx_data_mgmt[counter];
		frag->size = cpu_to_le16(MGMT_FRAME_SIZE);
		frag->flags = 0;
		frag->address = cpu_to_le32(priv->mgmt_rx[counter].pci_addr);
	}

	for (counter = 0; counter < ISL38XX_CB_RX_QSIZE; counter++) {
		cb->rx_data_low[counter].address =
		    cpu_to_le32((u32) priv->pci_map_rx_address[counter]);
	}

	/* since the receive queues are filled with empty fragments, now we can
	 * set the corresponding indexes in the Control Block */
	priv->control_block->driver_curr_frag[ISL38XX_CB_RX_DATA_LQ] =
	    cpu_to_le32(ISL38XX_CB_RX_QSIZE);
	priv->control_block->driver_curr_frag[ISL38XX_CB_RX_MGMTQ] =
	    cpu_to_le32(ISL38XX_CB_MGMT_QSIZE);

	/* reset the remaining real index registers and full flags */
	priv->free_data_rx = 0;
	priv->free_data_tx = 0;
	priv->data_low_tx_full = 0;

	if (reload_firmware) { /* Should we load the firmware ? */
	/* now that the data structures are cleaned up, upload
	 * firmware and reset interface */
		rc = islpci_upload_fw(priv);
		if (rc) {
			printk(KERN_ERR "%s: islpci_reset: failure\n",
				priv->ndev->name);
			return rc;
		}
	}

	/* finally reset interface */
	rc = islpci_reset_if(priv);
	if (rc)
		printk(KERN_ERR "prism54: Your card/socket may be faulty, or IRQ line too busy :(\n");
	return rc;
}

/******************************************************************************
    Network device configuration functions
******************************************************************************/
static int
islpci_alloc_memory(islpci_private *priv)
{
	int counter;

#if VERBOSE > SHOW_ERROR_MESSAGES
	printk(KERN_DEBUG "islpci_alloc_memory\n");
#endif

	/* remap the PCI device base address to accessible */
	if (!(priv->device_base =
	      ioremap(pci_resource_start(priv->pdev, 0),
		      ISL38XX_PCI_MEM_SIZE))) {
		/* error in remapping the PCI device memory address range */
		printk(KERN_ERR "PCI memory remapping failed\n");
		return -1;
	}

	/* memory layout for consistent DMA region:
	 *
	 * Area 1: Control Block for the device interface
	 * Area 2: Power Save Mode Buffer for temporary frame storage. Be aware that
	 *         the number of supported stations in the AP determines the minimal
	 *         size of the buffer !
	 */

	/* perform the allocation */
	priv->driver_mem_address = pci_alloc_consistent(priv->pdev,
							HOST_MEM_BLOCK,
							&priv->
							device_host_address);

	if (!priv->driver_mem_address) {
		/* error allocating the block of PCI memory */
		printk(KERN_ERR "%s: could not allocate DMA memory, aborting!",
		       "prism54");
		return -1;
	}

	/* assign the Control Block to the first address of the allocated area */
	priv->control_block =
	    (isl38xx_control_block *) priv->driver_mem_address;

	/* set the Power Save Buffer pointer directly behind the CB */
	priv->device_psm_buffer =
		priv->device_host_address + CONTROL_BLOCK_SIZE;

	/* make sure all buffer pointers are initialized */
	for (counter = 0; counter < ISL38XX_CB_QCOUNT; counter++) {
		priv->control_block->driver_curr_frag[counter] = cpu_to_le32(0);
		priv->control_block->device_curr_frag[counter] = cpu_to_le32(0);
	}

	priv->index_mgmt_rx = 0;
	memset(priv->mgmt_rx, 0, sizeof(priv->mgmt_rx));
	memset(priv->mgmt_tx, 0, sizeof(priv->mgmt_tx));

	/* allocate rx queue for management frames */
	if (islpci_mgmt_rx_fill(priv->ndev) < 0)
		goto out_free;

	/* now get the data rx skb's */
	memset(priv->data_low_rx, 0, sizeof (priv->data_low_rx));
	memset(priv->pci_map_rx_address, 0, sizeof (priv->pci_map_rx_address));

	for (counter = 0; counter < ISL38XX_CB_RX_QSIZE; counter++) {
		struct sk_buff *skb;

		/* allocate an sk_buff for received data frames storage
		 * each frame on receive size consists of 1 fragment
		 * include any required allignment operations */
		if (!(skb = dev_alloc_skb(MAX_FRAGMENT_SIZE_RX + 2))) {
			/* error allocating an sk_buff structure elements */
			printk(KERN_ERR "Error allocating skb.\n");
			skb = NULL;
			goto out_free;
		}
		skb_reserve(skb, (4 - (long) skb->data) & 0x03);
		/* add the new allocated sk_buff to the buffer array */
		priv->data_low_rx[counter] = skb;

		/* map the allocated skb data area to pci */
		priv->pci_map_rx_address[counter] =
		    pci_map_single(priv->pdev, (void *) skb->data,
				   MAX_FRAGMENT_SIZE_RX + 2,
				   PCI_DMA_FROMDEVICE);
		if (!priv->pci_map_rx_address[counter]) {
			/* error mapping the buffer to device
			   accessible memory address */
			printk(KERN_ERR "failed to map skb DMA'able\n");
			goto out_free;
		}
	}

	prism54_acl_init(&priv->acl);
	prism54_wpa_bss_ie_init(priv);
	if (mgt_init(priv))
		goto out_free;

	return 0;
 out_free:
	islpci_free_memory(priv);
	return -1;
}

int
islpci_free_memory(islpci_private *priv)
{
	int counter;

	if (priv->device_base)
		iounmap(priv->device_base);
	priv->device_base = NULL;

	/* free consistent DMA area... */
	if (priv->driver_mem_address)
		pci_free_consistent(priv->pdev, HOST_MEM_BLOCK,
				    priv->driver_mem_address,
				    priv->device_host_address);

	/* clear some dangling pointers */
	priv->driver_mem_address = NULL;
	priv->device_host_address = 0;
	priv->device_psm_buffer = 0;
	priv->control_block = NULL;

        /* clean up mgmt rx buffers */
        for (counter = 0; counter < ISL38XX_CB_MGMT_QSIZE; counter++) {
		struct islpci_membuf *buf = &priv->mgmt_rx[counter];
		if (buf->pci_addr)
			pci_unmap_single(priv->pdev, buf->pci_addr,
					 buf->size, PCI_DMA_FROMDEVICE);
		buf->pci_addr = 0;
		kfree(buf->mem);
		buf->size = 0;
		buf->mem = NULL;
        }

	/* clean up data rx buffers */
	for (counter = 0; counter < ISL38XX_CB_RX_QSIZE; counter++) {
		if (priv->pci_map_rx_address[counter])
			pci_unmap_single(priv->pdev,
					 priv->pci_map_rx_address[counter],
					 MAX_FRAGMENT_SIZE_RX + 2,
					 PCI_DMA_FROMDEVICE);
		priv->pci_map_rx_address[counter] = 0;

		if (priv->data_low_rx[counter])
			dev_kfree_skb(priv->data_low_rx[counter]);
		priv->data_low_rx[counter] = NULL;
	}

	/* Free the access control list and the WPA list */
	prism54_acl_clean(&priv->acl);
	prism54_wpa_bss_ie_clean(priv);
	mgt_clean(priv);

	return 0;
}

#if 0
static void
islpci_set_multicast_list(struct net_device *dev)
{
	/* put device into promisc mode and let network layer handle it */
}
#endif

static void islpci_ethtool_get_drvinfo(struct net_device *dev,
                                       struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
}

static const struct ethtool_ops islpci_ethtool_ops = {
	.get_drvinfo = islpci_ethtool_get_drvinfo,
};

static const struct net_device_ops islpci_netdev_ops = {
	.ndo_open 		= islpci_open,
	.ndo_stop		= islpci_close,
	.ndo_start_xmit		= islpci_eth_transmit,
	.ndo_tx_timeout		= islpci_eth_tx_timeout,
	.ndo_set_mac_address 	= prism54_set_mac_address,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
};

static struct device_type wlan_type = {
	.name	= "wlan",
};

struct net_device *
islpci_setup(struct pci_dev *pdev)
{
	islpci_private *priv;
	struct net_device *ndev = alloc_etherdev(sizeof (islpci_private));

	if (!ndev)
		return ndev;

	pci_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);
	SET_NETDEV_DEVTYPE(ndev, &wlan_type);

	/* setup the structure members */
	ndev->base_addr = pci_resource_start(pdev, 0);
	ndev->irq = pdev->irq;

	/* initialize the function pointers */
	ndev->netdev_ops = &islpci_netdev_ops;
	ndev->wireless_handlers = &prism54_handler_def;
	ndev->ethtool_ops = &islpci_ethtool_ops;

	/* ndev->set_multicast_list = &islpci_set_multicast_list; */
	ndev->addr_len = ETH_ALEN;
	/* Get a non-zero dummy MAC address for nameif. Jean II */
	memcpy(ndev->dev_addr, dummy_mac, ETH_ALEN);

	ndev->watchdog_timeo = ISLPCI_TX_TIMEOUT;

	/* allocate a private device structure to the network device  */
	priv = netdev_priv(ndev);
	priv->ndev = ndev;
	priv->pdev = pdev;
	priv->monitor_type = ARPHRD_IEEE80211;
	priv->ndev->type = (priv->iw_mode == IW_MODE_MONITOR) ?
		priv->monitor_type : ARPHRD_ETHER;

	/* Add pointers to enable iwspy support. */
	priv->wireless_data.spy_data = &priv->spy_data;
	ndev->wireless_data = &priv->wireless_data;

	/* save the start and end address of the PCI memory area */
	ndev->mem_start = (unsigned long) priv->device_base;
	ndev->mem_end = ndev->mem_start + ISL38XX_PCI_MEM_SIZE;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_TRACING, "PCI Memory remapped to 0x%p\n", priv->device_base);
#endif

	init_waitqueue_head(&priv->reset_done);

	/* init the queue read locks, process wait counter */
	mutex_init(&priv->mgmt_lock);
	priv->mgmt_received = NULL;
	init_waitqueue_head(&priv->mgmt_wqueue);
	mutex_init(&priv->stats_lock);
	spin_lock_init(&priv->slock);

	/* init state machine with off#1 state */
	priv->state = PRV_STATE_OFF;
	priv->state_off = 1;

	/* initialize workqueue's */
	INIT_WORK(&priv->stats_work, prism54_update_stats);
	priv->stats_timestamp = 0;

	INIT_WORK(&priv->reset_task, islpci_do_reset_and_wake);
	priv->reset_task_pending = 0;

	/* allocate various memory areas */
	if (islpci_alloc_memory(priv))
		goto do_free_netdev;

	/* select the firmware file depending on the device id */
	switch (pdev->device) {
	case 0x3877:
		strcpy(priv->firmware, ISL3877_IMAGE_FILE);
		break;

	case 0x3886:
		strcpy(priv->firmware, ISL3886_IMAGE_FILE);
		break;

	default:
		strcpy(priv->firmware, ISL3890_IMAGE_FILE);
		break;
	}

	if (register_netdev(ndev)) {
		DEBUG(SHOW_ERROR_MESSAGES,
		      "ERROR: register_netdev() failed\n");
		goto do_islpci_free_memory;
	}

	return ndev;

      do_islpci_free_memory:
	islpci_free_memory(priv);
      do_free_netdev:
	pci_set_drvdata(pdev, NULL);
	free_netdev(ndev);
	priv = NULL;
	return NULL;
}

islpci_state_t
islpci_set_state(islpci_private *priv, islpci_state_t new_state)
{
	islpci_state_t old_state;

	/* lock */
	old_state = priv->state;

	/* this means either a race condition or some serious error in
	 * the driver code */
	switch (new_state) {
	case PRV_STATE_OFF:
		priv->state_off++;
	default:
		priv->state = new_state;
		break;

	case PRV_STATE_PREBOOT:
		/* there are actually many off-states, enumerated by
		 * state_off */
		if (old_state == PRV_STATE_OFF)
			priv->state_off--;

		/* only if hw_unavailable is zero now it means we either
		 * were in off#1 state, or came here from
		 * somewhere else */
		if (!priv->state_off)
			priv->state = new_state;
		break;
	}
#if 0
	printk(KERN_DEBUG "%s: state transition %d -> %d (off#%d)\n",
	       priv->ndev->name, old_state, new_state, priv->state_off);
#endif

	/* invariants */
	BUG_ON(priv->state_off < 0);
	BUG_ON(priv->state_off && (priv->state != PRV_STATE_OFF));
	BUG_ON(!priv->state_off && (priv->state == PRV_STATE_OFF));

	/* unlock */
	return old_state;
}
