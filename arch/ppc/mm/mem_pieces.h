/*
 *    Copyright (c) 1996 Paul Mackerras <paulus@cs.anu.edu.au>
 *      Changes to accommodate Power Macintoshes.
 *    Cort Dougan <cort@cs.nmt.edu>
 *      Rewrites.
 *    Grant Erickson <grant@lcse.umn.edu>
 *      General rework and split from mm/init.c.
 *
 *    Module name: mem_pieces.h
 *
 *    Description:
 *      Routines and data structures for manipulating and representing
 *      phyiscal memory extents (i.e. address/length pairs).
 *
 */

#ifndef __MEM_PIECES_H__
#define	__MEM_PIECES_H__

#include <asm/prom.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Type Definitions */

#define	MEM_PIECES_MAX	32

struct mem_pieces {
    int n_regions;
    struct reg_property regions[MEM_PIECES_MAX];
};

/* Function Prototypes */

extern void	*mem_pieces_find(unsigned int size, unsigned int align);
extern void	 mem_pieces_remove(struct mem_pieces *mp, unsigned int start,
				   unsigned int size, int must_exist);
extern void	 mem_pieces_coalesce(struct mem_pieces *mp);
extern void	 mem_pieces_sort(struct mem_pieces *mp);

#ifdef __cplusplus
}
#endif

#endif /* __MEM_PIECES_H__ */
