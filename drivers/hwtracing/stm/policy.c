// SPDX-License-Identifier: GPL-2.0
/*
 * System Trace Module (STM) master/channel allocation policy management
 * Copyright (c) 2014, Intel Corporation.
 *
 * A master/channel allocation policy allows mapping string identifiers to
 * master and channel ranges, where allocation can be done.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/configfs.h>
#include <linux/slab.h>
#include <linux/stm.h>
#include "stm.h"

/*
 * STP Master/Channel allocation policy configfs layout.
 */

struct stp_policy {
	struct config_group	group;
	struct stm_device	*stm;
};

struct stp_policy_node {
	struct config_group	group;
	struct stp_policy	*policy;
	unsigned int		first_master;
	unsigned int		last_master;
	unsigned int		first_channel;
	unsigned int		last_channel;
};

static struct configfs_subsystem stp_policy_subsys;

void stp_policy_node_get_ranges(struct stp_policy_node *policy_node,
				unsigned int *mstart, unsigned int *mend,
				unsigned int *cstart, unsigned int *cend)
{
	*mstart	= policy_node->first_master;
	*mend	= policy_node->last_master;
	*cstart	= policy_node->first_channel;
	*cend	= policy_node->last_channel;
}

static inline char *stp_policy_node_name(struct stp_policy_node *policy_node)
{
	return policy_node->group.cg_item.ci_name ? : "<none>";
}

static inline struct stp_policy *to_stp_policy(struct config_item *item)
{
	return item ?
		container_of(to_config_group(item), struct stp_policy, group) :
		NULL;
}

static inline struct stp_policy_node *
to_stp_policy_node(struct config_item *item)
{
	return item ?
		container_of(to_config_group(item), struct stp_policy_node,
			     group) :
		NULL;
}

static ssize_t
stp_policy_node_masters_show(struct config_item *item, char *page)
{
	struct stp_policy_node *policy_node = to_stp_policy_node(item);
	ssize_t count;

	count = sprintf(page, "%u %u\n", policy_node->first_master,
			policy_node->last_master);

	return count;
}

static ssize_t
stp_policy_node_masters_store(struct config_item *item, const char *page,
			      size_t count)
{
	struct stp_policy_node *policy_node = to_stp_policy_node(item);
	unsigned int first, last;
	struct stm_device *stm;
	char *p = (char *)page;
	ssize_t ret = -ENODEV;

	if (sscanf(p, "%u %u", &first, &last) != 2)
		return -EINVAL;

	mutex_lock(&stp_policy_subsys.su_mutex);
	stm = policy_node->policy->stm;
	if (!stm)
		goto unlock;

	/* must be within [sw_start..sw_end], which is an inclusive range */
	if (first > last || first < stm->data->sw_start ||
	    last > stm->data->sw_end) {
		ret = -ERANGE;
		goto unlock;
	}

	ret = count;
	policy_node->first_master = first;
	policy_node->last_master = last;

unlock:
	mutex_unlock(&stp_policy_subsys.su_mutex);

	return ret;
}

static ssize_t
stp_policy_node_channels_show(struct config_item *item, char *page)
{
	struct stp_policy_node *policy_node = to_stp_policy_node(item);
	ssize_t count;

	count = sprintf(page, "%u %u\n", policy_node->first_channel,
			policy_node->last_channel);

	return count;
}

static ssize_t
stp_policy_node_channels_store(struct config_item *item, const char *page,
			       size_t count)
{
	struct stp_policy_node *policy_node = to_stp_policy_node(item);
	unsigned int first, last;
	struct stm_device *stm;
	char *p = (char *)page;
	ssize_t ret = -ENODEV;

	if (sscanf(p, "%u %u", &first, &last) != 2)
		return -EINVAL;

	mutex_lock(&stp_policy_subsys.su_mutex);
	stm = policy_node->policy->stm;
	if (!stm)
		goto unlock;

	if (first > INT_MAX || last > INT_MAX || first > last ||
	    last >= stm->data->sw_nchannels) {
		ret = -ERANGE;
		goto unlock;
	}

	ret = count;
	policy_node->first_channel = first;
	policy_node->last_channel = last;

unlock:
	mutex_unlock(&stp_policy_subsys.su_mutex);

	return ret;
}

static void stp_policy_node_release(struct config_item *item)
{
	kfree(to_stp_policy_node(item));
}

static struct configfs_item_operations stp_policy_node_item_ops = {
	.release		= stp_policy_node_release,
};

CONFIGFS_ATTR(stp_policy_node_, masters);
CONFIGFS_ATTR(stp_policy_node_, channels);

static struct configfs_attribute *stp_policy_node_attrs[] = {
	&stp_policy_node_attr_masters,
	&stp_policy_node_attr_channels,
	NULL,
};

static const struct config_item_type stp_policy_type;
static const struct config_item_type stp_policy_node_type;

static struct config_group *
stp_policy_node_make(struct config_group *group, const char *name)
{
	struct stp_policy_node *policy_node, *parent_node;
	struct stp_policy *policy;

	if (group->cg_item.ci_type == &stp_policy_type) {
		policy = container_of(group, struct stp_policy, group);
	} else {
		parent_node = container_of(group, struct stp_policy_node,
					   group);
		policy = parent_node->policy;
	}

	if (!policy->stm)
		return ERR_PTR(-ENODEV);

	policy_node = kzalloc(sizeof(struct stp_policy_node), GFP_KERNEL);
	if (!policy_node)
		return ERR_PTR(-ENOMEM);

	config_group_init_type_name(&policy_node->group, name,
				    &stp_policy_node_type);

	policy_node->policy = policy;

	/* default values for the attributes */
	policy_node->first_master = policy->stm->data->sw_start;
	policy_node->last_master = policy->stm->data->sw_end;
	policy_node->first_channel = 0;
	policy_node->last_channel = policy->stm->data->sw_nchannels - 1;

	return &policy_node->group;
}

static void
stp_policy_node_drop(struct config_group *group, struct config_item *item)
{
	config_item_put(item);
}

static struct configfs_group_operations stp_policy_node_group_ops = {
	.make_group	= stp_policy_node_make,
	.drop_item	= stp_policy_node_drop,
};

static const struct config_item_type stp_policy_node_type = {
	.ct_item_ops	= &stp_policy_node_item_ops,
	.ct_group_ops	= &stp_policy_node_group_ops,
	.ct_attrs	= stp_policy_node_attrs,
	.ct_owner	= THIS_MODULE,
};

/*
 * Root group: policies.
 */
static ssize_t stp_policy_device_show(struct config_item *item,
				      char *page)
{
	struct stp_policy *policy = to_stp_policy(item);
	ssize_t count;

	count = sprintf(page, "%s\n",
			(policy && policy->stm) ?
			policy->stm->data->name :
			"<none>");

	return count;
}

CONFIGFS_ATTR_RO(stp_policy_, device);

static struct configfs_attribute *stp_policy_attrs[] = {
	&stp_policy_attr_device,
	NULL,
};

void stp_policy_unbind(struct stp_policy *policy)
{
	struct stm_device *stm = policy->stm;

	/*
	 * stp_policy_release() will not call here if the policy is already
	 * unbound; other users should not either, as no link exists between
	 * this policy and anything else in that case
	 */
	if (WARN_ON_ONCE(!policy->stm))
		return;

	lockdep_assert_held(&stm->policy_mutex);

	stm->policy = NULL;
	policy->stm = NULL;

	stm_put_device(stm);
}

static void stp_policy_release(struct config_item *item)
{
	struct stp_policy *policy = to_stp_policy(item);
	struct stm_device *stm = policy->stm;

	/* a policy *can* be unbound and still exist in configfs tree */
	if (!stm)
		return;

	mutex_lock(&stm->policy_mutex);
	stp_policy_unbind(policy);
	mutex_unlock(&stm->policy_mutex);

	kfree(policy);
}

static struct configfs_item_operations stp_policy_item_ops = {
	.release		= stp_policy_release,
};

static struct configfs_group_operations stp_policy_group_ops = {
	.make_group	= stp_policy_node_make,
};

static const struct config_item_type stp_policy_type = {
	.ct_item_ops	= &stp_policy_item_ops,
	.ct_group_ops	= &stp_policy_group_ops,
	.ct_attrs	= stp_policy_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *
stp_policies_make(struct config_group *group, const char *name)
{
	struct config_group *ret;
	struct stm_device *stm;
	char *devname, *p;

	devname = kasprintf(GFP_KERNEL, "%s", name);
	if (!devname)
		return ERR_PTR(-ENOMEM);

	/*
	 * node must look like <device_name>.<policy_name>, where
	 * <device_name> is the name of an existing stm device; may
	 *               contain dots;
	 * <policy_name> is an arbitrary string; may not contain dots
	 */
	p = strrchr(devname, '.');
	if (!p) {
		kfree(devname);
		return ERR_PTR(-EINVAL);
	}

	*p = '\0';

	stm = stm_find_device(devname);
	kfree(devname);

	if (!stm)
		return ERR_PTR(-ENODEV);

	mutex_lock(&stm->policy_mutex);
	if (stm->policy) {
		ret = ERR_PTR(-EBUSY);
		goto unlock_policy;
	}

	stm->policy = kzalloc(sizeof(*stm->policy), GFP_KERNEL);
	if (!stm->policy) {
		ret = ERR_PTR(-ENOMEM);
		goto unlock_policy;
	}

	config_group_init_type_name(&stm->policy->group, name,
				    &stp_policy_type);
	stm->policy->stm = stm;

	ret = &stm->policy->group;

unlock_policy:
	mutex_unlock(&stm->policy_mutex);

	if (IS_ERR(ret))
		stm_put_device(stm);

	return ret;
}

static struct configfs_group_operations stp_policies_group_ops = {
	.make_group	= stp_policies_make,
};

static const struct config_item_type stp_policies_type = {
	.ct_group_ops	= &stp_policies_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem stp_policy_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf	= "stp-policy",
			.ci_type	= &stp_policies_type,
		},
	},
};

/*
 * Lock the policy mutex from the outside
 */
static struct stp_policy_node *
__stp_policy_node_lookup(struct stp_policy *policy, char *s)
{
	struct stp_policy_node *policy_node, *ret = NULL;
	struct list_head *head = &policy->group.cg_children;
	struct config_item *item;
	char *start, *end = s;

	if (list_empty(head))
		return NULL;

next:
	for (;;) {
		start = strsep(&end, "/");
		if (!start)
			break;

		if (!*start)
			continue;

		list_for_each_entry(item, head, ci_entry) {
			policy_node = to_stp_policy_node(item);

			if (!strcmp(start,
				    policy_node->group.cg_item.ci_name)) {
				ret = policy_node;

				if (!end)
					goto out;

				head = &policy_node->group.cg_children;
				goto next;
			}
		}
		break;
	}

out:
	return ret;
}


struct stp_policy_node *
stp_policy_node_lookup(struct stm_device *stm, char *s)
{
	struct stp_policy_node *policy_node = NULL;

	mutex_lock(&stp_policy_subsys.su_mutex);

	mutex_lock(&stm->policy_mutex);
	if (stm->policy)
		policy_node = __stp_policy_node_lookup(stm->policy, s);
	mutex_unlock(&stm->policy_mutex);

	if (policy_node)
		config_item_get(&policy_node->group.cg_item);
	else
		mutex_unlock(&stp_policy_subsys.su_mutex);

	return policy_node;
}

void stp_policy_node_put(struct stp_policy_node *policy_node)
{
	lockdep_assert_held(&stp_policy_subsys.su_mutex);

	mutex_unlock(&stp_policy_subsys.su_mutex);
	config_item_put(&policy_node->group.cg_item);
}

int __init stp_configfs_init(void)
{
	int err;

	config_group_init(&stp_policy_subsys.su_group);
	mutex_init(&stp_policy_subsys.su_mutex);
	err = configfs_register_subsystem(&stp_policy_subsys);

	return err;
}

void __exit stp_configfs_exit(void)
{
	configfs_unregister_subsystem(&stp_policy_subsys);
}
