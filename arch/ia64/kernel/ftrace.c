/*
 * Dynamic function tracing support.
 *
 * Copyright (C) 2008 Shaohua Li <shaohua.li@intel.com>
 *
 * For licencing details, see COPYING.
 *
 * Defines low-level handling of mcount calls when the kernel
 * is compiled with the -pg flag. When using dynamic ftrace, the
 * mcount call-sites get patched lazily with NOP till they are
 * enabled. All code mutation routines here take effect atomically.
 */

#include <linux/uaccess.h>
#include <linux/ftrace.h>

#include <asm/cacheflush.h>
#include <asm/patch.h>

/* In IA64, each function will be added below two bundles with -pg option */
static unsigned char __attribute__((aligned(8)))
ftrace_orig_code[MCOUNT_INSN_SIZE] = {
	0x02, 0x40, 0x31, 0x10, 0x80, 0x05, /* alloc r40=ar.pfs,12,8,0 */
	0xb0, 0x02, 0x00, 0x00, 0x42, 0x40, /* mov r43=r0;; */
	0x05, 0x00, 0xc4, 0x00,             /* mov r42=b0 */
	0x11, 0x48, 0x01, 0x02, 0x00, 0x21, /* mov r41=r1 */
	0x00, 0x00, 0x00, 0x02, 0x00, 0x00, /* nop.i 0x0 */
	0x08, 0x00, 0x00, 0x50              /* br.call.sptk.many b0 = _mcount;; */
};

struct ftrace_orig_insn {
	u64 dummy1, dummy2, dummy3;
	u64 dummy4:64-41+13;
	u64 imm20:20;
	u64 dummy5:3;
	u64 sign:1;
	u64 dummy6:4;
};

/* mcount stub will be converted below for nop */
static unsigned char ftrace_nop_code[MCOUNT_INSN_SIZE] = {
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MII] nop.m 0x0 */
	0x30, 0x00, 0x00, 0x60, 0x00, 0x00, /* mov r3=ip */
	0x00, 0x00, 0x04, 0x00,             /* nop.i 0x0 */
	0x05, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MLX] nop.m 0x0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* nop.x 0x0;; */
	0x00, 0x00, 0x04, 0x00
};

static unsigned char *ftrace_nop_replace(void)
{
	return ftrace_nop_code;
}

/*
 * mcount stub will be converted below for call
 * Note: Just the last instruction is changed against nop
 * */
static unsigned char __attribute__((aligned(8)))
ftrace_call_code[MCOUNT_INSN_SIZE] = {
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MII] nop.m 0x0 */
	0x30, 0x00, 0x00, 0x60, 0x00, 0x00, /* mov r3=ip */
	0x00, 0x00, 0x04, 0x00,             /* nop.i 0x0 */
	0x05, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MLX] nop.m 0x0 */
	0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, /* brl.many .;;*/
	0xf8, 0xff, 0xff, 0xc8
};

struct ftrace_call_insn {
	u64 dummy1, dummy2;
	u64 dummy3:48;
	u64 imm39_l:16;
	u64 imm39_h:23;
	u64 dummy4:13;
	u64 imm20:20;
	u64 dummy5:3;
	u64 i:1;
	u64 dummy6:4;
};

static unsigned char *ftrace_call_replace(unsigned long ip, unsigned long addr)
{
	struct ftrace_call_insn *code = (void *)ftrace_call_code;
	unsigned long offset = addr - (ip + 0x10);

	code->imm39_l = offset >> 24;
	code->imm39_h = offset >> 40;
	code->imm20 = offset >> 4;
	code->i = offset >> 63;
	return ftrace_call_code;
}

static int
ftrace_modify_code(unsigned long ip, unsigned char *old_code,
		   unsigned char *new_code, int do_check)
{
	unsigned char replaced[MCOUNT_INSN_SIZE];

	/*
	 * Note:
	 * We are paranoid about modifying text, as if a bug was to happen, it
	 * could cause us to read or write to someplace that could cause harm.
	 * Carefully read and modify the code with probe_kernel_*(), and make
	 * sure what we read is what we expected it to be before modifying it.
	 */

	if (!do_check)
		goto skip_check;

	/* read the text we want to modify */
	if (probe_kernel_read(replaced, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	/* Make sure it is what we expect it to be */
	if (memcmp(replaced, old_code, MCOUNT_INSN_SIZE) != 0)
		return -EINVAL;

skip_check:
	/* replace the text with the new text */
	if (probe_kernel_write(((void *)ip), new_code, MCOUNT_INSN_SIZE))
		return -EPERM;
	flush_icache_range(ip, ip + MCOUNT_INSN_SIZE);

	return 0;
}

static int ftrace_make_nop_check(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned char __attribute__((aligned(8))) replaced[MCOUNT_INSN_SIZE];
	unsigned long ip = rec->ip;

	if (probe_kernel_read(replaced, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;
	if (rec->flags & FTRACE_FL_CONVERTED) {
		struct ftrace_call_insn *call_insn, *tmp_call;

		call_insn = (void *)ftrace_call_code;
		tmp_call = (void *)replaced;
		call_insn->imm39_l = tmp_call->imm39_l;
		call_insn->imm39_h = tmp_call->imm39_h;
		call_insn->imm20 = tmp_call->imm20;
		call_insn->i = tmp_call->i;
		if (memcmp(replaced, ftrace_call_code, MCOUNT_INSN_SIZE) != 0)
			return -EINVAL;
		return 0;
	} else {
		struct ftrace_orig_insn *call_insn, *tmp_call;

		call_insn = (void *)ftrace_orig_code;
		tmp_call = (void *)replaced;
		call_insn->sign = tmp_call->sign;
		call_insn->imm20 = tmp_call->imm20;
		if (memcmp(replaced, ftrace_orig_code, MCOUNT_INSN_SIZE) != 0)
			return -EINVAL;
		return 0;
	}
}

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	int ret;
	char *new;

	ret = ftrace_make_nop_check(rec, addr);
	if (ret)
		return ret;
	new = ftrace_nop_replace();
	return ftrace_modify_code(rec->ip, NULL, new, 0);
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	unsigned char *old, *new;

	old=  ftrace_nop_replace();
	new = ftrace_call_replace(ip, addr);
	return ftrace_modify_code(ip, old, new, 1);
}

/* in IA64, _mcount can't directly call ftrace_stub. Only jump is ok */
int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip;
	unsigned long addr = ((struct fnptr *)ftrace_call)->ip;

	if (func == ftrace_stub)
		return 0;
	ip = ((struct fnptr *)func)->ip;

	ia64_patch_imm64(addr + 2, ip);

	flush_icache_range(addr, addr + 16);
	return 0;
}

/* run from kstop_machine */
int __init ftrace_dyn_arch_init(void)
{
	return 0;
}
