/*-
 * Copyright (c) 2016 Kai Wang
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
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "_libpe.h"

ELFTC_VCSID("$Id: libpe_section.c 3446 2016-05-03 01:31:17Z emaste $");

PE_Scn *
libpe_alloc_scn(PE *pe)
{
	PE_Scn *ps;

	if ((ps = calloc(1, sizeof(PE_Scn))) == NULL) {
		errno = ENOMEM;
		return (NULL);
	}
	STAILQ_INIT(&ps->ps_b);
	ps->ps_pe = pe;

	return (ps);
}

void
libpe_release_scn(PE_Scn *ps)
{
	PE *pe;
	PE_SecBuf *sb, *_sb;

	assert(ps != NULL);

	pe = ps->ps_pe;

	STAILQ_REMOVE(&pe->pe_scn, ps, _PE_Scn, ps_next);

	STAILQ_FOREACH_SAFE(sb, &ps->ps_b, sb_next, _sb)
		libpe_release_buffer(sb);

	free(ps);
}

static int
cmp_scn(PE_Scn *a, PE_Scn *b)
{

	if (a->ps_sh.sh_addr < b->ps_sh.sh_addr)
		return (-1);
	else if (a->ps_sh.sh_addr == b->ps_sh.sh_addr)
		return (0);
	else
		return (1);
}

static void
sort_sections(PE *pe)
{

	if (STAILQ_EMPTY(&pe->pe_scn))
		return;

	/* Sort the list of Scn by RVA in ascending order. */
	STAILQ_SORT(&pe->pe_scn, _PE_Scn, ps_next, cmp_scn);
}

int
libpe_parse_section_headers(PE *pe)
{
	char tmp[sizeof(PE_SecHdr)], *hdr;
	PE_Scn *ps;
	PE_SecHdr *sh;
	PE_CoffHdr *ch;
	PE_DataDir *dd;
	int found, i;

	assert(pe->pe_ch != NULL);

	for (i = 0; (uint16_t) i < pe->pe_ch->ch_nsec; i++) {
		if (read(pe->pe_fd, tmp, sizeof(PE_SecHdr)) !=
		    (ssize_t) sizeof(PE_SecHdr)) {
			pe->pe_flags |= LIBPE_F_BAD_SEC_HEADER;
			return (0);
		}

		if ((ps = libpe_alloc_scn(pe)) == NULL)
			return (-1);
		STAILQ_INSERT_TAIL(&pe->pe_scn, ps, ps_next);
		ps->ps_ndx = ++pe->pe_nscn;	/* Setion index is 1-based */
		sh = &ps->ps_sh;

		/*
		 * Note that the section name won't be NUL-terminated if
		 * its length happens to be 8.
		 */
		memcpy(sh->sh_name, tmp, sizeof(sh->sh_name));
		hdr = tmp + 8;
		PE_READ32(hdr, sh->sh_virtsize);
		PE_READ32(hdr, sh->sh_addr);
		PE_READ32(hdr, sh->sh_rawsize);
		PE_READ32(hdr, sh->sh_rawptr);
		PE_READ32(hdr, sh->sh_relocptr);
		PE_READ32(hdr, sh->sh_lineptr);
		PE_READ16(hdr, sh->sh_nreloc);
		PE_READ16(hdr, sh->sh_nline);
		PE_READ32(hdr, sh->sh_char);
	}

	/*
	 * For all the data directories that don't belong to any section,
	 * we create pseudo sections for them to make layout easier.
	 */
	dd = pe->pe_dd;
	if (dd != NULL && dd->dd_total > 0) {
		for (i = 0; (uint32_t) i < pe->pe_dd->dd_total; i++) {
			if (dd->dd_e[i].de_size == 0)
				continue;
			found = 0;
			STAILQ_FOREACH(ps, &pe->pe_scn, ps_next) {
				sh = &ps->ps_sh;
				if (dd->dd_e[i].de_addr >= sh->sh_addr &&
				    dd->dd_e[i].de_addr + dd->dd_e[i].de_size <=
				    sh->sh_addr + sh->sh_virtsize) {
					found = 1;
					break;
				}
			}
			if (found)
				continue;

			if ((ps = libpe_alloc_scn(pe)) == NULL)
				return (-1);
			STAILQ_INSERT_TAIL(&pe->pe_scn, ps, ps_next);
			ps->ps_ndx = 0xFFFF0000U | i;
			sh = &ps->ps_sh;
			sh->sh_rawptr = dd->dd_e[i].de_addr; /* FIXME */
			sh->sh_rawsize = dd->dd_e[i].de_size;
		}
	}

	/*
	 * Also consider the COFF symbol table as a pseudo section.
	 */
	ch = pe->pe_ch;
	if (ch->ch_nsym > 0) {
		if ((ps = libpe_alloc_scn(pe)) == NULL)
			return (-1);
		STAILQ_INSERT_TAIL(&pe->pe_scn, ps, ps_next);
		ps->ps_ndx = 0xFFFFFFFFU;
		sh = &ps->ps_sh;
		sh->sh_rawptr = ch->ch_symptr;
		sh->sh_rawsize = ch->ch_nsym * PE_SYM_ENTRY_SIZE;
		pe->pe_nsym = ch->ch_nsym;
	}

	/* PE file headers initialization is complete if we reach here. */
	return (0);
}

int
libpe_load_section(PE *pe, PE_Scn *ps)
{
	PE_SecHdr *sh;
	PE_SecBuf *sb;
	size_t sz;
	char tmp[4];

	assert(pe != NULL && ps != NULL);
	assert((ps->ps_flags & LIBPE_F_LOAD_SECTION) == 0);

	sh = &ps->ps_sh;

	/* Allocate a PE_SecBuf struct without buffer for empty sections. */
	if (sh->sh_rawsize == 0) {
		(void) libpe_alloc_buffer(ps, 0);
		ps->ps_flags |= LIBPE_F_LOAD_SECTION;
		return (0);
	}

	if ((pe->pe_flags & LIBPE_F_SPECIAL_FILE) == 0) {
		if (lseek(pe->pe_fd, (off_t) sh->sh_rawptr, SEEK_SET) < 0) {
			errno = EIO;
			return (-1);
		}
	}

	if ((sb = libpe_alloc_buffer(ps, sh->sh_rawsize)) == NULL)
		return (-1);

	if (read(pe->pe_fd, sb->sb_pb.pb_buf, sh->sh_rawsize) !=
	    (ssize_t) sh->sh_rawsize) {
		errno = EIO;
		return (-1);
	}

	if (ps->ps_ndx == 0xFFFFFFFFU) {
		/*
		 * Index 0xFFFFFFFF indicates this section is a pseudo
		 * section that contains the COFF symbol table. We should
		 * read in the string table right after it.
		 */
		if (read(pe->pe_fd, tmp, sizeof(tmp)) !=
		    (ssize_t) sizeof(tmp)) {
			errno = EIO;
			return (-1);
		}
		sz = le32dec(tmp);

		/*
		 * The minimum value for the size field is 4, which indicates
		 * there is no string table.
		 */
		if (sz > 4) {
			sz -= 4;
			if ((sb = libpe_alloc_buffer(ps, sz)) == NULL)
				return (-1);
			if (read(pe->pe_fd, sb->sb_pb.pb_buf, sz) !=
			    (ssize_t) sz) {
				errno = EIO;
				return (-1);
			}
		}
	}

	ps->ps_flags |= LIBPE_F_LOAD_SECTION;

	return (0);
}

int
libpe_load_all_sections(PE *pe)
{
	PE_Scn *ps;
	PE_SecHdr *sh;
	unsigned r, s;
	off_t off;
	char tmp[256];

	/* Calculate the current offset into the file. */
	off = 0;
	if (pe->pe_dh != NULL)
		off += pe->pe_dh->dh_lfanew + 4;
	if (pe->pe_ch != NULL)
		off += sizeof(PE_CoffHdr) + pe->pe_ch->ch_optsize;

	STAILQ_FOREACH(ps, &pe->pe_scn, ps_next) {
		if (ps->ps_flags & LIBPE_F_LOAD_SECTION)
			continue;
		sh = &ps->ps_sh;

		/*
		 * For special files, we consume the padding in between
		 * and advance to the section offset.
		 */
		if (pe->pe_flags & LIBPE_F_SPECIAL_FILE) {
			/* Can't go backwards. */
			if (off > sh->sh_rawptr) {
				errno = EIO;
				return (-1);
			}
			if (off < sh->sh_rawptr) {
				r = sh->sh_rawptr - off;
				for (; r > 0; r -= s) {
					s = r > sizeof(tmp) ? sizeof(tmp) : r;
					if (read(pe->pe_fd, tmp, s) !=
					    (ssize_t) s) {
						errno = EIO;
						return (-1);
					}
				}
			}
		}

		/* Load the section content. */
		if (libpe_load_section(pe, ps) < 0)
			return (-1);
	}

	return (0);
}

int
libpe_resync_sections(PE *pe, off_t off)
{
	PE_Scn *ps;
	PE_SecHdr *sh;
	size_t falign, nsec;

	/* Firstly, sort all sections by their file offsets. */
	sort_sections(pe);

	/* Count the number of sections. */
	nsec = 0;
	STAILQ_FOREACH(ps, &pe->pe_scn, ps_next) {
		if (ps->ps_flags & LIBPE_F_STRIP_SECTION)
			continue;
		if (ps->ps_ndx & 0xFFFF0000U)
			continue;
		nsec++;
	}
	pe->pe_nscn = nsec;

	/*
	 * Calculate the file offset for the first section. (`off' is
	 * currently pointing to the COFF header.)
	 */
	off += sizeof(PE_CoffHdr);
	if (pe->pe_ch != NULL && pe->pe_ch->ch_optsize > 0)
		off += pe->pe_ch->ch_optsize;
	else {
		switch (pe->pe_obj) {
		case PE_O_PE32:
			off += PE_COFF_OPT_SIZE_32;
			break;
		case PE_O_PE32P:
			off += PE_COFF_OPT_SIZE_32P;
			break;
		case PE_O_COFF:
		default:
			break;
		}
	}
	off += nsec * sizeof(PE_SecHdr);

	/*
	 * Determine the file alignment for sections.
	 */
	if (pe->pe_oh != NULL && pe->pe_oh->oh_filealign > 0)
		falign = pe->pe_oh->oh_filealign;
	else {
		/*
		 * Use the default file alignment defined by the
		 * PE/COFF specification.
		 */
		if (pe->pe_obj == PE_O_COFF)
			falign = 4;
		else
			falign = 512;
	}

	/*
	 * Step through each section (and pseduo section) and verify
	 * alignment constraint and overlapping, make adjustment if need.
	 */
	pe->pe_rvamax = 0;
	STAILQ_FOREACH(ps, &pe->pe_scn, ps_next) {
		if (ps->ps_flags & LIBPE_F_STRIP_SECTION)
			continue;

		sh = &ps->ps_sh;

		if (sh->sh_addr + sh->sh_virtsize > pe->pe_rvamax)
			pe->pe_rvamax = sh->sh_addr + sh->sh_virtsize;

		if (ps->ps_ndx & 0xFFFF0000U)
			ps->ps_falign = 4;
		else
			ps->ps_falign = falign;

		off = roundup(off, ps->ps_falign);

		if (off != sh->sh_rawptr)
			ps->ps_flags |= PE_F_DIRTY;

		if (ps->ps_flags & PE_F_DIRTY) {
			if ((ps->ps_flags & LIBPE_F_LOAD_SECTION) == 0) {
				if (libpe_load_section(pe, ps) < 0)
					return (-1);
			}
			sh->sh_rawsize = libpe_resync_buffers(ps);
		}

		/*
		 * Sections only contains uninitialized data should set
		 * PointerToRawData to zero according to the PE/COFF
		 * specification.
		 */
		if (sh->sh_rawsize == 0)
			sh->sh_rawptr = 0;
		else
			sh->sh_rawptr = off;

		off += sh->sh_rawsize;
	}

	return (0);
}

off_t
libpe_write_section_headers(PE *pe, off_t off)
{
	char tmp[sizeof(PE_SecHdr)], *hdr;
	PE_Scn *ps;
	PE_SecHdr *sh;

	if (pe->pe_flags & LIBPE_F_BAD_SEC_HEADER || pe->pe_nscn == 0)
		return (off);

	if ((pe->pe_flags & LIBPE_F_DIRTY_SEC_HEADER) == 0) {
		off += sizeof(PE_SecHdr) * pe->pe_ch->ch_nsec;
		return (off);
	}

	STAILQ_FOREACH(ps, &pe->pe_scn, ps_next) {
		if (ps->ps_flags & LIBPE_F_STRIP_SECTION)
			continue;
		if (ps->ps_ndx & 0xFFFF0000U)
			continue;
		if ((pe->pe_flags & LIBPE_F_DIRTY_SEC_HEADER) == 0 &&
		    (ps->ps_flags & PE_F_DIRTY) == 0)
			goto next_header;

		sh = &ps->ps_sh;

		memcpy(tmp, sh->sh_name, sizeof(sh->sh_name));
		hdr = tmp + 8;
		PE_WRITE32(hdr, sh->sh_virtsize);
		PE_WRITE32(hdr, sh->sh_addr);
		PE_WRITE32(hdr, sh->sh_rawsize);
		PE_WRITE32(hdr, sh->sh_rawptr);
		PE_WRITE32(hdr, sh->sh_relocptr);
		PE_WRITE32(hdr, sh->sh_lineptr);
		PE_WRITE16(hdr, sh->sh_nreloc);
		PE_WRITE16(hdr, sh->sh_nline);
		PE_WRITE32(hdr, sh->sh_char);

		if (write(pe->pe_fd, tmp, sizeof(PE_SecHdr)) !=
		    (ssize_t) sizeof(PE_SecHdr)) {
			errno = EIO;
			return (-1);
		}

	next_header:
		off += sizeof(PE_SecHdr);
	}

	return (off);
}

off_t
libpe_write_sections(PE *pe, off_t off)
{
	PE_Scn *ps;
	PE_SecHdr *sh;

	if (pe->pe_flags & LIBPE_F_BAD_SEC_HEADER)
		return (off);

	STAILQ_FOREACH(ps, &pe->pe_scn, ps_next) {
		sh = &ps->ps_sh;

		if (ps->ps_flags & LIBPE_F_STRIP_SECTION)
			continue;

		/* Skip empty sections. */
		if (sh->sh_rawptr == 0 || sh->sh_rawsize == 0)
			continue;

		/*
		 * Padding between sections. (padding always written
		 * in case the the section headers or sections are
		 * moved or shrunk.)
		 */
		assert(off <= sh->sh_rawptr);
		if (off < sh->sh_rawptr)
			libpe_pad(pe, sh->sh_rawptr - off);

		if ((ps->ps_flags & PE_F_DIRTY) == 0) {
			assert((pe->pe_flags & LIBPE_F_SPECIAL_FILE) == 0);
			if (lseek(pe->pe_fd,
			    (off_t) (sh->sh_rawptr + sh->sh_rawsize),
			    SEEK_SET) < 0) {
				errno = EIO;
				return (-1);
			}
			off = sh->sh_rawptr + sh->sh_rawsize;
			continue;
		}

		off = sh->sh_rawptr;

		if (libpe_write_buffers(ps) < 0)
			return (-1);

		off += sh->sh_rawsize;

		ps->ps_flags &= ~PE_F_DIRTY;
	}

	return (off);
}
