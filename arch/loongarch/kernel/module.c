// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#define pr_fmt(fmt) "kmod: " fmt

#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/mm.h>
#include <linux/numa.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>

static int rela_stack_push(s64 stack_value, s64 *rela_stack, size_t *rela_stack_top)
{
	if (*rela_stack_top >= RELA_STACK_DEPTH)
		return -ENOEXEC;

	rela_stack[(*rela_stack_top)++] = stack_value;
	pr_debug("%s stack_value = 0x%llx\n", __func__, stack_value);

	return 0;
}

static int rela_stack_pop(s64 *stack_value, s64 *rela_stack, size_t *rela_stack_top)
{
	if (*rela_stack_top == 0)
		return -ENOEXEC;

	*stack_value = rela_stack[--(*rela_stack_top)];
	pr_debug("%s stack_value = 0x%llx\n", __func__, *stack_value);

	return 0;
}

static int apply_r_larch_none(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	return 0;
}

static int apply_r_larch_error(struct module *me, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	pr_err("%s: Unsupport relocation type %u, please add its support.\n", me->name, type);
	return -EINVAL;
}

static int apply_r_larch_32(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	*location = v;
	return 0;
}

static int apply_r_larch_64(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	*(Elf_Addr *)location = v;
	return 0;
}

static int apply_r_larch_sop_push_pcrel(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	return rela_stack_push(v - (u64)location, rela_stack, rela_stack_top);
}

static int apply_r_larch_sop_push_absolute(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	return rela_stack_push(v, rela_stack, rela_stack_top);
}

static int apply_r_larch_sop_push_dup(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	int err = 0;
	s64 opr1;

	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_push(opr1, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_push(opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	return 0;
}

static int apply_r_larch_sop_push_plt_pcrel(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	ptrdiff_t offset = (void *)v - (void *)location;

	if (offset >= SZ_128M)
		v = module_emit_plt_entry(mod, v);

	if (offset < -SZ_128M)
		v = module_emit_plt_entry(mod, v);

	return apply_r_larch_sop_push_pcrel(mod, location, v, rela_stack, rela_stack_top, type);
}

static int apply_r_larch_sop(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	int err = 0;
	s64 opr1, opr2, opr3;

	if (type == R_LARCH_SOP_IF_ELSE) {
		err = rela_stack_pop(&opr3, rela_stack, rela_stack_top);
		if (err)
			return err;
	}

	err = rela_stack_pop(&opr2, rela_stack, rela_stack_top);
	if (err)
		return err;
	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	switch (type) {
	case R_LARCH_SOP_AND:
		err = rela_stack_push(opr1 & opr2, rela_stack, rela_stack_top);
		break;
	case R_LARCH_SOP_ADD:
		err = rela_stack_push(opr1 + opr2, rela_stack, rela_stack_top);
		break;
	case R_LARCH_SOP_SUB:
		err = rela_stack_push(opr1 - opr2, rela_stack, rela_stack_top);
		break;
	case R_LARCH_SOP_SL:
		err = rela_stack_push(opr1 << opr2, rela_stack, rela_stack_top);
		break;
	case R_LARCH_SOP_SR:
		err = rela_stack_push(opr1 >> opr2, rela_stack, rela_stack_top);
		break;
	case R_LARCH_SOP_IF_ELSE:
		err = rela_stack_push(opr1 ? opr2 : opr3, rela_stack, rela_stack_top);
		break;
	default:
		pr_err("%s: Unsupport relocation type %u\n", mod->name, type);
		return -EINVAL;
	}

	return err;
}

static int apply_r_larch_sop_imm_field(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	int err = 0;
	s64 opr1;
	union loongarch_instruction *insn = (union loongarch_instruction *)location;

	err = rela_stack_pop(&opr1, rela_stack, rela_stack_top);
	if (err)
		return err;

	switch (type) {
	case R_LARCH_SOP_POP_32_U_10_12:
		if (!unsigned_imm_check(opr1, 12))
			goto overflow;

		/* (*(uint32_t *) PC) [21 ... 10] = opr [11 ... 0] */
		insn->reg2i12_format.immediate = opr1 & 0xfff;
		return 0;
	case R_LARCH_SOP_POP_32_S_10_12:
		if (!signed_imm_check(opr1, 12))
			goto overflow;

		insn->reg2i12_format.immediate = opr1 & 0xfff;
		return 0;
	case R_LARCH_SOP_POP_32_S_10_16:
		if (!signed_imm_check(opr1, 16))
			goto overflow;

		insn->reg2i16_format.immediate = opr1 & 0xffff;
		return 0;
	case R_LARCH_SOP_POP_32_S_10_16_S2:
		if (opr1 % 4)
			goto unaligned;

		if (!signed_imm_check(opr1, 18))
			goto overflow;

		insn->reg2i16_format.immediate = (opr1 >> 2) & 0xffff;
		return 0;
	case R_LARCH_SOP_POP_32_S_5_20:
		if (!signed_imm_check(opr1, 20))
			goto overflow;

		insn->reg1i20_format.immediate = (opr1) & 0xfffff;
		return 0;
	case R_LARCH_SOP_POP_32_S_0_5_10_16_S2:
		if (opr1 % 4)
			goto unaligned;

		if (!signed_imm_check(opr1, 23))
			goto overflow;

		opr1 >>= 2;
		insn->reg1i21_format.immediate_l = opr1 & 0xffff;
		insn->reg1i21_format.immediate_h = (opr1 >> 16) & 0x1f;
		return 0;
	case R_LARCH_SOP_POP_32_S_0_10_10_16_S2:
		if (opr1 % 4)
			goto unaligned;

		if (!signed_imm_check(opr1, 28))
			goto overflow;

		opr1 >>= 2;
		insn->reg0i26_format.immediate_l = opr1 & 0xffff;
		insn->reg0i26_format.immediate_h = (opr1 >> 16) & 0x3ff;
		return 0;
	case R_LARCH_SOP_POP_32_U:
		if (!unsigned_imm_check(opr1, 32))
			goto overflow;

		/* (*(uint32_t *) PC) = opr */
		*location = (u32)opr1;
		return 0;
	default:
		pr_err("%s: Unsupport relocation type %u\n", mod->name, type);
		return -EINVAL;
	}

overflow:
	pr_err("module %s: opr1 = 0x%llx overflow! dangerous %s (%u) relocation\n",
		mod->name, opr1, __func__, type);
	return -ENOEXEC;

unaligned:
	pr_err("module %s: opr1 = 0x%llx unaligned! dangerous %s (%u) relocation\n",
		mod->name, opr1, __func__, type);
	return -ENOEXEC;
}

static int apply_r_larch_add_sub(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	switch (type) {
	case R_LARCH_ADD32:
		*(s32 *)location += v;
		return 0;
	case R_LARCH_ADD64:
		*(s64 *)location += v;
		return 0;
	case R_LARCH_SUB32:
		*(s32 *)location -= v;
		return 0;
	case R_LARCH_SUB64:
		*(s64 *)location -= v;
		return 0;
	default:
		pr_err("%s: Unsupport relocation type %u\n", mod->name, type);
		return -EINVAL;
	}
}

static int apply_r_larch_b26(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	ptrdiff_t offset = (void *)v - (void *)location;
	union loongarch_instruction *insn = (union loongarch_instruction *)location;

	if (offset >= SZ_128M)
		v = module_emit_plt_entry(mod, v);

	if (offset < -SZ_128M)
		v = module_emit_plt_entry(mod, v);

	offset = (void *)v - (void *)location;

	if (offset & 3) {
		pr_err("module %s: jump offset = 0x%llx unaligned! dangerous R_LARCH_B26 (%u) relocation\n",
				mod->name, (long long)offset, type);
		return -ENOEXEC;
	}

	if (!signed_imm_check(offset, 28)) {
		pr_err("module %s: jump offset = 0x%llx overflow! dangerous R_LARCH_B26 (%u) relocation\n",
				mod->name, (long long)offset, type);
		return -ENOEXEC;
	}

	offset >>= 2;
	insn->reg0i26_format.immediate_l = offset & 0xffff;
	insn->reg0i26_format.immediate_h = (offset >> 16) & 0x3ff;

	return 0;
}

static int apply_r_larch_pcala(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	union loongarch_instruction *insn = (union loongarch_instruction *)location;
	/* Use s32 for a sign-extension deliberately. */
	s32 offset_hi20 = (void *)((v + 0x800) & ~0xfff) -
			  (void *)((Elf_Addr)location & ~0xfff);
	Elf_Addr anchor = (((Elf_Addr)location) & ~0xfff) + offset_hi20;
	ptrdiff_t offset_rem = (void *)v - (void *)anchor;

	switch (type) {
	case R_LARCH_PCALA_LO12:
		insn->reg2i12_format.immediate = v & 0xfff;
		break;
	case R_LARCH_PCALA_HI20:
		v = offset_hi20 >> 12;
		insn->reg1i20_format.immediate = v & 0xfffff;
		break;
	case R_LARCH_PCALA64_LO20:
		v = offset_rem >> 32;
		insn->reg1i20_format.immediate = v & 0xfffff;
		break;
	case R_LARCH_PCALA64_HI12:
		v = offset_rem >> 52;
		insn->reg2i12_format.immediate = v & 0xfff;
		break;
	default:
		pr_err("%s: Unsupport relocation type %u\n", mod->name, type);
		return -EINVAL;
	}

	return 0;
}

static int apply_r_larch_got_pc(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type)
{
	Elf_Addr got = module_emit_got_entry(mod, v);

	if (!got)
		return -EINVAL;

	switch (type) {
	case R_LARCH_GOT_PC_LO12:
		type = R_LARCH_PCALA_LO12;
		break;
	case R_LARCH_GOT_PC_HI20:
		type = R_LARCH_PCALA_HI20;
		break;
	default:
		pr_err("%s: Unsupport relocation type %u\n", mod->name, type);
		return -EINVAL;
	}

	return apply_r_larch_pcala(mod, location, got, rela_stack, rela_stack_top, type);
}

/*
 * reloc_handlers_rela() - Apply a particular relocation to a module
 * @mod: the module to apply the reloc to
 * @location: the address at which the reloc is to be applied
 * @v: the value of the reloc, with addend for RELA-style
 * @rela_stack: the stack used for store relocation info, LOCAL to THIS module
 * @rela_stac_top: where the stack operation(pop/push) applies to
 *
 * Return: 0 upon success, else -ERRNO
 */
typedef int (*reloc_rela_handler)(struct module *mod, u32 *location, Elf_Addr v,
			s64 *rela_stack, size_t *rela_stack_top, unsigned int type);

/* The handlers for known reloc types */
static reloc_rela_handler reloc_rela_handlers[] = {
	[R_LARCH_NONE ... R_LARCH_64_PCREL]		     = apply_r_larch_error,

	[R_LARCH_NONE]					     = apply_r_larch_none,
	[R_LARCH_32]					     = apply_r_larch_32,
	[R_LARCH_64]					     = apply_r_larch_64,
	[R_LARCH_MARK_LA]				     = apply_r_larch_none,
	[R_LARCH_MARK_PCREL]				     = apply_r_larch_none,
	[R_LARCH_SOP_PUSH_PCREL]			     = apply_r_larch_sop_push_pcrel,
	[R_LARCH_SOP_PUSH_ABSOLUTE]			     = apply_r_larch_sop_push_absolute,
	[R_LARCH_SOP_PUSH_DUP]				     = apply_r_larch_sop_push_dup,
	[R_LARCH_SOP_PUSH_PLT_PCREL]			     = apply_r_larch_sop_push_plt_pcrel,
	[R_LARCH_SOP_SUB ... R_LARCH_SOP_IF_ELSE] 	     = apply_r_larch_sop,
	[R_LARCH_SOP_POP_32_S_10_5 ... R_LARCH_SOP_POP_32_U] = apply_r_larch_sop_imm_field,
	[R_LARCH_ADD32 ... R_LARCH_SUB64]		     = apply_r_larch_add_sub,
	[R_LARCH_B26]					     = apply_r_larch_b26,
	[R_LARCH_PCALA_HI20...R_LARCH_PCALA64_HI12]	     = apply_r_larch_pcala,
	[R_LARCH_GOT_PC_HI20...R_LARCH_GOT_PC_LO12]	     = apply_r_larch_got_pc,
};

int apply_relocate_add(Elf_Shdr *sechdrs, const char *strtab,
		       unsigned int symindex, unsigned int relsec,
		       struct module *mod)
{
	int i, err;
	unsigned int type;
	s64 rela_stack[RELA_STACK_DEPTH];
	size_t rela_stack_top = 0;
	reloc_rela_handler handler;
	void *location;
	Elf_Addr v;
	Elf_Sym *sym;
	Elf_Rela *rel = (void *) sechdrs[relsec].sh_addr;

	pr_debug("%s: Applying relocate section %u to %u\n", __func__, relsec,
	       sechdrs[relsec].sh_info);

	rela_stack_top = 0;
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr + rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf_Sym *)sechdrs[symindex].sh_addr + ELF_R_SYM(rel[i].r_info);
		if (IS_ERR_VALUE(sym->st_value)) {
			/* Ignore unresolved weak symbol */
			if (ELF_ST_BIND(sym->st_info) == STB_WEAK)
				continue;
			pr_warn("%s: Unknown symbol %s\n", mod->name, strtab + sym->st_name);
			return -ENOENT;
		}

		type = ELF_R_TYPE(rel[i].r_info);

		if (type < ARRAY_SIZE(reloc_rela_handlers))
			handler = reloc_rela_handlers[type];
		else
			handler = NULL;

		if (!handler) {
			pr_err("%s: Unknown relocation type %u\n", mod->name, type);
			return -EINVAL;
		}

		pr_debug("type %d st_value %llx r_addend %llx loc %llx\n",
		       (int)ELF_R_TYPE(rel[i].r_info),
		       sym->st_value, rel[i].r_addend, (u64)location);

		v = sym->st_value + rel[i].r_addend;
		err = handler(mod, location, v, rela_stack, &rela_stack_top, type);
		if (err)
			return err;
	}

	return 0;
}

void *module_alloc(unsigned long size)
{
	return __vmalloc_node_range(size, 1, MODULES_VADDR, MODULES_END,
			GFP_KERNEL, PAGE_KERNEL, 0, NUMA_NO_NODE, __builtin_return_address(0));
}
