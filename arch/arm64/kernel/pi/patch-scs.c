// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 * Author: Ard Biesheuvel <ardb@google.com>
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/types.h>

#include <asm/scs.h>

#include "pi.h"

bool dynamic_scs_is_enabled;

//
// This minimal DWARF CFI parser is partially based on the code in
// arch/arc/kernel/unwind.c, and on the document below:
// https://refspecs.linuxbase.org/LSB_4.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html
//

#define DW_CFA_nop                          0x00
#define DW_CFA_set_loc                      0x01
#define DW_CFA_advance_loc1                 0x02
#define DW_CFA_advance_loc2                 0x03
#define DW_CFA_advance_loc4                 0x04
#define DW_CFA_offset_extended              0x05
#define DW_CFA_restore_extended             0x06
#define DW_CFA_undefined                    0x07
#define DW_CFA_same_value                   0x08
#define DW_CFA_register                     0x09
#define DW_CFA_remember_state               0x0a
#define DW_CFA_restore_state                0x0b
#define DW_CFA_def_cfa                      0x0c
#define DW_CFA_def_cfa_register             0x0d
#define DW_CFA_def_cfa_offset               0x0e
#define DW_CFA_def_cfa_expression           0x0f
#define DW_CFA_expression                   0x10
#define DW_CFA_offset_extended_sf           0x11
#define DW_CFA_def_cfa_sf                   0x12
#define DW_CFA_def_cfa_offset_sf            0x13
#define DW_CFA_val_offset                   0x14
#define DW_CFA_val_offset_sf                0x15
#define DW_CFA_val_expression               0x16
#define DW_CFA_lo_user                      0x1c
#define DW_CFA_negate_ra_state              0x2d
#define DW_CFA_GNU_args_size                0x2e
#define DW_CFA_GNU_negative_offset_extended 0x2f
#define DW_CFA_hi_user                      0x3f

#define DW_EH_PE_sdata4                     0x0b
#define DW_EH_PE_sdata8                     0x0c
#define DW_EH_PE_pcrel                      0x10

enum {
	PACIASP		= 0xd503233f,
	AUTIASP		= 0xd50323bf,
	SCS_PUSH	= 0xf800865e,
	SCS_POP		= 0xf85f8e5e,
};

static void __always_inline scs_patch_loc(u64 loc)
{
	u32 insn = le32_to_cpup((void *)loc);

	switch (insn) {
	case PACIASP:
		*(u32 *)loc = cpu_to_le32(SCS_PUSH);
		break;
	case AUTIASP:
		*(u32 *)loc = cpu_to_le32(SCS_POP);
		break;
	default:
		/*
		 * While the DW_CFA_negate_ra_state directive is guaranteed to
		 * appear right after a PACIASP/AUTIASP instruction, it may
		 * also appear after a DW_CFA_restore_state directive that
		 * restores a state that is only partially accurate, and is
		 * followed by DW_CFA_negate_ra_state directive to toggle the
		 * PAC bit again. So we permit other instructions here, and ignore
		 * them.
		 */
		return;
	}
	if (IS_ENABLED(CONFIG_ARM64_WORKAROUND_CLEAN_CACHE))
		asm("dc civac, %0" :: "r"(loc));
	else
		asm(ALTERNATIVE("dc cvau, %0", "nop", ARM64_HAS_CACHE_IDC)
		    :: "r"(loc));
}

/*
 * Skip one uleb128/sleb128 encoded quantity from the opcode stream. All bytes
 * except the last one have bit #7 set.
 */
static int __always_inline skip_xleb128(const u8 **opcode, int size)
{
	u8 c;

	do {
		c = *(*opcode)++;
		size--;
	} while (c & BIT(7));

	return size;
}

struct eh_frame {
	/*
	 * The size of this frame if 0 < size < U32_MAX, 0 terminates the list.
	 */
	u32	size;

	/*
	 * The first frame is a Common Information Entry (CIE) frame, followed
	 * by one or more Frame Description Entry (FDE) frames. In the former
	 * case, this field is 0, otherwise it is the negated offset relative
	 * to the associated CIE frame.
	 */
	u32	cie_id_or_pointer;

	union {
		struct { // CIE
			u8	version;
			u8	augmentation_string[3];
			u8	code_alignment_factor;
			u8	data_alignment_factor;
			u8	return_address_register;
			u8	augmentation_data_size;
			u8	fde_pointer_format;
		};

		struct { // FDE
			s32	initial_loc;
			s32	range;
			u8	opcodes[];
		};

		struct { // FDE
			s64	initial_loc64;
			s64	range64;
			u8	opcodes64[];
		};
	};
};

static int scs_handle_fde_frame(const struct eh_frame *frame,
				int code_alignment_factor,
				bool use_sdata8,
				bool dry_run)
{
	int size = frame->size - offsetof(struct eh_frame, opcodes) + 4;
	u64 loc = (u64)offset_to_ptr(&frame->initial_loc);
	const u8 *opcode = frame->opcodes;
	int l;

	if (use_sdata8) {
		loc = (u64)&frame->initial_loc64 + frame->initial_loc64;
		opcode = frame->opcodes64;
		size -= 8;
	}

	// assume single byte uleb128_t for augmentation data size
	if (*opcode & BIT(7))
		return EDYNSCS_INVALID_FDE_AUGM_DATA_SIZE;

	l = *opcode++;
	opcode += l;
	size -= l + 1;

	/*
	 * Starting from 'loc', apply the CFA opcodes that advance the location
	 * pointer, and identify the locations of the PAC instructions.
	 */
	while (size-- > 0) {
		switch (*opcode++) {
		case DW_CFA_nop:
		case DW_CFA_remember_state:
		case DW_CFA_restore_state:
			break;

		case DW_CFA_advance_loc1:
			loc += *opcode++ * code_alignment_factor;
			size--;
			break;

		case DW_CFA_advance_loc2:
			loc += *opcode++ * code_alignment_factor;
			loc += (*opcode++ << 8) * code_alignment_factor;
			size -= 2;
			break;

		case DW_CFA_def_cfa:
		case DW_CFA_offset_extended:
			size = skip_xleb128(&opcode, size);
			fallthrough;
		case DW_CFA_def_cfa_offset:
		case DW_CFA_def_cfa_offset_sf:
		case DW_CFA_def_cfa_register:
		case DW_CFA_same_value:
		case DW_CFA_restore_extended:
		case 0x80 ... 0xbf:
			size = skip_xleb128(&opcode, size);
			break;

		case DW_CFA_negate_ra_state:
			if (!dry_run)
				scs_patch_loc(loc - 4);
			break;

		case 0x40 ... 0x7f:
			// advance loc
			loc += (opcode[-1] & 0x3f) * code_alignment_factor;
			break;

		case 0xc0 ... 0xff:
			break;

		default:
			return EDYNSCS_INVALID_CFA_OPCODE;
		}
	}
	return 0;
}

int scs_patch(const u8 eh_frame[], int size)
{
	int code_alignment_factor = 1;
	bool fde_use_sdata8 = false;
	const u8 *p = eh_frame;

	while (size > 4) {
		const struct eh_frame *frame = (const void *)p;
		int ret;

		if (frame->size == 0 ||
		    frame->size == U32_MAX ||
		    frame->size > size)
			break;

		if (frame->cie_id_or_pointer == 0) {
			/*
			 * Require presence of augmentation data (z) with a
			 * specifier for the size of the FDE initial_loc and
			 * range fields (R), and nothing else.
			 */
			if (strcmp(frame->augmentation_string, "zR"))
				return EDYNSCS_INVALID_CIE_HEADER;

			/*
			 * The code alignment factor is a uleb128 encoded field
			 * but given that the only sensible values are 1 or 4,
			 * there is no point in decoding the whole thing.  Also
			 * sanity check the size of the data alignment factor
			 * field, and the values of the return address register
			 * and augmentation data size fields.
			 */
			if ((frame->code_alignment_factor & BIT(7)) ||
			    (frame->data_alignment_factor & BIT(7)) ||
			    frame->return_address_register != 30 ||
			    frame->augmentation_data_size != 1)
				return EDYNSCS_INVALID_CIE_HEADER;

			code_alignment_factor = frame->code_alignment_factor;

			switch (frame->fde_pointer_format) {
			case DW_EH_PE_pcrel | DW_EH_PE_sdata4:
				fde_use_sdata8 = false;
				break;
			case DW_EH_PE_pcrel | DW_EH_PE_sdata8:
				fde_use_sdata8 = true;
				break;
			default:
				return EDYNSCS_INVALID_CIE_SDATA_SIZE;
			}
		} else {
			ret = scs_handle_fde_frame(frame, code_alignment_factor,
						   fde_use_sdata8, true);
			if (ret)
				return ret;
			scs_handle_fde_frame(frame, code_alignment_factor,
					     fde_use_sdata8, false);
		}

		p += sizeof(frame->size) + frame->size;
		size -= sizeof(frame->size) + frame->size;
	}
	return 0;
}
