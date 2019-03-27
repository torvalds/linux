/*-
 * Copyright (c) 2007-2010,2012 Kai Wang
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

#include <sys/queue.h>
#include <err.h>
#include <gelf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elfcopy.h"

ELFTC_VCSID("$Id: segments.c 3615 2018-05-17 04:12:24Z kaiwang27 $");

static void	insert_to_inseg_list(struct segment *seg, struct section *sec);

/*
 * elfcopy's segment handling is relatively simpler and less powerful than
 * libbfd. Program headers are modified or copied from input to output objects,
 * but never re-generated. As a result, if the input object has incorrect
 * program headers, the output object's program headers will remain incorrect
 * or become even worse.
 */

/*
 * Check whether a section is "loadable". If so, add it to the
 * corresponding segment list(s) and return 1.
 */
int
add_to_inseg_list(struct elfcopy *ecp, struct section *s)
{
	struct segment	*seg;
	int		 loadable;

	if (ecp->ophnum == 0)
		return (0);

	/*
	 * Segment is a different view of an ELF object. One segment can
	 * contain one or more sections, and one section can be included
	 * in one or more segments, or not included in any segment at all.
	 * We call those sections which can be found in one or more segments
	 * "loadable" sections, and call the rest "unloadable" sections.
	 * We keep track of "loadable" sections in their containing
	 * segment(s)' v_sec queue. These information are later used to
	 * recalculate the extents of segments, when sections are removed,
	 * for example.
	 */
	loadable = 0;
	STAILQ_FOREACH(seg, &ecp->v_seg, seg_list) {
		if (s->off < seg->off || (s->vma < seg->vaddr && !s->pseudo))
			continue;
		if (s->off + s->sz > seg->off + seg->fsz &&
		    s->type != SHT_NOBITS)
			continue;
		if (s->vma + s->sz > seg->vaddr + seg->msz)
			continue;
		if (seg->type == PT_TLS && ((s->flags & SHF_TLS) == 0))
			continue;

		insert_to_inseg_list(seg, s);
		if (seg->type == PT_LOAD)
			s->seg = seg;
		else if (seg->type == PT_TLS)
			s->seg_tls = seg;
		if (s->pseudo)
			s->vma = seg->vaddr + (s->off - seg->off);
		if (seg->paddr > 0)
			s->lma = seg->paddr + (s->off - seg->off);
		else
			s->lma = 0;
		loadable = 1;
	}

	return (loadable);
}

void
adjust_addr(struct elfcopy *ecp)
{
	struct section *s, *s0;
	struct segment *seg;
	struct sec_action *sac;
	uint64_t dl, vma, lma, start, end;
	int found, i;

	/*
	 * Apply VMA and global LMA changes in the first iteration.
	 */
	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {

		/* Only adjust loadable section's address. */
		if (!s->loadable)
			continue;

		/* Apply global VMA adjustment. */
		if (ecp->change_addr != 0)
			s->vma += ecp->change_addr;

		/* Apply global LMA adjustment. */
		if (ecp->change_addr != 0 && s->seg != NULL &&
		    s->seg->paddr > 0)
			s->lma += ecp->change_addr;
	}

	/*
	 * Apply sections VMA change in the second iteration.
	 */
	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {

		if (!s->loadable)
			continue;

		/*
		 * Check if there is a VMA change request for this
		 * section.
		 */
		sac = lookup_sec_act(ecp, s->name, 0);
		if (sac == NULL)
			continue;
		vma = s->vma;
		if (sac->setvma)
			vma = sac->vma;
		if (sac->vma_adjust != 0)
			vma += sac->vma_adjust;
		if (vma == s->vma)
			continue;

		/*
		 * No need to make segment adjustment if the section doesn't
		 * belong to any segment.
		 */
		if (s->seg == NULL) {
			s->vma = vma;
			continue;
		}

		/*
		 * Check if the VMA change is viable.
		 *
		 * 1. Check if the new VMA is properly aligned accroding to
		 *    section alignment.
		 *
		 * 2. Compute the new extent of segment that contains this
		 *    section, make sure it doesn't overlap with other
		 *    segments.
		 */
#ifdef	DEBUG
		printf("VMA for section %s: %#jx\n", s->name, vma);
#endif

		if (vma % s->align != 0)
			errx(EXIT_FAILURE, "The VMA %#jx for "
			    "section %s is not aligned to %ju",
			    (uintmax_t) vma, s->name, (uintmax_t) s->align);

		if (vma < s->vma) {
			/* Move section to lower address. */
			if (vma < s->vma - s->seg->vaddr)
				errx(EXIT_FAILURE, "Not enough space to move "
				    "section %s VMA to %#jx", s->name,
				    (uintmax_t) vma);
			start = vma - (s->vma - s->seg->vaddr);
			if (s == s->seg->v_sec[s->seg->nsec - 1])
				end = start + s->seg->msz;
			else
				end = s->seg->vaddr + s->seg->msz;
		} else {
			/* Move section to upper address. */
			if (s == s->seg->v_sec[0])
				start = vma;
			else
				start = s->seg->vaddr;
			end = vma + (s->seg->vaddr + s->seg->msz - s->vma);
			if (end < start)
				errx(EXIT_FAILURE, "Not enough space to move "
				    "section %s VMA to %#jx", s->name,
				    (uintmax_t) vma);
		}

#ifdef	DEBUG
		printf("new extent for segment containing %s: (%#jx,%#jx)\n",
		    s->name, start, end);
#endif

		STAILQ_FOREACH(seg, &ecp->v_seg, seg_list) {
			if (seg == s->seg || seg->type != PT_LOAD)
				continue;
			if (start > seg->vaddr + seg->msz)
				continue;
			if (end < seg->vaddr)
				continue;
			errx(EXIT_FAILURE, "The extent of segment containing "
			    "section %s overlaps with segment(%#jx,%#jx)",
			    s->name, (uintmax_t) seg->vaddr,
			    (uintmax_t) (seg->vaddr + seg->msz));
		}

		/*
		 * Update section VMA and file offset.
		 */

		if (vma < s->vma) {
			/*
			 * To move a section to lower VMA, we decrease
			 * the VMA of the section and all the sections that
			 * are before it, and we increase the file offsets
			 * of all the sections that are after it.
			 */
			dl = s->vma - vma;
			for (i = 0; i < s->seg->nsec; i++) {
				s0 = s->seg->v_sec[i];
				s0->vma -= dl;
#ifdef	DEBUG
				printf("section %s VMA set to %#jx\n",
				    s0->name, (uintmax_t) s0->vma);
#endif
				if (s0 == s)
					break;
			}
			for (i = i + 1; i < s->seg->nsec; i++) {
				s0 = s->seg->v_sec[i];
				s0->off += dl;
#ifdef	DEBUG
				printf("section %s offset set to %#jx\n",
				    s0->name, (uintmax_t) s0->off);
#endif
			}
		} else {
			/*
			 * To move a section to upper VMA, we increase
			 * the VMA of the section and all the sections that
			 * are after it, and we increase the their file
			 * offsets too unless the section in question
			 * is the first in its containing segment.
			 */
			dl = vma - s->vma;
			for (i = 0; i < s->seg->nsec; i++)
				if (s->seg->v_sec[i] == s)
					break;
			if (i >= s->seg->nsec)
				errx(EXIT_FAILURE, "Internal: section `%s' not"
				    " found in its containing segement",
				    s->name);
			for (; i < s->seg->nsec; i++) {
				s0 = s->seg->v_sec[i];
				s0->vma += dl;
#ifdef	DEBUG
				printf("section %s VMA set to %#jx\n",
				    s0->name, (uintmax_t) s0->lma);
#endif
				if (s != s->seg->v_sec[0]) {
					s0->off += dl;
#ifdef	DEBUG
					printf("section %s offset set to %#jx\n",
					    s0->name, (uintmax_t) s0->off);
#endif
				}
			}
		}
	}

	/*
	 * Apply load address padding.
	 */

	if (ecp->pad_to != 0) {

		/*
		 * Find the section with highest VMA.
		 */
		s = NULL;
		STAILQ_FOREACH(seg, &ecp->v_seg, seg_list) {
			if (seg->type != PT_LOAD)
				continue;
			for (i = seg->nsec - 1; i >= 0; i--)
				if (seg->v_sec[i]->type != SHT_NOBITS)
					break;
			if (i < 0)
				continue;
			if (s == NULL)
				s = seg->v_sec[i];
			else {
				s0 = seg->v_sec[i];
				if (s0->vma > s->vma)
					s = s0;
			}
		}

		if (s == NULL)
			goto adjust_lma;

		/* No need to pad if the pad_to address is lower. */
		if (ecp->pad_to <= s->vma + s->sz)
			goto adjust_lma;

		s->pad_sz = ecp->pad_to - (s->vma + s->sz);
#ifdef	DEBUG
		printf("pad section %s VMA to address %#jx by %#jx\n", s->name,
		    (uintmax_t) ecp->pad_to, (uintmax_t) s->pad_sz);
#endif
	}


adjust_lma:

	/*
	 * Apply sections LMA change in the third iteration.
	 */
	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {

		/*
		 * Only loadable section that's inside a segment can have
		 * LMA adjusted. Also, if LMA of the containing segment is
		 * set to 0, it probably means we should ignore the LMA.
		 */
		if (!s->loadable || s->seg == NULL || s->seg->paddr == 0)
			continue;

		/*
		 * Check if there is a LMA change request for this
		 * section.
		 */
		sac = lookup_sec_act(ecp, s->name, 0);
		if (sac == NULL)
			continue;
		if (!sac->setlma && sac->lma_adjust == 0)
			continue;
		lma = s->lma;
		if (sac->setlma)
			lma = sac->lma;
		if (sac->lma_adjust != 0)
			lma += sac->lma_adjust;
		if (lma == s->lma)
			continue;

#ifdef	DEBUG
		printf("LMA for section %s: %#jx\n", s->name, lma);
#endif

		/* Check alignment. */
		if (lma % s->align != 0)
			errx(EXIT_FAILURE, "The LMA %#jx for "
			    "section %s is not aligned to %ju",
			    (uintmax_t) lma, s->name, (uintmax_t) s->align);

		/*
		 * Update section LMA.
		 */

		if (lma < s->lma) {
			/*
			 * To move a section to lower LMA, we decrease
			 * the LMA of the section and all the sections that
			 * are before it.
			 */
			dl = s->lma - lma;
			for (i = 0; i < s->seg->nsec; i++) {
				s0 = s->seg->v_sec[i];
				s0->lma -= dl;
#ifdef	DEBUG
				printf("section %s LMA set to %#jx\n",
				    s0->name, (uintmax_t) s0->lma);
#endif
				if (s0 == s)
					break;
			}
		} else {
			/*
			 * To move a section to upper LMA, we increase
			 * the LMA of the section and all the sections that
			 * are after it.
			 */
			dl = lma - s->lma;
			for (i = 0; i < s->seg->nsec; i++)
				if (s->seg->v_sec[i] == s)
					break;
			if (i >= s->seg->nsec)
				errx(EXIT_FAILURE, "Internal: section `%s' not"
				    " found in its containing segement",
				    s->name);
			for (; i < s->seg->nsec; i++) {
				s0 = s->seg->v_sec[i];
				s0->lma += dl;
#ifdef	DEBUG
				printf("section %s LMA set to %#jx\n",
				    s0->name, (uintmax_t) s0->lma);
#endif
			}
		}
	}

	/*
	 * Issue a warning if there are VMA/LMA adjust requests for
	 * some nonexistent sections.
	 */
	if ((ecp->flags & NO_CHANGE_WARN) == 0) {
		STAILQ_FOREACH(sac, &ecp->v_sac, sac_list) {
			if (!sac->setvma && !sac->setlma &&
			    !sac->vma_adjust && !sac->lma_adjust)
				continue;
			found = 0;
			TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {
				if (s->pseudo || s->name == NULL)
					continue;
				if (!strcmp(s->name, sac->name)) {
					found = 1;
					break;
				}
			}
			if (!found)
				warnx("cannot find section `%s'", sac->name);
		}
	}
}

static void
insert_to_inseg_list(struct segment *seg, struct section *sec)
{
	struct section *s;
	int i;

	seg->nsec++;
	seg->v_sec = realloc(seg->v_sec, seg->nsec * sizeof(*seg->v_sec));
	if (seg->v_sec == NULL)
		err(EXIT_FAILURE, "realloc failed");

	/*
	 * Sort the section in order of offset.
	 */

	for (i = seg->nsec - 1; i > 0; i--) {
		s = seg->v_sec[i - 1];
		if (sec->off >= s->off) {
			seg->v_sec[i] = sec;
			break;
		} else
			seg->v_sec[i] = s;
	}
	if (i == 0)
		seg->v_sec[0] = sec;
}

void
setup_phdr(struct elfcopy *ecp)
{
	struct segment	*seg;
	GElf_Phdr	 iphdr;
	size_t		 iphnum, i;

	if (elf_getphnum(ecp->ein, &iphnum) == 0)
		errx(EXIT_FAILURE, "elf_getphnum failed: %s",
		    elf_errmsg(-1));

	ecp->ophnum = ecp->iphnum = iphnum;
	if (iphnum == 0)
		return;

	/* If --only-keep-debug is specified, discard all program headers. */
	if (ecp->strip == STRIP_NONDEBUG) {
		ecp->ophnum = 0;
		return;
	}

	for (i = 0; i < iphnum; i++) {
		if (gelf_getphdr(ecp->ein, i, &iphdr) != &iphdr)
			errx(EXIT_FAILURE, "gelf_getphdr failed: %s",
			    elf_errmsg(-1));
		if ((seg = calloc(1, sizeof(*seg))) == NULL)
			err(EXIT_FAILURE, "calloc failed");
		seg->vaddr	= iphdr.p_vaddr;
		seg->paddr	= iphdr.p_paddr;
		seg->off	= iphdr.p_offset;
		seg->fsz	= iphdr.p_filesz;
		seg->msz	= iphdr.p_memsz;
		seg->type	= iphdr.p_type;
		STAILQ_INSERT_TAIL(&ecp->v_seg, seg, seg_list);
	}
}

void
copy_phdr(struct elfcopy *ecp)
{
	struct segment	*seg;
	struct section	*s;
	GElf_Phdr	 iphdr, ophdr;
	int		 i;

	STAILQ_FOREACH(seg, &ecp->v_seg, seg_list) {
		if (seg->type == PT_PHDR) {
			if (!TAILQ_EMPTY(&ecp->v_sec)) {
				s = TAILQ_FIRST(&ecp->v_sec);
				if (s->pseudo) {
					seg->vaddr = s->vma +
					    gelf_fsize(ecp->eout, ELF_T_EHDR,
						1, EV_CURRENT);
					seg->paddr = s->lma +
					    gelf_fsize(ecp->eout, ELF_T_EHDR,
						1, EV_CURRENT);
				}
			}
			seg->fsz = seg->msz = gelf_fsize(ecp->eout, ELF_T_PHDR,
			    ecp->ophnum, EV_CURRENT);
			continue;
		}

		if (seg->nsec > 0) {
			s = seg->v_sec[0];
			seg->vaddr = s->vma;
			seg->paddr = s->lma;
		}

		seg->fsz = seg->msz = 0;
		for (i = 0; i < seg->nsec; i++) {
			s = seg->v_sec[i];
			seg->msz = s->vma + s->sz - seg->vaddr;
			if (s->type != SHT_NOBITS)
				seg->fsz = s->off + s->sz - seg->off;
		}
	}

	/*
	 * Allocate space for program headers, note that libelf keep
	 * track of the number in internal variable, and a call to
	 * elf_update is needed to update e_phnum of ehdr.
	 */
	if (gelf_newphdr(ecp->eout, ecp->ophnum) == NULL)
		errx(EXIT_FAILURE, "gelf_newphdr() failed: %s",
		    elf_errmsg(-1));

	/*
	 * This elf_update() call is to update the e_phnum field in
	 * ehdr. It's necessary because later we will call gelf_getphdr(),
	 * which does sanity check by comparing ndx argument with e_phnum.
	 */
	if (elf_update(ecp->eout, ELF_C_NULL) < 0)
		errx(EXIT_FAILURE, "elf_update() failed: %s", elf_errmsg(-1));

	/*
	 * iphnum == ophnum, since we don't remove program headers even if
	 * they no longer contain sections.
	 */
	i = 0;
	STAILQ_FOREACH(seg, &ecp->v_seg, seg_list) {
		if (i >= ecp->iphnum)
			break;
		if (gelf_getphdr(ecp->ein, i, &iphdr) != &iphdr)
			errx(EXIT_FAILURE, "gelf_getphdr failed: %s",
			    elf_errmsg(-1));
		if (gelf_getphdr(ecp->eout, i, &ophdr) != &ophdr)
			errx(EXIT_FAILURE, "gelf_getphdr failed: %s",
			    elf_errmsg(-1));

		ophdr.p_type = iphdr.p_type;
		ophdr.p_vaddr = seg->vaddr;
		ophdr.p_paddr = seg->paddr;
		ophdr.p_flags = iphdr.p_flags;
		ophdr.p_align = iphdr.p_align;
		ophdr.p_offset = seg->off;
		ophdr.p_filesz = seg->fsz;
		ophdr.p_memsz = seg->msz;
		if (!gelf_update_phdr(ecp->eout, i, &ophdr))
			errx(EXIT_FAILURE, "gelf_update_phdr failed: %s",
			    elf_errmsg(-1));

		i++;
	}
}
