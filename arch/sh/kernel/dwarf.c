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
 *	- Registers with DWARF_VAL_OFFSET rules aren't handled properly.
 */

/* #define DEBUG */
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/mempool.h>
#include <linux/mm.h>
#include <linux/elf.h>
#include <linux/ftrace.h>
#include <asm/dwarf.h>
#include <asm/unwinder.h>
#include <asm/sections.h>
#include <asm/unaligned.h>
#include <asm/stacktrace.h>

/* Reserve enough memory for two stack frames */
#define DWARF_FRAME_MIN_REQ	2
/* ... with 4 registers per frame. */
#define DWARF_REG_MIN_REQ	(DWARF_FRAME_MIN_REQ * 4)

static struct kmem_cache *dwarf_frame_cachep;
static mempool_t *dwarf_frame_pool;

static struct kmem_cache *dwarf_reg_cachep;
static mempool_t *dwarf_reg_pool;

static struct rb_root cie_root;
static DEFINE_SPINLOCK(dwarf_cie_lock);

static struct rb_root fde_root;
static DEFINE_SPINLOCK(dwarf_fde_lock);

static struct dwarf_cie *cached_cie;

/**
 *	dwarf_frame_alloc_reg - allocate memory for a DWARF register
 *	@frame: the DWARF frame whose list of registers we insert on
 *	@reg_num: the register number
 *
 *	Allocate space for, and initialise, a dwarf reg from
 *	dwarf_reg_pool and insert it onto the (unsorted) linked-list of
 *	dwarf registers for @frame.
 *
 *	Return the initialised DWARF reg.
 */
static struct dwarf_reg *dwarf_frame_alloc_reg(struct dwarf_frame *frame,
					       unsigned int reg_num)
{
	struct dwarf_reg *reg;

	reg = mempool_alloc(dwarf_reg_pool, GFP_ATOMIC);
	if (!reg) {
		printk(KERN_WARNING "Unable to allocate a DWARF register\n");
		/*
		 * Let's just bomb hard here, we have no way to
		 * gracefully recover.
		 */
		UNWINDER_BUG();
	}

	reg->number = reg_num;
	reg->addr = 0;
	reg->flags = 0;

	list_add(&reg->link, &frame->reg_list);

	return reg;
}

static void dwarf_frame_free_regs(struct dwarf_frame *frame)
{
	struct dwarf_reg *reg, *n;

	list_for_each_entry_safe(reg, n, &frame->reg_list, link) {
		list_del(&reg->link);
		mempool_free(reg, dwarf_reg_pool);
	}
}

/**
 *	dwarf_frame_reg - return a DWARF register
 *	@frame: the DWARF frame to search in for @reg_num
 *	@reg_num: the register number to search for
 *
 *	Lookup and return the dwarf reg @reg_num for this frame. Return
 *	NULL if @reg_num is an register invalid number.
 */
static struct dwarf_reg *dwarf_frame_reg(struct dwarf_frame *frame,
					 unsigned int reg_num)
{
	struct dwarf_reg *reg;

	list_for_each_entry(reg, &frame->reg_list, link) {
		if (reg->number == reg_num)
			return reg;
	}

	return NULL;
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
		UNWINDER_BUG();
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
		UNWINDER_BUG();
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
	struct rb_node **rb_node = &cie_root.rb_node;
	struct dwarf_cie *cie = NULL;
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

	while (*rb_node) {
		struct dwarf_cie *cie_tmp;

		cie_tmp = rb_entry(*rb_node, struct dwarf_cie, node);
		BUG_ON(!cie_tmp);

		if (cie_ptr == cie_tmp->cie_pointer) {
			cie = cie_tmp;
			cached_cie = cie_tmp;
			goto out;
		} else {
			if (cie_ptr < cie_tmp->cie_pointer)
				rb_node = &(*rb_node)->rb_left;
			else
				rb_node = &(*rb_node)->rb_right;
		}
	}

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
	struct rb_node **rb_node = &fde_root.rb_node;
	struct dwarf_fde *fde = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dwarf_fde_lock, flags);

	while (*rb_node) {
		struct dwarf_fde *fde_tmp;
		unsigned long tmp_start, tmp_end;

		fde_tmp = rb_entry(*rb_node, struct dwarf_fde, node);
		BUG_ON(!fde_tmp);

		tmp_start = fde_tmp->initial_location;
		tmp_end = fde_tmp->initial_location + fde_tmp->address_range;

		if (pc < tmp_start) {
			rb_node = &(*rb_node)->rb_left;
		} else {
			if (pc < tmp_end) {
				fde = fde_tmp;
				goto out;
			} else
				rb_node = &(*rb_node)->rb_right;
		}
	}

out:
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
	struct dwarf_reg *regp;

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
			regp = dwarf_frame_alloc_reg(frame, reg);
			regp->addr = offset;
			regp->flags |= DWARF_REG_OFFSET;
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
			regp = dwarf_frame_alloc_reg(frame, reg);
			regp->flags |= DWARF_UNDEFINED;
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
			regp = dwarf_frame_alloc_reg(frame, reg);
			regp->flags |= DWARF_REG_OFFSET;
			regp->addr = offset;
			break;
		case DW_CFA_val_offset:
			count = dwarf_read_uleb128(current_insn, &reg);
			current_insn += count;
			count = dwarf_read_leb128(current_insn, &offset);
			offset *= cie->data_alignment_factor;
			regp = dwarf_frame_alloc_reg(frame, reg);
			regp->flags |= DWARF_VAL_OFFSET;
			regp->addr = offset;
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

			regp = dwarf_frame_alloc_reg(frame, reg);
			regp->flags |= DWARF_REG_OFFSET;
			regp->addr = -offset;
			break;
		default:
			pr_debug("unhandled DWARF instruction 0x%x\n", insn);
			UNWINDER_BUG();
			break;
		}
	}

	return 0;
}

/**
 *	dwarf_free_frame - free the memory allocated for @frame
 *	@frame: the frame to free
 */
void dwarf_free_frame(struct dwarf_frame *frame)
{
	dwarf_frame_free_regs(frame);
	mempool_free(frame, dwarf_frame_pool);
}

extern void ret_from_irq(void);

/**
 *	dwarf_unwind_stack - unwind the stack
 *
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
	struct dwarf_reg *reg;
	unsigned long addr;

	/*
	 * If we're starting at the top of the stack we need get the
	 * contents of a physical register to get the CFA in order to
	 * begin the virtual unwinding of the stack.
	 *
	 * NOTE: the return address is guaranteed to be setup by the
	 * time this function makes its first function call.
	 */
	if (!pc || !prev)
		pc = (unsigned long)current_text_addr();

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/*
	 * If our stack has been patched by the function graph tracer
	 * then we might see the address of return_to_handler() where we
	 * expected to find the real return address.
	 */
	if (pc == (unsigned long)&return_to_handler) {
		int index = current->curr_ret_stack;

		/*
		 * We currently have no way of tracking how many
		 * return_to_handler()'s we've seen. If there is more
		 * than one patched return address on our stack,
		 * complain loudly.
		 */
		WARN_ON(index > 0);

		pc = current->ret_stack[index].ret;
	}
#endif

	frame = mempool_alloc(dwarf_frame_pool, GFP_ATOMIC);
	if (!frame) {
		printk(KERN_ERR "Unable to allocate a dwarf frame\n");
		UNWINDER_BUG();
	}

	INIT_LIST_HEAD(&frame->reg_list);
	frame->flags = 0;
	frame->prev = prev;
	frame->return_addr = 0;

	fde = dwarf_lookup_fde(pc);
	if (!fde) {
		/*
		 * This is our normal exit path. There are two reasons
		 * why we might exit here,
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
		goto bail;
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
			reg = dwarf_frame_reg(prev, frame->cfa_register);
			UNWINDER_BUG_ON(!reg);
			UNWINDER_BUG_ON(reg->flags != DWARF_REG_OFFSET);

			addr = prev->cfa + reg->addr;
			frame->cfa = __raw_readl(addr);

		} else {
			/*
			 * Again, we're starting from the top of the
			 * stack. We need to physically read
			 * the contents of a register in order to get
			 * the Canonical Frame Address for this
			 * function.
			 */
			frame->cfa = dwarf_read_arch_reg(frame->cfa_register);
		}

		frame->cfa += frame->cfa_offset;
		break;
	default:
		UNWINDER_BUG();
	}

	reg = dwarf_frame_reg(frame, DWARF_ARCH_RA_REG);

	/*
	 * If we haven't seen the return address register or the return
	 * address column is undefined then we must assume that this is
	 * the end of the callstack.
	 */
	if (!reg || reg->flags == DWARF_UNDEFINED)
		goto bail;

	UNWINDER_BUG_ON(reg->flags != DWARF_REG_OFFSET);

	addr = frame->cfa + reg->addr;
	frame->return_addr = __raw_readl(addr);

	/*
	 * Ah, the joys of unwinding through interrupts.
	 *
	 * Interrupts are tricky - the DWARF info needs to be _really_
	 * accurate and unfortunately I'm seeing a lot of bogus DWARF
	 * info. For example, I've seen interrupts occur in epilogues
	 * just after the frame pointer (r14) had been restored. The
	 * problem was that the DWARF info claimed that the CFA could be
	 * reached by using the value of the frame pointer before it was
	 * restored.
	 *
	 * So until the compiler can be trusted to produce reliable
	 * DWARF info when it really matters, let's stop unwinding once
	 * we've calculated the function that was interrupted.
	 */
	if (prev && prev->pc == (unsigned long)ret_from_irq)
		frame->return_addr = 0;

	return frame;

bail:
	dwarf_free_frame(frame);
	return NULL;
}

static int dwarf_parse_cie(void *entry, void *p, unsigned long len,
			   unsigned char *end, struct module *mod)
{
	struct rb_node **rb_node = &cie_root.rb_node;
	struct rb_node *parent;
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
	UNWINDER_BUG_ON(cie->version != 1);

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

		UNWINDER_BUG_ON((unsigned char *)p > end);

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
			UNWINDER_BUG();
		} else if (*cie->augmentation == 'S') {
			UNWINDER_BUG();
		} else {
			/*
			 * Unknown augmentation. Assume
			 * 'z' augmentation.
			 */
			p = cie->initial_instructions;
			UNWINDER_BUG_ON(!p);
			break;
		}
	}

	cie->initial_instructions = p;
	cie->instructions_end = end;

	/* Add to list */
	spin_lock_irqsave(&dwarf_cie_lock, flags);

	while (*rb_node) {
		struct dwarf_cie *cie_tmp;

		cie_tmp = rb_entry(*rb_node, struct dwarf_cie, node);

		parent = *rb_node;

		if (cie->cie_pointer < cie_tmp->cie_pointer)
			rb_node = &parent->rb_left;
		else if (cie->cie_pointer >= cie_tmp->cie_pointer)
			rb_node = &parent->rb_right;
		else
			WARN_ON(1);
	}

	rb_link_node(&cie->node, parent, rb_node);
	rb_insert_color(&cie->node, &cie_root);

	if (mod != NULL)
		list_add_tail(&cie->link, &mod->arch.cie_list);

	spin_unlock_irqrestore(&dwarf_cie_lock, flags);

	return 0;
}

static int dwarf_parse_fde(void *entry, u32 entry_type,
			   void *start, unsigned long len,
			   unsigned char *end, struct module *mod)
{
	struct rb_node **rb_node = &fde_root.rb_node;
	struct rb_node *parent;
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
	fde->end = end;

	/* Add to list. */
	spin_lock_irqsave(&dwarf_fde_lock, flags);

	while (*rb_node) {
		struct dwarf_fde *fde_tmp;
		unsigned long tmp_start, tmp_end;
		unsigned long start, end;

		fde_tmp = rb_entry(*rb_node, struct dwarf_fde, node);

		start = fde->initial_location;
		end = fde->initial_location + fde->address_range;

		tmp_start = fde_tmp->initial_location;
		tmp_end = fde_tmp->initial_location + fde_tmp->address_range;

		parent = *rb_node;

		if (start < tmp_start)
			rb_node = &parent->rb_left;
		else if (start >= tmp_end)
			rb_node = &parent->rb_right;
		else
			WARN_ON(1);
	}

	rb_link_node(&fde->node, parent, rb_node);
	rb_insert_color(&fde->node, &fde_root);

	if (mod != NULL)
		list_add_tail(&fde->link, &mod->arch.fde_list);

	spin_unlock_irqrestore(&dwarf_fde_lock, flags);

	return 0;
}

static void dwarf_unwinder_dump(struct task_struct *task,
				struct pt_regs *regs,
				unsigned long *sp,
				const struct stacktrace_ops *ops,
				void *data)
{
	struct dwarf_frame *frame, *_frame;
	unsigned long return_addr;

	_frame = NULL;
	return_addr = 0;

	while (1) {
		frame = dwarf_unwind_stack(return_addr, _frame);

		if (_frame)
			dwarf_free_frame(_frame);

		_frame = frame;

		if (!frame || !frame->return_addr)
			break;

		return_addr = frame->return_addr;
		ops->address(data, return_addr, 1);
	}

	if (frame)
		dwarf_free_frame(frame);
}

static struct unwinder dwarf_unwinder = {
	.name = "dwarf-unwinder",
	.dump = dwarf_unwinder_dump,
	.rating = 150,
};

static void dwarf_unwinder_cleanup(void)
{
	struct rb_node **fde_rb_node = &fde_root.rb_node;
	struct rb_node **cie_rb_node = &cie_root.rb_node;

	/*
	 * Deallocate all the memory allocated for the DWARF unwinder.
	 * Traverse all the FDE/CIE lists and remove and free all the
	 * memory associated with those data structures.
	 */
	while (*fde_rb_node) {
		struct dwarf_fde *fde;

		fde = rb_entry(*fde_rb_node, struct dwarf_fde, node);
		rb_erase(*fde_rb_node, &fde_root);
		kfree(fde);
	}

	while (*cie_rb_node) {
		struct dwarf_cie *cie;

		cie = rb_entry(*cie_rb_node, struct dwarf_cie, node);
		rb_erase(*cie_rb_node, &cie_root);
		kfree(cie);
	}

	kmem_cache_destroy(dwarf_reg_cachep);
	kmem_cache_destroy(dwarf_frame_cachep);
}

/**
 *	dwarf_parse_section - parse DWARF section
 *	@eh_frame_start: start address of the .eh_frame section
 *	@eh_frame_end: end address of the .eh_frame section
 *	@mod: the kernel module containing the .eh_frame section
 *
 *	Parse the information in a .eh_frame section.
 */
static int dwarf_parse_section(char *eh_frame_start, char *eh_frame_end,
			       struct module *mod)
{
	u32 entry_type;
	void *p, *entry;
	int count, err = 0;
	unsigned long len = 0;
	unsigned int c_entries, f_entries;
	unsigned char *end;

	c_entries = 0;
	f_entries = 0;
	entry = eh_frame_start;

	while ((char *)entry < eh_frame_end) {
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
			err = -EINVAL;
			goto out;
		} else
			p += count;

		/* initial length does not include itself */
		end = p + len;

		entry_type = get_unaligned((u32 *)p);
		p += 4;

		if (entry_type == DW_EH_FRAME_CIE) {
			err = dwarf_parse_cie(entry, p, len, end, mod);
			if (err < 0)
				goto out;
			else
				c_entries++;
		} else {
			err = dwarf_parse_fde(entry, entry_type, p, len,
					      end, mod);
			if (err < 0)
				goto out;
			else
				f_entries++;
		}

		entry = (char *)entry + len + 4;
	}

	printk(KERN_INFO "DWARF unwinder initialised: read %u CIEs, %u FDEs\n",
	       c_entries, f_entries);

	return 0;

out:
	return err;
}

#ifdef CONFIG_MODULES
int module_dwarf_finalize(const Elf_Ehdr *hdr, const Elf_Shdr *sechdrs,
			  struct module *me)
{
	unsigned int i, err;
	unsigned long start, end;
	char *secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;

	start = end = 0;

	for (i = 1; i < hdr->e_shnum; i++) {
		/* Alloc bit cleared means "ignore it." */
		if ((sechdrs[i].sh_flags & SHF_ALLOC)
		    && !strcmp(secstrings+sechdrs[i].sh_name, ".eh_frame")) {
			start = sechdrs[i].sh_addr;
			end = start + sechdrs[i].sh_size;
			break;
		}
	}

	/* Did we find the .eh_frame section? */
	if (i != hdr->e_shnum) {
		INIT_LIST_HEAD(&me->arch.cie_list);
		INIT_LIST_HEAD(&me->arch.fde_list);
		err = dwarf_parse_section((char *)start, (char *)end, me);
		if (err) {
			printk(KERN_WARNING "%s: failed to parse DWARF info\n",
			       me->name);
			return err;
		}
	}

	return 0;
}

/**
 *	module_dwarf_cleanup - remove FDE/CIEs associated with @mod
 *	@mod: the module that is being unloaded
 *
 *	Remove any FDEs and CIEs from the global lists that came from
 *	@mod's .eh_frame section because @mod is being unloaded.
 */
void module_dwarf_cleanup(struct module *mod)
{
	struct dwarf_fde *fde, *ftmp;
	struct dwarf_cie *cie, *ctmp;
	unsigned long flags;

	spin_lock_irqsave(&dwarf_cie_lock, flags);

	list_for_each_entry_safe(cie, ctmp, &mod->arch.cie_list, link) {
		list_del(&cie->link);
		rb_erase(&cie->node, &cie_root);
		kfree(cie);
	}

	spin_unlock_irqrestore(&dwarf_cie_lock, flags);

	spin_lock_irqsave(&dwarf_fde_lock, flags);

	list_for_each_entry_safe(fde, ftmp, &mod->arch.fde_list, link) {
		list_del(&fde->link);
		rb_erase(&fde->node, &fde_root);
		kfree(fde);
	}

	spin_unlock_irqrestore(&dwarf_fde_lock, flags);
}
#endif /* CONFIG_MODULES */

/**
 *	dwarf_unwinder_init - initialise the dwarf unwinder
 *
 *	Build the data structures describing the .dwarf_frame section to
 *	make it easier to lookup CIE and FDE entries. Because the
 *	.eh_frame section is packed as tightly as possible it is not
 *	easy to lookup the FDE for a given PC, so we build a list of FDE
 *	and CIE entries that make it easier.
 */
static int __init dwarf_unwinder_init(void)
{
	int err;

	dwarf_frame_cachep = kmem_cache_create("dwarf_frames",
			sizeof(struct dwarf_frame), 0,
			SLAB_PANIC | SLAB_HWCACHE_ALIGN | SLAB_NOTRACK, NULL);

	dwarf_reg_cachep = kmem_cache_create("dwarf_regs",
			sizeof(struct dwarf_reg), 0,
			SLAB_PANIC | SLAB_HWCACHE_ALIGN | SLAB_NOTRACK, NULL);

	dwarf_frame_pool = mempool_create(DWARF_FRAME_MIN_REQ,
					  mempool_alloc_slab,
					  mempool_free_slab,
					  dwarf_frame_cachep);

	dwarf_reg_pool = mempool_create(DWARF_REG_MIN_REQ,
					 mempool_alloc_slab,
					 mempool_free_slab,
					 dwarf_reg_cachep);

	err = dwarf_parse_section(__start_eh_frame, __stop_eh_frame, NULL);
	if (err)
		goto out;

	err = unwinder_register(&dwarf_unwinder);
	if (err)
		goto out;

	return 0;

out:
	printk(KERN_ERR "Failed to initialise DWARF unwinder: %d\n", err);
	dwarf_unwinder_cleanup();
	return -EINVAL;
}
early_initcall(dwarf_unwinder_init);
