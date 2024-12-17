// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Bluetooth support for Intel PCIe devices
 *
 *  Copyright (C) 2024  Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <linux/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btintel.h"
#include "btintel_pcie.h"

#define VERSION "0.1"

#define BTINTEL_PCI_DEVICE(dev, subdev)	\
	.vendor = PCI_VENDOR_ID_INTEL,	\
	.device = (dev),		\
	.subvendor = PCI_ANY_ID,	\
	.subdevice = (subdev),		\
	.driver_data = 0

#define POLL_INTERVAL_US	10

/* Intel Bluetooth PCIe device id table */
static const struct pci_device_id btintel_pcie_table[] = {
	{ BTINTEL_PCI_DEVICE(0xA876, PCI_ANY_ID) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, btintel_pcie_table);

/* Intel PCIe uses 4 bytes of HCI type instead of 1 byte BT SIG HCI type */
#define BTINTEL_PCIE_HCI_TYPE_LEN	4
#define BTINTEL_PCIE_HCI_CMD_PKT	0x00000001
#define BTINTEL_PCIE_HCI_ACL_PKT	0x00000002
#define BTINTEL_PCIE_HCI_SCO_PKT	0x00000003
#define BTINTEL_PCIE_HCI_EVT_PKT	0x00000004
#define BTINTEL_PCIE_HCI_ISO_PKT	0x00000005

/* Alive interrupt context */
enum {
	BTINTEL_PCIE_ROM,
	BTINTEL_PCIE_FW_DL,
	BTINTEL_PCIE_HCI_RESET,
	BTINTEL_PCIE_INTEL_HCI_RESET1,
	BTINTEL_PCIE_INTEL_HCI_RESET2,
	BTINTEL_PCIE_D0,
	BTINTEL_PCIE_D3
};

static inline void ipc_print_ia_ring(struct hci_dev *hdev, struct ia *ia,
				     u16 queue_num)
{
	bt_dev_dbg(hdev, "IA: %s: tr-h:%02u  tr-t:%02u  cr-h:%02u  cr-t:%02u",
		   queue_num == BTINTEL_PCIE_TXQ_NUM ? "TXQ" : "RXQ",
		   ia->tr_hia[queue_num], ia->tr_tia[queue_num],
		   ia->cr_hia[queue_num], ia->cr_tia[queue_num]);
}

static inline void ipc_print_urbd1(struct hci_dev *hdev, struct urbd1 *urbd1,
				   u16 index)
{
	bt_dev_dbg(hdev, "RXQ:urbd1(%u) frbd_tag:%u status: 0x%x fixed:0x%x",
		   index, urbd1->frbd_tag, urbd1->status, urbd1->fixed);
}

static struct btintel_pcie_data *btintel_pcie_get_data(struct msix_entry *entry)
{
	u8 queue = entry->entry;
	struct msix_entry *entries = entry - queue;

	return container_of(entries, struct btintel_pcie_data, msix_entries[0]);
}

/* Set the doorbell for TXQ to notify the device that @index (actually index-1)
 * of the TFD is updated and ready to transmit.
 */
static void btintel_pcie_set_tx_db(struct btintel_pcie_data *data, u16 index)
{
	u32 val;

	val = index;
	val |= (BTINTEL_PCIE_TX_DB_VEC << 16);

	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_HBUS_TARG_WRPTR, val);
}

/* Copy the data to next(@tfd_index) data buffer and update the TFD(transfer
 * descriptor) with the data length and the DMA address of the data buffer.
 */
static void btintel_pcie_prepare_tx(struct txq *txq, u16 tfd_index,
				    struct sk_buff *skb)
{
	struct data_buf *buf;
	struct tfd *tfd;

	tfd = &txq->tfds[tfd_index];
	memset(tfd, 0, sizeof(*tfd));

	buf = &txq->bufs[tfd_index];

	tfd->size = skb->len;
	tfd->addr = buf->data_p_addr;

	/* Copy the outgoing data to DMA buffer */
	memcpy(buf->data, skb->data, tfd->size);
}

static int btintel_pcie_send_sync(struct btintel_pcie_data *data,
				  struct sk_buff *skb)
{
	int ret;
	u16 tfd_index;
	struct txq *txq = &data->txq;

	tfd_index = data->ia.tr_hia[BTINTEL_PCIE_TXQ_NUM];

	if (tfd_index > txq->count)
		return -ERANGE;

	/* Prepare for TX. It updates the TFD with the length of data and
	 * address of the DMA buffer, and copy the data to the DMA buffer
	 */
	btintel_pcie_prepare_tx(txq, tfd_index, skb);

	tfd_index = (tfd_index + 1) % txq->count;
	data->ia.tr_hia[BTINTEL_PCIE_TXQ_NUM] = tfd_index;

	/* Arm wait event condition */
	data->tx_wait_done = false;

	/* Set the doorbell to notify the device */
	btintel_pcie_set_tx_db(data, tfd_index);

	/* Wait for the complete interrupt - URBD0 */
	ret = wait_event_timeout(data->tx_wait_q, data->tx_wait_done,
				 msecs_to_jiffies(BTINTEL_PCIE_TX_WAIT_TIMEOUT_MS));
	if (!ret)
		return -ETIME;

	return 0;
}

/* Set the doorbell for RXQ to notify the device that @index (actually index-1)
 * is available to receive the data
 */
static void btintel_pcie_set_rx_db(struct btintel_pcie_data *data, u16 index)
{
	u32 val;

	val = index;
	val |= (BTINTEL_PCIE_RX_DB_VEC << 16);

	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_HBUS_TARG_WRPTR, val);
}

/* Update the FRBD (free buffer descriptor) with the @frbd_index and the
 * DMA address of the free buffer.
 */
static void btintel_pcie_prepare_rx(struct rxq *rxq, u16 frbd_index)
{
	struct data_buf *buf;
	struct frbd *frbd;

	/* Get the buffer of the FRBD for DMA */
	buf = &rxq->bufs[frbd_index];

	frbd = &rxq->frbds[frbd_index];
	memset(frbd, 0, sizeof(*frbd));

	/* Update FRBD */
	frbd->tag = frbd_index;
	frbd->addr = buf->data_p_addr;
}

static int btintel_pcie_submit_rx(struct btintel_pcie_data *data)
{
	u16 frbd_index;
	struct rxq *rxq = &data->rxq;

	frbd_index = data->ia.tr_hia[BTINTEL_PCIE_RXQ_NUM];

	if (frbd_index > rxq->count)
		return -ERANGE;

	/* Prepare for RX submit. It updates the FRBD with the address of DMA
	 * buffer
	 */
	btintel_pcie_prepare_rx(rxq, frbd_index);

	frbd_index = (frbd_index + 1) % rxq->count;
	data->ia.tr_hia[BTINTEL_PCIE_RXQ_NUM] = frbd_index;
	ipc_print_ia_ring(data->hdev, &data->ia, BTINTEL_PCIE_RXQ_NUM);

	/* Set the doorbell to notify the device */
	btintel_pcie_set_rx_db(data, frbd_index);

	return 0;
}

static int btintel_pcie_start_rx(struct btintel_pcie_data *data)
{
	int i, ret;

	for (i = 0; i < BTINTEL_PCIE_RX_MAX_QUEUE; i++) {
		ret = btintel_pcie_submit_rx(data);
		if (ret)
			return ret;
	}

	return 0;
}

static void btintel_pcie_reset_ia(struct btintel_pcie_data *data)
{
	memset(data->ia.tr_hia, 0, sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES);
	memset(data->ia.tr_tia, 0, sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES);
	memset(data->ia.cr_hia, 0, sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES);
	memset(data->ia.cr_tia, 0, sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES);
}

static int btintel_pcie_reset_bt(struct btintel_pcie_data *data)
{
	u32 reg;
	int retry = 3;

	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG);

	reg &= ~(BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_ENA |
			BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_INIT |
			BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_INIT);
	reg |= BTINTEL_PCIE_CSR_FUNC_CTRL_BUS_MASTER_DISCON;

	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG, reg);

	do {
		reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG);
		if (reg & BTINTEL_PCIE_CSR_FUNC_CTRL_BUS_MASTER_STS)
			break;
		usleep_range(10000, 12000);

	} while (--retry > 0);
	usleep_range(10000, 12000);

	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG);

	reg &= ~(BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_ENA |
			BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_INIT |
			BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_INIT);
	reg |= BTINTEL_PCIE_CSR_FUNC_CTRL_SW_RESET;
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG, reg);
	usleep_range(10000, 12000);

	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG);
	bt_dev_dbg(data->hdev, "csr register after reset: 0x%8.8x", reg);

	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_BOOT_STAGE_REG);

	/* If shared hardware reset is success then boot stage register shall be
	 * set to 0
	 */
	return reg == 0 ? 0 : -ENODEV;
}

/* This function enables BT function by setting BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_INIT bit in
 * BTINTEL_PCIE_CSR_FUNC_CTRL_REG register and wait for MSI-X with
 * BTINTEL_PCIE_MSIX_HW_INT_CAUSES_GP0.
 * Then the host reads firmware version from BTINTEL_CSR_F2D_MBX and the boot stage
 * from BTINTEL_PCIE_CSR_BOOT_STAGE_REG.
 */
static int btintel_pcie_enable_bt(struct btintel_pcie_data *data)
{
	int err;
	u32 reg;

	data->gp0_received = false;

	/* Update the DMA address of CI struct to CSR */
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_CI_ADDR_LSB_REG,
			      data->ci_p_addr & 0xffffffff);
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_CI_ADDR_MSB_REG,
			      (u64)data->ci_p_addr >> 32);

	/* Reset the cached value of boot stage. it is updated by the MSI-X
	 * gp0 interrupt handler.
	 */
	data->boot_stage_cache = 0x0;

	/* Set MAC_INIT bit to start primary bootloader */
	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG);
	reg &= ~(BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_INIT |
			BTINTEL_PCIE_CSR_FUNC_CTRL_BUS_MASTER_DISCON |
			BTINTEL_PCIE_CSR_FUNC_CTRL_SW_RESET);
	reg |= (BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_ENA |
			BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_INIT);

	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG, reg);

	/* MAC is ready. Enable BT FUNC */
	btintel_pcie_set_reg_bits(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG,
				  BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_INIT);

	btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG);

	/* wait for interrupt from the device after booting up to primary
	 * bootloader.
	 */
	data->alive_intr_ctxt = BTINTEL_PCIE_ROM;
	err = wait_event_timeout(data->gp0_wait_q, data->gp0_received,
				 msecs_to_jiffies(BTINTEL_DEFAULT_INTR_TIMEOUT_MS));
	if (!err)
		return -ETIME;

	/* Check cached boot stage is BTINTEL_PCIE_CSR_BOOT_STAGE_ROM(BIT(0)) */
	if (~data->boot_stage_cache & BTINTEL_PCIE_CSR_BOOT_STAGE_ROM)
		return -ENODEV;

	return 0;
}

/* BIT(0) - ROM, BIT(1) - IML and BIT(3) - OP
 * Sometimes during firmware image switching from ROM to IML or IML to OP image,
 * the previous image bit is not cleared by firmware when alive interrupt is
 * received. Driver needs to take care of these sticky bits when deciding the
 * current image running on controller.
 * Ex: 0x10 and 0x11 - both represents that controller is running IML
 */
static inline bool btintel_pcie_in_rom(struct btintel_pcie_data *data)
{
	return data->boot_stage_cache & BTINTEL_PCIE_CSR_BOOT_STAGE_ROM &&
		!(data->boot_stage_cache & BTINTEL_PCIE_CSR_BOOT_STAGE_IML) &&
		!(data->boot_stage_cache & BTINTEL_PCIE_CSR_BOOT_STAGE_OPFW);
}

static inline bool btintel_pcie_in_op(struct btintel_pcie_data *data)
{
	return data->boot_stage_cache & BTINTEL_PCIE_CSR_BOOT_STAGE_OPFW;
}

static inline bool btintel_pcie_in_iml(struct btintel_pcie_data *data)
{
	return data->boot_stage_cache & BTINTEL_PCIE_CSR_BOOT_STAGE_IML &&
		!(data->boot_stage_cache & BTINTEL_PCIE_CSR_BOOT_STAGE_OPFW);
}

static inline bool btintel_pcie_in_d3(struct btintel_pcie_data *data)
{
	return data->boot_stage_cache & BTINTEL_PCIE_CSR_BOOT_STAGE_D3_STATE_READY;
}

static inline bool btintel_pcie_in_d0(struct btintel_pcie_data *data)
{
	return !(data->boot_stage_cache & BTINTEL_PCIE_CSR_BOOT_STAGE_D3_STATE_READY);
}

static void btintel_pcie_wr_sleep_cntrl(struct btintel_pcie_data *data,
					u32 dxstate)
{
	bt_dev_dbg(data->hdev, "writing sleep_ctl_reg: 0x%8.8x", dxstate);
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_IPC_SLEEP_CTL_REG, dxstate);
}

static inline char *btintel_pcie_alivectxt_state2str(u32 alive_intr_ctxt)
{
	switch (alive_intr_ctxt) {
	case BTINTEL_PCIE_ROM:
		return "rom";
	case BTINTEL_PCIE_FW_DL:
		return "fw_dl";
	case BTINTEL_PCIE_D0:
		return "d0";
	case BTINTEL_PCIE_D3:
		return "d3";
	case BTINTEL_PCIE_HCI_RESET:
		return "hci_reset";
	case BTINTEL_PCIE_INTEL_HCI_RESET1:
		return "intel_reset1";
	case BTINTEL_PCIE_INTEL_HCI_RESET2:
		return "intel_reset2";
	default:
		return "unknown";
	}
}

/* This function handles the MSI-X interrupt for gp0 cause (bit 0 in
 * BTINTEL_PCIE_CSR_MSIX_HW_INT_CAUSES) which is sent for boot stage and image response.
 */
static void btintel_pcie_msix_gp0_handler(struct btintel_pcie_data *data)
{
	bool submit_rx, signal_waitq;
	u32 reg, old_ctxt;

	/* This interrupt is for three different causes and it is not easy to
	 * know what causes the interrupt. So, it compares each register value
	 * with cached value and update it before it wake up the queue.
	 */
	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_BOOT_STAGE_REG);
	if (reg != data->boot_stage_cache)
		data->boot_stage_cache = reg;

	bt_dev_dbg(data->hdev, "Alive context: %s old_boot_stage: 0x%8.8x new_boot_stage: 0x%8.8x",
		   btintel_pcie_alivectxt_state2str(data->alive_intr_ctxt),
		   data->boot_stage_cache, reg);
	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_IMG_RESPONSE_REG);
	if (reg != data->img_resp_cache)
		data->img_resp_cache = reg;

	data->gp0_received = true;

	old_ctxt = data->alive_intr_ctxt;
	submit_rx = false;
	signal_waitq = false;

	switch (data->alive_intr_ctxt) {
	case BTINTEL_PCIE_ROM:
		data->alive_intr_ctxt = BTINTEL_PCIE_FW_DL;
		signal_waitq = true;
		break;
	case BTINTEL_PCIE_FW_DL:
		/* Error case is already handled. Ideally control shall not
		 * reach here
		 */
		break;
	case BTINTEL_PCIE_INTEL_HCI_RESET1:
		if (btintel_pcie_in_op(data)) {
			submit_rx = true;
			break;
		}

		if (btintel_pcie_in_iml(data)) {
			submit_rx = true;
			data->alive_intr_ctxt = BTINTEL_PCIE_FW_DL;
			break;
		}
		break;
	case BTINTEL_PCIE_INTEL_HCI_RESET2:
		if (btintel_test_and_clear_flag(data->hdev, INTEL_WAIT_FOR_D0)) {
			btintel_wake_up_flag(data->hdev, INTEL_WAIT_FOR_D0);
			data->alive_intr_ctxt = BTINTEL_PCIE_D0;
		}
		break;
	case BTINTEL_PCIE_D0:
		if (btintel_pcie_in_d3(data)) {
			data->alive_intr_ctxt = BTINTEL_PCIE_D3;
			signal_waitq = true;
			break;
		}
		break;
	case BTINTEL_PCIE_D3:
		if (btintel_pcie_in_d0(data)) {
			data->alive_intr_ctxt = BTINTEL_PCIE_D0;
			submit_rx = true;
			signal_waitq = true;
			break;
		}
		break;
	case BTINTEL_PCIE_HCI_RESET:
		data->alive_intr_ctxt = BTINTEL_PCIE_D0;
		submit_rx = true;
		signal_waitq = true;
		break;
	default:
		bt_dev_err(data->hdev, "Unknown state: 0x%2.2x",
			   data->alive_intr_ctxt);
		break;
	}

	if (submit_rx) {
		btintel_pcie_reset_ia(data);
		btintel_pcie_start_rx(data);
	}

	if (signal_waitq) {
		bt_dev_dbg(data->hdev, "wake up gp0 wait_q");
		wake_up(&data->gp0_wait_q);
	}

	if (old_ctxt != data->alive_intr_ctxt)
		bt_dev_dbg(data->hdev, "alive context changed: %s  ->  %s",
			   btintel_pcie_alivectxt_state2str(old_ctxt),
			   btintel_pcie_alivectxt_state2str(data->alive_intr_ctxt));
}

/* This function handles the MSX-X interrupt for rx queue 0 which is for TX
 */
static void btintel_pcie_msix_tx_handle(struct btintel_pcie_data *data)
{
	u16 cr_tia, cr_hia;
	struct txq *txq;
	struct urbd0 *urbd0;

	cr_tia = data->ia.cr_tia[BTINTEL_PCIE_TXQ_NUM];
	cr_hia = data->ia.cr_hia[BTINTEL_PCIE_TXQ_NUM];

	if (cr_tia == cr_hia)
		return;

	txq = &data->txq;

	while (cr_tia != cr_hia) {
		data->tx_wait_done = true;
		wake_up(&data->tx_wait_q);

		urbd0 = &txq->urbd0s[cr_tia];

		if (urbd0->tfd_index > txq->count)
			return;

		cr_tia = (cr_tia + 1) % txq->count;
		data->ia.cr_tia[BTINTEL_PCIE_TXQ_NUM] = cr_tia;
		ipc_print_ia_ring(data->hdev, &data->ia, BTINTEL_PCIE_TXQ_NUM);
	}
}

static int btintel_pcie_recv_event(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_event_hdr *hdr = (void *)skb->data;
	const char diagnostics_hdr[] = { 0x87, 0x80, 0x03 };
	struct btintel_pcie_data *data = hci_get_drvdata(hdev);

	if (skb->len > HCI_EVENT_HDR_SIZE && hdr->evt == 0xff &&
	    hdr->plen > 0) {
		const void *ptr = skb->data + HCI_EVENT_HDR_SIZE + 1;
		unsigned int len = skb->len - HCI_EVENT_HDR_SIZE - 1;

		if (btintel_test_flag(hdev, INTEL_BOOTLOADER)) {
			switch (skb->data[2]) {
			case 0x02:
				/* When switching to the operational firmware
				 * the device sends a vendor specific event
				 * indicating that the bootup completed.
				 */
				btintel_bootup(hdev, ptr, len);

				/* If bootup event is from operational image,
				 * driver needs to write sleep control register to
				 * move into D0 state
				 */
				if (btintel_pcie_in_op(data)) {
					btintel_pcie_wr_sleep_cntrl(data, BTINTEL_PCIE_STATE_D0);
					data->alive_intr_ctxt = BTINTEL_PCIE_INTEL_HCI_RESET2;
					kfree_skb(skb);
					return 0;
				}

				if (btintel_pcie_in_iml(data)) {
					/* In case of IML, there is no concept
					 * of D0 transition. Just mimic as if
					 * IML moved to D0 by clearing INTEL_WAIT_FOR_D0
					 * bit and waking up the task waiting on
					 * INTEL_WAIT_FOR_D0. This is required
					 * as intel_boot() is common function for
					 * both IML and OP image loading.
					 */
					if (btintel_test_and_clear_flag(data->hdev,
									INTEL_WAIT_FOR_D0))
						btintel_wake_up_flag(data->hdev,
								     INTEL_WAIT_FOR_D0);
				}
				kfree_skb(skb);
				return 0;
			case 0x06:
				/* When the firmware loading completes the
				 * device sends out a vendor specific event
				 * indicating the result of the firmware
				 * loading.
				 */
				btintel_secure_send_result(hdev, ptr, len);
				kfree_skb(skb);
				return 0;
			}
		}

		/* Handle all diagnostics events separately. May still call
		 * hci_recv_frame.
		 */
		if (len >= sizeof(diagnostics_hdr) &&
		    memcmp(&skb->data[2], diagnostics_hdr,
			   sizeof(diagnostics_hdr)) == 0) {
			return btintel_diagnostics(hdev, skb);
		}

		/* This is a debug event that comes from IML and OP image when it
		 * starts execution. There is no need pass this event to stack.
		 */
		if (skb->data[2] == 0x97)
			return 0;
	}

	return hci_recv_frame(hdev, skb);
}
/* Process the received rx data
 * It check the frame header to identify the data type and create skb
 * and calling HCI API
 */
static int btintel_pcie_recv_frame(struct btintel_pcie_data *data,
				       struct sk_buff *skb)
{
	int ret;
	u8 pkt_type;
	u16 plen;
	u32 pcie_pkt_type;
	struct sk_buff *new_skb;
	void *pdata;
	struct hci_dev *hdev = data->hdev;

	spin_lock(&data->hci_rx_lock);

	/* The first 4 bytes indicates the Intel PCIe specific packet type */
	pdata = skb_pull_data(skb, BTINTEL_PCIE_HCI_TYPE_LEN);
	if (!pdata) {
		bt_dev_err(hdev, "Corrupted packet received");
		ret = -EILSEQ;
		goto exit_error;
	}

	pcie_pkt_type = get_unaligned_le32(pdata);

	switch (pcie_pkt_type) {
	case BTINTEL_PCIE_HCI_ACL_PKT:
		if (skb->len >= HCI_ACL_HDR_SIZE) {
			plen = HCI_ACL_HDR_SIZE + __le16_to_cpu(hci_acl_hdr(skb)->dlen);
			pkt_type = HCI_ACLDATA_PKT;
		} else {
			bt_dev_err(hdev, "ACL packet is too short");
			ret = -EILSEQ;
			goto exit_error;
		}
		break;

	case BTINTEL_PCIE_HCI_SCO_PKT:
		if (skb->len >= HCI_SCO_HDR_SIZE) {
			plen = HCI_SCO_HDR_SIZE + hci_sco_hdr(skb)->dlen;
			pkt_type = HCI_SCODATA_PKT;
		} else {
			bt_dev_err(hdev, "SCO packet is too short");
			ret = -EILSEQ;
			goto exit_error;
		}
		break;

	case BTINTEL_PCIE_HCI_EVT_PKT:
		if (skb->len >= HCI_EVENT_HDR_SIZE) {
			plen = HCI_EVENT_HDR_SIZE + hci_event_hdr(skb)->plen;
			pkt_type = HCI_EVENT_PKT;
		} else {
			bt_dev_err(hdev, "Event packet is too short");
			ret = -EILSEQ;
			goto exit_error;
		}
		break;

	case BTINTEL_PCIE_HCI_ISO_PKT:
		if (skb->len >= HCI_ISO_HDR_SIZE) {
			plen = HCI_ISO_HDR_SIZE + __le16_to_cpu(hci_iso_hdr(skb)->dlen);
			pkt_type = HCI_ISODATA_PKT;
		} else {
			bt_dev_err(hdev, "ISO packet is too short");
			ret = -EILSEQ;
			goto exit_error;
		}
		break;

	default:
		bt_dev_err(hdev, "Invalid packet type received: 0x%4.4x",
			   pcie_pkt_type);
		ret = -EINVAL;
		goto exit_error;
	}

	if (skb->len < plen) {
		bt_dev_err(hdev, "Received corrupted packet. type: 0x%2.2x",
			   pkt_type);
		ret = -EILSEQ;
		goto exit_error;
	}

	bt_dev_dbg(hdev, "pkt_type: 0x%2.2x len: %u", pkt_type, plen);

	new_skb = bt_skb_alloc(plen, GFP_ATOMIC);
	if (!new_skb) {
		bt_dev_err(hdev, "Failed to allocate memory for skb of len: %u",
			   skb->len);
		ret = -ENOMEM;
		goto exit_error;
	}

	hci_skb_pkt_type(new_skb) = pkt_type;
	skb_put_data(new_skb, skb->data, plen);
	hdev->stat.byte_rx += plen;

	if (pcie_pkt_type == BTINTEL_PCIE_HCI_EVT_PKT)
		ret = btintel_pcie_recv_event(hdev, new_skb);
	else
		ret = hci_recv_frame(hdev, new_skb);

exit_error:
	if (ret)
		hdev->stat.err_rx++;

	spin_unlock(&data->hci_rx_lock);

	return ret;
}

static void btintel_pcie_rx_work(struct work_struct *work)
{
	struct btintel_pcie_data *data = container_of(work,
					struct btintel_pcie_data, rx_work);
	struct sk_buff *skb;
	int err;
	struct hci_dev *hdev = data->hdev;

	/* Process the sk_buf in queue and send to the HCI layer */
	while ((skb = skb_dequeue(&data->rx_skb_q))) {
		err = btintel_pcie_recv_frame(data, skb);
		if (err)
			bt_dev_err(hdev, "Failed to send received frame: %d",
				   err);
		kfree_skb(skb);
	}
}

/* create sk_buff with data and save it to queue and start RX work */
static int btintel_pcie_submit_rx_work(struct btintel_pcie_data *data, u8 status,
				       void *buf)
{
	int ret, len;
	struct rfh_hdr *rfh_hdr;
	struct sk_buff *skb;

	rfh_hdr = buf;

	len = rfh_hdr->packet_len;
	if (len <= 0) {
		ret = -EINVAL;
		goto resubmit;
	}

	/* Remove RFH header */
	buf += sizeof(*rfh_hdr);

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		goto resubmit;

	skb_put_data(skb, buf, len);
	skb_queue_tail(&data->rx_skb_q, skb);
	queue_work(data->workqueue, &data->rx_work);

resubmit:
	ret = btintel_pcie_submit_rx(data);

	return ret;
}

/* Handles the MSI-X interrupt for rx queue 1 which is for RX */
static void btintel_pcie_msix_rx_handle(struct btintel_pcie_data *data)
{
	u16 cr_hia, cr_tia;
	struct rxq *rxq;
	struct urbd1 *urbd1;
	struct data_buf *buf;
	int ret;
	struct hci_dev *hdev = data->hdev;

	cr_hia = data->ia.cr_hia[BTINTEL_PCIE_RXQ_NUM];
	cr_tia = data->ia.cr_tia[BTINTEL_PCIE_RXQ_NUM];

	bt_dev_dbg(hdev, "RXQ: cr_hia: %u  cr_tia: %u", cr_hia, cr_tia);

	/* Check CR_TIA and CR_HIA for change */
	if (cr_tia == cr_hia) {
		bt_dev_warn(hdev, "RXQ: no new CD found");
		return;
	}

	rxq = &data->rxq;

	/* The firmware sends multiple CD in a single MSI-X and it needs to
	 * process all received CDs in this interrupt.
	 */
	while (cr_tia != cr_hia) {
		urbd1 = &rxq->urbd1s[cr_tia];
		ipc_print_urbd1(data->hdev, urbd1, cr_tia);

		buf = &rxq->bufs[urbd1->frbd_tag];
		if (!buf) {
			bt_dev_err(hdev, "RXQ: failed to get the DMA buffer for %d",
				   urbd1->frbd_tag);
			return;
		}

		ret = btintel_pcie_submit_rx_work(data, urbd1->status,
						  buf->data);
		if (ret) {
			bt_dev_err(hdev, "RXQ: failed to submit rx request");
			return;
		}

		cr_tia = (cr_tia + 1) % rxq->count;
		data->ia.cr_tia[BTINTEL_PCIE_RXQ_NUM] = cr_tia;
		ipc_print_ia_ring(data->hdev, &data->ia, BTINTEL_PCIE_RXQ_NUM);
	}
}

static irqreturn_t btintel_pcie_msix_isr(int irq, void *data)
{
	return IRQ_WAKE_THREAD;
}

static irqreturn_t btintel_pcie_irq_msix_handler(int irq, void *dev_id)
{
	struct msix_entry *entry = dev_id;
	struct btintel_pcie_data *data = btintel_pcie_get_data(entry);
	u32 intr_fh, intr_hw;

	spin_lock(&data->irq_lock);
	intr_fh = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_MSIX_FH_INT_CAUSES);
	intr_hw = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_MSIX_HW_INT_CAUSES);

	/* Clear causes registers to avoid being handling the same cause */
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_MSIX_FH_INT_CAUSES, intr_fh);
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_MSIX_HW_INT_CAUSES, intr_hw);
	spin_unlock(&data->irq_lock);

	if (unlikely(!(intr_fh | intr_hw))) {
		/* Ignore interrupt, inta == 0 */
		return IRQ_NONE;
	}

	/* This interrupt is triggered by the firmware after updating
	 * boot_stage register and image_response register
	 */
	if (intr_hw & BTINTEL_PCIE_MSIX_HW_INT_CAUSES_GP0)
		btintel_pcie_msix_gp0_handler(data);

	/* For TX */
	if (intr_fh & BTINTEL_PCIE_MSIX_FH_INT_CAUSES_0)
		btintel_pcie_msix_tx_handle(data);

	/* For RX */
	if (intr_fh & BTINTEL_PCIE_MSIX_FH_INT_CAUSES_1)
		btintel_pcie_msix_rx_handle(data);

	/*
	 * Before sending the interrupt the HW disables it to prevent a nested
	 * interrupt. This is done by writing 1 to the corresponding bit in
	 * the mask register. After handling the interrupt, it should be
	 * re-enabled by clearing this bit. This register is defined as write 1
	 * clear (W1C) register, meaning that it's cleared by writing 1
	 * to the bit.
	 */
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_MSIX_AUTOMASK_ST,
			      BIT(entry->entry));

	return IRQ_HANDLED;
}

/* This function requests the irq for MSI-X and registers the handlers per irq.
 * Currently, it requests only 1 irq for all interrupt causes.
 */
static int btintel_pcie_setup_irq(struct btintel_pcie_data *data)
{
	int err;
	int num_irqs, i;

	for (i = 0; i < BTINTEL_PCIE_MSIX_VEC_MAX; i++)
		data->msix_entries[i].entry = i;

	num_irqs = pci_alloc_irq_vectors(data->pdev, BTINTEL_PCIE_MSIX_VEC_MIN,
					 BTINTEL_PCIE_MSIX_VEC_MAX, PCI_IRQ_MSIX);
	if (num_irqs < 0)
		return num_irqs;

	data->alloc_vecs = num_irqs;
	data->msix_enabled = 1;
	data->def_irq = 0;

	/* setup irq handler */
	for (i = 0; i < data->alloc_vecs; i++) {
		struct msix_entry *msix_entry;

		msix_entry = &data->msix_entries[i];
		msix_entry->vector = pci_irq_vector(data->pdev, i);

		err = devm_request_threaded_irq(&data->pdev->dev,
						msix_entry->vector,
						btintel_pcie_msix_isr,
						btintel_pcie_irq_msix_handler,
						IRQF_SHARED,
						KBUILD_MODNAME,
						msix_entry);
		if (err) {
			pci_free_irq_vectors(data->pdev);
			data->alloc_vecs = 0;
			return err;
		}
	}
	return 0;
}

struct btintel_pcie_causes_list {
	u32 cause;
	u32 mask_reg;
	u8 cause_num;
};

static struct btintel_pcie_causes_list causes_list[] = {
	{ BTINTEL_PCIE_MSIX_FH_INT_CAUSES_0,	BTINTEL_PCIE_CSR_MSIX_FH_INT_MASK,	0x00 },
	{ BTINTEL_PCIE_MSIX_FH_INT_CAUSES_1,	BTINTEL_PCIE_CSR_MSIX_FH_INT_MASK,	0x01 },
	{ BTINTEL_PCIE_MSIX_HW_INT_CAUSES_GP0, BTINTEL_PCIE_CSR_MSIX_HW_INT_MASK,	0x20 },
};

/* This function configures the interrupt masks for both HW_INT_CAUSES and
 * FH_INT_CAUSES which are meaningful to us.
 *
 * After resetting BT function via PCIE FLR or FUNC_CTRL reset, the driver
 * need to call this function again to configure since the masks
 * are reset to 0xFFFFFFFF after reset.
 */
static void btintel_pcie_config_msix(struct btintel_pcie_data *data)
{
	int i;
	int val = data->def_irq | BTINTEL_PCIE_MSIX_NON_AUTO_CLEAR_CAUSE;

	/* Set Non Auto Clear Cause */
	for (i = 0; i < ARRAY_SIZE(causes_list); i++) {
		btintel_pcie_wr_reg8(data,
				     BTINTEL_PCIE_CSR_MSIX_IVAR(causes_list[i].cause_num),
				     val);
		btintel_pcie_clr_reg_bits(data,
					  causes_list[i].mask_reg,
					  causes_list[i].cause);
	}

	/* Save the initial interrupt mask */
	data->fh_init_mask = ~btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_MSIX_FH_INT_MASK);
	data->hw_init_mask = ~btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_MSIX_HW_INT_MASK);
}

static int btintel_pcie_config_pcie(struct pci_dev *pdev,
				    struct btintel_pcie_data *data)
{
	int err;

	err = pcim_enable_device(pdev);
	if (err)
		return err;

	pci_set_master(pdev);

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err)
			return err;
	}

	data->base_addr = pcim_iomap_region(pdev, 0, KBUILD_MODNAME);
	if (IS_ERR(data->base_addr))
		return PTR_ERR(data->base_addr);

	err = btintel_pcie_setup_irq(data);
	if (err)
		return err;

	/* Configure MSI-X with causes list */
	btintel_pcie_config_msix(data);

	return 0;
}

static void btintel_pcie_init_ci(struct btintel_pcie_data *data,
				 struct ctx_info *ci)
{
	ci->version = 0x1;
	ci->size = sizeof(*ci);
	ci->config = 0x0000;
	ci->addr_cr_hia = data->ia.cr_hia_p_addr;
	ci->addr_tr_tia = data->ia.tr_tia_p_addr;
	ci->addr_cr_tia = data->ia.cr_tia_p_addr;
	ci->addr_tr_hia = data->ia.tr_hia_p_addr;
	ci->num_cr_ia = BTINTEL_PCIE_NUM_QUEUES;
	ci->num_tr_ia = BTINTEL_PCIE_NUM_QUEUES;
	ci->addr_urbdq0 = data->txq.urbd0s_p_addr;
	ci->addr_tfdq = data->txq.tfds_p_addr;
	ci->num_tfdq = data->txq.count;
	ci->num_urbdq0 = data->txq.count;
	ci->tfdq_db_vec = BTINTEL_PCIE_TXQ_NUM;
	ci->urbdq0_db_vec = BTINTEL_PCIE_TXQ_NUM;
	ci->rbd_size = BTINTEL_PCIE_RBD_SIZE_4K;
	ci->addr_frbdq = data->rxq.frbds_p_addr;
	ci->num_frbdq = data->rxq.count;
	ci->frbdq_db_vec = BTINTEL_PCIE_RXQ_NUM;
	ci->addr_urbdq1 = data->rxq.urbd1s_p_addr;
	ci->num_urbdq1 = data->rxq.count;
	ci->urbdq_db_vec = BTINTEL_PCIE_RXQ_NUM;
}

static void btintel_pcie_free_txq_bufs(struct btintel_pcie_data *data,
				       struct txq *txq)
{
	/* Free data buffers first */
	dma_free_coherent(&data->pdev->dev, txq->count * BTINTEL_PCIE_BUFFER_SIZE,
			  txq->buf_v_addr, txq->buf_p_addr);
	kfree(txq->bufs);
}

static int btintel_pcie_setup_txq_bufs(struct btintel_pcie_data *data,
				       struct txq *txq)
{
	int i;
	struct data_buf *buf;

	/* Allocate the same number of buffers as the descriptor */
	txq->bufs = kmalloc_array(txq->count, sizeof(*buf), GFP_KERNEL);
	if (!txq->bufs)
		return -ENOMEM;

	/* Allocate full chunk of data buffer for DMA first and do indexing and
	 * initialization next, so it can be freed easily
	 */
	txq->buf_v_addr = dma_alloc_coherent(&data->pdev->dev,
					     txq->count * BTINTEL_PCIE_BUFFER_SIZE,
					     &txq->buf_p_addr,
					     GFP_KERNEL | __GFP_NOWARN);
	if (!txq->buf_v_addr) {
		kfree(txq->bufs);
		return -ENOMEM;
	}

	/* Setup the allocated DMA buffer to bufs. Each data_buf should
	 * have virtual address and physical address
	 */
	for (i = 0; i < txq->count; i++) {
		buf = &txq->bufs[i];
		buf->data_p_addr = txq->buf_p_addr + (i * BTINTEL_PCIE_BUFFER_SIZE);
		buf->data = txq->buf_v_addr + (i * BTINTEL_PCIE_BUFFER_SIZE);
	}

	return 0;
}

static void btintel_pcie_free_rxq_bufs(struct btintel_pcie_data *data,
				       struct rxq *rxq)
{
	/* Free data buffers first */
	dma_free_coherent(&data->pdev->dev, rxq->count * BTINTEL_PCIE_BUFFER_SIZE,
			  rxq->buf_v_addr, rxq->buf_p_addr);
	kfree(rxq->bufs);
}

static int btintel_pcie_setup_rxq_bufs(struct btintel_pcie_data *data,
				       struct rxq *rxq)
{
	int i;
	struct data_buf *buf;

	/* Allocate the same number of buffers as the descriptor */
	rxq->bufs = kmalloc_array(rxq->count, sizeof(*buf), GFP_KERNEL);
	if (!rxq->bufs)
		return -ENOMEM;

	/* Allocate full chunk of data buffer for DMA first and do indexing and
	 * initialization next, so it can be freed easily
	 */
	rxq->buf_v_addr = dma_alloc_coherent(&data->pdev->dev,
					     rxq->count * BTINTEL_PCIE_BUFFER_SIZE,
					     &rxq->buf_p_addr,
					     GFP_KERNEL | __GFP_NOWARN);
	if (!rxq->buf_v_addr) {
		kfree(rxq->bufs);
		return -ENOMEM;
	}

	/* Setup the allocated DMA buffer to bufs. Each data_buf should
	 * have virtual address and physical address
	 */
	for (i = 0; i < rxq->count; i++) {
		buf = &rxq->bufs[i];
		buf->data_p_addr = rxq->buf_p_addr + (i * BTINTEL_PCIE_BUFFER_SIZE);
		buf->data = rxq->buf_v_addr + (i * BTINTEL_PCIE_BUFFER_SIZE);
	}

	return 0;
}

static void btintel_pcie_setup_ia(struct btintel_pcie_data *data,
				  dma_addr_t p_addr, void *v_addr,
				  struct ia *ia)
{
	/* TR Head Index Array */
	ia->tr_hia_p_addr = p_addr;
	ia->tr_hia = v_addr;

	/* TR Tail Index Array */
	ia->tr_tia_p_addr = p_addr + sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES;
	ia->tr_tia = v_addr + sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES;

	/* CR Head index Array */
	ia->cr_hia_p_addr = p_addr + (sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES * 2);
	ia->cr_hia = v_addr + (sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES * 2);

	/* CR Tail Index Array */
	ia->cr_tia_p_addr = p_addr + (sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES * 3);
	ia->cr_tia = v_addr + (sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES * 3);
}

static void btintel_pcie_free(struct btintel_pcie_data *data)
{
	btintel_pcie_free_rxq_bufs(data, &data->rxq);
	btintel_pcie_free_txq_bufs(data, &data->txq);

	dma_pool_free(data->dma_pool, data->dma_v_addr, data->dma_p_addr);
	dma_pool_destroy(data->dma_pool);
}

/* Allocate tx and rx queues, any related data structures and buffers.
 */
static int btintel_pcie_alloc(struct btintel_pcie_data *data)
{
	int err = 0;
	size_t total;
	dma_addr_t p_addr;
	void *v_addr;

	/* Allocate the chunk of DMA memory for descriptors, index array, and
	 * context information, instead of allocating individually.
	 * The DMA memory for data buffer is allocated while setting up the
	 * each queue.
	 *
	 * Total size is sum of the following
	 *  + size of TFD * Number of descriptors in queue
	 *  + size of URBD0 * Number of descriptors in queue
	 *  + size of FRBD * Number of descriptors in queue
	 *  + size of URBD1 * Number of descriptors in queue
	 *  + size of index * Number of queues(2) * type of index array(4)
	 *  + size of context information
	 */
	total = (sizeof(struct tfd) + sizeof(struct urbd0) + sizeof(struct frbd)
		+ sizeof(struct urbd1)) * BTINTEL_DESCS_COUNT;

	/* Add the sum of size of index array and size of ci struct */
	total += (sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES * 4) + sizeof(struct ctx_info);

	/* Allocate DMA Pool */
	data->dma_pool = dma_pool_create(KBUILD_MODNAME, &data->pdev->dev,
					 total, BTINTEL_PCIE_DMA_POOL_ALIGNMENT, 0);
	if (!data->dma_pool) {
		err = -ENOMEM;
		goto exit_error;
	}

	v_addr = dma_pool_zalloc(data->dma_pool, GFP_KERNEL | __GFP_NOWARN,
				 &p_addr);
	if (!v_addr) {
		dma_pool_destroy(data->dma_pool);
		err = -ENOMEM;
		goto exit_error;
	}

	data->dma_p_addr = p_addr;
	data->dma_v_addr = v_addr;

	/* Setup descriptor count */
	data->txq.count = BTINTEL_DESCS_COUNT;
	data->rxq.count = BTINTEL_DESCS_COUNT;

	/* Setup tfds */
	data->txq.tfds_p_addr = p_addr;
	data->txq.tfds = v_addr;

	p_addr += (sizeof(struct tfd) * BTINTEL_DESCS_COUNT);
	v_addr += (sizeof(struct tfd) * BTINTEL_DESCS_COUNT);

	/* Setup urbd0 */
	data->txq.urbd0s_p_addr = p_addr;
	data->txq.urbd0s = v_addr;

	p_addr += (sizeof(struct urbd0) * BTINTEL_DESCS_COUNT);
	v_addr += (sizeof(struct urbd0) * BTINTEL_DESCS_COUNT);

	/* Setup FRBD*/
	data->rxq.frbds_p_addr = p_addr;
	data->rxq.frbds = v_addr;

	p_addr += (sizeof(struct frbd) * BTINTEL_DESCS_COUNT);
	v_addr += (sizeof(struct frbd) * BTINTEL_DESCS_COUNT);

	/* Setup urbd1 */
	data->rxq.urbd1s_p_addr = p_addr;
	data->rxq.urbd1s = v_addr;

	p_addr += (sizeof(struct urbd1) * BTINTEL_DESCS_COUNT);
	v_addr += (sizeof(struct urbd1) * BTINTEL_DESCS_COUNT);

	/* Setup data buffers for txq */
	err = btintel_pcie_setup_txq_bufs(data, &data->txq);
	if (err)
		goto exit_error_pool;

	/* Setup data buffers for rxq */
	err = btintel_pcie_setup_rxq_bufs(data, &data->rxq);
	if (err)
		goto exit_error_txq;

	/* Setup Index Array */
	btintel_pcie_setup_ia(data, p_addr, v_addr, &data->ia);

	/* Setup Context Information */
	p_addr += sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES * 4;
	v_addr += sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES * 4;

	data->ci = v_addr;
	data->ci_p_addr = p_addr;

	/* Initialize the CI */
	btintel_pcie_init_ci(data, data->ci);

	return 0;

exit_error_txq:
	btintel_pcie_free_txq_bufs(data, &data->txq);
exit_error_pool:
	dma_pool_free(data->dma_pool, data->dma_v_addr, data->dma_p_addr);
	dma_pool_destroy(data->dma_pool);
exit_error:
	return err;
}

static int btintel_pcie_open(struct hci_dev *hdev)
{
	bt_dev_dbg(hdev, "");

	return 0;
}

static int btintel_pcie_close(struct hci_dev *hdev)
{
	bt_dev_dbg(hdev, "");

	return 0;
}

static int btintel_pcie_inject_cmd_complete(struct hci_dev *hdev, __u16 opcode)
{
	struct sk_buff *skb;
	struct hci_event_hdr *hdr;
	struct hci_ev_cmd_complete *evt;

	skb = bt_skb_alloc(sizeof(*hdr) + sizeof(*evt) + 1, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = (struct hci_event_hdr *)skb_put(skb, sizeof(*hdr));
	hdr->evt = HCI_EV_CMD_COMPLETE;
	hdr->plen = sizeof(*evt) + 1;

	evt = (struct hci_ev_cmd_complete *)skb_put(skb, sizeof(*evt));
	evt->ncmd = 0x01;
	evt->opcode = cpu_to_le16(opcode);

	*(u8 *)skb_put(skb, 1) = 0x00;

	hci_skb_pkt_type(skb) = HCI_EVENT_PKT;

	return hci_recv_frame(hdev, skb);
}

static int btintel_pcie_send_frame(struct hci_dev *hdev,
				       struct sk_buff *skb)
{
	struct btintel_pcie_data *data = hci_get_drvdata(hdev);
	struct hci_command_hdr *cmd;
	__u16 opcode = ~0;
	int ret;
	u32 type;
	u32 old_ctxt;

	/* Due to the fw limitation, the type header of the packet should be
	 * 4 bytes unlike 1 byte for UART. In UART, the firmware can read
	 * the first byte to get the packet type and redirect the rest of data
	 * packet to the right handler.
	 *
	 * But for PCIe, THF(Transfer Flow Handler) fetches the 4 bytes of data
	 * from DMA memory and by the time it reads the first 4 bytes, it has
	 * already consumed some part of packet. Thus the packet type indicator
	 * for iBT PCIe is 4 bytes.
	 *
	 * Luckily, when HCI core creates the skb, it allocates 8 bytes of
	 * head room for profile and driver use, and before sending the data
	 * to the device, append the iBT PCIe packet type in the front.
	 */
	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		type = BTINTEL_PCIE_HCI_CMD_PKT;
		cmd = (void *)skb->data;
		opcode = le16_to_cpu(cmd->opcode);
		if (btintel_test_flag(hdev, INTEL_BOOTLOADER)) {
			struct hci_command_hdr *cmd = (void *)skb->data;
			__u16 opcode = le16_to_cpu(cmd->opcode);

			/* When the 0xfc01 command is issued to boot into
			 * the operational firmware, it will actually not
			 * send a command complete event. To keep the flow
			 * control working inject that event here.
			 */
			if (opcode == 0xfc01)
				btintel_pcie_inject_cmd_complete(hdev, opcode);
		}
		hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		type = BTINTEL_PCIE_HCI_ACL_PKT;
		hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		type = BTINTEL_PCIE_HCI_SCO_PKT;
		hdev->stat.sco_tx++;
		break;
	case HCI_ISODATA_PKT:
		type = BTINTEL_PCIE_HCI_ISO_PKT;
		break;
	default:
		bt_dev_err(hdev, "Unknown HCI packet type");
		return -EILSEQ;
	}
	memcpy(skb_push(skb, BTINTEL_PCIE_HCI_TYPE_LEN), &type,
	       BTINTEL_PCIE_HCI_TYPE_LEN);

	ret = btintel_pcie_send_sync(data, skb);
	if (ret) {
		hdev->stat.err_tx++;
		bt_dev_err(hdev, "Failed to send frame (%d)", ret);
		goto exit_error;
	}

	if (type == BTINTEL_PCIE_HCI_CMD_PKT &&
	    (opcode == HCI_OP_RESET || opcode == 0xfc01)) {
		old_ctxt = data->alive_intr_ctxt;
		data->alive_intr_ctxt =
			(opcode == 0xfc01 ? BTINTEL_PCIE_INTEL_HCI_RESET1 :
				BTINTEL_PCIE_HCI_RESET);
		bt_dev_dbg(data->hdev, "sent cmd: 0x%4.4x alive context changed: %s  ->  %s",
			   opcode, btintel_pcie_alivectxt_state2str(old_ctxt),
			   btintel_pcie_alivectxt_state2str(data->alive_intr_ctxt));
		if (opcode == HCI_OP_RESET) {
			data->gp0_received = false;
			ret = wait_event_timeout(data->gp0_wait_q,
						 data->gp0_received,
						 msecs_to_jiffies(BTINTEL_DEFAULT_INTR_TIMEOUT_MS));
			if (!ret) {
				hdev->stat.err_tx++;
				bt_dev_err(hdev, "No alive interrupt received for %s",
					   btintel_pcie_alivectxt_state2str(data->alive_intr_ctxt));
				ret = -ETIME;
				goto exit_error;
			}
		}
	}
	hdev->stat.byte_tx += skb->len;
	kfree_skb(skb);

exit_error:
	return ret;
}

static void btintel_pcie_release_hdev(struct btintel_pcie_data *data)
{
	struct hci_dev *hdev;

	hdev = data->hdev;
	hci_unregister_dev(hdev);
	hci_free_dev(hdev);
	data->hdev = NULL;
}

static int btintel_pcie_setup_internal(struct hci_dev *hdev)
{
	const u8 param[1] = { 0xFF };
	struct intel_version_tlv ver_tlv;
	struct sk_buff *skb;
	int err;

	BT_DBG("%s", hdev->name);

	skb = __hci_cmd_sync(hdev, 0xfc05, 1, param, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Reading Intel version command failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	/* Check the status */
	if (skb->data[0]) {
		bt_dev_err(hdev, "Intel Read Version command failed (%02x)",
			   skb->data[0]);
		err = -EIO;
		goto exit_error;
	}

	/* Apply the common HCI quirks for Intel device */
	set_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks);
	set_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks);
	set_bit(HCI_QUIRK_NON_PERSISTENT_DIAG, &hdev->quirks);

	/* Set up the quality report callback for Intel devices */
	hdev->set_quality_report = btintel_set_quality_report;

	memset(&ver_tlv, 0, sizeof(ver_tlv));
	/* For TLV type device, parse the tlv data */
	err = btintel_parse_version_tlv(hdev, &ver_tlv, skb);
	if (err) {
		bt_dev_err(hdev, "Failed to parse TLV version information");
		goto exit_error;
	}

	switch (INTEL_HW_PLATFORM(ver_tlv.cnvi_bt)) {
	case 0x37:
		break;
	default:
		bt_dev_err(hdev, "Unsupported Intel hardware platform (0x%2x)",
			   INTEL_HW_PLATFORM(ver_tlv.cnvi_bt));
		err = -EINVAL;
		goto exit_error;
	}

	/* Check for supported iBT hardware variants of this firmware
	 * loading method.
	 *
	 * This check has been put in place to ensure correct forward
	 * compatibility options when newer hardware variants come
	 * along.
	 */
	switch (INTEL_HW_VARIANT(ver_tlv.cnvi_bt)) {
	case 0x1e:	/* BzrI */
		/* Display version information of TLV type */
		btintel_version_info_tlv(hdev, &ver_tlv);

		/* Apply the device specific HCI quirks for TLV based devices
		 *
		 * All TLV based devices support WBS
		 */
		set_bit(HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED, &hdev->quirks);

		/* Setup MSFT Extension support */
		btintel_set_msft_opcode(hdev,
					INTEL_HW_VARIANT(ver_tlv.cnvi_bt));

		err = btintel_bootloader_setup_tlv(hdev, &ver_tlv);
		if (err)
			goto exit_error;
		break;
	default:
		bt_dev_err(hdev, "Unsupported Intel hw variant (%u)",
			   INTEL_HW_VARIANT(ver_tlv.cnvi_bt));
		err = -EINVAL;
		goto exit_error;
		break;
	}

	btintel_print_fseq_info(hdev);
exit_error:
	kfree_skb(skb);

	return err;
}

static int btintel_pcie_setup(struct hci_dev *hdev)
{
	int err, fw_dl_retry = 0;
	struct btintel_pcie_data *data = hci_get_drvdata(hdev);

	while ((err = btintel_pcie_setup_internal(hdev)) && fw_dl_retry++ < 1) {
		bt_dev_err(hdev, "Firmware download retry count: %d",
			   fw_dl_retry);
		err = btintel_pcie_reset_bt(data);
		if (err) {
			bt_dev_err(hdev, "Failed to do shr reset: %d", err);
			break;
		}
		usleep_range(10000, 12000);
		btintel_pcie_reset_ia(data);
		btintel_pcie_config_msix(data);
		err = btintel_pcie_enable_bt(data);
		if (err) {
			bt_dev_err(hdev, "Failed to enable hardware: %d", err);
			break;
		}
		btintel_pcie_start_rx(data);
	}
	return err;
}

static int btintel_pcie_setup_hdev(struct btintel_pcie_data *data)
{
	int err;
	struct hci_dev *hdev;

	hdev = hci_alloc_dev_priv(sizeof(struct btintel_data));
	if (!hdev)
		return -ENOMEM;

	hdev->bus = HCI_PCI;
	hci_set_drvdata(hdev, data);

	data->hdev = hdev;
	SET_HCIDEV_DEV(hdev, &data->pdev->dev);

	hdev->manufacturer = 2;
	hdev->open = btintel_pcie_open;
	hdev->close = btintel_pcie_close;
	hdev->send = btintel_pcie_send_frame;
	hdev->setup = btintel_pcie_setup;
	hdev->shutdown = btintel_shutdown_combined;
	hdev->hw_error = btintel_hw_error;
	hdev->set_diag = btintel_set_diag;
	hdev->set_bdaddr = btintel_set_bdaddr;

	err = hci_register_dev(hdev);
	if (err < 0) {
		BT_ERR("Failed to register to hdev (%d)", err);
		goto exit_error;
	}

	return 0;

exit_error:
	hci_free_dev(hdev);
	return err;
}

static int btintel_pcie_probe(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	int err;
	struct btintel_pcie_data *data;

	if (!pdev)
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;

	spin_lock_init(&data->irq_lock);
	spin_lock_init(&data->hci_rx_lock);

	init_waitqueue_head(&data->gp0_wait_q);
	data->gp0_received = false;

	init_waitqueue_head(&data->tx_wait_q);
	data->tx_wait_done = false;

	data->workqueue = alloc_ordered_workqueue(KBUILD_MODNAME, WQ_HIGHPRI);
	if (!data->workqueue)
		return -ENOMEM;

	skb_queue_head_init(&data->rx_skb_q);
	INIT_WORK(&data->rx_work, btintel_pcie_rx_work);

	data->boot_stage_cache = 0x00;
	data->img_resp_cache = 0x00;

	err = btintel_pcie_config_pcie(pdev, data);
	if (err)
		goto exit_error;

	pci_set_drvdata(pdev, data);

	err = btintel_pcie_alloc(data);
	if (err)
		goto exit_error;

	err = btintel_pcie_enable_bt(data);
	if (err)
		goto exit_error;

	/* CNV information (CNVi and CNVr) is in CSR */
	data->cnvi = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_HW_REV_REG);

	data->cnvr = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_RF_ID_REG);

	err = btintel_pcie_start_rx(data);
	if (err)
		goto exit_error;

	err = btintel_pcie_setup_hdev(data);
	if (err)
		goto exit_error;

	bt_dev_dbg(data->hdev, "cnvi: 0x%8.8x cnvr: 0x%8.8x", data->cnvi,
		   data->cnvr);
	return 0;

exit_error:
	/* reset device before exit */
	btintel_pcie_reset_bt(data);

	pci_clear_master(pdev);

	pci_set_drvdata(pdev, NULL);

	return err;
}

static void btintel_pcie_remove(struct pci_dev *pdev)
{
	struct btintel_pcie_data *data;

	data = pci_get_drvdata(pdev);

	btintel_pcie_reset_bt(data);
	for (int i = 0; i < data->alloc_vecs; i++) {
		struct msix_entry *msix_entry;

		msix_entry = &data->msix_entries[i];
		free_irq(msix_entry->vector, msix_entry);
	}

	pci_free_irq_vectors(pdev);

	btintel_pcie_release_hdev(data);

	flush_work(&data->rx_work);

	destroy_workqueue(data->workqueue);

	btintel_pcie_free(data);

	pci_clear_master(pdev);

	pci_set_drvdata(pdev, NULL);
}

static struct pci_driver btintel_pcie_driver = {
	.name = KBUILD_MODNAME,
	.id_table = btintel_pcie_table,
	.probe = btintel_pcie_probe,
	.remove = btintel_pcie_remove,
};
module_pci_driver(btintel_pcie_driver);

MODULE_AUTHOR("Tedd Ho-Jeong An <tedd.an@intel.com>");
MODULE_DESCRIPTION("Intel Bluetooth PCIe transport driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
