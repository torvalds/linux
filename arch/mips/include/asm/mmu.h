#ifndef __ASM_MMU_H
#define __ASM_MMU_H

#include <linux/atomic.h>

typedef struct {
	unsigned long asid[NR_CPUS];
	void *vdso;
	atomic_t fp_mode_switching;
} mm_context_t;

#endif /* __ASM_MMU_H */
