// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Microchip Technology

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>

/* External Register Control Register */
#define LAN87XX_EXT_REG_CTL                     (0x14)
#define LAN87XX_EXT_REG_CTL_RD_CTL              (0x1000)
#define LAN87XX_EXT_REG_CTL_WR_CTL              (0x0800)

/* External Register Read Data Register */
#define LAN87XX_EXT_REG_RD_DATA                 (0x15)

/* External Register Write Data Register */
#define LAN87XX_EXT_REG_WR_DATA                 (0x16)

/* Interrupt Source Register */
#define LAN87XX_INTERRUPT_SOURCE                (0x18)

/* Interrupt Mask Register */
#define LAN87XX_INTERRUPT_MASK                  (0x19)
#define LAN87XX_MASK_LINK_UP                    (0x0004)
#define LAN87XX_MASK_LINK_DOWN                  (0x0002)

/* phyaccess nested types */
#define	PHYACC_ATTR_MODE_READ		0
#define	PHYACC_ATTR_MODE_WRITE		1
#define	PHYACC_ATTR_MODE_MODIFY		2

#define	PHYACC_ATTR_BANK_SMI		0
#define	PHYACC_ATTR_BANK_MISC		1
#define	PHYACC_ATTR_BANK_PCS		2
#define	PHYACC_ATTR_BANK_AFE		3
#define	PHYACC_ATTR_BANK_DSP		4
#define	PHYACC_ATTR_BANK_MAX		7

/* measurement defines */
#define	LAN87XX_CABLE_TEST_OK		0
#define	LAN87XX_CABLE_TEST_OPEN	1
#define	LAN87XX_CABLE_TEST_SAME_SHORT	2

#define DRIVER_AUTHOR	"Nisar Sayed <nisar.sayed@microchip.com>"
#define DRIVER_DESC	"Microchip LAN87XX T1 PHY driver"

struct access_ereg_val {
	u8  mode;
	u8  bank;
	u8  offset;
	u16 val;
	u16 mask;
};

static int access_ereg(struct phy_device *phydev, u8 mode, u8 bank,
		       u8 offset, u16 val)
{
	u16 ereg = 0;
	int rc = 0;

	if (mode > PHYACC_ATTR_MODE_WRITE || bank > PHYACC_ATTR_BANK_MAX)
		return -EINVAL;

	if (bank == PHYACC_ATTR_BANK_SMI) {
		if (mode == PHYACC_ATTR_MODE_WRITE)
			rc = phy_write(phydev, offset, val);
		else
			rc = phy_read(phydev, offset);
		return rc;
	}

	if (mode == PHYACC_ATTR_MODE_WRITE) {
		ereg = LAN87XX_EXT_REG_CTL_WR_CTL;
		rc = phy_write(phydev, LAN87XX_EXT_REG_WR_DATA, val);
		if (rc < 0)
			return rc;
	} else {
		ereg = LAN87XX_EXT_REG_CTL_RD_CTL;
	}

	ereg |= (bank << 8) | offset;

	rc = phy_write(phydev, LAN87XX_EXT_REG_CTL, ereg);
	if (rc < 0)
		return rc;

	if (mode == PHYACC_ATTR_MODE_READ)
		rc = phy_read(phydev, LAN87XX_EXT_REG_RD_DATA);

	return rc;
}

static int access_ereg_modify_changed(struct phy_device *phydev,
				      u8 bank, u8 offset, u16 val, u16 mask)
{
	int new = 0, rc = 0;

	if (bank > PHYACC_ATTR_BANK_MAX)
		return -EINVAL;

	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ, bank, offset, val);
	if (rc < 0)
		return rc;

	new = val | (rc & (mask ^ 0xFFFF));
	rc = access_ereg(phydev, PHYACC_ATTR_MODE_WRITE, bank, offset, new);

	return rc;
}

static int lan87xx_phy_init(struct phy_device *phydev)
{
	static const struct access_ereg_val init[] = {
		/* TX Amplitude = 5 */
		{PHYACC_ATTR_MODE_MODIFY, PHYACC_ATTR_BANK_AFE, 0x0B,
		 0x000A, 0x001E},
		/* Clear SMI interrupts */
		{PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_SMI, 0x18,
		 0, 0},
		/* Clear MISC interrupts */
		{PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_MISC, 0x08,
		 0, 0},
		/* Turn on TC10 Ring Oscillator (ROSC) */
		{PHYACC_ATTR_MODE_MODIFY, PHYACC_ATTR_BANK_MISC, 0x20,
		 0x0020, 0x0020},
		/* WUR Detect Length to 1.2uS, LPC Detect Length to 1.09uS */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_PCS, 0x20,
		 0x283C, 0},
		/* Wake_In Debounce Length to 39uS, Wake_Out Length to 79uS */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_MISC, 0x21,
		 0x274F, 0},
		/* Enable Auto Wake Forward to Wake_Out, ROSC on, Sleep,
		 * and Wake_In to wake PHY
		 */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_MISC, 0x20,
		 0x80A7, 0},
		/* Enable WUP Auto Fwd, Enable Wake on MDI, Wakeup Debouncer
		 * to 128 uS
		 */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_MISC, 0x24,
		 0xF110, 0},
		/* Enable HW Init */
		{PHYACC_ATTR_MODE_MODIFY, PHYACC_ATTR_BANK_SMI, 0x1A,
		 0x0100, 0x0100},
	};
	int rc, i;

	/* Start manual initialization procedures in Managed Mode */
	rc = access_ereg_modify_changed(phydev, PHYACC_ATTR_BANK_SMI,
					0x1a, 0x0000, 0x0100);
	if (rc < 0)
		return rc;

	/* Soft Reset the SMI block */
	rc = access_ereg_modify_changed(phydev, PHYACC_ATTR_BANK_SMI,
					0x00, 0x8000, 0x8000);
	if (rc < 0)
		return rc;

	/* Check to see if the self-clearing bit is cleared */
	usleep_range(1000, 2000);
	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
			 PHYACC_ATTR_BANK_SMI, 0x00, 0);
	if (rc < 0)
		return rc;
	if ((rc & 0x8000) != 0)
		return -ETIMEDOUT;

	/* PHY Initialization */
	for (i = 0; i < ARRAY_SIZE(init); i++) {
		if (init[i].mode == PHYACC_ATTR_MODE_MODIFY) {
			rc = access_ereg_modify_changed(phydev, init[i].bank,
							init[i].offset,
							init[i].val,
							init[i].mask);
		} else {
			rc = access_ereg(phydev, init[i].mode, init[i].bank,
					 init[i].offset, init[i].val);
		}
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int lan87xx_phy_config_intr(struct phy_device *phydev)
{
	int rc, val = 0;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/* unmask all source and clear them before enable */
		rc = phy_write(phydev, LAN87XX_INTERRUPT_MASK, 0x7FFF);
		rc = phy_read(phydev, LAN87XX_INTERRUPT_SOURCE);
		val = LAN87XX_MASK_LINK_UP | LAN87XX_MASK_LINK_DOWN;
		rc = phy_write(phydev, LAN87XX_INTERRUPT_MASK, val);
	} else {
		rc = phy_write(phydev, LAN87XX_INTERRUPT_MASK, val);
		if (rc)
			return rc;

		rc = phy_read(phydev, LAN87XX_INTERRUPT_SOURCE);
	}

	return rc < 0 ? rc : 0;
}

static irqreturn_t lan87xx_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, LAN87XX_INTERRUPT_SOURCE);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (irq_status == 0)
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int lan87xx_config_init(struct phy_device *phydev)
{
	int rc = lan87xx_phy_init(phydev);

	return rc < 0 ? rc : 0;
}

static int microchip_cable_test_start_common(struct phy_device *phydev)
{
	int bmcr, bmsr, ret;

	/* If auto-negotiation is enabled, but not complete, the cable
	 * test never completes. So disable auto-neg.
	 */
	bmcr = phy_read(phydev, MII_BMCR);
	if (bmcr < 0)
		return bmcr;

	bmsr = phy_read(phydev, MII_BMSR);

	if (bmsr < 0)
		return bmsr;

	if (bmcr & BMCR_ANENABLE) {
		ret =  phy_modify(phydev, MII_BMCR, BMCR_ANENABLE, 0);
		if (ret < 0)
			return ret;
		ret = genphy_soft_reset(phydev);
		if (ret < 0)
			return ret;
	}

	/* If the link is up, allow it some time to go down */
	if (bmsr & BMSR_LSTATUS)
		msleep(1500);

	return 0;
}

static int lan87xx_cable_test_start(struct phy_device *phydev)
{
	static const struct access_ereg_val cable_test[] = {
		/* min wait */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 93,
		 0, 0},
		/* max wait */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 94,
		 10, 0},
		/* pulse cycle */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 95,
		 90, 0},
		/* cable diag thresh */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 92,
		 60, 0},
		/* max gain */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 79,
		 31, 0},
		/* clock align for each iteration */
		{PHYACC_ATTR_MODE_MODIFY, PHYACC_ATTR_BANK_DSP, 55,
		 0, 0x0038},
		/* max cycle wait config */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 94,
		 70, 0},
		/* start cable diag*/
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 90,
		 1, 0},
	};
	int rc, i;

	rc = microchip_cable_test_start_common(phydev);
	if (rc < 0)
		return rc;

	/* start cable diag */
	/* check if part is alive - if not, return diagnostic error */
	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_SMI,
			 0x00, 0);
	if (rc < 0)
		return rc;

	/* master/slave specific configs */
	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_SMI,
			 0x0A, 0);
	if (rc < 0)
		return rc;

	if ((rc & 0x4000) != 0x4000) {
		/* DUT is Slave */
		rc = access_ereg_modify_changed(phydev, PHYACC_ATTR_BANK_AFE,
						0x0E, 0x5, 0x7);
		if (rc < 0)
			return rc;
		rc = access_ereg_modify_changed(phydev, PHYACC_ATTR_BANK_SMI,
						0x1A, 0x8, 0x8);
		if (rc < 0)
			return rc;
	} else {
		/* DUT is Master */
		rc = access_ereg_modify_changed(phydev, PHYACC_ATTR_BANK_SMI,
						0x10, 0x8, 0x40);
		if (rc < 0)
			return rc;
	}

	for (i = 0; i < ARRAY_SIZE(cable_test); i++) {
		if (cable_test[i].mode == PHYACC_ATTR_MODE_MODIFY) {
			rc = access_ereg_modify_changed(phydev,
							cable_test[i].bank,
							cable_test[i].offset,
							cable_test[i].val,
							cable_test[i].mask);
			/* wait 50ms */
			msleep(50);
		} else {
			rc = access_ereg(phydev, cable_test[i].mode,
					 cable_test[i].bank,
					 cable_test[i].offset,
					 cable_test[i].val);
		}
		if (rc < 0)
			return rc;
	}
	/* cable diag started */

	return 0;
}

static int lan87xx_cable_test_report_trans(u32 result)
{
	switch (result) {
	case LAN87XX_CABLE_TEST_OK:
		return ETHTOOL_A_CABLE_RESULT_CODE_OK;
	case LAN87XX_CABLE_TEST_OPEN:
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
	case LAN87XX_CABLE_TEST_SAME_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;
	default:
		/* DIAGNOSTIC_ERROR */
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}
}

static int lan87xx_cable_test_report(struct phy_device *phydev)
{
	int pos_peak_cycle = 0, pos_peak_in_phases = 0, pos_peak_phase = 0;
	int neg_peak_cycle = 0, neg_peak_in_phases = 0, neg_peak_phase = 0;
	int noise_margin = 20, time_margin = 89, jitter_var = 30;
	int min_time_diff = 96, max_time_diff = 96 + time_margin;
	bool fault = false, check_a = false, check_b = false;
	int gain_idx = 0, pos_peak = 0, neg_peak = 0;
	int pos_peak_time = 0, neg_peak_time = 0;
	int pos_peak_in_phases_hybrid = 0;
	int detect = -1;

	gain_idx = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
			       PHYACC_ATTR_BANK_DSP, 151, 0);
	/* read non-hybrid results */
	pos_peak = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
			       PHYACC_ATTR_BANK_DSP, 153, 0);
	neg_peak = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
			       PHYACC_ATTR_BANK_DSP, 154, 0);
	pos_peak_time = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
				    PHYACC_ATTR_BANK_DSP, 156, 0);
	neg_peak_time = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
				    PHYACC_ATTR_BANK_DSP, 157, 0);

	pos_peak_cycle = (pos_peak_time >> 7) & 0x7F;
	/* calculate non-hybrid values */
	pos_peak_phase = pos_peak_time & 0x7F;
	pos_peak_in_phases = (pos_peak_cycle * 96) + pos_peak_phase;
	neg_peak_cycle = (neg_peak_time >> 7) & 0x7F;
	neg_peak_phase = neg_peak_time & 0x7F;
	neg_peak_in_phases = (neg_peak_cycle * 96) + neg_peak_phase;

	/* process values */
	check_a =
		((pos_peak_in_phases - neg_peak_in_phases) >= min_time_diff) &&
		((pos_peak_in_phases - neg_peak_in_phases) < max_time_diff) &&
		pos_peak_in_phases_hybrid < pos_peak_in_phases &&
		(pos_peak_in_phases_hybrid < (neg_peak_in_phases + jitter_var));
	check_b =
		((neg_peak_in_phases - pos_peak_in_phases) >= min_time_diff) &&
		((neg_peak_in_phases - pos_peak_in_phases) < max_time_diff) &&
		pos_peak_in_phases_hybrid < neg_peak_in_phases &&
		(pos_peak_in_phases_hybrid < (pos_peak_in_phases + jitter_var));

	if (pos_peak_in_phases > neg_peak_in_phases && check_a)
		detect = 2;
	else if ((neg_peak_in_phases > pos_peak_in_phases) && check_b)
		detect = 1;

	if (pos_peak > noise_margin && neg_peak > noise_margin &&
	    gain_idx >= 0) {
		if (detect == 1 || detect == 2)
			fault = true;
	}

	if (!fault)
		detect = 0;

	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
				lan87xx_cable_test_report_trans(detect));

	return 0;
}

static int lan87xx_cable_test_get_status(struct phy_device *phydev,
					 bool *finished)
{
	int rc = 0;

	*finished = false;

	/* check if cable diag was finished */
	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_DSP,
			 90, 0);
	if (rc < 0)
		return rc;

	if ((rc & 2) == 2) {
		/* stop cable diag*/
		rc = access_ereg(phydev, PHYACC_ATTR_MODE_WRITE,
				 PHYACC_ATTR_BANK_DSP,
				 90, 0);
		if (rc < 0)
			return rc;

		*finished = true;

		return lan87xx_cable_test_report(phydev);
	}

	return 0;
}

static struct phy_driver microchip_t1_phy_driver[] = {
	{
		.phy_id         = 0x0007c150,
		.phy_id_mask    = 0xfffffff0,
		.name           = "Microchip LAN87xx T1",
		.flags          = PHY_POLL_CABLE_TEST,

		.features       = PHY_BASIC_T1_FEATURES,

		.config_init	= lan87xx_config_init,

		.config_intr    = lan87xx_phy_config_intr,
		.handle_interrupt = lan87xx_handle_interrupt,

		.suspend        = genphy_suspend,
		.resume         = genphy_resume,
		.cable_test_start = lan87xx_cable_test_start,
		.cable_test_get_status = lan87xx_cable_test_get_status,
	}
};

module_phy_driver(microchip_t1_phy_driver);

static struct mdio_device_id __maybe_unused microchip_t1_tbl[] = {
	{ 0x0007c150, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, microchip_t1_tbl);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
