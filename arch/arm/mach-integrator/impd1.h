/* SPDX-License-Identifier: GPL-2.0 */
#define IMPD1_LEDS	0x0c
#define IMPD1_INT	0x10
#define IMPD1_SW	0x14
#define IMPD1_CTRL	0x18

#define IMPD1_CTRL_DISP_LCD	(0 << 0)
#define IMPD1_CTRL_DISP_VGA	(1 << 0)
#define IMPD1_CTRL_DISP_LCD1	(2 << 0)
#define IMPD1_CTRL_DISP_ENABLE	(1 << 2)
#define IMPD1_CTRL_DISP_MASK	(7 << 0)

struct device;

void impd1_tweak_control(struct device *dev, u32 mask, u32 val);
