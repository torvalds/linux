/*
 *
 *  $Id$
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
#include <asm/semaphore.h>
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
	int v4l_minor_number_created_ok;
	int v4l_radio_minor_number_created_ok;
	int unit_number_created_ok;
	int bus_info_created_ok;
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
	struct device_attribute attr_enum;
	struct device_attribute attr_bits;
	struct device_attribute attr_val;
	struct device_attribute attr_custom;
	struct pvr2_ctrl *cptr;
	struct pvr2_sysfs *chptr;
	struct pvr2_sysfs_ctl_item *item_next;
	struct attribute *attr_gen[7];
	struct attribute_group grp;
	int created_ok;
	char name[80];
};

struct pvr2_sysfs_class {
	struct class class;
};

static ssize_t show_name(int id,struct device *class_dev,char *buf)
{
	struct pvr2_ctrl *cptr;
	struct pvr2_sysfs *sfp;
	const char *name;

	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	cptr = pvr2_hdw_get_ctrl_by_index(sfp->channel.hdw,id);
	if (!cptr) return -EINVAL;

	name = pvr2_ctrl_get_desc(cptr);
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_name(cid=%d) is %s",sfp,id,name);

	if (!name) return -EINVAL;

	return scnprintf(buf,PAGE_SIZE,"%s\n",name);
}

static ssize_t show_type(int id,struct device *class_dev,char *buf)
{
	struct pvr2_ctrl *cptr;
	struct pvr2_sysfs *sfp;
	const char *name;
	enum pvr2_ctl_type tp;

	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	cptr = pvr2_hdw_get_ctrl_by_index(sfp->channel.hdw,id);
	if (!cptr) return -EINVAL;

	tp = pvr2_ctrl_get_type(cptr);
	switch (tp) {
	case pvr2_ctl_int: name = "integer"; break;
	case pvr2_ctl_enum: name = "enum"; break;
	case pvr2_ctl_bitmask: name = "bitmask"; break;
	case pvr2_ctl_bool: name = "boolean"; break;
	default: name = "?"; break;
	}
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_type(cid=%d) is %s",sfp,id,name);

	if (!name) return -EINVAL;

	return scnprintf(buf,PAGE_SIZE,"%s\n",name);
}

static ssize_t show_min(int id,struct device *class_dev,char *buf)
{
	struct pvr2_ctrl *cptr;
	struct pvr2_sysfs *sfp;
	long val;

	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	cptr = pvr2_hdw_get_ctrl_by_index(sfp->channel.hdw,id);
	if (!cptr) return -EINVAL;
	val = pvr2_ctrl_get_min(cptr);

	pvr2_sysfs_trace("pvr2_sysfs(%p) show_min(cid=%d) is %ld",sfp,id,val);

	return scnprintf(buf,PAGE_SIZE,"%ld\n",val);
}

static ssize_t show_max(int id,struct device *class_dev,char *buf)
{
	struct pvr2_ctrl *cptr;
	struct pvr2_sysfs *sfp;
	long val;

	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	cptr = pvr2_hdw_get_ctrl_by_index(sfp->channel.hdw,id);
	if (!cptr) return -EINVAL;
	val = pvr2_ctrl_get_max(cptr);

	pvr2_sysfs_trace("pvr2_sysfs(%p) show_max(cid=%d) is %ld",sfp,id,val);

	return scnprintf(buf,PAGE_SIZE,"%ld\n",val);
}

static ssize_t show_val_norm(int id,struct device *class_dev,char *buf)
{
	struct pvr2_ctrl *cptr;
	struct pvr2_sysfs *sfp;
	int val,ret;
	unsigned int cnt = 0;

	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	cptr = pvr2_hdw_get_ctrl_by_index(sfp->channel.hdw,id);
	if (!cptr) return -EINVAL;

	ret = pvr2_ctrl_get_value(cptr,&val);
	if (ret < 0) return ret;

	ret = pvr2_ctrl_value_to_sym(cptr,~0,val,
				     buf,PAGE_SIZE-1,&cnt);

	pvr2_sysfs_trace("pvr2_sysfs(%p) show_val_norm(cid=%d) is %.*s (%d)",
			 sfp,id,cnt,buf,val);
	buf[cnt] = '\n';
	return cnt+1;
}

static ssize_t show_val_custom(int id,struct device *class_dev,char *buf)
{
	struct pvr2_ctrl *cptr;
	struct pvr2_sysfs *sfp;
	int val,ret;
	unsigned int cnt = 0;

	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	cptr = pvr2_hdw_get_ctrl_by_index(sfp->channel.hdw,id);
	if (!cptr) return -EINVAL;

	ret = pvr2_ctrl_get_value(cptr,&val);
	if (ret < 0) return ret;

	ret = pvr2_ctrl_custom_value_to_sym(cptr,~0,val,
					    buf,PAGE_SIZE-1,&cnt);

	pvr2_sysfs_trace("pvr2_sysfs(%p) show_val_custom(cid=%d) is %.*s (%d)",
			 sfp,id,cnt,buf,val);
	buf[cnt] = '\n';
	return cnt+1;
}

static ssize_t show_enum(int id,struct device *class_dev,char *buf)
{
	struct pvr2_ctrl *cptr;
	struct pvr2_sysfs *sfp;
	long val;
	unsigned int bcnt,ccnt,ecnt;

	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	cptr = pvr2_hdw_get_ctrl_by_index(sfp->channel.hdw,id);
	if (!cptr) return -EINVAL;
	ecnt = pvr2_ctrl_get_cnt(cptr);
	bcnt = 0;
	for (val = 0; val < ecnt; val++) {
		pvr2_ctrl_get_valname(cptr,val,buf+bcnt,PAGE_SIZE-bcnt,&ccnt);
		if (!ccnt) continue;
		bcnt += ccnt;
		if (bcnt >= PAGE_SIZE) break;
		buf[bcnt] = '\n';
		bcnt++;
	}
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_enum(cid=%d)",sfp,id);
	return bcnt;
}

static ssize_t show_bits(int id,struct device *class_dev,char *buf)
{
	struct pvr2_ctrl *cptr;
	struct pvr2_sysfs *sfp;
	int valid_bits,msk;
	unsigned int bcnt,ccnt;

	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	cptr = pvr2_hdw_get_ctrl_by_index(sfp->channel.hdw,id);
	if (!cptr) return -EINVAL;
	valid_bits = pvr2_ctrl_get_mask(cptr);
	bcnt = 0;
	for (msk = 1; valid_bits; msk <<= 1) {
		if (!(msk & valid_bits)) continue;
		valid_bits &= ~msk;
		pvr2_ctrl_get_valname(cptr,msk,buf+bcnt,PAGE_SIZE-bcnt,&ccnt);
		bcnt += ccnt;
		if (bcnt >= PAGE_SIZE) break;
		buf[bcnt] = '\n';
		bcnt++;
	}
	pvr2_sysfs_trace("pvr2_sysfs(%p) show_bits(cid=%d)",sfp,id);
	return bcnt;
}

static int store_val_any(int id,int customfl,struct pvr2_sysfs *sfp,
			 const char *buf,unsigned int count)
{
	struct pvr2_ctrl *cptr;
	int ret;
	int mask,val;

	cptr = pvr2_hdw_get_ctrl_by_index(sfp->channel.hdw,id);
	if (customfl) {
		ret = pvr2_ctrl_custom_sym_to_value(cptr,buf,count,&mask,&val);
	} else {
		ret = pvr2_ctrl_sym_to_value(cptr,buf,count,&mask,&val);
	}
	if (ret < 0) return ret;
	ret = pvr2_ctrl_set_mask_value(cptr,mask,val);
	pvr2_hdw_commit_ctl(sfp->channel.hdw);
	return ret;
}

static ssize_t store_val_norm(int id,struct device *class_dev,
			     const char *buf,size_t count)
{
	struct pvr2_sysfs *sfp;
	int ret;
	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	ret = store_val_any(id,0,sfp,buf,count);
	if (!ret) ret = count;
	return ret;
}

static ssize_t store_val_custom(int id,struct device *class_dev,
				const char *buf,size_t count)
{
	struct pvr2_sysfs *sfp;
	int ret;
	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	ret = store_val_any(id,1,sfp,buf,count);
	if (!ret) ret = count;
	return ret;
}

/*
  Mike Isely <isely@pobox.com> 30-April-2005

  This next batch of horrible preprocessor hackery is needed because the
  kernel's device_attribute mechanism fails to pass the actual
  attribute through to the show / store functions, which means we have no
  way to package up any attribute-specific parameters, like for example the
  control id.  So we work around this brain-damage by encoding the control
  id into the show / store functions themselves and pick the function based
  on the control id we're setting up.  These macros try to ease the pain.
  Yuck.
*/

#define CREATE_SHOW_INSTANCE(sf_name,ctl_id) \
static ssize_t sf_name##_##ctl_id(struct device *class_dev, \
struct device_attribute *attr, char *buf) \
{ return sf_name(ctl_id,class_dev,buf); }

#define CREATE_STORE_INSTANCE(sf_name,ctl_id) \
static ssize_t sf_name##_##ctl_id(struct device *class_dev, \
struct device_attribute *attr, const char *buf, size_t count) \
{ return sf_name(ctl_id,class_dev,buf,count); }

#define CREATE_BATCH(ctl_id) \
CREATE_SHOW_INSTANCE(show_name,ctl_id) \
CREATE_SHOW_INSTANCE(show_type,ctl_id) \
CREATE_SHOW_INSTANCE(show_min,ctl_id) \
CREATE_SHOW_INSTANCE(show_max,ctl_id) \
CREATE_SHOW_INSTANCE(show_val_norm,ctl_id) \
CREATE_SHOW_INSTANCE(show_val_custom,ctl_id) \
CREATE_SHOW_INSTANCE(show_enum,ctl_id) \
CREATE_SHOW_INSTANCE(show_bits,ctl_id) \
CREATE_STORE_INSTANCE(store_val_norm,ctl_id) \
CREATE_STORE_INSTANCE(store_val_custom,ctl_id) \

CREATE_BATCH(0)
CREATE_BATCH(1)
CREATE_BATCH(2)
CREATE_BATCH(3)
CREATE_BATCH(4)
CREATE_BATCH(5)
CREATE_BATCH(6)
CREATE_BATCH(7)
CREATE_BATCH(8)
CREATE_BATCH(9)
CREATE_BATCH(10)
CREATE_BATCH(11)
CREATE_BATCH(12)
CREATE_BATCH(13)
CREATE_BATCH(14)
CREATE_BATCH(15)
CREATE_BATCH(16)
CREATE_BATCH(17)
CREATE_BATCH(18)
CREATE_BATCH(19)
CREATE_BATCH(20)
CREATE_BATCH(21)
CREATE_BATCH(22)
CREATE_BATCH(23)
CREATE_BATCH(24)
CREATE_BATCH(25)
CREATE_BATCH(26)
CREATE_BATCH(27)
CREATE_BATCH(28)
CREATE_BATCH(29)
CREATE_BATCH(30)
CREATE_BATCH(31)
CREATE_BATCH(32)
CREATE_BATCH(33)
CREATE_BATCH(34)
CREATE_BATCH(35)
CREATE_BATCH(36)
CREATE_BATCH(37)
CREATE_BATCH(38)
CREATE_BATCH(39)
CREATE_BATCH(40)
CREATE_BATCH(41)
CREATE_BATCH(42)
CREATE_BATCH(43)
CREATE_BATCH(44)
CREATE_BATCH(45)
CREATE_BATCH(46)
CREATE_BATCH(47)
CREATE_BATCH(48)
CREATE_BATCH(49)
CREATE_BATCH(50)
CREATE_BATCH(51)
CREATE_BATCH(52)
CREATE_BATCH(53)
CREATE_BATCH(54)
CREATE_BATCH(55)
CREATE_BATCH(56)
CREATE_BATCH(57)
CREATE_BATCH(58)
CREATE_BATCH(59)

struct pvr2_sysfs_func_set {
	ssize_t (*show_name)(struct device *,
			     struct device_attribute *attr, char *);
	ssize_t (*show_type)(struct device *,
			     struct device_attribute *attr, char *);
	ssize_t (*show_min)(struct device *,
			    struct device_attribute *attr, char *);
	ssize_t (*show_max)(struct device *,
			    struct device_attribute *attr, char *);
	ssize_t (*show_enum)(struct device *,
			     struct device_attribute *attr, char *);
	ssize_t (*show_bits)(struct device *,
			     struct device_attribute *attr, char *);
	ssize_t (*show_val_norm)(struct device *,
				 struct device_attribute *attr, char *);
	ssize_t (*store_val_norm)(struct device *,
				  struct device_attribute *attr,
				  const char *,size_t);
	ssize_t (*show_val_custom)(struct device *,
				   struct device_attribute *attr, char *);
	ssize_t (*store_val_custom)(struct device *,
				    struct device_attribute *attr,
				    const char *,size_t);
};

#define INIT_BATCH(ctl_id) \
[ctl_id] = { \
    .show_name = show_name_##ctl_id, \
    .show_type = show_type_##ctl_id, \
    .show_min = show_min_##ctl_id, \
    .show_max = show_max_##ctl_id, \
    .show_enum = show_enum_##ctl_id, \
    .show_bits = show_bits_##ctl_id, \
    .show_val_norm = show_val_norm_##ctl_id, \
    .store_val_norm = store_val_norm_##ctl_id, \
    .show_val_custom = show_val_custom_##ctl_id, \
    .store_val_custom = store_val_custom_##ctl_id, \
} \

static struct pvr2_sysfs_func_set funcs[] = {
	INIT_BATCH(0),
	INIT_BATCH(1),
	INIT_BATCH(2),
	INIT_BATCH(3),
	INIT_BATCH(4),
	INIT_BATCH(5),
	INIT_BATCH(6),
	INIT_BATCH(7),
	INIT_BATCH(8),
	INIT_BATCH(9),
	INIT_BATCH(10),
	INIT_BATCH(11),
	INIT_BATCH(12),
	INIT_BATCH(13),
	INIT_BATCH(14),
	INIT_BATCH(15),
	INIT_BATCH(16),
	INIT_BATCH(17),
	INIT_BATCH(18),
	INIT_BATCH(19),
	INIT_BATCH(20),
	INIT_BATCH(21),
	INIT_BATCH(22),
	INIT_BATCH(23),
	INIT_BATCH(24),
	INIT_BATCH(25),
	INIT_BATCH(26),
	INIT_BATCH(27),
	INIT_BATCH(28),
	INIT_BATCH(29),
	INIT_BATCH(30),
	INIT_BATCH(31),
	INIT_BATCH(32),
	INIT_BATCH(33),
	INIT_BATCH(34),
	INIT_BATCH(35),
	INIT_BATCH(36),
	INIT_BATCH(37),
	INIT_BATCH(38),
	INIT_BATCH(39),
	INIT_BATCH(40),
	INIT_BATCH(41),
	INIT_BATCH(42),
	INIT_BATCH(43),
	INIT_BATCH(44),
	INIT_BATCH(45),
	INIT_BATCH(46),
	INIT_BATCH(47),
	INIT_BATCH(48),
	INIT_BATCH(49),
	INIT_BATCH(50),
	INIT_BATCH(51),
	INIT_BATCH(52),
	INIT_BATCH(53),
	INIT_BATCH(54),
	INIT_BATCH(55),
	INIT_BATCH(56),
	INIT_BATCH(57),
	INIT_BATCH(58),
	INIT_BATCH(59),
};


static void pvr2_sysfs_add_control(struct pvr2_sysfs *sfp,int ctl_id)
{
	struct pvr2_sysfs_ctl_item *cip;
	struct pvr2_sysfs_func_set *fp;
	struct pvr2_ctrl *cptr;
	unsigned int cnt,acnt;
	int ret;

	if ((ctl_id < 0) || (ctl_id >= ARRAY_SIZE(funcs))) {
		return;
	}

	fp = funcs + ctl_id;
	cptr = pvr2_hdw_get_ctrl_by_index(sfp->channel.hdw,ctl_id);
	if (!cptr) return;

	cip = kzalloc(sizeof(*cip),GFP_KERNEL);
	if (!cip) return;
	pvr2_sysfs_trace("Creating pvr2_sysfs_ctl_item id=%p",cip);

	cip->cptr = cptr;

	cip->chptr = sfp;
	cip->item_next = NULL;
	if (sfp->item_last) {
		sfp->item_last->item_next = cip;
	} else {
		sfp->item_first = cip;
	}
	sfp->item_last = cip;

	cip->attr_name.attr.name = "name";
	cip->attr_name.attr.mode = S_IRUGO;
	cip->attr_name.show = fp->show_name;

	cip->attr_type.attr.name = "type";
	cip->attr_type.attr.mode = S_IRUGO;
	cip->attr_type.show = fp->show_type;

	cip->attr_min.attr.name = "min_val";
	cip->attr_min.attr.mode = S_IRUGO;
	cip->attr_min.show = fp->show_min;

	cip->attr_max.attr.name = "max_val";
	cip->attr_max.attr.mode = S_IRUGO;
	cip->attr_max.show = fp->show_max;

	cip->attr_val.attr.name = "cur_val";
	cip->attr_val.attr.mode = S_IRUGO;

	cip->attr_custom.attr.name = "custom_val";
	cip->attr_custom.attr.mode = S_IRUGO;

	cip->attr_enum.attr.name = "enum_val";
	cip->attr_enum.attr.mode = S_IRUGO;
	cip->attr_enum.show = fp->show_enum;

	cip->attr_bits.attr.name = "bit_val";
	cip->attr_bits.attr.mode = S_IRUGO;
	cip->attr_bits.show = fp->show_bits;

	if (pvr2_ctrl_is_writable(cptr)) {
		cip->attr_val.attr.mode |= S_IWUSR|S_IWGRP;
		cip->attr_custom.attr.mode |= S_IWUSR|S_IWGRP;
	}

	acnt = 0;
	cip->attr_gen[acnt++] = &cip->attr_name.attr;
	cip->attr_gen[acnt++] = &cip->attr_type.attr;
	cip->attr_gen[acnt++] = &cip->attr_val.attr;
	cip->attr_val.show = fp->show_val_norm;
	cip->attr_val.store = fp->store_val_norm;
	if (pvr2_ctrl_has_custom_symbols(cptr)) {
		cip->attr_gen[acnt++] = &cip->attr_custom.attr;
		cip->attr_custom.show = fp->show_val_custom;
		cip->attr_custom.store = fp->store_val_custom;
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
		printk(KERN_WARNING "%s: sysfs_create_group error: %d\n",
		       __FUNCTION__, ret);
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
	dip->attr_debugcmd.attr.name = "debugcmd";
	dip->attr_debugcmd.attr.mode = S_IRUGO|S_IWUSR|S_IWGRP;
	dip->attr_debugcmd.show = debugcmd_show;
	dip->attr_debugcmd.store = debugcmd_store;
	dip->attr_debuginfo.attr.name = "debuginfo";
	dip->attr_debuginfo.attr.mode = S_IRUGO;
	dip->attr_debuginfo.show = debuginfo_show;
	sfp->debugifc = dip;
	ret = device_create_file(sfp->class_dev,&dip->attr_debugcmd);
	if (ret < 0) {
		printk(KERN_WARNING "%s: device_create_file error: %d\n",
		       __FUNCTION__, ret);
	} else {
		dip->debugcmd_created_ok = !0;
	}
	ret = device_create_file(sfp->class_dev,&dip->attr_debuginfo);
	if (ret < 0) {
		printk(KERN_WARNING "%s: device_create_file error: %d\n",
		       __FUNCTION__, ret);
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
	if (!sfp->class_dev) return;
#ifdef CONFIG_VIDEO_PVRUSB2_DEBUGIFC
	pvr2_sysfs_tear_down_debugifc(sfp);
#endif /* CONFIG_VIDEO_PVRUSB2_DEBUGIFC */
	pvr2_sysfs_tear_down_controls(sfp);
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
	sfp->class_dev->driver_data = NULL;
	device_unregister(sfp->class_dev);
	sfp->class_dev = NULL;
}


static ssize_t v4l_minor_number_show(struct device *class_dev,
				     struct device_attribute *attr, char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	return scnprintf(buf,PAGE_SIZE,"%d\n",
			 pvr2_hdw_v4l_get_minor_number(sfp->channel.hdw,
						       pvr2_v4l_type_video));
}


static ssize_t bus_info_show(struct device *class_dev,
			     struct device_attribute *attr, char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	return scnprintf(buf,PAGE_SIZE,"%s\n",
			 pvr2_hdw_get_bus_info(sfp->channel.hdw));
}


static ssize_t v4l_radio_minor_number_show(struct device *class_dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	return scnprintf(buf,PAGE_SIZE,"%d\n",
			 pvr2_hdw_v4l_get_minor_number(sfp->channel.hdw,
						       pvr2_v4l_type_radio));
}


static ssize_t unit_number_show(struct device *class_dev,
				struct device_attribute *attr, char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
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
	if (pvr2_hdw_get_sn(sfp->channel.hdw)) {
		snprintf(class_dev->bus_id, BUS_ID_SIZE, "sn-%lu",
			 pvr2_hdw_get_sn(sfp->channel.hdw));
	} else if (pvr2_hdw_get_unit_number(sfp->channel.hdw) >= 0) {
		snprintf(class_dev->bus_id, BUS_ID_SIZE, "unit-%c",
			 pvr2_hdw_get_unit_number(sfp->channel.hdw) + 'a');
	} else {
		kfree(class_dev);
		return;
	}

	class_dev->parent = &usb_dev->dev;

	sfp->class_dev = class_dev;
	class_dev->driver_data = sfp;
	ret = device_register(class_dev);
	if (ret) {
		printk(KERN_ERR "%s: device_register failed\n",
		       __FUNCTION__);
		kfree(class_dev);
		return;
	}

	sfp->attr_v4l_minor_number.attr.name = "v4l_minor_number";
	sfp->attr_v4l_minor_number.attr.mode = S_IRUGO;
	sfp->attr_v4l_minor_number.show = v4l_minor_number_show;
	sfp->attr_v4l_minor_number.store = NULL;
	ret = device_create_file(sfp->class_dev,
				       &sfp->attr_v4l_minor_number);
	if (ret < 0) {
		printk(KERN_WARNING "%s: device_create_file error: %d\n",
		       __FUNCTION__, ret);
	} else {
		sfp->v4l_minor_number_created_ok = !0;
	}

	sfp->attr_v4l_radio_minor_number.attr.name = "v4l_radio_minor_number";
	sfp->attr_v4l_radio_minor_number.attr.mode = S_IRUGO;
	sfp->attr_v4l_radio_minor_number.show = v4l_radio_minor_number_show;
	sfp->attr_v4l_radio_minor_number.store = NULL;
	ret = device_create_file(sfp->class_dev,
				       &sfp->attr_v4l_radio_minor_number);
	if (ret < 0) {
		printk(KERN_WARNING "%s: device_create_file error: %d\n",
		       __FUNCTION__, ret);
	} else {
		sfp->v4l_radio_minor_number_created_ok = !0;
	}

	sfp->attr_unit_number.attr.name = "unit_number";
	sfp->attr_unit_number.attr.mode = S_IRUGO;
	sfp->attr_unit_number.show = unit_number_show;
	sfp->attr_unit_number.store = NULL;
	ret = device_create_file(sfp->class_dev,&sfp->attr_unit_number);
	if (ret < 0) {
		printk(KERN_WARNING "%s: device_create_file error: %d\n",
		       __FUNCTION__, ret);
	} else {
		sfp->unit_number_created_ok = !0;
	}

	sfp->attr_bus_info.attr.name = "bus_info_str";
	sfp->attr_bus_info.attr.mode = S_IRUGO;
	sfp->attr_bus_info.show = bus_info_show;
	sfp->attr_bus_info.store = NULL;
	ret = device_create_file(sfp->class_dev,
				       &sfp->attr_bus_info);
	if (ret < 0) {
		printk(KERN_WARNING "%s: device_create_file error: %d\n",
		       __FUNCTION__, ret);
	} else {
		sfp->bus_info_created_ok = !0;
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


static int pvr2_sysfs_hotplug(struct device *cd,char **envp,
			      int numenvp,char *buf,int size)
{
	/* Even though we don't do anything here, we still need this function
	   because sysfs will still try to call it. */
	return 0;
}

struct pvr2_sysfs_class *pvr2_sysfs_class_create(void)
{
	struct pvr2_sysfs_class *clp;
	clp = kzalloc(sizeof(*clp),GFP_KERNEL);
	if (!clp) return clp;
	pvr2_sysfs_trace("Creating pvr2_sysfs_class id=%p",clp);
	clp->class.name = "pvrusb2";
	clp->class.class_release = pvr2_sysfs_class_release;
	clp->class.dev_release = pvr2_sysfs_release;
	clp->class.dev_uevent = pvr2_sysfs_hotplug;
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
	class_unregister(&clp->class);
}


#ifdef CONFIG_VIDEO_PVRUSB2_DEBUGIFC
static ssize_t debuginfo_show(struct device *class_dev,
			      struct device_attribute *attr, char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	pvr2_hdw_trigger_module_log(sfp->channel.hdw);
	return pvr2_debugifc_print_info(sfp->channel.hdw,buf,PAGE_SIZE);
}


static ssize_t debugcmd_show(struct device *class_dev,
			     struct device_attribute *attr, char *buf)
{
	struct pvr2_sysfs *sfp;
	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
	if (!sfp) return -EINVAL;
	return pvr2_debugifc_print_status(sfp->channel.hdw,buf,PAGE_SIZE);
}


static ssize_t debugcmd_store(struct device *class_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct pvr2_sysfs *sfp;
	int ret;

	sfp = (struct pvr2_sysfs *)class_dev->driver_data;
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
