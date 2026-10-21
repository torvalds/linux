// SPDX-License-Identifier: GPL-2.0

#include <linux/bitfield.h>
#include <linux/extable.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/panic.h>
#include <asm/asm-extable.h>
#include <asm/extable.h>
#include <asm/fpu.h>

const struct exception_table_entry *s390_search_extables(unsigned long addr)
{
	const struct exception_table_entry *fixup;
	size_t num;

	fixup = search_exception_tables(addr);
	if (fixup)
		return fixup;
	num = __stop_amode31_ex_table - __start_amode31_ex_table;
	return search_extable(__start_amode31_ex_table, num, addr);
}

static bool ex_handler_fixup(const struct exception_table_entry *ex, struct pt_regs *regs)
{
	regs->psw.addr = extable_fixup(ex);
	return true;
}

static bool ex_handler_ua_fault(const struct exception_table_entry *ex, struct pt_regs *regs)
{
	unsigned int reg_err = FIELD_GET(EX_DATA_REG_ERR, ex->data);

	regs->gprs[reg_err] = -EFAULT;
	regs->psw.addr = extable_fixup(ex);
	return true;
}

static bool ex_handler_ua_load_reg(const struct exception_table_entry *ex,
				   bool pair, struct pt_regs *regs)
{
	unsigned int reg_zero = FIELD_GET(EX_DATA_REG_ADDR, ex->data);
	unsigned int reg_err = FIELD_GET(EX_DATA_REG_ERR, ex->data);

	regs->gprs[reg_err] = -EFAULT;
	regs->gprs[reg_zero] = 0;
	if (pair)
		regs->gprs[reg_zero + 1] = 0;
	regs->psw.addr = extable_fixup(ex);
	return true;
}

static bool ex_handler_zeropad(const struct exception_table_entry *ex, struct pt_regs *regs)
{
	unsigned int reg_addr = FIELD_GET(EX_DATA_REG_ADDR, ex->data);
	unsigned int reg_data = FIELD_GET(EX_DATA_REG_ERR, ex->data);
	unsigned long data, addr, offset;

	addr = regs->gprs[reg_addr];
	offset = addr & (sizeof(unsigned long) - 1);
	addr &= ~(sizeof(unsigned long) - 1);
	data = *(unsigned long *)addr;
	data <<= BITS_PER_BYTE * offset;
	regs->gprs[reg_data] = data;
	regs->psw.addr = extable_fixup(ex);
	return true;
}

static bool ex_handler_fpc(const struct exception_table_entry *ex, struct pt_regs *regs)
{
	fpu_sfpc(0);
	regs->psw.addr = extable_fixup(ex);
	return true;
}

struct insn_ssf {
	u64	opc1 : 8;
	u64	r3   : 4;
	u64	opc2 : 4;
	u64	b1   : 4;
	u64	d1   : 12;
	u64	b2   : 4;
	u64	d2   : 12;
} __packed;

static bool ex_handler_ua_mvcos(const struct exception_table_entry *ex,
				bool from, struct pt_regs *regs)
{
	unsigned long uaddr, remainder;
	struct insn_ssf *insn;

	/*
	 * If the faulting user space access crossed a page boundary retry by
	 * limiting the access to the first page (adjust length accordingly).
	 * Then the mvcos instruction will either complete with condition code
	 * zero, or generate another fault where the user space access did not
	 * cross a page boundary.
	 * If the faulting user space access did not cross a page boundary set
	 * length to zero and retry. In this case no user space access will
	 * happen, and the mvcos instruction will complete with condition code
	 * zero.
	 * In both cases the instruction will complete with condition code
	 * zero (copying finished), and the register which contains the
	 * length, indicates the number of bytes copied.
	 */
	regs->psw.addr = extable_fixup(ex);
	insn = (struct insn_ssf *)regs->psw.addr;
	if (from)
		uaddr = regs->gprs[insn->b2] + insn->d2;
	else
		uaddr = regs->gprs[insn->b1] + insn->d1;
	remainder = PAGE_SIZE - (uaddr & (PAGE_SIZE - 1));
	if (regs->gprs[insn->r3] <= remainder)
		remainder = 0;
	regs->gprs[insn->r3] = remainder;
	return true;
}

bool fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *ex;

	ex = s390_search_extables(instruction_pointer(regs));
	if (!ex)
		return false;
	switch (ex->type) {
	case EX_TYPE_FIXUP:
		return ex_handler_fixup(ex, regs);
	case EX_TYPE_BPF:
		return ex_handler_bpf(ex, regs);
	case EX_TYPE_UA_FAULT:
		return ex_handler_ua_fault(ex, regs);
	case EX_TYPE_UA_LOAD_REG:
		return ex_handler_ua_load_reg(ex, false, regs);
	case EX_TYPE_UA_LOAD_REGPAIR:
		return ex_handler_ua_load_reg(ex, true, regs);
	case EX_TYPE_ZEROPAD:
		return ex_handler_zeropad(ex, regs);
	case EX_TYPE_FPC:
		return ex_handler_fpc(ex, regs);
	case EX_TYPE_UA_MVCOS_TO:
		return ex_handler_ua_mvcos(ex, false, regs);
	case EX_TYPE_UA_MVCOS_FROM:
		return ex_handler_ua_mvcos(ex, true, regs);
	}
	panic("invalid exception table entry");
}
