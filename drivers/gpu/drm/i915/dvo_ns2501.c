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

#include "dvo.h"
#include "i915_reg.h"
#include "i915_drv.h"

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

struct ns2501_priv {
	//I2CDevRec d;
	bool quiet;
	int reg_8_shadow;
	int reg_8_set;
	// Shadow registers for i915
	int dvoc;
	int pll_a;
	int srcdim;
	int fw_blc;
};

#define NSPTR(d) ((NS2501Ptr)(d->DriverPrivate.ptr))

/*
 * For reasons unclear to me, the ns2501 at least on the Fujitsu/Siemens
 * laptops does not react on the i2c bus unless
 * both the PLL is running and the display is configured in its native
 * resolution.
 * This function forces the DVO on, and stores the registers it touches.
 * Afterwards, registers are restored to regular values.
 *
 * This is pretty much a hack, though it works.
 * Without that, ns2501_readb and ns2501_writeb fail
 * when switching the resolution.
 */

static void enable_dvo(struct intel_dvo_device *dvo)
{
	struct ns2501_priv *ns = (struct ns2501_priv *)(dvo->dev_priv);
	struct i2c_adapter *adapter = dvo->i2c_bus;
	struct intel_gmbus *bus = container_of(adapter,
					       struct intel_gmbus,
					       adapter);
	struct drm_i915_private *dev_priv = bus->dev_priv;

	DRM_DEBUG_KMS("%s: Trying to re-enable the DVO\n", __FUNCTION__);

	ns->dvoc = I915_READ(DVO_C);
	ns->pll_a = I915_READ(_DPLL_A);
	ns->srcdim = I915_READ(DVOC_SRCDIM);
	ns->fw_blc = I915_READ(FW_BLC);

	I915_WRITE(DVOC, 0x10004084);
	I915_WRITE(_DPLL_A, 0xd0820000);
	I915_WRITE(DVOC_SRCDIM, 0x400300);	// 1024x768
	I915_WRITE(FW_BLC, 0x1080304);

	I915_WRITE(DVOC, 0x90004084);
}

/*
 * Restore the I915 registers modified by the above
 * trigger function.
 */
static void restore_dvo(struct intel_dvo_device *dvo)
{
	struct i2c_adapter *adapter = dvo->i2c_bus;
	struct intel_gmbus *bus = container_of(adapter,
					       struct intel_gmbus,
					       adapter);
	struct drm_i915_private *dev_priv = bus->dev_priv;
	struct ns2501_priv *ns = (struct ns2501_priv *)(dvo->dev_priv);

	I915_WRITE(DVOC, ns->dvoc);
	I915_WRITE(_DPLL_A, ns->pll_a);
	I915_WRITE(DVOC_SRCDIM, ns->srcdim);
	I915_WRITE(FW_BLC, ns->fw_blc);
}

/*
** Read a register from the ns2501.
** Returns true if successful, false otherwise.
** If it returns false, it might be wise to enable the
** DVO with the above function.
*/
static bool ns2501_readb(struct intel_dvo_device *dvo, int addr, uint8_t * ch)
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
	};

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
static bool ns2501_writeb(struct intel_dvo_device *dvo, int addr, uint8_t ch)
{
	struct ns2501_priv *ns = dvo->dev_priv;
	struct i2c_adapter *adapter = dvo->i2c_bus;
	uint8_t out_buf[2];

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
	ns->reg_8_set = 0;
	ns->reg_8_shadow =
	    NS2501_8_PD | NS2501_8_BPAS | NS2501_8_VEN | NS2501_8_HEN;

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
	    ("%s: is mode valid (hdisplay=%d,htotal=%d,vdisplay=%d,vtotal=%d)\n",
	     __FUNCTION__, mode->hdisplay, mode->htotal, mode->vdisplay,
	     mode->vtotal);

	/*
	 * Currently, these are all the modes I have data from.
	 * More might exist. Unclear how to find the native resolution
	 * of the panel in here so we could always accept it
	 * by disabling the scaler.
	 */
	if ((mode->hdisplay == 800 && mode->vdisplay == 600) ||
	    (mode->hdisplay == 640 && mode->vdisplay == 480) ||
	    (mode->hdisplay == 1024 && mode->vdisplay == 768)) {
		return MODE_OK;
	} else {
		return MODE_ONE_SIZE;	/* Is this a reasonable error? */
	}
}

static void ns2501_mode_set(struct intel_dvo_device *dvo,
			    struct drm_display_mode *mode,
			    struct drm_display_mode *adjusted_mode)
{
	bool ok;
	bool restore = false;
	struct ns2501_priv *ns = (struct ns2501_priv *)(dvo->dev_priv);

	DRM_DEBUG_KMS
	    ("%s: set mode (hdisplay=%d,htotal=%d,vdisplay=%d,vtotal=%d).\n",
	     __FUNCTION__, mode->hdisplay, mode->htotal, mode->vdisplay,
	     mode->vtotal);

	/*
	 * Where do I find the native resolution for which scaling is not required???
	 *
	 * First trigger the DVO on as otherwise the chip does not appear on the i2c
	 * bus.
	 */
	do {
		ok = true;

		if (mode->hdisplay == 800 && mode->vdisplay == 600) {
			/* mode 277 */
			ns->reg_8_shadow &= ~NS2501_8_BPAS;
			DRM_DEBUG_KMS("%s: switching to 800x600\n",
				      __FUNCTION__);

			/*
			 * No, I do not know where this data comes from.
			 * It is just what the video bios left in the DVO, so
			 * I'm just copying it here over.
			 * This also means that I cannot support any other modes
			 * except the ones supported by the bios.
			 */
			ok &= ns2501_writeb(dvo, 0x11, 0xc8);	// 0xc7 also works.
			ok &= ns2501_writeb(dvo, 0x1b, 0x19);
			ok &= ns2501_writeb(dvo, 0x1c, 0x62);	// VBIOS left 0x64 here, but 0x62 works nicer
			ok &= ns2501_writeb(dvo, 0x1d, 0x02);

			ok &= ns2501_writeb(dvo, 0x34, 0x03);
			ok &= ns2501_writeb(dvo, 0x35, 0xff);

			ok &= ns2501_writeb(dvo, 0x80, 0x27);
			ok &= ns2501_writeb(dvo, 0x81, 0x03);
			ok &= ns2501_writeb(dvo, 0x82, 0x41);
			ok &= ns2501_writeb(dvo, 0x83, 0x05);

			ok &= ns2501_writeb(dvo, 0x8d, 0x02);
			ok &= ns2501_writeb(dvo, 0x8e, 0x04);
			ok &= ns2501_writeb(dvo, 0x8f, 0x00);

			ok &= ns2501_writeb(dvo, 0x90, 0xfe);	/* vertical. VBIOS left 0xff here, but 0xfe works better */
			ok &= ns2501_writeb(dvo, 0x91, 0x07);
			ok &= ns2501_writeb(dvo, 0x94, 0x00);
			ok &= ns2501_writeb(dvo, 0x95, 0x00);

			ok &= ns2501_writeb(dvo, 0x96, 0x00);

			ok &= ns2501_writeb(dvo, 0x99, 0x00);
			ok &= ns2501_writeb(dvo, 0x9a, 0x88);

			ok &= ns2501_writeb(dvo, 0x9c, 0x23);	/* Looks like first and last line of the image. */
			ok &= ns2501_writeb(dvo, 0x9d, 0x00);
			ok &= ns2501_writeb(dvo, 0x9e, 0x25);
			ok &= ns2501_writeb(dvo, 0x9f, 0x03);

			ok &= ns2501_writeb(dvo, 0xa4, 0x80);

			ok &= ns2501_writeb(dvo, 0xb6, 0x00);

			ok &= ns2501_writeb(dvo, 0xb9, 0xc8);	/* horizontal? */
			ok &= ns2501_writeb(dvo, 0xba, 0x00);	/* horizontal? */

			ok &= ns2501_writeb(dvo, 0xc0, 0x05);	/* horizontal? */
			ok &= ns2501_writeb(dvo, 0xc1, 0xd7);

			ok &= ns2501_writeb(dvo, 0xc2, 0x00);
			ok &= ns2501_writeb(dvo, 0xc3, 0xf8);

			ok &= ns2501_writeb(dvo, 0xc4, 0x03);
			ok &= ns2501_writeb(dvo, 0xc5, 0x1a);

			ok &= ns2501_writeb(dvo, 0xc6, 0x00);
			ok &= ns2501_writeb(dvo, 0xc7, 0x73);
			ok &= ns2501_writeb(dvo, 0xc8, 0x02);

		} else if (mode->hdisplay == 640 && mode->vdisplay == 480) {
			/* mode 274 */
			DRM_DEBUG_KMS("%s: switching to 640x480\n",
				      __FUNCTION__);
			/*
			 * No, I do not know where this data comes from.
			 * It is just what the video bios left in the DVO, so
			 * I'm just copying it here over.
			 * This also means that I cannot support any other modes
			 * except the ones supported by the bios.
			 */
			ns->reg_8_shadow &= ~NS2501_8_BPAS;

			ok &= ns2501_writeb(dvo, 0x11, 0xa0);
			ok &= ns2501_writeb(dvo, 0x1b, 0x11);
			ok &= ns2501_writeb(dvo, 0x1c, 0x54);
			ok &= ns2501_writeb(dvo, 0x1d, 0x03);

			ok &= ns2501_writeb(dvo, 0x34, 0x03);
			ok &= ns2501_writeb(dvo, 0x35, 0xff);

			ok &= ns2501_writeb(dvo, 0x80, 0xff);
			ok &= ns2501_writeb(dvo, 0x81, 0x07);
			ok &= ns2501_writeb(dvo, 0x82, 0x3d);
			ok &= ns2501_writeb(dvo, 0x83, 0x05);

			ok &= ns2501_writeb(dvo, 0x8d, 0x02);
			ok &= ns2501_writeb(dvo, 0x8e, 0x10);
			ok &= ns2501_writeb(dvo, 0x8f, 0x00);

			ok &= ns2501_writeb(dvo, 0x90, 0xff);	/* vertical */
			ok &= ns2501_writeb(dvo, 0x91, 0x07);
			ok &= ns2501_writeb(dvo, 0x94, 0x00);
			ok &= ns2501_writeb(dvo, 0x95, 0x00);

			ok &= ns2501_writeb(dvo, 0x96, 0x05);

			ok &= ns2501_writeb(dvo, 0x99, 0x00);
			ok &= ns2501_writeb(dvo, 0x9a, 0x88);

			ok &= ns2501_writeb(dvo, 0x9c, 0x24);
			ok &= ns2501_writeb(dvo, 0x9d, 0x00);
			ok &= ns2501_writeb(dvo, 0x9e, 0x25);
			ok &= ns2501_writeb(dvo, 0x9f, 0x03);

			ok &= ns2501_writeb(dvo, 0xa4, 0x84);

			ok &= ns2501_writeb(dvo, 0xb6, 0x09);

			ok &= ns2501_writeb(dvo, 0xb9, 0xa0);	/* horizontal? */
			ok &= ns2501_writeb(dvo, 0xba, 0x00);	/* horizontal? */

			ok &= ns2501_writeb(dvo, 0xc0, 0x05);	/* horizontal? */
			ok &= ns2501_writeb(dvo, 0xc1, 0x90);

			ok &= ns2501_writeb(dvo, 0xc2, 0x00);
			ok &= ns2501_writeb(dvo, 0xc3, 0x0f);

			ok &= ns2501_writeb(dvo, 0xc4, 0x03);
			ok &= ns2501_writeb(dvo, 0xc5, 0x16);

			ok &= ns2501_writeb(dvo, 0xc6, 0x00);
			ok &= ns2501_writeb(dvo, 0xc7, 0x02);
			ok &= ns2501_writeb(dvo, 0xc8, 0x02);

		} else if (mode->hdisplay == 1024 && mode->vdisplay == 768) {
			/* mode 280 */
			DRM_DEBUG_KMS("%s: switching to 1024x768\n",
				      __FUNCTION__);
			/*
			 * This might or might not work, actually. I'm silently
			 * assuming here that the native panel resolution is
			 * 1024x768. If not, then this leaves the scaler disabled
			 * generating a picture that is likely not the expected.
			 *
			 * Problem is that I do not know where to take the panel
			 * dimensions from.
			 *
			 * Enable the bypass, scaling not required.
			 *
			 * The scaler registers are irrelevant here....
			 *
			 */
			ns->reg_8_shadow |= NS2501_8_BPAS;
			ok &= ns2501_writeb(dvo, 0x37, 0x44);
		} else {
			/*
			 * Data not known. Bummer!
			 * Hopefully, the code should not go here
			 * as mode_OK delivered no other modes.
			 */
			ns->reg_8_shadow |= NS2501_8_BPAS;
		}
		ok &= ns2501_writeb(dvo, NS2501_REG8, ns->reg_8_shadow);

		if (!ok) {
			if (restore)
				restore_dvo(dvo);
			enable_dvo(dvo);
			restore = true;
		}
	} while (!ok);
	/*
	 * Restore the old i915 registers before
	 * forcing the ns2501 on.
	 */
	if (restore)
		restore_dvo(dvo);
}

/* set the NS2501 power state */
static bool ns2501_get_hw_state(struct intel_dvo_device *dvo)
{
	unsigned char ch;

	if (!ns2501_readb(dvo, NS2501_REG8, &ch))
		return false;

	if (ch & NS2501_8_PD)
		return true;
	else
		return false;
}

/* set the NS2501 power state */
static void ns2501_dpms(struct intel_dvo_device *dvo, bool enable)
{
	bool ok;
	bool restore = false;
	struct ns2501_priv *ns = (struct ns2501_priv *)(dvo->dev_priv);
	unsigned char ch;

	DRM_DEBUG_KMS("%s: Trying set the dpms of the DVO to %i\n",
		      __FUNCTION__, enable);

	ch = ns->reg_8_shadow;

	if (enable)
		ch |= NS2501_8_PD;
	else
		ch &= ~NS2501_8_PD;

	if (ns->reg_8_set == 0 || ns->reg_8_shadow != ch) {
		ns->reg_8_set = 1;
		ns->reg_8_shadow = ch;

		do {
			ok = true;
			ok &= ns2501_writeb(dvo, NS2501_REG8, ch);
			ok &=
			    ns2501_writeb(dvo, 0x34,
					  enable ? 0x03 : 0x00);
			ok &=
			    ns2501_writeb(dvo, 0x35,
					  enable ? 0xff : 0x00);
			if (!ok) {
				if (restore)
					restore_dvo(dvo);
				enable_dvo(dvo);
				restore = true;
			}
		} while (!ok);

		if (restore)
			restore_dvo(dvo);
	}
}

static void ns2501_dump_regs(struct intel_dvo_device *dvo)
{
	uint8_t val;

	ns2501_readb(dvo, NS2501_FREQ_LO, &val);
	DRM_LOG_KMS("NS2501_FREQ_LO: 0x%02x\n", val);
	ns2501_readb(dvo, NS2501_FREQ_HI, &val);
	DRM_LOG_KMS("NS2501_FREQ_HI: 0x%02x\n", val);
	ns2501_readb(dvo, NS2501_REG8, &val);
	DRM_LOG_KMS("NS2501_REG8: 0x%02x\n", val);
	ns2501_readb(dvo, NS2501_REG9, &val);
	DRM_LOG_KMS("NS2501_REG9: 0x%02x\n", val);
	ns2501_readb(dvo, NS2501_REGC, &val);
	DRM_LOG_KMS("NS2501_REGC: 0x%02x\n", val);
}

static void ns2501_destroy(struct intel_dvo_device *dvo)
{
	struct ns2501_priv *ns = dvo->dev_priv;

	if (ns) {
		kfree(ns);
		dvo->dev_priv = NULL;
	}
}

struct intel_dvo_dev_ops ns2501_ops = {
	.init = ns2501_init,
	.detect = ns2501_detect,
	.mode_valid = ns2501_mode_valid,
	.mode_set = ns2501_mode_set,
	.dpms = ns2501_dpms,
	.get_hw_state = ns2501_get_hw_state,
	.dump_regs = ns2501_dump_regs,
	.destroy = ns2501_destroy,
};
