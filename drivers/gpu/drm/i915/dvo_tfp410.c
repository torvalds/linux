/*
 * Copyright © 2007 Dave Mueller
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Dave Mueller <dave.mueller@gmx.ch>
 *
 */

#include "dvo.h"

/* register definitions according to the TFP410 data sheet */
#define TFP410_VID		0x014C
#define TFP410_DID		0x0410

#define TFP410_VID_LO		0x00
#define TFP410_VID_HI		0x01
#define TFP410_DID_LO		0x02
#define TFP410_DID_HI		0x03
#define TFP410_REV		0x04

#define TFP410_CTL_1		0x08
#define TFP410_CTL_1_TDIS	(1<<6)
#define TFP410_CTL_1_VEN	(1<<5)
#define TFP410_CTL_1_HEN	(1<<4)
#define TFP410_CTL_1_DSEL	(1<<3)
#define TFP410_CTL_1_BSEL	(1<<2)
#define TFP410_CTL_1_EDGE	(1<<1)
#define TFP410_CTL_1_PD		(1<<0)

#define TFP410_CTL_2		0x09
#define TFP410_CTL_2_VLOW	(1<<7)
#define TFP410_CTL_2_MSEL_MASK	(0x7<<4)
#define TFP410_CTL_2_MSEL	(1<<4)
#define TFP410_CTL_2_TSEL	(1<<3)
#define TFP410_CTL_2_RSEN	(1<<2)
#define TFP410_CTL_2_HTPLG	(1<<1)
#define TFP410_CTL_2_MDI	(1<<0)

#define TFP410_CTL_3		0x0A
#define TFP410_CTL_3_DK_MASK 	(0x7<<5)
#define TFP410_CTL_3_DK		(1<<5)
#define TFP410_CTL_3_DKEN	(1<<4)
#define TFP410_CTL_3_CTL_MASK	(0x7<<1)
#define TFP410_CTL_3_CTL	(1<<1)

#define TFP410_USERCFG		0x0B

#define TFP410_DE_DLY		0x32

#define TFP410_DE_CTL		0x33
#define TFP410_DE_CTL_DEGEN	(1<<6)
#define TFP410_DE_CTL_VSPOL	(1<<5)
#define TFP410_DE_CTL_HSPOL	(1<<4)
#define TFP410_DE_CTL_DEDLY8	(1<<0)

#define TFP410_DE_TOP		0x34

#define TFP410_DE_CNT_LO	0x36
#define TFP410_DE_CNT_HI	0x37

#define TFP410_DE_LIN_LO	0x38
#define TFP410_DE_LIN_HI	0x39

#define TFP410_H_RES_LO		0x3A
#define TFP410_H_RES_HI		0x3B

#define TFP410_V_RES_LO		0x3C
#define TFP410_V_RES_HI		0x3D

struct tfp410_save_rec {
	uint8_t ctl1;
	uint8_t ctl2;
};

struct tfp410_priv {
	bool quiet;

	struct tfp410_save_rec saved_reg;
	struct tfp410_save_rec mode_reg;
};

static bool tfp410_readb(struct intel_dvo_device *dvo, int addr, uint8_t *ch)
{
	struct tfp410_priv *tfp = dvo->dev_priv;
	struct intel_i2c_chan *i2cbus = dvo->i2c_bus;
	u8 out_buf[2];
	u8 in_buf[2];

	struct i2c_msg msgs[] = {
		{
			.addr = i2cbus->slave_addr,
			.flags = 0,
			.len = 1,
			.buf = out_buf,
		},
		{
			.addr = i2cbus->slave_addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = in_buf,
		}
	};

	out_buf[0] = addr;
	out_buf[1] = 0;

	if (i2c_transfer(&i2cbus->adapter, msgs, 2) == 2) {
		*ch = in_buf[0];
		return true;
	};

	if (!tfp->quiet) {
		DRM_DEBUG("Unable to read register 0x%02x from %s:%02x.\n",
			  addr, i2cbus->adapter.name, i2cbus->slave_addr);
	}
	return false;
}

static bool tfp410_writeb(struct intel_dvo_device *dvo, int addr, uint8_t ch)
{
	struct tfp410_priv *tfp = dvo->dev_priv;
	struct intel_i2c_chan *i2cbus = dvo->i2c_bus;
	uint8_t out_buf[2];
	struct i2c_msg msg = {
		.addr = i2cbus->slave_addr,
		.flags = 0,
		.len = 2,
		.buf = out_buf,
	};

	out_buf[0] = addr;
	out_buf[1] = ch;

	if (i2c_transfer(&i2cbus->adapter, &msg, 1) == 1)
		return true;

	if (!tfp->quiet) {
		DRM_DEBUG("Unable to write register 0x%02x to %s:%d.\n",
			  addr, i2cbus->adapter.name, i2cbus->slave_addr);
	}

	return false;
}

static int tfp410_getid(struct intel_dvo_device *dvo, int addr)
{
	uint8_t ch1, ch2;

	if (tfp410_readb(dvo, addr+0, &ch1) &&
	    tfp410_readb(dvo, addr+1, &ch2))
		return ((ch2 << 8) & 0xFF00) | (ch1 & 0x00FF);

	return -1;
}

/* Ti TFP410 driver for chip on i2c bus */
static bool tfp410_init(struct intel_dvo_device *dvo,
			struct intel_i2c_chan *i2cbus)
{
	/* this will detect the tfp410 chip on the specified i2c bus */
	struct tfp410_priv *tfp;
	int id;

	tfp = kzalloc(sizeof(struct tfp410_priv), GFP_KERNEL);
	if (tfp == NULL)
		return false;

	dvo->i2c_bus = i2cbus;
	dvo->i2c_bus->slave_addr = dvo->slave_addr;
	dvo->dev_priv = tfp;
	tfp->quiet = true;

	if ((id = tfp410_getid(dvo, TFP410_VID_LO)) != TFP410_VID) {
		DRM_DEBUG("tfp410 not detected got VID %X: from %s Slave %d.\n",
			  id, i2cbus->adapter.name, i2cbus->slave_addr);
		goto out;
	}

	if ((id = tfp410_getid(dvo, TFP410_DID_LO)) != TFP410_DID) {
		DRM_DEBUG("tfp410 not detected got DID %X: from %s Slave %d.\n",
			  id, i2cbus->adapter.name, i2cbus->slave_addr);
		goto out;
	}
	tfp->quiet = false;
	return true;
out:
	kfree(tfp);
	return false;
}

static enum drm_connector_status tfp410_detect(struct intel_dvo_device *dvo)
{
	enum drm_connector_status ret = connector_status_disconnected;
	uint8_t ctl2;

	if (tfp410_readb(dvo, TFP410_CTL_2, &ctl2)) {
		if (ctl2 & TFP410_CTL_2_HTPLG)
			ret = connector_status_connected;
		else
			ret = connector_status_disconnected;
	}

	return ret;
}

static enum drm_mode_status tfp410_mode_valid(struct intel_dvo_device *dvo,
					      struct drm_display_mode *mode)
{
	return MODE_OK;
}

static void tfp410_mode_set(struct intel_dvo_device *dvo,
			    struct drm_display_mode *mode,
			    struct drm_display_mode *adjusted_mode)
{
    /* As long as the basics are set up, since we don't have clock dependencies
     * in the mode setup, we can just leave the registers alone and everything
     * will work fine.
     */
    /* don't do much */
    return;
}

/* set the tfp410 power state */
static void tfp410_dpms(struct intel_dvo_device *dvo, int mode)
{
	uint8_t ctl1;

	if (!tfp410_readb(dvo, TFP410_CTL_1, &ctl1))
		return;

	if (mode == DRM_MODE_DPMS_ON)
		ctl1 |= TFP410_CTL_1_PD;
	else
		ctl1 &= ~TFP410_CTL_1_PD;

	tfp410_writeb(dvo, TFP410_CTL_1, ctl1);
}

static void tfp410_dump_regs(struct intel_dvo_device *dvo)
{
	uint8_t val, val2;

	tfp410_readb(dvo, TFP410_REV, &val);
	DRM_DEBUG("TFP410_REV: 0x%02X\n", val);
	tfp410_readb(dvo, TFP410_CTL_1, &val);
	DRM_DEBUG("TFP410_CTL1: 0x%02X\n", val);
	tfp410_readb(dvo, TFP410_CTL_2, &val);
	DRM_DEBUG("TFP410_CTL2: 0x%02X\n", val);
	tfp410_readb(dvo, TFP410_CTL_3, &val);
	DRM_DEBUG("TFP410_CTL3: 0x%02X\n", val);
	tfp410_readb(dvo, TFP410_USERCFG, &val);
	DRM_DEBUG("TFP410_USERCFG: 0x%02X\n", val);
	tfp410_readb(dvo, TFP410_DE_DLY, &val);
	DRM_DEBUG("TFP410_DE_DLY: 0x%02X\n", val);
	tfp410_readb(dvo, TFP410_DE_CTL, &val);
	DRM_DEBUG("TFP410_DE_CTL: 0x%02X\n", val);
	tfp410_readb(dvo, TFP410_DE_TOP, &val);
	DRM_DEBUG("TFP410_DE_TOP: 0x%02X\n", val);
	tfp410_readb(dvo, TFP410_DE_CNT_LO, &val);
	tfp410_readb(dvo, TFP410_DE_CNT_HI, &val2);
	DRM_DEBUG("TFP410_DE_CNT: 0x%02X%02X\n", val2, val);
	tfp410_readb(dvo, TFP410_DE_LIN_LO, &val);
	tfp410_readb(dvo, TFP410_DE_LIN_HI, &val2);
	DRM_DEBUG("TFP410_DE_LIN: 0x%02X%02X\n", val2, val);
	tfp410_readb(dvo, TFP410_H_RES_LO, &val);
	tfp410_readb(dvo, TFP410_H_RES_HI, &val2);
	DRM_DEBUG("TFP410_H_RES: 0x%02X%02X\n", val2, val);
	tfp410_readb(dvo, TFP410_V_RES_LO, &val);
	tfp410_readb(dvo, TFP410_V_RES_HI, &val2);
	DRM_DEBUG("TFP410_V_RES: 0x%02X%02X\n", val2, val);
}

static void tfp410_save(struct intel_dvo_device *dvo)
{
	struct tfp410_priv *tfp = dvo->dev_priv;

	if (!tfp410_readb(dvo, TFP410_CTL_1, &tfp->saved_reg.ctl1))
		return;

	if (!tfp410_readb(dvo, TFP410_CTL_2, &tfp->saved_reg.ctl2))
		return;
}

static void tfp410_restore(struct intel_dvo_device *dvo)
{
	struct tfp410_priv *tfp = dvo->dev_priv;

	/* Restore it powered down initially */
	tfp410_writeb(dvo, TFP410_CTL_1, tfp->saved_reg.ctl1 & ~0x1);

	tfp410_writeb(dvo, TFP410_CTL_2, tfp->saved_reg.ctl2);
	tfp410_writeb(dvo, TFP410_CTL_1, tfp->saved_reg.ctl1);
}

static void tfp410_destroy(struct intel_dvo_device *dvo)
{
	struct tfp410_priv *tfp = dvo->dev_priv;

	if (tfp) {
		kfree(tfp);
		dvo->dev_priv = NULL;
	}
}

struct intel_dvo_dev_ops tfp410_ops = {
	.init = tfp410_init,
	.detect = tfp410_detect,
	.mode_valid = tfp410_mode_valid,
	.mode_set = tfp410_mode_set,
	.dpms = tfp410_dpms,
	.dump_regs = tfp410_dump_regs,
	.save = tfp410_save,
	.restore = tfp410_restore,
	.destroy = tfp410_destroy,
};
