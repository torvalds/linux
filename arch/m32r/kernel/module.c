/*  Kernel module help for M32R.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt...)
#endif

#define COPY_UNALIGNED_WORD(sw, tw, align) \
{ \
	void *__s = &(sw), *__t = &(tw); \
	unsigned short *__s2 = __s, *__t2 =__t; \
	unsigned char *__s1 = __s, *__t1 =__t; \
	switch ((align)) \
	{ \
	case 0: \
		*(unsigned long *) __t = *(unsigned long *) __s; \
		break; \
	case 2: \
		*__t2++ = *__s2++; \
		*__t2 = *__s2; \
		break; \
	default: \
		*__t1++ = *__s1++; \
		*__t1++ = *__s1++; \
		*__t1++ = *__s1++; \
		*__t1 = *__s1; \
		break; \
	} \
}

#define COPY_UNALIGNED_HWORD(sw, tw, align) \
  { \
    void *__s = &(sw), *__t = &(tw); \
    unsigned short *__s2 = __s, *__t2 =__t; \
    unsigned char *__s1 = __s, *__t1 =__t; \
    switch ((align)) \
    { \
    case 0: \
      *__t2 = *__s2; \
      break; \
    default: \
      *__t1++ = *__s1++; \
      *__t1 = *__s1; \
      break; \
    } \
  }

int apply_relocate_add(Elf32_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	unsigned int i;
	Elf32_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	Elf32_Addr relocation;
	uint32_t *location;
	uint32_t value;
	unsigned short *hlocation;
	unsigned short hvalue;
	int svalue;
	int align;

	DEBUGP("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);
		relocation = sym->st_value + rel[i].r_addend;
		align = (int)location & 3;

		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_M32R_32_RELA:
	    		COPY_UNALIGNED_WORD (*location, value, align);
			value += relocation;
	    		COPY_UNALIGNED_WORD (value, *location, align);
			break;
		case R_M32R_HI16_ULO_RELA:
	    		COPY_UNALIGNED_WORD (*location, value, align);
                        relocation = (relocation >>16) & 0xffff;
			/* RELA must has 0 at relocation field. */
			value += relocation;
	    		COPY_UNALIGNED_WORD (value, *location, align);
			break;
		case R_M32R_HI16_SLO_RELA:
	    		COPY_UNALIGNED_WORD (*location, value, align);
			if (relocation & 0x8000) relocation += 0x10000;
                        relocation = (relocation >>16) & 0xffff;
			/* RELA must has 0 at relocation field. */
			value += relocation;
	    		COPY_UNALIGNED_WORD (value, *location, align);
			break;
		case R_M32R_16_RELA:
			hlocation = (unsigned short *)location;
                        relocation = relocation & 0xffff;
			/* RELA must has 0 at relocation field. */
			hvalue = relocation;
	    		COPY_UNALIGNED_WORD (hvalue, *hlocation, align);
			break;
		case R_M32R_SDA16_RELA:
		case R_M32R_LO16_RELA:
	    		COPY_UNALIGNED_WORD (*location, value, align);
                        relocation = relocation & 0xffff;
			/* RELA must has 0 at relocation field. */
			value += relocation;
	    		COPY_UNALIGNED_WORD (value, *location, align);
			break;
		case R_M32R_24_RELA:
	    		COPY_UNALIGNED_WORD (*location, value, align);
                        relocation = relocation & 0xffffff;
			/* RELA must has 0 at relocation field. */
			value += relocation;
	    		COPY_UNALIGNED_WORD (value, *location, align);
			break;
		case R_M32R_18_PCREL_RELA:
	  		relocation = (relocation - (Elf32_Addr) location);
			if (relocation < -0x20000 || 0x1fffc < relocation)
				{
					printk(KERN_ERR "module %s: relocation overflow: %u\n",
					me->name, relocation);
					return -ENOEXEC;
				}
	    		COPY_UNALIGNED_WORD (*location, value, align);
			if (value & 0xffff)
				{
					/* RELA must has 0 at relocation field. */
					printk(KERN_ERR "module %s: illegal relocation field: %u\n",
					me->name, value);
					return -ENOEXEC;
				}
                        relocation = (relocation >> 2) & 0xffff;
			value += relocation;
	    		COPY_UNALIGNED_WORD (value, *location, align);
			break;
		case R_M32R_10_PCREL_RELA:
			hlocation = (unsigned short *)location;
	  		relocation = (relocation - (Elf32_Addr) location);
	    		COPY_UNALIGNED_HWORD (*hlocation, hvalue, align);
			svalue = (int)hvalue;
			svalue = (signed char)svalue << 2;
			relocation += svalue;
                        relocation = (relocation >> 2) & 0xff;
			hvalue = hvalue & 0xff00;
			hvalue += relocation;
	    		COPY_UNALIGNED_HWORD (hvalue, *hlocation, align);
			break;
		case R_M32R_26_PCREL_RELA:
	  		relocation = (relocation - (Elf32_Addr) location);
			if (relocation < -0x2000000 || 0x1fffffc < relocation)
				{
					printk(KERN_ERR "module %s: relocation overflow: %u\n",
					me->name, relocation);
					return -ENOEXEC;
				}
	    		COPY_UNALIGNED_WORD (*location, value, align);
			if (value & 0xffffff)
				{
					/* RELA must has 0 at relocation field. */
					printk(KERN_ERR "module %s: illegal relocation field: %u\n",
					me->name, value);
					return -ENOEXEC;
				}
                        relocation = (relocation >> 2) & 0xffffff;
			value += relocation;
	    		COPY_UNALIGNED_WORD (value, *location, align);
			break;
		default:
			printk(KERN_ERR "module %s: Unknown relocation: %u\n",
			       me->name, ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}
