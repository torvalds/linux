// SPDX-License-Identifier: GPL-2.0
#include <byteswap.h>
#include <elf.h>
#include <endian.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef be32toh
/* If libc provides [bl]e{32,64}toh() then we'll use them */
#elif BYTE_ORDER == LITTLE_ENDIAN
# define be32toh(x)	bswap_32(x)
# define le32toh(x)	(x)
# define be64toh(x)	bswap_64(x)
# define le64toh(x)	(x)
#elif BYTE_ORDER == BIG_ENDIAN
# define be32toh(x)	(x)
# define le32toh(x)	bswap_32(x)
# define be64toh(x)	(x)
# define le64toh(x)	bswap_64(x)
#endif

__attribute__((noreturn))
static void die(const char *msg)
{
	fputs(msg, stderr);
	exit(EXIT_FAILURE);
}

int main(int argc, const char *argv[])
{
	uint64_t entry;
	size_t nread;
	FILE *file;
	union {
		Elf32_Ehdr ehdr32;
		Elf64_Ehdr ehdr64;
	} hdr;

	if (argc != 2)
		die("Usage: elf-entry <elf-file>\n");

	file = fopen(argv[1], "r");
	if (!file) {
		perror("Unable to open input file");
		return EXIT_FAILURE;
	}

	nread = fread(&hdr, 1, sizeof(hdr), file);
	if (nread != sizeof(hdr)) {
		perror("Unable to read input file");
		return EXIT_FAILURE;
	}

	if (memcmp(hdr.ehdr32.e_ident, ELFMAG, SELFMAG))
		die("Input is not an ELF\n");

	switch (hdr.ehdr32.e_ident[EI_CLASS]) {
	case ELFCLASS32:
		switch (hdr.ehdr32.e_ident[EI_DATA]) {
		case ELFDATA2LSB:
			entry = le32toh(hdr.ehdr32.e_entry);
			break;
		case ELFDATA2MSB:
			entry = be32toh(hdr.ehdr32.e_entry);
			break;
		default:
			die("Invalid ELF encoding\n");
		}

		/* Sign extend to form a canonical address */
		entry = (int64_t)(int32_t)entry;
		break;

	case ELFCLASS64:
		switch (hdr.ehdr32.e_ident[EI_DATA]) {
		case ELFDATA2LSB:
			entry = le64toh(hdr.ehdr64.e_entry);
			break;
		case ELFDATA2MSB:
			entry = be64toh(hdr.ehdr64.e_entry);
			break;
		default:
			die("Invalid ELF encoding\n");
		}
		break;

	default:
		die("Invalid ELF class\n");
	}

	printf("0x%016" PRIx64 "\n", entry);
	return EXIT_SUCCESS;
}
