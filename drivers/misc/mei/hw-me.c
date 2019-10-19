// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2003-2018, Intel Corporation. All rights reserved.
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */

#include <linux/pci.h>

#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/sizes.h>

#include "mei_dev.h"
#include "hbm.h"

#include "hw-me.h"
#include "hw-me-regs.h"

#include "mei-trace.h"

/**
 * mei_me_reg_read - Reads 32bit data from the mei device
 *
 * @hw: the me hardware structure
 * @offset: offset from which to read the data
 *
 * Return: register value (u32)
 */
static inline u32 mei_me_reg_read(const struct mei_me_hw *hw,
			       unsigned long offset)
{
	return ioread32(hw->mem_addr + offset);
}


/**
 * mei_me_reg_write - Writes 32bit data to the mei device
 *
 * @hw: the me hardware structure
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
 * Return: ME_CB_RW register value (u32)
 */
static inline u32 mei_me_mecbrw_read(const struct mei_device *dev)
{
	return mei_me_reg_read(to_me_hw(dev), ME_CB_RW);
}

/**
 * mei_me_hcbww_write - write 32bit data to the host circular buffer
 *
 * @dev: the device structure
 * @data: 32bit data to be written to the host circular buffer
 */
static inline void mei_me_hcbww_write(struct mei_device *dev, u32 data)
{
	mei_me_reg_write(to_me_hw(dev), H_CB_WW, data);
}

/**
 * mei_me_mecsr_read - Reads 32bit data from the ME CSR
 *
 * @dev: the device structure
 *
 * Return: ME_CSR_HA register value (u32)
 */
static inline u32 mei_me_mecsr_read(const struct mei_device *dev)
{
	u32 reg;

	reg = mei_me_reg_read(to_me_hw(dev), ME_CSR_HA);
	trace_mei_reg_read(dev->dev, "ME_CSR_HA", ME_CSR_HA, reg);

	return reg;
}

/**
 * mei_hcsr_read - Reads 32bit data from the host CSR
 *
 * @dev: the device structure
 *
 * Return: H_CSR register value (u32)
 */
static inline u32 mei_hcsr_read(const struct mei_device *dev)
{
	u32 reg;

	reg = mei_me_reg_read(to_me_hw(dev), H_CSR);
	trace_mei_reg_read(dev->dev, "H_CSR", H_CSR, reg);

	return reg;
}

/**
 * mei_hcsr_write - writes H_CSR register to the mei device
 *
 * @dev: the device structure
 * @reg: new register value
 */
static inline void mei_hcsr_write(struct mei_device *dev, u32 reg)
{
	trace_mei_reg_write(dev->dev, "H_CSR", H_CSR, reg);
	mei_me_reg_write(to_me_hw(dev), H_CSR, reg);
}

/**
 * mei_hcsr_set - writes H_CSR register to the mei device,
 * and ignores the H_IS bit for it is write-one-to-zero.
 *
 * @dev: the device structure
 * @reg: new register value
 */
static inline void mei_hcsr_set(struct mei_device *dev, u32 reg)
{
	reg &= ~H_CSR_IS_MASK;
	mei_hcsr_write(dev, reg);
}

/**
 * mei_hcsr_set_hig - set host interrupt (set H_IG)
 *
 * @dev: the device structure
 */
static inline void mei_hcsr_set_hig(struct mei_device *dev)
{
	u32 hcsr;

	hcsr = mei_hcsr_read(dev) | H_IG;
	mei_hcsr_set(dev, hcsr);
}

/**
 * mei_me_d0i3c_read - Reads 32bit data from the D0I3C register
 *
 * @dev: the device structure
 *
 * Return: H_D0I3C register value (u32)
 */
static inline u32 mei_me_d0i3c_read(const struct mei_device *dev)
{
	u32 reg;

	reg = mei_me_reg_read(to_me_hw(dev), H_D0I3C);
	trace_mei_reg_read(dev->dev, "H_D0I3C", H_D0I3C, reg);

	return reg;
}

/**
 * mei_me_d0i3c_write - writes H_D0I3C register to device
 *
 * @dev: the device structure
 * @reg: new register value
 */
static inline void mei_me_d0i3c_write(struct mei_device *dev, u32 reg)
{
	trace_mei_reg_write(dev->dev, "H_D0I3C", H_D0I3C, reg);
	mei_me_reg_write(to_me_hw(dev), H_D0I3C, reg);
}

/**
 * mei_me_fw_status - read fw status register from pci config space
 *
 * @dev: mei device
 * @fw_status: fw status register values
 *
 * Return: 0 on success, error otherwise
 */
static int mei_me_fw_status(struct mei_device *dev,
			    struct mei_fw_status *fw_status)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct mei_me_hw *hw = to_me_hw(dev);
	const struct mei_fw_status *fw_src = &hw->cfg->fw_status;
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
 * mei_me_hw_config - configure hw dependent settings
 *
 * @dev: mei device
 */
static void mei_me_hw_config(struct mei_device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 hcsr, reg;

	/* Doesn't change in runtime */
	hcsr = mei_hcsr_read(dev);
	hw->hbuf_depth = (hcsr & H_CBD) >> 24;

	reg = 0;
	pci_read_config_dword(pdev, PCI_CFG_HFS_1, &reg);
	trace_mei_pci_cfg_read(dev->dev, "PCI_CFG_HFS_1", PCI_CFG_HFS_1, reg);
	hw->d0i3_supported =
		((reg & PCI_CFG_HFS_1_D0I3_MSK) == PCI_CFG_HFS_1_D0I3_MSK);

	hw->pg_state = MEI_PG_OFF;
	if (hw->d0i3_supported) {
		reg = mei_me_d0i3c_read(dev);
		if (reg & H_D0I3C_I3)
			hw->pg_state = MEI_PG_ON;
	}
}

/**
 * mei_me_pg_state  - translate internal pg state
 *   to the mei power gating state
 *
 * @dev:  mei device
 *
 * Return: MEI_PG_OFF if aliveness is on and MEI_PG_ON otherwise
 */
static inline enum mei_pg_state mei_me_pg_state(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);

	return hw->pg_state;
}

static inline u32 me_intr_src(u32 hcsr)
{
	return hcsr & H_CSR_IS_MASK;
}

/**
 * me_intr_disable - disables mei device interrupts
 *      using supplied hcsr register value.
 *
 * @dev: the device structure
 * @hcsr: supplied hcsr register value
 */
static inline void me_intr_disable(struct mei_device *dev, u32 hcsr)
{
	hcsr &= ~H_CSR_IE_MASK;
	mei_hcsr_set(dev, hcsr);
}

/**
 * mei_me_intr_clear - clear and stop interrupts
 *
 * @dev: the device structure
 * @hcsr: supplied hcsr register value
 */
static inline void me_intr_clear(struct mei_device *dev, u32 hcsr)
{
	if (me_intr_src(hcsr))
		mei_hcsr_write(dev, hcsr);
}

/**
 * mei_me_intr_clear - clear and stop interrupts
 *
 * @dev: the device structure
 */
static void mei_me_intr_clear(struct mei_device *dev)
{
	u32 hcsr = mei_hcsr_read(dev);

	me_intr_clear(dev, hcsr);
}
/**
 * mei_me_intr_enable - enables mei device interrupts
 *
 * @dev: the device structure
 */
static void mei_me_intr_enable(struct mei_device *dev)
{
	u32 hcsr = mei_hcsr_read(dev);

	hcsr |= H_CSR_IE_MASK;
	mei_hcsr_set(dev, hcsr);
}

/**
 * mei_me_intr_disable - disables mei device interrupts
 *
 * @dev: the device structure
 */
static void mei_me_intr_disable(struct mei_device *dev)
{
	u32 hcsr = mei_hcsr_read(dev);

	me_intr_disable(dev, hcsr);
}

/**
 * mei_me_synchronize_irq - wait for pending IRQ handlers
 *
 * @dev: the device structure
 */
static void mei_me_synchronize_irq(struct mei_device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	synchronize_irq(pdev->irq);
}

/**
 * mei_me_hw_reset_release - release device from the reset
 *
 * @dev: the device structure
 */
static void mei_me_hw_reset_release(struct mei_device *dev)
{
	u32 hcsr = mei_hcsr_read(dev);

	hcsr |= H_IG;
	hcsr &= ~H_RST;
	mei_hcsr_set(dev, hcsr);
}

/**
 * mei_me_host_set_ready - enable device
 *
 * @dev: mei device
 */
static void mei_me_host_set_ready(struct mei_device *dev)
{
	u32 hcsr = mei_hcsr_read(dev);

	hcsr |= H_CSR_IE_MASK | H_IG | H_RDY;
	mei_hcsr_set(dev, hcsr);
}

/**
 * mei_me_host_is_ready - check whether the host has turned ready
 *
 * @dev: mei device
 * Return: bool
 */
static bool mei_me_host_is_ready(struct mei_device *dev)
{
	u32 hcsr = mei_hcsr_read(dev);

	return (hcsr & H_RDY) == H_RDY;
}

/**
 * mei_me_hw_is_ready - check whether the me(hw) has turned ready
 *
 * @dev: mei device
 * Return: bool
 */
static bool mei_me_hw_is_ready(struct mei_device *dev)
{
	u32 mecsr = mei_me_mecsr_read(dev);

	return (mecsr & ME_RDY_HRA) == ME_RDY_HRA;
}

/**
 * mei_me_hw_is_resetting - check whether the me(hw) is in reset
 *
 * @dev: mei device
 * Return: bool
 */
static bool mei_me_hw_is_resetting(struct mei_device *dev)
{
	u32 mecsr = mei_me_mecsr_read(dev);

	return (mecsr & ME_RST_HRA) == ME_RST_HRA;
}

/**
 * mei_me_hw_ready_wait - wait until the me(hw) has turned ready
 *  or timeout is reached
 *
 * @dev: mei device
 * Return: 0 on success, error otherwise
 */
static int mei_me_hw_ready_wait(struct mei_device *dev)
{
	mutex_unlock(&dev->device_lock);
	wait_event_timeout(dev->wait_hw_ready,
			dev->recvd_hw_ready,
			mei_secs_to_jiffies(MEI_HW_READY_TIMEOUT));
	mutex_lock(&dev->device_lock);
	if (!dev->recvd_hw_ready) {
		dev_err(dev->dev, "wait hw ready failed\n");
		return -ETIME;
	}

	mei_me_hw_reset_release(dev);
	dev->recvd_hw_ready = false;
	return 0;
}

/**
 * mei_me_hw_start - hw start routine
 *
 * @dev: mei device
 * Return: 0 on success, error otherwise
 */
static int mei_me_hw_start(struct mei_device *dev)
{
	int ret = mei_me_hw_ready_wait(dev);

	if (ret)
		return ret;
	dev_dbg(dev->dev, "hw is ready\n");

	mei_me_host_set_ready(dev);
	return ret;
}


/**
 * mei_hbuf_filled_slots - gets number of device filled buffer slots
 *
 * @dev: the device structure
 *
 * Return: number of filled slots
 */
static unsigned char mei_hbuf_filled_slots(struct mei_device *dev)
{
	u32 hcsr;
	char read_ptr, write_ptr;

	hcsr = mei_hcsr_read(dev);

	read_ptr = (char) ((hcsr & H_CBRP) >> 8);
	write_ptr = (char) ((hcsr & H_CBWP) >> 16);

	return (unsigned char) (write_ptr - read_ptr);
}

/**
 * mei_me_hbuf_is_empty - checks if host buffer is empty.
 *
 * @dev: the device structure
 *
 * Return: true if empty, false - otherwise.
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
 * Return: -EOVERFLOW if overflow, otherwise empty slots count
 */
static int mei_me_hbuf_empty_slots(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	unsigned char filled_slots, empty_slots;

	filled_slots = mei_hbuf_filled_slots(dev);
	empty_slots = hw->hbuf_depth - filled_slots;

	/* check for overflow */
	if (filled_slots > hw->hbuf_depth)
		return -EOVERFLOW;

	return empty_slots;
}

/**
 * mei_me_hbuf_depth - returns depth of the hw buffer.
 *
 * @dev: the device structure
 *
 * Return: size of hw buffer in slots
 */
static u32 mei_me_hbuf_depth(const struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);

	return hw->hbuf_depth;
}

/**
 * mei_me_hbuf_write - writes a message to host hw buffer.
 *
 * @dev: the device structure
 * @hdr: header of message
 * @hdr_len: header length in bytes: must be multiplication of a slot (4bytes)
 * @data: payload
 * @data_len: payload length in bytes
 *
 * Return: 0 if success, < 0 - otherwise.
 */
static int mei_me_hbuf_write(struct mei_device *dev,
			     const void *hdr, size_t hdr_len,
			     const void *data, size_t data_len)
{
	unsigned long rem;
	unsigned long i;
	const u32 *reg_buf;
	u32 dw_cnt;
	int empty_slots;

	if (WARN_ON(!hdr || !data || hdr_len & 0x3))
		return -EINVAL;

	dev_dbg(dev->dev, MEI_HDR_FMT, MEI_HDR_PRM((struct mei_msg_hdr *)hdr));

	empty_slots = mei_hbuf_empty_slots(dev);
	dev_dbg(dev->dev, "empty slots = %hu.\n", empty_slots);

	if (empty_slots < 0)
		return -EOVERFLOW;

	dw_cnt = mei_data2slots(hdr_len + data_len);
	if (dw_cnt > (u32)empty_slots)
		return -EMSGSIZE;

	reg_buf = hdr;
	for (i = 0; i < hdr_len / MEI_SLOT_SIZE; i++)
		mei_me_hcbww_write(dev, reg_buf[i]);

	reg_buf = data;
	for (i = 0; i < data_len / MEI_SLOT_SIZE; i++)
		mei_me_hcbww_write(dev, reg_buf[i]);

	rem = data_len & 0x3;
	if (rem > 0) {
		u32 reg = 0;

		memcpy(&reg, (const u8 *)data + data_len - rem, rem);
		mei_me_hcbww_write(dev, reg);
	}

	mei_hcsr_set_hig(dev);
	if (!mei_me_hw_is_ready(dev))
		return -EIO;

	return 0;
}

/**
 * mei_me_count_full_read_slots - counts read full slots.
 *
 * @dev: the device structure
 *
 * Return: -EOVERFLOW if overflow, otherwise filled slots count
 */
static int mei_me_count_full_read_slots(struct mei_device *dev)
{
	u32 me_csr;
	char read_ptr, write_ptr;
	unsigned char buffer_depth, filled_slots;

	me_csr = mei_me_mecsr_read(dev);
	buffer_depth = (unsigned char)((me_csr & ME_CBD_HRA) >> 24);
	read_ptr = (char) ((me_csr & ME_CBRP_HRA) >> 8);
	write_ptr = (char) ((me_csr & ME_CBWP_HRA) >> 16);
	filled_slots = (unsigned char) (write_ptr - read_ptr);

	/* check for overflow */
	if (filled_slots > buffer_depth)
		return -EOVERFLOW;

	dev_dbg(dev->dev, "filled_slots =%08x\n", filled_slots);
	return (int)filled_slots;
}

/**
 * mei_me_read_slots - reads a message from mei device.
 *
 * @dev: the device structure
 * @buffer: message buffer will be written
 * @buffer_length: message size will be read
 *
 * Return: always 0
 */
static int mei_me_read_slots(struct mei_device *dev, unsigned char *buffer,
			     unsigned long buffer_length)
{
	u32 *reg_buf = (u32 *)buffer;

	for (; buffer_length >= MEI_SLOT_SIZE; buffer_length -= MEI_SLOT_SIZE)
		*reg_buf++ = mei_me_mecbrw_read(dev);

	if (buffer_length > 0) {
		u32 reg = mei_me_mecbrw_read(dev);

		memcpy(reg_buf, &reg, buffer_length);
	}

	mei_hcsr_set_hig(dev);
	return 0;
}

/**
 * mei_me_pg_set - write pg enter register
 *
 * @dev: the device structure
 */
static void mei_me_pg_set(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 reg;

	reg = mei_me_reg_read(hw, H_HPG_CSR);
	trace_mei_reg_read(dev->dev, "H_HPG_CSR", H_HPG_CSR, reg);

	reg |= H_HPG_CSR_PGI;

	trace_mei_reg_write(dev->dev, "H_HPG_CSR", H_HPG_CSR, reg);
	mei_me_reg_write(hw, H_HPG_CSR, reg);
}

/**
 * mei_me_pg_unset - write pg exit register
 *
 * @dev: the device structure
 */
static void mei_me_pg_unset(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 reg;

	reg = mei_me_reg_read(hw, H_HPG_CSR);
	trace_mei_reg_read(dev->dev, "H_HPG_CSR", H_HPG_CSR, reg);

	WARN(!(reg & H_HPG_CSR_PGI), "PGI is not set\n");

	reg |= H_HPG_CSR_PGIHEXR;

	trace_mei_reg_write(dev->dev, "H_HPG_CSR", H_HPG_CSR, reg);
	mei_me_reg_write(hw, H_HPG_CSR, reg);
}

/**
 * mei_me_pg_legacy_enter_sync - perform legacy pg entry procedure
 *
 * @dev: the device structure
 *
 * Return: 0 on success an error code otherwise
 */
static int mei_me_pg_legacy_enter_sync(struct mei_device *dev)
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
		mei_me_pg_set(dev);
		ret = 0;
	} else {
		ret = -ETIME;
	}

	dev->pg_event = MEI_PG_EVENT_IDLE;
	hw->pg_state = MEI_PG_ON;

	return ret;
}

/**
 * mei_me_pg_legacy_exit_sync - perform legacy pg exit procedure
 *
 * @dev: the device structure
 *
 * Return: 0 on success an error code otherwise
 */
static int mei_me_pg_legacy_exit_sync(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	unsigned long timeout = mei_secs_to_jiffies(MEI_PGI_TIMEOUT);
	int ret;

	if (dev->pg_event == MEI_PG_EVENT_RECEIVED)
		goto reply;

	dev->pg_event = MEI_PG_EVENT_WAIT;

	mei_me_pg_unset(dev);

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(dev->wait_pg,
		dev->pg_event == MEI_PG_EVENT_RECEIVED, timeout);
	mutex_lock(&dev->device_lock);

reply:
	if (dev->pg_event != MEI_PG_EVENT_RECEIVED) {
		ret = -ETIME;
		goto out;
	}

	dev->pg_event = MEI_PG_EVENT_INTR_WAIT;
	ret = mei_hbm_pg(dev, MEI_PG_ISOLATION_EXIT_RES_CMD);
	if (ret)
		return ret;

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(dev->wait_pg,
		dev->pg_event == MEI_PG_EVENT_INTR_RECEIVED, timeout);
	mutex_lock(&dev->device_lock);

	if (dev->pg_event == MEI_PG_EVENT_INTR_RECEIVED)
		ret = 0;
	else
		ret = -ETIME;

out:
	dev->pg_event = MEI_PG_EVENT_IDLE;
	hw->pg_state = MEI_PG_OFF;

	return ret;
}

/**
 * mei_me_pg_in_transition - is device now in pg transition
 *
 * @dev: the device structure
 *
 * Return: true if in pg transition, false otherwise
 */
static bool mei_me_pg_in_transition(struct mei_device *dev)
{
	return dev->pg_event >= MEI_PG_EVENT_WAIT &&
	       dev->pg_event <= MEI_PG_EVENT_INTR_WAIT;
}

/**
 * mei_me_pg_is_enabled - detect if PG is supported by HW
 *
 * @dev: the device structure
 *
 * Return: true is pg supported, false otherwise
 */
static bool mei_me_pg_is_enabled(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 reg = mei_me_mecsr_read(dev);

	if (hw->d0i3_supported)
		return true;

	if ((reg & ME_PGIC_HRA) == 0)
		goto notsupported;

	if (!dev->hbm_f_pg_supported)
		goto notsupported;

	return true;

notsupported:
	dev_dbg(dev->dev, "pg: not supported: d0i3 = %d HGP = %d hbm version %d.%d ?= %d.%d\n",
		hw->d0i3_supported,
		!!(reg & ME_PGIC_HRA),
		dev->version.major_version,
		dev->version.minor_version,
		HBM_MAJOR_VERSION_PGI,
		HBM_MINOR_VERSION_PGI);

	return false;
}

/**
 * mei_me_d0i3_set - write d0i3 register bit on mei device.
 *
 * @dev: the device structure
 * @intr: ask for interrupt
 *
 * Return: D0I3C register value
 */
static u32 mei_me_d0i3_set(struct mei_device *dev, bool intr)
{
	u32 reg = mei_me_d0i3c_read(dev);

	reg |= H_D0I3C_I3;
	if (intr)
		reg |= H_D0I3C_IR;
	else
		reg &= ~H_D0I3C_IR;
	mei_me_d0i3c_write(dev, reg);
	/* read it to ensure HW consistency */
	reg = mei_me_d0i3c_read(dev);
	return reg;
}

/**
 * mei_me_d0i3_unset - clean d0i3 register bit on mei device.
 *
 * @dev: the device structure
 *
 * Return: D0I3C register value
 */
static u32 mei_me_d0i3_unset(struct mei_device *dev)
{
	u32 reg = mei_me_d0i3c_read(dev);

	reg &= ~H_D0I3C_I3;
	reg |= H_D0I3C_IR;
	mei_me_d0i3c_write(dev, reg);
	/* read it to ensure HW consistency */
	reg = mei_me_d0i3c_read(dev);
	return reg;
}

/**
 * mei_me_d0i3_enter_sync - perform d0i3 entry procedure
 *
 * @dev: the device structure
 *
 * Return: 0 on success an error code otherwise
 */
static int mei_me_d0i3_enter_sync(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	unsigned long d0i3_timeout = mei_secs_to_jiffies(MEI_D0I3_TIMEOUT);
	unsigned long pgi_timeout = mei_secs_to_jiffies(MEI_PGI_TIMEOUT);
	int ret;
	u32 reg;

	reg = mei_me_d0i3c_read(dev);
	if (reg & H_D0I3C_I3) {
		/* we are in d0i3, nothing to do */
		dev_dbg(dev->dev, "d0i3 set not needed\n");
		ret = 0;
		goto on;
	}

	/* PGI entry procedure */
	dev->pg_event = MEI_PG_EVENT_WAIT;

	ret = mei_hbm_pg(dev, MEI_PG_ISOLATION_ENTRY_REQ_CMD);
	if (ret)
		/* FIXME: should we reset here? */
		goto out;

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(dev->wait_pg,
		dev->pg_event == MEI_PG_EVENT_RECEIVED, pgi_timeout);
	mutex_lock(&dev->device_lock);

	if (dev->pg_event != MEI_PG_EVENT_RECEIVED) {
		ret = -ETIME;
		goto out;
	}
	/* end PGI entry procedure */

	dev->pg_event = MEI_PG_EVENT_INTR_WAIT;

	reg = mei_me_d0i3_set(dev, true);
	if (!(reg & H_D0I3C_CIP)) {
		dev_dbg(dev->dev, "d0i3 enter wait not needed\n");
		ret = 0;
		goto on;
	}

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(dev->wait_pg,
		dev->pg_event == MEI_PG_EVENT_INTR_RECEIVED, d0i3_timeout);
	mutex_lock(&dev->device_lock);

	if (dev->pg_event != MEI_PG_EVENT_INTR_RECEIVED) {
		reg = mei_me_d0i3c_read(dev);
		if (!(reg & H_D0I3C_I3)) {
			ret = -ETIME;
			goto out;
		}
	}

	ret = 0;
on:
	hw->pg_state = MEI_PG_ON;
out:
	dev->pg_event = MEI_PG_EVENT_IDLE;
	dev_dbg(dev->dev, "d0i3 enter ret = %d\n", ret);
	return ret;
}

/**
 * mei_me_d0i3_enter - perform d0i3 entry procedure
 *   no hbm PG handshake
 *   no waiting for confirmation; runs with interrupts
 *   disabled
 *
 * @dev: the device structure
 *
 * Return: 0 on success an error code otherwise
 */
static int mei_me_d0i3_enter(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	u32 reg;

	reg = mei_me_d0i3c_read(dev);
	if (reg & H_D0I3C_I3) {
		/* we are in d0i3, nothing to do */
		dev_dbg(dev->dev, "already d0i3 : set not needed\n");
		goto on;
	}

	mei_me_d0i3_set(dev, false);
on:
	hw->pg_state = MEI_PG_ON;
	dev->pg_event = MEI_PG_EVENT_IDLE;
	dev_dbg(dev->dev, "d0i3 enter\n");
	return 0;
}

/**
 * mei_me_d0i3_exit_sync - perform d0i3 exit procedure
 *
 * @dev: the device structure
 *
 * Return: 0 on success an error code otherwise
 */
static int mei_me_d0i3_exit_sync(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	unsigned long timeout = mei_secs_to_jiffies(MEI_D0I3_TIMEOUT);
	int ret;
	u32 reg;

	dev->pg_event = MEI_PG_EVENT_INTR_WAIT;

	reg = mei_me_d0i3c_read(dev);
	if (!(reg & H_D0I3C_I3)) {
		/* we are not in d0i3, nothing to do */
		dev_dbg(dev->dev, "d0i3 exit not needed\n");
		ret = 0;
		goto off;
	}

	reg = mei_me_d0i3_unset(dev);
	if (!(reg & H_D0I3C_CIP)) {
		dev_dbg(dev->dev, "d0i3 exit wait not needed\n");
		ret = 0;
		goto off;
	}

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(dev->wait_pg,
		dev->pg_event == MEI_PG_EVENT_INTR_RECEIVED, timeout);
	mutex_lock(&dev->device_lock);

	if (dev->pg_event != MEI_PG_EVENT_INTR_RECEIVED) {
		reg = mei_me_d0i3c_read(dev);
		if (reg & H_D0I3C_I3) {
			ret = -ETIME;
			goto out;
		}
	}

	ret = 0;
off:
	hw->pg_state = MEI_PG_OFF;
out:
	dev->pg_event = MEI_PG_EVENT_IDLE;

	dev_dbg(dev->dev, "d0i3 exit ret = %d\n", ret);
	return ret;
}

/**
 * mei_me_pg_legacy_intr - perform legacy pg processing
 *			   in interrupt thread handler
 *
 * @dev: the device structure
 */
static void mei_me_pg_legacy_intr(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);

	if (dev->pg_event != MEI_PG_EVENT_INTR_WAIT)
		return;

	dev->pg_event = MEI_PG_EVENT_INTR_RECEIVED;
	hw->pg_state = MEI_PG_OFF;
	if (waitqueue_active(&dev->wait_pg))
		wake_up(&dev->wait_pg);
}

/**
 * mei_me_d0i3_intr - perform d0i3 processing in interrupt thread handler
 *
 * @dev: the device structure
 * @intr_source: interrupt source
 */
static void mei_me_d0i3_intr(struct mei_device *dev, u32 intr_source)
{
	struct mei_me_hw *hw = to_me_hw(dev);

	if (dev->pg_event == MEI_PG_EVENT_INTR_WAIT &&
	    (intr_source & H_D0I3C_IS)) {
		dev->pg_event = MEI_PG_EVENT_INTR_RECEIVED;
		if (hw->pg_state == MEI_PG_ON) {
			hw->pg_state = MEI_PG_OFF;
			if (dev->hbm_state != MEI_HBM_IDLE) {
				/*
				 * force H_RDY because it could be
				 * wiped off during PG
				 */
				dev_dbg(dev->dev, "d0i3 set host ready\n");
				mei_me_host_set_ready(dev);
			}
		} else {
			hw->pg_state = MEI_PG_ON;
		}

		wake_up(&dev->wait_pg);
	}

	if (hw->pg_state == MEI_PG_ON && (intr_source & H_IS)) {
		/*
		 * HW sent some data and we are in D0i3, so
		 * we got here because of HW initiated exit from D0i3.
		 * Start runtime pm resume sequence to exit low power state.
		 */
		dev_dbg(dev->dev, "d0i3 want resume\n");
		mei_hbm_pg_resume(dev);
	}
}

/**
 * mei_me_pg_intr - perform pg processing in interrupt thread handler
 *
 * @dev: the device structure
 * @intr_source: interrupt source
 */
static void mei_me_pg_intr(struct mei_device *dev, u32 intr_source)
{
	struct mei_me_hw *hw = to_me_hw(dev);

	if (hw->d0i3_supported)
		mei_me_d0i3_intr(dev, intr_source);
	else
		mei_me_pg_legacy_intr(dev);
}

/**
 * mei_me_pg_enter_sync - perform runtime pm entry procedure
 *
 * @dev: the device structure
 *
 * Return: 0 on success an error code otherwise
 */
int mei_me_pg_enter_sync(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);

	if (hw->d0i3_supported)
		return mei_me_d0i3_enter_sync(dev);
	else
		return mei_me_pg_legacy_enter_sync(dev);
}

/**
 * mei_me_pg_exit_sync - perform runtime pm exit procedure
 *
 * @dev: the device structure
 *
 * Return: 0 on success an error code otherwise
 */
int mei_me_pg_exit_sync(struct mei_device *dev)
{
	struct mei_me_hw *hw = to_me_hw(dev);

	if (hw->d0i3_supported)
		return mei_me_d0i3_exit_sync(dev);
	else
		return mei_me_pg_legacy_exit_sync(dev);
}

/**
 * mei_me_hw_reset - resets fw via mei csr register.
 *
 * @dev: the device structure
 * @intr_enable: if interrupt should be enabled after reset.
 *
 * Return: 0 on success an error code otherwise
 */
static int mei_me_hw_reset(struct mei_device *dev, bool intr_enable)
{
	struct mei_me_hw *hw = to_me_hw(dev);
	int ret;
	u32 hcsr;

	if (intr_enable) {
		mei_me_intr_enable(dev);
		if (hw->d0i3_supported) {
			ret = mei_me_d0i3_exit_sync(dev);
			if (ret)
				return ret;
		}
	}

	pm_runtime_set_active(dev->dev);

	hcsr = mei_hcsr_read(dev);
	/* H_RST may be found lit before reset is started,
	 * for example if preceding reset flow hasn't completed.
	 * In that case asserting H_RST will be ignored, therefore
	 * we need to clean H_RST bit to start a successful reset sequence.
	 */
	if ((hcsr & H_RST) == H_RST) {
		dev_warn(dev->dev, "H_RST is set = 0x%08X", hcsr);
		hcsr &= ~H_RST;
		mei_hcsr_set(dev, hcsr);
		hcsr = mei_hcsr_read(dev);
	}

	hcsr |= H_RST | H_IG | H_CSR_IS_MASK;

	if (!intr_enable)
		hcsr &= ~H_CSR_IE_MASK;

	dev->recvd_hw_ready = false;
	mei_hcsr_write(dev, hcsr);

	/*
	 * Host reads the H_CSR once to ensure that the
	 * posted write to H_CSR completes.
	 */
	hcsr = mei_hcsr_read(dev);

	if ((hcsr & H_RST) == 0)
		dev_warn(dev->dev, "H_RST is not set = 0x%08X", hcsr);

	if ((hcsr & H_RDY) == H_RDY)
		dev_warn(dev->dev, "H_RDY is not cleared 0x%08X", hcsr);

	if (!intr_enable) {
		mei_me_hw_reset_release(dev);
		if (hw->d0i3_supported) {
			ret = mei_me_d0i3_enter(dev);
			if (ret)
				return ret;
		}
	}
	return 0;
}

/**
 * mei_me_irq_quick_handler - The ISR of the MEI device
 *
 * @irq: The irq number
 * @dev_id: pointer to the device structure
 *
 * Return: irqreturn_t
 */
irqreturn_t mei_me_irq_quick_handler(int irq, void *dev_id)
{
	struct mei_device *dev = (struct mei_device *)dev_id;
	u32 hcsr;

	hcsr = mei_hcsr_read(dev);
	if (!me_intr_src(hcsr))
		return IRQ_NONE;

	dev_dbg(dev->dev, "interrupt source 0x%08X\n", me_intr_src(hcsr));

	/* disable interrupts on device */
	me_intr_disable(dev, hcsr);
	return IRQ_WAKE_THREAD;
}

/**
 * mei_me_irq_thread_handler - function called after ISR to handle the interrupt
 * processing.
 *
 * @irq: The irq number
 * @dev_id: pointer to the device structure
 *
 * Return: irqreturn_t
 *
 */
irqreturn_t mei_me_irq_thread_handler(int irq, void *dev_id)
{
	struct mei_device *dev = (struct mei_device *) dev_id;
	struct list_head cmpl_list;
	s32 slots;
	u32 hcsr;
	int rets = 0;

	dev_dbg(dev->dev, "function called after ISR to handle the interrupt processing.\n");
	/* initialize our complete list */
	mutex_lock(&dev->device_lock);

	hcsr = mei_hcsr_read(dev);
	me_intr_clear(dev, hcsr);

	INIT_LIST_HEAD(&cmpl_list);

	/* check if ME wants a reset */
	if (!mei_hw_is_ready(dev) && dev->dev_state != MEI_DEV_RESETTING) {
		dev_warn(dev->dev, "FW not ready: resetting.\n");
		schedule_work(&dev->reset_work);
		goto end;
	}

	if (mei_me_hw_is_resetting(dev))
		mei_hcsr_set_hig(dev);

	mei_me_pg_intr(dev, me_intr_src(hcsr));

	/*  check if we need to start the dev */
	if (!mei_host_is_ready(dev)) {
		if (mei_hw_is_ready(dev)) {
			dev_dbg(dev->dev, "we need to start the dev.\n");
			dev->recvd_hw_ready = true;
			wake_up(&dev->wait_hw_ready);
		} else {
			dev_dbg(dev->dev, "Spurious Interrupt\n");
		}
		goto end;
	}
	/* check slots available for reading */
	slots = mei_count_full_read_slots(dev);
	while (slots > 0) {
		dev_dbg(dev->dev, "slots to read = %08x\n", slots);
		rets = mei_irq_read_handler(dev, &cmpl_list, &slots);
		/* There is a race between ME write and interrupt delivery:
		 * Not all data is always available immediately after the
		 * interrupt, so try to read again on the next interrupt.
		 */
		if (rets == -ENODATA)
			break;

		if (rets &&
		    (dev->dev_state != MEI_DEV_RESETTING &&
		     dev->dev_state != MEI_DEV_POWER_DOWN)) {
			dev_err(dev->dev, "mei_irq_read_handler ret = %d.\n",
						rets);
			schedule_work(&dev->reset_work);
			goto end;
		}
	}

	dev->hbuf_is_ready = mei_hbuf_is_ready(dev);

	/*
	 * During PG handshake only allowed write is the replay to the
	 * PG exit message, so block calling write function
	 * if the pg event is in PG handshake
	 */
	if (dev->pg_event != MEI_PG_EVENT_WAIT &&
	    dev->pg_event != MEI_PG_EVENT_RECEIVED) {
		rets = mei_irq_write_handler(dev, &cmpl_list);
		dev->hbuf_is_ready = mei_hbuf_is_ready(dev);
	}

	mei_irq_compl_handler(dev, &cmpl_list);

end:
	dev_dbg(dev->dev, "interrupt thread end ret = %d\n", rets);
	mei_me_intr_enable(dev);
	mutex_unlock(&dev->device_lock);
	return IRQ_HANDLED;
}

static const struct mei_hw_ops mei_me_hw_ops = {

	.fw_status = mei_me_fw_status,
	.pg_state  = mei_me_pg_state,

	.host_is_ready = mei_me_host_is_ready,

	.hw_is_ready = mei_me_hw_is_ready,
	.hw_reset = mei_me_hw_reset,
	.hw_config = mei_me_hw_config,
	.hw_start = mei_me_hw_start,

	.pg_in_transition = mei_me_pg_in_transition,
	.pg_is_enabled = mei_me_pg_is_enabled,

	.intr_clear = mei_me_intr_clear,
	.intr_enable = mei_me_intr_enable,
	.intr_disable = mei_me_intr_disable,
	.synchronize_irq = mei_me_synchronize_irq,

	.hbuf_free_slots = mei_me_hbuf_empty_slots,
	.hbuf_is_ready = mei_me_hbuf_is_empty,
	.hbuf_depth = mei_me_hbuf_depth,

	.write = mei_me_hbuf_write,

	.rdbuf_full_slots = mei_me_count_full_read_slots,
	.read_hdr = mei_me_mecbrw_read,
	.read = mei_me_read_slots
};

static bool mei_me_fw_type_nm(struct pci_dev *pdev)
{
	u32 reg;

	pci_read_config_dword(pdev, PCI_CFG_HFS_2, &reg);
	trace_mei_pci_cfg_read(&pdev->dev, "PCI_CFG_HFS_2", PCI_CFG_HFS_2, reg);
	/* make sure that bit 9 (NM) is up and bit 10 (DM) is down */
	return (reg & 0x600) == 0x200;
}

#define MEI_CFG_FW_NM                           \
	.quirk_probe = mei_me_fw_type_nm

static bool mei_me_fw_type_sps(struct pci_dev *pdev)
{
	u32 reg;
	unsigned int devfn;

	/*
	 * Read ME FW Status register to check for SPS Firmware
	 * The SPS FW is only signaled in pci function 0
	 */
	devfn = PCI_DEVFN(PCI_SLOT(pdev->devfn), 0);
	pci_bus_read_config_dword(pdev->bus, devfn, PCI_CFG_HFS_1, &reg);
	trace_mei_pci_cfg_read(&pdev->dev, "PCI_CFG_HFS_1", PCI_CFG_HFS_1, reg);
	/* if bits [19:16] = 15, running SPS Firmware */
	return (reg & 0xf0000) == 0xf0000;
}

#define MEI_CFG_FW_SPS                           \
	.quirk_probe = mei_me_fw_type_sps

#define MEI_CFG_FW_VER_SUPP                     \
	.fw_ver_supported = 1

#define MEI_CFG_ICH_HFS                      \
	.fw_status.count = 0

#define MEI_CFG_ICH10_HFS                        \
	.fw_status.count = 1,                   \
	.fw_status.status[0] = PCI_CFG_HFS_1

#define MEI_CFG_PCH_HFS                         \
	.fw_status.count = 2,                   \
	.fw_status.status[0] = PCI_CFG_HFS_1,   \
	.fw_status.status[1] = PCI_CFG_HFS_2

#define MEI_CFG_PCH8_HFS                        \
	.fw_status.count = 6,                   \
	.fw_status.status[0] = PCI_CFG_HFS_1,   \
	.fw_status.status[1] = PCI_CFG_HFS_2,   \
	.fw_status.status[2] = PCI_CFG_HFS_3,   \
	.fw_status.status[3] = PCI_CFG_HFS_4,   \
	.fw_status.status[4] = PCI_CFG_HFS_5,   \
	.fw_status.status[5] = PCI_CFG_HFS_6

#define MEI_CFG_DMA_128 \
	.dma_size[DMA_DSCR_HOST] = SZ_128K, \
	.dma_size[DMA_DSCR_DEVICE] = SZ_128K, \
	.dma_size[DMA_DSCR_CTRL] = PAGE_SIZE

/* ICH Legacy devices */
static const struct mei_cfg mei_me_ich_cfg = {
	MEI_CFG_ICH_HFS,
};

/* ICH devices */
static const struct mei_cfg mei_me_ich10_cfg = {
	MEI_CFG_ICH10_HFS,
};

/* PCH6 devices */
static const struct mei_cfg mei_me_pch6_cfg = {
	MEI_CFG_PCH_HFS,
};

/* PCH7 devices */
static const struct mei_cfg mei_me_pch7_cfg = {
	MEI_CFG_PCH_HFS,
	MEI_CFG_FW_VER_SUPP,
};

/* PCH Cougar Point and Patsburg with quirk for Node Manager exclusion */
static const struct mei_cfg mei_me_pch_cpt_pbg_cfg = {
	MEI_CFG_PCH_HFS,
	MEI_CFG_FW_VER_SUPP,
	MEI_CFG_FW_NM,
};

/* PCH8 Lynx Point and newer devices */
static const struct mei_cfg mei_me_pch8_cfg = {
	MEI_CFG_PCH8_HFS,
	MEI_CFG_FW_VER_SUPP,
};

/* PCH8 Lynx Point with quirk for SPS Firmware exclusion */
static const struct mei_cfg mei_me_pch8_sps_cfg = {
	MEI_CFG_PCH8_HFS,
	MEI_CFG_FW_VER_SUPP,
	MEI_CFG_FW_SPS,
};

/* Cannon Lake and newer devices */
static const struct mei_cfg mei_me_pch12_cfg = {
	MEI_CFG_PCH8_HFS,
	MEI_CFG_FW_VER_SUPP,
	MEI_CFG_DMA_128,
};

/*
 * mei_cfg_list - A list of platform platform specific configurations.
 * Note: has to be synchronized with  enum mei_cfg_idx.
 */
static const struct mei_cfg *const mei_cfg_list[] = {
	[MEI_ME_UNDEF_CFG] = NULL,
	[MEI_ME_ICH_CFG] = &mei_me_ich_cfg,
	[MEI_ME_ICH10_CFG] = &mei_me_ich10_cfg,
	[MEI_ME_PCH6_CFG] = &mei_me_pch6_cfg,
	[MEI_ME_PCH7_CFG] = &mei_me_pch7_cfg,
	[MEI_ME_PCH_CPT_PBG_CFG] = &mei_me_pch_cpt_pbg_cfg,
	[MEI_ME_PCH8_CFG] = &mei_me_pch8_cfg,
	[MEI_ME_PCH8_SPS_CFG] = &mei_me_pch8_sps_cfg,
	[MEI_ME_PCH12_CFG] = &mei_me_pch12_cfg,
};

const struct mei_cfg *mei_me_get_cfg(kernel_ulong_t idx)
{
	BUILD_BUG_ON(ARRAY_SIZE(mei_cfg_list) != MEI_ME_NUM_CFG);

	if (idx >= MEI_ME_NUM_CFG)
		return NULL;

	return mei_cfg_list[idx];
};

/**
 * mei_me_dev_init - allocates and initializes the mei device structure
 *
 * @pdev: The pci device structure
 * @cfg: per device generation config
 *
 * Return: The mei_device pointer on success, NULL on failure.
 */
struct mei_device *mei_me_dev_init(struct pci_dev *pdev,
				   const struct mei_cfg *cfg)
{
	struct mei_device *dev;
	struct mei_me_hw *hw;
	int i;

	dev = devm_kzalloc(&pdev->dev, sizeof(struct mei_device) +
			   sizeof(struct mei_me_hw), GFP_KERNEL);
	if (!dev)
		return NULL;

	hw = to_me_hw(dev);

	for (i = 0; i < DMA_DSCR_NUM; i++)
		dev->dr_dscr[i].size = cfg->dma_size[i];

	mei_device_init(dev, &pdev->dev, &mei_me_hw_ops);
	hw->cfg = cfg;

	dev->fw_f_fw_ver_supported = cfg->fw_ver_supported;

	return dev;
}

