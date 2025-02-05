/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_PLPAR_WRAPPERS_H
#define _ASM_POWERPC_PLPAR_WRAPPERS_H

#ifdef CONFIG_PPC_PSERIES

#include <linux/string.h>
#include <linux/irqflags.h>
#include <linux/delay.h>

#include <asm/hvcall.h>
#include <asm/paca.h>
#include <asm/lppaca.h>
#include <asm/page.h>

static inline long poll_pending(void)
{
	return plpar_hcall_norets(H_POLL_PENDING);
}

static inline long cede_processor(void)
{
	/*
	 * We cannot call tracepoints inside RCU idle regions which
	 * means we must not trace H_CEDE.
	 */
	return plpar_hcall_norets_notrace(H_CEDE);
}

static inline long vpa_call(unsigned long flags, unsigned long cpu,
		unsigned long vpa)
{
	flags = flags << H_VPA_FUNC_SHIFT;

	return plpar_hcall_norets(H_REGISTER_VPA, flags, cpu, vpa);
}

static inline long unregister_vpa(unsigned long cpu)
{
	return vpa_call(H_VPA_DEREG_VPA, cpu, 0);
}

static inline long register_vpa(unsigned long cpu, unsigned long vpa)
{
	return vpa_call(H_VPA_REG_VPA, cpu, vpa);
}

static inline long unregister_slb_shadow(unsigned long cpu)
{
	return vpa_call(H_VPA_DEREG_SLB, cpu, 0);
}

static inline long register_slb_shadow(unsigned long cpu, unsigned long vpa)
{
	return vpa_call(H_VPA_REG_SLB, cpu, vpa);
}

static inline long unregister_dtl(unsigned long cpu)
{
	return vpa_call(H_VPA_DEREG_DTL, cpu, 0);
}

static inline long register_dtl(unsigned long cpu, unsigned long vpa)
{
	return vpa_call(H_VPA_REG_DTL, cpu, vpa);
}

extern void vpa_init(int cpu);

static inline long plpar_pte_enter(unsigned long flags,
		unsigned long hpte_group, unsigned long hpte_v,
		unsigned long hpte_r, unsigned long *slot)
{
	long rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	rc = plpar_hcall(H_ENTER, retbuf, flags, hpte_group, hpte_v, hpte_r);

	*slot = retbuf[0];

	return rc;
}

static inline long plpar_pte_remove(unsigned long flags, unsigned long ptex,
		unsigned long avpn, unsigned long *old_pteh_ret,
		unsigned long *old_ptel_ret)
{
	long rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	rc = plpar_hcall(H_REMOVE, retbuf, flags, ptex, avpn);

	*old_pteh_ret = retbuf[0];
	*old_ptel_ret = retbuf[1];

	return rc;
}

/* plpar_pte_remove_raw can be called in real mode. It calls plpar_hcall_raw */
static inline long plpar_pte_remove_raw(unsigned long flags, unsigned long ptex,
		unsigned long avpn, unsigned long *old_pteh_ret,
		unsigned long *old_ptel_ret)
{
	long rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	rc = plpar_hcall_raw(H_REMOVE, retbuf, flags, ptex, avpn);

	*old_pteh_ret = retbuf[0];
	*old_ptel_ret = retbuf[1];

	return rc;
}

static inline long plpar_pte_read(unsigned long flags, unsigned long ptex,
		unsigned long *old_pteh_ret, unsigned long *old_ptel_ret)
{
	long rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	rc = plpar_hcall(H_READ, retbuf, flags, ptex);

	*old_pteh_ret = retbuf[0];
	*old_ptel_ret = retbuf[1];

	return rc;
}

/* plpar_pte_read_raw can be called in real mode. It calls plpar_hcall_raw */
static inline long plpar_pte_read_raw(unsigned long flags, unsigned long ptex,
		unsigned long *old_pteh_ret, unsigned long *old_ptel_ret)
{
	long rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	rc = plpar_hcall_raw(H_READ, retbuf, flags, ptex);

	*old_pteh_ret = retbuf[0];
	*old_ptel_ret = retbuf[1];

	return rc;
}

/*
 * ptes must be 8*sizeof(unsigned long)
 */
static inline long plpar_pte_read_4(unsigned long flags, unsigned long ptex,
				    unsigned long *ptes)

{
	long rc;
	unsigned long retbuf[PLPAR_HCALL9_BUFSIZE];

	rc = plpar_hcall9(H_READ, retbuf, flags | H_READ_4, ptex);

	memcpy(ptes, retbuf, 8*sizeof(unsigned long));

	return rc;
}

/*
 * plpar_pte_read_4_raw can be called in real mode.
 * ptes must be 8*sizeof(unsigned long)
 */
static inline long plpar_pte_read_4_raw(unsigned long flags, unsigned long ptex,
					unsigned long *ptes)

{
	long rc;
	unsigned long retbuf[PLPAR_HCALL9_BUFSIZE];

	rc = plpar_hcall9_raw(H_READ, retbuf, flags | H_READ_4, ptex);

	memcpy(ptes, retbuf, 8*sizeof(unsigned long));

	return rc;
}

static inline long plpar_pte_protect(unsigned long flags, unsigned long ptex,
		unsigned long avpn)
{
	return plpar_hcall_norets(H_PROTECT, flags, ptex, avpn);
}

static inline long plpar_resize_hpt_prepare(unsigned long flags,
					    unsigned long shift)
{
	return plpar_hcall_norets(H_RESIZE_HPT_PREPARE, flags, shift);
}

static inline long plpar_resize_hpt_commit(unsigned long flags,
					   unsigned long shift)
{
	return plpar_hcall_norets(H_RESIZE_HPT_COMMIT, flags, shift);
}

static inline long plpar_tce_get(unsigned long liobn, unsigned long ioba,
		unsigned long *tce_ret)
{
	long rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	rc = plpar_hcall(H_GET_TCE, retbuf, liobn, ioba);

	*tce_ret = retbuf[0];

	return rc;
}

static inline long plpar_tce_put(unsigned long liobn, unsigned long ioba,
		unsigned long tceval)
{
	return plpar_hcall_norets(H_PUT_TCE, liobn, ioba, tceval);
}

static inline long plpar_tce_put_indirect(unsigned long liobn,
		unsigned long ioba, unsigned long page, unsigned long count)
{
	return plpar_hcall_norets(H_PUT_TCE_INDIRECT, liobn, ioba, page, count);
}

static inline long plpar_tce_stuff(unsigned long liobn, unsigned long ioba,
		unsigned long tceval, unsigned long count)
{
	return plpar_hcall_norets(H_STUFF_TCE, liobn, ioba, tceval, count);
}

/* Set various resource mode parameters */
static inline long plpar_set_mode(unsigned long mflags, unsigned long resource,
		unsigned long value1, unsigned long value2)
{
	return plpar_hcall_norets(H_SET_MODE, mflags, resource, value1, value2);
}

/*
 * Enable relocation on exceptions on this partition
 *
 * Note: this call has a partition wide scope and can take a while to complete.
 * If it returns H_LONG_BUSY_* it should be retried periodically until it
 * returns H_SUCCESS.
 */
static inline long enable_reloc_on_exceptions(void)
{
	/* mflags = 3: Exceptions at 0xC000000000004000 */
	return plpar_set_mode(3, H_SET_MODE_RESOURCE_ADDR_TRANS_MODE, 0, 0);
}

/*
 * Disable relocation on exceptions on this partition
 *
 * Note: this call has a partition wide scope and can take a while to complete.
 * If it returns H_LONG_BUSY_* it should be retried periodically until it
 * returns H_SUCCESS.
 */
static inline long disable_reloc_on_exceptions(void) {
	return plpar_set_mode(0, H_SET_MODE_RESOURCE_ADDR_TRANS_MODE, 0, 0);
}

/*
 * Take exceptions in big endian mode on this partition
 *
 * Note: this call has a partition wide scope and can take a while to complete.
 * If it returns H_LONG_BUSY_* it should be retried periodically until it
 * returns H_SUCCESS.
 */
static inline long enable_big_endian_exceptions(void)
{
	/* mflags = 0: big endian exceptions */
	return plpar_set_mode(0, H_SET_MODE_RESOURCE_LE, 0, 0);
}

/*
 * Take exceptions in little endian mode on this partition
 *
 * Note: this call has a partition wide scope and can take a while to complete.
 * If it returns H_LONG_BUSY_* it should be retried periodically until it
 * returns H_SUCCESS.
 */
static inline long enable_little_endian_exceptions(void)
{
	/* mflags = 1: little endian exceptions */
	return plpar_set_mode(1, H_SET_MODE_RESOURCE_LE, 0, 0);
}

static inline long plpar_set_ciabr(unsigned long ciabr)
{
	return plpar_set_mode(0, H_SET_MODE_RESOURCE_SET_CIABR, ciabr, 0);
}

static inline long plpar_set_watchpoint0(unsigned long dawr0, unsigned long dawrx0)
{
	return plpar_set_mode(0, H_SET_MODE_RESOURCE_SET_DAWR0, dawr0, dawrx0);
}

static inline long plpar_set_watchpoint1(unsigned long dawr1, unsigned long dawrx1)
{
	return plpar_set_mode(0, H_SET_MODE_RESOURCE_SET_DAWR1, dawr1, dawrx1);
}

static inline long plpar_signal_sys_reset(long cpu)
{
	return plpar_hcall_norets(H_SIGNAL_SYS_RESET, cpu);
}

static inline long plpar_get_cpu_characteristics(struct h_cpu_char_result *p)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	rc = plpar_hcall(H_GET_CPU_CHARACTERISTICS, retbuf);
	if (rc == H_SUCCESS) {
		p->character = retbuf[0];
		p->behaviour = retbuf[1];
	}

	return rc;
}

static inline long plpar_guest_create(unsigned long flags, unsigned long *guest_id)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	unsigned long token;
	long rc;

	token = -1UL;
	do {
		rc = plpar_hcall(H_GUEST_CREATE, retbuf, flags, token);
		if (rc == H_SUCCESS)
			*guest_id = retbuf[0];

		if (rc == H_BUSY) {
			token = retbuf[0];
			cond_resched();
		}

		if (H_IS_LONG_BUSY(rc)) {
			token = retbuf[0];
			msleep(get_longbusy_msecs(rc));
			rc = H_BUSY;
		}

	} while (rc == H_BUSY);

	return rc;
}

static inline long plpar_guest_create_vcpu(unsigned long flags,
					   unsigned long guest_id,
					   unsigned long vcpu_id)
{
	long rc;

	do {
		rc = plpar_hcall_norets(H_GUEST_CREATE_VCPU, 0, guest_id, vcpu_id);

		if (rc == H_BUSY)
			cond_resched();

		if (H_IS_LONG_BUSY(rc)) {
			msleep(get_longbusy_msecs(rc));
			rc = H_BUSY;
		}

	} while (rc == H_BUSY);

	return rc;
}

static inline long plpar_guest_set_state(unsigned long flags,
					 unsigned long guest_id,
					 unsigned long vcpu_id,
					 unsigned long data_buffer,
					 unsigned long data_size,
					 unsigned long *failed_index)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	while (true) {
		rc = plpar_hcall(H_GUEST_SET_STATE, retbuf, flags, guest_id,
				 vcpu_id, data_buffer, data_size);

		if (rc == H_BUSY) {
			cpu_relax();
			continue;
		}

		if (H_IS_LONG_BUSY(rc)) {
			mdelay(get_longbusy_msecs(rc));
			continue;
		}

		if (rc == H_INVALID_ELEMENT_ID)
			*failed_index = retbuf[0];
		else if (rc == H_INVALID_ELEMENT_SIZE)
			*failed_index = retbuf[0];
		else if (rc == H_INVALID_ELEMENT_VALUE)
			*failed_index = retbuf[0];

		break;
	}

	return rc;
}

static inline long plpar_guest_get_state(unsigned long flags,
					 unsigned long guest_id,
					 unsigned long vcpu_id,
					 unsigned long data_buffer,
					 unsigned long data_size,
					 unsigned long *failed_index)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	while (true) {
		rc = plpar_hcall(H_GUEST_GET_STATE, retbuf, flags, guest_id,
				 vcpu_id, data_buffer, data_size);

		if (rc == H_BUSY) {
			cpu_relax();
			continue;
		}

		if (H_IS_LONG_BUSY(rc)) {
			mdelay(get_longbusy_msecs(rc));
			continue;
		}

		if (rc == H_INVALID_ELEMENT_ID)
			*failed_index = retbuf[0];
		else if (rc == H_INVALID_ELEMENT_SIZE)
			*failed_index = retbuf[0];
		else if (rc == H_INVALID_ELEMENT_VALUE)
			*failed_index = retbuf[0];

		break;
	}

	return rc;
}

static inline long plpar_guest_run_vcpu(unsigned long flags, unsigned long guest_id,
					unsigned long vcpu_id, int *trap,
					unsigned long *failed_index)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	rc = plpar_hcall(H_GUEST_RUN_VCPU, retbuf, flags, guest_id, vcpu_id);
	if (rc == H_SUCCESS)
		*trap = retbuf[0];
	else if (rc == H_INVALID_ELEMENT_ID)
		*failed_index = retbuf[0];
	else if (rc == H_INVALID_ELEMENT_SIZE)
		*failed_index = retbuf[0];
	else if (rc == H_INVALID_ELEMENT_VALUE)
		*failed_index = retbuf[0];

	return rc;
}

static inline long plpar_guest_delete(unsigned long flags, u64 guest_id)
{
	long rc;

	do {
		rc = plpar_hcall_norets(H_GUEST_DELETE, flags, guest_id);
		if (rc == H_BUSY)
			cond_resched();

		if (H_IS_LONG_BUSY(rc)) {
			msleep(get_longbusy_msecs(rc));
			rc = H_BUSY;
		}

	} while (rc == H_BUSY);

	return rc;
}

static inline long plpar_guest_set_capabilities(unsigned long flags,
						unsigned long capabilities)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	do {
		rc = plpar_hcall(H_GUEST_SET_CAPABILITIES, retbuf, flags, capabilities);
		if (rc == H_BUSY)
			cond_resched();

		if (H_IS_LONG_BUSY(rc)) {
			msleep(get_longbusy_msecs(rc));
			rc = H_BUSY;
		}
	} while (rc == H_BUSY);

	return rc;
}

static inline long plpar_guest_get_capabilities(unsigned long flags,
						unsigned long *capabilities)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	do {
		rc = plpar_hcall(H_GUEST_GET_CAPABILITIES, retbuf, flags);
		if (rc == H_BUSY)
			cond_resched();

		if (H_IS_LONG_BUSY(rc)) {
			msleep(get_longbusy_msecs(rc));
			rc = H_BUSY;
		}
	} while (rc == H_BUSY);

	if (rc == H_SUCCESS)
		*capabilities = retbuf[0];

	return rc;
}

/*
 * Wrapper to H_RPT_INVALIDATE hcall that handles return values appropriately
 *
 * - Returns H_SUCCESS on success
 * - For H_BUSY return value, we retry the hcall.
 * - For any other hcall failures, attempt a full flush once before
 *   resorting to BUG().
 *
 * Note: This hcall is expected to fail only very rarely. The correct
 * error recovery of killing the process/guest will be eventually
 * needed.
 */
static inline long pseries_rpt_invalidate(u64 pid, u64 target, u64 type,
					  u64 page_sizes, u64 start, u64 end)
{
	long rc;
	unsigned long all;

	while (true) {
		rc = plpar_hcall_norets(H_RPT_INVALIDATE, pid, target, type,
					page_sizes, start, end);
		if (rc == H_BUSY) {
			cpu_relax();
			continue;
		} else if (rc == H_SUCCESS)
			return rc;

		/* Flush request failed, try with a full flush once */
		if (type & H_RPTI_TYPE_NESTED)
			all = H_RPTI_TYPE_NESTED | H_RPTI_TYPE_NESTED_ALL;
		else
			all = H_RPTI_TYPE_ALL;
retry:
		rc = plpar_hcall_norets(H_RPT_INVALIDATE, pid, target,
					all, page_sizes, 0, -1UL);
		if (rc == H_BUSY) {
			cpu_relax();
			goto retry;
		} else if (rc == H_SUCCESS)
			return rc;

		BUG();
	}
}

#else /* !CONFIG_PPC_PSERIES */

static inline long plpar_set_ciabr(unsigned long ciabr)
{
	return 0;
}

static inline long plpar_pte_read_4(unsigned long flags, unsigned long ptex,
				    unsigned long *ptes)
{
	return 0;
}

static inline long pseries_rpt_invalidate(u64 pid, u64 target, u64 type,
					  u64 page_sizes, u64 start, u64 end)
{
	return 0;
}

static inline long plpar_guest_create_vcpu(unsigned long flags,
					   unsigned long guest_id,
					   unsigned long vcpu_id)
{
	return 0;
}

static inline long plpar_guest_get_state(unsigned long flags,
					 unsigned long guest_id,
					 unsigned long vcpu_id,
					 unsigned long data_buffer,
					 unsigned long data_size,
					 unsigned long *failed_index)
{
	return 0;
}

static inline long plpar_guest_set_state(unsigned long flags,
					 unsigned long guest_id,
					 unsigned long vcpu_id,
					 unsigned long data_buffer,
					 unsigned long data_size,
					 unsigned long *failed_index)
{
	return 0;
}

static inline long plpar_guest_run_vcpu(unsigned long flags, unsigned long guest_id,
					unsigned long vcpu_id, int *trap,
					unsigned long *failed_index)
{
	return 0;
}

static inline long plpar_guest_create(unsigned long flags, unsigned long *guest_id)
{
	return 0;
}

static inline long plpar_guest_delete(unsigned long flags, u64 guest_id)
{
	return 0;
}

static inline long plpar_guest_get_capabilities(unsigned long flags,
						unsigned long *capabilities)
{
	return 0;
}

static inline long plpar_guest_set_capabilities(unsigned long flags,
						unsigned long capabilities)
{
	return 0;
}

#endif /* CONFIG_PPC_PSERIES */

#endif /* _ASM_POWERPC_PLPAR_WRAPPERS_H */
