/*
 * copyright (c) 2012 samsung electronics co., ltd.
 *		http://www.samsung.com
 *
 * this program is free software; you can redistribute it and/or modify
 * it under the terms of the gnu general public license version 2 as
 * published by the free software foundation.
*/

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/mutex.h>

#include <mach/asv-exynos.h>
#include <mach/tmu.h>

#include "exynos5410_volt_ctrl.h"
#define SAFE_VOLT(x)		(x + 25000)
#define UPPER_ALLOWED_SKEW	(150000)
#define LOWER_ALLOWED_SKEW	(62500)
#define COLD_VOLT_OFFSET	(75000)
#define LIMIT_COLD_VOLTAGE	(1250000)

#ifdef SKEW_DEBUG
unsigned int g_mif_maxvol;
#endif
static unsigned int exynos5_volt_offset;

struct mutex voltlock;

struct exynos5_volt_info exynos5_vdd_int = {
	.idx	= VDD_INT,
};

struct exynos5_volt_info exynos5_vdd_mif = {
	.idx	= VDD_MIF,
};

enum exynos5_int_idx {
	INT_L0,
	INT_L1,
	INT_L2,
	INT_L3,
	INT_L4,
	INT_L5,
	INT_L6,
	INT_L7,
	INT_L_END,
};

enum exynos5_mif_idx {
	MIF_L0,
	MIF_L1,
	MIF_L2,
	MIF_L3,
	MIF_L4,
	MIF_L5,
	MIF_L6,
	MIF_L7,
	MIF_L_END,
};

struct volt_table {
	unsigned int idx;
	unsigned long clk;
	unsigned long volt;
};

static struct volt_table mif_volt_table[] = {
	{MIF_L0, 800000, 0},	/* ISP Special Level */
	{MIF_L1, 667000, 0},	/* ISP Special Level */
	{MIF_L2, 533000, 0},
	{MIF_L3, 400000, 0},
	{MIF_L4, 267000, 0},
	{MIF_L5, 200000, 0},
	{MIF_L6, 160000, 0},
	{MIF_L7, 100000, 0},
};

static struct volt_table int_volt_table_mif_lv0[] = {
	{INT_L0, 800000, 0},	/* ISP Special Level */
	{INT_L1, 700000, 0},	/* ISP Special Level */
	{INT_L2, 400000, 0},
	{INT_L3, 267000, 0},
	{INT_L4, 200000, 0},
	{INT_L5, 160000, 0},
	{INT_L6, 100000, 0},
	{INT_L7,  50000, 0},
};

static struct volt_table int_volt_table_mif_lv1[] = {
	{INT_L0, 800000, 0},	/* ISP Special Level */
	{INT_L1, 700000, 0},	/* ISP Special Level */
	{INT_L2, 400000, 0},
	{INT_L3, 267000, 0},
	{INT_L4, 200000, 0},
	{INT_L5, 160000, 0},
	{INT_L6, 100000, 0},
	{INT_L7,  50000, 0},
};

static struct volt_table int_volt_table_mif_lv2[] = {
	{INT_L0, 800000, 0},	/* ISP Special Level */
	{INT_L1, 700000, 0},	/* ISP Special Level */
	{INT_L2, 400000, 0},
	{INT_L3, 267000, 0},
	{INT_L4, 200000, 0},
	{INT_L5, 160000, 0},
	{INT_L6, 100000, 0},
	{INT_L7,  50000, 0},
};

static struct volt_table int_volt_table_mif_lv3[] = {
	{INT_L0, 800000, 0},	/* ISP Special Level */
	{INT_L1, 700000, 0},	/* ISP Special Level */
	{INT_L2, 400000, 0},
	{INT_L3, 267000, 0},
	{INT_L4, 200000, 0},
	{INT_L5, 160000, 0},
	{INT_L6, 100000, 0},
	{INT_L7,  50000, 0},
};

static struct volt_table *target_int_volt_table;

static unsigned int get_limit_voltage(unsigned int voltage)
{
	if (voltage > LIMIT_COLD_VOLTAGE)
		return voltage;

	if (voltage + exynos5_volt_offset > LIMIT_COLD_VOLTAGE)
		return LIMIT_COLD_VOLTAGE;

	return voltage + exynos5_volt_offset;
}

static void exynos5_set_int_volt_list(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(int_volt_table_mif_lv0); i++)
		int_volt_table_mif_lv0[i].volt = get_match_volt(ID_INT_MIF_L0, int_volt_table_mif_lv0[i].clk);

	for (i = 0; i < ARRAY_SIZE(int_volt_table_mif_lv1); i++)
		int_volt_table_mif_lv1[i].volt = get_match_volt(ID_INT_MIF_L1, int_volt_table_mif_lv1[i].clk);

	for (i = 0; i < ARRAY_SIZE(int_volt_table_mif_lv2); i++)
		int_volt_table_mif_lv2[i].volt = get_match_volt(ID_INT_MIF_L2, int_volt_table_mif_lv2[i].clk);

	for (i = 0; i < ARRAY_SIZE(int_volt_table_mif_lv3); i++)
		int_volt_table_mif_lv3[i].volt = get_match_volt(ID_INT_MIF_L3, int_volt_table_mif_lv3[i].clk);
}

void exynos5_set_idx_match_freq(enum exynos5_volt_id target, unsigned int target_freq)
{
	unsigned int i;

	switch (target) {
	case VDD_INT:
		for (i = 0; i < ARRAY_SIZE(int_volt_table_mif_lv0); i++) {
			if (int_volt_table_mif_lv0[i].clk == target_freq)
				exynos5_vdd_int.cur_lv = int_volt_table_mif_lv0[i].idx;
		}
		break;
	case VDD_MIF:
		for (i = 0; i < ARRAY_SIZE(mif_volt_table); i++) {
			if (mif_volt_table[i].clk == target_freq)
				exynos5_vdd_mif.cur_lv = mif_volt_table[i].idx;
		}
		break;
	}
}

static bool is_skew_ok(unsigned int intvolt, unsigned int mifvolt)
{
	if ((intvolt <= mifvolt + UPPER_ALLOWED_SKEW) && (intvolt > mifvolt))
		return true;
	else if ((intvolt >= mifvolt - LOWER_ALLOWED_SKEW) && (intvolt <= mifvolt))
		return true;
	else
		return false;
}

#ifdef SKEW_DEBUG
static void volt_checkup(unsigned int number)
{
	unsigned int int_volt, mif_volt;

	int_volt = regulator_get_voltage(exynos5_vdd_int.vdd_target);
	mif_volt = regulator_get_voltage(exynos5_vdd_mif.vdd_target);

	if ((int_volt - mif_volt) > UPPER_ALLOWED_SKEW && (int_volt > mif_volt)) {
		pr_info("\n%d\n\nUpper skew violation!!!\nintvolt=%d, mifvolt=%d\n\n\n",
				number , int_volt, mif_volt);
		BUG_ON(1);
	}

	if ((mif_volt - int_volt) > LOWER_ALLOWED_SKEW && (mif_volt > int_volt)) {
		pr_info("\n%d\n\nLower skew violation!!!\nintvolt=%d, mifvolt=%d\n\n\n",
				number, int_volt, mif_volt);
		BUG_ON(1);
	}

	if (g_mif_maxvol + UPPER_ALLOWED_SKEW < mif_volt) {
		pr_info("\n\nMIF voltage exceeded... cur_mifvol=%d, maxval=%d\n\n", mif_volt, g_mif_maxvol + UPPER_ALLOWED_SKEW);
	}
}
#else
#define volt_checkup(number) do { } while (0)
#endif

/*
 * If exynos5_check_skew return true,
 * it is need to recalc VDD_INT and VDD_MIF by skew
 */
bool exynos5_check_skew(unsigned int *target_int, unsigned int *target_mif)
{
	unsigned int calc_int = *target_int;
	unsigned int calc_mif = *target_mif;
#ifdef SKEW_DEBUG
	unsigned int prev_int, prev_mif;
	prev_int = calc_int;
	prev_mif = calc_mif;
#endif

	/* Update INT/MIF target voltage */
	if (calc_int)
		exynos5_vdd_int.target_volt = calc_int;
	if (calc_mif)
		exynos5_vdd_mif.target_volt = calc_mif;

	if (!calc_int)
		calc_int = regulator_get_voltage(exynos5_vdd_int.vdd_target);
	if (!calc_mif)
		calc_mif = regulator_get_voltage(exynos5_vdd_mif.vdd_target);

	/* VDD_INT can not higher than (VDD_MIF + UPPER_ALLOWED_SKEW) */
	if ((calc_int > calc_mif) && (calc_int > (calc_mif + UPPER_ALLOWED_SKEW))) {
		calc_mif = calc_int - UPPER_ALLOWED_SKEW;
		goto update_skew;
	/* VDD_MIF can not higher than (VDD_INT + LOWER_ALLOWED_SKEW) */
	} else if ((calc_int < calc_mif) && ((calc_int + LOWER_ALLOWED_SKEW) < calc_mif)) {
		calc_int = calc_mif - LOWER_ALLOWED_SKEW;
		goto update_skew;
	}

	return false;

update_skew:
#ifdef SKEW_DEBUG
	pr_info("SKEW occur.. prev_int=%d, prev_mif=%d, new_int=%d, new_mif=%d\n",
			prev_int, prev_mif, calc_int, calc_mif);
#endif
	exynos5_vdd_int.set_volt = calc_int;
	exynos5_vdd_mif.set_volt = calc_mif;

	*target_int = calc_int;
	*target_mif = calc_mif;

	return true;
}

void exynos5_update_volt(unsigned int *target_int, unsigned int *target_mif)
{
	unsigned int calc_int = *target_int;
	unsigned int calc_mif = *target_mif;

	if (calc_int && !calc_mif && (exynos5_vdd_mif.target_volt != exynos5_vdd_mif.set_volt)) {
		calc_mif = exynos5_vdd_mif.target_volt;

		if ((calc_int > calc_mif) && (calc_int > (calc_mif + UPPER_ALLOWED_SKEW)))
			calc_mif = calc_int - UPPER_ALLOWED_SKEW;
		else if ((calc_int < calc_mif) && ((calc_int + LOWER_ALLOWED_SKEW) < calc_mif))
			calc_mif = calc_int + LOWER_ALLOWED_SKEW;

		*target_mif = calc_mif;
		exynos5_vdd_mif.set_volt = calc_mif;

		return;
	}

	if (!calc_int && calc_mif && (exynos5_vdd_int.target_volt != exynos5_vdd_int.set_volt)) {
		calc_int = exynos5_vdd_int.target_volt;

		if ((calc_int > calc_mif) && (calc_int > (calc_mif + UPPER_ALLOWED_SKEW)))
			calc_int = calc_mif + UPPER_ALLOWED_SKEW;
		else if ((calc_int < calc_mif) && ((calc_int + LOWER_ALLOWED_SKEW) < calc_mif))
			calc_int = calc_mif - LOWER_ALLOWED_SKEW;

		*target_int = calc_int;
		exynos5_vdd_int.set_volt = calc_int;

		return;
	}

	if (calc_int)
		exynos5_vdd_int.set_volt = calc_int;

	if (calc_mif)
		exynos5_vdd_mif.set_volt = calc_mif;
}

int exynos5_volt_ctrl(enum exynos5_volt_id target,
			unsigned int target_volt, unsigned int target_freq)
{
	int ret = 0;
	unsigned int calc_int = 0;
	unsigned int calc_mif = 0;
	unsigned int prev_int;
	unsigned int prev_mif;

	if ((target != VDD_INT) && (target != VDD_MIF)) {
		pr_err("Unknown target to set VDD\n");
		ret = -ENOMEM;
		goto out;
	}

	mutex_lock(&voltlock);

	prev_int = regulator_get_voltage(exynos5_vdd_int.vdd_target);
	prev_mif = regulator_get_voltage(exynos5_vdd_mif.vdd_target);

	exynos5_set_idx_match_freq(target, target_freq);

	switch (target) {
	case VDD_INT:
		calc_int = get_limit_voltage(target_int_volt_table[exynos5_vdd_int.cur_lv].volt);

		break;
	case VDD_MIF:
		calc_mif = get_limit_voltage(target_volt);

		if (exynos5_vdd_mif.cur_lv == MIF_L0)
			target_int_volt_table = int_volt_table_mif_lv0;
		else if (exynos5_vdd_mif.cur_lv == MIF_L3)
			target_int_volt_table = int_volt_table_mif_lv1;
		else if (exynos5_vdd_mif.cur_lv == MIF_L5)
			target_int_volt_table = int_volt_table_mif_lv2;
		else if (exynos5_vdd_mif.cur_lv == MIF_L7)
			target_int_volt_table = int_volt_table_mif_lv3;
		else
			pr_err("%s : Target freq is invalid\n", __func__);

		calc_int = get_limit_voltage(target_int_volt_table[exynos5_vdd_int.cur_lv].volt);
		break;
	}

	if (!exynos5_check_skew(&calc_int, &calc_mif)) {
		exynos5_update_volt(&calc_int, &calc_mif);
#ifdef SKEW_DEBUG
		pr_info("[%s]No skew:: volt int=%d, mif=%d FREQ : %d\n",
				(target == VDD_MIF) ? "MIF" : "INT",
				calc_int, calc_mif, target_freq);
#endif
	} else { /* (calc_mif && calc_int) */

#ifdef SKEW_DEBUG
		pr_info("[%s]Yes skew:: volt int=%d, mif=%d FREQ : %d\n",
				(target == VDD_MIF) ? "MIF" : "INT",
				calc_int, calc_mif, target_freq);
#endif
		if (is_skew_ok(prev_int, calc_mif)) {
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
			volt_checkup(50);
			regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
			volt_checkup(51);
		} else if (is_skew_ok(calc_int, prev_mif)) {
			regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
			volt_checkup(52);
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
			volt_checkup(53);
		} else if (prev_int < calc_mif) { /* case 1 */
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, prev_int + LOWER_ALLOWED_SKEW,
					SAFE_VOLT(prev_int + LOWER_ALLOWED_SKEW));
			volt_checkup(54);
			if ((is_skew_ok(calc_int, prev_int + LOWER_ALLOWED_SKEW))
				|| (calc_int < prev_int + LOWER_ALLOWED_SKEW)) {
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(55);
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(56);
			} else if (calc_int >= prev_int + LOWER_ALLOWED_SKEW) {
				regulator_set_voltage(exynos5_vdd_int.vdd_target,
						prev_int + LOWER_ALLOWED_SKEW + UPPER_ALLOWED_SKEW,
						SAFE_VOLT(prev_int + LOWER_ALLOWED_SKEW + UPPER_ALLOWED_SKEW));
			volt_checkup(57);
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(58);
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(59);
			} else {
				pr_info("Impossible point 2 !\n");
				BUG_ON(1);
			}
		} else if (prev_int > calc_mif) { /* case 2 */
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, prev_int - UPPER_ALLOWED_SKEW,
					SAFE_VOLT(prev_int - UPPER_ALLOWED_SKEW));
			volt_checkup(60);
			if ((is_skew_ok(calc_int, prev_int - UPPER_ALLOWED_SKEW))
					|| (calc_int > prev_int - UPPER_ALLOWED_SKEW)) {
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(61);
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(62);
			} else if (calc_int <= prev_int - UPPER_ALLOWED_SKEW) {
				regulator_set_voltage(exynos5_vdd_int.vdd_target,
						prev_int - UPPER_ALLOWED_SKEW - LOWER_ALLOWED_SKEW,
						SAFE_VOLT(prev_int - UPPER_ALLOWED_SKEW - LOWER_ALLOWED_SKEW));
				volt_checkup(63);
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(64);
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(65);
			} else {
				pr_info("Impossible point 3 !\n");
				BUG_ON(1);

			}
		} else if (calc_int > prev_mif) { /* case 3 */
			regulator_set_voltage(exynos5_vdd_int.vdd_target, prev_mif + UPPER_ALLOWED_SKEW,
					SAFE_VOLT(prev_mif + UPPER_ALLOWED_SKEW));
			volt_checkup(66);
			if ((is_skew_ok(prev_mif + UPPER_ALLOWED_SKEW, calc_mif))
				|| (prev_mif + UPPER_ALLOWED_SKEW > calc_mif)) {
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(67);
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(68);
			} else if (prev_mif + UPPER_ALLOWED_SKEW <= calc_mif) {
				regulator_set_voltage(exynos5_vdd_mif.vdd_target,
						prev_mif + UPPER_ALLOWED_SKEW + LOWER_ALLOWED_SKEW,
						SAFE_VOLT(prev_mif + UPPER_ALLOWED_SKEW + LOWER_ALLOWED_SKEW));
				volt_checkup(69);
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(70);
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(71);
			} else {
				pr_info("Impossible point 4 !\n");
				BUG_ON(1);
			}
		} else if (calc_int < prev_int) { /* case 4 */

			regulator_set_voltage(exynos5_vdd_int.vdd_target, prev_mif - LOWER_ALLOWED_SKEW,
					SAFE_VOLT(prev_mif - LOWER_ALLOWED_SKEW));
			volt_checkup(72);
			if ((is_skew_ok(prev_mif - LOWER_ALLOWED_SKEW, calc_mif))
				|| (prev_mif - LOWER_ALLOWED_SKEW < calc_mif)) {
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(73);
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(74);
			} else if (prev_mif - LOWER_ALLOWED_SKEW >= calc_mif) {
				regulator_set_voltage(exynos5_vdd_mif.vdd_target,
						prev_mif - LOWER_ALLOWED_SKEW - UPPER_ALLOWED_SKEW,
						SAFE_VOLT(prev_mif - LOWER_ALLOWED_SKEW - UPPER_ALLOWED_SKEW));
				volt_checkup(75);
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(76);
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(77);
			} else {
				pr_info("Impossible point 5 !\n");
				BUG_ON(1);
			}
		} else {
			pr_info("Impossible point 1 !\n");
			BUG_ON(1);
		}

		goto out;
	}

	if (calc_int && !calc_mif) {
		regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
		volt_checkup(5);
	} else if (calc_mif && !calc_int) {
		regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
		volt_checkup(6);
	} else if (calc_mif && calc_int) {
		if (is_skew_ok(prev_int, calc_mif)) {
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
			volt_checkup(150);
			regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
			volt_checkup(151);
		} else if (is_skew_ok(calc_int, prev_mif)) {
			regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
			volt_checkup(152);
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
			volt_checkup(153);
		} else if (prev_int < calc_mif) { /* case 1 */
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, prev_int + LOWER_ALLOWED_SKEW,
					SAFE_VOLT(prev_int + LOWER_ALLOWED_SKEW));
			volt_checkup(154);
			if ((is_skew_ok(calc_int, prev_int + LOWER_ALLOWED_SKEW))
				|| (calc_int < prev_int + LOWER_ALLOWED_SKEW)) {
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(155);
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(156);
			} else if (calc_int >= prev_int + LOWER_ALLOWED_SKEW) {
				regulator_set_voltage(exynos5_vdd_int.vdd_target,
						prev_int + LOWER_ALLOWED_SKEW + UPPER_ALLOWED_SKEW,
						SAFE_VOLT(prev_int + LOWER_ALLOWED_SKEW + UPPER_ALLOWED_SKEW));
			volt_checkup(157);
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(158);
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(159);
			} else {
				pr_info("Impossible point 12 !\n");
				BUG_ON(1);
			}
		} else if (prev_int > calc_mif) { /* case 2 */
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, prev_int - UPPER_ALLOWED_SKEW,
					SAFE_VOLT(prev_int - UPPER_ALLOWED_SKEW));
			volt_checkup(160);
			if ((is_skew_ok(calc_int, prev_int - UPPER_ALLOWED_SKEW))
					|| (calc_int > prev_int - UPPER_ALLOWED_SKEW)) {
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(161);
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(162);
			} else if (calc_int <= prev_int - UPPER_ALLOWED_SKEW) {
				regulator_set_voltage(exynos5_vdd_int.vdd_target,
						prev_int - UPPER_ALLOWED_SKEW - LOWER_ALLOWED_SKEW,
						SAFE_VOLT(prev_int - UPPER_ALLOWED_SKEW - LOWER_ALLOWED_SKEW));
				volt_checkup(163);
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(164);
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(165);
			} else {
				pr_info("Impossible point 13 !\n");
				BUG_ON(1);

			}
		} else if (calc_int > prev_mif) { /* case 3 */
			regulator_set_voltage(exynos5_vdd_int.vdd_target, prev_mif + UPPER_ALLOWED_SKEW,
					SAFE_VOLT(prev_mif + UPPER_ALLOWED_SKEW));
			volt_checkup(166);
			if ((is_skew_ok(prev_mif + UPPER_ALLOWED_SKEW, calc_mif))
				|| (prev_mif + UPPER_ALLOWED_SKEW > calc_mif)) {
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(167);
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(168);
			} else if (prev_mif + UPPER_ALLOWED_SKEW <= calc_mif) {
				regulator_set_voltage(exynos5_vdd_mif.vdd_target,
						prev_mif + UPPER_ALLOWED_SKEW + LOWER_ALLOWED_SKEW,
						SAFE_VOLT(prev_mif + UPPER_ALLOWED_SKEW + LOWER_ALLOWED_SKEW));
				volt_checkup(169);
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(170);
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(171);
			} else {
				pr_info("Impossible point 14 !\n");
				BUG_ON(1);
			}
		} else if (calc_int < prev_int) { /* case 4 */

			regulator_set_voltage(exynos5_vdd_int.vdd_target, prev_mif - LOWER_ALLOWED_SKEW,
					SAFE_VOLT(prev_mif - LOWER_ALLOWED_SKEW));
			volt_checkup(172);
			if ((is_skew_ok(prev_mif - LOWER_ALLOWED_SKEW, calc_mif))
				|| (prev_mif - LOWER_ALLOWED_SKEW < calc_mif)) {
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(173);
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(174);
			} else if (prev_mif - LOWER_ALLOWED_SKEW >= calc_mif) {
				regulator_set_voltage(exynos5_vdd_mif.vdd_target,
						prev_mif - LOWER_ALLOWED_SKEW - UPPER_ALLOWED_SKEW,
						SAFE_VOLT(prev_mif - LOWER_ALLOWED_SKEW - UPPER_ALLOWED_SKEW));
				volt_checkup(175);
				regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
				volt_checkup(176);
				regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
				volt_checkup(177);
			} else {
				pr_info("Impossible point 15 !\n");
				BUG_ON(1);
			}
		} else {
			pr_info("Impossible point 11 !\n");
			BUG_ON(1);
		}
	}
out:
	mutex_unlock(&voltlock);
	return ret;
}

#ifdef CONFIG_EXYNOS_THERMAL
static int exynos5_volt_ctrl_tmu_notifier(struct notifier_block *notifier,
						unsigned long event, void *v)
{
	unsigned int *on = v;
	unsigned int calc_mif, calc_int;
	unsigned int prev_int;
	unsigned int prev_mif;

	if (event != TMU_COLD)
		return NOTIFY_OK;

	mutex_lock(&voltlock);

	prev_int = regulator_get_voltage(exynos5_vdd_int.vdd_target);
	prev_mif = regulator_get_voltage(exynos5_vdd_mif.vdd_target);

	if (*on) {
		if (exynos5_volt_offset != COLD_VOLT_OFFSET) {
			exynos5_volt_offset = COLD_VOLT_OFFSET;
		} else {
			mutex_unlock(&voltlock);

			return NOTIFY_OK;
		}

		/* Setting voltage for MIF about cold temperature */
		exynos5_vdd_int.set_volt = get_limit_voltage(prev_int);
		exynos5_vdd_mif.set_volt = get_limit_voltage(prev_mif);
		calc_int = exynos5_vdd_int.set_volt;
		calc_mif = exynos5_vdd_mif.set_volt;
	} else {
		if (exynos5_volt_offset != 0) {
			exynos5_volt_offset = 0;
		} else {
			mutex_unlock(&voltlock);

			return NOTIFY_OK;
		}
		exynos5_vdd_int.set_volt = get_limit_voltage(prev_int - COLD_VOLT_OFFSET);
		exynos5_vdd_mif.set_volt = get_limit_voltage(prev_mif - COLD_VOLT_OFFSET);
		calc_int = exynos5_vdd_int.set_volt;
		calc_mif = exynos5_vdd_mif.set_volt;
	}

#ifdef SKEW_DEBUG
	pr_info("TMU Noti: offset=%d, prevInt=%d, prevMif=%d, int=%d, mif=%d\n", exynos5_volt_offset,
			prev_int, prev_mif, exynos5_vdd_int.set_volt, exynos5_vdd_mif.set_volt);
#endif
	if (is_skew_ok(prev_int, calc_mif)) {
		regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
		regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
	} else if (is_skew_ok(calc_int, prev_mif)) {
		regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
		regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
	} else if (prev_int < calc_mif) { /* case 1 */
		regulator_set_voltage(exynos5_vdd_mif.vdd_target, prev_int + LOWER_ALLOWED_SKEW,
				SAFE_VOLT(prev_int + LOWER_ALLOWED_SKEW));
		if ((is_skew_ok(calc_int, prev_int + LOWER_ALLOWED_SKEW))
				|| (calc_int < prev_int + LOWER_ALLOWED_SKEW)) {
			regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
		} else if (calc_int >= prev_int + LOWER_ALLOWED_SKEW) {
			regulator_set_voltage(exynos5_vdd_int.vdd_target,
					prev_int + LOWER_ALLOWED_SKEW + UPPER_ALLOWED_SKEW,
					SAFE_VOLT(prev_int + LOWER_ALLOWED_SKEW + UPPER_ALLOWED_SKEW));
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
			regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
		} else {
			pr_info("Impossible point 22!\n");
			BUG_ON(1);
		}
	} else if (prev_int > calc_mif) { /* case 2 */
		regulator_set_voltage(exynos5_vdd_mif.vdd_target, prev_int - UPPER_ALLOWED_SKEW,
				SAFE_VOLT(prev_int - UPPER_ALLOWED_SKEW));
		if ((is_skew_ok(calc_int, prev_int - UPPER_ALLOWED_SKEW))
				|| (calc_int > prev_int - UPPER_ALLOWED_SKEW)) {
			regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
		} else if (calc_int <= prev_int - UPPER_ALLOWED_SKEW) {
			regulator_set_voltage(exynos5_vdd_int.vdd_target,
					prev_int - UPPER_ALLOWED_SKEW - LOWER_ALLOWED_SKEW,
					SAFE_VOLT(prev_int - UPPER_ALLOWED_SKEW - LOWER_ALLOWED_SKEW));
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
			regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
		} else {
			pr_info("Impossible point 23 !\n");
			BUG_ON(1);

		}
	} else if (calc_int > prev_mif) { /* case 3 */
		regulator_set_voltage(exynos5_vdd_int.vdd_target, prev_mif + UPPER_ALLOWED_SKEW,
				SAFE_VOLT(prev_mif + UPPER_ALLOWED_SKEW));
		if ((is_skew_ok(prev_mif + UPPER_ALLOWED_SKEW, calc_mif))
				|| (prev_mif + UPPER_ALLOWED_SKEW > calc_mif)) {
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
			regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
		} else if (prev_mif + UPPER_ALLOWED_SKEW <= calc_mif) {
			regulator_set_voltage(exynos5_vdd_mif.vdd_target,
					prev_mif + UPPER_ALLOWED_SKEW + LOWER_ALLOWED_SKEW,
					SAFE_VOLT(prev_mif + UPPER_ALLOWED_SKEW + LOWER_ALLOWED_SKEW));
			regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
		} else {
			pr_info("Impossible point 24 !\n");
			BUG_ON(1);
		}
	} else if (calc_int < prev_int) { /* case 4 */

		regulator_set_voltage(exynos5_vdd_int.vdd_target, prev_mif - LOWER_ALLOWED_SKEW,
				SAFE_VOLT(prev_mif - LOWER_ALLOWED_SKEW));
		if ((is_skew_ok(prev_mif - LOWER_ALLOWED_SKEW, calc_mif))
				|| (prev_mif - LOWER_ALLOWED_SKEW < calc_mif)) {
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
			regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
		} else if (prev_mif - LOWER_ALLOWED_SKEW >= calc_mif) {
			regulator_set_voltage(exynos5_vdd_mif.vdd_target,
					prev_mif - LOWER_ALLOWED_SKEW - UPPER_ALLOWED_SKEW,
					SAFE_VOLT(prev_mif - LOWER_ALLOWED_SKEW - UPPER_ALLOWED_SKEW));
			regulator_set_voltage(exynos5_vdd_int.vdd_target, calc_int, SAFE_VOLT(calc_int));
			regulator_set_voltage(exynos5_vdd_mif.vdd_target, calc_mif, SAFE_VOLT(calc_mif));
		} else {
			pr_info("Impossible point 25 !\n");
			BUG_ON(1);
		}
	} else {
		pr_info("Impossible point 21 !\n");
		BUG_ON(1);
	}

	mutex_unlock(&voltlock);
	return NOTIFY_OK;
}

static struct notifier_block exynos5_volt_ctrl_tmu_nb = {
	.notifier_call = exynos5_volt_ctrl_tmu_notifier,
};
#endif

static int __init exynos5_volt_ctrl_init(void)
{
	unsigned int err = 0;
	exynos5_volt_offset = 0;
	/* INT Setting regulator inform */
	exynos5_vdd_int.vdd_target = regulator_get(NULL, "vdd_int");

	if (IS_ERR(exynos5_vdd_int.vdd_target)) {
		pr_err("Cannot get the regulator vdd_int\n");
		err = PTR_ERR(exynos5_vdd_int.vdd_target);
	}

	exynos5_vdd_int.set_volt = regulator_get_voltage(exynos5_vdd_int.vdd_target);
	exynos5_vdd_int.target_volt = exynos5_vdd_int.set_volt;

	/* MIF Setting regulator inform */
	exynos5_vdd_mif.vdd_target = regulator_get(NULL, "vdd_mif");

	if (IS_ERR(exynos5_vdd_mif.vdd_target)) {
		pr_err("Cannot get the regulator vdd_mif\n");
		err = PTR_ERR(exynos5_vdd_mif.vdd_target);
	}

	exynos5_vdd_mif.set_volt = regulator_get_voltage(exynos5_vdd_mif.vdd_target);
	exynos5_vdd_mif.target_volt = exynos5_vdd_mif.set_volt;

#ifdef SKEW_DEBUG
	g_mif_maxvol = exynos5_vdd_mif.set_volt;
#endif
	exynos5_set_int_volt_list();

	/* Set initial information */
	exynos5_vdd_mif.cur_lv = MIF_L0;
	exynos5_vdd_int.cur_lv = INT_L2;

	mutex_init(&voltlock);

	target_int_volt_table = int_volt_table_mif_lv0;
#ifdef CONFIG_EXYNOS_THERMAL
	exynos_tmu_add_notifier(&exynos5_volt_ctrl_tmu_nb);
#endif
	return err;
}
device_initcall(exynos5_volt_ctrl_init);

static void __exit exynos5_volt_ctrl_exit(void)
{
	if (exynos5_vdd_int.vdd_target)
		regulator_put(exynos5_vdd_int.vdd_target);

	if (exynos5_vdd_mif.vdd_target)
		regulator_put(exynos5_vdd_mif.vdd_target);
}
module_exit(exynos5_volt_ctrl_exit);
