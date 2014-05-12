/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/pci.h>

#include <linux/kthread.h>
#include <linux/interrupt.h>

#include "mei_dev.h"
#include "hbm.h"

#include "hw-me.h"
#include "hw-me-regs.h"

/**
 * mei_me_reg_read - Reads 32bit data from the mei device
 *
 * @dev: the device structure
 * @offset: offset from which to read the data
 *
 * returns register value (u32)
 */
static inline u32 mei_me_reg_read(const struct mei_me_hw *hw,
			       unsigned long offset)
{
	return ioread32(hw->mem_addr + offset);
}


/**
 * mei_me_reg_write - Writes 32bit data to the mei device
 *
 * @dev: the device structure
 * @offset: offset from which to write the data
 * @value: register value to write (u32)
 */
static inline void mei_me_reg_write(const struct mei_me_hw *hw,
				 unsigned long offset, u32 value)
{
	iowrite32(value, hw->mem_addr + offset);
}

/**
 * mei_me_mecbrw_read - Reads 32bit data from ME circular buffer
 *  read window register
 *
 * @dev: the device structure
 *
 * returns ME_CB_RW register value (u32)
 */
static u32 mei_me_mecbrw_read(const struct mei_device *dev)
{
	return mei_me_reg_read(to_me_hw(dev), ME_CB_RW);
}
/**
 * mei_me_mecsr_read - Reads 32bit data from the ME CSR
 *
 * @dev: the device structure
 *
 * returns ME_CSR_HA register value (u32)
 */
static inline u32 mei_me_mecsr_read(const struct mei_me_hw *hw)
{
	return mei_me_reg_read(hw, ME_CSR_HA);
}

/**
 * mei_hcsr_read - Reads 32bit data from the host CSR
 *
 * @dev: the device structure
 *
 * returns H_CSR register value (u32)
 */
static inline u32 mei_hcsr_read(const struct mei_me_hw *hw)
{
	return mei_me_reg_read(hw, H_CSR);
}

/**
 * mei_hcsr_set - writes H_CSR register to the mei device,
 * and ignores the H_IS bit for it is write-one-to-zero.
 *
 * @dev: the device structure
 */
static inline void mei_hcsr_set(struct mei_me_hw *hw, u32 hcsr)
{
	hcsr &= ~H_IS;
	mei_me_reg_write(hw, H_CSR, hcsr);
}


/**
 * mei_me_hw_config - configure hw dependent settings
 *
 * @dev: mei device
 */
static void mei_me_hw_config(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 hcsr = mei_hcsr_read(to_me_hw(dev));
	/* Doesn't change in runtime */
	dev->hbuf_depth = (hcsr & H_CBD) >> 24;

	hw->pg_state = MEI_PG_OFF;
}

/**
 * mei_me_pg_state  - translate internal pg state
 *   to the mei power gating state
 *
 * @hw -  me hardware
 * returns: MEI_PG_OFF if aliveness is on and MEI_PG_ON otherwise
 */
static inline enum mei_pg_state mei_me_pg_state(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	return hw->pg_state;
}

/**
 * mei_clear_interrupts - clear and stop interrupts
 *
 * @dev: the device structure
 */
static void mei_me_intr_clear(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 hcsr = mei_hcsr_read(hw);
	if ((hcsr & H_IS) == H_IS)
		mei_me_reg_write(hw, H_CSR, hcsr);
}
/**
 * mei_me_intr_enable - enables mei device interrupts
 *
 * @dev: the device structure
 */
static void mei_me_intr_enable(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 hcsr = mei_hcsr_read(hw);
	hcsr |= H_IE;
	mei_hcsr_set(hw, hcsr);
}

/**
 * mei_disable_interrupts - disables mei device interrupts
 *
 * @dev: the device structure
 */
static void mei_me_intr_disable(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 hcsr = mei_hcsr_read(hw);
	hcsr  &= ~H_IE;
	mei_hcsr_set(hw, hcsr);
}

/**
 * mei_me_hw_reset_release - release device from the reset
 *
 * @dev: the device structure
 */
static void mei_me_hw_reset_release(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 hcsr = mei_hcsr_read(hw);

	hcsr |= H_IG;
	hcsr &= ~H_RST;
	mei_hcsr_set(hw, hcsr);

	/* complete this write before we set host ready on another CPU */
	mmiowb();
}
/**
 * mei_me_hw_reset - resets fw via mei csr register.
 *
 * @dev: the device structure
 * @intr_enable: if interrupt should be enabled after reset.
 */
static int mei_me_hw_reset(struct mei_device *dev, bool intr_enable)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 hcsr = mei_hcsr_read(hw);

	hcsr |= H_RST | H_IG | H_IS;

	if (intr_enable)
		hcsr |= H_IE;
	else
		hcsr &= ~H_IE;

	dev->recvd_hw_ready = false;
	mei_me_reg_write(hw, H_CSR, hcsr);

	/*
	 * Host reads the H_CSR once to ensure that the
	 * posted write to H_CSR completes.
	 */
	hcsr = mei_hcsr_read(hw);

	if ((hcsr & H_RST) == 0)
		dev_warn(&dev->pdev->dev, "H_RST is not set = 0x%08X", hcsr);

	if ((hcsr & H_RDY) == H_RDY)
		dev_warn(&dev->pdev->dev, "H_RDY is not cleared 0x%08X", hcsr);

	if (intr_enable == false)
		mei_me_hw_reset_release(dev);

	return 0;
}

/**
 * mei_me_host_set_ready - enable device
 *
 * @dev - mei device
 * returns bool
 */

static void mei_me_host_set_ready(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	hw->host_hw_state = mei_hcsr_read(hw);
	hw->host_hw_state |= H_IE | H_IG | H_RDY;
	mei_hcsr_set(hw, hw->host_hw_state);
}
/**
 * mei_me_host_is_ready - check whether the host has turned ready
 *
 * @dev - mei device
 * returns bool
 */
static bool mei_me_host_is_ready(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	hw->host_hw_state = mei_hcsr_read(hw);
	return (hw->host_hw_state & H_RDY) == H_RDY;
}

/**
 * mei_me_hw_is_ready - check whether the me(hw) has turned ready
 *
 * @dev - mei device
 * returns bool
 */
static bool mei_me_hw_is_ready(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	hw->me_hw_state = mei_me_mecsr_read(hw);
	return (hw->me_hw_state & ME_RDY_HRA) == ME_RDY_HRA;
}

static int mei_me_hw_ready_wait(struct mei_device *dev)
{
	int err;

	mutex_unlock(&dev->device_lock);
	err = wait_event_interruptible_timeout(dev->wait_hw_ready,
			dev->recvd_hw_ready,
			mei_secs_to_jiffies(MEI_HW_READY_TIMEOUT));
	mutex_lock(&dev->device_lock);
	if (!err && !dev->recvd_hw_ready) {
		if (!err)
			err = -ETIME;
		dev_err(&dev->pdev->dev,
			"wait hw ready failed. status = %d\n", err);
		return err;
	}

	dev->recvd_hw_ready = false;
	return 0;
}

static int mei_me_hw_start(struct mei_device *dev)
{
	int ret = mei_me_hw_ready_wait(dev);
	if (ret)
		return ret;
	dev_dbg(&dev->pdev->dev, "hw is ready\n");

	mei_me_host_set_ready(dev);
	return ret;
}


/**
 * mei_hbuf_filled_slots - gets number of device filled buffer slots
 *
 * @dev: the device structure
 *
 * returns number of filled slots
 */
static unsigned char mei_hbuf_filled_slots(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	char read_ptr, write_ptr;

	hw->host_hw_state = mei_hcsr_read(hw);

	read_ptr = (char) ((hw->host_hw_state & H_CBRP) >> 8);
	write_ptr = (char) ((hw->host_hw_state & H_CBWP) >> 16);

	return (unsigned char) (write_ptr - read_ptr);
}

/**
 * mei_me_hbuf_is_empty - checks if host buffer is empty.
 *
 * @dev: the device structure
 *
 * returns true if empty, false - otherwise.
 */
static bool mei_me_hbuf_is_empty(struct mei_device *dev)
{
	return mei_hbuf_filled_slots(dev) == 0;
}

/**
 * mei_me_hbuf_empty_slots - counts write empty slots.
 *
 * @dev: the device structure
 *
 * returns -EOVERFLOW if overflow, otherwise empty slots count
 */
static int mei_me_hbuf_empty_slots(struct mei_device *dev)
{
	unsigned char filled_slots, empty_slots;

	filled_slots = mei_hbuf_filled_slots(dev);
	empty_slots = dev->hbuf_depth - filled_slots;

	/* check for overflow */
	if (filled_slots > dev->hbuf_depth)
		return -EOVERFLOW;

	return empty_slots;
}

static size_t mei_me_hbuf_max_len(const struct mei_device *dev)
{
	return dev->hbuf_depth * sizeof(u32) - sizeof(struct mei_msg_hdr);
}


/**
 * mei_me_write_message - writes a message to mei device.
 *
 * @dev: the device structure
 * @header: mei HECI header of message
 * @buf: message payload will be written
 *
 * This function returns -EIO if write has failed
 */
static int mei_me_write_message(struct mei_device *dev,
			struct mei_msg_hdr *header,
			unsigned char *buf)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	unsigned long rem;
	unsigned long length = header->length;
	u32 *reg_buf = (u32 *)buf;
	u32 hcsr;
	u32 dw_cnt;
	int i;
	int empty_slots;

	dev_dbg(&dev->pdev->dev, MEI_HDR_FMT, MEI_HDR_PRM(header));

	empty_slots = mei_hbuf_empty_slots(dev);
	dev_dbg(&dev->pdev->dev, "empty slots = %hu.\n", empty_slots);

	dw_cnt = mei_data2slots(length);
	if (empty_slots < 0 || dw_cnt > empty_slots)
		return -EMSGSIZE;

	mei_me_reg_write(hw, H_CB_WW, *((u32 *) header));

	for (i = 0; i < length / 4; i++)
		mei_me_reg_write(hw, H_CB_WW, reg_buf[i]);

	rem = length & 0x3;
	if (rem > 0) {
		u32 reg = 0;
		memcpy(&reg, &buf[length - rem], rem);
		mei_me_reg_write(hw, H_CB_WW, reg);
	}

	hcsr = mei_hcsr_read(hw) | H_IG;
	mei_hcsr_set(hw, hcsr);
	if (!mei_me_hw_is_ready(dev))
		return -EIO;

	return 0;
}

/**
 * mei_me_count_full_read_slots - counts read full slots.
 *
 * @dev: the device structure
 *
 * returns -EOVERFLOW if overflow, otherwise filled slots count
 */
static int mei_me_count_full_read_slots(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	char read_ptr, write_ptr;
	unsigned char buffer_depth, filled_slots;

	hw->me_hw_state = mei_me_mecsr_read(hw);
	buffer_depth = (unsigned char)((hw->me_hw_state & ME_CBD_HRA) >> 24);
	read_ptr = (char) ((hw->me_hw_state & ME_CBRP_HRA) >> 8);
	write_ptr = (char) ((hw->me_hw_state & ME_CBWP_HRA) >> 16);
	filled_slots = (unsigned char) (write_ptr - read_ptr);

	/* check for overflow */
	if (filled_slots > buffer_depth)
		return -EOVERFLOW;

	dev_dbg(&dev->pdev->dev, "filled_slots =%08x\n", filled_slots);
	return (int)filled_slots;
}

/**
 * mei_me_read_slots - reads a message from mei device.
 *
 * @dev: the device structure
 * @buffer: message buffer will be written
 * @buffer_length: message size will be read
 */
static int mei_me_read_slots(struct mei_device *dev, unsigned char *buffer,
		    unsigned long buffer_length)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 *reg_buf = (u32 *)buffer;
	u32 hcsr;

	for (; buffer_length >= sizeof(u32); buffer_length -= sizeof(u32))
		*reg_buf++ = mei_me_mecbrw_read(dev);

	if (buffer_length > 0) {
		u32 reg = mei_me_mecbrw_read(dev);
		memcpy(reg_buf, &reg, buffer_length);
	}

	hcsr = mei_hcsr_read(hw) | H_IG;
	mei_hcsr_set(hw, hcsr);
	return 0;
}

/**
 * mei_me_pg_enter - write pg enter register to mei device.
 *
 * @dev: the device structure
 */
static void mei_me_pg_enter(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 reg = mei_me_reg_read(hw, H_HPG_CSR);
	reg |= H_HPG_CSR_PGI;
	mei_me_reg_write(hw, H_HPG_CSR, reg);
}

/**
 * mei_me_pg_enter - write pg enter register to mei device.
 *
 * @dev: the device structure
 */
static void mei_me_pg_exit(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 reg = mei_me_reg_read(hw, H_HPG_CSR);

	WARN(!(reg & H_HPG_CSR_PGI), "PGI is not set\n");

	reg |= H_HPG_CSR_PGIHEXR;
	mei_me_reg_write(hw, H_HPG_CSR, reg);
}

/**
 * mei_me_pg_set_sync - perform pg entry procedure
 *
 * @dev: the device structure
 *
 * returns 0 on success an error code otherwise
 */
int mei_me_pg_set_sync(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	unsigned long timeout = mei_secs_to_jiffies(MEI_PGI_TIMEOUT);
	int ret;

	dev->pg_event = MEI_PG_EVENT_WAIT;

	ret = mei_hbm_pg(dev, MEI_PG_ISOLATION_ENTRY_REQ_CMD);
	if (ret)
		return ret;

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(dev->wait_pg,
		dev->pg_event == MEI_PG_EVENT_RECEIVED, timeout);
	mutex_lock(&dev->device_lock);

	if (dev->pg_event == MEI_PG_EVENT_RECEIVED) {
		mei_me_pg_enter(dev);
		ret = 0;
	} else {
		ret = -ETIME;
	}

	dev->pg_event = MEI_PG_EVENT_IDLE;
	hw->pg_state = MEI_PG_ON;

	return ret;
}

/**
 * mei_me_pg_unset_sync - perform pg exit procedure
 *
 * @dev: the device structure
 *
 * returns 0 on success an error code otherwise
 */
int mei_me_pg_unset_sync(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	unsigned long timeout = mei_secs_to_jiffies(MEI_PGI_TIMEOUT);
	int ret;

	if (dev->pg_event == MEI_PG_EVENT_RECEIVED)
		goto reply;

	dev->pg_event = MEI_PG_EVENT_WAIT;

	mei_me_pg_exit(dev);

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(dev->wait_pg,
		dev->pg_event == MEI_PG_EVENT_RECEIVED, timeout);
	mutex_lock(&dev->device_lock);

reply:
	if (dev->pg_event == MEI_PG_EVENT_RECEIVED)
		ret = mei_hbm_pg(dev, MEI_PG_ISOLATION_EXIT_RES_CMD);
	else
		ret = -ETIME;

	dev->pg_event = MEI_PG_EVENT_IDLE;
	hw->pg_state = MEI_PG_OFF;

	return ret;
}

/**
 * mei_me_pg_is_enabled - detect if PG is supported by HW
 *
 * @dev: the device structure
 *
 * returns: true is pg supported, false otherwise
 */
static bool mei_me_pg_is_enabled(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 reg = mei_me_reg_read(hw, ME_CSR_HA);

	if ((reg & ME_PGIC_HRA) == 0)
		goto notsupported;

	if (dev->version.major_version < HBM_MAJOR_VERSION_PGI)
		goto notsupported;

	if (dev->version.major_version == HBM_MAJOR_VERSION_PGI &&
	    dev->version.minor_version < HBM_MINOR_VERSION_PGI)
		goto notsupported;

	return true;

notsupported:
	dev_dbg(&dev->pdev->dev, "pg: not supported: HGP = %d hbm version %d.%d ?= %d.%d\n",
		!!(reg & ME_PGIC_HRA),
		dev->version.major_version,
		dev->version.minor_version,
		HBM_MAJOR_VERSION_PGI,
		HBM_MINOR_VERSION_PGI);

	return false;
}

/**
 * mei_me_irq_quick_handler - The ISR of the MEI device
 *
 * @irq: The irq number
 * @dev_id: pointer to the device structure
 *
 * returns irqreturn_t
 */

irqreturn_t mei_me_irq_quick_handler(int irq, void *dev_id)
{
	struct mei_device *dev = (struct mei_device *) dev_id;
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 csr_reg = mei_hcsr_read(hw);

	if ((csr_reg & H_IS) != H_IS)
		return IRQ_NONE;

	/* clear H_IS bit in H_CSR */
	mei_me_reg_write(hw, H_CSR, csr_reg);

	return IRQ_WAKE_THREAD;
}

/**
 * mei_me_irq_thread_handler - function called after ISR to handle the interrupt
 * processing.
 *
 * @irq: The irq number
 * @dev_id: pointer to the device structure
 *
 * returns irqreturn_t
 *
 */
irqreturn_t mei_me_irq_thread_handler(int irq, void *dev_id)
{
	struct mei_device *dev = (struct mei_device *) dev_id;
	struct mei_cl_cb complete_list;
	s32 slots;
	int rets = 0;

	dev_dbg(&dev->pdev->dev, "function called after ISR to handle the interrupt processing.\n");
	/* initialize our complete list */
	mutex_lock(&dev->device_lock);
	mei_io_list_init(&complete_list);

	/* Ack the interrupt here
	 * In case of MSI we don't go through the quick handler */
	if (pci_dev_msi_enabled(dev->pdev))
		mei_clear_interrupts(dev);

	/* check if ME wants a reset */
	if (!mei_hw_is_ready(dev) && dev->dev_state != MEI_DEV_RESETTING) {
		dev_warn(&dev->pdev->dev, "FW not ready: resetting.\n");
		schedule_work(&dev->reset_work);
		goto end;
	}

	/*  check if we need to start the dev */
	if (!mei_host_is_ready(dev)) {
		if (mei_hw_is_ready(dev)) {
			mei_me_hw_reset_release(dev);
			dev_dbg(&dev->pdev->dev, "we need to start the dev.\n");

			dev->recvd_hw_ready = true;
			wake_up_interruptible(&dev->wait_hw_ready);
		} else {
			dev_dbg(&dev->pdev->dev, "Spurious Interrupt\n");
		}
		goto end;
	}
	/* check slots available for reading */
	slots = mei_count_full_read_slots(dev);
	while (slots > 0) {
		dev_dbg(&dev->pdev->dev, "slots to read = %08x\n", slots);
		rets = mei_irq_read_handler(dev, &complete_list, &slots);
		/* There is a race between ME write and interrupt delivery:
		 * Not all data is always available immediately after the
		 * interrupt, so try to read again on the next interrupt.
		 */
		if (rets == -ENODATA)
			break;

		if (rets && dev->dev_state != MEI_DEV_RESETTING) {
			dev_err(&dev->pdev->dev, "mei_irq_read_handler ret = %d.\n",
						rets);
			schedule_work(&dev->reset_work);
			goto end;
		}
	}

	dev->hbuf_is_ready = mei_hbuf_is_ready(dev);

	/*
	 * During PG handshake only allowed write is the replay to the
	 * PG exit message, so block calling write function
	 * if the pg state is not idle
	 */
	if (dev->pg_event == MEI_PG_EVENT_IDLE) {
		rets = mei_irq_write_handler(dev, &complete_list);
		dev->hbuf_is_ready = mei_hbuf_is_ready(dev);
	}

	mei_irq_compl_handler(dev, &complete_list);

end:
	dev_dbg(&dev->pdev->dev, "interrupt thread end ret = %d\n", rets);
	mutex_unlock(&dev->device_lock);
	return IRQ_HANDLED;
}

/**
 * mei_me_fw_status - retrieve fw status from the pci config space
 *
 * @dev: the device structure
 * @fw_status: fw status registers storage
 *
 * returns 0 on success an error code otherwise
 */
static int mei_me_fw_status(struct mei_device *dev,
			    struct mei_fw_status *fw_status)
{
	const u32 pci_cfg_reg[] = {PCI_CFG_HFS_1, PCI_CFG_HFS_2};
	int i;

	if (!fw_status)
		return -EINVAL;

	switch (dev->pdev->device) {
	case MEI_DEV_ID_IBXPK_1:
	case MEI_DEV_ID_IBXPK_2:
	case MEI_DEV_ID_CPT_1:
	case MEI_DEV_ID_PBG_1:
	case MEI_DEV_ID_PPT_1:
	case MEI_DEV_ID_PPT_2:
	case MEI_DEV_ID_PPT_3:
	case MEI_DEV_ID_LPT_H:
	case MEI_DEV_ID_LPT_W:
	case MEI_DEV_ID_LPT_LP:
	case MEI_DEV_ID_LPT_HR:
	case MEI_DEV_ID_WPT_LP:
		fw_status->count = 2;
		break;
	case MEI_DEV_ID_ICH10_1:
	case MEI_DEV_ID_ICH10_2:
	case MEI_DEV_ID_ICH10_3:
	case MEI_DEV_ID_ICH10_4:
		fw_status->count = 1;
		break;
	default:
		fw_status->count = 0;
		break;
	}

	for (i = 0; i < fw_status->count && i < MEI_FW_STATUS_MAX; i++) {
		int ret;
		ret = pci_read_config_dword(dev->pdev,
				pci_cfg_reg[i], &fw_status->status[i]);
		if (ret)
			return ret;
	}
	return 0;
}

static const struct mei_hw_ops mei_me_hw_ops = {

	.pg_state  = mei_me_pg_state,

	.fw_status = mei_me_fw_status,
	.host_is_ready = mei_me_host_is_ready,

	.hw_is_ready = mei_me_hw_is_ready,
	.hw_reset = mei_me_hw_reset,
	.hw_config = mei_me_hw_config,
	.hw_start = mei_me_hw_start,

	.pg_is_enabled = mei_me_pg_is_enabled,

	.intr_clear = mei_me_intr_clear,
	.intr_enable = mei_me_intr_enable,
	.intr_disable = mei_me_intr_disable,

	.hbuf_free_slots = mei_me_hbuf_empty_slots,
	.hbuf_is_ready = mei_me_hbuf_is_empty,
	.hbuf_max_len = mei_me_hbuf_max_len,

	.write = mei_me_write_message,

	.rdbuf_full_slots = mei_me_count_full_read_slots,
	.read_hdr = mei_me_mecbrw_read,
	.read = mei_me_read_slots
};

#define MEI_CFG_LEGACY_HFS                      \
	.fw_status.count = 0

#define MEI_CFG_ICH_HFS                        \
	.fw_status.count = 1,                   \
	.fw_status.status[0] = PCI_CFG_HFS_1

#define MEI_CFG_PCH_HFS                         \
	.fw_status.count = 2,                   \
	.fw_status.status[0] = PCI_CFG_HFS_1,   \
	.fw_status.status[1] = PCI_CFG_HFS_2


/* ICH Legacy devices */
const struct mei_cfg mei_me_legacy_cfg = {
	MEI_CFG_LEGACY_HFS,
};

/* ICH devices */
const struct mei_cfg mei_me_ich_cfg = {
	MEI_CFG_ICH_HFS,
};

/* PCH devices */
const struct mei_cfg mei_me_pch_cfg = {
	MEI_CFG_PCH_HFS,
};

/**
 * mei_me_dev_init - allocates and initializes the mei device structure
 *
 * @pdev: The pci device structure
 * @cfg: per device generation config
 *
 * returns The mei_device_device pointer on success, NULL on failure.
 */
struct mei_device *mei_me_dev_init(struct pci_dev *pdev,
				   const struct mei_cfg *cfg)
{
	struct mei_device *dev;

	dev = kzalloc(sizeof(struct mei_device) +
			 sizeof(struct mei_me_hw), GFP_KERNEL);
	if (!dev)
		return NULL;

	mei_device_init(dev, cfg);

	dev->ops = &mei_me_hw_ops;

	dev->pdev = pdev;
	return dev;
}

