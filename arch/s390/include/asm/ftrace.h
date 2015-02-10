#ifndef _ASM_S390_FTRACE_H
#define _ASM_S390_FTRACE_H

#define ARCH_SUPPORTS_FTRACE_OPS 1

#define MCOUNT_INSN_SIZE	24
#define MCOUNT_RETURN_FIXUP	18

#ifndef __ASSEMBLY__

#define ftrace_return_address(n) __builtin_return_address(n)

void _mcount(void);
void ftrace_caller(void);

extern char ftrace_graph_caller_end;
extern unsigned long ftrace_plt;

struct dyn_arch_ftrace { };

#define MCOUNT_ADDR ((unsigned long)_mcount)
#define FTRACE_ADDR ((unsigned long)ftrace_caller)

#define KPROBE_ON_FTRACE_NOP	0
#define KPROBE_ON_FTRACE_CALL	1

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr;
}

struct ftrace_insn {
	u16 opc;
	s32 disp;
} __packed;

static inline void ftrace_generate_nop_insn(struct ftrace_insn *insn)
{
#ifdef CONFIG_FUNCTION_TRACER
	/* jg .+24 */
	insn->opc = 0xc0f4;
	insn->disp = MCOUNT_INSN_SIZE / 2;
#endif
}

static inline int is_ftrace_nop(struct ftrace_insn *insn)
{
#ifdef CONFIG_FUNCTION_TRACER
	if (insn->disp == MCOUNT_INSN_SIZE / 2)
		return 1;
#endif
	return 0;
}

static inline void ftrace_generate_call_insn(struct ftrace_insn *insn,
					     unsigned long ip)
{
#ifdef CONFIG_FUNCTION_TRACER
	unsigned long target;

	/* brasl r0,ftrace_caller */
	target = is_module_addr((void *) ip) ? ftrace_plt : FTRACE_ADDR;
	insn->opc = 0xc005;
	insn->disp = (target - ip) / 2;
#endif
}

#endif /* __ASSEMBLY__ */
#endif /* _ASM_S390_FTRACE_H */
