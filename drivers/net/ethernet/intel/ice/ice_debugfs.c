// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, Intel Corporation. */

#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/random.h>
#include <linux/vmalloc.h>
#include "ice.h"

static struct dentry *ice_debugfs_root;

/* create a define that has an extra module that doesn't really exist. this
 * is so we can add a module 'all' to easily enable/disable all the modules
 */
#define ICE_NR_FW_LOG_MODULES (ICE_AQC_FW_LOG_ID_MAX + 1)

/* the ordering in this array is important. it matches the ordering of the
 * values in the FW so the index is the same value as in ice_aqc_fw_logging_mod
 */
static const char * const ice_fwlog_module_string[] = {
	"general",
	"ctrl",
	"link",
	"link_topo",
	"dnl",
	"i2c",
	"sdp",
	"mdio",
	"adminq",
	"hdma",
	"lldp",
	"dcbx",
	"dcb",
	"xlr",
	"nvm",
	"auth",
	"vpd",
	"iosf",
	"parser",
	"sw",
	"scheduler",
	"txq",
	"rsvd",
	"post",
	"watchdog",
	"task_dispatch",
	"mng",
	"synce",
	"health",
	"tsdrv",
	"pfreg",
	"mdlver",
	"all",
};

/* the ordering in this array is important. it matches the ordering of the
 * values in the FW so the index is the same value as in ice_fwlog_level
 */
static const char * const ice_fwlog_level_string[] = {
	"none",
	"error",
	"warning",
	"normal",
	"verbose",
};

/* the order in this array is important. it matches the ordering of the
 * values in the FW so the index is the same value as in ice_fwlog_level
 */
static const char * const ice_fwlog_log_size[] = {
	"128K",
	"256K",
	"512K",
	"1M",
	"2M",
};

/**
 * ice_fwlog_print_module_cfg - print current FW logging module configuration
 * @hw: pointer to the HW structure
 * @module: module to print
 * @s: the seq file to put data into
 */
static void
ice_fwlog_print_module_cfg(struct ice_hw *hw, int module, struct seq_file *s)
{
	struct ice_fwlog_cfg *cfg = &hw->fwlog_cfg;
	struct ice_fwlog_module_entry *entry;

	if (module != ICE_AQC_FW_LOG_ID_MAX) {
		entry =	&cfg->module_entries[module];

		seq_printf(s, "\tModule: %s, Log Level: %s\n",
			   ice_fwlog_module_string[entry->module_id],
			   ice_fwlog_level_string[entry->log_level]);
	} else {
		int i;

		for (i = 0; i < ICE_AQC_FW_LOG_ID_MAX; i++) {
			entry =	&cfg->module_entries[i];

			seq_printf(s, "\tModule: %s, Log Level: %s\n",
				   ice_fwlog_module_string[entry->module_id],
				   ice_fwlog_level_string[entry->log_level]);
		}
	}
}

static int ice_find_module_by_dentry(struct ice_pf *pf, struct dentry *d)
{
	int i, module;

	module = -1;
	/* find the module based on the dentry */
	for (i = 0; i < ICE_NR_FW_LOG_MODULES; i++) {
		if (d == pf->ice_debugfs_pf_fwlog_modules[i]) {
			module = i;
			break;
		}
	}

	return module;
}

/**
 * ice_debugfs_module_show - read from 'module' file
 * @s: the opened file
 * @v: pointer to the offset
 */
static int ice_debugfs_module_show(struct seq_file *s, void *v)
{
	const struct file *filp = s->file;
	struct dentry *dentry;
	struct ice_pf *pf;
	int module;

	dentry = file_dentry(filp);
	pf = s->private;

	module = ice_find_module_by_dentry(pf, dentry);
	if (module < 0) {
		dev_info(ice_pf_to_dev(pf), "unknown module\n");
		return -EINVAL;
	}

	ice_fwlog_print_module_cfg(&pf->hw, module, s);

	return 0;
}

static int ice_debugfs_module_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, ice_debugfs_module_show, inode->i_private);
}

/**
 * ice_debugfs_module_write - write into 'module' file
 * @filp: the opened file
 * @buf: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 */
static ssize_t
ice_debugfs_module_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct ice_pf *pf = file_inode(filp)->i_private;
	struct dentry *dentry = file_dentry(filp);
	struct device *dev = ice_pf_to_dev(pf);
	char user_val[16], *cmd_buf;
	int module, log_level, cnt;

	/* don't allow partial writes or invalid input */
	if (*ppos != 0 || count > 8)
		return -EINVAL;

	cmd_buf = memdup_user(buf, count);
	if (IS_ERR(cmd_buf))
		return PTR_ERR(cmd_buf);

	module = ice_find_module_by_dentry(pf, dentry);
	if (module < 0) {
		dev_info(dev, "unknown module\n");
		return -EINVAL;
	}

	cnt = sscanf(cmd_buf, "%s", user_val);
	if (cnt != 1)
		return -EINVAL;

	log_level = sysfs_match_string(ice_fwlog_level_string, user_val);
	if (log_level < 0) {
		dev_info(dev, "unknown log level '%s'\n", user_val);
		return -EINVAL;
	}

	if (module != ICE_AQC_FW_LOG_ID_MAX) {
		ice_pf_fwlog_update_module(pf, log_level, module);
	} else {
		/* the module 'all' is a shortcut so that we can set
		 * all of the modules to the same level quickly
		 */
		int i;

		for (i = 0; i < ICE_AQC_FW_LOG_ID_MAX; i++)
			ice_pf_fwlog_update_module(pf, log_level, i);
	}

	return count;
}

static const struct file_operations ice_debugfs_module_fops = {
	.owner = THIS_MODULE,
	.open  = ice_debugfs_module_open,
	.read = seq_read,
	.release = single_release,
	.write = ice_debugfs_module_write,
};

/**
 * ice_debugfs_nr_messages_read - read from 'nr_messages' file
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 */
static ssize_t ice_debugfs_nr_messages_read(struct file *filp,
					    char __user *buffer, size_t count,
					    loff_t *ppos)
{
	struct ice_pf *pf = filp->private_data;
	struct ice_hw *hw = &pf->hw;
	char buff[32] = {};

	snprintf(buff, sizeof(buff), "%d\n",
		 hw->fwlog_cfg.log_resolution);

	return simple_read_from_buffer(buffer, count, ppos, buff, strlen(buff));
}

/**
 * ice_debugfs_nr_messages_write - write into 'nr_messages' file
 * @filp: the opened file
 * @buf: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 */
static ssize_t
ice_debugfs_nr_messages_write(struct file *filp, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct ice_pf *pf = filp->private_data;
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	char user_val[8], *cmd_buf;
	s16 nr_messages;
	ssize_t ret;

	/* don't allow partial writes or invalid input */
	if (*ppos != 0 || count > 4)
		return -EINVAL;

	cmd_buf = memdup_user(buf, count);
	if (IS_ERR(cmd_buf))
		return PTR_ERR(cmd_buf);

	ret = sscanf(cmd_buf, "%s", user_val);
	if (ret != 1)
		return -EINVAL;

	ret = kstrtos16(user_val, 0, &nr_messages);
	if (ret)
		return ret;

	if (nr_messages < ICE_AQC_FW_LOG_MIN_RESOLUTION ||
	    nr_messages > ICE_AQC_FW_LOG_MAX_RESOLUTION) {
		dev_err(dev, "Invalid FW log number of messages %d, value must be between %d - %d\n",
			nr_messages, ICE_AQC_FW_LOG_MIN_RESOLUTION,
			ICE_AQC_FW_LOG_MAX_RESOLUTION);
		return -EINVAL;
	}

	hw->fwlog_cfg.log_resolution = nr_messages;

	return count;
}

static const struct file_operations ice_debugfs_nr_messages_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read = ice_debugfs_nr_messages_read,
	.write = ice_debugfs_nr_messages_write,
};

/**
 * ice_debugfs_enable_read - read from 'enable' file
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 */
static ssize_t ice_debugfs_enable_read(struct file *filp,
				       char __user *buffer, size_t count,
				       loff_t *ppos)
{
	struct ice_pf *pf = filp->private_data;
	struct ice_hw *hw = &pf->hw;
	char buff[32] = {};

	snprintf(buff, sizeof(buff), "%u\n",
		 (u16)(hw->fwlog_cfg.options &
		 ICE_FWLOG_OPTION_IS_REGISTERED) >> 3);

	return simple_read_from_buffer(buffer, count, ppos, buff, strlen(buff));
}

/**
 * ice_debugfs_enable_write - write into 'enable' file
 * @filp: the opened file
 * @buf: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 */
static ssize_t
ice_debugfs_enable_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct ice_pf *pf = filp->private_data;
	struct ice_hw *hw = &pf->hw;
	char user_val[8], *cmd_buf;
	bool enable;
	ssize_t ret;

	/* don't allow partial writes or invalid input */
	if (*ppos != 0 || count > 2)
		return -EINVAL;

	cmd_buf = memdup_user(buf, count);
	if (IS_ERR(cmd_buf))
		return PTR_ERR(cmd_buf);

	ret = sscanf(cmd_buf, "%s", user_val);
	if (ret != 1)
		return -EINVAL;

	ret = kstrtobool(user_val, &enable);
	if (ret)
		goto enable_write_error;

	if (enable)
		hw->fwlog_cfg.options |= ICE_FWLOG_OPTION_ARQ_ENA;
	else
		hw->fwlog_cfg.options &= ~ICE_FWLOG_OPTION_ARQ_ENA;

	ret = ice_fwlog_set(hw, &hw->fwlog_cfg);
	if (ret)
		goto enable_write_error;

	if (enable)
		ret = ice_fwlog_register(hw);
	else
		ret = ice_fwlog_unregister(hw);

	if (ret)
		goto enable_write_error;

	/* if we get here, nothing went wrong; return count since we didn't
	 * really write anything
	 */
	ret = (ssize_t)count;

enable_write_error:
	/* This function always consumes all of the written input, or produces
	 * an error. Check and enforce this. Otherwise, the write operation
	 * won't complete properly.
	 */
	if (WARN_ON(ret != (ssize_t)count && ret >= 0))
		ret = -EIO;

	return ret;
}

static const struct file_operations ice_debugfs_enable_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read = ice_debugfs_enable_read,
	.write = ice_debugfs_enable_write,
};

/**
 * ice_debugfs_log_size_read - read from 'log_size' file
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 */
static ssize_t ice_debugfs_log_size_read(struct file *filp,
					 char __user *buffer, size_t count,
					 loff_t *ppos)
{
	struct ice_pf *pf = filp->private_data;
	struct ice_hw *hw = &pf->hw;
	char buff[32] = {};
	int index;

	index = hw->fwlog_ring.index;
	snprintf(buff, sizeof(buff), "%s\n", ice_fwlog_log_size[index]);

	return simple_read_from_buffer(buffer, count, ppos, buff, strlen(buff));
}

/**
 * ice_debugfs_log_size_write - write into 'log_size' file
 * @filp: the opened file
 * @buf: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 */
static ssize_t
ice_debugfs_log_size_write(struct file *filp, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct ice_pf *pf = filp->private_data;
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	char user_val[8], *cmd_buf;
	ssize_t ret;
	int index;

	/* don't allow partial writes or invalid input */
	if (*ppos != 0 || count > 5)
		return -EINVAL;

	cmd_buf = memdup_user(buf, count);
	if (IS_ERR(cmd_buf))
		return PTR_ERR(cmd_buf);

	ret = sscanf(cmd_buf, "%s", user_val);
	if (ret != 1)
		return -EINVAL;

	index = sysfs_match_string(ice_fwlog_log_size, user_val);
	if (index < 0) {
		dev_info(dev, "Invalid log size '%s'. The value must be one of 128K, 256K, 512K, 1M, 2M\n",
			 user_val);
		ret = -EINVAL;
		goto log_size_write_error;
	} else if (hw->fwlog_cfg.options & ICE_FWLOG_OPTION_IS_REGISTERED) {
		dev_info(dev, "FW logging is currently running. Please disable FW logging to change log_size\n");
		ret = -EINVAL;
		goto log_size_write_error;
	}

	/* free all the buffers and the tracking info and resize */
	ice_fwlog_realloc_rings(hw, index);

	/* if we get here, nothing went wrong; return count since we didn't
	 * really write anything
	 */
	ret = (ssize_t)count;

log_size_write_error:
	/* This function always consumes all of the written input, or produces
	 * an error. Check and enforce this. Otherwise, the write operation
	 * won't complete properly.
	 */
	if (WARN_ON(ret != (ssize_t)count && ret >= 0))
		ret = -EIO;

	return ret;
}

static const struct file_operations ice_debugfs_log_size_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read = ice_debugfs_log_size_read,
	.write = ice_debugfs_log_size_write,
};

/**
 * ice_debugfs_data_read - read from 'data' file
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 */
static ssize_t ice_debugfs_data_read(struct file *filp, char __user *buffer,
				     size_t count, loff_t *ppos)
{
	struct ice_pf *pf = filp->private_data;
	struct ice_hw *hw = &pf->hw;
	int data_copied = 0;
	bool done = false;

	if (ice_fwlog_ring_empty(&hw->fwlog_ring))
		return 0;

	while (!ice_fwlog_ring_empty(&hw->fwlog_ring) && !done) {
		struct ice_fwlog_data *log;
		u16 cur_buf_len;

		log = &hw->fwlog_ring.rings[hw->fwlog_ring.head];
		cur_buf_len = log->data_size;
		if (cur_buf_len >= count) {
			done = true;
			continue;
		}

		if (copy_to_user(buffer, log->data, cur_buf_len)) {
			/* if there is an error then bail and return whatever
			 * the driver has copied so far
			 */
			done = true;
			continue;
		}

		data_copied += cur_buf_len;
		buffer += cur_buf_len;
		count -= cur_buf_len;
		*ppos += cur_buf_len;
		ice_fwlog_ring_increment(&hw->fwlog_ring.head,
					 hw->fwlog_ring.size);
	}

	return data_copied;
}

/**
 * ice_debugfs_data_write - write into 'data' file
 * @filp: the opened file
 * @buf: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 */
static ssize_t
ice_debugfs_data_write(struct file *filp, const char __user *buf, size_t count,
		       loff_t *ppos)
{
	struct ice_pf *pf = filp->private_data;
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	ssize_t ret;

	/* don't allow partial writes */
	if (*ppos != 0)
		return 0;

	/* any value is allowed to clear the buffer so no need to even look at
	 * what the value is
	 */
	if (!(hw->fwlog_cfg.options & ICE_FWLOG_OPTION_IS_REGISTERED)) {
		hw->fwlog_ring.head = 0;
		hw->fwlog_ring.tail = 0;
	} else {
		dev_info(dev, "Can't clear FW log data while FW log running\n");
		ret = -EINVAL;
		goto nr_buffs_write_error;
	}

	/* if we get here, nothing went wrong; return count since we didn't
	 * really write anything
	 */
	ret = (ssize_t)count;

nr_buffs_write_error:
	/* This function always consumes all of the written input, or produces
	 * an error. Check and enforce this. Otherwise, the write operation
	 * won't complete properly.
	 */
	if (WARN_ON(ret != (ssize_t)count && ret >= 0))
		ret = -EIO;

	return ret;
}

static const struct file_operations ice_debugfs_data_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read = ice_debugfs_data_read,
	.write = ice_debugfs_data_write,
};

/**
 * ice_debugfs_fwlog_init - setup the debugfs directory
 * @pf: the ice that is starting up
 */
void ice_debugfs_fwlog_init(struct ice_pf *pf)
{
	const char *name = pci_name(pf->pdev);
	struct dentry *fw_modules_dir;
	struct dentry **fw_modules;
	int i;

	/* only support fw log commands on PF 0 */
	if (pf->hw.bus.func)
		return;

	/* allocate space for this first because if it fails then we don't
	 * need to unwind
	 */
	fw_modules = kcalloc(ICE_NR_FW_LOG_MODULES, sizeof(*fw_modules),
			     GFP_KERNEL);
	if (!fw_modules)
		return;

	pf->ice_debugfs_pf = debugfs_create_dir(name, ice_debugfs_root);
	if (IS_ERR(pf->ice_debugfs_pf))
		goto err_create_module_files;

	pf->ice_debugfs_pf_fwlog = debugfs_create_dir("fwlog",
						      pf->ice_debugfs_pf);
	if (IS_ERR(pf->ice_debugfs_pf))
		goto err_create_module_files;

	fw_modules_dir = debugfs_create_dir("modules",
					    pf->ice_debugfs_pf_fwlog);
	if (IS_ERR(fw_modules_dir))
		goto err_create_module_files;

	for (i = 0; i < ICE_NR_FW_LOG_MODULES; i++) {
		fw_modules[i] = debugfs_create_file(ice_fwlog_module_string[i],
						    0600, fw_modules_dir, pf,
						    &ice_debugfs_module_fops);
		if (IS_ERR(fw_modules[i]))
			goto err_create_module_files;
	}

	debugfs_create_file("nr_messages", 0600,
			    pf->ice_debugfs_pf_fwlog, pf,
			    &ice_debugfs_nr_messages_fops);

	pf->ice_debugfs_pf_fwlog_modules = fw_modules;

	debugfs_create_file("enable", 0600, pf->ice_debugfs_pf_fwlog,
			    pf, &ice_debugfs_enable_fops);

	debugfs_create_file("log_size", 0600, pf->ice_debugfs_pf_fwlog,
			    pf, &ice_debugfs_log_size_fops);

	debugfs_create_file("data", 0600, pf->ice_debugfs_pf_fwlog,
			    pf, &ice_debugfs_data_fops);

	return;

err_create_module_files:
	debugfs_remove_recursive(pf->ice_debugfs_pf_fwlog);
	kfree(fw_modules);
}

/**
 * ice_debugfs_init - create root directory for debugfs entries
 */
void ice_debugfs_init(void)
{
	ice_debugfs_root = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (IS_ERR(ice_debugfs_root))
		pr_info("init of debugfs failed\n");
}

/**
 * ice_debugfs_exit - remove debugfs entries
 */
void ice_debugfs_exit(void)
{
	debugfs_remove_recursive(ice_debugfs_root);
	ice_debugfs_root = NULL;
}
