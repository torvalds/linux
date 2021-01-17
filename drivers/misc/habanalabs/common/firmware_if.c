// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"
#include "../include/common/hl_boot_if.h"

#include <linux/firmware.h>
#include <linux/genalloc.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/slab.h>

#define FW_FILE_MAX_SIZE	0x1400000 /* maximum size of 20MB */
/**
 * hl_fw_load_fw_to_device() - Load F/W code to device's memory.
 *
 * @hdev: pointer to hl_device structure.
 * @fw_name: the firmware image name
 * @dst: IO memory mapped address space to copy firmware to
 *
 * Copy fw code from firmware file to device memory.
 *
 * Return: 0 on success, non-zero for failure.
 */
int hl_fw_load_fw_to_device(struct hl_device *hdev, const char *fw_name,
				void __iomem *dst)
{
	const struct firmware *fw;
	const u64 *fw_data;
	size_t fw_size;
	int rc;

	rc = request_firmware(&fw, fw_name, hdev->dev);
	if (rc) {
		dev_err(hdev->dev, "Firmware file %s is not found!\n", fw_name);
		goto out;
	}

	fw_size = fw->size;
	if ((fw_size % 4) != 0) {
		dev_err(hdev->dev, "Illegal %s firmware size %zu\n",
			fw_name, fw_size);
		rc = -EINVAL;
		goto out;
	}

	dev_dbg(hdev->dev, "%s firmware size == %zu\n", fw_name, fw_size);

	if (fw_size > FW_FILE_MAX_SIZE) {
		dev_err(hdev->dev,
			"FW file size %zu exceeds maximum of %u bytes\n",
			fw_size, FW_FILE_MAX_SIZE);
		rc = -EINVAL;
		goto out;
	}

	fw_data = (const u64 *) fw->data;

	memcpy_toio(dst, fw_data, fw_size);

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
				u16 len, u32 timeout, long *result)
{
	struct cpucp_packet *pkt;
	dma_addr_t pkt_dma_addr;
	u32 tmp;
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

	rc = hl_hw_queue_send_cb_no_cmpl(hdev, hw_queue_id, len, pkt_dma_addr);
	if (rc) {
		dev_err(hdev->dev, "Failed to send CB on CPU PQ (%d)\n", rc);
		goto out;
	}

	rc = hl_poll_timeout_memory(hdev, &pkt->fence, tmp,
				(tmp == CPUCP_PACKET_FENCE_VAL), 1000,
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
		*result = (long) le64_to_cpu(pkt->result);
	}

out:
	mutex_unlock(&hdev->send_cpu_message_lock);

	hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev, len, pkt);

	return rc;
}

int hl_fw_unmask_irq(struct hl_device *hdev, u16 event_type)
{
	struct cpucp_packet pkt;
	long result;
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
	long result;
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
	long result;
	int rc;

	test_pkt.ctl = cpu_to_le32(CPUCP_PACKET_TEST <<
					CPUCP_PKT_CTL_OPCODE_SHIFT);
	test_pkt.value = cpu_to_le64(CPUCP_PACKET_FENCE_VAL);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &test_pkt,
						sizeof(test_pkt), 0, &result);

	if (!rc) {
		if (result != CPUCP_PACKET_FENCE_VAL)
			dev_err(hdev->dev,
				"CPU queue test failed (0x%08lX)\n", result);
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
	long result;
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

int hl_fw_cpucp_info_get(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct cpucp_packet pkt = {};
	void *cpucp_info_cpu_addr;
	dma_addr_t cpucp_info_dma_addr;
	long result;
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

	memcpy(&prop->cpucp_info, cpucp_info_cpu_addr,
			sizeof(prop->cpucp_info));

	rc = hl_build_hwmon_channel_info(hdev, prop->cpucp_info.sensors);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to build hwmon channel info, error %d\n", rc);
		rc = -EFAULT;
		goto out;
	}

out:
	hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev,
			sizeof(struct cpucp_info), cpucp_info_cpu_addr);

	return rc;
}

int hl_fw_get_eeprom_data(struct hl_device *hdev, void *data, size_t max_size)
{
	struct cpucp_packet pkt = {};
	void *eeprom_info_cpu_addr;
	dma_addr_t eeprom_info_dma_addr;
	long result;
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
	long result;
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
	long result;
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

static void fw_read_errors(struct hl_device *hdev, u32 boot_err0_reg)
{
	u32 err_val;

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
		return;

	if (err_val & CPU_BOOT_ERR0_DRAM_INIT_FAIL)
		dev_err(hdev->dev,
			"Device boot error - DRAM initialization failed\n");
	if (err_val & CPU_BOOT_ERR0_FIT_CORRUPTED)
		dev_err(hdev->dev, "Device boot error - FIT image corrupted\n");
	if (err_val & CPU_BOOT_ERR0_TS_INIT_FAIL)
		dev_err(hdev->dev,
			"Device boot error - Thermal Sensor initialization failed\n");
	if (err_val & CPU_BOOT_ERR0_DRAM_SKIPPED)
		dev_warn(hdev->dev,
			"Device boot warning - Skipped DRAM initialization\n");
	if (err_val & CPU_BOOT_ERR0_BMC_WAIT_SKIPPED)
		dev_warn(hdev->dev,
			"Device boot error - Skipped waiting for BMC\n");
	if (err_val & CPU_BOOT_ERR0_NIC_DATA_NOT_RDY)
		dev_err(hdev->dev,
			"Device boot error - Serdes data from BMC not available\n");
	if (err_val & CPU_BOOT_ERR0_NIC_FW_FAIL)
		dev_err(hdev->dev,
			"Device boot error - NIC F/W initialization failed\n");
}

static void detect_cpu_boot_status(struct hl_device *hdev, u32 status)
{
	/* Some of the status codes below are deprecated in newer f/w
	 * versions but we keep them here for backward compatibility
	 */
	switch (status) {
	case CPU_BOOT_STATUS_NA:
		dev_err(hdev->dev,
			"Device boot error - BTL did NOT run\n");
		break;
	case CPU_BOOT_STATUS_IN_WFE:
		dev_err(hdev->dev,
			"Device boot error - Stuck inside WFE loop\n");
		break;
	case CPU_BOOT_STATUS_IN_BTL:
		dev_err(hdev->dev,
			"Device boot error - Stuck in BTL\n");
		break;
	case CPU_BOOT_STATUS_IN_PREBOOT:
		dev_err(hdev->dev,
			"Device boot error - Stuck in Preboot\n");
		break;
	case CPU_BOOT_STATUS_IN_SPL:
		dev_err(hdev->dev,
			"Device boot error - Stuck in SPL\n");
		break;
	case CPU_BOOT_STATUS_IN_UBOOT:
		dev_err(hdev->dev,
			"Device boot error - Stuck in u-boot\n");
		break;
	case CPU_BOOT_STATUS_DRAM_INIT_FAIL:
		dev_err(hdev->dev,
			"Device boot error - DRAM initialization failed\n");
		break;
	case CPU_BOOT_STATUS_UBOOT_NOT_READY:
		dev_err(hdev->dev,
			"Device boot error - u-boot stopped by user\n");
		break;
	case CPU_BOOT_STATUS_TS_INIT_FAIL:
		dev_err(hdev->dev,
			"Device boot error - Thermal Sensor initialization failed\n");
		break;
	default:
		dev_err(hdev->dev,
			"Device boot error - Invalid status code %d\n",
			status);
		break;
	}
}

int hl_fw_read_preboot_ver(struct hl_device *hdev, u32 cpu_boot_status_reg,
				u32 boot_err0_reg, u32 timeout)
{
	u32 status;
	int rc;

	if (!hdev->cpu_enable)
		return 0;

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
		dev_err(hdev->dev, "Failed to read preboot version\n");
		detect_cpu_boot_status(hdev, status);
		fw_read_errors(hdev, boot_err0_reg);
		return -EIO;
	}

	hdev->asic_funcs->read_device_fw_version(hdev, FW_COMP_PREBOOT);

	return 0;
}

int hl_fw_init_cpu(struct hl_device *hdev, u32 cpu_boot_status_reg,
			u32 msg_to_cpu_reg, u32 cpu_msg_status_reg,
			u32 boot_err0_reg, bool skip_bmc,
			u32 cpu_timeout, u32 boot_fit_timeout)
{
	u32 status;
	int rc;

	dev_info(hdev->dev, "Going to wait for device boot (up to %lds)\n",
		cpu_timeout / USEC_PER_SEC);

	/* Wait for boot FIT request */
	rc = hl_poll_timeout(
		hdev,
		cpu_boot_status_reg,
		status,
		status == CPU_BOOT_STATUS_WAITING_FOR_BOOT_FIT,
		10000,
		boot_fit_timeout);

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
			boot_fit_timeout);

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

	/* Read U-Boot version now in case we will later fail */
	hdev->asic_funcs->read_device_fw_version(hdev, FW_COMP_UBOOT);

	if (rc) {
		detect_cpu_boot_status(hdev, status);
		rc = -EIO;
		goto out;
	}

	if (!hdev->fw_loading) {
		dev_info(hdev->dev, "Skip loading FW\n");
		goto out;
	}

	if (status == CPU_BOOT_STATUS_SRAM_AVAIL)
		goto out;

	dev_info(hdev->dev,
		"Loading firmware to device, may take some time...\n");

	rc = hdev->asic_funcs->load_firmware_to_device(hdev);
	if (rc)
		goto out;

	if (skip_bmc) {
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

	dev_info(hdev->dev, "Successfully loaded firmware to device\n");

out:
	fw_read_errors(hdev, boot_err0_reg);

	return rc;
}
