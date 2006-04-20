#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/elf.h>
#include <asm/mman.h>

#define DEFINE(sym, val) \
	asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define STR(x) #x
#define DEFINE_STR(sym, val) asm volatile("\n->" #sym " " STR(val) " " #val: : )

#define BLANK() asm volatile("\n->" : : )

#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem));

void foo(void)
{
	OFFSET(HOST_TASK_DEBUGREGS, task_struct, thread.arch.debugregs);
	DEFINE(KERNEL_MADV_REMOVE, MADV_REMOVE);
#ifdef CONFIG_MODE_TT
	OFFSET(HOST_TASK_EXTERN_PID, task_struct, thread.mode.tt.extern_pid);
#endif
#include <common-offsets.h>
}
