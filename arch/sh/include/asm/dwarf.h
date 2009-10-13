/*
 * Copyright (C) 2009 Matt Fleming <matt@console-pimps.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#ifndef __ASM_SH_DWARF_H
#define __ASM_SH_DWARF_H

#ifdef CONFIG_DWARF_UNWINDER

/*
 * DWARF expression operations
 */
#define DW_OP_addr	0x03
#define DW_OP_deref	0x06
#define DW_OP_const1u	0x08
#define DW_OP_const1s	0x09
#define DW_OP_const2u	0x0a
#define DW_OP_const2s	0x0b
#define DW_OP_const4u	0x0c
#define DW_OP_const4s	0x0d
#define DW_OP_const8u	0x0e
#define DW_OP_const8s	0x0f
#define DW_OP_constu	0x10
#define DW_OP_consts	0x11
#define DW_OP_dup	0x12
#define DW_OP_drop	0x13
#define DW_OP_over	0x14
#define DW_OP_pick	0x15
#define DW_OP_swap	0x16
#define DW_OP_rot	0x17
#define DW_OP_xderef	0x18
#define DW_OP_abs	0x19
#define DW_OP_and	0x1a
#define DW_OP_div	0x1b
#define DW_OP_minus	0x1c
#define DW_OP_mod	0x1d
#define DW_OP_mul	0x1e
#define DW_OP_neg	0x1f
#define DW_OP_not	0x20
#define DW_OP_or	0x21
#define DW_OP_plus	0x22
#define DW_OP_plus_uconst	0x23
#define DW_OP_shl	0x24
#define DW_OP_shr	0x25
#define DW_OP_shra	0x26
#define DW_OP_xor	0x27
#define DW_OP_skip	0x2f
#define DW_OP_bra	0x28
#define DW_OP_eq	0x29
#define DW_OP_ge	0x2a
#define DW_OP_gt	0x2b
#define DW_OP_le	0x2c
#define DW_OP_lt	0x2d
#define DW_OP_ne	0x2e
#define DW_OP_lit0	0x30
#define DW_OP_lit1	0x31
#define DW_OP_lit2	0x32
#define DW_OP_lit3	0x33
#define DW_OP_lit4	0x34
#define DW_OP_lit5	0x35
#define DW_OP_lit6	0x36
#define DW_OP_lit7	0x37
#define DW_OP_lit8	0x38
#define DW_OP_lit9	0x39
#define DW_OP_lit10	0x3a
#define DW_OP_lit11	0x3b
#define DW_OP_lit12	0x3c
#define DW_OP_lit13	0x3d
#define DW_OP_lit14	0x3e
#define DW_OP_lit15	0x3f
#define DW_OP_lit16	0x40
#define DW_OP_lit17	0x41
#define DW_OP_lit18	0x42
#define DW_OP_lit19	0x43
#define DW_OP_lit20	0x44
#define DW_OP_lit21	0x45
#define DW_OP_lit22	0x46
#define DW_OP_lit23	0x47
#define DW_OP_lit24	0x48
#define DW_OP_lit25	0x49
#define DW_OP_lit26	0x4a
#define DW_OP_lit27	0x4b
#define DW_OP_lit28	0x4c
#define DW_OP_lit29	0x4d
#define DW_OP_lit30	0x4e
#define DW_OP_lit31	0x4f
#define DW_OP_reg0	0x50
#define DW_OP_reg1	0x51
#define DW_OP_reg2	0x52
#define DW_OP_reg3	0x53
#define DW_OP_reg4	0x54
#define DW_OP_reg5	0x55
#define DW_OP_reg6	0x56
#define DW_OP_reg7	0x57
#define DW_OP_reg8	0x58
#define DW_OP_reg9	0x59
#define DW_OP_reg10	0x5a
#define DW_OP_reg11	0x5b
#define DW_OP_reg12	0x5c
#define DW_OP_reg13	0x5d
#define DW_OP_reg14	0x5e
#define DW_OP_reg15	0x5f
#define DW_OP_reg16	0x60
#define DW_OP_reg17	0x61
#define DW_OP_reg18	0x62
#define DW_OP_reg19	0x63
#define DW_OP_reg20	0x64
#define DW_OP_reg21	0x65
#define DW_OP_reg22	0x66
#define DW_OP_reg23	0x67
#define DW_OP_reg24	0x68
#define DW_OP_reg25	0x69
#define DW_OP_reg26	0x6a
#define DW_OP_reg27	0x6b
#define DW_OP_reg28	0x6c
#define DW_OP_reg29	0x6d
#define DW_OP_reg30	0x6e
#define DW_OP_reg31	0x6f
#define DW_OP_breg0	0x70
#define DW_OP_breg1	0x71
#define DW_OP_breg2	0x72
#define DW_OP_breg3	0x73
#define DW_OP_breg4	0x74
#define DW_OP_breg5	0x75
#define DW_OP_breg6	0x76
#define DW_OP_breg7	0x77
#define DW_OP_breg8	0x78
#define DW_OP_breg9	0x79
#define DW_OP_breg10	0x7a
#define DW_OP_breg11	0x7b
#define DW_OP_breg12	0x7c
#define DW_OP_breg13	0x7d
#define DW_OP_breg14	0x7e
#define DW_OP_breg15	0x7f
#define DW_OP_breg16	0x80
#define DW_OP_breg17	0x81
#define DW_OP_breg18	0x82
#define DW_OP_breg19	0x83
#define DW_OP_breg20	0x84
#define DW_OP_breg21	0x85
#define DW_OP_breg22	0x86
#define DW_OP_breg23	0x87
#define DW_OP_breg24	0x88
#define DW_OP_breg25	0x89
#define DW_OP_breg26	0x8a
#define DW_OP_breg27	0x8b
#define DW_OP_breg28	0x8c
#define DW_OP_breg29	0x8d
#define DW_OP_breg30	0x8e
#define DW_OP_breg31	0x8f
#define DW_OP_regx	0x90
#define DW_OP_fbreg	0x91
#define DW_OP_bregx	0x92
#define DW_OP_piece	0x93
#define DW_OP_deref_size	0x94
#define DW_OP_xderef_size	0x95
#define DW_OP_nop	0x96
#define DW_OP_push_object_address	0x97
#define DW_OP_call2	0x98
#define DW_OP_call4	0x99
#define DW_OP_call_ref	0x9a
#define DW_OP_form_tls_address	0x9b
#define DW_OP_call_frame_cfa	0x9c
#define DW_OP_bit_piece	0x9d
#define DW_OP_lo_user	0xe0
#define DW_OP_hi_user	0xff

/*
 * Addresses used in FDE entries in the .eh_frame section may be encoded
 * using one of the following encodings.
 */
#define DW_EH_PE_absptr	0x00
#define DW_EH_PE_omit	0xff
#define DW_EH_PE_uleb128	0x01
#define DW_EH_PE_udata2	0x02
#define DW_EH_PE_udata4	0x03
#define DW_EH_PE_udata8	0x04
#define DW_EH_PE_sleb128	0x09
#define DW_EH_PE_sdata2	0x0a
#define DW_EH_PE_sdata4	0x0b
#define DW_EH_PE_sdata8	0x0c
#define DW_EH_PE_signed	0x09

#define DW_EH_PE_pcrel	0x10

/*
 * The architecture-specific register number that contains the return
 * address in the .debug_frame table.
 */
#define DWARF_ARCH_RA_REG	17

#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <linux/bug.h>
#include <linux/list.h>
#include <linux/module.h>

/*
 * Read either the frame pointer (r14) or the stack pointer (r15).
 * NOTE: this MUST be inlined.
 */
static __always_inline unsigned long dwarf_read_arch_reg(unsigned int reg)
{
	unsigned long value = 0;

	switch (reg) {
	case 14:
		__asm__ __volatile__("mov r14, %0\n" : "=r" (value));
		break;
	case 15:
		__asm__ __volatile__("mov r15, %0\n" : "=r" (value));
		break;
	default:
		BUG();
	}

	return value;
}

/**
 *	dwarf_cie - Common Information Entry
 */
struct dwarf_cie {
	unsigned long length;
	unsigned long cie_id;
	unsigned char version;
	const char *augmentation;
	unsigned int code_alignment_factor;
	int data_alignment_factor;

	/* Which column in the rule table represents return addr of func. */
	unsigned int return_address_reg;

	unsigned char *initial_instructions;
	unsigned char *instructions_end;

	unsigned char encoding;

	unsigned long cie_pointer;

	struct list_head link;

	unsigned long flags;
#define DWARF_CIE_Z_AUGMENTATION	(1 << 0)

	/*
	 * 'mod' will be non-NULL if this CIE came from a module's
	 * .eh_frame section.
	 */
	struct module *mod;
};

/**
 *	dwarf_fde - Frame Description Entry
 */
struct dwarf_fde {
	unsigned long length;
	unsigned long cie_pointer;
	struct dwarf_cie *cie;
	unsigned long initial_location;
	unsigned long address_range;
	unsigned char *instructions;
	unsigned char *end;
	struct list_head link;

	/*
	 * 'mod' will be non-NULL if this FDE came from a module's
	 * .eh_frame section.
	 */
	struct module *mod;
};

/**
 *	dwarf_frame - DWARF information for a frame in the call stack
 */
struct dwarf_frame {
	struct dwarf_frame *prev, *next;

	unsigned long pc;

	struct list_head reg_list;

	unsigned long cfa;

	/* Valid when DW_FRAME_CFA_REG_OFFSET is set in flags */
	unsigned int cfa_register;
	unsigned int cfa_offset;

	/* Valid when DW_FRAME_CFA_REG_EXP is set in flags */
	unsigned char *cfa_expr;
	unsigned int cfa_expr_len;

	unsigned long flags;
#define DWARF_FRAME_CFA_REG_OFFSET	(1 << 0)
#define DWARF_FRAME_CFA_REG_EXP		(1 << 1)

	unsigned long return_addr;
};

/**
 *	dwarf_reg - DWARF register
 *	@flags: Describes how to calculate the value of this register
 */
struct dwarf_reg {
	struct list_head link;

	unsigned int number;

	unsigned long addr;
	unsigned long flags;
#define DWARF_REG_OFFSET	(1 << 0)
#define DWARF_VAL_OFFSET	(1 << 1)
#define DWARF_UNDEFINED		(1 << 2)
};

/*
 * Call Frame instruction opcodes.
 */
#define DW_CFA_advance_loc	0x40
#define DW_CFA_offset		0x80
#define DW_CFA_restore		0xc0
#define DW_CFA_nop		0x00
#define DW_CFA_set_loc		0x01
#define DW_CFA_advance_loc1	0x02
#define DW_CFA_advance_loc2	0x03
#define DW_CFA_advance_loc4	0x04
#define DW_CFA_offset_extended	0x05
#define DW_CFA_restore_extended	0x06
#define DW_CFA_undefined	0x07
#define DW_CFA_same_value	0x08
#define DW_CFA_register		0x09
#define DW_CFA_remember_state	0x0a
#define DW_CFA_restore_state	0x0b
#define DW_CFA_def_cfa		0x0c
#define DW_CFA_def_cfa_register	0x0d
#define DW_CFA_def_cfa_offset	0x0e
#define DW_CFA_def_cfa_expression	0x0f
#define DW_CFA_expression	0x10
#define DW_CFA_offset_extended_sf	0x11
#define DW_CFA_def_cfa_sf	0x12
#define DW_CFA_def_cfa_offset_sf	0x13
#define DW_CFA_val_offset	0x14
#define DW_CFA_val_offset_sf	0x15
#define DW_CFA_val_expression	0x16
#define DW_CFA_lo_user		0x1c
#define DW_CFA_hi_user		0x3f

/* GNU extension opcodes  */
#define DW_CFA_GNU_args_size	0x2e
#define DW_CFA_GNU_negative_offset_extended 0x2f

/*
 * Some call frame instructions encode their operands in the opcode. We
 * need some helper functions to extract both the opcode and operands
 * from an instruction.
 */
static inline unsigned int DW_CFA_opcode(unsigned long insn)
{
	return (insn & 0xc0);
}

static inline unsigned int DW_CFA_operand(unsigned long insn)
{
	return (insn & 0x3f);
}

#define DW_EH_FRAME_CIE	0		/* .eh_frame CIE IDs are 0 */
#define DW_CIE_ID	0xffffffff
#define DW64_CIE_ID	0xffffffffffffffffULL

/*
 * DWARF FDE/CIE length field values.
 */
#define DW_EXT_LO	0xfffffff0
#define DW_EXT_HI	0xffffffff
#define DW_EXT_DWARF64	DW_EXT_HI

extern struct dwarf_frame *dwarf_unwind_stack(unsigned long,
					      struct dwarf_frame *);
extern void dwarf_free_frame(struct dwarf_frame *);

extern int module_dwarf_finalize(const Elf_Ehdr *, const Elf_Shdr *,
				 struct module *);
extern void module_dwarf_cleanup(struct module *);

#endif /* !__ASSEMBLY__ */

#define CFI_STARTPROC	.cfi_startproc
#define CFI_ENDPROC	.cfi_endproc
#define CFI_DEF_CFA	.cfi_def_cfa
#define CFI_REGISTER	.cfi_register
#define CFI_REL_OFFSET	.cfi_rel_offset
#define CFI_UNDEFINED	.cfi_undefined

#else

/*
 * Use the asm comment character to ignore the rest of the line.
 */
#define CFI_IGNORE	!

#define CFI_STARTPROC	CFI_IGNORE
#define CFI_ENDPROC	CFI_IGNORE
#define CFI_DEF_CFA	CFI_IGNORE
#define CFI_REGISTER	CFI_IGNORE
#define CFI_REL_OFFSET	CFI_IGNORE
#define CFI_UNDEFINED	CFI_IGNORE

#ifndef __ASSEMBLY__
static inline void dwarf_unwinder_init(void)
{
}

#define module_dwarf_finalize(hdr, sechdrs, me)	(0)
#define module_dwarf_cleanup(mod)		do { } while (0)

#endif

#endif /* CONFIG_DWARF_UNWINDER */

#endif /* __ASM_SH_DWARF_H */
