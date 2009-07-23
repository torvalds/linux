/*
 * Freescale STMP37XX/STMP378X internal functions and data declarations
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __MACH_STMP378X_H
#define __MACH_STMP378X_H

void stmp378x_map_io(void);
void stmp378x_init_irq(void);

extern struct platform_device stmp378x_pxp, stmp378x_i2c;
#endif /* __MACH_STMP378X_COMMON_H */
