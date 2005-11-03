#ifndef _PSERIES_PLPAR_WRAPPERS_H
#define _PSERIES_PLPAR_WRAPPERS_H

#include <asm/hvcall.h>

static inline long poll_pending(void)
{
	unsigned long dummy;
	return plpar_hcall(H_POLL_PENDING, 0, 0, 0, 0, &dummy, &dummy, &dummy);
}

static inline long prod_processor(void)
{
	plpar_hcall_norets(H_PROD);
	return 0;
}

static inline long cede_processor(void)
{
	plpar_hcall_norets(H_CEDE);
	return 0;
}

static inline long vpa_call(unsigned long flags, unsigned long cpu,
		unsigned long vpa)
{
	/* flags are in bits 16-18 (counting from most significant bit) */
	flags = flags << (63 - 18);

	return plpar_hcall_norets(H_REGISTER_VPA, flags, cpu, vpa);
}

static inline long unregister_vpa(unsigned long cpu, unsigned long vpa)
{
	return vpa_call(0x5, cpu, vpa);
}

static inline long register_vpa(unsigned long cpu, unsigned long vpa)
{
	return vpa_call(0x1, cpu, vpa);
}

extern void vpa_init(int cpu);

static inline long plpar_pte_remove(unsigned long flags, unsigned long ptex,
		unsigned long avpn, unsigned long *old_pteh_ret,
		unsigned long *old_ptel_ret)
{
	unsigned long dummy;
	return plpar_hcall(H_REMOVE, flags, ptex, avpn, 0, old_pteh_ret,
			old_ptel_ret, &dummy);
}

static inline long plpar_pte_read(unsigned long flags, unsigned long ptex,
		unsigned long *old_pteh_ret, unsigned long *old_ptel_ret)
{
	unsigned long dummy;
	return plpar_hcall(H_READ, flags, ptex, 0, 0, old_pteh_ret,
			old_ptel_ret, &dummy);
}

static inline long plpar_pte_protect(unsigned long flags, unsigned long ptex,
		unsigned long avpn)
{
	return plpar_hcall_norets(H_PROTECT, flags, ptex, avpn);
}

static inline long plpar_tce_get(unsigned long liobn, unsigned long ioba,
		unsigned long *tce_ret)
{
	unsigned long dummy;
	return plpar_hcall(H_GET_TCE, liobn, ioba, 0, 0, tce_ret, &dummy,
			&dummy);
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
	unsigned long *lbuf = (unsigned long *)buf_ret;	/* TODO: alignment? */
	return plpar_hcall(H_GET_TERM_CHAR, termno, 0, 0, 0, len_ret,
			lbuf + 0, lbuf + 1);
}

static inline long plpar_put_term_char(unsigned long termno, unsigned long len,
		const char *buffer)
{
	unsigned long *lbuf = (unsigned long *)buffer;	/* TODO: alignment? */
	return plpar_hcall_norets(H_PUT_TERM_CHAR, termno, len, lbuf[0],
			lbuf[1]);
}

static inline long plpar_set_xdabr(unsigned long address, unsigned long flags)
{
	return plpar_hcall_norets(H_SET_XDABR, address, flags);
}

static inline long plpar_set_dabr(unsigned long val)
{
	return plpar_hcall_norets(H_SET_DABR, val);
}

#endif /* _PSERIES_PLPAR_WRAPPERS_H */
