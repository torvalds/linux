/*-
 * Copyright (c) 2007-2013 Kai Wang
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
 * $Id: elfcopy.h 3615 2018-05-17 04:12:24Z kaiwang27 $
 */

#include <sys/queue.h>
#include <gelf.h>
#include <libelftc.h>

#include "_elftc.h"

/*
 * User specified symbol operation (strip, keep, localize, globalize,
 * weaken, rename, etc).
 */
struct symop {
	const char	*name;
	const char	*newname;

#define SYMOP_KEEP	0x0001U
#define SYMOP_STRIP	0x0002U
#define SYMOP_GLOBALIZE	0x0004U
#define SYMOP_LOCALIZE	0x0008U
#define SYMOP_KEEPG	0x0010U
#define SYMOP_WEAKEN	0x0020U
#define SYMOP_REDEF	0x0040U

	unsigned int	op;

	STAILQ_ENTRY(symop) symop_list;
};

/* File containing symbol list. */
struct symfile {
	dev_t		 dev;
	ino_t		 ino;
	size_t		 size;
	char		*data;
	unsigned int	 op;

	STAILQ_ENTRY(symfile) symfile_list;
};

/* Sections to copy/remove/rename/... */
struct sec_action {
	const char	*name;
	const char	*addopt;
	const char	*newname;
	const char	*string;
	uint64_t	 lma;
	uint64_t	 vma;
	int64_t		 lma_adjust;
	int64_t		 vma_adjust;

#define	SF_ALLOC	0x0001U
#define	SF_LOAD		0x0002U
#define	SF_NOLOAD	0x0004U
#define	SF_READONLY	0x0008U
#define	SF_DEBUG	0x0010U
#define	SF_CODE		0x0020U
#define	SF_DATA		0x0040U
#define	SF_ROM		0x0080U
#define	SF_SHARED	0X0100U
#define	SF_CONTENTS	0x0200U

	int	flags;
	int	add;
	int	append;
	int	compress;
	int	copy;
	int	print;
	int	remove;
	int	rename;
	int	setflags;
	int	setlma;
	int	setvma;

	STAILQ_ENTRY(sec_action) sac_list;
};

/* Sections to add from file. */
struct sec_add {
	char	*name;
	char	*content;
	size_t	 size;

	STAILQ_ENTRY(sec_add) sadd_list;
};

struct segment;

/* Internal data structure for sections. */
struct section {
	struct segment	*seg;	/* containing segment */
	struct segment	*seg_tls; /* tls segment */
	const char	*name;	/* section name */
	char		*newname; /* new section name */
	Elf_Scn		*is;	/* input scn */
	Elf_Scn		*os;	/* output scn */
	void		*buf;	/* section content */
	uint8_t		*pad;	/* section padding */
	uint64_t	 off;	/* section offset */
	uint64_t	 sz;	/* section size */
	uint64_t	 cap;	/* section capacity */
	uint64_t	 align;	/* section alignment */
	uint64_t	 type;	/* section type */
	uint64_t	 flags;	/* section flags */
	uint64_t	 vma;	/* section virtual addr */
	uint64_t	 lma;	/* section load addr */
	uint64_t	 pad_sz;/* section padding size */
	int		 loadable; /* whether loadable */
	int		 pseudo;
	int		 nocopy;

	TAILQ_ENTRY(section) sec_list;	/* next section */
};

/* Internal data structure for segments. */
struct segment {
	uint64_t	vaddr;	/* virtual addr (VMA) */
	uint64_t	paddr;	/* physical addr (LMA) */
	uint64_t	off;	/* file offset */
	uint64_t	fsz;	/* file size */
	uint64_t	msz;	/* memory size */
	uint64_t	type;	/* segment type */
	int		remove;	/* whether remove */
	int		nsec;	/* number of sections contained */
	struct section **v_sec;	/* list of sections contained */

	STAILQ_ENTRY(segment) seg_list; /* next segment */
};

/*
 * In-memory representation of ar(1) archive member(object).
 */
struct ar_obj {
	char	*name;		/* member name */
	char	*buf;		/* member content */
	void	*maddr;		/* mmap start address */
	uid_t	 uid;		/* user id */
	gid_t	 gid;		/* group id */
	mode_t	 md;		/* octal file permissions */
	size_t	 size;		/* member size */
	time_t	 mtime;		/* modification time */

	STAILQ_ENTRY(ar_obj) objs;
};

/*
 * Structure encapsulates the "global" data for "elfcopy" program.
 */
struct elfcopy {
	const char	*progname; /* program name */
	int		 iec;	/* elfclass of input object */
	Elftc_Bfd_Target_Flavor itf; /* flavour of input object */
	Elftc_Bfd_Target_Flavor otf; /* flavour of output object */
	const char	*otgt;	/* output target name */
	int		 oec;	/* elfclass of output object */
	unsigned char	 oed;	/* endianness of output object */
	int		 oem;	/* EM_XXX of output object */
	int		 abi;	/* OSABI of output object */
	Elf		*ein;	/* ELF descriptor of input object */
	Elf		*eout;	/* ELF descriptor of output object */
	int		 iphnum; /* num. of input object phdr entries */
	int		 ophnum; /* num. of output object phdr entries */
	int		 nos;	/* num. of output object sections */

	enum {
		STRIP_NONE = 0,
		STRIP_ALL,
		STRIP_DEBUG,
		STRIP_DWO,
		STRIP_NONDEBUG,
		STRIP_NONDWO,
		STRIP_UNNEEDED
	} strip;

#define	EXECUTABLE	0x00000001U
#define	DYNAMIC		0x00000002U
#define	RELOCATABLE	0x00000004U
#define	SYMTAB_EXIST	0x00000010U
#define	SYMTAB_INTACT	0x00000020U
#define	KEEP_GLOBAL	0x00000040U
#define	DISCARD_LOCAL	0x00000080U
#define	WEAKEN_ALL	0x00000100U
#define	PRESERVE_DATE	0x00001000U
#define	SREC_FORCE_S3	0x00002000U
#define	SREC_FORCE_LEN	0x00004000U
#define	SET_START	0x00008000U
#define	GAP_FILL	0x00010000U
#define	WILDCARD	0x00020000U
#define	NO_CHANGE_WARN	0x00040000U
#define	SEC_ADD		0x00080000U
#define	SEC_APPEND	0x00100000U
#define	SEC_COMPRESS	0x00200000U
#define	SEC_PRINT	0x00400000U
#define	SEC_REMOVE	0x00800000U
#define	SEC_COPY	0x01000000U
#define	DISCARD_LLABEL	0x02000000U
#define	LOCALIZE_HIDDEN	0x04000000U

	int		 flags;		/* elfcopy run control flags. */
	int64_t		 change_addr;	/* Section address adjustment. */
	int64_t		 change_start;	/* Entry point adjustment. */
	uint64_t	 set_start;	/* Entry point value. */
	unsigned long	 srec_len;	/* S-Record length. */
	uint64_t	 pad_to;	/* load address padding. */
	uint8_t		 fill;		/* gap fill value. */
	char		*prefix_sec;	/* section prefix. */
	char		*prefix_alloc;	/* alloc section prefix. */
	char		*prefix_sym;	/* symbol prefix. */
	char		*debuglink;	/* GNU debuglink file. */
	struct section	*symtab;	/* .symtab section. */
	struct section	*strtab;	/* .strtab section. */
	struct section	*shstrtab;	/* .shstrtab section. */
	uint64_t	*secndx;	/* section index map. */
	uint64_t	*symndx;	/* symbol index map. */
	unsigned char	*v_rel;		/* symbols needed by relocation. */
	unsigned char	*v_grp;		/* symbols referred by section group. */
	unsigned char	*v_secsym;	/* sections with section symbol. */
	STAILQ_HEAD(, segment) v_seg;	/* list of segments. */
	STAILQ_HEAD(, sec_action) v_sac;/* list of section operations. */
	STAILQ_HEAD(, sec_add) v_sadd;	/* list of sections to add. */
	STAILQ_HEAD(, symop) v_symop;	/* list of symbols operations. */
	STAILQ_HEAD(, symfile) v_symfile; /* list of symlist files. */
	TAILQ_HEAD(, section) v_sec;	/* list of sections. */

	/*
	 * Fields for the ar(1) archive.
	 */
	char		*as;		/* buffer for archive string table. */
	size_t		 as_sz;		/* current size of as table. */
	size_t		 as_cap;	/* capacity of as table buffer. */
	uint32_t	 s_cnt;		/* current number of symbols. */
	uint32_t	*s_so;		/* symbol offset table. */
	size_t		 s_so_cap;	/* capacity of so table buffer. */
	char		*s_sn;		/* symbol name table */
	size_t		 s_sn_cap;	/* capacity of sn table buffer. */
	size_t		 s_sn_sz;	/* current size of sn table. */
	off_t		 rela_off;	/* offset relative to pseudo members. */
	STAILQ_HEAD(, ar_obj) v_arobj;	/* archive object(member) list. */
};

void	add_section(struct elfcopy *_ecp, const char *_optarg);
void	add_to_shstrtab(struct elfcopy *_ecp, const char *_name);
void	add_to_symop_list(struct elfcopy *_ecp, const char *_name,
    const char *_newname, unsigned int _op);
void	add_to_symtab(struct elfcopy *_ecp, const char *_name,
    uint64_t _st_value, uint64_t _st_size, uint16_t _st_shndx,
    unsigned char _st_info, unsigned char _st_other, int _ndx_known);
int	add_to_inseg_list(struct elfcopy *_ecp, struct section *_sec);
void	adjust_addr(struct elfcopy *_ecp);
void	copy_content(struct elfcopy *_ecp);
void	copy_data(struct section *_s);
void	copy_phdr(struct elfcopy *_ecp);
void	copy_shdr(struct elfcopy *_ecp, struct section *_s, const char *_name,
    int _copy, int _sec_flags);
void	create_binary(int _ifd, int _ofd);
void	create_elf(struct elfcopy *_ecp);
void	create_elf_from_binary(struct elfcopy *_ecp, int _ifd, const char *ifn);
void	create_elf_from_ihex(struct elfcopy *_ecp, int _ifd);
void	create_elf_from_srec(struct elfcopy *_ecp, int _ifd);
struct section *create_external_section(struct elfcopy *_ecp, const char *_name,
    char *_newname, void *_buf, uint64_t _size, uint64_t _off, uint64_t _stype,
    Elf_Type _dtype, uint64_t flags, uint64_t _align, uint64_t _vma,
    int _loadable);
void	create_external_symtab(struct elfcopy *_ecp);
void	create_ihex(int _ifd, int _ofd);
void	create_pe(struct elfcopy *_ecp, int _ifd, int _ofd);
void	create_scn(struct elfcopy *_ecp);
void	create_srec(struct elfcopy *_ecp, int _ifd, int _ofd, const char *_ofn);
void	create_symtab(struct elfcopy *_ecp);
void	create_symtab_data(struct elfcopy *_ecp);
void	create_tempfile(char **_fn, int *_fd);
void	finalize_external_symtab(struct elfcopy *_ecp);
void	free_elf(struct elfcopy *_ecp);
void	free_sec_act(struct elfcopy *_ecp);
void	free_sec_add(struct elfcopy *_ecp);
void	free_symtab(struct elfcopy *_ecp);
void	init_shstrtab(struct elfcopy *_ecp);
void	insert_to_sec_list(struct elfcopy *_ecp, struct section *_sec,
    int _tail);
struct section *insert_shtab(struct elfcopy *_ecp, int tail);
int	is_remove_reloc_sec(struct elfcopy *_ecp, uint32_t _sh_info);
int	is_remove_section(struct elfcopy *_ecp, const char *_name);
struct sec_action *lookup_sec_act(struct elfcopy *_ecp,
    const char *_name, int _add);
struct symop *lookup_symop_list(struct elfcopy *_ecp, const char *_name,
    unsigned int _op);
void	resync_sections(struct elfcopy *_ecp);
void	set_shstrtab(struct elfcopy *_ecp);
void	setup_phdr(struct elfcopy *_ecp);
void	update_shdr(struct elfcopy *_ecp, int _update_link);

#ifndef LIBELF_AR
int	ac_detect_ar(int _ifd);
void	ac_create_ar(struct elfcopy *_ecp, int _ifd, int _ofd);
#endif	/* ! LIBELF_AR */
