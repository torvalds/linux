/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __M68K_PGTABLE_H
#define __M68K_PGTABLE_H

#include <asm/page.h>

#ifdef __uClinux__
#include <asm/pgtable_no.h>
#else
#include <asm/pgtable_mm.h>
#endif

#ifndef __ASSEMBLY__
extern void paging_init(void);
#endif

#endif /* __M68K_PGTABLE_H */
