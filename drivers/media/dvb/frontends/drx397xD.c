/*
 * Driver for Micronas drx397xD demodulator
 *
 * Copyright (C) 2007 Henk Vergonet <Henk.Vergonet@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#define DEBUG			/* uncomment if you want debugging output */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/firmware.h>
#include <asm/div64.h>

#include "dvb_frontend.h"
#include "drx397xD.h"

static const char mod_name[] = "drx397xD";

#define MAX_CLOCK_DRIFT		200	/* maximal 200 PPM allowed */

#define F_SET_0D0h	1
#define F_SET_0D4h	2

enum fw_ix {
#define _FW_ENTRY(a, b, c)	b
#include "drx397xD_fw.h"
};

/* chip specifics */
struct drx397xD_state {
	struct i2c_adapter *i2c;
	struct dvb_frontend frontend;
	struct drx397xD_config config;
	enum fw_ix chip_rev;
	int flags;
	u32 bandwidth_parm;	/* internal bandwidth conversions */
	u32 f_osc;		/* w90: actual osc frequency [Hz] */
};

/* Firmware */
static const char *blob_name[] = {
#define _BLOB_ENTRY(a, b)		a
#include "drx397xD_fw.h"
};

enum blob_ix {
#define _BLOB_ENTRY(a, b)		b
#include "drx397xD_fw.h"
};

static struct {
	const char *name;
	const struct firmware *file;
	rwlock_t lock;
	int refcnt;
	const u8 *data[ARRAY_SIZE(blob_name)];
} fw[] = {
#define _FW_ENTRY(a, b, c)	{					\
			.name	= a,					\
			.file	= 0,					\
			.lock	= __RW_LOCK_UNLOCKED(fw[c].lock),	\
			.refcnt = 0,					\
			.data	= { }		}
#include "drx397xD_fw.h"
};

/* use only with writer lock aquired */
static void _drx_release_fw(struct drx397xD_state *s, enum fw_ix ix)
{
	memset(&fw[ix].data[0], 0, sizeof(fw[0].data));
	if (fw[ix].file)
		release_firmware(fw[ix].file);
}

static void drx_release_fw(struct drx397xD_state *s)
{
	enum fw_ix ix = s->chip_rev;

	pr_debug("%s\n", __func__);

	write_lock(&fw[ix].lock);
	if (fw[ix].refcnt) {
		fw[ix].refcnt--;
		if (fw[ix].refcnt == 0)
			_drx_release_fw(s, ix);
	}
	write_unlock(&fw[ix].lock);
}

static int drx_load_fw(struct drx397xD_state *s, enum fw_ix ix)
{
	const u8 *data;
	size_t size, len;
	int i = 0, j, rc = -EINVAL;

	pr_debug("%s\n", __func__);

	if (ix < 0 || ix >= ARRAY_SIZE(fw))
		return -EINVAL;
	s->chip_rev = ix;

	write_lock(&fw[ix].lock);
	if (fw[ix].file) {
		rc = 0;
		goto exit_ok;
	}
	memset(&fw[ix].data[0], 0, sizeof(fw[0].data));

	if (request_firmware(&fw[ix].file, fw[ix].name, &s->i2c->dev) != 0) {
		printk(KERN_ERR "%s: Firmware \"%s\" not available\n",
		       mod_name, fw[ix].name);
		rc = -ENOENT;
		goto exit_err;
	}

	if (!fw[ix].file->data || fw[ix].file->size < 10)
		goto exit_corrupt;

	data = fw[ix].file->data;
	size = fw[ix].file->size;

	if (data[i++] != 2)	/* check firmware version */
		goto exit_corrupt;

	do {
		switch (data[i++]) {
		case 0x00:	/* bytecode */
			if (i >= size)
				break;
			i += data[i];
		case 0x01:	/* reset */
		case 0x02:	/* sleep */
			i++;
			break;
		case 0xfe:	/* name */
			len = strnlen(&data[i], size - i);
			if (i + len + 1 >= size)
				goto exit_corrupt;
			if (data[i + len + 1] != 0)
				goto exit_corrupt;
			for (j = 0; j < ARRAY_SIZE(blob_name); j++) {
				if (strcmp(blob_name[j], &data[i]) == 0) {
					fw[ix].data[j] = &data[i + len + 1];
					pr_debug("Loading %s\n", blob_name[j]);
				}
			}
			i += len + 1;
			break;
		case 0xff:	/* file terminator */
			if (i == size) {
				rc = 0;
				goto exit_ok;
			}
		default:
			goto exit_corrupt;
		}
	} while (i < size);

exit_corrupt:
	printk(KERN_ERR "%s: Firmware is corrupt\n", mod_name);
exit_err:
	_drx_release_fw(s, ix);
	fw[ix].refcnt--;
exit_ok:
	fw[ix].refcnt++;
	write_unlock(&fw[ix].lock);

	return rc;
}

/* i2c bus IO */
static int write_fw(struct drx397xD_state *s, enum blob_ix ix)
{
	const u8 *data;
	int len, rc = 0, i = 0;
	struct i2c_msg msg = {
		.addr = s->config.demod_address,
		.flags = 0
	};

	if (ix < 0 || ix >= ARRAY_SIZE(blob_name)) {
		pr_debug("%s drx_fw_ix_t out of range\n", __func__);
		return -EINVAL;
	}
	pr_debug("%s %s\n", __func__, blob_name[ix]);

	read_lock(&fw[s->chip_rev].lock);
	data = fw[s->chip_rev].data[ix];
	if (!data) {
		rc = -EINVAL;
		goto exit_rc;
	}

	for (;;) {
		switch (data[i++]) {
		case 0:	/* bytecode */
			len = data[i++];
			msg.len = len;
			msg.buf = (__u8 *) &data[i];
			if (i2c_transfer(s->i2c, &msg, 1) != 1) {
				rc = -EIO;
				goto exit_rc;
			}
			i += len;
			break;
		case 1:	/* reset */
		case 2:	/* sleep */
			i++;
			break;
		default:
			goto exit_rc;
		}
	}
exit_rc:
	read_unlock(&fw[s->chip_rev].lock);

	return 0;
}

/* Function is not endian safe, use the RD16 wrapper below */
static int _read16(struct drx397xD_state *s, __le32 i2c_adr)
{
	int rc;
	u8 a[4];
	__le16 v;
	struct i2c_msg msg[2] = {
		{
			.addr = s->config.demod_address,
			.flags = 0,
			.buf = a,
			.len = sizeof(a)
		}, {
			.addr = s->config.demod_address,
			.flags = I2C_M_RD,
			.buf = (u8 *)&v,
			.len = sizeof(v)
		}
	};

	*(__le32 *) a = i2c_adr;

	rc = i2c_transfer(s->i2c, msg, 2);
	if (rc != 2)
		return -EIO;

	return le16_to_cpu(v);
}

/* Function is not endian safe, use the WR16.. wrappers below */
static int _write16(struct drx397xD_state *s, __le32 i2c_adr, __le16 val)
{
	u8 a[6];
	int rc;
	struct i2c_msg msg = {
		.addr = s->config.demod_address,
		.flags = 0,
		.buf = a,
		.len = sizeof(a)
	};

	*(__le32 *)a = i2c_adr;
	*(__le16 *)&a[4] = val;

	rc = i2c_transfer(s->i2c, &msg, 1);
	if (rc != 1)
		return -EIO;

	return 0;
}

#define WR16(ss, adr, val) \
		_write16(ss, I2C_ADR_C0(adr), cpu_to_le16(val))
#define WR16_E0(ss, adr, val) \
		_write16(ss, I2C_ADR_E0(adr), cpu_to_le16(val))
#define RD16(ss, adr) \
		_read16(ss, I2C_ADR_C0(adr))

#define EXIT_RC(cmd)	\
	if ((rc = (cmd)) < 0)	\
		goto exit_rc

/* Tuner callback */
static int PLL_Set(struct drx397xD_state *s,
		   struct dvb_frontend_parameters *fep, int *df_tuner)
{
	struct dvb_frontend *fe = &s->frontend;
	u32 f_tuner, f = fep->frequency;
	int rc;

	pr_debug("%s\n", __func__);

	if ((f > s->frontend.ops.tuner_ops.info.frequency_max) ||
	    (f < s->frontend.ops.tuner_ops.info.frequency_min))
		return -EINVAL;

	*df_tuner = 0;
	if (!s->frontend.ops.tuner_ops.set_params ||
	    !s->frontend.ops.tuner_ops.get_frequency)
		return -ENOSYS;

	rc = s->frontend.ops.tuner_ops.set_params(fe, fep);
	if (rc < 0)
		return rc;

	rc = s->frontend.ops.tuner_ops.get_frequency(fe, &f_tuner);
	if (rc < 0)
		return rc;

	*df_tuner = f_tuner - f;
	pr_debug("%s requested %d [Hz] tuner %d [Hz]\n", __func__, f,
		 f_tuner);

	return 0;
}

/* Demodulator helper functions */
static int SC_WaitForReady(struct drx397xD_state *s)
{
	int cnt = 1000;
	int rc;

	pr_debug("%s\n", __func__);

	while (cnt--) {
		rc = RD16(s, 0x820043);
		if (rc == 0)
			return 0;
	}

	return -1;
}

static int SC_SendCommand(struct drx397xD_state *s, int cmd)
{
	int rc;

	pr_debug("%s\n", __func__);

	WR16(s, 0x820043, cmd);
	SC_WaitForReady(s);
	rc = RD16(s, 0x820042);
	if ((rc & 0xffff) == 0xffff)
		return -1;

	return 0;
}

static int HI_Command(struct drx397xD_state *s, u16 cmd)
{
	int rc, cnt = 1000;

	pr_debug("%s\n", __func__);

	rc = WR16(s, 0x420032, cmd);
	if (rc < 0)
		return rc;

	do {
		rc = RD16(s, 0x420032);
		if (rc == 0) {
			rc = RD16(s, 0x420031);
			return rc;
		}
		if (rc < 0)
			return rc;
	} while (--cnt);

	return rc;
}

static int HI_CfgCommand(struct drx397xD_state *s)
{

	pr_debug("%s\n", __func__);

	WR16(s, 0x420033, 0x3973);
	WR16(s, 0x420034, s->config.w50);	/* code 4, log 4 */
	WR16(s, 0x420035, s->config.w52);	/* code 15,  log 9 */
	WR16(s, 0x420036, s->config.demod_address << 1);
	WR16(s, 0x420037, s->config.w56);	/* code (set_i2c ??  initX 1 ), log 1 */
	/* WR16(s, 0x420033, 0x3973); */
	if ((s->config.w56 & 8) == 0)
		return HI_Command(s, 3);

	return WR16(s, 0x420032, 0x3);
}

static const u8 fastIncrDecLUT_15273[] = {
	0x0e, 0x0f, 0x0f, 0x10, 0x11, 0x12, 0x12, 0x13, 0x14,
	0x15, 0x16, 0x17, 0x18, 0x1a, 0x1b, 0x1c, 0x1d, 0x1f
};

static const u8 slowIncrDecLUT_15272[] = {
	3, 4, 4, 5, 6
};

static int SetCfgIfAgc(struct drx397xD_state *s, struct drx397xD_CfgIfAgc *agc)
{
	u16 w06 = agc->w06;
	u16 w08 = agc->w08;
	u16 w0A = agc->w0A;
	u16 w0C = agc->w0C;
	int quot, rem, i, rc = -EINVAL;

	pr_debug("%s\n", __func__);

	if (agc->w04 > 0x3ff)
		goto exit_rc;

	if (agc->d00 == 1) {
		EXIT_RC(RD16(s, 0x0c20010));
		rc &= ~0x10;
		EXIT_RC(WR16(s, 0x0c20010, rc));
		return WR16(s, 0x0c20030, agc->w04 & 0x7ff);
	}

	if (agc->d00 != 0)
		goto exit_rc;
	if (w0A < w08)
		goto exit_rc;
	if (w0A > 0x3ff)
		goto exit_rc;
	if (w0C > 0x3ff)
		goto exit_rc;
	if (w06 > 0x3ff)
		goto exit_rc;

	EXIT_RC(RD16(s, 0x0c20010));
	rc |= 0x10;
	EXIT_RC(WR16(s, 0x0c20010, rc));

	EXIT_RC(WR16(s, 0x0c20025, (w06 >> 1) & 0x1ff));
	EXIT_RC(WR16(s, 0x0c20031, (w0A - w08) >> 1));
	EXIT_RC(WR16(s, 0x0c20032, ((w0A + w08) >> 1) - 0x1ff));

	quot = w0C / 113;
	rem = w0C % 113;
	if (quot <= 8) {
		quot = 8 - quot;
	} else {
		quot = 0;
		rem += 113;
	}

	EXIT_RC(WR16(s, 0x0c20024, quot));

	i = fastIncrDecLUT_15273[rem / 8];
	EXIT_RC(WR16(s, 0x0c2002d, i));
	EXIT_RC(WR16(s, 0x0c2002e, i));

	i = slowIncrDecLUT_15272[rem / 28];
	EXIT_RC(WR16(s, 0x0c2002b, i));
	rc = WR16(s, 0x0c2002c, i);
exit_rc:
	return rc;
}

static int SetCfgRfAgc(struct drx397xD_state *s, struct drx397xD_CfgRfAgc *agc)
{
	u16 w04 = agc->w04;
	u16 w06 = agc->w06;
	int rc = -1;

	pr_debug("%s %d 0x%x 0x%x\n", __func__, agc->d00, w04, w06);

	if (w04 > 0x3ff)
		goto exit_rc;

	switch (agc->d00) {
	case 1:
		if (w04 == 0x3ff)
			w04 = 0x400;

		EXIT_RC(WR16(s, 0x0c20036, w04));
		s->config.w9C &= ~2;
		EXIT_RC(WR16(s, 0x0c20015, s->config.w9C));
		EXIT_RC(RD16(s, 0x0c20010));
		rc &= 0xbfdf;
		EXIT_RC(WR16(s, 0x0c20010, rc));
		EXIT_RC(RD16(s, 0x0c20013));
		rc &= ~2;
		break;
	case 0:
		/* loc_8000659 */
		s->config.w9C &= ~2;
		EXIT_RC(WR16(s, 0x0c20015, s->config.w9C));
		EXIT_RC(RD16(s, 0x0c20010));
		rc &= 0xbfdf;
		rc |= 0x4000;
		EXIT_RC(WR16(s, 0x0c20010, rc));
		EXIT_RC(WR16(s, 0x0c20051, (w06 >> 4) & 0x3f));
		EXIT_RC(RD16(s, 0x0c20013));
		rc &= ~2;
		break;
	default:
		s->config.w9C |= 2;
		EXIT_RC(WR16(s, 0x0c20015, s->config.w9C));
		EXIT_RC(RD16(s, 0x0c20010));
		rc &= 0xbfdf;
		EXIT_RC(WR16(s, 0x0c20010, rc));

		EXIT_RC(WR16(s, 0x0c20036, 0));

		EXIT_RC(RD16(s, 0x0c20013));
		rc |= 2;
	}
	rc = WR16(s, 0x0c20013, rc);

exit_rc:
	return rc;
}

static int GetLockStatus(struct drx397xD_state *s, int *lockstat)
{
	int rc;

	*lockstat = 0;

	rc = RD16(s, 0x082004b);
	if (rc < 0)
		return rc;

	if (s->config.d60 != 2)
		return 0;

	if ((rc & 7) == 7)
		*lockstat |= 1;
	if ((rc & 3) == 3)
		*lockstat |= 2;
	if (rc & 1)
		*lockstat |= 4;
	return 0;
}

static int CorrectSysClockDeviation(struct drx397xD_state *s)
{
	int rc = -EINVAL;
	int lockstat;
	u32 clk, clk_limit;

	pr_debug("%s\n", __func__);

	if (s->config.d5C == 0) {
		EXIT_RC(WR16(s, 0x08200e8, 0x010));
		EXIT_RC(WR16(s, 0x08200e9, 0x113));
		s->config.d5C = 1;
		return rc;
	}
	if (s->config.d5C != 1)
		goto exit_rc;

	rc = RD16(s, 0x0820048);

	rc = GetLockStatus(s, &lockstat);
	if (rc < 0)
		goto exit_rc;
	if ((lockstat & 1) == 0)
		goto exit_rc;

	EXIT_RC(WR16(s, 0x0420033, 0x200));
	EXIT_RC(WR16(s, 0x0420034, 0xc5));
	EXIT_RC(WR16(s, 0x0420035, 0x10));
	EXIT_RC(WR16(s, 0x0420036, 0x1));
	EXIT_RC(WR16(s, 0x0420037, 0xa));
	EXIT_RC(HI_Command(s, 6));
	EXIT_RC(RD16(s, 0x0420040));
	clk = rc;
	EXIT_RC(RD16(s, 0x0420041));
	clk |= rc << 16;

	if (clk <= 0x26ffff)
		goto exit_rc;
	if (clk > 0x610000)
		goto exit_rc;

	if (!s->bandwidth_parm)
		return -EINVAL;

	/* round & convert to Hz */
	clk = ((u64) (clk + 0x800000) * s->bandwidth_parm + (1 << 20)) >> 21;
	clk_limit = s->config.f_osc * MAX_CLOCK_DRIFT / 1000;

	if (clk - s->config.f_osc * 1000 + clk_limit <= 2 * clk_limit) {
		s->f_osc = clk;
		pr_debug("%s: osc %d %d [Hz]\n", __func__,
			 s->config.f_osc * 1000, clk - s->config.f_osc * 1000);
	}
	rc = WR16(s, 0x08200e8, 0);

exit_rc:
	return rc;
}

static int ConfigureMPEGOutput(struct drx397xD_state *s, int type)
{
	int rc, si, bp;

	pr_debug("%s\n", __func__);

	si = s->config.wA0;
	if (s->config.w98 == 0) {
		si |= 1;
		bp = 0;
	} else {
		si &= ~1;
		bp = 0x200;
	}
	if (s->config.w9A == 0)
		si |= 0x80;
	else
		si &= ~0x80;

	EXIT_RC(WR16(s, 0x2150045, 0));
	EXIT_RC(WR16(s, 0x2150010, si));
	EXIT_RC(WR16(s, 0x2150011, bp));
	rc = WR16(s, 0x2150012, (type == 0 ? 0xfff : 0));

exit_rc:
	return rc;
}

static int drx_tune(struct drx397xD_state *s,
		    struct dvb_frontend_parameters *fep)
{
	u16 v22 = 0;
	u16 v1C = 0;
	u16 v1A = 0;
	u16 v18 = 0;
	u32 edi = 0, ebx = 0, ebp = 0, edx = 0;
	u16 v20 = 0, v1E = 0, v16 = 0, v14 = 0, v12 = 0, v10 = 0, v0E = 0;

	int rc, df_tuner = 0;
	int a, b, c, d;
	pr_debug("%s %d\n", __func__, s->config.d60);

	if (s->config.d60 != 2)
		goto set_tuner;
	rc = CorrectSysClockDeviation(s);
	if (rc < 0)
		goto set_tuner;

	s->config.d60 = 1;
	rc = ConfigureMPEGOutput(s, 0);
	if (rc < 0)
		goto set_tuner;
set_tuner:

	rc = PLL_Set(s, fep, &df_tuner);
	if (rc < 0) {
		printk(KERN_ERR "Error in pll_set\n");
		goto exit_rc;
	}
	msleep(200);

	a = rc = RD16(s, 0x2150016);
	if (rc < 0)
		goto exit_rc;
	b = rc = RD16(s, 0x2150010);
	if (rc < 0)
		goto exit_rc;
	c = rc = RD16(s, 0x2150034);
	if (rc < 0)
		goto exit_rc;
	d = rc = RD16(s, 0x2150035);
	if (rc < 0)
		goto exit_rc;
	rc = WR16(s, 0x2150014, c);
	rc = WR16(s, 0x2150015, d);
	rc = WR16(s, 0x2150010, 0);
	rc = WR16(s, 0x2150000, 2);
	rc = WR16(s, 0x2150036, 0x0fff);
	rc = WR16(s, 0x2150016, a);

	rc = WR16(s, 0x2150010, 2);
	rc = WR16(s, 0x2150007, 0);
	rc = WR16(s, 0x2150000, 1);
	rc = WR16(s, 0x2110000, 0);
	rc = WR16(s, 0x0800000, 0);
	rc = WR16(s, 0x2800000, 0);
	rc = WR16(s, 0x2110010, 0x664);

	rc = write_fw(s, DRXD_ResetECRAM);
	rc = WR16(s, 0x2110000, 1);

	rc = write_fw(s, DRXD_InitSC);
	if (rc < 0)
		goto exit_rc;

	rc = SetCfgIfAgc(s, &s->config.ifagc);
	if (rc < 0)
		goto exit_rc;

	rc = SetCfgRfAgc(s, &s->config.rfagc);
	if (rc < 0)
		goto exit_rc;

	if (fep->u.ofdm.transmission_mode != TRANSMISSION_MODE_2K)
		v22 = 1;
	switch (fep->u.ofdm.transmission_mode) {
	case TRANSMISSION_MODE_8K:
		edi = 1;
		if (s->chip_rev == DRXD_FW_B1)
			break;

		rc = WR16(s, 0x2010010, 0);
		if (rc < 0)
			break;
		v1C = 0x63;
		v1A = 0x53;
		v18 = 0x43;
		break;
	default:
		edi = 0;
		if (s->chip_rev == DRXD_FW_B1)
			break;

		rc = WR16(s, 0x2010010, 1);
		if (rc < 0)
			break;

		v1C = 0x61;
		v1A = 0x47;
		v18 = 0x41;
	}

	switch (fep->u.ofdm.guard_interval) {
	case GUARD_INTERVAL_1_4:
		edi |= 0x0c;
		break;
	case GUARD_INTERVAL_1_8:
		edi |= 0x08;
		break;
	case GUARD_INTERVAL_1_16:
		edi |= 0x04;
		break;
	case GUARD_INTERVAL_1_32:
		break;
	default:
		v22 |= 2;
	}

	ebx = 0;
	ebp = 0;
	v20 = 0;
	v1E = 0;
	v16 = 0;
	v14 = 0;
	v12 = 0;
	v10 = 0;
	v0E = 0;

	switch (fep->u.ofdm.hierarchy_information) {
	case HIERARCHY_1:
		edi |= 0x40;
		if (s->chip_rev == DRXD_FW_B1)
			break;
		rc = WR16(s, 0x1c10047, 1);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x2010012, 1);
		if (rc < 0)
			goto exit_rc;
		ebx = 0x19f;
		ebp = 0x1fb;
		v20 = 0x0c0;
		v1E = 0x195;
		v16 = 0x1d6;
		v14 = 0x1ef;
		v12 = 4;
		v10 = 5;
		v0E = 5;
		break;
	case HIERARCHY_2:
		edi |= 0x80;
		if (s->chip_rev == DRXD_FW_B1)
			break;
		rc = WR16(s, 0x1c10047, 2);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x2010012, 2);
		if (rc < 0)
			goto exit_rc;
		ebx = 0x08f;
		ebp = 0x12f;
		v20 = 0x0c0;
		v1E = 0x11e;
		v16 = 0x1d6;
		v14 = 0x15e;
		v12 = 4;
		v10 = 5;
		v0E = 5;
		break;
	case HIERARCHY_4:
		edi |= 0xc0;
		if (s->chip_rev == DRXD_FW_B1)
			break;
		rc = WR16(s, 0x1c10047, 3);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x2010012, 3);
		if (rc < 0)
			goto exit_rc;
		ebx = 0x14d;
		ebp = 0x197;
		v20 = 0x0c0;
		v1E = 0x1ce;
		v16 = 0x1d6;
		v14 = 0x11a;
		v12 = 4;
		v10 = 6;
		v0E = 5;
		break;
	default:
		v22 |= 8;
		if (s->chip_rev == DRXD_FW_B1)
			break;
		rc = WR16(s, 0x1c10047, 0);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x2010012, 0);
		if (rc < 0)
			goto exit_rc;
				/* QPSK    QAM16  QAM64	*/
		ebx = 0x19f;	/*                 62	*/
		ebp = 0x1fb;	/*                 15	*/
		v20 = 0x16a;	/*  62			*/
		v1E = 0x195;	/*         62		*/
		v16 = 0x1bb;	/*  15			*/
		v14 = 0x1ef;	/*         15		*/
		v12 = 5;	/*  16			*/
		v10 = 5;	/*         16		*/
		v0E = 5;	/*                 16	*/
	}

	switch (fep->u.ofdm.constellation) {
	default:
		v22 |= 4;
	case QPSK:
		if (s->chip_rev == DRXD_FW_B1)
			break;

		rc = WR16(s, 0x1c10046, 0);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x2010011, 0);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x201001a, 0x10);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x201001b, 0);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x201001c, 0);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x1c10062, v20);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x1c1002a, v1C);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x1c10015, v16);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x1c10016, v12);
		if (rc < 0)
			goto exit_rc;
		break;
	case QAM_16:
		edi |= 0x10;
		if (s->chip_rev == DRXD_FW_B1)
			break;

		rc = WR16(s, 0x1c10046, 1);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x2010011, 1);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x201001a, 0x10);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x201001b, 4);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x201001c, 0);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x1c10062, v1E);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x1c1002a, v1A);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x1c10015, v14);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x1c10016, v10);
		if (rc < 0)
			goto exit_rc;
		break;
	case QAM_64:
		edi |= 0x20;
		rc = WR16(s, 0x1c10046, 2);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x2010011, 2);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x201001a, 0x20);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x201001b, 8);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x201001c, 2);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x1c10062, ebx);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x1c1002a, v18);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x1c10015, ebp);
		if (rc < 0)
			goto exit_rc;
		rc = WR16(s, 0x1c10016, v0E);
		if (rc < 0)
			goto exit_rc;
		break;
	}

	if (s->config.s20d24 == 1) {
		rc = WR16(s, 0x2010013, 0);
	} else {
		rc = WR16(s, 0x2010013, 1);
		edi |= 0x1000;
	}

	switch (fep->u.ofdm.code_rate_HP) {
	default:
		v22 |= 0x10;
	case FEC_1_2:
		if (s->chip_rev == DRXD_FW_B1)
			break;
		rc = WR16(s, 0x2090011, 0);
		break;
	case FEC_2_3:
		edi |= 0x200;
		if (s->chip_rev == DRXD_FW_B1)
			break;
		rc = WR16(s, 0x2090011, 1);
		break;
	case FEC_3_4:
		edi |= 0x400;
		if (s->chip_rev == DRXD_FW_B1)
			break;
		rc = WR16(s, 0x2090011, 2);
		break;
	case FEC_5_6:		/* 5 */
		edi |= 0x600;
		if (s->chip_rev == DRXD_FW_B1)
			break;
		rc = WR16(s, 0x2090011, 3);
		break;
	case FEC_7_8:		/* 7 */
		edi |= 0x800;
		if (s->chip_rev == DRXD_FW_B1)
			break;
		rc = WR16(s, 0x2090011, 4);
		break;
	};
	if (rc < 0)
		goto exit_rc;

	switch (fep->u.ofdm.bandwidth) {
	default:
		rc = -EINVAL;
		goto exit_rc;
	case BANDWIDTH_8_MHZ:	/* 0 */
	case BANDWIDTH_AUTO:
		rc = WR16(s, 0x0c2003f, 0x32);
		s->bandwidth_parm = ebx = 0x8b8249;
		edx = 0;
		break;
	case BANDWIDTH_7_MHZ:
		rc = WR16(s, 0x0c2003f, 0x3b);
		s->bandwidth_parm = ebx = 0x7a1200;
		edx = 0x4807;
		break;
	case BANDWIDTH_6_MHZ:
		rc = WR16(s, 0x0c2003f, 0x47);
		s->bandwidth_parm = ebx = 0x68a1b6;
		edx = 0x0f07;
		break;
	};

	if (rc < 0)
		goto exit_rc;

	rc = WR16(s, 0x08200ec, edx);
	if (rc < 0)
		goto exit_rc;

	rc = RD16(s, 0x0820050);
	if (rc < 0)
		goto exit_rc;
	rc = WR16(s, 0x0820050, rc);

	{
		/* Configure bandwidth specific factor */
		ebx = div64_u64(((u64) (s->f_osc) << 21) + (ebx >> 1),
				     (u64)ebx) - 0x800000;
		EXIT_RC(WR16(s, 0x0c50010, ebx & 0xffff));
		EXIT_RC(WR16(s, 0x0c50011, ebx >> 16));

		/* drx397xD oscillator calibration */
		ebx = div64_u64(((u64) (s->config.f_if + df_tuner) << 28) +
				     (s->f_osc >> 1), (u64)s->f_osc);
	}
	ebx &= 0xfffffff;
	if (fep->inversion == INVERSION_ON)
		ebx = 0x10000000 - ebx;

	EXIT_RC(WR16(s, 0x0c30010, ebx & 0xffff));
	EXIT_RC(WR16(s, 0x0c30011, ebx >> 16));

	EXIT_RC(WR16(s, 0x0800000, 1));
	EXIT_RC(RD16(s, 0x0800000));


	EXIT_RC(SC_WaitForReady(s));
	EXIT_RC(WR16(s, 0x0820042, 0));
	EXIT_RC(WR16(s, 0x0820041, v22));
	EXIT_RC(WR16(s, 0x0820040, edi));
	EXIT_RC(SC_SendCommand(s, 3));

	rc = RD16(s, 0x0800000);

	SC_WaitForReady(s);
	WR16(s, 0x0820042, 0);
	WR16(s, 0x0820041, 1);
	WR16(s, 0x0820040, 1);
	SC_SendCommand(s, 1);


	rc = WR16(s, 0x2150000, 2);
	rc = WR16(s, 0x2150016, a);
	rc = WR16(s, 0x2150010, 4);
	rc = WR16(s, 0x2150036, 0);
	rc = WR16(s, 0x2150000, 1);
	s->config.d60 = 2;

exit_rc:
	return rc;
}

/*******************************************************************************
 * DVB interface
 ******************************************************************************/

static int drx397x_init(struct dvb_frontend *fe)
{
	struct drx397xD_state *s = fe->demodulator_priv;
	int rc;

	pr_debug("%s\n", __func__);

	s->config.rfagc.d00 = 2;	/* 0x7c */
	s->config.rfagc.w04 = 0;
	s->config.rfagc.w06 = 0x3ff;

	s->config.ifagc.d00 = 0;	/* 0x68 */
	s->config.ifagc.w04 = 0;
	s->config.ifagc.w06 = 140;
	s->config.ifagc.w08 = 0;
	s->config.ifagc.w0A = 0x3ff;
	s->config.ifagc.w0C = 0x388;

	/* for signal strenght calculations */
	s->config.ss76 = 820;
	s->config.ss78 = 2200;
	s->config.ss7A = 150;

	/* HI_CfgCommand */
	s->config.w50 = 4;
	s->config.w52 = 9;

	s->config.f_if = 42800000;	/* d14: intermediate frequency [Hz] */
	s->config.f_osc = 48000;	/* s66 : oscillator frequency [kHz] */
	s->config.w92 = 12000;

	s->config.w9C = 0x000e;
	s->config.w9E = 0x0000;

	/* ConfigureMPEGOutput params */
	s->config.wA0 = 4;
	s->config.w98 = 1;
	s->config.w9A = 1;

	/* get chip revision */
	rc = RD16(s, 0x2410019);
	if (rc < 0)
		return -ENODEV;

	if (rc == 0) {
		printk(KERN_INFO "%s: chip revision A2\n", mod_name);
		rc = drx_load_fw(s, DRXD_FW_A2);
	} else {

		rc = (rc >> 12) - 3;
		switch (rc) {
		case 1:
			s->flags |= F_SET_0D4h;
		case 0:
		case 4:
			s->flags |= F_SET_0D0h;
			break;
		case 2:
		case 5:
			break;
		case 3:
			s->flags |= F_SET_0D4h;
			break;
		default:
			return -ENODEV;
		};
		printk(KERN_INFO "%s: chip revision B1.%d\n", mod_name, rc);
		rc = drx_load_fw(s, DRXD_FW_B1);
	}
	if (rc < 0)
		goto error;

	rc = WR16(s, 0x0420033, 0x3973);
	if (rc < 0)
		goto error;

	rc = HI_Command(s, 2);

	msleep(1);

	if (s->chip_rev == DRXD_FW_A2) {
		rc = WR16(s, 0x043012d, 0x47F);
		if (rc < 0)
			goto error;
	}
	rc = WR16_E0(s, 0x0400000, 0);
	if (rc < 0)
		goto error;

	if (s->config.w92 > 20000 || s->config.w92 % 4000) {
		printk(KERN_ERR "%s: invalid osc frequency\n", mod_name);
		rc = -1;
		goto error;
	}

	rc = WR16(s, 0x2410010, 1);
	if (rc < 0)
		goto error;
	rc = WR16(s, 0x2410011, 0x15);
	if (rc < 0)
		goto error;
	rc = WR16(s, 0x2410012, s->config.w92 / 4000);
	if (rc < 0)
		goto error;
#ifdef ORIG_FW
	rc = WR16(s, 0x2410015, 2);
	if (rc < 0)
		goto error;
#endif
	rc = WR16(s, 0x2410017, 0x3973);
	if (rc < 0)
		goto error;

	s->f_osc = s->config.f_osc * 1000;	/* initial estimator */

	s->config.w56 = 1;

	rc = HI_CfgCommand(s);
	if (rc < 0)
		goto error;

	rc = write_fw(s, DRXD_InitAtomicRead);
	if (rc < 0)
		goto error;

	if (s->chip_rev == DRXD_FW_A2) {
		rc = WR16(s, 0x2150013, 0);
		if (rc < 0)
			goto error;
	}

	rc = WR16_E0(s, 0x0400002, 0);
	if (rc < 0)
		goto error;
	rc = WR16(s, 0x0400002, 0);
	if (rc < 0)
		goto error;

	if (s->chip_rev == DRXD_FW_A2) {
		rc = write_fw(s, DRXD_ResetCEFR);
		if (rc < 0)
			goto error;
	}
	rc = write_fw(s, DRXD_microcode);
	if (rc < 0)
		goto error;

	s->config.w9C = 0x0e;
	if (s->flags & F_SET_0D0h) {
		s->config.w9C = 0;
		rc = RD16(s, 0x0c20010);
		if (rc < 0)
			goto write_DRXD_InitFE_1;

		rc &= ~0x1000;
		rc = WR16(s, 0x0c20010, rc);
		if (rc < 0)
			goto write_DRXD_InitFE_1;

		rc = RD16(s, 0x0c20011);
		if (rc < 0)
			goto write_DRXD_InitFE_1;

		rc &= ~0x8;
		rc = WR16(s, 0x0c20011, rc);
		if (rc < 0)
			goto write_DRXD_InitFE_1;

		rc = WR16(s, 0x0c20012, 1);
	}

write_DRXD_InitFE_1:

	rc = write_fw(s, DRXD_InitFE_1);
	if (rc < 0)
		goto error;

	rc = 1;
	if (s->chip_rev == DRXD_FW_B1) {
		if (s->flags & F_SET_0D0h)
			rc = 0;
	} else {
		if (s->flags & F_SET_0D0h)
			rc = 4;
	}

	rc = WR16(s, 0x0C20012, rc);
	if (rc < 0)
		goto error;

	rc = WR16(s, 0x0C20013, s->config.w9E);
	if (rc < 0)
		goto error;
	rc = WR16(s, 0x0C20015, s->config.w9C);
	if (rc < 0)
		goto error;

	rc = write_fw(s, DRXD_InitFE_2);
	if (rc < 0)
		goto error;
	rc = write_fw(s, DRXD_InitFT);
	if (rc < 0)
		goto error;
	rc = write_fw(s, DRXD_InitCP);
	if (rc < 0)
		goto error;
	rc = write_fw(s, DRXD_InitCE);
	if (rc < 0)
		goto error;
	rc = write_fw(s, DRXD_InitEQ);
	if (rc < 0)
		goto error;
	rc = write_fw(s, DRXD_InitEC);
	if (rc < 0)
		goto error;
	rc = write_fw(s, DRXD_InitSC);
	if (rc < 0)
		goto error;

	rc = SetCfgIfAgc(s, &s->config.ifagc);
	if (rc < 0)
		goto error;

	rc = SetCfgRfAgc(s, &s->config.rfagc);
	if (rc < 0)
		goto error;

	rc = ConfigureMPEGOutput(s, 1);
	rc = WR16(s, 0x08201fe, 0x0017);
	rc = WR16(s, 0x08201ff, 0x0101);

	s->config.d5C = 0;
	s->config.d60 = 1;
	s->config.d48 = 1;

error:
	return rc;
}

static int drx397x_get_frontend(struct dvb_frontend *fe,
				struct dvb_frontend_parameters *params)
{
	return 0;
}

static int drx397x_set_frontend(struct dvb_frontend *fe,
				struct dvb_frontend_parameters *params)
{
	struct drx397xD_state *s = fe->demodulator_priv;

	s->config.s20d24 = 1;

	return drx_tune(s, params);
}

static int drx397x_get_tune_settings(struct dvb_frontend *fe,
				     struct dvb_frontend_tune_settings
				     *fe_tune_settings)
{
	fe_tune_settings->min_delay_ms = 10000;
	fe_tune_settings->step_size = 0;
	fe_tune_settings->max_drift = 0;

	return 0;
}

static int drx397x_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct drx397xD_state *s = fe->demodulator_priv;
	int lockstat;

	GetLockStatus(s, &lockstat);

	*status = 0;
	if (lockstat & 2) {
		CorrectSysClockDeviation(s);
		ConfigureMPEGOutput(s, 1);
		*status = FE_HAS_LOCK | FE_HAS_SYNC | FE_HAS_VITERBI;
	}
	if (lockstat & 4)
		*status |= FE_HAS_CARRIER | FE_HAS_SIGNAL;

	return 0;
}

static int drx397x_read_ber(struct dvb_frontend *fe, unsigned int *ber)
{
	*ber = 0;

	return 0;
}

static int drx397x_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	*snr = 0;

	return 0;
}

static int drx397x_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct drx397xD_state *s = fe->demodulator_priv;
	int rc;

	if (s->config.ifagc.d00 == 2) {
		*strength = 0xffff;
		return 0;
	}
	rc = RD16(s, 0x0c20035);
	if (rc < 0) {
		*strength = 0;
		return 0;
	}
	rc &= 0x3ff;
	/* Signal strength is calculated using the following formula:
	 *
	 * a = 2200 * 150 / (2200 + 150);
	 * a = a * 3300 /  (a + 820);
	 * b = 2200 * 3300 / (2200 + 820);
	 * c = (((b-a) * rc) >> 10  + a) << 4;
	 * strength = ~c & 0xffff;
	 *
	 * The following does the same but with less rounding errors:
	 */
	*strength = ~(7720 + (rc * 30744 >> 10));

	return 0;
}

static int drx397x_read_ucblocks(struct dvb_frontend *fe,
				 unsigned int *ucblocks)
{
	*ucblocks = 0;

	return 0;
}

static int drx397x_sleep(struct dvb_frontend *fe)
{
	return 0;
}

static void drx397x_release(struct dvb_frontend *fe)
{
	struct drx397xD_state *s = fe->demodulator_priv;
	printk(KERN_INFO "%s: release demodulator\n", mod_name);
	if (s) {
		drx_release_fw(s);
		kfree(s);
	}

}

static struct dvb_frontend_ops drx397x_ops = {

	.info = {
		 .name			= "Micronas DRX397xD DVB-T Frontend",
		 .type			= FE_OFDM,
		 .frequency_min		= 47125000,
		 .frequency_max		= 855250000,
		 .frequency_stepsize	= 166667,
		 .frequency_tolerance	= 0,
		 .caps =				  /* 0x0C01B2EAE */
			 FE_CAN_FEC_1_2			| /* = 0x2, */
			 FE_CAN_FEC_2_3			| /* = 0x4, */
			 FE_CAN_FEC_3_4			| /* = 0x8, */
			 FE_CAN_FEC_5_6			| /* = 0x20, */
			 FE_CAN_FEC_7_8			| /* = 0x80, */
			 FE_CAN_FEC_AUTO		| /* = 0x200, */
			 FE_CAN_QPSK			| /* = 0x400, */
			 FE_CAN_QAM_16			| /* = 0x800, */
			 FE_CAN_QAM_64			| /* = 0x2000, */
			 FE_CAN_QAM_AUTO		| /* = 0x10000, */
			 FE_CAN_TRANSMISSION_MODE_AUTO	| /* = 0x20000, */
			 FE_CAN_GUARD_INTERVAL_AUTO	| /* = 0x80000, */
			 FE_CAN_HIERARCHY_AUTO		| /* = 0x100000, */
			 FE_CAN_RECOVER			| /* = 0x40000000, */
			 FE_CAN_MUTE_TS			  /* = 0x80000000 */
	 },

	.release = drx397x_release,
	.init = drx397x_init,
	.sleep = drx397x_sleep,

	.set_frontend = drx397x_set_frontend,
	.get_tune_settings = drx397x_get_tune_settings,
	.get_frontend = drx397x_get_frontend,

	.read_status = drx397x_read_status,
	.read_snr = drx397x_read_snr,
	.read_signal_strength = drx397x_read_signal_strength,
	.read_ber = drx397x_read_ber,
	.read_ucblocks = drx397x_read_ucblocks,
};

struct dvb_frontend *drx397xD_attach(const struct drx397xD_config *config,
				     struct i2c_adapter *i2c)
{
	struct drx397xD_state *state;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct drx397xD_state), GFP_KERNEL);
	if (!state)
		goto error;

	/* setup the state */
	state->i2c = i2c;
	memcpy(&state->config, config, sizeof(struct drx397xD_config));

	/* check if the demod is there */
	if (RD16(state, 0x2410019) < 0)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &drx397x_ops,
			sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	return &state->frontend;
error:
	kfree(state);

	return NULL;
}
EXPORT_SYMBOL(drx397xD_attach);

MODULE_DESCRIPTION("Micronas DRX397xD DVB-T Frontend");
MODULE_AUTHOR("Henk Vergonet");
MODULE_LICENSE("GPL");

