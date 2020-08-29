// SPDX-License-Identifier: GPL-2.0-only
/*
 * File:	mca_drv.c
 * Purpose:	Generic MCA handling layer
 *
 * Copyright (C) 2004 FUJITSU LIMITED
 * Copyright (C) 2004 Hidetoshi Seto <seto.hidetoshi@jp.fujitsu.com>
 * Copyright (C) 2005 Silicon Graphics, Inc
 * Copyright (C) 2005 Keith Owens <kaos@sgi.com>
 * Copyright (C) 2006 Russ Anderson <rja@sgi.com>
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kallsyms.h>
#include <linux/memblock.h>
#include <linux/acpi.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/workqueue.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/delay.h>
#include <asm/page.h>
#include <asm/ptrace.h>
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

typedef enum {
	MCA_NOT_RECOVERED = 0,
	MCA_RECOVERED	  = 1
} recovery_status_t;

/*
 *  This pool keeps pointers to the section part of SAL error record
 */
static struct {
	slidx_list_t *buffer; /* section pointer list pool */
	int	     cur_idx; /* Current index of section pointer list pool */
	int	     max_idx; /* Maximum index of section pointer list pool */
} slidx_pool;

static int
fatal_mca(const char *fmt, ...)
{
	va_list args;
	char buf[256];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	ia64_mca_printk(KERN_ALERT "MCA: %s\n", buf);

	return MCA_NOT_RECOVERED;
}

static int
mca_recovered(const char *fmt, ...)
{
	va_list args;
	char buf[256];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	ia64_mca_printk(KERN_INFO "MCA: %s\n", buf);

	return MCA_RECOVERED;
}

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
mca_handler_bh(unsigned long paddr, void *iip, unsigned long ipsr)
{
	ia64_mlogbuf_dump();
	printk(KERN_ERR "OS_MCA: process [cpu %d, pid: %d, uid: %d, "
		"iip: %p, psr: 0x%lx,paddr: 0x%lx](%s) encounters MCA.\n",
	       raw_smp_processor_id(), current->pid,
		from_kuid(&init_user_ns, current_uid()),
		iip, ipsr, paddr, current->comm);

	spin_lock(&mca_bh_lock);
	switch (mca_page_isolate(paddr)) {
	case ISOLATE_OK:
		printk(KERN_DEBUG "Page isolation: ( %lx ) success.\n", paddr);
		break;
	case ISOLATE_NG:
		printk(KERN_CRIT "Page isolation: ( %lx ) failure.\n", paddr);
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
	slidx_pool.buffer =
		kmalloc_array(slidx_pool.max_idx, sizeof(slidx_list_t),
			      GFP_KERNEL);

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
 * get_target_identifier - Get the valid Cache or Bus check target identifier.
 * @peidx:	pointer of index of processor error section
 *
 * Return value:
 *	target address on Success / 0 on Failure
 */
static u64
get_target_identifier(peidx_table_t *peidx)
{
	u64 target_address = 0;
	sal_log_mod_error_info_t *smei;
	pal_cache_check_info_t *pcci;
	int i, level = 9;

	/*
	 * Look through the cache checks for a valid target identifier
	 * If more than one valid target identifier, return the one
	 * with the lowest cache level.
	 */
	for (i = 0; i < peidx_cache_check_num(peidx); i++) {
		smei = (sal_log_mod_error_info_t *)peidx_cache_check(peidx, i);
		if (smei->valid.target_identifier && smei->target_identifier) {
			pcci = (pal_cache_check_info_t *)&(smei->check_info);
			if (!target_address || (pcci->level < level)) {
				target_address = smei->target_identifier;
				level = pcci->level;
				continue;
			}
		}
	}
	if (target_address)
		return target_address;

	/*
	 * Look at the bus check for a valid target identifier
	 */
	smei = peidx_bus_check(peidx, 0);
	if (smei && smei->valid.target_identifier)
		return smei->target_identifier;

	return 0;
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
	u64 target_identifier;
	struct pal_min_state_area *pmsa;
	struct ia64_psr *psr1, *psr2;
	ia64_fptr_t *mca_hdlr_bh = (ia64_fptr_t*)mca_handler_bhhook;

	/* Is target address valid? */
	target_identifier = get_target_identifier(peidx);
	if (!target_identifier)
		return fatal_mca("target address not valid");

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
		return fatal_mca("minstate not valid");
	psr1 =(struct ia64_psr *)&(peidx_minstate_area(peidx)->pmsa_ipsr);
	psr2 =(struct ia64_psr *)&(peidx_minstate_area(peidx)->pmsa_xpsr);

	/*
	 *  Check the privilege level of interrupted context.
	 *   If it is user-mode, then terminate affected process.
	 */

	pmsa = sos->pal_min_state;
	if (psr1->cpl != 0 ||
	   ((psr2->cpl != 0) && mca_recover_range(pmsa->pmsa_iip))) {
		/*
		 *  setup for resume to bottom half of MCA,
		 * "mca_handler_bhhook"
		 */
		/* pass to bhhook as argument (gr8, ...) */
		pmsa->pmsa_gr[8-1] = target_identifier;
		pmsa->pmsa_gr[9-1] = pmsa->pmsa_iip;
		pmsa->pmsa_gr[10-1] = pmsa->pmsa_ipsr;
		/* set interrupted return address (but no use) */
		pmsa->pmsa_br0 = pmsa->pmsa_iip;
		/* change resume address to bottom half */
		pmsa->pmsa_iip = mca_hdlr_bh->fp;
		pmsa->pmsa_gr[1-1] = mca_hdlr_bh->gp;
		/* set cpl with kernel mode */
		psr2 = (struct ia64_psr *)&pmsa->pmsa_ipsr;
		psr2->cpl = 0;
		psr2->ri  = 0;
		psr2->bn  = 1;
		psr2->i  = 0;

		return mca_recovered("user memory corruption. "
				"kill affected process - recovered.");
	}

	return fatal_mca("kernel context not recovered, iip 0x%lx\n",
			 pmsa->pmsa_iip);
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
	} else if (psp->cc && !psp->bc) {	/* Cache error */
		status = recover_from_read_error(slidx, peidx, pbci, sos);
	}

	return status;
}

/*
 * recover_from_tlb_check
 * @peidx:	pointer of index of processor error section
 *
 * Return value:
 *	1 on Success / 0 on Failure
 */
static int
recover_from_tlb_check(peidx_table_t *peidx)
{
	sal_log_mod_error_info_t *smei;
	pal_tlb_check_info_t *ptci;

	smei = (sal_log_mod_error_info_t *)peidx_tlb_check(peidx, 0);
	ptci = (pal_tlb_check_info_t *)&(smei->check_info);

	/*
	 * Look for signature of a duplicate TLB DTC entry, which is
	 * a SW bug and always fatal.
	 */
	if (ptci->op == PAL_TLB_CHECK_OP_PURGE
	    && !(ptci->itr || ptci->dtc || ptci->itc))
		return fatal_mca("Duplicate TLB entry");

	return mca_recovered("TLB check recovered");
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
		return mca_recovered("machine check is already corrected.");

	/*
	 * The error was not contained.  Software must be reset.
	 */
	if (psp->us || psp->ci == 0)
		return fatal_mca("error not contained");

	/*
	 * Look for recoverable TLB check
	 */
	if (psp->tc && !(psp->cc || psp->bc || psp->rc || psp->uc))
		return recover_from_tlb_check(peidx);

	/*
	 * The cache check and bus check bits have four possible states
	 *   cc bc
	 *    1  1	Memory error, attempt recovery
	 *    1  0	Cache error, attempt recovery
	 *    0  1	I/O error, attempt recovery
	 *    0  0	Other error type, not recovered
	 */
	if (psp->cc == 0 && (psp->bc == 0 || pbci == NULL))
		return fatal_mca("No cache or bus check");

	/*
	 * Cannot handle more than one bus check.
	 */
	if (peidx_bus_check_num(peidx) > 1)
		return fatal_mca("Too many bus checks");

	if (pbci->ib)
		return fatal_mca("Internal Bus error");
	if (pbci->eb && pbci->bsi > 0)
		return fatal_mca("External bus check fatal status");

	/*
	 * This is a local MCA and estimated as a recoverable error.
	 */
	if (platform)
		return recover_from_platform_error(slidx, peidx, pbci, sos);

	/*
	 * On account of strange SAL error record, we cannot recover.
	 */
	return fatal_mca("Strange SAL record");
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
		return fatal_mca("Too Many Errors");
	else if (n_proc_err == 0)
		/* Weird SAL record ... We can't do anything */
		return fatal_mca("Weird SAL record");

	/* Make index of processor error section */
	mca_make_peidx((sal_log_processor_info_t*)
		slidx_first_entry(&slidx.proc_err)->hdr, &peidx);

	/* Extract Processor BUS_CHECK[0] */
	*((u64*)&pbci) = peidx_check_info(&peidx, bus_check, 0);

	/* Check whether MCA is global or not */
	if (is_mca_global(&peidx, &pbci, sos))
		return fatal_mca("global MCA");
	
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
