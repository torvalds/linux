#ifndef __ASM_ARCH_PXA27x_KEYPAD_H
#define __ASM_ARCH_PXA27x_KEYPAD_H

#include <linux/input.h>
#include <linux/input/matrix_keypad.h>

#define MAX_MATRIX_KEY_ROWS	(8)
#define MAX_MATRIX_KEY_COLS	(8)
#define MATRIX_ROW_SHIFT	(3)
#define MAX_DIRECT_KEY_NUM	(8)

/* pxa3xx keypad platform specific parameters
 *
 * NOTE:
 * 1. direct_key_num indicates the number of keys in the direct keypad
 *    _plus_ the number of rotary-encoder sensor inputs,  this can be
 *    left as 0 if only rotary encoders are enabled,  the driver will
 *    automatically calculate this
 *
 * 2. direct_key_map is the key code map for the direct keys, if rotary
 *    encoder(s) are enabled, direct key 0/1(2/3) will be ignored
 *
 * 3. rotary can be either interpreted as a relative input event (e.g.
 *    REL_WHEEL/REL_HWHEEL) or specific keys (e.g. UP/DOWN/LEFT/RIGHT)
 *
 * 4. matrix key and direct key will use the same debounce_interval by
 *    default, which should be sufficient in most cases
 *
 * pxa168 keypad platform specific parameter
 *
 * NOTE:
 * clear_wakeup_event callback is a workaround required to clear the
 * keypad interrupt. The keypad wake must be cleared in addition to
 * reading the MI/DI bits in the KPC register.
 */
struct pxa27x_keypad_platform_data {

	/* code map for the matrix keys */
	unsigned int	matrix_key_rows;
	unsigned int	matrix_key_cols;
	unsigned int	*matrix_key_map;
	int		matrix_key_map_size;

	/* direct keys */
	int		direct_key_num;
	unsigned int	direct_key_map[MAX_DIRECT_KEY_NUM];

	/* rotary encoders 0 */
	int		enable_rotary0;
	int		rotary0_rel_code;
	int		rotary0_up_key;
	int		rotary0_down_key;

	/* rotary encoders 1 */
	int		enable_rotary1;
	int		rotary1_rel_code;
	int		rotary1_up_key;
	int		rotary1_down_key;

	/* key debounce interval */
	unsigned int	debounce_interval;

	/* clear wakeup event requirement for pxa168 */
	void		(*clear_wakeup_event)(void);
};

extern void pxa_set_keypad_info(struct pxa27x_keypad_platform_data *info);

#endif /* __ASM_ARCH_PXA27x_KEYPAD_H */
