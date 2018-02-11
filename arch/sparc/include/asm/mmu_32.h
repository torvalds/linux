/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MMU_H
#define __MMU_H

/* Default "unsigned long" context */
typedef unsigned long mm_context_t;

/* mm/srmmu.c */
extern ctxd_t *srmmu_ctx_table_phys;

#endif
