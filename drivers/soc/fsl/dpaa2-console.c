// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Freescale DPAA2 Platforms Console Driver
 *
 * Copyright 2015-2016 Freescale Semiconductor Inc.
 * Copyright 2018 NXP
 */

#define pr_fmt(fmt) "dpaa2-console: " fmt

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/io.h>

/* MC firmware base low/high registers indexes */
#define MCFBALR_OFFSET 0
#define MCFBAHR_OFFSET 1

/* Bit masks used to get the most/least significant part of the MC base addr */
#define MC_FW_ADDR_MASK_HIGH 0x1FFFF
#define MC_FW_ADDR_MASK_LOW  0xE0000000

#define MC_BUFFER_OFFSET 0x01000000
#define MC_BUFFER_SIZE   (1024 * 1024 * 16)
#define MC_OFFSET_DELTA  MC_BUFFER_OFFSET

#define AIOP_BUFFER_OFFSET 0x06000000
#define AIOP_BUFFER_SIZE   (1024 * 1024 * 16)
#define AIOP_OFFSET_DELTA  0

#define LOG_HEADER_FLAG_BUFFER_WRAPAROUND 0x80000000
#define LAST_BYTE(a) ((a) & ~(LOG_HEADER_FLAG_BUFFER_WRAPAROUND))

/* MC and AIOP Magic words */
#define MAGIC_MC   0x4d430100
#define MAGIC_AIOP 0x41494F50

struct log_header {
	__le32 magic_word;
	char reserved[4];
	__le32 buf_start;
	__le32 buf_length;
	__le32 last_byte;
};

struct console_data {
	void __iomem *map_addr;
	struct log_header __iomem *hdr;
	void __iomem *start_addr;
	void __iomem *end_addr;
	void __iomem *end_of_data;
	void __iomem *cur_ptr;
};

static struct resource mc_base_addr;

static inline void adjust_end(struct console_data *cd)
{
	u32 last_byte = readl(&cd->hdr->last_byte);

	cd->end_of_data = cd->start_addr + LAST_BYTE(last_byte);
}

static u64 get_mc_fw_base_address(void)
{
	u64 mcfwbase = 0ULL;
	u32 __iomem *mcfbaregs;

	mcfbaregs = ioremap(mc_base_addr.start, resource_size(&mc_base_addr));
	if (!mcfbaregs) {
		pr_err("could not map MC Firmware Base registers\n");
		return 0;
	}

	mcfwbase  = readl(mcfbaregs + MCFBAHR_OFFSET) &
			  MC_FW_ADDR_MASK_HIGH;
	mcfwbase <<= 32;
	mcfwbase |= readl(mcfbaregs + MCFBALR_OFFSET) & MC_FW_ADDR_MASK_LOW;
	iounmap(mcfbaregs);

	pr_debug("MC base address at 0x%016llx\n", mcfwbase);
	return mcfwbase;
}

static ssize_t dpaa2_console_size(struct console_data *cd)
{
	ssize_t size;

	if (cd->cur_ptr <= cd->end_of_data)
		size = cd->end_of_data - cd->cur_ptr;
	else
		size = (cd->end_addr - cd->cur_ptr) +
			(cd->end_of_data - cd->start_addr);

	return size;
}

static int dpaa2_generic_console_open(struct inode *node, struct file *fp,
				      u64 offset, u64 size,
				      u32 expected_magic,
				      u32 offset_delta)
{
	u32 read_magic, wrapped, last_byte, buf_start, buf_length;
	struct console_data *cd;
	u64 base_addr;
	int err;

	cd = kmalloc(sizeof(*cd), GFP_KERNEL);
	if (!cd)
		return -ENOMEM;

	base_addr = get_mc_fw_base_address();
	if (!base_addr) {
		err = -EIO;
		goto err_fwba;
	}

	cd->map_addr = ioremap(base_addr + offset, size);
	if (!cd->map_addr) {
		pr_err("cannot map console log memory\n");
		err = -EIO;
		goto err_ioremap;
	}

	cd->hdr = (struct log_header __iomem *)cd->map_addr;
	read_magic = readl(&cd->hdr->magic_word);
	last_byte =  readl(&cd->hdr->last_byte);
	buf_start =  readl(&cd->hdr->buf_start);
	buf_length = readl(&cd->hdr->buf_length);

	if (read_magic != expected_magic) {
		pr_warn("expected = %08x, read = %08x\n",
			expected_magic, read_magic);
		err = -EIO;
		goto err_magic;
	}

	cd->start_addr = cd->map_addr + buf_start - offset_delta;
	cd->end_addr = cd->start_addr + buf_length;

	wrapped = last_byte & LOG_HEADER_FLAG_BUFFER_WRAPAROUND;

	adjust_end(cd);
	if (wrapped && cd->end_of_data != cd->end_addr)
		cd->cur_ptr = cd->end_of_data + 1;
	else
		cd->cur_ptr = cd->start_addr;

	fp->private_data = cd;

	return 0;

err_magic:
	iounmap(cd->map_addr);

err_ioremap:
err_fwba:
	kfree(cd);

	return err;
}

static int dpaa2_mc_console_open(struct inode *node, struct file *fp)
{
	return dpaa2_generic_console_open(node, fp,
					  MC_BUFFER_OFFSET, MC_BUFFER_SIZE,
					  MAGIC_MC, MC_OFFSET_DELTA);
}

static int dpaa2_aiop_console_open(struct inode *node, struct file *fp)
{
	return dpaa2_generic_console_open(node, fp,
					  AIOP_BUFFER_OFFSET, AIOP_BUFFER_SIZE,
					  MAGIC_AIOP, AIOP_OFFSET_DELTA);
}

static int dpaa2_console_close(struct inode *node, struct file *fp)
{
	struct console_data *cd = fp->private_data;

	iounmap(cd->map_addr);
	kfree(cd);
	return 0;
}

static ssize_t dpaa2_console_read(struct file *fp, char __user *buf,
				  size_t count, loff_t *f_pos)
{
	struct console_data *cd = fp->private_data;
	size_t bytes = dpaa2_console_size(cd);
	size_t bytes_end = cd->end_addr - cd->cur_ptr;
	size_t written = 0;
	void *kbuf;
	int err;

	/* Check if we need to adjust the end of data addr */
	adjust_end(cd);

	if (cd->end_of_data == cd->cur_ptr)
		return 0;

	if (count < bytes)
		bytes = count;

	kbuf = kmalloc(bytes, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (bytes > bytes_end) {
		memcpy_fromio(kbuf, cd->cur_ptr, bytes_end);
		if (copy_to_user(buf, kbuf, bytes_end)) {
			err = -EFAULT;
			goto err_free_buf;
		}
		buf += bytes_end;
		cd->cur_ptr = cd->start_addr;
		bytes -= bytes_end;
		written += bytes_end;
	}

	memcpy_fromio(kbuf, cd->cur_ptr, bytes);
	if (copy_to_user(buf, kbuf, bytes)) {
		err = -EFAULT;
		goto err_free_buf;
	}
	cd->cur_ptr += bytes;
	written += bytes;

	kfree(kbuf);
	return written;

err_free_buf:
	kfree(kbuf);

	return err;
}

static const struct file_operations dpaa2_mc_console_fops = {
	.owner          = THIS_MODULE,
	.open           = dpaa2_mc_console_open,
	.release        = dpaa2_console_close,
	.read           = dpaa2_console_read,
};

static struct miscdevice dpaa2_mc_console_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "dpaa2_mc_console",
	.fops = &dpaa2_mc_console_fops
};

static const struct file_operations dpaa2_aiop_console_fops = {
	.owner          = THIS_MODULE,
	.open           = dpaa2_aiop_console_open,
	.release        = dpaa2_console_close,
	.read           = dpaa2_console_read,
};

static struct miscdevice dpaa2_aiop_console_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "dpaa2_aiop_console",
	.fops = &dpaa2_aiop_console_fops
};

static int dpaa2_console_probe(struct platform_device *pdev)
{
	int error;

	error = of_address_to_resource(pdev->dev.of_node, 0, &mc_base_addr);
	if (error < 0) {
		pr_err("of_address_to_resource() failed for %pOF with %d\n",
		       pdev->dev.of_node, error);
		return error;
	}

	error = misc_register(&dpaa2_mc_console_dev);
	if (error) {
		pr_err("cannot register device %s\n",
		       dpaa2_mc_console_dev.name);
		goto err_register_mc;
	}

	error = misc_register(&dpaa2_aiop_console_dev);
	if (error) {
		pr_err("cannot register device %s\n",
		       dpaa2_aiop_console_dev.name);
		goto err_register_aiop;
	}

	return 0;

err_register_aiop:
	misc_deregister(&dpaa2_mc_console_dev);
err_register_mc:
	return error;
}

static int dpaa2_console_remove(struct platform_device *pdev)
{
	misc_deregister(&dpaa2_mc_console_dev);
	misc_deregister(&dpaa2_aiop_console_dev);

	return 0;
}

static const struct of_device_id dpaa2_console_match_table[] = {
	{ .compatible = "fsl,dpaa2-console",},
	{},
};

MODULE_DEVICE_TABLE(of, dpaa2_console_match_table);

static struct platform_driver dpaa2_console_driver = {
	.driver = {
		   .name = "dpaa2-console",
		   .pm = NULL,
		   .of_match_table = dpaa2_console_match_table,
		   },
	.probe = dpaa2_console_probe,
	.remove = dpaa2_console_remove,
};
module_platform_driver(dpaa2_console_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Roy Pledge <roy.pledge@nxp.com>");
MODULE_DESCRIPTION("DPAA2 console driver");
