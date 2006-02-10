/*
 * File:	mca_drv.c
 * Purpose:	Generic MCA handling layer
 *
 * Copyright (C) 2004 FUJITSU LIMITED
 * Copyright (C) Hidetoshi Seto (seto.hidetoshi@jp.fujitsu.com)
 * Copyright (C) 2005 Silicon Graphics, Inc
 * Copyright (C) 2005 Keith Owens <kaos@sgi.com>
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kallsyms.h>
#include <linux/smp_lock.h>
#include <linux/bootmem.h>
#include <linux/acpi.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/workqueue.h>
#include <linux/mm.h>

#include <asm/delay.h>
#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/sal.h>
#include <asm/mca.h>

#include <asm/irq.h>
#include <asm/hw_irq.h>

#include "mca_drv.h"

/* max size of SAL error record (default) */
static int sal_rec_max = 10000;

/* from mca_drv_asm.S */
extern void *mca_handler_bhhook(void);

static DEFINE_SPINLOCK(mca_bh_lock);

typedef enum {
	MCA_IS_LOCAL  = 0,
	MCA_IS_GLOBAL = 1
} mca_type_t;

#define MAX_PAGE_ISOLATE 1024

static struct page *page_isolate[MAX_PAGE_ISOLATE];
static int num_page_isolate = 0;

typedef enum {
	ISOLATE_NG,
	ISOLATE_OK,
	ISOLATE_NONE
} isolate_status_t;

/*
 *  This pool keeps pointers to the section part of SAL error record
 */
static struct {
	slidx_list_t *buffer; /* section pointer list pool */
	int	     cur_idx; /* Current index of section pointer list pool */
	int	     max_idx; /* Maximum index of section pointer list pool */
} slidx_pool;

/**
 * mca_page_isolate - isolate a poisoned page in order not to use it later
 * @paddr:	poisoned memory location
 *
 * Return value:
 *	one of isolate_status_t, ISOLATE_OK/NG/NONE.
 */

static isolate_status_t
mca_page_isolate(unsigned long paddr)
{
	int i;
	struct page *p;

	/* whether physical address is valid or not */
	if (!ia64_phys_addr_valid(paddr))
		return ISOLATE_NONE;

	if (!pfn_valid(paddr >> PAGE_SHIFT))
		return ISOLATE_NONE;

	/* convert physical address to physical page number */
	p = pfn_to_page(paddr>>PAGE_SHIFT);

	/* check whether a page number have been already registered or not */
	for (i = 0; i < num_page_isolate; i++)
		if (page_isolate[i] == p)
			return ISOLATE_OK; /* already listed */

	/* limitation check */
	if (num_page_isolate == MAX_PAGE_ISOLATE)
		return ISOLATE_NG;

	/* kick pages having attribute 'SLAB' or 'Reserved' */
	if (PageSlab(p) || PageReserved(p))
		return ISOLATE_NG;

	/* add attribute 'Reserved' and register the page */
	get_page(p);
	SetPageReserved(p);
	page_isolate[num_page_isolate++] = p;

	return ISOLATE_OK;
}

/**
 * mca_hanlder_bh - Kill the process which occurred memory read error
 * @paddr:	poisoned address received from MCA Handler
 */

void
mca_handler_bh(unsigned long paddr)
{
	printk(KERN_DEBUG "OS_MCA: process [pid: %d](%s) encounters MCA.\n",
		current->pid, current->comm);

	spin_lock(&mca_bh_lock);
	switch (mca_page_isolate(paddr)) {
	case ISOLATE_OK:
		printk(KERN_DEBUG "Page isolation: ( %lx ) success.\n", paddr);
		break;
	case ISOLATE_NG:
		printk(KERN_DEBUG "Page isolation: ( %lx ) failure.\n", paddr);
		break;
	default:
		break;
	}
	spin_unlock(&mca_bh_lock);

	/* This process is about to be killed itself */
	do_exit(SIGKILL);
}

/**
 * mca_make_peidx - Make index of processor error section
 * @slpi:	pointer to record of processor error section
 * @peidx:	pointer to index of processor error section
 */

static void
mca_make_peidx(sal_log_processor_info_t *slpi, peidx_table_t *peidx)
{
	/*
	 * calculate the start address of
	 *   "struct cpuid_info" and "sal_processor_static_info_t".
	 */
	u64 total_check_num = slpi->valid.num_cache_check
				+ slpi->valid.num_tlb_check
				+ slpi->valid.num_bus_check
				+ slpi->valid.num_reg_file_check
				+ slpi->valid.num_ms_check;
	u64 head_size =	sizeof(sal_log_mod_error_info_t) * total_check_num
			+ sizeof(sal_log_processor_info_t);
	u64 mid_size  = slpi->valid.cpuid_info * sizeof(struct sal_cpuid_info);

	peidx_head(peidx)   = slpi;
	peidx_mid(peidx)    = (struct sal_cpuid_info *)
		(slpi->valid.cpuid_info ? ((char*)slpi + head_size) : NULL);
	peidx_bottom(peidx) = (sal_processor_static_info_t *)
		(slpi->valid.psi_static_struct ?
			((char*)slpi + head_size + mid_size) : NULL);
}

/**
 * mca_make_slidx -  Make index of SAL error record
 * @buffer:	pointer to SAL error record
 * @slidx:	pointer to index of SAL error record
 *
 * Return value:
 *	1 if record has platform error / 0 if not
 */
#define LOG_INDEX_ADD_SECT_PTR(sect, ptr) \
	{slidx_list_t *hl = &slidx_pool.buffer[slidx_pool.cur_idx]; \
	hl->hdr = ptr; \
	list_add(&hl->list, &(sect)); \
	slidx_pool.cur_idx = (slidx_pool.cur_idx + 1)%slidx_pool.max_idx; }

static int
mca_make_slidx(void *buffer, slidx_table_t *slidx)
{
	int platform_err = 0;
	int record_len = ((sal_log_record_header_t*)buffer)->len;
	u32 ercd_pos;
	int sects;
	sal_log_section_hdr_t *sp;

	/*
	 * Initialize index referring current record
	 */
	INIT_LIST_HEAD(&(slidx->proc_err));
	INIT_LIST_HEAD(&(slidx->mem_dev_err));
	INIT_LIST_HEAD(&(slidx->sel_dev_err));
	INIT_LIST_HEAD(&(slidx->pci_bus_err));
	INIT_LIST_HEAD(&(slidx->smbios_dev_err));
	INIT_LIST_HEAD(&(slidx->pci_comp_err));
	INIT_LIST_HEAD(&(slidx->plat_specific_err));
	INIT_LIST_HEAD(&(slidx->host_ctlr_err));
	INIT_LIST_HEAD(&(slidx->plat_bus_err));
	INIT_LIST_HEAD(&(slidx->unsupported));

	/*
	 * Extract a Record Header
	 */
	slidx->header = buffer;

	/*
	 * Extract each section records
	 * (arranged from "int ia64_log_platform_info_print()")
	 */
	for (ercd_pos = sizeof(sal_log_record_header_t), sects = 0;
		ercd_pos < record_len; ercd_pos += sp->len, sects++) {
		sp = (sal_log_section_hdr_t *)((char*)buffer + ercd_pos);
		if (!efi_guidcmp(sp->guid, SAL_PROC_DEV_ERR_SECT_GUID)) {
			LOG_INDEX_ADD_SECT_PTR(slidx->proc_err, sp);
		} else if (!efi_guidcmp(sp->guid,
				SAL_PLAT_MEM_DEV_ERR_SECT_GUID)) {
			platform_err = 1;
			LOG_INDEX_ADD_SECT_PTR(slidx->mem_dev_err, sp);
		} else if (!efi_guidcmp(sp->guid,
				SAL_PLAT_SEL_DEV_ERR_SECT_GUID)) {
			platform_err = 1;
			LOG_INDEX_ADD_SECT_PTR(slidx->sel_dev_err, sp);
		} else if (!efi_guidcmp(sp->guid,
				SAL_PLAT_PCI_BUS_ERR_SECT_GUID)) {
			platform_err = 1;
			LOG_INDEX_ADD_SECT_PTR(slidx->pci_bus_err, sp);
		} else if (!efi_guidcmp(sp->guid,
				SAL_PLAT_SMBIOS_DEV_ERR_SECT_GUID)) {
			platform_err = 1;
			LOG_INDEX_ADD_SECT_PTR(slidx->smbios_dev_err, sp);
		} else if (!efi_guidcmp(sp->guid,
				SAL_PLAT_PCI_COMP_ERR_SECT_GUID)) {
			platform_err = 1;
			LOG_INDEX_ADD_SECT_PTR(slidx->pci_comp_err, sp);
		} else if (!efi_guidcmp(sp->guid,
				SAL_PLAT_SPECIFIC_ERR_SECT_GUID)) {
			platform_err = 1;
			LOG_INDEX_ADD_SECT_PTR(slidx->plat_specific_err, sp);
		} else if (!efi_guidcmp(sp->guid,
				SAL_PLAT_HOST_CTLR_ERR_SECT_GUID)) {
			platform_err = 1;
			LOG_INDEX_ADD_SECT_PTR(slidx->host_ctlr_err, sp);
		} else if (!efi_guidcmp(sp->guid,
				SAL_PLAT_BUS_ERR_SECT_GUID)) {
			platform_err = 1;
			LOG_INDEX_ADD_SECT_PTR(slidx->plat_bus_err, sp);
		} else {
			LOG_INDEX_ADD_SECT_PTR(slidx->unsupported, sp);
		}
	}
	slidx->n_sections = sects;

	return platform_err;
}

/**
 * init_record_index_pools - Initialize pool of lists for SAL record index
 *
 * Return value:
 *	0 on Success / -ENOMEM on Failure
 */
static int
init_record_index_pools(void)
{
	int i;
	int rec_max_size;  /* Maximum size of SAL error records */
	int sect_min_size; /* Minimum size of SAL error sections */
	/* minimum size table of each section */
	static int sal_log_sect_min_sizes[] = {
		sizeof(sal_log_processor_info_t)
		+ sizeof(sal_processor_static_info_t),
		sizeof(sal_log_mem_dev_err_info_t),
		sizeof(sal_log_sel_dev_err_info_t),
		sizeof(sal_log_pci_bus_err_info_t),
		sizeof(sal_log_smbios_dev_err_info_t),
		sizeof(sal_log_pci_comp_err_info_t),
		sizeof(sal_log_plat_specific_err_info_t),
		sizeof(sal_log_host_ctlr_err_info_t),
		sizeof(sal_log_plat_bus_err_info_t),
	};

	/*
	 * MCA handler cannot allocate new memory on flight,
	 * so we preallocate enough memory to handle a SAL record.
	 *
	 * Initialize a handling set of slidx_pool:
	 *   1. Pick up the max size of SAL error records
	 *   2. Pick up the min size of SAL error sections
	 *   3. Allocate the pool as enough to 2 SAL records
	 *     (now we can estimate the maxinum of section in a record.)
	 */

	/* - 1 - */
	rec_max_size = sal_rec_max;

	/* - 2 - */
	sect_min_size = sal_log_sect_min_sizes[0];
	for (i = 1; i < sizeof sal_log_sect_min_sizes/sizeof(size_t); i++)
		if (sect_min_size > sal_log_sect_min_sizes[i])
			sect_min_size = sal_log_sect_min_sizes[i];

	/* - 3 - */
	slidx_pool.max_idx = (rec_max_size/sect_min_size) * 2 + 1;
	slidx_pool.buffer = (slidx_list_t *)
		kmalloc(slidx_pool.max_idx * sizeof(slidx_list_t), GFP_KERNEL);

	return slidx_pool.buffer ? 0 : -ENOMEM;
}


/*****************************************************************************
 * Recovery functions                                                        *
 *****************************************************************************/

/**
 * is_mca_global - Check whether this MCA is global or not
 * @peidx:	pointer of index of processor error section
 * @pbci:	pointer to pal_bus_check_info_t
 * @sos:	pointer to hand off struct between SAL and OS
 *
 * Return value:
 *	MCA_IS_LOCAL / MCA_IS_GLOBAL
 */

static mca_type_t
is_mca_global(peidx_table_t *peidx, pal_bus_check_info_t *pbci,
	      struct ia64_sal_os_state *sos)
{
	pal_processor_state_info_t *psp =
		(pal_processor_state_info_t*)peidx_psp(peidx);

	/*
	 * PAL can request a rendezvous, if the MCA has a global scope.
	 * If "rz_always" flag is set, SAL requests MCA rendezvous
	 * in spite of global MCA.
	 * Therefore it is local MCA when rendezvous has not been requested.
	 * Failed to rendezvous, the system must be down.
	 */
	switch (sos->rv_rc) {
		case -1: /* SAL rendezvous unsuccessful */
			return MCA_IS_GLOBAL;
		case  0: /* SAL rendezvous not required */
			return MCA_IS_LOCAL;
		case  1: /* SAL rendezvous successful int */
		case  2: /* SAL rendezvous successful int with init */
		default:
			break;
	}

	/*
	 * If One or more Cache/TLB/Reg_File/Uarch_Check is here,
	 * it would be a local MCA. (i.e. processor internal error)
	 */
	if (psp->tc || psp->cc || psp->rc || psp->uc)
		return MCA_IS_LOCAL;
	
	/*
	 * Bus_Check structure with Bus_Check.ib (internal bus error) flag set
	 * would be a global MCA. (e.g. a system bus address parity error)
	 */
	if (!pbci || pbci->ib)
		return MCA_IS_GLOBAL;

	/*
	 * Bus_Check structure with Bus_Check.eb (external bus error) flag set
	 * could be either a local MCA or a global MCA.
	 *
	 * Referring Bus_Check.bsi:
	 *   0: Unknown/unclassified
	 *   1: BERR#
	 *   2: BINIT#
	 *   3: Hard Fail
	 * (FIXME: Are these SGI specific or generic bsi values?)
	 */
	if (pbci->eb)
		switch (pbci->bsi) {
			case 0:
				/* e.g. a load from poisoned memory */
				return MCA_IS_LOCAL;
			case 1:
			case 2:
			case 3:
				return MCA_IS_GLOBAL;
		}

	return MCA_IS_GLOBAL;
}

/**
 * recover_from_read_error - Try to recover the errors which type are "read"s.
 * @slidx:	pointer of index of SAL error record
 * @peidx:	pointer of index of processor error section
 * @pbci:	pointer of pal_bus_check_info
 * @sos:	pointer to hand off struct between SAL and OS
 *
 * Return value:
 *	1 on Success / 0 on Failure
 */

static int
recover_from_read_error(slidx_table_t *slidx,
			peidx_table_t *peidx, pal_bus_check_info_t *pbci,
			struct ia64_sal_os_state *sos)
{
	sal_log_mod_error_info_t *smei;
	pal_min_state_area_t *pmsa;
	struct ia64_psr *psr1, *psr2;
	ia64_fptr_t *mca_hdlr_bh = (ia64_fptr_t*)mca_handler_bhhook;

	/* Is target address valid? */
	if (!pbci->tv)
		return 0;

	/*
	 * cpu read or memory-mapped io read
	 *
	 *    offending process  affected process  OS MCA do
	 *     kernel mode        kernel mode       down system
	 *     kernel mode        user   mode       kill the process
	 *     user   mode        kernel mode       down system (*)
	 *     user   mode        user   mode       kill the process
	 *
	 * (*) You could terminate offending user-mode process
	 *    if (pbci->pv && pbci->pl != 0) *and* if you sure
	 *    the process not have any locks of kernel.
	 */

	/* Is minstate valid? */
	if (!peidx_bottom(peidx) || !(peidx_bottom(peidx)->valid.minstate))
		return 0;
	psr1 =(struct ia64_psr *)&(peidx_minstate_area(peidx)->pmsa_ipsr);

	/*
	 *  Check the privilege level of interrupted context.
	 *   If it is user-mode, then terminate affected process.
	 */
	if (psr1->cpl != 0) {
		smei = peidx_bus_check(peidx, 0);
		if (smei->valid.target_identifier) {
			/*
			 *  setup for resume to bottom half of MCA,
			 * "mca_handler_bhhook"
			 */
			pmsa = sos->pal_min_state;
			/* pass to bhhook as 1st argument (gr8) */
			pmsa->pmsa_gr[8-1] = smei->target_identifier;
			/* set interrupted return address (but no use) */
			pmsa->pmsa_br0 = pmsa->pmsa_iip;
			/* change resume address to bottom half */
			pmsa->pmsa_iip = mca_hdlr_bh->fp;
			pmsa->pmsa_gr[1-1] = mca_hdlr_bh->gp;
			/* set cpl with kernel mode */
			psr2 = (struct ia64_psr *)&pmsa->pmsa_ipsr;
			psr2->cpl = 0;
			psr2->ri  = 0;
			psr2->i  = 0;

			return 1;
		}

	}

	return 0;
}

/**
 * recover_from_platform_error - Recover from platform error.
 * @slidx:	pointer of index of SAL error record
 * @peidx:	pointer of index of processor error section
 * @pbci:	pointer of pal_bus_check_info
 * @sos:	pointer to hand off struct between SAL and OS
 *
 * Return value:
 *	1 on Success / 0 on Failure
 */

static int
recover_from_platform_error(slidx_table_t *slidx, peidx_table_t *peidx,
			    pal_bus_check_info_t *pbci,
			    struct ia64_sal_os_state *sos)
{
	int status = 0;
	pal_processor_state_info_t *psp =
		(pal_processor_state_info_t*)peidx_psp(peidx);

	if (psp->bc && pbci->eb && pbci->bsi == 0) {
		switch(pbci->type) {
		case 1: /* partial read */
		case 3: /* full line(cpu) read */
		case 9: /* I/O space read */
			status = recover_from_read_error(slidx, peidx, pbci,
							 sos);
			break;
		case 0: /* unknown */
		case 2: /* partial write */
		case 4: /* full line write */
		case 5: /* implicit or explicit write-back operation */
		case 6: /* snoop probe */
		case 7: /* incoming or outgoing ptc.g */
		case 8: /* write coalescing transactions */
		case 10: /* I/O space write */
		case 11: /* inter-processor interrupt message(IPI) */
		case 12: /* interrupt acknowledge or
				external task priority cycle */
		default:
			break;
		}
	}

	return status;
}

/**
 * recover_from_processor_error
 * @platform:	whether there are some platform error section or not
 * @slidx:	pointer of index of SAL error record
 * @peidx:	pointer of index of processor error section
 * @pbci:	pointer of pal_bus_check_info
 * @sos:	pointer to hand off struct between SAL and OS
 *
 * Return value:
 *	1 on Success / 0 on Failure
 */
/*
 *  Later we try to recover when below all conditions are satisfied.
 *   1. Only one processor error section is exist.
 *   2. BUS_CHECK is exist and the others are not exist.(Except TLB_CHECK)
 *   3. The entry of BUS_CHECK_INFO is 1.
 *   4. "External bus error" flag is set and the others are not set.
 */

static int
recover_from_processor_error(int platform, slidx_table_t *slidx,
			     peidx_table_t *peidx, pal_bus_check_info_t *pbci,
			     struct ia64_sal_os_state *sos)
{
	pal_processor_state_info_t *psp =
		(pal_processor_state_info_t*)peidx_psp(peidx);

	/*
	 * Processor recovery status must key off of the PAL recovery
	 * status in the Processor State Parameter.
	 */

	/*
	 * The machine check is corrected.
	 */
	if (psp->cm == 1)
		return 1;

	/*
	 * The error was not contained.  Software must be reset.
	 */
	if (psp->us || psp->ci == 0)
		return 0;

	/*
	 * If there is no bus error, record is weird but we need not to recover.
	 */
	if (psp->bc == 0 || pbci == NULL)
		return 1;

	/*
	 * Sorry, we cannot handle so many.
	 */
	if (peidx_bus_check_num(peidx) > 1)
		return 0;
	/*
	 * Well, here is only one bus error.
	 */
	if (pbci->ib || pbci->cc)
		return 0;
	if (pbci->eb && pbci->bsi > 0)
		return 0;

	/*
	 * This is a local MCA and estimated as recoverble external bus error.
	 * (e.g. a load from poisoned memory)
	 * This means "there are some platform errors".
	 */
	if (platform)
		return recover_from_platform_error(slidx, peidx, pbci, sos);
	/*
	 * On account of strange SAL error record, we cannot recover.
	 */
	return 0;
}

/**
 * mca_try_to_recover - Try to recover from MCA
 * @rec:	pointer to a SAL error record
 * @sos:	pointer to hand off struct between SAL and OS
 *
 * Return value:
 *	1 on Success / 0 on Failure
 */

static int
mca_try_to_recover(void *rec, struct ia64_sal_os_state *sos)
{
	int platform_err;
	int n_proc_err;
	slidx_table_t slidx;
	peidx_table_t peidx;
	pal_bus_check_info_t pbci;

	/* Make index of SAL error record */
	platform_err = mca_make_slidx(rec, &slidx);

	/* Count processor error sections */
	n_proc_err = slidx_count(&slidx, proc_err);

	 /* Now, OS can recover when there is one processor error section */
	if (n_proc_err > 1)
		return 0;
	else if (n_proc_err == 0) {
		/* Weird SAL record ... We need not to recover */

		return 1;
	}

	/* Make index of processor error section */
	mca_make_peidx((sal_log_processor_info_t*)
		slidx_first_entry(&slidx.proc_err)->hdr, &peidx);

	/* Extract Processor BUS_CHECK[0] */
	*((u64*)&pbci) = peidx_check_info(&peidx, bus_check, 0);

	/* Check whether MCA is global or not */
	if (is_mca_global(&peidx, &pbci, sos))
		return 0;
	
	/* Try to recover a processor error */
	return recover_from_processor_error(platform_err, &slidx, &peidx,
					    &pbci, sos);
}

/*
 * =============================================================================
 */

int __init mca_external_handler_init(void)
{
	if (init_record_index_pools())
		return -ENOMEM;

	/* register external mca handlers */
	if (ia64_reg_MCA_extension(mca_try_to_recover)) {	
		printk(KERN_ERR "ia64_reg_MCA_extension failed.\n");
		kfree(slidx_pool.buffer);
		return -EFAULT;
	}
	return 0;
}

void __exit mca_external_handler_exit(void)
{
	/* unregister external mca handlers */
	ia64_unreg_MCA_extension();
	kfree(slidx_pool.buffer);
}

module_init(mca_external_handler_init);
module_exit(mca_external_handler_exit);

module_param(sal_rec_max, int, 0644);
MODULE_PARM_DESC(sal_rec_max, "Max size of SAL error record");

MODULE_DESCRIPTION("ia64 platform dependent mca handler driver");
MODULE_LICENSE("GPL");
