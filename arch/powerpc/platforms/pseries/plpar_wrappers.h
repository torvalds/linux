#ifndef _PSERIES_PLPAR_WRAPPERS_H
#define _PSERIES_PLPAR_WRAPPERS_H

#include <linux/string.h>

#include <asm/hvcall.h>
#include <asm/paca.h>
#include <asm/page.h>

/* Get state of physical CPU from query_cpu_stopped */
int smp_query_cpu_stopped(unsigned int pcpu);
#define QCSS_STOPPED 0
#define QCSS_STOPPING 1
#define QCSS_NOT_STOPPED 2
#define QCSS_HARDWARE_ERROR -1
#define QCSS_HARDWARE_BUSY -2

static inline long poll_pending(void)
{
	return plpar_hcall_norets(H_POLL_PENDING);
}

static inline u8 get_cede_latency_hint(void)
{
	return get_lppaca()->cede_latency_hint;
}

static inline void set_cede_latency_hint(u8 latency_hint)
{
	get_lppaca()->cede_latency_hint = latency_hint;
}

static inline long cede_processor(void)
{
	return plpar_hcall_norets(H_CEDE);
}

static inline long extended_cede_processor(unsigned long latency_hint)
{
	long rc;
	u8 old_latency_hint = get_cede_latency_hint();

	set_cede_latency_hint(latency_hint);
	rc = cede_processor();
	set_cede_latency_hint(old_latency_hint);

	return rc;
}

static inline long vpa_call(unsigned long flags, unsigned long cpu,
		unsigned long vpa)
{
	/* flags are in bits 16-18 (counting from most significant bit) */
	flags = flags << (63 - 18);

	return plpar_hcall_norets(H_REGISTER_VPA, flags, cpu, vpa);
}

static inline long unregister_vpa(unsigned long cpu)
{
	return vpa_call(0x5, cpu, 0);
}

static inline long register_vpa(unsigned long cpu, unsigned long vpa)
{
	return vpa_call(0x1, cpu, vpa);
}

static inline long unregister_slb_shadow(unsigned long cpu)
{
	return vpa_call(0x7, cpu, 0);
}

static inline long register_slb_shadow(unsigned long cpu, unsigned long vpa)
{
	return vpa_call(0x3, cpu, vpa);
}

static inline long unregister_dtl(unsigned long cpu)
{
	return vpa_call(0x6, cpu, 0);
}

static inline long register_dtl(unsigned long cpu, unsigned long vpa)
{
	return vpa_call(0x2, cpu, vpa);
}

static inline long plpar_page_set_loaned(unsigned long vpa)
{
	unsigned long cmo_page_sz = cmo_get_page_size();
	long rc = 0;
	int i;

	for (i = 0; !rc && i < PAGE_SIZE; i += cmo_page_sz)
		rc = plpar_hcall_norets(H_PAGE_INIT, H_PAGE_SET_LOANED, vpa + i, 0);

	for (i -= cmo_page_sz; rc && i != 0; i -= cmo_page_sz)
		plpar_hcall_norets(H_PAGE_INIT, H_PAGE_SET_ACTIVE,
				   vpa + i - cmo_page_sz, 0);

	return rc;
}

static inline long plpar_page_set_active(unsigned long vpa)
{
	unsigned long cmo_page_sz = cmo_get_page_size();
	long rc = 0;
	int i;

	for (i = 0; !rc && i < PAGE_SIZE; i += cmo_page_sz)
		rc = plpar_hcall_norets(H_PAGE_INIT, H_PAGE_SET_ACTIVE, vpa + i, 0);

	for (i -= cmo_page_sz; rc && i != 0; i -= cmo_page_sz)
		plpar_hcall_norets(H_PAGE_INIT, H_PAGE_SET_LOANED,
				   vpa + i - cmo_page_sz, 0);

	return rc;
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

static inline long plpar_get_term_char(unsigned long termno,
		unsigned long *len_ret, char *buf_ret)
{
	long rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	unsigned long *lbuf = (unsigned long *)buf_ret;	/* TODO: alignment? */

	rc = plpar_hcall(H_GET_TERM_CHAR, retbuf, termno);

	*len_ret = retbuf[0];
	lbuf[0] = retbuf[1];
	lbuf[1] = retbuf[2];

	return rc;
}

static inline long plpar_put_term_char(unsigned long termno, unsigned long len,
		const char *buffer)
{
	unsigned long *lbuf = (unsigned long *)buffer;	/* TODO: alignment? */
	return plpar_hcall_norets(H_PUT_TERM_CHAR, termno, len, lbuf[0],
			lbuf[1]);
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
	return plpar_set_mode(3, 3, 0, 0);
}

/*
 * Disable relocation on exceptions on this partition
 *
 * Note: this call has a partition wide scope and can take a while to complete.
 * If it returns H_LONG_BUSY_* it should be retried periodically until it
 * returns H_SUCCESS.
 */
static inline long disable_reloc_on_exceptions(void) {
	return plpar_set_mode(0, 3, 0, 0);
}

#endif /* _PSERIES_PLPAR_WRAPPERS_H */
