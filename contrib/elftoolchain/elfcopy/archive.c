/*-
 * Copyright (c) 2007-2009 Kai Wang
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
#include <sys/stat.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef LIBELF_AR
#include <archive.h>
#include <archive_entry.h>
#endif	/* ! LIBELF_AR */

#include "elfcopy.h"

ELFTC_VCSID("$Id: archive.c 3490 2016-08-31 00:12:22Z emaste $");

#define _ARMAG_LEN 8		/* length of ar magic string */
#define _ARHDR_LEN 60		/* length of ar header */
#define _INIT_AS_CAP 128	/* initial archive string table size */
#define _INIT_SYMOFF_CAP (256*(sizeof(uint32_t))) /* initial so table size */
#define _INIT_SYMNAME_CAP 1024			  /* initial sn table size */
#define _MAXNAMELEN_SVR4 15	/* max member name length in svr4 variant */

#ifndef LIBELF_AR
static void ac_read_objs(struct elfcopy *ecp, int ifd);
static void ac_write_cleanup(struct elfcopy *ecp);
static void ac_write_data(struct archive *a, const void *buf, size_t s);
static void ac_write_objs(struct elfcopy *ecp, int ofd);
#endif	/* ! LIBELF_AR */
static void add_to_ar_str_table(struct elfcopy *elfcopy, const char *name);
static void add_to_ar_sym_table(struct elfcopy *ecp, const char *name);
static void extract_arsym(struct elfcopy *ecp);
static void process_ar_obj(struct elfcopy *ecp, struct ar_obj *obj);
static void sync_ar(struct elfcopy *ecp);


static void
process_ar_obj(struct elfcopy *ecp, struct ar_obj *obj)
{
	struct stat	 sb;
	char		*tempfile;
	int		 fd;

	/* Output to a temporary file. */
	create_tempfile(&tempfile, &fd);
	if ((ecp->eout = elf_begin(fd, ELF_C_WRITE, NULL)) == NULL)
		errx(EXIT_FAILURE, "elf_begin() failed: %s",
		    elf_errmsg(-1));
	elf_flagelf(ecp->eout, ELF_C_SET, ELF_F_LAYOUT);
	create_elf(ecp);
	elf_end(ecp->ein);
	elf_end(ecp->eout);
	free(obj->buf);
	obj->buf = NULL;

	/* Extract archive symbols. */
	if (lseek(fd, 0, SEEK_SET) < 0)
		err(EXIT_FAILURE, "lseek failed for '%s'", tempfile);
	if ((ecp->eout = elf_begin(fd, ELF_C_READ, NULL)) == NULL)
		errx(EXIT_FAILURE, "elf_begin() failed: %s",
		    elf_errmsg(-1));
	extract_arsym(ecp);
	elf_end(ecp->eout);

	if (fstat(fd, &sb) == -1)
		err(EXIT_FAILURE, "fstat %s failed", tempfile);
	if (lseek(fd, 0, SEEK_SET) < 0)
		err(EXIT_FAILURE, "lseek %s failed", tempfile);
	obj->size = sb.st_size;
	if ((obj->maddr = malloc(obj->size)) == NULL)
		err(EXIT_FAILURE, "memory allocation failed for '%s'",
		    tempfile);
	if ((size_t) read(fd, obj->maddr, obj->size) != obj->size)
		err(EXIT_FAILURE, "read failed for '%s'", tempfile);
	if (unlink(tempfile))
		err(EXIT_FAILURE, "unlink %s failed", tempfile);
	free(tempfile);
	close(fd);
	if (strlen(obj->name) > _MAXNAMELEN_SVR4)
		add_to_ar_str_table(ecp, obj->name);
	ecp->rela_off += _ARHDR_LEN + obj->size + obj->size % 2;
	STAILQ_INSERT_TAIL(&ecp->v_arobj, obj, objs);
}

/*
 * Append to the archive string table buffer.
 */
static void
add_to_ar_str_table(struct elfcopy *ecp, const char *name)
{

	if (ecp->as == NULL) {
		ecp->as_cap = _INIT_AS_CAP;
		ecp->as_sz = 0;
		if ((ecp->as = malloc(ecp->as_cap)) == NULL)
			err(EXIT_FAILURE, "malloc failed");
	}

	/*
	 * The space required for holding one member name in as table includes:
	 * strlen(name) + (1 for '/') + (1 for '\n') + (possibly 1 for padding).
	 */
	while (ecp->as_sz + strlen(name) + 3 > ecp->as_cap) {
		ecp->as_cap *= 2;
		ecp->as = realloc(ecp->as, ecp->as_cap);
		if (ecp->as == NULL)
			err(EXIT_FAILURE, "realloc failed");
	}
	strncpy(&ecp->as[ecp->as_sz], name, strlen(name));
	ecp->as_sz += strlen(name);
	ecp->as[ecp->as_sz++] = '/';
	ecp->as[ecp->as_sz++] = '\n';
}

/*
 * Append to the archive symbol table buffer.
 */
static void
add_to_ar_sym_table(struct elfcopy *ecp, const char *name)
{

	if (ecp->s_so == NULL) {
		if ((ecp->s_so = malloc(_INIT_SYMOFF_CAP)) == NULL)
			err(EXIT_FAILURE, "malloc failed");
		ecp->s_so_cap = _INIT_SYMOFF_CAP;
		ecp->s_cnt = 0;
	}

	if (ecp->s_sn == NULL) {
		if ((ecp->s_sn = malloc(_INIT_SYMNAME_CAP)) == NULL)
			err(EXIT_FAILURE, "malloc failed");
		ecp->s_sn_cap = _INIT_SYMNAME_CAP;
		ecp->s_sn_sz = 0;
	}

	if (ecp->s_cnt * sizeof(uint32_t) >= ecp->s_so_cap) {
		ecp->s_so_cap *= 2;
		ecp->s_so = realloc(ecp->s_so, ecp->s_so_cap);
		if (ecp->s_so == NULL)
			err(EXIT_FAILURE, "realloc failed");
	}
	ecp->s_so[ecp->s_cnt] = ecp->rela_off;
	ecp->s_cnt++;

	/*
	 * The space required for holding one symbol name in sn table includes:
	 * strlen(name) + (1 for '\n') + (possibly 1 for padding).
	 */
	while (ecp->s_sn_sz + strlen(name) + 2 > ecp->s_sn_cap) {
		ecp->s_sn_cap *= 2;
		ecp->s_sn = realloc(ecp->s_sn, ecp->s_sn_cap);
		if (ecp->s_sn == NULL)
			err(EXIT_FAILURE, "realloc failed");
	}
	strncpy(&ecp->s_sn[ecp->s_sn_sz], name, strlen(name));
	ecp->s_sn_sz += strlen(name);
	ecp->s_sn[ecp->s_sn_sz++] = '\0';
}

static void
sync_ar(struct elfcopy *ecp)
{
	size_t s_sz;		/* size of archive symbol table. */
	size_t pm_sz;		/* size of pseudo members */
	int i;

	/*
	 * Pad the symbol name string table. It is treated specially because
	 * symbol name table should be padded by a '\0', not the common '\n'
	 * for other members. The size of sn table includes the pad bit.
	 */
	if (ecp->s_cnt != 0 && ecp->s_sn_sz % 2 != 0)
		ecp->s_sn[ecp->s_sn_sz++] = '\0';

	/*
	 * Archive string table is padded by a "\n" as the normal members.
	 * The difference is that the size of archive string table counts
	 * in the pad bit, while normal members' size fileds do not.
	 */
	if (ecp->as != NULL && ecp->as_sz % 2 != 0)
		ecp->as[ecp->as_sz++] = '\n';

	/*
	 * If there is a symbol table, calculate the size of pseudo members,
	 * convert previously stored relative offsets to absolute ones, and
	 * then make them Big Endian.
	 *
	 * absolute_offset = htobe32(relative_offset + size_of_pseudo_members)
	 */

	if (ecp->s_cnt != 0) {
		s_sz = (ecp->s_cnt + 1) * sizeof(uint32_t) + ecp->s_sn_sz;
		pm_sz = _ARMAG_LEN + (_ARHDR_LEN + s_sz);
		if (ecp->as != NULL)
			pm_sz += _ARHDR_LEN + ecp->as_sz;
		for (i = 0; (size_t)i < ecp->s_cnt; i++)
			*(ecp->s_so + i) = htobe32(*(ecp->s_so + i) +
			    pm_sz);
	}
}

/*
 * Extract global symbols from archive members.
 */
static void
extract_arsym(struct elfcopy *ecp)
{
	Elf_Scn		*scn;
	GElf_Shdr	 shdr;
	GElf_Sym	 sym;
	Elf_Data	*data;
	char		*name;
	size_t		 n, shstrndx;
	int		 elferr, tabndx, len, i;

	if (elf_kind(ecp->eout) != ELF_K_ELF) {
		warnx("internal: cannot extract symbols from non-elf object");
		return;
	}
	if (elf_getshstrndx(ecp->eout, &shstrndx) == 0) {
		warnx("elf_getshstrndx failed: %s", elf_errmsg(-1));
		return;
	}

	tabndx = -1;
	scn = NULL;
	while ((scn = elf_nextscn(ecp->eout, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			warnx("elf_getshdr failed: %s", elf_errmsg(-1));
			continue;
		}
		if ((name = elf_strptr(ecp->eout, shstrndx, shdr.sh_name)) ==
		    NULL) {
			warnx("elf_strptr failed: %s", elf_errmsg(-1));
			continue;
		}
		if (strcmp(name, ".strtab") == 0) {
			tabndx = elf_ndxscn(scn);
			break;
		}
	}
	elferr = elf_errno();
	if (elferr != 0)
		warnx("elf_nextscn failed: %s", elf_errmsg(elferr));

	/* Ignore members without symbol table. */
	if (tabndx == -1)
		return;

	scn = NULL;
	while ((scn = elf_nextscn(ecp->eout, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			warnx("elf_getshdr failed: %s", elf_errmsg(-1));
			continue;
		}
		if (shdr.sh_type != SHT_SYMTAB)
			continue;

		data = NULL;
		n = 0;
		while (n < shdr.sh_size &&
		    (data = elf_getdata(scn, data)) != NULL) {
			len = data->d_size / shdr.sh_entsize;
			for (i = 0; i < len; i++) {
				if (gelf_getsym(data, i, &sym) != &sym) {
					warnx("gelf_getsym failed: %s",
					     elf_errmsg(-1));
					continue;
				}

				/* keep only global or weak symbols */
				if (GELF_ST_BIND(sym.st_info) != STB_GLOBAL &&
				    GELF_ST_BIND(sym.st_info) != STB_WEAK)
					continue;

				/* keep only defined symbols */
				if (sym.st_shndx == SHN_UNDEF)
					continue;

				if ((name = elf_strptr(ecp->eout, tabndx,
				    sym.st_name)) == NULL) {
					warnx("elf_strptr failed: %s",
					     elf_errmsg(-1));
					continue;
				}

				add_to_ar_sym_table(ecp, name);
			}
		}
	}
	elferr = elf_errno();
	if (elferr != 0)
		warnx("elf_nextscn failed: %s", elf_errmsg(elferr));
}

#ifndef LIBELF_AR

/*
 * Convenient wrapper for general libarchive error handling.
 */
#define	AC(CALL) do {							\
	if ((CALL))							\
		errx(EXIT_FAILURE, "%s", archive_error_string(a));	\
} while (0)

/* Earlier versions of libarchive had some functions that returned 'void'. */
#if	ARCHIVE_VERSION_NUMBER >= 2000000
#define	ACV(CALL) 	AC(CALL)
#else
#define	ACV(CALL)	do {						\
		(CALL);							\
	} while (0)
#endif

int
ac_detect_ar(int ifd)
{
	struct archive		*a;
	struct archive_entry	*entry;
	int			 r;

	r = -1;
	if ((a = archive_read_new()) == NULL)
		return (0);
	archive_read_support_format_ar(a);
	if (archive_read_open_fd(a, ifd, 10240) == ARCHIVE_OK)
		r = archive_read_next_header(a, &entry);
	archive_read_close(a);
	archive_read_free(a);

	return (r == ARCHIVE_OK);
}

void
ac_create_ar(struct elfcopy *ecp, int ifd, int ofd)
{

	ac_read_objs(ecp, ifd);
	sync_ar(ecp);
	ac_write_objs(ecp, ofd);
	ac_write_cleanup(ecp);
}

static void
ac_read_objs(struct elfcopy *ecp, int ifd)
{
	struct archive		*a;
	struct archive_entry	*entry;
	struct ar_obj		*obj;
	const char		*name;
	char			*buff;
	size_t			 size;
	int			 r;

	ecp->rela_off = 0;
	if (lseek(ifd, 0, SEEK_SET) == -1)
		err(EXIT_FAILURE, "lseek failed");
	if ((a = archive_read_new()) == NULL)
		errx(EXIT_FAILURE, "archive_read_new failed");
	archive_read_support_format_ar(a);
	AC(archive_read_open_fd(a, ifd, 10240));
	for(;;) {
		r = archive_read_next_header(a, &entry);
		if (r == ARCHIVE_FATAL)
			errx(EXIT_FAILURE, "%s", archive_error_string(a));
		if (r == ARCHIVE_EOF)
			break;
		if (r == ARCHIVE_WARN || r == ARCHIVE_RETRY)
			warnx("%s", archive_error_string(a));
		if (r == ARCHIVE_RETRY)
			continue;

		name = archive_entry_pathname(entry);

		/* skip pseudo members. */
		if (strcmp(name, "/") == 0 || strcmp(name, "//") == 0)
			continue;

		size = archive_entry_size(entry);

		if (size > 0) {
			if ((buff = malloc(size)) == NULL)
				err(EXIT_FAILURE, "malloc failed");
			if (archive_read_data(a, buff, size) != (ssize_t)size) {
				warnx("%s", archive_error_string(a));
				free(buff);
				continue;
			}
			if ((obj = malloc(sizeof(*obj))) == NULL)
				err(EXIT_FAILURE, "malloc failed");
			if ((obj->name = strdup(name)) == NULL)
				err(EXIT_FAILURE, "strdup failed");
			obj->buf = buff;
			obj->uid = archive_entry_uid(entry);
			obj->gid = archive_entry_gid(entry);
			obj->md = archive_entry_mode(entry);
			obj->mtime = archive_entry_mtime(entry);
			if ((ecp->ein = elf_memory(buff, size)) == NULL)
				errx(EXIT_FAILURE, "elf_memory() failed: %s",
				    elf_errmsg(-1));
			if (elf_kind(ecp->ein) != ELF_K_ELF)
				errx(EXIT_FAILURE,
				    "file format not recognized");
			process_ar_obj(ecp, obj);
		}
	}
	AC(archive_read_close(a));
	ACV(archive_read_free(a));
}

static void
ac_write_objs(struct elfcopy *ecp, int ofd)
{
	struct archive		*a;
	struct archive_entry	*entry;
	struct ar_obj		*obj;
	time_t			 timestamp;
	int			 nr;

	if ((a = archive_write_new()) == NULL)
		errx(EXIT_FAILURE, "archive_write_new failed");
	archive_write_set_format_ar_svr4(a);
	AC(archive_write_open_fd(a, ofd));

	/* Write the archive symbol table, even if it's empty. */
	entry = archive_entry_new();
	archive_entry_copy_pathname(entry, "/");
	if (elftc_timestamp(&timestamp) != 0)
		err(EXIT_FAILURE, "elftc_timestamp");
	archive_entry_set_mtime(entry, timestamp, 0);
	archive_entry_set_size(entry, (ecp->s_cnt + 1) * sizeof(uint32_t) +
	    ecp->s_sn_sz);
	AC(archive_write_header(a, entry));
	nr = htobe32(ecp->s_cnt);
	ac_write_data(a, &nr, sizeof(uint32_t));
	ac_write_data(a, ecp->s_so, sizeof(uint32_t) * ecp->s_cnt);
	ac_write_data(a, ecp->s_sn, ecp->s_sn_sz);
	archive_entry_free(entry);

	/* Write the archive string table, if exist. */
	if (ecp->as != NULL) {
		entry = archive_entry_new();
		archive_entry_copy_pathname(entry, "//");
		archive_entry_set_size(entry, ecp->as_sz);
		AC(archive_write_header(a, entry));
		ac_write_data(a, ecp->as, ecp->as_sz);
		archive_entry_free(entry);
	}

	/* Write normal members. */
	STAILQ_FOREACH(obj, &ecp->v_arobj, objs) {
		entry = archive_entry_new();
		archive_entry_copy_pathname(entry, obj->name);
		archive_entry_set_uid(entry, obj->uid);
		archive_entry_set_gid(entry, obj->gid);
		archive_entry_set_mode(entry, obj->md);
		archive_entry_set_size(entry, obj->size);
		archive_entry_set_mtime(entry, obj->mtime, 0);
		archive_entry_set_filetype(entry, AE_IFREG);
		AC(archive_write_header(a, entry));
		ac_write_data(a, obj->maddr, obj->size);
		archive_entry_free(entry);
	}

	AC(archive_write_close(a));
	ACV(archive_write_free(a));
}

static void
ac_write_cleanup(struct elfcopy *ecp)
{
	struct ar_obj		*obj, *obj_temp;

	STAILQ_FOREACH_SAFE(obj, &ecp->v_arobj, objs, obj_temp) {
		STAILQ_REMOVE(&ecp->v_arobj, obj, ar_obj, objs);
		if (obj->maddr != NULL)
			free(obj->maddr);
		free(obj->name);
		free(obj);
	}

	free(ecp->as);
	free(ecp->s_so);
	free(ecp->s_sn);
	ecp->as = NULL;
	ecp->s_so = NULL;
	ecp->s_sn = NULL;
}

/*
 * Wrapper for archive_write_data().
 */
static void
ac_write_data(struct archive *a, const void *buf, size_t s)
{
	if (archive_write_data(a, buf, s) != (ssize_t)s)
		errx(EXIT_FAILURE, "%s", archive_error_string(a));
}

#endif	/* ! LIBELF_AR */
