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
#include <linux/overflow.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/acpi.h>
#include <linux/suspend.h>

#include <linux/unaligned.h>
#include <linux/devcoredump.h>
#include <linux/scatterlist.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci_drv.h>

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

#define BTINTEL_PCIE_DMA_ALIGN_128B	128 /* 128 byte aligned */

/* Intel Bluetooth PCIe device id table */
static const struct pci_device_id btintel_pcie_table[] = {
	/* BlazarI, Wildcat Lake */
	{ BTINTEL_PCI_DEVICE(0x4D76, PCI_ANY_ID) },
	/* BlazarI, Lunar Lake */
	{ BTINTEL_PCI_DEVICE(0xA876, PCI_ANY_ID) },
	/* Scorpious, Panther Lake-H484 */
	{ BTINTEL_PCI_DEVICE(0xE376, PCI_ANY_ID) },
	 /* Scorpious, Panther Lake-H404 */
	{ BTINTEL_PCI_DEVICE(0xE476, PCI_ANY_ID) },
	 /* Scorpious2, Nova Lake-PCD-H */
	{ BTINTEL_PCI_DEVICE(0xD346, PCI_ANY_ID) },
	 /* Scorpious2, Nova Lake-PCD-S */
	{ BTINTEL_PCI_DEVICE(0x6E74, PCI_ANY_ID) },
	 /* Draco */
	{ BTINTEL_PCI_DEVICE(0x2731, PCI_ANY_ID) },
	{ BTINTEL_PCI_DEVICE(0x2732, PCI_ANY_ID) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, btintel_pcie_table);

struct btintel_pcie_dev_recovery {
	struct list_head list;
	u8 count;
	time64_t last_error;
	char name[];
};

/* Intel PCIe uses 4 bytes of HCI type instead of 1 byte BT SIG HCI type */
#define BTINTEL_PCIE_HCI_TYPE_LEN	4
#define BTINTEL_PCIE_HCI_CMD_PKT	0x00000001
#define BTINTEL_PCIE_HCI_ACL_PKT	0x00000002
#define BTINTEL_PCIE_HCI_SCO_PKT	0x00000003
#define BTINTEL_PCIE_HCI_EVT_PKT	0x00000004
#define BTINTEL_PCIE_HCI_ISO_PKT	0x00000005

#define BTINTEL_PCIE_MAGIC_NUM	0xA5A5A5A5

#define BTINTEL_PCIE_TRIGGER_REASON_USER_TRIGGER	0x17A2
#define BTINTEL_PCIE_TRIGGER_REASON_FW_ASSERT		0x1E61

#define BTINTEL_PCIE_RESET_WINDOW_SECS		5
#define BTINTEL_PCIE_FLR_MAX_RETRY	1

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

enum {
	BTINTEL_PCIE_DSM_SET_RESET_TIMING = 1,
	BTINTEL_PCIE_DSM_GET_RESET_TIMING = 2,
	BTINTEL_PCIE_DSM_BT_PLDR_CONFIG = 3,
	BTINTEL_PCIE_DSM_GET_RESET_TYPE = 4,
	BTINTEL_PCIE_DSM_DYNAMIC_PLDR = 5,
	BTINTEL_PCIE_DSM_GET_RESET_METHOD = 6,
	BTINTEL_PCIE_DSM_SET_PLDR_DELAY = 7,
};

enum btintel_dsm_internal_product_reset_mode {
	BTINTEL_PCIE_DSM_PLDR_MODE_EN_PROD_RESET	= BIT(0),
	BTINTEL_PCIE_DSM_PLDR_MODE_EN_WIFI_FLR		= BIT(1),
	BTINTEL_PCIE_DSM_PLDR_MODE_EN_BT_OFF_ON		= BIT(2),
};

/* Structure for dbgc fragment buffer
 * @buf_addr_lsb: LSB of the buffer's physical address
 * @buf_addr_msb: MSB of the buffer's physical address
 * @buf_size: Total size of the buffer
 */
struct btintel_pcie_dbgc_ctxt_buf {
	u32	buf_addr_lsb;
	u32	buf_addr_msb;
	u32	buf_size;
};

/* Structure for dbgc fragment
 * @magic_num: 0XA5A5A5A5
 * @ver: For Driver-FW compatibility
 * @total_size: Total size of the payload debug info
 * @num_buf: Num of allocated debug bufs
 * @bufs: All buffer's addresses and sizes
 */
struct btintel_pcie_dbgc_ctxt {
	u32	magic_num;
	u32     ver;
	u32     total_size;
	u32     num_buf;
	struct btintel_pcie_dbgc_ctxt_buf bufs[BTINTEL_PCIE_DBGC_BUFFER_COUNT];
};

struct btintel_pcie_mdbgc_ctxt {
	u32     magic_num;
	u32     ver;
	u32     buf1_index;
	u32     buf1_count;
	struct btintel_pcie_dbgc_ctxt_buf buf1[BTINTEL_PCIE_DBGC_BUFFER_COUNT];
	u32     buf2_index;
	u32     buf2_count;
	struct btintel_pcie_dbgc_ctxt_buf buf2[BTINTEL_PCIE_DBGC_BUFFER_COUNT];
	u32     buf3_index;
	u32     buf3_count;
	struct btintel_pcie_dbgc_ctxt_buf buf3[BTINTEL_PCIE_DBGC_BUFFER_COUNT];
};

struct btintel_pcie_trigger_evt {
	u8 type;
	u8 len;
	__le32 addr;
	__le32 size;
} __packed;

struct btintel_pcie_fwtrigger_evt {
	__le32 reserved;
	u8	type; /* Debug Trigger event */
	__le16	len;
	u8	event_type;
	__le16	event_id;
	__le16	reserved2;
} __packed;

static LIST_HEAD(btintel_pcie_recovery_list);
static DEFINE_SPINLOCK(btintel_pcie_recovery_lock);

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

/* Returns true when firmware traces are routed to the WiFi DBGC. In that
 * mode the host must not allocate DBGC buffers and must not publish their
 * addresses in the context info.
 */
static inline bool btintel_pcie_dbg_to_wifi(struct btintel_pcie_data *data)
{
	return data->dbg_path_cache != BTINTEL_PCIE_DRAM;
}

/* Helper function to allocate and setup a debug buffer group
 * @data: driver data structure
 * @buf: pointer to data_buf array pointer
 * @p_addr: pointer to physical DMA address
 * @v_addr: pointer to virtual address
 * @frag: pointer to fragment buffer array
 * @buf_index: buffer index (for error messages)
 * @buf_count: number of buffers to allocate
 */
static int btintel_pcie_alloc_dbgc_buf(struct btintel_pcie_data *data,
				       struct data_buf **buf,
				       dma_addr_t *p_addr,
				       void **v_addr,
				       struct btintel_pcie_dbgc_ctxt_buf *frag,
				       u32 buf_index,
				       u32 buf_count)
{
	struct data_buf *b;
	int i;

	*buf = devm_kcalloc(&data->pdev->dev, buf_count,
			    sizeof(**buf), GFP_KERNEL);
	if (!*buf) {
		BT_ERR("Failed to allocate dbgc buf: %u", buf_index + 1);
		return -ENOMEM;
	}

	*v_addr = dmam_alloc_coherent(&data->pdev->dev,
				      buf_count *
					BTINTEL_PCIE_DBGC_BUFFER_SIZE,
					p_addr,
					GFP_KERNEL | __GFP_NOWARN);
	if (!*v_addr) {
		BT_ERR("Failed to allocate dbgc buf: %u DMA", buf_index + 1);
		return -ENOMEM;
	}

	for (i = 0; i < buf_count; i++) {
		b = &(*buf)[i];
		b->data_p_addr = *p_addr + i * BTINTEL_PCIE_DBGC_BUFFER_SIZE;
		b->data = *v_addr + i * BTINTEL_PCIE_DBGC_BUFFER_SIZE;
		frag[i].buf_addr_lsb = lower_32_bits(b->data_p_addr);
		frag[i].buf_addr_msb = upper_32_bits(b->data_p_addr);
		frag[i].buf_size = BTINTEL_PCIE_DBGC_BUFFER_SIZE;
	}

	return 0;
}

/* This function initializes the memory for MDBGC buffers and formats the
 * DBGC fragment which consists header info and DBGC buffer's LSB, MSB and
 * size as the payload
 */
static int btintel_pcie_setup_mdbgc(struct btintel_pcie_data *data)
{
	struct btintel_pcie_mdbgc_ctxt db_frag;
	u32 frag_size = sizeof(db_frag);
	void *frag_v_addr;
	int err;

	data->mdbgc.count = BTINTEL_PCIE_DBGC_BUFFER_COUNT;

	/* Allocate fragment context structure */
	frag_v_addr = dmam_alloc_coherent(&data->pdev->dev, frag_size,
					  &data->mdbgc.frag_p_addr,
					  GFP_KERNEL | __GFP_NOWARN);
	if (!frag_v_addr) {
		BT_ERR("Failed to allocate mdbgc context");
		return -ENOMEM;
	}

	data->mdbgc.frag_v_addr = frag_v_addr;
	data->mdbgc.frag_size = frag_size;

	/* Initialize fragment header */
	memset(&db_frag, 0, sizeof(db_frag));
	db_frag.magic_num = BTINTEL_PCIE_MAGIC_NUM;
	db_frag.ver = BTINTEL_PCIE_MDBGC_FRAG_VERSION;

	/* Allocate DBGC buffer 1 */
	db_frag.buf1_index = BTINTEL_PCIE_MDBGC_ALLOCATIONID_1;
	db_frag.buf1_count = data->mdbgc.count;
	err = btintel_pcie_alloc_dbgc_buf(data,
					  &data->mdbgc.buf1,
					  &data->mdbgc.buf1_p_addr,
					  &data->mdbgc.buf1_v_addr,
					  db_frag.buf1,
					  0,
					  data->mdbgc.count);
	if (err)
		return err;

	/* Allocate DBGC buffer 2 */
	db_frag.buf2_index = BTINTEL_PCIE_MDBGC_ALLOCATIONID_2;
	db_frag.buf2_count = data->mdbgc.count;
	err = btintel_pcie_alloc_dbgc_buf(data,
					  &data->mdbgc.buf2,
					  &data->mdbgc.buf2_p_addr,
					  &data->mdbgc.buf2_v_addr,
					  db_frag.buf2,
					  1,
					  data->mdbgc.count);
	if (err)
		return err;

	/* Allocate DBGC buffer 3 */
	db_frag.buf3_index = BTINTEL_PCIE_MDBGC_ALLOCATIONID_3;
	db_frag.buf3_count = data->mdbgc.count;
	err = btintel_pcie_alloc_dbgc_buf(data,
					  &data->mdbgc.buf3,
					  &data->mdbgc.buf3_p_addr,
					  &data->mdbgc.buf3_v_addr,
					  db_frag.buf3,
					  2,
					  data->mdbgc.count);
	if (err)
		return err;

	/* Copy fragment to DMA coherent memory */
	memcpy(data->mdbgc.frag_v_addr, &db_frag, sizeof(db_frag));
	return 0;
}

/* This function initializes the memory for DBGC buffers and formats the
 * DBGC fragment which consists header info and DBGC buffer's LSB, MSB and
 * size as the payload
 */
static int btintel_pcie_setup_dbgc(struct btintel_pcie_data *data)
{
	struct btintel_pcie_dbgc_ctxt db_frag;
	u32 frag_size = sizeof(db_frag);
	int err;

	data->dbgc.count = BTINTEL_PCIE_DBGC_BUFFER_COUNT;

	/* Allocate fragment context structure */
	data->dbgc.frag_v_addr = dmam_alloc_coherent(&data->pdev->dev,
						     frag_size,
						     &data->dbgc.frag_p_addr,
						     GFP_KERNEL | __GFP_NOWARN);
	if (!data->dbgc.frag_v_addr) {
		BT_ERR("Failed to allocate dbgc context");
		return -ENOMEM;
	}

	data->dbgc.frag_size = sizeof(struct btintel_pcie_dbgc_ctxt);

	/* Initialize fragment header */
	memset(&db_frag, 0, sizeof(db_frag));
	db_frag.magic_num = BTINTEL_PCIE_MAGIC_NUM;
	db_frag.ver = BTINTEL_PCIE_DBGC_FRAG_VERSION;
	db_frag.total_size = BTINTEL_PCIE_DBGC_FRAG_PAYLOAD_SIZE;
	db_frag.num_buf = BTINTEL_PCIE_DBGC_FRAG_BUFFER_COUNT;

	/* Allocate DBGC buffers */
	err = btintel_pcie_alloc_dbgc_buf(data,
					  &data->dbgc.bufs,
					  &data->dbgc.buf_p_addr,
					  &data->dbgc.buf_v_addr,
					  db_frag.bufs,
					  0,
					  data->dbgc.count);
	if (err)
		return err;

	/* Copy fragment to DMA coherent memory */
	memcpy(data->dbgc.frag_v_addr, &db_frag, sizeof(db_frag));
	return 0;
}

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

static inline void btintel_pcie_dump_debug_registers(struct hci_dev *hdev)
{
	struct btintel_pcie_data *data = hci_get_drvdata(hdev);
	u16 cr_hia, cr_tia;
	u32 reg, mbox_reg;
	struct sk_buff *skb;
	u8 buf[80];

	skb = alloc_skb(1024, GFP_ATOMIC);
	if (!skb)
		return;

	strscpy(buf, "---- Dump of debug registers ---");
	bt_dev_dbg(hdev, "%s", buf);
	skb_put_data(skb, buf, strlen(buf));

	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_BOOT_STAGE_REG);
	snprintf(buf, sizeof(buf), "boot stage: 0x%8.8x", reg);
	bt_dev_dbg(hdev, "%s", buf);
	skb_put_data(skb, buf, strlen(buf));
	data->boot_stage_cache = reg;

	if (reg & BTINTEL_PCIE_CSR_BOOT_STAGE_DEVICE_WARNING)
		bt_dev_warn(hdev, "Controller device warning (boot_stage: 0x%8.8x)", reg);

	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_IPC_STATUS_REG);
	snprintf(buf, sizeof(buf), "ipc status: 0x%8.8x", reg);
	skb_put_data(skb, buf, strlen(buf));
	bt_dev_dbg(hdev, "%s", buf);

	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_IPC_CONTROL_REG);
	snprintf(buf, sizeof(buf), "ipc control: 0x%8.8x", reg);
	skb_put_data(skb, buf, strlen(buf));
	bt_dev_dbg(hdev, "%s", buf);

	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_IPC_SLEEP_CTL_REG);
	snprintf(buf, sizeof(buf), "ipc sleep control: 0x%8.8x", reg);
	skb_put_data(skb, buf, strlen(buf));
	bt_dev_dbg(hdev, "%s", buf);

	/*Read the Mail box status and registers*/
	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_MBOX_STATUS_REG);
	snprintf(buf, sizeof(buf), "mbox status: 0x%8.8x", reg);
	skb_put_data(skb, buf, strlen(buf));
	if (reg & BTINTEL_PCIE_CSR_MBOX_STATUS_MBOX1) {
		mbox_reg = btintel_pcie_rd_reg32(data,
						 BTINTEL_PCIE_CSR_MBOX_1_REG);
		snprintf(buf, sizeof(buf), "mbox_1: 0x%8.8x", mbox_reg);
		skb_put_data(skb, buf, strlen(buf));
		bt_dev_dbg(hdev, "%s", buf);
	}

	if (reg & BTINTEL_PCIE_CSR_MBOX_STATUS_MBOX2) {
		mbox_reg = btintel_pcie_rd_reg32(data,
						 BTINTEL_PCIE_CSR_MBOX_2_REG);
		snprintf(buf, sizeof(buf), "mbox_2: 0x%8.8x", mbox_reg);
		skb_put_data(skb, buf, strlen(buf));
		bt_dev_dbg(hdev, "%s", buf);
	}

	if (reg & BTINTEL_PCIE_CSR_MBOX_STATUS_MBOX3) {
		mbox_reg = btintel_pcie_rd_reg32(data,
						 BTINTEL_PCIE_CSR_MBOX_3_REG);
		snprintf(buf, sizeof(buf), "mbox_3: 0x%8.8x", mbox_reg);
		skb_put_data(skb, buf, strlen(buf));
		bt_dev_dbg(hdev, "%s", buf);
	}

	if (reg & BTINTEL_PCIE_CSR_MBOX_STATUS_MBOX4) {
		mbox_reg = btintel_pcie_rd_reg32(data,
						 BTINTEL_PCIE_CSR_MBOX_4_REG);
		snprintf(buf, sizeof(buf), "mbox_4: 0x%8.8x", mbox_reg);
		skb_put_data(skb, buf, strlen(buf));
		bt_dev_dbg(hdev, "%s", buf);
	}

	cr_hia = data->ia.cr_hia[BTINTEL_PCIE_RXQ_NUM];
	cr_tia = data->ia.cr_tia[BTINTEL_PCIE_RXQ_NUM];
	snprintf(buf, sizeof(buf), "rxq: cr_tia: %u cr_hia: %u", cr_tia, cr_hia);
	skb_put_data(skb, buf, strlen(buf));
	bt_dev_dbg(hdev, "%s", buf);

	cr_hia = data->ia.cr_hia[BTINTEL_PCIE_TXQ_NUM];
	cr_tia = data->ia.cr_tia[BTINTEL_PCIE_TXQ_NUM];
	snprintf(buf, sizeof(buf), "txq: cr_tia: %u cr_hia: %u", cr_tia, cr_hia);
	skb_put_data(skb, buf, strlen(buf));
	bt_dev_dbg(hdev, "%s", buf);
	strscpy(buf, "--------------------------------");
	bt_dev_dbg(hdev, "%s", buf);

	hci_recv_diag(hdev, skb);
}

static int btintel_pcie_send_sync(struct btintel_pcie_data *data,
				  struct sk_buff *skb, u32 pkt_type, u16 opcode)
{
	int ret;
	u16 tfd_index;
	u32 old_ctxt;
	bool wait_on_alive = false;
	struct hci_dev *hdev = data->hdev;

	struct txq *txq = &data->txq;

	tfd_index = data->ia.tr_hia[BTINTEL_PCIE_TXQ_NUM];

	if (tfd_index >= txq->count)
		return -ERANGE;

	if (skb->len > BTINTEL_PCIE_BUFFER_SIZE - BTINTEL_PCIE_HCI_TYPE_LEN) {
		bt_dev_err(hdev, "TX skb too large (%u > %u)", skb->len,
			   BTINTEL_PCIE_BUFFER_SIZE - BTINTEL_PCIE_HCI_TYPE_LEN);
		return -EMSGSIZE;
	}

	/* Firmware raises alive interrupt on HCI_OP_RESET or
	 * BTINTEL_HCI_OP_RESET
	 */
	wait_on_alive = (pkt_type == BTINTEL_PCIE_HCI_CMD_PKT &&
		(opcode == BTINTEL_HCI_OP_RESET || opcode == HCI_OP_RESET));

	if (wait_on_alive) {
		data->gp0_received = false;
		old_ctxt = data->alive_intr_ctxt;
		data->alive_intr_ctxt =
			(opcode == BTINTEL_HCI_OP_RESET ? BTINTEL_PCIE_INTEL_HCI_RESET1 :
				BTINTEL_PCIE_HCI_RESET);
		bt_dev_dbg(data->hdev, "sending cmd: 0x%4.4x alive context changed: %s  ->  %s",
			   opcode, btintel_pcie_alivectxt_state2str(old_ctxt),
			   btintel_pcie_alivectxt_state2str(data->alive_intr_ctxt));
	}

	memcpy(skb_push(skb, BTINTEL_PCIE_HCI_TYPE_LEN), &pkt_type,
	       BTINTEL_PCIE_HCI_TYPE_LEN);

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
	if (!ret) {
		bt_dev_err(data->hdev, "Timeout (%u ms) on tx completion",
			   BTINTEL_PCIE_TX_WAIT_TIMEOUT_MS);
		btintel_pcie_dump_debug_registers(data->hdev);
		return -ETIME;
	}

	if (wait_on_alive) {
		ret = wait_event_timeout(data->gp0_wait_q,
					 data->gp0_received,
					 msecs_to_jiffies(BTINTEL_DEFAULT_INTR_TIMEOUT_MS));
		if (!ret) {
			hdev->stat.err_tx++;
			bt_dev_err(hdev, "Timeout (%u ms)  on alive interrupt, alive context: %s",
				   BTINTEL_DEFAULT_INTR_TIMEOUT_MS,
				   btintel_pcie_alivectxt_state2str(data->alive_intr_ctxt));
			return  -ETIME;
		}
	}
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

	if (frbd_index >= rxq->count)
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
	struct rxq *rxq = &data->rxq;

	/* Post (BTINTEL_PCIE_RX_DESCS_COUNT - 3) buffers to overcome the
	 * hardware issues leading to race condition at the firmware.
	 */

	for (i = 0; i < rxq->count - 3; i++) {
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

static void btintel_pcie_mac_init(struct btintel_pcie_data *data)
{
	u32 reg;

	/* Set MAC_INIT bit to start primary bootloader */
	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG);
	reg &= ~(BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_INIT |
			BTINTEL_PCIE_CSR_FUNC_CTRL_BUS_MASTER_DISCON |
			BTINTEL_PCIE_CSR_FUNC_CTRL_SW_RESET);
	reg |= (BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_ENA |
			BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_INIT);
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG, reg);
}

static int btintel_pcie_get_mac_access(struct btintel_pcie_data *data)
{
	u32 reg;
	int retry = 15;

	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG);

	if (!(reg & BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_ACCESS_REQ)) {
		reg |= BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_ACCESS_REQ;
		btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG, reg);
	}

	do {
		reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG);
		if (reg & BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_ACCESS_STS)
			return 0;
		/* Need delay here for Target Access harwdware to settle down*/
		usleep_range(1000, 1200);

	} while (--retry > 0);

	return -ETIME;
}

static void btintel_pcie_release_mac_access(struct btintel_pcie_data *data)
{
	u32 reg;

	reg = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG);

	if (reg & BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_ACCESS_REQ) {
		reg &= ~BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_ACCESS_REQ;
		btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_FUNC_CTRL_REG, reg);
	}
}

static struct scatterlist *btintel_pcie_alloc_sgtable(ssize_t size)
{
	int nents;
	struct page *page;
	struct scatterlist *sg, *result;

	if (size <= 0)
		return NULL;

	nents = DIV_ROUND_UP(size, PAGE_SIZE);
	result = kcalloc(nents, sizeof(*result), GFP_KERNEL);
	if (!result)
		return NULL;

	sg_init_table(result, nents);
	sg = result;

	while (size > 0) {
		ssize_t bytes = min_t(ssize_t, size, PAGE_SIZE);

		page = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!page)
			goto err_free;

		sg_set_page(sg, page, bytes, 0);
		sg = sg_next(sg);
		size -= bytes;
	}
	return result;

err_free:
	for (sg = result; sg; sg = sg_next(sg)) {
		page = sg_page(sg);
		if (page)
			__free_page(page);
	}
	kfree(result);
	return NULL;
}

static struct btintel_pcie_dump_entry *
btintel_pcie_dump_entry_alloc(u32 data_size)
{
	struct btintel_pcie_dump_entry *entry;

	entry = vzalloc(sizeof(*entry) + data_size);
	if (!entry)
		return NULL;

	entry->size = data_size;
	INIT_LIST_HEAD(&entry->list);
	return entry;
}

static void btintel_pcie_dump_list_free(struct list_head *list)
{
	struct btintel_pcie_dump_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, list, list) {
		list_del(&entry->list);
		vfree(entry);
	}
}

static u32 btintel_pcie_dump_list_total_size(struct list_head *list)
{
	struct btintel_pcie_dump_entry *entry;
	u32 total = 0;

	list_for_each_entry(entry, list, list)
		total += entry->size;

	return total;
}

static int btintel_pcie_dump_dram(struct list_head *list,
				  u8 count, struct data_buf *bufs,
				  u32 write_ptr, u32 wrap_ctr, u32 cur_frag,
				  u32 region_id, const char *name)
{
	struct btintel_pcie_dump_entry *entry;
	struct btintel_pcie_ini_dump_data *dump_data;
	struct btintel_pcie_ini_monitor_dump *mon_dump;
	struct btintel_pcie_dump_range *range;
	u32 mon_hdr_size, ranges_size, payload_size, total_size;
	int i;

	mon_hdr_size = sizeof(*mon_dump);
	ranges_size = count * (sizeof(*range) + BTINTEL_PCIE_DBGC_BUFFER_SIZE);
	payload_size = mon_hdr_size + ranges_size;
	total_size = sizeof(*dump_data) + payload_size;

	entry = btintel_pcie_dump_entry_alloc(total_size);
	if (!entry)
		return -ENOMEM;

	dump_data = (void *)entry->data;
	dump_data->type = BTINTEL_PCIE_INI_REGION_DRAM_BUFFER;
	dump_data->sub_type = 0;
	dump_data->sub_type_ver = 0;
	dump_data->reserved = 0;
	dump_data->len = cpu_to_le32(payload_size);

	mon_dump = (void *)dump_data->data;
	mon_dump->header.version = cpu_to_le32(BTINTEL_PCIE_INI_DUMP_VER);
	mon_dump->header.region_id = cpu_to_le32(region_id);
	mon_dump->header.num_of_ranges = cpu_to_le32(count);
	mon_dump->header.name_len = cpu_to_le32(BTINTEL_PCIE_INI_MAX_NAME);
	memset(mon_dump->header.name, 0, BTINTEL_PCIE_INI_MAX_NAME);
	strscpy(mon_dump->header.name, name, BTINTEL_PCIE_INI_MAX_NAME);

	mon_dump->write_ptr = cpu_to_le32(write_ptr);
	mon_dump->cycle_cnt = cpu_to_le32(wrap_ctr);
	mon_dump->cur_frag = cpu_to_le32(cur_frag);

	range = (void *)mon_dump->data;
	for (i = 0; i < count; i++) {
		range->range_data_size =
			cpu_to_le32(BTINTEL_PCIE_DBGC_BUFFER_SIZE);
		range->dram_base_addr = cpu_to_le64(bufs[i].data_p_addr);
		memcpy(range->data, bufs[i].data,
		       BTINTEL_PCIE_DBGC_BUFFER_SIZE);
		range = (void *)range->data + BTINTEL_PCIE_DBGC_BUFFER_SIZE;
	}

	list_add_tail(&entry->list, list);
	return 0;
}

static int
btintel_pcie_dump_dram_monitor(struct btintel_pcie_data *data,
			       struct list_head *list, u8 count,
			       struct data_buf *bufs, u32 status_reg,
			       u32 wrap_reg, u8 allocation_id, u32 region_id,
			       const char *name)
{
	u32 allocation_offset, write_ptr, wrap_ctr, wr_ptr_status;
	u8 cur_frag;

	allocation_offset = allocation_id *
			    BTINTEL_PCIE_DBGC_ALLOCATION_OFFSET;
	wr_ptr_status = btintel_pcie_rd_dev_mem(data, status_reg +
						allocation_offset);
	wrap_ctr = btintel_pcie_rd_dev_mem(data, wrap_reg +
					  allocation_offset);

	write_ptr = wr_ptr_status & BTINTEL_PCIE_DBG_OFFSET_BIT_MASK;
	write_ptr >>= 2;
	cur_frag = BTINTEL_PCIE_DBGC_DBG_BUF_IDX(wr_ptr_status);
	if (cur_frag >= count) {
		bt_dev_warn(data->hdev,
			    "Invalid DRAM monitor fragment %u for allocation %u",
			    cur_frag, allocation_id);
		return -EINVAL;
	}

	bt_dev_dbg(data->hdev,
		   "allocation=%u wr_ptr_status=0x%08x write_ptr=0x%06x cur_frag=%u wrap_ctr=0x%08x",
		   allocation_id, wr_ptr_status, write_ptr, cur_frag, wrap_ctr);

	return btintel_pcie_dump_dram(list, count, bufs, write_ptr, wrap_ctr,
				      cur_frag, region_id, name);
}

static int btintel_pcie_dump_target_region(struct btintel_pcie_data *data,
					   struct list_head *list,
					   u32 region_id, const char *name,
					   u32 addr_start, u32 size)
{
	struct btintel_pcie_dump_entry *entry;
	struct btintel_pcie_ini_dump_data *dump_data;
	struct btintel_pcie_ini_dump_header *hdr;
	struct btintel_pcie_dump_range *range;
	u32 payload_size, total_size, target_mem_offset, tempdata;
	u8 *dest;
	int i;

	if (!size) {
		bt_dev_warn(data->hdev, "Skipping empty dump region: %s", name);
		return 1;
	}

	if (!addr_start) {
		bt_dev_warn(data->hdev,
			    "Skipping dump region with zero address: %s",
			    name);
		return 1;
	}

	if (addr_start > U32_MAX - size) {
		bt_dev_warn(data->hdev,
			    "Skipping dump region %s: addr_start 0x%08x + size %u would overflow",
			    name, addr_start, size);
		return 1;
	}

	/* Align to 4 bytes - target access reads 32-bit words */
	size = round_down(size, sizeof(u32));
	if (!size) {
		bt_dev_warn(data->hdev,
			    "Skipping dump region %s: size is smaller than 4-byte access",
			    name);
		return 1;
	}

	bt_dev_dbg(data->hdev,
		   "Target access: region=%s start=0x%08x size=%u",
		   name, addr_start, size);

	payload_size = sizeof(*hdr) + sizeof(*range) + size;
	total_size = sizeof(*dump_data) + payload_size;
	entry = btintel_pcie_dump_entry_alloc(total_size);
	if (!entry)
		return -ENOMEM;

	dump_data = (void *)entry->data;
	dump_data->type = BTINTEL_PCIE_INI_REGION_DEVICE_MEMORY;
	dump_data->sub_type = 0;
	dump_data->sub_type_ver = 0;
	dump_data->reserved = 0;
	dump_data->len = cpu_to_le32(payload_size);

	hdr = (void *)dump_data->data;
	hdr->version = cpu_to_le32(BTINTEL_PCIE_INI_DUMP_VER);
	hdr->region_id = cpu_to_le32(region_id);
	hdr->num_of_ranges = cpu_to_le32(1);
	hdr->name_len = cpu_to_le32(BTINTEL_PCIE_INI_MAX_NAME);
	memset(hdr->name, 0, BTINTEL_PCIE_INI_MAX_NAME);
	strscpy(hdr->name, name, BTINTEL_PCIE_INI_MAX_NAME);

	range = (void *)(hdr + 1);
	range->range_data_size = cpu_to_le32(size);
	range->internal_base_addr = cpu_to_le32(addr_start);

	dest = (u8 *)range->data;
	target_mem_offset = size / sizeof(u32);
	for (i = 0; i < target_mem_offset; i++) {
		u32 offset = BTINTEL_PCIE_TARGET_ACCESS_FRAG_OFFSET * i;

		tempdata = btintel_pcie_rd_dev_mem(data,
						   addr_start + offset);
		memcpy(dest, &tempdata, sizeof(tempdata));
		dest += sizeof(tempdata);
	}

	list_add_tail(&entry->list, list);
	return 0;
}

static int btintel_pcie_dump_smem_monitor_region(struct btintel_pcie_data *data,
						 struct list_head *list,
						 u32 region_id,
						 const char *name,
						 u32 addr_start, u32 size)
{
	struct btintel_pcie_dump_entry *entry;
	struct btintel_pcie_ini_dump_data *dump_data;
	struct btintel_pcie_ini_monitor_dump *mon;
	struct btintel_pcie_dump_range *range;
	u32 payload_size, total_size, target_mem_offset, tempdata;
	u8 *dest;
	int i;

	if (!size || !addr_start) {
		bt_dev_err(data->hdev, "Skipping smem dump: size = %u addr = %8.8x",
			   size, addr_start);
		return -EINVAL;
	}

	payload_size = sizeof(*mon) + sizeof(*range) + size;
	total_size = sizeof(*dump_data) + payload_size;
	entry = btintel_pcie_dump_entry_alloc(total_size);
	if (!entry)
		return -ENOMEM;

	dump_data = (void *)entry->data;
	dump_data->type = BTINTEL_PCIE_INI_REGION_INTERNAL_BUFFER;
	dump_data->sub_type = 0;
	dump_data->sub_type_ver = 0;
	dump_data->reserved = 0;
	dump_data->len = cpu_to_le32(payload_size);

	mon = (void *)dump_data->data;
	mon->header.version = cpu_to_le32(BTINTEL_PCIE_INI_DUMP_VER);
	mon->header.region_id = cpu_to_le32(region_id);
	mon->header.num_of_ranges = cpu_to_le32(1);
	mon->header.name_len = cpu_to_le32(BTINTEL_PCIE_INI_MAX_NAME);
	memset(mon->header.name, 0, BTINTEL_PCIE_INI_MAX_NAME);
	strscpy(mon->header.name, name, BTINTEL_PCIE_INI_MAX_NAME);

	mon->write_ptr = cpu_to_le32(0);
	mon->cycle_cnt = cpu_to_le32(0);
	mon->cur_frag = cpu_to_le32(0);

	range = (void *)mon->data;
	range->range_data_size = cpu_to_le32(size);
	range->internal_base_addr = cpu_to_le32(addr_start);

	dest = (u8 *)range->data;
	target_mem_offset = size / sizeof(u32);
	for (i = 0; i < target_mem_offset; i++) {
		u32 offset = BTINTEL_PCIE_TARGET_ACCESS_FRAG_OFFSET * i;

		tempdata = btintel_pcie_rd_dev_mem(data,
						   addr_start + offset);
		memcpy(dest, &tempdata, sizeof(tempdata));
		dest += sizeof(tempdata);
	}

	list_add_tail(&entry->list, list);
	return 0;
}

static int btintel_pcie_dump_info(struct btintel_pcie_data *data,
				  struct list_head *list,
				  u64 regions_mask)
{
	struct btintel_pcie_dump_entry *entry;
	struct btintel_pcie_error_dump_data *tlv;
	struct btintel_pcie_ini_dump_info *dump;
	u32 size = sizeof(*tlv) + sizeof(*dump);
	char build_tag[64];

	entry = btintel_pcie_dump_entry_alloc(size);
	if (!entry)
		return -ENOMEM;

	tlv = (void *)entry->data;
	tlv->type = cpu_to_le32(BTINTEL_PCIE_INI_DUMP_INFO_TYPE);
	tlv->len = cpu_to_le32(sizeof(*dump));

	dump = (void *)tlv->data;
	memset(dump, 0, sizeof(*dump));

	dump->version = cpu_to_le32(BTINTEL_PCIE_INI_DUMP_VER);
	dump->trigger_reason = cpu_to_le32(data->dmp_hdr.trigger_reason);

	if (data->dmp_hdr.trigger_reason ==
	    BTINTEL_PCIE_TRIGGER_REASON_FW_ASSERT)
		dump->time_point =
			cpu_to_le32(BTINTEL_PCIE_TIME_POINT_FW_ASSERT);
	else if (data->dmp_hdr.trigger_reason ==
		 BTINTEL_PCIE_TRIGGER_REASON_USER_TRIGGER)
		dump->time_point =
			cpu_to_le32(BTINTEL_PCIE_TIME_POINT_USER_TRIGGER);

	dump->hw_type = cpu_to_le32(INTEL_CNVX_TOP_TYPE(data->dmp_hdr.cnvi_top));
	dump->hw_step = cpu_to_le32(INTEL_CNVX_TOP_STEP(data->dmp_hdr.cnvi_top));
	bt_dev_dbg(data->hdev, "hw_type=0x%x hw_step=0x%x (cnvi_top=0x%x)",
		   le32_to_cpu(dump->hw_type), le32_to_cpu(dump->hw_step),
		   data->dmp_hdr.cnvi_top);

	dump->ver_type = cpu_to_le32(data->dmp_hdr.cnvi_bt);
	dump->ver_subtype = cpu_to_le32(data->dmp_hdr.fw_sha);
	dump->rf_id_type = cpu_to_le32(INTEL_CNVX_TOP_TYPE(data->dmp_hdr.cnvr_top));
	dump->rf_id_dash = cpu_to_le32(INTEL_CNVX_TOP_DASH(data->dmp_hdr.cnvr_top));
	dump->rf_id_step = cpu_to_le32(INTEL_CNVX_TOP_STEP(data->dmp_hdr.cnvr_top));
	dump->rf_id_flavor = cpu_to_le32(INTEL_CNVX_TOP_FLAVOR(data->dmp_hdr.cnvr_top));
	bt_dev_dbg(data->hdev,
		   "rf_id_type=0x%x rf_id_dash=0x%x rf_id_step=0x%x rf_id_flavor=0x%x (cnvr_top=0x%x)",
		   le32_to_cpu(dump->rf_id_type),
		   le32_to_cpu(dump->rf_id_dash),
		   le32_to_cpu(dump->rf_id_step),
		   le32_to_cpu(dump->rf_id_flavor),
		   data->dmp_hdr.cnvr_top);
	dump->lmac_major = cpu_to_le32(0);
	dump->lmac_minor = cpu_to_le32(0);
	dump->umac_major = cpu_to_le32(0);
	dump->umac_minor = cpu_to_le32(0);
	dump->fw_mon_mode = cpu_to_le32(BTINTEL_PCIE_FW_MON_MODE_DRAM);

	dump->regions_mask = cpu_to_le64(regions_mask);

	bt_dev_dbg(data->hdev, "ExpectedRegionIDs regions_mask=0x%016llx",
		   le64_to_cpu(dump->regions_mask));

	snprintf(build_tag, sizeof(build_tag), "%08X", data->dmp_hdr.fw_sha);

	dump->build_tag_len = cpu_to_le32(strlen(build_tag));
	memcpy(dump->build_tag, build_tag, min(strlen(build_tag),
					       sizeof(dump->build_tag)));

	dump->num_of_cfg_names = cpu_to_le32(0);

	bt_dev_dbg(data->hdev,
		   "HwType=0x%08x HwStep=0x%08x RfIdType=0x%08x RfIdDash=0x%08x RfIdStep=0x%08x RfIdFlavor=0x%08x",
		   le32_to_cpu(dump->hw_type), le32_to_cpu(dump->hw_step),
		   le32_to_cpu(dump->rf_id_type),
		   le32_to_cpu(dump->rf_id_dash),
		   le32_to_cpu(dump->rf_id_step),
		   le32_to_cpu(dump->rf_id_flavor));
	bt_dev_dbg(data->hdev, "VerType=0x%08x VerSubType=0x%08x",
		   le32_to_cpu(dump->ver_type), le32_to_cpu(dump->ver_subtype));
	bt_dev_dbg(data->hdev,
		   "LmacMajor=0x%08x LmacMinor=0x%08x UmacMajor=0x%08x UmacMinor=0x%08x",
		   le32_to_cpu(dump->lmac_major), le32_to_cpu(dump->lmac_minor),
		   le32_to_cpu(dump->umac_major),
		   le32_to_cpu(dump->umac_minor));
	bt_dev_dbg(data->hdev, "TriggerReason=0x%04x MonMode=%u BuildTag=%.64s",
		   le32_to_cpu(dump->trigger_reason),
		   le32_to_cpu(dump->fw_mon_mode),
		   dump->build_tag);

	list_add(&entry->list, list);

	return 0;
}

static bool btintel_pcie_is_mdbgc_supported(struct btintel_pcie_data *data)
{
	return data->pdev->device == BTINTEL_PCIE_DEVICE_ID_NVL_S_SCP2 ||
		data->pdev->device == BTINTEL_PCIE_DEVICE_ID_NVL_Hx_SCP2 ||
		data->pdev->device == BTINTEL_PCIE_DEVICE_ID_PTL_FMP2;
}

static void btintel_pcie_dump_mem_range(struct btintel_pcie_data *data,
					struct list_head *list, u32 region_id,
					const char *name, u32 addr_start,
					u32 region_size, u64 *regions_mask)
{
	if (!region_size)
		return;

	if (!btintel_pcie_dump_target_region(data, list, region_id, name,
					     addr_start, region_size))
		*regions_mask |= BIT_ULL(region_id);
}

static int btintel_pcie_read_debug_regions(struct btintel_pcie_data *data)
{
	struct btintel_pcie_dbgc *dbgc = NULL;
	struct btintel_pcie_mdbgc *mdbgc = NULL;
	struct btintel_pcie_dump_mem_info *dump_info = &data->dump_info;
	struct hci_dev *hdev = data->hdev;
	struct btintel_pcie_dump_entry *entry;
	struct btintel_pcie_dump_file_hdr *file_hdr;
	struct scatterlist *sg_dump_data;
	u32 status_reg, wrap_reg;
	u64 regions_mask = 0;
	u8 hw_variant;
	u32 file_len;
	u8 count;
	int ret;
	LIST_HEAD(dump_list);

	if (!IS_ENABLED(CONFIG_DEV_COREDUMP))
		return -EOPNOTSUPP;

	if (btintel_pcie_is_mdbgc_supported(data)) {
		mdbgc = &data->mdbgc;
		count = mdbgc->count;
	} else {
		dbgc = &data->dbgc;
		count = dbgc->count;
	}

	hw_variant = INTEL_HW_VARIANT(data->cnvi);

	if (hw_variant == BTINTEL_HWID_BZRI ||
	    hw_variant == BTINTEL_HWID_BZRIW) {
		status_reg = BTINTEL_PCIE_DBGC_CUR_DBGBUFF_STATUS;
		wrap_reg = BTINTEL_PCIE_DBGC_DBGBUFF_WRAP_ARND;
	} else if (hw_variant >= BTINTEL_HWID_SCP) {
		status_reg = BTINTEL_PCIE_DBGC_CUR_DBGBUFF_STATUS_SCP;
		wrap_reg = BTINTEL_PCIE_DBGC_DBGBUFF_WRAP_ARND_SCP;
	} else {
		bt_dev_err(hdev,
			   "Unsupported Intel hardware variant (0x%2.2x)",
			   hw_variant);
		return -EINVAL;
	}

	if (btintel_pcie_is_mdbgc_supported(data)) {
		ret = btintel_pcie_dump_dram_monitor(data, &dump_list, count,
						     mdbgc->buf1, status_reg,
						     wrap_reg,
						     BTINTEL_PCIE_MDBGC_ALLOCATIONID_1,
						     BTINTEL_PCIE_INI_ID_DRAM_MONITOR1,
						     "monitor");
		if (!ret)
			regions_mask |=
				BIT_ULL(BTINTEL_PCIE_INI_ID_DRAM_MONITOR1);
		else
			bt_dev_warn(hdev, "Failed to dump DRAM buf1: %d", ret);

		ret = btintel_pcie_dump_dram_monitor(data, &dump_list, count,
						     mdbgc->buf2, status_reg,
						     wrap_reg,
						     BTINTEL_PCIE_MDBGC_ALLOCATIONID_2,
						     BTINTEL_PCIE_INI_ID_DRAM_MONITOR2,
						     "monitor2");
		if (!ret)
			regions_mask |=
				BIT_ULL(BTINTEL_PCIE_INI_ID_DRAM_MONITOR2);
		else
			bt_dev_warn(hdev, "Failed to dump DRAM buf2: %d", ret);

		ret = btintel_pcie_dump_dram_monitor(data, &dump_list, count,
						     mdbgc->buf3, status_reg,
						     wrap_reg,
						     BTINTEL_PCIE_MDBGC_ALLOCATIONID_3,
						     BTINTEL_PCIE_INI_ID_DRAM_MONITOR3,
						     "monitor3");
		if (!ret)
			regions_mask |=
				BIT_ULL(BTINTEL_PCIE_INI_ID_DRAM_MONITOR3);
		else
			bt_dev_warn(hdev, "Failed to dump DRAM buf3: %d", ret);
	} else {
		ret = btintel_pcie_dump_dram_monitor(data, &dump_list, count,
						     dbgc->bufs, status_reg,
						     wrap_reg,
						     BTINTEL_PCIE_MDBGC_ALLOCATIONID_1,
						     BTINTEL_PCIE_INI_ID_DRAM_MONITOR1,
						     "monitor");
		if (!ret)
			regions_mask |=
				BIT_ULL(BTINTEL_PCIE_INI_ID_DRAM_MONITOR1);
		else
			bt_dev_warn(hdev,
				    "Failed to dump DRAM region: %d", ret);
	}

	if (dump_info->smem_size &&
	    !btintel_pcie_dump_smem_monitor_region(data, &dump_list,
						   BTINTEL_PCIE_INI_ID_SMEM,
						   "monitor_smem",
						   dump_info->smem_addr_start,
						   dump_info->smem_size))
		regions_mask |= BIT_ULL(BTINTEL_PCIE_INI_ID_SMEM);

	if (dump_info->exception_dump_addr && dump_info->exception_dump_len) {
		ret = btintel_pcie_dump_target_region(data, &dump_list,
						      BTINTEL_PCIE_INI_ID_EXCEPTION_EVT,
						      "EXCEPTION_EVT_BUFFER",
						      dump_info->exception_dump_addr,
						      dump_info->exception_dump_len);
		if (!ret)
			regions_mask |= BIT_ULL(BTINTEL_PCIE_INI_ID_EXCEPTION_EVT);
	}

	btintel_pcie_dump_mem_range(data, &dump_list,
				    BTINTEL_PCIE_INI_ID_DCCM, "DCCM",
				    dump_info->dccm_addr_start,
				    dump_info->dccm_size,
				    &regions_mask);

	btintel_pcie_dump_mem_range(data, &dump_list,
				    BTINTEL_PCIE_INI_ID_SDS, "SDS",
				    dump_info->sds_start_addr_start,
				    dump_info->sds_start_size,
				    &regions_mask);

	btintel_pcie_dump_mem_range(data, &dump_list,
				    BTINTEL_PCIE_INI_ID_SDS_IOSF, "SDS_IOSF",
				    dump_info->sds_iosf_data_addr_start,
				    dump_info->sds_iosf_data_size,
				    &regions_mask);

	btintel_pcie_dump_mem_range(data, &dump_list,
				    BTINTEL_PCIE_INI_ID_ECL, "ECL_REGION",
				    dump_info->ecl_addr_start,
				    dump_info->ecl_size,
				    &regions_mask);

	ret = btintel_pcie_dump_info(data, &dump_list, regions_mask);
	if (ret) {
		btintel_pcie_dump_list_free(&dump_list);
		return ret;
	}

	file_len = sizeof(*file_hdr) +
		   btintel_pcie_dump_list_total_size(&dump_list);
	entry = btintel_pcie_dump_entry_alloc(sizeof(*file_hdr));
	if (!entry) {
		btintel_pcie_dump_list_free(&dump_list);
		return -ENOMEM;
	}

	file_hdr = (void *)entry->data;
	file_hdr->barker = cpu_to_le32(BTINTEL_PCIE_INI_ERROR_DUMP_BARKER);
	file_hdr->file_len = cpu_to_le32(file_len);
	list_add(&entry->list, &dump_list);

	sg_dump_data = btintel_pcie_alloc_sgtable(file_len);
	if (sg_dump_data) {
		int sg_entries = sg_nents(sg_dump_data);
		u32 offs = 0;

		list_for_each_entry(entry, &dump_list, list) {
			sg_pcopy_from_buffer(sg_dump_data, sg_entries,
					     entry->data, entry->size, offs);
			offs += entry->size;
		}

		bt_dev_dbg(hdev, "triggering dev_coredumpsg()");
		dev_coredumpsg(&hdev->dev, sg_dump_data, file_len, GFP_KERNEL);
	} else {
		bt_dev_err(hdev, "Failed to allocate scatter-gather table for coredump");
		ret = -ENOMEM;
	}

	btintel_pcie_dump_list_free(&dump_list);
	return ret;
}

static void btintel_pcie_dump_traces(struct hci_dev *hdev)
{
	struct btintel_pcie_data *data = hci_get_drvdata(hdev);
	int ret;

	ret = btintel_pcie_get_mac_access(data);
	if (ret) {
		bt_dev_err(hdev, "Failed to get mac access: (%d)", ret);
		return;
	}

	ret = btintel_pcie_read_debug_regions(data);

	btintel_pcie_release_mac_access(data);

	if (ret)
		bt_dev_err(hdev, "Failed to dump traces: (%d)", ret);
}

static bool btintel_pcie_is_blazariw(struct pci_dev *pdev)
{
	return pdev->device == 0x4D76;
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

	/* On BlazarIW, the D0 entry to MAC init does not complete in
	 * time. Wait 50 ms (worst case as per HW analysis) for the
	 * shared hardware reset flow to complete before proceeding with
	 * MAC init.
	 */
	if (btintel_pcie_is_blazariw(data->pdev))
		msleep(50);

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

static inline bool btintel_pcie_in_device_halt(struct btintel_pcie_data *data)
{
	return data->boot_stage_cache & BTINTEL_PCIE_CSR_BOOT_STAGE_DEVICE_HALTED;
}

static void btintel_pcie_wr_sleep_cntrl(struct btintel_pcie_data *data,
					u32 dxstate)
{
	bt_dev_dbg(data->hdev, "writing sleep_ctl_reg: 0x%8.8x", dxstate);
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_IPC_SLEEP_CTL_REG, dxstate);
}

static int btintel_pcie_read_device_mem(struct btintel_pcie_data *data,
					void *buf, u32 dev_addr, int len)
{
	int err;
	u32 *val = buf;

	/* Get device mac access */
	err = btintel_pcie_get_mac_access(data);
	if (err) {
		bt_dev_err(data->hdev, "Failed to get mac access %d", err);
		return err;
	}

	for (; len > 0; len -= 4, dev_addr += 4, val++)
		*val = btintel_pcie_rd_dev_mem(data, dev_addr);

	btintel_pcie_release_mac_access(data);

	return 0;
}

static inline bool btintel_pcie_in_lockdown(struct btintel_pcie_data *data)
{
	return (data->boot_stage_cache &
		BTINTEL_PCIE_CSR_BOOT_STAGE_ROM_LOCKDOWN) ||
		(data->boot_stage_cache &
		 BTINTEL_PCIE_CSR_BOOT_STAGE_IML_LOCKDOWN);
}

static inline bool btintel_pcie_in_error(struct btintel_pcie_data *data)
{
	if (data->boot_stage_cache & BTINTEL_PCIE_CSR_BOOT_STAGE_DEVICE_WARNING)
		bt_dev_warn(data->hdev, "Controller device warning (boot_stage: 0x%8.8x)",
			    data->boot_stage_cache);

	return	data->boot_stage_cache & BTINTEL_PCIE_CSR_BOOT_STAGE_ABORT_HANDLER;
}

static u32 btintel_pcie_region_size(struct hci_dev *hdev, const char *name,
				    u32 start, u32 end)
{
	u64 size;

	if (!start || !end)
		return 0;

	if (end < start) {
		bt_dev_warn(hdev, "%s region invalid: start=0x%08x end=0x%08x",
			    name, start, end);
		return 0;
	}

	size = (u64)end + 0x04 - start;
	if (size % sizeof(u32))
		bt_dev_warn(hdev, "%s region size %llu is not 4-byte aligned",
			    name, size);

	if (size > BTINTEL_PCIE_REGION_MAX_SIZE) {
		bt_dev_warn(hdev, "%s region size %llu exceeds cap %u, ignoring",
			    name, size, BTINTEL_PCIE_REGION_MAX_SIZE);
		return 0;
	}

	return (u32)size;
}

static const char *btintel_pcie_tlv_str(u8 tlv_type)
{
	switch (tlv_type) {
	case BTINTEL_PCIE_TLV_TYPE_EXCEPTION_DUMP_ADDRESS:
		return "EXCEPTION_DUMP_ADDRESS";
	case BTINTEL_PCIE_TLV_TYPE_DCCM_MEM_ADDRESS:
		return "DCCM_MEM_ADDRESS";
	case BTINTEL_PCIE_TLV_TYPE_SDS_MEM_ADDRESS:
		return "SDS_MEM_ADDRESS";
	case BTINTEL_PCIE_TLV_TYPE_ECL_MEM_ADDRESS:
		return "ECL_MEM_ADDRESS";
	case BTINTEL_PCIE_TLV_TYPE_SMEM_ADDRESS:
		return "SMEM_ADDRESS";
	default:
		return "UNKNOWN";
	}
}

static int btintel_parse_mbox_tlv(struct btintel_pcie_data *data)
{
	/* Custom TLV structure for mailbox parsing
	 * len is __le16 as per agreement with FW
	 */
	struct mbox_tlv {
		u8 type;
		__le16 len;
		u8 val[];
	} __packed;

	u8 *buffer, *ptr;
	u32 buffer_len, remaining;
	int err;
	u32 tbl_addr, tbl_size;
	struct mbox_tlv *tlv;
	struct btintel_data *cnvi_data = hci_get_priv(data->hdev);
	u8 hw_variant = INTEL_HW_VARIANT(cnvi_data->cnvi_bt);
	long t;

	/* Wait for GP0 alive interrupt to post RX buffers */
	t = wait_event_timeout(data->mbox_parse_wait_q,
			       test_bit(BTINTEL_PCIE_MBOX_PARSE_READY, &data->flags),
			       msecs_to_jiffies(BTINTEL_PCIE_MBOX_INTR_TIMEOUT_MS));
	if (!t) {
		bt_dev_warn(data->hdev,
			    "Timeout (%u ms) waiting for alive interrupt before mbox TLV parse; skipping",
			    BTINTEL_PCIE_MBOX_INTR_TIMEOUT_MS);
		return 0;
	}
	clear_bit(BTINTEL_PCIE_MBOX_PARSE_READY, &data->flags);

	bt_dev_info(data->hdev,
		    "mbox TLV parse started at %lld ns (%lld us after mbox interrupt)",
		    ktime_to_ns(ktime_get()),
		    ktime_to_us(ktime_sub(ktime_get(), data->mbox_intr_ts)));

	memset(&data->dump_info, 0, sizeof(data->dump_info));

	/* Snapshot to avoid TOCTOU with the GP1 IRQ handler */
	tbl_size = READ_ONCE(data->debug_table_size);
	tbl_addr = READ_ONCE(data->debug_table_addr);

	if (!tbl_size || !tbl_addr)
		return -EINVAL;

	/* Ensure size is 4-byte aligned; btintel_pcie_read_device_mem()
	 * reads device memory in 4-byte units.
	 */
	tbl_size = ALIGN_DOWN(tbl_size, 4);

	if (!tbl_size)
		return -EINVAL;

	if (tbl_size > SZ_1M) {
		bt_dev_err(data->hdev, "Debug table size too large: %u",
			   tbl_size);
		return -EINVAL;
	}

	buffer_len = tbl_size;

	buffer = vmalloc(buffer_len);
	if (!buffer)
		return -ENOMEM;

	btintel_pcie_mac_init(data);

	err = btintel_pcie_read_device_mem(data, buffer, tbl_addr,
					   buffer_len);
	if (err)
		goto exit_on_error;

	print_hex_dump(KERN_INFO, "Bluetooth: mbox_tlv: ", DUMP_PREFIX_OFFSET, 16, 1,
		       buffer, buffer_len, false);

	ptr = buffer;
	remaining = buffer_len;

	/* Parse TLV structures: 1 byte type + 2 bytes length +
	 * variable value
	 */
	while (remaining >= sizeof(struct mbox_tlv)) {
		u16 tlv_len;
		u32 tlv_total;

		tlv = (struct mbox_tlv *)ptr;
		tlv_len = le16_to_cpu(tlv->len);
		tlv_total = sizeof(tlv->type) + sizeof(tlv->len) + tlv_len;

		if (tlv_total > remaining) {
			bt_dev_err(data->hdev, "TLV parse error: not enough data for TLV value (type=%u, len=%u)",
				   tlv->type, tlv_len);
			break;
		}

		switch (tlv->type) {
		case BTINTEL_PCIE_TLV_TYPE_EXCEPTION_DUMP_ADDRESS:
			if (tlv_len < 8) {
				bt_dev_err(data->hdev,
					   "TLV %s too short: len=%u (need 8)",
					   btintel_pcie_tlv_str(tlv->type),
					   tlv_len);
				break;
			}
			data->dump_info.exception_dump_addr =
				get_unaligned_le32(&tlv->val[0]);
			data->dump_info.exception_dump_len =
				get_unaligned_le32(&tlv->val[4]);
			if (data->dump_info.exception_dump_len >
			    BTINTEL_PCIE_REGION_MAX_SIZE) {
				bt_dev_warn(data->hdev,
					    "EXCEPTION_EVT_BUFFER size %u exceeds cap %u, ignoring",
					    data->dump_info.exception_dump_len,
					    BTINTEL_PCIE_REGION_MAX_SIZE);
				data->dump_info.exception_dump_addr = 0;
				data->dump_info.exception_dump_len = 0;
			}
			break;
		case BTINTEL_PCIE_TLV_TYPE_DCCM_MEM_ADDRESS:
			if (tlv_len < 8) {
				bt_dev_err(data->hdev,
					   "TLV %s too short: len=%u (need 8)",
					   btintel_pcie_tlv_str(tlv->type),
					   tlv_len);
				break;
			}
			data->dump_info.dccm_addr_start =
				get_unaligned_le32(&tlv->val[0]);
			data->dump_info.dccm_size =
				btintel_pcie_region_size(data->hdev, "DCCM",
							 data->dump_info.dccm_addr_start,
							 get_unaligned_le32(&tlv->val[4]));
			if (!data->dump_info.dccm_size)
				data->dump_info.dccm_addr_start = 0;
			break;
		case BTINTEL_PCIE_TLV_TYPE_SDS_MEM_ADDRESS: {
			struct btintel_pcie_dump_mem_info *di = &data->dump_info;

			/* hw_variant comes from cnvi_bt which is set during
			 * setup. If mailbox fires before setup completes,
			 * hw_variant is 0. Skip SDS parsing in that case.
			 */
			if (!hw_variant) {
				bt_dev_dbg(data->hdev, "SDS TLV: skipped, hw_variant not yet known");
				break;
			}
			if (tlv_len == 16 && hw_variant > BTINTEL_HWID_BZRI) {
				di->sds_start_addr_start =
					get_unaligned_le32(&tlv->val[0]);
				di->sds_start_size =
					btintel_pcie_region_size(data->hdev, "SDS",
								 di->sds_start_addr_start,
								 get_unaligned_le32(&tlv->val[4]));
				di->sds_iosf_data_addr_start =
					get_unaligned_le32(&tlv->val[8]);
				di->sds_iosf_data_size =
					btintel_pcie_region_size(data->hdev, "SDS_IOSF",
								 di->sds_iosf_data_addr_start,
								 get_unaligned_le32(&tlv->val[12]));
			} else if (tlv_len == 24 &&
				   (hw_variant == BTINTEL_HWID_BZRI ||
				    hw_variant == BTINTEL_HWID_BZRIW)) {
				di->sds_fixed_rom_addr_start =
					get_unaligned_le32(&tlv->val[0]);
				di->sds_fixed_rom_size =
					btintel_pcie_region_size(data->hdev, "SDS_FIXED_ROM",
								 di->sds_fixed_rom_addr_start,
								 get_unaligned_le32(&tlv->val[4]));
				di->sds_start_addr_start =
					get_unaligned_le32(&tlv->val[8]);
				di->sds_start_size =
					btintel_pcie_region_size(data->hdev, "SDS",
								 di->sds_start_addr_start,
								 get_unaligned_le32(&tlv->val[12]));
				di->sds_iosf_data_addr_start =
					get_unaligned_le32(&tlv->val[16]);
				di->sds_iosf_data_size =
					btintel_pcie_region_size(data->hdev, "SDS_IOSF",
								 di->sds_iosf_data_addr_start,
								 get_unaligned_le32(&tlv->val[20]));
			} else {
				bt_dev_err(data->hdev,
					   "SDS TLV: hw=0x%2.2x len=%u",
					   hw_variant, tlv_len);
			}
			if (!di->sds_start_size)
				di->sds_start_addr_start = 0;
			if (!di->sds_iosf_data_size)
				di->sds_iosf_data_addr_start = 0;
			if (!di->sds_fixed_rom_size)
				di->sds_fixed_rom_addr_start = 0;
			break;
		}
		case BTINTEL_PCIE_TLV_TYPE_ECL_MEM_ADDRESS:
			if (tlv_len < 8) {
				bt_dev_err(data->hdev,
					   "TLV %s too short: len=%u (need 8)",
					   btintel_pcie_tlv_str(tlv->type),
					   tlv_len);
				break;
			}
			data->dump_info.ecl_addr_start =
				get_unaligned_le32(&tlv->val[0]);
			data->dump_info.ecl_size =
				btintel_pcie_region_size(data->hdev, "ECL",
							 data->dump_info.ecl_addr_start,
							 get_unaligned_le32(&tlv->val[4]));
			if (!data->dump_info.ecl_size)
				data->dump_info.ecl_addr_start = 0;
			break;
		case BTINTEL_PCIE_TLV_TYPE_SMEM_ADDRESS:
			if (tlv_len < 8) {
				bt_dev_err(data->hdev,
					   "TLV %s too short: len=%u (need 8)",
					   btintel_pcie_tlv_str(tlv->type),
					   tlv_len);
				break;
			}
			data->dump_info.smem_addr_start =
				get_unaligned_le32(&tlv->val[0]);
			data->dump_info.smem_size =
				btintel_pcie_region_size(data->hdev, "SMEM",
							 data->dump_info.smem_addr_start,
							 get_unaligned_le32(&tlv->val[4]));
			if (!data->dump_info.smem_size)
				data->dump_info.smem_addr_start = 0;
			break;
		default:
			bt_dev_dbg(data->hdev, "Unknown TLV type: %u length: %u",
				   tlv->type, tlv_len);
			break;
		}

		/* Move to next TLV */
		ptr += tlv_total;
		remaining -= tlv_total;
	}

	bt_dev_info(data->hdev,
		   "exception_dump: addr:0x%08x len:0x%08x",
		   data->dump_info.exception_dump_addr,
		   data->dump_info.exception_dump_len);
	bt_dev_info(data->hdev,
		   "dccm: start:0x%8.8x size:%u",
		   data->dump_info.dccm_addr_start,
		   data->dump_info.dccm_size);
	bt_dev_info(data->hdev,
		   "sds_fixed_rom: start:0x%8.8x size:%u",
		   data->dump_info.sds_fixed_rom_addr_start,
		   data->dump_info.sds_fixed_rom_size);
	bt_dev_info(data->hdev,
		   "sds: start:0x%8.8x size:%u",
		   data->dump_info.sds_start_addr_start,
		   data->dump_info.sds_start_size);
	bt_dev_info(data->hdev,
		   "sds_iosf: start:0x%8.8x size:%u",
		   data->dump_info.sds_iosf_data_addr_start,
		   data->dump_info.sds_iosf_data_size);
	bt_dev_info(data->hdev,
		   "ecl: start:0x%8.8x size:%u",
		   data->dump_info.ecl_addr_start,
		   data->dump_info.ecl_size);
	bt_dev_info(data->hdev,
		   "smem: start:0x%8.8x size:%u",
		   data->dump_info.smem_addr_start,
		   data->dump_info.smem_size);

	vfree(buffer);
	return 0;

exit_on_error:
	vfree(buffer);
	return err;
}

static void btintel_pcie_msix_gp1_handler(struct btintel_pcie_data *data)
{
	bool target_access = false;
	u32 addr = 0, size = 0;

	/* Read the Mail box status and registers */
	data->mbox.mbox_status = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_MBOX_STATUS_REG);
	if (data->mbox.mbox_status & BTINTEL_PCIE_CSR_MBOX_STATUS_MBOX1) {
		data->mbox.mbox1 = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_MBOX_1_REG);
		if (data->mbox.mbox1 ==
		    BTINTEL_PCIE_BUILD_SPECIFIC_RESOURCES_MAPPING) {
			bt_dev_info(data->hdev,
				    "mailbox for target access");
			target_access = true;
		}
	}

	if (data->mbox.mbox_status & BTINTEL_PCIE_CSR_MBOX_STATUS_MBOX2) {
		data->mbox.mbox2 = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_MBOX_2_REG);
		if (target_access)
			addr = data->mbox.mbox2;
	}

	if (data->mbox.mbox_status & BTINTEL_PCIE_CSR_MBOX_STATUS_MBOX3) {
		data->mbox.mbox3 = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_MBOX_3_REG);
		if (target_access)
			size = data->mbox.mbox3;
	}

	if (data->mbox.mbox_status & BTINTEL_PCIE_CSR_MBOX_STATUS_MBOX4)
		data->mbox.mbox4 = btintel_pcie_rd_reg32(data, BTINTEL_PCIE_CSR_MBOX_4_REG);

	bt_dev_dbg(data->hdev,
		   "GP1: sts:0x%08x mb1:0x%08x mb2:0x%08x mb3:0x%08x mb4:0x%08x",
		   data->mbox.mbox_status, data->mbox.mbox1,
		   data->mbox.mbox2, data->mbox.mbox3,
		   data->mbox.mbox4);

	if (target_access &&
	    !test_and_set_bit(BTINTEL_PCIE_MAIL_BOX_INTR,
				   &data->flags)) {
		/* Arm the mbox<->alive handshake */
		clear_bit(BTINTEL_PCIE_MBOX_PARSE_READY, &data->flags);
		set_bit(BTINTEL_PCIE_MBOX_PARSE_PENDING, &data->flags);
		data->mbox_intr_ts = ktime_get();

		bt_dev_info(data->hdev,
			    "mbox interrupt received at %lld ns; queuing mbox_work",
			    ktime_to_ns(data->mbox_intr_ts));

		WRITE_ONCE(data->debug_table_addr, addr);
		WRITE_ONCE(data->debug_table_size, size);
		if (!queue_work(data->dump_workqueue,
				&data->mbox_work)) {
			clear_bit(BTINTEL_PCIE_MAIL_BOX_INTR, &data->flags);
			clear_bit(BTINTEL_PCIE_MBOX_PARSE_PENDING, &data->flags);
		}
	}

	/* Mailbox is read, ack to FW */
	btintel_pcie_set_reg_bits(data,
				  BTINTEL_PCIE_CSR_IPC_DOORBELL_VEC_REG,
				  BTINTEL_PCIE_CSR_DOORBELL_MBOX_READ_CONFIRM);
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

	if (btintel_pcie_in_error(data)) {
		bt_dev_err(data->hdev, "Controller in error state (boot_stage: 0x%8.8x)",
			   data->boot_stage_cache);
		btintel_pcie_dump_debug_registers(data->hdev);
		return;
	}

	if (btintel_pcie_in_lockdown(data)) {
		bt_dev_err(data->hdev, "Controller in lockdown state");
		btintel_pcie_dump_debug_registers(data->hdev);
		return;
	}

	data->gp0_received = true;

	old_ctxt = data->alive_intr_ctxt;
	submit_rx = false;
	signal_waitq = false;

	/* Serialize the alive_intr_ctxt claim against set_dxstate()'s
	 * register-based fallback, which can run concurrently.
	 */
	spin_lock(&data->irq_lock);
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
			signal_waitq = true;
			break;
		}

		if (btintel_pcie_in_iml(data)) {
			submit_rx = true;
			signal_waitq = true;
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
	spin_unlock(&data->irq_lock);

	if (submit_rx) {
		btintel_pcie_reset_ia(data);
		btintel_pcie_start_rx(data);

		/* Complete the mbox<->alive handshake */
		if (test_and_clear_bit(BTINTEL_PCIE_MBOX_PARSE_PENDING, &data->flags)) {
			set_bit(BTINTEL_PCIE_MBOX_PARSE_READY, &data->flags);
			wake_up(&data->mbox_parse_wait_q);
		}
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

	if (cr_hia >= txq->count) {
		bt_dev_err(data->hdev, "TXQ: invalid cr_hia %u", cr_hia);
		return;
	}

	while (cr_tia != cr_hia) {
		data->tx_wait_done = true;
		wake_up(&data->tx_wait_q);

		urbd0 = &txq->urbd0s[cr_tia];

		if (urbd0->tfd_index >= txq->count)
			return;

		cr_tia = (cr_tia + 1) % txq->count;
		data->ia.cr_tia[BTINTEL_PCIE_TXQ_NUM] = cr_tia;
		ipc_print_ia_ring(data->hdev, &data->ia, BTINTEL_PCIE_TXQ_NUM);
	}
}

static int btintel_pcie_recv_event(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_event_hdr *hdr = (void *)skb->data;
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

		/* This is a debug event that comes from IML and OP image when it
		 * starts execution. There is no need pass this event to stack.
		 */
		if (skb->data[2] == 0x97) {
			hci_recv_diag(hdev, skb);
			return 0;
		}
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
	u32 plen;
	u32 pcie_pkt_type;
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

	hci_skb_pkt_type(skb) = pkt_type;
	hdev->stat.byte_rx += plen;
	skb_trim(skb, plen);

	if (pcie_pkt_type == BTINTEL_PCIE_HCI_EVT_PKT)
		ret = btintel_pcie_recv_event(hdev, skb);
	else
		ret = hci_recv_frame(hdev, skb);
	skb = NULL; /* skb is freed in the callee  */

exit_error:
	kfree_skb(skb);

	if (ret)
		hdev->stat.err_rx++;

	spin_unlock(&data->hci_rx_lock);

	return ret;
}

static void btintel_pcie_read_hwexp(struct btintel_pcie_data *data)
{
	int err, offset, pending;
	u32 len;
	struct sk_buff *skb;
	u8 *buf, prefix[64];
	u32 addr, val;
	u16 pkt_len;

	struct tlv {
		u8	type;
		__le16	len;
		u8	val[];
	} __packed;

	struct tlv *tlv;

	switch (data->dmp_hdr.cnvi_top & 0xfff) {
	case BTINTEL_CNVI_BLAZARI:
	case BTINTEL_CNVI_BLAZARIW:
		/* only from step B0 onwards */
		if (INTEL_CNVX_TOP_STEP(data->dmp_hdr.cnvi_top) != 0x01)
			return;
		break;
	case BTINTEL_CNVI_SCP:
	case BTINTEL_CNVI_SCP2:
	case BTINTEL_CNVI_SCP2_C0:
	case BTINTEL_CNVI_SCP2F:
	case BTINTEL_CNVI_DRACO:
		break;
	default:
		bt_dev_err(data->hdev, "Unsupported cnvi 0x%8.8x", data->dmp_hdr.cnvi_top);
		return;
	}

	len = data->dump_info.exception_dump_len;
	addr = data->dump_info.exception_dump_addr;

	if (!addr || len < sizeof(__le32) || len > SZ_4K) {
		bt_dev_err(data->hdev, "Invalid exception address: 0x%8.8x or length: %u",
			   addr, len);
		return;
	}

	/* Ensure size is 4-byte aligned; btintel_pcie_read_device_mem()
	 * reads device memory in 4-byte units.
	 */
	len = ALIGN_DOWN(len, 4);

	buf = kzalloc(len, GFP_KERNEL);
	if (!buf)
		goto exit_on_error;

	btintel_pcie_mac_init(data);

	err = btintel_pcie_read_device_mem(data, buf, addr, len);
	if (err)
		goto exit_on_error;

	val = get_unaligned_le32(buf);
	if (val != BTINTEL_PCIE_MAGIC_NUM) {
		bt_dev_err(data->hdev, "Invalid exception dump signature: 0x%8.8x",
			   val);
		goto exit_on_error;
	}

	snprintf(prefix, sizeof(prefix), "Bluetooth: %s: ", bt_dev_name(data->hdev));

	offset = 4;
	do {
		pending = len - offset;
		if (pending < sizeof(*tlv))
			break;
		tlv = (struct tlv *)(buf + offset);

		/* If type == 0, then there are no more TLVs to be parsed */
		if (!tlv->type) {
			bt_dev_dbg(data->hdev, "Invalid TLV type 0");
			break;
		}
		pkt_len = le16_to_cpu(tlv->len);
		offset += sizeof(*tlv);
		pending = len - offset;
		if (pkt_len > pending)
			break;

		offset += pkt_len;

		 /* Only TLVs of type == 1 are HCI events, no need to process other
		  * TLVs
		  */
		if (tlv->type != 1)
			continue;

		bt_dev_dbg(data->hdev, "TLV packet length: %u", pkt_len);
		if (pkt_len > HCI_MAX_EVENT_SIZE)
			break;
		skb = bt_skb_alloc(pkt_len, GFP_KERNEL);
		if (!skb)
			goto exit_on_error;
		hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
		skb_put_data(skb, tlv->val, pkt_len);

		/* copy Intel specific pcie packet type */
		val = BTINTEL_PCIE_HCI_EVT_PKT;
		memcpy(skb_push(skb, BTINTEL_PCIE_HCI_TYPE_LEN), &val,
		       BTINTEL_PCIE_HCI_TYPE_LEN);

		print_hex_dump(KERN_DEBUG, prefix, DUMP_PREFIX_OFFSET, 16, 1,
			       tlv->val, pkt_len, false);

		btintel_pcie_recv_frame(data, skb);
	} while (offset < len);

exit_on_error:
	kfree(buf);
}

static int btintel_pcie_dump_fwtrigger_event(struct btintel_pcie_data *data)
{
	struct btintel_pcie_fwtrigger_evt *evt;
	struct sk_buff *skb;
	unsigned int len;
	int err;
	u8 *buf;

	if (!data->debug_evt_size || !data->debug_evt_addr)
		return -EINVAL;

	len = data->debug_evt_size;

	len = ALIGN_DOWN(len, 4);

	if (len < sizeof(*evt) || len > HCI_MAX_EVENT_SIZE) {
		bt_dev_err(data->hdev, "Invalid FW trigger data size (%u bytes)", len);
		return -EINVAL;
	}

	buf = kzalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	btintel_pcie_mac_init(data);

	err = btintel_pcie_read_device_mem(data, buf, data->debug_evt_addr,
					   len);
	if (err)
		goto exit_on_error;

	evt = (void *)buf;
	bt_dev_dbg(data->hdev, "event type: 0x%2.2x event id: 0x%4.4x len: %u",
		   evt->event_type, le16_to_cpu(evt->event_id), len);

	skb = bt_skb_alloc(len, GFP_KERNEL);
	if (!skb) {
		err = -ENOMEM;
		goto exit_on_error;
	}
	skb_put_data(skb, buf, len);

	hci_recv_diag(data->hdev, skb);
	err = 0;

exit_on_error:
	kfree(buf);
	return err;
}

/* Queue a coredump dump_traces() pass.
 *
 * Returns true if a new coredump was queued, false if one was already
 * in-flight (the BTINTEL_PCIE_COREDUMP_INPROGRESS bit serves as the
 * single-writer guard for the @coredump_work item) or the workqueue is
 * disabled (reset / remove in progress).
 *
 * Always queue this AFTER any companion event-reader work (hwexp /
 * fwtrigger) so that, on the ordered @dump_workqueue, the event reader
 * runs first and the trigger metadata is populated before dump_traces
 * consumes it.
 */
static bool btintel_pcie_queue_coredump(struct btintel_pcie_data *data,
					u16 trigger_reason)
{
	if (test_and_set_bit(BTINTEL_PCIE_COREDUMP_INPROGRESS, &data->flags))
		return false;

	data->dmp_hdr.trigger_reason = trigger_reason;

	if (queue_work(data->dump_workqueue, &data->coredump_work))
		return true;

	/* Workqueue is disabled (reset/remove drained it). Release the
	 * guard so a later trigger, after re-probe, can succeed.
	 */
	clear_bit(BTINTEL_PCIE_COREDUMP_INPROGRESS, &data->flags);
	return false;
}

static void btintel_pcie_msix_fw_trigger_handler(struct btintel_pcie_data *data)
{
	bt_dev_dbg(data->hdev, "Received firmware smart trigger cause");

	/* Per-work guard: deduplicate concurrent FW-trigger interrupts.
	 * Cleared at the tail of btintel_pcie_fwtrigger_worker().
	 */
	if (test_and_set_bit(BTINTEL_PCIE_FWTRIGGER_DUMP_INPROGRESS,
			     &data->flags))
		return;

	if (!queue_work(data->dump_workqueue, &data->fwtrigger_work)) {
		clear_bit(BTINTEL_PCIE_FWTRIGGER_DUMP_INPROGRESS, &data->flags);
		return;
	}

	/* Queue coredump after the fwtrigger event reader so dmp_hdr.event_*
	 * is populated before dump_traces consumes it.
	 */
	btintel_pcie_queue_coredump(data, BTINTEL_PCIE_TRIGGER_REASON_FW_ASSERT);
}

static void btintel_pcie_msix_hw_exp_handler(struct btintel_pcie_data *data)
{
	bt_dev_err(data->hdev, "Received hw exception interrupt");

	/* CORE_HALTED is the single-writer guard for this handler. It is
	 * set once on first HW exception and cleared only by re-probe
	 * (data is reallocated), so it also serializes hwexp_work
	 * scheduling without needing a separate bit.
	 */
	if (test_and_set_bit(BTINTEL_PCIE_CORE_HALTED, &data->flags))
		return;

	/* Queue companion coredump first so it is appended after hwexp_work
	 * on the ordered @dump_workqueue (preserves the original
	 * coredump-then-hwexp ordering).
	 */
	btintel_pcie_queue_coredump(data, BTINTEL_PCIE_TRIGGER_REASON_FW_ASSERT);

	queue_work(data->dump_workqueue, &data->hwexp_work);
}

static void btintel_pcie_coredump_worker(struct work_struct *work)
{
	struct btintel_pcie_data *data = container_of(work,
					struct btintel_pcie_data, coredump_work);

	/* hdev is NULL until setup_hdev() succeeds, and is cleared on
	 * teardown after disable_work_sync() drains us; bail in that case.
	 */
	if (!data->hdev)
		goto out;

	/* When firmware routes debug traces to the WiFi DBGC, no host
	 * DBGC buffers were allocated, so there is nothing to dump here.
	 */
	if (btintel_pcie_dbg_to_wifi(data)) {
		bt_dev_info(data->hdev,
			    "Skipping coredump: debug traces routed to WiFi DBGC");
		goto out;
	}

	btintel_pcie_dump_traces(data->hdev);
out:
	/* Release guard last so a new trigger can run only after this
	 * pass has fully completed (including dev_coredumpv()).
	 */
	clear_bit(BTINTEL_PCIE_COREDUMP_INPROGRESS, &data->flags);
}

static void btintel_pcie_hwexp_worker(struct work_struct *work)
{
	struct btintel_pcie_data *data = container_of(work,
					struct btintel_pcie_data, hwexp_work);

	if (!data->hdev)
		return;

	/* Unlike usb products, controller will not send hardware exception
	 * event on exception. Instead controller writes the hardware event
	 * to device memory along with optional debug events, raises MSIX
	 * and halts. Driver shall read the exception event from device
	 * memory and passes it to the stack for further processing.
	 *
	 * Re-entry is gated by BTINTEL_PCIE_CORE_HALTED in the IRQ
	 * handler, which is only cleared by re-probe; no per-work bit
	 * is needed here.
	 */
	btintel_pcie_read_hwexp(data);
}

static void btintel_pcie_fwtrigger_worker(struct work_struct *work)
{
	struct btintel_pcie_data *data = container_of(work,
					struct btintel_pcie_data, fwtrigger_work);
	int err;

	if (!data->hdev)
		goto out;

	err = btintel_pcie_dump_fwtrigger_event(data);
	if (err)
		bt_dev_warn(data->hdev, "failed to log fwtrigger event");
out:
	/* Release guard last; matches set in fw_trigger handler. */
	clear_bit(BTINTEL_PCIE_FWTRIGGER_DUMP_INPROGRESS, &data->flags);
}

static void btintel_pcie_mbox_worker(struct work_struct *work)
{
	struct btintel_pcie_data *data = container_of(work,
					struct btintel_pcie_data, mbox_work);

	if (!data->hdev)
		goto out;

	btintel_parse_mbox_tlv(data);
out:
	/* Release guard last; matches set in gp1 handler. */
	clear_bit(BTINTEL_PCIE_MAIL_BOX_INTR, &data->flags);
}

static void btintel_pcie_rx_work(struct work_struct *work)
{
	struct btintel_pcie_data *data = container_of(work,
					struct btintel_pcie_data, rx_work);
	struct sk_buff *skb;

	/* Process the sk_buf in queue and send to the HCI layer */
	while ((skb = skb_dequeue(&data->rx_skb_q))) {
		btintel_pcie_recv_frame(data, skb);
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
	if (len == 0 || len > BTINTEL_PCIE_BUFFER_SIZE - sizeof(*rfh_hdr)) {
		bt_dev_err(data->hdev, "Invalid packet_len %d (max %zu)", len,
			   BTINTEL_PCIE_BUFFER_SIZE - sizeof(*rfh_hdr));
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
	if (cr_tia == cr_hia)
		return;

	rxq = &data->rxq;

	if (cr_hia >= rxq->count) {
		bt_dev_err(hdev, "RXQ: invalid cr_hia %u", cr_hia);
		return;
	}

	/* The firmware sends multiple CD in a single MSI-X and it needs to
	 * process all received CDs in this interrupt.
	 */
	while (cr_tia != cr_hia) {
		urbd1 = &rxq->urbd1s[cr_tia];
		ipc_print_urbd1(data->hdev, urbd1, cr_tia);

		if (urbd1->frbd_tag >= rxq->count) {
			bt_dev_err(hdev, "RXQ: invalid frbd_tag %u",
				   urbd1->frbd_tag);
			return;
		}

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

static inline bool btintel_pcie_is_rxq_empty(struct btintel_pcie_data *data)
{
	return data->ia.cr_hia[BTINTEL_PCIE_RXQ_NUM] == data->ia.cr_tia[BTINTEL_PCIE_RXQ_NUM];
}

static inline bool btintel_pcie_is_txackq_empty(struct btintel_pcie_data *data)
{
	return data->ia.cr_tia[BTINTEL_PCIE_TXQ_NUM] == data->ia.cr_hia[BTINTEL_PCIE_TXQ_NUM];
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
		bt_warn_ratelimited("Bluetooth: btintel_pcie: Received spurious interrupt\n");
		btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_MSIX_AUTOMASK_ST,
				      BIT(entry->entry));
		return IRQ_NONE;
	}

	/* This interrupt is raised when there is an hardware exception */
	if (intr_hw & BTINTEL_PCIE_MSIX_HW_INT_CAUSES_HWEXP)
		btintel_pcie_msix_hw_exp_handler(data);

	if (intr_hw & BTINTEL_PCIE_MSIX_HW_INT_CAUSES_GP1)
		btintel_pcie_msix_gp1_handler(data);


	/* For TX */
	if (intr_fh & BTINTEL_PCIE_MSIX_FH_INT_CAUSES_0) {
		btintel_pcie_msix_tx_handle(data);
		if (!btintel_pcie_is_rxq_empty(data))
			btintel_pcie_msix_rx_handle(data);
	}

	/* For RX */
	if (intr_fh & BTINTEL_PCIE_MSIX_FH_INT_CAUSES_1) {
		btintel_pcie_msix_rx_handle(data);
		if (!btintel_pcie_is_txackq_empty(data))
			btintel_pcie_msix_tx_handle(data);
	}

	if (intr_hw & BTINTEL_PCIE_MSIX_HW_INT_CAUSES_FWTRIG)
		btintel_pcie_msix_fw_trigger_handler(data);

	/* This interrupt is triggered by the firmware after updating
	 * boot_stage register and image_response register
	 */
	if (intr_hw & BTINTEL_PCIE_MSIX_HW_INT_CAUSES_GP0)
		btintel_pcie_msix_gp0_handler(data);

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
	int num_irqs, i, j;

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
						NULL,
						btintel_pcie_irq_msix_handler,
						IRQF_ONESHOT | IRQF_SHARED,
						KBUILD_MODNAME,
						msix_entry);
		if (err) {
			for (j = 0; j < i; j++)
				devm_free_irq(&data->pdev->dev,
					      data->msix_entries[j].vector,
					      &data->msix_entries[j]);
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
	{ BTINTEL_PCIE_MSIX_HW_INT_CAUSES_GP0,	BTINTEL_PCIE_CSR_MSIX_HW_INT_MASK,	0x20 },
	{ BTINTEL_PCIE_MSIX_HW_INT_CAUSES_HWEXP, BTINTEL_PCIE_CSR_MSIX_HW_INT_MASK,	0x23 },
	{ BTINTEL_PCIE_MSIX_HW_INT_CAUSES_FWTRIG, BTINTEL_PCIE_CSR_MSIX_HW_INT_MASK,	0x25 },
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

	/* Do shared hardware reset to ensure a clean start before
	 * configuring interrupts.
	 */
	err = btintel_pcie_reset_bt(data);
	if (err)
		return err;

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

	ci->dbg_output_mode = btintel_pcie_dbg_to_wifi(data) ?
			      BTINTEL_PCIE_WIFI_DBGC : BTINTEL_PCIE_DRAM;
	if (btintel_pcie_dbg_to_wifi(data)) {
		/* Firmware forwards debug traces to the WiFi DBGC, so no
		 * host DBGC buffer is needed; leave dbgc_addr/size as 0.
		 */
		ci->dbgc_addr = 0;
		ci->dbgc_size = 0;
	} else if (btintel_pcie_is_mdbgc_supported(data)) {
		ci->dbgc_addr = data->mdbgc.frag_p_addr;
		ci->dbgc_size = data->mdbgc.frag_size;
	} else {
		ci->dbgc_addr = data->dbgc.frag_p_addr;
		ci->dbgc_size = data->dbgc.frag_size;
	}

	ci->dbg_preset = 0x00;
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
	txq->bufs = kmalloc_objs(*buf, txq->count);
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
	rxq->bufs = kmalloc_objs(*buf, rxq->count);
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
	size_t tfd_size, frbd_size, ctx_size, ci_size, urbd0_size, urbd1_size;

	/* Allocate the chunk of DMA memory for descriptors, index array, and
	 * context information, instead of allocating individually.
	 * The DMA memory for data buffer is allocated while setting up the
	 * each queue.
	 *
	 * Total size is sum of the following and each of the individual sizes
	 * are aligned to 128 bytes before adding up.
	 *
	 *  + size of TFD * Number of descriptors in queue
	 *  + size of URBD0 * Number of descriptors in queue
	 *  + size of FRBD * Number of descriptors in queue
	 *  + size of URBD1 * Number of descriptors in queue
	 *  + size of index * Number of queues(2) * type of index array(4)
	 *  + size of context information
	 */
	tfd_size = ALIGN(sizeof(struct tfd) * BTINTEL_PCIE_TX_DESCS_COUNT,
			 BTINTEL_PCIE_DMA_ALIGN_128B);
	urbd0_size = ALIGN(sizeof(struct urbd0) * BTINTEL_PCIE_TX_DESCS_COUNT,
			   BTINTEL_PCIE_DMA_ALIGN_128B);

	frbd_size = ALIGN(sizeof(struct frbd) * BTINTEL_PCIE_RX_DESCS_COUNT,
			  BTINTEL_PCIE_DMA_ALIGN_128B);
	urbd1_size = ALIGN(sizeof(struct urbd1) * BTINTEL_PCIE_RX_DESCS_COUNT,
			   BTINTEL_PCIE_DMA_ALIGN_128B);

	ci_size = ALIGN(sizeof(u16) * BTINTEL_PCIE_NUM_QUEUES,
			BTINTEL_PCIE_DMA_ALIGN_128B);

	ctx_size = ALIGN(sizeof(struct ctx_info), BTINTEL_PCIE_DMA_ALIGN_128B);

	total = tfd_size + urbd0_size + frbd_size + urbd1_size + ctx_size + ci_size * 4;

	data->dma_pool = dma_pool_create(KBUILD_MODNAME, &data->pdev->dev,
					 total, BTINTEL_PCIE_DMA_ALIGN_128B, 0);
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
	data->txq.count = BTINTEL_PCIE_TX_DESCS_COUNT;
	data->rxq.count = BTINTEL_PCIE_RX_DESCS_COUNT;

	/* Setup tfds */
	data->txq.tfds_p_addr = p_addr;
	data->txq.tfds = v_addr;

	p_addr += tfd_size;
	v_addr += tfd_size;

	/* Setup urbd0 */
	data->txq.urbd0s_p_addr = p_addr;
	data->txq.urbd0s = v_addr;

	p_addr += urbd0_size;
	v_addr += urbd0_size;

	/* Setup FRBD*/
	data->rxq.frbds_p_addr = p_addr;
	data->rxq.frbds = v_addr;

	p_addr += frbd_size;
	v_addr += frbd_size;

	/* Setup urbd1 */
	data->rxq.urbd1s_p_addr = p_addr;
	data->rxq.urbd1s = v_addr;

	p_addr += urbd1_size;
	v_addr += urbd1_size;

	/* Setup data buffers for txq */
	err = btintel_pcie_setup_txq_bufs(data, &data->txq);
	if (err)
		goto exit_error_pool;

	/* Setup data buffers for rxq */
	err = btintel_pcie_setup_rxq_bufs(data, &data->rxq);
	if (err)
		goto exit_error_txq;

	/* TR Head Index Array */
	data->ia.tr_hia_p_addr = p_addr;
	data->ia.tr_hia = v_addr;
	p_addr += ci_size;
	v_addr += ci_size;

	/* TR Tail Index Array */
	data->ia.tr_tia_p_addr = p_addr;
	data->ia.tr_tia = v_addr;
	p_addr += ci_size;
	v_addr += ci_size;

	/* CR Head index Array */
	data->ia.cr_hia_p_addr = p_addr;
	data->ia.cr_hia = v_addr;
	p_addr += ci_size;
	v_addr += ci_size;

	/* CR Tail Index Array */
	data->ia.cr_tia_p_addr = p_addr;
	data->ia.cr_tia = v_addr;
	p_addr += ci_size;
	v_addr += ci_size;

	/* Setup data buffers for dbgc */
	if (btintel_pcie_dbg_to_wifi(data)) {
		/* Firmware routes traces to the WiFi DBGC; skip host DBGC
		 * buffer allocation entirely.
		 */
		err = 0;
	} else if (btintel_pcie_is_mdbgc_supported(data)) {
		err = btintel_pcie_setup_mdbgc(data);
	} else {
		err = btintel_pcie_setup_dbgc(data);
	}
	if (err)
		goto exit_error_txq;

	/* Setup Context Information */
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

	if (test_bit(BTINTEL_PCIE_CORE_HALTED, &data->flags))
		return -ENODEV;

	if (test_bit(BTINTEL_PCIE_RECOVERY_IN_PROGRESS, &data->flags))
		return -ENODEV;

	/* Account for the 4-byte PCIe type header prepended before the
	 * DMA copy.  Written as a subtraction to avoid wrap-around on
	 * attacker-controlled skb->len.
	 */
	if (skb->len > BTINTEL_PCIE_BUFFER_SIZE - BTINTEL_PCIE_HCI_TYPE_LEN) {
		bt_dev_err(hdev, "Packet too large: %u > %u", skb->len,
			   BTINTEL_PCIE_BUFFER_SIZE - BTINTEL_PCIE_HCI_TYPE_LEN);
		return -EMSGSIZE;
	}

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

			/* When the BTINTEL_HCI_OP_RESET command is issued to
			 * boot into the operational firmware, it will actually
			 * not send a command complete event. To keep the flow
			 * control working inject that event here.
			 */
			if (opcode == BTINTEL_HCI_OP_RESET)
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

	ret = btintel_pcie_send_sync(data, skb, type, opcode);
	if (ret) {
		hdev->stat.err_tx++;
		bt_dev_err(hdev, "Failed to send frame (%d)", ret);
		goto exit_error;
	}

	hdev->stat.byte_tx += skb->len;
	kfree_skb(skb);

exit_error:
	return ret;
}

static void btintel_pcie_release_hdev(struct btintel_pcie_data *data)
{
	struct hci_dev *hdev = data->hdev;

	if (!hdev)
		return;

	hci_unregister_dev(hdev);
	hci_free_dev(hdev);
	data->hdev = NULL;
}

static void btintel_pcie_disable_interrupts(struct btintel_pcie_data *data)
{
	spin_lock(&data->irq_lock);
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_MSIX_FH_INT_MASK, data->fh_init_mask);
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_MSIX_HW_INT_MASK, data->hw_init_mask);
	spin_unlock(&data->irq_lock);
}

static void btintel_pcie_enable_interrupts(struct btintel_pcie_data *data)
{
	spin_lock(&data->irq_lock);
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_MSIX_FH_INT_MASK, ~data->fh_init_mask);
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_CSR_MSIX_HW_INT_MASK, ~data->hw_init_mask);
	spin_unlock(&data->irq_lock);
}

static void btintel_pcie_synchronize_irqs(struct btintel_pcie_data *data)
{
	for (int i = 0; i < data->alloc_vecs; i++)
		synchronize_irq(data->msix_entries[i].vector);
}

static int btintel_pcie_get_debug_info_addr(struct hci_dev *hdev)
{
	struct btintel_pcie_data *data = hci_get_drvdata(hdev);
	struct btintel_pcie_trigger_evt *evt;
	u8 param[1] = {0x10};
	struct sk_buff *skb;
	int err = 0;

	skb = __hci_cmd_sync(hdev, BTINTEL_HCI_OP_DEBUG, 1, param,
			     HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Reading Intel read debug info address command failed (%ld)",
			   PTR_ERR(skb));
		/* Not all Intel products supports this command */
		if (PTR_ERR(skb) == -EOPNOTSUPP)
			return 0;
		return PTR_ERR(skb);
	}

	if (skb->len < (1 + sizeof(*evt))) {
		bt_dev_err(hdev, "Debug info response too short (%u bytes)", skb->len);
		err = -EIO;
		goto exit_error;
	}

	/* Check the status */
	if (skb->data[0]) {
		bt_dev_err(hdev, "Reading Intel read debug info command failed (0x%2.2x)",
			   skb->data[0]);
		err = -EIO;
		goto exit_error;
	}

	/* Consume Command Complete Status field */
	skb_pull(skb, 1);

	evt = (void *)skb->data;

	data->debug_evt_addr = le32_to_cpu(evt->addr);
	data->debug_evt_size = le32_to_cpu(evt->size);

	bt_dev_dbg(hdev, "config type: %u config len: %u debug event addr: 0x%8.8x size: 0x%8.8x",
		   evt->type, evt->len, data->debug_evt_addr,
		   data->debug_evt_size);
exit_error:
	kfree_skb(skb);
	return err;
}

static int btintel_pcie_setup_internal(struct hci_dev *hdev)
{
	struct btintel_pcie_data *data = hci_get_drvdata(hdev);
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
	hci_set_quirk(hdev, HCI_QUIRK_STRICT_DUPLICATE_FILTER);
	hci_set_quirk(hdev, HCI_QUIRK_SIMULTANEOUS_DISCOVERY);
	hci_set_quirk(hdev, HCI_QUIRK_NON_PERSISTENT_DIAG);

	/* Set up the quality report callback for Intel devices */
	hdev->set_quality_report = btintel_set_quality_report;

	memset(&ver_tlv, 0, sizeof(ver_tlv));
	/* For TLV type device, parse the tlv data */
	err = btintel_parse_version_tlv(hdev, &ver_tlv, skb);
	if (err) {
		bt_dev_err(hdev, "Failed to parse TLV version information");
		goto exit_error;
	}

	data->dmp_hdr.cnvi_bt = ver_tlv.cnvi_bt;
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
	case 0x1f:	/* ScP  */
	case 0x20:	/* ScP2 */
	case 0x21:	/* ScP2 F */
	case 0x22:	/* BzrIW */
	case 0x23:	/* DrC */
		/* Display version information of TLV type */
		btintel_version_info_tlv(hdev, &ver_tlv);

		/* Apply the device specific HCI quirks for TLV based devices
		 *
		 * All TLV based devices support WBS
		 */
		hci_set_quirk(hdev, HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED);

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
	}

	data->dmp_hdr.cnvi_top = ver_tlv.cnvi_top;
	data->dmp_hdr.cnvr_top = ver_tlv.cnvr_top;
	data->dmp_hdr.fw_timestamp = ver_tlv.timestamp;
	data->dmp_hdr.fw_build_type = ver_tlv.build_type;
	data->dmp_hdr.fw_build_num = ver_tlv.build_num;

	if (ver_tlv.img_type == 0x02 || ver_tlv.img_type == 0x03)
		data->dmp_hdr.fw_sha = ver_tlv.git_sha1;

	err = btintel_pcie_get_debug_info_addr(hdev);
	if (err)
		goto exit_error;

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
		btintel_pcie_dump_debug_registers(hdev);
		btintel_pcie_disable_interrupts(data);
		btintel_pcie_synchronize_irqs(data);
		err = btintel_pcie_reset_bt(data);
		if (err) {
			bt_dev_err(hdev, "Failed to do shr reset: %d", err);
			break;
		}
		usleep_range(10000, 12000);
		btintel_pcie_reset_ia(data);
		btintel_pcie_enable_interrupts(data);
		btintel_pcie_config_msix(data);
		err = btintel_pcie_enable_bt(data);
		if (err) {
			bt_dev_err(hdev, "Failed to enable hardware: %d", err);
			break;
		}
		btintel_pcie_start_rx(data);
	}

	if (!err)
		set_bit(BTINTEL_PCIE_SETUP_DONE, &data->flags);
	return err;
}

static struct btintel_pcie_dev_recovery *
btintel_pcie_get_recovery(struct pci_dev *pdev, struct device *dev)
{
	struct btintel_pcie_dev_recovery *tmp, *data = NULL;
	const char *name = pci_name(pdev);
	const size_t name_len = strlen(name) + 1;
	struct hci_dev *hdev = to_hci_dev(dev);

	spin_lock(&btintel_pcie_recovery_lock);
	list_for_each_entry(tmp, &btintel_pcie_recovery_list, list) {
		if (strcmp(tmp->name, name))
			continue;
		data = tmp;
		break;
	}
	spin_unlock(&btintel_pcie_recovery_lock);

	if (data) {
		bt_dev_dbg(hdev, "Found restart data for BDF: %s", data->name);
		return data;
	}

	data = kzalloc_flex(*data, name, name_len, GFP_ATOMIC);
	if (!data)
		return NULL;

	strscpy(data->name, name, name_len);
	spin_lock(&btintel_pcie_recovery_lock);
	list_add_tail(&data->list, &btintel_pcie_recovery_list);
	spin_unlock(&btintel_pcie_recovery_lock);

	return data;
}

static void btintel_pcie_free_restart_list(void)
{
	struct btintel_pcie_dev_recovery *tmp;

	while ((tmp = list_first_entry_or_null(&btintel_pcie_recovery_list,
					       typeof(*tmp), list))) {
		list_del(&tmp->list);
		kfree(tmp);
	}
}

static void btintel_pcie_inc_recovery_count(struct pci_dev *pdev,
					    struct device *dev)
{
	struct btintel_pcie_dev_recovery *data;
	time64_t retry_window;

	data = btintel_pcie_get_recovery(pdev, dev);
	if (!data)
		return;

	retry_window = ktime_get_boottime_seconds() - data->last_error;
	if (data->count == 0) {
		data->last_error = ktime_get_boottime_seconds();
		data->count++;
	} else if (retry_window < BTINTEL_PCIE_RESET_WINDOW_SECS &&
		   data->count <= BTINTEL_PCIE_FLR_MAX_RETRY) {
		data->count++;
	} else if (retry_window > BTINTEL_PCIE_RESET_WINDOW_SECS) {
		data->last_error = 0;
		data->count = 0;
	}
}

static int btintel_pcie_acpi_reset_method(struct btintel_pcie_data *data)
{
	union acpi_object *obj, argv4;
	acpi_handle handle;
	int ret;
	struct pldr_mode {
		__le16	cmd_type;
		__le16	cmd_payload;
	} __packed;

	/* set 1 for _PRR mode
	 * Product Reset (PLDR Abort flow)
	 */
	static const struct pldr_mode mode = {
		.cmd_type = cpu_to_le16(1),
		.cmd_payload = cpu_to_le16(BTINTEL_PCIE_DSM_PLDR_MODE_EN_PROD_RESET |
			       BTINTEL_PCIE_DSM_PLDR_MODE_EN_WIFI_FLR),
	};
	struct hci_dev *hdev = data->hdev;

	handle = ACPI_HANDLE(GET_HCIDEV_DEV(data->hdev));
	if (!handle) {
		bt_dev_err(data->hdev, "No support for bluetooth device in ACPI firmware");
		return -EACCES;
	}

	if (!acpi_has_method(handle, "_PRR")) {
		bt_dev_err(data->hdev, "No support for _PRR ACPI method, cold boot");
		return -ENODEV;
	}

	argv4.buffer.type = ACPI_TYPE_BUFFER;
	argv4.buffer.length = sizeof(mode);
	argv4.buffer.pointer = (void *)&mode;

	obj = acpi_evaluate_dsm(handle, &btintel_guid_dsm, 0,
				BTINTEL_PCIE_DSM_DYNAMIC_PLDR, &argv4);
	if (!obj) {
		bt_dev_err(data->hdev, "Failed to call dsm to set reset method");
		return -EIO;
	}
	ACPI_FREE(obj);

	pci_dev_lock(data->pdev);
	pci_save_state(data->pdev);
	ret = btintel_acpi_reset_method(hdev);
	if (ret)
		bt_dev_err(data->hdev, "ACPI _PRR reset failed (%d), PLDR incomplete",
			   ret);
	pci_restore_state(data->pdev);
	pci_dev_unlock(data->pdev);
	return ret;
}

static void btintel_pcie_perform_pldr(struct btintel_pcie_data *data)
{
	struct pci_dev *pdev = data->pdev;
	struct pci_dev *wifi = NULL;
	struct pci_bus *bus;
	int ret;
	/* on integrated we have to look up by ID (same bus) */
	static const struct pci_device_id wifi_device_ids[] = {
	#define WIFI_DEV(_id) { PCI_DEVICE(PCI_VENDOR_ID_INTEL, _id) }
		WIFI_DEV(0xA840), /* LNL */
		WIFI_DEV(0xE440), /* PTL-P */
		WIFI_DEV(0xE340), /* PTL-H */
		WIFI_DEV(0xD340), /* NVL-H */
		WIFI_DEV(0x6E70), /* NVL-S */
		WIFI_DEV(0x4D40), /* WCL */
		{}
	};
	struct pci_dev *tmp = NULL;

	bus = pdev->bus;
	if (!bus)
		return;

	list_for_each_entry(tmp, &bus->devices, bus_list) {
		if (pci_match_id(wifi_device_ids, tmp)) {
			wifi = pci_dev_get(tmp);
			break;
		}
	}

	if (wifi)
		device_release_driver(&wifi->dev);

	/* Wi-Fi is fully unbound before the reset and fully reprobed after
	 * the normal PCI probe path handles all state setup from scratch.
	 * BT needs pci_save_state()/pci_restore_state() because the BT driver
	 * is still partially attached when the _PRR runs (it hasn't been unbound yet).
	 * The PCI device needs to remain minimally functional so that
	 * device_reprobe(&pdev->dev) can work afterward
	 */
	ret = btintel_pcie_acpi_reset_method(data);

	if (wifi) {
		if (device_reprobe(&wifi->dev))
			BT_ERR("WiFi reprobe failed for BDF:%s", pci_name(wifi));
		pci_dev_put(wifi);
	}

	if (!ret) {
		if (device_reprobe(&pdev->dev))
			BT_ERR("BT reprobe failed for BDF:%s", pci_name(pdev));
	}
}

/*
 * Issue a Function Level Reset and hand teardown/re-init off to the PCI
 * core via device_reprobe(), mirroring the PLDR path's contract.
 *
 * Caller must hold pci_lock_rescan_remove() and must have already
 * disabled interrupts and drained both rx_work and coredump_work.
 */
static int btintel_pcie_perform_flr(struct btintel_pcie_data *data)
{
	struct pci_dev *pdev = data->pdev;
	int err;

	/* pci_try_reset_function() avoids the device_lock ABBA against
	 * btintel_pcie_remove(): .remove() runs with device_lock held and
	 * then waits for this work via disable_work_sync(); the blocking
	 * pci_reset_function() would deadlock by trying to re-acquire
	 * device_lock here.
	 */
	err = pci_try_reset_function(pdev);
	if (err) {
		BT_ERR("Failed resetting the pcie device (%d)", err);
		return err;
	}

	/* device_reprobe() always detaches the driver first (running
	 * .remove(), which frees 'data'); any re-probe failure leaves the
	 * device unbound but 'data' is already gone, so just log it.
	 */
	if (device_reprobe(&pdev->dev))
		BT_ERR("BT reprobe failed for BDF:%s", pci_name(pdev));

	return 0;
}

static void btintel_pcie_reset_work(struct work_struct *wk)
{
	struct btintel_pcie_data *data =
		container_of(wk, struct btintel_pcie_data, reset_work);
	struct pci_dev *pdev = data->pdev;

	pci_lock_rescan_remove();

	if (!pdev->bus)
		goto out;

	if (!data)
		goto out;

	btintel_pcie_disable_interrupts(data);
	btintel_pcie_synchronize_irqs(data);

	flush_work(&data->rx_work);
	/* Drain any in-flight dump workers and block new ones across reset.
	 * Safe from self-deadlock: they all run on a separate wq.
	 */
	disable_work_sync(&data->coredump_work);
	disable_work_sync(&data->hwexp_work);
	disable_work_sync(&data->fwtrigger_work);
	disable_work_sync(&data->mbox_work);

	bt_dev_dbg(data->hdev, "Release bluetooth interface");

	/* Both reset paths follow the same contract: on success they
	 * destroy 'data' via device_reprobe() (a fresh probe re-INIT_WORKs
	 * the dump workers with disable count 0), so enable_work() must
	 * NOT be called on the success path. Only the FLR path can fail
	 * with 'data' still alive, in which case we balance the
	 * disable_work_sync() calls above so a later successful reset is
	 * not permanently blocked.
	 *
	 * pci_lock_rescan_remove() (held above) serializes against PCI
	 * device addition/removal (hotplug), so no device can be added to
	 * or removed from the bus list while this code runs.
	 */
	if (data->reset_type == BTINTEL_PCIE_IOSF_PRR_PLDR) {
		btintel_pcie_perform_pldr(data);
		goto out;
	}

	if (btintel_pcie_perform_flr(data)) {
		enable_work(&data->coredump_work);
		enable_work(&data->hwexp_work);
		enable_work(&data->fwtrigger_work);
		enable_work(&data->mbox_work);
	}

out:
	pci_dev_put(pdev);
	pci_unlock_rescan_remove();
}

/* Schedule a device reset of the requested type.
 *
 * BTINTEL_PCIE_RECOVERY_IN_PROGRESS serializes all reset requesters
 * (sysfs reset attribute, hci_cmd_timeout(), hw_error, resume error
 * path, etc.) so that:
 *
 *   - dev_data->reset_type is written by exactly one caller (the
 *     thread that wins test_and_set_bit), eliminating the race where
 *     a second hw_error could clobber an already-scheduled reset's
 *     type;
 *   - the write happens AFTER the bit is set, so reset_work observes
 *     it through schedule_work()'s memory ordering;
 *   - losers return without touching reset_type or scheduling the
 *     work, so concurrent triggers are silently coalesced into the
 *     in-flight one (whose recovery will reinitialize the device
 *     regardless of the dropped trigger's variant).
 *
 * The bit is cleared only by .remove() / re-probe via fresh devm
 * allocation, which is the intended one-shot semantics: a reset
 * tears down and re-probes 'data', so there is no "in-flight"
 * reset to follow up after device_reprobe() succeeds.
 */
static void btintel_pcie_request_reset(struct btintel_pcie_data *data,
				       enum btintel_pcie_reset_type type)
{
	if (!test_bit(BTINTEL_PCIE_SETUP_DONE, &data->flags))
		return;

	if (test_and_set_bit(BTINTEL_PCIE_RECOVERY_IN_PROGRESS, &data->flags))
		return;

	data->reset_type = type;

	pci_dev_get(data->pdev);
	schedule_work(&data->reset_work);
}

static void btintel_pcie_hci_reset(struct hci_dev *hdev)
{
	struct btintel_pcie_data *data = hci_get_drvdata(hdev);

	btintel_pcie_request_reset(data, BTINTEL_PCIE_IOSF_PRR_FLR);
}

static ssize_t vendor_reset_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned int val;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct btintel_pcie_data *data = pci_get_drvdata(pdev);

	if (!data || !data->hdev)
		return -ENODEV;

	if (kstrtouint(buf, 10, &val) || val != 0) {
		bt_dev_warn(data->hdev, "PLDR rejected: invalid input");
		return -EINVAL;
	}

	bt_dev_info(data->hdev, "PLDR triggered via sysfs");
	btintel_pcie_request_reset(data, BTINTEL_PCIE_IOSF_PRR_PLDR);

	return count;
}

static ssize_t vendor_reset_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "0 - PLDR\n");
}

static DEVICE_ATTR_RW(vendor_reset);

static struct attribute *btintel_pcie_attrs[] = {
	&dev_attr_vendor_reset.attr,
	NULL,
};

ATTRIBUTE_GROUPS(btintel_pcie);

static void btintel_pcie_hw_error(struct hci_dev *hdev, u8 code)
{
	struct btintel_pcie_dev_recovery *rec;
	struct btintel_pcie_data *dev_data = hci_get_drvdata(hdev);
	struct pci_dev *pdev = dev_data->pdev;
	enum btintel_pcie_reset_type type;
	time64_t retry_window;

	if (test_bit(BTINTEL_PCIE_RECOVERY_IN_PROGRESS, &dev_data->flags))
		return;

	btintel_pcie_dump_debug_registers(hdev);

	rec = btintel_pcie_get_recovery(pdev, &hdev->dev);
	if (!rec)
		return;

	type = (code == 0x13) ? BTINTEL_PCIE_IOSF_PRR_PLDR
			      : BTINTEL_PCIE_IOSF_PRR_FLR;

	bt_dev_err(hdev, "Encountered exception err:0x%x triggering: %s", code,
		   type == BTINTEL_PCIE_IOSF_PRR_PLDR ? "PLDR" : "FLR");
	retry_window = ktime_get_boottime_seconds() - rec->last_error;

	if (retry_window < BTINTEL_PCIE_RESET_WINDOW_SECS &&
	    rec->count >= BTINTEL_PCIE_FLR_MAX_RETRY) {
		bt_dev_err(hdev, "Exhausted maximum: %d recovery attempts: %d",
			   BTINTEL_PCIE_FLR_MAX_RETRY, rec->count);
		bt_dev_dbg(hdev, "Boot time: %lld seconds",
			   ktime_get_boottime_seconds());
		bt_dev_dbg(hdev, "last error at: %lld seconds",
			   rec->last_error);
		return;
	}
	btintel_pcie_inc_recovery_count(pdev, &hdev->dev);
	btintel_pcie_request_reset(dev_data, type);
}

static bool btintel_pcie_wakeup(struct hci_dev *hdev)
{
	struct btintel_pcie_data *data = hci_get_drvdata(hdev);

	return device_may_wakeup(&data->pdev->dev);
}

static const struct {
	u16 opcode;
	const char *desc;
} btintel_pcie_hci_drv_supported_commands[] = {
	/* Common commands */
	{ HCI_DRV_OP_READ_INFO, "Read Info" },
};

static int btintel_pcie_hci_drv_read_info(struct hci_dev *hdev, void *data,
					  u16 data_len)
{
	struct hci_drv_rp_read_info *rp;
	size_t rp_size;
	int err, i;
	u16 opcode, num_supported_commands =
		ARRAY_SIZE(btintel_pcie_hci_drv_supported_commands);

	rp_size = struct_size(rp, supported_commands, num_supported_commands);

	rp = kmalloc(rp_size, GFP_KERNEL);
	if (!rp)
		return -ENOMEM;

	strscpy_pad(rp->driver_name, KBUILD_MODNAME);

	rp->num_supported_commands = cpu_to_le16(num_supported_commands);
	for (i = 0; i < num_supported_commands; i++) {
		opcode = btintel_pcie_hci_drv_supported_commands[i].opcode;
		bt_dev_dbg(hdev,
			    "Supported HCI Drv command (0x%02x|0x%04x): %s",
			    hci_opcode_ogf(opcode),
			    hci_opcode_ocf(opcode),
			    btintel_pcie_hci_drv_supported_commands[i].desc);
		rp->supported_commands[i] = cpu_to_le16(opcode);
	}

	err = hci_drv_cmd_complete(hdev, HCI_DRV_OP_READ_INFO,
				   HCI_DRV_STATUS_SUCCESS,
				   rp, rp_size);

	kfree(rp);
	return err;
}

static const struct hci_drv_handler btintel_pcie_hci_drv_common_handlers[] = {
	{ btintel_pcie_hci_drv_read_info,       HCI_DRV_READ_INFO_SIZE },
};

static const struct hci_drv_handler btintel_pcie_hci_drv_specific_handlers[] = {};

static struct hci_drv btintel_pcie_hci_drv = {
	.common_handler_count   = ARRAY_SIZE(btintel_pcie_hci_drv_common_handlers),
	.common_handlers        = btintel_pcie_hci_drv_common_handlers,
	.specific_handler_count = ARRAY_SIZE(btintel_pcie_hci_drv_specific_handlers),
	.specific_handlers      = btintel_pcie_hci_drv_specific_handlers,
};

static int btintel_pcie_setup_hdev(struct btintel_pcie_data *data)
{
	int err;
	struct hci_dev *hdev;

	hdev = hci_alloc_dev_priv(sizeof(struct btintel_data));
	if (!hdev)
		return -ENOMEM;

	hdev->bus = HCI_PCI;
	hci_set_drvdata(hdev, data);

	SET_HCIDEV_DEV(hdev, &data->pdev->dev);

	hdev->manufacturer = 2;
	hdev->open = btintel_pcie_open;
	hdev->close = btintel_pcie_close;
	hdev->send = btintel_pcie_send_frame;
	hdev->setup = btintel_pcie_setup;
	hdev->shutdown = btintel_shutdown_combined;
	hdev->hw_error = btintel_pcie_hw_error;
	hdev->set_diag = btintel_set_diag;
	hdev->set_bdaddr = btintel_set_bdaddr;
	hdev->reset = btintel_pcie_hci_reset;
	hdev->wakeup = btintel_pcie_wakeup;
	hdev->hci_drv = &btintel_pcie_hci_drv;

	err = hci_register_dev(hdev);
	if (err < 0) {
		BT_ERR("Failed to register to hdev (%d)", err);
		hci_free_dev(hdev);
		return err;
	}

	/* Publish hdev only after successful registration; the coredump
	 * worker bails on !data->hdev, so it never observes a half-set-up
	 * device.
	 */
	data->hdev = hdev;
	data->dmp_hdr.driver_name = KBUILD_MODNAME;
	return 0;
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

	init_waitqueue_head(&data->mbox_parse_wait_q);

	data->workqueue = alloc_ordered_workqueue(KBUILD_MODNAME, WQ_HIGHPRI);
	if (!data->workqueue)
		return -ENOMEM;

	data->dump_workqueue = alloc_ordered_workqueue(KBUILD_MODNAME "_cd", 0);
	if (!data->dump_workqueue) {
		destroy_workqueue(data->workqueue);
		return -ENOMEM;
	}

	skb_queue_head_init(&data->rx_skb_q);
	INIT_WORK(&data->rx_work, btintel_pcie_rx_work);
	INIT_WORK(&data->reset_work, btintel_pcie_reset_work);
	INIT_WORK(&data->coredump_work, btintel_pcie_coredump_worker);
	INIT_WORK(&data->hwexp_work, btintel_pcie_hwexp_worker);
	INIT_WORK(&data->fwtrigger_work, btintel_pcie_fwtrigger_worker);
	INIT_WORK(&data->mbox_work, btintel_pcie_mbox_worker);

	data->boot_stage_cache = 0x00;
	data->img_resp_cache = 0x00;
	data->dbg_path_cache = BTINTEL_PCIE_WIFI_DBGC;
	/* FLR can be invoked by echoing to debugfs path, so explicitly
	 * initialized
	 */
	data->reset_type = BTINTEL_PCIE_IOSF_PRR_FLR;
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

	destroy_workqueue(data->dump_workqueue);

	pci_clear_master(pdev);

	pci_set_drvdata(pdev, NULL);

	return err;
}

static void btintel_pcie_remove(struct pci_dev *pdev)
{
	struct btintel_pcie_data *data;

	data = pci_get_drvdata(pdev);
	if (!data) {
		BT_WARN("PCI driver data is NULL, aborting remove");
		return;
	}

	/* Permanently block all dump triggers and drain the workers before
	 * tearing down. Must run before disable_work_sync(&reset_work) so
	 * the disable counters stay >= 1 even after reset_work()'s
	 * balanced enable_work() (counter 2 -> 1, never reaching 0).
	 */
	disable_work_sync(&data->coredump_work);
	disable_work_sync(&data->hwexp_work);
	disable_work_sync(&data->fwtrigger_work);
	disable_work_sync(&data->mbox_work);

	/* Cancel pending reset work. Skip only when remove() is called from
	 * within the reset work itself (PLDR device_reprobe path) to avoid
	 * deadlock. current_work() returns the work_struct of the caller if
	 * we are in a workqueue context.
	 */
	if (current_work() != &data->reset_work)
		disable_work_sync(&data->reset_work);

	btintel_pcie_disable_interrupts(data);

	btintel_pcie_synchronize_irqs(data);

	flush_work(&data->rx_work);

	btintel_pcie_reset_bt(data);
	for (int i = 0; i < data->alloc_vecs; i++) {
		struct msix_entry *msix_entry;

		msix_entry = &data->msix_entries[i];
		devm_free_irq(&pdev->dev, msix_entry->vector, msix_entry);
	}

	pci_free_irq_vectors(pdev);

	btintel_pcie_release_hdev(data);

	destroy_workqueue(data->dump_workqueue);
	destroy_workqueue(data->workqueue);

	btintel_pcie_free(data);

	pci_clear_master(pdev);

	pci_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_DEV_COREDUMP
static void btintel_pcie_coredump(struct device *dev)
{
	struct  pci_dev *pdev = to_pci_dev(dev);
	struct btintel_pcie_data *data = pci_get_drvdata(pdev);

	if (!data)
		return;

	btintel_pcie_queue_coredump(data,
				    BTINTEL_PCIE_TRIGGER_REASON_USER_TRIGGER);
}
#endif

static int btintel_pcie_set_dxstate(struct btintel_pcie_data *data, u32 dxstate)
{
	int retry = 0, err;
	long status;
	u32 dx_intr_timeout_ms = 200;

	do {
		data->gp0_received = false;

		btintel_pcie_wr_sleep_cntrl(data, dxstate);

		status = wait_event_timeout(data->gp0_wait_q, data->gp0_received,
			msecs_to_jiffies(dx_intr_timeout_ms));

		if (!status) {
			bt_dev_warn(data->hdev,
				    "Timeout (%u ms) on alive interrupt for D%d entry, retry count %d",
				    dx_intr_timeout_ms, dxstate, retry);

			/* MSIX_HW_INT_CAUSES is W1C. Write only GP0 so other
			 * pending causes are not acknowledged here.
			 */
			btintel_pcie_wr_reg32(data,
					      BTINTEL_PCIE_CSR_MSIX_HW_INT_CAUSES,
					      BTINTEL_PCIE_MSIX_HW_INT_CAUSES_GP0);
		}

		/* gp0_received is set at the top of the handler, before the switch on
		 * alive_intr_ctxt. Error and lockdown are filtered out above it, but a
		 * matched case can still complete without doing anything - D3 breaks
		 * unchanged while the controller has not reached D0 - and a hardware
		 * bug may drop the interrupt outright. Either way the flag says a gp0
		 * was handled, not that the transition completed, and only the register
		 * knows. Refresh the cache here and retry only if the state check still
		 * fails.
		 */
		data->boot_stage_cache = btintel_pcie_rd_reg32(data,
							       BTINTEL_PCIE_CSR_BOOT_STAGE_REG);
		if (dxstate == BTINTEL_PCIE_STATE_D0) {
			if (btintel_pcie_in_d0(data)) {
				bool armed;

				/* Claim the transition under irq_lock so a
				 * concurrent gp0 handler run cannot also arm RX.
				 */
				spin_lock(&data->irq_lock);
				armed = data->alive_intr_ctxt == BTINTEL_PCIE_D0;
				if (!armed)
					data->alive_intr_ctxt = BTINTEL_PCIE_D0;
				spin_unlock(&data->irq_lock);

				if (armed)
					return 0;

				/* Do what the handler's D3 -> D0 branch
				 * would have done, unless it already has.
				 */
				btintel_pcie_reset_ia(data);
				err = btintel_pcie_start_rx(data);
				if (err)
					return err;

				/* Complete the mbox<->alive handshake */
				if (test_and_clear_bit(BTINTEL_PCIE_MBOX_PARSE_PENDING,
						       &data->flags)) {
					set_bit(BTINTEL_PCIE_MBOX_PARSE_READY, &data->flags);
					wake_up(&data->mbox_parse_wait_q);
				}

				return 0;
			}
		} else {
			if (btintel_pcie_in_d3(data)) {
				/* Do what the handler's D0 -> D3 branch
				 * would have done, unless it already has.
				 */
				spin_lock(&data->irq_lock);
				if (data->alive_intr_ctxt != BTINTEL_PCIE_D3)
					data->alive_intr_ctxt = BTINTEL_PCIE_D3;
				spin_unlock(&data->irq_lock);

				return 0;
			}
		}

	} while (++retry < BTINTEL_PCIE_DX_TRANSITION_MAX_RETRIES);

	return -EBUSY;
}

static int btintel_pcie_suspend_late(struct device *dev, pm_message_t mesg)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct btintel_pcie_data *data;
	ktime_t start;
	u32 dxstate;
	int err;

	data = pci_get_drvdata(pdev);

	/* S0ix (s2idle) uses D3_HOT; S3, freeze and hibernate use D3_COLD. */
	if (mesg.event == PM_EVENT_SUSPEND &&
	    pm_suspend_target_state == PM_SUSPEND_TO_IDLE)
		dxstate = BTINTEL_PCIE_STATE_D3_HOT;
	else
		dxstate = BTINTEL_PCIE_STATE_D3_COLD;

	start = ktime_get();

	/* Refer: 6.4.11.7 -> Platform power management */
	err = btintel_pcie_set_dxstate(data, dxstate);

	if (err) {
		bt_dev_err(data->hdev, "Failed to set dxstate:%u (%d)",
			   dxstate, err);
		return err;
	}

	bt_dev_dbg(data->hdev,
		   "device entered into d3 state from d0 in %lld us",
		   ktime_to_us(ktime_get() - start));
	return err;
}

static int btintel_pcie_suspend(struct device *dev)
{
	return btintel_pcie_suspend_late(dev, PMSG_SUSPEND);
}

static int btintel_pcie_hibernate(struct device *dev)
{
	return btintel_pcie_suspend_late(dev, PMSG_HIBERNATE);
}

static int btintel_pcie_freeze(struct device *dev)
{
	return btintel_pcie_suspend_late(dev, PMSG_FREEZE);
}

static int btintel_pcie_resume_event(struct device *dev, pm_message_t mesg)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct btintel_pcie_data *data;
	ktime_t start;
	int err;

	data = pci_get_drvdata(pdev);

	start = ktime_get();

	/* S3 and S4 may cut power, erasing the firmware. Force FLR to recover
	 * instead of a normal D0 transition.
	 */
	if (mesg.event == PM_EVENT_RESTORE ||
	    (mesg.event == PM_EVENT_RESUME &&
	     pm_suspend_target_state == PM_SUSPEND_MEM)) {
		set_bit(BTINTEL_PCIE_CORE_HALTED, &data->flags);
		btintel_pcie_request_reset(data, BTINTEL_PCIE_IOSF_PRR_FLR);
		return 0;
	}

	/* Refer: 6.4.11.7 -> Platform power management */
	err = btintel_pcie_set_dxstate(data, BTINTEL_PCIE_STATE_D0);

	if (err) {
		bt_dev_err(data->hdev, "Failed to set D0 state (%d)", err);
	} else {
		bt_dev_dbg(data->hdev,
			   "device entered into d0 state from d3 in %lld us",
			   ktime_to_us(ktime_get() - start));
		return err;
	}

	/* Trigger function level reset if the controller is in error
	 * state during resume() to bring back the controller to
	 * operational mode
	 */

	data->boot_stage_cache = btintel_pcie_rd_reg32(data,
			BTINTEL_PCIE_CSR_BOOT_STAGE_REG);
	if (btintel_pcie_in_error(data) ||
			btintel_pcie_in_device_halt(data)) {
		bt_dev_err(data->hdev, "Controller in error state for D0 entry");
		btintel_pcie_queue_coredump(data,
					    BTINTEL_PCIE_TRIGGER_REASON_FW_ASSERT);
		set_bit(BTINTEL_PCIE_CORE_HALTED, &data->flags);
		btintel_pcie_request_reset(data, BTINTEL_PCIE_IOSF_PRR_FLR);
	}
	return err;
}

static int btintel_pcie_resume(struct device *dev)
{
	return btintel_pcie_resume_event(dev, PMSG_RESUME);
}

static int btintel_pcie_restore(struct device *dev)
{
	return btintel_pcie_resume_event(dev, PMSG_RESTORE);
}

static int btintel_pcie_thaw(struct device *dev)
{
	return btintel_pcie_resume_event(dev, PMSG_THAW);
}

static const struct dev_pm_ops btintel_pcie_pm_ops = {
	.suspend = btintel_pcie_suspend,
	.resume = btintel_pcie_resume,
	.freeze = btintel_pcie_freeze,
	.thaw = btintel_pcie_thaw,
	.poweroff = btintel_pcie_hibernate,
	.restore = btintel_pcie_restore,
};

static struct pci_driver btintel_pcie_driver = {
	.name = KBUILD_MODNAME,
	.id_table = btintel_pcie_table,
	.probe = btintel_pcie_probe,
	.remove = btintel_pcie_remove,
	.driver.pm = pm_sleep_ptr(&btintel_pcie_pm_ops),
	.dev_groups = btintel_pcie_groups,
#ifdef CONFIG_DEV_COREDUMP
	.driver.coredump = btintel_pcie_coredump
#endif
};

static int __init btintel_pcie_init(void)
{
	return pci_register_driver(&btintel_pcie_driver);
}

static void __exit btintel_pcie_exit(void)
{
	pci_unregister_driver(&btintel_pcie_driver);
	btintel_pcie_free_restart_list();
}

module_init(btintel_pcie_init);
module_exit(btintel_pcie_exit);

MODULE_AUTHOR("Tedd Ho-Jeong An <tedd.an@intel.com>");
MODULE_DESCRIPTION("Intel Bluetooth PCIe transport driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
