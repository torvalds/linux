#ifndef __ASM_ARCH_W90P910_KEYPAD_H
#define __ASM_ARCH_W90P910_KEYPAD_H

#include <linux/input/matrix_keypad.h>

extern void mfp_set_groupi(struct device *dev);

struct w90p910_keypad_platform_data {

	unsigned int	prescale;
	unsigned int	debounce;
	unsigned int	matrix_key_rows;
	unsigned int	matrix_key_cols;
	unsigned int	*matrix_key_map;
	int		matrix_key_map_size;
};

#endif /* __ASM_ARCH_W90P910_KEYPAD_H */
