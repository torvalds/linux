// SPDX-License-Identifier: GPL-2.0
/*
 *    driver for Microchip PQI-based storage controllers
 *    Copyright (c) 2019-2021 Microchip Technology Inc. and its subsidiaries
 *    Copyright (c) 2016-2018 Microsemi Corporation
 *    Copyright (c) 2016 PMC-Sierra, Inc.
 *
 *    Questions/Comments/Bugfixes to storagedev@microchip.com
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <scsi/scsi_device.h>
#include <asm/unaligned.h>
#include "smartpqi.h"
#include "smartpqi_sis.h"

/* legacy SIS interface commands */
#define SIS_CMD_GET_ADAPTER_PROPERTIES		0x19
#define SIS_CMD_INIT_BASE_STRUCT_ADDRESS	0x1b
#define SIS_CMD_GET_PQI_CAPABILITIES		0x3000

/* for submission of legacy SIS commands */
#define SIS_REENABLE_SIS_MODE			0x1
#define SIS_ENABLE_MSIX				0x40
#define SIS_ENABLE_INTX				0x80
#define SIS_SOFT_RESET				0x100
#define SIS_CMD_READY				0x200
#define SIS_TRIGGER_SHUTDOWN			0x800000
#define SIS_PQI_RESET_QUIESCE			0x1000000

#define SIS_CMD_COMPLETE			0x1000
#define SIS_CLEAR_CTRL_TO_HOST_DOORBELL		0x1000

#define SIS_CMD_STATUS_SUCCESS			0x1
#define SIS_CMD_COMPLETE_TIMEOUT_SECS		30
#define SIS_CMD_COMPLETE_POLL_INTERVAL_MSECS	10

/* used with SIS_CMD_GET_ADAPTER_PROPERTIES command */
#define SIS_EXTENDED_PROPERTIES_SUPPORTED	0x800000
#define SIS_SMARTARRAY_FEATURES_SUPPORTED	0x2
#define SIS_PQI_MODE_SUPPORTED			0x4
#define SIS_PQI_RESET_QUIESCE_SUPPORTED		0x8
#define SIS_REQUIRED_EXTENDED_PROPERTIES	\
	(SIS_SMARTARRAY_FEATURES_SUPPORTED | SIS_PQI_MODE_SUPPORTED)

/* used with SIS_CMD_INIT_BASE_STRUCT_ADDRESS command */
#define SIS_BASE_STRUCT_REVISION		9
#define SIS_BASE_STRUCT_ALIGNMENT		16

#define SIS_CTRL_KERNEL_FW_TRIAGE		0x3
#define SIS_CTRL_KERNEL_UP			0x80
#define SIS_CTRL_KERNEL_PANIC			0x100
#define SIS_CTRL_READY_TIMEOUT_SECS		180
#define SIS_CTRL_READY_RESUME_TIMEOUT_SECS	90
#define SIS_CTRL_READY_POLL_INTERVAL_MSECS	10

enum sis_fw_triage_status {
	FW_TRIAGE_NOT_STARTED = 0,
	FW_TRIAGE_STARTED,
	FW_TRIAGE_COND_INVALID,
	FW_TRIAGE_COMPLETED
};

#pragma pack(1)

/* for use with SIS_CMD_INIT_BASE_STRUCT_ADDRESS command */
struct sis_base_struct {
	__le32	revision;		/* revision of this structure */
	__le32	flags;			/* reserved */
	__le32	error_buffer_paddr_low;	/* lower 32 bits of physical memory */
					/* buffer for PQI error response */
					/* data */
	__le32	error_buffer_paddr_high;	/* upper 32 bits of physical */
						/* memory buffer for PQI */
						/* error response data */
	__le32	error_buffer_element_length;	/* length of each PQI error */
						/* response buffer element */
						/* in bytes */
	__le32	error_buffer_num_elements;	/* total number of PQI error */
						/* response buffers available */
};

#pragma pack()

static int sis_wait_for_ctrl_ready_with_timeout(struct pqi_ctrl_info *ctrl_info,
	unsigned int timeout_secs)
{
	unsigned long timeout;
	u32 status;

	timeout = (timeout_secs * HZ) + jiffies;

	while (1) {
		status = readl(&ctrl_info->registers->sis_firmware_status);
		if (status != ~0) {
			if (status & SIS_CTRL_KERNEL_PANIC) {
				dev_err(&ctrl_info->pci_dev->dev,
					"controller is offline: status code 0x%x\n",
					readl(
					&ctrl_info->registers->sis_mailbox[7]));
				return -ENODEV;
			}
			if (status & SIS_CTRL_KERNEL_UP)
				break;
		}
		if (time_after(jiffies, timeout)) {
			dev_err(&ctrl_info->pci_dev->dev,
				"controller not ready after %u seconds\n",
				timeout_secs);
			return -ETIMEDOUT;
		}
		msleep(SIS_CTRL_READY_POLL_INTERVAL_MSECS);
	}

	return 0;
}

int sis_wait_for_ctrl_ready(struct pqi_ctrl_info *ctrl_info)
{
	return sis_wait_for_ctrl_ready_with_timeout(ctrl_info,
		SIS_CTRL_READY_TIMEOUT_SECS);
}

int sis_wait_for_ctrl_ready_resume(struct pqi_ctrl_info *ctrl_info)
{
	return sis_wait_for_ctrl_ready_with_timeout(ctrl_info,
		SIS_CTRL_READY_RESUME_TIMEOUT_SECS);
}

bool sis_is_firmware_running(struct pqi_ctrl_info *ctrl_info)
{
	bool running;
	u32 status;

	status = readl(&ctrl_info->registers->sis_firmware_status);

	if (status & SIS_CTRL_KERNEL_PANIC)
		running = false;
	else
		running = true;

	if (!running)
		dev_err(&ctrl_info->pci_dev->dev,
			"controller is offline: status code 0x%x\n",
			readl(&ctrl_info->registers->sis_mailbox[7]));

	return running;
}

bool sis_is_kernel_up(struct pqi_ctrl_info *ctrl_info)
{
	return readl(&ctrl_info->registers->sis_firmware_status) &
		SIS_CTRL_KERNEL_UP;
}

u32 sis_get_product_id(struct pqi_ctrl_info *ctrl_info)
{
	return readl(&ctrl_info->registers->sis_product_identifier);
}

/* used for passing command parameters/results when issuing SIS commands */
struct sis_sync_cmd_params {
	u32	mailbox[6];	/* mailboxes 0-5 */
};

static int sis_send_sync_cmd(struct pqi_ctrl_info *ctrl_info,
	u32 cmd, struct sis_sync_cmd_params *params)
{
	struct pqi_ctrl_registers __iomem *registers;
	unsigned int i;
	unsigned long timeout;
	u32 doorbell;
	u32 cmd_status;

	registers = ctrl_info->registers;

	/* Write the command to mailbox 0. */
	writel(cmd, &registers->sis_mailbox[0]);

	/*
	 * Write the command parameters to mailboxes 1-4 (mailbox 5 is not used
	 * when sending a command to the controller).
	 */
	for (i = 1; i <= 4; i++)
		writel(params->mailbox[i], &registers->sis_mailbox[i]);

	/* Clear the command doorbell. */
	writel(SIS_CLEAR_CTRL_TO_HOST_DOORBELL,
		&registers->sis_ctrl_to_host_doorbell_clear);

	/* Disable doorbell interrupts by masking all interrupts. */
	writel(~0, &registers->sis_interrupt_mask);

	/*
	 * Force the completion of the interrupt mask register write before
	 * submitting the command.
	 */
	readl(&registers->sis_interrupt_mask);

	/* Submit the command to the controller. */
	writel(SIS_CMD_READY, &registers->sis_host_to_ctrl_doorbell);

	/*
	 * Poll for command completion.  Note that the call to msleep() is at
	 * the top of the loop in order to give the controller time to start
	 * processing the command before we start polling.
	 */
	timeout = (SIS_CMD_COMPLETE_TIMEOUT_SECS * HZ) + jiffies;
	while (1) {
		msleep(SIS_CMD_COMPLETE_POLL_INTERVAL_MSECS);
		doorbell = readl(&registers->sis_ctrl_to_host_doorbell);
		if (doorbell & SIS_CMD_COMPLETE)
			break;
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
	}

	/* Read the command status from mailbox 0. */
	cmd_status = readl(&registers->sis_mailbox[0]);
	if (cmd_status != SIS_CMD_STATUS_SUCCESS) {
		dev_err(&ctrl_info->pci_dev->dev,
			"SIS command failed for command 0x%x: status = 0x%x\n",
			cmd, cmd_status);
		return -EINVAL;
	}

	/*
	 * The command completed successfully, so save the command status and
	 * read the values returned in mailboxes 1-5.
	 */
	params->mailbox[0] = cmd_status;
	for (i = 1; i < ARRAY_SIZE(params->mailbox); i++)
		params->mailbox[i] = readl(&registers->sis_mailbox[i]);

	return 0;
}

/*
 * This function verifies that we are talking to a controller that speaks PQI.
 */

int sis_get_ctrl_properties(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	u32 properties;
	u32 extended_properties;
	struct sis_sync_cmd_params params;

	memset(&params, 0, sizeof(params));

	rc = sis_send_sync_cmd(ctrl_info, SIS_CMD_GET_ADAPTER_PROPERTIES,
		&params);
	if (rc)
		return rc;

	properties = params.mailbox[1];

	if (!(properties & SIS_EXTENDED_PROPERTIES_SUPPORTED))
		return -ENODEV;

	extended_properties = params.mailbox[4];

	if ((extended_properties & SIS_REQUIRED_EXTENDED_PROPERTIES) !=
		SIS_REQUIRED_EXTENDED_PROPERTIES)
		return -ENODEV;

	if (extended_properties & SIS_PQI_RESET_QUIESCE_SUPPORTED)
		ctrl_info->pqi_reset_quiesce_supported = true;

	return 0;
}

int sis_get_pqi_capabilities(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	struct sis_sync_cmd_params params;

	memset(&params, 0, sizeof(params));

	rc = sis_send_sync_cmd(ctrl_info, SIS_CMD_GET_PQI_CAPABILITIES,
		&params);
	if (rc)
		return rc;

	ctrl_info->max_sg_entries = params.mailbox[1];
	ctrl_info->max_transfer_size = params.mailbox[2];
	ctrl_info->max_outstanding_requests = params.mailbox[3];
	ctrl_info->config_table_offset = params.mailbox[4];
	ctrl_info->config_table_length = params.mailbox[5];

	return 0;
}

int sis_init_base_struct_addr(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	void *base_struct_unaligned;
	struct sis_base_struct *base_struct;
	struct sis_sync_cmd_params params;
	unsigned long error_buffer_paddr;
	dma_addr_t bus_address;

	base_struct_unaligned = kzalloc(sizeof(*base_struct)
		+ SIS_BASE_STRUCT_ALIGNMENT - 1, GFP_KERNEL);
	if (!base_struct_unaligned)
		return -ENOMEM;

	base_struct = PTR_ALIGN(base_struct_unaligned,
		SIS_BASE_STRUCT_ALIGNMENT);
	error_buffer_paddr = (unsigned long)ctrl_info->error_buffer_dma_handle;

	put_unaligned_le32(SIS_BASE_STRUCT_REVISION, &base_struct->revision);
	put_unaligned_le32(lower_32_bits(error_buffer_paddr),
		&base_struct->error_buffer_paddr_low);
	put_unaligned_le32(upper_32_bits(error_buffer_paddr),
		&base_struct->error_buffer_paddr_high);
	put_unaligned_le32(PQI_ERROR_BUFFER_ELEMENT_LENGTH,
		&base_struct->error_buffer_element_length);
	put_unaligned_le32(ctrl_info->max_io_slots,
		&base_struct->error_buffer_num_elements);

	bus_address = dma_map_single(&ctrl_info->pci_dev->dev, base_struct,
		sizeof(*base_struct), DMA_TO_DEVICE);
	if (dma_mapping_error(&ctrl_info->pci_dev->dev, bus_address)) {
		rc = -ENOMEM;
		goto out;
	}

	memset(&params, 0, sizeof(params));
	params.mailbox[1] = lower_32_bits((u64)bus_address);
	params.mailbox[2] = upper_32_bits((u64)bus_address);
	params.mailbox[3] = sizeof(*base_struct);

	rc = sis_send_sync_cmd(ctrl_info, SIS_CMD_INIT_BASE_STRUCT_ADDRESS,
		&params);

	dma_unmap_single(&ctrl_info->pci_dev->dev, bus_address,
			sizeof(*base_struct), DMA_TO_DEVICE);
out:
	kfree(base_struct_unaligned);

	return rc;
}

#define SIS_DOORBELL_BIT_CLEAR_TIMEOUT_SECS	30

static int sis_wait_for_doorbell_bit_to_clear(
	struct pqi_ctrl_info *ctrl_info, u32 bit)
{
	int rc = 0;
	u32 doorbell_register;
	unsigned long timeout;

	timeout = (SIS_DOORBELL_BIT_CLEAR_TIMEOUT_SECS * HZ) + jiffies;

	while (1) {
		doorbell_register =
			readl(&ctrl_info->registers->sis_host_to_ctrl_doorbell);
		if ((doorbell_register & bit) == 0)
			break;
		if (readl(&ctrl_info->registers->sis_firmware_status) &
			SIS_CTRL_KERNEL_PANIC) {
			rc = -ENODEV;
			break;
		}
		if (time_after(jiffies, timeout)) {
			dev_err(&ctrl_info->pci_dev->dev,
				"doorbell register bit 0x%x not cleared\n",
				bit);
			rc = -ETIMEDOUT;
			break;
		}
		usleep_range(1000, 2000);
	}

	return rc;
}

static inline int sis_set_doorbell_bit(struct pqi_ctrl_info *ctrl_info, u32 bit)
{
	writel(bit, &ctrl_info->registers->sis_host_to_ctrl_doorbell);

	return sis_wait_for_doorbell_bit_to_clear(ctrl_info, bit);
}

void sis_enable_msix(struct pqi_ctrl_info *ctrl_info)
{
	sis_set_doorbell_bit(ctrl_info, SIS_ENABLE_MSIX);
}

void sis_enable_intx(struct pqi_ctrl_info *ctrl_info)
{
	sis_set_doorbell_bit(ctrl_info, SIS_ENABLE_INTX);
}

void sis_shutdown_ctrl(struct pqi_ctrl_info *ctrl_info,
	enum pqi_ctrl_shutdown_reason ctrl_shutdown_reason)
{
	if (readl(&ctrl_info->registers->sis_firmware_status) &
		SIS_CTRL_KERNEL_PANIC)
		return;

	if (ctrl_info->firmware_triage_supported)
		writel(ctrl_shutdown_reason, &ctrl_info->registers->sis_ctrl_shutdown_reason_code);

	writel(SIS_TRIGGER_SHUTDOWN, &ctrl_info->registers->sis_host_to_ctrl_doorbell);
}

int sis_pqi_reset_quiesce(struct pqi_ctrl_info *ctrl_info)
{
	return sis_set_doorbell_bit(ctrl_info, SIS_PQI_RESET_QUIESCE);
}

int sis_reenable_sis_mode(struct pqi_ctrl_info *ctrl_info)
{
	return sis_set_doorbell_bit(ctrl_info, SIS_REENABLE_SIS_MODE);
}

void sis_write_driver_scratch(struct pqi_ctrl_info *ctrl_info, u32 value)
{
	writel(value, &ctrl_info->registers->sis_driver_scratch);
}

u32 sis_read_driver_scratch(struct pqi_ctrl_info *ctrl_info)
{
	return readl(&ctrl_info->registers->sis_driver_scratch);
}

static inline enum sis_fw_triage_status
	sis_read_firmware_triage_status(struct pqi_ctrl_info *ctrl_info)
{
	return ((enum sis_fw_triage_status)(readl(&ctrl_info->registers->sis_firmware_status) &
		SIS_CTRL_KERNEL_FW_TRIAGE));
}

void sis_soft_reset(struct pqi_ctrl_info *ctrl_info)
{
	writel(SIS_SOFT_RESET,
		&ctrl_info->registers->sis_host_to_ctrl_doorbell);
}

#define SIS_FW_TRIAGE_STATUS_TIMEOUT_SECS		300
#define SIS_FW_TRIAGE_STATUS_POLL_INTERVAL_SECS		1

int sis_wait_for_fw_triage_completion(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	enum sis_fw_triage_status status;
	unsigned long timeout;

	timeout = (SIS_FW_TRIAGE_STATUS_TIMEOUT_SECS * HZ) + jiffies;
	while (1) {
		status = sis_read_firmware_triage_status(ctrl_info);
		if (status == FW_TRIAGE_COND_INVALID) {
			dev_err(&ctrl_info->pci_dev->dev,
				"firmware triage condition invalid\n");
			rc = -EINVAL;
			break;
		} else if (status == FW_TRIAGE_NOT_STARTED ||
			status == FW_TRIAGE_COMPLETED) {
			rc = 0;
			break;
		}

		if (time_after(jiffies, timeout)) {
			dev_err(&ctrl_info->pci_dev->dev,
				"timed out waiting for firmware triage status\n");
			rc = -ETIMEDOUT;
			break;
		}

		ssleep(SIS_FW_TRIAGE_STATUS_POLL_INTERVAL_SECS);
	}

	return rc;
}

static void __attribute__((unused)) verify_structures(void)
{
	BUILD_BUG_ON(offsetof(struct sis_base_struct,
		revision) != 0x0);
	BUILD_BUG_ON(offsetof(struct sis_base_struct,
		flags) != 0x4);
	BUILD_BUG_ON(offsetof(struct sis_base_struct,
		error_buffer_paddr_low) != 0x8);
	BUILD_BUG_ON(offsetof(struct sis_base_struct,
		error_buffer_paddr_high) != 0xc);
	BUILD_BUG_ON(offsetof(struct sis_base_struct,
		error_buffer_element_length) != 0x10);
	BUILD_BUG_ON(offsetof(struct sis_base_struct,
		error_buffer_num_elements) != 0x14);
	BUILD_BUG_ON(sizeof(struct sis_base_struct) != 0x18);
}
