/*

  Broadcom BCM43xx wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
                     Stefano Brivio <st3@riseup.net>
                     Michael Buesch <mbuesch@freenet.de>
                     Danny van Dyk <kugelfang@gentoo.org>
                     Andreas Jaggi <andreas.jaggi@waterwave.ch>

  Some parts of the code in this file are derived from the ipw2200
  driver  Copyright(c) 2003 - 2004 Intel Corporation.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include <linux/delay.h>

#include "bcm43xx.h"
#include "bcm43xx_power.h"
#include "bcm43xx_main.h"


/* Get the Slow Clock Source */
static int bcm43xx_pctl_get_slowclksrc(struct bcm43xx_private *bcm)
{
	u32 tmp;
	int err;

	assert(bcm->current_core == &bcm->core_chipcommon);
	if (bcm->current_core->rev < 6) {
		if (bcm->bustype == BCM43xx_BUSTYPE_PCMCIA ||
		    bcm->bustype == BCM43xx_BUSTYPE_SB)
			return BCM43xx_PCTL_CLKSRC_XTALOS;
		if (bcm->bustype == BCM43xx_BUSTYPE_PCI) {
			err = bcm43xx_pci_read_config32(bcm, BCM43xx_PCTL_OUT, &tmp);
			assert(!err);
			if (tmp & 0x10)
				return BCM43xx_PCTL_CLKSRC_PCI;
			return BCM43xx_PCTL_CLKSRC_XTALOS;
		}
	}
	if (bcm->current_core->rev < 10) {
		tmp = bcm43xx_read32(bcm, BCM43xx_CHIPCOMMON_SLOWCLKCTL);
		tmp &= 0x7;
		if (tmp == 0)
			return BCM43xx_PCTL_CLKSRC_LOPWROS;
		if (tmp == 1)
			return BCM43xx_PCTL_CLKSRC_XTALOS;
		if (tmp == 2)
			return BCM43xx_PCTL_CLKSRC_PCI;
	}

	return BCM43xx_PCTL_CLKSRC_XTALOS;
}

/* Get max/min slowclock frequency
 * as described in http://bcm-specs.sipsolutions.net/PowerControl
 */
static int bcm43xx_pctl_clockfreqlimit(struct bcm43xx_private *bcm,
				       int get_max)
{
	int limit;
	int clocksrc;
	int divisor;
	u32 tmp;

	assert(bcm->chipcommon_capabilities & BCM43xx_CAPABILITIES_PCTL);
	assert(bcm->current_core == &bcm->core_chipcommon);

	clocksrc = bcm43xx_pctl_get_slowclksrc(bcm);
	if (bcm->current_core->rev < 6) {
		switch (clocksrc) {
		case BCM43xx_PCTL_CLKSRC_PCI:
			divisor = 64;
			break;
		case BCM43xx_PCTL_CLKSRC_XTALOS:
			divisor = 32;
			break;
		default:
			assert(0);
			divisor = 1;
		}
	} else if (bcm->current_core->rev < 10) {
		switch (clocksrc) {
		case BCM43xx_PCTL_CLKSRC_LOPWROS:
			divisor = 1;
			break;
		case BCM43xx_PCTL_CLKSRC_XTALOS:
		case BCM43xx_PCTL_CLKSRC_PCI:
			tmp = bcm43xx_read32(bcm, BCM43xx_CHIPCOMMON_SLOWCLKCTL);
			divisor = ((tmp & 0xFFFF0000) >> 16) + 1;
			divisor *= 4;
			break;
		default:
			assert(0);
			divisor = 1;
		}
	} else {
		tmp = bcm43xx_read32(bcm, BCM43xx_CHIPCOMMON_SYSCLKCTL);
		divisor = ((tmp & 0xFFFF0000) >> 16) + 1;
		divisor *= 4;
	}

	switch (clocksrc) {
	case BCM43xx_PCTL_CLKSRC_LOPWROS:
		if (get_max)
			limit = 43000;
		else
			limit = 25000;
		break;
	case BCM43xx_PCTL_CLKSRC_XTALOS:
		if (get_max)
			limit = 20200000;
		else
			limit = 19800000;
		break;
	case BCM43xx_PCTL_CLKSRC_PCI:
		if (get_max)
			limit = 34000000;
		else
			limit = 25000000;
		break;
	default:
		assert(0);
		limit = 0;
	}
	limit /= divisor;

	return limit;
}


/* init power control
 * as described in http://bcm-specs.sipsolutions.net/PowerControl
 */
int bcm43xx_pctl_init(struct bcm43xx_private *bcm)
{
	int err, maxfreq;
	struct bcm43xx_coreinfo *old_core;

	if (!(bcm->chipcommon_capabilities & BCM43xx_CAPABILITIES_PCTL))
		return 0;
	old_core = bcm->current_core;
	err = bcm43xx_switch_core(bcm, &bcm->core_chipcommon);
	if (err == -ENODEV)
		return 0;
	if (err)
		goto out;

	maxfreq = bcm43xx_pctl_clockfreqlimit(bcm, 1);
	bcm43xx_write32(bcm, BCM43xx_CHIPCOMMON_PLLONDELAY,
			(maxfreq * 150 + 999999) / 1000000);
	bcm43xx_write32(bcm, BCM43xx_CHIPCOMMON_FREFSELDELAY,
			(maxfreq * 15 + 999999) / 1000000);

	err = bcm43xx_switch_core(bcm, old_core);
	assert(err == 0);

out:
	return err;
}

u16 bcm43xx_pctl_powerup_delay(struct bcm43xx_private *bcm)
{
	u16 delay = 0;
	int err;
	u32 pll_on_delay;
	struct bcm43xx_coreinfo *old_core;
	int minfreq;

	if (bcm->bustype != BCM43xx_BUSTYPE_PCI)
		goto out;
	if (!(bcm->chipcommon_capabilities & BCM43xx_CAPABILITIES_PCTL))
		goto out;
	old_core = bcm->current_core;
	err = bcm43xx_switch_core(bcm, &bcm->core_chipcommon);
	if (err == -ENODEV)
		goto out;

	minfreq = bcm43xx_pctl_clockfreqlimit(bcm, 0);
	pll_on_delay = bcm43xx_read32(bcm, BCM43xx_CHIPCOMMON_PLLONDELAY);
	delay = (((pll_on_delay + 2) * 1000000) + (minfreq - 1)) / minfreq;

	err = bcm43xx_switch_core(bcm, old_core);
	assert(err == 0);

out:
	return delay;
}

/* set the powercontrol clock
 * as described in http://bcm-specs.sipsolutions.net/PowerControl
 */
int bcm43xx_pctl_set_clock(struct bcm43xx_private *bcm, u16 mode)
{
	int err;
	struct bcm43xx_coreinfo *old_core;
	u32 tmp;

	old_core = bcm->current_core;
	err = bcm43xx_switch_core(bcm, &bcm->core_chipcommon);
	if (err == -ENODEV)
		return 0;
	if (err)
		goto out;
	
	if (bcm->core_chipcommon.rev < 6) {
		if (mode == BCM43xx_PCTL_CLK_FAST) {
			err = bcm43xx_pctl_set_crystal(bcm, 1);
			if (err)
				goto out;
		}
	} else {
		if ((bcm->chipcommon_capabilities & BCM43xx_CAPABILITIES_PCTL) &&
			(bcm->core_chipcommon.rev < 10)) {
			switch (mode) {
			case BCM43xx_PCTL_CLK_FAST:
				tmp = bcm43xx_read32(bcm, BCM43xx_CHIPCOMMON_SLOWCLKCTL);
				tmp = (tmp & ~BCM43xx_PCTL_FORCE_SLOW) | BCM43xx_PCTL_FORCE_PLL;
				bcm43xx_write32(bcm, BCM43xx_CHIPCOMMON_SLOWCLKCTL, tmp);
				break;
			case BCM43xx_PCTL_CLK_SLOW:
				tmp = bcm43xx_read32(bcm, BCM43xx_CHIPCOMMON_SLOWCLKCTL);
				tmp |= BCM43xx_PCTL_FORCE_SLOW;
				bcm43xx_write32(bcm, BCM43xx_CHIPCOMMON_SLOWCLKCTL, tmp);
				break;
			case BCM43xx_PCTL_CLK_DYNAMIC:
				tmp = bcm43xx_read32(bcm, BCM43xx_CHIPCOMMON_SLOWCLKCTL);
				tmp &= ~BCM43xx_PCTL_FORCE_SLOW;
				tmp |= BCM43xx_PCTL_FORCE_PLL;
				tmp &= ~BCM43xx_PCTL_DYN_XTAL;
				bcm43xx_write32(bcm, BCM43xx_CHIPCOMMON_SLOWCLKCTL, tmp);
			}
		}
	}
	
	err = bcm43xx_switch_core(bcm, old_core);
	assert(err == 0);

out:
	return err;
}

int bcm43xx_pctl_set_crystal(struct bcm43xx_private *bcm, int on)
{
	int err;
	u32 in, out, outenable;

	err = bcm43xx_pci_read_config32(bcm, BCM43xx_PCTL_IN, &in);
	if (err)
		goto err_pci;
	err = bcm43xx_pci_read_config32(bcm, BCM43xx_PCTL_OUT, &out);
	if (err)
		goto err_pci;
	err = bcm43xx_pci_read_config32(bcm, BCM43xx_PCTL_OUTENABLE, &outenable);
	if (err)
		goto err_pci;

	outenable |= (BCM43xx_PCTL_XTAL_POWERUP | BCM43xx_PCTL_PLL_POWERDOWN);

	if (on) {
		if (in & 0x40)
			return 0;

		out |= (BCM43xx_PCTL_XTAL_POWERUP | BCM43xx_PCTL_PLL_POWERDOWN);

		err = bcm43xx_pci_write_config32(bcm, BCM43xx_PCTL_OUT, out);
		if (err)
			goto err_pci;
		err = bcm43xx_pci_write_config32(bcm, BCM43xx_PCTL_OUTENABLE, outenable);
		if (err)
			goto err_pci;
		udelay(1000);

		out &= ~BCM43xx_PCTL_PLL_POWERDOWN;
		err = bcm43xx_pci_write_config32(bcm, BCM43xx_PCTL_OUT, out);
		if (err)
			goto err_pci;
		udelay(5000);
	} else {
		if (bcm->current_core->rev < 5)
			return 0;
		if (bcm->sprom.boardflags & BCM43xx_BFL_XTAL_NOSLOW)
			return 0;

/*		XXX: Why BCM43xx_MMIO_RADIO_HWENABLED_xx can't be read at this time?
 *		err = bcm43xx_switch_core(bcm, bcm->active_80211_core);
 *		if (err)
 *			return err;
 *		if (((bcm->current_core->rev >= 3) &&
 *			(bcm43xx_read32(bcm, BCM43xx_MMIO_RADIO_HWENABLED_HI) & (1 << 16))) ||
 *		      ((bcm->current_core->rev < 3) &&
 *			!(bcm43xx_read16(bcm, BCM43xx_MMIO_RADIO_HWENABLED_LO) & (1 << 4))))
 *			return 0;
 *		err = bcm43xx_switch_core(bcm, &bcm->core_chipcommon);
 *		if (err)
 *			return err;
 */
		
		err = bcm43xx_pctl_set_clock(bcm, BCM43xx_PCTL_CLK_SLOW);
		if (err)
			goto out;
		out &= ~BCM43xx_PCTL_XTAL_POWERUP;
		out |= BCM43xx_PCTL_PLL_POWERDOWN;
		err = bcm43xx_pci_write_config32(bcm, BCM43xx_PCTL_OUT, out);
		if (err)
			goto err_pci;
		err = bcm43xx_pci_write_config32(bcm, BCM43xx_PCTL_OUTENABLE, outenable);
		if (err)
			goto err_pci;
	}

out:
	return err;

err_pci:
	printk(KERN_ERR PFX "Error: pctl_set_clock() could not access PCI config space!\n");
	err = -EBUSY;
	goto out;
}

/* Set the PowerSavingControlBits.
 * Bitvalues:
 *   0  => unset the bit
 *   1  => set the bit
 *   -1 => calculate the bit
 */
void bcm43xx_power_saving_ctl_bits(struct bcm43xx_private *bcm,
				   int bit25, int bit26)
{
	int i;
	u32 status;

//FIXME: Force 25 to off and 26 to on for now:
bit25 = 0;
bit26 = 1;

	if (bit25 == -1) {
		//TODO: If powersave is not off and FIXME is not set and we are not in adhoc
		//	and thus is not an AP and we are associated, set bit 25
	}
	if (bit26 == -1) {
		//TODO: If the device is awake or this is an AP, or we are scanning, or FIXME,
		//	or we are associated, or FIXME, or the latest PS-Poll packet sent was
		//	successful, set bit26
	}
	status = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	if (bit25)
		status |= BCM43xx_SBF_PS1;
	else
		status &= ~BCM43xx_SBF_PS1;
	if (bit26)
		status |= BCM43xx_SBF_PS2;
	else
		status &= ~BCM43xx_SBF_PS2;
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, status);
	if (bit26 && bcm->current_core->rev >= 5) {
		for (i = 0; i < 100; i++) {
			if (bcm43xx_shm_read32(bcm, BCM43xx_SHM_SHARED, 0x0040) != 4)
				break;
			udelay(10);
		}
	}
}
