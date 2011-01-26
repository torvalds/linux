/*
 * rmm.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 *  This memory manager provides general heap management and arbitrary
 *  alignment for any number of memory segments.
 *
 *  Notes:
 *
 *  Memory blocks are allocated from the end of the first free memory
 *  block large enough to satisfy the request.  Alignment requirements
 *  are satisfied by "sliding" the block forward until its base satisfies
 *  the alignment specification; if this is not possible then the next
 *  free block large enough to hold the request is tried.
 *
 *  Since alignment can cause the creation of a new free block - the
 *  unused memory formed between the start of the original free block
 *  and the start of the allocated block - the memory manager must free
 *  this memory to prevent a memory leak.
 *
 *  Overlay memory is managed by reserving through rmm_alloc, and freeing
 *  it through rmm_free. The memory manager prevents DSP code/data that is
 *  overlayed from being overwritten as long as the memory it runs at has
 *  been allocated, and not yet freed.
 */

#include <linux/types.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/list.h>

/*  ----------------------------------- This */
#include <dspbridge/rmm.h>

/*
 *  ======== rmm_header ========
 *  This header is used to maintain a list of free memory blocks.
 */
struct rmm_header {
	struct rmm_header *next;	/* form a free memory link list */
	u32 size;		/* size of the free memory */
	u32 addr;		/* DSP address of memory block */
};

/*
 *  ======== rmm_ovly_sect ========
 *  Keeps track of memory occupied by overlay section.
 */
struct rmm_ovly_sect {
	struct list_head list_elem;
	u32 addr;		/* Start of memory section */
	u32 size;		/* Length (target MAUs) of section */
	s32 page;		/* Memory page */
};

/*
 *  ======== rmm_target_obj ========
 */
struct rmm_target_obj {
	struct rmm_segment *seg_tab;
	struct rmm_header **free_list;
	u32 num_segs;
	struct lst_list *ovly_list;	/* List of overlay memory in use */
};

static u32 refs;		/* module reference count */

static bool alloc_block(struct rmm_target_obj *target, u32 segid, u32 size,
			u32 align, u32 *dsp_address);
static bool free_block(struct rmm_target_obj *target, u32 segid, u32 addr,
		       u32 size);

/*
 *  ======== rmm_alloc ========
 */
int rmm_alloc(struct rmm_target_obj *target, u32 segid, u32 size,
		     u32 align, u32 *dsp_address, bool reserve)
{
	struct rmm_ovly_sect *sect;
	struct rmm_ovly_sect *prev_sect = NULL;
	struct rmm_ovly_sect *new_sect;
	u32 addr;
	int status = 0;

	DBC_REQUIRE(target);
	DBC_REQUIRE(dsp_address != NULL);
	DBC_REQUIRE(size > 0);
	DBC_REQUIRE(reserve || (target->num_segs > 0));
	DBC_REQUIRE(refs > 0);

	if (!reserve) {
		if (!alloc_block(target, segid, size, align, dsp_address)) {
			status = -ENOMEM;
		} else {
			/* Increment the number of allocated blocks in this
			 * segment */
			target->seg_tab[segid].number++;
		}
		goto func_end;
	}
	/* An overlay section - See if block is already in use. If not,
	 * insert into the list in ascending address size. */
	addr = *dsp_address;
	sect = (struct rmm_ovly_sect *)lst_first(target->ovly_list);
	/*  Find place to insert new list element. List is sorted from
	 *  smallest to largest address. */
	while (sect != NULL) {
		if (addr <= sect->addr) {
			/* Check for overlap with sect */
			if ((addr + size > sect->addr) || (prev_sect &&
							   (prev_sect->addr +
							    prev_sect->size >
							    addr))) {
				status = -ENXIO;
			}
			break;
		}
		prev_sect = sect;
		sect = (struct rmm_ovly_sect *)lst_next(target->ovly_list,
							(struct list_head *)
							sect);
	}
	if (!status) {
		/* No overlap - allocate list element for new section. */
		new_sect = kzalloc(sizeof(struct rmm_ovly_sect), GFP_KERNEL);
		if (new_sect == NULL) {
			status = -ENOMEM;
		} else {
			lst_init_elem((struct list_head *)new_sect);
			new_sect->addr = addr;
			new_sect->size = size;
			new_sect->page = segid;
			if (sect == NULL) {
				/* Put new section at the end of the list */
				lst_put_tail(target->ovly_list,
					     (struct list_head *)new_sect);
			} else {
				/* Put new section just before sect */
				lst_insert_before(target->ovly_list,
						  (struct list_head *)new_sect,
						  (struct list_head *)sect);
			}
		}
	}
func_end:
	return status;
}

/*
 *  ======== rmm_create ========
 */
int rmm_create(struct rmm_target_obj **target_obj,
		      struct rmm_segment seg_tab[], u32 num_segs)
{
	struct rmm_header *hptr;
	struct rmm_segment *sptr, *tmp;
	struct rmm_target_obj *target;
	s32 i;
	int status = 0;

	DBC_REQUIRE(target_obj != NULL);
	DBC_REQUIRE(num_segs == 0 || seg_tab != NULL);

	/* Allocate DBL target object */
	target = kzalloc(sizeof(struct rmm_target_obj), GFP_KERNEL);

	if (target == NULL)
		status = -ENOMEM;

	if (status)
		goto func_cont;

	target->num_segs = num_segs;
	if (!(num_segs > 0))
		goto func_cont;

	/* Allocate the memory for freelist from host's memory */
	target->free_list = kzalloc(num_segs * sizeof(struct rmm_header *),
							GFP_KERNEL);
	if (target->free_list == NULL) {
		status = -ENOMEM;
	} else {
		/* Allocate headers for each element on the free list */
		for (i = 0; i < (s32) num_segs; i++) {
			target->free_list[i] =
				kzalloc(sizeof(struct rmm_header), GFP_KERNEL);
			if (target->free_list[i] == NULL) {
				status = -ENOMEM;
				break;
			}
		}
		/* Allocate memory for initial segment table */
		target->seg_tab = kzalloc(num_segs * sizeof(struct rmm_segment),
								GFP_KERNEL);
		if (target->seg_tab == NULL) {
			status = -ENOMEM;
		} else {
			/* Initialize segment table and free list */
			sptr = target->seg_tab;
			for (i = 0, tmp = seg_tab; num_segs > 0;
			     num_segs--, i++) {
				*sptr = *tmp;
				hptr = target->free_list[i];
				hptr->addr = tmp->base;
				hptr->size = tmp->length;
				hptr->next = NULL;
				tmp++;
				sptr++;
			}
		}
	}
func_cont:
	/* Initialize overlay memory list */
	if (!status) {
		target->ovly_list = kzalloc(sizeof(struct lst_list),
							GFP_KERNEL);
		if (target->ovly_list == NULL)
			status = -ENOMEM;
		else
			INIT_LIST_HEAD(&target->ovly_list->head);
	}

	if (!status) {
		*target_obj = target;
	} else {
		*target_obj = NULL;
		if (target)
			rmm_delete(target);

	}

	DBC_ENSURE((!status && *target_obj)
		   || (status && *target_obj == NULL));

	return status;
}

/*
 *  ======== rmm_delete ========
 */
void rmm_delete(struct rmm_target_obj *target)
{
	struct rmm_ovly_sect *ovly_section;
	struct rmm_header *hptr;
	struct rmm_header *next;
	u32 i;

	DBC_REQUIRE(target);

	kfree(target->seg_tab);

	if (target->ovly_list) {
		while ((ovly_section = (struct rmm_ovly_sect *)lst_get_head
			(target->ovly_list))) {
			kfree(ovly_section);
		}
		DBC_ASSERT(LST_IS_EMPTY(target->ovly_list));
		kfree(target->ovly_list);
	}

	if (target->free_list != NULL) {
		/* Free elements on freelist */
		for (i = 0; i < target->num_segs; i++) {
			hptr = next = target->free_list[i];
			while (next) {
				hptr = next;
				next = hptr->next;
				kfree(hptr);
			}
		}
		kfree(target->free_list);
	}

	kfree(target);
}

/*
 *  ======== rmm_exit ========
 */
void rmm_exit(void)
{
	DBC_REQUIRE(refs > 0);

	refs--;

	DBC_ENSURE(refs >= 0);
}

/*
 *  ======== rmm_free ========
 */
bool rmm_free(struct rmm_target_obj *target, u32 segid, u32 dsp_addr, u32 size,
	      bool reserved)
{
	struct rmm_ovly_sect *sect;
	bool ret = true;

	DBC_REQUIRE(target);

	DBC_REQUIRE(reserved || segid < target->num_segs);
	DBC_REQUIRE(reserved || (dsp_addr >= target->seg_tab[segid].base &&
				 (dsp_addr + size) <= (target->seg_tab[segid].
						   base +
						   target->seg_tab[segid].
						   length)));

	/*
	 *  Free or unreserve memory.
	 */
	if (!reserved) {
		ret = free_block(target, segid, dsp_addr, size);
		if (ret)
			target->seg_tab[segid].number--;

	} else {
		/* Unreserve memory */
		sect = (struct rmm_ovly_sect *)lst_first(target->ovly_list);
		while (sect != NULL) {
			if (dsp_addr == sect->addr) {
				DBC_ASSERT(size == sect->size);
				/* Remove from list */
				lst_remove_elem(target->ovly_list,
						(struct list_head *)sect);
				kfree(sect);
				break;
			}
			sect =
			    (struct rmm_ovly_sect *)lst_next(target->ovly_list,
							     (struct list_head
							      *)sect);
		}
		if (sect == NULL)
			ret = false;

	}
	return ret;
}

/*
 *  ======== rmm_init ========
 */
bool rmm_init(void)
{
	DBC_REQUIRE(refs >= 0);

	refs++;

	return true;
}

/*
 *  ======== rmm_stat ========
 */
bool rmm_stat(struct rmm_target_obj *target, enum dsp_memtype segid,
	      struct dsp_memstat *mem_stat_buf)
{
	struct rmm_header *head;
	bool ret = false;
	u32 max_free_size = 0;
	u32 total_free_size = 0;
	u32 free_blocks = 0;

	DBC_REQUIRE(mem_stat_buf != NULL);
	DBC_ASSERT(target != NULL);

	if ((u32) segid < target->num_segs) {
		head = target->free_list[segid];

		/* Collect data from free_list */
		while (head != NULL) {
			max_free_size = max(max_free_size, head->size);
			total_free_size += head->size;
			free_blocks++;
			head = head->next;
		}

		/* ul_size */
		mem_stat_buf->ul_size = target->seg_tab[segid].length;

		/* ul_num_free_blocks */
		mem_stat_buf->ul_num_free_blocks = free_blocks;

		/* ul_total_free_size */
		mem_stat_buf->ul_total_free_size = total_free_size;

		/* ul_len_max_free_block */
		mem_stat_buf->ul_len_max_free_block = max_free_size;

		/* ul_num_alloc_blocks */
		mem_stat_buf->ul_num_alloc_blocks =
		    target->seg_tab[segid].number;

		ret = true;
	}

	return ret;
}

/*
 *  ======== balloc ========
 *  This allocation function allocates memory from the lowest addresses
 *  first.
 */
static bool alloc_block(struct rmm_target_obj *target, u32 segid, u32 size,
			u32 align, u32 *dsp_address)
{
	struct rmm_header *head;
	struct rmm_header *prevhead = NULL;
	struct rmm_header *next;
	u32 tmpalign;
	u32 alignbytes;
	u32 hsize;
	u32 allocsize;
	u32 addr;

	alignbytes = (align == 0) ? 1 : align;
	prevhead = NULL;
	head = target->free_list[segid];

	do {
		hsize = head->size;
		next = head->next;

		addr = head->addr;	/* alloc from the bottom */

		/* align allocation */
		(tmpalign = (u32) addr % alignbytes);
		if (tmpalign != 0)
			tmpalign = alignbytes - tmpalign;

		allocsize = size + tmpalign;

		if (hsize >= allocsize) {	/* big enough */
			if (hsize == allocsize && prevhead != NULL) {
				prevhead->next = next;
				kfree(head);
			} else {
				head->size = hsize - allocsize;
				head->addr += allocsize;
			}

			/* free up any hole created by alignment */
			if (tmpalign)
				free_block(target, segid, addr, tmpalign);

			*dsp_address = addr + tmpalign;
			return true;
		}

		prevhead = head;
		head = next;

	} while (head != NULL);

	return false;
}

/*
 *  ======== free_block ========
 *  TO DO: free_block() allocates memory, which could result in failure.
 *  Could allocate an rmm_header in rmm_alloc(), to be kept in a pool.
 *  free_block() could use an rmm_header from the pool, freeing as blocks
 *  are coalesced.
 */
static bool free_block(struct rmm_target_obj *target, u32 segid, u32 addr,
		       u32 size)
{
	struct rmm_header *head;
	struct rmm_header *thead;
	struct rmm_header *rhead;
	bool ret = true;

	/* Create a memory header to hold the newly free'd block. */
	rhead = kzalloc(sizeof(struct rmm_header), GFP_KERNEL);
	if (rhead == NULL) {
		ret = false;
	} else {
		/* search down the free list to find the right place for addr */
		head = target->free_list[segid];

		if (addr >= head->addr) {
			while (head->next != NULL && addr > head->next->addr)
				head = head->next;

			thead = head->next;

			head->next = rhead;
			rhead->next = thead;
			rhead->addr = addr;
			rhead->size = size;
		} else {
			*rhead = *head;
			head->next = rhead;
			head->addr = addr;
			head->size = size;
			thead = rhead->next;
		}

		/* join with upper block, if possible */
		if (thead != NULL && (rhead->addr + rhead->size) ==
		    thead->addr) {
			head->next = rhead->next;
			thead->size = size + thead->size;
			thead->addr = addr;
			kfree(rhead);
			rhead = thead;
		}

		/* join with the lower block, if possible */
		if ((head->addr + head->size) == rhead->addr) {
			head->next = rhead->next;
			head->size = head->size + rhead->size;
			kfree(rhead);
		}
	}

	return ret;
}
