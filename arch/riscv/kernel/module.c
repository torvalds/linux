// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Copyright (C) 2017 Zihao Yu
 */

#include <linux/elf.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/hashtable.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/moduleloader.h>
#include <linux/sizes.h>
#include <linux/pgtable.h>
#include <asm/alternative.h>
#include <asm/sections.h>

struct used_bucket {
	struct list_head head;
	struct hlist_head *bucket;
};

struct relocation_head {
	struct hlist_node node;
	struct list_head rel_entry;
	void *location;
};

struct relocation_entry {
	struct list_head head;
	Elf_Addr value;
	unsigned int type;
};

struct relocation_handlers {
	int (*reloc_handler)(struct module *me, void *location, Elf_Addr v);
	int (*accumulate_handler)(struct module *me, void *location,
				  long buffer);
};

/*
 * The auipc+jalr instruction pair can reach any PC-relative offset
 * in the range [-2^31 - 2^11, 2^31 - 2^11)
 */
static bool riscv_insn_valid_32bit_offset(ptrdiff_t val)
{
#ifdef CONFIG_32BIT
	return true;
#else
	return (-(1L << 31) - (1L << 11)) <= val && val < ((1L << 31) - (1L << 11));
#endif
}

static int riscv_insn_rmw(void *location, u32 keep, u32 set)
{
	__le16 *parcel = location;
	u32 insn = (u32)le16_to_cpu(parcel[0]) | (u32)le16_to_cpu(parcel[1]) << 16;

	insn &= keep;
	insn |= set;

	parcel[0] = cpu_to_le16(insn);
	parcel[1] = cpu_to_le16(insn >> 16);
	return 0;
}

static int riscv_insn_rvc_rmw(void *location, u16 keep, u16 set)
{
	__le16 *parcel = location;
	u16 insn = le16_to_cpu(*parcel);

	insn &= keep;
	insn |= set;

	*parcel = cpu_to_le16(insn);
	return 0;
}

static int apply_r_riscv_32_rela(struct module *me, void *location, Elf_Addr v)
{
	if (v != (u32)v) {
		pr_err("%s: value %016llx out of range for 32-bit field\n",
		       me->name, (long long)v);
		return -EINVAL;
	}
	*(u32 *)location = v;
	return 0;
}

static int apply_r_riscv_64_rela(struct module *me, void *location, Elf_Addr v)
{
	*(u64 *)location = v;
	return 0;
}

static int apply_r_riscv_branch_rela(struct module *me, void *location,
				     Elf_Addr v)
{
	ptrdiff_t offset = (void *)v - location;
	u32 imm12 = (offset & 0x1000) << (31 - 12);
	u32 imm11 = (offset & 0x800) >> (11 - 7);
	u32 imm10_5 = (offset & 0x7e0) << (30 - 10);
	u32 imm4_1 = (offset & 0x1e) << (11 - 4);

	return riscv_insn_rmw(location, 0x1fff07f, imm12 | imm11 | imm10_5 | imm4_1);
}

static int apply_r_riscv_jal_rela(struct module *me, void *location,
				  Elf_Addr v)
{
	ptrdiff_t offset = (void *)v - location;
	u32 imm20 = (offset & 0x100000) << (31 - 20);
	u32 imm19_12 = (offset & 0xff000);
	u32 imm11 = (offset & 0x800) << (20 - 11);
	u32 imm10_1 = (offset & 0x7fe) << (30 - 10);

	return riscv_insn_rmw(location, 0xfff, imm20 | imm19_12 | imm11 | imm10_1);
}

static int apply_r_riscv_rvc_branch_rela(struct module *me, void *location,
					 Elf_Addr v)
{
	ptrdiff_t offset = (void *)v - location;
	u16 imm8 = (offset & 0x100) << (12 - 8);
	u16 imm7_6 = (offset & 0xc0) >> (6 - 5);
	u16 imm5 = (offset & 0x20) >> (5 - 2);
	u16 imm4_3 = (offset & 0x18) << (12 - 5);
	u16 imm2_1 = (offset & 0x6) << (12 - 10);

	return riscv_insn_rvc_rmw(location, 0xe383,
			imm8 | imm7_6 | imm5 | imm4_3 | imm2_1);
}

static int apply_r_riscv_rvc_jump_rela(struct module *me, void *location,
				       Elf_Addr v)
{
	ptrdiff_t offset = (void *)v - location;
	u16 imm11 = (offset & 0x800) << (12 - 11);
	u16 imm10 = (offset & 0x400) >> (10 - 8);
	u16 imm9_8 = (offset & 0x300) << (12 - 11);
	u16 imm7 = (offset & 0x80) >> (7 - 6);
	u16 imm6 = (offset & 0x40) << (12 - 11);
	u16 imm5 = (offset & 0x20) >> (5 - 2);
	u16 imm4 = (offset & 0x10) << (12 - 5);
	u16 imm3_1 = (offset & 0xe) << (12 - 10);

	return riscv_insn_rvc_rmw(location, 0xe003,
			imm11 | imm10 | imm9_8 | imm7 | imm6 | imm5 | imm4 | imm3_1);
}

static int apply_r_riscv_pcrel_hi20_rela(struct module *me, void *location,
					 Elf_Addr v)
{
	ptrdiff_t offset = (void *)v - location;

	if (!riscv_insn_valid_32bit_offset(offset)) {
		pr_err(
		  "%s: target %016llx can not be addressed by the 32-bit offset from PC = %p\n",
		  me->name, (long long)v, location);
		return -EINVAL;
	}

	return riscv_insn_rmw(location, 0xfff, (offset + 0x800) & 0xfffff000);
}

static int apply_r_riscv_pcrel_lo12_i_rela(struct module *me, void *location,
					   Elf_Addr v)
{
	/*
	 * v is the lo12 value to fill. It is calculated before calling this
	 * handler.
	 */
	return riscv_insn_rmw(location, 0xfffff, (v & 0xfff) << 20);
}

static int apply_r_riscv_pcrel_lo12_s_rela(struct module *me, void *location,
					   Elf_Addr v)
{
	/*
	 * v is the lo12 value to fill. It is calculated before calling this
	 * handler.
	 */
	u32 imm11_5 = (v & 0xfe0) << (31 - 11);
	u32 imm4_0 = (v & 0x1f) << (11 - 4);

	return riscv_insn_rmw(location, 0x1fff07f, imm11_5 | imm4_0);
}

static int apply_r_riscv_hi20_rela(struct module *me, void *location,
				   Elf_Addr v)
{
	if (IS_ENABLED(CONFIG_CMODEL_MEDLOW)) {
		pr_err(
		  "%s: target %016llx can not be addressed by the 32-bit offset from PC = %p\n",
		  me->name, (long long)v, location);
		return -EINVAL;
	}

	return riscv_insn_rmw(location, 0xfff, ((s32)v + 0x800) & 0xfffff000);
}

static int apply_r_riscv_lo12_i_rela(struct module *me, void *location,
				     Elf_Addr v)
{
	/* Skip medlow checking because of filtering by HI20 already */
	s32 hi20 = ((s32)v + 0x800) & 0xfffff000;
	s32 lo12 = ((s32)v - hi20);

	return riscv_insn_rmw(location, 0xfffff, (lo12 & 0xfff) << 20);
}

static int apply_r_riscv_lo12_s_rela(struct module *me, void *location,
				     Elf_Addr v)
{
	/* Skip medlow checking because of filtering by HI20 already */
	s32 hi20 = ((s32)v + 0x800) & 0xfffff000;
	s32 lo12 = ((s32)v - hi20);
	u32 imm11_5 = (lo12 & 0xfe0) << (31 - 11);
	u32 imm4_0 = (lo12 & 0x1f) << (11 - 4);

	return riscv_insn_rmw(location, 0x1fff07f, imm11_5 | imm4_0);
}

static int apply_r_riscv_got_hi20_rela(struct module *me, void *location,
				       Elf_Addr v)
{
	ptrdiff_t offset = (void *)v - location;

	/* Always emit the got entry */
	if (IS_ENABLED(CONFIG_MODULE_SECTIONS)) {
		offset = (void *)module_emit_got_entry(me, v) - location;
	} else {
		pr_err(
		  "%s: can not generate the GOT entry for symbol = %016llx from PC = %p\n",
		  me->name, (long long)v, location);
		return -EINVAL;
	}

	return riscv_insn_rmw(location, 0xfff, (offset + 0x800) & 0xfffff000);
}

static int apply_r_riscv_call_plt_rela(struct module *me, void *location,
				       Elf_Addr v)
{
	ptrdiff_t offset = (void *)v - location;
	u32 hi20, lo12;

	if (!riscv_insn_valid_32bit_offset(offset)) {
		/* Only emit the plt entry if offset over 32-bit range */
		if (IS_ENABLED(CONFIG_MODULE_SECTIONS)) {
			offset = (void *)module_emit_plt_entry(me, v) - location;
		} else {
			pr_err(
			  "%s: target %016llx can not be addressed by the 32-bit offset from PC = %p\n",
			  me->name, (long long)v, location);
			return -EINVAL;
		}
	}

	hi20 = (offset + 0x800) & 0xfffff000;
	lo12 = (offset - hi20) & 0xfff;
	riscv_insn_rmw(location, 0xfff, hi20);
	return riscv_insn_rmw(location + 4, 0xfffff, lo12 << 20);
}

static int apply_r_riscv_call_rela(struct module *me, void *location,
				   Elf_Addr v)
{
	ptrdiff_t offset = (void *)v - location;
	u32 hi20, lo12;

	if (!riscv_insn_valid_32bit_offset(offset)) {
		pr_err(
		  "%s: target %016llx can not be addressed by the 32-bit offset from PC = %p\n",
		  me->name, (long long)v, location);
		return -EINVAL;
	}

	hi20 = (offset + 0x800) & 0xfffff000;
	lo12 = (offset - hi20) & 0xfff;
	riscv_insn_rmw(location, 0xfff, hi20);
	return riscv_insn_rmw(location + 4, 0xfffff, lo12 << 20);
}

static int apply_r_riscv_relax_rela(struct module *me, void *location,
				    Elf_Addr v)
{
	return 0;
}

static int apply_r_riscv_align_rela(struct module *me, void *location,
				    Elf_Addr v)
{
	pr_err(
	  "%s: The unexpected relocation type 'R_RISCV_ALIGN' from PC = %p\n",
	  me->name, location);
	return -EINVAL;
}

static int apply_r_riscv_add8_rela(struct module *me, void *location, Elf_Addr v)
{
	*(u8 *)location += (u8)v;
	return 0;
}

static int apply_r_riscv_add16_rela(struct module *me, void *location,
				    Elf_Addr v)
{
	*(u16 *)location += (u16)v;
	return 0;
}

static int apply_r_riscv_add32_rela(struct module *me, void *location,
				    Elf_Addr v)
{
	*(u32 *)location += (u32)v;
	return 0;
}

static int apply_r_riscv_add64_rela(struct module *me, void *location,
				    Elf_Addr v)
{
	*(u64 *)location += (u64)v;
	return 0;
}

static int apply_r_riscv_sub8_rela(struct module *me, void *location, Elf_Addr v)
{
	*(u8 *)location -= (u8)v;
	return 0;
}

static int apply_r_riscv_sub16_rela(struct module *me, void *location,
				    Elf_Addr v)
{
	*(u16 *)location -= (u16)v;
	return 0;
}

static int apply_r_riscv_sub32_rela(struct module *me, void *location,
				    Elf_Addr v)
{
	*(u32 *)location -= (u32)v;
	return 0;
}

static int apply_r_riscv_sub64_rela(struct module *me, void *location,
				    Elf_Addr v)
{
	*(u64 *)location -= (u64)v;
	return 0;
}

static int dynamic_linking_not_supported(struct module *me, void *location,
					 Elf_Addr v)
{
	pr_err("%s: Dynamic linking not supported in kernel modules PC = %p\n",
	       me->name, location);
	return -EINVAL;
}

static int tls_not_supported(struct module *me, void *location, Elf_Addr v)
{
	pr_err("%s: Thread local storage not supported in kernel modules PC = %p\n",
	       me->name, location);
	return -EINVAL;
}

static int apply_r_riscv_sub6_rela(struct module *me, void *location, Elf_Addr v)
{
	u8 *byte = location;
	u8 value = v;

	*byte = (*byte - (value & 0x3f)) & 0x3f;
	return 0;
}

static int apply_r_riscv_set6_rela(struct module *me, void *location, Elf_Addr v)
{
	u8 *byte = location;
	u8 value = v;

	*byte = (*byte & 0xc0) | (value & 0x3f);
	return 0;
}

static int apply_r_riscv_set8_rela(struct module *me, void *location, Elf_Addr v)
{
	*(u8 *)location = (u8)v;
	return 0;
}

static int apply_r_riscv_set16_rela(struct module *me, void *location,
				    Elf_Addr v)
{
	*(u16 *)location = (u16)v;
	return 0;
}

static int apply_r_riscv_set32_rela(struct module *me, void *location,
				    Elf_Addr v)
{
	*(u32 *)location = (u32)v;
	return 0;
}

static int apply_r_riscv_32_pcrel_rela(struct module *me, void *location,
				       Elf_Addr v)
{
	*(u32 *)location = v - (uintptr_t)location;
	return 0;
}

static int apply_r_riscv_plt32_rela(struct module *me, void *location,
				    Elf_Addr v)
{
	ptrdiff_t offset = (void *)v - location;

	if (!riscv_insn_valid_32bit_offset(offset)) {
		/* Only emit the plt entry if offset over 32-bit range */
		if (IS_ENABLED(CONFIG_MODULE_SECTIONS)) {
			offset = (void *)module_emit_plt_entry(me, v) - location;
		} else {
			pr_err("%s: target %016llx can not be addressed by the 32-bit offset from PC = %p\n",
			       me->name, (long long)v, location);
			return -EINVAL;
		}
	}

	*(u32 *)location = (u32)offset;
	return 0;
}

static int apply_r_riscv_set_uleb128(struct module *me, void *location, Elf_Addr v)
{
	*(long *)location = v;
	return 0;
}

static int apply_r_riscv_sub_uleb128(struct module *me, void *location, Elf_Addr v)
{
	*(long *)location -= v;
	return 0;
}

static int apply_6_bit_accumulation(struct module *me, void *location, long buffer)
{
	u8 *byte = location;
	u8 value = buffer;

	if (buffer > 0x3f) {
		pr_err("%s: value %ld out of range for 6-bit relocation.\n",
		       me->name, buffer);
		return -EINVAL;
	}

	*byte = (*byte & 0xc0) | (value & 0x3f);
	return 0;
}

static int apply_8_bit_accumulation(struct module *me, void *location, long buffer)
{
	if (buffer > U8_MAX) {
		pr_err("%s: value %ld out of range for 8-bit relocation.\n",
		       me->name, buffer);
		return -EINVAL;
	}
	*(u8 *)location = (u8)buffer;
	return 0;
}

static int apply_16_bit_accumulation(struct module *me, void *location, long buffer)
{
	if (buffer > U16_MAX) {
		pr_err("%s: value %ld out of range for 16-bit relocation.\n",
		       me->name, buffer);
		return -EINVAL;
	}
	*(u16 *)location = (u16)buffer;
	return 0;
}

static int apply_32_bit_accumulation(struct module *me, void *location, long buffer)
{
	if (buffer > U32_MAX) {
		pr_err("%s: value %ld out of range for 32-bit relocation.\n",
		       me->name, buffer);
		return -EINVAL;
	}
	*(u32 *)location = (u32)buffer;
	return 0;
}

static int apply_64_bit_accumulation(struct module *me, void *location, long buffer)
{
	*(u64 *)location = (u64)buffer;
	return 0;
}

static int apply_uleb128_accumulation(struct module *me, void *location, long buffer)
{
	/*
	 * ULEB128 is a variable length encoding. Encode the buffer into
	 * the ULEB128 data format.
	 */
	u8 *p = location;

	while (buffer != 0) {
		u8 value = buffer & 0x7f;

		buffer >>= 7;
		value |= (!!buffer) << 7;

		*p++ = value;
	}
	return 0;
}

/*
 * Relocations defined in the riscv-elf-psabi-doc.
 * This handles static linking only.
 */
static const struct relocation_handlers reloc_handlers[] = {
	[R_RISCV_32]		= { .reloc_handler = apply_r_riscv_32_rela },
	[R_RISCV_64]		= { .reloc_handler = apply_r_riscv_64_rela },
	[R_RISCV_RELATIVE]	= { .reloc_handler = dynamic_linking_not_supported },
	[R_RISCV_COPY]		= { .reloc_handler = dynamic_linking_not_supported },
	[R_RISCV_JUMP_SLOT]	= { .reloc_handler = dynamic_linking_not_supported },
	[R_RISCV_TLS_DTPMOD32]	= { .reloc_handler = dynamic_linking_not_supported },
	[R_RISCV_TLS_DTPMOD64]	= { .reloc_handler = dynamic_linking_not_supported },
	[R_RISCV_TLS_DTPREL32]	= { .reloc_handler = dynamic_linking_not_supported },
	[R_RISCV_TLS_DTPREL64]	= { .reloc_handler = dynamic_linking_not_supported },
	[R_RISCV_TLS_TPREL32]	= { .reloc_handler = dynamic_linking_not_supported },
	[R_RISCV_TLS_TPREL64]	= { .reloc_handler = dynamic_linking_not_supported },
	/* 12-15 undefined */
	[R_RISCV_BRANCH]	= { .reloc_handler = apply_r_riscv_branch_rela },
	[R_RISCV_JAL]		= { .reloc_handler = apply_r_riscv_jal_rela },
	[R_RISCV_CALL]		= { .reloc_handler = apply_r_riscv_call_rela },
	[R_RISCV_CALL_PLT]	= { .reloc_handler = apply_r_riscv_call_plt_rela },
	[R_RISCV_GOT_HI20]	= { .reloc_handler = apply_r_riscv_got_hi20_rela },
	[R_RISCV_TLS_GOT_HI20]	= { .reloc_handler = tls_not_supported },
	[R_RISCV_TLS_GD_HI20]	= { .reloc_handler = tls_not_supported },
	[R_RISCV_PCREL_HI20]	= { .reloc_handler = apply_r_riscv_pcrel_hi20_rela },
	[R_RISCV_PCREL_LO12_I]	= { .reloc_handler = apply_r_riscv_pcrel_lo12_i_rela },
	[R_RISCV_PCREL_LO12_S]	= { .reloc_handler = apply_r_riscv_pcrel_lo12_s_rela },
	[R_RISCV_HI20]		= { .reloc_handler = apply_r_riscv_hi20_rela },
	[R_RISCV_LO12_I]	= { .reloc_handler = apply_r_riscv_lo12_i_rela },
	[R_RISCV_LO12_S]	= { .reloc_handler = apply_r_riscv_lo12_s_rela },
	[R_RISCV_TPREL_HI20]	= { .reloc_handler = tls_not_supported },
	[R_RISCV_TPREL_LO12_I]	= { .reloc_handler = tls_not_supported },
	[R_RISCV_TPREL_LO12_S]	= { .reloc_handler = tls_not_supported },
	[R_RISCV_TPREL_ADD]	= { .reloc_handler = tls_not_supported },
	[R_RISCV_ADD8]		= { .reloc_handler = apply_r_riscv_add8_rela,
				    .accumulate_handler = apply_8_bit_accumulation },
	[R_RISCV_ADD16]		= { .reloc_handler = apply_r_riscv_add16_rela,
				    .accumulate_handler = apply_16_bit_accumulation },
	[R_RISCV_ADD32]		= { .reloc_handler = apply_r_riscv_add32_rela,
				    .accumulate_handler = apply_32_bit_accumulation },
	[R_RISCV_ADD64]		= { .reloc_handler = apply_r_riscv_add64_rela,
				    .accumulate_handler = apply_64_bit_accumulation },
	[R_RISCV_SUB8]		= { .reloc_handler = apply_r_riscv_sub8_rela,
				    .accumulate_handler = apply_8_bit_accumulation },
	[R_RISCV_SUB16]		= { .reloc_handler = apply_r_riscv_sub16_rela,
				    .accumulate_handler = apply_16_bit_accumulation },
	[R_RISCV_SUB32]		= { .reloc_handler = apply_r_riscv_sub32_rela,
				    .accumulate_handler = apply_32_bit_accumulation },
	[R_RISCV_SUB64]		= { .reloc_handler = apply_r_riscv_sub64_rela,
				    .accumulate_handler = apply_64_bit_accumulation },
	/* 41-42 reserved for future standard use */
	[R_RISCV_ALIGN]		= { .reloc_handler = apply_r_riscv_align_rela },
	[R_RISCV_RVC_BRANCH]	= { .reloc_handler = apply_r_riscv_rvc_branch_rela },
	[R_RISCV_RVC_JUMP]	= { .reloc_handler = apply_r_riscv_rvc_jump_rela },
	/* 46-50 reserved for future standard use */
	[R_RISCV_RELAX]		= { .reloc_handler = apply_r_riscv_relax_rela },
	[R_RISCV_SUB6]		= { .reloc_handler = apply_r_riscv_sub6_rela,
				    .accumulate_handler = apply_6_bit_accumulation },
	[R_RISCV_SET6]		= { .reloc_handler = apply_r_riscv_set6_rela,
				    .accumulate_handler = apply_6_bit_accumulation },
	[R_RISCV_SET8]		= { .reloc_handler = apply_r_riscv_set8_rela,
				    .accumulate_handler = apply_8_bit_accumulation },
	[R_RISCV_SET16]		= { .reloc_handler = apply_r_riscv_set16_rela,
				    .accumulate_handler = apply_16_bit_accumulation },
	[R_RISCV_SET32]		= { .reloc_handler = apply_r_riscv_set32_rela,
				    .accumulate_handler = apply_32_bit_accumulation },
	[R_RISCV_32_PCREL]	= { .reloc_handler = apply_r_riscv_32_pcrel_rela },
	[R_RISCV_IRELATIVE]	= { .reloc_handler = dynamic_linking_not_supported },
	[R_RISCV_PLT32]		= { .reloc_handler = apply_r_riscv_plt32_rela },
	[R_RISCV_SET_ULEB128]	= { .reloc_handler = apply_r_riscv_set_uleb128,
				    .accumulate_handler = apply_uleb128_accumulation },
	[R_RISCV_SUB_ULEB128]	= { .reloc_handler = apply_r_riscv_sub_uleb128,
				    .accumulate_handler = apply_uleb128_accumulation },
	/* 62-191 reserved for future standard use */
	/* 192-255 nonstandard ABI extensions  */
};

static void
process_accumulated_relocations(struct module *me,
				struct hlist_head **relocation_hashtable,
				struct list_head *used_buckets_list)
{
	/*
	 * Only ADD/SUB/SET/ULEB128 should end up here.
	 *
	 * Each bucket may have more than one relocation location. All
	 * relocations for a location are stored in a list in a bucket.
	 *
	 * Relocations are applied to a temp variable before being stored to the
	 * provided location to check for overflow. This also allows ULEB128 to
	 * properly decide how many entries are needed before storing to
	 * location. The final value is stored into location using the handler
	 * for the last relocation to an address.
	 *
	 * Three layers of indexing:
	 *	- Each of the buckets in use
	 *	- Groups of relocations in each bucket by location address
	 *	- Each relocation entry for a location address
	 */
	struct used_bucket *bucket_iter;
	struct used_bucket *bucket_iter_tmp;
	struct relocation_head *rel_head_iter;
	struct hlist_node *rel_head_iter_tmp;
	struct relocation_entry *rel_entry_iter;
	struct relocation_entry *rel_entry_iter_tmp;
	int curr_type;
	void *location;
	long buffer;

	list_for_each_entry_safe(bucket_iter, bucket_iter_tmp,
				 used_buckets_list, head) {
		hlist_for_each_entry_safe(rel_head_iter, rel_head_iter_tmp,
					  bucket_iter->bucket, node) {
			buffer = 0;
			location = rel_head_iter->location;
			list_for_each_entry_safe(rel_entry_iter,
						 rel_entry_iter_tmp,
						 &rel_head_iter->rel_entry,
						 head) {
				curr_type = rel_entry_iter->type;
				reloc_handlers[curr_type].reloc_handler(
					me, &buffer, rel_entry_iter->value);
				kfree(rel_entry_iter);
			}
			reloc_handlers[curr_type].accumulate_handler(
				me, location, buffer);
			kfree(rel_head_iter);
		}
		kfree(bucket_iter);
	}

	kvfree(*relocation_hashtable);
}

static int add_relocation_to_accumulate(struct module *me, int type,
					void *location,
					unsigned int hashtable_bits, Elf_Addr v,
					struct hlist_head *relocation_hashtable,
					struct list_head *used_buckets_list)
{
	struct relocation_entry *entry;
	struct relocation_head *rel_head;
	struct hlist_head *current_head;
	struct used_bucket *bucket;
	unsigned long hash;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);

	if (!entry)
		return -ENOMEM;

	INIT_LIST_HEAD(&entry->head);
	entry->type = type;
	entry->value = v;

	hash = hash_min((uintptr_t)location, hashtable_bits);

	current_head = &relocation_hashtable[hash];

	/*
	 * Search for the relocation_head for the relocations that happen at the
	 * provided location
	 */
	bool found = false;
	struct relocation_head *rel_head_iter;

	hlist_for_each_entry(rel_head_iter, current_head, node) {
		if (rel_head_iter->location == location) {
			found = true;
			rel_head = rel_head_iter;
			break;
		}
	}

	/*
	 * If there has not yet been any relocations at the provided location,
	 * create a relocation_head for that location and populate it with this
	 * relocation_entry.
	 */
	if (!found) {
		rel_head = kmalloc(sizeof(*rel_head), GFP_KERNEL);

		if (!rel_head) {
			kfree(entry);
			return -ENOMEM;
		}

		INIT_LIST_HEAD(&rel_head->rel_entry);
		rel_head->location = location;
		INIT_HLIST_NODE(&rel_head->node);
		if (!current_head->first) {
			bucket =
				kmalloc(sizeof(struct used_bucket), GFP_KERNEL);

			if (!bucket) {
				kfree(entry);
				kfree(rel_head);
				return -ENOMEM;
			}

			INIT_LIST_HEAD(&bucket->head);
			bucket->bucket = current_head;
			list_add(&bucket->head, used_buckets_list);
		}
		hlist_add_head(&rel_head->node, current_head);
	}

	/* Add relocation to head of discovered rel_head */
	list_add_tail(&entry->head, &rel_head->rel_entry);

	return 0;
}

static unsigned int
initialize_relocation_hashtable(unsigned int num_relocations,
				struct hlist_head **relocation_hashtable)
{
	/* Can safely assume that bits is not greater than sizeof(long) */
	unsigned long hashtable_size = roundup_pow_of_two(num_relocations);
	/*
	 * When hashtable_size == 1, hashtable_bits == 0.
	 * This is valid because the hashing algorithm returns 0 in this case.
	 */
	unsigned int hashtable_bits = ilog2(hashtable_size);

	/*
	 * Double size of hashtable if num_relocations * 1.25 is greater than
	 * hashtable_size.
	 */
	int should_double_size = ((num_relocations + (num_relocations >> 2)) > (hashtable_size));

	hashtable_bits += should_double_size;

	hashtable_size <<= should_double_size;

	/* Number of relocations may be large, so kvmalloc it */
	*relocation_hashtable = kvmalloc_array(hashtable_size,
					       sizeof(**relocation_hashtable),
					       GFP_KERNEL);
	if (!*relocation_hashtable)
		return 0;

	__hash_init(*relocation_hashtable, hashtable_size);

	return hashtable_bits;
}

int apply_relocate_add(Elf_Shdr *sechdrs, const char *strtab,
		       unsigned int symindex, unsigned int relsec,
		       struct module *me)
{
	Elf_Rela *rel = (void *) sechdrs[relsec].sh_addr;
	int (*handler)(struct module *me, void *location, Elf_Addr v);
	Elf_Sym *sym;
	void *location;
	unsigned int i, type;
	unsigned int j_idx = 0;
	Elf_Addr v;
	int res;
	unsigned int num_relocations = sechdrs[relsec].sh_size / sizeof(*rel);
	struct hlist_head *relocation_hashtable;
	unsigned int hashtable_bits;
	LIST_HEAD(used_buckets_list);

	hashtable_bits = initialize_relocation_hashtable(num_relocations,
							 &relocation_hashtable);

	if (!relocation_hashtable)
		return -ENOMEM;

	pr_debug("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);

	for (i = 0; i < num_relocations; i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf_Sym *)sechdrs[symindex].sh_addr
			+ ELF_RISCV_R_SYM(rel[i].r_info);
		if (IS_ERR_VALUE(sym->st_value)) {
			/* Ignore unresolved weak symbol */
			if (ELF_ST_BIND(sym->st_info) == STB_WEAK)
				continue;
			pr_warn("%s: Unknown symbol %s\n",
				me->name, strtab + sym->st_name);
			return -ENOENT;
		}

		type = ELF_RISCV_R_TYPE(rel[i].r_info);

		if (type < ARRAY_SIZE(reloc_handlers))
			handler = reloc_handlers[type].reloc_handler;
		else
			handler = NULL;

		if (!handler) {
			pr_err("%s: Unknown relocation type %u\n",
			       me->name, type);
			return -EINVAL;
		}

		v = sym->st_value + rel[i].r_addend;

		if (type == R_RISCV_PCREL_LO12_I || type == R_RISCV_PCREL_LO12_S) {
			unsigned int j = j_idx;
			bool found = false;

			do {
				unsigned long hi20_loc =
					sechdrs[sechdrs[relsec].sh_info].sh_addr
					+ rel[j].r_offset;
				u32 hi20_type = ELF_RISCV_R_TYPE(rel[j].r_info);

				/* Find the corresponding HI20 relocation entry */
				if (hi20_loc == sym->st_value
				    && (hi20_type == R_RISCV_PCREL_HI20
					|| hi20_type == R_RISCV_GOT_HI20)) {
					s32 hi20, lo12;
					Elf_Sym *hi20_sym =
						(Elf_Sym *)sechdrs[symindex].sh_addr
						+ ELF_RISCV_R_SYM(rel[j].r_info);
					unsigned long hi20_sym_val =
						hi20_sym->st_value
						+ rel[j].r_addend;

					/* Calculate lo12 */
					size_t offset = hi20_sym_val - hi20_loc;
					if (IS_ENABLED(CONFIG_MODULE_SECTIONS)
					    && hi20_type == R_RISCV_GOT_HI20) {
						offset = module_emit_got_entry(
							 me, hi20_sym_val);
						offset = offset - hi20_loc;
					}
					hi20 = (offset + 0x800) & 0xfffff000;
					lo12 = offset - hi20;
					v = lo12;
					found = true;

					break;
				}

				j++;
				if (j == num_relocations)
					j = 0;

			} while (j_idx != j);

			if (!found) {
				pr_err(
				  "%s: Can not find HI20 relocation information\n",
				  me->name);
				return -EINVAL;
			}

			/* Record the previous j-loop end index */
			j_idx = j;
		}

		if (reloc_handlers[type].accumulate_handler)
			res = add_relocation_to_accumulate(me, type, location,
							   hashtable_bits, v,
							   relocation_hashtable,
							   &used_buckets_list);
		else
			res = handler(me, location, v);
		if (res)
			return res;
	}

	process_accumulated_relocations(me, &relocation_hashtable,
					&used_buckets_list);

	return 0;
}

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	const Elf_Shdr *s;

	s = find_section(hdr, sechdrs, ".alternative");
	if (s)
		apply_module_alternatives((void *)s->sh_addr, s->sh_size);

	return 0;
}
