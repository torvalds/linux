/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014 Intel Corporation
 * Copyright (C) 2013-2014 Intel Mobile Communications GmbH
 */
#define MVM_DEBUGFS_READ_FILE_OPS(name)					\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.read = iwl_dbgfs_##name##_read,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define MVM_DEBUGFS_WRITE_WRAPPER(name, buflen, argtype)		\
static ssize_t _iwl_dbgfs_##name##_write(struct file *file,		\
					 const char __user *user_buf,	\
					 size_t count, loff_t *ppos)	\
{									\
	argtype *arg = file->private_data;				\
	char buf[buflen] = {};						\
	size_t buf_size = min(count, sizeof(buf) -  1);			\
									\
	if (copy_from_user(buf, user_buf, buf_size))			\
		return -EFAULT;						\
									\
	return iwl_dbgfs_##name##_write(arg, buf, buf_size, ppos);	\
}									\

#define _MVM_DEBUGFS_READ_WRITE_FILE_OPS(name, buflen, argtype)		\
MVM_DEBUGFS_WRITE_WRAPPER(name, buflen, argtype)			\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.write = _iwl_dbgfs_##name##_write,				\
	.read = iwl_dbgfs_##name##_read,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};

#define _MVM_DEBUGFS_WRITE_FILE_OPS(name, buflen, argtype)		\
MVM_DEBUGFS_WRITE_WRAPPER(name, buflen, argtype)			\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.write = _iwl_dbgfs_##name##_write,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};
