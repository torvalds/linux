// SPDX-License-Identifier: GPL-2.0

#include <linux/bitfield.h>
#include <linux/extable.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/panic.h>
#include <asm/asm-extable.h>
#include <asm/extable.h>

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

static bool ex_handler_ua_store(const struct exception_table_entry *ex, struct pt_regs *regs)
{
	unsigned int reg_err = FIELD_GET(EX_DATA_REG_ERR, ex->data);

	regs->gprs[reg_err] = -EFAULT;
	regs->psw.addr = extable_fixup(ex);
	return true;
}

static bool ex_handler_ua_load_mem(const struct exception_table_entry *ex, struct pt_regs *regs)
{
	unsigned int reg_addr = FIELD_GET(EX_DATA_REG_ADDR, ex->data);
	unsigned int reg_err = FIELD_GET(EX_DATA_REG_ERR, ex->data);
	size_t len = FIELD_GET(EX_DATA_LEN, ex->data);

	regs->gprs[reg_err] = -EFAULT;
	memset((void *)regs->gprs[reg_addr], 0, len);
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
	case EX_TYPE_UA_STORE:
		return ex_handler_ua_store(ex, regs);
	case EX_TYPE_UA_LOAD_MEM:
		return ex_handler_ua_load_mem(ex, regs);
	case EX_TYPE_UA_LOAD_REG:
		return ex_handler_ua_load_reg(ex, false, regs);
	case EX_TYPE_UA_LOAD_REGPAIR:
		return ex_handler_ua_load_reg(ex, true, regs);
	case EX_TYPE_ZEROPAD:
		return ex_handler_zeropad(ex, regs);
	}
	panic("invalid exception table entry");
}
