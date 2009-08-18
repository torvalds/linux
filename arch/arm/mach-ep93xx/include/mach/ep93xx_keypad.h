/*
 * arch/arm/mach-ep93xx/include/mach/ep93xx_keypad.h
 */

#ifndef __ASM_ARCH_EP93XX_KEYPAD_H
#define __ASM_ARCH_EP93XX_KEYPAD_H

#define MAX_MATRIX_KEY_ROWS		(8)
#define MAX_MATRIX_KEY_COLS		(8)

/* flags for the ep93xx_keypad driver */
#define EP93XX_KEYPAD_DISABLE_3_KEY	(1<<0)	/* disable 3-key reset */
#define EP93XX_KEYPAD_DIAG_MODE		(1<<1)	/* diagnostic mode */
#define EP93XX_KEYPAD_BACK_DRIVE	(1<<2)	/* back driving mode */
#define EP93XX_KEYPAD_TEST_MODE		(1<<3)	/* scan only column 0 */
#define EP93XX_KEYPAD_KDIV		(1<<4)	/* 1/4 clock or 1/16 clock */
#define EP93XX_KEYPAD_AUTOREPEAT	(1<<5)	/* enable key autorepeat */

/**
 * struct ep93xx_keypad_platform_data - platform specific device structure
 * @matrix_key_rows:		number of rows in the keypad matrix
 * @matrix_key_cols:		number of columns in the keypad matrix
 * @matrix_key_map:		array of keycodes defining the keypad matrix
 * @matrix_key_map_size:	ARRAY_SIZE(matrix_key_map)
 * @debounce:			debounce start count; terminal count is 0xff
 * @prescale:			row/column counter pre-scaler load value
 * @flags:			see above
 */
struct ep93xx_keypad_platform_data {
	unsigned int	matrix_key_rows;
	unsigned int	matrix_key_cols;
	unsigned int	*matrix_key_map;
	int		matrix_key_map_size;
	unsigned int	debounce;
	unsigned int	prescale;
	unsigned int	flags;
};

/* macro for creating the matrix_key_map table */
#define KEY(row, col, val)	(((row) << 28) | ((col) << 24) | (val))

#endif	/* __ASM_ARCH_EP93XX_KEYPAD_H */
