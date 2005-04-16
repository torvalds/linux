/*
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 */

#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <asm/thread_info.h>

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int main(void)
{
	/* offsets into the thread_info struct */
	DEFINE(TI_TASK,		offsetof(struct thread_info, task));
	DEFINE(TI_EXEC_DOMAIN,	offsetof(struct thread_info, exec_domain));
	DEFINE(TI_FLAGS,	offsetof(struct thread_info, flags));
	DEFINE(TI_CPU,		offsetof(struct thread_info, cpu));
	DEFINE(TI_PRE_COUNT,	offsetof(struct thread_info, preempt_count));
	DEFINE(TI_RESTART_BLOCK,offsetof(struct thread_info, restart_block));

	return 0;
}
