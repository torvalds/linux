/* SPDX-License-Identifier: GPL-2.0 */
/*
 * u_ether_configfs.h
 *
 * Utility definitions for configfs support in USB Ethernet functions
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 */

#ifndef __U_ETHER_CONFIGFS_H
#define __U_ETHER_CONFIGFS_H

#include <linux/cleanup.h>
#include <linux/hex.h>
#include <linux/if_ether.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>

#define USB_ETHERNET_CONFIGFS_ITEM(_f_)					\
	static void _f_##_attr_release(struct config_item *item)	\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
									\
		usb_put_function_instance(&opts->func_inst);		\
	}								\
									\
	static const struct configfs_item_operations _f_##_item_ops = {	\
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
	static ssize_t _f_##_opts_ifname_store(struct config_item *item, \
					       const char *page, size_t len)\
	{								\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);	\
		int ret = -EBUSY;					\
									\
		mutex_lock(&opts->lock);				\
		if (!opts->refcnt)					\
			ret = gether_set_ifname(opts->net, page, len);	\
		mutex_unlock(&opts->lock);				\
		return ret ?: len;					\
	}								\
									\
	CONFIGFS_ATTR(_f_##_opts_, ifname)

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
		int ret = -EINVAL;					\
		u8 val;							\
									\
		mutex_lock(&opts->lock);				\
		if (sscanf(page, "%02hhx", &val) > 0) {			\
			opts->_n_ = val;				\
			ret = len;					\
		}							\
		mutex_unlock(&opts->lock);				\
									\
		return ret;						\
	}								\
									\
	CONFIGFS_ATTR(_f_##_opts_, _n_)

#define USB_ETHER_OPTS_ITEM(_f_)						\
	static void _f_##_attr_release(struct config_item *item)		\
	{									\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);		\
										\
		usb_put_function_instance(&opts->func_inst);			\
	}									\
										\
	static struct configfs_item_operations _f_##_item_ops = {		\
		.release	= _f_##_attr_release,				\
	}

#define USB_ETHER_OPTS_ATTR_DEV_ADDR(_f_)					\
	static ssize_t _f_##_opts_dev_addr_show(struct config_item *item,	\
						char *page)			\
	{									\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);		\
										\
		guard(mutex)(&opts->lock);					\
		return sysfs_emit(page, "%pM\n", opts->net_opts.dev_mac);	\
	}									\
										\
	static ssize_t _f_##_opts_dev_addr_store(struct config_item *item,	\
						 const char *page, size_t len)	\
	{									\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);		\
		u8 new_addr[ETH_ALEN];						\
		const char *p = page;						\
										\
		guard(mutex)(&opts->lock);					\
		if (opts->refcnt)						\
			return -EBUSY;						\
										\
		for (int i = 0; i < ETH_ALEN; i++) {				\
			unsigned char num;					\
			if ((*p == '.') || (*p == ':'))				\
				p++;						\
			num = hex_to_bin(*p++) << 4;				\
			num |= hex_to_bin(*p++);				\
			new_addr[i] = num;					\
		}								\
		if (!is_valid_ether_addr(new_addr))				\
			return -EINVAL;						\
		memcpy(opts->net_opts.dev_mac, new_addr, ETH_ALEN);		\
		opts->net_opts.addr_assign_type = NET_ADDR_SET;			\
		return len;							\
	}									\
										\
	CONFIGFS_ATTR(_f_##_opts_, dev_addr)

#define USB_ETHER_OPTS_ATTR_HOST_ADDR(_f_)					\
	static ssize_t _f_##_opts_host_addr_show(struct config_item *item,	\
						 char *page)			\
	{									\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);		\
										\
		guard(mutex)(&opts->lock);					\
		return sysfs_emit(page, "%pM\n", opts->net_opts.host_mac);	\
	}									\
										\
	static ssize_t _f_##_opts_host_addr_store(struct config_item *item,	\
						  const char *page, size_t len)	\
	{									\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);		\
		u8 new_addr[ETH_ALEN];						\
		const char *p = page;						\
										\
		guard(mutex)(&opts->lock);					\
		if (opts->refcnt)						\
			return -EBUSY;						\
										\
		for (int i = 0; i < ETH_ALEN; i++) {				\
			unsigned char num;					\
			if ((*p == '.') || (*p == ':'))				\
				p++;						\
			num = hex_to_bin(*p++) << 4;				\
			num |= hex_to_bin(*p++);				\
			new_addr[i] = num;					\
		}								\
		if (!is_valid_ether_addr(new_addr))				\
			return -EINVAL;						\
		memcpy(opts->net_opts.host_mac, new_addr, ETH_ALEN);		\
		return len;							\
	}									\
										\
	CONFIGFS_ATTR(_f_##_opts_, host_addr)

#define USB_ETHER_OPTS_ATTR_QMULT(_f_)						\
	static ssize_t _f_##_opts_qmult_show(struct config_item *item,		\
					     char *page)			\
	{									\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);		\
										\
		guard(mutex)(&opts->lock);					\
		return sysfs_emit(page, "%u\n", opts->net_opts.qmult);		\
	}									\
										\
	static ssize_t _f_##_opts_qmult_store(struct config_item *item,		\
					      const char *page, size_t len)	\
	{									\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);		\
		u32 val;							\
		int ret;							\
										\
		guard(mutex)(&opts->lock);					\
		if (opts->refcnt)						\
			return -EBUSY;						\
										\
		ret = kstrtou32(page, 0, &val);					\
		if (ret)							\
			return ret;						\
										\
		opts->net_opts.qmult = val;					\
		return len;							\
	}									\
										\
	CONFIGFS_ATTR(_f_##_opts_, qmult)

#define USB_ETHER_OPTS_ATTR_IFNAME(_f_)						\
	static ssize_t _f_##_opts_ifname_show(struct config_item *item,		\
					      char *page)			\
	{									\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);		\
		const char *name;						\
										\
		guard(mutex)(&opts->lock);					\
		rtnl_lock();							\
		if (opts->net_opts.ifname_set)					\
			name = opts->net_opts.name;				\
		else if (opts->net)						\
			name = netdev_name(opts->net);				\
		else								\
			name = "(inactive net_device)";				\
		rtnl_unlock();							\
		return sysfs_emit(page, "%s\n", name);				\
	}									\
										\
	static ssize_t _f_##_opts_ifname_store(struct config_item *item,	\
					       const char *page, size_t len)	\
	{									\
		struct f_##_f_##_opts *opts = to_f_##_f_##_opts(item);		\
		char tmp[IFNAMSIZ];						\
		const char *p;							\
		size_t c_len = len;						\
										\
		if (c_len > 0 && page[c_len - 1] == '\n')			\
			c_len--;						\
										\
		if (c_len >= sizeof(tmp))					\
			return -E2BIG;						\
										\
		strscpy(tmp, page, c_len + 1);					\
		if (!dev_valid_name(tmp))					\
			return -EINVAL;						\
										\
		/* Require exactly one %d */					\
		p = strchr(tmp, '%');						\
		if (!p || p[1] != 'd' || strchr(p + 2, '%'))			\
			return -EINVAL;						\
										\
		guard(mutex)(&opts->lock);					\
		if (opts->refcnt)						\
			return -EBUSY;						\
		strscpy(opts->net_opts.name, tmp, sizeof(opts->net_opts.name));	\
		opts->net_opts.ifname_set = true;				\
		return len;							\
	}									\
										\
	CONFIGFS_ATTR(_f_##_opts_, ifname)

#endif /* __U_ETHER_CONFIGFS_H */
