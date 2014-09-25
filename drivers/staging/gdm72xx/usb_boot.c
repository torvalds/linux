/*
 * Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/usb.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/firmware.h>

#include <asm/byteorder.h>
#include "gdm_usb.h"
#include "usb_boot.h"

#define DN_KERNEL_MAGIC_NUMBER	0x10760001
#define DN_ROOTFS_MAGIC_NUMBER	0x10760002

#define DOWNLOAD_SIZE		1024

#define MAX_IMG_CNT		16
#define FW_DIR			"gdm72xx/"
#define FW_UIMG			"gdmuimg.bin"
#define FW_KERN			"zImage"
#define FW_FS			"ramdisk.jffs2"

struct dn_header {
	__be32	magic_num;
	__be32	file_size;
};

struct img_header {
	u32	magic_code;
	u32	count;
	u32	len;
	u32	offset[MAX_IMG_CNT];
	char	hostname[32];
	char	date[32];
};

struct fw_info {
	u32	id;
	u32	len;
	u32	kernel_len;
	u32	rootfs_len;
	u32	kernel_offset;
	u32	rootfs_offset;
	u32	fw_ver;
	u32	mac_ver;
	char	hostname[32];
	char	userid[16];
	char	date[32];
	char	user_desc[128];
};

static void array_le32_to_cpu(u32 *arr, int num)
{
	int i;

	for (i = 0; i < num; i++, arr++)
		le32_to_cpus(arr);
}

static u8 *tx_buf;

static int gdm_wibro_send(struct usb_device *usbdev, void *data, int len)
{
	int ret;
	int actual;

	ret = usb_bulk_msg(usbdev, usb_sndbulkpipe(usbdev, 1), data, len,
			   &actual, 1000);

	if (ret < 0) {
		dev_err(&usbdev->dev, "Error : usb_bulk_msg ( result = %d )\n",
			ret);
		return ret;
	}
	return 0;
}

static int gdm_wibro_recv(struct usb_device *usbdev, void *data, int len)
{
	int ret;
	int actual;

	ret = usb_bulk_msg(usbdev, usb_rcvbulkpipe(usbdev, 2), data, len,
			   &actual, 5000);

	if (ret < 0) {
		dev_err(&usbdev->dev,
			"Error : usb_bulk_msg(recv) ( result = %d )\n", ret);
		return ret;
	}
	return 0;
}

static int download_image(struct usb_device *usbdev,
			  const struct firmware *firm,
			  loff_t pos, u32 img_len, u32 magic_num)
{
	struct dn_header h;
	int ret = 0;
	u32 size;

	size = ALIGN(img_len, DOWNLOAD_SIZE);
	h.magic_num = cpu_to_be32(magic_num);
	h.file_size = cpu_to_be32(size);

	ret = gdm_wibro_send(usbdev, &h, sizeof(h));
	if (ret < 0)
		return ret;

	while (img_len > 0) {
		if (img_len > DOWNLOAD_SIZE)
			size = DOWNLOAD_SIZE;
		else
			size = img_len;	/* the last chunk of data */

		memcpy(tx_buf, firm->data + pos, size);
		ret = gdm_wibro_send(usbdev, tx_buf, size);

		if (ret < 0)
			return ret;

		img_len -= size;
		pos += size;
	}

	return ret;
}

int usb_boot(struct usb_device *usbdev, u16 pid)
{
	int i, ret = 0;
	struct img_header hdr;
	struct fw_info fw_info;
	loff_t pos = 0;
	char *img_name = FW_DIR FW_UIMG;
	const struct firmware *firm;

	ret = request_firmware(&firm, img_name, &usbdev->dev);
	if (ret < 0) {
		dev_err(&usbdev->dev,
			"requesting firmware %s failed with error %d\n",
			img_name, ret);
		return ret;
	}

	tx_buf = kmalloc(DOWNLOAD_SIZE, GFP_KERNEL);
	if (tx_buf == NULL)
		return -ENOMEM;

	if (firm->size < sizeof(hdr)) {
		dev_err(&usbdev->dev, "Cannot read the image info.\n");
		ret = -EIO;
		goto out;
	}
	memcpy(&hdr, firm->data, sizeof(hdr));

	array_le32_to_cpu((u32 *)&hdr, 19);

	if (hdr.count > MAX_IMG_CNT) {
		dev_err(&usbdev->dev, "Too many images. %d\n", hdr.count);
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < hdr.count; i++) {
		if (hdr.offset[i] > hdr.len) {
			dev_err(&usbdev->dev,
				"Invalid offset. Entry = %d Offset = 0x%08x Image length = 0x%08x\n",
				i, hdr.offset[i], hdr.len);
			ret = -EINVAL;
			goto out;
		}

		pos = hdr.offset[i];
		if (firm->size < sizeof(fw_info) + pos) {
			dev_err(&usbdev->dev, "Cannot read the FW info.\n");
			ret = -EIO;
			goto out;
		}
		memcpy(&fw_info, firm->data + pos, sizeof(fw_info));

		array_le32_to_cpu((u32 *)&fw_info, 8);

		if ((fw_info.id & 0xffff) != pid)
			continue;

		pos = hdr.offset[i] + fw_info.kernel_offset;
		if (firm->size < fw_info.kernel_len + pos) {
			dev_err(&usbdev->dev, "Kernel FW is too small.\n");
			goto out;
		}

		ret = download_image(usbdev, firm, pos, fw_info.kernel_len,
				     DN_KERNEL_MAGIC_NUMBER);
		if (ret < 0)
			goto out;
		dev_info(&usbdev->dev, "GCT: Kernel download success.\n");

		pos = hdr.offset[i] + fw_info.rootfs_offset;
		if (firm->size < fw_info.rootfs_len + pos) {
			dev_err(&usbdev->dev, "Filesystem FW is too small.\n");
			goto out;
		}
		ret = download_image(usbdev, firm, pos, fw_info.rootfs_len,
				     DN_ROOTFS_MAGIC_NUMBER);
		if (ret < 0)
			goto out;
		dev_info(&usbdev->dev, "GCT: Filesystem download success.\n");

		break;
	}

	if (i == hdr.count) {
		dev_err(&usbdev->dev, "Firmware for gsk%x is not installed.\n",
			pid);
		ret = -EINVAL;
	}
out:
	release_firmware(firm);
	kfree(tx_buf);
	return ret;
}

/*#define GDM7205_PADDING		256 */
#define DOWNLOAD_CHUCK			2048
#define KERNEL_TYPE_STRING		"linux"
#define FS_TYPE_STRING			"rootfs"

static int em_wait_ack(struct usb_device *usbdev, int send_zlp)
{
	int ack;
	int ret = -1;

	if (send_zlp) {
		/*Send ZLP*/
		ret = gdm_wibro_send(usbdev, NULL, 0);
		if (ret < 0)
			goto out;
	}

	/*Wait for ACK*/
	ret = gdm_wibro_recv(usbdev, &ack, sizeof(ack));
	if (ret < 0)
		goto out;
out:
	return ret;
}

static int em_download_image(struct usb_device *usbdev, const char *img_name,
			     char *type_string)
{
	char *buf = NULL;
	loff_t pos = 0;
	int ret = 0;
	int len;
	int img_len;
	const struct firmware *firm;
	#if defined(GDM7205_PADDING)
	const int pad_size = GDM7205_PADDING;
	#else
	const int pad_size = 0;
	#endif

	ret = request_firmware(&firm, img_name, &usbdev->dev);
	if (ret < 0) {
		dev_err(&usbdev->dev,
			"requesting firmware %s failed with error %d\n",
			img_name, ret);
		return ret;
	}

	buf = kmalloc(DOWNLOAD_CHUCK + pad_size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	strcpy(buf+pad_size, type_string);
	ret = gdm_wibro_send(usbdev, buf, strlen(type_string)+pad_size);
	if (ret < 0)
		goto out;

	img_len = firm->size;

	if (img_len <= 0) {
		ret = -1;
		goto out;
	}

	while (img_len > 0) {
		if (img_len > DOWNLOAD_CHUCK)
			len = DOWNLOAD_CHUCK;
		else
			len = img_len; /* the last chunk of data */

		memcpy(buf+pad_size, firm->data + pos, len);
		ret = gdm_wibro_send(usbdev, buf, len+pad_size);

		if (ret < 0)
			goto out;

		img_len -= DOWNLOAD_CHUCK;
		pos += DOWNLOAD_CHUCK;

		ret = em_wait_ack(usbdev, ((len+pad_size) % 512 == 0));
		if (ret < 0)
			goto out;
	}

	ret = em_wait_ack(usbdev, 1);
	if (ret < 0)
		goto out;

out:
	release_firmware(firm);
	kfree(buf);

	return ret;
}

static int em_fw_reset(struct usb_device *usbdev)
{
	/*Send ZLP*/
	return gdm_wibro_send(usbdev, NULL, 0);
}

int usb_emergency(struct usb_device *usbdev)
{
	int ret;
	const char *kern_name = FW_DIR FW_KERN;
	const char *fs_name = FW_DIR FW_FS;

	ret = em_download_image(usbdev, kern_name, KERNEL_TYPE_STRING);
	if (ret < 0)
		return ret;
	dev_err(&usbdev->dev, "GCT Emergency: Kernel download success.\n");

	ret = em_download_image(usbdev, fs_name, FS_TYPE_STRING);
	if (ret < 0)
		return ret;
	dev_info(&usbdev->dev, "GCT Emergency: Filesystem download success.\n");

	ret = em_fw_reset(usbdev);

	return ret;
}
