/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/synch.h>
#include <misc/cxl.h>

#include "cxl.h"

static int afu_control(struct cxl_afu *afu, u64 command,
		       u64 result, u64 mask, bool enabled)
{
	u64 AFU_Cntl = cxl_p2n_read(afu, CXL_AFU_Cntl_An);
	unsigned long timeout = jiffies + (HZ * CXL_TIMEOUT);

	spin_lock(&afu->afu_cntl_lock);
	pr_devel("AFU command starting: %llx\n", command);

	cxl_p2n_write(afu, CXL_AFU_Cntl_An, AFU_Cntl | command);

	AFU_Cntl = cxl_p2n_read(afu, CXL_AFU_Cntl_An);
	while ((AFU_Cntl & mask) != result) {
		if (time_after_eq(jiffies, timeout)) {
			dev_warn(&afu->dev, "WARNING: AFU control timed out!\n");
			spin_unlock(&afu->afu_cntl_lock);
			return -EBUSY;
		}
		pr_devel_ratelimited("AFU control... (0x%.16llx)\n",
				     AFU_Cntl | command);
		cpu_relax();
		AFU_Cntl = cxl_p2n_read(afu, CXL_AFU_Cntl_An);
	};
	pr_devel("AFU command complete: %llx\n", command);
	afu->enabled = enabled;
	spin_unlock(&afu->afu_cntl_lock);

	return 0;
}

static int afu_enable(struct cxl_afu *afu)
{
	pr_devel("AFU enable request\n");

	return afu_control(afu, CXL_AFU_Cntl_An_E,
			   CXL_AFU_Cntl_An_ES_Enabled,
			   CXL_AFU_Cntl_An_ES_MASK, true);
}

int cxl_afu_disable(struct cxl_afu *afu)
{
	pr_devel("AFU disable request\n");

	return afu_control(afu, 0, CXL_AFU_Cntl_An_ES_Disabled,
			   CXL_AFU_Cntl_An_ES_MASK, false);
}

/* This will disable as well as reset */
int cxl_afu_reset(struct cxl_afu *afu)
{
	pr_devel("AFU reset request\n");

	return afu_control(afu, CXL_AFU_Cntl_An_RA,
			   CXL_AFU_Cntl_An_RS_Complete | CXL_AFU_Cntl_An_ES_Disabled,
			   CXL_AFU_Cntl_An_RS_MASK | CXL_AFU_Cntl_An_ES_MASK,
			   false);
}

static int afu_check_and_enable(struct cxl_afu *afu)
{
	if (afu->enabled)
		return 0;
	return afu_enable(afu);
}

int cxl_psl_purge(struct cxl_afu *afu)
{
	u64 PSL_CNTL = cxl_p1n_read(afu, CXL_PSL_SCNTL_An);
	u64 AFU_Cntl = cxl_p2n_read(afu, CXL_AFU_Cntl_An);
	u64 dsisr, dar;
	u64 start, end;
	unsigned long timeout = jiffies + (HZ * CXL_TIMEOUT);

	pr_devel("PSL purge request\n");

	if ((AFU_Cntl & CXL_AFU_Cntl_An_ES_MASK) != CXL_AFU_Cntl_An_ES_Disabled) {
		WARN(1, "psl_purge request while AFU not disabled!\n");
		cxl_afu_disable(afu);
	}

	cxl_p1n_write(afu, CXL_PSL_SCNTL_An,
		       PSL_CNTL | CXL_PSL_SCNTL_An_Pc);
	start = local_clock();
	PSL_CNTL = cxl_p1n_read(afu, CXL_PSL_SCNTL_An);
	while ((PSL_CNTL &  CXL_PSL_SCNTL_An_Ps_MASK)
			== CXL_PSL_SCNTL_An_Ps_Pending) {
		if (time_after_eq(jiffies, timeout)) {
			dev_warn(&afu->dev, "WARNING: PSL Purge timed out!\n");
			return -EBUSY;
		}
		dsisr = cxl_p2n_read(afu, CXL_PSL_DSISR_An);
		pr_devel_ratelimited("PSL purging... PSL_CNTL: 0x%.16llx  PSL_DSISR: 0x%.16llx\n", PSL_CNTL, dsisr);
		if (dsisr & CXL_PSL_DSISR_TRANS) {
			dar = cxl_p2n_read(afu, CXL_PSL_DAR_An);
			dev_notice(&afu->dev, "PSL purge terminating pending translation, DSISR: 0x%.16llx, DAR: 0x%.16llx\n", dsisr, dar);
			cxl_p2n_write(afu, CXL_PSL_TFC_An, CXL_PSL_TFC_An_AE);
		} else if (dsisr) {
			dev_notice(&afu->dev, "PSL purge acknowledging pending non-translation fault, DSISR: 0x%.16llx\n", dsisr);
			cxl_p2n_write(afu, CXL_PSL_TFC_An, CXL_PSL_TFC_An_A);
		} else {
			cpu_relax();
		}
		PSL_CNTL = cxl_p1n_read(afu, CXL_PSL_SCNTL_An);
	};
	end = local_clock();
	pr_devel("PSL purged in %lld ns\n", end - start);

	cxl_p1n_write(afu, CXL_PSL_SCNTL_An,
		       PSL_CNTL & ~CXL_PSL_SCNTL_An_Pc);
	return 0;
}

static int spa_max_procs(int spa_size)
{
	/*
	 * From the CAIA:
	 *    end_of_SPA_area = SPA_Base + ((n+4) * 128) + (( ((n*8) + 127) >> 7) * 128) + 255
	 * Most of that junk is really just an overly-complicated way of saying
	 * the last 256 bytes are __aligned(128), so it's really:
	 *    end_of_SPA_area = end_of_PSL_queue_area + __aligned(128) 255
	 * and
	 *    end_of_PSL_queue_area = SPA_Base + ((n+4) * 128) + (n*8) - 1
	 * so
	 *    sizeof(SPA) = ((n+4) * 128) + (n*8) + __aligned(128) 256
	 * Ignore the alignment (which is safe in this case as long as we are
	 * careful with our rounding) and solve for n:
	 */
	return ((spa_size / 8) - 96) / 17;
}

static int alloc_spa(struct cxl_afu *afu)
{
	u64 spap;

	/* Work out how many pages to allocate */
	afu->spa_order = 0;
	do {
		afu->spa_order++;
		afu->spa_size = (1 << afu->spa_order) * PAGE_SIZE;
		afu->spa_max_procs = spa_max_procs(afu->spa_size);
	} while (afu->spa_max_procs < afu->num_procs);

	WARN_ON(afu->spa_size > 0x100000); /* Max size supported by the hardware */

	if (!(afu->spa = (struct cxl_process_element *)
	      __get_free_pages(GFP_KERNEL | __GFP_ZERO, afu->spa_order))) {
		pr_err("cxl_alloc_spa: Unable to allocate scheduled process area\n");
		return -ENOMEM;
	}
	pr_devel("spa pages: %i afu->spa_max_procs: %i   afu->num_procs: %i\n",
		 1<<afu->spa_order, afu->spa_max_procs, afu->num_procs);

	afu->sw_command_status = (__be64 *)((char *)afu->spa +
					    ((afu->spa_max_procs + 3) * 128));

	spap = virt_to_phys(afu->spa) & CXL_PSL_SPAP_Addr;
	spap |= ((afu->spa_size >> (12 - CXL_PSL_SPAP_Size_Shift)) - 1) & CXL_PSL_SPAP_Size;
	spap |= CXL_PSL_SPAP_V;
	pr_devel("cxl: SPA allocated at 0x%p. Max processes: %i, sw_command_status: 0x%p CXL_PSL_SPAP_An=0x%016llx\n", afu->spa, afu->spa_max_procs, afu->sw_command_status, spap);
	cxl_p1n_write(afu, CXL_PSL_SPAP_An, spap);

	return 0;
}

static void release_spa(struct cxl_afu *afu)
{
	free_pages((unsigned long) afu->spa, afu->spa_order);
}

int cxl_tlb_slb_invalidate(struct cxl *adapter)
{
	unsigned long timeout = jiffies + (HZ * CXL_TIMEOUT);

	pr_devel("CXL adapter wide TLBIA & SLBIA\n");

	cxl_p1_write(adapter, CXL_PSL_AFUSEL, CXL_PSL_AFUSEL_A);

	cxl_p1_write(adapter, CXL_PSL_TLBIA, CXL_TLB_SLB_IQ_ALL);
	while (cxl_p1_read(adapter, CXL_PSL_TLBIA) & CXL_TLB_SLB_P) {
		if (time_after_eq(jiffies, timeout)) {
			dev_warn(&adapter->dev, "WARNING: CXL adapter wide TLBIA timed out!\n");
			return -EBUSY;
		}
		cpu_relax();
	}

	cxl_p1_write(adapter, CXL_PSL_SLBIA, CXL_TLB_SLB_IQ_ALL);
	while (cxl_p1_read(adapter, CXL_PSL_SLBIA) & CXL_TLB_SLB_P) {
		if (time_after_eq(jiffies, timeout)) {
			dev_warn(&adapter->dev, "WARNING: CXL adapter wide SLBIA timed out!\n");
			return -EBUSY;
		}
		cpu_relax();
	}
	return 0;
}

int cxl_afu_slbia(struct cxl_afu *afu)
{
	unsigned long timeout = jiffies + (HZ * CXL_TIMEOUT);

	pr_devel("cxl_afu_slbia issuing SLBIA command\n");
	cxl_p2n_write(afu, CXL_SLBIA_An, CXL_TLB_SLB_IQ_ALL);
	while (cxl_p2n_read(afu, CXL_SLBIA_An) & CXL_TLB_SLB_P) {
		if (time_after_eq(jiffies, timeout)) {
			dev_warn(&afu->dev, "WARNING: CXL AFU SLBIA timed out!\n");
			return -EBUSY;
		}
		cpu_relax();
	}
	return 0;
}

static int cxl_write_sstp(struct cxl_afu *afu, u64 sstp0, u64 sstp1)
{
	int rc;

	/* 1. Disable SSTP by writing 0 to SSTP1[V] */
	cxl_p2n_write(afu, CXL_SSTP1_An, 0);

	/* 2. Invalidate all SLB entries */
	if ((rc = cxl_afu_slbia(afu)))
		return rc;

	/* 3. Set SSTP0_An */
	cxl_p2n_write(afu, CXL_SSTP0_An, sstp0);

	/* 4. Set SSTP1_An */
	cxl_p2n_write(afu, CXL_SSTP1_An, sstp1);

	return 0;
}

/* Using per slice version may improve performance here. (ie. SLBIA_An) */
static void slb_invalid(struct cxl_context *ctx)
{
	struct cxl *adapter = ctx->afu->adapter;
	u64 slbia;

	WARN_ON(!mutex_is_locked(&ctx->afu->spa_mutex));

	cxl_p1_write(adapter, CXL_PSL_LBISEL,
			((u64)be32_to_cpu(ctx->elem->common.pid) << 32) |
			be32_to_cpu(ctx->elem->lpid));
	cxl_p1_write(adapter, CXL_PSL_SLBIA, CXL_TLB_SLB_IQ_LPIDPID);

	while (1) {
		slbia = cxl_p1_read(adapter, CXL_PSL_SLBIA);
		if (!(slbia & CXL_TLB_SLB_P))
			break;
		cpu_relax();
	}
}

static int do_process_element_cmd(struct cxl_context *ctx,
				  u64 cmd, u64 pe_state)
{
	u64 state;

	WARN_ON(!ctx->afu->enabled);

	ctx->elem->software_state = cpu_to_be32(pe_state);
	smp_wmb();
	*(ctx->afu->sw_command_status) = cpu_to_be64(cmd | 0 | ctx->pe);
	smp_mb();
	cxl_p1n_write(ctx->afu, CXL_PSL_LLCMD_An, cmd | ctx->pe);
	while (1) {
		state = be64_to_cpup(ctx->afu->sw_command_status);
		if (state == ~0ULL) {
			pr_err("cxl: Error adding process element to AFU\n");
			return -1;
		}
		if ((state & (CXL_SPA_SW_CMD_MASK | CXL_SPA_SW_STATE_MASK  | CXL_SPA_SW_LINK_MASK)) ==
		    (cmd | (cmd >> 16) | ctx->pe))
			break;
		/*
		 * The command won't finish in the PSL if there are
		 * outstanding DSIs.  Hence we need to yield here in
		 * case there are outstanding DSIs that we need to
		 * service.  Tuning possiblity: we could wait for a
		 * while before sched
		 */
		schedule();

	}
	return 0;
}

static int add_process_element(struct cxl_context *ctx)
{
	int rc = 0;

	mutex_lock(&ctx->afu->spa_mutex);
	pr_devel("%s Adding pe: %i started\n", __func__, ctx->pe);
	if (!(rc = do_process_element_cmd(ctx, CXL_SPA_SW_CMD_ADD, CXL_PE_SOFTWARE_STATE_V)))
		ctx->pe_inserted = true;
	pr_devel("%s Adding pe: %i finished\n", __func__, ctx->pe);
	mutex_unlock(&ctx->afu->spa_mutex);
	return rc;
}

static int terminate_process_element(struct cxl_context *ctx)
{
	int rc = 0;

	/* fast path terminate if it's already invalid */
	if (!(ctx->elem->software_state & cpu_to_be32(CXL_PE_SOFTWARE_STATE_V)))
		return rc;

	mutex_lock(&ctx->afu->spa_mutex);
	pr_devel("%s Terminate pe: %i started\n", __func__, ctx->pe);
	rc = do_process_element_cmd(ctx, CXL_SPA_SW_CMD_TERMINATE,
				    CXL_PE_SOFTWARE_STATE_V | CXL_PE_SOFTWARE_STATE_T);
	ctx->elem->software_state = 0;	/* Remove Valid bit */
	pr_devel("%s Terminate pe: %i finished\n", __func__, ctx->pe);
	mutex_unlock(&ctx->afu->spa_mutex);
	return rc;
}

static int remove_process_element(struct cxl_context *ctx)
{
	int rc = 0;

	mutex_lock(&ctx->afu->spa_mutex);
	pr_devel("%s Remove pe: %i started\n", __func__, ctx->pe);
	if (!(rc = do_process_element_cmd(ctx, CXL_SPA_SW_CMD_REMOVE, 0)))
		ctx->pe_inserted = false;
	slb_invalid(ctx);
	pr_devel("%s Remove pe: %i finished\n", __func__, ctx->pe);
	mutex_unlock(&ctx->afu->spa_mutex);

	return rc;
}


static void assign_psn_space(struct cxl_context *ctx)
{
	if (!ctx->afu->pp_size || ctx->master) {
		ctx->psn_phys = ctx->afu->psn_phys;
		ctx->psn_size = ctx->afu->adapter->ps_size;
	} else {
		ctx->psn_phys = ctx->afu->psn_phys +
			(ctx->afu->pp_offset + ctx->afu->pp_size * ctx->pe);
		ctx->psn_size = ctx->afu->pp_size;
	}
}

static int activate_afu_directed(struct cxl_afu *afu)
{
	int rc;

	dev_info(&afu->dev, "Activating AFU directed mode\n");

	if (alloc_spa(afu))
		return -ENOMEM;

	cxl_p1n_write(afu, CXL_PSL_SCNTL_An, CXL_PSL_SCNTL_An_PM_AFU);
	cxl_p1n_write(afu, CXL_PSL_AMOR_An, 0xFFFFFFFFFFFFFFFFULL);
	cxl_p1n_write(afu, CXL_PSL_ID_An, CXL_PSL_ID_An_F | CXL_PSL_ID_An_L);

	afu->current_mode = CXL_MODE_DIRECTED;
	afu->num_procs = afu->max_procs_virtualised;

	if ((rc = cxl_chardev_m_afu_add(afu)))
		return rc;

	if ((rc = cxl_sysfs_afu_m_add(afu)))
		goto err;

	if ((rc = cxl_chardev_s_afu_add(afu)))
		goto err1;

	return 0;
err1:
	cxl_sysfs_afu_m_remove(afu);
err:
	cxl_chardev_afu_remove(afu);
	return rc;
}

#ifdef CONFIG_CPU_LITTLE_ENDIAN
#define set_endian(sr) ((sr) |= CXL_PSL_SR_An_LE)
#else
#define set_endian(sr) ((sr) &= ~(CXL_PSL_SR_An_LE))
#endif

static int attach_afu_directed(struct cxl_context *ctx, u64 wed, u64 amr)
{
	u64 sr;
	int r, result;

	assign_psn_space(ctx);

	ctx->elem->ctxtime = 0; /* disable */
	ctx->elem->lpid = cpu_to_be32(mfspr(SPRN_LPID));
	ctx->elem->haurp = 0; /* disable */
	ctx->elem->sdr = cpu_to_be64(mfspr(SPRN_SDR1));

	sr = CXL_PSL_SR_An_SC;
	if (ctx->master)
		sr |= CXL_PSL_SR_An_MP;
	if (mfspr(SPRN_LPCR) & LPCR_TC)
		sr |= CXL_PSL_SR_An_TC;
	/* HV=0, PR=1, R=1 for userspace
	 * For kernel contexts: this would need to change
	 */
	sr |= CXL_PSL_SR_An_PR | CXL_PSL_SR_An_R;
	set_endian(sr);
	sr &= ~(CXL_PSL_SR_An_HV);
	if (!test_tsk_thread_flag(current, TIF_32BIT))
		sr |= CXL_PSL_SR_An_SF;
	ctx->elem->common.pid = cpu_to_be32(current->pid);
	ctx->elem->common.tid = 0;
	ctx->elem->sr = cpu_to_be64(sr);

	ctx->elem->common.csrp = 0; /* disable */
	ctx->elem->common.aurp0 = 0; /* disable */
	ctx->elem->common.aurp1 = 0; /* disable */

	cxl_prefault(ctx, wed);

	ctx->elem->common.sstp0 = cpu_to_be64(ctx->sstp0);
	ctx->elem->common.sstp1 = cpu_to_be64(ctx->sstp1);

	for (r = 0; r < CXL_IRQ_RANGES; r++) {
		ctx->elem->ivte_offsets[r] = cpu_to_be16(ctx->irqs.offset[r]);
		ctx->elem->ivte_ranges[r] = cpu_to_be16(ctx->irqs.range[r]);
	}

	ctx->elem->common.amr = cpu_to_be64(amr);
	ctx->elem->common.wed = cpu_to_be64(wed);

	/* first guy needs to enable */
	if ((result = afu_check_and_enable(ctx->afu)))
		return result;

	add_process_element(ctx);

	return 0;
}

static int deactivate_afu_directed(struct cxl_afu *afu)
{
	dev_info(&afu->dev, "Deactivating AFU directed mode\n");

	afu->current_mode = 0;
	afu->num_procs = 0;

	cxl_sysfs_afu_m_remove(afu);
	cxl_chardev_afu_remove(afu);

	cxl_afu_reset(afu);
	cxl_afu_disable(afu);
	cxl_psl_purge(afu);

	release_spa(afu);

	return 0;
}

static int activate_dedicated_process(struct cxl_afu *afu)
{
	dev_info(&afu->dev, "Activating dedicated process mode\n");

	cxl_p1n_write(afu, CXL_PSL_SCNTL_An, CXL_PSL_SCNTL_An_PM_Process);

	cxl_p1n_write(afu, CXL_PSL_CtxTime_An, 0); /* disable */
	cxl_p1n_write(afu, CXL_PSL_SPAP_An, 0);    /* disable */
	cxl_p1n_write(afu, CXL_PSL_AMOR_An, 0xFFFFFFFFFFFFFFFFULL);
	cxl_p1n_write(afu, CXL_PSL_LPID_An, mfspr(SPRN_LPID));
	cxl_p1n_write(afu, CXL_HAURP_An, 0);       /* disable */
	cxl_p1n_write(afu, CXL_PSL_SDR_An, mfspr(SPRN_SDR1));

	cxl_p2n_write(afu, CXL_CSRP_An, 0);        /* disable */
	cxl_p2n_write(afu, CXL_AURP0_An, 0);       /* disable */
	cxl_p2n_write(afu, CXL_AURP1_An, 0);       /* disable */

	afu->current_mode = CXL_MODE_DEDICATED;
	afu->num_procs = 1;

	return cxl_chardev_d_afu_add(afu);
}

static int attach_dedicated(struct cxl_context *ctx, u64 wed, u64 amr)
{
	struct cxl_afu *afu = ctx->afu;
	u64 sr;
	int rc;

	sr = CXL_PSL_SR_An_SC;
	set_endian(sr);
	if (ctx->master)
		sr |= CXL_PSL_SR_An_MP;
	if (mfspr(SPRN_LPCR) & LPCR_TC)
		sr |= CXL_PSL_SR_An_TC;
	sr |= CXL_PSL_SR_An_PR | CXL_PSL_SR_An_R;
	if (!test_tsk_thread_flag(current, TIF_32BIT))
		sr |= CXL_PSL_SR_An_SF;
	cxl_p2n_write(afu, CXL_PSL_PID_TID_An, (u64)current->pid << 32);
	cxl_p1n_write(afu, CXL_PSL_SR_An, sr);

	if ((rc = cxl_write_sstp(afu, ctx->sstp0, ctx->sstp1)))
		return rc;

	cxl_prefault(ctx, wed);

	cxl_p1n_write(afu, CXL_PSL_IVTE_Offset_An,
		       (((u64)ctx->irqs.offset[0] & 0xffff) << 48) |
		       (((u64)ctx->irqs.offset[1] & 0xffff) << 32) |
		       (((u64)ctx->irqs.offset[2] & 0xffff) << 16) |
			((u64)ctx->irqs.offset[3] & 0xffff));
	cxl_p1n_write(afu, CXL_PSL_IVTE_Limit_An, (u64)
		       (((u64)ctx->irqs.range[0] & 0xffff) << 48) |
		       (((u64)ctx->irqs.range[1] & 0xffff) << 32) |
		       (((u64)ctx->irqs.range[2] & 0xffff) << 16) |
			((u64)ctx->irqs.range[3] & 0xffff));

	cxl_p2n_write(afu, CXL_PSL_AMR_An, amr);

	/* master only context for dedicated */
	assign_psn_space(ctx);

	if ((rc = cxl_afu_reset(afu)))
		return rc;

	cxl_p2n_write(afu, CXL_PSL_WED_An, wed);

	return afu_enable(afu);
}

static int deactivate_dedicated_process(struct cxl_afu *afu)
{
	dev_info(&afu->dev, "Deactivating dedicated process mode\n");

	afu->current_mode = 0;
	afu->num_procs = 0;

	cxl_chardev_afu_remove(afu);

	return 0;
}

int _cxl_afu_deactivate_mode(struct cxl_afu *afu, int mode)
{
	if (mode == CXL_MODE_DIRECTED)
		return deactivate_afu_directed(afu);
	if (mode == CXL_MODE_DEDICATED)
		return deactivate_dedicated_process(afu);
	return 0;
}

int cxl_afu_deactivate_mode(struct cxl_afu *afu)
{
	return _cxl_afu_deactivate_mode(afu, afu->current_mode);
}

int cxl_afu_activate_mode(struct cxl_afu *afu, int mode)
{
	if (!mode)
		return 0;
	if (!(mode & afu->modes_supported))
		return -EINVAL;

	if (mode == CXL_MODE_DIRECTED)
		return activate_afu_directed(afu);
	if (mode == CXL_MODE_DEDICATED)
		return activate_dedicated_process(afu);

	return -EINVAL;
}

int cxl_attach_process(struct cxl_context *ctx, bool kernel, u64 wed, u64 amr)
{
	ctx->kernel = kernel;
	if (ctx->afu->current_mode == CXL_MODE_DIRECTED)
		return attach_afu_directed(ctx, wed, amr);

	if (ctx->afu->current_mode == CXL_MODE_DEDICATED)
		return attach_dedicated(ctx, wed, amr);

	return -EINVAL;
}

static inline int detach_process_native_dedicated(struct cxl_context *ctx)
{
	cxl_afu_reset(ctx->afu);
	cxl_afu_disable(ctx->afu);
	cxl_psl_purge(ctx->afu);
	return 0;
}

/*
 * TODO: handle case when this is called inside a rcu_read_lock() which may
 * happen when we unbind the driver (ie. cxl_context_detach_all()) .  Terminate
 * & remove use a mutex lock and schedule which will not good with lock held.
 * May need to write do_process_element_cmd() that handles outstanding page
 * faults synchronously.
 */
static inline int detach_process_native_afu_directed(struct cxl_context *ctx)
{
	if (!ctx->pe_inserted)
		return 0;
	if (terminate_process_element(ctx))
		return -1;
	if (remove_process_element(ctx))
		return -1;

	return 0;
}

int cxl_detach_process(struct cxl_context *ctx)
{
	if (ctx->afu->current_mode == CXL_MODE_DEDICATED)
		return detach_process_native_dedicated(ctx);

	return detach_process_native_afu_directed(ctx);
}

int cxl_get_irq(struct cxl_context *ctx, struct cxl_irq_info *info)
{
	u64 pidtid;

	info->dsisr = cxl_p2n_read(ctx->afu, CXL_PSL_DSISR_An);
	info->dar = cxl_p2n_read(ctx->afu, CXL_PSL_DAR_An);
	info->dsr = cxl_p2n_read(ctx->afu, CXL_PSL_DSR_An);
	pidtid = cxl_p2n_read(ctx->afu, CXL_PSL_PID_TID_An);
	info->pid = pidtid >> 32;
	info->tid = pidtid & 0xffffffff;
	info->afu_err = cxl_p2n_read(ctx->afu, CXL_AFU_ERR_An);
	info->errstat = cxl_p2n_read(ctx->afu, CXL_PSL_ErrStat_An);

	return 0;
}

static void recover_psl_err(struct cxl_afu *afu, u64 errstat)
{
	u64 dsisr;

	pr_devel("RECOVERING FROM PSL ERROR... (0x%.16llx)\n", errstat);

	/* Clear PSL_DSISR[PE] */
	dsisr = cxl_p2n_read(afu, CXL_PSL_DSISR_An);
	cxl_p2n_write(afu, CXL_PSL_DSISR_An, dsisr & ~CXL_PSL_DSISR_An_PE);

	/* Write 1s to clear error status bits */
	cxl_p2n_write(afu, CXL_PSL_ErrStat_An, errstat);
}

int cxl_ack_irq(struct cxl_context *ctx, u64 tfc, u64 psl_reset_mask)
{
	if (tfc)
		cxl_p2n_write(ctx->afu, CXL_PSL_TFC_An, tfc);
	if (psl_reset_mask)
		recover_psl_err(ctx->afu, psl_reset_mask);

	return 0;
}

int cxl_check_error(struct cxl_afu *afu)
{
	return (cxl_p1n_read(afu, CXL_PSL_SCNTL_An) == ~0ULL);
}
