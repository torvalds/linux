/*-
 * Copyright (c) 2006,2008-2011 Joseph Koshy
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
 *
 * $Id: _libelf.h 3632 2018-10-10 21:12:43Z jkoshy $
 */

#ifndef	__LIBELF_H_
#define	__LIBELF_H_

#include <sys/queue.h>

#include "_libelf_config.h"

#include "_elftc.h"

/*
 * Library-private data structures.
 */

#define LIBELF_MSG_SIZE	256

struct _libelf_globals {
	int		libelf_arch;
	unsigned int	libelf_byteorder;
	int		libelf_class;
	int		libelf_error;
	int		libelf_fillchar;
	unsigned int	libelf_version;
	unsigned char	libelf_msg[LIBELF_MSG_SIZE];
};

extern struct _libelf_globals _libelf;

#define	LIBELF_PRIVATE(N)	(_libelf.libelf_##N)

#define	LIBELF_ELF_ERROR_MASK			0xFF
#define	LIBELF_OS_ERROR_SHIFT			8

#define	LIBELF_ERROR(E, O) (((E) & LIBELF_ELF_ERROR_MASK) |	\
	((O) << LIBELF_OS_ERROR_SHIFT))

#define	LIBELF_SET_ERROR(E, O) do {					\
		LIBELF_PRIVATE(error) = LIBELF_ERROR(ELF_E_##E, (O));	\
	} while (0)

#define	LIBELF_ADJUST_AR_SIZE(S)	(((S) + 1U) & ~1U)

/*
 * Flags for library internal use.  These use the upper 16 bits of the
 * `e_flags' field.
 */
#define	LIBELF_F_API_MASK	0x00FFFFU  /* Flags defined by the API. */
#define	LIBELF_F_AR_HEADER	0x010000U  /* translated header available */
#define	LIBELF_F_AR_VARIANT_SVR4 0x020000U /* BSD style ar(1) archive */
#define	LIBELF_F_DATA_MALLOCED	0x040000U /* whether data was malloc'ed */
#define	LIBELF_F_RAWFILE_MALLOC	0x080000U /* whether e_rawfile was malloc'ed */
#define	LIBELF_F_RAWFILE_MMAP	0x100000U /* whether e_rawfile was mmap'ed */
#define	LIBELF_F_SHDRS_LOADED	0x200000U /* whether all shdrs were read in */
#define	LIBELF_F_SPECIAL_FILE	0x400000U /* non-regular file */

struct _Elf {
	int		e_activations;	/* activation count */
	unsigned int	e_byteorder;	/* ELFDATA* */
	int		e_class;	/* ELFCLASS*  */
	Elf_Cmd		e_cmd;		/* ELF_C_* used at creation time */
	int		e_fd;		/* associated file descriptor */
	unsigned int	e_flags;	/* ELF_F_* & LIBELF_F_* flags */
	Elf_Kind	e_kind;		/* ELF_K_* */
	Elf		*e_parent; 	/* non-NULL for archive members */
	unsigned char	*e_rawfile;	/* uninterpreted bytes */
	size_t		e_rawsize;	/* size of uninterpreted bytes */
	unsigned int	e_version;	/* file version */

	/*
	 * Header information for archive members.  See the
	 * LIBELF_F_AR_HEADER flag.
	 */
	union {
		Elf_Arhdr	*e_arhdr;	/* translated header */
		unsigned char	*e_rawhdr;	/* untranslated header */
	} e_hdr;

	union {
		struct {		/* ar(1) archives */
			off_t	e_next;	/* set by elf_rand()/elf_next() */
			int	e_nchildren;
			unsigned char *e_rawstrtab; /* file name strings */
			size_t	e_rawstrtabsz;
			unsigned char *e_rawsymtab;	/* symbol table */
			size_t	e_rawsymtabsz;
			Elf_Arsym *e_symtab;
			size_t	e_symtabsz;
		} e_ar;
		struct {		/* regular ELF files */
			union {
				Elf32_Ehdr *e_ehdr32;
				Elf64_Ehdr *e_ehdr64;
			} e_ehdr;
			union {
				Elf32_Phdr *e_phdr32;
				Elf64_Phdr *e_phdr64;
			} e_phdr;
			STAILQ_HEAD(, _Elf_Scn)	e_scn;	/* section list */
			size_t	e_nphdr;	/* number of Phdr entries */
			size_t	e_nscn;		/* number of sections */
			size_t	e_strndx;	/* string table section index */
		} e_elf;
	} e_u;
};

/*
 * The internal descriptor wrapping the "Elf_Data" type.
 */
struct _Libelf_Data {
	Elf_Data	d_data;		/* The exported descriptor. */
	Elf_Scn		*d_scn;		/* The containing section */
	unsigned int	d_flags;
	STAILQ_ENTRY(_Libelf_Data) d_next;
};

struct _Elf_Scn {
	union {
		Elf32_Shdr	s_shdr32;
		Elf64_Shdr	s_shdr64;
	} s_shdr;
	STAILQ_HEAD(, _Libelf_Data) s_data;	/* translated data */
	STAILQ_HEAD(, _Libelf_Data) s_rawdata;	/* raw data */
	STAILQ_ENTRY(_Elf_Scn) s_next;
	struct _Elf	*s_elf;		/* parent ELF descriptor */
	unsigned int	s_flags;	/* flags for the section as a whole */
	size_t		s_ndx;		/* index# for this section */
	uint64_t	s_offset;	/* managed by elf_update() */
	uint64_t	s_rawoff;	/* original offset in the file */
	uint64_t	s_size;		/* managed by elf_update() */
};


enum {
	ELF_TOFILE,
	ELF_TOMEMORY
};


/*
 * The LIBELF_COPY macros are used to copy fields from a GElf_*
 * structure to their 32-bit counterparts, while checking for out of
 * range values.
 *
 * - LIBELF_COPY_U32 :: copy an unsigned 32 bit field.
 * - LIBELF_COPY_S32 :: copy a signed 32 bit field.
 */

#define	LIBELF_COPY_U32(DST, SRC, NAME)	do {			\
		if ((SRC)->NAME > UINT32_MAX) {			\
			LIBELF_SET_ERROR(RANGE, 0);		\
			return (0);				\
		}						\
		(DST)->NAME = (SRC)->NAME & 0xFFFFFFFFU;	\
	} while (0)

#define	LIBELF_COPY_S32(DST, SRC, NAME)	do {			\
		if ((SRC)->NAME > INT32_MAX ||			\
		    (SRC)->NAME < INT32_MIN) {			\
			LIBELF_SET_ERROR(RANGE, 0);		\
			return (0);				\
		}						\
		(DST)->NAME = (int32_t) (SRC)->NAME;		\
	} while (0)


/*
 * Function Prototypes.
 */

typedef int _libelf_translator_function(unsigned char *_dst, size_t dsz,
    unsigned char *_src, size_t _cnt, int _byteswap);

#ifdef __cplusplus
extern "C" {
#endif
struct _Libelf_Data *_libelf_allocate_data(Elf_Scn *_s);
Elf	*_libelf_allocate_elf(void);
Elf_Scn	*_libelf_allocate_scn(Elf *_e, size_t _ndx);
Elf_Arhdr *_libelf_ar_gethdr(Elf *_e);
Elf	*_libelf_ar_open(Elf *_e, int _reporterror);
Elf	*_libelf_ar_open_member(int _fd, Elf_Cmd _c, Elf *_ar);
Elf_Arsym *_libelf_ar_process_bsd_symtab(Elf *_ar, size_t *_dst);
Elf_Arsym *_libelf_ar_process_svr4_symtab(Elf *_ar, size_t *_dst);
long	 _libelf_checksum(Elf *_e, int _elfclass);
void	*_libelf_ehdr(Elf *_e, int _elfclass, int _allocate);
int	_libelf_elfmachine(Elf *_e);
unsigned int _libelf_falign(Elf_Type _t, int _elfclass);
size_t	_libelf_fsize(Elf_Type _t, int _elfclass, unsigned int _version,
    size_t count);
_libelf_translator_function *_libelf_get_translator(Elf_Type _t,
    int _direction, int _elfclass, int _elfmachine);
void	*_libelf_getphdr(Elf *_e, int _elfclass);
void	*_libelf_getshdr(Elf_Scn *_scn, int _elfclass);
void	_libelf_init_elf(Elf *_e, Elf_Kind _kind);
int	_libelf_is_mips64el(Elf *e);
int	_libelf_load_section_headers(Elf *e, void *ehdr);
unsigned int _libelf_malign(Elf_Type _t, int _elfclass);
Elf	*_libelf_memory(unsigned char *_image, size_t _sz, int _reporterror);
size_t	_libelf_msize(Elf_Type _t, int _elfclass, unsigned int _version);
void	*_libelf_newphdr(Elf *_e, int _elfclass, size_t _count);
Elf	*_libelf_open_object(int _fd, Elf_Cmd _c, int _reporterror);
Elf64_Xword _libelf_mips64el_r_info_tof(Elf64_Xword r_info);
Elf64_Xword _libelf_mips64el_r_info_tom(Elf64_Xword r_info);
struct _Libelf_Data *_libelf_release_data(struct _Libelf_Data *_d);
Elf	*_libelf_release_elf(Elf *_e);
Elf_Scn	*_libelf_release_scn(Elf_Scn *_s);
int	_libelf_setphnum(Elf *_e, void *_eh, int _elfclass, size_t _phnum);
int	_libelf_setshnum(Elf *_e, void *_eh, int _elfclass, size_t _shnum);
int	_libelf_setshstrndx(Elf *_e, void *_eh, int _elfclass,
    size_t _shstrndx);
Elf_Data *_libelf_xlate(Elf_Data *_d, const Elf_Data *_s,
    unsigned int _encoding, int _elfclass, int _elfmachine, int _direction);
int	_libelf_xlate_shtype(uint32_t _sht);
#ifdef __cplusplus
}
#endif

#endif	/* __LIBELF_H_ */
