/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition Communication (XPC) sn2-based functions.
 *
 *     Architecture specific implementation of common functions.
 *
 */

#include <linux/kernel.h>
#include <asm/uncached.h>
#include <asm/sn/sn_sal.h>
#include "xpc.h"

struct xpc_vars *xpc_vars;
struct xpc_vars_part *xpc_vars_part;

static enum xp_retval
xpc_rsvd_page_init_sn2(struct xpc_rsvd_page *rp)
{
	AMO_t *amos_page;
	u64 nasid_array = 0;
	int i;
	int ret;

	xpc_vars = XPC_RP_VARS(rp);

	rp->sn.vars_pa = __pa(xpc_vars);

	xpc_vars_part = XPC_RP_VARS_PART(rp);

	/*
	 * Before clearing xpc_vars, see if a page of AMOs had been previously
	 * allocated. If not we'll need to allocate one and set permissions
	 * so that cross-partition AMOs are allowed.
	 *
	 * The allocated AMO page needs MCA reporting to remain disabled after
	 * XPC has unloaded.  To make this work, we keep a copy of the pointer
	 * to this page (i.e., amos_page) in the struct xpc_vars structure,
	 * which is pointed to by the reserved page, and re-use that saved copy
	 * on subsequent loads of XPC. This AMO page is never freed, and its
	 * memory protections are never restricted.
	 */
	amos_page = xpc_vars->amos_page;
	if (amos_page == NULL) {
		amos_page = (AMO_t *)TO_AMO(uncached_alloc_page(0, 1));
		if (amos_page == NULL) {
			dev_err(xpc_part, "can't allocate page of AMOs\n");
			return xpNoMemory;
		}

		/*
		 * Open up AMO-R/W to cpu.  This is done for Shub 1.1 systems
		 * when xpc_allow_IPI_ops() is called via xpc_hb_init().
		 */
		if (!enable_shub_wars_1_1()) {
			ret = sn_change_memprotect(ia64_tpa((u64)amos_page),
						   PAGE_SIZE,
						   SN_MEMPROT_ACCESS_CLASS_1,
						   &nasid_array);
			if (ret != 0) {
				dev_err(xpc_part, "can't change memory "
					"protections\n");
				uncached_free_page(__IA64_UNCACHED_OFFSET |
						   TO_PHYS((u64)amos_page), 1);
				return xpSalError;
			}
		}
	}

	/* clear xpc_vars */
	memset(xpc_vars, 0, sizeof(struct xpc_vars));

	xpc_vars->version = XPC_V_VERSION;
	xpc_vars->act_nasid = cpuid_to_nasid(0);
	xpc_vars->act_phys_cpuid = cpu_physical_id(0);
	xpc_vars->vars_part_pa = __pa(xpc_vars_part);
	xpc_vars->amos_page_pa = ia64_tpa((u64)amos_page);
	xpc_vars->amos_page = amos_page;	/* save for next load of XPC */

	/* clear xpc_vars_part */
	memset((u64 *)xpc_vars_part, 0, sizeof(struct xpc_vars_part) *
	       xp_max_npartitions);

	/* initialize the activate IRQ related AMO variables */
	for (i = 0; i < xp_nasid_mask_words; i++)
		(void)xpc_IPI_init(XPC_ACTIVATE_IRQ_AMOS + i);

	/* initialize the engaged remote partitions related AMO variables */
	(void)xpc_IPI_init(XPC_ENGAGED_PARTITIONS_AMO);
	(void)xpc_IPI_init(XPC_DISENGAGE_REQUEST_AMO);

	return xpSuccess;
}

void
xpc_init_sn2(void)
{
	xpc_rsvd_page_init = xpc_rsvd_page_init_sn2;
}

void
xpc_exit_sn2(void)
{
}
