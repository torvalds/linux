// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright 2008 Michael Ellerman, IBM Corporation.
 */

#include <linux/kprobes.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/cpuhotplug.h>
#include <linux/uaccess.h>

#include <asm/tlbflush.h>
#include <asm/page.h>
#include <asm/code-patching.h>
#include <asm/inst.h>

static int __patch_instruction(u32 *exec_addr, ppc_inst_t instr, u32 *patch_addr)
{
	if (!ppc_inst_prefixed(instr)) {
		u32 val = ppc_inst_val(instr);

		__put_kernel_nofault(patch_addr, &val, u32, failed);
	} else {
		u64 val = ppc_inst_as_ulong(instr);

		__put_kernel_nofault(patch_addr, &val, u64, failed);
	}

	asm ("dcbst 0, %0; sync; icbi 0,%1; sync; isync" :: "r" (patch_addr),
							    "r" (exec_addr));

	return 0;

failed:
	return -EFAULT;
}

int raw_patch_instruction(u32 *addr, ppc_inst_t instr)
{
	return __patch_instruction(addr, instr, addr);
}

#ifdef CONFIG_STRICT_KERNEL_RWX
static DEFINE_PER_CPU(struct vm_struct *, text_poke_area);

static int text_area_cpu_up(unsigned int cpu)
{
	struct vm_struct *area;

	area = get_vm_area(PAGE_SIZE, VM_ALLOC);
	if (!area) {
		WARN_ONCE(1, "Failed to create text area for cpu %d\n",
			cpu);
		return -1;
	}
	this_cpu_write(text_poke_area, area);

	return 0;
}

static int text_area_cpu_down(unsigned int cpu)
{
	free_vm_area(this_cpu_read(text_poke_area));
	return 0;
}

/*
 * Although BUG_ON() is rude, in this case it should only happen if ENOMEM, and
 * we judge it as being preferable to a kernel that will crash later when
 * someone tries to use patch_instruction().
 */
void __init poking_init(void)
{
	BUG_ON(!cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
		"powerpc/text_poke:online", text_area_cpu_up,
		text_area_cpu_down));
}

/*
 * This can be called for kernel text or a module.
 */
static int map_patch_area(void *addr, unsigned long text_poke_addr)
{
	unsigned long pfn;

	if (is_vmalloc_or_module_addr(addr))
		pfn = vmalloc_to_pfn(addr);
	else
		pfn = __pa_symbol(addr) >> PAGE_SHIFT;

	return map_kernel_page(text_poke_addr, (pfn << PAGE_SHIFT), PAGE_KERNEL);
}

static void unmap_patch_area(unsigned long addr)
{
	pte_t *ptep;
	pmd_t *pmdp;
	pud_t *pudp;
	p4d_t *p4dp;
	pgd_t *pgdp;

	pgdp = pgd_offset_k(addr);
	if (WARN_ON(pgd_none(*pgdp)))
		return;

	p4dp = p4d_offset(pgdp, addr);
	if (WARN_ON(p4d_none(*p4dp)))
		return;

	pudp = pud_offset(p4dp, addr);
	if (WARN_ON(pud_none(*pudp)))
		return;

	pmdp = pmd_offset(pudp, addr);
	if (WARN_ON(pmd_none(*pmdp)))
		return;

	ptep = pte_offset_kernel(pmdp, addr);
	if (WARN_ON(pte_none(*ptep)))
		return;

	/*
	 * In hash, pte_clear flushes the tlb, in radix, we have to
	 */
	pte_clear(&init_mm, addr, ptep);
	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
}

static int __do_patch_instruction(u32 *addr, ppc_inst_t instr)
{
	int err;
	u32 *patch_addr;
	unsigned long text_poke_addr;

	text_poke_addr = (unsigned long)__this_cpu_read(text_poke_area)->addr;
	patch_addr = (u32 *)(text_poke_addr + offset_in_page(addr));

	err = map_patch_area(addr, text_poke_addr);
	if (err)
		return err;

	err = __patch_instruction(addr, instr, patch_addr);

	unmap_patch_area(text_poke_addr);

	return err;
}

static int do_patch_instruction(u32 *addr, ppc_inst_t instr)
{
	int err;
	unsigned long flags;

	/*
	 * During early early boot patch_instruction is called
	 * when text_poke_area is not ready, but we still need
	 * to allow patching. We just do the plain old patching
	 */
	if (!this_cpu_read(text_poke_area))
		return raw_patch_instruction(addr, instr);

	local_irq_save(flags);
	err = __do_patch_instruction(addr, instr);
	local_irq_restore(flags);

	return err;
}
#else /* !CONFIG_STRICT_KERNEL_RWX */

static int do_patch_instruction(u32 *addr, ppc_inst_t instr)
{
	return raw_patch_instruction(addr, instr);
}

#endif /* CONFIG_STRICT_KERNEL_RWX */

int patch_instruction(u32 *addr, ppc_inst_t instr)
{
	/* Make sure we aren't patching a freed init section */
	if (system_state >= SYSTEM_FREEING_INITMEM && init_section_contains(addr, 4))
		return 0;

	return do_patch_instruction(addr, instr);
}
NOKPROBE_SYMBOL(patch_instruction);

int patch_branch(u32 *addr, unsigned long target, int flags)
{
	ppc_inst_t instr;

	if (create_branch(&instr, addr, target, flags))
		return -ERANGE;

	return patch_instruction(addr, instr);
}

bool is_offset_in_branch_range(long offset)
{
	/*
	 * Powerpc branch instruction is :
	 *
	 *  0         6                 30   31
	 *  +---------+----------------+---+---+
	 *  | opcode  |     LI         |AA |LK |
	 *  +---------+----------------+---+---+
	 *  Where AA = 0 and LK = 0
	 *
	 * LI is a signed 24 bits integer. The real branch offset is computed
	 * by: imm32 = SignExtend(LI:'0b00', 32);
	 *
	 * So the maximum forward branch should be:
	 *   (0x007fffff << 2) = 0x01fffffc =  0x1fffffc
	 * The maximum backward branch should be:
	 *   (0xff800000 << 2) = 0xfe000000 = -0x2000000
	 */
	return (offset >= -0x2000000 && offset <= 0x1fffffc && !(offset & 0x3));
}

bool is_offset_in_cond_branch_range(long offset)
{
	return offset >= -0x8000 && offset <= 0x7fff && !(offset & 0x3);
}

/*
 * Helper to check if a given instruction is a conditional branch
 * Derived from the conditional checks in analyse_instr()
 */
bool is_conditional_branch(ppc_inst_t instr)
{
	unsigned int opcode = ppc_inst_primary_opcode(instr);

	if (opcode == 16)       /* bc, bca, bcl, bcla */
		return true;
	if (opcode == 19) {
		switch ((ppc_inst_val(instr) >> 1) & 0x3ff) {
		case 16:        /* bclr, bclrl */
		case 528:       /* bcctr, bcctrl */
		case 560:       /* bctar, bctarl */
			return true;
		}
	}
	return false;
}
NOKPROBE_SYMBOL(is_conditional_branch);

int create_branch(ppc_inst_t *instr, const u32 *addr,
		  unsigned long target, int flags)
{
	long offset;

	*instr = ppc_inst(0);
	offset = target;
	if (! (flags & BRANCH_ABSOLUTE))
		offset = offset - (unsigned long)addr;

	/* Check we can represent the target in the instruction format */
	if (!is_offset_in_branch_range(offset))
		return 1;

	/* Mask out the flags and target, so they don't step on each other. */
	*instr = ppc_inst(0x48000000 | (flags & 0x3) | (offset & 0x03FFFFFC));

	return 0;
}

int create_cond_branch(ppc_inst_t *instr, const u32 *addr,
		       unsigned long target, int flags)
{
	long offset;

	offset = target;
	if (! (flags & BRANCH_ABSOLUTE))
		offset = offset - (unsigned long)addr;

	/* Check we can represent the target in the instruction format */
	if (!is_offset_in_cond_branch_range(offset))
		return 1;

	/* Mask out the flags and target, so they don't step on each other. */
	*instr = ppc_inst(0x40000000 | (flags & 0x3FF0003) | (offset & 0xFFFC));

	return 0;
}

int instr_is_relative_branch(ppc_inst_t instr)
{
	if (ppc_inst_val(instr) & BRANCH_ABSOLUTE)
		return 0;

	return instr_is_branch_iform(instr) || instr_is_branch_bform(instr);
}

int instr_is_relative_link_branch(ppc_inst_t instr)
{
	return instr_is_relative_branch(instr) && (ppc_inst_val(instr) & BRANCH_SET_LINK);
}

static unsigned long branch_iform_target(const u32 *instr)
{
	signed long imm;

	imm = ppc_inst_val(ppc_inst_read(instr)) & 0x3FFFFFC;

	/* If the top bit of the immediate value is set this is negative */
	if (imm & 0x2000000)
		imm -= 0x4000000;

	if ((ppc_inst_val(ppc_inst_read(instr)) & BRANCH_ABSOLUTE) == 0)
		imm += (unsigned long)instr;

	return (unsigned long)imm;
}

static unsigned long branch_bform_target(const u32 *instr)
{
	signed long imm;

	imm = ppc_inst_val(ppc_inst_read(instr)) & 0xFFFC;

	/* If the top bit of the immediate value is set this is negative */
	if (imm & 0x8000)
		imm -= 0x10000;

	if ((ppc_inst_val(ppc_inst_read(instr)) & BRANCH_ABSOLUTE) == 0)
		imm += (unsigned long)instr;

	return (unsigned long)imm;
}

unsigned long branch_target(const u32 *instr)
{
	if (instr_is_branch_iform(ppc_inst_read(instr)))
		return branch_iform_target(instr);
	else if (instr_is_branch_bform(ppc_inst_read(instr)))
		return branch_bform_target(instr);

	return 0;
}

int translate_branch(ppc_inst_t *instr, const u32 *dest, const u32 *src)
{
	unsigned long target;
	target = branch_target(src);

	if (instr_is_branch_iform(ppc_inst_read(src)))
		return create_branch(instr, dest, target,
				     ppc_inst_val(ppc_inst_read(src)));
	else if (instr_is_branch_bform(ppc_inst_read(src)))
		return create_cond_branch(instr, dest, target,
					  ppc_inst_val(ppc_inst_read(src)));

	return 1;
}
