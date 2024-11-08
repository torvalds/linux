// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD MP1 Smart Trace Buffer (STB) Layer
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *          Sanket Goswami <Sanket.Goswami@amd.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/amd_nb.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include "pmc.h"

/* STB Spill to DRAM Parameters */
#define S2D_TELEMETRY_DRAMBYTES_MAX	0x1000000
#define S2D_TELEMETRY_BYTES_MAX		0x100000U
#define S2D_RSVD_RAM_SPACE		0x100000

/* STB Registers */
#define AMD_STB_PMI_0			0x03E30600
#define AMD_PMC_STB_DUMMY_PC	0xC6000007

/* STB Spill to DRAM Message Definition */
#define STB_FORCE_FLUSH_DATA		0xCF
#define FIFO_SIZE		4096

/* STB S2D(Spill to DRAM) has different message port offset */
#define AMD_S2D_REGISTER_MESSAGE	0xA20
#define AMD_S2D_REGISTER_RESPONSE	0xA80
#define AMD_S2D_REGISTER_ARGUMENT	0xA88

/* STB S2D (Spill to DRAM) message port offset for 44h model */
#define AMD_GNR_REGISTER_MESSAGE	0x524
#define AMD_GNR_REGISTER_RESPONSE	0x570
#define AMD_GNR_REGISTER_ARGUMENT	0xA40

static bool enable_stb;
module_param(enable_stb, bool, 0644);
MODULE_PARM_DESC(enable_stb, "Enable the STB debug mechanism");

static bool dump_custom_stb;
module_param(dump_custom_stb, bool, 0644);
MODULE_PARM_DESC(dump_custom_stb, "Enable to dump full STB buffer");

enum s2d_arg {
	S2D_TELEMETRY_SIZE = 0x01,
	S2D_PHYS_ADDR_LOW,
	S2D_PHYS_ADDR_HIGH,
	S2D_NUM_SAMPLES,
	S2D_DRAM_SIZE,
};

struct amd_stb_v2_data {
	size_t size;
	u8 data[] __counted_by(size);
};

int amd_stb_write(struct amd_pmc_dev *dev, u32 data)
{
	int err;

	err = amd_smn_write(0, AMD_STB_PMI_0, data);
	if (err) {
		dev_err(dev->dev, "failed to write data in stb: 0x%X\n", AMD_STB_PMI_0);
		return pcibios_err_to_errno(err);
	}

	return 0;
}

int amd_stb_read(struct amd_pmc_dev *dev, u32 *buf)
{
	int i, err;

	for (i = 0; i < FIFO_SIZE; i++) {
		err = amd_smn_read(0, AMD_STB_PMI_0, buf++);
		if (err) {
			dev_err(dev->dev, "error reading data from stb: 0x%X\n", AMD_STB_PMI_0);
			return pcibios_err_to_errno(err);
		}
	}

	return 0;
}

static int amd_stb_debugfs_open(struct inode *inode, struct file *filp)
{
	struct amd_pmc_dev *dev = filp->f_inode->i_private;
	u32 size = FIFO_SIZE * sizeof(u32);
	u32 *buf;
	int rc;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rc = amd_stb_read(dev, buf);
	if (rc) {
		kfree(buf);
		return rc;
	}

	filp->private_data = buf;
	return rc;
}

static ssize_t amd_stb_debugfs_read(struct file *filp, char __user *buf, size_t size, loff_t *pos)
{
	if (!filp->private_data)
		return -EINVAL;

	return simple_read_from_buffer(buf, size, pos, filp->private_data,
				       FIFO_SIZE * sizeof(u32));
}

static int amd_stb_debugfs_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);
	return 0;
}

static const struct file_operations amd_stb_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = amd_stb_debugfs_open,
	.read = amd_stb_debugfs_read,
	.release = amd_stb_debugfs_release,
};

/* Enhanced STB Firmware Reporting Mechanism */
static int amd_stb_handle_efr(struct file *filp)
{
	struct amd_pmc_dev *dev = filp->f_inode->i_private;
	struct amd_stb_v2_data *stb_data_arr;
	u32 fsize;

	fsize = dev->dram_size - S2D_RSVD_RAM_SPACE;
	stb_data_arr = kmalloc(struct_size(stb_data_arr, data, fsize), GFP_KERNEL);
	if (!stb_data_arr)
		return -ENOMEM;

	stb_data_arr->size = fsize;
	memcpy_fromio(stb_data_arr->data, dev->stb_virt_addr, fsize);
	filp->private_data = stb_data_arr;

	return 0;
}

static int amd_stb_debugfs_open_v2(struct inode *inode, struct file *filp)
{
	struct amd_pmc_dev *dev = filp->f_inode->i_private;
	u32 fsize, num_samples, val, stb_rdptr_offset = 0;
	struct amd_stb_v2_data *stb_data_arr;
	int ret;

	/* Write dummy postcode while reading the STB buffer */
	ret = amd_stb_write(dev, AMD_PMC_STB_DUMMY_PC);
	if (ret)
		dev_err(dev->dev, "error writing to STB: %d\n", ret);

	/* Spill to DRAM num_samples uses separate SMU message port */
	dev->msg_port = MSG_PORT_S2D;

	ret = amd_pmc_send_cmd(dev, 0, &val, STB_FORCE_FLUSH_DATA, 1);
	if (ret)
		dev_dbg_once(dev->dev, "S2D force flush not supported: %d\n", ret);

	/*
	 * We have a custom stb size and the PMFW is supposed to give
	 * the enhanced dram size. Note that we land here only for the
	 * platforms that support enhanced dram size reporting.
	 */
	if (dump_custom_stb)
		return amd_stb_handle_efr(filp);

	/* Get the num_samples to calculate the last push location */
	ret = amd_pmc_send_cmd(dev, S2D_NUM_SAMPLES, &num_samples, dev->stb_arg.s2d_msg_id, true);
	/* Clear msg_port for other SMU operation */
	dev->msg_port = MSG_PORT_PMC;
	if (ret) {
		dev_err(dev->dev, "error: S2D_NUM_SAMPLES not supported : %d\n", ret);
		return ret;
	}

	fsize = min(num_samples, S2D_TELEMETRY_BYTES_MAX);
	stb_data_arr = kmalloc(struct_size(stb_data_arr, data, fsize), GFP_KERNEL);
	if (!stb_data_arr)
		return -ENOMEM;

	stb_data_arr->size = fsize;

	/*
	 * Start capturing data from the last push location.
	 * This is for general cases, where the stb limits
	 * are meant for standard usage.
	 */
	if (num_samples > S2D_TELEMETRY_BYTES_MAX) {
		/* First read oldest data starting 1 behind last write till end of ringbuffer */
		stb_rdptr_offset = num_samples % S2D_TELEMETRY_BYTES_MAX;
		fsize = S2D_TELEMETRY_BYTES_MAX - stb_rdptr_offset;

		memcpy_fromio(stb_data_arr->data, dev->stb_virt_addr + stb_rdptr_offset, fsize);
		/* Second copy the newer samples from offset 0 - last write */
		memcpy_fromio(stb_data_arr->data + fsize, dev->stb_virt_addr, stb_rdptr_offset);
	} else {
		memcpy_fromio(stb_data_arr->data, dev->stb_virt_addr, fsize);
	}

	filp->private_data = stb_data_arr;

	return 0;
}

static ssize_t amd_stb_debugfs_read_v2(struct file *filp, char __user *buf, size_t size,
				       loff_t *pos)
{
	struct amd_stb_v2_data *data = filp->private_data;

	return simple_read_from_buffer(buf, size, pos, data->data, data->size);
}

static int amd_stb_debugfs_release_v2(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);
	return 0;
}

static const struct file_operations amd_stb_debugfs_fops_v2 = {
	.owner = THIS_MODULE,
	.open = amd_stb_debugfs_open_v2,
	.read = amd_stb_debugfs_read_v2,
	.release = amd_stb_debugfs_release_v2,
};

static void amd_stb_update_args(struct amd_pmc_dev *dev)
{
	if (cpu_feature_enabled(X86_FEATURE_ZEN5))
		switch (boot_cpu_data.x86_model) {
		case 0x44:
			dev->stb_arg.msg = AMD_GNR_REGISTER_MESSAGE;
			dev->stb_arg.arg = AMD_GNR_REGISTER_ARGUMENT;
			dev->stb_arg.resp = AMD_GNR_REGISTER_RESPONSE;
			return;
		default:
			break;
	}

	dev->stb_arg.msg = AMD_S2D_REGISTER_MESSAGE;
	dev->stb_arg.arg = AMD_S2D_REGISTER_ARGUMENT;
	dev->stb_arg.resp = AMD_S2D_REGISTER_RESPONSE;
}

static bool amd_is_stb_supported(struct amd_pmc_dev *dev)
{
	switch (dev->cpu_id) {
	case AMD_CPU_ID_YC:
	case AMD_CPU_ID_CB:
		if (boot_cpu_data.x86_model == 0x44)
			dev->stb_arg.s2d_msg_id = 0x9B;
		else
			dev->stb_arg.s2d_msg_id = 0xBE;
		break;
	case AMD_CPU_ID_PS:
		dev->stb_arg.s2d_msg_id = 0x85;
		break;
	case PCI_DEVICE_ID_AMD_1AH_M20H_ROOT:
	case PCI_DEVICE_ID_AMD_1AH_M60H_ROOT:
		if (boot_cpu_data.x86_model == 0x70)
			dev->stb_arg.s2d_msg_id = 0xF1;
		else
			dev->stb_arg.s2d_msg_id = 0xDE;
		break;
	default:
		return false;
	}

	amd_stb_update_args(dev);
	return true;
}

int amd_stb_s2d_init(struct amd_pmc_dev *dev)
{
	u32 phys_addr_low, phys_addr_hi;
	u64 stb_phys_addr;
	u32 size = 0;
	int ret;

	if (!enable_stb)
		return 0;

	if (amd_is_stb_supported(dev)) {
		debugfs_create_file("stb_read", 0644, dev->dbgfs_dir, dev,
				    &amd_stb_debugfs_fops_v2);
	} else {
		debugfs_create_file("stb_read", 0644, dev->dbgfs_dir, dev,
				    &amd_stb_debugfs_fops);
		return 0;
	}

	/* Spill to DRAM feature uses separate SMU message port */
	dev->msg_port = MSG_PORT_S2D;

	amd_pmc_send_cmd(dev, S2D_TELEMETRY_SIZE, &size, dev->stb_arg.s2d_msg_id, true);
	if (size != S2D_TELEMETRY_BYTES_MAX)
		return -EIO;

	/* Get DRAM size */
	ret = amd_pmc_send_cmd(dev, S2D_DRAM_SIZE, &dev->dram_size, dev->stb_arg.s2d_msg_id, true);
	if (ret || !dev->dram_size)
		dev->dram_size = S2D_TELEMETRY_DRAMBYTES_MAX;

	/* Get STB DRAM address */
	amd_pmc_send_cmd(dev, S2D_PHYS_ADDR_LOW, &phys_addr_low, dev->stb_arg.s2d_msg_id, true);
	amd_pmc_send_cmd(dev, S2D_PHYS_ADDR_HIGH, &phys_addr_hi, dev->stb_arg.s2d_msg_id, true);

	stb_phys_addr = ((u64)phys_addr_hi << 32 | phys_addr_low);

	/* Clear msg_port for other SMU operation */
	dev->msg_port = MSG_PORT_PMC;

	dev->stb_virt_addr = devm_ioremap(dev->dev, stb_phys_addr, dev->dram_size);
	if (!dev->stb_virt_addr)
		return -ENOMEM;

	return 0;
}
