// SPDX-License-Identifier: GPL-2.0
/*
 * uvc_configfs.c
 *
 * Configfs support for the uvc function.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 */
#include "u_uvc.h"
#include "uvc_configfs.h"

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

static inline struct f_uvc_opts *to_f_uvc_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_uvc_opts,
			    func_inst.group);
}

/* control/header/<NAME> */
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

#define UVCG_CTRL_HDR_ATTR(cname, aname, conv, str2u, uxx, vnoc, limit)	\
static ssize_t uvcg_control_header_##cname##_show(			\
	struct config_item *item, char *page)			\
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
	result = sprintf(page, "%d\n", conv(ch->desc.aname));		\
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
	uxx num;							\
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
	ret = str2u(page, 0, &num);					\
	if (ret)							\
		goto end;						\
									\
	if (num > limit) {						\
		ret = -EINVAL;						\
		goto end;						\
	}								\
	ch->desc.aname = vnoc(num);					\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_control_header_, cname, aname)

UVCG_CTRL_HDR_ATTR(bcd_uvc, bcdUVC, le16_to_cpu, kstrtou16, u16, cpu_to_le16,
		   0xffff);

UVCG_CTRL_HDR_ATTR(dw_clock_frequency, dwClockFrequency, le32_to_cpu, kstrtou32,
		   u32, cpu_to_le32, 0x7fffffff);

#undef UVCG_CTRL_HDR_ATTR

static struct configfs_attribute *uvcg_control_header_attrs[] = {
	&uvcg_control_header_attr_bcd_uvc,
	&uvcg_control_header_attr_dw_clock_frequency,
	NULL,
};

static const struct config_item_type uvcg_control_header_type = {
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
	h->desc.bcdUVC			= cpu_to_le16(0x0100);
	h->desc.dwClockFrequency	= cpu_to_le32(48000000);

	config_item_init_type_name(&h->item, name, &uvcg_control_header_type);

	return &h->item;
}

static void uvcg_control_header_drop(struct config_group *group,
			      struct config_item *item)
{
	struct uvcg_control_header *h = to_uvcg_control_header(item);

	kfree(h);
}

/* control/header */
static struct uvcg_control_header_grp {
	struct config_group	group;
} uvcg_control_header_grp;

static struct configfs_group_operations uvcg_control_header_grp_ops = {
	.make_item		= uvcg_control_header_make,
	.drop_item		= uvcg_control_header_drop,
};

static const struct config_item_type uvcg_control_header_grp_type = {
	.ct_group_ops	= &uvcg_control_header_grp_ops,
	.ct_owner	= THIS_MODULE,
};

/* control/processing/default */
static struct uvcg_default_processing {
	struct config_group	group;
} uvcg_default_processing;

static inline struct uvcg_default_processing
*to_uvcg_default_processing(struct config_item *item)
{
	return container_of(to_config_group(item),
			    struct uvcg_default_processing, group);
}

#define UVCG_DEFAULT_PROCESSING_ATTR(cname, aname, conv)		\
static ssize_t uvcg_default_processing_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct uvcg_default_processing *dp = to_uvcg_default_processing(item); \
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &dp->group.cg_subsys->su_mutex;	\
	struct uvc_processing_unit_descriptor *pd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = dp->group.cg_item.ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
	pd = &opts->uvc_processing;					\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%d\n", conv(pd->aname));		\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_processing_, cname, aname)

#define identity_conv(x) (x)

UVCG_DEFAULT_PROCESSING_ATTR(b_unit_id, bUnitID, identity_conv);
UVCG_DEFAULT_PROCESSING_ATTR(b_source_id, bSourceID, identity_conv);
UVCG_DEFAULT_PROCESSING_ATTR(w_max_multiplier, wMaxMultiplier, le16_to_cpu);
UVCG_DEFAULT_PROCESSING_ATTR(i_processing, iProcessing, identity_conv);

#undef identity_conv

#undef UVCG_DEFAULT_PROCESSING_ATTR

static ssize_t uvcg_default_processing_bm_controls_show(
	struct config_item *item, char *page)
{
	struct uvcg_default_processing *dp = to_uvcg_default_processing(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &dp->group.cg_subsys->su_mutex;
	struct uvc_processing_unit_descriptor *pd;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = dp->group.cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	pd = &opts->uvc_processing;

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; i < pd->bControlSize; ++i) {
		result += sprintf(pg, "%d\n", pd->bmControls[i]);
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

static const struct config_item_type uvcg_default_processing_type = {
	.ct_attrs	= uvcg_default_processing_attrs,
	.ct_owner	= THIS_MODULE,
};

/* struct uvcg_processing {}; */

/* control/processing */
static struct uvcg_processing_grp {
	struct config_group	group;
} uvcg_processing_grp;

static const struct config_item_type uvcg_processing_grp_type = {
	.ct_owner = THIS_MODULE,
};

/* control/terminal/camera/default */
static struct uvcg_default_camera {
	struct config_group	group;
} uvcg_default_camera;

static inline struct uvcg_default_camera
*to_uvcg_default_camera(struct config_item *item)
{
	return container_of(to_config_group(item),
			    struct uvcg_default_camera, group);
}

#define UVCG_DEFAULT_CAMERA_ATTR(cname, aname, conv)			\
static ssize_t uvcg_default_camera_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct uvcg_default_camera *dc = to_uvcg_default_camera(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &dc->group.cg_subsys->su_mutex;	\
	struct uvc_camera_terminal_descriptor *cd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = dc->group.cg_item.ci_parent->ci_parent->ci_parent->	\
			ci_parent;					\
	opts = to_f_uvc_opts(opts_item);				\
	cd = &opts->uvc_camera_terminal;				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%d\n", conv(cd->aname));		\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
									\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_camera_, cname, aname)

#define identity_conv(x) (x)

UVCG_DEFAULT_CAMERA_ATTR(b_terminal_id, bTerminalID, identity_conv);
UVCG_DEFAULT_CAMERA_ATTR(w_terminal_type, wTerminalType, le16_to_cpu);
UVCG_DEFAULT_CAMERA_ATTR(b_assoc_terminal, bAssocTerminal, identity_conv);
UVCG_DEFAULT_CAMERA_ATTR(i_terminal, iTerminal, identity_conv);
UVCG_DEFAULT_CAMERA_ATTR(w_objective_focal_length_min, wObjectiveFocalLengthMin,
			 le16_to_cpu);
UVCG_DEFAULT_CAMERA_ATTR(w_objective_focal_length_max, wObjectiveFocalLengthMax,
			 le16_to_cpu);
UVCG_DEFAULT_CAMERA_ATTR(w_ocular_focal_length, wOcularFocalLength,
			 le16_to_cpu);

#undef identity_conv

#undef UVCG_DEFAULT_CAMERA_ATTR

static ssize_t uvcg_default_camera_bm_controls_show(
	struct config_item *item, char *page)
{
	struct uvcg_default_camera *dc = to_uvcg_default_camera(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &dc->group.cg_subsys->su_mutex;
	struct uvc_camera_terminal_descriptor *cd;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = dc->group.cg_item.ci_parent->ci_parent->ci_parent->
			ci_parent;
	opts = to_f_uvc_opts(opts_item);
	cd = &opts->uvc_camera_terminal;

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; i < cd->bControlSize; ++i) {
		result += sprintf(pg, "%d\n", cd->bmControls[i]);
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

static const struct config_item_type uvcg_default_camera_type = {
	.ct_attrs	= uvcg_default_camera_attrs,
	.ct_owner	= THIS_MODULE,
};

/* struct uvcg_camera {}; */

/* control/terminal/camera */
static struct uvcg_camera_grp {
	struct config_group	group;
} uvcg_camera_grp;

static const struct config_item_type uvcg_camera_grp_type = {
	.ct_owner = THIS_MODULE,
};

/* control/terminal/output/default */
static struct uvcg_default_output {
	struct config_group	group;
} uvcg_default_output;

static inline struct uvcg_default_output
*to_uvcg_default_output(struct config_item *item)
{
	return container_of(to_config_group(item),
			    struct uvcg_default_output, group);
}

#define UVCG_DEFAULT_OUTPUT_ATTR(cname, aname, conv)			\
static ssize_t uvcg_default_output_##cname##_show(			\
	struct config_item *item, char *page)			\
{									\
	struct uvcg_default_output *dout = to_uvcg_default_output(item); \
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &dout->group.cg_subsys->su_mutex;	\
	struct uvc_output_terminal_descriptor *cd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = dout->group.cg_item.ci_parent->ci_parent->		\
			ci_parent->ci_parent;				\
	opts = to_f_uvc_opts(opts_item);				\
	cd = &opts->uvc_output_terminal;				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%d\n", conv(cd->aname));		\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
									\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_output_, cname, aname)

#define identity_conv(x) (x)

UVCG_DEFAULT_OUTPUT_ATTR(b_terminal_id, bTerminalID, identity_conv);
UVCG_DEFAULT_OUTPUT_ATTR(w_terminal_type, wTerminalType, le16_to_cpu);
UVCG_DEFAULT_OUTPUT_ATTR(b_assoc_terminal, bAssocTerminal, identity_conv);
UVCG_DEFAULT_OUTPUT_ATTR(b_source_id, bSourceID, identity_conv);
UVCG_DEFAULT_OUTPUT_ATTR(i_terminal, iTerminal, identity_conv);

#undef identity_conv

#undef UVCG_DEFAULT_OUTPUT_ATTR

static struct configfs_attribute *uvcg_default_output_attrs[] = {
	&uvcg_default_output_attr_b_terminal_id,
	&uvcg_default_output_attr_w_terminal_type,
	&uvcg_default_output_attr_b_assoc_terminal,
	&uvcg_default_output_attr_b_source_id,
	&uvcg_default_output_attr_i_terminal,
	NULL,
};

static const struct config_item_type uvcg_default_output_type = {
	.ct_attrs	= uvcg_default_output_attrs,
	.ct_owner	= THIS_MODULE,
};

/* struct uvcg_output {}; */

/* control/terminal/output */
static struct uvcg_output_grp {
	struct config_group	group;
} uvcg_output_grp;

static const struct config_item_type uvcg_output_grp_type = {
	.ct_owner = THIS_MODULE,
};

/* control/terminal */
static struct uvcg_terminal_grp {
	struct config_group	group;
} uvcg_terminal_grp;

static const struct config_item_type uvcg_terminal_grp_type = {
	.ct_owner = THIS_MODULE,
};

/* control/class/{fs} */
static struct uvcg_control_class {
	struct config_group	group;
} uvcg_control_class_fs, uvcg_control_class_ss;


static inline struct uvc_descriptor_header
**uvcg_get_ctl_class_arr(struct config_item *i, struct f_uvc_opts *o)
{
	struct uvcg_control_class *cl = container_of(to_config_group(i),
		struct uvcg_control_class, group);

	if (cl == &uvcg_control_class_fs)
		return o->uvc_fs_control_cls;

	if (cl == &uvcg_control_class_ss)
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
	mutex_unlock(su_mutex);
}

static struct configfs_item_operations uvcg_control_class_item_ops = {
	.allow_link	= uvcg_control_class_allow_link,
	.drop_link	= uvcg_control_class_drop_link,
};

static const struct config_item_type uvcg_control_class_type = {
	.ct_item_ops	= &uvcg_control_class_item_ops,
	.ct_owner	= THIS_MODULE,
};

/* control/class */
static struct uvcg_control_class_grp {
	struct config_group	group;
} uvcg_control_class_grp;

static const struct config_item_type uvcg_control_class_grp_type = {
	.ct_owner = THIS_MODULE,
};

/* control */
static struct uvcg_control_grp {
	struct config_group	group;
} uvcg_control_grp;

static const struct config_item_type uvcg_control_grp_type = {
	.ct_owner = THIS_MODULE,
};

/* streaming/uncompressed */
static struct uvcg_uncompressed_grp {
	struct config_group	group;
} uvcg_uncompressed_grp;

/* streaming/mjpeg */
static struct uvcg_mjpeg_grp {
	struct config_group	group;
} uvcg_mjpeg_grp;

static struct config_item *fmt_parent[] = {
	&uvcg_uncompressed_grp.group.cg_item,
	&uvcg_mjpeg_grp.group.cg_item,
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

/* streaming/header/<NAME> */
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

	for (i = 0; i < ARRAY_SIZE(fmt_parent); ++i)
		if (target->ci_parent == fmt_parent[i])
			break;
	if (i == ARRAY_SIZE(fmt_parent))
		goto out;

	target_fmt = container_of(to_config_group(target), struct uvcg_format,
				  group);
	if (!target_fmt)
		goto out;

	format_ptr = kzalloc(sizeof(*format_ptr), GFP_KERNEL);
	if (!format_ptr) {
		ret = -ENOMEM;
		goto out;
	}
	ret = 0;
	format_ptr->fmt = target_fmt;
	list_add_tail(&format_ptr->entry, &src_hdr->formats);
	++src_hdr->num_fmt;

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

out:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
}

static struct configfs_item_operations uvcg_streaming_header_item_ops = {
	.allow_link		= uvcg_streaming_header_allow_link,
	.drop_link		= uvcg_streaming_header_drop_link,
};

#define UVCG_STREAMING_HEADER_ATTR(cname, aname, conv)			\
static ssize_t uvcg_streaming_header_##cname##_show(			\
	struct config_item *item, char *page)			\
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
	result = sprintf(page, "%d\n", conv(sh->desc.aname));		\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_streaming_header_, cname, aname)

#define identity_conv(x) (x)

UVCG_STREAMING_HEADER_ATTR(bm_info, bmInfo, identity_conv);
UVCG_STREAMING_HEADER_ATTR(b_terminal_link, bTerminalLink, identity_conv);
UVCG_STREAMING_HEADER_ATTR(b_still_capture_method, bStillCaptureMethod,
			   identity_conv);
UVCG_STREAMING_HEADER_ATTR(b_trigger_support, bTriggerSupport, identity_conv);
UVCG_STREAMING_HEADER_ATTR(b_trigger_usage, bTriggerUsage, identity_conv);

#undef identity_conv

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

static void uvcg_streaming_header_drop(struct config_group *group,
			      struct config_item *item)
{
	struct uvcg_streaming_header *h = to_uvcg_streaming_header(item);

	kfree(h);
}

/* streaming/header */
static struct uvcg_streaming_header_grp {
	struct config_group	group;
} uvcg_streaming_header_grp;

static struct configfs_group_operations uvcg_streaming_header_grp_ops = {
	.make_item		= uvcg_streaming_header_make,
	.drop_item		= uvcg_streaming_header_drop,
};

static const struct config_item_type uvcg_streaming_header_grp_type = {
	.ct_group_ops	= &uvcg_streaming_header_grp_ops,
	.ct_owner	= THIS_MODULE,
};

/* streaming/<mode>/<format>/<NAME> */
struct uvcg_frame {
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
	enum uvcg_format_type	fmt_type;
	struct config_item	item;
};

static struct uvcg_frame *to_uvcg_frame(struct config_item *item)
{
	return container_of(item, struct uvcg_frame, item);
}

#define UVCG_FRAME_ATTR(cname, aname, to_cpu_endian, to_little_endian, bits) \
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
	result = sprintf(page, "%d\n", to_cpu_endian(f->frame.cname));	\
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
	int ret;							\
	u##bits num;							\
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
	f->frame.cname = to_little_endian(num);				\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_frame_, cname, aname);

#define noop_conversion(x) (x)

UVCG_FRAME_ATTR(bm_capabilities, bmCapabilities, noop_conversion,
		noop_conversion, 8);
UVCG_FRAME_ATTR(w_width, wWidth, le16_to_cpu, cpu_to_le16, 16);
UVCG_FRAME_ATTR(w_height, wHeight, le16_to_cpu, cpu_to_le16, 16);
UVCG_FRAME_ATTR(dw_min_bit_rate, dwMinBitRate, le32_to_cpu, cpu_to_le32, 32);
UVCG_FRAME_ATTR(dw_max_bit_rate, dwMaxBitRate, le32_to_cpu, cpu_to_le32, 32);
UVCG_FRAME_ATTR(dw_max_video_frame_buffer_size, dwMaxVideoFrameBufferSize,
		le32_to_cpu, cpu_to_le32, 32);
UVCG_FRAME_ATTR(dw_default_frame_interval, dwDefaultFrameInterval,
		le32_to_cpu, cpu_to_le32, 32);

#undef noop_conversion

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
		result += sprintf(pg, "%d\n",
				  le32_to_cpu(frm->dw_frame_interval[i]));
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
	**interv = cpu_to_le32(num);
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
	ret = len;

end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

UVC_ATTR(uvcg_frame_, dw_frame_interval, dwFrameInterval);

static struct configfs_attribute *uvcg_frame_attrs[] = {
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
	h->frame.w_width			= cpu_to_le16(640);
	h->frame.w_height			= cpu_to_le16(360);
	h->frame.dw_min_bit_rate		= cpu_to_le32(18432000);
	h->frame.dw_max_bit_rate		= cpu_to_le32(55296000);
	h->frame.dw_max_video_frame_buffer_size	= cpu_to_le32(460800);
	h->frame.dw_default_frame_interval	= cpu_to_le32(666666);

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
	struct uvcg_frame *h = to_uvcg_frame(item);
	struct uvcg_format *fmt;
	struct f_uvc_opts *opts;
	struct config_item *opts_item;

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	fmt = to_uvcg_format(&group->cg_item);
	--fmt->num_frames;
	kfree(h);
	mutex_unlock(&opts->lock);
}

/* streaming/uncompressed/<NAME> */
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

#define UVCG_UNCOMPRESSED_ATTR_RO(cname, aname, conv)			\
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
	result = sprintf(page, "%d\n", conv(u->desc.aname));		\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_uncompressed_, cname, aname);

#define UVCG_UNCOMPRESSED_ATTR(cname, aname, conv)			\
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
	result = sprintf(page, "%d\n", conv(u->desc.aname));		\
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
	if (num > 255) {						\
		ret = -EINVAL;						\
		goto end;						\
	}								\
	u->desc.aname = num;						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_uncompressed_, cname, aname);

#define identity_conv(x) (x)

UVCG_UNCOMPRESSED_ATTR(b_bits_per_pixel, bBitsPerPixel, identity_conv);
UVCG_UNCOMPRESSED_ATTR(b_default_frame_index, bDefaultFrameIndex,
		       identity_conv);
UVCG_UNCOMPRESSED_ATTR_RO(b_aspect_ratio_x, bAspectRatioX, identity_conv);
UVCG_UNCOMPRESSED_ATTR_RO(b_aspect_ratio_y, bAspectRatioY, identity_conv);
UVCG_UNCOMPRESSED_ATTR_RO(bm_interface_flags, bmInterfaceFlags, identity_conv);

#undef identity_conv

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

static void uvcg_uncompressed_drop(struct config_group *group,
			    struct config_item *item)
{
	struct uvcg_uncompressed *h = to_uvcg_uncompressed(item);

	kfree(h);
}

static struct configfs_group_operations uvcg_uncompressed_grp_ops = {
	.make_group		= uvcg_uncompressed_make,
	.drop_item		= uvcg_uncompressed_drop,
};

static const struct config_item_type uvcg_uncompressed_grp_type = {
	.ct_group_ops	= &uvcg_uncompressed_grp_ops,
	.ct_owner	= THIS_MODULE,
};

/* streaming/mjpeg/<NAME> */
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

#define UVCG_MJPEG_ATTR_RO(cname, aname, conv)				\
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
	result = sprintf(page, "%d\n", conv(u->desc.aname));		\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_mjpeg_, cname, aname)

#define UVCG_MJPEG_ATTR(cname, aname, conv)				\
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
	result = sprintf(page, "%d\n", conv(u->desc.aname));		\
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
	if (num > 255) {						\
		ret = -EINVAL;						\
		goto end;						\
	}								\
	u->desc.aname = num;						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_mjpeg_, cname, aname)

#define identity_conv(x) (x)

UVCG_MJPEG_ATTR(b_default_frame_index, bDefaultFrameIndex,
		       identity_conv);
UVCG_MJPEG_ATTR_RO(bm_flags, bmFlags, identity_conv);
UVCG_MJPEG_ATTR_RO(b_aspect_ratio_x, bAspectRatioX, identity_conv);
UVCG_MJPEG_ATTR_RO(b_aspect_ratio_y, bAspectRatioY, identity_conv);
UVCG_MJPEG_ATTR_RO(bm_interface_flags, bmInterfaceFlags, identity_conv);

#undef identity_conv

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
	&uvcg_mjpeg_attr_b_default_frame_index,
	&uvcg_mjpeg_attr_bm_flags,
	&uvcg_mjpeg_attr_b_aspect_ratio_x,
	&uvcg_mjpeg_attr_b_aspect_ratio_y,
	&uvcg_mjpeg_attr_bm_interface_flags,
	&uvcg_mjpeg_attr_bma_controls,
	NULL,
};

static const struct config_item_type uvcg_mjpeg_type = {
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

static void uvcg_mjpeg_drop(struct config_group *group,
			    struct config_item *item)
{
	struct uvcg_mjpeg *h = to_uvcg_mjpeg(item);

	kfree(h);
}

static struct configfs_group_operations uvcg_mjpeg_grp_ops = {
	.make_group		= uvcg_mjpeg_make,
	.drop_item		= uvcg_mjpeg_drop,
};

static const struct config_item_type uvcg_mjpeg_grp_type = {
	.ct_group_ops	= &uvcg_mjpeg_grp_ops,
	.ct_owner	= THIS_MODULE,
};

/* streaming/color_matching/default */
static struct uvcg_default_color_matching {
	struct config_group	group;
} uvcg_default_color_matching;

static inline struct uvcg_default_color_matching
*to_uvcg_default_color_matching(struct config_item *item)
{
	return container_of(to_config_group(item),
			    struct uvcg_default_color_matching, group);
}

#define UVCG_DEFAULT_COLOR_MATCHING_ATTR(cname, aname, conv)		\
static ssize_t uvcg_default_color_matching_##cname##_show(		\
	struct config_item *item, char *page)		\
{									\
	struct uvcg_default_color_matching *dc =			\
		to_uvcg_default_color_matching(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &dc->group.cg_subsys->su_mutex;	\
	struct uvc_color_matching_descriptor *cd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = dc->group.cg_item.ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
	cd = &opts->uvc_color_matching;					\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%d\n", conv(cd->aname));		\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_color_matching_, cname, aname)

#define identity_conv(x) (x)

UVCG_DEFAULT_COLOR_MATCHING_ATTR(b_color_primaries, bColorPrimaries,
				 identity_conv);
UVCG_DEFAULT_COLOR_MATCHING_ATTR(b_transfer_characteristics,
				 bTransferCharacteristics, identity_conv);
UVCG_DEFAULT_COLOR_MATCHING_ATTR(b_matrix_coefficients, bMatrixCoefficients,
				 identity_conv);

#undef identity_conv

#undef UVCG_DEFAULT_COLOR_MATCHING_ATTR

static struct configfs_attribute *uvcg_default_color_matching_attrs[] = {
	&uvcg_default_color_matching_attr_b_color_primaries,
	&uvcg_default_color_matching_attr_b_transfer_characteristics,
	&uvcg_default_color_matching_attr_b_matrix_coefficients,
	NULL,
};

static const struct config_item_type uvcg_default_color_matching_type = {
	.ct_attrs	= uvcg_default_color_matching_attrs,
	.ct_owner	= THIS_MODULE,
};

/* struct uvcg_color_matching {}; */

/* streaming/color_matching */
static struct uvcg_color_matching_grp {
	struct config_group	group;
} uvcg_color_matching_grp;

static const struct config_item_type uvcg_color_matching_grp_type = {
	.ct_owner = THIS_MODULE,
};

/* streaming/class/{fs|hs|ss} */
static struct uvcg_streaming_class {
	struct config_group	group;
} uvcg_streaming_class_fs, uvcg_streaming_class_hs, uvcg_streaming_class_ss;


static inline struct uvc_descriptor_header
***__uvcg_get_stream_class_arr(struct config_item *i, struct f_uvc_opts *o)
{
	struct uvcg_streaming_class *cl = container_of(to_config_group(i),
		struct uvcg_streaming_class, group);

	if (cl == &uvcg_streaming_class_fs)
		return &o->uvc_fs_streaming_cls;

	if (cl == &uvcg_streaming_class_hs)
		return &o->uvc_hs_streaming_cls;

	if (cl == &uvcg_streaming_class_ss)
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
			struct uvc_format_uncompressed *unc = *dest;
			struct uvcg_uncompressed *u =
				container_of(fmt, struct uvcg_uncompressed,
					     fmt);

			memcpy(*dest, &u->desc, sizeof(u->desc));
			*dest += sizeof(u->desc);
			unc->bNumFrameDescriptors = fmt->num_frames;
			unc->bFormatIndex = n + 1;
		} else if (fmt->type == UVCG_MJPEG) {
			struct uvc_format_mjpeg *mjp = *dest;
			struct uvcg_mjpeg *m =
				container_of(fmt, struct uvcg_mjpeg, fmt);

			memcpy(*dest, &m->desc, sizeof(m->desc));
			*dest += sizeof(m->desc);
			mjp->bNumFrameDescriptors = fmt->num_frames;
			mjp->bFormatIndex = n + 1;
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
	mutex_unlock(su_mutex);
}

static struct configfs_item_operations uvcg_streaming_class_item_ops = {
	.allow_link	= uvcg_streaming_class_allow_link,
	.drop_link	= uvcg_streaming_class_drop_link,
};

static const struct config_item_type uvcg_streaming_class_type = {
	.ct_item_ops	= &uvcg_streaming_class_item_ops,
	.ct_owner	= THIS_MODULE,
};

/* streaming/class */
static struct uvcg_streaming_class_grp {
	struct config_group	group;
} uvcg_streaming_class_grp;

static const struct config_item_type uvcg_streaming_class_grp_type = {
	.ct_owner = THIS_MODULE,
};

/* streaming */
static struct uvcg_streaming_grp {
	struct config_group	group;
} uvcg_streaming_grp;

static const struct config_item_type uvcg_streaming_grp_type = {
	.ct_owner = THIS_MODULE,
};

static void uvc_attr_release(struct config_item *item)
{
	struct f_uvc_opts *opts = to_f_uvc_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations uvc_item_ops = {
	.release		= uvc_attr_release,
};

#define UVCG_OPTS_ATTR(cname, aname, conv, str2u, uxx, vnoc, limit)	\
static ssize_t f_uvc_opts_##cname##_show(				\
	struct config_item *item, char *page)				\
{									\
	struct f_uvc_opts *opts = to_f_uvc_opts(item);			\
	int result;							\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%d\n", conv(opts->cname));		\
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
	int ret;							\
	uxx num;							\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = str2u(page, 0, &num);					\
	if (ret)							\
		goto end;						\
									\
	if (num > limit) {						\
		ret = -EINVAL;						\
		goto end;						\
	}								\
	opts->cname = vnoc(num);					\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	return ret;							\
}									\
									\
UVC_ATTR(f_uvc_opts_, cname, cname)

#define identity_conv(x) (x)

UVCG_OPTS_ATTR(streaming_interval, streaming_interval, identity_conv,
	       kstrtou8, u8, identity_conv, 16);
UVCG_OPTS_ATTR(streaming_maxpacket, streaming_maxpacket, le16_to_cpu,
	       kstrtou16, u16, le16_to_cpu, 3072);
UVCG_OPTS_ATTR(streaming_maxburst, streaming_maxburst, identity_conv,
	       kstrtou8, u8, identity_conv, 15);

#undef identity_conv

#undef UVCG_OPTS_ATTR

static struct configfs_attribute *uvc_attrs[] = {
	&f_uvc_opts_attr_streaming_interval,
	&f_uvc_opts_attr_streaming_maxpacket,
	&f_uvc_opts_attr_streaming_maxburst,
	NULL,
};

static const struct config_item_type uvc_func_type = {
	.ct_item_ops	= &uvc_item_ops,
	.ct_attrs	= uvc_attrs,
	.ct_owner	= THIS_MODULE,
};

int uvcg_attach_configfs(struct f_uvc_opts *opts)
{
	config_group_init_type_name(&uvcg_control_header_grp.group,
				    "header",
				    &uvcg_control_header_grp_type);

	config_group_init_type_name(&uvcg_default_processing.group,
			"default", &uvcg_default_processing_type);
	config_group_init_type_name(&uvcg_processing_grp.group,
			"processing", &uvcg_processing_grp_type);
	configfs_add_default_group(&uvcg_default_processing.group,
			&uvcg_processing_grp.group);

	config_group_init_type_name(&uvcg_default_camera.group,
			"default", &uvcg_default_camera_type);
	config_group_init_type_name(&uvcg_camera_grp.group,
			"camera", &uvcg_camera_grp_type);
	configfs_add_default_group(&uvcg_default_camera.group,
			&uvcg_camera_grp.group);

	config_group_init_type_name(&uvcg_default_output.group,
			"default", &uvcg_default_output_type);
	config_group_init_type_name(&uvcg_output_grp.group,
			"output", &uvcg_output_grp_type);
	configfs_add_default_group(&uvcg_default_output.group,
			&uvcg_output_grp.group);

	config_group_init_type_name(&uvcg_terminal_grp.group,
			"terminal", &uvcg_terminal_grp_type);
	configfs_add_default_group(&uvcg_camera_grp.group,
			&uvcg_terminal_grp.group);
	configfs_add_default_group(&uvcg_output_grp.group,
			&uvcg_terminal_grp.group);

	config_group_init_type_name(&uvcg_control_class_fs.group,
			"fs", &uvcg_control_class_type);
	config_group_init_type_name(&uvcg_control_class_ss.group,
			"ss", &uvcg_control_class_type);
	config_group_init_type_name(&uvcg_control_class_grp.group,
			"class",
			&uvcg_control_class_grp_type);
	configfs_add_default_group(&uvcg_control_class_fs.group,
			&uvcg_control_class_grp.group);
	configfs_add_default_group(&uvcg_control_class_ss.group,
			&uvcg_control_class_grp.group);

	config_group_init_type_name(&uvcg_control_grp.group,
			"control",
			&uvcg_control_grp_type);
	configfs_add_default_group(&uvcg_control_header_grp.group,
			&uvcg_control_grp.group);
	configfs_add_default_group(&uvcg_processing_grp.group,
			&uvcg_control_grp.group);
	configfs_add_default_group(&uvcg_terminal_grp.group,
			&uvcg_control_grp.group);
	configfs_add_default_group(&uvcg_control_class_grp.group,
			&uvcg_control_grp.group);

	config_group_init_type_name(&uvcg_streaming_header_grp.group,
				    "header",
				    &uvcg_streaming_header_grp_type);
	config_group_init_type_name(&uvcg_uncompressed_grp.group,
				    "uncompressed",
				    &uvcg_uncompressed_grp_type);
	config_group_init_type_name(&uvcg_mjpeg_grp.group,
				    "mjpeg",
				    &uvcg_mjpeg_grp_type);
	config_group_init_type_name(&uvcg_default_color_matching.group,
				    "default",
				    &uvcg_default_color_matching_type);
	config_group_init_type_name(&uvcg_color_matching_grp.group,
			"color_matching",
			&uvcg_color_matching_grp_type);
	configfs_add_default_group(&uvcg_default_color_matching.group,
			&uvcg_color_matching_grp.group);

	config_group_init_type_name(&uvcg_streaming_class_fs.group,
			"fs", &uvcg_streaming_class_type);
	config_group_init_type_name(&uvcg_streaming_class_hs.group,
			"hs", &uvcg_streaming_class_type);
	config_group_init_type_name(&uvcg_streaming_class_ss.group,
			"ss", &uvcg_streaming_class_type);
	config_group_init_type_name(&uvcg_streaming_class_grp.group,
			"class", &uvcg_streaming_class_grp_type);
	configfs_add_default_group(&uvcg_streaming_class_fs.group,
			&uvcg_streaming_class_grp.group);
	configfs_add_default_group(&uvcg_streaming_class_hs.group,
			&uvcg_streaming_class_grp.group);
	configfs_add_default_group(&uvcg_streaming_class_ss.group,
			&uvcg_streaming_class_grp.group);

	config_group_init_type_name(&uvcg_streaming_grp.group,
			"streaming", &uvcg_streaming_grp_type);
	configfs_add_default_group(&uvcg_streaming_header_grp.group,
			&uvcg_streaming_grp.group);
	configfs_add_default_group(&uvcg_uncompressed_grp.group,
			&uvcg_streaming_grp.group);
	configfs_add_default_group(&uvcg_mjpeg_grp.group,
			&uvcg_streaming_grp.group);
	configfs_add_default_group(&uvcg_color_matching_grp.group,
			&uvcg_streaming_grp.group);
	configfs_add_default_group(&uvcg_streaming_class_grp.group,
			&uvcg_streaming_grp.group);

	config_group_init_type_name(&opts->func_inst.group,
			"",
			&uvc_func_type);
	configfs_add_default_group(&uvcg_control_grp.group,
			&opts->func_inst.group);
	configfs_add_default_group(&uvcg_streaming_grp.group,
			&opts->func_inst.group);

	return 0;
}
