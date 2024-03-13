// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 pureLiFi
 */

#include <linux/firmware.h>
#include <linux/bitrev.h>

#include "mac.h"
#include "usb.h"

static int send_vendor_request(struct usb_device *udev, int request,
			       unsigned char *buffer, int buffer_size)
{
	return usb_control_msg(udev,
			       usb_rcvctrlpipe(udev, 0),
			       request, 0xC0, 0, 0,
			       buffer, buffer_size, PLF_USB_TIMEOUT);
}

static int send_vendor_command(struct usb_device *udev, int request,
			       unsigned char *buffer, int buffer_size)
{
	return usb_control_msg(udev,
			       usb_sndctrlpipe(udev, 0),
			       request, USB_TYPE_VENDOR /*0x40*/, 0, 0,
			       buffer, buffer_size, PLF_USB_TIMEOUT);
}

int plfxlc_download_fpga(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	unsigned char *fpga_dmabuff = NULL;
	const struct firmware *fw = NULL;
	int blk_tran_len = PLF_BULK_TLEN;
	unsigned char *fw_data;
	const char *fw_name;
	int r, actual_length;
	int fw_data_i = 0;

	if ((le16_to_cpu(udev->descriptor.idVendor) ==
				PURELIFI_X_VENDOR_ID_0) &&
	    (le16_to_cpu(udev->descriptor.idProduct) ==
				PURELIFI_X_PRODUCT_ID_0)) {
		fw_name = "plfxlc/lifi-x.bin";
		dev_dbg(&intf->dev, "bin file for X selected\n");

	} else if ((le16_to_cpu(udev->descriptor.idVendor)) ==
					PURELIFI_XC_VENDOR_ID_0 &&
		   (le16_to_cpu(udev->descriptor.idProduct) ==
					PURELIFI_XC_PRODUCT_ID_0)) {
		fw_name = "plfxlc/lifi-xc.bin";
		dev_dbg(&intf->dev, "bin file for XC selected\n");

	} else {
		r = -EINVAL;
		goto error;
	}

	r = request_firmware(&fw, fw_name, &intf->dev);
	if (r) {
		dev_err(&intf->dev, "request_firmware failed (%d)\n", r);
		goto error;
	}
	fpga_dmabuff = kmalloc(PLF_FPGA_STATUS_LEN, GFP_KERNEL);

	if (!fpga_dmabuff) {
		r = -ENOMEM;
		goto error_free_fw;
	}
	send_vendor_request(udev, PLF_VNDR_FPGA_SET_REQ,
			    fpga_dmabuff, PLF_FPGA_STATUS_LEN);

	send_vendor_command(udev, PLF_VNDR_FPGA_SET_CMD, NULL, 0);

	if (fpga_dmabuff[0] != PLF_FPGA_MG) {
		dev_err(&intf->dev, "fpga_dmabuff[0] is wrong\n");
		r = -EINVAL;
		goto error_free_fw;
	}

	for (fw_data_i = 0; fw_data_i < fw->size;) {
		int tbuf_idx;

		if ((fw->size - fw_data_i) < blk_tran_len)
			blk_tran_len = fw->size - fw_data_i;

		fw_data = kmemdup(&fw->data[fw_data_i], blk_tran_len,
				  GFP_KERNEL);
		if (!fw_data) {
			r = -ENOMEM;
			goto error_free_fw;
		}

		for (tbuf_idx = 0; tbuf_idx < blk_tran_len; tbuf_idx++) {
			/* u8 bit reverse */
			fw_data[tbuf_idx] = bitrev8(fw_data[tbuf_idx]);
		}
		r = usb_bulk_msg(udev,
				 usb_sndbulkpipe(interface_to_usbdev(intf),
						 fpga_dmabuff[0] & 0xff),
				 fw_data,
				 blk_tran_len,
				 &actual_length,
				 2 * PLF_USB_TIMEOUT);

		if (r)
			dev_err(&intf->dev, "Bulk msg failed (%d)\n", r);

		kfree(fw_data);
		fw_data_i += blk_tran_len;
	}

	kfree(fpga_dmabuff);
	fpga_dmabuff = kmalloc(PLF_FPGA_STATE_LEN, GFP_KERNEL);
	if (!fpga_dmabuff) {
		r = -ENOMEM;
		goto error_free_fw;
	}
	memset(fpga_dmabuff, 0xff, PLF_FPGA_STATE_LEN);

	send_vendor_request(udev, PLF_VNDR_FPGA_STATE_REQ, fpga_dmabuff,
			    PLF_FPGA_STATE_LEN);

	dev_dbg(&intf->dev, "%*ph\n", 8, fpga_dmabuff);

	if (fpga_dmabuff[0] != 0) {
		r = -EINVAL;
		goto error_free_fw;
	}

	send_vendor_command(udev, PLF_VNDR_FPGA_STATE_CMD, NULL, 0);

	msleep(PLF_MSLEEP_TIME);

error_free_fw:
	kfree(fpga_dmabuff);
	release_firmware(fw);
error:
	return r;
}

int plfxlc_download_xl_firmware(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	const struct firmware *fwp = NULL;
	struct plfxlc_firmware_file file = {0};
	const char *fw_pack;
	int s, r;
	u8 *buf;
	u32 i;

	r = send_vendor_command(udev, PLF_VNDR_XL_FW_CMD, NULL, 0);
	msleep(PLF_MSLEEP_TIME);

	if (r) {
		dev_err(&intf->dev, "vendor command failed (%d)\n", r);
		return -EINVAL;
	}
	/* Code for single pack file download */

	fw_pack = "plfxlc/lifi-xl.bin";

	r = request_firmware(&fwp, fw_pack, &intf->dev);
	if (r) {
		dev_err(&intf->dev, "Request_firmware failed (%d)\n", r);
		return -EINVAL;
	}
	file.total_files = get_unaligned_le32(&fwp->data[0]);
	file.total_size = get_unaligned_le32(&fwp->size);

	dev_dbg(&intf->dev, "XL Firmware (%d, %d)\n",
		file.total_files, file.total_size);

	buf = kzalloc(PLF_XL_BUF_LEN, GFP_KERNEL);
	if (!buf) {
		release_firmware(fwp);
		return -ENOMEM;
	}

	if (file.total_files > 10) {
		dev_err(&intf->dev, "Too many files (%d)\n", file.total_files);
		release_firmware(fwp);
		kfree(buf);
		return -EINVAL;
	}

	/* Download firmware files in multiple steps */
	for (s = 0; s < file.total_files; s++) {
		buf[0] = s;
		r = send_vendor_command(udev, PLF_VNDR_XL_FILE_CMD, buf,
					PLF_XL_BUF_LEN);

		if (s < file.total_files - 1)
			file.size = get_unaligned_le32(&fwp->data[4 + ((s + 1) * 4)])
				    - get_unaligned_le32(&fwp->data[4 + (s) * 4]);
		else
			file.size = file.total_size -
				    get_unaligned_le32(&fwp->data[4 + (s) * 4]);

		if (file.size > file.total_size || file.size > 60000) {
			dev_err(&intf->dev, "File size is too large (%d)\n", file.size);
			break;
		}

		file.start_addr = get_unaligned_le32(&fwp->data[4 + (s * 4)]);

		if (file.size % PLF_XL_BUF_LEN && s < 2)
			file.size += PLF_XL_BUF_LEN - file.size % PLF_XL_BUF_LEN;

		file.control_packets = file.size / PLF_XL_BUF_LEN;

		for (i = 0; i < file.control_packets; i++) {
			memcpy(buf,
			       &fwp->data[file.start_addr + (i * PLF_XL_BUF_LEN)],
			       PLF_XL_BUF_LEN);
			r = send_vendor_command(udev, PLF_VNDR_XL_DATA_CMD, buf,
						PLF_XL_BUF_LEN);
		}
		dev_dbg(&intf->dev, "fw-dw step=%d,r=%d size=%d\n", s, r,
			file.size);
	}
	release_firmware(fwp);
	kfree(buf);

	/* Code for single pack file download ends fw download finish */

	r = send_vendor_command(udev, PLF_VNDR_XL_EX_CMD, NULL, 0);
	dev_dbg(&intf->dev, "Download fpga (4) (%d)\n", r);

	return 0;
}

int plfxlc_upload_mac_and_serial(struct usb_interface *intf,
				 unsigned char *hw_address,
				 unsigned char *serial_number)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	unsigned long long firmware_version;
	unsigned char *dma_buffer = NULL;

	dma_buffer = kmalloc(PLF_SERIAL_LEN, GFP_KERNEL);
	if (!dma_buffer)
		return -ENOMEM;

	BUILD_BUG_ON(ETH_ALEN > PLF_SERIAL_LEN);
	BUILD_BUG_ON(PLF_FW_VER_LEN > PLF_SERIAL_LEN);

	send_vendor_request(udev, PLF_MAC_VENDOR_REQUEST, dma_buffer,
			    ETH_ALEN);

	memcpy(hw_address, dma_buffer, ETH_ALEN);

	send_vendor_request(udev, PLF_SERIAL_NUMBER_VENDOR_REQUEST,
			    dma_buffer, PLF_SERIAL_LEN);

	send_vendor_request(udev, PLF_SERIAL_NUMBER_VENDOR_REQUEST,
			    dma_buffer, PLF_SERIAL_LEN);

	memcpy(serial_number, dma_buffer, PLF_SERIAL_LEN);

	memset(dma_buffer, 0x00, PLF_SERIAL_LEN);

	send_vendor_request(udev, PLF_FIRMWARE_VERSION_VENDOR_REQUEST,
			    (unsigned char *)dma_buffer, PLF_FW_VER_LEN);

	memcpy(&firmware_version, dma_buffer, PLF_FW_VER_LEN);

	dev_info(&intf->dev, "Firmware Version: %llu\n", firmware_version);
	kfree(dma_buffer);

	dev_dbg(&intf->dev, "Mac: %pM\n", hw_address);

	return 0;
}

