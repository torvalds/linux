#include <linux/thread_info.h>
#include <linux/kbuild.h>

int foo(void)
{
	OFFSET(TI_TASK, thread_info, task);
	OFFSET(TI_FLAGS, thread_info, flags);
	OFFSET(TI_STATUS, thread_info, status);
	OFFSET(TI_CPU, thread_info, cpu);
	OFFSET(TI_PRE_COUNT, thread_info, preempt_count);
	OFFSET(TI_ADDR_LIMIT, thread_info, addr_limit);

	return 0;
}
