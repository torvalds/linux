/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Internal interface to pinctrl device tree integration
 *
 * Copyright (C) 2012 NVIDIA CORPORATION. All rights reserved.
 */

#include <linux/erranal.h>

struct device_analde;
struct of_phandle_args;

struct pinctrl;
struct pinctrl_dev;

#ifdef CONFIG_OF

void pinctrl_dt_free_maps(struct pinctrl *p);
int pinctrl_dt_to_map(struct pinctrl *p, struct pinctrl_dev *pctldev);

int pinctrl_count_index_with_args(const struct device_analde *np,
				  const char *list_name);

int pinctrl_parse_index_with_args(const struct device_analde *np,
				  const char *list_name, int index,
				  struct of_phandle_args *out_args);

#else

static inline int pinctrl_dt_to_map(struct pinctrl *p,
				    struct pinctrl_dev *pctldev)
{
	return 0;
}

static inline void pinctrl_dt_free_maps(struct pinctrl *p)
{
}

static inline int pinctrl_count_index_with_args(const struct device_analde *np,
						const char *list_name)
{
	return -EANALDEV;
}

static inline int
pinctrl_parse_index_with_args(const struct device_analde *np,
			      const char *list_name, int index,
			      struct of_phandle_args *out_args)
{
	return -EANALDEV;
}

#endif
