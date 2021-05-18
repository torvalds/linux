// SPDX-License-Identifier: GPL-2.0
/*
 * uvc_configfs.c
 *
 * Configfs support for the uvc function.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 */

#include <linux/sort.h>

#include "u_uvc.h"
#include "uvc_configfs.h"

/* -----------------------------------------------------------------------------
 * Global Utility Structures and Macros
 */

#define UVCG_STREAMING_CONTROL_SIZE	1

#define UVC_ATTR(prefix, cname, aname) \
static struct configfs_attribute prefix##attr_##cname = { \
	.ca_name	= __stringify(aname),				\
	.ca_mode	= S_IRUGO | S_IWUGO,				\
	.ca_owner	= THIS_MODULE,					\
	.show		= prefix##cname##_show,				\
	.store		= prefix##cname##_store,			\
}

#define UVC_ATTR_RO(prefix, cname, aname) \
static struct configfs_attribute prefix##attr_##cname = { \
	.ca_name	= __stringify(aname),				\
	.ca_mode	= S_IRUGO,					\
	.ca_owner	= THIS_MODULE,					\
	.show		= prefix##cname##_show,				\
}

#define le8_to_cpu(x)	(x)
#define cpu_to_le8(x)	(x)

static int uvcg_config_compare_u32(const void *l, const void *r)
{
	u32 li = *(const u32 *)l;
	u32 ri = *(const u32 *)r;

	return li < ri ? -1 : li == ri ? 0 : 1;
}

static inline struct f_uvc_opts *to_f_uvc_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_uvc_opts,
			    func_inst.group);
}

struct uvcg_config_group_type {
	struct config_item_type type;
	const char *name;
	const struct uvcg_config_group_type **children;
	int (*create_children)(struct config_group *group);
};

static void uvcg_config_item_release(struct config_item *item)
{
	struct config_group *group = to_config_group(item);

	kfree(group);
}

static struct configfs_item_operations uvcg_config_item_ops = {
	.release	= uvcg_config_item_release,
};

static int uvcg_config_create_group(struct config_group *parent,
				    const struct uvcg_config_group_type *type);

static int uvcg_config_create_children(struct config_group *group,
				const struct uvcg_config_group_type *type)
{
	const struct uvcg_config_group_type **child;
	int ret;

	if (type->create_children)
		return type->create_children(group);

	for (child = type->children; child && *child; ++child) {
		ret = uvcg_config_create_group(group, *child);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int uvcg_config_create_group(struct config_group *parent,
				    const struct uvcg_config_group_type *type)
{
	struct config_group *group;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return -ENOMEM;

	config_group_init_type_name(group, type->name, &type->type);
	configfs_add_default_group(group, parent);

	return uvcg_config_create_children(group, type);
}

static void uvcg_config_remove_children(struct config_group *group)
{
	struct config_group *child, *n;

	list_for_each_entry_safe(child, n, &group->default_groups, group_entry) {
		list_del(&child->group_entry);
		uvcg_config_remove_children(child);
		config_item_put(&child->cg_item);
	}
}

/* -----------------------------------------------------------------------------
 * control/header/<NAME>
 * control/header
 */

DECLARE_UVC_HEADER_DESCRIPTOR(1);

struct uvcg_control_header {
	struct config_item		item;
	struct UVC_HEADER_DESCRIPTOR(1)	desc;
	unsigned			linked;
};

static struct uvcg_control_header *to_uvcg_control_header(struct config_item *item)
{
	return container_of(item, struct uvcg_control_header, item);
}

#define UVCG_CTRL_HDR_ATTR(cname, aname, bits, limit)			\
static ssize_t uvcg_control_header_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct uvcg_control_header *ch = to_uvcg_control_header(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &ch->item.ci_group->cg_subsys->su_mutex;\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = ch->item.ci_parent->ci_parent->ci_parent;		\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(ch->desc.aname));\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t								\
uvcg_control_header_##cname##_store(struct config_item *item,		\
			   const char *page, size_t len)		\
{									\
	struct uvcg_control_header *ch = to_uvcg_control_header(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &ch->item.ci_group->cg_subsys->su_mutex;\
	int ret;							\
	u##bits num;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = ch->item.ci_parent->ci_parent->ci_parent;		\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	if (ch->linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtou##bits(page, 0, &num);				\
	if (ret)							\
		goto end;						\
									\
	if (num > limit) {						\
		ret = -EINVAL;						\
		goto end;						\
	}								\
	ch->desc.aname = cpu_to_le##bits(num);				\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_control_header_, cname, aname)

UVCG_CTRL_HDR_ATTR(bcd_uvc, bcdUVC, 16, 0xffff);

UVCG_CTRL_HDR_ATTR(dw_clock_frequency, dwClockFrequency, 32, 0x7fffffff);

#undef UVCG_CTRL_HDR_ATTR

static struct configfs_attribute *uvcg_control_header_attrs[] = {
	&uvcg_control_header_attr_bcd_uvc,
	&uvcg_control_header_attr_dw_clock_frequency,
	NULL,
};

static const struct config_item_type uvcg_control_header_type = {
	.ct_item_ops	= &uvcg_config_item_ops,
	.ct_attrs	= uvcg_control_header_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_item *uvcg_control_header_make(struct config_group *group,
						    const char *name)
{
	struct uvcg_control_header *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->desc.bLength			= UVC_DT_HEADER_SIZE(1);
	h->desc.bDescriptorType		= USB_DT_CS_INTERFACE;
	h->desc.bDescriptorSubType	= UVC_VC_HEADER;
	h->desc.bcdUVC			= cpu_to_le16(0x0110);
	h->desc.dwClockFrequency	= cpu_to_le32(48000000);

	config_item_init_type_name(&h->item, name, &uvcg_control_header_type);

	return &h->item;
}

static struct configfs_group_operations uvcg_control_header_grp_ops = {
	.make_item		= uvcg_control_header_make,
};

static const struct uvcg_config_group_type uvcg_control_header_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_group_ops	= &uvcg_control_header_grp_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "header",
};

/* -----------------------------------------------------------------------------
 * control/processing/default
 */

#define UVCG_DEFAULT_PROCESSING_ATTR(cname, aname, bits)		\
static ssize_t uvcg_default_processing_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct config_group *group = to_config_group(item);		\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;		\
	struct uvc_processing_unit_descriptor *pd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
	pd = &opts->uvc_processing;					\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(pd->aname));	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_processing_, cname, aname)

UVCG_DEFAULT_PROCESSING_ATTR(b_unit_id, bUnitID, 8);
UVCG_DEFAULT_PROCESSING_ATTR(b_source_id, bSourceID, 8);
UVCG_DEFAULT_PROCESSING_ATTR(w_max_multiplier, wMaxMultiplier, 16);
UVCG_DEFAULT_PROCESSING_ATTR(i_processing, iProcessing, 8);

#undef UVCG_DEFAULT_PROCESSING_ATTR

static ssize_t uvcg_default_processing_bm_controls_show(
	struct config_item *item, char *page)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct uvc_processing_unit_descriptor *pd;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	pd = &opts->uvc_processing;

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; i < pd->bControlSize; ++i) {
		result += sprintf(pg, "%u\n", pd->bmControls[i]);
		pg = page + result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return result;
}

UVC_ATTR_RO(uvcg_default_processing_, bm_controls, bmControls);

static struct configfs_attribute *uvcg_default_processing_attrs[] = {
	&uvcg_default_processing_attr_b_unit_id,
	&uvcg_default_processing_attr_b_source_id,
	&uvcg_default_processing_attr_w_max_multiplier,
	&uvcg_default_processing_attr_bm_controls,
	&uvcg_default_processing_attr_i_processing,
	NULL,
};

static const struct uvcg_config_group_type uvcg_default_processing_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_processing_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "default",
};

/* -----------------------------------------------------------------------------
 * control/processing
 */

static const struct uvcg_config_group_type uvcg_processing_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "processing",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_default_processing_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * control/terminal/camera/default
 */

#define UVCG_DEFAULT_CAMERA_ATTR(cname, aname, bits)			\
static ssize_t uvcg_default_camera_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct config_group *group = to_config_group(item);		\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;		\
	struct uvc_camera_terminal_descriptor *cd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent->	\
			ci_parent;					\
	opts = to_f_uvc_opts(opts_item);				\
	cd = &opts->uvc_camera_terminal;				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(cd->aname));	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
									\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_camera_, cname, aname)

UVCG_DEFAULT_CAMERA_ATTR(b_terminal_id, bTerminalID, 8);
UVCG_DEFAULT_CAMERA_ATTR(w_terminal_type, wTerminalType, 16);
UVCG_DEFAULT_CAMERA_ATTR(b_assoc_terminal, bAssocTerminal, 8);
UVCG_DEFAULT_CAMERA_ATTR(i_terminal, iTerminal, 8);
UVCG_DEFAULT_CAMERA_ATTR(w_objective_focal_length_min, wObjectiveFocalLengthMin,
			 16);
UVCG_DEFAULT_CAMERA_ATTR(w_objective_focal_length_max, wObjectiveFocalLengthMax,
			 16);
UVCG_DEFAULT_CAMERA_ATTR(w_ocular_focal_length, wOcularFocalLength,
			 16);

#undef UVCG_DEFAULT_CAMERA_ATTR

static ssize_t uvcg_default_camera_bm_controls_show(
	struct config_item *item, char *page)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct uvc_camera_terminal_descriptor *cd;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent->
			ci_parent;
	opts = to_f_uvc_opts(opts_item);
	cd = &opts->uvc_camera_terminal;

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; i < cd->bControlSize; ++i) {
		result += sprintf(pg, "%u\n", cd->bmControls[i]);
		pg = page + result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);
	return result;
}

UVC_ATTR_RO(uvcg_default_camera_, bm_controls, bmControls);

static struct configfs_attribute *uvcg_default_camera_attrs[] = {
	&uvcg_default_camera_attr_b_terminal_id,
	&uvcg_default_camera_attr_w_terminal_type,
	&uvcg_default_camera_attr_b_assoc_terminal,
	&uvcg_default_camera_attr_i_terminal,
	&uvcg_default_camera_attr_w_objective_focal_length_min,
	&uvcg_default_camera_attr_w_objective_focal_length_max,
	&uvcg_default_camera_attr_w_ocular_focal_length,
	&uvcg_default_camera_attr_bm_controls,
	NULL,
};

static const struct uvcg_config_group_type uvcg_default_camera_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_camera_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "default",
};

/* -----------------------------------------------------------------------------
 * control/terminal/camera
 */

static const struct uvcg_config_group_type uvcg_camera_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "camera",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_default_camera_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * control/terminal/output/default
 */

#define UVCG_DEFAULT_OUTPUT_ATTR(cname, aname, bits)			\
static ssize_t uvcg_default_output_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct config_group *group = to_config_group(item);		\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;		\
	struct uvc_output_terminal_descriptor *cd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = group->cg_item.ci_parent->ci_parent->		\
			ci_parent->ci_parent;				\
	opts = to_f_uvc_opts(opts_item);				\
	cd = &opts->uvc_output_terminal;				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(cd->aname));	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
									\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_output_, cname, aname)

UVCG_DEFAULT_OUTPUT_ATTR(b_terminal_id, bTerminalID, 8);
UVCG_DEFAULT_OUTPUT_ATTR(w_terminal_type, wTerminalType, 16);
UVCG_DEFAULT_OUTPUT_ATTR(b_assoc_terminal, bAssocTerminal, 8);
UVCG_DEFAULT_OUTPUT_ATTR(b_source_id, bSourceID, 8);
UVCG_DEFAULT_OUTPUT_ATTR(i_terminal, iTerminal, 8);

#undef UVCG_DEFAULT_OUTPUT_ATTR

static struct configfs_attribute *uvcg_default_output_attrs[] = {
	&uvcg_default_output_attr_b_terminal_id,
	&uvcg_default_output_attr_w_terminal_type,
	&uvcg_default_output_attr_b_assoc_terminal,
	&uvcg_default_output_attr_b_source_id,
	&uvcg_default_output_attr_i_terminal,
	NULL,
};

static const struct uvcg_config_group_type uvcg_default_output_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_output_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "default",
};

/* -----------------------------------------------------------------------------
 * control/terminal/output
 */

static const struct uvcg_config_group_type uvcg_output_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "output",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_default_output_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * control/terminal
 */

static const struct uvcg_config_group_type uvcg_terminal_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "terminal",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_camera_grp_type,
		&uvcg_output_grp_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * control/class/{fs|ss}
 */

struct uvcg_control_class_group {
	struct config_group group;
	const char *name;
};

static inline struct uvc_descriptor_header
**uvcg_get_ctl_class_arr(struct config_item *i, struct f_uvc_opts *o)
{
	struct uvcg_control_class_group *group =
		container_of(i, struct uvcg_control_class_group,
			     group.cg_item);

	if (!strcmp(group->name, "fs"))
		return o->uvc_fs_control_cls;

	if (!strcmp(group->name, "ss"))
		return o->uvc_ss_control_cls;

	return NULL;
}

static int uvcg_control_class_allow_link(struct config_item *src,
					 struct config_item *target)
{
	struct config_item *control, *header;
	struct f_uvc_opts *opts;
	struct mutex *su_mutex = &src->ci_group->cg_subsys->su_mutex;
	struct uvc_descriptor_header **class_array;
	struct uvcg_control_header *target_hdr;
	int ret = -EINVAL;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	control = src->ci_parent->ci_parent;
	header = config_group_find_item(to_config_group(control), "header");
	if (!header || target->ci_parent != header)
		goto out;

	opts = to_f_uvc_opts(control->ci_parent);

	mutex_lock(&opts->lock);

	class_array = uvcg_get_ctl_class_arr(src, opts);
	if (!class_array)
		goto unlock;
	if (opts->refcnt || class_array[0]) {
		ret = -EBUSY;
		goto unlock;
	}

	target_hdr = to_uvcg_control_header(target);
	++target_hdr->linked;
	class_array[0] = (struct uvc_descriptor_header *)&target_hdr->desc;
	ret = 0;

unlock:
	mutex_unlock(&opts->lock);
out:
	config_item_put(header);
	mutex_unlock(su_mutex);
	return ret;
}

static void uvcg_control_class_drop_link(struct config_item *src,
					struct config_item *target)
{
	struct config_item *control, *header;
	struct f_uvc_opts *opts;
	struct mutex *su_mutex = &src->ci_group->cg_subsys->su_mutex;
	struct uvc_descriptor_header **class_array;
	struct uvcg_control_header *target_hdr;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	control = src->ci_parent->ci_parent;
	header = config_group_find_item(to_config_group(control), "header");
	if (!header || target->ci_parent != header)
		goto out;

	opts = to_f_uvc_opts(control->ci_parent);

	mutex_lock(&opts->lock);

	class_array = uvcg_get_ctl_class_arr(src, opts);
	if (!class_array || opts->refcnt)
		goto unlock;

	target_hdr = to_uvcg_control_header(target);
	--target_hdr->linked;
	class_array[0] = NULL;

unlock:
	mutex_unlock(&opts->lock);
out:
	config_item_put(header);
	mutex_unlock(su_mutex);
}

static struct configfs_item_operations uvcg_control_class_item_ops = {
	.release	= uvcg_config_item_release,
	.allow_link	= uvcg_control_class_allow_link,
	.drop_link	= uvcg_control_class_drop_link,
};

static const struct config_item_type uvcg_control_class_type = {
	.ct_item_ops	= &uvcg_control_class_item_ops,
	.ct_owner	= THIS_MODULE,
};

/* -----------------------------------------------------------------------------
 * control/class
 */

static int uvcg_control_class_create_children(struct config_group *parent)
{
	static const char * const names[] = { "fs", "ss" };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(names); ++i) {
		struct uvcg_control_class_group *group;

		group = kzalloc(sizeof(*group), GFP_KERNEL);
		if (!group)
			return -ENOMEM;

		group->name = names[i];

		config_group_init_type_name(&group->group, group->name,
					    &uvcg_control_class_type);
		configfs_add_default_group(&group->group, parent);
	}

	return 0;
}

static const struct uvcg_config_group_type uvcg_control_class_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "class",
	.create_children = uvcg_control_class_create_children,
};

/* -----------------------------------------------------------------------------
 * control
 */

static ssize_t uvcg_default_control_b_interface_number_show(
	struct config_item *item, char *page)
{
	struct config_group *group = to_config_group(item);
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct config_item *opts_item;
	struct f_uvc_opts *opts;
	int result = 0;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = item->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	result += sprintf(page, "%u\n", opts->control_interface);
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return result;
}

UVC_ATTR_RO(uvcg_default_control_, b_interface_number, bInterfaceNumber);

static struct configfs_attribute *uvcg_default_control_attrs[] = {
	&uvcg_default_control_attr_b_interface_number,
	NULL,
};

static const struct uvcg_config_group_type uvcg_control_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_control_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "control",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_control_header_grp_type,
		&uvcg_processing_grp_type,
		&uvcg_terminal_grp_type,
		&uvcg_control_class_grp_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * streaming/uncompressed
 * streaming/mjpeg
 */

static const char * const uvcg_format_names[] = {
	"uncompressed",
	"mjpeg",
};

enum uvcg_format_type {
	UVCG_UNCOMPRESSED = 0,
	UVCG_MJPEG,
};

struct uvcg_format {
	struct config_group	group;
	enum uvcg_format_type	type;
	unsigned		linked;
	unsigned		num_frames;
	__u8			bmaControls[UVCG_STREAMING_CONTROL_SIZE];
};

static struct uvcg_format *to_uvcg_format(struct config_item *item)
{
	return container_of(to_config_group(item), struct uvcg_format, group);
}

static ssize_t uvcg_format_bma_controls_show(struct uvcg_format *f, char *page)
{
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &f->group.cg_subsys->su_mutex;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = f->group.cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	result = sprintf(pg, "0x");
	pg += result;
	for (i = 0; i < UVCG_STREAMING_CONTROL_SIZE; ++i) {
		result += sprintf(pg, "%x\n", f->bmaControls[i]);
		pg = page + result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);
	return result;
}

static ssize_t uvcg_format_bma_controls_store(struct uvcg_format *ch,
					      const char *page, size_t len)
{
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &ch->group.cg_subsys->su_mutex;
	int ret = -EINVAL;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = ch->group.cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	if (ch->linked || opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	if (len < 4 || *page != '0' ||
	    (*(page + 1) != 'x' && *(page + 1) != 'X'))
		goto end;
	ret = hex2bin(ch->bmaControls, page + 2, 1);
	if (ret < 0)
		goto end;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

struct uvcg_format_ptr {
	struct uvcg_format	*fmt;
	struct list_head	entry;
};

/* -----------------------------------------------------------------------------
 * streaming/header/<NAME>
 * streaming/header
 */

struct uvcg_streaming_header {
	struct config_item				item;
	struct uvc_input_header_descriptor		desc;
	unsigned					linked;
	struct list_head				formats;
	unsigned					num_fmt;
};

static struct uvcg_streaming_header *to_uvcg_streaming_header(struct config_item *item)
{
	return container_of(item, struct uvcg_streaming_header, item);
}

static void uvcg_format_set_indices(struct config_group *fmt);

static int uvcg_streaming_header_allow_link(struct config_item *src,
					    struct config_item *target)
{
	struct mutex *su_mutex = &src->ci_group->cg_subsys->su_mutex;
	struct config_item *opts_item;
	struct f_uvc_opts *opts;
	struct uvcg_streaming_header *src_hdr;
	struct uvcg_format *target_fmt = NULL;
	struct uvcg_format_ptr *format_ptr;
	int i, ret = -EINVAL;

	src_hdr = to_uvcg_streaming_header(src);
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = src->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);

	if (src_hdr->linked) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * Linking is only allowed to direct children of the format nodes
	 * (streaming/uncompressed or streaming/mjpeg nodes). First check that
	 * the grand-parent of the target matches the grand-parent of the source
	 * (the streaming node), and then verify that the target parent is a
	 * format node.
	 */
	if (src->ci_parent->ci_parent != target->ci_parent->ci_parent)
		goto out;

	for (i = 0; i < ARRAY_SIZE(uvcg_format_names); ++i) {
		if (!strcmp(target->ci_parent->ci_name, uvcg_format_names[i]))
			break;
	}

	if (i == ARRAY_SIZE(uvcg_format_names))
		goto out;

	target_fmt = container_of(to_config_group(target), struct uvcg_format,
				  group);
	if (!target_fmt)
		goto out;

	uvcg_format_set_indices(to_config_group(target));

	format_ptr = kzalloc(sizeof(*format_ptr), GFP_KERNEL);
	if (!format_ptr) {
		ret = -ENOMEM;
		goto out;
	}
	ret = 0;
	format_ptr->fmt = target_fmt;
	list_add_tail(&format_ptr->entry, &src_hdr->formats);
	++src_hdr->num_fmt;
	++target_fmt->linked;

out:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

static void uvcg_streaming_header_drop_link(struct config_item *src,
					   struct config_item *target)
{
	struct mutex *su_mutex = &src->ci_group->cg_subsys->su_mutex;
	struct config_item *opts_item;
	struct f_uvc_opts *opts;
	struct uvcg_streaming_header *src_hdr;
	struct uvcg_format *target_fmt = NULL;
	struct uvcg_format_ptr *format_ptr, *tmp;

	src_hdr = to_uvcg_streaming_header(src);
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = src->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	target_fmt = container_of(to_config_group(target), struct uvcg_format,
				  group);
	if (!target_fmt)
		goto out;

	list_for_each_entry_safe(format_ptr, tmp, &src_hdr->formats, entry)
		if (format_ptr->fmt == target_fmt) {
			list_del(&format_ptr->entry);
			kfree(format_ptr);
			--src_hdr->num_fmt;
			break;
		}

	--target_fmt->linked;

out:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
}

static struct configfs_item_operations uvcg_streaming_header_item_ops = {
	.release	= uvcg_config_item_release,
	.allow_link	= uvcg_streaming_header_allow_link,
	.drop_link	= uvcg_streaming_header_drop_link,
};

#define UVCG_STREAMING_HEADER_ATTR(cname, aname, bits)			\
static ssize_t uvcg_streaming_header_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct uvcg_streaming_header *sh = to_uvcg_streaming_header(item); \
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &sh->item.ci_group->cg_subsys->su_mutex;\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = sh->item.ci_parent->ci_parent->ci_parent;		\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(sh->desc.aname));\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_streaming_header_, cname, aname)

UVCG_STREAMING_HEADER_ATTR(bm_info, bmInfo, 8);
UVCG_STREAMING_HEADER_ATTR(b_terminal_link, bTerminalLink, 8);
UVCG_STREAMING_HEADER_ATTR(b_still_capture_method, bStillCaptureMethod, 8);
UVCG_STREAMING_HEADER_ATTR(b_trigger_support, bTriggerSupport, 8);
UVCG_STREAMING_HEADER_ATTR(b_trigger_usage, bTriggerUsage, 8);

#undef UVCG_STREAMING_HEADER_ATTR

static struct configfs_attribute *uvcg_streaming_header_attrs[] = {
	&uvcg_streaming_header_attr_bm_info,
	&uvcg_streaming_header_attr_b_terminal_link,
	&uvcg_streaming_header_attr_b_still_capture_method,
	&uvcg_streaming_header_attr_b_trigger_support,
	&uvcg_streaming_header_attr_b_trigger_usage,
	NULL,
};

static const struct config_item_type uvcg_streaming_header_type = {
	.ct_item_ops	= &uvcg_streaming_header_item_ops,
	.ct_attrs	= uvcg_streaming_header_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_item
*uvcg_streaming_header_make(struct config_group *group, const char *name)
{
	struct uvcg_streaming_header *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&h->formats);
	h->desc.bDescriptorType		= USB_DT_CS_INTERFACE;
	h->desc.bDescriptorSubType	= UVC_VS_INPUT_HEADER;
	h->desc.bTerminalLink		= 3;
	h->desc.bControlSize		= UVCG_STREAMING_CONTROL_SIZE;

	config_item_init_type_name(&h->item, name, &uvcg_streaming_header_type);

	return &h->item;
}

static struct configfs_group_operations uvcg_streaming_header_grp_ops = {
	.make_item		= uvcg_streaming_header_make,
};

static const struct uvcg_config_group_type uvcg_streaming_header_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_group_ops	= &uvcg_streaming_header_grp_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "header",
};

/* -----------------------------------------------------------------------------
 * streaming/<mode>/<format>/<NAME>
 */

struct uvcg_frame {
	struct config_item	item;
	enum uvcg_format_type	fmt_type;
	struct {
		u8	b_length;
		u8	b_descriptor_type;
		u8	b_descriptor_subtype;
		u8	b_frame_index;
		u8	bm_capabilities;
		u16	w_width;
		u16	w_height;
		u32	dw_min_bit_rate;
		u32	dw_max_bit_rate;
		u32	dw_max_video_frame_buffer_size;
		u32	dw_default_frame_interval;
		u8	b_frame_interval_type;
	} __attribute__((packed)) frame;
	u32 *dw_frame_interval;
};

static struct uvcg_frame *to_uvcg_frame(struct config_item *item)
{
	return container_of(item, struct uvcg_frame, item);
}

#define UVCG_FRAME_ATTR(cname, aname, bits) \
static ssize_t uvcg_frame_##cname##_show(struct config_item *item, char *page)\
{									\
	struct uvcg_frame *f = to_uvcg_frame(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &f->item.ci_group->cg_subsys->su_mutex;\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = f->item.ci_parent->ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", f->frame.cname);			\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t  uvcg_frame_##cname##_store(struct config_item *item,	\
					   const char *page, size_t len)\
{									\
	struct uvcg_frame *f = to_uvcg_frame(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct uvcg_format *fmt;					\
	struct mutex *su_mutex = &f->item.ci_group->cg_subsys->su_mutex;\
	typeof(f->frame.cname) num;					\
	int ret;							\
									\
	ret = kstrtou##bits(page, 0, &num);				\
	if (ret)							\
		return ret;						\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = f->item.ci_parent->ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
	fmt = to_uvcg_format(f->item.ci_parent);			\
									\
	mutex_lock(&opts->lock);					\
	if (fmt->linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	f->frame.cname = num;						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_frame_, cname, aname);

static ssize_t uvcg_frame_b_frame_index_show(struct config_item *item,
					     char *page)
{
	struct uvcg_frame *f = to_uvcg_frame(item);
	struct uvcg_format *fmt;
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct config_item *fmt_item;
	struct mutex *su_mutex = &f->item.ci_group->cg_subsys->su_mutex;
	int result;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	fmt_item = f->item.ci_parent;
	fmt = to_uvcg_format(fmt_item);

	if (!fmt->linked) {
		result = -EBUSY;
		goto out;
	}

	opts_item = fmt_item->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", f->frame.b_frame_index);
	mutex_unlock(&opts->lock);

out:
	mutex_unlock(su_mutex);
	return result;
}

UVC_ATTR_RO(uvcg_frame_, b_frame_index, bFrameIndex);

UVCG_FRAME_ATTR(bm_capabilities, bmCapabilities, 8);
UVCG_FRAME_ATTR(w_width, wWidth, 16);
UVCG_FRAME_ATTR(w_height, wHeight, 16);
UVCG_FRAME_ATTR(dw_min_bit_rate, dwMinBitRate, 32);
UVCG_FRAME_ATTR(dw_max_bit_rate, dwMaxBitRate, 32);
UVCG_FRAME_ATTR(dw_max_video_frame_buffer_size, dwMaxVideoFrameBufferSize, 32);
UVCG_FRAME_ATTR(dw_default_frame_interval, dwDefaultFrameInterval, 32);

#undef UVCG_FRAME_ATTR

static ssize_t uvcg_frame_dw_frame_interval_show(struct config_item *item,
						 char *page)
{
	struct uvcg_frame *frm = to_uvcg_frame(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &frm->item.ci_group->cg_subsys->su_mutex;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = frm->item.ci_parent->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; i < frm->frame.b_frame_interval_type; ++i) {
		result += sprintf(pg, "%u\n", frm->dw_frame_interval[i]);
		pg = page + result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);
	return result;
}

static inline int __uvcg_count_frm_intrv(char *buf, void *priv)
{
	++*((int *)priv);
	return 0;
}

static inline int __uvcg_fill_frm_intrv(char *buf, void *priv)
{
	u32 num, **interv;
	int ret;

	ret = kstrtou32(buf, 0, &num);
	if (ret)
		return ret;

	interv = priv;
	**interv = num;
	++*interv;

	return 0;
}

static int __uvcg_iter_frm_intrv(const char *page, size_t len,
				 int (*fun)(char *, void *), void *priv)
{
	/* sign, base 2 representation, newline, terminator */
	char buf[1 + sizeof(u32) * 8 + 1 + 1];
	const char *pg = page;
	int i, ret;

	if (!fun)
		return -EINVAL;

	while (pg - page < len) {
		i = 0;
		while (i < sizeof(buf) && (pg - page < len) &&
				*pg != '\0' && *pg != '\n')
			buf[i++] = *pg++;
		if (i == sizeof(buf))
			return -EINVAL;
		while ((pg - page < len) && (*pg == '\0' || *pg == '\n'))
			++pg;
		buf[i] = '\0';
		ret = fun(buf, priv);
		if (ret)
			return ret;
	}

	return 0;
}

static ssize_t uvcg_frame_dw_frame_interval_store(struct config_item *item,
						  const char *page, size_t len)
{
	struct uvcg_frame *ch = to_uvcg_frame(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct uvcg_format *fmt;
	struct mutex *su_mutex = &ch->item.ci_group->cg_subsys->su_mutex;
	int ret = 0, n = 0;
	u32 *frm_intrv, *tmp;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = ch->item.ci_parent->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	fmt = to_uvcg_format(ch->item.ci_parent);

	mutex_lock(&opts->lock);
	if (fmt->linked || opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = __uvcg_iter_frm_intrv(page, len, __uvcg_count_frm_intrv, &n);
	if (ret)
		goto end;

	tmp = frm_intrv = kcalloc(n, sizeof(u32), GFP_KERNEL);
	if (!frm_intrv) {
		ret = -ENOMEM;
		goto end;
	}

	ret = __uvcg_iter_frm_intrv(page, len, __uvcg_fill_frm_intrv, &tmp);
	if (ret) {
		kfree(frm_intrv);
		goto end;
	}

	kfree(ch->dw_frame_interval);
	ch->dw_frame_interval = frm_intrv;
	ch->frame.b_frame_interval_type = n;
	sort(ch->dw_frame_interval, n, sizeof(*ch->dw_frame_interval),
	     uvcg_config_compare_u32, NULL);
	ret = len;

end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

UVC_ATTR(uvcg_frame_, dw_frame_interval, dwFrameInterval);

static struct configfs_attribute *uvcg_frame_attrs[] = {
	&uvcg_frame_attr_b_frame_index,
	&uvcg_frame_attr_bm_capabilities,
	&uvcg_frame_attr_w_width,
	&uvcg_frame_attr_w_height,
	&uvcg_frame_attr_dw_min_bit_rate,
	&uvcg_frame_attr_dw_max_bit_rate,
	&uvcg_frame_attr_dw_max_video_frame_buffer_size,
	&uvcg_frame_attr_dw_default_frame_interval,
	&uvcg_frame_attr_dw_frame_interval,
	NULL,
};

static const struct config_item_type uvcg_frame_type = {
	.ct_item_ops	= &uvcg_config_item_ops,
	.ct_attrs	= uvcg_frame_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_item *uvcg_frame_make(struct config_group *group,
					   const char *name)
{
	struct uvcg_frame *h;
	struct uvcg_format *fmt;
	struct f_uvc_opts *opts;
	struct config_item *opts_item;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->frame.b_descriptor_type		= USB_DT_CS_INTERFACE;
	h->frame.b_frame_index			= 1;
	h->frame.w_width			= 640;
	h->frame.w_height			= 360;
	h->frame.dw_min_bit_rate		= 18432000;
	h->frame.dw_max_bit_rate		= 55296000;
	h->frame.dw_max_video_frame_buffer_size	= 460800;
	h->frame.dw_default_frame_interval	= 666666;

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	fmt = to_uvcg_format(&group->cg_item);
	if (fmt->type == UVCG_UNCOMPRESSED) {
		h->frame.b_descriptor_subtype = UVC_VS_FRAME_UNCOMPRESSED;
		h->fmt_type = UVCG_UNCOMPRESSED;
	} else if (fmt->type == UVCG_MJPEG) {
		h->frame.b_descriptor_subtype = UVC_VS_FRAME_MJPEG;
		h->fmt_type = UVCG_MJPEG;
	} else {
		mutex_unlock(&opts->lock);
		kfree(h);
		return ERR_PTR(-EINVAL);
	}
	++fmt->num_frames;
	mutex_unlock(&opts->lock);

	config_item_init_type_name(&h->item, name, &uvcg_frame_type);

	return &h->item;
}

static void uvcg_frame_drop(struct config_group *group, struct config_item *item)
{
	struct uvcg_format *fmt;
	struct f_uvc_opts *opts;
	struct config_item *opts_item;

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	fmt = to_uvcg_format(&group->cg_item);
	--fmt->num_frames;
	mutex_unlock(&opts->lock);

	config_item_put(item);
}

static void uvcg_format_set_indices(struct config_group *fmt)
{
	struct config_item *ci;
	unsigned int i = 1;

	list_for_each_entry(ci, &fmt->cg_children, ci_entry) {
		struct uvcg_frame *frm;

		if (ci->ci_type != &uvcg_frame_type)
			continue;

		frm = to_uvcg_frame(ci);
		frm->frame.b_frame_index = i++;
	}
}

/* -----------------------------------------------------------------------------
 * streaming/uncompressed/<NAME>
 */

struct uvcg_uncompressed {
	struct uvcg_format		fmt;
	struct uvc_format_uncompressed	desc;
};

static struct uvcg_uncompressed *to_uvcg_uncompressed(struct config_item *item)
{
	return container_of(
		container_of(to_config_group(item), struct uvcg_format, group),
		struct uvcg_uncompressed, fmt);
}

static struct configfs_group_operations uvcg_uncompressed_group_ops = {
	.make_item		= uvcg_frame_make,
	.drop_item		= uvcg_frame_drop,
};

static ssize_t uvcg_uncompressed_guid_format_show(struct config_item *item,
							char *page)
{
	struct uvcg_uncompressed *ch = to_uvcg_uncompressed(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &ch->fmt.group.cg_subsys->su_mutex;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = ch->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	memcpy(page, ch->desc.guidFormat, sizeof(ch->desc.guidFormat));
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return sizeof(ch->desc.guidFormat);
}

static ssize_t uvcg_uncompressed_guid_format_store(struct config_item *item,
						   const char *page, size_t len)
{
	struct uvcg_uncompressed *ch = to_uvcg_uncompressed(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &ch->fmt.group.cg_subsys->su_mutex;
	int ret;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = ch->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	if (ch->fmt.linked || opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	memcpy(ch->desc.guidFormat, page,
	       min(sizeof(ch->desc.guidFormat), len));
	ret = sizeof(ch->desc.guidFormat);

end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

UVC_ATTR(uvcg_uncompressed_, guid_format, guidFormat);

#define UVCG_UNCOMPRESSED_ATTR_RO(cname, aname, bits)			\
static ssize_t uvcg_uncompressed_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct uvcg_uncompressed *u = to_uvcg_uncompressed(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(u->desc.aname));\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_uncompressed_, cname, aname);

#define UVCG_UNCOMPRESSED_ATTR(cname, aname, bits)			\
static ssize_t uvcg_uncompressed_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct uvcg_uncompressed *u = to_uvcg_uncompressed(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(u->desc.aname));\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t								\
uvcg_uncompressed_##cname##_store(struct config_item *item,		\
				    const char *page, size_t len)	\
{									\
	struct uvcg_uncompressed *u = to_uvcg_uncompressed(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int ret;							\
	u8 num;								\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	if (u->fmt.linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtou8(page, 0, &num);					\
	if (ret)							\
		goto end;						\
									\
	u->desc.aname = num;						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_uncompressed_, cname, aname);

UVCG_UNCOMPRESSED_ATTR_RO(b_format_index, bFormatIndex, 8);
UVCG_UNCOMPRESSED_ATTR(b_bits_per_pixel, bBitsPerPixel, 8);
UVCG_UNCOMPRESSED_ATTR(b_default_frame_index, bDefaultFrameIndex, 8);
UVCG_UNCOMPRESSED_ATTR_RO(b_aspect_ratio_x, bAspectRatioX, 8);
UVCG_UNCOMPRESSED_ATTR_RO(b_aspect_ratio_y, bAspectRatioY, 8);
UVCG_UNCOMPRESSED_ATTR_RO(bm_interface_flags, bmInterfaceFlags, 8);

#undef UVCG_UNCOMPRESSED_ATTR
#undef UVCG_UNCOMPRESSED_ATTR_RO

static inline ssize_t
uvcg_uncompressed_bma_controls_show(struct config_item *item, char *page)
{
	struct uvcg_uncompressed *unc = to_uvcg_uncompressed(item);
	return uvcg_format_bma_controls_show(&unc->fmt, page);
}

static inline ssize_t
uvcg_uncompressed_bma_controls_store(struct config_item *item,
				     const char *page, size_t len)
{
	struct uvcg_uncompressed *unc = to_uvcg_uncompressed(item);
	return uvcg_format_bma_controls_store(&unc->fmt, page, len);
}

UVC_ATTR(uvcg_uncompressed_, bma_controls, bmaControls);

static struct configfs_attribute *uvcg_uncompressed_attrs[] = {
	&uvcg_uncompressed_attr_b_format_index,
	&uvcg_uncompressed_attr_guid_format,
	&uvcg_uncompressed_attr_b_bits_per_pixel,
	&uvcg_uncompressed_attr_b_default_frame_index,
	&uvcg_uncompressed_attr_b_aspect_ratio_x,
	&uvcg_uncompressed_attr_b_aspect_ratio_y,
	&uvcg_uncompressed_attr_bm_interface_flags,
	&uvcg_uncompressed_attr_bma_controls,
	NULL,
};

static const struct config_item_type uvcg_uncompressed_type = {
	.ct_item_ops	= &uvcg_config_item_ops,
	.ct_group_ops	= &uvcg_uncompressed_group_ops,
	.ct_attrs	= uvcg_uncompressed_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *uvcg_uncompressed_make(struct config_group *group,
						   const char *name)
{
	static char guid[] = {
		'Y',  'U',  'Y',  '2', 0x00, 0x00, 0x10, 0x00,
		 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
	};
	struct uvcg_uncompressed *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->desc.bLength			= UVC_DT_FORMAT_UNCOMPRESSED_SIZE;
	h->desc.bDescriptorType		= USB_DT_CS_INTERFACE;
	h->desc.bDescriptorSubType	= UVC_VS_FORMAT_UNCOMPRESSED;
	memcpy(h->desc.guidFormat, guid, sizeof(guid));
	h->desc.bBitsPerPixel		= 16;
	h->desc.bDefaultFrameIndex	= 1;
	h->desc.bAspectRatioX		= 0;
	h->desc.bAspectRatioY		= 0;
	h->desc.bmInterfaceFlags	= 0;
	h->desc.bCopyProtect		= 0;

	h->fmt.type = UVCG_UNCOMPRESSED;
	config_group_init_type_name(&h->fmt.group, name,
				    &uvcg_uncompressed_type);

	return &h->fmt.group;
}

static struct configfs_group_operations uvcg_uncompressed_grp_ops = {
	.make_group		= uvcg_uncompressed_make,
};

static const struct uvcg_config_group_type uvcg_uncompressed_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_group_ops	= &uvcg_uncompressed_grp_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "uncompressed",
};

/* -----------------------------------------------------------------------------
 * streaming/mjpeg/<NAME>
 */

struct uvcg_mjpeg {
	struct uvcg_format		fmt;
	struct uvc_format_mjpeg		desc;
};

static struct uvcg_mjpeg *to_uvcg_mjpeg(struct config_item *item)
{
	return container_of(
		container_of(to_config_group(item), struct uvcg_format, group),
		struct uvcg_mjpeg, fmt);
}

static struct configfs_group_operations uvcg_mjpeg_group_ops = {
	.make_item		= uvcg_frame_make,
	.drop_item		= uvcg_frame_drop,
};

#define UVCG_MJPEG_ATTR_RO(cname, aname, bits)				\
static ssize_t uvcg_mjpeg_##cname##_show(struct config_item *item, char *page)\
{									\
	struct uvcg_mjpeg *u = to_uvcg_mjpeg(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(u->desc.aname));\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_mjpeg_, cname, aname)

#define UVCG_MJPEG_ATTR(cname, aname, bits)				\
static ssize_t uvcg_mjpeg_##cname##_show(struct config_item *item, char *page)\
{									\
	struct uvcg_mjpeg *u = to_uvcg_mjpeg(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(u->desc.aname));\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t								\
uvcg_mjpeg_##cname##_store(struct config_item *item,			\
			   const char *page, size_t len)		\
{									\
	struct uvcg_mjpeg *u = to_uvcg_mjpeg(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int ret;							\
	u8 num;								\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	if (u->fmt.linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtou8(page, 0, &num);					\
	if (ret)							\
		goto end;						\
									\
	u->desc.aname = num;						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_mjpeg_, cname, aname)

UVCG_MJPEG_ATTR_RO(b_format_index, bFormatIndex, 8);
UVCG_MJPEG_ATTR(b_default_frame_index, bDefaultFrameIndex, 8);
UVCG_MJPEG_ATTR_RO(bm_flags, bmFlags, 8);
UVCG_MJPEG_ATTR_RO(b_aspect_ratio_x, bAspectRatioX, 8);
UVCG_MJPEG_ATTR_RO(b_aspect_ratio_y, bAspectRatioY, 8);
UVCG_MJPEG_ATTR_RO(bm_interface_flags, bmInterfaceFlags, 8);

#undef UVCG_MJPEG_ATTR
#undef UVCG_MJPEG_ATTR_RO

static inline ssize_t
uvcg_mjpeg_bma_controls_show(struct config_item *item, char *page)
{
	struct uvcg_mjpeg *u = to_uvcg_mjpeg(item);
	return uvcg_format_bma_controls_show(&u->fmt, page);
}

static inline ssize_t
uvcg_mjpeg_bma_controls_store(struct config_item *item,
				     const char *page, size_t len)
{
	struct uvcg_mjpeg *u = to_uvcg_mjpeg(item);
	return uvcg_format_bma_controls_store(&u->fmt, page, len);
}

UVC_ATTR(uvcg_mjpeg_, bma_controls, bmaControls);

static struct configfs_attribute *uvcg_mjpeg_attrs[] = {
	&uvcg_mjpeg_attr_b_format_index,
	&uvcg_mjpeg_attr_b_default_frame_index,
	&uvcg_mjpeg_attr_bm_flags,
	&uvcg_mjpeg_attr_b_aspect_ratio_x,
	&uvcg_mjpeg_attr_b_aspect_ratio_y,
	&uvcg_mjpeg_attr_bm_interface_flags,
	&uvcg_mjpeg_attr_bma_controls,
	NULL,
};

static const struct config_item_type uvcg_mjpeg_type = {
	.ct_item_ops	= &uvcg_config_item_ops,
	.ct_group_ops	= &uvcg_mjpeg_group_ops,
	.ct_attrs	= uvcg_mjpeg_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *uvcg_mjpeg_make(struct config_group *group,
						   const char *name)
{
	struct uvcg_mjpeg *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->desc.bLength			= UVC_DT_FORMAT_MJPEG_SIZE;
	h->desc.bDescriptorType		= USB_DT_CS_INTERFACE;
	h->desc.bDescriptorSubType	= UVC_VS_FORMAT_MJPEG;
	h->desc.bDefaultFrameIndex	= 1;
	h->desc.bAspectRatioX		= 0;
	h->desc.bAspectRatioY		= 0;
	h->desc.bmInterfaceFlags	= 0;
	h->desc.bCopyProtect		= 0;

	h->fmt.type = UVCG_MJPEG;
	config_group_init_type_name(&h->fmt.group, name,
				    &uvcg_mjpeg_type);

	return &h->fmt.group;
}

static struct configfs_group_operations uvcg_mjpeg_grp_ops = {
	.make_group		= uvcg_mjpeg_make,
};

static const struct uvcg_config_group_type uvcg_mjpeg_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_group_ops	= &uvcg_mjpeg_grp_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "mjpeg",
};

/* -----------------------------------------------------------------------------
 * streaming/color_matching/default
 */

#define UVCG_DEFAULT_COLOR_MATCHING_ATTR(cname, aname, bits)		\
static ssize_t uvcg_default_color_matching_##cname##_show(		\
	struct config_item *item, char *page)				\
{									\
	struct config_group *group = to_config_group(item);		\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;		\
	struct uvc_color_matching_descriptor *cd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
	cd = &opts->uvc_color_matching;					\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(cd->aname));	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_color_matching_, cname, aname)

UVCG_DEFAULT_COLOR_MATCHING_ATTR(b_color_primaries, bColorPrimaries, 8);
UVCG_DEFAULT_COLOR_MATCHING_ATTR(b_transfer_characteristics,
				 bTransferCharacteristics, 8);
UVCG_DEFAULT_COLOR_MATCHING_ATTR(b_matrix_coefficients, bMatrixCoefficients, 8);

#undef UVCG_DEFAULT_COLOR_MATCHING_ATTR

static struct configfs_attribute *uvcg_default_color_matching_attrs[] = {
	&uvcg_default_color_matching_attr_b_color_primaries,
	&uvcg_default_color_matching_attr_b_transfer_characteristics,
	&uvcg_default_color_matching_attr_b_matrix_coefficients,
	NULL,
};

static const struct uvcg_config_group_type uvcg_default_color_matching_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_color_matching_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "default",
};

/* -----------------------------------------------------------------------------
 * streaming/color_matching
 */

static const struct uvcg_config_group_type uvcg_color_matching_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "color_matching",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_default_color_matching_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * streaming/class/{fs|hs|ss}
 */

struct uvcg_streaming_class_group {
	struct config_group group;
	const char *name;
};

static inline struct uvc_descriptor_header
***__uvcg_get_stream_class_arr(struct config_item *i, struct f_uvc_opts *o)
{
	struct uvcg_streaming_class_group *group =
		container_of(i, struct uvcg_streaming_class_group,
			     group.cg_item);

	if (!strcmp(group->name, "fs"))
		return &o->uvc_fs_streaming_cls;

	if (!strcmp(group->name, "hs"))
		return &o->uvc_hs_streaming_cls;

	if (!strcmp(group->name, "ss"))
		return &o->uvc_ss_streaming_cls;

	return NULL;
}

enum uvcg_strm_type {
	UVCG_HEADER = 0,
	UVCG_FORMAT,
	UVCG_FRAME
};

/*
 * Iterate over a hierarchy of streaming descriptors' config items.
 * The items are created by the user with configfs.
 *
 * It "processes" the header pointed to by @priv1, then for each format
 * that follows the header "processes" the format itself and then for
 * each frame inside a format "processes" the frame.
 *
 * As a "processing" function the @fun is used.
 *
 * __uvcg_iter_strm_cls() is used in two context: first, to calculate
 * the amount of memory needed for an array of streaming descriptors
 * and second, to actually fill the array.
 *
 * @h: streaming header pointer
 * @priv2: an "inout" parameter (the caller might want to see the changes to it)
 * @priv3: an "inout" parameter (the caller might want to see the changes to it)
 * @fun: callback function for processing each level of the hierarchy
 */
static int __uvcg_iter_strm_cls(struct uvcg_streaming_header *h,
	void *priv2, void *priv3,
	int (*fun)(void *, void *, void *, int, enum uvcg_strm_type type))
{
	struct uvcg_format_ptr *f;
	struct config_group *grp;
	struct config_item *item;
	struct uvcg_frame *frm;
	int ret, i, j;

	if (!fun)
		return -EINVAL;

	i = j = 0;
	ret = fun(h, priv2, priv3, 0, UVCG_HEADER);
	if (ret)
		return ret;
	list_for_each_entry(f, &h->formats, entry) {
		ret = fun(f->fmt, priv2, priv3, i++, UVCG_FORMAT);
		if (ret)
			return ret;
		grp = &f->fmt->group;
		list_for_each_entry(item, &grp->cg_children, ci_entry) {
			frm = to_uvcg_frame(item);
			ret = fun(frm, priv2, priv3, j++, UVCG_FRAME);
			if (ret)
				return ret;
		}
	}

	return ret;
}

/*
 * Count how many bytes are needed for an array of streaming descriptors.
 *
 * @priv1: pointer to a header, format or frame
 * @priv2: inout parameter, accumulated size of the array
 * @priv3: inout parameter, accumulated number of the array elements
 * @n: unused, this function's prototype must match @fun in __uvcg_iter_strm_cls
 */
static int __uvcg_cnt_strm(void *priv1, void *priv2, void *priv3, int n,
			   enum uvcg_strm_type type)
{
	size_t *size = priv2;
	size_t *count = priv3;

	switch (type) {
	case UVCG_HEADER: {
		struct uvcg_streaming_header *h = priv1;

		*size += sizeof(h->desc);
		/* bmaControls */
		*size += h->num_fmt * UVCG_STREAMING_CONTROL_SIZE;
	}
	break;
	case UVCG_FORMAT: {
		struct uvcg_format *fmt = priv1;

		if (fmt->type == UVCG_UNCOMPRESSED) {
			struct uvcg_uncompressed *u =
				container_of(fmt, struct uvcg_uncompressed,
					     fmt);

			*size += sizeof(u->desc);
		} else if (fmt->type == UVCG_MJPEG) {
			struct uvcg_mjpeg *m =
				container_of(fmt, struct uvcg_mjpeg, fmt);

			*size += sizeof(m->desc);
		} else {
			return -EINVAL;
		}
	}
	break;
	case UVCG_FRAME: {
		struct uvcg_frame *frm = priv1;
		int sz = sizeof(frm->dw_frame_interval);

		*size += sizeof(frm->frame);
		*size += frm->frame.b_frame_interval_type * sz;
	}
	break;
	}

	++*count;

	return 0;
}

/*
 * Fill an array of streaming descriptors.
 *
 * @priv1: pointer to a header, format or frame
 * @priv2: inout parameter, pointer into a block of memory
 * @priv3: inout parameter, pointer to a 2-dimensional array
 */
static int __uvcg_fill_strm(void *priv1, void *priv2, void *priv3, int n,
			    enum uvcg_strm_type type)
{
	void **dest = priv2;
	struct uvc_descriptor_header ***array = priv3;
	size_t sz;

	**array = *dest;
	++*array;

	switch (type) {
	case UVCG_HEADER: {
		struct uvc_input_header_descriptor *ihdr = *dest;
		struct uvcg_streaming_header *h = priv1;
		struct uvcg_format_ptr *f;

		memcpy(*dest, &h->desc, sizeof(h->desc));
		*dest += sizeof(h->desc);
		sz = UVCG_STREAMING_CONTROL_SIZE;
		list_for_each_entry(f, &h->formats, entry) {
			memcpy(*dest, f->fmt->bmaControls, sz);
			*dest += sz;
		}
		ihdr->bLength = sizeof(h->desc) + h->num_fmt * sz;
		ihdr->bNumFormats = h->num_fmt;
	}
	break;
	case UVCG_FORMAT: {
		struct uvcg_format *fmt = priv1;

		if (fmt->type == UVCG_UNCOMPRESSED) {
			struct uvcg_uncompressed *u =
				container_of(fmt, struct uvcg_uncompressed,
					     fmt);

			u->desc.bFormatIndex = n + 1;
			u->desc.bNumFrameDescriptors = fmt->num_frames;
			memcpy(*dest, &u->desc, sizeof(u->desc));
			*dest += sizeof(u->desc);
		} else if (fmt->type == UVCG_MJPEG) {
			struct uvcg_mjpeg *m =
				container_of(fmt, struct uvcg_mjpeg, fmt);

			m->desc.bFormatIndex = n + 1;
			m->desc.bNumFrameDescriptors = fmt->num_frames;
			memcpy(*dest, &m->desc, sizeof(m->desc));
			*dest += sizeof(m->desc);
		} else {
			return -EINVAL;
		}
	}
	break;
	case UVCG_FRAME: {
		struct uvcg_frame *frm = priv1;
		struct uvc_descriptor_header *h = *dest;

		sz = sizeof(frm->frame);
		memcpy(*dest, &frm->frame, sz);
		*dest += sz;
		sz = frm->frame.b_frame_interval_type *
			sizeof(*frm->dw_frame_interval);
		memcpy(*dest, frm->dw_frame_interval, sz);
		*dest += sz;
		if (frm->fmt_type == UVCG_UNCOMPRESSED)
			h->bLength = UVC_DT_FRAME_UNCOMPRESSED_SIZE(
				frm->frame.b_frame_interval_type);
		else if (frm->fmt_type == UVCG_MJPEG)
			h->bLength = UVC_DT_FRAME_MJPEG_SIZE(
				frm->frame.b_frame_interval_type);
	}
	break;
	}

	return 0;
}

static int uvcg_streaming_class_allow_link(struct config_item *src,
					   struct config_item *target)
{
	struct config_item *streaming, *header;
	struct f_uvc_opts *opts;
	struct mutex *su_mutex = &src->ci_group->cg_subsys->su_mutex;
	struct uvc_descriptor_header ***class_array, **cl_arr;
	struct uvcg_streaming_header *target_hdr;
	void *data, *data_save;
	size_t size = 0, count = 0;
	int ret = -EINVAL;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	streaming = src->ci_parent->ci_parent;
	header = config_group_find_item(to_config_group(streaming), "header");
	if (!header || target->ci_parent != header)
		goto out;

	opts = to_f_uvc_opts(streaming->ci_parent);

	mutex_lock(&opts->lock);

	class_array = __uvcg_get_stream_class_arr(src, opts);
	if (!class_array || *class_array || opts->refcnt) {
		ret = -EBUSY;
		goto unlock;
	}

	target_hdr = to_uvcg_streaming_header(target);
	ret = __uvcg_iter_strm_cls(target_hdr, &size, &count, __uvcg_cnt_strm);
	if (ret)
		goto unlock;

	count += 2; /* color_matching, NULL */
	*class_array = kcalloc(count, sizeof(void *), GFP_KERNEL);
	if (!*class_array) {
		ret = -ENOMEM;
		goto unlock;
	}

	data = data_save = kzalloc(size, GFP_KERNEL);
	if (!data) {
		kfree(*class_array);
		*class_array = NULL;
		ret = -ENOMEM;
		goto unlock;
	}
	cl_arr = *class_array;
	ret = __uvcg_iter_strm_cls(target_hdr, &data, &cl_arr,
				   __uvcg_fill_strm);
	if (ret) {
		kfree(*class_array);
		*class_array = NULL;
		/*
		 * __uvcg_fill_strm() called from __uvcg_iter_stream_cls()
		 * might have advanced the "data", so use a backup copy
		 */
		kfree(data_save);
		goto unlock;
	}
	*cl_arr = (struct uvc_descriptor_header *)&opts->uvc_color_matching;

	++target_hdr->linked;
	ret = 0;

unlock:
	mutex_unlock(&opts->lock);
out:
	config_item_put(header);
	mutex_unlock(su_mutex);
	return ret;
}

static void uvcg_streaming_class_drop_link(struct config_item *src,
					  struct config_item *target)
{
	struct config_item *streaming, *header;
	struct f_uvc_opts *opts;
	struct mutex *su_mutex = &src->ci_group->cg_subsys->su_mutex;
	struct uvc_descriptor_header ***class_array;
	struct uvcg_streaming_header *target_hdr;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	streaming = src->ci_parent->ci_parent;
	header = config_group_find_item(to_config_group(streaming), "header");
	if (!header || target->ci_parent != header)
		goto out;

	opts = to_f_uvc_opts(streaming->ci_parent);

	mutex_lock(&opts->lock);

	class_array = __uvcg_get_stream_class_arr(src, opts);
	if (!class_array || !*class_array)
		goto unlock;

	if (opts->refcnt)
		goto unlock;

	target_hdr = to_uvcg_streaming_header(target);
	--target_hdr->linked;
	kfree(**class_array);
	kfree(*class_array);
	*class_array = NULL;

unlock:
	mutex_unlock(&opts->lock);
out:
	config_item_put(header);
	mutex_unlock(su_mutex);
}

static struct configfs_item_operations uvcg_streaming_class_item_ops = {
	.release	= uvcg_config_item_release,
	.allow_link	= uvcg_streaming_class_allow_link,
	.drop_link	= uvcg_streaming_class_drop_link,
};

static const struct config_item_type uvcg_streaming_class_type = {
	.ct_item_ops	= &uvcg_streaming_class_item_ops,
	.ct_owner	= THIS_MODULE,
};

/* -----------------------------------------------------------------------------
 * streaming/class
 */

static int uvcg_streaming_class_create_children(struct config_group *parent)
{
	static const char * const names[] = { "fs", "hs", "ss" };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(names); ++i) {
		struct uvcg_streaming_class_group *group;

		group = kzalloc(sizeof(*group), GFP_KERNEL);
		if (!group)
			return -ENOMEM;

		group->name = names[i];

		config_group_init_type_name(&group->group, group->name,
					    &uvcg_streaming_class_type);
		configfs_add_default_group(&group->group, parent);
	}

	return 0;
}

static const struct uvcg_config_group_type uvcg_streaming_class_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "class",
	.create_children = uvcg_streaming_class_create_children,
};

/* -----------------------------------------------------------------------------
 * streaming
 */

static ssize_t uvcg_default_streaming_b_interface_number_show(
	struct config_item *item, char *page)
{
	struct config_group *group = to_config_group(item);
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct config_item *opts_item;
	struct f_uvc_opts *opts;
	int result = 0;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = item->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	result += sprintf(page, "%u\n", opts->streaming_interface);
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return result;
}

UVC_ATTR_RO(uvcg_default_streaming_, b_interface_number, bInterfaceNumber);

static struct configfs_attribute *uvcg_default_streaming_attrs[] = {
	&uvcg_default_streaming_attr_b_interface_number,
	NULL,
};

static const struct uvcg_config_group_type uvcg_streaming_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_streaming_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "streaming",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_streaming_header_grp_type,
		&uvcg_uncompressed_grp_type,
		&uvcg_mjpeg_grp_type,
		&uvcg_color_matching_grp_type,
		&uvcg_streaming_class_grp_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * UVC function
 */

static void uvc_func_item_release(struct config_item *item)
{
	struct f_uvc_opts *opts = to_f_uvc_opts(item);

	uvcg_config_remove_children(to_config_group(item));
	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations uvc_func_item_ops = {
	.release	= uvc_func_item_release,
};

#define UVCG_OPTS_ATTR(cname, aname, limit)				\
static ssize_t f_uvc_opts_##cname##_show(				\
	struct config_item *item, char *page)				\
{									\
	struct f_uvc_opts *opts = to_f_uvc_opts(item);			\
	int result;							\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", opts->cname);			\
	mutex_unlock(&opts->lock);					\
									\
	return result;							\
}									\
									\
static ssize_t								\
f_uvc_opts_##cname##_store(struct config_item *item,			\
			   const char *page, size_t len)		\
{									\
	struct f_uvc_opts *opts = to_f_uvc_opts(item);			\
	unsigned int num;						\
	int ret;							\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtouint(page, 0, &num);				\
	if (ret)							\
		goto end;						\
									\
	if (num > limit) {						\
		ret = -EINVAL;						\
		goto end;						\
	}								\
	opts->cname = num;						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	return ret;							\
}									\
									\
UVC_ATTR(f_uvc_opts_, cname, cname)

UVCG_OPTS_ATTR(streaming_interval, streaming_interval, 16);
UVCG_OPTS_ATTR(streaming_maxpacket, streaming_maxpacket, 3072);
UVCG_OPTS_ATTR(streaming_maxburst, streaming_maxburst, 15);

#undef UVCG_OPTS_ATTR

static struct configfs_attribute *uvc_attrs[] = {
	&f_uvc_opts_attr_streaming_interval,
	&f_uvc_opts_attr_streaming_maxpacket,
	&f_uvc_opts_attr_streaming_maxburst,
	NULL,
};

static const struct uvcg_config_group_type uvc_func_type = {
	.type = {
		.ct_item_ops	= &uvc_func_item_ops,
		.ct_attrs	= uvc_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_control_grp_type,
		&uvcg_streaming_grp_type,
		NULL,
	},
};

int uvcg_attach_configfs(struct f_uvc_opts *opts)
{
	int ret;

	config_group_init_type_name(&opts->func_inst.group, uvc_func_type.name,
				    &uvc_func_type.type);

	ret = uvcg_config_create_children(&opts->func_inst.group,
					  &uvc_func_type);
	if (ret < 0)
		config_group_put(&opts->func_inst.group);

	return ret;
}
