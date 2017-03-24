/*
 * Internal interface to pinctrl device tree integration
 *
 * Copyright (C) 2012 NVIDIA CORPORATION. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

struct of_phandle_args;

#ifdef CONFIG_OF

bool pinctrl_dt_has_hogs(struct pinctrl_dev *pctldev);

void pinctrl_dt_free_maps(struct pinctrl *p);
int pinctrl_dt_to_map(struct pinctrl *p, struct pinctrl_dev *pctldev);

int pinctrl_count_index_with_args(const struct device_node *np,
				  const char *list_name);

int pinctrl_parse_index_with_args(const struct device_node *np,
				  const char *list_name, int index,
				  struct of_phandle_args *out_args);

#else

static inline bool pinctrl_dt_has_hogs(struct pinctrl_dev *pctldev)
{
	return false;
}

static inline int pinctrl_dt_to_map(struct pinctrl *p,
				    struct pinctrl_dev *pctldev)
{
	return 0;
}

static inline void pinctrl_dt_free_maps(struct pinctrl *p)
{
}

static inline int pinctrl_count_index_with_args(const struct device_node *np,
						const char *list_name)
{
	return -ENODEV;
}

static inline int
pinctrl_parse_index_with_args(const struct device_node *np,
			      const char *list_name, int index,
			      struct of_phandle_args *out_args)
{
	return -ENODEV;
}

#endif
