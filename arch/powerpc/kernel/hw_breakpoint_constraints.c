// SPDX-License-Identifier: GPL-2.0+
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <asm/hw_breakpoint.h>
#include <asm/sstep.h>
#include <asm/cache.h>

static bool dar_in_user_range(unsigned long dar, struct arch_hw_breakpoint *info)
{
	return ((info->address <= dar) && (dar - info->address < info->len));
}

static bool ea_user_range_overlaps(unsigned long ea, int size,
				   struct arch_hw_breakpoint *info)
{
	return ((ea < info->address + info->len) &&
		(ea + size > info->address));
}

static bool dar_in_hw_range(unsigned long dar, struct arch_hw_breakpoint *info)
{
	unsigned long hw_start_addr, hw_end_addr;

	hw_start_addr = ALIGN_DOWN(info->address, HW_BREAKPOINT_SIZE);
	hw_end_addr = ALIGN(info->address + info->len, HW_BREAKPOINT_SIZE);

	return ((hw_start_addr <= dar) && (hw_end_addr > dar));
}

static bool ea_hw_range_overlaps(unsigned long ea, int size,
				 struct arch_hw_breakpoint *info)
{
	unsigned long hw_start_addr, hw_end_addr;
	unsigned long align_size = HW_BREAKPOINT_SIZE;

	/*
	 * On p10 predecessors, quadword is handle differently then
	 * other instructions.
	 */
	if (!cpu_has_feature(CPU_FTR_ARCH_31) && size == 16)
		align_size = HW_BREAKPOINT_SIZE_QUADWORD;

	hw_start_addr = ALIGN_DOWN(info->address, align_size);
	hw_end_addr = ALIGN(info->address + info->len, align_size);

	return ((ea < hw_end_addr) && (ea + size > hw_start_addr));
}

/*
 * If hw has multiple DAWR registers, we also need to check all
 * dawrx constraint bits to confirm this is _really_ a valid event.
 * If type is UNKNOWN, but privilege level matches, consider it as
 * a positive match.
 */
static bool check_dawrx_constraints(struct pt_regs *regs, int type,
				    struct arch_hw_breakpoint *info)
{
	if (OP_IS_LOAD(type) && !(info->type & HW_BRK_TYPE_READ))
		return false;

	/*
	 * The Cache Management instructions other than dcbz never
	 * cause a match. i.e. if type is CACHEOP, the instruction
	 * is dcbz, and dcbz is treated as Store.
	 */
	if ((OP_IS_STORE(type) || type == CACHEOP) && !(info->type & HW_BRK_TYPE_WRITE))
		return false;

	if (is_kernel_addr(regs->nip) && !(info->type & HW_BRK_TYPE_KERNEL))
		return false;

	if (user_mode(regs) && !(info->type & HW_BRK_TYPE_USER))
		return false;

	return true;
}

/*
 * Return true if the event is valid wrt dawr configuration,
 * including extraneous exception. Otherwise return false.
 */
bool wp_check_constraints(struct pt_regs *regs, struct ppc_inst instr,
			  unsigned long ea, int type, int size,
			  struct arch_hw_breakpoint *info)
{
	bool in_user_range = dar_in_user_range(regs->dar, info);
	bool dawrx_constraints;

	/*
	 * 8xx supports only one breakpoint and thus we can
	 * unconditionally return true.
	 */
	if (IS_ENABLED(CONFIG_PPC_8xx)) {
		if (!in_user_range)
			info->type |= HW_BRK_TYPE_EXTRANEOUS_IRQ;
		return true;
	}

	if (unlikely(ppc_inst_equal(instr, ppc_inst(0)))) {
		if (cpu_has_feature(CPU_FTR_ARCH_31) &&
		    !dar_in_hw_range(regs->dar, info))
			return false;

		return true;
	}

	dawrx_constraints = check_dawrx_constraints(regs, type, info);

	if (type == UNKNOWN) {
		if (cpu_has_feature(CPU_FTR_ARCH_31) &&
		    !dar_in_hw_range(regs->dar, info))
			return false;

		return dawrx_constraints;
	}

	if (ea_user_range_overlaps(ea, size, info))
		return dawrx_constraints;

	if (ea_hw_range_overlaps(ea, size, info)) {
		if (dawrx_constraints) {
			info->type |= HW_BRK_TYPE_EXTRANEOUS_IRQ;
			return true;
		}
	}
	return false;
}

void wp_get_instr_detail(struct pt_regs *regs, struct ppc_inst *instr,
			 int *type, int *size, unsigned long *ea)
{
	struct instruction_op op;

	if (__get_user_instr(*instr, (void __user *)regs->nip))
		return;

	analyse_instr(&op, regs, *instr);
	*type = GETTYPE(op.type);
	*ea = op.ea;

	if (!(regs->msr & MSR_64BIT))
		*ea &= 0xffffffffUL;


	*size = GETSIZE(op.type);
	if (*type == CACHEOP) {
		*size = l1_dcache_bytes();
		*ea &= ~(*size - 1);
	} else if (*type == LOAD_VMX || *type == STORE_VMX) {
		*ea &= ~(*size - 1);
	}
}
