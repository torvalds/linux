/* ZD1211 USB-WLAN driver for Linux
 *
 * Copyright (C) 2005-2007 Ulrich Kunitz <kune@deine-taler.de>
 * Copyright (C) 2006-2007 Daniel Drake <dsd@gentoo.org>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* This file implements all the hardware specific functions for the ZD1211
 * and ZD1211B chips. Support for the ZD1211B was possible after Timothy
 * Legge sent me a ZD1211B device. Thank you Tim. -- Uli
 */

#include <linux/kernel.h>
#include <linux/errno.h>

#include "zd_def.h"
#include "zd_chip.h"
#include "zd_mac.h"
#include "zd_rf.h"

void zd_chip_init(struct zd_chip *chip,
	         struct ieee80211_hw *hw,
		 struct usb_interface *intf)
{
	memset(chip, 0, sizeof(*chip));
	mutex_init(&chip->mutex);
	zd_usb_init(&chip->usb, hw, intf);
	zd_rf_init(&chip->rf);
}

void zd_chip_clear(struct zd_chip *chip)
{
	ZD_ASSERT(!mutex_is_locked(&chip->mutex));
	zd_usb_clear(&chip->usb);
	zd_rf_clear(&chip->rf);
	mutex_destroy(&chip->mutex);
	ZD_MEMCLEAR(chip, sizeof(*chip));
}

static int scnprint_mac_oui(struct zd_chip *chip, char *buffer, size_t size)
{
	u8 *addr = zd_mac_get_perm_addr(zd_chip_to_mac(chip));
	return scnprintf(buffer, size, "%02x-%02x-%02x",
		         addr[0], addr[1], addr[2]);
}

/* Prints an identifier line, which will support debugging. */
static int scnprint_id(struct zd_chip *chip, char *buffer, size_t size)
{
	int i = 0;

	i = scnprintf(buffer, size, "zd1211%s chip ",
		      zd_chip_is_zd1211b(chip) ? "b" : "");
	i += zd_usb_scnprint_id(&chip->usb, buffer+i, size-i);
	i += scnprintf(buffer+i, size-i, " ");
	i += scnprint_mac_oui(chip, buffer+i, size-i);
	i += scnprintf(buffer+i, size-i, " ");
	i += zd_rf_scnprint_id(&chip->rf, buffer+i, size-i);
	i += scnprintf(buffer+i, size-i, " pa%1x %c%c%c%c%c", chip->pa_type,
		chip->patch_cck_gain ? 'g' : '-',
		chip->patch_cr157 ? '7' : '-',
		chip->patch_6m_band_edge ? '6' : '-',
		chip->new_phy_layout ? 'N' : '-',
		chip->al2230s_bit ? 'S' : '-');
	return i;
}

static void print_id(struct zd_chip *chip)
{
	char buffer[80];

	scnprint_id(chip, buffer, sizeof(buffer));
	buffer[sizeof(buffer)-1] = 0;
	dev_info(zd_chip_dev(chip), "%s\n", buffer);
}

static zd_addr_t inc_addr(zd_addr_t addr)
{
	u16 a = (u16)addr;
	/* Control registers use byte addressing, but everything else uses word
	 * addressing. */
	if ((a & 0xf000) == CR_START)
		a += 2;
	else
		a += 1;
	return (zd_addr_t)a;
}

/* Read a variable number of 32-bit values. Parameter count is not allowed to
 * exceed USB_MAX_IOREAD32_COUNT.
 */
int zd_ioread32v_locked(struct zd_chip *chip, u32 *values, const zd_addr_t *addr,
		 unsigned int count)
{
	int r;
	int i;
	zd_addr_t *a16;
	u16 *v16;
	unsigned int count16;

	if (count > USB_MAX_IOREAD32_COUNT)
		return -EINVAL;

	/* Allocate a single memory block for values and addresses. */
	count16 = 2*count;
	a16 = (zd_addr_t *) kmalloc(count16 * (sizeof(zd_addr_t) + sizeof(u16)),
		                   GFP_KERNEL);
	if (!a16) {
		dev_dbg_f(zd_chip_dev(chip),
			  "error ENOMEM in allocation of a16\n");
		r = -ENOMEM;
		goto out;
	}
	v16 = (u16 *)(a16 + count16);

	for (i = 0; i < count; i++) {
		int j = 2*i;
		/* We read the high word always first. */
		a16[j] = inc_addr(addr[i]);
		a16[j+1] = addr[i];
	}

	r = zd_ioread16v_locked(chip, v16, a16, count16);
	if (r) {
		dev_dbg_f(zd_chip_dev(chip),
			  "error: zd_ioread16v_locked. Error number %d\n", r);
		goto out;
	}

	for (i = 0; i < count; i++) {
		int j = 2*i;
		values[i] = (v16[j] << 16) | v16[j+1];
	}

out:
	kfree((void *)a16);
	return r;
}

int _zd_iowrite32v_locked(struct zd_chip *chip, const struct zd_ioreq32 *ioreqs,
	           unsigned int count)
{
	int i, j, r;
	struct zd_ioreq16 *ioreqs16;
	unsigned int count16;

	ZD_ASSERT(mutex_is_locked(&chip->mutex));

	if (count == 0)
		return 0;
	if (count > USB_MAX_IOWRITE32_COUNT)
		return -EINVAL;

	/* Allocate a single memory block for values and addresses. */
	count16 = 2*count;
	ioreqs16 = kmalloc(count16 * sizeof(struct zd_ioreq16), GFP_KERNEL);
	if (!ioreqs16) {
		r = -ENOMEM;
		dev_dbg_f(zd_chip_dev(chip),
			  "error %d in ioreqs16 allocation\n", r);
		goto out;
	}

	for (i = 0; i < count; i++) {
		j = 2*i;
		/* We write the high word always first. */
		ioreqs16[j].value   = ioreqs[i].value >> 16;
		ioreqs16[j].addr    = inc_addr(ioreqs[i].addr);
		ioreqs16[j+1].value = ioreqs[i].value;
		ioreqs16[j+1].addr  = ioreqs[i].addr;
	}

	r = zd_usb_iowrite16v(&chip->usb, ioreqs16, count16);
#ifdef DEBUG
	if (r) {
		dev_dbg_f(zd_chip_dev(chip),
			  "error %d in zd_usb_write16v\n", r);
	}
#endif /* DEBUG */
out:
	kfree(ioreqs16);
	return r;
}

int zd_iowrite16a_locked(struct zd_chip *chip,
                  const struct zd_ioreq16 *ioreqs, unsigned int count)
{
	int r;
	unsigned int i, j, t, max;

	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	for (i = 0; i < count; i += j + t) {
		t = 0;
		max = count-i;
		if (max > USB_MAX_IOWRITE16_COUNT)
			max = USB_MAX_IOWRITE16_COUNT;
		for (j = 0; j < max; j++) {
			if (!ioreqs[i+j].addr) {
				t = 1;
				break;
			}
		}

		r = zd_usb_iowrite16v(&chip->usb, &ioreqs[i], j);
		if (r) {
			dev_dbg_f(zd_chip_dev(chip),
				  "error zd_usb_iowrite16v. Error number %d\n",
				  r);
			return r;
		}
	}

	return 0;
}

/* Writes a variable number of 32 bit registers. The functions will split
 * that in several USB requests. A split can be forced by inserting an IO
 * request with an zero address field.
 */
int zd_iowrite32a_locked(struct zd_chip *chip,
	          const struct zd_ioreq32 *ioreqs, unsigned int count)
{
	int r;
	unsigned int i, j, t, max;

	for (i = 0; i < count; i += j + t) {
		t = 0;
		max = count-i;
		if (max > USB_MAX_IOWRITE32_COUNT)
			max = USB_MAX_IOWRITE32_COUNT;
		for (j = 0; j < max; j++) {
			if (!ioreqs[i+j].addr) {
				t = 1;
				break;
			}
		}

		r = _zd_iowrite32v_locked(chip, &ioreqs[i], j);
		if (r) {
			dev_dbg_f(zd_chip_dev(chip),
				"error _zd_iowrite32v_locked."
				" Error number %d\n", r);
			return r;
		}
	}

	return 0;
}

int zd_ioread16(struct zd_chip *chip, zd_addr_t addr, u16 *value)
{
	int r;

	mutex_lock(&chip->mutex);
	r = zd_ioread16_locked(chip, value, addr);
	mutex_unlock(&chip->mutex);
	return r;
}

int zd_ioread32(struct zd_chip *chip, zd_addr_t addr, u32 *value)
{
	int r;

	mutex_lock(&chip->mutex);
	r = zd_ioread32_locked(chip, value, addr);
	mutex_unlock(&chip->mutex);
	return r;
}

int zd_iowrite16(struct zd_chip *chip, zd_addr_t addr, u16 value)
{
	int r;

	mutex_lock(&chip->mutex);
	r = zd_iowrite16_locked(chip, value, addr);
	mutex_unlock(&chip->mutex);
	return r;
}

int zd_iowrite32(struct zd_chip *chip, zd_addr_t addr, u32 value)
{
	int r;

	mutex_lock(&chip->mutex);
	r = zd_iowrite32_locked(chip, value, addr);
	mutex_unlock(&chip->mutex);
	return r;
}

int zd_ioread32v(struct zd_chip *chip, const zd_addr_t *addresses,
	          u32 *values, unsigned int count)
{
	int r;

	mutex_lock(&chip->mutex);
	r = zd_ioread32v_locked(chip, values, addresses, count);
	mutex_unlock(&chip->mutex);
	return r;
}

int zd_iowrite32a(struct zd_chip *chip, const struct zd_ioreq32 *ioreqs,
	          unsigned int count)
{
	int r;

	mutex_lock(&chip->mutex);
	r = zd_iowrite32a_locked(chip, ioreqs, count);
	mutex_unlock(&chip->mutex);
	return r;
}

static int read_pod(struct zd_chip *chip, u8 *rf_type)
{
	int r;
	u32 value;

	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	r = zd_ioread32_locked(chip, &value, E2P_POD);
	if (r)
		goto error;
	dev_dbg_f(zd_chip_dev(chip), "E2P_POD %#010x\n", value);

	/* FIXME: AL2230 handling (Bit 7 in POD) */
	*rf_type = value & 0x0f;
	chip->pa_type = (value >> 16) & 0x0f;
	chip->patch_cck_gain = (value >> 8) & 0x1;
	chip->patch_cr157 = (value >> 13) & 0x1;
	chip->patch_6m_band_edge = (value >> 21) & 0x1;
	chip->new_phy_layout = (value >> 31) & 0x1;
	chip->al2230s_bit = (value >> 7) & 0x1;
	chip->link_led = ((value >> 4) & 1) ? LED1 : LED2;
	chip->supports_tx_led = 1;
	if (value & (1 << 24)) { /* LED scenario */
		if (value & (1 << 29))
			chip->supports_tx_led = 0;
	}

	dev_dbg_f(zd_chip_dev(chip),
		"RF %s %#01x PA type %#01x patch CCK %d patch CR157 %d "
		"patch 6M %d new PHY %d link LED%d tx led %d\n",
		zd_rf_name(*rf_type), *rf_type,
		chip->pa_type, chip->patch_cck_gain,
		chip->patch_cr157, chip->patch_6m_band_edge,
		chip->new_phy_layout,
		chip->link_led == LED1 ? 1 : 2,
		chip->supports_tx_led);
	return 0;
error:
	*rf_type = 0;
	chip->pa_type = 0;
	chip->patch_cck_gain = 0;
	chip->patch_cr157 = 0;
	chip->patch_6m_band_edge = 0;
	chip->new_phy_layout = 0;
	return r;
}

/* MAC address: if custom mac addresses are to be used CR_MAC_ADDR_P1 and
 *              CR_MAC_ADDR_P2 must be overwritten
 */
int zd_write_mac_addr(struct zd_chip *chip, const u8 *mac_addr)
{
	int r;
	struct zd_ioreq32 reqs[2] = {
		[0] = { .addr = CR_MAC_ADDR_P1 },
		[1] = { .addr = CR_MAC_ADDR_P2 },
	};

	if (mac_addr) {
		reqs[0].value = (mac_addr[3] << 24)
			      | (mac_addr[2] << 16)
			      | (mac_addr[1] <<  8)
			      |  mac_addr[0];
		reqs[1].value = (mac_addr[5] <<  8)
			      |  mac_addr[4];
		dev_dbg_f(zd_chip_dev(chip), "mac addr %pM\n", mac_addr);
	} else {
		dev_dbg_f(zd_chip_dev(chip), "set NULL mac\n");
	}

	mutex_lock(&chip->mutex);
	r = zd_iowrite32a_locked(chip, reqs, ARRAY_SIZE(reqs));
	mutex_unlock(&chip->mutex);
	return r;
}

int zd_read_regdomain(struct zd_chip *chip, u8 *regdomain)
{
	int r;
	u32 value;

	mutex_lock(&chip->mutex);
	r = zd_ioread32_locked(chip, &value, E2P_SUBID);
	mutex_unlock(&chip->mutex);
	if (r)
		return r;

	*regdomain = value >> 16;
	dev_dbg_f(zd_chip_dev(chip), "regdomain: %#04x\n", *regdomain);

	return 0;
}

static int read_values(struct zd_chip *chip, u8 *values, size_t count,
	               zd_addr_t e2p_addr, u32 guard)
{
	int r;
	int i;
	u32 v;

	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	for (i = 0;;) {
		r = zd_ioread32_locked(chip, &v,
			               (zd_addr_t)((u16)e2p_addr+i/2));
		if (r)
			return r;
		v -= guard;
		if (i+4 < count) {
			values[i++] = v;
			values[i++] = v >>  8;
			values[i++] = v >> 16;
			values[i++] = v >> 24;
			continue;
		}
		for (;i < count; i++)
			values[i] = v >> (8*(i%3));
		return 0;
	}
}

static int read_pwr_cal_values(struct zd_chip *chip)
{
	return read_values(chip, chip->pwr_cal_values,
		        E2P_CHANNEL_COUNT, E2P_PWR_CAL_VALUE1,
			0);
}

static int read_pwr_int_values(struct zd_chip *chip)
{
	return read_values(chip, chip->pwr_int_values,
		        E2P_CHANNEL_COUNT, E2P_PWR_INT_VALUE1,
			E2P_PWR_INT_GUARD);
}

static int read_ofdm_cal_values(struct zd_chip *chip)
{
	int r;
	int i;
	static const zd_addr_t addresses[] = {
		E2P_36M_CAL_VALUE1,
		E2P_48M_CAL_VALUE1,
		E2P_54M_CAL_VALUE1,
	};

	for (i = 0; i < 3; i++) {
		r = read_values(chip, chip->ofdm_cal_values[i],
				E2P_CHANNEL_COUNT, addresses[i], 0);
		if (r)
			return r;
	}
	return 0;
}

static int read_cal_int_tables(struct zd_chip *chip)
{
	int r;

	r = read_pwr_cal_values(chip);
	if (r)
		return r;
	r = read_pwr_int_values(chip);
	if (r)
		return r;
	r = read_ofdm_cal_values(chip);
	if (r)
		return r;
	return 0;
}

/* phy means physical registers */
int zd_chip_lock_phy_regs(struct zd_chip *chip)
{
	int r;
	u32 tmp;

	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	r = zd_ioread32_locked(chip, &tmp, CR_REG1);
	if (r) {
		dev_err(zd_chip_dev(chip), "error ioread32(CR_REG1): %d\n", r);
		return r;
	}

	tmp &= ~UNLOCK_PHY_REGS;

	r = zd_iowrite32_locked(chip, tmp, CR_REG1);
	if (r)
		dev_err(zd_chip_dev(chip), "error iowrite32(CR_REG1): %d\n", r);
	return r;
}

int zd_chip_unlock_phy_regs(struct zd_chip *chip)
{
	int r;
	u32 tmp;

	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	r = zd_ioread32_locked(chip, &tmp, CR_REG1);
	if (r) {
		dev_err(zd_chip_dev(chip),
			"error ioread32(CR_REG1): %d\n", r);
		return r;
	}

	tmp |= UNLOCK_PHY_REGS;

	r = zd_iowrite32_locked(chip, tmp, CR_REG1);
	if (r)
		dev_err(zd_chip_dev(chip), "error iowrite32(CR_REG1): %d\n", r);
	return r;
}

/* CR157 can be optionally patched by the EEPROM for original ZD1211 */
static int patch_cr157(struct zd_chip *chip)
{
	int r;
	u16 value;

	if (!chip->patch_cr157)
		return 0;

	r = zd_ioread16_locked(chip, &value, E2P_PHY_REG);
	if (r)
		return r;

	dev_dbg_f(zd_chip_dev(chip), "patching value %x\n", value >> 8);
	return zd_iowrite32_locked(chip, value >> 8, CR157);
}

/*
 * 6M band edge can be optionally overwritten for certain RF's
 * Vendor driver says: for FCC regulation, enabled per HWFeature 6M band edge
 * bit (for AL2230, AL2230S)
 */
static int patch_6m_band_edge(struct zd_chip *chip, u8 channel)
{
	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	if (!chip->patch_6m_band_edge)
		return 0;

	return zd_rf_patch_6m_band_edge(&chip->rf, channel);
}

/* Generic implementation of 6M band edge patching, used by most RFs via
 * zd_rf_generic_patch_6m() */
int zd_chip_generic_patch_6m_band(struct zd_chip *chip, int channel)
{
	struct zd_ioreq16 ioreqs[] = {
		{ CR128, 0x14 }, { CR129, 0x12 }, { CR130, 0x10 },
		{ CR47,  0x1e },
	};

	/* FIXME: Channel 11 is not the edge for all regulatory domains. */
	if (channel == 1 || channel == 11)
		ioreqs[0].value = 0x12;

	dev_dbg_f(zd_chip_dev(chip), "patching for channel %d\n", channel);
	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int zd1211_hw_reset_phy(struct zd_chip *chip)
{
	static const struct zd_ioreq16 ioreqs[] = {
		{ CR0,   0x0a }, { CR1,   0x06 }, { CR2,   0x26 },
		{ CR3,   0x38 }, { CR4,   0x80 }, { CR9,   0xa0 },
		{ CR10,  0x81 }, { CR11,  0x00 }, { CR12,  0x7f },
		{ CR13,  0x8c }, { CR14,  0x80 }, { CR15,  0x3d },
		{ CR16,  0x20 }, { CR17,  0x1e }, { CR18,  0x0a },
		{ CR19,  0x48 }, { CR20,  0x0c }, { CR21,  0x0c },
		{ CR22,  0x23 }, { CR23,  0x90 }, { CR24,  0x14 },
		{ CR25,  0x40 }, { CR26,  0x10 }, { CR27,  0x19 },
		{ CR28,  0x7f }, { CR29,  0x80 }, { CR30,  0x4b },
		{ CR31,  0x60 }, { CR32,  0x43 }, { CR33,  0x08 },
		{ CR34,  0x06 }, { CR35,  0x0a }, { CR36,  0x00 },
		{ CR37,  0x00 }, { CR38,  0x38 }, { CR39,  0x0c },
		{ CR40,  0x84 }, { CR41,  0x2a }, { CR42,  0x80 },
		{ CR43,  0x10 }, { CR44,  0x12 }, { CR46,  0xff },
		{ CR47,  0x1E }, { CR48,  0x26 }, { CR49,  0x5b },
		{ CR64,  0xd0 }, { CR65,  0x04 }, { CR66,  0x58 },
		{ CR67,  0xc9 }, { CR68,  0x88 }, { CR69,  0x41 },
		{ CR70,  0x23 }, { CR71,  0x10 }, { CR72,  0xff },
		{ CR73,  0x32 }, { CR74,  0x30 }, { CR75,  0x65 },
		{ CR76,  0x41 }, { CR77,  0x1b }, { CR78,  0x30 },
		{ CR79,  0x68 }, { CR80,  0x64 }, { CR81,  0x64 },
		{ CR82,  0x00 }, { CR83,  0x00 }, { CR84,  0x00 },
		{ CR85,  0x02 }, { CR86,  0x00 }, { CR87,  0x00 },
		{ CR88,  0xff }, { CR89,  0xfc }, { CR90,  0x00 },
		{ CR91,  0x00 }, { CR92,  0x00 }, { CR93,  0x08 },
		{ CR94,  0x00 }, { CR95,  0x00 }, { CR96,  0xff },
		{ CR97,  0xe7 }, { CR98,  0x00 }, { CR99,  0x00 },
		{ CR100, 0x00 }, { CR101, 0xae }, { CR102, 0x02 },
		{ CR103, 0x00 }, { CR104, 0x03 }, { CR105, 0x65 },
		{ CR106, 0x04 }, { CR107, 0x00 }, { CR108, 0x0a },
		{ CR109, 0xaa }, { CR110, 0xaa }, { CR111, 0x25 },
		{ CR112, 0x25 }, { CR113, 0x00 }, { CR119, 0x1e },
		{ CR125, 0x90 }, { CR126, 0x00 }, { CR127, 0x00 },
		{ },
		{ CR5,   0x00 }, { CR6,   0x00 }, { CR7,   0x00 },
		{ CR8,   0x00 }, { CR9,   0x20 }, { CR12,  0xf0 },
		{ CR20,  0x0e }, { CR21,  0x0e }, { CR27,  0x10 },
		{ CR44,  0x33 }, { CR47,  0x1E }, { CR83,  0x24 },
		{ CR84,  0x04 }, { CR85,  0x00 }, { CR86,  0x0C },
		{ CR87,  0x12 }, { CR88,  0x0C }, { CR89,  0x00 },
		{ CR90,  0x10 }, { CR91,  0x08 }, { CR93,  0x00 },
		{ CR94,  0x01 }, { CR95,  0x00 }, { CR96,  0x50 },
		{ CR97,  0x37 }, { CR98,  0x35 }, { CR101, 0x13 },
		{ CR102, 0x27 }, { CR103, 0x27 }, { CR104, 0x18 },
		{ CR105, 0x12 }, { CR109, 0x27 }, { CR110, 0x27 },
		{ CR111, 0x27 }, { CR112, 0x27 }, { CR113, 0x27 },
		{ CR114, 0x27 }, { CR115, 0x26 }, { CR116, 0x24 },
		{ CR117, 0xfc }, { CR118, 0xfa }, { CR120, 0x4f },
		{ CR125, 0xaa }, { CR127, 0x03 }, { CR128, 0x14 },
		{ CR129, 0x12 }, { CR130, 0x10 }, { CR131, 0x0C },
		{ CR136, 0xdf }, { CR137, 0x40 }, { CR138, 0xa0 },
		{ CR139, 0xb0 }, { CR140, 0x99 }, { CR141, 0x82 },
		{ CR142, 0x54 }, { CR143, 0x1c }, { CR144, 0x6c },
		{ CR147, 0x07 }, { CR148, 0x4c }, { CR149, 0x50 },
		{ CR150, 0x0e }, { CR151, 0x18 }, { CR160, 0xfe },
		{ CR161, 0xee }, { CR162, 0xaa }, { CR163, 0xfa },
		{ CR164, 0xfa }, { CR165, 0xea }, { CR166, 0xbe },
		{ CR167, 0xbe }, { CR168, 0x6a }, { CR169, 0xba },
		{ CR170, 0xba }, { CR171, 0xba },
		/* Note: CR204 must lead the CR203 */
		{ CR204, 0x7d },
		{ },
		{ CR203, 0x30 },
	};

	int r, t;

	dev_dbg_f(zd_chip_dev(chip), "\n");

	r = zd_chip_lock_phy_regs(chip);
	if (r)
		goto out;

	r = zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
	if (r)
		goto unlock;

	r = patch_cr157(chip);
unlock:
	t = zd_chip_unlock_phy_regs(chip);
	if (t && !r)
		r = t;
out:
	return r;
}

static int zd1211b_hw_reset_phy(struct zd_chip *chip)
{
	static const struct zd_ioreq16 ioreqs[] = {
		{ CR0,   0x14 }, { CR1,   0x06 }, { CR2,   0x26 },
		{ CR3,   0x38 }, { CR4,   0x80 }, { CR9,   0xe0 },
		{ CR10,  0x81 },
		/* power control { { CR11,  1 << 6 }, */
		{ CR11,  0x00 },
		{ CR12,  0xf0 }, { CR13,  0x8c }, { CR14,  0x80 },
		{ CR15,  0x3d }, { CR16,  0x20 }, { CR17,  0x1e },
		{ CR18,  0x0a }, { CR19,  0x48 },
		{ CR20,  0x10 }, /* Org:0x0E, ComTrend:RalLink AP */
		{ CR21,  0x0e }, { CR22,  0x23 }, { CR23,  0x90 },
		{ CR24,  0x14 }, { CR25,  0x40 }, { CR26,  0x10 },
		{ CR27,  0x10 }, { CR28,  0x7f }, { CR29,  0x80 },
		{ CR30,  0x4b }, /* ASIC/FWT, no jointly decoder */
		{ CR31,  0x60 }, { CR32,  0x43 }, { CR33,  0x08 },
		{ CR34,  0x06 }, { CR35,  0x0a }, { CR36,  0x00 },
		{ CR37,  0x00 }, { CR38,  0x38 }, { CR39,  0x0c },
		{ CR40,  0x84 }, { CR41,  0x2a }, { CR42,  0x80 },
		{ CR43,  0x10 }, { CR44,  0x33 }, { CR46,  0xff },
		{ CR47,  0x1E }, { CR48,  0x26 }, { CR49,  0x5b },
		{ CR64,  0xd0 }, { CR65,  0x04 }, { CR66,  0x58 },
		{ CR67,  0xc9 }, { CR68,  0x88 }, { CR69,  0x41 },
		{ CR70,  0x23 }, { CR71,  0x10 }, { CR72,  0xff },
		{ CR73,  0x32 }, { CR74,  0x30 }, { CR75,  0x65 },
		{ CR76,  0x41 }, { CR77,  0x1b }, { CR78,  0x30 },
		{ CR79,  0xf0 }, { CR80,  0x64 }, { CR81,  0x64 },
		{ CR82,  0x00 }, { CR83,  0x24 }, { CR84,  0x04 },
		{ CR85,  0x00 }, { CR86,  0x0c }, { CR87,  0x12 },
		{ CR88,  0x0c }, { CR89,  0x00 }, { CR90,  0x58 },
		{ CR91,  0x04 }, { CR92,  0x00 }, { CR93,  0x00 },
		{ CR94,  0x01 },
		{ CR95,  0x20 }, /* ZD1211B */
		{ CR96,  0x50 }, { CR97,  0x37 }, { CR98,  0x35 },
		{ CR99,  0x00 }, { CR100, 0x01 }, { CR101, 0x13 },
		{ CR102, 0x27 }, { CR103, 0x27 }, { CR104, 0x18 },
		{ CR105, 0x12 }, { CR106, 0x04 }, { CR107, 0x00 },
		{ CR108, 0x0a }, { CR109, 0x27 }, { CR110, 0x27 },
		{ CR111, 0x27 }, { CR112, 0x27 }, { CR113, 0x27 },
		{ CR114, 0x27 }, { CR115, 0x26 }, { CR116, 0x24 },
		{ CR117, 0xfc }, { CR118, 0xfa }, { CR119, 0x1e },
		{ CR125, 0x90 }, { CR126, 0x00 }, { CR127, 0x00 },
		{ CR128, 0x14 }, { CR129, 0x12 }, { CR130, 0x10 },
		{ CR131, 0x0c }, { CR136, 0xdf }, { CR137, 0xa0 },
		{ CR138, 0xa8 }, { CR139, 0xb4 }, { CR140, 0x98 },
		{ CR141, 0x82 }, { CR142, 0x53 }, { CR143, 0x1c },
		{ CR144, 0x6c }, { CR147, 0x07 }, { CR148, 0x40 },
		{ CR149, 0x40 }, /* Org:0x50 ComTrend:RalLink AP */
		{ CR150, 0x14 }, /* Org:0x0E ComTrend:RalLink AP */
		{ CR151, 0x18 }, { CR159, 0x70 }, { CR160, 0xfe },
		{ CR161, 0xee }, { CR162, 0xaa }, { CR163, 0xfa },
		{ CR164, 0xfa }, { CR165, 0xea }, { CR166, 0xbe },
		{ CR167, 0xbe }, { CR168, 0x6a }, { CR169, 0xba },
		{ CR170, 0xba }, { CR171, 0xba },
		/* Note: CR204 must lead the CR203 */
		{ CR204, 0x7d },
		{},
		{ CR203, 0x30 },
	};

	int r, t;

	dev_dbg_f(zd_chip_dev(chip), "\n");

	r = zd_chip_lock_phy_regs(chip);
	if (r)
		goto out;

	r = zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
	t = zd_chip_unlock_phy_regs(chip);
	if (t && !r)
		r = t;
out:
	return r;
}

static int hw_reset_phy(struct zd_chip *chip)
{
	return zd_chip_is_zd1211b(chip) ? zd1211b_hw_reset_phy(chip) :
		                  zd1211_hw_reset_phy(chip);
}

static int zd1211_hw_init_hmac(struct zd_chip *chip)
{
	static const struct zd_ioreq32 ioreqs[] = {
		{ CR_ZD1211_RETRY_MAX,		ZD1211_RETRY_COUNT },
		{ CR_RX_THRESHOLD,		0x000c0640 },
	};

	dev_dbg_f(zd_chip_dev(chip), "\n");
	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	return zd_iowrite32a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int zd1211b_hw_init_hmac(struct zd_chip *chip)
{
	static const struct zd_ioreq32 ioreqs[] = {
		{ CR_ZD1211B_RETRY_MAX,		ZD1211B_RETRY_COUNT },
		{ CR_ZD1211B_CWIN_MAX_MIN_AC0,	0x007f003f },
		{ CR_ZD1211B_CWIN_MAX_MIN_AC1,	0x007f003f },
		{ CR_ZD1211B_CWIN_MAX_MIN_AC2,  0x003f001f },
		{ CR_ZD1211B_CWIN_MAX_MIN_AC3,  0x001f000f },
		{ CR_ZD1211B_AIFS_CTL1,		0x00280028 },
		{ CR_ZD1211B_AIFS_CTL2,		0x008C003C },
		{ CR_ZD1211B_TXOP,		0x01800824 },
		{ CR_RX_THRESHOLD,		0x000c0eff, },
	};

	dev_dbg_f(zd_chip_dev(chip), "\n");
	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	return zd_iowrite32a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int hw_init_hmac(struct zd_chip *chip)
{
	int r;
	static const struct zd_ioreq32 ioreqs[] = {
		{ CR_ACK_TIMEOUT_EXT,		0x20 },
		{ CR_ADDA_MBIAS_WARMTIME,	0x30000808 },
		{ CR_SNIFFER_ON,		0 },
		{ CR_RX_FILTER,			STA_RX_FILTER },
		{ CR_GROUP_HASH_P1,		0x00 },
		{ CR_GROUP_HASH_P2,		0x80000000 },
		{ CR_REG1,			0xa4 },
		{ CR_ADDA_PWR_DWN,		0x7f },
		{ CR_BCN_PLCP_CFG,		0x00f00401 },
		{ CR_PHY_DELAY,			0x00 },
		{ CR_ACK_TIMEOUT_EXT,		0x80 },
		{ CR_ADDA_PWR_DWN,		0x00 },
		{ CR_ACK_TIME_80211,		0x100 },
		{ CR_RX_PE_DELAY,		0x70 },
		{ CR_PS_CTRL,			0x10000000 },
		{ CR_RTS_CTS_RATE,		0x02030203 },
		{ CR_AFTER_PNP,			0x1 },
		{ CR_WEP_PROTECT,		0x114 },
		{ CR_IFS_VALUE,			IFS_VALUE_DEFAULT },
		{ CR_CAM_MODE,			MODE_AP_WDS},
	};

	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	r = zd_iowrite32a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
	if (r)
		return r;

	return zd_chip_is_zd1211b(chip) ?
		zd1211b_hw_init_hmac(chip) : zd1211_hw_init_hmac(chip);
}

struct aw_pt_bi {
	u32 atim_wnd_period;
	u32 pre_tbtt;
	u32 beacon_interval;
};

static int get_aw_pt_bi(struct zd_chip *chip, struct aw_pt_bi *s)
{
	int r;
	static const zd_addr_t aw_pt_bi_addr[] =
		{ CR_ATIM_WND_PERIOD, CR_PRE_TBTT, CR_BCN_INTERVAL };
	u32 values[3];

	r = zd_ioread32v_locked(chip, values, (const zd_addr_t *)aw_pt_bi_addr,
		         ARRAY_SIZE(aw_pt_bi_addr));
	if (r) {
		memset(s, 0, sizeof(*s));
		return r;
	}

	s->atim_wnd_period = values[0];
	s->pre_tbtt = values[1];
	s->beacon_interval = values[2];
	return 0;
}

static int set_aw_pt_bi(struct zd_chip *chip, struct aw_pt_bi *s)
{
	struct zd_ioreq32 reqs[3];

	if (s->beacon_interval <= 5)
		s->beacon_interval = 5;
	if (s->pre_tbtt < 4 || s->pre_tbtt >= s->beacon_interval)
		s->pre_tbtt = s->beacon_interval - 1;
	if (s->atim_wnd_period >= s->pre_tbtt)
		s->atim_wnd_period = s->pre_tbtt - 1;

	reqs[0].addr = CR_ATIM_WND_PERIOD;
	reqs[0].value = s->atim_wnd_period;
	reqs[1].addr = CR_PRE_TBTT;
	reqs[1].value = s->pre_tbtt;
	reqs[2].addr = CR_BCN_INTERVAL;
	reqs[2].value = s->beacon_interval;

	return zd_iowrite32a_locked(chip, reqs, ARRAY_SIZE(reqs));
}


static int set_beacon_interval(struct zd_chip *chip, u32 interval)
{
	int r;
	struct aw_pt_bi s;

	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	r = get_aw_pt_bi(chip, &s);
	if (r)
		return r;
	s.beacon_interval = interval;
	return set_aw_pt_bi(chip, &s);
}

int zd_set_beacon_interval(struct zd_chip *chip, u32 interval)
{
	int r;

	mutex_lock(&chip->mutex);
	r = set_beacon_interval(chip, interval);
	mutex_unlock(&chip->mutex);
	return r;
}

static int hw_init(struct zd_chip *chip)
{
	int r;

	dev_dbg_f(zd_chip_dev(chip), "\n");
	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	r = hw_reset_phy(chip);
	if (r)
		return r;

	r = hw_init_hmac(chip);
	if (r)
		return r;

	return set_beacon_interval(chip, 100);
}

static zd_addr_t fw_reg_addr(struct zd_chip *chip, u16 offset)
{
	return (zd_addr_t)((u16)chip->fw_regs_base + offset);
}

#ifdef DEBUG
static int dump_cr(struct zd_chip *chip, const zd_addr_t addr,
	           const char *addr_string)
{
	int r;
	u32 value;

	r = zd_ioread32_locked(chip, &value, addr);
	if (r) {
		dev_dbg_f(zd_chip_dev(chip),
			"error reading %s. Error number %d\n", addr_string, r);
		return r;
	}

	dev_dbg_f(zd_chip_dev(chip), "%s %#010x\n",
		addr_string, (unsigned int)value);
	return 0;
}

static int test_init(struct zd_chip *chip)
{
	int r;

	r = dump_cr(chip, CR_AFTER_PNP, "CR_AFTER_PNP");
	if (r)
		return r;
	r = dump_cr(chip, CR_GPI_EN, "CR_GPI_EN");
	if (r)
		return r;
	return dump_cr(chip, CR_INTERRUPT, "CR_INTERRUPT");
}

static void dump_fw_registers(struct zd_chip *chip)
{
	const zd_addr_t addr[4] = {
		fw_reg_addr(chip, FW_REG_FIRMWARE_VER),
		fw_reg_addr(chip, FW_REG_USB_SPEED),
		fw_reg_addr(chip, FW_REG_FIX_TX_RATE),
		fw_reg_addr(chip, FW_REG_LED_LINK_STATUS),
	};

	int r;
	u16 values[4];

	r = zd_ioread16v_locked(chip, values, (const zd_addr_t*)addr,
		         ARRAY_SIZE(addr));
	if (r) {
		dev_dbg_f(zd_chip_dev(chip), "error %d zd_ioread16v_locked\n",
			 r);
		return;
	}

	dev_dbg_f(zd_chip_dev(chip), "FW_FIRMWARE_VER %#06hx\n", values[0]);
	dev_dbg_f(zd_chip_dev(chip), "FW_USB_SPEED %#06hx\n", values[1]);
	dev_dbg_f(zd_chip_dev(chip), "FW_FIX_TX_RATE %#06hx\n", values[2]);
	dev_dbg_f(zd_chip_dev(chip), "FW_LINK_STATUS %#06hx\n", values[3]);
}
#endif /* DEBUG */

static int print_fw_version(struct zd_chip *chip)
{
	int r;
	u16 version;

	r = zd_ioread16_locked(chip, &version,
		fw_reg_addr(chip, FW_REG_FIRMWARE_VER));
	if (r)
		return r;

	dev_info(zd_chip_dev(chip),"firmware version %04hx\n", version);
	return 0;
}

static int set_mandatory_rates(struct zd_chip *chip, int gmode)
{
	u32 rates;
	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	/* This sets the mandatory rates, which only depend from the standard
	 * that the device is supporting. Until further notice we should try
	 * to support 802.11g also for full speed USB.
	 */
	if (!gmode)
		rates = CR_RATE_1M|CR_RATE_2M|CR_RATE_5_5M|CR_RATE_11M;
	else
		rates = CR_RATE_1M|CR_RATE_2M|CR_RATE_5_5M|CR_RATE_11M|
			CR_RATE_6M|CR_RATE_12M|CR_RATE_24M;

	return zd_iowrite32_locked(chip, rates, CR_MANDATORY_RATE_TBL);
}

int zd_chip_set_rts_cts_rate_locked(struct zd_chip *chip,
				    int preamble)
{
	u32 value = 0;

	dev_dbg_f(zd_chip_dev(chip), "preamble=%x\n", preamble);
	value |= preamble << RTSCTS_SH_RTS_PMB_TYPE;
	value |= preamble << RTSCTS_SH_CTS_PMB_TYPE;

	/* We always send 11M RTS/self-CTS messages, like the vendor driver. */
	value |= ZD_PURE_RATE(ZD_CCK_RATE_11M) << RTSCTS_SH_RTS_RATE;
	value |= ZD_RX_CCK << RTSCTS_SH_RTS_MOD_TYPE;
	value |= ZD_PURE_RATE(ZD_CCK_RATE_11M) << RTSCTS_SH_CTS_RATE;
	value |= ZD_RX_CCK << RTSCTS_SH_CTS_MOD_TYPE;

	return zd_iowrite32_locked(chip, value, CR_RTS_CTS_RATE);
}

int zd_chip_enable_hwint(struct zd_chip *chip)
{
	int r;

	mutex_lock(&chip->mutex);
	r = zd_iowrite32_locked(chip, HWINT_ENABLED, CR_INTERRUPT);
	mutex_unlock(&chip->mutex);
	return r;
}

static int disable_hwint(struct zd_chip *chip)
{
	return zd_iowrite32_locked(chip, HWINT_DISABLED, CR_INTERRUPT);
}

int zd_chip_disable_hwint(struct zd_chip *chip)
{
	int r;

	mutex_lock(&chip->mutex);
	r = disable_hwint(chip);
	mutex_unlock(&chip->mutex);
	return r;
}

static int read_fw_regs_offset(struct zd_chip *chip)
{
	int r;

	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	r = zd_ioread16_locked(chip, (u16*)&chip->fw_regs_base,
		               FWRAW_REGS_ADDR);
	if (r)
		return r;
	dev_dbg_f(zd_chip_dev(chip), "fw_regs_base: %#06hx\n",
		  (u16)chip->fw_regs_base);

	return 0;
}

/* Read mac address using pre-firmware interface */
int zd_chip_read_mac_addr_fw(struct zd_chip *chip, u8 *addr)
{
	dev_dbg_f(zd_chip_dev(chip), "\n");
	return zd_usb_read_fw(&chip->usb, E2P_MAC_ADDR_P1, addr,
		ETH_ALEN);
}

int zd_chip_init_hw(struct zd_chip *chip)
{
	int r;
	u8 rf_type;

	dev_dbg_f(zd_chip_dev(chip), "\n");

	mutex_lock(&chip->mutex);

#ifdef DEBUG
	r = test_init(chip);
	if (r)
		goto out;
#endif
	r = zd_iowrite32_locked(chip, 1, CR_AFTER_PNP);
	if (r)
		goto out;

	r = read_fw_regs_offset(chip);
	if (r)
		goto out;

	/* GPI is always disabled, also in the other driver.
	 */
	r = zd_iowrite32_locked(chip, 0, CR_GPI_EN);
	if (r)
		goto out;
	r = zd_iowrite32_locked(chip, CWIN_SIZE, CR_CWMIN_CWMAX);
	if (r)
		goto out;
	/* Currently we support IEEE 802.11g for full and high speed USB.
	 * It might be discussed, whether we should suppport pure b mode for
	 * full speed USB.
	 */
	r = set_mandatory_rates(chip, 1);
	if (r)
		goto out;
	/* Disabling interrupts is certainly a smart thing here.
	 */
	r = disable_hwint(chip);
	if (r)
		goto out;
	r = read_pod(chip, &rf_type);
	if (r)
		goto out;
	r = hw_init(chip);
	if (r)
		goto out;
	r = zd_rf_init_hw(&chip->rf, rf_type);
	if (r)
		goto out;

	r = print_fw_version(chip);
	if (r)
		goto out;

#ifdef DEBUG
	dump_fw_registers(chip);
	r = test_init(chip);
	if (r)
		goto out;
#endif /* DEBUG */

	r = read_cal_int_tables(chip);
	if (r)
		goto out;

	print_id(chip);
out:
	mutex_unlock(&chip->mutex);
	return r;
}

static int update_pwr_int(struct zd_chip *chip, u8 channel)
{
	u8 value = chip->pwr_int_values[channel - 1];
	return zd_iowrite16_locked(chip, value, CR31);
}

static int update_pwr_cal(struct zd_chip *chip, u8 channel)
{
	u8 value = chip->pwr_cal_values[channel-1];
	return zd_iowrite16_locked(chip, value, CR68);
}

static int update_ofdm_cal(struct zd_chip *chip, u8 channel)
{
	struct zd_ioreq16 ioreqs[3];

	ioreqs[0].addr = CR67;
	ioreqs[0].value = chip->ofdm_cal_values[OFDM_36M_INDEX][channel-1];
	ioreqs[1].addr = CR66;
	ioreqs[1].value = chip->ofdm_cal_values[OFDM_48M_INDEX][channel-1];
	ioreqs[2].addr = CR65;
	ioreqs[2].value = chip->ofdm_cal_values[OFDM_54M_INDEX][channel-1];

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int update_channel_integration_and_calibration(struct zd_chip *chip,
	                                              u8 channel)
{
	int r;

	if (!zd_rf_should_update_pwr_int(&chip->rf))
		return 0;

	r = update_pwr_int(chip, channel);
	if (r)
		return r;
	if (zd_chip_is_zd1211b(chip)) {
		static const struct zd_ioreq16 ioreqs[] = {
			{ CR69, 0x28 },
			{},
			{ CR69, 0x2a },
		};

		r = update_ofdm_cal(chip, channel);
		if (r)
			return r;
		r = update_pwr_cal(chip, channel);
		if (r)
			return r;
		r = zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
		if (r)
			return r;
	}

	return 0;
}

/* The CCK baseband gain can be optionally patched by the EEPROM */
static int patch_cck_gain(struct zd_chip *chip)
{
	int r;
	u32 value;

	if (!chip->patch_cck_gain || !zd_rf_should_patch_cck_gain(&chip->rf))
		return 0;

	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	r = zd_ioread32_locked(chip, &value, E2P_PHY_REG);
	if (r)
		return r;
	dev_dbg_f(zd_chip_dev(chip), "patching value %x\n", value & 0xff);
	return zd_iowrite16_locked(chip, value & 0xff, CR47);
}

int zd_chip_set_channel(struct zd_chip *chip, u8 channel)
{
	int r, t;

	mutex_lock(&chip->mutex);
	r = zd_chip_lock_phy_regs(chip);
	if (r)
		goto out;
	r = zd_rf_set_channel(&chip->rf, channel);
	if (r)
		goto unlock;
	r = update_channel_integration_and_calibration(chip, channel);
	if (r)
		goto unlock;
	r = patch_cck_gain(chip);
	if (r)
		goto unlock;
	r = patch_6m_band_edge(chip, channel);
	if (r)
		goto unlock;
	r = zd_iowrite32_locked(chip, 0, CR_CONFIG_PHILIPS);
unlock:
	t = zd_chip_unlock_phy_regs(chip);
	if (t && !r)
		r = t;
out:
	mutex_unlock(&chip->mutex);
	return r;
}

u8 zd_chip_get_channel(struct zd_chip *chip)
{
	u8 channel;

	mutex_lock(&chip->mutex);
	channel = chip->rf.channel;
	mutex_unlock(&chip->mutex);
	return channel;
}

int zd_chip_control_leds(struct zd_chip *chip, enum led_status status)
{
	const zd_addr_t a[] = {
		fw_reg_addr(chip, FW_REG_LED_LINK_STATUS),
		CR_LED,
	};

	int r;
	u16 v[ARRAY_SIZE(a)];
	struct zd_ioreq16 ioreqs[ARRAY_SIZE(a)] = {
		[0] = { fw_reg_addr(chip, FW_REG_LED_LINK_STATUS) },
		[1] = { CR_LED },
	};
	u16 other_led;

	mutex_lock(&chip->mutex);
	r = zd_ioread16v_locked(chip, v, (const zd_addr_t *)a, ARRAY_SIZE(a));
	if (r)
		goto out;

	other_led = chip->link_led == LED1 ? LED2 : LED1;

	switch (status) {
	case ZD_LED_OFF:
		ioreqs[0].value = FW_LINK_OFF;
		ioreqs[1].value = v[1] & ~(LED1|LED2);
		break;
	case ZD_LED_SCANNING:
		ioreqs[0].value = FW_LINK_OFF;
		ioreqs[1].value = v[1] & ~other_led;
		if (get_seconds() % 3 == 0) {
			ioreqs[1].value &= ~chip->link_led;
		} else {
			ioreqs[1].value |= chip->link_led;
		}
		break;
	case ZD_LED_ASSOCIATED:
		ioreqs[0].value = FW_LINK_TX;
		ioreqs[1].value = v[1] & ~other_led;
		ioreqs[1].value |= chip->link_led;
		break;
	default:
		r = -EINVAL;
		goto out;
	}

	if (v[0] != ioreqs[0].value || v[1] != ioreqs[1].value) {
		r = zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
		if (r)
			goto out;
	}
	r = 0;
out:
	mutex_unlock(&chip->mutex);
	return r;
}

int zd_chip_set_basic_rates(struct zd_chip *chip, u16 cr_rates)
{
	int r;

	if (cr_rates & ~(CR_RATES_80211B|CR_RATES_80211G))
		return -EINVAL;

	mutex_lock(&chip->mutex);
	r = zd_iowrite32_locked(chip, cr_rates, CR_BASIC_RATE_TBL);
	mutex_unlock(&chip->mutex);
	return r;
}

static int ofdm_qual_db(u8 status_quality, u8 zd_rate, unsigned int size)
{
	static const u16 constants[] = {
		715, 655, 585, 540, 470, 410, 360, 315,
		270, 235, 205, 175, 150, 125, 105,  85,
		 65,  50,  40,  25,  15
	};

	int i;
	u32 x;

	/* It seems that their quality parameter is somehow per signal
	 * and is now transferred per bit.
	 */
	switch (zd_rate) {
	case ZD_OFDM_RATE_6M:
	case ZD_OFDM_RATE_12M:
	case ZD_OFDM_RATE_24M:
		size *= 2;
		break;
	case ZD_OFDM_RATE_9M:
	case ZD_OFDM_RATE_18M:
	case ZD_OFDM_RATE_36M:
	case ZD_OFDM_RATE_54M:
		size *= 4;
		size /= 3;
		break;
	case ZD_OFDM_RATE_48M:
		size *= 3;
		size /= 2;
		break;
	default:
		return -EINVAL;
	}

	x = (10000 * status_quality)/size;
	for (i = 0; i < ARRAY_SIZE(constants); i++) {
		if (x > constants[i])
			break;
	}

	switch (zd_rate) {
	case ZD_OFDM_RATE_6M:
	case ZD_OFDM_RATE_9M:
		i += 3;
		break;
	case ZD_OFDM_RATE_12M:
	case ZD_OFDM_RATE_18M:
		i += 5;
		break;
	case ZD_OFDM_RATE_24M:
	case ZD_OFDM_RATE_36M:
		i += 9;
		break;
	case ZD_OFDM_RATE_48M:
	case ZD_OFDM_RATE_54M:
		i += 15;
		break;
	default:
		return -EINVAL;
	}

	return i;
}

static int ofdm_qual_percent(u8 status_quality, u8 zd_rate, unsigned int size)
{
	int r;

	r = ofdm_qual_db(status_quality, zd_rate, size);
	ZD_ASSERT(r >= 0);
	if (r < 0)
		r = 0;

	r = (r * 100)/29;
	return r <= 100 ? r : 100;
}

static unsigned int log10times100(unsigned int x)
{
	static const u8 log10[] = {
		  0,
		  0,   30,   47,   60,   69,   77,   84,   90,   95,  100,
		104,  107,  111,  114,  117,  120,  123,  125,  127,  130,
		132,  134,  136,  138,  139,  141,  143,  144,  146,  147,
		149,  150,  151,  153,  154,  155,  156,  157,  159,  160,
		161,  162,  163,  164,  165,  166,  167,  168,  169,  169,
		170,  171,  172,  173,  174,  174,  175,  176,  177,  177,
		178,  179,  179,  180,  181,  181,  182,  183,  183,  184,
		185,  185,  186,  186,  187,  188,  188,  189,  189,  190,
		190,  191,  191,  192,  192,  193,  193,  194,  194,  195,
		195,  196,  196,  197,  197,  198,  198,  199,  199,  200,
		200,  200,  201,  201,  202,  202,  202,  203,  203,  204,
		204,  204,  205,  205,  206,  206,  206,  207,  207,  207,
		208,  208,  208,  209,  209,  210,  210,  210,  211,  211,
		211,  212,  212,  212,  213,  213,  213,  213,  214,  214,
		214,  215,  215,  215,  216,  216,  216,  217,  217,  217,
		217,  218,  218,  218,  219,  219,  219,  219,  220,  220,
		220,  220,  221,  221,  221,  222,  222,  222,  222,  223,
		223,  223,  223,  224,  224,  224,  224,
	};

	return x < ARRAY_SIZE(log10) ? log10[x] : 225;
}

enum {
	MAX_CCK_EVM_DB = 45,
};

static int cck_evm_db(u8 status_quality)
{
	return (20 * log10times100(status_quality)) / 100;
}

static int cck_snr_db(u8 status_quality)
{
	int r = MAX_CCK_EVM_DB - cck_evm_db(status_quality);
	ZD_ASSERT(r >= 0);
	return r;
}

static int cck_qual_percent(u8 status_quality)
{
	int r;

	r = cck_snr_db(status_quality);
	r = (100*r)/17;
	return r <= 100 ? r : 100;
}

static inline u8 zd_rate_from_ofdm_plcp_header(const void *rx_frame)
{
	return ZD_OFDM | zd_ofdm_plcp_header_rate(rx_frame);
}

u8 zd_rx_qual_percent(const void *rx_frame, unsigned int size,
	              const struct rx_status *status)
{
	return (status->frame_status&ZD_RX_OFDM) ?
		ofdm_qual_percent(status->signal_quality_ofdm,
				  zd_rate_from_ofdm_plcp_header(rx_frame),
			          size) :
		cck_qual_percent(status->signal_quality_cck);
}

/**
 * zd_rx_rate - report zd-rate
 * @rx_frame - received frame
 * @rx_status - rx_status as given by the device
 *
 * This function converts the rate as encoded in the received packet to the
 * zd-rate, we are using on other places in the driver.
 */
u8 zd_rx_rate(const void *rx_frame, const struct rx_status *status)
{
	u8 zd_rate;
	if (status->frame_status & ZD_RX_OFDM) {
		zd_rate = zd_rate_from_ofdm_plcp_header(rx_frame);
	} else {
		switch (zd_cck_plcp_header_signal(rx_frame)) {
		case ZD_CCK_PLCP_SIGNAL_1M:
			zd_rate = ZD_CCK_RATE_1M;
			break;
		case ZD_CCK_PLCP_SIGNAL_2M:
			zd_rate = ZD_CCK_RATE_2M;
			break;
		case ZD_CCK_PLCP_SIGNAL_5M5:
			zd_rate = ZD_CCK_RATE_5_5M;
			break;
		case ZD_CCK_PLCP_SIGNAL_11M:
			zd_rate = ZD_CCK_RATE_11M;
			break;
		default:
			zd_rate = 0;
		}
	}

	return zd_rate;
}

int zd_chip_switch_radio_on(struct zd_chip *chip)
{
	int r;

	mutex_lock(&chip->mutex);
	r = zd_switch_radio_on(&chip->rf);
	mutex_unlock(&chip->mutex);
	return r;
}

int zd_chip_switch_radio_off(struct zd_chip *chip)
{
	int r;

	mutex_lock(&chip->mutex);
	r = zd_switch_radio_off(&chip->rf);
	mutex_unlock(&chip->mutex);
	return r;
}

int zd_chip_enable_int(struct zd_chip *chip)
{
	int r;

	mutex_lock(&chip->mutex);
	r = zd_usb_enable_int(&chip->usb);
	mutex_unlock(&chip->mutex);
	return r;
}

void zd_chip_disable_int(struct zd_chip *chip)
{
	mutex_lock(&chip->mutex);
	zd_usb_disable_int(&chip->usb);
	mutex_unlock(&chip->mutex);
}

int zd_chip_enable_rxtx(struct zd_chip *chip)
{
	int r;

	mutex_lock(&chip->mutex);
	zd_usb_enable_tx(&chip->usb);
	r = zd_usb_enable_rx(&chip->usb);
	mutex_unlock(&chip->mutex);
	return r;
}

void zd_chip_disable_rxtx(struct zd_chip *chip)
{
	mutex_lock(&chip->mutex);
	zd_usb_disable_rx(&chip->usb);
	zd_usb_disable_tx(&chip->usb);
	mutex_unlock(&chip->mutex);
}

int zd_rfwritev_locked(struct zd_chip *chip,
	               const u32* values, unsigned int count, u8 bits)
{
	int r;
	unsigned int i;

	for (i = 0; i < count; i++) {
		r = zd_rfwrite_locked(chip, values[i], bits);
		if (r)
			return r;
	}

	return 0;
}

/*
 * We can optionally program the RF directly through CR regs, if supported by
 * the hardware. This is much faster than the older method.
 */
int zd_rfwrite_cr_locked(struct zd_chip *chip, u32 value)
{
	struct zd_ioreq16 ioreqs[] = {
		{ CR244, (value >> 16) & 0xff },
		{ CR243, (value >>  8) & 0xff },
		{ CR242,  value        & 0xff },
	};
	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

int zd_rfwritev_cr_locked(struct zd_chip *chip,
	                  const u32 *values, unsigned int count)
{
	int r;
	unsigned int i;

	for (i = 0; i < count; i++) {
		r = zd_rfwrite_cr_locked(chip, values[i]);
		if (r)
			return r;
	}

	return 0;
}

int zd_chip_set_multicast_hash(struct zd_chip *chip,
	                       struct zd_mc_hash *hash)
{
	struct zd_ioreq32 ioreqs[] = {
		{ CR_GROUP_HASH_P1, hash->low },
		{ CR_GROUP_HASH_P2, hash->high },
	};

	return zd_iowrite32a(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

u64 zd_chip_get_tsf(struct zd_chip *chip)
{
	int r;
	static const zd_addr_t aw_pt_bi_addr[] =
		{ CR_TSF_LOW_PART, CR_TSF_HIGH_PART };
	u32 values[2];
	u64 tsf;

	mutex_lock(&chip->mutex);
	r = zd_ioread32v_locked(chip, values, (const zd_addr_t *)aw_pt_bi_addr,
	                        ARRAY_SIZE(aw_pt_bi_addr));
	mutex_unlock(&chip->mutex);
	if (r)
		return 0;

	tsf = values[1];
	tsf = (tsf << 32) | values[0];

	return tsf;
}
