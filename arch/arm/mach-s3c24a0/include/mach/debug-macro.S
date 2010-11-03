/* arch/arm/mach-s3c2410/include/mach/debug-macro.S
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* pull in the relevant register and map files. */

#include <mach/map.h>
#include <plat/regs-serial.h>

	.macro addruart, rp, rv
		ldr	\rp, = S3C24XX_PA_UART
		ldr	\rv, = S3C24XX_VA_UART
#if CONFIG_DEBUG_S3C_UART != 0
		add	\rp, \rp, #(S3C2410_UART1_OFF * CONFIG_DEBUG_S3C_UART)
		add	\rv, \rv, #(S3C2410_UART1_OFF * CONFIG_DEBUG_S3C_UART)
#endif
	.endm

/* include the reset of the code which will do the work, we're only
 * compiling for a single cpu processor type so the default of s3c2440
 * will be fine with us.
 */

#include <plat/debug-macro.S>
