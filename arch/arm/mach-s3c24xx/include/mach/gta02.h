#ifndef _GTA02_H
#define _GTA02_H

#include <mach/regs-gpio.h>

#define GTA02_GPIO_AUX_LED	S3C2410_GPB(2)
#define GTA02_GPIO_USB_PULLUP	S3C2410_GPB(9)
#define GTA02_GPIO_AUX_KEY	S3C2410_GPF(6)
#define GTA02_GPIO_HOLD_KEY	S3C2410_GPF(7)
#define GTA02_GPIO_AMP_SHUT	S3C2410_GPJ(1)	/* v2 + v3 + v4 only */
#define GTA02_GPIO_HP_IN	S3C2410_GPJ(2)	/* v2 + v3 + v4 only */

#define GTA02_IRQ_PCF50633	IRQ_EINT9

#endif /* _GTA02_H */
