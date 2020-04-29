/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/page.h>
#include <abi/entry.h>

__HEAD
ENTRY(_start)
	SETUP_MMU

	/* set stack point */
	lrw     r6, init_thread_union + THREAD_SIZE
	mov	sp, r6

	jmpi	csky_start
END(_start)

#ifdef CONFIG_SMP
.align 10
ENTRY(_start_smp_secondary)
	SETUP_MMU

	/* copy msa1 from CPU0 */
	lrw     r6, secondary_msa1
	ld.w	r6, (r6, 0)
	mtcr	r6, cr<31, 15>

	/* set stack point */
	lrw     r6, secondary_stack
	ld.w	r6, (r6, 0)
	mov	sp, r6

	jmpi	csky_start_secondary
END(_start_smp_secondary)
#endif
