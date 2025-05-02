/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * FacetimeHD camera driver
 *
 * Copyright (C) 2015 Sven Schnelle <svens@stackframe.org>
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include "fthd_drv.h"
#include "fthd_debugfs.h"
#include "fthd_isp.h"
#include "fthd_ringbuf.h"
#include "fthd_hw.h"

static ssize_t fthd_store_debug(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)

{
	struct fthd_isp_debug_cmd cmd;
	struct fthd_private *dev_priv = file->private_data;
	int ret, opcode;
	char buf[64];
	int len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	memset(&cmd, 0, sizeof(cmd));

	if (!strcmp(buf, "ps"))
		opcode = CISP_CMD_DEBUG_PS;
	else if (!strcmp(buf, "banner"))
		opcode = CISP_CMD_DEBUG_BANNER;
	else if (!strcmp(buf, "get_root"))
		opcode = CISP_CMD_DEBUG_GET_ROOT_HANDLE;
	else if (!strcmp(buf, "heap"))
		opcode = CISP_CMD_DEBUG_HEAP_STATISTICS;
	else if (!strcmp(buf, "irq"))
		opcode = CISP_CMD_DEBUG_IRQ_STATISTICS;
	else if (!strcmp(buf, "semaphore"))
		opcode = CISP_CMD_DEBUG_SHOW_SEMAPHORE_STATUS;
	else if (!strcmp(buf, "wiring"))
		opcode = CISP_CMD_DEBUG_SHOW_WIRING_OPERATIONS;
	else if (sscanf(buf, "get_object_by_name %s", (char *)&cmd.arg) == 1)
		opcode = CISP_CMD_DEBUG_GET_OBJECT_BY_NAME;
	else if (sscanf(buf, "dump_object %x", &cmd.arg[0]) == 1)
		opcode = CISP_CMD_DEBUG_DUMP_OBJECT;
	else if (!strcmp(buf, "dump_objects"))
		opcode = CISP_CMD_DEBUG_DUMP_ALL_OBJECTS;
	else if (!strcmp(buf, "show_objects"))
		opcode = CISP_CMD_DEBUG_SHOW_OBJECT_GRAPH;
	else if (sscanf(buf, "get_debug_level %i", &cmd.arg[0]) == 1)
		opcode = CISP_CMD_DEBUG_GET_DEBUG_LEVEL;
	else if (sscanf(buf, "set_debug_level %x %i", &cmd.arg[0], &cmd.arg[1]) == 2)
		opcode = CISP_CMD_DEBUG_SET_DEBUG_LEVEL;
	else if (sscanf(buf, "set_debug_level_rec %x %i", &cmd.arg[0], &cmd.arg[1]) == 2)
		opcode = CISP_CMD_DEBUG_SET_DEBUG_LEVEL_RECURSIVE;
	else if (!strcmp(buf, "get_fsm_count"))
		opcode = CISP_CMD_DEBUG_GET_FSM_COUNT;
	else if (sscanf(buf, "get_fsm_by_name %s", (char *)&cmd.arg[0]) == 1)
		opcode = CISP_CMD_DEBUG_GET_FSM_BY_NAME;
	else if (sscanf(buf, "get_fsm_by_index %i", &cmd.arg[0]) == 1)
		opcode = CISP_CMD_DEBUG_GET_FSM_BY_INDEX;
	else if (sscanf(buf, "get_fsm_debug_level %x", &cmd.arg[0]) == 1)
		opcode = CISP_CMD_DEBUG_GET_FSM_DEBUG_LEVEL;
	else if (sscanf(buf, "set_fsm_debug_level %x", &cmd.arg[0]) == 2)
		opcode = CISP_CMD_DEBUG_SET_FSM_DEBUG_LEVEL;

	else if (sscanf(buf, "%i %i\n", &opcode, &cmd.arg[0]) != 2)
		return -EINVAL;
	cmd.show_errors = 1;

	ret = fthd_isp_debug_cmd(dev_priv, opcode, &cmd, sizeof(cmd), NULL);
	if (ret)
		return ret;

	return count;
}


static int seq_channel_read(struct seq_file *seq, struct fthd_private *dev_priv,
			struct fw_channel *chan)
{
	int i;
	char pos;
	u32 entry;

	spin_lock_irq(&chan->lock);
	for( i = 0; i < chan->size; i++) {
		if (chan->ringbuf.idx == i)
			pos = '*';
		else
			pos = ' ';
		entry = get_entry_addr(dev_priv, chan, i);
		seq_printf(seq, "%c%3.3d: ADDRESS %08x REQUEST_SIZE %08x RESPONSE_SIZE %08x\n",
			   pos, i,
			   FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_ADDRESS_FLAGS),
			   FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_REQUEST_SIZE),
			   FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_RESPONSE_SIZE));
	}
	spin_unlock_irq(&chan->lock);
	return 0;
}

static int seq_channel_terminal_read(struct seq_file *seq, void *data)

{
	struct fthd_private *dev_priv = dev_get_drvdata(seq->private);
	return seq_channel_read(seq, dev_priv, dev_priv->channel_terminal);
}

static int seq_channel_sharedmalloc_read(struct seq_file *seq, void *data)

{
	struct fthd_private *dev_priv = dev_get_drvdata(seq->private);
	return seq_channel_read(seq, dev_priv, dev_priv->channel_shared_malloc);
}

static int seq_channel_io_read(struct seq_file *seq, void *data)

{
	struct fthd_private *dev_priv = dev_get_drvdata(seq->private);
	return seq_channel_read(seq, dev_priv, dev_priv->channel_io);
}

static int seq_channel_io_t2h_read(struct seq_file *seq, void *data)

{
	struct fthd_private *dev_priv = dev_get_drvdata(seq->private);
	return seq_channel_read(seq, dev_priv, dev_priv->channel_io_t2h);
}

static int seq_channel_buf_h2t_read(struct seq_file *seq, void *data)

{
	struct fthd_private *dev_priv = dev_get_drvdata(seq->private);
	return seq_channel_read(seq, dev_priv, dev_priv->channel_buf_h2t);
}

static int seq_channel_buf_t2h_read(struct seq_file *seq, void *data)

{
	struct fthd_private *dev_priv = dev_get_drvdata(seq->private);
	return seq_channel_read(seq, dev_priv, dev_priv->channel_buf_t2h);
}

static int seq_channel_debug_read(struct seq_file *seq, void *data)

{
	struct fthd_private *dev_priv = dev_get_drvdata(seq->private);
	return seq_channel_read(seq, dev_priv, dev_priv->channel_debug);
}

static const struct file_operations fops_debug = {
	.read = NULL,
	.write = fthd_store_debug,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

int fthd_debugfs_init(struct fthd_private *dev_priv)
{
	struct dentry *d, *top;

	top = debugfs_create_dir("facetimehd", NULL);
	if (IS_ERR(top))
		return PTR_ERR(top);

	d = debugfs_create_dir(dev_name(&dev_priv->pdev->dev), top);
	if (IS_ERR(d)) {
		debugfs_remove_recursive(top);
		return PTR_ERR(d);
	}

	debugfs_create_devm_seqfile(&dev_priv->pdev->dev, "channel_terminal", d, seq_channel_terminal_read);
	debugfs_create_devm_seqfile(&dev_priv->pdev->dev, "channel_sharedmalloc", d, seq_channel_sharedmalloc_read);
	debugfs_create_devm_seqfile(&dev_priv->pdev->dev, "channel_io", d, seq_channel_io_read);
	debugfs_create_devm_seqfile(&dev_priv->pdev->dev, "channel_io_t2h", d, seq_channel_io_t2h_read);
	debugfs_create_devm_seqfile(&dev_priv->pdev->dev, "channel_buf_h2t", d, seq_channel_buf_h2t_read);
	debugfs_create_devm_seqfile(&dev_priv->pdev->dev, "channel_buf_t2h", d, seq_channel_buf_t2h_read);
	debugfs_create_devm_seqfile(&dev_priv->pdev->dev, "channel_debug", d, seq_channel_debug_read);
	debugfs_create_file("debug", S_IRUSR | S_IWUSR, d, dev_priv, &fops_debug);
	dev_priv->debugfs = top;
	return 0;
}

void fthd_debugfs_exit(struct fthd_private *dev_priv)
{
	debugfs_remove_recursive(dev_priv->debugfs);
}
