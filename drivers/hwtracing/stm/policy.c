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

struct stp_policy_yesde {
	struct config_group	group;
	struct stp_policy	*policy;
	unsigned int		first_master;
	unsigned int		last_master;
	unsigned int		first_channel;
	unsigned int		last_channel;
	/* this is the one that's exposed to the attributes */
	unsigned char		priv[0];
};

void *stp_policy_yesde_priv(struct stp_policy_yesde *pn)
{
	if (!pn)
		return NULL;

	return pn->priv;
}

static struct configfs_subsystem stp_policy_subsys;

void stp_policy_yesde_get_ranges(struct stp_policy_yesde *policy_yesde,
				unsigned int *mstart, unsigned int *mend,
				unsigned int *cstart, unsigned int *cend)
{
	*mstart	= policy_yesde->first_master;
	*mend	= policy_yesde->last_master;
	*cstart	= policy_yesde->first_channel;
	*cend	= policy_yesde->last_channel;
}

static inline char *stp_policy_yesde_name(struct stp_policy_yesde *policy_yesde)
{
	return policy_yesde->group.cg_item.ci_name ? : "<yesne>";
}

static inline struct stp_policy *to_stp_policy(struct config_item *item)
{
	return item ?
		container_of(to_config_group(item), struct stp_policy, group) :
		NULL;
}

static inline struct stp_policy_yesde *
to_stp_policy_yesde(struct config_item *item)
{
	return item ?
		container_of(to_config_group(item), struct stp_policy_yesde,
			     group) :
		NULL;
}

void *to_pdrv_policy_yesde(struct config_item *item)
{
	struct stp_policy_yesde *yesde = to_stp_policy_yesde(item);

	return stp_policy_yesde_priv(yesde);
}
EXPORT_SYMBOL_GPL(to_pdrv_policy_yesde);

static ssize_t
stp_policy_yesde_masters_show(struct config_item *item, char *page)
{
	struct stp_policy_yesde *policy_yesde = to_stp_policy_yesde(item);
	ssize_t count;

	count = sprintf(page, "%u %u\n", policy_yesde->first_master,
			policy_yesde->last_master);

	return count;
}

static ssize_t
stp_policy_yesde_masters_store(struct config_item *item, const char *page,
			      size_t count)
{
	struct stp_policy_yesde *policy_yesde = to_stp_policy_yesde(item);
	unsigned int first, last;
	struct stm_device *stm;
	char *p = (char *)page;
	ssize_t ret = -ENODEV;

	if (sscanf(p, "%u %u", &first, &last) != 2)
		return -EINVAL;

	mutex_lock(&stp_policy_subsys.su_mutex);
	stm = policy_yesde->policy->stm;
	if (!stm)
		goto unlock;

	/* must be within [sw_start..sw_end], which is an inclusive range */
	if (first > last || first < stm->data->sw_start ||
	    last > stm->data->sw_end) {
		ret = -ERANGE;
		goto unlock;
	}

	ret = count;
	policy_yesde->first_master = first;
	policy_yesde->last_master = last;

unlock:
	mutex_unlock(&stp_policy_subsys.su_mutex);

	return ret;
}

static ssize_t
stp_policy_yesde_channels_show(struct config_item *item, char *page)
{
	struct stp_policy_yesde *policy_yesde = to_stp_policy_yesde(item);
	ssize_t count;

	count = sprintf(page, "%u %u\n", policy_yesde->first_channel,
			policy_yesde->last_channel);

	return count;
}

static ssize_t
stp_policy_yesde_channels_store(struct config_item *item, const char *page,
			       size_t count)
{
	struct stp_policy_yesde *policy_yesde = to_stp_policy_yesde(item);
	unsigned int first, last;
	struct stm_device *stm;
	char *p = (char *)page;
	ssize_t ret = -ENODEV;

	if (sscanf(p, "%u %u", &first, &last) != 2)
		return -EINVAL;

	mutex_lock(&stp_policy_subsys.su_mutex);
	stm = policy_yesde->policy->stm;
	if (!stm)
		goto unlock;

	if (first > INT_MAX || last > INT_MAX || first > last ||
	    last >= stm->data->sw_nchannels) {
		ret = -ERANGE;
		goto unlock;
	}

	ret = count;
	policy_yesde->first_channel = first;
	policy_yesde->last_channel = last;

unlock:
	mutex_unlock(&stp_policy_subsys.su_mutex);

	return ret;
}

static void stp_policy_yesde_release(struct config_item *item)
{
	struct stp_policy_yesde *yesde = to_stp_policy_yesde(item);

	kfree(yesde);
}

static struct configfs_item_operations stp_policy_yesde_item_ops = {
	.release		= stp_policy_yesde_release,
};

CONFIGFS_ATTR(stp_policy_yesde_, masters);
CONFIGFS_ATTR(stp_policy_yesde_, channels);

static struct configfs_attribute *stp_policy_yesde_attrs[] = {
	&stp_policy_yesde_attr_masters,
	&stp_policy_yesde_attr_channels,
	NULL,
};

static const struct config_item_type stp_policy_type;
static const struct config_item_type stp_policy_yesde_type;

const struct config_item_type *
get_policy_yesde_type(struct configfs_attribute **attrs)
{
	struct config_item_type *type;
	struct configfs_attribute **merged;

	type = kmemdup(&stp_policy_yesde_type, sizeof(stp_policy_yesde_type),
		       GFP_KERNEL);
	if (!type)
		return NULL;

	merged = memcat_p(stp_policy_yesde_attrs, attrs);
	if (!merged) {
		kfree(type);
		return NULL;
	}

	type->ct_attrs = merged;

	return type;
}

static struct config_group *
stp_policy_yesde_make(struct config_group *group, const char *name)
{
	const struct config_item_type *type = &stp_policy_yesde_type;
	struct stp_policy_yesde *policy_yesde, *parent_yesde;
	const struct stm_protocol_driver *pdrv;
	struct stp_policy *policy;

	if (group->cg_item.ci_type == &stp_policy_type) {
		policy = container_of(group, struct stp_policy, group);
	} else {
		parent_yesde = container_of(group, struct stp_policy_yesde,
					   group);
		policy = parent_yesde->policy;
	}

	if (!policy->stm)
		return ERR_PTR(-ENODEV);

	pdrv = policy->stm->pdrv;
	policy_yesde =
		kzalloc(offsetof(struct stp_policy_yesde, priv[pdrv->priv_sz]),
			GFP_KERNEL);
	if (!policy_yesde)
		return ERR_PTR(-ENOMEM);

	if (pdrv->policy_yesde_init)
		pdrv->policy_yesde_init((void *)policy_yesde->priv);

	if (policy->stm->pdrv_yesde_type)
		type = policy->stm->pdrv_yesde_type;

	config_group_init_type_name(&policy_yesde->group, name, type);

	policy_yesde->policy = policy;

	/* default values for the attributes */
	policy_yesde->first_master = policy->stm->data->sw_start;
	policy_yesde->last_master = policy->stm->data->sw_end;
	policy_yesde->first_channel = 0;
	policy_yesde->last_channel = policy->stm->data->sw_nchannels - 1;

	return &policy_yesde->group;
}

static void
stp_policy_yesde_drop(struct config_group *group, struct config_item *item)
{
	config_item_put(item);
}

static struct configfs_group_operations stp_policy_yesde_group_ops = {
	.make_group	= stp_policy_yesde_make,
	.drop_item	= stp_policy_yesde_drop,
};

static const struct config_item_type stp_policy_yesde_type = {
	.ct_item_ops	= &stp_policy_yesde_item_ops,
	.ct_group_ops	= &stp_policy_yesde_group_ops,
	.ct_attrs	= stp_policy_yesde_attrs,
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
			"<yesne>");

	return count;
}

CONFIGFS_ATTR_RO(stp_policy_, device);

static ssize_t stp_policy_protocol_show(struct config_item *item,
					char *page)
{
	struct stp_policy *policy = to_stp_policy(item);
	ssize_t count;

	count = sprintf(page, "%s\n",
			(policy && policy->stm) ?
			policy->stm->pdrv->name :
			"<yesne>");

	return count;
}

CONFIGFS_ATTR_RO(stp_policy_, protocol);

static struct configfs_attribute *stp_policy_attrs[] = {
	&stp_policy_attr_device,
	&stp_policy_attr_protocol,
	NULL,
};

void stp_policy_unbind(struct stp_policy *policy)
{
	struct stm_device *stm = policy->stm;

	/*
	 * stp_policy_release() will yest call here if the policy is already
	 * unbound; other users should yest either, as yes link exists between
	 * this policy and anything else in that case
	 */
	if (WARN_ON_ONCE(!policy->stm))
		return;

	lockdep_assert_held(&stm->policy_mutex);

	stm->policy = NULL;
	policy->stm = NULL;

	/*
	 * Drop the reference on the protocol driver and lose the link.
	 */
	stm_put_protocol(stm->pdrv);
	stm->pdrv = NULL;
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
	.make_group	= stp_policy_yesde_make,
};

static const struct config_item_type stp_policy_type = {
	.ct_item_ops	= &stp_policy_item_ops,
	.ct_group_ops	= &stp_policy_group_ops,
	.ct_attrs	= stp_policy_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *
stp_policy_make(struct config_group *group, const char *name)
{
	const struct config_item_type *pdrv_yesde_type;
	const struct stm_protocol_driver *pdrv;
	char *devname, *proto, *p;
	struct config_group *ret;
	struct stm_device *stm;
	int err;

	devname = kasprintf(GFP_KERNEL, "%s", name);
	if (!devname)
		return ERR_PTR(-ENOMEM);

	/*
	 * yesde must look like <device_name>.<policy_name>, where
	 * <device_name> is the name of an existing stm device; may
	 *               contain dots;
	 * <policy_name> is an arbitrary string; may yest contain dots
	 * <device_name>:<protocol_name>.<policy_name>
	 */
	p = strrchr(devname, '.');
	if (!p) {
		kfree(devname);
		return ERR_PTR(-EINVAL);
	}

	*p = '\0';

	/*
	 * look for ":<protocol_name>":
	 *  + yes protocol suffix: fall back to whatever is available;
	 *  + unkyeswn protocol: fail the whole thing
	 */
	proto = strrchr(devname, ':');
	if (proto)
		*proto++ = '\0';

	stm = stm_find_device(devname);
	if (!stm) {
		kfree(devname);
		return ERR_PTR(-ENODEV);
	}

	err = stm_lookup_protocol(proto, &pdrv, &pdrv_yesde_type);
	kfree(devname);

	if (err) {
		stm_put_device(stm);
		return ERR_PTR(-ENODEV);
	}

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

	stm->pdrv = pdrv;
	stm->pdrv_yesde_type = pdrv_yesde_type;
	stm->policy->stm = stm;
	ret = &stm->policy->group;

unlock_policy:
	mutex_unlock(&stm->policy_mutex);

	if (IS_ERR(ret)) {
		/*
		 * pdrv and stm->pdrv at this point can be quite different,
		 * and only one of them needs to be 'put'
		 */
		stm_put_protocol(pdrv);
		stm_put_device(stm);
	}

	return ret;
}

static struct configfs_group_operations stp_policy_root_group_ops = {
	.make_group	= stp_policy_make,
};

static const struct config_item_type stp_policy_root_type = {
	.ct_group_ops	= &stp_policy_root_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem stp_policy_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf	= "stp-policy",
			.ci_type	= &stp_policy_root_type,
		},
	},
};

/*
 * Lock the policy mutex from the outside
 */
static struct stp_policy_yesde *
__stp_policy_yesde_lookup(struct stp_policy *policy, char *s)
{
	struct stp_policy_yesde *policy_yesde, *ret = NULL;
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
			policy_yesde = to_stp_policy_yesde(item);

			if (!strcmp(start,
				    policy_yesde->group.cg_item.ci_name)) {
				ret = policy_yesde;

				if (!end)
					goto out;

				head = &policy_yesde->group.cg_children;
				goto next;
			}
		}
		break;
	}

out:
	return ret;
}


struct stp_policy_yesde *
stp_policy_yesde_lookup(struct stm_device *stm, char *s)
{
	struct stp_policy_yesde *policy_yesde = NULL;

	mutex_lock(&stp_policy_subsys.su_mutex);

	mutex_lock(&stm->policy_mutex);
	if (stm->policy)
		policy_yesde = __stp_policy_yesde_lookup(stm->policy, s);
	mutex_unlock(&stm->policy_mutex);

	if (policy_yesde)
		config_item_get(&policy_yesde->group.cg_item);
	else
		mutex_unlock(&stp_policy_subsys.su_mutex);

	return policy_yesde;
}

void stp_policy_yesde_put(struct stp_policy_yesde *policy_yesde)
{
	lockdep_assert_held(&stp_policy_subsys.su_mutex);

	mutex_unlock(&stp_policy_subsys.su_mutex);
	config_item_put(&policy_yesde->group.cg_item);
}

int __init stp_configfs_init(void)
{
	config_group_init(&stp_policy_subsys.su_group);
	mutex_init(&stp_policy_subsys.su_mutex);
	return configfs_register_subsystem(&stp_policy_subsys);
}

void __exit stp_configfs_exit(void)
{
	configfs_unregister_subsystem(&stp_policy_subsys);
}
