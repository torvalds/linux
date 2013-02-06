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
#include <linux/mei.h>

#include "mei_dev.h"
#include "hw-me.h"

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
u32 mei_mecbrw_read(const struct mei_device *dev)
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
void mei_hw_config(struct mei_device *dev)
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
void mei_clear_interrupts(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 hcsr = mei_hcsr_read(hw);
	if ((hcsr & H_IS) == H_IS)
		mei_reg_write(hw, H_CSR, hcsr);
}

/**
 * mei_enable_interrupts - enables mei device interrupts
 *
 * @dev: the device structure
 */
void mei_enable_interrupts(struct mei_device *dev)
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
void mei_disable_interrupts(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 hcsr = mei_hcsr_read(hw);
	hcsr  &= ~H_IE;
	mei_hcsr_set(hw, hcsr);
}

/**
 * mei_hw_reset - resets fw via mei csr register.
 *
 * @dev: the device structure
 * @interrupts_enabled: if interrupt should be enabled after reset.
 */
void mei_hw_reset(struct mei_device *dev, bool intr_enable)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 hcsr = mei_hcsr_read(hw);

	dev_dbg(&dev->pdev->dev, "before reset HCSR = 0x%08x.\n", hcsr);

	hcsr |= (H_RST | H_IG);

	if (intr_enable)
		hcsr |= H_IE;
	else
		hcsr &= ~H_IE;

	mei_hcsr_set(hw, hcsr);

	hcsr = mei_hcsr_read(hw) | H_IG;
	hcsr &= ~H_RST;

	mei_hcsr_set(hw, hcsr);

	hcsr = mei_hcsr_read(hw);

	dev_dbg(&dev->pdev->dev, "current HCSR = 0x%08x.\n", hcsr);
}

/**
 * mei_host_set_ready - enable device
 *
 * @dev - mei device
 * returns bool
 */

void mei_host_set_ready(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	hw->host_hw_state |= H_IE | H_IG | H_RDY;
	mei_hcsr_set(hw, hw->host_hw_state);
}
/**
 * mei_host_is_ready - check whether the host has turned ready
 *
 * @dev - mei device
 * returns bool
 */
bool mei_host_is_ready(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	hw->host_hw_state = mei_hcsr_read(hw);
	return (hw->host_hw_state & H_RDY) == H_RDY;
}

/**
 * mei_me_is_ready - check whether the me has turned ready
 *
 * @dev - mei device
 * returns bool
 */
bool mei_me_is_ready(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	hw->me_hw_state = mei_mecsr_read(hw);
	return (hw->me_hw_state & ME_RDY_HRA) == ME_RDY_HRA;
}

/**
 * mei_interrupt_quick_handler - The ISR of the MEI device
 *
 * @irq: The irq number
 * @dev_id: pointer to the device structure
 *
 * returns irqreturn_t
 */
irqreturn_t mei_interrupt_quick_handler(int irq, void *dev_id)
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
bool mei_hbuf_is_empty(struct mei_device *dev)
{
	return mei_hbuf_filled_slots(dev) == 0;
}

/**
 * mei_hbuf_empty_slots - counts write empty slots.
 *
 * @dev: the device structure
 *
 * returns -1(ESLOTS_OVERFLOW) if overflow, otherwise empty slots count
 */
int mei_hbuf_empty_slots(struct mei_device *dev)
{
	unsigned char filled_slots, empty_slots;

	filled_slots = mei_hbuf_filled_slots(dev);
	empty_slots = dev->hbuf_depth - filled_slots;

	/* check for overflow */
	if (filled_slots > dev->hbuf_depth)
		return -EOVERFLOW;

	return empty_slots;
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
int mei_write_message(struct mei_device *dev, struct mei_msg_hdr *header,
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
	if (!mei_me_is_ready(dev))
		return -EIO;

	return 0;
}

/**
 * mei_count_full_read_slots - counts read full slots.
 *
 * @dev: the device structure
 *
 * returns -1(ESLOTS_OVERFLOW) if overflow, otherwise filled slots count
 */
int mei_count_full_read_slots(struct mei_device *dev)
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
 * mei_read_slots - reads a message from mei device.
 *
 * @dev: the device structure
 * @buffer: message buffer will be written
 * @buffer_length: message size will be read
 */
void mei_read_slots(struct mei_device *dev, unsigned char *buffer,
		    unsigned long buffer_length)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 *reg_buf = (u32 *)buffer;
	u32 hcsr;

	for (; buffer_length >= sizeof(u32); buffer_length -= sizeof(u32))
		*reg_buf++ = mei_mecbrw_read(dev);

	if (buffer_length > 0) {
		u32 reg = mei_mecbrw_read(dev);
		memcpy(reg_buf, &reg, buffer_length);
	}

	hcsr = mei_hcsr_read(hw) | H_IG;
	mei_hcsr_set(hw, hcsr);
}

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

	dev->pdev = pdev;
	return dev;
}
