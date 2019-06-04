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

static struct mwifiex_if_ops pcie_ops;

static const struct of_device_id mwifiex_pcie_of_match_table[] = {
	{ .compatible = "pci11ab,2b42" },
	{ .compatible = "pci1b4b,2b42" },
	{ }
};

static int mwifiex_pcie_probe_of(struct device *dev)
{
	if (!of_match_node(mwifiex_pcie_of_match_table, dev->of_node)) {
		dev_err(dev, "required compatible string missing\n");
		return -EINVAL;
	}

	return 0;
}

static void mwifiex_pcie_work(struct work_struct *work);

static int
mwifiex_map_pci_memory(struct mwifiex_adapter *adapter, struct sk_buff *skb,
		       size_t size, int flags)
{
	struct pcie_service_card *card = adapter->card;
	struct mwifiex_dma_mapping mapping;

	mapping.addr = pci_map_single(card->dev, skb->data, size, flags);
	if (pci_dma_mapping_error(card->dev, mapping.addr)) {
		mwifiex_dbg(adapter, ERROR, "failed to map pci memory!\n");
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
 * This function writes data into PCIE card register.
 */
static int mwifiex_write_reg(struct mwifiex_adapter *adapter, int reg, u32 data)
{
	struct pcie_service_card *card = adapter->card;

	iowrite32(data, card->pci_mmap1 + reg);

	return 0;
}

/* This function reads data from PCIE card register.
 */
static int mwifiex_read_reg(struct mwifiex_adapter *adapter, int reg, u32 *data)
{
	struct pcie_service_card *card = adapter->card;

	*data = ioread32(card->pci_mmap1 + reg);
	if (*data == 0xffffffff)
		return 0xffffffff;

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
 * This function reads sleep cookie and checks if FW is ready
 */
static bool mwifiex_pcie_ok_to_access_hw(struct mwifiex_adapter *adapter)
{
	u32 cookie_value;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	if (!reg->sleep_cookie)
		return true;

	if (card->sleep_cookie_vbase) {
		cookie_value = get_unaligned_le32(card->sleep_cookie_vbase);
		mwifiex_dbg(adapter, INFO,
			    "info: ACCESS_HW: sleep cookie=0x%x\n",
			    cookie_value);
		if (cookie_value == FW_AWAKE_COOKIE)
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
	struct pci_dev *pdev = to_pci_dev(dev);

	card = pci_get_drvdata(pdev);

	/* Might still be loading firmware */
	wait_for_completion(&card->fw_done);

	adapter = card->adapter;
	if (!adapter) {
		dev_err(dev, "adapter is not valid\n");
		return 0;
	}

	mwifiex_enable_wake(adapter);

	/* Enable the Host Sleep */
	if (!mwifiex_enable_hs(adapter)) {
		mwifiex_dbg(adapter, ERROR,
			    "cmd: failed to suspend\n");
		clear_bit(MWIFIEX_IS_HS_ENABLING, &adapter->work_flags);
		mwifiex_disable_wake(adapter);
		return -EFAULT;
	}

	flush_workqueue(adapter->workqueue);

	/* Indicate device suspended */
	set_bit(MWIFIEX_IS_SUSPENDED, &adapter->work_flags);
	clear_bit(MWIFIEX_IS_HS_ENABLING, &adapter->work_flags);

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

	card = pci_get_drvdata(pdev);

	if (!card->adapter) {
		dev_err(dev, "adapter structure is not valid\n");
		return 0;
	}

	adapter = card->adapter;

	if (!test_bit(MWIFIEX_IS_SUSPENDED, &adapter->work_flags)) {
		mwifiex_dbg(adapter, WARN,
			    "Device already resumed\n");
		return 0;
	}

	clear_bit(MWIFIEX_IS_SUSPENDED, &adapter->work_flags);

	mwifiex_cancel_hs(mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_STA),
			  MWIFIEX_ASYNC_CMD);
	mwifiex_disable_wake(adapter);

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
	int ret;

	pr_debug("info: vendor=0x%4.04X device=0x%4.04X rev=%d\n",
		 pdev->vendor, pdev->device, pdev->revision);

	card = devm_kzalloc(&pdev->dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	init_completion(&card->fw_done);

	card->dev = pdev;

	if (ent->driver_data) {
		struct mwifiex_pcie_device *data = (void *)ent->driver_data;
		card->pcie.reg = data->reg;
		card->pcie.blksz_fw_dl = data->blksz_fw_dl;
		card->pcie.tx_buf_size = data->tx_buf_size;
		card->pcie.can_dump_fw = data->can_dump_fw;
		card->pcie.mem_type_mapping_tbl = data->mem_type_mapping_tbl;
		card->pcie.num_mem_types = data->num_mem_types;
		card->pcie.can_ext_scan = data->can_ext_scan;
		INIT_WORK(&card->work, mwifiex_pcie_work);
	}

	/* device tree node parsing and platform specific configuration*/
	if (pdev->dev.of_node) {
		ret = mwifiex_pcie_probe_of(&pdev->dev);
		if (ret)
			return ret;
	}

	if (mwifiex_add_card(card, &card->fw_done, &pcie_ops,
			     MWIFIEX_PCIE, &pdev->dev)) {
		pr_err("%s failed\n", __func__);
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
	const struct mwifiex_pcie_card_reg *reg;
	u32 fw_status;
	int ret;

	card = pci_get_drvdata(pdev);

	wait_for_completion(&card->fw_done);

	adapter = card->adapter;
	if (!adapter || !adapter->priv_num)
		return;

	reg = card->pcie.reg;
	if (reg)
		ret = mwifiex_read_reg(adapter, reg->fw_status, &fw_status);
	else
		fw_status = -1;

	if (fw_status == FIRMWARE_READY_PCIE && !adapter->mfg_mode) {
		mwifiex_deauthenticate_all(adapter);

		priv = mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);

		mwifiex_disable_auto_ds(priv);

		mwifiex_init_shutdown_fw(priv, MWIFIEX_FUNC_SHUTDOWN);
	}

	mwifiex_remove_card(adapter);
}

static void mwifiex_pcie_shutdown(struct pci_dev *pdev)
{
	mwifiex_pcie_remove(pdev);

	return;
}

static void mwifiex_pcie_coredump(struct device *dev)
{
	struct pci_dev *pdev;
	struct pcie_service_card *card;

	pdev = container_of(dev, struct pci_dev, dev);
	card = pci_get_drvdata(pdev);

	if (!test_and_set_bit(MWIFIEX_IFACE_WORK_DEVICE_DUMP,
			      &card->work_flags))
		schedule_work(&card->work);
}

static const struct pci_device_id mwifiex_ids[] = {
	{
		PCIE_VENDOR_ID_MARVELL, PCIE_DEVICE_ID_MARVELL_88W8766P,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		.driver_data = (unsigned long)&mwifiex_pcie8766,
	},
	{
		PCIE_VENDOR_ID_MARVELL, PCIE_DEVICE_ID_MARVELL_88W8897,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		.driver_data = (unsigned long)&mwifiex_pcie8897,
	},
	{
		PCIE_VENDOR_ID_MARVELL, PCIE_DEVICE_ID_MARVELL_88W8997,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		.driver_data = (unsigned long)&mwifiex_pcie8997,
	},
	{
		PCIE_VENDOR_ID_V2_MARVELL, PCIE_DEVICE_ID_MARVELL_88W8997,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		.driver_data = (unsigned long)&mwifiex_pcie8997,
	},
	{},
};

MODULE_DEVICE_TABLE(pci, mwifiex_ids);

/*
 * Cleanup all software without cleaning anything related to PCIe and HW.
 */
static void mwifiex_pcie_reset_prepare(struct pci_dev *pdev)
{
	struct pcie_service_card *card = pci_get_drvdata(pdev);
	struct mwifiex_adapter *adapter = card->adapter;

	if (!adapter) {
		dev_err(&pdev->dev, "%s: adapter structure is not valid\n",
			__func__);
		return;
	}

	mwifiex_dbg(adapter, INFO,
		    "%s: vendor=0x%4.04x device=0x%4.04x rev=%d Pre-FLR\n",
		    __func__, pdev->vendor, pdev->device, pdev->revision);

	mwifiex_shutdown_sw(adapter);
	clear_bit(MWIFIEX_IFACE_WORK_DEVICE_DUMP, &card->work_flags);
	clear_bit(MWIFIEX_IFACE_WORK_CARD_RESET, &card->work_flags);
	mwifiex_dbg(adapter, INFO, "%s, successful\n", __func__);
}

/*
 * Kernel stores and restores PCIe function context before and after performing
 * FLR respectively. Reconfigure the software and firmware including firmware
 * redownload.
 */
static void mwifiex_pcie_reset_done(struct pci_dev *pdev)
{
	struct pcie_service_card *card = pci_get_drvdata(pdev);
	struct mwifiex_adapter *adapter = card->adapter;
	int ret;

	if (!adapter) {
		dev_err(&pdev->dev, "%s: adapter structure is not valid\n",
			__func__);
		return;
	}

	mwifiex_dbg(adapter, INFO,
		    "%s: vendor=0x%4.04x device=0x%4.04x rev=%d Post-FLR\n",
		    __func__, pdev->vendor, pdev->device, pdev->revision);

	ret = mwifiex_reinit_sw(adapter);
	if (ret)
		dev_err(&pdev->dev, "reinit failed: %d\n", ret);
	else
		mwifiex_dbg(adapter, INFO, "%s, successful\n", __func__);
}

static const struct pci_error_handlers mwifiex_pcie_err_handler = {
	.reset_prepare		= mwifiex_pcie_reset_prepare,
	.reset_done		= mwifiex_pcie_reset_done,
};

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
	.driver   = {
		.coredump = mwifiex_pcie_coredump,
#ifdef CONFIG_PM_SLEEP
		.pm = &mwifiex_pcie_pm_ops,
#endif
	},
	.shutdown = mwifiex_pcie_shutdown,
	.err_handler = &mwifiex_pcie_err_handler,
};

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
	struct sk_buff *cmdrsp = card->cmdrsp_buf;

	for (count = 0; count < max_delay_loop_cnt; count++) {
		pci_dma_sync_single_for_cpu(card->dev,
					    MWIFIEX_SKB_DMA_ADDR(cmdrsp),
					    sizeof(sleep_cookie),
					    PCI_DMA_FROMDEVICE);
		buffer = cmdrsp->data;
		sleep_cookie = get_unaligned_le32(buffer);

		if (sleep_cookie == MWIFIEX_DEF_SLEEP_COOKIE) {
			mwifiex_dbg(adapter, INFO,
				    "sleep cookie found at count %d\n", count);
			break;
		}
		pci_dma_sync_single_for_device(card->dev,
					       MWIFIEX_SKB_DMA_ADDR(cmdrsp),
					       sizeof(sleep_cookie),
					       PCI_DMA_FROMDEVICE);
		usleep_range(20, 30);
	}

	if (count >= max_delay_loop_cnt)
		mwifiex_dbg(adapter, INFO,
			    "max count reached while accessing sleep cookie\n");
}

/* This function wakes up the card by reading fw_status register. */
static int mwifiex_pm_wakeup_card(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	mwifiex_dbg(adapter, EVENT,
		    "event: Wakeup device...\n");

	if (reg->sleep_cookie)
		mwifiex_pcie_dev_wakeup_delay(adapter);

	/* Accessing fw_status register will wakeup device */
	if (mwifiex_write_reg(adapter, reg->fw_status, FIRMWARE_READY_PCIE)) {
		mwifiex_dbg(adapter, ERROR,
			    "Writing fw_status register failed\n");
		return -1;
	}

	if (reg->sleep_cookie) {
		mwifiex_pcie_dev_wakeup_delay(adapter);
		mwifiex_dbg(adapter, INFO,
			    "PCIE wakeup: Setting PS_STATE_AWAKE\n");
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
	mwifiex_dbg(adapter, CMD,
		    "cmd: Wakeup device completed\n");

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
			mwifiex_dbg(adapter, ERROR,
				    "Disable host interrupt failed\n");
			return -1;
		}
	}

	atomic_set(&adapter->tx_hw_pending, 0);
	return 0;
}

static void mwifiex_pcie_disable_host_int_noerr(struct mwifiex_adapter *adapter)
{
	WARN_ON(mwifiex_pcie_disable_host_int(adapter));
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
			mwifiex_dbg(adapter, ERROR,
				    "Enable host interrupt failed\n");
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
		skb = mwifiex_alloc_dma_align_buf(MWIFIEX_RX_DATA_BUF_SIZE,
						  GFP_KERNEL);
		if (!skb) {
			mwifiex_dbg(adapter, ERROR,
				    "Unable to allocate skb for RX ring.\n");
			kfree(card->rxbd_ring_vbase);
			return -ENOMEM;
		}

		if (mwifiex_map_pci_memory(adapter, skb,
					   MWIFIEX_RX_DATA_BUF_SIZE,
					   PCI_DMA_FROMDEVICE))
			return -1;

		buf_pa = MWIFIEX_SKB_DMA_ADDR(skb);

		mwifiex_dbg(adapter, INFO,
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
			mwifiex_dbg(adapter, ERROR,
				    "Unable to allocate skb for EVENT buf.\n");
			kfree(card->evtbd_ring_vbase);
			return -ENOMEM;
		}
		skb_put(skb, MAX_EVENT_SIZE);

		if (mwifiex_map_pci_memory(adapter, skb, MAX_EVENT_SIZE,
					   PCI_DMA_FROMDEVICE))
			return -1;

		buf_pa = MWIFIEX_SKB_DMA_ADDR(skb);

		mwifiex_dbg(adapter, EVENT,
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

	atomic_set(&adapter->tx_hw_pending, 0);
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

	mwifiex_dbg(adapter, INFO,
		    "info: txbd_ring: Allocating %d bytes\n",
		    card->txbd_ring_size);
	card->txbd_ring_vbase = pci_alloc_consistent(card->dev,
						     card->txbd_ring_size,
						     &card->txbd_ring_pbase);
	if (!card->txbd_ring_vbase) {
		mwifiex_dbg(adapter, ERROR,
			    "allocate consistent memory (%d bytes) failed!\n",
			    card->txbd_ring_size);
		return -ENOMEM;
	}
	mwifiex_dbg(adapter, DATA,
		    "info: txbd_ring - base: %p, pbase: %#x:%x, len: %x\n",
		    card->txbd_ring_vbase, (unsigned int)card->txbd_ring_pbase,
		    (u32)((u64)card->txbd_ring_pbase >> 32),
		    card->txbd_ring_size);

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

	mwifiex_dbg(adapter, INFO,
		    "info: rxbd_ring: Allocating %d bytes\n",
		    card->rxbd_ring_size);
	card->rxbd_ring_vbase = pci_alloc_consistent(card->dev,
						     card->rxbd_ring_size,
						     &card->rxbd_ring_pbase);
	if (!card->rxbd_ring_vbase) {
		mwifiex_dbg(adapter, ERROR,
			    "allocate consistent memory (%d bytes) failed!\n",
			    card->rxbd_ring_size);
		return -ENOMEM;
	}

	mwifiex_dbg(adapter, DATA,
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

	mwifiex_dbg(adapter, INFO,
		    "info: evtbd_ring: Allocating %d bytes\n",
		card->evtbd_ring_size);
	card->evtbd_ring_vbase = pci_alloc_consistent(card->dev,
						      card->evtbd_ring_size,
						      &card->evtbd_ring_pbase);
	if (!card->evtbd_ring_vbase) {
		mwifiex_dbg(adapter, ERROR,
			    "allocate consistent memory (%d bytes) failed!\n",
			    card->evtbd_ring_size);
		return -ENOMEM;
	}

	mwifiex_dbg(adapter, EVENT,
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
		mwifiex_dbg(adapter, ERROR,
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
		card->cmdrsp_buf = NULL;
	}

	if (card && card->cmd_buf) {
		mwifiex_unmap_pci_memory(adapter, card->cmd_buf,
					 PCI_DMA_TODEVICE);
		dev_kfree_skb_any(card->cmd_buf);
		card->cmd_buf = NULL;
	}
	return 0;
}

/*
 * This function allocates a buffer for sleep cookie
 */
static int mwifiex_pcie_alloc_sleep_cookie_buf(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	u32 tmp;

	card->sleep_cookie_vbase = pci_alloc_consistent(card->dev, sizeof(u32),
						     &card->sleep_cookie_pbase);
	if (!card->sleep_cookie_vbase) {
		mwifiex_dbg(adapter, ERROR,
			    "pci_alloc_consistent failed!\n");
		return -ENOMEM;
	}
	/* Init val of Sleep Cookie */
	tmp = FW_AWAKE_COOKIE;
	put_unaligned(tmp, card->sleep_cookie_vbase);

	mwifiex_dbg(adapter, INFO,
		    "alloc_scook: sleep cookie=0x%x\n",
		    get_unaligned(card->sleep_cookie_vbase));

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
			mwifiex_dbg(adapter, ERROR,
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
		mwifiex_dbg(adapter, ERROR,
			    "SEND COMP: failed to read reg->tx_rdptr\n");
		return -1;
	}

	mwifiex_dbg(adapter, DATA,
		    "SEND COMP: rdptr_prev=0x%x, rdptr=0x%x\n",
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
			mwifiex_dbg(adapter, DATA,
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
			atomic_dec(&adapter->tx_hw_pending);
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
		case PCIE_DEVICE_ID_MARVELL_88W8997:
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

	if (!(skb->data && skb->len)) {
		mwifiex_dbg(adapter, ERROR,
			    "%s(): invalid parameter <%p, %#x>\n",
			    __func__, skb->data, skb->len);
		return -1;
	}

	if (!mwifiex_pcie_ok_to_access_hw(adapter))
		mwifiex_pm_wakeup_card(adapter);

	num_tx_buffs = MWIFIEX_MAX_TXRX_BD << reg->tx_start_ptr;
	mwifiex_dbg(adapter, DATA,
		    "info: SEND DATA: <Rd: %#x, Wr: %#x>\n",
		card->txbd_rdptr, card->txbd_wrptr);
	if (mwifiex_pcie_txbd_not_full(card)) {
		u8 *payload;

		adapter->data_sent = true;
		payload = skb->data;
		put_unaligned_le16((u16)skb->len, payload + 0);
		put_unaligned_le16(MWIFIEX_TYPE_DATA, payload + 2);

		if (mwifiex_map_pci_memory(adapter, skb, skb->len,
					   PCI_DMA_TODEVICE))
			return -1;

		wrindx = (card->txbd_wrptr & reg->tx_mask) >> reg->tx_start_ptr;
		buf_pa = MWIFIEX_SKB_DMA_ADDR(skb);
		card->tx_buf_list[wrindx] = skb;
		atomic_inc(&adapter->tx_hw_pending);

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
		case PCIE_DEVICE_ID_MARVELL_88W8997:
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
			mwifiex_dbg(adapter, ERROR,
				    "SEND DATA: failed to write reg->tx_wrptr\n");
			ret = -1;
			goto done_unmap;
		}
		if ((mwifiex_pcie_txbd_not_full(card)) &&
		    tx_param->next_pkt_len) {
			/* have more packets and TxBD still can hold more */
			mwifiex_dbg(adapter, DATA,
				    "SEND DATA: delay dnld-rdy interrupt.\n");
			adapter->data_sent = false;
		} else {
			/* Send the TX ready interrupt */
			if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
					      CPU_INTR_DNLD_RDY)) {
				mwifiex_dbg(adapter, ERROR,
					    "SEND DATA: failed to assert dnld-rdy interrupt.\n");
				ret = -1;
				goto done_unmap;
			}
		}
		mwifiex_dbg(adapter, DATA,
			    "info: SEND DATA: Updated <Rd: %#x, Wr:\t"
			    "%#x> and sent packet to firmware successfully\n",
			    card->txbd_rdptr, card->txbd_wrptr);
	} else {
		mwifiex_dbg(adapter, DATA,
			    "info: TX Ring full, can't send packets to fw\n");
		adapter->data_sent = true;
		/* Send the TX ready interrupt */
		if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
				      CPU_INTR_DNLD_RDY))
			mwifiex_dbg(adapter, ERROR,
				    "SEND DATA: failed to assert door-bell intr\n");
		return -EBUSY;
	}

	return -EINPROGRESS;
done_unmap:
	mwifiex_unmap_pci_memory(adapter, skb, PCI_DMA_TODEVICE);
	card->tx_buf_list[wrindx] = NULL;
	atomic_dec(&adapter->tx_hw_pending);
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

	if (!mwifiex_pcie_ok_to_access_hw(adapter))
		mwifiex_pm_wakeup_card(adapter);

	/* Read the RX ring Write pointer set by firmware */
	if (mwifiex_read_reg(adapter, reg->rx_wrptr, &wrptr)) {
		mwifiex_dbg(adapter, ERROR,
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
		rx_len = get_unaligned_le16(skb_data->data);
		if (WARN_ON(rx_len <= adapter->intf_hdr_len ||
			    rx_len > MWIFIEX_RX_DATA_BUF_SIZE)) {
			mwifiex_dbg(adapter, ERROR,
				    "Invalid RX len %d, Rd=%#x, Wr=%#x\n",
				    rx_len, card->rxbd_rdptr, wrptr);
			dev_kfree_skb_any(skb_data);
		} else {
			skb_put(skb_data, rx_len);
			mwifiex_dbg(adapter, DATA,
				    "info: RECV DATA: Rd=%#x, Wr=%#x, Len=%d\n",
				    card->rxbd_rdptr, wrptr, rx_len);
			skb_pull(skb_data, adapter->intf_hdr_len);
			if (adapter->rx_work_enabled) {
				skb_queue_tail(&adapter->rx_data_q, skb_data);
				adapter->data_received = true;
				atomic_inc(&adapter->rx_pending);
			} else {
				mwifiex_handle_rx_packet(adapter, skb_data);
			}
		}

		skb_tmp = mwifiex_alloc_dma_align_buf(MWIFIEX_RX_DATA_BUF_SIZE,
						      GFP_KERNEL);
		if (!skb_tmp) {
			mwifiex_dbg(adapter, ERROR,
				    "Unable to allocate skb.\n");
			return -ENOMEM;
		}

		if (mwifiex_map_pci_memory(adapter, skb_tmp,
					   MWIFIEX_RX_DATA_BUF_SIZE,
					   PCI_DMA_FROMDEVICE))
			return -1;

		buf_pa = MWIFIEX_SKB_DMA_ADDR(skb_tmp);

		mwifiex_dbg(adapter, INFO,
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
		mwifiex_dbg(adapter, DATA,
			    "info: RECV DATA: <Rd: %#x, Wr: %#x>\n",
			    card->rxbd_rdptr, wrptr);

		tx_val = card->txbd_wrptr & reg->tx_wrap_mask;
		/* Write the RX ring read pointer in to reg->rx_rdptr */
		if (mwifiex_write_reg(adapter, reg->rx_rdptr,
				      card->rxbd_rdptr | tx_val)) {
			mwifiex_dbg(adapter, DATA,
				    "RECV DATA: failed to write reg->rx_rdptr\n");
			ret = -1;
			goto done;
		}

		/* Read the RX ring Write pointer set by firmware */
		if (mwifiex_read_reg(adapter, reg->rx_wrptr, &wrptr)) {
			mwifiex_dbg(adapter, ERROR,
				    "RECV DATA: failed to read reg->rx_wrptr\n");
			ret = -1;
			goto done;
		}
		mwifiex_dbg(adapter, DATA,
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
		mwifiex_dbg(adapter, ERROR,
			    "Invalid parameter in %s <%p. len %d>\n",
			    __func__, skb->data, skb->len);
		return -1;
	}

	if (mwifiex_map_pci_memory(adapter, skb, skb->len, PCI_DMA_TODEVICE))
		return -1;

	buf_pa = MWIFIEX_SKB_DMA_ADDR(skb);

	/* Write the lower 32bits of the physical address to low command
	 * address scratch register
	 */
	if (mwifiex_write_reg(adapter, reg->cmd_addr_lo, (u32)buf_pa)) {
		mwifiex_dbg(adapter, ERROR,
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
		mwifiex_dbg(adapter, ERROR,
			    "%s: failed to write download command to boot code.\n",
			    __func__);
		mwifiex_unmap_pci_memory(adapter, skb, PCI_DMA_TODEVICE);
		return -1;
	}

	/* Write the command length to cmd_size scratch register */
	if (mwifiex_write_reg(adapter, reg->cmd_size, skb->len)) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: failed to write command len to cmd_size scratch reg\n",
			    __func__);
		mwifiex_unmap_pci_memory(adapter, skb, PCI_DMA_TODEVICE);
		return -1;
	}

	/* Ring the door bell */
	if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
			      CPU_INTR_DOOR_BELL)) {
		mwifiex_dbg(adapter, ERROR,
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
		mwifiex_dbg(adapter, ERROR,
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
		mwifiex_dbg(adapter, ERROR,
			    "Invalid parameter in %s <%p, %#x>\n",
			    __func__, skb->data, skb->len);
		return -1;
	}

	/* Make sure a command response buffer is available */
	if (!card->cmdrsp_buf) {
		mwifiex_dbg(adapter, ERROR,
			    "No response buffer available, send command failed\n");
		return -EBUSY;
	}

	if (!mwifiex_pcie_ok_to_access_hw(adapter))
		mwifiex_pm_wakeup_card(adapter);

	adapter->cmd_sent = true;

	put_unaligned_le16((u16)skb->len, &payload[0]);
	put_unaligned_le16(MWIFIEX_TYPE_CMD, &payload[2]);

	if (mwifiex_map_pci_memory(adapter, skb, skb->len, PCI_DMA_TODEVICE))
		return -1;

	card->cmd_buf = skb;
	/*
	 * Need to keep a reference, since core driver might free up this
	 * buffer before we've unmapped it.
	 */
	skb_get(skb);

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
			mwifiex_dbg(adapter, ERROR,
				    "Failed to write download cmd to boot code.\n");
			ret = -1;
			goto done;
		}
		/* Write the upper 32bits of the cmdrsp buffer physical
		   address */
		if (mwifiex_write_reg(adapter, reg->cmdrsp_addr_hi,
				      (u32)((u64)cmdrsp_buf_pa >> 32))) {
			mwifiex_dbg(adapter, ERROR,
				    "Failed to write download cmd to boot code.\n");
			ret = -1;
			goto done;
		}
	}

	cmd_buf_pa = MWIFIEX_SKB_DMA_ADDR(card->cmd_buf);
	/* Write the lower 32bits of the physical address to reg->cmd_addr_lo */
	if (mwifiex_write_reg(adapter, reg->cmd_addr_lo,
			      (u32)cmd_buf_pa)) {
		mwifiex_dbg(adapter, ERROR,
			    "Failed to write download cmd to boot code.\n");
		ret = -1;
		goto done;
	}
	/* Write the upper 32bits of the physical address to reg->cmd_addr_hi */
	if (mwifiex_write_reg(adapter, reg->cmd_addr_hi,
			      (u32)((u64)cmd_buf_pa >> 32))) {
		mwifiex_dbg(adapter, ERROR,
			    "Failed to write download cmd to boot code.\n");
		ret = -1;
		goto done;
	}

	/* Write the command length to reg->cmd_size */
	if (mwifiex_write_reg(adapter, reg->cmd_size,
			      card->cmd_buf->len)) {
		mwifiex_dbg(adapter, ERROR,
			    "Failed to write cmd len to reg->cmd_size\n");
		ret = -1;
		goto done;
	}

	/* Ring the door bell */
	if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
			      CPU_INTR_DOOR_BELL)) {
		mwifiex_dbg(adapter, ERROR,
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

	mwifiex_dbg(adapter, CMD,
		    "info: Rx CMD Response\n");

	if (adapter->curr_cmd)
		mwifiex_unmap_pci_memory(adapter, skb, PCI_DMA_FROMDEVICE);
	else
		pci_dma_sync_single_for_cpu(card->dev,
					    MWIFIEX_SKB_DMA_ADDR(skb),
					    MWIFIEX_UPLD_SIZE,
					    PCI_DMA_FROMDEVICE);

	/* Unmap the command as a response has been received. */
	if (card->cmd_buf) {
		mwifiex_unmap_pci_memory(adapter, card->cmd_buf,
					 PCI_DMA_TODEVICE);
		dev_kfree_skb_any(card->cmd_buf);
		card->cmd_buf = NULL;
	}

	rx_len = get_unaligned_le16(skb->data);
	skb_put(skb, MWIFIEX_UPLD_SIZE - skb->len);
	skb_trim(skb, rx_len);

	if (!adapter->curr_cmd) {
		if (adapter->ps_state == PS_STATE_SLEEP_CFM) {
			pci_dma_sync_single_for_device(card->dev,
						MWIFIEX_SKB_DMA_ADDR(skb),
						MWIFIEX_SLEEP_COOKIE_SIZE,
						PCI_DMA_FROMDEVICE);
			if (mwifiex_write_reg(adapter,
					      PCIE_CPU_INT_EVENT,
					      CPU_INTR_SLEEP_CFM_DONE)) {
				mwifiex_dbg(adapter, ERROR,
					    "Write register failed\n");
				return -1;
			}
			mwifiex_delay_for_sleep_cookie(adapter,
						       MWIFIEX_MAX_DELAY_COUNT);
			mwifiex_unmap_pci_memory(adapter, skb,
						 PCI_DMA_FROMDEVICE);
			skb_pull(skb, adapter->intf_hdr_len);
			while (reg->sleep_cookie && (count++ < 10) &&
			       mwifiex_pcie_ok_to_access_hw(adapter))
				usleep_range(50, 60);
			mwifiex_pcie_enable_host_int(adapter);
			mwifiex_process_sleep_confirm_resp(adapter, skb->data,
							   skb->len);
		} else {
			mwifiex_dbg(adapter, ERROR,
				    "There is no command but got cmdrsp\n");
		}
		memcpy(adapter->upld_buf, skb->data,
		       min_t(u32, MWIFIEX_SIZE_OF_CMD_BUFFER, skb->len));
		skb_push(skb, adapter->intf_hdr_len);
		if (mwifiex_map_pci_memory(adapter, skb, MWIFIEX_UPLD_SIZE,
					   PCI_DMA_FROMDEVICE))
			return -1;
	} else if (mwifiex_pcie_ok_to_access_hw(adapter)) {
		skb_pull(skb, adapter->intf_hdr_len);
		adapter->curr_cmd->resp_skb = skb;
		adapter->cmd_resp_received = true;
		/* Take the pointer and set it to CMD node and will
		   return in the response complete callback */
		card->cmdrsp_buf = NULL;

		/* Clear the cmd-rsp buffer address in scratch registers. This
		   will prevent firmware from writing to the same response
		   buffer again. */
		if (mwifiex_write_reg(adapter, reg->cmdrsp_addr_lo, 0)) {
			mwifiex_dbg(adapter, ERROR,
				    "cmd_done: failed to clear cmd_rsp_addr_lo\n");
			return -1;
		}
		/* Write the upper 32bits of the cmdrsp buffer physical
		   address */
		if (mwifiex_write_reg(adapter, reg->cmdrsp_addr_hi, 0)) {
			mwifiex_dbg(adapter, ERROR,
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
		skb_push(card->cmdrsp_buf, adapter->intf_hdr_len);
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
		mwifiex_dbg(adapter, EVENT,
			    "info: Event being processed,\t"
			    "do not process this interrupt just yet\n");
		return 0;
	}

	if (rdptr >= MWIFIEX_MAX_EVT_BD) {
		mwifiex_dbg(adapter, ERROR,
			    "info: Invalid read pointer...\n");
		return -1;
	}

	/* Read the event ring write pointer set by firmware */
	if (mwifiex_read_reg(adapter, reg->evt_wrptr, &wrptr)) {
		mwifiex_dbg(adapter, ERROR,
			    "EventReady: failed to read reg->evt_wrptr\n");
		return -1;
	}

	mwifiex_dbg(adapter, EVENT,
		    "info: EventReady: Initial <Rd: 0x%x, Wr: 0x%x>",
		    card->evtbd_rdptr, wrptr);
	if (((wrptr & MWIFIEX_EVTBD_MASK) != (card->evtbd_rdptr
					      & MWIFIEX_EVTBD_MASK)) ||
	    ((wrptr & reg->evt_rollover_ind) ==
	     (card->evtbd_rdptr & reg->evt_rollover_ind))) {
		struct sk_buff *skb_cmd;
		__le16 data_len = 0;
		u16 evt_len;

		mwifiex_dbg(adapter, INFO,
			    "info: Read Index: %d\n", rdptr);
		skb_cmd = card->evt_buf_list[rdptr];
		mwifiex_unmap_pci_memory(adapter, skb_cmd, PCI_DMA_FROMDEVICE);

		/* Take the pointer and set it to event pointer in adapter
		   and will return back after event handling callback */
		card->evt_buf_list[rdptr] = NULL;
		desc = card->evtbd_ring[rdptr];
		memset(desc, 0, sizeof(*desc));

		event = get_unaligned_le32(
			&skb_cmd->data[adapter->intf_hdr_len]);
		adapter->event_cause = event;
		/* The first 4bytes will be the event transfer header
		   len is 2 bytes followed by type which is 2 bytes */
		memcpy(&data_len, skb_cmd->data, sizeof(__le16));
		evt_len = le16_to_cpu(data_len);
		skb_trim(skb_cmd, evt_len);
		skb_pull(skb_cmd, adapter->intf_hdr_len);
		mwifiex_dbg(adapter, EVENT,
			    "info: Event length: %d\n", evt_len);

		if (evt_len > MWIFIEX_EVENT_HEADER_LEN &&
		    evt_len < MAX_EVENT_SIZE)
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
			mwifiex_dbg(adapter, ERROR,
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
		mwifiex_dbg(adapter, ERROR,
			    "event_complete: Invalid rdptr 0x%x\n",
			    rdptr);
		return -EINVAL;
	}

	/* Read the event ring write pointer set by firmware */
	if (mwifiex_read_reg(adapter, reg->evt_wrptr, &wrptr)) {
		mwifiex_dbg(adapter, ERROR,
			    "event_complete: failed to read reg->evt_wrptr\n");
		return -1;
	}

	if (!card->evt_buf_list[rdptr]) {
		skb_push(skb, adapter->intf_hdr_len);
		skb_put(skb, MAX_EVENT_SIZE - skb->len);
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
		mwifiex_dbg(adapter, ERROR,
			    "info: ERROR: buf still valid at index %d, <%p, %p>\n",
			    rdptr, card->evt_buf_list[rdptr], skb);
	}

	if ((++card->evtbd_rdptr & MWIFIEX_EVTBD_MASK) == MWIFIEX_MAX_EVT_BD) {
		card->evtbd_rdptr = ((card->evtbd_rdptr &
					reg->evt_rollover_ind) ^
					reg->evt_rollover_ind);
	}

	mwifiex_dbg(adapter, EVENT,
		    "info: Updated <Rd: 0x%x, Wr: 0x%x>",
		    card->evtbd_rdptr, wrptr);

	/* Write the event ring read pointer in to reg->evt_rdptr */
	if (mwifiex_write_reg(adapter, reg->evt_rdptr,
			      card->evtbd_rdptr)) {
		mwifiex_dbg(adapter, ERROR,
			    "event_complete: failed to read reg->evt_rdptr\n");
		return -1;
	}

	mwifiex_dbg(adapter, EVENT,
		    "info: Check Events Again\n");
	ret = mwifiex_pcie_process_event_ready(adapter);

	return ret;
}

/* Combo firmware image is a combination of
 * (1) combo crc heaer, start with CMD5
 * (2) bluetooth image, start with CMD7, end with CMD6, data wrapped in CMD1.
 * (3) wifi image.
 *
 * This function bypass the header and bluetooth part, return
 * the offset of tail wifi-only part. If the image is already wifi-only,
 * that is start with CMD1, return 0.
 */

static int mwifiex_extract_wifi_fw(struct mwifiex_adapter *adapter,
				   const void *firmware, u32 firmware_len) {
	const struct mwifiex_fw_data *fwdata;
	u32 offset = 0, data_len, dnld_cmd;
	int ret = 0;
	bool cmd7_before = false, first_cmd = false;

	while (1) {
		/* Check for integer and buffer overflow */
		if (offset + sizeof(fwdata->header) < sizeof(fwdata->header) ||
		    offset + sizeof(fwdata->header) >= firmware_len) {
			mwifiex_dbg(adapter, ERROR,
				    "extract wifi-only fw failure!\n");
			ret = -1;
			goto done;
		}

		fwdata = firmware + offset;
		dnld_cmd = le32_to_cpu(fwdata->header.dnld_cmd);
		data_len = le32_to_cpu(fwdata->header.data_length);

		/* Skip past header */
		offset += sizeof(fwdata->header);

		switch (dnld_cmd) {
		case MWIFIEX_FW_DNLD_CMD_1:
			if (offset + data_len < data_len) {
				mwifiex_dbg(adapter, ERROR, "bad FW parse\n");
				ret = -1;
				goto done;
			}

			/* Image start with cmd1, already wifi-only firmware */
			if (!first_cmd) {
				mwifiex_dbg(adapter, MSG,
					    "input wifi-only firmware\n");
				return 0;
			}

			if (!cmd7_before) {
				mwifiex_dbg(adapter, ERROR,
					    "no cmd7 before cmd1!\n");
				ret = -1;
				goto done;
			}
			offset += data_len;
			break;
		case MWIFIEX_FW_DNLD_CMD_5:
			first_cmd = true;
			/* Check for integer overflow */
			if (offset + data_len < data_len) {
				mwifiex_dbg(adapter, ERROR, "bad FW parse\n");
				ret = -1;
				goto done;
			}
			offset += data_len;
			break;
		case MWIFIEX_FW_DNLD_CMD_6:
			first_cmd = true;
			/* Check for integer overflow */
			if (offset + data_len < data_len) {
				mwifiex_dbg(adapter, ERROR, "bad FW parse\n");
				ret = -1;
				goto done;
			}
			offset += data_len;
			if (offset >= firmware_len) {
				mwifiex_dbg(adapter, ERROR,
					    "extract wifi-only fw failure!\n");
				ret = -1;
			} else {
				ret = offset;
			}
			goto done;
		case MWIFIEX_FW_DNLD_CMD_7:
			first_cmd = true;
			cmd7_before = true;
			break;
		default:
			mwifiex_dbg(adapter, ERROR, "unknown dnld_cmd %d\n",
				    dnld_cmd);
			ret = -1;
			goto done;
		}
	}

done:
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
	u32 txlen, tx_blocks = 0, tries, len, val;
	u32 block_retry_cnt = 0;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	if (!firmware || !firmware_len) {
		mwifiex_dbg(adapter, ERROR,
			    "No firmware image found! Terminating download\n");
		return -1;
	}

	mwifiex_dbg(adapter, INFO,
		    "info: Downloading FW image (%d bytes)\n",
		    firmware_len);

	if (mwifiex_pcie_disable_host_int(adapter)) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: Disabling interrupts failed.\n", __func__);
		return -1;
	}

	skb = dev_alloc_skb(MWIFIEX_UPLD_SIZE);
	if (!skb) {
		ret = -ENOMEM;
		goto done;
	}

	ret = mwifiex_read_reg(adapter, PCIE_SCRATCH_13_REG, &val);
	if (ret) {
		mwifiex_dbg(adapter, FATAL, "Failed to read scratch register 13\n");
		goto done;
	}

	/* PCIE FLR case: extract wifi part from combo firmware*/
	if (val == MWIFIEX_PCIE_FLR_HAPPENS) {
		ret = mwifiex_extract_wifi_fw(adapter, firmware, firmware_len);
		if (ret < 0) {
			mwifiex_dbg(adapter, ERROR, "Failed to extract wifi fw\n");
			goto done;
		}
		offset = ret;
		mwifiex_dbg(adapter, MSG,
			    "info: dnld wifi firmware from %d bytes\n", offset);
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
				mwifiex_dbg(adapter, FATAL,
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
			mwifiex_dbg(adapter, ERROR,
				    "FW download failure @ %d, invalid length %d\n",
				    offset, len);
			ret = -1;
			goto done;
		}

		txlen = len;

		if (len & BIT(0)) {
			block_retry_cnt++;
			if (block_retry_cnt > MAX_WRITE_IOMEM_RETRY) {
				mwifiex_dbg(adapter, ERROR,
					    "FW download failure @ %d, over max\t"
					    "retry count\n", offset);
				ret = -1;
				goto done;
			}
			mwifiex_dbg(adapter, ERROR,
				    "FW CRC error indicated by the\t"
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

			tx_blocks = (txlen + card->pcie.blksz_fw_dl - 1) /
				    card->pcie.blksz_fw_dl;

			/* Copy payload to buffer */
			memmove(skb->data, &firmware[offset], txlen);
		}

		skb_put(skb, MWIFIEX_UPLD_SIZE - skb->len);
		skb_trim(skb, tx_blocks * card->pcie.blksz_fw_dl);

		/* Send the boot command to device */
		if (mwifiex_pcie_send_boot_cmd(adapter, skb)) {
			mwifiex_dbg(adapter, ERROR,
				    "Failed to send firmware download command\n");
			ret = -1;
			goto done;
		}

		/* Wait for the command done interrupt */
		for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
			if (mwifiex_read_reg(adapter, PCIE_CPU_INT_STATUS,
					     &ireg_intr)) {
				mwifiex_dbg(adapter, ERROR,
					    "%s: Failed to read\t"
					    "interrupt status during fw dnld.\n",
					    __func__);
				mwifiex_unmap_pci_memory(adapter, skb,
							 PCI_DMA_TODEVICE);
				ret = -1;
				goto done;
			}
			if (!(ireg_intr & CPU_INTR_DOOR_BELL))
				break;
			usleep_range(10, 20);
		}
		if (ireg_intr & CPU_INTR_DOOR_BELL) {
			mwifiex_dbg(adapter, ERROR, "%s: Card failed to ACK download\n",
				    __func__);
			mwifiex_unmap_pci_memory(adapter, skb,
						 PCI_DMA_TODEVICE);
			ret = -1;
			goto done;
		}

		mwifiex_unmap_pci_memory(adapter, skb, PCI_DMA_TODEVICE);

		offset += txlen;
	} while (true);

	mwifiex_dbg(adapter, MSG,
		    "info: FW download over, size %d bytes\n", offset);

	ret = 0;

done:
	dev_kfree_skb_any(skb);
	return ret;
}

/*
 * This function checks the firmware status in card.
 */
static int
mwifiex_check_fw_status(struct mwifiex_adapter *adapter, u32 poll_num)
{
	int ret = 0;
	u32 firmware_stat;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	u32 tries;

	/* Mask spurios interrupts */
	if (mwifiex_write_reg(adapter, PCIE_HOST_INT_STATUS_MASK,
			      HOST_INTR_MASK)) {
		mwifiex_dbg(adapter, ERROR,
			    "Write register failed\n");
		return -1;
	}

	mwifiex_dbg(adapter, INFO,
		    "Setting driver ready signature\n");
	if (mwifiex_write_reg(adapter, reg->drv_rdy,
			      FIRMWARE_READY_PCIE)) {
		mwifiex_dbg(adapter, ERROR,
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

		mwifiex_dbg(adapter, INFO, "Try %d if FW is ready <%d,%#x>",
			    tries, ret, firmware_stat);

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

	return ret;
}

/* This function checks if WLAN is the winner.
 */
static int
mwifiex_check_winner_status(struct mwifiex_adapter *adapter)
{
	u32 winner = 0;
	int ret = 0;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	if (mwifiex_read_reg(adapter, reg->fw_status, &winner)) {
		ret = -1;
	} else if (!winner) {
		mwifiex_dbg(adapter, INFO, "PCI-E is the winner\n");
		adapter->winner = 1;
	} else {
		mwifiex_dbg(adapter, ERROR,
			    "PCI-E is not the winner <%#x>", winner);
	}

	return ret;
}

/*
 * This function reads the interrupt status from card.
 */
static void mwifiex_interrupt_status(struct mwifiex_adapter *adapter,
				     int msg_id)
{
	u32 pcie_ireg;
	unsigned long flags;
	struct pcie_service_card *card = adapter->card;

	if (card->msi_enable) {
		spin_lock_irqsave(&adapter->int_lock, flags);
		adapter->int_status = 1;
		spin_unlock_irqrestore(&adapter->int_lock, flags);
		return;
	}

	if (!mwifiex_pcie_ok_to_access_hw(adapter))
		return;

	if (card->msix_enable && msg_id >= 0) {
		pcie_ireg = BIT(msg_id);
	} else {
		if (mwifiex_read_reg(adapter, PCIE_HOST_INT_STATUS,
				     &pcie_ireg)) {
			mwifiex_dbg(adapter, ERROR, "Read register failed\n");
			return;
		}

		if ((pcie_ireg == 0xFFFFFFFF) || !pcie_ireg)
			return;


		mwifiex_pcie_disable_host_int(adapter);

		/* Clear the pending interrupts */
		if (mwifiex_write_reg(adapter, PCIE_HOST_INT_STATUS,
				      ~pcie_ireg)) {
			mwifiex_dbg(adapter, ERROR,
				    "Write register failed\n");
			return;
		}
	}

	if (!adapter->pps_uapsd_mode &&
	    adapter->ps_state == PS_STATE_SLEEP &&
	    mwifiex_pcie_ok_to_access_hw(adapter)) {
		/* Potentially for PCIe we could get other
		 * interrupts like shared. Don't change power
		 * state until cookie is set
		 */
		adapter->ps_state = PS_STATE_AWAKE;
		adapter->pm_wakeup_fw_try = false;
		del_timer(&adapter->wakeup_timer);
	}

	spin_lock_irqsave(&adapter->int_lock, flags);
	adapter->int_status |= pcie_ireg;
	spin_unlock_irqrestore(&adapter->int_lock, flags);
	mwifiex_dbg(adapter, INTR, "ireg: 0x%08x\n", pcie_ireg);
}

/*
 * Interrupt handler for PCIe root port
 *
 * This function reads the interrupt status from firmware and assigns
 * the main process in workqueue which will handle the interrupt.
 */
static irqreturn_t mwifiex_pcie_interrupt(int irq, void *context)
{
	struct mwifiex_msix_context *ctx = context;
	struct pci_dev *pdev = ctx->dev;
	struct pcie_service_card *card;
	struct mwifiex_adapter *adapter;

	card = pci_get_drvdata(pdev);

	if (!card->adapter) {
		pr_err("info: %s: card=%p adapter=%p\n", __func__, card,
		       card ? card->adapter : NULL);
		goto exit;
	}
	adapter = card->adapter;

	if (test_bit(MWIFIEX_SURPRISE_REMOVED, &adapter->work_flags))
		goto exit;

	if (card->msix_enable)
		mwifiex_interrupt_status(adapter, ctx->msg_id);
	else
		mwifiex_interrupt_status(adapter, -1);

	mwifiex_queue_main_work(adapter);

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
	u32 pcie_ireg = 0;
	unsigned long flags;
	struct pcie_service_card *card = adapter->card;

	spin_lock_irqsave(&adapter->int_lock, flags);
	if (!card->msi_enable) {
		/* Clear out unused interrupts */
		pcie_ireg = adapter->int_status;
	}
	adapter->int_status = 0;
	spin_unlock_irqrestore(&adapter->int_lock, flags);

	if (card->msi_enable) {
		if (mwifiex_pcie_ok_to_access_hw(adapter)) {
			if (mwifiex_read_reg(adapter, PCIE_HOST_INT_STATUS,
					     &pcie_ireg)) {
				mwifiex_dbg(adapter, ERROR,
					    "Read register failed\n");
				return -1;
			}

			if ((pcie_ireg != 0xFFFFFFFF) && (pcie_ireg)) {
				if (mwifiex_write_reg(adapter,
						      PCIE_HOST_INT_STATUS,
						      ~pcie_ireg)) {
					mwifiex_dbg(adapter, ERROR,
						    "Write register failed\n");
					return -1;
				}
				if (!adapter->pps_uapsd_mode &&
				    adapter->ps_state == PS_STATE_SLEEP) {
					adapter->ps_state = PS_STATE_AWAKE;
					adapter->pm_wakeup_fw_try = false;
					del_timer(&adapter->wakeup_timer);
				}
			}
		}
	}

	if (pcie_ireg & HOST_INTR_DNLD_DONE) {
		mwifiex_dbg(adapter, INTR, "info: TX DNLD Done\n");
		ret = mwifiex_pcie_send_data_complete(adapter);
		if (ret)
			return ret;
	}
	if (pcie_ireg & HOST_INTR_UPLD_RDY) {
		mwifiex_dbg(adapter, INTR, "info: Rx DATA\n");
		ret = mwifiex_pcie_process_recv_data(adapter);
		if (ret)
			return ret;
	}
	if (pcie_ireg & HOST_INTR_EVENT_RDY) {
		mwifiex_dbg(adapter, INTR, "info: Rx EVENT\n");
		ret = mwifiex_pcie_process_event_ready(adapter);
		if (ret)
			return ret;
	}
	if (pcie_ireg & HOST_INTR_CMD_DONE) {
		if (adapter->cmd_sent) {
			mwifiex_dbg(adapter, INTR,
				    "info: CMD sent Interrupt\n");
			adapter->cmd_sent = false;
		}
		/* Handle command response */
		ret = mwifiex_pcie_process_cmd_complete(adapter);
		if (ret)
			return ret;
	}

	mwifiex_dbg(adapter, INTR,
		    "info: cmd_sent=%d data_sent=%d\n",
		    adapter->cmd_sent, adapter->data_sent);
	if (!card->msi_enable && !card->msix_enable &&
				 adapter->ps_state != PS_STATE_SLEEP)
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
		mwifiex_dbg(adapter, ERROR,
			    "Passed NULL skb to %s\n", __func__);
		return -1;
	}

	if (type == MWIFIEX_TYPE_DATA)
		return mwifiex_pcie_send_data(adapter, skb, tx_param);
	else if (type == MWIFIEX_TYPE_CMD)
		return mwifiex_pcie_send_cmd(adapter, skb);

	return 0;
}

/* Function to dump PCIE scratch registers in case of FW crash
 */
static int
mwifiex_pcie_reg_dump(struct mwifiex_adapter *adapter, char *drv_buf)
{
	char *p = drv_buf;
	char buf[256], *ptr;
	int i;
	u32 value;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	int pcie_scratch_reg[] = {PCIE_SCRATCH_12_REG,
				  PCIE_SCRATCH_14_REG,
				  PCIE_SCRATCH_15_REG};

	if (!p)
		return 0;

	mwifiex_dbg(adapter, MSG, "PCIE register dump start\n");

	if (mwifiex_read_reg(adapter, reg->fw_status, &value)) {
		mwifiex_dbg(adapter, ERROR, "failed to read firmware status");
		return 0;
	}

	ptr = buf;
	mwifiex_dbg(adapter, MSG, "pcie scratch register:");
	for (i = 0; i < ARRAY_SIZE(pcie_scratch_reg); i++) {
		mwifiex_read_reg(adapter, pcie_scratch_reg[i], &value);
		ptr += sprintf(ptr, "reg:0x%x, value=0x%x\n",
			       pcie_scratch_reg[i], value);
	}

	mwifiex_dbg(adapter, MSG, "%s\n", buf);
	p += sprintf(p, "%s\n", buf);

	mwifiex_dbg(adapter, MSG, "PCIE register dump end\n");

	return p - drv_buf;
}

/* This function read/write firmware */
static enum rdwr_status
mwifiex_pcie_rdwr_firmware(struct mwifiex_adapter *adapter, u8 doneflag)
{
	int ret, tries;
	u8 ctrl_data;
	u32 fw_status;
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	if (mwifiex_read_reg(adapter, reg->fw_status, &fw_status))
		return RDWR_STATUS_FAILURE;

	ret = mwifiex_write_reg(adapter, reg->fw_dump_ctrl,
				reg->fw_dump_host_ready);
	if (ret) {
		mwifiex_dbg(adapter, ERROR,
			    "PCIE write err\n");
		return RDWR_STATUS_FAILURE;
	}

	for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
		mwifiex_read_reg_byte(adapter, reg->fw_dump_ctrl, &ctrl_data);
		if (ctrl_data == FW_DUMP_DONE)
			return RDWR_STATUS_SUCCESS;
		if (doneflag && ctrl_data == doneflag)
			return RDWR_STATUS_DONE;
		if (ctrl_data != reg->fw_dump_host_ready) {
			mwifiex_dbg(adapter, WARN,
				    "The ctrl reg was changed, re-try again!\n");
			ret = mwifiex_write_reg(adapter, reg->fw_dump_ctrl,
						reg->fw_dump_host_ready);
			if (ret) {
				mwifiex_dbg(adapter, ERROR,
					    "PCIE write err\n");
				return RDWR_STATUS_FAILURE;
			}
		}
		usleep_range(100, 200);
	}

	mwifiex_dbg(adapter, ERROR, "Fail to pull ctrl_data\n");
	return RDWR_STATUS_FAILURE;
}

/* This function dump firmware memory to file */
static void mwifiex_pcie_fw_dump(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *creg = card->pcie.reg;
	unsigned int reg, reg_start, reg_end;
	u8 *dbg_ptr, *end_ptr, *tmp_ptr, fw_dump_num, dump_num;
	u8 idx, i, read_reg, doneflag = 0;
	enum rdwr_status stat;
	u32 memory_size;
	int ret;

	if (!card->pcie.can_dump_fw)
		return;

	for (idx = 0; idx < adapter->num_mem_types; idx++) {
		struct memory_type_mapping *entry =
				&adapter->mem_type_mapping_tbl[idx];

		if (entry->mem_ptr) {
			vfree(entry->mem_ptr);
			entry->mem_ptr = NULL;
		}
		entry->mem_size = 0;
	}

	mwifiex_dbg(adapter, MSG, "== mwifiex firmware dump start ==\n");

	/* Read the number of the memories which will dump */
	stat = mwifiex_pcie_rdwr_firmware(adapter, doneflag);
	if (stat == RDWR_STATUS_FAILURE)
		return;

	reg = creg->fw_dump_start;
	mwifiex_read_reg_byte(adapter, reg, &fw_dump_num);

	/* W8997 chipset firmware dump will be restore in single region*/
	if (fw_dump_num == 0)
		dump_num = 1;
	else
		dump_num = fw_dump_num;

	/* Read the length of every memory which will dump */
	for (idx = 0; idx < dump_num; idx++) {
		struct memory_type_mapping *entry =
				&adapter->mem_type_mapping_tbl[idx];
		memory_size = 0;
		if (fw_dump_num != 0) {
			stat = mwifiex_pcie_rdwr_firmware(adapter, doneflag);
			if (stat == RDWR_STATUS_FAILURE)
				return;

			reg = creg->fw_dump_start;
			for (i = 0; i < 4; i++) {
				mwifiex_read_reg_byte(adapter, reg, &read_reg);
				memory_size |= (read_reg << (i * 8));
				reg++;
			}
		} else {
			memory_size = MWIFIEX_FW_DUMP_MAX_MEMSIZE;
		}

		if (memory_size == 0) {
			mwifiex_dbg(adapter, MSG, "Firmware dump Finished!\n");
			ret = mwifiex_write_reg(adapter, creg->fw_dump_ctrl,
						creg->fw_dump_read_done);
			if (ret) {
				mwifiex_dbg(adapter, ERROR, "PCIE write err\n");
				return;
			}
			break;
		}

		mwifiex_dbg(adapter, DUMP,
			    "%s_SIZE=0x%x\n", entry->mem_name, memory_size);
		entry->mem_ptr = vmalloc(memory_size + 1);
		entry->mem_size = memory_size;
		if (!entry->mem_ptr) {
			mwifiex_dbg(adapter, ERROR,
				    "Vmalloc %s failed\n", entry->mem_name);
			return;
		}
		dbg_ptr = entry->mem_ptr;
		end_ptr = dbg_ptr + memory_size;

		doneflag = entry->done_flag;
		mwifiex_dbg(adapter, DUMP, "Start %s output, please wait...\n",
			    entry->mem_name);

		do {
			stat = mwifiex_pcie_rdwr_firmware(adapter, doneflag);
			if (RDWR_STATUS_FAILURE == stat)
				return;

			reg_start = creg->fw_dump_start;
			reg_end = creg->fw_dump_end;
			for (reg = reg_start; reg <= reg_end; reg++) {
				mwifiex_read_reg_byte(adapter, reg, dbg_ptr);
				if (dbg_ptr < end_ptr) {
					dbg_ptr++;
					continue;
				}
				mwifiex_dbg(adapter, ERROR,
					    "pre-allocated buf not enough\n");
				tmp_ptr =
					vzalloc(memory_size + MWIFIEX_SIZE_4K);
				if (!tmp_ptr)
					return;
				memcpy(tmp_ptr, entry->mem_ptr, memory_size);
				vfree(entry->mem_ptr);
				entry->mem_ptr = tmp_ptr;
				tmp_ptr = NULL;
				dbg_ptr = entry->mem_ptr + memory_size;
				memory_size += MWIFIEX_SIZE_4K;
				end_ptr = entry->mem_ptr + memory_size;
			}

			if (stat != RDWR_STATUS_DONE)
				continue;

			mwifiex_dbg(adapter, DUMP,
				    "%s done: size=0x%tx\n",
				    entry->mem_name, dbg_ptr - entry->mem_ptr);
			break;
		} while (true);
	}
	mwifiex_dbg(adapter, MSG, "== mwifiex firmware dump end ==\n");
}

static void mwifiex_pcie_device_dump_work(struct mwifiex_adapter *adapter)
{
	adapter->devdump_data = vzalloc(MWIFIEX_FW_DUMP_SIZE);
	if (!adapter->devdump_data) {
		mwifiex_dbg(adapter, ERROR,
			    "vzalloc devdump data failure!\n");
		return;
	}

	mwifiex_drv_info_dump(adapter);
	mwifiex_pcie_fw_dump(adapter);
	mwifiex_prepare_fw_dump_info(adapter);
	mwifiex_upload_device_dump(adapter);
}

static void mwifiex_pcie_card_reset_work(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;

	/* We can't afford to wait here; remove() might be waiting on us. If we
	 * can't grab the device lock, maybe we'll get another chance later.
	 */
	pci_try_reset_function(card->dev);
}

static void mwifiex_pcie_work(struct work_struct *work)
{
	struct pcie_service_card *card =
		container_of(work, struct pcie_service_card, work);

	if (test_and_clear_bit(MWIFIEX_IFACE_WORK_DEVICE_DUMP,
			       &card->work_flags))
		mwifiex_pcie_device_dump_work(card->adapter);
	if (test_and_clear_bit(MWIFIEX_IFACE_WORK_CARD_RESET,
			       &card->work_flags))
		mwifiex_pcie_card_reset_work(card->adapter);
}

/* This function dumps FW information */
static void mwifiex_pcie_device_dump(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;

	if (!test_and_set_bit(MWIFIEX_IFACE_WORK_DEVICE_DUMP,
			      &card->work_flags))
		schedule_work(&card->work);
}

static void mwifiex_pcie_card_reset(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;

	if (!test_and_set_bit(MWIFIEX_IFACE_WORK_CARD_RESET, &card->work_flags))
		schedule_work(&card->work);
}

static int mwifiex_pcie_alloc_buffers(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	int ret;

	card->cmdrsp_buf = NULL;
	ret = mwifiex_pcie_create_txbd_ring(adapter);
	if (ret) {
		mwifiex_dbg(adapter, ERROR, "Failed to create txbd ring\n");
		goto err_cre_txbd;
	}

	ret = mwifiex_pcie_create_rxbd_ring(adapter);
	if (ret) {
		mwifiex_dbg(adapter, ERROR, "Failed to create rxbd ring\n");
		goto err_cre_rxbd;
	}

	ret = mwifiex_pcie_create_evtbd_ring(adapter);
	if (ret) {
		mwifiex_dbg(adapter, ERROR, "Failed to create evtbd ring\n");
		goto err_cre_evtbd;
	}

	ret = mwifiex_pcie_alloc_cmdrsp_buf(adapter);
	if (ret) {
		mwifiex_dbg(adapter, ERROR, "Failed to allocate cmdbuf buffer\n");
		goto err_alloc_cmdbuf;
	}

	if (reg->sleep_cookie) {
		ret = mwifiex_pcie_alloc_sleep_cookie_buf(adapter);
		if (ret) {
			mwifiex_dbg(adapter, ERROR, "Failed to allocate sleep_cookie buffer\n");
			goto err_alloc_cookie;
		}
	} else {
		card->sleep_cookie_vbase = NULL;
	}

	return 0;

err_alloc_cookie:
	mwifiex_pcie_delete_cmdrsp_buf(adapter);
err_alloc_cmdbuf:
	mwifiex_pcie_delete_evtbd_ring(adapter);
err_cre_evtbd:
	mwifiex_pcie_delete_rxbd_ring(adapter);
err_cre_rxbd:
	mwifiex_pcie_delete_txbd_ring(adapter);
err_cre_txbd:
	return ret;
}

static void mwifiex_pcie_free_buffers(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	if (reg->sleep_cookie)
		mwifiex_pcie_delete_sleep_cookie_buf(adapter);

	mwifiex_pcie_delete_cmdrsp_buf(adapter);
	mwifiex_pcie_delete_evtbd_ring(adapter);
	mwifiex_pcie_delete_rxbd_ring(adapter);
	mwifiex_pcie_delete_txbd_ring(adapter);
}

/*
 * This function initializes the PCI-E host memory space, WCB rings, etc.
 */
static int mwifiex_init_pcie(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	int ret;
	struct pci_dev *pdev = card->dev;

	pci_set_drvdata(pdev, card);

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_enable_dev;

	pci_set_master(pdev);

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		pr_err("set_dma_mask(32) failed: %d\n", ret);
		goto err_set_dma_mask;
	}

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		pr_err("set_consistent_dma_mask(64) failed\n");
		goto err_set_dma_mask;
	}

	ret = pci_request_region(pdev, 0, DRV_NAME);
	if (ret) {
		pr_err("req_reg(0) error\n");
		goto err_req_region0;
	}
	card->pci_mmap = pci_iomap(pdev, 0, 0);
	if (!card->pci_mmap) {
		pr_err("iomap(0) error\n");
		ret = -EIO;
		goto err_iomap0;
	}
	ret = pci_request_region(pdev, 2, DRV_NAME);
	if (ret) {
		pr_err("req_reg(2) error\n");
		goto err_req_region2;
	}
	card->pci_mmap1 = pci_iomap(pdev, 2, 0);
	if (!card->pci_mmap1) {
		pr_err("iomap(2) error\n");
		ret = -EIO;
		goto err_iomap2;
	}

	pr_notice("PCI memory map Virt0: %pK PCI memory map Virt2: %pK\n",
		  card->pci_mmap, card->pci_mmap1);

	ret = mwifiex_pcie_alloc_buffers(adapter);
	if (ret)
		goto err_alloc_buffers;

	return 0;

err_alloc_buffers:
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
	return ret;
}

/*
 * This function cleans up the allocated card buffers.
 */
static void mwifiex_cleanup_pcie(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	struct pci_dev *pdev = card->dev;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	int ret;
	u32 fw_status;

	cancel_work_sync(&card->work);

	ret = mwifiex_read_reg(adapter, reg->fw_status, &fw_status);
	if (fw_status == FIRMWARE_READY_PCIE) {
		mwifiex_dbg(adapter, INFO,
			    "Clearing driver ready signature\n");
		if (mwifiex_write_reg(adapter, reg->drv_rdy, 0x00000000))
			mwifiex_dbg(adapter, ERROR,
				    "Failed to write driver not-ready signature\n");
	}

	pci_disable_device(pdev);

	pci_iounmap(pdev, card->pci_mmap);
	pci_iounmap(pdev, card->pci_mmap1);
	pci_release_region(pdev, 2);
	pci_release_region(pdev, 0);

	mwifiex_pcie_free_buffers(adapter);
}

static int mwifiex_pcie_request_irq(struct mwifiex_adapter *adapter)
{
	int ret, i, j;
	struct pcie_service_card *card = adapter->card;
	struct pci_dev *pdev = card->dev;

	if (card->pcie.reg->msix_support) {
		for (i = 0; i < MWIFIEX_NUM_MSIX_VECTORS; i++)
			card->msix_entries[i].entry = i;
		ret = pci_enable_msix_exact(pdev, card->msix_entries,
					    MWIFIEX_NUM_MSIX_VECTORS);
		if (!ret) {
			for (i = 0; i < MWIFIEX_NUM_MSIX_VECTORS; i++) {
				card->msix_ctx[i].dev = pdev;
				card->msix_ctx[i].msg_id = i;

				ret = request_irq(card->msix_entries[i].vector,
						  mwifiex_pcie_interrupt, 0,
						  "MWIFIEX_PCIE_MSIX",
						  &card->msix_ctx[i]);
				if (ret)
					break;
			}

			if (ret) {
				mwifiex_dbg(adapter, INFO, "request_irq fail: %d\n",
					    ret);
				for (j = 0; j < i; j++)
					free_irq(card->msix_entries[j].vector,
						 &card->msix_ctx[i]);
				pci_disable_msix(pdev);
			} else {
				mwifiex_dbg(adapter, MSG, "MSIx enabled!");
				card->msix_enable = 1;
				return 0;
			}
		}
	}

	if (pci_enable_msi(pdev) != 0)
		pci_disable_msi(pdev);
	else
		card->msi_enable = 1;

	mwifiex_dbg(adapter, INFO, "msi_enable = %d\n", card->msi_enable);

	card->share_irq_ctx.dev = pdev;
	card->share_irq_ctx.msg_id = -1;
	ret = request_irq(pdev->irq, mwifiex_pcie_interrupt, IRQF_SHARED,
			  "MRVL_PCIE", &card->share_irq_ctx);
	if (ret) {
		pr_err("request_irq failed: ret=%d\n", ret);
		return -1;
	}

	return 0;
}

/*
 * This function gets the firmware name for downloading by revision id
 *
 * Read revision id register to get revision id
 */
static void mwifiex_pcie_get_fw_name(struct mwifiex_adapter *adapter)
{
	int revision_id = 0;
	int version, magic;
	struct pcie_service_card *card = adapter->card;

	switch (card->dev->device) {
	case PCIE_DEVICE_ID_MARVELL_88W8766P:
		strcpy(adapter->fw_name, PCIE8766_DEFAULT_FW_NAME);
		break;
	case PCIE_DEVICE_ID_MARVELL_88W8897:
		mwifiex_write_reg(adapter, 0x0c58, 0x80c00000);
		mwifiex_read_reg(adapter, 0x0c58, &revision_id);
		revision_id &= 0xff00;
		switch (revision_id) {
		case PCIE8897_A0:
			strcpy(adapter->fw_name, PCIE8897_A0_FW_NAME);
			break;
		case PCIE8897_B0:
			strcpy(adapter->fw_name, PCIE8897_B0_FW_NAME);
			break;
		default:
			strcpy(adapter->fw_name, PCIE8897_DEFAULT_FW_NAME);

			break;
		}
		break;
	case PCIE_DEVICE_ID_MARVELL_88W8997:
		mwifiex_read_reg(adapter, 0x8, &revision_id);
		mwifiex_read_reg(adapter, 0x0cd0, &version);
		mwifiex_read_reg(adapter, 0x0cd4, &magic);
		revision_id &= 0xff;
		version &= 0x7;
		magic &= 0xff;
		if (revision_id == PCIE8997_A1 &&
		    magic == CHIP_MAGIC_VALUE &&
		    version == CHIP_VER_PCIEUART)
			strcpy(adapter->fw_name, PCIEUART8997_FW_NAME_V4);
		else
			strcpy(adapter->fw_name, PCIEUSB8997_FW_NAME_V4);
		break;
	default:
		break;
	}
}

/*
 * This function registers the PCIE device.
 *
 * PCIE IRQ is claimed, block size is set and driver data is initialized.
 */
static int mwifiex_register_dev(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;

	/* save adapter pointer in card */
	card->adapter = adapter;

	if (mwifiex_pcie_request_irq(adapter))
		return -1;

	adapter->tx_buf_size = card->pcie.tx_buf_size;
	adapter->mem_type_mapping_tbl = card->pcie.mem_type_mapping_tbl;
	adapter->num_mem_types = card->pcie.num_mem_types;
	adapter->ext_scan = card->pcie.can_ext_scan;
	mwifiex_pcie_get_fw_name(adapter);

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
	struct pci_dev *pdev = card->dev;
	int i;

	if (card->msix_enable) {
		for (i = 0; i < MWIFIEX_NUM_MSIX_VECTORS; i++)
			synchronize_irq(card->msix_entries[i].vector);

		for (i = 0; i < MWIFIEX_NUM_MSIX_VECTORS; i++)
			free_irq(card->msix_entries[i].vector,
				 &card->msix_ctx[i]);

		card->msix_enable = 0;
		pci_disable_msix(pdev);
	} else {
		mwifiex_dbg(adapter, INFO,
			    "%s(): calling free_irq()\n", __func__);
	       free_irq(card->dev->irq, &card->share_irq_ctx);

		if (card->msi_enable)
			pci_disable_msi(pdev);
	}
	card->adapter = NULL;
}

/*
 * This function initializes the PCI-E host memory space, WCB rings, etc.,
 * similar to mwifiex_init_pcie(), but without resetting PCI-E state.
 */
static void mwifiex_pcie_up_dev(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	struct pci_dev *pdev = card->dev;

	/* tx_buf_size might be changed to 3584 by firmware during
	 * data transfer, we should reset it to default size.
	 */
	adapter->tx_buf_size = card->pcie.tx_buf_size;

	mwifiex_pcie_alloc_buffers(adapter);

	pci_set_master(pdev);
}

/* This function cleans up the PCI-E host memory space. */
static void mwifiex_pcie_down_dev(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;
	struct pci_dev *pdev = card->dev;

	if (mwifiex_write_reg(adapter, reg->drv_rdy, 0x00000000))
		mwifiex_dbg(adapter, ERROR, "Failed to write driver not-ready signature\n");

	pci_clear_master(pdev);

	adapter->seq_num = 0;

	mwifiex_pcie_free_buffers(adapter);
}

static struct mwifiex_if_ops pcie_ops = {
	.init_if =			mwifiex_init_pcie,
	.cleanup_if =			mwifiex_cleanup_pcie,
	.check_fw_status =		mwifiex_check_fw_status,
	.check_winner_status =          mwifiex_check_winner_status,
	.prog_fw =			mwifiex_prog_fw_w_helper,
	.register_dev =			mwifiex_register_dev,
	.unregister_dev =		mwifiex_unregister_dev,
	.enable_int =			mwifiex_pcie_enable_host_int,
	.disable_int =			mwifiex_pcie_disable_host_int_noerr,
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
	.card_reset =			mwifiex_pcie_card_reset,
	.reg_dump =			mwifiex_pcie_reg_dump,
	.device_dump =			mwifiex_pcie_device_dump,
	.down_dev =			mwifiex_pcie_down_dev,
	.up_dev =			mwifiex_pcie_up_dev,
};

module_pci_driver(mwifiex_pcie);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell WiFi-Ex PCI-Express Driver version " PCIE_VERSION);
MODULE_VERSION(PCIE_VERSION);
MODULE_LICENSE("GPL v2");
