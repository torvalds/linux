/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Rockchip Electronics Co., Ltd. */

#ifndef CAM_SLEEP_WAKEUP_H
#define CAM_SLEEP_WAKEUP_H

#include <linux/types.h>

typedef int (*sensor_write_array)(struct i2c_client *, void *);

struct sensor_clk_obj {
	struct clk *xvclk;
	u32 clk_freq;
};

struct sensor_pin_obj {
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_sleep;
	bool reset_active_state;
	bool pwdn_active_state;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	int supplies_num;
	struct regulator_bulk_data *supplies;
};

struct cam_sw_info {
	phys_addr_t phys;
	struct sensor_clk_obj clk;
	struct sensor_pin_obj pin;
	struct i2c_client *client;
	void *array_regs;
	struct preisp_hdrae_exp_s hdr_ae;
	sensor_write_array write_array;
};

#if IS_REACHABLE(CONFIG_VIDEO_CAM_SLEEP_WAKEUP)
struct cam_sw_info *cam_sw_init(void);
int cam_sw_deinit(struct cam_sw_info *info);
int cam_sw_clk_init(struct cam_sw_info *info, struct clk *xvclk, u32 clk_freq);
int cam_sw_reset_pin_init(struct cam_sw_info *info,
			  struct gpio_desc *reset_gpio,
			  bool reset_active_state);
int cam_sw_pwdn_pin_init(struct cam_sw_info *info, struct gpio_desc *pwdn_gpio,
			 bool pwdn_active_state);
int cam_sw_pinctrl_init(struct cam_sw_info *info, struct pinctrl *pinctrl,
			struct pinctrl_state *pins_default,
			struct pinctrl_state *pins_sleep);
int cam_sw_regulator_bulk_init(struct cam_sw_info *info, int supplies_num,
			       struct regulator_bulk_data *supplies);
int cam_sw_write_array_cb_init(struct cam_sw_info *info,
			       struct i2c_client *client, void *array_regs,
			       sensor_write_array write_array);
int cam_sw_write_array(struct cam_sw_info *info);
int cam_sw_prepare_wakeup(struct cam_sw_info *info, struct device *dev);
int cam_sw_prepare_sleep(struct cam_sw_info *info);

#else

static inline struct cam_sw_info *cam_sw_init(void)
{
	return NULL;
}

static inline int cam_sw_deinit(struct cam_sw_info *info)
{
	return 0;
}

static inline int cam_sw_clk_init(struct cam_sw_info *info, struct clk *xvclk,
				  u32 clk_freq)
{
	return 0;
}

static inline int cam_sw_reset_pin_init(struct cam_sw_info *info,
					struct gpio_desc *reset_gpio,
					bool reset_active_state)
{
	return 0;
}

static inline int cam_sw_pwdn_pin_init(struct cam_sw_info *info,
				       struct gpio_desc *pwdn_gpio,
				       bool pwdn_active_state)
{
	return 0;
}

static inline int cam_sw_pinctrl_init(struct cam_sw_info *info,
				      struct pinctrl *pinctrl,
				      struct pinctrl_state *pins_default,
				      struct pinctrl_state *pins_sleep)
{
	return 0;
}

static inline int
cam_sw_regulator_bulk_init(struct cam_sw_info *info, int supplies_num,
			   struct regulator_bulk_data *supplies)
{
	return 0;
}

static inline int cam_sw_write_array_cb_init(struct cam_sw_info *info,
					     struct i2c_client *client,
					     void *array_regs,
					     sensor_write_array write_array)
{
	return 0;
}

static inline int cam_sw_write_array(struct cam_sw_info *info)
{
	return 0;
}

static inline int cam_sw_prepare_wakeup(struct cam_sw_info *info,
					struct device *dev)
{
	return 0;
}

static inline int cam_sw_prepare_sleep(struct cam_sw_info *info)
{
	return 0;
}

#endif
#endif
