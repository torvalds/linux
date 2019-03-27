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
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "_libpe.h"

ELFTC_VCSID("$Id: libpe_dos.c 3312 2016-01-10 09:23:51Z kaiwang27 $");

int
libpe_parse_msdos_header(PE *pe, char *hdr)
{
	PE_DosHdr *dh;
	char coff[sizeof(PE_CoffHdr)];
	uint32_t pe_magic;
	int i;

	if ((pe->pe_stub = malloc(sizeof(PE_DosHdr))) == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	memcpy(pe->pe_stub, hdr, sizeof(PE_DosHdr));

	if ((dh = malloc(sizeof(*dh))) == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	pe->pe_dh = dh;

	/* Read the conventional MS-DOS EXE header. */
	memcpy(dh->dh_magic, hdr, 2);
	hdr += 2;
	PE_READ16(hdr, dh->dh_lastsize);
	PE_READ16(hdr, dh->dh_nblock);
	PE_READ16(hdr, dh->dh_nreloc);
	PE_READ16(hdr, dh->dh_hdrsize);
	PE_READ16(hdr, dh->dh_minalloc);
	PE_READ16(hdr, dh->dh_maxalloc);
	PE_READ16(hdr, dh->dh_ss);
	PE_READ16(hdr, dh->dh_sp);
	PE_READ16(hdr, dh->dh_checksum);
	PE_READ16(hdr, dh->dh_ip);
	PE_READ16(hdr, dh->dh_cs);
	PE_READ16(hdr, dh->dh_relocpos);
	PE_READ16(hdr, dh->dh_noverlay);

	/* Do not continue if the EXE is not a PE/NE/... (new executable) */
	if (dh->dh_relocpos != 0x40) {
		pe->pe_flags |= LIBPE_F_BAD_DOS_HEADER;
		return (0);
	}

	for (i = 0; i < 4; i++)
		PE_READ16(hdr, dh->dh_reserved1[i]);
	PE_READ16(hdr, dh->dh_oemid);
	PE_READ16(hdr, dh->dh_oeminfo);
	for (i = 0; i < 10; i++)
		PE_READ16(hdr, dh->dh_reserved2[i]);
	PE_READ32(hdr, dh->dh_lfanew);

	/* Check if the e_lfanew pointer is valid. */
	if (dh->dh_lfanew > pe->pe_fsize - 4) {
		pe->pe_flags |= LIBPE_F_BAD_DOS_HEADER;
		return (0);
	}

	if (dh->dh_lfanew < sizeof(PE_DosHdr) &&
	    (pe->pe_flags & LIBPE_F_SPECIAL_FILE)) {
		pe->pe_flags |= LIBPE_F_BAD_DOS_HEADER;
		return (0);
	}

	if (dh->dh_lfanew > sizeof(PE_DosHdr)) {
		pe->pe_stub_ex = dh->dh_lfanew - sizeof(PE_DosHdr);
		if (pe->pe_flags & LIBPE_F_SPECIAL_FILE) {
			/* Read in DOS stub now. */
			if (libpe_read_msdos_stub(pe) < 0) {
				pe->pe_flags |= LIBPE_F_BAD_DOS_HEADER;
				return (0);
			}
		}
	}

	if ((pe->pe_flags & LIBPE_F_SPECIAL_FILE) == 0) {
		/* Jump to the PE header. */
		if (lseek(pe->pe_fd, (off_t) dh->dh_lfanew, SEEK_SET) < 0) {
			pe->pe_flags |= LIBPE_F_BAD_PE_HEADER;
			return (0);
		}
	}

	if (read(pe->pe_fd, &pe_magic, 4) != 4 ||
	    htole32(pe_magic) != PE_SIGNATURE) {
		pe->pe_flags |= LIBPE_F_BAD_PE_HEADER;
		return (0);
	}

	if (read(pe->pe_fd, coff, sizeof(coff)) != (ssize_t) sizeof(coff)) {
		pe->pe_flags |= LIBPE_F_BAD_COFF_HEADER;
		return (0);
	}

	return (libpe_parse_coff_header(pe, coff));
}

int
libpe_read_msdos_stub(PE *pe)
{
	void *m;

	assert(pe->pe_stub_ex > 0 &&
	    (pe->pe_flags & LIBPE_F_LOAD_DOS_STUB) == 0);

	if ((pe->pe_flags & LIBPE_F_SPECIAL_FILE) == 0) {
		if (lseek(pe->pe_fd, (off_t) sizeof(PE_DosHdr), SEEK_SET) <
		    0) {
			errno = EIO;
			goto fail;
		}
	}

	if ((m = realloc(pe->pe_stub, sizeof(PE_DosHdr) + pe->pe_stub_ex)) ==
	    NULL) {
		errno = ENOMEM;
		goto fail;
	}
	pe->pe_stub = m;

	if (read(pe->pe_fd, pe->pe_stub + sizeof(PE_DosHdr), pe->pe_stub_ex) !=
	    (ssize_t) pe->pe_stub_ex) {
		errno = EIO;
		goto fail;
	}

	pe->pe_flags |= LIBPE_F_LOAD_DOS_STUB;

	/* Search for the Rich header embedded just before the PE header. */
	(void) libpe_parse_rich_header(pe);

	return (0);

fail:
	pe->pe_stub_ex = 0;

	return (-1);
}

/*
 * The "standard" MS-DOS stub displaying "This program cannot be run in
 * DOS mode".
 */
static const char msdos_stub[] = {
    '\x0e','\x1f','\xba','\x0e','\x00','\xb4','\x09','\xcd',
    '\x21','\xb8','\x01','\x4c','\xcd','\x21','\x54','\x68',
    '\x69','\x73','\x20','\x70','\x72','\x6f','\x67','\x72',
    '\x61','\x6d','\x20','\x63','\x61','\x6e','\x6e','\x6f',
    '\x74','\x20','\x62','\x65','\x20','\x72','\x75','\x6e',
    '\x20','\x69','\x6e','\x20','\x44','\x4f','\x53','\x20',
    '\x6d','\x6f','\x64','\x65','\x2e','\x0d','\x0d','\x0a',
    '\x24','\x00','\x00','\x00','\x00','\x00','\x00','\x00',
};

static void
init_dos_header(PE_DosHdr *dh)
{

	dh->dh_magic[0] = 'M';
	dh->dh_magic[1] = 'Z';
	dh->dh_lastsize = 144;
	dh->dh_nblock = 3;
	dh->dh_hdrsize = 4;
	dh->dh_maxalloc = 65535;
	dh->dh_sp = 184;
	dh->dh_relocpos = 0x40;
	dh->dh_lfanew = 0x80;
}

off_t
libpe_write_msdos_stub(PE *pe, off_t off)
{
	PE_DosHdr *dh;
	char tmp[sizeof(PE_DosHdr)], *hdr;
	off_t d;
	int i, strip_rich;

	strip_rich = 0;

	if (pe->pe_cmd == PE_C_RDWR) {
		assert((pe->pe_flags & LIBPE_F_SPECIAL_FILE) == 0);

		if (pe->pe_dh != NULL &&
		    (pe->pe_flags & PE_F_STRIP_DOS_STUB)) {
			/*
			 * If we strip MS-DOS stub, everything after it
			 * needs rewritten.
			 */
			pe->pe_flags |= LIBPE_F_BAD_PE_HEADER;
			goto done;
		}

		/*
		 * lseek(2) to the PE signature if MS-DOS stub is not
		 * modified.
		 */
		if (pe->pe_dh != NULL &&
		    (pe->pe_flags & LIBPE_F_DIRTY_DOS_HEADER) == 0 &&
		    (pe->pe_flags & LIBPE_F_BAD_DOS_HEADER) == 0 &&
		    (pe->pe_flags & PE_F_STRIP_RICH_HEADER) == 0) {
			if (lseek(pe->pe_fd,
			    (off_t) (sizeof(PE_DosHdr) + pe->pe_stub_ex),
			    SEEK_CUR) < 0) {
				errno = EIO;
				return (-1);
			}
			off = sizeof(PE_DosHdr) + pe->pe_stub_ex;
			goto done;
		}

		/* Check if we should strip the Rich header. */
		if (pe->pe_dh != NULL && pe->pe_stub_app == NULL &&
		    (pe->pe_flags & LIBPE_F_BAD_DOS_HEADER) == 0 &&
		    (pe->pe_flags & PE_F_STRIP_RICH_HEADER)) {
			if ((pe->pe_flags & LIBPE_F_LOAD_DOS_STUB) == 0) {
				(void) libpe_read_msdos_stub(pe);
				if (lseek(pe->pe_fd, off, SEEK_SET) < 0) {
					errno = EIO;
					return (-1);
				}
			}
			if (pe->pe_rh != NULL) {
				strip_rich = 1;
				pe->pe_flags |= LIBPE_F_DIRTY_DOS_HEADER;
			}
		}

		/*
		 * If length of MS-DOS stub will change, Mark the PE
		 * signature is broken so that the PE signature and the
		 * headers follow it will be rewritten.
		 *
		 * The sections should be loaded now since the stub might
		 * overwrite the section data.
		 */
		if ((pe->pe_flags & LIBPE_F_BAD_DOS_HEADER) ||
		    (pe->pe_stub_app != NULL && pe->pe_stub_app_sz !=
			sizeof(PE_DosHdr) + pe->pe_stub_ex) || strip_rich) {
			if (libpe_load_all_sections(pe) < 0)
				return (-1);
			if (lseek(pe->pe_fd, off, SEEK_SET) < 0) {
				errno = EIO;
				return (-1);
			}
			pe->pe_flags |= LIBPE_F_BAD_PE_HEADER;
		}
	}

	if (pe->pe_flags & PE_F_STRIP_DOS_STUB)
		goto done;

	/* Always use application supplied MS-DOS stub, if exists. */
	if (pe->pe_stub_app != NULL && pe->pe_stub_app_sz > 0) {
		if (write(pe->pe_fd, pe->pe_stub_app, pe->pe_stub_app_sz) !=
		    (ssize_t) pe->pe_stub_app_sz) {
			errno = EIO;
			return (-1);
		}
		off = pe->pe_stub_app_sz;
		goto done;
	}

	/*
	 * Write MS-DOS header.
	 */

	if (pe->pe_dh == NULL) {
		if ((dh = calloc(1, sizeof(PE_DosHdr))) == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		pe->pe_dh = dh;

		init_dos_header(dh);

		pe->pe_flags |= LIBPE_F_DIRTY_DOS_HEADER;
	} else
		dh = pe->pe_dh;

	if (pe->pe_flags & LIBPE_F_BAD_DOS_HEADER)
		init_dos_header(dh);

	if (strip_rich) {
		d = pe->pe_rh_start - pe->pe_stub;
		dh->dh_lfanew = roundup(d, 8);
	}

	if ((pe->pe_flags & LIBPE_F_DIRTY_DOS_HEADER) ||
	    (pe->pe_flags & LIBPE_F_BAD_DOS_HEADER)) {
		memcpy(tmp, dh->dh_magic, 2);
		hdr = tmp + 2;
		PE_WRITE16(hdr, dh->dh_lastsize);
		PE_WRITE16(hdr, dh->dh_nblock);
		PE_WRITE16(hdr, dh->dh_nreloc);
		PE_WRITE16(hdr, dh->dh_hdrsize);
		PE_WRITE16(hdr, dh->dh_minalloc);
		PE_WRITE16(hdr, dh->dh_maxalloc);
		PE_WRITE16(hdr, dh->dh_ss);
		PE_WRITE16(hdr, dh->dh_sp);
		PE_WRITE16(hdr, dh->dh_checksum);
		PE_WRITE16(hdr, dh->dh_ip);
		PE_WRITE16(hdr, dh->dh_cs);
		PE_WRITE16(hdr, dh->dh_relocpos);
		PE_WRITE16(hdr, dh->dh_noverlay);
		for (i = 0; i < 4; i++)
			PE_WRITE16(hdr, dh->dh_reserved1[i]);
		PE_WRITE16(hdr, dh->dh_oemid);
		PE_WRITE16(hdr, dh->dh_oeminfo);
		for (i = 0; i < 10; i++)
			PE_WRITE16(hdr, dh->dh_reserved2[i]);
		PE_WRITE32(hdr, dh->dh_lfanew);

		if (write(pe->pe_fd, tmp, sizeof(tmp)) !=
		    (ssize_t) sizeof(tmp)) {
			errno = EIO;
			return (-1);
		}
	} else {
		assert((pe->pe_flags & LIBPE_F_SPECIAL_FILE) == 0);
		if (lseek(pe->pe_fd, (off_t) sizeof(PE_DosHdr), SEEK_CUR) <
		    0) {
			errno = EIO;
			return (-1);
		}
	}

	off = sizeof(PE_DosHdr);

	/*
	 * Write the MS-DOS stub.
	 */

	if (strip_rich) {
		assert((pe->pe_flags & LIBPE_F_SPECIAL_FILE) == 0);
		assert(pe->pe_stub != NULL && pe->pe_rh_start != NULL);
		d = pe->pe_rh_start - pe->pe_stub;
		if (lseek(pe->pe_fd, d, SEEK_SET) < 0) {
			errno = EIO;
			return (-1);
		}
		off = d;
		goto done;
	}

	if (pe->pe_cmd == PE_C_RDWR) {
		if (lseek(pe->pe_fd, (off_t) pe->pe_stub_ex, SEEK_CUR) < 0) {
			errno = EIO;
			return (-1);
		}
		off += pe->pe_stub_ex;
		goto done;
	}

	if (write(pe->pe_fd, msdos_stub, sizeof(msdos_stub)) !=
	    (ssize_t) sizeof(msdos_stub)) {
		errno = EIO;
		return (-1);
	}
	off += sizeof(msdos_stub);

done:
	pe->pe_flags &= ~LIBPE_F_DIRTY_DOS_HEADER;
	pe->pe_flags &= ~LIBPE_F_BAD_DOS_HEADER;

	return (off);
}
