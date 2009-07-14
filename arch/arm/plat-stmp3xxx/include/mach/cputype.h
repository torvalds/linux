/*
 * Freescale STMP37XX/STMP378X CPU type detection
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
#ifndef __ASM_PLAT_CPU_H
#define __ASM_PLAT_CPU_H

#ifdef CONFIG_ARCH_STMP37XX
#define cpu_is_stmp37xx()	(1)
#else
#define cpu_is_stmp37xx()	(0)
#endif

#ifdef CONFIG_ARCH_STMP378X
#define cpu_is_stmp378x()	(1)
#else
#define cpu_is_stmp378x()	(0)
#endif

#endif /* __ASM_PLAT_CPU_H */
