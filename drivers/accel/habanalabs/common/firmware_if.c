// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"
#include "../include/common/hl_boot_if.h"

#include <linux/firmware.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/vmalloc.h>

#include <trace/events/habanalabs.h>

#define FW_FILE_MAX_SIZE		0x1400000 /* maximum size of 20MB */

static char *comms_cmd_str_arr[COMMS_INVLD_LAST] = {
	[COMMS_NOOP] = __stringify(COMMS_NOOP),
	[COMMS_CLR_STS] = __stringify(COMMS_CLR_STS),
	[COMMS_RST_STATE] = __stringify(COMMS_RST_STATE),
	[COMMS_PREP_DESC] = __stringify(COMMS_PREP_DESC),
	[COMMS_DATA_RDY] = __stringify(COMMS_DATA_RDY),
	[COMMS_EXEC] = __stringify(COMMS_EXEC),
	[COMMS_RST_DEV] = __stringify(COMMS_RST_DEV),
	[COMMS_GOTO_WFE] = __stringify(COMMS_GOTO_WFE),
	[COMMS_SKIP_BMC] = __stringify(COMMS_SKIP_BMC),
	[COMMS_PREP_DESC_ELBI] = __stringify(COMMS_PREP_DESC_ELBI),
};

static char *comms_sts_str_arr[COMMS_STS_INVLD_LAST] = {
	[COMMS_STS_NOOP] = __stringify(COMMS_STS_NOOP),
	[COMMS_STS_ACK] = __stringify(COMMS_STS_ACK),
	[COMMS_STS_OK] = __stringify(COMMS_STS_OK),
	[COMMS_STS_ERR] = __stringify(COMMS_STS_ERR),
	[COMMS_STS_VALID_ERR] = __stringify(COMMS_STS_VALID_ERR),
	[COMMS_STS_TIMEOUT_ERR] = __stringify(COMMS_STS_TIMEOUT_ERR),
};

static char *extract_fw_ver_from_str(const char *fw_str)
{
	char *str, *fw_ver, *whitespace;
	u32 ver_offset;

	fw_ver = kmalloc(VERSION_MAX_LEN, GFP_KERNEL);
	if (!fw_ver)
		return NULL;

	str = strnstr(fw_str, "fw-", VERSION_MAX_LEN);
	if (!str)
		goto free_fw_ver;

	/* Skip the fw- part */
	str += 3;
	ver_offset = str - fw_str;

	/* Copy until the next whitespace */
	whitespace = strnstr(str, " ", VERSION_MAX_LEN - ver_offset);
	if (!whitespace)
		goto free_fw_ver;

	strscpy(fw_ver, str, whitespace - str + 1);

	return fw_ver;

free_fw_ver:
	kfree(fw_ver);
	return NULL;
}

/**
 * extract_u32_until_given_char() - given a string of the format "<u32><char>*", extract the u32.
 * @str: the given string
 * @ver_num: the pointer to the extracted u32 to be returned to the caller.
 * @given_char: the given char at the end of the u32 in the string
 *
 * Return: Upon success, return a pointer to the given_char in the string. Upon failure, return NULL
 */
static char *extract_u32_until_given_char(char *str, u32 *ver_num, char given_char)
{
	char num_str[8] = {}, *ch;

	ch = strchrnul(str, given_char);
	if (*ch == '\0' || ch == str || ch - str >= sizeof(num_str))
		return NULL;

	memcpy(num_str, str, ch - str);
	if (kstrtou32(num_str, 10, ver_num))
		return NULL;
	return ch;
}

/**
 * hl_get_sw_major_minor_subminor() - extract the FW's SW version major, minor, sub-minor
 *				      from the version string
 * @hdev: pointer to the hl_device
 * @fw_str: the FW's version string
 *
 * The extracted version is set in the hdev fields: fw_sw_{major/minor/sub_minor}_ver.
 *
 * fw_str is expected to have one of two possible formats, examples:
 * 1) 'Preboot version hl-gaudi2-1.9.0-fw-42.0.1-sec-3'
 * 2) 'Preboot version hl-gaudi2-1.9.0-rc-fw-42.0.1-sec-3'
 * In those examples, the SW major,minor,subminor are correspondingly: 1,9,0.
 *
 * Return: 0 for success or a negative error code for failure.
 */
static int hl_get_sw_major_minor_subminor(struct hl_device *hdev, const char *fw_str)
{
	char *end, *start;

	end = strnstr(fw_str, "-rc-", VERSION_MAX_LEN);
	if (end == fw_str)
		return -EINVAL;

	if (!end)
		end = strnstr(fw_str, "-fw-", VERSION_MAX_LEN);

	if (end == fw_str)
		return -EINVAL;

	if (!end)
		return -EINVAL;

	for (start = end - 1; start != fw_str; start--) {
		if (*start == '-')
			break;
	}

	if (start == fw_str)
		return -EINVAL;

	/* start/end point each to the starting and ending hyphen of the sw version e.g. -1.9.0- */
	start++;
	start = extract_u32_until_given_char(start, &hdev->fw_sw_major_ver, '.');
	if (!start)
		goto err_zero_ver;

	start++;
	start = extract_u32_until_given_char(start, &hdev->fw_sw_minor_ver, '.');
	if (!start)
		goto err_zero_ver;

	start++;
	start = extract_u32_until_given_char(start, &hdev->fw_sw_sub_minor_ver, '-');
	if (!start)
		goto err_zero_ver;

	return 0;

err_zero_ver:
	hdev->fw_sw_major_ver = 0;
	hdev->fw_sw_minor_ver = 0;
	hdev->fw_sw_sub_minor_ver = 0;
	return -EINVAL;
}

/**
 * hl_get_preboot_major_minor() - extract the FW's version major, minor from the version string.
 * @hdev: pointer to the hl_device
 * @preboot_ver: the FW's version string
 *
 * preboot_ver is expected to be the format of <major>.<minor>.<sub minor>*, e.g: 42.0.1-sec-3
 * The extracted version is set in the hdev fields: fw_inner_{major/minor}_ver.
 *
 * Return: 0 on success, negative error code for failure.
 */
static int hl_get_preboot_major_minor(struct hl_device *hdev, char *preboot_ver)
{
	preboot_ver = extract_u32_until_given_char(preboot_ver, &hdev->fw_inner_major_ver, '.');
	if (!preboot_ver) {
		dev_err(hdev->dev, "Error parsing preboot major version\n");
		goto err_zero_ver;
	}

	preboot_ver++;

	preboot_ver = extract_u32_until_given_char(preboot_ver, &hdev->fw_inner_minor_ver, '.');
	if (!preboot_ver) {
		dev_err(hdev->dev, "Error parsing preboot minor version\n");
		goto err_zero_ver;
	}
	return 0;

err_zero_ver:
	hdev->fw_inner_major_ver = 0;
	hdev->fw_inner_minor_ver = 0;
	return -EINVAL;
}

static int hl_request_fw(struct hl_device *hdev,
				const struct firmware **firmware_p,
				const char *fw_name)
{
	size_t fw_size;
	int rc;

	rc = request_firmware(firmware_p, fw_name, hdev->dev);
	if (rc) {
		dev_err(hdev->dev, "Firmware file %s is not found! (error %d)\n",
				fw_name, rc);
		goto out;
	}

	fw_size = (*firmware_p)->size;
	if ((fw_size % 4) != 0) {
		dev_err(hdev->dev, "Illegal %s firmware size %zu\n",
				fw_name, fw_size);
		rc = -EINVAL;
		goto release_fw;
	}

	dev_dbg(hdev->dev, "%s firmware size == %zu\n", fw_name, fw_size);

	if (fw_size > FW_FILE_MAX_SIZE) {
		dev_err(hdev->dev,
			"FW file size %zu exceeds maximum of %u bytes\n",
			fw_size, FW_FILE_MAX_SIZE);
		rc = -EINVAL;
		goto release_fw;
	}

	return 0;

release_fw:
	release_firmware(*firmware_p);
out:
	return rc;
}

/**
 * hl_release_firmware() - release FW
 *
 * @fw: fw descriptor
 *
 * note: this inline function added to serve as a comprehensive mirror for the
 *       hl_request_fw function.
 */
static inline void hl_release_firmware(const struct firmware *fw)
{
	release_firmware(fw);
}

/**
 * hl_fw_copy_fw_to_device() - copy FW to device
 *
 * @hdev: pointer to hl_device structure.
 * @fw: fw descriptor
 * @dst: IO memory mapped address space to copy firmware to
 * @src_offset: offset in src FW to copy from
 * @size: amount of bytes to copy (0 to copy the whole binary)
 *
 * actual copy of FW binary data to device, shared by static and dynamic loaders
 */
static int hl_fw_copy_fw_to_device(struct hl_device *hdev,
				const struct firmware *fw, void __iomem *dst,
				u32 src_offset, u32 size)
{
	const void *fw_data;

	/* size 0 indicates to copy the whole file */
	if (!size)
		size = fw->size;

	if (src_offset + size > fw->size) {
		dev_err(hdev->dev,
			"size to copy(%u) and offset(%u) are invalid\n",
			size, src_offset);
		return -EINVAL;
	}

	fw_data = (const void *) fw->data;

	memcpy_toio(dst, fw_data + src_offset, size);
	return 0;
}

/**
 * hl_fw_copy_msg_to_device() - copy message to device
 *
 * @hdev: pointer to hl_device structure.
 * @msg: message
 * @dst: IO memory mapped address space to copy firmware to
 * @src_offset: offset in src message to copy from
 * @size: amount of bytes to copy (0 to copy the whole binary)
 *
 * actual copy of message data to device.
 */
static int hl_fw_copy_msg_to_device(struct hl_device *hdev,
		struct lkd_msg_comms *msg, void __iomem *dst,
		u32 src_offset, u32 size)
{
	void *msg_data;

	/* size 0 indicates to copy the whole file */
	if (!size)
		size = sizeof(struct lkd_msg_comms);

	if (src_offset + size > sizeof(struct lkd_msg_comms)) {
		dev_err(hdev->dev,
			"size to copy(%u) and offset(%u) are invalid\n",
			size, src_offset);
		return -EINVAL;
	}

	msg_data = (void *) msg;

	memcpy_toio(dst, msg_data + src_offset, size);

	return 0;
}

/**
 * hl_fw_load_fw_to_device() - Load F/W code to device's memory.
 *
 * @hdev: pointer to hl_device structure.
 * @fw_name: the firmware image name
 * @dst: IO memory mapped address space to copy firmware to
 * @src_offset: offset in src FW to copy from
 * @size: amount of bytes to copy (0 to copy the whole binary)
 *
 * Copy fw code from firmware file to device memory.
 *
 * Return: 0 on success, non-zero for failure.
 */
int hl_fw_load_fw_to_device(struct hl_device *hdev, const char *fw_name,
				void __iomem *dst, u32 src_offset, u32 size)
{
	const struct firmware *fw;
	int rc;

	rc = hl_request_fw(hdev, &fw, fw_name);
	if (rc)
		return rc;

	rc = hl_fw_copy_fw_to_device(hdev, fw, dst, src_offset, size);

	hl_release_firmware(fw);
	return rc;
}

int hl_fw_send_pci_access_msg(struct hl_device *hdev, u32 opcode, u64 value)
{
	struct cpucp_packet pkt = {};

	pkt.ctl = cpu_to_le32(opcode << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(value);

	return hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
}

int hl_fw_send_cpu_message(struct hl_device *hdev, u32 hw_queue_id, u32 *msg,
				u16 len, u32 timeout, u64 *result)
{
	struct hl_hw_queue *queue = &hdev->kernel_queues[hw_queue_id];
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct cpucp_packet *pkt;
	dma_addr_t pkt_dma_addr;
	struct hl_bd *sent_bd;
	u32 tmp, expected_ack_val, pi, opcode;
	int rc;

	pkt = hl_cpu_accessible_dma_pool_alloc(hdev, len, &pkt_dma_addr);
	if (!pkt) {
		dev_err(hdev->dev,
			"Failed to allocate DMA memory for packet to CPU\n");
		return -ENOMEM;
	}

	memcpy(pkt, msg, len);

	mutex_lock(&hdev->send_cpu_message_lock);

	/* CPU-CP messages can be sent during soft-reset */
	if (hdev->disabled && !hdev->reset_info.in_compute_reset) {
		rc = 0;
		goto out;
	}

	if (hdev->device_cpu_disabled) {
		rc = -EIO;
		goto out;
	}

	/* set fence to a non valid value */
	pkt->fence = cpu_to_le32(UINT_MAX);
	pi = queue->pi;

	/*
	 * The CPU queue is a synchronous queue with an effective depth of
	 * a single entry (although it is allocated with room for multiple
	 * entries). We lock on it using 'send_cpu_message_lock' which
	 * serializes accesses to the CPU queue.
	 * Which means that we don't need to lock the access to the entire H/W
	 * queues module when submitting a JOB to the CPU queue.
	 */
	hl_hw_queue_submit_bd(hdev, queue, hl_queue_inc_ptr(queue->pi), len, pkt_dma_addr);

	if (prop->fw_app_cpu_boot_dev_sts0 & CPU_BOOT_DEV_STS0_PKT_PI_ACK_EN)
		expected_ack_val = queue->pi;
	else
		expected_ack_val = CPUCP_PACKET_FENCE_VAL;

	rc = hl_poll_timeout_memory(hdev, &pkt->fence, tmp,
				(tmp == expected_ack_val), 1000,
				timeout, true);

	hl_hw_queue_inc_ci_kernel(hdev, hw_queue_id);

	if (rc == -ETIMEDOUT) {
		/* If FW performed reset just before sending it a packet, we will get a timeout.
		 * This is expected behavior, hence no need for error message.
		 */
		if (!hl_device_operational(hdev, NULL) && !hdev->reset_info.in_compute_reset)
			dev_dbg(hdev->dev, "Device CPU packet timeout (0x%x) due to FW reset\n",
					tmp);
		else
			dev_err(hdev->dev, "Device CPU packet timeout (status = 0x%x)\n", tmp);
		hdev->device_cpu_disabled = true;
		goto out;
	}

	tmp = le32_to_cpu(pkt->ctl);

	rc = (tmp & CPUCP_PKT_CTL_RC_MASK) >> CPUCP_PKT_CTL_RC_SHIFT;
	if (rc) {
		opcode = (tmp & CPUCP_PKT_CTL_OPCODE_MASK) >> CPUCP_PKT_CTL_OPCODE_SHIFT;

		if (!prop->supports_advanced_cpucp_rc) {
			dev_dbg(hdev->dev, "F/W ERROR %d for CPU packet %d\n", rc, opcode);
			rc = -EIO;
			goto scrub_descriptor;
		}

		switch (rc) {
		case cpucp_packet_invalid:
			dev_err(hdev->dev,
				"CPU packet %d is not supported by F/W\n", opcode);
			break;
		case cpucp_packet_fault:
			dev_err(hdev->dev,
				"F/W failed processing CPU packet %d\n", opcode);
			break;
		case cpucp_packet_invalid_pkt:
			dev_dbg(hdev->dev,
				"CPU packet %d is not supported by F/W\n", opcode);
			break;
		case cpucp_packet_invalid_params:
			dev_err(hdev->dev,
				"F/W reports invalid parameters for CPU packet %d\n", opcode);
			break;

		default:
			dev_err(hdev->dev,
				"Unknown F/W ERROR %d for CPU packet %d\n", rc, opcode);
		}

		/* propagate the return code from the f/w to the callers who want to check it */
		if (result)
			*result = rc;

		rc = -EIO;

	} else if (result) {
		*result = le64_to_cpu(pkt->result);
	}

scrub_descriptor:
	/* Scrub previous buffer descriptor 'ctl' field which contains the
	 * previous PI value written during packet submission.
	 * We must do this or else F/W can read an old value upon queue wraparound.
	 */
	sent_bd = queue->kernel_address;
	sent_bd += hl_pi_2_offset(pi);
	sent_bd->ctl = cpu_to_le32(UINT_MAX);

out:
	mutex_unlock(&hdev->send_cpu_message_lock);

	hl_cpu_accessible_dma_pool_free(hdev, len, pkt);

	return rc;
}

int hl_fw_unmask_irq(struct hl_device *hdev, u16 event_type)
{
	struct cpucp_packet pkt;
	u64 result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_UNMASK_RAZWI_IRQ <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(event_type);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, &result);

	if (rc)
		dev_err(hdev->dev, "failed to unmask RAZWI IRQ %d", event_type);

	return rc;
}

int hl_fw_unmask_irq_arr(struct hl_device *hdev, const u32 *irq_arr,
		size_t irq_arr_size)
{
	struct cpucp_unmask_irq_arr_packet *pkt;
	size_t total_pkt_size;
	u64 result;
	int rc;

	total_pkt_size = sizeof(struct cpucp_unmask_irq_arr_packet) +
			irq_arr_size;

	/* data should be aligned to 8 bytes in order to CPU-CP to copy it */
	total_pkt_size = (total_pkt_size + 0x7) & ~0x7;

	/* total_pkt_size is casted to u16 later on */
	if (total_pkt_size > USHRT_MAX) {
		dev_err(hdev->dev, "too many elements in IRQ array\n");
		return -EINVAL;
	}

	pkt = kzalloc(total_pkt_size, GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	pkt->length = cpu_to_le32(irq_arr_size / sizeof(irq_arr[0]));
	memcpy(&pkt->irqs, irq_arr, irq_arr_size);

	pkt->cpucp_pkt.ctl = cpu_to_le32(CPUCP_PACKET_UNMASK_RAZWI_IRQ_ARRAY <<
						CPUCP_PKT_CTL_OPCODE_SHIFT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) pkt,
						total_pkt_size, 0, &result);

	if (rc)
		dev_err(hdev->dev, "failed to unmask IRQ array\n");

	kfree(pkt);

	return rc;
}

int hl_fw_test_cpu_queue(struct hl_device *hdev)
{
	struct cpucp_packet test_pkt = {};
	u64 result;
	int rc;

	test_pkt.ctl = cpu_to_le32(CPUCP_PACKET_TEST <<
					CPUCP_PKT_CTL_OPCODE_SHIFT);
	test_pkt.value = cpu_to_le64(CPUCP_PACKET_FENCE_VAL);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &test_pkt,
						sizeof(test_pkt), 0, &result);

	if (!rc) {
		if (result != CPUCP_PACKET_FENCE_VAL)
			dev_err(hdev->dev,
				"CPU queue test failed (%#08llx)\n", result);
	} else {
		dev_err(hdev->dev, "CPU queue test failed, error %d\n", rc);
	}

	return rc;
}

void *hl_fw_cpu_accessible_dma_pool_alloc(struct hl_device *hdev, size_t size,
						dma_addr_t *dma_handle)
{
	u64 kernel_addr;

	kernel_addr = gen_pool_alloc(hdev->cpu_accessible_dma_pool, size);

	*dma_handle = hdev->cpu_accessible_dma_address +
		(kernel_addr - (u64) (uintptr_t) hdev->cpu_accessible_dma_mem);

	return (void *) (uintptr_t) kernel_addr;
}

void hl_fw_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size,
					void *vaddr)
{
	gen_pool_free(hdev->cpu_accessible_dma_pool, (u64) (uintptr_t) vaddr,
			size);
}

int hl_fw_send_soft_reset(struct hl_device *hdev)
{
	struct cpucp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));
	pkt.ctl = cpu_to_le32(CPUCP_PACKET_SOFT_RESET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc)
		dev_err(hdev->dev, "failed to send soft-reset msg (err = %d)\n", rc);

	return rc;
}

int hl_fw_send_device_activity(struct hl_device *hdev, bool open)
{
	struct cpucp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));
	pkt.ctl = cpu_to_le32(CPUCP_PACKET_ACTIVE_STATUS_SET <<	CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(open);
	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc)
		dev_err(hdev->dev, "failed to send device activity msg(%u)\n", open);

	return rc;
}

int hl_fw_send_heartbeat(struct hl_device *hdev)
{
	struct cpucp_packet hb_pkt;
	u64 result;
	int rc;

	memset(&hb_pkt, 0, sizeof(hb_pkt));
	hb_pkt.ctl = cpu_to_le32(CPUCP_PACKET_TEST <<
					CPUCP_PKT_CTL_OPCODE_SHIFT);
	hb_pkt.value = cpu_to_le64(CPUCP_PACKET_FENCE_VAL);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &hb_pkt,
						sizeof(hb_pkt), 0, &result);

	if ((rc) || (result != CPUCP_PACKET_FENCE_VAL))
		return -EIO;

	if (le32_to_cpu(hb_pkt.status_mask) &
					CPUCP_PKT_HB_STATUS_EQ_FAULT_MASK) {
		dev_warn(hdev->dev, "FW reported EQ fault during heartbeat\n");
		rc = -EIO;
	}

	return rc;
}

static bool fw_report_boot_dev0(struct hl_device *hdev, u32 err_val,
								u32 sts_val)
{
	bool err_exists = false;

	if (!(err_val & CPU_BOOT_ERR0_ENABLED))
		return false;

	if (err_val & CPU_BOOT_ERR0_DRAM_INIT_FAIL) {
		dev_err(hdev->dev,
			"Device boot error - DRAM initialization failed\n");
		err_exists = true;
	}

	if (err_val & CPU_BOOT_ERR0_FIT_CORRUPTED) {
		dev_err(hdev->dev, "Device boot error - FIT image corrupted\n");
		err_exists = true;
	}

	if (err_val & CPU_BOOT_ERR0_TS_INIT_FAIL) {
		dev_err(hdev->dev,
			"Device boot error - Thermal Sensor initialization failed\n");
		err_exists = true;
	}

	if (err_val & CPU_BOOT_ERR0_BMC_WAIT_SKIPPED) {
		if (hdev->bmc_enable) {
			dev_err(hdev->dev,
				"Device boot error - Skipped waiting for BMC\n");
			err_exists = true;
		} else {
			dev_info(hdev->dev,
				"Device boot message - Skipped waiting for BMC\n");
			/* This is an info so we don't want it to disable the
			 * device
			 */
			err_val &= ~CPU_BOOT_ERR0_BMC_WAIT_SKIPPED;
		}
	}

	if (err_val & CPU_BOOT_ERR0_NIC_DATA_NOT_RDY) {
		dev_err(hdev->dev,
			"Device boot error - Serdes data from BMC not available\n");
		err_exists = true;
	}

	if (err_val & CPU_BOOT_ERR0_NIC_FW_FAIL) {
		dev_err(hdev->dev,
			"Device boot error - NIC F/W initialization failed\n");
		err_exists = true;
	}

	if (err_val & CPU_BOOT_ERR0_SECURITY_NOT_RDY) {
		dev_err(hdev->dev,
			"Device boot warning - security not ready\n");
		err_exists = true;
	}

	if (err_val & CPU_BOOT_ERR0_SECURITY_FAIL) {
		dev_err(hdev->dev, "Device boot error - security failure\n");
		err_exists = true;
	}

	if (err_val & CPU_BOOT_ERR0_EFUSE_FAIL) {
		dev_err(hdev->dev, "Device boot error - eFuse failure\n");
		err_exists = true;
	}

	if (err_val & CPU_BOOT_ERR0_SEC_IMG_VER_FAIL) {
		dev_err(hdev->dev, "Device boot error - Failed to load preboot secondary image\n");
		err_exists = true;
	}

	if (err_val & CPU_BOOT_ERR0_PLL_FAIL) {
		dev_err(hdev->dev, "Device boot error - PLL failure\n");
		err_exists = true;
	}

	if (err_val & CPU_BOOT_ERR0_DEVICE_UNUSABLE_FAIL) {
		/* Ignore this bit, don't prevent driver loading */
		dev_dbg(hdev->dev, "device unusable status is set\n");
		err_val &= ~CPU_BOOT_ERR0_DEVICE_UNUSABLE_FAIL;
	}

	if (err_val & CPU_BOOT_ERR0_BINNING_FAIL) {
		dev_err(hdev->dev, "Device boot error - binning failure\n");
		err_exists = true;
	}

	if (sts_val & CPU_BOOT_DEV_STS0_ENABLED)
		dev_dbg(hdev->dev, "Device status0 %#x\n", sts_val);

	if (err_val & CPU_BOOT_ERR0_EEPROM_FAIL) {
		dev_err(hdev->dev, "Device boot error - EEPROM failure detected\n");
		err_exists = true;
	}

	/* All warnings should go here in order not to reach the unknown error validation */
	if (err_val & CPU_BOOT_ERR0_DRAM_SKIPPED) {
		dev_warn(hdev->dev,
			"Device boot warning - Skipped DRAM initialization\n");
		/* This is a warning so we don't want it to disable the
		 * device
		 */
		err_val &= ~CPU_BOOT_ERR0_DRAM_SKIPPED;
	}

	if (err_val & CPU_BOOT_ERR0_PRI_IMG_VER_FAIL) {
		dev_warn(hdev->dev,
			"Device boot warning - Failed to load preboot primary image\n");
		/* This is a warning so we don't want it to disable the
		 * device as we have a secondary preboot image
		 */
		err_val &= ~CPU_BOOT_ERR0_PRI_IMG_VER_FAIL;
	}

	if (err_val & CPU_BOOT_ERR0_TPM_FAIL) {
		dev_warn(hdev->dev,
			"Device boot warning - TPM failure\n");
		/* This is a warning so we don't want it to disable the
		 * device
		 */
		err_val &= ~CPU_BOOT_ERR0_TPM_FAIL;
	}

	if (!err_exists && (err_val & ~CPU_BOOT_ERR0_ENABLED)) {
		dev_err(hdev->dev,
			"Device boot error - unknown ERR0 error 0x%08x\n", err_val);
		err_exists = true;
	}

	/* return error only if it's in the predefined mask */
	if (err_exists && ((err_val & ~CPU_BOOT_ERR0_ENABLED) &
				lower_32_bits(hdev->boot_error_status_mask)))
		return true;

	return false;
}

/* placeholder for ERR1 as no errors defined there yet */
static bool fw_report_boot_dev1(struct hl_device *hdev, u32 err_val,
								u32 sts_val)
{
	/*
	 * keep this variable to preserve the logic of the function.
	 * this way it would require less modifications when error will be
	 * added to DEV_ERR1
	 */
	bool err_exists = false;

	if (!(err_val & CPU_BOOT_ERR1_ENABLED))
		return false;

	if (sts_val & CPU_BOOT_DEV_STS1_ENABLED)
		dev_dbg(hdev->dev, "Device status1 %#x\n", sts_val);

	if (!err_exists && (err_val & ~CPU_BOOT_ERR1_ENABLED)) {
		dev_err(hdev->dev,
			"Device boot error - unknown ERR1 error 0x%08x\n",
								err_val);
		err_exists = true;
	}

	/* return error only if it's in the predefined mask */
	if (err_exists && ((err_val & ~CPU_BOOT_ERR1_ENABLED) &
				upper_32_bits(hdev->boot_error_status_mask)))
		return true;

	return false;
}

static int fw_read_errors(struct hl_device *hdev, u32 boot_err0_reg,
				u32 boot_err1_reg, u32 cpu_boot_dev_status0_reg,
				u32 cpu_boot_dev_status1_reg)
{
	u32 err_val, status_val;
	bool err_exists = false;

	/* Some of the firmware status codes are deprecated in newer f/w
	 * versions. In those versions, the errors are reported
	 * in different registers. Therefore, we need to check those
	 * registers and print the exact errors. Moreover, there
	 * may be multiple errors, so we need to report on each error
	 * separately. Some of the error codes might indicate a state
	 * that is not an error per-se, but it is an error in production
	 * environment
	 */
	err_val = RREG32(boot_err0_reg);
	status_val = RREG32(cpu_boot_dev_status0_reg);
	err_exists = fw_report_boot_dev0(hdev, err_val, status_val);

	err_val = RREG32(boot_err1_reg);
	status_val = RREG32(cpu_boot_dev_status1_reg);
	err_exists |= fw_report_boot_dev1(hdev, err_val, status_val);

	if (err_exists)
		return -EIO;

	return 0;
}

int hl_fw_cpucp_info_get(struct hl_device *hdev,
				u32 sts_boot_dev_sts0_reg,
				u32 sts_boot_dev_sts1_reg, u32 boot_err0_reg,
				u32 boot_err1_reg)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct cpucp_packet pkt = {};
	dma_addr_t cpucp_info_dma_addr;
	void *cpucp_info_cpu_addr;
	char *kernel_ver;
	u64 result;
	int rc;

	cpucp_info_cpu_addr = hl_cpu_accessible_dma_pool_alloc(hdev, sizeof(struct cpucp_info),
								&cpucp_info_dma_addr);
	if (!cpucp_info_cpu_addr) {
		dev_err(hdev->dev,
			"Failed to allocate DMA memory for CPU-CP info packet\n");
		return -ENOMEM;
	}

	memset(cpucp_info_cpu_addr, 0, sizeof(struct cpucp_info));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_INFO_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.addr = cpu_to_le64(cpucp_info_dma_addr);
	pkt.data_max_size = cpu_to_le32(sizeof(struct cpucp_info));

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					HL_CPUCP_INFO_TIMEOUT_USEC, &result);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to handle CPU-CP info pkt, error %d\n", rc);
		goto out;
	}

	rc = fw_read_errors(hdev, boot_err0_reg, boot_err1_reg,
				sts_boot_dev_sts0_reg, sts_boot_dev_sts1_reg);
	if (rc) {
		dev_err(hdev->dev, "Errors in device boot\n");
		goto out;
	}

	memcpy(&prop->cpucp_info, cpucp_info_cpu_addr,
			sizeof(prop->cpucp_info));

	rc = hl_build_hwmon_channel_info(hdev, prop->cpucp_info.sensors);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to build hwmon channel info, error %d\n", rc);
		rc = -EFAULT;
		goto out;
	}

	kernel_ver = extract_fw_ver_from_str(prop->cpucp_info.kernel_version);
	if (kernel_ver) {
		dev_info(hdev->dev, "Linux version %s", kernel_ver);
		kfree(kernel_ver);
	}

	/* assume EQ code doesn't need to check eqe index */
	hdev->event_queue.check_eqe_index = false;

	/* Read FW application security bits again */
	if (prop->fw_cpu_boot_dev_sts0_valid) {
		prop->fw_app_cpu_boot_dev_sts0 = RREG32(sts_boot_dev_sts0_reg);
		if (prop->fw_app_cpu_boot_dev_sts0 &
				CPU_BOOT_DEV_STS0_EQ_INDEX_EN)
			hdev->event_queue.check_eqe_index = true;
	}

	if (prop->fw_cpu_boot_dev_sts1_valid)
		prop->fw_app_cpu_boot_dev_sts1 = RREG32(sts_boot_dev_sts1_reg);

out:
	hl_cpu_accessible_dma_pool_free(hdev, sizeof(struct cpucp_info), cpucp_info_cpu_addr);

	return rc;
}

static int hl_fw_send_msi_info_msg(struct hl_device *hdev)
{
	struct cpucp_array_data_packet *pkt;
	size_t total_pkt_size, data_size;
	u64 result;
	int rc;

	/* skip sending this info for unsupported ASICs */
	if (!hdev->asic_funcs->get_msi_info)
		return 0;

	data_size = CPUCP_NUM_OF_MSI_TYPES * sizeof(u32);
	total_pkt_size = sizeof(struct cpucp_array_data_packet) + data_size;

	/* data should be aligned to 8 bytes in order to CPU-CP to copy it */
	total_pkt_size = (total_pkt_size + 0x7) & ~0x7;

	/* total_pkt_size is casted to u16 later on */
	if (total_pkt_size > USHRT_MAX) {
		dev_err(hdev->dev, "CPUCP array data is too big\n");
		return -EINVAL;
	}

	pkt = kzalloc(total_pkt_size, GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	pkt->length = cpu_to_le32(CPUCP_NUM_OF_MSI_TYPES);

	memset((void *) &pkt->data, 0xFF, data_size);
	hdev->asic_funcs->get_msi_info(pkt->data);

	pkt->cpucp_pkt.ctl = cpu_to_le32(CPUCP_PACKET_MSI_INFO_SET <<
						CPUCP_PKT_CTL_OPCODE_SHIFT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *)pkt,
						total_pkt_size, 0, &result);

	/*
	 * in case packet result is invalid it means that FW does not support
	 * this feature and will use default/hard coded MSI values. no reason
	 * to stop the boot
	 */
	if (rc && result == cpucp_packet_invalid)
		rc = 0;

	if (rc)
		dev_err(hdev->dev, "failed to send CPUCP array data\n");

	kfree(pkt);

	return rc;
}

int hl_fw_cpucp_handshake(struct hl_device *hdev,
				u32 sts_boot_dev_sts0_reg,
				u32 sts_boot_dev_sts1_reg, u32 boot_err0_reg,
				u32 boot_err1_reg)
{
	int rc;

	rc = hl_fw_cpucp_info_get(hdev, sts_boot_dev_sts0_reg,
					sts_boot_dev_sts1_reg, boot_err0_reg,
					boot_err1_reg);
	if (rc)
		return rc;

	return hl_fw_send_msi_info_msg(hdev);
}

int hl_fw_get_eeprom_data(struct hl_device *hdev, void *data, size_t max_size)
{
	struct cpucp_packet pkt = {};
	void *eeprom_info_cpu_addr;
	dma_addr_t eeprom_info_dma_addr;
	u64 result;
	int rc;

	eeprom_info_cpu_addr = hl_cpu_accessible_dma_pool_alloc(hdev, max_size,
									&eeprom_info_dma_addr);
	if (!eeprom_info_cpu_addr) {
		dev_err(hdev->dev,
			"Failed to allocate DMA memory for CPU-CP EEPROM packet\n");
		return -ENOMEM;
	}

	memset(eeprom_info_cpu_addr, 0, max_size);

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_EEPROM_DATA_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.addr = cpu_to_le64(eeprom_info_dma_addr);
	pkt.data_max_size = cpu_to_le32(max_size);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
			HL_CPUCP_EEPROM_TIMEOUT_USEC, &result);

	if (rc) {
		dev_err(hdev->dev,
			"Failed to handle CPU-CP EEPROM packet, error %d\n",
			rc);
		goto out;
	}

	/* result contains the actual size */
	memcpy(data, eeprom_info_cpu_addr, min((size_t)result, max_size));

out:
	hl_cpu_accessible_dma_pool_free(hdev, max_size, eeprom_info_cpu_addr);

	return rc;
}

int hl_fw_get_monitor_dump(struct hl_device *hdev, void *data)
{
	struct cpucp_monitor_dump *mon_dump_cpu_addr;
	dma_addr_t mon_dump_dma_addr;
	struct cpucp_packet pkt = {};
	size_t data_size;
	__le32 *src_ptr;
	u32 *dst_ptr;
	u64 result;
	int i, rc;

	data_size = sizeof(struct cpucp_monitor_dump);
	mon_dump_cpu_addr = hl_cpu_accessible_dma_pool_alloc(hdev, data_size, &mon_dump_dma_addr);
	if (!mon_dump_cpu_addr) {
		dev_err(hdev->dev,
			"Failed to allocate DMA memory for CPU-CP monitor-dump packet\n");
		return -ENOMEM;
	}

	memset(mon_dump_cpu_addr, 0, data_size);

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_MONITOR_DUMP_GET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.addr = cpu_to_le64(mon_dump_dma_addr);
	pkt.data_max_size = cpu_to_le32(data_size);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
							HL_CPUCP_MON_DUMP_TIMEOUT_USEC, &result);
	if (rc) {
		dev_err(hdev->dev, "Failed to handle CPU-CP monitor-dump packet, error %d\n", rc);
		goto out;
	}

	/* result contains the actual size */
	src_ptr = (__le32 *) mon_dump_cpu_addr;
	dst_ptr = data;
	for (i = 0; i < (data_size / sizeof(u32)); i++) {
		*dst_ptr = le32_to_cpu(*src_ptr);
		src_ptr++;
		dst_ptr++;
	}

out:
	hl_cpu_accessible_dma_pool_free(hdev, data_size, mon_dump_cpu_addr);

	return rc;
}

int hl_fw_cpucp_pci_counters_get(struct hl_device *hdev,
		struct hl_info_pci_counters *counters)
{
	struct cpucp_packet pkt = {};
	u64 result;
	int rc;

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_PCIE_THROUGHPUT_GET <<
			CPUCP_PKT_CTL_OPCODE_SHIFT);

	/* Fetch PCI rx counter */
	pkt.index = cpu_to_le32(cpucp_pcie_throughput_rx);
	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					HL_CPUCP_INFO_TIMEOUT_USEC, &result);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to handle CPU-CP PCI info pkt, error %d\n", rc);
		return rc;
	}
	counters->rx_throughput = result;

	memset(&pkt, 0, sizeof(pkt));
	pkt.ctl = cpu_to_le32(CPUCP_PACKET_PCIE_THROUGHPUT_GET <<
			CPUCP_PKT_CTL_OPCODE_SHIFT);

	/* Fetch PCI tx counter */
	pkt.index = cpu_to_le32(cpucp_pcie_throughput_tx);
	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					HL_CPUCP_INFO_TIMEOUT_USEC, &result);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to handle CPU-CP PCI info pkt, error %d\n", rc);
		return rc;
	}
	counters->tx_throughput = result;

	/* Fetch PCI replay counter */
	memset(&pkt, 0, sizeof(pkt));
	pkt.ctl = cpu_to_le32(CPUCP_PACKET_PCIE_REPLAY_CNT_GET <<
			CPUCP_PKT_CTL_OPCODE_SHIFT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
			HL_CPUCP_INFO_TIMEOUT_USEC, &result);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to handle CPU-CP PCI info pkt, error %d\n", rc);
		return rc;
	}
	counters->replay_cnt = (u32) result;

	return rc;
}

int hl_fw_cpucp_total_energy_get(struct hl_device *hdev, u64 *total_energy)
{
	struct cpucp_packet pkt = {};
	u64 result;
	int rc;

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_TOTAL_ENERGY_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					HL_CPUCP_INFO_TIMEOUT_USEC, &result);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to handle CpuCP total energy pkt, error %d\n",
				rc);
		return rc;
	}

	*total_energy = result;

	return rc;
}

int get_used_pll_index(struct hl_device *hdev, u32 input_pll_index,
						enum pll_index *pll_index)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u8 pll_byte, pll_bit_off;
	bool dynamic_pll;
	int fw_pll_idx;

	dynamic_pll = !!(prop->fw_app_cpu_boot_dev_sts0 &
						CPU_BOOT_DEV_STS0_DYN_PLL_EN);

	if (!dynamic_pll) {
		/*
		 * in case we are working with legacy FW (each asic has unique
		 * PLL numbering) use the driver based index as they are
		 * aligned with fw legacy numbering
		 */
		*pll_index = input_pll_index;
		return 0;
	}

	/* retrieve a FW compatible PLL index based on
	 * ASIC specific user request
	 */
	fw_pll_idx = hdev->asic_funcs->map_pll_idx_to_fw_idx(input_pll_index);
	if (fw_pll_idx < 0) {
		dev_err(hdev->dev, "Invalid PLL index (%u) error %d\n",
			input_pll_index, fw_pll_idx);
		return -EINVAL;
	}

	/* PLL map is a u8 array */
	pll_byte = prop->cpucp_info.pll_map[fw_pll_idx >> 3];
	pll_bit_off = fw_pll_idx & 0x7;

	if (!(pll_byte & BIT(pll_bit_off))) {
		dev_err(hdev->dev, "PLL index %d is not supported\n",
			fw_pll_idx);
		return -EINVAL;
	}

	*pll_index = fw_pll_idx;

	return 0;
}

int hl_fw_cpucp_pll_info_get(struct hl_device *hdev, u32 pll_index,
		u16 *pll_freq_arr)
{
	struct cpucp_packet pkt;
	enum pll_index used_pll_idx;
	u64 result;
	int rc;

	rc = get_used_pll_index(hdev, pll_index, &used_pll_idx);
	if (rc)
		return rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_PLL_INFO_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.pll_type = __cpu_to_le16((u16)used_pll_idx);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
			HL_CPUCP_INFO_TIMEOUT_USEC, &result);
	if (rc) {
		dev_err(hdev->dev, "Failed to read PLL info, error %d\n", rc);
		return rc;
	}

	pll_freq_arr[0] = FIELD_GET(CPUCP_PKT_RES_PLL_OUT0_MASK, result);
	pll_freq_arr[1] = FIELD_GET(CPUCP_PKT_RES_PLL_OUT1_MASK, result);
	pll_freq_arr[2] = FIELD_GET(CPUCP_PKT_RES_PLL_OUT2_MASK, result);
	pll_freq_arr[3] = FIELD_GET(CPUCP_PKT_RES_PLL_OUT3_MASK, result);

	return 0;
}

int hl_fw_cpucp_power_get(struct hl_device *hdev, u64 *power)
{
	struct cpucp_packet pkt;
	u64 result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_POWER_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.type = cpu_to_le16(CPUCP_POWER_INPUT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
			HL_CPUCP_INFO_TIMEOUT_USEC, &result);
	if (rc) {
		dev_err(hdev->dev, "Failed to read power, error %d\n", rc);
		return rc;
	}

	*power = result;

	return rc;
}

int hl_fw_dram_replaced_row_get(struct hl_device *hdev,
				struct cpucp_hbm_row_info *info)
{
	struct cpucp_hbm_row_info *cpucp_repl_rows_info_cpu_addr;
	dma_addr_t cpucp_repl_rows_info_dma_addr;
	struct cpucp_packet pkt = {};
	u64 result;
	int rc;

	cpucp_repl_rows_info_cpu_addr = hl_cpu_accessible_dma_pool_alloc(hdev,
							sizeof(struct cpucp_hbm_row_info),
							&cpucp_repl_rows_info_dma_addr);
	if (!cpucp_repl_rows_info_cpu_addr) {
		dev_err(hdev->dev,
			"Failed to allocate DMA memory for CPU-CP replaced rows info packet\n");
		return -ENOMEM;
	}

	memset(cpucp_repl_rows_info_cpu_addr, 0, sizeof(struct cpucp_hbm_row_info));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_HBM_REPLACED_ROWS_INFO_GET <<
					CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.addr = cpu_to_le64(cpucp_repl_rows_info_dma_addr);
	pkt.data_max_size = cpu_to_le32(sizeof(struct cpucp_hbm_row_info));

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					HL_CPUCP_INFO_TIMEOUT_USEC, &result);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to handle CPU-CP replaced rows info pkt, error %d\n", rc);
		goto out;
	}

	memcpy(info, cpucp_repl_rows_info_cpu_addr, sizeof(*info));

out:
	hl_cpu_accessible_dma_pool_free(hdev, sizeof(struct cpucp_hbm_row_info),
						cpucp_repl_rows_info_cpu_addr);

	return rc;
}

int hl_fw_dram_pending_row_get(struct hl_device *hdev, u32 *pend_rows_num)
{
	struct cpucp_packet pkt;
	u64 result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_HBM_PENDING_ROWS_STATUS << CPUCP_PKT_CTL_OPCODE_SHIFT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, &result);
	if (rc) {
		dev_err(hdev->dev,
				"Failed to handle CPU-CP pending rows info pkt, error %d\n", rc);
		goto out;
	}

	*pend_rows_num = (u32) result;
out:
	return rc;
}

int hl_fw_cpucp_engine_core_asid_set(struct hl_device *hdev, u32 asid)
{
	struct cpucp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_ENGINE_CORE_ASID_SET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(asid);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						HL_CPUCP_INFO_TIMEOUT_USEC, NULL);
	if (rc)
		dev_err(hdev->dev,
			"Failed on ASID configuration request for engine core, error %d\n",
			rc);

	return rc;
}

void hl_fw_ask_hard_reset_without_linux(struct hl_device *hdev)
{
	struct static_fw_load_mgr *static_loader =
			&hdev->fw_loader.static_loader;
	int rc;

	if (hdev->asic_prop.dynamic_fw_load) {
		rc = hl_fw_dynamic_send_protocol_cmd(hdev, &hdev->fw_loader,
				COMMS_RST_DEV, 0, false,
				hdev->fw_loader.cpu_timeout);
		if (rc)
			dev_err(hdev->dev, "Failed sending COMMS_RST_DEV\n");
	} else {
		WREG32(static_loader->kmd_msg_to_cpu_reg, KMD_MSG_RST_DEV);
	}
}

void hl_fw_ask_halt_machine_without_linux(struct hl_device *hdev)
{
	struct fw_load_mgr *fw_loader = &hdev->fw_loader;
	u32 status, cpu_boot_status_reg, cpu_timeout;
	struct static_fw_load_mgr *static_loader;
	struct pre_fw_load_props *pre_fw_load;
	int rc;

	if (hdev->device_cpu_is_halted)
		return;

	/* Stop device CPU to make sure nothing bad happens */
	if (hdev->asic_prop.dynamic_fw_load) {
		pre_fw_load = &fw_loader->pre_fw_load;
		cpu_timeout = fw_loader->cpu_timeout;
		cpu_boot_status_reg = pre_fw_load->cpu_boot_status_reg;

		rc = hl_fw_dynamic_send_protocol_cmd(hdev, &hdev->fw_loader,
				COMMS_GOTO_WFE, 0, false, cpu_timeout);
		if (rc) {
			dev_err(hdev->dev, "Failed sending COMMS_GOTO_WFE\n");
		} else {
			rc = hl_poll_timeout(
				hdev,
				cpu_boot_status_reg,
				status,
				status == CPU_BOOT_STATUS_IN_WFE,
				hdev->fw_poll_interval_usec,
				cpu_timeout);
			if (rc)
				dev_err(hdev->dev, "Current status=%u. Timed-out updating to WFE\n",
						status);
		}
	} else {
		static_loader = &hdev->fw_loader.static_loader;
		WREG32(static_loader->kmd_msg_to_cpu_reg, KMD_MSG_GOTO_WFE);
		msleep(static_loader->cpu_reset_wait_msec);

		/* Must clear this register in order to prevent preboot
		 * from reading WFE after reboot
		 */
		WREG32(static_loader->kmd_msg_to_cpu_reg, KMD_MSG_NA);
	}

	hdev->device_cpu_is_halted = true;
}

static void detect_cpu_boot_status(struct hl_device *hdev, u32 status)
{
	/* Some of the status codes below are deprecated in newer f/w
	 * versions but we keep them here for backward compatibility
	 */
	switch (status) {
	case CPU_BOOT_STATUS_NA:
		dev_err(hdev->dev,
			"Device boot progress - BTL/ROM did NOT run\n");
		break;
	case CPU_BOOT_STATUS_IN_WFE:
		dev_err(hdev->dev,
			"Device boot progress - Stuck inside WFE loop\n");
		break;
	case CPU_BOOT_STATUS_IN_BTL:
		dev_err(hdev->dev,
			"Device boot progress - Stuck in BTL\n");
		break;
	case CPU_BOOT_STATUS_IN_PREBOOT:
		dev_err(hdev->dev,
			"Device boot progress - Stuck in Preboot\n");
		break;
	case CPU_BOOT_STATUS_IN_SPL:
		dev_err(hdev->dev,
			"Device boot progress - Stuck in SPL\n");
		break;
	case CPU_BOOT_STATUS_IN_UBOOT:
		dev_err(hdev->dev,
			"Device boot progress - Stuck in u-boot\n");
		break;
	case CPU_BOOT_STATUS_DRAM_INIT_FAIL:
		dev_err(hdev->dev,
			"Device boot progress - DRAM initialization failed\n");
		break;
	case CPU_BOOT_STATUS_UBOOT_NOT_READY:
		dev_err(hdev->dev,
			"Device boot progress - Cannot boot\n");
		break;
	case CPU_BOOT_STATUS_TS_INIT_FAIL:
		dev_err(hdev->dev,
			"Device boot progress - Thermal Sensor initialization failed\n");
		break;
	case CPU_BOOT_STATUS_SECURITY_READY:
		dev_err(hdev->dev,
			"Device boot progress - Stuck in preboot after security initialization\n");
		break;
	default:
		dev_err(hdev->dev,
			"Device boot progress - Invalid or unexpected status code %d\n", status);
		break;
	}
}

int hl_fw_wait_preboot_ready(struct hl_device *hdev)
{
	struct pre_fw_load_props *pre_fw_load = &hdev->fw_loader.pre_fw_load;
	u32 status;
	int rc;

	/* Need to check two possible scenarios:
	 *
	 * CPU_BOOT_STATUS_WAITING_FOR_BOOT_FIT - for newer firmwares where
	 * the preboot is waiting for the boot fit
	 *
	 * All other status values - for older firmwares where the uboot was
	 * loaded from the FLASH
	 */
	rc = hl_poll_timeout(
		hdev,
		pre_fw_load->cpu_boot_status_reg,
		status,
		(status == CPU_BOOT_STATUS_NIC_FW_RDY) ||
		(status == CPU_BOOT_STATUS_READY_TO_BOOT) ||
		(status == CPU_BOOT_STATUS_WAITING_FOR_BOOT_FIT),
		hdev->fw_poll_interval_usec,
		pre_fw_load->wait_for_preboot_timeout);

	if (rc) {
		detect_cpu_boot_status(hdev, status);
		dev_err(hdev->dev, "CPU boot ready timeout (status = %d)\n", status);

		/* If we read all FF, then something is totally wrong, no point
		 * of reading specific errors
		 */
		if (status != -1)
			fw_read_errors(hdev, pre_fw_load->boot_err0_reg,
						pre_fw_load->boot_err1_reg,
						pre_fw_load->sts_boot_dev_sts0_reg,
						pre_fw_load->sts_boot_dev_sts1_reg);
		return -EIO;
	}

	hdev->fw_loader.fw_comp_loaded |= FW_TYPE_PREBOOT_CPU;

	return 0;
}

static int hl_fw_read_preboot_caps(struct hl_device *hdev)
{
	struct pre_fw_load_props *pre_fw_load;
	struct asic_fixed_properties *prop;
	u32 reg_val;
	int rc;

	prop = &hdev->asic_prop;
	pre_fw_load = &hdev->fw_loader.pre_fw_load;

	rc = hl_fw_wait_preboot_ready(hdev);
	if (rc)
		return rc;

	/*
	 * the registers DEV_STS* contain FW capabilities/features.
	 * We can rely on this registers only if bit CPU_BOOT_DEV_STS*_ENABLED
	 * is set.
	 * In the first read of this register we store the value of this
	 * register ONLY if the register is enabled (which will be propagated
	 * to next stages) and also mark the register as valid.
	 * In case it is not enabled the stored value will be left 0- all
	 * caps/features are off
	 */
	reg_val = RREG32(pre_fw_load->sts_boot_dev_sts0_reg);
	if (reg_val & CPU_BOOT_DEV_STS0_ENABLED) {
		prop->fw_cpu_boot_dev_sts0_valid = true;
		prop->fw_preboot_cpu_boot_dev_sts0 = reg_val;
	}

	reg_val = RREG32(pre_fw_load->sts_boot_dev_sts1_reg);
	if (reg_val & CPU_BOOT_DEV_STS1_ENABLED) {
		prop->fw_cpu_boot_dev_sts1_valid = true;
		prop->fw_preboot_cpu_boot_dev_sts1 = reg_val;
	}

	prop->dynamic_fw_load = !!(prop->fw_preboot_cpu_boot_dev_sts0 &
						CPU_BOOT_DEV_STS0_FW_LD_COM_EN);

	/* initialize FW loader once we know what load protocol is used */
	hdev->asic_funcs->init_firmware_loader(hdev);

	dev_dbg(hdev->dev, "Attempting %s FW load\n",
			prop->dynamic_fw_load ? "dynamic" : "legacy");
	return 0;
}

static int hl_fw_static_read_device_fw_version(struct hl_device *hdev,
					enum hl_fw_component fwc)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct fw_load_mgr *fw_loader = &hdev->fw_loader;
	struct static_fw_load_mgr *static_loader;
	char *dest, *boot_ver, *preboot_ver;
	u32 ver_off, limit;
	const char *name;
	char btl_ver[32];

	static_loader = &hdev->fw_loader.static_loader;

	switch (fwc) {
	case FW_COMP_BOOT_FIT:
		ver_off = RREG32(static_loader->boot_fit_version_offset_reg);
		dest = prop->uboot_ver;
		name = "Boot-fit";
		limit = static_loader->boot_fit_version_max_off;
		break;
	case FW_COMP_PREBOOT:
		ver_off = RREG32(static_loader->preboot_version_offset_reg);
		dest = prop->preboot_ver;
		name = "Preboot";
		limit = static_loader->preboot_version_max_off;
		break;
	default:
		dev_warn(hdev->dev, "Undefined FW component: %d\n", fwc);
		return -EIO;
	}

	ver_off &= static_loader->sram_offset_mask;

	if (ver_off < limit) {
		memcpy_fromio(dest,
			hdev->pcie_bar[fw_loader->sram_bar_id] + ver_off,
			VERSION_MAX_LEN);
	} else {
		dev_err(hdev->dev, "%s version offset (0x%x) is above SRAM\n",
								name, ver_off);
		strscpy(dest, "unavailable", VERSION_MAX_LEN);
		return -EIO;
	}

	if (fwc == FW_COMP_BOOT_FIT) {
		boot_ver = extract_fw_ver_from_str(prop->uboot_ver);
		if (boot_ver) {
			dev_info(hdev->dev, "boot-fit version %s\n", boot_ver);
			kfree(boot_ver);
		}
	} else if (fwc == FW_COMP_PREBOOT) {
		preboot_ver = strnstr(prop->preboot_ver, "Preboot",
						VERSION_MAX_LEN);
		if (preboot_ver && preboot_ver != prop->preboot_ver) {
			strscpy(btl_ver, prop->preboot_ver,
				min((int) (preboot_ver - prop->preboot_ver),
									31));
			dev_info(hdev->dev, "%s\n", btl_ver);
		}

		preboot_ver = extract_fw_ver_from_str(prop->preboot_ver);
		if (preboot_ver) {
			dev_info(hdev->dev, "preboot version %s\n",
								preboot_ver);
			kfree(preboot_ver);
		}
	}

	return 0;
}

/**
 * hl_fw_preboot_update_state - update internal data structures during
 *                              handshake with preboot
 *
 *
 * @hdev: pointer to the habanalabs device structure
 *
 * @return 0 on success, otherwise non-zero error code
 */
static void hl_fw_preboot_update_state(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 cpu_boot_dev_sts0, cpu_boot_dev_sts1;

	cpu_boot_dev_sts0 = prop->fw_preboot_cpu_boot_dev_sts0;
	cpu_boot_dev_sts1 = prop->fw_preboot_cpu_boot_dev_sts1;

	/* We read boot_dev_sts registers multiple times during boot:
	 * 1. preboot - a. Check whether the security status bits are valid
	 *              b. Check whether fw security is enabled
	 *              c. Check whether hard reset is done by preboot
	 * 2. boot cpu - a. Fetch boot cpu security status
	 *               b. Check whether hard reset is done by boot cpu
	 * 3. FW application - a. Fetch fw application security status
	 *                     b. Check whether hard reset is done by fw app
	 */
	prop->hard_reset_done_by_fw = !!(cpu_boot_dev_sts0 & CPU_BOOT_DEV_STS0_FW_HARD_RST_EN);

	prop->fw_security_enabled = !!(cpu_boot_dev_sts0 & CPU_BOOT_DEV_STS0_SECURITY_EN);

	dev_dbg(hdev->dev, "Firmware preboot boot device status0 %#x\n",
							cpu_boot_dev_sts0);

	dev_dbg(hdev->dev, "Firmware preboot boot device status1 %#x\n",
							cpu_boot_dev_sts1);

	dev_dbg(hdev->dev, "Firmware preboot hard-reset is %s\n",
			prop->hard_reset_done_by_fw ? "enabled" : "disabled");

	dev_dbg(hdev->dev, "firmware-level security is %s\n",
			prop->fw_security_enabled ? "enabled" : "disabled");

	dev_dbg(hdev->dev, "GIC controller is %s\n",
			prop->gic_interrupts_enable ? "enabled" : "disabled");
}

static int hl_fw_static_read_preboot_status(struct hl_device *hdev)
{
	int rc;

	rc = hl_fw_static_read_device_fw_version(hdev, FW_COMP_PREBOOT);
	if (rc)
		return rc;

	return 0;
}

int hl_fw_read_preboot_status(struct hl_device *hdev)
{
	int rc;

	if (!(hdev->fw_components & FW_TYPE_PREBOOT_CPU))
		return 0;

	/* get FW pre-load parameters  */
	hdev->asic_funcs->init_firmware_preload_params(hdev);

	/*
	 * In order to determine boot method (static VS dynamic) we need to
	 * read the boot caps register
	 */
	rc = hl_fw_read_preboot_caps(hdev);
	if (rc)
		return rc;

	hl_fw_preboot_update_state(hdev);

	/* no need to read preboot status in dynamic load */
	if (hdev->asic_prop.dynamic_fw_load)
		return 0;

	return hl_fw_static_read_preboot_status(hdev);
}

/* associate string with COMM status */
static char *hl_dynamic_fw_status_str[COMMS_STS_INVLD_LAST] = {
	[COMMS_STS_NOOP] = "NOOP",
	[COMMS_STS_ACK] = "ACK",
	[COMMS_STS_OK] = "OK",
	[COMMS_STS_ERR] = "ERR",
	[COMMS_STS_VALID_ERR] = "VALID_ERR",
	[COMMS_STS_TIMEOUT_ERR] = "TIMEOUT_ERR",
};

/**
 * hl_fw_dynamic_report_error_status - report error status
 *
 * @hdev: pointer to the habanalabs device structure
 * @status: value of FW status register
 * @expected_status: the expected status
 */
static void hl_fw_dynamic_report_error_status(struct hl_device *hdev,
						u32 status,
						enum comms_sts expected_status)
{
	enum comms_sts comm_status =
				FIELD_GET(COMMS_STATUS_STATUS_MASK, status);

	if (comm_status < COMMS_STS_INVLD_LAST)
		dev_err(hdev->dev, "Device status %s, expected status: %s\n",
				hl_dynamic_fw_status_str[comm_status],
				hl_dynamic_fw_status_str[expected_status]);
	else
		dev_err(hdev->dev, "Device status unknown %d, expected status: %s\n",
				comm_status,
				hl_dynamic_fw_status_str[expected_status]);
}

/**
 * hl_fw_dynamic_send_cmd - send LKD to FW cmd
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_loader: managing structure for loading device's FW
 * @cmd: LKD to FW cmd code
 * @size: size of next FW component to be loaded (0 if not necessary)
 *
 * LDK to FW exact command layout is defined at struct comms_command.
 * note: the size argument is used only when the next FW component should be
 *       loaded, otherwise it shall be 0. the size is used by the FW in later
 *       protocol stages and when sending only indicating the amount of memory
 *       to be allocated by the FW to receive the next boot component.
 */
static void hl_fw_dynamic_send_cmd(struct hl_device *hdev,
				struct fw_load_mgr *fw_loader,
				enum comms_cmd cmd, unsigned int size)
{
	struct cpu_dyn_regs *dyn_regs;
	u32 val;

	dyn_regs = &fw_loader->dynamic_loader.comm_desc.cpu_dyn_regs;

	val = FIELD_PREP(COMMS_COMMAND_CMD_MASK, cmd);
	val |= FIELD_PREP(COMMS_COMMAND_SIZE_MASK, size);

	trace_habanalabs_comms_send_cmd(hdev->dev, comms_cmd_str_arr[cmd]);
	WREG32(le32_to_cpu(dyn_regs->kmd_msg_to_cpu), val);
}

/**
 * hl_fw_dynamic_extract_fw_response - update the FW response
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_loader: managing structure for loading device's FW
 * @response: FW response
 * @status: the status read from CPU status register
 *
 * @return 0 on success, otherwise non-zero error code
 */
static int hl_fw_dynamic_extract_fw_response(struct hl_device *hdev,
						struct fw_load_mgr *fw_loader,
						struct fw_response *response,
						u32 status)
{
	response->status = FIELD_GET(COMMS_STATUS_STATUS_MASK, status);
	response->ram_offset = FIELD_GET(COMMS_STATUS_OFFSET_MASK, status) <<
						COMMS_STATUS_OFFSET_ALIGN_SHIFT;
	response->ram_type = FIELD_GET(COMMS_STATUS_RAM_TYPE_MASK, status);

	if ((response->ram_type != COMMS_SRAM) &&
					(response->ram_type != COMMS_DRAM)) {
		dev_err(hdev->dev, "FW status: invalid RAM type %u\n",
							response->ram_type);
		return -EIO;
	}

	return 0;
}

/**
 * hl_fw_dynamic_wait_for_status - wait for status in dynamic FW load
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_loader: managing structure for loading device's FW
 * @expected_status: expected status to wait for
 * @timeout: timeout for status wait
 *
 * @return 0 on success, otherwise non-zero error code
 *
 * waiting for status from FW include polling the FW status register until
 * expected status is received or timeout occurs (whatever occurs first).
 */
static int hl_fw_dynamic_wait_for_status(struct hl_device *hdev,
						struct fw_load_mgr *fw_loader,
						enum comms_sts expected_status,
						u32 timeout)
{
	struct cpu_dyn_regs *dyn_regs;
	u32 status;
	int rc;

	dyn_regs = &fw_loader->dynamic_loader.comm_desc.cpu_dyn_regs;

	trace_habanalabs_comms_wait_status(hdev->dev, comms_sts_str_arr[expected_status]);

	/* Wait for expected status */
	rc = hl_poll_timeout(
		hdev,
		le32_to_cpu(dyn_regs->cpu_cmd_status_to_host),
		status,
		FIELD_GET(COMMS_STATUS_STATUS_MASK, status) == expected_status,
		hdev->fw_comms_poll_interval_usec,
		timeout);

	if (rc) {
		hl_fw_dynamic_report_error_status(hdev, status,
							expected_status);
		return -EIO;
	}

	trace_habanalabs_comms_wait_status_done(hdev->dev, comms_sts_str_arr[expected_status]);

	/*
	 * skip storing FW response for NOOP to preserve the actual desired
	 * FW status
	 */
	if (expected_status == COMMS_STS_NOOP)
		return 0;

	rc = hl_fw_dynamic_extract_fw_response(hdev, fw_loader,
					&fw_loader->dynamic_loader.response,
					status);
	return rc;
}

/**
 * hl_fw_dynamic_send_clear_cmd - send clear command to FW
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_loader: managing structure for loading device's FW
 *
 * @return 0 on success, otherwise non-zero error code
 *
 * after command cycle between LKD to FW CPU (i.e. LKD got an expected status
 * from FW) we need to clear the CPU status register in order to avoid garbage
 * between command cycles.
 * This is done by sending clear command and polling the CPU to LKD status
 * register to hold the status NOOP
 */
static int hl_fw_dynamic_send_clear_cmd(struct hl_device *hdev,
						struct fw_load_mgr *fw_loader)
{
	hl_fw_dynamic_send_cmd(hdev, fw_loader, COMMS_CLR_STS, 0);

	return hl_fw_dynamic_wait_for_status(hdev, fw_loader, COMMS_STS_NOOP,
							fw_loader->cpu_timeout);
}

/**
 * hl_fw_dynamic_send_protocol_cmd - send LKD to FW cmd and wait for ACK
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_loader: managing structure for loading device's FW
 * @cmd: LKD to FW cmd code
 * @size: size of next FW component to be loaded (0 if not necessary)
 * @wait_ok: if true also wait for OK response from FW
 * @timeout: timeout for status wait
 *
 * @return 0 on success, otherwise non-zero error code
 *
 * brief:
 * when sending protocol command we have the following steps:
 * - send clear (clear command and verify clear status register)
 * - send the actual protocol command
 * - wait for ACK on the protocol command
 * - send clear
 * - send NOOP
 * if, in addition, the specific protocol command should wait for OK then:
 * - wait for OK
 * - send clear
 * - send NOOP
 *
 * NOTES:
 * send clear: this is necessary in order to clear the status register to avoid
 *             leftovers between command
 * NOOP command: necessary to avoid loop on the clear command by the FW
 */
int hl_fw_dynamic_send_protocol_cmd(struct hl_device *hdev,
				struct fw_load_mgr *fw_loader,
				enum comms_cmd cmd, unsigned int size,
				bool wait_ok, u32 timeout)
{
	int rc;

	trace_habanalabs_comms_protocol_cmd(hdev->dev, comms_cmd_str_arr[cmd]);

	/* first send clear command to clean former commands */
	rc = hl_fw_dynamic_send_clear_cmd(hdev, fw_loader);
	if (rc)
		return rc;

	/* send the actual command */
	hl_fw_dynamic_send_cmd(hdev, fw_loader, cmd, size);

	/* wait for ACK for the command */
	rc = hl_fw_dynamic_wait_for_status(hdev, fw_loader, COMMS_STS_ACK,
								timeout);
	if (rc)
		return rc;

	/* clear command to prepare for NOOP command */
	rc = hl_fw_dynamic_send_clear_cmd(hdev, fw_loader);
	if (rc)
		return rc;

	/* send the actual NOOP command */
	hl_fw_dynamic_send_cmd(hdev, fw_loader, COMMS_NOOP, 0);

	if (!wait_ok)
		return 0;

	rc = hl_fw_dynamic_wait_for_status(hdev, fw_loader, COMMS_STS_OK,
								timeout);
	if (rc)
		return rc;

	/* clear command to prepare for NOOP command */
	rc = hl_fw_dynamic_send_clear_cmd(hdev, fw_loader);
	if (rc)
		return rc;

	/* send the actual NOOP command */
	hl_fw_dynamic_send_cmd(hdev, fw_loader, COMMS_NOOP, 0);

	return 0;
}

/**
 * hl_fw_compat_crc32 - CRC compatible with FW
 *
 * @data: pointer to the data
 * @size: size of the data
 *
 * @return the CRC32 result
 *
 * NOTE: kernel's CRC32 differs from standard CRC32 calculation.
 *       in order to be aligned we need to flip the bits of both the input
 *       initial CRC and kernel's CRC32 result.
 *       in addition both sides use initial CRC of 0,
 */
static u32 hl_fw_compat_crc32(u8 *data, size_t size)
{
	return ~crc32_le(~((u32)0), data, size);
}

/**
 * hl_fw_dynamic_validate_memory_bound - validate memory bounds for memory
 *                                        transfer (image or descriptor) between
 *                                        host and FW
 *
 * @hdev: pointer to the habanalabs device structure
 * @addr: device address of memory transfer
 * @size: memory transfer size
 * @region: PCI memory region
 *
 * @return 0 on success, otherwise non-zero error code
 */
static int hl_fw_dynamic_validate_memory_bound(struct hl_device *hdev,
						u64 addr, size_t size,
						struct pci_mem_region *region)
{
	u64 end_addr;

	/* now make sure that the memory transfer is within region's bounds */
	end_addr = addr + size;
	if (end_addr >= region->region_base + region->region_size) {
		dev_err(hdev->dev,
			"dynamic FW load: memory transfer end address out of memory region bounds. addr: %llx\n",
							end_addr);
		return -EIO;
	}

	/*
	 * now make sure memory transfer is within predefined BAR bounds.
	 * this is to make sure we do not need to set the bar (e.g. for DRAM
	 * memory transfers)
	 */
	if (end_addr >= region->region_base - region->offset_in_bar +
							region->bar_size) {
		dev_err(hdev->dev,
			"FW image beyond PCI BAR bounds\n");
		return -EIO;
	}

	return 0;
}

/**
 * hl_fw_dynamic_validate_descriptor - validate FW descriptor
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_loader: managing structure for loading device's FW
 * @fw_desc: the descriptor from FW
 *
 * @return 0 on success, otherwise non-zero error code
 */
static int hl_fw_dynamic_validate_descriptor(struct hl_device *hdev,
					struct fw_load_mgr *fw_loader,
					struct lkd_fw_comms_desc *fw_desc)
{
	struct pci_mem_region *region;
	enum pci_region region_id;
	size_t data_size;
	u32 data_crc32;
	u8 *data_ptr;
	u64 addr;
	int rc;

	if (le32_to_cpu(fw_desc->header.magic) != HL_COMMS_DESC_MAGIC)
		dev_dbg(hdev->dev, "Invalid magic for dynamic FW descriptor (%x)\n",
				fw_desc->header.magic);

	if (fw_desc->header.version != HL_COMMS_DESC_VER)
		dev_dbg(hdev->dev, "Invalid version for dynamic FW descriptor (%x)\n",
				fw_desc->header.version);

	/*
	 * Calc CRC32 of data without header. use the size of the descriptor
	 * reported by firmware, without calculating it ourself, to allow adding
	 * more fields to the lkd_fw_comms_desc structure.
	 * note that no alignment/stride address issues here as all structures
	 * are 64 bit padded.
	 */
	data_ptr = (u8 *)fw_desc + sizeof(struct comms_desc_header);
	data_size = le16_to_cpu(fw_desc->header.size);

	data_crc32 = hl_fw_compat_crc32(data_ptr, data_size);
	if (data_crc32 != le32_to_cpu(fw_desc->header.crc32)) {
		dev_err(hdev->dev, "CRC32 mismatch for dynamic FW descriptor (%x:%x)\n",
			data_crc32, fw_desc->header.crc32);
		return -EIO;
	}

	/* find memory region to which to copy the image */
	addr = le64_to_cpu(fw_desc->img_addr);
	region_id = hl_get_pci_memory_region(hdev, addr);
	if ((region_id != PCI_REGION_SRAM) && ((region_id != PCI_REGION_DRAM))) {
		dev_err(hdev->dev, "Invalid region to copy FW image address=%llx\n", addr);
		return -EIO;
	}

	region = &hdev->pci_mem_region[region_id];

	/* store the region for the copy stage */
	fw_loader->dynamic_loader.image_region = region;

	/*
	 * here we know that the start address is valid, now make sure that the
	 * image is within region's bounds
	 */
	rc = hl_fw_dynamic_validate_memory_bound(hdev, addr,
					fw_loader->dynamic_loader.fw_image_size,
					region);
	if (rc) {
		dev_err(hdev->dev, "invalid mem transfer request for FW image\n");
		return rc;
	}

	/* here we can mark the descriptor as valid as the content has been validated */
	fw_loader->dynamic_loader.fw_desc_valid = true;

	return 0;
}

static int hl_fw_dynamic_validate_response(struct hl_device *hdev,
						struct fw_response *response,
						struct pci_mem_region *region)
{
	u64 device_addr;
	int rc;

	device_addr = region->region_base + response->ram_offset;

	/*
	 * validate that the descriptor is within region's bounds
	 * Note that as the start address was supplied according to the RAM
	 * type- testing only the end address is enough
	 */
	rc = hl_fw_dynamic_validate_memory_bound(hdev, device_addr,
					sizeof(struct lkd_fw_comms_desc),
					region);
	return rc;
}

/*
 * hl_fw_dynamic_read_descriptor_msg - read and show the ascii msg that sent by fw
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_desc: the descriptor from FW
 */
static void hl_fw_dynamic_read_descriptor_msg(struct hl_device *hdev,
					struct lkd_fw_comms_desc *fw_desc)
{
	int i;
	char *msg;

	for (i = 0 ; i < LKD_FW_ASCII_MSG_MAX ; i++) {
		if (!fw_desc->ascii_msg[i].valid)
			return;

		/* force NULL termination */
		msg = fw_desc->ascii_msg[i].msg;
		msg[LKD_FW_ASCII_MSG_MAX_LEN - 1] = '\0';

		switch (fw_desc->ascii_msg[i].msg_lvl) {
		case LKD_FW_ASCII_MSG_ERR:
			dev_err(hdev->dev, "fw: %s", fw_desc->ascii_msg[i].msg);
			break;
		case LKD_FW_ASCII_MSG_WRN:
			dev_warn(hdev->dev, "fw: %s", fw_desc->ascii_msg[i].msg);
			break;
		case LKD_FW_ASCII_MSG_INF:
			dev_info(hdev->dev, "fw: %s", fw_desc->ascii_msg[i].msg);
			break;
		default:
			dev_dbg(hdev->dev, "fw: %s", fw_desc->ascii_msg[i].msg);
			break;
		}
	}
}

/**
 * hl_fw_dynamic_read_and_validate_descriptor - read and validate FW descriptor
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_loader: managing structure for loading device's FW
 *
 * @return 0 on success, otherwise non-zero error code
 */
static int hl_fw_dynamic_read_and_validate_descriptor(struct hl_device *hdev,
						struct fw_load_mgr *fw_loader)
{
	struct lkd_fw_comms_desc *fw_desc;
	struct pci_mem_region *region;
	struct fw_response *response;
	void *temp_fw_desc;
	void __iomem *src;
	u16 fw_data_size;
	enum pci_region region_id;
	int rc;

	fw_desc = &fw_loader->dynamic_loader.comm_desc;
	response = &fw_loader->dynamic_loader.response;

	region_id = (response->ram_type == COMMS_SRAM) ?
					PCI_REGION_SRAM : PCI_REGION_DRAM;

	region = &hdev->pci_mem_region[region_id];

	rc = hl_fw_dynamic_validate_response(hdev, response, region);
	if (rc) {
		dev_err(hdev->dev,
			"invalid mem transfer request for FW descriptor\n");
		return rc;
	}

	/*
	 * extract address to copy the descriptor from
	 * in addition, as the descriptor value is going to be over-ridden by new data- we mark it
	 * as invalid.
	 * it will be marked again as valid once validated
	 */
	fw_loader->dynamic_loader.fw_desc_valid = false;
	src = hdev->pcie_bar[region->bar_id] + region->offset_in_bar +
							response->ram_offset;

	/*
	 * We do the copy of the fw descriptor in 2 phases:
	 * 1. copy the header + data info according to our lkd_fw_comms_desc definition.
	 *    then we're able to read the actual data size provided by fw.
	 *    this is needed for cases where data in descriptor was changed(add/remove)
	 *    in embedded specs header file before updating lkd copy of the header file
	 * 2. copy descriptor to temporary buffer with aligned size and send it to validation
	 */
	memcpy_fromio(fw_desc, src, sizeof(struct lkd_fw_comms_desc));
	fw_data_size = le16_to_cpu(fw_desc->header.size);

	temp_fw_desc = vzalloc(sizeof(struct comms_desc_header) + fw_data_size);
	if (!temp_fw_desc)
		return -ENOMEM;

	memcpy_fromio(temp_fw_desc, src, sizeof(struct comms_desc_header) + fw_data_size);

	rc = hl_fw_dynamic_validate_descriptor(hdev, fw_loader,
					(struct lkd_fw_comms_desc *) temp_fw_desc);

	if (!rc)
		hl_fw_dynamic_read_descriptor_msg(hdev, temp_fw_desc);

	vfree(temp_fw_desc);

	return rc;
}

/**
 * hl_fw_dynamic_request_descriptor - handshake with CPU to get FW descriptor
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_loader: managing structure for loading device's FW
 * @next_image_size: size to allocate for next FW component
 *
 * @return 0 on success, otherwise non-zero error code
 */
static int hl_fw_dynamic_request_descriptor(struct hl_device *hdev,
						struct fw_load_mgr *fw_loader,
						size_t next_image_size)
{
	int rc;

	rc = hl_fw_dynamic_send_protocol_cmd(hdev, fw_loader, COMMS_PREP_DESC,
						next_image_size, true,
						fw_loader->cpu_timeout);
	if (rc)
		return rc;

	return hl_fw_dynamic_read_and_validate_descriptor(hdev, fw_loader);
}

/**
 * hl_fw_dynamic_read_device_fw_version - read FW version to exposed properties
 *
 * @hdev: pointer to the habanalabs device structure
 * @fwc: the firmware component
 * @fw_version: fw component's version string
 */
static int hl_fw_dynamic_read_device_fw_version(struct hl_device *hdev,
					enum hl_fw_component fwc,
					const char *fw_version)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	char *preboot_ver, *boot_ver;
	char btl_ver[32];
	int rc;

	switch (fwc) {
	case FW_COMP_BOOT_FIT:
		strscpy(prop->uboot_ver, fw_version, VERSION_MAX_LEN);
		boot_ver = extract_fw_ver_from_str(prop->uboot_ver);
		if (boot_ver) {
			dev_info(hdev->dev, "boot-fit version %s\n", boot_ver);
			kfree(boot_ver);
		}

		break;
	case FW_COMP_PREBOOT:
		strscpy(prop->preboot_ver, fw_version, VERSION_MAX_LEN);
		preboot_ver = strnstr(prop->preboot_ver, "Preboot", VERSION_MAX_LEN);
		dev_info(hdev->dev, "preboot full version: '%s'\n", preboot_ver);

		if (preboot_ver && preboot_ver != prop->preboot_ver) {
			strscpy(btl_ver, prop->preboot_ver,
				min((int) (preboot_ver - prop->preboot_ver), 31));
			dev_info(hdev->dev, "%s\n", btl_ver);
		}

		rc = hl_get_sw_major_minor_subminor(hdev, preboot_ver);
		if (rc)
			return rc;
		preboot_ver = extract_fw_ver_from_str(prop->preboot_ver);
		if (preboot_ver) {
			rc = hl_get_preboot_major_minor(hdev, preboot_ver);
			kfree(preboot_ver);
			if (rc)
				return rc;
		}

		break;
	default:
		dev_warn(hdev->dev, "Undefined FW component: %d\n", fwc);
		return -EINVAL;
	}

	return 0;
}

/**
 * hl_fw_dynamic_copy_image - copy image to memory allocated by the FW
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw: fw descriptor
 * @fw_loader: managing structure for loading device's FW
 */
static int hl_fw_dynamic_copy_image(struct hl_device *hdev,
						const struct firmware *fw,
						struct fw_load_mgr *fw_loader)
{
	struct lkd_fw_comms_desc *fw_desc;
	struct pci_mem_region *region;
	void __iomem *dest;
	u64 addr;
	int rc;

	fw_desc = &fw_loader->dynamic_loader.comm_desc;
	addr = le64_to_cpu(fw_desc->img_addr);

	/* find memory region to which to copy the image */
	region = fw_loader->dynamic_loader.image_region;

	dest = hdev->pcie_bar[region->bar_id] + region->offset_in_bar +
					(addr - region->region_base);

	rc = hl_fw_copy_fw_to_device(hdev, fw, dest,
					fw_loader->boot_fit_img.src_off,
					fw_loader->boot_fit_img.copy_size);

	return rc;
}

/**
 * hl_fw_dynamic_copy_msg - copy msg to memory allocated by the FW
 *
 * @hdev: pointer to the habanalabs device structure
 * @msg: message
 * @fw_loader: managing structure for loading device's FW
 */
static int hl_fw_dynamic_copy_msg(struct hl_device *hdev,
		struct lkd_msg_comms *msg, struct fw_load_mgr *fw_loader)
{
	struct lkd_fw_comms_desc *fw_desc;
	struct pci_mem_region *region;
	void __iomem *dest;
	u64 addr;
	int rc;

	fw_desc = &fw_loader->dynamic_loader.comm_desc;
	addr = le64_to_cpu(fw_desc->img_addr);

	/* find memory region to which to copy the image */
	region = fw_loader->dynamic_loader.image_region;

	dest = hdev->pcie_bar[region->bar_id] + region->offset_in_bar +
					(addr - region->region_base);

	rc = hl_fw_copy_msg_to_device(hdev, msg, dest, 0, 0);

	return rc;
}

/**
 * hl_fw_boot_fit_update_state - update internal data structures after boot-fit
 *                               is loaded
 *
 * @hdev: pointer to the habanalabs device structure
 * @cpu_boot_dev_sts0_reg: register holding CPU boot dev status 0
 * @cpu_boot_dev_sts1_reg: register holding CPU boot dev status 1
 *
 * @return 0 on success, otherwise non-zero error code
 */
static void hl_fw_boot_fit_update_state(struct hl_device *hdev,
						u32 cpu_boot_dev_sts0_reg,
						u32 cpu_boot_dev_sts1_reg)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	hdev->fw_loader.fw_comp_loaded |= FW_TYPE_BOOT_CPU;

	/* Read boot_cpu status bits */
	if (prop->fw_preboot_cpu_boot_dev_sts0 & CPU_BOOT_DEV_STS0_ENABLED) {
		prop->fw_bootfit_cpu_boot_dev_sts0 =
				RREG32(cpu_boot_dev_sts0_reg);

		prop->hard_reset_done_by_fw = !!(prop->fw_bootfit_cpu_boot_dev_sts0 &
							CPU_BOOT_DEV_STS0_FW_HARD_RST_EN);

		dev_dbg(hdev->dev, "Firmware boot CPU status0 %#x\n",
					prop->fw_bootfit_cpu_boot_dev_sts0);
	}

	if (prop->fw_cpu_boot_dev_sts1_valid) {
		prop->fw_bootfit_cpu_boot_dev_sts1 =
				RREG32(cpu_boot_dev_sts1_reg);

		dev_dbg(hdev->dev, "Firmware boot CPU status1 %#x\n",
					prop->fw_bootfit_cpu_boot_dev_sts1);
	}

	dev_dbg(hdev->dev, "Firmware boot CPU hard-reset is %s\n",
			prop->hard_reset_done_by_fw ? "enabled" : "disabled");
}

static void hl_fw_dynamic_update_linux_interrupt_if(struct hl_device *hdev)
{
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;

	/* Check whether all 3 interrupt interfaces are set, if not use a
	 * single interface
	 */
	if (!hdev->asic_prop.gic_interrupts_enable &&
			!(hdev->asic_prop.fw_app_cpu_boot_dev_sts0 &
				CPU_BOOT_DEV_STS0_MULTI_IRQ_POLL_EN)) {
		dyn_regs->gic_host_halt_irq = dyn_regs->gic_host_pi_upd_irq;
		dyn_regs->gic_host_ints_irq = dyn_regs->gic_host_pi_upd_irq;

		dev_warn(hdev->dev,
			"Using a single interrupt interface towards cpucp");
	}
}
/**
 * hl_fw_dynamic_load_image - load FW image using dynamic protocol
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_loader: managing structure for loading device's FW
 * @load_fwc: the FW component to be loaded
 * @img_ld_timeout: image load timeout
 *
 * @return 0 on success, otherwise non-zero error code
 */
static int hl_fw_dynamic_load_image(struct hl_device *hdev,
						struct fw_load_mgr *fw_loader,
						enum hl_fw_component load_fwc,
						u32 img_ld_timeout)
{
	enum hl_fw_component cur_fwc;
	const struct firmware *fw;
	char *fw_name;
	int rc = 0;

	/*
	 * when loading image we have one of 2 scenarios:
	 * 1. current FW component is preboot and we want to load boot-fit
	 * 2. current FW component is boot-fit and we want to load linux
	 */
	if (load_fwc == FW_COMP_BOOT_FIT) {
		cur_fwc = FW_COMP_PREBOOT;
		fw_name = fw_loader->boot_fit_img.image_name;
	} else {
		cur_fwc = FW_COMP_BOOT_FIT;
		fw_name = fw_loader->linux_img.image_name;
	}

	/* request FW in order to communicate to FW the size to be allocated */
	rc = hl_request_fw(hdev, &fw, fw_name);
	if (rc)
		return rc;

	/* store the image size for future validation */
	fw_loader->dynamic_loader.fw_image_size = fw->size;

	rc = hl_fw_dynamic_request_descriptor(hdev, fw_loader, fw->size);
	if (rc)
		goto release_fw;

	/* read preboot version */
	rc = hl_fw_dynamic_read_device_fw_version(hdev, cur_fwc,
				fw_loader->dynamic_loader.comm_desc.cur_fw_ver);
	if (rc)
		goto release_fw;

	/* copy boot fit to space allocated by FW */
	rc = hl_fw_dynamic_copy_image(hdev, fw, fw_loader);
	if (rc)
		goto release_fw;

	rc = hl_fw_dynamic_send_protocol_cmd(hdev, fw_loader, COMMS_DATA_RDY,
						0, true,
						fw_loader->cpu_timeout);
	if (rc)
		goto release_fw;

	rc = hl_fw_dynamic_send_protocol_cmd(hdev, fw_loader, COMMS_EXEC,
						0, false,
						img_ld_timeout);

release_fw:
	hl_release_firmware(fw);
	return rc;
}

static int hl_fw_dynamic_wait_for_boot_fit_active(struct hl_device *hdev,
					struct fw_load_mgr *fw_loader)
{
	struct dynamic_fw_load_mgr *dyn_loader;
	u32 status;
	int rc;

	dyn_loader = &fw_loader->dynamic_loader;

	/*
	 * Make sure CPU boot-loader is running
	 * Note that the CPU_BOOT_STATUS_SRAM_AVAIL is generally set by Linux
	 * yet there is a debug scenario in which we loading uboot (without Linux)
	 * which at later stage is relocated to DRAM. In this case we expect
	 * uboot to set the CPU_BOOT_STATUS_SRAM_AVAIL and so we add it to the
	 * poll flags
	 */
	rc = hl_poll_timeout(
		hdev,
		le32_to_cpu(dyn_loader->comm_desc.cpu_dyn_regs.cpu_boot_status),
		status,
		(status == CPU_BOOT_STATUS_READY_TO_BOOT) ||
		(status == CPU_BOOT_STATUS_SRAM_AVAIL),
		hdev->fw_poll_interval_usec,
		dyn_loader->wait_for_bl_timeout);
	if (rc) {
		dev_err(hdev->dev, "failed to wait for boot (status = %d)\n", status);
		return rc;
	}

	dev_dbg(hdev->dev, "uboot status = %d\n", status);
	return 0;
}

static int hl_fw_dynamic_wait_for_linux_active(struct hl_device *hdev,
						struct fw_load_mgr *fw_loader)
{
	struct dynamic_fw_load_mgr *dyn_loader;
	u32 status;
	int rc;

	dyn_loader = &fw_loader->dynamic_loader;

	/* Make sure CPU linux is running */

	rc = hl_poll_timeout(
		hdev,
		le32_to_cpu(dyn_loader->comm_desc.cpu_dyn_regs.cpu_boot_status),
		status,
		(status == CPU_BOOT_STATUS_SRAM_AVAIL),
		hdev->fw_poll_interval_usec,
		fw_loader->cpu_timeout);
	if (rc) {
		dev_err(hdev->dev, "failed to wait for Linux (status = %d)\n", status);
		return rc;
	}

	dev_dbg(hdev->dev, "Boot status = %d\n", status);
	return 0;
}

/**
 * hl_fw_linux_update_state -	update internal data structures after Linux
 *				is loaded.
 *				Note: Linux initialization is comprised mainly
 *				of two stages - loading kernel (SRAM_AVAIL)
 *				& loading ARMCP.
 *				Therefore reading boot device status in any of
 *				these stages might result in different values.
 *
 * @hdev: pointer to the habanalabs device structure
 * @cpu_boot_dev_sts0_reg: register holding CPU boot dev status 0
 * @cpu_boot_dev_sts1_reg: register holding CPU boot dev status 1
 *
 * @return 0 on success, otherwise non-zero error code
 */
static void hl_fw_linux_update_state(struct hl_device *hdev,
						u32 cpu_boot_dev_sts0_reg,
						u32 cpu_boot_dev_sts1_reg)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	hdev->fw_loader.fw_comp_loaded |= FW_TYPE_LINUX;

	/* Read FW application security bits */
	if (prop->fw_cpu_boot_dev_sts0_valid) {
		prop->fw_app_cpu_boot_dev_sts0 = RREG32(cpu_boot_dev_sts0_reg);

		prop->hard_reset_done_by_fw = !!(prop->fw_app_cpu_boot_dev_sts0 &
							CPU_BOOT_DEV_STS0_FW_HARD_RST_EN);

		if (prop->fw_app_cpu_boot_dev_sts0 &
				CPU_BOOT_DEV_STS0_GIC_PRIVILEGED_EN)
			prop->gic_interrupts_enable = false;

		dev_dbg(hdev->dev,
			"Firmware application CPU status0 %#x\n",
			prop->fw_app_cpu_boot_dev_sts0);

		dev_dbg(hdev->dev, "GIC controller is %s\n",
				prop->gic_interrupts_enable ?
						"enabled" : "disabled");
	}

	if (prop->fw_cpu_boot_dev_sts1_valid) {
		prop->fw_app_cpu_boot_dev_sts1 = RREG32(cpu_boot_dev_sts1_reg);

		dev_dbg(hdev->dev,
			"Firmware application CPU status1 %#x\n",
			prop->fw_app_cpu_boot_dev_sts1);
	}

	dev_dbg(hdev->dev, "Firmware application CPU hard-reset is %s\n",
			prop->hard_reset_done_by_fw ? "enabled" : "disabled");

	dev_info(hdev->dev, "Successfully loaded firmware to device\n");
}

/**
 * hl_fw_dynamic_send_msg - send a COMMS message with attached data
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_loader: managing structure for loading device's FW
 * @msg_type: message type
 * @data: data to be sent
 *
 * @return 0 on success, otherwise non-zero error code
 */
static int hl_fw_dynamic_send_msg(struct hl_device *hdev,
		struct fw_load_mgr *fw_loader, u8 msg_type, void *data)
{
	struct lkd_msg_comms *msg;
	int rc;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	/* create message to be sent */
	msg->header.type = msg_type;
	msg->header.size = cpu_to_le16(sizeof(struct comms_msg_header));
	msg->header.magic = cpu_to_le32(HL_COMMS_MSG_MAGIC);

	switch (msg_type) {
	case HL_COMMS_RESET_CAUSE_TYPE:
		msg->reset_cause = *(__u8 *) data;
		break;

	default:
		dev_err(hdev->dev,
			"Send COMMS message - invalid message type %u\n",
			msg_type);
		rc = -EINVAL;
		goto out;
	}

	rc = hl_fw_dynamic_request_descriptor(hdev, fw_loader,
			sizeof(struct lkd_msg_comms));
	if (rc)
		goto out;

	/* copy message to space allocated by FW */
	rc = hl_fw_dynamic_copy_msg(hdev, msg, fw_loader);
	if (rc)
		goto out;

	rc = hl_fw_dynamic_send_protocol_cmd(hdev, fw_loader, COMMS_DATA_RDY,
						0, true,
						fw_loader->cpu_timeout);
	if (rc)
		goto out;

	rc = hl_fw_dynamic_send_protocol_cmd(hdev, fw_loader, COMMS_EXEC,
						0, true,
						fw_loader->cpu_timeout);

out:
	kfree(msg);
	return rc;
}

/**
 * hl_fw_dynamic_init_cpu - initialize the device CPU using dynamic protocol
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_loader: managing structure for loading device's FW
 *
 * @return 0 on success, otherwise non-zero error code
 *
 * brief: the dynamic protocol is master (LKD) slave (FW CPU) protocol.
 * the communication is done using registers:
 * - LKD command register
 * - FW status register
 * the protocol is race free. this goal is achieved by splitting the requests
 * and response to known synchronization points between the LKD and the FW.
 * each response to LKD request is known and bound to a predefined timeout.
 * in case of timeout expiration without the desired status from FW- the
 * protocol (and hence the boot) will fail.
 */
static int hl_fw_dynamic_init_cpu(struct hl_device *hdev,
					struct fw_load_mgr *fw_loader)
{
	struct cpu_dyn_regs *dyn_regs;
	int rc, fw_error_rc;

	dev_info(hdev->dev,
		"Loading %sfirmware to device, may take some time...\n",
		hdev->asic_prop.fw_security_enabled ? "secured " : "");

	/* initialize FW descriptor as invalid */
	fw_loader->dynamic_loader.fw_desc_valid = false;

	/*
	 * In this stage, "cpu_dyn_regs" contains only LKD's hard coded values!
	 * It will be updated from FW after hl_fw_dynamic_request_descriptor().
	 */
	dyn_regs = &fw_loader->dynamic_loader.comm_desc.cpu_dyn_regs;

	rc = hl_fw_dynamic_send_protocol_cmd(hdev, fw_loader, COMMS_RST_STATE,
						0, true,
						fw_loader->cpu_timeout);
	if (rc)
		goto protocol_err;

	if (hdev->reset_info.curr_reset_cause) {
		rc = hl_fw_dynamic_send_msg(hdev, fw_loader,
				HL_COMMS_RESET_CAUSE_TYPE, &hdev->reset_info.curr_reset_cause);
		if (rc)
			goto protocol_err;

		/* Clear current reset cause */
		hdev->reset_info.curr_reset_cause = HL_RESET_CAUSE_UNKNOWN;
	}

	if (!(hdev->fw_components & FW_TYPE_BOOT_CPU)) {
		struct lkd_fw_binning_info *binning_info;

		rc = hl_fw_dynamic_request_descriptor(hdev, fw_loader, 0);
		if (rc)
			goto protocol_err;

		/* read preboot version */
		rc = hl_fw_dynamic_read_device_fw_version(hdev, FW_COMP_PREBOOT,
				fw_loader->dynamic_loader.comm_desc.cur_fw_ver);

		if (rc)
			return rc;

		/* read binning info from preboot */
		if (hdev->support_preboot_binning) {
			binning_info = &fw_loader->dynamic_loader.comm_desc.binning_info;
			hdev->tpc_binning = le64_to_cpu(binning_info->tpc_mask_l);
			hdev->dram_binning = le32_to_cpu(binning_info->dram_mask);
			hdev->edma_binning = le32_to_cpu(binning_info->edma_mask);
			hdev->decoder_binning = le32_to_cpu(binning_info->dec_mask);
			hdev->rotator_binning = le32_to_cpu(binning_info->rot_mask);

			rc = hdev->asic_funcs->set_dram_properties(hdev);
			if (rc)
				return rc;

			rc = hdev->asic_funcs->set_binning_masks(hdev);
			if (rc)
				return rc;

			dev_dbg(hdev->dev,
				"Read binning masks: tpc: 0x%llx, dram: 0x%llx, edma: 0x%x, dec: 0x%x, rot:0x%x\n",
				hdev->tpc_binning, hdev->dram_binning, hdev->edma_binning,
				hdev->decoder_binning, hdev->rotator_binning);
		}

		return 0;
	}

	/* load boot fit to FW */
	rc = hl_fw_dynamic_load_image(hdev, fw_loader, FW_COMP_BOOT_FIT,
						fw_loader->boot_fit_timeout);
	if (rc) {
		dev_err(hdev->dev, "failed to load boot fit\n");
		goto protocol_err;
	}

	rc = hl_fw_dynamic_wait_for_boot_fit_active(hdev, fw_loader);
	if (rc)
		goto protocol_err;

	hl_fw_boot_fit_update_state(hdev,
			le32_to_cpu(dyn_regs->cpu_boot_dev_sts0),
			le32_to_cpu(dyn_regs->cpu_boot_dev_sts1));

	/*
	 * when testing FW load (without Linux) on PLDM we don't want to
	 * wait until boot fit is active as it may take several hours.
	 * instead, we load the bootfit and let it do all initialization in
	 * the background.
	 */
	if (hdev->pldm && !(hdev->fw_components & FW_TYPE_LINUX))
		return 0;

	/* Enable DRAM scrambling before Linux boot and after successful
	 *  UBoot
	 */
	hdev->asic_funcs->init_cpu_scrambler_dram(hdev);

	if (!(hdev->fw_components & FW_TYPE_LINUX)) {
		dev_info(hdev->dev, "Skip loading Linux F/W\n");
		return 0;
	}

	if (fw_loader->skip_bmc) {
		rc = hl_fw_dynamic_send_protocol_cmd(hdev, fw_loader,
							COMMS_SKIP_BMC, 0,
							true,
							fw_loader->cpu_timeout);
		if (rc) {
			dev_err(hdev->dev, "failed to load boot fit\n");
			goto protocol_err;
		}
	}

	/* load Linux image to FW */
	rc = hl_fw_dynamic_load_image(hdev, fw_loader, FW_COMP_LINUX,
							fw_loader->cpu_timeout);
	if (rc) {
		dev_err(hdev->dev, "failed to load Linux\n");
		goto protocol_err;
	}

	rc = hl_fw_dynamic_wait_for_linux_active(hdev, fw_loader);
	if (rc)
		goto protocol_err;

	hl_fw_linux_update_state(hdev,
				le32_to_cpu(dyn_regs->cpu_boot_dev_sts0),
				le32_to_cpu(dyn_regs->cpu_boot_dev_sts1));

	hl_fw_dynamic_update_linux_interrupt_if(hdev);

protocol_err:
	if (fw_loader->dynamic_loader.fw_desc_valid) {
		fw_error_rc = fw_read_errors(hdev, le32_to_cpu(dyn_regs->cpu_boot_err0),
				le32_to_cpu(dyn_regs->cpu_boot_err1),
				le32_to_cpu(dyn_regs->cpu_boot_dev_sts0),
				le32_to_cpu(dyn_regs->cpu_boot_dev_sts1));

		if (fw_error_rc)
			return fw_error_rc;
	}

	return rc;
}

/**
 * hl_fw_static_init_cpu - initialize the device CPU using static protocol
 *
 * @hdev: pointer to the habanalabs device structure
 * @fw_loader: managing structure for loading device's FW
 *
 * @return 0 on success, otherwise non-zero error code
 */
static int hl_fw_static_init_cpu(struct hl_device *hdev,
					struct fw_load_mgr *fw_loader)
{
	u32 cpu_msg_status_reg, cpu_timeout, msg_to_cpu_reg, status;
	u32 cpu_boot_dev_status0_reg, cpu_boot_dev_status1_reg;
	struct static_fw_load_mgr *static_loader;
	u32 cpu_boot_status_reg;
	int rc;

	if (!(hdev->fw_components & FW_TYPE_BOOT_CPU))
		return 0;

	/* init common loader parameters */
	cpu_timeout = fw_loader->cpu_timeout;

	/* init static loader parameters */
	static_loader = &fw_loader->static_loader;
	cpu_msg_status_reg = static_loader->cpu_cmd_status_to_host_reg;
	msg_to_cpu_reg = static_loader->kmd_msg_to_cpu_reg;
	cpu_boot_dev_status0_reg = static_loader->cpu_boot_dev_status0_reg;
	cpu_boot_dev_status1_reg = static_loader->cpu_boot_dev_status1_reg;
	cpu_boot_status_reg = static_loader->cpu_boot_status_reg;

	dev_info(hdev->dev, "Going to wait for device boot (up to %lds)\n",
		cpu_timeout / USEC_PER_SEC);

	/* Wait for boot FIT request */
	rc = hl_poll_timeout(
		hdev,
		cpu_boot_status_reg,
		status,
		status == CPU_BOOT_STATUS_WAITING_FOR_BOOT_FIT,
		hdev->fw_poll_interval_usec,
		fw_loader->boot_fit_timeout);

	if (rc) {
		dev_dbg(hdev->dev,
			"No boot fit request received (status = %d), resuming boot\n", status);
	} else {
		rc = hdev->asic_funcs->load_boot_fit_to_device(hdev);
		if (rc)
			goto out;

		/* Clear device CPU message status */
		WREG32(cpu_msg_status_reg, CPU_MSG_CLR);

		/* Signal device CPU that boot loader is ready */
		WREG32(msg_to_cpu_reg, KMD_MSG_FIT_RDY);

		/* Poll for CPU device ack */
		rc = hl_poll_timeout(
			hdev,
			cpu_msg_status_reg,
			status,
			status == CPU_MSG_OK,
			hdev->fw_poll_interval_usec,
			fw_loader->boot_fit_timeout);

		if (rc) {
			dev_err(hdev->dev,
				"Timeout waiting for boot fit load ack (status = %d)\n", status);
			goto out;
		}

		/* Clear message */
		WREG32(msg_to_cpu_reg, KMD_MSG_NA);
	}

	/*
	 * Make sure CPU boot-loader is running
	 * Note that the CPU_BOOT_STATUS_SRAM_AVAIL is generally set by Linux
	 * yet there is a debug scenario in which we loading uboot (without Linux)
	 * which at later stage is relocated to DRAM. In this case we expect
	 * uboot to set the CPU_BOOT_STATUS_SRAM_AVAIL and so we add it to the
	 * poll flags
	 */
	rc = hl_poll_timeout(
		hdev,
		cpu_boot_status_reg,
		status,
		(status == CPU_BOOT_STATUS_DRAM_RDY) ||
		(status == CPU_BOOT_STATUS_NIC_FW_RDY) ||
		(status == CPU_BOOT_STATUS_READY_TO_BOOT) ||
		(status == CPU_BOOT_STATUS_SRAM_AVAIL),
		hdev->fw_poll_interval_usec,
		cpu_timeout);

	dev_dbg(hdev->dev, "uboot status = %d\n", status);

	/* Read U-Boot version now in case we will later fail */
	hl_fw_static_read_device_fw_version(hdev, FW_COMP_BOOT_FIT);

	/* update state according to boot stage */
	hl_fw_boot_fit_update_state(hdev, cpu_boot_dev_status0_reg,
						cpu_boot_dev_status1_reg);

	if (rc) {
		detect_cpu_boot_status(hdev, status);
		rc = -EIO;
		goto out;
	}

	/* Enable DRAM scrambling before Linux boot and after successful
	 *  UBoot
	 */
	hdev->asic_funcs->init_cpu_scrambler_dram(hdev);

	if (!(hdev->fw_components & FW_TYPE_LINUX)) {
		dev_info(hdev->dev, "Skip loading Linux F/W\n");
		rc = 0;
		goto out;
	}

	if (status == CPU_BOOT_STATUS_SRAM_AVAIL) {
		rc = 0;
		goto out;
	}

	dev_info(hdev->dev,
		"Loading firmware to device, may take some time...\n");

	rc = hdev->asic_funcs->load_firmware_to_device(hdev);
	if (rc)
		goto out;

	if (fw_loader->skip_bmc) {
		WREG32(msg_to_cpu_reg, KMD_MSG_SKIP_BMC);

		rc = hl_poll_timeout(
			hdev,
			cpu_boot_status_reg,
			status,
			(status == CPU_BOOT_STATUS_BMC_WAITING_SKIPPED),
			hdev->fw_poll_interval_usec,
			cpu_timeout);

		if (rc) {
			dev_err(hdev->dev,
				"Failed to get ACK on skipping BMC (status = %d)\n",
				status);
			WREG32(msg_to_cpu_reg, KMD_MSG_NA);
			rc = -EIO;
			goto out;
		}
	}

	WREG32(msg_to_cpu_reg, KMD_MSG_FIT_RDY);

	rc = hl_poll_timeout(
		hdev,
		cpu_boot_status_reg,
		status,
		(status == CPU_BOOT_STATUS_SRAM_AVAIL),
		hdev->fw_poll_interval_usec,
		cpu_timeout);

	/* Clear message */
	WREG32(msg_to_cpu_reg, KMD_MSG_NA);

	if (rc) {
		if (status == CPU_BOOT_STATUS_FIT_CORRUPTED)
			dev_err(hdev->dev,
				"Device reports FIT image is corrupted\n");
		else
			dev_err(hdev->dev,
				"Failed to load firmware to device (status = %d)\n",
				status);

		rc = -EIO;
		goto out;
	}

	rc = fw_read_errors(hdev, fw_loader->static_loader.boot_err0_reg,
					fw_loader->static_loader.boot_err1_reg,
					cpu_boot_dev_status0_reg,
					cpu_boot_dev_status1_reg);
	if (rc)
		return rc;

	hl_fw_linux_update_state(hdev, cpu_boot_dev_status0_reg,
						cpu_boot_dev_status1_reg);

	return 0;

out:
	fw_read_errors(hdev, fw_loader->static_loader.boot_err0_reg,
					fw_loader->static_loader.boot_err1_reg,
					cpu_boot_dev_status0_reg,
					cpu_boot_dev_status1_reg);

	return rc;
}

/**
 * hl_fw_init_cpu - initialize the device CPU
 *
 * @hdev: pointer to the habanalabs device structure
 *
 * @return 0 on success, otherwise non-zero error code
 *
 * perform necessary initializations for device's CPU. takes into account if
 * init protocol is static or dynamic.
 */
int hl_fw_init_cpu(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct fw_load_mgr *fw_loader = &hdev->fw_loader;

	return  prop->dynamic_fw_load ?
			hl_fw_dynamic_init_cpu(hdev, fw_loader) :
			hl_fw_static_init_cpu(hdev, fw_loader);
}

void hl_fw_set_pll_profile(struct hl_device *hdev)
{
	hl_fw_set_frequency(hdev, hdev->asic_prop.clk_pll_index,
				hdev->asic_prop.max_freq_value);
}

int hl_fw_get_clk_rate(struct hl_device *hdev, u32 *cur_clk, u32 *max_clk)
{
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	if (!hdev->pdev) {
		*cur_clk = 0;
		*max_clk = 0;
		return 0;
	}

	value = hl_fw_get_frequency(hdev, hdev->asic_prop.clk_pll_index, false);

	if (value < 0) {
		dev_err(hdev->dev, "Failed to retrieve device max clock %ld\n", value);
		return value;
	}

	*max_clk = (value / 1000 / 1000);

	value = hl_fw_get_frequency(hdev, hdev->asic_prop.clk_pll_index, true);

	if (value < 0) {
		dev_err(hdev->dev, "Failed to retrieve device current clock %ld\n", value);
		return value;
	}

	*cur_clk = (value / 1000 / 1000);

	return 0;
}

long hl_fw_get_frequency(struct hl_device *hdev, u32 pll_index, bool curr)
{
	struct cpucp_packet pkt;
	u32 used_pll_idx;
	u64 result;
	int rc;

	rc = get_used_pll_index(hdev, pll_index, &used_pll_idx);
	if (rc)
		return rc;

	memset(&pkt, 0, sizeof(pkt));

	if (curr)
		pkt.ctl = cpu_to_le32(CPUCP_PACKET_FREQUENCY_CURR_GET <<
						CPUCP_PKT_CTL_OPCODE_SHIFT);
	else
		pkt.ctl = cpu_to_le32(CPUCP_PACKET_FREQUENCY_GET << CPUCP_PKT_CTL_OPCODE_SHIFT);

	pkt.pll_index = cpu_to_le32((u32)used_pll_idx);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, &result);

	if (rc) {
		dev_err(hdev->dev, "Failed to get frequency of PLL %d, error %d\n",
			used_pll_idx, rc);
		return rc;
	}

	return (long) result;
}

void hl_fw_set_frequency(struct hl_device *hdev, u32 pll_index, u64 freq)
{
	struct cpucp_packet pkt;
	u32 used_pll_idx;
	int rc;

	rc = get_used_pll_index(hdev, pll_index, &used_pll_idx);
	if (rc)
		return;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_FREQUENCY_SET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.pll_index = cpu_to_le32((u32)used_pll_idx);
	pkt.value = cpu_to_le64(freq);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);

	if (rc)
		dev_err(hdev->dev, "Failed to set frequency to PLL %d, error %d\n",
			used_pll_idx, rc);
}

long hl_fw_get_max_power(struct hl_device *hdev)
{
	struct cpucp_packet pkt;
	u64 result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_MAX_POWER_GET << CPUCP_PKT_CTL_OPCODE_SHIFT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, &result);

	if (rc) {
		dev_err(hdev->dev, "Failed to get max power, error %d\n", rc);
		return rc;
	}

	return result;
}

void hl_fw_set_max_power(struct hl_device *hdev)
{
	struct cpucp_packet pkt;
	int rc;

	/* TODO: remove this after simulator supports this packet */
	if (!hdev->pdev)
		return;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_MAX_POWER_SET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(hdev->max_power);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);

	if (rc)
		dev_err(hdev->dev, "Failed to set max power, error %d\n", rc);
}

static int hl_fw_get_sec_attest_data(struct hl_device *hdev, u32 packet_id, void *data, u32 size,
					u32 nonce, u32 timeout)
{
	struct cpucp_packet pkt = {};
	dma_addr_t req_dma_addr;
	void *req_cpu_addr;
	int rc;

	req_cpu_addr = hl_cpu_accessible_dma_pool_alloc(hdev, size, &req_dma_addr);
	if (!req_cpu_addr) {
		dev_err(hdev->dev,
			"Failed to allocate DMA memory for CPU-CP packet %u\n", packet_id);
		return -ENOMEM;
	}

	memset(data, 0, size);

	pkt.ctl = cpu_to_le32(packet_id << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.addr = cpu_to_le64(req_dma_addr);
	pkt.data_max_size = cpu_to_le32(size);
	pkt.nonce = cpu_to_le32(nonce);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					timeout, NULL);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to handle CPU-CP pkt %u, error %d\n", packet_id, rc);
		goto out;
	}

	memcpy(data, req_cpu_addr, size);

out:
	hl_cpu_accessible_dma_pool_free(hdev, size, req_cpu_addr);

	return rc;
}

int hl_fw_get_sec_attest_info(struct hl_device *hdev, struct cpucp_sec_attest_info *sec_attest_info,
				u32 nonce)
{
	return hl_fw_get_sec_attest_data(hdev, CPUCP_PACKET_SEC_ATTEST_GET, sec_attest_info,
					sizeof(struct cpucp_sec_attest_info), nonce,
					HL_CPUCP_SEC_ATTEST_INFO_TINEOUT_USEC);
}

int hl_fw_send_generic_request(struct hl_device *hdev, enum hl_passthrough_type sub_opcode,
						dma_addr_t buff, u32 *size)
{
	struct cpucp_packet pkt = {};
	u64 result;
	int rc = 0;

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_GENERIC_PASSTHROUGH << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.addr = cpu_to_le64(buff);
	pkt.data_max_size = cpu_to_le32(*size);
	pkt.pkt_subidx = cpu_to_le32(sub_opcode);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *)&pkt, sizeof(pkt),
						HL_CPUCP_INFO_TIMEOUT_USEC, &result);
	if (rc)
		dev_err(hdev->dev, "failed to send CPUCP data of generic fw pkt\n");
	else
		dev_dbg(hdev->dev, "generic pkt was successful, result: 0x%llx\n", result);

	*size = (u32)result;

	return rc;
}
