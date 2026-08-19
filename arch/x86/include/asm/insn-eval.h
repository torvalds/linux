#ifndef _ASM_X86_INSN_EVAL_H
#define _ASM_X86_INSN_EVAL_H
/*
 * A collection of utility functions for x86 instruction analysis to be
 * used in a kernel context. Useful when, for instance, making sense
 * of the registers indicated by operands.
 */

#include <linux/compiler.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <asm/insn.h>
#include <asm/ptrace.h>

#define INSN_CODE_SEG_ADDR_SZ(params) ((params >> 4) & 0xf)
#define INSN_CODE_SEG_OPND_SZ(params) (params & 0xf)
#define INSN_CODE_SEG_PARAMS(oper_sz, addr_sz) (oper_sz | (addr_sz << 4))

int pt_regs_offset(struct pt_regs *regs, int regno);

bool insn_has_rep_prefix(struct insn *insn);
void __user *insn_get_addr_ref(struct insn *insn, struct pt_regs *regs);
int insn_get_modrm_rm_off(struct insn *insn, struct pt_regs *regs);
int insn_get_modrm_reg_off(struct insn *insn, struct pt_regs *regs);
unsigned long *insn_get_modrm_reg_ptr(struct insn *insn, struct pt_regs *regs);
unsigned long insn_get_seg_base(struct pt_regs *regs, int seg_reg_idx);
int insn_get_code_seg_params(struct pt_regs *regs);
int insn_get_effective_ip(struct pt_regs *regs, unsigned long *ip);
int insn_fetch_from_user(struct pt_regs *regs,
			 unsigned char buf[MAX_INSN_SIZE]);
int insn_fetch_from_user_inatomic(struct pt_regs *regs,
				  unsigned char buf[MAX_INSN_SIZE]);
bool insn_decode_from_regs(struct insn *insn, struct pt_regs *regs,
			   unsigned char buf[MAX_INSN_SIZE], int buf_size);

enum insn_mmio_type {
	INSN_MMIO_DECODE_FAILED,
	INSN_MMIO_WRITE,
	INSN_MMIO_WRITE_IMM,
	INSN_MMIO_READ,
	INSN_MMIO_READ_ZERO_EXTEND,
	INSN_MMIO_READ_SIGN_EXTEND,
	INSN_MMIO_MOVS,
};

enum insn_mmio_type insn_decode_mmio(struct insn *insn, int *bytes);

bool insn_is_nop(struct insn *insn);

/*
 * Write @val into *@reg following the x86 rules for writes to
 * general-purpose registers (Intel SDM Vol. 1, "General-Purpose
 * Registers in 64-Bit Mode"): an 8- or 16-bit write leaves the rest of
 * the register untouched, a 32-bit write zero-extends the result into
 * the upper 32 bits, and a 64-bit write replaces the whole register.
 *
 * @bytes is the width of the write, not a property of the instruction:
 * an instruction that, say, sign-extends a 32-bit immediate into a
 * 64-bit register does a 64-bit write here.
 *
 * @reg need not be 8-byte aligned: KVM's instruction emulator offsets
 * the pointer by one byte to address the high-byte registers (AH, CH,
 * DH, BH).  Use narrow stores for the sub-word cases so the access
 * width matches @bytes and the adjacent bytes are left alone.
 */
static inline void insn_assign_reg(unsigned long *reg, u64 val, int bytes)
{
	switch (bytes) {
	case 1:
		*(u8 *)reg = (u8)val;
		break;
	case 2:
		*(u16 *)reg = (u16)val;
		break;
	case 4:
		/* A 32-bit write zero-extends into the upper 32 bits. */
		*reg = (u32)val;
		break;
	case 8:
		*reg = val;
		break;
	}
}

#endif /* _ASM_X86_INSN_EVAL_H */
