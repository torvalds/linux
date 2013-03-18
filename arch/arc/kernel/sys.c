
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/unistd.h>

#include <asm/syscalls.h>

#define sys_clone	sys_clone_wrapper
#define sys_fork	sys_fork_wrapper
#define sys_vfork	sys_vfork_wrapper

#undef __SYSCALL
#define __SYSCALL(nr, call) [nr] = (call),

void *sys_call_table[NR_syscalls] = {
	[0 ... NR_syscalls-1] = sys_ni_syscall,
#include <asm/unistd.h>
};
