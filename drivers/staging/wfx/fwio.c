// SPDX-License-Identifier: GPL-2.0-only
/*
 * Firmware loading.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/bitfield.h>

#include "fwio.h"
#include "wfx.h"
#include "hwio.h"

// Addresses below are in SRAM area
#define WFX_DNLD_FIFO             0x09004000
#define     DNLD_BLOCK_SIZE           0x0400
#define     DNLD_FIFO_SIZE            0x8000 // (32 * DNLD_BLOCK_SIZE)
// Download Control Area (DCA)
#define WFX_DCA_IMAGE_SIZE        0x0900C000
#define WFX_DCA_PUT               0x0900C004
#define WFX_DCA_GET               0x0900C008
#define WFX_DCA_HOST_STATUS       0x0900C00C
#define     HOST_READY                0x87654321
#define     HOST_INFO_READ            0xA753BD99
#define     HOST_UPLOAD_PENDING       0xABCDDCBA
#define     HOST_UPLOAD_COMPLETE      0xD4C64A99
#define     HOST_OK_TO_JUMP           0x174FC882
#define WFX_DCA_NCP_STATUS        0x0900C010
#define     NCP_NOT_READY             0x12345678
#define     NCP_READY                 0x87654321
#define     NCP_INFO_READY            0xBD53EF99
#define     NCP_DOWNLOAD_PENDING      0xABCDDCBA
#define     NCP_DOWNLOAD_COMPLETE     0xCAFEFECA
#define     NCP_AUTH_OK               0xD4C64A99
#define     NCP_AUTH_FAIL             0x174FC882
#define     NCP_PUB_KEY_RDY           0x7AB41D19
#define WFX_DCA_FW_SIGNATURE      0x0900C014
#define     FW_SIGNATURE_SIZE         0x40
#define WFX_DCA_FW_HASH           0x0900C054
#define     FW_HASH_SIZE              0x08
#define WFX_DCA_FW_VERSION        0x0900C05C
#define     FW_VERSION_SIZE           0x04
#define WFX_DCA_RESERVED          0x0900C060
#define     DCA_RESERVED_SIZE         0x20
#define WFX_STATUS_INFO           0x0900C080
#define WFX_BOOTLOADER_LABEL      0x0900C084
#define     BOOTLOADER_LABEL_SIZE     0x3C
#define WFX_PTE_INFO              0x0900C0C0
#define     PTE_INFO_KEYSET_IDX       0x0D
#define     PTE_INFO_SIZE             0x10
#define WFX_ERR_INFO              0x0900C0D0
#define     ERR_INVALID_SEC_TYPE      0x05
#define     ERR_SIG_VERIF_FAILED      0x0F
#define     ERR_AES_CTRL_KEY          0x10
#define     ERR_ECC_PUB_KEY           0x11
#define     ERR_MAC_KEY               0x18

#define DCA_TIMEOUT  50 // milliseconds
#define WAKEUP_TIMEOUT 200 // milliseconds

static const char * const fwio_error_strings[] = {
	[ERR_INVALID_SEC_TYPE] = "Invalid section type or wrong encryption",
	[ERR_SIG_VERIF_FAILED] = "Signature verification failed",
	[ERR_AES_CTRL_KEY] = "AES control key not initialized",
	[ERR_ECC_PUB_KEY] = "ECC public key not initialized",
	[ERR_MAC_KEY] = "MAC key not initialized",
};

/*
 * request_firmware() allocate data using vmalloc(). It is not compatible with
 * underlying hardware that use DMA. Function below detect this case and
 * allocate a bounce buffer if necessary.
 *
 * Notice that, in doubt, you can enable CONFIG_DEBUG_SG to ask kernel to
 * detect this problem at runtime  (else, kernel silently fail).
 *
 * NOTE: it may also be possible to use 'pages' from struct firmware and avoid
 * bounce buffer
 */
static int sram_write_dma_safe(struct wfx_dev *wdev, u32 addr, const u8 *buf,
			       size_t len)
{
	int ret;
	const u8 *tmp;

	if (!virt_addr_valid(buf)) {
		tmp = kmemdup(buf, len, GFP_KERNEL);
		if (!tmp)
			return -ENOMEM;
	} else {
		tmp = buf;
	}
	ret = sram_buf_write(wdev, addr, tmp, len);
	if (!virt_addr_valid(buf))
		kfree(tmp);
	return ret;
}

int get_firmware(struct wfx_dev *wdev, u32 keyset_chip,
		 const struct firmware **fw, int *file_offset)
{
	int keyset_file;
	char filename[256];
	const char *data;
	int ret;

	snprintf(filename, sizeof(filename), "%s_%02X.sec", wdev->pdata.file_fw,
		 keyset_chip);
	ret = firmware_request_nowarn(fw, filename, wdev->dev);
	if (ret) {
		dev_info(wdev->dev, "can't load %s, falling back to %s.sec\n",
			 filename, wdev->pdata.file_fw);
		snprintf(filename, sizeof(filename), "%s.sec",
			 wdev->pdata.file_fw);
		ret = request_firmware(fw, filename, wdev->dev);
		if (ret) {
			dev_err(wdev->dev, "can't load %s\n", filename);
			*fw = NULL;
			return ret;
		}
	}

	data = (*fw)->data;
	if (memcmp(data, "KEYSET", 6) != 0) {
		// Legacy firmware format
		*file_offset = 0;
		keyset_file = 0x90;
	} else {
		*file_offset = 8;
		keyset_file = (hex_to_bin(data[6]) * 16) | hex_to_bin(data[7]);
		if (keyset_file < 0) {
			dev_err(wdev->dev, "%s corrupted\n", filename);
			release_firmware(*fw);
			*fw = NULL;
			return -EINVAL;
		}
	}
	if (keyset_file != keyset_chip) {
		dev_err(wdev->dev, "firmware keyset is incompatible with chip (file: 0x%02X, chip: 0x%02X)\n",
			keyset_file, keyset_chip);
		release_firmware(*fw);
		*fw = NULL;
		return -ENODEV;
	}
	wdev->keyset = keyset_file;
	return 0;
}

static int wait_ncp_status(struct wfx_dev *wdev, u32 status)
{
	ktime_t now, start;
	u32 reg;
	int ret;

	start = ktime_get();
	for (;;) {
		ret = sram_reg_read(wdev, WFX_DCA_NCP_STATUS, &reg);
		if (ret < 0)
			return -EIO;
		now = ktime_get();
		if (reg == status)
			break;
		if (ktime_after(now, ktime_add_ms(start, DCA_TIMEOUT)))
			return -ETIMEDOUT;
	}
	if (ktime_compare(now, start))
		dev_dbg(wdev->dev, "chip answer after %lldus\n",
			ktime_us_delta(now, start));
	else
		dev_dbg(wdev->dev, "chip answer immediately\n");
	return 0;
}

static int upload_firmware(struct wfx_dev *wdev, const u8 *data, size_t len)
{
	int ret;
	u32 offs, bytes_done;
	ktime_t now, start;

	if (len % DNLD_BLOCK_SIZE) {
		dev_err(wdev->dev, "firmware size is not aligned. Buffer overrun will occur\n");
		return -EIO;
	}
	offs = 0;
	while (offs < len) {
		start = ktime_get();
		for (;;) {
			ret = sram_reg_read(wdev, WFX_DCA_GET, &bytes_done);
			if (ret < 0)
				return ret;
			now = ktime_get();
			if (offs +
			    DNLD_BLOCK_SIZE - bytes_done < DNLD_FIFO_SIZE)
				break;
			if (ktime_after(now, ktime_add_ms(start, DCA_TIMEOUT)))
				return -ETIMEDOUT;
		}
		if (ktime_compare(now, start))
			dev_dbg(wdev->dev, "answer after %lldus\n",
				ktime_us_delta(now, start));

		ret = sram_write_dma_safe(wdev, WFX_DNLD_FIFO +
					  (offs % DNLD_FIFO_SIZE),
					  data + offs, DNLD_BLOCK_SIZE);
		if (ret < 0)
			return ret;

		// WFx seems to not support writing 0 in this register during
		// first loop
		offs += DNLD_BLOCK_SIZE;
		ret = sram_reg_write(wdev, WFX_DCA_PUT, offs);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static void print_boot_status(struct wfx_dev *wdev)
{
	u32 val32;

	sram_reg_read(wdev, WFX_STATUS_INFO, &val32);
	if (val32 == 0x12345678) {
		dev_info(wdev->dev, "no error reported by secure boot\n");
	} else {
		sram_reg_read(wdev, WFX_ERR_INFO, &val32);
		if (val32 < ARRAY_SIZE(fwio_error_strings) &&
		    fwio_error_strings[val32])
			dev_info(wdev->dev, "secure boot error: %s\n",
				 fwio_error_strings[val32]);
		else
			dev_info(wdev->dev,
				 "secure boot error: Unknown (0x%02x)\n",
				 val32);
	}
}

static int load_firmware_secure(struct wfx_dev *wdev)
{
	const struct firmware *fw = NULL;
	int header_size;
	int fw_offset;
	ktime_t start;
	u8 *buf;
	int ret;

	BUILD_BUG_ON(PTE_INFO_SIZE > BOOTLOADER_LABEL_SIZE);
	buf = kmalloc(BOOTLOADER_LABEL_SIZE + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	sram_reg_write(wdev, WFX_DCA_HOST_STATUS, HOST_READY);
	ret = wait_ncp_status(wdev, NCP_INFO_READY);
	if (ret)
		goto error;

	sram_buf_read(wdev, WFX_BOOTLOADER_LABEL, buf, BOOTLOADER_LABEL_SIZE);
	buf[BOOTLOADER_LABEL_SIZE] = 0;
	dev_dbg(wdev->dev, "bootloader: \"%s\"\n", buf);

	sram_buf_read(wdev, WFX_PTE_INFO, buf, PTE_INFO_SIZE);
	ret = get_firmware(wdev, buf[PTE_INFO_KEYSET_IDX], &fw, &fw_offset);
	if (ret)
		goto error;
	header_size = fw_offset + FW_SIGNATURE_SIZE + FW_HASH_SIZE;

	sram_reg_write(wdev, WFX_DCA_HOST_STATUS, HOST_INFO_READ);
	ret = wait_ncp_status(wdev, NCP_READY);
	if (ret)
		goto error;

	sram_reg_write(wdev, WFX_DNLD_FIFO, 0xFFFFFFFF); // Fifo init
	sram_write_dma_safe(wdev, WFX_DCA_FW_VERSION, "\x01\x00\x00\x00",
			    FW_VERSION_SIZE);
	sram_write_dma_safe(wdev, WFX_DCA_FW_SIGNATURE, fw->data + fw_offset,
			    FW_SIGNATURE_SIZE);
	sram_write_dma_safe(wdev, WFX_DCA_FW_HASH,
			    fw->data + fw_offset + FW_SIGNATURE_SIZE,
			    FW_HASH_SIZE);
	sram_reg_write(wdev, WFX_DCA_IMAGE_SIZE, fw->size - header_size);
	sram_reg_write(wdev, WFX_DCA_HOST_STATUS, HOST_UPLOAD_PENDING);
	ret = wait_ncp_status(wdev, NCP_DOWNLOAD_PENDING);
	if (ret)
		goto error;

	start = ktime_get();
	ret = upload_firmware(wdev, fw->data + header_size,
			      fw->size - header_size);
	if (ret)
		goto error;
	dev_dbg(wdev->dev, "firmware load after %lldus\n",
		ktime_us_delta(ktime_get(), start));

	sram_reg_write(wdev, WFX_DCA_HOST_STATUS, HOST_UPLOAD_COMPLETE);
	ret = wait_ncp_status(wdev, NCP_AUTH_OK);
	// Legacy ROM support
	if (ret < 0)
		ret = wait_ncp_status(wdev, NCP_PUB_KEY_RDY);
	if (ret < 0)
		goto error;
	sram_reg_write(wdev, WFX_DCA_HOST_STATUS, HOST_OK_TO_JUMP);

error:
	kfree(buf);
	if (fw)
		release_firmware(fw);
	if (ret)
		print_boot_status(wdev);
	return ret;
}

static int init_gpr(struct wfx_dev *wdev)
{
	int ret, i;
	static const struct {
		int index;
		u32 value;
	} gpr_init[] = {
		{ 0x07, 0x208775 },
		{ 0x08, 0x2EC020 },
		{ 0x09, 0x3C3C3C },
		{ 0x0B, 0x322C44 },
		{ 0x0C, 0xA06497 },
	};

	for (i = 0; i < ARRAY_SIZE(gpr_init); i++) {
		ret = igpr_reg_write(wdev, gpr_init[i].index,
				     gpr_init[i].value);
		if (ret < 0)
			return ret;
		dev_dbg(wdev->dev, "  index %02x: %08x\n", gpr_init[i].index,
			gpr_init[i].value);
	}
	return 0;
}

int wfx_init_device(struct wfx_dev *wdev)
{
	int ret;
	int hw_revision, hw_type;
	int wakeup_timeout = 50; // ms
	ktime_t now, start;
	u32 reg;

	reg = CFG_DIRECT_ACCESS_MODE | CFG_CPU_RESET | CFG_WORD_MODE2;
	if (wdev->pdata.use_rising_clk)
		reg |= CFG_CLK_RISE_EDGE;
	ret = config_reg_write(wdev, reg);
	if (ret < 0) {
		dev_err(wdev->dev, "bus returned an error during first write access. Host configuration error?\n");
		return -EIO;
	}

	ret = config_reg_read(wdev, &reg);
	if (ret < 0) {
		dev_err(wdev->dev, "bus returned an error during first read access. Bus configuration error?\n");
		return -EIO;
	}
	if (reg == 0 || reg == ~0) {
		dev_err(wdev->dev, "chip mute. Bus configuration error or chip wasn't reset?\n");
		return -EIO;
	}
	dev_dbg(wdev->dev, "initial config register value: %08x\n", reg);

	hw_revision = FIELD_GET(CFG_DEVICE_ID_MAJOR, reg);
	if (hw_revision == 0 || hw_revision > 2) {
		dev_err(wdev->dev, "bad hardware revision number: %d\n",
			hw_revision);
		return -ENODEV;
	}
	hw_type = FIELD_GET(CFG_DEVICE_ID_TYPE, reg);
	if (hw_type == 1) {
		dev_notice(wdev->dev, "development hardware detected\n");
		wakeup_timeout = 2000;
	}

	ret = init_gpr(wdev);
	if (ret < 0)
		return ret;

	ret = control_reg_write(wdev, CTRL_WLAN_WAKEUP);
	if (ret < 0)
		return -EIO;
	start = ktime_get();
	for (;;) {
		ret = control_reg_read(wdev, &reg);
		now = ktime_get();
		if (reg & CTRL_WLAN_READY)
			break;
		if (ktime_after(now, ktime_add_ms(start, wakeup_timeout))) {
			dev_err(wdev->dev, "chip didn't wake up. Chip wasn't reset?\n");
			return -ETIMEDOUT;
		}
	}
	dev_dbg(wdev->dev, "chip wake up after %lldus\n",
		ktime_us_delta(now, start));

	ret = config_reg_write_bits(wdev, CFG_CPU_RESET, 0);
	if (ret < 0)
		return ret;
	ret = load_firmware_secure(wdev);
	if (ret < 0)
		return ret;
	ret = config_reg_write_bits(wdev,
				    CFG_DIRECT_ACCESS_MODE |
				    CFG_IRQ_ENABLE_DATA |
				    CFG_IRQ_ENABLE_WRDY,
				    CFG_IRQ_ENABLE_DATA);
	return ret;
}
