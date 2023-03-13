// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 * Author: Ard Biesheuvel <ardb@google.com>
 */

#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/printk.h>
#include <linux/types.h>

#include <asm/cacheflush.h>
#include <asm/scs.h>

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

extern const u8 __eh_frame_start[], __eh_frame_end[];

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
	dcache_clean_pou(loc, loc + sizeof(u32));
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
			u8	augmentation_string[];
		};

		struct { // FDE
			s32	initial_loc;
			s32	range;
			u8	opcodes[];
		};
	};
};

static int noinstr scs_handle_fde_frame(const struct eh_frame *frame,
					bool fde_has_augmentation_data,
					int code_alignment_factor,
					bool dry_run)
{
	int size = frame->size - offsetof(struct eh_frame, opcodes) + 4;
	u64 loc = (u64)offset_to_ptr(&frame->initial_loc);
	const u8 *opcode = frame->opcodes;

	if (fde_has_augmentation_data) {
		int l;

		// assume single byte uleb128_t
		if (WARN_ON(*opcode & BIT(7)))
			return -ENOEXEC;

		l = *opcode++;
		opcode += l;
		size -= l + 1;
	}

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
			pr_err("unhandled opcode: %02x in FDE frame %lx\n", opcode[-1], (uintptr_t)frame);
			return -ENOEXEC;
		}
	}
	return 0;
}

int noinstr scs_patch(const u8 eh_frame[], int size)
{
	const u8 *p = eh_frame;

	while (size > 4) {
		const struct eh_frame *frame = (const void *)p;
		bool fde_has_augmentation_data = true;
		int code_alignment_factor = 1;
		int ret;

		if (frame->size == 0 ||
		    frame->size == U32_MAX ||
		    frame->size > size)
			break;

		if (frame->cie_id_or_pointer == 0) {
			const u8 *p = frame->augmentation_string;

			/* a 'z' in the augmentation string must come first */
			fde_has_augmentation_data = *p == 'z';

			/*
			 * The code alignment factor is a uleb128 encoded field
			 * but given that the only sensible values are 1 or 4,
			 * there is no point in decoding the whole thing.
			 */
			p += strlen(p) + 1;
			if (!WARN_ON(*p & BIT(7)))
				code_alignment_factor = *p;
		} else {
			ret = scs_handle_fde_frame(frame,
						   fde_has_augmentation_data,
						   code_alignment_factor,
						   true);
			if (ret)
				return ret;
			scs_handle_fde_frame(frame, fde_has_augmentation_data,
					     code_alignment_factor, false);
		}

		p += sizeof(frame->size) + frame->size;
		size -= sizeof(frame->size) + frame->size;
	}
	return 0;
}

asmlinkage void __init scs_patch_vmlinux(void)
{
	if (!should_patch_pac_into_scs())
		return;

	WARN_ON(scs_patch(__eh_frame_start, __eh_frame_end - __eh_frame_start));
	icache_inval_all_pou();
	isb();
}
