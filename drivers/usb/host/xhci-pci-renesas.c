// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2020 Linaro Limited */

#include <linux/acpi.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/unaligned/access_ok.h>

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
  #define RENESAS_ROM_STATUS_INVALID			0
  #define RENESAS_ROM_STATUS_SUCCESS			BIT(4)
  #define RENESAS_ROM_STATUS_ERROR			BIT(5)
#define RENESAS_ROM_STATUS_SET_DATA0			BIT(8)
#define RENESAS_ROM_STATUS_SET_DATA1			BIT(9)
#define RENESAS_ROM_STATUS_ROM_EXISTS			BIT(15)

#define RENESAS_ROM_ERASE_MAGIC				0x5A65726F
#define RENESAS_ROM_WRITE_MAGIC				0x53524F4D

#define RENESAS_RETRY	10000
#define RENESAS_DELAY	10

static struct hc_driver __read_mostly xhci_pci_hc_driver;

static const struct xhci_driver_overrides xhci_pci_overrides __initconst = {
	.reset = xhci_pci_setup,
};

static const struct renesas_fw_entry {
	const char *firmware_name;
	u16 device;
	u8 revision;
	u16 expected_version;
} renesas_fw_table[] = {
	/*
	 * Only the uPD720201K8-711-BAC-A or uPD720202K8-711-BAA-A
	 * are listed in R19UH0078EJ0500 Rev.5.00 as devices which
	 * need the software loader.
	 *
	 * PP2U/ReleaseNote_USB3-201-202-FW.txt:
	 *
	 * Note: This firmware is for the following devices.
	 *  - uPD720201 ES 2.0 sample whose revision ID is 2.
	 *  - uPD720201 ES 2.1 sample & CS sample & Mass product, ID is 3.
	 *  - uPD720202 ES 2.0 sample & CS sample & Mass product, ID is 2.
	 */
	{ "K2013080.mem", 0x0014, 0x02, 0x2013 },
	{ "K2013080.mem", 0x0014, 0x03, 0x2013 },
	{ "K2013080.mem", 0x0015, 0x02, 0x2013 },
};

MODULE_FIRMWARE("K2013080.mem");

static const struct renesas_fw_entry *renesas_needs_fw_dl(struct pci_dev *dev)
{
	const struct renesas_fw_entry *entry;
	size_t i;

	/* This loader will only work with a RENESAS device. */
	if (!(dev->vendor == PCI_VENDOR_ID_RENESAS))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(renesas_fw_table); i++) {
		entry = &renesas_fw_table[i];
		if (entry->device == dev->device &&
		    entry->revision == dev->revision)
			return entry;
	}

	return NULL;
}

static int renesas_fw_download_image(struct pci_dev *dev,
				     const u32 *fw,
				     size_t step)
{
	size_t i;
	int err;
	u8 fw_status;
	bool data0_or_data1;

	/*
	 * The hardware does alternate between two 32-bit pages.
	 * (This is because each row of the firmware is 8 bytes).
	 *
	 * for even steps we use DATA0, for odd steps DATA1.
	 */
	data0_or_data1 = (step & 1) == 1;

	/* step+1. Read "Set DATAX" and confirm it is cleared. */
	for (i = 0; i < RENESAS_RETRY; i++) {
		err = pci_read_config_byte(dev, RENESAS_FW_STATUS_MSB,
					   &fw_status);
		if (err)
			return pcibios_err_to_errno(err);
		if (!(fw_status & BIT(data0_or_data1)))
			break;

		udelay(RENESAS_DELAY);
	}
	if (i == RENESAS_RETRY)
		return -ETIMEDOUT;

	/*
	 * step+2. Write FW data to "DATAX".
	 * "LSB is left" => force little endian
	 */
	err = pci_write_config_dword(dev, data0_or_data1 ?
				     RENESAS_DATA1 : RENESAS_DATA0,
				     (__force u32)cpu_to_le32(fw[step]));
	if (err)
		return pcibios_err_to_errno(err);

	udelay(100);

	/* step+3. Set "Set DATAX". */
	err = pci_write_config_byte(dev, RENESAS_FW_STATUS_MSB,
				    BIT(data0_or_data1));
	if (err)
		return pcibios_err_to_errno(err);

	return 0;
}

static int renesas_fw_verify(struct pci_dev *dev,
			     const void *fw_data,
			     size_t length)
{
	const struct renesas_fw_entry *entry = renesas_needs_fw_dl(dev);
	u16 fw_version_pointer;
	u16 fw_version;

	if (!entry)
		return -EINVAL;

	/*
	 * The Firmware's Data Format is describe in
	 * "6.3 Data Format" R19UH0078EJ0500 Rev.5.00 page 124
	 */

	/*
	 * The bootrom chips of the big brother have sizes up to 64k, let's
	 * assume that's the biggest the firmware can get.
	 */
	if (length < 0x1000 || length >= 0x10000) {
		dev_err(&dev->dev, "firmware is size %zd is not (4k - 64k).",
			length);
		return -EINVAL;
	}

	/* The First 2 bytes are fixed value (55aa). "LSB on Left" */
	if (get_unaligned_le16(fw_data) != 0x55aa) {
		dev_err(&dev->dev, "no valid firmware header found.");
		return -EINVAL;
	}

	/* verify the firmware version position and print it. */
	fw_version_pointer = get_unaligned_le16(fw_data + 4);
	if (fw_version_pointer + 2 >= length) {
		dev_err(&dev->dev,
			"firmware version pointer is outside of the firmware image.");
		return -EINVAL;
	}

	fw_version = get_unaligned_le16(fw_data + fw_version_pointer);
	dev_dbg(&dev->dev, "got firmware version: %02x.", fw_version);

	if (fw_version != entry->expected_version) {
		dev_err(&dev->dev,
			"firmware version mismatch, expected version: %02x.",
			entry->expected_version);
		return -EINVAL;
	}

	return 0;
}
static int renesas_check_rom_state(struct pci_dev *pdev)
{
	const struct renesas_fw_entry *entry;
	u16 rom_state;
	u32 version;
	bool valid_version = false;
	int err, i;

	/* check FW version */
	err = pci_read_config_dword(pdev, RENESAS_FW_VERSION, &version);
	if (err)
		return pcibios_err_to_errno(err);

	version &= RENESAS_FW_VERSION_FIELD;
	version = version >> RENESAS_FW_VERSION_OFFSET;
	dev_dbg(&pdev->dev, "Found FW version loaded is %x\n", version);

	/* treat version in renesas_fw_table as correct ones */
	for (i = 0; i < ARRAY_SIZE(renesas_fw_table); i++) {
		entry = &renesas_fw_table[i];
		if (version == entry->expected_version) {
			dev_dbg(&pdev->dev, "Detected valid ROM version..\n");
			valid_version = true;
		}
	}
	if (valid_version == false)
		dev_dbg(&pdev->dev, "Didn't find valid ROM version\n");

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
			dev_dbg(&pdev->dev, "Success ROM load...");
			/* we have valid version and status so success */
			if (valid_version)
				return 0;
			break;

		case RENESAS_ROM_STATUS_INVALID: /* No result yet */
			dev_dbg(&pdev->dev, "No result as it is ROM...");
			/* we have valid version and status so success */
			if (valid_version)
				return 0;
			break;

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
	int err;
	u8 fw_state;

	/* Check if device has ROM and loaded, if so skip everything */
	err = renesas_check_rom_state(pdev);
	if (!err)
		return err;

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
	 * with it and it can't be resetted, we have to give up too... and
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
		err = renesas_fw_download_image(pdev, fw_data, i);
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
			return -ETIMEDOUT;

		default:
			return err;
		}
	}
	/*
	 * Optional last step: Engage Firmware Lock
	 *
	 * err = pci_write_config_byte(pdev, 0xF4, BIT(2));
	 * if (err)
	 *	return pcibios_err_to_errno(err);
	 */

	return 0;
}

struct renesas_fw_ctx {
	struct pci_dev *pdev;
	const struct pci_device_id *id;
	bool resume;
	const struct renesas_fw_entry *entry;
};

static bool renesas_check_rom(struct pci_dev *pdev)
{
	u16 rom_status;
	int retval;

	/* 1. Check if external ROM exists */
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

static bool renesas_download_rom(struct pci_dev *pdev,
				 const u32 *fw, size_t step)
{
	bool data0_or_data1;
	u8 fw_status;
	size_t i;
	int err;

	/*
	 * The hardware does alternate between two 32-bit pages.
	 * (This is because each row of the firmware is 8 bytes).
	 *
	 * for even steps we use DATA0, for odd steps DATA1.
	 */
	data0_or_data1 = (step & 1) == 1;

	/* Read "Set DATAX" and confirm it is cleared. */
	for (i = 0; i < RENESAS_RETRY; i++) {
		err = pci_read_config_byte(pdev, RENESAS_ROM_STATUS_MSB,
					   &fw_status);
		if (err) {
			dev_err(&pdev->dev, "Read ROM Status failed: %d\n",
				pcibios_err_to_errno(err));
			return false;
		}
		if (!(fw_status & BIT(data0_or_data1)))
			break;

		udelay(RENESAS_DELAY);
	}
	if (i == RENESAS_RETRY) {
		dev_err(&pdev->dev, "Timeout for Set DATAX step: %zd\n", step);
		return false;
	}

	/*
	 * Write FW data to "DATAX".
	 * "LSB is left" => force little endian
	 */
	err = pci_write_config_dword(pdev, data0_or_data1 ?
				     RENESAS_DATA1 : RENESAS_DATA0,
				     (__force u32)cpu_to_le32(fw[step]));
	if (err) {
		dev_err(&pdev->dev, "Write to DATAX failed: %d\n",
			pcibios_err_to_errno(err));
		return false;
	}

	udelay(100);

	/* Set "Set DATAX". */
	err = pci_write_config_byte(pdev, RENESAS_ROM_STATUS_MSB,
				    BIT(data0_or_data1));
	if (err) {
		dev_err(&pdev->dev, "Write config for DATAX failed: %d\n",
			pcibios_err_to_errno(err));
		return false;
	}

	return true;
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
		err = renesas_download_rom(pdev, fw_data, i);
		if (!err) {
			dev_err(&pdev->dev,
				"ROM Download Step %d failed at position %d bytes\n",
				 i, i * 4);
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

	dev_dbg(&pdev->dev, "Download to external ROM scuceeded\n");

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

static void renesas_fw_callback(const struct firmware *fw,
				void *context)
{
	struct renesas_fw_ctx *ctx = context;
	struct pci_dev *pdev = ctx->pdev;
	struct device *parent = pdev->dev.parent;
	bool rom;
	int err;

	if (!fw) {
		dev_err(&pdev->dev, "firmware failed to load\n");

		goto cleanup;
	}

	err = renesas_fw_verify(pdev, fw->data, fw->size);
	if (err)
		goto cleanup;

	/* Check if the device has external ROM */
	rom = renesas_check_rom(pdev);
	if (rom) {
		/* perfrom chip erase first */
		renesas_rom_erase(pdev);

		/* lets try loading fw on ROM first */
		rom = renesas_setup_rom(pdev, fw);
		if (!rom) {
			dev_err(&pdev->dev,
				"ROM load failed, falling back on FW load\n");
		} else {
			dev_dbg(&pdev->dev, "ROM load done..\n");

			release_firmware(fw);
			goto do_probe;
		}
	}

	err = renesas_fw_download(pdev, fw);
	release_firmware(fw);
	if (err) {
		dev_err(&pdev->dev, "firmware failed to download (%d).", err);
		goto cleanup;
	}

do_probe:
	if (ctx->resume)
		return;

	err = xhci_pci_probe(pdev, ctx->id);
	if (!err) {
		/* everything worked */
		devm_kfree(&pdev->dev, ctx);
		return;
	}

cleanup:
	/* in case of an error - fall through */
	dev_info(&pdev->dev, "Unloading driver");

	if (parent)
		device_lock(parent);

	device_release_driver(&pdev->dev);

	if (parent)
		device_unlock(parent);

	pci_dev_put(pdev);
}

static int renesas_fw_alive_check(struct pci_dev *pdev)
{
	const struct renesas_fw_entry *entry;

	/* check if we have a eligible RENESAS' uPD720201/2 w/o FW. */
	entry = renesas_needs_fw_dl(pdev);
	if (!entry)
		return 0;

	return renesas_fw_check_running(pdev);
}

static int renesas_fw_download_to_hw(struct pci_dev *pdev,
				     const struct pci_device_id *id,
				     bool do_resume)
{
	const struct renesas_fw_entry *entry;
	struct renesas_fw_ctx *ctx;
	int err;

	/* check if we have a eligible RENESAS' uPD720201/2 w/o FW. */
	entry = renesas_needs_fw_dl(pdev);
	if (!entry)
		return 0;

	err = renesas_fw_check_running(pdev);
	/* Continue ahead, if the firmware is already running. */
	if (err == 0)
		return 0;

	if (err != 1)
		return err;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->pdev = pdev;
	ctx->resume = do_resume;
	ctx->id = id;
	ctx->entry = entry;

	pci_dev_get(pdev);
	err = request_firmware_nowait(THIS_MODULE, 1, entry->firmware_name,
				      &pdev->dev, GFP_KERNEL,
				      ctx, renesas_fw_callback);
	if (err) {
		pci_dev_put(pdev);
		return err;
	}

	/*
	 * The renesas_fw_callback() callback will continue the probe
	 * process, once it aquires the firmware.
	 */
	return 1;
}

static int renesas_xhci_pci_probe(struct pci_dev *dev,
				  const struct pci_device_id *id)
{
	int retval;

	/*
	 * Check if this device is a RENESAS uPD720201/2 device.
	 * Otherwise, we can continue with xhci_pci_probe as usual.
	 */
	retval = renesas_fw_download_to_hw(dev, id, false);
	switch (retval) {
	case 0:
		break;

	case 1: /* let it load the firmware and recontinue the probe. */
		return 0;

	default:
		return retval;
	};

	return xhci_pci_probe(dev, id);
}

static void renesas_xhci_pci_remove(struct pci_dev *dev)
{
	if (renesas_fw_alive_check(dev)) {
		/*
		 * bail out early, if this was a renesas device w/o FW.
		 * Else we might hit the NMI watchdog in xhci_handsake
		 * during xhci_reset as part of the driver's unloading.
		 * which we forced in the renesas_fw_callback().
		 */
		return;
	}

	xhci_pci_remove(dev);
}

static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(0x1912, 0x0014),
		.driver_data =	(unsigned long)&xhci_pci_hc_driver,
	},
	{ PCI_DEVICE(0x1912, 0x0015),
		.driver_data =	(unsigned long)&xhci_pci_hc_driver,
	},
	{ /* sentinal */ }
};
MODULE_DEVICE_TABLE(pci, pci_ids);

static struct pci_driver renesas_xhci_pci_driver = {
	.name =		"renesas xhci",
	.id_table =	pci_ids,

	.probe =	renesas_xhci_pci_probe,
	.remove =	renesas_xhci_pci_remove,
	/* suspend and resume implemented later */

	.shutdown =	usb_hcd_pci_shutdown,
#ifdef CONFIG_PM
	.driver = {
		.pm = &usb_hcd_pci_pm_ops
	},
#endif
};

static int __init xhci_pci_init(void)
{
	xhci_init_driver(&xhci_pci_hc_driver, &xhci_pci_overrides);
#ifdef CONFIG_PM
	xhci_pci_hc_driver.pci_suspend = xhci_pci_suspend;
	xhci_pci_hc_driver.pci_resume = xhci_pci_resume;
#endif
	return pci_register_driver(&renesas_xhci_pci_driver);
}
module_init(xhci_pci_init);

static void __exit xhci_pci_exit(void)
{
	pci_unregister_driver(&renesas_xhci_pci_driver);
}
module_exit(xhci_pci_exit);

MODULE_DESCRIPTION("xHCI PCI Host Controller Driver");
MODULE_LICENSE("GPL");
