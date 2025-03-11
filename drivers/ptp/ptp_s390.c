// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * s390 PTP clock driver
 *
 */

#include "ptp_private.h"
#include <linux/time.h>
#include <asm/stp.h>

static struct ptp_clock *ptp_stcke_clock, *ptp_qpt_clock;

static int ptp_s390_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	return -EOPNOTSUPP;
}

static int ptp_s390_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	return -EOPNOTSUPP;
}

static struct timespec64 eitod_to_timespec64(union tod_clock *clk)
{
	return ns_to_timespec64(eitod_to_ns(clk->eitod - TOD_UNIX_EPOCH));
}

static struct timespec64 tod_to_timespec64(unsigned long tod)
{
	return ns_to_timespec64(tod_to_ns(tod - TOD_UNIX_EPOCH));
}

static int ptp_s390_stcke_gettime(struct ptp_clock_info *ptp,
				  struct timespec64 *ts)
{
	union tod_clock tod;

	if (!stp_enabled())
		return -EOPNOTSUPP;

	store_tod_clock_ext(&tod);
	*ts = eitod_to_timespec64(&tod);
	return 0;
}

static int ptp_s390_qpt_gettime(struct ptp_clock_info *ptp,
				struct timespec64 *ts)
{
	unsigned long tod;

	ptff(&tod, sizeof(tod), PTFF_QPT);
	*ts = tod_to_timespec64(tod);
	return 0;
}

static int ptp_s390_settime(struct ptp_clock_info *ptp,
			    const struct timespec64 *ts)
{
	return -EOPNOTSUPP;
}

static int s390_arch_ptp_get_crosststamp(ktime_t *device_time,
					 struct system_counterval_t *system_counter,
					 void *ctx)
{
	union tod_clock clk;

	store_tod_clock_ext(&clk);
	*device_time = ns_to_ktime(tod_to_ns(clk.tod - TOD_UNIX_EPOCH));
	system_counter->cycles = clk.tod;
	system_counter->cs_id = CSID_S390_TOD;
	return 0;
}

static int ptp_s390_getcrosststamp(struct ptp_clock_info *ptp,
				   struct system_device_crosststamp *xtstamp)
{
	if (!stp_enabled())
		return -EOPNOTSUPP;
	return get_device_system_crosststamp(s390_arch_ptp_get_crosststamp, NULL, NULL, xtstamp);
}

static struct ptp_clock_info ptp_s390_stcke_info = {
	.owner		= THIS_MODULE,
	.name		= "s390 STCKE Clock",
	.max_adj	= 0,
	.adjfine	= ptp_s390_adjfine,
	.adjtime	= ptp_s390_adjtime,
	.gettime64	= ptp_s390_stcke_gettime,
	.settime64	= ptp_s390_settime,
	.getcrosststamp = ptp_s390_getcrosststamp,
};

static struct ptp_clock_info ptp_s390_qpt_info = {
	.owner		= THIS_MODULE,
	.name		= "s390 Physical Clock",
	.max_adj	= 0,
	.adjfine	= ptp_s390_adjfine,
	.adjtime	= ptp_s390_adjtime,
	.gettime64	= ptp_s390_qpt_gettime,
	.settime64	= ptp_s390_settime,
};

static __init int ptp_s390_init(void)
{
	ptp_stcke_clock = ptp_clock_register(&ptp_s390_stcke_info, NULL);
	if (IS_ERR(ptp_stcke_clock))
		return PTR_ERR(ptp_stcke_clock);

	ptp_qpt_clock = ptp_clock_register(&ptp_s390_qpt_info, NULL);
	if (IS_ERR(ptp_qpt_clock)) {
		ptp_clock_unregister(ptp_stcke_clock);
		return PTR_ERR(ptp_qpt_clock);
	}
	return 0;
}

static __exit void ptp_s390_exit(void)
{
	ptp_clock_unregister(ptp_qpt_clock);
	ptp_clock_unregister(ptp_stcke_clock);
}

module_init(ptp_s390_init);
module_exit(ptp_s390_exit);

MODULE_AUTHOR("Sven Schnelle <svens@linux.ibm.com>");
MODULE_DESCRIPTION("s390 Physical/STCKE Clock PtP Driver");
MODULE_LICENSE("GPL");
