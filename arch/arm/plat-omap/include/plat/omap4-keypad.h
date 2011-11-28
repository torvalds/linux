#ifndef ARCH_ARM_PLAT_OMAP4_KEYPAD_H
#define ARCH_ARM_PLAT_OMAP4_KEYPAD_H

#include <linux/input/matrix_keypad.h>

struct omap4_keypad_platform_data {
	const struct matrix_keymap_data *keymap_data;

	u8 rows;
	u8 cols;
};

extern int omap4_keyboard_init(struct omap4_keypad_platform_data *,
				struct omap_board_data *);
#endif
