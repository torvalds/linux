// SPDX-License-Identifier: GPL-2.0
/*
 * arch/alpha/boot/tools/objstrip.c
 *
 * Strip the object file headers/trailers from an executable (ELF or ECOFF).
 *
 * Copyright (C) 1996 David Mosberger-Tang.
 */
/*
 * Converts an ECOFF or ELF object file into a bootable file.  The
 * object file must be a OMAGIC file (i.e., data and bss follow immediately
 * behind the text).  See DEC "Assembly Language Programmer's Guide"
 * documentation for details.  The SRM boot process is documented in
 * the Alpha AXP Architecture Reference Manual, Second Edition by
 * Richard L. Sites and Richard T. Witek.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/a.out.h>
#include <linux/coff.h>
#include <linux/param.h>
#ifdef __ELF__
# include <linux/elf.h>
# define elfhdr elf64_hdr
# define elf_phdr elf64_phdr
# define elf_check_arch(x) ((x)->e_machine == EM_ALPHA)
#endif

/* bootfile size must be multiple of BLOCK_SIZE: */
#define BLOCK_SIZE	512

const char * prog_name;


static void
usage (void)
{
    fprintf(stderr,
	    "usage: %s [-v] -p file primary\n"
	    "       %s [-vb] file [secondary]\n", prog_name, prog_name);
    exit(1);
}


int
main (int argc, char *argv[])
{
    size_t nwritten, tocopy, n, mem_size, fil_size, pad = 0;
    int fd, ofd, i, j, verbose = 0, primary = 0;
    char buf[8192], *inname;
    struct exec * aout;		/* includes file & aout header */
    long offset;
#ifdef __ELF__
    struct elfhdr *elf;
    struct elf_phdr *elf_phdr;	/* program header */
    unsigned long long e_entry;
#endif

    prog_name = argv[0];

    for (i = 1; i < argc && argv[i][0] == '-'; ++i) {
	for (j = 1; argv[i][j]; ++j) {
	    switch (argv[i][j]) {
	      case 'v':
		  verbose = ~verbose;
		  break;

	      case 'b':
		  pad = BLOCK_SIZE;
		  break;

	      case 'p':
		  primary = 1;		/* make primary bootblock */
		  break;
	    }
	}
    }

    if (i >= argc) {
	usage();
    }
    inname = argv[i++];

    fd = open(inname, O_RDONLY);
    if (fd == -1) {
	perror("open");
	exit(1);
    }

    ofd = 1;
    if (i < argc) {
	ofd = open(argv[i++], O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (ofd == -1) {
	    perror("open");
	    exit(1);
	}
    }

    if (primary) {
	/* generate bootblock for primary loader */
	
	unsigned long bb[64], sum = 0;
	struct stat st;
	off_t size;
	int i;

	if (ofd == 1) {
	    usage();
	}

	if (fstat(fd, &st) == -1) {
	    perror("fstat");
	    exit(1);
	}

	size = (st.st_size + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
	memset(bb, 0, sizeof(bb));
	strcpy((char *) bb, "Linux SRM bootblock");
	bb[60] = size / BLOCK_SIZE;	/* count */
	bb[61] = 1;			/* starting sector # */
	bb[62] = 0;			/* flags---must be 0 */
	for (i = 0; i < 63; ++i) {
	    sum += bb[i];
	}
	bb[63] = sum;
	if (write(ofd, bb, sizeof(bb)) != sizeof(bb)) {
	    perror("boot-block write");
	    exit(1);
	}
	printf("%lu\n", size);
	return 0;
    }

    /* read and inspect exec header: */

    if (read(fd, buf, sizeof(buf)) < 0) {
	perror("read");
	exit(1);
    }

#ifdef __ELF__
    elf = (struct elfhdr *) buf;

    if (memcmp(&elf->e_ident[EI_MAG0], ELFMAG, SELFMAG) == 0) {
	if (elf->e_type != ET_EXEC) {
	    fprintf(stderr, "%s: %s is not an ELF executable\n",
		    prog_name, inname);
	    exit(1);
	}
	if (!elf_check_arch(elf)) {
	    fprintf(stderr, "%s: is not for this processor (e_machine=%d)\n",
		    prog_name, elf->e_machine);
	    exit(1);
	}
	if (elf->e_phnum != 1) {
	    fprintf(stderr,
		    "%s: %d program headers (forgot to link with -N?)\n",
		    prog_name, elf->e_phnum);
	}

	e_entry = elf->e_entry;

	lseek(fd, elf->e_phoff, SEEK_SET);
	if (read(fd, buf, sizeof(*elf_phdr)) != sizeof(*elf_phdr)) {
	    perror("read");
	    exit(1);
	}

	elf_phdr = (struct elf_phdr *) buf;
	offset	 = elf_phdr->p_offset;
	mem_size = elf_phdr->p_memsz;
	fil_size = elf_phdr->p_filesz;

	/* work around ELF bug: */
	if (elf_phdr->p_vaddr < e_entry) {
	    unsigned long delta = e_entry - elf_phdr->p_vaddr;
	    offset   += delta;
	    mem_size -= delta;
	    fil_size -= delta;
	    elf_phdr->p_vaddr += delta;
	}

	if (verbose) {
	    fprintf(stderr, "%s: extracting %#016lx-%#016lx (at %lx)\n",
		    prog_name, (long) elf_phdr->p_vaddr,
		    elf_phdr->p_vaddr + fil_size, offset);
	}
    } else
#endif
    {
	aout = (struct exec *) buf;

	if (!(aout->fh.f_flags & COFF_F_EXEC)) {
	    fprintf(stderr, "%s: %s is not in executable format\n",
		    prog_name, inname);
	    exit(1);
	}

	if (aout->fh.f_opthdr != sizeof(aout->ah)) {
	    fprintf(stderr, "%s: %s has unexpected optional header size\n",
		    prog_name, inname);
	    exit(1);
	}

	if (N_MAGIC(*aout) != OMAGIC) {
	    fprintf(stderr, "%s: %s is not an OMAGIC file\n",
		    prog_name, inname);
	    exit(1);
	}
	offset = N_TXTOFF(*aout);
	fil_size = aout->ah.tsize + aout->ah.dsize;
	mem_size = fil_size + aout->ah.bsize;

	if (verbose) {
	    fprintf(stderr, "%s: extracting %#016lx-%#016lx (at %lx)\n",
		    prog_name, aout->ah.text_start,
		    aout->ah.text_start + fil_size, offset);
	}
    }

    if (lseek(fd, offset, SEEK_SET) != offset) {
	perror("lseek");
	exit(1);
    }

    if (verbose) {
	fprintf(stderr, "%s: copying %lu byte from %s\n",
		prog_name, (unsigned long) fil_size, inname);
    }

    tocopy = fil_size;
    while (tocopy > 0) {
	n = tocopy;
	if (n > sizeof(buf)) {
	    n = sizeof(buf);
	}
	tocopy -= n;
	if ((size_t) read(fd, buf, n) != n) {
	    perror("read");
	    exit(1);
	}
	do {
	    nwritten = write(ofd, buf, n);
	    if ((ssize_t) nwritten == -1) {
		perror("write");
		exit(1);
	    }
	    n -= nwritten;
	} while (n > 0);
    }

    if (pad) {
	mem_size = ((mem_size + pad - 1) / pad) * pad;
    }

    tocopy = mem_size - fil_size;
    if (tocopy > 0) {
	fprintf(stderr,
		"%s: zero-filling bss and aligning to %lu with %lu bytes\n",
		prog_name, pad, (unsigned long) tocopy);

	memset(buf, 0x00, sizeof(buf));
	do {
	    n = tocopy;
	    if (n > sizeof(buf)) {
		n = sizeof(buf);
	    }
	    nwritten = write(ofd, buf, n);
	    if ((ssize_t) nwritten == -1) {
		perror("write");
		exit(1);
	    }
	    tocopy -= nwritten;
	} while (tocopy > 0);
    }
    return 0;
}
