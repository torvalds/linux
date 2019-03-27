/*
 * Backtrace debugging
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifdef WPA_TRACE_BFD
#define _GNU_SOURCE
#include <link.h>
#endif /* WPA_TRACE_BCD */
#include "includes.h"

#include "common.h"
#include "trace.h"

#ifdef WPA_TRACE

static struct dl_list active_references =
{ &active_references, &active_references };

#ifdef WPA_TRACE_BFD
#include <bfd.h>

#define DMGL_PARAMS      (1 << 0)
#define DMGL_ANSI        (1 << 1)

static char *prg_fname = NULL;
static bfd *cached_abfd = NULL;
static asymbol **syms = NULL;
static unsigned long start_offset;
static int start_offset_looked_up;


static int callback(struct dl_phdr_info *info, size_t size, void *data)
{
	/*
	 * dl_iterate_phdr(3):
	 * "The first object visited by callback is the main program."
	 */
	start_offset = info->dlpi_addr;

	/*
	 * dl_iterate_phdr(3):
	 * "The dl_iterate_phdr() function walks through the list of an
	 *  application's shared objects and calls the function callback
	 *  once for each object, until either all shared objects have
	 *  been processed or callback returns a nonzero value."
	 */
	return 1;
}


static void get_prg_fname(void)
{
	char exe[50], fname[512];
	int len;
	os_snprintf(exe, sizeof(exe) - 1, "/proc/%u/exe", getpid());
	len = readlink(exe, fname, sizeof(fname) - 1);
	if (len < 0 || len >= (int) sizeof(fname)) {
		wpa_printf(MSG_ERROR, "readlink: %s", strerror(errno));
		return;
	}
	fname[len] = '\0';
	prg_fname = strdup(fname);
}


static bfd * open_bfd(const char *fname)
{
	bfd *abfd;
	char **matching;

	abfd = bfd_openr(prg_fname, NULL);
	if (abfd == NULL) {
		wpa_printf(MSG_INFO, "bfd_openr failed");
		return NULL;
	}

	if (bfd_check_format(abfd, bfd_archive)) {
		wpa_printf(MSG_INFO, "bfd_check_format failed");
		bfd_close(abfd);
		return NULL;
	}

	if (!bfd_check_format_matches(abfd, bfd_object, &matching)) {
		wpa_printf(MSG_INFO, "bfd_check_format_matches failed");
		free(matching);
		bfd_close(abfd);
		return NULL;
	}

	return abfd;
}


static void read_syms(bfd *abfd)
{
	long storage, symcount;
	bfd_boolean dynamic = FALSE;

	if (syms)
		return;

	if (!(bfd_get_file_flags(abfd) & HAS_SYMS)) {
		wpa_printf(MSG_INFO, "No symbols");
		return;
	}

	storage = bfd_get_symtab_upper_bound(abfd);
	if (storage == 0) {
		storage = bfd_get_dynamic_symtab_upper_bound(abfd);
		dynamic = TRUE;
	}
	if (storage < 0) {
		wpa_printf(MSG_INFO, "Unknown symtab upper bound");
		return;
	}

	syms = malloc(storage);
	if (syms == NULL) {
		wpa_printf(MSG_INFO, "Failed to allocate memory for symtab "
			   "(%ld bytes)", storage);
		return;
	}
	if (dynamic)
		symcount = bfd_canonicalize_dynamic_symtab(abfd, syms);
	else
		symcount = bfd_canonicalize_symtab(abfd, syms);
	if (symcount < 0) {
		wpa_printf(MSG_INFO, "Failed to canonicalize %ssymtab",
			   dynamic ? "dynamic " : "");
		free(syms);
		syms = NULL;
		return;
	}
}


struct bfd_data {
	bfd_vma pc;
	bfd_boolean found;
	const char *filename;
	const char *function;
	unsigned int line;
};


static void find_addr_sect(bfd *abfd, asection *section, void *obj)
{
	struct bfd_data *data = obj;
	bfd_vma vma;
	bfd_size_type size;

	if (data->found)
		return;

	if (!(bfd_get_section_vma(abfd, section)))
		return;

	vma = bfd_get_section_vma(abfd, section);
	if (data->pc < vma)
		return;

	size = bfd_get_section_size(section);
	if (data->pc >= vma + size)
		return;

	data->found = bfd_find_nearest_line(abfd, section, syms,
					    data->pc - vma,
					    &data->filename,
					    &data->function,
					    &data->line);
}


static void wpa_trace_bfd_addr(void *pc)
{
	bfd *abfd = cached_abfd;
	struct bfd_data data;
	const char *name;
	char *aname = NULL;
	const char *filename;

	if (abfd == NULL)
		return;

	data.pc = (bfd_hostptr_t) (pc - start_offset);
	data.found = FALSE;
	bfd_map_over_sections(abfd, find_addr_sect, &data);

	if (!data.found)
		return;

	do {
		if (data.function)
			aname = bfd_demangle(abfd, data.function,
					     DMGL_ANSI | DMGL_PARAMS);
		name = aname ? aname : data.function;
		filename = data.filename;
		if (filename) {
			char *end = os_strrchr(filename, '/');
			int i = 0;
			while (*filename && *filename == prg_fname[i] &&
			       filename <= end) {
				filename++;
				i++;
			}
		}
		wpa_printf(MSG_INFO, "     %s() %s:%u",
			   name, filename, data.line);
		free(aname);
		aname = NULL;

		data.found = bfd_find_inliner_info(abfd, &data.filename,
						   &data.function, &data.line);
	} while (data.found);
}


static const char * wpa_trace_bfd_addr2func(void *pc)
{
	bfd *abfd = cached_abfd;
	struct bfd_data data;

	if (abfd == NULL)
		return NULL;

	data.pc = (bfd_hostptr_t) (pc - start_offset);
	data.found = FALSE;
	bfd_map_over_sections(abfd, find_addr_sect, &data);

	if (!data.found)
		return NULL;

	return data.function;
}


static void wpa_trace_bfd_init(void)
{
	if (!prg_fname) {
		get_prg_fname();
		if (!prg_fname)
			return;
	}

	if (!cached_abfd) {
		cached_abfd = open_bfd(prg_fname);
		if (!cached_abfd) {
			wpa_printf(MSG_INFO, "Failed to open bfd");
			return;
		}
	}

	read_syms(cached_abfd);
	if (!syms) {
		wpa_printf(MSG_INFO, "Failed to read symbols");
		return;
	}

	if (!start_offset_looked_up) {
		dl_iterate_phdr(callback, NULL);
		start_offset_looked_up = 1;
	}
}


void wpa_trace_dump_funcname(const char *title, void *pc)
{
	wpa_printf(MSG_INFO, "WPA_TRACE: %s: %p", title, pc);
	wpa_trace_bfd_init();
	wpa_trace_bfd_addr(pc);
}


size_t wpa_trace_calling_func(const char *buf[], size_t len)
{
	bfd *abfd;
	void *btrace_res[WPA_TRACE_LEN];
	int i, btrace_num;
	size_t pos = 0;

	if (len == 0)
		return 0;
	if (len > WPA_TRACE_LEN)
		len = WPA_TRACE_LEN;

	wpa_trace_bfd_init();
	abfd = cached_abfd;
	if (!abfd)
		return 0;

	btrace_num = backtrace(btrace_res, len);
	if (btrace_num < 1)
		return 0;

	for (i = 0; i < btrace_num; i++) {
		struct bfd_data data;

		data.pc = (bfd_hostptr_t) (btrace_res[i] - start_offset);
		data.found = FALSE;
		bfd_map_over_sections(abfd, find_addr_sect, &data);

		while (data.found) {
			if (data.function &&
			    (pos > 0 ||
			     os_strcmp(data.function, __func__) != 0)) {
				buf[pos++] = data.function;
				if (pos == len)
					return pos;
			}

			data.found = bfd_find_inliner_info(abfd, &data.filename,
							   &data.function,
							   &data.line);
		}
	}

	return pos;
}

#else /* WPA_TRACE_BFD */

#define wpa_trace_bfd_init() do { } while (0)
#define wpa_trace_bfd_addr(pc) do { } while (0)
#define wpa_trace_bfd_addr2func(pc) NULL

#endif /* WPA_TRACE_BFD */

void wpa_trace_dump_func(const char *title, void **btrace, int btrace_num)
{
	char **sym;
	int i;
	enum { TRACE_HEAD, TRACE_RELEVANT, TRACE_TAIL } state;

	wpa_trace_bfd_init();
	wpa_printf(MSG_INFO, "WPA_TRACE: %s - START", title);
	sym = backtrace_symbols(btrace, btrace_num);
	state = TRACE_HEAD;
	for (i = 0; i < btrace_num; i++) {
		const char *func = wpa_trace_bfd_addr2func(btrace[i]);
		if (state == TRACE_HEAD && func &&
		    (os_strcmp(func, "wpa_trace_add_ref_func") == 0 ||
		     os_strcmp(func, "wpa_trace_check_ref") == 0 ||
		     os_strcmp(func, "wpa_trace_show") == 0))
			continue;
		if (state == TRACE_TAIL && sym && sym[i] &&
		    os_strstr(sym[i], "__libc_start_main"))
			break;
		if (state == TRACE_HEAD)
			state = TRACE_RELEVANT;
		if (sym)
			wpa_printf(MSG_INFO, "[%d]: %s", i, sym[i]);
		else
			wpa_printf(MSG_INFO, "[%d]: ?? [%p]", i, btrace[i]);
		wpa_trace_bfd_addr(btrace[i]);
		if (state == TRACE_RELEVANT && func &&
		    os_strcmp(func, "main") == 0)
			state = TRACE_TAIL;
	}
	free(sym);
	wpa_printf(MSG_INFO, "WPA_TRACE: %s - END", title);
}


void wpa_trace_show(const char *title)
{
	struct info {
		WPA_TRACE_INFO
	} info;
	wpa_trace_record(&info);
	wpa_trace_dump(title, &info);
}


void wpa_trace_add_ref_func(struct wpa_trace_ref *ref, const void *addr)
{
	if (addr == NULL)
		return;
	ref->addr = addr;
	wpa_trace_record(ref);
	dl_list_add(&active_references, &ref->list);
}


void wpa_trace_check_ref(const void *addr)
{
	struct wpa_trace_ref *ref;
	dl_list_for_each(ref, &active_references, struct wpa_trace_ref, list) {
		if (addr != ref->addr)
			continue;
		wpa_trace_show("Freeing referenced memory");
		wpa_trace_dump("Reference registration", ref);
		abort();
	}
}


void wpa_trace_deinit(void)
{
#ifdef WPA_TRACE_BFD
	free(syms);
	syms = NULL;
#endif /* WPA_TRACE_BFD */
}

#endif /* WPA_TRACE */
