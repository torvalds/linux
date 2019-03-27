/*
 * Copyright (c) Christos Zoulas 2003.
 * All Rights Reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "file.h"

#ifndef lint
FILE_RCSID("@(#)$File: readelf.c,v 1.144 2018/07/08 23:37:33 christos Exp $")
#endif

#ifdef BUILTIN_ELF
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "readelf.h"
#include "magic.h"

#ifdef	ELFCORE
private int dophn_core(struct magic_set *, int, int, int, off_t, int, size_t,
    off_t, int *, uint16_t *);
#endif
private int dophn_exec(struct magic_set *, int, int, int, off_t, int, size_t,
    off_t, int, int *, uint16_t *);
private int doshn(struct magic_set *, int, int, int, off_t, int, size_t,
    off_t, int, int, int *, uint16_t *);
private size_t donote(struct magic_set *, void *, size_t, size_t, int,
    int, size_t, int *, uint16_t *, int, off_t, int, off_t);

#define	ELF_ALIGN(a)	((((a) + align - 1) / align) * align)

#define isquote(c) (strchr("'\"`", (c)) != NULL)

private uint16_t getu16(int, uint16_t);
private uint32_t getu32(int, uint32_t);
private uint64_t getu64(int, uint64_t);

#define MAX_PHNUM	128
#define	MAX_SHNUM	32768
#define SIZE_UNKNOWN	CAST(off_t, -1)

private int
toomany(struct magic_set *ms, const char *name, uint16_t num)
{
	if (file_printf(ms, ", too many %s (%u)", name, num) == -1)
		return -1;
	return 0;
}

private uint16_t
getu16(int swap, uint16_t value)
{
	union {
		uint16_t ui;
		char c[2];
	} retval, tmpval;

	if (swap) {
		tmpval.ui = value;

		retval.c[0] = tmpval.c[1];
		retval.c[1] = tmpval.c[0];
		
		return retval.ui;
	} else
		return value;
}

private uint32_t
getu32(int swap, uint32_t value)
{
	union {
		uint32_t ui;
		char c[4];
	} retval, tmpval;

	if (swap) {
		tmpval.ui = value;

		retval.c[0] = tmpval.c[3];
		retval.c[1] = tmpval.c[2];
		retval.c[2] = tmpval.c[1];
		retval.c[3] = tmpval.c[0];
		
		return retval.ui;
	} else
		return value;
}

private uint64_t
getu64(int swap, uint64_t value)
{
	union {
		uint64_t ui;
		char c[8];
	} retval, tmpval;

	if (swap) {
		tmpval.ui = value;

		retval.c[0] = tmpval.c[7];
		retval.c[1] = tmpval.c[6];
		retval.c[2] = tmpval.c[5];
		retval.c[3] = tmpval.c[4];
		retval.c[4] = tmpval.c[3];
		retval.c[5] = tmpval.c[2];
		retval.c[6] = tmpval.c[1];
		retval.c[7] = tmpval.c[0];
		
		return retval.ui;
	} else
		return value;
}

#define elf_getu16(swap, value) getu16(swap, value)
#define elf_getu32(swap, value) getu32(swap, value)
#define elf_getu64(swap, value) getu64(swap, value)

#define xsh_addr	(clazz == ELFCLASS32			\
			 ? CAST(void *, &sh32)			\
			 : CAST(void *, &sh64))
#define xsh_sizeof	(clazz == ELFCLASS32			\
			 ? sizeof(sh32)				\
			 : sizeof(sh64))
#define xsh_size	CAST(size_t, (clazz == ELFCLASS32	\
			 ? elf_getu32(swap, sh32.sh_size)	\
			 : elf_getu64(swap, sh64.sh_size)))
#define xsh_offset	CAST(off_t, (clazz == ELFCLASS32	\
			 ? elf_getu32(swap, sh32.sh_offset)	\
			 : elf_getu64(swap, sh64.sh_offset)))
#define xsh_type	(clazz == ELFCLASS32			\
			 ? elf_getu32(swap, sh32.sh_type)	\
			 : elf_getu32(swap, sh64.sh_type))
#define xsh_name    	(clazz == ELFCLASS32			\
			 ? elf_getu32(swap, sh32.sh_name)	\
			 : elf_getu32(swap, sh64.sh_name))

#define xph_addr	(clazz == ELFCLASS32			\
			 ? CAST(void *, &ph32)			\
			 : CAST(void *, &ph64))
#define xph_sizeof	(clazz == ELFCLASS32			\
			 ? sizeof(ph32)				\
			 : sizeof(ph64))
#define xph_type	(clazz == ELFCLASS32			\
			 ? elf_getu32(swap, ph32.p_type)	\
			 : elf_getu32(swap, ph64.p_type))
#define xph_offset	CAST(off_t, (clazz == ELFCLASS32	\
			 ? elf_getu32(swap, ph32.p_offset)	\
			 : elf_getu64(swap, ph64.p_offset)))
#define xph_align	CAST(size_t, (clazz == ELFCLASS32	\
			 ? CAST(off_t, (ph32.p_align ? 		\
			    elf_getu32(swap, ph32.p_align) : 4))\
			 : CAST(off_t, (ph64.p_align ?		\
			    elf_getu64(swap, ph64.p_align) : 4))))
#define xph_vaddr	CAST(size_t, (clazz == ELFCLASS32	\
			 ? CAST(off_t, (ph32.p_vaddr ? 		\
			    elf_getu32(swap, ph32.p_vaddr) : 4))\
			 : CAST(off_t, (ph64.p_vaddr ?		\
			    elf_getu64(swap, ph64.p_vaddr) : 4))))
#define xph_filesz	CAST(size_t, (clazz == ELFCLASS32	\
			 ? elf_getu32(swap, ph32.p_filesz)	\
			 : elf_getu64(swap, ph64.p_filesz)))
#define xph_memsz	CAST(size_t, ((clazz == ELFCLASS32	\
			 ? elf_getu32(swap, ph32.p_memsz)	\
			 : elf_getu64(swap, ph64.p_memsz))))
#define xnh_addr	(clazz == ELFCLASS32			\
			 ? CAST(void *, &nh32)			\
			 : CAST(void *, &nh64))
#define xnh_sizeof	(clazz == ELFCLASS32			\
			 ? sizeof(nh32)				\
			 : sizeof(nh64))
#define xnh_type	(clazz == ELFCLASS32			\
			 ? elf_getu32(swap, nh32.n_type)	\
			 : elf_getu32(swap, nh64.n_type))
#define xnh_namesz	(clazz == ELFCLASS32			\
			 ? elf_getu32(swap, nh32.n_namesz)	\
			 : elf_getu32(swap, nh64.n_namesz))
#define xnh_descsz	(clazz == ELFCLASS32			\
			 ? elf_getu32(swap, nh32.n_descsz)	\
			 : elf_getu32(swap, nh64.n_descsz))

#define xdh_addr	(clazz == ELFCLASS32			\
			 ? CAST(void *, &dh32)			\
			 : CAST(void *, &dh64))
#define xdh_sizeof	(clazz == ELFCLASS32			\
			 ? sizeof(dh32)				\
			 : sizeof(dh64))
#define xdh_tag		(clazz == ELFCLASS32			\
			 ? elf_getu32(swap, dh32.d_tag)		\
			 : elf_getu64(swap, dh64.d_tag))
#define xdh_val		(clazz == ELFCLASS32			\
			 ? elf_getu32(swap, dh32.d_un.d_val)	\
			 : elf_getu64(swap, dh64.d_un.d_val))

#define xcap_addr	(clazz == ELFCLASS32			\
			 ? CAST(void *, &cap32)			\
			 : CAST(void *, &cap64))
#define xcap_sizeof	(clazz == ELFCLASS32			\
			 ? sizeof(cap32)			\
			 : sizeof(cap64))
#define xcap_tag	(clazz == ELFCLASS32			\
			 ? elf_getu32(swap, cap32.c_tag)	\
			 : elf_getu64(swap, cap64.c_tag))
#define xcap_val	(clazz == ELFCLASS32			\
			 ? elf_getu32(swap, cap32.c_un.c_val)	\
			 : elf_getu64(swap, cap64.c_un.c_val))

#define xauxv_addr	(clazz == ELFCLASS32			\
			 ? CAST(void *, &auxv32)		\
			 : CAST(void *, &auxv64))
#define xauxv_sizeof	(clazz == ELFCLASS32			\
			 ? sizeof(auxv32)			\
			 : sizeof(auxv64))
#define xauxv_type	(clazz == ELFCLASS32			\
			 ? elf_getu32(swap, auxv32.a_type)	\
			 : elf_getu64(swap, auxv64.a_type))
#define xauxv_val	(clazz == ELFCLASS32			\
			 ? elf_getu32(swap, auxv32.a_v)		\
			 : elf_getu64(swap, auxv64.a_v))

#define prpsoffsets(i)	(clazz == ELFCLASS32			\
			 ? prpsoffsets32[i]			\
			 : prpsoffsets64[i])

#ifdef ELFCORE
/*
 * Try larger offsets first to avoid false matches
 * from earlier data that happen to look like strings.
 */
static const size_t	prpsoffsets32[] = {
#ifdef USE_NT_PSINFO
	104,		/* SunOS 5.x (command line) */
	88,		/* SunOS 5.x (short name) */
#endif /* USE_NT_PSINFO */

	100,		/* SunOS 5.x (command line) */
	84,		/* SunOS 5.x (short name) */

	44,		/* Linux (command line) */
	28,		/* Linux 2.0.36 (short name) */

	8,		/* FreeBSD */
};

static const size_t	prpsoffsets64[] = {
#ifdef USE_NT_PSINFO
	152,		/* SunOS 5.x (command line) */
	136,		/* SunOS 5.x (short name) */
#endif /* USE_NT_PSINFO */

	136,		/* SunOS 5.x, 64-bit (command line) */
	120,		/* SunOS 5.x, 64-bit (short name) */

	56,		/* Linux (command line) */
	40,             /* Linux (tested on core from 2.4.x, short name) */

	16,		/* FreeBSD, 64-bit */
};

#define	NOFFSETS32	(sizeof(prpsoffsets32) / sizeof(prpsoffsets32[0]))
#define NOFFSETS64	(sizeof(prpsoffsets64) / sizeof(prpsoffsets64[0]))

#define NOFFSETS	(clazz == ELFCLASS32 ? NOFFSETS32 : NOFFSETS64)

/*
 * Look through the program headers of an executable image, searching
 * for a PT_NOTE section of type NT_PRPSINFO, with a name "CORE" or
 * "FreeBSD"; if one is found, try looking in various places in its
 * contents for a 16-character string containing only printable
 * characters - if found, that string should be the name of the program
 * that dropped core.  Note: right after that 16-character string is,
 * at least in SunOS 5.x (and possibly other SVR4-flavored systems) and
 * Linux, a longer string (80 characters, in 5.x, probably other
 * SVR4-flavored systems, and Linux) containing the start of the
 * command line for that program.
 *
 * SunOS 5.x core files contain two PT_NOTE sections, with the types
 * NT_PRPSINFO (old) and NT_PSINFO (new).  These structs contain the
 * same info about the command name and command line, so it probably
 * isn't worthwhile to look for NT_PSINFO, but the offsets are provided
 * above (see USE_NT_PSINFO), in case we ever decide to do so.  The
 * NT_PRPSINFO and NT_PSINFO sections are always in order and adjacent;
 * the SunOS 5.x file command relies on this (and prefers the latter).
 *
 * The signal number probably appears in a section of type NT_PRSTATUS,
 * but that's also rather OS-dependent, in ways that are harder to
 * dissect with heuristics, so I'm not bothering with the signal number.
 * (I suppose the signal number could be of interest in situations where
 * you don't have the binary of the program that dropped core; if you
 * *do* have that binary, the debugger will probably tell you what
 * signal it was.)
 */

#define	OS_STYLE_SVR4		0
#define	OS_STYLE_FREEBSD	1
#define	OS_STYLE_NETBSD		2

private const char os_style_names[][8] = {
	"SVR4",
	"FreeBSD",
	"NetBSD",
};

#define FLAGS_CORE_STYLE		0x0003

#define FLAGS_DID_CORE			0x0004
#define FLAGS_DID_OS_NOTE		0x0008
#define FLAGS_DID_BUILD_ID		0x0010
#define FLAGS_DID_CORE_STYLE		0x0020
#define FLAGS_DID_NETBSD_PAX		0x0040
#define FLAGS_DID_NETBSD_MARCH		0x0080
#define FLAGS_DID_NETBSD_CMODEL		0x0100
#define FLAGS_DID_NETBSD_EMULATION	0x0200
#define FLAGS_DID_NETBSD_UNKNOWN	0x0400
#define FLAGS_IS_CORE			0x0800
#define FLAGS_DID_AUXV			0x1000

private int
dophn_core(struct magic_set *ms, int clazz, int swap, int fd, off_t off,
    int num, size_t size, off_t fsize, int *flags, uint16_t *notecount)
{
	Elf32_Phdr ph32;
	Elf64_Phdr ph64;
	size_t offset, len;
	unsigned char nbuf[BUFSIZ];
	ssize_t bufsize;
	off_t ph_off = off;
	int ph_num = num;

	if (size != xph_sizeof) {
		if (file_printf(ms, ", corrupted program header size") == -1)
			return -1;
		return 0;
	}

	/*
	 * Loop through all the program headers.
	 */
	for ( ; num; num--) {
		if (pread(fd, xph_addr, xph_sizeof, off) <
		    CAST(ssize_t, xph_sizeof)) {
			file_badread(ms);
			return -1;
		}
		off += size;

		if (fsize != SIZE_UNKNOWN && xph_offset > fsize) {
			/* Perhaps warn here */
			continue;
		}

		if (xph_type != PT_NOTE)
			continue;

		/*
		 * This is a PT_NOTE section; loop through all the notes
		 * in the section.
		 */
		len = xph_filesz < sizeof(nbuf) ? xph_filesz : sizeof(nbuf);
		if ((bufsize = pread(fd, nbuf, len, xph_offset)) == -1) {
			file_badread(ms);
			return -1;
		}
		offset = 0;
		for (;;) {
			if (offset >= (size_t)bufsize)
				break;
			offset = donote(ms, nbuf, offset, (size_t)bufsize,
			    clazz, swap, 4, flags, notecount, fd, ph_off,
			    ph_num, fsize);
			if (offset == 0)
				break;

		}
	}
	return 0;
}
#endif

static void
do_note_netbsd_version(struct magic_set *ms, int swap, void *v)
{
	uint32_t desc;
	memcpy(&desc, v, sizeof(desc));
	desc = elf_getu32(swap, desc);

	if (file_printf(ms, ", for NetBSD") == -1)
		return;
	/*
	 * The version number used to be stuck as 199905, and was thus
	 * basically content-free.  Newer versions of NetBSD have fixed
	 * this and now use the encoding of __NetBSD_Version__:
	 *
	 *	MMmmrrpp00
	 *
	 * M = major version
	 * m = minor version
	 * r = release ["",A-Z,Z[A-Z] but numeric]
	 * p = patchlevel
	 */
	if (desc > 100000000U) {
		uint32_t ver_patch = (desc / 100) % 100;
		uint32_t ver_rel = (desc / 10000) % 100;
		uint32_t ver_min = (desc / 1000000) % 100;
		uint32_t ver_maj = desc / 100000000;

		if (file_printf(ms, " %u.%u", ver_maj, ver_min) == -1)
			return;
		if (ver_rel == 0 && ver_patch != 0) {
			if (file_printf(ms, ".%u", ver_patch) == -1)
				return;
		} else if (ver_rel != 0) {
			while (ver_rel > 26) {
				if (file_printf(ms, "Z") == -1)
					return;
				ver_rel -= 26;
			}
			if (file_printf(ms, "%c", 'A' + ver_rel - 1)
			    == -1)
				return;
		}
	}
}

static void
do_note_freebsd_version(struct magic_set *ms, int swap, void *v)
{
	uint32_t desc;

	memcpy(&desc, v, sizeof(desc));
	desc = elf_getu32(swap, desc);
	if (file_printf(ms, ", for FreeBSD") == -1)
		return;

	/*
	 * Contents is __FreeBSD_version, whose relation to OS
	 * versions is defined by a huge table in the Porter's
	 * Handbook.  This is the general scheme:
	 * 
	 * Releases:
	 * 	Mmp000 (before 4.10)
	 * 	Mmi0p0 (before 5.0)
	 * 	Mmm0p0
	 * 
	 * Development branches:
	 * 	Mmpxxx (before 4.6)
	 * 	Mmp1xx (before 4.10)
	 * 	Mmi1xx (before 5.0)
	 * 	M000xx (pre-M.0)
	 * 	Mmm1xx
	 * 
	 * M = major version
	 * m = minor version
	 * i = minor version increment (491000 -> 4.10)
	 * p = patchlevel
	 * x = revision
	 * 
	 * The first release of FreeBSD to use ELF by default
	 * was version 3.0.
	 */
	if (desc == 460002) {
		if (file_printf(ms, " 4.6.2") == -1)
			return;
	} else if (desc < 460100) {
		if (file_printf(ms, " %d.%d", desc / 100000,
		    desc / 10000 % 10) == -1)
			return;
		if (desc / 1000 % 10 > 0)
			if (file_printf(ms, ".%d", desc / 1000 % 10) == -1)
				return;
		if ((desc % 1000 > 0) || (desc % 100000 == 0))
			if (file_printf(ms, " (%d)", desc) == -1)
				return;
	} else if (desc < 500000) {
		if (file_printf(ms, " %d.%d", desc / 100000,
		    desc / 10000 % 10 + desc / 1000 % 10) == -1)
			return;
		if (desc / 100 % 10 > 0) {
			if (file_printf(ms, " (%d)", desc) == -1)
				return;
		} else if (desc / 10 % 10 > 0) {
			if (file_printf(ms, ".%d", desc / 10 % 10) == -1)
				return;
		}
	} else {
		if (file_printf(ms, " %d.%d", desc / 100000,
		    desc / 1000 % 100) == -1)
			return;
		if ((desc / 100 % 10 > 0) ||
		    (desc % 100000 / 100 == 0)) {
			if (file_printf(ms, " (%d)", desc) == -1)
				return;
		} else if (desc / 10 % 10 > 0) {
			if (file_printf(ms, ".%d", desc / 10 % 10) == -1)
				return;
		}
	}
}

private int
/*ARGSUSED*/
do_bid_note(struct magic_set *ms, unsigned char *nbuf, uint32_t type,
    int swap __attribute__((__unused__)), uint32_t namesz, uint32_t descsz,
    size_t noff, size_t doff, int *flags)
{
	if (namesz == 4 && strcmp((char *)&nbuf[noff], "GNU") == 0 &&
	    type == NT_GNU_BUILD_ID && (descsz >= 4 && descsz <= 20)) {
		uint8_t desc[20];
		const char *btype;
		uint32_t i;
		*flags |= FLAGS_DID_BUILD_ID;
		switch (descsz) {
		case 8:
		    btype = "xxHash";
		    break;
		case 16:
		    btype = "md5/uuid";
		    break;
		case 20:
		    btype = "sha1";
		    break;
		default:
		    btype = "unknown";
		    break;
		}
		if (file_printf(ms, ", BuildID[%s]=", btype) == -1)
			return 1;
		memcpy(desc, &nbuf[doff], descsz);
		for (i = 0; i < descsz; i++)
		    if (file_printf(ms, "%02x", desc[i]) == -1)
			return 1;
		return 1;
	}
	if (namesz == 4 && strcmp((char *)&nbuf[noff], "Go") == 0 &&
	    type == NT_GO_BUILD_ID && descsz < 128) {
		if (file_printf(ms, ", Go BuildID=%s",
		    (char *)&nbuf[doff]) == -1)
			return 1;
		return 1;
	}
	return 0;
}

private int
do_os_note(struct magic_set *ms, unsigned char *nbuf, uint32_t type,
    int swap, uint32_t namesz, uint32_t descsz,
    size_t noff, size_t doff, int *flags)
{
	if (namesz == 5 && strcmp((char *)&nbuf[noff], "SuSE") == 0 &&
	    type == NT_GNU_VERSION && descsz == 2) {
	    *flags |= FLAGS_DID_OS_NOTE;
	    file_printf(ms, ", for SuSE %d.%d", nbuf[doff], nbuf[doff + 1]);
	    return 1;
	}

	if (namesz == 4 && strcmp((char *)&nbuf[noff], "GNU") == 0 &&
	    type == NT_GNU_VERSION && descsz == 16) {
		uint32_t desc[4];
		memcpy(desc, &nbuf[doff], sizeof(desc));

		*flags |= FLAGS_DID_OS_NOTE;
		if (file_printf(ms, ", for GNU/") == -1)
			return 1;
		switch (elf_getu32(swap, desc[0])) {
		case GNU_OS_LINUX:
			if (file_printf(ms, "Linux") == -1)
				return 1;
			break;
		case GNU_OS_HURD:
			if (file_printf(ms, "Hurd") == -1)
				return 1;
			break;
		case GNU_OS_SOLARIS:
			if (file_printf(ms, "Solaris") == -1)
				return 1;
			break;
		case GNU_OS_KFREEBSD:
			if (file_printf(ms, "kFreeBSD") == -1)
				return 1;
			break;
		case GNU_OS_KNETBSD:
			if (file_printf(ms, "kNetBSD") == -1)
				return 1;
			break;
		default:
			if (file_printf(ms, "<unknown>") == -1)
				return 1; 
		}
		if (file_printf(ms, " %d.%d.%d", elf_getu32(swap, desc[1]),
		    elf_getu32(swap, desc[2]), elf_getu32(swap, desc[3])) == -1)
			return 1;
		return 1;
	}

	if (namesz == 7 && strcmp((char *)&nbuf[noff], "NetBSD") == 0) {
	    	if (type == NT_NETBSD_VERSION && descsz == 4) {
			*flags |= FLAGS_DID_OS_NOTE;
			do_note_netbsd_version(ms, swap, &nbuf[doff]);
			return 1;
		}
	}

	if (namesz == 8 && strcmp((char *)&nbuf[noff], "FreeBSD") == 0) {
	    	if (type == NT_FREEBSD_VERSION && descsz == 4) {
			*flags |= FLAGS_DID_OS_NOTE;
			do_note_freebsd_version(ms, swap, &nbuf[doff]);
			return 1;
		}
	}

	if (namesz == 8 && strcmp((char *)&nbuf[noff], "OpenBSD") == 0 &&
	    type == NT_OPENBSD_VERSION && descsz == 4) {
		*flags |= FLAGS_DID_OS_NOTE;
		if (file_printf(ms, ", for OpenBSD") == -1)
			return 1;
		/* Content of note is always 0 */
		return 1;
	}

	if (namesz == 10 && strcmp((char *)&nbuf[noff], "DragonFly") == 0 &&
	    type == NT_DRAGONFLY_VERSION && descsz == 4) {
		uint32_t desc;
		*flags |= FLAGS_DID_OS_NOTE;
		if (file_printf(ms, ", for DragonFly") == -1)
			return 1;
		memcpy(&desc, &nbuf[doff], sizeof(desc));
		desc = elf_getu32(swap, desc);
		if (file_printf(ms, " %d.%d.%d", desc / 100000,
		    desc / 10000 % 10, desc % 10000) == -1)
			return 1;
		return 1;
	}
	return 0;
}

private int
do_pax_note(struct magic_set *ms, unsigned char *nbuf, uint32_t type,
    int swap, uint32_t namesz, uint32_t descsz,
    size_t noff, size_t doff, int *flags)
{
	if (namesz == 4 && strcmp((char *)&nbuf[noff], "PaX") == 0 &&
	    type == NT_NETBSD_PAX && descsz == 4) {
		static const char *pax[] = {
		    "+mprotect",
		    "-mprotect",
		    "+segvguard",
		    "-segvguard",
		    "+ASLR",
		    "-ASLR",
		};
		uint32_t desc;
		size_t i;
		int did = 0;

		*flags |= FLAGS_DID_NETBSD_PAX;
		memcpy(&desc, &nbuf[doff], sizeof(desc));
		desc = elf_getu32(swap, desc);

		if (desc && file_printf(ms, ", PaX: ") == -1)
			return 1;

		for (i = 0; i < __arraycount(pax); i++) {
			if (((1 << (int)i) & desc) == 0)
				continue;
			if (file_printf(ms, "%s%s", did++ ? "," : "",
			    pax[i]) == -1)
				return 1;
		}
		return 1;
	}
	return 0;
}

private int
do_core_note(struct magic_set *ms, unsigned char *nbuf, uint32_t type,
    int swap, uint32_t namesz, uint32_t descsz,
    size_t noff, size_t doff, int *flags, size_t size, int clazz)
{
#ifdef ELFCORE
	int os_style = -1;
	/*
	 * Sigh.  The 2.0.36 kernel in Debian 2.1, at
	 * least, doesn't correctly implement name
	 * sections, in core dumps, as specified by
	 * the "Program Linking" section of "UNIX(R) System
	 * V Release 4 Programmer's Guide: ANSI C and
	 * Programming Support Tools", because my copy
	 * clearly says "The first 'namesz' bytes in 'name'
	 * contain a *null-terminated* [emphasis mine]
	 * character representation of the entry's owner
	 * or originator", but the 2.0.36 kernel code
	 * doesn't include the terminating null in the
	 * name....
	 */
	if ((namesz == 4 && strncmp((char *)&nbuf[noff], "CORE", 4) == 0) ||
	    (namesz == 5 && strcmp((char *)&nbuf[noff], "CORE") == 0)) {
		os_style = OS_STYLE_SVR4;
	} 

	if ((namesz == 8 && strcmp((char *)&nbuf[noff], "FreeBSD") == 0)) {
		os_style = OS_STYLE_FREEBSD;
	}

	if ((namesz >= 11 && strncmp((char *)&nbuf[noff], "NetBSD-CORE", 11)
	    == 0)) {
		os_style = OS_STYLE_NETBSD;
	}

	if (os_style != -1 && (*flags & FLAGS_DID_CORE_STYLE) == 0) {
		if (file_printf(ms, ", %s-style", os_style_names[os_style])
		    == -1)
			return 1;
		*flags |= FLAGS_DID_CORE_STYLE;
		*flags |= os_style;
	}

	switch (os_style) {
	case OS_STYLE_NETBSD:
		if (type == NT_NETBSD_CORE_PROCINFO) {
			char sbuf[512];
			struct NetBSD_elfcore_procinfo pi;
			memset(&pi, 0, sizeof(pi));
			memcpy(&pi, nbuf + doff, descsz);

			if (file_printf(ms, ", from '%.31s', pid=%u, uid=%u, "
			    "gid=%u, nlwps=%u, lwp=%u (signal %u/code %u)",
			    file_printable(sbuf, sizeof(sbuf),
			    CAST(char *, pi.cpi_name)),
			    elf_getu32(swap, (uint32_t)pi.cpi_pid),
			    elf_getu32(swap, pi.cpi_euid),
			    elf_getu32(swap, pi.cpi_egid),
			    elf_getu32(swap, pi.cpi_nlwps),
			    elf_getu32(swap, (uint32_t)pi.cpi_siglwp),
			    elf_getu32(swap, pi.cpi_signo),
			    elf_getu32(swap, pi.cpi_sigcode)) == -1)
				return 1;

			*flags |= FLAGS_DID_CORE;
			return 1;
		}
		break;

	default:
		if (type == NT_PRPSINFO && *flags & FLAGS_IS_CORE) {
			size_t i, j;
			unsigned char c;
			/*
			 * Extract the program name.  We assume
			 * it to be 16 characters (that's what it
			 * is in SunOS 5.x and Linux).
			 *
			 * Unfortunately, it's at a different offset
			 * in various OSes, so try multiple offsets.
			 * If the characters aren't all printable,
			 * reject it.
			 */
			for (i = 0; i < NOFFSETS; i++) {
				unsigned char *cname, *cp;
				size_t reloffset = prpsoffsets(i);
				size_t noffset = doff + reloffset;
				size_t k;
				for (j = 0; j < 16; j++, noffset++,
				    reloffset++) {
					/*
					 * Make sure we're not past
					 * the end of the buffer; if
					 * we are, just give up.
					 */
					if (noffset >= size)
						goto tryanother;

					/*
					 * Make sure we're not past
					 * the end of the contents;
					 * if we are, this obviously
					 * isn't the right offset.
					 */
					if (reloffset >= descsz)
						goto tryanother;

					c = nbuf[noffset];
					if (c == '\0') {
						/*
						 * A '\0' at the
						 * beginning is
						 * obviously wrong.
						 * Any other '\0'
						 * means we're done.
						 */
						if (j == 0)
							goto tryanother;
						else
							break;
					} else {
						/*
						 * A nonprintable
						 * character is also
						 * wrong.
						 */
						if (!isprint(c) || isquote(c))
							goto tryanother;
					}
				}
				/*
				 * Well, that worked.
				 */

				/*
				 * Try next offsets, in case this match is
				 * in the middle of a string.
				 */
				for (k = i + 1 ; k < NOFFSETS; k++) {
					size_t no;
					int adjust = 1;
					if (prpsoffsets(k) >= prpsoffsets(i))
						continue;
					for (no = doff + prpsoffsets(k);
					     no < doff + prpsoffsets(i); no++)
						adjust = adjust
						         && isprint(nbuf[no]);
					if (adjust)
						i = k;
				}

				cname = (unsigned char *)
				    &nbuf[doff + prpsoffsets(i)];
				for (cp = cname; cp < nbuf + size && *cp
				    && isprint(*cp); cp++)
					continue;
				/*
				 * Linux apparently appends a space at the end
				 * of the command line: remove it.
				 */
				while (cp > cname && isspace(cp[-1]))
					cp--;
				if (file_printf(ms, ", from '%.*s'",
				    (int)(cp - cname), cname) == -1)
					return 1;
				*flags |= FLAGS_DID_CORE;
				return 1;

			tryanother:
				;
			}
		}
		break;
	}
#endif
	return 0;
}

private off_t
get_offset_from_virtaddr(struct magic_set *ms, int swap, int clazz, int fd,
    off_t off, int num, off_t fsize, uint64_t virtaddr)
{
	Elf32_Phdr ph32;
	Elf64_Phdr ph64;

	/*
	 * Loop through all the program headers and find the header with
	 * virtual address in which the "virtaddr" belongs to.
	 */
	for ( ; num; num--) {
		if (pread(fd, xph_addr, xph_sizeof, off) < (ssize_t)xph_sizeof) {
			file_badread(ms);
			return -1;
		}
		off += xph_sizeof;

		if (fsize != SIZE_UNKNOWN && xph_offset > fsize) {
			/* Perhaps warn here */
			continue;
		}

		if (virtaddr >= xph_vaddr && virtaddr < xph_vaddr + xph_filesz)
			return xph_offset + (virtaddr - xph_vaddr);
	}
	return 0;
}

private size_t
get_string_on_virtaddr(struct magic_set *ms,
    int swap, int clazz, int fd, off_t ph_off, int ph_num,
    off_t fsize, uint64_t virtaddr, char *buf, ssize_t buflen)
{
	char *bptr;
	off_t offset;

	if (buflen == 0)
		return 0;

	offset = get_offset_from_virtaddr(ms, swap, clazz, fd, ph_off, ph_num,
	    fsize, virtaddr);
	if ((buflen = pread(fd, buf, CAST(size_t, buflen), offset)) <= 0) {
		file_badread(ms);
		return 0;
	}

	buf[buflen - 1] = '\0';

	/* We expect only printable characters, so return if buffer contains
	 * non-printable character before the '\0' or just '\0'. */
	for (bptr = buf; *bptr && isprint((unsigned char)*bptr); bptr++)
		continue;
	if (*bptr != '\0')
		return 0;

	return bptr - buf;
}


/*ARGSUSED*/
private int
do_auxv_note(struct magic_set *ms, unsigned char *nbuf, uint32_t type,
    int swap, uint32_t namesz __attribute__((__unused__)),
    uint32_t descsz __attribute__((__unused__)),
    size_t noff __attribute__((__unused__)), size_t doff,
    int *flags, size_t size __attribute__((__unused__)), int clazz,
    int fd, off_t ph_off, int ph_num, off_t fsize)
{
#ifdef ELFCORE
	Aux32Info auxv32;
	Aux64Info auxv64;
	size_t elsize = xauxv_sizeof;
	const char *tag;
	int is_string;
	size_t nval;

	if ((*flags & (FLAGS_IS_CORE|FLAGS_DID_CORE_STYLE)) !=
	    (FLAGS_IS_CORE|FLAGS_DID_CORE_STYLE))
		return 0;

	switch (*flags & FLAGS_CORE_STYLE) {
	case OS_STYLE_SVR4:
		if (type != NT_AUXV)
			return 0;
		break;
#ifdef notyet
	case OS_STYLE_NETBSD:
		if (type != NT_NETBSD_CORE_AUXV)
			return 0;
		break;
	case OS_STYLE_FREEBSD:
		if (type != NT_FREEBSD_PROCSTAT_AUXV)
			return 0;
		break;
#endif
	default:
		return 0;
	}

	*flags |= FLAGS_DID_AUXV;

	nval = 0;
	for (size_t off = 0; off + elsize <= descsz; off += elsize) {
		memcpy(xauxv_addr, &nbuf[doff + off], xauxv_sizeof);
		/* Limit processing to 50 vector entries to prevent DoS */
		if (nval++ >= 50) {
			file_error(ms, 0, "Too many ELF Auxv elements");
			return 1;
		}

		switch(xauxv_type) {
		case AT_LINUX_EXECFN:
			is_string = 1;
			tag = "execfn";
			break;
		case AT_LINUX_PLATFORM:
			is_string = 1;
			tag = "platform";
			break;
		case AT_LINUX_UID:
			is_string = 0;
			tag = "real uid";
			break;
		case AT_LINUX_GID:
			is_string = 0;
			tag = "real gid";
			break;
		case AT_LINUX_EUID:
			is_string = 0;
			tag = "effective uid";
			break;
		case AT_LINUX_EGID:
			is_string = 0;
			tag = "effective gid";
			break;
		default:
			is_string = 0;
			tag = NULL;
			break;
		}

		if (tag == NULL)
			continue;

		if (is_string) {
			char buf[256];
			ssize_t buflen;
			buflen = get_string_on_virtaddr(ms, swap, clazz, fd,
			    ph_off, ph_num, fsize, xauxv_val, buf, sizeof(buf));

			if (buflen == 0)
				continue;
			
			if (file_printf(ms, ", %s: '%s'", tag, buf) == -1)
				return 0;
		} else {
			if (file_printf(ms, ", %s: %d", tag, (int) xauxv_val)
			    == -1)
				return 0;
		}
	}
	return 1;
#else
	return 0;
#endif
}

private size_t
dodynamic(struct magic_set *ms, void *vbuf, size_t offset, size_t size,
    int clazz, int swap)
{
	Elf32_Dyn dh32;
	Elf64_Dyn dh64;
	unsigned char *dbuf = CAST(unsigned char *, vbuf);

	if (xdh_sizeof + offset > size) {
		/*
		 * We're out of note headers.
		 */
		return xdh_sizeof + offset;
	}

	memcpy(xdh_addr, &dbuf[offset], xdh_sizeof);
	offset += xdh_sizeof;

	switch (xdh_tag) {
	case DT_FLAGS_1:
		if (xdh_val == DF_1_PIE)
			ms->mode |= 0111;
		else
			ms->mode &= ~0111;
		break;
	default:
		break;
	}
	return offset;
}


private size_t
donote(struct magic_set *ms, void *vbuf, size_t offset, size_t size,
    int clazz, int swap, size_t align, int *flags, uint16_t *notecount,
    int fd, off_t ph_off, int ph_num, off_t fsize)
{
	Elf32_Nhdr nh32;
	Elf64_Nhdr nh64;
	size_t noff, doff;
	uint32_t namesz, descsz;
	unsigned char *nbuf = CAST(unsigned char *, vbuf);

	if (*notecount == 0)
		return 0;
	--*notecount;

	if (xnh_sizeof + offset > size) {
		/*
		 * We're out of note headers.
		 */
		return xnh_sizeof + offset;
	}

	memcpy(xnh_addr, &nbuf[offset], xnh_sizeof);
	offset += xnh_sizeof;

	namesz = xnh_namesz;
	descsz = xnh_descsz;

	if ((namesz == 0) && (descsz == 0)) {
		/*
		 * We're out of note headers.
		 */
		return (offset >= size) ? offset : size;
	}

	if (namesz & 0x80000000) {
	    file_printf(ms, ", bad note name size %#lx",
		CAST(unsigned long, namesz));
	    return 0;
	}

	if (descsz & 0x80000000) {
	    file_printf(ms, ", bad note description size %#lx",
		CAST(unsigned long, descsz));
	    return 0;
	}

	noff = offset;
	doff = ELF_ALIGN(offset + namesz);

	if (offset + namesz > size) {
		/*
		 * We're past the end of the buffer.
		 */
		return doff;
	}

	offset = ELF_ALIGN(doff + descsz);
	if (doff + descsz > size) {
		/*
		 * We're past the end of the buffer.
		 */
		return (offset >= size) ? offset : size;
	}


	if ((*flags & FLAGS_DID_OS_NOTE) == 0) {
		if (do_os_note(ms, nbuf, xnh_type, swap,
		    namesz, descsz, noff, doff, flags))
			return offset;
	}

	if ((*flags & FLAGS_DID_BUILD_ID) == 0) {
		if (do_bid_note(ms, nbuf, xnh_type, swap,
		    namesz, descsz, noff, doff, flags))
			return offset;
	}
		
	if ((*flags & FLAGS_DID_NETBSD_PAX) == 0) {
		if (do_pax_note(ms, nbuf, xnh_type, swap,
		    namesz, descsz, noff, doff, flags))
			return offset;
	}

	if ((*flags & FLAGS_DID_CORE) == 0) {
		if (do_core_note(ms, nbuf, xnh_type, swap,
		    namesz, descsz, noff, doff, flags, size, clazz))
			return offset;
	}

	if ((*flags & FLAGS_DID_AUXV) == 0) {
		if (do_auxv_note(ms, nbuf, xnh_type, swap,
			namesz, descsz, noff, doff, flags, size, clazz,
			fd, ph_off, ph_num, fsize))
			return offset;
	}

	if (namesz == 7 && strcmp(CAST(char *, &nbuf[noff]), "NetBSD") == 0) {
		int descw, flag;
		const char *str, *tag;
		if (descsz > 100)
			descsz = 100;
		switch (xnh_type) {
	    	case NT_NETBSD_VERSION:
			return offset;
		case NT_NETBSD_MARCH:
			flag = FLAGS_DID_NETBSD_MARCH;
			tag = "compiled for";
			break;
		case NT_NETBSD_CMODEL:
			flag = FLAGS_DID_NETBSD_CMODEL;
			tag = "compiler model";
			break;
		case NT_NETBSD_EMULATION:
			flag = FLAGS_DID_NETBSD_EMULATION;
			tag = "emulation:";
			break;
		default:
			if (*flags & FLAGS_DID_NETBSD_UNKNOWN)
				return offset;
			*flags |= FLAGS_DID_NETBSD_UNKNOWN;
			if (file_printf(ms, ", note=%u", xnh_type) == -1)
				return offset;
			return offset;
		}

		if (*flags & flag)
			return offset;
		str = CAST(const char *, &nbuf[doff]);
		descw = CAST(int, descsz);
		*flags |= flag;
		file_printf(ms, ", %s: %.*s", tag, descw, str);
		return offset;
	}

	return offset;
}

/* SunOS 5.x hardware capability descriptions */
typedef struct cap_desc {
	uint64_t cd_mask;
	const char *cd_name;
} cap_desc_t;

static const cap_desc_t cap_desc_sparc[] = {
	{ AV_SPARC_MUL32,		"MUL32" },
	{ AV_SPARC_DIV32,		"DIV32" },
	{ AV_SPARC_FSMULD,		"FSMULD" },
	{ AV_SPARC_V8PLUS,		"V8PLUS" },
	{ AV_SPARC_POPC,		"POPC" },
	{ AV_SPARC_VIS,			"VIS" },
	{ AV_SPARC_VIS2,		"VIS2" },
	{ AV_SPARC_ASI_BLK_INIT,	"ASI_BLK_INIT" },
	{ AV_SPARC_FMAF,		"FMAF" },
	{ AV_SPARC_FJFMAU,		"FJFMAU" },
	{ AV_SPARC_IMA,			"IMA" },
	{ 0, NULL }
};

static const cap_desc_t cap_desc_386[] = {
	{ AV_386_FPU,			"FPU" },
	{ AV_386_TSC,			"TSC" },
	{ AV_386_CX8,			"CX8" },
	{ AV_386_SEP,			"SEP" },
	{ AV_386_AMD_SYSC,		"AMD_SYSC" },
	{ AV_386_CMOV,			"CMOV" },
	{ AV_386_MMX,			"MMX" },
	{ AV_386_AMD_MMX,		"AMD_MMX" },
	{ AV_386_AMD_3DNow,		"AMD_3DNow" },
	{ AV_386_AMD_3DNowx,		"AMD_3DNowx" },
	{ AV_386_FXSR,			"FXSR" },
	{ AV_386_SSE,			"SSE" },
	{ AV_386_SSE2,			"SSE2" },
	{ AV_386_PAUSE,			"PAUSE" },
	{ AV_386_SSE3,			"SSE3" },
	{ AV_386_MON,			"MON" },
	{ AV_386_CX16,			"CX16" },
	{ AV_386_AHF,			"AHF" },
	{ AV_386_TSCP,			"TSCP" },
	{ AV_386_AMD_SSE4A,		"AMD_SSE4A" },
	{ AV_386_POPCNT,		"POPCNT" },
	{ AV_386_AMD_LZCNT,		"AMD_LZCNT" },
	{ AV_386_SSSE3,			"SSSE3" },
	{ AV_386_SSE4_1,		"SSE4.1" },
	{ AV_386_SSE4_2,		"SSE4.2" },
	{ 0, NULL }
};

private int
doshn(struct magic_set *ms, int clazz, int swap, int fd, off_t off, int num,
    size_t size, off_t fsize, int mach, int strtab, int *flags,
    uint16_t *notecount)
{
	Elf32_Shdr sh32;
	Elf64_Shdr sh64;
	int stripped = 1, has_debug_info = 0;
	size_t nbadcap = 0;
	void *nbuf;
	off_t noff, coff, name_off;
	uint64_t cap_hw1 = 0;	/* SunOS 5.x hardware capabilities */
	uint64_t cap_sf1 = 0;	/* SunOS 5.x software capabilities */
	char name[50];
	ssize_t namesize;

	if (size != xsh_sizeof) {
		if (file_printf(ms, ", corrupted section header size") == -1)
			return -1;
		return 0;
	}

	/* Read offset of name section to be able to read section names later */
	if (pread(fd, xsh_addr, xsh_sizeof, CAST(off_t, (off + size * strtab)))
	    < CAST(ssize_t, xsh_sizeof)) {
		if (file_printf(ms, ", missing section headers") == -1)
			return -1;
		return 0;
	}
	name_off = xsh_offset;

	for ( ; num; num--) {
		/* Read the name of this section. */
		if ((namesize = pread(fd, name, sizeof(name) - 1,
		    name_off + xsh_name)) == -1) {
			file_badread(ms);
			return -1;
		}
		name[namesize] = '\0';
		if (strcmp(name, ".debug_info") == 0) {
			has_debug_info = 1;
			stripped = 0;
		}

		if (pread(fd, xsh_addr, xsh_sizeof, off) <
		    CAST(ssize_t, xsh_sizeof)) {
			file_badread(ms);
			return -1;
		}
		off += size;

		/* Things we can determine before we seek */
		switch (xsh_type) {
		case SHT_SYMTAB:
#if 0
		case SHT_DYNSYM:
#endif
			stripped = 0;
			break;
		default:
			if (fsize != SIZE_UNKNOWN && xsh_offset > fsize) {
				/* Perhaps warn here */
				continue;
			}
			break;
		}


		/* Things we can determine when we seek */
		switch (xsh_type) {
		case SHT_NOTE:
			if (CAST(uintmax_t, (xsh_size + xsh_offset)) >
			    CAST(uintmax_t, fsize)) {
				if (file_printf(ms,
				    ", note offset/size %#" INTMAX_T_FORMAT
				    "x+%#" INTMAX_T_FORMAT "x exceeds"
				    " file size %#" INTMAX_T_FORMAT "x",
				    CAST(uintmax_t, xsh_offset),
				    CAST(uintmax_t, xsh_size),
				    CAST(uintmax_t, fsize)) == -1)
					return -1;
				return 0; 
			}
			if ((nbuf = malloc(xsh_size)) == NULL) {
				file_error(ms, errno, "Cannot allocate memory"
				    " for note");
				return -1;
			}
			if (pread(fd, nbuf, xsh_size, xsh_offset) <
			    CAST(ssize_t, xsh_size)) {
				file_badread(ms);
				free(nbuf);
				return -1;
			}

			noff = 0;
			for (;;) {
				if (noff >= CAST(off_t, xsh_size))
					break;
				noff = donote(ms, nbuf, CAST(size_t, noff),
				    xsh_size, clazz, swap, 4, flags, notecount,
				    fd, 0, 0, 0);
				if (noff == 0)
					break;
			}
			free(nbuf);
			break;
		case SHT_SUNW_cap:
			switch (mach) {
			case EM_SPARC:
			case EM_SPARCV9:
			case EM_IA_64:
			case EM_386:
			case EM_AMD64:
				break;
			default:
				goto skip;
			}

			if (nbadcap > 5)
				break;
			if (lseek(fd, xsh_offset, SEEK_SET)
			    == CAST(off_t, -1)) {
				file_badseek(ms);
				return -1;
			}
			coff = 0;
			for (;;) {
				Elf32_Cap cap32;
				Elf64_Cap cap64;
				char cbuf[/*CONSTCOND*/
				    MAX(sizeof(cap32), sizeof(cap64))];
				if ((coff += xcap_sizeof) >
				    CAST(off_t, xsh_size))
					break;
				if (read(fd, cbuf, CAST(size_t, xcap_sizeof)) !=
				    CAST(ssize_t, xcap_sizeof)) {
					file_badread(ms);
					return -1;
				}
				if (cbuf[0] == 'A') {
#ifdef notyet
					char *p = cbuf + 1;
					uint32_t len, tag;
					memcpy(&len, p, sizeof(len));
					p += 4;
					len = getu32(swap, len);
					if (memcmp("gnu", p, 3) != 0) {
					    if (file_printf(ms,
						", unknown capability %.3s", p)
						== -1)
						return -1;
					    break;
					}
					p += strlen(p) + 1;
					tag = *p++;
					memcpy(&len, p, sizeof(len));
					p += 4;
					len = getu32(swap, len);
					if (tag != 1) {
					    if (file_printf(ms, ", unknown gnu"
						" capability tag %d", tag)
						== -1)
						return -1;
					    break;
					}
					// gnu attributes 
#endif
					break;
				}
				memcpy(xcap_addr, cbuf, xcap_sizeof);
				switch (xcap_tag) {
				case CA_SUNW_NULL:
					break;
				case CA_SUNW_HW_1:
					cap_hw1 |= xcap_val;
					break;
				case CA_SUNW_SF_1:
					cap_sf1 |= xcap_val;
					break;
				default:
					if (file_printf(ms,
					    ", with unknown capability "
					    "%#" INT64_T_FORMAT "x = %#"
					    INT64_T_FORMAT "x",
					    CAST(unsigned long long, xcap_tag),
					    CAST(unsigned long long, xcap_val))
					    == -1)
						return -1;
					if (nbadcap++ > 2)
						coff = xsh_size;
					break;
				}
			}
			/*FALLTHROUGH*/
		skip:
		default:
			break;
		}
	}

	if (has_debug_info) {
		if (file_printf(ms, ", with debug_info") == -1)
			return -1;
	}
	if (file_printf(ms, ", %sstripped", stripped ? "" : "not ") == -1)
		return -1;
	if (cap_hw1) {
		const cap_desc_t *cdp;
		switch (mach) {
		case EM_SPARC:
		case EM_SPARC32PLUS:
		case EM_SPARCV9:
			cdp = cap_desc_sparc;
			break;
		case EM_386:
		case EM_IA_64:
		case EM_AMD64:
			cdp = cap_desc_386;
			break;
		default:
			cdp = NULL;
			break;
		}
		if (file_printf(ms, ", uses") == -1)
			return -1;
		if (cdp) {
			while (cdp->cd_name) {
				if (cap_hw1 & cdp->cd_mask) {
					if (file_printf(ms,
					    " %s", cdp->cd_name) == -1)
						return -1;
					cap_hw1 &= ~cdp->cd_mask;
				}
				++cdp;
			}
			if (cap_hw1)
				if (file_printf(ms,
				    " unknown hardware capability %#"
				    INT64_T_FORMAT "x",
				    CAST(unsigned long long, cap_hw1)) == -1)
					return -1;
		} else {
			if (file_printf(ms,
			    " hardware capability %#" INT64_T_FORMAT "x",
			    CAST(unsigned long long, cap_hw1)) == -1)
				return -1;
		}
	}
	if (cap_sf1) {
		if (cap_sf1 & SF1_SUNW_FPUSED) {
			if (file_printf(ms,
			    (cap_sf1 & SF1_SUNW_FPKNWN)
			    ? ", uses frame pointer"
			    : ", not known to use frame pointer") == -1)
				return -1;
		}
		cap_sf1 &= ~SF1_SUNW_MASK;
		if (cap_sf1)
			if (file_printf(ms,
			    ", with unknown software capability %#"
			    INT64_T_FORMAT "x",
			    CAST(unsigned long long, cap_sf1)) == -1)
				return -1;
	}
	return 0;
}

/*
 * Look through the program headers of an executable image, searching
 * for a PT_INTERP section; if one is found, it's dynamically linked,
 * otherwise it's statically linked.
 */
private int
dophn_exec(struct magic_set *ms, int clazz, int swap, int fd, off_t off,
    int num, size_t size, off_t fsize, int sh_num, int *flags,
    uint16_t *notecount)
{
	Elf32_Phdr ph32;
	Elf64_Phdr ph64;
	const char *linking_style = "statically";
	unsigned char nbuf[BUFSIZ];
	char ibuf[BUFSIZ];
	char interp[BUFSIZ];
	ssize_t bufsize;
	size_t offset, align, len;
	
	if (size != xph_sizeof) {
		if (file_printf(ms, ", corrupted program header size") == -1)
			return -1;
		return 0;
	}

	interp[0] = '\0';
  	for ( ; num; num--) {
		int doread;
		if (pread(fd, xph_addr, xph_sizeof, off) < 
		    CAST(ssize_t, xph_sizeof)) {
			file_badread(ms);
			return -1;
		}

		off += size;
		bufsize = 0;
		align = 4;

		/* Things we can determine before we seek */
		switch (xph_type) {
		case PT_DYNAMIC:
			linking_style = "dynamically";
			doread = 1;
			break;
		case PT_NOTE:
			if (sh_num)	/* Did this through section headers */
				continue;
			if (((align = xph_align) & 0x80000000UL) != 0 ||
			    align < 4) {
				if (file_printf(ms, 
				    ", invalid note alignment %#lx",
				    CAST(unsigned long, align)) == -1)
					return -1;
				align = 4;
			}
			/*FALLTHROUGH*/
		case PT_INTERP:
			doread = 1;
			break;
		default:
			doread = 0;
			if (fsize != SIZE_UNKNOWN && xph_offset > fsize) {
				/* Maybe warn here? */
				continue;
			}
			break;
		}

		if (doread) {
			len = xph_filesz < sizeof(nbuf) ? xph_filesz
			    : sizeof(nbuf);
			bufsize = pread(fd, nbuf, len, xph_offset);
			if (bufsize == -1) {
				file_badread(ms);
				return -1;
			}
		} else
			len = 0;

		/* Things we can determine when we seek */
		switch (xph_type) {
		case PT_DYNAMIC:
			offset = 0;
			for (;;) {
				if (offset >= (size_t)bufsize)
					break;
				offset = dodynamic(ms, nbuf, offset,
				    CAST(size_t, bufsize), clazz, swap);
				if (offset == 0)
					break;
			}
			break;

		case PT_INTERP:
			if (bufsize && nbuf[0]) {
				nbuf[bufsize - 1] = '\0';
				memcpy(interp, nbuf, bufsize);
			} else
				strlcpy(interp, "*empty*", sizeof(interp));
			break;
		case PT_NOTE:
			/*
			 * This is a PT_NOTE section; loop through all the notes
			 * in the section.
			 */
			offset = 0;
			for (;;) {
				if (offset >= (size_t)bufsize)
					break;
				offset = donote(ms, nbuf, offset,
				    CAST(size_t, bufsize), clazz, swap, align,
				    flags, notecount, fd, 0, 0, 0);
				if (offset == 0)
					break;
			}
			break;
		default:
			break;
		}
	}
	if (file_printf(ms, ", %s linked", linking_style)
	    == -1)
		return -1;
	if (interp[0])
		if (file_printf(ms, ", interpreter %s",
		    file_printable(ibuf, sizeof(ibuf), interp)) == -1)
			return -1;
	return 0;
}


protected int
file_tryelf(struct magic_set *ms, const struct buffer *b)
{
	int fd = b->fd;
	const unsigned char *buf = b->fbuf;
	size_t nbytes = b->flen;
	union {
		int32_t l;
		char c[sizeof(int32_t)];
	} u;
	int clazz;
	int swap;
	struct stat st;
	off_t fsize;
	int flags = 0;
	Elf32_Ehdr elf32hdr;
	Elf64_Ehdr elf64hdr;
	uint16_t type, phnum, shnum, notecount;

	if (ms->flags & (MAGIC_MIME|MAGIC_APPLE|MAGIC_EXTENSION))
		return 0;
	/*
	 * ELF executables have multiple section headers in arbitrary
	 * file locations and thus file(1) cannot determine it from easily.
	 * Instead we traverse thru all section headers until a symbol table
	 * one is found or else the binary is stripped.
	 * Return immediately if it's not ELF (so we avoid pipe2file unless needed).
	 */
	if (buf[EI_MAG0] != ELFMAG0
	    || (buf[EI_MAG1] != ELFMAG1 && buf[EI_MAG1] != OLFMAG1)
	    || buf[EI_MAG2] != ELFMAG2 || buf[EI_MAG3] != ELFMAG3)
		return 0;

	/*
	 * If we cannot seek, it must be a pipe, socket or fifo.
	 */
	if((lseek(fd, CAST(off_t, 0), SEEK_SET) == CAST(off_t, -1))
	    && (errno == ESPIPE))
		fd = file_pipe2file(ms, fd, buf, nbytes);

	if (fstat(fd, &st) == -1) {
  		file_badread(ms);
		return -1;
	}
	if (S_ISREG(st.st_mode) || st.st_size != 0)
		fsize = st.st_size;
	else
		fsize = SIZE_UNKNOWN;

	clazz = buf[EI_CLASS];

	switch (clazz) {
	case ELFCLASS32:
#undef elf_getu
#define elf_getu(a, b)	elf_getu32(a, b)
#undef elfhdr
#define elfhdr elf32hdr
#include "elfclass.h"
	case ELFCLASS64:
#undef elf_getu
#define elf_getu(a, b)	elf_getu64(a, b)
#undef elfhdr
#define elfhdr elf64hdr
#include "elfclass.h"
	default:
	    if (file_printf(ms, ", unknown class %d", clazz) == -1)
		    return -1;
	    break;
	}
	return 0;
}
#endif
