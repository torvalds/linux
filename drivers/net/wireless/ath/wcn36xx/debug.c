/*
 * Copyright (c) 2013 Eugene Krasnikov <k.eugene.e@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include "wcn36xx.h"
#include "debug.h"
#include "pmc.h"

#ifdef CONFIG_WCN36XX_DEBUGFS

static ssize_t read_file_bool_bmps(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct wcn36xx *wcn = file->private_data;
	struct wcn36xx_vif *vif_priv = NULL;
	struct ieee80211_vif *vif = NULL;
	char buf[3];

	list_for_each_entry(vif_priv, &wcn->vif_list, list) {
			vif = container_of((void *)vif_priv,
				   struct ieee80211_vif,
				   drv_priv);
			if (NL80211_IFTYPE_STATION == vif->type) {
				if (vif_priv->pw_state == WCN36XX_BMPS)
					buf[0] = '1';
				else
					buf[0] = '0';
				break;
			}
	}
	buf[1] = '\n';
	buf[2] = 0x00;

	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t write_file_bool_bmps(struct file *file,
				    const char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct wcn36xx *wcn = file->private_data;
	struct wcn36xx_vif *vif_priv = NULL;
	struct ieee80211_vif *vif = NULL;

	char buf[32];
	int buf_size;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	switch (buf[0]) {
	case 'y':
	case 'Y':
	case '1':
		list_for_each_entry(vif_priv, &wcn->vif_list, list) {
			vif = container_of((void *)vif_priv,
				   struct ieee80211_vif,
				   drv_priv);
			if (NL80211_IFTYPE_STATION == vif->type) {
				wcn36xx_enable_keep_alive_null_packet(wcn, vif);
				wcn36xx_pmc_enter_bmps_state(wcn, vif);
			}
		}
		break;
	case 'n':
	case 'N':
	case '0':
		list_for_each_entry(vif_priv, &wcn->vif_list, list) {
			vif = container_of((void *)vif_priv,
				   struct ieee80211_vif,
				   drv_priv);
			if (NL80211_IFTYPE_STATION == vif->type)
				wcn36xx_pmc_exit_bmps_state(wcn, vif);
		}
		break;
	}

	return count;
}

static const struct file_operations fops_wcn36xx_bmps = {
	.open = simple_open,
	.read  =       read_file_bool_bmps,
	.write =       write_file_bool_bmps,
};

static ssize_t write_file_dump(struct file *file,
				    const char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct wcn36xx *wcn = file->private_data;
	char buf[255], *tmp;
	int buf_size;
	u32 arg[WCN36xx_MAX_DUMP_ARGS];
	int i;

	memset(buf, 0, sizeof(buf));
	memset(arg, 0, sizeof(arg));

	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	tmp = buf;

	for (i = 0; i < WCN36xx_MAX_DUMP_ARGS; i++) {
		char *begin;
		begin = strsep(&tmp, " ");
		if (begin == NULL)
			break;

		if (kstrtou32(begin, 0, &arg[i]) != 0)
			break;
	}

	wcn36xx_info("DUMP args is %d %d %d %d %d\n", arg[0], arg[1], arg[2],
		     arg[3], arg[4]);
	wcn36xx_smd_dump_cmd_req(wcn, arg[0], arg[1], arg[2], arg[3], arg[4]);

	return count;
}

static const struct file_operations fops_wcn36xx_dump = {
	.open = simple_open,
	.write =       write_file_dump,
};

#define ADD_FILE(name, mode, fop, priv_data)		\
	do {							\
		struct dentry *d;				\
		d = debugfs_create_file(__stringify(name),	\
					mode, dfs->rootdir,	\
					priv_data, fop);	\
		dfs->file_##name.dentry = d;			\
		if (IS_ERR(d)) {				\
			wcn36xx_warn("Create the debugfs entry failed");\
			dfs->file_##name.dentry = NULL;		\
		}						\
	} while (0)


void wcn36xx_debugfs_init(struct wcn36xx *wcn)
{
	struct wcn36xx_dfs_entry *dfs = &wcn->dfs;

	dfs->rootdir = debugfs_create_dir(KBUILD_MODNAME,
					  wcn->hw->wiphy->debugfsdir);
	if (IS_ERR(dfs->rootdir)) {
		wcn36xx_warn("Create the debugfs failed\n");
		dfs->rootdir = NULL;
	}

	ADD_FILE(bmps_switcher, S_IRUSR | S_IWUSR,
		 &fops_wcn36xx_bmps, wcn);
	ADD_FILE(dump, S_IWUSR, &fops_wcn36xx_dump, wcn);
}

void wcn36xx_debugfs_exit(struct wcn36xx *wcn)
{
	struct wcn36xx_dfs_entry *dfs = &wcn->dfs;
	debugfs_remove_recursive(dfs->rootdir);
}

#endif /* CONFIG_WCN36XX_DEBUGFS */
