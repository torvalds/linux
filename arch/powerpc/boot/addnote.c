// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Program to hack in a PT_NOTE program header entry in an ELF file.
 * This is needed for OF on RS/6000s to load an image correctly.
 * Note that OF needs a program header entry for the note, not an
 * ELF section.
 *
 * Copyright 2000 Paul Mackerras.
 *
 * Adapted for 64 bit little endian images by Andrew Tauferner.
 *
 * Usage: addnote zImage
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/* CHRP note section */
static const char arch[] = "PowerPC";

#define N_DESCR	6
unsigned int descr[N_DESCR] = {
	0xffffffff,		/* real-mode = true */
	0x02000000,		/* real-base, i.e. where we expect OF to be */
	0xffffffff,		/* real-size */
	0xffffffff,		/* virt-base */
	0xffffffff,		/* virt-size */
	0x4000,			/* load-base */
};

/* RPA note section */
static const char rpaname[] = "IBM,RPA-Client-Config";

/*
 * Note: setting ignore_my_client_config *should* mean that OF ignores
 * all the other fields, but there is a firmware bug which means that
 * it looks at the splpar field at least.  So these values need to be
 * reasonable.
 */
#define N_RPA_DESCR	8
unsigned int rpanote[N_RPA_DESCR] = {
	0,			/* lparaffinity */
	64,			/* min_rmo_size */
	0,			/* min_rmo_percent */
	40,			/* max_pft_size */
	1,			/* splpar */
	-1,			/* min_load */
	0,			/* new_mem_def */
	1,			/* ignore_my_client_config */
};

#define ROUNDUP(len)	(((len) + 3) & ~3)

unsigned char buf[1024];
#define ELFDATA2LSB     1
#define ELFDATA2MSB     2
static int e_data = ELFDATA2MSB;
#define ELFCLASS32      1
#define ELFCLASS64      2
static int e_class = ELFCLASS32;

#define GET_16BE(off)	((buf[off] << 8) + (buf[(off)+1]))
#define GET_32BE(off)	((GET_16BE(off) << 16U) + GET_16BE((off)+2U))
#define GET_64BE(off)	((((unsigned long long)GET_32BE(off)) << 32ULL) + \
			((unsigned long long)GET_32BE((off)+4ULL)))
#define PUT_16BE(off, v)(buf[off] = ((v) >> 8) & 0xff, \
			 buf[(off) + 1] = (v) & 0xff)
#define PUT_32BE(off, v)(PUT_16BE((off), (v) >> 16L), PUT_16BE((off) + 2, (v)))
#define PUT_64BE(off, v)((PUT_32BE((off), (v) >> 32L), \
			  PUT_32BE((off) + 4, (v))))

#define GET_16LE(off)	((buf[off]) + (buf[(off)+1] << 8))
#define GET_32LE(off)	(GET_16LE(off) + (GET_16LE((off)+2U) << 16U))
#define GET_64LE(off)	((unsigned long long)GET_32LE(off) + \
			(((unsigned long long)GET_32LE((off)+4ULL)) << 32ULL))
#define PUT_16LE(off, v) (buf[off] = (v) & 0xff, \
			  buf[(off) + 1] = ((v) >> 8) & 0xff)
#define PUT_32LE(off, v) (PUT_16LE((off), (v)), PUT_16LE((off) + 2, (v) >> 16L))
#define PUT_64LE(off, v) (PUT_32LE((off), (v)), PUT_32LE((off) + 4, (v) >> 32L))

#define GET_16(off)	(e_data == ELFDATA2MSB ? GET_16BE(off) : GET_16LE(off))
#define GET_32(off)	(e_data == ELFDATA2MSB ? GET_32BE(off) : GET_32LE(off))
#define GET_64(off)	(e_data == ELFDATA2MSB ? GET_64BE(off) : GET_64LE(off))
#define PUT_16(off, v)	(e_data == ELFDATA2MSB ? PUT_16BE(off, v) : \
			 PUT_16LE(off, v))
#define PUT_32(off, v)  (e_data == ELFDATA2MSB ? PUT_32BE(off, v) : \
			 PUT_32LE(off, v))
#define PUT_64(off, v)  (e_data == ELFDATA2MSB ? PUT_64BE(off, v) : \
			 PUT_64LE(off, v))

/* Structure of an ELF file */
#define E_IDENT		0	/* ELF header */
#define	E_PHOFF		(e_class == ELFCLASS32 ? 28 : 32)
#define E_PHENTSIZE	(e_class == ELFCLASS32 ? 42 : 54)
#define E_PHNUM		(e_class == ELFCLASS32 ? 44 : 56)
#define E_HSIZE		(e_class == ELFCLASS32 ? 52 : 64)

#define EI_MAGIC	0	/* offsets in E_IDENT area */
#define EI_CLASS	4
#define EI_DATA		5

#define PH_TYPE		0	/* ELF program header */
#define PH_OFFSET	(e_class == ELFCLASS32 ? 4 : 8)
#define PH_FILESZ	(e_class == ELFCLASS32 ? 16 : 32)
#define PH_HSIZE	(e_class == ELFCLASS32 ? 32 : 56)

#define PT_NOTE		4	/* Program header type = note */


unsigned char elf_magic[4] = { 0x7f, 'E', 'L', 'F' };

int
main(int ac, char **av)
{
	int fd, n, i;
	unsigned long ph, ps, np;
	long nnote, nnote2, ns;

	if (ac != 2) {
		fprintf(stderr, "Usage: %s elf-file\n", av[0]);
		exit(1);
	}
	fd = open(av[1], O_RDWR);
	if (fd < 0) {
		perror(av[1]);
		exit(1);
	}

	nnote = 12 + ROUNDUP(strlen(arch) + 1) + sizeof(descr);
	nnote2 = 12 + ROUNDUP(strlen(rpaname) + 1) + sizeof(rpanote);

	n = read(fd, buf, sizeof(buf));
	if (n < 0) {
		perror("read");
		exit(1);
	}

	if (memcmp(&buf[E_IDENT+EI_MAGIC], elf_magic, 4) != 0)
		goto notelf;
	e_class = buf[E_IDENT+EI_CLASS];
	if (e_class != ELFCLASS32 && e_class != ELFCLASS64)
		goto notelf;
	e_data = buf[E_IDENT+EI_DATA];
	if (e_data != ELFDATA2MSB && e_data != ELFDATA2LSB)
		goto notelf;
	if (n < E_HSIZE)
		goto notelf;

	ph = (e_class == ELFCLASS32 ? GET_32(E_PHOFF) : GET_64(E_PHOFF));
	ps = GET_16(E_PHENTSIZE);
	np = GET_16(E_PHNUM);
	if (ph < E_HSIZE || ps < PH_HSIZE || np < 1)
		goto notelf;
	if (ph + (np + 2) * ps + nnote + nnote2 > n)
		goto nospace;

	for (i = 0; i < np; ++i) {
		if (GET_32(ph + PH_TYPE) == PT_NOTE) {
			fprintf(stderr, "%s already has a note entry\n",
				av[1]);
			exit(0);
		}
		ph += ps;
	}

	/* XXX check that the area we want to use is all zeroes */
	for (i = 0; i < 2 * ps + nnote + nnote2; ++i)
		if (buf[ph + i] != 0)
			goto nospace;

	/* fill in the program header entry */
	ns = ph + 2 * ps;
	PUT_32(ph + PH_TYPE, PT_NOTE);
	if (e_class == ELFCLASS32)
		PUT_32(ph + PH_OFFSET, ns);
	else
		PUT_64(ph + PH_OFFSET, ns);

	if (e_class == ELFCLASS32)
		PUT_32(ph + PH_FILESZ, nnote);
	else
		PUT_64(ph + PH_FILESZ, nnote);

	/* fill in the note area we point to */
	/* XXX we should probably make this a proper section */
	PUT_32(ns, strlen(arch) + 1);
	PUT_32(ns + 4, N_DESCR * 4);
	PUT_32(ns + 8, 0x1275);
	strcpy((char *) &buf[ns + 12], arch);
	ns += 12 + strlen(arch) + 1;
	for (i = 0; i < N_DESCR; ++i, ns += 4)
		PUT_32BE(ns, descr[i]);

	/* fill in the second program header entry and the RPA note area */
	ph += ps;
	PUT_32(ph + PH_TYPE, PT_NOTE);
	if (e_class == ELFCLASS32)
		PUT_32(ph + PH_OFFSET, ns);
	else
		PUT_64(ph + PH_OFFSET, ns);

	if (e_class == ELFCLASS32)
		PUT_32(ph + PH_FILESZ, nnote);
	else
		PUT_64(ph + PH_FILESZ, nnote2);

	/* fill in the note area we point to */
	PUT_32(ns, strlen(rpaname) + 1);
	PUT_32(ns + 4, sizeof(rpanote));
	PUT_32(ns + 8, 0x12759999);
	strcpy((char *) &buf[ns + 12], rpaname);
	ns += 12 + ROUNDUP(strlen(rpaname) + 1);
	for (i = 0; i < N_RPA_DESCR; ++i, ns += 4)
		PUT_32BE(ns, rpanote[i]);

	/* Update the number of program headers */
	PUT_16(E_PHNUM, np + 2);

	/* write back */
	i = lseek(fd, (long) 0, SEEK_SET);
	if (i < 0) {
		perror("lseek");
		exit(1);
	}
	i = write(fd, buf, n);
	if (i < 0) {
		perror("write");
		exit(1);
	}
	if (i < n) {
		fprintf(stderr, "%s: write truncated\n", av[1]);
		exit(1);
	}

	exit(0);

 notelf:
	fprintf(stderr, "%s does not appear to be an ELF file\n", av[1]);
	exit(1);

 nospace:
	fprintf(stderr, "sorry, I can't find space in %s to put the note\n",
		av[1]);
	exit(1);
}
