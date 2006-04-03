#include "linux/sched.h"

void debug_arch_force_load_TLS(void)
{
}

void clear_flushed_tls(struct task_struct *task)
{
}

int arch_copy_tls(struct task_struct *t)
{
        return 0;
}
