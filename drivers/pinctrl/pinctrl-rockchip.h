/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021 Rockchip Electronics Co. Ltd.
 *
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * With some ideas taken from pinctrl-samsung:
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2012 Linaro Ltd
 *		https://www.linaro.org
 *
 * and pinctrl-at91:
 * Copyright (C) 2011-2012 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 */

#ifndef _PINCTRL_ROCKCHIP_H
#define _PINCTRL_ROCKCHIP_H

enum rockchip_pinctrl_type {
	PX30,
	RV1108,
	RK2928,
	RK3066B,
	RK3128,
	RK3188,
	RK3288,
	RK3308,
	RK3368,
	RK3399,
	RK3568,
};

/**
 * struct rockchip_gpio_regs
 * @port_dr: data register
 * @port_ddr: data direction register
 * @int_en: interrupt enable
 * @int_mask: interrupt mask
 * @int_type: interrupt trigger type, such as high, low, edge trriger type.
 * @int_polarity: interrupt polarity enable register
 * @int_bothedge: interrupt bothedge enable register
 * @int_status: interrupt status register
 * @int_rawstatus: int_status = int_rawstatus & int_mask
 * @debounce: enable debounce for interrupt signal
 * @dbclk_div_en: enable divider for debounce clock
 * @dbclk_div_con: setting for divider of debounce clock
 * @port_eoi: end of interrupt of the port
 * @ext_port: port data from external
 * @version_id: controller version register
 */
struct rockchip_gpio_regs {
	u32 port_dr;
	u32 port_ddr;
	u32 int_en;
	u32 int_mask;
	u32 int_type;
	u32 int_polarity;
	u32 int_bothedge;
	u32 int_status;
	u32 int_rawstatus;
	u32 debounce;
	u32 dbclk_div_en;
	u32 dbclk_div_con;
	u32 port_eoi;
	u32 ext_port;
	u32 version_id;
};

/**
 * struct rockchip_iomux
 * @type: iomux variant using IOMUX_* constants
 * @offset: if initialized to -1 it will be autocalculated, by specifying
 *	    an initial offset value the relevant source offset can be reset
 *	    to a new value for autocalculating the following iomux registers.
 */
struct rockchip_iomux {
	int type;
	int offset;
};

/*
 * enum type index corresponding to rockchip_perpin_drv_list arrays index.
 */
enum rockchip_pin_drv_type {
	DRV_TYPE_IO_DEFAULT = 0,
	DRV_TYPE_IO_1V8_OR_3V0,
	DRV_TYPE_IO_1V8_ONLY,
	DRV_TYPE_IO_1V8_3V0_AUTO,
	DRV_TYPE_IO_3V3_ONLY,
	DRV_TYPE_MAX
};

/*
 * enum type index corresponding to rockchip_pull_list arrays index.
 */
enum rockchip_pin_pull_type {
	PULL_TYPE_IO_DEFAULT = 0,
	PULL_TYPE_IO_1V8_ONLY,
	PULL_TYPE_MAX
};

/**
 * struct rockchip_drv
 * @drv_type: drive strength variant using rockchip_perpin_drv_type
 * @offset: if initialized to -1 it will be autocalculated, by specifying
 *	    an initial offset value the relevant source offset can be reset
 *	    to a new value for autocalculating the following drive strength
 *	    registers. if used chips own cal_drv func instead to calculate
 *	    registers offset, the variant could be ignored.
 */
struct rockchip_drv {
	enum rockchip_pin_drv_type	drv_type;
	int				offset;
};

/**
 * struct rockchip_pin_bank
 * @dev: the pinctrl device bind to the bank
 * @reg_base: register base of the gpio bank
 * @regmap_pull: optional separate register for additional pull settings
 * @clk: clock of the gpio bank
 * @db_clk: clock of the gpio debounce
 * @irq: interrupt of the gpio bank
 * @saved_masks: Saved content of GPIO_INTEN at suspend time.
 * @pin_base: first pin number
 * @nr_pins: number of pins in this bank
 * @name: name of the bank
 * @bank_num: number of the bank, to account for holes
 * @iomux: array describing the 4 iomux sources of the bank
 * @drv: array describing the 4 drive strength sources of the bank
 * @pull_type: array describing the 4 pull type sources of the bank
 * @valid: is all necessary information present
 * @of_node: dt node of this bank
 * @drvdata: common pinctrl basedata
 * @domain: irqdomain of the gpio bank
 * @gpio_chip: gpiolib chip
 * @grange: gpio range
 * @slock: spinlock for the gpio bank
 * @toggle_edge_mode: bit mask to toggle (falling/rising) edge mode
 * @recalced_mask: bit mask to indicate a need to recalulate the mask
 * @route_mask: bits describing the routing pins of per bank
 * @deferred_output: gpio output settings to be done after gpio bank probed
 * @deferred_lock: mutex for the deferred_output shared btw gpio and pinctrl
 */
struct rockchip_pin_bank {
	struct device			*dev;
	void __iomem			*reg_base;
	struct regmap			*regmap_pull;
	struct clk			*clk;
	struct clk			*db_clk;
	int				irq;
	u32				saved_masks;
	u32				pin_base;
	u8				nr_pins;
	char				*name;
	u8				bank_num;
	struct rockchip_iomux		iomux[4];
	struct rockchip_drv		drv[4];
	enum rockchip_pin_pull_type	pull_type[4];
	bool				valid;
	struct device_node		*of_node;
	struct rockchip_pinctrl		*drvdata;
	struct irq_domain		*domain;
	struct gpio_chip		gpio_chip;
	struct pinctrl_gpio_range	grange;
	raw_spinlock_t			slock;
	const struct rockchip_gpio_regs	*gpio_regs;
	u32				gpio_type;
	u32				toggle_edge_mode;
	u32				recalced_mask;
	u32				route_mask;
	struct list_head		deferred_output;
	struct mutex			deferred_lock;
};

/**
 * struct rockchip_mux_recalced_data: represent a pin iomux data.
 * @num: bank number.
 * @pin: pin number.
 * @bit: index at register.
 * @reg: register offset.
 * @mask: mask bit
 */
struct rockchip_mux_recalced_data {
	u8 num;
	u8 pin;
	u32 reg;
	u8 bit;
	u8 mask;
};

enum rockchip_mux_route_location {
	ROCKCHIP_ROUTE_SAME = 0,
	ROCKCHIP_ROUTE_PMU,
	ROCKCHIP_ROUTE_GRF,
};

/**
 * struct rockchip_mux_recalced_data: represent a pin iomux data.
 * @bank_num: bank number.
 * @pin: index at register or used to calc index.
 * @func: the min pin.
 * @route_location: the mux route location (same, pmu, grf).
 * @route_offset: the max pin.
 * @route_val: the register offset.
 */
struct rockchip_mux_route_data {
	u8 bank_num;
	u8 pin;
	u8 func;
	enum rockchip_mux_route_location route_location;
	u32 route_offset;
	u32 route_val;
};

struct rockchip_pin_ctrl {
	struct rockchip_pin_bank	*pin_banks;
	u32				nr_banks;
	u32				nr_pins;
	char				*label;
	enum rockchip_pinctrl_type	type;
	int				grf_mux_offset;
	int				pmu_mux_offset;
	int				grf_drv_offset;
	int				pmu_drv_offset;
	struct rockchip_mux_recalced_data *iomux_recalced;
	u32				niomux_recalced;
	struct rockchip_mux_route_data *iomux_routes;
	u32				niomux_routes;

	void	(*pull_calc_reg)(struct rockchip_pin_bank *bank,
				    int pin_num, struct regmap **regmap,
				    int *reg, u8 *bit);
	void	(*drv_calc_reg)(struct rockchip_pin_bank *bank,
				    int pin_num, struct regmap **regmap,
				    int *reg, u8 *bit);
	int	(*schmitt_calc_reg)(struct rockchip_pin_bank *bank,
				    int pin_num, struct regmap **regmap,
				    int *reg, u8 *bit);
};

struct rockchip_pin_config {
	unsigned int		func;
	unsigned long		*configs;
	unsigned int		nconfigs;
};

struct rockchip_pin_output_deferred {
	struct list_head head;
	unsigned int pin;
	u32 arg;
};

/**
 * struct rockchip_pin_group: represent group of pins of a pinmux function.
 * @name: name of the pin group, used to lookup the group.
 * @pins: the pins included in this group.
 * @npins: number of pins included in this group.
 * @data: local pin configuration
 */
struct rockchip_pin_group {
	const char			*name;
	unsigned int			npins;
	unsigned int			*pins;
	struct rockchip_pin_config	*data;
};

/**
 * struct rockchip_pmx_func: represent a pin function.
 * @name: name of the pin function, used to lookup the function.
 * @groups: one or more names of pin groups that provide this function.
 * @ngroups: number of groups included in @groups.
 */
struct rockchip_pmx_func {
	const char		*name;
	const char		**groups;
	u8			ngroups;
};

struct rockchip_pinctrl {
	struct regmap			*regmap_base;
	int				reg_size;
	struct regmap			*regmap_pull;
	struct regmap			*regmap_pmu;
	struct device			*dev;
	struct rockchip_pin_ctrl	*ctrl;
	struct pinctrl_desc		pctl;
	struct pinctrl_dev		*pctl_dev;
	struct rockchip_pin_group	*groups;
	unsigned int			ngroups;
	struct rockchip_pmx_func	*functions;
	unsigned int			nfunctions;
};

#endif
