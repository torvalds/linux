/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include "iface.h"
#include "sta.h"

#define MLD_DEBUGFS_OPEN_WRAPPER(name, buflen, argtype)			\
struct dbgfs_##name##_data {						\
	argtype *arg;							\
	bool read_done;							\
	ssize_t rlen;							\
	char buf[buflen];						\
};									\
static int _iwl_dbgfs_##name##_open(struct inode *inode,		\
				    struct file *file)			\
{									\
	struct dbgfs_##name##_data *data;				\
									\
	if ((file->f_flags & O_ACCMODE) == O_RDWR)			\
		return -EOPNOTSUPP;					\
									\
	data = kzalloc(sizeof(*data), GFP_KERNEL);			\
	if (!data)							\
		return -ENOMEM;						\
									\
	data->read_done = false;					\
	data->arg = inode->i_private;					\
	file->private_data = data;					\
									\
	return 0;							\
}

#define MLD_DEBUGFS_READ_WRAPPER(name)					\
static ssize_t _iwl_dbgfs_##name##_read(struct file *file,		\
					char __user *user_buf,		\
					size_t count, loff_t *ppos)	\
{									\
	struct dbgfs_##name##_data *data = file->private_data;		\
									\
	if (!data->read_done) {						\
		data->read_done = true;					\
		data->rlen = iwl_dbgfs_##name##_read(data->arg,		\
						     sizeof(data->buf),\
						     data->buf);	\
	}								\
									\
	if (data->rlen < 0)						\
		return data->rlen;					\
	return simple_read_from_buffer(user_buf, count, ppos,		\
				       data->buf, data->rlen);		\
}

static int _iwl_dbgfs_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

#define _MLD_DEBUGFS_READ_FILE_OPS(name, buflen, argtype)		\
MLD_DEBUGFS_OPEN_WRAPPER(name, buflen, argtype)				\
MLD_DEBUGFS_READ_WRAPPER(name)						\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.read = _iwl_dbgfs_##name##_read,				\
	.open = _iwl_dbgfs_##name##_open,				\
	.llseek = generic_file_llseek,					\
	.release = _iwl_dbgfs_release,					\
}

#define WIPHY_DEBUGFS_WRITE_HANDLER_WRAPPER(name)			\
static ssize_t iwl_dbgfs_##name##_write_handler(struct wiphy *wiphy,	\
				       struct file *file, char *buf,	\
				       size_t count, void *data)	\
{									\
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);		\
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);			\
	return iwl_dbgfs_##name##_write(mld, buf, count, data);		\
}

static inline struct iwl_mld *
iwl_mld_from_link_sta(struct ieee80211_link_sta *link_sta)
{
	struct ieee80211_vif *vif =
		iwl_mld_sta_from_mac80211(link_sta->sta)->vif;
	return iwl_mld_vif_from_mac80211(vif)->mld;
}

static inline struct iwl_mld *
iwl_mld_from_bss_conf(struct ieee80211_bss_conf *link)
{
	return iwl_mld_vif_from_mac80211(link->vif)->mld;
}

static inline struct iwl_mld *iwl_mld_from_vif(struct ieee80211_vif *vif)
{
	return iwl_mld_vif_from_mac80211(vif)->mld;
}

#define WIPHY_DEBUGFS_WRITE_WRAPPER(name, bufsz, objtype)		\
WIPHY_DEBUGFS_WRITE_HANDLER_WRAPPER(name)				\
static ssize_t __iwl_dbgfs_##name##_write(struct file *file,		\
					  const char __user *user_buf,	\
					  size_t count, loff_t *ppos)	\
{									\
	struct ieee80211_##objtype *arg = file->private_data;		\
	struct iwl_mld *mld = iwl_mld_from_##objtype(arg);		\
	char buf[bufsz] = {};						\
									\
	return wiphy_locked_debugfs_write(mld->wiphy, file,		\
				buf, sizeof(buf),			\
				user_buf, count,			\
				iwl_dbgfs_##name##_write_handler,	\
				arg);					\
}

#define WIPHY_DEBUGFS_WRITE_FILE_OPS(name, bufsz, objtype)		\
	WIPHY_DEBUGFS_WRITE_WRAPPER(name, bufsz, objtype)		\
	static const struct file_operations iwl_dbgfs_##name##_ops = {	\
		.write = __iwl_dbgfs_##name##_write,			\
		.open = simple_open,					\
		.llseek = generic_file_llseek,				\
	}

#define WIPHY_DEBUGFS_READ_HANDLER_WRAPPER_MLD(name)			\
static ssize_t iwl_dbgfs_##name##_read_handler(struct wiphy *wiphy,	\
				       struct file *file, char *buf,	\
				       size_t count, void *data)	\
{									\
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);		\
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);			\
	return iwl_dbgfs_##name##_read(mld, buf, count);		\
}

#define WIPHY_DEBUGFS_WRITE_HANDLER_WRAPPER_MLD(name)			\
static ssize_t iwl_dbgfs_##name##_write_handler(struct wiphy *wiphy,	\
				       struct file *file, char *buf,	\
				       size_t count, void *data)	\
{									\
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);		\
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);			\
	return iwl_dbgfs_##name##_write(mld, buf, count);		\
}

#define WIPHY_DEBUGFS_WRITE_WRAPPER_MLD(name)				\
WIPHY_DEBUGFS_WRITE_HANDLER_WRAPPER_MLD(name)				\
static ssize_t __iwl_dbgfs_##name##_write(struct file *file,		\
					  const char __user *user_buf,	\
					  size_t count, loff_t *ppos)	\
{									\
	struct dbgfs_##name##_data *data = file->private_data;		\
	struct iwl_mld *mld = data->arg;				\
									\
	return wiphy_locked_debugfs_write(mld->wiphy, file,		\
				data->buf, sizeof(data->buf),		\
				user_buf, count,			\
				iwl_dbgfs_##name##_write_handler,	\
				NULL);					\
}

#define WIPHY_DEBUGFS_READ_WRAPPER_MLD(name)				\
WIPHY_DEBUGFS_READ_HANDLER_WRAPPER_MLD(name)				\
static ssize_t __iwl_dbgfs_##name##_read(struct file *file,		\
					char __user *user_buf,		\
					size_t count, loff_t *ppos)	\
{									\
	struct dbgfs_##name##_data *data = file->private_data;		\
	struct iwl_mld *mld = data->arg;				\
									\
	if (!data->read_done) {						\
		data->read_done = true;					\
		data->rlen = wiphy_locked_debugfs_read(mld->wiphy,	\
				file, data->buf, sizeof(data->buf),	\
				user_buf, count, ppos,			\
				iwl_dbgfs_##name##_read_handler, NULL);	\
		return data->rlen;					\
	}								\
									\
	if (data->rlen < 0)						\
		return data->rlen;					\
	return simple_read_from_buffer(user_buf, count, ppos,		\
				       data->buf, data->rlen);		\
}

#define WIPHY_DEBUGFS_READ_FILE_OPS_MLD(name, bufsz)			\
	MLD_DEBUGFS_OPEN_WRAPPER(name, bufsz, struct iwl_mld)		\
	WIPHY_DEBUGFS_READ_WRAPPER_MLD(name)				\
	static const struct file_operations iwl_dbgfs_##name##_ops = {	\
		.read = __iwl_dbgfs_##name##_read,			\
		.open = _iwl_dbgfs_##name##_open,			\
		.llseek = generic_file_llseek,				\
		.release = _iwl_dbgfs_release,				\
	}

#define WIPHY_DEBUGFS_WRITE_FILE_OPS_MLD(name, bufsz)			\
	MLD_DEBUGFS_OPEN_WRAPPER(name, bufsz, struct iwl_mld)		\
	WIPHY_DEBUGFS_WRITE_WRAPPER_MLD(name)				\
	static const struct file_operations iwl_dbgfs_##name##_ops = {	\
		.write = __iwl_dbgfs_##name##_write,			\
		.open = _iwl_dbgfs_##name##_open,			\
		.llseek = generic_file_llseek,				\
		.release = _iwl_dbgfs_release,				\
	}

#define WIPHY_DEBUGFS_READ_WRITE_FILE_OPS_MLD(name, bufsz)		\
	MLD_DEBUGFS_OPEN_WRAPPER(name, bufsz, struct iwl_mld)		\
	WIPHY_DEBUGFS_WRITE_WRAPPER_MLD(name)				\
	WIPHY_DEBUGFS_READ_WRAPPER_MLD(name)				\
	static const struct file_operations iwl_dbgfs_##name##_ops = {	\
		.write = __iwl_dbgfs_##name##_write,			\
		.read = __iwl_dbgfs_##name##_read,			\
		.open = _iwl_dbgfs_##name##_open,			\
		.llseek = generic_file_llseek,				\
		.release = _iwl_dbgfs_release,				\
	}

#define WIPHY_DEBUGFS_WRITE_WRAPPER_IEEE80211(name, bufsz, objtype)	\
WIPHY_DEBUGFS_WRITE_HANDLER_WRAPPER(name)				\
static ssize_t _iwl_dbgfs_##name##_write(struct file *file,		\
					  const char __user *user_buf,	\
					  size_t count, loff_t *ppos)	\
{									\
	struct dbgfs_##name##_data *data = file->private_data;		\
	struct ieee80211_##objtype *arg = data->arg;			\
	struct iwl_mld *mld = iwl_mld_from_##objtype(arg);		\
	char buf[bufsz] = {};						\
									\
	return wiphy_locked_debugfs_write(mld->wiphy, file,		\
				buf, sizeof(buf),			\
				user_buf, count,			\
				iwl_dbgfs_##name##_write_handler,	\
				arg);					\
}

#define IEEE80211_WIPHY_DEBUGFS_READ_WRITE_FILE_OPS(name, bufsz, objtype) \
	MLD_DEBUGFS_OPEN_WRAPPER(name, bufsz, struct ieee80211_##objtype) \
	WIPHY_DEBUGFS_WRITE_WRAPPER_IEEE80211(name, bufsz, objtype)	  \
	MLD_DEBUGFS_READ_WRAPPER(name)					  \
	static const struct file_operations iwl_dbgfs_##name##_ops = {	  \
		.write = _iwl_dbgfs_##name##_write,			  \
		.read = _iwl_dbgfs_##name##_read,			  \
		.open = _iwl_dbgfs_##name##_open,			  \
		.llseek = generic_file_llseek,				  \
		.release = _iwl_dbgfs_release,				  \
	}
