/*
 * Program to hack in a PT_NOTE program header entry in an ELF file.
 * This is needed for OF on RS/6000s to load an image correctly.
 * Note that OF needs a program header entry for the note, not an
 * ELF section.
 *
 * Copyright 2000 Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Usage: addnote zImage
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

char arch[] = "PowerPC";

#define N_DESCR	6
unsigned int descr[N_DESCR] = {
#if 1
	/* values for IBM RS/6000 machines */
	0xffffffff,		/* real-mode = true */
	0x00c00000,		/* real-base, i.e. where we expect OF to be */
	0xffffffff,		/* real-size */
	0xffffffff,		/* virt-base */
	0xffffffff,		/* virt-size */
	0x4000,			/* load-base */
#else
	/* values for longtrail CHRP */
	0,			/* real-mode = false */
	0xffffffff,		/* real-base */
	0xffffffff,		/* real-size */
	0xffffffff,		/* virt-base */
	0xffffffff,		/* virt-size */
	0x00600000,		/* load-base */
#endif
};

unsigned char buf[512];

#define GET_16BE(off)	((buf[off] << 8) + (buf[(off)+1]))
#define GET_32BE(off)	((GET_16BE(off) << 16) + GET_16BE((off)+2))

#define PUT_16BE(off, v)	(buf[off] = ((v) >> 8) & 0xff, \
				 buf[(off) + 1] = (v) & 0xff)
#define PUT_32BE(off, v)	(PUT_16BE((off), (v) >> 16), \
				 PUT_16BE((off) + 2, (v)))

/* Structure of an ELF file */
#define E_IDENT		0	/* ELF header */
#define	E_PHOFF		28
#define E_PHENTSIZE	42
#define E_PHNUM		44
#define E_HSIZE		52	/* size of ELF header */

#define EI_MAGIC	0	/* offsets in E_IDENT area */
#define EI_CLASS	4
#define EI_DATA		5

#define PH_TYPE		0	/* ELF program header */
#define PH_OFFSET	4
#define PH_FILESZ	16
#define PH_HSIZE	32	/* size of program header */

#define PT_NOTE		4	/* Program header type = note */

#define ELFCLASS32	1
#define ELFDATA2MSB	2

unsigned char elf_magic[4] = { 0x7f, 'E', 'L', 'F' };

int main(int ac, char **av)
{
	int fd, n, i;
	int ph, ps, np;
	int nnote, ns;

	if (ac != 2) {
		fprintf(stderr, "Usage: %s elf-file\n", av[0]);
		exit(1);
	}
	fd = open(av[1], O_RDWR);
	if (fd < 0) {
		perror(av[1]);
		exit(1);
	}

	nnote = strlen(arch) + 1 + (N_DESCR + 3) * 4;

	n = read(fd, buf, sizeof(buf));
	if (n < 0) {
		perror("read");
		exit(1);
	}

	if (n < E_HSIZE || memcmp(&buf[E_IDENT+EI_MAGIC], elf_magic, 4) != 0)
		goto notelf;

	if (buf[E_IDENT+EI_CLASS] != ELFCLASS32
	    || buf[E_IDENT+EI_DATA] != ELFDATA2MSB) {
		fprintf(stderr, "%s is not a big-endian 32-bit ELF image\n",
			av[1]);
		exit(1);
	}

	ph = GET_32BE(E_PHOFF);
	ps = GET_16BE(E_PHENTSIZE);
	np = GET_16BE(E_PHNUM);
	if (ph < E_HSIZE || ps < PH_HSIZE || np < 1)
		goto notelf;
	if (ph + (np + 1) * ps + nnote > n)
		goto nospace;

	for (i = 0; i < np; ++i) {
		if (GET_32BE(ph + PH_TYPE) == PT_NOTE) {
			fprintf(stderr, "%s already has a note entry\n",
				av[1]);
			exit(0);
		}
		ph += ps;
	}

	/* XXX check that the area we want to use is all zeroes */
	for (i = 0; i < ps + nnote; ++i)
		if (buf[ph + i] != 0)
			goto nospace;

	/* fill in the program header entry */
	ns = ph + ps;
	PUT_32BE(ph + PH_TYPE, PT_NOTE);
	PUT_32BE(ph + PH_OFFSET, ns);
	PUT_32BE(ph + PH_FILESZ, nnote);

	/* fill in the note area we point to */
	/* XXX we should probably make this a proper section */
	PUT_32BE(ns, strlen(arch) + 1);
	PUT_32BE(ns + 4, N_DESCR * 4);
	PUT_32BE(ns + 8, 0x1275);
	strcpy(&buf[ns + 12], arch);
	ns += 12 + strlen(arch) + 1;
	for (i = 0; i < N_DESCR; ++i)
		PUT_32BE(ns + i * 4, descr[i]);

	/* Update the number of program headers */
	PUT_16BE(E_PHNUM, np + 1);

	/* write back */
	lseek(fd, (long) 0, SEEK_SET);
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
	fprintf(stderr, "%s does not appear to be an ELF file\n", av[0]);
	exit(1);

 nospace:
	fprintf(stderr, "sorry, I can't find space in %s to put the note\n",
		av[0]);
	exit(1);
}
