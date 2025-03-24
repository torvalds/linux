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

	fw_wr32(fbd, desc_offset + 1, upper_32_bits(desc));
	fw_wrfl(fbd);
	fw_wr32(fbd, desc_offset, lower_32_bits(desc));
}

static u64 __fbnic_mbx_rd_desc(struct fbnic_dev *fbd, int mbx_idx, int desc_idx)
{
	u32 desc_offset = FBNIC_IPC_MBX(mbx_idx, desc_idx);
	u64 desc;

	desc = fw_rd32(fbd, desc_offset);
	desc |= (u64)fw_rd32(fbd, desc_offset + 1) << 32;

	return desc;
}

static void fbnic_mbx_init_desc_ring(struct fbnic_dev *fbd, int mbx_idx)
{
	int desc_idx;

	/* Initialize first descriptor to all 0s. Doing this gives us a
	 * solid stop for the firmware to hit when it is done looping
	 * through the ring.
	 */
	__fbnic_mbx_wr_desc(fbd, mbx_idx, 0, 0);

	fw_wrfl(fbd);

	/* We then fill the rest of the ring starting at the end and moving
	 * back toward descriptor 0 with skip descriptors that have no
	 * length nor address, and tell the firmware that they can skip
	 * them and just move past them to the one we initialized to 0.
	 */
	for (desc_idx = FBNIC_IPC_MBX_DESC_LEN; --desc_idx;) {
		__fbnic_mbx_wr_desc(fbd, mbx_idx, desc_idx,
				    FBNIC_IPC_MBX_DESC_FW_CMPL |
				    FBNIC_IPC_MBX_DESC_HOST_CMPL);
		fw_wrfl(fbd);
	}
}

void fbnic_mbx_init(struct fbnic_dev *fbd)
{
	int i;

	/* Initialize lock to protect Tx ring */
	spin_lock_init(&fbd->fw_tx_lock);

	/* Reinitialize mailbox memory */
	for (i = 0; i < FBNIC_IPC_MBX_INDICES; i++)
		memset(&fbd->mbx[i], 0, sizeof(struct fbnic_fw_mbx));

	/* Do not auto-clear the FW mailbox interrupt, let SW clear it */
	wr32(fbd, FBNIC_INTR_SW_AC_MODE(0), ~(1u << FBNIC_FW_MSIX_ENTRY));

	/* Clear any stale causes in vector 0 as that is used for doorbell */
	wr32(fbd, FBNIC_INTR_CLEAR(0), 1u << FBNIC_FW_MSIX_ENTRY);

	for (i = 0; i < FBNIC_IPC_MBX_INDICES; i++)
		fbnic_mbx_init_desc_ring(fbd, i);
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
	if (dma_mapping_error(fbd->dev, addr)) {
		free_page((unsigned long)msg);

		return -ENOSPC;
	}

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

	fbnic_mbx_init_desc_ring(fbd, mbx_idx);

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
	count = (head - tail - 1) % FBNIC_IPC_MBX_DESC_LEN;
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

static int fbnic_mbx_map_req_w_cmpl(struct fbnic_dev *fbd,
				    struct fbnic_tlv_msg *msg,
				    struct fbnic_fw_completion *cmpl_data)
{
	unsigned long flags;
	int err;

	spin_lock_irqsave(&fbd->fw_tx_lock, flags);

	/* If we are already waiting on a completion then abort */
	if (cmpl_data && fbd->cmpl_data) {
		err = -EBUSY;
		goto unlock_mbx;
	}

	/* Record completion location and submit request */
	if (cmpl_data)
		fbd->cmpl_data = cmpl_data;

	err = fbnic_mbx_map_msg(fbd, FBNIC_IPC_MBX_TX_IDX, msg,
				le16_to_cpu(msg->hdr.len) * sizeof(u32), 1);

	/* If msg failed then clear completion data for next caller */
	if (err && cmpl_data)
		fbd->cmpl_data = NULL;

unlock_mbx:
	spin_unlock_irqrestore(&fbd->fw_tx_lock, flags);

	return err;
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

	spin_lock_irqsave(&fbd->fw_tx_lock, flags);
	if (fbd->cmpl_data && fbd->cmpl_data->msg_type == msg_type) {
		cmpl_data = fbd->cmpl_data;
		kref_get(&fbd->cmpl_data->ref_count);
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
 *     -EOPNOTSUPP: Is not ASIC so mailbox is not supported
 *     -ENODEV: Device I/O error
 *     -ENOMEM: Failed to allocate message
 *     -EBUSY: No space in mailbox
 *     -ENOSPC: DMA mapping failed
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

/**
 * fbnic_fw_xmit_cap_msg - Allocate and populate a FW capabilities message
 * @fbd: FBNIC device structure
 *
 * Return: NULL on failure to allocate, error pointer on error, or pointer
 * to new TLV test message.
 *
 * Sends a single TLV header indicating the host wants the firmware to
 * confirm the capabilities and version.
 **/
static int fbnic_fw_xmit_cap_msg(struct fbnic_dev *fbd)
{
	int err = fbnic_fw_xmit_simple_msg(fbd, FBNIC_TLV_MSG_ID_HOST_CAP_REQ);

	/* Return 0 if we are not calling this on ASIC */
	return (err == -EOPNOTSUPP) ? 0 : err;
}

static void fbnic_mbx_postinit_desc_ring(struct fbnic_dev *fbd, int mbx_idx)
{
	struct fbnic_fw_mbx *mbx = &fbd->mbx[mbx_idx];

	/* This is a one time init, so just exit if it is completed */
	if (mbx->ready)
		return;

	mbx->ready = true;

	switch (mbx_idx) {
	case FBNIC_IPC_MBX_RX_IDX:
		/* Make sure we have a page for the FW to write to */
		fbnic_mbx_alloc_rx_msgs(fbd);
		break;
	case FBNIC_IPC_MBX_TX_IDX:
		/* Force version to 1 if we successfully requested an update
		 * from the firmware. This should be overwritten once we get
		 * the actual version from the firmware in the capabilities
		 * request message.
		 */
		if (!fbnic_fw_xmit_cap_msg(fbd) &&
		    !fbd->fw_cap.running.mgmt.version)
			fbd->fw_cap.running.mgmt.version = 1;
		break;
	}
}

static void fbnic_mbx_postinit(struct fbnic_dev *fbd)
{
	int i;

	/* We only need to do this on the first interrupt following init.
	 * this primes the mailbox so that we will have cleared all the
	 * skip descriptors.
	 */
	if (!(rd32(fbd, FBNIC_INTR_STATUS(0)) & (1u << FBNIC_FW_MSIX_ENTRY)))
		return;

	wr32(fbd, FBNIC_INTR_CLEAR(0), 1u << FBNIC_FW_MSIX_ENTRY);

	for (i = 0; i < FBNIC_IPC_MBX_INDICES; i++)
		fbnic_mbx_postinit_desc_ring(fbd, i);
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
	u32 active_slot = 0, all_multi = 0;
	struct fbnic_dev *fbd = opaque;
	u32 speed = 0, fec = 0;
	size_t commit_size = 0;
	bool bmc_present;
	int err;

	get_unsigned_result(FBNIC_FW_CAP_RESP_VERSION,
			    fbd->fw_cap.running.mgmt.version);

	if (!fbd->fw_cap.running.mgmt.version)
		return -EINVAL;

	if (fbd->fw_cap.running.mgmt.version < MIN_FW_VERSION_CODE) {
		char running_ver[FBNIC_FW_VER_MAX_SIZE];

		fbnic_mk_fw_ver_str(fbd->fw_cap.running.mgmt.version,
				    running_ver);
		dev_err(fbd->dev, "Device firmware version(%s) is older than minimum required version(%02d.%02d.%02d)\n",
			running_ver,
			MIN_FW_MAJOR_VERSION,
			MIN_FW_MINOR_VERSION,
			MIN_FW_BUILD_VERSION);
		/* Disable TX mailbox to prevent card use until firmware is
		 * updated.
		 */
		fbd->mbx[FBNIC_IPC_MBX_TX_IDX].ready = false;
		return -EINVAL;
	}

	get_string_result(FBNIC_FW_CAP_RESP_VERSION_COMMIT_STR, commit_size,
			  fbd->fw_cap.running.mgmt.commit,
			  FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE);
	if (!commit_size)
		dev_warn(fbd->dev, "Firmware did not send mgmt commit!\n");

	get_unsigned_result(FBNIC_FW_CAP_RESP_STORED_VERSION,
			    fbd->fw_cap.stored.mgmt.version);
	get_string_result(FBNIC_FW_CAP_RESP_STORED_COMMIT_STR, commit_size,
			  fbd->fw_cap.stored.mgmt.commit,
			  FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE);

	get_unsigned_result(FBNIC_FW_CAP_RESP_CMRT_VERSION,
			    fbd->fw_cap.running.bootloader.version);
	get_string_result(FBNIC_FW_CAP_RESP_CMRT_COMMIT_STR, commit_size,
			  fbd->fw_cap.running.bootloader.commit,
			  FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE);

	get_unsigned_result(FBNIC_FW_CAP_RESP_STORED_CMRT_VERSION,
			    fbd->fw_cap.stored.bootloader.version);
	get_string_result(FBNIC_FW_CAP_RESP_STORED_CMRT_COMMIT_STR, commit_size,
			  fbd->fw_cap.stored.bootloader.commit,
			  FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE);

	get_unsigned_result(FBNIC_FW_CAP_RESP_UEFI_VERSION,
			    fbd->fw_cap.stored.undi.version);
	get_string_result(FBNIC_FW_CAP_RESP_UEFI_COMMIT_STR, commit_size,
			  fbd->fw_cap.stored.undi.commit,
			  FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE);

	get_unsigned_result(FBNIC_FW_CAP_RESP_ACTIVE_FW_SLOT, active_slot);
	fbd->fw_cap.active_slot = active_slot;

	get_unsigned_result(FBNIC_FW_CAP_RESP_FW_LINK_SPEED, speed);
	get_unsigned_result(FBNIC_FW_CAP_RESP_FW_LINK_FEC, fec);
	fbd->fw_cap.link_speed = speed;
	fbd->fw_cap.link_fec = fec;

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

		get_unsigned_result(FBNIC_FW_CAP_RESP_BMC_ALL_MULTI, all_multi);
	} else {
		memset(fbd->fw_cap.bmc_mac_addr, 0,
		       sizeof(fbd->fw_cap.bmc_mac_addr));
	}

	fbd->fw_cap.bmc_present = bmc_present;

	if (results[FBNIC_FW_CAP_RESP_BMC_ALL_MULTI] || !bmc_present)
		fbd->fw_cap.all_multi = all_multi;

	return 0;
}

static const struct fbnic_tlv_index fbnic_ownership_resp_index[] = {
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_parse_ownership_resp(void *opaque,
					 struct fbnic_tlv_msg **results)
{
	struct fbnic_dev *fbd = (struct fbnic_dev *)opaque;

	/* Count the ownership response as a heartbeat reply */
	fbd->last_heartbeat_response = jiffies;

	return 0;
}

static const struct fbnic_tlv_index fbnic_heartbeat_resp_index[] = {
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_parse_heartbeat_resp(void *opaque,
					 struct fbnic_tlv_msg **results)
{
	struct fbnic_dev *fbd = (struct fbnic_dev *)opaque;

	fbd->last_heartbeat_response = jiffies;

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
	if (!fbnic_fw_heartbeat_current(fbd)) {
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
	FBNIC_TLV_ATTR_S32(FBNIC_TSENE_THERM),
	FBNIC_TLV_ATTR_S32(FBNIC_TSENE_VOLT),
	FBNIC_TLV_ATTR_S32(FBNIC_TSENE_ERROR),
	FBNIC_TLV_ATTR_LAST
};

static int fbnic_fw_parse_tsene_read_resp(void *opaque,
					  struct fbnic_tlv_msg **results)
{
	struct fbnic_fw_completion *cmpl_data;
	struct fbnic_dev *fbd = opaque;
	int err = 0;

	/* Verify we have a completion pointer to provide with data */
	cmpl_data = fbnic_fw_get_cmpl_by_type(fbd,
					      FBNIC_TLV_MSG_ID_TSENE_READ_RESP);
	if (!cmpl_data)
		return -EINVAL;

	if (results[FBNIC_TSENE_ERROR]) {
		err = fbnic_tlv_attr_get_unsigned(results[FBNIC_TSENE_ERROR]);
		if (err)
			goto exit_complete;
	}

	if (!results[FBNIC_TSENE_THERM] || !results[FBNIC_TSENE_VOLT]) {
		err = -EINVAL;
		goto exit_complete;
	}

	cmpl_data->u.tsene.millidegrees =
		fbnic_tlv_attr_get_signed(results[FBNIC_TSENE_THERM]);
	cmpl_data->u.tsene.millivolts =
		fbnic_tlv_attr_get_signed(results[FBNIC_TSENE_VOLT]);

exit_complete:
	cmpl_data->result = err;
	complete(&cmpl_data->done);
	fbnic_fw_put_cmpl(cmpl_data);

	return err;
}

static const struct fbnic_tlv_parser fbnic_fw_tlv_parser[] = {
	FBNIC_TLV_PARSER(FW_CAP_RESP, fbnic_fw_cap_resp_index,
			 fbnic_fw_parse_cap_resp),
	FBNIC_TLV_PARSER(OWNERSHIP_RESP, fbnic_ownership_resp_index,
			 fbnic_fw_parse_ownership_resp),
	FBNIC_TLV_PARSER(HEARTBEAT_RESP, fbnic_heartbeat_resp_index,
			 fbnic_fw_parse_heartbeat_resp),
	FBNIC_TLV_PARSER(TSENE_READ_RESP,
			 fbnic_tsene_read_resp_index,
			 fbnic_fw_parse_tsene_read_resp),
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
	fbnic_mbx_postinit(fbd);

	fbnic_mbx_process_tx_msgs(fbd);
	fbnic_mbx_process_rx_msgs(fbd);
}

int fbnic_mbx_poll_tx_ready(struct fbnic_dev *fbd)
{
	struct fbnic_fw_mbx *tx_mbx;
	int attempts = 50;

	/* Immediate fail if BAR4 isn't there */
	if (!fbnic_fw_present(fbd))
		return -ENODEV;

	tx_mbx = &fbd->mbx[FBNIC_IPC_MBX_TX_IDX];
	while (!tx_mbx->ready && --attempts) {
		/* Force the firmware to trigger an interrupt response to
		 * avoid the mailbox getting stuck closed if the interrupt
		 * is reset.
		 */
		fbnic_mbx_init_desc_ring(fbd, FBNIC_IPC_MBX_TX_IDX);

		msleep(200);

		fbnic_mbx_poll(fbd);
	}

	return attempts ? 0 : -ETIMEDOUT;
}

void fbnic_mbx_flush_tx(struct fbnic_dev *fbd)
{
	struct fbnic_fw_mbx *tx_mbx;
	int attempts = 50;
	u8 count = 0;

	/* Nothing to do if there is no mailbox */
	if (!fbnic_fw_present(fbd))
		return;

	/* Record current Rx stats */
	tx_mbx = &fbd->mbx[FBNIC_IPC_MBX_TX_IDX];

	/* Nothing to do if mailbox never got to ready */
	if (!tx_mbx->ready)
		return;

	/* Give firmware time to process packet,
	 * we will wait up to 10 seconds which is 50 waits of 200ms.
	 */
	do {
		u8 head = tx_mbx->head;

		if (head == tx_mbx->tail)
			break;

		msleep(200);
		fbnic_mbx_process_tx_msgs(fbd);

		count += (tx_mbx->head - head) % FBNIC_IPC_MBX_DESC_LEN;
	} while (count < FBNIC_IPC_MBX_DESC_LEN && --attempts);
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

void fbnic_fw_init_cmpl(struct fbnic_fw_completion *fw_cmpl,
			u32 msg_type)
{
	fw_cmpl->msg_type = msg_type;
	init_completion(&fw_cmpl->done);
	kref_init(&fw_cmpl->ref_count);
}

void fbnic_fw_clear_compl(struct fbnic_dev *fbd)
{
	unsigned long flags;

	spin_lock_irqsave(&fbd->fw_tx_lock, flags);
	fbd->cmpl_data = NULL;
	spin_unlock_irqrestore(&fbd->fw_tx_lock, flags);
}

void fbnic_fw_put_cmpl(struct fbnic_fw_completion *fw_cmpl)
{
	kref_put(&fw_cmpl->ref_count, fbnic_fw_release_cmpl_data);
}
