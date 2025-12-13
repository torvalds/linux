/* SPDX-License-Identifier: GPL-2.0 */
/* n2rng.h: Niagara2 RNG defines.
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */

#ifndef _N2RNG_H
#define _N2RNG_H

/* ver1 devices - n2-rng, vf-rng, kt-rng */
#define RNG_v1_CTL_WAIT       0x0000000001fffe00ULL /* Minimum wait time    */
#define RNG_v1_CTL_WAIT_SHIFT 9
#define RNG_v1_CTL_BYPASS     0x0000000000000100ULL /* VCO voltage source   */
#define RNG_v1_CTL_VCO        0x00000000000000c0ULL /* VCO rate control     */
#define RNG_v1_CTL_VCO_SHIFT  6
#define RNG_v1_CTL_ASEL       0x0000000000000030ULL /* Analog MUX select    */
#define RNG_v1_CTL_ASEL_SHIFT 4
#define RNG_v1_CTL_ASEL_NOOUT 2

/* these are the same in v2 as in v1 */
#define RNG_CTL_LFSR       0x0000000000000008ULL /* Use LFSR or plain shift */
#define RNG_CTL_ES3        0x0000000000000004ULL /* Enable entropy source 3 */
#define RNG_CTL_ES2        0x0000000000000002ULL /* Enable entropy source 2 */
#define RNG_CTL_ES1        0x0000000000000001ULL /* Enable entropy source 1 */

/* ver2 devices - m4-rng, m7-rng */
#define RNG_v2_CTL_WAIT       0x0000000007fff800ULL /* Minimum wait time    */
#define RNG_v2_CTL_WAIT_SHIFT 12
#define RNG_v2_CTL_BYPASS     0x0000000000000400ULL /* VCO voltage source   */
#define RNG_v2_CTL_VCO        0x0000000000000300ULL /* VCO rate control     */
#define RNG_v2_CTL_VCO_SHIFT  9
#define RNG_v2_CTL_PERF       0x0000000000000180ULL /* Perf */
#define RNG_v2_CTL_ASEL       0x0000000000000070ULL /* Analog MUX select    */
#define RNG_v2_CTL_ASEL_SHIFT 4
#define RNG_v2_CTL_ASEL_NOOUT 7


#define HV_FAST_RNG_GET_DIAG_CTL	0x130
#define HV_FAST_RNG_CTL_READ		0x131
#define HV_FAST_RNG_CTL_WRITE		0x132
#define HV_FAST_RNG_DATA_READ_DIAG	0x133
#define HV_FAST_RNG_DATA_READ		0x134

#define HV_RNG_STATE_UNCONFIGURED	0
#define HV_RNG_STATE_CONFIGURED		1
#define HV_RNG_STATE_HEALTHCHECK	2
#define HV_RNG_STATE_ERROR		3

#define HV_RNG_NUM_CONTROL		4

#ifndef __ASSEMBLER__
extern unsigned long sun4v_rng_get_diag_ctl(void);
extern unsigned long sun4v_rng_ctl_read_v1(unsigned long ctl_regs_ra,
					   unsigned long *state,
					   unsigned long *tick_delta);
extern unsigned long sun4v_rng_ctl_read_v2(unsigned long ctl_regs_ra,
					   unsigned long unit,
					   unsigned long *state,
					   unsigned long *tick_delta,
					   unsigned long *watchdog,
					   unsigned long *write_status);
extern unsigned long sun4v_rng_ctl_write_v1(unsigned long ctl_regs_ra,
					    unsigned long state,
					    unsigned long write_timeout,
					    unsigned long *tick_delta);
extern unsigned long sun4v_rng_ctl_write_v2(unsigned long ctl_regs_ra,
					    unsigned long state,
					    unsigned long write_timeout,
					    unsigned long unit);
extern unsigned long sun4v_rng_data_read_diag_v1(unsigned long data_ra,
						 unsigned long len,
						 unsigned long *tick_delta);
extern unsigned long sun4v_rng_data_read_diag_v2(unsigned long data_ra,
						 unsigned long len,
						 unsigned long unit,
						 unsigned long *tick_delta);
extern unsigned long sun4v_rng_data_read(unsigned long data_ra,
					 unsigned long *tick_delta);

enum n2rng_compat_id {
	N2_n2_rng,
	N2_vf_rng,
	N2_kt_rng,
	N2_m4_rng,
	N2_m7_rng,
};

struct n2rng_template {
	enum n2rng_compat_id id;
	int multi_capable;
	int chip_version;
};

struct n2rng_unit {
	u64			control[HV_RNG_NUM_CONTROL];
};

struct n2rng {
	struct platform_device	*op;

	unsigned long		flags;
#define N2RNG_FLAG_MULTI	0x00000001 /* Multi-unit capable RNG */
#define N2RNG_FLAG_CONTROL	0x00000002 /* Operating in control domain */
#define N2RNG_FLAG_READY	0x00000008 /* Ready for hw-rng layer      */
#define N2RNG_FLAG_SHUTDOWN	0x00000010 /* Driver unregistering        */
#define N2RNG_FLAG_BUFFER_VALID	0x00000020 /* u32 buffer holds valid data */

	struct n2rng_template	*data;
	int			num_units;
	struct n2rng_unit	*units;

	struct hwrng		hwrng;
	u32			buffer;

	/* Registered hypervisor group API major and minor version.  */
	unsigned long		hvapi_major;
	unsigned long		hvapi_minor;

	struct delayed_work	work;

	unsigned long		hv_state; /* HV_RNG_STATE_foo */

	unsigned long		health_check_sec;
	unsigned long		accum_cycles;
	unsigned long		wd_timeo;
#define N2RNG_HEALTH_CHECK_SEC_DEFAULT	0
#define N2RNG_ACCUM_CYCLES_DEFAULT	2048
#define N2RNG_WD_TIMEO_DEFAULT		0

	u64			scratch_control[HV_RNG_NUM_CONTROL];

#define RNG_v1_SELFTEST_TICKS	38859
#define RNG_v1_SELFTEST_VAL	((u64)0xB8820C7BD387E32C)
#define RNG_v2_SELFTEST_TICKS	64
#define RNG_v2_SELFTEST_VAL	((u64)0xffffffffffffffff)
#define SELFTEST_POLY		((u64)0x231DCEE91262B8A3)
#define SELFTEST_MATCH_GOAL	6
#define SELFTEST_LOOPS_MAX	40000
#define SELFTEST_BUFFER_WORDS	8

	u64			test_data;
	u64			test_control[HV_RNG_NUM_CONTROL];
	u64			test_buffer[SELFTEST_BUFFER_WORDS];
};

#define N2RNG_BLOCK_LIMIT	60000
#define N2RNG_BUSY_LIMIT	100
#define N2RNG_HCHECK_LIMIT	100

#endif /* !(__ASSEMBLER__) */

#endif /* _N2RNG_H */
