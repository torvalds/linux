/*
 * File:         arch/blackfin/kernel/module.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <asm/dma.h>
#include <asm/cacheflush.h>

/*
 * handle arithmetic relocations.
 * See binutils/bfd/elf32-bfin.c for more details
 */
#define RELOC_STACK_SIZE 100
static uint32_t reloc_stack[RELOC_STACK_SIZE];
static unsigned int reloc_stack_tos;

#define is_reloc_stack_empty() ((reloc_stack_tos > 0)?0:1)

static void reloc_stack_push(uint32_t value)
{
	reloc_stack[reloc_stack_tos++] = value;
}

static uint32_t reloc_stack_pop(void)
{
	return reloc_stack[--reloc_stack_tos];
}

static uint32_t reloc_stack_operate(unsigned int oper, struct module *mod)
{
	uint32_t value;

	switch (oper) {
	case R_add:
		value = reloc_stack[reloc_stack_tos - 2] +
			reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 2;
		break;
	case R_sub:
		value = reloc_stack[reloc_stack_tos - 2] -
			reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 2;
		break;
	case R_mult:
		value = reloc_stack[reloc_stack_tos - 2] *
			reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 2;
		break;
	case R_div:
		value = reloc_stack[reloc_stack_tos - 2] /
			reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 2;
		break;
	case R_mod:
		value = reloc_stack[reloc_stack_tos - 2] %
			reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 2;
		break;
	case R_lshift:
		value = reloc_stack[reloc_stack_tos - 2] <<
			reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 2;
		break;
	case R_rshift:
		value = reloc_stack[reloc_stack_tos - 2] >>
			reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 2;
		break;
	case R_and:
		value = reloc_stack[reloc_stack_tos - 2] &
			reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 2;
		break;
	case R_or:
		value = reloc_stack[reloc_stack_tos - 2] |
			reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 2;
		break;
	case R_xor:
		value = reloc_stack[reloc_stack_tos - 2] ^
			reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 2;
		break;
	case R_land:
		value = reloc_stack[reloc_stack_tos - 2] &&
			reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 2;
		break;
	case R_lor:
		value = reloc_stack[reloc_stack_tos - 2] ||
			reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 2;
		break;
	case R_neg:
		value = -reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos--;
		break;
	case R_comp:
		value = ~reloc_stack[reloc_stack_tos - 1];
		reloc_stack_tos -= 1;
		break;
	default:
		printk(KERN_WARNING "module %s: unhandled reloction\n",
				mod->name);
		return 0;
	}

	/* now push the new value back on stack */
	reloc_stack_push(value);

	return value;
}

void *module_alloc(unsigned long size)
{
	if (size == 0)
		return NULL;
	return vmalloc(size);
}

/* Free memory returned from module_alloc */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);
}

/* Transfer the section to the L1 memory */
int
module_frob_arch_sections(Elf_Ehdr * hdr, Elf_Shdr * sechdrs,
			  char *secstrings, struct module *mod)
{
	Elf_Shdr *s, *sechdrs_end = sechdrs + hdr->e_shnum;
	void *dest = NULL;

	for (s = sechdrs; s < sechdrs_end; ++s) {
		if ((strcmp(".l1.text", secstrings + s->sh_name) == 0) ||
		    ((strcmp(".text", secstrings + s->sh_name) == 0) &&
		     (hdr->e_flags & FLG_CODE_IN_L1) && (s->sh_size > 0))) {
			mod->arch.text_l1 = s;
			dest = l1_inst_sram_alloc(s->sh_size);
			if (dest == NULL) {
				printk(KERN_ERR
				       "module %s: L1 instruction memory allocation failed\n",
				       mod->name);
				return -1;
			}
			dma_memcpy(dest, (void *)s->sh_addr, s->sh_size);
			s->sh_flags &= ~SHF_ALLOC;
			s->sh_addr = (unsigned long)dest;
		}
		if ((strcmp(".l1.data", secstrings + s->sh_name) == 0) ||
		    ((strcmp(".data", secstrings + s->sh_name) == 0) &&
		     (hdr->e_flags & FLG_DATA_IN_L1) && (s->sh_size > 0))) {
			mod->arch.data_a_l1 = s;
			dest = l1_data_sram_alloc(s->sh_size);
			if (dest == NULL) {
				printk(KERN_ERR
					"module %s: L1 data memory allocation failed\n",
					mod->name);
				return -1;
			}
			memcpy(dest, (void *)s->sh_addr, s->sh_size);
			s->sh_flags &= ~SHF_ALLOC;
			s->sh_addr = (unsigned long)dest;
		}
		if (strcmp(".l1.bss", secstrings + s->sh_name) == 0 ||
		    ((strcmp(".bss", secstrings + s->sh_name) == 0) &&
		     (hdr->e_flags & FLG_DATA_IN_L1) && (s->sh_size > 0))) {
			mod->arch.bss_a_l1 = s;
			dest = l1_data_sram_alloc(s->sh_size);
			if (dest == NULL) {
				printk(KERN_ERR
					"module %s: L1 data memory allocation failed\n",
					mod->name);
				return -1;
			}
			memset(dest, 0, s->sh_size);
			s->sh_flags &= ~SHF_ALLOC;
			s->sh_addr = (unsigned long)dest;
		}
		if (strcmp(".l1.data.B", secstrings + s->sh_name) == 0) {
			mod->arch.data_b_l1 = s;
			dest = l1_data_B_sram_alloc(s->sh_size);
			if (dest == NULL) {
				printk(KERN_ERR
					"module %s: L1 data memory allocation failed\n",
					mod->name);
				return -1;
			}
			memcpy(dest, (void *)s->sh_addr, s->sh_size);
			s->sh_flags &= ~SHF_ALLOC;
			s->sh_addr = (unsigned long)dest;
		}
		if (strcmp(".l1.bss.B", secstrings + s->sh_name) == 0) {
			mod->arch.bss_b_l1 = s;
			dest = l1_data_B_sram_alloc(s->sh_size);
			if (dest == NULL) {
				printk(KERN_ERR
					"module %s: L1 data memory allocation failed\n",
					mod->name);
				return -1;
			}
			memset(dest, 0, s->sh_size);
			s->sh_flags &= ~SHF_ALLOC;
			s->sh_addr = (unsigned long)dest;
		}
	}
	return 0;
}

int
apply_relocate(Elf_Shdr * sechdrs, const char *strtab,
	       unsigned int symindex, unsigned int relsec, struct module *me)
{
	printk(KERN_ERR "module %s: .rel unsupported\n", me->name);
	return -ENOEXEC;
}

/*************************************************************************/
/* FUNCTION : apply_relocate_add                                         */
/* ABSTRACT : Blackfin specific relocation handling for the loadable     */
/*            modules. Modules are expected to be .o files.              */
/*            Arithmetic relocations are handled.                        */
/*            We do not expect LSETUP to be split and hence is not       */
/*            handled.                                                   */
/*            R_byte and R_byte2 are also not handled as the gas         */
/*            does not generate it.                                      */
/*************************************************************************/
int
apply_relocate_add(Elf_Shdr * sechdrs, const char *strtab,
		   unsigned int symindex, unsigned int relsec,
		   struct module *mod)
{
	unsigned int i;
	unsigned short tmp;
	Elf32_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	uint32_t *location32;
	uint16_t *location16;
	uint32_t value;

	pr_debug("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location16 =
		    (uint16_t *) (sechdrs[sechdrs[relsec].sh_info].sh_addr +
				  rel[i].r_offset);
		location32 = (uint32_t *) location16;
		/* This is the symbol it is referring to. Note that all
		   undefined symbols have been resolved. */
		sym = (Elf32_Sym *) sechdrs[symindex].sh_addr
		    + ELF32_R_SYM(rel[i].r_info);
		if (is_reloc_stack_empty()) {
			value = sym->st_value;
		} else {
			value = reloc_stack_pop();
		}
		value += rel[i].r_addend;
		pr_debug("location is %x, value is %x type is %d \n",
			 (unsigned int) location32, value,
			 ELF32_R_TYPE(rel[i].r_info));

		switch (ELF32_R_TYPE(rel[i].r_info)) {

		case R_pcrel24:
		case R_pcrel24_jump_l:
			/* Add the value, subtract its postition */
			location16 =
			    (uint16_t *) (sechdrs[sechdrs[relsec].sh_info].
					  sh_addr + rel[i].r_offset - 2);
			location32 = (uint32_t *) location16;
			value -= (uint32_t) location32;
			value >>= 1;
			pr_debug("value is %x, before %x-%x after %x-%x\n", value,
			       *location16, *(location16 + 1),
			       (*location16 & 0xff00) | (value >> 16 & 0x00ff),
			       value & 0xffff);
			*location16 =
			    (*location16 & 0xff00) | (value >> 16 & 0x00ff);
			*(location16 + 1) = value & 0xffff;
			break;
		case R_pcrel12_jump:
		case R_pcrel12_jump_s:
			value -= (uint32_t) location32;
			value >>= 1;
			*location16 = (value & 0xfff);
			break;
		case R_pcrel10:
			value -= (uint32_t) location32;
			value >>= 1;
			*location16 = (value & 0x3ff);
			break;
		case R_luimm16:
			pr_debug("before %x after %x\n", *location16,
				       (value & 0xffff));
			tmp = (value & 0xffff);
			if ((unsigned long)location16 >= L1_CODE_START) {
				dma_memcpy(location16, &tmp, 2);
			} else
				*location16 = tmp;
			break;
		case R_huimm16:
			pr_debug("before %x after %x\n", *location16,
				       ((value >> 16) & 0xffff));
			tmp = ((value >> 16) & 0xffff);
			if ((unsigned long)location16 >= L1_CODE_START) {
				dma_memcpy(location16, &tmp, 2);
			} else
				*location16 = tmp;
			break;
		case R_rimm16:
			*location16 = (value & 0xffff);
			break;
		case R_byte4_data:
			pr_debug("before %x after %x\n", *location32, value);
			*location32 = value;
			break;
		case R_push:
			reloc_stack_push(value);
			break;
		case R_const:
			reloc_stack_push(rel[i].r_addend);
			break;
		case R_add:
		case R_sub:
		case R_mult:
		case R_div:
		case R_mod:
		case R_lshift:
		case R_rshift:
		case R_and:
		case R_or:
		case R_xor:
		case R_land:
		case R_lor:
		case R_neg:
		case R_comp:
			reloc_stack_operate(ELF32_R_TYPE(rel[i].r_info), mod);
			break;
		default:
			printk(KERN_ERR "module %s: Unknown relocation: %u\n",
			       mod->name, ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}

int
module_finalize(const Elf_Ehdr * hdr,
		const Elf_Shdr * sechdrs, struct module *mod)
{
	unsigned int i, strindex = 0, symindex = 0;
	char *secstrings;

	secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;

	for (i = 1; i < hdr->e_shnum; i++) {
		/* Internal symbols and strings. */
		if (sechdrs[i].sh_type == SHT_SYMTAB) {
			symindex = i;
			strindex = sechdrs[i].sh_link;
		}
	}

	for (i = 1; i < hdr->e_shnum; i++) {
		const char *strtab = (char *)sechdrs[strindex].sh_addr;
		unsigned int info = sechdrs[i].sh_info;

		/* Not a valid relocation section? */
		if (info >= hdr->e_shnum)
			continue;

		if ((sechdrs[i].sh_type == SHT_RELA) &&
		    ((strcmp(".rela.l1.text", secstrings + sechdrs[i].sh_name) == 0) ||
		    ((strcmp(".rela.text", secstrings + sechdrs[i].sh_name) == 0) &&
			 (hdr->e_flags & FLG_CODE_IN_L1)))) {
			apply_relocate_add((Elf_Shdr *) sechdrs, strtab,
					   symindex, i, mod);
		}
	}
	return 0;
}

void module_arch_cleanup(struct module *mod)
{
	if ((mod->arch.text_l1) && (mod->arch.text_l1->sh_addr))
		l1_inst_sram_free((void *)mod->arch.text_l1->sh_addr);
	if ((mod->arch.data_a_l1) && (mod->arch.data_a_l1->sh_addr))
		l1_data_sram_free((void *)mod->arch.data_a_l1->sh_addr);
	if ((mod->arch.bss_a_l1) && (mod->arch.bss_a_l1->sh_addr))
		l1_data_sram_free((void *)mod->arch.bss_a_l1->sh_addr);
	if ((mod->arch.data_b_l1) && (mod->arch.data_b_l1->sh_addr))
		l1_data_B_sram_free((void *)mod->arch.data_b_l1->sh_addr);
	if ((mod->arch.bss_b_l1) && (mod->arch.bss_b_l1->sh_addr))
		l1_data_B_sram_free((void *)mod->arch.bss_b_l1->sh_addr);
}
