#ifndef _ASM_X86_VDSO32_H
#define _ASM_X86_VDSO32_H

#define VDSO_BASE_PAGE	0
#define VDSO_VVAR_PAGE	1
#define VDSO_HPET_PAGE	2
#define VDSO_PAGES	3
#define VDSO_PREV_PAGES	2
#define VDSO_OFFSET(x)	((x) * PAGE_SIZE)

#endif
