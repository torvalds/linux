// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/bitfield.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/types.h>

#include "fbnic.h"
#include "fbnic_tlv.h"

static void __fbnic_mbx_wr_desc(struct fbnic_dev *fbd, int mbx_idx,
				int desc_idx, u64 desc)
{
	u32 desc_offset = FBNIC_IPC_MBX(mbx_idx, desc_idx);

	/* Write the upper 32b and then the lower 32b. Doing this the
	 * FW can then read lower, upper, lower to verify that the state
	 * of the descriptor wasn't changed mid-transaction.
	 */
	fw_wr32(fbd, desc_offset + 1, upper_32_bits(desc));
	fw_wrfl(fbd);
	fw_wr32(fbd, desc_offset, lower_32_bits(desc));
}

static void __fbnic_mbx_invalidate_desc(struct fbnic_dev *fbd, int mbx_idx,
					int desc_idx, u32 desc)
{
	u32 desc_offset = FBNIC_IPC_MBX(mbx_idx, desc_idx);

	/* For initialization we write the lower 32b of the descriptor first.
	 * This way we can set the state to mark it invalid before we clear the
	 * upper 32b.
	 */
	fw_wr32(fbd, desc_offset, desc);
	fw_wrfl(fbd);
	fw_wr32(fbd, desc_offset + 1, 0);
}

static u64 __fbnic_mbx_rd_desc(struct fbnic_dev *fbd, int mbx_idx, int desc_idx)
{
	u32 desc_offset = FBNIC_IPC_MBX(mbx_idx, desc_idx);
	u64 desc;

	desc = fw_rd32(fbd, desc_offset);
	desc |= (u64)fw_rd32(fbd, desc_offset + 1) << 32;

	return desc;
}

static void fbnic_mbx_reset_desc_ring(struct fbnic_dev *fbd, int mbx_idx)
{
	int desc_idx;

	/* Disable DMA transactions from the device,
	 * and flush any transactions triggered during cleaning
	 */
	switch (mbx_idx) {
	case FBNIC_IPC_MBX_RX_IDX:
		wr32(fbd, FBNIC_PUL_OB_TLP_HDR_AW_CFG,
		     FBNIC_PUL_OB_TLP_HDR_AW_CFG_FLUSH);
		break;
	case FBNIC_IPC_MBX_TX_IDX:
		wr32(fbd, FBNIC_PUL_OB_TLP_HDR_AR_CFG,
		     FBNIC_PUL_OB_TLP_HDR_AR_CFG_FLUSH);
		break;
	}

	wrfl(fbd);

	/* Initialize first descriptor to all 0s. Doing this gives us a
	 * solid stop for the firmware to hit when it is done looping
	 * through the ring.
	 */
	__fbnic_mbx_invalidate_desc(fbd, mbx_idx, 0, 0);

	/* We then fill the rest of the ring starting at the end and moving
	 * back toward descriptor 0 with skip descriptors that have no
	 * length nor address, and tell the firmware that they can skip
	 * them and just move past them to the one we initialized to 0.
	 */
	for (desc_idx = FBNIC_IPC_MBX_DESC_LEN; --desc_idx;)
		__fbnic_mbx_invalidate_desc(fbd, mbx_idx, desc_idx,
					    FBNIC_IPC_MBX_DESC_FW_CMPL |
					    FBNIC_IPC_MBX_DESC_HOST_CMPL);
}

void fbnic_mbx_init(struct fbnic_dev *fbd)
{
	int i;

	/* Initialize lock to protect Tx ring */
	spin_lock_init(&fbd->fw_tx_lock);

	/* Reset FW Capabilities */
	memset(&fbd->fw_cap, 0, sizeof(fbd->fw_cap));

	/* Reinitialize mailbox memory */
	for (i = 0; i < FBNIC_IPC_MBX_INDICES; i++)
		memset(&fbd->mbx[i], 0, sizeof(struct fbnic_fw_mbx));

	/* Do not auto-clear the FW mailbox interrupt, let SW clear it */
	wr32(fbd, FBNIC_INTR_SW_AC_MODE(0), ~(1u << FBNIC_FW_MSIX_ENTRY));

	/* Clear any stale causes in vector 0 as that is used for doorbell */
	wr32(fbd, FBNIC_INTR_CLEAR(0), 1u << FBNIC_FW_MSIX_ENTRY);

	for (i = 0; i < FBNIC_IPC_MBX_INDICES; i++)
		fbnic_mbx_reset_desc_ring(fbd, i);
}

static int fbnic_mbx_map_msg(struct fbnic_dev *fbd, int mbx_idx,
			     struct fbnic_tlv_msg *msg, u16 length, u8 eom)
{
	struct fbnic_fw_mbx *mbx = &fbd->mbx[mbx_idx];
	u8 tail = mbx->tail;
	dma_addr_t addr;
	int direction;

	if (!mbx->ready || !fbnic_fw_present(fbd))
		return -ENODEV;

	direction = (mbx_idx == FBNIC_IPC_MBX_RX_IDX) ? DMA_FROM_DEVICE :
							DMA_TO_DEVICE;

	if (mbx->head == ((tail + 1) % FBNIC_IPC_MBX_DESC_LEN))
		return -EBUSY;

	addr = dma_map_single(fbd->dev, msg, PAGE_SIZE, direction);
	if (dma_mapping_error(fbd->dev, addr))
		return -ENOSPC;

	mbx->buf_info[tail].msg = msg;
	mbx->buf_info[tail].addr = addr;

	mbx->tail = (tail + 1) % FBNIC_IPC_MBX_DESC_LEN;

	fw_wr32(fbd, FBNIC_IPC_MBX(mbx_idx, mbx->tail), 0);

	__fbnic_mbx_wr_desc(fbd, mbx_idx, tail,
			    FIELD_PREP(FBNIC_IPC_MBX_DESC_LEN_MASK, length) |
			    (addr & FBNIC_IPC_MBX_DESC_ADDR_MASK) |
			    (eom ? FBNIC_IPC_MBX_DESC_EOM : 0) |
			    FBNIC_IPC_MBX_DESC_HOST_CMPL);

	return 0;
}

static void fbnic_mbx_unmap_and_free_msg(struct fbnic_dev *fbd, int mbx_idx,
					 int desc_idx)
{
	struct fbnic_fw_mbx *mbx = &fbd->mbx[mbx_idx];
	int direction;

	if (!mbx->buf_info[desc_idx].msg)
		return;

	direction = (mbx_idx == FBNIC_IPC_MBX_RX_IDX) ? DMA_FROM_DEVICE :
							DMA_TO_DEVICE;
	dma_unmap_single(fbd->dev, mbx->buf_info[desc_idx].addr,
			 PAGE_SIZE, direction);

	free_page((unsigned long)mbx->buf_info[desc_idx].msg);
	mbx->buf_info[desc_idx].msg = NULL;
}

static void fbnic_mbx_clean_desc_ring(struct fbnic_dev *fbd, int mbx_idx)
{
	int i;

	fbnic_mbx_reset_desc_ring(fbd, mbx_idx);

	for (i = FBNIC_IPC_MBX_DESC_LEN; i--;)
		fbnic_mbx_unmap_and_free_msg(fbd, mbx_idx, i);
}

void fbnic_mbx_clean(struct fbnic_dev *fbd)
{
	int i;

	for (i = 0; i < FBNIC_IPC_MBX_INDICES; i++)
		fbnic_mbx_clean_desc_ring(fbd, i);
}

#define FBNIC_MBX_MAX_PAGE_SIZE	FIELD_MAX(FBNIC_IPC_MBX_DESC_LEN_MASK)
#define FBNIC_RX_PAGE_SIZE	min_t(int, PAGE_SIZE, FBNIC_MBX_MAX_PAGE_SIZE)

static int fbnic_mbx_alloc_rx_msgs(struct fbnic_dev *fbd)
{
	struct fbnic_fw_mbx *rx_mbx = &fbd->mbx[FBNIC_IPC_MBX_RX_IDX];
	u8 tail = rx_mbx->tail, head = rx_mbx->head, count;
	int err = 0;

	/* Do nothing if mailbox is not ready, or we already have pages on
	 * the ring that can be used by the firmware
	 */
	if (!rx_mbx->ready)
		return -ENODEV;

	/* Fill all but 1 unused descriptors in the Rx queue. */
	count = (head - tail - 1) & (FBNIC_IPC_MBX_DESC_LEN - 1);
	while (!err && count--) {
		struct fbnic_tlv_msg *msg;

		msg = (struct fbnic_tlv_msg *)__get_free_page(GFP_ATOMIC |
							      __GFP_NOWARN);
		if (!msg) {
			err = -ENOMEM;
			break;
		}

		err = fbnic_mbx_map_msg(fbd, FBNIC_IPC_MBX_RX_IDX, msg,
					FBNIC_RX_PAGE_SIZE, 0);
		if (err)
			free_page((unsigned long)msg);
	}

	return err;
}

static int fbnic_mbx_map_tlv_msg(struct fbnic_dev *fbd,
				 struct fbnic_tlv_msg *msg)
{
	unsigned long flags;
	int err;

	spin_lock_irqsave(&fbd->fw_tx_lock, flags);

	err = fbnic_mbx_map_msg(fbd, FBNIC_IPC_MBX_TX_IDX, msg,
				le16_to_cpu(msg->hdr.len) * sizeof(u32), 1);

	spin_unlock_irqrestore(&fbd->fw_tx_lock, flags);

	return err;
}

static int fbnic_mbx_set_cmpl_slot(struct fbnic_dev *fbd,
				   struct fbnic_fw_completion *cmpl_data)
{
	struct fbnic_fw_mbx *tx_mbx = &fbd->mbx[FBNIC_IPC_MBX_TX_IDX];
	int free = -EXFULL;
	int i;

	if (!tx_mbx->ready)
		return -ENODEV;

	for (i = 0; i < FBNIC_MBX_CMPL_SLOTS; i++) {
		if (!fbd->cmpl_data[i])
			free = i;
		else if (fbd->cmpl_data[i]->msg_type == cmpl_data->msg_type)
			return -EEXIST;
	}

	if (free == -EXFULL)
		return -EXFULL;

	fbd->cmpl_data[free] = cmpl_data;

	return 0;
}

static void fbnic_mbx_clear_cmpl_slot(struct fbnic_dev *fbd,
				      struct fbnic_fw_completion *cmpl_data)
{
	int i;

	for (i = 0; i < FBNIC_MBX_CMPL_SLOTS; i++) {
		if (fbd->cmpl_data[i] == cmpl_data) {
			fbd->cmpl_data[i] = NULL;
			break;
		}
	}
}

static void fbnic_mbx_process_tx_msgs(struct fbnic_dev *fbd)
{
	struct fbnic_fw_mbx *tx_mbx = &fbd->mbx[FBNIC_IPC_MBX_TX_IDX];
	u8 head = tx_mbx->head;
	u64 desc;

	while (head != tx_mbx->tail) {
		desc = __fbnic_mbx_rd_desc(fbd, FBNIC_IPC_MBX_TX_IDX, head);
		if (!(desc & FBNIC_IPC_MBX_DESC_FW_CMPL))
			break;

		fbnic_mbx_unmap_and_free_msg(fbd, FBNIC_IPC_MBX_TX_IDX, head);

		head++;
		head %= FBNIC_IPC_MBX_DESC_LEN;
	}

	/* Record head for next interrupt */
	tx_mbx->head = head;
}

int fbnic_mbx_set_cmpl(struct fbnic_dev *fbd,
		       struct fbnic_fw_completion *cmpl_data)
{
	unsigned long flags;
	int err;

	spin_lock_irqsave(&fbd->fw_tx_lock, flags);
	err = fbnic_mbx_set_cmpl_slot(fbd, cmpl_data);
	spin_unlock_irqrestore(&fbd->fw_tx_lock, flags);

	return err;
}

static int fbnic_mbx_map_req_w_cmpl(struct fbnic_dev *fbd,
				    struct fbnic_tlv_msg *msg,
				    struct fbnic_fw_completion *cmpl_data)
{
	unsigned long flags;
	int err;

	spin_lock_irqsave(&fbd->fw_tx_lock, flags);
	if (cmpl_data) {
		err = fbnic_mbx_set_cmpl_slot(fbd, cmpl_data);
		if (err)
			goto unlock_mbx;
	}

	err = fbnic_mbx_map_msg(fbd, FBNIC_IPC_MBX_TX_IDX, msg,
				le16_to_cpu(msg->hdr.len) * sizeof(u32), 1);

	/* If we successfully reserved a completion and msg failed
	 * then clear completion data for next caller
	 */
	if (err && cmpl_data)
		fbnic_mbx_clear_cmpl_slot(fbd, cmpl_data);

unlock_mbx:
	spin_unlock_irqrestore(&fbd->fw_tx_lock, flags);

	return err;
}

void fbnic_mbx_clear_cmpl(struct fbnic_dev *fbd,
			  struct fbnic_fw_completion *fw_cmpl)
{
	unsigned long flags;

	spin_lock_irqsave(&fbd->fw_tx_lock, flags);
	fbnic_mbx_clear_cmpl_slot(fbd, fw_cmpl);
	spin_unlock_irqrestore(&fbd->fw_tx_lock, flags);
}

static void fbnic_fw_release_cmpl_data(struct kref *kref)
{
	struct fbnic_fw_completion *cmpl_data;

	cmpl_data = container_of(kref, struct fbnic_fw_completion,
				 ref_count);
	kfree(cmpl_data);
}

static struct fbnic_fw_completion *
fbnic_fw_get_cmpl_by_type(struct fbnic_dev *fbd, u32 msg_type)
{
	struct fbnic_fw_completion *cmpl_data = NULL;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&fbd->fw_tx_lock, flags);
	for (i = 0; i < FBNIC_MBX_CMPL_SLOTS; i++) {
		if (fbd->cmpl_data[i] &&
		    fbd->cmpl_data[i]->msg_type == msg_type) {
			cmpl_data = fbd->cmpl_data[i];
			kref_get(&cmpl_data->ref_count);
			break;
		}
	}

	spin_unlock_irqrestore(&fbd->fw_tx_lock, flags);

	return cmpl_data;
}

/**
 * fbnic_fw_xmit_simple_msg - Transmit a simple single TLV message w/o data
 * @fbd: FBNIC device structure
 * @msg_type: ENUM value indicating message type to send
 *
 * Return:
 *   One the following values:
 *	-EOPNOTSUPP: Is not ASIC so mailbox is not supported
 *	-ENODEV: Device I/O error
 *	-ENOMEM: Failed to allocate message
 *	-EBUSY: No space in mailbox
 *	-ENOSPC: DMA mapping failed
 *
 * This function sends a single TLV header indicating the host wants to take
 * some action. However there are no other side effects which means that any
 * response will need to be caught via a completion if this action is
 * expected to kick off a resultant action.
 */
static int fbnic_fw_xmit_simple_msg(struct fbnic_dev *fbd, u32 msg_type)
{
	struct fbnic_tlv_msg *msg;
	int err = 0;

	if (!fbnic_fw_present(fbd))
		return -ENODEV;

	msg = fbnic_tlv_msg_alloc(msg_type);
	if (!msg)
		return -ENOMEM;

	err = fbnic_mbx_map_tlv_msg(fbd, msg);
	if (err)
		free_page((unsigned long)msg);

	return err;
}

static void fbnic_mbx_init_desc_ring(struct fbnic_dev *fbd, int mbx_idx)
{
	struct fbnic_fw_mbx *mbx = &fbd->mbx[mbx_idx];

	mbx->ready = true;

	switch (mbx_idx) {
	case FBNIC_IPC_MBX_RX_IDX:
		/* Enable DMA writes from the device */
		wr32(fbd, FBNIC_PUL_OB_TLP_HDR_AW_CFG,
		     FBNIC_PUL_OB_TLP_HDR_AW_CFG_BME);

		/* Make sure we have a page for the FW to write to */
		fbnic_mbx_alloc_rx_msgs(fbd);
		break;
	case FBNIC_IPC_MBX_TX_IDX:
		/* Enable DMA reads from the device */
		wr32(fbd, FBNIC_PUL_OB_TLP_HDR_AR_CFG,
		     FBNIC_PUL_OB_TLP_HDR_AR_CFG_BME);
		break;
	}
}

static bool fbnic_mbx_event(struct fbnic_dev *fbd)
{
	/* We only need to do this on the first interrupt following reset.
	 * this primes the mailbox so that we will have cleared all the
	 * skip descriptors.
	 */
	if (!(rd32(fbd, FBNIC_INTR_STATUS(0)) & (1u << FBNIC_FW_MSIX_ENTRY)))
		return false;

	wr32(fbd, FBNIC_INTR_CLEAR(0), 1u << FBNIC_FW_MSIX_ENTRY);

	return true;
}

/**
 * fbnic_fw_xmit_ownership_msg - Create and transmit a host ownership message
 * to FW mailbox
 *
 * @fbd: FBNIC device structure
 * @take_ownership: take/release the ownership
 *
 * Return: zero on success, negative value on failure
 *
 * Notifies the firmware that the driver either takes ownership of the NIC
 * (when @take_ownership is true) or releases it.
 */
int fbnic_fw_xmit_ownership_msg(struct fbnic_dev *fbd, bool take_ownership)
{
	unsigned long req_time = jiffies;
	struct fbnic_tlv_msg *msg;
	int err = 0;

	if (!fbnic_fw_present(fbd))
		return -ENODEV;

	msg = fbnic_tlv_msg_alloc(FBNIC_TLV_MSG_ID_OWNERSHIP_REQ);
	if (!msg)
		return -ENOMEM;

	if (take_ownership) {
		err = fbnic_tlv_attr_put_flag(msg, FBNIC_FW_OWNERSHIP_FLAG);
		if (err)
			goto free_message;
	}

	err = fbnic_mbx_map_tlv_msg(fbd, msg);
	if (err)
		goto free_message;

	/* Initialize heartbeat, set last response to 1 second in the past
	 * so that we will trigger a timeout if the firmware doesn't respond
	 */
	fbd->last_heartbeat_response = req_time - HZ;

	fbd->last_heartbeat_request = req_time;

	/* Set prev_firmware_time to 0 to avoid triggering firmware crash
	 * detection until we receive the second uptime in a heartbeat resp.
	 */
	fbd->prev_firmware_time = 0;

	/* Set heartbeat detection based on if we are taking ownership */
	fbd->fw_heartbeat_enabled = take_ownership;

	return err;

free_message:
	free_page((unsigned long)msg);
	return err;
}

static const struct fbnic_tlv_index fbnic_fw_cap_resp_index[] = {
	FBNIC_TLV_ATTR_U32(FBNIC_FW_CAP_RESP_VERSION),
	FBNIC_TLV_ATTR_FLAG(FBNIC_FW_CAP_RESP_BMC_PRESENT),
	FBNIC_TLV_ATTR_MAC_ADDR(FBNIC_FW_CAP_RESP_BMC_MAC_ADDR),
	FBNIC_TLV_ATTR_ARRAY(FBNIC_FW_CAP_RESP_BMC_MAC_ARRAY),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_CAP_RESP_STORED_VERSION),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_CAP_RESP_ACTIVE_FW_SLOT),
	FBNIC_TLV_ATTR_STRING(FBNIC_FW_CAP_RESP_VERSION_COMMIT_STR,
			      FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_CAP_RESP_BMC_ALL_MULTI),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_CAP_RESP_FW_LINK_SPEED),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_CAP_RESP_FW_LINK_FEC),
	FBNIC_TLV_ATTR_STRING(FBNIC_FW_CAP_RESP_STORED_COMMIT_STR,
			      FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_CAP_RESP_CMRT_VERSION),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_CAP_RESP_STORED_CMRT_VERSION),
	FBNIC_TLV_ATTR_STRING(FBNIC_FW_CAP_RESP_CMRT_COMMIT_STR,
			      FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE),
	FBNIC_TLV_ATTR_STRING(FBNIC_FW_CAP_RESP_STORED_CMRT_COMMIT_STR,
			      FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_CAP_RESP_UEFI_VERSION),
	FBNIC_TLV_ATTR_STRING(FBNIC_FW_CAP_RESP_UEFI_COMMIT_STR,
			      FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_CAP_RESP_ANTI_ROLLBACK_VERSION),
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_parse_bmc_addrs(u8 bmc_mac_addr[][ETH_ALEN],
				    struct fbnic_tlv_msg *attr, int len)
{
	int attr_len = le16_to_cpu(attr->hdr.len) / sizeof(u32) - 1;
	struct fbnic_tlv_msg *mac_results[8];
	int err, i = 0;

	/* Make sure we have enough room to process all the MAC addresses */
	if (len > 8)
		return -ENOSPC;

	/* Parse the array */
	err = fbnic_tlv_attr_parse_array(&attr[1], attr_len, mac_results,
					 fbnic_fw_cap_resp_index,
					 FBNIC_FW_CAP_RESP_BMC_MAC_ADDR, len);
	if (err)
		return err;

	/* Copy results into MAC addr array */
	for (i = 0; i < len && mac_results[i]; i++)
		fbnic_tlv_attr_addr_copy(bmc_mac_addr[i], mac_results[i]);

	/* Zero remaining unused addresses */
	while (i < len)
		eth_zero_addr(bmc_mac_addr[i++]);

	return 0;
}

static int fbnic_fw_parse_cap_resp(void *opaque, struct fbnic_tlv_msg **results)
{
	u32 all_multi = 0, version = 0;
	struct fbnic_dev *fbd = opaque;
	bool bmc_present;
	int err;

	version = fta_get_uint(results, FBNIC_FW_CAP_RESP_VERSION);
	fbd->fw_cap.running.mgmt.version = version;
	if (!fbd->fw_cap.running.mgmt.version)
		return -EINVAL;

	if (fbd->fw_cap.running.mgmt.version < MIN_FW_VER_CODE) {
		char required_ver[FBNIC_FW_VER_MAX_SIZE];
		char running_ver[FBNIC_FW_VER_MAX_SIZE];

		fbnic_mk_fw_ver_str(fbd->fw_cap.running.mgmt.version,
				    running_ver);
		fbnic_mk_fw_ver_str(MIN_FW_VER_CODE, required_ver);
		dev_err(fbd->dev, "Device firmware version(%s) is older than minimum required version(%s)\n",
			running_ver, required_ver);
		/* Disable TX mailbox to prevent card use until firmware is
		 * updated.
		 */
		fbd->mbx[FBNIC_IPC_MBX_TX_IDX].ready = false;
		return -EINVAL;
	}

	if (fta_get_str(results, FBNIC_FW_CAP_RESP_VERSION_COMMIT_STR,
			fbd->fw_cap.running.mgmt.commit,
			FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE) <= 0)
		dev_warn(fbd->dev, "Firmware did not send mgmt commit!\n");

	version = fta_get_uint(results, FBNIC_FW_CAP_RESP_STORED_VERSION);
	fbd->fw_cap.stored.mgmt.version = version;
	fta_get_str(results, FBNIC_FW_CAP_RESP_STORED_COMMIT_STR,
		    fbd->fw_cap.stored.mgmt.commit,
		    FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE);

	version = fta_get_uint(results, FBNIC_FW_CAP_RESP_CMRT_VERSION);
	fbd->fw_cap.running.bootloader.version = version;
	fta_get_str(results, FBNIC_FW_CAP_RESP_CMRT_COMMIT_STR,
		    fbd->fw_cap.running.bootloader.commit,
		    FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE);

	version = fta_get_uint(results, FBNIC_FW_CAP_RESP_STORED_CMRT_VERSION);
	fbd->fw_cap.stored.bootloader.version = version;
	fta_get_str(results, FBNIC_FW_CAP_RESP_STORED_CMRT_COMMIT_STR,
		    fbd->fw_cap.stored.bootloader.commit,
		    FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE);

	version = fta_get_uint(results, FBNIC_FW_CAP_RESP_UEFI_VERSION);
	fbd->fw_cap.stored.undi.version = version;
	fta_get_str(results, FBNIC_FW_CAP_RESP_UEFI_COMMIT_STR,
		    fbd->fw_cap.stored.undi.commit,
		    FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE);

	fbd->fw_cap.active_slot =
		fta_get_uint(results, FBNIC_FW_CAP_RESP_ACTIVE_FW_SLOT);
	fbd->fw_cap.link_speed =
		fta_get_uint(results, FBNIC_FW_CAP_RESP_FW_LINK_SPEED);
	fbd->fw_cap.link_fec =
		fta_get_uint(results, FBNIC_FW_CAP_RESP_FW_LINK_FEC);

	bmc_present = !!results[FBNIC_FW_CAP_RESP_BMC_PRESENT];
	if (bmc_present) {
		struct fbnic_tlv_msg *attr;

		attr = results[FBNIC_FW_CAP_RESP_BMC_MAC_ARRAY];
		if (!attr)
			return -EINVAL;

		err = fbnic_fw_parse_bmc_addrs(fbd->fw_cap.bmc_mac_addr,
					       attr, 4);
		if (err)
			return err;

		all_multi =
			fta_get_uint(results, FBNIC_FW_CAP_RESP_BMC_ALL_MULTI);
	} else {
		memset(fbd->fw_cap.bmc_mac_addr, 0,
		       sizeof(fbd->fw_cap.bmc_mac_addr));
	}

	fbd->fw_cap.bmc_present = bmc_present;

	if (results[FBNIC_FW_CAP_RESP_BMC_ALL_MULTI] || !bmc_present)
		fbd->fw_cap.all_multi = all_multi;

	fbd->fw_cap.anti_rollback_version =
		fta_get_uint(results, FBNIC_FW_CAP_RESP_ANTI_ROLLBACK_VERSION);

	/* Always assume we need a BMC reinit */
	fbd->fw_cap.need_bmc_tcam_reinit = true;

	return 0;
}

static const struct fbnic_tlv_index fbnic_ownership_resp_index[] = {
	FBNIC_TLV_ATTR_U64(FBNIC_FW_OWNERSHIP_TIME),
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_parse_ownership_resp(void *opaque,
					 struct fbnic_tlv_msg **results)
{
	struct fbnic_dev *fbd = (struct fbnic_dev *)opaque;

	/* Count the ownership response as a heartbeat reply */
	fbd->last_heartbeat_response = jiffies;

	/* Capture firmware time for logging and firmware crash check */
	fbd->firmware_time = fta_get_uint(results, FBNIC_FW_OWNERSHIP_TIME);

	return 0;
}

static const struct fbnic_tlv_index fbnic_heartbeat_resp_index[] = {
	FBNIC_TLV_ATTR_U64(FBNIC_FW_HEARTBEAT_UPTIME),
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_parse_heartbeat_resp(void *opaque,
					 struct fbnic_tlv_msg **results)
{
	struct fbnic_dev *fbd = (struct fbnic_dev *)opaque;

	fbd->last_heartbeat_response = jiffies;

	/* Capture firmware time for logging and firmware crash check */
	fbd->firmware_time = fta_get_uint(results, FBNIC_FW_HEARTBEAT_UPTIME);

	return 0;
}

static int fbnic_fw_xmit_heartbeat_message(struct fbnic_dev *fbd)
{
	unsigned long req_time = jiffies;
	struct fbnic_tlv_msg *msg;
	int err = 0;

	if (!fbnic_fw_present(fbd))
		return -ENODEV;

	msg = fbnic_tlv_msg_alloc(FBNIC_TLV_MSG_ID_HEARTBEAT_REQ);
	if (!msg)
		return -ENOMEM;

	err = fbnic_mbx_map_tlv_msg(fbd, msg);
	if (err)
		goto free_message;

	fbd->last_heartbeat_request = req_time;
	fbd->prev_firmware_time = fbd->firmware_time;

	return err;

free_message:
	free_page((unsigned long)msg);
	return err;
}

static bool fbnic_fw_heartbeat_current(struct fbnic_dev *fbd)
{
	unsigned long last_response = fbd->last_heartbeat_response;
	unsigned long last_request = fbd->last_heartbeat_request;

	return !time_before(last_response, last_request);
}

int fbnic_fw_init_heartbeat(struct fbnic_dev *fbd, bool poll)
{
	int err = -ETIMEDOUT;
	int attempts = 50;

	if (!fbnic_fw_present(fbd))
		return -ENODEV;

	while (attempts--) {
		msleep(200);
		if (poll)
			fbnic_mbx_poll(fbd);

		if (!fbnic_fw_heartbeat_current(fbd))
			continue;

		/* Place new message on mailbox to elicit a response */
		err = fbnic_fw_xmit_heartbeat_message(fbd);
		if (err)
			dev_warn(fbd->dev,
				 "Failed to send heartbeat message: %d\n",
				 err);
		break;
	}

	return err;
}

void fbnic_fw_check_heartbeat(struct fbnic_dev *fbd)
{
	unsigned long last_request = fbd->last_heartbeat_request;
	int err;

	/* Do not check heartbeat or send another request until current
	 * period has expired. Otherwise we might start spamming requests.
	 */
	if (time_is_after_jiffies(last_request + FW_HEARTBEAT_PERIOD))
		return;

	/* We already reported no mailbox. Wait for it to come back */
	if (!fbd->fw_heartbeat_enabled)
		return;

	/* Was the last heartbeat response long time ago? */
	if (!fbnic_fw_heartbeat_current(fbd) ||
	    fbd->firmware_time < fbd->prev_firmware_time) {
		dev_warn(fbd->dev,
			 "Firmware did not respond to heartbeat message\n");
		fbd->fw_heartbeat_enabled = false;
	}

	/* Place new message on mailbox to elicit a response */
	err = fbnic_fw_xmit_heartbeat_message(fbd);
	if (err)
		dev_warn(fbd->dev, "Failed to send heartbeat message\n");
}

/**
 * fbnic_fw_xmit_coredump_info_msg - Create and transmit a coredump info message
 * @fbd: FBNIC device structure
 * @cmpl_data: Structure to store info in
 * @force: Force coredump event if one hasn't already occurred
 *
 * Return: zero on success, negative errno on failure
 *
 * Asks the FW for info related to coredump. If a coredump doesn't exist it
 * can optionally force one if force is true.
 */
int fbnic_fw_xmit_coredump_info_msg(struct fbnic_dev *fbd,
				    struct fbnic_fw_completion *cmpl_data,
				    bool force)
{
	struct fbnic_tlv_msg *msg;
	int err = 0;

	msg = fbnic_tlv_msg_alloc(FBNIC_TLV_MSG_ID_COREDUMP_GET_INFO_REQ);
	if (!msg)
		return -ENOMEM;

	if (force) {
		err = fbnic_tlv_attr_put_flag(msg, FBNIC_FW_COREDUMP_REQ_INFO_CREATE);
		if (err)
			goto free_msg;
	}

	err = fbnic_mbx_map_req_w_cmpl(fbd, msg, cmpl_data);
	if (err)
		goto free_msg;

	return 0;

free_msg:
	free_page((unsigned long)msg);
	return err;
}

static const struct fbnic_tlv_index fbnic_coredump_info_resp_index[] = {
	FBNIC_TLV_ATTR_FLAG(FBNIC_FW_COREDUMP_INFO_AVAILABLE),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_COREDUMP_INFO_SIZE),
	FBNIC_TLV_ATTR_S32(FBNIC_FW_COREDUMP_INFO_ERROR),
	FBNIC_TLV_ATTR_LAST
};

static int
fbnic_fw_parse_coredump_info_resp(void *opaque, struct fbnic_tlv_msg **results)
{
	struct fbnic_fw_completion *cmpl_data;
	struct fbnic_dev *fbd = opaque;
	u32 msg_type;
	s32 err;

	/* Verify we have a completion pointer to provide with data */
	msg_type = FBNIC_TLV_MSG_ID_COREDUMP_GET_INFO_RESP;
	cmpl_data = fbnic_fw_get_cmpl_by_type(fbd, msg_type);
	if (!cmpl_data)
		return -ENOSPC;

	err = fta_get_sint(results, FBNIC_FW_COREDUMP_INFO_ERROR);
	if (err)
		goto msg_err;

	if (!results[FBNIC_FW_COREDUMP_INFO_AVAILABLE]) {
		err = -ENOENT;
		goto msg_err;
	}

	cmpl_data->u.coredump_info.size =
		fta_get_uint(results, FBNIC_FW_COREDUMP_INFO_SIZE);

msg_err:
	cmpl_data->result = err;
	complete(&cmpl_data->done);
	fbnic_fw_put_cmpl(cmpl_data);

	return err;
}

/**
 * fbnic_fw_xmit_coredump_read_msg - Create and transmit a coredump read request
 * @fbd: FBNIC device structure
 * @cmpl_data: Completion struct to store coredump
 * @offset: Offset into coredump requested
 * @length: Length of section of cordeump to fetch
 *
 * Return: zero on success, negative errno on failure
 *
 * Asks the firmware to provide a section of the cordeump back in a message.
 * The response will have an offset and size matching the values provided.
 */
int fbnic_fw_xmit_coredump_read_msg(struct fbnic_dev *fbd,
				    struct fbnic_fw_completion *cmpl_data,
				    u32 offset, u32 length)
{
	struct fbnic_tlv_msg *msg;
	int err = 0;

	msg = fbnic_tlv_msg_alloc(FBNIC_TLV_MSG_ID_COREDUMP_READ_REQ);
	if (!msg)
		return -ENOMEM;

	if (offset) {
		err = fbnic_tlv_attr_put_int(msg, FBNIC_FW_COREDUMP_READ_OFFSET,
					     offset);
		if (err)
			goto free_message;
	}

	if (length) {
		err = fbnic_tlv_attr_put_int(msg, FBNIC_FW_COREDUMP_READ_LENGTH,
					     length);
		if (err)
			goto free_message;
	}

	err = fbnic_mbx_map_req_w_cmpl(fbd, msg, cmpl_data);
	if (err)
		goto free_message;

	return 0;

free_message:
	free_page((unsigned long)msg);
	return err;
}

static const struct fbnic_tlv_index fbnic_coredump_resp_index[] = {
	FBNIC_TLV_ATTR_U32(FBNIC_FW_COREDUMP_READ_OFFSET),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_COREDUMP_READ_LENGTH),
	FBNIC_TLV_ATTR_RAW_DATA(FBNIC_FW_COREDUMP_READ_DATA),
	FBNIC_TLV_ATTR_S32(FBNIC_FW_COREDUMP_READ_ERROR),
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_parse_coredump_resp(void *opaque,
					struct fbnic_tlv_msg **results)
{
	struct fbnic_fw_completion *cmpl_data;
	u32 index, last_offset, last_length;
	struct fbnic_dev *fbd = opaque;
	struct fbnic_tlv_msg *data_hdr;
	u32 length, offset;
	u32 msg_type;
	s32 err;

	/* Verify we have a completion pointer to provide with data */
	msg_type = FBNIC_TLV_MSG_ID_COREDUMP_READ_RESP;
	cmpl_data = fbnic_fw_get_cmpl_by_type(fbd, msg_type);
	if (!cmpl_data)
		return -ENOSPC;

	err = fta_get_sint(results, FBNIC_FW_COREDUMP_READ_ERROR);
	if (err)
		goto msg_err;

	data_hdr = results[FBNIC_FW_COREDUMP_READ_DATA];
	if (!data_hdr) {
		err = -ENODATA;
		goto msg_err;
	}

	offset = fta_get_uint(results, FBNIC_FW_COREDUMP_READ_OFFSET);
	length = fta_get_uint(results, FBNIC_FW_COREDUMP_READ_LENGTH);

	if (length > le16_to_cpu(data_hdr->hdr.len) - sizeof(u32)) {
		dev_err(fbd->dev, "length greater than size of message\n");
		err = -EINVAL;
		goto msg_err;
	}

	/* Only the last offset can have a length != stride */
	last_length =
		(cmpl_data->u.coredump.size % cmpl_data->u.coredump.stride) ? :
		cmpl_data->u.coredump.stride;
	last_offset = cmpl_data->u.coredump.size - last_length;

	/* Verify offset and length */
	if (offset % cmpl_data->u.coredump.stride || offset > last_offset) {
		dev_err(fbd->dev, "offset %d out of range\n", offset);
		err = -EINVAL;
	} else if (length != ((offset == last_offset) ?
			      last_length : cmpl_data->u.coredump.stride)) {
		dev_err(fbd->dev, "length %d out of range for offset %d\n",
			length, offset);
		err = -EINVAL;
	}
	if (err)
		goto msg_err;

	/* If data pointer is NULL it is already filled, just skip the copy */
	index = offset / cmpl_data->u.coredump.stride;
	if (!cmpl_data->u.coredump.data[index])
		goto msg_err;

	/* Copy data and mark index filled by setting pointer to NULL */
	memcpy(cmpl_data->u.coredump.data[index],
	       fbnic_tlv_attr_get_value_ptr(data_hdr), length);
	cmpl_data->u.coredump.data[index] = NULL;

msg_err:
	cmpl_data->result = err;
	complete(&cmpl_data->done);
	fbnic_fw_put_cmpl(cmpl_data);

	return err;
}

int fbnic_fw_xmit_fw_start_upgrade(struct fbnic_dev *fbd,
				   struct fbnic_fw_completion *cmpl_data,
				   unsigned int id, unsigned int len)
{
	struct fbnic_tlv_msg *msg;
	int err;

	if (!fbnic_fw_present(fbd))
		return -ENODEV;

	if (!len)
		return -EINVAL;

	msg = fbnic_tlv_msg_alloc(FBNIC_TLV_MSG_ID_FW_START_UPGRADE_REQ);
	if (!msg)
		return -ENOMEM;

	err = fbnic_tlv_attr_put_int(msg, FBNIC_FW_START_UPGRADE_SECTION, id);
	if (err)
		goto free_message;

	err = fbnic_tlv_attr_put_int(msg, FBNIC_FW_START_UPGRADE_IMAGE_LENGTH,
				     len);
	if (err)
		goto free_message;

	err = fbnic_mbx_map_req_w_cmpl(fbd, msg, cmpl_data);
	if (err)
		goto free_message;

	return 0;

free_message:
	free_page((unsigned long)msg);
	return err;
}

static const struct fbnic_tlv_index fbnic_fw_start_upgrade_resp_index[] = {
	FBNIC_TLV_ATTR_S32(FBNIC_FW_START_UPGRADE_ERROR),
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_parse_fw_start_upgrade_resp(void *opaque,
						struct fbnic_tlv_msg **results)
{
	struct fbnic_fw_completion *cmpl_data;
	struct fbnic_dev *fbd = opaque;
	u32 msg_type;
	s32 err;

	/* Verify we have a completion pointer */
	msg_type = FBNIC_TLV_MSG_ID_FW_START_UPGRADE_REQ;
	cmpl_data = fbnic_fw_get_cmpl_by_type(fbd, msg_type);
	if (!cmpl_data)
		return -ENOSPC;

	/* Check for errors */
	err = fta_get_sint(results, FBNIC_FW_START_UPGRADE_ERROR);

	cmpl_data->result = err;
	complete(&cmpl_data->done);
	fbnic_fw_put_cmpl(cmpl_data);

	return 0;
}

int fbnic_fw_xmit_fw_write_chunk(struct fbnic_dev *fbd,
				 const u8 *data, u32 offset, u16 length,
				 int cancel_error)
{
	struct fbnic_tlv_msg *msg;
	int err;

	msg = fbnic_tlv_msg_alloc(FBNIC_TLV_MSG_ID_FW_WRITE_CHUNK_RESP);
	if (!msg)
		return -ENOMEM;

	/* Report error to FW to cancel upgrade */
	if (cancel_error) {
		err = fbnic_tlv_attr_put_int(msg, FBNIC_FW_WRITE_CHUNK_ERROR,
					     cancel_error);
		if (err)
			goto free_message;
	}

	if (data) {
		err = fbnic_tlv_attr_put_int(msg, FBNIC_FW_WRITE_CHUNK_OFFSET,
					     offset);
		if (err)
			goto free_message;

		err = fbnic_tlv_attr_put_int(msg, FBNIC_FW_WRITE_CHUNK_LENGTH,
					     length);
		if (err)
			goto free_message;

		err = fbnic_tlv_attr_put_value(msg, FBNIC_FW_WRITE_CHUNK_DATA,
					       data + offset, length);
		if (err)
			goto free_message;
	}

	err = fbnic_mbx_map_tlv_msg(fbd, msg);
	if (err)
		goto free_message;

	return 0;

free_message:
	free_page((unsigned long)msg);
	return err;
}

static const struct fbnic_tlv_index fbnic_fw_write_chunk_req_index[] = {
	FBNIC_TLV_ATTR_U32(FBNIC_FW_WRITE_CHUNK_OFFSET),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_WRITE_CHUNK_LENGTH),
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_parse_fw_write_chunk_req(void *opaque,
					     struct fbnic_tlv_msg **results)
{
	struct fbnic_fw_completion *cmpl_data;
	struct fbnic_dev *fbd = opaque;
	u32 msg_type;
	u32 offset;
	u32 length;

	/* Verify we have a completion pointer */
	msg_type = FBNIC_TLV_MSG_ID_FW_WRITE_CHUNK_REQ;
	cmpl_data = fbnic_fw_get_cmpl_by_type(fbd, msg_type);
	if (!cmpl_data)
		return -ENOSPC;

	/* Pull length/offset pair and mark it as complete */
	offset = fta_get_uint(results, FBNIC_FW_WRITE_CHUNK_OFFSET);
	length = fta_get_uint(results, FBNIC_FW_WRITE_CHUNK_LENGTH);
	cmpl_data->u.fw_update.offset = offset;
	cmpl_data->u.fw_update.length = length;

	complete(&cmpl_data->done);
	fbnic_fw_put_cmpl(cmpl_data);

	return 0;
}

static const struct fbnic_tlv_index fbnic_fw_finish_upgrade_req_index[] = {
	FBNIC_TLV_ATTR_S32(FBNIC_FW_FINISH_UPGRADE_ERROR),
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_parse_fw_finish_upgrade_req(void *opaque,
						struct fbnic_tlv_msg **results)
{
	struct fbnic_fw_completion *cmpl_data;
	struct fbnic_dev *fbd = opaque;
	u32 msg_type;
	s32 err;

	/* Verify we have a completion pointer */
	msg_type = FBNIC_TLV_MSG_ID_FW_WRITE_CHUNK_REQ;
	cmpl_data = fbnic_fw_get_cmpl_by_type(fbd, msg_type);
	if (!cmpl_data)
		return -ENOSPC;

	/* Check for errors */
	err = fta_get_sint(results, FBNIC_FW_FINISH_UPGRADE_ERROR);

	/* Close out update by incrementing offset by length which should
	 * match the total size of the component. Set length to 0 since no
	 * new chunks will be requested.
	 */
	cmpl_data->u.fw_update.offset += cmpl_data->u.fw_update.length;
	cmpl_data->u.fw_update.length = 0;

	cmpl_data->result = err;
	complete(&cmpl_data->done);
	fbnic_fw_put_cmpl(cmpl_data);

	return 0;
}

/**
 * fbnic_fw_xmit_qsfp_read_msg - Transmit a QSFP read request
 * @fbd: FBNIC device structure
 * @cmpl_data: Structure to store EEPROM response in
 * @page: Refers to page number on page enabled QSFP modules
 * @bank: Refers to a collection of pages
 * @offset: Offset into QSFP EEPROM requested
 * @length: Length of section of QSFP EEPROM to fetch
 *
 * Return: zero on success, negative value on failure
 *
 * Asks the firmware to provide a section of the QSFP EEPROM back in a
 * message. The response will have an offset and size matching the values
 * provided.
 */
int fbnic_fw_xmit_qsfp_read_msg(struct fbnic_dev *fbd,
				struct fbnic_fw_completion *cmpl_data,
				u32 page, u32 bank, u32 offset, u32 length)
{
	struct fbnic_tlv_msg *msg;
	int err = 0;

	if (!length || length > TLV_MAX_DATA)
		return -EINVAL;

	msg = fbnic_tlv_msg_alloc(FBNIC_TLV_MSG_ID_QSFP_READ_REQ);
	if (!msg)
		return -ENOMEM;

	err = fbnic_tlv_attr_put_int(msg, FBNIC_FW_QSFP_BANK, bank);
	if (err)
		goto free_message;

	err = fbnic_tlv_attr_put_int(msg, FBNIC_FW_QSFP_PAGE, page);
	if (err)
		goto free_message;

	err = fbnic_tlv_attr_put_int(msg, FBNIC_FW_QSFP_OFFSET, offset);
	if (err)
		goto free_message;

	err = fbnic_tlv_attr_put_int(msg, FBNIC_FW_QSFP_LENGTH, length);
	if (err)
		goto free_message;

	err = fbnic_mbx_map_req_w_cmpl(fbd, msg, cmpl_data);
	if (err)
		goto free_message;

	return 0;

free_message:
	free_page((unsigned long)msg);
	return err;
}

static const struct fbnic_tlv_index fbnic_qsfp_read_resp_index[] = {
	FBNIC_TLV_ATTR_U32(FBNIC_FW_QSFP_BANK),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_QSFP_PAGE),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_QSFP_OFFSET),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_QSFP_LENGTH),
	FBNIC_TLV_ATTR_RAW_DATA(FBNIC_FW_QSFP_DATA),
	FBNIC_TLV_ATTR_S32(FBNIC_FW_QSFP_ERROR),
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_parse_qsfp_read_resp(void *opaque,
					 struct fbnic_tlv_msg **results)
{
	struct fbnic_fw_completion *cmpl_data;
	struct fbnic_dev *fbd = opaque;
	struct fbnic_tlv_msg *data_hdr;
	u32 length, offset, page, bank;
	u8 *data;
	s32 err;

	/* Verify we have a completion pointer to provide with data */
	cmpl_data = fbnic_fw_get_cmpl_by_type(fbd,
					      FBNIC_TLV_MSG_ID_QSFP_READ_RESP);
	if (!cmpl_data)
		return -ENOSPC;

	bank = fta_get_uint(results, FBNIC_FW_QSFP_BANK);
	if (bank != cmpl_data->u.qsfp.bank) {
		dev_warn(fbd->dev, "bank not equal to bank requested: %d vs %d\n",
			 bank, cmpl_data->u.qsfp.bank);
		err = -EINVAL;
		goto msg_err;
	}

	page = fta_get_uint(results, FBNIC_FW_QSFP_PAGE);
	if (page != cmpl_data->u.qsfp.page) {
		dev_warn(fbd->dev, "page not equal to page requested: %d vs %d\n",
			 page, cmpl_data->u.qsfp.page);
		err = -EINVAL;
		goto msg_err;
	}

	offset = fta_get_uint(results, FBNIC_FW_QSFP_OFFSET);
	length = fta_get_uint(results, FBNIC_FW_QSFP_LENGTH);

	if (length != cmpl_data->u.qsfp.length ||
	    offset != cmpl_data->u.qsfp.offset) {
		dev_warn(fbd->dev,
			 "offset/length not equal to size requested: %d/%d vs %d/%d\n",
			 offset, length,
			 cmpl_data->u.qsfp.offset, cmpl_data->u.qsfp.length);
		err = -EINVAL;
		goto msg_err;
	}

	err = fta_get_sint(results, FBNIC_FW_QSFP_ERROR);
	if (err)
		goto msg_err;

	data_hdr = results[FBNIC_FW_QSFP_DATA];
	if (!data_hdr) {
		err = -ENODATA;
		goto msg_err;
	}

	/* Copy data */
	data = fbnic_tlv_attr_get_value_ptr(data_hdr);
	memcpy(cmpl_data->u.qsfp.data, data, length);
msg_err:
	cmpl_data->result = err;
	complete(&cmpl_data->done);
	fbnic_fw_put_cmpl(cmpl_data);

	return err;
}

/**
 * fbnic_fw_xmit_tsene_read_msg - Create and transmit a sensor read request
 * @fbd: FBNIC device structure
 * @cmpl_data: Completion data structure to store sensor response
 *
 * Asks the firmware to provide an update with the latest sensor data.
 * The response will contain temperature and voltage readings.
 *
 * Return: 0 on success, negative error value on failure
 */
int fbnic_fw_xmit_tsene_read_msg(struct fbnic_dev *fbd,
				 struct fbnic_fw_completion *cmpl_data)
{
	struct fbnic_tlv_msg *msg;
	int err;

	if (!fbnic_fw_present(fbd))
		return -ENODEV;

	msg = fbnic_tlv_msg_alloc(FBNIC_TLV_MSG_ID_TSENE_READ_REQ);
	if (!msg)
		return -ENOMEM;

	err = fbnic_mbx_map_req_w_cmpl(fbd, msg, cmpl_data);
	if (err)
		goto free_message;

	return 0;

free_message:
	free_page((unsigned long)msg);
	return err;
}

static const struct fbnic_tlv_index fbnic_tsene_read_resp_index[] = {
	FBNIC_TLV_ATTR_S32(FBNIC_FW_TSENE_THERM),
	FBNIC_TLV_ATTR_S32(FBNIC_FW_TSENE_VOLT),
	FBNIC_TLV_ATTR_S32(FBNIC_FW_TSENE_ERROR),
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_parse_tsene_read_resp(void *opaque,
					  struct fbnic_tlv_msg **results)
{
	struct fbnic_fw_completion *cmpl_data;
	struct fbnic_dev *fbd = opaque;
	s32 err_resp;
	int err = 0;

	/* Verify we have a completion pointer to provide with data */
	cmpl_data = fbnic_fw_get_cmpl_by_type(fbd,
					      FBNIC_TLV_MSG_ID_TSENE_READ_RESP);
	if (!cmpl_data)
		return -ENOSPC;

	err_resp = fta_get_sint(results, FBNIC_FW_TSENE_ERROR);
	if (err_resp)
		goto msg_err;

	if (!results[FBNIC_FW_TSENE_THERM] || !results[FBNIC_FW_TSENE_VOLT]) {
		err = -EINVAL;
		goto msg_err;
	}

	cmpl_data->u.tsene.millidegrees =
		fta_get_sint(results, FBNIC_FW_TSENE_THERM);
	cmpl_data->u.tsene.millivolts =
		fta_get_sint(results, FBNIC_FW_TSENE_VOLT);

msg_err:
	cmpl_data->result = err_resp ? : err;
	complete(&cmpl_data->done);
	fbnic_fw_put_cmpl(cmpl_data);

	return err;
}

static const struct fbnic_tlv_index fbnic_fw_log_req_index[] = {
	FBNIC_TLV_ATTR_U32(FBNIC_FW_LOG_MSEC),
	FBNIC_TLV_ATTR_U64(FBNIC_FW_LOG_INDEX),
	FBNIC_TLV_ATTR_STRING(FBNIC_FW_LOG_MSG, FBNIC_FW_LOG_MAX_SIZE),
	FBNIC_TLV_ATTR_U32(FBNIC_FW_LOG_LENGTH),
	FBNIC_TLV_ATTR_ARRAY(FBNIC_FW_LOG_MSEC_ARRAY),
	FBNIC_TLV_ATTR_ARRAY(FBNIC_FW_LOG_INDEX_ARRAY),
	FBNIC_TLV_ATTR_ARRAY(FBNIC_FW_LOG_MSG_ARRAY),
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_process_log_array(struct fbnic_tlv_msg **results,
				      u16 length, u16 arr_type_idx,
				      u16 attr_type_idx,
				      struct fbnic_tlv_msg **tlv_array_out)
{
	struct fbnic_tlv_msg *attr;
	int attr_len;
	int err;

	if (!results[attr_type_idx])
		return -EINVAL;

	tlv_array_out[0] = results[attr_type_idx];

	if (!length)
		return 0;

	if (!results[arr_type_idx])
		return -EINVAL;

	attr = results[arr_type_idx];
	attr_len = le16_to_cpu(attr->hdr.len) / sizeof(u32) - 1;
	err = fbnic_tlv_attr_parse_array(&attr[1], attr_len, &tlv_array_out[1],
					 fbnic_fw_log_req_index,
					 attr_type_idx,
					 length);
	if (err)
		return err;

	return 0;
}

static int fbnic_fw_parse_logs(struct fbnic_dev *fbd,
			       struct fbnic_tlv_msg **msec_tlv,
			       struct fbnic_tlv_msg **index_tlv,
			       struct fbnic_tlv_msg **log_tlv,
			       int count)
{
	int i;

	for (i = 0; i < count; i++) {
		char log[FBNIC_FW_LOG_MAX_SIZE];
		ssize_t len;
		u64 index;
		u32 msec;
		int err;

		if (!msec_tlv[i] || !index_tlv[i] || !log_tlv[i]) {
			dev_warn(fbd->dev, "Received log message with missing attributes!\n");
			return -EINVAL;
		}

		index = fbnic_tlv_attr_get_signed(index_tlv[i], 0);
		msec = fbnic_tlv_attr_get_signed(msec_tlv[i], 0);
		len = fbnic_tlv_attr_get_string(log_tlv[i], log,
						FBNIC_FW_LOG_MAX_SIZE);
		if (len < 0)
			return len;

		err = fbnic_fw_log_write(fbd, index, msec, log);
		if (err)
			return err;
	}

	return 0;
}

static int fbnic_fw_parse_log_req(void *opaque,
				  struct fbnic_tlv_msg **results)
{
	struct fbnic_tlv_msg *index_tlv[FBNIC_FW_MAX_LOG_HISTORY];
	struct fbnic_tlv_msg *msec_tlv[FBNIC_FW_MAX_LOG_HISTORY];
	struct fbnic_tlv_msg *log_tlv[FBNIC_FW_MAX_LOG_HISTORY];
	struct fbnic_dev *fbd = opaque;
	u16 length;
	int err;

	length = fta_get_uint(results, FBNIC_FW_LOG_LENGTH);
	if (length >= FBNIC_FW_MAX_LOG_HISTORY)
		return -E2BIG;

	err = fbnic_fw_process_log_array(results, length,
					 FBNIC_FW_LOG_MSEC_ARRAY,
					 FBNIC_FW_LOG_MSEC, msec_tlv);
	if (err)
		return err;

	err = fbnic_fw_process_log_array(results, length,
					 FBNIC_FW_LOG_INDEX_ARRAY,
					 FBNIC_FW_LOG_INDEX, index_tlv);
	if (err)
		return err;

	err = fbnic_fw_process_log_array(results, length,
					 FBNIC_FW_LOG_MSG_ARRAY,
					 FBNIC_FW_LOG_MSG, log_tlv);
	if (err)
		return err;

	err = fbnic_fw_parse_logs(fbd, msec_tlv, index_tlv, log_tlv,
				  length + 1);
	if (err)
		return err;

	return 0;
}

int fbnic_fw_xmit_send_logs(struct fbnic_dev *fbd, bool enable,
			    bool send_log_history)
{
	struct fbnic_tlv_msg *msg;
	int err;

	if (fbd->fw_cap.running.mgmt.version < MIN_FW_VER_CODE_LOG) {
		dev_warn(fbd->dev, "Firmware version is too old to support firmware logs!\n");
		return -EOPNOTSUPP;
	}

	msg = fbnic_tlv_msg_alloc(FBNIC_TLV_MSG_ID_LOG_SEND_LOGS_REQ);
	if (!msg)
		return -ENOMEM;

	if (enable) {
		err = fbnic_tlv_attr_put_flag(msg, FBNIC_SEND_LOGS);
		if (err)
			goto free_message;

		/* Report request for version 1 of logs */
		err = fbnic_tlv_attr_put_int(msg, FBNIC_SEND_LOGS_VERSION,
					     FBNIC_FW_LOG_VERSION);
		if (err)
			goto free_message;

		if (send_log_history) {
			err = fbnic_tlv_attr_put_flag(msg,
						      FBNIC_SEND_LOGS_HISTORY);
			if (err)
				goto free_message;
		}
	}

	err = fbnic_mbx_map_tlv_msg(fbd, msg);
	if (err)
		goto free_message;

	return 0;

free_message:
	free_page((unsigned long)msg);
	return err;
}

static const struct fbnic_tlv_parser fbnic_fw_tlv_parser[] = {
	FBNIC_TLV_PARSER(FW_CAP_RESP, fbnic_fw_cap_resp_index,
			 fbnic_fw_parse_cap_resp),
	FBNIC_TLV_PARSER(OWNERSHIP_RESP, fbnic_ownership_resp_index,
			 fbnic_fw_parse_ownership_resp),
	FBNIC_TLV_PARSER(HEARTBEAT_RESP, fbnic_heartbeat_resp_index,
			 fbnic_fw_parse_heartbeat_resp),
	FBNIC_TLV_PARSER(COREDUMP_GET_INFO_RESP,
			 fbnic_coredump_info_resp_index,
			 fbnic_fw_parse_coredump_info_resp),
	FBNIC_TLV_PARSER(COREDUMP_READ_RESP, fbnic_coredump_resp_index,
			 fbnic_fw_parse_coredump_resp),
	FBNIC_TLV_PARSER(FW_START_UPGRADE_RESP,
			 fbnic_fw_start_upgrade_resp_index,
			 fbnic_fw_parse_fw_start_upgrade_resp),
	FBNIC_TLV_PARSER(FW_WRITE_CHUNK_REQ,
			 fbnic_fw_write_chunk_req_index,
			 fbnic_fw_parse_fw_write_chunk_req),
	FBNIC_TLV_PARSER(FW_FINISH_UPGRADE_REQ,
			 fbnic_fw_finish_upgrade_req_index,
			 fbnic_fw_parse_fw_finish_upgrade_req),
	FBNIC_TLV_PARSER(QSFP_READ_RESP,
			 fbnic_qsfp_read_resp_index,
			 fbnic_fw_parse_qsfp_read_resp),
	FBNIC_TLV_PARSER(TSENE_READ_RESP,
			 fbnic_tsene_read_resp_index,
			 fbnic_fw_parse_tsene_read_resp),
	FBNIC_TLV_PARSER(LOG_MSG_REQ,
			 fbnic_fw_log_req_index,
			 fbnic_fw_parse_log_req),
	FBNIC_TLV_MSG_ERROR
};

static void fbnic_mbx_process_rx_msgs(struct fbnic_dev *fbd)
{
	struct fbnic_fw_mbx *rx_mbx = &fbd->mbx[FBNIC_IPC_MBX_RX_IDX];
	u8 head = rx_mbx->head;
	u64 desc, length;

	while (head != rx_mbx->tail) {
		struct fbnic_tlv_msg *msg;
		int err;

		desc = __fbnic_mbx_rd_desc(fbd, FBNIC_IPC_MBX_RX_IDX, head);
		if (!(desc & FBNIC_IPC_MBX_DESC_FW_CMPL))
			break;

		dma_unmap_single(fbd->dev, rx_mbx->buf_info[head].addr,
				 PAGE_SIZE, DMA_FROM_DEVICE);

		msg = rx_mbx->buf_info[head].msg;

		length = FIELD_GET(FBNIC_IPC_MBX_DESC_LEN_MASK, desc);

		/* Ignore NULL mailbox descriptors */
		if (!length)
			goto next_page;

		/* Report descriptors with length greater than page size */
		if (length > PAGE_SIZE) {
			dev_warn(fbd->dev,
				 "Invalid mailbox descriptor length: %lld\n",
				 length);
			goto next_page;
		}

		if (le16_to_cpu(msg->hdr.len) * sizeof(u32) > length)
			dev_warn(fbd->dev, "Mailbox message length mismatch\n");

		/* If parsing fails dump contents of message to dmesg */
		err = fbnic_tlv_msg_parse(fbd, msg, fbnic_fw_tlv_parser);
		if (err) {
			dev_warn(fbd->dev, "Unable to process message: %d\n",
				 err);
			print_hex_dump(KERN_WARNING, "fbnic:",
				       DUMP_PREFIX_OFFSET, 16, 2,
				       msg, length, true);
		}

		dev_dbg(fbd->dev, "Parsed msg type %d\n", msg->hdr.type);
next_page:

		free_page((unsigned long)rx_mbx->buf_info[head].msg);
		rx_mbx->buf_info[head].msg = NULL;

		head++;
		head %= FBNIC_IPC_MBX_DESC_LEN;
	}

	/* Record head for next interrupt */
	rx_mbx->head = head;

	/* Make sure we have at least one page for the FW to write to */
	fbnic_mbx_alloc_rx_msgs(fbd);
}

void fbnic_mbx_poll(struct fbnic_dev *fbd)
{
	fbnic_mbx_event(fbd);

	fbnic_mbx_process_tx_msgs(fbd);
	fbnic_mbx_process_rx_msgs(fbd);
}

int fbnic_mbx_poll_tx_ready(struct fbnic_dev *fbd)
{
	struct fbnic_fw_mbx *tx_mbx = &fbd->mbx[FBNIC_IPC_MBX_TX_IDX];
	unsigned long timeout = jiffies + 10 * HZ + 1;
	int err, i;

	do {
		if (!time_is_after_jiffies(timeout))
			return -ETIMEDOUT;

		/* Force the firmware to trigger an interrupt response to
		 * avoid the mailbox getting stuck closed if the interrupt
		 * is reset.
		 */
		fbnic_mbx_reset_desc_ring(fbd, FBNIC_IPC_MBX_TX_IDX);

		/* Immediate fail if BAR4 went away */
		if (!fbnic_fw_present(fbd))
			return -ENODEV;

		msleep(20);
	} while (!fbnic_mbx_event(fbd));

	/* FW has shown signs of life. Enable DMA and start Tx/Rx */
	for (i = 0; i < FBNIC_IPC_MBX_INDICES; i++)
		fbnic_mbx_init_desc_ring(fbd, i);

	/* Request an update from the firmware. This should overwrite
	 * mgmt.version once we get the actual version from the firmware
	 * in the capabilities request message.
	 */
	err = fbnic_fw_xmit_simple_msg(fbd, FBNIC_TLV_MSG_ID_HOST_CAP_REQ);
	if (err)
		goto clean_mbx;

	/* Poll until we get a current management firmware version, use "1"
	 * to indicate we entered the polling state waiting for a response
	 */
	for (fbd->fw_cap.running.mgmt.version = 1;
	     fbd->fw_cap.running.mgmt.version < MIN_FW_VER_CODE;) {
		if (!tx_mbx->ready)
			err = -ENODEV;
		if (err)
			goto clean_mbx;

		msleep(20);
		fbnic_mbx_poll(fbd);

		/* set err, but wait till mgmt.version check to report it */
		if (!time_is_after_jiffies(timeout))
			err = -ETIMEDOUT;
	}

	return 0;
clean_mbx:
	/* Cleanup Rx buffers and disable mailbox */
	fbnic_mbx_clean(fbd);
	return err;
}

static void __fbnic_fw_evict_cmpl(struct fbnic_fw_completion *cmpl_data)
{
	cmpl_data->result = -EPIPE;
	complete(&cmpl_data->done);
}

static void fbnic_mbx_evict_all_cmpl(struct fbnic_dev *fbd)
{
	int i;

	for (i = 0; i < FBNIC_MBX_CMPL_SLOTS; i++) {
		struct fbnic_fw_completion *cmpl_data = fbd->cmpl_data[i];

		if (cmpl_data)
			__fbnic_fw_evict_cmpl(cmpl_data);
	}

	memset(fbd->cmpl_data, 0, sizeof(fbd->cmpl_data));
}

void fbnic_mbx_flush_tx(struct fbnic_dev *fbd)
{
	unsigned long timeout = jiffies + 10 * HZ + 1;
	struct fbnic_fw_mbx *tx_mbx;
	u8 tail;

	/* Record current Rx stats */
	tx_mbx = &fbd->mbx[FBNIC_IPC_MBX_TX_IDX];

	spin_lock_irq(&fbd->fw_tx_lock);

	/* Clear ready to prevent any further attempts to transmit */
	tx_mbx->ready = false;

	/* Read tail to determine the last tail state for the ring */
	tail = tx_mbx->tail;

	/* Flush any completions as we are no longer processing Rx */
	fbnic_mbx_evict_all_cmpl(fbd);

	spin_unlock_irq(&fbd->fw_tx_lock);

	/* Give firmware time to process packet,
	 * we will wait up to 10 seconds which is 500 waits of 20ms.
	 */
	do {
		u8 head = tx_mbx->head;

		/* Tx ring is empty once head == tail */
		if (head == tail)
			break;

		msleep(20);
		fbnic_mbx_process_tx_msgs(fbd);
	} while (time_is_after_jiffies(timeout));
}

int fbnic_fw_xmit_rpc_macda_sync(struct fbnic_dev *fbd)
{
	struct fbnic_tlv_msg *mac_array;
	int i, addr_count = 0, err;
	struct fbnic_tlv_msg *msg;
	u32 rx_flags = 0;

	/* Nothing to do if there is no FW to sync with */
	if (!fbd->mbx[FBNIC_IPC_MBX_TX_IDX].ready)
		return 0;

	msg = fbnic_tlv_msg_alloc(FBNIC_TLV_MSG_ID_RPC_MAC_SYNC_REQ);
	if (!msg)
		return -ENOMEM;

	mac_array = fbnic_tlv_attr_nest_start(msg,
					      FBNIC_FW_RPC_MAC_SYNC_UC_ARRAY);
	if (!mac_array)
		goto free_message_nospc;

	/* Populate the unicast MAC addrs and capture PROMISC/ALLMULTI flags */
	for (addr_count = 0, i = FBNIC_RPC_TCAM_MACDA_PROMISC_IDX;
	     i >= fbd->mac_addr_boundary; i--) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		if (mac_addr->state != FBNIC_TCAM_S_VALID)
			continue;
		if (test_bit(FBNIC_MAC_ADDR_T_ALLMULTI, mac_addr->act_tcam))
			rx_flags |= FW_RPC_MAC_SYNC_RX_FLAGS_ALLMULTI;
		if (test_bit(FBNIC_MAC_ADDR_T_PROMISC, mac_addr->act_tcam))
			rx_flags |= FW_RPC_MAC_SYNC_RX_FLAGS_PROMISC;
		if (!test_bit(FBNIC_MAC_ADDR_T_UNICAST, mac_addr->act_tcam))
			continue;
		if (addr_count == FW_RPC_MAC_SYNC_UC_ARRAY_SIZE) {
			rx_flags |= FW_RPC_MAC_SYNC_RX_FLAGS_PROMISC;
			continue;
		}

		err = fbnic_tlv_attr_put_value(mac_array,
					       FBNIC_FW_RPC_MAC_SYNC_MAC_ADDR,
					       mac_addr->value.addr8,
					       ETH_ALEN);
		if (err)
			goto free_message;
		addr_count++;
	}

	/* Close array */
	fbnic_tlv_attr_nest_stop(msg);

	mac_array = fbnic_tlv_attr_nest_start(msg,
					      FBNIC_FW_RPC_MAC_SYNC_MC_ARRAY);
	if (!mac_array)
		goto free_message_nospc;

	/* Repeat for multicast addrs, record BROADCAST/ALLMULTI flags */
	for (addr_count = 0, i = FBNIC_RPC_TCAM_MACDA_BROADCAST_IDX;
	     i < fbd->mac_addr_boundary; i++) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		if (mac_addr->state != FBNIC_TCAM_S_VALID)
			continue;
		if (test_bit(FBNIC_MAC_ADDR_T_BROADCAST, mac_addr->act_tcam))
			rx_flags |= FW_RPC_MAC_SYNC_RX_FLAGS_BROADCAST;
		if (test_bit(FBNIC_MAC_ADDR_T_ALLMULTI, mac_addr->act_tcam))
			rx_flags |= FW_RPC_MAC_SYNC_RX_FLAGS_ALLMULTI;
		if (!test_bit(FBNIC_MAC_ADDR_T_MULTICAST, mac_addr->act_tcam))
			continue;
		if (addr_count == FW_RPC_MAC_SYNC_MC_ARRAY_SIZE) {
			rx_flags |= FW_RPC_MAC_SYNC_RX_FLAGS_ALLMULTI;
			continue;
		}

		err = fbnic_tlv_attr_put_value(mac_array,
					       FBNIC_FW_RPC_MAC_SYNC_MAC_ADDR,
					       mac_addr->value.addr8,
					       ETH_ALEN);
		if (err)
			goto free_message;
		addr_count++;
	}

	/* Close array */
	fbnic_tlv_attr_nest_stop(msg);

	/* Report flags at end of list */
	err = fbnic_tlv_attr_put_int(msg, FBNIC_FW_RPC_MAC_SYNC_RX_FLAGS,
				     rx_flags);
	if (err)
		goto free_message;

	/* Send message of to FW notifying it of current RPC config */
	err = fbnic_mbx_map_tlv_msg(fbd, msg);
	if (err)
		goto free_message;
	return 0;
free_message_nospc:
	err = -ENOSPC;
free_message:
	free_page((unsigned long)msg);
	return err;
}

void fbnic_get_fw_ver_commit_str(struct fbnic_dev *fbd, char *fw_version,
				 const size_t str_sz)
{
	struct fbnic_fw_ver *mgmt = &fbd->fw_cap.running.mgmt;
	const char *delim = "";

	if (mgmt->commit[0])
		delim = "_";

	fbnic_mk_full_fw_ver_str(mgmt->version, delim, mgmt->commit,
				 fw_version, str_sz);
}

struct fbnic_fw_completion *__fbnic_fw_alloc_cmpl(u32 msg_type,
						  size_t priv_size)
{
	struct fbnic_fw_completion *cmpl;

	cmpl = kzalloc(sizeof(*cmpl) + priv_size, GFP_KERNEL);
	if (!cmpl)
		return NULL;

	cmpl->msg_type = msg_type;
	init_completion(&cmpl->done);
	kref_init(&cmpl->ref_count);

	return cmpl;
}

struct fbnic_fw_completion *fbnic_fw_alloc_cmpl(u32 msg_type)
{
	return __fbnic_fw_alloc_cmpl(msg_type, 0);
}

void fbnic_fw_put_cmpl(struct fbnic_fw_completion *fw_cmpl)
{
	kref_put(&fw_cmpl->ref_count, fbnic_fw_release_cmpl_data);
}
