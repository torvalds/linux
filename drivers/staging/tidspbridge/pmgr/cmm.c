/*
 * cmm.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * The Communication(Shared) Memory Management(CMM) module provides
 * shared memory management services for DSP/BIOS Bridge data streaming
 * and messaging.
 *
 * Multiple shared memory segments can be registered with CMM.
 * Each registered SM segment is represented by a SM "allocator" that
 * describes a block of physically contiguous shared memory used for
 * future allocations by CMM.
 *
 * Memory is coalesced back to the appropriate heap when a buffer is
 * freed.
 *
 * Notes:
 *   Va: Virtual address.
 *   Pa: Physical or kernel system address.
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
#include <linux/types.h>
#include <linux/list.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/sync.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/proc.h>

/*  ----------------------------------- This */
#include <dspbridge/cmm.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define NEXT_PA(pnode)   (pnode->pa + pnode->size)

/* Other bus/platform translations */
#define DSPPA2GPPPA(base, x, y)  ((x)+(y))
#define GPPPA2DSPPA(base, x, y)  ((x)-(y))

/*
 *  Allocators define a block of contiguous memory used for future allocations.
 *
 *      sma - shared memory allocator.
 *      vma - virtual memory allocator.(not used).
 */
struct cmm_allocator {		/* sma */
	unsigned int shm_base;	/* Start of physical SM block */
	u32 sm_size;		/* Size of SM block in bytes */
	unsigned int vm_base;	/* Start of VM block. (Dev driver
					 * context for 'sma') */
	u32 dsp_phys_addr_offset;	/* DSP PA to GPP PA offset for this
					 * SM space */
	s8 c_factor;		/* DSPPa to GPPPa Conversion Factor */
	unsigned int dsp_base;	/* DSP virt base byte address */
	u32 dsp_size;	/* DSP seg size in bytes */
	struct cmm_object *cmm_mgr;	/* back ref to parent mgr */
	/* node list of available memory */
	struct list_head free_list;
	/* node list of memory in use */
	struct list_head in_use_list;
};

struct cmm_xlator {		/* Pa<->Va translator object */
	/* CMM object this translator associated */
	struct cmm_object *cmm_mgr;
	/*
	 *  Client process virtual base address that corresponds to phys SM
	 *  base address for translator's seg_id.
	 *  Only 1 segment ID currently supported.
	 */
	unsigned int virt_base;	/* virtual base address */
	u32 virt_size;		/* size of virt space in bytes */
	u32 seg_id;		/* Segment Id */
};

/* CMM Mgr */
struct cmm_object {
	/*
	 * Cmm Lock is used to serialize access mem manager for multi-threads.
	 */
	struct mutex cmm_lock;	/* Lock to access cmm mgr */
	struct list_head node_free_list;	/* Free list of memory nodes */
	u32 min_block_size;	/* Min SM block; default 16 bytes */
	u32 page_size;	/* Memory Page size (1k/4k) */
	/* GPP SM segment ptrs */
	struct cmm_allocator *pa_gppsm_seg_tab[CMM_MAXGPPSEGS];
};

/* Default CMM Mgr attributes */
static struct cmm_mgrattrs cmm_dfltmgrattrs = {
	/* min_block_size, min block size(bytes) allocated by cmm mgr */
	16
};

/* Default allocation attributes */
static struct cmm_attrs cmm_dfltalctattrs = {
	1		/* seg_id, default segment Id for allocator */
};

/* Address translator default attrs */
static struct cmm_xlatorattrs cmm_dfltxlatorattrs = {
	/* seg_id, does not have to match cmm_dfltalctattrs ul_seg_id */
	1,
	0,			/* dsp_bufs */
	0,			/* dsp_buf_size */
	NULL,			/* vm_base */
	0,			/* vm_size */
};

/* SM node representing a block of memory. */
struct cmm_mnode {
	struct list_head link;	/* must be 1st element */
	u32 pa;		/* Phys addr */
	u32 va;			/* Virtual address in device process context */
	u32 size;		/* SM block size in bytes */
	u32 client_proc;	/* Process that allocated this mem block */
};

/*  ----------------------------------- Globals */
static u32 refs;		/* module reference count */

/*  ----------------------------------- Function Prototypes */
static void add_to_free_list(struct cmm_allocator *allocator,
			     struct cmm_mnode *pnode);
static struct cmm_allocator *get_allocator(struct cmm_object *cmm_mgr_obj,
					   u32 ul_seg_id);
static struct cmm_mnode *get_free_block(struct cmm_allocator *allocator,
					u32 usize);
static struct cmm_mnode *get_node(struct cmm_object *cmm_mgr_obj, u32 dw_pa,
				  u32 dw_va, u32 ul_size);
/* get available slot for new allocator */
static s32 get_slot(struct cmm_object *cmm_mgr_obj);
static void un_register_gppsm_seg(struct cmm_allocator *psma);

/*
 *  ======== cmm_calloc_buf ========
 *  Purpose:
 *      Allocate a SM buffer, zero contents, and return the physical address
 *      and optional driver context virtual address(pp_buf_va).
 *
 *      The freelist is sorted in increasing size order. Get the first
 *      block that satifies the request and sort the remaining back on
 *      the freelist; if large enough. The kept block is placed on the
 *      inUseList.
 */
void *cmm_calloc_buf(struct cmm_object *hcmm_mgr, u32 usize,
		     struct cmm_attrs *pattrs, void **pp_buf_va)
{
	struct cmm_object *cmm_mgr_obj = (struct cmm_object *)hcmm_mgr;
	void *buf_pa = NULL;
	struct cmm_mnode *pnode = NULL;
	struct cmm_mnode *new_node = NULL;
	struct cmm_allocator *allocator = NULL;
	u32 delta_size;
	u8 *pbyte = NULL;
	s32 cnt;

	if (pattrs == NULL)
		pattrs = &cmm_dfltalctattrs;

	if (pp_buf_va != NULL)
		*pp_buf_va = NULL;

	if (cmm_mgr_obj && (usize != 0)) {
		if (pattrs->seg_id > 0) {
			/* SegId > 0 is SM */
			/* get the allocator object for this segment id */
			allocator =
			    get_allocator(cmm_mgr_obj, pattrs->seg_id);
			/* keep block size a multiple of min_block_size */
			usize =
			    ((usize - 1) & ~(cmm_mgr_obj->min_block_size -
					     1))
			    + cmm_mgr_obj->min_block_size;
			mutex_lock(&cmm_mgr_obj->cmm_lock);
			pnode = get_free_block(allocator, usize);
		}
		if (pnode) {
			delta_size = (pnode->size - usize);
			if (delta_size >= cmm_mgr_obj->min_block_size) {
				/* create a new block with the leftovers and
				 * add to freelist */
				new_node =
				    get_node(cmm_mgr_obj, pnode->pa + usize,
					     pnode->va + usize,
					     (u32) delta_size);
				/* leftovers go free */
				add_to_free_list(allocator, new_node);
				/* adjust our node's size */
				pnode->size = usize;
			}
			/* Tag node with client process requesting allocation
			 * We'll need to free up a process's alloc'd SM if the
			 * client process goes away.
			 */
			/* Return TGID instead of process handle */
			pnode->client_proc = current->tgid;

			/* put our node on InUse list */
			list_add_tail(&pnode->link, &allocator->in_use_list);
			buf_pa = (void *)pnode->pa;	/* physical address */
			/* clear mem */
			pbyte = (u8 *) pnode->va;
			for (cnt = 0; cnt < (s32) usize; cnt++, pbyte++)
				*pbyte = 0;

			if (pp_buf_va != NULL) {
				/* Virtual address */
				*pp_buf_va = (void *)pnode->va;
			}
		}
		mutex_unlock(&cmm_mgr_obj->cmm_lock);
	}
	return buf_pa;
}

/*
 *  ======== cmm_create ========
 *  Purpose:
 *      Create a communication memory manager object.
 */
int cmm_create(struct cmm_object **ph_cmm_mgr,
		      struct dev_object *hdev_obj,
		      const struct cmm_mgrattrs *mgr_attrts)
{
	struct cmm_object *cmm_obj = NULL;
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(ph_cmm_mgr != NULL);

	*ph_cmm_mgr = NULL;
	/* create, zero, and tag a cmm mgr object */
	cmm_obj = kzalloc(sizeof(struct cmm_object), GFP_KERNEL);
	if (!cmm_obj)
		return -ENOMEM;

	if (mgr_attrts == NULL)
		mgr_attrts = &cmm_dfltmgrattrs;	/* set defaults */

	/* 4 bytes minimum */
	DBC_ASSERT(mgr_attrts->min_block_size >= 4);
	/* save away smallest block allocation for this cmm mgr */
	cmm_obj->min_block_size = mgr_attrts->min_block_size;
	cmm_obj->page_size = PAGE_SIZE;

	/* create node free list */
	INIT_LIST_HEAD(&cmm_obj->node_free_list);
	mutex_init(&cmm_obj->cmm_lock);
	*ph_cmm_mgr = cmm_obj;

	return status;
}

/*
 *  ======== cmm_destroy ========
 *  Purpose:
 *      Release the communication memory manager resources.
 */
int cmm_destroy(struct cmm_object *hcmm_mgr, bool force)
{
	struct cmm_object *cmm_mgr_obj = (struct cmm_object *)hcmm_mgr;
	struct cmm_info temp_info;
	int status = 0;
	s32 slot_seg;
	struct cmm_mnode *node, *tmp;

	DBC_REQUIRE(refs > 0);
	if (!hcmm_mgr) {
		status = -EFAULT;
		return status;
	}
	mutex_lock(&cmm_mgr_obj->cmm_lock);
	/* If not force then fail if outstanding allocations exist */
	if (!force) {
		/* Check for outstanding memory allocations */
		status = cmm_get_info(hcmm_mgr, &temp_info);
		if (!status) {
			if (temp_info.total_in_use_cnt > 0) {
				/* outstanding allocations */
				status = -EPERM;
			}
		}
	}
	if (!status) {
		/* UnRegister SM allocator */
		for (slot_seg = 0; slot_seg < CMM_MAXGPPSEGS; slot_seg++) {
			if (cmm_mgr_obj->pa_gppsm_seg_tab[slot_seg] != NULL) {
				un_register_gppsm_seg
				    (cmm_mgr_obj->pa_gppsm_seg_tab[slot_seg]);
				/* Set slot to NULL for future reuse */
				cmm_mgr_obj->pa_gppsm_seg_tab[slot_seg] = NULL;
			}
		}
	}
	list_for_each_entry_safe(node, tmp, &cmm_mgr_obj->node_free_list,
			link) {
		list_del(&node->link);
		kfree(node);
	}
	mutex_unlock(&cmm_mgr_obj->cmm_lock);
	if (!status) {
		/* delete CS & cmm mgr object */
		mutex_destroy(&cmm_mgr_obj->cmm_lock);
		kfree(cmm_mgr_obj);
	}
	return status;
}

/*
 *  ======== cmm_exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 */
void cmm_exit(void)
{
	DBC_REQUIRE(refs > 0);

	refs--;
}

/*
 *  ======== cmm_free_buf ========
 *  Purpose:
 *      Free the given buffer.
 */
int cmm_free_buf(struct cmm_object *hcmm_mgr, void *buf_pa, u32 ul_seg_id)
{
	struct cmm_object *cmm_mgr_obj = (struct cmm_object *)hcmm_mgr;
	int status = -EFAULT;
	struct cmm_mnode *curr, *tmp;
	struct cmm_allocator *allocator;
	struct cmm_attrs *pattrs;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(buf_pa != NULL);

	if (ul_seg_id == 0) {
		pattrs = &cmm_dfltalctattrs;
		ul_seg_id = pattrs->seg_id;
	}
	if (!hcmm_mgr || !(ul_seg_id > 0)) {
		status = -EFAULT;
		return status;
	}

	allocator = get_allocator(cmm_mgr_obj, ul_seg_id);
	if (!allocator)
		return status;

	mutex_lock(&cmm_mgr_obj->cmm_lock);
	list_for_each_entry_safe(curr, tmp, &allocator->in_use_list, link) {
		if (curr->pa == (u32) buf_pa) {
			list_del(&curr->link);
			add_to_free_list(allocator, curr);
			status = 0;
			break;
		}
	}
	mutex_unlock(&cmm_mgr_obj->cmm_lock);

	return status;
}

/*
 *  ======== cmm_get_handle ========
 *  Purpose:
 *      Return the communication memory manager object for this device.
 *      This is typically called from the client process.
 */
int cmm_get_handle(void *hprocessor, struct cmm_object ** ph_cmm_mgr)
{
	int status = 0;
	struct dev_object *hdev_obj;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(ph_cmm_mgr != NULL);
	if (hprocessor != NULL)
		status = proc_get_dev_object(hprocessor, &hdev_obj);
	else
		hdev_obj = dev_get_first();	/* default */

	if (!status)
		status = dev_get_cmm_mgr(hdev_obj, ph_cmm_mgr);

	return status;
}

/*
 *  ======== cmm_get_info ========
 *  Purpose:
 *      Return the current memory utilization information.
 */
int cmm_get_info(struct cmm_object *hcmm_mgr,
			struct cmm_info *cmm_info_obj)
{
	struct cmm_object *cmm_mgr_obj = (struct cmm_object *)hcmm_mgr;
	u32 ul_seg;
	int status = 0;
	struct cmm_allocator *altr;
	struct cmm_mnode *curr;

	DBC_REQUIRE(cmm_info_obj != NULL);

	if (!hcmm_mgr) {
		status = -EFAULT;
		return status;
	}
	mutex_lock(&cmm_mgr_obj->cmm_lock);
	cmm_info_obj->num_gppsm_segs = 0;	/* # of SM segments */
	/* Total # of outstanding alloc */
	cmm_info_obj->total_in_use_cnt = 0;
	/* min block size */
	cmm_info_obj->min_block_size = cmm_mgr_obj->min_block_size;
	/* check SM memory segments */
	for (ul_seg = 1; ul_seg <= CMM_MAXGPPSEGS; ul_seg++) {
		/* get the allocator object for this segment id */
		altr = get_allocator(cmm_mgr_obj, ul_seg);
		if (!altr)
			continue;
		cmm_info_obj->num_gppsm_segs++;
		cmm_info_obj->seg_info[ul_seg - 1].seg_base_pa =
			altr->shm_base - altr->dsp_size;
		cmm_info_obj->seg_info[ul_seg - 1].total_seg_size =
			altr->dsp_size + altr->sm_size;
		cmm_info_obj->seg_info[ul_seg - 1].gpp_base_pa =
			altr->shm_base;
		cmm_info_obj->seg_info[ul_seg - 1].gpp_size =
			altr->sm_size;
		cmm_info_obj->seg_info[ul_seg - 1].dsp_base_va =
			altr->dsp_base;
		cmm_info_obj->seg_info[ul_seg - 1].dsp_size =
			altr->dsp_size;
		cmm_info_obj->seg_info[ul_seg - 1].seg_base_va =
			altr->vm_base - altr->dsp_size;
		cmm_info_obj->seg_info[ul_seg - 1].in_use_cnt = 0;

		list_for_each_entry(curr, &altr->in_use_list, link) {
			cmm_info_obj->total_in_use_cnt++;
			cmm_info_obj->seg_info[ul_seg - 1].in_use_cnt++;
		}
	}
	mutex_unlock(&cmm_mgr_obj->cmm_lock);
	return status;
}

/*
 *  ======== cmm_init ========
 *  Purpose:
 *      Initializes private state of CMM module.
 */
bool cmm_init(void)
{
	bool ret = true;

	DBC_REQUIRE(refs >= 0);
	if (ret)
		refs++;

	DBC_ENSURE((ret && (refs > 0)) || (!ret && (refs >= 0)));

	return ret;
}

/*
 *  ======== cmm_register_gppsm_seg ========
 *  Purpose:
 *      Register a block of SM with the CMM to be used for later GPP SM
 *      allocations.
 */
int cmm_register_gppsm_seg(struct cmm_object *hcmm_mgr,
				  u32 dw_gpp_base_pa, u32 ul_size,
				  u32 dsp_addr_offset, s8 c_factor,
				  u32 dw_dsp_base, u32 ul_dsp_size,
				  u32 *sgmt_id, u32 gpp_base_va)
{
	struct cmm_object *cmm_mgr_obj = (struct cmm_object *)hcmm_mgr;
	struct cmm_allocator *psma = NULL;
	int status = 0;
	struct cmm_mnode *new_node;
	s32 slot_seg;

	DBC_REQUIRE(ul_size > 0);
	DBC_REQUIRE(sgmt_id != NULL);
	DBC_REQUIRE(dw_gpp_base_pa != 0);
	DBC_REQUIRE(gpp_base_va != 0);
	DBC_REQUIRE((c_factor <= CMM_ADDTODSPPA) &&
			(c_factor >= CMM_SUBFROMDSPPA));

	dev_dbg(bridge, "%s: dw_gpp_base_pa %x ul_size %x dsp_addr_offset %x "
			"dw_dsp_base %x ul_dsp_size %x gpp_base_va %x\n",
			__func__, dw_gpp_base_pa, ul_size, dsp_addr_offset,
			dw_dsp_base, ul_dsp_size, gpp_base_va);

	if (!hcmm_mgr)
		return -EFAULT;

	/* make sure we have room for another allocator */
	mutex_lock(&cmm_mgr_obj->cmm_lock);

	slot_seg = get_slot(cmm_mgr_obj);
	if (slot_seg < 0) {
		status = -EPERM;
		goto func_end;
	}

	/* Check if input ul_size is big enough to alloc at least one block */
	if (ul_size < cmm_mgr_obj->min_block_size) {
		status = -EINVAL;
		goto func_end;
	}

	/* create, zero, and tag an SM allocator object */
	psma = kzalloc(sizeof(struct cmm_allocator), GFP_KERNEL);
	if (!psma) {
		status = -ENOMEM;
		goto func_end;
	}

	psma->cmm_mgr = hcmm_mgr;	/* ref to parent */
	psma->shm_base = dw_gpp_base_pa;	/* SM Base phys */
	psma->sm_size = ul_size;	/* SM segment size in bytes */
	psma->vm_base = gpp_base_va;
	psma->dsp_phys_addr_offset = dsp_addr_offset;
	psma->c_factor = c_factor;
	psma->dsp_base = dw_dsp_base;
	psma->dsp_size = ul_dsp_size;
	if (psma->vm_base == 0) {
		status = -EPERM;
		goto func_end;
	}
	/* return the actual segment identifier */
	*sgmt_id = (u32) slot_seg + 1;

	INIT_LIST_HEAD(&psma->free_list);
	INIT_LIST_HEAD(&psma->in_use_list);

	/* Get a mem node for this hunk-o-memory */
	new_node = get_node(cmm_mgr_obj, dw_gpp_base_pa,
			psma->vm_base, ul_size);
	/* Place node on the SM allocator's free list */
	if (new_node) {
		list_add_tail(&new_node->link, &psma->free_list);
	} else {
		status = -ENOMEM;
		goto func_end;
	}
	/* make entry */
	cmm_mgr_obj->pa_gppsm_seg_tab[slot_seg] = psma;

func_end:
	/* Cleanup allocator */
	if (status && psma)
		un_register_gppsm_seg(psma);
	mutex_unlock(&cmm_mgr_obj->cmm_lock);

	return status;
}

/*
 *  ======== cmm_un_register_gppsm_seg ========
 *  Purpose:
 *      UnRegister GPP SM segments with the CMM.
 */
int cmm_un_register_gppsm_seg(struct cmm_object *hcmm_mgr,
				     u32 ul_seg_id)
{
	struct cmm_object *cmm_mgr_obj = (struct cmm_object *)hcmm_mgr;
	int status = 0;
	struct cmm_allocator *psma;
	u32 ul_id = ul_seg_id;

	DBC_REQUIRE(ul_seg_id > 0);
	if (!hcmm_mgr)
		return -EFAULT;

	if (ul_seg_id == CMM_ALLSEGMENTS)
		ul_id = 1;

	if ((ul_id <= 0) || (ul_id > CMM_MAXGPPSEGS))
		return -EINVAL;

	/*
	 * FIXME: CMM_MAXGPPSEGS == 1. why use a while cycle? Seems to me like
	 * the ul_seg_id is not needed here. It must be always 1.
	 */
	while (ul_id <= CMM_MAXGPPSEGS) {
		mutex_lock(&cmm_mgr_obj->cmm_lock);
		/* slot = seg_id-1 */
		psma = cmm_mgr_obj->pa_gppsm_seg_tab[ul_id - 1];
		if (psma != NULL) {
			un_register_gppsm_seg(psma);
			/* Set alctr ptr to NULL for future reuse */
			cmm_mgr_obj->pa_gppsm_seg_tab[ul_id - 1] = NULL;
		} else if (ul_seg_id != CMM_ALLSEGMENTS) {
			status = -EPERM;
		}
		mutex_unlock(&cmm_mgr_obj->cmm_lock);
		if (ul_seg_id != CMM_ALLSEGMENTS)
			break;

		ul_id++;
	}	/* end while */
	return status;
}

/*
 *  ======== un_register_gppsm_seg ========
 *  Purpose:
 *      UnRegister the SM allocator by freeing all its resources and
 *      nulling cmm mgr table entry.
 *  Note:
 *      This routine is always called within cmm lock crit sect.
 */
static void un_register_gppsm_seg(struct cmm_allocator *psma)
{
	struct cmm_mnode *curr, *tmp;

	DBC_REQUIRE(psma != NULL);

	/* free nodes on free list */
	list_for_each_entry_safe(curr, tmp, &psma->free_list, link) {
		list_del(&curr->link);
		kfree(curr);
	}

	/* free nodes on InUse list */
	list_for_each_entry_safe(curr, tmp, &psma->in_use_list, link) {
		list_del(&curr->link);
		kfree(curr);
	}

	if ((void *)psma->vm_base != NULL)
		MEM_UNMAP_LINEAR_ADDRESS((void *)psma->vm_base);

	/* Free allocator itself */
	kfree(psma);
}

/*
 *  ======== get_slot ========
 *  Purpose:
 *      An available slot # is returned. Returns negative on failure.
 */
static s32 get_slot(struct cmm_object *cmm_mgr_obj)
{
	s32 slot_seg = -1;	/* neg on failure */
	DBC_REQUIRE(cmm_mgr_obj != NULL);
	/* get first available slot in cmm mgr SMSegTab[] */
	for (slot_seg = 0; slot_seg < CMM_MAXGPPSEGS; slot_seg++) {
		if (cmm_mgr_obj->pa_gppsm_seg_tab[slot_seg] == NULL)
			break;

	}
	if (slot_seg == CMM_MAXGPPSEGS)
		slot_seg = -1;	/* failed */

	return slot_seg;
}

/*
 *  ======== get_node ========
 *  Purpose:
 *      Get a memory node from freelist or create a new one.
 */
static struct cmm_mnode *get_node(struct cmm_object *cmm_mgr_obj, u32 dw_pa,
				  u32 dw_va, u32 ul_size)
{
	struct cmm_mnode *pnode;

	DBC_REQUIRE(cmm_mgr_obj != NULL);
	DBC_REQUIRE(dw_pa != 0);
	DBC_REQUIRE(dw_va != 0);
	DBC_REQUIRE(ul_size != 0);

	/* Check cmm mgr's node freelist */
	if (list_empty(&cmm_mgr_obj->node_free_list)) {
		pnode = kzalloc(sizeof(struct cmm_mnode), GFP_KERNEL);
		if (!pnode)
			return NULL;
	} else {
		/* surely a valid element */
		pnode = list_first_entry(&cmm_mgr_obj->node_free_list,
				struct cmm_mnode, link);
		list_del_init(&pnode->link);
	}

	pnode->pa = dw_pa;
	pnode->va = dw_va;
	pnode->size = ul_size;

	return pnode;
}

/*
 *  ======== delete_node ========
 *  Purpose:
 *      Put a memory node on the cmm nodelist for later use.
 *      Doesn't actually delete the node. Heap thrashing friendly.
 */
static void delete_node(struct cmm_object *cmm_mgr_obj, struct cmm_mnode *pnode)
{
	DBC_REQUIRE(pnode != NULL);
	list_add_tail(&pnode->link, &cmm_mgr_obj->node_free_list);
}

/*
 * ====== get_free_block ========
 *  Purpose:
 *      Scan the free block list and return the first block that satisfies
 *      the size.
 */
static struct cmm_mnode *get_free_block(struct cmm_allocator *allocator,
					u32 usize)
{
	struct cmm_mnode *node, *tmp;

	if (!allocator)
		return NULL;

	list_for_each_entry_safe(node, tmp, &allocator->free_list, link) {
		if (usize <= node->size) {
			list_del(&node->link);
			return node;
		}
	}

	return NULL;
}

/*
 *  ======== add_to_free_list ========
 *  Purpose:
 *      Coalesce node into the freelist in ascending size order.
 */
static void add_to_free_list(struct cmm_allocator *allocator,
			     struct cmm_mnode *node)
{
	struct cmm_mnode *curr;

	if (!node) {
		pr_err("%s: failed - node is NULL\n", __func__);
		return;
	}

	list_for_each_entry(curr, &allocator->free_list, link) {
		if (NEXT_PA(curr) == node->pa) {
			curr->size += node->size;
			delete_node(allocator->cmm_mgr, node);
			return;
		}
		if (curr->pa == NEXT_PA(node)) {
			curr->pa = node->pa;
			curr->va = node->va;
			curr->size += node->size;
			delete_node(allocator->cmm_mgr, node);
			return;
		}
	}
	list_for_each_entry(curr, &allocator->free_list, link) {
		if (curr->size >= node->size) {
			list_add_tail(&node->link, &curr->link);
			return;
		}
	}
	list_add_tail(&node->link, &allocator->free_list);
}

/*
 * ======== get_allocator ========
 *  Purpose:
 *      Return the allocator for the given SM Segid.
 *      SegIds:  1,2,3..max.
 */
static struct cmm_allocator *get_allocator(struct cmm_object *cmm_mgr_obj,
					   u32 ul_seg_id)
{
	DBC_REQUIRE(cmm_mgr_obj != NULL);
	DBC_REQUIRE((ul_seg_id > 0) && (ul_seg_id <= CMM_MAXGPPSEGS));

	return cmm_mgr_obj->pa_gppsm_seg_tab[ul_seg_id - 1];
}

/*
 *  The CMM_Xlator[xxx] routines below are used by Node and Stream
 *  to perform SM address translation to the client process address space.
 *  A "translator" object is created by a node/stream for each SM seg used.
 */

/*
 *  ======== cmm_xlator_create ========
 *  Purpose:
 *      Create an address translator object.
 */
int cmm_xlator_create(struct cmm_xlatorobject **xlator,
			     struct cmm_object *hcmm_mgr,
			     struct cmm_xlatorattrs *xlator_attrs)
{
	struct cmm_xlator *xlator_object = NULL;
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(xlator != NULL);
	DBC_REQUIRE(hcmm_mgr != NULL);

	*xlator = NULL;
	if (xlator_attrs == NULL)
		xlator_attrs = &cmm_dfltxlatorattrs;	/* set defaults */

	xlator_object = kzalloc(sizeof(struct cmm_xlator), GFP_KERNEL);
	if (xlator_object != NULL) {
		xlator_object->cmm_mgr = hcmm_mgr;	/* ref back to CMM */
		/* SM seg_id */
		xlator_object->seg_id = xlator_attrs->seg_id;
	} else {
		status = -ENOMEM;
	}
	if (!status)
		*xlator = (struct cmm_xlatorobject *)xlator_object;

	return status;
}

/*
 *  ======== cmm_xlator_alloc_buf ========
 */
void *cmm_xlator_alloc_buf(struct cmm_xlatorobject *xlator, void *va_buf,
			   u32 pa_size)
{
	struct cmm_xlator *xlator_obj = (struct cmm_xlator *)xlator;
	void *pbuf = NULL;
	void *tmp_va_buff;
	struct cmm_attrs attrs;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(xlator != NULL);
	DBC_REQUIRE(xlator_obj->cmm_mgr != NULL);
	DBC_REQUIRE(va_buf != NULL);
	DBC_REQUIRE(pa_size > 0);
	DBC_REQUIRE(xlator_obj->seg_id > 0);

	if (xlator_obj) {
		attrs.seg_id = xlator_obj->seg_id;
		__raw_writel(0, va_buf);
		/* Alloc SM */
		pbuf =
		    cmm_calloc_buf(xlator_obj->cmm_mgr, pa_size, &attrs, NULL);
		if (pbuf) {
			/* convert to translator(node/strm) process Virtual
			 * address */
			 tmp_va_buff = cmm_xlator_translate(xlator,
							 pbuf, CMM_PA2VA);
			__raw_writel((u32)tmp_va_buff, va_buf);
		}
	}
	return pbuf;
}

/*
 *  ======== cmm_xlator_free_buf ========
 *  Purpose:
 *      Free the given SM buffer and descriptor.
 *      Does not free virtual memory.
 */
int cmm_xlator_free_buf(struct cmm_xlatorobject *xlator, void *buf_va)
{
	struct cmm_xlator *xlator_obj = (struct cmm_xlator *)xlator;
	int status = -EPERM;
	void *buf_pa = NULL;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(buf_va != NULL);
	DBC_REQUIRE(xlator_obj->seg_id > 0);

	if (xlator_obj) {
		/* convert Va to Pa so we can free it. */
		buf_pa = cmm_xlator_translate(xlator, buf_va, CMM_VA2PA);
		if (buf_pa) {
			status = cmm_free_buf(xlator_obj->cmm_mgr, buf_pa,
					      xlator_obj->seg_id);
			if (status) {
				/* Uh oh, this shouldn't happen. Descriptor
				 * gone! */
				DBC_ASSERT(false);	/* CMM is leaking mem */
			}
		}
	}
	return status;
}

/*
 *  ======== cmm_xlator_info ========
 *  Purpose:
 *      Set/Get translator info.
 */
int cmm_xlator_info(struct cmm_xlatorobject *xlator, u8 ** paddr,
			   u32 ul_size, u32 segm_id, bool set_info)
{
	struct cmm_xlator *xlator_obj = (struct cmm_xlator *)xlator;
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(paddr != NULL);
	DBC_REQUIRE((segm_id > 0) && (segm_id <= CMM_MAXGPPSEGS));

	if (xlator_obj) {
		if (set_info) {
			/* set translators virtual address range */
			xlator_obj->virt_base = (u32) *paddr;
			xlator_obj->virt_size = ul_size;
		} else {	/* return virt base address */
			*paddr = (u8 *) xlator_obj->virt_base;
		}
	} else {
		status = -EFAULT;
	}
	return status;
}

/*
 *  ======== cmm_xlator_translate ========
 */
void *cmm_xlator_translate(struct cmm_xlatorobject *xlator, void *paddr,
			   enum cmm_xlatetype xtype)
{
	u32 dw_addr_xlate = 0;
	struct cmm_xlator *xlator_obj = (struct cmm_xlator *)xlator;
	struct cmm_object *cmm_mgr_obj = NULL;
	struct cmm_allocator *allocator = NULL;
	u32 dw_offset = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(paddr != NULL);
	DBC_REQUIRE((xtype >= CMM_VA2PA) && (xtype <= CMM_DSPPA2PA));

	if (!xlator_obj)
		goto loop_cont;

	cmm_mgr_obj = (struct cmm_object *)xlator_obj->cmm_mgr;
	/* get this translator's default SM allocator */
	DBC_ASSERT(xlator_obj->seg_id > 0);
	allocator = cmm_mgr_obj->pa_gppsm_seg_tab[xlator_obj->seg_id - 1];
	if (!allocator)
		goto loop_cont;

	if ((xtype == CMM_VA2DSPPA) || (xtype == CMM_VA2PA) ||
	    (xtype == CMM_PA2VA)) {
		if (xtype == CMM_PA2VA) {
			/* Gpp Va = Va Base + offset */
			dw_offset = (u8 *) paddr - (u8 *) (allocator->shm_base -
							   allocator->
							   dsp_size);
			dw_addr_xlate = xlator_obj->virt_base + dw_offset;
			/* Check if translated Va base is in range */
			if ((dw_addr_xlate < xlator_obj->virt_base) ||
			    (dw_addr_xlate >=
			     (xlator_obj->virt_base +
			      xlator_obj->virt_size))) {
				dw_addr_xlate = 0;	/* bad address */
			}
		} else {
			/* Gpp PA =  Gpp Base + offset */
			dw_offset =
			    (u8 *) paddr - (u8 *) xlator_obj->virt_base;
			dw_addr_xlate =
			    allocator->shm_base - allocator->dsp_size +
			    dw_offset;
		}
	} else {
		dw_addr_xlate = (u32) paddr;
	}
	/*Now convert address to proper target physical address if needed */
	if ((xtype == CMM_VA2DSPPA) || (xtype == CMM_PA2DSPPA)) {
		/* Got Gpp Pa now, convert to DSP Pa */
		dw_addr_xlate =
		    GPPPA2DSPPA((allocator->shm_base - allocator->dsp_size),
				dw_addr_xlate,
				allocator->dsp_phys_addr_offset *
				allocator->c_factor);
	} else if (xtype == CMM_DSPPA2PA) {
		/* Got DSP Pa, convert to GPP Pa */
		dw_addr_xlate =
		    DSPPA2GPPPA(allocator->shm_base - allocator->dsp_size,
				dw_addr_xlate,
				allocator->dsp_phys_addr_offset *
				allocator->c_factor);
	}
loop_cont:
	return (void *)dw_addr_xlate;
}
