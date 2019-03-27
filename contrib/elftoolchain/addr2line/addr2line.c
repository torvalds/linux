/*-
 * Copyright (c) 2009 Kai Wang
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

#include <sys/param.h>
#include <dwarf.h>
#include <err.h>
#include <fcntl.h>
#include <gelf.h>
#include <getopt.h>
#include <libdwarf.h>
#include <libelftc.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uthash.h"
#include "_elftc.h"

ELFTC_VCSID("$Id: addr2line.c 3544 2017-06-05 14:51:44Z emaste $");

struct Func {
	char *name;
	Dwarf_Unsigned lopc;
	Dwarf_Unsigned hipc;
	Dwarf_Unsigned call_file;
	Dwarf_Unsigned call_line;
	Dwarf_Ranges *ranges;
	Dwarf_Signed ranges_cnt;
	struct Func *inlined_caller;
	STAILQ_ENTRY(Func) next;
};

struct CU {
	Dwarf_Off off;
	Dwarf_Unsigned lopc;
	Dwarf_Unsigned hipc;
	char **srcfiles;
	Dwarf_Signed nsrcfiles;
	STAILQ_HEAD(, Func) funclist;
	UT_hash_handle hh;
};

static struct option longopts[] = {
	{"addresses", no_argument, NULL, 'a'},
	{"target" , required_argument, NULL, 'b'},
	{"demangle", no_argument, NULL, 'C'},
	{"exe", required_argument, NULL, 'e'},
	{"functions", no_argument, NULL, 'f'},
	{"inlines", no_argument, NULL, 'i'},
	{"section", required_argument, NULL, 'j'},
	{"pretty-print", no_argument, NULL, 'p'},
	{"basename", no_argument, NULL, 's'},
	{"help", no_argument, NULL, 'H'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};
static int demangle, func, base, inlines, print_addr, pretty_print;
static char unknown[] = { '?', '?', '\0' };
static Dwarf_Addr section_base;
static struct CU *culist;

#define	USAGE_MESSAGE	"\
Usage: %s [options] hexaddress...\n\
  Map program addresses to source file names and line numbers.\n\n\
  Options:\n\
  -a      | --addresses       Display address prior to line number info.\n\
  -b TGT  | --target=TGT      (Accepted but ignored).\n\
  -e EXE  | --exe=EXE         Use program \"EXE\" to translate addresses.\n\
  -f      | --functions       Display function names.\n\
  -i      | --inlines         Display caller info for inlined functions.\n\
  -j NAME | --section=NAME    Values are offsets into section \"NAME\".\n\
  -p      | --pretty-print    Display line number info and function name\n\
                              in human readable manner.\n\
  -s      | --basename        Only show the base name for each file name.\n\
  -C      | --demangle        Demangle C++ names.\n\
  -H      | --help            Print a help message.\n\
  -V      | --version         Print a version identifier and exit.\n"

static void
usage(void)
{
	(void) fprintf(stderr, USAGE_MESSAGE, ELFTC_GETPROGNAME());
	exit(1);
}

static void
version(void)
{

	fprintf(stderr, "%s (%s)\n", ELFTC_GETPROGNAME(), elftc_version());
	exit(0);
}

/*
 * Handle DWARF 4 'offset from' DW_AT_high_pc.  Although we don't
 * fully support DWARF 4, some compilers (like FreeBSD Clang 3.5.1)
 * generate DW_AT_high_pc as an offset from DW_AT_low_pc.
 *
 * "If the value of the DW_AT_high_pc is of class address, it is the
 * relocated address of the first location past the last instruction
 * associated with the entity; if it is of class constant, the value
 * is an unsigned integer offset which when added to the low PC gives
 * the address of the first location past the last instruction
 * associated with the entity."
 *
 * DWARF4 spec, section 2.17.2.
 */
static int
handle_high_pc(Dwarf_Die die, Dwarf_Unsigned lopc, Dwarf_Unsigned *hipc)
{
	Dwarf_Error de;
	Dwarf_Half form;
	Dwarf_Attribute at;
	int ret;

	ret = dwarf_attr(die, DW_AT_high_pc, &at, &de);
	if (ret == DW_DLV_ERROR) {
		warnx("dwarf_attr failed: %s", dwarf_errmsg(de));
		return (ret);
	}
	ret = dwarf_whatform(at, &form, &de);
	if (ret == DW_DLV_ERROR) {
		warnx("dwarf_whatform failed: %s", dwarf_errmsg(de));
		return (ret);
	}
	if (dwarf_get_form_class(2, 0, 0, form) == DW_FORM_CLASS_CONSTANT)
		*hipc += lopc;

	return (DW_DLV_OK);
}

static struct Func *
search_func(struct CU *cu, Dwarf_Unsigned addr)
{
	struct Func *f, *f0;
	Dwarf_Unsigned lopc, hipc, addr_base;
	int i;

	f0 = NULL;

	STAILQ_FOREACH(f, &cu->funclist, next) {
		if (f->ranges != NULL) {
			addr_base = 0;
			for (i = 0; i < f->ranges_cnt; i++) {
				if (f->ranges[i].dwr_type == DW_RANGES_END)
					break;
				if (f->ranges[i].dwr_type ==
				    DW_RANGES_ADDRESS_SELECTION) {
					addr_base = f->ranges[i].dwr_addr2;
					continue;
				}

				/* DW_RANGES_ENTRY */
				lopc = f->ranges[i].dwr_addr1 + addr_base;
				hipc = f->ranges[i].dwr_addr2 + addr_base;
				if (addr >= lopc && addr < hipc) {
					if (f0 == NULL ||
					    (lopc >= f0->lopc &&
					    hipc <= f0->hipc)) {
						f0 = f;
						f0->lopc = lopc;
						f0->hipc = hipc;
						break;
					}
				}
			}
		} else if (addr >= f->lopc && addr < f->hipc) {
			if (f0 == NULL ||
			    (f->lopc >= f0->lopc && f->hipc <= f0->hipc))
				f0 = f;
		}
	}

	return (f0);
}

static void
collect_func(Dwarf_Debug dbg, Dwarf_Die die, struct Func *parent, struct CU *cu)
{
	Dwarf_Die ret_die, abst_die, spec_die;
	Dwarf_Error de;
	Dwarf_Half tag;
	Dwarf_Unsigned lopc, hipc, ranges_off;
	Dwarf_Signed ranges_cnt;
	Dwarf_Off ref;
	Dwarf_Attribute abst_at, spec_at;
	Dwarf_Ranges *ranges;
	const char *funcname;
	struct Func *f;
	int found_ranges, ret;

	f = NULL;
	abst_die = spec_die = NULL;

	if (dwarf_tag(die, &tag, &de)) {
		warnx("dwarf_tag: %s", dwarf_errmsg(de));
		goto cont_search;
	}
	if (tag == DW_TAG_subprogram || tag == DW_TAG_entry_point ||
	    tag == DW_TAG_inlined_subroutine) {
		/*
		 * Function address range can be specified by either
		 * a DW_AT_ranges attribute which points to a range list or
		 * by a pair of DW_AT_low_pc and DW_AT_high_pc attributes.
		 */
		ranges = NULL;
		ranges_cnt = 0;
		found_ranges = 0;
		if (dwarf_attrval_unsigned(die, DW_AT_ranges, &ranges_off,
		    &de) == DW_DLV_OK &&
		    dwarf_get_ranges(dbg, (Dwarf_Off) ranges_off, &ranges,
		    &ranges_cnt, NULL, &de) == DW_DLV_OK) {
			if (ranges != NULL && ranges_cnt > 0) {
				found_ranges = 1;
				goto get_func_name;
			}
		}

		/*
		 * Search for DW_AT_low_pc/DW_AT_high_pc if ranges pointer
		 * not found.
		 */
		if (dwarf_attrval_unsigned(die, DW_AT_low_pc, &lopc, &de) ||
		    dwarf_attrval_unsigned(die, DW_AT_high_pc, &hipc, &de))
			goto cont_search;
		if (handle_high_pc(die, lopc, &hipc) != DW_DLV_OK)
			goto cont_search;

	get_func_name:
		/*
		 * Most common case the function name is stored in DW_AT_name
		 * attribute.
		 */
		if (dwarf_attrval_string(die, DW_AT_name, &funcname, &de) ==
		    DW_DLV_OK)
			goto add_func;

		/*
		 * For inlined function, the actual name is probably in the DIE
		 * referenced by DW_AT_abstract_origin. (if present)
		 */
		if (dwarf_attr(die, DW_AT_abstract_origin, &abst_at, &de) ==
		    DW_DLV_OK &&
		    dwarf_global_formref(abst_at, &ref, &de) == DW_DLV_OK &&
		    dwarf_offdie(dbg, ref, &abst_die, &de) == DW_DLV_OK &&
		    dwarf_attrval_string(abst_die, DW_AT_name, &funcname,
		    &de) == DW_DLV_OK)
			goto add_func;

		/*
		 * If DW_AT_name is not present, but DW_AT_specification is
		 * present, then probably the actual name is in the DIE
		 * referenced by DW_AT_specification.
		 */
		if (dwarf_attr(die, DW_AT_specification, &spec_at, &de) ==
		    DW_DLV_OK &&
		    dwarf_global_formref(spec_at, &ref, &de) == DW_DLV_OK &&
		    dwarf_offdie(dbg, ref, &spec_die, &de) == DW_DLV_OK &&
		    dwarf_attrval_string(spec_die, DW_AT_name, &funcname,
		    &de) == DW_DLV_OK)
			goto add_func;

		/* Skip if no name associated with this DIE. */
		goto cont_search;

	add_func:
		if ((f = calloc(1, sizeof(*f))) == NULL)
			err(EXIT_FAILURE, "calloc");
		if ((f->name = strdup(funcname)) == NULL)
			err(EXIT_FAILURE, "strdup");
		if (found_ranges) {
			f->ranges = ranges;
			f->ranges_cnt = ranges_cnt;
		} else {
			f->lopc = lopc;
			f->hipc = hipc;
		}
		if (tag == DW_TAG_inlined_subroutine) {
			f->inlined_caller = parent;
			dwarf_attrval_unsigned(die, DW_AT_call_file,
			    &f->call_file, &de);
			dwarf_attrval_unsigned(die, DW_AT_call_line,
			    &f->call_line, &de);
		}
		STAILQ_INSERT_TAIL(&cu->funclist, f, next);
	}

cont_search:

	/* Search children. */
	ret = dwarf_child(die, &ret_die, &de);
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_child: %s", dwarf_errmsg(de));
	else if (ret == DW_DLV_OK) {
		if (f != NULL)
			collect_func(dbg, ret_die, f, cu);
		else
			collect_func(dbg, ret_die, parent, cu);
	}

	/* Search sibling. */
	ret = dwarf_siblingof(dbg, die, &ret_die, &de);
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_siblingof: %s", dwarf_errmsg(de));
	else if (ret == DW_DLV_OK)
		collect_func(dbg, ret_die, parent, cu);

	/* Cleanup */
	dwarf_dealloc(dbg, die, DW_DLA_DIE);

	if (abst_die != NULL)
		dwarf_dealloc(dbg, abst_die, DW_DLA_DIE);

	if (spec_die != NULL)
		dwarf_dealloc(dbg, spec_die, DW_DLA_DIE);
}

static void
print_inlines(struct CU *cu, struct Func *f, Dwarf_Unsigned call_file,
    Dwarf_Unsigned call_line)
{
	char demangled[1024];
	char *file;

	if (call_file > 0 && (Dwarf_Signed) call_file <= cu->nsrcfiles)
		file = cu->srcfiles[call_file - 1];
	else
		file = unknown;

	if (pretty_print)
		printf(" (inlined by) ");

	if (func) {
		if (demangle && !elftc_demangle(f->name, demangled,
		    sizeof(demangled), 0)) {
			if (pretty_print)
				printf("%s at ", demangled);
			else
				printf("%s\n", demangled);
		} else {
			if (pretty_print)
				printf("%s at ", f->name);
			else
				printf("%s\n", f->name);
		}
	}
	(void) printf("%s:%ju\n", base ? basename(file) : file,
	    (uintmax_t) call_line);

	if (f->inlined_caller != NULL)
		print_inlines(cu, f->inlined_caller, f->call_file,
		    f->call_line);
}

static void
translate(Dwarf_Debug dbg, Elf *e, const char* addrstr)
{
	Dwarf_Die die, ret_die;
	Dwarf_Line *lbuf;
	Dwarf_Error de;
	Dwarf_Half tag;
	Dwarf_Unsigned lopc, hipc, addr, lineno, plineno;
	Dwarf_Signed lcount;
	Dwarf_Addr lineaddr, plineaddr;
	Dwarf_Off off;
	struct CU *cu;
	struct Func *f;
	const char *funcname;
	char *file, *file0, *pfile;
	char demangled[1024];
	int ec, i, ret;

	addr = strtoull(addrstr, NULL, 16);
	addr += section_base;
	lineno = 0;
	file = unknown;
	cu = NULL;
	die = NULL;

	while ((ret = dwarf_next_cu_header(dbg, NULL, NULL, NULL, NULL, NULL,
	    &de)) ==  DW_DLV_OK) {
		die = NULL;
		while (dwarf_siblingof(dbg, die, &ret_die, &de) == DW_DLV_OK) {
			if (die != NULL)
				dwarf_dealloc(dbg, die, DW_DLA_DIE);
			die = ret_die;
			if (dwarf_tag(die, &tag, &de) != DW_DLV_OK) {
				warnx("dwarf_tag failed: %s",
				    dwarf_errmsg(de));
				goto next_cu;
			}

			/* XXX: What about DW_TAG_partial_unit? */
			if (tag == DW_TAG_compile_unit)
				break;
		}
		if (ret_die == NULL) {
			warnx("could not find DW_TAG_compile_unit die");
			goto next_cu;
		}
		if (dwarf_attrval_unsigned(die, DW_AT_low_pc, &lopc, &de) ==
		    DW_DLV_OK) {
			if (dwarf_attrval_unsigned(die, DW_AT_high_pc, &hipc,
			   &de) == DW_DLV_OK) {
				/*
				 * Check if the address falls into the PC
				 * range of this CU.
				 */
				if (handle_high_pc(die, lopc, &hipc) !=
				    DW_DLV_OK)
					goto out;
			} else {
				/* Assume ~0ULL if DW_AT_high_pc not present */
				hipc = ~0ULL;
			}

			/*
			 * Record the CU in the hash table for faster lookup
			 * later.
			 */
			if (dwarf_dieoffset(die, &off, &de) != DW_DLV_OK) {
				warnx("dwarf_dieoffset failed: %s",
				    dwarf_errmsg(de));
				goto out;
			}
			HASH_FIND(hh, culist, &off, sizeof(off), cu);
			if (cu == NULL) {
				if ((cu = calloc(1, sizeof(*cu))) == NULL)
					err(EXIT_FAILURE, "calloc");
				cu->off = off;
				cu->lopc = lopc;
				cu->hipc = hipc;
				STAILQ_INIT(&cu->funclist);
				HASH_ADD(hh, culist, off, sizeof(off), cu);
			}

			if (addr >= lopc && addr < hipc)
				break;
		}

	next_cu:
		if (die != NULL) {
			dwarf_dealloc(dbg, die, DW_DLA_DIE);
			die = NULL;
		}
	}

	if (ret != DW_DLV_OK || die == NULL)
		goto out;

	switch (dwarf_srclines(die, &lbuf, &lcount, &de)) {
	case DW_DLV_OK:
		break;
	case DW_DLV_NO_ENTRY:
		/* If a CU lacks debug info, just skip it. */
		goto out;
	default:
		warnx("dwarf_srclines: %s", dwarf_errmsg(de));
		goto out;
	}

	plineaddr = ~0ULL;
	plineno = 0;
	pfile = unknown;
	for (i = 0; i < lcount; i++) {
		if (dwarf_lineaddr(lbuf[i], &lineaddr, &de)) {
			warnx("dwarf_lineaddr: %s", dwarf_errmsg(de));
			goto out;
		}
		if (dwarf_lineno(lbuf[i], &lineno, &de)) {
			warnx("dwarf_lineno: %s", dwarf_errmsg(de));
			goto out;
		}
		if (dwarf_linesrc(lbuf[i], &file0, &de)) {
			warnx("dwarf_linesrc: %s", dwarf_errmsg(de));
		} else
			file = file0;
		if (addr == lineaddr)
			goto out;
		else if (addr < lineaddr && addr > plineaddr) {
			lineno = plineno;
			file = pfile;
			goto out;
		}
		plineaddr = lineaddr;
		plineno = lineno;
		pfile = file;
	}

out:
	f = NULL;
	funcname = NULL;
	if (ret == DW_DLV_OK && (func || inlines) && cu != NULL) {
		if (cu->srcfiles == NULL)
			if (dwarf_srcfiles(die, &cu->srcfiles, &cu->nsrcfiles,
			    &de))
				warnx("dwarf_srcfiles: %s", dwarf_errmsg(de));
		if (STAILQ_EMPTY(&cu->funclist)) {
			collect_func(dbg, die, NULL, cu);
			die = NULL;
		}
		f = search_func(cu, addr);
		if (f != NULL)
			funcname = f->name;
	}

	if (print_addr) {
		if ((ec = gelf_getclass(e)) == ELFCLASSNONE) {
			warnx("gelf_getclass failed: %s", elf_errmsg(-1));
			ec = ELFCLASS64;
		}
		if (ec == ELFCLASS32) {
			if (pretty_print)
				printf("0x%08jx: ", (uintmax_t) addr);
			else
				printf("0x%08jx\n", (uintmax_t) addr);
		} else {
			if (pretty_print)
				printf("0x%016jx: ", (uintmax_t) addr);
			else
				printf("0x%016jx\n", (uintmax_t) addr);
		}
	}

	if (func) {
		if (funcname == NULL)
			funcname = unknown;
		if (demangle && !elftc_demangle(funcname, demangled,
		    sizeof(demangled), 0)) {
			if (pretty_print)
				printf("%s at ", demangled);
			else
				printf("%s\n", demangled);
		} else {
			if (pretty_print)
				printf("%s at ", funcname);
			else
				printf("%s\n", funcname);
		}
	}

	(void) printf("%s:%ju\n", base ? basename(file) : file,
	    (uintmax_t) lineno);

	if (ret == DW_DLV_OK && inlines && cu != NULL &&
	    cu->srcfiles != NULL && f != NULL && f->inlined_caller != NULL)
		print_inlines(cu, f->inlined_caller, f->call_file,
		    f->call_line);

	if (die != NULL)
		dwarf_dealloc(dbg, die, DW_DLA_DIE);

	/*
	 * Reset internal CU pointer, so we will start from the first CU
	 * next round.
	 */
	while (ret != DW_DLV_NO_ENTRY) {
		if (ret == DW_DLV_ERROR)
			errx(EXIT_FAILURE, "dwarf_next_cu_header: %s",
			    dwarf_errmsg(de));
		ret = dwarf_next_cu_header(dbg, NULL, NULL, NULL, NULL, NULL,
		    &de);
	}
}

static void
find_section_base(const char *exe, Elf *e, const char *section)
{
	Dwarf_Addr off;
	Elf_Scn *scn;
	GElf_Ehdr eh;
	GElf_Shdr sh;
	size_t shstrndx;
	int elferr;
	const char *name;

	if (gelf_getehdr(e, &eh) != &eh) {
		warnx("gelf_getehdr failed: %s", elf_errmsg(-1));
		return;
	}

	if (!elf_getshstrndx(e, &shstrndx)) {
		warnx("elf_getshstrndx failed: %s", elf_errmsg(-1));
		return;
	}

	(void) elf_errno();
	off = 0;
	scn = NULL;
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		if (gelf_getshdr(scn, &sh) == NULL) {
			warnx("gelf_getshdr failed: %s", elf_errmsg(-1));
			continue;
		}
		if ((name = elf_strptr(e, shstrndx, sh.sh_name)) == NULL)
			goto next;
		if (!strcmp(section, name)) {
			if (eh.e_type == ET_EXEC || eh.e_type == ET_DYN) {
				/*
				 * For executables, section base is the virtual
				 * address of the specified section.
				 */
				section_base = sh.sh_addr;
			} else if (eh.e_type == ET_REL) {
				/*
				 * For relocatables, section base is the
				 * relative offset of the specified section
				 * to the start of the first section.
				 */
				section_base = off;
			} else
				warnx("unknown e_type %u", eh.e_type);
			return;
		}
	next:
		off += sh.sh_size;
	}
	elferr = elf_errno();
	if (elferr != 0)
		warnx("elf_nextscn failed: %s", elf_errmsg(elferr));

	errx(EXIT_FAILURE, "%s: cannot find section %s", exe, section);
}

int
main(int argc, char **argv)
{
	Elf *e;
	Dwarf_Debug dbg;
	Dwarf_Error de;
	const char *exe, *section;
	char line[1024];
	int fd, i, opt;

	exe = NULL;
	section = NULL;
	while ((opt = getopt_long(argc, argv, "ab:Ce:fij:psHV", longopts,
	    NULL)) != -1) {
		switch (opt) {
		case 'a':
			print_addr = 1;
			break;
		case 'b':
			/* ignored */
			break;
		case 'C':
			demangle = 1;
			break;
		case 'e':
			exe = optarg;
			break;
		case 'f':
			func = 1;
			break;
		case 'i':
			inlines = 1;
			break;
		case 'j':
			section = optarg;
			break;
		case 'p':
			pretty_print = 1;
			break;
		case 's':
			base = 1;
			break;
		case 'H':
			usage();
		case 'V':
			version();
		default:
			usage();
		}
	}

	argv += optind;
	argc -= optind;

	if (exe == NULL)
		exe = "a.out";

	if ((fd = open(exe, O_RDONLY)) < 0)
		err(EXIT_FAILURE, "%s", exe);

	if (dwarf_init(fd, DW_DLC_READ, NULL, NULL, &dbg, &de))
		errx(EXIT_FAILURE, "dwarf_init: %s", dwarf_errmsg(de));

	if (dwarf_get_elf(dbg, &e, &de) != DW_DLV_OK)
		errx(EXIT_FAILURE, "dwarf_get_elf: %s", dwarf_errmsg(de));

	if (section)
		find_section_base(exe, e, section);
	else
		section_base = 0;

	if (argc > 0)
		for (i = 0; i < argc; i++)
			translate(dbg, e, argv[i]);
	else {
		setvbuf(stdout, NULL, _IOLBF, 0);
		while (fgets(line, sizeof(line), stdin) != NULL)
			translate(dbg, e, line);
	}

	dwarf_finish(dbg, &de);

	(void) elf_end(e);

	exit(0);
}
