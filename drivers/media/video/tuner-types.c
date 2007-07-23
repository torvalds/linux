/*
 *
 * i2c tv tuner chip device type database.
 *
 */

#include <linux/i2c.h>
#include <media/tuner.h>
#include <media/tuner-types.h>

/* ---------------------------------------------------------------------- */

/*
 *	The floats in the tuner struct are computed at compile time
 *	by gcc and cast back to integers. Thus we don't violate the
 *	"no float in kernel" rule.
 *
 *	A tuner_range may be referenced by multiple tuner_params structs.
 *	There are many duplicates in here. Reusing tuner_range structs,
 *	rather than defining new ones for each tuner, will cut down on
 *	memory usage, and is preferred when possible.
 *
 *	Each tuner_params array may contain one or more elements, one
 *	for each video standard.
 *
 *	FIXME: tuner_params struct contains an element, tda988x. We must
 *	set this for all tuners that contain a tda988x chip, and then we
 *	can remove this setting from the various card structs.
 *
 *	FIXME: Right now, all tuners are using the first tuner_params[]
 *	array element for analog mode. In the future, we will be merging
 *	similar tuner definitions together, such that each tuner definition
 *	will have a tuner_params struct for each available video standard.
 *	At that point, the tuner_params[] array element will be chosen
 *	based on the video standard in use.
 */

/* 0-9 */
/* ------------ TUNER_TEMIC_PAL - TEMIC PAL ------------ */

static struct tuner_range tuner_temic_pal_ranges[] = {
	{ 16 * 140.25 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 463.25 /*MHz*/, 0x8e, 0x04, },
	{ 16 * 999.99        , 0x8e, 0x01, },
};

static struct tuner_params tuner_temic_pal_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_temic_pal_ranges,
		.count  = ARRAY_SIZE(tuner_temic_pal_ranges),
	},
};

/* ------------ TUNER_PHILIPS_PAL_I - Philips PAL_I ------------ */

static struct tuner_range tuner_philips_pal_i_ranges[] = {
	{ 16 * 140.25 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 463.25 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_philips_pal_i_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_philips_pal_i_ranges,
		.count  = ARRAY_SIZE(tuner_philips_pal_i_ranges),
	},
};

/* ------------ TUNER_PHILIPS_NTSC - Philips NTSC ------------ */

static struct tuner_range tuner_philips_ntsc_ranges[] = {
	{ 16 * 157.25 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 451.25 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_philips_ntsc_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_philips_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_philips_ntsc_ranges),
		.cb_first_if_lower_freq = 1,
	},
};

/* ------------ TUNER_PHILIPS_SECAM - Philips SECAM ------------ */

static struct tuner_range tuner_philips_secam_ranges[] = {
	{ 16 * 168.25 /*MHz*/, 0x8e, 0xa7, },
	{ 16 * 447.25 /*MHz*/, 0x8e, 0x97, },
	{ 16 * 999.99        , 0x8e, 0x37, },
};

static struct tuner_params tuner_philips_secam_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_SECAM,
		.ranges = tuner_philips_secam_ranges,
		.count  = ARRAY_SIZE(tuner_philips_secam_ranges),
		.cb_first_if_lower_freq = 1,
	},
};

/* ------------ TUNER_PHILIPS_PAL - Philips PAL ------------ */

static struct tuner_range tuner_philips_pal_ranges[] = {
	{ 16 * 168.25 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 447.25 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_philips_pal_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_philips_pal_ranges,
		.count  = ARRAY_SIZE(tuner_philips_pal_ranges),
		.cb_first_if_lower_freq = 1,
	},
};

/* ------------ TUNER_TEMIC_NTSC - TEMIC NTSC ------------ */

static struct tuner_range tuner_temic_ntsc_ranges[] = {
	{ 16 * 157.25 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 463.25 /*MHz*/, 0x8e, 0x04, },
	{ 16 * 999.99        , 0x8e, 0x01, },
};

static struct tuner_params tuner_temic_ntsc_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_temic_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_temic_ntsc_ranges),
	},
};

/* ------------ TUNER_TEMIC_PAL_I - TEMIC PAL_I ------------ */

static struct tuner_range tuner_temic_pal_i_ranges[] = {
	{ 16 * 170.00 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 450.00 /*MHz*/, 0x8e, 0x04, },
	{ 16 * 999.99        , 0x8e, 0x01, },
};

static struct tuner_params tuner_temic_pal_i_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_temic_pal_i_ranges,
		.count  = ARRAY_SIZE(tuner_temic_pal_i_ranges),
	},
};

/* ------------ TUNER_TEMIC_4036FY5_NTSC - TEMIC NTSC ------------ */

static struct tuner_range tuner_temic_4036fy5_ntsc_ranges[] = {
	{ 16 * 157.25 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 463.25 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_temic_4036fy5_ntsc_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_temic_4036fy5_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_temic_4036fy5_ntsc_ranges),
	},
};

/* ------------ TUNER_ALPS_TSBH1_NTSC - TEMIC NTSC ------------ */

static struct tuner_range tuner_alps_tsb_1_ranges[] = {
	{ 16 * 137.25 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 385.25 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x08, },
};

static struct tuner_params tuner_alps_tsbh1_ntsc_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_alps_tsb_1_ranges,
		.count  = ARRAY_SIZE(tuner_alps_tsb_1_ranges),
	},
};

/* 10-19 */
/* ------------ TUNER_ALPS_TSBE1_PAL - TEMIC PAL ------------ */

static struct tuner_params tuner_alps_tsb_1_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_alps_tsb_1_ranges,
		.count  = ARRAY_SIZE(tuner_alps_tsb_1_ranges),
	},
};

/* ------------ TUNER_ALPS_TSBB5_PAL_I - Alps PAL_I ------------ */

static struct tuner_range tuner_alps_tsb_5_pal_ranges[] = {
	{ 16 * 133.25 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 351.25 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x08, },
};

static struct tuner_params tuner_alps_tsbb5_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_alps_tsb_5_pal_ranges,
		.count  = ARRAY_SIZE(tuner_alps_tsb_5_pal_ranges),
	},
};

/* ------------ TUNER_ALPS_TSBE5_PAL - Alps PAL ------------ */

static struct tuner_params tuner_alps_tsbe5_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_alps_tsb_5_pal_ranges,
		.count  = ARRAY_SIZE(tuner_alps_tsb_5_pal_ranges),
	},
};

/* ------------ TUNER_ALPS_TSBC5_PAL - Alps PAL ------------ */

static struct tuner_params tuner_alps_tsbc5_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_alps_tsb_5_pal_ranges,
		.count  = ARRAY_SIZE(tuner_alps_tsb_5_pal_ranges),
	},
};

/* ------------ TUNER_TEMIC_4006FH5_PAL - TEMIC PAL ------------ */

static struct tuner_range tuner_lg_pal_ranges[] = {
	{ 16 * 170.00 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 450.00 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_temic_4006fh5_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_lg_pal_ranges,
		.count  = ARRAY_SIZE(tuner_lg_pal_ranges),
	},
};

/* ------------ TUNER_ALPS_TSHC6_NTSC - Alps NTSC ------------ */

static struct tuner_range tuner_alps_tshc6_ntsc_ranges[] = {
	{ 16 * 137.25 /*MHz*/, 0x8e, 0x14, },
	{ 16 * 385.25 /*MHz*/, 0x8e, 0x12, },
	{ 16 * 999.99        , 0x8e, 0x11, },
};

static struct tuner_params tuner_alps_tshc6_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_alps_tshc6_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_alps_tshc6_ntsc_ranges),
	},
};

/* ------------ TUNER_TEMIC_PAL_DK - TEMIC PAL ------------ */

static struct tuner_range tuner_temic_pal_dk_ranges[] = {
	{ 16 * 168.25 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 456.25 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_temic_pal_dk_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_temic_pal_dk_ranges,
		.count  = ARRAY_SIZE(tuner_temic_pal_dk_ranges),
	},
};

/* ------------ TUNER_PHILIPS_NTSC_M - Philips NTSC ------------ */

static struct tuner_range tuner_philips_ntsc_m_ranges[] = {
	{ 16 * 160.00 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 454.00 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_philips_ntsc_m_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_philips_ntsc_m_ranges,
		.count  = ARRAY_SIZE(tuner_philips_ntsc_m_ranges),
	},
};

/* ------------ TUNER_TEMIC_4066FY5_PAL_I - TEMIC PAL_I ------------ */

static struct tuner_range tuner_temic_40x6f_5_pal_ranges[] = {
	{ 16 * 169.00 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 454.00 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_temic_4066fy5_pal_i_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_temic_40x6f_5_pal_ranges,
		.count  = ARRAY_SIZE(tuner_temic_40x6f_5_pal_ranges),
	},
};

/* ------------ TUNER_TEMIC_4006FN5_MULTI_PAL - TEMIC PAL ------------ */

static struct tuner_params tuner_temic_4006fn5_multi_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_temic_40x6f_5_pal_ranges,
		.count  = ARRAY_SIZE(tuner_temic_40x6f_5_pal_ranges),
	},
};

/* 20-29 */
/* ------------ TUNER_TEMIC_4009FR5_PAL - TEMIC PAL ------------ */

static struct tuner_range tuner_temic_4009f_5_pal_ranges[] = {
	{ 16 * 141.00 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 464.00 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_temic_4009f_5_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_temic_4009f_5_pal_ranges,
		.count  = ARRAY_SIZE(tuner_temic_4009f_5_pal_ranges),
	},
};

/* ------------ TUNER_TEMIC_4039FR5_NTSC - TEMIC NTSC ------------ */

static struct tuner_range tuner_temic_4x3x_f_5_ntsc_ranges[] = {
	{ 16 * 158.00 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 453.00 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_temic_4039fr5_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_temic_4x3x_f_5_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_temic_4x3x_f_5_ntsc_ranges),
	},
};

/* ------------ TUNER_TEMIC_4046FM5 - TEMIC PAL ------------ */

static struct tuner_params tuner_temic_4046fm5_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_temic_40x6f_5_pal_ranges,
		.count  = ARRAY_SIZE(tuner_temic_40x6f_5_pal_ranges),
	},
};

/* ------------ TUNER_PHILIPS_PAL_DK - Philips PAL ------------ */

static struct tuner_params tuner_philips_pal_dk_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_lg_pal_ranges,
		.count  = ARRAY_SIZE(tuner_lg_pal_ranges),
	},
};

/* ------------ TUNER_PHILIPS_FQ1216ME - Philips PAL ------------ */

static struct tuner_params tuner_philips_fq1216me_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_lg_pal_ranges,
		.count  = ARRAY_SIZE(tuner_lg_pal_ranges),
		.has_tda9887 = 1,
		.port1_active = 1,
		.port2_active = 1,
		.port2_invert_for_secam_lc = 1,
	},
};

/* ------------ TUNER_LG_PAL_I_FM - LGINNOTEK PAL_I ------------ */

static struct tuner_params tuner_lg_pal_i_fm_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_lg_pal_ranges,
		.count  = ARRAY_SIZE(tuner_lg_pal_ranges),
	},
};

/* ------------ TUNER_LG_PAL_I - LGINNOTEK PAL_I ------------ */

static struct tuner_params tuner_lg_pal_i_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_lg_pal_ranges,
		.count  = ARRAY_SIZE(tuner_lg_pal_ranges),
	},
};

/* ------------ TUNER_LG_NTSC_FM - LGINNOTEK NTSC ------------ */

static struct tuner_range tuner_lg_ntsc_fm_ranges[] = {
	{ 16 * 210.00 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 497.00 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_lg_ntsc_fm_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_lg_ntsc_fm_ranges,
		.count  = ARRAY_SIZE(tuner_lg_ntsc_fm_ranges),
	},
};

/* ------------ TUNER_LG_PAL_FM - LGINNOTEK PAL ------------ */

static struct tuner_params tuner_lg_pal_fm_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_lg_pal_ranges,
		.count  = ARRAY_SIZE(tuner_lg_pal_ranges),
	},
};

/* ------------ TUNER_LG_PAL - LGINNOTEK PAL ------------ */

static struct tuner_params tuner_lg_pal_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_lg_pal_ranges,
		.count  = ARRAY_SIZE(tuner_lg_pal_ranges),
	},
};

/* 30-39 */
/* ------------ TUNER_TEMIC_4009FN5_MULTI_PAL_FM - TEMIC PAL ------------ */

static struct tuner_params tuner_temic_4009_fn5_multi_pal_fm_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_temic_4009f_5_pal_ranges,
		.count  = ARRAY_SIZE(tuner_temic_4009f_5_pal_ranges),
	},
};

/* ------------ TUNER_SHARP_2U5JF5540_NTSC - SHARP NTSC ------------ */

static struct tuner_range tuner_sharp_2u5jf5540_ntsc_ranges[] = {
	{ 16 * 137.25 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 317.25 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x08, },
};

static struct tuner_params tuner_sharp_2u5jf5540_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_sharp_2u5jf5540_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_sharp_2u5jf5540_ntsc_ranges),
	},
};

/* ------------ TUNER_Samsung_PAL_TCPM9091PD27 - Samsung PAL ------------ */

static struct tuner_range tuner_samsung_pal_tcpm9091pd27_ranges[] = {
	{ 16 * 169 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 464 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99     , 0x8e, 0x30, },
};

static struct tuner_params tuner_samsung_pal_tcpm9091pd27_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_samsung_pal_tcpm9091pd27_ranges,
		.count  = ARRAY_SIZE(tuner_samsung_pal_tcpm9091pd27_ranges),
	},
};

/* ------------ TUNER_TEMIC_4106FH5 - TEMIC PAL ------------ */

static struct tuner_params tuner_temic_4106fh5_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_temic_4009f_5_pal_ranges,
		.count  = ARRAY_SIZE(tuner_temic_4009f_5_pal_ranges),
	},
};

/* ------------ TUNER_TEMIC_4012FY5 - TEMIC PAL ------------ */

static struct tuner_params tuner_temic_4012fy5_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_temic_pal_ranges,
		.count  = ARRAY_SIZE(tuner_temic_pal_ranges),
	},
};

/* ------------ TUNER_TEMIC_4136FY5 - TEMIC NTSC ------------ */

static struct tuner_params tuner_temic_4136_fy5_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_temic_4x3x_f_5_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_temic_4x3x_f_5_ntsc_ranges),
	},
};

/* ------------ TUNER_LG_PAL_NEW_TAPC - LGINNOTEK PAL ------------ */

static struct tuner_range tuner_lg_new_tapc_ranges[] = {
	{ 16 * 170.00 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 450.00 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x08, },
};

static struct tuner_params tuner_lg_pal_new_tapc_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_lg_new_tapc_ranges,
		.count  = ARRAY_SIZE(tuner_lg_new_tapc_ranges),
	},
};

/* ------------ TUNER_PHILIPS_FM1216ME_MK3 - Philips PAL ------------ */

static struct tuner_range tuner_fm1216me_mk3_pal_ranges[] = {
	{ 16 * 158.00 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 442.00 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x04, },
};

static struct tuner_params tuner_fm1216me_mk3_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_fm1216me_mk3_pal_ranges,
		.count  = ARRAY_SIZE(tuner_fm1216me_mk3_pal_ranges),
		.cb_first_if_lower_freq = 1,
		.has_tda9887 = 1,
		.port1_active = 1,
		.port2_active = 1,
		.port2_invert_for_secam_lc = 1,
		.port1_fm_high_sensitivity = 1,
		.default_top_mid = -2,
		.default_top_secam_mid = -2,
		.default_top_secam_high = -2,
	},
};

/* ------------ TUNER_LG_NTSC_NEW_TAPC - LGINNOTEK NTSC ------------ */

static struct tuner_params tuner_lg_ntsc_new_tapc_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_lg_new_tapc_ranges,
		.count  = ARRAY_SIZE(tuner_lg_new_tapc_ranges),
	},
};

/* 40-49 */
/* ------------ TUNER_HITACHI_NTSC - HITACHI NTSC ------------ */

static struct tuner_params tuner_hitachi_ntsc_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_lg_new_tapc_ranges,
		.count  = ARRAY_SIZE(tuner_lg_new_tapc_ranges),
	},
};

/* ------------ TUNER_PHILIPS_PAL_MK - Philips PAL ------------ */

static struct tuner_range tuner_philips_pal_mk_pal_ranges[] = {
	{ 16 * 140.25 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 463.25 /*MHz*/, 0x8e, 0xc2, },
	{ 16 * 999.99        , 0x8e, 0xcf, },
};

static struct tuner_params tuner_philips_pal_mk_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_philips_pal_mk_pal_ranges,
		.count  = ARRAY_SIZE(tuner_philips_pal_mk_pal_ranges),
	},
};

/* ---- TUNER_PHILIPS_ATSC - Philips FCV1236D (ATSC/NTSC) ---- */

static struct tuner_range tuner_philips_fcv1236d_ranges[] = {
	{ 16 * 157.25 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 451.25 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_philips_fcv1236d_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_philips_fcv1236d_ranges,
		.count  = ARRAY_SIZE(tuner_philips_fcv1236d_ranges),
	},
};

/* ------------ TUNER_PHILIPS_FM1236_MK3 - Philips NTSC ------------ */

static struct tuner_range tuner_fm1236_mk3_ntsc_ranges[] = {
	{ 16 * 160.00 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 442.00 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x04, },
};

static struct tuner_params tuner_fm1236_mk3_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_fm1236_mk3_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_fm1236_mk3_ntsc_ranges),
		.cb_first_if_lower_freq = 1,
		.has_tda9887 = 1,
		.port1_active = 1,
		.port2_active = 1,
		.port1_fm_high_sensitivity = 1,
	},
};

/* ------------ TUNER_PHILIPS_4IN1 - Philips NTSC ------------ */

static struct tuner_params tuner_philips_4in1_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_fm1236_mk3_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_fm1236_mk3_ntsc_ranges),
	},
};

/* ------------ TUNER_MICROTUNE_4049FM5 - Microtune PAL ------------ */

static struct tuner_params tuner_microtune_4049_fm5_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_temic_4009f_5_pal_ranges,
		.count  = ARRAY_SIZE(tuner_temic_4009f_5_pal_ranges),
		.has_tda9887 = 1,
		.port1_invert_for_secam_lc = 1,
		.default_pll_gating_18 = 1,
		.fm_gain_normal=1,
	},
};

/* ------------ TUNER_PANASONIC_VP27 - Panasonic NTSC ------------ */

static struct tuner_range tuner_panasonic_vp27_ntsc_ranges[] = {
	{ 16 * 160.00 /*MHz*/, 0xce, 0x01, },
	{ 16 * 454.00 /*MHz*/, 0xce, 0x02, },
	{ 16 * 999.99        , 0xce, 0x08, },
};

static struct tuner_params tuner_panasonic_vp27_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_panasonic_vp27_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_panasonic_vp27_ntsc_ranges),
		.has_tda9887 = 1,
		.intercarrier_mode = 1,
	},
};

/* ------------ TUNER_TNF_8831BGFF - Philips PAL ------------ */

static struct tuner_range tuner_tnf_8831bgff_pal_ranges[] = {
	{ 16 * 161.25 /*MHz*/, 0x8e, 0xa0, },
	{ 16 * 463.25 /*MHz*/, 0x8e, 0x90, },
	{ 16 * 999.99        , 0x8e, 0x30, },
};

static struct tuner_params tuner_tnf_8831bgff_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_tnf_8831bgff_pal_ranges,
		.count  = ARRAY_SIZE(tuner_tnf_8831bgff_pal_ranges),
	},
};

/* ------------ TUNER_MICROTUNE_4042FI5 - Microtune NTSC ------------ */

static struct tuner_range tuner_microtune_4042fi5_ntsc_ranges[] = {
	{ 16 * 162.00 /*MHz*/, 0x8e, 0xa2, },
	{ 16 * 457.00 /*MHz*/, 0x8e, 0x94, },
	{ 16 * 999.99        , 0x8e, 0x31, },
};

static struct tuner_params tuner_microtune_4042fi5_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_microtune_4042fi5_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_microtune_4042fi5_ntsc_ranges),
	},
};

/* 50-59 */
/* ------------ TUNER_TCL_2002N - TCL NTSC ------------ */

static struct tuner_range tuner_tcl_2002n_ntsc_ranges[] = {
	{ 16 * 172.00 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 448.00 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x08, },
};

static struct tuner_params tuner_tcl_2002n_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_tcl_2002n_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_tcl_2002n_ntsc_ranges),
		.cb_first_if_lower_freq = 1,
	},
};

/* ------------ TUNER_PHILIPS_FM1256_IH3 - Philips PAL ------------ */

static struct tuner_params tuner_philips_fm1256_ih3_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_fm1236_mk3_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_fm1236_mk3_ntsc_ranges),
	},
};

/* ------------ TUNER_THOMSON_DTT7610 - THOMSON ATSC ------------ */

static struct tuner_range tuner_thomson_dtt7610_ntsc_ranges[] = {
	{ 16 * 157.25 /*MHz*/, 0x8e, 0x39, },
	{ 16 * 454.00 /*MHz*/, 0x8e, 0x3a, },
	{ 16 * 999.99        , 0x8e, 0x3c, },
};

static struct tuner_params tuner_thomson_dtt7610_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_thomson_dtt7610_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_thomson_dtt7610_ntsc_ranges),
	},
};

/* ------------ TUNER_PHILIPS_FQ1286 - Philips NTSC ------------ */

static struct tuner_range tuner_philips_fq1286_ntsc_ranges[] = {
	{ 16 * 160.00 /*MHz*/, 0x8e, 0x41, },
	{ 16 * 454.00 /*MHz*/, 0x8e, 0x42, },
	{ 16 * 999.99        , 0x8e, 0x04, },
};

static struct tuner_params tuner_philips_fq1286_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_philips_fq1286_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_philips_fq1286_ntsc_ranges),
	},
};

/* ------------ TUNER_TCL_2002MB - TCL PAL ------------ */

static struct tuner_range tuner_tcl_2002mb_pal_ranges[] = {
	{ 16 * 170.00 /*MHz*/, 0xce, 0x01, },
	{ 16 * 450.00 /*MHz*/, 0xce, 0x02, },
	{ 16 * 999.99        , 0xce, 0x08, },
};

static struct tuner_params tuner_tcl_2002mb_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_tcl_2002mb_pal_ranges,
		.count  = ARRAY_SIZE(tuner_tcl_2002mb_pal_ranges),
	},
};

/* ------------ TUNER_PHILIPS_FQ1216AME_MK4 - Philips PAL ------------ */

static struct tuner_range tuner_philips_fq12_6a___mk4_pal_ranges[] = {
	{ 16 * 160.00 /*MHz*/, 0xce, 0x01, },
	{ 16 * 442.00 /*MHz*/, 0xce, 0x02, },
	{ 16 * 999.99        , 0xce, 0x04, },
};

static struct tuner_params tuner_philips_fq1216ame_mk4_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_philips_fq12_6a___mk4_pal_ranges,
		.count  = ARRAY_SIZE(tuner_philips_fq12_6a___mk4_pal_ranges),
		.has_tda9887 = 1,
		.port1_active = 1,
		.port2_invert_for_secam_lc = 1,
		.default_top_mid = -2,
		.default_top_secam_low = -2,
		.default_top_secam_mid = -2,
		.default_top_secam_high = -2,
	},
};

/* ------------ TUNER_PHILIPS_FQ1236A_MK4 - Philips NTSC ------------ */

static struct tuner_params tuner_philips_fq1236a_mk4_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_fm1236_mk3_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_fm1236_mk3_ntsc_ranges),
	},
};

/* ------------ TUNER_YMEC_TVF_8531MF - Philips NTSC ------------ */

static struct tuner_params tuner_ymec_tvf_8531mf_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_philips_ntsc_m_ranges,
		.count  = ARRAY_SIZE(tuner_philips_ntsc_m_ranges),
	},
};

/* ------------ TUNER_YMEC_TVF_5533MF - Philips NTSC ------------ */

static struct tuner_range tuner_ymec_tvf_5533mf_ntsc_ranges[] = {
	{ 16 * 160.00 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 454.00 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x04, },
};

static struct tuner_params tuner_ymec_tvf_5533mf_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_ymec_tvf_5533mf_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_ymec_tvf_5533mf_ntsc_ranges),
	},
};

/* 60-69 */
/* ------------ TUNER_THOMSON_DTT761X - THOMSON ATSC ------------ */
/* DTT 7611 7611A 7612 7613 7613A 7614 7615 7615A */

static struct tuner_range tuner_thomson_dtt761x_ntsc_ranges[] = {
	{ 16 * 145.25 /*MHz*/, 0x8e, 0x39, },
	{ 16 * 415.25 /*MHz*/, 0x8e, 0x3a, },
	{ 16 * 999.99        , 0x8e, 0x3c, },
};


static struct tuner_params tuner_thomson_dtt761x_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_thomson_dtt761x_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_thomson_dtt761x_ntsc_ranges),
	},
};

/* ------------ TUNER_TENA_9533_DI - Philips PAL ------------ */

static struct tuner_range tuner_tena_9533_di_pal_ranges[] = {
	{ 16 * 160.25 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 464.25 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x04, },
};

static struct tuner_params tuner_tena_9533_di_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_tena_9533_di_pal_ranges,
		.count  = ARRAY_SIZE(tuner_tena_9533_di_pal_ranges),
	},
};

/* ------------ TUNER_PHILIPS_FMD1216ME_MK3 - Philips PAL ------------ */

static struct tuner_range tuner_philips_fmd1216me_mk3_pal_ranges[] = {
	{ 16 * 160.00 /*MHz*/, 0x86, 0x51, },
	{ 16 * 442.00 /*MHz*/, 0x86, 0x52, },
	{ 16 * 999.99        , 0x86, 0x54, },
};


static struct tuner_params tuner_philips_fmd1216me_mk3_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_philips_fmd1216me_mk3_pal_ranges,
		.count  = ARRAY_SIZE(tuner_philips_fmd1216me_mk3_pal_ranges),
		.has_tda9887 = 1,
		.port1_active = 1,
		.port2_active = 1,
		.port2_fm_high_sensitivity = 1,
		.port2_invert_for_secam_lc = 1,
		.port1_set_for_fm_mono = 1,
	},
};


/* ------ TUNER_LG_TDVS_H06XF - LG INNOTEK / INFINEON ATSC ----- */

static struct tuner_range tuner_tua6034_ntsc_ranges[] = {
	{ 16 * 165.00 /*MHz*/, 0x8e, 0x01 },
	{ 16 * 450.00 /*MHz*/, 0x8e, 0x02 },
	{ 16 * 999.99        , 0x8e, 0x04 },
};


static struct tuner_params tuner_lg_tdvs_h06xf_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_tua6034_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_tua6034_ntsc_ranges),
	},
};

/* ------------ TUNER_YMEC_TVF66T5_B_DFF - Philips PAL ------------ */

static struct tuner_range tuner_ymec_tvf66t5_b_dff_pal_ranges[] = {
	{ 16 * 160.25 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 464.25 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x08, },
};

static struct tuner_params tuner_ymec_tvf66t5_b_dff_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_ymec_tvf66t5_b_dff_pal_ranges,
		.count  = ARRAY_SIZE(tuner_ymec_tvf66t5_b_dff_pal_ranges),
	},
};

/* ------------ TUNER_LG_NTSC_TALN_MINI - LGINNOTEK NTSC ------------ */

static struct tuner_range tuner_lg_taln_ntsc_ranges[] = {
	{ 16 * 137.25 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 373.25 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x08, },
};

static struct tuner_range tuner_lg_taln_pal_secam_ranges[] = {
	{ 16 * 150.00 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 425.00 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x08, },
};

static struct tuner_params tuner_lg_taln_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_lg_taln_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_lg_taln_ntsc_ranges),
	},{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_lg_taln_pal_secam_ranges,
		.count  = ARRAY_SIZE(tuner_lg_taln_pal_secam_ranges),
	},
};

/* ------------ TUNER_PHILIPS_TD1316 - Philips PAL ------------ */

static struct tuner_range tuner_philips_td1316_pal_ranges[] = {
	{ 16 * 160.00 /*MHz*/, 0xc8, 0xa1, },
	{ 16 * 442.00 /*MHz*/, 0xc8, 0xa2, },
	{ 16 * 999.99        , 0xc8, 0xa4, },
};

static struct tuner_params tuner_philips_td1316_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_philips_td1316_pal_ranges,
		.count  = ARRAY_SIZE(tuner_philips_td1316_pal_ranges),
	},
};

/* ------------ TUNER_PHILIPS_TUV1236D - Philips ATSC ------------ */

static struct tuner_range tuner_tuv1236d_ntsc_ranges[] = {
	{ 16 * 157.25 /*MHz*/, 0xce, 0x01, },
	{ 16 * 454.00 /*MHz*/, 0xce, 0x02, },
	{ 16 * 999.99        , 0xce, 0x04, },
};


static struct tuner_params tuner_tuv1236d_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_tuv1236d_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_tuv1236d_ntsc_ranges),
	},
};

/* ------------ TUNER_TNF_xxx5  - Texas Instruments--------- */
/* This is known to work with Tenna TVF58t5-MFF and TVF5835 MFF
 *	but it is expected to work also with other Tenna/Ymec
 *	models based on TI SN 761677 chip on both PAL and NTSC
 */

static struct tuner_range tuner_tnf_5335_d_if_pal_ranges[] = {
	{ 16 * 168.25 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 471.25 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x08, },
};

static struct tuner_range tuner_tnf_5335mf_ntsc_ranges[] = {
	{ 16 * 169.25 /*MHz*/, 0x8e, 0x01, },
	{ 16 * 469.25 /*MHz*/, 0x8e, 0x02, },
	{ 16 * 999.99        , 0x8e, 0x08, },
};

static struct tuner_params tuner_tnf_5335mf_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_tnf_5335mf_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_tnf_5335mf_ntsc_ranges),
	},
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_tnf_5335_d_if_pal_ranges,
		.count  = ARRAY_SIZE(tuner_tnf_5335_d_if_pal_ranges),
	},
};

/* 70-79 */
/* ------------ TUNER_SAMSUNG_TCPN_2121P30A - Samsung NTSC ------------ */

/* '+ 4' turns on the Low Noise Amplifier */
static struct tuner_range tuner_samsung_tcpn_2121p30a_ntsc_ranges[] = {
	{ 16 * 130.00 /*MHz*/, 0xce, 0x01 + 4, },
	{ 16 * 364.50 /*MHz*/, 0xce, 0x02 + 4, },
	{ 16 * 999.99        , 0xce, 0x08 + 4, },
};

static struct tuner_params tuner_samsung_tcpn_2121p30a_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_NTSC,
		.ranges = tuner_samsung_tcpn_2121p30a_ntsc_ranges,
		.count  = ARRAY_SIZE(tuner_samsung_tcpn_2121p30a_ntsc_ranges),
	},
};

/* ------------ TUNER_THOMSON_FE6600 - DViCO Hybrid PAL ------------ */

static struct tuner_range tuner_thomson_fe6600_ranges[] = {
	{ 16 * 160.00 /*MHz*/, 0xfe, 0x11, },
	{ 16 * 442.00 /*MHz*/, 0xf6, 0x12, },
	{ 16 * 999.99        , 0xf6, 0x18, },
};

static struct tuner_params tuner_thomson_fe6600_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_thomson_fe6600_ranges,
		.count  = ARRAY_SIZE(tuner_thomson_fe6600_ranges),
	},
};

/* ------------ TUNER_SAMSUNG_TCPG_6121P30A - Samsung PAL ------------ */

/* '+ 4' turns on the Low Noise Amplifier */
static struct tuner_range tuner_samsung_tcpg_6121p30a_pal_ranges[] = {
	{ 16 * 146.25 /*MHz*/, 0xce, 0x01 + 4, },
	{ 16 * 428.50 /*MHz*/, 0xce, 0x02 + 4, },
	{ 16 * 999.99        , 0xce, 0x08 + 4, },
};

static struct tuner_params tuner_samsung_tcpg_6121p30a_params[] = {
	{
		.type   = TUNER_PARAM_TYPE_PAL,
		.ranges = tuner_samsung_tcpg_6121p30a_pal_ranges,
		.count  = ARRAY_SIZE(tuner_samsung_tcpg_6121p30a_pal_ranges),
		.has_tda9887 = 1,
		.port1_active = 1,
		.port2_active = 1,
		.port2_invert_for_secam_lc = 1,
	},
};

/* --------------------------------------------------------------------- */

struct tunertype tuners[] = {
	/* 0-9 */
	[TUNER_TEMIC_PAL] = { /* TEMIC PAL */
		.name   = "Temic PAL (4002 FH5)",
		.params = tuner_temic_pal_params,
		.count  = ARRAY_SIZE(tuner_temic_pal_params),
	},
	[TUNER_PHILIPS_PAL_I] = { /* Philips PAL_I */
		.name   = "Philips PAL_I (FI1246 and compatibles)",
		.params = tuner_philips_pal_i_params,
		.count  = ARRAY_SIZE(tuner_philips_pal_i_params),
	},
	[TUNER_PHILIPS_NTSC] = { /* Philips NTSC */
		.name   = "Philips NTSC (FI1236,FM1236 and compatibles)",
		.params = tuner_philips_ntsc_params,
		.count  = ARRAY_SIZE(tuner_philips_ntsc_params),
	},
	[TUNER_PHILIPS_SECAM] = { /* Philips SECAM */
		.name   = "Philips (SECAM+PAL_BG) (FI1216MF, FM1216MF, FR1216MF)",
		.params = tuner_philips_secam_params,
		.count  = ARRAY_SIZE(tuner_philips_secam_params),
	},
	[TUNER_ABSENT] = { /* Tuner Absent */
		.name   = "NoTuner",
	},
	[TUNER_PHILIPS_PAL] = { /* Philips PAL */
		.name   = "Philips PAL_BG (FI1216 and compatibles)",
		.params = tuner_philips_pal_params,
		.count  = ARRAY_SIZE(tuner_philips_pal_params),
	},
	[TUNER_TEMIC_NTSC] = { /* TEMIC NTSC */
		.name   = "Temic NTSC (4032 FY5)",
		.params = tuner_temic_ntsc_params,
		.count  = ARRAY_SIZE(tuner_temic_ntsc_params),
	},
	[TUNER_TEMIC_PAL_I] = { /* TEMIC PAL_I */
		.name   = "Temic PAL_I (4062 FY5)",
		.params = tuner_temic_pal_i_params,
		.count  = ARRAY_SIZE(tuner_temic_pal_i_params),
	},
	[TUNER_TEMIC_4036FY5_NTSC] = { /* TEMIC NTSC */
		.name   = "Temic NTSC (4036 FY5)",
		.params = tuner_temic_4036fy5_ntsc_params,
		.count  = ARRAY_SIZE(tuner_temic_4036fy5_ntsc_params),
	},
	[TUNER_ALPS_TSBH1_NTSC] = { /* TEMIC NTSC */
		.name   = "Alps HSBH1",
		.params = tuner_alps_tsbh1_ntsc_params,
		.count  = ARRAY_SIZE(tuner_alps_tsbh1_ntsc_params),
	},

	/* 10-19 */
	[TUNER_ALPS_TSBE1_PAL] = { /* TEMIC PAL */
		.name   = "Alps TSBE1",
		.params = tuner_alps_tsb_1_params,
		.count  = ARRAY_SIZE(tuner_alps_tsb_1_params),
	},
	[TUNER_ALPS_TSBB5_PAL_I] = { /* Alps PAL_I */
		.name   = "Alps TSBB5",
		.params = tuner_alps_tsbb5_params,
		.count  = ARRAY_SIZE(tuner_alps_tsbb5_params),
	},
	[TUNER_ALPS_TSBE5_PAL] = { /* Alps PAL */
		.name   = "Alps TSBE5",
		.params = tuner_alps_tsbe5_params,
		.count  = ARRAY_SIZE(tuner_alps_tsbe5_params),
	},
	[TUNER_ALPS_TSBC5_PAL] = { /* Alps PAL */
		.name   = "Alps TSBC5",
		.params = tuner_alps_tsbc5_params,
		.count  = ARRAY_SIZE(tuner_alps_tsbc5_params),
	},
	[TUNER_TEMIC_4006FH5_PAL] = { /* TEMIC PAL */
		.name   = "Temic PAL_BG (4006FH5)",
		.params = tuner_temic_4006fh5_params,
		.count  = ARRAY_SIZE(tuner_temic_4006fh5_params),
	},
	[TUNER_ALPS_TSHC6_NTSC] = { /* Alps NTSC */
		.name   = "Alps TSCH6",
		.params = tuner_alps_tshc6_params,
		.count  = ARRAY_SIZE(tuner_alps_tshc6_params),
	},
	[TUNER_TEMIC_PAL_DK] = { /* TEMIC PAL */
		.name   = "Temic PAL_DK (4016 FY5)",
		.params = tuner_temic_pal_dk_params,
		.count  = ARRAY_SIZE(tuner_temic_pal_dk_params),
	},
	[TUNER_PHILIPS_NTSC_M] = { /* Philips NTSC */
		.name   = "Philips NTSC_M (MK2)",
		.params = tuner_philips_ntsc_m_params,
		.count  = ARRAY_SIZE(tuner_philips_ntsc_m_params),
	},
	[TUNER_TEMIC_4066FY5_PAL_I] = { /* TEMIC PAL_I */
		.name   = "Temic PAL_I (4066 FY5)",
		.params = tuner_temic_4066fy5_pal_i_params,
		.count  = ARRAY_SIZE(tuner_temic_4066fy5_pal_i_params),
	},
	[TUNER_TEMIC_4006FN5_MULTI_PAL] = { /* TEMIC PAL */
		.name   = "Temic PAL* auto (4006 FN5)",
		.params = tuner_temic_4006fn5_multi_params,
		.count  = ARRAY_SIZE(tuner_temic_4006fn5_multi_params),
	},

	/* 20-29 */
	[TUNER_TEMIC_4009FR5_PAL] = { /* TEMIC PAL */
		.name   = "Temic PAL_BG (4009 FR5) or PAL_I (4069 FR5)",
		.params = tuner_temic_4009f_5_params,
		.count  = ARRAY_SIZE(tuner_temic_4009f_5_params),
	},
	[TUNER_TEMIC_4039FR5_NTSC] = { /* TEMIC NTSC */
		.name   = "Temic NTSC (4039 FR5)",
		.params = tuner_temic_4039fr5_params,
		.count  = ARRAY_SIZE(tuner_temic_4039fr5_params),
	},
	[TUNER_TEMIC_4046FM5] = { /* TEMIC PAL */
		.name   = "Temic PAL/SECAM multi (4046 FM5)",
		.params = tuner_temic_4046fm5_params,
		.count  = ARRAY_SIZE(tuner_temic_4046fm5_params),
	},
	[TUNER_PHILIPS_PAL_DK] = { /* Philips PAL */
		.name   = "Philips PAL_DK (FI1256 and compatibles)",
		.params = tuner_philips_pal_dk_params,
		.count  = ARRAY_SIZE(tuner_philips_pal_dk_params),
	},
	[TUNER_PHILIPS_FQ1216ME] = { /* Philips PAL */
		.name   = "Philips PAL/SECAM multi (FQ1216ME)",
		.params = tuner_philips_fq1216me_params,
		.count  = ARRAY_SIZE(tuner_philips_fq1216me_params),
	},
	[TUNER_LG_PAL_I_FM] = { /* LGINNOTEK PAL_I */
		.name   = "LG PAL_I+FM (TAPC-I001D)",
		.params = tuner_lg_pal_i_fm_params,
		.count  = ARRAY_SIZE(tuner_lg_pal_i_fm_params),
	},
	[TUNER_LG_PAL_I] = { /* LGINNOTEK PAL_I */
		.name   = "LG PAL_I (TAPC-I701D)",
		.params = tuner_lg_pal_i_params,
		.count  = ARRAY_SIZE(tuner_lg_pal_i_params),
	},
	[TUNER_LG_NTSC_FM] = { /* LGINNOTEK NTSC */
		.name   = "LG NTSC+FM (TPI8NSR01F)",
		.params = tuner_lg_ntsc_fm_params,
		.count  = ARRAY_SIZE(tuner_lg_ntsc_fm_params),
	},
	[TUNER_LG_PAL_FM] = { /* LGINNOTEK PAL */
		.name   = "LG PAL_BG+FM (TPI8PSB01D)",
		.params = tuner_lg_pal_fm_params,
		.count  = ARRAY_SIZE(tuner_lg_pal_fm_params),
	},
	[TUNER_LG_PAL] = { /* LGINNOTEK PAL */
		.name   = "LG PAL_BG (TPI8PSB11D)",
		.params = tuner_lg_pal_params,
		.count  = ARRAY_SIZE(tuner_lg_pal_params),
	},

	/* 30-39 */
	[TUNER_TEMIC_4009FN5_MULTI_PAL_FM] = { /* TEMIC PAL */
		.name   = "Temic PAL* auto + FM (4009 FN5)",
		.params = tuner_temic_4009_fn5_multi_pal_fm_params,
		.count  = ARRAY_SIZE(tuner_temic_4009_fn5_multi_pal_fm_params),
	},
	[TUNER_SHARP_2U5JF5540_NTSC] = { /* SHARP NTSC */
		.name   = "SHARP NTSC_JP (2U5JF5540)",
		.params = tuner_sharp_2u5jf5540_params,
		.count  = ARRAY_SIZE(tuner_sharp_2u5jf5540_params),
	},
	[TUNER_Samsung_PAL_TCPM9091PD27] = { /* Samsung PAL */
		.name   = "Samsung PAL TCPM9091PD27",
		.params = tuner_samsung_pal_tcpm9091pd27_params,
		.count  = ARRAY_SIZE(tuner_samsung_pal_tcpm9091pd27_params),
	},
	[TUNER_MT2032] = { /* Microtune PAL|NTSC */
		.name   = "MT20xx universal",
		/* see mt20xx.c for details */ },
	[TUNER_TEMIC_4106FH5] = { /* TEMIC PAL */
		.name   = "Temic PAL_BG (4106 FH5)",
		.params = tuner_temic_4106fh5_params,
		.count  = ARRAY_SIZE(tuner_temic_4106fh5_params),
	},
	[TUNER_TEMIC_4012FY5] = { /* TEMIC PAL */
		.name   = "Temic PAL_DK/SECAM_L (4012 FY5)",
		.params = tuner_temic_4012fy5_params,
		.count  = ARRAY_SIZE(tuner_temic_4012fy5_params),
	},
	[TUNER_TEMIC_4136FY5] = { /* TEMIC NTSC */
		.name   = "Temic NTSC (4136 FY5)",
		.params = tuner_temic_4136_fy5_params,
		.count  = ARRAY_SIZE(tuner_temic_4136_fy5_params),
	},
	[TUNER_LG_PAL_NEW_TAPC] = { /* LGINNOTEK PAL */
		.name   = "LG PAL (newer TAPC series)",
		.params = tuner_lg_pal_new_tapc_params,
		.count  = ARRAY_SIZE(tuner_lg_pal_new_tapc_params),
	},
	[TUNER_PHILIPS_FM1216ME_MK3] = { /* Philips PAL */
		.name   = "Philips PAL/SECAM multi (FM1216ME MK3)",
		.params = tuner_fm1216me_mk3_params,
		.count  = ARRAY_SIZE(tuner_fm1216me_mk3_params),
	},
	[TUNER_LG_NTSC_NEW_TAPC] = { /* LGINNOTEK NTSC */
		.name   = "LG NTSC (newer TAPC series)",
		.params = tuner_lg_ntsc_new_tapc_params,
		.count  = ARRAY_SIZE(tuner_lg_ntsc_new_tapc_params),
	},

	/* 40-49 */
	[TUNER_HITACHI_NTSC] = { /* HITACHI NTSC */
		.name   = "HITACHI V7-J180AT",
		.params = tuner_hitachi_ntsc_params,
		.count  = ARRAY_SIZE(tuner_hitachi_ntsc_params),
	},
	[TUNER_PHILIPS_PAL_MK] = { /* Philips PAL */
		.name   = "Philips PAL_MK (FI1216 MK)",
		.params = tuner_philips_pal_mk_params,
		.count  = ARRAY_SIZE(tuner_philips_pal_mk_params),
	},
	[TUNER_PHILIPS_ATSC] = { /* Philips ATSC */
		.name   = "Philips FCV1236D ATSC/NTSC dual in",
		.params = tuner_philips_fcv1236d_params,
		.count  = ARRAY_SIZE(tuner_philips_fcv1236d_params),
	},
	[TUNER_PHILIPS_FM1236_MK3] = { /* Philips NTSC */
		.name   = "Philips NTSC MK3 (FM1236MK3 or FM1236/F)",
		.params = tuner_fm1236_mk3_params,
		.count  = ARRAY_SIZE(tuner_fm1236_mk3_params),
	},
	[TUNER_PHILIPS_4IN1] = { /* Philips NTSC */
		.name   = "Philips 4 in 1 (ATI TV Wonder Pro/Conexant)",
		.params = tuner_philips_4in1_params,
		.count  = ARRAY_SIZE(tuner_philips_4in1_params),
	},
	[TUNER_MICROTUNE_4049FM5] = { /* Microtune PAL */
		.name   = "Microtune 4049 FM5",
		.params = tuner_microtune_4049_fm5_params,
		.count  = ARRAY_SIZE(tuner_microtune_4049_fm5_params),
	},
	[TUNER_PANASONIC_VP27] = { /* Panasonic NTSC */
		.name   = "Panasonic VP27s/ENGE4324D",
		.params = tuner_panasonic_vp27_params,
		.count  = ARRAY_SIZE(tuner_panasonic_vp27_params),
	},
	[TUNER_LG_NTSC_TAPE] = { /* LGINNOTEK NTSC */
		.name   = "LG NTSC (TAPE series)",
		.params = tuner_fm1236_mk3_params,
		.count  = ARRAY_SIZE(tuner_fm1236_mk3_params),
	},
	[TUNER_TNF_8831BGFF] = { /* Philips PAL */
		.name   = "Tenna TNF 8831 BGFF)",
		.params = tuner_tnf_8831bgff_params,
		.count  = ARRAY_SIZE(tuner_tnf_8831bgff_params),
	},
	[TUNER_MICROTUNE_4042FI5] = { /* Microtune NTSC */
		.name   = "Microtune 4042 FI5 ATSC/NTSC dual in",
		.params = tuner_microtune_4042fi5_params,
		.count  = ARRAY_SIZE(tuner_microtune_4042fi5_params),
	},

	/* 50-59 */
	[TUNER_TCL_2002N] = { /* TCL NTSC */
		.name   = "TCL 2002N",
		.params = tuner_tcl_2002n_params,
		.count  = ARRAY_SIZE(tuner_tcl_2002n_params),
	},
	[TUNER_PHILIPS_FM1256_IH3] = { /* Philips PAL */
		.name   = "Philips PAL/SECAM_D (FM 1256 I-H3)",
		.params = tuner_philips_fm1256_ih3_params,
		.count  = ARRAY_SIZE(tuner_philips_fm1256_ih3_params),
	},
	[TUNER_THOMSON_DTT7610] = { /* THOMSON ATSC */
		.name   = "Thomson DTT 7610 (ATSC/NTSC)",
		.params = tuner_thomson_dtt7610_params,
		.count  = ARRAY_SIZE(tuner_thomson_dtt7610_params),
	},
	[TUNER_PHILIPS_FQ1286] = { /* Philips NTSC */
		.name   = "Philips FQ1286",
		.params = tuner_philips_fq1286_params,
		.count  = ARRAY_SIZE(tuner_philips_fq1286_params),
	},
	[TUNER_PHILIPS_TDA8290] = { /* Philips PAL|NTSC */
		.name   = "tda8290+75",
		/* see tda8290.c for details */ },
	[TUNER_TCL_2002MB] = { /* TCL PAL */
		.name   = "TCL 2002MB",
		.params = tuner_tcl_2002mb_params,
		.count  = ARRAY_SIZE(tuner_tcl_2002mb_params),
	},
	[TUNER_PHILIPS_FQ1216AME_MK4] = { /* Philips PAL */
		.name   = "Philips PAL/SECAM multi (FQ1216AME MK4)",
		.params = tuner_philips_fq1216ame_mk4_params,
		.count  = ARRAY_SIZE(tuner_philips_fq1216ame_mk4_params),
	},
	[TUNER_PHILIPS_FQ1236A_MK4] = { /* Philips NTSC */
		.name   = "Philips FQ1236A MK4",
		.params = tuner_philips_fq1236a_mk4_params,
		.count  = ARRAY_SIZE(tuner_philips_fq1236a_mk4_params),
	},
	[TUNER_YMEC_TVF_8531MF] = { /* Philips NTSC */
		.name   = "Ymec TVision TVF-8531MF/8831MF/8731MF",
		.params = tuner_ymec_tvf_8531mf_params,
		.count  = ARRAY_SIZE(tuner_ymec_tvf_8531mf_params),
	},
	[TUNER_YMEC_TVF_5533MF] = { /* Philips NTSC */
		.name   = "Ymec TVision TVF-5533MF",
		.params = tuner_ymec_tvf_5533mf_params,
		.count  = ARRAY_SIZE(tuner_ymec_tvf_5533mf_params),
	},

	/* 60-69 */
	[TUNER_THOMSON_DTT761X] = { /* THOMSON ATSC */
		/* DTT 7611 7611A 7612 7613 7613A 7614 7615 7615A */
		.name   = "Thomson DTT 761X (ATSC/NTSC)",
		.params = tuner_thomson_dtt761x_params,
		.count  = ARRAY_SIZE(tuner_thomson_dtt761x_params),
	},
	[TUNER_TENA_9533_DI] = { /* Philips PAL */
		.name   = "Tena TNF9533-D/IF/TNF9533-B/DF",
		.params = tuner_tena_9533_di_params,
		.count  = ARRAY_SIZE(tuner_tena_9533_di_params),
	},
	[TUNER_TEA5767] = { /* Philips RADIO */
		.name   = "Philips TEA5767HN FM Radio",
		/* see tea5767.c for details */
	},
	[TUNER_PHILIPS_FMD1216ME_MK3] = { /* Philips PAL */
		.name   = "Philips FMD1216ME MK3 Hybrid Tuner",
		.params = tuner_philips_fmd1216me_mk3_params,
		.count  = ARRAY_SIZE(tuner_philips_fmd1216me_mk3_params),
	},
	[TUNER_LG_TDVS_H06XF] = { /* LGINNOTEK ATSC */
		.name   = "LG TDVS-H06xF", /* H061F, H062F & H064F */
		.params = tuner_lg_tdvs_h06xf_params,
		.count  = ARRAY_SIZE(tuner_lg_tdvs_h06xf_params),
	},
	[TUNER_YMEC_TVF66T5_B_DFF] = { /* Philips PAL */
		.name   = "Ymec TVF66T5-B/DFF",
		.params = tuner_ymec_tvf66t5_b_dff_params,
		.count  = ARRAY_SIZE(tuner_ymec_tvf66t5_b_dff_params),
	},
	[TUNER_LG_TALN] = { /* LGINNOTEK NTSC / PAL / SECAM */
		.name   = "LG TALN series",
		.params = tuner_lg_taln_params,
		.count  = ARRAY_SIZE(tuner_lg_taln_params),
	},
	[TUNER_PHILIPS_TD1316] = { /* Philips PAL */
		.name   = "Philips TD1316 Hybrid Tuner",
		.params = tuner_philips_td1316_params,
		.count  = ARRAY_SIZE(tuner_philips_td1316_params),
	},
	[TUNER_PHILIPS_TUV1236D] = { /* Philips ATSC */
		.name   = "Philips TUV1236D ATSC/NTSC dual in",
		.params = tuner_tuv1236d_params,
		.count  = ARRAY_SIZE(tuner_tuv1236d_params),
	},
	[TUNER_TNF_5335MF] = { /* Tenna PAL/NTSC */
		.name   = "Tena TNF 5335 and similar models",
		.params = tuner_tnf_5335mf_params,
		.count  = ARRAY_SIZE(tuner_tnf_5335mf_params),
	},

	/* 70-79 */
	[TUNER_SAMSUNG_TCPN_2121P30A] = { /* Samsung NTSC */
		.name   = "Samsung TCPN 2121P30A",
		.params = tuner_samsung_tcpn_2121p30a_params,
		.count  = ARRAY_SIZE(tuner_samsung_tcpn_2121p30a_params),
	},
	[TUNER_XCEIVE_XC3028] = { /* Xceive 3028 */
		.name	= "Xceive xc3028",
		/* see xc3028.c for details */
	},
	[TUNER_THOMSON_FE6600] = { /* Thomson PAL / DVB-T */
		.name   = "Thomson FE6600",
		.params = tuner_thomson_fe6600_params,
		.count  = ARRAY_SIZE(tuner_thomson_fe6600_params),
	},
	[TUNER_SAMSUNG_TCPG_6121P30A] = { /* Samsung PAL */
		.name   = "Samsung TCPG 6121P30A",
		.params = tuner_samsung_tcpg_6121p30a_params,
		.count  = ARRAY_SIZE(tuner_samsung_tcpg_6121p30a_params),
	},
	[TUNER_TDA9887] = { /* Philips TDA 9887 IF PLL Demodulator.
				This chip is part of some modern tuners */
		.name   = "Philips TDA988[5,6,7] IF PLL Demodulator",
		/* see tda9887.c for details */
	},
	[TUNER_TEA5761] = { /* Philips RADIO */
		.name   = "Philips TEA5761 FM Radio",
		/* see tea5767.c for details */
	},
};

unsigned const int tuner_count = ARRAY_SIZE(tuners);
