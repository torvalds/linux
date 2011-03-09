/* arch/arm/mach-s5pv210/include/mach/regs-sys.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV210 - System registers definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define S5PV210_USB_PHY_CON	(S3C_VA_SYS + 0xE80C)
#define S5PV210_USB_PHY0_EN	(1 << 0)
#define S5PV210_USB_PHY1_EN	(1 << 1)

/* compatibility defines for s3c-hsotg driver */
#define S3C64XX_OTHERS		S5PV210_USB_PHY_CON
#define S3C64XX_OTHERS_USBMASK	S5PV210_USB_PHY0_EN
