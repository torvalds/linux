/*
 * Copyright (c) 1995
 *	Ted Lemon (hereinafter referred to as the author)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* elf2ecoff.c

   This program converts an elf executable to an ECOFF executable.
   No symbol table is retained.   This is useful primarily in building
   net-bootable kernels for machines (e.g., DECstation and Alpha) which
   only support the ECOFF object file format. */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <elf.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdlib.h>

#include "ecoff.h"

/*
 * Some extra ELF definitions
 */
#define PT_MIPS_REGINFO 0x70000000	/* Register usage information */

/* -------------------------------------------------------------------- */

struct sect {
	unsigned long vaddr;
	unsigned long len;
};

int *symTypeTable;
int must_convert_endian = 0;
int format_bigendian = 0;

static void copy(int out, int in, off_t offset, off_t size)
{
	char ibuf[4096];
	int remaining, cur, count;

	/* Go to the start of the ELF symbol table... */
	if (lseek(in, offset, SEEK_SET) < 0) {
		perror("copy: lseek");
		exit(1);
	}

	remaining = size;
	while (remaining) {
		cur = remaining;
		if (cur > sizeof ibuf)
			cur = sizeof ibuf;
		remaining -= cur;
		if ((count = read(in, ibuf, cur)) != cur) {
			fprintf(stderr, "copy: read: %s\n",
				count ? strerror(errno) :
				"premature end of file");
			exit(1);
		}
		if ((count = write(out, ibuf, cur)) != cur) {
			perror("copy: write");
			exit(1);
		}
	}
}

/*
 * Combine two segments, which must be contiguous.   If pad is true, it's
 * okay for there to be padding between.
 */
static void combine(struct sect *base, struct sect *new, int pad)
{
	if (!base->len)
		*base = *new;
	else if (new->len) {
		if (base->vaddr + base->len != new->vaddr) {
			if (pad)
				base->len = new->vaddr - base->vaddr;
			else {
				fprintf(stderr,
					"Non-contiguous data can't be converted.\n");
				exit(1);
			}
		}
		base->len += new->len;
	}
}

static int phcmp(const void *v1, const void *v2)
{
	const Elf32_Phdr *h1 = v1;
	const Elf32_Phdr *h2 = v2;

	if (h1->p_vaddr > h2->p_vaddr)
		return 1;
	else if (h1->p_vaddr < h2->p_vaddr)
		return -1;
	else
		return 0;
}

static char *saveRead(int file, off_t offset, off_t len, char *name)
{
	char *tmp;
	int count;
	off_t off;
	if ((off = lseek(file, offset, SEEK_SET)) < 0) {
		fprintf(stderr, "%s: fseek: %s\n", name, strerror(errno));
		exit(1);
	}
	if (!(tmp = (char *) malloc(len))) {
		fprintf(stderr, "%s: Can't allocate %ld bytes.\n", name,
			len);
		exit(1);
	}
	count = read(file, tmp, len);
	if (count != len) {
		fprintf(stderr, "%s: read: %s.\n",
			name,
			count ? strerror(errno) : "End of file reached");
		exit(1);
	}
	return tmp;
}

#define swab16(x) \
	((unsigned short)( \
		(((unsigned short)(x) & (unsigned short)0x00ffU) << 8) | \
		(((unsigned short)(x) & (unsigned short)0xff00U) >> 8) ))

#define swab32(x) \
	((unsigned int)( \
		(((unsigned int)(x) & (unsigned int)0x000000ffUL) << 24) | \
		(((unsigned int)(x) & (unsigned int)0x0000ff00UL) <<  8) | \
		(((unsigned int)(x) & (unsigned int)0x00ff0000UL) >>  8) | \
		(((unsigned int)(x) & (unsigned int)0xff000000UL) >> 24) ))

static void convert_elf_hdr(Elf32_Ehdr * e)
{
	e->e_type = swab16(e->e_type);
	e->e_machine = swab16(e->e_machine);
	e->e_version = swab32(e->e_version);
	e->e_entry = swab32(e->e_entry);
	e->e_phoff = swab32(e->e_phoff);
	e->e_shoff = swab32(e->e_shoff);
	e->e_flags = swab32(e->e_flags);
	e->e_ehsize = swab16(e->e_ehsize);
	e->e_phentsize = swab16(e->e_phentsize);
	e->e_phnum = swab16(e->e_phnum);
	e->e_shentsize = swab16(e->e_shentsize);
	e->e_shnum = swab16(e->e_shnum);
	e->e_shstrndx = swab16(e->e_shstrndx);
}

static void convert_elf_phdrs(Elf32_Phdr * p, int num)
{
	int i;

	for (i = 0; i < num; i++, p++) {
		p->p_type = swab32(p->p_type);
		p->p_offset = swab32(p->p_offset);
		p->p_vaddr = swab32(p->p_vaddr);
		p->p_paddr = swab32(p->p_paddr);
		p->p_filesz = swab32(p->p_filesz);
		p->p_memsz = swab32(p->p_memsz);
		p->p_flags = swab32(p->p_flags);
		p->p_align = swab32(p->p_align);
	}

}

static void convert_elf_shdrs(Elf32_Shdr * s, int num)
{
	int i;

	for (i = 0; i < num; i++, s++) {
		s->sh_name = swab32(s->sh_name);
		s->sh_type = swab32(s->sh_type);
		s->sh_flags = swab32(s->sh_flags);
		s->sh_addr = swab32(s->sh_addr);
		s->sh_offset = swab32(s->sh_offset);
		s->sh_size = swab32(s->sh_size);
		s->sh_link = swab32(s->sh_link);
		s->sh_info = swab32(s->sh_info);
		s->sh_addralign = swab32(s->sh_addralign);
		s->sh_entsize = swab32(s->sh_entsize);
	}
}

static void convert_ecoff_filehdr(struct filehdr *f)
{
	f->f_magic = swab16(f->f_magic);
	f->f_nscns = swab16(f->f_nscns);
	f->f_timdat = swab32(f->f_timdat);
	f->f_symptr = swab32(f->f_symptr);
	f->f_nsyms = swab32(f->f_nsyms);
	f->f_opthdr = swab16(f->f_opthdr);
	f->f_flags = swab16(f->f_flags);
}

static void convert_ecoff_aouthdr(struct aouthdr *a)
{
	a->magic = swab16(a->magic);
	a->vstamp = swab16(a->vstamp);
	a->tsize = swab32(a->tsize);
	a->dsize = swab32(a->dsize);
	a->bsize = swab32(a->bsize);
	a->entry = swab32(a->entry);
	a->text_start = swab32(a->text_start);
	a->data_start = swab32(a->data_start);
	a->bss_start = swab32(a->bss_start);
	a->gprmask = swab32(a->gprmask);
	a->cprmask[0] = swab32(a->cprmask[0]);
	a->cprmask[1] = swab32(a->cprmask[1]);
	a->cprmask[2] = swab32(a->cprmask[2]);
	a->cprmask[3] = swab32(a->cprmask[3]);
	a->gp_value = swab32(a->gp_value);
}

static void convert_ecoff_esecs(struct scnhdr *s, int num)
{
	int i;

	for (i = 0; i < num; i++, s++) {
		s->s_paddr = swab32(s->s_paddr);
		s->s_vaddr = swab32(s->s_vaddr);
		s->s_size = swab32(s->s_size);
		s->s_scnptr = swab32(s->s_scnptr);
		s->s_relptr = swab32(s->s_relptr);
		s->s_lnnoptr = swab32(s->s_lnnoptr);
		s->s_nreloc = swab16(s->s_nreloc);
		s->s_nlnno = swab16(s->s_nlnno);
		s->s_flags = swab32(s->s_flags);
	}
}

int main(int argc, char *argv[])
{
	Elf32_Ehdr ex;
	Elf32_Phdr *ph;
	Elf32_Shdr *sh;
	char *shstrtab;
	int i, pad;
	struct sect text, data, bss;
	struct filehdr efh;
	struct aouthdr eah;
	struct scnhdr esecs[6];
	int infile, outfile;
	unsigned long cur_vma = ULONG_MAX;
	int addflag = 0;
	int nosecs;

	text.len = data.len = bss.len = 0;
	text.vaddr = data.vaddr = bss.vaddr = 0;

	/* Check args... */
	if (argc < 3 || argc > 4) {
	      usage:
		fprintf(stderr,
			"usage: elf2ecoff <elf executable> <ecoff executable> [-a]\n");
		exit(1);
	}
	if (argc == 4) {
		if (strcmp(argv[3], "-a"))
			goto usage;
		addflag = 1;
	}

	/* Try the input file... */
	if ((infile = open(argv[1], O_RDONLY)) < 0) {
		fprintf(stderr, "Can't open %s for read: %s\n",
			argv[1], strerror(errno));
		exit(1);
	}

	/* Read the header, which is at the beginning of the file... */
	i = read(infile, &ex, sizeof ex);
	if (i != sizeof ex) {
		fprintf(stderr, "ex: %s: %s.\n",
			argv[1],
			i ? strerror(errno) : "End of file reached");
		exit(1);
	}

	if (ex.e_ident[EI_DATA] == ELFDATA2MSB)
		format_bigendian = 1;

	if (ntohs(0xaa55) == 0xaa55) {
		if (!format_bigendian)
			must_convert_endian = 1;
	} else {
		if (format_bigendian)
			must_convert_endian = 1;
	}
	if (must_convert_endian)
		convert_elf_hdr(&ex);

	/* Read the program headers... */
	ph = (Elf32_Phdr *) saveRead(infile, ex.e_phoff,
				     ex.e_phnum * sizeof(Elf32_Phdr),
				     "ph");
	if (must_convert_endian)
		convert_elf_phdrs(ph, ex.e_phnum);
	/* Read the section headers... */
	sh = (Elf32_Shdr *) saveRead(infile, ex.e_shoff,
				     ex.e_shnum * sizeof(Elf32_Shdr),
				     "sh");
	if (must_convert_endian)
		convert_elf_shdrs(sh, ex.e_shnum);
	/* Read in the section string table. */
	shstrtab = saveRead(infile, sh[ex.e_shstrndx].sh_offset,
			    sh[ex.e_shstrndx].sh_size, "shstrtab");

	/* Figure out if we can cram the program header into an ECOFF
	   header...  Basically, we can't handle anything but loadable
	   segments, but we can ignore some kinds of segments.  We can't
	   handle holes in the address space.  Segments may be out of order,
	   so we sort them first. */

	qsort(ph, ex.e_phnum, sizeof(Elf32_Phdr), phcmp);

	for (i = 0; i < ex.e_phnum; i++) {
		/* Section types we can ignore... */
		if (ph[i].p_type == PT_NULL || ph[i].p_type == PT_NOTE ||
		    ph[i].p_type == PT_PHDR
		    || ph[i].p_type == PT_MIPS_REGINFO)
			continue;
		/* Section types we can't handle... */
		else if (ph[i].p_type != PT_LOAD) {
			fprintf(stderr,
				"Program header %d type %d can't be converted.\n",
				ex.e_phnum, ph[i].p_type);
			exit(1);
		}
		/* Writable (data) segment? */
		if (ph[i].p_flags & PF_W) {
			struct sect ndata, nbss;

			ndata.vaddr = ph[i].p_vaddr;
			ndata.len = ph[i].p_filesz;
			nbss.vaddr = ph[i].p_vaddr + ph[i].p_filesz;
			nbss.len = ph[i].p_memsz - ph[i].p_filesz;

			combine(&data, &ndata, 0);
			combine(&bss, &nbss, 1);
		} else {
			struct sect ntxt;

			ntxt.vaddr = ph[i].p_vaddr;
			ntxt.len = ph[i].p_filesz;

			combine(&text, &ntxt, 0);
		}
		/* Remember the lowest segment start address. */
		if (ph[i].p_vaddr < cur_vma)
			cur_vma = ph[i].p_vaddr;
	}

	/* Sections must be in order to be converted... */
	if (text.vaddr > data.vaddr || data.vaddr > bss.vaddr ||
	    text.vaddr + text.len > data.vaddr
	    || data.vaddr + data.len > bss.vaddr) {
		fprintf(stderr,
			"Sections ordering prevents a.out conversion.\n");
		exit(1);
	}

	/* If there's a data section but no text section, then the loader
	   combined everything into one section.   That needs to be the
	   text section, so just make the data section zero length following
	   text. */
	if (data.len && !text.len) {
		text = data;
		data.vaddr = text.vaddr + text.len;
		data.len = 0;
	}

	/* If there is a gap between text and data, we'll fill it when we copy
	   the data, so update the length of the text segment as represented in
	   a.out to reflect that, since a.out doesn't allow gaps in the program
	   address space. */
	if (text.vaddr + text.len < data.vaddr)
		text.len = data.vaddr - text.vaddr;

	/* We now have enough information to cons up an a.out header... */
	eah.magic = OMAGIC;
	eah.vstamp = 200;
	eah.tsize = text.len;
	eah.dsize = data.len;
	eah.bsize = bss.len;
	eah.entry = ex.e_entry;
	eah.text_start = text.vaddr;
	eah.data_start = data.vaddr;
	eah.bss_start = bss.vaddr;
	eah.gprmask = 0xf3fffffe;
	memset(&eah.cprmask, '\0', sizeof eah.cprmask);
	eah.gp_value = 0;	/* unused. */

	if (format_bigendian)
		efh.f_magic = MIPSEBMAGIC;
	else
		efh.f_magic = MIPSELMAGIC;
	if (addflag)
		nosecs = 6;
	else
		nosecs = 3;
	efh.f_nscns = nosecs;
	efh.f_timdat = 0;	/* bogus */
	efh.f_symptr = 0;
	efh.f_nsyms = 0;
	efh.f_opthdr = sizeof eah;
	efh.f_flags = 0x100f;	/* Stripped, not sharable. */

	memset(esecs, 0, sizeof esecs);
	strcpy(esecs[0].s_name, ".text");
	strcpy(esecs[1].s_name, ".data");
	strcpy(esecs[2].s_name, ".bss");
	if (addflag) {
		strcpy(esecs[3].s_name, ".rdata");
		strcpy(esecs[4].s_name, ".sdata");
		strcpy(esecs[5].s_name, ".sbss");
	}
	esecs[0].s_paddr = esecs[0].s_vaddr = eah.text_start;
	esecs[1].s_paddr = esecs[1].s_vaddr = eah.data_start;
	esecs[2].s_paddr = esecs[2].s_vaddr = eah.bss_start;
	if (addflag) {
		esecs[3].s_paddr = esecs[3].s_vaddr = 0;
		esecs[4].s_paddr = esecs[4].s_vaddr = 0;
		esecs[5].s_paddr = esecs[5].s_vaddr = 0;
	}
	esecs[0].s_size = eah.tsize;
	esecs[1].s_size = eah.dsize;
	esecs[2].s_size = eah.bsize;
	if (addflag) {
		esecs[3].s_size = 0;
		esecs[4].s_size = 0;
		esecs[5].s_size = 0;
	}
	esecs[0].s_scnptr = N_TXTOFF(efh, eah);
	esecs[1].s_scnptr = N_DATOFF(efh, eah);
#define ECOFF_SEGMENT_ALIGNMENT(a) 0x10
#define ECOFF_ROUND(s, a) (((s)+(a)-1)&~((a)-1))
	esecs[2].s_scnptr = esecs[1].s_scnptr +
	    ECOFF_ROUND(esecs[1].s_size, ECOFF_SEGMENT_ALIGNMENT(&eah));
	if (addflag) {
		esecs[3].s_scnptr = 0;
		esecs[4].s_scnptr = 0;
		esecs[5].s_scnptr = 0;
	}
	esecs[0].s_relptr = esecs[1].s_relptr = esecs[2].s_relptr = 0;
	esecs[0].s_lnnoptr = esecs[1].s_lnnoptr = esecs[2].s_lnnoptr = 0;
	esecs[0].s_nreloc = esecs[1].s_nreloc = esecs[2].s_nreloc = 0;
	esecs[0].s_nlnno = esecs[1].s_nlnno = esecs[2].s_nlnno = 0;
	if (addflag) {
		esecs[3].s_relptr = esecs[4].s_relptr
		    = esecs[5].s_relptr = 0;
		esecs[3].s_lnnoptr = esecs[4].s_lnnoptr
		    = esecs[5].s_lnnoptr = 0;
		esecs[3].s_nreloc = esecs[4].s_nreloc = esecs[5].s_nreloc =
		    0;
		esecs[3].s_nlnno = esecs[4].s_nlnno = esecs[5].s_nlnno = 0;
	}
	esecs[0].s_flags = 0x20;
	esecs[1].s_flags = 0x40;
	esecs[2].s_flags = 0x82;
	if (addflag) {
		esecs[3].s_flags = 0x100;
		esecs[4].s_flags = 0x200;
		esecs[5].s_flags = 0x400;
	}

	/* Make the output file... */
	if ((outfile = open(argv[2], O_WRONLY | O_CREAT, 0777)) < 0) {
		fprintf(stderr, "Unable to create %s: %s\n", argv[2],
			strerror(errno));
		exit(1);
	}

	if (must_convert_endian)
		convert_ecoff_filehdr(&efh);
	/* Write the headers... */
	i = write(outfile, &efh, sizeof efh);
	if (i != sizeof efh) {
		perror("efh: write");
		exit(1);

		for (i = 0; i < nosecs; i++) {
			printf
			    ("Section %d: %s phys %lx  size %lx  file offset %lx\n",
			     i, esecs[i].s_name, esecs[i].s_paddr,
			     esecs[i].s_size, esecs[i].s_scnptr);
		}
	}
	fprintf(stderr, "wrote %d byte file header.\n", i);

	if (must_convert_endian)
		convert_ecoff_aouthdr(&eah);
	i = write(outfile, &eah, sizeof eah);
	if (i != sizeof eah) {
		perror("eah: write");
		exit(1);
	}
	fprintf(stderr, "wrote %d byte a.out header.\n", i);

	if (must_convert_endian)
		convert_ecoff_esecs(&esecs[0], nosecs);
	i = write(outfile, &esecs, nosecs * sizeof(struct scnhdr));
	if (i != nosecs * sizeof(struct scnhdr)) {
		perror("esecs: write");
		exit(1);
	}
	fprintf(stderr, "wrote %d bytes of section headers.\n", i);

	pad = (sizeof(efh) + sizeof(eah) + nosecs * sizeof(struct scnhdr)) & 15;
	if (pad) {
		pad = 16 - pad;
		i = write(outfile, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0", pad);
		if (i < 0) {
			perror("ipad: write");
			exit(1);
		}
		fprintf(stderr, "wrote %d byte pad.\n", i);
	}

	/*
	 * Copy the loadable sections.   Zero-fill any gaps less than 64k;
	 * complain about any zero-filling, and die if we're asked to zero-fill
	 * more than 64k.
	 */
	for (i = 0; i < ex.e_phnum; i++) {
		/* Unprocessable sections were handled above, so just verify that
		   the section can be loaded before copying. */
		if (ph[i].p_type == PT_LOAD && ph[i].p_filesz) {
			if (cur_vma != ph[i].p_vaddr) {
				unsigned long gap =
				    ph[i].p_vaddr - cur_vma;
				char obuf[1024];
				if (gap > 65536) {
					fprintf(stderr,
						"Intersegment gap (%ld bytes) too large.\n",
						gap);
					exit(1);
				}
				fprintf(stderr,
					"Warning: %ld byte intersegment gap.\n",
					gap);
				memset(obuf, 0, sizeof obuf);
				while (gap) {
					int count =
					    write(outfile, obuf,
						  (gap >
						   sizeof obuf ? sizeof
						   obuf : gap));
					if (count < 0) {
						fprintf(stderr,
							"Error writing gap: %s\n",
							strerror(errno));
						exit(1);
					}
					gap -= count;
				}
			}
			fprintf(stderr, "writing %d bytes...\n",
				ph[i].p_filesz);
			copy(outfile, infile, ph[i].p_offset,
			     ph[i].p_filesz);
			cur_vma = ph[i].p_vaddr + ph[i].p_filesz;
		}
	}

	/*
	 * Write a page of padding for boot PROMS that read entire pages.
	 * Without this, they may attempt to read past the end of the
	 * data section, incur an error, and refuse to boot.
	 */
	{
		char obuf[4096];
		memset(obuf, 0, sizeof obuf);
		if (write(outfile, obuf, sizeof(obuf)) != sizeof(obuf)) {
			fprintf(stderr, "Error writing PROM padding: %s\n",
				strerror(errno));
			exit(1);
		}
	}

	/* Looks like we won... */
	exit(0);
}
