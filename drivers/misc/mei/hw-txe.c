/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 */

#include <linux/pci.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/irqreturn.h>
#include <linux/pm_runtime.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hw-txe.h"
#include "client.h"
#include "hbm.h"

#include "mei-trace.h"


/**
 * mei_txe_reg_read - Reads 32bit data from the txe device
 *
 * @base_addr: registers base address
 * @offset: register offset
 *
 * Return: register value
 */
static inline u32 mei_txe_reg_read(void __iomem *base_addr,
					unsigned long offset)
{
	return ioread32(base_addr + offset);
}

/**
 * mei_txe_reg_write - Writes 32bit data to the txe device
 *
 * @base_addr: registers base address
 * @offset: register offset
 * @value: the value to write
 */
static inline void mei_txe_reg_write(void __iomem *base_addr,
				unsigned long offset, u32 value)
{
	iowrite32(value, base_addr + offset);
}

/**
 * mei_txe_sec_reg_read_silent - Reads 32bit data from the SeC BAR
 *
 * @hw: the txe hardware structure
 * @offset: register offset
 *
 * Doesn't check for aliveness while Reads 32bit data from the SeC BAR
 *
 * Return: register value
 */
static inline u32 mei_txe_sec_reg_read_silent(struct mei_txe_hw *hw,
				unsigned long offset)
{
	return mei_txe_reg_read(hw->mem_addr[SEC_BAR], offset);
}

/**
 * mei_txe_sec_reg_read - Reads 32bit data from the SeC BAR
 *
 * @hw: the txe hardware structure
 * @offset: register offset
 *
 * Reads 32bit data from the SeC BAR and shout loud if aliveness is not set
 *
 * Return: register value
 */
static inline u32 mei_txe_sec_reg_read(struct mei_txe_hw *hw,
				unsigned long offset)
{
	WARN(!hw->aliveness, "sec read: aliveness not asserted\n");
	return mei_txe_sec_reg_read_silent(hw, offset);
}
/**
 * mei_txe_sec_reg_write_silent - Writes 32bit data to the SeC BAR
 *   doesn't check for aliveness
 *
 * @hw: the txe hardware structure
 * @offset: register offset
 * @value: value to write
 *
 * Doesn't check for aliveness while writes 32bit data from to the SeC BAR
 */
static inline void mei_txe_sec_reg_write_silent(struct mei_txe_hw *hw,
				unsigned long offset, u32 value)
{
	mei_txe_reg_write(hw->mem_addr[SEC_BAR], offset, value);
}

/**
 * mei_txe_sec_reg_write - Writes 32bit data to the SeC BAR
 *
 * @hw: the txe hardware structure
 * @offset: register offset
 * @value: value to write
 *
 * Writes 32bit data from the SeC BAR and shout loud if aliveness is not set
 */
static inline void mei_txe_sec_reg_write(struct mei_txe_hw *hw,
				unsigned long offset, u32 value)
{
	WARN(!hw->aliveness, "sec write: aliveness not asserted\n");
	mei_txe_sec_reg_write_silent(hw, offset, value);
}
/**
 * mei_txe_br_reg_read - Reads 32bit data from the Bridge BAR
 *
 * @hw: the txe hardware structure
 * @offset: offset from which to read the data
 *
 * Return: the byte read.
 */
static inline u32 mei_txe_br_reg_read(struct mei_txe_hw *hw,
				unsigned long offset)
{
	return mei_txe_reg_read(hw->mem_addr[BRIDGE_BAR], offset);
}

/**
 * mei_txe_br_reg_write - Writes 32bit data to the Bridge BAR
 *
 * @hw: the txe hardware structure
 * @offset: offset from which to write the data
 * @value: the byte to write
 */
static inline void mei_txe_br_reg_write(struct mei_txe_hw *hw,
				unsigned long offset, u32 value)
{
	mei_txe_reg_write(hw->mem_addr[BRIDGE_BAR], offset, value);
}

/**
 * mei_txe_aliveness_set - request for aliveness change
 *
 * @dev: the device structure
 * @req: requested aliveness value
 *
 * Request for aliveness change and returns true if the change is
 *   really needed and false if aliveness is already
 *   in the requested state
 *
 * Locking: called under "dev->device_lock" lock
 *
 * Return: true if request was send
 */
static bool mei_txe_aliveness_set(struct mei_device *dev, u32 req)
{

	struct mei_txe_hw *hw = to_txe_hw(dev);
	bool do_req = hw->aliveness != req;

	dev_dbg(dev->dev, "Aliveness current=%d request=%d\n",
				hw->aliveness, req);
	if (do_req) {
		dev->pg_event = MEI_PG_EVENT_WAIT;
		mei_txe_br_reg_write(hw, SICR_HOST_ALIVENESS_REQ_REG, req);
	}
	return do_req;
}


/**
 * mei_txe_aliveness_req_get - get aliveness requested register value
 *
 * @dev: the device structure
 *
 * Extract HICR_HOST_ALIVENESS_RESP_ACK bit from
 * from HICR_HOST_ALIVENESS_REQ register value
 *
 * Return: SICR_HOST_ALIVENESS_REQ_REQUESTED bit value
 */
static u32 mei_txe_aliveness_req_get(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);
	u32 reg;

	reg = mei_txe_br_reg_read(hw, SICR_HOST_ALIVENESS_REQ_REG);
	return reg & SICR_HOST_ALIVENESS_REQ_REQUESTED;
}

/**
 * mei_txe_aliveness_get - get aliveness response register value
 *
 * @dev: the device structure
 *
 * Return: HICR_HOST_ALIVENESS_RESP_ACK bit from HICR_HOST_ALIVENESS_RESP
 *         register
 */
static u32 mei_txe_aliveness_get(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);
	u32 reg;

	reg = mei_txe_br_reg_read(hw, HICR_HOST_ALIVENESS_RESP_REG);
	return reg & HICR_HOST_ALIVENESS_RESP_ACK;
}

/**
 * mei_txe_aliveness_poll - waits for aliveness to settle
 *
 * @dev: the device structure
 * @expected: expected aliveness value
 *
 * Polls for HICR_HOST_ALIVENESS_RESP.ALIVENESS_RESP to be set
 *
 * Return: 0 if the expected value was received, -ETIME otherwise
 */
static int mei_txe_aliveness_poll(struct mei_device *dev, u32 expected)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);
	ktime_t stop, start;

	start = ktime_get();
	stop = ktime_add(start, ms_to_ktime(SEC_ALIVENESS_WAIT_TIMEOUT));
	do {
		hw->aliveness = mei_txe_aliveness_get(dev);
		if (hw->aliveness == expected) {
			dev->pg_event = MEI_PG_EVENT_IDLE;
			dev_dbg(dev->dev, "aliveness settled after %lld usecs\n",
				ktime_to_us(ktime_sub(ktime_get(), start)));
			return 0;
		}
		usleep_range(20, 50);
	} while (ktime_before(ktime_get(), stop));

	dev->pg_event = MEI_PG_EVENT_IDLE;
	dev_err(dev->dev, "aliveness timed out\n");
	return -ETIME;
}

/**
 * mei_txe_aliveness_wait - waits for aliveness to settle
 *
 * @dev: the device structure
 * @expected: expected aliveness value
 *
 * Waits for HICR_HOST_ALIVENESS_RESP.ALIVENESS_RESP to be set
 *
 * Return: 0 on success and < 0 otherwise
 */
static int mei_txe_aliveness_wait(struct mei_device *dev, u32 expected)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);
	const unsigned long timeout =
			msecs_to_jiffies(SEC_ALIVENESS_WAIT_TIMEOUT);
	long err;
	int ret;

	hw->aliveness = mei_txe_aliveness_get(dev);
	if (hw->aliveness == expected)
		return 0;

	mutex_unlock(&dev->device_lock);
	err = wait_event_timeout(hw->wait_aliveness_resp,
			dev->pg_event == MEI_PG_EVENT_RECEIVED, timeout);
	mutex_lock(&dev->device_lock);

	hw->aliveness = mei_txe_aliveness_get(dev);
	ret = hw->aliveness == expected ? 0 : -ETIME;

	if (ret)
		dev_warn(dev->dev, "aliveness timed out = %ld aliveness = %d event = %d\n",
			err, hw->aliveness, dev->pg_event);
	else
		dev_dbg(dev->dev, "aliveness settled after = %d msec aliveness = %d event = %d\n",
			jiffies_to_msecs(timeout - err),
			hw->aliveness, dev->pg_event);

	dev->pg_event = MEI_PG_EVENT_IDLE;
	return ret;
}

/**
 * mei_txe_aliveness_set_sync - sets an wait for aliveness to complete
 *
 * @dev: the device structure
 * @req: requested aliveness value
 *
 * Return: 0 on success and < 0 otherwise
 */
int mei_txe_aliveness_set_sync(struct mei_device *dev, u32 req)
{
	if (mei_txe_aliveness_set(dev, req))
		return mei_txe_aliveness_wait(dev, req);
	return 0;
}

/**
 * mei_txe_pg_in_transition - is device now in pg transition
 *
 * @dev: the device structure
 *
 * Return: true if in pg transition, false otherwise
 */
static bool mei_txe_pg_in_transition(struct mei_device *dev)
{
	return dev->pg_event == MEI_PG_EVENT_WAIT;
}

/**
 * mei_txe_pg_is_enabled - detect if PG is supported by HW
 *
 * @dev: the device structure
 *
 * Return: true is pg supported, false otherwise
 */
static bool mei_txe_pg_is_enabled(struct mei_device *dev)
{
	return true;
}

/**
 * mei_txe_pg_state  - translate aliveness register value
 *   to the mei power gating state
 *
 * @dev: the device structure
 *
 * Return: MEI_PG_OFF if aliveness is on and MEI_PG_ON otherwise
 */
static inline enum mei_pg_state mei_txe_pg_state(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);

	return hw->aliveness ? MEI_PG_OFF : MEI_PG_ON;
}

/**
 * mei_txe_input_ready_interrupt_enable - sets the Input Ready Interrupt
 *
 * @dev: the device structure
 */
static void mei_txe_input_ready_interrupt_enable(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);
	u32 hintmsk;
	/* Enable the SEC_IPC_HOST_INT_MASK_IN_RDY interrupt */
	hintmsk = mei_txe_sec_reg_read(hw, SEC_IPC_HOST_INT_MASK_REG);
	hintmsk |= SEC_IPC_HOST_INT_MASK_IN_RDY;
	mei_txe_sec_reg_write(hw, SEC_IPC_HOST_INT_MASK_REG, hintmsk);
}

/**
 * mei_txe_input_doorbell_set - sets bit 0 in
 *    SEC_IPC_INPUT_DOORBELL.IPC_INPUT_DOORBELL.
 *
 * @hw: the txe hardware structure
 */
static void mei_txe_input_doorbell_set(struct mei_txe_hw *hw)
{
	/* Clear the interrupt cause */
	clear_bit(TXE_INTR_IN_READY_BIT, &hw->intr_cause);
	mei_txe_sec_reg_write(hw, SEC_IPC_INPUT_DOORBELL_REG, 1);
}

/**
 * mei_txe_output_ready_set - Sets the SICR_SEC_IPC_OUTPUT_STATUS bit to 1
 *
 * @hw: the txe hardware structure
 */
static void mei_txe_output_ready_set(struct mei_txe_hw *hw)
{
	mei_txe_br_reg_write(hw,
			SICR_SEC_IPC_OUTPUT_STATUS_REG,
			SEC_IPC_OUTPUT_STATUS_RDY);
}

/**
 * mei_txe_is_input_ready - check if TXE is ready for receiving data
 *
 * @dev: the device structure
 *
 * Return: true if INPUT STATUS READY bit is set
 */
static bool mei_txe_is_input_ready(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);
	u32 status;

	status = mei_txe_sec_reg_read(hw, SEC_IPC_INPUT_STATUS_REG);
	return !!(SEC_IPC_INPUT_STATUS_RDY & status);
}

/**
 * mei_txe_intr_clear - clear all interrupts
 *
 * @dev: the device structure
 */
static inline void mei_txe_intr_clear(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);

	mei_txe_sec_reg_write_silent(hw, SEC_IPC_HOST_INT_STATUS_REG,
		SEC_IPC_HOST_INT_STATUS_PENDING);
	mei_txe_br_reg_write(hw, HISR_REG, HISR_INT_STS_MSK);
	mei_txe_br_reg_write(hw, HHISR_REG, IPC_HHIER_MSK);
}

/**
 * mei_txe_intr_disable - disable all interrupts
 *
 * @dev: the device structure
 */
static void mei_txe_intr_disable(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);

	mei_txe_br_reg_write(hw, HHIER_REG, 0);
	mei_txe_br_reg_write(hw, HIER_REG, 0);
}
/**
 * mei_txe_intr_enable - enable all interrupts
 *
 * @dev: the device structure
 */
static void mei_txe_intr_enable(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);

	mei_txe_br_reg_write(hw, HHIER_REG, IPC_HHIER_MSK);
	mei_txe_br_reg_write(hw, HIER_REG, HIER_INT_EN_MSK);
}

/**
 * mei_txe_pending_interrupts - check if there are pending interrupts
 *	only Aliveness, Input ready, and output doorbell are of relevance
 *
 * @dev: the device structure
 *
 * Checks if there are pending interrupts
 * only Aliveness, Readiness, Input ready, and Output doorbell are relevant
 *
 * Return: true if there are pending interrupts
 */
static bool mei_txe_pending_interrupts(struct mei_device *dev)
{

	struct mei_txe_hw *hw = to_txe_hw(dev);
	bool ret = (hw->intr_cause & (TXE_INTR_READINESS |
				      TXE_INTR_ALIVENESS |
				      TXE_INTR_IN_READY  |
				      TXE_INTR_OUT_DB));

	if (ret) {
		dev_dbg(dev->dev,
			"Pending Interrupts InReady=%01d Readiness=%01d, Aliveness=%01d, OutDoor=%01d\n",
			!!(hw->intr_cause & TXE_INTR_IN_READY),
			!!(hw->intr_cause & TXE_INTR_READINESS),
			!!(hw->intr_cause & TXE_INTR_ALIVENESS),
			!!(hw->intr_cause & TXE_INTR_OUT_DB));
	}
	return ret;
}

/**
 * mei_txe_input_payload_write - write a dword to the host buffer
 *	at offset idx
 *
 * @dev: the device structure
 * @idx: index in the host buffer
 * @value: value
 */
static void mei_txe_input_payload_write(struct mei_device *dev,
			unsigned long idx, u32 value)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);

	mei_txe_sec_reg_write(hw, SEC_IPC_INPUT_PAYLOAD_REG +
			(idx * sizeof(u32)), value);
}

/**
 * mei_txe_out_data_read - read dword from the device buffer
 *	at offset idx
 *
 * @dev: the device structure
 * @idx: index in the device buffer
 *
 * Return: register value at index
 */
static u32 mei_txe_out_data_read(const struct mei_device *dev,
					unsigned long idx)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);

	return mei_txe_br_reg_read(hw,
		BRIDGE_IPC_OUTPUT_PAYLOAD_REG + (idx * sizeof(u32)));
}

/* Readiness */

/**
 * mei_txe_readiness_set_host_rdy - set host readiness bit
 *
 * @dev: the device structure
 */
static void mei_txe_readiness_set_host_rdy(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);

	mei_txe_br_reg_write(hw,
		SICR_HOST_IPC_READINESS_REQ_REG,
		SICR_HOST_IPC_READINESS_HOST_RDY);
}

/**
 * mei_txe_readiness_clear - clear host readiness bit
 *
 * @dev: the device structure
 */
static void mei_txe_readiness_clear(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);

	mei_txe_br_reg_write(hw, SICR_HOST_IPC_READINESS_REQ_REG,
				SICR_HOST_IPC_READINESS_RDY_CLR);
}
/**
 * mei_txe_readiness_get - Reads and returns
 *	the HICR_SEC_IPC_READINESS register value
 *
 * @dev: the device structure
 *
 * Return: the HICR_SEC_IPC_READINESS register value
 */
static u32 mei_txe_readiness_get(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);

	return mei_txe_br_reg_read(hw, HICR_SEC_IPC_READINESS_REG);
}


/**
 * mei_txe_readiness_is_sec_rdy - check readiness
 *  for HICR_SEC_IPC_READINESS_SEC_RDY
 *
 * @readiness: cached readiness state
 *
 * Return: true if readiness bit is set
 */
static inline bool mei_txe_readiness_is_sec_rdy(u32 readiness)
{
	return !!(readiness & HICR_SEC_IPC_READINESS_SEC_RDY);
}

/**
 * mei_txe_hw_is_ready - check if the hw is ready
 *
 * @dev: the device structure
 *
 * Return: true if sec is ready
 */
static bool mei_txe_hw_is_ready(struct mei_device *dev)
{
	u32 readiness =  mei_txe_readiness_get(dev);

	return mei_txe_readiness_is_sec_rdy(readiness);
}

/**
 * mei_txe_host_is_ready - check if the host is ready
 *
 * @dev: the device structure
 *
 * Return: true if host is ready
 */
static inline bool mei_txe_host_is_ready(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);
	u32 reg = mei_txe_br_reg_read(hw, HICR_SEC_IPC_READINESS_REG);

	return !!(reg & HICR_SEC_IPC_READINESS_HOST_RDY);
}

/**
 * mei_txe_readiness_wait - wait till readiness settles
 *
 * @dev: the device structure
 *
 * Return: 0 on success and -ETIME on timeout
 */
static int mei_txe_readiness_wait(struct mei_device *dev)
{
	if (mei_txe_hw_is_ready(dev))
		return 0;

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(dev->wait_hw_ready, dev->recvd_hw_ready,
			msecs_to_jiffies(SEC_RESET_WAIT_TIMEOUT));
	mutex_lock(&dev->device_lock);
	if (!dev->recvd_hw_ready) {
		dev_err(dev->dev, "wait for readiness failed\n");
		return -ETIME;
	}

	dev->recvd_hw_ready = false;
	return 0;
}

static const struct mei_fw_status mei_txe_fw_sts = {
	.count = 2,
	.status[0] = PCI_CFG_TXE_FW_STS0,
	.status[1] = PCI_CFG_TXE_FW_STS1
};

/**
 * mei_txe_fw_status - read fw status register from pci config space
 *
 * @dev: mei device
 * @fw_status: fw status register values
 *
 * Return: 0 on success, error otherwise
 */
static int mei_txe_fw_status(struct mei_device *dev,
			     struct mei_fw_status *fw_status)
{
	const struct mei_fw_status *fw_src = &mei_txe_fw_sts;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int ret;
	int i;

	if (!fw_status)
		return -EINVAL;

	fw_status->count = fw_src->count;
	for (i = 0; i < fw_src->count && i < MEI_FW_STATUS_MAX; i++) {
		ret = pci_read_config_dword(pdev, fw_src->status[i],
					    &fw_status->status[i]);
		trace_mei_pci_cfg_read(dev->dev, "PCI_CFG_HSF_X",
				       fw_src->status[i],
				       fw_status->status[i]);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 *  mei_txe_hw_config - configure hardware at the start of the devices
 *
 * @dev: the device structure
 *
 * Configure hardware at the start of the device should be done only
 *   once at the device probe time
 */
static void mei_txe_hw_config(struct mei_device *dev)
{

	struct mei_txe_hw *hw = to_txe_hw(dev);

	/* Doesn't change in runtime */
	dev->hbuf_depth = PAYLOAD_SIZE / 4;

	hw->aliveness = mei_txe_aliveness_get(dev);
	hw->readiness = mei_txe_readiness_get(dev);

	dev_dbg(dev->dev, "aliveness_resp = 0x%08x, readiness = 0x%08x.\n",
		hw->aliveness, hw->readiness);
}


/**
 * mei_txe_write - writes a message to device.
 *
 * @dev: the device structure
 * @header: header of message
 * @buf: message buffer will be written
 *
 * Return: 0 if success, <0 - otherwise.
 */

static int mei_txe_write(struct mei_device *dev,
		struct mei_msg_hdr *header, unsigned char *buf)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);
	unsigned long rem;
	unsigned long length;
	int slots = dev->hbuf_depth;
	u32 *reg_buf = (u32 *)buf;
	u32 dw_cnt;
	int i;

	if (WARN_ON(!header || !buf))
		return -EINVAL;

	length = header->length;

	dev_dbg(dev->dev, MEI_HDR_FMT, MEI_HDR_PRM(header));

	dw_cnt = mei_data2slots(length);
	if (dw_cnt > slots)
		return -EMSGSIZE;

	if (WARN(!hw->aliveness, "txe write: aliveness not asserted\n"))
		return -EAGAIN;

	/* Enable Input Ready Interrupt. */
	mei_txe_input_ready_interrupt_enable(dev);

	if (!mei_txe_is_input_ready(dev)) {
		char fw_sts_str[MEI_FW_STATUS_STR_SZ];

		mei_fw_status_str(dev, fw_sts_str, MEI_FW_STATUS_STR_SZ);
		dev_err(dev->dev, "Input is not ready %s\n", fw_sts_str);
		return -EAGAIN;
	}

	mei_txe_input_payload_write(dev, 0, *((u32 *)header));

	for (i = 0; i < length / 4; i++)
		mei_txe_input_payload_write(dev, i + 1, reg_buf[i]);

	rem = length & 0x3;
	if (rem > 0) {
		u32 reg = 0;

		memcpy(&reg, &buf[length - rem], rem);
		mei_txe_input_payload_write(dev, i + 1, reg);
	}

	/* after each write the whole buffer is consumed */
	hw->slots = 0;

	/* Set Input-Doorbell */
	mei_txe_input_doorbell_set(hw);

	return 0;
}

/**
 * mei_txe_hbuf_max_len - mimics the me hbuf circular buffer
 *
 * @dev: the device structure
 *
 * Return: the PAYLOAD_SIZE - 4
 */
static size_t mei_txe_hbuf_max_len(const struct mei_device *dev)
{
	return PAYLOAD_SIZE - sizeof(struct mei_msg_hdr);
}

/**
 * mei_txe_hbuf_empty_slots - mimics the me hbuf circular buffer
 *
 * @dev: the device structure
 *
 * Return: always hbuf_depth
 */
static int mei_txe_hbuf_empty_slots(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);

	return hw->slots;
}

/**
 * mei_txe_count_full_read_slots - mimics the me device circular buffer
 *
 * @dev: the device structure
 *
 * Return: always buffer size in dwords count
 */
static int mei_txe_count_full_read_slots(struct mei_device *dev)
{
	/* read buffers has static size */
	return  PAYLOAD_SIZE / 4;
}

/**
 * mei_txe_read_hdr - read message header which is always in 4 first bytes
 *
 * @dev: the device structure
 *
 * Return: mei message header
 */

static u32 mei_txe_read_hdr(const struct mei_device *dev)
{
	return mei_txe_out_data_read(dev, 0);
}
/**
 * mei_txe_read - reads a message from the txe device.
 *
 * @dev: the device structure
 * @buf: message buffer will be written
 * @len: message size will be read
 *
 * Return: -EINVAL on error wrong argument and 0 on success
 */
static int mei_txe_read(struct mei_device *dev,
		unsigned char *buf, unsigned long len)
{

	struct mei_txe_hw *hw = to_txe_hw(dev);
	u32 *reg_buf, reg;
	u32 rem;
	u32 i;

	if (WARN_ON(!buf || !len))
		return -EINVAL;

	reg_buf = (u32 *)buf;
	rem = len & 0x3;

	dev_dbg(dev->dev, "buffer-length = %lu buf[0]0x%08X\n",
		len, mei_txe_out_data_read(dev, 0));

	for (i = 0; i < len / 4; i++) {
		/* skip header: index starts from 1 */
		reg = mei_txe_out_data_read(dev, i + 1);
		dev_dbg(dev->dev, "buf[%d] = 0x%08X\n", i, reg);
		*reg_buf++ = reg;
	}

	if (rem) {
		reg = mei_txe_out_data_read(dev, i + 1);
		memcpy(reg_buf, &reg, rem);
	}

	mei_txe_output_ready_set(hw);
	return 0;
}

/**
 * mei_txe_hw_reset - resets host and fw.
 *
 * @dev: the device structure
 * @intr_enable: if interrupt should be enabled after reset.
 *
 * Return: 0 on success and < 0 in case of error
 */
static int mei_txe_hw_reset(struct mei_device *dev, bool intr_enable)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);

	u32 aliveness_req;
	/*
	 * read input doorbell to ensure consistency between  Bridge and SeC
	 * return value might be garbage return
	 */
	(void)mei_txe_sec_reg_read_silent(hw, SEC_IPC_INPUT_DOORBELL_REG);

	aliveness_req = mei_txe_aliveness_req_get(dev);
	hw->aliveness = mei_txe_aliveness_get(dev);

	/* Disable interrupts in this stage we will poll */
	mei_txe_intr_disable(dev);

	/*
	 * If Aliveness Request and Aliveness Response are not equal then
	 * wait for them to be equal
	 * Since we might have interrupts disabled - poll for it
	 */
	if (aliveness_req != hw->aliveness)
		if (mei_txe_aliveness_poll(dev, aliveness_req) < 0) {
			dev_err(dev->dev, "wait for aliveness settle failed ... bailing out\n");
			return -EIO;
		}

	/*
	 * If Aliveness Request and Aliveness Response are set then clear them
	 */
	if (aliveness_req) {
		mei_txe_aliveness_set(dev, 0);
		if (mei_txe_aliveness_poll(dev, 0) < 0) {
			dev_err(dev->dev, "wait for aliveness failed ... bailing out\n");
			return -EIO;
		}
	}

	/*
	 * Set readiness RDY_CLR bit
	 */
	mei_txe_readiness_clear(dev);

	return 0;
}

/**
 * mei_txe_hw_start - start the hardware after reset
 *
 * @dev: the device structure
 *
 * Return: 0 on success an error code otherwise
 */
static int mei_txe_hw_start(struct mei_device *dev)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);
	int ret;

	u32 hisr;

	/* bring back interrupts */
	mei_txe_intr_enable(dev);

	ret = mei_txe_readiness_wait(dev);
	if (ret < 0) {
		dev_err(dev->dev, "waiting for readiness failed\n");
		return ret;
	}

	/*
	 * If HISR.INT2_STS interrupt status bit is set then clear it.
	 */
	hisr = mei_txe_br_reg_read(hw, HISR_REG);
	if (hisr & HISR_INT_2_STS)
		mei_txe_br_reg_write(hw, HISR_REG, HISR_INT_2_STS);

	/* Clear the interrupt cause of OutputDoorbell */
	clear_bit(TXE_INTR_OUT_DB_BIT, &hw->intr_cause);

	ret = mei_txe_aliveness_set_sync(dev, 1);
	if (ret < 0) {
		dev_err(dev->dev, "wait for aliveness failed ... bailing out\n");
		return ret;
	}

	pm_runtime_set_active(dev->dev);

	/* enable input ready interrupts:
	 * SEC_IPC_HOST_INT_MASK.IPC_INPUT_READY_INT_MASK
	 */
	mei_txe_input_ready_interrupt_enable(dev);


	/*  Set the SICR_SEC_IPC_OUTPUT_STATUS.IPC_OUTPUT_READY bit */
	mei_txe_output_ready_set(hw);

	/* Set bit SICR_HOST_IPC_READINESS.HOST_RDY
	 */
	mei_txe_readiness_set_host_rdy(dev);

	return 0;
}

/**
 * mei_txe_check_and_ack_intrs - translate multi BAR interrupt into
 *  single bit mask and acknowledge the interrupts
 *
 * @dev: the device structure
 * @do_ack: acknowledge interrupts
 *
 * Return: true if found interrupts to process.
 */
static bool mei_txe_check_and_ack_intrs(struct mei_device *dev, bool do_ack)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);
	u32 hisr;
	u32 hhisr;
	u32 ipc_isr;
	u32 aliveness;
	bool generated;

	/* read interrupt registers */
	hhisr = mei_txe_br_reg_read(hw, HHISR_REG);
	generated = (hhisr & IPC_HHIER_MSK);
	if (!generated)
		goto out;

	hisr = mei_txe_br_reg_read(hw, HISR_REG);

	aliveness = mei_txe_aliveness_get(dev);
	if (hhisr & IPC_HHIER_SEC && aliveness)
		ipc_isr = mei_txe_sec_reg_read_silent(hw,
				SEC_IPC_HOST_INT_STATUS_REG);
	else
		ipc_isr = 0;

	generated = generated ||
		(hisr & HISR_INT_STS_MSK) ||
		(ipc_isr & SEC_IPC_HOST_INT_STATUS_PENDING);

	if (generated && do_ack) {
		/* Save the interrupt causes */
		hw->intr_cause |= hisr & HISR_INT_STS_MSK;
		if (ipc_isr & SEC_IPC_HOST_INT_STATUS_IN_RDY)
			hw->intr_cause |= TXE_INTR_IN_READY;


		mei_txe_intr_disable(dev);
		/* Clear the interrupts in hierarchy:
		 * IPC and Bridge, than the High Level */
		mei_txe_sec_reg_write_silent(hw,
			SEC_IPC_HOST_INT_STATUS_REG, ipc_isr);
		mei_txe_br_reg_write(hw, HISR_REG, hisr);
		mei_txe_br_reg_write(hw, HHISR_REG, hhisr);
	}

out:
	return generated;
}

/**
 * mei_txe_irq_quick_handler - The ISR of the MEI device
 *
 * @irq: The irq number
 * @dev_id: pointer to the device structure
 *
 * Return: IRQ_WAKE_THREAD if interrupt is designed for the device
 *         IRQ_NONE otherwise
 */
irqreturn_t mei_txe_irq_quick_handler(int irq, void *dev_id)
{
	struct mei_device *dev = dev_id;

	if (mei_txe_check_and_ack_intrs(dev, true))
		return IRQ_WAKE_THREAD;
	return IRQ_NONE;
}


/**
 * mei_txe_irq_thread_handler - txe interrupt thread
 *
 * @irq: The irq number
 * @dev_id: pointer to the device structure
 *
 * Return: IRQ_HANDLED
 */
irqreturn_t mei_txe_irq_thread_handler(int irq, void *dev_id)
{
	struct mei_device *dev = (struct mei_device *) dev_id;
	struct mei_txe_hw *hw = to_txe_hw(dev);
	struct mei_cl_cb complete_list;
	s32 slots;
	int rets = 0;

	dev_dbg(dev->dev, "irq thread: Interrupt Registers HHISR|HISR|SEC=%02X|%04X|%02X\n",
		mei_txe_br_reg_read(hw, HHISR_REG),
		mei_txe_br_reg_read(hw, HISR_REG),
		mei_txe_sec_reg_read_silent(hw, SEC_IPC_HOST_INT_STATUS_REG));


	/* initialize our complete list */
	mutex_lock(&dev->device_lock);
	mei_io_list_init(&complete_list);

	if (pci_dev_msi_enabled(to_pci_dev(dev->dev)))
		mei_txe_check_and_ack_intrs(dev, true);

	/* show irq events */
	mei_txe_pending_interrupts(dev);

	hw->aliveness = mei_txe_aliveness_get(dev);
	hw->readiness = mei_txe_readiness_get(dev);

	/* Readiness:
	 * Detection of TXE driver going through reset
	 * or TXE driver resetting the HECI interface.
	 */
	if (test_and_clear_bit(TXE_INTR_READINESS_BIT, &hw->intr_cause)) {
		dev_dbg(dev->dev, "Readiness Interrupt was received...\n");

		/* Check if SeC is going through reset */
		if (mei_txe_readiness_is_sec_rdy(hw->readiness)) {
			dev_dbg(dev->dev, "we need to start the dev.\n");
			dev->recvd_hw_ready = true;
		} else {
			dev->recvd_hw_ready = false;
			if (dev->dev_state != MEI_DEV_RESETTING) {

				dev_warn(dev->dev, "FW not ready: resetting.\n");
				schedule_work(&dev->reset_work);
				goto end;

			}
		}
		wake_up(&dev->wait_hw_ready);
	}

	/************************************************************/
	/* Check interrupt cause:
	 * Aliveness: Detection of SeC acknowledge of host request that
	 * it remain alive or host cancellation of that request.
	 */

	if (test_and_clear_bit(TXE_INTR_ALIVENESS_BIT, &hw->intr_cause)) {
		/* Clear the interrupt cause */
		dev_dbg(dev->dev,
			"Aliveness Interrupt: Status: %d\n", hw->aliveness);
		dev->pg_event = MEI_PG_EVENT_RECEIVED;
		if (waitqueue_active(&hw->wait_aliveness_resp))
			wake_up(&hw->wait_aliveness_resp);
	}


	/* Output Doorbell:
	 * Detection of SeC having sent output to host
	 */
	slots = mei_count_full_read_slots(dev);
	if (test_and_clear_bit(TXE_INTR_OUT_DB_BIT, &hw->intr_cause)) {
		/* Read from TXE */
		rets = mei_irq_read_handler(dev, &complete_list, &slots);
		if (rets && dev->dev_state != MEI_DEV_RESETTING) {
			dev_err(dev->dev,
				"mei_irq_read_handler ret = %d.\n", rets);

			schedule_work(&dev->reset_work);
			goto end;
		}
	}
	/* Input Ready: Detection if host can write to SeC */
	if (test_and_clear_bit(TXE_INTR_IN_READY_BIT, &hw->intr_cause)) {
		dev->hbuf_is_ready = true;
		hw->slots = dev->hbuf_depth;
	}

	if (hw->aliveness && dev->hbuf_is_ready) {
		/* get the real register value */
		dev->hbuf_is_ready = mei_hbuf_is_ready(dev);
		rets = mei_irq_write_handler(dev, &complete_list);
		if (rets && rets != -EMSGSIZE)
			dev_err(dev->dev, "mei_irq_write_handler ret = %d.\n",
				rets);
		dev->hbuf_is_ready = mei_hbuf_is_ready(dev);
	}

	mei_irq_compl_handler(dev, &complete_list);

end:
	dev_dbg(dev->dev, "interrupt thread end ret = %d\n", rets);

	mutex_unlock(&dev->device_lock);

	mei_enable_interrupts(dev);
	return IRQ_HANDLED;
}

static const struct mei_hw_ops mei_txe_hw_ops = {

	.host_is_ready = mei_txe_host_is_ready,

	.fw_status = mei_txe_fw_status,
	.pg_state = mei_txe_pg_state,

	.hw_is_ready = mei_txe_hw_is_ready,
	.hw_reset = mei_txe_hw_reset,
	.hw_config = mei_txe_hw_config,
	.hw_start = mei_txe_hw_start,

	.pg_in_transition = mei_txe_pg_in_transition,
	.pg_is_enabled = mei_txe_pg_is_enabled,

	.intr_clear = mei_txe_intr_clear,
	.intr_enable = mei_txe_intr_enable,
	.intr_disable = mei_txe_intr_disable,

	.hbuf_free_slots = mei_txe_hbuf_empty_slots,
	.hbuf_is_ready = mei_txe_is_input_ready,
	.hbuf_max_len = mei_txe_hbuf_max_len,

	.write = mei_txe_write,

	.rdbuf_full_slots = mei_txe_count_full_read_slots,
	.read_hdr = mei_txe_read_hdr,

	.read = mei_txe_read,

};

/**
 * mei_txe_dev_init - allocates and initializes txe hardware specific structure
 *
 * @pdev: pci device
 *
 * Return: struct mei_device * on success or NULL
 */
struct mei_device *mei_txe_dev_init(struct pci_dev *pdev)
{
	struct mei_device *dev;
	struct mei_txe_hw *hw;

	dev = kzalloc(sizeof(struct mei_device) +
			 sizeof(struct mei_txe_hw), GFP_KERNEL);
	if (!dev)
		return NULL;

	mei_device_init(dev, &pdev->dev, &mei_txe_hw_ops);

	hw = to_txe_hw(dev);

	init_waitqueue_head(&hw->wait_aliveness_resp);

	return dev;
}

/**
 * mei_txe_setup_satt2 - SATT2 configuration for DMA support.
 *
 * @dev:   the device structure
 * @addr:  physical address start of the range
 * @range: physical range size
 *
 * Return: 0 on success an error code otherwise
 */
int mei_txe_setup_satt2(struct mei_device *dev, phys_addr_t addr, u32 range)
{
	struct mei_txe_hw *hw = to_txe_hw(dev);

	u32 lo32 = lower_32_bits(addr);
	u32 hi32 = upper_32_bits(addr);
	u32 ctrl;

	/* SATT is limited to 36 Bits */
	if (hi32 & ~0xF)
		return -EINVAL;

	/* SATT has to be 16Byte aligned */
	if (lo32 & 0xF)
		return -EINVAL;

	/* SATT range has to be 4Bytes aligned */
	if (range & 0x4)
		return -EINVAL;

	/* SATT is limited to 32 MB range*/
	if (range > SATT_RANGE_MAX)
		return -EINVAL;

	ctrl = SATT2_CTRL_VALID_MSK;
	ctrl |= hi32  << SATT2_CTRL_BR_BASE_ADDR_REG_SHIFT;

	mei_txe_br_reg_write(hw, SATT2_SAP_SIZE_REG, range);
	mei_txe_br_reg_write(hw, SATT2_BRG_BA_LSB_REG, lo32);
	mei_txe_br_reg_write(hw, SATT2_CTRL_REG, ctrl);
	dev_dbg(dev->dev, "SATT2: SAP_SIZE_OFFSET=0x%08X, BRG_BA_LSB_OFFSET=0x%08X, CTRL_OFFSET=0x%08X\n",
		range, lo32, ctrl);

	return 0;
}
