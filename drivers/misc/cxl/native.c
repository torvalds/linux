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
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <asm/synch.h>
#include <misc/cxl-base.h>

#include "cxl.h"
#include "trace.h"

static int afu_control(struct cxl_afu *afu, u64 command, u64 clear,
		       u64 result, u64 mask, bool enabled)
{
	u64 AFU_Cntl;
	unsigned long timeout = jiffies + (HZ * CXL_TIMEOUT);
	int rc = 0;

	spin_lock(&afu->afu_cntl_lock);
	pr_devel("AFU command starting: %llx\n", command);

	trace_cxl_afu_ctrl(afu, command);

	AFU_Cntl = cxl_p2n_read(afu, CXL_AFU_Cntl_An);
	cxl_p2n_write(afu, CXL_AFU_Cntl_An, (AFU_Cntl & ~clear) | command);

	AFU_Cntl = cxl_p2n_read(afu, CXL_AFU_Cntl_An);
	while ((AFU_Cntl & mask) != result) {
		if (time_after_eq(jiffies, timeout)) {
			dev_warn(&afu->dev, "WARNING: AFU control timed out!\n");
			rc = -EBUSY;
			goto out;
		}

		if (!cxl_ops->link_ok(afu->adapter, afu)) {
			afu->enabled = enabled;
			rc = -EIO;
			goto out;
		}

		pr_devel_ratelimited("AFU control... (0x%016llx)\n",
				     AFU_Cntl | command);
		cpu_relax();
		AFU_Cntl = cxl_p2n_read(afu, CXL_AFU_Cntl_An);
	}

	if (AFU_Cntl & CXL_AFU_Cntl_An_RA) {
		/*
		 * Workaround for a bug in the XSL used in the Mellanox CX4
		 * that fails to clear the RA bit after an AFU reset,
		 * preventing subsequent AFU resets from working.
		 */
		cxl_p2n_write(afu, CXL_AFU_Cntl_An, AFU_Cntl & ~CXL_AFU_Cntl_An_RA);
	}

	pr_devel("AFU command complete: %llx\n", command);
	afu->enabled = enabled;
out:
	trace_cxl_afu_ctrl_done(afu, command, rc);
	spin_unlock(&afu->afu_cntl_lock);

	return rc;
}

static int afu_enable(struct cxl_afu *afu)
{
	pr_devel("AFU enable request\n");

	return afu_control(afu, CXL_AFU_Cntl_An_E, 0,
			   CXL_AFU_Cntl_An_ES_Enabled,
			   CXL_AFU_Cntl_An_ES_MASK, true);
}

int cxl_afu_disable(struct cxl_afu *afu)
{
	pr_devel("AFU disable request\n");

	return afu_control(afu, 0, CXL_AFU_Cntl_An_E,
			   CXL_AFU_Cntl_An_ES_Disabled,
			   CXL_AFU_Cntl_An_ES_MASK, false);
}

/* This will disable as well as reset */
static int native_afu_reset(struct cxl_afu *afu)
{
	int rc;
	u64 serr;

	pr_devel("AFU reset request\n");

	rc = afu_control(afu, CXL_AFU_Cntl_An_RA, 0,
			   CXL_AFU_Cntl_An_RS_Complete | CXL_AFU_Cntl_An_ES_Disabled,
			   CXL_AFU_Cntl_An_RS_MASK | CXL_AFU_Cntl_An_ES_MASK,
			   false);

	/* Re-enable any masked interrupts */
	serr = cxl_p1n_read(afu, CXL_PSL_SERR_An);
	serr &= ~CXL_PSL_SERR_An_IRQ_MASKS;
	cxl_p1n_write(afu, CXL_PSL_SERR_An, serr);


	return rc;
}

static int native_afu_check_and_enable(struct cxl_afu *afu)
{
	if (!cxl_ops->link_ok(afu->adapter, afu)) {
		WARN(1, "Refusing to enable afu while link down!\n");
		return -EIO;
	}
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
	u64 trans_fault = 0x0ULL;
	unsigned long timeout = jiffies + (HZ * CXL_TIMEOUT);
	int rc = 0;

	trace_cxl_psl_ctrl(afu, CXL_PSL_SCNTL_An_Pc);

	pr_devel("PSL purge request\n");

	if (cxl_is_psl8(afu))
		trans_fault = CXL_PSL_DSISR_TRANS;
	if (cxl_is_psl9(afu))
		trans_fault = CXL_PSL9_DSISR_An_TF;

	if (!cxl_ops->link_ok(afu->adapter, afu)) {
		dev_warn(&afu->dev, "PSL Purge called with link down, ignoring\n");
		rc = -EIO;
		goto out;
	}

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
			rc = -EBUSY;
			goto out;
		}
		if (!cxl_ops->link_ok(afu->adapter, afu)) {
			rc = -EIO;
			goto out;
		}

		dsisr = cxl_p2n_read(afu, CXL_PSL_DSISR_An);
		pr_devel_ratelimited("PSL purging... PSL_CNTL: 0x%016llx  PSL_DSISR: 0x%016llx\n",
				     PSL_CNTL, dsisr);

		if (dsisr & trans_fault) {
			dar = cxl_p2n_read(afu, CXL_PSL_DAR_An);
			dev_notice(&afu->dev, "PSL purge terminating pending translation, DSISR: 0x%016llx, DAR: 0x%016llx\n",
				   dsisr, dar);
			cxl_p2n_write(afu, CXL_PSL_TFC_An, CXL_PSL_TFC_An_AE);
		} else if (dsisr) {
			dev_notice(&afu->dev, "PSL purge acknowledging pending non-translation fault, DSISR: 0x%016llx\n",
				   dsisr);
			cxl_p2n_write(afu, CXL_PSL_TFC_An, CXL_PSL_TFC_An_A);
		} else {
			cpu_relax();
		}
		PSL_CNTL = cxl_p1n_read(afu, CXL_PSL_SCNTL_An);
	}
	end = local_clock();
	pr_devel("PSL purged in %lld ns\n", end - start);

	cxl_p1n_write(afu, CXL_PSL_SCNTL_An,
		       PSL_CNTL & ~CXL_PSL_SCNTL_An_Pc);
out:
	trace_cxl_psl_ctrl_done(afu, CXL_PSL_SCNTL_An_Pc, rc);
	return rc;
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

static int cxl_alloc_spa(struct cxl_afu *afu, int mode)
{
	unsigned spa_size;

	/* Work out how many pages to allocate */
	afu->native->spa_order = -1;
	do {
		afu->native->spa_order++;
		spa_size = (1 << afu->native->spa_order) * PAGE_SIZE;

		if (spa_size > 0x100000) {
			dev_warn(&afu->dev, "num_of_processes too large for the SPA, limiting to %i (0x%x)\n",
					afu->native->spa_max_procs, afu->native->spa_size);
			if (mode != CXL_MODE_DEDICATED)
				afu->num_procs = afu->native->spa_max_procs;
			break;
		}

		afu->native->spa_size = spa_size;
		afu->native->spa_max_procs = spa_max_procs(afu->native->spa_size);
	} while (afu->native->spa_max_procs < afu->num_procs);

	if (!(afu->native->spa = (struct cxl_process_element *)
	      __get_free_pages(GFP_KERNEL | __GFP_ZERO, afu->native->spa_order))) {
		pr_err("cxl_alloc_spa: Unable to allocate scheduled process area\n");
		return -ENOMEM;
	}
	pr_devel("spa pages: %i afu->spa_max_procs: %i   afu->num_procs: %i\n",
		 1<<afu->native->spa_order, afu->native->spa_max_procs, afu->num_procs);

	return 0;
}

static void attach_spa(struct cxl_afu *afu)
{
	u64 spap;

	afu->native->sw_command_status = (__be64 *)((char *)afu->native->spa +
					    ((afu->native->spa_max_procs + 3) * 128));

	spap = virt_to_phys(afu->native->spa) & CXL_PSL_SPAP_Addr;
	spap |= ((afu->native->spa_size >> (12 - CXL_PSL_SPAP_Size_Shift)) - 1) & CXL_PSL_SPAP_Size;
	spap |= CXL_PSL_SPAP_V;
	pr_devel("cxl: SPA allocated at 0x%p. Max processes: %i, sw_command_status: 0x%p CXL_PSL_SPAP_An=0x%016llx\n",
		afu->native->spa, afu->native->spa_max_procs,
		afu->native->sw_command_status, spap);
	cxl_p1n_write(afu, CXL_PSL_SPAP_An, spap);
}

static inline void detach_spa(struct cxl_afu *afu)
{
	cxl_p1n_write(afu, CXL_PSL_SPAP_An, 0);
}

void cxl_release_spa(struct cxl_afu *afu)
{
	if (afu->native->spa) {
		free_pages((unsigned long) afu->native->spa,
			afu->native->spa_order);
		afu->native->spa = NULL;
	}
}

/*
 * Invalidation of all ERAT entries is no longer required by CAIA2. Use
 * only for debug.
 */
int cxl_invalidate_all_psl9(struct cxl *adapter)
{
	unsigned long timeout = jiffies + (HZ * CXL_TIMEOUT);
	u64 ierat;

	pr_devel("CXL adapter - invalidation of all ERAT entries\n");

	/* Invalidates all ERAT entries for Radix or HPT */
	ierat = CXL_XSL9_IERAT_IALL;
	if (radix_enabled())
		ierat |= CXL_XSL9_IERAT_INVR;
	cxl_p1_write(adapter, CXL_XSL9_IERAT, ierat);

	while (cxl_p1_read(adapter, CXL_XSL9_IERAT) & CXL_XSL9_IERAT_IINPROG) {
		if (time_after_eq(jiffies, timeout)) {
			dev_warn(&adapter->dev,
			"WARNING: CXL adapter invalidation of all ERAT entries timed out!\n");
			return -EBUSY;
		}
		if (!cxl_ops->link_ok(adapter, NULL))
			return -EIO;
		cpu_relax();
	}
	return 0;
}

int cxl_invalidate_all_psl8(struct cxl *adapter)
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
		if (!cxl_ops->link_ok(adapter, NULL))
			return -EIO;
		cpu_relax();
	}

	cxl_p1_write(adapter, CXL_PSL_SLBIA, CXL_TLB_SLB_IQ_ALL);
	while (cxl_p1_read(adapter, CXL_PSL_SLBIA) & CXL_TLB_SLB_P) {
		if (time_after_eq(jiffies, timeout)) {
			dev_warn(&adapter->dev, "WARNING: CXL adapter wide SLBIA timed out!\n");
			return -EBUSY;
		}
		if (!cxl_ops->link_ok(adapter, NULL))
			return -EIO;
		cpu_relax();
	}
	return 0;
}

int cxl_data_cache_flush(struct cxl *adapter)
{
	u64 reg;
	unsigned long timeout = jiffies + (HZ * CXL_TIMEOUT);

	pr_devel("Flushing data cache\n");

	reg = cxl_p1_read(adapter, CXL_PSL_Control);
	reg |= CXL_PSL_Control_Fr;
	cxl_p1_write(adapter, CXL_PSL_Control, reg);

	reg = cxl_p1_read(adapter, CXL_PSL_Control);
	while ((reg & CXL_PSL_Control_Fs_MASK) != CXL_PSL_Control_Fs_Complete) {
		if (time_after_eq(jiffies, timeout)) {
			dev_warn(&adapter->dev, "WARNING: cache flush timed out!\n");
			return -EBUSY;
		}

		if (!cxl_ops->link_ok(adapter, NULL)) {
			dev_warn(&adapter->dev, "WARNING: link down when flushing cache\n");
			return -EIO;
		}
		cpu_relax();
		reg = cxl_p1_read(adapter, CXL_PSL_Control);
	}

	reg &= ~CXL_PSL_Control_Fr;
	cxl_p1_write(adapter, CXL_PSL_Control, reg);
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

	WARN_ON(!mutex_is_locked(&ctx->afu->native->spa_mutex));

	cxl_p1_write(adapter, CXL_PSL_LBISEL,
			((u64)be32_to_cpu(ctx->elem->common.pid) << 32) |
			be32_to_cpu(ctx->elem->lpid));
	cxl_p1_write(adapter, CXL_PSL_SLBIA, CXL_TLB_SLB_IQ_LPIDPID);

	while (1) {
		if (!cxl_ops->link_ok(adapter, NULL))
			break;
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
	unsigned long timeout = jiffies + (HZ * CXL_TIMEOUT);
	int rc = 0;

	trace_cxl_llcmd(ctx, cmd);

	WARN_ON(!ctx->afu->enabled);

	ctx->elem->software_state = cpu_to_be32(pe_state);
	smp_wmb();
	*(ctx->afu->native->sw_command_status) = cpu_to_be64(cmd | 0 | ctx->pe);
	smp_mb();
	cxl_p1n_write(ctx->afu, CXL_PSL_LLCMD_An, cmd | ctx->pe);
	while (1) {
		if (time_after_eq(jiffies, timeout)) {
			dev_warn(&ctx->afu->dev, "WARNING: Process Element Command timed out!\n");
			rc = -EBUSY;
			goto out;
		}
		if (!cxl_ops->link_ok(ctx->afu->adapter, ctx->afu)) {
			dev_warn(&ctx->afu->dev, "WARNING: Device link down, aborting Process Element Command!\n");
			rc = -EIO;
			goto out;
		}
		state = be64_to_cpup(ctx->afu->native->sw_command_status);
		if (state == ~0ULL) {
			pr_err("cxl: Error adding process element to AFU\n");
			rc = -1;
			goto out;
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
out:
	trace_cxl_llcmd_done(ctx, cmd, rc);
	return rc;
}

static int add_process_element(struct cxl_context *ctx)
{
	int rc = 0;

	mutex_lock(&ctx->afu->native->spa_mutex);
	pr_devel("%s Adding pe: %i started\n", __func__, ctx->pe);
	if (!(rc = do_process_element_cmd(ctx, CXL_SPA_SW_CMD_ADD, CXL_PE_SOFTWARE_STATE_V)))
		ctx->pe_inserted = true;
	pr_devel("%s Adding pe: %i finished\n", __func__, ctx->pe);
	mutex_unlock(&ctx->afu->native->spa_mutex);
	return rc;
}

static int terminate_process_element(struct cxl_context *ctx)
{
	int rc = 0;

	/* fast path terminate if it's already invalid */
	if (!(ctx->elem->software_state & cpu_to_be32(CXL_PE_SOFTWARE_STATE_V)))
		return rc;

	mutex_lock(&ctx->afu->native->spa_mutex);
	pr_devel("%s Terminate pe: %i started\n", __func__, ctx->pe);
	/* We could be asked to terminate when the hw is down. That
	 * should always succeed: it's not running if the hw has gone
	 * away and is being reset.
	 */
	if (cxl_ops->link_ok(ctx->afu->adapter, ctx->afu))
		rc = do_process_element_cmd(ctx, CXL_SPA_SW_CMD_TERMINATE,
					    CXL_PE_SOFTWARE_STATE_V | CXL_PE_SOFTWARE_STATE_T);
	ctx->elem->software_state = 0;	/* Remove Valid bit */
	pr_devel("%s Terminate pe: %i finished\n", __func__, ctx->pe);
	mutex_unlock(&ctx->afu->native->spa_mutex);
	return rc;
}

static int remove_process_element(struct cxl_context *ctx)
{
	int rc = 0;

	mutex_lock(&ctx->afu->native->spa_mutex);
	pr_devel("%s Remove pe: %i started\n", __func__, ctx->pe);

	/* We could be asked to remove when the hw is down. Again, if
	 * the hw is down, the PE is gone, so we succeed.
	 */
	if (cxl_ops->link_ok(ctx->afu->adapter, ctx->afu))
		rc = do_process_element_cmd(ctx, CXL_SPA_SW_CMD_REMOVE, 0);

	if (!rc)
		ctx->pe_inserted = false;
	if (cxl_is_power8())
		slb_invalid(ctx);
	pr_devel("%s Remove pe: %i finished\n", __func__, ctx->pe);
	mutex_unlock(&ctx->afu->native->spa_mutex);

	return rc;
}

void cxl_assign_psn_space(struct cxl_context *ctx)
{
	if (!ctx->afu->pp_size || ctx->master) {
		ctx->psn_phys = ctx->afu->psn_phys;
		ctx->psn_size = ctx->afu->adapter->ps_size;
	} else {
		ctx->psn_phys = ctx->afu->psn_phys +
			(ctx->afu->native->pp_offset + ctx->afu->pp_size * ctx->pe);
		ctx->psn_size = ctx->afu->pp_size;
	}
}

static int activate_afu_directed(struct cxl_afu *afu)
{
	int rc;

	dev_info(&afu->dev, "Activating AFU directed mode\n");

	afu->num_procs = afu->max_procs_virtualised;
	if (afu->native->spa == NULL) {
		if (cxl_alloc_spa(afu, CXL_MODE_DIRECTED))
			return -ENOMEM;
	}
	attach_spa(afu);

	cxl_p1n_write(afu, CXL_PSL_SCNTL_An, CXL_PSL_SCNTL_An_PM_AFU);
	if (cxl_is_power8())
		cxl_p1n_write(afu, CXL_PSL_AMOR_An, 0xFFFFFFFFFFFFFFFFULL);
	cxl_p1n_write(afu, CXL_PSL_ID_An, CXL_PSL_ID_An_F | CXL_PSL_ID_An_L);

	afu->current_mode = CXL_MODE_DIRECTED;

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

static u64 calculate_sr(struct cxl_context *ctx)
{
	u64 sr = 0;

	set_endian(sr);
	if (ctx->master)
		sr |= CXL_PSL_SR_An_MP;
	if (mfspr(SPRN_LPCR) & LPCR_TC)
		sr |= CXL_PSL_SR_An_TC;
	if (ctx->kernel) {
		if (!ctx->real_mode)
			sr |= CXL_PSL_SR_An_R;
		sr |= (mfmsr() & MSR_SF) | CXL_PSL_SR_An_HV;
	} else {
		sr |= CXL_PSL_SR_An_PR | CXL_PSL_SR_An_R;
		if (radix_enabled())
			sr |= CXL_PSL_SR_An_HV;
		else
			sr &= ~(CXL_PSL_SR_An_HV);
		if (!test_tsk_thread_flag(current, TIF_32BIT))
			sr |= CXL_PSL_SR_An_SF;
	}
	if (cxl_is_psl9(ctx->afu)) {
		if (radix_enabled())
			sr |= CXL_PSL_SR_An_XLAT_ror;
		else
			sr |= CXL_PSL_SR_An_XLAT_hpt;
	}
	return sr;
}

static void update_ivtes_directed(struct cxl_context *ctx)
{
	bool need_update = (ctx->status == STARTED);
	int r;

	if (need_update) {
		WARN_ON(terminate_process_element(ctx));
		WARN_ON(remove_process_element(ctx));
	}

	for (r = 0; r < CXL_IRQ_RANGES; r++) {
		ctx->elem->ivte_offsets[r] = cpu_to_be16(ctx->irqs.offset[r]);
		ctx->elem->ivte_ranges[r] = cpu_to_be16(ctx->irqs.range[r]);
	}

	/*
	 * Theoretically we could use the update llcmd, instead of a
	 * terminate/remove/add (or if an atomic update was required we could
	 * do a suspend/update/resume), however it seems there might be issues
	 * with the update llcmd on some cards (including those using an XSL on
	 * an ASIC) so for now it's safest to go with the commands that are
	 * known to work. In the future if we come across a situation where the
	 * card may be performing transactions using the same PE while we are
	 * doing this update we might need to revisit this.
	 */
	if (need_update)
		WARN_ON(add_process_element(ctx));
}

static int process_element_entry_psl9(struct cxl_context *ctx, u64 wed, u64 amr)
{
	u32 pid;

	cxl_assign_psn_space(ctx);

	ctx->elem->ctxtime = 0; /* disable */
	ctx->elem->lpid = cpu_to_be32(mfspr(SPRN_LPID));
	ctx->elem->haurp = 0; /* disable */

	if (ctx->kernel)
		pid = 0;
	else {
		if (ctx->mm == NULL) {
			pr_devel("%s: unable to get mm for pe=%d pid=%i\n",
				__func__, ctx->pe, pid_nr(ctx->pid));
			return -EINVAL;
		}
		pid = ctx->mm->context.id;
	}

	ctx->elem->common.tid = 0;
	ctx->elem->common.pid = cpu_to_be32(pid);

	ctx->elem->sr = cpu_to_be64(calculate_sr(ctx));

	ctx->elem->common.csrp = 0; /* disable */

	cxl_prefault(ctx, wed);

	/*
	 * Ensure we have the multiplexed PSL interrupt set up to take faults
	 * for kernel contexts that may not have allocated any AFU IRQs at all:
	 */
	if (ctx->irqs.range[0] == 0) {
		ctx->irqs.offset[0] = ctx->afu->native->psl_hwirq;
		ctx->irqs.range[0] = 1;
	}

	ctx->elem->common.amr = cpu_to_be64(amr);
	ctx->elem->common.wed = cpu_to_be64(wed);

	return 0;
}

int cxl_attach_afu_directed_psl9(struct cxl_context *ctx, u64 wed, u64 amr)
{
	int result;

	/* fill the process element entry */
	result = process_element_entry_psl9(ctx, wed, amr);
	if (result)
		return result;

	update_ivtes_directed(ctx);

	/* first guy needs to enable */
	result = cxl_ops->afu_check_and_enable(ctx->afu);
	if (result)
		return result;

	return add_process_element(ctx);
}

int cxl_attach_afu_directed_psl8(struct cxl_context *ctx, u64 wed, u64 amr)
{
	u32 pid;
	int result;

	cxl_assign_psn_space(ctx);

	ctx->elem->ctxtime = 0; /* disable */
	ctx->elem->lpid = cpu_to_be32(mfspr(SPRN_LPID));
	ctx->elem->haurp = 0; /* disable */
	ctx->elem->u.sdr = cpu_to_be64(mfspr(SPRN_SDR1));

	pid = current->pid;
	if (ctx->kernel)
		pid = 0;
	ctx->elem->common.tid = 0;
	ctx->elem->common.pid = cpu_to_be32(pid);

	ctx->elem->sr = cpu_to_be64(calculate_sr(ctx));

	ctx->elem->common.csrp = 0; /* disable */
	ctx->elem->common.u.psl8.aurp0 = 0; /* disable */
	ctx->elem->common.u.psl8.aurp1 = 0; /* disable */

	cxl_prefault(ctx, wed);

	ctx->elem->common.u.psl8.sstp0 = cpu_to_be64(ctx->sstp0);
	ctx->elem->common.u.psl8.sstp1 = cpu_to_be64(ctx->sstp1);

	/*
	 * Ensure we have the multiplexed PSL interrupt set up to take faults
	 * for kernel contexts that may not have allocated any AFU IRQs at all:
	 */
	if (ctx->irqs.range[0] == 0) {
		ctx->irqs.offset[0] = ctx->afu->native->psl_hwirq;
		ctx->irqs.range[0] = 1;
	}

	update_ivtes_directed(ctx);

	ctx->elem->common.amr = cpu_to_be64(amr);
	ctx->elem->common.wed = cpu_to_be64(wed);

	/* first guy needs to enable */
	if ((result = cxl_ops->afu_check_and_enable(ctx->afu)))
		return result;

	return add_process_element(ctx);
}

static int deactivate_afu_directed(struct cxl_afu *afu)
{
	dev_info(&afu->dev, "Deactivating AFU directed mode\n");

	afu->current_mode = 0;
	afu->num_procs = 0;

	cxl_sysfs_afu_m_remove(afu);
	cxl_chardev_afu_remove(afu);

	/*
	 * The CAIA section 2.2.1 indicates that the procedure for starting and
	 * stopping an AFU in AFU directed mode is AFU specific, which is not
	 * ideal since this code is generic and with one exception has no
	 * knowledge of the AFU. This is in contrast to the procedure for
	 * disabling a dedicated process AFU, which is documented to just
	 * require a reset. The architecture does indicate that both an AFU
	 * reset and an AFU disable should result in the AFU being disabled and
	 * we do both followed by a PSL purge for safety.
	 *
	 * Notably we used to have some issues with the disable sequence on PSL
	 * cards, which is why we ended up using this heavy weight procedure in
	 * the first place, however a bug was discovered that had rendered the
	 * disable operation ineffective, so it is conceivable that was the
	 * sole explanation for those difficulties. Careful regression testing
	 * is recommended if anyone attempts to remove or reorder these
	 * operations.
	 *
	 * The XSL on the Mellanox CX4 behaves a little differently from the
	 * PSL based cards and will time out an AFU reset if the AFU is still
	 * enabled. That card is special in that we do have a means to identify
	 * it from this code, so in that case we skip the reset and just use a
	 * disable/purge to avoid the timeout and corresponding noise in the
	 * kernel log.
	 */
	if (afu->adapter->native->sl_ops->needs_reset_before_disable)
		cxl_ops->afu_reset(afu);
	cxl_afu_disable(afu);
	cxl_psl_purge(afu);

	return 0;
}

int cxl_activate_dedicated_process_psl9(struct cxl_afu *afu)
{
	dev_info(&afu->dev, "Activating dedicated process mode\n");

	/*
	 * If XSL is set to dedicated mode (Set in PSL_SCNTL reg), the
	 * XSL and AFU are programmed to work with a single context.
	 * The context information should be configured in the SPA area
	 * index 0 (so PSL_SPAP must be configured before enabling the
	 * AFU).
	 */
	afu->num_procs = 1;
	if (afu->native->spa == NULL) {
		if (cxl_alloc_spa(afu, CXL_MODE_DEDICATED))
			return -ENOMEM;
	}
	attach_spa(afu);

	cxl_p1n_write(afu, CXL_PSL_SCNTL_An, CXL_PSL_SCNTL_An_PM_Process);
	cxl_p1n_write(afu, CXL_PSL_ID_An, CXL_PSL_ID_An_F | CXL_PSL_ID_An_L);

	afu->current_mode = CXL_MODE_DEDICATED;

	return cxl_chardev_d_afu_add(afu);
}

int cxl_activate_dedicated_process_psl8(struct cxl_afu *afu)
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

void cxl_update_dedicated_ivtes_psl9(struct cxl_context *ctx)
{
	int r;

	for (r = 0; r < CXL_IRQ_RANGES; r++) {
		ctx->elem->ivte_offsets[r] = cpu_to_be16(ctx->irqs.offset[r]);
		ctx->elem->ivte_ranges[r] = cpu_to_be16(ctx->irqs.range[r]);
	}
}

void cxl_update_dedicated_ivtes_psl8(struct cxl_context *ctx)
{
	struct cxl_afu *afu = ctx->afu;

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
}

int cxl_attach_dedicated_process_psl9(struct cxl_context *ctx, u64 wed, u64 amr)
{
	struct cxl_afu *afu = ctx->afu;
	int result;

	/* fill the process element entry */
	result = process_element_entry_psl9(ctx, wed, amr);
	if (result)
		return result;

	if (ctx->afu->adapter->native->sl_ops->update_dedicated_ivtes)
		afu->adapter->native->sl_ops->update_dedicated_ivtes(ctx);

	result = cxl_ops->afu_reset(afu);
	if (result)
		return result;

	return afu_enable(afu);
}

int cxl_attach_dedicated_process_psl8(struct cxl_context *ctx, u64 wed, u64 amr)
{
	struct cxl_afu *afu = ctx->afu;
	u64 pid;
	int rc;

	pid = (u64)current->pid << 32;
	if (ctx->kernel)
		pid = 0;
	cxl_p2n_write(afu, CXL_PSL_PID_TID_An, pid);

	cxl_p1n_write(afu, CXL_PSL_SR_An, calculate_sr(ctx));

	if ((rc = cxl_write_sstp(afu, ctx->sstp0, ctx->sstp1)))
		return rc;

	cxl_prefault(ctx, wed);

	if (ctx->afu->adapter->native->sl_ops->update_dedicated_ivtes)
		afu->adapter->native->sl_ops->update_dedicated_ivtes(ctx);

	cxl_p2n_write(afu, CXL_PSL_AMR_An, amr);

	/* master only context for dedicated */
	cxl_assign_psn_space(ctx);

	if ((rc = cxl_ops->afu_reset(afu)))
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

static int native_afu_deactivate_mode(struct cxl_afu *afu, int mode)
{
	if (mode == CXL_MODE_DIRECTED)
		return deactivate_afu_directed(afu);
	if (mode == CXL_MODE_DEDICATED)
		return deactivate_dedicated_process(afu);
	return 0;
}

static int native_afu_activate_mode(struct cxl_afu *afu, int mode)
{
	if (!mode)
		return 0;
	if (!(mode & afu->modes_supported))
		return -EINVAL;

	if (!cxl_ops->link_ok(afu->adapter, afu)) {
		WARN(1, "Device link is down, refusing to activate!\n");
		return -EIO;
	}

	if (mode == CXL_MODE_DIRECTED)
		return activate_afu_directed(afu);
	if ((mode == CXL_MODE_DEDICATED) &&
	    (afu->adapter->native->sl_ops->activate_dedicated_process))
		return afu->adapter->native->sl_ops->activate_dedicated_process(afu);

	return -EINVAL;
}

static int native_attach_process(struct cxl_context *ctx, bool kernel,
				u64 wed, u64 amr)
{
	if (!cxl_ops->link_ok(ctx->afu->adapter, ctx->afu)) {
		WARN(1, "Device link is down, refusing to attach process!\n");
		return -EIO;
	}

	ctx->kernel = kernel;
	if ((ctx->afu->current_mode == CXL_MODE_DIRECTED) &&
	    (ctx->afu->adapter->native->sl_ops->attach_afu_directed))
		return ctx->afu->adapter->native->sl_ops->attach_afu_directed(ctx, wed, amr);

	if ((ctx->afu->current_mode == CXL_MODE_DEDICATED) &&
	    (ctx->afu->adapter->native->sl_ops->attach_dedicated_process))
		return ctx->afu->adapter->native->sl_ops->attach_dedicated_process(ctx, wed, amr);

	return -EINVAL;
}

static inline int detach_process_native_dedicated(struct cxl_context *ctx)
{
	/*
	 * The CAIA section 2.1.1 indicates that we need to do an AFU reset to
	 * stop the AFU in dedicated mode (we therefore do not make that
	 * optional like we do in the afu directed path). It does not indicate
	 * that we need to do an explicit disable (which should occur
	 * implicitly as part of the reset) or purge, but we do these as well
	 * to be on the safe side.
	 *
	 * Notably we used to have some issues with the disable sequence
	 * (before the sequence was spelled out in the architecture) which is
	 * why we were so heavy weight in the first place, however a bug was
	 * discovered that had rendered the disable operation ineffective, so
	 * it is conceivable that was the sole explanation for those
	 * difficulties. Point is, we should be careful and do some regression
	 * testing if we ever attempt to remove any part of this procedure.
	 */
	cxl_ops->afu_reset(ctx->afu);
	cxl_afu_disable(ctx->afu);
	cxl_psl_purge(ctx->afu);
	return 0;
}

static void native_update_ivtes(struct cxl_context *ctx)
{
	if (ctx->afu->current_mode == CXL_MODE_DIRECTED)
		return update_ivtes_directed(ctx);
	if ((ctx->afu->current_mode == CXL_MODE_DEDICATED) &&
	    (ctx->afu->adapter->native->sl_ops->update_dedicated_ivtes))
		return ctx->afu->adapter->native->sl_ops->update_dedicated_ivtes(ctx);
	WARN(1, "native_update_ivtes: Bad mode\n");
}

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

static int native_detach_process(struct cxl_context *ctx)
{
	trace_cxl_detach(ctx);

	if (ctx->afu->current_mode == CXL_MODE_DEDICATED)
		return detach_process_native_dedicated(ctx);

	return detach_process_native_afu_directed(ctx);
}

static int native_get_irq_info(struct cxl_afu *afu, struct cxl_irq_info *info)
{
	/* If the adapter has gone away, we can't get any meaningful
	 * information.
	 */
	if (!cxl_ops->link_ok(afu->adapter, afu))
		return -EIO;

	info->dsisr = cxl_p2n_read(afu, CXL_PSL_DSISR_An);
	info->dar = cxl_p2n_read(afu, CXL_PSL_DAR_An);
	if (cxl_is_power8())
		info->dsr = cxl_p2n_read(afu, CXL_PSL_DSR_An);
	info->afu_err = cxl_p2n_read(afu, CXL_AFU_ERR_An);
	info->errstat = cxl_p2n_read(afu, CXL_PSL_ErrStat_An);
	info->proc_handle = 0;

	return 0;
}

void cxl_native_irq_dump_regs_psl9(struct cxl_context *ctx)
{
	u64 fir1, fir2, serr;

	fir1 = cxl_p1_read(ctx->afu->adapter, CXL_PSL9_FIR1);
	fir2 = cxl_p1_read(ctx->afu->adapter, CXL_PSL9_FIR2);

	dev_crit(&ctx->afu->dev, "PSL_FIR1: 0x%016llx\n", fir1);
	dev_crit(&ctx->afu->dev, "PSL_FIR2: 0x%016llx\n", fir2);
	if (ctx->afu->adapter->native->sl_ops->register_serr_irq) {
		serr = cxl_p1n_read(ctx->afu, CXL_PSL_SERR_An);
		cxl_afu_decode_psl_serr(ctx->afu, serr);
	}
}

void cxl_native_irq_dump_regs_psl8(struct cxl_context *ctx)
{
	u64 fir1, fir2, fir_slice, serr, afu_debug;

	fir1 = cxl_p1_read(ctx->afu->adapter, CXL_PSL_FIR1);
	fir2 = cxl_p1_read(ctx->afu->adapter, CXL_PSL_FIR2);
	fir_slice = cxl_p1n_read(ctx->afu, CXL_PSL_FIR_SLICE_An);
	afu_debug = cxl_p1n_read(ctx->afu, CXL_AFU_DEBUG_An);

	dev_crit(&ctx->afu->dev, "PSL_FIR1: 0x%016llx\n", fir1);
	dev_crit(&ctx->afu->dev, "PSL_FIR2: 0x%016llx\n", fir2);
	if (ctx->afu->adapter->native->sl_ops->register_serr_irq) {
		serr = cxl_p1n_read(ctx->afu, CXL_PSL_SERR_An);
		cxl_afu_decode_psl_serr(ctx->afu, serr);
	}
	dev_crit(&ctx->afu->dev, "PSL_FIR_SLICE_An: 0x%016llx\n", fir_slice);
	dev_crit(&ctx->afu->dev, "CXL_PSL_AFU_DEBUG_An: 0x%016llx\n", afu_debug);
}

static irqreturn_t native_handle_psl_slice_error(struct cxl_context *ctx,
						u64 dsisr, u64 errstat)
{

	dev_crit(&ctx->afu->dev, "PSL ERROR STATUS: 0x%016llx\n", errstat);

	if (ctx->afu->adapter->native->sl_ops->psl_irq_dump_registers)
		ctx->afu->adapter->native->sl_ops->psl_irq_dump_registers(ctx);

	if (ctx->afu->adapter->native->sl_ops->debugfs_stop_trace) {
		dev_crit(&ctx->afu->dev, "STOPPING CXL TRACE\n");
		ctx->afu->adapter->native->sl_ops->debugfs_stop_trace(ctx->afu->adapter);
	}

	return cxl_ops->ack_irq(ctx, 0, errstat);
}

static bool cxl_is_translation_fault(struct cxl_afu *afu, u64 dsisr)
{
	if ((cxl_is_psl8(afu)) && (dsisr & CXL_PSL_DSISR_TRANS))
		return true;

	if ((cxl_is_psl9(afu)) && (dsisr & CXL_PSL9_DSISR_An_TF))
		return true;

	return false;
}

irqreturn_t cxl_fail_irq_psl(struct cxl_afu *afu, struct cxl_irq_info *irq_info)
{
	if (cxl_is_translation_fault(afu, irq_info->dsisr))
		cxl_p2n_write(afu, CXL_PSL_TFC_An, CXL_PSL_TFC_An_AE);
	else
		cxl_p2n_write(afu, CXL_PSL_TFC_An, CXL_PSL_TFC_An_A);

	return IRQ_HANDLED;
}

static irqreturn_t native_irq_multiplexed(int irq, void *data)
{
	struct cxl_afu *afu = data;
	struct cxl_context *ctx;
	struct cxl_irq_info irq_info;
	u64 phreg = cxl_p2n_read(afu, CXL_PSL_PEHandle_An);
	int ph, ret = IRQ_HANDLED, res;

	/* check if eeh kicked in while the interrupt was in flight */
	if (unlikely(phreg == ~0ULL)) {
		dev_warn(&afu->dev,
			 "Ignoring slice interrupt(%d) due to fenced card",
			 irq);
		return IRQ_HANDLED;
	}
	/* Mask the pe-handle from register value */
	ph = phreg & 0xffff;
	if ((res = native_get_irq_info(afu, &irq_info))) {
		WARN(1, "Unable to get CXL IRQ Info: %i\n", res);
		if (afu->adapter->native->sl_ops->fail_irq)
			return afu->adapter->native->sl_ops->fail_irq(afu, &irq_info);
		return ret;
	}

	rcu_read_lock();
	ctx = idr_find(&afu->contexts_idr, ph);
	if (ctx) {
		if (afu->adapter->native->sl_ops->handle_interrupt)
			ret = afu->adapter->native->sl_ops->handle_interrupt(irq, ctx, &irq_info);
		rcu_read_unlock();
		return ret;
	}
	rcu_read_unlock();

	WARN(1, "Unable to demultiplex CXL PSL IRQ for PE %i DSISR %016llx DAR"
		" %016llx\n(Possible AFU HW issue - was a term/remove acked"
		" with outstanding transactions?)\n", ph, irq_info.dsisr,
		irq_info.dar);
	if (afu->adapter->native->sl_ops->fail_irq)
		ret = afu->adapter->native->sl_ops->fail_irq(afu, &irq_info);
	return ret;
}

static void native_irq_wait(struct cxl_context *ctx)
{
	u64 dsisr;
	int timeout = 1000;
	int ph;

	/*
	 * Wait until no further interrupts are presented by the PSL
	 * for this context.
	 */
	while (timeout--) {
		ph = cxl_p2n_read(ctx->afu, CXL_PSL_PEHandle_An) & 0xffff;
		if (ph != ctx->pe)
			return;
		dsisr = cxl_p2n_read(ctx->afu, CXL_PSL_DSISR_An);
		if (cxl_is_psl8(ctx->afu) &&
		   ((dsisr & CXL_PSL_DSISR_PENDING) == 0))
			return;
		if (cxl_is_psl9(ctx->afu) &&
		   ((dsisr & CXL_PSL9_DSISR_PENDING) == 0))
			return;
		/*
		 * We are waiting for the workqueue to process our
		 * irq, so need to let that run here.
		 */
		msleep(1);
	}

	dev_warn(&ctx->afu->dev, "WARNING: waiting on DSI for PE %i"
		 " DSISR %016llx!\n", ph, dsisr);
	return;
}

static irqreturn_t native_slice_irq_err(int irq, void *data)
{
	struct cxl_afu *afu = data;
	u64 errstat, serr, afu_error, dsisr;
	u64 fir_slice, afu_debug, irq_mask;

	/*
	 * slice err interrupt is only used with full PSL (no XSL)
	 */
	serr = cxl_p1n_read(afu, CXL_PSL_SERR_An);
	errstat = cxl_p2n_read(afu, CXL_PSL_ErrStat_An);
	afu_error = cxl_p2n_read(afu, CXL_AFU_ERR_An);
	dsisr = cxl_p2n_read(afu, CXL_PSL_DSISR_An);
	cxl_afu_decode_psl_serr(afu, serr);

	if (cxl_is_power8()) {
		fir_slice = cxl_p1n_read(afu, CXL_PSL_FIR_SLICE_An);
		afu_debug = cxl_p1n_read(afu, CXL_AFU_DEBUG_An);
		dev_crit(&afu->dev, "PSL_FIR_SLICE_An: 0x%016llx\n", fir_slice);
		dev_crit(&afu->dev, "CXL_PSL_AFU_DEBUG_An: 0x%016llx\n", afu_debug);
	}
	dev_crit(&afu->dev, "CXL_PSL_ErrStat_An: 0x%016llx\n", errstat);
	dev_crit(&afu->dev, "AFU_ERR_An: 0x%.16llx\n", afu_error);
	dev_crit(&afu->dev, "PSL_DSISR_An: 0x%.16llx\n", dsisr);

	/* mask off the IRQ so it won't retrigger until the AFU is reset */
	irq_mask = (serr & CXL_PSL_SERR_An_IRQS) >> 32;
	serr |= irq_mask;
	cxl_p1n_write(afu, CXL_PSL_SERR_An, serr);
	dev_info(&afu->dev, "Further such interrupts will be masked until the AFU is reset\n");

	return IRQ_HANDLED;
}

void cxl_native_err_irq_dump_regs(struct cxl *adapter)
{
	u64 fir1, fir2;

	fir1 = cxl_p1_read(adapter, CXL_PSL_FIR1);
	fir2 = cxl_p1_read(adapter, CXL_PSL_FIR2);

	dev_crit(&adapter->dev, "PSL_FIR1: 0x%016llx\nPSL_FIR2: 0x%016llx\n", fir1, fir2);
}

static irqreturn_t native_irq_err(int irq, void *data)
{
	struct cxl *adapter = data;
	u64 err_ivte;

	WARN(1, "CXL ERROR interrupt %i\n", irq);

	err_ivte = cxl_p1_read(adapter, CXL_PSL_ErrIVTE);
	dev_crit(&adapter->dev, "PSL_ErrIVTE: 0x%016llx\n", err_ivte);

	if (adapter->native->sl_ops->debugfs_stop_trace) {
		dev_crit(&adapter->dev, "STOPPING CXL TRACE\n");
		adapter->native->sl_ops->debugfs_stop_trace(adapter);
	}

	if (adapter->native->sl_ops->err_irq_dump_registers)
		adapter->native->sl_ops->err_irq_dump_registers(adapter);

	return IRQ_HANDLED;
}

int cxl_native_register_psl_err_irq(struct cxl *adapter)
{
	int rc;

	adapter->irq_name = kasprintf(GFP_KERNEL, "cxl-%s-err",
				      dev_name(&adapter->dev));
	if (!adapter->irq_name)
		return -ENOMEM;

	if ((rc = cxl_register_one_irq(adapter, native_irq_err, adapter,
				       &adapter->native->err_hwirq,
				       &adapter->native->err_virq,
				       adapter->irq_name))) {
		kfree(adapter->irq_name);
		adapter->irq_name = NULL;
		return rc;
	}

	cxl_p1_write(adapter, CXL_PSL_ErrIVTE, adapter->native->err_hwirq & 0xffff);

	return 0;
}

void cxl_native_release_psl_err_irq(struct cxl *adapter)
{
	if (adapter->native->err_virq != irq_find_mapping(NULL, adapter->native->err_hwirq))
		return;

	cxl_p1_write(adapter, CXL_PSL_ErrIVTE, 0x0000000000000000);
	cxl_unmap_irq(adapter->native->err_virq, adapter);
	cxl_ops->release_one_irq(adapter, adapter->native->err_hwirq);
	kfree(adapter->irq_name);
}

int cxl_native_register_serr_irq(struct cxl_afu *afu)
{
	u64 serr;
	int rc;

	afu->err_irq_name = kasprintf(GFP_KERNEL, "cxl-%s-err",
				      dev_name(&afu->dev));
	if (!afu->err_irq_name)
		return -ENOMEM;

	if ((rc = cxl_register_one_irq(afu->adapter, native_slice_irq_err, afu,
				       &afu->serr_hwirq,
				       &afu->serr_virq, afu->err_irq_name))) {
		kfree(afu->err_irq_name);
		afu->err_irq_name = NULL;
		return rc;
	}

	serr = cxl_p1n_read(afu, CXL_PSL_SERR_An);
	if (cxl_is_power8())
		serr = (serr & 0x00ffffffffff0000ULL) | (afu->serr_hwirq & 0xffff);
	if (cxl_is_power9()) {
		/*
		 * By default, all errors are masked. So don't set all masks.
		 * Slice errors will be transfered.
		 */
		serr = (serr & ~0xff0000007fffffffULL) | (afu->serr_hwirq & 0xffff);
	}
	cxl_p1n_write(afu, CXL_PSL_SERR_An, serr);

	return 0;
}

void cxl_native_release_serr_irq(struct cxl_afu *afu)
{
	if (afu->serr_virq != irq_find_mapping(NULL, afu->serr_hwirq))
		return;

	cxl_p1n_write(afu, CXL_PSL_SERR_An, 0x0000000000000000);
	cxl_unmap_irq(afu->serr_virq, afu);
	cxl_ops->release_one_irq(afu->adapter, afu->serr_hwirq);
	kfree(afu->err_irq_name);
}

int cxl_native_register_psl_irq(struct cxl_afu *afu)
{
	int rc;

	afu->psl_irq_name = kasprintf(GFP_KERNEL, "cxl-%s",
				      dev_name(&afu->dev));
	if (!afu->psl_irq_name)
		return -ENOMEM;

	if ((rc = cxl_register_one_irq(afu->adapter, native_irq_multiplexed,
				    afu, &afu->native->psl_hwirq, &afu->native->psl_virq,
				    afu->psl_irq_name))) {
		kfree(afu->psl_irq_name);
		afu->psl_irq_name = NULL;
	}
	return rc;
}

void cxl_native_release_psl_irq(struct cxl_afu *afu)
{
	if (afu->native->psl_virq != irq_find_mapping(NULL, afu->native->psl_hwirq))
		return;

	cxl_unmap_irq(afu->native->psl_virq, afu);
	cxl_ops->release_one_irq(afu->adapter, afu->native->psl_hwirq);
	kfree(afu->psl_irq_name);
}

static void recover_psl_err(struct cxl_afu *afu, u64 errstat)
{
	u64 dsisr;

	pr_devel("RECOVERING FROM PSL ERROR... (0x%016llx)\n", errstat);

	/* Clear PSL_DSISR[PE] */
	dsisr = cxl_p2n_read(afu, CXL_PSL_DSISR_An);
	cxl_p2n_write(afu, CXL_PSL_DSISR_An, dsisr & ~CXL_PSL_DSISR_An_PE);

	/* Write 1s to clear error status bits */
	cxl_p2n_write(afu, CXL_PSL_ErrStat_An, errstat);
}

static int native_ack_irq(struct cxl_context *ctx, u64 tfc, u64 psl_reset_mask)
{
	trace_cxl_psl_irq_ack(ctx, tfc);
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

static bool native_support_attributes(const char *attr_name,
				      enum cxl_attrs type)
{
	return true;
}

static int native_afu_cr_read64(struct cxl_afu *afu, int cr, u64 off, u64 *out)
{
	if (unlikely(!cxl_ops->link_ok(afu->adapter, afu)))
		return -EIO;
	if (unlikely(off >= afu->crs_len))
		return -ERANGE;
	*out = in_le64(afu->native->afu_desc_mmio + afu->crs_offset +
		(cr * afu->crs_len) + off);
	return 0;
}

static int native_afu_cr_read32(struct cxl_afu *afu, int cr, u64 off, u32 *out)
{
	if (unlikely(!cxl_ops->link_ok(afu->adapter, afu)))
		return -EIO;
	if (unlikely(off >= afu->crs_len))
		return -ERANGE;
	*out = in_le32(afu->native->afu_desc_mmio + afu->crs_offset +
		(cr * afu->crs_len) + off);
	return 0;
}

static int native_afu_cr_read16(struct cxl_afu *afu, int cr, u64 off, u16 *out)
{
	u64 aligned_off = off & ~0x3L;
	u32 val;
	int rc;

	rc = native_afu_cr_read32(afu, cr, aligned_off, &val);
	if (!rc)
		*out = (val >> ((off & 0x3) * 8)) & 0xffff;
	return rc;
}

static int native_afu_cr_read8(struct cxl_afu *afu, int cr, u64 off, u8 *out)
{
	u64 aligned_off = off & ~0x3L;
	u32 val;
	int rc;

	rc = native_afu_cr_read32(afu, cr, aligned_off, &val);
	if (!rc)
		*out = (val >> ((off & 0x3) * 8)) & 0xff;
	return rc;
}

static int native_afu_cr_write32(struct cxl_afu *afu, int cr, u64 off, u32 in)
{
	if (unlikely(!cxl_ops->link_ok(afu->adapter, afu)))
		return -EIO;
	if (unlikely(off >= afu->crs_len))
		return -ERANGE;
	out_le32(afu->native->afu_desc_mmio + afu->crs_offset +
		(cr * afu->crs_len) + off, in);
	return 0;
}

static int native_afu_cr_write16(struct cxl_afu *afu, int cr, u64 off, u16 in)
{
	u64 aligned_off = off & ~0x3L;
	u32 val32, mask, shift;
	int rc;

	rc = native_afu_cr_read32(afu, cr, aligned_off, &val32);
	if (rc)
		return rc;
	shift = (off & 0x3) * 8;
	WARN_ON(shift == 24);
	mask = 0xffff << shift;
	val32 = (val32 & ~mask) | (in << shift);

	rc = native_afu_cr_write32(afu, cr, aligned_off, val32);
	return rc;
}

static int native_afu_cr_write8(struct cxl_afu *afu, int cr, u64 off, u8 in)
{
	u64 aligned_off = off & ~0x3L;
	u32 val32, mask, shift;
	int rc;

	rc = native_afu_cr_read32(afu, cr, aligned_off, &val32);
	if (rc)
		return rc;
	shift = (off & 0x3) * 8;
	mask = 0xff << shift;
	val32 = (val32 & ~mask) | (in << shift);

	rc = native_afu_cr_write32(afu, cr, aligned_off, val32);
	return rc;
}

const struct cxl_backend_ops cxl_native_ops = {
	.module = THIS_MODULE,
	.adapter_reset = cxl_pci_reset,
	.alloc_one_irq = cxl_pci_alloc_one_irq,
	.release_one_irq = cxl_pci_release_one_irq,
	.alloc_irq_ranges = cxl_pci_alloc_irq_ranges,
	.release_irq_ranges = cxl_pci_release_irq_ranges,
	.setup_irq = cxl_pci_setup_irq,
	.handle_psl_slice_error = native_handle_psl_slice_error,
	.psl_interrupt = NULL,
	.ack_irq = native_ack_irq,
	.irq_wait = native_irq_wait,
	.attach_process = native_attach_process,
	.detach_process = native_detach_process,
	.update_ivtes = native_update_ivtes,
	.support_attributes = native_support_attributes,
	.link_ok = cxl_adapter_link_ok,
	.release_afu = cxl_pci_release_afu,
	.afu_read_err_buffer = cxl_pci_afu_read_err_buffer,
	.afu_check_and_enable = native_afu_check_and_enable,
	.afu_activate_mode = native_afu_activate_mode,
	.afu_deactivate_mode = native_afu_deactivate_mode,
	.afu_reset = native_afu_reset,
	.afu_cr_read8 = native_afu_cr_read8,
	.afu_cr_read16 = native_afu_cr_read16,
	.afu_cr_read32 = native_afu_cr_read32,
	.afu_cr_read64 = native_afu_cr_read64,
	.afu_cr_write8 = native_afu_cr_write8,
	.afu_cr_write16 = native_afu_cr_write16,
	.afu_cr_write32 = native_afu_cr_write32,
	.read_adapter_vpd = cxl_pci_read_adapter_vpd,
};
