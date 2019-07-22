/*
 *
 * Copyright (c) 2012 Gilles Dartiguelongue, Thomas Richter
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_drv.h"
#include "intel_dvo_dev.h"

#define NS2501_VID 0x1305
#define NS2501_DID 0x6726

#define NS2501_VID_LO 0x00
#define NS2501_VID_HI 0x01
#define NS2501_DID_LO 0x02
#define NS2501_DID_HI 0x03
#define NS2501_REV 0x04
#define NS2501_RSVD 0x05
#define NS2501_FREQ_LO 0x06
#define NS2501_FREQ_HI 0x07

#define NS2501_REG8 0x08
#define NS2501_8_VEN (1<<5)
#define NS2501_8_HEN (1<<4)
#define NS2501_8_DSEL (1<<3)
#define NS2501_8_BPAS (1<<2)
#define NS2501_8_RSVD (1<<1)
#define NS2501_8_PD (1<<0)

#define NS2501_REG9 0x09
#define NS2501_9_VLOW (1<<7)
#define NS2501_9_MSEL_MASK (0x7<<4)
#define NS2501_9_TSEL (1<<3)
#define NS2501_9_RSEN (1<<2)
#define NS2501_9_RSVD (1<<1)
#define NS2501_9_MDI (1<<0)

#define NS2501_REGC 0x0c

/*
 * The following registers are not part of the official datasheet
 * and are the result of reverse engineering.
 */

/*
 * Register c0 controls how the DVO synchronizes with
 * its input.
 */
#define NS2501_REGC0 0xc0
#define NS2501_C0_ENABLE (1<<0)	/* enable the DVO sync in general */
#define NS2501_C0_HSYNC (1<<1)	/* synchronize horizontal with input */
#define NS2501_C0_VSYNC (1<<2)	/* synchronize vertical with input */
#define NS2501_C0_RESET (1<<7)	/* reset the synchronization flip/flops */

/*
 * Register 41 is somehow related to the sync register and sync
 * configuration. It should be 0x32 whenever regC0 is 0x05 (hsync off)
 * and 0x00 otherwise.
 */
#define NS2501_REG41 0x41

/*
 * this register controls the dithering of the DVO
 * One bit enables it, the other define the dithering depth.
 * The higher the value, the lower the dithering depth.
 */
#define NS2501_F9_REG 0xf9
#define NS2501_F9_ENABLE (1<<0)		/* if set, dithering is enabled */
#define NS2501_F9_DITHER_MASK (0x7f<<1)	/* controls the dither depth */
#define NS2501_F9_DITHER_SHIFT 1	/* shifts the dither mask */

/*
 * PLL configuration register. This is a pair of registers,
 * one single byte register at 1B, and a pair at 1C,1D.
 * These registers are counters/dividers.
 */
#define NS2501_REG1B 0x1b /* one byte PLL control register */
#define NS2501_REG1C 0x1c /* low-part of the second register */
#define NS2501_REG1D 0x1d /* high-part of the second register */

/*
 * Scaler control registers. Horizontal at b8,b9,
 * vertical at 10,11. The scale factor is computed as
 * 2^16/control-value. The low-byte comes first.
 */
#define NS2501_REG10 0x10 /* low-byte vertical scaler */
#define NS2501_REG11 0x11 /* high-byte vertical scaler */
#define NS2501_REGB8 0xb8 /* low-byte horizontal scaler */
#define NS2501_REGB9 0xb9 /* high-byte horizontal scaler */

/*
 * Display window definition. This consists of four registers
 * per dimension. One register pair defines the start of the
 * display, one the end.
 * As far as I understand, this defines the window within which
 * the scaler samples the input.
 */
#define NS2501_REGC1 0xc1 /* low-byte horizontal display start */
#define NS2501_REGC2 0xc2 /* high-byte horizontal display start */
#define NS2501_REGC3 0xc3 /* low-byte horizontal display stop */
#define NS2501_REGC4 0xc4 /* high-byte horizontal display stop */
#define NS2501_REGC5 0xc5 /* low-byte vertical display start */
#define NS2501_REGC6 0xc6 /* high-byte vertical display start */
#define NS2501_REGC7 0xc7 /* low-byte vertical display stop */
#define NS2501_REGC8 0xc8 /* high-byte vertical display stop */

/*
 * The following register pair seems to define the start of
 * the vertical sync. If automatic syncing is enabled, and the
 * register value defines a sync pulse that is later than the
 * incoming sync, then the register value is ignored and the
 * external hsync triggers the synchronization.
 */
#define NS2501_REG80 0x80 /* low-byte vsync-start */
#define NS2501_REG81 0x81 /* high-byte vsync-start */

/*
 * The following register pair seems to define the total number
 * of lines created at the output side of the scaler.
 * This is again a low-high register pair.
 */
#define NS2501_REG82 0x82 /* output display height, low byte */
#define NS2501_REG83 0x83 /* output display height, high byte */

/*
 * The following registers define the end of the front-porch
 * in horizontal and vertical position and hence allow to shift
 * the image left/right or up/down.
 */
#define NS2501_REG98 0x98 /* horizontal start of display + 256, low */
#define NS2501_REG99 0x99 /* horizontal start of display + 256, high */
#define NS2501_REG8E 0x8e /* vertical start of the display, low byte */
#define NS2501_REG8F 0x8f /* vertical start of the display, high byte */

/*
 * The following register pair control the function of the
 * backlight and the DVO output. To enable the corresponding
 * function, the corresponding bit must be set in both registers.
 */
#define NS2501_REG34 0x34 /* DVO enable functions, first register */
#define NS2501_REG35 0x35 /* DVO enable functions, second register */
#define NS2501_34_ENABLE_OUTPUT (1<<0) /* enable DVO output */
#define NS2501_34_ENABLE_BACKLIGHT (1<<1) /* enable backlight */

/*
 * Registers 9C and 9D define the vertical output offset
 * of the visible region.
 */
#define NS2501_REG9C 0x9c
#define NS2501_REG9D 0x9d

/*
 * The register 9F defines the dithering. This requires the
 * scaler to be ON. Bit 0 enables dithering, the remaining
 * bits control the depth of the dither. The higher the value,
 * the LOWER the dithering amplitude. A good value seems to be
 * 15 (total register value).
 */
#define NS2501_REGF9 0xf9
#define NS2501_F9_ENABLE_DITHER (1<<0) /* enable dithering */
#define NS2501_F9_DITHER_MASK (0x7f<<1) /* dither masking */
#define NS2501_F9_DITHER_SHIFT 1	/* upshift of the dither mask */

enum {
	MODE_640x480,
	MODE_800x600,
	MODE_1024x768,
};

struct ns2501_reg {
	u8 offset;
	u8 value;
};

/*
 * The following structure keeps the complete configuration of
 * the DVO, given a specific output configuration.
 * This is pretty much guess-work from reverse-engineering, so
 * read all this with a grain of salt.
 */
struct ns2501_configuration {
	u8 sync;		/* configuration of the C0 register */
	u8 conf;		/* configuration register 8 */
	u8 syncb;		/* configuration register 41 */
	u8 dither;		/* configuration of the dithering */
	u8 pll_a;		/* PLL configuration, register A, 1B */
	u16 pll_b;		/* PLL configuration, register B, 1C/1D */
	u16 hstart;		/* horizontal start, registers C1/C2 */
	u16 hstop;		/* horizontal total, registers C3/C4 */
	u16 vstart;		/* vertical start, registers C5/C6 */
	u16 vstop;		/* vertical total, registers C7/C8 */
	u16 vsync;		/* manual vertical sync start, 80/81 */
	u16 vtotal;		/* number of lines generated, 82/83 */
	u16 hpos;		/* horizontal position + 256, 98/99  */
	u16 vpos;		/* vertical position, 8e/8f */
	u16 voffs;		/* vertical output offset, 9c/9d */
	u16 hscale;		/* horizontal scaling factor, b8/b9 */
	u16 vscale;		/* vertical scaling factor, 10/11 */
};

/*
 * DVO configuration values, partially based on what the BIOS
 * of the Fujitsu Lifebook S6010 writes into registers,
 * partially found by manual tweaking. These configurations assume
 * a 1024x768 panel.
 */
static const struct ns2501_configuration ns2501_modes[] = {
	[MODE_640x480] = {
		.sync	= NS2501_C0_ENABLE | NS2501_C0_VSYNC,
		.conf	= NS2501_8_VEN | NS2501_8_HEN | NS2501_8_PD,
		.syncb	= 0x32,
		.dither	= 0x0f,
		.pll_a	= 17,
		.pll_b	= 852,
		.hstart	= 144,
		.hstop	= 783,
		.vstart	= 22,
		.vstop	= 514,
		.vsync	= 2047, /* actually, ignored with this config */
		.vtotal	= 1341,
		.hpos	= 0,
		.vpos	= 16,
		.voffs	= 36,
		.hscale	= 40960,
		.vscale	= 40960
	},
	[MODE_800x600] = {
		.sync	= NS2501_C0_ENABLE |
			  NS2501_C0_HSYNC | NS2501_C0_VSYNC,
		.conf   = NS2501_8_VEN | NS2501_8_HEN | NS2501_8_PD,
		.syncb	= 0x00,
		.dither	= 0x0f,
		.pll_a	= 25,
		.pll_b	= 612,
		.hstart	= 215,
		.hstop	= 1016,
		.vstart	= 26,
		.vstop	= 627,
		.vsync	= 807,
		.vtotal	= 1341,
		.hpos	= 0,
		.vpos	= 4,
		.voffs	= 35,
		.hscale	= 51248,
		.vscale	= 51232
	},
	[MODE_1024x768] = {
		.sync	= NS2501_C0_ENABLE | NS2501_C0_VSYNC,
		.conf   = NS2501_8_VEN | NS2501_8_HEN | NS2501_8_PD,
		.syncb	= 0x32,
		.dither	= 0x0f,
		.pll_a	= 11,
		.pll_b	= 1350,
		.hstart	= 276,
		.hstop	= 1299,
		.vstart	= 15,
		.vstop	= 1056,
		.vsync	= 2047,
		.vtotal	= 1341,
		.hpos	= 0,
		.vpos	= 7,
		.voffs	= 27,
		.hscale	= 65535,
		.vscale	= 65535
	}
};

/*
 * Other configuration values left by the BIOS of the
 * Fujitsu S6010 in the DVO control registers. Their
 * value does not depend on the BIOS and their meaning
 * is unknown.
 */

static const struct ns2501_reg mode_agnostic_values[] = {
	/* 08 is mode specific */
	[0] = { .offset = 0x0a, .value = 0x81, },
	/* 10,11 are part of the mode specific configuration */
	[1] = { .offset = 0x12, .value = 0x02, },
	[2] = { .offset = 0x18, .value = 0x07, },
	[3] = { .offset = 0x19, .value = 0x00, },
	[4] = { .offset = 0x1a, .value = 0x00, }, /* PLL?, ignored */
	/* 1b,1c,1d are part of the mode specific configuration */
	[5] = { .offset = 0x1e, .value = 0x02, },
	[6] = { .offset = 0x1f, .value = 0x40, },
	[7] = { .offset = 0x20, .value = 0x00, },
	[8] = { .offset = 0x21, .value = 0x00, },
	[9] = { .offset = 0x22, .value = 0x00, },
	[10] = { .offset = 0x23, .value = 0x00, },
	[11] = { .offset = 0x24, .value = 0x00, },
	[12] = { .offset = 0x25, .value = 0x00, },
	[13] = { .offset = 0x26, .value = 0x00, },
	[14] = { .offset = 0x27, .value = 0x00, },
	[15] = { .offset = 0x7e, .value = 0x18, },
	/* 80-84 are part of the mode-specific configuration */
	[16] = { .offset = 0x84, .value = 0x00, },
	[17] = { .offset = 0x85, .value = 0x00, },
	[18] = { .offset = 0x86, .value = 0x00, },
	[19] = { .offset = 0x87, .value = 0x00, },
	[20] = { .offset = 0x88, .value = 0x00, },
	[21] = { .offset = 0x89, .value = 0x00, },
	[22] = { .offset = 0x8a, .value = 0x00, },
	[23] = { .offset = 0x8b, .value = 0x00, },
	[24] = { .offset = 0x8c, .value = 0x10, },
	[25] = { .offset = 0x8d, .value = 0x02, },
	/* 8e,8f are part of the mode-specific configuration */
	[26] = { .offset = 0x90, .value = 0xff, },
	[27] = { .offset = 0x91, .value = 0x07, },
	[28] = { .offset = 0x92, .value = 0xa0, },
	[29] = { .offset = 0x93, .value = 0x02, },
	[30] = { .offset = 0x94, .value = 0x00, },
	[31] = { .offset = 0x95, .value = 0x00, },
	[32] = { .offset = 0x96, .value = 0x05, },
	[33] = { .offset = 0x97, .value = 0x00, },
	/* 98,99 are part of the mode-specific configuration */
	[34] = { .offset = 0x9a, .value = 0x88, },
	[35] = { .offset = 0x9b, .value = 0x00, },
	/* 9c,9d are part of the mode-specific configuration */
	[36] = { .offset = 0x9e, .value = 0x25, },
	[37] = { .offset = 0x9f, .value = 0x03, },
	[38] = { .offset = 0xa0, .value = 0x28, },
	[39] = { .offset = 0xa1, .value = 0x01, },
	[40] = { .offset = 0xa2, .value = 0x28, },
	[41] = { .offset = 0xa3, .value = 0x05, },
	/* register 0xa4 is mode specific, but 0x80..0x84 works always */
	[42] = { .offset = 0xa4, .value = 0x84, },
	[43] = { .offset = 0xa5, .value = 0x00, },
	[44] = { .offset = 0xa6, .value = 0x00, },
	[45] = { .offset = 0xa7, .value = 0x00, },
	[46] = { .offset = 0xa8, .value = 0x00, },
	/* 0xa9 to 0xab are mode specific, but have no visible effect */
	[47] = { .offset = 0xa9, .value = 0x04, },
	[48] = { .offset = 0xaa, .value = 0x70, },
	[49] = { .offset = 0xab, .value = 0x4f, },
	[50] = { .offset = 0xac, .value = 0x00, },
	[51] = { .offset = 0xad, .value = 0x00, },
	[52] = { .offset = 0xb6, .value = 0x09, },
	[53] = { .offset = 0xb7, .value = 0x03, },
	/* b8,b9 are part of the mode-specific configuration */
	[54] = { .offset = 0xba, .value = 0x00, },
	[55] = { .offset = 0xbb, .value = 0x20, },
	[56] = { .offset = 0xf3, .value = 0x90, },
	[57] = { .offset = 0xf4, .value = 0x00, },
	[58] = { .offset = 0xf7, .value = 0x88, },
	/* f8 is mode specific, but the value does not matter */
	[59] = { .offset = 0xf8, .value = 0x0a, },
	[60] = { .offset = 0xf9, .value = 0x00, }
};

static const struct ns2501_reg regs_init[] = {
	[0] = { .offset = 0x35, .value = 0xff, },
	[1] = { .offset = 0x34, .value = 0x00, },
	[2] = { .offset = 0x08, .value = 0x30, },
};

struct ns2501_priv {
	bool quiet;
	const struct ns2501_configuration *conf;
};

#define NSPTR(d) ((NS2501Ptr)(d->DriverPrivate.ptr))

/*
** Read a register from the ns2501.
** Returns true if successful, false otherwise.
** If it returns false, it might be wise to enable the
** DVO with the above function.
*/
static bool ns2501_readb(struct intel_dvo_device *dvo, int addr, u8 *ch)
{
	struct ns2501_priv *ns = dvo->dev_priv;
	struct i2c_adapter *adapter = dvo->i2c_bus;
	u8 out_buf[2];
	u8 in_buf[2];

	struct i2c_msg msgs[] = {
		{
		 .addr = dvo->slave_addr,
		 .flags = 0,
		 .len = 1,
		 .buf = out_buf,
		 },
		{
		 .addr = dvo->slave_addr,
		 .flags = I2C_M_RD,
		 .len = 1,
		 .buf = in_buf,
		 }
	};

	out_buf[0] = addr;
	out_buf[1] = 0;

	if (i2c_transfer(adapter, msgs, 2) == 2) {
		*ch = in_buf[0];
		return true;
	}

	if (!ns->quiet) {
		DRM_DEBUG_KMS
		    ("Unable to read register 0x%02x from %s:0x%02x.\n", addr,
		     adapter->name, dvo->slave_addr);
	}

	return false;
}

/*
** Write a register to the ns2501.
** Returns true if successful, false otherwise.
** If it returns false, it might be wise to enable the
** DVO with the above function.
*/
static bool ns2501_writeb(struct intel_dvo_device *dvo, int addr, u8 ch)
{
	struct ns2501_priv *ns = dvo->dev_priv;
	struct i2c_adapter *adapter = dvo->i2c_bus;
	u8 out_buf[2];

	struct i2c_msg msg = {
		.addr = dvo->slave_addr,
		.flags = 0,
		.len = 2,
		.buf = out_buf,
	};

	out_buf[0] = addr;
	out_buf[1] = ch;

	if (i2c_transfer(adapter, &msg, 1) == 1) {
		return true;
	}

	if (!ns->quiet) {
		DRM_DEBUG_KMS("Unable to write register 0x%02x to %s:%d\n",
			      addr, adapter->name, dvo->slave_addr);
	}

	return false;
}

/* National Semiconductor 2501 driver for chip on i2c bus
 * scan for the chip on the bus.
 * Hope the VBIOS initialized the PLL correctly so we can
 * talk to it. If not, it will not be seen and not detected.
 * Bummer!
 */
static bool ns2501_init(struct intel_dvo_device *dvo,
			struct i2c_adapter *adapter)
{
	/* this will detect the NS2501 chip on the specified i2c bus */
	struct ns2501_priv *ns;
	unsigned char ch;

	ns = kzalloc(sizeof(struct ns2501_priv), GFP_KERNEL);
	if (ns == NULL)
		return false;

	dvo->i2c_bus = adapter;
	dvo->dev_priv = ns;
	ns->quiet = true;

	if (!ns2501_readb(dvo, NS2501_VID_LO, &ch))
		goto out;

	if (ch != (NS2501_VID & 0xff)) {
		DRM_DEBUG_KMS("ns2501 not detected got %d: from %s Slave %d.\n",
			      ch, adapter->name, dvo->slave_addr);
		goto out;
	}

	if (!ns2501_readb(dvo, NS2501_DID_LO, &ch))
		goto out;

	if (ch != (NS2501_DID & 0xff)) {
		DRM_DEBUG_KMS("ns2501 not detected got %d: from %s Slave %d.\n",
			      ch, adapter->name, dvo->slave_addr);
		goto out;
	}
	ns->quiet = false;

	DRM_DEBUG_KMS("init ns2501 dvo controller successfully!\n");

	return true;

out:
	kfree(ns);
	return false;
}

static enum drm_connector_status ns2501_detect(struct intel_dvo_device *dvo)
{
	/*
	 * This is a Laptop display, it doesn't have hotplugging.
	 * Even if not, the detection bit of the 2501 is unreliable as
	 * it only works for some display types.
	 * It is even more unreliable as the PLL must be active for
	 * allowing reading from the chiop.
	 */
	return connector_status_connected;
}

static enum drm_mode_status ns2501_mode_valid(struct intel_dvo_device *dvo,
					      struct drm_display_mode *mode)
{
	DRM_DEBUG_KMS
	    ("is mode valid (hdisplay=%d,htotal=%d,vdisplay=%d,vtotal=%d)\n",
	     mode->hdisplay, mode->htotal, mode->vdisplay, mode->vtotal);

	/*
	 * Currently, these are all the modes I have data from.
	 * More might exist. Unclear how to find the native resolution
	 * of the panel in here so we could always accept it
	 * by disabling the scaler.
	 */
	if ((mode->hdisplay == 640 && mode->vdisplay == 480 && mode->clock == 25175) ||
	    (mode->hdisplay == 800 && mode->vdisplay == 600 && mode->clock == 40000) ||
	    (mode->hdisplay == 1024 && mode->vdisplay == 768 && mode->clock == 65000)) {
		return MODE_OK;
	} else {
		return MODE_ONE_SIZE;	/* Is this a reasonable error? */
	}
}

static void ns2501_mode_set(struct intel_dvo_device *dvo,
			    const struct drm_display_mode *mode,
			    const struct drm_display_mode *adjusted_mode)
{
	const struct ns2501_configuration *conf;
	struct ns2501_priv *ns = (struct ns2501_priv *)(dvo->dev_priv);
	int mode_idx, i;

	DRM_DEBUG_KMS
	    ("set mode (hdisplay=%d,htotal=%d,vdisplay=%d,vtotal=%d).\n",
	     mode->hdisplay, mode->htotal, mode->vdisplay, mode->vtotal);

	DRM_DEBUG_KMS("Detailed requested mode settings are:\n"
			"clock		: %d kHz\n"
			"hdisplay	: %d\n"
			"hblank start	: %d\n"
			"hblank end	: %d\n"
			"hsync start	: %d\n"
			"hsync end	: %d\n"
			"htotal		: %d\n"
			"hskew		: %d\n"
			"vdisplay	: %d\n"
			"vblank start	: %d\n"
			"hblank end	: %d\n"
			"vsync start	: %d\n"
			"vsync end	: %d\n"
			"vtotal		: %d\n",
			adjusted_mode->crtc_clock,
			adjusted_mode->crtc_hdisplay,
			adjusted_mode->crtc_hblank_start,
			adjusted_mode->crtc_hblank_end,
			adjusted_mode->crtc_hsync_start,
			adjusted_mode->crtc_hsync_end,
			adjusted_mode->crtc_htotal,
			adjusted_mode->crtc_hskew,
			adjusted_mode->crtc_vdisplay,
			adjusted_mode->crtc_vblank_start,
			adjusted_mode->crtc_vblank_end,
			adjusted_mode->crtc_vsync_start,
			adjusted_mode->crtc_vsync_end,
			adjusted_mode->crtc_vtotal);

	if (mode->hdisplay == 640 && mode->vdisplay == 480)
		mode_idx = MODE_640x480;
	else if (mode->hdisplay == 800 && mode->vdisplay == 600)
		mode_idx = MODE_800x600;
	else if (mode->hdisplay == 1024 && mode->vdisplay == 768)
		mode_idx = MODE_1024x768;
	else
		return;

	/* Hopefully doing it every time won't hurt... */
	for (i = 0; i < ARRAY_SIZE(regs_init); i++)
		ns2501_writeb(dvo, regs_init[i].offset, regs_init[i].value);

	/* Write the mode-agnostic values */
	for (i = 0; i < ARRAY_SIZE(mode_agnostic_values); i++)
		ns2501_writeb(dvo, mode_agnostic_values[i].offset,
				mode_agnostic_values[i].value);

	/* Write now the mode-specific configuration */
	conf = ns2501_modes + mode_idx;
	ns->conf = conf;

	ns2501_writeb(dvo, NS2501_REG8, conf->conf);
	ns2501_writeb(dvo, NS2501_REG1B, conf->pll_a);
	ns2501_writeb(dvo, NS2501_REG1C, conf->pll_b & 0xff);
	ns2501_writeb(dvo, NS2501_REG1D, conf->pll_b >> 8);
	ns2501_writeb(dvo, NS2501_REGC1, conf->hstart & 0xff);
	ns2501_writeb(dvo, NS2501_REGC2, conf->hstart >> 8);
	ns2501_writeb(dvo, NS2501_REGC3, conf->hstop & 0xff);
	ns2501_writeb(dvo, NS2501_REGC4, conf->hstop >> 8);
	ns2501_writeb(dvo, NS2501_REGC5, conf->vstart & 0xff);
	ns2501_writeb(dvo, NS2501_REGC6, conf->vstart >> 8);
	ns2501_writeb(dvo, NS2501_REGC7, conf->vstop & 0xff);
	ns2501_writeb(dvo, NS2501_REGC8, conf->vstop >> 8);
	ns2501_writeb(dvo, NS2501_REG80, conf->vsync & 0xff);
	ns2501_writeb(dvo, NS2501_REG81, conf->vsync >> 8);
	ns2501_writeb(dvo, NS2501_REG82, conf->vtotal & 0xff);
	ns2501_writeb(dvo, NS2501_REG83, conf->vtotal >> 8);
	ns2501_writeb(dvo, NS2501_REG98, conf->hpos & 0xff);
	ns2501_writeb(dvo, NS2501_REG99, conf->hpos >> 8);
	ns2501_writeb(dvo, NS2501_REG8E, conf->vpos & 0xff);
	ns2501_writeb(dvo, NS2501_REG8F, conf->vpos >> 8);
	ns2501_writeb(dvo, NS2501_REG9C, conf->voffs & 0xff);
	ns2501_writeb(dvo, NS2501_REG9D, conf->voffs >> 8);
	ns2501_writeb(dvo, NS2501_REGB8, conf->hscale & 0xff);
	ns2501_writeb(dvo, NS2501_REGB9, conf->hscale >> 8);
	ns2501_writeb(dvo, NS2501_REG10, conf->vscale & 0xff);
	ns2501_writeb(dvo, NS2501_REG11, conf->vscale >> 8);
	ns2501_writeb(dvo, NS2501_REGF9, conf->dither);
	ns2501_writeb(dvo, NS2501_REG41, conf->syncb);
	ns2501_writeb(dvo, NS2501_REGC0, conf->sync);
}

/* set the NS2501 power state */
static bool ns2501_get_hw_state(struct intel_dvo_device *dvo)
{
	unsigned char ch;

	if (!ns2501_readb(dvo, NS2501_REG8, &ch))
		return false;

	return ch & NS2501_8_PD;
}

/* set the NS2501 power state */
static void ns2501_dpms(struct intel_dvo_device *dvo, bool enable)
{
	struct ns2501_priv *ns = (struct ns2501_priv *)(dvo->dev_priv);

	DRM_DEBUG_KMS("Trying set the dpms of the DVO to %i\n", enable);

	if (enable) {
		ns2501_writeb(dvo, NS2501_REGC0, ns->conf->sync | 0x08);

		ns2501_writeb(dvo, NS2501_REG41, ns->conf->syncb);

		ns2501_writeb(dvo, NS2501_REG34, NS2501_34_ENABLE_OUTPUT);
		msleep(15);

		ns2501_writeb(dvo, NS2501_REG8,
				ns->conf->conf | NS2501_8_BPAS);
		if (!(ns->conf->conf & NS2501_8_BPAS))
			ns2501_writeb(dvo, NS2501_REG8, ns->conf->conf);
		msleep(200);

		ns2501_writeb(dvo, NS2501_REG34,
			NS2501_34_ENABLE_OUTPUT | NS2501_34_ENABLE_BACKLIGHT);

		ns2501_writeb(dvo, NS2501_REGC0, ns->conf->sync);
	} else {
		ns2501_writeb(dvo, NS2501_REG34, NS2501_34_ENABLE_OUTPUT);
		msleep(200);

		ns2501_writeb(dvo, NS2501_REG8, NS2501_8_VEN | NS2501_8_HEN |
				NS2501_8_BPAS);
		msleep(15);

		ns2501_writeb(dvo, NS2501_REG34, 0x00);
	}
}

static void ns2501_destroy(struct intel_dvo_device *dvo)
{
	struct ns2501_priv *ns = dvo->dev_priv;

	if (ns) {
		kfree(ns);
		dvo->dev_priv = NULL;
	}
}

const struct intel_dvo_dev_ops ns2501_ops = {
	.init = ns2501_init,
	.detect = ns2501_detect,
	.mode_valid = ns2501_mode_valid,
	.mode_set = ns2501_mode_set,
	.dpms = ns2501_dpms,
	.get_hw_state = ns2501_get_hw_state,
	.destroy = ns2501_destroy,
};
