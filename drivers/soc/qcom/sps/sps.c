// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/* Smart-Peripheral-Switch (SPS) Module. */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/memory.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "sps_bam.h"
#include "spsi.h"
#include "sps_core.h"

#define SPS_DRV_NAME "msm_sps"	/* must match the platform_device name */

/**
 *  SPS driver state
 */
struct sps_drv *sps;

u32 d_type;
bool enhd_pipe;
bool imem;
enum sps_bam_type bam_type;
static enum sps_bam_type bam_types[] = {
	SPS_BAM_LEGACY, SPS_BAM_NDP, SPS_BAM_NDP_4K};

static void sps_device_de_init(void);

#ifdef CONFIG_DEBUG_FS
u8 debugfs_record_enabled;
u8 logging_option;
u8 debug_level_option;
u8 print_limit_option;
static u8 reg_dump_option;
static u32 testbus_sel;
static u32 bam_pipe_sel;
static u32 desc_option;
/*
 * Specifies range of log level from level 0 to level 3 to have fine-granularity
 * for logging to serve all BAM use cases.
 */
static u32 log_level_sel;

static char *debugfs_buf;
static u32 debugfs_buf_size;
static u32 debugfs_buf_used;
static int wraparound;
static struct mutex sps_debugfs_lock;

static struct dentry *dent;
static struct dentry *dfile_info;
static struct dentry *dfile_logging_option;
static struct dentry *dfile_bam_addr;

static struct sps_bam *phy2bam(phys_addr_t phys_addr);

/* record debug info for debugfs */
void sps_debugfs_record(const char *msg)
{
	mutex_lock(&sps_debugfs_lock);
	if (debugfs_record_enabled) {
		if (debugfs_buf_used + MAX_MSG_LEN >= debugfs_buf_size) {
			debugfs_buf_used = 0;
			wraparound = true;
		}
		debugfs_buf_used += scnprintf(debugfs_buf + debugfs_buf_used,
				debugfs_buf_size - debugfs_buf_used, msg);

		if (wraparound)
			scnprintf(debugfs_buf + debugfs_buf_used,
					debugfs_buf_size - debugfs_buf_used,
					"\n**** end line of sps log ****\n\n");
	}
	mutex_unlock(&sps_debugfs_lock);
}

/* read the recorded debug info to userspace */
static ssize_t sps_read_info(struct file *file, char __user *ubuf,
		size_t count, loff_t *ppos)
{
	int ret = 0;
	int size;

	mutex_lock(&sps_debugfs_lock);
	if (debugfs_record_enabled) {
		if (wraparound)
			size = debugfs_buf_size - MAX_MSG_LEN;
		else
			size = debugfs_buf_used;

		ret = simple_read_from_buffer(ubuf, count, ppos,
				debugfs_buf, size);
	}
	mutex_unlock(&sps_debugfs_lock);

	return ret;
}

/*
 * set the buffer size (in KB) for debug info
 */
static ssize_t sps_set_info(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	unsigned long missing;
	char str[MAX_MSG_LEN];
	int i;
	u32 buf_size_kb = 0;
	u32 new_buf_size;
	u32 size = sizeof(str) < count ? sizeof(str) : count;

	memset(str, 0, sizeof(str));
	missing = copy_from_user(str, buf, size);
	if (missing)
		return -EFAULT;

	for (i = 0; i < sizeof(str) && (str[i] >= '0') && (str[i] <= '9'); ++i)
		buf_size_kb = (buf_size_kb * 10) + (str[i] - '0');

	pr_info("sps:debugfs: input buffer size is %dKB\n", buf_size_kb);

	if ((logging_option == 0) || (logging_option == 2)) {
		pr_info("sps:debugfs: need to first turn on recording\n");
		return -EFAULT;
	}

	if (buf_size_kb < 1) {
		pr_info("sps:debugfs:buffer size should be no less than 1KB\n");
		return -EFAULT;
	}

	if (buf_size_kb > (INT_MAX/SZ_1K)) {
		pr_err("sps:debugfs: buffer size is too large\n");
		return -EFAULT;
	}

	new_buf_size = buf_size_kb * SZ_1K;

	mutex_lock(&sps_debugfs_lock);
	if (debugfs_record_enabled) {
		if (debugfs_buf_size == new_buf_size) {
			/* need do nothing */
			pr_info(
				"sps:debugfs: input buffer size is the same as before\n"
				);
			mutex_unlock(&sps_debugfs_lock);
			return count;
		}
		/* release the current buffer */
		debugfs_record_enabled = false;
		debugfs_buf_used = 0;
		wraparound = false;
		kfree(debugfs_buf);
		debugfs_buf = NULL;
	}

	/* allocate new buffer */
	debugfs_buf_size = new_buf_size;

	debugfs_buf = kzalloc(debugfs_buf_size,	GFP_KERNEL);
	if (!debugfs_buf) {
		debugfs_buf_size = 0;
		mutex_unlock(&sps_debugfs_lock);
		return -ENOMEM;
	}

	debugfs_buf_used = 0;
	wraparound = false;
	debugfs_record_enabled = true;
	mutex_unlock(&sps_debugfs_lock);

	return count;
}

static const struct file_operations sps_info_ops = {
	.read = sps_read_info,
	.write = sps_set_info,
};

/* return the current logging option to userspace */
static ssize_t sps_read_logging_option(struct file *file, char __user *ubuf,
		size_t count, loff_t *ppos)
{
	char value[MAX_MSG_LEN];
	int nbytes;

	nbytes = scnprintf(value, MAX_MSG_LEN, "%d\n", logging_option);

	return simple_read_from_buffer(ubuf, count, ppos, value, nbytes);
}

/*
 * set the logging option
 */
static ssize_t sps_set_logging_option(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	int ret;
	u32 option = 0;

	ret = kstrtouint_from_user(buf, count, 10, &option);
	if (ret)
		return ret;

	pr_info("sps:debugfs: try to change logging option to %d\n", option);

	if (option > 3) {
		pr_err("sps:debugfs: invalid logging option:%d\n", option);
		return count;
	}

	mutex_lock(&sps_debugfs_lock);
	if (((option == 0) || (option == 2)) &&
		((logging_option == 1) || (logging_option == 3))) {
		debugfs_record_enabled = false;
		kfree(debugfs_buf);
		debugfs_buf = NULL;
		debugfs_buf_used = 0;
		debugfs_buf_size = 0;
		wraparound = false;
	}

	logging_option = option;
	mutex_unlock(&sps_debugfs_lock);

	return count;
}

static const struct file_operations sps_logging_option_ops = {
	.read = sps_read_logging_option,
	.write = sps_set_logging_option,
};

/*
 * input the bam physical address
 */
static ssize_t sps_set_bam_addr(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	u32 ret;
	int i;
	u32 bam_addr = 0;
	struct sps_bam *bam;
	u32 num_pipes = 0;
	void *vir_addr;

	ret = kstrtouint_from_user(buf, count, 10, &bam_addr);
	if (ret)
		return ret;

	pr_info("sps:debugfs:input BAM physical address:0x%x\n", bam_addr);

	bam = phy2bam(bam_addr);

	if (bam == NULL) {
		pr_err("sps:debugfs:BAM 0x%x is not registered\n", bam_addr);
		return count;
	}
	vir_addr = &bam->base;
	num_pipes = bam->props.num_pipes;
	if (log_level_sel <= SPS_IPC_MAX_LOGLEVEL)
		bam->ipc_loglevel = log_level_sel;

	switch (reg_dump_option) {
	case 1: /* output all registers of this BAM */
		print_bam_reg(bam->base);
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_reg(bam->base, i);
		break;
	case 2: /* output BAM-level registers */
		print_bam_reg(bam->base);
		break;
	case 3: /* output selected BAM-level registers */
		print_bam_selected_reg(vir_addr, bam->props.ee);
		break;
	case 4: /* output selected registers of all pipes */
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_selected_reg(vir_addr, i);
		break;
	case 5: /* output selected registers of selected pipes */
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_selected_reg(vir_addr, i);
		break;
	case 6: /* output selected registers of typical pipes */
		print_bam_pipe_selected_reg(vir_addr, 4);
		print_bam_pipe_selected_reg(vir_addr, 5);
		break;
	case 7: /* output desc FIFO of all pipes */
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_desc_fifo(vir_addr, i, 0);
		break;
	case 8: /* output desc FIFO of selected pipes */
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i, 0);
		break;
	case 9: /* output desc FIFO of typical pipes */
		print_bam_pipe_desc_fifo(vir_addr, 4, 0);
		print_bam_pipe_desc_fifo(vir_addr, 5, 0);
		break;
	case 10: /* output selected registers and desc FIFO of all pipes */
		for (i = 0; i < num_pipes; i++) {
			print_bam_pipe_selected_reg(vir_addr, i);
			print_bam_pipe_desc_fifo(vir_addr, i, 0);
		}
		break;
	case 11: /* output selected registers and desc FIFO of selected pipes */
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i)) {
				print_bam_pipe_selected_reg(vir_addr, i);
				print_bam_pipe_desc_fifo(vir_addr, i, 0);
			}
		break;
	case 12: /* output selected registers and desc FIFO of typical pipes */
		print_bam_pipe_selected_reg(vir_addr, 4);
		print_bam_pipe_desc_fifo(vir_addr, 4, 0);
		print_bam_pipe_selected_reg(vir_addr, 5);
		print_bam_pipe_desc_fifo(vir_addr, 5, 0);
		break;
	case 13: /* output BAM_TEST_BUS_REG */
		if (testbus_sel)
			print_bam_test_bus_reg(vir_addr, testbus_sel);
		else {
			pr_info("sps:output TEST_BUS_REG for all TEST_BUS_SEL\n");
			print_bam_test_bus_reg(vir_addr, testbus_sel);
		}
		break;
	case 14: /* output partial desc FIFO of selected pipes */
		if (desc_option == 0)
			desc_option = 1;
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i,
							desc_option);
		break;
	case 15: /* output partial data blocks of descriptors */
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i, 100);
		break;
	case 16: /* output all registers of selected pipes */
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_reg(bam->base, i);
		break;
	case 91: /*
		  * output testbus register, BAM global regisers
		  * and registers of all pipes
		  */
		print_bam_test_bus_reg(vir_addr, testbus_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_selected_reg(vir_addr, i);
		break;
	case 92: /*
		  * output testbus register, BAM global regisers
		  * and registers of selected pipes
		  */
		print_bam_test_bus_reg(vir_addr, testbus_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_selected_reg(vir_addr, i);
		break;
	case 93: /*
		  * output registers and partial desc FIFOs
		  * of selected pipes: format 1
		  */
		if (desc_option == 0)
			desc_option = 1;
		print_bam_test_bus_reg(vir_addr, testbus_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_selected_reg(vir_addr, i);
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i,
							desc_option);
		break;
	case 94: /*
		  * output registers and partial desc FIFOs
		  * of selected pipes: format 2
		  */
		if (desc_option == 0)
			desc_option = 1;
		print_bam_test_bus_reg(vir_addr, testbus_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i)) {
				print_bam_pipe_selected_reg(vir_addr, i);
				print_bam_pipe_desc_fifo(vir_addr, i,
							desc_option);
			}
		break;
	case 95: /*
		  * output registers and desc FIFOs
		  * of selected pipes: format 1
		  */
		print_bam_test_bus_reg(vir_addr, testbus_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_selected_reg(vir_addr, i);
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i, 0);
		break;
	case 96: /*
		  * output registers and desc FIFOs
		  * of selected pipes: format 2
		  */
		print_bam_test_bus_reg(vir_addr, testbus_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i)) {
				print_bam_pipe_selected_reg(vir_addr, i);
				print_bam_pipe_desc_fifo(vir_addr, i, 0);
			}
		break;
	case 97: /*
		  * output registers, desc FIFOs and partial data blocks
		  * of selected pipes: format 1
		  */
		print_bam_test_bus_reg(vir_addr, testbus_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_selected_reg(vir_addr, i);
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i, 0);
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i, 100);
		break;
	case 98: /*
		  * output registers, desc FIFOs and partial data blocks
		  * of selected pipes: format 2
		  */
		print_bam_test_bus_reg(vir_addr, testbus_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (bam_pipe_sel & (1UL << i)) {
				print_bam_pipe_selected_reg(vir_addr, i);
				print_bam_pipe_desc_fifo(vir_addr, i, 0);
				print_bam_pipe_desc_fifo(vir_addr, i, 100);
			}
		break;
	case 99: /* output all registers, desc FIFOs and partial data blocks */
		print_bam_test_bus_reg(vir_addr, testbus_sel);
		print_bam_reg(bam->base);
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_reg(bam->base, i);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_selected_reg(vir_addr, i);
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_desc_fifo(vir_addr, i, 0);
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_desc_fifo(vir_addr, i, 100);
		break;
	default:
		pr_info("sps:no dump option is chosen yet\n");
	}

	return count;
}

static const struct file_operations sps_bam_addr_ops = {
	.write = sps_set_bam_addr,
};

static void sps_debugfs_init(void)
{
	debugfs_record_enabled = false;
	logging_option = 0;
	debug_level_option = 0;
	print_limit_option = 0;
	reg_dump_option = 0;
	testbus_sel = 0;
	bam_pipe_sel = 0;
	desc_option = 0;
	debugfs_buf_size = 0;
	debugfs_buf_used = 0;
	wraparound = false;
	log_level_sel = SPS_IPC_MAX_LOGLEVEL + 1;

	dent = debugfs_create_dir("sps", NULL);
	if (IS_ERR(dent)) {
		pr_err("sps:fail to create the folder for debug_fs\n");
		return;
	}

	dfile_info = debugfs_create_file("info", 0664, dent, NULL,
			&sps_info_ops);
	if (!dfile_info || IS_ERR(dfile_info)) {
		pr_err("sps:fail to create the file for debug_fs info\n");
		goto cleanup;
	}

	dfile_logging_option = debugfs_create_file("logging_option", 0664,
			dent, NULL, &sps_logging_option_ops);
	if (!dfile_logging_option || IS_ERR(dfile_logging_option)) {
		pr_err("sps:fail to create debug_fs for logging_option\n");
		goto cleanup;
	}

	debugfs_create_u8("debug_level_option",
					0664, dent, &debug_level_option);

	debugfs_create_u8("print_limit_option",
					0664, dent, &print_limit_option);

	debugfs_create_u8("reg_dump_option", 0664, dent, &reg_dump_option);

	debugfs_create_u32("testbus_sel", 0664, dent, &testbus_sel);

	debugfs_create_u32("bam_pipe_sel", 0664, dent, &bam_pipe_sel);

	debugfs_create_u32("desc_option", 0664, dent, &desc_option);

	dfile_bam_addr = debugfs_create_file("bam_addr", 0664,
			dent, NULL, &sps_bam_addr_ops);
	if (!dfile_bam_addr || IS_ERR(dfile_bam_addr)) {
		pr_err("sps:fail to create the file for debug_fs bam_addr\n");
		goto cleanup;
	}

	debugfs_create_u32("log_level_sel", 0664, dent, &log_level_sel);

	mutex_init(&sps_debugfs_lock);

	return;

cleanup:
	debugfs_remove_recursive(dent);
}

static void sps_debugfs_exit(void)
{
	debugfs_remove_recursive(dent);
	kfree(debugfs_buf);
	debugfs_buf = NULL;
}
#endif

/* Get the debug info of BAM registers and descriptor FIFOs */
int sps_get_bam_debug_info(unsigned long dev, u32 option, u32 para,
		u32 tb_sel, u32 desc_sel)
{
	int res = 0;
	struct sps_bam *bam;
	u32 i;
	u32 num_pipes = 0;
	void *vir_addr;

	if (dev == 0) {
		SPS_ERR(sps, "sps: device handle should not be 0\n");
		return SPS_ERROR;
	}

	if (sps == NULL || !sps->is_ready) {
		SPS_DBG3(sps, "sps: sps driver is not ready\n");
		return -EPROBE_DEFER;
	}

	mutex_lock(&sps->lock);
	/* Search for the target BAM device */
	bam = sps_h2bam(dev);
	if (bam == NULL) {
		pr_err("sps:Can't find any BAM with handle 0x%pK\n",
					(void *)dev);
		mutex_unlock(&sps->lock);
		return SPS_ERROR;
	}
	mutex_unlock(&sps->lock);

	vir_addr = &bam->base;
	num_pipes = bam->props.num_pipes;

	SPS_DUMP("sps:<bam-addr> dump BAM:%pa\n", &bam->props.phys_addr);

	switch (option) {
	case 1: /* output all registers of this BAM */
		print_bam_reg(bam->base);
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_reg(bam->base, i);
		break;
	case 2: /* output BAM-level registers */
		print_bam_reg(bam->base);
		break;
	case 3: /* output selected BAM-level registers */
		print_bam_selected_reg(vir_addr, bam->props.ee);
		break;
	case 4: /* output selected registers of all pipes */
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_selected_reg(vir_addr, i);
		break;
	case 5: /* output selected registers of selected pipes */
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_selected_reg(vir_addr, i);
		break;
	case 6: /* output selected registers of typical pipes */
		print_bam_pipe_selected_reg(vir_addr, 4);
		print_bam_pipe_selected_reg(vir_addr, 5);
		break;
	case 7: /* output desc FIFO of all pipes */
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_desc_fifo(vir_addr, i, 0);
		break;
	case 8: /* output desc FIFO of selected pipes */
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i, 0);
		break;
	case 9: /* output desc FIFO of typical pipes */
		print_bam_pipe_desc_fifo(vir_addr, 4, 0);
		print_bam_pipe_desc_fifo(vir_addr, 5, 0);
		break;
	case 10: /* output selected registers and desc FIFO of all pipes */
		for (i = 0; i < num_pipes; i++) {
			print_bam_pipe_selected_reg(vir_addr, i);
			print_bam_pipe_desc_fifo(vir_addr, i, 0);
		}
		break;
	case 11: /* output selected registers and desc FIFO of selected pipes */
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i)) {
				print_bam_pipe_selected_reg(vir_addr, i);
				print_bam_pipe_desc_fifo(vir_addr, i, 0);
			}
		break;
	case 12: /* output selected registers and desc FIFO of typical pipes */
		print_bam_pipe_selected_reg(vir_addr, 4);
		print_bam_pipe_desc_fifo(vir_addr, 4, 0);
		print_bam_pipe_selected_reg(vir_addr, 5);
		print_bam_pipe_desc_fifo(vir_addr, 5, 0);
		break;
	case 13: /* output BAM_TEST_BUS_REG */
		if (tb_sel)
			print_bam_test_bus_reg(vir_addr, tb_sel);
		else
			pr_info("sps:TEST_BUS_SEL should NOT be zero\n");
		break;
	case 14: /* output partial desc FIFO of selected pipes */
		if (desc_sel == 0)
			desc_sel = 1;
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i,
							desc_sel);
		break;
	case 15: /* output partial data blocks of descriptors */
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i, 100);
		break;
	case 16: /* output all registers of selected pipes */
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_reg(bam->base, i);
		break;
	case 91: /*
		  * output testbus register, BAM global regisers
		  * and registers of all pipes
		  */
		print_bam_test_bus_reg(vir_addr, tb_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_selected_reg(vir_addr, i);
		break;
	case 92: /*
		  * output testbus register, BAM global regisers
		  * and registers of selected pipes
		  */
		print_bam_test_bus_reg(vir_addr, tb_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_selected_reg(vir_addr, i);
		break;
	case 93: /*
		  * output registers and partial desc FIFOs
		  * of selected pipes: format 1
		  */
		if (desc_sel == 0)
			desc_sel = 1;
		print_bam_test_bus_reg(vir_addr, tb_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_selected_reg(vir_addr, i);
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i,
							desc_sel);
		break;
	case 94: /*
		  * output registers and partial desc FIFOs
		  * of selected pipes: format 2
		  */
		if (desc_sel == 0)
			desc_sel = 1;
		print_bam_test_bus_reg(vir_addr, tb_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i)) {
				print_bam_pipe_selected_reg(vir_addr, i);
				print_bam_pipe_desc_fifo(vir_addr, i,
							desc_sel);
			}
		break;
	case 95: /*
		  * output registers and desc FIFOs
		  * of selected pipes: format 1
		  */
		print_bam_test_bus_reg(vir_addr, tb_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_selected_reg(vir_addr, i);
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i, 0);
		break;
	case 96: /*
		  * output registers and desc FIFOs
		  * of selected pipes: format 2
		  */
		print_bam_test_bus_reg(vir_addr, tb_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i)) {
				print_bam_pipe_selected_reg(vir_addr, i);
				print_bam_pipe_desc_fifo(vir_addr, i, 0);
			}
		break;
	case 97: /*
		  * output registers, desc FIFOs and partial data blocks
		  * of selected pipes: format 1
		  */
		print_bam_test_bus_reg(vir_addr, tb_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_selected_reg(vir_addr, i);
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i, 0);
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i))
				print_bam_pipe_desc_fifo(vir_addr, i, 100);
		break;
	case 98: /*
		  * output registers, desc FIFOs and partial data blocks
		  * of selected pipes: format 2
		  */
		print_bam_test_bus_reg(vir_addr, tb_sel);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			if (para & (1UL << i)) {
				print_bam_pipe_selected_reg(vir_addr, i);
				print_bam_pipe_desc_fifo(vir_addr, i, 0);
				print_bam_pipe_desc_fifo(vir_addr, i, 100);
			}
		break;
	case 99: /* output all registers, desc FIFOs and partial data blocks */
		print_bam_test_bus_reg(vir_addr, tb_sel);
		print_bam_reg(bam->base);
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_reg(bam->base, i);
		print_bam_selected_reg(vir_addr, bam->props.ee);
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_selected_reg(vir_addr, i);
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_desc_fifo(vir_addr, i, 0);
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_desc_fifo(vir_addr, i, 100);
		break;
	default:
		pr_info("sps:no option is chosen yet\n");
	}

	return res;
}
EXPORT_SYMBOL(sps_get_bam_debug_info);

/**
 * Initialize SPS device
 *
 * This function initializes the SPS device.
 *
 * @return 0 on success, negative value on error
 *
 */
static int sps_device_init(void)
{
	int result;
	int success;
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
	struct sps_bam_props bamdma_props = {0};
#endif

	SPS_DBG3(sps, "Enter\n");

	success = false;

	result = sps_mem_init(sps->pipemem_phys_base, sps->pipemem_size);
	if (result) {
		SPS_ERR(sps, "sps: SPS memory init failed\n");
		goto exit_err;
	}

	INIT_LIST_HEAD(&sps->bams_q);
	mutex_init(&sps->lock);

	if (sps_rm_init(&sps->connection_ctrl, sps->options)) {
		SPS_ERR(sps, "sps: Fail to init SPS resource manager\n");
		goto exit_err;
	}

	result = sps_bam_driver_init(sps->options);
	if (result) {
		SPS_ERR(sps, "sps: SPS BAM driver init failed\n");
		goto exit_err;
	}

	/* Initialize the BAM DMA device */
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
	bamdma_props.phys_addr = sps->bamdma_bam_phys_base;
	bamdma_props.virt_addr = ioremap(sps->bamdma_bam_phys_base,
					 sps->bamdma_bam_size);

	if (!bamdma_props.virt_addr) {
		SPS_ERR(sps, "sps: Fail to IO map BAM-DMA BAM registers\n");
		goto exit_err;
	}

	SPS_DBG3(sps, "sps:bamdma_bam.phys=%pa.virt=0x%pK\n",
		&bamdma_props.phys_addr,
		bamdma_props.virt_addr);

	bamdma_props.periph_phys_addr =	sps->bamdma_dma_phys_base;
	bamdma_props.periph_virt_size = sps->bamdma_dma_size;
	bamdma_props.periph_virt_addr = ioremap(sps->bamdma_dma_phys_base,
						sps->bamdma_dma_size);

	if (!bamdma_props.periph_virt_addr) {
		SPS_ERR(sps, "sps: Fail to IO map BAM-DMA peripheral reg\n");
		goto exit_err;
	}

	SPS_DBG3(sps, "sps:bamdma_dma.phys=%pa.virt=0x%pK\n",
		&bamdma_props.periph_phys_addr,
		bamdma_props.periph_virt_addr);

	bamdma_props.irq = sps->bamdma_irq;

	bamdma_props.event_threshold = 0x10;	/* Pipe event threshold */
	bamdma_props.summing_threshold = 0x10;	/* BAM event threshold */

	bamdma_props.options = SPS_BAM_OPT_BAMDMA;
	bamdma_props.restricted_pipes =	sps->bamdma_restricted_pipes;

	result = sps_dma_init(&bamdma_props);
	if (result) {
		SPS_ERR(sps, "sps: SPS BAM DMA driver init failed\n");
		goto exit_err;
	}
#endif /* CONFIG_SPS_SUPPORT_BAMDMA */

	result = sps_map_init(NULL, sps->options);
	if (result) {
		SPS_ERR(sps,
			"sps: SPS connection mapping init failed\n");
		goto exit_err;
	}

	success = true;
exit_err:
	if (!success) {
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
		sps_device_de_init();
#endif
		return SPS_ERROR;
	}

	return 0;
}

/**
 * De-initialize SPS device
 *
 * This function de-initializes the SPS device.
 *
 * @return 0 on success, negative value on error
 *
 */
static void sps_device_de_init(void)
{
	SPS_DBG3(sps, "Enter\n");

	if (sps != NULL) {
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
		sps_dma_de_init();
#endif
		/* Are there any remaining BAM registrations? */
		if (!list_empty(&sps->bams_q))
			SPS_ERR(sps,
				"sps: BAMs are still registered\n");

		sps_map_de_init();
	}

	sps_mem_de_init();
}

/**
 * Initialize client state context
 *
 * This function initializes a client state context struct.
 *
 * @client - Pointer to client state context
 *
 * @return 0 on success, negative value on error
 *
 */
static int sps_client_init(struct sps_pipe *client)
{
	SPS_DBG(sps, "Enter\n");

	if (client == NULL)
		return -EINVAL;

	/*
	 * NOTE: Cannot store any state within the SPS driver because
	 * the driver init function may not have been called yet.
	 */
	memset(client, 0, sizeof(*client));
	sps_rm_config_init(&client->connect);

	client->client_state = SPS_STATE_DISCONNECT;
	client->bam = NULL;

	return 0;
}

/**
 * De-initialize client state context
 *
 * This function de-initializes a client state context struct.
 *
 * @client - Pointer to client state context
 *
 * @return 0 on success, negative value on error
 *
 */
static int sps_client_de_init(struct sps_pipe *client)
{
	SPS_DBG(sps, "Enter\n");

	if (client->client_state != SPS_STATE_DISCONNECT) {
		SPS_ERR(sps, "sps:De-init client in connected state: 0x%x\n",
				   client->client_state);
		return SPS_ERROR;
	}

	client->bam = NULL;
	client->map = NULL;
	memset(&client->connect, 0, sizeof(client->connect));

	return 0;
}

/**
 * Find the BAM device from the physical address
 *
 * This function finds a BAM device in the BAM registration list that
 * matches the specified physical address.
 *
 * @phys_addr - physical address of the BAM
 *
 * @return - pointer to the BAM device struct, or NULL on error
 *
 */
static struct sps_bam *phy2bam(phys_addr_t phys_addr)
{
	struct sps_bam *bam;

	SPS_DBG2(sps, "Enter\n");

	list_for_each_entry(bam, &sps->bams_q, list) {
		if (bam->props.phys_addr == phys_addr)
			return bam;
	}

	return NULL;
}

/**
 * Find the handle of a BAM device based on the physical address
 *
 * This function finds a BAM device in the BAM registration list that
 * matches the specified physical address, and returns its handle.
 *
 * @phys_addr - physical address of the BAM
 *
 * @h - device handle of the BAM
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_phy2h(phys_addr_t phys_addr, unsigned long *handle)
{
	struct sps_bam *bam;

	SPS_DBG2(sps, "Enter\n");

	if (sps == NULL || !sps->is_ready) {
		SPS_DBG3(sps, "sps: sps driver is not ready\n");
		return -EPROBE_DEFER;
	}

	if (handle == NULL) {
		SPS_ERR(sps, "sps: handle is NULL\n");
		return SPS_ERROR;
	}

	list_for_each_entry(bam, &sps->bams_q, list) {
		if (bam->props.phys_addr == phys_addr) {
			*handle = (uintptr_t) bam;
			return 0;
		}
	}

	SPS_ERR(sps,
		"sps: BAM device %pa is not registered yet\n", &phys_addr);

	return -ENODEV;
}
EXPORT_SYMBOL(sps_phy2h);

/**
 * Setup desc/data FIFO for bam-to-bam connection
 *
 * @mem_buffer - Pointer to struct for allocated memory properties.
 *
 * @addr - address of FIFO
 *
 * @size - FIFO size
 *
 * @use_offset - use address offset instead of absolute address
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_setup_bam2bam_fifo(struct sps_mem_buffer *mem_buffer,
		  u32 addr, u32 size, int use_offset)
{
	SPS_DBG1(sps, "Enter\n");

	if ((mem_buffer == NULL) || (size == 0)) {
		SPS_ERR(sps, "sps: invalid buffer address or size\n");
		return SPS_ERROR;
	}

	if (sps == NULL || !sps->is_ready) {
		SPS_DBG3(sps, "sps: sps driver is not ready\n");
		return -EPROBE_DEFER;
	}

	if (use_offset) {
		if ((addr + size) <= sps->pipemem_size)
			mem_buffer->phys_base = sps->pipemem_phys_base + addr;
		else {
			SPS_ERR(sps,
			"sps: requested mem is out of pipe mem range\n");
			return SPS_ERROR;
		}
	} else {
		if (addr >= sps->pipemem_phys_base &&
			(addr + size) <= (sps->pipemem_phys_base
						+ sps->pipemem_size))
			mem_buffer->phys_base = addr;
		else {
			SPS_ERR(sps,
			"sps: requested mem is out of pipe mem range\n");
			return SPS_ERROR;
		}
	}

	mem_buffer->base = spsi_get_mem_ptr(mem_buffer->phys_base);
	mem_buffer->size = size;

	memset(mem_buffer->base, 0, mem_buffer->size);

	return 0;
}
EXPORT_SYMBOL(sps_setup_bam2bam_fifo);

/**
 * Find the BAM device from the handle
 *
 * This function finds a BAM device in the BAM registration list that
 * matches the specified device handle.
 *
 * @h - device handle of the BAM
 *
 * @return - pointer to the BAM device struct, or NULL on error
 *
 */
struct sps_bam *sps_h2bam(unsigned long h)
{
	struct sps_bam *bam;

	SPS_DBG1(sps, "sps: BAM handle:0x%pK\n", (void *)h);

	if (h == SPS_DEV_HANDLE_MEM || h == SPS_DEV_HANDLE_INVALID)
		return NULL;

	list_for_each_entry(bam, &sps->bams_q, list) {
		if ((uintptr_t) bam == h)
			return bam;
	}

	SPS_ERR(sps, "sps:Can't find BAM device for handle 0x%pK\n", (void *)h);

	return NULL;
}

/**
 * Lock BAM device
 *
 * This function obtains the BAM spinlock on the client's connection.
 *
 * @pipe - pointer to client pipe state
 *
 * @return pointer to BAM device struct, or NULL on error
 *
 */
static struct sps_bam *sps_bam_lock(struct sps_pipe *pipe)
{
	struct sps_bam *bam;
	u32 pipe_index;

	bam = pipe->bam;
	if (bam == NULL) {
		SPS_ERR(sps, "sps: Connection is not in connected state\n");
		return NULL;
	}

	spin_lock_irqsave(&bam->connection_lock, bam->irqsave_flags);

	/* Verify client owns this pipe */
	pipe_index = pipe->pipe_index;
	if (pipe_index >= bam->props.num_pipes ||
	    pipe != bam->pipes[pipe_index]) {
		SPS_ERR(bam,
			"sps:Client not owner of BAM %pa pipe: %d (max %d)\n",
			&bam->props.phys_addr, pipe_index,
			bam->props.num_pipes);
		spin_unlock_irqrestore(&bam->connection_lock,
						bam->irqsave_flags);
		return NULL;
	}

	return bam;
}

/**
 * Unlock BAM device
 *
 * This function releases the BAM spinlock on the client's connection.
 *
 * @bam - pointer to BAM device struct
 *
 */
static inline void sps_bam_unlock(struct sps_bam *bam)
{
	spin_unlock_irqrestore(&bam->connection_lock, bam->irqsave_flags);
}

/**
 * Connect an SPS connection end point
 *
 */
int sps_connect(struct sps_pipe *h, struct sps_connect *connect)
{
	struct sps_pipe *pipe = h;
	unsigned long dev;
	struct sps_bam *bam;
	int result;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	} else if (connect == NULL) {
		SPS_ERR(sps, "sps: connection is NULL\n");
		return SPS_ERROR;
	}

	if (sps == NULL)
		return -ENODEV;

	if (!sps->is_ready) {
		SPS_ERR(sps, "sps: sps driver is not ready\n");
		return -EAGAIN;
	}

	if ((connect->lock_group != SPSRM_CLEAR)
		&& (connect->lock_group > BAM_MAX_P_LOCK_GROUP_NUM)) {
		SPS_ERR(sps,
			"sps: The value of pipe lock group is invalid\n");
		return SPS_ERROR;
	}

	mutex_lock(&sps->lock);
	/*
	 * Must lock the BAM device at the top level function, so must
	 * determine which BAM is the target for the connection
	 */
	if (connect->mode == SPS_MODE_SRC)
		dev = connect->source;
	else
		dev = connect->destination;

	bam = sps_h2bam(dev);
	if (bam == NULL) {
		SPS_ERR(sps, "sps:Invalid BAM device handle: 0x%pK\n",
					(void *)dev);
		result = SPS_ERROR;
		goto exit_err;
	}

	mutex_lock(&bam->lock);
	SPS_DBG2(bam, "sps: bam %pa src 0x%pK dest 0x%pK mode %s\n",
			BAM_ID(bam),
			(void *)connect->source,
			(void *)connect->destination,
			connect->mode == SPS_MODE_SRC ? "SRC" : "DEST");

	/* Allocate resources for the specified connection */
	pipe->connect = *connect;
	result = sps_rm_state_change(pipe, SPS_STATE_ALLOCATE);
	if (result) {
		mutex_unlock(&bam->lock);
		goto exit_err;
	}

	/* Configure the connection */
	result = sps_rm_state_change(pipe, SPS_STATE_CONNECT);
	mutex_unlock(&bam->lock);
	if (result) {
		sps_disconnect(h);
		goto exit_err;
	}

exit_err:
	mutex_unlock(&sps->lock);

	return result;
}
EXPORT_SYMBOL(sps_connect);

/**
 * Disconnect an SPS connection end point
 *
 * This function disconnects an SPS connection end point.
 * The SPS hardware associated with that end point will be disabled.
 * For a connection involving system memory (SPS_DEV_HANDLE_MEM), all
 * connection resources are deallocated.  For a peripheral-to-peripheral
 * connection, the resources associated with the connection will not be
 * deallocated until both end points are closed.
 *
 * The client must call sps_connect() for the handle before calling
 * this function.
 *
 * @h - client context for SPS connection end point
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_disconnect(struct sps_pipe *h)
{
	struct sps_pipe *pipe = h;
	struct sps_pipe *check;
	struct sps_bam *bam;
	int result;

	if (pipe == NULL) {
		SPS_ERR(sps, "sps: Invalid pipe\n");
		return SPS_ERROR;
	}

	bam = pipe->bam;
	if (bam == NULL) {
		SPS_ERR(sps,
			"sps: BAM device of this pipe is NULL\n");
		return SPS_ERROR;
	}

	SPS_DBG2(bam,
		"sps: bam %pa src 0x%pK dest 0x%pK mode %s\n",
		BAM_ID(bam),
		(void *)pipe->connect.source,
		(void *)pipe->connect.destination,
		pipe->connect.mode == SPS_MODE_SRC ? "SRC" : "DEST");

	result = SPS_ERROR;
	/* Cross-check client with map table */
	if (pipe->connect.mode == SPS_MODE_SRC)
		check = pipe->map->client_src;
	else
		check = pipe->map->client_dest;

	if (check != pipe) {
		SPS_ERR(sps, "sps: Client context is corrupt\n");
		goto exit_err;
	}

	/* Disconnect the BAM pipe */
	mutex_lock(&bam->lock);
	result = sps_rm_state_change(pipe, SPS_STATE_DISCONNECT);
	mutex_unlock(&bam->lock);
	if (result)
		goto exit_err;

	sps_rm_config_init(&pipe->connect);
	result = 0;

exit_err:

	return result;
}
EXPORT_SYMBOL(sps_disconnect);

/**
 * Register an event object for an SPS connection end point
 *
 */
int sps_register_event(struct sps_pipe *h, struct sps_register_event *reg)
{
	struct sps_pipe *pipe = h;
	struct sps_bam *bam;
	int result;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	} else if (reg == NULL) {
		SPS_ERR(sps, "sps: registered event is NULL\n");
		return SPS_ERROR;
	}

	if (sps == NULL)
		return -ENODEV;

	if (!sps->is_ready) {
		SPS_ERR(sps, "sps: sps driver not ready\n");
		return -EAGAIN;
	}

	bam = sps_bam_lock(pipe);
	if (bam == NULL)
		return SPS_ERROR;

	SPS_DBG2(bam, "sps: events:%d\n", reg->options);

	result = sps_bam_pipe_reg_event(bam, pipe->pipe_index, reg);
	sps_bam_unlock(bam);
	if (result)
		SPS_ERR(bam,
			"sps:Fail to register event for BAM %pa pipe %d\n",
			&pipe->bam->props.phys_addr, pipe->pipe_index);

	return result;
}
EXPORT_SYMBOL(sps_register_event);

/**
 * Enable an SPS connection end point
 *
 */
int sps_flow_on(struct sps_pipe *h)
{
	struct sps_pipe *pipe = h;
	struct sps_bam *bam;
	int result = 0;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_bam_lock(pipe);
	if (bam == NULL)
		return SPS_ERROR;

	bam_pipe_halt(&bam->base, pipe->pipe_index, false);

	sps_bam_unlock(bam);

	return result;
}
EXPORT_SYMBOL(sps_flow_on);

/**
 * Disable an SPS connection end point
 *
 */
int sps_flow_off(struct sps_pipe *h, enum sps_flow_off mode)
{
	struct sps_pipe *pipe = h;
	struct sps_bam *bam;
	int result = 0;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_bam_lock(pipe);
	if (bam == NULL)
		return SPS_ERROR;

	bam_pipe_halt(&bam->base, pipe->pipe_index, true);

	sps_bam_unlock(bam);

	return result;
}
EXPORT_SYMBOL(sps_flow_off);

/**
 * Check if the flags on a descriptor/iovec are valid
 *
 * @flags - flags on a descriptor/iovec
 *
 * @return 0 on success, negative value on error
 *
 */
static int sps_check_iovec_flags(u32 flags)
{
	if ((flags & SPS_IOVEC_FLAG_NWD) &&
		!(flags & (SPS_IOVEC_FLAG_EOT | SPS_IOVEC_FLAG_CMD))) {
		SPS_ERR(sps,
			"sps: NWD is only valid with EOT or CMD\n");
		return SPS_ERROR;
	} else if ((flags & SPS_IOVEC_FLAG_EOT) &&
		(flags & SPS_IOVEC_FLAG_CMD)) {
		SPS_ERR(sps,
			"sps: EOT and CMD are not allowed to coexist\n");
		return SPS_ERROR;
	} else if (!(flags & SPS_IOVEC_FLAG_CMD) &&
		(flags & (SPS_IOVEC_FLAG_LOCK | SPS_IOVEC_FLAG_UNLOCK))) {
		SPS_ERR(sps,
			"sps: pipe lock/unlock flags are only valid with Command Descriptor\n");
		return SPS_ERROR;
	} else if ((flags & SPS_IOVEC_FLAG_LOCK) &&
		(flags & SPS_IOVEC_FLAG_UNLOCK)) {
		SPS_ERR(sps,
			"sps: Can't lock and unlock a pipe by the same Command Descriptor\n");
		return SPS_ERROR;
	} else if ((flags & SPS_IOVEC_FLAG_IMME) &&
		(flags & SPS_IOVEC_FLAG_CMD)) {
		SPS_ERR(sps,
			"sps: Immediate and CMD are not allowed to coexist\n");
		return SPS_ERROR;
	} else if ((flags & SPS_IOVEC_FLAG_IMME) &&
		(flags & SPS_IOVEC_FLAG_NWD)) {
		SPS_ERR(sps,
			"sps: Immediate and NWD are not allowed to coexist\n");
		return SPS_ERROR;
	}

	return 0;
}

/**
 * Perform a DMA transfer on an SPS connection end point
 *
 */
int sps_transfer(struct sps_pipe *h, struct sps_transfer *transfer)
{
	struct sps_pipe *pipe = h;
	struct sps_bam *bam;
	int result;
	struct sps_iovec *iovec;
	int i;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	} else if (transfer == NULL) {
		SPS_ERR(sps, "sps: transfer is NULL\n");
		return SPS_ERROR;
	} else if (transfer->iovec == NULL) {
		SPS_ERR(sps, "sps: iovec list is NULL\n");
		return SPS_ERROR;
	} else if (transfer->iovec_count == 0) {
		SPS_ERR(sps, "sps: iovec list is empty\n");
		return SPS_ERROR;
	} else if (transfer->iovec_phys == 0) {
		SPS_ERR(sps,
			"sps: iovec list address is invalid\n");
		return SPS_ERROR;
	}

	/* Verify content of IOVECs */
	iovec = transfer->iovec;
	for (i = 0; i < transfer->iovec_count; i++) {
		u32 flags = iovec->flags;

		if (iovec->size > SPS_IOVEC_MAX_SIZE) {
			SPS_ERR(sps,
				"sps: iovec size is invalid\n");
			return SPS_ERROR;
		}

		if (sps_check_iovec_flags(flags))
			return SPS_ERROR;

		iovec++;
	}

	bam = sps_bam_lock(pipe);
	if (bam == NULL)
		return SPS_ERROR;

	result = sps_bam_pipe_transfer(bam, pipe->pipe_index, transfer);

	sps_bam_unlock(bam);

	return result;
}
EXPORT_SYMBOL(sps_transfer);

/**
 * Perform a single DMA transfer on an SPS connection end point
 *
 */
int sps_transfer_one(struct sps_pipe *h, phys_addr_t addr, u32 size,
		     void *user, u32 flags)
{
	struct sps_pipe *pipe = h;
	struct sps_bam *bam;
	int result;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	}

	if (sps_check_iovec_flags(flags))
		return SPS_ERROR;

	bam = sps_bam_lock(pipe);
	if (bam == NULL)
		return SPS_ERROR;

	result = sps_bam_pipe_transfer_one(bam, pipe->pipe_index,
				SPS_GET_LOWER_ADDR(addr), size, user,
				DESC_FLAG_WORD(flags, addr));

	sps_bam_unlock(bam);

	return result;
}
EXPORT_SYMBOL(sps_transfer_one);

/**
 * Read event queue for an SPS connection end point
 *
 */
int sps_get_event(struct sps_pipe *h, struct sps_event_notify *notify)
{
	struct sps_pipe *pipe = h;
	struct sps_bam *bam;
	int result;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	} else if (notify == NULL) {
		SPS_ERR(sps, "sps: event_notify is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_bam_lock(pipe);
	if (bam == NULL)
		return SPS_ERROR;

	result = sps_bam_pipe_get_event(bam, pipe->pipe_index, notify);
	sps_bam_unlock(bam);

	return result;
}
EXPORT_SYMBOL(sps_get_event);

/**
 * Determine whether an SPS connection end point FIFO is empty
 *
 */
int sps_is_pipe_empty(struct sps_pipe *h, u32 *empty)
{
	struct sps_pipe *pipe = h;
	struct sps_bam *bam;
	int result;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	} else if (empty == NULL) {
		SPS_ERR(sps, "sps: result pointer is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_bam_lock(pipe);
	if (bam == NULL)
		return SPS_ERROR;

	result = sps_bam_pipe_is_empty(bam, pipe->pipe_index, empty);
	sps_bam_unlock(bam);

	return result;
}
EXPORT_SYMBOL(sps_is_pipe_empty);

/**
 * Get number of free transfer entries for an SPS connection end point
 *
 */
int sps_get_free_count(struct sps_pipe *h, u32 *count)
{
	struct sps_pipe *pipe = h;
	struct sps_bam *bam;
	int result;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	} else if (count == NULL) {
		SPS_ERR(sps, "sps: result pointer is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_bam_lock(pipe);
	if (bam == NULL)
		return SPS_ERROR;

	result = sps_bam_get_free_count(bam, pipe->pipe_index, count);
	sps_bam_unlock(bam);

	return result;
}
EXPORT_SYMBOL(sps_get_free_count);

/**
 * Reset an SPS BAM device
 *
 */
int sps_device_reset(unsigned long dev)
{
	struct sps_bam *bam;
	int result;

	if (dev == 0) {
		SPS_ERR(sps,
			"sps: device handle should not be 0\n");
		return SPS_ERROR;
	}

	if (sps == NULL || !sps->is_ready) {
		SPS_DBG3(sps, "sps: sps driver is not ready\n");
		return -EPROBE_DEFER;
	}

	mutex_lock(&sps->lock);
	/* Search for the target BAM device */
	bam = sps_h2bam(dev);
	if (bam == NULL) {
		SPS_ERR(sps, "sps:Invalid BAM device handle: 0x%pK\n",
					(void *)dev);
		result = SPS_ERROR;
		goto exit_err;
	}

	mutex_lock(&bam->lock);
	result = sps_bam_reset(bam);
	mutex_unlock(&bam->lock);
	if (result) {
		SPS_ERR(sps, "sps:Fail to reset BAM device: 0x%pK\n",
					(void *)dev);
		goto exit_err;
	}

exit_err:
	mutex_unlock(&sps->lock);

	return result;
}
EXPORT_SYMBOL(sps_device_reset);

/**
 * Get the configuration parameters for an SPS connection end point
 *
 */
int sps_get_config(struct sps_pipe *h, struct sps_connect *config)
{
	struct sps_pipe *pipe = h;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	} else if (config == NULL) {
		SPS_ERR(sps, "sps: config pointer is NULL\n");
		return SPS_ERROR;
	}

	if (pipe->bam == NULL)
		SPS_DBG(sps, "sps:%s\n", __func__);
	else
		SPS_DBG(pipe->bam,
			"sps: BAM: %pa; pipe index:%d; options:0x%x\n",
			BAM_ID(pipe->bam), pipe->pipe_index,
			pipe->connect.options);

	/* Copy current client connection state */
	*config = pipe->connect;

	return 0;
}
EXPORT_SYMBOL(sps_get_config);

/**
 * Set the configuration parameters for an SPS connection end point
 *
 */
int sps_set_config(struct sps_pipe *h, struct sps_connect *config)
{
	struct sps_pipe *pipe = h;
	struct sps_bam *bam;
	int result;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	} else if (config == NULL) {
		SPS_ERR(sps, "sps: config pointer is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_bam_lock(pipe);
	if (bam == NULL) {
		SPS_ERR(sps, "sps: BAM is NULL\n");
		return SPS_ERROR;
	}

	SPS_DBG(bam, "sps: BAM: %pa; pipe index:%d, config-options:0x%x\n",
			BAM_ID(bam), pipe->pipe_index, config->options);

	result = sps_bam_pipe_set_params(bam, pipe->pipe_index,
					 config->options);
	if (result == 0)
		pipe->connect.options = config->options;
	sps_bam_unlock(bam);

	return result;
}
EXPORT_SYMBOL(sps_set_config);

/**
 * Set ownership of an SPS connection end point
 *
 */
int sps_set_owner(struct sps_pipe *h, enum sps_owner owner,
		  struct sps_satellite *connect)
{
	struct sps_pipe *pipe = h;
	struct sps_bam *bam;
	int result;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	} else if (connect == NULL) {
		SPS_ERR(sps, "sps: connection is NULL\n");
		return SPS_ERROR;
	}

	if (owner != SPS_OWNER_REMOTE) {
		SPS_ERR(sps, "sps: Unsupported ownership state: %d\n", owner);
		return SPS_ERROR;
	}

	bam = sps_bam_lock(pipe);
	if (bam == NULL)
		return SPS_ERROR;

	SPS_DBG(bam, "sps: BAM: %pa; pipe index:%d\n",
			BAM_ID(bam), pipe->pipe_index);

	result = sps_bam_set_satellite(bam, pipe->pipe_index);
	if (result)
		goto exit_err;

	/* Return satellite connect info */
	if (connect == NULL)
		goto exit_err;

	if (pipe->connect.mode == SPS_MODE_SRC) {
		connect->dev = pipe->map->src.bam_phys;
		connect->pipe_index = pipe->map->src.pipe_index;
	} else {
		connect->dev = pipe->map->dest.bam_phys;
		connect->pipe_index = pipe->map->dest.pipe_index;
	}
	connect->config = SPS_CONFIG_SATELLITE;
	connect->options = (enum sps_option) 0;

exit_err:
	sps_bam_unlock(bam);

	return result;
}
EXPORT_SYMBOL(sps_set_owner);

/**
 * Allocate memory from the SPS Pipe-Memory.
 *
 */
int sps_alloc_mem(struct sps_pipe *h, enum sps_mem mem,
		  struct sps_mem_buffer *mem_buffer)
{
	if (sps == NULL)
		return -ENODEV;

	if (!sps->is_ready) {
		SPS_ERR(sps, "sps: sps driver is not ready\n");
		return -EAGAIN;
	}

	if (mem_buffer == NULL || mem_buffer->size == 0) {
		SPS_ERR(sps, "sps: invalid memory buffer address or size\n");
		return SPS_ERROR;
	}

	if (h == NULL)
		SPS_DBG2(sps,
			"sps: allocate pipe memory before setup pipe\n");
	else
		SPS_DBG2(sps,
			"sps:allocate pipe memory for pipe %d\n",
			h->pipe_index);

	mem_buffer->phys_base = sps_mem_alloc_io(mem_buffer->size);
	if (mem_buffer->phys_base == SPS_ADDR_INVALID) {
		SPS_ERR(sps, "sps: invalid address of allocated memory\n");
		return SPS_ERROR;
	}

	mem_buffer->base = spsi_get_mem_ptr(mem_buffer->phys_base);

	return 0;
}
EXPORT_SYMBOL(sps_alloc_mem);

/**
 * Free memory from the SPS Pipe-Memory.
 *
 */
int sps_free_mem(struct sps_pipe *h, struct sps_mem_buffer *mem_buffer)
{
	SPS_DBG(sps, "sps: Enter\n");

	if (mem_buffer == NULL || mem_buffer->phys_base == SPS_ADDR_INVALID) {
		SPS_ERR(sps, "sps: invalid memory to free\n");
		return SPS_ERROR;
	}

	if (h == NULL)
		SPS_DBG2(sps, "sps: free pipe memory\n");
	else
		SPS_DBG2(sps,
			"sps:free pipe memory for pipe %d\n", h->pipe_index);

	sps_mem_free_io(mem_buffer->phys_base, mem_buffer->size);

	return 0;
}
EXPORT_SYMBOL(sps_free_mem);

/**
 * Get the number of unused descriptors in the descriptor FIFO
 * of a pipe
 *
 */
int sps_get_unused_desc_num(struct sps_pipe *h, u32 *desc_num)
{
	struct sps_pipe *pipe = h;
	struct sps_bam *bam;
	int result;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	} else if (desc_num == NULL) {
		SPS_ERR(sps, "sps: result pointer is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_bam_lock(pipe);
	if (bam == NULL)
		return SPS_ERROR;

	SPS_DBG(bam, "sps: BAM: %pa; pipe index:%d\n", BAM_ID(bam),
			pipe->pipe_index);

	result = sps_bam_pipe_get_unused_desc_num(bam, pipe->pipe_index,
						desc_num);

	sps_bam_unlock(bam);

	return result;
}
EXPORT_SYMBOL(sps_get_unused_desc_num);

/**
 * Vote for or relinquish BAM DMA clock
 *
 */
int sps_ctrl_bam_dma_clk(bool clk_on)
{
	int ret;

	if (sps == NULL || !sps->is_ready) {
		SPS_DBG3(sps, "sps: sps driver is not ready\n");
		return -EPROBE_DEFER;
	}

	if (clk_on) {
		SPS_DBG1(sps, "%s", "sps:vote for bam dma clk\n");
		ret = clk_prepare_enable(sps->bamdma_clk);
		if (ret) {
			SPS_ERR(sps,
				"sps:fail to enable bamdma_clk:ret=%d\n", ret);
			return ret;
		}
	} else {
		SPS_DBG1(sps, "%s", "sps:relinquish bam dma clk\n");
		clk_disable_unprepare(sps->bamdma_clk);
	}

	return 0;
}
EXPORT_SYMBOL(sps_ctrl_bam_dma_clk);

/**
 * Register a BAM device
 *
 */
int sps_register_bam_device(const struct sps_bam_props *bam_props,
				unsigned long *dev_handle)
{
	struct sps_bam *bam = NULL;
	void __iomem *virt_addr = NULL;
	char bam_name[MAX_MSG_LEN];
	u32 manage;
	int ok;
	int result;

	if (bam_props == NULL) {
		SPS_ERR(sps, "sps: bam_props is NULL\n");
		return SPS_ERROR;
	} else if (dev_handle == NULL) {
		SPS_ERR(sps, "sps: device handle is NULL\n");
		return SPS_ERROR;
	}

	if (sps == NULL) {
		pr_err("sps: sps driver is not ready\n");
		return -EPROBE_DEFER;
	}

	SPS_DBG3(sps, "sps: Client requests to register BAM %pa\n",
			&bam_props->phys_addr);

	/* BAM-DMA is registered internally during power-up */
	if ((!sps->is_ready) && !(bam_props->options & SPS_BAM_OPT_BAMDMA)) {
		SPS_ERR(sps, "sps: sps driver not ready\n");
		return -EAGAIN;
	}

	/* Check BAM parameters */
	manage = bam_props->manage & SPS_BAM_MGR_ACCESS_MASK;
	if (manage != SPS_BAM_MGR_NONE) {
		if (bam_props->virt_addr == NULL && bam_props->virt_size == 0) {
			SPS_ERR(sps, "sps:Invalid properties for BAM: %pa\n",
					   &bam_props->phys_addr);
			return SPS_ERROR;
		}
	}
	if ((bam_props->manage & SPS_BAM_MGR_DEVICE_REMOTE) == 0) {
		/* BAM global is configured by local processor */
		if (bam_props->summing_threshold == 0) {
			SPS_ERR(sps,
				"sps:Invalid device ctrl properties for BAM: %pa\n",
				&bam_props->phys_addr);
			return SPS_ERROR;
		}
	}
	manage = bam_props->manage &
		  (SPS_BAM_MGR_PIPE_NO_CONFIG | SPS_BAM_MGR_PIPE_NO_CTRL);

	/* In case of error */
	*dev_handle = SPS_DEV_HANDLE_INVALID;
	result = SPS_ERROR;

	mutex_lock(&sps->lock);
	/* Is this BAM already registered? */
	bam = phy2bam(bam_props->phys_addr);
	if (bam != NULL) {
		mutex_unlock(&sps->lock);
		SPS_ERR(sps, "sps:BAM is already registered: %pa\n",
				&bam->props.phys_addr);
		result = -EEXIST;
		bam = NULL;   /* Avoid error clean-up kfree(bam) */
		goto exit_err;
	}

	/* Perform virtual mapping if required */
	if ((bam_props->manage & SPS_BAM_MGR_ACCESS_MASK) !=
	    SPS_BAM_MGR_NONE && bam_props->virt_addr == NULL) {
		/* Map the memory region */
		virt_addr = ioremap(bam_props->phys_addr, bam_props->virt_size);
		if (virt_addr == NULL) {
			SPS_ERR(sps,
				"sps:Unable to map BAM IO mem:%pa size:0x%x\n",
				&bam_props->phys_addr, bam_props->virt_size);
			goto exit_err;
		}
	}

	bam = kzalloc(sizeof(*bam), GFP_KERNEL);
	if (bam == NULL)
		goto exit_err;

	memset(bam, 0, sizeof(*bam));

	mutex_init(&bam->lock);
	mutex_lock(&bam->lock);

	/* Copy configuration to BAM device descriptor */
	bam->props = *bam_props;
	if (virt_addr != NULL)
		bam->props.virt_addr = virt_addr;

	snprintf(bam_name, sizeof(bam_name), "sps_bam_%pa_0",
					&bam->props.phys_addr);
	bam->ipc_log0 = ipc_log_context_create(SPS_IPC_LOGPAGES,
							bam_name, 0);
	if (!bam->ipc_log0)
		SPS_ERR(sps, "unable to create IPC Log 0 for bam %pa\n",
				&bam->props.phys_addr);

	snprintf(bam_name, sizeof(bam_name), "sps_bam_%pa_1",
					&bam->props.phys_addr);
	bam->ipc_log1 = ipc_log_context_create(SPS_IPC_LOGPAGES,
							bam_name, 0);
	if (!bam->ipc_log1)
		SPS_ERR(sps, "unable to create IPC Log 1 for bam %pa\n",
				&bam->props.phys_addr);

	snprintf(bam_name, sizeof(bam_name), "sps_bam_%pa_2",
					&bam->props.phys_addr);
	bam->ipc_log2 = ipc_log_context_create(SPS_IPC_LOGPAGES,
							bam_name, 0);
	if (!bam->ipc_log2)
		SPS_ERR(sps, "unable to create IPC Log 2 for bam %pa\n",
				&bam->props.phys_addr);

	snprintf(bam_name, sizeof(bam_name), "sps_bam_%pa_3",
					&bam->props.phys_addr);
	bam->ipc_log3 = ipc_log_context_create(SPS_IPC_LOGPAGES,
							bam_name, 0);
	if (!bam->ipc_log3)
		SPS_ERR(sps, "unable to create IPC Log 3 for bam %pa\n",
				&bam->props.phys_addr);

	snprintf(bam_name, sizeof(bam_name), "sps_bam_%pa_4",
					&bam->props.phys_addr);
	bam->ipc_log4 = ipc_log_context_create(SPS_IPC_LOGPAGES,
							bam_name, 0);
	if (!bam->ipc_log4)
		SPS_ERR(sps, "unable to create IPC Log 4 for bam %pa\n",
				&bam->props.phys_addr);

	if (bam_props->ipc_loglevel)
		bam->ipc_loglevel = bam_props->ipc_loglevel;
	else
		bam->ipc_loglevel = SPS_IPC_DEFAULT_LOGLEVEL;

	ok = sps_bam_device_init(bam);
	mutex_unlock(&bam->lock);
	if (ok) {
		SPS_ERR(bam, "sps:Fail to init BAM device: phys %pa\n",
			&bam->props.phys_addr);
		goto exit_err;
	}

	/* Add BAM to the list */
	list_add_tail(&bam->list, &sps->bams_q);
	*dev_handle = (uintptr_t) bam;

	result = 0;
exit_err:
	mutex_unlock(&sps->lock);

	if (result) {
		if (bam != NULL) {
			if (virt_addr != NULL)
				iounmap(bam->props.virt_addr);
			kfree(bam);
		}

		return result;
	}

	/* If this BAM is attached to a BAM-DMA, init the BAM-DMA device */
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
	if ((bam->props.options & SPS_BAM_OPT_BAMDMA)) {
		if (sps_dma_device_init((uintptr_t) bam)) {
			bam->props.options &= ~SPS_BAM_OPT_BAMDMA;
			sps_deregister_bam_device((uintptr_t) bam);
			SPS_ERR(bam, "sps:Fail to init BAM-DMA BAM: phys %pa\n",
				&bam->props.phys_addr);
			return SPS_ERROR;
		}
	}
#endif /* CONFIG_SPS_SUPPORT_BAMDMA */

	SPS_INFO(bam, "sps:BAM %pa is registered\n", &bam->props.phys_addr);

	return 0;
}
EXPORT_SYMBOL(sps_register_bam_device);

/**
 * Deregister a BAM device
 *
 */
int sps_deregister_bam_device(unsigned long dev_handle)
{
	struct sps_bam *bam;
	int n;

	if (dev_handle == 0) {
		SPS_ERR(sps, "sps: device handle should not be 0\n");
		return SPS_ERROR;
	}

	bam = sps_h2bam(dev_handle);
	if (bam == NULL) {
		SPS_ERR(sps, "sps: did not find a BAM for this handle\n");
		return SPS_ERROR;
	}

	SPS_DBG3(sps, "sps: SPS deregister BAM: phys %pa\n",
			&bam->props.phys_addr);

	if (bam->props.options & SPS_BAM_HOLD_MEM) {
		for (n = 0; n < BAM_MAX_PIPES; n++)
			kfree(bam->desc_cache_pointers[n]);
	}

	/* If this BAM is attached to a BAM-DMA, init the BAM-DMA device */
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
	if ((bam->props.options & SPS_BAM_OPT_BAMDMA)) {
		mutex_lock(&bam->lock);
		(void)sps_dma_device_de_init((uintptr_t) bam);
		bam->props.options &= ~SPS_BAM_OPT_BAMDMA;
		mutex_unlock(&bam->lock);
	}
#endif

	/* Remove the BAM from the registration list */
	mutex_lock(&sps->lock);
	list_del(&bam->list);
	mutex_unlock(&sps->lock);

	/* De-init the BAM and free resources */
	mutex_lock(&bam->lock);
	sps_bam_device_de_init(bam);
	mutex_unlock(&bam->lock);
	ipc_log_context_destroy(bam->ipc_log0);
	ipc_log_context_destroy(bam->ipc_log1);
	ipc_log_context_destroy(bam->ipc_log2);
	ipc_log_context_destroy(bam->ipc_log3);
	ipc_log_context_destroy(bam->ipc_log4);
	if (bam->props.virt_size)
		(void)iounmap(bam->props.virt_addr);

	kfree(bam);

	return 0;
}
EXPORT_SYMBOL(sps_deregister_bam_device);

/**
 * Get processed I/O vector (completed transfers)
 *
 */
int sps_get_iovec(struct sps_pipe *h, struct sps_iovec *iovec)
{
	struct sps_pipe *pipe = h;
	struct sps_bam *bam;
	int result;

	if (h == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	} else if (iovec == NULL) {
		SPS_ERR(sps, "sps: iovec pointer is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_bam_lock(pipe);
	if (bam == NULL) {
		SPS_ERR(sps, "sps: BAM is not found by handle\n");
		return SPS_ERROR;
	}

	SPS_DBG(bam, "sps: BAM: %pa; pipe index:%d\n",
			BAM_ID(bam), pipe->pipe_index);

	/* Get an iovec from the BAM pipe descriptor FIFO */
	result = sps_bam_pipe_get_iovec(bam, pipe->pipe_index, iovec);
	sps_bam_unlock(bam);

	return result;
}
EXPORT_SYMBOL(sps_get_iovec);

/**
 * Perform timer control
 *
 */
int sps_timer_ctrl(struct sps_pipe *h,
			struct sps_timer_ctrl *timer_ctrl,
			struct sps_timer_result *timer_result)
{
	return 0;
}
EXPORT_SYMBOL(sps_timer_ctrl);

/*
 * Reset a BAM pipe
 */
int sps_pipe_reset(unsigned long dev, u32 pipe)
{
	struct sps_bam *bam;

	if (!dev) {
		SPS_ERR(sps, "sps: BAM handle is NULL\n");
		return SPS_ERROR;
	}

	if (pipe >= BAM_MAX_PIPES) {
		SPS_ERR(sps, "sps: pipe index is invalid\n");
		return SPS_ERROR;
	}

	bam = sps_h2bam(dev);
	if (bam == NULL) {
		SPS_ERR(sps, "sps: BAM is not found by handle\n");
		return SPS_ERROR;
	}

	SPS_DBG2(bam, "sps: BAM: %pa; pipe index:%d\n", BAM_ID(bam), pipe);

	bam_pipe_reset(&bam->base, pipe);

	return 0;
}
EXPORT_SYMBOL(sps_pipe_reset);

/*
 * Disable a BAM pipe
 */
int sps_pipe_disable(unsigned long dev, u32 pipe)
{
	struct sps_bam *bam;

	if (!dev) {
		SPS_ERR(sps, "sps: BAM handle is NULL\n");
		return SPS_ERROR;
	}

	if (pipe >= BAM_MAX_PIPES) {
		SPS_ERR(sps, "sps: pipe index is invalid\n");
		return SPS_ERROR;
	}

	bam = sps_h2bam(dev);
	if (bam == NULL) {
		SPS_ERR(sps, "sps: BAM is not found by handle\n");
		return SPS_ERROR;
	}

	SPS_DBG(bam, "sps: BAM: %pa; pipe index:%d\n", BAM_ID(bam), pipe);

	bam_disable_pipe(&bam->base, pipe);

	return 0;
}
EXPORT_SYMBOL(sps_pipe_disable);

/*
 * Check pending descriptors in the descriptor FIFO
 * of a pipe
 */
int sps_pipe_pending_desc(unsigned long dev, u32 pipe, bool *pending)
{

	struct sps_bam *bam;

	if (!dev) {
		SPS_ERR(sps, "sps: BAM handle is NULL\n");
		return SPS_ERROR;
	}

	if (pipe >= BAM_MAX_PIPES) {
		SPS_ERR(sps, "sps: pipe index is invalid\n");
		return SPS_ERROR;
	}

	if (!pending) {
		SPS_ERR(sps, "sps: input flag is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_h2bam(dev);
	if (bam == NULL) {
		SPS_ERR(sps, "sps: :BAM is not found by handle\n");
		return SPS_ERROR;
	}

	SPS_DBG(bam, "sps: BAM: %pa; pipe index:%d\n", BAM_ID(bam), pipe);

	*pending = sps_bam_pipe_pending_desc(bam, pipe);

	return 0;
}
EXPORT_SYMBOL(sps_pipe_pending_desc);

/*
 * Process any pending IRQ of a BAM
 */
int sps_bam_process_irq(unsigned long dev)
{
	struct sps_bam *bam;
	int ret = 0;

	if (!dev) {
		SPS_ERR(sps, "sps: BAM handle is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_h2bam(dev);
	if (bam == NULL) {
		SPS_ERR(sps, "sps: BAM is not found by handle\n");
		return SPS_ERROR;
	}

	SPS_DBG1(bam, "sps: BAM: %pa\n", BAM_ID(bam));

	ret = sps_bam_check_irq(bam);

	return ret;
}
EXPORT_SYMBOL(sps_bam_process_irq);

/*
 * Enable all IRQs of a BAM
 */
int sps_bam_enable_irqs(unsigned long dev)
{
	struct sps_bam *bam;

	if (!dev) {
		SPS_ERR(sps, "sps: BAM handle is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_h2bam(dev);
	if (bam == NULL) {
		SPS_ERR(sps, "sps: BAM is not found by handle\n");
		return SPS_ERROR;
	}

	SPS_DBG1(bam, "sps: BAM: %pa\n", BAM_ID(bam));

	sps_bam_enable_all_irqs(bam);

	return 0;
}
EXPORT_SYMBOL(sps_bam_enable_irqs);

/*
 * Disable all IRQs of a BAM
 */
int sps_bam_disable_irqs(unsigned long dev)
{
	struct sps_bam *bam;

	if (!dev) {
		SPS_ERR(sps, "sps: BAM handle is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_h2bam(dev);
	if (bam == NULL) {
		SPS_ERR(sps, "sps: BAM is not found by handle\n");
		return SPS_ERROR;
	}

	SPS_DBG1(bam, "sps: BAM: %pa\n", BAM_ID(bam));

	sps_bam_disable_all_irqs(bam);

	return 0;
}
EXPORT_SYMBOL(sps_bam_disable_irqs);

/*
 * Get address info of a BAM
 */
int sps_get_bam_addr(unsigned long dev, phys_addr_t *base,
				u32 *size)
{
	struct sps_bam *bam;

	if (!dev) {
		SPS_ERR(sps, "sps: BAM handle is NULL\n");
		return SPS_ERROR;
	}

	bam = sps_h2bam(dev);
	if (bam == NULL) {
		SPS_ERR(sps, "sps: BAM is not found by handle\n");
		return SPS_ERROR;
	}

	*base = bam->props.phys_addr;
	*size = bam->props.virt_size;

	SPS_DBG2(bam, "sps: BAM: %pa; base:%pa; size:%d\n",
			BAM_ID(bam), base, *size);

	return 0;
}
EXPORT_SYMBOL(sps_get_bam_addr);

/*
 * Inject a ZLT with EOT for a BAM pipe
 */
int sps_pipe_inject_zlt(unsigned long dev, u32 pipe_index)
{
	struct sps_bam *bam;
	int rc;

	if (!dev) {
		SPS_ERR(sps, "sps: BAM handle is NULL\n");
		return SPS_ERROR;
	}

	if (pipe_index >= BAM_MAX_PIPES) {
		SPS_ERR(sps, "sps: pipe index is invalid\n");
		return SPS_ERROR;
	}

	bam = sps_h2bam(dev);
	if (bam == NULL) {
		SPS_ERR(sps, "sps: BAM is not found by handle\n");
		return SPS_ERROR;
	}

	SPS_DBG(bam, "sps: BAM: %pa; pipe index:%d\n", BAM_ID(bam), pipe_index);

	rc = sps_bam_pipe_inject_zlt(bam, pipe_index);
	if (rc)
		SPS_ERR(bam, "sps: failed to inject a ZLT\n");

	return rc;
}
EXPORT_SYMBOL(sps_pipe_inject_zlt);

/**
 * Allocate client state context
 *
 */
struct sps_pipe *sps_alloc_endpoint(void)
{
	struct sps_pipe *ctx = NULL;

	SPS_DBG(sps, "sps: Enter\n");

	ctx = kzalloc(sizeof(struct sps_pipe), GFP_KERNEL);
	if (ctx == NULL)
		return NULL;

	sps_client_init(ctx);

	return ctx;
}
EXPORT_SYMBOL(sps_alloc_endpoint);

/**
 * Free client state context
 *
 */
int sps_free_endpoint(struct sps_pipe *ctx)
{
	int res;

	SPS_DBG(sps, "sps: Enter\n");

	if (ctx == NULL) {
		SPS_ERR(sps, "sps: pipe is NULL\n");
		return SPS_ERROR;
	}

	res = sps_client_de_init(ctx);

	if (res == 0)
		kfree(ctx);

	return res;
}
EXPORT_SYMBOL(sps_free_endpoint);

/**
 * Platform Driver.
 */
static int get_platform_data(struct platform_device *pdev)
{
	struct resource *resource;
	struct msm_sps_platform_data *pdata;

	SPS_DBG3(sps, "sps: Enter\n");

	pdata = pdev->dev.platform_data;

	if (pdata == NULL) {
		SPS_ERR(sps, "sps: invalid platform data\n");
		sps->bamdma_restricted_pipes = 0;
		return -EINVAL;
	}
	sps->bamdma_restricted_pipes = pdata->bamdma_restricted_pipes;
	SPS_DBG3(sps, "sps:bamdma_restricted_pipes=0x%x\n",
			sps->bamdma_restricted_pipes);

	resource  = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						 "pipe_mem");
	if (resource) {
		sps->pipemem_phys_base = resource->start;
		sps->pipemem_size = resource_size(resource);
		SPS_DBG3(sps, "sps:pipemem.base=%pa,size=0x%x\n",
			&sps->pipemem_phys_base,
			sps->pipemem_size);
	}

#ifdef CONFIG_SPS_SUPPORT_BAMDMA
	resource  = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						 "bamdma_bam");
	if (resource) {
		sps->bamdma_bam_phys_base = resource->start;
		sps->bamdma_bam_size = resource_size(resource);
		SPS_DBG(sps, "sps:bamdma_bam.base=%pa,size=0x%x\n",
			&sps->bamdma_bam_phys_base,
			sps->bamdma_bam_size);
	}

	resource  = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						 "bamdma_dma");
	if (resource) {
		sps->bamdma_dma_phys_base = resource->start;
		sps->bamdma_dma_size = resource_size(resource);
		SPS_DBG(sps, "sps:bamdma_dma.base=%pa,size=0x%x\n",
			&sps->bamdma_dma_phys_base,
			sps->bamdma_dma_size);
	}

	resource  = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
						 "bamdma_irq");
	if (resource) {
		sps->bamdma_irq = resource->start;
		SPS_DBG(sps, "sps:bamdma_irq=%d\n", sps->bamdma_irq);
	}
#endif

	return 0;
}

/**
 * Read data from device tree
 */
static int get_device_tree_data(struct platform_device *pdev)
{
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
	struct resource *resource;

	SPS_DBG(sps, "sps: Enter\n");

	if (of_property_read_u32((&pdev->dev)->of_node,
				"qcom,bam-dma-res-pipes",
				&sps->bamdma_restricted_pipes))
		SPS_DBG(sps,
			"sps: No restricted bamdma pipes on this target\n");
	else
		SPS_DBG(sps, "sps:bamdma_restricted_pipes=0x%x\n",
			sps->bamdma_restricted_pipes);

	resource  = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (resource) {
		sps->bamdma_bam_phys_base = resource->start;
		sps->bamdma_bam_size = resource_size(resource);
		SPS_DBG(sps, "sps:bamdma_bam.base=%pa,size=0x%x\n",
			&sps->bamdma_bam_phys_base,
			sps->bamdma_bam_size);
	} else {
		SPS_ERR(sps, "sps: BAM DMA BAM mem unavailable\n");
		return -ENODEV;
	}

	resource  = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (resource) {
		sps->bamdma_dma_phys_base = resource->start;
		sps->bamdma_dma_size = resource_size(resource);
		SPS_DBG(sps, "sps:bamdma_dma.base=%pa,size=0x%x\n",
			&sps->bamdma_dma_phys_base,
			sps->bamdma_dma_size);
	} else {
		SPS_ERR(sps, "sps: BAM DMA mem unavailable\n");
		return -ENODEV;
	}

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (resource) {
		imem = true;
		sps->pipemem_phys_base = resource->start;
		sps->pipemem_size = resource_size(resource);
		SPS_DBG(sps, "sps:pipemem.base=%pa,size=0x%x\n",
			&sps->pipemem_phys_base,
			sps->pipemem_size);
	} else {
		imem = false;
		SPS_DBG(sps, "sps: No pipe memory on this target\n");
	}

	resource  = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (resource) {
		sps->bamdma_irq = resource->start;
		SPS_DBG(sps, "sps:bamdma_irq=%d\n", sps->bamdma_irq);
	} else {
		SPS_ERR(sps, "sps: BAM DMA IRQ unavailable\n");
		return -ENODEV;
	}
#endif

	if (of_property_read_u32((&pdev->dev)->of_node,
				"qcom,device-type",
				&d_type)) {
		d_type = 3;
		SPS_DBG3(sps, "sps:default device type %d\n", d_type);
	} else
		SPS_DBG3(sps, "sps:device type is %d\n", d_type);

	enhd_pipe = of_property_read_bool((&pdev->dev)->of_node,
			"qcom,pipe-attr-ee");
	SPS_DBG3(sps, "sps:PIPE_ATTR_EE is %s supported\n",
			(enhd_pipe ? "" : "not"));

	return 0;
}

static const struct of_device_id msm_sps_match[] = {
	{	.compatible = "qcom,msm-sps",
		.data = &bam_types[SPS_BAM_NDP]
	},
	{	.compatible = "qcom,msm-sps-4k",
		.data = &bam_types[SPS_BAM_NDP_4K]
	},
	{},
};

static int msm_sps_probe(struct platform_device *pdev)
{
	int ret = -ENODEV;

	SPS_DBG3(sps, "sps: Enter\n");

	if (pdev->dev.of_node) {
		const struct of_device_id *match;

		if (get_device_tree_data(pdev)) {
			SPS_ERR(sps,
				"sps: Fail to get data from device tree\n");
			return -ENODEV;
		}
		SPS_DBG(sps, "%s", "sps:get data from device tree\n");

		match = of_match_device(msm_sps_match, &pdev->dev);
		if (match) {
			bam_type = *((enum sps_bam_type *)(match->data));
			SPS_DBG3(sps, "sps:BAM type is:%d\n", bam_type);
		} else {
			bam_type = SPS_BAM_NDP;
			SPS_DBG3(sps, "sps:use default BAM type:%d\n",
				bam_type);
		}
	} else {
		d_type = 0;
		if (get_platform_data(pdev)) {
			SPS_ERR(sps, "sps: :Fail to get platform data\n");
			return -ENODEV;
		}
		SPS_DBG(sps, "%s", "sps:get platform data\n");
		bam_type = SPS_BAM_LEGACY;
	}

	/* Create Device */
	sps->dev_class = class_create(THIS_MODULE, SPS_DRV_NAME);

	ret = alloc_chrdev_region(&sps->dev_num, 0, 1, SPS_DRV_NAME);
	if (ret) {
		SPS_ERR(sps, "sps: alloc_chrdev_region err\n");
		goto alloc_chrdev_region_err;
	}

	sps->dev = device_create(sps->dev_class, NULL, sps->dev_num, sps,
				SPS_DRV_NAME);
	if (IS_ERR(sps->dev)) {
		SPS_ERR(sps, "sps: device_create err\n");
		goto device_create_err;
	}

	if (pdev->dev.of_node)
		sps->dev->of_node = pdev->dev.of_node;

	if (!d_type) {
		sps->pmem_clk = clk_get(sps->dev, "mem_clk");
		if (IS_ERR(sps->pmem_clk)) {
			if (PTR_ERR(sps->pmem_clk) == -EPROBE_DEFER)
				ret = -EPROBE_DEFER;
			else
				SPS_ERR(sps, "sps: fail to get pmem_clk\n");
			goto pmem_clk_err;
		} else {
			ret = clk_prepare_enable(sps->pmem_clk);
			if (ret) {
				SPS_ERR(sps,
					"sps: failed to enable pmem_clk\n");
				goto pmem_clk_en_err;
			}
		}
	}

#ifdef CONFIG_SPS_SUPPORT_BAMDMA
	sps->dfab_clk = clk_get(sps->dev, "dfab_clk");
	if (IS_ERR(sps->dfab_clk)) {
		if (PTR_ERR(sps->dfab_clk) == -EPROBE_DEFER)
			ret = -EPROBE_DEFER;
		else
			SPS_ERR(sps, "sps: fail to get dfab_clk\n");
		goto dfab_clk_err;
	} else {
		ret = clk_set_rate(sps->dfab_clk, 64000000);
		if (ret) {
			SPS_ERR(sps, "sps: failed to set dfab_clk rate\n");
			clk_put(sps->dfab_clk);
			goto dfab_clk_err;
		}
	}

	sps->bamdma_clk = clk_get(sps->dev, "dma_bam_pclk");
	if (IS_ERR(sps->bamdma_clk)) {
		if (PTR_ERR(sps->bamdma_clk) == -EPROBE_DEFER)
			ret = -EPROBE_DEFER;
		else
			SPS_ERR(sps, "sps: fail to get bamdma_clk\n");
		clk_put(sps->dfab_clk);
		goto dfab_clk_err;
	} else {
		ret = clk_prepare_enable(sps->bamdma_clk);
		if (ret) {
			SPS_ERR(sps, "sps:failed to enable bamdma_clk ret=%d\n",
									ret);
			clk_put(sps->bamdma_clk);
			clk_put(sps->dfab_clk);
			goto dfab_clk_err;
		}
	}

	ret = clk_prepare_enable(sps->dfab_clk);
	if (ret) {
		SPS_ERR(sps, "sps:failed to enable dfab_clk ret=%d\n", ret);
		clk_disable_unprepare(sps->bamdma_clk);
		clk_put(sps->bamdma_clk);
		clk_put(sps->dfab_clk);
		goto dfab_clk_err;
	}
#endif
	ret = sps_device_init();
	if (ret) {
		SPS_ERR(sps, "sps: sps_device_init err\n");

#ifdef CONFIG_SPS_SUPPORT_BAMDMA
		clk_disable_unprepare(sps->dfab_clk);
		clk_disable_unprepare(sps->bamdma_clk);
		clk_put(sps->bamdma_clk);
		clk_put(sps->dfab_clk);
#endif
		goto dfab_clk_err;
	}
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
	clk_disable_unprepare(sps->dfab_clk);
	clk_disable_unprepare(sps->bamdma_clk);
#endif
	sps->is_ready = true;

	SPS_INFO(sps, "%s", "sps:sps is ready\n");

	return 0;
dfab_clk_err:
	if (!d_type)
		clk_disable_unprepare(sps->pmem_clk);
pmem_clk_en_err:
	if (!d_type)
		clk_put(sps->pmem_clk);
pmem_clk_err:
	device_destroy(sps->dev_class, sps->dev_num);
device_create_err:
	unregister_chrdev_region(sps->dev_num, 1);
alloc_chrdev_region_err:
	class_destroy(sps->dev_class);

	return ret;
}

static int msm_sps_remove(struct platform_device *pdev)
{
	SPS_DBG3(sps, "sps: Enter\n");

	device_destroy(sps->dev_class, sps->dev_num);
	unregister_chrdev_region(sps->dev_num, 1);
	class_destroy(sps->dev_class);
	sps_device_de_init();

	clk_put(sps->dfab_clk);
	if (!d_type)
		clk_put(sps->pmem_clk);
	clk_put(sps->bamdma_clk);

	return 0;
}

static struct platform_driver msm_sps_driver = {
	.probe          = msm_sps_probe,
	.driver		= {
		.name	= SPS_DRV_NAME,
		.of_match_table = msm_sps_match,
		.suppress_bind_attrs = true,
	},
	.remove		= msm_sps_remove,
};

/**
 * Module Init.
 */
static int __init sps_init(void)
{
	int ret;

#ifdef CONFIG_DEBUG_FS
	sps_debugfs_init();
#endif

	pr_debug("sps:%s\n", __func__);

	/* Allocate the SPS driver state struct */
	sps = kzalloc(sizeof(*sps), GFP_KERNEL);
	if (sps == NULL)
		return -ENOMEM;

	sps->ipc_log0 = ipc_log_context_create(SPS_IPC_LOGPAGES,
							"sps_ipc_log0", 0);
	if (!sps->ipc_log0)
		pr_err("Failed to create IPC log0\n");
	sps->ipc_log1 = ipc_log_context_create(SPS_IPC_LOGPAGES,
							"sps_ipc_log1", 0);
	if (!sps->ipc_log1)
		pr_err("Failed to create IPC log1\n");
	sps->ipc_log2 = ipc_log_context_create(SPS_IPC_LOGPAGES,
							"sps_ipc_log2", 0);
	if (!sps->ipc_log2)
		pr_err("Failed to create IPC log2\n");
	sps->ipc_log3 = ipc_log_context_create(SPS_IPC_LOGPAGES,
							"sps_ipc_log3", 0);
	if (!sps->ipc_log3)
		pr_err("Failed to create IPC log3\n");
	sps->ipc_log4 = ipc_log_context_create(SPS_IPC_LOGPAGES *
				SPS_IPC_REG_DUMP_FACTOR, "sps_ipc_log4", 0);
	if (!sps->ipc_log4)
		pr_err("Failed to create IPC log4\n");

	ret = platform_driver_register(&msm_sps_driver);

	return ret;
}

/**
 * Module Exit.
 */
static void __exit sps_exit(void)
{
	pr_debug("sps:%s\n", __func__);

	platform_driver_unregister(&msm_sps_driver);

	kfree(sps);
	sps = NULL;

#ifdef CONFIG_DEBUG_FS
	sps_debugfs_exit();
#endif
}

arch_initcall(sps_init);
module_exit(sps_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Smart Peripheral Switch (SPS)");
