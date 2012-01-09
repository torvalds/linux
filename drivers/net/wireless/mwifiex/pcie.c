/*
 * Marvell Wireless LAN device driver: PCIE specific handling
 *
 * Copyright (C) 2011, Marvell International Ltd.
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
static int mwifiex_pcie_enable_host_int(struct mwifiex_adapter *adapter);
static int mwifiex_pcie_resume(struct pci_dev *pdev);

/*
 * This function is called after skb allocation to update
 * "skb->cb" with physical address of data pointer.
 */
static phys_addr_t *mwifiex_update_sk_buff_pa(struct sk_buff *skb)
{
	phys_addr_t *buf_pa = MWIFIEX_SKB_PACB(skb);

	*buf_pa = (phys_addr_t)virt_to_phys(skb->data);

	return buf_pa;
}

/*
 * This function reads sleep cookie and checks if FW is ready
 */
static bool mwifiex_pcie_ok_to_access_hw(struct mwifiex_adapter *adapter)
{
	u32 *cookie_addr;
	struct pcie_service_card *card = adapter->card;

	if (card->sleep_cookie) {
		cookie_addr = (u32 *)card->sleep_cookie->data;
		dev_dbg(adapter->dev, "info: ACCESS_HW: sleep cookie=0x%x\n",
			*cookie_addr);
		if (*cookie_addr == FW_AWAKE_COOKIE)
			return true;
	}

	return false;
}

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
	if (!card) {
		pr_err("%s: failed to alloc memory\n", __func__);
		return -ENOMEM;
	}

	card->dev = pdev;

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
	int i;

	card = pci_get_drvdata(pdev);
	if (!card)
		return;

	adapter = card->adapter;
	if (!adapter || !adapter->priv_num)
		return;

	if (user_rmmod) {
#ifdef CONFIG_PM
		if (adapter->is_suspended)
			mwifiex_pcie_resume(pdev);
#endif

		for (i = 0; i < adapter->priv_num; i++)
			if ((GET_BSS_ROLE(adapter->priv[i]) ==
						MWIFIEX_BSS_ROLE_STA) &&
					adapter->priv[i]->media_connected)
				mwifiex_deauthenticate(adapter->priv[i], NULL);

		mwifiex_disable_auto_ds(mwifiex_get_priv(adapter,
						 MWIFIEX_BSS_ROLE_ANY));

		mwifiex_init_shutdown_fw(mwifiex_get_priv(adapter,
						MWIFIEX_BSS_ROLE_ANY),
					 MWIFIEX_FUNC_SHUTDOWN);
	}

	mwifiex_remove_card(card->adapter, &add_remove_card_sem);
	kfree(card);
}

/*
 * Kernel needs to suspend all functions separately. Therefore all
 * registered functions must have drivers with suspend and resume
 * methods. Failing that the kernel simply removes the whole card.
 *
 * If already not suspended, this function allocates and sends a host
 * sleep activate request to the firmware and turns off the traffic.
 */
static int mwifiex_pcie_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct mwifiex_adapter *adapter;
	struct pcie_service_card *card;
	int hs_actived, i;

	if (pdev) {
		card = (struct pcie_service_card *) pci_get_drvdata(pdev);
		if (!card || card->adapter) {
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

	for (i = 0; i < adapter->priv_num; i++)
		netif_carrier_off(adapter->priv[i]->netdev);

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
static int mwifiex_pcie_resume(struct pci_dev *pdev)
{
	struct mwifiex_adapter *adapter;
	struct pcie_service_card *card;
	int i;

	if (pdev) {
		card = (struct pcie_service_card *) pci_get_drvdata(pdev);
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

	for (i = 0; i < adapter->priv_num; i++)
		if (adapter->priv[i]->media_connected)
			netif_carrier_on(adapter->priv[i]->netdev);

	mwifiex_cancel_hs(mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_STA),
			      MWIFIEX_ASYNC_CMD);

	return 0;
}

#define PCIE_VENDOR_ID_MARVELL              (0x11ab)
#define PCIE_DEVICE_ID_MARVELL_88W8766P		(0x2b30)

static DEFINE_PCI_DEVICE_TABLE(mwifiex_ids) = {
	{
		PCIE_VENDOR_ID_MARVELL, PCIE_DEVICE_ID_MARVELL_88W8766P,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	},
	{},
};

MODULE_DEVICE_TABLE(pci, mwifiex_ids);

/* PCI Device Driver */
static struct pci_driver __refdata mwifiex_pcie = {
	.name     = "mwifiex_pcie",
	.id_table = mwifiex_ids,
	.probe    = mwifiex_pcie_probe,
	.remove   = mwifiex_pcie_remove,
#ifdef CONFIG_PM
	/* Power Management Hooks */
	.suspend  = mwifiex_pcie_suspend,
	.resume   = mwifiex_pcie_resume,
#endif
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

/*
 * This function wakes up the card.
 *
 * A host power up command is written to the card configuration
 * register to wake up the card.
 */
static int mwifiex_pm_wakeup_card(struct mwifiex_adapter *adapter)
{
	int i = 0;

	while (mwifiex_pcie_ok_to_access_hw(adapter)) {
		i++;
		udelay(10);
		/* 50ms max wait */
		if (i == 50000)
			break;
	}

	dev_dbg(adapter->dev, "event: Wakeup device...\n");

	/* Enable interrupts or any chip access will wakeup device */
	if (mwifiex_write_reg(adapter, PCIE_HOST_INT_MASK, HOST_INTR_MASK)) {
		dev_warn(adapter->dev, "Enable host interrupt failed\n");
		return -1;
	}

	dev_dbg(adapter->dev, "PCIE wakeup: Setting PS_STATE_AWAKE\n");
	adapter->ps_state = PS_STATE_AWAKE;

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
 * This function creates buffer descriptor ring for TX
 */
static int mwifiex_pcie_create_txbd_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	struct sk_buff *skb;
	int i;
	phys_addr_t *buf_pa;

	/*
	 * driver maintaines the write pointer and firmware maintaines the read
	 * pointer. The write pointer starts at 0 (zero) while the read pointer
	 * starts at zero with rollover bit set
	 */
	card->txbd_wrptr = 0;
	card->txbd_rdptr |= MWIFIEX_BD_FLAG_ROLLOVER_IND;

	/* allocate shared memory for the BD ring and divide the same in to
	   several descriptors */
	card->txbd_ring_size = sizeof(struct mwifiex_pcie_buf_desc) *
				MWIFIEX_MAX_TXRX_BD;
	dev_dbg(adapter->dev, "info: txbd_ring: Allocating %d bytes\n",
				card->txbd_ring_size);
	card->txbd_ring_vbase = kzalloc(card->txbd_ring_size, GFP_KERNEL);
	if (!card->txbd_ring_vbase) {
		dev_err(adapter->dev, "Unable to allocate buffer for txbd ring.\n");
		kfree(card->txbd_ring_vbase);
		return -1;
	}
	card->txbd_ring_pbase = virt_to_phys(card->txbd_ring_vbase);

	dev_dbg(adapter->dev, "info: txbd_ring - base: %p, pbase: %#x:%x,"
			"len: %x\n", card->txbd_ring_vbase,
			(u32)card->txbd_ring_pbase,
			(u32)((u64)card->txbd_ring_pbase >> 32),
			card->txbd_ring_size);

	for (i = 0; i < MWIFIEX_MAX_TXRX_BD; i++) {
		card->txbd_ring[i] = (struct mwifiex_pcie_buf_desc *)
				(card->txbd_ring_vbase +
				(sizeof(struct mwifiex_pcie_buf_desc) * i));

		/* Allocate buffer here so that firmware can DMA data from it */
		skb = dev_alloc_skb(MWIFIEX_RX_DATA_BUF_SIZE);
		if (!skb) {
			dev_err(adapter->dev, "Unable to allocate skb for TX ring.\n");
			kfree(card->txbd_ring_vbase);
			return -ENOMEM;
		}
		buf_pa = mwifiex_update_sk_buff_pa(skb);

		skb_put(skb, MWIFIEX_RX_DATA_BUF_SIZE);
		dev_dbg(adapter->dev, "info: TX ring: add new skb base: %p, "
				"buf_base: %p, buf_pbase: %#x:%x, "
				"buf_len: %#x\n", skb, skb->data,
				(u32)*buf_pa, (u32)(((u64)*buf_pa >> 32)),
				skb->len);

		card->tx_buf_list[i] = skb;
		card->txbd_ring[i]->paddr = *buf_pa;
		card->txbd_ring[i]->len = (u16)skb->len;
		card->txbd_ring[i]->flags = 0;
	}

	return 0;
}

static int mwifiex_pcie_delete_txbd_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	int i;

	for (i = 0; i < MWIFIEX_MAX_TXRX_BD; i++) {
		if (card->tx_buf_list[i])
			dev_kfree_skb_any(card->tx_buf_list[i]);
		card->tx_buf_list[i] = NULL;
		card->txbd_ring[i]->paddr = 0;
		card->txbd_ring[i]->len = 0;
		card->txbd_ring[i]->flags = 0;
		card->txbd_ring[i] = NULL;
	}

	kfree(card->txbd_ring_vbase);
	card->txbd_ring_size = 0;
	card->txbd_wrptr = 0;
	card->txbd_rdptr = 0 | MWIFIEX_BD_FLAG_ROLLOVER_IND;
	card->txbd_ring_vbase = NULL;

	return 0;
}

/*
 * This function creates buffer descriptor ring for RX
 */
static int mwifiex_pcie_create_rxbd_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	struct sk_buff *skb;
	int i;
	phys_addr_t *buf_pa;

	/*
	 * driver maintaines the read pointer and firmware maintaines the write
	 * pointer. The write pointer starts at 0 (zero) while the read pointer
	 * starts at zero with rollover bit set
	 */
	card->rxbd_wrptr = 0;
	card->rxbd_rdptr |= MWIFIEX_BD_FLAG_ROLLOVER_IND;

	card->rxbd_ring_size = sizeof(struct mwifiex_pcie_buf_desc) *
				MWIFIEX_MAX_TXRX_BD;
	dev_dbg(adapter->dev, "info: rxbd_ring: Allocating %d bytes\n",
				card->rxbd_ring_size);
	card->rxbd_ring_vbase = kzalloc(card->rxbd_ring_size, GFP_KERNEL);
	if (!card->rxbd_ring_vbase) {
		dev_err(adapter->dev, "Unable to allocate buffer for "
				"rxbd_ring.\n");
		return -1;
	}
	card->rxbd_ring_pbase = virt_to_phys(card->rxbd_ring_vbase);

	dev_dbg(adapter->dev, "info: rxbd_ring - base: %p, pbase: %#x:%x,"
			"len: %#x\n", card->rxbd_ring_vbase,
			(u32)card->rxbd_ring_pbase,
			(u32)((u64)card->rxbd_ring_pbase >> 32),
			card->rxbd_ring_size);

	for (i = 0; i < MWIFIEX_MAX_TXRX_BD; i++) {
		card->rxbd_ring[i] = (struct mwifiex_pcie_buf_desc *)
				(card->rxbd_ring_vbase +
				(sizeof(struct mwifiex_pcie_buf_desc) * i));

		/* Allocate skb here so that firmware can DMA data from it */
		skb = dev_alloc_skb(MWIFIEX_RX_DATA_BUF_SIZE);
		if (!skb) {
			dev_err(adapter->dev, "Unable to allocate skb for RX ring.\n");
			kfree(card->rxbd_ring_vbase);
			return -ENOMEM;
		}
		buf_pa = mwifiex_update_sk_buff_pa(skb);
		skb_put(skb, MWIFIEX_RX_DATA_BUF_SIZE);

		dev_dbg(adapter->dev, "info: RX ring: add new skb base: %p, "
				"buf_base: %p, buf_pbase: %#x:%x, "
				"buf_len: %#x\n", skb, skb->data,
				(u32)*buf_pa, (u32)((u64)*buf_pa >> 32),
				skb->len);

		card->rx_buf_list[i] = skb;
		card->rxbd_ring[i]->paddr = *buf_pa;
		card->rxbd_ring[i]->len = (u16)skb->len;
		card->rxbd_ring[i]->flags = 0;
	}

	return 0;
}

/*
 * This function deletes Buffer descriptor ring for RX
 */
static int mwifiex_pcie_delete_rxbd_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	int i;

	for (i = 0; i < MWIFIEX_MAX_TXRX_BD; i++) {
		if (card->rx_buf_list[i])
			dev_kfree_skb_any(card->rx_buf_list[i]);
		card->rx_buf_list[i] = NULL;
		card->rxbd_ring[i]->paddr = 0;
		card->rxbd_ring[i]->len = 0;
		card->rxbd_ring[i]->flags = 0;
		card->rxbd_ring[i] = NULL;
	}

	kfree(card->rxbd_ring_vbase);
	card->rxbd_ring_size = 0;
	card->rxbd_wrptr = 0;
	card->rxbd_rdptr = 0 | MWIFIEX_BD_FLAG_ROLLOVER_IND;
	card->rxbd_ring_vbase = NULL;

	return 0;
}

/*
 * This function creates buffer descriptor ring for Events
 */
static int mwifiex_pcie_create_evtbd_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	struct sk_buff *skb;
	int i;
	phys_addr_t *buf_pa;

	/*
	 * driver maintaines the read pointer and firmware maintaines the write
	 * pointer. The write pointer starts at 0 (zero) while the read pointer
	 * starts at zero with rollover bit set
	 */
	card->evtbd_wrptr = 0;
	card->evtbd_rdptr |= MWIFIEX_BD_FLAG_ROLLOVER_IND;

	card->evtbd_ring_size = sizeof(struct mwifiex_pcie_buf_desc) *
				MWIFIEX_MAX_EVT_BD;
	dev_dbg(adapter->dev, "info: evtbd_ring: Allocating %d bytes\n",
				card->evtbd_ring_size);
	card->evtbd_ring_vbase = kzalloc(card->evtbd_ring_size, GFP_KERNEL);
	if (!card->evtbd_ring_vbase) {
		dev_err(adapter->dev, "Unable to allocate buffer. "
				"Terminating download\n");
		return -1;
	}
	card->evtbd_ring_pbase = virt_to_phys(card->evtbd_ring_vbase);

	dev_dbg(adapter->dev, "info: CMDRSP/EVT bd_ring - base: %p, "
		       "pbase: %#x:%x, len: %#x\n", card->evtbd_ring_vbase,
		       (u32)card->evtbd_ring_pbase,
		       (u32)((u64)card->evtbd_ring_pbase >> 32),
		       card->evtbd_ring_size);

	for (i = 0; i < MWIFIEX_MAX_EVT_BD; i++) {
		card->evtbd_ring[i] = (struct mwifiex_pcie_buf_desc *)
				(card->evtbd_ring_vbase +
				(sizeof(struct mwifiex_pcie_buf_desc) * i));

		/* Allocate skb here so that firmware can DMA data from it */
		skb = dev_alloc_skb(MAX_EVENT_SIZE);
		if (!skb) {
			dev_err(adapter->dev, "Unable to allocate skb for EVENT buf.\n");
			kfree(card->evtbd_ring_vbase);
			return -ENOMEM;
		}
		buf_pa = mwifiex_update_sk_buff_pa(skb);
		skb_put(skb, MAX_EVENT_SIZE);

		dev_dbg(adapter->dev, "info: Evt ring: add new skb. base: %p, "
			       "buf_base: %p, buf_pbase: %#x:%x, "
			       "buf_len: %#x\n", skb, skb->data,
			       (u32)*buf_pa, (u32)((u64)*buf_pa >> 32),
			       skb->len);

		card->evt_buf_list[i] = skb;
		card->evtbd_ring[i]->paddr = *buf_pa;
		card->evtbd_ring[i]->len = (u16)skb->len;
		card->evtbd_ring[i]->flags = 0;
	}

	return 0;
}

/*
 * This function deletes Buffer descriptor ring for Events
 */
static int mwifiex_pcie_delete_evtbd_ring(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	int i;

	for (i = 0; i < MWIFIEX_MAX_EVT_BD; i++) {
		if (card->evt_buf_list[i])
			dev_kfree_skb_any(card->evt_buf_list[i]);
		card->evt_buf_list[i] = NULL;
		card->evtbd_ring[i]->paddr = 0;
		card->evtbd_ring[i]->len = 0;
		card->evtbd_ring[i]->flags = 0;
		card->evtbd_ring[i] = NULL;
	}

	kfree(card->evtbd_ring_vbase);
	card->evtbd_wrptr = 0;
	card->evtbd_rdptr = 0 | MWIFIEX_BD_FLAG_ROLLOVER_IND;
	card->evtbd_ring_size = 0;
	card->evtbd_ring_vbase = NULL;

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
		dev_err(adapter->dev, "Unable to allocate skb for command "
				      "response data.\n");
		return -ENOMEM;
	}
	mwifiex_update_sk_buff_pa(skb);
	skb_put(skb, MWIFIEX_UPLD_SIZE);
	card->cmdrsp_buf = skb;

	skb = NULL;
	/* Allocate memory for sending command to firmware */
	skb = dev_alloc_skb(MWIFIEX_SIZE_OF_CMD_BUFFER);
	if (!skb) {
		dev_err(adapter->dev, "Unable to allocate skb for command "
				      "data.\n");
		return -ENOMEM;
	}
	mwifiex_update_sk_buff_pa(skb);
	skb_put(skb, MWIFIEX_SIZE_OF_CMD_BUFFER);
	card->cmd_buf = skb;

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

	if (card && card->cmdrsp_buf)
		dev_kfree_skb_any(card->cmdrsp_buf);

	if (card && card->cmd_buf)
		dev_kfree_skb_any(card->cmd_buf);

	return 0;
}

/*
 * This function allocates a buffer for sleep cookie
 */
static int mwifiex_pcie_alloc_sleep_cookie_buf(struct mwifiex_adapter *adapter)
{
	struct sk_buff *skb;
	struct pcie_service_card *card = adapter->card;

	/* Allocate memory for sleep cookie */
	skb = dev_alloc_skb(sizeof(u32));
	if (!skb) {
		dev_err(adapter->dev, "Unable to allocate skb for sleep "
				      "cookie!\n");
		return -ENOMEM;
	}
	mwifiex_update_sk_buff_pa(skb);
	skb_put(skb, sizeof(u32));

	/* Init val of Sleep Cookie */
	*(u32 *)skb->data = FW_AWAKE_COOKIE;

	dev_dbg(adapter->dev, "alloc_scook: sleep cookie=0x%x\n",
				*((u32 *)skb->data));

	/* Save the sleep cookie */
	card->sleep_cookie = skb;

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

	if (card && card->sleep_cookie) {
		dev_kfree_skb_any(card->sleep_cookie);
		card->sleep_cookie = NULL;
	}

	return 0;
}

/*
 * This function sends data buffer to device
 */
static int
mwifiex_pcie_send_data(struct mwifiex_adapter *adapter, struct sk_buff *skb)
{
	struct pcie_service_card *card = adapter->card;
	u32 wrindx, rdptr;
	phys_addr_t *buf_pa;
	__le16 *tmp;

	if (!mwifiex_pcie_ok_to_access_hw(adapter))
		mwifiex_pm_wakeup_card(adapter);

	/* Read the TX ring read pointer set by firmware */
	if (mwifiex_read_reg(adapter, REG_TXBD_RDPTR, &rdptr)) {
		dev_err(adapter->dev, "SEND DATA: failed to read "
				      "REG_TXBD_RDPTR\n");
		return -1;
	}

	wrindx = card->txbd_wrptr & MWIFIEX_TXBD_MASK;

	dev_dbg(adapter->dev, "info: SEND DATA: <Rd: %#x, Wr: %#x>\n", rdptr,
				card->txbd_wrptr);
	if (((card->txbd_wrptr & MWIFIEX_TXBD_MASK) !=
			(rdptr & MWIFIEX_TXBD_MASK)) ||
	    ((card->txbd_wrptr & MWIFIEX_BD_FLAG_ROLLOVER_IND) !=
			(rdptr & MWIFIEX_BD_FLAG_ROLLOVER_IND))) {
		struct sk_buff *skb_data;
		u8 *payload;

		adapter->data_sent = true;
		skb_data = card->tx_buf_list[wrindx];
		memcpy(skb_data->data, skb->data, skb->len);
		payload = skb_data->data;
		tmp = (__le16 *)&payload[0];
		*tmp = cpu_to_le16((u16)skb->len);
		tmp = (__le16 *)&payload[2];
		*tmp = cpu_to_le16(MWIFIEX_TYPE_DATA);
		skb_put(skb_data, MWIFIEX_RX_DATA_BUF_SIZE - skb_data->len);
		skb_trim(skb_data, skb->len);
		buf_pa = MWIFIEX_SKB_PACB(skb_data);
		card->txbd_ring[wrindx]->paddr = *buf_pa;
		card->txbd_ring[wrindx]->len = (u16)skb_data->len;
		card->txbd_ring[wrindx]->flags = MWIFIEX_BD_FLAG_FIRST_DESC |
						MWIFIEX_BD_FLAG_LAST_DESC;

		if ((++card->txbd_wrptr & MWIFIEX_TXBD_MASK) ==
							MWIFIEX_MAX_TXRX_BD)
			card->txbd_wrptr = ((card->txbd_wrptr &
						MWIFIEX_BD_FLAG_ROLLOVER_IND) ^
						MWIFIEX_BD_FLAG_ROLLOVER_IND);

		/* Write the TX ring write pointer in to REG_TXBD_WRPTR */
		if (mwifiex_write_reg(adapter, REG_TXBD_WRPTR,
							card->txbd_wrptr)) {
			dev_err(adapter->dev, "SEND DATA: failed to write "
					      "REG_TXBD_WRPTR\n");
			return 0;
		}

		/* Send the TX ready interrupt */
		if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
				      CPU_INTR_DNLD_RDY)) {
			dev_err(adapter->dev, "SEND DATA: failed to assert "
					      "door-bell interrupt.\n");
			return -1;
		}
		dev_dbg(adapter->dev, "info: SEND DATA: Updated <Rd: %#x, Wr: "
				      "%#x> and sent packet to firmware "
				      "successfully\n", rdptr,
				      card->txbd_wrptr);
	} else {
		dev_dbg(adapter->dev, "info: TX Ring full, can't send anymore "
				      "packets to firmware\n");
		adapter->data_sent = true;
		/* Send the TX ready interrupt */
		if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
				      CPU_INTR_DNLD_RDY))
			dev_err(adapter->dev, "SEND DATA: failed to assert "
					      "door-bell interrupt\n");
		return -EBUSY;
	}

	return 0;
}

/*
 * This function handles received buffer ring and
 * dispatches packets to upper
 */
static int mwifiex_pcie_process_recv_data(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	u32 wrptr, rd_index;
	int ret = 0;
	struct sk_buff *skb_tmp = NULL;

	/* Read the RX ring Write pointer set by firmware */
	if (mwifiex_read_reg(adapter, REG_RXBD_WRPTR, &wrptr)) {
		dev_err(adapter->dev, "RECV DATA: failed to read "
				      "REG_TXBD_RDPTR\n");
		ret = -1;
		goto done;
	}

	while (((wrptr & MWIFIEX_RXBD_MASK) !=
		(card->rxbd_rdptr & MWIFIEX_RXBD_MASK)) ||
	       ((wrptr & MWIFIEX_BD_FLAG_ROLLOVER_IND) ==
		(card->rxbd_rdptr & MWIFIEX_BD_FLAG_ROLLOVER_IND))) {
		struct sk_buff *skb_data;
		u16 rx_len;

		rd_index = card->rxbd_rdptr & MWIFIEX_RXBD_MASK;
		skb_data = card->rx_buf_list[rd_index];

		/* Get data length from interface header -
		   first byte is len, second byte is type */
		rx_len = *((u16 *)skb_data->data);
		dev_dbg(adapter->dev, "info: RECV DATA: Rd=%#x, Wr=%#x, "
				"Len=%d\n", card->rxbd_rdptr, wrptr, rx_len);
		skb_tmp = dev_alloc_skb(rx_len);
		if (!skb_tmp) {
			dev_dbg(adapter->dev, "info: Failed to alloc skb "
					      "for RX\n");
			ret = -EBUSY;
			goto done;
		}

		skb_put(skb_tmp, rx_len);

		memcpy(skb_tmp->data, skb_data->data + INTF_HEADER_LEN, rx_len);
		if ((++card->rxbd_rdptr & MWIFIEX_RXBD_MASK) ==
							MWIFIEX_MAX_TXRX_BD) {
			card->rxbd_rdptr = ((card->rxbd_rdptr &
					     MWIFIEX_BD_FLAG_ROLLOVER_IND) ^
					    MWIFIEX_BD_FLAG_ROLLOVER_IND);
		}
		dev_dbg(adapter->dev, "info: RECV DATA: <Rd: %#x, Wr: %#x>\n",
				card->rxbd_rdptr, wrptr);

		/* Write the RX ring read pointer in to REG_RXBD_RDPTR */
		if (mwifiex_write_reg(adapter, REG_RXBD_RDPTR,
				      card->rxbd_rdptr)) {
			dev_err(adapter->dev, "RECV DATA: failed to "
					      "write REG_RXBD_RDPTR\n");
			ret = -1;
			goto done;
		}

		/* Read the RX ring Write pointer set by firmware */
		if (mwifiex_read_reg(adapter, REG_RXBD_WRPTR, &wrptr)) {
			dev_err(adapter->dev, "RECV DATA: failed to read "
					      "REG_TXBD_RDPTR\n");
			ret = -1;
			goto done;
		}
		dev_dbg(adapter->dev, "info: RECV DATA: Received packet from "
				      "firmware successfully\n");
		mwifiex_handle_rx_packet(adapter, skb_tmp);
	}

done:
	if (ret && skb_tmp)
		dev_kfree_skb_any(skb_tmp);
	return ret;
}

/*
 * This function downloads the boot command to device
 */
static int
mwifiex_pcie_send_boot_cmd(struct mwifiex_adapter *adapter, struct sk_buff *skb)
{
	phys_addr_t *buf_pa = MWIFIEX_SKB_PACB(skb);

	if (!(skb->data && skb->len && *buf_pa)) {
		dev_err(adapter->dev, "Invalid parameter in %s <%p, %#x:%x, "
				"%x>\n", __func__, skb->data, skb->len,
				(u32)*buf_pa, (u32)((u64)*buf_pa >> 32));
		return -1;
	}

	/* Write the lower 32bits of the physical address to scratch
	 * register 0 */
	if (mwifiex_write_reg(adapter, PCIE_SCRATCH_0_REG, (u32)*buf_pa)) {
		dev_err(adapter->dev, "%s: failed to write download command "
				      "to boot code.\n", __func__);
		return -1;
	}

	/* Write the upper 32bits of the physical address to scratch
	 * register 1 */
	if (mwifiex_write_reg(adapter, PCIE_SCRATCH_1_REG,
			      (u32)((u64)*buf_pa >> 32))) {
		dev_err(adapter->dev, "%s: failed to write download command "
				      "to boot code.\n", __func__);
		return -1;
	}

	/* Write the command length to scratch register 2 */
	if (mwifiex_write_reg(adapter, PCIE_SCRATCH_2_REG, skb->len)) {
		dev_err(adapter->dev, "%s: failed to write command length to "
				      "scratch register 2\n", __func__);
		return -1;
	}

	/* Ring the door bell */
	if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
			      CPU_INTR_DOOR_BELL)) {
		dev_err(adapter->dev, "%s: failed to assert door-bell "
				      "interrupt.\n", __func__);
		return -1;
	}

	return 0;
}

/*
 * This function downloads commands to the device
 */
static int
mwifiex_pcie_send_cmd(struct mwifiex_adapter *adapter, struct sk_buff *skb)
{
	struct pcie_service_card *card = adapter->card;
	int ret = 0;
	phys_addr_t *cmd_buf_pa;
	phys_addr_t *cmdrsp_buf_pa;

	if (!(skb->data && skb->len)) {
		dev_err(adapter->dev, "Invalid parameter in %s <%p, %#x>\n",
				      __func__, skb->data, skb->len);
		return -1;
	}

	/* Make sure a command response buffer is available */
	if (!card->cmdrsp_buf) {
		dev_err(adapter->dev, "No response buffer available, send "
				      "command failed\n");
		return -EBUSY;
	}

	/* Make sure a command buffer is available */
	if (!card->cmd_buf) {
		dev_err(adapter->dev, "Command buffer not available\n");
		return -EBUSY;
	}

	adapter->cmd_sent = true;
	/* Copy the given skb in to DMA accessable shared buffer */
	skb_put(card->cmd_buf, MWIFIEX_SIZE_OF_CMD_BUFFER - card->cmd_buf->len);
	skb_trim(card->cmd_buf, skb->len);
	memcpy(card->cmd_buf->data, skb->data, skb->len);

	/* To send a command, the driver will:
		1. Write the 64bit physical address of the data buffer to
		   SCRATCH1 + SCRATCH0
		2. Ring the door bell (i.e. set the door bell interrupt)

		In response to door bell interrupt, the firmware will perform
		the DMA of the command packet (first header to obtain the total
		length and then rest of the command).
	*/

	if (card->cmdrsp_buf) {
		cmdrsp_buf_pa = MWIFIEX_SKB_PACB(card->cmdrsp_buf);
		/* Write the lower 32bits of the cmdrsp buffer physical
		   address */
		if (mwifiex_write_reg(adapter, REG_CMDRSP_ADDR_LO,
					(u32)*cmdrsp_buf_pa)) {
			dev_err(adapter->dev, "Failed to write download command to boot code.\n");
			ret = -1;
			goto done;
		}
		/* Write the upper 32bits of the cmdrsp buffer physical
		   address */
		if (mwifiex_write_reg(adapter, REG_CMDRSP_ADDR_HI,
					(u32)((u64)*cmdrsp_buf_pa >> 32))) {
			dev_err(adapter->dev, "Failed to write download command"
					      " to boot code.\n");
			ret = -1;
			goto done;
		}
	}

	cmd_buf_pa = MWIFIEX_SKB_PACB(card->cmd_buf);
	/* Write the lower 32bits of the physical address to REG_CMD_ADDR_LO */
	if (mwifiex_write_reg(adapter, REG_CMD_ADDR_LO,
				(u32)*cmd_buf_pa)) {
		dev_err(adapter->dev, "Failed to write download command "
				      "to boot code.\n");
		ret = -1;
		goto done;
	}
	/* Write the upper 32bits of the physical address to REG_CMD_ADDR_HI */
	if (mwifiex_write_reg(adapter, REG_CMD_ADDR_HI,
				(u32)((u64)*cmd_buf_pa >> 32))) {
		dev_err(adapter->dev, "Failed to write download command "
				      "to boot code.\n");
		ret = -1;
		goto done;
	}

	/* Write the command length to REG_CMD_SIZE */
	if (mwifiex_write_reg(adapter, REG_CMD_SIZE,
				card->cmd_buf->len)) {
		dev_err(adapter->dev, "Failed to write command length to "
				      "REG_CMD_SIZE\n");
		ret = -1;
		goto done;
	}

	/* Ring the door bell */
	if (mwifiex_write_reg(adapter, PCIE_CPU_INT_EVENT,
			      CPU_INTR_DOOR_BELL)) {
		dev_err(adapter->dev, "Failed to assert door-bell "
				      "interrupt.\n");
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
	int count = 0;

	dev_dbg(adapter->dev, "info: Rx CMD Response\n");

	if (!adapter->curr_cmd) {
		skb_pull(card->cmdrsp_buf, INTF_HEADER_LEN);
		if (adapter->ps_state == PS_STATE_SLEEP_CFM) {
			mwifiex_process_sleep_confirm_resp(adapter,
					card->cmdrsp_buf->data,
					card->cmdrsp_buf->len);
			while (mwifiex_pcie_ok_to_access_hw(adapter) &&
							(count++ < 10))
				udelay(50);
		} else {
			dev_err(adapter->dev, "There is no command but "
					      "got cmdrsp\n");
		}
		memcpy(adapter->upld_buf, card->cmdrsp_buf->data,
		       min_t(u32, MWIFIEX_SIZE_OF_CMD_BUFFER,
			     card->cmdrsp_buf->len));
		skb_push(card->cmdrsp_buf, INTF_HEADER_LEN);
	} else if (mwifiex_pcie_ok_to_access_hw(adapter)) {
		skb_pull(card->cmdrsp_buf, INTF_HEADER_LEN);
		adapter->curr_cmd->resp_skb = card->cmdrsp_buf;
		adapter->cmd_resp_received = true;
		/* Take the pointer and set it to CMD node and will
		   return in the response complete callback */
		card->cmdrsp_buf = NULL;

		/* Clear the cmd-rsp buffer address in scratch registers. This
		   will prevent firmware from writing to the same response
		   buffer again. */
		if (mwifiex_write_reg(adapter, REG_CMDRSP_ADDR_LO, 0)) {
			dev_err(adapter->dev, "cmd_done: failed to clear "
					      "cmd_rsp address.\n");
			return -1;
		}
		/* Write the upper 32bits of the cmdrsp buffer physical
		   address */
		if (mwifiex_write_reg(adapter, REG_CMDRSP_ADDR_HI, 0)) {
			dev_err(adapter->dev, "cmd_done: failed to clear "
					      "cmd_rsp address.\n");
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
	}

	return 0;
}

/*
 * This function handles firmware event ready interrupt
 */
static int mwifiex_pcie_process_event_ready(struct mwifiex_adapter *adapter)
{
	struct pcie_service_card *card = adapter->card;
	u32 rdptr = card->evtbd_rdptr & MWIFIEX_EVTBD_MASK;
	u32 wrptr, event;

	if (adapter->event_received) {
		dev_dbg(adapter->dev, "info: Event being processed, "\
				"do not process this interrupt just yet\n");
		return 0;
	}

	if (rdptr >= MWIFIEX_MAX_EVT_BD) {
		dev_dbg(adapter->dev, "info: Invalid read pointer...\n");
		return -1;
	}

	/* Read the event ring write pointer set by firmware */
	if (mwifiex_read_reg(adapter, REG_EVTBD_WRPTR, &wrptr)) {
		dev_err(adapter->dev, "EventReady: failed to read REG_EVTBD_WRPTR\n");
		return -1;
	}

	dev_dbg(adapter->dev, "info: EventReady: Initial <Rd: 0x%x, Wr: 0x%x>",
			card->evtbd_rdptr, wrptr);
	if (((wrptr & MWIFIEX_EVTBD_MASK) !=
	     (card->evtbd_rdptr & MWIFIEX_EVTBD_MASK)) ||
	    ((wrptr & MWIFIEX_BD_FLAG_ROLLOVER_IND) ==
	     (card->evtbd_rdptr & MWIFIEX_BD_FLAG_ROLLOVER_IND))) {
		struct sk_buff *skb_cmd;
		__le16 data_len = 0;
		u16 evt_len;

		dev_dbg(adapter->dev, "info: Read Index: %d\n", rdptr);
		skb_cmd = card->evt_buf_list[rdptr];
		/* Take the pointer and set it to event pointer in adapter
		   and will return back after event handling callback */
		card->evt_buf_list[rdptr] = NULL;
		card->evtbd_ring[rdptr]->paddr = 0;
		card->evtbd_ring[rdptr]->len = 0;
		card->evtbd_ring[rdptr]->flags = 0;

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
	int ret = 0;
	u32 rdptr = card->evtbd_rdptr & MWIFIEX_EVTBD_MASK;
	u32 wrptr;
	phys_addr_t *buf_pa;

	if (!skb)
		return 0;

	if (rdptr >= MWIFIEX_MAX_EVT_BD)
		dev_err(adapter->dev, "event_complete: Invalid rdptr 0x%x\n",
					rdptr);

	/* Read the event ring write pointer set by firmware */
	if (mwifiex_read_reg(adapter, REG_EVTBD_WRPTR, &wrptr)) {
		dev_err(adapter->dev, "event_complete: failed to read REG_EVTBD_WRPTR\n");
		ret = -1;
		goto done;
	}

	if (!card->evt_buf_list[rdptr]) {
		skb_push(skb, INTF_HEADER_LEN);
		card->evt_buf_list[rdptr] = skb;
		buf_pa = MWIFIEX_SKB_PACB(skb);
		card->evtbd_ring[rdptr]->paddr = *buf_pa;
		card->evtbd_ring[rdptr]->len = (u16)skb->len;
		card->evtbd_ring[rdptr]->flags = 0;
		skb = NULL;
	} else {
		dev_dbg(adapter->dev, "info: ERROR: Buffer is still valid at "
				      "index %d, <%p, %p>\n", rdptr,
				      card->evt_buf_list[rdptr], skb);
	}

	if ((++card->evtbd_rdptr & MWIFIEX_EVTBD_MASK) == MWIFIEX_MAX_EVT_BD) {
		card->evtbd_rdptr = ((card->evtbd_rdptr &
					MWIFIEX_BD_FLAG_ROLLOVER_IND) ^
					MWIFIEX_BD_FLAG_ROLLOVER_IND);
	}

	dev_dbg(adapter->dev, "info: Updated <Rd: 0x%x, Wr: 0x%x>",
				card->evtbd_rdptr, wrptr);

	/* Write the event ring read pointer in to REG_EVTBD_RDPTR */
	if (mwifiex_write_reg(adapter, REG_EVTBD_RDPTR, card->evtbd_rdptr)) {
		dev_err(adapter->dev, "event_complete: failed to read REG_EVTBD_RDPTR\n");
		ret = -1;
		goto done;
	}

done:
	/* Free the buffer for failure case */
	if (ret && skb)
		dev_kfree_skb_any(skb);

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

	if (!adapter) {
		pr_err("adapter structure is not valid\n");
		return -1;
	}

	if (!firmware || !firmware_len) {
		dev_err(adapter->dev, "No firmware image found! "
				      "Terminating download\n");
		return -1;
	}

	dev_dbg(adapter->dev, "info: Downloading FW image (%d bytes)\n",
				firmware_len);

	if (mwifiex_pcie_disable_host_int(adapter)) {
		dev_err(adapter->dev, "%s: Disabling interrupts"
				      " failed.\n", __func__);
		return -1;
	}

	skb = dev_alloc_skb(MWIFIEX_UPLD_SIZE);
	if (!skb) {
		ret = -ENOMEM;
		goto done;
	}
	mwifiex_update_sk_buff_pa(skb);

	/* Perform firmware data transfer */
	do {
		u32 ireg_intr = 0;

		/* More data? */
		if (offset >= firmware_len)
			break;

		for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
			ret = mwifiex_read_reg(adapter, PCIE_SCRATCH_2_REG,
					       &len);
			if (ret) {
				dev_warn(adapter->dev, "Failed reading length from boot code\n");
				goto done;
			}
			if (len)
				break;
			udelay(10);
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
					      "helper: len = 0x%04X, txlen = "
					      "%d\n", len, txlen);
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

			tx_blocks =
				(txlen + MWIFIEX_PCIE_BLOCK_SIZE_FW_DNLD - 1) /
				MWIFIEX_PCIE_BLOCK_SIZE_FW_DNLD;

			/* Copy payload to buffer */
			memmove(skb->data, &firmware[offset], txlen);
		}

		skb_put(skb, MWIFIEX_UPLD_SIZE - skb->len);
		skb_trim(skb, tx_blocks * MWIFIEX_PCIE_BLOCK_SIZE_FW_DNLD);

		/* Send the boot command to device */
		if (mwifiex_pcie_send_boot_cmd(adapter, skb)) {
			dev_err(adapter->dev, "Failed to send firmware download command\n");
			ret = -1;
			goto done;
		}
		/* Wait for the command done interrupt */
		do {
			if (mwifiex_read_reg(adapter, PCIE_CPU_INT_STATUS,
					     &ireg_intr)) {
				dev_err(adapter->dev, "%s: Failed to read "
						      "interrupt status during "
						      "fw dnld.\n", __func__);
				ret = -1;
				goto done;
			}
		} while ((ireg_intr & CPU_INTR_DOOR_BELL) ==
			 CPU_INTR_DOOR_BELL);
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
	u32 tries;

	/* Mask spurios interrupts */
	if (mwifiex_write_reg(adapter, PCIE_HOST_INT_STATUS_MASK,
				HOST_INTR_MASK)) {
		dev_warn(adapter->dev, "Write register failed\n");
		return -1;
	}

	dev_dbg(adapter->dev, "Setting driver ready signature\n");
	if (mwifiex_write_reg(adapter, REG_DRV_READY, FIRMWARE_READY_PCIE)) {
		dev_err(adapter->dev, "Failed to write driver ready signature\n");
		return -1;
	}

	/* Wait for firmware initialization event */
	for (tries = 0; tries < poll_num; tries++) {
		if (mwifiex_read_reg(adapter, PCIE_SCRATCH_3_REG,
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
			mdelay(100);
			ret = -1;
		}
	}

	if (ret) {
		if (mwifiex_read_reg(adapter, PCIE_SCRATCH_3_REG,
				     &winner_status))
			ret = -1;
		else if (!winner_status) {
			dev_err(adapter->dev, "PCI-E is the winner\n");
			adapter->winner = 1;
			ret = -1;
		} else {
			dev_err(adapter->dev, "PCI-E is not the winner <%#x, %d>, exit download\n",
					ret, adapter->winner);
			ret = 0;
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

		if (pcie_ireg & HOST_INTR_CMD_DONE) {
			if ((adapter->ps_state == PS_STATE_SLEEP_CFM) ||
			    (adapter->ps_state == PS_STATE_SLEEP)) {
				mwifiex_pcie_enable_host_int(adapter);
				if (mwifiex_write_reg(adapter,
						PCIE_CPU_INT_EVENT,
						CPU_INTR_SLEEP_CFM_DONE)) {
					dev_warn(adapter->dev, "Write register"
							       " failed\n");
					return;

				}
			}
		} else if (!adapter->pps_uapsd_mode &&
			   adapter->ps_state == PS_STATE_SLEEP) {
				/* Potentially for PCIe we could get other
				 * interrupts like shared. Don't change power
				 * state until cookie is set */
				if (mwifiex_pcie_ok_to_access_hw(adapter))
					adapter->ps_state = PS_STATE_AWAKE;
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

	card = (struct pcie_service_card *) pci_get_drvdata(pdev);
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
	u32 pcie_ireg = 0;
	unsigned long flags;

	spin_lock_irqsave(&adapter->int_lock, flags);
	/* Clear out unused interrupts */
	adapter->int_status &= HOST_INTR_MASK;
	spin_unlock_irqrestore(&adapter->int_lock, flags);

	while (adapter->int_status & HOST_INTR_MASK) {
		if (adapter->int_status & HOST_INTR_DNLD_DONE) {
			adapter->int_status &= ~HOST_INTR_DNLD_DONE;
			if (adapter->data_sent) {
				dev_dbg(adapter->dev, "info: DATA sent Interrupt\n");
				adapter->data_sent = false;
			}
		}
		if (adapter->int_status & HOST_INTR_UPLD_RDY) {
			adapter->int_status &= ~HOST_INTR_UPLD_RDY;
			dev_dbg(adapter->dev, "info: Rx DATA\n");
			ret = mwifiex_pcie_process_recv_data(adapter);
			if (ret)
				return ret;
		}
		if (adapter->int_status & HOST_INTR_EVENT_RDY) {
			adapter->int_status &= ~HOST_INTR_EVENT_RDY;
			dev_dbg(adapter->dev, "info: Rx EVENT\n");
			ret = mwifiex_pcie_process_event_ready(adapter);
			if (ret)
				return ret;
		}

		if (adapter->int_status & HOST_INTR_CMD_DONE) {
			adapter->int_status &= ~HOST_INTR_CMD_DONE;
			if (adapter->cmd_sent) {
				dev_dbg(adapter->dev, "info: CMD sent Interrupt\n");
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
				dev_warn(adapter->dev, "Read register failed\n");
				return -1;
			}

			if ((pcie_ireg != 0xFFFFFFFF) && (pcie_ireg)) {
				if (mwifiex_write_reg(adapter,
					PCIE_HOST_INT_STATUS, ~pcie_ireg)) {
					dev_warn(adapter->dev, "Write register"
							       " failed\n");
					return -1;
				}
				adapter->int_status |= pcie_ireg;
				adapter->int_status &= HOST_INTR_MASK;
			}

		}
	}
	dev_dbg(adapter->dev, "info: cmd_sent=%d data_sent=%d\n",
	       adapter->cmd_sent, adapter->data_sent);
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
	if (!adapter || !skb) {
		dev_err(adapter->dev, "Invalid parameter in %s <%p, %p>\n",
				__func__, adapter, skb);
		return -1;
	}

	if (type == MWIFIEX_TYPE_DATA)
		return mwifiex_pcie_send_data(adapter, skb);
	else if (type == MWIFIEX_TYPE_CMD)
		return mwifiex_pcie_send_cmd(adapter, skb);

	return 0;
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
		goto err_iomap2;
	}

	dev_dbg(adapter->dev, "PCI memory map Virt0: %p PCI memory map Virt2: "
			      "%p\n", card->pci_mmap, card->pci_mmap1);

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
	ret = mwifiex_pcie_alloc_sleep_cookie_buf(adapter);
	if (ret)
		goto err_alloc_cookie;

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

	mwifiex_pcie_delete_sleep_cookie_buf(adapter);
	mwifiex_pcie_delete_cmdrsp_buf(adapter);
	mwifiex_pcie_delete_evtbd_ring(adapter);
	mwifiex_pcie_delete_rxbd_ring(adapter);
	mwifiex_pcie_delete_txbd_ring(adapter);
	card->cmdrsp_buf = NULL;

	dev_dbg(adapter->dev, "Clearing driver ready signature\n");
	if (user_rmmod) {
		if (mwifiex_write_reg(adapter, REG_DRV_READY, 0x00000000))
			dev_err(adapter->dev, "Failed to write driver not-ready signature\n");
	}

	if (pdev) {
		pci_iounmap(pdev, card->pci_mmap);
		pci_iounmap(pdev, card->pci_mmap1);

		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
	}
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
	strcpy(adapter->fw_name, PCIE8766_DEFAULT_FW_NAME);

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

	if (card) {
		dev_dbg(adapter->dev, "%s(): calling free_irq()\n", __func__);
		free_irq(card->dev->irq, card->dev);
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

	pr_debug("Marvell 8766 PCIe Driver\n");

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
MODULE_FIRMWARE("mrvl/pcie8766_uapsta.bin");
