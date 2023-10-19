// SPDX-License-Identifier: GPL-2.0-only
//
// Framework for Ethernet Power Sourcing Equipment
//
// Copyright (c) 2022 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
//

#include <linux/device.h>
#include <linux/of.h>
#include <linux/pse-pd/pse.h>

static DEFINE_MUTEX(pse_list_mutex);
static LIST_HEAD(pse_controller_list);

/**
 * struct pse_control - a PSE control
 * @pcdev: a pointer to the PSE controller device
 *         this PSE control belongs to
 * @list: list entry for the pcdev's PSE controller list
 * @id: ID of the PSE line in the PSE controller device
 * @refcnt: Number of gets of this pse_control
 */
struct pse_control {
	struct pse_controller_dev *pcdev;
	struct list_head list;
	unsigned int id;
	struct kref refcnt;
};

/**
 * of_pse_zero_xlate - dummy function for controllers with one only control
 * @pcdev: a pointer to the PSE controller device
 * @pse_spec: PSE line specifier as found in the device tree
 *
 * This static translation function is used by default if of_xlate in
 * :c:type:`pse_controller_dev` is not set. It is useful for all PSE
 * controllers with #pse-cells = <0>.
 */
static int of_pse_zero_xlate(struct pse_controller_dev *pcdev,
			     const struct of_phandle_args *pse_spec)
{
	return 0;
}

/**
 * of_pse_simple_xlate - translate pse_spec to the PSE line number
 * @pcdev: a pointer to the PSE controller device
 * @pse_spec: PSE line specifier as found in the device tree
 *
 * This static translation function is used by default if of_xlate in
 * :c:type:`pse_controller_dev` is not set. It is useful for all PSE
 * controllers with 1:1 mapping, where PSE lines can be indexed by number
 * without gaps.
 */
static int of_pse_simple_xlate(struct pse_controller_dev *pcdev,
			       const struct of_phandle_args *pse_spec)
{
	if (pse_spec->args[0] >= pcdev->nr_lines)
		return -EINVAL;

	return pse_spec->args[0];
}

/**
 * pse_controller_register - register a PSE controller device
 * @pcdev: a pointer to the initialized PSE controller device
 */
int pse_controller_register(struct pse_controller_dev *pcdev)
{
	if (!pcdev->of_xlate) {
		if (pcdev->of_pse_n_cells == 0)
			pcdev->of_xlate = of_pse_zero_xlate;
		else if (pcdev->of_pse_n_cells == 1)
			pcdev->of_xlate = of_pse_simple_xlate;
	}

	mutex_init(&pcdev->lock);
	INIT_LIST_HEAD(&pcdev->pse_control_head);

	mutex_lock(&pse_list_mutex);
	list_add(&pcdev->list, &pse_controller_list);
	mutex_unlock(&pse_list_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(pse_controller_register);

/**
 * pse_controller_unregister - unregister a PSE controller device
 * @pcdev: a pointer to the PSE controller device
 */
void pse_controller_unregister(struct pse_controller_dev *pcdev)
{
	mutex_lock(&pse_list_mutex);
	list_del(&pcdev->list);
	mutex_unlock(&pse_list_mutex);
}
EXPORT_SYMBOL_GPL(pse_controller_unregister);

static void devm_pse_controller_release(struct device *dev, void *res)
{
	pse_controller_unregister(*(struct pse_controller_dev **)res);
}

/**
 * devm_pse_controller_register - resource managed pse_controller_register()
 * @dev: device that is registering this PSE controller
 * @pcdev: a pointer to the initialized PSE controller device
 *
 * Managed pse_controller_register(). For PSE controllers registered by
 * this function, pse_controller_unregister() is automatically called on
 * driver detach. See pse_controller_register() for more information.
 */
int devm_pse_controller_register(struct device *dev,
				 struct pse_controller_dev *pcdev)
{
	struct pse_controller_dev **pcdevp;
	int ret;

	pcdevp = devres_alloc(devm_pse_controller_release, sizeof(*pcdevp),
			      GFP_KERNEL);
	if (!pcdevp)
		return -ENOMEM;

	ret = pse_controller_register(pcdev);
	if (ret) {
		devres_free(pcdevp);
		return ret;
	}

	*pcdevp = pcdev;
	devres_add(dev, pcdevp);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_pse_controller_register);

/* PSE control section */

static void __pse_control_release(struct kref *kref)
{
	struct pse_control *psec = container_of(kref, struct pse_control,
						  refcnt);

	lockdep_assert_held(&pse_list_mutex);

	module_put(psec->pcdev->owner);

	list_del(&psec->list);
	kfree(psec);
}

static void __pse_control_put_internal(struct pse_control *psec)
{
	lockdep_assert_held(&pse_list_mutex);

	kref_put(&psec->refcnt, __pse_control_release);
}

/**
 * pse_control_put - free the PSE control
 * @psec: PSE control pointer
 */
void pse_control_put(struct pse_control *psec)
{
	if (IS_ERR_OR_NULL(psec))
		return;

	mutex_lock(&pse_list_mutex);
	__pse_control_put_internal(psec);
	mutex_unlock(&pse_list_mutex);
}
EXPORT_SYMBOL_GPL(pse_control_put);

static struct pse_control *
pse_control_get_internal(struct pse_controller_dev *pcdev, unsigned int index)
{
	struct pse_control *psec;

	lockdep_assert_held(&pse_list_mutex);

	list_for_each_entry(psec, &pcdev->pse_control_head, list) {
		if (psec->id == index) {
			kref_get(&psec->refcnt);
			return psec;
		}
	}

	psec = kzalloc(sizeof(*psec), GFP_KERNEL);
	if (!psec)
		return ERR_PTR(-ENOMEM);

	if (!try_module_get(pcdev->owner)) {
		kfree(psec);
		return ERR_PTR(-ENODEV);
	}

	psec->pcdev = pcdev;
	list_add(&psec->list, &pcdev->pse_control_head);
	psec->id = index;
	kref_init(&psec->refcnt);

	return psec;
}

struct pse_control *
of_pse_control_get(struct device_node *node)
{
	struct pse_controller_dev *r, *pcdev;
	struct of_phandle_args args;
	struct pse_control *psec;
	int psec_id;
	int ret;

	if (!node)
		return ERR_PTR(-EINVAL);

	ret = of_parse_phandle_with_args(node, "pses", "#pse-cells", 0, &args);
	if (ret)
		return ERR_PTR(ret);

	mutex_lock(&pse_list_mutex);
	pcdev = NULL;
	list_for_each_entry(r, &pse_controller_list, list) {
		if (args.np == r->dev->of_node) {
			pcdev = r;
			break;
		}
	}

	if (!pcdev) {
		psec = ERR_PTR(-EPROBE_DEFER);
		goto out;
	}

	if (WARN_ON(args.args_count != pcdev->of_pse_n_cells)) {
		psec = ERR_PTR(-EINVAL);
		goto out;
	}

	psec_id = pcdev->of_xlate(pcdev, &args);
	if (psec_id < 0) {
		psec = ERR_PTR(psec_id);
		goto out;
	}

	/* pse_list_mutex also protects the pcdev's pse_control list */
	psec = pse_control_get_internal(pcdev, psec_id);

out:
	mutex_unlock(&pse_list_mutex);
	of_node_put(args.np);

	return psec;
}
EXPORT_SYMBOL_GPL(of_pse_control_get);

/**
 * pse_ethtool_get_status - get status of PSE control
 * @psec: PSE control pointer
 * @extack: extack for reporting useful error messages
 * @status: struct to store PSE status
 */
int pse_ethtool_get_status(struct pse_control *psec,
			   struct netlink_ext_ack *extack,
			   struct pse_control_status *status)
{
	const struct pse_controller_ops *ops;
	int err;

	ops = psec->pcdev->ops;

	if (!ops->ethtool_get_status) {
		NL_SET_ERR_MSG(extack,
			       "PSE driver does not support status report");
		return -EOPNOTSUPP;
	}

	mutex_lock(&psec->pcdev->lock);
	err = ops->ethtool_get_status(psec->pcdev, psec->id, extack, status);
	mutex_unlock(&psec->pcdev->lock);

	return err;
}
EXPORT_SYMBOL_GPL(pse_ethtool_get_status);

/**
 * pse_ethtool_set_config - set PSE control configuration
 * @psec: PSE control pointer
 * @extack: extack for reporting useful error messages
 * @config: Configuration of the test to run
 */
int pse_ethtool_set_config(struct pse_control *psec,
			   struct netlink_ext_ack *extack,
			   const struct pse_control_config *config)
{
	const struct pse_controller_ops *ops;
	int err;

	ops = psec->pcdev->ops;

	if (!ops->ethtool_set_config) {
		NL_SET_ERR_MSG(extack,
			       "PSE driver does not configuration");
		return -EOPNOTSUPP;
	}

	mutex_lock(&psec->pcdev->lock);
	err = ops->ethtool_set_config(psec->pcdev, psec->id, extack, config);
	mutex_unlock(&psec->pcdev->lock);

	return err;
}
EXPORT_SYMBOL_GPL(pse_ethtool_set_config);
