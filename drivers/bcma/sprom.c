/*
 * Broadcom specific AMBA
 * SPROM reading
 *
 * Copyright 2011, 2012, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"

#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_regs.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

static int(*get_fallback_sprom)(struct bcma_bus *dev, struct ssb_sprom *out);

/**
 * bcma_arch_register_fallback_sprom - Registers a method providing a
 * fallback SPROM if no SPROM is found.
 *
 * @sprom_callback: The callback function.
 *
 * With this function the architecture implementation may register a
 * callback handler which fills the SPROM data structure. The fallback is
 * used for PCI based BCMA devices, where no valid SPROM can be found
 * in the shadow registers and to provide the SPROM for SoCs where BCMA is
 * to controll the system bus.
 *
 * This function is useful for weird architectures that have a half-assed
 * BCMA device hardwired to their PCI bus.
 *
 * This function is available for architecture code, only. So it is not
 * exported.
 */
int bcma_arch_register_fallback_sprom(int (*sprom_callback)(struct bcma_bus *bus,
				     struct ssb_sprom *out))
{
	if (get_fallback_sprom)
		return -EEXIST;
	get_fallback_sprom = sprom_callback;

	return 0;
}

static int bcma_fill_sprom_with_fallback(struct bcma_bus *bus,
					 struct ssb_sprom *out)
{
	int err;

	if (!get_fallback_sprom) {
		err = -ENOENT;
		goto fail;
	}

	err = get_fallback_sprom(bus, out);
	if (err)
		goto fail;

	pr_debug("Using SPROM revision %d provided by"
		 " platform.\n", bus->sprom.revision);
	return 0;
fail:
	pr_warn("Using fallback SPROM failed (err %d)\n", err);
	return err;
}

/**************************************************
 * R/W ops.
 **************************************************/

static void bcma_sprom_read(struct bcma_bus *bus, u16 offset, u16 *sprom)
{
	int i;
	for (i = 0; i < SSB_SPROMSIZE_WORDS_R4; i++)
		sprom[i] = bcma_read16(bus->drv_cc.core,
				       offset + (i * 2));
}

/**************************************************
 * Validation.
 **************************************************/

static inline u8 bcma_crc8(u8 crc, u8 data)
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

static u8 bcma_sprom_crc(const u16 *sprom)
{
	int word;
	u8 crc = 0xFF;

	for (word = 0; word < SSB_SPROMSIZE_WORDS_R4 - 1; word++) {
		crc = bcma_crc8(crc, sprom[word] & 0x00FF);
		crc = bcma_crc8(crc, (sprom[word] & 0xFF00) >> 8);
	}
	crc = bcma_crc8(crc, sprom[SSB_SPROMSIZE_WORDS_R4 - 1] & 0x00FF);
	crc ^= 0xFF;

	return crc;
}

static int bcma_sprom_check_crc(const u16 *sprom)
{
	u8 crc;
	u8 expected_crc;
	u16 tmp;

	crc = bcma_sprom_crc(sprom);
	tmp = sprom[SSB_SPROMSIZE_WORDS_R4 - 1] & SSB_SPROM_REVISION_CRC;
	expected_crc = tmp >> SSB_SPROM_REVISION_CRC_SHIFT;
	if (crc != expected_crc)
		return -EPROTO;

	return 0;
}

static int bcma_sprom_valid(const u16 *sprom)
{
	u16 revision;
	int err;

	err = bcma_sprom_check_crc(sprom);
	if (err)
		return err;

	revision = sprom[SSB_SPROMSIZE_WORDS_R4 - 1] & SSB_SPROM_REVISION_REV;
	if (revision != 8 && revision != 9) {
		pr_err("Unsupported SPROM revision: %d\n", revision);
		return -ENOENT;
	}

	return 0;
}

/**************************************************
 * SPROM extraction.
 **************************************************/

#define SPOFF(offset)	((offset) / sizeof(u16))

#define SPEX(_field, _offset, _mask, _shift)	\
	bus->sprom._field = ((sprom[SPOFF(_offset)] & (_mask)) >> (_shift))

static void bcma_sprom_extract_r8(struct bcma_bus *bus, const u16 *sprom)
{
	u16 v, o;
	int i;
	u16 pwr_info_offset[] = {
		SSB_SROM8_PWR_INFO_CORE0, SSB_SROM8_PWR_INFO_CORE1,
		SSB_SROM8_PWR_INFO_CORE2, SSB_SROM8_PWR_INFO_CORE3
	};
	BUILD_BUG_ON(ARRAY_SIZE(pwr_info_offset) !=
			ARRAY_SIZE(bus->sprom.core_pwr_info));

	bus->sprom.revision = sprom[SSB_SPROMSIZE_WORDS_R4 - 1] &
		SSB_SPROM_REVISION_REV;

	for (i = 0; i < 3; i++) {
		v = sprom[SPOFF(SSB_SPROM8_IL0MAC) + i];
		*(((__be16 *)bus->sprom.il0mac) + i) = cpu_to_be16(v);
	}

	SPEX(board_rev, SSB_SPROM8_BOARDREV, ~0, 0);

	SPEX(txpid2g[0], SSB_SPROM4_TXPID2G01, SSB_SPROM4_TXPID2G0,
	     SSB_SPROM4_TXPID2G0_SHIFT);
	SPEX(txpid2g[1], SSB_SPROM4_TXPID2G01, SSB_SPROM4_TXPID2G1,
	     SSB_SPROM4_TXPID2G1_SHIFT);
	SPEX(txpid2g[2], SSB_SPROM4_TXPID2G23, SSB_SPROM4_TXPID2G2,
	     SSB_SPROM4_TXPID2G2_SHIFT);
	SPEX(txpid2g[3], SSB_SPROM4_TXPID2G23, SSB_SPROM4_TXPID2G3,
	     SSB_SPROM4_TXPID2G3_SHIFT);

	SPEX(txpid5gl[0], SSB_SPROM4_TXPID5GL01, SSB_SPROM4_TXPID5GL0,
	     SSB_SPROM4_TXPID5GL0_SHIFT);
	SPEX(txpid5gl[1], SSB_SPROM4_TXPID5GL01, SSB_SPROM4_TXPID5GL1,
	     SSB_SPROM4_TXPID5GL1_SHIFT);
	SPEX(txpid5gl[2], SSB_SPROM4_TXPID5GL23, SSB_SPROM4_TXPID5GL2,
	     SSB_SPROM4_TXPID5GL2_SHIFT);
	SPEX(txpid5gl[3], SSB_SPROM4_TXPID5GL23, SSB_SPROM4_TXPID5GL3,
	     SSB_SPROM4_TXPID5GL3_SHIFT);

	SPEX(txpid5g[0], SSB_SPROM4_TXPID5G01, SSB_SPROM4_TXPID5G0,
	     SSB_SPROM4_TXPID5G0_SHIFT);
	SPEX(txpid5g[1], SSB_SPROM4_TXPID5G01, SSB_SPROM4_TXPID5G1,
	     SSB_SPROM4_TXPID5G1_SHIFT);
	SPEX(txpid5g[2], SSB_SPROM4_TXPID5G23, SSB_SPROM4_TXPID5G2,
	     SSB_SPROM4_TXPID5G2_SHIFT);
	SPEX(txpid5g[3], SSB_SPROM4_TXPID5G23, SSB_SPROM4_TXPID5G3,
	     SSB_SPROM4_TXPID5G3_SHIFT);

	SPEX(txpid5gh[0], SSB_SPROM4_TXPID5GH01, SSB_SPROM4_TXPID5GH0,
	     SSB_SPROM4_TXPID5GH0_SHIFT);
	SPEX(txpid5gh[1], SSB_SPROM4_TXPID5GH01, SSB_SPROM4_TXPID5GH1,
	     SSB_SPROM4_TXPID5GH1_SHIFT);
	SPEX(txpid5gh[2], SSB_SPROM4_TXPID5GH23, SSB_SPROM4_TXPID5GH2,
	     SSB_SPROM4_TXPID5GH2_SHIFT);
	SPEX(txpid5gh[3], SSB_SPROM4_TXPID5GH23, SSB_SPROM4_TXPID5GH3,
	     SSB_SPROM4_TXPID5GH3_SHIFT);

	SPEX(boardflags_lo, SSB_SPROM8_BFLLO, ~0, 0);
	SPEX(boardflags_hi, SSB_SPROM8_BFLHI, ~0, 0);
	SPEX(boardflags2_lo, SSB_SPROM8_BFL2LO, ~0, 0);
	SPEX(boardflags2_hi, SSB_SPROM8_BFL2HI, ~0, 0);

	SPEX(country_code, SSB_SPROM8_CCODE, ~0, 0);

	/* Extract cores power info info */
	for (i = 0; i < ARRAY_SIZE(pwr_info_offset); i++) {
		o = pwr_info_offset[i];
		SPEX(core_pwr_info[i].itssi_2g, o + SSB_SROM8_2G_MAXP_ITSSI,
			SSB_SPROM8_2G_ITSSI, SSB_SPROM8_2G_ITSSI_SHIFT);
		SPEX(core_pwr_info[i].maxpwr_2g, o + SSB_SROM8_2G_MAXP_ITSSI,
			SSB_SPROM8_2G_MAXP, 0);

		SPEX(core_pwr_info[i].pa_2g[0], o + SSB_SROM8_2G_PA_0, ~0, 0);
		SPEX(core_pwr_info[i].pa_2g[1], o + SSB_SROM8_2G_PA_1, ~0, 0);
		SPEX(core_pwr_info[i].pa_2g[2], o + SSB_SROM8_2G_PA_2, ~0, 0);

		SPEX(core_pwr_info[i].itssi_5g, o + SSB_SROM8_5G_MAXP_ITSSI,
			SSB_SPROM8_5G_ITSSI, SSB_SPROM8_5G_ITSSI_SHIFT);
		SPEX(core_pwr_info[i].maxpwr_5g, o + SSB_SROM8_5G_MAXP_ITSSI,
			SSB_SPROM8_5G_MAXP, 0);
		SPEX(core_pwr_info[i].maxpwr_5gh, o + SSB_SPROM8_5GHL_MAXP,
			SSB_SPROM8_5GH_MAXP, 0);
		SPEX(core_pwr_info[i].maxpwr_5gl, o + SSB_SPROM8_5GHL_MAXP,
			SSB_SPROM8_5GL_MAXP, SSB_SPROM8_5GL_MAXP_SHIFT);

		SPEX(core_pwr_info[i].pa_5gl[0], o + SSB_SROM8_5GL_PA_0, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gl[1], o + SSB_SROM8_5GL_PA_1, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gl[2], o + SSB_SROM8_5GL_PA_2, ~0, 0);
		SPEX(core_pwr_info[i].pa_5g[0], o + SSB_SROM8_5G_PA_0, ~0, 0);
		SPEX(core_pwr_info[i].pa_5g[1], o + SSB_SROM8_5G_PA_1, ~0, 0);
		SPEX(core_pwr_info[i].pa_5g[2], o + SSB_SROM8_5G_PA_2, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gh[0], o + SSB_SROM8_5GH_PA_0, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gh[1], o + SSB_SROM8_5GH_PA_1, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gh[2], o + SSB_SROM8_5GH_PA_2, ~0, 0);
	}

	SPEX(fem.ghz2.tssipos, SSB_SPROM8_FEM2G, SSB_SROM8_FEM_TSSIPOS,
	     SSB_SROM8_FEM_TSSIPOS_SHIFT);
	SPEX(fem.ghz2.extpa_gain, SSB_SPROM8_FEM2G, SSB_SROM8_FEM_EXTPA_GAIN,
	     SSB_SROM8_FEM_EXTPA_GAIN_SHIFT);
	SPEX(fem.ghz2.pdet_range, SSB_SPROM8_FEM2G, SSB_SROM8_FEM_PDET_RANGE,
	     SSB_SROM8_FEM_PDET_RANGE_SHIFT);
	SPEX(fem.ghz2.tr_iso, SSB_SPROM8_FEM2G, SSB_SROM8_FEM_TR_ISO,
	     SSB_SROM8_FEM_TR_ISO_SHIFT);
	SPEX(fem.ghz2.antswlut, SSB_SPROM8_FEM2G, SSB_SROM8_FEM_ANTSWLUT,
	     SSB_SROM8_FEM_ANTSWLUT_SHIFT);

	SPEX(fem.ghz5.tssipos, SSB_SPROM8_FEM5G, SSB_SROM8_FEM_TSSIPOS,
	     SSB_SROM8_FEM_TSSIPOS_SHIFT);
	SPEX(fem.ghz5.extpa_gain, SSB_SPROM8_FEM5G, SSB_SROM8_FEM_EXTPA_GAIN,
	     SSB_SROM8_FEM_EXTPA_GAIN_SHIFT);
	SPEX(fem.ghz5.pdet_range, SSB_SPROM8_FEM5G, SSB_SROM8_FEM_PDET_RANGE,
	     SSB_SROM8_FEM_PDET_RANGE_SHIFT);
	SPEX(fem.ghz5.tr_iso, SSB_SPROM8_FEM5G, SSB_SROM8_FEM_TR_ISO,
	     SSB_SROM8_FEM_TR_ISO_SHIFT);
	SPEX(fem.ghz5.antswlut, SSB_SPROM8_FEM5G, SSB_SROM8_FEM_ANTSWLUT,
	     SSB_SROM8_FEM_ANTSWLUT_SHIFT);
}

/*
 * Indicates the presence of external SPROM.
 */
static bool bcma_sprom_ext_available(struct bcma_bus *bus)
{
	u32 chip_status;
	u32 srom_control;
	u32 present_mask;

	if (bus->drv_cc.core->id.rev >= 31) {
		if (!(bus->drv_cc.capabilities & BCMA_CC_CAP_SPROM))
			return false;

		srom_control = bcma_read32(bus->drv_cc.core,
					   BCMA_CC_SROM_CONTROL);
		return srom_control & BCMA_CC_SROM_CONTROL_PRESENT;
	}

	/* older chipcommon revisions use chip status register */
	chip_status = bcma_read32(bus->drv_cc.core, BCMA_CC_CHIPSTAT);
	switch (bus->chipinfo.id) {
	case 0x4313:
		present_mask = BCMA_CC_CHIPST_4313_SPROM_PRESENT;
		break;

	case 0x4331:
		present_mask = BCMA_CC_CHIPST_4331_SPROM_PRESENT;
		break;

	default:
		return true;
	}

	return chip_status & present_mask;
}

/*
 * Indicates that on-chip OTP memory is present and enabled.
 */
static bool bcma_sprom_onchip_available(struct bcma_bus *bus)
{
	u32 chip_status;
	u32 otpsize = 0;
	bool present;

	chip_status = bcma_read32(bus->drv_cc.core, BCMA_CC_CHIPSTAT);
	switch (bus->chipinfo.id) {
	case 0x4313:
		present = chip_status & BCMA_CC_CHIPST_4313_OTP_PRESENT;
		break;

	case 0x4331:
		present = chip_status & BCMA_CC_CHIPST_4331_OTP_PRESENT;
		break;

	case 43224:
	case 43225:
		/* for these chips OTP is always available */
		present = true;
		break;

	default:
		present = false;
		break;
	}

	if (present) {
		otpsize = bus->drv_cc.capabilities & BCMA_CC_CAP_OTPS;
		otpsize >>= BCMA_CC_CAP_OTPS_SHIFT;
	}

	return otpsize != 0;
}

/*
 * Verify OTP is filled and determine the byte
 * offset where SPROM data is located.
 *
 * On error, returns 0; byte offset otherwise.
 */
static int bcma_sprom_onchip_offset(struct bcma_bus *bus)
{
	struct bcma_device *cc = bus->drv_cc.core;
	u32 offset;

	/* verify OTP status */
	if ((bcma_read32(cc, BCMA_CC_OTPS) & BCMA_CC_OTPS_GU_PROG_HW) == 0)
		return 0;

	/* obtain bit offset from otplayout register */
	offset = (bcma_read32(cc, BCMA_CC_OTPL) & BCMA_CC_OTPL_GURGN_OFFSET);
	return BCMA_CC_SPROM + (offset >> 3);
}

int bcma_sprom_get(struct bcma_bus *bus)
{
	u16 offset = BCMA_CC_SPROM;
	u16 *sprom;
	int err = 0;

	if (!bus->drv_cc.core)
		return -EOPNOTSUPP;

	if (!bcma_sprom_ext_available(bus)) {
		bool sprom_onchip;

		/*
		 * External SPROM takes precedence so check
		 * on-chip OTP only when no external SPROM
		 * is present.
		 */
		sprom_onchip = bcma_sprom_onchip_available(bus);
		if (sprom_onchip) {
			/* determine offset */
			offset = bcma_sprom_onchip_offset(bus);
		}
		if (!offset || !sprom_onchip) {
			/*
			 * Maybe there is no SPROM on the device?
			 * Now we ask the arch code if there is some sprom
			 * available for this device in some other storage.
			 */
			err = bcma_fill_sprom_with_fallback(bus, &bus->sprom);
			return err;
		}
	}

	sprom = kcalloc(SSB_SPROMSIZE_WORDS_R4, sizeof(u16),
			GFP_KERNEL);
	if (!sprom)
		return -ENOMEM;

	if (bus->chipinfo.id == 0x4331)
		bcma_chipco_bcm4331_ext_pa_lines_ctl(&bus->drv_cc, false);

	pr_debug("SPROM offset 0x%x\n", offset);
	bcma_sprom_read(bus, offset, sprom);

	if (bus->chipinfo.id == 0x4331)
		bcma_chipco_bcm4331_ext_pa_lines_ctl(&bus->drv_cc, true);

	err = bcma_sprom_valid(sprom);
	if (err)
		goto out;

	bcma_sprom_extract_r8(bus, sprom);

out:
	kfree(sprom);
	return err;
}
