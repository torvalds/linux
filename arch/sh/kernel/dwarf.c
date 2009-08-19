/*
 * Copyright (C) 2009 Matt Fleming <matt@console-pimps.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This is an implementation of a DWARF unwinder. Its main purpose is
 * for generating stacktrace information. Based on the DWARF 3
 * specification from http://www.dwarfstd.org.
 *
 * TODO:
 *	- DWARF64 doesn't work.
 */

/* #define DEBUG */
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <asm/dwarf.h>
#include <asm/unwinder.h>
#include <asm/sections.h>
#include <asm/unaligned.h>
#include <asm/dwarf.h>
#include <asm/stacktrace.h>

static LIST_HEAD(dwarf_cie_list);
DEFINE_SPINLOCK(dwarf_cie_lock);

static LIST_HEAD(dwarf_fde_list);
DEFINE_SPINLOCK(dwarf_fde_lock);

static struct dwarf_cie *cached_cie;

/*
 * Figure out whether we need to allocate some dwarf registers. If dwarf
 * registers have already been allocated then we may need to realloc
 * them. "reg" is a register number that we need to be able to access
 * after this call.
 *
 * Register numbers start at zero, therefore we need to allocate space
 * for "reg" + 1 registers.
 */
static void dwarf_frame_alloc_regs(struct dwarf_frame *frame,
				   unsigned int reg)
{
	struct dwarf_reg *regs;
	unsigned int num_regs = reg + 1;
	size_t new_size;
	size_t old_size;

	new_size = num_regs * sizeof(*regs);
	old_size = frame->num_regs * sizeof(*regs);

	/* Fast path: don't allocate any regs if we've already got enough. */
	if (frame->num_regs >= num_regs)
		return;

	regs = kzalloc(new_size, GFP_ATOMIC);
	if (!regs) {
		printk(KERN_WARNING "Unable to allocate DWARF registers\n");
		/*
		 * Let's just bomb hard here, we have no way to
		 * gracefully recover.
		 */
		BUG();
	}

	if (frame->regs) {
		memcpy(regs, frame->regs, old_size);
		kfree(frame->regs);
	}

	frame->regs = regs;
	frame->num_regs = num_regs;
}

/**
 *	dwarf_read_addr - read dwarf data
 *	@src: source address of data
 *	@dst: destination address to store the data to
 *
 *	Read 'n' bytes from @src, where 'n' is the size of an address on
 *	the native machine. We return the number of bytes read, which
 *	should always be 'n'. We also have to be careful when reading
 *	from @src and writing to @dst, because they can be arbitrarily
 *	aligned. Return 'n' - the number of bytes read.
 */
static inline int dwarf_read_addr(unsigned long *src, unsigned long *dst)
{
	u32 val = get_unaligned(src);
	put_unaligned(val, dst);
	return sizeof(unsigned long *);
}

/**
 *	dwarf_read_uleb128 - read unsigned LEB128 data
 *	@addr: the address where the ULEB128 data is stored
 *	@ret: address to store the result
 *
 *	Decode an unsigned LEB128 encoded datum. The algorithm is taken
 *	from Appendix C of the DWARF 3 spec. For information on the
 *	encodings refer to section "7.6 - Variable Length Data". Return
 *	the number of bytes read.
 */
static inline unsigned long dwarf_read_uleb128(char *addr, unsigned int *ret)
{
	unsigned int result;
	unsigned char byte;
	int shift, count;

	result = 0;
	shift = 0;
	count = 0;

	while (1) {
		byte = __raw_readb(addr);
		addr++;
		count++;

		result |= (byte & 0x7f) << shift;
		shift += 7;

		if (!(byte & 0x80))
			break;
	}

	*ret = result;

	return count;
}

/**
 *	dwarf_read_leb128 - read signed LEB128 data
 *	@addr: the address of the LEB128 encoded data
 *	@ret: address to store the result
 *
 *	Decode signed LEB128 data. The algorithm is taken from Appendix
 *	C of the DWARF 3 spec. Return the number of bytes read.
 */
static inline unsigned long dwarf_read_leb128(char *addr, int *ret)
{
	unsigned char byte;
	int result, shift;
	int num_bits;
	int count;

	result = 0;
	shift = 0;
	count = 0;

	while (1) {
		byte = __raw_readb(addr);
		addr++;
		result |= (byte & 0x7f) << shift;
		shift += 7;
		count++;

		if (!(byte & 0x80))
			break;
	}

	/* The number of bits in a signed integer. */
	num_bits = 8 * sizeof(result);

	if ((shift < num_bits) && (byte & 0x40))
		result |= (-1 << shift);

	*ret = result;

	return count;
}

/**
 *	dwarf_read_encoded_value - return the decoded value at @addr
 *	@addr: the address of the encoded value
 *	@val: where to write the decoded value
 *	@encoding: the encoding with which we can decode @addr
 *
 *	GCC emits encoded address in the .eh_frame FDE entries. Decode
 *	the value at @addr using @encoding. The decoded value is written
 *	to @val and the number of bytes read is returned.
 */
static int dwarf_read_encoded_value(char *addr, unsigned long *val,
				    char encoding)
{
	unsigned long decoded_addr = 0;
	int count = 0;

	switch (encoding & 0x70) {
	case DW_EH_PE_absptr:
		break;
	case DW_EH_PE_pcrel:
		decoded_addr = (unsigned long)addr;
		break;
	default:
		pr_debug("encoding=0x%x\n", (encoding & 0x70));
		BUG();
	}

	if ((encoding & 0x07) == 0x00)
		encoding |= DW_EH_PE_udata4;

	switch (encoding & 0x0f) {
	case DW_EH_PE_sdata4:
	case DW_EH_PE_udata4:
		count += 4;
		decoded_addr += get_unaligned((u32 *)addr);
		__raw_writel(decoded_addr, val);
		break;
	default:
		pr_debug("encoding=0x%x\n", encoding);
		BUG();
	}

	return count;
}

/**
 *	dwarf_entry_len - return the length of an FDE or CIE
 *	@addr: the address of the entry
 *	@len: the length of the entry
 *
 *	Read the initial_length field of the entry and store the size of
 *	the entry in @len. We return the number of bytes read. Return a
 *	count of 0 on error.
 */
static inline int dwarf_entry_len(char *addr, unsigned long *len)
{
	u32 initial_len;
	int count;

	initial_len = get_unaligned((u32 *)addr);
	count = 4;

	/*
	 * An initial length field value in the range DW_LEN_EXT_LO -
	 * DW_LEN_EXT_HI indicates an extension, and should not be
	 * interpreted as a length. The only extension that we currently
	 * understand is the use of DWARF64 addresses.
	 */
	if (initial_len >= DW_EXT_LO && initial_len <= DW_EXT_HI) {
		/*
		 * The 64-bit length field immediately follows the
		 * compulsory 32-bit length field.
		 */
		if (initial_len == DW_EXT_DWARF64) {
			*len = get_unaligned((u64 *)addr + 4);
			count = 12;
		} else {
			printk(KERN_WARNING "Unknown DWARF extension\n");
			count = 0;
		}
	} else
		*len = initial_len;

	return count;
}

/**
 *	dwarf_lookup_cie - locate the cie
 *	@cie_ptr: pointer to help with lookup
 */
static struct dwarf_cie *dwarf_lookup_cie(unsigned long cie_ptr)
{
	struct dwarf_cie *cie, *n;
	unsigned long flags;

	spin_lock_irqsave(&dwarf_cie_lock, flags);

	/*
	 * We've cached the last CIE we looked up because chances are
	 * that the FDE wants this CIE.
	 */
	if (cached_cie && cached_cie->cie_pointer == cie_ptr) {
		cie = cached_cie;
		goto out;
	}

	list_for_each_entry_safe(cie, n, &dwarf_cie_list, link) {
		if (cie->cie_pointer == cie_ptr) {
			cached_cie = cie;
			break;
		}
	}

	/* Couldn't find the entry in the list. */
	if (&cie->link == &dwarf_cie_list)
		cie = NULL;
out:
	spin_unlock_irqrestore(&dwarf_cie_lock, flags);
	return cie;
}

/**
 *	dwarf_lookup_fde - locate the FDE that covers pc
 *	@pc: the program counter
 */
struct dwarf_fde *dwarf_lookup_fde(unsigned long pc)
{
	unsigned long flags;
	struct dwarf_fde *fde, *n;

	spin_lock_irqsave(&dwarf_fde_lock, flags);
	list_for_each_entry_safe(fde, n, &dwarf_fde_list, link) {
		unsigned long start, end;

		start = fde->initial_location;
		end = fde->initial_location + fde->address_range;

		if (pc >= start && pc < end)
			break;
	}

	/* Couldn't find the entry in the list. */
	if (&fde->link == &dwarf_fde_list)
		fde = NULL;

	spin_unlock_irqrestore(&dwarf_fde_lock, flags);

	return fde;
}

/**
 *	dwarf_cfa_execute_insns - execute instructions to calculate a CFA
 *	@insn_start: address of the first instruction
 *	@insn_end: address of the last instruction
 *	@cie: the CIE for this function
 *	@fde: the FDE for this function
 *	@frame: the instructions calculate the CFA for this frame
 *	@pc: the program counter of the address we're interested in
 *
 *	Execute the Call Frame instruction sequence starting at
 *	@insn_start and ending at @insn_end. The instructions describe
 *	how to calculate the Canonical Frame Address of a stackframe.
 *	Store the results in @frame.
 */
static int dwarf_cfa_execute_insns(unsigned char *insn_start,
				   unsigned char *insn_end,
				   struct dwarf_cie *cie,
				   struct dwarf_fde *fde,
				   struct dwarf_frame *frame,
				   unsigned long pc)
{
	unsigned char insn;
	unsigned char *current_insn;
	unsigned int count, delta, reg, expr_len, offset;

	current_insn = insn_start;

	while (current_insn < insn_end && frame->pc <= pc) {
		insn = __raw_readb(current_insn++);

		/*
		 * Firstly, handle the opcodes that embed their operands
		 * in the instructions.
		 */
		switch (DW_CFA_opcode(insn)) {
		case DW_CFA_advance_loc:
			delta = DW_CFA_operand(insn);
			delta *= cie->code_alignment_factor;
			frame->pc += delta;
			continue;
			/* NOTREACHED */
		case DW_CFA_offset:
			reg = DW_CFA_operand(insn);
			count = dwarf_read_uleb128(current_insn, &offset);
			current_insn += count;
			offset *= cie->data_alignment_factor;
			dwarf_frame_alloc_regs(frame, reg);
			frame->regs[reg].addr = offset;
			frame->regs[reg].flags |= DWARF_REG_OFFSET;
			continue;
			/* NOTREACHED */
		case DW_CFA_restore:
			reg = DW_CFA_operand(insn);
			continue;
			/* NOTREACHED */
		}

		/*
		 * Secondly, handle the opcodes that don't embed their
		 * operands in the instruction.
		 */
		switch (insn) {
		case DW_CFA_nop:
			continue;
		case DW_CFA_advance_loc1:
			delta = *current_insn++;
			frame->pc += delta * cie->code_alignment_factor;
			break;
		case DW_CFA_advance_loc2:
			delta = get_unaligned((u16 *)current_insn);
			current_insn += 2;
			frame->pc += delta * cie->code_alignment_factor;
			break;
		case DW_CFA_advance_loc4:
			delta = get_unaligned((u32 *)current_insn);
			current_insn += 4;
			frame->pc += delta * cie->code_alignment_factor;
			break;
		case DW_CFA_offset_extended:
			count = dwarf_read_uleb128(current_insn, &reg);
			current_insn += count;
			count = dwarf_read_uleb128(current_insn, &offset);
			current_insn += count;
			offset *= cie->data_alignment_factor;
			break;
		case DW_CFA_restore_extended:
			count = dwarf_read_uleb128(current_insn, &reg);
			current_insn += count;
			break;
		case DW_CFA_undefined:
			count = dwarf_read_uleb128(current_insn, &reg);
			current_insn += count;
			break;
		case DW_CFA_def_cfa:
			count = dwarf_read_uleb128(current_insn,
						   &frame->cfa_register);
			current_insn += count;
			count = dwarf_read_uleb128(current_insn,
						   &frame->cfa_offset);
			current_insn += count;

			frame->flags |= DWARF_FRAME_CFA_REG_OFFSET;
			break;
		case DW_CFA_def_cfa_register:
			count = dwarf_read_uleb128(current_insn,
						   &frame->cfa_register);
			current_insn += count;
			frame->flags |= DWARF_FRAME_CFA_REG_OFFSET;
			break;
		case DW_CFA_def_cfa_offset:
			count = dwarf_read_uleb128(current_insn, &offset);
			current_insn += count;
			frame->cfa_offset = offset;
			break;
		case DW_CFA_def_cfa_expression:
			count = dwarf_read_uleb128(current_insn, &expr_len);
			current_insn += count;

			frame->cfa_expr = current_insn;
			frame->cfa_expr_len = expr_len;
			current_insn += expr_len;

			frame->flags |= DWARF_FRAME_CFA_REG_EXP;
			break;
		case DW_CFA_offset_extended_sf:
			count = dwarf_read_uleb128(current_insn, &reg);
			current_insn += count;
			count = dwarf_read_leb128(current_insn, &offset);
			current_insn += count;
			offset *= cie->data_alignment_factor;
			dwarf_frame_alloc_regs(frame, reg);
			frame->regs[reg].flags |= DWARF_REG_OFFSET;
			frame->regs[reg].addr = offset;
			break;
		case DW_CFA_val_offset:
			count = dwarf_read_uleb128(current_insn, &reg);
			current_insn += count;
			count = dwarf_read_leb128(current_insn, &offset);
			offset *= cie->data_alignment_factor;
			frame->regs[reg].flags |= DWARF_REG_OFFSET;
			frame->regs[reg].addr = offset;
			break;
		case DW_CFA_GNU_args_size:
			count = dwarf_read_uleb128(current_insn, &offset);
			current_insn += count;
			break;
		case DW_CFA_GNU_negative_offset_extended:
			count = dwarf_read_uleb128(current_insn, &reg);
			current_insn += count;
			count = dwarf_read_uleb128(current_insn, &offset);
			offset *= cie->data_alignment_factor;
			dwarf_frame_alloc_regs(frame, reg);
			frame->regs[reg].flags |= DWARF_REG_OFFSET;
			frame->regs[reg].addr = -offset;
			break;
		default:
			pr_debug("unhandled DWARF instruction 0x%x\n", insn);
			break;
		}
	}

	return 0;
}

/**
 *	dwarf_unwind_stack - recursively unwind the stack
 *	@pc: address of the function to unwind
 *	@prev: struct dwarf_frame of the previous stackframe on the callstack
 *
 *	Return a struct dwarf_frame representing the most recent frame
 *	on the callstack. Each of the lower (older) stack frames are
 *	linked via the "prev" member.
 */
struct dwarf_frame *dwarf_unwind_stack(unsigned long pc,
				       struct dwarf_frame *prev)
{
	struct dwarf_frame *frame;
	struct dwarf_cie *cie;
	struct dwarf_fde *fde;
	unsigned long addr;
	int i, offset;

	/*
	 * If this is the first invocation of this recursive function we
	 * need get the contents of a physical register to get the CFA
	 * in order to begin the virtual unwinding of the stack.
	 *
	 * NOTE: the return address is guaranteed to be setup by the
	 * time this function makes its first function call.
	 */
	if (!pc && !prev)
		pc = (unsigned long)current_text_addr();

	frame = kzalloc(sizeof(*frame), GFP_ATOMIC);
	if (!frame)
		return NULL;

	frame->prev = prev;

	fde = dwarf_lookup_fde(pc);
	if (!fde) {
		/*
		 * This is our normal exit path - the one that stops the
		 * recursion. There's two reasons why we might exit
		 * here,
		 *
		 *	a) pc has no asscociated DWARF frame info and so
		 *	we don't know how to unwind this frame. This is
		 *	usually the case when we're trying to unwind a
		 *	frame that was called from some assembly code
		 *	that has no DWARF info, e.g. syscalls.
		 *
		 *	b) the DEBUG info for pc is bogus. There's
		 *	really no way to distinguish this case from the
		 *	case above, which sucks because we could print a
		 *	warning here.
		 */
		return NULL;
	}

	cie = dwarf_lookup_cie(fde->cie_pointer);

	frame->pc = fde->initial_location;

	/* CIE initial instructions */
	dwarf_cfa_execute_insns(cie->initial_instructions,
				cie->instructions_end, cie, fde,
				frame, pc);

	/* FDE instructions */
	dwarf_cfa_execute_insns(fde->instructions, fde->end, cie,
				fde, frame, pc);

	/* Calculate the CFA */
	switch (frame->flags) {
	case DWARF_FRAME_CFA_REG_OFFSET:
		if (prev) {
			BUG_ON(!prev->regs[frame->cfa_register].flags);

			addr = prev->cfa;
			addr += prev->regs[frame->cfa_register].addr;
			frame->cfa = __raw_readl(addr);

		} else {
			/*
			 * Again, this is the first invocation of this
			 * recurisve function. We need to physically
			 * read the contents of a register in order to
			 * get the Canonical Frame Address for this
			 * function.
			 */
			frame->cfa = dwarf_read_arch_reg(frame->cfa_register);
		}

		frame->cfa += frame->cfa_offset;
		break;
	default:
		BUG();
	}

	/* If we haven't seen the return address reg, we're screwed. */
	BUG_ON(!frame->regs[DWARF_ARCH_RA_REG].flags);

	for (i = 0; i <= frame->num_regs; i++) {
		struct dwarf_reg *reg = &frame->regs[i];

		if (!reg->flags)
			continue;

		offset = reg->addr;
		offset += frame->cfa;
	}

	addr = frame->cfa + frame->regs[DWARF_ARCH_RA_REG].addr;
	frame->return_addr = __raw_readl(addr);

	frame->next = dwarf_unwind_stack(frame->return_addr, frame);
	return frame;
}

static int dwarf_parse_cie(void *entry, void *p, unsigned long len,
			   unsigned char *end)
{
	struct dwarf_cie *cie;
	unsigned long flags;
	int count;

	cie = kzalloc(sizeof(*cie), GFP_KERNEL);
	if (!cie)
		return -ENOMEM;

	cie->length = len;

	/*
	 * Record the offset into the .eh_frame section
	 * for this CIE. It allows this CIE to be
	 * quickly and easily looked up from the
	 * corresponding FDE.
	 */
	cie->cie_pointer = (unsigned long)entry;

	cie->version = *(char *)p++;
	BUG_ON(cie->version != 1);

	cie->augmentation = p;
	p += strlen(cie->augmentation) + 1;

	count = dwarf_read_uleb128(p, &cie->code_alignment_factor);
	p += count;

	count = dwarf_read_leb128(p, &cie->data_alignment_factor);
	p += count;

	/*
	 * Which column in the rule table contains the
	 * return address?
	 */
	if (cie->version == 1) {
		cie->return_address_reg = __raw_readb(p);
		p++;
	} else {
		count = dwarf_read_uleb128(p, &cie->return_address_reg);
		p += count;
	}

	if (cie->augmentation[0] == 'z') {
		unsigned int length, count;
		cie->flags |= DWARF_CIE_Z_AUGMENTATION;

		count = dwarf_read_uleb128(p, &length);
		p += count;

		BUG_ON((unsigned char *)p > end);

		cie->initial_instructions = p + length;
		cie->augmentation++;
	}

	while (*cie->augmentation) {
		/*
		 * "L" indicates a byte showing how the
		 * LSDA pointer is encoded. Skip it.
		 */
		if (*cie->augmentation == 'L') {
			p++;
			cie->augmentation++;
		} else if (*cie->augmentation == 'R') {
			/*
			 * "R" indicates a byte showing
			 * how FDE addresses are
			 * encoded.
			 */
			cie->encoding = *(char *)p++;
			cie->augmentation++;
		} else if (*cie->augmentation == 'P') {
			/*
			 * "R" indicates a personality
			 * routine in the CIE
			 * augmentation.
			 */
			BUG();
		} else if (*cie->augmentation == 'S') {
			BUG();
		} else {
			/*
			 * Unknown augmentation. Assume
			 * 'z' augmentation.
			 */
			p = cie->initial_instructions;
			BUG_ON(!p);
			break;
		}
	}

	cie->initial_instructions = p;
	cie->instructions_end = end;

	/* Add to list */
	spin_lock_irqsave(&dwarf_cie_lock, flags);
	list_add_tail(&cie->link, &dwarf_cie_list);
	spin_unlock_irqrestore(&dwarf_cie_lock, flags);

	return 0;
}

static int dwarf_parse_fde(void *entry, u32 entry_type,
			   void *start, unsigned long len)
{
	struct dwarf_fde *fde;
	struct dwarf_cie *cie;
	unsigned long flags;
	int count;
	void *p = start;

	fde = kzalloc(sizeof(*fde), GFP_KERNEL);
	if (!fde)
		return -ENOMEM;

	fde->length = len;

	/*
	 * In a .eh_frame section the CIE pointer is the
	 * delta between the address within the FDE
	 */
	fde->cie_pointer = (unsigned long)(p - entry_type - 4);

	cie = dwarf_lookup_cie(fde->cie_pointer);
	fde->cie = cie;

	if (cie->encoding)
		count = dwarf_read_encoded_value(p, &fde->initial_location,
						 cie->encoding);
	else
		count = dwarf_read_addr(p, &fde->initial_location);

	p += count;

	if (cie->encoding)
		count = dwarf_read_encoded_value(p, &fde->address_range,
						 cie->encoding & 0x0f);
	else
		count = dwarf_read_addr(p, &fde->address_range);

	p += count;

	if (fde->cie->flags & DWARF_CIE_Z_AUGMENTATION) {
		unsigned int length;
		count = dwarf_read_uleb128(p, &length);
		p += count + length;
	}

	/* Call frame instructions. */
	fde->instructions = p;
	fde->end = start + len;

	/* Add to list. */
	spin_lock_irqsave(&dwarf_fde_lock, flags);
	list_add_tail(&fde->link, &dwarf_fde_list);
	spin_unlock_irqrestore(&dwarf_fde_lock, flags);

	return 0;
}

static void dwarf_unwinder_dump(struct task_struct *task, struct pt_regs *regs,
				unsigned long *sp,
				const struct stacktrace_ops *ops, void *data)
{
	struct dwarf_frame *frame;

	frame = dwarf_unwind_stack(0, NULL);

	while (frame && frame->return_addr) {
		ops->address(data, frame->return_addr, 1);
		frame = frame->next;
	}
}

static struct unwinder dwarf_unwinder = {
	.name = "dwarf-unwinder",
	.dump = dwarf_unwinder_dump,
	.rating = 150,
};

static void dwarf_unwinder_cleanup(void)
{
	struct dwarf_cie *cie, *m;
	struct dwarf_fde *fde, *n;
	unsigned long flags;

	/*
	 * Deallocate all the memory allocated for the DWARF unwinder.
	 * Traverse all the FDE/CIE lists and remove and free all the
	 * memory associated with those data structures.
	 */
	spin_lock_irqsave(&dwarf_cie_lock, flags);
	list_for_each_entry_safe(cie, m, &dwarf_cie_list, link)
		kfree(cie);
	spin_unlock_irqrestore(&dwarf_cie_lock, flags);

	spin_lock_irqsave(&dwarf_fde_lock, flags);
	list_for_each_entry_safe(fde, n, &dwarf_fde_list, link)
		kfree(fde);
	spin_unlock_irqrestore(&dwarf_fde_lock, flags);
}

/**
 *	dwarf_unwinder_init - initialise the dwarf unwinder
 *
 *	Build the data structures describing the .dwarf_frame section to
 *	make it easier to lookup CIE and FDE entries. Because the
 *	.eh_frame section is packed as tightly as possible it is not
 *	easy to lookup the FDE for a given PC, so we build a list of FDE
 *	and CIE entries that make it easier.
 */
void dwarf_unwinder_init(void)
{
	u32 entry_type;
	void *p, *entry;
	int count, err;
	unsigned long len;
	unsigned int c_entries, f_entries;
	unsigned char *end;
	INIT_LIST_HEAD(&dwarf_cie_list);
	INIT_LIST_HEAD(&dwarf_fde_list);

	c_entries = 0;
	f_entries = 0;
	entry = &__start_eh_frame;

	while ((char *)entry < __stop_eh_frame) {
		p = entry;

		count = dwarf_entry_len(p, &len);
		if (count == 0) {
			/*
			 * We read a bogus length field value. There is
			 * nothing we can do here apart from disabling
			 * the DWARF unwinder. We can't even skip this
			 * entry and move to the next one because 'len'
			 * tells us where our next entry is.
			 */
			goto out;
		} else
			p += count;

		/* initial length does not include itself */
		end = p + len;

		entry_type = get_unaligned((u32 *)p);
		p += 4;

		if (entry_type == DW_EH_FRAME_CIE) {
			err = dwarf_parse_cie(entry, p, len, end);
			if (err < 0)
				goto out;
			else
				c_entries++;
		} else {
			err = dwarf_parse_fde(entry, entry_type, p, len);
			if (err < 0)
				goto out;
			else
				f_entries++;
		}

		entry = (char *)entry + len + 4;
	}

	printk(KERN_INFO "DWARF unwinder initialised: read %u CIEs, %u FDEs\n",
	       c_entries, f_entries);

	err = unwinder_register(&dwarf_unwinder);
	if (err)
		goto out;

	return;

out:
	printk(KERN_ERR "Failed to initialise DWARF unwinder: %d\n", err);
	dwarf_unwinder_cleanup();
}
