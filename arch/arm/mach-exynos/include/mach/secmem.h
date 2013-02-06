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
#if defined(CONFIG_ION)
#include <linux/ion.h>
#endif

struct secchunk_info {
	int		index;
	phys_addr_t	base;
	size_t		size;
};

struct secmem_fd_info {
	uint32_t phys_addr;
	size_t size;
};

struct secmem_fd_list {
	struct secmem_fd_list *next;
	struct secmem_fd_list *prev;
	struct secmem_fd_info fdinfo;
};

extern struct miscdevice secmem;
#if defined(CONFIG_ION)
struct secfd_info {
	int             fd;
	ion_phys_addr_t phys;
};
#endif


struct secmem_crypto_driver_ftn {
	int (*lock) (void);
	int (*release) (void);
};

struct secmem_region {
	char		*virt_addr;
	dma_addr_t	phys_addr;
	unsigned long	len;
};

#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412) || \
		defined(CONFIG_CPU_EXYNOS5250)
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
#if defined(CONFIG_CPU_EXYNOS5250)
#define SECMEM_IOC_GET_FD_PHYS_ADDR	_IOWR('S', 8, int)
#endif

#define SECMEM_IOC_MFC_MAGIC_KEY	_IOWR('S', 9, int)
#define SECMEM_IOC_TEXT_CHUNKINFO	_IOWR('S', 10, struct secchunk_info)

#endif /* __ASM_ARCH_SECMEM_H */
