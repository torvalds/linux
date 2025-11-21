// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, Intel Corporation. */

#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/net/intel/libie/fwlog.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/vmalloc.h>

#define DEFAULT_SYMBOL_NAMESPACE	"LIBIE_FWLOG"

/* create a define that has an extra module that doesn't really exist. this
 * is so we can add a module 'all' to easily enable/disable all the modules
 */
#define LIBIE_NR_FW_LOG_MODULES (LIBIE_AQC_FW_LOG_ID_MAX + 1)

/* the ordering in this array is important. it matches the ordering of the
 * values in the FW so the index is the same value as in
 * libie_aqc_fw_logging_mod
 */
static const char * const libie_fwlog_module_string[] = {
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
 * values in the FW so the index is the same value as in libie_fwlog_level
 */
static const char * const libie_fwlog_level_string[] = {
	"none",
	"error",
	"warning",
	"normal",
	"verbose",
};

static const char * const libie_fwlog_log_size[] = {
	"128K",
	"256K",
	"512K",
	"1M",
	"2M",
};

static bool libie_fwlog_ring_empty(struct libie_fwlog_ring *rings)
{
	return rings->head == rings->tail;
}

static void libie_fwlog_ring_increment(u16 *item, u16 size)
{
	*item = (*item + 1) & (size - 1);
}

static int libie_fwlog_alloc_ring_buffs(struct libie_fwlog_ring *rings)
{
	int i, nr_bytes;
	u8 *mem;

	nr_bytes = rings->size * LIBIE_AQ_MAX_BUF_LEN;
	mem = vzalloc(nr_bytes);
	if (!mem)
		return -ENOMEM;

	for (i = 0; i < rings->size; i++) {
		struct libie_fwlog_data *ring = &rings->rings[i];

		ring->data_size = LIBIE_AQ_MAX_BUF_LEN;
		ring->data = mem;
		mem += LIBIE_AQ_MAX_BUF_LEN;
	}

	return 0;
}

static void libie_fwlog_free_ring_buffs(struct libie_fwlog_ring *rings)
{
	int i;

	for (i = 0; i < rings->size; i++) {
		struct libie_fwlog_data *ring = &rings->rings[i];

		/* the first ring is the base memory for the whole range so
		 * free it
		 */
		if (!i)
			vfree(ring->data);

		ring->data = NULL;
		ring->data_size = 0;
	}
}

#define LIBIE_FWLOG_INDEX_TO_BYTES(n) ((128 * 1024) << (n))
/**
 * libie_fwlog_realloc_rings - reallocate the FW log rings
 * @fwlog: pointer to the fwlog structure
 * @index: the new index to use to allocate memory for the log data
 *
 */
static void libie_fwlog_realloc_rings(struct libie_fwlog *fwlog, int index)
{
	struct libie_fwlog_ring ring;
	int status, ring_size;

	/* convert the number of bytes into a number of 4K buffers. externally
	 * the driver presents the interface to the FW log data as a number of
	 * bytes because that's easy for users to understand. internally the
	 * driver uses a ring of buffers because the driver doesn't know where
	 * the beginning and end of any line of log data is so the driver has
	 * to overwrite data as complete blocks. when the data is returned to
	 * the user the driver knows that the data is correct and the FW log
	 * can be correctly parsed by the tools
	 */
	ring_size = LIBIE_FWLOG_INDEX_TO_BYTES(index) / LIBIE_AQ_MAX_BUF_LEN;
	if (ring_size == fwlog->ring.size)
		return;

	/* allocate space for the new rings and buffers then release the
	 * old rings and buffers. that way if we don't have enough
	 * memory then we at least have what we had before
	 */
	ring.rings = kcalloc(ring_size, sizeof(*ring.rings), GFP_KERNEL);
	if (!ring.rings)
		return;

	ring.size = ring_size;

	status = libie_fwlog_alloc_ring_buffs(&ring);
	if (status) {
		dev_warn(&fwlog->pdev->dev, "Unable to allocate memory for FW log ring data buffers\n");
		libie_fwlog_free_ring_buffs(&ring);
		kfree(ring.rings);
		return;
	}

	libie_fwlog_free_ring_buffs(&fwlog->ring);
	kfree(fwlog->ring.rings);

	fwlog->ring.rings = ring.rings;
	fwlog->ring.size = ring.size;
	fwlog->ring.index = index;
	fwlog->ring.head = 0;
	fwlog->ring.tail = 0;
}

/**
 * libie_fwlog_supported - Cached for whether FW supports FW logging or not
 * @fwlog: pointer to the fwlog structure
 *
 * This will always return false if called before libie_init_hw(), so it must be
 * called after libie_init_hw().
 */
static bool libie_fwlog_supported(struct libie_fwlog *fwlog)
{
	return fwlog->supported;
}

/**
 * libie_aq_fwlog_set - Set FW logging configuration AQ command (0xFF30)
 * @fwlog: pointer to the fwlog structure
 * @entries: entries to configure
 * @num_entries: number of @entries
 * @options: options from libie_fwlog_cfg->options structure
 * @log_resolution: logging resolution
 */
static int
libie_aq_fwlog_set(struct libie_fwlog *fwlog,
		   struct libie_fwlog_module_entry *entries, u16 num_entries,
		   u16 options, u16 log_resolution)
{
	struct libie_aqc_fw_log_cfg_resp *fw_modules;
	struct libie_aq_desc desc = {0};
	struct libie_aqc_fw_log *cmd;
	int status;
	int i;

	fw_modules = kcalloc(num_entries, sizeof(*fw_modules), GFP_KERNEL);
	if (!fw_modules)
		return -ENOMEM;

	for (i = 0; i < num_entries; i++) {
		fw_modules[i].module_identifier =
			cpu_to_le16(entries[i].module_id);
		fw_modules[i].log_level = entries[i].log_level;
	}

	desc.opcode = cpu_to_le16(libie_aqc_opc_fw_logs_config);
	desc.flags = cpu_to_le16(LIBIE_AQ_FLAG_SI) |
		     cpu_to_le16(LIBIE_AQ_FLAG_RD);

	cmd = libie_aq_raw(&desc);

	cmd->cmd_flags = LIBIE_AQC_FW_LOG_CONF_SET_VALID;
	cmd->ops.cfg.log_resolution = cpu_to_le16(log_resolution);
	cmd->ops.cfg.mdl_cnt = cpu_to_le16(num_entries);

	if (options & LIBIE_FWLOG_OPTION_ARQ_ENA)
		cmd->cmd_flags |= LIBIE_AQC_FW_LOG_CONF_AQ_EN;
	if (options & LIBIE_FWLOG_OPTION_UART_ENA)
		cmd->cmd_flags |= LIBIE_AQC_FW_LOG_CONF_UART_EN;

	status = fwlog->send_cmd(fwlog->priv, &desc, fw_modules,
				 sizeof(*fw_modules) * num_entries);

	kfree(fw_modules);

	return status;
}

/**
 * libie_fwlog_set - Set the firmware logging settings
 * @fwlog: pointer to the fwlog structure
 * @cfg: config used to set firmware logging
 *
 * This function should be called whenever the driver needs to set the firmware
 * logging configuration. It can be called on initialization, reset, or during
 * runtime.
 *
 * If the PF wishes to receive FW logging then it must register via
 * libie_fwlog_register. Note, that libie_fwlog_register does not need to be called
 * for init.
 */
static int libie_fwlog_set(struct libie_fwlog *fwlog,
			   struct libie_fwlog_cfg *cfg)
{
	if (!libie_fwlog_supported(fwlog))
		return -EOPNOTSUPP;

	return libie_aq_fwlog_set(fwlog, cfg->module_entries,
				LIBIE_AQC_FW_LOG_ID_MAX, cfg->options,
				cfg->log_resolution);
}

/**
 * libie_aq_fwlog_register - Register PF for firmware logging events (0xFF31)
 * @fwlog: pointer to the fwlog structure
 * @reg: true to register and false to unregister
 */
static int libie_aq_fwlog_register(struct libie_fwlog *fwlog, bool reg)
{
	struct libie_aq_desc desc = {0};
	struct libie_aqc_fw_log *cmd;

	desc.opcode = cpu_to_le16(libie_aqc_opc_fw_logs_register);
	desc.flags = cpu_to_le16(LIBIE_AQ_FLAG_SI);
	cmd = libie_aq_raw(&desc);

	if (reg)
		cmd->cmd_flags = LIBIE_AQC_FW_LOG_AQ_REGISTER;

	return fwlog->send_cmd(fwlog->priv, &desc, NULL, 0);
}

/**
 * libie_fwlog_register - Register the PF for firmware logging
 * @fwlog: pointer to the fwlog structure
 *
 * After this call the PF will start to receive firmware logging based on the
 * configuration set in libie_fwlog_set.
 */
static int libie_fwlog_register(struct libie_fwlog *fwlog)
{
	int status;

	if (!libie_fwlog_supported(fwlog))
		return -EOPNOTSUPP;

	status = libie_aq_fwlog_register(fwlog, true);
	if (status)
		dev_dbg(&fwlog->pdev->dev, "Failed to register for firmware logging events over ARQ\n");
	else
		fwlog->cfg.options |= LIBIE_FWLOG_OPTION_IS_REGISTERED;

	return status;
}

/**
 * libie_fwlog_unregister - Unregister the PF from firmware logging
 * @fwlog: pointer to the fwlog structure
 */
static int libie_fwlog_unregister(struct libie_fwlog *fwlog)
{
	int status;

	if (!libie_fwlog_supported(fwlog))
		return -EOPNOTSUPP;

	status = libie_aq_fwlog_register(fwlog, false);
	if (status)
		dev_dbg(&fwlog->pdev->dev, "Failed to unregister from firmware logging events over ARQ\n");
	else
		fwlog->cfg.options &= ~LIBIE_FWLOG_OPTION_IS_REGISTERED;

	return status;
}

/**
 * libie_fwlog_print_module_cfg - print current FW logging module configuration
 * @cfg: pointer to the fwlog cfg structure
 * @module: module to print
 * @s: the seq file to put data into
 */
static void
libie_fwlog_print_module_cfg(struct libie_fwlog_cfg *cfg, int module,
			     struct seq_file *s)
{
	struct libie_fwlog_module_entry *entry;

	if (module != LIBIE_AQC_FW_LOG_ID_MAX) {
		entry =	&cfg->module_entries[module];

		seq_printf(s, "\tModule: %s, Log Level: %s\n",
			   libie_fwlog_module_string[entry->module_id],
			   libie_fwlog_level_string[entry->log_level]);
	} else {
		int i;

		for (i = 0; i < LIBIE_AQC_FW_LOG_ID_MAX; i++) {
			entry =	&cfg->module_entries[i];

			seq_printf(s, "\tModule: %s, Log Level: %s\n",
				   libie_fwlog_module_string[entry->module_id],
				   libie_fwlog_level_string[entry->log_level]);
		}
	}
}

static int libie_find_module_by_dentry(struct dentry **modules, struct dentry *d)
{
	int i, module;

	module = -1;
	/* find the module based on the dentry */
	for (i = 0; i < LIBIE_NR_FW_LOG_MODULES; i++) {
		if (d == modules[i]) {
			module = i;
			break;
		}
	}

	return module;
}

/**
 * libie_debugfs_module_show - read from 'module' file
 * @s: the opened file
 * @v: pointer to the offset
 */
static int libie_debugfs_module_show(struct seq_file *s, void *v)
{
	struct libie_fwlog *fwlog = s->private;
	const struct file *filp = s->file;
	struct dentry *dentry;
	int module;

	dentry = file_dentry(filp);

	module = libie_find_module_by_dentry(fwlog->debugfs_modules, dentry);
	if (module < 0) {
		dev_info(&fwlog->pdev->dev, "unknown module\n");
		return -EINVAL;
	}

	libie_fwlog_print_module_cfg(&fwlog->cfg, module, s);

	return 0;
}

static int libie_debugfs_module_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, libie_debugfs_module_show, inode->i_private);
}

/**
 * libie_debugfs_module_write - write into 'module' file
 * @filp: the opened file
 * @buf: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 */
static ssize_t
libie_debugfs_module_write(struct file *filp, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct libie_fwlog *fwlog = file_inode(filp)->i_private;
	struct dentry *dentry = file_dentry(filp);
	struct device *dev = &fwlog->pdev->dev;
	char user_val[16], *cmd_buf;
	int module, log_level, cnt;

	/* don't allow partial writes or invalid input */
	if (*ppos != 0 || count > 8)
		return -EINVAL;

	cmd_buf = memdup_user_nul(buf, count);
	if (IS_ERR(cmd_buf))
		return PTR_ERR(cmd_buf);

	module = libie_find_module_by_dentry(fwlog->debugfs_modules, dentry);
	if (module < 0) {
		dev_info(dev, "unknown module\n");
		return -EINVAL;
	}

	cnt = sscanf(cmd_buf, "%s", user_val);
	if (cnt != 1)
		return -EINVAL;

	log_level = sysfs_match_string(libie_fwlog_level_string, user_val);
	if (log_level < 0) {
		dev_info(dev, "unknown log level '%s'\n", user_val);
		return -EINVAL;
	}

	if (module != LIBIE_AQC_FW_LOG_ID_MAX) {
		fwlog->cfg.module_entries[module].log_level = log_level;
	} else {
		/* the module 'all' is a shortcut so that we can set
		 * all of the modules to the same level quickly
		 */
		int i;

		for (i = 0; i < LIBIE_AQC_FW_LOG_ID_MAX; i++)
			fwlog->cfg.module_entries[i].log_level = log_level;
	}

	return count;
}

static const struct file_operations libie_debugfs_module_fops = {
	.owner = THIS_MODULE,
	.open  = libie_debugfs_module_open,
	.read = seq_read,
	.release = single_release,
	.write = libie_debugfs_module_write,
};

/**
 * libie_debugfs_nr_messages_read - read from 'nr_messages' file
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 */
static ssize_t libie_debugfs_nr_messages_read(struct file *filp,
					      char __user *buffer, size_t count,
					      loff_t *ppos)
{
	struct libie_fwlog *fwlog = filp->private_data;
	char buff[32] = {};

	snprintf(buff, sizeof(buff), "%d\n",
		 fwlog->cfg.log_resolution);

	return simple_read_from_buffer(buffer, count, ppos, buff, strlen(buff));
}

/**
 * libie_debugfs_nr_messages_write - write into 'nr_messages' file
 * @filp: the opened file
 * @buf: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 */
static ssize_t
libie_debugfs_nr_messages_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct libie_fwlog *fwlog = filp->private_data;
	struct device *dev = &fwlog->pdev->dev;
	char user_val[8], *cmd_buf;
	s16 nr_messages;
	ssize_t ret;

	/* don't allow partial writes or invalid input */
	if (*ppos != 0 || count > 4)
		return -EINVAL;

	cmd_buf = memdup_user_nul(buf, count);
	if (IS_ERR(cmd_buf))
		return PTR_ERR(cmd_buf);

	ret = sscanf(cmd_buf, "%s", user_val);
	if (ret != 1)
		return -EINVAL;

	ret = kstrtos16(user_val, 0, &nr_messages);
	if (ret)
		return ret;

	if (nr_messages < LIBIE_AQC_FW_LOG_MIN_RESOLUTION ||
	    nr_messages > LIBIE_AQC_FW_LOG_MAX_RESOLUTION) {
		dev_err(dev, "Invalid FW log number of messages %d, value must be between %d - %d\n",
			nr_messages, LIBIE_AQC_FW_LOG_MIN_RESOLUTION,
			LIBIE_AQC_FW_LOG_MAX_RESOLUTION);
		return -EINVAL;
	}

	fwlog->cfg.log_resolution = nr_messages;

	return count;
}

static const struct file_operations libie_debugfs_nr_messages_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read = libie_debugfs_nr_messages_read,
	.write = libie_debugfs_nr_messages_write,
};

/**
 * libie_debugfs_enable_read - read from 'enable' file
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 */
static ssize_t libie_debugfs_enable_read(struct file *filp,
					 char __user *buffer, size_t count,
					 loff_t *ppos)
{
	struct libie_fwlog *fwlog = filp->private_data;
	char buff[32] = {};

	snprintf(buff, sizeof(buff), "%u\n",
		 (u16)(fwlog->cfg.options &
		 LIBIE_FWLOG_OPTION_IS_REGISTERED) >> 3);

	return simple_read_from_buffer(buffer, count, ppos, buff, strlen(buff));
}

/**
 * libie_debugfs_enable_write - write into 'enable' file
 * @filp: the opened file
 * @buf: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 */
static ssize_t
libie_debugfs_enable_write(struct file *filp, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct libie_fwlog *fwlog = filp->private_data;
	char user_val[8], *cmd_buf;
	bool enable;
	ssize_t ret;

	/* don't allow partial writes or invalid input */
	if (*ppos != 0 || count > 2)
		return -EINVAL;

	cmd_buf = memdup_user_nul(buf, count);
	if (IS_ERR(cmd_buf))
		return PTR_ERR(cmd_buf);

	ret = sscanf(cmd_buf, "%s", user_val);
	if (ret != 1)
		return -EINVAL;

	ret = kstrtobool(user_val, &enable);
	if (ret)
		goto enable_write_error;

	if (enable)
		fwlog->cfg.options |= LIBIE_FWLOG_OPTION_ARQ_ENA;
	else
		fwlog->cfg.options &= ~LIBIE_FWLOG_OPTION_ARQ_ENA;

	ret = libie_fwlog_set(fwlog, &fwlog->cfg);
	if (ret)
		goto enable_write_error;

	if (enable)
		ret = libie_fwlog_register(fwlog);
	else
		ret = libie_fwlog_unregister(fwlog);

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

static const struct file_operations libie_debugfs_enable_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read = libie_debugfs_enable_read,
	.write = libie_debugfs_enable_write,
};

/**
 * libie_debugfs_log_size_read - read from 'log_size' file
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 */
static ssize_t libie_debugfs_log_size_read(struct file *filp,
					   char __user *buffer, size_t count,
					   loff_t *ppos)
{
	struct libie_fwlog *fwlog = filp->private_data;
	char buff[32] = {};
	int index;

	index = fwlog->ring.index;
	snprintf(buff, sizeof(buff), "%s\n", libie_fwlog_log_size[index]);

	return simple_read_from_buffer(buffer, count, ppos, buff, strlen(buff));
}

/**
 * libie_debugfs_log_size_write - write into 'log_size' file
 * @filp: the opened file
 * @buf: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 */
static ssize_t
libie_debugfs_log_size_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct libie_fwlog *fwlog = filp->private_data;
	struct device *dev = &fwlog->pdev->dev;
	char user_val[8], *cmd_buf;
	ssize_t ret;
	int index;

	/* don't allow partial writes or invalid input */
	if (*ppos != 0 || count > 5)
		return -EINVAL;

	cmd_buf = memdup_user_nul(buf, count);
	if (IS_ERR(cmd_buf))
		return PTR_ERR(cmd_buf);

	ret = sscanf(cmd_buf, "%s", user_val);
	if (ret != 1)
		return -EINVAL;

	index = sysfs_match_string(libie_fwlog_log_size, user_val);
	if (index < 0) {
		dev_info(dev, "Invalid log size '%s'. The value must be one of 128K, 256K, 512K, 1M, 2M\n",
			 user_val);
		ret = -EINVAL;
		goto log_size_write_error;
	} else if (fwlog->cfg.options & LIBIE_FWLOG_OPTION_IS_REGISTERED) {
		dev_info(dev, "FW logging is currently running. Please disable FW logging to change log_size\n");
		ret = -EINVAL;
		goto log_size_write_error;
	}

	/* free all the buffers and the tracking info and resize */
	libie_fwlog_realloc_rings(fwlog, index);

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

static const struct file_operations libie_debugfs_log_size_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read = libie_debugfs_log_size_read,
	.write = libie_debugfs_log_size_write,
};

/**
 * libie_debugfs_data_read - read from 'data' file
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 */
static ssize_t libie_debugfs_data_read(struct file *filp, char __user *buffer,
				       size_t count, loff_t *ppos)
{
	struct libie_fwlog *fwlog = filp->private_data;
	int data_copied = 0;
	bool done = false;

	if (libie_fwlog_ring_empty(&fwlog->ring))
		return 0;

	while (!libie_fwlog_ring_empty(&fwlog->ring) && !done) {
		struct libie_fwlog_data *log;
		u16 cur_buf_len;

		log = &fwlog->ring.rings[fwlog->ring.head];
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
		libie_fwlog_ring_increment(&fwlog->ring.head, fwlog->ring.size);
	}

	return data_copied;
}

/**
 * libie_debugfs_data_write - write into 'data' file
 * @filp: the opened file
 * @buf: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 */
static ssize_t
libie_debugfs_data_write(struct file *filp, const char __user *buf, size_t count,
			 loff_t *ppos)
{
	struct libie_fwlog *fwlog = filp->private_data;
	struct device *dev = &fwlog->pdev->dev;
	ssize_t ret;

	/* don't allow partial writes */
	if (*ppos != 0)
		return 0;

	/* any value is allowed to clear the buffer so no need to even look at
	 * what the value is
	 */
	if (!(fwlog->cfg.options & LIBIE_FWLOG_OPTION_IS_REGISTERED)) {
		fwlog->ring.head = 0;
		fwlog->ring.tail = 0;
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

static const struct file_operations libie_debugfs_data_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read = libie_debugfs_data_read,
	.write = libie_debugfs_data_write,
};

/**
 * libie_debugfs_fwlog_init - setup the debugfs directory
 * @fwlog: pointer to the fwlog structure
 * @root: debugfs root entry on which fwlog director will be registered
 */
static void libie_debugfs_fwlog_init(struct libie_fwlog *fwlog,
				     struct dentry *root)
{
	struct dentry *fw_modules_dir;
	struct dentry **fw_modules;
	int i;

	/* allocate space for this first because if it fails then we don't
	 * need to unwind
	 */
	fw_modules = kcalloc(LIBIE_NR_FW_LOG_MODULES, sizeof(*fw_modules),
			     GFP_KERNEL);
	if (!fw_modules)
		return;

	fwlog->debugfs = debugfs_create_dir("fwlog", root);
	if (IS_ERR(fwlog->debugfs))
		goto err_create_module_files;

	fw_modules_dir = debugfs_create_dir("modules", fwlog->debugfs);
	if (IS_ERR(fw_modules_dir))
		goto err_create_module_files;

	for (i = 0; i < LIBIE_NR_FW_LOG_MODULES; i++) {
		fw_modules[i] = debugfs_create_file(libie_fwlog_module_string[i],
						    0600, fw_modules_dir, fwlog,
						    &libie_debugfs_module_fops);
		if (IS_ERR(fw_modules[i]))
			goto err_create_module_files;
	}

	debugfs_create_file("nr_messages", 0600, fwlog->debugfs, fwlog,
			    &libie_debugfs_nr_messages_fops);

	fwlog->debugfs_modules = fw_modules;

	debugfs_create_file("enable", 0600, fwlog->debugfs, fwlog,
			    &libie_debugfs_enable_fops);

	debugfs_create_file("log_size", 0600, fwlog->debugfs, fwlog,
			    &libie_debugfs_log_size_fops);

	debugfs_create_file("data", 0600, fwlog->debugfs, fwlog,
			    &libie_debugfs_data_fops);

	return;

err_create_module_files:
	debugfs_remove_recursive(fwlog->debugfs);
	kfree(fw_modules);
}

static bool libie_fwlog_ring_full(struct libie_fwlog_ring *rings)
{
	u16 head, tail;

	head = rings->head;
	tail = rings->tail;

	if (head < tail && (tail - head == (rings->size - 1)))
		return true;
	else if (head > tail && (tail == (head - 1)))
		return true;

	return false;
}

/**
 * libie_aq_fwlog_get - Get the current firmware logging configuration (0xFF32)
 * @fwlog: pointer to the fwlog structure
 * @cfg: firmware logging configuration to populate
 */
static int libie_aq_fwlog_get(struct libie_fwlog *fwlog,
			      struct libie_fwlog_cfg *cfg)
{
	struct libie_aqc_fw_log_cfg_resp *fw_modules;
	struct libie_aq_desc desc = {0};
	struct libie_aqc_fw_log *cmd;
	u16 module_id_cnt;
	int status;
	void *buf;
	int i;

	memset(cfg, 0, sizeof(*cfg));

	buf = kzalloc(LIBIE_AQ_MAX_BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	desc.opcode = cpu_to_le16(libie_aqc_opc_fw_logs_query);
	desc.flags = cpu_to_le16(LIBIE_AQ_FLAG_SI);
	cmd = libie_aq_raw(&desc);

	cmd->cmd_flags = LIBIE_AQC_FW_LOG_AQ_QUERY;

	status = fwlog->send_cmd(fwlog->priv, &desc, buf, LIBIE_AQ_MAX_BUF_LEN);
	if (status) {
		dev_dbg(&fwlog->pdev->dev, "Failed to get FW log configuration\n");
		goto status_out;
	}

	module_id_cnt = le16_to_cpu(cmd->ops.cfg.mdl_cnt);
	if (module_id_cnt < LIBIE_AQC_FW_LOG_ID_MAX) {
		dev_dbg(&fwlog->pdev->dev, "FW returned less than the expected number of FW log module IDs\n");
	} else if (module_id_cnt > LIBIE_AQC_FW_LOG_ID_MAX) {
		dev_dbg(&fwlog->pdev->dev, "FW returned more than expected number of FW log module IDs, setting module_id_cnt to software expected max %u\n",
			LIBIE_AQC_FW_LOG_ID_MAX);
		module_id_cnt = LIBIE_AQC_FW_LOG_ID_MAX;
	}

	cfg->log_resolution = le16_to_cpu(cmd->ops.cfg.log_resolution);
	if (cmd->cmd_flags & LIBIE_AQC_FW_LOG_CONF_AQ_EN)
		cfg->options |= LIBIE_FWLOG_OPTION_ARQ_ENA;
	if (cmd->cmd_flags & LIBIE_AQC_FW_LOG_CONF_UART_EN)
		cfg->options |= LIBIE_FWLOG_OPTION_UART_ENA;
	if (cmd->cmd_flags & LIBIE_AQC_FW_LOG_QUERY_REGISTERED)
		cfg->options |= LIBIE_FWLOG_OPTION_IS_REGISTERED;

	fw_modules = (struct libie_aqc_fw_log_cfg_resp *)buf;

	for (i = 0; i < module_id_cnt; i++) {
		struct libie_aqc_fw_log_cfg_resp *fw_module = &fw_modules[i];

		cfg->module_entries[i].module_id =
			le16_to_cpu(fw_module->module_identifier);
		cfg->module_entries[i].log_level = fw_module->log_level;
	}

status_out:
	kfree(buf);
	return status;
}

/**
 * libie_fwlog_set_supported - Set if FW logging is supported by FW
 * @fwlog: pointer to the fwlog structure
 *
 * If FW returns success to the libie_aq_fwlog_get call then it supports FW
 * logging, else it doesn't. Set the fwlog_supported flag accordingly.
 *
 * This function is only meant to be called during driver init to determine if
 * the FW support FW logging.
 */
static void libie_fwlog_set_supported(struct libie_fwlog *fwlog)
{
	struct libie_fwlog_cfg *cfg;
	int status;

	fwlog->supported = false;

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return;

	status = libie_aq_fwlog_get(fwlog, cfg);
	if (status)
		dev_dbg(&fwlog->pdev->dev, "libie_aq_fwlog_get failed, FW logging is not supported on this version of FW, status %d\n",
			status);
	else
		fwlog->supported = true;

	kfree(cfg);
}

/**
 * libie_fwlog_init - Initialize FW logging configuration
 * @fwlog: pointer to the fwlog structure
 * @api: api structure to init fwlog
 *
 * This function should be called on driver initialization during
 * libie_init_hw().
 */
int libie_fwlog_init(struct libie_fwlog *fwlog, struct libie_fwlog_api *api)
{
	fwlog->api = *api;
	libie_fwlog_set_supported(fwlog);

	if (libie_fwlog_supported(fwlog)) {
		int status;

		/* read the current config from the FW and store it */
		status = libie_aq_fwlog_get(fwlog, &fwlog->cfg);
		if (status)
			return status;

		fwlog->ring.rings = kcalloc(LIBIE_FWLOG_RING_SIZE_DFLT,
					    sizeof(*fwlog->ring.rings),
					    GFP_KERNEL);
		if (!fwlog->ring.rings) {
			dev_warn(&fwlog->pdev->dev, "Unable to allocate memory for FW log rings\n");
			return -ENOMEM;
		}

		fwlog->ring.size = LIBIE_FWLOG_RING_SIZE_DFLT;
		fwlog->ring.index = LIBIE_FWLOG_RING_SIZE_INDEX_DFLT;

		status = libie_fwlog_alloc_ring_buffs(&fwlog->ring);
		if (status) {
			dev_warn(&fwlog->pdev->dev, "Unable to allocate memory for FW log ring data buffers\n");
			libie_fwlog_free_ring_buffs(&fwlog->ring);
			kfree(fwlog->ring.rings);
			return status;
		}

		libie_debugfs_fwlog_init(fwlog, api->debugfs_root);
	} else {
		dev_warn(&fwlog->pdev->dev, "FW logging is not supported in this NVM image. Please update the NVM to get FW log support\n");
	}

	return 0;
}
EXPORT_SYMBOL_GPL(libie_fwlog_init);

/**
 * libie_fwlog_deinit - unroll FW logging configuration
 * @fwlog: pointer to the fwlog structure
 *
 * This function should be called in libie_deinit_hw().
 */
void libie_fwlog_deinit(struct libie_fwlog *fwlog)
{
	int status;

	/* make sure FW logging is disabled to not put the FW in a weird state
	 * for the next driver load
	 */
	fwlog->cfg.options &= ~LIBIE_FWLOG_OPTION_ARQ_ENA;
	status = libie_fwlog_set(fwlog, &fwlog->cfg);
	if (status)
		dev_warn(&fwlog->pdev->dev, "Unable to turn off FW logging, status: %d\n",
			 status);

	kfree(fwlog->debugfs_modules);

	fwlog->debugfs_modules = NULL;

	status = libie_fwlog_unregister(fwlog);
	if (status)
		dev_warn(&fwlog->pdev->dev, "Unable to unregister FW logging, status: %d\n",
			 status);

	if (fwlog->ring.rings) {
		libie_fwlog_free_ring_buffs(&fwlog->ring);
		kfree(fwlog->ring.rings);
	}
}
EXPORT_SYMBOL_GPL(libie_fwlog_deinit);

/**
 * libie_get_fwlog_data - copy the FW log data from ARQ event
 * @fwlog: fwlog that the FW log event is associated with
 * @buf: event buffer pointer
 * @len: len of event descriptor
 */
void libie_get_fwlog_data(struct libie_fwlog *fwlog, u8 *buf, u16 len)
{
	struct libie_fwlog_data *log;

	log = &fwlog->ring.rings[fwlog->ring.tail];

	memset(log->data, 0, PAGE_SIZE);
	log->data_size = len;

	memcpy(log->data, buf, log->data_size);
	libie_fwlog_ring_increment(&fwlog->ring.tail, fwlog->ring.size);

	if (libie_fwlog_ring_full(&fwlog->ring)) {
		/* the rings are full so bump the head to create room */
		libie_fwlog_ring_increment(&fwlog->ring.head, fwlog->ring.size);
	}
}
EXPORT_SYMBOL_GPL(libie_get_fwlog_data);

void libie_fwlog_reregister(struct libie_fwlog *fwlog)
{
	if (!(fwlog->cfg.options & LIBIE_FWLOG_OPTION_IS_REGISTERED))
		return;

	if (libie_fwlog_register(fwlog))
		fwlog->cfg.options &= ~LIBIE_FWLOG_OPTION_IS_REGISTERED;
}
EXPORT_SYMBOL_GPL(libie_fwlog_reregister);

MODULE_DESCRIPTION("Intel(R) Ethernet common library");
MODULE_LICENSE("GPL");
