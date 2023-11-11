/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__ALPHA_A_OUT_H__
#define _UAPI__ALPHA_A_OUT_H__

#include <linux/types.h>

/*
 * OSF/1 ECOFF header structs.  ECOFF files consist of:
 * 	- a file header (struct filehdr),
 *	- an a.out header (struct aouthdr),
 *	- one or more section headers (struct scnhdr). 
 *	  The filhdr's "f_nscns" field contains the
 *	  number of section headers.
 */

struct filehdr
{
	/* OSF/1 "file" header */
	__u16 f_magic, f_nscns;
	__u32 f_timdat;
	__u64 f_symptr;
	__u32 f_nsyms;
	__u16 f_opthdr, f_flags;
};

struct aouthdr
{
	__u64 info;		/* after that it looks quite normal.. */
	__u64 tsize;
	__u64 dsize;
	__u64 bsize;
	__u64 entry;
	__u64 text_start;	/* with a few additions that actually make sense */
	__u64 data_start;
	__u64 bss_start;
	__u32 gprmask, fprmask;	/* bitmask of general & floating point regs used in binary */
	__u64 gpvalue;
};

struct scnhdr
{
	char	s_name[8];
	__u64	s_paddr;
	__u64	s_vaddr;
	__u64	s_size;
	__u64	s_scnptr;
	__u64	s_relptr;
	__u64	s_lnnoptr;
	__u16	s_nreloc;
	__u16	s_nlnno;
	__u32	s_flags;
};

struct exec
{
	/* OSF/1 "file" header */
	struct filehdr		fh;
	struct aouthdr		ah;
};

/*
 * Define's so that the kernel exec code can access the a.out header
 * fields...
 */
#define	a_info		ah.info
#define	a_text		ah.tsize
#define a_data		ah.dsize
#define a_bss		ah.bsize
#define a_entry		ah.entry
#define a_textstart	ah.text_start
#define	a_datastart	ah.data_start
#define	a_bssstart	ah.bss_start
#define	a_gprmask	ah.gprmask
#define a_fprmask	ah.fprmask
#define a_gpvalue	ah.gpvalue

#define N_TXTADDR(x) ((x).a_textstart)
#define N_DATADDR(x) ((x).a_datastart)
#define N_BSSADDR(x) ((x).a_bssstart)
#define N_DRSIZE(x) 0
#define N_TRSIZE(x) 0
#define N_SYMSIZE(x) 0

#define AOUTHSZ		sizeof(struct aouthdr)
#define SCNHSZ		sizeof(struct scnhdr)
#define SCNROUND	16

#define N_TXTOFF(x) \
  ((long) N_MAGIC(x) == ZMAGIC ? 0 : \
   (sizeof(struct exec) + (x).fh.f_nscns*SCNHSZ + SCNROUND - 1) & ~(SCNROUND - 1))

#endif /* _UAPI__ALPHA_A_OUT_H__ */
