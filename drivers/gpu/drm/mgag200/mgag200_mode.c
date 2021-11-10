// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2010 Matt Turner.
 * Copyright 2012 Red Hat
 *
 * Authors: Matthew Garrett
 *	    Matt Turner
 *	    Dave Airlie
 */

#include <linux/delay.h>
#include <linux/dma-buf-map.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "mgag200_drv.h"

#define MGAG200_LUT_SIZE 256

/*
 * This file contains setup code for the CRTC.
 */

static void mga_crtc_load_lut(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = to_mga_device(dev);
	struct drm_framebuffer *fb;
	u16 *r_ptr, *g_ptr, *b_ptr;
	int i;

	if (!crtc->enabled)
		return;

	if (!mdev->display_pipe.plane.state)
		return;

	fb = mdev->display_pipe.plane.state->fb;

	r_ptr = crtc->gamma_store;
	g_ptr = r_ptr + crtc->gamma_size;
	b_ptr = g_ptr + crtc->gamma_size;

	WREG8(DAC_INDEX + MGA1064_INDEX, 0);

	if (fb && fb->format->cpp[0] * 8 == 16) {
		int inc = (fb->format->depth == 15) ? 8 : 4;
		u8 r, b;
		for (i = 0; i < MGAG200_LUT_SIZE; i += inc) {
			if (fb->format->depth == 16) {
				if (i > (MGAG200_LUT_SIZE >> 1)) {
					r = b = 0;
				} else {
					r = *r_ptr++ >> 8;
					b = *b_ptr++ >> 8;
					r_ptr++;
					b_ptr++;
				}
			} else {
				r = *r_ptr++ >> 8;
				b = *b_ptr++ >> 8;
			}
			/* VGA registers */
			WREG8(DAC_INDEX + MGA1064_COL_PAL, r);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, *g_ptr++ >> 8);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, b);
		}
		return;
	}
	for (i = 0; i < MGAG200_LUT_SIZE; i++) {
		/* VGA registers */
		WREG8(DAC_INDEX + MGA1064_COL_PAL, *r_ptr++ >> 8);
		WREG8(DAC_INDEX + MGA1064_COL_PAL, *g_ptr++ >> 8);
		WREG8(DAC_INDEX + MGA1064_COL_PAL, *b_ptr++ >> 8);
	}
}

static inline void mga_wait_vsync(struct mga_device *mdev)
{
	unsigned long timeout = jiffies + HZ/10;
	unsigned int status = 0;

	do {
		status = RREG32(MGAREG_Status);
	} while ((status & 0x08) && time_before(jiffies, timeout));
	timeout = jiffies + HZ/10;
	status = 0;
	do {
		status = RREG32(MGAREG_Status);
	} while (!(status & 0x08) && time_before(jiffies, timeout));
}

static inline void mga_wait_busy(struct mga_device *mdev)
{
	unsigned long timeout = jiffies + HZ;
	unsigned int status = 0;
	do {
		status = RREG8(MGAREG_Status + 2);
	} while ((status & 0x01) && time_before(jiffies, timeout));
}

static void mgag200_g200wb_hold_bmc(struct mga_device *mdev)
{
	u8 tmp;
	int iter_max;

	/* 1- The first step is to warn the BMC of an upcoming mode change.
	 * We are putting the misc<0> to output.*/

	WREG8(DAC_INDEX, MGA1064_GEN_IO_CTL);
	tmp = RREG8(DAC_DATA);
	tmp |= 0x10;
	WREG_DAC(MGA1064_GEN_IO_CTL, tmp);

	/* we are putting a 1 on the misc<0> line */
	WREG8(DAC_INDEX, MGA1064_GEN_IO_DATA);
	tmp = RREG8(DAC_DATA);
	tmp |= 0x10;
	WREG_DAC(MGA1064_GEN_IO_DATA, tmp);

	/* 2- Second step to mask and further scan request
	 * This will be done by asserting the remfreqmsk bit (XSPAREREG<7>)
	 */
	WREG8(DAC_INDEX, MGA1064_SPAREREG);
	tmp = RREG8(DAC_DATA);
	tmp |= 0x80;
	WREG_DAC(MGA1064_SPAREREG, tmp);

	/* 3a- the third step is to verifu if there is an active scan
	 * We are searching for a 0 on remhsyncsts <XSPAREREG<0>)
	 */
	iter_max = 300;
	while (!(tmp & 0x1) && iter_max) {
		WREG8(DAC_INDEX, MGA1064_SPAREREG);
		tmp = RREG8(DAC_DATA);
		udelay(1000);
		iter_max--;
	}

	/* 3b- this step occurs only if the remove is actually scanning
	 * we are waiting for the end of the frame which is a 1 on
	 * remvsyncsts (XSPAREREG<1>)
	 */
	if (iter_max) {
		iter_max = 300;
		while ((tmp & 0x2) && iter_max) {
			WREG8(DAC_INDEX, MGA1064_SPAREREG);
			tmp = RREG8(DAC_DATA);
			udelay(1000);
			iter_max--;
		}
	}
}

static void mgag200_g200wb_release_bmc(struct mga_device *mdev)
{
	u8 tmp;

	/* 1- The first step is to ensure that the vrsten and hrsten are set */
	WREG8(MGAREG_CRTCEXT_INDEX, 1);
	tmp = RREG8(MGAREG_CRTCEXT_DATA);
	WREG8(MGAREG_CRTCEXT_DATA, tmp | 0x88);

	/* 2- second step is to assert the rstlvl2 */
	WREG8(DAC_INDEX, MGA1064_REMHEADCTL2);
	tmp = RREG8(DAC_DATA);
	tmp |= 0x8;
	WREG8(DAC_DATA, tmp);

	/* wait 10 us */
	udelay(10);

	/* 3- deassert rstlvl2 */
	tmp &= ~0x08;
	WREG8(DAC_INDEX, MGA1064_REMHEADCTL2);
	WREG8(DAC_DATA, tmp);

	/* 4- remove mask of scan request */
	WREG8(DAC_INDEX, MGA1064_SPAREREG);
	tmp = RREG8(DAC_DATA);
	tmp &= ~0x80;
	WREG8(DAC_DATA, tmp);

	/* 5- put back a 0 on the misc<0> line */
	WREG8(DAC_INDEX, MGA1064_GEN_IO_DATA);
	tmp = RREG8(DAC_DATA);
	tmp &= ~0x10;
	WREG_DAC(MGA1064_GEN_IO_DATA, tmp);
}

/*
 * This is how the framebuffer base address is stored in g200 cards:
 *   * Assume @offset is the gpu_addr variable of the framebuffer object
 *   * Then addr is the number of _pixels_ (not bytes) from the start of
 *     VRAM to the first pixel we want to display. (divided by 2 for 32bit
 *     framebuffers)
 *   * addr is stored in the CRTCEXT0, CRTCC and CRTCD registers
 *      addr<20> -> CRTCEXT0<6>
 *      addr<19-16> -> CRTCEXT0<3-0>
 *      addr<15-8> -> CRTCC<7-0>
 *      addr<7-0> -> CRTCD<7-0>
 *
 *  CRTCEXT0 has to be programmed last to trigger an update and make the
 *  new addr variable take effect.
 */
static void mgag200_set_startadd(struct mga_device *mdev,
				 unsigned long offset)
{
	struct drm_device *dev = &mdev->base;
	u32 startadd;
	u8 crtcc, crtcd, crtcext0;

	startadd = offset / 8;

	/*
	 * Can't store addresses any higher than that, but we also
	 * don't have more than 16 MiB of memory, so it should be fine.
	 */
	drm_WARN_ON(dev, startadd > 0x1fffff);

	RREG_ECRT(0x00, crtcext0);

	crtcc = (startadd >> 8) & 0xff;
	crtcd = startadd & 0xff;
	crtcext0 &= 0xb0;
	crtcext0 |= ((startadd >> 14) & BIT(6)) |
		    ((startadd >> 16) & 0x0f);

	WREG_CRT(0x0c, crtcc);
	WREG_CRT(0x0d, crtcd);
	WREG_ECRT(0x00, crtcext0);
}

static void mgag200_set_dac_regs(struct mga_device *mdev)
{
	size_t i;
	u8 dacvalue[] = {
		/* 0x00: */        0,    0,    0,    0,    0,    0, 0x00,    0,
		/* 0x08: */        0,    0,    0,    0,    0,    0,    0,    0,
		/* 0x10: */        0,    0,    0,    0,    0,    0,    0,    0,
		/* 0x18: */     0x00,    0, 0xC9, 0xFF, 0xBF, 0x20, 0x1F, 0x20,
		/* 0x20: */     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* 0x28: */     0x00, 0x00, 0x00, 0x00,    0,    0,    0, 0x40,
		/* 0x30: */     0x00, 0xB0, 0x00, 0xC2, 0x34, 0x14, 0x02, 0x83,
		/* 0x38: */     0x00, 0x93, 0x00, 0x77, 0x00, 0x00, 0x00, 0x3A,
		/* 0x40: */        0,    0,    0,    0,    0,    0,    0,    0,
		/* 0x48: */        0,    0,    0,    0,    0,    0,    0,    0
	};

	switch (mdev->type) {
	case G200_PCI:
	case G200_AGP:
		dacvalue[MGA1064_SYS_PLL_M] = 0x04;
		dacvalue[MGA1064_SYS_PLL_N] = 0x2D;
		dacvalue[MGA1064_SYS_PLL_P] = 0x19;
		break;
	case G200_SE_A:
	case G200_SE_B:
		dacvalue[MGA1064_VREF_CTL] = 0x03;
		dacvalue[MGA1064_PIX_CLK_CTL] = MGA1064_PIX_CLK_CTL_SEL_PLL;
		dacvalue[MGA1064_MISC_CTL] = MGA1064_MISC_CTL_DAC_EN |
					     MGA1064_MISC_CTL_VGA8 |
					     MGA1064_MISC_CTL_DAC_RAM_CS;
		break;
	case G200_WB:
	case G200_EW3:
		dacvalue[MGA1064_VREF_CTL] = 0x07;
		break;
	case G200_EV:
		dacvalue[MGA1064_PIX_CLK_CTL] = MGA1064_PIX_CLK_CTL_SEL_PLL;
		dacvalue[MGA1064_MISC_CTL] = MGA1064_MISC_CTL_VGA8 |
					     MGA1064_MISC_CTL_DAC_RAM_CS;
		break;
	case G200_EH:
	case G200_EH3:
		dacvalue[MGA1064_MISC_CTL] = MGA1064_MISC_CTL_VGA8 |
					     MGA1064_MISC_CTL_DAC_RAM_CS;
		break;
	case G200_ER:
		break;
	}

	for (i = 0; i < ARRAY_SIZE(dacvalue); i++) {
		if ((i <= 0x17) ||
		    (i == 0x1b) ||
		    (i == 0x1c) ||
		    ((i >= 0x1f) && (i <= 0x29)) ||
		    ((i >= 0x30) && (i <= 0x37)))
			continue;
		if (IS_G200_SE(mdev) &&
		    ((i == 0x2c) || (i == 0x2d) || (i == 0x2e)))
			continue;
		if ((mdev->type == G200_EV ||
		    mdev->type == G200_WB ||
		    mdev->type == G200_EH ||
		    mdev->type == G200_EW3 ||
		    mdev->type == G200_EH3) &&
		    (i >= 0x44) && (i <= 0x4e))
			continue;

		WREG_DAC(i, dacvalue[i]);
	}

	if (mdev->type == G200_ER)
		WREG_DAC(0x90, 0);
}

static void mgag200_init_regs(struct mga_device *mdev)
{
	u8 crtc11, misc;

	mgag200_set_dac_regs(mdev);

	WREG_SEQ(2, 0x0f);
	WREG_SEQ(3, 0x00);
	WREG_SEQ(4, 0x0e);

	WREG_CRT(10, 0);
	WREG_CRT(11, 0);
	WREG_CRT(12, 0);
	WREG_CRT(13, 0);
	WREG_CRT(14, 0);
	WREG_CRT(15, 0);

	RREG_CRT(0x11, crtc11);
	crtc11 &= ~(MGAREG_CRTC11_CRTCPROTECT |
		    MGAREG_CRTC11_VINTEN |
		    MGAREG_CRTC11_VINTCLR);
	WREG_CRT(0x11, crtc11);

	if (mdev->type == G200_ER)
		WREG_ECRT(0x24, 0x5);

	if (mdev->type == G200_EW3)
		WREG_ECRT(0x34, 0x5);

	misc = RREG8(MGA_MISC_IN);
	misc |= MGAREG_MISC_IOADSEL;
	WREG8(MGA_MISC_OUT, misc);
}

static void mgag200_set_mode_regs(struct mga_device *mdev,
				  const struct drm_display_mode *mode)
{
	unsigned int hdisplay, hsyncstart, hsyncend, htotal;
	unsigned int vdisplay, vsyncstart, vsyncend, vtotal;
	u8 misc, crtcext1, crtcext2, crtcext5;

	hdisplay = mode->hdisplay / 8 - 1;
	hsyncstart = mode->hsync_start / 8 - 1;
	hsyncend = mode->hsync_end / 8 - 1;
	htotal = mode->htotal / 8 - 1;

	/* Work around hardware quirk */
	if ((htotal & 0x07) == 0x06 || (htotal & 0x07) == 0x04)
		htotal++;

	vdisplay = mode->vdisplay - 1;
	vsyncstart = mode->vsync_start - 1;
	vsyncend = mode->vsync_end - 1;
	vtotal = mode->vtotal - 2;

	misc = RREG8(MGA_MISC_IN);

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		misc |= MGAREG_MISC_HSYNCPOL;
	else
		misc &= ~MGAREG_MISC_HSYNCPOL;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		misc |= MGAREG_MISC_VSYNCPOL;
	else
		misc &= ~MGAREG_MISC_VSYNCPOL;

	crtcext1 = (((htotal - 4) & 0x100) >> 8) |
		   ((hdisplay & 0x100) >> 7) |
		   ((hsyncstart & 0x100) >> 6) |
		    (htotal & 0x40);
	if (mdev->type == G200_WB || mdev->type == G200_EW3)
		crtcext1 |= BIT(7) | /* vrsten */
			    BIT(3); /* hrsten */

	crtcext2 = ((vtotal & 0xc00) >> 10) |
		   ((vdisplay & 0x400) >> 8) |
		   ((vdisplay & 0xc00) >> 7) |
		   ((vsyncstart & 0xc00) >> 5) |
		   ((vdisplay & 0x400) >> 3);
	crtcext5 = 0x00;

	WREG_CRT(0, htotal - 4);
	WREG_CRT(1, hdisplay);
	WREG_CRT(2, hdisplay);
	WREG_CRT(3, (htotal & 0x1F) | 0x80);
	WREG_CRT(4, hsyncstart);
	WREG_CRT(5, ((htotal & 0x20) << 2) | (hsyncend & 0x1F));
	WREG_CRT(6, vtotal & 0xFF);
	WREG_CRT(7, ((vtotal & 0x100) >> 8) |
		 ((vdisplay & 0x100) >> 7) |
		 ((vsyncstart & 0x100) >> 6) |
		 ((vdisplay & 0x100) >> 5) |
		 ((vdisplay & 0x100) >> 4) | /* linecomp */
		 ((vtotal & 0x200) >> 4) |
		 ((vdisplay & 0x200) >> 3) |
		 ((vsyncstart & 0x200) >> 2));
	WREG_CRT(9, ((vdisplay & 0x200) >> 4) |
		 ((vdisplay & 0x200) >> 3));
	WREG_CRT(16, vsyncstart & 0xFF);
	WREG_CRT(17, (vsyncend & 0x0F) | 0x20);
	WREG_CRT(18, vdisplay & 0xFF);
	WREG_CRT(20, 0);
	WREG_CRT(21, vdisplay & 0xFF);
	WREG_CRT(22, (vtotal + 1) & 0xFF);
	WREG_CRT(23, 0xc3);
	WREG_CRT(24, vdisplay & 0xFF);

	WREG_ECRT(0x01, crtcext1);
	WREG_ECRT(0x02, crtcext2);
	WREG_ECRT(0x05, crtcext5);

	WREG8(MGA_MISC_OUT, misc);
}

static u8 mgag200_get_bpp_shift(const struct drm_format_info *format)
{
	static const u8 bpp_shift[] = {0, 1, 0, 2};

	return bpp_shift[format->cpp[0] - 1];
}

/*
 * Calculates the HW offset value from the framebuffer's pitch. The
 * offset is a multiple of the pixel size and depends on the display
 * format.
 */
static u32 mgag200_calculate_offset(struct mga_device *mdev,
				    const struct drm_framebuffer *fb)
{
	u32 offset = fb->pitches[0] / fb->format->cpp[0];
	u8 bppshift = mgag200_get_bpp_shift(fb->format);

	if (fb->format->cpp[0] * 8 == 24)
		offset = (offset * 3) >> (4 - bppshift);
	else
		offset = offset >> (4 - bppshift);

	return offset;
}

static void mgag200_set_offset(struct mga_device *mdev,
			       const struct drm_framebuffer *fb)
{
	u8 crtc13, crtcext0;
	u32 offset = mgag200_calculate_offset(mdev, fb);

	RREG_ECRT(0, crtcext0);

	crtc13 = offset & 0xff;

	crtcext0 &= ~MGAREG_CRTCEXT0_OFFSET_MASK;
	crtcext0 |= (offset >> 4) & MGAREG_CRTCEXT0_OFFSET_MASK;

	WREG_CRT(0x13, crtc13);
	WREG_ECRT(0x00, crtcext0);
}

static void mgag200_set_format_regs(struct mga_device *mdev,
				    const struct drm_framebuffer *fb)
{
	struct drm_device *dev = &mdev->base;
	const struct drm_format_info *format = fb->format;
	unsigned int bpp, bppshift, scale;
	u8 crtcext3, xmulctrl;

	bpp = format->cpp[0] * 8;

	bppshift = mgag200_get_bpp_shift(format);
	switch (bpp) {
	case 24:
		scale = ((1 << bppshift) * 3) - 1;
		break;
	default:
		scale = (1 << bppshift) - 1;
		break;
	}

	RREG_ECRT(3, crtcext3);

	switch (bpp) {
	case 8:
		xmulctrl = MGA1064_MUL_CTL_8bits;
		break;
	case 16:
		if (format->depth == 15)
			xmulctrl = MGA1064_MUL_CTL_15bits;
		else
			xmulctrl = MGA1064_MUL_CTL_16bits;
		break;
	case 24:
		xmulctrl = MGA1064_MUL_CTL_24bits;
		break;
	case 32:
		xmulctrl = MGA1064_MUL_CTL_32_24bits;
		break;
	default:
		/* BUG: We should have caught this problem already. */
		drm_WARN_ON(dev, "invalid format depth\n");
		return;
	}

	crtcext3 &= ~GENMASK(2, 0);
	crtcext3 |= scale;

	WREG_DAC(MGA1064_MUL_CTL, xmulctrl);

	WREG_GFX(0, 0x00);
	WREG_GFX(1, 0x00);
	WREG_GFX(2, 0x00);
	WREG_GFX(3, 0x00);
	WREG_GFX(4, 0x00);
	WREG_GFX(5, 0x40);
	WREG_GFX(6, 0x05);
	WREG_GFX(7, 0x0f);
	WREG_GFX(8, 0x0f);

	WREG_ECRT(3, crtcext3);
}

static void mgag200_g200er_reset_tagfifo(struct mga_device *mdev)
{
	static uint32_t RESET_FLAG = 0x00200000; /* undocumented magic value */
	u32 memctl;

	memctl = RREG32(MGAREG_MEMCTL);

	memctl |= RESET_FLAG;
	WREG32(MGAREG_MEMCTL, memctl);

	udelay(1000);

	memctl &= ~RESET_FLAG;
	WREG32(MGAREG_MEMCTL, memctl);
}

static void mgag200_g200se_set_hiprilvl(struct mga_device *mdev,
					const struct drm_display_mode *mode,
					const struct drm_framebuffer *fb)
{
	u32 unique_rev_id = mdev->model.g200se.unique_rev_id;
	unsigned int hiprilvl;
	u8 crtcext6;

	if  (unique_rev_id >= 0x04) {
		hiprilvl = 0;
	} else if (unique_rev_id >= 0x02) {
		unsigned int bpp;
		unsigned long mb;

		if (fb->format->cpp[0] * 8 > 16)
			bpp = 32;
		else if (fb->format->cpp[0] * 8 > 8)
			bpp = 16;
		else
			bpp = 8;

		mb = (mode->clock * bpp) / 1000;
		if (mb > 3100)
			hiprilvl = 0;
		else if (mb > 2600)
			hiprilvl = 1;
		else if (mb > 1900)
			hiprilvl = 2;
		else if (mb > 1160)
			hiprilvl = 3;
		else if (mb > 440)
			hiprilvl = 4;
		else
			hiprilvl = 5;

	} else if (unique_rev_id >= 0x01) {
		hiprilvl = 3;
	} else {
		hiprilvl = 4;
	}

	crtcext6 = hiprilvl; /* implicitly sets maxhipri to 0 */

	WREG_ECRT(0x06, crtcext6);
}

static void mgag200_g200ev_set_hiprilvl(struct mga_device *mdev)
{
	WREG_ECRT(0x06, 0x00);
}

static void mgag200_enable_display(struct mga_device *mdev)
{
	u8 seq0, seq1, crtcext1;

	RREG_SEQ(0x00, seq0);
	seq0 |= MGAREG_SEQ0_SYNCRST |
		MGAREG_SEQ0_ASYNCRST;
	WREG_SEQ(0x00, seq0);

	/*
	 * TODO: replace busy waiting with vblank IRQ; put
	 *       msleep(50) before changing SCROFF
	 */
	mga_wait_vsync(mdev);
	mga_wait_busy(mdev);

	RREG_SEQ(0x01, seq1);
	seq1 &= ~MGAREG_SEQ1_SCROFF;
	WREG_SEQ(0x01, seq1);

	msleep(20);

	RREG_ECRT(0x01, crtcext1);
	crtcext1 &= ~MGAREG_CRTCEXT1_VSYNCOFF;
	crtcext1 &= ~MGAREG_CRTCEXT1_HSYNCOFF;
	WREG_ECRT(0x01, crtcext1);
}

static void mgag200_disable_display(struct mga_device *mdev)
{
	u8 seq0, seq1, crtcext1;

	RREG_SEQ(0x00, seq0);
	seq0 &= ~MGAREG_SEQ0_SYNCRST;
	WREG_SEQ(0x00, seq0);

	/*
	 * TODO: replace busy waiting with vblank IRQ; put
	 *       msleep(50) before changing SCROFF
	 */
	mga_wait_vsync(mdev);
	mga_wait_busy(mdev);

	RREG_SEQ(0x01, seq1);
	seq1 |= MGAREG_SEQ1_SCROFF;
	WREG_SEQ(0x01, seq1);

	msleep(20);

	RREG_ECRT(0x01, crtcext1);
	crtcext1 |= MGAREG_CRTCEXT1_VSYNCOFF |
		    MGAREG_CRTCEXT1_HSYNCOFF;
	WREG_ECRT(0x01, crtcext1);
}

/*
 * Connector
 */

static int mga_vga_get_modes(struct drm_connector *connector)
{
	struct mga_connector *mga_connector = to_mga_connector(connector);
	struct edid *edid;
	int ret = 0;

	edid = drm_get_edid(connector, &mga_connector->i2c->adapter);
	if (edid) {
		drm_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}
	return ret;
}

static uint32_t mga_vga_calculate_mode_bandwidth(struct drm_display_mode *mode,
							int bits_per_pixel)
{
	uint32_t total_area, divisor;
	uint64_t active_area, pixels_per_second, bandwidth;
	uint64_t bytes_per_pixel = (bits_per_pixel + 7) / 8;

	divisor = 1024;

	if (!mode->htotal || !mode->vtotal || !mode->clock)
		return 0;

	active_area = mode->hdisplay * mode->vdisplay;
	total_area = mode->htotal * mode->vtotal;

	pixels_per_second = active_area * mode->clock * 1000;
	do_div(pixels_per_second, total_area);

	bandwidth = pixels_per_second * bytes_per_pixel * 100;
	do_div(bandwidth, divisor);

	return (uint32_t)(bandwidth);
}

#define MODE_BANDWIDTH	MODE_BAD

static enum drm_mode_status mga_vga_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct mga_device *mdev = to_mga_device(dev);
	int bpp = 32;

	if (IS_G200_SE(mdev)) {
		u32 unique_rev_id = mdev->model.g200se.unique_rev_id;

		if (unique_rev_id == 0x01) {
			if (mode->hdisplay > 1600)
				return MODE_VIRTUAL_X;
			if (mode->vdisplay > 1200)
				return MODE_VIRTUAL_Y;
			if (mga_vga_calculate_mode_bandwidth(mode, bpp)
				> (24400 * 1024))
				return MODE_BANDWIDTH;
		} else if (unique_rev_id == 0x02) {
			if (mode->hdisplay > 1920)
				return MODE_VIRTUAL_X;
			if (mode->vdisplay > 1200)
				return MODE_VIRTUAL_Y;
			if (mga_vga_calculate_mode_bandwidth(mode, bpp)
				> (30100 * 1024))
				return MODE_BANDWIDTH;
		} else {
			if (mga_vga_calculate_mode_bandwidth(mode, bpp)
				> (55000 * 1024))
				return MODE_BANDWIDTH;
		}
	} else if (mdev->type == G200_WB) {
		if (mode->hdisplay > 1280)
			return MODE_VIRTUAL_X;
		if (mode->vdisplay > 1024)
			return MODE_VIRTUAL_Y;
		if (mga_vga_calculate_mode_bandwidth(mode, bpp) >
		    (31877 * 1024))
			return MODE_BANDWIDTH;
	} else if (mdev->type == G200_EV &&
		(mga_vga_calculate_mode_bandwidth(mode, bpp)
			> (32700 * 1024))) {
		return MODE_BANDWIDTH;
	} else if (mdev->type == G200_EH &&
		(mga_vga_calculate_mode_bandwidth(mode, bpp)
			> (37500 * 1024))) {
		return MODE_BANDWIDTH;
	} else if (mdev->type == G200_ER &&
		(mga_vga_calculate_mode_bandwidth(mode,
			bpp) > (55000 * 1024))) {
		return MODE_BANDWIDTH;
	}

	if ((mode->hdisplay % 8) != 0 || (mode->hsync_start % 8) != 0 ||
	    (mode->hsync_end % 8) != 0 || (mode->htotal % 8) != 0) {
		return MODE_H_ILLEGAL;
	}

	if (mode->crtc_hdisplay > 2048 || mode->crtc_hsync_start > 4096 ||
	    mode->crtc_hsync_end > 4096 || mode->crtc_htotal > 4096 ||
	    mode->crtc_vdisplay > 2048 || mode->crtc_vsync_start > 4096 ||
	    mode->crtc_vsync_end > 4096 || mode->crtc_vtotal > 4096) {
		return MODE_BAD;
	}

	/* Validate the mode input by the user */
	if (connector->cmdline_mode.specified) {
		if (connector->cmdline_mode.bpp_specified)
			bpp = connector->cmdline_mode.bpp;
	}

	if ((mode->hdisplay * mode->vdisplay * (bpp/8)) > mdev->vram_fb_available) {
		if (connector->cmdline_mode.specified)
			connector->cmdline_mode.specified = false;
		return MODE_BAD;
	}

	return MODE_OK;
}

static void mga_connector_destroy(struct drm_connector *connector)
{
	struct mga_connector *mga_connector = to_mga_connector(connector);
	mgag200_i2c_destroy(mga_connector->i2c);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_helper_funcs mga_vga_connector_helper_funcs = {
	.get_modes  = mga_vga_get_modes,
	.mode_valid = mga_vga_mode_valid,
};

static const struct drm_connector_funcs mga_vga_connector_funcs = {
	.reset                  = drm_atomic_helper_connector_reset,
	.fill_modes             = drm_helper_probe_single_connector_modes,
	.destroy                = mga_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_connector_destroy_state,
};

static int mgag200_vga_connector_init(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct mga_connector *mconnector = &mdev->connector;
	struct drm_connector *connector = &mconnector->base;
	struct mga_i2c_chan *i2c;
	int ret;

	i2c = mgag200_i2c_create(dev);
	if (!i2c)
		drm_warn(dev, "failed to add DDC bus\n");

	ret = drm_connector_init_with_ddc(dev, connector,
					  &mga_vga_connector_funcs,
					  DRM_MODE_CONNECTOR_VGA,
					  &i2c->adapter);
	if (ret)
		goto err_mgag200_i2c_destroy;
	drm_connector_helper_add(connector, &mga_vga_connector_helper_funcs);

	mconnector->i2c = i2c;

	return 0;

err_mgag200_i2c_destroy:
	mgag200_i2c_destroy(i2c);
	return ret;
}

/*
 * Simple Display Pipe
 */

static enum drm_mode_status
mgag200_simple_display_pipe_mode_valid(struct drm_simple_display_pipe *pipe,
				       const struct drm_display_mode *mode)
{
	return MODE_OK;
}

static void
mgag200_handle_damage(struct mga_device *mdev, struct drm_framebuffer *fb,
		      struct drm_rect *clip, const struct dma_buf_map *map)
{
	void __iomem *dst = mdev->vram;
	void *vmap = map->vaddr; /* TODO: Use mapping abstraction properly */

	dst += drm_fb_clip_offset(fb->pitches[0], fb->format, clip);
	drm_fb_memcpy_toio(dst, fb->pitches[0], vmap, fb, clip);

	/* Always scanout image at VRAM offset 0 */
	mgag200_set_startadd(mdev, (u32)0);
	mgag200_set_offset(mdev, fb);
}

static void
mgag200_simple_display_pipe_enable(struct drm_simple_display_pipe *pipe,
				   struct drm_crtc_state *crtc_state,
				   struct drm_plane_state *plane_state)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = to_mga_device(dev);
	struct mgag200_pll *pixpll = &mdev->pixpll;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_rect fullscreen = {
		.x1 = 0,
		.x2 = fb->width,
		.y1 = 0,
		.y2 = fb->height,
	};

	if (mdev->type == G200_WB || mdev->type == G200_EW3)
		mgag200_g200wb_hold_bmc(mdev);

	mgag200_set_format_regs(mdev, fb);
	mgag200_set_mode_regs(mdev, adjusted_mode);

	pixpll->funcs->update(pixpll, &mgag200_crtc_state->pixpllc);

	if (mdev->type == G200_ER)
		mgag200_g200er_reset_tagfifo(mdev);

	if (IS_G200_SE(mdev))
		mgag200_g200se_set_hiprilvl(mdev, adjusted_mode, fb);
	else if (mdev->type == G200_EV)
		mgag200_g200ev_set_hiprilvl(mdev);

	if (mdev->type == G200_WB || mdev->type == G200_EW3)
		mgag200_g200wb_release_bmc(mdev);

	mga_crtc_load_lut(crtc);
	mgag200_enable_display(mdev);

	mgag200_handle_damage(mdev, fb, &fullscreen, &shadow_plane_state->data[0]);
}

static void
mgag200_simple_display_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct mga_device *mdev = to_mga_device(crtc->dev);

	mgag200_disable_display(mdev);
}

static int
mgag200_simple_display_pipe_check(struct drm_simple_display_pipe *pipe,
				  struct drm_plane_state *plane_state,
				  struct drm_crtc_state *crtc_state)
{
	struct drm_plane *plane = plane_state->plane;
	struct drm_device *dev = plane->dev;
	struct mga_device *mdev = to_mga_device(dev);
	struct mgag200_pll *pixpll = &mdev->pixpll;
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);
	struct drm_framebuffer *new_fb = plane_state->fb;
	struct drm_framebuffer *fb = NULL;
	int ret;

	if (!new_fb)
		return 0;

	if (plane->state)
		fb = plane->state->fb;

	if (!fb || (fb->format != new_fb->format))
		crtc_state->mode_changed = true; /* update PLL settings */

	if (crtc_state->mode_changed) {
		ret = pixpll->funcs->compute(pixpll, crtc_state->mode.clock,
					     &mgag200_crtc_state->pixpllc);
		if (ret)
			return ret;
	}

	return 0;
}

static void
mgag200_simple_display_pipe_update(struct drm_simple_display_pipe *pipe,
				   struct drm_plane_state *old_state)
{
	struct drm_plane *plane = &pipe->plane;
	struct drm_device *dev = plane->dev;
	struct mga_device *mdev = to_mga_device(dev);
	struct drm_plane_state *state = plane->state;
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	struct drm_rect damage;

	if (!fb)
		return;

	if (drm_atomic_helper_damage_merged(old_state, state, &damage))
		mgag200_handle_damage(mdev, fb, &damage, &shadow_plane_state->data[0]);
}

static struct drm_crtc_state *
mgag200_simple_display_pipe_duplicate_crtc_state(struct drm_simple_display_pipe *pipe)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_crtc_state *crtc_state = crtc->state;
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);
	struct mgag200_crtc_state *new_mgag200_crtc_state;

	if (!crtc_state)
		return NULL;

	new_mgag200_crtc_state = kzalloc(sizeof(*new_mgag200_crtc_state), GFP_KERNEL);
	if (!new_mgag200_crtc_state)
		return NULL;
	__drm_atomic_helper_crtc_duplicate_state(crtc, &new_mgag200_crtc_state->base);

	memcpy(&new_mgag200_crtc_state->pixpllc, &mgag200_crtc_state->pixpllc,
	       sizeof(new_mgag200_crtc_state->pixpllc));

	return &new_mgag200_crtc_state->base;
}

static void mgag200_simple_display_pipe_destroy_crtc_state(struct drm_simple_display_pipe *pipe,
							   struct drm_crtc_state *crtc_state)
{
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);

	__drm_atomic_helper_crtc_destroy_state(&mgag200_crtc_state->base);
	kfree(mgag200_crtc_state);
}

static void mgag200_simple_display_pipe_reset_crtc(struct drm_simple_display_pipe *pipe)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct mgag200_crtc_state *mgag200_crtc_state;

	if (crtc->state) {
		mgag200_simple_display_pipe_destroy_crtc_state(pipe, crtc->state);
		crtc->state = NULL; /* must be set to NULL here */
	}

	mgag200_crtc_state = kzalloc(sizeof(*mgag200_crtc_state), GFP_KERNEL);
	if (!mgag200_crtc_state)
		return;
	__drm_atomic_helper_crtc_reset(crtc, &mgag200_crtc_state->base);
}

static const struct drm_simple_display_pipe_funcs
mgag200_simple_display_pipe_funcs = {
	.mode_valid = mgag200_simple_display_pipe_mode_valid,
	.enable	    = mgag200_simple_display_pipe_enable,
	.disable    = mgag200_simple_display_pipe_disable,
	.check	    = mgag200_simple_display_pipe_check,
	.update	    = mgag200_simple_display_pipe_update,
	.reset_crtc = mgag200_simple_display_pipe_reset_crtc,
	.duplicate_crtc_state = mgag200_simple_display_pipe_duplicate_crtc_state,
	.destroy_crtc_state = mgag200_simple_display_pipe_destroy_crtc_state,
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
};

static const uint32_t mgag200_simple_display_pipe_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
};

static const uint64_t mgag200_simple_display_pipe_fmtmods[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

/*
 * Mode config
 */

static const struct drm_mode_config_funcs mgag200_mode_config_funcs = {
	.fb_create     = drm_gem_fb_create_with_dirty,
	.atomic_check  = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static unsigned int mgag200_preferred_depth(struct mga_device *mdev)
{
	if (IS_G200_SE(mdev) && mdev->vram_fb_available < (2048*1024))
		return 16;
	else
		return 32;
}

int mgag200_modeset_init(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct drm_connector *connector = &mdev->connector.base;
	struct drm_simple_display_pipe *pipe = &mdev->display_pipe;
	size_t format_count = ARRAY_SIZE(mgag200_simple_display_pipe_formats);
	int ret;

	mgag200_init_regs(mdev);

	ret = drmm_mode_config_init(dev);
	if (ret) {
		drm_err(dev, "drmm_mode_config_init() failed, error %d\n",
			ret);
		return ret;
	}

	dev->mode_config.max_width = MGAG200_MAX_FB_WIDTH;
	dev->mode_config.max_height = MGAG200_MAX_FB_HEIGHT;

	dev->mode_config.preferred_depth = mgag200_preferred_depth(mdev);

	dev->mode_config.fb_base = mdev->mc.vram_base;

	dev->mode_config.funcs = &mgag200_mode_config_funcs;

	ret = mgag200_vga_connector_init(mdev);
	if (ret) {
		drm_err(dev,
			"mgag200_vga_connector_init() failed, error %d\n",
			ret);
		return ret;
	}

	ret = mgag200_pixpll_init(&mdev->pixpll, mdev);
	if (ret)
		return ret;

	ret = drm_simple_display_pipe_init(dev, pipe,
					   &mgag200_simple_display_pipe_funcs,
					   mgag200_simple_display_pipe_formats,
					   format_count,
					   mgag200_simple_display_pipe_fmtmods,
					   connector);
	if (ret) {
		drm_err(dev,
			"drm_simple_display_pipe_init() failed, error %d\n",
			ret);
		return ret;
	}

	/* FIXME: legacy gamma tables; convert to CRTC state */
	drm_mode_crtc_set_gamma_size(&pipe->crtc, MGAG200_LUT_SIZE);

	drm_mode_config_reset(dev);

	return 0;
}
