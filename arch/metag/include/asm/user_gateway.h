/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2010 Imagination Technologies
 */

#ifndef __ASM_METAG_USER_GATEWAY_H
#define __ASM_METAG_USER_GATEWAY_H

#include <asm/page.h>

/* Page of kernel code accessible to userspace. */
#define USER_GATEWAY_PAGE	0x6ffff000
/* Offset of TLS pointer array in gateway page. */
#define USER_GATEWAY_TLS	0x100

#ifndef __ASSEMBLY__

extern char __user_gateway_start;
extern char __user_gateway_end;

/* Kernel mapping of the gateway page. */
extern void *gateway_page;

static inline void set_gateway_tls(void __user *tls_ptr)
{
	void **gateway_tls = (void **)(gateway_page + USER_GATEWAY_TLS +
				       hard_processor_id() * 4);

	*gateway_tls = (__force void *)tls_ptr;
#ifdef CONFIG_METAG_META12
	/* Avoid cache aliases on virtually tagged cache. */
	__builtin_dcache_flush((void *)USER_GATEWAY_PAGE + USER_GATEWAY_TLS +
				       hard_processor_id() * sizeof(void *));
#endif
}

extern int __kuser_get_tls(void);
extern char *__kuser_get_tls_end[];

extern int __kuser_cmpxchg(int, int, unsigned long *);
extern char *__kuser_cmpxchg_end[];

#endif

#endif
