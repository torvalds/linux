// SPDX-License-Identifier: GPL-2.0
/*
 * Based on arch/arm/mm/extable.c
 */

#include <linux/bitfield.h>
#include <linux/extable.h>
#include <linux/uaccess.h>

#include <asm/asm-extable.h>
#include <asm/esr.h>
#include <asm/ptrace.h>

static bool cpy_faulted_on_uaccess(const struct exception_table_entry *ex,
				   unsigned long esr)
{
	bool uaccess_is_write = FIELD_GET(EX_DATA_UACCESS_WRITE, ex->data);
	bool fault_on_write = esr & ESR_ELx_WNR;

	return uaccess_is_write == fault_on_write;
}

bool insn_may_access_user(unsigned long addr, unsigned long esr)
{
	const struct exception_table_entry *ex = search_exception_tables(addr);

	if (!ex)
		return false;

	switch (ex->type) {
	case EX_TYPE_UACCESS_CPY:
		return cpy_faulted_on_uaccess(ex, esr);
	default:
		return true;
	}
}

static inline unsigned long
get_ex_fixup(const struct exception_table_entry *ex)
{
	return ((unsigned long)&ex->fixup + ex->fixup);
}

static bool ex_handler_uaccess_err_zero(const struct exception_table_entry *ex,
					struct pt_regs *regs)
{
	int reg_err = FIELD_GET(EX_DATA_REG_ERR, ex->data);
	int reg_zero = FIELD_GET(EX_DATA_REG_ZERO, ex->data);

	pt_regs_write_reg(regs, reg_err, -EFAULT);
	pt_regs_write_reg(regs, reg_zero, 0);

	regs->pc = get_ex_fixup(ex);
	return true;
}

static bool ex_handler_uaccess_cpy(const struct exception_table_entry *ex,
				   struct pt_regs *regs, unsigned long esr)
{
	/* Do not fix up faults on kernel memory accesses */
	if (!cpy_faulted_on_uaccess(ex, esr))
		return false;

	regs->pc = get_ex_fixup(ex);
	return true;
}

static bool
ex_handler_load_unaligned_zeropad(const struct exception_table_entry *ex,
				  struct pt_regs *regs)
{
	int reg_data = FIELD_GET(EX_DATA_REG_DATA, ex->data);
	int reg_addr = FIELD_GET(EX_DATA_REG_ADDR, ex->data);
	unsigned long data, addr, offset;

	addr = pt_regs_read_reg(regs, reg_addr);

	offset = addr & 0x7UL;
	addr &= ~0x7UL;

	data = *(unsigned long*)addr;

#ifndef __AARCH64EB__
	data >>= 8 * offset;
#else
	data <<= 8 * offset;
#endif

	pt_regs_write_reg(regs, reg_data, data);

	regs->pc = get_ex_fixup(ex);
	return true;
}

bool fixup_exception(struct pt_regs *regs, unsigned long esr)
{
	const struct exception_table_entry *ex;

	ex = search_exception_tables(instruction_pointer(regs));
	if (!ex)
		return false;

	switch (ex->type) {
	case EX_TYPE_BPF:
		return ex_handler_bpf(ex, regs);
	case EX_TYPE_UACCESS_ERR_ZERO:
	case EX_TYPE_KACCESS_ERR_ZERO:
		return ex_handler_uaccess_err_zero(ex, regs);
	case EX_TYPE_UACCESS_CPY:
		return ex_handler_uaccess_cpy(ex, regs, esr);
	case EX_TYPE_LOAD_UNALIGNED_ZEROPAD:
		return ex_handler_load_unaligned_zeropad(ex, regs);
	}

	BUG();
}
