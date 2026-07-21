// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023-2026 Intel Corporation */
#include <linux/align.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "hw_heci.h"
#include "hw_heci_regs.h"
#include "hw_msg.h"

/**
 * heci_reg_read - Reads 32bit data from the issei heci device
 * @hw: the heci hardware structure
 * @offset: offset from which to read the data
 *
 * Return: register value (u32)
 */
static inline u32 heci_reg_read(const struct issei_heci_hw *hw, unsigned long offset)
{
	return ioread32(hw->mem_addr + offset);
}

/**
 * heci_reg_write - Writes 32bit data to the issei heci device
 *
 * @hw: the heci hardware structure
 * @offset: offset from which to write the data
 * @value: register value to write (u32)
 */
static inline void heci_reg_write(const struct issei_heci_hw *hw, unsigned long offset, u32 value)
{
	iowrite32(value, hw->mem_addr + offset);
}

/**
 * heci_fwcbrw_read - Reads 32bit data from heci circular buffer
 * @hw: the heci hardware structure
 *
 * Return: FW_CB_RW register value (u32)
 */
static inline u32 heci_fwcbrw_read(const struct issei_heci_hw *hw)
{
	return heci_reg_read(hw, FW_CB_RW);
}

/**
 * heci_hcbww_write - write 32bit data to the host circular buffer
 * @hw: the heci hardware structure
 * @data: 32bit data to be written to the host circular buffer
 */
static inline void heci_hcbww_write(const struct issei_heci_hw *hw, u32 data)
{
	heci_reg_write(hw, H_CB_WW, data);
}

/**
 * heci_irq_src - Filters IRQ source bits from the host CSR
 * @hcsr: host CSR register value
 *
 * Return: interrupt source bits of host CSR
 */
static inline u32 heci_irq_src(u32 hcsr)
{
	return hcsr & H_CSR_IS;
}

/**
 * heci_hcsr_read - Reads 32bit data from the host CSR
 * @idev: the device structure
 *
 * Return: H_CSR register value (u32)
 */
static inline u32 heci_hcsr_read(const struct issei_device *idev)
{
	return heci_reg_read(to_heci_hw(idev), H_CSR);
}

/**
 * heci_hcsr_write - writes H_CSR register to device
 * @idev: the device structure
 * @reg: new register value
 */
static inline void heci_hcsr_write(struct issei_device *idev, u32 reg)
{
	heci_reg_write(to_heci_hw(idev), H_CSR, reg);
}

/**
 * heci_hcsr_set - writes H_CSR register to the heci device
 * @idev: the device structure
 * @reg: new register value
 *
 * Writes H_CSR register to the heci device
 * and ignores the H_IS bit for it is write-one-to-zero.
 *
 */
static inline void heci_hcsr_set(struct issei_device *idev, u32 reg)
{
	reg &= ~H_CSR_IS;
	heci_hcsr_write(idev, reg);
}

/**
 * heci_hcsr_set_hig - set host interrupt (set H_CSR_IG)
 * @idev: the device structure
 */
static inline void heci_hcsr_set_hig(struct issei_device *idev)
{
	u32 reg;

	reg = heci_hcsr_read(idev) | H_CSR_IG;
	heci_hcsr_set(idev, reg);
}

/**
 * heci_fwcsr_read - Reads 32bit data from the FW CSR
 * @idev: the device structure
 *
 * Return: FW_CSR_HA register value (u32)
 */
static inline u32 heci_fwcsr_read(const struct issei_device *idev)
{
	return heci_reg_read(to_heci_hw(idev), FW_CSR_HA);
}

/**
 * heci_count_full_read_slots - counts read full slots.
 * @idev: the device structure
 *
 * Return: -EOVERFLOW if overflow, otherwise filled slots count
 */
static int heci_count_full_read_slots(struct issei_device *idev)
{
	u8 buffer_depth, filled_slots;
	u8 read_ptr, write_ptr;
	u32 reg;

	reg = heci_fwcsr_read(idev);
	buffer_depth = (u8)FIELD_GET(FW_CSR_CBD, reg);
	read_ptr = (u8)FIELD_GET(FW_CSR_CBRP, reg);
	write_ptr = (u8)FIELD_GET(FW_CSR_CBWP, reg);
	filled_slots = write_ptr - read_ptr;

	/* check for overflow */
	if (filled_slots > buffer_depth)
		return -EOVERFLOW;

	dev_dbg(&idev->dev, "filled_slots = %08x\n", filled_slots);
	return filled_slots;
}

/**
 * heci_irq_disable - disables heci device interrupts
 * @idev: the device structure
 * @reg: supplied hcsr register value
 *
 * disables heci device interrupts using supplied hcsr register value.
 */
static inline void heci_irq_disable(struct issei_device *idev, u32 reg)
{
	reg &= ~H_CSR_IE;
	heci_hcsr_set(idev, reg);
}

/**
 * heci_irq_clear - clear and stop interrupts
 * @idev: the device structure
 * @reg: supplied hcsr register value
 */
static inline void heci_irq_clear(struct issei_device *idev, u32 reg)
{
	if (heci_irq_src(reg))
		heci_hcsr_write(idev, reg);
}

/**
 * issei_heci_irq_clear - clear and stop interrupts
 * @idev: the device structure
 */
static void issei_heci_irq_clear(struct issei_device *idev)
{
	u32 reg = heci_hcsr_read(idev);

	heci_irq_clear(idev, reg);
}

/**
 * issei_heci_irq_enable - enables heci device interrupts
 * @idev: the device structure
 */
static void issei_heci_irq_enable(struct issei_device *idev)
{
	u32 reg;

	reg = heci_hcsr_read(idev) | H_CSR_IE;
	heci_hcsr_set(idev, reg);
}

/**
 * issei_heci_irq_disable - disables heci device interrupts
 * @idev: the device structure
 */
static void issei_heci_irq_disable(struct issei_device *idev)
{
	u32 reg = heci_hcsr_read(idev);

	heci_irq_disable(idev, reg);
}

static void issei_heci_irq_sync(struct issei_device *idev)
{
	synchronize_irq(to_heci_hw(idev)->irq);
}

/**
 * issei_heci_hw_reset_release - release device from the reset
 * @idev: the device structure
 */
static void issei_heci_hw_reset_release(struct issei_device *idev)
{
	u32 reg = heci_hcsr_read(idev);

	reg |= H_CSR_IG;
	reg &= ~H_CSR_RST;
	heci_hcsr_set(idev, reg);
}

/**
 * heci_hw_is_ready - check whether the hw has turned ready
 * @idev: the device structure
 *
 * Return: bool
 */
static bool heci_hw_is_ready(struct issei_device *idev)
{
	u32 reg = heci_fwcsr_read(idev);

	return reg & FW_CSR_RDY;
}

/**
 * heci_hw_is_resetting - check whether the hw is in reset
 * @idev: the device structure
 *
 * Return: bool
 */
static bool heci_hw_is_resetting(struct issei_device *idev)
{
	u32 reg = heci_fwcsr_read(idev);

	return reg & FW_CSR_RST;
}

/**
 * issei_heci_host_set_ready - enable device
 * @idev: the device structure
 */
static void issei_heci_host_set_ready(struct issei_device *idev)
{
	u32 reg = heci_hcsr_read(idev);

	reg |= H_CSR_IE | H_CSR_IG | H_CSR_RDY;
	heci_hcsr_set(idev, reg);
}

/**
 * issei_heci_hw_reset - resets fw via heci csr register.
 * @idev: the device structure
 * @enable: if interrupt should be enabled after reset.
 *
 * Return: 0 on success an error code otherwise
 */
static int issei_heci_hw_reset(struct issei_device *idev, bool enable)
{
	u32 reg;

	if (enable)
		issei_heci_irq_enable(idev);

	reg = heci_hcsr_read(idev);
	/*
	 * H_CSR_RST may be found lit before reset is started,
	 * for example if preceding reset flow hasn't completed.
	 * In that case asserting H_CSR_RST will be ignored, therefore
	 * we need to clean H_CSR_RST bit to start a successful reset sequence.
	 */
	if (reg & H_CSR_RST) {
		dev_warn(&idev->dev, "H_CSR_RST is set = 0x%08X", reg);
		reg &= ~H_CSR_RST;
		heci_hcsr_set(idev, reg);
		reg = heci_hcsr_read(idev);
	}

	reg |= H_CSR_RST | H_CSR_IG | H_CSR_IS;

	if (!enable)
		reg &= ~H_CSR_IE;

	heci_hcsr_write(idev, reg);

	/*
	 * Host reads the H_CSR once to ensure that the
	 * posted write to H_CSR completes.
	 */
	reg = heci_hcsr_read(idev);

	if (!(reg & H_CSR_RST))
		dev_warn(&idev->dev, "H_CSR_RST is not set = 0x%08X", reg);

	if (reg & H_CSR_RDY)
		dev_warn(&idev->dev, "H_CSR_RDY is not cleared 0x%08X", reg);

	if (!enable)
		issei_heci_hw_reset_release(idev);
	return 0;
}

/**
 * issei_heci_irq_write_generate - generate interrupt to signal write completion.
 * @idev: the device structure
 *
 * Return: 0 on success, -EIO if hardware is not ready and requires reset
 */
static int issei_heci_irq_write_generate(struct issei_device *idev)
{
	struct issei_heci_hw *hw = to_heci_hw(idev);

	scoped_guard(spinlock_irqsave, &hw->access_lock)
		heci_hcsr_set_hig(idev);
	if (!heci_hw_is_ready(idev))
		return -EIO;
	return 0;
}

/**
 * issei_heci_hw_config - initial hardware configuration.
 * @idev: the device structure
 *
 * Return: 0 always
 */
static int issei_heci_hw_config(struct issei_device *idev)
{
	struct issei_heci_hw *hw = to_heci_hw(idev);
	u32 reg;

	/* Doesn't change in runtime */
	reg = heci_hcsr_read(idev);
	hw->hbuf_depth = FIELD_GET(H_CSR_CBD, reg);

	return 0;
}

/**
 * heci_write_hbuf - write to hardware buffer
 * @idev: the device structure
 * @data: data to write
 * @data_len: data size
 *
 * Return: 0 on success, <0 on error
 */
static int heci_write_hbuf(struct issei_device *idev, const void *data, size_t data_len)
{
	struct issei_heci_hw *hw = to_heci_hw(idev);

	if (!IS_ALIGNED(data_len, CB_SLOT_SIZE)) {
		dev_err(&idev->dev, "Data size %zu not aligned to slot size %zu\n",
			data_len, CB_SLOT_SIZE);
		return -EINVAL;
	}

	scoped_guard(spinlock_irqsave, &hw->access_lock) {
		const u32 *reg_buf = data;
		size_t i;

		for (i = 0; i < data_len / CB_SLOT_SIZE; i++)
			heci_hcbww_write(hw, reg_buf[i]);
	}

	return issei_heci_irq_write_generate(idev);
}

/**
 * heci_read_hbuf - read data from hardware buffer
 * @idev: the device structure
 * @data: buffer to store read data
 * @data_len: buffer size
 *
 * Return: 0 on success, <0 on error
 */
static int heci_read_hbuf(struct issei_device *idev, void *data, size_t data_len)
{
	struct issei_heci_hw *hw = to_heci_hw(idev);
	u32 *reg_buf = data;

	if (!IS_ALIGNED(data_len, CB_SLOT_SIZE)) {
		dev_err(&idev->dev, "Data size %zu not aligned to slot size %zu\n",
			data_len, CB_SLOT_SIZE);
		return -EINVAL;
	}

	scoped_guard(spinlock_irqsave, &hw->access_lock) {
		for (; data_len >= CB_SLOT_SIZE; data_len -= CB_SLOT_SIZE)
			*reg_buf++ = heci_fwcbrw_read(hw);
	}
	return 0;
}

/**
 * issei_heci_setup_message_send - send setup message to firmware
 * @idev: the device structure
 *
 * Return: 0 on success, <0 on error
 */
static int issei_heci_setup_message_send(struct issei_device *idev)
{
	struct ham_setup_shared_memory_req req = {
		.msg_id = HAM_CB_MESSAGE_ID_REQ,
		.ver = HAM_CB_MESSAGE_VER,
		.reserved = 0,
		.buffer_physical_address = idev->dma.daddr,
		.host_to_fw_section_length = idev->dma.length.h2f,
		.fw_to_host_section_length = idev->dma.length.f2h,
		.control_length = idev->dma.length.ctl
	};
	int ret;

	ret = heci_write_hbuf(idev, &req, sizeof(req));
	if (ret)
		dev_err(&idev->dev, "Shared memory req write failed ret = %d\n", ret);

	return ret;
}

/**
 * issei_heci_setup_message_recv - receive setup message response from firmware
 * @idev: the device structure
 *
 * Return: 0 on success, <0 on error
 */
static int issei_heci_setup_message_recv(struct issei_device *idev)
{
	struct ham_setup_shared_memory_res res;
	int ret;

	if (heci_count_full_read_slots(idev) != sizeof(res) / CB_SLOT_SIZE) {
		dev_dbg(&idev->dev, "Setup response is not fully received\n");
		return -ENODATA;
	}

	ret = heci_read_hbuf(idev, &res, sizeof(res));
	if (ret) {
		dev_err(&idev->dev, "Shared memory res read failed ret = %d\n", ret);
		return ret;
	}

	if (res.msg_id != HAM_CB_MESSAGE_ID_RES) {
		dev_err(&idev->dev, "Shared memory res header 0x%x != 0x%x\n",
			res.msg_id, HAM_CB_MESSAGE_ID_RES);
		return -EPROTO;
	}
	if (res.status != 0) {
		dev_err(&idev->dev, "Shared memory res status %d != 0\n", res.status);
		return -EPROTO;
	}

	return 0;
}

static const struct issei_hw_ops hw_heci_ops = {
	.irq_clear = issei_heci_irq_clear,
	.irq_enable = issei_heci_irq_enable,
	.irq_disable = issei_heci_irq_disable,
	.irq_sync = issei_heci_irq_sync,

	.hw_reset = issei_heci_hw_reset,
	.hw_config = issei_heci_hw_config,
	.hw_reset_release = issei_heci_hw_reset_release,
	.host_set_ready = issei_heci_host_set_ready,

	.hw_is_ready = heci_hw_is_ready,

	.setup_message_send = issei_heci_setup_message_send,
	.setup_message_recv = issei_heci_setup_message_recv,
	.irq_write_generate = issei_heci_irq_write_generate,
};

const struct issei_hw_ops *issei_heci_get_ops(void)
{
	return &hw_heci_ops;
}

irqreturn_t issei_heci_irq_quick_handler(int irq, void *dev_id)
{
	struct issei_device *idev = dev_id;
	struct issei_heci_hw *hw = to_heci_hw(idev);
	u32 reg;

	reg = heci_hcsr_read(idev);
	if (!heci_irq_src(reg))
		return IRQ_NONE;

	scoped_guard(spinlock_irqsave, &hw->access_lock) {
		reg = heci_hcsr_read(idev);
		heci_irq_clear(idev, reg);

		if (heci_hw_is_resetting(idev))
			heci_hcsr_set_hig(idev);
	}

	dev_dbg(&idev->dev, "interrupt source 0x%08X\n", heci_irq_src(reg));

	issei_poke_process_thread(idev);

	return IRQ_HANDLED;
}

static const struct hw_heci_cfg hw_heci_pch_cfg = {
	.dma_length.h2f = SZ_64K,
	.dma_length.f2h = SZ_64K,
	.dma_length.ctl = SZ_4K,
};

const struct hw_heci_cfg *issei_heci_get_cfg(kernel_ulong_t idx)
{
	return &hw_heci_pch_cfg;
}

/**
 * issei_heci_dev_init - initializes the issei device structure with hw_heci
 * @idev: device structure
 * @mem_addr: memory address on bar
 * @cfg: per device generation config
 */
void issei_heci_dev_init(struct issei_device *idev,
			 void __iomem *mem_addr, const struct hw_heci_cfg *cfg)
{
	struct issei_heci_hw *hw = to_heci_hw(idev);

	spin_lock_init(&hw->access_lock);
	hw->mem_addr = mem_addr;
	hw->cfg = cfg;
}
