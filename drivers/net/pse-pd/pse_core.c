// SPDX-License-Identifier: GPL-2.0-only
//
// Framework for Ethernet Power Sourcing Equipment
//
// Copyright (c) 2022 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
//

#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/pse-pd/pse.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/rtnetlink.h>
#include <net/net_trackers.h>

#define PSE_PW_D_LIMIT INT_MAX

static DEFINE_MUTEX(pse_list_mutex);
static LIST_HEAD(pse_controller_list);
static DEFINE_XARRAY_ALLOC(pse_pw_d_map);
static DEFINE_MUTEX(pse_pw_d_mutex);

/**
 * struct pse_control - a PSE control
 * @pcdev: a pointer to the PSE controller device
 *         this PSE control belongs to
 * @ps: PSE PI supply of the PSE control
 * @list: list entry for the pcdev's PSE controller list
 * @id: ID of the PSE line in the PSE controller device
 * @refcnt: Number of gets of this pse_control
 * @attached_phydev: PHY device pointer attached by the PSE control
 */
struct pse_control {
	struct pse_controller_dev *pcdev;
	struct regulator *ps;
	struct list_head list;
	unsigned int id;
	struct kref refcnt;
	struct phy_device *attached_phydev;
};

/**
 * struct pse_power_domain - a PSE power domain
 * @id: ID of the power domain
 * @supply: Power supply the Power Domain
 * @refcnt: Number of gets of this pse_power_domain
 * @budget_eval_strategy: Current power budget evaluation strategy of the
 *			  power domain
 */
struct pse_power_domain {
	int id;
	struct regulator *supply;
	struct kref refcnt;
	u32 budget_eval_strategy;
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

	for (i = 0; i < pcdev->nr_lines; i++) {
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

	pcdev->pi = kcalloc(pcdev->nr_lines, sizeof(*pcdev->pi), GFP_KERNEL);
	if (!pcdev->pi)
		return -ENOMEM;

	pis = of_get_child_by_name(np, "pse-pis");
	if (!pis) {
		/* no description of PSE PIs */
		pcdev->no_of_pse_pi = true;
		return 0;
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
 * pse_control_find_net_by_id - Find net attached to the pse control id
 * @pcdev: a pointer to the PSE
 * @id: index of the PSE control
 *
 * Return: pse_control pointer or NULL. The device returned has had a
 *	   reference added and the pointer is safe until the user calls
 *	   pse_control_put() to indicate they have finished with it.
 */
static struct pse_control *
pse_control_find_by_id(struct pse_controller_dev *pcdev, int id)
{
	struct pse_control *psec;

	mutex_lock(&pse_list_mutex);
	list_for_each_entry(psec, &pcdev->pse_control_head, list) {
		if (psec->id == id) {
			kref_get(&psec->refcnt);
			mutex_unlock(&pse_list_mutex);
			return psec;
		}
	}
	mutex_unlock(&pse_list_mutex);
	return NULL;
}

/**
 * pse_control_get_netdev - Return netdev associated to a PSE control
 * @psec: PSE control pointer
 *
 * Return: netdev pointer or NULL
 */
static struct net_device *pse_control_get_netdev(struct pse_control *psec)
{
	ASSERT_RTNL();

	if (!psec || !psec->attached_phydev)
		return NULL;

	return psec->attached_phydev->attached_dev;
}

/**
 * pse_pi_is_hw_enabled - Is PI enabled at the hardware level
 * @pcdev: a pointer to the PSE controller device
 * @id: Index of the PI
 *
 * Return: 1 if the PI is enabled at the hardware level, 0 if not, and
 *	   a failure value on error
 */
static int pse_pi_is_hw_enabled(struct pse_controller_dev *pcdev, int id)
{
	struct pse_admin_state admin_state = {0};
	int ret;

	ret = pcdev->ops->pi_get_admin_state(pcdev, id, &admin_state);
	if (ret < 0)
		return ret;

	/* PI is well enabled at the hardware level */
	if (admin_state.podl_admin_state == ETHTOOL_PODL_PSE_ADMIN_STATE_ENABLED ||
	    admin_state.c33_admin_state == ETHTOOL_C33_PSE_ADMIN_STATE_ENABLED)
		return 1;

	return 0;
}

/**
 * pse_pi_is_admin_enable_pending - Check if PI is in admin enable pending state
 *				    which mean the power is not yet being
 *				    delivered
 * @pcdev: a pointer to the PSE controller device
 * @id: Index of the PI
 *
 * Detects if a PI is enabled in software with a PD detected, but the hardware
 * admin state hasn't been applied yet.
 *
 * This function is used in the power delivery and retry mechanisms to determine
 * which PIs need to have power delivery attempted again.
 *
 * Return: true if the PI has admin enable flag set in software but not yet
 *	   reflected in the hardware admin state, false otherwise.
 */
static bool
pse_pi_is_admin_enable_pending(struct pse_controller_dev *pcdev, int id)
{
	int ret;

	/* PI not enabled or nothing is plugged */
	if (!pcdev->pi[id].admin_state_enabled ||
	    !pcdev->pi[id].isr_pd_detected)
		return false;

	ret = pse_pi_is_hw_enabled(pcdev, id);
	/* PSE PI is already enabled at hardware level */
	if (ret == 1)
		return false;

	return true;
}

static int _pse_pi_delivery_power_sw_pw_ctrl(struct pse_controller_dev *pcdev,
					     int id,
					     struct netlink_ext_ack *extack);

/**
 * pse_pw_d_retry_power_delivery - Retry power delivery for pending ports in a
 *				   PSE power domain
 * @pcdev: a pointer to the PSE controller device
 * @pw_d: a pointer to the PSE power domain
 *
 * Scans all ports in the specified power domain and attempts to enable power
 * delivery to any ports that have admin enable state set but don't yet have
 * hardware power enabled. Used when there are changes in connection status,
 * admin state, or priority that might allow previously unpowered ports to
 * receive power, especially in over-budget conditions.
 */
static void pse_pw_d_retry_power_delivery(struct pse_controller_dev *pcdev,
					  struct pse_power_domain *pw_d)
{
	int i, ret = 0;

	for (i = 0; i < pcdev->nr_lines; i++) {
		int prio_max = pcdev->nr_lines;
		struct netlink_ext_ack extack;

		if (pcdev->pi[i].pw_d != pw_d)
			continue;

		if (!pse_pi_is_admin_enable_pending(pcdev, i))
			continue;

		/* Do not try to enable PI with a lower prio (higher value)
		 * than one which already can't be enabled.
		 */
		if (pcdev->pi[i].prio > prio_max)
			continue;

		ret = _pse_pi_delivery_power_sw_pw_ctrl(pcdev, i, &extack);
		if (ret == -ERANGE)
			prio_max = pcdev->pi[i].prio;
	}
}

/**
 * pse_pw_d_is_sw_pw_control - Determine if power control is software managed
 * @pcdev: a pointer to the PSE controller device
 * @pw_d: a pointer to the PSE power domain
 *
 * This function determines whether the power control for a specific power
 * domain is managed by software in the interrupt handler rather than directly
 * by hardware.
 *
 * Software power control is active in the following cases:
 * - When the budget evaluation strategy is set to static
 * - When the budget evaluation strategy is disabled but the PSE controller
 *   has an interrupt handler that can report if a Powered Device is connected
 *
 * Return: true if the power control of the power domain is managed by software,
 *         false otherwise
 */
static bool pse_pw_d_is_sw_pw_control(struct pse_controller_dev *pcdev,
				      struct pse_power_domain *pw_d)
{
	if (!pw_d)
		return false;

	if (pw_d->budget_eval_strategy == PSE_BUDGET_EVAL_STRAT_STATIC)
		return true;
	if (pw_d->budget_eval_strategy == PSE_BUDGET_EVAL_STRAT_DISABLED &&
	    pcdev->ops->pi_enable && pcdev->irq)
		return true;

	return false;
}

static int pse_pi_is_enabled(struct regulator_dev *rdev)
{
	struct pse_controller_dev *pcdev = rdev_get_drvdata(rdev);
	const struct pse_controller_ops *ops;
	int id, ret;

	ops = pcdev->ops;
	if (!ops->pi_get_admin_state)
		return -EOPNOTSUPP;

	id = rdev_get_id(rdev);
	mutex_lock(&pcdev->lock);
	if (pse_pw_d_is_sw_pw_control(pcdev, pcdev->pi[id].pw_d)) {
		ret = pcdev->pi[id].admin_state_enabled;
		goto out;
	}

	ret = pse_pi_is_hw_enabled(pcdev, id);

out:
	mutex_unlock(&pcdev->lock);

	return ret;
}

/**
 * pse_pi_deallocate_pw_budget - Deallocate power budget of the PI
 * @pi: a pointer to the PSE PI
 */
static void pse_pi_deallocate_pw_budget(struct pse_pi *pi)
{
	if (!pi->pw_d || !pi->pw_allocated_mW)
		return;

	regulator_free_power_budget(pi->pw_d->supply, pi->pw_allocated_mW);
	pi->pw_allocated_mW = 0;
}

/**
 * _pse_pi_disable - Call disable operation. Assumes the PSE lock has been
 *		     acquired.
 * @pcdev: a pointer to the PSE
 * @id: index of the PSE control
 *
 * Return: 0 on success and failure value on error
 */
static int _pse_pi_disable(struct pse_controller_dev *pcdev, int id)
{
	const struct pse_controller_ops *ops = pcdev->ops;
	int ret;

	if (!ops->pi_disable)
		return -EOPNOTSUPP;

	ret = ops->pi_disable(pcdev, id);
	if (ret)
		return ret;

	pse_pi_deallocate_pw_budget(&pcdev->pi[id]);

	if (pse_pw_d_is_sw_pw_control(pcdev, pcdev->pi[id].pw_d))
		pse_pw_d_retry_power_delivery(pcdev, pcdev->pi[id].pw_d);

	return 0;
}

/**
 * pse_disable_pi_pol - Disable a PI on a power budget policy
 * @pcdev: a pointer to the PSE
 * @id: index of the PSE PI
 *
 * Return: 0 on success and failure value on error
 */
static int pse_disable_pi_pol(struct pse_controller_dev *pcdev, int id)
{
	unsigned long notifs = ETHTOOL_PSE_EVENT_OVER_BUDGET;
	struct pse_ntf ntf = {};
	int ret;

	dev_dbg(pcdev->dev, "Disabling PI %d to free power budget\n", id);

	ret = _pse_pi_disable(pcdev, id);
	if (ret)
		notifs |= ETHTOOL_PSE_EVENT_SW_PW_CONTROL_ERROR;

	ntf.notifs = notifs;
	ntf.id = id;
	kfifo_in_spinlocked(&pcdev->ntf_fifo, &ntf, 1, &pcdev->ntf_fifo_lock);
	schedule_work(&pcdev->ntf_work);

	return ret;
}

/**
 * pse_disable_pi_prio - Disable all PIs of a given priority inside a PSE
 *			 power domain
 * @pcdev: a pointer to the PSE
 * @pw_d: a pointer to the PSE power domain
 * @prio: priority
 *
 * Return: 0 on success and failure value on error
 */
static int pse_disable_pi_prio(struct pse_controller_dev *pcdev,
			       struct pse_power_domain *pw_d,
			       int prio)
{
	int i;

	for (i = 0; i < pcdev->nr_lines; i++) {
		int ret;

		if (pcdev->pi[i].prio != prio ||
		    pcdev->pi[i].pw_d != pw_d ||
		    pse_pi_is_hw_enabled(pcdev, i) <= 0)
			continue;

		ret = pse_disable_pi_pol(pcdev, i);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * pse_pi_allocate_pw_budget_static_prio - Allocate power budget for the PI
 *					   when the budget eval strategy is
 *					   static
 * @pcdev: a pointer to the PSE
 * @id: index of the PSE control
 * @pw_req: power requested in mW
 * @extack: extack for error reporting
 *
 * Allocates power using static budget evaluation strategy, where allocation
 * is based on PD classification. When insufficient budget is available,
 * lower-priority ports (higher priority numbers) are turned off first.
 *
 * Return: 0 on success and failure value on error
 */
static int
pse_pi_allocate_pw_budget_static_prio(struct pse_controller_dev *pcdev, int id,
				      int pw_req, struct netlink_ext_ack *extack)
{
	struct pse_pi *pi = &pcdev->pi[id];
	int ret, _prio;

	_prio = pcdev->nr_lines;
	while (regulator_request_power_budget(pi->pw_d->supply, pw_req) == -ERANGE) {
		if (_prio <= pi->prio) {
			NL_SET_ERR_MSG_FMT(extack,
					   "PI %d: not enough power budget available",
					   id);
			return -ERANGE;
		}

		ret = pse_disable_pi_prio(pcdev, pi->pw_d, _prio);
		if (ret < 0)
			return ret;

		_prio--;
	}

	pi->pw_allocated_mW = pw_req;
	return 0;
}

/**
 * pse_pi_allocate_pw_budget - Allocate power budget for the PI
 * @pcdev: a pointer to the PSE
 * @id: index of the PSE control
 * @pw_req: power requested in mW
 * @extack: extack for error reporting
 *
 * Return: 0 on success and failure value on error
 */
static int pse_pi_allocate_pw_budget(struct pse_controller_dev *pcdev, int id,
				     int pw_req, struct netlink_ext_ack *extack)
{
	struct pse_pi *pi = &pcdev->pi[id];

	if (!pi->pw_d)
		return 0;

	/* PSE_BUDGET_EVAL_STRAT_STATIC */
	if (pi->pw_d->budget_eval_strategy == PSE_BUDGET_EVAL_STRAT_STATIC)
		return pse_pi_allocate_pw_budget_static_prio(pcdev, id, pw_req,
							     extack);

	return 0;
}

/**
 * _pse_pi_delivery_power_sw_pw_ctrl - Enable PSE PI in case of software power
 *				       control. Assumes the PSE lock has been
 *				       acquired.
 * @pcdev: a pointer to the PSE
 * @id: index of the PSE control
 * @extack: extack for error reporting
 *
 * Return: 0 on success and failure value on error
 */
static int _pse_pi_delivery_power_sw_pw_ctrl(struct pse_controller_dev *pcdev,
					     int id,
					     struct netlink_ext_ack *extack)
{
	const struct pse_controller_ops *ops = pcdev->ops;
	struct pse_pi *pi = &pcdev->pi[id];
	int ret, pw_req;

	if (!ops->pi_get_pw_req) {
		/* No power allocation management */
		ret = ops->pi_enable(pcdev, id);
		if (ret)
			NL_SET_ERR_MSG_FMT(extack,
					   "PI %d: enable error %d",
					   id, ret);
		return ret;
	}

	ret = ops->pi_get_pw_req(pcdev, id);
	if (ret < 0)
		return ret;

	pw_req = ret;

	/* Compare requested power with port power limit and use the lowest
	 * one.
	 */
	if (ops->pi_get_pw_limit) {
		ret = ops->pi_get_pw_limit(pcdev, id);
		if (ret < 0)
			return ret;

		if (ret < pw_req)
			pw_req = ret;
	}

	ret = pse_pi_allocate_pw_budget(pcdev, id, pw_req, extack);
	if (ret)
		return ret;

	ret = ops->pi_enable(pcdev, id);
	if (ret) {
		pse_pi_deallocate_pw_budget(pi);
		NL_SET_ERR_MSG_FMT(extack,
				   "PI %d: enable error %d",
				   id, ret);
		return ret;
	}

	return 0;
}

static int pse_pi_enable(struct regulator_dev *rdev)
{
	struct pse_controller_dev *pcdev = rdev_get_drvdata(rdev);
	const struct pse_controller_ops *ops;
	int id, ret = 0;

	ops = pcdev->ops;
	if (!ops->pi_enable)
		return -EOPNOTSUPP;

	id = rdev_get_id(rdev);
	mutex_lock(&pcdev->lock);
	if (pse_pw_d_is_sw_pw_control(pcdev, pcdev->pi[id].pw_d)) {
		/* Manage enabled status by software.
		 * Real enable process will happen if a port is connected.
		 */
		if (pcdev->pi[id].isr_pd_detected) {
			struct netlink_ext_ack extack;

			ret = _pse_pi_delivery_power_sw_pw_ctrl(pcdev, id, &extack);
		}
		if (!ret || ret == -ERANGE) {
			pcdev->pi[id].admin_state_enabled = 1;
			ret = 0;
		}
		mutex_unlock(&pcdev->lock);
		return ret;
	}

	ret = ops->pi_enable(pcdev, id);
	if (!ret)
		pcdev->pi[id].admin_state_enabled = 1;
	mutex_unlock(&pcdev->lock);

	return ret;
}

static int pse_pi_disable(struct regulator_dev *rdev)
{
	struct pse_controller_dev *pcdev = rdev_get_drvdata(rdev);
	struct pse_pi *pi;
	int id, ret;

	id = rdev_get_id(rdev);
	pi = &pcdev->pi[id];
	mutex_lock(&pcdev->lock);
	ret = _pse_pi_disable(pcdev, id);
	if (!ret)
		pi->admin_state_enabled = 0;

	mutex_unlock(&pcdev->lock);
	return 0;
}

static int _pse_pi_get_voltage(struct regulator_dev *rdev)
{
	struct pse_controller_dev *pcdev = rdev_get_drvdata(rdev);
	const struct pse_controller_ops *ops;
	int id;

	ops = pcdev->ops;
	if (!ops->pi_get_voltage)
		return -EOPNOTSUPP;

	id = rdev_get_id(rdev);
	return ops->pi_get_voltage(pcdev, id);
}

static int pse_pi_get_voltage(struct regulator_dev *rdev)
{
	struct pse_controller_dev *pcdev = rdev_get_drvdata(rdev);
	int ret;

	mutex_lock(&pcdev->lock);
	ret = _pse_pi_get_voltage(rdev);
	mutex_unlock(&pcdev->lock);

	return ret;
}

static int pse_pi_get_current_limit(struct regulator_dev *rdev)
{
	struct pse_controller_dev *pcdev = rdev_get_drvdata(rdev);
	const struct pse_controller_ops *ops;
	int id, uV, mW, ret;
	s64 tmp_64;

	ops = pcdev->ops;
	id = rdev_get_id(rdev);
	if (!ops->pi_get_pw_limit || !ops->pi_get_voltage)
		return -EOPNOTSUPP;

	mutex_lock(&pcdev->lock);
	ret = ops->pi_get_pw_limit(pcdev, id);
	if (ret < 0)
		goto out;
	mW = ret;

	ret = _pse_pi_get_voltage(rdev);
	if (!ret) {
		dev_err(pcdev->dev, "Voltage null\n");
		ret = -ERANGE;
		goto out;
	}
	if (ret < 0)
		goto out;
	uV = ret;

	tmp_64 = mW;
	tmp_64 *= 1000000000ull;
	/* uA = mW * 1000000000 / uV */
	ret = DIV_ROUND_CLOSEST_ULL(tmp_64, uV);

out:
	mutex_unlock(&pcdev->lock);
	return ret;
}

static int pse_pi_set_current_limit(struct regulator_dev *rdev, int min_uA,
				    int max_uA)
{
	struct pse_controller_dev *pcdev = rdev_get_drvdata(rdev);
	const struct pse_controller_ops *ops;
	int id, mW, ret;
	s64 tmp_64;

	ops = pcdev->ops;
	if (!ops->pi_set_pw_limit || !ops->pi_get_voltage)
		return -EOPNOTSUPP;

	if (max_uA > MAX_PI_CURRENT)
		return -ERANGE;

	id = rdev_get_id(rdev);
	mutex_lock(&pcdev->lock);
	ret = _pse_pi_get_voltage(rdev);
	if (!ret) {
		dev_err(pcdev->dev, "Voltage null\n");
		ret = -ERANGE;
		goto out;
	}
	if (ret < 0)
		goto out;

	tmp_64 = ret;
	tmp_64 *= max_uA;
	/* mW = uA * uV / 1000000000 */
	mW = DIV_ROUND_CLOSEST_ULL(tmp_64, 1000000000);
	ret = ops->pi_set_pw_limit(pcdev, id, mW);
out:
	mutex_unlock(&pcdev->lock);

	return ret;
}

static const struct regulator_ops pse_pi_ops = {
	.is_enabled = pse_pi_is_enabled,
	.enable = pse_pi_enable,
	.disable = pse_pi_disable,
	.get_voltage = pse_pi_get_voltage,
	.get_current_limit = pse_pi_get_current_limit,
	.set_current_limit = pse_pi_set_current_limit,
};

static int
devm_pse_pi_regulator_register(struct pse_controller_dev *pcdev,
			       char *name, int id)
{
	struct regulator_init_data *rinit_data;
	struct regulator_config rconfig = {0};
	struct regulator_desc *rdesc;
	struct regulator_dev *rdev;

	rinit_data = devm_kzalloc(pcdev->dev, sizeof(*rinit_data),
				  GFP_KERNEL);
	if (!rinit_data)
		return -ENOMEM;

	rdesc = devm_kzalloc(pcdev->dev, sizeof(*rdesc), GFP_KERNEL);
	if (!rdesc)
		return -ENOMEM;

	/* Regulator descriptor id have to be the same as its associated
	 * PSE PI id for the well functioning of the PSE controls.
	 */
	rdesc->id = id;
	rdesc->name = name;
	rdesc->type = REGULATOR_VOLTAGE;
	rdesc->ops = &pse_pi_ops;
	rdesc->owner = pcdev->owner;

	rinit_data->constraints.valid_ops_mask = REGULATOR_CHANGE_STATUS;

	if (pcdev->ops->pi_set_pw_limit)
		rinit_data->constraints.valid_ops_mask |=
			REGULATOR_CHANGE_CURRENT;

	rinit_data->supply_regulator = "vpwr";

	rconfig.dev = pcdev->dev;
	rconfig.driver_data = pcdev;
	rconfig.init_data = rinit_data;
	rconfig.of_node = pcdev->pi[id].np;

	rdev = devm_regulator_register(pcdev->dev, rdesc, &rconfig);
	if (IS_ERR(rdev)) {
		dev_err_probe(pcdev->dev, PTR_ERR(rdev),
			      "Failed to register regulator\n");
		return PTR_ERR(rdev);
	}

	pcdev->pi[id].rdev = rdev;

	return 0;
}

static void __pse_pw_d_release(struct kref *kref)
{
	struct pse_power_domain *pw_d = container_of(kref,
						     struct pse_power_domain,
						     refcnt);

	regulator_put(pw_d->supply);
	xa_erase(&pse_pw_d_map, pw_d->id);
	mutex_unlock(&pse_pw_d_mutex);
}

/**
 * pse_flush_pw_ds - flush all PSE power domains of a PSE
 * @pcdev: a pointer to the initialized PSE controller device
 */
static void pse_flush_pw_ds(struct pse_controller_dev *pcdev)
{
	struct pse_power_domain *pw_d;
	int i;

	for (i = 0; i < pcdev->nr_lines; i++) {
		if (!pcdev->pi[i].pw_d)
			continue;

		pw_d = xa_load(&pse_pw_d_map, pcdev->pi[i].pw_d->id);
		if (!pw_d)
			continue;

		kref_put_mutex(&pw_d->refcnt, __pse_pw_d_release,
			       &pse_pw_d_mutex);
	}
}

/**
 * devm_pse_alloc_pw_d - allocate a new PSE power domain for a device
 * @dev: device that is registering this PSE power domain
 *
 * Return: Pointer to the newly allocated PSE power domain or error pointers
 */
static struct pse_power_domain *devm_pse_alloc_pw_d(struct device *dev)
{
	struct pse_power_domain *pw_d;
	int index, ret;

	pw_d = devm_kzalloc(dev, sizeof(*pw_d), GFP_KERNEL);
	if (!pw_d)
		return ERR_PTR(-ENOMEM);

	ret = xa_alloc(&pse_pw_d_map, &index, pw_d, XA_LIMIT(1, PSE_PW_D_LIMIT),
		       GFP_KERNEL);
	if (ret)
		return ERR_PTR(ret);

	kref_init(&pw_d->refcnt);
	pw_d->id = index;
	return pw_d;
}

/**
 * pse_register_pw_ds - register the PSE power domains for a PSE
 * @pcdev: a pointer to the PSE controller device
 *
 * Return: 0 on success and failure value on error
 */
static int pse_register_pw_ds(struct pse_controller_dev *pcdev)
{
	int i, ret = 0;

	mutex_lock(&pse_pw_d_mutex);
	for (i = 0; i < pcdev->nr_lines; i++) {
		struct regulator_dev *rdev = pcdev->pi[i].rdev;
		struct pse_power_domain *pw_d;
		struct regulator *supply;
		bool present = false;
		unsigned long index;

		/* No regulator or regulator parent supply registered.
		 * We need a regulator parent to register a PSE power domain
		 */
		if (!rdev || !rdev->supply)
			continue;

		xa_for_each(&pse_pw_d_map, index, pw_d) {
			/* Power supply already registered as a PSE power
			 * domain.
			 */
			if (regulator_is_equal(pw_d->supply, rdev->supply)) {
				present = true;
				pcdev->pi[i].pw_d = pw_d;
				break;
			}
		}
		if (present) {
			kref_get(&pw_d->refcnt);
			continue;
		}

		pw_d = devm_pse_alloc_pw_d(pcdev->dev);
		if (IS_ERR(pw_d)) {
			ret = PTR_ERR(pw_d);
			goto out;
		}

		supply = regulator_get(&rdev->dev, rdev->supply_name);
		if (IS_ERR(supply)) {
			xa_erase(&pse_pw_d_map, pw_d->id);
			ret = PTR_ERR(supply);
			goto out;
		}

		pw_d->supply = supply;
		if (pcdev->supp_budget_eval_strategies)
			pw_d->budget_eval_strategy = pcdev->supp_budget_eval_strategies;
		else
			pw_d->budget_eval_strategy = PSE_BUDGET_EVAL_STRAT_DISABLED;
		kref_init(&pw_d->refcnt);
		pcdev->pi[i].pw_d = pw_d;
	}

out:
	mutex_unlock(&pse_pw_d_mutex);
	return ret;
}

/**
 * pse_send_ntf_worker - Worker to send PSE notifications
 * @work: work object
 *
 * Manage and send PSE netlink notifications using a workqueue to avoid
 * deadlock between pcdev_lock and pse_list_mutex.
 */
static void pse_send_ntf_worker(struct work_struct *work)
{
	struct pse_controller_dev *pcdev;
	struct pse_ntf ntf;

	pcdev = container_of(work, struct pse_controller_dev, ntf_work);

	while (kfifo_out(&pcdev->ntf_fifo, &ntf, 1)) {
		struct net_device *netdev;
		struct pse_control *psec;

		psec = pse_control_find_by_id(pcdev, ntf.id);
		rtnl_lock();
		netdev = pse_control_get_netdev(psec);
		if (netdev)
			ethnl_pse_send_ntf(netdev, ntf.notifs);
		rtnl_unlock();
		pse_control_put(psec);
	}
}

/**
 * pse_controller_register - register a PSE controller device
 * @pcdev: a pointer to the initialized PSE controller device
 *
 * Return: 0 on success and failure value on error
 */
int pse_controller_register(struct pse_controller_dev *pcdev)
{
	size_t reg_name_len;
	int ret, i;

	mutex_init(&pcdev->lock);
	INIT_LIST_HEAD(&pcdev->pse_control_head);
	spin_lock_init(&pcdev->ntf_fifo_lock);
	ret = kfifo_alloc(&pcdev->ntf_fifo, pcdev->nr_lines, GFP_KERNEL);
	if (ret) {
		dev_err(pcdev->dev, "failed to allocate kfifo notifications\n");
		return ret;
	}
	INIT_WORK(&pcdev->ntf_work, pse_send_ntf_worker);

	if (!pcdev->nr_lines)
		pcdev->nr_lines = 1;

	if (!pcdev->ops->pi_get_admin_state ||
	    !pcdev->ops->pi_get_pw_status) {
		dev_err(pcdev->dev,
			"Mandatory status report callbacks are missing");
		return -EINVAL;
	}

	ret = of_load_pse_pis(pcdev);
	if (ret)
		return ret;

	if (pcdev->ops->setup_pi_matrix) {
		ret = pcdev->ops->setup_pi_matrix(pcdev);
		if (ret)
			return ret;
	}

	/* Each regulator name len is pcdev dev name + 7 char +
	 * int max digit number (10) + 1
	 */
	reg_name_len = strlen(dev_name(pcdev->dev)) + 18;

	/* Register PI regulators */
	for (i = 0; i < pcdev->nr_lines; i++) {
		char *reg_name;

		/* Do not register regulator for PIs not described */
		if (!pcdev->no_of_pse_pi && !pcdev->pi[i].np)
			continue;

		reg_name = devm_kzalloc(pcdev->dev, reg_name_len, GFP_KERNEL);
		if (!reg_name)
			return -ENOMEM;

		snprintf(reg_name, reg_name_len, "pse-%s_pi%d",
			 dev_name(pcdev->dev), i);

		ret = devm_pse_pi_regulator_register(pcdev, reg_name, i);
		if (ret)
			return ret;
	}

	ret = pse_register_pw_ds(pcdev);
	if (ret)
		return ret;

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
	pse_flush_pw_ds(pcdev);
	pse_release_pis(pcdev);
	if (pcdev->irq)
		disable_irq(pcdev->irq);
	cancel_work_sync(&pcdev->ntf_work);
	kfifo_free(&pcdev->ntf_fifo);
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
 *
 * Return: 0 on success and failure value on error
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

struct pse_irq {
	struct pse_controller_dev *pcdev;
	struct pse_irq_desc desc;
	unsigned long *notifs;
};

/**
 * pse_to_regulator_notifs - Convert PSE notifications to Regulator
 *			     notifications
 * @notifs: PSE notifications
 *
 * Return: Regulator notifications
 */
static unsigned long pse_to_regulator_notifs(unsigned long notifs)
{
	unsigned long rnotifs = 0;

	if (notifs & ETHTOOL_PSE_EVENT_OVER_CURRENT)
		rnotifs |= REGULATOR_EVENT_OVER_CURRENT;
	if (notifs & ETHTOOL_PSE_EVENT_OVER_TEMP)
		rnotifs |= REGULATOR_EVENT_OVER_TEMP;

	return rnotifs;
}

/**
 * pse_set_config_isr - Set PSE control config according to the PSE
 *			notifications
 * @pcdev: a pointer to the PSE
 * @id: index of the PSE control
 * @notifs: PSE event notifications
 *
 * Return: 0 on success and failure value on error
 */
static int pse_set_config_isr(struct pse_controller_dev *pcdev, int id,
			      unsigned long notifs)
{
	int ret = 0;

	if (notifs & PSE_BUDGET_EVAL_STRAT_DYNAMIC)
		return 0;

	if ((notifs & ETHTOOL_C33_PSE_EVENT_DISCONNECTION) &&
	    ((notifs & ETHTOOL_C33_PSE_EVENT_DETECTION) ||
	     (notifs & ETHTOOL_C33_PSE_EVENT_CLASSIFICATION))) {
		dev_dbg(pcdev->dev,
			"PI %d: error, connection and disconnection reported simultaneously",
			id);
		return -EINVAL;
	}

	if (notifs & ETHTOOL_C33_PSE_EVENT_CLASSIFICATION) {
		struct netlink_ext_ack extack;

		pcdev->pi[id].isr_pd_detected = true;
		if (pcdev->pi[id].admin_state_enabled) {
			ret = _pse_pi_delivery_power_sw_pw_ctrl(pcdev, id,
								&extack);
			if (ret == -ERANGE)
				ret = 0;
		}
	} else if (notifs & ETHTOOL_C33_PSE_EVENT_DISCONNECTION) {
		if (pcdev->pi[id].admin_state_enabled &&
		    pcdev->pi[id].isr_pd_detected)
			ret = _pse_pi_disable(pcdev, id);
		pcdev->pi[id].isr_pd_detected = false;
	}

	return ret;
}

/**
 * pse_isr - IRQ handler for PSE
 * @irq: irq number
 * @data: pointer to user interrupt structure
 *
 * Return: irqreturn_t - status of IRQ
 */
static irqreturn_t pse_isr(int irq, void *data)
{
	struct pse_controller_dev *pcdev;
	unsigned long notifs_mask = 0;
	struct pse_irq_desc *desc;
	struct pse_irq *h = data;
	int ret, i;

	desc = &h->desc;
	pcdev = h->pcdev;

	/* Clear notifs mask */
	memset(h->notifs, 0, pcdev->nr_lines * sizeof(*h->notifs));
	mutex_lock(&pcdev->lock);
	ret = desc->map_event(irq, pcdev, h->notifs, &notifs_mask);
	if (ret || !notifs_mask) {
		mutex_unlock(&pcdev->lock);
		return IRQ_NONE;
	}

	for_each_set_bit(i, &notifs_mask, pcdev->nr_lines) {
		unsigned long notifs, rnotifs;
		struct pse_ntf ntf = {};

		/* Do nothing PI not described */
		if (!pcdev->pi[i].rdev)
			continue;

		notifs = h->notifs[i];
		if (pse_pw_d_is_sw_pw_control(pcdev, pcdev->pi[i].pw_d)) {
			ret = pse_set_config_isr(pcdev, i, notifs);
			if (ret)
				notifs |= ETHTOOL_PSE_EVENT_SW_PW_CONTROL_ERROR;
		}

		dev_dbg(h->pcdev->dev,
			"Sending PSE notification EVT 0x%lx\n", notifs);

		ntf.notifs = notifs;
		ntf.id = i;
		kfifo_in_spinlocked(&pcdev->ntf_fifo, &ntf, 1,
				    &pcdev->ntf_fifo_lock);
		schedule_work(&pcdev->ntf_work);

		rnotifs = pse_to_regulator_notifs(notifs);
		regulator_notifier_call_chain(pcdev->pi[i].rdev, rnotifs,
					      NULL);
	}

	mutex_unlock(&pcdev->lock);

	return IRQ_HANDLED;
}

/**
 * devm_pse_irq_helper - Register IRQ based PSE event notifier
 * @pcdev: a pointer to the PSE
 * @irq: the irq value to be passed to request_irq
 * @irq_flags: the flags to be passed to request_irq
 * @d: PSE interrupt description
 *
 * Return: 0 on success and errno on failure
 */
int devm_pse_irq_helper(struct pse_controller_dev *pcdev, int irq,
			int irq_flags, const struct pse_irq_desc *d)
{
	struct device *dev = pcdev->dev;
	size_t irq_name_len;
	struct pse_irq *h;
	char *irq_name;
	int ret;

	if (!d || !d->map_event || !d->name)
		return -EINVAL;

	h = devm_kzalloc(dev, sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;

	h->pcdev = pcdev;
	h->desc = *d;

	/* IRQ name len is pcdev dev name + 5 char + irq desc name + 1 */
	irq_name_len = strlen(dev_name(pcdev->dev)) + 5 + strlen(d->name) + 1;
	irq_name = devm_kzalloc(dev, irq_name_len, GFP_KERNEL);
	if (!irq_name)
		return -ENOMEM;

	snprintf(irq_name, irq_name_len, "pse-%s:%s", dev_name(pcdev->dev),
		 d->name);

	h->notifs = devm_kcalloc(dev, pcdev->nr_lines,
				 sizeof(*h->notifs), GFP_KERNEL);
	if (!h->notifs)
		return -ENOMEM;

	ret = devm_request_threaded_irq(dev, irq, NULL, pse_isr,
					IRQF_ONESHOT | irq_flags,
					irq_name, h);
	if (ret)
		dev_err(pcdev->dev, "Failed to request IRQ %d\n", irq);

	pcdev->irq = irq;
	return ret;
}
EXPORT_SYMBOL_GPL(devm_pse_irq_helper);

/* PSE control section */

static void __pse_control_release(struct kref *kref)
{
	struct pse_control *psec = container_of(kref, struct pse_control,
						  refcnt);

	lockdep_assert_held(&pse_list_mutex);

	if (psec->pcdev->pi[psec->id].admin_state_enabled)
		regulator_disable(psec->ps);
	devm_regulator_put(psec->ps);

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
pse_control_get_internal(struct pse_controller_dev *pcdev, unsigned int index,
			 struct phy_device *phydev)
{
	struct pse_control *psec;
	int ret;

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
		ret = -ENODEV;
		goto free_psec;
	}

	if (!pcdev->ops->pi_get_admin_state) {
		ret = -EOPNOTSUPP;
		goto free_psec;
	}

	/* Initialize admin_state_enabled before the regulator_get. This
	 * aims to have the right value reported in the first is_enabled
	 * call in case of control managed by software.
	 */
	ret = pse_pi_is_hw_enabled(pcdev, index);
	if (ret < 0)
		goto free_psec;

	pcdev->pi[index].admin_state_enabled = ret;
	psec->ps = devm_regulator_get_exclusive(pcdev->dev,
						rdev_get_name(pcdev->pi[index].rdev));
	if (IS_ERR(psec->ps)) {
		ret = PTR_ERR(psec->ps);
		goto put_module;
	}

	psec->pcdev = pcdev;
	list_add(&psec->list, &pcdev->pse_control_head);
	psec->id = index;
	psec->attached_phydev = phydev;
	kref_init(&psec->refcnt);

	return psec;

put_module:
	module_put(pcdev->owner);
free_psec:
	kfree(psec);

	return ERR_PTR(ret);
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

	for (i = 0; i < pcdev->nr_lines; i++) {
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

struct pse_control *of_pse_control_get(struct device_node *node,
				       struct phy_device *phydev)
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
	psec = pse_control_get_internal(pcdev, psec_id, phydev);

out:
	mutex_unlock(&pse_list_mutex);
	of_node_put(args.np);

	return psec;
}
EXPORT_SYMBOL_GPL(of_pse_control_get);

/**
 * pse_get_sw_admin_state - Convert the software admin state to c33 or podl
 *			    admin state value used in the standard
 * @psec: PSE control pointer
 * @admin_state: a pointer to the admin_state structure
 */
static void pse_get_sw_admin_state(struct pse_control *psec,
				   struct pse_admin_state *admin_state)
{
	struct pse_pi *pi = &psec->pcdev->pi[psec->id];

	if (pse_has_podl(psec)) {
		if (pi->admin_state_enabled)
			admin_state->podl_admin_state =
				ETHTOOL_PODL_PSE_ADMIN_STATE_ENABLED;
		else
			admin_state->podl_admin_state =
				ETHTOOL_PODL_PSE_ADMIN_STATE_DISABLED;
	}
	if (pse_has_c33(psec)) {
		if (pi->admin_state_enabled)
			admin_state->c33_admin_state =
				ETHTOOL_C33_PSE_ADMIN_STATE_ENABLED;
		else
			admin_state->c33_admin_state =
				ETHTOOL_C33_PSE_ADMIN_STATE_DISABLED;
	}
}

/**
 * pse_ethtool_get_status - get status of PSE control
 * @psec: PSE control pointer
 * @extack: extack for reporting useful error messages
 * @status: struct to store PSE status
 *
 * Return: 0 on success and failure value on error
 */
int pse_ethtool_get_status(struct pse_control *psec,
			   struct netlink_ext_ack *extack,
			   struct ethtool_pse_control_status *status)
{
	struct pse_admin_state admin_state = {0};
	struct pse_pw_status pw_status = {0};
	const struct pse_controller_ops *ops;
	struct pse_controller_dev *pcdev;
	struct pse_pi *pi;
	int ret;

	pcdev = psec->pcdev;
	ops = pcdev->ops;

	pi = &pcdev->pi[psec->id];
	mutex_lock(&pcdev->lock);
	if (pi->pw_d) {
		status->pw_d_id = pi->pw_d->id;
		if (pse_pw_d_is_sw_pw_control(pcdev, pi->pw_d)) {
			pse_get_sw_admin_state(psec, &admin_state);
		} else {
			ret = ops->pi_get_admin_state(pcdev, psec->id,
						      &admin_state);
			if (ret)
				goto out;
		}
		status->podl_admin_state = admin_state.podl_admin_state;
		status->c33_admin_state = admin_state.c33_admin_state;

		switch (pi->pw_d->budget_eval_strategy) {
		case PSE_BUDGET_EVAL_STRAT_STATIC:
			status->prio_max = pcdev->nr_lines - 1;
			status->prio = pi->prio;
			break;
		case PSE_BUDGET_EVAL_STRAT_DYNAMIC:
			status->prio_max = pcdev->pis_prio_max;
			if (ops->pi_get_prio) {
				ret = ops->pi_get_prio(pcdev, psec->id);
				if (ret < 0)
					goto out;

				status->prio = ret;
			}
			break;
		default:
			break;
		}
	}

	ret = ops->pi_get_pw_status(pcdev, psec->id, &pw_status);
	if (ret)
		goto out;
	status->podl_pw_status = pw_status.podl_pw_status;
	status->c33_pw_status = pw_status.c33_pw_status;

	if (ops->pi_get_ext_state) {
		struct pse_ext_state_info ext_state_info = {0};

		ret = ops->pi_get_ext_state(pcdev, psec->id,
					    &ext_state_info);
		if (ret)
			goto out;

		memcpy(&status->c33_ext_state_info,
		       &ext_state_info.c33_ext_state_info,
		       sizeof(status->c33_ext_state_info));
	}

	if (ops->pi_get_pw_class) {
		ret = ops->pi_get_pw_class(pcdev, psec->id);
		if (ret < 0)
			goto out;

		status->c33_pw_class = ret;
	}

	if (ops->pi_get_actual_pw) {
		ret = ops->pi_get_actual_pw(pcdev, psec->id);
		if (ret < 0)
			goto out;

		status->c33_actual_pw = ret;
	}

	if (ops->pi_get_pw_limit) {
		ret = ops->pi_get_pw_limit(pcdev, psec->id);
		if (ret < 0)
			goto out;

		status->c33_avail_pw_limit = ret;
	}

	if (ops->pi_get_pw_limit_ranges) {
		struct pse_pw_limit_ranges pw_limit_ranges = {0};

		ret = ops->pi_get_pw_limit_ranges(pcdev, psec->id,
						  &pw_limit_ranges);
		if (ret < 0)
			goto out;

		status->c33_pw_limit_ranges =
			pw_limit_ranges.c33_pw_limit_ranges;
		status->c33_pw_limit_nb_ranges = ret;
	}
out:
	mutex_unlock(&psec->pcdev->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(pse_ethtool_get_status);

static int pse_ethtool_c33_set_config(struct pse_control *psec,
				      const struct pse_control_config *config)
{
	int err = 0;

	/* Look at admin_state_enabled status to not call regulator_enable
	 * or regulator_disable twice creating a regulator counter mismatch
	 */
	switch (config->c33_admin_control) {
	case ETHTOOL_C33_PSE_ADMIN_STATE_ENABLED:
		/* We could have mismatch between admin_state_enabled and
		 * state reported by regulator_is_enabled. This can occur when
		 * the PI is forcibly turn off by the controller. Call
		 * regulator_disable on that case to fix the counters state.
		 */
		if (psec->pcdev->pi[psec->id].admin_state_enabled &&
		    !regulator_is_enabled(psec->ps)) {
			err = regulator_disable(psec->ps);
			if (err)
				break;
		}
		if (!psec->pcdev->pi[psec->id].admin_state_enabled)
			err = regulator_enable(psec->ps);
		break;
	case ETHTOOL_C33_PSE_ADMIN_STATE_DISABLED:
		if (psec->pcdev->pi[psec->id].admin_state_enabled)
			err = regulator_disable(psec->ps);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	return err;
}

static int pse_ethtool_podl_set_config(struct pse_control *psec,
				       const struct pse_control_config *config)
{
	int err = 0;

	/* Look at admin_state_enabled status to not call regulator_enable
	 * or regulator_disable twice creating a regulator counter mismatch
	 */
	switch (config->podl_admin_control) {
	case ETHTOOL_PODL_PSE_ADMIN_STATE_ENABLED:
		if (!psec->pcdev->pi[psec->id].admin_state_enabled)
			err = regulator_enable(psec->ps);
		break;
	case ETHTOOL_PODL_PSE_ADMIN_STATE_DISABLED:
		if (psec->pcdev->pi[psec->id].admin_state_enabled)
			err = regulator_disable(psec->ps);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	return err;
}

/**
 * pse_ethtool_set_config - set PSE control configuration
 * @psec: PSE control pointer
 * @extack: extack for reporting useful error messages
 * @config: Configuration of the test to run
 *
 * Return: 0 on success and failure value on error
 */
int pse_ethtool_set_config(struct pse_control *psec,
			   struct netlink_ext_ack *extack,
			   const struct pse_control_config *config)
{
	int err = 0;

	if (pse_has_c33(psec) && config->c33_admin_control) {
		err = pse_ethtool_c33_set_config(psec, config);
		if (err)
			return err;
	}

	if (pse_has_podl(psec) && config->podl_admin_control)
		err = pse_ethtool_podl_set_config(psec, config);

	return err;
}
EXPORT_SYMBOL_GPL(pse_ethtool_set_config);

/**
 * pse_pi_update_pw_budget - Update PSE power budget allocated with new
 *			     power in mW
 * @pcdev: a pointer to the PSE controller device
 * @id: index of the PSE PI
 * @pw_req: power requested
 * @extack: extack for reporting useful error messages
 *
 * Return: Previous power allocated on success and failure value on error
 */
static int pse_pi_update_pw_budget(struct pse_controller_dev *pcdev, int id,
				   const unsigned int pw_req,
				   struct netlink_ext_ack *extack)
{
	struct pse_pi *pi = &pcdev->pi[id];
	int previous_pw_allocated;
	int pw_diff, ret = 0;

	/* We don't want pw_allocated_mW value change in the middle of an
	 * power budget update
	 */
	mutex_lock(&pcdev->lock);
	previous_pw_allocated = pi->pw_allocated_mW;
	pw_diff = pw_req - previous_pw_allocated;
	if (!pw_diff) {
		goto out;
	} else if (pw_diff > 0) {
		ret = regulator_request_power_budget(pi->pw_d->supply, pw_diff);
		if (ret) {
			NL_SET_ERR_MSG_FMT(extack,
					   "PI %d: not enough power budget available",
					   id);
			goto out;
		}

	} else {
		regulator_free_power_budget(pi->pw_d->supply, -pw_diff);
	}
	pi->pw_allocated_mW = pw_req;
	ret = previous_pw_allocated;

out:
	mutex_unlock(&pcdev->lock);
	return ret;
}

/**
 * pse_ethtool_set_pw_limit - set PSE control power limit
 * @psec: PSE control pointer
 * @extack: extack for reporting useful error messages
 * @pw_limit: power limit value in mW
 *
 * Return: 0 on success and failure value on error
 */
int pse_ethtool_set_pw_limit(struct pse_control *psec,
			     struct netlink_ext_ack *extack,
			     const unsigned int pw_limit)
{
	int uV, uA, ret, previous_pw_allocated = 0;
	s64 tmp_64;

	if (pw_limit > MAX_PI_PW)
		return -ERANGE;

	ret = regulator_get_voltage(psec->ps);
	if (!ret) {
		NL_SET_ERR_MSG(extack,
			       "Can't calculate the current, PSE voltage read is 0");
		return -ERANGE;
	}
	if (ret < 0) {
		NL_SET_ERR_MSG(extack,
			       "Error reading PSE voltage");
		return ret;
	}
	uV = ret;

	tmp_64 = pw_limit;
	tmp_64 *= 1000000000ull;
	/* uA = mW * 1000000000 / uV */
	uA = DIV_ROUND_CLOSEST_ULL(tmp_64, uV);

	/* Update power budget only in software power control case and
	 * if a Power Device is powered.
	 */
	if (pse_pw_d_is_sw_pw_control(psec->pcdev,
				      psec->pcdev->pi[psec->id].pw_d) &&
	    psec->pcdev->pi[psec->id].admin_state_enabled &&
	    psec->pcdev->pi[psec->id].isr_pd_detected) {
		ret = pse_pi_update_pw_budget(psec->pcdev, psec->id,
					      pw_limit, extack);
		if (ret < 0)
			return ret;
		previous_pw_allocated = ret;
	}

	ret = regulator_set_current_limit(psec->ps, 0, uA);
	if (ret < 0 && previous_pw_allocated) {
		pse_pi_update_pw_budget(psec->pcdev, psec->id,
					previous_pw_allocated, extack);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(pse_ethtool_set_pw_limit);

/**
 * pse_ethtool_set_prio - Set PSE PI priority according to the budget
 *			  evaluation strategy
 * @psec: PSE control pointer
 * @extack: extack for reporting useful error messages
 * @prio: priovity value
 *
 * Return: 0 on success and failure value on error
 */
int pse_ethtool_set_prio(struct pse_control *psec,
			 struct netlink_ext_ack *extack,
			 unsigned int prio)
{
	struct pse_controller_dev *pcdev = psec->pcdev;
	const struct pse_controller_ops *ops;
	int ret = 0;

	if (!pcdev->pi[psec->id].pw_d) {
		NL_SET_ERR_MSG(extack, "no power domain attached");
		return -EOPNOTSUPP;
	}

	/* We don't want priority change in the middle of an
	 * enable/disable call or a priority mode change
	 */
	mutex_lock(&pcdev->lock);
	switch (pcdev->pi[psec->id].pw_d->budget_eval_strategy) {
	case PSE_BUDGET_EVAL_STRAT_STATIC:
		if (prio >= pcdev->nr_lines) {
			NL_SET_ERR_MSG_FMT(extack,
					   "priority %d exceed priority max %d",
					   prio, pcdev->nr_lines);
			ret = -ERANGE;
			goto out;
		}

		pcdev->pi[psec->id].prio = prio;
		pse_pw_d_retry_power_delivery(pcdev, pcdev->pi[psec->id].pw_d);
		break;

	case PSE_BUDGET_EVAL_STRAT_DYNAMIC:
		ops = psec->pcdev->ops;
		if (!ops->pi_set_prio) {
			NL_SET_ERR_MSG(extack,
				       "pse driver does not support setting port priority");
			ret = -EOPNOTSUPP;
			goto out;
		}

		if (prio > pcdev->pis_prio_max) {
			NL_SET_ERR_MSG_FMT(extack,
					   "priority %d exceed priority max %d",
					   prio, pcdev->pis_prio_max);
			ret = -ERANGE;
			goto out;
		}

		ret = ops->pi_set_prio(pcdev, psec->id, prio);
		break;

	default:
		ret = -EOPNOTSUPP;
	}

out:
	mutex_unlock(&pcdev->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(pse_ethtool_set_prio);

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
