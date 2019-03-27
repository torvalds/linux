/*-
 * Copyright (c) 2015 Kai Wang
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
#include <time.h>
#include <unistd.h>

#include "_libpe.h"

ELFTC_VCSID("$Id: libpe_coff.c 3326 2016-01-16 17:46:17Z kaiwang27 $");

int
libpe_parse_coff_header(PE *pe, char *hdr)
{
	char tmp[128];
	PE_CoffHdr *ch;
	PE_OptHdr *oh;
	PE_DataDir *dd;
	unsigned p, r, s;
	int i;

	if ((ch = malloc(sizeof(PE_CoffHdr))) == NULL) {
		errno = ENOMEM;
		return (-1);
	}

	PE_READ16(hdr, ch->ch_machine);
	PE_READ16(hdr, ch->ch_nsec);
	PE_READ32(hdr, ch->ch_timestamp);
	PE_READ32(hdr, ch->ch_symptr);
	PE_READ32(hdr, ch->ch_nsym);
	PE_READ16(hdr, ch->ch_optsize);
	PE_READ16(hdr, ch->ch_char);

	pe->pe_ch = ch;

	/*
	 * The Optional header is omitted for object files.
	 */
	if (ch->ch_optsize == 0)
		return (libpe_parse_section_headers(pe));

	if ((oh = calloc(1, sizeof(PE_OptHdr))) == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	pe->pe_oh = oh;

#define READ_OPT(n)							\
	do {								\
		/*							\
		 * Since the Optional Header size is variable, we must	\
		 * check if the requested read size will overrun the	\
		 * remaining header bytes.				\
		 */							\
		if (p + (n) > ch->ch_optsize) {				\
			/* Consume the "extra" bytes */			\
			r = ch->ch_optsize - p;				\
			if (read(pe->pe_fd, tmp, r) != (ssize_t) r) {	\
				pe->pe_flags |= LIBPE_F_BAD_SEC_HEADER;\
				return (0);				\
			}						\
			return (libpe_parse_section_headers(pe));	\
		}							\
		if (read(pe->pe_fd, tmp, (n)) != (ssize_t) (n)) {	\
			pe->pe_flags |= LIBPE_F_BAD_OPT_HEADER;	\
			return (0);					\
		}							\
		p += (n);						\
	} while (0)
#define	READ_OPT8(v) do { READ_OPT(1); (v) = *tmp; } while(0)
#define	READ_OPT16(v) do { READ_OPT(2); (v) = le16dec(tmp); } while(0)
#define	READ_OPT32(v) do { READ_OPT(4); (v) = le32dec(tmp); } while(0)
#define	READ_OPT64(v) do { READ_OPT(8); (v) = le64dec(tmp); } while(0)

	/*
	 * Read in the Optional header. Size of some fields are depending
	 * on the PE format specified by the oh_magic field. (PE32 or PE32+)
	 */

	p = 0;
	READ_OPT16(oh->oh_magic);
	if (oh->oh_magic == PE_FORMAT_32P)
		pe->pe_obj = PE_O_PE32P;
	READ_OPT8(oh->oh_ldvermajor);
	READ_OPT8(oh->oh_ldverminor);
	READ_OPT32(oh->oh_textsize);
	READ_OPT32(oh->oh_datasize);
	READ_OPT32(oh->oh_bsssize);
	READ_OPT32(oh->oh_entry);
	READ_OPT32(oh->oh_textbase);
	if (oh->oh_magic != PE_FORMAT_32P) {
		READ_OPT32(oh->oh_database);
		READ_OPT32(oh->oh_imgbase);
	} else
		READ_OPT64(oh->oh_imgbase);
	READ_OPT32(oh->oh_secalign);
	READ_OPT32(oh->oh_filealign);
	READ_OPT16(oh->oh_osvermajor);
	READ_OPT16(oh->oh_osverminor);
	READ_OPT16(oh->oh_imgvermajor);
	READ_OPT16(oh->oh_imgverminor);
	READ_OPT16(oh->oh_subvermajor);
	READ_OPT16(oh->oh_subverminor);
	READ_OPT32(oh->oh_win32ver);
	READ_OPT32(oh->oh_imgsize);
	READ_OPT32(oh->oh_hdrsize);
	READ_OPT32(oh->oh_checksum);
	READ_OPT16(oh->oh_subsystem);
	READ_OPT16(oh->oh_dllchar);
	if (oh->oh_magic != PE_FORMAT_32P) {
		READ_OPT32(oh->oh_stacksizer);
		READ_OPT32(oh->oh_stacksizec);
		READ_OPT32(oh->oh_heapsizer);
		READ_OPT32(oh->oh_heapsizec);
	} else {
		READ_OPT64(oh->oh_stacksizer);
		READ_OPT64(oh->oh_stacksizec);
		READ_OPT64(oh->oh_heapsizer);
		READ_OPT64(oh->oh_heapsizec);
	}
	READ_OPT32(oh->oh_ldrflags);
	READ_OPT32(oh->oh_ndatadir);

	/*
	 * Read in the Data Directories.
	 */

	if (oh->oh_ndatadir > 0) {
		if ((dd = calloc(1, sizeof(PE_DataDir))) == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		pe->pe_dd = dd;

		dd->dd_total = oh->oh_ndatadir < PE_DD_MAX ? oh->oh_ndatadir :
			PE_DD_MAX;

		for (i = 0; (uint32_t) i < dd->dd_total; i++) {
			READ_OPT32(dd->dd_e[i].de_addr);
			READ_OPT32(dd->dd_e[i].de_size);
		}
	}

	/* Consume the remaining bytes in the Optional header, if any. */
	if (ch->ch_optsize > p) {
		r = ch->ch_optsize - p;
		for (; r > 0; r -= s) {
			s = r > sizeof(tmp) ? sizeof(tmp) : r;
			if (read(pe->pe_fd, tmp, s) != (ssize_t) s) {
				pe->pe_flags |= LIBPE_F_BAD_SEC_HEADER;
				return (0);
			}
		}
	}

	return (libpe_parse_section_headers(pe));
}

off_t
libpe_write_pe_header(PE *pe, off_t off)
{
	char tmp[4];

	if (pe->pe_cmd == PE_C_RDWR &&
	    (pe->pe_flags & LIBPE_F_BAD_PE_HEADER) == 0) {
		assert(pe->pe_dh != NULL);
		off = lseek(pe->pe_fd, (off_t) pe->pe_dh->dh_lfanew + 4,
		    SEEK_SET);
		return (off);
	}

	/*
	 * PE Header should to be aligned on 8-byte boundary according to
	 * the PE/COFF specification.
	 */
	if ((off = libpe_align(pe, off, 8)) < 0)
		return (-1);

	le32enc(tmp, PE_SIGNATURE);
	if (write(pe->pe_fd, tmp, sizeof(tmp)) != (ssize_t) sizeof(tmp)) {
		errno = EIO;
		return (-1);
	}

	off += 4;

	pe->pe_flags &= ~LIBPE_F_BAD_PE_HEADER;

	/* Trigger rewrite for the following headers. */
	pe->pe_flags |= LIBPE_F_DIRTY_COFF_HEADER;
	pe->pe_flags |= LIBPE_F_DIRTY_OPT_HEADER;

	return (off);
}

off_t
libpe_write_coff_header(PE *pe, off_t off)
{
	char tmp[128], *hdr;
	PE_CoffHdr *ch;
	PE_DataDir *dd;
	PE_OptHdr *oh;
	PE_Scn *ps;
	PE_SecHdr *sh;
	unsigned p;
	uint32_t reloc_rva, reloc_sz;
	int i, reloc;

	reloc = 0;
	reloc_rva = reloc_sz = 0;

	if (pe->pe_cmd == PE_C_RDWR) {
		assert((pe->pe_flags & LIBPE_F_SPECIAL_FILE) == 0);

		if ((pe->pe_flags & LIBPE_F_DIRTY_COFF_HEADER) == 0 &&
		    (pe->pe_flags & LIBPE_F_BAD_COFF_HEADER) == 0) {
			if (lseek(pe->pe_fd, (off_t) sizeof(PE_CoffHdr),
			    SEEK_CUR) < 0) {
				errno = EIO;
				return (-1);
			}
			off += sizeof(PE_CoffHdr);
			assert(pe->pe_ch != NULL);
			ch = pe->pe_ch;
			goto coff_done;
		}

		/* lseek(2) to the offset of the COFF header. */
		if (lseek(pe->pe_fd, off, SEEK_SET) < 0) {
			errno = EIO;
			return (-1);
		}
	}

	if (pe->pe_ch == NULL) {
		if ((ch = calloc(1, sizeof(PE_CoffHdr))) == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		pe->pe_ch = ch;

		/*
		 * Default value for ch_machine if not provided by the
		 * application.
		 */
		if (pe->pe_obj == PE_O_PE32P)
			ch->ch_machine = IMAGE_FILE_MACHINE_AMD64;
		else
			ch->ch_machine = IMAGE_FILE_MACHINE_I386;

	} else
		ch = pe->pe_ch;

	if (!ch->ch_timestamp)
		ch->ch_timestamp = time(NULL);

	if (pe->pe_obj == PE_O_PE32) {
		if (!ch->ch_optsize)
			ch->ch_optsize = PE_COFF_OPT_SIZE_32;
		ch->ch_char |= IMAGE_FILE_EXECUTABLE_IMAGE |
		    IMAGE_FILE_32BIT_MACHINE;
	} else if (pe->pe_obj == PE_O_PE32P) {
		if (!ch->ch_optsize)
			ch->ch_optsize = PE_COFF_OPT_SIZE_32P;
		ch->ch_char |= IMAGE_FILE_EXECUTABLE_IMAGE |
		    IMAGE_FILE_LARGE_ADDRESS_AWARE;
	} else
		ch->ch_optsize = 0;

	/*
	 * COFF line number is deprecated by the PE/COFF
	 * specification. COFF symbol table is deprecated
	 * for executables.
	 */
	ch->ch_char |= IMAGE_FILE_LINE_NUMS_STRIPPED;
	if (pe->pe_obj == PE_O_PE32 || pe->pe_obj == PE_O_PE32P)
		ch->ch_char |= IMAGE_FILE_LOCAL_SYMS_STRIPPED;

	ch->ch_nsec = pe->pe_nscn;

	STAILQ_FOREACH(ps, &pe->pe_scn, ps_next) {
		sh = &ps->ps_sh;

		if (ps->ps_ndx == 0xFFFFFFFFU) {
			ch->ch_symptr = sh->sh_rawptr;
			ch->ch_nsym = pe->pe_nsym;
		}

		if (pe->pe_obj == PE_O_PE32 || pe->pe_obj == PE_O_PE32P) {
			if (ps->ps_ndx == (0xFFFF0000 | PE_DD_BASERELOC) ||
			    strncmp(sh->sh_name, ".reloc", strlen(".reloc")) ==
			    0) {
				reloc = 1;
				reloc_rva = sh->sh_addr;
				reloc_sz = sh->sh_virtsize;
			}
		}
	}

	if (!reloc)
		ch->ch_char |= IMAGE_FILE_RELOCS_STRIPPED;

	if (pe->pe_flags & LIBPE_F_BAD_OPT_HEADER) {
		if (pe->pe_obj == PE_O_PE32)
			ch->ch_optsize = PE_COFF_OPT_SIZE_32;
		else if (pe->pe_obj == PE_O_PE32P)
			ch->ch_optsize = PE_COFF_OPT_SIZE_32P;
		else
			ch->ch_optsize = 0;
	}

	/*
	 * Write the COFF header.
	 */
	hdr = tmp;
	PE_WRITE16(hdr, ch->ch_machine);
	PE_WRITE16(hdr, ch->ch_nsec);
	PE_WRITE32(hdr, ch->ch_timestamp);
	PE_WRITE32(hdr, ch->ch_symptr);
	PE_WRITE32(hdr, ch->ch_nsym);
	PE_WRITE16(hdr, ch->ch_optsize);
	PE_WRITE16(hdr, ch->ch_char);
	if (write(pe->pe_fd, tmp, sizeof(PE_CoffHdr)) !=
	    (ssize_t) sizeof(PE_CoffHdr)) {
		errno = EIO;
		return (-1);
	}

coff_done:
	off += sizeof(PE_CoffHdr);
	pe->pe_flags &= ~LIBPE_F_DIRTY_COFF_HEADER;
	pe->pe_flags &= ~LIBPE_F_BAD_COFF_HEADER;
	pe->pe_flags |= LIBPE_F_DIRTY_SEC_HEADER;

	if (ch->ch_optsize == 0)
		return (off);

	/*
	 * Write the Optional header.
	 */

	if (pe->pe_cmd == PE_C_RDWR) {
		if ((pe->pe_flags & LIBPE_F_DIRTY_OPT_HEADER) == 0 &&
		    (pe->pe_flags & LIBPE_F_BAD_OPT_HEADER) == 0) {
			if (lseek(pe->pe_fd, (off_t) ch->ch_optsize,
			    SEEK_CUR) < 0) {
				errno = EIO;
				return (-1);
			}
			off += ch->ch_optsize;
			return (off);
		}

	}

	if (pe->pe_oh == NULL) {
		if ((oh = calloc(1, sizeof(PE_OptHdr))) == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		pe->pe_oh = oh;
	} else
		oh = pe->pe_oh;

	if (pe->pe_obj == PE_O_PE32)
		oh->oh_magic = PE_FORMAT_32;
	else
		oh->oh_magic = PE_FORMAT_32P;

	/*
	 * LinkerVersion should not be less than 2.5, which will cause
	 * Windows to complain the executable is invalid in some case.
	 * By default we set LinkerVersion to 2.22 (binutils 2.22)
	 */
	if (!oh->oh_ldvermajor && !oh->oh_ldverminor) {
		oh->oh_ldvermajor = 2;
		oh->oh_ldverminor = 22;
	}

	/*
	 * The library always tries to write out all 16 data directories
	 * but the actual data dir written will depend on ch_optsize.
	 */
	oh->oh_ndatadir = PE_DD_MAX;

	if (!oh->oh_filealign)
		oh->oh_filealign = 0x200;
	if (!oh->oh_secalign)
		oh->oh_secalign = 0x1000;
	oh->oh_hdrsize = roundup(off + ch->ch_optsize + pe->pe_nscn *
	    sizeof(PE_SecHdr), oh->oh_filealign);
	oh->oh_imgsize = roundup(pe->pe_rvamax, oh->oh_secalign);

#define WRITE_OPT(n)							\
	do {								\
		/*							\
		 * Since the Optional Header size is variable, we must	\
		 * check if the requested write size will overrun the	\
		 * remaining header bytes.				\
		 */							\
		if (p + (n) > ch->ch_optsize) {				\
			/* Pad the "extra" bytes */			\
			if (libpe_pad(pe, ch->ch_optsize - p) < 0) {	\
				errno = EIO;				\
				return (-1);				\
			}						\
			goto opt_done;					\
		}							\
		if (write(pe->pe_fd, tmp, (n)) != (ssize_t) (n)) {	\
			errno = EIO;					\
			return (-1);					\
		}							\
		p += (n);						\
	} while (0)
#define	WRITE_OPT8(v) do { *tmp = (v); WRITE_OPT(1); } while(0)
#define	WRITE_OPT16(v) do { le16enc(tmp, (v)); WRITE_OPT(2); } while(0)
#define	WRITE_OPT32(v) do { le32enc(tmp, (v)); WRITE_OPT(4); } while(0)
#define	WRITE_OPT64(v) do { le64enc(tmp, (v)); WRITE_OPT(8); } while(0)

	p = 0;
	WRITE_OPT16(oh->oh_magic);
	if (oh->oh_magic == PE_FORMAT_32P)
		pe->pe_obj = PE_O_PE32P;
	WRITE_OPT8(oh->oh_ldvermajor);
	WRITE_OPT8(oh->oh_ldverminor);
	WRITE_OPT32(oh->oh_textsize);
	WRITE_OPT32(oh->oh_datasize);
	WRITE_OPT32(oh->oh_bsssize);
	WRITE_OPT32(oh->oh_entry);
	WRITE_OPT32(oh->oh_textbase);
	if (oh->oh_magic != PE_FORMAT_32P) {
		WRITE_OPT32(oh->oh_database);
		WRITE_OPT32(oh->oh_imgbase);
	} else
		WRITE_OPT64(oh->oh_imgbase);
	WRITE_OPT32(oh->oh_secalign);
	WRITE_OPT32(oh->oh_filealign);
	WRITE_OPT16(oh->oh_osvermajor);
	WRITE_OPT16(oh->oh_osverminor);
	WRITE_OPT16(oh->oh_imgvermajor);
	WRITE_OPT16(oh->oh_imgverminor);
	WRITE_OPT16(oh->oh_subvermajor);
	WRITE_OPT16(oh->oh_subverminor);
	WRITE_OPT32(oh->oh_win32ver);
	WRITE_OPT32(oh->oh_imgsize);
	WRITE_OPT32(oh->oh_hdrsize);
	WRITE_OPT32(oh->oh_checksum);
	WRITE_OPT16(oh->oh_subsystem);
	WRITE_OPT16(oh->oh_dllchar);
	if (oh->oh_magic != PE_FORMAT_32P) {
		WRITE_OPT32(oh->oh_stacksizer);
		WRITE_OPT32(oh->oh_stacksizec);
		WRITE_OPT32(oh->oh_heapsizer);
		WRITE_OPT32(oh->oh_heapsizec);
	} else {
		WRITE_OPT64(oh->oh_stacksizer);
		WRITE_OPT64(oh->oh_stacksizec);
		WRITE_OPT64(oh->oh_heapsizer);
		WRITE_OPT64(oh->oh_heapsizec);
	}
	WRITE_OPT32(oh->oh_ldrflags);
	WRITE_OPT32(oh->oh_ndatadir);

	/*
	 * Write the Data Directories.
	 */

	if (oh->oh_ndatadir > 0) {
		if (pe->pe_dd == NULL) {
			if ((dd = calloc(1, sizeof(PE_DataDir))) == NULL) {
				errno = ENOMEM;
				return (-1);
			}
			pe->pe_dd = dd;
			dd->dd_total = PE_DD_MAX;
		} else
			dd = pe->pe_dd;

		assert(oh->oh_ndatadir <= PE_DD_MAX);

		if (reloc) {
			dd->dd_e[PE_DD_BASERELOC].de_addr = reloc_rva;
			dd->dd_e[PE_DD_BASERELOC].de_size = reloc_sz;
		}

		for (i = 0; (uint32_t) i < dd->dd_total; i++) {
			WRITE_OPT32(dd->dd_e[i].de_addr);
			WRITE_OPT32(dd->dd_e[i].de_size);
		}
	}

	/* Pad the remaining bytes in the Optional header, if any. */
	if (ch->ch_optsize > p) {
		if (libpe_pad(pe, ch->ch_optsize - p) < 0) {
			errno = EIO;
			return (-1);
		}
	}

opt_done:
	off += ch->ch_optsize;
	pe->pe_flags &= ~LIBPE_F_DIRTY_OPT_HEADER;
	pe->pe_flags &= ~LIBPE_F_BAD_OPT_HEADER;
	pe->pe_flags |= LIBPE_F_DIRTY_SEC_HEADER;

	return (off);
}
