// SPDX-License-Identifier: GPL-2.0-only
/*
 * H/W layer of ISHTP provider device (ISH)
 *
 * Copyright (c) 2014-2016, Intel Corporation.
 */

#include <linux/devm-helpers.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include "client.h"
#include "hw-ish.h"
#include "hbm.h"

/* For FW reset flow */
static struct work_struct fw_reset_work;
static struct ishtp_device *ishtp_dev;

/**
 * ish_reg_read() - Read register
 * @dev: ISHTP device pointer
 * @offset: Register offset
 *
 * Read 32 bit register at a given offset
 *
 * Return: Read register value
 */
static inline uint32_t ish_reg_read(const struct ishtp_device *dev,
	unsigned long offset)
{
	struct ish_hw *hw = to_ish_hw(dev);

	return readl(hw->mem_addr + offset);
}

/**
 * ish_reg_write() - Write register
 * @dev: ISHTP device pointer
 * @offset: Register offset
 * @value: Value to write
 *
 * Writes 32 bit register at a give offset
 */
static inline void ish_reg_write(struct ishtp_device *dev,
				 unsigned long offset,
				 uint32_t value)
{
	struct ish_hw *hw = to_ish_hw(dev);

	writel(value, hw->mem_addr + offset);
}

/**
 * _ish_read_fw_sts_reg() - Read FW status register
 * @dev: ISHTP device pointer
 *
 * Read FW status register
 *
 * Return: Read register value
 */
static inline uint32_t _ish_read_fw_sts_reg(struct ishtp_device *dev)
{
	return ish_reg_read(dev, IPC_REG_ISH_HOST_FWSTS);
}

/**
 * check_generated_interrupt() - Check if ISH interrupt
 * @dev: ISHTP device pointer
 *
 * Check if an interrupt was generated for ISH
 *
 * Return: Read true or false
 */
static bool check_generated_interrupt(struct ishtp_device *dev)
{
	bool interrupt_generated = true;
	uint32_t pisr_val = 0;

	if (dev->pdev->device == CHV_DEVICE_ID) {
		pisr_val = ish_reg_read(dev, IPC_REG_PISR_CHV_AB);
		interrupt_generated =
			IPC_INT_FROM_ISH_TO_HOST_CHV_AB(pisr_val);
	} else {
		pisr_val = ish_reg_read(dev, IPC_REG_PISR_BXT);
		interrupt_generated = !!pisr_val;
		/* only busy-clear bit is RW, others are RO */
		if (pisr_val)
			ish_reg_write(dev, IPC_REG_PISR_BXT, pisr_val);
	}

	return interrupt_generated;
}

/**
 * ish_is_input_ready() - Check if FW ready for RX
 * @dev: ISHTP device pointer
 *
 * Check if ISH FW is ready for receiving data
 *
 * Return: Read true or false
 */
static bool ish_is_input_ready(struct ishtp_device *dev)
{
	uint32_t doorbell_val;

	doorbell_val = ish_reg_read(dev, IPC_REG_HOST2ISH_DRBL);
	return !IPC_IS_BUSY(doorbell_val);
}

/**
 * set_host_ready() - Indicate host ready
 * @dev: ISHTP device pointer
 *
 * Set host ready indication to FW
 */
static void set_host_ready(struct ishtp_device *dev)
{
	if (dev->pdev->device == CHV_DEVICE_ID) {
		if (dev->pdev->revision == REVISION_ID_CHT_A0 ||
				(dev->pdev->revision & REVISION_ID_SI_MASK) ==
				REVISION_ID_CHT_Ax_SI)
			ish_reg_write(dev, IPC_REG_HOST_COMM, 0x81);
		else if (dev->pdev->revision == REVISION_ID_CHT_B0 ||
				(dev->pdev->revision & REVISION_ID_SI_MASK) ==
				REVISION_ID_CHT_Bx_SI ||
				(dev->pdev->revision & REVISION_ID_SI_MASK) ==
				REVISION_ID_CHT_Kx_SI ||
				(dev->pdev->revision & REVISION_ID_SI_MASK) ==
				REVISION_ID_CHT_Dx_SI) {
			uint32_t host_comm_val;

			host_comm_val = ish_reg_read(dev, IPC_REG_HOST_COMM);
			host_comm_val |= IPC_HOSTCOMM_INT_EN_BIT_CHV_AB | 0x81;
			ish_reg_write(dev, IPC_REG_HOST_COMM, host_comm_val);
		}
	} else {
			uint32_t host_pimr_val;

			host_pimr_val = ish_reg_read(dev, IPC_REG_PIMR_BXT);
			host_pimr_val |= IPC_PIMR_INT_EN_BIT_BXT;
			/*
			 * disable interrupt generated instead of
			 * RX_complete_msg
			 */
			host_pimr_val &= ~IPC_HOST2ISH_BUSYCLEAR_MASK_BIT;

			ish_reg_write(dev, IPC_REG_PIMR_BXT, host_pimr_val);
	}
}

/**
 * ishtp_fw_is_ready() - Check if FW ready
 * @dev: ISHTP device pointer
 *
 * Check if ISH FW is ready
 *
 * Return: Read true or false
 */
static bool ishtp_fw_is_ready(struct ishtp_device *dev)
{
	uint32_t ish_status = _ish_read_fw_sts_reg(dev);

	return IPC_IS_ISH_ILUP(ish_status) &&
		IPC_IS_ISH_ISHTP_READY(ish_status);
}

/**
 * ish_set_host_rdy() - Indicate host ready
 * @dev: ISHTP device pointer
 *
 * Set host ready indication to FW
 */
static void ish_set_host_rdy(struct ishtp_device *dev)
{
	uint32_t host_status = ish_reg_read(dev, IPC_REG_HOST_COMM);

	IPC_SET_HOST_READY(host_status);
	ish_reg_write(dev, IPC_REG_HOST_COMM, host_status);
}

/**
 * ish_clr_host_rdy() - Indicate host not ready
 * @dev: ISHTP device pointer
 *
 * Send host not ready indication to FW
 */
static void ish_clr_host_rdy(struct ishtp_device *dev)
{
	uint32_t host_status = ish_reg_read(dev, IPC_REG_HOST_COMM);

	IPC_CLEAR_HOST_READY(host_status);
	ish_reg_write(dev, IPC_REG_HOST_COMM, host_status);
}

static bool ish_chk_host_rdy(struct ishtp_device *dev)
{
	uint32_t host_status = ish_reg_read(dev, IPC_REG_HOST_COMM);

	return (host_status & IPC_HOSTCOMM_READY_BIT);
}

/**
 * ish_set_host_ready() - reconfig ipc host registers
 * @dev: ishtp device pointer
 *
 * Set host to ready state
 * This API is called in some case:
 *    fw is still on, but ipc is powered down.
 *    such as OOB case.
 *
 * Return: 0 for success else error fault code
 */
void ish_set_host_ready(struct ishtp_device *dev)
{
	if (ish_chk_host_rdy(dev))
		return;

	ish_set_host_rdy(dev);
	set_host_ready(dev);
}

/**
 * _ishtp_read_hdr() - Read message header
 * @dev: ISHTP device pointer
 *
 * Read header of 32bit length
 *
 * Return: Read register value
 */
static uint32_t _ishtp_read_hdr(const struct ishtp_device *dev)
{
	return ish_reg_read(dev, IPC_REG_ISH2HOST_MSG);
}

/**
 * _ishtp_read - Read message
 * @dev: ISHTP device pointer
 * @buffer: message buffer
 * @buffer_length: length of message buffer
 *
 * Read message from FW
 *
 * Return: Always 0
 */
static int _ishtp_read(struct ishtp_device *dev, unsigned char *buffer,
	unsigned long buffer_length)
{
	uint32_t	i;
	uint32_t	*r_buf = (uint32_t *)buffer;
	uint32_t	msg_offs;

	msg_offs = IPC_REG_ISH2HOST_MSG + sizeof(struct ishtp_msg_hdr);
	for (i = 0; i < buffer_length; i += sizeof(uint32_t))
		*r_buf++ = ish_reg_read(dev, msg_offs + i);

	return 0;
}

/**
 * write_ipc_from_queue() - try to write ipc msg from Tx queue to device
 * @dev: ishtp device pointer
 *
 * Check if DRBL is cleared. if it is - write the first IPC msg,  then call
 * the callback function (unless it's NULL)
 *
 * Return: 0 for success else failure code
 */
static int write_ipc_from_queue(struct ishtp_device *dev)
{
	struct wr_msg_ctl_info	*ipc_link;
	unsigned long	length;
	unsigned long	rem;
	unsigned long	flags;
	uint32_t	doorbell_val;
	uint32_t	*r_buf;
	uint32_t	reg_addr;
	int	i;
	void	(*ipc_send_compl)(void *);
	void	*ipc_send_compl_prm;

	if (dev->dev_state == ISHTP_DEV_DISABLED)
		return -EINVAL;

	spin_lock_irqsave(&dev->wr_processing_spinlock, flags);
	if (!ish_is_input_ready(dev)) {
		spin_unlock_irqrestore(&dev->wr_processing_spinlock, flags);
		return -EBUSY;
	}

	/*
	 * if tx send list is empty - return 0;
	 * may happen, as RX_COMPLETE handler doesn't check list emptiness.
	 */
	if (list_empty(&dev->wr_processing_list)) {
		spin_unlock_irqrestore(&dev->wr_processing_spinlock, flags);
		return	0;
	}

	ipc_link = list_first_entry(&dev->wr_processing_list,
				    struct wr_msg_ctl_info, link);
	/* first 4 bytes of the data is the doorbell value (IPC header) */
	length = ipc_link->length - sizeof(uint32_t);
	doorbell_val = *(uint32_t *)ipc_link->inline_data;
	r_buf = (uint32_t *)(ipc_link->inline_data + sizeof(uint32_t));

	/* If sending MNG_SYNC_FW_CLOCK, update clock again */
	if (IPC_HEADER_GET_PROTOCOL(doorbell_val) == IPC_PROTOCOL_MNG &&
		IPC_HEADER_GET_MNG_CMD(doorbell_val) == MNG_SYNC_FW_CLOCK) {
		uint64_t usec_system, usec_utc;
		struct ipc_time_update_msg time_update;
		struct time_sync_format ts_format;

		usec_system = ktime_to_us(ktime_get_boottime());
		usec_utc = ktime_to_us(ktime_get_real());
		ts_format.ts1_source = HOST_SYSTEM_TIME_USEC;
		ts_format.ts2_source = HOST_UTC_TIME_USEC;
		ts_format.reserved = 0;

		time_update.primary_host_time = usec_system;
		time_update.secondary_host_time = usec_utc;
		time_update.sync_info = ts_format;

		memcpy(r_buf, &time_update,
		       sizeof(struct ipc_time_update_msg));
	}

	for (i = 0, reg_addr = IPC_REG_HOST2ISH_MSG; i < length >> 2; i++,
			reg_addr += 4)
		ish_reg_write(dev, reg_addr, r_buf[i]);

	rem = length & 0x3;
	if (rem > 0) {
		uint32_t reg = 0;

		memcpy(&reg, &r_buf[length >> 2], rem);
		ish_reg_write(dev, reg_addr, reg);
	}
	ish_reg_write(dev, IPC_REG_HOST2ISH_DRBL, doorbell_val);

	/* Flush writes to msg registers and doorbell */
	ish_reg_read(dev, IPC_REG_ISH_HOST_FWSTS);

	/* Update IPC counters */
	++dev->ipc_tx_cnt;
	dev->ipc_tx_bytes_cnt += IPC_HEADER_GET_LENGTH(doorbell_val);

	ipc_send_compl = ipc_link->ipc_send_compl;
	ipc_send_compl_prm = ipc_link->ipc_send_compl_prm;
	list_del_init(&ipc_link->link);
	list_add(&ipc_link->link, &dev->wr_free_list);
	spin_unlock_irqrestore(&dev->wr_processing_spinlock, flags);

	/*
	 * callback will be called out of spinlock,
	 * after ipc_link returned to free list
	 */
	if (ipc_send_compl)
		ipc_send_compl(ipc_send_compl_prm);

	return 0;
}

/**
 * write_ipc_to_queue() - write ipc msg to Tx queue
 * @dev: ishtp device instance
 * @ipc_send_compl: Send complete callback
 * @ipc_send_compl_prm:	Parameter to send in complete callback
 * @msg: Pointer to message
 * @length: Length of message
 *
 * Recived msg with IPC (and upper protocol) header  and add it to the device
 *  Tx-to-write list then try to send the first IPC waiting msg
 *  (if DRBL is cleared)
 * This function returns negative value for failure (means free list
 *  is empty, or msg too long) and 0 for success.
 *
 * Return: 0 for success else failure code
 */
static int write_ipc_to_queue(struct ishtp_device *dev,
	void (*ipc_send_compl)(void *), void *ipc_send_compl_prm,
	unsigned char *msg, int length)
{
	struct wr_msg_ctl_info *ipc_link;
	unsigned long flags;

	if (length > IPC_FULL_MSG_SIZE)
		return -EMSGSIZE;

	spin_lock_irqsave(&dev->wr_processing_spinlock, flags);
	if (list_empty(&dev->wr_free_list)) {
		spin_unlock_irqrestore(&dev->wr_processing_spinlock, flags);
		return -ENOMEM;
	}
	ipc_link = list_first_entry(&dev->wr_free_list,
				    struct wr_msg_ctl_info, link);
	list_del_init(&ipc_link->link);

	ipc_link->ipc_send_compl = ipc_send_compl;
	ipc_link->ipc_send_compl_prm = ipc_send_compl_prm;
	ipc_link->length = length;
	memcpy(ipc_link->inline_data, msg, length);

	list_add_tail(&ipc_link->link, &dev->wr_processing_list);
	spin_unlock_irqrestore(&dev->wr_processing_spinlock, flags);

	write_ipc_from_queue(dev);

	return 0;
}

/**
 * ipc_send_mng_msg() - Send management message
 * @dev: ishtp device instance
 * @msg_code: Message code
 * @msg: Pointer to message
 * @size: Length of message
 *
 * Send management message to FW
 *
 * Return: 0 for success else failure code
 */
static int ipc_send_mng_msg(struct ishtp_device *dev, uint32_t msg_code,
	void *msg, size_t size)
{
	unsigned char	ipc_msg[IPC_FULL_MSG_SIZE];
	uint32_t	drbl_val = IPC_BUILD_MNG_MSG(msg_code, size);

	memcpy(ipc_msg, &drbl_val, sizeof(uint32_t));
	memcpy(ipc_msg + sizeof(uint32_t), msg, size);
	return	write_ipc_to_queue(dev, NULL, NULL, ipc_msg,
		sizeof(uint32_t) + size);
}

#define WAIT_FOR_FW_RDY			0x1
#define WAIT_FOR_INPUT_RDY		0x2

/**
 * timed_wait_for_timeout() - wait special event with timeout
 * @dev: ISHTP device pointer
 * @condition: indicate the condition for waiting
 * @timeinc: time slice for every wait cycle, in ms
 * @timeout: time in ms for timeout
 *
 * This function will check special event to be ready in a loop, the loop
 * period is specificd in timeinc. Wait timeout will causes failure.
 *
 * Return: 0 for success else failure code
 */
static int timed_wait_for_timeout(struct ishtp_device *dev, int condition,
				unsigned int timeinc, unsigned int timeout)
{
	bool complete = false;
	int ret;

	do {
		if (condition == WAIT_FOR_FW_RDY) {
			complete = ishtp_fw_is_ready(dev);
		} else if (condition == WAIT_FOR_INPUT_RDY) {
			complete = ish_is_input_ready(dev);
		} else {
			ret = -EINVAL;
			goto out;
		}

		if (!complete) {
			unsigned long left_time;

			left_time = msleep_interruptible(timeinc);
			timeout -= (timeinc - left_time);
		}
	} while (!complete && timeout > 0);

	if (complete)
		ret = 0;
	else
		ret = -EBUSY;

out:
	return ret;
}

#define TIME_SLICE_FOR_FW_RDY_MS		100
#define TIME_SLICE_FOR_INPUT_RDY_MS		100
#define TIMEOUT_FOR_FW_RDY_MS			2000
#define TIMEOUT_FOR_INPUT_RDY_MS		2000

/**
 * ish_fw_reset_handler() - FW reset handler
 * @dev: ishtp device pointer
 *
 * Handle FW reset
 *
 * Return: 0 for success else failure code
 */
static int ish_fw_reset_handler(struct ishtp_device *dev)
{
	uint32_t	reset_id;
	unsigned long	flags;

	/* Read reset ID */
	reset_id = ish_reg_read(dev, IPC_REG_ISH2HOST_MSG) & 0xFFFF;

	/* Clear IPC output queue */
	spin_lock_irqsave(&dev->wr_processing_spinlock, flags);
	list_splice_init(&dev->wr_processing_list, &dev->wr_free_list);
	spin_unlock_irqrestore(&dev->wr_processing_spinlock, flags);

	/* ISHTP notification in IPC_RESET */
	ishtp_reset_handler(dev);

	if (!ish_is_input_ready(dev))
		timed_wait_for_timeout(dev, WAIT_FOR_INPUT_RDY,
			TIME_SLICE_FOR_INPUT_RDY_MS, TIMEOUT_FOR_INPUT_RDY_MS);

	/* ISH FW is dead */
	if (!ish_is_input_ready(dev))
		return	-EPIPE;
	/*
	 * Set HOST2ISH.ILUP. Apparently we need this BEFORE sending
	 * RESET_NOTIFY_ACK - FW will be checking for it
	 */
	ish_set_host_rdy(dev);
	/* Send RESET_NOTIFY_ACK (with reset_id) */
	ipc_send_mng_msg(dev, MNG_RESET_NOTIFY_ACK, &reset_id,
			 sizeof(uint32_t));

	/* Wait for ISH FW'es ILUP and ISHTP_READY */
	timed_wait_for_timeout(dev, WAIT_FOR_FW_RDY,
			TIME_SLICE_FOR_FW_RDY_MS, TIMEOUT_FOR_FW_RDY_MS);
	if (!ishtp_fw_is_ready(dev)) {
		/* ISH FW is dead */
		uint32_t	ish_status;

		ish_status = _ish_read_fw_sts_reg(dev);
		dev_err(dev->devc,
			"[ishtp-ish]: completed reset, ISH is dead (FWSTS = %08X)\n",
			ish_status);
		return -ENODEV;
	}
	return	0;
}

#define TIMEOUT_FOR_HW_RDY_MS			300

/**
 * fw_reset_work_fn() - FW reset worker function
 * @unused: not used
 *
 * Call ish_fw_reset_handler to complete FW reset
 */
static void fw_reset_work_fn(struct work_struct *unused)
{
	int	rv;

	rv = ish_fw_reset_handler(ishtp_dev);
	if (!rv) {
		/* ISH is ILUP & ISHTP-ready. Restart ISHTP */
		msleep_interruptible(TIMEOUT_FOR_HW_RDY_MS);
		ishtp_dev->recvd_hw_ready = 1;
		wake_up_interruptible(&ishtp_dev->wait_hw_ready);

		/* ISHTP notification in IPC_RESET sequence completion */
		ishtp_reset_compl_handler(ishtp_dev);
	} else
		dev_err(ishtp_dev->devc, "[ishtp-ish]: FW reset failed (%d)\n",
			rv);
}

/**
 * _ish_sync_fw_clock() -Sync FW clock with the OS clock
 * @dev: ishtp device pointer
 *
 * Sync FW and OS time
 */
static void _ish_sync_fw_clock(struct ishtp_device *dev)
{
	static unsigned long	prev_sync;
	uint64_t	usec;

	if (prev_sync && time_before(jiffies, prev_sync + 20 * HZ))
		return;

	prev_sync = jiffies;
	usec = ktime_to_us(ktime_get_boottime());
	ipc_send_mng_msg(dev, MNG_SYNC_FW_CLOCK, &usec, sizeof(uint64_t));
}

/**
 * recv_ipc() - Receive and process IPC management messages
 * @dev: ishtp device instance
 * @doorbell_val: doorbell value
 *
 * This function runs in ISR context.
 * NOTE: Any other mng command than reset_notify and reset_notify_ack
 * won't wake BH handler
 */
static void	recv_ipc(struct ishtp_device *dev, uint32_t doorbell_val)
{
	uint32_t	mng_cmd;

	mng_cmd = IPC_HEADER_GET_MNG_CMD(doorbell_val);

	switch (mng_cmd) {
	default:
		break;

	case MNG_RX_CMPL_INDICATION:
		if (dev->suspend_flag) {
			dev->suspend_flag = 0;
			wake_up_interruptible(&dev->suspend_wait);
		}
		if (dev->resume_flag) {
			dev->resume_flag = 0;
			wake_up_interruptible(&dev->resume_wait);
		}

		write_ipc_from_queue(dev);
		break;

	case MNG_RESET_NOTIFY:
		if (!ishtp_dev) {
			ishtp_dev = dev;
		}
		schedule_work(&fw_reset_work);
		break;

	case MNG_RESET_NOTIFY_ACK:
		dev->recvd_hw_ready = 1;
		wake_up_interruptible(&dev->wait_hw_ready);
		break;
	}
}

/**
 * ish_irq_handler() - ISH IRQ handler
 * @irq: irq number
 * @dev_id: ishtp device pointer
 *
 * ISH IRQ handler. If interrupt is generated and is for ISH it will process
 * the interrupt.
 */
irqreturn_t ish_irq_handler(int irq, void *dev_id)
{
	struct ishtp_device	*dev = dev_id;
	uint32_t	doorbell_val;
	bool	interrupt_generated;

	/* Check that it's interrupt from ISH (may be shared) */
	interrupt_generated = check_generated_interrupt(dev);

	if (!interrupt_generated)
		return IRQ_NONE;

	doorbell_val = ish_reg_read(dev, IPC_REG_ISH2HOST_DRBL);
	if (!IPC_IS_BUSY(doorbell_val))
		return IRQ_HANDLED;

	if (dev->dev_state == ISHTP_DEV_DISABLED)
		return	IRQ_HANDLED;

	/* Sanity check: IPC dgram length in header */
	if (IPC_HEADER_GET_LENGTH(doorbell_val) > IPC_PAYLOAD_SIZE) {
		dev_err(dev->devc,
			"IPC hdr - bad length: %u; dropped\n",
			(unsigned int)IPC_HEADER_GET_LENGTH(doorbell_val));
		goto	eoi;
	}

	switch (IPC_HEADER_GET_PROTOCOL(doorbell_val)) {
	default:
		break;
	case IPC_PROTOCOL_MNG:
		recv_ipc(dev, doorbell_val);
		break;
	case IPC_PROTOCOL_ISHTP:
		ishtp_recv(dev);
		break;
	}

eoi:
	/* Update IPC counters */
	++dev->ipc_rx_cnt;
	dev->ipc_rx_bytes_cnt += IPC_HEADER_GET_LENGTH(doorbell_val);

	ish_reg_write(dev, IPC_REG_ISH2HOST_DRBL, 0);
	/* Flush write to doorbell */
	ish_reg_read(dev, IPC_REG_ISH_HOST_FWSTS);

	return	IRQ_HANDLED;
}

/**
 * ish_disable_dma() - disable dma communication between host and ISHFW
 * @dev: ishtp device pointer
 *
 * Clear the dma enable bit and wait for dma inactive.
 *
 * Return: 0 for success else error code.
 */
int ish_disable_dma(struct ishtp_device *dev)
{
	unsigned int	dma_delay;

	/* Clear the dma enable bit */
	ish_reg_write(dev, IPC_REG_ISH_RMP2, 0);

	/* wait for dma inactive */
	for (dma_delay = 0; dma_delay < MAX_DMA_DELAY &&
		_ish_read_fw_sts_reg(dev) & (IPC_ISH_IN_DMA);
		dma_delay += 5)
		mdelay(5);

	if (dma_delay >= MAX_DMA_DELAY) {
		dev_err(dev->devc,
			"Wait for DMA inactive timeout\n");
		return	-EBUSY;
	}

	return 0;
}

/**
 * ish_wakeup() - wakeup ishfw from waiting-for-host state
 * @dev: ishtp device pointer
 *
 * Set the dma enable bit and send a void message to FW,
 * it wil wakeup FW from waiting-for-host state.
 */
static void ish_wakeup(struct ishtp_device *dev)
{
	/* Set dma enable bit */
	ish_reg_write(dev, IPC_REG_ISH_RMP2, IPC_RMP2_DMA_ENABLED);

	/*
	 * Send 0 IPC message so that ISH FW wakes up if it was already
	 * asleep.
	 */
	ish_reg_write(dev, IPC_REG_HOST2ISH_DRBL, IPC_DRBL_BUSY_BIT);

	/* Flush writes to doorbell and REMAP2 */
	ish_reg_read(dev, IPC_REG_ISH_HOST_FWSTS);
}

/**
 * _ish_hw_reset() - HW reset
 * @dev: ishtp device pointer
 *
 * Reset ISH HW to recover if any error
 *
 * Return: 0 for success else error fault code
 */
static int _ish_hw_reset(struct ishtp_device *dev)
{
	struct pci_dev *pdev = dev->pdev;
	int	rv;
	uint16_t csr;

	if (!pdev)
		return	-ENODEV;

	rv = pci_reset_function(pdev);
	if (!rv)
		dev->dev_state = ISHTP_DEV_RESETTING;

	if (!pdev->pm_cap) {
		dev_err(&pdev->dev, "Can't reset - no PM caps\n");
		return	-EINVAL;
	}

	/* Disable dma communication between FW and host */
	if (ish_disable_dma(dev)) {
		dev_err(&pdev->dev,
			"Can't reset - stuck with DMA in-progress\n");
		return	-EBUSY;
	}

	pci_read_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, &csr);

	csr &= ~PCI_PM_CTRL_STATE_MASK;
	csr |= PCI_D3hot;
	pci_write_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, csr);

	mdelay(pdev->d3hot_delay);

	csr &= ~PCI_PM_CTRL_STATE_MASK;
	csr |= PCI_D0;
	pci_write_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, csr);

	/* Now we can enable ISH DMA operation and wakeup ISHFW */
	ish_wakeup(dev);

	return	0;
}

/**
 * _ish_ipc_reset() - IPC reset
 * @dev: ishtp device pointer
 *
 * Resets host and fw IPC and upper layers
 *
 * Return: 0 for success else error fault code
 */
static int _ish_ipc_reset(struct ishtp_device *dev)
{
	struct ipc_rst_payload_type ipc_mng_msg;
	int	rv = 0;

	ipc_mng_msg.reset_id = 1;
	ipc_mng_msg.reserved = 0;

	set_host_ready(dev);

	/* Clear the incoming doorbell */
	ish_reg_write(dev, IPC_REG_ISH2HOST_DRBL, 0);
	/* Flush write to doorbell */
	ish_reg_read(dev, IPC_REG_ISH_HOST_FWSTS);

	dev->recvd_hw_ready = 0;

	/* send message */
	rv = ipc_send_mng_msg(dev, MNG_RESET_NOTIFY, &ipc_mng_msg,
		sizeof(struct ipc_rst_payload_type));
	if (rv) {
		dev_err(dev->devc, "Failed to send IPC MNG_RESET_NOTIFY\n");
		return	rv;
	}

	wait_event_interruptible_timeout(dev->wait_hw_ready,
					 dev->recvd_hw_ready, 2 * HZ);
	if (!dev->recvd_hw_ready) {
		dev_err(dev->devc, "Timed out waiting for HW ready\n");
		rv = -ENODEV;
	}

	return rv;
}

/**
 * ish_hw_start() -Start ISH HW
 * @dev: ishtp device pointer
 *
 * Set host to ready state and wait for FW reset
 *
 * Return: 0 for success else error fault code
 */
int ish_hw_start(struct ishtp_device *dev)
{
	ish_set_host_rdy(dev);

	set_host_ready(dev);

	/* After that we can enable ISH DMA operation and wakeup ISHFW */
	ish_wakeup(dev);

	/* wait for FW-initiated reset flow */
	if (!dev->recvd_hw_ready)
		wait_event_interruptible_timeout(dev->wait_hw_ready,
						 dev->recvd_hw_ready,
						 10 * HZ);

	if (!dev->recvd_hw_ready) {
		dev_err(dev->devc,
			"[ishtp-ish]: Timed out waiting for FW-initiated reset\n");
		return	-ENODEV;
	}

	return 0;
}

/**
 * ish_ipc_get_header() -Get doorbell value
 * @dev: ishtp device pointer
 * @length: length of message
 * @busy: busy status
 *
 * Get door bell value from message header
 *
 * Return: door bell value
 */
static uint32_t ish_ipc_get_header(struct ishtp_device *dev, int length,
				   int busy)
{
	uint32_t drbl_val;

	drbl_val = IPC_BUILD_HEADER(length, IPC_PROTOCOL_ISHTP, busy);

	return drbl_val;
}

/**
 * _dma_no_cache_snooping()
 *
 * Check on current platform, DMA supports cache snooping or not.
 * This callback is used to notify uplayer driver if manully cache
 * flush is needed when do DMA operation.
 *
 * Please pay attention to this callback implementation, if declare
 * having cache snooping on a cache snooping not supported platform
 * will cause uplayer driver receiving mismatched data; and if
 * declare no cache snooping on a cache snooping supported platform
 * will cause cache be flushed twice and performance hit.
 *
 * @dev: ishtp device pointer
 *
 * Return: false - has cache snooping capability
 *         true - no cache snooping, need manually cache flush
 */
static bool _dma_no_cache_snooping(struct ishtp_device *dev)
{
	return (dev->pdev->device == EHL_Ax_DEVICE_ID ||
		dev->pdev->device == TGL_LP_DEVICE_ID ||
		dev->pdev->device == TGL_H_DEVICE_ID ||
		dev->pdev->device == ADL_S_DEVICE_ID ||
		dev->pdev->device == ADL_P_DEVICE_ID);
}

static const struct ishtp_hw_ops ish_hw_ops = {
	.hw_reset = _ish_hw_reset,
	.ipc_reset = _ish_ipc_reset,
	.ipc_get_header = ish_ipc_get_header,
	.ishtp_read = _ishtp_read,
	.write = write_ipc_to_queue,
	.get_fw_status = _ish_read_fw_sts_reg,
	.sync_fw_clock = _ish_sync_fw_clock,
	.ishtp_read_hdr = _ishtp_read_hdr,
	.dma_no_cache_snooping = _dma_no_cache_snooping
};

/**
 * ish_dev_init() -Initialize ISH devoce
 * @pdev: PCI device
 *
 * Allocate ISHTP device and initialize IPC processing
 *
 * Return: ISHTP device instance on success else NULL
 */
struct ishtp_device *ish_dev_init(struct pci_dev *pdev)
{
	struct ishtp_device *dev;
	int	i;
	int	ret;

	dev = devm_kzalloc(&pdev->dev,
			   sizeof(struct ishtp_device) + sizeof(struct ish_hw),
			   GFP_KERNEL);
	if (!dev)
		return NULL;

	ishtp_device_init(dev);

	init_waitqueue_head(&dev->wait_hw_ready);

	spin_lock_init(&dev->wr_processing_spinlock);

	/* Init IPC processing and free lists */
	INIT_LIST_HEAD(&dev->wr_processing_list);
	INIT_LIST_HEAD(&dev->wr_free_list);
	for (i = 0; i < IPC_TX_FIFO_SIZE; i++) {
		struct wr_msg_ctl_info	*tx_buf;

		tx_buf = devm_kzalloc(&pdev->dev,
				      sizeof(struct wr_msg_ctl_info),
				      GFP_KERNEL);
		if (!tx_buf) {
			/*
			 * IPC buffers may be limited or not available
			 * at all - although this shouldn't happen
			 */
			dev_err(dev->devc,
				"[ishtp-ish]: failure in Tx FIFO allocations (%d)\n",
				i);
			break;
		}
		list_add_tail(&tx_buf->link, &dev->wr_free_list);
	}

	ret = devm_work_autocancel(&pdev->dev, &fw_reset_work, fw_reset_work_fn);
	if (ret) {
		dev_err(dev->devc, "Failed to initialise FW reset work\n");
		return NULL;
	}

	dev->ops = &ish_hw_ops;
	dev->devc = &pdev->dev;
	dev->mtu = IPC_PAYLOAD_SIZE - sizeof(struct ishtp_msg_hdr);
	return dev;
}

/**
 * ish_device_disable() - Disable ISH device
 * @dev: ISHTP device pointer
 *
 * Disable ISH by clearing host ready to inform firmware.
 */
void	ish_device_disable(struct ishtp_device *dev)
{
	struct pci_dev *pdev = dev->pdev;

	if (!pdev)
		return;

	/* Disable dma communication between FW and host */
	if (ish_disable_dma(dev)) {
		dev_err(&pdev->dev,
			"Can't reset - stuck with DMA in-progress\n");
		return;
	}

	/* Put ISH to D3hot state for power saving */
	pci_set_power_state(pdev, PCI_D3hot);

	dev->dev_state = ISHTP_DEV_DISABLED;
	ish_clr_host_rdy(dev);
}
