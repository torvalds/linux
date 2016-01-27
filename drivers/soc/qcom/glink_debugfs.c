/* Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/ipc_logging.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <soc/qcom/glink.h>
#include "glink_private.h"
#include "glink_core_if.h"


static const char * const ss_string[] = {
	[GLINK_DBGFS_MPSS] = "mpss",
	[GLINK_DBGFS_APSS] = "apss",
	[GLINK_DBGFS_LPASS] = "lpass",
	[GLINK_DBGFS_DSPS] = "dsps",
	[GLINK_DBGFS_RPM] = "rpm",
	[GLINK_DBGFS_WCNSS] = "wcnss",
	[GLINK_DBGFS_LLOOP] = "lloop",
	[GLINK_DBGFS_MOCK] = "mock"
};

static const char * const xprt_string[] = {
	[GLINK_DBGFS_SMEM] = "smem",
	[GLINK_DBGFS_SMD] = "smd",
	[GLINK_DBGFS_XLLOOP] = "lloop",
	[GLINK_DBGFS_XMOCK] = "mock",
	[GLINK_DBGFS_XMOCK_LOW] = "mock_low",
	[GLINK_DBGFS_XMOCK_HIGH] = "mock_high"
};

static const char * const ch_st_string[] = {
	[GLINK_CHANNEL_CLOSED] = "CLOSED",
	[GLINK_CHANNEL_OPENING] = "OPENING",
	[GLINK_CHANNEL_OPENED] = "OPENED",
	[GLINK_CHANNEL_CLOSING] = "CLOSING",
};

static const char * const xprt_st_string[] = {
	[GLINK_XPRT_DOWN] = "DOWN",
	[GLINK_XPRT_NEGOTIATING] = "NEGOT",
	[GLINK_XPRT_OPENED] = "OPENED",
	[GLINK_XPRT_FAILED] = "FAILED"
};

#if defined(CONFIG_DEBUG_FS)
#define GLINK_DBGFS_NAME_SIZE (2 * GLINK_NAME_SIZE + 1)

struct glink_dbgfs_dent {
	struct list_head list_node;
	char par_name[GLINK_DBGFS_NAME_SIZE];
	char self_name[GLINK_DBGFS_NAME_SIZE];
	struct dentry *parent;
	struct dentry *self;
	spinlock_t file_list_lock_lhb0;
	struct list_head file_list;
};

static struct dentry *dent;
static LIST_HEAD(dent_list);
static DEFINE_MUTEX(dent_list_lock_lha0);

static int debugfs_show(struct seq_file *s, void *data)
{
	struct glink_dbgfs_data *dfs_d;
	dfs_d = s->private;
	dfs_d->o_func(s);
	return 0;
}

static int debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_show, inode->i_private);
}

static const struct file_operations debug_ops = {
	.open = debug_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek,
};
#endif

/**
 * glink_get_ss_enum_string() - get the name of the subsystem based on enum value
 * @enum_id:	enum id of a specific subsystem.
 *
 * Return: name of the subsystem, NULL in case of invalid input
 */
const char *glink_get_ss_enum_string(unsigned int enum_id)
{
	if (enum_id >= ARRAY_SIZE(ss_string))
		return NULL;

	return ss_string[enum_id];
}
EXPORT_SYMBOL(glink_get_ss_enum_string);

/**
 * glink_get_xprt_enum_string() - get the name of the transport based on enum value
 * @enum_id:	enum id of a specific transport.
 *
 * Return: name of the transport, NULL in case of invalid input
 */
const char *glink_get_xprt_enum_string(unsigned int enum_id)
{
	if (enum_id >= ARRAY_SIZE(xprt_string))
		return NULL;
	return xprt_string[enum_id];
}
EXPORT_SYMBOL(glink_get_xprt_enum_string);

/**
 * glink_get_xprt_state_string() - get the name of the transport based on enum value
 * @enum_id:	enum id of the state of the transport.
 *
 * Return: name of the transport state, NULL in case of invalid input
 */
const char *glink_get_xprt_state_string(
				enum transport_state_e enum_id)
{
	if (enum_id >= ARRAY_SIZE(xprt_st_string))
		return NULL;

	return xprt_st_string[enum_id];
}
EXPORT_SYMBOL(glink_get_xprt_state_string);

/**
 * glink_get_ch_state_string() - get the name of the transport based on enum value
 * @enum_id:	enum id of a specific state of the channel.
 *
 * Return: name of the channel state, NULL in case of invalid input
 */
const char *glink_get_ch_state_string(
				enum local_channel_state_e enum_id)
{
	if (enum_id >= ARRAY_SIZE(ch_st_string))
		return NULL;

	return ch_st_string[enum_id];
}
EXPORT_SYMBOL(glink_get_ch_state_string);

#if defined(CONFIG_DEBUG_FS)
/**
 * glink_dfs_create_file() - create the debugfs file
 * @name:	debugfs file name
 * @parent:	pointer to the parent dentry structure
 * @show:	pointer to the actual function which will be invoked upon
 *		opening this file.
 *
 * Return:	pointer to the allocated glink_dbgfs_data structure or
 *		NULL in case of an error.
 *
 * This function actually create a debugfs file under the parent directory
 */
static struct glink_dbgfs_data *glink_dfs_create_file(const char *name,
		struct dentry *parent, void (*show)(struct seq_file *s),
		void *dbgfs_data, bool b_free_req)
{
	struct dentry *file;
	struct glink_dbgfs_data *dfs_d;

	dfs_d = kzalloc(sizeof(struct glink_dbgfs_data), GFP_KERNEL);
	if (dfs_d == NULL)
		return NULL;

	dfs_d->o_func = show;
	if (dbgfs_data != NULL) {
		dfs_d->priv_data = dbgfs_data;
		dfs_d->b_priv_free_req = b_free_req;
	}
	file = debugfs_create_file(name, 0400, parent, dfs_d, &debug_ops);
	if (!file)
		GLINK_DBG("%s: unable to create file '%s'\n", __func__,
				name);
	dfs_d->dent = file;
	return dfs_d;
}

/**
 * write_ch_intent() - write channel intent details
 * @s:		pointer to the sequential file
 * @intent:	pointer glink core intent structure
 * @i_type:	type of intent
 * @count:	serial number of the intent.
 *
 * This function is a helper function of glink_dfs_update_ch_intents()
 * that prints out details of any specific intent.
 */
static void write_ch_intent(struct seq_file *s,
			struct glink_core_rx_intent *intent,
			char *i_type, unsigned int count)
{
	char *intent_type;
	/*
	* formatted, human readable channel state output, ie:
	* TYPE       |SN  |ID |PKT_SIZE|W_OFFSET|INT_SIZE|
	* --------------------------------------------------------------
	* LOCAL_LIST|#2  |1   |0       |0       |8       |
	*/
	if (count == 1) {
		intent_type = i_type;
		seq_puts(s,
		"\n--------------------------------------------------------\n");
	} else {
		intent_type = "";
	}
	seq_printf(s, "%-20s|#%-5d|%-6u|%-10zu|%-10zu|%-10zu|\n",
			intent_type,
			count,
			intent->id,
			intent->pkt_size,
			intent->write_offset,
			intent->intent_size);
}

/**
 * glink_dfs_update_ch_intent() - writes the intent details of a specific
 *				  channel to the corresponding debugfs file
 * @s:		pointer to the sequential file
 *
 * This function extracts the intent details of a channel & prints them to
 * corrseponding debugfs file of that channel.
 */
static void glink_dfs_update_ch_intent(struct seq_file *s)
{
	struct glink_dbgfs_data *dfs_d;
	struct channel_ctx *ch_ctx;
	struct glink_core_rx_intent *intent;
	struct glink_core_rx_intent *intent_temp;
	struct glink_ch_intent_info ch_intent_info;
	unsigned long flags;
	unsigned int count = 0;

	dfs_d = s->private;
	ch_ctx = dfs_d->priv_data;
	if (ch_ctx != NULL) {
		glink_get_ch_intent_info(ch_ctx, &ch_intent_info);
		seq_puts(s,
		"---------------------------------------------------------------\n");
		seq_printf(s, "%-20s|%-6s|%-6s|%-10s|%-10s|%-10s|\n",
					"INTENT TYPE",
					"SN",
					"ID",
					"PKT_SIZE",
					"W_OFFSET",
					"INT_SIZE");
		seq_puts(s,
		"---------------------------------------------------------------\n");
		spin_lock_irqsave(ch_intent_info.li_lst_lock, flags);
		list_for_each_entry_safe(intent, intent_temp,
				ch_intent_info.li_avail_list, list) {
			count++;
			write_ch_intent(s, intent, "LOCAL_AVAIL_LIST", count);
		}

		count = 0;
		list_for_each_entry_safe(intent, intent_temp,
				ch_intent_info.li_used_list, list) {
			count++;
			write_ch_intent(s, intent, "LOCAL_USED_LIST", count);
		}
		spin_unlock_irqrestore(ch_intent_info.li_lst_lock, flags);

		count = 0;
		spin_lock_irqsave(ch_intent_info.ri_lst_lock, flags);
		list_for_each_entry_safe(intent, intent_temp,
				ch_intent_info.ri_list, list) {
			count++;
			write_ch_intent(s, intent, "REMOTE_LIST", count);
		}
		spin_unlock_irqrestore(ch_intent_info.ri_lst_lock,
					flags);
		seq_puts(s,
		"---------------------------------------------------------------\n");
	}
}

/**
 * glink_dfs_update_ch_stats() - writes statistics of a specific
 *				 channel to the corresponding debugfs file
 * @s:		pointer to the sequential file
 *
 * This function extracts other statistics of a channel & prints them to
 * corrseponding debugfs file of that channel
 */
static void glink_dfs_update_ch_stats(struct seq_file *s)
{
	/* FUTURE:  add channel statistics */
	seq_puts(s, "not yet implemented\n");
}

/**
 * glink_debugfs_remove_channel() - remove all channel specifc files & folder in
 *				 debugfs when channel is fully closed
 * @ch_ctx:		pointer to the channel_contenxt
 * @xprt_ctx:		pointer to the transport_context
 *
 * This function is invoked when any channel is fully closed. It removes the
 * folders & other files in debugfs for that channel.
 */
void glink_debugfs_remove_channel(struct channel_ctx *ch_ctx,
			struct glink_core_xprt_ctx *xprt_ctx){

	struct glink_dbgfs ch_rm_dbgfs;
	char *edge_name;
	char curr_dir_name[GLINK_DBGFS_NAME_SIZE];
	char *xprt_name;

	ch_rm_dbgfs.curr_name = glink_get_ch_name(ch_ctx);
	edge_name = glink_get_xprt_edge_name(xprt_ctx);
	xprt_name = glink_get_xprt_name(xprt_ctx);
	if (!xprt_name || !edge_name) {
		GLINK_ERR("%s: Invalid xprt_name  or edge_name for ch '%s'\n",
				__func__, ch_rm_dbgfs.curr_name);
		return;
	}
	snprintf(curr_dir_name, sizeof(curr_dir_name), "%s_%s",
					edge_name, xprt_name);
	ch_rm_dbgfs.par_name = curr_dir_name;
	glink_debugfs_remove_recur(&ch_rm_dbgfs);
}
EXPORT_SYMBOL(glink_debugfs_remove_channel);

/**
 * glink_debugfs_add_channel() - create channel specifc files & folder in
 *				 debugfs when channel is added
 * @ch_ctx:		pointer to the channel_contenxt
 * @xprt_ctx:		pointer to the transport_context
 *
 * This function is invoked when a new channel is created. It creates the
 * folders & other files in debugfs for that channel
 */
void glink_debugfs_add_channel(struct channel_ctx *ch_ctx,
		struct glink_core_xprt_ctx *xprt_ctx)
{
	struct glink_dbgfs ch_dbgfs;
	char *ch_name;
	char *edge_name;
	char *xprt_name;
	char curr_dir_name[GLINK_DBGFS_NAME_SIZE];

	if (ch_ctx == NULL) {
		GLINK_ERR("%s: Channel Context is NULL\n", __func__);
		return;
	}

	ch_name = glink_get_ch_name(ch_ctx);
	edge_name =  glink_get_xprt_edge_name(xprt_ctx);
	xprt_name =  glink_get_xprt_name(xprt_ctx);
	if (!xprt_name || !edge_name) {
		GLINK_ERR("%s: Invalid xprt_name  or edge_name for ch '%s'\n",
				__func__, ch_name);
		return;
	}
	snprintf(curr_dir_name, sizeof(curr_dir_name), "%s_%s",
					edge_name, xprt_name);

	ch_dbgfs.curr_name = curr_dir_name;
	ch_dbgfs.par_name = "channel";
	ch_dbgfs.b_dir_create = true;
	glink_debugfs_create(ch_name, NULL, &ch_dbgfs, NULL, false);

	ch_dbgfs.par_name = ch_dbgfs.curr_name;
	ch_dbgfs.curr_name = ch_name;
	ch_dbgfs.b_dir_create = false;
	glink_debugfs_create("stats", glink_dfs_update_ch_stats,
				&ch_dbgfs, (void *)ch_ctx, false);
	glink_debugfs_create("intents", glink_dfs_update_ch_intent,
			&ch_dbgfs, (void *)ch_ctx, false);
}
EXPORT_SYMBOL(glink_debugfs_add_channel);

/**
 * glink_debugfs_add_xprt() - create transport specifc files & folder in
 *			      debugfs when new transport is registerd
 * @xprt_ctx:		pointer to the transport_context
 *
 * This function is invoked when a new transport is registered. It creates the
 * folders & other files in debugfs for that transport
 */
void glink_debugfs_add_xprt(struct glink_core_xprt_ctx *xprt_ctx)
{
	struct glink_dbgfs xprt_dbgfs;
	char *xprt_name;
	char *edge_name;
	char curr_dir_name[GLINK_DBGFS_NAME_SIZE];

	if (xprt_ctx == NULL)
		GLINK_ERR("%s: Transport Context is NULL\n", __func__);
	xprt_name = glink_get_xprt_name(xprt_ctx);
	edge_name = glink_get_xprt_edge_name(xprt_ctx);
	if (!xprt_name || !edge_name) {
		GLINK_ERR("%s: xprt name or edge name is NULL\n", __func__);
		return;
	}
	snprintf(curr_dir_name, sizeof(curr_dir_name), "%s_%s",
					edge_name, xprt_name);
	xprt_dbgfs.par_name = "glink";
	xprt_dbgfs.curr_name = "xprt";
	xprt_dbgfs.b_dir_create = true;
	glink_debugfs_create(curr_dir_name, NULL, &xprt_dbgfs, NULL, false);
	xprt_dbgfs.curr_name = "channel";
	glink_debugfs_create(curr_dir_name, NULL, &xprt_dbgfs, NULL, false);
}
EXPORT_SYMBOL(glink_debugfs_add_xprt);

/**
 * glink_dfs_create_channel_list() - create & update the channel details
 * s:	pointer to seq_file
 *
 * This function updates channel details in debugfs
 * file present in /glink/channel/channels
 */
static void glink_dfs_create_channel_list(struct seq_file *s)
{
	struct xprt_ctx_iterator xprt_iter;
	struct ch_ctx_iterator ch_iter;

	struct glink_core_xprt_ctx *xprt_ctx;
	struct channel_ctx *ch_ctx;
	int count = 0;
	/*
	* formatted, human readable channel state output, ie:
	* NAME               |LCID|RCID|XPRT|EDGE|LSTATE |RSTATE|LINT-Q|RINT-Q|
	* --------------------------------------------------------------------
	* LOCAL_LOOPBACK_CLNT|2   |1  |lloop|local|OPENED|OPENED|5     |6    |
	* N.B. Number of TX & RX Packets not implemented yet. -ENOSYS is printed
	*/
	seq_printf(s, "%-20s|%-4s|%-4s|%-10s|%-6s|%-7s|%-7s|%-5s|%-5s|\n",
								"NAME",
								"LCID",
								"RCID",
								"XPRT",
								"EDGE",
								"LSTATE",
								"RSTATE",
								"LINTQ",
								"RINTQ");
	seq_puts(s,
		"-------------------------------------------------------------------------------\n");
	glink_xprt_ctx_iterator_init(&xprt_iter);
	xprt_ctx = glink_xprt_ctx_iterator_next(&xprt_iter);

	while (xprt_ctx != NULL) {
		glink_ch_ctx_iterator_init(&ch_iter, xprt_ctx);
		ch_ctx = glink_ch_ctx_iterator_next(&ch_iter);
		while (ch_ctx != NULL) {
			count++;
			seq_printf(s, "%-20s|%-4i|%-4i|%-10s|%-6s|%-7s|",
					glink_get_ch_name(ch_ctx),
					glink_get_ch_lcid(ch_ctx),
					glink_get_ch_rcid(ch_ctx),
					glink_get_ch_xprt_name(ch_ctx),
					glink_get_ch_edge_name(ch_ctx),
					glink_get_ch_lstate(ch_ctx));
			seq_printf(s, "%-7s|%-5i|%-5i|\n",
			(glink_get_ch_rstate(ch_ctx) ? "OPENED" : "CLOSED"),
			glink_get_ch_lintents_queued(ch_ctx),
			glink_get_ch_rintents_queued(ch_ctx));

			ch_ctx = glink_ch_ctx_iterator_next(&ch_iter);
		}
		glink_ch_ctx_iterator_end(&ch_iter, xprt_ctx);
		xprt_ctx = glink_xprt_ctx_iterator_next(&xprt_iter);
	}

	glink_xprt_ctx_iterator_end(&xprt_iter);
}

/**
 * glink_dfs_create_xprt_list() - create & update the transport details
 * @s:	pointer to seq_file
 *
 * This function updates channel details in debugfs file present
 * in /glink/xprt/xprts
 */
static void glink_dfs_create_xprt_list(struct seq_file *s)
{
	struct xprt_ctx_iterator xprt_iter;
	struct glink_core_xprt_ctx *xprt_ctx;
	const struct glink_core_version  *gver;
	uint32_t version;
	uint32_t features;
	int count = 0;
	/*
	* formatted, human readable channel state output, ie:
	* XPRT_NAME|REMOTE    |STATE|VERSION |FEATURES|
	* ---------------------------------------------
	* smd_trans|lpass     |2    |0       |1       |
	* smem     |mpss      |0    |0       |0       |
	*/
	seq_printf(s, "%-20s|%-20s|%-6s|%-8s|%-8s|\n",
							"XPRT_NAME",
							"REMOTE",
							"STATE",
							"VERSION",
							"FEATURES");
	seq_puts(s,
		"-------------------------------------------------------------------------------\n");
	glink_xprt_ctx_iterator_init(&xprt_iter);
	xprt_ctx = glink_xprt_ctx_iterator_next(&xprt_iter);

	while (xprt_ctx != NULL) {
		count++;
		seq_printf(s, "%-20s|%-20s|",
					glink_get_xprt_name(xprt_ctx),
					glink_get_xprt_edge_name(xprt_ctx));
		gver = glink_get_xprt_version_features(xprt_ctx);
		if (gver != NULL) {
			version = gver->version;
			features = gver->features;
			seq_printf(s, "%-6s|%-8i|%-8i|\n",
					glink_get_xprt_state(xprt_ctx),
					version,
					features);
		} else {
			seq_printf(s, "%-6s|%-8i|%-8i|\n",
					glink_get_xprt_state(xprt_ctx),
					-ENODATA,
					-ENODATA);
		}
		xprt_ctx = glink_xprt_ctx_iterator_next(&xprt_iter);

	}

	glink_xprt_ctx_iterator_end(&xprt_iter);
}

/**
 * glink_dfs_update_list() - update the internally maintained dentry linked list
 * @curr_dent:	pointer to the current dentry object
 * @parent:	pointer to the parent dentry object
 * @curr:	current directory name
 * @par_dir:	parent directory name
 */
void glink_dfs_update_list(struct dentry *curr_dent, struct dentry *parent,
			const char *curr, const char *par_dir)
{
	struct glink_dbgfs_dent *dbgfs_dent_s;
	if (curr_dent != NULL) {
		dbgfs_dent_s = kzalloc(sizeof(struct glink_dbgfs_dent),
				GFP_KERNEL);
		if (dbgfs_dent_s != NULL) {
			INIT_LIST_HEAD(&dbgfs_dent_s->file_list);
			spin_lock_init(&dbgfs_dent_s->file_list_lock_lhb0);
			dbgfs_dent_s->parent = parent;
			dbgfs_dent_s->self = curr_dent;
			strlcpy(dbgfs_dent_s->self_name,
				curr, strlen(curr) + 1);
			strlcpy(dbgfs_dent_s->par_name, par_dir,
					strlen(par_dir) + 1);
			mutex_lock(&dent_list_lock_lha0);
			list_add_tail(&dbgfs_dent_s->list_node, &dent_list);
			mutex_unlock(&dent_list_lock_lha0);
		}
	} else {
		GLINK_DBG("%s:create directory failed for par:curr [%s:%s]\n",
				__func__, par_dir, curr);
	}
	return;
}

/**
 * glink_remove_dfs_entry() - remove the the entries from dent_list
 * @entry:	pointer to the glink_dbgfs_dent structure
 *
 * This function removes the removes the entries from internally maintained
 * linked list of dentries. It also deletes the file list and associated memory
 * if present.
 */
void glink_remove_dfs_entry(struct glink_dbgfs_dent *entry)
{
	struct glink_dbgfs_data *fentry, *fentry_temp;
	unsigned long flags;

	if (entry == NULL)
		return;
	if (!list_empty(&entry->file_list)) {
		spin_lock_irqsave(&entry->file_list_lock_lhb0, flags);
		list_for_each_entry_safe(fentry, fentry_temp,
				&entry->file_list, flist) {
			if (fentry->b_priv_free_req)
				kfree(fentry->priv_data);
			list_del(&fentry->flist);
			kfree(fentry);
			fentry = NULL;
		}
		spin_unlock_irqrestore(&entry->file_list_lock_lhb0, flags);
	}
	list_del(&entry->list_node);
	kfree(entry);
	entry = NULL;
}

/**
 * glink_debugfs_remove_recur() - remove the the directory & files recursively
 * @rm_dfs:	pointer to the structure glink_dbgfs
 *
 * This function removes the files & directories below the given directory.
 * This also takes care of freeing any memory associated with the debugfs file.
 */
void glink_debugfs_remove_recur(struct glink_dbgfs *rm_dfs)
{
	const char *c_dir_name;
	const char *p_dir_name;
	struct glink_dbgfs_dent *entry, *entry_temp;
	struct dentry *par_dent = NULL;

	if (rm_dfs == NULL)
		return;

	c_dir_name = rm_dfs->curr_name;
	p_dir_name = rm_dfs->par_name;

	mutex_lock(&dent_list_lock_lha0);
	list_for_each_entry_safe(entry, entry_temp, &dent_list, list_node) {
		if (!strcmp(entry->par_name, c_dir_name)) {
			glink_remove_dfs_entry(entry);
		} else if (!strcmp(entry->self_name, c_dir_name)
				&& !strcmp(entry->par_name, p_dir_name)) {
			par_dent = entry->self;
			glink_remove_dfs_entry(entry);
		}
	}
	mutex_unlock(&dent_list_lock_lha0);
	if (par_dent != NULL)
		debugfs_remove_recursive(par_dent);
}
EXPORT_SYMBOL(glink_debugfs_remove_recur);

/**
 * glink_debugfs_create() - create the debugfs file
 * @name:	debugfs file name
 * @show:	pointer to the actual function which will be invoked upon
 *		opening this file.
 * @dir:	pointer to a structure debugfs_dir
 * dbgfs_data:	pointer to any private data need to be associated with debugfs
 * b_free_req:	boolean value to decide to free the memory associated with
 *		@dbgfs_data during deletion of the file
 *
 * Return:	pointer to the file/directory created, NULL in case of error
 *
 * This function checks which directory will be used to create the debugfs file
 * and calls glink_dfs_create_file. Anybody who intend to allocate some memory
 * for the dbgfs_data and required to free it in deletion, need to set
 * b_free_req to true. Otherwise, there will be a memory leak.
 */
struct dentry *glink_debugfs_create(const char *name,
		void (*show)(struct seq_file *),
		struct glink_dbgfs *dir, void *dbgfs_data, bool b_free_req)
{
	struct dentry *parent =  NULL;
	struct dentry *dent = NULL;
	struct glink_dbgfs_dent *entry;
	struct glink_dbgfs_data *file_data;
	const char *c_dir_name;
	const char *p_dir_name;
	unsigned long flags;

	if (dir == NULL) {
		GLINK_ERR("%s: debugfs_dir strucutre is null\n", __func__);
		return NULL;
	}
	c_dir_name = dir->curr_name;
	p_dir_name = dir->par_name;

	mutex_lock(&dent_list_lock_lha0);
	list_for_each_entry(entry, &dent_list, list_node)
		if (!strcmp(entry->par_name, p_dir_name)
				&& !strcmp(entry->self_name, c_dir_name)) {
			parent = entry->self;
			break;
		}
	mutex_unlock(&dent_list_lock_lha0);
	p_dir_name = c_dir_name;
	c_dir_name = name;
	if (parent != NULL) {
		if (dir->b_dir_create) {
			dent = debugfs_create_dir(name, parent);
			if (dent != NULL)
				glink_dfs_update_list(dent, parent,
							c_dir_name, p_dir_name);
		} else {
			file_data = glink_dfs_create_file(name, parent, show,
							dbgfs_data, b_free_req);
			spin_lock_irqsave(&entry->file_list_lock_lhb0, flags);
			if (file_data != NULL)
				list_add_tail(&file_data->flist,
						&entry->file_list);
			spin_unlock_irqrestore(&entry->file_list_lock_lhb0,
						flags);
		}
	} else {
		GLINK_DBG("%s: parent dentry is null for [%s]\n",
				__func__, name);
	}
	return dent;
}
EXPORT_SYMBOL(glink_debugfs_create);

/**
 * glink_debugfs_init() - initialize the glink debugfs directory structure
 *
 * Return:	0 in success otherwise appropriate error code
 *
 * This function initializes the debugfs directory for glink
 */
int glink_debugfs_init(void)
{
	struct glink_dbgfs dbgfs;

	/* fake parent name */
	dent = debugfs_create_dir("glink", NULL);
	if (IS_ERR_OR_NULL(dent))
		return PTR_ERR(dent);

	glink_dfs_update_list(dent, NULL, "glink", "root");

	dbgfs.b_dir_create = true;
	dbgfs.curr_name = "glink";
	dbgfs.par_name = "root";
	glink_debugfs_create("xprt", NULL, &dbgfs, NULL, false);
	glink_debugfs_create("channel", NULL, &dbgfs, NULL, false);

	dbgfs.curr_name = "channel";
	dbgfs.par_name = "glink";
	dbgfs.b_dir_create = false;
	glink_debugfs_create("channels", glink_dfs_create_channel_list,
				&dbgfs, NULL, false);
	dbgfs.curr_name = "xprt";
	glink_debugfs_create("xprts", glink_dfs_create_xprt_list,
				&dbgfs, NULL, false);

	return 0;
}
EXPORT_SYMBOL(glink_debugfs_init);

/**
 * glink_debugfs_exit() - removes the glink debugfs directory
 *
 * This function recursively remove all the debugfs directories
 * starting from dent
 */
void glink_debugfs_exit(void)
{
	if (dent != NULL)
		debugfs_remove_recursive(dent);
}
EXPORT_SYMBOL(glink_debugfs_exit);
#else
void glink_debugfs_remove_recur(struct glink_dbgfs *dfs) { }
EXPORT_SYMBOL(glink_debugfs_remove_recur);

void glink_debugfs_remove_channel(struct channel_ctx *ch_ctx,
			struct glink_core_xprt_ctx *xprt_ctx) { }
EXPORT_SYMBOL(glink_debugfs_remove_channel);

void glink_debugfs_add_channel(struct channel_ctx *ch_ctx,
		struct glink_core_xprt_ctx *xprt_ctx) { }
EXPORT_SYMBOL(glink_debugfs_add_channel);

void glink_debugfs_add_xprt(struct glink_core_xprt_ctx *xprt_ctx) { }
EXPORT_SYMBOL(glink_debugfs_add_xprt);

int glink_debugfs_init(void) { return 0; }
EXPORT_SYMBOL(glink_debugfs_init);

void glink_debugfs_exit(void) { }
EXPORT_SYMBOL(glink_debugfs_exit);
#endif /* CONFIG_DEBUG_FS */
