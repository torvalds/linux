/*-
 * Copyright (c) 2007 Hyogeol Lee <hyogeollee@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ar.h>
#include <assert.h>
#include <ctype.h>
#include <dwarf.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <getopt.h>
#include <inttypes.h>
#include <libdwarf.h>
#include <libelftc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "_elftc.h"

ELFTC_VCSID("$Id: nm.c 3504 2016-12-17 15:33:16Z kaiwang27 $");

/* symbol information list */
STAILQ_HEAD(sym_head, sym_entry);

struct sym_entry {
	char		*name;
	GElf_Sym	*sym;
	STAILQ_ENTRY(sym_entry) sym_entries;
};

typedef int (*fn_sort)(const void *, const void *);
typedef void (*fn_elem_print)(char, const char *, const GElf_Sym *, const char *);
typedef void (*fn_sym_print)(const GElf_Sym *);
typedef int (*fn_filter)(char, const GElf_Sym *, const char *);

/* output filter list */
static SLIST_HEAD(filter_head, filter_entry) nm_out_filter =
    SLIST_HEAD_INITIALIZER(nm_out_filter);

struct filter_entry {
	fn_filter	fn;
	SLIST_ENTRY(filter_entry) filter_entries;
};

struct sym_print_data {
	struct sym_head	*headp;
	size_t		sh_num, list_num;
	const char	*t_table, **s_table, *filename, *objname;
};

struct nm_prog_info {
	const char	*name;
	const char	*def_filename;
};

/* List for line number information. */
struct line_info_entry {
	uint64_t	addr;	/* address */
	uint64_t	line;	/* line number */
	char		*file;	/* file name with path */
	SLIST_ENTRY(line_info_entry) entries;
};
SLIST_HEAD(line_info_head, line_info_entry);

/* List for function line number information. */
struct func_info_entry {
	char		*name;	/* function name */
	char		*file;	/* file name with path */
	uint64_t	lowpc;	/* low address */
	uint64_t	highpc;	/* high address */
	uint64_t	line;	/* line number */
	SLIST_ENTRY(func_info_entry) entries;
};
SLIST_HEAD(func_info_head, func_info_entry);

/* List for variable line number information. */
struct var_info_entry {
	char		*name;	/* variable name */
	char		*file;	/* file name with path */
	uint64_t	addr;	/* address */
	uint64_t	line;	/* line number */
	SLIST_ENTRY(var_info_entry) entries;
};
SLIST_HEAD(var_info_head, var_info_entry);

/* output numric type */
enum radix {
	RADIX_OCT,
	RADIX_HEX,
	RADIX_DEC
};

/* output symbol type, PRINT_SYM_DYN for dynamic symbol only */
enum print_symbol {
	PRINT_SYM_SYM,
	PRINT_SYM_DYN
};

/* output name type */
enum print_name {
	PRINT_NAME_NONE,
	PRINT_NAME_FULL,
	PRINT_NAME_MULTI
};

struct nm_prog_options {
	enum print_symbol	print_symbol;
	enum print_name		print_name;
	enum radix		t;
	int			demangle_type;
	bool			print_debug;
	bool			print_armap;
	int			print_size;
	bool			debug_line;
	int			def_only;
	bool			undef_only;
	int			sort_size;
	bool			sort_reverse;
	int			no_demangle;

	/*
	 * function pointer to sort symbol list.
	 * possible function - cmp_name, cmp_none, cmp_size, cmp_value
	 */
	fn_sort			sort_fn;

	/*
	 * function pointer to print symbol elem.
	 * possible function - sym_elem_print_all
	 *		       sym_elem_print_all_portable
	 *		       sym_elem_print_all_sysv
	 */
	fn_elem_print		elem_print_fn;

	fn_sym_print		value_print_fn;
	fn_sym_print		size_print_fn;
};

#define	CHECK_SYM_PRINT_DATA(p)	(p->headp == NULL || p->sh_num == 0 ||	      \
p->t_table == NULL || p->s_table == NULL || p->filename == NULL)
#define	IS_SYM_TYPE(t)		((t) == '?' || isalpha((t)) != 0)
#define	IS_UNDEF_SYM_TYPE(t)	((t) == 'U' || (t) == 'v' || (t) == 'w')
#define	UNUSED(p)		((void)p)

static int		cmp_name(const void *, const void *);
static int		cmp_none(const void *, const void *);
static int		cmp_size(const void *, const void *);
static int		cmp_value(const void *, const void *);
static void		filter_dest(void);
static int		filter_insert(fn_filter);
static void		get_opt(int, char **);
static int		get_sym(Elf *, struct sym_head *, int, size_t, size_t,
			    const char *, const char **, int);
static const char *	get_sym_name(Elf *, const GElf_Sym *, size_t,
			    const char **, int);
static char		get_sym_type(const GElf_Sym *, const char *);
static void		global_dest(void);
static void		global_init(void);
static bool		is_sec_data(GElf_Shdr *);
static bool		is_sec_debug(const char *);
static bool		is_sec_nobits(GElf_Shdr *);
static bool		is_sec_readonly(GElf_Shdr *);
static bool		is_sec_text(GElf_Shdr *);
static void		print_ar_index(int, Elf *);
static void		print_header(const char *, const char *);
static void		print_version(void);
static int		read_elf(Elf *, const char *, Elf_Kind);
static int		read_object(const char *);
static int		read_files(int, char **);
static void		set_opt_value_print_fn(enum radix);
static int		sym_elem_def(char, const GElf_Sym *, const char *);
static int		sym_elem_global(char, const GElf_Sym *, const char *);
static int		sym_elem_global_static(char, const GElf_Sym *,
			    const char *);
static int		sym_elem_nondebug(char, const GElf_Sym *, const char *);
static int		sym_elem_nonzero_size(char, const GElf_Sym *,
			    const char *);
static void		sym_elem_print_all(char, const char *,
			    const GElf_Sym *, const char *);
static void		sym_elem_print_all_portable(char, const char *,
			    const GElf_Sym *, const char *);
static void		sym_elem_print_all_sysv(char, const char *,
			    const GElf_Sym *, const char *);
static int		sym_elem_undef(char, const GElf_Sym *, const char *);
static void		sym_list_dest(struct sym_head *);
static int		sym_list_insert(struct sym_head *, const char *,
			    const GElf_Sym *);
static void		sym_list_print(struct sym_print_data *,
			    struct func_info_head *, struct var_info_head *,
			    struct line_info_head *);
static void		sym_list_print_each(struct sym_entry *,
			    struct sym_print_data *, struct func_info_head *,
			    struct var_info_head *, struct line_info_head *);
static struct sym_entry	*sym_list_sort(struct sym_print_data *);
static void		sym_size_oct_print(const GElf_Sym *);
static void		sym_size_hex_print(const GElf_Sym *);
static void		sym_size_dec_print(const GElf_Sym *);
static void		sym_value_oct_print(const GElf_Sym *);
static void		sym_value_hex_print(const GElf_Sym *);
static void		sym_value_dec_print(const GElf_Sym *);
static void		usage(int);

static struct nm_prog_info	nm_info;
static struct nm_prog_options	nm_opts;
static int			nm_elfclass;

/*
 * Point to current sym_print_data to use portable qsort function.
 *  (e.g. There is no qsort_r function in NetBSD.)
 *
 * Using in sym_list_sort.
 */
static struct sym_print_data	*nm_print_data;

static const struct option nm_longopts[] = {
	{ "debug-syms",		no_argument,		NULL,		'a' },
	{ "defined-only",	no_argument,		&nm_opts.def_only, 1},
	{ "demangle",		optional_argument,	NULL,		'C' },
	{ "dynamic",		no_argument,		NULL,		'D' },
	{ "extern-only",	no_argument,		NULL,		'g' },
	{ "format",		required_argument,	NULL,		'F' },
	{ "help",		no_argument,		NULL,		'h' },
	{ "line-numbers",	no_argument,		NULL,		'l' },
	{ "no-demangle",	no_argument,		&nm_opts.no_demangle,
	  1},
	{ "no-sort",		no_argument,		NULL,		'p' },
	{ "numeric-sort",	no_argument,		NULL,		'v' },
	{ "print-armap",	no_argument,		NULL,		's' },
	{ "print-file-name",	no_argument,		NULL,		'A' },
	{ "print-size",		no_argument,		NULL,		'S' },
	{ "radix",		required_argument,	NULL,		't' },
	{ "reverse-sort",	no_argument,		NULL,		'r' },
	{ "size-sort",		no_argument,		&nm_opts.sort_size, 1},
	{ "undefined-only",	no_argument,		NULL,		'u' },
	{ "version",		no_argument,		NULL,		'V' },
	{ NULL,			0,			NULL,		0   }
};

#if defined(ELFTC_NEED_BYTEORDER_EXTENSIONS)
static __inline uint32_t
be32dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	return ((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

static __inline uint32_t
le32dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	return ((p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0]);
}

static __inline uint64_t
be64dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	return (((uint64_t)be32dec(p) << 32) | be32dec(p + 4));
}

static __inline uint64_t
le64dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	return (((uint64_t)le32dec(p + 4) << 32) | le32dec(p));
}
#endif

static int
cmp_name(const void *l, const void *r)
{

	assert(l != NULL);
	assert(r != NULL);
	assert(((const struct sym_entry *)l)->name != NULL);
	assert(((const struct sym_entry *)r)->name != NULL);

	return (strcmp(((const struct sym_entry *)l)->name,
	    ((const struct sym_entry *)r)->name));
}

static int
cmp_none(const void *l, const void *r)
{

	UNUSED(l);
	UNUSED(r);

	return (0);
}

/* Size comparison. If l and r have same size, compare their name. */
static int
cmp_size(const void *lp, const void *rp)
{
	const struct sym_entry *l, *r;

	l = lp;
	r = rp;

	assert(l != NULL);
	assert(l->name != NULL);
	assert(l->sym != NULL);
	assert(r != NULL);
	assert(r->name != NULL);
	assert(r->sym != NULL);

	if (l->sym->st_size == r->sym->st_size)
		return (strcmp(l->name, r->name));

	return (l->sym->st_size - r->sym->st_size);
}

/* Value comparison. Undefined symbols come first. */
static int
cmp_value(const void *lp, const void *rp)
{
	const struct sym_entry *l, *r;
	const char *ttable;
	int l_is_undef, r_is_undef;

	l = lp;
	r = rp;

	assert(nm_print_data != NULL);
	ttable = nm_print_data->t_table;

	assert(l != NULL);
	assert(l->name != NULL);
	assert(l->sym != NULL);
	assert(r != NULL);
	assert(r->name != NULL);
	assert(r->sym != NULL);
	assert(ttable != NULL);

	l_is_undef = IS_UNDEF_SYM_TYPE(get_sym_type(l->sym, ttable)) ? 1 : 0;
	r_is_undef = IS_UNDEF_SYM_TYPE(get_sym_type(r->sym, ttable)) ? 1 : 0;

	assert(l_is_undef + r_is_undef >= 0);
	assert(l_is_undef + r_is_undef <= 2);

	switch (l_is_undef + r_is_undef) {
	case 0:
		/* Both defined */
		if (l->sym->st_value == r->sym->st_value)
			return (strcmp(l->name, r->name));
		return (l->sym->st_value > r->sym->st_value ? 1 : -1);
	case 1:
		/* One undefined */
		return (l_is_undef == 0 ? 1 : -1);
	case 2:
		/* Both undefined */
		return (strcmp(l->name, r->name));
	}
	/* NOTREACHED */

	return (l->sym->st_value - r->sym->st_value);
}

static void
filter_dest(void)
{
	struct filter_entry *e;

	while (!SLIST_EMPTY(&nm_out_filter)) {
		e = SLIST_FIRST(&nm_out_filter);
		SLIST_REMOVE_HEAD(&nm_out_filter, filter_entries);
		free(e);
	}
}

static int
filter_insert(fn_filter filter_fn)
{
	struct filter_entry *e;

	assert(filter_fn != NULL);

	if ((e = malloc(sizeof(struct filter_entry))) == NULL) {
		warn("malloc");
		return (0);
	}
	e->fn = filter_fn;
	SLIST_INSERT_HEAD(&nm_out_filter, e, filter_entries);

	return (1);
}

static int
parse_demangle_option(const char *opt)
{

	if (opt == NULL)
		return (ELFTC_DEM_UNKNOWN);
	else if (!strncasecmp(opt, "gnu-v2", 6))
		return (ELFTC_DEM_GNU2);
	else if (!strncasecmp(opt, "gnu-v3", 6))
		return (ELFTC_DEM_GNU3);
	else if (!strncasecmp(opt, "arm", 3))
		return (ELFTC_DEM_ARM);
	else
		errx(EXIT_FAILURE, "unknown demangling style '%s'", opt);

	/* NOTREACHED */
	return (0);
}

static void
get_opt(int argc, char **argv)
{
	int ch;
	bool is_posix, oflag;

	if (argc <= 0 || argv == NULL)
		return;

	oflag = is_posix = false;
	nm_opts.t = RADIX_HEX;
	while ((ch = getopt_long(argc, argv, "ABCDF:PSVaefghlnoprst:uvx",
		    nm_longopts, NULL)) != -1) {
		switch (ch) {
		case 'A':
			nm_opts.print_name = PRINT_NAME_FULL;
			break;
		case 'B':
			nm_opts.elem_print_fn = &sym_elem_print_all;
			break;
		case 'C':
			nm_opts.demangle_type = parse_demangle_option(optarg);
			break;
		case 'D':
			nm_opts.print_symbol = PRINT_SYM_DYN;
			break;
		case 'F':
			/* sysv, bsd, posix */
			switch (optarg[0]) {
			case 'B':
			case 'b':
				nm_opts.elem_print_fn = &sym_elem_print_all;
				break;
			case 'P':
			case 'p':
				is_posix = true;
				nm_opts.elem_print_fn =
				    &sym_elem_print_all_portable;
				break;
			case 'S':
			case 's':
				nm_opts.elem_print_fn =
				    &sym_elem_print_all_sysv;
				break;
			default:
				warnx("%s: Invalid format", optarg);
				usage(1);
			}

			break;
		case 'P':
			is_posix = true;
			nm_opts.elem_print_fn = &sym_elem_print_all_portable;
			break;
		case 'S':
			nm_opts.print_size = 1;
			break;
		case 'V':
			print_version();
			/* NOTREACHED */
		case 'a':
			nm_opts.print_debug = true;
			break;
		case 'e':
			filter_insert(sym_elem_global_static);
			break;
		case 'f':
			break;
		case 'g':
			filter_insert(sym_elem_global);
			break;
		case 'h':
			usage(0);
			break;
		case 'l':
			nm_opts.debug_line = true;
			break;
		case 'n':
		case 'v':
			nm_opts.sort_fn = &cmp_value;
			break;
		case 'o':
			oflag = true;
			break;
		case 'p':
			nm_opts.sort_fn = &cmp_none;
			break;
		case 'r':
			nm_opts.sort_reverse = true;
			break;
		case 's':
			nm_opts.print_armap = true;
			break;
		case 't':
			/* t require always argument to getopt_long */
			switch (optarg[0]) {
			case 'd':
				nm_opts.t = RADIX_DEC;
				break;
			case 'o':
				nm_opts.t = RADIX_OCT;
				break;
			case 'x':
				nm_opts.t = RADIX_HEX;
				break;
			default:
				warnx("%s: Invalid radix", optarg);
				usage(1);
			}
			break;
		case 'u':
			filter_insert(sym_elem_undef);
			nm_opts.undef_only = true;
			break;
		/* case 'v': see case 'n' above. */
		case 'x':
			nm_opts.t = RADIX_HEX;
			break;
		case 0:
			if (nm_opts.sort_size != 0) {
				nm_opts.sort_fn = &cmp_size;
				filter_insert(sym_elem_def);
				filter_insert(sym_elem_nonzero_size);
			}
			if (nm_opts.def_only != 0)
				filter_insert(sym_elem_def);
			if (nm_opts.no_demangle != 0)
				nm_opts.demangle_type = -1;
			break;
		default :
			usage(1);
		}
	}

	/*
	 * In POSIX mode, the '-o' option controls the output radix.
	 * In non-POSIX mode, the option is a synonym for the '-A' and
	 * '--print-file-name' options.
	 */
	if (oflag) {
		if (is_posix)
			nm_opts.t = RADIX_OCT;
		else
			nm_opts.print_name = PRINT_NAME_FULL;
	}

	assert(nm_opts.sort_fn != NULL && "nm_opts.sort_fn is null");
	assert(nm_opts.elem_print_fn != NULL &&
	    "nm_opts.elem_print_fn is null");
	assert(nm_opts.value_print_fn != NULL &&
	    "nm_opts.value_print_fn is null");

	set_opt_value_print_fn(nm_opts.t);

	if (nm_opts.undef_only == true) {
		if (nm_opts.sort_fn == &cmp_size)
			errx(EXIT_FAILURE,
			    "--size-sort with -u is meaningless");
		if (nm_opts.def_only != 0)
			errx(EXIT_FAILURE,
			    "-u with --defined-only is meaningless");
	}
	if (nm_opts.print_debug == false)
		filter_insert(sym_elem_nondebug);
	if (nm_opts.sort_reverse == true && nm_opts.sort_fn == cmp_none)
		nm_opts.sort_reverse = false;
}

/*
 * Get symbol information from elf.
 */
static int
get_sym(Elf *elf, struct sym_head *headp, int shnum, size_t dynndx,
    size_t strndx, const char *type_table, const char **sec_table,
    int sec_table_size)
{
	Elf_Scn *scn;
	Elf_Data *data;
	GElf_Shdr shdr;
	GElf_Sym sym;
	struct filter_entry *fep;
	size_t ndx;
	int rtn;
	const char *sym_name;
	char type;
	bool filter;
	int i, j;

	assert(elf != NULL);
	assert(headp != NULL);

	rtn = 0;
	for (i = 1; i < shnum; i++) {
		if ((scn = elf_getscn(elf, i)) == NULL) {
			warnx("elf_getscn failed: %s", elf_errmsg(-1));
			continue;
		}
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			warnx("gelf_getshdr failed: %s", elf_errmsg(-1));
			continue;
		}
		if (shdr.sh_type == SHT_SYMTAB) {
			if (nm_opts.print_symbol != PRINT_SYM_SYM)
				continue;
		} else if (shdr.sh_type == SHT_DYNSYM) {
			if (nm_opts.print_symbol != PRINT_SYM_DYN)
				continue;
		} else
			continue;

		ndx = shdr.sh_type == SHT_DYNSYM ? dynndx : strndx;

		data = NULL;
		while ((data = elf_getdata(scn, data)) != NULL) {
			j = 1;
			while (gelf_getsym(data, j++, &sym) != NULL) {
				sym_name = get_sym_name(elf, &sym, ndx,
				    sec_table, sec_table_size);
				filter = false;
				type = get_sym_type(&sym, type_table);
				SLIST_FOREACH(fep, &nm_out_filter,
				    filter_entries) {
					if (!fep->fn(type, &sym, sym_name)) {
						filter = true;
						break;
					}
				}
				if (filter == false) {
					if (sym_list_insert(headp, sym_name,
					    &sym) == 0)
						return (0);
					rtn++;
				}
			}
		}
	}

	return (rtn);
}

static const char *
get_sym_name(Elf *elf, const GElf_Sym *sym, size_t ndx, const char **sec_table,
    int sec_table_size)
{
	const char *sym_name;

	sym_name = NULL;

	/* Show section name as symbol name for STT_SECTION symbols. */
	if (GELF_ST_TYPE(sym->st_info) == STT_SECTION) {
		if (sec_table != NULL && sym->st_shndx < sec_table_size)
			sym_name = sec_table[sym->st_shndx];
	} else
		sym_name = elf_strptr(elf, ndx, sym->st_name);

	if (sym_name == NULL)
		sym_name = "(null)";

	return (sym_name);
}

static char
get_sym_type(const GElf_Sym *sym, const char *type_table)
{
	bool is_local;

	if (sym == NULL || type_table == NULL)
		return ('?');

	is_local = sym->st_info >> 4 == STB_LOCAL;

	if (sym->st_shndx == SHN_ABS) /* absolute */
		return (is_local ? 'a' : 'A');

	if (sym->st_shndx == SHN_COMMON) /* common */
		return ('C');

	if ((sym->st_info) >> 4 == STB_WEAK) { /* weak */
		if ((sym->st_info & 0xf) == STT_OBJECT)
			return (sym->st_shndx == SHN_UNDEF ? 'v' : 'V');

		return (sym->st_shndx == SHN_UNDEF ? 'w' : 'W');
	}

	if (sym->st_shndx == SHN_UNDEF) /* undefined */
		return ('U');

	return (is_local == true && type_table[sym->st_shndx] != 'N' ?
	    tolower((unsigned char) type_table[sym->st_shndx]) :
	    type_table[sym->st_shndx]);
}

static void
global_dest(void)
{

	filter_dest();
}

static void
global_init(void)
{

	if (elf_version(EV_CURRENT) == EV_NONE)
		errx(EXIT_FAILURE, "elf_version error");

	nm_info.name = ELFTC_GETPROGNAME();
	nm_info.def_filename = "a.out";
	nm_opts.print_symbol = PRINT_SYM_SYM;
	nm_opts.print_name = PRINT_NAME_NONE;
	nm_opts.demangle_type = -1;
	nm_opts.print_debug = false;
	nm_opts.print_armap = false;
	nm_opts.print_size = 0;
	nm_opts.debug_line = false;
	nm_opts.def_only = 0;
	nm_opts.undef_only = false;
	nm_opts.sort_size = 0;
	nm_opts.sort_reverse = false;
	nm_opts.no_demangle = 0;
	nm_opts.sort_fn = &cmp_name;
	nm_opts.elem_print_fn = &sym_elem_print_all;
	nm_opts.value_print_fn = &sym_value_dec_print;
	nm_opts.size_print_fn = &sym_size_dec_print;
	SLIST_INIT(&nm_out_filter);
}

static bool
is_sec_data(GElf_Shdr *s)
{

	assert(s != NULL && "shdr is NULL");

	return (((s->sh_flags & SHF_ALLOC) != 0) && s->sh_type != SHT_NOBITS);
}

static bool
is_sec_debug(const char *shname)
{
	const char *dbg_sec[] = {
		".debug",
		".gnu.linkonce.wi.",
		".line",
		".rel.debug",
		".rela.debug",
		".stab",
		NULL
	};
	const char **p;

	if (shname == NULL)
		return (false);

	for (p = dbg_sec; *p; p++) {
		if (!strncmp(shname, *p, strlen(*p)))
			return (true);
	}

	return (false);
}

static bool
is_sec_nobits(GElf_Shdr *s)
{

	assert(s != NULL && "shdr is NULL");

	return (s->sh_type == SHT_NOBITS);
}

static bool
is_sec_readonly(GElf_Shdr *s)
{

	assert(s != NULL && "shdr is NULL");

	return ((s->sh_flags & SHF_WRITE) == 0);
}

static bool
is_sec_text(GElf_Shdr *s)
{

	assert(s != NULL && "shdr is NULL");

	return ((s->sh_flags & SHF_EXECINSTR) != 0);
}

static void
print_ar_index(int fd, Elf *arf)
{
	Elf *elf;
	Elf_Arhdr *arhdr;
	Elf_Arsym *arsym;
	Elf_Cmd cmd;
	off_t start;
	size_t arsym_size;

	if (arf == NULL)
		return;

	if ((arsym = elf_getarsym(arf, &arsym_size)) == NULL)
		return;

	printf("\nArchive index:\n");

	start = arsym->as_off;
	cmd = ELF_C_READ;
	while (arsym_size > 1) {
		if (elf_rand(arf, arsym->as_off) == arsym->as_off &&
		    (elf = elf_begin(fd, cmd, arf)) != NULL) {
			if ((arhdr = elf_getarhdr(elf)) != NULL)
				printf("%s in %s\n", arsym->as_name,
				    arhdr->ar_name != NULL ?
				    arhdr->ar_name : arhdr->ar_rawname);
			elf_end(elf);
		}
		++arsym;
		--arsym_size;
	}

	elf_rand(arf, start);
}

#define	DEMANGLED_BUFFER_SIZE	(8 * 1024)
#define	PRINT_DEMANGLED_NAME(FORMAT, NAME) do {				\
	char _demangled[DEMANGLED_BUFFER_SIZE];				\
	if (nm_opts.demangle_type < 0 ||				\
	    elftc_demangle((NAME), _demangled, sizeof(_demangled),	\
		nm_opts.demangle_type) < 0)				\
		printf((FORMAT), (NAME));				\
	else								\
		printf((FORMAT), _demangled);				\
	} while (0)

static void
print_header(const char *file, const char *obj)
{

	if (file == NULL)
		return;

	if (nm_opts.elem_print_fn == &sym_elem_print_all_sysv) {
		printf("\n\n%s from %s",
		    nm_opts.undef_only == false ? "Symbols" :
		    "Undefined symbols", file);
		if (obj != NULL)
			printf("[%s]", obj);
		printf(":\n\n");

		printf("\
Name                  Value           Class        Type         Size             Line  Section\n\n");
	} else {
		/* archive file without -A option and POSIX */
		if (nm_opts.print_name != PRINT_NAME_FULL && obj != NULL) {
			if (nm_opts.elem_print_fn ==
			    sym_elem_print_all_portable)
				printf("%s[%s]:\n", file, obj);
			else if (nm_opts.elem_print_fn == sym_elem_print_all)
				printf("\n%s:\n", obj);
			/* multiple files(not archive) without -A option */
		} else if (nm_opts.print_name == PRINT_NAME_MULTI) {
			if (nm_opts.elem_print_fn == sym_elem_print_all)
				printf("\n");
			printf("%s:\n", file);
		}
	}
}

static void
print_version(void)
{

	(void) printf("%s (%s)\n", nm_info.name, elftc_version());
	exit(0);
}

static uint64_t
get_block_value(Dwarf_Debug dbg, Dwarf_Block *block)
{
	Elf *elf;
	GElf_Ehdr eh;
	Dwarf_Error de;

	if (dwarf_get_elf(dbg, &elf, &de) != DW_DLV_OK) {
		warnx("dwarf_get_elf failed: %s", dwarf_errmsg(de));
		return (0);
	}

	if (gelf_getehdr(elf, &eh) != &eh) {
		warnx("gelf_getehdr failed: %s", elf_errmsg(-1));
		return (0);
	}

	if (block->bl_len == 5) {
		if (eh.e_ident[EI_DATA] == ELFDATA2LSB)
			return (le32dec((uint8_t *) block->bl_data + 1));
		else
			return (be32dec((uint8_t *) block->bl_data + 1));
	} else if (block->bl_len == 9) {
		if (eh.e_ident[EI_DATA] == ELFDATA2LSB)
			return (le64dec((uint8_t *) block->bl_data + 1));
		else
			return (be64dec((uint8_t *) block->bl_data + 1));
	}

	return (0);
}

static char *
find_object_name(Dwarf_Debug dbg, Dwarf_Die die)
{
	Dwarf_Die ret_die;
	Dwarf_Attribute at;
	Dwarf_Off off;
	Dwarf_Error de;
	const char *str;
	char *name;

	if (dwarf_attrval_string(die, DW_AT_name, &str, &de) == DW_DLV_OK) {
		if ((name = strdup(str)) == NULL) {
			warn("strdup");
			return (NULL);
		}
		return (name);
	}

	if (dwarf_attr(die, DW_AT_specification, &at, &de) != DW_DLV_OK)
		return (NULL);

	if (dwarf_global_formref(at, &off, &de) != DW_DLV_OK)
		return (NULL);

	if (dwarf_offdie(dbg, off, &ret_die, &de) != DW_DLV_OK)
		return (NULL);

	return (find_object_name(dbg, ret_die));
}

static void
search_line_attr(Dwarf_Debug dbg, struct func_info_head *func_info,
    struct var_info_head *var_info, Dwarf_Die die, char **src_files,
    Dwarf_Signed filecount)
{
	Dwarf_Attribute at;
	Dwarf_Unsigned udata;
	Dwarf_Half tag;
	Dwarf_Block *block;
	Dwarf_Bool flag;
	Dwarf_Die ret_die;
	Dwarf_Error de;
	struct func_info_entry *func;
	struct var_info_entry *var;
	int ret;

	if (dwarf_tag(die, &tag, &de) != DW_DLV_OK) {
		warnx("dwarf_tag failed: %s", dwarf_errmsg(de));
		goto cont_search;
	}

	/* We're interested in DIEs which define functions or variables. */
	if (tag != DW_TAG_subprogram && tag != DW_TAG_entry_point &&
	    tag != DW_TAG_inlined_subroutine && tag != DW_TAG_variable)
		goto cont_search;

	if (tag == DW_TAG_variable) {

		/* Ignore "artificial" variable. */
		if (dwarf_attrval_flag(die, DW_AT_artificial, &flag, &de) ==
		    DW_DLV_OK && flag)
			goto cont_search;

		/* Ignore pure declaration. */
		if (dwarf_attrval_flag(die, DW_AT_declaration, &flag, &de) ==
		    DW_DLV_OK && flag)
			goto cont_search;

		/* Ignore stack varaibles. */
		if (dwarf_attrval_flag(die, DW_AT_external, &flag, &de) !=
		    DW_DLV_OK || !flag)
			goto cont_search;

		if ((var = calloc(1, sizeof(*var))) == NULL) {
			warn("calloc failed");
			goto cont_search;
		}

		if (dwarf_attrval_unsigned(die, DW_AT_decl_file, &udata,
		    &de) == DW_DLV_OK && udata > 0 &&
		    (Dwarf_Signed) (udata - 1) < filecount) {
			var->file = strdup(src_files[udata - 1]);
			if (var->file == NULL) {
				warn("strdup");
				free(var);
				goto cont_search;
			}
		}

		if (dwarf_attrval_unsigned(die, DW_AT_decl_line, &udata, &de) ==
		    DW_DLV_OK)
			var->line = udata;

		var->name = find_object_name(dbg, die);
		if (var->name == NULL) {
			if (var->file)
				free(var->file);
			free(var);
			goto cont_search;
		}

		if (dwarf_attr(die, DW_AT_location, &at, &de) == DW_DLV_OK &&
		    dwarf_formblock(at, &block, &de) == DW_DLV_OK) {
			/*
			 * Since we ignored stack variables, the rest are the
			 * external varaibles which should always use DW_OP_addr
			 * operator for DW_AT_location value.
			 */
			if (*((uint8_t *)block->bl_data) == DW_OP_addr)
				var->addr = get_block_value(dbg, block);
		}

		SLIST_INSERT_HEAD(var_info, var, entries);

	} else {

		if ((func = calloc(1, sizeof(*func))) == NULL) {
			warn("calloc failed");
			goto cont_search;
		}

		/*
		 * Note that dwarf_attrval_unsigned() handles DW_AT_abstract_origin
		 * internally, so it can retrieve DW_AT_decl_file/DW_AT_decl_line
		 * attributes for inlined functions as well.
		 */
		if (dwarf_attrval_unsigned(die, DW_AT_decl_file, &udata,
		    &de) == DW_DLV_OK && udata > 0 &&
		    (Dwarf_Signed) (udata - 1) < filecount) {
			func->file = strdup(src_files[udata - 1]);
			if (func->file == NULL) {
				warn("strdup");
				free(func);
				goto cont_search;
			}
		}

		if (dwarf_attrval_unsigned(die, DW_AT_decl_line, &udata, &de) ==
		    DW_DLV_OK)
			func->line = udata;

		func->name = find_object_name(dbg, die);
		if (func->name == NULL) {
			if (func->file)
				free(func->file);
			free(func);
			goto cont_search;
		}

		if (dwarf_attrval_unsigned(die, DW_AT_low_pc, &udata, &de) ==
		    DW_DLV_OK)
			func->lowpc = udata;
		if (dwarf_attrval_unsigned(die, DW_AT_high_pc, &udata, &de) ==
		    DW_DLV_OK)
			func->highpc = udata;

		SLIST_INSERT_HEAD(func_info, func, entries);
	}

cont_search:

	/* Search children. */
	ret = dwarf_child(die, &ret_die, &de);
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_child: %s", dwarf_errmsg(de));
	else if (ret == DW_DLV_OK)
		search_line_attr(dbg, func_info, var_info, ret_die, src_files,
		    filecount);

	/* Search sibling. */
	ret = dwarf_siblingof(dbg, die, &ret_die, &de);
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_siblingof: %s", dwarf_errmsg(de));
	else if (ret == DW_DLV_OK)
		search_line_attr(dbg, func_info, var_info, ret_die, src_files,
		    filecount);

	dwarf_dealloc(dbg, die, DW_DLA_DIE);
}

/*
 * Read elf file and collect symbol information, sort them, print.
 * Return 1 at failed, 0 at success.
 */
static int
read_elf(Elf *elf, const char *filename, Elf_Kind kind)
{
	Dwarf_Debug dbg;
	Dwarf_Die die;
	Dwarf_Error de;
	Dwarf_Half tag;
	Elf_Arhdr *arhdr;
	Elf_Scn *scn;
	GElf_Shdr shdr;
	GElf_Half i;
	Dwarf_Line *lbuf;
	Dwarf_Unsigned lineno;
	Dwarf_Signed lcount, filecount;
	Dwarf_Addr lineaddr;
	struct sym_print_data p_data;
	struct sym_head list_head;
	struct line_info_head *line_info;
	struct func_info_head *func_info;
	struct var_info_head *var_info;
	struct line_info_entry *lie;
	struct func_info_entry *func;
	struct var_info_entry *var;
	const char *shname, *objname;
	char *type_table, **sec_table, *sfile, **src_files;
	size_t shstrndx, shnum, dynndx, strndx;
	int ret, rtn, e_err;

#define	OBJNAME	(objname == NULL ? filename : objname)

	assert(filename != NULL && "filename is null");

	STAILQ_INIT(&list_head);
	type_table = NULL;
	sec_table = NULL;
	line_info = NULL;
	func_info = NULL;
	var_info = NULL;
	objname = NULL;
	dynndx = SHN_UNDEF;
	strndx = SHN_UNDEF;
	rtn = 0;

	nm_elfclass = gelf_getclass(elf);

	if (kind == ELF_K_AR) {
		if ((arhdr = elf_getarhdr(elf)) == NULL)
			goto next_cmd;
		objname = arhdr->ar_name != NULL ? arhdr->ar_name :
		    arhdr->ar_rawname;
	}
	if (!elf_getshnum(elf, &shnum)) {
		if ((e_err = elf_errno()) != 0)
			warnx("%s: %s", OBJNAME, "File format not recognized");
		else
			warnx("%s: cannot get section number", OBJNAME);
		rtn = 1;
		goto next_cmd;
	}
	if (shnum == 0) {
		warnx("%s: has no section", OBJNAME);
		rtn = 1;
		goto next_cmd;
	}
	if (!elf_getshstrndx(elf, &shstrndx)) {
		warnx("%s: cannot get str index", OBJNAME);
		rtn = 1;
		goto next_cmd;
	}
	/* type_table for type determine */
	if ((type_table = malloc(sizeof(char) * shnum)) == NULL) {
		warn("%s: malloc", OBJNAME);
		rtn = 1;
		goto next_cmd;
	}
	/* sec_table for section name to display in sysv format */
	if ((sec_table = calloc(shnum, sizeof(char *))) == NULL) {
		warn("%s: calloc", OBJNAME);
		rtn = 1;
		goto next_cmd;
	}

	type_table[0] = 'U';
	if ((sec_table[0] = strdup("*UND*")) == NULL) {
		warn("strdup");
		goto next_cmd;
	}

	for (i = 1; i < shnum; ++i) {
		type_table[i] = 'U';
		if ((scn = elf_getscn(elf, i)) == NULL) {
			if ((e_err = elf_errno()) != 0)
				warnx("%s: %s", OBJNAME, elf_errmsg(e_err));
			else
				warnx("%s: cannot get section", OBJNAME);
			rtn = 1;
			goto next_cmd;
		}
		if (gelf_getshdr(scn, &shdr) == NULL)
			goto next_cmd;

		/*
		 * Cannot test by type and attribute for dynstr, strtab
		 */
		shname = elf_strptr(elf, shstrndx, (size_t) shdr.sh_name);
		if (shname != NULL) {
			if ((sec_table[i] = strdup(shname)) == NULL) {
				warn("strdup");
				goto next_cmd;
			}
			if (!strncmp(shname, ".dynstr", 7)) {
				dynndx = elf_ndxscn(scn);
				if (dynndx == SHN_UNDEF) {
					warnx("%s: elf_ndxscn failed: %s",
					    OBJNAME, elf_errmsg(-1));
					goto next_cmd;
				}
			}
			if (!strncmp(shname, ".strtab", 7)) {
				strndx = elf_ndxscn(scn);
				if (strndx == SHN_UNDEF) {
					warnx("%s: elf_ndxscn failed: %s",
					    OBJNAME, elf_errmsg(-1));
					goto next_cmd;
				}					
			}
		} else {
			sec_table[i] = strdup("*UND*");
			if (sec_table[i] == NULL) {
				warn("strdup");
				goto next_cmd;
			}
		}


		if (is_sec_text(&shdr))
			type_table[i] = 'T';
		else if (is_sec_data(&shdr)) {
			if (is_sec_readonly(&shdr))
				type_table[i] = 'R';
			else
				type_table[i] = 'D';
		} else if (is_sec_nobits(&shdr))
			type_table[i] = 'B';
		else if (is_sec_debug(shname))
			type_table[i] = 'N';
		else if (is_sec_readonly(&shdr) && !is_sec_nobits(&shdr))
			type_table[i] = 'n';
	}

	print_header(filename, objname);

	if ((dynndx == SHN_UNDEF && nm_opts.print_symbol == PRINT_SYM_DYN) ||
	    (strndx == SHN_UNDEF && nm_opts.print_symbol == PRINT_SYM_SYM)) {
		warnx("%s: no symbols", OBJNAME);
		/* This is not an error case */
		goto next_cmd;
	}

	STAILQ_INIT(&list_head);

	if (!nm_opts.debug_line)
		goto process_sym;

	/*
	 * Collect dwarf line number information.
	 */

	if (dwarf_elf_init(elf, DW_DLC_READ, NULL, NULL, &dbg, &de) !=
	    DW_DLV_OK) {
		warnx("dwarf_elf_init failed: %s", dwarf_errmsg(de));
		goto process_sym;
	}

	line_info = malloc(sizeof(struct line_info_head));
	func_info = malloc(sizeof(struct func_info_head));
	var_info = malloc(sizeof(struct var_info_head));
	if (line_info != NULL)
		SLIST_INIT(line_info);
	if (func_info != NULL)
		SLIST_INIT(func_info);
	if (var_info != NULL)
		SLIST_INIT(var_info);
	if (line_info == NULL || func_info == NULL || var_info == NULL) {
		warn("malloc");
		(void) dwarf_finish(dbg, &de);
		goto process_sym;
	}

	while ((ret = dwarf_next_cu_header(dbg, NULL, NULL, NULL, NULL, NULL,
	    &de)) ==  DW_DLV_OK) {
		die = NULL;
		while (dwarf_siblingof(dbg, die, &die, &de) == DW_DLV_OK) {
			if (dwarf_tag(die, &tag, &de) != DW_DLV_OK) {
				warnx("dwarf_tag failed: %s",
				    dwarf_errmsg(de));
				continue;
			}
			/* XXX: What about DW_TAG_partial_unit? */
			if (tag == DW_TAG_compile_unit)
				break;
		}
		if (die == NULL) {
			warnx("could not find DW_TAG_compile_unit die");
			continue;
		}

		/* Retrieve source file list. */
		ret = dwarf_srcfiles(die, &src_files, &filecount, &de);
		if (ret == DW_DLV_ERROR)
			warnx("dwarf_srclines: %s", dwarf_errmsg(de));
		if (ret != DW_DLV_OK)
			continue;

		/*
		 * Retrieve line number information from .debug_line section.
		 */

		ret = dwarf_srclines(die, &lbuf, &lcount, &de);
		if (ret == DW_DLV_ERROR)
			warnx("dwarf_srclines: %s", dwarf_errmsg(de));
		if (ret != DW_DLV_OK)
			goto line_attr;
		for (i = 0; (Dwarf_Signed) i < lcount; i++) {
			if (dwarf_lineaddr(lbuf[i], &lineaddr, &de)) {
				warnx("dwarf_lineaddr: %s", dwarf_errmsg(de));
				continue;
			}
			if (dwarf_lineno(lbuf[i], &lineno, &de)) {
				warnx("dwarf_lineno: %s", dwarf_errmsg(de));
				continue;
			}
			if (dwarf_linesrc(lbuf[i], &sfile, &de)) {
				warnx("dwarf_linesrc: %s", dwarf_errmsg(de));
				continue;
			}
			if ((lie = malloc(sizeof(*lie))) == NULL) {
				warn("malloc");
				continue;
			}
			lie->addr = lineaddr;
			lie->line = lineno;
			lie->file = strdup(sfile);
			if (lie->file == NULL) {
				warn("strdup");
				free(lie);
				continue;
			}
			SLIST_INSERT_HEAD(line_info, lie, entries);
		}

	line_attr:
		/* Retrieve line number information from DIEs. */
		search_line_attr(dbg, func_info, var_info, die, src_files, filecount);
	}

	(void) dwarf_finish(dbg, &de);

process_sym:

	p_data.list_num = get_sym(elf, &list_head, shnum, dynndx, strndx,
	    type_table, (void *) sec_table, shnum);

	if (p_data.list_num == 0)
		goto next_cmd;

	p_data.headp = &list_head;
	p_data.sh_num = shnum;
	p_data.t_table = type_table;
	p_data.s_table = (void *) sec_table;
	p_data.filename = filename;
	p_data.objname = objname;

	sym_list_print(&p_data, func_info, var_info, line_info);

next_cmd:
	if (nm_opts.debug_line) {
		if (func_info != NULL) {
			while (!SLIST_EMPTY(func_info)) {
				func = SLIST_FIRST(func_info);
				SLIST_REMOVE_HEAD(func_info, entries);
				free(func->file);
				free(func->name);
				free(func);
			}
			free(func_info);
			func_info = NULL;
		}
		if (var_info != NULL) {
			while (!SLIST_EMPTY(var_info)) {
				var = SLIST_FIRST(var_info);
				SLIST_REMOVE_HEAD(var_info, entries);
				free(var->file);
				free(var->name);
				free(var);
			}
			free(var_info);
			var_info = NULL;
		}
		if (line_info != NULL) {
			while (!SLIST_EMPTY(line_info)) {
				lie = SLIST_FIRST(line_info);
				SLIST_REMOVE_HEAD(line_info, entries);
				free(lie->file);
				free(lie);
			}
			free(line_info);
			line_info = NULL;
		}
	}

	if (sec_table != NULL)
		for (i = 0; i < shnum; ++i)
			free(sec_table[i]);
	free(sec_table);
	free(type_table);

	sym_list_dest(&list_head);

	return (rtn);

#undef	OBJNAME
}

static int
read_object(const char *filename)
{
	Elf *elf, *arf;
	Elf_Cmd elf_cmd;
	Elf_Kind kind;
	int fd, rtn, e_err;

	assert(filename != NULL && "filename is null");

	if ((fd = open(filename, O_RDONLY)) == -1) {
		warn("'%s'", filename);
		return (1);
	}

	elf_cmd = ELF_C_READ;
	if ((arf = elf_begin(fd, elf_cmd, (Elf *) NULL)) == NULL) {
		if ((e_err = elf_errno()) != 0)
			warnx("elf_begin error: %s", elf_errmsg(e_err));
		else
			warnx("elf_begin error");
		close(fd);
		return (1);
	}

	assert(arf != NULL && "arf is null.");

	rtn = 0;
	if ((kind = elf_kind(arf)) == ELF_K_NONE) {
		warnx("%s: File format not recognized", filename);
		elf_end(arf);
		close(fd);
		return (1);
	}
	if (kind == ELF_K_AR) {
		if (nm_opts.print_name == PRINT_NAME_MULTI &&
		    nm_opts.elem_print_fn == sym_elem_print_all)
			printf("\n%s:\n", filename);
		if (nm_opts.print_armap == true)
			print_ar_index(fd, arf);
	}

	while ((elf = elf_begin(fd, elf_cmd, arf)) != NULL) {
		rtn |= read_elf(elf, filename, kind);

		/*
		 * If file is not archive, elf_next return ELF_C_NULL and
		 * stop the loop.
		 */
		elf_cmd = elf_next(elf);
		elf_end(elf);
	}

	elf_end(arf);
	close(fd);

	return (rtn);
}

static int
read_files(int argc, char **argv)
{
	int rtn = 0;

	if (argc < 0 || argv == NULL)
		return (1);

	if (argc == 0)
		rtn |= read_object(nm_info.def_filename);
	else {
		if (nm_opts.print_name == PRINT_NAME_NONE && argc > 1)
			nm_opts.print_name = PRINT_NAME_MULTI;
		while (argc > 0) {
			rtn |= read_object(*argv);
			--argc;
			++argv;
		}
	}

	return (rtn);
}

static void
print_lineno(struct sym_entry *ep, struct func_info_head *func_info,
    struct var_info_head *var_info, struct line_info_head *line_info)
{
	struct func_info_entry *func;
	struct var_info_entry *var;
	struct line_info_entry *lie;

	/* For function symbol, search the function line information list.  */
	if ((ep->sym->st_info & 0xf) == STT_FUNC && func_info != NULL) {
		SLIST_FOREACH(func, func_info, entries) {
			if (func->name != NULL &&
			    !strcmp(ep->name, func->name) &&
			    ep->sym->st_value >= func->lowpc &&
			    ep->sym->st_value < func->highpc) {
				printf("\t%s:%" PRIu64, func->file, func->line);
				return;
			}
		}
	}

	/* For variable symbol, search the variable line information list.  */
	if ((ep->sym->st_info & 0xf) == STT_OBJECT && var_info != NULL) {
		SLIST_FOREACH(var, var_info, entries) {
			if (!strcmp(ep->name, var->name) &&
			    ep->sym->st_value == var->addr) {
				printf("\t%s:%" PRIu64, var->file, var->line);
				return;
			}
		}
	}

	/* Otherwise search line number information the .debug_line section. */
	if (line_info != NULL) {
		SLIST_FOREACH(lie, line_info, entries) {
			if (ep->sym->st_value == lie->addr) {
				printf("\t%s:%" PRIu64, lie->file, lie->line);
				return;
			}
		}
	}
}

static void
set_opt_value_print_fn(enum radix t)
{

	switch (t) {
	case RADIX_OCT:
		nm_opts.value_print_fn = &sym_value_oct_print;
		nm_opts.size_print_fn = &sym_size_oct_print;

		break;
	case RADIX_DEC:
		nm_opts.value_print_fn = &sym_value_dec_print;
		nm_opts.size_print_fn = &sym_size_dec_print;

		break;
	case RADIX_HEX:
	default :
		nm_opts.value_print_fn = &sym_value_hex_print;
		nm_opts.size_print_fn  = &sym_size_hex_print;
	}

	assert(nm_opts.value_print_fn != NULL &&
	    "nm_opts.value_print_fn is null");
}

static void
sym_elem_print_all(char type, const char *sec, const GElf_Sym *sym,
    const char *name)
{

	if (sec == NULL || sym == NULL || name == NULL ||
	    nm_opts.value_print_fn == NULL)
		return;

	if (IS_UNDEF_SYM_TYPE(type)) {
		if (nm_opts.t == RADIX_HEX && nm_elfclass == ELFCLASS32)
			printf("%-8s", "");
		else
			printf("%-16s", "");
	} else {
		switch ((nm_opts.sort_fn == & cmp_size ? 2 : 0) +
		    nm_opts.print_size) {
		case 3:
			if (sym->st_size != 0) {
				nm_opts.value_print_fn(sym);
				printf(" ");
				nm_opts.size_print_fn(sym);
			}
			break;

		case 2:
			if (sym->st_size != 0)
				nm_opts.size_print_fn(sym);
			break;

		case 1:
			nm_opts.value_print_fn(sym);
			if (sym->st_size != 0) {
				printf(" ");
				nm_opts.size_print_fn(sym);
			}
			break;

		case 0:
		default:
			nm_opts.value_print_fn(sym);
		}
	}

	printf(" %c ", type);
	PRINT_DEMANGLED_NAME("%s", name);
}

static void
sym_elem_print_all_portable(char type, const char *sec, const GElf_Sym *sym,
    const char *name)
{

	if (sec == NULL || sym == NULL || name == NULL ||
	    nm_opts.value_print_fn == NULL)
		return;

	PRINT_DEMANGLED_NAME("%s", name);
	printf(" %c ", type);
	if (!IS_UNDEF_SYM_TYPE(type)) {
		nm_opts.value_print_fn(sym);
		printf(" ");
		if (sym->st_size != 0)
			nm_opts.size_print_fn(sym);
	} else
		printf("        ");
}

static void
sym_elem_print_all_sysv(char type, const char *sec, const GElf_Sym *sym,
    const char *name)
{

	if (sec == NULL || sym == NULL || name == NULL ||
	    nm_opts.value_print_fn == NULL)
		return;

	PRINT_DEMANGLED_NAME("%-20s|", name);
	if (IS_UNDEF_SYM_TYPE(type))
		printf("                ");
	else
		nm_opts.value_print_fn(sym);

	printf("|   %c  |", type);

	switch (sym->st_info & 0xf) {
	case STT_OBJECT:
		printf("%18s|", "OBJECT");
		break;

	case STT_FUNC:
		printf("%18s|", "FUNC");
		break;

	case STT_SECTION:
		printf("%18s|", "SECTION");
		break;

	case STT_FILE:
		printf("%18s|", "FILE");
		break;

	case STT_LOPROC:
		printf("%18s|", "LOPROC");
		break;

	case STT_HIPROC:
		printf("%18s|", "HIPROC");
		break;

	case STT_NOTYPE:
	default:
		printf("%18s|", "NOTYPE");
	}

	if (sym->st_size != 0)
		nm_opts.size_print_fn(sym);
	else
		printf("                ");

	printf("|     |%s", sec);
}

static int
sym_elem_def(char type, const GElf_Sym *sym, const char *name)
{

	assert(IS_SYM_TYPE((unsigned char) type));

	UNUSED(sym);
	UNUSED(name);

	return (!IS_UNDEF_SYM_TYPE((unsigned char) type));
}

static int
sym_elem_global(char type, const GElf_Sym *sym, const char *name)
{

	assert(IS_SYM_TYPE((unsigned char) type));

	UNUSED(sym);
	UNUSED(name);

	/* weak symbols resemble global. */
	return (isupper((unsigned char) type) || type == 'w');
}

static int
sym_elem_global_static(char type, const GElf_Sym *sym, const char *name)
{
	unsigned char info;

	assert(sym != NULL);

	UNUSED(type);
	UNUSED(name);

	info = sym->st_info >> 4;

	return (info == STB_LOCAL ||
	    info == STB_GLOBAL ||
	    info == STB_WEAK);
}

static int
sym_elem_nondebug(char type, const GElf_Sym *sym, const char *name)
{

	assert(sym != NULL);

	UNUSED(type);
	UNUSED(name);

	if (sym->st_value == 0 && (sym->st_info & 0xf) == STT_FILE)
		return (0);
	if (sym->st_name == 0)
		return (0);

	return (1);
}

static int
sym_elem_nonzero_size(char type, const GElf_Sym *sym, const char *name)
{

	assert(sym != NULL);

	UNUSED(type);
	UNUSED(name);

	return (sym->st_size > 0);
}

static int
sym_elem_undef(char type, const GElf_Sym *sym, const char *name)
{

	assert(IS_SYM_TYPE((unsigned char) type));

	UNUSED(sym);
	UNUSED(name);

	return (IS_UNDEF_SYM_TYPE((unsigned char) type));
}

static void
sym_list_dest(struct sym_head *headp)
{
	struct sym_entry *ep, *ep_n;

	if (headp == NULL)
		return;

	ep = STAILQ_FIRST(headp);
	while (ep != NULL) {
		ep_n = STAILQ_NEXT(ep, sym_entries);
		free(ep->sym);
		free(ep->name);
		free(ep);
		ep = ep_n;
	}
}

static int
sym_list_insert(struct sym_head *headp, const char *name, const GElf_Sym *sym)
{
	struct sym_entry *e;

	if (headp == NULL || name == NULL || sym == NULL)
		return (0);
	if ((e = malloc(sizeof(struct sym_entry))) == NULL) {
		warn("malloc");
		return (0);
	}
	if ((e->name = strdup(name)) == NULL) {
		warn("strdup");
		free(e);
		return (0);
	}
	if ((e->sym = malloc(sizeof(GElf_Sym))) == NULL) {
		warn("malloc");
		free(e->name);
		free(e);
		return (0);
	}

	memcpy(e->sym, sym, sizeof(GElf_Sym));

	/* Display size instead of value for common symbol. */
	if (sym->st_shndx == SHN_COMMON)
		e->sym->st_value = sym->st_size;

	STAILQ_INSERT_TAIL(headp, e, sym_entries);

	return (1);
}

/* If file has not .debug_info, line_info will be NULL */
static void
sym_list_print(struct sym_print_data *p, struct func_info_head *func_info,
    struct var_info_head *var_info, struct line_info_head *line_info)
{
	struct sym_entry *e_v;
	size_t si;
	int i;

	if (p == NULL || CHECK_SYM_PRINT_DATA(p))
		return;
	if ((e_v = sym_list_sort(p)) == NULL)
		return;
	if (nm_opts.sort_reverse == false)
		for (si = 0; si != p->list_num; ++si)
			sym_list_print_each(&e_v[si], p, func_info, var_info,
			    line_info);
	else
		for (i = p->list_num - 1; i != -1; --i)
			sym_list_print_each(&e_v[i], p, func_info, var_info,
			    line_info);

	free(e_v);
}

/* If file has not .debug_info, line_info will be NULL */
static void
sym_list_print_each(struct sym_entry *ep, struct sym_print_data *p,
    struct func_info_head *func_info, struct var_info_head *var_info,
    struct line_info_head *line_info)
{
	const char *sec;
	char type;

	if (ep == NULL || CHECK_SYM_PRINT_DATA(p))
		return;

	assert(ep->name != NULL);
	assert(ep->sym != NULL);

	type = get_sym_type(ep->sym, p->t_table);

	if (nm_opts.print_name == PRINT_NAME_FULL) {
		printf("%s", p->filename);
		if (nm_opts.elem_print_fn == &sym_elem_print_all_portable) {
			if (p->objname != NULL)
				printf("[%s]", p->objname);
			printf(": ");
		} else {
			if (p->objname != NULL)
				printf(":%s", p->objname);
			printf(":");
		}
	}

	switch (ep->sym->st_shndx) {
	case SHN_LOPROC:
		/* LOPROC or LORESERVE */
		sec = "*LOPROC*";
		break;
	case SHN_HIPROC:
		sec = "*HIPROC*";
		break;
	case SHN_LOOS:
		sec = "*LOOS*";
		break;
	case SHN_HIOS:
		sec = "*HIOS*";
		break;
	case SHN_ABS:
		sec = "*ABS*";
		break;
	case SHN_COMMON:
		sec = "*COM*";
		break;
	case SHN_HIRESERVE:
		/* HIRESERVE or XINDEX */
		sec = "*HIRESERVE*";
		break;
	default:
		if (ep->sym->st_shndx > p->sh_num)
			return;
		sec = p->s_table[ep->sym->st_shndx];
		break;
	}

	nm_opts.elem_print_fn(type, sec, ep->sym, ep->name);

	if (nm_opts.debug_line == true && !IS_UNDEF_SYM_TYPE(type))
		print_lineno(ep, func_info, var_info, line_info);

	printf("\n");
}

static struct sym_entry	*
sym_list_sort(struct sym_print_data *p)
{
	struct sym_entry *ep, *e_v;
	int idx;

	if (p == NULL || CHECK_SYM_PRINT_DATA(p))
		return (NULL);

	if ((e_v = malloc(sizeof(struct sym_entry) * p->list_num)) == NULL) {
		warn("malloc");
		return (NULL);
	}

	idx = 0;
	STAILQ_FOREACH(ep, p->headp, sym_entries) {
		if (ep->name != NULL && ep->sym != NULL) {
			e_v[idx].name = ep->name;
			e_v[idx].sym = ep->sym;
			++idx;
		}
	}

	assert((size_t)idx == p->list_num);

	if (nm_opts.sort_fn != &cmp_none) {
		nm_print_data = p;
		assert(nm_print_data != NULL);
		qsort(e_v, p->list_num, sizeof(struct sym_entry),
		    nm_opts.sort_fn);
	}

	return (e_v);
}

static void
sym_size_oct_print(const GElf_Sym *sym)
{

	assert(sym != NULL && "sym is null");
	printf("%016" PRIo64, sym->st_size);
}

static void
sym_size_hex_print(const GElf_Sym *sym)
{

	assert(sym != NULL && "sym is null");
	if (nm_elfclass == ELFCLASS32)
		printf("%08" PRIx64, sym->st_size);
	else
		printf("%016" PRIx64, sym->st_size);
}

static void
sym_size_dec_print(const GElf_Sym *sym)
{

	assert(sym != NULL && "sym is null");
	printf("%016" PRId64, sym->st_size);
}

static void
sym_value_oct_print(const GElf_Sym *sym)
{

	assert(sym != NULL && "sym is null");
	printf("%016" PRIo64, sym->st_value);
}

static void
sym_value_hex_print(const GElf_Sym *sym)
{

	assert(sym != NULL && "sym is null");
	if (nm_elfclass == ELFCLASS32)
		printf("%08" PRIx64, sym->st_value);
	else
		printf("%016" PRIx64, sym->st_value);
}

static void
sym_value_dec_print(const GElf_Sym *sym)
{

	assert(sym != NULL && "sym is null");
	printf("%016" PRId64, sym->st_value);
}

static void
usage(int exitcode)
{

	printf("Usage: %s [options] file ...\
\n  Display symbolic information in file.\n\
\n  Options: \
\n  -A, --print-file-name     Write the full pathname or library name of an\
\n                            object on each line.\
\n  -a, --debug-syms          Display all symbols include debugger-only\
\n                            symbols.", nm_info.name);
	printf("\
\n  -B                        Equivalent to specifying \"--format=bsd\".\
\n  -C, --demangle[=style]    Decode low-level symbol names.\
\n      --no-demangle         Do not demangle low-level symbol names.\
\n  -D, --dynamic             Display only dynamic symbols.\
\n  -e                        Display only global and static symbols.");
	printf("\
\n  -f                        Produce full output (default).\
\n      --format=format       Display output in specific format.  Allowed\
\n                            formats are: \"bsd\", \"posix\" and \"sysv\".\
\n  -g, --extern-only         Display only global symbol information.\
\n  -h, --help                Show this help message.\
\n  -l, --line-numbers        Display filename and linenumber using\
\n                            debugging information.\
\n  -n, --numeric-sort        Sort symbols numerically by value.");
	printf("\
\n  -o                        Write numeric values in octal. Equivalent to\
\n                            specifying \"-t o\".\
\n  -p, --no-sort             Do not sort symbols.\
\n  -P                        Write information in a portable output format.\
\n                            Equivalent to specifying \"--format=posix\".\
\n  -r, --reverse-sort        Reverse the order of the sort.\
\n  -S, --print-size          Print symbol sizes instead values.\
\n  -s, --print-armap         Include an index of archive members.\
\n      --size-sort           Sort symbols by size.");
	printf("\
\n  -t, --radix=format        Write each numeric value in the specified\
\n                            format:\
\n                               d   In decimal,\
\n                               o   In octal,\
\n                               x   In hexadecimal.");
	printf("\
\n  -u, --undefined-only      Display only undefined symbols.\
\n      --defined-only        Display only defined symbols.\
\n  -V, --version             Show the version identifier for %s.\
\n  -v                        Sort output by value.\
\n  -x                        Write numeric values in hexadecimal.\
\n                            Equivalent to specifying \"-t x\".",
	    nm_info.name);
	printf("\n\
\n  The default options are: output in bsd format, use a hexadecimal radix,\
\n  sort by symbol name, do not demangle names.\n");

	exit(exitcode);
}

/*
 * Display symbolic information in file.
 * Return 0 at success, >0 at failed.
 */
int
main(int argc, char **argv)
{
	int rtn;

	global_init();
	get_opt(argc, argv);
	rtn = read_files(argc - optind, argv + optind);
	global_dest();

	exit(rtn);
}
