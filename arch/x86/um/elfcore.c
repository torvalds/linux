#include <linux/elf.h>
#include <linux/coredump.h>
#include <linux/fs.h>
#include <linux/mm.h>

#include <asm/elf.h>


Elf32_Half elf_core_extra_phdrs(void)
{
	return vsyscall_ehdr ? (((struct elfhdr *)vsyscall_ehdr)->e_phnum) : 0;
}

int elf_core_write_extra_phdrs(struct coredump_params *cprm, loff_t offset)
{
	if ( vsyscall_ehdr ) {
		const struct elfhdr *const ehdrp =
			(struct elfhdr *) vsyscall_ehdr;
		const struct elf_phdr *const phdrp =
			(const struct elf_phdr *) (vsyscall_ehdr + ehdrp->e_phoff);
		int i;
		Elf32_Off ofs = 0;

		for (i = 0; i < ehdrp->e_phnum; ++i) {
			struct elf_phdr phdr = phdrp[i];

			if (phdr.p_type == PT_LOAD) {
				ofs = phdr.p_offset = offset;
				offset += phdr.p_filesz;
			} else {
				phdr.p_offset += ofs;
			}
			phdr.p_paddr = 0; /* match other core phdrs */
			if (!dump_emit(cprm, &phdr, sizeof(phdr)))
				return 0;
		}
	}
	return 1;
}

int elf_core_write_extra_data(struct coredump_params *cprm)
{
	if ( vsyscall_ehdr ) {
		const struct elfhdr *const ehdrp =
			(struct elfhdr *) vsyscall_ehdr;
		const struct elf_phdr *const phdrp =
			(const struct elf_phdr *) (vsyscall_ehdr + ehdrp->e_phoff);
		int i;

		for (i = 0; i < ehdrp->e_phnum; ++i) {
			if (phdrp[i].p_type == PT_LOAD) {
				void *addr = (void *) phdrp[i].p_vaddr;
				size_t filesz = phdrp[i].p_filesz;
				if (!dump_emit(cprm, addr, filesz))
					return 0;
			}
		}
	}
	return 1;
}

size_t elf_core_extra_data_size(void)
{
	if ( vsyscall_ehdr ) {
		const struct elfhdr *const ehdrp =
			(struct elfhdr *)vsyscall_ehdr;
		const struct elf_phdr *const phdrp =
			(const struct elf_phdr *) (vsyscall_ehdr + ehdrp->e_phoff);
		int i;

		for (i = 0; i < ehdrp->e_phnum; ++i)
			if (phdrp[i].p_type == PT_LOAD)
				return (size_t) phdrp[i].p_filesz;
	}
	return 0;
}
