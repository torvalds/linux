#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/elf.h>
#include <linux/crypto.h>
#include <asm/page.h>
#include <asm/mman.h>

#define DEFINE(sym, val) \
	asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define DEFINE_STR1(x) #x
#define DEFINE_STR(sym, val) asm volatile("\n->" #sym " " DEFINE_STR1(val) " " #val: : )

#define BLANK() asm volatile("\n->" : : )

#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem));

#define __NO_STUBS 1
#undef __SYSCALL
#undef _ASM_X86_64_UNISTD_H_
#define __SYSCALL(nr, sym) [nr] = 1,
static char syscalls[] = {
#include <asm/arch/unistd.h>
};

void foo(void)
{
#include <common-offsets.h>
DEFINE(UM_NR_syscall_max, sizeof(syscalls) - 1);
}
