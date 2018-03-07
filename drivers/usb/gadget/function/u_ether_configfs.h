// SPDX-License-Identifier: GPL-2.0
/*
 * u_ether_configfs.h
 *
 * Utility definitions for configfs support in USB Ethernet functions
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 */

#ifndef __U_ETHER_CONFIGFS_H
#define __U_ETHER_CONFIGFS_H

#define USB_ETHERNET_CONFIGFS_ITEM(_f_)					\
	static void _f_##_attr_release(struct config_item *item)	\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
									\
		usb_put_function_instance(&opts->func_inst);		\
	}								\
									\
	static struct configfs_item_operations _f_##_item_ops = {	\
		.release	= _f_##_attr_release,			\
	}

#define USB_ETHERNET_CONFIGFS_ITEM_ATTR_DEV_ADDR(_f_)			\
	static ssize_t _f_##_opts_dev_addr_show(struct config_item *item, \
						char *page)		\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
		int result;						\
									\
		mutex_lock(&opts->lock);				\
		result = gether_get_dev_addr(opts->net, page, PAGE_SIZE); \
		mutex_unlock(&opts->lock);				\
									\
		return result;						\
	}								\
									\
	static ssize_t _f_##_opts_dev_addr_store(struct config_item *item, \
						 const char *page, size_t len)\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
		int ret;						\
									\
		mutex_lock(&opts->lock);				\
		if (opts->refcnt) {					\
			mutex_unlock(&opts->lock);			\
			return -EBUSY;					\
		}							\
									\
		ret = gether_set_dev_addr(opts->net, page);		\
		mutex_unlock(&opts->lock);				\
		if (!ret)						\
			ret = len;					\
		return ret;						\
	}								\
									\
	CONFIGFS_ATTR(_f_##_opts_, dev_addr)

#define USB_ETHERNET_CONFIGFS_ITEM_ATTR_HOST_ADDR(_f_)			\
	static ssize_t _f_##_opts_host_addr_show(struct config_item *item, \
						 char *page)		\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
		int result;						\
									\
		mutex_lock(&opts->lock);				\
		result = gether_get_host_addr(opts->net, page, PAGE_SIZE); \
		mutex_unlock(&opts->lock);				\
									\
		return result;						\
	}								\
									\
	static ssize_t _f_##_opts_host_addr_store(struct config_item *item, \
						  const char *page, size_t len)\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
		int ret;						\
									\
		mutex_lock(&opts->lock);				\
		if (opts->refcnt) {					\
			mutex_unlock(&opts->lock);			\
			return -EBUSY;					\
		}							\
									\
		ret = gether_set_host_addr(opts->net, page);		\
		mutex_unlock(&opts->lock);				\
		if (!ret)						\
			ret = len;					\
		return ret;						\
	}								\
									\
	CONFIGFS_ATTR(_f_##_opts_, host_addr)

#define USB_ETHERNET_CONFIGFS_ITEM_ATTR_QMULT(_f_)			\
	static ssize_t _f_##_opts_qmult_show(struct config_item *item,	\
					     char *page)		\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
		unsigned qmult;						\
									\
		mutex_lock(&opts->lock);				\
		qmult = gether_get_qmult(opts->net);			\
		mutex_unlock(&opts->lock);				\
		return sprintf(page, "%d\n", qmult);			\
	}								\
									\
	static ssize_t _f_##_opts_qmult_store(struct config_item *item, \
					      const char *page, size_t len)\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
		u8 val;							\
		int ret;						\
									\
		mutex_lock(&opts->lock);				\
		if (opts->refcnt) {					\
			ret = -EBUSY;					\
			goto out;					\
		}							\
									\
		ret = kstrtou8(page, 0, &val);				\
		if (ret)						\
			goto out;					\
									\
		gether_set_qmult(opts->net, val);			\
		ret = len;						\
out:									\
		mutex_unlock(&opts->lock);				\
		return ret;						\
	}								\
									\
	CONFIGFS_ATTR(_f_##_opts_, qmult)

#define USB_ETHERNET_CONFIGFS_ITEM_ATTR_IFNAME(_f_)			\
	static ssize_t _f_##_opts_ifname_show(struct config_item *item, \
					      char *page)		\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
		int ret;						\
									\
		mutex_lock(&opts->lock);				\
		ret = gether_get_ifname(opts->net, page, PAGE_SIZE);	\
		mutex_unlock(&opts->lock);				\
									\
		return ret;						\
	}								\
									\
	CONFIGFS_ATTR_RO(_f_##_opts_, ifname)

#define USB_ETHER_CONFIGFS_ITEM_ATTR_U8_RW(_f_, _n_)			\
	static ssize_t _f_##_opts_##_n_##_show(struct config_item *item,\
					       char *page)		\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
		int ret;						\
									\
		mutex_lock(&opts->lock);				\
		ret = sprintf(page, "%02x\n", opts->_n_);		\
		mutex_unlock(&opts->lock);				\
									\
		return ret;						\
	}								\
									\
	static ssize_t _f_##_opts_##_n_##_store(struct config_item *item,\
						const char *page,	\
						size_t len)		\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
		int ret;						\
		u8 val;							\
									\
		mutex_lock(&opts->lock);				\
		ret = sscanf(page, "%02hhx", &val);			\
		if (ret > 0) {						\
			opts->_n_ = val;				\
			ret = len;					\
		}							\
		mutex_unlock(&opts->lock);				\
									\
		return ret;						\
	}								\
									\
	CONFIGFS_ATTR(_f_##_opts_, _n_)

#endif /* __U_ETHER_CONFIGFS_H */
