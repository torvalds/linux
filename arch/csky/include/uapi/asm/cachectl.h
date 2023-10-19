/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef __ASM_CSKY_CACHECTL_H
#define __ASM_CSKY_CACHECTL_H

/*
 * See "man cacheflush"
 */
#define ICACHE  (1<<0)
#define DCACHE  (1<<1)
#define BCACHE  (ICACHE|DCACHE)

#endif /* __ASM_CSKY_CACHECTL_H */
