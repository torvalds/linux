// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 *	      http://www.samsung.com/
 * Author: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * Simplified generic voltage coupler from regulator core.c
 * The main difference is that it keeps current regulator voltage
 * if consumers didn't apply their constraints yet.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/regulator/coupler.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

static int regulator_get_optimal_voltage(struct regulator_dev *rdev,
					 int *current_uV,
					 int *min_uV, int *max_uV,
					 suspend_state_t state)
{
	struct coupling_desc *c_desc = &rdev->coupling_desc;
	struct regulator_dev **c_rdevs = c_desc->coupled_rdevs;
	struct regulation_constraints *constraints = rdev->constraints;
	int desired_min_uV = 0, desired_max_uV = INT_MAX;
	int max_current_uV = 0, min_current_uV = INT_MAX;
	int highest_min_uV = 0, target_uV, possible_uV;
	int i, ret, max_spread, n_coupled = c_desc->n_coupled;
	bool done;

	*current_uV = -1;

	/* Find highest min desired voltage */
	for (i = 0; i < n_coupled; i++) {
		int tmp_min = 0;
		int tmp_max = INT_MAX;

		lockdep_assert_held_once(&c_rdevs[i]->mutex.base);

		ret = regulator_check_consumers(c_rdevs[i],
						&tmp_min,
						&tmp_max, state);
		if (ret < 0)
			return ret;

		if (tmp_min == 0) {
			ret = regulator_get_voltage_rdev(c_rdevs[i]);
			if (ret < 0)
				return ret;
			tmp_min = ret;
		}

		/* apply constraints */
		ret = regulator_check_voltage(c_rdevs[i], &tmp_min, &tmp_max);
		if (ret < 0)
			return ret;

		highest_min_uV = max(highest_min_uV, tmp_min);

		if (i == 0) {
			desired_min_uV = tmp_min;
			desired_max_uV = tmp_max;
		}
	}

	max_spread = constraints->max_spread[0];

	/*
	 * Let target_uV be equal to the desired one if possible.
	 * If not, set it to minimum voltage, allowed by other coupled
	 * regulators.
	 */
	target_uV = max(desired_min_uV, highest_min_uV - max_spread);

	/*
	 * Find min and max voltages, which currently aren't violating
	 * max_spread.
	 */
	for (i = 1; i < n_coupled; i++) {
		int tmp_act;

		tmp_act = regulator_get_voltage_rdev(c_rdevs[i]);
		if (tmp_act < 0)
			return tmp_act;

		min_current_uV = min(tmp_act, min_current_uV);
		max_current_uV = max(tmp_act, max_current_uV);
	}

	/*
	 * Correct target voltage, so as it currently isn't
	 * violating max_spread
	 */
	possible_uV = max(target_uV, max_current_uV - max_spread);
	possible_uV = min(possible_uV, min_current_uV + max_spread);

	if (possible_uV > desired_max_uV)
		return -EINVAL;

	done = (possible_uV == target_uV);
	desired_min_uV = possible_uV;

	/* Set current_uV if wasn't done earlier in the code and if necessary */
	if (*current_uV == -1) {
		ret = regulator_get_voltage_rdev(rdev);
		if (ret < 0)
			return ret;
		*current_uV = ret;
	}

	*min_uV = desired_min_uV;
	*max_uV = desired_max_uV;

	return done;
}

static int exynos_coupler_balance_voltage(struct regulator_coupler *coupler,
					  struct regulator_dev *rdev,
					  suspend_state_t state)
{
	struct regulator_dev **c_rdevs;
	struct regulator_dev *best_rdev;
	struct coupling_desc *c_desc = &rdev->coupling_desc;
	int i, ret, n_coupled, best_min_uV, best_max_uV, best_c_rdev;
	unsigned int delta, best_delta;
	unsigned long c_rdev_done = 0;
	bool best_c_rdev_done;

	c_rdevs = c_desc->coupled_rdevs;
	n_coupled = c_desc->n_coupled;

	/*
	 * Find the best possible voltage change on each loop. Leave the loop
	 * if there isn't any possible change.
	 */
	do {
		best_c_rdev_done = false;
		best_delta = 0;
		best_min_uV = 0;
		best_max_uV = 0;
		best_c_rdev = 0;
		best_rdev = NULL;

		/*
		 * Find highest difference between optimal voltage
		 * and current voltage.
		 */
		for (i = 0; i < n_coupled; i++) {
			/*
			 * optimal_uV is the best voltage that can be set for
			 * i-th regulator at the moment without violating
			 * max_spread constraint in order to balance
			 * the coupled voltages.
			 */
			int optimal_uV = 0, optimal_max_uV = 0, current_uV = 0;

			if (test_bit(i, &c_rdev_done))
				continue;

			ret = regulator_get_optimal_voltage(c_rdevs[i],
							    &current_uV,
							    &optimal_uV,
							    &optimal_max_uV,
							    state);
			if (ret < 0)
				goto out;

			delta = abs(optimal_uV - current_uV);

			if (delta && best_delta <= delta) {
				best_c_rdev_done = ret;
				best_delta = delta;
				best_rdev = c_rdevs[i];
				best_min_uV = optimal_uV;
				best_max_uV = optimal_max_uV;
				best_c_rdev = i;
			}
		}

		/* Nothing to change, return successfully */
		if (!best_rdev) {
			ret = 0;
			goto out;
		}

		ret = regulator_set_voltage_rdev(best_rdev, best_min_uV,
						 best_max_uV, state);

		if (ret < 0)
			goto out;

		if (best_c_rdev_done)
			set_bit(best_c_rdev, &c_rdev_done);

	} while (n_coupled > 1);

out:
	return ret;
}

static int exynos_coupler_attach(struct regulator_coupler *coupler,
				 struct regulator_dev *rdev)
{
	return 0;
}

static struct regulator_coupler exynos_coupler = {
	.attach_regulator = exynos_coupler_attach,
	.balance_voltage  = exynos_coupler_balance_voltage,
};

static int __init exynos_coupler_init(void)
{
	if (!of_machine_is_compatible("samsung,exynos5800"))
		return 0;

	return regulator_coupler_register(&exynos_coupler);
}
arch_initcall(exynos_coupler_init);
