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

#if defined(CONFIG_ION)
#include <linux/ion.h>
#endif

#define MAX_NAME_LEN	20

struct secchunk_info {
	int		index;
	char		name[MAX_NAME_LEN];
	phys_addr_t	base;
	size_t		size;
};

#ifdef CONFIG_EXYNOS5_DEV_GSC
extern struct platform_device exynos5_device_gsc0;
#endif

#if defined(CONFIG_ION)
extern struct ion_device *ion_exynos;
struct secfd_info {
	int	fd;
	ion_phys_addr_t phys;
};
#endif

struct secmem_crypto_driver_ftn {
	int (*lock) (void);
	int (*release) (void);
};

struct secmem_region {
	char		*virt_addr;
	unsigned long	phys_addr;
	unsigned long	len;
};

#if defined(CONFIG_EXYNOS_CONTENT_PATH_PROTECTION)
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
#if defined(CONFIG_SOC_EXYNOS5250) || defined(CONFIG_SOC_EXYNOS5410)
#define SECMEM_IOC_GET_FD_PHYS_ADDR    _IOWR('S', 8, struct secfd_info)
#endif
#define SECMEM_IOC_GET_CHUNK_NUM	_IOWR('S', 9, int)
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
#define SECMEM_IOC_REQ_MIF_LOCK		_IOWR('S', 10, int)
#endif

#endif /* __ASM_ARCH_SECMEM_H */
