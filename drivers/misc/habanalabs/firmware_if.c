// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/firmware.h>
#include <linux/genalloc.h>
#include <linux/io-64-nonatomic-lo-hi.h>

/**
 * hl_fw_push_fw_to_device() - Push FW code to device.
 * @hdev: pointer to hl_device structure.
 *
 * Copy fw code from firmware file to device memory.
 *
 * Return: 0 on success, non-zero for failure.
 */
int hl_fw_push_fw_to_device(struct hl_device *hdev, const char *fw_name,
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

	fw_data = (const u64 *) fw->data;

	memcpy_toio(dst, fw_data, fw_size);

out:
	release_firmware(fw);
	return rc;
}

int hl_fw_send_pci_access_msg(struct hl_device *hdev, u32 opcode)
{
	struct armcp_packet pkt = {};

	pkt.ctl = cpu_to_le32(opcode << ARMCP_PKT_CTL_OPCODE_SHIFT);

	return hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt,
				sizeof(pkt), HL_DEVICE_TIMEOUT_USEC, NULL);
}

int hl_fw_send_cpu_message(struct hl_device *hdev, u32 hw_queue_id, u32 *msg,
				u16 len, u32 timeout, long *result)
{
	struct armcp_packet *pkt;
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
				(tmp == ARMCP_PACKET_FENCE_VAL), 1000,
				timeout, true);

	hl_hw_queue_inc_ci_kernel(hdev, hw_queue_id);

	if (rc == -ETIMEDOUT) {
		dev_err(hdev->dev, "Device CPU packet timeout (0x%x)\n", tmp);
		hdev->device_cpu_disabled = true;
		goto out;
	}

	tmp = le32_to_cpu(pkt->ctl);

	rc = (tmp & ARMCP_PKT_CTL_RC_MASK) >> ARMCP_PKT_CTL_RC_SHIFT;
	if (rc) {
		dev_err(hdev->dev, "F/W ERROR %d for CPU packet %d\n",
			rc,
			(tmp & ARMCP_PKT_CTL_OPCODE_MASK)
						>> ARMCP_PKT_CTL_OPCODE_SHIFT);
		rc = -EIO;
	} else if (result) {
		*result = (long) le64_to_cpu(pkt->result);
	}

out:
	mutex_unlock(&hdev->send_cpu_message_lock);

	hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev, len, pkt);

	return rc;
}

int hl_fw_test_cpu_queue(struct hl_device *hdev)
{
	struct armcp_packet test_pkt = {};
	long result;
	int rc;

	test_pkt.ctl = cpu_to_le32(ARMCP_PACKET_TEST <<
					ARMCP_PKT_CTL_OPCODE_SHIFT);
	test_pkt.value = cpu_to_le64(ARMCP_PACKET_FENCE_VAL);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &test_pkt,
			sizeof(test_pkt), HL_DEVICE_TIMEOUT_USEC, &result);

	if (!rc) {
		if (result == ARMCP_PACKET_FENCE_VAL)
			dev_info(hdev->dev,
				"queue test on CPU queue succeeded\n");
		else
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
	struct armcp_packet hb_pkt = {};
	long result;
	int rc;

	hb_pkt.ctl = cpu_to_le32(ARMCP_PACKET_TEST <<
					ARMCP_PKT_CTL_OPCODE_SHIFT);
	hb_pkt.value = cpu_to_le64(ARMCP_PACKET_FENCE_VAL);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &hb_pkt,
			sizeof(hb_pkt), HL_DEVICE_TIMEOUT_USEC, &result);

	if ((rc) || (result != ARMCP_PACKET_FENCE_VAL))
		rc = -EIO;

	return rc;
}

int hl_fw_armcp_info_get(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct armcp_packet pkt = {};
	void *armcp_info_cpu_addr;
	dma_addr_t armcp_info_dma_addr;
	long result;
	int rc;

	armcp_info_cpu_addr =
			hdev->asic_funcs->cpu_accessible_dma_pool_alloc(hdev,
					sizeof(struct armcp_info),
					&armcp_info_dma_addr);
	if (!armcp_info_cpu_addr) {
		dev_err(hdev->dev,
			"Failed to allocate DMA memory for ArmCP info packet\n");
		return -ENOMEM;
	}

	memset(armcp_info_cpu_addr, 0, sizeof(struct armcp_info));

	pkt.ctl = cpu_to_le32(ARMCP_PACKET_INFO_GET <<
				ARMCP_PKT_CTL_OPCODE_SHIFT);
	pkt.addr = cpu_to_le64(armcp_info_dma_addr);
	pkt.data_max_size = cpu_to_le32(sizeof(struct armcp_info));

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					HL_ARMCP_INFO_TIMEOUT_USEC, &result);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to send ArmCP info pkt, error %d\n", rc);
		goto out;
	}

	memcpy(&prop->armcp_info, armcp_info_cpu_addr,
			sizeof(prop->armcp_info));

	rc = hl_build_hwmon_channel_info(hdev, prop->armcp_info.sensors);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to build hwmon channel info, error %d\n", rc);
		rc = -EFAULT;
		goto out;
	}

out:
	hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev,
			sizeof(struct armcp_info), armcp_info_cpu_addr);

	return rc;
}

int hl_fw_get_eeprom_data(struct hl_device *hdev, void *data, size_t max_size)
{
	struct armcp_packet pkt = {};
	void *eeprom_info_cpu_addr;
	dma_addr_t eeprom_info_dma_addr;
	long result;
	int rc;

	eeprom_info_cpu_addr =
			hdev->asic_funcs->cpu_accessible_dma_pool_alloc(hdev,
					max_size, &eeprom_info_dma_addr);
	if (!eeprom_info_cpu_addr) {
		dev_err(hdev->dev,
			"Failed to allocate DMA memory for ArmCP EEPROM packet\n");
		return -ENOMEM;
	}

	memset(eeprom_info_cpu_addr, 0, max_size);

	pkt.ctl = cpu_to_le32(ARMCP_PACKET_EEPROM_DATA_GET <<
				ARMCP_PKT_CTL_OPCODE_SHIFT);
	pkt.addr = cpu_to_le64(eeprom_info_dma_addr);
	pkt.data_max_size = cpu_to_le32(max_size);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
			HL_ARMCP_EEPROM_TIMEOUT_USEC, &result);

	if (rc) {
		dev_err(hdev->dev,
			"Failed to send ArmCP EEPROM packet, error %d\n", rc);
		goto out;
	}

	/* result contains the actual size */
	memcpy(data, eeprom_info_cpu_addr, min((size_t)result, max_size));

out:
	hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev, max_size,
			eeprom_info_cpu_addr);

	return rc;
}
