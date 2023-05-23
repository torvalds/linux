// SPDX-License-Identifier: GPL-2.0
/*
 * configfs.c - Implementation of configfs interface to the driver stack
 *
 * Copyright (C) 2013-2015 Microchip Technology Germany II GmbH & Co. KG
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/configfs.h>
#include <linux/most.h>

#define MAX_STRING_SIZE 80

struct mdev_link {
	struct config_item item;
	struct list_head list;
	bool create_link;
	bool destroy_link;
	u16 num_buffers;
	u16 buffer_size;
	u16 subbuffer_size;
	u16 packets_per_xact;
	u16 dbr_size;
	char datatype[MAX_STRING_SIZE];
	char direction[MAX_STRING_SIZE];
	char name[MAX_STRING_SIZE];
	char device[MAX_STRING_SIZE];
	char channel[MAX_STRING_SIZE];
	char comp[MAX_STRING_SIZE];
	char comp_params[MAX_STRING_SIZE];
};

static struct list_head mdev_link_list;

static int set_cfg_buffer_size(struct mdev_link *link)
{
	return most_set_cfg_buffer_size(link->device, link->channel,
					link->buffer_size);
}

static int set_cfg_subbuffer_size(struct mdev_link *link)
{
	return most_set_cfg_subbuffer_size(link->device, link->channel,
					   link->subbuffer_size);
}

static int set_cfg_dbr_size(struct mdev_link *link)
{
	return most_set_cfg_dbr_size(link->device, link->channel,
				     link->dbr_size);
}

static int set_cfg_num_buffers(struct mdev_link *link)
{
	return most_set_cfg_num_buffers(link->device, link->channel,
					link->num_buffers);
}

static int set_cfg_packets_xact(struct mdev_link *link)
{
	return most_set_cfg_packets_xact(link->device, link->channel,
					 link->packets_per_xact);
}

static int set_cfg_direction(struct mdev_link *link)
{
	return most_set_cfg_direction(link->device, link->channel,
				      link->direction);
}

static int set_cfg_datatype(struct mdev_link *link)
{
	return most_set_cfg_datatype(link->device, link->channel,
				     link->datatype);
}

static int (*set_config_val[])(struct mdev_link *link) = {
	set_cfg_buffer_size,
	set_cfg_subbuffer_size,
	set_cfg_dbr_size,
	set_cfg_num_buffers,
	set_cfg_packets_xact,
	set_cfg_direction,
	set_cfg_datatype,
};

static struct mdev_link *to_mdev_link(struct config_item *item)
{
	return container_of(item, struct mdev_link, item);
}

static int set_config_and_add_link(struct mdev_link *mdev_link)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(set_config_val); i++) {
		ret = set_config_val[i](mdev_link);
		if (ret < 0 && ret != -ENODEV) {
			pr_err("Config failed\n");
			return ret;
		}
	}

	return most_add_link(mdev_link->device, mdev_link->channel,
			     mdev_link->comp, mdev_link->name,
			     mdev_link->comp_params);
}

static ssize_t mdev_link_create_link_store(struct config_item *item,
					   const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);
	bool tmp;
	int ret;

	ret = kstrtobool(page, &tmp);
	if (ret)
		return ret;
	if (!tmp)
		return count;
	ret = set_config_and_add_link(mdev_link);
	if (ret && ret != -ENODEV)
		return ret;
	list_add_tail(&mdev_link->list, &mdev_link_list);
	mdev_link->create_link = tmp;
	mdev_link->destroy_link = false;

	return count;
}

static ssize_t mdev_link_destroy_link_store(struct config_item *item,
					    const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);
	bool tmp;
	int ret;

	ret = kstrtobool(page, &tmp);
	if (ret)
		return ret;
	if (!tmp)
		return count;

	ret = most_remove_link(mdev_link->device, mdev_link->channel,
			       mdev_link->comp);
	if (ret)
		return ret;
	if (!list_empty(&mdev_link_list))
		list_del(&mdev_link->list);

	mdev_link->destroy_link = tmp;

	return count;
}

static ssize_t mdev_link_direction_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", to_mdev_link(item)->direction);
}

static ssize_t mdev_link_direction_store(struct config_item *item,
					 const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);

	if (!sysfs_streq(page, "dir_rx") && !sysfs_streq(page, "rx") &&
	    !sysfs_streq(page, "dir_tx") && !sysfs_streq(page, "tx"))
		return -EINVAL;
	strcpy(mdev_link->direction, page);
	strim(mdev_link->direction);
	return count;
}

static ssize_t mdev_link_datatype_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", to_mdev_link(item)->datatype);
}

static ssize_t mdev_link_datatype_store(struct config_item *item,
					const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);

	if (!sysfs_streq(page, "control") && !sysfs_streq(page, "async") &&
	    !sysfs_streq(page, "sync") && !sysfs_streq(page, "isoc") &&
	    !sysfs_streq(page, "isoc_avp"))
		return -EINVAL;
	strcpy(mdev_link->datatype, page);
	strim(mdev_link->datatype);
	return count;
}

static ssize_t mdev_link_device_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", to_mdev_link(item)->device);
}

static ssize_t mdev_link_device_store(struct config_item *item,
				      const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);

	strscpy(mdev_link->device, page, sizeof(mdev_link->device));
	strim(mdev_link->device);
	return count;
}

static ssize_t mdev_link_channel_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", to_mdev_link(item)->channel);
}

static ssize_t mdev_link_channel_store(struct config_item *item,
				       const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);

	strscpy(mdev_link->channel, page, sizeof(mdev_link->channel));
	strim(mdev_link->channel);
	return count;
}

static ssize_t mdev_link_comp_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", to_mdev_link(item)->comp);
}

static ssize_t mdev_link_comp_store(struct config_item *item,
				    const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);

	strscpy(mdev_link->comp, page, sizeof(mdev_link->comp));
	strim(mdev_link->comp);
	return count;
}

static ssize_t mdev_link_comp_params_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n",
			to_mdev_link(item)->comp_params);
}

static ssize_t mdev_link_comp_params_store(struct config_item *item,
					   const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);

	strscpy(mdev_link->comp_params, page, sizeof(mdev_link->comp_params));
	strim(mdev_link->comp_params);
	return count;
}

static ssize_t mdev_link_num_buffers_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n",
			to_mdev_link(item)->num_buffers);
}

static ssize_t mdev_link_num_buffers_store(struct config_item *item,
					   const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);
	int ret;

	ret = kstrtou16(page, 0, &mdev_link->num_buffers);
	if (ret)
		return ret;
	return count;
}

static ssize_t mdev_link_buffer_size_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n",
			to_mdev_link(item)->buffer_size);
}

static ssize_t mdev_link_buffer_size_store(struct config_item *item,
					   const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);
	int ret;

	ret = kstrtou16(page, 0, &mdev_link->buffer_size);
	if (ret)
		return ret;
	return count;
}

static ssize_t mdev_link_subbuffer_size_show(struct config_item *item,
					     char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n",
			to_mdev_link(item)->subbuffer_size);
}

static ssize_t mdev_link_subbuffer_size_store(struct config_item *item,
					      const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);
	int ret;

	ret = kstrtou16(page, 0, &mdev_link->subbuffer_size);
	if (ret)
		return ret;
	return count;
}

static ssize_t mdev_link_packets_per_xact_show(struct config_item *item,
					       char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n",
			to_mdev_link(item)->packets_per_xact);
}

static ssize_t mdev_link_packets_per_xact_store(struct config_item *item,
						const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);
	int ret;

	ret = kstrtou16(page, 0, &mdev_link->packets_per_xact);
	if (ret)
		return ret;
	return count;
}

static ssize_t mdev_link_dbr_size_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n", to_mdev_link(item)->dbr_size);
}

static ssize_t mdev_link_dbr_size_store(struct config_item *item,
					const char *page, size_t count)
{
	struct mdev_link *mdev_link = to_mdev_link(item);
	int ret;

	ret = kstrtou16(page, 0, &mdev_link->dbr_size);
	if (ret)
		return ret;
	return count;
}

CONFIGFS_ATTR_WO(mdev_link_, create_link);
CONFIGFS_ATTR_WO(mdev_link_, destroy_link);
CONFIGFS_ATTR(mdev_link_, device);
CONFIGFS_ATTR(mdev_link_, channel);
CONFIGFS_ATTR(mdev_link_, comp);
CONFIGFS_ATTR(mdev_link_, comp_params);
CONFIGFS_ATTR(mdev_link_, num_buffers);
CONFIGFS_ATTR(mdev_link_, buffer_size);
CONFIGFS_ATTR(mdev_link_, subbuffer_size);
CONFIGFS_ATTR(mdev_link_, packets_per_xact);
CONFIGFS_ATTR(mdev_link_, datatype);
CONFIGFS_ATTR(mdev_link_, direction);
CONFIGFS_ATTR(mdev_link_, dbr_size);

static struct configfs_attribute *mdev_link_attrs[] = {
	&mdev_link_attr_create_link,
	&mdev_link_attr_destroy_link,
	&mdev_link_attr_device,
	&mdev_link_attr_channel,
	&mdev_link_attr_comp,
	&mdev_link_attr_comp_params,
	&mdev_link_attr_num_buffers,
	&mdev_link_attr_buffer_size,
	&mdev_link_attr_subbuffer_size,
	&mdev_link_attr_packets_per_xact,
	&mdev_link_attr_datatype,
	&mdev_link_attr_direction,
	&mdev_link_attr_dbr_size,
	NULL,
};

static void mdev_link_release(struct config_item *item)
{
	struct mdev_link *mdev_link = to_mdev_link(item);
	int ret;

	if (mdev_link->destroy_link)
		goto free_item;

	ret = most_remove_link(mdev_link->device, mdev_link->channel,
			       mdev_link->comp);
	if (ret) {
		pr_err("Removing link failed.\n");
		goto free_item;
	}

	if (!list_empty(&mdev_link_list))
		list_del(&mdev_link->list);

free_item:
	kfree(to_mdev_link(item));
}

static struct configfs_item_operations mdev_link_item_ops = {
	.release		= mdev_link_release,
};

static const struct config_item_type mdev_link_type = {
	.ct_item_ops	= &mdev_link_item_ops,
	.ct_attrs	= mdev_link_attrs,
	.ct_owner	= THIS_MODULE,
};

struct most_common {
	struct config_group group;
	struct module *mod;
	struct configfs_subsystem subsys;
};

static struct most_common *to_most_common(struct configfs_subsystem *subsys)
{
	return container_of(subsys, struct most_common, subsys);
}

static struct config_item *most_common_make_item(struct config_group *group,
						 const char *name)
{
	struct mdev_link *mdev_link;
	struct most_common *mc = to_most_common(group->cg_subsys);

	mdev_link = kzalloc(sizeof(*mdev_link), GFP_KERNEL);
	if (!mdev_link)
		return ERR_PTR(-ENOMEM);

	if (!try_module_get(mc->mod)) {
		kfree(mdev_link);
		return ERR_PTR(-ENOLCK);
	}
	config_item_init_type_name(&mdev_link->item, name,
				   &mdev_link_type);

	if (!strcmp(group->cg_item.ci_namebuf, "most_cdev"))
		strcpy(mdev_link->comp, "cdev");
	else if (!strcmp(group->cg_item.ci_namebuf, "most_net"))
		strcpy(mdev_link->comp, "net");
	else if (!strcmp(group->cg_item.ci_namebuf, "most_video"))
		strcpy(mdev_link->comp, "video");
	strcpy(mdev_link->name, name);
	return &mdev_link->item;
}

static void most_common_release(struct config_item *item)
{
	struct config_group *group = to_config_group(item);

	kfree(to_most_common(group->cg_subsys));
}

static struct configfs_item_operations most_common_item_ops = {
	.release	= most_common_release,
};

static void most_common_disconnect(struct config_group *group,
				   struct config_item *item)
{
	struct most_common *mc = to_most_common(group->cg_subsys);

	module_put(mc->mod);
}

static struct configfs_group_operations most_common_group_ops = {
	.make_item	= most_common_make_item,
	.disconnect_notify = most_common_disconnect,
};

static const struct config_item_type most_common_type = {
	.ct_item_ops	= &most_common_item_ops,
	.ct_group_ops	= &most_common_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct most_common most_cdev = {
	.subsys = {
		.su_group = {
			.cg_item = {
				.ci_namebuf = "most_cdev",
				.ci_type = &most_common_type,
			},
		},
	},
};

static struct most_common most_net = {
	.subsys = {
		.su_group = {
			.cg_item = {
				.ci_namebuf = "most_net",
				.ci_type = &most_common_type,
			},
		},
	},
};

static struct most_common most_video = {
	.subsys = {
		.su_group = {
			.cg_item = {
				.ci_namebuf = "most_video",
				.ci_type = &most_common_type,
			},
		},
	},
};

struct most_snd_grp {
	struct config_group group;
	bool create_card;
	struct list_head list;
};

static struct most_snd_grp *to_most_snd_grp(struct config_item *item)
{
	return container_of(to_config_group(item), struct most_snd_grp, group);
}

static struct config_item *most_snd_grp_make_item(struct config_group *group,
						  const char *name)
{
	struct mdev_link *mdev_link;

	mdev_link = kzalloc(sizeof(*mdev_link), GFP_KERNEL);
	if (!mdev_link)
		return ERR_PTR(-ENOMEM);

	config_item_init_type_name(&mdev_link->item, name, &mdev_link_type);
	mdev_link->create_link = false;
	strcpy(mdev_link->name, name);
	strcpy(mdev_link->comp, "sound");
	return &mdev_link->item;
}

static ssize_t most_snd_grp_create_card_store(struct config_item *item,
					      const char *page, size_t count)
{
	struct most_snd_grp *snd_grp = to_most_snd_grp(item);
	int ret;
	bool tmp;

	ret = kstrtobool(page, &tmp);
	if (ret)
		return ret;
	if (tmp) {
		ret = most_cfg_complete("sound");
		if (ret)
			return ret;
	}
	snd_grp->create_card = tmp;
	return count;
}

CONFIGFS_ATTR_WO(most_snd_grp_, create_card);

static struct configfs_attribute *most_snd_grp_attrs[] = {
	&most_snd_grp_attr_create_card,
	NULL,
};

static void most_snd_grp_release(struct config_item *item)
{
	struct most_snd_grp *group = to_most_snd_grp(item);

	list_del(&group->list);
	kfree(group);
}

static struct configfs_item_operations most_snd_grp_item_ops = {
	.release	= most_snd_grp_release,
};

static struct configfs_group_operations most_snd_grp_group_ops = {
	.make_item	= most_snd_grp_make_item,
};

static const struct config_item_type most_snd_grp_type = {
	.ct_item_ops	= &most_snd_grp_item_ops,
	.ct_group_ops	= &most_snd_grp_group_ops,
	.ct_attrs	= most_snd_grp_attrs,
	.ct_owner	= THIS_MODULE,
};

struct most_sound {
	struct configfs_subsystem subsys;
	struct list_head soundcard_list;
	struct module *mod;
};

static struct config_group *most_sound_make_group(struct config_group *group,
						  const char *name)
{
	struct most_snd_grp *most;
	struct most_sound *ms = container_of(group->cg_subsys,
					     struct most_sound, subsys);

	list_for_each_entry(most, &ms->soundcard_list, list) {
		if (!most->create_card) {
			pr_info("adapter configuration still in progress.\n");
			return ERR_PTR(-EPROTO);
		}
	}
	if (!try_module_get(ms->mod))
		return ERR_PTR(-ENOLCK);
	most = kzalloc(sizeof(*most), GFP_KERNEL);
	if (!most) {
		module_put(ms->mod);
		return ERR_PTR(-ENOMEM);
	}
	config_group_init_type_name(&most->group, name, &most_snd_grp_type);
	list_add_tail(&most->list, &ms->soundcard_list);
	return &most->group;
}

static void most_sound_disconnect(struct config_group *group,
				  struct config_item *item)
{
	struct most_sound *ms = container_of(group->cg_subsys,
					     struct most_sound, subsys);
	module_put(ms->mod);
}

static struct configfs_group_operations most_sound_group_ops = {
	.make_group	= most_sound_make_group,
	.disconnect_notify = most_sound_disconnect,
};

static const struct config_item_type most_sound_type = {
	.ct_group_ops	= &most_sound_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct most_sound most_sound_subsys = {
	.subsys = {
		.su_group = {
			.cg_item = {
				.ci_namebuf = "most_sound",
				.ci_type = &most_sound_type,
			},
		},
	},
};

int most_register_configfs_subsys(struct most_component *c)
{
	int ret;

	if (!strcmp(c->name, "cdev")) {
		most_cdev.mod = c->mod;
		ret = configfs_register_subsystem(&most_cdev.subsys);
	} else if (!strcmp(c->name, "net")) {
		most_net.mod = c->mod;
		ret = configfs_register_subsystem(&most_net.subsys);
	} else if (!strcmp(c->name, "video")) {
		most_video.mod = c->mod;
		ret = configfs_register_subsystem(&most_video.subsys);
	} else if (!strcmp(c->name, "sound")) {
		most_sound_subsys.mod = c->mod;
		ret = configfs_register_subsystem(&most_sound_subsys.subsys);
	} else {
		return -ENODEV;
	}

	if (ret) {
		pr_err("Error %d while registering subsystem %s\n",
		       ret, c->name);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(most_register_configfs_subsys);

void most_interface_register_notify(const char *mdev)
{
	bool register_snd_card = false;
	struct mdev_link *mdev_link;

	list_for_each_entry(mdev_link, &mdev_link_list, list) {
		if (!strcmp(mdev_link->device, mdev)) {
			set_config_and_add_link(mdev_link);
			if (!strcmp(mdev_link->comp, "sound"))
				register_snd_card = true;
		}
	}
	if (register_snd_card)
		most_cfg_complete("sound");
}

void most_deregister_configfs_subsys(struct most_component *c)
{
	if (!strcmp(c->name, "cdev"))
		configfs_unregister_subsystem(&most_cdev.subsys);
	else if (!strcmp(c->name, "net"))
		configfs_unregister_subsystem(&most_net.subsys);
	else if (!strcmp(c->name, "video"))
		configfs_unregister_subsystem(&most_video.subsys);
	else if (!strcmp(c->name, "sound"))
		configfs_unregister_subsystem(&most_sound_subsys.subsys);
}
EXPORT_SYMBOL_GPL(most_deregister_configfs_subsys);

int __init configfs_init(void)
{
	config_group_init(&most_cdev.subsys.su_group);
	mutex_init(&most_cdev.subsys.su_mutex);

	config_group_init(&most_net.subsys.su_group);
	mutex_init(&most_net.subsys.su_mutex);

	config_group_init(&most_video.subsys.su_group);
	mutex_init(&most_video.subsys.su_mutex);

	config_group_init(&most_sound_subsys.subsys.su_group);
	mutex_init(&most_sound_subsys.subsys.su_mutex);

	INIT_LIST_HEAD(&most_sound_subsys.soundcard_list);
	INIT_LIST_HEAD(&mdev_link_list);

	return 0;
}
