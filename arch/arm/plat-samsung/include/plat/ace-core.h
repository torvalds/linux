/* linux/arch/arm/plat-s5p/include/plat/ace-core.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Samsung Advanced Crypto Engine core function
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_ACE_CORE_H
#define __ASM_PLAT_ACE_CORE_H __FILE__

/* These functions are only for use with the core support code, such as
 * the cpu specific initialisation code
 */

/* re-define device name depending on support. */
static inline void s5p_ace_setname(char *name)
{
	s5p_device_ace.name = name;
}

#endif /* __ASM_PLAT_ACE_CORE_H */
