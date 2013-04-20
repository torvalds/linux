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
#include "hw-me.h"

#include "hbm.h"


/**
 * mei_reg_read - Reads 32bit data from the mei device
 *
 * @dev: the device structure
 * @offset: offset from which to read the data
 *
 * returns register value (u32)
 */
static inline u32 mei_reg_read(const struct mei_me_hw *hw,
			       unsigned long offset)
{
	return ioread32(hw->mem_addr + offset);
}


/**
 * mei_reg_write - Writes 32bit data to the mei device
 *
 * @dev: the device structure
 * @offset: offset from which to write the data
 * @value: register value to write (u32)
 */
static inline void mei_reg_write(const struct mei_me_hw *hw,
				 unsigned long offset, u32 value)
{
	iowrite32(value, hw->mem_addr + offset);
}

/**
 * mei_mecbrw_read - Reads 32bit data from ME circular buffer
 *  read window register
 *
 * @dev: the device structure
 *
 * returns ME_CB_RW register value (u32)
 */
static u32 mei_me_mecbrw_read(const struct mei_device *dev)
{
	return mei_reg_read(to_me_hw(dev), ME_CB_RW);
}
/**
 * mei_mecsr_read - Reads 32bit data from the ME CSR
 *
 * @dev: the device structure
 *
 * returns ME_CSR_HA register value (u32)
 */
static inline u32 mei_mecsr_read(const struct mei_me_hw *hw)
{
	return mei_reg_read(hw, ME_CSR_HA);
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
	return mei_reg_read(hw, H_CSR);
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
	mei_reg_write(hw, H_CSR, hcsr);
}


/**
 * me_hw_config - configure hw dependent settings
 *
 * @dev: mei device
 */
static void mei_me_hw_config(struct mei_device *dev)
{
	u32 hcsr = mei_hcsr_read(to_me_hw(dev));
	/* Doesn't change in runtime */
	dev->hbuf_depth = (hcsr & H_CBD) >> 24;
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
		mei_reg_write(hw, H_CSR, hcsr);
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
}
/**
 * mei_me_hw_reset - resets fw via mei csr register.
 *
 * @dev: the device structure
 * @interrupts_enabled: if interrupt should be enabled after reset.
 */
static void mei_me_hw_reset(struct mei_device *dev, bool intr_enable)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 hcsr = mei_hcsr_read(hw);

	dev_dbg(&dev->pdev->dev, "before reset HCSR = 0x%08x.\n", hcsr);

	hcsr |= (H_RST | H_IG);

	if (intr_enable)
		hcsr |= H_IE;
	else
		hcsr |= ~H_IE;

	mei_hcsr_set(hw, hcsr);

	if (dev->dev_state == MEI_DEV_POWER_DOWN)
		mei_me_hw_reset_release(dev);

	dev_dbg(&dev->pdev->dev, "current HCSR = 0x%08x.\n", mei_hcsr_read(hw));
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
	hw->me_hw_state = mei_mecsr_read(hw);
	return (hw->me_hw_state & ME_RDY_HRA) == ME_RDY_HRA;
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
 * mei_hbuf_is_empty - checks if host buffer is empty.
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
 * returns -1(ESLOTS_OVERFLOW) if overflow, otherwise empty slots count
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
 * mei_write_message - writes a message to mei device.
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
	unsigned long rem, dw_cnt;
	unsigned long length = header->length;
	u32 *reg_buf = (u32 *)buf;
	u32 hcsr;
	int i;
	int empty_slots;

	dev_dbg(&dev->pdev->dev, MEI_HDR_FMT, MEI_HDR_PRM(header));

	empty_slots = mei_hbuf_empty_slots(dev);
	dev_dbg(&dev->pdev->dev, "empty slots = %hu.\n", empty_slots);

	dw_cnt = mei_data2slots(length);
	if (empty_slots < 0 || dw_cnt > empty_slots)
		return -EIO;

	mei_reg_write(hw, H_CB_WW, *((u32 *) header));

	for (i = 0; i < length / 4; i++)
		mei_reg_write(hw, H_CB_WW, reg_buf[i]);

	rem = length & 0x3;
	if (rem > 0) {
		u32 reg = 0;
		memcpy(&reg, &buf[length - rem], rem);
		mei_reg_write(hw, H_CB_WW, reg);
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
 * returns -1(ESLOTS_OVERFLOW) if overflow, otherwise filled slots count
 */
static int mei_me_count_full_read_slots(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	char read_ptr, write_ptr;
	unsigned char buffer_depth, filled_slots;

	hw->me_hw_state = mei_mecsr_read(hw);
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
	mei_reg_write(hw, H_CSR, csr_reg);

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
	struct mei_cl_cb *cb_pos = NULL, *cb_next = NULL;
	struct mei_cl *cl;
	s32 slots;
	int rets;
	bool  bus_message_received;


	dev_dbg(&dev->pdev->dev, "function called after ISR to handle the interrupt processing.\n");
	/* initialize our complete list */
	mutex_lock(&dev->device_lock);
	mei_io_list_init(&complete_list);

	/* Ack the interrupt here
	 * In case of MSI we don't go through the quick handler */
	if (pci_dev_msi_enabled(dev->pdev))
		mei_clear_interrupts(dev);

	/* check if ME wants a reset */
	if (!mei_hw_is_ready(dev) &&
	    dev->dev_state != MEI_DEV_RESETING &&
	    dev->dev_state != MEI_DEV_INITIALIZING) {
		dev_dbg(&dev->pdev->dev, "FW not ready.\n");
		mei_reset(dev, 1);
		mutex_unlock(&dev->device_lock);
		return IRQ_HANDLED;
	}

	/*  check if we need to start the dev */
	if (!mei_host_is_ready(dev)) {
		if (mei_hw_is_ready(dev)) {
			dev_dbg(&dev->pdev->dev, "we need to start the dev.\n");

			mei_host_set_ready(dev);

			dev_dbg(&dev->pdev->dev, "link is established start sending messages.\n");
			/* link is established * start sending messages.  */

			dev->dev_state = MEI_DEV_INIT_CLIENTS;

			mei_hbm_start_req(dev);
			mutex_unlock(&dev->device_lock);
			return IRQ_HANDLED;
		} else {
			dev_dbg(&dev->pdev->dev, "Reset Completed.\n");
			mei_me_hw_reset_release(dev);
			mutex_unlock(&dev->device_lock);
			return IRQ_HANDLED;
		}
	}
	/* check slots available for reading */
	slots = mei_count_full_read_slots(dev);
	while (slots > 0) {
		/* we have urgent data to send so break the read */
		if (dev->wr_ext_msg.hdr.length)
			break;
		dev_dbg(&dev->pdev->dev, "slots =%08x\n", slots);
		dev_dbg(&dev->pdev->dev, "call mei_irq_read_handler.\n");
		rets = mei_irq_read_handler(dev, &complete_list, &slots);
		if (rets)
			goto end;
	}
	rets = mei_irq_write_handler(dev, &complete_list);
end:
	dev_dbg(&dev->pdev->dev, "end of bottom half function.\n");
	dev->hbuf_is_ready = mei_hbuf_is_ready(dev);

	bus_message_received = false;
	if (dev->recvd_msg && waitqueue_active(&dev->wait_recvd_msg)) {
		dev_dbg(&dev->pdev->dev, "received waiting bus message\n");
		bus_message_received = true;
	}
	mutex_unlock(&dev->device_lock);
	if (bus_message_received) {
		dev_dbg(&dev->pdev->dev, "wake up dev->wait_recvd_msg\n");
		wake_up_interruptible(&dev->wait_recvd_msg);
		bus_message_received = false;
	}
	if (list_empty(&complete_list.list))
		return IRQ_HANDLED;


	list_for_each_entry_safe(cb_pos, cb_next, &complete_list.list, list) {
		cl = cb_pos->cl;
		list_del(&cb_pos->list);
		if (cl) {
			if (cl != &dev->iamthif_cl) {
				dev_dbg(&dev->pdev->dev, "completing call back.\n");
				mei_irq_complete_handler(cl, cb_pos);
				cb_pos = NULL;
			} else if (cl == &dev->iamthif_cl) {
				mei_amthif_complete(dev, cb_pos);
			}
		}
	}
	return IRQ_HANDLED;
}
static const struct mei_hw_ops mei_me_hw_ops = {

	.host_set_ready = mei_me_host_set_ready,
	.host_is_ready = mei_me_host_is_ready,

	.hw_is_ready = mei_me_hw_is_ready,
	.hw_reset = mei_me_hw_reset,
	.hw_config  = mei_me_hw_config,

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

/**
 * init_mei_device - allocates and initializes the mei device structure
 *
 * @pdev: The pci device structure
 *
 * returns The mei_device_device pointer on success, NULL on failure.
 */
struct mei_device *mei_me_dev_init(struct pci_dev *pdev)
{
	struct mei_device *dev;

	dev = kzalloc(sizeof(struct mei_device) +
			 sizeof(struct mei_me_hw), GFP_KERNEL);
	if (!dev)
		return NULL;

	mei_device_init(dev);

	INIT_LIST_HEAD(&dev->wd_cl.link);
	INIT_LIST_HEAD(&dev->iamthif_cl.link);
	mei_io_list_init(&dev->amthif_cmd_list);
	mei_io_list_init(&dev->amthif_rd_complete_list);

	INIT_DELAYED_WORK(&dev->timer_work, mei_timer);
	INIT_WORK(&dev->init_work, mei_host_client_init);

	dev->ops = &mei_me_hw_ops;

	dev->pdev = pdev;
	return dev;
}

