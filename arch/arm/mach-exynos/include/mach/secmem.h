/* linux/arch/arm/mach-exynos/include/mach/secmem.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - Secure memory support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_SECMEM_H
#define __ASM_ARCH_SECMEM_H __FILE__

#include <linux/miscdevice.h>

struct secchunk_info {
	int		index;
	phys_addr_t	base;
	size_t		size;
};

extern struct miscdevice secmem;

struct secmem_crypto_driver_ftn {
	int (*lock) (void);
	int (*release) (void);
};

struct secmem_region {
	char		*virt_addr;
	unsigned long	phys_addr;
	unsigned long	len;
};

#if defined(CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION)
void secmem_crypto_register(struct secmem_crypto_driver_ftn *ftn);
void secmem_crypto_deregister(void);
#else
#define secmem_crypto_register(ftn)
#define secmem_crypto_deregister()
#endif

#define SECMEM_IOC_CHUNKINFO		_IOWR('S', 1, struct secchunk_info)
#define SECMEM_IOC_SET_DRM_ONOFF	_IOWR('S', 2, int)
#define SECMEM_IOC_GET_DRM_ONOFF	_IOWR('S', 3, int)
#define SECMEM_IOC_GET_CRYPTO_LOCK	_IOR('S', 4, int)
#define SECMEM_IOC_RELEASE_CRYPTO_LOCK	_IOR('S', 5, int)
#define SECMEM_IOC_GET_ADDR		_IOWR('S', 6, int)
#define SECMEM_IOC_RELEASE_ADDR		_IOWR('S', 7, int)

#endif /* __ASM_ARCH_SECMEM_H */
