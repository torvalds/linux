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
 * Memory is coelesced back to the appropriate heap when a buffer is
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

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/cfg.h>
#include <dspbridge/list.h>
#include <dspbridge/sync.h>
#include <dspbridge/utildefs.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/proc.h>

/*  ----------------------------------- This */
#include <dspbridge/cmm.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define NEXT_PA(pnode)   (pnode->dw_pa + pnode->ul_size)

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
	u32 ul_sm_size;		/* Size of SM block in bytes */
	unsigned int dw_vm_base;	/* Start of VM block. (Dev driver
					 * context for 'sma') */
	u32 dw_dsp_phys_addr_offset;	/* DSP PA to GPP PA offset for this
					 * SM space */
	s8 c_factor;		/* DSPPa to GPPPa Conversion Factor */
	unsigned int dw_dsp_base;	/* DSP virt base byte address */
	u32 ul_dsp_size;	/* DSP seg size in bytes */
	struct cmm_object *hcmm_mgr;	/* back ref to parent mgr */
	/* node list of available memory */
	struct lst_list *free_list_head;
	/* node list of memory in use */
	struct lst_list *in_use_list_head;
};

struct cmm_xlator {		/* Pa<->Va translator object */
	/* CMM object this translator associated */
	struct cmm_object *hcmm_mgr;
	/*
	 *  Client process virtual base address that corresponds to phys SM
	 *  base address for translator's ul_seg_id.
	 *  Only 1 segment ID currently supported.
	 */
	unsigned int dw_virt_base;	/* virtual base address */
	u32 ul_virt_size;	/* size of virt space in bytes */
	u32 ul_seg_id;		/* Segment Id */
};

/* CMM Mgr */
struct cmm_object {
	/*
	 * Cmm Lock is used to serialize access mem manager for multi-threads.
	 */
	struct mutex cmm_lock;	/* Lock to access cmm mgr */
	struct lst_list *node_free_list_head;	/* Free list of memory nodes */
	u32 ul_min_block_size;	/* Min SM block; default 16 bytes */
	u32 dw_page_size;	/* Memory Page size (1k/4k) */
	/* GPP SM segment ptrs */
	struct cmm_allocator *pa_gppsm_seg_tab[CMM_MAXGPPSEGS];
};

/* Default CMM Mgr attributes */
static struct cmm_mgrattrs cmm_dfltmgrattrs = {
	/* ul_min_block_size, min block size(bytes) allocated by cmm mgr */
	16
};

/* Default allocation attributes */
static struct cmm_attrs cmm_dfltalctattrs = {
	1		/* ul_seg_id, default segment Id for allocator */
};

/* Address translator default attrs */
static struct cmm_xlatorattrs cmm_dfltxlatorattrs = {
	/* ul_seg_id, does not have to match cmm_dfltalctattrs ul_seg_id */
	1,
	0,			/* dw_dsp_bufs */
	0,			/* dw_dsp_buf_size */
	NULL,			/* vm_base */
	0,			/* dw_vm_size */
};

/* SM node representing a block of memory. */
struct cmm_mnode {
	struct list_head link;	/* must be 1st element */
	u32 dw_pa;		/* Phys addr */
	u32 dw_va;		/* Virtual address in device process context */
	u32 ul_size;		/* SM block size in bytes */
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
		if (pattrs->ul_seg_id > 0) {
			/* SegId > 0 is SM */
			/* get the allocator object for this segment id */
			allocator =
			    get_allocator(cmm_mgr_obj, pattrs->ul_seg_id);
			/* keep block size a multiple of ul_min_block_size */
			usize =
			    ((usize - 1) & ~(cmm_mgr_obj->ul_min_block_size -
					     1))
			    + cmm_mgr_obj->ul_min_block_size;
			mutex_lock(&cmm_mgr_obj->cmm_lock);
			pnode = get_free_block(allocator, usize);
		}
		if (pnode) {
			delta_size = (pnode->ul_size - usize);
			if (delta_size >= cmm_mgr_obj->ul_min_block_size) {
				/* create a new block with the leftovers and
				 * add to freelist */
				new_node =
				    get_node(cmm_mgr_obj, pnode->dw_pa + usize,
					     pnode->dw_va + usize,
					     (u32) delta_size);
				/* leftovers go free */
				add_to_free_list(allocator, new_node);
				/* adjust our node's size */
				pnode->ul_size = usize;
			}
			/* Tag node with client process requesting allocation
			 * We'll need to free up a process's alloc'd SM if the
			 * client process goes away.
			 */
			/* Return TGID instead of process handle */
			pnode->client_proc = current->tgid;

			/* put our node on InUse list */
			lst_put_tail(allocator->in_use_list_head,
				     (struct list_head *)pnode);
			buf_pa = (void *)pnode->dw_pa;	/* physical address */
			/* clear mem */
			pbyte = (u8 *) pnode->dw_va;
			for (cnt = 0; cnt < (s32) usize; cnt++, pbyte++)
				*pbyte = 0;

			if (pp_buf_va != NULL) {
				/* Virtual address */
				*pp_buf_va = (void *)pnode->dw_va;
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
	struct util_sysinfo sys_info;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(ph_cmm_mgr != NULL);

	*ph_cmm_mgr = NULL;
	/* create, zero, and tag a cmm mgr object */
	cmm_obj = kzalloc(sizeof(struct cmm_object), GFP_KERNEL);
	if (cmm_obj != NULL) {
		if (mgr_attrts == NULL)
			mgr_attrts = &cmm_dfltmgrattrs;	/* set defaults */

		/* 4 bytes minimum */
		DBC_ASSERT(mgr_attrts->ul_min_block_size >= 4);
		/* save away smallest block allocation for this cmm mgr */
		cmm_obj->ul_min_block_size = mgr_attrts->ul_min_block_size;
		/* save away the systems memory page size */
		sys_info.dw_page_size = PAGE_SIZE;
		sys_info.dw_allocation_granularity = PAGE_SIZE;
		sys_info.dw_number_of_processors = 1;

		cmm_obj->dw_page_size = sys_info.dw_page_size;

		/* Note: DSP SM seg table(aDSPSMSegTab[]) zero'd by
		 * MEM_ALLOC_OBJECT */

		/* create node free list */
		cmm_obj->node_free_list_head =
				kzalloc(sizeof(struct lst_list),
						GFP_KERNEL);
		if (cmm_obj->node_free_list_head == NULL) {
			status = -ENOMEM;
			cmm_destroy(cmm_obj, true);
		} else {
			INIT_LIST_HEAD(&cmm_obj->
				       node_free_list_head->head);
			mutex_init(&cmm_obj->cmm_lock);
			*ph_cmm_mgr = cmm_obj;
		}
	} else {
		status = -ENOMEM;
	}
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
	struct cmm_mnode *pnode;

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
			if (temp_info.ul_total_in_use_cnt > 0) {
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
	if (cmm_mgr_obj->node_free_list_head != NULL) {
		/* Free the free nodes */
		while (!LST_IS_EMPTY(cmm_mgr_obj->node_free_list_head)) {
			pnode = (struct cmm_mnode *)
			    lst_get_head(cmm_mgr_obj->node_free_list_head);
			kfree(pnode);
		}
		/* delete NodeFreeList list */
		kfree(cmm_mgr_obj->node_free_list_head);
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
int cmm_free_buf(struct cmm_object *hcmm_mgr, void *buf_pa,
			u32 ul_seg_id)
{
	struct cmm_object *cmm_mgr_obj = (struct cmm_object *)hcmm_mgr;
	int status = -EFAULT;
	struct cmm_mnode *mnode_obj = NULL;
	struct cmm_allocator *allocator = NULL;
	struct cmm_attrs *pattrs;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(buf_pa != NULL);

	if (ul_seg_id == 0) {
		pattrs = &cmm_dfltalctattrs;
		ul_seg_id = pattrs->ul_seg_id;
	}
	if (!hcmm_mgr || !(ul_seg_id > 0)) {
		status = -EFAULT;
		return status;
	}
	/* get the allocator for this segment id */
	allocator = get_allocator(cmm_mgr_obj, ul_seg_id);
	if (allocator != NULL) {
		mutex_lock(&cmm_mgr_obj->cmm_lock);
		mnode_obj =
		    (struct cmm_mnode *)lst_first(allocator->in_use_list_head);
		while (mnode_obj) {
			if ((u32) buf_pa == mnode_obj->dw_pa) {
				/* Found it */
				lst_remove_elem(allocator->in_use_list_head,
						(struct list_head *)mnode_obj);
				/* back to freelist */
				add_to_free_list(allocator, mnode_obj);
				status = 0;	/* all right! */
				break;
			}
			/* next node. */
			mnode_obj = (struct cmm_mnode *)
			    lst_next(allocator->in_use_list_head,
				     (struct list_head *)mnode_obj);
		}
		mutex_unlock(&cmm_mgr_obj->cmm_lock);
	}
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
	struct cmm_mnode *mnode_obj = NULL;

	DBC_REQUIRE(cmm_info_obj != NULL);

	if (!hcmm_mgr) {
		status = -EFAULT;
		return status;
	}
	mutex_lock(&cmm_mgr_obj->cmm_lock);
	cmm_info_obj->ul_num_gppsm_segs = 0;	/* # of SM segments */
	/* Total # of outstanding alloc */
	cmm_info_obj->ul_total_in_use_cnt = 0;
	/* min block size */
	cmm_info_obj->ul_min_block_size = cmm_mgr_obj->ul_min_block_size;
	/* check SM memory segments */
	for (ul_seg = 1; ul_seg <= CMM_MAXGPPSEGS; ul_seg++) {
		/* get the allocator object for this segment id */
		altr = get_allocator(cmm_mgr_obj, ul_seg);
		if (altr != NULL) {
			cmm_info_obj->ul_num_gppsm_segs++;
			cmm_info_obj->seg_info[ul_seg - 1].dw_seg_base_pa =
			    altr->shm_base - altr->ul_dsp_size;
			cmm_info_obj->seg_info[ul_seg - 1].ul_total_seg_size =
			    altr->ul_dsp_size + altr->ul_sm_size;
			cmm_info_obj->seg_info[ul_seg - 1].dw_gpp_base_pa =
			    altr->shm_base;
			cmm_info_obj->seg_info[ul_seg - 1].ul_gpp_size =
			    altr->ul_sm_size;
			cmm_info_obj->seg_info[ul_seg - 1].dw_dsp_base_va =
			    altr->dw_dsp_base;
			cmm_info_obj->seg_info[ul_seg - 1].ul_dsp_size =
			    altr->ul_dsp_size;
			cmm_info_obj->seg_info[ul_seg - 1].dw_seg_base_va =
			    altr->dw_vm_base - altr->ul_dsp_size;
			cmm_info_obj->seg_info[ul_seg - 1].ul_in_use_cnt = 0;
			mnode_obj = (struct cmm_mnode *)
			    lst_first(altr->in_use_list_head);
			/* Count inUse blocks */
			while (mnode_obj) {
				cmm_info_obj->ul_total_in_use_cnt++;
				cmm_info_obj->seg_info[ul_seg -
						       1].ul_in_use_cnt++;
				/* next node. */
				mnode_obj = (struct cmm_mnode *)
				    lst_next(altr->in_use_list_head,
					     (struct list_head *)mnode_obj);
			}
		}
	}			/* end for */
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
		"dw_dsp_base %x ul_dsp_size %x gpp_base_va %x\n", __func__,
		dw_gpp_base_pa, ul_size, dsp_addr_offset, dw_dsp_base,
		ul_dsp_size, gpp_base_va);
	if (!hcmm_mgr) {
		status = -EFAULT;
		return status;
	}
	/* make sure we have room for another allocator */
	mutex_lock(&cmm_mgr_obj->cmm_lock);
	slot_seg = get_slot(cmm_mgr_obj);
	if (slot_seg < 0) {
		/* get a slot number */
		status = -EPERM;
		goto func_end;
	}
	/* Check if input ul_size is big enough to alloc at least one block */
	if (ul_size < cmm_mgr_obj->ul_min_block_size) {
		status = -EINVAL;
		goto func_end;
	}

	/* create, zero, and tag an SM allocator object */
	psma = kzalloc(sizeof(struct cmm_allocator), GFP_KERNEL);
	if (psma != NULL) {
		psma->hcmm_mgr = hcmm_mgr;	/* ref to parent */
		psma->shm_base = dw_gpp_base_pa;	/* SM Base phys */
		psma->ul_sm_size = ul_size;	/* SM segment size in bytes */
		psma->dw_vm_base = gpp_base_va;
		psma->dw_dsp_phys_addr_offset = dsp_addr_offset;
		psma->c_factor = c_factor;
		psma->dw_dsp_base = dw_dsp_base;
		psma->ul_dsp_size = ul_dsp_size;
		if (psma->dw_vm_base == 0) {
			status = -EPERM;
			goto func_end;
		}
		/* return the actual segment identifier */
		*sgmt_id = (u32) slot_seg + 1;
		/* create memory free list */
		psma->free_list_head = kzalloc(sizeof(struct lst_list),
							GFP_KERNEL);
		if (psma->free_list_head == NULL) {
			status = -ENOMEM;
			goto func_end;
		}
		INIT_LIST_HEAD(&psma->free_list_head->head);

		/* create memory in-use list */
		psma->in_use_list_head = kzalloc(sizeof(struct
						lst_list), GFP_KERNEL);
		if (psma->in_use_list_head == NULL) {
			status = -ENOMEM;
			goto func_end;
		}
		INIT_LIST_HEAD(&psma->in_use_list_head->head);

		/* Get a mem node for this hunk-o-memory */
		new_node = get_node(cmm_mgr_obj, dw_gpp_base_pa,
				    psma->dw_vm_base, ul_size);
		/* Place node on the SM allocator's free list */
		if (new_node) {
			lst_put_tail(psma->free_list_head,
				     (struct list_head *)new_node);
		} else {
			status = -ENOMEM;
			goto func_end;
		}
	} else {
		status = -ENOMEM;
		goto func_end;
	}
	/* make entry */
	cmm_mgr_obj->pa_gppsm_seg_tab[slot_seg] = psma;

func_end:
	if (status && psma) {
		/* Cleanup allocator */
		un_register_gppsm_seg(psma);
	}

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
	if (hcmm_mgr) {
		if (ul_seg_id == CMM_ALLSEGMENTS)
			ul_id = 1;

		if ((ul_id > 0) && (ul_id <= CMM_MAXGPPSEGS)) {
			while (ul_id <= CMM_MAXGPPSEGS) {
				mutex_lock(&cmm_mgr_obj->cmm_lock);
				/* slot = seg_id-1 */
				psma = cmm_mgr_obj->pa_gppsm_seg_tab[ul_id - 1];
				if (psma != NULL) {
					un_register_gppsm_seg(psma);
					/* Set alctr ptr to NULL for future
					 * reuse */
					cmm_mgr_obj->pa_gppsm_seg_tab[ul_id -
								      1] = NULL;
				} else if (ul_seg_id != CMM_ALLSEGMENTS) {
					status = -EPERM;
				}
				mutex_unlock(&cmm_mgr_obj->cmm_lock);
				if (ul_seg_id != CMM_ALLSEGMENTS)
					break;

				ul_id++;
			}	/* end while */
		} else {
			status = -EINVAL;
		}
	} else {
		status = -EFAULT;
	}
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
	struct cmm_mnode *mnode_obj = NULL;
	struct cmm_mnode *next_node = NULL;

	DBC_REQUIRE(psma != NULL);
	if (psma->free_list_head != NULL) {
		/* free nodes on free list */
		mnode_obj = (struct cmm_mnode *)lst_first(psma->free_list_head);
		while (mnode_obj) {
			next_node =
			    (struct cmm_mnode *)lst_next(psma->free_list_head,
							 (struct list_head *)
							 mnode_obj);
			lst_remove_elem(psma->free_list_head,
					(struct list_head *)mnode_obj);
			kfree((void *)mnode_obj);
			/* next node. */
			mnode_obj = next_node;
		}
		kfree(psma->free_list_head);	/* delete freelist */
		/* free nodes on InUse list */
		mnode_obj =
		    (struct cmm_mnode *)lst_first(psma->in_use_list_head);
		while (mnode_obj) {
			next_node =
			    (struct cmm_mnode *)lst_next(psma->in_use_list_head,
							 (struct list_head *)
							 mnode_obj);
			lst_remove_elem(psma->in_use_list_head,
					(struct list_head *)mnode_obj);
			kfree((void *)mnode_obj);
			/* next node. */
			mnode_obj = next_node;
		}
		kfree(psma->in_use_list_head);	/* delete InUse list */
	}
	if ((void *)psma->dw_vm_base != NULL)
		MEM_UNMAP_LINEAR_ADDRESS((void *)psma->dw_vm_base);

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
	struct cmm_mnode *pnode = NULL;

	DBC_REQUIRE(cmm_mgr_obj != NULL);
	DBC_REQUIRE(dw_pa != 0);
	DBC_REQUIRE(dw_va != 0);
	DBC_REQUIRE(ul_size != 0);
	/* Check cmm mgr's node freelist */
	if (LST_IS_EMPTY(cmm_mgr_obj->node_free_list_head)) {
		pnode = kzalloc(sizeof(struct cmm_mnode), GFP_KERNEL);
	} else {
		/* surely a valid element */
		pnode = (struct cmm_mnode *)
		    lst_get_head(cmm_mgr_obj->node_free_list_head);
	}
	if (pnode) {
		lst_init_elem((struct list_head *)pnode);	/* set self */
		pnode->dw_pa = dw_pa;	/* Physical addr of start of block */
		pnode->dw_va = dw_va;	/* Virtual   "            " */
		pnode->ul_size = ul_size;	/* Size of block */
	}
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
	lst_init_elem((struct list_head *)pnode);	/* init .self ptr */
	lst_put_tail(cmm_mgr_obj->node_free_list_head,
		     (struct list_head *)pnode);
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
	if (allocator) {
		struct cmm_mnode *mnode_obj = (struct cmm_mnode *)
		    lst_first(allocator->free_list_head);
		while (mnode_obj) {
			if (usize <= (u32) mnode_obj->ul_size) {
				lst_remove_elem(allocator->free_list_head,
						(struct list_head *)mnode_obj);
				return mnode_obj;
			}
			/* next node. */
			mnode_obj = (struct cmm_mnode *)
			    lst_next(allocator->free_list_head,
				     (struct list_head *)mnode_obj);
		}
	}
	return NULL;
}

/*
 *  ======== add_to_free_list ========
 *  Purpose:
 *      Coelesce node into the freelist in ascending size order.
 */
static void add_to_free_list(struct cmm_allocator *allocator,
			     struct cmm_mnode *pnode)
{
	struct cmm_mnode *node_prev = NULL;
	struct cmm_mnode *node_next = NULL;
	struct cmm_mnode *mnode_obj;
	u32 dw_this_pa;
	u32 dw_next_pa;

	DBC_REQUIRE(pnode != NULL);
	DBC_REQUIRE(allocator != NULL);
	dw_this_pa = pnode->dw_pa;
	dw_next_pa = NEXT_PA(pnode);
	mnode_obj = (struct cmm_mnode *)lst_first(allocator->free_list_head);
	while (mnode_obj) {
		if (dw_this_pa == NEXT_PA(mnode_obj)) {
			/* found the block ahead of this one */
			node_prev = mnode_obj;
		} else if (dw_next_pa == mnode_obj->dw_pa) {
			node_next = mnode_obj;
		}
		if ((node_prev == NULL) || (node_next == NULL)) {
			/* next node. */
			mnode_obj = (struct cmm_mnode *)
			    lst_next(allocator->free_list_head,
				     (struct list_head *)mnode_obj);
		} else {
			/* got 'em */
			break;
		}
	}			/* while */
	if (node_prev != NULL) {
		/* combine with previous block */
		lst_remove_elem(allocator->free_list_head,
				(struct list_head *)node_prev);
		/* grow node to hold both */
		pnode->ul_size += node_prev->ul_size;
		pnode->dw_pa = node_prev->dw_pa;
		pnode->dw_va = node_prev->dw_va;
		/* place node on mgr nodeFreeList */
		delete_node((struct cmm_object *)allocator->hcmm_mgr,
			    node_prev);
	}
	if (node_next != NULL) {
		/* combine with next block */
		lst_remove_elem(allocator->free_list_head,
				(struct list_head *)node_next);
		/* grow da node */
		pnode->ul_size += node_next->ul_size;
		/* place node on mgr nodeFreeList */
		delete_node((struct cmm_object *)allocator->hcmm_mgr,
			    node_next);
	}
	/* Now, let's add to freelist in increasing size order */
	mnode_obj = (struct cmm_mnode *)lst_first(allocator->free_list_head);
	while (mnode_obj) {
		if (pnode->ul_size <= mnode_obj->ul_size)
			break;

		/* next node. */
		mnode_obj =
		    (struct cmm_mnode *)lst_next(allocator->free_list_head,
						 (struct list_head *)mnode_obj);
	}
	/* if mnode_obj is NULL then add our pnode to the end of the freelist */
	if (mnode_obj == NULL) {
		lst_put_tail(allocator->free_list_head,
			     (struct list_head *)pnode);
	} else {
		/* insert our node before the current traversed node */
		lst_insert_before(allocator->free_list_head,
				  (struct list_head *)pnode,
				  (struct list_head *)mnode_obj);
	}
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
	struct cmm_allocator *allocator = NULL;

	DBC_REQUIRE(cmm_mgr_obj != NULL);
	DBC_REQUIRE((ul_seg_id > 0) && (ul_seg_id <= CMM_MAXGPPSEGS));
	allocator = cmm_mgr_obj->pa_gppsm_seg_tab[ul_seg_id - 1];
	if (allocator != NULL) {
		/* make sure it's for real */
		if (!allocator) {
			allocator = NULL;
			DBC_ASSERT(false);
		}
	}
	return allocator;
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
		xlator_object->hcmm_mgr = hcmm_mgr;	/* ref back to CMM */
		/* SM seg_id */
		xlator_object->ul_seg_id = xlator_attrs->ul_seg_id;
	} else {
		status = -ENOMEM;
	}
	if (!status)
		*xlator = (struct cmm_xlatorobject *)xlator_object;

	return status;
}

/*
 *  ======== cmm_xlator_delete ========
 *  Purpose:
 *      Free the Xlator resources.
 *      VM gets freed later.
 */
int cmm_xlator_delete(struct cmm_xlatorobject *xlator, bool force)
{
	struct cmm_xlator *xlator_obj = (struct cmm_xlator *)xlator;

	DBC_REQUIRE(refs > 0);

	kfree(xlator_obj);

	return 0;
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
	DBC_REQUIRE(xlator_obj->hcmm_mgr != NULL);
	DBC_REQUIRE(va_buf != NULL);
	DBC_REQUIRE(pa_size > 0);
	DBC_REQUIRE(xlator_obj->ul_seg_id > 0);

	if (xlator_obj) {
		attrs.ul_seg_id = xlator_obj->ul_seg_id;
		__raw_writel(0, va_buf);
		/* Alloc SM */
		pbuf =
		    cmm_calloc_buf(xlator_obj->hcmm_mgr, pa_size, &attrs, NULL);
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
	DBC_REQUIRE(xlator_obj->ul_seg_id > 0);

	if (xlator_obj) {
		/* convert Va to Pa so we can free it. */
		buf_pa = cmm_xlator_translate(xlator, buf_va, CMM_VA2PA);
		if (buf_pa) {
			status = cmm_free_buf(xlator_obj->hcmm_mgr, buf_pa,
					      xlator_obj->ul_seg_id);
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
			xlator_obj->dw_virt_base = (u32) *paddr;
			xlator_obj->ul_virt_size = ul_size;
		} else {	/* return virt base address */
			*paddr = (u8 *) xlator_obj->dw_virt_base;
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

	cmm_mgr_obj = (struct cmm_object *)xlator_obj->hcmm_mgr;
	/* get this translator's default SM allocator */
	DBC_ASSERT(xlator_obj->ul_seg_id > 0);
	allocator = cmm_mgr_obj->pa_gppsm_seg_tab[xlator_obj->ul_seg_id - 1];
	if (!allocator)
		goto loop_cont;

	if ((xtype == CMM_VA2DSPPA) || (xtype == CMM_VA2PA) ||
	    (xtype == CMM_PA2VA)) {
		if (xtype == CMM_PA2VA) {
			/* Gpp Va = Va Base + offset */
			dw_offset = (u8 *) paddr - (u8 *) (allocator->shm_base -
							   allocator->
							   ul_dsp_size);
			dw_addr_xlate = xlator_obj->dw_virt_base + dw_offset;
			/* Check if translated Va base is in range */
			if ((dw_addr_xlate < xlator_obj->dw_virt_base) ||
			    (dw_addr_xlate >=
			     (xlator_obj->dw_virt_base +
			      xlator_obj->ul_virt_size))) {
				dw_addr_xlate = 0;	/* bad address */
			}
		} else {
			/* Gpp PA =  Gpp Base + offset */
			dw_offset =
			    (u8 *) paddr - (u8 *) xlator_obj->dw_virt_base;
			dw_addr_xlate =
			    allocator->shm_base - allocator->ul_dsp_size +
			    dw_offset;
		}
	} else {
		dw_addr_xlate = (u32) paddr;
	}
	/*Now convert address to proper target physical address if needed */
	if ((xtype == CMM_VA2DSPPA) || (xtype == CMM_PA2DSPPA)) {
		/* Got Gpp Pa now, convert to DSP Pa */
		dw_addr_xlate =
		    GPPPA2DSPPA((allocator->shm_base - allocator->ul_dsp_size),
				dw_addr_xlate,
				allocator->dw_dsp_phys_addr_offset *
				allocator->c_factor);
	} else if (xtype == CMM_DSPPA2PA) {
		/* Got DSP Pa, convert to GPP Pa */
		dw_addr_xlate =
		    DSPPA2GPPPA(allocator->shm_base - allocator->ul_dsp_size,
				dw_addr_xlate,
				allocator->dw_dsp_phys_addr_offset *
				allocator->c_factor);
	}
loop_cont:
	return (void *)dw_addr_xlate;
}
