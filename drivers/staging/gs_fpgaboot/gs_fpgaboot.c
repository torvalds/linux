/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/firmware.h>

#include "gs_fpgaboot.h"
#include "io.h"

#define DEVICE_NAME "device"
#define CLASS_NAME  "fpgaboot"

static u8 bits_magic[] = {
	0x0, 0x9, 0xf, 0xf0, 0xf, 0xf0,
	0xf, 0xf0, 0xf, 0xf0, 0x0, 0x0, 0x1};

/* fake device for request_firmware */
static struct platform_device	*firmware_pdev;

static char	*file = "xlinx_fpga_firmware.bit";
module_param(file, charp, S_IRUGO);
MODULE_PARM_DESC(file, "Xilinx FPGA firmware file.");

static void read_bitstream(char *bitdata, char *buf, int *offset, int rdsize)
{
	memcpy(buf, bitdata + *offset, rdsize);
	*offset += rdsize;
}

static void readinfo_bitstream(char *bitdata, char *buf, int *offset)
{
	char tbuf[64];
	s32 len;

	/* read section char */
	read_bitstream(bitdata, tbuf, offset, 1);

	/* read length */
	read_bitstream(bitdata, tbuf, offset, 2);

	len = tbuf[0] << 8 | tbuf[1];

	read_bitstream(bitdata, buf, offset, len);
	buf[len] = '\0';
}

/*
 * read bitdata length
 */
static int readlength_bitstream(char *bitdata, int *lendata, int *offset)
{
	char tbuf[64];

	/* read section char */
	read_bitstream(bitdata, tbuf, offset, 1);

	/* make sure it is section 'e' */
	if (tbuf[0] != 'e') {
		pr_err("error: length section is not 'e', but %c\n", tbuf[0]);
		return -1;
	}

	/* read 4bytes length */
	read_bitstream(bitdata, tbuf, offset, 4);

	*lendata = tbuf[0] << 24 | tbuf[1] << 16 |
		tbuf[2] << 8 | tbuf[3];

	return 0;
}

/*
 * read first 13 bytes to check bitstream magic number
 */
static int readmagic_bitstream(char *bitdata, int *offset)
{
	char buf[13];
	int r;

	read_bitstream(bitdata, buf, offset, 13);
	r = memcmp(buf, bits_magic, 13);
	if (r) {
		pr_err("error: corrupted header");
		return -1;
	}
	pr_info("bitstream file magic number Ok\n");

	*offset = 13;	/* magic length */

	return 0;
}

/*
 * NOTE: supports only bitstream format
 */
static enum fmt_image get_imageformat(struct fpgaimage *fimage)
{
	return f_bit;
}

static void gs_print_header(struct fpgaimage *fimage)
{
	pr_info("file: %s\n", fimage->filename);
	pr_info("part: %s\n", fimage->part);
	pr_info("date: %s\n", fimage->date);
	pr_info("time: %s\n", fimage->time);
	pr_info("lendata: %d\n", fimage->lendata);
}

static void gs_read_bitstream(struct fpgaimage *fimage)
{
	char *bitdata;
	int offset;

	offset = 0;
	bitdata = (char *)fimage->fw_entry->data;

	readmagic_bitstream(bitdata, &offset);
	readinfo_bitstream(bitdata, fimage->filename, &offset);
	readinfo_bitstream(bitdata, fimage->part, &offset);
	readinfo_bitstream(bitdata, fimage->date, &offset);
	readinfo_bitstream(bitdata, fimage->time, &offset);
	readlength_bitstream(bitdata, &fimage->lendata, &offset);

	fimage->fpgadata = bitdata + offset;
}

static int gs_read_image(struct fpgaimage *fimage)
{
	int img_fmt;

	img_fmt = get_imageformat(fimage);

	switch (img_fmt) {
	case f_bit:
		pr_info("image is bitstream format\n");
		gs_read_bitstream(fimage);
		break;
	default:
		pr_err("unsupported fpga image format\n");
		return -1;
	}

	gs_print_header(fimage);

	return 0;
}

static int gs_load_image(struct fpgaimage *fimage, char *fw_file)
{
	int err;

	pr_info("load fpgaimage %s\n", fw_file);

	err = request_firmware(&fimage->fw_entry, fw_file, &firmware_pdev->dev);
	if (err != 0) {
		pr_err("firmware %s is missing, cannot continue.\n", fw_file);
		return err;
	}

	return 0;
}

static int gs_download_image(struct fpgaimage *fimage, enum wbus bus_bytes)
{
	char *bitdata;
	int size, i, cnt;

	cnt = 0;
	bitdata = (char *)fimage->fpgadata;
	size = fimage->lendata;

#ifdef DEBUG_FPGA
	print_hex_dump_bytes("bitfile sample: ", DUMP_PREFIX_OFFSET,
			     bitdata, 0x100);
#endif /* DEBUG_FPGA */
	if (!xl_supported_prog_bus_width(bus_bytes)) {
		pr_err("unsupported program bus width %d\n",
		       bus_bytes);
		return -1;
	}

	/* Bring csi_b, rdwr_b Low and program_b High */
	xl_program_b(1);
	xl_rdwr_b(0);
	xl_csi_b(0);

	/* Configuration reset */
	xl_program_b(0);
	msleep(20);
	xl_program_b(1);

	/* Wait for Device Initialization */
	while (xl_get_init_b() == 0)
		;

	pr_info("device init done\n");

	for (i = 0; i < size; i += bus_bytes)
		xl_shift_bytes_out(bus_bytes, bitdata + i);

	pr_info("program done\n");

	/* Check INIT_B */
	if (xl_get_init_b() == 0) {
		pr_err("init_b 0\n");
		return -1;
	}

	while (xl_get_done_b() == 0) {
		if (cnt++ > MAX_WAIT_DONE) {
			pr_err("init_B %d\n", xl_get_init_b());
			break;
		}
	}

	if (cnt > MAX_WAIT_DONE) {
		pr_err("fpga download fail\n");
		return -1;
	}

	pr_info("download fpgaimage\n");

	/* Compensate for Special Startup Conditions */
	xl_shift_cclk(8);

	return 0;
}

static int gs_release_image(struct fpgaimage *fimage)
{
	release_firmware(fimage->fw_entry);
	pr_info("release fpgaimage\n");

	return 0;
}

/*
 * NOTE: supports systemmap parallel programming
 */
static int gs_set_download_method(struct fpgaimage *fimage)
{
	pr_info("set program method\n");

	fimage->dmethod = m_systemmap;

	pr_info("systemmap program method\n");

	return 0;
}

static int init_driver(void)
{
	firmware_pdev = platform_device_register_simple("fpgaboot", -1,
							NULL, 0);
	return PTR_ERR_OR_ZERO(firmware_pdev);
}

static int gs_fpgaboot(void)
{
	int err;
	struct fpgaimage	*fimage;

	fimage = kmalloc(sizeof(*fimage), GFP_KERNEL);
	if (!fimage)
		return -ENOMEM;

	err = gs_load_image(fimage, file);
	if (err) {
		pr_err("gs_load_image error\n");
		goto err_out1;
	}

	err = gs_read_image(fimage);
	if (err) {
		pr_err("gs_read_image error\n");
		goto err_out2;
	}

	err = gs_set_download_method(fimage);
	if (err) {
		pr_err("gs_set_download_method error\n");
		goto err_out2;
	}

	err = gs_download_image(fimage, bus_2byte);
	if (err) {
		pr_err("gs_download_image error\n");
		goto err_out2;
	}

	err = gs_release_image(fimage);
	if (err) {
		pr_err("gs_release_image error\n");
		goto err_out1;
	}

	kfree(fimage);
	return 0;

err_out2:
	err = gs_release_image(fimage);
	if (err)
		pr_err("gs_release_image error\n");
err_out1:
	kfree(fimage);

	return -1;
}

static int __init gs_fpgaboot_init(void)
{
	int err;

	pr_info("FPGA DOWNLOAD --->\n");

	pr_info("FPGA image file name: %s\n", file);

	err = init_driver();
	if (err) {
		pr_err("FPGA DRIVER INIT FAIL!!\n");
		return err;
	}

	err = xl_init_io();
	if (err) {
		pr_err("GPIO INIT FAIL!!\n");
		goto errout;
	}

	err = gs_fpgaboot();
	if (err) {
		pr_err("FPGA DOWNLOAD FAIL!!\n");
		goto errout;
	}

	pr_info("FPGA DOWNLOAD DONE <---\n");

	return 0;

errout:
	platform_device_unregister(firmware_pdev);

	return err;
}

static void __exit gs_fpgaboot_exit(void)
{
	platform_device_unregister(firmware_pdev);
	pr_info("FPGA image download module removed\n");
}

module_init(gs_fpgaboot_init);
module_exit(gs_fpgaboot_exit);

MODULE_AUTHOR("Insop Song");
MODULE_DESCRIPTION("Xlinix FPGA firmware download");
MODULE_LICENSE("GPL");
