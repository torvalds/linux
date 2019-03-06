// SPDX-License-Identifier: MIT
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include "radeon.h"

/*
 * Integrated TV out support based on the GATOS code by
 * Federico Ulivi <fulivi@lycos.com>
 */


/*
 * Limits of h/v positions (hPos & vPos)
 */
#define MAX_H_POSITION 5 /* Range: [-5..5], negative is on the left, 0 is default, positive is on the right */
#define MAX_V_POSITION 5 /* Range: [-5..5], negative is up, 0 is default, positive is down */

/*
 * Unit for hPos (in TV clock periods)
 */
#define H_POS_UNIT 10

/*
 * Indexes in h. code timing table for horizontal line position adjustment
 */
#define H_TABLE_POS1 6
#define H_TABLE_POS2 8

/*
 * Limits of hor. size (hSize)
 */
#define MAX_H_SIZE 5 /* Range: [-5..5], negative is smaller, positive is larger */

/* tv standard constants */
#define NTSC_TV_CLOCK_T 233
#define NTSC_TV_VFTOTAL 1
#define NTSC_TV_LINES_PER_FRAME 525
#define NTSC_TV_ZERO_H_SIZE 479166
#define NTSC_TV_H_SIZE_UNIT 9478

#define PAL_TV_CLOCK_T 188
#define PAL_TV_VFTOTAL 3
#define PAL_TV_LINES_PER_FRAME 625
#define PAL_TV_ZERO_H_SIZE 473200
#define PAL_TV_H_SIZE_UNIT 9360

/* tv pll setting for 27 mhz ref clk */
#define NTSC_TV_PLL_M_27 22
#define NTSC_TV_PLL_N_27 175
#define NTSC_TV_PLL_P_27 5

#define PAL_TV_PLL_M_27 113
#define PAL_TV_PLL_N_27 668
#define PAL_TV_PLL_P_27 3

/* tv pll setting for 14 mhz ref clk */
#define NTSC_TV_PLL_M_14 33
#define NTSC_TV_PLL_N_14 693
#define NTSC_TV_PLL_P_14 7

#define PAL_TV_PLL_M_14 19
#define PAL_TV_PLL_N_14 353
#define PAL_TV_PLL_P_14 5

#define VERT_LEAD_IN_LINES 2
#define FRAC_BITS 0xe
#define FRAC_MASK 0x3fff

struct radeon_tv_mode_constants {
	uint16_t hor_resolution;
	uint16_t ver_resolution;
	enum radeon_tv_std standard;
	uint16_t hor_total;
	uint16_t ver_total;
	uint16_t hor_start;
	uint16_t hor_syncstart;
	uint16_t ver_syncstart;
	unsigned def_restart;
	uint16_t crtcPLL_N;
	uint8_t  crtcPLL_M;
	uint8_t  crtcPLL_post_div;
	unsigned pix_to_tv;
};

static const uint16_t hor_timing_NTSC[MAX_H_CODE_TIMING_LEN] = {
	0x0007,
	0x003f,
	0x0263,
	0x0a24,
	0x2a6b,
	0x0a36,
	0x126d, /* H_TABLE_POS1 */
	0x1bfe,
	0x1a8f, /* H_TABLE_POS2 */
	0x1ec7,
	0x3863,
	0x1bfe,
	0x1bfe,
	0x1a2a,
	0x1e95,
	0x0e31,
	0x201b,
	0
};

static const uint16_t vert_timing_NTSC[MAX_V_CODE_TIMING_LEN] = {
	0x2001,
	0x200d,
	0x1006,
	0x0c06,
	0x1006,
	0x1818,
	0x21e3,
	0x1006,
	0x0c06,
	0x1006,
	0x1817,
	0x21d4,
	0x0002,
	0
};

static const uint16_t hor_timing_PAL[MAX_H_CODE_TIMING_LEN] = {
	0x0007,
	0x0058,
	0x027c,
	0x0a31,
	0x2a77,
	0x0a95,
	0x124f, /* H_TABLE_POS1 */
	0x1bfe,
	0x1b22, /* H_TABLE_POS2 */
	0x1ef9,
	0x387c,
	0x1bfe,
	0x1bfe,
	0x1b31,
	0x1eb5,
	0x0e43,
	0x201b,
	0
};

static const uint16_t vert_timing_PAL[MAX_V_CODE_TIMING_LEN] = {
	0x2001,
	0x200c,
	0x1005,
	0x0c05,
	0x1005,
	0x1401,
	0x1821,
	0x2240,
	0x1005,
	0x0c05,
	0x1005,
	0x1401,
	0x1822,
	0x2230,
	0x0002,
	0
};

/**********************************************************************
 *
 * availableModes
 *
 * Table of all allowed modes for tv output
 *
 **********************************************************************/
static const struct radeon_tv_mode_constants available_tv_modes[] = {
	{   /* NTSC timing for 27 Mhz ref clk */
		800,                /* horResolution */
		600,                /* verResolution */
		TV_STD_NTSC,        /* standard */
		990,                /* horTotal */
		740,                /* verTotal */
		813,                /* horStart */
		824,                /* horSyncStart */
		632,                /* verSyncStart */
		625592,             /* defRestart */
		592,                /* crtcPLL_N */
		91,                 /* crtcPLL_M */
		4,                  /* crtcPLL_postDiv */
		1022,               /* pixToTV */
	},
	{   /* PAL timing for 27 Mhz ref clk */
		800,               /* horResolution */
		600,               /* verResolution */
		TV_STD_PAL,        /* standard */
		1144,              /* horTotal */
		706,               /* verTotal */
		812,               /* horStart */
		824,               /* horSyncStart */
		669,               /* verSyncStart */
		696700,            /* defRestart */
		1382,              /* crtcPLL_N */
		231,               /* crtcPLL_M */
		4,                 /* crtcPLL_postDiv */
		759,               /* pixToTV */
	},
	{   /* NTSC timing for 14 Mhz ref clk */
		800,                /* horResolution */
		600,                /* verResolution */
		TV_STD_NTSC,        /* standard */
		1018,               /* horTotal */
		727,                /* verTotal */
		813,                /* horStart */
		840,                /* horSyncStart */
		633,                /* verSyncStart */
		630627,             /* defRestart */
		347,                /* crtcPLL_N */
		14,                 /* crtcPLL_M */
		8,                  /* crtcPLL_postDiv */
		1022,               /* pixToTV */
	},
	{ /* PAL timing for 14 Mhz ref clk */
		800,                /* horResolution */
		600,                /* verResolution */
		TV_STD_PAL,         /* standard */
		1131,               /* horTotal */
		742,                /* verTotal */
		813,                /* horStart */
		840,                /* horSyncStart */
		633,                /* verSyncStart */
		708369,             /* defRestart */
		211,                /* crtcPLL_N */
		9,                  /* crtcPLL_M */
		8,                  /* crtcPLL_postDiv */
		759,                /* pixToTV */
	},
};

#define N_AVAILABLE_MODES ARRAY_SIZE(available_tv_modes)

static const struct radeon_tv_mode_constants *radeon_legacy_tv_get_std_mode(struct radeon_encoder *radeon_encoder,
									    uint16_t *pll_ref_freq)
{
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc;
	struct radeon_encoder_tv_dac *tv_dac = radeon_encoder->enc_priv;
	const struct radeon_tv_mode_constants *const_ptr;
	struct radeon_pll *pll;

	radeon_crtc = to_radeon_crtc(radeon_encoder->base.crtc);
	if (radeon_crtc->crtc_id == 1)
		pll = &rdev->clock.p2pll;
	else
		pll = &rdev->clock.p1pll;

	if (pll_ref_freq)
		*pll_ref_freq = pll->reference_freq;

	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J ||
	    tv_dac->tv_std == TV_STD_PAL_M) {
		if (pll->reference_freq == 2700)
			const_ptr = &available_tv_modes[0];
		else
			const_ptr = &available_tv_modes[2];
	} else {
		if (pll->reference_freq == 2700)
			const_ptr = &available_tv_modes[1];
		else
			const_ptr = &available_tv_modes[3];
	}
	return const_ptr;
}

static long YCOEF_value[5] = { 2, 2, 0, 4, 0 };
static long YCOEF_EN_value[5] = { 1, 1, 0, 1, 0 };
static long SLOPE_value[5] = { 1, 2, 2, 4, 8 };
static long SLOPE_limit[5] = { 6, 5, 4, 3, 2 };

static void radeon_wait_pll_lock(struct drm_encoder *encoder, unsigned n_tests,
				 unsigned n_wait_loops, unsigned cnt_threshold)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t save_pll_test;
	unsigned int i, j;

	WREG32(RADEON_TEST_DEBUG_MUX, (RREG32(RADEON_TEST_DEBUG_MUX) & 0xffff60ff) | 0x100);
	save_pll_test = RREG32_PLL(RADEON_PLL_TEST_CNTL);
	WREG32_PLL(RADEON_PLL_TEST_CNTL, save_pll_test & ~RADEON_PLL_MASK_READ_B);

	WREG8(RADEON_CLOCK_CNTL_INDEX, RADEON_PLL_TEST_CNTL);
	for (i = 0; i < n_tests; i++) {
		WREG8(RADEON_CLOCK_CNTL_DATA + 3, 0);
		for (j = 0; j < n_wait_loops; j++)
			if (RREG8(RADEON_CLOCK_CNTL_DATA + 3) >= cnt_threshold)
				break;
	}
	WREG32_PLL(RADEON_PLL_TEST_CNTL, save_pll_test);
	WREG32(RADEON_TEST_DEBUG_MUX, RREG32(RADEON_TEST_DEBUG_MUX) & 0xffffe0ff);
}


static void radeon_legacy_tv_write_fifo(struct radeon_encoder *radeon_encoder,
					uint16_t addr, uint32_t value)
{
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t tmp;
	int i = 0;

	WREG32(RADEON_TV_HOST_WRITE_DATA, value);

	WREG32(RADEON_TV_HOST_RD_WT_CNTL, addr);
	WREG32(RADEON_TV_HOST_RD_WT_CNTL, addr | RADEON_HOST_FIFO_WT);

	do {
		tmp = RREG32(RADEON_TV_HOST_RD_WT_CNTL);
		if ((tmp & RADEON_HOST_FIFO_WT_ACK) == 0)
			break;
		i++;
	} while (i < 10000);
	WREG32(RADEON_TV_HOST_RD_WT_CNTL, 0);
}

#if 0 /* included for completeness */
static uint32_t radeon_legacy_tv_read_fifo(struct radeon_encoder *radeon_encoder, uint16_t addr)
{
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t tmp;
	int i = 0;

	WREG32(RADEON_TV_HOST_RD_WT_CNTL, addr);
	WREG32(RADEON_TV_HOST_RD_WT_CNTL, addr | RADEON_HOST_FIFO_RD);

	do {
		tmp = RREG32(RADEON_TV_HOST_RD_WT_CNTL);
		if ((tmp & RADEON_HOST_FIFO_RD_ACK) == 0)
			break;
		i++;
	} while (i < 10000);
	WREG32(RADEON_TV_HOST_RD_WT_CNTL, 0);
	return RREG32(RADEON_TV_HOST_READ_DATA);
}
#endif

static uint16_t radeon_get_htiming_tables_addr(uint32_t tv_uv_adr)
{
	uint16_t h_table;

	switch ((tv_uv_adr & RADEON_HCODE_TABLE_SEL_MASK) >> RADEON_HCODE_TABLE_SEL_SHIFT) {
	case 0:
		h_table = RADEON_TV_MAX_FIFO_ADDR_INTERNAL;
		break;
	case 1:
		h_table = ((tv_uv_adr & RADEON_TABLE1_BOT_ADR_MASK) >> RADEON_TABLE1_BOT_ADR_SHIFT) * 2;
		break;
	case 2:
		h_table = ((tv_uv_adr & RADEON_TABLE3_TOP_ADR_MASK) >> RADEON_TABLE3_TOP_ADR_SHIFT) * 2;
		break;
	default:
		h_table = 0;
		break;
	}
	return h_table;
}

static uint16_t radeon_get_vtiming_tables_addr(uint32_t tv_uv_adr)
{
	uint16_t v_table;

	switch ((tv_uv_adr & RADEON_VCODE_TABLE_SEL_MASK) >> RADEON_VCODE_TABLE_SEL_SHIFT) {
	case 0:
		v_table = ((tv_uv_adr & RADEON_MAX_UV_ADR_MASK) >> RADEON_MAX_UV_ADR_SHIFT) * 2 + 1;
		break;
	case 1:
		v_table = ((tv_uv_adr & RADEON_TABLE1_BOT_ADR_MASK) >> RADEON_TABLE1_BOT_ADR_SHIFT) * 2 + 1;
		break;
	case 2:
		v_table = ((tv_uv_adr & RADEON_TABLE3_TOP_ADR_MASK) >> RADEON_TABLE3_TOP_ADR_SHIFT) * 2 + 1;
		break;
	default:
		v_table = 0;
		break;
	}
	return v_table;
}

static void radeon_restore_tv_timing_tables(struct radeon_encoder *radeon_encoder)
{
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder_tv_dac *tv_dac = radeon_encoder->enc_priv;
	uint16_t h_table, v_table;
	uint32_t tmp;
	int i;

	WREG32(RADEON_TV_UV_ADR, tv_dac->tv.tv_uv_adr);
	h_table = radeon_get_htiming_tables_addr(tv_dac->tv.tv_uv_adr);
	v_table = radeon_get_vtiming_tables_addr(tv_dac->tv.tv_uv_adr);

	for (i = 0; i < MAX_H_CODE_TIMING_LEN; i += 2, h_table--) {
		tmp = ((uint32_t)tv_dac->tv.h_code_timing[i] << 14) | ((uint32_t)tv_dac->tv.h_code_timing[i+1]);
		radeon_legacy_tv_write_fifo(radeon_encoder, h_table, tmp);
		if (tv_dac->tv.h_code_timing[i] == 0 || tv_dac->tv.h_code_timing[i + 1] == 0)
			break;
	}
	for (i = 0; i < MAX_V_CODE_TIMING_LEN; i += 2, v_table++) {
		tmp = ((uint32_t)tv_dac->tv.v_code_timing[i+1] << 14) | ((uint32_t)tv_dac->tv.v_code_timing[i]);
		radeon_legacy_tv_write_fifo(radeon_encoder, v_table, tmp);
		if (tv_dac->tv.v_code_timing[i] == 0 || tv_dac->tv.v_code_timing[i + 1] == 0)
			break;
	}
}

static void radeon_legacy_write_tv_restarts(struct radeon_encoder *radeon_encoder)
{
	struct drm_device *dev = radeon_encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder_tv_dac *tv_dac = radeon_encoder->enc_priv;
	WREG32(RADEON_TV_FRESTART, tv_dac->tv.frestart);
	WREG32(RADEON_TV_HRESTART, tv_dac->tv.hrestart);
	WREG32(RADEON_TV_VRESTART, tv_dac->tv.vrestart);
}

static bool radeon_legacy_tv_init_restarts(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_tv_dac *tv_dac = radeon_encoder->enc_priv;
	int restart;
	unsigned int h_total, v_total, f_total;
	int v_offset, h_offset;
	u16 p1, p2, h_inc;
	bool h_changed;
	const struct radeon_tv_mode_constants *const_ptr;

	const_ptr = radeon_legacy_tv_get_std_mode(radeon_encoder, NULL);
	if (!const_ptr)
		return false;

	h_total = const_ptr->hor_total;
	v_total = const_ptr->ver_total;

	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J ||
	    tv_dac->tv_std == TV_STD_PAL_M ||
	    tv_dac->tv_std == TV_STD_PAL_60)
		f_total = NTSC_TV_VFTOTAL + 1;
	else
		f_total = PAL_TV_VFTOTAL + 1;

	/* adjust positions 1&2 in hor. cod timing table */
	h_offset = tv_dac->h_pos * H_POS_UNIT;

	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J ||
	    tv_dac->tv_std == TV_STD_PAL_M) {
		h_offset -= 50;
		p1 = hor_timing_NTSC[H_TABLE_POS1];
		p2 = hor_timing_NTSC[H_TABLE_POS2];
	} else {
		p1 = hor_timing_PAL[H_TABLE_POS1];
		p2 = hor_timing_PAL[H_TABLE_POS2];
	}

	p1 = (u16)((int)p1 + h_offset);
	p2 = (u16)((int)p2 - h_offset);

	h_changed = (p1 != tv_dac->tv.h_code_timing[H_TABLE_POS1] ||
		     p2 != tv_dac->tv.h_code_timing[H_TABLE_POS2]);

	tv_dac->tv.h_code_timing[H_TABLE_POS1] = p1;
	tv_dac->tv.h_code_timing[H_TABLE_POS2] = p2;

	/* Convert hOffset from n. of TV clock periods to n. of CRTC clock periods (CRTC pixels) */
	h_offset = (h_offset * (int)(const_ptr->pix_to_tv)) / 1000;

	/* adjust restart */
	restart = const_ptr->def_restart;

	/*
	 * convert v_pos TV lines to n. of CRTC pixels
	 */
	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J ||
	    tv_dac->tv_std == TV_STD_PAL_M ||
	    tv_dac->tv_std == TV_STD_PAL_60)
		v_offset = ((int)(v_total * h_total) * 2 * tv_dac->v_pos) / (int)(NTSC_TV_LINES_PER_FRAME);
	else
		v_offset = ((int)(v_total * h_total) * 2 * tv_dac->v_pos) / (int)(PAL_TV_LINES_PER_FRAME);

	restart -= v_offset + h_offset;

	DRM_DEBUG_KMS("compute_restarts: def = %u h = %d v = %d, p1 = %04x, p2 = %04x, restart = %d\n",
		  const_ptr->def_restart, tv_dac->h_pos, tv_dac->v_pos, p1, p2, restart);

	tv_dac->tv.hrestart = restart % h_total;
	restart /= h_total;
	tv_dac->tv.vrestart = restart % v_total;
	restart /= v_total;
	tv_dac->tv.frestart = restart % f_total;

	DRM_DEBUG_KMS("compute_restart: F/H/V=%u,%u,%u\n",
		  (unsigned)tv_dac->tv.frestart,
		  (unsigned)tv_dac->tv.vrestart,
		  (unsigned)tv_dac->tv.hrestart);

	/* compute h_inc from hsize */
	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J ||
	    tv_dac->tv_std == TV_STD_PAL_M)
		h_inc = (u16)((int)(const_ptr->hor_resolution * 4096 * NTSC_TV_CLOCK_T) /
			      (tv_dac->h_size * (int)(NTSC_TV_H_SIZE_UNIT) + (int)(NTSC_TV_ZERO_H_SIZE)));
	else
		h_inc = (u16)((int)(const_ptr->hor_resolution * 4096 * PAL_TV_CLOCK_T) /
			      (tv_dac->h_size * (int)(PAL_TV_H_SIZE_UNIT) + (int)(PAL_TV_ZERO_H_SIZE)));

	tv_dac->tv.timing_cntl = (tv_dac->tv.timing_cntl & ~RADEON_H_INC_MASK) |
		((u32)h_inc << RADEON_H_INC_SHIFT);

	DRM_DEBUG_KMS("compute_restart: h_size = %d h_inc = %d\n", tv_dac->h_size, h_inc);

	return h_changed;
}

void radeon_legacy_tv_mode_set(struct drm_encoder *encoder,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_tv_dac *tv_dac = radeon_encoder->enc_priv;
	const struct radeon_tv_mode_constants *const_ptr;
	struct radeon_crtc *radeon_crtc;
	int i;
	uint16_t pll_ref_freq;
	uint32_t vert_space, flicker_removal, tmp;
	uint32_t tv_master_cntl, tv_rgb_cntl, tv_dac_cntl;
	uint32_t tv_modulator_cntl1, tv_modulator_cntl2;
	uint32_t tv_vscaler_cntl1, tv_vscaler_cntl2;
	uint32_t tv_pll_cntl, tv_pll_cntl1, tv_ftotal;
	uint32_t tv_y_fall_cntl, tv_y_rise_cntl, tv_y_saw_tooth_cntl;
	uint32_t m, n, p;
	const uint16_t *hor_timing;
	const uint16_t *vert_timing;

	const_ptr = radeon_legacy_tv_get_std_mode(radeon_encoder, &pll_ref_freq);
	if (!const_ptr)
		return;

	radeon_crtc = to_radeon_crtc(encoder->crtc);

	tv_master_cntl = (RADEON_VIN_ASYNC_RST |
			  RADEON_CRT_FIFO_CE_EN |
			  RADEON_TV_FIFO_CE_EN |
			  RADEON_TV_ON);

	if (!ASIC_IS_R300(rdev))
		tv_master_cntl |= RADEON_TVCLK_ALWAYS_ONb;

	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J)
		tv_master_cntl |= RADEON_RESTART_PHASE_FIX;

	tv_modulator_cntl1 = (RADEON_SLEW_RATE_LIMIT |
			      RADEON_SYNC_TIP_LEVEL |
			      RADEON_YFLT_EN |
			      RADEON_UVFLT_EN |
			      (6 << RADEON_CY_FILT_BLEND_SHIFT));

	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J) {
		tv_modulator_cntl1 |= (0x46 << RADEON_SET_UP_LEVEL_SHIFT) |
			(0x3b << RADEON_BLANK_LEVEL_SHIFT);
		tv_modulator_cntl2 = (-111 & RADEON_TV_U_BURST_LEVEL_MASK) |
			((0 & RADEON_TV_V_BURST_LEVEL_MASK) << RADEON_TV_V_BURST_LEVEL_SHIFT);
	} else if (tv_dac->tv_std == TV_STD_SCART_PAL) {
		tv_modulator_cntl1 |= RADEON_ALT_PHASE_EN;
		tv_modulator_cntl2 = (0 & RADEON_TV_U_BURST_LEVEL_MASK) |
			((0 & RADEON_TV_V_BURST_LEVEL_MASK) << RADEON_TV_V_BURST_LEVEL_SHIFT);
	} else {
		tv_modulator_cntl1 |= RADEON_ALT_PHASE_EN |
			(0x3b << RADEON_SET_UP_LEVEL_SHIFT) |
			(0x3b << RADEON_BLANK_LEVEL_SHIFT);
		tv_modulator_cntl2 = (-78 & RADEON_TV_U_BURST_LEVEL_MASK) |
			((62 & RADEON_TV_V_BURST_LEVEL_MASK) << RADEON_TV_V_BURST_LEVEL_SHIFT);
	}


	tv_rgb_cntl = (RADEON_RGB_DITHER_EN
		       | RADEON_TVOUT_SCALE_EN
		       | (0x0b << RADEON_UVRAM_READ_MARGIN_SHIFT)
		       | (0x07 << RADEON_FIFORAM_FFMACRO_READ_MARGIN_SHIFT)
		       | RADEON_RGB_ATTEN_SEL(0x3)
		       | RADEON_RGB_ATTEN_VAL(0xc));

	if (radeon_crtc->crtc_id == 1)
		tv_rgb_cntl |= RADEON_RGB_SRC_SEL_CRTC2;
	else {
		if (radeon_crtc->rmx_type != RMX_OFF)
			tv_rgb_cntl |= RADEON_RGB_SRC_SEL_RMX;
		else
			tv_rgb_cntl |= RADEON_RGB_SRC_SEL_CRTC1;
	}

	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J ||
	    tv_dac->tv_std == TV_STD_PAL_M ||
	    tv_dac->tv_std == TV_STD_PAL_60)
		vert_space = const_ptr->ver_total * 2 * 10000 / NTSC_TV_LINES_PER_FRAME;
	else
		vert_space = const_ptr->ver_total * 2 * 10000 / PAL_TV_LINES_PER_FRAME;

	tmp = RREG32(RADEON_TV_VSCALER_CNTL1);
	tmp &= 0xe3ff0000;
	tmp |= (vert_space * (1 << FRAC_BITS) / 10000);
	tv_vscaler_cntl1 = tmp;

	if (pll_ref_freq == 2700)
		tv_vscaler_cntl1 |= RADEON_RESTART_FIELD;

	if (const_ptr->hor_resolution == 1024)
		tv_vscaler_cntl1 |= (4 << RADEON_Y_DEL_W_SIG_SHIFT);
	else
		tv_vscaler_cntl1 |= (2 << RADEON_Y_DEL_W_SIG_SHIFT);

	/* scale up for int divide */
	tmp = const_ptr->ver_total * 2 * 1000;
	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J ||
	    tv_dac->tv_std == TV_STD_PAL_M ||
	    tv_dac->tv_std == TV_STD_PAL_60) {
		tmp /= NTSC_TV_LINES_PER_FRAME;
	} else {
		tmp /= PAL_TV_LINES_PER_FRAME;
	}
	flicker_removal = (tmp + 500) / 1000;

	if (flicker_removal < 3)
		flicker_removal = 3;
	for (i = 0; i < ARRAY_SIZE(SLOPE_limit); ++i) {
		if (flicker_removal == SLOPE_limit[i])
			break;
	}

	tv_y_saw_tooth_cntl = (vert_space * SLOPE_value[i] * (1 << (FRAC_BITS - 1)) +
				5001) / 10000 / 8 | ((SLOPE_value[i] *
				(1 << (FRAC_BITS - 1)) / 8) << 16);
	tv_y_fall_cntl =
		(YCOEF_EN_value[i] << 17) | ((YCOEF_value[i] * (1 << 8) / 8) << 24) |
		RADEON_Y_FALL_PING_PONG | (272 * SLOPE_value[i] / 8) * (1 << (FRAC_BITS - 1)) /
		1024;
	tv_y_rise_cntl = RADEON_Y_RISE_PING_PONG|
		(flicker_removal * 1024 - 272) * SLOPE_value[i] / 8 * (1 << (FRAC_BITS - 1)) / 1024;

	tv_vscaler_cntl2 = RREG32(RADEON_TV_VSCALER_CNTL2) & 0x00fffff0;
	tv_vscaler_cntl2 |= (0x10 << 24) |
		RADEON_DITHER_MODE |
		RADEON_Y_OUTPUT_DITHER_EN |
		RADEON_UV_OUTPUT_DITHER_EN |
		RADEON_UV_TO_BUF_DITHER_EN;

	tmp = (tv_vscaler_cntl1 >> RADEON_UV_INC_SHIFT) & RADEON_UV_INC_MASK;
	tmp = ((16384 * 256 * 10) / tmp + 5) / 10;
	tmp = (tmp << RADEON_UV_OUTPUT_POST_SCALE_SHIFT) | 0x000b0000;
	tv_dac->tv.timing_cntl = tmp;

	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J ||
	    tv_dac->tv_std == TV_STD_PAL_M ||
	    tv_dac->tv_std == TV_STD_PAL_60)
		tv_dac_cntl = tv_dac->ntsc_tvdac_adj;
	else
		tv_dac_cntl = tv_dac->pal_tvdac_adj;

	tv_dac_cntl |= RADEON_TV_DAC_NBLANK | RADEON_TV_DAC_NHOLD;

	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J)
		tv_dac_cntl |= RADEON_TV_DAC_STD_NTSC;
	else
		tv_dac_cntl |= RADEON_TV_DAC_STD_PAL;

	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J) {
		if (pll_ref_freq == 2700) {
			m = NTSC_TV_PLL_M_27;
			n = NTSC_TV_PLL_N_27;
			p = NTSC_TV_PLL_P_27;
		} else {
			m = NTSC_TV_PLL_M_14;
			n = NTSC_TV_PLL_N_14;
			p = NTSC_TV_PLL_P_14;
		}
	} else {
		if (pll_ref_freq == 2700) {
			m = PAL_TV_PLL_M_27;
			n = PAL_TV_PLL_N_27;
			p = PAL_TV_PLL_P_27;
		} else {
			m = PAL_TV_PLL_M_14;
			n = PAL_TV_PLL_N_14;
			p = PAL_TV_PLL_P_14;
		}
	}

	tv_pll_cntl = (m & RADEON_TV_M0LO_MASK) |
		(((m >> 8) & RADEON_TV_M0HI_MASK) << RADEON_TV_M0HI_SHIFT) |
		((n & RADEON_TV_N0LO_MASK) << RADEON_TV_N0LO_SHIFT) |
		(((n >> 9) & RADEON_TV_N0HI_MASK) << RADEON_TV_N0HI_SHIFT) |
		((p & RADEON_TV_P_MASK) << RADEON_TV_P_SHIFT);

	tv_pll_cntl1 = (((4 & RADEON_TVPCP_MASK) << RADEON_TVPCP_SHIFT) |
			((4 & RADEON_TVPVG_MASK) << RADEON_TVPVG_SHIFT) |
			((1 & RADEON_TVPDC_MASK) << RADEON_TVPDC_SHIFT) |
			RADEON_TVCLK_SRC_SEL_TVPLL |
			RADEON_TVPLL_TEST_DIS);

	tv_dac->tv.tv_uv_adr = 0xc8;

	if (tv_dac->tv_std == TV_STD_NTSC ||
	    tv_dac->tv_std == TV_STD_NTSC_J ||
	    tv_dac->tv_std == TV_STD_PAL_M ||
	    tv_dac->tv_std == TV_STD_PAL_60) {
		tv_ftotal = NTSC_TV_VFTOTAL;
		hor_timing = hor_timing_NTSC;
		vert_timing = vert_timing_NTSC;
	} else {
		hor_timing = hor_timing_PAL;
		vert_timing = vert_timing_PAL;
		tv_ftotal = PAL_TV_VFTOTAL;
	}

	for (i = 0; i < MAX_H_CODE_TIMING_LEN; i++) {
		if ((tv_dac->tv.h_code_timing[i] = hor_timing[i]) == 0)
			break;
	}

	for (i = 0; i < MAX_V_CODE_TIMING_LEN; i++) {
		if ((tv_dac->tv.v_code_timing[i] = vert_timing[i]) == 0)
			break;
	}

	radeon_legacy_tv_init_restarts(encoder);

	/* play with DAC_CNTL */
	/* play with GPIOPAD_A */
	/* DISP_OUTPUT_CNTL */
	/* use reference freq */

	/* program the TV registers */
	WREG32(RADEON_TV_MASTER_CNTL, (tv_master_cntl | RADEON_TV_ASYNC_RST |
				       RADEON_CRT_ASYNC_RST | RADEON_TV_FIFO_ASYNC_RST));

	tmp = RREG32(RADEON_TV_DAC_CNTL);
	tmp &= ~RADEON_TV_DAC_NBLANK;
	tmp |= RADEON_TV_DAC_BGSLEEP |
		RADEON_TV_DAC_RDACPD |
		RADEON_TV_DAC_GDACPD |
		RADEON_TV_DAC_BDACPD;
	WREG32(RADEON_TV_DAC_CNTL, tmp);

	/* TV PLL */
	WREG32_PLL_P(RADEON_TV_PLL_CNTL1, 0, ~RADEON_TVCLK_SRC_SEL_TVPLL);
	WREG32_PLL(RADEON_TV_PLL_CNTL, tv_pll_cntl);
	WREG32_PLL_P(RADEON_TV_PLL_CNTL1, RADEON_TVPLL_RESET, ~RADEON_TVPLL_RESET);

	radeon_wait_pll_lock(encoder, 200, 800, 135);

	WREG32_PLL_P(RADEON_TV_PLL_CNTL1, 0, ~RADEON_TVPLL_RESET);

	radeon_wait_pll_lock(encoder, 300, 160, 27);
	radeon_wait_pll_lock(encoder, 200, 800, 135);

	WREG32_PLL_P(RADEON_TV_PLL_CNTL1, 0, ~0xf);
	WREG32_PLL_P(RADEON_TV_PLL_CNTL1, RADEON_TVCLK_SRC_SEL_TVPLL, ~RADEON_TVCLK_SRC_SEL_TVPLL);

	WREG32_PLL_P(RADEON_TV_PLL_CNTL1, (1 << RADEON_TVPDC_SHIFT), ~RADEON_TVPDC_MASK);
	WREG32_PLL_P(RADEON_TV_PLL_CNTL1, 0, ~RADEON_TVPLL_SLEEP);

	/* TV HV */
	WREG32(RADEON_TV_RGB_CNTL, tv_rgb_cntl);
	WREG32(RADEON_TV_HTOTAL, const_ptr->hor_total - 1);
	WREG32(RADEON_TV_HDISP, const_ptr->hor_resolution - 1);
	WREG32(RADEON_TV_HSTART, const_ptr->hor_start);

	WREG32(RADEON_TV_VTOTAL, const_ptr->ver_total - 1);
	WREG32(RADEON_TV_VDISP, const_ptr->ver_resolution - 1);
	WREG32(RADEON_TV_FTOTAL, tv_ftotal);
	WREG32(RADEON_TV_VSCALER_CNTL1, tv_vscaler_cntl1);
	WREG32(RADEON_TV_VSCALER_CNTL2, tv_vscaler_cntl2);

	WREG32(RADEON_TV_Y_FALL_CNTL, tv_y_fall_cntl);
	WREG32(RADEON_TV_Y_RISE_CNTL, tv_y_rise_cntl);
	WREG32(RADEON_TV_Y_SAW_TOOTH_CNTL, tv_y_saw_tooth_cntl);

	WREG32(RADEON_TV_MASTER_CNTL, (tv_master_cntl | RADEON_TV_ASYNC_RST |
				       RADEON_CRT_ASYNC_RST));

	/* TV restarts */
	radeon_legacy_write_tv_restarts(radeon_encoder);

	/* tv timings */
	radeon_restore_tv_timing_tables(radeon_encoder);

	WREG32(RADEON_TV_MASTER_CNTL, (tv_master_cntl | RADEON_TV_ASYNC_RST));

	/* tv std */
	WREG32(RADEON_TV_SYNC_CNTL, (RADEON_SYNC_PUB | RADEON_TV_SYNC_IO_DRIVE));
	WREG32(RADEON_TV_TIMING_CNTL, tv_dac->tv.timing_cntl);
	WREG32(RADEON_TV_MODULATOR_CNTL1, tv_modulator_cntl1);
	WREG32(RADEON_TV_MODULATOR_CNTL2, tv_modulator_cntl2);
	WREG32(RADEON_TV_PRE_DAC_MUX_CNTL, (RADEON_Y_RED_EN |
					    RADEON_C_GRN_EN |
					    RADEON_CMP_BLU_EN |
					    RADEON_DAC_DITHER_EN));

	WREG32(RADEON_TV_CRC_CNTL, 0);

	WREG32(RADEON_TV_MASTER_CNTL, tv_master_cntl);

	WREG32(RADEON_TV_GAIN_LIMIT_SETTINGS, ((0x17f << RADEON_UV_GAIN_LIMIT_SHIFT) |
					       (0x5ff << RADEON_Y_GAIN_LIMIT_SHIFT)));
	WREG32(RADEON_TV_LINEAR_GAIN_SETTINGS, ((0x100 << RADEON_UV_GAIN_SHIFT) |
						(0x100 << RADEON_Y_GAIN_SHIFT)));

	WREG32(RADEON_TV_DAC_CNTL, tv_dac_cntl);

}

void radeon_legacy_tv_adjust_crtc_reg(struct drm_encoder *encoder,
				      uint32_t *h_total_disp, uint32_t *h_sync_strt_wid,
				      uint32_t *v_total_disp, uint32_t *v_sync_strt_wid)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	const struct radeon_tv_mode_constants *const_ptr;
	uint32_t tmp;

	const_ptr = radeon_legacy_tv_get_std_mode(radeon_encoder, NULL);
	if (!const_ptr)
		return;

	*h_total_disp = (((const_ptr->hor_resolution / 8) - 1) << RADEON_CRTC_H_DISP_SHIFT) |
		(((const_ptr->hor_total / 8) - 1) << RADEON_CRTC_H_TOTAL_SHIFT);

	tmp = *h_sync_strt_wid;
	tmp &= ~(RADEON_CRTC_H_SYNC_STRT_PIX | RADEON_CRTC_H_SYNC_STRT_CHAR);
	tmp |= (((const_ptr->hor_syncstart / 8) - 1) << RADEON_CRTC_H_SYNC_STRT_CHAR_SHIFT) |
		(const_ptr->hor_syncstart & 7);
	*h_sync_strt_wid = tmp;

	*v_total_disp = ((const_ptr->ver_resolution - 1) << RADEON_CRTC_V_DISP_SHIFT) |
		((const_ptr->ver_total - 1) << RADEON_CRTC_V_TOTAL_SHIFT);

	tmp = *v_sync_strt_wid;
	tmp &= ~RADEON_CRTC_V_SYNC_STRT;
	tmp |= ((const_ptr->ver_syncstart - 1) << RADEON_CRTC_V_SYNC_STRT_SHIFT);
	*v_sync_strt_wid = tmp;
}

static int get_post_div(int value)
{
	int post_div;
	switch (value) {
	case 1: post_div = 0; break;
	case 2: post_div = 1; break;
	case 3: post_div = 4; break;
	case 4: post_div = 2; break;
	case 6: post_div = 6; break;
	case 8: post_div = 3; break;
	case 12: post_div = 7; break;
	case 16:
	default: post_div = 5; break;
	}
	return post_div;
}

void radeon_legacy_tv_adjust_pll1(struct drm_encoder *encoder,
				  uint32_t *htotal_cntl, uint32_t *ppll_ref_div,
				  uint32_t *ppll_div_3, uint32_t *pixclks_cntl)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	const struct radeon_tv_mode_constants *const_ptr;

	const_ptr = radeon_legacy_tv_get_std_mode(radeon_encoder, NULL);
	if (!const_ptr)
		return;

	*htotal_cntl = (const_ptr->hor_total & 0x7) | RADEON_HTOT_CNTL_VGA_EN;

	*ppll_ref_div = const_ptr->crtcPLL_M;

	*ppll_div_3 = (const_ptr->crtcPLL_N & 0x7ff) | (get_post_div(const_ptr->crtcPLL_post_div) << 16);
	*pixclks_cntl &= ~(RADEON_PIX2CLK_SRC_SEL_MASK | RADEON_PIXCLK_TV_SRC_SEL);
	*pixclks_cntl |= RADEON_PIX2CLK_SRC_SEL_P2PLLCLK;
}

void radeon_legacy_tv_adjust_pll2(struct drm_encoder *encoder,
				  uint32_t *htotal2_cntl, uint32_t *p2pll_ref_div,
				  uint32_t *p2pll_div_0, uint32_t *pixclks_cntl)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	const struct radeon_tv_mode_constants *const_ptr;

	const_ptr = radeon_legacy_tv_get_std_mode(radeon_encoder, NULL);
	if (!const_ptr)
		return;

	*htotal2_cntl = (const_ptr->hor_total & 0x7);

	*p2pll_ref_div = const_ptr->crtcPLL_M;

	*p2pll_div_0 = (const_ptr->crtcPLL_N & 0x7ff) | (get_post_div(const_ptr->crtcPLL_post_div) << 16);
	*pixclks_cntl &= ~RADEON_PIX2CLK_SRC_SEL_MASK;
	*pixclks_cntl |= RADEON_PIX2CLK_SRC_SEL_P2PLLCLK | RADEON_PIXCLK_TV_SRC_SEL;
}

