/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Some ECOFF definitions.
 */

#include <stdint.h>

typedef struct filehdr {
	uint16_t	f_magic;	/* magic number */
	uint16_t	f_nscns;	/* number of sections */
	int32_t		f_timdat;	/* time & date stamp */
	int32_t		f_symptr;	/* file pointer to symbolic header */
	int32_t		f_nsyms;	/* sizeof(symbolic hdr) */
	uint16_t	f_opthdr;	/* sizeof(optional hdr) */
	uint16_t	f_flags;	/* flags */
} FILHDR;
#define FILHSZ	sizeof(FILHDR)

#define MIPSEBMAGIC	0x160
#define MIPSELMAGIC	0x162

typedef struct scnhdr {
	char		s_name[8];	/* section name */
	int32_t		s_paddr;	/* physical address, aliased s_nlib */
	int32_t		s_vaddr;	/* virtual address */
	int32_t		s_size;		/* section size */
	int32_t		s_scnptr;	/* file ptr to raw data for section */
	int32_t		s_relptr;	/* file ptr to relocation */
	int32_t		s_lnnoptr;	/* file ptr to gp histogram */
	uint16_t	s_nreloc;	/* number of relocation entries */
	uint16_t	s_nlnno;	/* number of gp histogram entries */
	int32_t		s_flags;	/* flags */
} SCNHDR;
#define SCNHSZ		sizeof(SCNHDR)
#define SCNROUND	((int32_t)16)

typedef struct aouthdr {
	int16_t	magic;		/* see above				*/
	int16_t	vstamp;		/* version stamp			*/
	int32_t	tsize;		/* text size in bytes, padded to DW bdry*/
	int32_t	dsize;		/* initialized data "  "		*/
	int32_t	bsize;		/* uninitialized data "	  "		*/
	int32_t	entry;		/* entry pt.				*/
	int32_t	text_start;	/* base of text used for this file	*/
	int32_t	data_start;	/* base of data used for this file	*/
	int32_t	bss_start;	/* base of bss used for this file	*/
	int32_t	gprmask;	/* general purpose register mask	*/
	int32_t	cprmask[4];	/* co-processor register masks		*/
	int32_t	gp_value;	/* the gp value used for this object	*/
} AOUTHDR;
#define AOUTHSZ sizeof(AOUTHDR)

#define OMAGIC		0407
#define NMAGIC		0410
#define ZMAGIC		0413
#define SMAGIC		0411
#define LIBMAGIC	0443

#define N_TXTOFF(f, a) \
 ((a).magic == ZMAGIC || (a).magic == LIBMAGIC ? 0 : \
  ((a).vstamp < 23 ? \
   ((FILHSZ + AOUTHSZ + (f).f_nscns * SCNHSZ + 7) & 0xfffffff8) : \
   ((FILHSZ + AOUTHSZ + (f).f_nscns * SCNHSZ + SCNROUND-1) & ~(SCNROUND-1)) ) )
#define N_DATOFF(f, a) \
  N_TXTOFF(f, a) + (a).tsize;
