// SPDX-License-Identifier: GPL-2.0
#include <asm/insn.h>
#include <linux/mm.h>

#include "perf_event.h"

static int decode_branch_type(struct insn *insn)
{
	int ext;

	if (insn_get_opcode(insn))
		return X86_BR_ABORT;

	switch (insn->opcode.bytes[0]) {
	case 0xf:
		switch (insn->opcode.bytes[1]) {
		case 0x05: /* syscall */
		case 0x34: /* sysenter */
			return X86_BR_SYSCALL;
		case 0x07: /* sysret */
		case 0x35: /* sysexit */
			return X86_BR_SYSRET;
		case 0x80 ... 0x8f: /* conditional */
			return X86_BR_JCC;
		}
		return X86_BR_NONE;
	case 0x70 ... 0x7f: /* conditional */
		return X86_BR_JCC;
	case 0xc2: /* near ret */
	case 0xc3: /* near ret */
	case 0xca: /* far ret */
	case 0xcb: /* far ret */
		return X86_BR_RET;
	case 0xcf: /* iret */
		return X86_BR_IRET;
	case 0xcc ... 0xce: /* int */
		return X86_BR_INT;
	case 0xe8: /* call near rel */
		if (insn_get_immediate(insn) || insn->immediate1.value == 0) {
			/* zero length call */
			return X86_BR_ZERO_CALL;
		}
		fallthrough;
	case 0x9a: /* call far absolute */
		return X86_BR_CALL;
	case 0xe0 ... 0xe3: /* loop jmp */
		return X86_BR_JCC;
	case 0xe9 ... 0xeb: /* jmp */
		return X86_BR_JMP;
	case 0xff: /* call near absolute, call far absolute ind */
		if (insn_get_modrm(insn))
			return X86_BR_ABORT;

		ext = (insn->modrm.bytes[0] >> 3) & 0x7;
		switch (ext) {
		case 2: /* near ind call */
		case 3: /* far ind call */
			return X86_BR_IND_CALL;
		case 4:
		case 5:
			return X86_BR_IND_JMP;
		}
		return X86_BR_NONE;
	}

	return X86_BR_NONE;
}

/*
 * return the type of control flow change at address "from"
 * instruction is not necessarily a branch (in case of interrupt).
 *
 * The branch type returned also includes the priv level of the
 * target of the control flow change (X86_BR_USER, X86_BR_KERNEL).
 *
 * If a branch type is unknown OR the instruction cannot be
 * decoded (e.g., text page not present), then X86_BR_NONE is
 * returned.
 *
 * While recording branches, some processors can report the "from"
 * address to be that of an instruction preceding the actual branch
 * when instruction fusion occurs. If fusion is expected, attempt to
 * find the type of the first branch instruction within the next
 * MAX_INSN_SIZE bytes and if found, provide the offset between the
 * reported "from" address and the actual branch instruction address.
 */
static int get_branch_type(unsigned long from, unsigned long to, int abort,
			   bool fused, int *offset)
{
	struct insn insn;
	void *addr;
	int bytes_read, bytes_left, insn_offset;
	int ret = X86_BR_NONE;
	int to_plm, from_plm;
	u8 buf[MAX_INSN_SIZE];
	int is64 = 0;

	/* make sure we initialize offset */
	if (offset)
		*offset = 0;

	to_plm = kernel_ip(to) ? X86_BR_KERNEL : X86_BR_USER;
	from_plm = kernel_ip(from) ? X86_BR_KERNEL : X86_BR_USER;

	/*
	 * maybe zero if lbr did not fill up after a reset by the time
	 * we get a PMU interrupt
	 */
	if (from == 0 || to == 0)
		return X86_BR_NONE;

	if (abort)
		return X86_BR_ABORT | to_plm;

	if (from_plm == X86_BR_USER) {
		/*
		 * can happen if measuring at the user level only
		 * and we interrupt in a kernel thread, e.g., idle.
		 */
		if (!current->mm)
			return X86_BR_NONE;

		/* may fail if text not present */
		bytes_left = copy_from_user_nmi(buf, (void __user *)from,
						MAX_INSN_SIZE);
		bytes_read = MAX_INSN_SIZE - bytes_left;
		if (!bytes_read)
			return X86_BR_NONE;

		addr = buf;
	} else {
		/*
		 * The LBR logs any address in the IP, even if the IP just
		 * faulted. This means userspace can control the from address.
		 * Ensure we don't blindly read any address by validating it is
		 * a known text address and not a vsyscall address.
		 */
		if (kernel_text_address(from) && !in_gate_area_no_mm(from)) {
			addr = (void *)from;
			/*
			 * Assume we can get the maximum possible size
			 * when grabbing kernel data.  This is not
			 * _strictly_ true since we could possibly be
			 * executing up next to a memory hole, but
			 * it is very unlikely to be a problem.
			 */
			bytes_read = MAX_INSN_SIZE;
		} else {
			return X86_BR_NONE;
		}
	}

	/*
	 * decoder needs to know the ABI especially
	 * on 64-bit systems running 32-bit apps
	 */
#ifdef CONFIG_X86_64
	is64 = kernel_ip((unsigned long)addr) || any_64bit_mode(current_pt_regs());
#endif
	insn_init(&insn, addr, bytes_read, is64);
	ret = decode_branch_type(&insn);
	insn_offset = 0;

	/* Check for the possibility of branch fusion */
	while (fused && ret == X86_BR_NONE) {
		/* Check for decoding errors */
		if (insn_get_length(&insn) || !insn.length)
			break;

		insn_offset += insn.length;
		bytes_read -= insn.length;
		if (bytes_read < 0)
			break;

		insn_init(&insn, addr + insn_offset, bytes_read, is64);
		ret = decode_branch_type(&insn);
	}

	if (offset)
		*offset = insn_offset;

	/*
	 * interrupts, traps, faults (and thus ring transition) may
	 * occur on any instructions. Thus, to classify them correctly,
	 * we need to first look at the from and to priv levels. If they
	 * are different and to is in the kernel, then it indicates
	 * a ring transition. If the from instruction is not a ring
	 * transition instr (syscall, systenter, int), then it means
	 * it was a irq, trap or fault.
	 *
	 * we have no way of detecting kernel to kernel faults.
	 */
	if (from_plm == X86_BR_USER && to_plm == X86_BR_KERNEL
	    && ret != X86_BR_SYSCALL && ret != X86_BR_INT)
		ret = X86_BR_IRQ;

	/*
	 * branch priv level determined by target as
	 * is done by HW when LBR_SELECT is implemented
	 */
	if (ret != X86_BR_NONE)
		ret |= to_plm;

	return ret;
}

int branch_type(unsigned long from, unsigned long to, int abort)
{
	return get_branch_type(from, to, abort, false, NULL);
}

int branch_type_fused(unsigned long from, unsigned long to, int abort,
		      int *offset)
{
	return get_branch_type(from, to, abort, true, offset);
}

#define X86_BR_TYPE_MAP_MAX	16

static int branch_map[X86_BR_TYPE_MAP_MAX] = {
	PERF_BR_CALL,		/* X86_BR_CALL */
	PERF_BR_RET,		/* X86_BR_RET */
	PERF_BR_SYSCALL,	/* X86_BR_SYSCALL */
	PERF_BR_SYSRET,		/* X86_BR_SYSRET */
	PERF_BR_UNKNOWN,	/* X86_BR_INT */
	PERF_BR_ERET,		/* X86_BR_IRET */
	PERF_BR_COND,		/* X86_BR_JCC */
	PERF_BR_UNCOND,		/* X86_BR_JMP */
	PERF_BR_IRQ,		/* X86_BR_IRQ */
	PERF_BR_IND_CALL,	/* X86_BR_IND_CALL */
	PERF_BR_UNKNOWN,	/* X86_BR_ABORT */
	PERF_BR_UNKNOWN,	/* X86_BR_IN_TX */
	PERF_BR_NO_TX,		/* X86_BR_NO_TX */
	PERF_BR_CALL,		/* X86_BR_ZERO_CALL */
	PERF_BR_UNKNOWN,	/* X86_BR_CALL_STACK */
	PERF_BR_IND,		/* X86_BR_IND_JMP */
};

int common_branch_type(int type)
{
	int i;

	type >>= 2; /* skip X86_BR_USER and X86_BR_KERNEL */

	if (type) {
		i = __ffs(type);
		if (i < X86_BR_TYPE_MAP_MAX)
			return branch_map[i];
	}

	return PERF_BR_UNKNOWN;
}
