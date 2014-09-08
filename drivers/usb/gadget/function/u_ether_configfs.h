/*
 * u_ether_configfs.h
 *
 * Utility definitions for configfs support in USB Ethernet functions
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __U_ETHER_CONFIGFS_H
#define __U_ETHER_CONFIGFS_H

#define USB_ETHERNET_CONFIGFS_ITEM(_f_)					\
	CONFIGFS_ATTR_STRUCT(f_##_f_##_opts);				\
	CONFIGFS_ATTR_OPS(f_##_f_##_opts);				\
									\
	static void _f_##_attr_release(struct config_item *item)	\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
									\
		usb_put_function_instance(&opts->func_inst);		\
	}								\
									\
	static struct configfs_item_operations _f_##_item_ops = {	\
		.release	= _f_##_attr_release,			\
		.show_attribute = f_##_f_##_opts_attr_show,		\
		.store_attribute = f_##_f_##_opts_attr_store,		\
	}

#define USB_ETHERNET_CONFIGFS_ITEM_ATTR_DEV_ADDR(_f_)			\
	static ssize_t _f_##_opts_dev_addr_show(struct f_##_f_##_opts *opts, \
						char *page)		\
	{								\
		int result;						\
									\
		mutex_lock(&opts->lock);				\
		result = gether_get_dev_addr(opts->net, page, PAGE_SIZE); \
		mutex_unlock(&opts->lock);				\
									\
		return result;						\
	}								\
									\
	static ssize_t _f_##_opts_dev_addr_store(struct f_##_f_##_opts *opts, \
						 const char *page, size_t len)\
	{								\
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
	static struct f_##_f_##_opts_attribute f_##_f_##_opts_dev_addr = \
		__CONFIGFS_ATTR(dev_addr, S_IRUGO | S_IWUSR,		\
				_f_##_opts_dev_addr_show,		\
				_f_##_opts_dev_addr_store)

#define USB_ETHERNET_CONFIGFS_ITEM_ATTR_HOST_ADDR(_f_)			\
	static ssize_t _f_##_opts_host_addr_show(struct f_##_f_##_opts *opts, \
						 char *page)		\
	{								\
		int result;						\
									\
		mutex_lock(&opts->lock);				\
		result = gether_get_host_addr(opts->net, page, PAGE_SIZE); \
		mutex_unlock(&opts->lock);				\
									\
		return result;						\
	}								\
									\
	static ssize_t _f_##_opts_host_addr_store(struct f_##_f_##_opts *opts, \
						  const char *page, size_t len)\
	{								\
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
	static struct f_##_f_##_opts_attribute f_##_f_##_opts_host_addr = \
		__CONFIGFS_ATTR(host_addr, S_IRUGO | S_IWUSR,		\
				_f_##_opts_host_addr_show,		\
				_f_##_opts_host_addr_store)

#define USB_ETHERNET_CONFIGFS_ITEM_ATTR_QMULT(_f_)			\
	static ssize_t _f_##_opts_qmult_show(struct f_##_f_##_opts *opts, \
					     char *page)		\
	{								\
		unsigned qmult;						\
									\
		mutex_lock(&opts->lock);				\
		qmult = gether_get_qmult(opts->net);			\
		mutex_unlock(&opts->lock);				\
		return sprintf(page, "%d", qmult);			\
	}								\
									\
	static ssize_t _f_##_opts_qmult_store(struct f_##_f_##_opts *opts, \
					      const char *page, size_t len)\
	{								\
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
	static struct f_##_f_##_opts_attribute f_##_f_##_opts_qmult =	\
		__CONFIGFS_ATTR(qmult, S_IRUGO | S_IWUSR,		\
				_f_##_opts_qmult_show,		\
				_f_##_opts_qmult_store)

#define USB_ETHERNET_CONFIGFS_ITEM_ATTR_IFNAME(_f_)			\
	static ssize_t _f_##_opts_ifname_show(struct f_##_f_##_opts *opts, \
					      char *page)		\
	{								\
		int ret;						\
									\
		mutex_lock(&opts->lock);				\
		ret = gether_get_ifname(opts->net, page, PAGE_SIZE);	\
		mutex_unlock(&opts->lock);				\
									\
		return ret;						\
	}								\
									\
	static struct f_##_f_##_opts_attribute f_##_f_##_opts_ifname =	\
		__CONFIGFS_ATTR_RO(ifname, _f_##_opts_ifname_show)

#endif /* __U_ETHER_CONFIGFS_H */
