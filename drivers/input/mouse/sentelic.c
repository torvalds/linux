/*-
 * Finger Sensing Pad PS/2 mouse driver.
 *
 * Copyright (C) 2005-2007 Asia Vital Components Co., Ltd.
 * Copyright (C) 2005-2012 Tai-hwa Liang, Sentelic Corporation.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2
 *   of the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/ctype.h>
#include <linux/libps2.h>
#include <linux/serio.h>
#include <linux/jiffies.h>
#include <linux/slab.h>

#include "psmouse.h"
#include "sentelic.h"

/*
 * Timeout for FSP PS/2 command only (in milliseconds).
 */
#define	FSP_CMD_TIMEOUT		200
#define	FSP_CMD_TIMEOUT2	30

#define	GET_ABS_X(packet)	((packet[1] << 2) | ((packet[3] >> 2) & 0x03))
#define	GET_ABS_Y(packet)	((packet[2] << 2) | (packet[3] & 0x03))

/** Driver version. */
static const char fsp_drv_ver[] = "1.1.0-K";

/*
 * Make sure that the value being sent to FSP will not conflict with
 * possible sample rate values.
 */
static unsigned char fsp_test_swap_cmd(unsigned char reg_val)
{
	switch (reg_val) {
	case 10: case 20: case 40: case 60: case 80: case 100: case 200:
		/*
		 * The requested value being sent to FSP matched to possible
		 * sample rates, swap the given value such that the hardware
		 * wouldn't get confused.
		 */
		return (reg_val >> 4) | (reg_val << 4);
	default:
		return reg_val;	/* swap isn't necessary */
	}
}

/*
 * Make sure that the value being sent to FSP will not conflict with certain
 * commands.
 */
static unsigned char fsp_test_invert_cmd(unsigned char reg_val)
{
	switch (reg_val) {
	case 0xe9: case 0xee: case 0xf2: case 0xff:
		/*
		 * The requested value being sent to FSP matched to certain
		 * commands, inverse the given value such that the hardware
		 * wouldn't get confused.
		 */
		return ~reg_val;
	default:
		return reg_val;	/* inversion isn't necessary */
	}
}

static int fsp_reg_read(struct psmouse *psmouse, int reg_addr, int *reg_val)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[3];
	unsigned char addr;
	int rc = -1;

	/*
	 * We need to shut off the device and switch it into command
	 * mode so we don't confuse our protocol handler. We don't need
	 * to do that for writes because sysfs set helper does this for
	 * us.
	 */
	psmouse_deactivate(psmouse);

	ps2_begin_command(ps2dev);

	if (ps2_sendbyte(ps2dev, 0xf3, FSP_CMD_TIMEOUT) < 0)
		goto out;

	/* should return 0xfe(request for resending) */
	ps2_sendbyte(ps2dev, 0x66, FSP_CMD_TIMEOUT2);
	/* should return 0xfc(failed) */
	ps2_sendbyte(ps2dev, 0x88, FSP_CMD_TIMEOUT2);

	if (ps2_sendbyte(ps2dev, 0xf3, FSP_CMD_TIMEOUT) < 0)
		goto out;

	if ((addr = fsp_test_invert_cmd(reg_addr)) != reg_addr) {
		ps2_sendbyte(ps2dev, 0x68, FSP_CMD_TIMEOUT2);
	} else if ((addr = fsp_test_swap_cmd(reg_addr)) != reg_addr) {
		/* swapping is required */
		ps2_sendbyte(ps2dev, 0xcc, FSP_CMD_TIMEOUT2);
		/* expect 0xfe */
	} else {
		/* swapping isn't necessary */
		ps2_sendbyte(ps2dev, 0x66, FSP_CMD_TIMEOUT2);
		/* expect 0xfe */
	}
	/* should return 0xfc(failed) */
	ps2_sendbyte(ps2dev, addr, FSP_CMD_TIMEOUT);

	if (__ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO) < 0)
		goto out;

	*reg_val = param[2];
	rc = 0;

 out:
	ps2_end_command(ps2dev);
	psmouse_activate(psmouse);
	psmouse_dbg(psmouse,
		    "READ REG: 0x%02x is 0x%02x (rc = %d)\n",
		    reg_addr, *reg_val, rc);
	return rc;
}

static int fsp_reg_write(struct psmouse *psmouse, int reg_addr, int reg_val)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char v;
	int rc = -1;

	ps2_begin_command(ps2dev);

	if (ps2_sendbyte(ps2dev, 0xf3, FSP_CMD_TIMEOUT) < 0)
		goto out;

	if ((v = fsp_test_invert_cmd(reg_addr)) != reg_addr) {
		/* inversion is required */
		ps2_sendbyte(ps2dev, 0x74, FSP_CMD_TIMEOUT2);
	} else {
		if ((v = fsp_test_swap_cmd(reg_addr)) != reg_addr) {
			/* swapping is required */
			ps2_sendbyte(ps2dev, 0x77, FSP_CMD_TIMEOUT2);
		} else {
			/* swapping isn't necessary */
			ps2_sendbyte(ps2dev, 0x55, FSP_CMD_TIMEOUT2);
		}
	}
	/* write the register address in correct order */
	ps2_sendbyte(ps2dev, v, FSP_CMD_TIMEOUT2);

	if (ps2_sendbyte(ps2dev, 0xf3, FSP_CMD_TIMEOUT) < 0)
		goto out;

	if ((v = fsp_test_invert_cmd(reg_val)) != reg_val) {
		/* inversion is required */
		ps2_sendbyte(ps2dev, 0x47, FSP_CMD_TIMEOUT2);
	} else if ((v = fsp_test_swap_cmd(reg_val)) != reg_val) {
		/* swapping is required */
		ps2_sendbyte(ps2dev, 0x44, FSP_CMD_TIMEOUT2);
	} else {
		/* swapping isn't necessary */
		ps2_sendbyte(ps2dev, 0x33, FSP_CMD_TIMEOUT2);
	}

	/* write the register value in correct order */
	ps2_sendbyte(ps2dev, v, FSP_CMD_TIMEOUT2);
	rc = 0;

 out:
	ps2_end_command(ps2dev);
	psmouse_dbg(psmouse,
		    "WRITE REG: 0x%02x to 0x%02x (rc = %d)\n",
		    reg_addr, reg_val, rc);
	return rc;
}

/* Enable register clock gating for writing certain registers */
static int fsp_reg_write_enable(struct psmouse *psmouse, bool enable)
{
	int v, nv;

	if (fsp_reg_read(psmouse, FSP_REG_SYSCTL1, &v) == -1)
		return -1;

	if (enable)
		nv = v | FSP_BIT_EN_REG_CLK;
	else
		nv = v & ~FSP_BIT_EN_REG_CLK;

	/* only write if necessary */
	if (nv != v)
		if (fsp_reg_write(psmouse, FSP_REG_SYSCTL1, nv) == -1)
			return -1;

	return 0;
}

static int fsp_page_reg_read(struct psmouse *psmouse, int *reg_val)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[3];
	int rc = -1;

	psmouse_deactivate(psmouse);

	ps2_begin_command(ps2dev);

	if (ps2_sendbyte(ps2dev, 0xf3, FSP_CMD_TIMEOUT) < 0)
		goto out;

	ps2_sendbyte(ps2dev, 0x66, FSP_CMD_TIMEOUT2);
	ps2_sendbyte(ps2dev, 0x88, FSP_CMD_TIMEOUT2);

	if (ps2_sendbyte(ps2dev, 0xf3, FSP_CMD_TIMEOUT) < 0)
		goto out;

	ps2_sendbyte(ps2dev, 0x83, FSP_CMD_TIMEOUT2);
	ps2_sendbyte(ps2dev, 0x88, FSP_CMD_TIMEOUT2);

	/* get the returned result */
	if (__ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		goto out;

	*reg_val = param[2];
	rc = 0;

 out:
	ps2_end_command(ps2dev);
	psmouse_activate(psmouse);
	psmouse_dbg(psmouse,
		    "READ PAGE REG: 0x%02x (rc = %d)\n",
		    *reg_val, rc);
	return rc;
}

static int fsp_page_reg_write(struct psmouse *psmouse, int reg_val)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char v;
	int rc = -1;

	ps2_begin_command(ps2dev);

	if (ps2_sendbyte(ps2dev, 0xf3, FSP_CMD_TIMEOUT) < 0)
		goto out;

	ps2_sendbyte(ps2dev, 0x38, FSP_CMD_TIMEOUT2);
	ps2_sendbyte(ps2dev, 0x88, FSP_CMD_TIMEOUT2);

	if (ps2_sendbyte(ps2dev, 0xf3, FSP_CMD_TIMEOUT) < 0)
		goto out;

	if ((v = fsp_test_invert_cmd(reg_val)) != reg_val) {
		ps2_sendbyte(ps2dev, 0x47, FSP_CMD_TIMEOUT2);
	} else if ((v = fsp_test_swap_cmd(reg_val)) != reg_val) {
		/* swapping is required */
		ps2_sendbyte(ps2dev, 0x44, FSP_CMD_TIMEOUT2);
	} else {
		/* swapping isn't necessary */
		ps2_sendbyte(ps2dev, 0x33, FSP_CMD_TIMEOUT2);
	}

	ps2_sendbyte(ps2dev, v, FSP_CMD_TIMEOUT2);
	rc = 0;

 out:
	ps2_end_command(ps2dev);
	psmouse_dbg(psmouse,
		    "WRITE PAGE REG: to 0x%02x (rc = %d)\n",
		    reg_val, rc);
	return rc;
}

static int fsp_get_version(struct psmouse *psmouse, int *version)
{
	if (fsp_reg_read(psmouse, FSP_REG_VERSION, version))
		return -EIO;

	return 0;
}

static int fsp_get_revision(struct psmouse *psmouse, int *rev)
{
	if (fsp_reg_read(psmouse, FSP_REG_REVISION, rev))
		return -EIO;

	return 0;
}

static int fsp_get_sn(struct psmouse *psmouse, int *sn)
{
	int v0, v1, v2;
	int rc = -EIO;

	/* production number since Cx is available at: 0x0b40 ~ 0x0b42 */
	if (fsp_page_reg_write(psmouse, FSP_PAGE_0B))
		goto out;
	if (fsp_reg_read(psmouse, FSP_REG_SN0, &v0))
		goto out;
	if (fsp_reg_read(psmouse, FSP_REG_SN1, &v1))
		goto out;
	if (fsp_reg_read(psmouse, FSP_REG_SN2, &v2))
		goto out;
	*sn = (v0 << 16) | (v1 << 8) | v2;
	rc = 0;
out:
	fsp_page_reg_write(psmouse, FSP_PAGE_DEFAULT);
	return rc;
}

static int fsp_get_buttons(struct psmouse *psmouse, int *btn)
{
	static const int buttons[] = {
		0x16, /* Left/Middle/Right/Forward/Backward & Scroll Up/Down */
		0x06, /* Left/Middle/Right & Scroll Up/Down/Right/Left */
		0x04, /* Left/Middle/Right & Scroll Up/Down */
		0x02, /* Left/Middle/Right */
	};
	int val;

	if (fsp_reg_read(psmouse, FSP_REG_TMOD_STATUS, &val) == -1)
		return -EIO;

	*btn = buttons[(val & 0x30) >> 4];
	return 0;
}

/* Enable on-pad command tag output */
static int fsp_opc_tag_enable(struct psmouse *psmouse, bool enable)
{
	int v, nv;
	int res = 0;

	if (fsp_reg_read(psmouse, FSP_REG_OPC_QDOWN, &v) == -1) {
		psmouse_err(psmouse, "Unable get OPC state.\n");
		return -EIO;
	}

	if (enable)
		nv = v | FSP_BIT_EN_OPC_TAG;
	else
		nv = v & ~FSP_BIT_EN_OPC_TAG;

	/* only write if necessary */
	if (nv != v) {
		fsp_reg_write_enable(psmouse, true);
		res = fsp_reg_write(psmouse, FSP_REG_OPC_QDOWN, nv);
		fsp_reg_write_enable(psmouse, false);
	}

	if (res != 0) {
		psmouse_err(psmouse, "Unable to enable OPC tag.\n");
		res = -EIO;
	}

	return res;
}

static int fsp_onpad_vscr(struct psmouse *psmouse, bool enable)
{
	struct fsp_data *pad = psmouse->private;
	int val;

	if (fsp_reg_read(psmouse, FSP_REG_ONPAD_CTL, &val))
		return -EIO;

	pad->vscroll = enable;

	if (enable)
		val |= (FSP_BIT_FIX_VSCR | FSP_BIT_ONPAD_ENABLE);
	else
		val &= ~FSP_BIT_FIX_VSCR;

	if (fsp_reg_write(psmouse, FSP_REG_ONPAD_CTL, val))
		return -EIO;

	return 0;
}

static int fsp_onpad_hscr(struct psmouse *psmouse, bool enable)
{
	struct fsp_data *pad = psmouse->private;
	int val, v2;

	if (fsp_reg_read(psmouse, FSP_REG_ONPAD_CTL, &val))
		return -EIO;

	if (fsp_reg_read(psmouse, FSP_REG_SYSCTL5, &v2))
		return -EIO;

	pad->hscroll = enable;

	if (enable) {
		val |= (FSP_BIT_FIX_HSCR | FSP_BIT_ONPAD_ENABLE);
		v2 |= FSP_BIT_EN_MSID6;
	} else {
		val &= ~FSP_BIT_FIX_HSCR;
		v2 &= ~(FSP_BIT_EN_MSID6 | FSP_BIT_EN_MSID7 | FSP_BIT_EN_MSID8);
	}

	if (fsp_reg_write(psmouse, FSP_REG_ONPAD_CTL, val))
		return -EIO;

	/* reconfigure horizontal scrolling packet output */
	if (fsp_reg_write(psmouse, FSP_REG_SYSCTL5, v2))
		return -EIO;

	return 0;
}

/*
 * Write device specific initial parameters.
 *
 * ex: 0xab 0xcd - write oxcd into register 0xab
 */
static ssize_t fsp_attr_set_setreg(struct psmouse *psmouse, void *data,
				   const char *buf, size_t count)
{
	int reg, val;
	char *rest;
	ssize_t retval;

	reg = simple_strtoul(buf, &rest, 16);
	if (rest == buf || *rest != ' ' || reg > 0xff)
		return -EINVAL;

	retval = kstrtoint(rest + 1, 16, &val);
	if (retval)
		return retval;

	if (val > 0xff)
		return -EINVAL;

	if (fsp_reg_write_enable(psmouse, true))
		return -EIO;

	retval = fsp_reg_write(psmouse, reg, val) < 0 ? -EIO : count;

	fsp_reg_write_enable(psmouse, false);

	return count;
}

PSMOUSE_DEFINE_WO_ATTR(setreg, S_IWUSR, NULL, fsp_attr_set_setreg);

static ssize_t fsp_attr_show_getreg(struct psmouse *psmouse,
					void *data, char *buf)
{
	struct fsp_data *pad = psmouse->private;

	return sprintf(buf, "%02x%02x\n", pad->last_reg, pad->last_val);
}

/*
 * Read a register from device.
 *
 * ex: 0xab -- read content from register 0xab
 */
static ssize_t fsp_attr_set_getreg(struct psmouse *psmouse, void *data,
					const char *buf, size_t count)
{
	struct fsp_data *pad = psmouse->private;
	int reg, val, err;

	err = kstrtoint(buf, 16, &reg);
	if (err)
		return err;

	if (reg > 0xff)
		return -EINVAL;

	if (fsp_reg_read(psmouse, reg, &val))
		return -EIO;

	pad->last_reg = reg;
	pad->last_val = val;

	return count;
}

PSMOUSE_DEFINE_ATTR(getreg, S_IWUSR | S_IRUGO, NULL,
			fsp_attr_show_getreg, fsp_attr_set_getreg);

static ssize_t fsp_attr_show_pagereg(struct psmouse *psmouse,
					void *data, char *buf)
{
	int val = 0;

	if (fsp_page_reg_read(psmouse, &val))
		return -EIO;

	return sprintf(buf, "%02x\n", val);
}

static ssize_t fsp_attr_set_pagereg(struct psmouse *psmouse, void *data,
					const char *buf, size_t count)
{
	int val, err;

	err = kstrtoint(buf, 16, &val);
	if (err)
		return err;

	if (val > 0xff)
		return -EINVAL;

	if (fsp_page_reg_write(psmouse, val))
		return -EIO;

	return count;
}

PSMOUSE_DEFINE_ATTR(page, S_IWUSR | S_IRUGO, NULL,
			fsp_attr_show_pagereg, fsp_attr_set_pagereg);

static ssize_t fsp_attr_show_vscroll(struct psmouse *psmouse,
					void *data, char *buf)
{
	struct fsp_data *pad = psmouse->private;

	return sprintf(buf, "%d\n", pad->vscroll);
}

static ssize_t fsp_attr_set_vscroll(struct psmouse *psmouse, void *data,
					const char *buf, size_t count)
{
	unsigned int val;
	int err;

	err = kstrtouint(buf, 10, &val);
	if (err)
		return err;

	if (val > 1)
		return -EINVAL;

	fsp_onpad_vscr(psmouse, val);

	return count;
}

PSMOUSE_DEFINE_ATTR(vscroll, S_IWUSR | S_IRUGO, NULL,
			fsp_attr_show_vscroll, fsp_attr_set_vscroll);

static ssize_t fsp_attr_show_hscroll(struct psmouse *psmouse,
					void *data, char *buf)
{
	struct fsp_data *pad = psmouse->private;

	return sprintf(buf, "%d\n", pad->hscroll);
}

static ssize_t fsp_attr_set_hscroll(struct psmouse *psmouse, void *data,
					const char *buf, size_t count)
{
	unsigned int val;
	int err;

	err = kstrtouint(buf, 10, &val);
	if (err)
		return err;

	if (val > 1)
		return -EINVAL;

	fsp_onpad_hscr(psmouse, val);

	return count;
}

PSMOUSE_DEFINE_ATTR(hscroll, S_IWUSR | S_IRUGO, NULL,
			fsp_attr_show_hscroll, fsp_attr_set_hscroll);

static ssize_t fsp_attr_show_flags(struct psmouse *psmouse,
					void *data, char *buf)
{
	struct fsp_data *pad = psmouse->private;

	return sprintf(buf, "%c\n",
			pad->flags & FSPDRV_FLAG_EN_OPC ? 'C' : 'c');
}

static ssize_t fsp_attr_set_flags(struct psmouse *psmouse, void *data,
					const char *buf, size_t count)
{
	struct fsp_data *pad = psmouse->private;
	size_t i;

	for (i = 0; i < count; i++) {
		switch (buf[i]) {
		case 'C':
			pad->flags |= FSPDRV_FLAG_EN_OPC;
			break;
		case 'c':
			pad->flags &= ~FSPDRV_FLAG_EN_OPC;
			break;
		default:
			return -EINVAL;
		}
	}
	return count;
}

PSMOUSE_DEFINE_ATTR(flags, S_IWUSR | S_IRUGO, NULL,
			fsp_attr_show_flags, fsp_attr_set_flags);

static ssize_t fsp_attr_show_ver(struct psmouse *psmouse,
					void *data, char *buf)
{
	return sprintf(buf, "Sentelic FSP kernel module %s\n", fsp_drv_ver);
}

PSMOUSE_DEFINE_RO_ATTR(ver, S_IRUGO, NULL, fsp_attr_show_ver);

static struct attribute *fsp_attributes[] = {
	&psmouse_attr_setreg.dattr.attr,
	&psmouse_attr_getreg.dattr.attr,
	&psmouse_attr_page.dattr.attr,
	&psmouse_attr_vscroll.dattr.attr,
	&psmouse_attr_hscroll.dattr.attr,
	&psmouse_attr_flags.dattr.attr,
	&psmouse_attr_ver.dattr.attr,
	NULL
};

static struct attribute_group fsp_attribute_group = {
	.attrs = fsp_attributes,
};

#ifdef	FSP_DEBUG
static void fsp_packet_debug(struct psmouse *psmouse, unsigned char packet[])
{
	static unsigned int ps2_packet_cnt;
	static unsigned int ps2_last_second;
	unsigned int jiffies_msec;
	const char *packet_type = "UNKNOWN";
	unsigned short abs_x = 0, abs_y = 0;

	/* Interpret & dump the packet data. */
	switch (packet[0] >> FSP_PKT_TYPE_SHIFT) {
	case FSP_PKT_TYPE_ABS:
		packet_type = "Absolute";
		abs_x = GET_ABS_X(packet);
		abs_y = GET_ABS_Y(packet);
		break;
	case FSP_PKT_TYPE_NORMAL:
		packet_type = "Normal";
		break;
	case FSP_PKT_TYPE_NOTIFY:
		packet_type = "Notify";
		break;
	case FSP_PKT_TYPE_NORMAL_OPC:
		packet_type = "Normal-OPC";
		break;
	}

	ps2_packet_cnt++;
	jiffies_msec = jiffies_to_msecs(jiffies);
	psmouse_dbg(psmouse,
		    "%08dms %s packets: %02x, %02x, %02x, %02x; "
		    "abs_x: %d, abs_y: %d\n",
		    jiffies_msec, packet_type,
		    packet[0], packet[1], packet[2], packet[3], abs_x, abs_y);

	if (jiffies_msec - ps2_last_second > 1000) {
		psmouse_dbg(psmouse, "PS/2 packets/sec = %d\n", ps2_packet_cnt);
		ps2_packet_cnt = 0;
		ps2_last_second = jiffies_msec;
	}
}
#else
static void fsp_packet_debug(struct psmouse *psmouse, unsigned char packet[])
{
}
#endif

static void fsp_set_slot(struct input_dev *dev, int slot, bool active,
			 unsigned int x, unsigned int y)
{
	input_mt_slot(dev, slot);
	input_mt_report_slot_state(dev, MT_TOOL_FINGER, active);
	if (active) {
		input_report_abs(dev, ABS_MT_POSITION_X, x);
		input_report_abs(dev, ABS_MT_POSITION_Y, y);
	}
}

static psmouse_ret_t fsp_process_byte(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;
	struct fsp_data *ad = psmouse->private;
	unsigned char *packet = psmouse->packet;
	unsigned char button_status = 0, lscroll = 0, rscroll = 0;
	unsigned short abs_x, abs_y, fgrs = 0;
	int rel_x, rel_y;

	if (psmouse->pktcnt < 4)
		return PSMOUSE_GOOD_DATA;

	/*
	 * Full packet accumulated, process it
	 */

	fsp_packet_debug(psmouse, packet);

	switch (psmouse->packet[0] >> FSP_PKT_TYPE_SHIFT) {
	case FSP_PKT_TYPE_ABS:

		if ((packet[0] == 0x48 || packet[0] == 0x49) &&
		    packet[1] == 0 && packet[2] == 0) {
			/*
			 * Ignore coordinate noise when finger leaving the
			 * surface, otherwise cursor may jump to upper-left
			 * corner.
			 */
			packet[3] &= 0xf0;
		}

		abs_x = GET_ABS_X(packet);
		abs_y = GET_ABS_Y(packet);

		if (packet[0] & FSP_PB0_MFMC) {
			/*
			 * MFMC packet: assume that there are two fingers on
			 * pad
			 */
			fgrs = 2;

			/* MFMC packet */
			if (packet[0] & FSP_PB0_MFMC_FGR2) {
				/* 2nd finger */
				if (ad->last_mt_fgr == 2) {
					/*
					 * workaround for buggy firmware
					 * which doesn't clear MFMC bit if
					 * the 1st finger is up
					 */
					fgrs = 1;
					fsp_set_slot(dev, 0, false, 0, 0);
				}
				ad->last_mt_fgr = 2;

				fsp_set_slot(dev, 1, fgrs == 2, abs_x, abs_y);
			} else {
				/* 1st finger */
				if (ad->last_mt_fgr == 1) {
					/*
					 * workaround for buggy firmware
					 * which doesn't clear MFMC bit if
					 * the 2nd finger is up
					 */
					fgrs = 1;
					fsp_set_slot(dev, 1, false, 0, 0);
				}
				ad->last_mt_fgr = 1;
				fsp_set_slot(dev, 0, fgrs != 0, abs_x, abs_y);
			}
		} else {
			/* SFAC packet */
			if ((packet[0] & (FSP_PB0_LBTN|FSP_PB0_PHY_BTN)) ==
				FSP_PB0_LBTN) {
				/* On-pad click in SFAC mode should be handled
				 * by userspace.  On-pad clicks in MFMC mode
				 * are real clickpad clicks, and not ignored.
				 */
				packet[0] &= ~FSP_PB0_LBTN;
			}

			/* no multi-finger information */
			ad->last_mt_fgr = 0;

			if (abs_x != 0 && abs_y != 0)
				fgrs = 1;

			fsp_set_slot(dev, 0, fgrs > 0, abs_x, abs_y);
			fsp_set_slot(dev, 1, false, 0, 0);
		}
		if (fgrs > 0) {
			input_report_abs(dev, ABS_X, abs_x);
			input_report_abs(dev, ABS_Y, abs_y);
		}
		input_report_key(dev, BTN_LEFT, packet[0] & 0x01);
		input_report_key(dev, BTN_RIGHT, packet[0] & 0x02);
		input_report_key(dev, BTN_TOUCH, fgrs);
		input_report_key(dev, BTN_TOOL_FINGER, fgrs == 1);
		input_report_key(dev, BTN_TOOL_DOUBLETAP, fgrs == 2);
		break;

	case FSP_PKT_TYPE_NORMAL_OPC:
		/* on-pad click, filter it if necessary */
		if ((ad->flags & FSPDRV_FLAG_EN_OPC) != FSPDRV_FLAG_EN_OPC)
			packet[0] &= ~FSP_PB0_LBTN;
		/* fall through */

	case FSP_PKT_TYPE_NORMAL:
		/* normal packet */
		/* special packet data translation from on-pad packets */
		if (packet[3] != 0) {
			if (packet[3] & BIT(0))
				button_status |= 0x01;	/* wheel down */
			if (packet[3] & BIT(1))
				button_status |= 0x0f;	/* wheel up */
			if (packet[3] & BIT(2))
				button_status |= BIT(4);/* horizontal left */
			if (packet[3] & BIT(3))
				button_status |= BIT(5);/* horizontal right */
			/* push back to packet queue */
			if (button_status != 0)
				packet[3] = button_status;
			rscroll = (packet[3] >> 4) & 1;
			lscroll = (packet[3] >> 5) & 1;
		}
		/*
		 * Processing wheel up/down and extra button events
		 */
		input_report_rel(dev, REL_WHEEL,
				 (int)(packet[3] & 8) - (int)(packet[3] & 7));
		input_report_rel(dev, REL_HWHEEL, lscroll - rscroll);
		input_report_key(dev, BTN_BACK, lscroll);
		input_report_key(dev, BTN_FORWARD, rscroll);

		/*
		 * Standard PS/2 Mouse
		 */
		input_report_key(dev, BTN_LEFT, packet[0] & 1);
		input_report_key(dev, BTN_MIDDLE, (packet[0] >> 2) & 1);
		input_report_key(dev, BTN_RIGHT, (packet[0] >> 1) & 1);

		rel_x = packet[1] ? (int)packet[1] - (int)((packet[0] << 4) & 0x100) : 0;
		rel_y = packet[2] ? (int)((packet[0] << 3) & 0x100) - (int)packet[2] : 0;

		input_report_rel(dev, REL_X, rel_x);
		input_report_rel(dev, REL_Y, rel_y);
		break;
	}

	input_sync(dev);

	return PSMOUSE_FULL_PACKET;
}

static int fsp_activate_protocol(struct psmouse *psmouse)
{
	struct fsp_data *pad = psmouse->private;
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[2];
	int val;

	/*
	 * Standard procedure to enter FSP Intellimouse mode
	 * (scrolling wheel, 4th and 5th buttons)
	 */
	param[0] = 200;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	param[0] = 200;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	param[0] =  80;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);

	ps2_command(ps2dev, param, PSMOUSE_CMD_GETID);
	if (param[0] != 0x04) {
		psmouse_err(psmouse,
			    "Unable to enable 4 bytes packet format.\n");
		return -EIO;
	}

	if (pad->ver < FSP_VER_STL3888_C0) {
		/* Preparing relative coordinates output for older hardware */
		if (fsp_reg_read(psmouse, FSP_REG_SYSCTL5, &val)) {
			psmouse_err(psmouse,
				    "Unable to read SYSCTL5 register.\n");
			return -EIO;
		}

		if (fsp_get_buttons(psmouse, &pad->buttons)) {
			psmouse_err(psmouse,
				    "Unable to retrieve number of buttons.\n");
			return -EIO;
		}

		val &= ~(FSP_BIT_EN_MSID7 | FSP_BIT_EN_MSID8 | FSP_BIT_EN_AUTO_MSID8);
		/* Ensure we are not in absolute mode */
		val &= ~FSP_BIT_EN_PKT_G0;
		if (pad->buttons == 0x06) {
			/* Left/Middle/Right & Scroll Up/Down/Right/Left */
			val |= FSP_BIT_EN_MSID6;
		}

		if (fsp_reg_write(psmouse, FSP_REG_SYSCTL5, val)) {
			psmouse_err(psmouse,
				    "Unable to set up required mode bits.\n");
			return -EIO;
		}

		/*
		 * Enable OPC tags such that driver can tell the difference
		 * between on-pad and real button click
		 */
		if (fsp_opc_tag_enable(psmouse, true))
			psmouse_warn(psmouse,
				     "Failed to enable OPC tag mode.\n");
		/* enable on-pad click by default */
		pad->flags |= FSPDRV_FLAG_EN_OPC;

		/* Enable on-pad vertical and horizontal scrolling */
		fsp_onpad_vscr(psmouse, true);
		fsp_onpad_hscr(psmouse, true);
	} else {
		/* Enable absolute coordinates output for Cx/Dx hardware */
		if (fsp_reg_write(psmouse, FSP_REG_SWC1,
				  FSP_BIT_SWC1_EN_ABS_1F |
				  FSP_BIT_SWC1_EN_ABS_2F |
				  FSP_BIT_SWC1_EN_FUP_OUT |
				  FSP_BIT_SWC1_EN_ABS_CON)) {
			psmouse_err(psmouse,
				    "Unable to enable absolute coordinates output.\n");
			return -EIO;
		}
	}

	return 0;
}

static int fsp_set_input_params(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;
	struct fsp_data *pad = psmouse->private;

	if (pad->ver < FSP_VER_STL3888_C0) {
		__set_bit(BTN_MIDDLE, dev->keybit);
		__set_bit(BTN_BACK, dev->keybit);
		__set_bit(BTN_FORWARD, dev->keybit);
		__set_bit(REL_WHEEL, dev->relbit);
		__set_bit(REL_HWHEEL, dev->relbit);
	} else {
		/*
		 * Hardware prior to Cx performs much better in relative mode;
		 * hence, only enable absolute coordinates output as well as
		 * multi-touch output for the newer hardware.
		 *
		 * Maximum coordinates can be computed as:
		 *
		 *	number of scanlines * 64 - 57
		 *
		 * where number of X/Y scanline lines are 16/12.
		 */
		int abs_x = 967, abs_y = 711;

		__set_bit(EV_ABS, dev->evbit);
		__clear_bit(EV_REL, dev->evbit);
		__set_bit(BTN_TOUCH, dev->keybit);
		__set_bit(BTN_TOOL_FINGER, dev->keybit);
		__set_bit(BTN_TOOL_DOUBLETAP, dev->keybit);
		__set_bit(INPUT_PROP_SEMI_MT, dev->propbit);

		input_set_abs_params(dev, ABS_X, 0, abs_x, 0, 0);
		input_set_abs_params(dev, ABS_Y, 0, abs_y, 0, 0);
		input_mt_init_slots(dev, 2, 0);
		input_set_abs_params(dev, ABS_MT_POSITION_X, 0, abs_x, 0, 0);
		input_set_abs_params(dev, ABS_MT_POSITION_Y, 0, abs_y, 0, 0);
	}

	return 0;
}

int fsp_detect(struct psmouse *psmouse, bool set_properties)
{
	int id;

	if (fsp_reg_read(psmouse, FSP_REG_DEVICE_ID, &id))
		return -EIO;

	if (id != 0x01)
		return -ENODEV;

	if (set_properties) {
		psmouse->vendor = "Sentelic";
		psmouse->name = "FingerSensingPad";
	}

	return 0;
}

static void fsp_reset(struct psmouse *psmouse)
{
	fsp_opc_tag_enable(psmouse, false);
	fsp_onpad_vscr(psmouse, false);
	fsp_onpad_hscr(psmouse, false);
}

static void fsp_disconnect(struct psmouse *psmouse)
{
	sysfs_remove_group(&psmouse->ps2dev.serio->dev.kobj,
			   &fsp_attribute_group);

	fsp_reset(psmouse);
	kfree(psmouse->private);
}

static int fsp_reconnect(struct psmouse *psmouse)
{
	int version;

	if (fsp_detect(psmouse, 0))
		return -ENODEV;

	if (fsp_get_version(psmouse, &version))
		return -ENODEV;

	if (fsp_activate_protocol(psmouse))
		return -EIO;

	return 0;
}

int fsp_init(struct psmouse *psmouse)
{
	struct fsp_data *priv;
	int ver, rev, sn = 0;
	int error;

	if (fsp_get_version(psmouse, &ver) ||
	    fsp_get_revision(psmouse, &rev)) {
		return -ENODEV;
	}
	if (ver >= FSP_VER_STL3888_C0) {
		/* firmware information is only available since C0 */
		fsp_get_sn(psmouse, &sn);
	}

	psmouse_info(psmouse,
		     "Finger Sensing Pad, hw: %d.%d.%d, sn: %x, sw: %s\n",
		     ver >> 4, ver & 0x0F, rev, sn, fsp_drv_ver);

	psmouse->private = priv = kzalloc(sizeof(struct fsp_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ver = ver;
	priv->rev = rev;

	psmouse->protocol_handler = fsp_process_byte;
	psmouse->disconnect = fsp_disconnect;
	psmouse->reconnect = fsp_reconnect;
	psmouse->cleanup = fsp_reset;
	psmouse->pktsize = 4;

	error = fsp_activate_protocol(psmouse);
	if (error)
		goto err_out;

	/* Set up various supported input event bits */
	error = fsp_set_input_params(psmouse);
	if (error)
		goto err_out;

	error = sysfs_create_group(&psmouse->ps2dev.serio->dev.kobj,
				   &fsp_attribute_group);
	if (error) {
		psmouse_err(psmouse,
			    "Failed to create sysfs attributes (%d)", error);
		goto err_out;
	}

	return 0;

 err_out:
	kfree(psmouse->private);
	psmouse->private = NULL;
	return error;
}
