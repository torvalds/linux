// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2020 Linaro Limited */

#include <linux/acpi.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "xhci.h"
#include "xhci-trace.h"
#include "xhci-pci.h"

#define RENESAS_FW_VERSION				0x6C
#define RENESAS_ROM_CONFIG				0xF0
#define RENESAS_FW_STATUS				0xF4
#define RENESAS_FW_STATUS_MSB				0xF5
#define RENESAS_ROM_STATUS				0xF6
#define RENESAS_ROM_STATUS_MSB				0xF7
#define RENESAS_DATA0					0xF8
#define RENESAS_DATA1					0xFC

#define RENESAS_FW_VERSION_FIELD			GENMASK(23, 7)
#define RENESAS_FW_VERSION_OFFSET			8

#define RENESAS_FW_STATUS_DOWNLOAD_ENABLE		BIT(0)
#define RENESAS_FW_STATUS_LOCK				BIT(1)
#define RENESAS_FW_STATUS_RESULT			GENMASK(6, 4)
  #define RENESAS_FW_STATUS_INVALID			0
  #define RENESAS_FW_STATUS_SUCCESS			BIT(4)
  #define RENESAS_FW_STATUS_ERROR			BIT(5)
#define RENESAS_FW_STATUS_SET_DATA0			BIT(8)
#define RENESAS_FW_STATUS_SET_DATA1			BIT(9)

#define RENESAS_ROM_STATUS_ACCESS			BIT(0)
#define RENESAS_ROM_STATUS_ERASE			BIT(1)
#define RENESAS_ROM_STATUS_RELOAD			BIT(2)
#define RENESAS_ROM_STATUS_RESULT			GENMASK(6, 4)
  #define RENESAS_ROM_STATUS_NO_RESULT			0
  #define RENESAS_ROM_STATUS_SUCCESS			BIT(4)
  #define RENESAS_ROM_STATUS_ERROR			BIT(5)
#define RENESAS_ROM_STATUS_SET_DATA0			BIT(8)
#define RENESAS_ROM_STATUS_SET_DATA1			BIT(9)
#define RENESAS_ROM_STATUS_ROM_EXISTS			BIT(15)

#define RENESAS_ROM_ERASE_MAGIC				0x5A65726F
#define RENESAS_ROM_WRITE_MAGIC				0x53524F4D

#define RENESAS_RETRY	10000
#define RENESAS_DELAY	10

#define ROM_VALID_01 0x2013
#define ROM_VALID_02 0x2026

static int renesas_verify_fw_version(struct pci_dev *pdev, u32 version)
{
	switch (version) {
	case ROM_VALID_01:
	case ROM_VALID_02:
		return 0;
	}
	dev_err(&pdev->dev, "FW has invalid version :%d\n", version);
	return -EINVAL;
}

static int renesas_fw_download_image(struct pci_dev *dev,
				     const u32 *fw, size_t step, bool rom)
{
	size_t i;
	int err;
	u8 fw_status;
	bool data0_or_data1;
	u32 status_reg;

	if (rom)
		status_reg = RENESAS_ROM_STATUS_MSB;
	else
		status_reg = RENESAS_FW_STATUS_MSB;

	/*
	 * The hardware does alternate between two 32-bit pages.
	 * (This is because each row of the firmware is 8 bytes).
	 *
	 * for even steps we use DATA0, for odd steps DATA1.
	 */
	data0_or_data1 = (step & 1) == 1;

	/* step+1. Read "Set DATAX" and confirm it is cleared. */
	for (i = 0; i < RENESAS_RETRY; i++) {
		err = pci_read_config_byte(dev, status_reg, &fw_status);
		if (err) {
			dev_err(&dev->dev, "Read Status failed: %d\n",
				pcibios_err_to_errno(err));
			return pcibios_err_to_errno(err);
		}
		if (!(fw_status & BIT(data0_or_data1)))
			break;

		udelay(RENESAS_DELAY);
	}
	if (i == RENESAS_RETRY) {
		dev_err(&dev->dev, "Timeout for Set DATAX step: %zd\n", step);
		return -ETIMEDOUT;
	}

	/*
	 * step+2. Write FW data to "DATAX".
	 * "LSB is left" => force little endian
	 */
	err = pci_write_config_dword(dev, data0_or_data1 ?
				     RENESAS_DATA1 : RENESAS_DATA0,
				     (__force u32)cpu_to_le32(fw[step]));
	if (err) {
		dev_err(&dev->dev, "Write to DATAX failed: %d\n",
			pcibios_err_to_errno(err));
		return pcibios_err_to_errno(err);
	}

	udelay(100);

	/* step+3. Set "Set DATAX". */
	err = pci_write_config_byte(dev, status_reg, BIT(data0_or_data1));
	if (err) {
		dev_err(&dev->dev, "Write config for DATAX failed: %d\n",
			pcibios_err_to_errno(err));
		return pcibios_err_to_errno(err);
	}

	return 0;
}

static int renesas_fw_verify(const void *fw_data,
			     size_t length)
{
	u16 fw_version_pointer;
	u16 fw_version;

	/*
	 * The Firmware's Data Format is describe in
	 * "6.3 Data Format" R19UH0078EJ0500 Rev.5.00 page 124
	 */

	/*
	 * The bootrom chips of the big brother have sizes up to 64k, let's
	 * assume that's the biggest the firmware can get.
	 */
	if (length < 0x1000 || length >= 0x10000) {
		pr_err("firmware is size %zd is not (4k - 64k).",
			length);
		return -EINVAL;
	}

	/* The First 2 bytes are fixed value (55aa). "LSB on Left" */
	if (get_unaligned_le16(fw_data) != 0x55aa) {
		pr_err("no valid firmware header found.");
		return -EINVAL;
	}

	/* verify the firmware version position and print it. */
	fw_version_pointer = get_unaligned_le16(fw_data + 4);
	if (fw_version_pointer + 2 >= length) {
		pr_err("fw ver pointer is outside of the firmware image");
		return -EINVAL;
	}

	fw_version = get_unaligned_le16(fw_data + fw_version_pointer);
	pr_err("got firmware version: %02x.", fw_version);

	return 0;
}

static bool renesas_check_rom(struct pci_dev *pdev)
{
	u16 rom_status;
	int retval;

	/* Check if external ROM exists */
	retval = pci_read_config_word(pdev, RENESAS_ROM_STATUS, &rom_status);
	if (retval)
		return false;

	rom_status &= RENESAS_ROM_STATUS_ROM_EXISTS;
	if (rom_status) {
		dev_dbg(&pdev->dev, "External ROM exists\n");
		return true; /* External ROM exists */
	}

	return false;
}

static int renesas_check_rom_state(struct pci_dev *pdev)
{
	u16 rom_state;
	u32 version;
	int err;

	/* check FW version */
	err = pci_read_config_dword(pdev, RENESAS_FW_VERSION, &version);
	if (err)
		return pcibios_err_to_errno(err);

	version &= RENESAS_FW_VERSION_FIELD;
	version = version >> RENESAS_FW_VERSION_OFFSET;

	err = renesas_verify_fw_version(pdev, version);
	if (err)
		return err;

	/*
	 * Test if ROM is present and loaded, if so we can skip everything
	 */
	err = pci_read_config_word(pdev, RENESAS_ROM_STATUS, &rom_state);
	if (err)
		return pcibios_err_to_errno(err);

	if (rom_state & BIT(15)) {
		/* ROM exists */
		dev_dbg(&pdev->dev, "ROM exists\n");

		/* Check the "Result Code" Bits (6:4) and act accordingly */
		switch (rom_state & RENESAS_ROM_STATUS_RESULT) {
		case RENESAS_ROM_STATUS_SUCCESS:
			return 0;

		case RENESAS_ROM_STATUS_NO_RESULT: /* No result yet */
			return 0;

		case RENESAS_ROM_STATUS_ERROR: /* Error State */
		default: /* All other states are marked as "Reserved states" */
			dev_err(&pdev->dev, "Invalid ROM..");
			break;
		}
	}

	return -EIO;
}

static int renesas_fw_check_running(struct pci_dev *pdev)
{
	u8 fw_state;
	int err;

	/* Check if device has ROM and loaded, if so skip everything */
	err = renesas_check_rom(pdev);
	if (err) { /* we have rom */
		err = renesas_check_rom_state(pdev);
		if (!err)
			return err;
	}

	/*
	 * Test if the device is actually needing the firmware. As most
	 * BIOSes will initialize the device for us. If the device is
	 * initialized.
	 */
	err = pci_read_config_byte(pdev, RENESAS_FW_STATUS, &fw_state);
	if (err)
		return pcibios_err_to_errno(err);

	/*
	 * Check if "FW Download Lock" is locked. If it is and the FW is
	 * ready we can simply continue. If the FW is not ready, we have
	 * to give up.
	 */
	if (fw_state & RENESAS_FW_STATUS_LOCK) {
		dev_dbg(&pdev->dev, "FW Download Lock is engaged.");

		if (fw_state & RENESAS_FW_STATUS_SUCCESS)
			return 0;

		dev_err(&pdev->dev,
			"FW Download Lock is set and FW is not ready. Giving Up.");
		return -EIO;
	}

	/*
	 * Check if "FW Download Enable" is set. If someone (us?) tampered
	 * with it and it can't be reset, we have to give up too... and
	 * ask for a forgiveness and a reboot.
	 */
	if (fw_state & RENESAS_FW_STATUS_DOWNLOAD_ENABLE) {
		dev_err(&pdev->dev,
			"FW Download Enable is stale. Giving Up (poweroff/reboot needed).");
		return -EIO;
	}

	/* Otherwise, Check the "Result Code" Bits (6:4) and act accordingly */
	switch (fw_state & RENESAS_FW_STATUS_RESULT) {
	case 0: /* No result yet */
		dev_dbg(&pdev->dev, "FW is not ready/loaded yet.");

		/* tell the caller, that this device needs the firmware. */
		return 1;

	case RENESAS_FW_STATUS_SUCCESS: /* Success, device should be working. */
		dev_dbg(&pdev->dev, "FW is ready.");
		return 0;

	case RENESAS_FW_STATUS_ERROR: /* Error State */
		dev_err(&pdev->dev,
			"hardware is in an error state. Giving up (poweroff/reboot needed).");
		return -ENODEV;

	default: /* All other states are marked as "Reserved states" */
		dev_err(&pdev->dev,
			"hardware is in an invalid state %lx. Giving up (poweroff/reboot needed).",
			(fw_state & RENESAS_FW_STATUS_RESULT) >> 4);
		return -EINVAL;
	}
}

static int renesas_fw_download(struct pci_dev *pdev,
			       const struct firmware *fw)
{
	const u32 *fw_data = (const u32 *)fw->data;
	size_t i;
	int err;
	u8 fw_status;

	/*
	 * For more information and the big picture: please look at the
	 * "Firmware Download Sequence" in "7.1 FW Download Interface"
	 * of R19UH0078EJ0500 Rev.5.00 page 131
	 */

	/*
	 * 0. Set "FW Download Enable" bit in the
	 * "FW Download Control & Status Register" at 0xF4
	 */
	err = pci_write_config_byte(pdev, RENESAS_FW_STATUS,
				    RENESAS_FW_STATUS_DOWNLOAD_ENABLE);
	if (err)
		return pcibios_err_to_errno(err);

	/* 1 - 10 follow one step after the other. */
	for (i = 0; i < fw->size / 4; i++) {
		err = renesas_fw_download_image(pdev, fw_data, i, false);
		if (err) {
			dev_err(&pdev->dev,
				"Firmware Download Step %zd failed at position %zd bytes with (%d).",
				i, i * 4, err);
			return err;
		}
	}

	/*
	 * This sequence continues until the last data is written to
	 * "DATA0" or "DATA1". Naturally, we wait until "SET DATA0/1"
	 * is cleared by the hardware beforehand.
	 */
	for (i = 0; i < RENESAS_RETRY; i++) {
		err = pci_read_config_byte(pdev, RENESAS_FW_STATUS_MSB,
					   &fw_status);
		if (err)
			return pcibios_err_to_errno(err);
		if (!(fw_status & (BIT(0) | BIT(1))))
			break;

		udelay(RENESAS_DELAY);
	}
	if (i == RENESAS_RETRY)
		dev_warn(&pdev->dev, "Final Firmware Download step timed out.");

	/*
	 * 11. After finishing writing the last data of FW, the
	 * System Software must clear "FW Download Enable"
	 */
	err = pci_write_config_byte(pdev, RENESAS_FW_STATUS, 0);
	if (err)
		return pcibios_err_to_errno(err);

	/* 12. Read "Result Code" and confirm it is good. */
	for (i = 0; i < RENESAS_RETRY; i++) {
		err = pci_read_config_byte(pdev, RENESAS_FW_STATUS, &fw_status);
		if (err)
			return pcibios_err_to_errno(err);
		if (fw_status & RENESAS_FW_STATUS_SUCCESS)
			break;

		udelay(RENESAS_DELAY);
	}
	if (i == RENESAS_RETRY) {
		/* Timed out / Error - let's see if we can fix this */
		err = renesas_fw_check_running(pdev);
		switch (err) {
		case 0: /*
			 * we shouldn't end up here.
			 * maybe it took a little bit longer.
			 * But all should be well?
			 */
			break;

		case 1: /* (No result yet! */
			dev_err(&pdev->dev, "FW Load timedout");
			return -ETIMEDOUT;

		default:
			return err;
		}
	}

	return 0;
}

static void renesas_rom_erase(struct pci_dev *pdev)
{
	int retval, i;
	u8 status;

	dev_dbg(&pdev->dev, "Performing ROM Erase...\n");
	retval = pci_write_config_dword(pdev, RENESAS_DATA0,
					RENESAS_ROM_ERASE_MAGIC);
	if (retval) {
		dev_err(&pdev->dev, "ROM erase, magic word write failed: %d\n",
			pcibios_err_to_errno(retval));
		return;
	}

	retval = pci_read_config_byte(pdev, RENESAS_ROM_STATUS, &status);
	if (retval) {
		dev_err(&pdev->dev, "ROM status read failed: %d\n",
			pcibios_err_to_errno(retval));
		return;
	}
	status |= RENESAS_ROM_STATUS_ERASE;
	retval = pci_write_config_byte(pdev, RENESAS_ROM_STATUS, status);
	if (retval) {
		dev_err(&pdev->dev, "ROM erase set word write failed\n");
		return;
	}

	/* sleep a bit while ROM is erased */
	msleep(20);

	for (i = 0; i < RENESAS_RETRY; i++) {
		retval = pci_read_config_byte(pdev, RENESAS_ROM_STATUS,
					      &status);
		status &= RENESAS_ROM_STATUS_ERASE;
		if (!status)
			break;

		mdelay(RENESAS_DELAY);
	}

	if (i == RENESAS_RETRY)
		dev_dbg(&pdev->dev, "Chip erase timedout: %x\n", status);

	dev_dbg(&pdev->dev, "ROM Erase... Done success\n");
}

static bool renesas_setup_rom(struct pci_dev *pdev, const struct firmware *fw)
{
	const u32 *fw_data = (const u32 *)fw->data;
	int err, i;
	u8 status;

	/* 2. Write magic word to Data0 */
	err = pci_write_config_dword(pdev, RENESAS_DATA0,
				     RENESAS_ROM_WRITE_MAGIC);
	if (err)
		return false;

	/* 3. Set External ROM access */
	err = pci_write_config_byte(pdev, RENESAS_ROM_STATUS,
				    RENESAS_ROM_STATUS_ACCESS);
	if (err)
		goto remove_bypass;

	/* 4. Check the result */
	err = pci_read_config_byte(pdev, RENESAS_ROM_STATUS, &status);
	if (err)
		goto remove_bypass;
	status &= GENMASK(6, 4);
	if (status) {
		dev_err(&pdev->dev,
			"setting external rom failed: %x\n", status);
		goto remove_bypass;
	}

	/* 5 to 16 Write FW to DATA0/1 while checking SetData0/1 */
	for (i = 0; i < fw->size / 4; i++) {
		err = renesas_fw_download_image(pdev, fw_data, i, true);
		if (err) {
			dev_err(&pdev->dev,
				"ROM Download Step %d failed at position %d bytes with (%d)\n",
				 i, i * 4, err);
			goto remove_bypass;
		}
	}

	/*
	 * wait till DATA0/1 is cleared
	 */
	for (i = 0; i < RENESAS_RETRY; i++) {
		err = pci_read_config_byte(pdev, RENESAS_ROM_STATUS_MSB,
					   &status);
		if (err)
			goto remove_bypass;
		if (!(status & (BIT(0) | BIT(1))))
			break;

		udelay(RENESAS_DELAY);
	}
	if (i == RENESAS_RETRY) {
		dev_err(&pdev->dev, "Final Firmware ROM Download step timed out\n");
		goto remove_bypass;
	}

	/* 17. Remove bypass */
	err = pci_write_config_byte(pdev, RENESAS_ROM_STATUS, 0);
	if (err)
		return false;

	udelay(10);

	/* 18. check result */
	for (i = 0; i < RENESAS_RETRY; i++) {
		err = pci_read_config_byte(pdev, RENESAS_ROM_STATUS, &status);
		if (err) {
			dev_err(&pdev->dev, "Read ROM status failed:%d\n",
				pcibios_err_to_errno(err));
			return false;
		}
		status &= RENESAS_ROM_STATUS_RESULT;
		if (status ==  RENESAS_ROM_STATUS_SUCCESS) {
			dev_dbg(&pdev->dev, "Download ROM success\n");
			break;
		}
		udelay(RENESAS_DELAY);
	}
	if (i == RENESAS_RETRY) { /* Timed out */
		dev_err(&pdev->dev,
			"Download to external ROM TO: %x\n", status);
		return false;
	}

	dev_dbg(&pdev->dev, "Download to external ROM succeeded\n");

	/* Last step set Reload */
	err = pci_write_config_byte(pdev, RENESAS_ROM_STATUS,
				    RENESAS_ROM_STATUS_RELOAD);
	if (err) {
		dev_err(&pdev->dev, "Set ROM execute failed: %d\n",
			pcibios_err_to_errno(err));
		return false;
	}

	/*
	 * wait till Reload is cleared
	 */
	for (i = 0; i < RENESAS_RETRY; i++) {
		err = pci_read_config_byte(pdev, RENESAS_ROM_STATUS, &status);
		if (err)
			return false;
		if (!(status & RENESAS_ROM_STATUS_RELOAD))
			break;

		udelay(RENESAS_DELAY);
	}
	if (i == RENESAS_RETRY) {
		dev_err(&pdev->dev, "ROM Exec timed out: %x\n", status);
		return false;
	}

	return true;

remove_bypass:
	pci_write_config_byte(pdev, RENESAS_ROM_STATUS, 0);
	return false;
}

static int renesas_load_fw(struct pci_dev *pdev, const struct firmware *fw)
{
	int err = 0;
	bool rom;

	/* Check if the device has external ROM */
	rom = renesas_check_rom(pdev);
	if (rom) {
		/* perform chip erase first */
		renesas_rom_erase(pdev);

		/* lets try loading fw on ROM first */
		rom = renesas_setup_rom(pdev, fw);
		if (!rom) {
			dev_dbg(&pdev->dev,
				"ROM load failed, falling back on FW load\n");
		} else {
			dev_dbg(&pdev->dev,
				"ROM load success\n");
			goto exit;
		}
	}

	err = renesas_fw_download(pdev, fw);

exit:
	if (err)
		dev_err(&pdev->dev, "firmware failed to download (%d).", err);
	return err;
}

int renesas_xhci_check_request_fw(struct pci_dev *pdev,
				  const struct pci_device_id *id)
{
	struct xhci_driver_data *driver_data =
			(struct xhci_driver_data *)id->driver_data;
	const char *fw_name = driver_data->firmware;
	const struct firmware *fw;
	int err;

	err = renesas_fw_check_running(pdev);
	/* Continue ahead, if the firmware is already running. */
	if (err == 0)
		return 0;

	if (err != 1)
		return err;

	pci_dev_get(pdev);
	err = request_firmware(&fw, fw_name, &pdev->dev);
	pci_dev_put(pdev);
	if (err) {
		dev_err(&pdev->dev, "request_firmware failed: %d\n", err);
		return err;
	}

	err = renesas_fw_verify(fw->data, fw->size);
	if (err)
		goto exit;

	err = renesas_load_fw(pdev, fw);
exit:
	release_firmware(fw);
	return err;
}
EXPORT_SYMBOL_GPL(renesas_xhci_check_request_fw);

void renesas_xhci_pci_exit(struct pci_dev *dev)
{
}
EXPORT_SYMBOL_GPL(renesas_xhci_pci_exit);

MODULE_LICENSE("GPL v2");
