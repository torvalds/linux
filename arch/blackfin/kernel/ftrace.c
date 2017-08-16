/*
 * ftrace graph code
 *
 * Copyright (C) 2009-2010 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#include <linux/ftrace.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <asm/cacheflush.h>

#ifdef CONFIG_DYNAMIC_FTRACE

static const unsigned char mnop[] = {
	0x03, 0xc0, 0x00, 0x18, /* MNOP; */
	0x03, 0xc0, 0x00, 0x18, /* MNOP; */
};

static void bfin_make_pcrel24(unsigned char *insn, unsigned long src,
                              unsigned long dst)
{
	uint32_t pcrel = (dst - src) >> 1;
	insn[0] = pcrel >> 16;
	insn[1] = 0xe3;
	insn[2] = pcrel;
	insn[3] = pcrel >> 8;
}
#define bfin_make_pcrel24(insn, src, dst) bfin_make_pcrel24(insn, src, (unsigned long)(dst))

static int ftrace_modify_code(unsigned long ip, const unsigned char *code,
                              unsigned long len)
{
	int ret = probe_kernel_write((void *)ip, (void *)code, len);
	flush_icache_range(ip, ip + len);
	return ret;
}

int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec,
                    unsigned long addr)
{
	/* Turn the mcount call site into two MNOPs as those are 32bit insns */
	return ftrace_modify_code(rec->ip, mnop, sizeof(mnop));
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	/* Restore the mcount call site */
	unsigned char call[8];
	call[0] = 0x67; /* [--SP] = RETS; */
	call[1] = 0x01;
	bfin_make_pcrel24(&call[2], rec->ip + 2, addr);
	call[6] = 0x27; /* RETS = [SP++]; */
	call[7] = 0x01;
	return ftrace_modify_code(rec->ip, call, sizeof(call));
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned char call[4];
	unsigned long ip = (unsigned long)&ftrace_call;
	bfin_make_pcrel24(call, ip, func);
	return ftrace_modify_code(ip, call, sizeof(call));
}

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}

#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

# ifdef CONFIG_DYNAMIC_FTRACE

extern void ftrace_graph_call(void);

int ftrace_enable_ftrace_graph_caller(void)
{
	unsigned long ip = (unsigned long)&ftrace_graph_call;
	uint16_t jump_pcrel12 = ((unsigned long)&ftrace_graph_caller - ip) >> 1;
	jump_pcrel12 |= 0x2000;
	return ftrace_modify_code(ip, (void *)&jump_pcrel12, sizeof(jump_pcrel12));
}

int ftrace_disable_ftrace_graph_caller(void)
{
	return ftrace_modify_code((unsigned long)&ftrace_graph_call, empty_zero_page, 2);
}

# endif

/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
                           unsigned long frame_pointer)
{
	struct ftrace_graph_ent trace;
	unsigned long return_hooker = (unsigned long)&return_to_handler;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	if (ftrace_push_return_trace(*parent, self_addr, &trace.depth,
				     frame_pointer, NULL) == -EBUSY)
		return;

	trace.func = self_addr;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace)) {
		current->curr_ret_stack--;
		return;
	}

	/* all is well in the world !  hijack RETS ... */
	*parent = return_hooker;
}

#endif
