/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Inochi Amaoto <inochiama@outlook.com>
 */

#ifndef _PINCTRL_SOPHGO_H
#define _PINCTRL_SOPHGO_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include "../core.h"

struct sophgo_pinctrl;

struct sophgo_pin {
	u16				id;
	u16				flags;
};

struct sophgo_pin_mux_config {
	const struct sophgo_pin	*pin;
	u32			config;
};

/**
 * struct sophgo_cfg_ops - pin configuration operations
 *
 * @pctrl_init: soc specific init callback
 * @verify_pinmux_config: verify the pinmux config for a pin
 * @verify_pin_group: verify the whole pinmux group
 * @dt_node_to_map_post: post init for the pinmux config map
 * @compute_pinconf_config: compute pinconf config
 * @set_pinconf_config: set pinconf config (the caller holds lock)
 * @set_pinmux_config: set mux config (the caller holds lock)
 */
struct sophgo_cfg_ops {
	int (*pctrl_init)(struct platform_device *pdev,
			  struct sophgo_pinctrl *pctrl);
	int (*verify_pinmux_config)(const struct sophgo_pin_mux_config *config);
	int (*verify_pin_group)(const struct sophgo_pin_mux_config *pinmuxs,
				unsigned int npins);
	int (*dt_node_to_map_post)(struct device_node *cur,
				   struct sophgo_pinctrl *pctrl,
				   struct sophgo_pin_mux_config *pinmuxs,
				   unsigned int npins);
	int (*compute_pinconf_config)(struct sophgo_pinctrl *pctrl,
				      const struct sophgo_pin *sp,
				      unsigned long *configs,
				      unsigned int num_configs,
				      u32 *value, u32 *mask);
	int (*set_pinconf_config)(struct sophgo_pinctrl *pctrl,
				  const struct sophgo_pin *sp,
				  u32 value, u32 mask);
	void (*set_pinmux_config)(struct sophgo_pinctrl *pctrl,
				  const struct sophgo_pin *sp, u32 config);
};

/**
 * struct sophgo_vddio_cfg_ops - pin vddio operations
 *
 * @get_pull_up: get resistor for pull up;
 * @get_pull_down: get resistor for pull down.
 * @get_oc_map: get mapping for typical low level output current value to
 *	register value map.
 * @get_schmitt_map: get mapping for register value to typical schmitt
 *	threshold.
 */
struct sophgo_vddio_cfg_ops {
	int (*get_pull_up)(const struct sophgo_pin *pin, const u32 *psmap);
	int (*get_pull_down)(const struct sophgo_pin *pin, const u32 *psmap);
	int (*get_oc_map)(const struct sophgo_pin *pin, const u32 *psmap,
			  const u32 **map);
	int (*get_schmitt_map)(const struct sophgo_pin *pin, const u32 *psmap,
			       const u32 **map);
};

struct sophgo_pinctrl_data {
	const struct pinctrl_pin_desc		*pins;
	const void				*pindata;
	const char				* const *pdnames;
	const struct sophgo_vddio_cfg_ops	*vddio_ops;
	const struct sophgo_cfg_ops		*cfg_ops;
	const struct pinctrl_ops		*pctl_ops;
	const struct pinmux_ops			*pmx_ops;
	const struct pinconf_ops		*pconf_ops;
	u16					npins;
	u16					npds;
	u16					pinsize;
};

struct sophgo_pinctrl {
	struct device				*dev;
	struct pinctrl_dev			*pctrl_dev;
	const struct sophgo_pinctrl_data	*data;
	struct pinctrl_desc			pdesc;

	struct mutex				mutex;
	raw_spinlock_t				lock;
	void					*priv_ctrl;
};

const struct sophgo_pin *sophgo_get_pin(struct sophgo_pinctrl *pctrl,
					unsigned long pin_id);
int sophgo_pctrl_dt_node_to_map(struct pinctrl_dev *pctldev, struct device_node *np,
				struct pinctrl_map **maps, unsigned int *num_maps);
int sophgo_pmx_set_mux(struct pinctrl_dev *pctldev,
		       unsigned int fsel, unsigned int gsel);
int sophgo_pconf_set(struct pinctrl_dev *pctldev, unsigned int pin_id,
		     unsigned long *configs, unsigned int num_configs);
int sophgo_pconf_group_set(struct pinctrl_dev *pctldev, unsigned int gsel,
			   unsigned long *configs, unsigned int num_configs);
u32 sophgo_pinctrl_typical_pull_down(struct sophgo_pinctrl *pctrl,
				     const struct sophgo_pin *pin,
				     const u32 *power_cfg);
u32 sophgo_pinctrl_typical_pull_up(struct sophgo_pinctrl *pctrl,
				   const struct sophgo_pin *pin,
				   const u32 *power_cfg);
int sophgo_pinctrl_oc2reg(struct sophgo_pinctrl *pctrl,
			  const struct sophgo_pin *pin,
			  const u32 *power_cfg, u32 target);
int sophgo_pinctrl_reg2oc(struct sophgo_pinctrl *pctrl,
			  const struct sophgo_pin *pin,
			  const u32 *power_cfg, u32 reg);
int sophgo_pinctrl_schmitt2reg(struct sophgo_pinctrl *pctrl,
			       const struct sophgo_pin *pin,
			       const u32 *power_cfg, u32 target);
int sophgo_pinctrl_reg2schmitt(struct sophgo_pinctrl *pctrl,
			       const struct sophgo_pin *pin,
			       const u32 *power_cfg, u32 reg);
int sophgo_pinctrl_probe(struct platform_device *pdev);

#endif /* _PINCTRL_SOPHGO_H */
