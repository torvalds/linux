// SPDX-License-Identifier: GPL-2.0
/*
 * ELF loader for kexec_file_load system call.
 *
 * Copyright IBM Corp. 2018
 *
 * Author(s): Philipp Rudo <prudo@linux.vnet.ibm.com>
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <asm/setup.h>

static int kexec_file_add_elf_kernel(struct kimage *image,
				     struct s390_load_data *data,
				     char *kernel, unsigned long kernel_len)
{
	struct kexec_buf buf;
	const Elf_Ehdr *ehdr;
	const Elf_Phdr *phdr;
	Elf_Addr entry;
	int i, ret;

	ehdr = (Elf_Ehdr *)kernel;
	buf.image = image;
	if (image->type == KEXEC_TYPE_CRASH)
		entry = STARTUP_KDUMP_OFFSET;
	else
		entry = ehdr->e_entry;

	phdr = (void *)ehdr + ehdr->e_phoff;
	for (i = 0; i < ehdr->e_phnum; i++, phdr++) {
		if (phdr->p_type != PT_LOAD)
			continue;

		buf.buffer = kernel + phdr->p_offset;
		buf.bufsz = phdr->p_filesz;

		buf.mem = ALIGN(phdr->p_paddr, phdr->p_align);
		buf.memsz = phdr->p_memsz;

		if (entry - phdr->p_paddr < phdr->p_memsz) {
			data->kernel_buf = buf.buffer;
			data->memsz += STARTUP_NORMAL_OFFSET;

			buf.buffer += STARTUP_NORMAL_OFFSET;
			buf.bufsz -= STARTUP_NORMAL_OFFSET;

			buf.mem += STARTUP_NORMAL_OFFSET;
			buf.memsz -= STARTUP_NORMAL_OFFSET;
		}

		if (image->type == KEXEC_TYPE_CRASH)
			buf.mem += crashk_res.start;

		ret = kexec_add_buffer(&buf);
		if (ret)
			return ret;

		data->memsz = ALIGN(data->memsz, phdr->p_align) + buf.memsz;
	}

	return 0;
}

static void *s390_elf_load(struct kimage *image,
			   char *kernel, unsigned long kernel_len,
			   char *initrd, unsigned long initrd_len,
			   char *cmdline, unsigned long cmdline_len)
{
	struct s390_load_data data = {0};
	const Elf_Ehdr *ehdr;
	const Elf_Phdr *phdr;
	size_t size;
	int i, ret;

	/* image->fobs->probe already checked for valid ELF magic number. */
	ehdr = (Elf_Ehdr *)kernel;

	if (ehdr->e_type != ET_EXEC ||
	    ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
	    !elf_check_arch(ehdr))
		return ERR_PTR(-EINVAL);

	if (!ehdr->e_phnum || ehdr->e_phentsize != sizeof(Elf_Phdr))
		return ERR_PTR(-EINVAL);

	size = ehdr->e_ehsize + ehdr->e_phoff;
	size += ehdr->e_phentsize * ehdr->e_phnum;
	if (size > kernel_len)
		return ERR_PTR(-EINVAL);

	phdr = (void *)ehdr + ehdr->e_phoff;
	size = ALIGN(size, phdr->p_align);
	for (i = 0; i < ehdr->e_phnum; i++, phdr++) {
		if (phdr->p_type == PT_INTERP)
			return ERR_PTR(-EINVAL);

		if (phdr->p_offset > kernel_len)
			return ERR_PTR(-EINVAL);

		size += ALIGN(phdr->p_filesz, phdr->p_align);
	}

	if (size > kernel_len)
		return ERR_PTR(-EINVAL);

	ret = kexec_file_add_elf_kernel(image, &data, kernel, kernel_len);
	if (ret)
		return ERR_PTR(ret);

	if (!data.memsz)
		return ERR_PTR(-EINVAL);

	if (initrd) {
		ret = kexec_file_add_initrd(image, &data, initrd, initrd_len);
		if (ret)
			return ERR_PTR(ret);
	}

	ret = kexec_file_add_purgatory(image, &data);
	if (ret)
		return ERR_PTR(ret);

	return kexec_file_update_kernel(image, &data);
}

static int s390_elf_probe(const char *buf, unsigned long len)
{
	const Elf_Ehdr *ehdr;

	if (len < sizeof(Elf_Ehdr))
		return -ENOEXEC;

	ehdr = (Elf_Ehdr *)buf;

	/* Only check the ELF magic number here and do proper validity check
	 * in the loader. Any check here that fails would send the erroneous
	 * ELF file to the image loader that does not care what it gets.
	 * (Most likely) causing behavior not intended by the user.
	 */
	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0)
		return -ENOEXEC;

	return 0;
}

const struct kexec_file_ops s390_kexec_elf_ops = {
	.probe = s390_elf_probe,
	.load = s390_elf_load,
};
