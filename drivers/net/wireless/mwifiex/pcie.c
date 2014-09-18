/*
 * Marvell Wireless LAN device driver: PCIE specific handling
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include <linux/firmware.h>

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "wmm.h"
#include "11n.h"
#include "pcie.h"

#define PCIE_VERSION	"1.0"
#define DRV_NAME        "Marvell mwifiex PCIe"

static u8 user_rmmod;

static struct mwifiex_if_ops pcie_ops;

static struct semaphore add_remove_card_sem;

static struct memory_type_mapping mem_type_mapping_tbl[] = {
	{"ITCM", NULL, 0, 0xF0},
	{"DTCM", NULL, 0, 0xF1},
	{"SQRAM", NULL, 0, 0xF2},
	{"IRAM", NULL, 0, 0xF3},
};

static int
mwifiex_map_pci_memory(struct mwifiex_adapter *adapter, struct sk_buff *skb,
		       size_t size, int flags)
{
	struct pcie_service_card *card = adapter->card;
	struct mwifiex_dma_mapping mapping;

	mapping.addr = pci_map_single(card->dev, skb->data, size, flags);
	if (pci_dma_mapping_error(card->dev, mapping.addr)) {
		dev_err(adapter->dev, "failed to map pci memory!\n");
		return -1;
	}
	mapping.len = size;
	mwifiex_store_mapping(skb, &mapping);
	return 0;
}

static void mwifiex_unmap_pci_memory(struct mwifiex_adapter *adapter,
				     struct sk_buff *skb, int flags)
{
	struct pcie_service_card *card = adapter->card;
	struct mwifiex_dma_mapping mapping;

	mwifiex_get_mapping(skb, &mapping);
	pci_unmap_single(card->dev, mapping.addr, mapping.len, flags);
}

/*
 * This function reads sleep cookie and checks if FW is ready
 */
static bool mwifiex_pcie_ok_to_access_hw(struct mwifiex_adapter *adapter)
{
	u32 *cookie_addr;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	if (!reg->sleep_cookie)
		return true;

	if (card->sleep_cookie_vbase) {
		cookie_addr = (u32 *)card->sleep_cookie_vbase;
		dev_dbg(adapter->dev, "info: ACCESS_HW: sleep cookie=0x%x\n",
			*cookie_addr);
		if (*cookie_addr == FW_AWAKE_COOKIE)
			return true;
	}

	return false;
}

#ifdef CONFIG_PM_SLEEP
/*
 * Kernel needs to suspend all functions separately. Therefore all
 * registered functions must have drivers with suspend and resume
 * methods. Failing that the kernel simply removes the whole card.
 *
 * If already not suspended, this function allocates and sends a host
 * sleep activate request to the firmware and turns off the traffic.
 */
static int mwifiex_pcie_suspend(struct device *dev)
{
	struct mwifiex_adapter *adapter;
	struct pcie_service_card *card;
	int hs_actived;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (pdev) {
		card = pci_get_drvdata(pdev);
		if (!card || !card->adapter) {
			pr_err("Card or adapter structure is not valid\n");
			return 0;
		}
	} else {
		pr_err("PCIE device is not specified\n");
		return 0;
	}

	adapter = card->adapter;

	hs_actived = mwifiex_enable_hs(adapter);

	/* Indicate device suspended */
	adapter->is_suspended = true;
	adapter->hs_enabling = false;

	return 0;
}

/*
 * Kernel needs to suspend all functions separately. Therefore all
 * registered functions must have drivers with suspend and resume
 * methods. Failing that the kernel simply removes the whole card.
 *
 * If already not resumed, this function turns on the traffic and
 * sends a host sleep cancel request to the firmware.
 */
static int mwifiex_pcie_resume(struct device *dev)
{
	struct mwifiex_adapter *adapter;
	struct pcie_service_card *card;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (pdev) {
		card = pci_get_drvdata(pdev);
		if (!card || !card->adapter) {
			pr_err("Card or adapter structure is not valid\n");
			return 0;
		}
	} else {
		pr_err("PCIE device is not specified\n");
		return 0;
	}

	adapter = card->adapter;

	if (!adapter->is_suspended) {
		dev_warn(adapter->dev, "Device already resumed\n");
		return 0;
	}

	adapter->is_suspended = false;

	mwifiex_cancel_hs(mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_STA),
			  MWIFIEX_ASYNC_CMD);

	return 0;
}
#endif

/*
 * This function probes an mwifiex device and registers it. It allocates
 * the card structure, enables PCIE function number and initiates the
 * device registration and initialization procedure by adding a logical
 * interface.
 */
static int mwifiex_pcie_probe(struct pci_dev *pdev,
					const struct pci_device_id *ent)
{
	struct pcie_service_card *card;

	pr_debug("info: vendor=0x%4.04X device=0x%4.04X rev=%d\n",
		 pdev->vendor, pdev->device, pdev->revision);

	card = kzalloc(sizeof(struct pcie_service_card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->dev = pdev;

	if (ent->driver_data) {
		struct mwifiex_pcie_device *data = (void *)ent->driver_data;
		card->pcie.firmware = data->firmware;
		card->pcie.reg = data->reg;
		card->pcie.blksz_fw_dl = data->blksz_fw_dl;
		card->pcie.tx_buf_size = data->tx_buf_size;
		card->pcie.supports_fw_dump = data->supports_fw_dump;
	}

	if (mwifiex_add_card(card, &add_remove_card_sem, &pcie_ops,
			     MWIFIEX_PCIE)) {
		pr_err("%s failed\n", __func__);
		kfree(card);
		return -1;
	}

	return 0;
}

/*
 * This function removes the interface and frees up the card structure.
 */
static void mwifiex_pcie_remove(struct pci_dev *pdev)
{
	struct pcie_service_card *card;
	struct mwifiex_adapter *adapter;
	struct mwifiex_private *priv;

	card = pci_get_drvdata(pdev);
	if (!card)
		return;

	adapter = card->adapter;
	if (!adapter || !adapter->priv_num)
		return;

	cancel_work_sync(&adapter->iface_work);

	if (user_rmmod) {
#ifdef CONFIG_PM_SLEEP
		if (adapter->is_suspended)
			mwifiex_pcie_resume(&pdev->dev);
#endif

		mwifiex_deauthenticate_all(adapter);

		priv = mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);

		mwifiex_disable_auto_ds(priv);

		mwifiex_init_shutdown_fw(priv, MWIFIEX_FUNC_SHUTDOWN);
	}

	mwifiex_remove_card(card->adapter, &add_remove_card_sem);
}

static void mwifiex_pcie_shutdown(struct pci_dev *pdev)
{
	user_rmmod = 1;
	mwifiex_pcie_remove(pdev);

	return;
}

static const struct pci_device_id mwifiex_ids[] = {
	{
		PCIE_VENDOR_ID_MARVELL, PCIE_DEVICE_ID_MARVELL_88W8766P,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		.driver_data = (unsigned long) &mwifiex_pcie8766,
	},
	{
		PCIE_VENDOR_ID_MARVELL, PCIE_DEVICE_ID_MARVELL_88W8897,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		.driver_data = (unsigned long) &mwifiex_pcie8897,
	},
	{},
};

MODULE_DEVICE_TABLE(pci, mwifiex_ids);

#ifdef CONFIG_PM_SLEEP
/* Power Management Hooks */
static SIMPLE_DEV_PM_OPS(mwifiex_pcie_pm_ops, mwifiex_pcie_suspend,
				mwifiex_pcie_resume);
#endif

/* PCI Device Driver */
static struct pci_driver __refdata mwifiex_pcie = {
	.name     = "mwifiex_pcie",
	.id_table = mwifiex_ids,
	.probe    = mwifiex_pcie_probe,
	.remove   = mwifiex_pcie_remove,
#ifdef CONFIG_PM_SLEEP
	.driver   = {
		.pm = &mwifiex_pcie_pm_ops,
	},
#endif
	.shutdown = mwifiex_pcie_shutdown,
};

/*
 * This function writes data into PCIE card register.
 */
static int mwifiex_write_reg(struct mwifiex_adapter *adapter, int reg, u32 data)
{
	struct pcie_service_card *card = adapter->card;

	iowrite32(data, card->pci_mmap1 + reg);

	return 0;
}

/*
 * This function reads data from PCIE card register.
 */
static int mwifiex_read_reg(struct mwifiex_adapter *adapter, int reg, u32 *data)
{
	struct pcie_service_card *card = adapter->card;

	*data = ioread32(card->pci_mmap1 + reg);

	return 0;
}

/* This function reads u8 data from PCIE card register. */
static int mwifiex_read_reg_byte(struct mwifiex_adapter *adapter,
				 int reg, u8 *data)
{
	struct pcie_service_card *card = adapter->card;

	*data = ioread8(card->pci_mmap1 + reg);

	return 0;
}

/*
 * This function adds delay loop to ensure FW is awake before proceeding.
 */
static void mwifiex_pcie_dev_wakeup_delay(struct mwifiex_adapter *adapter)
{
	int i = 0;

	while (mwifiex_pcie_ok_to_access_hw(adapter)) {
		i++;
		usleep_range(10, 20);
		/* 50ms max wait */
		if (i == 5000)
			break;
	}

	return;
}

static void mwifiex_delay_for_sleep_cookie(struct mwifiex_adapter *adapter,
					   u32 max_delay_loop_cnt)
{
	struct pcie_service_card *card = adapter->card;
	u8 *buffer;
	u32 sleep_cookie, count;

	for (count = 0; count < max_delay_loop_cnt; count++) {
		buffer = card->cmdrsp_buf->data - INTF_HEADER_LEN;
		sleep_cookie = *(u32 *)buffer;

		if (sleep_cookie == MWIFIEX_DEF_SLEEP_COOKIE) {
			dev_dbg(adapter->dev,
				"sleep cookie found at count %d\n", count);
			break;
		}
		usleep_range(20, 30);
	}

	if (count >= max_delay_loop_cnt)
		dev_dbg(adapter->dev,
			"max count reached while accessing sleep cookie\n");
}

/* This function wakes up the card by reading fw_status register. */
static int mwifiex_pm_wakeup_card(struct mwifiex_adapter *adapter)
{
	u32 fw_status;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	dev_dbg(adapter->dev, "event: Wakeup device...\n");

	if (reg->sleep_cookie)
		mwifiex_pcie_dev_wakeup_delay(adapter);

	/* Reading fw_status register will wakeup device */
	if (mwifiex_read_reg(adapter, reg->fw_status, &fw_status)) {
		dev_warn(adapter->dev, "Reading fw_status register failed\n");
		return -1;
	}

	if (reg->sleep_cookie) {
		mwifiex_pcie_dev_wakeup_delay(adapter);
		dev_dbg(adapter->dev, "PCIE wakeup: Setting PS_STATE_AWAKE\n");
		adapter->ps_state = PS_STATE_AWAKE;
	}

	return 0;
}

/*
 * This function is called after the card has woken up.
 *
 * The card configuration register is reset.
 */
static int mwifiex_pm_wakeup_card_complete(struct mwifiex_adapter *adapter)
{
	dev_dbg(adapter->dev, "cmd: Wakeup device completed\n");

	return 0;
}

/*
 * This function disables the host interrupt.
 *
 * The host interrupt mask is read, the disable bit is reset and
 * written back to the card host interrupt mask register.
 */
static int mwifiex_pcie_disable_host_int(struct mwifiex_adapter *adapter)
{
	if (mwifiex_pcie_ok_to_access_hw(adapter)) {
		if (mwifiex_write_reg(adapter, PCIE_HOST_INT_MASK,
				      0x00000000)) {
			dev_warn(adapter->dev, "Disable host interrupt failed\n");
			return -1;
		}
	}

	return 0;
}

/*
 * This function enables the host interrupt.
 *
 * The host interrupt enable mask is written to the card
 * host interrupt mask register.
 */
static int mwifiex_pcie_enable_host_int(struct mwifiex_adapter *adapter)
{
	if (mwifiex_pcie_ok_to_access_hw(adapter)) {
		/* Simply write the mask to the register */
		if (mwifiex_write_reg(adapter, PCIE_HOST_INT_MASK,
				      HOST_INTR_MASK)) {
			dev_warn(adapter->dev, "Enable host interrupt failed\n");
			return -1;
		}
	}

	return 0;
}

/*
 * This function initializes TX buffer ring descriptors
 */
static int mwifiex_init_txq_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	struct mwifiex_pcie_buf_desc *desc;
	struct mwifiex_pfu_buf_desc *desc2;
	int i;

	for (i = 0; i < MWIFIEX_MAX_TXRX_BD; i++) {
		card->tx_buf_list[i] = NULL;
		if (reg->pfu_enabled) {
			card->txbd_ring[i] = (void *)card->txbd_ring_vbase +
					     (sizeof(*desc2) * i);
			desc2 = card->txbd_ring[i];
			memset(desc2, 0, sizeof(*desc2));
		} else {
			card->txbd_ring[i] = (void *)card->txbd_ring_vbase +
					     (sizeof(*desc) * i);
			desc = card->txbd_ring[i];
			memset(desc, 0, sizeof(*desc));
		}
	}

	return 0;
}

/* This function initializes RX buffer ring descriptors. Each SKB is allocated
 * here and after mapping PCI memory, its physical address is assigned to
 * PCIE Rx buffer descriptor's physical address.
 */
static int mwifiex_init_rxq_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	struct sk_buff *skb;
	struct mwifiex_pcie_buf_desc *desc;
	struct mwifiex_pfu_buf_desc *desc2;
	dma_addr_t buf_pa;
	int i;

	for (i = 0; i < MWIFIEX_MAX_TXRX_BD; i++) {
		/* Allocate skb here so that firmware can DMA data from it */
		skb = dev_alloc_skb(MWIFIEX_RX_DATA_BUF_SIZE);
		if (!skb) {
			dev_err(adapter->dev,
				"Unable to allocate skb for RX ring.\n");
			kfree(card->rxbd_ring_vbase);
			return -ENOMEM;
		}

		if (mwifiex_map_pci_memory(adapter, skb,
					   MWIFIEX_RX_DATA_BUF_SIZE,
					   PCI_DMA_FROMDEVICE))
			return -1;

		buf_pa = MWIFIEX_SKB_DMA_ADDR(skb);

		dev_dbg(adapter->dev,
			"info: RX ring: skb=%p len=%d data=%p buf_pa=%#x:%x\n",
			skb, skb->len, skb->data, (u32)buf_pa,
			(u32)((u64)buf_pa >> 32));

		card->rx_buf_list[i] = skb;
		if (reg->pfu_enabled) {
			card->rxbd_ring[i] = (void *)card->rxbd_ring_vbase +
					     (sizeof(*desc2) * i);
			desc2 = card->rxbd_ring[i];
			desc2->paddr = buf_pa;
			desc2->len = (u16)skb->len;
			desc2->frag_len = (u16)skb->len;
			desc2->flags = reg->ring_flag_eop | reg->ring_flag_sop;
			desc2->offset = 0;
		} else {
			card->rxbd_ring[i] = (void *)(card->rxbd_ring_vbase +
					     (sizeof(*desc) * i));
			desc = card->rxbd_ring[i];
			desc->paddr = buf_pa;
			desc->len = (u16)skb->len;
			desc->flags = 0;
		}
	}

	return 0;
}

/* This function initializes event buffer ring descriptors. Each SKB is
 * allocated here and after mapping PCI memory, its physical address is assigned
 * to PCIE Rx buffer descriptor's physical address
 */
static int mwifiex_pcie_init_evt_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	struct mwifiex_evt_buf_desc *desc;
	struct sk_buff *skb;
	dma_addr_t buf_pa;
	int i;

	for (i = 0; i < MWIFIEX_MAX_EVT_BD; i++) {
		/* Allocate skb here so that firmware can DMA data from it */
		skb = dev_alloc_skb(MAX_EVENT_SIZE);
		if (!skb) {
			dev_err(adapter->dev,
				"Unable to allocate skb for EVENT buf.\n");
			kfree(card->evtbd_ring_vbase);
			return -ENOMEM;
		}
		skb_put(skb, MAX_EVENT_SIZE);

		if (mwifiex_map_pci_memory(adapter, skb, MAX_EVENT_SIZE,
					   PCI_DMA_FROMDEVICE))
			return -1;

		buf_pa = MWIFIEX_SKB_DMA_ADDR(skb);

		dev_dbg(adapter->dev,
			"info: EVT ring: skb=%p len=%d data=%p buf_pa=%#x:%x\n",
			skb, skb->len, skb->data, (u32)buf_pa,
			(u32)((u64)buf_pa >> 32));

		card->evt_buf_list[i] = skb;
		card->evtbd_ring[i] = (void *)(card->evtbd_ring_vbase +
				      (sizeof(*desc) * i));
		desc = card->evtbd_ring[i];
		desc->paddr = buf_pa;
		desc->len = (u16)skb->len;
		desc->flags = 0;
	}

	return 0;
}

/* This function cleans up TX buffer rings. If any of the buffer list has valid
 * SKB address, associated SKB is freed.
 */
static void mwifiex_cleanup_txq_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	struct sk_buff *skb;
	struct mwifiex_pcie_buf_desc *desc;
	struct mwifiex_pfu_buf_desc *desc2;
	int i;

	for (i = 0; i < MWIFIEX_MAX_TXRX_BD; i++) {
		if (reg->pfu_enabled) {
			desc2 = card->txbd_ring[i];
			if (card->tx_buf_list[i]) {
				skb = card->tx_buf_list[i];
				mwifiex_unmap_pci_memory(adapter, skb,
							 PCI_DMA_TODEVICE);
				dev_kfree_skb_any(skb);
			}
			memset(desc2, 0, sizeof(*desc2));
		} else {
			desc = card->txbd_ring[i];
			if (card->tx_buf_list[i]) {
				skb = card->tx_buf_list[i];
				mwifiex_unmap_pci_memory(adapter, skb,
							 PCI_DMA_TODEVICE);
				dev_kfree_skb_any(skb);
			}
			memset(desc, 0, sizeof(*desc));
		}
		card->tx_buf_list[i] = NULL;
	}

	return;
}

/* This function cleans up RX buffer rings. If any of the buffer list has valid
 * SKB address, associated SKB is freed.
 */
static void mwifiex_cleanup_rxq_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	struct mwifiex_pcie_buf_desc *desc;
	struct mwifiex_pfu_buf_desc *desc2;
	struct sk_buff *skb;
	int i;

	for (i = 0; i < MWIFIEX_MAX_TXRX_BD; i++) {
		if (reg->pfu_enabled) {
			desc2 = card->rxbd_ring[i];
			if (card->rx_buf_list[i]) {
				skb = card->rx_buf_list[i];
				mwifiex_unmap_pci_memory(adapter, skb,
							 PCI_DMA_FROMDEVICE);
				dev_kfree_skb_any(skb);
			}
			memset(desc2, 0, sizeof(*desc2));
		} else {
			desc = card->rxbd_ring[i];
			if (card->rx_buf_list[i]) {
				skb = card->rx_buf_list[i];
				mwifiex_unmap_pci_memory(adapter, skb,
							 PCI_DMA_FROMDEVICE);
				dev_kfree_skb_any(skb);
			}
			memset(desc, 0, sizeof(*desc));
		}
		card->rx_buf_list[i] = NULL;
	}

	return;
}

/* This function cleans up event buffer rings. If any of the buffer list has
 * valid SKB address, associated SKB is freed.
 */
static void mwifiex_cleanup_evt_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	struct mwifiex_evt_buf_desc *desc;
	struct sk_buff *skb;
	int i;

	for (i = 0; i < MWIFIEX_MAX_EVT_BD; i++) {
		desc = card->evtbd_ring[i];
		if (card->evt_buf_list[i]) {
			skb = card->evt_buf_list[i];
			mwifiex_unmap_pci_memory(adapter, skb,
						 PCI_DMA_FROMDEVICE);
			dev_kfree_skb_any(skb);
		}
		card->evt_buf_list[i] = NULL;
		memset(desc, 0, sizeof(*desc));
	}

	return;
}

/* This function creates buffer descriptor ring for TX
 */
static int mwifiex_pcie_create_txbd_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	/*
	 * driver maintaines the write pointer and firmware maintaines the read
	 * pointer. The write pointer starts at 0 (zero) while the read pointer
	 * starts at zero with rollover bit set
	 */
	card->txbd_wrptr = 0;

	if (reg->pfu_enabled)
		card->txbd_rdptr = 0;
	else
		card->txbd_rdptr |= reg->tx_rollover_ind;

	/* allocate shared memory for the BD ring and divide the same in to
	   several descriptors */
	if (reg->pfu_enabled)
		card->txbd_ring_size = sizeof(struct mwifiex_pfu_buf_desc) *
				       MWIFIEX_MAX_TXRX_BD;
	else
		card->txbd_ring_size = sizeof(struct mwifiex_pcie_buf_desc) *
				       MWIFIEX_MAX_TXRX_BD;

	dev_dbg(adapter->dev, "info: txbd_ring: Allocating %d bytes\n",
		card->txbd_ring_size);
	card->txbd_ring_vbase = pci_alloc_consistent(card->dev,
						     card->txbd_ring_size,
						     &card->txbd_ring_pbase);
	if (!card->txbd_ring_vbase) {
		dev_err(adapter->dev,
			"allocate consistent memory (%d bytes) failed!\n",
			card->txbd_ring_size);
		return -ENOMEM;
	}
	dev_dbg(adapter->dev,
		"info: txbd_ring - base: %p, pbase: %#x:%x, len: %x\n",
		card->txbd_ring_vbase, (unsigned int)card->txbd_ring_pbase,
		(u32)((u64)card->txbd_ring_pbase >> 32), card->txbd_ring_size);

	return mwifiex_init_txq_ring(adapter);
}

static int mwifiex_pcie_delete_txbd_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	mwifiex_cleanup_txq_ring(adapter);

	if (card->txbd_ring_vbase)
		pci_free_consistent(card->dev, card->txbd_ring_size,
				    card->txbd_ring_vbase,
				    card->txbd_ring_pbase);
	card->txbd_ring_size = 0;
	card->txbd_wrptr = 0;
	card->txbd_rdptr = 0 | reg->tx_rollover_ind;
	card->txbd_ring_vbase = NULL;
	card->txbd_ring_pbase = 0;

	return 0;
}

/*
 * This function creates buffer descriptor ring for RX
 */
static int mwifiex_pcie_create_rxbd_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	/*
	 * driver maintaines the read pointer and firmware maintaines the write
	 * pointer. The write pointer starts at 0 (zero) while the read pointer
	 * starts at zero with rollover bit set
	 */
	card->rxbd_wrptr = 0;
	card->rxbd_rdptr = reg->rx_rollover_ind;

	if (reg->pfu_enabled)
		card->rxbd_ring_size = sizeof(struct mwifiex_pfu_buf_desc) *
				       MWIFIEX_MAX_TXRX_BD;
	else
		card->rxbd_ring_size = sizeof(struct mwifiex_pcie_buf_desc) *
				       MWIFIEX_MAX_TXRX_BD;

	dev_dbg(adapter->dev, "info: rxbd_ring: Allocating %d bytes\n",
		card->rxbd_ring_size);
	card->rxbd_ring_vbase = pci_alloc_consistent(card->dev,
						     card->rxbd_ring_size,
						     &card->rxbd_ring_pbase);
	if (!card->rxbd_ring_vbase) {
		dev_err(adapter->dev,
			"allocate consistent memory (%d bytes) failed!\n",
			card->rxbd_ring_size);
		return -ENOMEM;
	}

	dev_dbg(adapter->dev,
		"info: rxbd_ring - base: %p, pbase: %#x:%x, len: %#x\n",
		card->rxbd_ring_vbase, (u32)card->rxbd_ring_pbase,
		(u32)((u64)card->rxbd_ring_pbase >> 32),
		card->rxbd_ring_size);

	return mwifiex_init_rxq_ring(adapter);
}

/*
 * This function deletes Buffer descriptor ring for RX
 */
static int mwifiex_pcie_delete_rxbd_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	mwifiex_cleanup_rxq_ring(adapter);

	if (card->rxbd_ring_vbase)
		pci_free_consistent(card->dev, card->rxbd_ring_size,
				    card->rxbd_ring_vbase,
				    card->rxbd_ring_pbase);
	card->rxbd_ring_size = 0;
	card->rxbd_wrptr = 0;
	card->rxbd_rdptr = 0 | reg->rx_rollover_ind;
	card->rxbd_ring_vbase = NULL;
	card->rxbd_ring_pbase = 0;

	return 0;
}

/*
 * This function creates buffer descriptor ring for Events
 */
static int mwifiex_pcie_create_evtbd_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	/*
	 * driver maintaines the read pointer and firmware maintaines the write
	 * pointer. The write pointer starts at 0 (zero) while the read pointer
	 * starts at zero with rollover bit set
	 */
	card->evtbd_wrptr = 0;
	card->evtbd_rdptr = reg->evt_rollover_ind;

	card->evtbd_ring_size = sizeof(struct mwifiex_evt_buf_desc) *
				MWIFIEX_MAX_EVT_BD;

	dev_dbg(adapter->dev, "info: evtbd_ring: Allocating %d bytes\n",
		card->evtbd_ring_size);
	card->evtbd_ring_vbase = pci_alloc_consistent(card->dev,
						      card->evtbd_ring_size,
						      &card->evtbd_ring_pbase);
	if (!card->evtbd_ring_vbase) {
		dev_err(adapter->dev,
			"allocate consistent memory (%d bytes) failed!\n",
			card->evtbd_ring_size);
		return -ENOMEM;
	}

	dev_dbg(adapter->dev,
		"info: CMDRSP/EVT bd_ring - base: %p pbase: %#x:%x len: %#x\n",
		card->evtbd_ring_vbase, (u32)card->evtbd_ring_pbase,
		(u32)((u64)card->evtbd_ring_pbase >> 32),
		card->evtbd_ring_size);

	return mwifiex_pcie_init_evt_ring(adapter);
}

/*
 * This function deletes Buffer descriptor ring for Events
 */
static int mwifiex_pcie_delete_evtbd_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	mwifiex_cleanup_evt_ring(adapter);

	if (card->evtbd_ring_vbase)
		pci_free_consistent(card->dev, card->evtbd_ring_size,
				    card->evtbd_ring_vbase,
				    card->evtbd_ring_pbase);
	card->evtbd_wrptr = 0;
	card->evtbd_rdptr = 0 | reg->evt_rollover_ind;
	card->evtbd_ring_size = 0;
	card->evtbd_ring_vbase = NULL;
	card->evtbd_ring_pbase = 0;

	return 0;
}

/*
 * This function allocates a buffer for CMDRSP
 */
static int mwifiex_pcie_alloc_cmdrsp_buf(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	struct sk_buff *skb;

	/* Allocate memory for receiving command response data */
	skb = dev_alloc_skb(MWIFIEX_UPLD_SIZE);
	if (!skb) {
		dev_err(adapter->dev,
			"Unable to allocate skb for command response data.\n");
		return -ENOMEM;
	}
	skb_put(skb, MWIFIEX_UPLD_SIZE);
	if (mwifiex_map_pci_memory(adapter, skb, MWIFIEX_UPLD_SIZE,
				   PCI_DMA_FROMDEVICE))
		return -1;

	card->cmdrsp_buf = skb;

	return 0;
}

/*
 * This function deletes a buffer for CMDRSP
 */
static int mwifiex_pcie_delete_cmdrsp_buf(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card;

	if (!adapter)
		return 0;

	card = adapter->card;

	if (card && card->cmdrsp_buf) {
		mwifiex_unmap_pci_memory(adapter, card->cmdrsp_buf,
					 PCI_DMA_FROMDEVICE);
		dev_kfree_skb_any(card->cmdrsp_buf);
	}

	if (card && card->cmd_buf) {
		mwifiex_unmap_pci_memory(adapter, card->cmd_buf,
					 PCI_DMA_TODEVICE);
	}
	return 0;
}

/*
 * This function allocates a buffer for sleep cookie
 */
static int mwifiex_pcie_alloc_sleep_cookie_buf(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;

	card->sleep_cookie_vbase = pci_alloc_consistent(card->dev, sizeof(u32),
						     &card->sleep_cookie_pbase);
	if (!card->sleep_cookie_vbase) {
		dev_err(adapter->dev, "pci_alloc_consistent failed!\n");
		return -ENOMEM;
	}
	/* Init val of Sleep Cookie */
	*(u32 *)card->sleep_cookie_vbase = FW_AWAKE_COOKIE;

	dev_dbg(adapter->dev, "alloc_scook: sleep cookie=0x%x\n",
		*((u32 *)card->sleep_cookie_vbase));

	return 0;
}

/*
 * This function deletes buffer for sleep cookie
 */
static int mwifiex_pcie_delete_sleep_cookie_buf(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card;

	if (!adapter)
		return 0;

	card = adapter->card;

	if (card && card->sleep_cookie_vbase) {
		pci_free_consistent(card->dev, sizeof(u32),
				    card->sleep_cookie_vbase,
				    card->sleep_cookie_pbase);
		card->sleep_cookie_vbase = NULL;
	}

	return 0;
}

/* This function flushes the TX buffer descriptor ring
 * This function defined as handler is also called while cleaning TXRX
 * during disconnect/ bss stop.
 */
static int mwifiex_clean_pcie_ring_buf(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;

	if (!mwifiex_pcie_txbd_empty(card, card->txbd_rdptr)) {
		card->txbd_flush = 1;
		/* write pointer already set at last send
		 * send dnld-rdy intr again, wait for completion.
		 */
		if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
				      CPU_INTR_DNLD_RDY)) {
			dev_err(adapter->dev,
				"failed to assert dnld-rdy interrupt.\n");
			return -1;
		}
	}
	return 0;
}

/*
 * This function unmaps and frees downloaded data buffer
 */
static int mwifiex_pcie_send_data_complete(struct mwifiex_adapter *adapter)
{
	struct sk_buff *skb;
	u32 wrdoneidx, rdptr, num_tx_buffs, unmap_count = 0;
	struct mwifiex_pcie_buf_desc *desc;
	struct mwifiex_pfu_buf_desc *desc2;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	if (!mwifiex_pcie_ok_to_access_hw(adapter))
		mwifiex_pm_wakeup_card(adapter);

	/* Read the TX ring read pointer set by firmware */
	if (mwifiex_read_reg(adapter, reg->tx_rdptr, &rdptr)) {
		dev_err(adapter->dev,
			"SEND COMP: failed to read reg->tx_rdptr\n");
		return -1;
	}

	dev_dbg(adapter->dev, "SEND COMP: rdptr_prev=0x%x, rdptr=0x%x\n",
		card->txbd_rdptr, rdptr);

	num_tx_buffs = MWIFIEX_MAX_TXRX_BD << reg->tx_start_ptr;
	/* free from previous txbd_rdptr to current txbd_rdptr */
	while (((card->txbd_rdptr & reg->tx_mask) !=
		(rdptr & reg->tx_mask)) ||
	       ((card->txbd_rdptr & reg->tx_rollover_ind) !=
		(rdptr & reg->tx_rollover_ind))) {
		wrdoneidx = (card->txbd_rdptr & reg->tx_mask) >>
			    reg->tx_start_ptr;

		skb = card->tx_buf_list[wrdoneidx];

		if (skb) {
			dev_dbg(adapter->dev,
				"SEND COMP: Detach skb %p at txbd_rdidx=%d\n",
				skb, wrdoneidx);
			mwifiex_unmap_pci_memory(adapter, skb,
						 PCI_DMA_TODEVICE);

			unmap_count++;

			if (card->txbd_flush)
				mwifiex_write_data_complete(adapter, skb, 0,
							    -1);
			else
				mwifiex_write_data_complete(adapter, skb, 0, 0);
		}

		card->tx_buf_list[wrdoneidx] = NULL;

		if (reg->pfu_enabled) {
			desc2 = card->txbd_ring[wrdoneidx];
			memset(desc2, 0, sizeof(*desc2));
		} else {
			desc = card->txbd_ring[wrdoneidx];
			memset(desc, 0, sizeof(*desc));
		}
		switch (card->dev->device) {
		case PCIE_DEVICE_ID_MARVELL_88W8766P:
			card->txbd_rdptr++;
			break;
		case PCIE_DEVICE_ID_MARVELL_88W8897:
			card->txbd_rdptr += reg->ring_tx_start_ptr;
			break;
		}


		if ((card->txbd_rdptr & reg->tx_mask) == num_tx_buffs)
			card->txbd_rdptr = ((card->txbd_rdptr &
					     reg->tx_rollover_ind) ^
					     reg->tx_rollover_ind);
	}

	if (unmap_count)
		adapter->data_sent = false;

	if (card->txbd_flush) {
		if (mwifiex_pcie_txbd_empty(card, card->txbd_rdptr))
			card->txbd_flush = 0;
		else
			mwifiex_clean_pcie_ring_buf(adapter);
	}

	return 0;
}

/* This function sends data buffer to device. First 4 bytes of payload
 * are filled with payload length and payload type. Then this payload
 * is mapped to PCI device memory. Tx ring pointers are advanced accordingly.
 * Download ready interrupt to FW is deffered if Tx ring is not full and
 * additional payload can be accomodated.
 * Caller must ensure tx_param parameter to this function is not NULL.
 */
static int
mwifiex_pcie_send_data(struct mwifiex_adapter *adapter, struct sk_buff *skb,
		       struct mwifiex_tx_param *tx_param)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	u32 wrindx, num_tx_buffs, rx_val;
	int ret;
	dma_addr_t buf_pa;
	struct mwifiex_pcie_buf_desc *desc = NULL;
	struct mwifiex_pfu_buf_desc *desc2 = NULL;
	__le16 *tmp;

	if (!(skb->data && skb->len)) {
		dev_err(adapter->dev, "%s(): invalid parameter <%p, %#x>\n",
			__func__, skb->data, skb->len);
		return -1;
	}

	if (!mwifiex_pcie_ok_to_access_hw(adapter))
		mwifiex_pm_wakeup_card(adapter);

	num_tx_buffs = MWIFIEX_MAX_TXRX_BD << reg->tx_start_ptr;
	dev_dbg(adapter->dev, "info: SEND DATA: <Rd: %#x, Wr: %#x>\n",
		card->txbd_rdptr, card->txbd_wrptr);
	if (mwifiex_pcie_txbd_not_full(card)) {
		u8 *payload;

		adapter->data_sent = true;
		payload = skb->data;
		tmp = (__le16 *)&payload[0];
		*tmp = cpu_to_le16((u16)skb->len);
		tmp = (__le16 *)&payload[2];
		*tmp = cpu_to_le16(MWIFIEX_TYPE_DATA);

		if (mwifiex_map_pci_memory(adapter, skb, skb->len,
					   PCI_DMA_TODEVICE))
			return -1;

		wrindx = (card->txbd_wrptr & reg->tx_mask) >> reg->tx_start_ptr;
		buf_pa = MWIFIEX_SKB_DMA_ADDR(skb);
		card->tx_buf_list[wrindx] = skb;

		if (reg->pfu_enabled) {
			desc2 = card->txbd_ring[wrindx];
			desc2->paddr = buf_pa;
			desc2->len = (u16)skb->len;
			desc2->frag_len = (u16)skb->len;
			desc2->offset = 0;
			desc2->flags = MWIFIEX_BD_FLAG_FIRST_DESC |
					 MWIFIEX_BD_FLAG_LAST_DESC;
		} else {
			desc = card->txbd_ring[wrindx];
			desc->paddr = buf_pa;
			desc->len = (u16)skb->len;
			desc->flags = MWIFIEX_BD_FLAG_FIRST_DESC |
				      MWIFIEX_BD_FLAG_LAST_DESC;
		}

		switch (card->dev->device) {
		case PCIE_DEVICE_ID_MARVELL_88W8766P:
			card->txbd_wrptr++;
			break;
		case PCIE_DEVICE_ID_MARVELL_88W8897:
			card->txbd_wrptr += reg->ring_tx_start_ptr;
			break;
		}

		if ((card->txbd_wrptr & reg->tx_mask) == num_tx_buffs)
			card->txbd_wrptr = ((card->txbd_wrptr &
						reg->tx_rollover_ind) ^
						reg->tx_rollover_ind);

		rx_val = card->rxbd_rdptr & reg->rx_wrap_mask;
		/* Write the TX ring write pointer in to reg->tx_wrptr */
		if (mwifiex_write_reg(adapter, reg->tx_wrptr,
				      card->txbd_wrptr | rx_val)) {
			dev_err(adapter->dev,
				"SEND DATA: failed to write reg->tx_wrptr\n");
			ret = -1;
			goto done_unmap;
		}
		if ((mwifiex_pcie_txbd_not_full(card)) &&
		    tx_param->next_pkt_len) {
			/* have more packets and TxBD still can hold more */
			dev_dbg(adapter->dev,
				"SEND DATA: delay dnld-rdy interrupt.\n");
			adapter->data_sent = false;
		} else {
			/* Send the TX ready interrupt */
			if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
					      CPU_INTR_DNLD_RDY)) {
				dev_err(adapter->dev,
					"SEND DATA: failed to assert dnld-rdy interrupt.\n");
				ret = -1;
				goto done_unmap;
			}
		}
		dev_dbg(adapter->dev, "info: SEND DATA: Updated <Rd: %#x, Wr: "
			"%#x> and sent packet to firmware successfully\n",
			card->txbd_rdptr, card->txbd_wrptr);
	} else {
		dev_dbg(adapter->dev,
			"info: TX Ring full, can't send packets to fw\n");
		adapter->data_sent = true;
		/* Send the TX ready interrupt */
		if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
				      CPU_INTR_DNLD_RDY))
			dev_err(adapter->dev,
				"SEND DATA: failed to assert door-bell intr\n");
		return -EBUSY;
	}

	return -EINPROGRESS;
done_unmap:
	mwifiex_unmap_pci_memory(adapter, skb, PCI_DMA_TODEVICE);
	card->tx_buf_list[wrindx] = NULL;
	if (reg->pfu_enabled)
		memset(desc2, 0, sizeof(*desc2));
	else
		memset(desc, 0, sizeof(*desc));

	return ret;
}

/*
 * This function handles received buffer ring and
 * dispatches packets to upper
 */
static int mwifiex_pcie_process_recv_data(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	u32 wrptr, rd_index, tx_val;
	dma_addr_t buf_pa;
	int ret = 0;
	struct sk_buff *skb_tmp = NULL;
	struct mwifiex_pcie_buf_desc *desc;
	struct mwifiex_pfu_buf_desc *desc2;
	unsigned long flags;

	if (!mwifiex_pcie_ok_to_access_hw(adapter))
		mwifiex_pm_wakeup_card(adapter);

	/* Read the RX ring Write pointer set by firmware */
	if (mwifiex_read_reg(adapter, reg->rx_wrptr, &wrptr)) {
		dev_err(adapter->dev,
			"RECV DATA: failed to read reg->rx_wrptr\n");
		ret = -1;
		goto done;
	}
	card->rxbd_wrptr = wrptr;

	while (((wrptr & reg->rx_mask) !=
		(card->rxbd_rdptr & reg->rx_mask)) ||
	       ((wrptr & reg->rx_rollover_ind) ==
		(card->rxbd_rdptr & reg->rx_rollover_ind))) {
		struct sk_buff *skb_data;
		u16 rx_len;
		__le16 pkt_len;

		rd_index = card->rxbd_rdptr & reg->rx_mask;
		skb_data = card->rx_buf_list[rd_index];

		/* If skb allocation was failed earlier for Rx packet,
		 * rx_buf_list[rd_index] would have been left with a NULL.
		 */
		if (!skb_data)
			return -ENOMEM;

		mwifiex_unmap_pci_memory(adapter, skb_data, PCI_DMA_FROMDEVICE);
		card->rx_buf_list[rd_index] = NULL;

		/* Get data length from interface header -
		 * first 2 bytes for len, next 2 bytes is for type
		 */
		pkt_len = *((__le16 *)skb_data->data);
		rx_len = le16_to_cpu(pkt_len);
		if (WARN_ON(rx_len <= INTF_HEADER_LEN ||
			    rx_len > MWIFIEX_RX_DATA_BUF_SIZE)) {
			dev_err(adapter->dev,
				"Invalid RX len %d, Rd=%#x, Wr=%#x\n",
				rx_len, card->rxbd_rdptr, wrptr);
			dev_kfree_skb_any(skb_data);
		} else {
			skb_put(skb_data, rx_len);
			dev_dbg(adapter->dev,
				"info: RECV DATA: Rd=%#x, Wr=%#x, Len=%d\n",
				card->rxbd_rdptr, wrptr, rx_len);
			skb_pull(skb_data, INTF_HEADER_LEN);
			if (adapter->rx_work_enabled) {
				spin_lock_irqsave(&adapter->rx_q_lock, flags);
				skb_queue_tail(&adapter->rx_data_q, skb_data);
				spin_unlock_irqrestore(&adapter->rx_q_lock,
						       flags);
				adapter->data_received = true;
				atomic_inc(&adapter->rx_pending);
			} else {
				mwifiex_handle_rx_packet(adapter, skb_data);
			}
		}

		skb_tmp = dev_alloc_skb(MWIFIEX_RX_DATA_BUF_SIZE);
		if (!skb_tmp) {
			dev_err(adapter->dev,
				"Unable to allocate skb.\n");
			return -ENOMEM;
		}

		if (mwifiex_map_pci_memory(adapter, skb_tmp,
					   MWIFIEX_RX_DATA_BUF_SIZE,
					   PCI_DMA_FROMDEVICE))
			return -1;

		buf_pa = MWIFIEX_SKB_DMA_ADDR(skb_tmp);

		dev_dbg(adapter->dev,
			"RECV DATA: Attach new sk_buff %p at rxbd_rdidx=%d\n",
			skb_tmp, rd_index);
		card->rx_buf_list[rd_index] = skb_tmp;

		if (reg->pfu_enabled) {
			desc2 = card->rxbd_ring[rd_index];
			desc2->paddr = buf_pa;
			desc2->len = skb_tmp->len;
			desc2->frag_len = skb_tmp->len;
			desc2->offset = 0;
			desc2->flags = reg->ring_flag_sop | reg->ring_flag_eop;
		} else {
			desc = card->rxbd_ring[rd_index];
			desc->paddr = buf_pa;
			desc->len = skb_tmp->len;
			desc->flags = 0;
		}

		if ((++card->rxbd_rdptr & reg->rx_mask) ==
							MWIFIEX_MAX_TXRX_BD) {
			card->rxbd_rdptr = ((card->rxbd_rdptr &
					     reg->rx_rollover_ind) ^
					     reg->rx_rollover_ind);
		}
		dev_dbg(adapter->dev, "info: RECV DATA: <Rd: %#x, Wr: %#x>\n",
			card->rxbd_rdptr, wrptr);

		tx_val = card->txbd_wrptr & reg->tx_wrap_mask;
		/* Write the RX ring read pointer in to reg->rx_rdptr */
		if (mwifiex_write_reg(adapter, reg->rx_rdptr,
				      card->rxbd_rdptr | tx_val)) {
			dev_err(adapter->dev,
				"RECV DATA: failed to write reg->rx_rdptr\n");
			ret = -1;
			goto done;
		}

		/* Read the RX ring Write pointer set by firmware */
		if (mwifiex_read_reg(adapter, reg->rx_wrptr, &wrptr)) {
			dev_err(adapter->dev,
				"RECV DATA: failed to read reg->rx_wrptr\n");
			ret = -1;
			goto done;
		}
		dev_dbg(adapter->dev,
			"info: RECV DATA: Rcvd packet from fw successfully\n");
		card->rxbd_wrptr = wrptr;
	}

done:
	return ret;
}

/*
 * This function downloads the boot command to device
 */
static int
mwifiex_pcie_send_boot_cmd(struct mwifiex_adapter *adapter, struct sk_buff *skb)
{
	dma_addr_t buf_pa;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	if (!(skb->data && skb->len)) {
		dev_err(adapter->dev,
			"Invalid parameter in %s <%p. len %d>\n",
			__func__, skb->data, skb->len);
		return -1;
	}

	if (mwifiex_map_pci_memory(adapter, skb, skb->len , PCI_DMA_TODEVICE))
		return -1;

	buf_pa = MWIFIEX_SKB_DMA_ADDR(skb);

	/* Write the lower 32bits of the physical address to low command
	 * address scratch register
	 */
	if (mwifiex_write_reg(adapter, reg->cmd_addr_lo, (u32)buf_pa)) {
		dev_err(adapter->dev,
			"%s: failed to write download command to boot code.\n",
			__func__);
		mwifiex_unmap_pci_memory(adapter, skb, PCI_DMA_TODEVICE);
		return -1;
	}

	/* Write the upper 32bits of the physical address to high command
	 * address scratch register
	 */
	if (mwifiex_write_reg(adapter, reg->cmd_addr_hi,
			      (u32)((u64)buf_pa >> 32))) {
		dev_err(adapter->dev,
			"%s: failed to write download command to boot code.\n",
			__func__);
		mwifiex_unmap_pci_memory(adapter, skb, PCI_DMA_TODEVICE);
		return -1;
	}

	/* Write the command length to cmd_size scratch register */
	if (mwifiex_write_reg(adapter, reg->cmd_size, skb->len)) {
		dev_err(adapter->dev,
			"%s: failed to write command len to cmd_size scratch reg\n",
			__func__);
		mwifiex_unmap_pci_memory(adapter, skb, PCI_DMA_TODEVICE);
		return -1;
	}

	/* Ring the door bell */
	if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
			      CPU_INTR_DOOR_BELL)) {
		dev_err(adapter->dev,
			"%s: failed to assert door-bell intr\n", __func__);
		mwifiex_unmap_pci_memory(adapter, skb, PCI_DMA_TODEVICE);
		return -1;
	}

	return 0;
}

/* This function init rx port in firmware which in turn enables to receive data
 * from device before transmitting any packet.
 */
static int mwifiex_pcie_init_fw_port(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	int tx_wrap = card->txbd_wrptr & reg->tx_wrap_mask;

	/* Write the RX ring read pointer in to reg->rx_rdptr */
	if (mwifiex_write_reg(adapter, reg->rx_rdptr, card->rxbd_rdptr |
			      tx_wrap)) {
		dev_err(adapter->dev,
			"RECV DATA: failed to write reg->rx_rdptr\n");
		return -1;
	}
	return 0;
}

/* This function downloads commands to the device
 */
static int
mwifiex_pcie_send_cmd(struct mwifiex_adapter *adapter, struct sk_buff *skb)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	int ret = 0;
	dma_addr_t cmd_buf_pa, cmdrsp_buf_pa;
	u8 *payload = (u8 *)skb->data;

	if (!(skb->data && skb->len)) {
		dev_err(adapter->dev, "Invalid parameter in %s <%p, %#x>\n",
			__func__, skb->data, skb->len);
		return -1;
	}

	/* Make sure a command response buffer is available */
	if (!card->cmdrsp_buf) {
		dev_err(adapter->dev,
			"No response buffer available, send command failed\n");
		return -EBUSY;
	}

	if (!mwifiex_pcie_ok_to_access_hw(adapter))
		mwifiex_pm_wakeup_card(adapter);

	adapter->cmd_sent = true;

	*(__le16 *)&payload[0] = cpu_to_le16((u16)skb->len);
	*(__le16 *)&payload[2] = cpu_to_le16(MWIFIEX_TYPE_CMD);

	if (mwifiex_map_pci_memory(adapter, skb, skb->len, PCI_DMA_TODEVICE))
		return -1;

	card->cmd_buf = skb;

	/* To send a command, the driver will:
		1. Write the 64bit physical address of the data buffer to
		   cmd response address low  + cmd response address high
		2. Ring the door bell (i.e. set the door bell interrupt)

		In response to door bell interrupt, the firmware will perform
		the DMA of the command packet (first header to obtain the total
		length and then rest of the command).
	*/

	if (card->cmdrsp_buf) {
		cmdrsp_buf_pa = MWIFIEX_SKB_DMA_ADDR(card->cmdrsp_buf);
		/* Write the lower 32bits of the cmdrsp buffer physical
		   address */
		if (mwifiex_write_reg(adapter, reg->cmdrsp_addr_lo,
				      (u32)cmdrsp_buf_pa)) {
			dev_err(adapter->dev,
				"Failed to write download cmd to boot code.\n");
			ret = -1;
			goto done;
		}
		/* Write the upper 32bits of the cmdrsp buffer physical
		   address */
		if (mwifiex_write_reg(adapter, reg->cmdrsp_addr_hi,
				      (u32)((u64)cmdrsp_buf_pa >> 32))) {
			dev_err(adapter->dev,
				"Failed to write download cmd to boot code.\n");
			ret = -1;
			goto done;
		}
	}

	cmd_buf_pa = MWIFIEX_SKB_DMA_ADDR(card->cmd_buf);
	/* Write the lower 32bits of the physical address to reg->cmd_addr_lo */
	if (mwifiex_write_reg(adapter, reg->cmd_addr_lo,
			      (u32)cmd_buf_pa)) {
		dev_err(adapter->dev,
			"Failed to write download cmd to boot code.\n");
		ret = -1;
		goto done;
	}
	/* Write the upper 32bits of the physical address to reg->cmd_addr_hi */
	if (mwifiex_write_reg(adapter, reg->cmd_addr_hi,
			      (u32)((u64)cmd_buf_pa >> 32))) {
		dev_err(adapter->dev,
			"Failed to write download cmd to boot code.\n");
		ret = -1;
		goto done;
	}

	/* Write the command length to reg->cmd_size */
	if (mwifiex_write_reg(adapter, reg->cmd_size,
			      card->cmd_buf->len)) {
		dev_err(adapter->dev,
			"Failed to write cmd len to reg->cmd_size\n");
		ret = -1;
		goto done;
	}

	/* Ring the door bell */
	if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
			      CPU_INTR_DOOR_BELL)) {
		dev_err(adapter->dev,
			"Failed to assert door-bell intr\n");
		ret = -1;
		goto done;
	}

done:
	if (ret)
		adapter->cmd_sent = false;

	return 0;
}

/*
 * This function handles command complete interrupt
 */
static int mwifiex_pcie_process_cmd_complete(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	struct sk_buff *skb = card->cmdrsp_buf;
	int count = 0;
	u16 rx_len;
	__le16 pkt_len;

	dev_dbg(adapter->dev, "info: Rx CMD Response\n");

	mwifiex_unmap_pci_memory(adapter, skb, PCI_DMA_FROMDEVICE);

	/* Unmap the command as a response has been received. */
	if (card->cmd_buf) {
		mwifiex_unmap_pci_memory(adapter, card->cmd_buf,
					 PCI_DMA_TODEVICE);
		card->cmd_buf = NULL;
	}

	pkt_len = *((__le16 *)skb->data);
	rx_len = le16_to_cpu(pkt_len);
	skb_trim(skb, rx_len);
	skb_pull(skb, INTF_HEADER_LEN);

	if (!adapter->curr_cmd) {
		if (adapter->ps_state == PS_STATE_SLEEP_CFM) {
			mwifiex_process_sleep_confirm_resp(adapter, skb->data,
							   skb->len);
			mwifiex_pcie_enable_host_int(adapter);
			if (mwifiex_write_reg(adapter,
					      PCIE_CPU_INT_EVENT,
					      CPU_INTR_SLEEP_CFM_DONE)) {
				dev_warn(adapter->dev,
					 "Write register failed\n");
				return -1;
			}
			mwifiex_delay_for_sleep_cookie(adapter,
						       MWIFIEX_MAX_DELAY_COUNT);
			while (reg->sleep_cookie && (count++ < 10) &&
			       mwifiex_pcie_ok_to_access_hw(adapter))
				usleep_range(50, 60);
		} else {
			dev_err(adapter->dev,
				"There is no command but got cmdrsp\n");
		}
		memcpy(adapter->upld_buf, skb->data,
		       min_t(u32, MWIFIEX_SIZE_OF_CMD_BUFFER, skb->len));
		skb_push(skb, INTF_HEADER_LEN);
		if (mwifiex_map_pci_memory(adapter, skb, MWIFIEX_UPLD_SIZE,
					   PCI_DMA_FROMDEVICE))
			return -1;
	} else if (mwifiex_pcie_ok_to_access_hw(adapter)) {
		adapter->curr_cmd->resp_skb = skb;
		adapter->cmd_resp_received = true;
		/* Take the pointer and set it to CMD node and will
		   return in the response complete callback */
		card->cmdrsp_buf = NULL;

		/* Clear the cmd-rsp buffer address in scratch registers. This
		   will prevent firmware from writing to the same response
		   buffer again. */
		if (mwifiex_write_reg(adapter, reg->cmdrsp_addr_lo, 0)) {
			dev_err(adapter->dev,
				"cmd_done: failed to clear cmd_rsp_addr_lo\n");
			return -1;
		}
		/* Write the upper 32bits of the cmdrsp buffer physical
		   address */
		if (mwifiex_write_reg(adapter, reg->cmdrsp_addr_hi, 0)) {
			dev_err(adapter->dev,
				"cmd_done: failed to clear cmd_rsp_addr_hi\n");
			return -1;
		}
	}

	return 0;
}

/*
 * Command Response processing complete handler
 */
static int mwifiex_pcie_cmdrsp_complete(struct mwifiex_adapter *adapter,
					struct sk_buff *skb)
{
	struct pcie_service_card *card = adapter->card;

	if (skb) {
		card->cmdrsp_buf = skb;
		skb_push(card->cmdrsp_buf, INTF_HEADER_LEN);
		if (mwifiex_map_pci_memory(adapter, skb, MWIFIEX_UPLD_SIZE,
					   PCI_DMA_FROMDEVICE))
			return -1;
	}

	return 0;
}

/*
 * This function handles firmware event ready interrupt
 */
static int mwifiex_pcie_process_event_ready(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	u32 rdptr = card->evtbd_rdptr & MWIFIEX_EVTBD_MASK;
	u32 wrptr, event;
	struct mwifiex_evt_buf_desc *desc;

	if (!mwifiex_pcie_ok_to_access_hw(adapter))
		mwifiex_pm_wakeup_card(adapter);

	if (adapter->event_received) {
		dev_dbg(adapter->dev, "info: Event being processed, "
			"do not process this interrupt just yet\n");
		return 0;
	}

	if (rdptr >= MWIFIEX_MAX_EVT_BD) {
		dev_dbg(adapter->dev, "info: Invalid read pointer...\n");
		return -1;
	}

	/* Read the event ring write pointer set by firmware */
	if (mwifiex_read_reg(adapter, reg->evt_wrptr, &wrptr)) {
		dev_err(adapter->dev,
			"EventReady: failed to read reg->evt_wrptr\n");
		return -1;
	}

	dev_dbg(adapter->dev, "info: EventReady: Initial <Rd: 0x%x, Wr: 0x%x>",
		card->evtbd_rdptr, wrptr);
	if (((wrptr & MWIFIEX_EVTBD_MASK) != (card->evtbd_rdptr
					      & MWIFIEX_EVTBD_MASK)) ||
	    ((wrptr & reg->evt_rollover_ind) ==
	     (card->evtbd_rdptr & reg->evt_rollover_ind))) {
		struct sk_buff *skb_cmd;
		__le16 data_len = 0;
		u16 evt_len;

		dev_dbg(adapter->dev, "info: Read Index: %d\n", rdptr);
		skb_cmd = card->evt_buf_list[rdptr];
		mwifiex_unmap_pci_memory(adapter, skb_cmd, PCI_DMA_FROMDEVICE);

		/* Take the pointer and set it to event pointer in adapter
		   and will return back after event handling callback */
		card->evt_buf_list[rdptr] = NULL;
		desc = card->evtbd_ring[rdptr];
		memset(desc, 0, sizeof(*desc));

		event = *(u32 *) &skb_cmd->data[INTF_HEADER_LEN];
		adapter->event_cause = event;
		/* The first 4bytes will be the event transfer header
		   len is 2 bytes followed by type which is 2 bytes */
		memcpy(&data_len, skb_cmd->data, sizeof(__le16));
		evt_len = le16_to_cpu(data_len);

		skb_pull(skb_cmd, INTF_HEADER_LEN);
		dev_dbg(adapter->dev, "info: Event length: %d\n", evt_len);

		if ((evt_len > 0) && (evt_len  < MAX_EVENT_SIZE))
			memcpy(adapter->event_body, skb_cmd->data +
			       MWIFIEX_EVENT_HEADER_LEN, evt_len -
			       MWIFIEX_EVENT_HEADER_LEN);

		adapter->event_received = true;
		adapter->event_skb = skb_cmd;

		/* Do not update the event read pointer here, wait till the
		   buffer is released. This is just to make things simpler,
		   we need to find a better method of managing these buffers.
		*/
	} else {
		if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
				      CPU_INTR_EVENT_DONE)) {
			dev_warn(adapter->dev,
				 "Write register failed\n");
			return -1;
		}
	}

	return 0;
}

/*
 * Event processing complete handler
 */
static int mwifiex_pcie_event_complete(struct mwifiex_adapter *adapter,
				       struct sk_buff *skb)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	int ret = 0;
	u32 rdptr = card->evtbd_rdptr & MWIFIEX_EVTBD_MASK;
	u32 wrptr;
	struct mwifiex_evt_buf_desc *desc;

	if (!skb)
		return 0;

	if (rdptr >= MWIFIEX_MAX_EVT_BD) {
		dev_err(adapter->dev, "event_complete: Invalid rdptr 0x%x\n",
			rdptr);
		return -EINVAL;
	}

	/* Read the event ring write pointer set by firmware */
	if (mwifiex_read_reg(adapter, reg->evt_wrptr, &wrptr)) {
		dev_err(adapter->dev,
			"event_complete: failed to read reg->evt_wrptr\n");
		return -1;
	}

	if (!card->evt_buf_list[rdptr]) {
		skb_push(skb, INTF_HEADER_LEN);
		if (mwifiex_map_pci_memory(adapter, skb,
					   MAX_EVENT_SIZE,
					   PCI_DMA_FROMDEVICE))
			return -1;
		card->evt_buf_list[rdptr] = skb;
		desc = card->evtbd_ring[rdptr];
		desc->paddr = MWIFIEX_SKB_DMA_ADDR(skb);
		desc->len = (u16)skb->len;
		desc->flags = 0;
		skb = NULL;
	} else {
		dev_dbg(adapter->dev,
			"info: ERROR: buf still valid at index %d, <%p, %p>\n",
			rdptr, card->evt_buf_list[rdptr], skb);
	}

	if ((++card->evtbd_rdptr & MWIFIEX_EVTBD_MASK) == MWIFIEX_MAX_EVT_BD) {
		card->evtbd_rdptr = ((card->evtbd_rdptr &
					reg->evt_rollover_ind) ^
					reg->evt_rollover_ind);
	}

	dev_dbg(adapter->dev, "info: Updated <Rd: 0x%x, Wr: 0x%x>",
		card->evtbd_rdptr, wrptr);

	/* Write the event ring read pointer in to reg->evt_rdptr */
	if (mwifiex_write_reg(adapter, reg->evt_rdptr,
			      card->evtbd_rdptr)) {
		dev_err(adapter->dev,
			"event_complete: failed to read reg->evt_rdptr\n");
		return -1;
	}

	dev_dbg(adapter->dev, "info: Check Events Again\n");
	ret = mwifiex_pcie_process_event_ready(adapter);

	return ret;
}

/*
 * This function downloads the firmware to the card.
 *
 * Firmware is downloaded to the card in blocks. Every block download
 * is tested for CRC errors, and retried a number of times before
 * returning failure.
 */
static int mwifiex_prog_fw_w_helper(struct mwifiex_adapter *adapter,
				    struct mwifiex_fw_image *fw)
{
	int ret;
	u8 *firmware = fw->fw_buf;
	u32 firmware_len = fw->fw_len;
	u32 offset = 0;
	struct sk_buff *skb;
	u32 txlen, tx_blocks = 0, tries, len;
	u32 block_retry_cnt = 0;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	if (!firmware || !firmware_len) {
		dev_err(adapter->dev,
			"No firmware image found! Terminating download\n");
		return -1;
	}

	dev_dbg(adapter->dev, "info: Downloading FW image (%d bytes)\n",
		firmware_len);

	if (mwifiex_pcie_disable_host_int(adapter)) {
		dev_err(adapter->dev,
			"%s: Disabling interrupts failed.\n", __func__);
		return -1;
	}

	skb = dev_alloc_skb(MWIFIEX_UPLD_SIZE);
	if (!skb) {
		ret = -ENOMEM;
		goto done;
	}

	/* Perform firmware data transfer */
	do {
		u32 ireg_intr = 0;

		/* More data? */
		if (offset >= firmware_len)
			break;

		for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
			ret = mwifiex_read_reg(adapter, reg->cmd_size,
					       &len);
			if (ret) {
				dev_warn(adapter->dev,
					 "Failed reading len from boot code\n");
				goto done;
			}
			if (len)
				break;
			usleep_range(10, 20);
		}

		if (!len) {
			break;
		} else if (len > MWIFIEX_UPLD_SIZE) {
			pr_err("FW download failure @ %d, invalid length %d\n",
			       offset, len);
			ret = -1;
			goto done;
		}

		txlen = len;

		if (len & BIT(0)) {
			block_retry_cnt++;
			if (block_retry_cnt > MAX_WRITE_IOMEM_RETRY) {
				pr_err("FW download failure @ %d, over max "
				       "retry count\n", offset);
				ret = -1;
				goto done;
			}
			dev_err(adapter->dev, "FW CRC error indicated by the "
				"helper: len = 0x%04X, txlen = %d\n",
				len, txlen);
			len &= ~BIT(0);
			/* Setting this to 0 to resend from same offset */
			txlen = 0;
		} else {
			block_retry_cnt = 0;
			/* Set blocksize to transfer - checking for
			   last block */
			if (firmware_len - offset < txlen)
				txlen = firmware_len - offset;

			dev_dbg(adapter->dev, ".");

			tx_blocks = (txlen + card->pcie.blksz_fw_dl - 1) /
				    card->pcie.blksz_fw_dl;

			/* Copy payload to buffer */
			memmove(skb->data, &firmware[offset], txlen);
		}

		skb_put(skb, MWIFIEX_UPLD_SIZE - skb->len);
		skb_trim(skb, tx_blocks * card->pcie.blksz_fw_dl);

		/* Send the boot command to device */
		if (mwifiex_pcie_send_boot_cmd(adapter, skb)) {
			dev_err(adapter->dev,
				"Failed to send firmware download command\n");
			ret = -1;
			goto done;
		}

		/* Wait for the command done interrupt */
		do {
			if (mwifiex_read_reg(adapter, PCIE_CPU_INT_STATUS,
					     &ireg_intr)) {
				dev_err(adapter->dev, "%s: Failed to read "
					"interrupt status during fw dnld.\n",
					__func__);
				mwifiex_unmap_pci_memory(adapter, skb,
							 PCI_DMA_TODEVICE);
				ret = -1;
				goto done;
			}
		} while ((ireg_intr & CPU_INTR_DOOR_BELL) ==
			 CPU_INTR_DOOR_BELL);

		mwifiex_unmap_pci_memory(adapter, skb, PCI_DMA_TODEVICE);

		offset += txlen;
	} while (true);

	dev_dbg(adapter->dev, "info:\nFW download over, size %d bytes\n",
		offset);

	ret = 0;

done:
	dev_kfree_skb_any(skb);
	return ret;
}

/*
 * This function checks the firmware status in card.
 *
 * The winner interface is also determined by this function.
 */
static int
mwifiex_check_fw_status(struct mwifiex_adapter *adapter, u32 poll_num)
{
	int ret = 0;
	u32 firmware_stat, winner_status;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	u32 tries;

	/* Mask spurios interrupts */
	if (mwifiex_write_reg(adapter, PCIE_HOST_INT_STATUS_MASK,
			      HOST_INTR_MASK)) {
		dev_warn(adapter->dev, "Write register failed\n");
		return -1;
	}

	dev_dbg(adapter->dev, "Setting driver ready signature\n");
	if (mwifiex_write_reg(adapter, reg->drv_rdy,
			      FIRMWARE_READY_PCIE)) {
		dev_err(adapter->dev,
			"Failed to write driver ready signature\n");
		return -1;
	}

	/* Wait for firmware initialization event */
	for (tries = 0; tries < poll_num; tries++) {
		if (mwifiex_read_reg(adapter, reg->fw_status,
				     &firmware_stat))
			ret = -1;
		else
			ret = 0;
		if (ret)
			continue;
		if (firmware_stat == FIRMWARE_READY_PCIE) {
			ret = 0;
			break;
		} else {
			msleep(100);
			ret = -1;
		}
	}

	if (ret) {
		if (mwifiex_read_reg(adapter, reg->fw_status,
				     &winner_status))
			ret = -1;
		else if (!winner_status) {
			dev_err(adapter->dev, "PCI-E is the winner\n");
			adapter->winner = 1;
		} else {
			dev_err(adapter->dev,
				"PCI-E is not the winner <%#x,%d>, exit dnld\n",
				ret, adapter->winner);
		}
	}

	return ret;
}

/*
 * This function reads the interrupt status from card.
 */
static void mwifiex_interrupt_status(struct mwifiex_adapter *adapter)
{
	u32 pcie_ireg;
	unsigned long flags;

	if (!mwifiex_pcie_ok_to_access_hw(adapter))
		return;

	if (mwifiex_read_reg(adapter, PCIE_HOST_INT_STATUS, &pcie_ireg)) {
		dev_warn(adapter->dev, "Read register failed\n");
		return;
	}

	if ((pcie_ireg != 0xFFFFFFFF) && (pcie_ireg)) {

		mwifiex_pcie_disable_host_int(adapter);

		/* Clear the pending interrupts */
		if (mwifiex_write_reg(adapter, PCIE_HOST_INT_STATUS,
				      ~pcie_ireg)) {
			dev_warn(adapter->dev, "Write register failed\n");
			return;
		}
		spin_lock_irqsave(&adapter->int_lock, flags);
		adapter->int_status |= pcie_ireg;
		spin_unlock_irqrestore(&adapter->int_lock, flags);

		if (!adapter->pps_uapsd_mode &&
		    adapter->ps_state == PS_STATE_SLEEP &&
		    mwifiex_pcie_ok_to_access_hw(adapter)) {
				/* Potentially for PCIe we could get other
				 * interrupts like shared. Don't change power
				 * state until cookie is set */
				adapter->ps_state = PS_STATE_AWAKE;
				adapter->pm_wakeup_fw_try = false;
		}
	}
}

/*
 * Interrupt handler for PCIe root port
 *
 * This function reads the interrupt status from firmware and assigns
 * the main process in workqueue which will handle the interrupt.
 */
static irqreturn_t mwifiex_pcie_interrupt(int irq, void *context)
{
	struct pci_dev *pdev = (struct pci_dev *)context;
	struct pcie_service_card *card;
	struct mwifiex_adapter *adapter;

	if (!pdev) {
		pr_debug("info: %s: pdev is NULL\n", (u8 *)pdev);
		goto exit;
	}

	card = pci_get_drvdata(pdev);
	if (!card || !card->adapter) {
		pr_debug("info: %s: card=%p adapter=%p\n", __func__, card,
			 card ? card->adapter : NULL);
		goto exit;
	}
	adapter = card->adapter;

	if (adapter->surprise_removed)
		goto exit;

	mwifiex_interrupt_status(adapter);
	queue_work(adapter->workqueue, &adapter->main_work);

exit:
	return IRQ_HANDLED;
}

/*
 * This function checks the current interrupt status.
 *
 * The following interrupts are checked and handled by this function -
 *      - Data sent
 *      - Command sent
 *      - Command received
 *      - Packets received
 *      - Events received
 *
 * In case of Rx packets received, the packets are uploaded from card to
 * host and processed accordingly.
 */
static int mwifiex_process_int_status(struct mwifiex_adapter *adapter)
{
	int ret;
	u32 pcie_ireg;
	unsigned long flags;

	spin_lock_irqsave(&adapter->int_lock, flags);
	/* Clear out unused interrupts */
	pcie_ireg = adapter->int_status;
	adapter->int_status = 0;
	spin_unlock_irqrestore(&adapter->int_lock, flags);

	while (pcie_ireg & HOST_INTR_MASK) {
		if (pcie_ireg & HOST_INTR_DNLD_DONE) {
			pcie_ireg &= ~HOST_INTR_DNLD_DONE;
			dev_dbg(adapter->dev, "info: TX DNLD Done\n");
			ret = mwifiex_pcie_send_data_complete(adapter);
			if (ret)
				return ret;
		}
		if (pcie_ireg & HOST_INTR_UPLD_RDY) {
			pcie_ireg &= ~HOST_INTR_UPLD_RDY;
			dev_dbg(adapter->dev, "info: Rx DATA\n");
			ret = mwifiex_pcie_process_recv_data(adapter);
			if (ret)
				return ret;
		}
		if (pcie_ireg & HOST_INTR_EVENT_RDY) {
			pcie_ireg &= ~HOST_INTR_EVENT_RDY;
			dev_dbg(adapter->dev, "info: Rx EVENT\n");
			ret = mwifiex_pcie_process_event_ready(adapter);
			if (ret)
				return ret;
		}

		if (pcie_ireg & HOST_INTR_CMD_DONE) {
			pcie_ireg &= ~HOST_INTR_CMD_DONE;
			if (adapter->cmd_sent) {
				dev_dbg(adapter->dev,
					"info: CMD sent Interrupt\n");
				adapter->cmd_sent = false;
			}
			/* Handle command response */
			ret = mwifiex_pcie_process_cmd_complete(adapter);
			if (ret)
				return ret;
		}

		if (mwifiex_pcie_ok_to_access_hw(adapter)) {
			if (mwifiex_read_reg(adapter, PCIE_HOST_INT_STATUS,
					     &pcie_ireg)) {
				dev_warn(adapter->dev,
					 "Read register failed\n");
				return -1;
			}

			if ((pcie_ireg != 0xFFFFFFFF) && (pcie_ireg)) {
				if (mwifiex_write_reg(adapter,
						      PCIE_HOST_INT_STATUS,
						      ~pcie_ireg)) {
					dev_warn(adapter->dev,
						 "Write register failed\n");
					return -1;
				}
			}

		}
	}
	dev_dbg(adapter->dev, "info: cmd_sent=%d data_sent=%d\n",
		adapter->cmd_sent, adapter->data_sent);
	if (adapter->ps_state != PS_STATE_SLEEP)
		mwifiex_pcie_enable_host_int(adapter);

	return 0;
}

/*
 * This function downloads data from driver to card.
 *
 * Both commands and data packets are transferred to the card by this
 * function.
 *
 * This function adds the PCIE specific header to the front of the buffer
 * before transferring. The header contains the length of the packet and
 * the type. The firmware handles the packets based upon this set type.
 */
static int mwifiex_pcie_host_to_card(struct mwifiex_adapter *adapter, u8 type,
				     struct sk_buff *skb,
				     struct mwifiex_tx_param *tx_param)
{
	if (!skb) {
		dev_err(adapter->dev, "Passed NULL skb to %s\n", __func__);
		return -1;
	}

	if (type == MWIFIEX_TYPE_DATA)
		return mwifiex_pcie_send_data(adapter, skb, tx_param);
	else if (type == MWIFIEX_TYPE_CMD)
		return mwifiex_pcie_send_cmd(adapter, skb);

	return 0;
}

/* This function read/write firmware */
static enum rdwr_status
mwifiex_pcie_rdwr_firmware(struct mwifiex_adapter *adapter, u8 doneflag)
{
	int ret, tries;
	u8 ctrl_data;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	ret = mwifiex_write_reg(adapter, reg->fw_dump_ctrl, FW_DUMP_HOST_READY);
	if (ret) {
		dev_err(adapter->dev, "PCIE write err\n");
		return RDWR_STATUS_FAILURE;
	}

	for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
		mwifiex_read_reg_byte(adapter, reg->fw_dump_ctrl, &ctrl_data);
		if (ctrl_data == FW_DUMP_DONE)
			return RDWR_STATUS_SUCCESS;
		if (doneflag && ctrl_data == doneflag)
			return RDWR_STATUS_DONE;
		if (ctrl_data != FW_DUMP_HOST_READY) {
			dev_info(adapter->dev,
				 "The ctrl reg was changed, re-try again!\n");
			ret = mwifiex_write_reg(adapter, reg->fw_dump_ctrl,
						FW_DUMP_HOST_READY);
			if (ret) {
				dev_err(adapter->dev, "PCIE write err\n");
				return RDWR_STATUS_FAILURE;
			}
		}
		usleep_range(100, 200);
	}

	dev_err(adapter->dev, "Fail to pull ctrl_data\n");
	return RDWR_STATUS_FAILURE;
}

/* This function dump firmware memory to file */
static void mwifiex_pcie_fw_dump_work(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *creg = card->pcie.reg;
	unsigned int reg, reg_start, reg_end;
	u8 *dbg_ptr, *end_ptr, dump_num, idx, i, read_reg, doneflag = 0;
	enum rdwr_status stat;
	u32 memory_size;
	int ret;
	static char *env[] = { "DRIVER=mwifiex_pcie", "EVENT=fw_dump", NULL };

	if (!card->pcie.supports_fw_dump)
		return;

	for (idx = 0; idx < ARRAY_SIZE(mem_type_mapping_tbl); idx++) {
		struct memory_type_mapping *entry = &mem_type_mapping_tbl[idx];

		if (entry->mem_ptr) {
			vfree(entry->mem_ptr);
			entry->mem_ptr = NULL;
		}
		entry->mem_size = 0;
	}

	dev_info(adapter->dev, "== mwifiex firmware dump start ==\n");

	/* Read the number of the memories which will dump */
	stat = mwifiex_pcie_rdwr_firmware(adapter, doneflag);
	if (stat == RDWR_STATUS_FAILURE)
		goto done;

	reg = creg->fw_dump_start;
	mwifiex_read_reg_byte(adapter, reg, &dump_num);

	/* Read the length of every memory which will dump */
	for (idx = 0; idx < dump_num; idx++) {
		struct memory_type_mapping *entry = &mem_type_mapping_tbl[idx];

		stat = mwifiex_pcie_rdwr_firmware(adapter, doneflag);
		if (stat == RDWR_STATUS_FAILURE)
			goto done;

		memory_size = 0;
		reg = creg->fw_dump_start;
		for (i = 0; i < 4; i++) {
			mwifiex_read_reg_byte(adapter, reg, &read_reg);
			memory_size |= (read_reg << (i * 8));
			reg++;
		}

		if (memory_size == 0) {
			dev_info(adapter->dev, "Firmware dump Finished!\n");
			break;
		}

		dev_info(adapter->dev,
			 "%s_SIZE=0x%x\n", entry->mem_name, memory_size);
		entry->mem_ptr = vmalloc(memory_size + 1);
		entry->mem_size = memory_size;
		if (!entry->mem_ptr) {
			dev_err(adapter->dev,
				"Vmalloc %s failed\n", entry->mem_name);
			goto done;
		}
		dbg_ptr = entry->mem_ptr;
		end_ptr = dbg_ptr + memory_size;

		doneflag = entry->done_flag;
		dev_info(adapter->dev, "Start %s output, please wait...\n",
			 entry->mem_name);

		do {
			stat = mwifiex_pcie_rdwr_firmware(adapter, doneflag);
			if (RDWR_STATUS_FAILURE == stat)
				goto done;

			reg_start = creg->fw_dump_start;
			reg_end = creg->fw_dump_end;
			for (reg = reg_start; reg <= reg_end; reg++) {
				mwifiex_read_reg_byte(adapter, reg, dbg_ptr);
				if (dbg_ptr < end_ptr) {
					dbg_ptr++;
				} else {
					dev_err(adapter->dev,
						"Allocated buf not enough\n");
					goto done;
				}
			}

			if (stat != RDWR_STATUS_DONE)
				continue;

			dev_info(adapter->dev, "%s done: size=0x%tx\n",
				 entry->mem_name, dbg_ptr - entry->mem_ptr);
			break;
		} while (true);
	}
	dev_info(adapter->dev, "== mwifiex firmware dump end ==\n");

	kobject_uevent_env(&adapter->wiphy->dev.kobj, KOBJ_CHANGE, env);

done:
	adapter->curr_mem_idx = 0;
}

static void mwifiex_pcie_work(struct work_struct *work)
{
	struct mwifiex_adapter *adapter =
			container_of(work, struct mwifiex_adapter, iface_work);

	if (test_and_clear_bit(MWIFIEX_IFACE_WORK_FW_DUMP,
			       &adapter->iface_work_flags))
		mwifiex_pcie_fw_dump_work(adapter);
}

/* This function dumps FW information */
static void mwifiex_pcie_fw_dump(struct mwifiex_adapter *adapter)
{
	if (test_bit(MWIFIEX_IFACE_WORK_FW_DUMP, &adapter->iface_work_flags))
		return;

	set_bit(MWIFIEX_IFACE_WORK_FW_DUMP, &adapter->iface_work_flags);

	schedule_work(&adapter->iface_work);
}

/*
 * This function initializes the PCI-E host memory space, WCB rings, etc.
 *
 * The following initializations steps are followed -
 *      - Allocate TXBD ring buffers
 *      - Allocate RXBD ring buffers
 *      - Allocate event BD ring buffers
 *      - Allocate command response ring buffer
 *      - Allocate sleep cookie buffer
 */
static int mwifiex_pcie_init(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	int ret;
	struct pci_dev *pdev = card->dev;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	pci_set_drvdata(pdev, card);

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_enable_dev;

	pci_set_master(pdev);

	dev_dbg(adapter->dev, "try set_consistent_dma_mask(32)\n");
	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(adapter->dev, "set_dma_mask(32) failed\n");
		goto err_set_dma_mask;
	}

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(adapter->dev, "set_consistent_dma_mask(64) failed\n");
		goto err_set_dma_mask;
	}

	ret = pci_request_region(pdev, 0, DRV_NAME);
	if (ret) {
		dev_err(adapter->dev, "req_reg(0) error\n");
		goto err_req_region0;
	}
	card->pci_mmap = pci_iomap(pdev, 0, 0);
	if (!card->pci_mmap) {
		dev_err(adapter->dev, "iomap(0) error\n");
		ret = -EIO;
		goto err_iomap0;
	}
	ret = pci_request_region(pdev, 2, DRV_NAME);
	if (ret) {
		dev_err(adapter->dev, "req_reg(2) error\n");
		goto err_req_region2;
	}
	card->pci_mmap1 = pci_iomap(pdev, 2, 0);
	if (!card->pci_mmap1) {
		dev_err(adapter->dev, "iomap(2) error\n");
		ret = -EIO;
		goto err_iomap2;
	}

	dev_dbg(adapter->dev,
		"PCI memory map Virt0: %p PCI memory map Virt2: %p\n",
		card->pci_mmap, card->pci_mmap1);

	card->cmdrsp_buf = NULL;
	ret = mwifiex_pcie_create_txbd_ring(adapter);
	if (ret)
		goto err_cre_txbd;
	ret = mwifiex_pcie_create_rxbd_ring(adapter);
	if (ret)
		goto err_cre_rxbd;
	ret = mwifiex_pcie_create_evtbd_ring(adapter);
	if (ret)
		goto err_cre_evtbd;
	ret = mwifiex_pcie_alloc_cmdrsp_buf(adapter);
	if (ret)
		goto err_alloc_cmdbuf;
	if (reg->sleep_cookie) {
		ret = mwifiex_pcie_alloc_sleep_cookie_buf(adapter);
		if (ret)
			goto err_alloc_cookie;
	} else {
		card->sleep_cookie_vbase = NULL;
	}
	return ret;

err_alloc_cookie:
	mwifiex_pcie_delete_cmdrsp_buf(adapter);
err_alloc_cmdbuf:
	mwifiex_pcie_delete_evtbd_ring(adapter);
err_cre_evtbd:
	mwifiex_pcie_delete_rxbd_ring(adapter);
err_cre_rxbd:
	mwifiex_pcie_delete_txbd_ring(adapter);
err_cre_txbd:
	pci_iounmap(pdev, card->pci_mmap1);
err_iomap2:
	pci_release_region(pdev, 2);
err_req_region2:
	pci_iounmap(pdev, card->pci_mmap);
err_iomap0:
	pci_release_region(pdev, 0);
err_req_region0:
err_set_dma_mask:
	pci_disable_device(pdev);
err_enable_dev:
	pci_set_drvdata(pdev, NULL);
	return ret;
}

/*
 * This function cleans up the allocated card buffers.
 *
 * The following are freed by this function -
 *      - TXBD ring buffers
 *      - RXBD ring buffers
 *      - Event BD ring buffers
 *      - Command response ring buffer
 *      - Sleep cookie buffer
 */
static void mwifiex_pcie_cleanup(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	struct pci_dev *pdev = card->dev;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	if (user_rmmod) {
		dev_dbg(adapter->dev, "Clearing driver ready signature\n");
		if (mwifiex_write_reg(adapter, reg->drv_rdy, 0x00000000))
			dev_err(adapter->dev,
				"Failed to write driver not-ready signature\n");
	}

	if (pdev) {
		pci_iounmap(pdev, card->pci_mmap);
		pci_iounmap(pdev, card->pci_mmap1);
		pci_disable_device(pdev);
		pci_release_region(pdev, 2);
		pci_release_region(pdev, 0);
		pci_set_drvdata(pdev, NULL);
	}
	kfree(card);
}

/*
 * This function registers the PCIE device.
 *
 * PCIE IRQ is claimed, block size is set and driver data is initialized.
 */
static int mwifiex_register_dev(struct mwifiex_adapter *adapter)
{
	int ret;
	struct pcie_service_card *card = adapter->card;
	struct pci_dev *pdev = card->dev;

	/* save adapter pointer in card */
	card->adapter = adapter;

	ret = request_irq(pdev->irq, mwifiex_pcie_interrupt, IRQF_SHARED,
			  "MRVL_PCIE", pdev);
	if (ret) {
		pr_err("request_irq failed: ret=%d\n", ret);
		adapter->card = NULL;
		return -1;
	}

	adapter->dev = &pdev->dev;
	adapter->tx_buf_size = card->pcie.tx_buf_size;
	adapter->mem_type_mapping_tbl = mem_type_mapping_tbl;
	adapter->num_mem_types = ARRAY_SIZE(mem_type_mapping_tbl);
	strcpy(adapter->fw_name, card->pcie.firmware);

	return 0;
}

/*
 * This function unregisters the PCIE device.
 *
 * The PCIE IRQ is released, the function is disabled and driver
 * data is set to null.
 */
static void mwifiex_unregister_dev(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg;

	if (card) {
		dev_dbg(adapter->dev, "%s(): calling free_irq()\n", __func__);
		free_irq(card->dev->irq, card->dev);

		reg = card->pcie.reg;
		if (reg->sleep_cookie)
			mwifiex_pcie_delete_sleep_cookie_buf(adapter);

		mwifiex_pcie_delete_cmdrsp_buf(adapter);
		mwifiex_pcie_delete_evtbd_ring(adapter);
		mwifiex_pcie_delete_rxbd_ring(adapter);
		mwifiex_pcie_delete_txbd_ring(adapter);
		card->cmdrsp_buf = NULL;
	}
}

static struct mwifiex_if_ops pcie_ops = {
	.init_if =			mwifiex_pcie_init,
	.cleanup_if =			mwifiex_pcie_cleanup,
	.check_fw_status =		mwifiex_check_fw_status,
	.prog_fw =			mwifiex_prog_fw_w_helper,
	.register_dev =			mwifiex_register_dev,
	.unregister_dev =		mwifiex_unregister_dev,
	.enable_int =			mwifiex_pcie_enable_host_int,
	.process_int_status =		mwifiex_process_int_status,
	.host_to_card =			mwifiex_pcie_host_to_card,
	.wakeup =			mwifiex_pm_wakeup_card,
	.wakeup_complete =		mwifiex_pm_wakeup_card_complete,

	/* PCIE specific */
	.cmdrsp_complete =		mwifiex_pcie_cmdrsp_complete,
	.event_complete =		mwifiex_pcie_event_complete,
	.update_mp_end_port =		NULL,
	.cleanup_mpa_buf =		NULL,
	.init_fw_port =			mwifiex_pcie_init_fw_port,
	.clean_pcie_ring =		mwifiex_clean_pcie_ring_buf,
	.fw_dump =			mwifiex_pcie_fw_dump,
	.iface_work =			mwifiex_pcie_work,
};

/*
 * This function initializes the PCIE driver module.
 *
 * This initiates the semaphore and registers the device with
 * PCIE bus.
 */
static int mwifiex_pcie_init_module(void)
{
	int ret;

	pr_debug("Marvell PCIe Driver\n");

	sema_init(&add_remove_card_sem, 1);

	/* Clear the flag in case user removes the card. */
	user_rmmod = 0;

	ret = pci_register_driver(&mwifiex_pcie);
	if (ret)
		pr_err("Driver register failed!\n");
	else
		pr_debug("info: Driver registered successfully!\n");

	return ret;
}

/*
 * This function cleans up the PCIE driver.
 *
 * The following major steps are followed for cleanup -
 *      - Resume the device if its suspended
 *      - Disconnect the device if connected
 *      - Shutdown the firmware
 *      - Unregister the device from PCIE bus.
 */
static void mwifiex_pcie_cleanup_module(void)
{
	if (!down_interruptible(&add_remove_card_sem))
		up(&add_remove_card_sem);

	/* Set the flag as user is removing this module. */
	user_rmmod = 1;

	pci_unregister_driver(&mwifiex_pcie);
}

module_init(mwifiex_pcie_init_module);
module_exit(mwifiex_pcie_cleanup_module);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell WiFi-Ex PCI-Express Driver version " PCIE_VERSION);
MODULE_VERSION(PCIE_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_FIRMWARE(PCIE8766_DEFAULT_FW_NAME);
MODULE_FIRMWARE(PCIE8897_DEFAULT_FW_NAME);
