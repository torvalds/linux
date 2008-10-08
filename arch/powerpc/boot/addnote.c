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
 * Usage: addnote zImage [note.elf]
 *
 * If note.elf is supplied, it is the name of an ELF file that contains
 * an RPA note to use instead of the built-in one.  Alternatively, the
 * note.elf file may be empty, in which case the built-in RPA note is
 * used (this is to simplify how this is invoked from the wrapper script).
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/* CHRP note section */
char arch[] = "PowerPC";

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
char rpaname[] = "IBM,RPA-Client-Config";

/*
 * Note: setting ignore_my_client_config *should* mean that OF ignores
 * all the other fields, but there is a firmware bug which means that
 * it looks at the splpar field at least.  So these values need to be
 * reasonable.
 */
#define N_RPA_DESCR	8
unsigned int rpanote[N_RPA_DESCR] = {
	1,			/* lparaffinity */
	128,			/* min_rmo_size */
	0,			/* min_rmo_percent */
	46,			/* max_pft_size */
	1,			/* splpar */
	-1,			/* min_load */
	1,			/* new_mem_def */
	0,			/* ignore_my_client_config */
};

#define ROUNDUP(len)	(((len) + 3) & ~3)

unsigned char buf[512];
unsigned char notebuf[512];

#define GET_16BE(b, off)	(((b)[off] << 8) + ((b)[(off)+1]))
#define GET_32BE(b, off)	((GET_16BE((b), (off)) << 16) + \
				 GET_16BE((b), (off)+2))

#define PUT_16BE(b, off, v)	((b)[off] = ((v) >> 8) & 0xff, \
				 (b)[(off) + 1] = (v) & 0xff)
#define PUT_32BE(b, off, v)	(PUT_16BE((b), (off), (v) >> 16), \
				 PUT_16BE((b), (off) + 2, (v)))

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

unsigned char *read_rpanote(const char *fname, int *nnp)
{
	int notefd, nr, i;
	int ph, ps, np;
	int note, notesize;

	notefd = open(fname, O_RDONLY);
	if (notefd < 0) {
		perror(fname);
		exit(1);
	}
	nr = read(notefd, notebuf, sizeof(notebuf));
	if (nr < 0) {
		perror("read note");
		exit(1);
	}
	if (nr == 0)		/* empty file */
		return NULL;
	if (nr < E_HSIZE ||
	    memcmp(&notebuf[E_IDENT+EI_MAGIC], elf_magic, 4) != 0 ||
	    notebuf[E_IDENT+EI_CLASS] != ELFCLASS32 ||
	    notebuf[E_IDENT+EI_DATA] != ELFDATA2MSB)
		goto notelf;
	close(notefd);

	/* now look for the RPA-note */
	ph = GET_32BE(notebuf, E_PHOFF);
	ps = GET_16BE(notebuf, E_PHENTSIZE);
	np = GET_16BE(notebuf, E_PHNUM);
	if (ph < E_HSIZE || ps < PH_HSIZE || np < 1)
		goto notelf;

	for (i = 0; i < np; ++i, ph += ps) {
		if (GET_32BE(notebuf, ph + PH_TYPE) != PT_NOTE)
			continue;
		note = GET_32BE(notebuf, ph + PH_OFFSET);
		notesize = GET_32BE(notebuf, ph + PH_FILESZ);
		if (notesize < 34 || note + notesize > nr)
			continue;
		if (GET_32BE(notebuf, note) != strlen(rpaname) + 1 ||
		    GET_32BE(notebuf, note + 8) != 0x12759999 ||
		    strcmp((char *)&notebuf[note + 12], rpaname) != 0)
			continue;
		/* looks like an RPA note, return it */
		*nnp = notesize;
		return &notebuf[note];
	}
	/* no RPA note found */
	return NULL;

 notelf:
	fprintf(stderr, "%s is not a big-endian 32-bit ELF image\n", fname);
	exit(1);
}

int
main(int ac, char **av)
{
	int fd, n, i;
	int ph, ps, np;
	int nnote, nnote2, ns;
	unsigned char *rpap;

	if (ac != 2 && ac != 3) {
		fprintf(stderr, "Usage: %s elf-file [rpanote.elf]\n", av[0]);
		exit(1);
	}
	fd = open(av[1], O_RDWR);
	if (fd < 0) {
		perror(av[1]);
		exit(1);
	}

	nnote = 12 + ROUNDUP(strlen(arch) + 1) + sizeof(descr);
	nnote2 = 12 + ROUNDUP(strlen(rpaname) + 1) + sizeof(rpanote);
	rpap = NULL;

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

	if (ac == 3)
		rpap = read_rpanote(av[2], &nnote2);

	ph = GET_32BE(buf, E_PHOFF);
	ps = GET_16BE(buf, E_PHENTSIZE);
	np = GET_16BE(buf, E_PHNUM);
	if (ph < E_HSIZE || ps < PH_HSIZE || np < 1)
		goto notelf;
	if (ph + (np + 2) * ps + nnote + nnote2 > n)
		goto nospace;

	for (i = 0; i < np; ++i) {
		if (GET_32BE(buf, ph + PH_TYPE) == PT_NOTE) {
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
	PUT_32BE(buf, ph + PH_TYPE, PT_NOTE);
	PUT_32BE(buf, ph + PH_OFFSET, ns);
	PUT_32BE(buf, ph + PH_FILESZ, nnote);

	/* fill in the note area we point to */
	/* XXX we should probably make this a proper section */
	PUT_32BE(buf, ns, strlen(arch) + 1);
	PUT_32BE(buf, ns + 4, N_DESCR * 4);
	PUT_32BE(buf, ns + 8, 0x1275);
	strcpy((char *) &buf[ns + 12], arch);
	ns += 12 + strlen(arch) + 1;
	for (i = 0; i < N_DESCR; ++i, ns += 4)
		PUT_32BE(buf, ns, descr[i]);

	/* fill in the second program header entry and the RPA note area */
	ph += ps;
	PUT_32BE(buf, ph + PH_TYPE, PT_NOTE);
	PUT_32BE(buf, ph + PH_OFFSET, ns);
	PUT_32BE(buf, ph + PH_FILESZ, nnote2);

	/* fill in the note area we point to */
	if (rpap) {
		/* RPA note supplied in file, just copy the whole thing over */
		memcpy(buf + ns, rpap, nnote2);
	} else {
		PUT_32BE(buf, ns, strlen(rpaname) + 1);
		PUT_32BE(buf, ns + 4, sizeof(rpanote));
		PUT_32BE(buf, ns + 8, 0x12759999);
		strcpy((char *) &buf[ns + 12], rpaname);
		ns += 12 + ROUNDUP(strlen(rpaname) + 1);
		for (i = 0; i < N_RPA_DESCR; ++i, ns += 4)
			PUT_32BE(buf, ns, rpanote[i]);
	}

	/* Update the number of program headers */
	PUT_16BE(buf, E_PHNUM, np + 2);

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
	fprintf(stderr, "%s does not appear to be an ELF file\n", av[1]);
	exit(1);

 nospace:
	fprintf(stderr, "sorry, I can't find space in %s to put the note\n",
		av[1]);
	exit(1);
}
