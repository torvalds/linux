/*
 * Sonics Silicon Backplane PCI-Hostbus related functions.
 *
 * Copyright (C) 2005-2006 Michael Buesch <mb@bu3sch.de>
 * Copyright (C) 2005 Martin Langer <martin-langer@gmx.de>
 * Copyright (C) 2005 Stefano Brivio <st3@riseup.net>
 * Copyright (C) 2005 Danny van Dyk <kugelfang@gentoo.org>
 * Copyright (C) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
 *
 * Derived from the Broadcom 4400 device driver.
 * Copyright (C) 2002 David S. Miller (davem@redhat.com)
 * Fixed by Pekka Pietikainen (pp@ee.oulu.fi)
 * Copyright (C) 2006 Broadcom Corporation.
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/ssb/ssb.h>
#include <linux/ssb/ssb_regs.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "ssb_private.h"


/* Define the following to 1 to enable a printk on each coreswitch. */
#define SSB_VERBOSE_PCICORESWITCH_DEBUG		0


/* Lowlevel coreswitching */
int ssb_pci_switch_coreidx(struct ssb_bus *bus, u8 coreidx)
{
	int err;
	int attempts = 0;
	u32 cur_core;

	while (1) {
		err = pci_write_config_dword(bus->host_pci, SSB_BAR0_WIN,
					     (coreidx * SSB_CORE_SIZE)
					     + SSB_ENUM_BASE);
		if (err)
			goto error;
		err = pci_read_config_dword(bus->host_pci, SSB_BAR0_WIN,
					    &cur_core);
		if (err)
			goto error;
		cur_core = (cur_core - SSB_ENUM_BASE)
			   / SSB_CORE_SIZE;
		if (cur_core == coreidx)
			break;

		if (attempts++ > SSB_BAR0_MAX_RETRIES)
			goto error;
		udelay(10);
	}
	return 0;
error:
	ssb_printk(KERN_ERR PFX "Failed to switch to core %u\n", coreidx);
	return -ENODEV;
}

int ssb_pci_switch_core(struct ssb_bus *bus,
			struct ssb_device *dev)
{
	int err;
	unsigned long flags;

#if SSB_VERBOSE_PCICORESWITCH_DEBUG
	ssb_printk(KERN_INFO PFX
		   "Switching to %s core, index %d\n",
		   ssb_core_name(dev->id.coreid),
		   dev->core_index);
#endif

	spin_lock_irqsave(&bus->bar_lock, flags);
	err = ssb_pci_switch_coreidx(bus, dev->core_index);
	if (!err)
		bus->mapped_device = dev;
	spin_unlock_irqrestore(&bus->bar_lock, flags);

	return err;
}

/* Enable/disable the on board crystal oscillator and/or PLL. */
int ssb_pci_xtal(struct ssb_bus *bus, u32 what, int turn_on)
{
	int err;
	u32 in, out, outenable;
	u16 pci_status;

	if (bus->bustype != SSB_BUSTYPE_PCI)
		return 0;

	err = pci_read_config_dword(bus->host_pci, SSB_GPIO_IN, &in);
	if (err)
		goto err_pci;
	err = pci_read_config_dword(bus->host_pci, SSB_GPIO_OUT, &out);
	if (err)
		goto err_pci;
	err = pci_read_config_dword(bus->host_pci, SSB_GPIO_OUT_ENABLE, &outenable);
	if (err)
		goto err_pci;

	outenable |= what;

	if (turn_on) {
		/* Avoid glitching the clock if GPRS is already using it.
		 * We can't actually read the state of the PLLPD so we infer it
		 * by the value of XTAL_PU which *is* readable via gpioin.
		 */
		if (!(in & SSB_GPIO_XTAL)) {
			if (what & SSB_GPIO_XTAL) {
				/* Turn the crystal on */
				out |= SSB_GPIO_XTAL;
				if (what & SSB_GPIO_PLL)
					out |= SSB_GPIO_PLL;
				err = pci_write_config_dword(bus->host_pci, SSB_GPIO_OUT, out);
				if (err)
					goto err_pci;
				err = pci_write_config_dword(bus->host_pci, SSB_GPIO_OUT_ENABLE,
							     outenable);
				if (err)
					goto err_pci;
				msleep(1);
			}
			if (what & SSB_GPIO_PLL) {
				/* Turn the PLL on */
				out &= ~SSB_GPIO_PLL;
				err = pci_write_config_dword(bus->host_pci, SSB_GPIO_OUT, out);
				if (err)
					goto err_pci;
				msleep(5);
			}
		}

		err = pci_read_config_word(bus->host_pci, PCI_STATUS, &pci_status);
		if (err)
			goto err_pci;
		pci_status &= ~PCI_STATUS_SIG_TARGET_ABORT;
		err = pci_write_config_word(bus->host_pci, PCI_STATUS, pci_status);
		if (err)
			goto err_pci;
	} else {
		if (what & SSB_GPIO_XTAL) {
			/* Turn the crystal off */
			out &= ~SSB_GPIO_XTAL;
		}
		if (what & SSB_GPIO_PLL) {
			/* Turn the PLL off */
			out |= SSB_GPIO_PLL;
		}
		err = pci_write_config_dword(bus->host_pci, SSB_GPIO_OUT, out);
		if (err)
			goto err_pci;
		err = pci_write_config_dword(bus->host_pci, SSB_GPIO_OUT_ENABLE, outenable);
		if (err)
			goto err_pci;
	}

out:
	return err;

err_pci:
	printk(KERN_ERR PFX "Error: ssb_pci_xtal() could not access PCI config space!\n");
	err = -EBUSY;
	goto out;
}

/* Get the word-offset for a SSB_SPROM_XXX define. */
#define SPOFF(offset)	(((offset) - SSB_SPROM_BASE) / sizeof(u16))
/* Helper to extract some _offset, which is one of the SSB_SPROM_XXX defines. */
#define SPEX(_outvar, _offset, _mask, _shift)	\
	out->_outvar = ((in[SPOFF(_offset)] & (_mask)) >> (_shift))

static inline u8 ssb_crc8(u8 crc, u8 data)
{
	/* Polynomial:   x^8 + x^7 + x^6 + x^4 + x^2 + 1   */
	static const u8 t[] = {
		0x00, 0xF7, 0xB9, 0x4E, 0x25, 0xD2, 0x9C, 0x6B,
		0x4A, 0xBD, 0xF3, 0x04, 0x6F, 0x98, 0xD6, 0x21,
		0x94, 0x63, 0x2D, 0xDA, 0xB1, 0x46, 0x08, 0xFF,
		0xDE, 0x29, 0x67, 0x90, 0xFB, 0x0C, 0x42, 0xB5,
		0x7F, 0x88, 0xC6, 0x31, 0x5A, 0xAD, 0xE3, 0x14,
		0x35, 0xC2, 0x8C, 0x7B, 0x10, 0xE7, 0xA9, 0x5E,
		0xEB, 0x1C, 0x52, 0xA5, 0xCE, 0x39, 0x77, 0x80,
		0xA1, 0x56, 0x18, 0xEF, 0x84, 0x73, 0x3D, 0xCA,
		0xFE, 0x09, 0x47, 0xB0, 0xDB, 0x2C, 0x62, 0x95,
		0xB4, 0x43, 0x0D, 0xFA, 0x91, 0x66, 0x28, 0xDF,
		0x6A, 0x9D, 0xD3, 0x24, 0x4F, 0xB8, 0xF6, 0x01,
		0x20, 0xD7, 0x99, 0x6E, 0x05, 0xF2, 0xBC, 0x4B,
		0x81, 0x76, 0x38, 0xCF, 0xA4, 0x53, 0x1D, 0xEA,
		0xCB, 0x3C, 0x72, 0x85, 0xEE, 0x19, 0x57, 0xA0,
		0x15, 0xE2, 0xAC, 0x5B, 0x30, 0xC7, 0x89, 0x7E,
		0x5F, 0xA8, 0xE6, 0x11, 0x7A, 0x8D, 0xC3, 0x34,
		0xAB, 0x5C, 0x12, 0xE5, 0x8E, 0x79, 0x37, 0xC0,
		0xE1, 0x16, 0x58, 0xAF, 0xC4, 0x33, 0x7D, 0x8A,
		0x3F, 0xC8, 0x86, 0x71, 0x1A, 0xED, 0xA3, 0x54,
		0x75, 0x82, 0xCC, 0x3B, 0x50, 0xA7, 0xE9, 0x1E,
		0xD4, 0x23, 0x6D, 0x9A, 0xF1, 0x06, 0x48, 0xBF,
		0x9E, 0x69, 0x27, 0xD0, 0xBB, 0x4C, 0x02, 0xF5,
		0x40, 0xB7, 0xF9, 0x0E, 0x65, 0x92, 0xDC, 0x2B,
		0x0A, 0xFD, 0xB3, 0x44, 0x2F, 0xD8, 0x96, 0x61,
		0x55, 0xA2, 0xEC, 0x1B, 0x70, 0x87, 0xC9, 0x3E,
		0x1F, 0xE8, 0xA6, 0x51, 0x3A, 0xCD, 0x83, 0x74,
		0xC1, 0x36, 0x78, 0x8F, 0xE4, 0x13, 0x5D, 0xAA,
		0x8B, 0x7C, 0x32, 0xC5, 0xAE, 0x59, 0x17, 0xE0,
		0x2A, 0xDD, 0x93, 0x64, 0x0F, 0xF8, 0xB6, 0x41,
		0x60, 0x97, 0xD9, 0x2E, 0x45, 0xB2, 0xFC, 0x0B,
		0xBE, 0x49, 0x07, 0xF0, 0x9B, 0x6C, 0x22, 0xD5,
		0xF4, 0x03, 0x4D, 0xBA, 0xD1, 0x26, 0x68, 0x9F,
	};
	return t[crc ^ data];
}

static u8 ssb_sprom_crc(const u16 *sprom, u16 size)
{
	int word;
	u8 crc = 0xFF;

	for (word = 0; word < size - 1; word++) {
		crc = ssb_crc8(crc, sprom[word] & 0x00FF);
		crc = ssb_crc8(crc, (sprom[word] & 0xFF00) >> 8);
	}
	crc = ssb_crc8(crc, sprom[size - 1] & 0x00FF);
	crc ^= 0xFF;

	return crc;
}

static int sprom_check_crc(const u16 *sprom, u16 size)
{
	u8 crc;
	u8 expected_crc;
	u16 tmp;

	crc = ssb_sprom_crc(sprom, size);
	tmp = sprom[size - 1] & SSB_SPROM_REVISION_CRC;
	expected_crc = tmp >> SSB_SPROM_REVISION_CRC_SHIFT;
	if (crc != expected_crc)
		return -EPROTO;

	return 0;
}

static void sprom_do_read(struct ssb_bus *bus, u16 *sprom)
{
	int i;

	for (i = 0; i < bus->sprom_size; i++)
		sprom[i] = ioread16(bus->mmio + SSB_SPROM_BASE + (i * 2));
}

static int sprom_do_write(struct ssb_bus *bus, const u16 *sprom)
{
	struct pci_dev *pdev = bus->host_pci;
	int i, err;
	u32 spromctl;
	u16 size = bus->sprom_size;

	ssb_printk(KERN_NOTICE PFX "Writing SPROM. Do NOT turn off the power! Please stand by...\n");
	err = pci_read_config_dword(pdev, SSB_SPROMCTL, &spromctl);
	if (err)
		goto err_ctlreg;
	spromctl |= SSB_SPROMCTL_WE;
	err = pci_write_config_dword(pdev, SSB_SPROMCTL, spromctl);
	if (err)
		goto err_ctlreg;
	ssb_printk(KERN_NOTICE PFX "[ 0%%");
	msleep(500);
	for (i = 0; i < size; i++) {
		if (i == size / 4)
			ssb_printk("25%%");
		else if (i == size / 2)
			ssb_printk("50%%");
		else if (i == (size * 3) / 4)
			ssb_printk("75%%");
		else if (i % 2)
			ssb_printk(".");
		writew(sprom[i], bus->mmio + SSB_SPROM_BASE + (i * 2));
		mmiowb();
		msleep(20);
	}
	err = pci_read_config_dword(pdev, SSB_SPROMCTL, &spromctl);
	if (err)
		goto err_ctlreg;
	spromctl &= ~SSB_SPROMCTL_WE;
	err = pci_write_config_dword(pdev, SSB_SPROMCTL, spromctl);
	if (err)
		goto err_ctlreg;
	msleep(500);
	ssb_printk("100%% ]\n");
	ssb_printk(KERN_NOTICE PFX "SPROM written.\n");

	return 0;
err_ctlreg:
	ssb_printk(KERN_ERR PFX "Could not access SPROM control register.\n");
	return err;
}

static s8 r123_extract_antgain(u8 sprom_revision, const u16 *in,
			       u16 mask, u16 shift)
{
	u16 v;
	u8 gain;

	v = in[SPOFF(SSB_SPROM1_AGAIN)];
	gain = (v & mask) >> shift;
	if (gain == 0xFF)
		gain = 2; /* If unset use 2dBm */
	if (sprom_revision == 1) {
		/* Convert to Q5.2 */
		gain <<= 2;
	} else {
		/* Q5.2 Fractional part is stored in 0xC0 */
		gain = ((gain & 0xC0) >> 6) | ((gain & 0x3F) << 2);
	}

	return (s8)gain;
}

static void sprom_extract_r123(struct ssb_sprom *out, const u16 *in)
{
	int i;
	u16 v;
	s8 gain;
	u16 loc[3];

	if (out->revision == 3) {			/* rev 3 moved MAC */
		loc[0] = SSB_SPROM3_IL0MAC;
		loc[1] = SSB_SPROM3_ET0MAC;
		loc[2] = SSB_SPROM3_ET1MAC;
	} else {
		loc[0] = SSB_SPROM1_IL0MAC;
		loc[1] = SSB_SPROM1_ET0MAC;
		loc[2] = SSB_SPROM1_ET1MAC;
	}
	for (i = 0; i < 3; i++) {
		v = in[SPOFF(loc[0]) + i];
		*(((__be16 *)out->il0mac) + i) = cpu_to_be16(v);
	}
	for (i = 0; i < 3; i++) {
		v = in[SPOFF(loc[1]) + i];
		*(((__be16 *)out->et0mac) + i) = cpu_to_be16(v);
	}
	for (i = 0; i < 3; i++) {
		v = in[SPOFF(loc[2]) + i];
		*(((__be16 *)out->et1mac) + i) = cpu_to_be16(v);
	}
	SPEX(et0phyaddr, SSB_SPROM1_ETHPHY, SSB_SPROM1_ETHPHY_ET0A, 0);
	SPEX(et1phyaddr, SSB_SPROM1_ETHPHY, SSB_SPROM1_ETHPHY_ET1A,
	     SSB_SPROM1_ETHPHY_ET1A_SHIFT);
	SPEX(et0mdcport, SSB_SPROM1_ETHPHY, SSB_SPROM1_ETHPHY_ET0M, 14);
	SPEX(et1mdcport, SSB_SPROM1_ETHPHY, SSB_SPROM1_ETHPHY_ET1M, 15);
	SPEX(board_rev, SSB_SPROM1_BINF, SSB_SPROM1_BINF_BREV, 0);
	SPEX(country_code, SSB_SPROM1_BINF, SSB_SPROM1_BINF_CCODE,
	     SSB_SPROM1_BINF_CCODE_SHIFT);
	SPEX(ant_available_a, SSB_SPROM1_BINF, SSB_SPROM1_BINF_ANTA,
	     SSB_SPROM1_BINF_ANTA_SHIFT);
	SPEX(ant_available_bg, SSB_SPROM1_BINF, SSB_SPROM1_BINF_ANTBG,
	     SSB_SPROM1_BINF_ANTBG_SHIFT);
	SPEX(pa0b0, SSB_SPROM1_PA0B0, 0xFFFF, 0);
	SPEX(pa0b1, SSB_SPROM1_PA0B1, 0xFFFF, 0);
	SPEX(pa0b2, SSB_SPROM1_PA0B2, 0xFFFF, 0);
	SPEX(pa1b0, SSB_SPROM1_PA1B0, 0xFFFF, 0);
	SPEX(pa1b1, SSB_SPROM1_PA1B1, 0xFFFF, 0);
	SPEX(pa1b2, SSB_SPROM1_PA1B2, 0xFFFF, 0);
	SPEX(gpio0, SSB_SPROM1_GPIOA, SSB_SPROM1_GPIOA_P0, 0);
	SPEX(gpio1, SSB_SPROM1_GPIOA, SSB_SPROM1_GPIOA_P1,
	     SSB_SPROM1_GPIOA_P1_SHIFT);
	SPEX(gpio2, SSB_SPROM1_GPIOB, SSB_SPROM1_GPIOB_P2, 0);
	SPEX(gpio3, SSB_SPROM1_GPIOB, SSB_SPROM1_GPIOB_P3,
	     SSB_SPROM1_GPIOB_P3_SHIFT);
	SPEX(maxpwr_a, SSB_SPROM1_MAXPWR, SSB_SPROM1_MAXPWR_A,
	     SSB_SPROM1_MAXPWR_A_SHIFT);
	SPEX(maxpwr_bg, SSB_SPROM1_MAXPWR, SSB_SPROM1_MAXPWR_BG, 0);
	SPEX(itssi_a, SSB_SPROM1_ITSSI, SSB_SPROM1_ITSSI_A,
	     SSB_SPROM1_ITSSI_A_SHIFT);
	SPEX(itssi_bg, SSB_SPROM1_ITSSI, SSB_SPROM1_ITSSI_BG, 0);
	SPEX(boardflags_lo, SSB_SPROM1_BFLLO, 0xFFFF, 0);
	if (out->revision >= 2)
		SPEX(boardflags_hi, SSB_SPROM2_BFLHI, 0xFFFF, 0);

	/* Extract the antenna gain values. */
	gain = r123_extract_antgain(out->revision, in,
				    SSB_SPROM1_AGAIN_BG,
				    SSB_SPROM1_AGAIN_BG_SHIFT);
	out->antenna_gain.ghz24.a0 = gain;
	out->antenna_gain.ghz24.a1 = gain;
	out->antenna_gain.ghz24.a2 = gain;
	out->antenna_gain.ghz24.a3 = gain;
	gain = r123_extract_antgain(out->revision, in,
				    SSB_SPROM1_AGAIN_A,
				    SSB_SPROM1_AGAIN_A_SHIFT);
	out->antenna_gain.ghz5.a0 = gain;
	out->antenna_gain.ghz5.a1 = gain;
	out->antenna_gain.ghz5.a2 = gain;
	out->antenna_gain.ghz5.a3 = gain;
}

static void sprom_extract_r4(struct ssb_sprom *out, const u16 *in)
{
	int i;
	u16 v;

	/* extract the equivalent of the r1 variables */
	for (i = 0; i < 3; i++) {
		v = in[SPOFF(SSB_SPROM4_IL0MAC) + i];
		*(((__be16 *)out->il0mac) + i) = cpu_to_be16(v);
	}
	for (i = 0; i < 3; i++) {
		v = in[SPOFF(SSB_SPROM4_ET0MAC) + i];
		*(((__be16 *)out->et0mac) + i) = cpu_to_be16(v);
	}
	for (i = 0; i < 3; i++) {
		v = in[SPOFF(SSB_SPROM4_ET1MAC) + i];
		*(((__be16 *)out->et1mac) + i) = cpu_to_be16(v);
	}
	SPEX(et0phyaddr, SSB_SPROM4_ETHPHY, SSB_SPROM4_ETHPHY_ET0A, 0);
	SPEX(et1phyaddr, SSB_SPROM4_ETHPHY, SSB_SPROM4_ETHPHY_ET1A,
	     SSB_SPROM4_ETHPHY_ET1A_SHIFT);
	SPEX(country_code, SSB_SPROM4_CCODE, 0xFFFF, 0);
	SPEX(boardflags_lo, SSB_SPROM4_BFLLO, 0xFFFF, 0);
	SPEX(boardflags_hi, SSB_SPROM4_BFLHI, 0xFFFF, 0);
	SPEX(ant_available_a, SSB_SPROM4_ANTAVAIL, SSB_SPROM4_ANTAVAIL_A,
	     SSB_SPROM4_ANTAVAIL_A_SHIFT);
	SPEX(ant_available_bg, SSB_SPROM4_ANTAVAIL, SSB_SPROM4_ANTAVAIL_BG,
	     SSB_SPROM4_ANTAVAIL_BG_SHIFT);
	SPEX(maxpwr_bg, SSB_SPROM4_MAXP_BG, SSB_SPROM4_MAXP_BG_MASK, 0);
	SPEX(itssi_bg, SSB_SPROM4_MAXP_BG, SSB_SPROM4_ITSSI_BG,
	     SSB_SPROM4_ITSSI_BG_SHIFT);
	SPEX(maxpwr_a, SSB_SPROM4_MAXP_A, SSB_SPROM4_MAXP_A_MASK, 0);
	SPEX(itssi_a, SSB_SPROM4_MAXP_A, SSB_SPROM4_ITSSI_A,
	     SSB_SPROM4_ITSSI_A_SHIFT);
	SPEX(gpio0, SSB_SPROM4_GPIOA, SSB_SPROM4_GPIOA_P0, 0);
	SPEX(gpio1, SSB_SPROM4_GPIOA, SSB_SPROM4_GPIOA_P1,
	     SSB_SPROM4_GPIOA_P1_SHIFT);
	SPEX(gpio2, SSB_SPROM4_GPIOB, SSB_SPROM4_GPIOB_P2, 0);
	SPEX(gpio3, SSB_SPROM4_GPIOB, SSB_SPROM4_GPIOB_P3,
	     SSB_SPROM4_GPIOB_P3_SHIFT);

	/* Extract the antenna gain values. */
	SPEX(antenna_gain.ghz24.a0, SSB_SPROM4_AGAIN01,
	     SSB_SPROM4_AGAIN0, SSB_SPROM4_AGAIN0_SHIFT);
	SPEX(antenna_gain.ghz24.a1, SSB_SPROM4_AGAIN01,
	     SSB_SPROM4_AGAIN1, SSB_SPROM4_AGAIN1_SHIFT);
	SPEX(antenna_gain.ghz24.a2, SSB_SPROM4_AGAIN23,
	     SSB_SPROM4_AGAIN2, SSB_SPROM4_AGAIN2_SHIFT);
	SPEX(antenna_gain.ghz24.a3, SSB_SPROM4_AGAIN23,
	     SSB_SPROM4_AGAIN3, SSB_SPROM4_AGAIN3_SHIFT);
	memcpy(&out->antenna_gain.ghz5, &out->antenna_gain.ghz24,
	       sizeof(out->antenna_gain.ghz5));

	/* TODO - get remaining rev 4 stuff needed */
}

static int sprom_extract(struct ssb_bus *bus, struct ssb_sprom *out,
			 const u16 *in, u16 size)
{
	memset(out, 0, sizeof(*out));

	out->revision = in[size - 1] & 0x00FF;
	ssb_dprintk(KERN_DEBUG PFX "SPROM revision %d detected.\n", out->revision);
	if ((bus->chip_id & 0xFF00) == 0x4400) {
		/* Workaround: The BCM44XX chip has a stupid revision
		 * number stored in the SPROM.
		 * Always extract r1. */
		out->revision = 1;
		sprom_extract_r123(out, in);
	} else if (bus->chip_id == 0x4321) {
		/* the BCM4328 has a chipid == 0x4321 and a rev 4 SPROM */
		out->revision = 4;
		sprom_extract_r4(out, in);
	} else {
		if (out->revision == 0)
			goto unsupported;
		if (out->revision >= 1 && out->revision <= 3) {
			sprom_extract_r123(out, in);
		}
		if (out->revision == 4)
			sprom_extract_r4(out, in);
		if (out->revision >= 5)
			goto unsupported;
	}

	return 0;
unsupported:
	ssb_printk(KERN_WARNING PFX "Unsupported SPROM revision %d "
		   "detected. Will extract v1\n", out->revision);
	sprom_extract_r123(out, in);
	return 0;
}

static int ssb_pci_sprom_get(struct ssb_bus *bus,
			     struct ssb_sprom *sprom)
{
	int err = -ENOMEM;
	u16 *buf;

	buf = kcalloc(SSB_SPROMSIZE_WORDS_R123, sizeof(u16), GFP_KERNEL);
	if (!buf)
		goto out;
	bus->sprom_size = SSB_SPROMSIZE_WORDS_R123;
	sprom_do_read(bus, buf);
	err = sprom_check_crc(buf, bus->sprom_size);
	if (err) {
		/* check for rev 4 sprom - has special signature */
		if (buf[32] == 0x5372) {
			kfree(buf);
			buf = kcalloc(SSB_SPROMSIZE_WORDS_R4, sizeof(u16),
				      GFP_KERNEL);
			if (!buf)
				goto out;
			bus->sprom_size = SSB_SPROMSIZE_WORDS_R4;
			sprom_do_read(bus, buf);
			err = sprom_check_crc(buf, bus->sprom_size);
		}
		if (err)
			ssb_printk(KERN_WARNING PFX "WARNING: Invalid"
				   " SPROM CRC (corrupt SPROM)\n");
	}
	err = sprom_extract(bus, sprom, buf, bus->sprom_size);

	kfree(buf);
out:
	return err;
}

static void ssb_pci_get_boardinfo(struct ssb_bus *bus,
				  struct ssb_boardinfo *bi)
{
	pci_read_config_word(bus->host_pci, PCI_SUBSYSTEM_VENDOR_ID,
			     &bi->vendor);
	pci_read_config_word(bus->host_pci, PCI_SUBSYSTEM_ID,
			     &bi->type);
	pci_read_config_word(bus->host_pci, PCI_REVISION_ID,
			     &bi->rev);
}

int ssb_pci_get_invariants(struct ssb_bus *bus,
			   struct ssb_init_invariants *iv)
{
	int err;

	err = ssb_pci_sprom_get(bus, &iv->sprom);
	if (err)
		goto out;
	ssb_pci_get_boardinfo(bus, &iv->boardinfo);

out:
	return err;
}

#ifdef CONFIG_SSB_DEBUG
static int ssb_pci_assert_buspower(struct ssb_bus *bus)
{
	if (likely(bus->powered_up))
		return 0;

	printk(KERN_ERR PFX "FATAL ERROR: Bus powered down "
	       "while accessing PCI MMIO space\n");
	if (bus->power_warn_count <= 10) {
		bus->power_warn_count++;
		dump_stack();
	}

	return -ENODEV;
}
#else /* DEBUG */
static inline int ssb_pci_assert_buspower(struct ssb_bus *bus)
{
	return 0;
}
#endif /* DEBUG */

static u16 ssb_pci_read16(struct ssb_device *dev, u16 offset)
{
	struct ssb_bus *bus = dev->bus;

	if (unlikely(ssb_pci_assert_buspower(bus)))
		return 0xFFFF;
	if (unlikely(bus->mapped_device != dev)) {
		if (unlikely(ssb_pci_switch_core(bus, dev)))
			return 0xFFFF;
	}
	return ioread16(bus->mmio + offset);
}

static u32 ssb_pci_read32(struct ssb_device *dev, u16 offset)
{
	struct ssb_bus *bus = dev->bus;

	if (unlikely(ssb_pci_assert_buspower(bus)))
		return 0xFFFFFFFF;
	if (unlikely(bus->mapped_device != dev)) {
		if (unlikely(ssb_pci_switch_core(bus, dev)))
			return 0xFFFFFFFF;
	}
	return ioread32(bus->mmio + offset);
}

static void ssb_pci_write16(struct ssb_device *dev, u16 offset, u16 value)
{
	struct ssb_bus *bus = dev->bus;

	if (unlikely(ssb_pci_assert_buspower(bus)))
		return;
	if (unlikely(bus->mapped_device != dev)) {
		if (unlikely(ssb_pci_switch_core(bus, dev)))
			return;
	}
	iowrite16(value, bus->mmio + offset);
}

static void ssb_pci_write32(struct ssb_device *dev, u16 offset, u32 value)
{
	struct ssb_bus *bus = dev->bus;

	if (unlikely(ssb_pci_assert_buspower(bus)))
		return;
	if (unlikely(bus->mapped_device != dev)) {
		if (unlikely(ssb_pci_switch_core(bus, dev)))
			return;
	}
	iowrite32(value, bus->mmio + offset);
}

/* Not "static", as it's used in main.c */
const struct ssb_bus_ops ssb_pci_ops = {
	.read16		= ssb_pci_read16,
	.read32		= ssb_pci_read32,
	.write16	= ssb_pci_write16,
	.write32	= ssb_pci_write32,
};

static int sprom2hex(const u16 *sprom, char *buf, size_t buf_len, u16 size)
{
	int i, pos = 0;

	for (i = 0; i < size; i++)
		pos += snprintf(buf + pos, buf_len - pos - 1,
				"%04X", swab16(sprom[i]) & 0xFFFF);
	pos += snprintf(buf + pos, buf_len - pos - 1, "\n");

	return pos + 1;
}

static int hex2sprom(u16 *sprom, const char *dump, size_t len, u16 size)
{
	char tmp[5] = { 0 };
	int cnt = 0;
	unsigned long parsed;

	if (len < size * 2)
		return -EINVAL;

	while (cnt < size) {
		memcpy(tmp, dump, 4);
		dump += 4;
		parsed = simple_strtoul(tmp, NULL, 16);
		sprom[cnt++] = swab16((u16)parsed);
	}

	return 0;
}

static ssize_t ssb_pci_attr_sprom_show(struct device *pcidev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct pci_dev *pdev = container_of(pcidev, struct pci_dev, dev);
	struct ssb_bus *bus;
	u16 *sprom;
	int err = -ENODEV;
	ssize_t count = 0;

	bus = ssb_pci_dev_to_bus(pdev);
	if (!bus)
		goto out;
	err = -ENOMEM;
	sprom = kcalloc(bus->sprom_size, sizeof(u16), GFP_KERNEL);
	if (!sprom)
		goto out;

	/* Use interruptible locking, as the SPROM write might
	 * be holding the lock for several seconds. So allow userspace
	 * to cancel operation. */
	err = -ERESTARTSYS;
	if (mutex_lock_interruptible(&bus->pci_sprom_mutex))
		goto out_kfree;
	sprom_do_read(bus, sprom);
	mutex_unlock(&bus->pci_sprom_mutex);

	count = sprom2hex(sprom, buf, PAGE_SIZE, bus->sprom_size);
	err = 0;

out_kfree:
	kfree(sprom);
out:
	return err ? err : count;
}

static ssize_t ssb_pci_attr_sprom_store(struct device *pcidev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct pci_dev *pdev = container_of(pcidev, struct pci_dev, dev);
	struct ssb_bus *bus;
	u16 *sprom;
	int res = 0, err = -ENODEV;

	bus = ssb_pci_dev_to_bus(pdev);
	if (!bus)
		goto out;
	err = -ENOMEM;
	sprom = kcalloc(bus->sprom_size, sizeof(u16), GFP_KERNEL);
	if (!sprom)
		goto out;
	err = hex2sprom(sprom, buf, count, bus->sprom_size);
	if (err) {
		err = -EINVAL;
		goto out_kfree;
	}
	err = sprom_check_crc(sprom, bus->sprom_size);
	if (err) {
		err = -EINVAL;
		goto out_kfree;
	}

	/* Use interruptible locking, as the SPROM write might
	 * be holding the lock for several seconds. So allow userspace
	 * to cancel operation. */
	err = -ERESTARTSYS;
	if (mutex_lock_interruptible(&bus->pci_sprom_mutex))
		goto out_kfree;
	err = ssb_devices_freeze(bus);
	if (err == -EOPNOTSUPP) {
		ssb_printk(KERN_ERR PFX "SPROM write: Could not freeze devices. "
			   "No suspend support. Is CONFIG_PM enabled?\n");
		goto out_unlock;
	}
	if (err) {
		ssb_printk(KERN_ERR PFX "SPROM write: Could not freeze all devices\n");
		goto out_unlock;
	}
	res = sprom_do_write(bus, sprom);
	err = ssb_devices_thaw(bus);
	if (err)
		ssb_printk(KERN_ERR PFX "SPROM write: Could not thaw all devices\n");
out_unlock:
	mutex_unlock(&bus->pci_sprom_mutex);
out_kfree:
	kfree(sprom);
out:
	if (res)
		return res;
	return err ? err : count;
}

static DEVICE_ATTR(ssb_sprom, 0600,
		   ssb_pci_attr_sprom_show,
		   ssb_pci_attr_sprom_store);

void ssb_pci_exit(struct ssb_bus *bus)
{
	struct pci_dev *pdev;

	if (bus->bustype != SSB_BUSTYPE_PCI)
		return;

	pdev = bus->host_pci;
	device_remove_file(&pdev->dev, &dev_attr_ssb_sprom);
}

int ssb_pci_init(struct ssb_bus *bus)
{
	struct pci_dev *pdev;
	int err;

	if (bus->bustype != SSB_BUSTYPE_PCI)
		return 0;

	pdev = bus->host_pci;
	mutex_init(&bus->pci_sprom_mutex);
	err = device_create_file(&pdev->dev, &dev_attr_ssb_sprom);
	if (err)
		goto out;

out:
	return err;
}
