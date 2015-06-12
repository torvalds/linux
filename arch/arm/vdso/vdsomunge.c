/*
 * Copyright 2015 Mentor Graphics Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * vdsomunge - Host program which produces a shared object
 * architecturally specified to be usable by both soft- and hard-float
 * programs.
 *
 * The Procedure Call Standard for the ARM Architecture (ARM IHI
 * 0042E) says:
 *
 *	6.4.1 VFP and Base Standard Compatibility
 *
 *	Code compiled for the VFP calling standard is compatible with
 *	the base standard (and vice-versa) if no floating-point or
 *	containerized vector arguments or results are used.
 *
 * And ELF for the ARM Architecture (ARM IHI 0044E) (Table 4-2) says:
 *
 *	If both EF_ARM_ABI_FLOAT_XXXX bits are clear, conformance to the
 *	base procedure-call standard is implied.
 *
 * The VDSO is built with -msoft-float, as with the rest of the ARM
 * kernel, and uses no floating point arguments or results.  The build
 * process will produce a shared object that may or may not have the
 * EF_ARM_ABI_FLOAT_SOFT flag set (it seems to depend on the binutils
 * version; binutils starting with 2.24 appears to set it).  The
 * EF_ARM_ABI_FLOAT_HARD flag should definitely not be set, and this
 * program will error out if it is.
 *
 * If the soft-float flag is set, this program clears it.  That's all
 * it does.
 */

#define _GNU_SOURCE

#include <byteswap.h>
#include <elf.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define HOST_ORDER ELFDATA2LSB
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define HOST_ORDER ELFDATA2MSB
#endif

/* Some of the ELF constants we'd like to use were added to <elf.h>
 * relatively recently.
 */
#ifndef EF_ARM_EABI_VER5
#define EF_ARM_EABI_VER5 0x05000000
#endif

#ifndef EF_ARM_ABI_FLOAT_SOFT
#define EF_ARM_ABI_FLOAT_SOFT 0x200
#endif

#ifndef EF_ARM_ABI_FLOAT_HARD
#define EF_ARM_ABI_FLOAT_HARD 0x400
#endif

static const char *outfile;

static void cleanup(void)
{
	if (error_message_count > 0 && outfile != NULL)
		unlink(outfile);
}

static Elf32_Word read_elf_word(Elf32_Word word, bool swap)
{
	return swap ? bswap_32(word) : word;
}

static Elf32_Half read_elf_half(Elf32_Half half, bool swap)
{
	return swap ? bswap_16(half) : half;
}

static void write_elf_word(Elf32_Word val, Elf32_Word *dst, bool swap)
{
	*dst = swap ? bswap_32(val) : val;
}

int main(int argc, char **argv)
{
	const Elf32_Ehdr *inhdr;
	bool clear_soft_float;
	const char *infile;
	Elf32_Word e_flags;
	const void *inbuf;
	struct stat stat;
	void *outbuf;
	bool swap;
	int outfd;
	int infd;

	atexit(cleanup);

	if (argc != 3)
		error(EXIT_FAILURE, 0, "Usage: %s [infile] [outfile]", argv[0]);

	infile = argv[1];
	outfile = argv[2];

	infd = open(infile, O_RDONLY);
	if (infd < 0)
		error(EXIT_FAILURE, errno, "Cannot open %s", infile);

	if (fstat(infd, &stat) != 0)
		error(EXIT_FAILURE, errno, "Failed stat for %s", infile);

	inbuf = mmap(NULL, stat.st_size, PROT_READ, MAP_PRIVATE, infd, 0);
	if (inbuf == MAP_FAILED)
		error(EXIT_FAILURE, errno, "Failed to map %s", infile);

	close(infd);

	inhdr = inbuf;

	if (memcmp(&inhdr->e_ident, ELFMAG, SELFMAG) != 0)
		error(EXIT_FAILURE, 0, "Not an ELF file");

	if (inhdr->e_ident[EI_CLASS] != ELFCLASS32)
		error(EXIT_FAILURE, 0, "Unsupported ELF class");

	swap = inhdr->e_ident[EI_DATA] != HOST_ORDER;

	if (read_elf_half(inhdr->e_type, swap) != ET_DYN)
		error(EXIT_FAILURE, 0, "Not a shared object");

	if (read_elf_half(inhdr->e_machine, swap) != EM_ARM) {
		error(EXIT_FAILURE, 0, "Unsupported architecture %#x",
		      inhdr->e_machine);
	}

	e_flags = read_elf_word(inhdr->e_flags, swap);

	if (EF_ARM_EABI_VERSION(e_flags) != EF_ARM_EABI_VER5) {
		error(EXIT_FAILURE, 0, "Unsupported EABI version %#x",
		      EF_ARM_EABI_VERSION(e_flags));
	}

	if (e_flags & EF_ARM_ABI_FLOAT_HARD)
		error(EXIT_FAILURE, 0,
		      "Unexpected hard-float flag set in e_flags");

	clear_soft_float = !!(e_flags & EF_ARM_ABI_FLOAT_SOFT);

	outfd = open(outfile, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (outfd < 0)
		error(EXIT_FAILURE, errno, "Cannot open %s", outfile);

	if (ftruncate(outfd, stat.st_size) != 0)
		error(EXIT_FAILURE, errno, "Cannot truncate %s", outfile);

	outbuf = mmap(NULL, stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		      outfd, 0);
	if (outbuf == MAP_FAILED)
		error(EXIT_FAILURE, errno, "Failed to map %s", outfile);

	close(outfd);

	memcpy(outbuf, inbuf, stat.st_size);

	if (clear_soft_float) {
		Elf32_Ehdr *outhdr;

		outhdr = outbuf;
		e_flags &= ~EF_ARM_ABI_FLOAT_SOFT;
		write_elf_word(e_flags, &outhdr->e_flags, swap);
	}

	if (msync(outbuf, stat.st_size, MS_SYNC) != 0)
		error(EXIT_FAILURE, errno, "Failed to sync %s", outfile);

	return EXIT_SUCCESS;
}
