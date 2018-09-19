/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt76x0.h"
#include "mcu.h"

#define MT7610E_FIRMWARE	"mediatek/mt7610e.bin"
#define MT7650E_FIRMWARE	"mediatek/mt7650e.bin"

#define MT_MCU_IVB_ADDR		(MT_MCU_ILM_ADDR + 0x54000 - MT_MCU_IVB_SIZE)

static int mt76x0e_load_firmware(struct mt76x0_dev *dev)
{
	bool is_combo_chip = mt76_chip(&dev->mt76) != 0x7610;
	u32 val, ilm_len, dlm_len, offset = 0;
	const struct mt76x02_fw_header *hdr;
	const struct firmware *fw;
	const char *firmware;
	const u8 *fw_payload;
	int len, err;

	if (mt76x0_firmware_running(dev))
		return 0;

	if (is_combo_chip)
		firmware = MT7650E_FIRMWARE;
	else
		firmware = MT7610E_FIRMWARE;

	err = request_firmware(&fw, firmware, dev->mt76.dev);
	if (err)
		return err;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		err = -EIO;
		goto out;
	}

	hdr = (const struct mt76x02_fw_header *)fw->data;

	len = sizeof(*hdr);
	len += le32_to_cpu(hdr->ilm_len);
	len += le32_to_cpu(hdr->dlm_len);

	if (fw->size != len) {
		err = -EIO;
		goto out;
	}

	fw_payload = fw->data + sizeof(*hdr);

	val = le16_to_cpu(hdr->fw_ver);
	dev_info(dev->mt76.dev, "Firmware Version: %d.%d.%02d\n",
		 (val >> 12) & 0xf, (val >> 8) & 0xf, val & 0xf);

	val = le16_to_cpu(hdr->fw_ver);
	dev_dbg(dev->mt76.dev,
		"Firmware Version: %d.%d.%02d Build: %x Build time: %.16s\n",
		(val >> 12) & 0xf, (val >> 8) & 0xf, val & 0xf,
		le16_to_cpu(hdr->build_ver), hdr->build_time);

	if (is_combo_chip && !mt76_poll(dev, MT_MCU_SEMAPHORE_00, 1, 1, 600)) {
		dev_err(dev->mt76.dev,
			"Could not get hardware semaphore for loading fw\n");
		err = -ETIMEDOUT;
		goto out;
	}

	/* upload ILM. */
	mt76_wr(dev, MT_MCU_PCIE_REMAP_BASE4, 0);
	ilm_len = le32_to_cpu(hdr->ilm_len);
	if (is_combo_chip) {
		ilm_len -= MT_MCU_IVB_SIZE;
		offset = MT_MCU_IVB_SIZE;
	}
	dev_dbg(dev->mt76.dev, "loading FW - ILM %u\n", ilm_len);
	mt76_wr_copy(dev, MT_MCU_ILM_ADDR + offset, fw_payload + offset,
		     ilm_len);

	/* upload IVB. */
	if (is_combo_chip) {
		dev_dbg(dev->mt76.dev, "loading FW - IVB %u\n",
			MT_MCU_IVB_SIZE);
		mt76_wr_copy(dev, MT_MCU_IVB_ADDR, fw_payload, MT_MCU_IVB_SIZE);
	}

	/* upload DLM. */
	mt76_wr(dev, MT_MCU_PCIE_REMAP_BASE4, MT_MCU_DLM_OFFSET);
	dlm_len = le32_to_cpu(hdr->dlm_len);
	dev_dbg(dev->mt76.dev, "loading FW - DLM %u\n", dlm_len);
	mt76_wr_copy(dev, MT_MCU_ILM_ADDR,
		     fw_payload + le32_to_cpu(hdr->ilm_len), dlm_len);

	/* trigger firmware */
	mt76_wr(dev, MT_MCU_PCIE_REMAP_BASE4, 0);
	if (is_combo_chip)
		mt76_wr(dev, MT_MCU_INT_LEVEL, 0x3);
	else
		mt76_wr(dev, MT_MCU_RESET_CTL, 0x300);

	if (!mt76_poll_msec(dev, MT_MCU_COM_REG0, 1, 1, 1000)) {
		dev_err(dev->mt76.dev, "Firmware failed to start\n");
		err = -ETIMEDOUT;
		goto out;
	}

	dev_dbg(dev->mt76.dev, "Firmware running!\n");

out:
	if (is_combo_chip)
		mt76_wr(dev, MT_MCU_SEMAPHORE_00, 0x1);
	release_firmware(fw);

	return err;
}

static int
mt76x0e_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mt76x0_dev *dev;
	int ret = -ENODEV;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	dev = mt76x0_alloc_device(&pdev->dev, NULL);
	if (!dev)
		return -ENOMEM;

	mt76_mmio_init(&dev->mt76, pcim_iomap_table(pdev)[0]);

	dev->mt76.rev = mt76_rr(dev, MT_ASIC_VERSION);
	dev_info(dev->mt76.dev, "ASIC revision: %08x\n", dev->mt76.rev);

	ret = mt76x0e_load_firmware(dev);
	if (ret < 0)
		goto error;

	return 0;

error:
	ieee80211_free_hw(mt76_hw(dev));
	return ret;
}

static void
mt76x0e_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);

	mt76_unregister_device(mdev);
	ieee80211_free_hw(mdev->hw);
}

static const struct pci_device_id mt76x0e_device_table[] = {
	{ PCI_DEVICE(0x14c3, 0x7630) },
	{ PCI_DEVICE(0x14c3, 0x7650) },
	{ },
};

MODULE_DEVICE_TABLE(pci, mt76x0e_device_table);
MODULE_LICENSE("Dual BSD/GPL");

static struct pci_driver mt76x0e_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt76x0e_device_table,
	.probe		= mt76x0e_probe,
	.remove		= mt76x0e_remove,
};

module_pci_driver(mt76x0e_driver);
