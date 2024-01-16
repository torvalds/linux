/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ALPHA_MMU_H
#define __ALPHA_MMU_H

/* The alpha MMU context is one "unsigned long" bitmap per CPU */
typedef unsigned long mm_context_t[NR_CPUS];

#endif
