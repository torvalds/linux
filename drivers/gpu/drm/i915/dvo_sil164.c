/**************************************************************************

Copyright © 2006 Dave Airlie

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

#include "dvo.h"

#define SIL164_VID 0x0001
#define SIL164_DID 0x0006

#define SIL164_VID_LO 0x00
#define SIL164_VID_HI 0x01
#define SIL164_DID_LO 0x02
#define SIL164_DID_HI 0x03
#define SIL164_REV    0x04
#define SIL164_RSVD   0x05
#define SIL164_FREQ_LO 0x06
#define SIL164_FREQ_HI 0x07

#define SIL164_REG8 0x08
#define SIL164_8_VEN (1<<5)
#define SIL164_8_HEN (1<<4)
#define SIL164_8_DSEL (1<<3)
#define SIL164_8_BSEL (1<<2)
#define SIL164_8_EDGE (1<<1)
#define SIL164_8_PD   (1<<0)

#define SIL164_REG9 0x09
#define SIL164_9_VLOW (1<<7)
#define SIL164_9_MSEL_MASK (0x7<<4)
#define SIL164_9_TSEL (1<<3)
#define SIL164_9_RSEN (1<<2)
#define SIL164_9_HTPLG (1<<1)
#define SIL164_9_MDI (1<<0)

#define SIL164_REGC 0x0c

struct sil164_priv {
	//I2CDevRec d;
	bool quiet;
};

#define SILPTR(d) ((SIL164Ptr)(d->DriverPrivate.ptr))

static bool sil164_readb(struct intel_dvo_device *dvo, int addr, uint8_t *ch)
{
	struct sil164_priv *sil = dvo->dev_priv;
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

	if (!sil->quiet) {
		DRM_DEBUG_KMS("Unable to read register 0x%02x from %s:%02x.\n",
			  addr, adapter->name, dvo->slave_addr);
	}
	return false;
}

static bool sil164_writeb(struct intel_dvo_device *dvo, int addr, uint8_t ch)
{
	struct sil164_priv *sil = dvo->dev_priv;
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

	if (i2c_transfer(adapter, &msg, 1) == 1)
		return true;

	if (!sil->quiet) {
		DRM_DEBUG_KMS("Unable to write register 0x%02x to %s:%d.\n",
			  addr, adapter->name, dvo->slave_addr);
	}

	return false;
}

/* Silicon Image 164 driver for chip on i2c bus */
static bool sil164_init(struct intel_dvo_device *dvo,
			struct i2c_adapter *adapter)
{
	/* this will detect the SIL164 chip on the specified i2c bus */
	struct sil164_priv *sil;
	unsigned char ch;

	sil = kzalloc(sizeof(struct sil164_priv), GFP_KERNEL);
	if (sil == NULL)
		return false;

	dvo->i2c_bus = adapter;
	dvo->dev_priv = sil;
	sil->quiet = true;

	if (!sil164_readb(dvo, SIL164_VID_LO, &ch))
		goto out;

	if (ch != (SIL164_VID & 0xff)) {
		DRM_DEBUG_KMS("sil164 not detected got %d: from %s Slave %d.\n",
			  ch, adapter->name, dvo->slave_addr);
		goto out;
	}

	if (!sil164_readb(dvo, SIL164_DID_LO, &ch))
		goto out;

	if (ch != (SIL164_DID & 0xff)) {
		DRM_DEBUG_KMS("sil164 not detected got %d: from %s Slave %d.\n",
			  ch, adapter->name, dvo->slave_addr);
		goto out;
	}
	sil->quiet = false;

	DRM_DEBUG_KMS("init sil164 dvo controller successfully!\n");
	return true;

out:
	kfree(sil);
	return false;
}

static enum drm_connector_status sil164_detect(struct intel_dvo_device *dvo)
{
	uint8_t reg9;

	sil164_readb(dvo, SIL164_REG9, &reg9);

	if (reg9 & SIL164_9_HTPLG)
		return connector_status_connected;
	else
		return connector_status_disconnected;
}

static enum drm_mode_status sil164_mode_valid(struct intel_dvo_device *dvo,
					      struct drm_display_mode *mode)
{
	return MODE_OK;
}

static void sil164_mode_set(struct intel_dvo_device *dvo,
			    struct drm_display_mode *mode,
			    struct drm_display_mode *adjusted_mode)
{
	/* As long as the basics are set up, since we don't have clock
	 * dependencies in the mode setup, we can just leave the
	 * registers alone and everything will work fine.
	 */
	/* recommended programming sequence from doc */
	/*sil164_writeb(sil, 0x08, 0x30);
	  sil164_writeb(sil, 0x09, 0x00);
	  sil164_writeb(sil, 0x0a, 0x90);
	  sil164_writeb(sil, 0x0c, 0x89);
	  sil164_writeb(sil, 0x08, 0x31);*/
	/* don't do much */
	return;
}

/* set the SIL164 power state */
static void sil164_dpms(struct intel_dvo_device *dvo, int mode)
{
	int ret;
	unsigned char ch;

	ret = sil164_readb(dvo, SIL164_REG8, &ch);
	if (ret == false)
		return;

	if (mode == DRM_MODE_DPMS_ON)
		ch |= SIL164_8_PD;
	else
		ch &= ~SIL164_8_PD;

	sil164_writeb(dvo, SIL164_REG8, ch);
	return;
}

static void sil164_dump_regs(struct intel_dvo_device *dvo)
{
	uint8_t val;

	sil164_readb(dvo, SIL164_FREQ_LO, &val);
	DRM_LOG_KMS("SIL164_FREQ_LO: 0x%02x\n", val);
	sil164_readb(dvo, SIL164_FREQ_HI, &val);
	DRM_LOG_KMS("SIL164_FREQ_HI: 0x%02x\n", val);
	sil164_readb(dvo, SIL164_REG8, &val);
	DRM_LOG_KMS("SIL164_REG8: 0x%02x\n", val);
	sil164_readb(dvo, SIL164_REG9, &val);
	DRM_LOG_KMS("SIL164_REG9: 0x%02x\n", val);
	sil164_readb(dvo, SIL164_REGC, &val);
	DRM_LOG_KMS("SIL164_REGC: 0x%02x\n", val);
}

static void sil164_destroy(struct intel_dvo_device *dvo)
{
	struct sil164_priv *sil = dvo->dev_priv;

	if (sil) {
		kfree(sil);
		dvo->dev_priv = NULL;
	}
}

struct intel_dvo_dev_ops sil164_ops = {
	.init = sil164_init,
	.detect = sil164_detect,
	.mode_valid = sil164_mode_valid,
	.mode_set = sil164_mode_set,
	.dpms = sil164_dpms,
	.dump_regs = sil164_dump_regs,
	.destroy = sil164_destroy,
};
