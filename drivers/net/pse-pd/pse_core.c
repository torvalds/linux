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

static int of_load_single_pse_pi_pairset(struct device_node *node,
					 struct pse_pi *pi,
					 int pairset_num)
{
	struct device_node *pairset_np;
	const char *name;
	int ret;

	ret = of_property_read_string_index(node, "pairset-names",
					    pairset_num, &name);
	if (ret)
		return ret;

	if (!strcmp(name, "alternative-a")) {
		pi->pairset[pairset_num].pinout = ALTERNATIVE_A;
	} else if (!strcmp(name, "alternative-b")) {
		pi->pairset[pairset_num].pinout = ALTERNATIVE_B;
	} else {
		pr_err("pse: wrong pairset-names value %s (%pOF)\n",
		       name, node);
		return -EINVAL;
	}

	pairset_np = of_parse_phandle(node, "pairsets", pairset_num);
	if (!pairset_np)
		return -ENODEV;

	pi->pairset[pairset_num].np = pairset_np;

	return 0;
}

/**
 * of_load_pse_pi_pairsets - load PSE PI pairsets pinout and polarity
 * @node: a pointer of the device node
 * @pi: a pointer of the PSE PI to fill
 * @npairsets: the number of pairsets (1 or 2) used by the PI
 *
 * Return: 0 on success and failure value on error
 */
static int of_load_pse_pi_pairsets(struct device_node *node,
				   struct pse_pi *pi,
				   int npairsets)
{
	int i, ret;

	ret = of_property_count_strings(node, "pairset-names");
	if (ret != npairsets) {
		pr_err("pse: amount of pairsets and pairset-names is not equal %d != %d (%pOF)\n",
		       npairsets, ret, node);
		return -EINVAL;
	}

	for (i = 0; i < npairsets; i++) {
		ret = of_load_single_pse_pi_pairset(node, pi, i);
		if (ret)
			goto out;
	}

	if (npairsets == 2 &&
	    pi->pairset[0].pinout == pi->pairset[1].pinout) {
		pr_err("pse: two PI pairsets can not have identical pinout (%pOF)",
		       node);
		ret = -EINVAL;
	}

out:
	/* If an error appears, release all the pairset device node kref */
	if (ret) {
		of_node_put(pi->pairset[0].np);
		pi->pairset[0].np = NULL;
		of_node_put(pi->pairset[1].np);
		pi->pairset[1].np = NULL;
	}

	return ret;
}

static void pse_release_pis(struct pse_controller_dev *pcdev)
{
	int i;

	for (i = 0; i <= pcdev->nr_lines; i++) {
		of_node_put(pcdev->pi[i].pairset[0].np);
		of_node_put(pcdev->pi[i].pairset[1].np);
		of_node_put(pcdev->pi[i].np);
	}
	kfree(pcdev->pi);
}

/**
 * of_load_pse_pis - load all the PSE PIs
 * @pcdev: a pointer to the PSE controller device
 *
 * Return: 0 on success and failure value on error
 */
static int of_load_pse_pis(struct pse_controller_dev *pcdev)
{
	struct device_node *np = pcdev->dev->of_node;
	struct device_node *node, *pis;
	int ret;

	if (!np)
		return -ENODEV;

	pis = of_get_child_by_name(np, "pse-pis");
	if (!pis) {
		/* no description of PSE PIs */
		pcdev->no_of_pse_pi = true;
		return 0;
	}

	pcdev->pi = kcalloc(pcdev->nr_lines, sizeof(*pcdev->pi), GFP_KERNEL);
	if (!pcdev->pi) {
		of_node_put(pis);
		return -ENOMEM;
	}

	for_each_child_of_node(pis, node) {
		struct pse_pi pi = {0};
		u32 id;

		if (!of_node_name_eq(node, "pse-pi"))
			continue;

		ret = of_property_read_u32(node, "reg", &id);
		if (ret) {
			dev_err(pcdev->dev,
				"can't get reg property for node '%pOF'",
				node);
			goto out;
		}

		if (id >= pcdev->nr_lines) {
			dev_err(pcdev->dev,
				"reg value (%u) is out of range (%u) (%pOF)\n",
				id, pcdev->nr_lines, node);
			ret = -EINVAL;
			goto out;
		}

		if (pcdev->pi[id].np) {
			dev_err(pcdev->dev,
				"other node with same reg value was already registered. %pOF : %pOF\n",
				pcdev->pi[id].np, node);
			ret = -EINVAL;
			goto out;
		}

		ret = of_count_phandle_with_args(node, "pairsets", NULL);
		/* npairsets is limited to value one or two */
		if (ret == 1 || ret == 2) {
			ret = of_load_pse_pi_pairsets(node, &pi, ret);
			if (ret)
				goto out;
		} else if (ret != ENOENT) {
			dev_err(pcdev->dev,
				"error: wrong number of pairsets. Should be 1 or 2, got %d (%pOF)\n",
				ret, node);
			ret = -EINVAL;
			goto out;
		}

		of_node_get(node);
		pi.np = node;
		memcpy(&pcdev->pi[id], &pi, sizeof(pi));
	}

	of_node_put(pis);
	return 0;

out:
	pse_release_pis(pcdev);
	of_node_put(node);
	of_node_put(pis);
	return ret;
}

/**
 * pse_controller_register - register a PSE controller device
 * @pcdev: a pointer to the initialized PSE controller device
 */
int pse_controller_register(struct pse_controller_dev *pcdev)
{
	int ret;

	mutex_init(&pcdev->lock);
	INIT_LIST_HEAD(&pcdev->pse_control_head);

	if (!pcdev->nr_lines)
		pcdev->nr_lines = 1;

	ret = of_load_pse_pis(pcdev);
	if (ret)
		return ret;

	if (pcdev->ops->setup_pi_matrix) {
		ret = pcdev->ops->setup_pi_matrix(pcdev);
		if (ret)
			return ret;
	}

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
	pse_release_pis(pcdev);
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

/**
 * of_pse_match_pi - Find the PSE PI id matching the device node phandle
 * @pcdev: a pointer to the PSE controller device
 * @np: a pointer to the device node
 *
 * Return: id of the PSE PI, -EINVAL if not found
 */
static int of_pse_match_pi(struct pse_controller_dev *pcdev,
			   struct device_node *np)
{
	int i;

	for (i = 0; i <= pcdev->nr_lines; i++) {
		if (pcdev->pi[i].np == np)
			return i;
	}

	return -EINVAL;
}

/**
 * psec_id_xlate - translate pse_spec to the PSE line number according
 *		   to the number of pse-cells in case of no pse_pi node
 * @pcdev: a pointer to the PSE controller device
 * @pse_spec: PSE line specifier as found in the device tree
 *
 * Return: 0 if #pse-cells = <0>. Return PSE line number otherwise.
 */
static int psec_id_xlate(struct pse_controller_dev *pcdev,
			 const struct of_phandle_args *pse_spec)
{
	if (!pcdev->of_pse_n_cells)
		return 0;

	if (pcdev->of_pse_n_cells > 1 ||
	    pse_spec->args[0] >= pcdev->nr_lines)
		return -EINVAL;

	return pse_spec->args[0];
}

struct pse_control *of_pse_control_get(struct device_node *node)
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
		if (!r->no_of_pse_pi) {
			ret = of_pse_match_pi(r, args.np);
			if (ret >= 0) {
				pcdev = r;
				psec_id = ret;
				break;
			}
		} else if (args.np == r->dev->of_node) {
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

	if (pcdev->no_of_pse_pi) {
		psec_id = psec_id_xlate(pcdev, &args);
		if (psec_id < 0) {
			psec = ERR_PTR(psec_id);
			goto out;
		}
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

bool pse_has_podl(struct pse_control *psec)
{
	return psec->pcdev->types & ETHTOOL_PSE_PODL;
}
EXPORT_SYMBOL_GPL(pse_has_podl);

bool pse_has_c33(struct pse_control *psec)
{
	return psec->pcdev->types & ETHTOOL_PSE_C33;
}
EXPORT_SYMBOL_GPL(pse_has_c33);
