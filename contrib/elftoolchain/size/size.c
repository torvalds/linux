/*-
 * Copyright (c) 2007 S.Sam Arun Raj
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <gelf.h>
#include <getopt.h>
#include <libelftc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "_elftc.h"

ELFTC_VCSID("$Id: size.c 3458 2016-05-09 15:01:25Z emaste $");

#define	BUF_SIZE			1024
#define	ELF_ALIGN(val,x) (((val)+(x)-1) & ~((x)-1))
#define	SIZE_VERSION_STRING		"size 1.0"

enum return_code {
	RETURN_OK,
	RETURN_NOINPUT,
	RETURN_DATAERR,
	RETURN_USAGE
};

enum output_style {
	STYLE_BERKELEY,
	STYLE_SYSV
};

enum radix_style {
	RADIX_OCTAL,
	RADIX_DECIMAL,
	RADIX_HEX
};

static uint64_t bss_size, data_size, text_size, total_size;
static uint64_t bss_size_total, data_size_total, text_size_total;
static int show_totals;
static int size_option;
static enum radix_style radix = RADIX_DECIMAL;
static enum output_style style = STYLE_BERKELEY;
static const char *default_args[2] = { "a.out", NULL };

static struct {
	int row;
	int col;
	int *width;
	char ***tbl;
} *tb;

enum {
	OPT_FORMAT,
	OPT_RADIX
};

static struct option size_longopts[] = {
	{ "format",	required_argument, &size_option, OPT_FORMAT },
	{ "help",	no_argument,	NULL,	'h' },
	{ "radix",	required_argument, &size_option, OPT_RADIX },
	{ "totals",	no_argument,	NULL,	't' },
	{ "version",	no_argument,	NULL,	'V' },
	{ NULL, 0, NULL, 0 }  
};

static void	berkeley_calc(GElf_Shdr *);
static void	berkeley_footer(const char *, const char *, const char *);
static void	berkeley_header(void);
static void	berkeley_totals(void);
static int	handle_core(char const *, Elf *elf, GElf_Ehdr *);
static void	handle_core_note(Elf *, GElf_Ehdr *, GElf_Phdr *, char **);
static int	handle_elf(char const *);
static void	handle_phdr(Elf *, GElf_Ehdr *, GElf_Phdr *, uint32_t,
		    const char *);
static void	show_version(void);
static void	sysv_header(const char *, Elf_Arhdr *);
static void	sysv_footer(void);
static void	sysv_calc(Elf *, GElf_Ehdr *, GElf_Shdr *);
static void	usage(void);
static void	tbl_new(int);
static void	tbl_print(const char *, int);
static void	tbl_print_num(uint64_t, enum radix_style, int);
static void	tbl_append(void);
static void	tbl_flush(void);

/*
 * size utility using elf(3) and gelf(3) API to list section sizes and
 * total in elf files. Supports only elf files (core dumps in elf
 * included) that can be opened by libelf, other formats are not supported.
 */
int
main(int argc, char **argv)
{
	int ch, r, rc;
	const char **files, *fn;

	rc = RETURN_OK;

	if (elf_version(EV_CURRENT) == EV_NONE)
		errx(EXIT_FAILURE, "ELF library initialization failed: %s",
		    elf_errmsg(-1));

	while ((ch = getopt_long(argc, argv, "ABVdhotx", size_longopts,
	    NULL)) != -1)
		switch((char)ch) {
		case 'A':
			style = STYLE_SYSV;
			break;
		case 'B':
			style = STYLE_BERKELEY;
			break;
		case 'V':
			show_version();
			break;
		case 'd':
			radix = RADIX_DECIMAL;
			break;
		case 'o':
			radix = RADIX_OCTAL;
			break;
		case 't':
			show_totals = 1;
			break;
		case 'x':
			radix = RADIX_HEX;
			break;
		case 0:
			switch (size_option) {
			case OPT_FORMAT:
				if (*optarg == 's' || *optarg == 'S')
					style = STYLE_SYSV;
				else if (*optarg == 'b' || *optarg == 'B')
					style = STYLE_BERKELEY;
				else {
					warnx("unrecognized format \"%s\".",
					      optarg);
					usage();
				}
				break;
			case OPT_RADIX:
				r = strtol(optarg, NULL, 10);
				if (r == 8)
					radix = RADIX_OCTAL;
				else if (r == 10)
					radix = RADIX_DECIMAL;
				else if (r == 16)
					radix = RADIX_HEX;
				else {
					warnx("unsupported radix \"%s\".",
					      optarg);
					usage();
				}
				break;
			default:
				err(EXIT_FAILURE, "Error in option handling.");
				/*NOTREACHED*/
			}
			break;
		case 'h':
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	argc -= optind;
	argv += optind;

	files = (argc == 0) ? default_args : (void *) argv;

	while ((fn = *files) != NULL) {
		rc = handle_elf(fn);
		if (rc != RETURN_OK)
			warnx(rc == RETURN_NOINPUT ?
			      "'%s': No such file" :
			      "%s: File format not recognized", fn);
		files++;
	}
	if (style == STYLE_BERKELEY) {
		if (show_totals)
			berkeley_totals();
		tbl_flush();
	}
        return (rc);
}

static Elf_Data *
xlatetom(Elf *elf, GElf_Ehdr *elfhdr, void *_src, void *_dst,
    Elf_Type type, size_t size)
{
	Elf_Data src, dst;

	src.d_buf = _src;
	src.d_type = type;
	src.d_version = elfhdr->e_version;
	src.d_size = size;
	dst.d_buf = _dst;
	dst.d_version = elfhdr->e_version;
	dst.d_size = size;
	return (gelf_xlatetom(elf, &dst, &src, elfhdr->e_ident[EI_DATA]));
}

#define NOTE_OFFSET_32(nhdr, namesz, offset) 			\
	((char *)nhdr + sizeof(Elf32_Nhdr) +			\
	    ELF_ALIGN((int32_t)namesz, 4) + offset)

#define NOTE_OFFSET_64(nhdr, namesz, offset) 			\
	((char *)nhdr + sizeof(Elf32_Nhdr) +			\
	    ELF_ALIGN((int32_t)namesz, 8) + offset)

#define PID32(nhdr, namesz, offset) 				\
	(pid_t)*((int *)((uintptr_t)NOTE_OFFSET_32(nhdr,	\
	    namesz, offset)));

#define PID64(nhdr, namesz, offset) 				\
	(pid_t)*((int *)((uintptr_t)NOTE_OFFSET_64(nhdr,	\
	    namesz, offset)));

#define NEXT_NOTE(elfhdr, descsz, namesz, offset) do {		\
	if (elfhdr->e_ident[EI_CLASS] == ELFCLASS32) { 		\
		offset += ELF_ALIGN((int32_t)descsz, 4) +	\
		    sizeof(Elf32_Nhdr) + 			\
			ELF_ALIGN((int32_t)namesz, 4); 		\
	} else {						\
		offset += ELF_ALIGN((int32_t)descsz, 8) + 	\
		    sizeof(Elf32_Nhdr) + 			\
		        ELF_ALIGN((int32_t)namesz, 8); 		\
	}							\
} while (0)

/*
 * Parse individual note entries inside a PT_NOTE segment.
 */
static void
handle_core_note(Elf *elf, GElf_Ehdr *elfhdr, GElf_Phdr *phdr,
    char **cmd_line)
{
	size_t max_size, segment_end;
	uint64_t raw_size;
	GElf_Off offset;
	static pid_t pid;
	uintptr_t ver;
	Elf32_Nhdr *nhdr, nhdr_l;
	static int reg_pseudo = 0, reg2_pseudo = 0 /*, regxfp_pseudo = 0*/;
	char buf[BUF_SIZE], *data, *name;

 	if (elf == NULL || elfhdr == NULL || phdr == NULL)
		return;

	data = elf_rawfile(elf, &max_size);
	offset = phdr->p_offset;
	if (offset >= max_size || phdr->p_filesz > max_size - offset) {
		warnx("invalid PHDR offset");
		return;
	}
	segment_end = phdr->p_offset + phdr->p_filesz;

	while (data != NULL && offset + sizeof(Elf32_Nhdr) < segment_end) {
		nhdr = (Elf32_Nhdr *)(uintptr_t)((char*)data + offset);
		memset(&nhdr_l, 0, sizeof(Elf32_Nhdr));
		if (!xlatetom(elf, elfhdr, &nhdr->n_type, &nhdr_l.n_type,
			ELF_T_WORD, sizeof(Elf32_Word)) ||
		    !xlatetom(elf, elfhdr, &nhdr->n_descsz, &nhdr_l.n_descsz,
			ELF_T_WORD, sizeof(Elf32_Word)) ||
		    !xlatetom(elf, elfhdr, &nhdr->n_namesz, &nhdr_l.n_namesz,
			ELF_T_WORD, sizeof(Elf32_Word)))
			break;

		if (offset + sizeof(Elf32_Nhdr) +
		    ELF_ALIGN(nhdr_l.n_namesz, 4) +
		    ELF_ALIGN(nhdr_l.n_descsz, 4) >= segment_end) {
			warnx("invalid note header");
			return;
		}

		name = (char *)((char *)nhdr + sizeof(Elf32_Nhdr));
		switch (nhdr_l.n_type) {
		case NT_PRSTATUS: {
			raw_size = 0;
			if (elfhdr->e_ident[EI_OSABI] == ELFOSABI_FREEBSD &&
			    nhdr_l.n_namesz == 0x8 &&
			    !strcmp(name,"FreeBSD")) {
				if (elfhdr->e_ident[EI_CLASS] == ELFCLASS32) {
					raw_size = (uint64_t)*((uint32_t *)
					    (uintptr_t)(name +
						ELF_ALIGN((int32_t)
						nhdr_l.n_namesz, 4) + 8));
					ver = (uintptr_t)NOTE_OFFSET_32(nhdr,
					    nhdr_l.n_namesz,0);
					if (*((int *)ver) == 1)
						pid = PID32(nhdr,
						    nhdr_l.n_namesz, 24);
				} else {
					raw_size = *((uint64_t *)(uintptr_t)
					    (name + ELF_ALIGN((int32_t)
						nhdr_l.n_namesz, 8) + 16));
					ver = (uintptr_t)NOTE_OFFSET_64(nhdr,
					    nhdr_l.n_namesz,0);
					if (*((int *)ver) == 1)
						pid = PID64(nhdr,
						    nhdr_l.n_namesz, 40);
				}
				xlatetom(elf, elfhdr, &raw_size, &raw_size,
				    ELF_T_WORD, sizeof(uint64_t));
				xlatetom(elf, elfhdr, &pid, &pid, ELF_T_WORD,
				    sizeof(pid_t));
			}

			if (raw_size != 0 && style == STYLE_SYSV) {
				(void) snprintf(buf, BUF_SIZE, "%s/%d",
				    ".reg", pid);
				tbl_append();
				tbl_print(buf, 0);
				tbl_print_num(raw_size, radix, 1);
				tbl_print_num(0, radix, 2);
				if (!reg_pseudo) {
					tbl_append();
					tbl_print(".reg", 0);
					tbl_print_num(raw_size, radix, 1);
					tbl_print_num(0, radix, 2);
					reg_pseudo = 1;
					text_size_total += raw_size;
				}
				text_size_total += raw_size;
			}
		}
		break;
		case NT_FPREGSET:	/* same as NT_PRFPREG */
			if (style == STYLE_SYSV) {
				(void) snprintf(buf, BUF_SIZE,
				    "%s/%d", ".reg2", pid);
				tbl_append();
				tbl_print(buf, 0);
				tbl_print_num(nhdr_l.n_descsz, radix, 1);
				tbl_print_num(0, radix, 2);
				if (!reg2_pseudo) {
					tbl_append();
					tbl_print(".reg2", 0);
					tbl_print_num(nhdr_l.n_descsz, radix,
					    1);
					tbl_print_num(0, radix, 2);
					reg2_pseudo = 1;
					text_size_total += nhdr_l.n_descsz;
				}
				text_size_total += nhdr_l.n_descsz;
			}
			break;
#if 0
		case NT_AUXV:
			if (style == STYLE_SYSV) {
				tbl_append();
				tbl_print(".auxv", 0);
				tbl_print_num(nhdr_l.n_descsz, radix, 1);
				tbl_print_num(0, radix, 2);
				text_size_total += nhdr_l.n_descsz;
			}
			break;
		case NT_PRXFPREG:
			if (style == STYLE_SYSV) {
				(void) snprintf(buf, BUF_SIZE, "%s/%d",
				    ".reg-xfp", pid);
				tbl_append();
				tbl_print(buf, 0);
				tbl_print_num(nhdr_l.n_descsz, radix, 1);
				tbl_print_num(0, radix, 2);
				if (!regxfp_pseudo) {
					tbl_append();
					tbl_print(".reg-xfp", 0);
					tbl_print_num(nhdr_l.n_descsz, radix,
					    1);
					tbl_print_num(0, radix, 2);
					regxfp_pseudo = 1;
					text_size_total += nhdr_l.n_descsz;
				}
				text_size_total += nhdr_l.n_descsz;
			}
			break;
		case NT_PSINFO:
#endif
		case NT_PRPSINFO: {
			/* FreeBSD 64-bit */
			if (nhdr_l.n_descsz == 0x78 &&
				!strcmp(name,"FreeBSD")) {
				*cmd_line = strdup(NOTE_OFFSET_64(nhdr,
				    nhdr_l.n_namesz, 33));
			/* FreeBSD 32-bit */
			} else if (nhdr_l.n_descsz == 0x6c &&
				!strcmp(name,"FreeBSD")) {
				*cmd_line = strdup(NOTE_OFFSET_32(nhdr,
				    nhdr_l.n_namesz, 25));
			}
			/* Strip any trailing spaces */
			if (*cmd_line != NULL) {
				char *s;

				s = *cmd_line + strlen(*cmd_line);
				while (s > *cmd_line) {
					if (*(s-1) != 0x20) break;
					s--;
				}
				*s = 0;
			}
			break;
		}
#if 0
		case NT_PSTATUS:
		case NT_LWPSTATUS:
#endif
		default:
			break;
		}
		NEXT_NOTE(elfhdr, nhdr_l.n_descsz, nhdr_l.n_namesz, offset);
	}
}

/*
 * Handles program headers except for PT_NOTE, when sysv output style is
 * chosen, prints out the segment name and length. For berkely output
 * style only PT_LOAD segments are handled, and text,
 * data, bss size is calculated for them.
 */
static void
handle_phdr(Elf *elf, GElf_Ehdr *elfhdr, GElf_Phdr *phdr,
    uint32_t idx, const char *name)
{
	uint64_t addr, size;
	int split;
	char buf[BUF_SIZE];

	if (elf == NULL || elfhdr == NULL || phdr == NULL)
		return;

	split = (phdr->p_memsz > 0) && 	(phdr->p_filesz > 0) &&
	    (phdr->p_memsz > phdr->p_filesz);

	if (style == STYLE_SYSV) {
		(void) snprintf(buf, BUF_SIZE,
		    "%s%d%s", name, idx, (split ? "a" : ""));
		tbl_append();
		tbl_print(buf, 0);
		tbl_print_num(phdr->p_filesz, radix, 1);
		tbl_print_num(phdr->p_vaddr, radix, 2);
		text_size_total += phdr->p_filesz;
		if (split) {
			size = phdr->p_memsz - phdr->p_filesz;
			addr = phdr->p_vaddr + phdr->p_filesz;
			(void) snprintf(buf, BUF_SIZE, "%s%d%s", name,
			    idx, "b");
			text_size_total += phdr->p_memsz - phdr->p_filesz;
			tbl_append();
			tbl_print(buf, 0);
			tbl_print_num(size, radix, 1);
			tbl_print_num(addr, radix, 2);
		}
	} else {
		if (phdr->p_type != PT_LOAD)
			return;
		if ((phdr->p_flags & PF_W) && !(phdr->p_flags & PF_X)) {
			data_size += phdr->p_filesz;
			if (split)
				data_size += phdr->p_memsz - phdr->p_filesz;
		} else {
			text_size += phdr->p_filesz;
			if (split)
				text_size += phdr->p_memsz - phdr->p_filesz;
		}
	}
}

/*
 * Given a core dump file, this function maps program headers to segments.
 */
static int
handle_core(char const *name, Elf *elf, GElf_Ehdr *elfhdr)
{
	GElf_Phdr phdr;
	uint32_t i;
	char *core_cmdline;
	const char *seg_name;

	if (name == NULL || elf == NULL || elfhdr == NULL)
		return (RETURN_DATAERR);
	if  (elfhdr->e_shnum != 0 || elfhdr->e_type != ET_CORE)
		return (RETURN_DATAERR);

	seg_name = core_cmdline = NULL;
	if (style == STYLE_SYSV)
		sysv_header(name, NULL);
	else
		berkeley_header();

	for (i = 0; i < elfhdr->e_phnum; i++) {
		if (gelf_getphdr(elf, i, &phdr) != NULL) {
			if (phdr.p_type == PT_NOTE) {
				handle_phdr(elf, elfhdr, &phdr, i, "note");
				handle_core_note(elf, elfhdr, &phdr,
				    &core_cmdline);
			} else {
				switch(phdr.p_type) {
				case PT_NULL:
					seg_name = "null";
					break;
				case PT_LOAD:
					seg_name = "load";
					break;
				case PT_DYNAMIC:
					seg_name = "dynamic";
					break;
				case PT_INTERP:
					seg_name = "interp";
					break;
				case PT_SHLIB:
					seg_name = "shlib";
					break;
				case PT_PHDR:
					seg_name = "phdr";
					break;
				case PT_GNU_EH_FRAME:
					seg_name = "eh_frame_hdr";
					break;
				case PT_GNU_STACK:
					seg_name = "stack";
					break;
				default:
					seg_name = "segment";
				}
				handle_phdr(elf, elfhdr, &phdr, i, seg_name);
			}
		}
	}

	if (style == STYLE_BERKELEY) {
		if (core_cmdline != NULL) {
			berkeley_footer(core_cmdline, name,
			    "core file invoked as");
		} else {
			berkeley_footer(core_cmdline, name, "core file");
		}
	} else {
		sysv_footer();
		if (core_cmdline != NULL) {
			(void) printf(" (core file invoked as %s)\n\n",
			    core_cmdline);
		} else {
			(void) printf(" (core file)\n\n");
		}
	}
	free(core_cmdline);
	return (RETURN_OK);
}

/*
 * Given an elf object,ar(1) filename, and based on the output style
 * and radix format the various sections and their length will be printed
 * or the size of the text, data, bss sections will be printed out.
 */
static int
handle_elf(char const *name)
{
	GElf_Ehdr elfhdr;
	GElf_Shdr shdr;
	Elf *elf, *elf1;
	Elf_Arhdr *arhdr;
	Elf_Scn *scn;
	Elf_Cmd elf_cmd;
	int exit_code, fd;

	if (name == NULL)
		return (RETURN_NOINPUT);

	if ((fd = open(name, O_RDONLY, 0)) < 0)
		return (RETURN_NOINPUT);

	elf_cmd = ELF_C_READ;
	elf1 = elf_begin(fd, elf_cmd, NULL);
	while ((elf = elf_begin(fd, elf_cmd, elf1)) != NULL) {
		arhdr = elf_getarhdr(elf);
		if (elf_kind(elf) == ELF_K_NONE && arhdr == NULL) {
			(void) elf_end(elf);
			(void) elf_end(elf1);
			(void) close(fd);
			return (RETURN_DATAERR);
		}
		if (elf_kind(elf) != ELF_K_ELF ||
		    (gelf_getehdr(elf, &elfhdr) == NULL)) {
			elf_cmd = elf_next(elf);
			(void) elf_end(elf);
			warnx("%s: File format not recognized",
			    arhdr != NULL ? arhdr->ar_name : name);
			continue;
		}
		/* Core dumps are handled separately */
		if (elfhdr.e_shnum == 0 && elfhdr.e_type == ET_CORE) {
			exit_code = handle_core(name, elf, &elfhdr);
			(void) elf_end(elf);
			(void) elf_end(elf1);
			(void) close(fd);
			return (exit_code);
		} else {
			scn = NULL;
			if (style == STYLE_BERKELEY) {
				berkeley_header();
				while ((scn = elf_nextscn(elf, scn)) != NULL) {
					if (gelf_getshdr(scn, &shdr) != NULL)
						berkeley_calc(&shdr);
				}
			} else {
				sysv_header(name, arhdr);
				scn = NULL;
				while ((scn = elf_nextscn(elf, scn)) != NULL) {
					if (gelf_getshdr(scn, &shdr) !=	NULL)
						sysv_calc(elf, &elfhdr, &shdr);
				}
			}
			if (style == STYLE_BERKELEY) {
				if (arhdr != NULL) {
					berkeley_footer(name, arhdr->ar_name,
					    "ex");
				} else {
					berkeley_footer(name, NULL, "ex");
				}
			} else {
				sysv_footer();
			}
		}
		elf_cmd = elf_next(elf);
		(void) elf_end(elf);
	}
	(void) elf_end(elf1);
	(void) close(fd);
	return (RETURN_OK);
}

/*
 * Sysv formatting helper functions.
 */
static void
sysv_header(const char *name, Elf_Arhdr *arhdr)
{

	text_size_total = 0;
	if (arhdr != NULL)
		(void) printf("%s   (ex %s):\n", arhdr->ar_name, name);
	else
		(void) printf("%s  :\n", name);
	tbl_new(3);
	tbl_append();
	tbl_print("section", 0);
	tbl_print("size", 1);
	tbl_print("addr", 2);
}

static void
sysv_calc(Elf *elf, GElf_Ehdr *elfhdr, GElf_Shdr *shdr)
{
	char *section_name;

	section_name = elf_strptr(elf, elfhdr->e_shstrndx,
	    (size_t) shdr->sh_name);
	if ((shdr->sh_type == SHT_SYMTAB ||
	    shdr->sh_type == SHT_STRTAB || shdr->sh_type == SHT_RELA ||
	    shdr->sh_type == SHT_REL) && shdr->sh_addr == 0)
		return;
	tbl_append();
	tbl_print(section_name, 0);
	tbl_print_num(shdr->sh_size, radix, 1);
	tbl_print_num(shdr->sh_addr, radix, 2);
	text_size_total += shdr->sh_size;
}

static void
sysv_footer(void)
{
	tbl_append();
	tbl_print("Total", 0);
	tbl_print_num(text_size_total, radix, 1);
	tbl_flush();
	putchar('\n');
}

/*
 * berkeley style output formatting helper functions.
 */
static void
berkeley_header(void)
{
	static int printed;

	text_size = data_size = bss_size = 0;
	if (!printed) {
		tbl_new(6);
		tbl_append();
		tbl_print("text", 0);
		tbl_print("data", 1);
		tbl_print("bss", 2);
		if (radix == RADIX_OCTAL)
			tbl_print("oct", 3);
		else
			tbl_print("dec", 3);
		tbl_print("hex", 4);
		tbl_print("filename", 5);
		printed = 1;
	}
}

static void
berkeley_calc(GElf_Shdr *shdr)
{
	if (shdr != NULL) {
		if (!(shdr->sh_flags & SHF_ALLOC))
			return;
		if ((shdr->sh_flags & SHF_ALLOC) &&
		    ((shdr->sh_flags & SHF_EXECINSTR) ||
		    !(shdr->sh_flags & SHF_WRITE)))
			text_size += shdr->sh_size;
		else if ((shdr->sh_flags & SHF_ALLOC) &&
		    (shdr->sh_flags & SHF_WRITE) &&
		    (shdr->sh_type != SHT_NOBITS))
			data_size += shdr->sh_size;
		else
			bss_size += shdr->sh_size;
	}
}

static void
berkeley_totals(void)
{
	uint64_t grand_total;

	grand_total = text_size_total + data_size_total + bss_size_total;
	tbl_append();
	tbl_print_num(text_size_total, radix, 0);
	tbl_print_num(data_size_total, radix, 1);
	tbl_print_num(bss_size_total, radix, 2);
	if (radix == RADIX_OCTAL)
		tbl_print_num(grand_total, RADIX_OCTAL, 3);
	else
		tbl_print_num(grand_total, RADIX_DECIMAL, 3);
	tbl_print_num(grand_total, RADIX_HEX, 4);
}

static void
berkeley_footer(const char *name, const char *ar_name, const char *msg)
{
	char buf[BUF_SIZE];

	total_size = text_size + data_size + bss_size;
	if (show_totals) {
		text_size_total += text_size;
		bss_size_total += bss_size;
		data_size_total += data_size;
	}

	tbl_append();
	tbl_print_num(text_size, radix, 0);
	tbl_print_num(data_size, radix, 1);
	tbl_print_num(bss_size, radix, 2);
	if (radix == RADIX_OCTAL)
		tbl_print_num(total_size, RADIX_OCTAL, 3);
	else
		tbl_print_num(total_size, RADIX_DECIMAL, 3);
	tbl_print_num(total_size, RADIX_HEX, 4);
	if (ar_name != NULL && name != NULL)
		(void) snprintf(buf, BUF_SIZE, "%s (%s %s)", ar_name, msg,
		    name);
	else if (ar_name != NULL && name == NULL)
		(void) snprintf(buf, BUF_SIZE, "%s (%s)", ar_name, msg);
	else
		(void) snprintf(buf, BUF_SIZE, "%s", name);
	tbl_print(buf, 5);
}


static void
tbl_new(int col)
{

	assert(tb == NULL);
	assert(col > 0);
	if ((tb = calloc(1, sizeof(*tb))) == NULL)
		err(EXIT_FAILURE, "calloc");
	if ((tb->tbl = calloc(col, sizeof(*tb->tbl))) == NULL)
		err(EXIT_FAILURE, "calloc");
	if ((tb->width = calloc(col, sizeof(*tb->width))) == NULL)
		err(EXIT_FAILURE, "calloc");
	tb->col = col;
	tb->row = 0;
}

static void
tbl_print(const char *s, int col)
{
	int len;

	assert(tb != NULL && tb->col > 0 && tb->row > 0 && col < tb->col);
	assert(s != NULL && tb->tbl[col][tb->row - 1] == NULL);
	if ((tb->tbl[col][tb->row - 1] = strdup(s)) == NULL)
		err(EXIT_FAILURE, "strdup");
	len = strlen(s);
	if (len > tb->width[col])
		tb->width[col] = len;
}

static void
tbl_print_num(uint64_t num, enum radix_style rad, int col)
{
	char buf[BUF_SIZE];

	(void) snprintf(buf, BUF_SIZE, (rad == RADIX_DECIMAL ? "%ju" :
	    ((rad == RADIX_OCTAL) ? "0%jo" : "0x%jx")), (uintmax_t) num);
	tbl_print(buf, col);
}

static void
tbl_append(void)
{
	int i;

	assert(tb != NULL && tb->col > 0);
	tb->row++;
	for (i = 0; i < tb->col; i++) {
		tb->tbl[i] = realloc(tb->tbl[i], sizeof(*tb->tbl[i]) * tb->row);
		if (tb->tbl[i] == NULL)
			err(EXIT_FAILURE, "realloc");
		tb->tbl[i][tb->row - 1] = NULL;
	}
}

static void
tbl_flush(void)
{
	const char *str;
	int i, j;

	if (tb == NULL)
		return;

	assert(tb->col > 0);
	for (i = 0; i < tb->row; i++) {
		if (style == STYLE_BERKELEY)
			printf("  ");
		for (j = 0; j < tb->col; j++) {
			str = (tb->tbl[j][i] != NULL ? tb->tbl[j][i] : "");
			if (style == STYLE_SYSV && j == 0)
				printf("%-*s", tb->width[j], str);
			else if (style == STYLE_BERKELEY && j == tb->col - 1)
				printf("%s", str);
			else
				printf("%*s", tb->width[j], str);
			if (j == tb->col -1)
				putchar('\n');
			else
				printf("   ");
		}
	}

	for (i = 0; i < tb->col; i++) {
		for (j = 0; j < tb->row; j++) {
			if (tb->tbl[i][j])
				free(tb->tbl[i][j]);
		}
		free(tb->tbl[i]);
	}
	free(tb->tbl);
	free(tb->width);
	free(tb);
	tb = NULL;
}

#define	USAGE_MESSAGE	"\
Usage: %s [options] file ...\n\
  Display sizes of ELF sections.\n\n\
  Options:\n\
  --format=format    Display output in specified format.  Supported\n\
                     values are `berkeley' and `sysv'.\n\
  --help             Display this help message and exit.\n\
  --radix=radix      Display numeric values in the specified radix.\n\
                     Supported values are: 8, 10 and 16.\n\
  --totals           Show cumulative totals of section sizes.\n\
  --version          Display a version identifier and exit.\n\
  -A                 Equivalent to `--format=sysv'.\n\
  -B                 Equivalent to `--format=berkeley'.\n\
  -V                 Equivalent to `--version'.\n\
  -d                 Equivalent to `--radix=10'.\n\
  -h                 Same as option --help.\n\
  -o                 Equivalent to `--radix=8'.\n\
  -t                 Equivalent to option --totals.\n\
  -x                 Equivalent to `--radix=16'.\n"

static void
usage(void)
{
	(void) fprintf(stderr, USAGE_MESSAGE, ELFTC_GETPROGNAME());
	exit(EXIT_FAILURE);
}

static void
show_version(void)
{
	(void) printf("%s (%s)\n", ELFTC_GETPROGNAME(), elftc_version());
	exit(EXIT_SUCCESS);
}
