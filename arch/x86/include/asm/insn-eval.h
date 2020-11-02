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
#include <asm/ptrace.h>

#define INSN_CODE_SEG_ADDR_SZ(params) ((params >> 4) & 0xf)
#define INSN_CODE_SEG_OPND_SZ(params) (params & 0xf)
#define INSN_CODE_SEG_PARAMS(oper_sz, addr_sz) (oper_sz | (addr_sz << 4))

bool insn_has_rep_prefix(struct insn *insn);
void __user *insn_get_addr_ref(struct insn *insn, struct pt_regs *regs);
int insn_get_modrm_rm_off(struct insn *insn, struct pt_regs *regs);
int insn_get_modrm_reg_off(struct insn *insn, struct pt_regs *regs);
unsigned long insn_get_seg_base(struct pt_regs *regs, int seg_reg_idx);
int insn_get_code_seg_params(struct pt_regs *regs);
unsigned long insn_get_effective_ip(struct pt_regs *regs);
int insn_fetch_from_user(struct pt_regs *regs,
			 unsigned char buf[MAX_INSN_SIZE]);
int insn_fetch_from_user_inatomic(struct pt_regs *regs,
				  unsigned char buf[MAX_INSN_SIZE]);
bool insn_decode_from_regs(struct insn *insn, struct pt_regs *regs,
			   unsigned char buf[MAX_INSN_SIZE], int buf_size);

#endif /* _ASM_X86_INSN_EVAL_H */
