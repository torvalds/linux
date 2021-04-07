// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"
#include "../include/common/hl_boot_if.h"

#include <linux/firmware.h>
#include <linux/slab.h>

#define FW_FILE_MAX_SIZE	0x1400000 /* maximum size of 20MB */

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
	const void *fw_data;
	size_t fw_size;
	int rc;

	rc = hl_request_fw(hdev, &fw, fw_name);
	if (rc)
		return rc;

	fw_size = fw->size;

	if (size - src_offset > fw_size) {
		dev_err(hdev->dev,
			"size to copy(%u) and offset(%u) are invalid\n",
			size, src_offset);
		rc = -EINVAL;
		goto out;
	}

	if (size)
		fw_size = size;

	fw_data = (const void *) fw->data;

	memcpy_toio(dst, fw_data + src_offset, fw_size);

out:
	release_firmware(fw);
	return rc;
}

int hl_fw_send_pci_access_msg(struct hl_device *hdev, u32 opcode)
{
	struct cpucp_packet pkt = {};

	pkt.ctl = cpu_to_le32(opcode << CPUCP_PKT_CTL_OPCODE_SHIFT);

	return hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt,
						sizeof(pkt), 0, NULL);
}

int hl_fw_send_cpu_message(struct hl_device *hdev, u32 hw_queue_id, u32 *msg,
				u16 len, u32 timeout, u64 *result)
{
	struct hl_hw_queue *queue = &hdev->kernel_queues[hw_queue_id];
	struct cpucp_packet *pkt;
	dma_addr_t pkt_dma_addr;
	u32 tmp, expected_ack_val;
	int rc = 0;

	pkt = hdev->asic_funcs->cpu_accessible_dma_pool_alloc(hdev, len,
								&pkt_dma_addr);
	if (!pkt) {
		dev_err(hdev->dev,
			"Failed to allocate DMA memory for packet to CPU\n");
		return -ENOMEM;
	}

	memcpy(pkt, msg, len);

	mutex_lock(&hdev->send_cpu_message_lock);

	if (hdev->disabled)
		goto out;

	if (hdev->device_cpu_disabled) {
		rc = -EIO;
		goto out;
	}

	/* set fence to a non valid value */
	pkt->fence = cpu_to_le32(UINT_MAX);

	rc = hl_hw_queue_send_cb_no_cmpl(hdev, hw_queue_id, len, pkt_dma_addr);
	if (rc) {
		dev_err(hdev->dev, "Failed to send CB on CPU PQ (%d)\n", rc);
		goto out;
	}

	if (hdev->asic_prop.fw_app_security_map &
			CPU_BOOT_DEV_STS0_PKT_PI_ACK_EN)
		expected_ack_val = queue->pi;
	else
		expected_ack_val = CPUCP_PACKET_FENCE_VAL;

	rc = hl_poll_timeout_memory(hdev, &pkt->fence, tmp,
				(tmp == expected_ack_val), 1000,
				timeout, true);

	hl_hw_queue_inc_ci_kernel(hdev, hw_queue_id);

	if (rc == -ETIMEDOUT) {
		dev_err(hdev->dev, "Device CPU packet timeout (0x%x)\n", tmp);
		hdev->device_cpu_disabled = true;
		goto out;
	}

	tmp = le32_to_cpu(pkt->ctl);

	rc = (tmp & CPUCP_PKT_CTL_RC_MASK) >> CPUCP_PKT_CTL_RC_SHIFT;
	if (rc) {
		dev_err(hdev->dev, "F/W ERROR %d for CPU packet %d\n",
			rc,
			(tmp & CPUCP_PKT_CTL_OPCODE_MASK)
						>> CPUCP_PKT_CTL_OPCODE_SHIFT);
		rc = -EIO;
	} else if (result) {
		*result = le64_to_cpu(pkt->result);
	}

out:
	mutex_unlock(&hdev->send_cpu_message_lock);

	hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev, len, pkt);

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

int hl_fw_send_heartbeat(struct hl_device *hdev)
{
	struct cpucp_packet hb_pkt = {};
	u64 result;
	int rc;

	hb_pkt.ctl = cpu_to_le32(CPUCP_PACKET_TEST <<
					CPUCP_PKT_CTL_OPCODE_SHIFT);
	hb_pkt.value = cpu_to_le64(CPUCP_PACKET_FENCE_VAL);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &hb_pkt,
						sizeof(hb_pkt), 0, &result);

	if ((rc) || (result != CPUCP_PACKET_FENCE_VAL))
		rc = -EIO;

	return rc;
}

static int fw_read_errors(struct hl_device *hdev, u32 boot_err0_reg,
		u32 cpu_security_boot_status_reg)
{
	u32 err_val, security_val;
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
	if (!(err_val & CPU_BOOT_ERR0_ENABLED))
		return 0;

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

	if (err_val & CPU_BOOT_ERR0_DRAM_SKIPPED) {
		dev_warn(hdev->dev,
			"Device boot warning - Skipped DRAM initialization\n");
		/* This is a warning so we don't want it to disable the
		 * device
		 */
		err_val &= ~CPU_BOOT_ERR0_DRAM_SKIPPED;
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

	if (err_val & CPU_BOOT_ERR0_PLL_FAIL) {
		dev_err(hdev->dev, "Device boot error - PLL failure\n");
		err_exists = true;
	}

	if (err_val & CPU_BOOT_ERR0_DEVICE_UNUSABLE_FAIL) {
		dev_err(hdev->dev,
			"Device boot error - device unusable\n");
		err_exists = true;
	}

	security_val = RREG32(cpu_security_boot_status_reg);
	if (security_val & CPU_BOOT_DEV_STS0_ENABLED)
		dev_dbg(hdev->dev, "Device security status %#x\n",
				security_val);

	if (!err_exists && (err_val & ~CPU_BOOT_ERR0_ENABLED)) {
		dev_err(hdev->dev,
			"Device boot error - unknown error 0x%08x\n",
			err_val);
		err_exists = true;
	}

	if (err_exists && ((err_val & ~CPU_BOOT_ERR0_ENABLED) &
				lower_32_bits(hdev->boot_error_status_mask)))
		return -EIO;

	return 0;
}

int hl_fw_cpucp_info_get(struct hl_device *hdev,
			u32 cpu_security_boot_status_reg,
			u32 boot_err0_reg)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct cpucp_packet pkt = {};
	void *cpucp_info_cpu_addr;
	dma_addr_t cpucp_info_dma_addr;
	u64 result;
	int rc;

	cpucp_info_cpu_addr =
			hdev->asic_funcs->cpu_accessible_dma_pool_alloc(hdev,
					sizeof(struct cpucp_info),
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

	rc = fw_read_errors(hdev, boot_err0_reg, cpu_security_boot_status_reg);
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

	/* Read FW application security bits again */
	if (hdev->asic_prop.fw_security_status_valid)
		hdev->asic_prop.fw_app_security_map =
				RREG32(cpu_security_boot_status_reg);

out:
	hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev,
			sizeof(struct cpucp_info), cpucp_info_cpu_addr);

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
			u32 cpu_security_boot_status_reg,
			u32 boot_err0_reg)
{
	int rc;

	rc = hl_fw_cpucp_info_get(hdev, cpu_security_boot_status_reg,
					boot_err0_reg);
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

	eeprom_info_cpu_addr =
			hdev->asic_funcs->cpu_accessible_dma_pool_alloc(hdev,
					max_size, &eeprom_info_dma_addr);
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
	hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev, max_size,
			eeprom_info_cpu_addr);

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

	dynamic_pll = prop->fw_security_status_valid &&
		(prop->fw_app_security_map & CPU_BOOT_DEV_STS0_DYN_PLL_EN);

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
	if (rc)
		dev_err(hdev->dev, "Failed to read PLL info, error %d\n", rc);

	pll_freq_arr[0] = FIELD_GET(CPUCP_PKT_RES_PLL_OUT0_MASK, result);
	pll_freq_arr[1] = FIELD_GET(CPUCP_PKT_RES_PLL_OUT1_MASK, result);
	pll_freq_arr[2] = FIELD_GET(CPUCP_PKT_RES_PLL_OUT2_MASK, result);
	pll_freq_arr[3] = FIELD_GET(CPUCP_PKT_RES_PLL_OUT3_MASK, result);

	return rc;
}

int hl_fw_cpucp_power_get(struct hl_device *hdev, u64 *power)
{
	struct cpucp_packet pkt;
	u64 result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_POWER_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
			HL_CPUCP_INFO_TIMEOUT_USEC, &result);
	if (rc) {
		dev_err(hdev->dev, "Failed to read power, error %d\n", rc);
		return rc;
	}

	*power = result;

	return rc;
}

static void detect_cpu_boot_status(struct hl_device *hdev, u32 status)
{
	/* Some of the status codes below are deprecated in newer f/w
	 * versions but we keep them here for backward compatibility
	 */
	switch (status) {
	case CPU_BOOT_STATUS_NA:
		dev_err(hdev->dev,
			"Device boot progress - BTL did NOT run\n");
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
	default:
		dev_err(hdev->dev,
			"Device boot progress - Invalid status code %d\n",
			status);
		break;
	}
}

static int hl_fw_read_preboot_caps(struct hl_device *hdev,
					u32 cpu_boot_status_reg,
					u32 cpu_boot_caps_reg,
					u32 boot_err0_reg, u32 timeout)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
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
		cpu_boot_status_reg,
		status,
		(status == CPU_BOOT_STATUS_IN_UBOOT) ||
		(status == CPU_BOOT_STATUS_DRAM_RDY) ||
		(status == CPU_BOOT_STATUS_NIC_FW_RDY) ||
		(status == CPU_BOOT_STATUS_READY_TO_BOOT) ||
		(status == CPU_BOOT_STATUS_SRAM_AVAIL) ||
		(status == CPU_BOOT_STATUS_WAITING_FOR_BOOT_FIT),
		10000,
		timeout);

	if (rc) {
		dev_err(hdev->dev, "CPU boot ready status timeout\n");
		detect_cpu_boot_status(hdev, status);

		/* If we read all FF, then something is totally wrong, no point
		 * of reading specific errors
		 */
		if (status != -1)
			fw_read_errors(hdev, boot_err0_reg,
					cpu_boot_status_reg);
		return -EIO;
	}

	prop->fw_preboot_caps_map = RREG32(cpu_boot_caps_reg);

	/*
	 * For now- force dynamic_fw_load to false as LKD does not yet
	 * implements all necessary parts of it.
	 * TODO: once dynamic load is ready set to:
	 * prop->dynamic_fw_load = !!(prop->fw_preboot_caps_map &
	 *                                CPU_BOOT_DEV_STS0_FW_LD_COM_EN)
	 */
	prop->dynamic_fw_load = 0;

	dev_dbg(hdev->dev, "Attempting %s FW load\n",
			prop->dynamic_fw_load ? "dynamic" : "legacy");
	return 0;
}

static int hl_read_device_fw_version(struct hl_device *hdev,
					enum hl_fw_component fwc)
{
	struct fw_load_mgr *fw_loader = &hdev->fw_loader;
	struct static_fw_load_mgr *static_loader;
	const char *name;
	u32 ver_off, limit;
	char *dest;

	static_loader = &hdev->fw_loader.static_loader;

	switch (fwc) {
	case FW_COMP_BOOT_FIT:
		ver_off = RREG32(static_loader->boot_fit_version_offset_reg);
		dest = hdev->asic_prop.uboot_ver;
		name = "Boot-fit";
		limit = static_loader->boot_fit_version_max_off;
		break;
	case FW_COMP_PREBOOT:
		ver_off = RREG32(static_loader->preboot_version_offset_reg);
		dest = hdev->asic_prop.preboot_ver;
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

	return 0;
}

static int hl_fw_read_preboot_status_legacy(struct hl_device *hdev,
		u32 cpu_boot_status_reg, u32 cpu_security_boot_status_reg,
		u32 boot_err0_reg, u32 timeout)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 security_status;
	int rc;

	rc = hl_read_device_fw_version(hdev, FW_COMP_PREBOOT);
	if (rc)
		return rc;

	security_status = prop->fw_preboot_caps_map;

	/* We read security status multiple times during boot:
	 * 1. preboot - a. Check whether the security status bits are valid
	 *              b. Check whether fw security is enabled
	 *              c. Check whether hard reset is done by preboot
	 * 2. boot cpu - a. Fetch boot cpu security status
	 *               b. Check whether hard reset is done by boot cpu
	 * 3. FW application - a. Fetch fw application security status
	 *                     b. Check whether hard reset is done by fw app
	 *
	 * Preboot:
	 * Check security status bit (CPU_BOOT_DEV_STS0_ENABLED), if it is set
	 * check security enabled bit (CPU_BOOT_DEV_STS0_SECURITY_EN)
	 */
	if (security_status & CPU_BOOT_DEV_STS0_ENABLED) {
		prop->fw_security_status_valid = 1;

		/* FW security should be derived from PCI ID, we keep this
		 * check for backward compatibility
		 */
		if (security_status & CPU_BOOT_DEV_STS0_SECURITY_EN)
			prop->fw_security_disabled = false;

		if (security_status & CPU_BOOT_DEV_STS0_FW_HARD_RST_EN)
			prop->hard_reset_done_by_fw = true;
	} else {
		prop->fw_security_status_valid = 0;
	}

	dev_dbg(hdev->dev, "Firmware preboot security status %#x\n",
			security_status);

	dev_dbg(hdev->dev, "Firmware preboot hard-reset is %s\n",
			prop->hard_reset_done_by_fw ? "enabled" : "disabled");

	dev_info(hdev->dev, "firmware-level security is %s\n",
			prop->fw_security_disabled ? "disabled" : "enabled");

	return 0;
}

int hl_fw_read_preboot_status(struct hl_device *hdev, u32 cpu_boot_status_reg,
		u32 cpu_boot_caps_reg, u32 boot_err0_reg,
		u32 timeout)
{
	int rc;

	hdev->asic_funcs->init_firmware_loader(hdev);

	/* pldm was added for cases in which we use preboot on pldm and want
	 * to load boot fit, but we can't wait for preboot because it runs
	 * very slowly
	 */
	if (!(hdev->fw_components & FW_TYPE_PREBOOT_CPU) || hdev->pldm)
		return 0;

	/*
	 * In order to determine boot method (static VS dymanic) we need to
	 * read the boot caps register
	 */
	rc = hl_fw_read_preboot_caps(hdev, cpu_boot_status_reg,
				cpu_boot_caps_reg, boot_err0_reg,
				timeout);
	if (rc)
		return rc;

	if (!hdev->asic_prop.dynamic_fw_load)
		return hl_fw_read_preboot_status_legacy(hdev, cpu_boot_status_reg,
				cpu_boot_caps_reg, boot_err0_reg,
				timeout);

	dev_err(hdev->dev, "Dynamic FW load is not supported\n");
	return -EINVAL;
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
 * @lkd_cmd: LKD to FW cmd code
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
	struct comms_command lkd_cmd;

	lkd_cmd.val = FIELD_PREP(COMMS_COMMAND_CMD_MASK, cmd);
	lkd_cmd.val |= FIELD_PREP(COMMS_COMMAND_SIZE_MASK, size);

	WREG32(fw_loader->kmd_msg_to_cpu_reg, lkd_cmd.val);
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
	u32 status;
	int rc;

	/* Wait for expected status */
	rc = hl_poll_timeout(
		hdev,
		le32_to_cpu(fw_loader->cpu_cmd_status_to_host_reg),
		status,
		FIELD_GET(COMMS_STATUS_STATUS_MASK, status) == expected_status,
		10000,
		timeout);

	if (rc) {
		hl_fw_dynamic_report_error_status(hdev, status,
							expected_status);
		return -EIO;
	}

	return 0;
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
 * @lkd_cmd: LKD to FW cmd code
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
static int hl_fw_dynamic_send_protocol_cmd(struct hl_device *hdev,
				struct fw_load_mgr *fw_loader,
				enum comms_cmd cmd, unsigned int size,
				bool wait_ok, u32 timeout)
{
	int rc;

	/* first send clear command to clean former commands */
	rc = hl_fw_dynamic_send_clear_cmd(hdev, fw_loader);

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
	int rc;

	rc = hl_fw_dynamic_send_protocol_cmd(hdev, fw_loader, COMMS_RST_STATE,
						0, true,
						fw_loader->cpu_timeout);
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
	u32 cpu_boot_status_reg, cpu_security_boot_status_reg;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct static_fw_load_mgr *static_loader;
	int rc;

	if (!(hdev->fw_components & FW_TYPE_BOOT_CPU))
		return 0;

	/* init common loader parameters */
	static_loader = &fw_loader->static_loader;
	cpu_msg_status_reg = fw_loader->cpu_cmd_status_to_host_reg;
	msg_to_cpu_reg = fw_loader->kmd_msg_to_cpu_reg;
	cpu_timeout = fw_loader->cpu_timeout;

	/* init static loader parameters */
	cpu_security_boot_status_reg = static_loader->cpu_boot_status_reg;
	cpu_boot_status_reg = static_loader->cpu_boot_status_reg;

	dev_info(hdev->dev, "Going to wait for device boot (up to %lds)\n",
		cpu_timeout / USEC_PER_SEC);

	/* Wait for boot FIT request */
	rc = hl_poll_timeout(
		hdev,
		cpu_boot_status_reg,
		status,
		status == CPU_BOOT_STATUS_WAITING_FOR_BOOT_FIT,
		10000,
		fw_loader->boot_fit_timeout);

	if (rc) {
		dev_dbg(hdev->dev,
			"No boot fit request received, resuming boot\n");
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
			10000,
			fw_loader->boot_fit_timeout);

		if (rc) {
			dev_err(hdev->dev,
				"Timeout waiting for boot fit load ack\n");
			goto out;
		}

		/* Clear message */
		WREG32(msg_to_cpu_reg, KMD_MSG_NA);
	}

	/* Make sure CPU boot-loader is running */
	rc = hl_poll_timeout(
		hdev,
		cpu_boot_status_reg,
		status,
		(status == CPU_BOOT_STATUS_DRAM_RDY) ||
		(status == CPU_BOOT_STATUS_NIC_FW_RDY) ||
		(status == CPU_BOOT_STATUS_READY_TO_BOOT) ||
		(status == CPU_BOOT_STATUS_SRAM_AVAIL),
		10000,
		cpu_timeout);

	dev_dbg(hdev->dev, "uboot status = %d\n", status);

	/* Read U-Boot version now in case we will later fail */
	hl_read_device_fw_version(hdev, FW_COMP_BOOT_FIT);

	/* Clear reset status since we need to read it again from boot CPU */
	prop->hard_reset_done_by_fw = false;

	/* Read boot_cpu security bits */
	if (prop->fw_security_status_valid) {
		prop->fw_boot_cpu_security_map =
				RREG32(cpu_security_boot_status_reg);

		if (prop->fw_boot_cpu_security_map &
				CPU_BOOT_DEV_STS0_FW_HARD_RST_EN)
			prop->hard_reset_done_by_fw = true;

		dev_dbg(hdev->dev,
			"Firmware boot CPU security status %#x\n",
			prop->fw_boot_cpu_security_map);
	}

	dev_dbg(hdev->dev, "Firmware boot CPU hard-reset is %s\n",
			prop->hard_reset_done_by_fw ? "enabled" : "disabled");

	if (rc) {
		detect_cpu_boot_status(hdev, status);
		rc = -EIO;
		goto out;
	}

	if (!(hdev->fw_components & FW_TYPE_LINUX)) {
		dev_info(hdev->dev, "Skip loading Linux F/W\n");
		goto out;
	}

	if (status == CPU_BOOT_STATUS_SRAM_AVAIL)
		goto out;

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
			10000,
			cpu_timeout);

		if (rc) {
			dev_err(hdev->dev,
				"Failed to get ACK on skipping BMC, %d\n",
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
		10000,
		cpu_timeout);

	/* Clear message */
	WREG32(msg_to_cpu_reg, KMD_MSG_NA);

	if (rc) {
		if (status == CPU_BOOT_STATUS_FIT_CORRUPTED)
			dev_err(hdev->dev,
				"Device reports FIT image is corrupted\n");
		else
			dev_err(hdev->dev,
				"Failed to load firmware to device, %d\n",
				status);

		rc = -EIO;
		goto out;
	}

	rc = fw_read_errors(hdev, fw_loader->static_loader.boot_err0_reg,
					cpu_security_boot_status_reg);
	if (rc)
		return rc;

	/* Clear reset status since we need to read again from app */
	prop->hard_reset_done_by_fw = false;

	/* Read FW application security bits */
	if (prop->fw_security_status_valid) {
		prop->fw_app_security_map =
				RREG32(cpu_security_boot_status_reg);

		if (prop->fw_app_security_map &
				CPU_BOOT_DEV_STS0_FW_HARD_RST_EN)
			prop->hard_reset_done_by_fw = true;

		dev_dbg(hdev->dev,
			"Firmware application CPU security status %#x\n",
			prop->fw_app_security_map);
	}

	dev_dbg(hdev->dev, "Firmware application CPU hard-reset is %s\n",
			prop->hard_reset_done_by_fw ? "enabled" : "disabled");

	dev_info(hdev->dev, "Successfully loaded firmware to device\n");

	return 0;

out:
	fw_read_errors(hdev, fw_loader->static_loader.boot_err0_reg,
					cpu_security_boot_status_reg);

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
