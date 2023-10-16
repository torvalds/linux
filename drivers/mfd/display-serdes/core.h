/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * core.h -- core define for mfd display arch
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author: luowei <lw@rock-chips.com>
 *
 */

#ifndef __MFD_SERDES_CORE_H__
#define __MFD_SERDES_CORE_H__

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/regulator/consumer.h>
#include <linux/backlight.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>

#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/extcon-provider.h>
#include <linux/bitfield.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_of.h>
#include <drm/drm_connector.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_modes.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modeset_helper_vtables.h>

#include <video/videomode.h>
#include <video/of_display_timing.h>
#include <video/display_timing.h>
#include <uapi/linux/media-bus-format.h>

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>

#include <asm/unaligned.h>
#include "gpio.h"

#include "../../../../drivers/pinctrl/core.h"
#include "../../../../drivers/pinctrl/pinconf.h"
#include "../../../../drivers/pinctrl/pinmux.h"
#include "../../../../drivers/gpio/gpiolib.h"

/*
* if enable all the debug information,
* there will be much log.
*
* so suggest set CONFIG_LOG_BUF_SHIFT to 18
*/
//#define SERDES_DEBUG_MFD
//#define SERDES_DEBUG_I2C
//#define SERDES_DEBUG_CHIP

#ifdef SERDES_DEBUG_MFD
#define SERDES_DBG_MFD(x...) pr_info(x)
#else
#define SERDES_DBG_MFD(x...) no_printk(x)
#endif

#ifdef SERDES_DEBUG_I2C
#define SERDES_DBG_I2C(x...) pr_info(x)
#else
#define SERDES_DBG_I2C(x...) no_printk(x)
#endif

#ifdef SERDES_DEBUG_CHIP
#define SERDES_DBG_CHIP(x...) pr_info(x)
#else
#define SERDES_DBG_CHIP(x...) no_printk(x)
#endif

#define MFD_SERDES_DISPLAY_VERSION "serdes-mfd-displaly-v10-230901"

struct serdes;
struct serdes_chip_pinctrl_info {
	struct pinctrl_pin_desc *pins;
	unsigned int num_pins;
	struct group_desc *groups;
	unsigned int num_groups;
	struct function_desc *functions;
	unsigned int num_functions;
};

struct serdes_chip_bridge_ops {
	/* serdes chip function for bridge */
	int (*power_on)(struct serdes *serdes);
	int (*init)(struct serdes *serdes);
	int (*attach)(struct serdes *serdes);
	enum drm_connector_status (*detect)(struct serdes *serdes);
	int (*get_modes)(struct serdes *serdes);
	int (*pre_enable)(struct serdes *serdes);
	int (*enable)(struct serdes *serdes);
	int (*disable)(struct serdes *serdes);
	int (*post_disable)(struct serdes *serdes);
};

struct serdes_chip_panel_ops {
	/*serdes chip function for bridge*/
	int (*power_on)(struct serdes *serdes);
	int (*init)(struct serdes *serdes);
	int (*disable)(struct serdes *serdes);
	int (*unprepare)(struct serdes *serdes);
	int (*prepare)(struct serdes *serdes);
	int (*enable)(struct serdes *serdes);
	int (*get_modes)(struct serdes *serdes);
	int (*backlight_enable)(struct serdes *serdes);
	int (*backlight_disable)(struct serdes *serdes);
};

struct serdes_chip_pinctrl_ops {
	/* serdes chip pinctrl function */
	int (*pin_config_get)(struct serdes *serdes,
			      unsigned int pin,
			      unsigned long *config);
	int (*pin_config_set)(struct serdes *serdes,
			      unsigned int pin,
			      unsigned long *configs,
			      unsigned int num_configs);

	int (*set_mux)(struct serdes *serdes, unsigned int func_selector,
		       unsigned int group_selector);
};

struct serdes_chip_gpio_ops {
	/* serdes chip gpio function */
	int (*direction_input)(struct serdes *serdes, int gpio);
	int (*direction_output)(struct serdes *serdes, int gpio, int value);
	int (*get_level)(struct serdes *serdes, int gpio);
	int (*set_level)(struct serdes *serdes, int gpio, int value);
	int (*set_config)(struct serdes *serdes, int gpio, unsigned long config);
	int (*to_irq)(struct serdes *serdes, int gpio);
};

struct serdes_chip_pm_ops {
	/* serdes chip function for suspend and resume */
	int (*suspend)(struct serdes *serdes);
	int (*resume)(struct serdes *serdes);
};

struct serdes_chip_irq_ops {
	/* serdes chip function for lock and err irq */
	int (*lock_handle)(struct serdes *serdes);
	int (*err_handle)(struct serdes *serdes);
};

struct serdes_chip_data {
	const char *name;
	enum serdes_type serdes_type;
	enum serdes_id serdes_id;
	enum serdes_bridge_type bridge_type;
	int sequence_init;
	int connector_type;
	int reg_id;
	int id_data;
	int int_status_reg;
	int int_trig;
	int num_gpio;
	int gpio_base;
	int same_chip_count;
	u8 bank_num;

	struct regmap_config *regmap_config;
	struct serdes_chip_pinctrl_info *pinctrl_info;
	struct serdes_chip_bridge_ops *bridge_ops;
	struct serdes_chip_panel_ops *panel_ops;
	struct serdes_chip_pinctrl_ops *pinctrl_ops;
	struct serdes_chip_gpio_ops *gpio_ops;
	struct serdes_chip_pm_ops *pm_ops;
	struct serdes_chip_irq_ops *irq_ops;
};

struct serdes_init_seq {
	struct reg_sequence *reg_sequence;
	unsigned int reg_seq_cnt;
};

struct serdes_gpio {
	struct device *dev;
	struct serdes_pinctrl *parent;
	struct regmap *regmap;
	struct gpio_chip gpio_chip;
};

struct serdes_pinctrl {
	struct device *dev;
	struct serdes *parent;
	struct pinctrl_dev *pctl;
	struct pinctrl_pin_desc *pdesc;
	struct regmap *regmap;
	struct pinctrl_desc *pinctrl_desc;
	struct serdes_gpio *gpio;
	int pin_base;
};

struct serdes_panel {
	struct drm_panel panel;
	enum drm_connector_status status;
	struct drm_connector connector;

	const char *name;
	u32 width_mm;
	u32 height_mm;
	u32 link_rate;
	u32 lane_count;
	bool ssc;

	struct device *dev;
	struct serdes *parent;
	struct regmap *regmap;
	struct mipi_dsi_device *dsi;
	struct device_node *dsi_node;
	struct drm_display_mode mode;
	struct backlight_device *backlight;
	struct serdes_init_seq *serdes_init_seq;
	bool sel_mipi;
	bool dv_swp_ab;
	bool dpi_deskew_en;
	bool split_mode;
	u32 num_lanes;
	u32 dsi_lane_map[4];
};

struct serdes_bridge {
	struct drm_bridge base_bridge;
	struct drm_bridge *next_bridge;
	enum drm_connector_status status;
	atomic_t triggered;
	struct drm_connector connector;
	struct drm_panel *panel;

	struct device *dev;
	struct serdes *parent;
	struct regmap *regmap;
	struct mipi_dsi_device *dsi;
	struct device_node *dsi_node;
	struct drm_display_mode mode;
	struct backlight_device *backlight;

	bool sel_mipi;
	bool dv_swp_ab;
	bool dpi_deskew_en;
	bool split_mode;
	u32 num_lanes;
	u32 dsi_lane_map[4];
};

struct serdes {
	int num_gpio;
	struct mutex io_lock;
	struct mutex irq_lock;
	struct mutex wq_lock;
	struct device *dev;
	enum serdes_type type;
	struct regmap *regmap;
	struct regulator *supply;
	struct extcon_dev *extcon;
	atomic_t conn_trigger;

	/* serdes power and reset pin */
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	struct regulator *vpower;

	/* serdes irq pin */
	struct gpio_desc *lock_gpio;
	struct gpio_desc *err_gpio;
	int lock_irq;
	int err_irq;
	int lock_irq_trig;
	int err_irq_trig;
	atomic_t flag_ser_init;

	struct workqueue_struct *mfd_wq;
	struct delayed_work mfd_delay_work;
	bool route_enable;
	bool use_delay_work;
	struct pinctrl *pinctrl_node;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_sleep;

	struct serdes_init_seq *serdes_init_seq;
	struct serdes_bridge *serdes_bridge;
	struct serdes_panel *serdes_panel;
	struct serdes_pinctrl *pinctrl;
	struct serdes_chip_data *chip_data;
};

/* Device I/O API */
int serdes_reg_read(struct serdes *serdes, unsigned int reg, unsigned int *val);
int serdes_reg_write(struct serdes *serdes, unsigned int reg, unsigned int val);
void serdes_reg_lock(struct serdes *serdes);
int serdes_reg_unlock(struct serdes *serdes);
int serdes_set_bits(struct serdes *serdes, unsigned int reg,
		    unsigned int mask, unsigned int val);
int serdes_bulk_read(struct serdes *serdes, unsigned int reg,
		     int count, u16 *buf);
int serdes_bulk_write(struct serdes *serdes, unsigned int reg,
		      int count, void *src);
int serdes_multi_reg_write(struct serdes *serdes, const struct reg_sequence *regs,
			   int num_regs);
int serdes_i2c_set_sequence(struct serdes *serdes);

int serdes_device_init(struct serdes *serdes);
int serdes_set_pinctrl_default(struct serdes *serdes);
int serdes_set_pinctrl_sleep(struct serdes *serdes);
int serdes_device_suspend(struct serdes *serdes);
int serdes_device_resume(struct serdes *serdes);
void serdes_device_poweroff(struct serdes *serdes);
int serdes_device_shutdown(struct serdes *serdes);
int serdes_irq_init(struct serdes *serdes);
void serdes_irq_exit(struct serdes *serdes);
void serdes_auxadc_init(struct serdes *serdes);

extern struct serdes_chip_data serdes_bu18tl82_data;
extern struct serdes_chip_data serdes_bu18rl82_data;
extern struct serdes_chip_data serdes_max96745_data;
extern struct serdes_chip_data serdes_max96752_data;
extern struct serdes_chip_data serdes_max96755_data;
extern struct serdes_chip_data serdes_max96772_data;
extern struct serdes_chip_data serdes_max96789_data;
extern struct serdes_chip_data serdes_rkx111_data;
extern struct serdes_chip_data serdes_rkx121_data;
extern struct serdes_chip_data serdes_nca9539_data;

#endif
