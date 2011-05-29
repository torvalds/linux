/*
 *
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/string.h>
#include <linux/slab.h>
#include "pvrusb2-sysfs.h"
#include "pvrusb2-hdw.h"
#include "pvrusb2-debug.h"
#ifdef CONFIG_VIDEO_PVRUSB2_DEBUGIFC
#include "pvrusb2-debugifc.h"
#endif /* CONFIG_VIDEO_PVRUSB2_DEBUGIFC */

#define pvr2_sysfs_trace(...) pvr2_trace(PVR2_TRACE_SYSFS,__VA_ARGS__)

struct pvr2_sysfs {
	struct pvr2_channel channel;
	struct device *class_dev;
#ifdef CONFIG_VIDEO_PVRUSB2_DEBUGIFC
	struct pvr2_sysfs_debugifc *debugifc;
#endif /* CONFIG_VIDEO_PVRUSB2_DEBUGIFC */
	struct pvr2_sysfs_ctl_item *item_first;
	struct pvr2_sysfs_ctl_item *item_last;
	struct device_attribute attr_v4l_minor_number;
	struct device_attribute attr_v4l_radio_minor_number;
	struct device_attribute attr_unit_number;
	struct device_attribute attr_bus_info;
	struct device_attribute attr_hdw_name;
	struct device_attribute attr_hdw_desc;
	int v4l_minor_number_created_ok;
	int v4l_radio_minor_number_created_ok;
	int unit_number_created_ok;
	int bus_info_created_ok;
	int hdw_name_created_ok;
	int hdw_desc_created_ok;
};

#ifdef CONFIG_VIDEO_PVRUSB2_DEBUGIFC
struct pvr2_sysfs_debugifc {
	struct device_attribute attr_debugcmd;
	struct device_attribute attr_debuginfo;
	int debugcmd_created_ok;
	int debuginfo_created_ok;
};
#endif /* CONFIG_VIDEO_PVRUSB2_DEBUGIFC */

struct pvr2_sysfs_ctl_item {
	struct device_attribute attr_name;
	struct device_attribute attr_type;
	struct device_attribute attr_min;
	struct device_attribute attr_max;
	struct device_attribute attr_def;
	struct device_attribute attr_enum;
	struct device_attribute attr_bits;
	struct device_attribute attr_val;
	struct device_attribute attr_custom;
	struct pvr2_ctrl *cptr;
	int ctl_id;
	struct pvr2_sysfs *chptr;
	struct pvr2_sysfs_ctl_item *item_next;
	struct attribute *attr_gen[8];
	struct attribute_group grp;
	int created_ok;
	char name[80];
};

struct pvr2_sysfs_class {
	struct class class;
};

static ssize_t show_name(struct device *class_dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct pvr2_sysfs_ctl_item *cip;
	const char *name;
	cip = container_of(attr, struct pvr2_sysfs_ctl_item, attr_name);
	name = pvr2_ctrl_get_desc(cip->cptr);
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_name(cid=%d) is %s",
			 cip->chptr, cip->ctl_id, name);
	if (!name) return -EINVAL;
	return scnprintf(buf, PAGE_SIZE, "%s\n", name);
}

static ssize_t show_type(struct device *class_dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct pvr2_sysfs_ctl_item *cip;
	const char *name;
	enum pvr2_ctl_type tp;
	cip = container_of(attr, struct pvr2_sysfs_ctl_item, attr_type);
	tp = pvr2_ctrl_get_type(cip->cptr);
	switch (tp) {
	case pvr2_ctl_int: name = "integer"; break;
	case pvr2_ctl_enum: name = "enum"; break;
	case pvr2_ctl_bitmask: name = "bitmask"; break;
	case pvr2_ctl_bool: name = "boolean"; break;
	default: name = "?"; break;
	}
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_type(cid=%d) is %s",
			 cip->chptr, cip->ctl_id, name);
	if (!name) return -EINVAL;
	return scnprintf(buf, PAGE_SIZE, "%s\n", name);
}

static ssize_t show_min(struct device *class_dev,
			struct device_attribute *attr,
			char *buf)
{
	struct pvr2_sysfs_ctl_item *cip;
	long val;
	cip = container_of(attr, struct pvr2_sysfs_ctl_item, attr_min);
	val = pvr2_ctrl_get_min(cip->cptr);
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_min(cid=%d) is %ld",
			 cip->chptr, cip->ctl_id, val);
	return scnprintf(buf, PAGE_SIZE, "%ld\n", val);
}

static ssize_t show_max(struct device *class_dev,
			struct device_attribute *attr,
			char *buf)
{
	struct pvr2_sysfs_ctl_item *cip;
	long val;
	cip = container_of(attr, struct pvr2_sysfs_ctl_item, attr_max);
	val = pvr2_ctrl_get_max(cip->cptr);
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_max(cid=%d) is %ld",
			 cip->chptr, cip->ctl_id, val);
	return scnprintf(buf, PAGE_SIZE, "%ld\n", val);
}

static ssize_t show_def(struct device *class_dev,
			struct device_attribute *attr,
			char *buf)
{
	struct pvr2_sysfs_ctl_item *cip;
	int val;
	int ret;
	unsigned int cnt = 0;
	cip = container_of(attr, struct pvr2_sysfs_ctl_item, attr_def);
	ret = pvr2_ctrl_get_def(cip->cptr, &val);
	if (ret < 0) return ret;
	ret = pvr2_ctrl_value_to_sym(cip->cptr, ~0, val,
				     buf, PAGE_SIZE - 1, &cnt);
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_def(cid=%d) is %.*s (%d)",
			 cip->chptr, cip->ctl_id, cnt, buf, val);
	buf[cnt] = '\n';
	return cnt + 1;
}

static ssize_t show_val_norm(struct device *class_dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct pvr2_sysfs_ctl_item *cip;
	int val;
	int ret;
	unsigned int cnt = 0;
	cip = container_of(attr, struct pvr2_sysfs_ctl_item, attr_val);
	ret = pvr2_ctrl_get_value(cip->cptr, &val);
	if (ret < 0) return ret;
	ret = pvr2_ctrl_value_to_sym(cip->cptr, ~0, val,
				     buf, PAGE_SIZE - 1, &cnt);
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_val_norm(cid=%d) is %.*s (%d)",
			 cip->chptr, cip->ctl_id, cnt, buf, val);
	buf[cnt] = '\n';
	return cnt+1;
}

static ssize_t show_val_custom(struct device *class_dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct pvr2_sysfs_ctl_item *cip;
	int val;
	int ret;
	unsigned int cnt = 0;
	cip = container_of(attr, struct pvr2_sysfs_ctl_item, attr_custom);
	ret = pvr2_ctrl_get_value(cip->cptr, &val);
	if (ret < 0) return ret;
	ret = pvr2_ctrl_custom_value_to_sym(cip->cptr, ~0, val,
					    buf, PAGE_SIZE - 1, &cnt);
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_val_custom(cid=%d) is %.*s (%d)",
			 cip->chptr, cip->ctl_id, cnt, buf, val);
	buf[cnt] = '\n';
	return cnt+1;
}

static ssize_t show_enum(struct device *class_dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct pvr2_sysfs_ctl_item *cip;
	long val;
	unsigned int bcnt, ccnt, ecnt;
	cip = container_of(attr, struct pvr2_sysfs_ctl_item, attr_enum);
	ecnt = pvr2_ctrl_get_cnt(cip->cptr);
	bcnt = 0;
	for (val = 0; val < ecnt; val++) {
		pvr2_ctrl_get_valname(cip->cptr, val, buf + bcnt,
				      PAGE_SIZE - bcnt, &ccnt);
		if (!ccnt) continue;
		bcnt += ccnt;
		if (bcnt >= PAGE_SIZE) break;
		buf[bcnt] = '\n';
		bcnt++;
	}
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_enum(cid=%d)",
			 cip->chptr, cip->ctl_id);
	return bcnt;
}

static ssize_t show_bits(struct device *class_dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct pvr2_sysfs_ctl_item *cip;
	int valid_bits, msk;
	unsigned int bcnt, ccnt;
	cip = container_of(attr, struct pvr2_sysfs_ctl_item, attr_bits);
	valid_bits = pvr2_ctrl_get_mask(cip->cptr);
	bcnt = 0;
	for (msk = 1; valid_bits; msk <<= 1) {
		if (!(msk & valid_bits)) continue;
		valid_bits &= ~msk;
		pvr2_ctrl_get_valname(cip->cptr, msk, buf + bcnt,
				      PAGE_SIZE - bcnt, &ccnt);
		bcnt += ccnt;
		if (bcnt >= PAGE_SIZE) break;
		buf[bcnt] = '\n';
		bcnt++;
	}
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_bits(cid=%d)",
			 cip->chptr, cip->ctl_id);
	return bcnt;
}

static int store_val_any(struct pvr2_sysfs_ctl_item *cip, int customfl,
			 const char *buf,unsigned int count)
{
	int ret;
	int mask,val;
	if (customfl) {
		ret = pvr2_ctrl_custom_sym_to_value(cip->cptr, buf, count,
						    &mask, &val);
	} else {
		ret = pvr2_ctrl_sym_to_value(cip->cptr, buf, count,
					     &mask, &val);
	}
	if (ret < 0) return ret;
	ret = pvr2_ctrl_set_mask_value(cip->cptr, mask, val);
	pvr2_hdw_commit_ctl(cip->chptr->channel.hdw);
	return ret;
}

static ssize_t store_val_norm(struct device *class_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct pvr2_sysfs_ctl_item *cip;
	int ret;
	cip = container_of(attr, struct pvr2_sysfs_ctl_item, attr_val);
	pvr2_sysfs_trace("pvr2_sysfs(%p) store_val_norm(cid=%d) \"%.*s\"",
			 cip->chptr, cip->ctl_id, (int)count, buf);
	ret = store_val_any(cip, 0, buf, count);
	if (!ret) ret = count;
	return ret;
}

static ssize_t store_val_custom(struct device *class_dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct pvr2_sysfs_ctl_item *cip;
	int ret;
	cip = container_of(attr, struct pvr2_sysfs_ctl_item, attr_custom);
	pvr2_sysfs_trace("pvr2_sysfs(%p) store_val_custom(cid=%d) \"%.*s\"",
			 cip->chptr, cip->ctl_id, (int)count, buf);
	ret = store_val_any(cip, 1, buf, count);
	if (!ret) ret = count;
	return ret;
}

static void pvr2_sysfs_add_control(struct pvr2_sysfs *sfp,int ctl_id)
{
	struct pvr2_sysfs_ctl_item *cip;
	struct pvr2_ctrl *cptr;
	unsigned int cnt,acnt;
	int ret;

	cptr = pvr2_hdw_get_ctrl_by_index(sfp->channel.hdw,ctl_id);
	if (!cptr) return;

	cip = kzalloc(sizeof(*cip),GFP_KERNEL);
	if (!cip) return;
	pvr2_sysfs_trace("Creating pvr2_sysfs_ctl_item id=%p",cip);

	cip->cptr = cptr;
	cip->ctl_id = ctl_id;

	cip->chptr = sfp;
	cip->item_next = NULL;
	if (sfp->item_last) {
		sfp->item_last->item_next = cip;
	} else {
		sfp->item_first = cip;
	}
	sfp->item_last = cip;

	sysfs_attr_init(&cip->attr_name.attr);
	cip->attr_name.attr.name = "name";
	cip->attr_name.attr.mode = S_IRUGO;
	cip->attr_name.show = show_name;

	sysfs_attr_init(&cip->attr_type.attr);
	cip->attr_type.attr.name = "type";
	cip->attr_type.attr.mode = S_IRUGO;
	cip->attr_type.show = show_type;

	sysfs_attr_init(&cip->attr_min.attr);
	cip->attr_min.attr.name = "min_val";
	cip->attr_min.attr.mode = S_IRUGO;
	cip->attr_min.show = show_min;

	sysfs_attr_init(&cip->attr_max.attr);
	cip->attr_max.attr.name = "max_val";
	cip->attr_max.attr.mode = S_IRUGO;
	cip->attr_max.show = show_max;

	sysfs_attr_init(&cip->attr_def.attr);
	cip->attr_def.attr.name = "def_val";
	cip->attr_def.attr.mode = S_IRUGO;
	cip->attr_def.show = show_def;

	sysfs_attr_init(&cip->attr_val.attr);
	cip->attr_val.attr.name = "cur_val";
	cip->attr_val.attr.mode = S_IRUGO;

	sysfs_attr_init(&cip->attr_custom.attr);
	cip->attr_custom.attr.name = "custom_val";
	cip->attr_custom.attr.mode = S_IRUGO;

	sysfs_attr_init(&cip->attr_enum.attr);
	cip->attr_enum.attr.name = "enum_val";
	cip->attr_enum.attr.mode = S_IRUGO;
	cip->attr_enum.show = show_enum;

	sysfs_attr_init(&cip->attr_bits.attr);
	cip->attr_bits.attr.name = "bit_val";
	cip->attr_bits.attr.mode = S_IRUGO;
	cip->attr_bits.show = show_bits;

	if (pvr2_ctrl_is_writable(cptr)) {
		cip->attr_val.attr.mode |= S_IWUSR|S_IWGRP;
		cip->attr_custom.attr.mode |= S_IWUSR|S_IWGRP;
	}

	acnt = 0;
	cip->attr_gen[acnt++] = &cip->attr_name.attr;
	cip->attr_gen[acnt++] = &cip->attr_type.attr;
	cip->attr_gen[acnt++] = &cip->attr_val.attr;
	cip->attr_gen[acnt++] = &cip->attr_def.attr;
	cip->attr_val.show = show_val_norm;
	cip->attr_val.store = store_val_norm;
	if (pvr2_ctrl_has_custom_symbols(cptr)) {
		cip->attr_gen[acnt++] = &cip->attr_custom.attr;
		cip->attr_custom.show = show_val_custom;
		cip->attr_custom.store = store_val_custom;
	}
	switch (pvr2_ctrl_get_type(cptr)) {
	case pvr2_ctl_enum:
		// Control is an enumeration
		cip->attr_gen[acnt++] = &cip->attr_enum.attr;
		break;
	case pvr2_ctl_int:
		// Control is an integer
		cip->attr_gen[acnt++] = &cip->attr_min.attr;
		cip->attr_gen[acnt++] = &cip->attr_max.attr;
		break;
	case pvr2_ctl_bitmask:
		// Control is an bitmask
		cip->attr_gen[acnt++] = &cip->attr_bits.attr;
		break;
	default: break;
	}

	cnt = scnprintf(cip->name,sizeof(cip->name)-1,"ctl_%s",
			pvr2_ctrl_get_name(cptr));
	cip->name[cnt] = 0;
	cip->grp.name = cip->name;
	cip->grp.attrs = cip->attr_gen;

	ret = sysfs_create_group(&sfp->class_dev->kobj,&cip->grp);
	if (ret) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "sysfs_create_group error: %d",
			   ret);
		return;
	}
	cip->created_ok = !0;
}

#ifdef CONFIG_VIDEO_PVRUSB2_DEBUGIFC
static ssize_t debuginfo_show(struct device *, struct device_attribute *,
			      char *);
static ssize_t debugcmd_show(struct device *, struct device_attribute *,
			     char *);
static ssize_t debugcmd_store(struct device *, struct device_attribute *,
			      const char *, size_t count);

static void pvr2_sysfs_add_debugifc(struct pvr2_sysfs *sfp)
{
	struct pvr2_sysfs_debugifc *dip;
	int ret;

	dip = kzalloc(sizeof(*dip),GFP_KERNEL);
	if (!dip) return;
	sysfs_attr_init(&dip->attr_debugcmd.attr);
	dip->attr_debugcmd.attr.name = "debugcmd";
	dip->attr_debugcmd.attr.mode = S_IRUGO|S_IWUSR|S_IWGRP;
	dip->attr_debugcmd.show = debugcmd_show;
	dip->attr_debugcmd.store = debugcmd_store;
	sysfs_attr_init(&dip->attr_debuginfo.attr);
	dip->attr_debuginfo.attr.name = "debuginfo";
	dip->attr_debuginfo.attr.mode = S_IRUGO;
	dip->attr_debuginfo.show = debuginfo_show;
	sfp->debugifc = dip;
	ret = device_create_file(sfp->class_dev,&dip->attr_debugcmd);
	if (ret < 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "device_create_file error: %d",
			   ret);
	} else {
		dip->debugcmd_created_ok = !0;
	}
	ret = device_create_file(sfp->class_dev,&dip->attr_debuginfo);
	if (ret < 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "device_create_file error: %d",
			   ret);
	} else {
		dip->debuginfo_created_ok = !0;
	}
}


static void pvr2_sysfs_tear_down_debugifc(struct pvr2_sysfs *sfp)
{
	if (!sfp->debugifc) return;
	if (sfp->debugifc->debuginfo_created_ok) {
		device_remove_file(sfp->class_dev,
					 &sfp->debugifc->attr_debuginfo);
	}
	if (sfp->debugifc->debugcmd_created_ok) {
		device_remove_file(sfp->class_dev,
					 &sfp->debugifc->attr_debugcmd);
	}
	kfree(sfp->debugifc);
	sfp->debugifc = NULL;
}
#endif /* CONFIG_VIDEO_PVRUSB2_DEBUGIFC */


static void pvr2_sysfs_add_controls(struct pvr2_sysfs *sfp)
{
	unsigned int idx,cnt;
	cnt = pvr2_hdw_get_ctrl_count(sfp->channel.hdw);
	for (idx = 0; idx < cnt; idx++) {
		pvr2_sysfs_add_control(sfp,idx);
	}
}


static void pvr2_sysfs_tear_down_controls(struct pvr2_sysfs *sfp)
{
	struct pvr2_sysfs_ctl_item *cip1,*cip2;
	for (cip1 = sfp->item_first; cip1; cip1 = cip2) {
		cip2 = cip1->item_next;
		if (cip1->created_ok) {
			sysfs_remove_group(&sfp->class_dev->kobj,&cip1->grp);
		}
		pvr2_sysfs_trace("Destroying pvr2_sysfs_ctl_item id=%p",cip1);
		kfree(cip1);
	}
}


static void pvr2_sysfs_class_release(struct class *class)
{
	struct pvr2_sysfs_class *clp;
	clp = container_of(class,struct pvr2_sysfs_class,class);
	pvr2_sysfs_trace("Destroying pvr2_sysfs_class id=%p",clp);
	kfree(clp);
}


static void pvr2_sysfs_release(struct device *class_dev)
{
	pvr2_sysfs_trace("Releasing class_dev id=%p",class_dev);
	kfree(class_dev);
}


static void class_dev_destroy(struct pvr2_sysfs *sfp)
{
	struct device *dev;
	if (!sfp->class_dev) return;
#ifdef CONFIG_VIDEO_PVRUSB2_DEBUGIFC
	pvr2_sysfs_tear_down_debugifc(sfp);
#endif /* CONFIG_VIDEO_PVRUSB2_DEBUGIFC */
	pvr2_sysfs_tear_down_controls(sfp);
	if (sfp->hdw_desc_created_ok) {
		device_remove_file(sfp->class_dev,
				   &sfp->attr_hdw_desc);
	}
	if (sfp->hdw_name_created_ok) {
		device_remove_file(sfp->class_dev,
				   &sfp->attr_hdw_name);
	}
	if (sfp->bus_info_created_ok) {
		device_remove_file(sfp->class_dev,
					 &sfp->attr_bus_info);
	}
	if (sfp->v4l_minor_number_created_ok) {
		device_remove_file(sfp->class_dev,
					 &sfp->attr_v4l_minor_number);
	}
	if (sfp->v4l_radio_minor_number_created_ok) {
		device_remove_file(sfp->class_dev,
					 &sfp->attr_v4l_radio_minor_number);
	}
	if (sfp->unit_number_created_ok) {
		device_remove_file(sfp->class_dev,
					 &sfp->attr_unit_number);
	}
	pvr2_sysfs_trace("Destroying class_dev id=%p",sfp->class_dev);
	dev_set_drvdata(sfp->class_dev, NULL);
	dev = sfp->class_dev->parent;
	sfp->class_dev->parent = NULL;
	put_device(dev);
	device_unregister(sfp->class_dev);
	sfp->class_dev = NULL;
}


static ssize_t v4l_minor_number_show(struct device *class_dev,
				     struct device_attribute *attr, char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = dev_get_drvdata(class_dev);
	if (!sfp) return -EINVAL;
	return scnprintf(buf,PAGE_SIZE,"%d\n",
			 pvr2_hdw_v4l_get_minor_number(sfp->channel.hdw,
						       pvr2_v4l_type_video));
}


static ssize_t bus_info_show(struct device *class_dev,
			     struct device_attribute *attr, char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = dev_get_drvdata(class_dev);
	if (!sfp) return -EINVAL;
	return scnprintf(buf,PAGE_SIZE,"%s\n",
			 pvr2_hdw_get_bus_info(sfp->channel.hdw));
}


static ssize_t hdw_name_show(struct device *class_dev,
			     struct device_attribute *attr, char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = dev_get_drvdata(class_dev);
	if (!sfp) return -EINVAL;
	return scnprintf(buf,PAGE_SIZE,"%s\n",
			 pvr2_hdw_get_type(sfp->channel.hdw));
}


static ssize_t hdw_desc_show(struct device *class_dev,
			     struct device_attribute *attr, char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = dev_get_drvdata(class_dev);
	if (!sfp) return -EINVAL;
	return scnprintf(buf,PAGE_SIZE,"%s\n",
			 pvr2_hdw_get_desc(sfp->channel.hdw));
}


static ssize_t v4l_radio_minor_number_show(struct device *class_dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = dev_get_drvdata(class_dev);
	if (!sfp) return -EINVAL;
	return scnprintf(buf,PAGE_SIZE,"%d\n",
			 pvr2_hdw_v4l_get_minor_number(sfp->channel.hdw,
						       pvr2_v4l_type_radio));
}


static ssize_t unit_number_show(struct device *class_dev,
				struct device_attribute *attr, char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = dev_get_drvdata(class_dev);
	if (!sfp) return -EINVAL;
	return scnprintf(buf,PAGE_SIZE,"%d\n",
			 pvr2_hdw_get_unit_number(sfp->channel.hdw));
}


static void class_dev_create(struct pvr2_sysfs *sfp,
			     struct pvr2_sysfs_class *class_ptr)
{
	struct usb_device *usb_dev;
	struct device *class_dev;
	int ret;

	usb_dev = pvr2_hdw_get_dev(sfp->channel.hdw);
	if (!usb_dev) return;
	class_dev = kzalloc(sizeof(*class_dev),GFP_KERNEL);
	if (!class_dev) return;

	pvr2_sysfs_trace("Creating class_dev id=%p",class_dev);

	class_dev->class = &class_ptr->class;

	dev_set_name(class_dev, "%s",
		     pvr2_hdw_get_device_identifier(sfp->channel.hdw));

	class_dev->parent = get_device(&usb_dev->dev);

	sfp->class_dev = class_dev;
	dev_set_drvdata(class_dev, sfp);
	ret = device_register(class_dev);
	if (ret) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "device_register failed");
		put_device(class_dev);
		return;
	}

	sysfs_attr_init(&sfp->attr_v4l_minor_number.attr);
	sfp->attr_v4l_minor_number.attr.name = "v4l_minor_number";
	sfp->attr_v4l_minor_number.attr.mode = S_IRUGO;
	sfp->attr_v4l_minor_number.show = v4l_minor_number_show;
	sfp->attr_v4l_minor_number.store = NULL;
	ret = device_create_file(sfp->class_dev,
				       &sfp->attr_v4l_minor_number);
	if (ret < 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "device_create_file error: %d",
			   ret);
	} else {
		sfp->v4l_minor_number_created_ok = !0;
	}

	sysfs_attr_init(&sfp->attr_v4l_radio_minor_number.attr);
	sfp->attr_v4l_radio_minor_number.attr.name = "v4l_radio_minor_number";
	sfp->attr_v4l_radio_minor_number.attr.mode = S_IRUGO;
	sfp->attr_v4l_radio_minor_number.show = v4l_radio_minor_number_show;
	sfp->attr_v4l_radio_minor_number.store = NULL;
	ret = device_create_file(sfp->class_dev,
				       &sfp->attr_v4l_radio_minor_number);
	if (ret < 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "device_create_file error: %d",
			   ret);
	} else {
		sfp->v4l_radio_minor_number_created_ok = !0;
	}

	sysfs_attr_init(&sfp->attr_unit_number.attr);
	sfp->attr_unit_number.attr.name = "unit_number";
	sfp->attr_unit_number.attr.mode = S_IRUGO;
	sfp->attr_unit_number.show = unit_number_show;
	sfp->attr_unit_number.store = NULL;
	ret = device_create_file(sfp->class_dev,&sfp->attr_unit_number);
	if (ret < 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "device_create_file error: %d",
			   ret);
	} else {
		sfp->unit_number_created_ok = !0;
	}

	sysfs_attr_init(&sfp->attr_bus_info.attr);
	sfp->attr_bus_info.attr.name = "bus_info_str";
	sfp->attr_bus_info.attr.mode = S_IRUGO;
	sfp->attr_bus_info.show = bus_info_show;
	sfp->attr_bus_info.store = NULL;
	ret = device_create_file(sfp->class_dev,
				       &sfp->attr_bus_info);
	if (ret < 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "device_create_file error: %d",
			   ret);
	} else {
		sfp->bus_info_created_ok = !0;
	}

	sysfs_attr_init(&sfp->attr_hdw_name.attr);
	sfp->attr_hdw_name.attr.name = "device_hardware_type";
	sfp->attr_hdw_name.attr.mode = S_IRUGO;
	sfp->attr_hdw_name.show = hdw_name_show;
	sfp->attr_hdw_name.store = NULL;
	ret = device_create_file(sfp->class_dev,
				 &sfp->attr_hdw_name);
	if (ret < 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "device_create_file error: %d",
			   ret);
	} else {
		sfp->hdw_name_created_ok = !0;
	}

	sysfs_attr_init(&sfp->attr_hdw_desc.attr);
	sfp->attr_hdw_desc.attr.name = "device_hardware_description";
	sfp->attr_hdw_desc.attr.mode = S_IRUGO;
	sfp->attr_hdw_desc.show = hdw_desc_show;
	sfp->attr_hdw_desc.store = NULL;
	ret = device_create_file(sfp->class_dev,
				 &sfp->attr_hdw_desc);
	if (ret < 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "device_create_file error: %d",
			   ret);
	} else {
		sfp->hdw_desc_created_ok = !0;
	}

	pvr2_sysfs_add_controls(sfp);
#ifdef CONFIG_VIDEO_PVRUSB2_DEBUGIFC
	pvr2_sysfs_add_debugifc(sfp);
#endif /* CONFIG_VIDEO_PVRUSB2_DEBUGIFC */
}


static void pvr2_sysfs_internal_check(struct pvr2_channel *chp)
{
	struct pvr2_sysfs *sfp;
	sfp = container_of(chp,struct pvr2_sysfs,channel);
	if (!sfp->channel.mc_head->disconnect_flag) return;
	pvr2_trace(PVR2_TRACE_STRUCT,"Destroying pvr2_sysfs id=%p",sfp);
	class_dev_destroy(sfp);
	pvr2_channel_done(&sfp->channel);
	kfree(sfp);
}


struct pvr2_sysfs *pvr2_sysfs_create(struct pvr2_context *mp,
				     struct pvr2_sysfs_class *class_ptr)
{
	struct pvr2_sysfs *sfp;
	sfp = kzalloc(sizeof(*sfp),GFP_KERNEL);
	if (!sfp) return sfp;
	pvr2_trace(PVR2_TRACE_STRUCT,"Creating pvr2_sysfs id=%p",sfp);
	pvr2_channel_init(&sfp->channel,mp);
	sfp->channel.check_func = pvr2_sysfs_internal_check;

	class_dev_create(sfp,class_ptr);
	return sfp;
}



struct pvr2_sysfs_class *pvr2_sysfs_class_create(void)
{
	struct pvr2_sysfs_class *clp;
	clp = kzalloc(sizeof(*clp),GFP_KERNEL);
	if (!clp) return clp;
	pvr2_sysfs_trace("Creating and registering pvr2_sysfs_class id=%p",
			 clp);
	clp->class.name = "pvrusb2";
	clp->class.class_release = pvr2_sysfs_class_release;
	clp->class.dev_release = pvr2_sysfs_release;
	if (class_register(&clp->class)) {
		pvr2_sysfs_trace(
			"Registration failed for pvr2_sysfs_class id=%p",clp);
		kfree(clp);
		clp = NULL;
	}
	return clp;
}


void pvr2_sysfs_class_destroy(struct pvr2_sysfs_class *clp)
{
	pvr2_sysfs_trace("Unregistering pvr2_sysfs_class id=%p", clp);
	class_unregister(&clp->class);
}


#ifdef CONFIG_VIDEO_PVRUSB2_DEBUGIFC
static ssize_t debuginfo_show(struct device *class_dev,
			      struct device_attribute *attr, char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = dev_get_drvdata(class_dev);
	if (!sfp) return -EINVAL;
	pvr2_hdw_trigger_module_log(sfp->channel.hdw);
	return pvr2_debugifc_print_info(sfp->channel.hdw,buf,PAGE_SIZE);
}


static ssize_t debugcmd_show(struct device *class_dev,
			     struct device_attribute *attr, char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = dev_get_drvdata(class_dev);
	if (!sfp) return -EINVAL;
	return pvr2_debugifc_print_status(sfp->channel.hdw,buf,PAGE_SIZE);
}


static ssize_t debugcmd_store(struct device *class_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct pvr2_sysfs *sfp;
	int ret;

	sfp = dev_get_drvdata(class_dev);
	if (!sfp) return -EINVAL;

	ret = pvr2_debugifc_docmd(sfp->channel.hdw,buf,count);
	if (ret < 0) return ret;
	return count;
}
#endif /* CONFIG_VIDEO_PVRUSB2_DEBUGIFC */


/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 75 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */
