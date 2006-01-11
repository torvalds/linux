#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/elf.h>
#include <asm/page.h>

#define DEFINE(sym, val) \
	asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define DEFINE_STR1(x) #x
#define DEFINE_STR(sym, val) asm volatile("\n->" #sym " " DEFINE_STR1(val) " " #val: : )

#define BLANK() asm volatile("\n->" : : )

#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem));

void foo(void)
{
#ifdef CONFIG_MODE_TT
	OFFSET(HOST_TASK_EXTERN_PID, task_struct, thread.mode.tt.extern_pid);
#endif
#include <common-offsets.h>
}
