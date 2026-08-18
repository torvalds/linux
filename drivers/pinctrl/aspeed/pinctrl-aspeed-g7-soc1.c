// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pinctrl driver for Aspeed G7 SoC1
 *
 * Copyright (C) 2026 Aspeed Technology Inc.
 */

#include <linux/errno.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinctrl-utils.h"
#include "../pinmux.h"

#define ASPEED_G7_SOC1_NR_PINS 220
#define ASPEED_G7_SOC1_REG_WIDTH 32
#define ASPEED_G7_SOC1_REG_STRIDE 4

#define ASPEED_G7_SOC1_MUX_BASE          0x400
#define ASPEED_G7_SOC1_BIAS_BASE         0x480
#define ASPEED_G7_SOC1_DRV_BASE          0x4C0
#define ASPEED_G7_SOC1_PCIE_REG          0x908
#define ASPEED_G7_SOC1_USB_MODE_REG      0x3B0
#define ASPEED_G7_SOC1_SGMII_REG         0x47C

/*
 * Each pin occupies a 4-bit slot in the MUX registers (MUX_BITS_PER_PIN),
 * but only bits [2:0] select the mux function; bit 3 is reserved read-only
 * and must not be written.  MUX_FUNC_MASK therefore covers 3 bits, not 4.
 */
#define ASPEED_G7_SOC1_MUX_FUNC_MASK 0x7
#define ASPEED_G7_SOC1_MUX_BITS_PER_PIN 4
#define ASPEED_G7_SOC1_MUX_PINS_PER_REG \
	(ASPEED_G7_SOC1_REG_WIDTH / ASPEED_G7_SOC1_MUX_BITS_PER_PIN)

#define ASPEED_G7_SOC1_BIAS_FUNC_MASK 0x1
#define ASPEED_G7_SOC1_BIAS_BITS_PER_PIN 1
#define ASPEED_G7_SOC1_BIAS_PINS_PER_REG \
	(ASPEED_G7_SOC1_REG_WIDTH / ASPEED_G7_SOC1_BIAS_BITS_PER_PIN)

#define ASPEED_G7_SOC1_DRV_FUNC_MASK 0x3
#define ASPEED_G7_SOC1_DRV_BITS_PER_PIN 2
#define ASPEED_G7_SOC1_DRV_PINS_PER_REG \
	(ASPEED_G7_SOC1_REG_WIDTH / ASPEED_G7_SOC1_DRV_BITS_PER_PIN)

#define ASPEED_G7_SOC1_DRV_STRENGTH_STEP_MA 4
#define ASPEED_G7_SOC1_DRV_STRENGTH_HW_BASE 1
#define ASPEED_G7_SOC1_DRV_STRENGTH_MIN_MA \
	(ASPEED_G7_SOC1_DRV_STRENGTH_HW_BASE * ASPEED_G7_SOC1_DRV_STRENGTH_STEP_MA)
#define ASPEED_G7_SOC1_DRV_STRENGTH_MAX_MA \
	((ASPEED_G7_SOC1_DRV_FUNC_MASK + ASPEED_G7_SOC1_DRV_STRENGTH_HW_BASE) * \
	 ASPEED_G7_SOC1_DRV_STRENGTH_STEP_MA)

/*
 * NOTE: The numeric values of these enum entries are significant.
 * They must match the SoC GPIO numbering / ball-to-GPIO ID mapping.
 * Do not reorder alphabetically.
 */
enum {
	C16,
	C14,
	C11,
	D9,
	F14,
	D10,
	C12,
	C13,
	AC26,
	AA25,
	AB23,
	U22,
	V21,
	N26,
	P25,
	N25,
	V23,
	W22,
	AB26,
	AD26,
	P26,
	AE26,
	AF26,
	AF25,
	AE25,
	AD25,
	AF23,
	AF20,
	AF21,
	AE21,
	AE23,
	AD22,
	AF17,
	AA16,
	Y16,
	V17,
	J13,
	AB16,
	AC16,
	AF16,
	AA15,
	AB15,
	AC15,
	AD15,
	Y15,
	AA14,
	W16,
	V16,
	AB18,
	AC18,
	K13,
	AA17,
	AB17,
	AD16,
	AC17,
	AD17,
	AE16,
	AE17,
	AB24,
	W26,
	HOLE0,
	HOLE1,
	HOLE2,
	HOLE3,
	W25,
	Y23,
	Y24,
	W21,
	AA23,
	AC22,
	AB22,
	Y21,
	AE20,
	AF19,
	Y22,
	AA20,
	AA22,
	AB20,
	AF18,
	AE19,
	AD20,
	AC20,
	AA21,
	AB21,
	AC19,
	AE18,
	AD19,
	AD18,
	U25,
	U26,
	Y26,
	AA24,
	R25,
	AA26,
	R26,
	Y25,
	B16,
	D14,
	B15,
	B14,
	C17,
	B13,
	E14,
	C15,
	D24,
	B23,
	B22,
	C23,
	B18,
	B21,
	M15,
	B19,
	B26,
	A25,
	A24,
	B24,
	E26,
	A21,
	A19,
	A18,
	D26,
	C26,
	A23,
	A22,
	B25,
	F26,
	A26,
	A14,
	E10,
	E13,
	D12,
	F10,
	E11,
	F11,
	F13,
	N15,
	C20,
	C19,
	A8,
	R14,
	A7,
	P14,
	D20,
	A6,
	B6,
	N14,
	B7,
	B8,
	B9,
	M14,
	J11,
	E7,
	D19,
	B11,
	D15,
	B12,
	B10,
	P13,
	C18,
	C6,
	C7,
	D7,
	N13,
	C8,
	C9,
	C10,
	M16,
	A15,
	G11,
	H7,
	H8,
	H9,
	H10,
	H11,
	J9,
	J10,
	E9,
	F9,
	F8,
	M13,
	F7,
	D8,
	E8,
	L12,
	F12,
	E12,
	J12,
	G7,
	G8,
	G9,
	G10,
	K12,
	W17,
	V18,
	W18,
	Y17,
	AA18,
	AA13,
	Y18,
	AA12,
	W20,
	V20,
	Y11,
	V14,
	V19,
	W14,
	Y20,
	AB19,
	U21,
	T24,
	V24,
	V22,
	T23,
	AC25,
	AB25,
	AC24,
	PCIERC2_PERST,
	PORTC_MODE,
	PORTD_MODE,
	SGMII0,
};

struct aspeed_g7_soc1_pinctrl {
	struct device *dev;
	struct regmap *regmap;
	struct pinctrl_dev *pctl;
};

struct aspeed_g7_field {
	unsigned int reg;
	unsigned int shift;
	unsigned int mask;
};

static struct aspeed_g7_field
aspeed_g7_soc1_pinmux_field_from_pin(unsigned int pin)
{
	return (struct aspeed_g7_field){
		.reg = ASPEED_G7_SOC1_MUX_BASE +
		       (pin / ASPEED_G7_SOC1_MUX_PINS_PER_REG) *
			       ASPEED_G7_SOC1_REG_STRIDE,
		.shift = (pin % ASPEED_G7_SOC1_MUX_PINS_PER_REG) *
			 ASPEED_G7_SOC1_MUX_BITS_PER_PIN,
		.mask = ASPEED_G7_SOC1_MUX_FUNC_MASK,
	};
}

static struct aspeed_g7_field
aspeed_g7_soc1_bias_field_from_pin(unsigned int pin)
{
	return (struct aspeed_g7_field){
		.reg = ASPEED_G7_SOC1_BIAS_BASE +
		       (pin / ASPEED_G7_SOC1_BIAS_PINS_PER_REG) *
			       ASPEED_G7_SOC1_REG_STRIDE,
		.shift = pin % ASPEED_G7_SOC1_BIAS_PINS_PER_REG,
		.mask = ASPEED_G7_SOC1_BIAS_FUNC_MASK,
	};
}

static struct aspeed_g7_field
aspeed_g7_soc1_drv_field_from_idx(unsigned int idx)
{
	return (struct aspeed_g7_field){
		.reg = ASPEED_G7_SOC1_DRV_BASE +
		       (idx / ASPEED_G7_SOC1_DRV_PINS_PER_REG) *
			       ASPEED_G7_SOC1_REG_STRIDE,
		.shift = (idx % ASPEED_G7_SOC1_DRV_PINS_PER_REG) *
			 ASPEED_G7_SOC1_DRV_BITS_PER_PIN,
		.mask = ASPEED_G7_SOC1_DRV_FUNC_MASK,
	};
}

#define PIN(n) PINCTRL_PIN(n, #n)

static const struct pinctrl_pin_desc aspeed_g7_soc1_pins[] = {
	PIN(C16),
	PIN(C14),
	PIN(C11),
	PIN(D9),
	PIN(F14),
	PIN(D10),
	PIN(C12),
	PIN(C13),
	PIN(AC26),
	PIN(AA25),
	PIN(AB23),
	PIN(U22),
	PIN(V21),
	PIN(N26),
	PIN(P25),
	PIN(N25),
	PIN(V23),
	PIN(W22),
	PIN(AB26),
	PIN(AD26),
	PIN(P26),
	PIN(AE26),
	PIN(AF26),
	PIN(AF25),
	PIN(AE25),
	PIN(AD25),
	PIN(AF23),
	PIN(AF20),
	PIN(AF21),
	PIN(AE21),
	PIN(AE23),
	PIN(AD22),
	PIN(AF17),
	PIN(AA16),
	PIN(Y16),
	PIN(V17),
	PIN(J13),
	PIN(AB16),
	PIN(AC16),
	PIN(AF16),
	PIN(AA15),
	PIN(AB15),
	PIN(AC15),
	PIN(AD15),
	PIN(Y15),
	PIN(AA14),
	PIN(W16),
	PIN(V16),
	PIN(AB18),
	PIN(AC18),
	PIN(K13),
	PIN(AA17),
	PIN(AB17),
	PIN(AD16),
	PIN(AC17),
	PIN(AD17),
	PIN(AE16),
	PIN(AE17),
	PIN(AB24),
	PIN(W26),
	PIN(HOLE0),
	PIN(HOLE1),
	PIN(HOLE2),
	PIN(HOLE3),
	PIN(W25),
	PIN(Y23),
	PIN(Y24),
	PIN(W21),
	PIN(AA23),
	PIN(AC22),
	PIN(AB22),
	PIN(Y21),
	PIN(AE20),
	PIN(AF19),
	PIN(Y22),
	PIN(AA20),
	PIN(AA22),
	PIN(AB20),
	PIN(AF18),
	PIN(AE19),
	PIN(AD20),
	PIN(AC20),
	PIN(AA21),
	PIN(AB21),
	PIN(AC19),
	PIN(AE18),
	PIN(AD19),
	PIN(AD18),
	PIN(U25),
	PIN(U26),
	PIN(Y26),
	PIN(AA24),
	PIN(R25),
	PIN(AA26),
	PIN(R26),
	PIN(Y25),
	PIN(B16),
	PIN(D14),
	PIN(B15),
	PIN(B14),
	PIN(C17),
	PIN(B13),
	PIN(E14),
	PIN(C15),
	PIN(D24),
	PIN(B23),
	PIN(B22),
	PIN(C23),
	PIN(B18),
	PIN(B21),
	PIN(M15),
	PIN(B19),
	PIN(B26),
	PIN(A25),
	PIN(A24),
	PIN(B24),
	PIN(E26),
	PIN(A21),
	PIN(A19),
	PIN(A18),
	PIN(D26),
	PIN(C26),
	PIN(A23),
	PIN(A22),
	PIN(B25),
	PIN(F26),
	PIN(A26),
	PIN(A14),
	PIN(E10),
	PIN(E13),
	PIN(D12),
	PIN(F10),
	PIN(E11),
	PIN(F11),
	PIN(F13),
	PIN(N15),
	PIN(C20),
	PIN(C19),
	PIN(A8),
	PIN(R14),
	PIN(A7),
	PIN(P14),
	PIN(D20),
	PIN(A6),
	PIN(B6),
	PIN(N14),
	PIN(B7),
	PIN(B8),
	PIN(B9),
	PIN(M14),
	PIN(J11),
	PIN(E7),
	PIN(D19),
	PIN(B11),
	PIN(D15),
	PIN(B12),
	PIN(B10),
	PIN(P13),
	PIN(C18),
	PIN(C6),
	PIN(C7),
	PIN(D7),
	PIN(N13),
	PIN(C8),
	PIN(C9),
	PIN(C10),
	PIN(M16),
	PIN(A15),
	PIN(G11),
	PIN(H7),
	PIN(H8),
	PIN(H9),
	PIN(H10),
	PIN(H11),
	PIN(J9),
	PIN(J10),
	PIN(E9),
	PIN(F9),
	PIN(F8),
	PIN(M13),
	PIN(F7),
	PIN(D8),
	PIN(E8),
	PIN(L12),
	PIN(F12),
	PIN(E12),
	PIN(J12),
	PIN(G7),
	PIN(G8),
	PIN(G9),
	PIN(G10),
	PIN(K12),
	PIN(W17),
	PIN(V18),
	PIN(W18),
	PIN(Y17),
	PIN(AA18),
	PIN(AA13),
	PIN(Y18),
	PIN(AA12),
	PIN(W20),
	PIN(V20),
	PIN(Y11),
	PIN(V14),
	PIN(V19),
	PIN(W14),
	PIN(Y20),
	PIN(AB19),
	PIN(U21),
	PIN(T24),
	PIN(V24),
	PIN(V22),
	PIN(T23),
	PIN(AC25),
	PIN(AB25),
	PIN(AC24),
	PIN(PCIERC2_PERST),
	PIN(PORTC_MODE),
	PIN(PORTD_MODE),
	PIN(SGMII0),
};

static const struct pinctrl_ops aspeed_g7_soc1_pctl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinctrl_utils_free_map,
};

struct aspeed_g7_soc1_function {
	struct pinfunction pinfunction;
	const u8 *muxvals;
};

static int aspeed_g7_soc1_drive_strength_to_hw(u32 strength,
					       unsigned int *val)
{
	if (strength < ASPEED_G7_SOC1_DRV_STRENGTH_MIN_MA ||
	    strength > ASPEED_G7_SOC1_DRV_STRENGTH_MAX_MA ||
	    strength % ASPEED_G7_SOC1_DRV_STRENGTH_STEP_MA)
		return -EINVAL;

	*val = (strength / ASPEED_G7_SOC1_DRV_STRENGTH_STEP_MA) -
	       ASPEED_G7_SOC1_DRV_STRENGTH_HW_BASE;

	return 0;
}

static int aspeed_g7_soc1_set_mux(struct pinctrl_dev *pctldev,
				  unsigned int fselector, unsigned int group)
{
	struct aspeed_g7_soc1_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	const struct aspeed_g7_soc1_function *soc1_func;
	const struct function_desc *fd;
	const struct pinfunction *func;
	const struct pingroup *grp;
	struct group_desc *gd;
	const char *gname;
	int i, g_idx = -1, ret;

	gd = pinctrl_generic_get_group(pctldev, group);
	if (!gd)
		return -EINVAL;

	grp = &gd->grp;

	fd = pinmux_generic_get_function(pctldev, fselector);
	if (!fd)
		return -EINVAL;

	soc1_func = fd->data;
	if (!soc1_func)
		return -EINVAL;

	func = &soc1_func->pinfunction;
	gname = grp->name;

	for (i = 0; i < func->ngroups; i++) {
		if (!strcmp(gname, func->groups[i])) {
			g_idx = i;
			break;
		}
	}

	if (g_idx < 0)
		return -EINVAL;

	for (i = 0; i < grp->npins; i++) {
		unsigned int val = soc1_func->muxvals[g_idx];
		unsigned int pin = grp->pins[i];
		struct aspeed_g7_field field;

		if (pin == PCIERC2_PERST) {
			/*
			 * PCIERC2_PERST is a special case: it is managed by a
			 * dedicated control register (0x908) instead of the
			 * standard 4-bit multi-function field.
			 */
			field.reg = ASPEED_G7_SOC1_PCIE_REG;
			field.shift = 0;
			field.mask = 0x1;
			val = 1;
		} else if (pin == PORTC_MODE || pin == PORTD_MODE) {
			/*
			 * PORTC_MODE and PORTD_MODE are virtual "pins" that
			 * control the USB 2.0 controller mode settings.
			 * These reside in a specific control register (0x3B0)
			 * with non-standard bit widths.
			 */
			field.reg = ASPEED_G7_SOC1_USB_MODE_REG;
			field.mask = 0x3;
			field.shift = pin == PORTC_MODE ? 0 : 2;
		} else if (pin == SGMII0) {
			/*
			 * SGMII0 is a virtual pin whose mux control resides at
			 * SCU47C bit 0, outside the contiguous pin-indexed MUX
			 * register range starting at MUX_BASE.  The field is
			 * 1 bit wide; use a 1-bit mask to avoid clobbering
			 * adjacent bits in SCU47C.
			 */
			field.reg = ASPEED_G7_SOC1_SGMII_REG;
			field.shift = 0;
			field.mask = 0x1;
		} else {
			/* Standard 4-bit-per-pin multi-function configuration */
			field = aspeed_g7_soc1_pinmux_field_from_pin(pin);
		}

		dev_dbg(pctl->dev,
			"Setting pin %u reg 0x%x shift %u to function %s (muxval=0x%x)\n",
			pin, field.reg, field.shift, func->name, val);

		ret = regmap_update_bits(pctl->regmap, field.reg,
					 field.mask << field.shift,
					 val << field.shift);
		if (ret)
			return ret;
	}

	return 0;
}

static int aspeed_g7_soc1_gpio_request_enable(struct pinctrl_dev *pctldev,
					      struct pinctrl_gpio_range *range,
					      unsigned int pin)
{
	struct aspeed_g7_soc1_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct aspeed_g7_field field;
	unsigned int val = 0;
	int ret = -ENOTSUPP;

	if (pin <= AC24) {
		/*
		 * Balls W17 through AB19 are the ADC-capable pins: mux
		 * function 0 selects the ADC input and function 1 selects
		 * GPIO, unlike all other pins where function 0 is GPIO.
		 */
		if (pin >= W17 && pin <= AB19)
			val = 1;
		field = aspeed_g7_soc1_pinmux_field_from_pin(pin);
		ret = regmap_update_bits(pctl->regmap, field.reg,
					 field.mask << field.shift,
					 val << field.shift);
	}

	return ret;
}

static const struct pinmux_ops aspeed_g7_soc1_pmx_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = aspeed_g7_soc1_set_mux,
	.gpio_request_enable = aspeed_g7_soc1_gpio_request_enable,
	.strict = true,
};

/*
 * aspeed_g7_soc1_drv_map - Mapping table for pin drive strength control.
 *
 * In AST2700 SOC1, drive strength configuration is architecturally decoupled
 * from the main pin mux registers (0x400 range). It is managed by a separate
 * set of registers starting at 0x4C0.
 *
 * This table is required because:
 * 1. The mapping between physical pin IDs and drive strength control slots
 *    is non-linear and sparse.
 *    For example, W25 maps to field index 8 (stored as 9),
 *    meaning it occupies bits [17:16] of the first 0x4C0 register.
 * 2. Only a subset of physical pins supports drive strength configuration.
 *
 * The table stores (drive strength field index + 1).
 * The field index refers to the 2-bit drive strength field position within the
 * 0x4C0 register range. A value of 0 indicates that the pin does not support
 * drive strength configuration (returning -ENOTSUPP).
 * This +1 offset allows us to rely on C's default zero-initialization for
 * unsupported pins while avoiding compiler warnings regarding overridden
 * initializers.
 */
static const int aspeed_g7_soc1_drv_map[ASPEED_G7_SOC1_NR_PINS] = {
	[C16] = 1,   [C14] = 2,	  [C11] = 3,   [D9] = 4,    [F14] = 5,	 [D10] = 6,   [C12] = 7,
	[C13] = 8,   [W25] = 9,	  [Y23] = 10,  [Y24] = 11,  [W21] = 12,	 [AA23] = 13, [AC22] = 14,
	[AB22] = 15, [Y21] = 16,  [AE20] = 17, [AF19] = 18, [Y22] = 19,	 [AA20] = 20, [AA22] = 21,
	[AB20] = 22, [AF18] = 23, [AE19] = 24, [AD20] = 25, [AC20] = 26, [AA21] = 27, [AB21] = 28,
	[AC19] = 29, [AE18] = 30, [AD19] = 31, [AD18] = 32, [U25] = 33,	 [U26] = 34,  [Y26] = 35,
	[AA24] = 36, [R25] = 37,  [AA26] = 38, [R26] = 39,  [Y25] = 40,	 [B16] = 41,  [D14] = 42,
	[B15] = 43,  [B14] = 44,  [C17] = 45,  [B13] = 46,  [E14] = 47,	 [C15] = 48,  [D24] = 49,
	[B23] = 50,  [B22] = 51,  [C23] = 52,  [B18] = 53,  [B21] = 54,	 [M15] = 55,  [B19] = 56,
	[B26] = 57,  [A25] = 58,  [A24] = 59,  [B24] = 60,  [E26] = 61,	 [A21] = 62,  [A19] = 63,
	[A18] = 64,  [D26] = 65,  [C26] = 66,  [A23] = 67,  [A22] = 68,	 [B25] = 69,  [F26] = 70,
	[A26] = 71,  [A14] = 72,  [E10] = 73,  [E13] = 74,  [D12] = 75,	 [F10] = 76,  [E11] = 77,
	[F11] = 78,  [F13] = 79,  [N15] = 80,  [C20] = 81,  [C19] = 82,	 [A8] = 83,   [R14] = 84,
	[A7] = 85,   [P14] = 86,  [D20] = 87,  [A6] = 88,   [B6] = 89,	 [N14] = 90,  [B7] = 91,
	[B8] = 92,   [B9] = 93,	  [M14] = 94,  [J11] = 95,  [E7] = 96,	 [D19] = 97,  [B11] = 98,
	[D15] = 99,  [B12] = 100, [B10] = 101, [P13] = 102, [C18] = 103, [C6] = 104,  [C7] = 105,
	[D7] = 106,  [N13] = 107, [C8] = 108,  [C9] = 109,  [C10] = 110, [M16] = 111, [A15] = 112,
	[E9] = 113,  [F9] = 114,  [F8] = 115,  [M13] = 116, [F7] = 117,	 [D8] = 118,  [E8] = 119,
	[L12] = 120,
};

static int aspeed_g7_soc1_pin_config_get(struct pinctrl_dev *pctldev,
					 unsigned int pin,
					 unsigned long *config)
{
	struct aspeed_g7_soc1_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	struct aspeed_g7_field field;
	unsigned int val, val_raw;
	int ret, ds_idx;

	if (pin > AC24)
		return -EINVAL;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		field = aspeed_g7_soc1_bias_field_from_pin(pin);
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
		/*
		 * The hardware has a single 1-bit enable/disable field per
		 * pin; pull direction is fixed in silicon and cannot be read
		 * back from the register.  Reject readback requests for a
		 * specific pull direction.
		 */
		return -ENOTSUPP;
	case PIN_CONFIG_DRIVE_STRENGTH:
		ds_idx = aspeed_g7_soc1_drv_map[pin];
		if (!ds_idx)
			return -ENOTSUPP;
		ds_idx--; /* Adjust back to 0-based hardware index */
		field = aspeed_g7_soc1_drv_field_from_idx(ds_idx);
		break;
	default:
		return -ENOTSUPP;
	}

	ret = regmap_read(pctl->regmap, field.reg, &val_raw);
	if (ret)
		return ret;

	val = (val_raw & (field.mask << field.shift)) >> field.shift;
	if (param == PIN_CONFIG_DRIVE_STRENGTH)
		val = (val + ASPEED_G7_SOC1_DRV_STRENGTH_HW_BASE) *
		      ASPEED_G7_SOC1_DRV_STRENGTH_STEP_MA;

	if (!val)
		return -EINVAL;

	*config = pinconf_to_config_packed(param, val);

	return 0;
}

static int aspeed_g7_soc1_pin_config_set(struct pinctrl_dev *pctldev,
					 unsigned int pin,
					 unsigned long *configs,
					 unsigned int num_configs)
{
	struct aspeed_g7_soc1_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct aspeed_g7_field field;
	enum pin_config_param param;
	int i, ret, ds_idx;
	unsigned int val;
	u32 arg;

	if (pin > AC24)
		return -EINVAL;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_DOWN:
		case PIN_CONFIG_BIAS_PULL_UP:
			/*
			 * The hardware has one enable/disable bit per pin;
			 * pull direction is fixed in silicon.  Both PULL_UP
			 * and PULL_DOWN map to "enable bias"; the caller must
			 * request the direction that the hardware provides.
			 */
		case PIN_CONFIG_BIAS_DISABLE:
			field = aspeed_g7_soc1_bias_field_from_pin(pin);
			val = (param == PIN_CONFIG_BIAS_DISABLE) ? 1 : 0;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			ds_idx = aspeed_g7_soc1_drv_map[pin];
			if (!ds_idx)
				return -ENOTSUPP;
			ds_idx--; /* Adjust back to 0-based hardware index */
			field = aspeed_g7_soc1_drv_field_from_idx(ds_idx);
			ret = aspeed_g7_soc1_drive_strength_to_hw(arg, &val);
			if (ret)
				return ret;
			break;
		default:
			return -ENOTSUPP;
		}

		dev_dbg(pctl->dev,
			"Configuring pin %u reg 0x%x shift %u param %d arg %u val 0x%x\n",
			pin, field.reg, field.shift, param, arg, val);

		ret = regmap_update_bits(pctl->regmap, field.reg,
					 field.mask << field.shift,
					 val << field.shift);

		if (ret)
			return ret;
	}

	return 0;
}

static int aspeed_g7_soc1_pin_config_group_get(struct pinctrl_dev *pctldev,
					       unsigned int selector,
					       unsigned long *config)
{
	const unsigned int *pins;
	unsigned int npins;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, selector, &pins, &npins);
	if (ret)
		return ret;
	if (!npins)
		return -ENODEV;

	return aspeed_g7_soc1_pin_config_get(pctldev, pins[0], config);
}

static int aspeed_g7_soc1_pin_config_group_set(struct pinctrl_dev *pctldev,
					       unsigned int selector,
					       unsigned long *configs,
					       unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int npins;
	int ret;
	int i;

	ret = pinctrl_generic_get_group_pins(pctldev, selector, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = aspeed_g7_soc1_pin_config_set(pctldev, pins[i], configs,
						    num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops aspeed_g7_soc1_conf_ops = {
	.is_generic = true,
	.pin_config_get = aspeed_g7_soc1_pin_config_get,
	.pin_config_set = aspeed_g7_soc1_pin_config_set,
	.pin_config_group_get = aspeed_g7_soc1_pin_config_group_get,
	.pin_config_group_set = aspeed_g7_soc1_pin_config_group_set,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

static const struct pinctrl_desc aspeed_g7_soc1_desc = {
	.name = "aspeed-g7-soc1-pinctrl",
	.pins = aspeed_g7_soc1_pins,
	.npins = ARRAY_SIZE(aspeed_g7_soc1_pins),
	.pctlops = &aspeed_g7_soc1_pctl_ops,
	.pmxops = &aspeed_g7_soc1_pmx_ops,
	.confops = &aspeed_g7_soc1_conf_ops,
	.owner = THIS_MODULE,
};

 #define PIN_GROUP(name, ...) static const unsigned int name ## _pins[] = { __VA_ARGS__ }

/* Pin Groups and Functions */
PIN_GROUP(ADC0, W17);
PIN_GROUP(ADC1, V18);
PIN_GROUP(ADC10, Y11);
PIN_GROUP(ADC11, V14);
PIN_GROUP(ADC12, V19);
PIN_GROUP(ADC13, W14);
PIN_GROUP(ADC14, Y20);
PIN_GROUP(ADC15, AB19);
PIN_GROUP(ADC2, W18);
PIN_GROUP(ADC3, Y17);
PIN_GROUP(ADC4, AA18);
PIN_GROUP(ADC5, AA13);
PIN_GROUP(ADC6, Y18);
PIN_GROUP(ADC7, AA12);
PIN_GROUP(ADC8, W20);
PIN_GROUP(ADC9, V20);
PIN_GROUP(AUXPWRGOOD0, W14);
PIN_GROUP(AUXPWRGOOD1, Y20);
PIN_GROUP(CANBUS, G7, G8, G9);
PIN_GROUP(DI2C0, C16, D9);
PIN_GROUP(DI2C1, C14, F14);
PIN_GROUP(DI2C10, R25, AA26);
PIN_GROUP(DI2C11, R26, Y25);
PIN_GROUP(DI2C12, W25, Y23);
PIN_GROUP(DI2C13, Y24, W21);
PIN_GROUP(DI2C14, AA23, AC22);
PIN_GROUP(DI2C15, AB22, Y21);
PIN_GROUP(DI2C2, D10, C12);
PIN_GROUP(DI2C3, C11, C13);
PIN_GROUP(DI2C8, U25, U26);
PIN_GROUP(DI2C9, Y26, AA24);
PIN_GROUP(DSGPM0, D19, B10, C7, D7);
PIN_GROUP(ESPI0, B16, D14, B15, B14, C17, B13, E14, C15);
PIN_GROUP(ESPI1, C16, C14, C11, D9, F14, D10, C12, C13);
PIN_GROUP(FSI0, AD20, AC20);
PIN_GROUP(FSI1, AA21, AB21);
PIN_GROUP(FSI2, AC19, AE18);
PIN_GROUP(FSI3, AD19, AD18);
PIN_GROUP(FWQSPI, M16, A15);
PIN_GROUP(FWSPIABR, A14);
PIN_GROUP(FWWPN, N15);
PIN_GROUP(HBLED, V24);
PIN_GROUP(HVI3C0, U25, U26);
PIN_GROUP(HVI3C1, Y26, AA24);
PIN_GROUP(HVI3C12, W25, Y23);
PIN_GROUP(HVI3C13, Y24, W21);
PIN_GROUP(HVI3C14, AA23, AC22);
PIN_GROUP(HVI3C15, AB22, Y21);
PIN_GROUP(HVI3C2, R25, AA26);
PIN_GROUP(HVI3C3, R26, Y25);
PIN_GROUP(I2C0, G11, H7);
PIN_GROUP(I2C1, H8, H9);
PIN_GROUP(I2C10, G8, G9);
PIN_GROUP(I2C11, G10, K12);
PIN_GROUP(I2C12, AC18, AA17);
PIN_GROUP(I2C13, AB17, AD16);
PIN_GROUP(I2C14, AC17, AD17);
PIN_GROUP(I2C15, AE16, AE17);
PIN_GROUP(I2C2, H10, H11);
PIN_GROUP(I2C3, J9, J10);
PIN_GROUP(I2C4, E9, F9);
PIN_GROUP(I2C5, F8, M13);
PIN_GROUP(I2C6, F7, D8);
PIN_GROUP(I2C7, E8, L12);
PIN_GROUP(I2C8, F12, E12);
PIN_GROUP(I2C9, J12, G7);
PIN_GROUP(I2CF0, F12, E12, J12, G7);
PIN_GROUP(I2CF1, E9, F9, F8, M13);
PIN_GROUP(I2CF2, F7, D8, E8, L12);
PIN_GROUP(I3C10, AC19, AE18);
PIN_GROUP(I3C11, AD19, AD18);
PIN_GROUP(I3C4, AE20, AF19);
PIN_GROUP(I3C5, Y22, AA20);
PIN_GROUP(I3C6, AA22, AB20);
PIN_GROUP(I3C7, AF18, AE19);
PIN_GROUP(I3C8, AD20, AC20);
PIN_GROUP(I3C9, AA21, AB21);
PIN_GROUP(JTAGM1, D12, F10, E11, F11, F13);
PIN_GROUP(LPC0, AF26, AF25, B16, D14, B15, B14, C17, B13, E14, C15);
PIN_GROUP(LPC1, C16, C14, C11, D9, F14, D10, C12, C13, AE16, AE17);
PIN_GROUP(LTPI, U25, U26, Y26, AA24);
PIN_GROUP(LTPI_PS_I2C0, G11, H7);
PIN_GROUP(LTPI_PS_I2C1, H8, H9);
PIN_GROUP(LTPI_PS_I2C2, H10, H11);
PIN_GROUP(LTPI_PS_I2C3, J9, J10);
PIN_GROUP(MACLINK0, U21);
PIN_GROUP(MACLINK1, AC24);
PIN_GROUP(MACLINK2, T24);
PIN_GROUP(MDIO0, B9, M14);
PIN_GROUP(MDIO1, C9, C10);
PIN_GROUP(MDIO2, E10, E13);
PIN_GROUP(NCTS0, AF17);
PIN_GROUP(NCTS1, AA15);
PIN_GROUP(NCTS5, V21);
PIN_GROUP(NCTS6, AB26);
PIN_GROUP(NDCD0, AA16);
PIN_GROUP(NDCD1, AB15);
PIN_GROUP(NDCD5, N26);
PIN_GROUP(NDCD6, AD26);
PIN_GROUP(NDSR0, Y16);
PIN_GROUP(NDSR1, AC15);
PIN_GROUP(NDSR5, P25);
PIN_GROUP(NDSR6, P26);
PIN_GROUP(NDTR0, J13);
PIN_GROUP(NDTR1, Y15);
PIN_GROUP(NDTR5, V23);
PIN_GROUP(NDTR6, AF26);
PIN_GROUP(NRI0, V17);
PIN_GROUP(NRI1, AD15);
PIN_GROUP(NRI5, N25);
PIN_GROUP(NRI6, AE26);
PIN_GROUP(NRTS0, AB16);
PIN_GROUP(NRTS1, AA14);
PIN_GROUP(NRTS5, W22);
PIN_GROUP(NRTS6, AF25);
PIN_GROUP(OSCCLK, C17);
PIN_GROUP(PE2SGRSTN, E10, PCIERC2_PERST);
PIN_GROUP(PWM0, AE25);
PIN_GROUP(PWM1, AD25);
PIN_GROUP(PWM10, AB17);
PIN_GROUP(PWM11, AD16);
PIN_GROUP(PWM12, AC17);
PIN_GROUP(PWM13, AD17);
PIN_GROUP(PWM14, AE16);
PIN_GROUP(PWM15, AE17);
PIN_GROUP(PWM2, AF23);
PIN_GROUP(PWM3, AF20);
PIN_GROUP(PWM4, AF21);
PIN_GROUP(PWM5, AE21);
PIN_GROUP(PWM6, AE23);
PIN_GROUP(PWM7, AD22);
PIN_GROUP(PWM8, K13);
PIN_GROUP(PWM9, AA17);
PIN_GROUP(QSPI0, C23, B18);
PIN_GROUP(QSPI1, B24, E26);
PIN_GROUP(QSPI2, B25, F26);
PIN_GROUP(RGMII0, C20, C19, A8, R14, A7, P14, D20, A6, B6, N14, B7, B8);
PIN_GROUP(RGMII1, D19, B11, D15, B12, B10, P13, C18, C6, C7, D7, N13, C8);
PIN_GROUP(RMII0, C20, A8, R14, A7, P14, A6, B6, N14);
PIN_GROUP(RMII0RCLKO, D20);
PIN_GROUP(RMII1, D19, D15, B12, B10, P13, C6, C7, D7);
PIN_GROUP(RMII1RCLKO, C18);
PIN_GROUP(SALT0, AC17);
PIN_GROUP(SALT1, AD17);
PIN_GROUP(SALT10, Y18);
PIN_GROUP(SALT11, AA12);
PIN_GROUP(SALT12, AB26);
PIN_GROUP(SALT13, AD26);
PIN_GROUP(SALT14, P26);
PIN_GROUP(SALT15, AE26);
PIN_GROUP(SALT2, AC15);
PIN_GROUP(SALT3, AD15);
PIN_GROUP(SALT4, W17);
PIN_GROUP(SALT5, V18);
PIN_GROUP(SALT6, W18);
PIN_GROUP(SALT7, Y17);
PIN_GROUP(SALT8, AA18);
PIN_GROUP(SALT9, AA13);
PIN_GROUP(SD, C16, C14, C11, D9, F14, D10, C12, C13);
PIN_GROUP(SGMII, SGMII0);
PIN_GROUP(SGPM0, U21, T24, V22, T23);
PIN_GROUP(SGPM1, AC25, AB25, AB24, W26);
PIN_GROUP(SGPS, B11, C18, N13, C8);
PIN_GROUP(SIOONCTRLN0, AE23);
PIN_GROUP(SIOONCTRLN1, AA15);
PIN_GROUP(SIOPBIN0, AD25);
PIN_GROUP(SIOPBIN1, AA16);
PIN_GROUP(SIOPBON0, AE25);
PIN_GROUP(SIOPBON1, AF17);
PIN_GROUP(SIOPWREQN0, AE21);
PIN_GROUP(SIOPWREQN1, AB16);
PIN_GROUP(SIOPWRGD1, AB15);
PIN_GROUP(SIOS3N0, AF20);
PIN_GROUP(SIOS3N1, V17);
PIN_GROUP(SIOS5N0, AF21);
PIN_GROUP(SIOS5N1, J13);
PIN_GROUP(SIOSCIN0, AF23);
PIN_GROUP(SIOSCIN1, Y16);
PIN_GROUP(SMON0, U21, T24, V22, T23);
PIN_GROUP(SMON1, W26, AC25, AB25);
PIN_GROUP(SPI0, D24, B23, B22);
PIN_GROUP(SPI0ABR, M15);
PIN_GROUP(SPI0CS1, B21);
PIN_GROUP(SPI0WPN, B19);
PIN_GROUP(SPI1, B26, A25, A24);
PIN_GROUP(SPI1ABR, A19);
PIN_GROUP(SPI1CS1, A21);
PIN_GROUP(SPI1WPN, A18);
PIN_GROUP(SPI2, D26, C26, A23, A22);
PIN_GROUP(SPI2CS1, A26);
PIN_GROUP(TACH0, AC26);
PIN_GROUP(TACH1, AA25);
PIN_GROUP(TACH10, AB26);
PIN_GROUP(TACH11, AD26);
PIN_GROUP(TACH12, P26);
PIN_GROUP(TACH13, AE26);
PIN_GROUP(TACH14, AF26);
PIN_GROUP(TACH15, AF25);
PIN_GROUP(TACH2, AB23);
PIN_GROUP(TACH3, U22);
PIN_GROUP(TACH4, V21);
PIN_GROUP(TACH5, N26);
PIN_GROUP(TACH6, P25);
PIN_GROUP(TACH7, N25);
PIN_GROUP(TACH8, V23);
PIN_GROUP(TACH9, W22);
PIN_GROUP(THRU0, AC26, AA25);
PIN_GROUP(THRU1, AB23, U22);
PIN_GROUP(THRU2, A19, A18);
PIN_GROUP(THRU3, B25, F26);
PIN_GROUP(UART0, AC16, AF16);
PIN_GROUP(UART1, W16, V16);
PIN_GROUP(UART2, AB18, AC18);
PIN_GROUP(UART3, K13, AA17);
PIN_GROUP(UART5, AB17, AD16);
PIN_GROUP(UART6, AC17, AD17);
PIN_GROUP(UART7, AE16, AE17);
PIN_GROUP(UART8, M15, B19);
PIN_GROUP(UART9, B26, A25);
PIN_GROUP(UART10, A24, B24);
PIN_GROUP(UART11, E26, A21);
PIN_GROUP(USB2CD, PORTC_MODE);
PIN_GROUP(USB2CH, PORTC_MODE);
PIN_GROUP(USB2CU, PORTC_MODE);
PIN_GROUP(USB2CUD, PORTC_MODE);
PIN_GROUP(USB2DD, PORTD_MODE);
PIN_GROUP(USB2DH, PORTD_MODE);
PIN_GROUP(USBUART, G10, K12);
PIN_GROUP(VGA, J11, E7);
PIN_GROUP(VPI, C16, C14, C11, D9, F14, D10, AC26, AA25, AB23, U22, V21, N26,
	  P25, N25, V23, W22, AB26, AD26, P26, AE26, AF26, AF25, AE25, AD25,
	  AF23, AF20, AF21, AE21);
PIN_GROUP(WDTRST0N, K13);
PIN_GROUP(WDTRST1N, AA17);
PIN_GROUP(WDTRST2N, AB17);
PIN_GROUP(WDTRST3N, AD16);
PIN_GROUP(WDTRST4N, AC25);
PIN_GROUP(WDTRST5N, AB25);
PIN_GROUP(WDTRST6N, AC24);
PIN_GROUP(WDTRST7N, AB24);

#define GROUP(n) PINCTRL_PINGROUP(#n, n##_pins, ARRAY_SIZE(n##_pins))

static const struct pingroup aspeed_g7_soc1_groups[] = {
	GROUP(ADC0),
	GROUP(ADC1),
	GROUP(ADC10),
	GROUP(ADC11),
	GROUP(ADC12),
	GROUP(ADC13),
	GROUP(ADC14),
	GROUP(ADC15),
	GROUP(ADC2),
	GROUP(ADC3),
	GROUP(ADC4),
	GROUP(ADC5),
	GROUP(ADC6),
	GROUP(ADC7),
	GROUP(ADC8),
	GROUP(ADC9),
	GROUP(AUXPWRGOOD0),
	GROUP(AUXPWRGOOD1),
	GROUP(CANBUS),
	GROUP(DI2C0),
	GROUP(DI2C1),
	GROUP(DI2C10),
	GROUP(DI2C11),
	GROUP(DI2C12),
	GROUP(DI2C13),
	GROUP(DI2C14),
	GROUP(DI2C15),
	GROUP(DI2C2),
	GROUP(DI2C3),
	GROUP(DI2C8),
	GROUP(DI2C9),
	GROUP(DSGPM0),
	GROUP(ESPI0),
	GROUP(ESPI1),
	GROUP(FSI0),
	GROUP(FSI1),
	GROUP(FSI2),
	GROUP(FSI3),
	GROUP(FWQSPI),
	GROUP(FWSPIABR),
	GROUP(FWWPN),
	GROUP(HBLED),
	GROUP(HVI3C0),
	GROUP(HVI3C1),
	GROUP(HVI3C12),
	GROUP(HVI3C13),
	GROUP(HVI3C14),
	GROUP(HVI3C15),
	GROUP(HVI3C2),
	GROUP(HVI3C3),
	GROUP(I2C0),
	GROUP(I2C1),
	GROUP(I2C10),
	GROUP(I2C11),
	GROUP(I2C12),
	GROUP(I2C13),
	GROUP(I2C14),
	GROUP(I2C15),
	GROUP(I2C2),
	GROUP(I2C3),
	GROUP(I2C4),
	GROUP(I2C5),
	GROUP(I2C6),
	GROUP(I2C7),
	GROUP(I2C8),
	GROUP(I2C9),
	GROUP(I2CF0),
	GROUP(I2CF1),
	GROUP(I2CF2),
	GROUP(I3C10),
	GROUP(I3C11),
	GROUP(I3C4),
	GROUP(I3C5),
	GROUP(I3C6),
	GROUP(I3C7),
	GROUP(I3C8),
	GROUP(I3C9),
	GROUP(JTAGM1),
	GROUP(LPC0),
	GROUP(LPC1),
	GROUP(LTPI),
	GROUP(LTPI_PS_I2C0),
	GROUP(LTPI_PS_I2C1),
	GROUP(LTPI_PS_I2C2),
	GROUP(LTPI_PS_I2C3),
	GROUP(MACLINK0),
	GROUP(MACLINK1),
	GROUP(MACLINK2),
	GROUP(MDIO0),
	GROUP(MDIO1),
	GROUP(MDIO2),
	GROUP(NCTS0),
	GROUP(NCTS1),
	GROUP(NCTS5),
	GROUP(NCTS6),
	GROUP(NDCD0),
	GROUP(NDCD1),
	GROUP(NDCD5),
	GROUP(NDCD6),
	GROUP(NDSR0),
	GROUP(NDSR1),
	GROUP(NDSR5),
	GROUP(NDSR6),
	GROUP(NDTR0),
	GROUP(NDTR1),
	GROUP(NDTR5),
	GROUP(NDTR6),
	GROUP(NRI0),
	GROUP(NRI1),
	GROUP(NRI5),
	GROUP(NRI6),
	GROUP(NRTS0),
	GROUP(NRTS1),
	GROUP(NRTS5),
	GROUP(NRTS6),
	GROUP(OSCCLK),
	GROUP(PE2SGRSTN),
	GROUP(PWM0),
	GROUP(PWM1),
	GROUP(PWM10),
	GROUP(PWM11),
	GROUP(PWM12),
	GROUP(PWM13),
	GROUP(PWM14),
	GROUP(PWM15),
	GROUP(PWM2),
	GROUP(PWM3),
	GROUP(PWM4),
	GROUP(PWM5),
	GROUP(PWM6),
	GROUP(PWM7),
	GROUP(PWM8),
	GROUP(PWM9),
	GROUP(QSPI0),
	GROUP(QSPI1),
	GROUP(QSPI2),
	GROUP(RGMII0),
	GROUP(RGMII1),
	GROUP(RMII0),
	GROUP(RMII0RCLKO),
	GROUP(RMII1),
	GROUP(RMII1RCLKO),
	GROUP(SALT0),
	GROUP(SALT1),
	GROUP(SALT10),
	GROUP(SALT11),
	GROUP(SALT12),
	GROUP(SALT13),
	GROUP(SALT14),
	GROUP(SALT15),
	GROUP(SALT2),
	GROUP(SALT3),
	GROUP(SALT4),
	GROUP(SALT5),
	GROUP(SALT6),
	GROUP(SALT7),
	GROUP(SALT8),
	GROUP(SALT9),
	GROUP(SD),
	GROUP(SGMII),
	GROUP(SGPM0),
	GROUP(SGPM1),
	GROUP(SGPS),
	GROUP(SIOONCTRLN0),
	GROUP(SIOONCTRLN1),
	GROUP(SIOPBIN0),
	GROUP(SIOPBIN1),
	GROUP(SIOPBON0),
	GROUP(SIOPBON1),
	GROUP(SIOPWREQN0),
	GROUP(SIOPWREQN1),
	GROUP(SIOPWRGD1),
	GROUP(SIOS3N0),
	GROUP(SIOS3N1),
	GROUP(SIOS5N0),
	GROUP(SIOS5N1),
	GROUP(SIOSCIN0),
	GROUP(SIOSCIN1),
	GROUP(SMON0),
	GROUP(SMON1),
	GROUP(SPI0),
	GROUP(SPI0ABR),
	GROUP(SPI0CS1),
	GROUP(SPI0WPN),
	GROUP(SPI1),
	GROUP(SPI1ABR),
	GROUP(SPI1CS1),
	GROUP(SPI1WPN),
	GROUP(SPI2),
	GROUP(SPI2CS1),
	GROUP(TACH0),
	GROUP(TACH1),
	GROUP(TACH10),
	GROUP(TACH11),
	GROUP(TACH12),
	GROUP(TACH13),
	GROUP(TACH14),
	GROUP(TACH15),
	GROUP(TACH2),
	GROUP(TACH3),
	GROUP(TACH4),
	GROUP(TACH5),
	GROUP(TACH6),
	GROUP(TACH7),
	GROUP(TACH8),
	GROUP(TACH9),
	GROUP(THRU0),
	GROUP(THRU1),
	GROUP(THRU2),
	GROUP(THRU3),
	GROUP(UART0),
	GROUP(UART1),
	GROUP(UART10),
	GROUP(UART11),
	GROUP(UART2),
	GROUP(UART3),
	GROUP(UART5),
	GROUP(UART6),
	GROUP(UART7),
	GROUP(UART8),
	GROUP(UART9),
	GROUP(USB2CD),
	GROUP(USB2CH),
	GROUP(USB2CU),
	GROUP(USB2CUD),
	GROUP(USB2DD),
	GROUP(USB2DH),
	GROUP(USBUART),
	GROUP(VGA),
	GROUP(VPI),
	GROUP(WDTRST0N),
	GROUP(WDTRST1N),
	GROUP(WDTRST2N),
	GROUP(WDTRST3N),
	GROUP(WDTRST4N),
	GROUP(WDTRST5N),
	GROUP(WDTRST6N),
	GROUP(WDTRST7N),
};

/**
 * VM() - Helper macro to unwrap a parenthesized list of arguments.
 * @...: The parenthesized list to be unwrapped.
 *
 * Since the C preprocessor treats commas inside braces {} as argument
 * separators for macros, we wrap lists (like mux values) in parentheses ()
 * to protect them during macro expansion. This macro strips those
 * parentheses when the values are needed for array initialization.
 */
#define VM(...) __VA_ARGS__

/**
 * FUNC() - Macro to initialize an aspeed_g7_soc1_function entry.
 * @n: Name of the pin function.
 * @m: Parenthesized list of mux values, mapped 1:1 to the groups list.
 * @...: Variable list of pin group names associated with this function.
 *
 * This macro solves complex static initialization by:
 * 1. Creating anonymous arrays for both group names and mux values
 *    using C99 Compound Literals.
 * 2. Using VM(m) to unwrap mux values into the array initializer.
 * 3. Calculating the number of groups via sizeof() division, which
 *    bypasses the __must_be_array() check performed by ARRAY_SIZE()
 *    that often fails on compound literals in the kernel environment.
 *
 * Example: FUNC(i2c0, (1, 4), "i2c0", "di2c0")
 *          Maps "i2c0" group to mux value 1 and "di2c0" group to mux value 4.
 */
#define FUNC(n, m, ...)                                                                          \
	{                                                                                        \
		.pinfunction = {                                                                 \
			.name = #n,                                                              \
			.groups = (const char *const[]){ __VA_ARGS__ },                          \
			.ngroups = sizeof((const char *const[]){ __VA_ARGS__ }) / sizeof(char *), \
		},                                                                               \
		.muxvals = (const u8[]){ VM m }                                                  \
	}

static const struct aspeed_g7_soc1_function aspeed_g7_soc1_functions[] = {
	FUNC(ADC0, (0), "ADC0"),
	FUNC(ADC1, (0), "ADC1"),
	FUNC(ADC10, (0), "ADC10"),
	FUNC(ADC11, (0), "ADC11"),
	FUNC(ADC12, (0), "ADC12"),
	FUNC(ADC13, (0), "ADC13"),
	FUNC(ADC14, (0), "ADC14"),
	FUNC(ADC15, (0), "ADC15"),
	FUNC(ADC2, (0), "ADC2"),
	FUNC(ADC3, (0), "ADC3"),
	FUNC(ADC4, (0), "ADC4"),
	FUNC(ADC5, (0), "ADC5"),
	FUNC(ADC6, (0), "ADC6"),
	FUNC(ADC7, (0), "ADC7"),
	FUNC(ADC8, (0), "ADC8"),
	FUNC(ADC9, (0), "ADC9"),
	FUNC(AUXPWRGOOD0, (2), "AUXPWRGOOD0"),
	FUNC(AUXPWRGOOD1, (2), "AUXPWRGOOD1"),
	FUNC(CANBUS, (2), "CANBUS"),
	FUNC(ESPI0, (1), "ESPI0"),
	FUNC(ESPI1, (1), "ESPI1"),
	FUNC(FSI0, (2), "FSI0"),
	FUNC(FSI1, (2), "FSI1"),
	FUNC(FSI2, (2), "FSI2"),
	FUNC(FSI3, (2), "FSI3"),
	FUNC(FWQSPI, (1), "FWQSPI"),
	FUNC(FWSPIABR, (1), "FWSPIABR"),
	FUNC(FWWPN, (1), "FWWPN"),
	FUNC(HBLED, (2), "HBLED"),
	FUNC(I2C0, (1, 2, 4), "I2C0", "LTPI_PS_I2C0", "DI2C0"),
	FUNC(I2C1, (1, 2, 4), "I2C1", "LTPI_PS_I2C1", "DI2C1"),
	FUNC(I2C10, (1, 2), "I2C10", "DI2C10"),
	FUNC(I2C11, (1, 2), "I2C11", "DI2C11"),
	FUNC(I2C12, (4, 2), "I2C12", "DI2C12"),
	FUNC(I2C13, (4, 2), "I2C13", "DI2C13"),
	FUNC(I2C14, (4, 2), "I2C14", "DI2C14"),
	FUNC(I2C15, (2, 2), "I2C15", "DI2C15"),
	FUNC(I2C2, (1, 2, 4), "I2C2", "LTPI_PS_I2C2", "DI2C2"),
	FUNC(I2C3, (1, 2, 4), "I2C3", "LTPI_PS_I2C3", "DI2C3"),
	FUNC(I2C4, (1), "I2C4"),
	FUNC(I2C5, (1), "I2C5"),
	FUNC(I2C6, (1), "I2C6"),
	FUNC(I2C7, (1), "I2C7"),
	FUNC(I2C8, (1, 2), "I2C8", "DI2C8"),
	FUNC(I2C9, (1, 2), "I2C9", "DI2C9"),
	FUNC(I2CF0, (5), "I2CF0"),
	FUNC(I2CF1, (5), "I2CF1"),
	FUNC(I2CF2, (5), "I2CF2"),
	FUNC(I3C0, (1), "HVI3C0"),
	FUNC(I3C1, (1), "HVI3C1"),
	FUNC(I3C10, (1), "I3C10"),
	FUNC(I3C11, (1), "I3C11"),
	FUNC(I3C12, (1), "HVI3C12"),
	FUNC(I3C13, (1), "HVI3C13"),
	FUNC(I3C14, (1), "HVI3C14"),
	FUNC(I3C15, (1), "HVI3C15"),
	FUNC(I3C2, (1), "HVI3C2"),
	FUNC(I3C3, (1), "HVI3C3"),
	FUNC(I3C4, (1), "I3C4"),
	FUNC(I3C5, (1), "I3C5"),
	FUNC(I3C6, (1), "I3C6"),
	FUNC(I3C7, (1), "I3C7"),
	FUNC(I3C8, (1), "I3C8"),
	FUNC(I3C9, (1), "I3C9"),
	FUNC(JTAGM1, (1), "JTAGM1"),
	FUNC(LPC0, (2), "LPC0"),
	FUNC(LPC1, (2), "LPC1"),
	FUNC(LTPI, (2), "LTPI"),
	FUNC(MACLINK0, (4), "MACLINK0"),
	FUNC(MACLINK1, (3), "MACLINK1"),
	FUNC(MACLINK2, (4), "MACLINK2"),
	FUNC(MDIO0, (1), "MDIO0"),
	FUNC(MDIO1, (1), "MDIO1"),
	FUNC(MDIO2, (1), "MDIO2"),
	FUNC(NCTS0, (1), "NCTS0"),
	FUNC(NCTS1, (1), "NCTS1"),
	FUNC(NCTS5, (4), "NCTS5"),
	FUNC(NCTS6, (4), "NCTS6"),
	FUNC(NDCD0, (1), "NDCD0"),
	FUNC(NDCD1, (1), "NDCD1"),
	FUNC(NDCD5, (4), "NDCD5"),
	FUNC(NDCD6, (4), "NDCD6"),
	FUNC(NDSR0, (1), "NDSR0"),
	FUNC(NDSR1, (1), "NDSR1"),
	FUNC(NDSR5, (4), "NDSR5"),
	FUNC(NDSR6, (4), "NDSR6"),
	FUNC(NDTR0, (1), "NDTR0"),
	FUNC(NDTR1, (1), "NDTR1"),
	FUNC(NDTR5, (4), "NDTR5"),
	FUNC(NDTR6, (4), "NDTR6"),
	FUNC(NRI0, (1), "NRI0"),
	FUNC(NRI1, (1), "NRI1"),
	FUNC(NRI5, (4), "NRI5"),
	FUNC(NRI6, (4), "NRI6"),
	FUNC(NRTS0, (1), "NRTS0"),
	FUNC(NRTS1, (1), "NRTS1"),
	FUNC(NRTS5, (4), "NRTS5"),
	FUNC(NRTS6, (4), "NRTS6"),
	FUNC(OSCCLK, (3), "OSCCLK"),
	FUNC(PCIERC, (2), "PE2SGRSTN"),
	FUNC(PWM0, (1), "PWM0"),
	FUNC(PWM1, (1), "PWM1"),
	FUNC(PWM10, (3), "PWM10"),
	FUNC(PWM11, (3), "PWM11"),
	FUNC(PWM12, (3), "PWM12"),
	FUNC(PWM13, (3), "PWM13"),
	FUNC(PWM14, (3), "PWM14"),
	FUNC(PWM15, (3), "PWM15"),
	FUNC(PWM2, (1), "PWM2"),
	FUNC(PWM3, (1), "PWM3"),
	FUNC(PWM4, (1), "PWM4"),
	FUNC(PWM5, (1), "PWM5"),
	FUNC(PWM6, (1), "PWM6"),
	FUNC(PWM7, (1), "PWM7"),
	FUNC(PWM8, (3), "PWM8"),
	FUNC(PWM9, (3), "PWM9"),
	FUNC(QSPI0, (1), "QSPI0"),
	FUNC(QSPI1, (1), "QSPI1"),
	FUNC(QSPI2, (1), "QSPI2"),
	FUNC(RGMII0, (1), "RGMII0"),
	FUNC(RGMII1, (1), "RGMII1"),
	FUNC(RMII0, (2), "RMII0"),
	FUNC(RMII0RCLKO, (2), "RMII0RCLKO"),
	FUNC(RMII1, (2), "RMII1"),
	FUNC(RMII1RCLKO, (2), "RMII1RCLKO"),
	FUNC(SALT0, (2), "SALT0"),
	FUNC(SALT1, (2), "SALT1"),
	FUNC(SALT10, (2), "SALT10"),
	FUNC(SALT11, (2), "SALT11"),
	FUNC(SALT12, (2), "SALT12"),
	FUNC(SALT13, (2), "SALT13"),
	FUNC(SALT14, (2), "SALT14"),
	FUNC(SALT15, (2), "SALT15"),
	FUNC(SALT2, (2), "SALT2"),
	FUNC(SALT3, (2), "SALT3"),
	FUNC(SALT4, (2), "SALT4"),
	FUNC(SALT5, (2), "SALT5"),
	FUNC(SALT6, (2), "SALT6"),
	FUNC(SALT7, (2), "SALT7"),
	FUNC(SALT8, (2), "SALT8"),
	FUNC(SALT9, (2), "SALT9"),
	FUNC(SD, (3), "SD"),
	FUNC(SGMII, (1), "SGMII"),
	FUNC(SGPM0, (1, 4), "SGPM0", "DSGPM0"),
	FUNC(SGPM1, (1), "SGPM1"),
	FUNC(SGPS, (5), "SGPS"),
	FUNC(SIOONCTRLN0, (2), "SIOONCTRLN0"),
	FUNC(SIOONCTRLN1, (2), "SIOONCTRLN1"),
	FUNC(SIOPBIN0, (2), "SIOPBIN0"),
	FUNC(SIOPBIN1, (2), "SIOPBIN1"),
	FUNC(SIOPBON0, (2), "SIOPBON0"),
	FUNC(SIOPBON1, (2), "SIOPBON1"),
	FUNC(SIOPWREQN0, (2), "SIOPWREQN0"),
	FUNC(SIOPWREQN1, (2), "SIOPWREQN1"),
	FUNC(SIOPWRGD1, (2), "SIOPWRGD1"),
	FUNC(SIOS3N0, (2), "SIOS3N0"),
	FUNC(SIOS3N1, (2), "SIOS3N1"),
	FUNC(SIOS5N0, (2), "SIOS5N0"),
	FUNC(SIOS5N1, (2), "SIOS5N1"),
	FUNC(SIOSCIN0, (2), "SIOSCIN0"),
	FUNC(SIOSCIN1, (2), "SIOSCIN1"),
	FUNC(SMON0, (2), "SMON0"),
	FUNC(SMON1, (4), "SMON1"),
	FUNC(SPI0, (1), "SPI0"),
	FUNC(SPI0ABR, (1), "SPI0ABR"),
	FUNC(SPI0CS1, (1), "SPI0CS1"),
	FUNC(SPI0WPN, (1), "SPI0WPN"),
	FUNC(SPI1, (1), "SPI1"),
	FUNC(SPI1ABR, (1), "SPI1ABR"),
	FUNC(SPI1CS1, (1), "SPI1CS1"),
	FUNC(SPI1WPN, (1), "SPI1WPN"),
	FUNC(SPI2, (1), "SPI2"),
	FUNC(SPI2CS1, (1), "SPI2CS1"),
	FUNC(TACH0, (1), "TACH0"),
	FUNC(TACH1, (1), "TACH1"),
	FUNC(TACH10, (1), "TACH10"),
	FUNC(TACH11, (1), "TACH11"),
	FUNC(TACH12, (1), "TACH12"),
	FUNC(TACH13, (1), "TACH13"),
	FUNC(TACH14, (1), "TACH14"),
	FUNC(TACH15, (1), "TACH15"),
	FUNC(TACH2, (1), "TACH2"),
	FUNC(TACH3, (1), "TACH3"),
	FUNC(TACH4, (1), "TACH4"),
	FUNC(TACH5, (1), "TACH5"),
	FUNC(TACH6, (1), "TACH6"),
	FUNC(TACH7, (1), "TACH7"),
	FUNC(TACH8, (1), "TACH8"),
	FUNC(TACH9, (1), "TACH9"),
	FUNC(THRU0, (2), "THRU0"),
	FUNC(THRU1, (2), "THRU1"),
	FUNC(THRU2, (4), "THRU2"),
	FUNC(THRU3, (4), "THRU3"),
	FUNC(UART0, (1), "UART0"),
	FUNC(UART1, (1), "UART1"),
	FUNC(UART10, (3), "UART10"),
	FUNC(UART11, (3), "UART11"),
	FUNC(UART2, (1), "UART2"),
	FUNC(UART3, (1), "UART3"),
	FUNC(UART5, (4), "UART5"),
	FUNC(UART6, (4), "UART6"),
	FUNC(UART7, (1), "UART7"),
	FUNC(UART8, (3), "UART8"),
	FUNC(UART9, (3), "UART9"),
	FUNC(USB2C, (0, 1, 2, 3), "USB2CUD", "USB2CD", "USB2CH", "USB2CU"),
	FUNC(USB2D, (1, 2), "USB2DD", "USB2DH"),
	FUNC(USBUART, (2), "USBUART"),
	FUNC(VGA, (1), "VGA"),
	FUNC(VPI, (5), "VPI"),
	FUNC(WDTRST0N, (2), "WDTRST0N"),
	FUNC(WDTRST1N, (2), "WDTRST1N"),
	FUNC(WDTRST2N, (2), "WDTRST2N"),
	FUNC(WDTRST3N, (2), "WDTRST3N"),
	FUNC(WDTRST4N, (2), "WDTRST4N"),
	FUNC(WDTRST5N, (2), "WDTRST5N"),
	FUNC(WDTRST6N, (2), "WDTRST6N"),
	FUNC(WDTRST7N, (2), "WDTRST7N"),
};

static int aspeed_g7_soc1_pinctrl_probe(struct platform_device *pdev)
{
	struct aspeed_g7_soc1_pinctrl *pctl;
	struct device *dev = &pdev->dev;
	int i, ret;

	pctl = devm_kzalloc(dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;

	pctl->dev = dev;
	pctl->regmap = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(pctl->regmap)) {
		dev_err(dev, "Failed to get regmap from parent\n");
		return PTR_ERR(pctl->regmap);
	}

	ret = devm_pinctrl_register_and_init(dev, &aspeed_g7_soc1_desc, pctl,
					     &pctl->pctl);
	if (ret) {
		dev_err(dev, "Failed to register pinctrl\n");
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(aspeed_g7_soc1_groups); i++) {
		const struct pingroup *grp = &aspeed_g7_soc1_groups[i];

		ret = pinctrl_generic_add_group(pctl->pctl, grp->name,
						(const unsigned int *)grp->pins,
						grp->npins, pctl);
		if (ret < 0) {
			dev_err(dev, "Failed to add group %s\n", grp->name);
			return ret;
		}
	}

	for (i = 0; i < ARRAY_SIZE(aspeed_g7_soc1_functions); i++) {
		const struct aspeed_g7_soc1_function *func = &aspeed_g7_soc1_functions[i];

		ret = pinmux_generic_add_function(pctl->pctl, func->pinfunction.name,
						  func->pinfunction.groups,
						  func->pinfunction.ngroups, (void *)func);
		if (ret < 0) {
			dev_err(dev, "Failed to add function %s\n", func->pinfunction.name);
			return ret;
		}
	}

	return pinctrl_enable(pctl->pctl);
}

static const struct of_device_id aspeed_g7_soc1_pinctrl_match[] = {
	{ .compatible = "aspeed,ast2700-soc1-pinctrl" },
	{}
};
MODULE_DEVICE_TABLE(of, aspeed_g7_soc1_pinctrl_match);

static struct platform_driver aspeed_g7_soc1_pinctrl_driver = {
	.probe = aspeed_g7_soc1_pinctrl_probe,
	.driver = {
		.name = "aspeed-g7-soc1-pinctrl",
		.of_match_table = aspeed_g7_soc1_pinctrl_match,
		.suppress_bind_attrs = true,
	},
};

static int __init aspeed_g7_soc1_pinctrl_init(void)
{
	return platform_driver_register(&aspeed_g7_soc1_pinctrl_driver);
}
arch_initcall(aspeed_g7_soc1_pinctrl_init);
