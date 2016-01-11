/*
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S3C -  Nand Controller core functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_NAND_CORE_H
#define __ASM_ARCH_NAND_CORE_H __FILE__

/* These functions are only for use with the core support code, such as
 * the cpu specific initialisation code
 */

/* re-define device name depending on support. */
static inline void s3c_nand_setname(char *name)
{
#ifdef CONFIG_S3C_DEV_NAND
	s3c_device_nand.name = name;
#endif
}

#endif /* __ASM_ARCH_NAND_CORE_H */
