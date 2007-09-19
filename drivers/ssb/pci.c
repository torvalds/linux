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

static u8 ssb_sprom_crc(const u16 *sprom)
{
	int word;
	u8 crc = 0xFF;

	for (word = 0; word < SSB_SPROMSIZE_WORDS - 1; word++) {
		crc = ssb_crc8(crc, sprom[word] & 0x00FF);
		crc = ssb_crc8(crc, (sprom[word] & 0xFF00) >> 8);
	}
	crc = ssb_crc8(crc, sprom[SPOFF(SSB_SPROM_REVISION)] & 0x00FF);
	crc ^= 0xFF;

	return crc;
}

static int sprom_check_crc(const u16 *sprom)
{
	u8 crc;
	u8 expected_crc;
	u16 tmp;

	crc = ssb_sprom_crc(sprom);
	tmp = sprom[SPOFF(SSB_SPROM_REVISION)] & SSB_SPROM_REVISION_CRC;
	expected_crc = tmp >> SSB_SPROM_REVISION_CRC_SHIFT;
	if (crc != expected_crc)
		return -EPROTO;

	return 0;
}

static void sprom_do_read(struct ssb_bus *bus, u16 *sprom)
{
	int i;

	for (i = 0; i < SSB_SPROMSIZE_WORDS; i++)
		sprom[i] = readw(bus->mmio + SSB_SPROM_BASE + (i * 2));
}

static int sprom_do_write(struct ssb_bus *bus, const u16 *sprom)
{
	struct pci_dev *pdev = bus->host_pci;
	int i, err;
	u32 spromctl;

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
	for (i = 0; i < SSB_SPROMSIZE_WORDS; i++) {
		if (i == SSB_SPROMSIZE_WORDS / 4)
			ssb_printk("25%%");
		else if (i == SSB_SPROMSIZE_WORDS / 2)
			ssb_printk("50%%");
		else if (i == (SSB_SPROMSIZE_WORDS / 4) * 3)
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

static void sprom_extract_r1(struct ssb_sprom_r1 *out, const u16 *in)
{
	int i;
	u16 v;

	SPEX(pci_spid, SSB_SPROM1_SPID, 0xFFFF, 0);
	SPEX(pci_svid, SSB_SPROM1_SVID, 0xFFFF, 0);
	SPEX(pci_pid, SSB_SPROM1_PID, 0xFFFF, 0);
	for (i = 0; i < 3; i++) {
		v = in[SPOFF(SSB_SPROM1_IL0MAC) + i];
		*(((__be16 *)out->il0mac) + i) = cpu_to_be16(v);
	}
	for (i = 0; i < 3; i++) {
		v = in[SPOFF(SSB_SPROM1_ET0MAC) + i];
		*(((__be16 *)out->et0mac) + i) = cpu_to_be16(v);
	}
	for (i = 0; i < 3; i++) {
		v = in[SPOFF(SSB_SPROM1_ET1MAC) + i];
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
	SPEX(antenna_a, SSB_SPROM1_BINF, SSB_SPROM1_BINF_ANTA,
	     SSB_SPROM1_BINF_ANTA_SHIFT);
	SPEX(antenna_bg, SSB_SPROM1_BINF, SSB_SPROM1_BINF_ANTBG,
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
	SPEX(antenna_gain_a, SSB_SPROM1_AGAIN, SSB_SPROM1_AGAIN_A, 0);
	SPEX(antenna_gain_bg, SSB_SPROM1_AGAIN, SSB_SPROM1_AGAIN_BG,
	     SSB_SPROM1_AGAIN_BG_SHIFT);
	for (i = 0; i < 4; i++) {
		v = in[SPOFF(SSB_SPROM1_OEM) + i];
		*(((__le16 *)out->oem) + i) = cpu_to_le16(v);
	}
}

static void sprom_extract_r2(struct ssb_sprom_r2 *out, const u16 *in)
{
	int i;
	u16 v;

	SPEX(boardflags_hi, SSB_SPROM2_BFLHI,  0xFFFF, 0);
	SPEX(maxpwr_a_hi, SSB_SPROM2_MAXP_A, SSB_SPROM2_MAXP_A_HI, 0);
	SPEX(maxpwr_a_lo, SSB_SPROM2_MAXP_A, SSB_SPROM2_MAXP_A_LO,
	     SSB_SPROM2_MAXP_A_LO_SHIFT);
	SPEX(pa1lob0, SSB_SPROM2_PA1LOB0, 0xFFFF, 0);
	SPEX(pa1lob1, SSB_SPROM2_PA1LOB1, 0xFFFF, 0);
	SPEX(pa1lob2, SSB_SPROM2_PA1LOB2, 0xFFFF, 0);
	SPEX(pa1hib0, SSB_SPROM2_PA1HIB0, 0xFFFF, 0);
	SPEX(pa1hib1, SSB_SPROM2_PA1HIB1, 0xFFFF, 0);
	SPEX(pa1hib2, SSB_SPROM2_PA1HIB2, 0xFFFF, 0);
	SPEX(ofdm_pwr_off, SSB_SPROM2_OPO, SSB_SPROM2_OPO_VALUE, 0);
	for (i = 0; i < 4; i++) {
		v = in[SPOFF(SSB_SPROM2_CCODE) + i];
		*(((__le16 *)out->country_str) + i) = cpu_to_le16(v);
	}
}

static void sprom_extract_r3(struct ssb_sprom_r3 *out, const u16 *in)
{
	out->ofdmapo  = (in[SPOFF(SSB_SPROM3_OFDMAPO) + 0] & 0xFF00) >> 8;
	out->ofdmapo |= (in[SPOFF(SSB_SPROM3_OFDMAPO) + 0] & 0x00FF) << 8;
	out->ofdmapo <<= 16;
	out->ofdmapo |= (in[SPOFF(SSB_SPROM3_OFDMAPO) + 1] & 0xFF00) >> 8;
	out->ofdmapo |= (in[SPOFF(SSB_SPROM3_OFDMAPO) + 1] & 0x00FF) << 8;

	out->ofdmalpo  = (in[SPOFF(SSB_SPROM3_OFDMALPO) + 0] & 0xFF00) >> 8;
	out->ofdmalpo |= (in[SPOFF(SSB_SPROM3_OFDMALPO) + 0] & 0x00FF) << 8;
	out->ofdmalpo <<= 16;
	out->ofdmalpo |= (in[SPOFF(SSB_SPROM3_OFDMALPO) + 1] & 0xFF00) >> 8;
	out->ofdmalpo |= (in[SPOFF(SSB_SPROM3_OFDMALPO) + 1] & 0x00FF) << 8;

	out->ofdmahpo  = (in[SPOFF(SSB_SPROM3_OFDMAHPO) + 0] & 0xFF00) >> 8;
	out->ofdmahpo |= (in[SPOFF(SSB_SPROM3_OFDMAHPO) + 0] & 0x00FF) << 8;
	out->ofdmahpo <<= 16;
	out->ofdmahpo |= (in[SPOFF(SSB_SPROM3_OFDMAHPO) + 1] & 0xFF00) >> 8;
	out->ofdmahpo |= (in[SPOFF(SSB_SPROM3_OFDMAHPO) + 1] & 0x00FF) << 8;

	SPEX(gpioldc_on_cnt, SSB_SPROM3_GPIOLDC, SSB_SPROM3_GPIOLDC_ON,
	     SSB_SPROM3_GPIOLDC_ON_SHIFT);
	SPEX(gpioldc_off_cnt, SSB_SPROM3_GPIOLDC, SSB_SPROM3_GPIOLDC_OFF,
	     SSB_SPROM3_GPIOLDC_OFF_SHIFT);
	SPEX(cckpo_1M, SSB_SPROM3_CCKPO, SSB_SPROM3_CCKPO_1M, 0);
	SPEX(cckpo_2M, SSB_SPROM3_CCKPO, SSB_SPROM3_CCKPO_2M,
	     SSB_SPROM3_CCKPO_2M_SHIFT);
	SPEX(cckpo_55M, SSB_SPROM3_CCKPO, SSB_SPROM3_CCKPO_55M,
	     SSB_SPROM3_CCKPO_55M_SHIFT);
	SPEX(cckpo_11M, SSB_SPROM3_CCKPO, SSB_SPROM3_CCKPO_11M,
	     SSB_SPROM3_CCKPO_11M_SHIFT);

	out->ofdmgpo  = (in[SPOFF(SSB_SPROM3_OFDMGPO) + 0] & 0xFF00) >> 8;
	out->ofdmgpo |= (in[SPOFF(SSB_SPROM3_OFDMGPO) + 0] & 0x00FF) << 8;
	out->ofdmgpo <<= 16;
	out->ofdmgpo |= (in[SPOFF(SSB_SPROM3_OFDMGPO) + 1] & 0xFF00) >> 8;
	out->ofdmgpo |= (in[SPOFF(SSB_SPROM3_OFDMGPO) + 1] & 0x00FF) << 8;
}

static int sprom_extract(struct ssb_bus *bus,
			 struct ssb_sprom *out, const u16 *in)
{
	memset(out, 0, sizeof(*out));

	SPEX(revision, SSB_SPROM_REVISION, SSB_SPROM_REVISION_REV, 0);
	SPEX(crc, SSB_SPROM_REVISION, SSB_SPROM_REVISION_CRC,
	     SSB_SPROM_REVISION_CRC_SHIFT);

	if ((bus->chip_id & 0xFF00) == 0x4400) {
		/* Workaround: The BCM44XX chip has a stupid revision
		 * number stored in the SPROM.
		 * Always extract r1. */
		sprom_extract_r1(&out->r1, in);
	} else {
		if (out->revision == 0)
			goto unsupported;
		if (out->revision >= 1 && out->revision <= 3)
			sprom_extract_r1(&out->r1, in);
		if (out->revision >= 2 && out->revision <= 3)
			sprom_extract_r2(&out->r2, in);
		if (out->revision == 3)
			sprom_extract_r3(&out->r3, in);
		if (out->revision >= 4)
			goto unsupported;
	}

	return 0;
unsupported:
	ssb_printk(KERN_WARNING PFX "Unsupported SPROM revision %d "
		   "detected. Will extract v1\n", out->revision);
	sprom_extract_r1(&out->r1, in);
	return 0;
}

static int ssb_pci_sprom_get(struct ssb_bus *bus,
			     struct ssb_sprom *sprom)
{
	int err = -ENOMEM;
	u16 *buf;

	buf = kcalloc(SSB_SPROMSIZE_WORDS, sizeof(u16), GFP_KERNEL);
	if (!buf)
		goto out;
	sprom_do_read(bus, buf);
	err = sprom_check_crc(buf);
	if (err) {
		ssb_printk(KERN_WARNING PFX
			   "WARNING: Invalid SPROM CRC (corrupt SPROM)\n");
	}
	err = sprom_extract(bus, sprom, buf);

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

static int sprom2hex(const u16 *sprom, char *buf, size_t buf_len)
{
	int i, pos = 0;

	for (i = 0; i < SSB_SPROMSIZE_WORDS; i++) {
		pos += snprintf(buf + pos, buf_len - pos - 1,
				"%04X", swab16(sprom[i]) & 0xFFFF);
	}
	pos += snprintf(buf + pos, buf_len - pos - 1, "\n");

	return pos + 1;
}

static int hex2sprom(u16 *sprom, const char *dump, size_t len)
{
	char tmp[5] = { 0 };
	int cnt = 0;
	unsigned long parsed;

	if (len < SSB_SPROMSIZE_BYTES * 2)
		return -EINVAL;

	while (cnt < SSB_SPROMSIZE_WORDS) {
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
	sprom = kcalloc(SSB_SPROMSIZE_WORDS, sizeof(u16), GFP_KERNEL);
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

	count = sprom2hex(sprom, buf, PAGE_SIZE);
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
	sprom = kcalloc(SSB_SPROMSIZE_WORDS, sizeof(u16), GFP_KERNEL);
	if (!sprom)
		goto out;
	err = hex2sprom(sprom, buf, count);
	if (err) {
		err = -EINVAL;
		goto out_kfree;
	}
	err = sprom_check_crc(sprom);
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
