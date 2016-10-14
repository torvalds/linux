#include "relocs.h"

#define ELF_BITS 64

#define ELF_MACHINE             EM_MIPS
#define ELF_MACHINE_NAME        "MIPS64"
#define SHT_REL_TYPE            SHT_RELA
#define Elf_Rel                 Elf64_Rela

typedef uint8_t Elf64_Byte;

typedef union {
	struct {
		Elf64_Word r_sym;	/* Symbol index.  */
		Elf64_Byte r_ssym;	/* Special symbol.  */
		Elf64_Byte r_type3;	/* Third relocation.  */
		Elf64_Byte r_type2;	/* Second relocation.  */
		Elf64_Byte r_type;	/* First relocation.  */
	} fields;
	Elf64_Xword unused;
} Elf64_Mips_Rela;

#define ELF_CLASS               ELFCLASS64
#define ELF_R_SYM(val)          (((Elf64_Mips_Rela *)(&val))->fields.r_sym)
#define ELF_R_TYPE(val)         (((Elf64_Mips_Rela *)(&val))->fields.r_type)
#define ELF_ST_TYPE(o)          ELF64_ST_TYPE(o)
#define ELF_ST_BIND(o)          ELF64_ST_BIND(o)
#define ELF_ST_VISIBILITY(o)    ELF64_ST_VISIBILITY(o)

#include "relocs.c"
