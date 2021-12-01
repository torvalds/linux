/*
 * u_uac.h - Utility definitions for UAC function
 *
 * Copyright (C) 2016 Ruslan Bilovol <ruslan.bilovol@gmail.com>
 * Copyright (C) 2017 Julian Scheel <julian@juss.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __U_UAC_H
#define __U_UAC_H

#include <linux/usb/composite.h>
#include "u_audio.h"

#define UAC_DEF_CCHMASK		0x3
#define UAC_DEF_CSRATE		48000
#define UAC_DEF_CSSIZE		2
#define UAC_DEF_CFU		0
#define UAC_DEF_PCHMASK		0x3
#define UAC_DEF_PSRATE		48000
#define UAC_DEF_PSSIZE		2
#define UAC_DEF_PFU		0
#define UAC_DEF_REQ_NUM		2

#define UAC1_OUT_EP_MAX_PACKET_SIZE 200

#define EPIN_EN(_opts) ((_opts)->p_chmask != 0)
#define EPOUT_EN(_opts) ((_opts)->c_chmask != 0)
#define EPIN_FU(_opts) ((_opts)->p_feature_unit != 0)
#define EPOUT_FU(_opts) ((_opts)->c_feature_unit != 0)

struct f_uac_opts {
	struct usb_function_instance	func_inst;
	int				c_chmask;
	int				c_srate[UAC_MAX_RATES];
	int				c_srate_active;
	int				c_ssize;
	int				c_feature_unit;
	int				p_chmask;
	int				p_srate[UAC_MAX_RATES];
	int				p_srate_active;
	int				p_ssize;
	int				p_feature_unit;
	int				req_number;
	unsigned			bound:1;

	struct mutex			lock;
	int				refcnt;
};

#define UAC_ATTRIBUTE(name)						\
static ssize_t f_uac_opts_##name##_show(				\
					  struct config_item *item,	\
					  char *page)			\
{									\
	struct f_uac_opts *opts = to_f_uac_opts(item);		\
	int result;							\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", opts->name);			\
	mutex_unlock(&opts->lock);					\
									\
	return result;							\
}									\
									\
static ssize_t f_uac_opts_##name##_store(				\
					  struct config_item *item,	\
					  const char *page, size_t len)	\
{									\
	struct f_uac_opts *opts = to_f_uac_opts(item);		\
	int ret;							\
	u32 num;							\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtou32(page, 0, &num);					\
	if (ret)							\
		goto end;						\
									\
	opts->name = num;						\
	ret = len;							\
									\
end:									\
	mutex_unlock(&opts->lock);					\
	return ret;							\
}									\
									\
CONFIGFS_ATTR(f_uac_opts_, name)

#define UAC_RATE_ATTRIBUTE(name)					\
static ssize_t f_uac_opts_##name##_show(struct config_item *item,	\
					 char *page)			\
{									\
	struct f_uac_opts *opts = to_f_uac_opts(item);			\
	int result = 0;							\
	int i;								\
									\
	mutex_lock(&opts->lock);					\
	page[0] = '\0';							\
	for (i = 0; i < UAC_MAX_RATES; i++) {				\
		if (opts->name[i] == 0)					\
			continue;					\
		result += sprintf(page + strlen(page), "%u,",		\
				opts->name[i]);				\
	}								\
	if (strlen(page) > 0)						\
		page[strlen(page) - 1] = '\n';				\
	mutex_unlock(&opts->lock);					\
									\
	return result;							\
}									\
									\
static ssize_t f_uac_opts_##name##_store(struct config_item *item,	\
					  const char *page, size_t len)	\
{									\
	struct f_uac_opts *opts = to_f_uac_opts(item);			\
	char *split_page = NULL;					\
	int ret = -EINVAL;						\
	char *token;							\
	u32 num;							\
	int i;								\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	i = 0;								\
	memset(opts->name, 0x00, sizeof(opts->name));			\
	split_page = kstrdup(page, GFP_KERNEL);				\
	while ((token = strsep(&split_page, ",")) != NULL) {		\
		ret = kstrtou32(token, 0, &num);			\
		if (ret)						\
			goto end;					\
									\
		opts->name[i++] = num;					\
		opts->name##_active = num;				\
		ret = len;						\
	};								\
									\
end:									\
	kfree(split_page);						\
	mutex_unlock(&opts->lock);					\
	return ret;							\
}									\
									\
CONFIGFS_ATTR(f_uac_opts_, name)

struct f_uac {
	struct g_audio g_audio;
	u8 ac_intf, as_in_intf, as_out_intf;
	u8 ac_alt, as_in_alt, as_out_alt;	/* needed for get_alt() */
	int ctl_id;

	struct list_head cs;
	u8 set_cmd;
	u8 get_cmd;
	struct usb_audio_control *set_con;
	struct usb_audio_control *get_con;
};

static inline struct f_uac *func_to_uac(struct usb_function *f)
{
	return container_of(f, struct f_uac, g_audio.func);
}

static inline
struct f_uac_opts *g_audio_to_uac_opts(struct g_audio *agdev)
{
	return container_of(agdev->func.fi, struct f_uac_opts, func_inst);
}

static inline struct f_uac_opts *to_f_uac_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_uac_opts,
			    func_inst.group);
}

static inline void f_uac_attr_release(struct config_item *item)
{
	struct f_uac_opts *opts = to_f_uac_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

#endif /* __U_UAC_H */
