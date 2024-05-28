/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2023 Realtek Semiconductor Corp.
 */

#define NA 0xffffffff
#define PADDRI_4_8 1
#define PADDRI_2_4 0

struct rtd_pin_group_desc {
	const char *name;
	const unsigned int *pins;
	unsigned int num_pins;
};

struct rtd_pin_func_desc {
	const char *name;
	const char * const *groups;
	unsigned int num_groups;
};

struct rtd_pin_mux_desc {
	const char *name;
	u32 mux_value;
};

struct rtd_pin_config_desc {
	const char *name;
	unsigned int reg_offset;
	unsigned int base_bit;
	unsigned int pud_en_offset;
	unsigned int pud_sel_offset;
	unsigned int curr_offset;
	unsigned int smt_offset;
	unsigned int power_offset;
	unsigned int curr_type;
};

struct rtd_pin_sconfig_desc {
	const char *name;
	unsigned int reg_offset;
	unsigned int dcycle_offset;
	unsigned int dcycle_maskbits;
	unsigned int ndrive_offset;
	unsigned int ndrive_maskbits;
	unsigned int pdrive_offset;
	unsigned int pdrive_maskbits;
};

struct rtd_pin_desc {
	const char *name;
	unsigned int mux_offset;
	u32 mux_mask;
	const struct rtd_pin_mux_desc *functions;
};

struct rtd_pin_reg_list {
	unsigned int reg_offset;
	unsigned int val;
};

#define SHIFT_LEFT(_val, _shift) ((_val) << (_shift))

#define RTK_PIN_MUX(_name, _mux_off, _mux_mask, ...) \
	{ \
		.name = # _name, \
		.mux_offset = _mux_off, \
		.mux_mask = _mux_mask, \
		.functions = (const struct rtd_pin_mux_desc []) { \
			__VA_ARGS__, { } \
		}, \
	}

#define RTK_PIN_CONFIG(_name, _reg_off, _base_bit, _pud_en_off, \
		       _pud_sel_off, _curr_off, _smt_off, _pow_off, _curr_type) \
	{ \
		.name = # _name, \
		.reg_offset = _reg_off, \
		.base_bit = _base_bit, \
		.pud_en_offset = _pud_en_off, \
		.pud_sel_offset = _pud_sel_off, \
		.curr_offset = _curr_off, \
		.smt_offset = _smt_off, \
		.power_offset = _pow_off, \
		.curr_type = _curr_type, \
	}

#define RTK_PIN_SCONFIG(_name, _reg_off, _d_offset, _d_mask, \
			_n_offset, _n_mask, _p_offset, _p_mask) \
	{ \
		.name = # _name, \
		.reg_offset = _reg_off, \
		.dcycle_offset = _d_offset, \
		.dcycle_maskbits = _d_mask, \
		.ndrive_offset = _n_offset, \
		.ndrive_maskbits = _n_mask, \
		.pdrive_offset = _p_offset, \
		.pdrive_maskbits = _p_mask, \
	}

#define RTK_PIN_FUNC(_mux_val, _name) \
	{ \
		.name = _name, \
		.mux_value = _mux_val, \
	}

struct rtd_pinctrl_desc {
	const struct pinctrl_pin_desc *pins;
	unsigned int num_pins;
	const struct rtd_pin_group_desc *groups;
	unsigned int num_groups;
	const struct rtd_pin_func_desc *functions;
	unsigned int num_functions;
	const struct rtd_pin_desc *muxes;
	unsigned int num_muxes;
	const struct rtd_pin_config_desc *configs;
	unsigned int num_configs;
	const struct rtd_pin_sconfig_desc *sconfigs;
	unsigned int num_sconfigs;
	struct rtd_pin_reg_list *lists;
	unsigned int num_regs;
};

int rtd_pinctrl_probe(struct platform_device *pdev, const struct rtd_pinctrl_desc *desc);
