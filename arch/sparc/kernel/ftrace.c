#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/list.h>
#include <trace/syscall.h>

#include <asm/ftrace.h>

#ifdef CONFIG_DYNAMIC_FTRACE
static const u32 ftrace_nop = 0x01000000;

static u32 ftrace_call_replace(unsigned long ip, unsigned long addr)
{
	static u32 call;
	s32 off;

	off = ((s32)addr - (s32)ip);
	call = 0x40000000 | ((u32)off >> 2);

	return call;
}

static int ftrace_modify_code(unsigned long ip, u32 old, u32 new)
{
	u32 replaced;
	int faulted;

	__asm__ __volatile__(
	"1:	cas	[%[ip]], %[old], %[new]\n"
	"	flush	%[ip]\n"
	"	mov	0, %[faulted]\n"
	"2:\n"
	"	.section .fixup,#alloc,#execinstr\n"
	"	.align	4\n"
	"3:	sethi	%%hi(2b), %[faulted]\n"
	"	jmpl	%[faulted] + %%lo(2b), %%g0\n"
	"	 mov	1, %[faulted]\n"
	"	.previous\n"
	"	.section __ex_table,\"a\"\n"
	"	.align	4\n"
	"	.word	1b, 3b\n"
	"	.previous\n"
	: "=r" (replaced), [faulted] "=r" (faulted)
	: [new] "0" (new), [old] "r" (old), [ip] "r" (ip)
	: "memory");

	if (replaced != old && replaced != new)
		faulted = 2;

	return faulted;
}

int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	u32 old, new;

	old = ftrace_call_replace(ip, addr);
	new = ftrace_nop;
	return ftrace_modify_code(ip, old, new);
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	u32 old, new;

	old = ftrace_nop;
	new = ftrace_call_replace(ip, addr);
	return ftrace_modify_code(ip, old, new);
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip = (unsigned long)(&ftrace_call);
	u32 old, new;

	old = *(u32 *) &ftrace_call;
	new = ftrace_call_replace(ip, (unsigned long)func);
	return ftrace_modify_code(ip, old, new);
}

int __init ftrace_dyn_arch_init(void *data)
{
	unsigned long *p = data;

	*p = 0;

	return 0;
}
#endif

#ifdef CONFIG_FTRACE_SYSCALLS

extern unsigned int sys_call_table[];

unsigned long __init arch_syscall_addr(int nr)
{
	return (unsigned long)sys_call_table[nr];
}

#endif
