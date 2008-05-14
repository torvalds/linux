/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2007 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

/*****************************************************************************
 * Support for the SFE4001 NIC: driver code for the PCA9539 I/O expander that
 * controls the PHY power rails, and for the MAX6647 temp. sensor used to check
 * the PHY
 */
#include <linux/delay.h>
#include "efx.h"
#include "phy.h"
#include "boards.h"
#include "falcon.h"
#include "falcon_hwdefs.h"
#include "mac.h"

/**************************************************************************
 *
 * I2C IO Expander device
 *
 **************************************************************************/
#define	PCA9539 0x74

#define	P0_IN 0x00
#define	P0_OUT 0x02
#define	P0_INVERT 0x04
#define	P0_CONFIG 0x06

#define	P0_EN_1V0X_LBN 0
#define	P0_EN_1V0X_WIDTH 1
#define	P0_EN_1V2_LBN 1
#define	P0_EN_1V2_WIDTH 1
#define	P0_EN_2V5_LBN 2
#define	P0_EN_2V5_WIDTH 1
#define	P0_EN_3V3X_LBN 3
#define	P0_EN_3V3X_WIDTH 1
#define	P0_EN_5V_LBN 4
#define	P0_EN_5V_WIDTH 1
#define	P0_SHORTEN_JTAG_LBN 5
#define	P0_SHORTEN_JTAG_WIDTH 1
#define	P0_X_TRST_LBN 6
#define	P0_X_TRST_WIDTH 1
#define	P0_DSP_RESET_LBN 7
#define	P0_DSP_RESET_WIDTH 1

#define	P1_IN 0x01
#define	P1_OUT 0x03
#define	P1_INVERT 0x05
#define	P1_CONFIG 0x07

#define	P1_AFE_PWD_LBN 0
#define	P1_AFE_PWD_WIDTH 1
#define	P1_DSP_PWD25_LBN 1
#define	P1_DSP_PWD25_WIDTH 1
#define	P1_RESERVED_LBN 2
#define	P1_RESERVED_WIDTH 2
#define	P1_SPARE_LBN 4
#define	P1_SPARE_WIDTH 4


/**************************************************************************
 *
 * Temperature Sensor
 *
 **************************************************************************/
#define	MAX6647	0x4e

#define	RLTS	0x00
#define	RLTE	0x01
#define	RSL	0x02
#define	RCL	0x03
#define	RCRA	0x04
#define	RLHN	0x05
#define	RLLI	0x06
#define	RRHI	0x07
#define	RRLS	0x08
#define	WCRW	0x0a
#define	WLHO	0x0b
#define	WRHA	0x0c
#define	WRLN	0x0e
#define	OSHT	0x0f
#define	REET	0x10
#define	RIET	0x11
#define	RWOE	0x19
#define	RWOI	0x20
#define	HYS	0x21
#define	QUEUE	0x22
#define	MFID	0xfe
#define	REVID	0xff

/* Status bits */
#define MAX6647_BUSY	(1 << 7)	/* ADC is converting */
#define MAX6647_LHIGH	(1 << 6)	/* Local high temp. alarm */
#define MAX6647_LLOW	(1 << 5)	/* Local low temp. alarm */
#define MAX6647_RHIGH	(1 << 4)	/* Remote high temp. alarm */
#define MAX6647_RLOW	(1 << 3)	/* Remote low temp. alarm */
#define MAX6647_FAULT	(1 << 2)	/* DXN/DXP short/open circuit */
#define MAX6647_EOT	(1 << 1)	/* Remote junction overtemp. */
#define MAX6647_IOT	(1 << 0)	/* Local junction overtemp. */

static const u8 xgphy_max_temperature = 90;

void sfe4001_poweroff(struct efx_nic *efx)
{
	struct efx_i2c_interface *i2c = &efx->i2c;

	u8 cfg, out, in;

	EFX_INFO(efx, "%s\n", __func__);

	/* Turn off all power rails */
	out = 0xff;
	(void) efx_i2c_write(i2c, PCA9539, P0_OUT, &out, 1);

	/* Disable port 1 outputs on IO expander */
	cfg = 0xff;
	(void) efx_i2c_write(i2c, PCA9539, P1_CONFIG, &cfg, 1);

	/* Disable port 0 outputs on IO expander */
	cfg = 0xff;
	(void) efx_i2c_write(i2c, PCA9539, P0_CONFIG, &cfg, 1);

	/* Clear any over-temperature alert */
	(void) efx_i2c_read(i2c, MAX6647, RSL, &in, 1);
}

/* The P0_EN_3V3X line on SFE4001 boards (from A2 onward) is connected
 * to the FLASH_CFG_1 input on the DSP.  We must keep it high at power-
 * up to allow writing the flash (done through MDIO from userland).
 */
unsigned int sfe4001_phy_flash_cfg;
module_param_named(phy_flash_cfg, sfe4001_phy_flash_cfg, uint, 0444);
MODULE_PARM_DESC(phy_flash_cfg,
		 "Force PHY to enter flash configuration mode");

/* This board uses an I2C expander to provider power to the PHY, which needs to
 * be turned on before the PHY can be used.
 * Context: Process context, rtnl lock held
 */
int sfe4001_poweron(struct efx_nic *efx)
{
	struct efx_i2c_interface *i2c = &efx->i2c;
	unsigned int count;
	int rc;
	u8 out, in, cfg;
	efx_dword_t reg;

	/* 10Xpress has fixed-function LED pins, so there is no board-specific
	 * blink code. */
	efx->board_info.blink = tenxpress_phy_blink;

	/* Ensure that XGXS and XAUI SerDes are held in reset */
	EFX_POPULATE_DWORD_7(reg, XX_PWRDNA_EN, 1,
			     XX_PWRDNB_EN, 1,
			     XX_RSTPLLAB_EN, 1,
			     XX_RESETA_EN, 1,
			     XX_RESETB_EN, 1,
			     XX_RSTXGXSRX_EN, 1,
			     XX_RSTXGXSTX_EN, 1);
	falcon_xmac_writel(efx, &reg, XX_PWR_RST_REG_MAC);
	udelay(10);

	/* Set DSP over-temperature alert threshold */
	EFX_INFO(efx, "DSP cut-out at %dC\n", xgphy_max_temperature);
	rc = efx_i2c_write(i2c, MAX6647, WLHO,
			   &xgphy_max_temperature, 1);
	if (rc)
		goto fail1;

	/* Read it back and verify */
	rc = efx_i2c_read(i2c, MAX6647, RLHN, &in, 1);
	if (rc)
		goto fail1;
	if (in != xgphy_max_temperature) {
		rc = -EFAULT;
		goto fail1;
	}

	/* Clear any previous over-temperature alert */
	rc = efx_i2c_read(i2c, MAX6647, RSL, &in, 1);
	if (rc)
		goto fail1;

	/* Enable port 0 and port 1 outputs on IO expander */
	cfg = 0x00;
	rc = efx_i2c_write(i2c, PCA9539, P0_CONFIG, &cfg, 1);
	if (rc)
		goto fail1;
	cfg = 0xff & ~(1 << P1_SPARE_LBN);
	rc = efx_i2c_write(i2c, PCA9539, P1_CONFIG, &cfg, 1);
	if (rc)
		goto fail2;

	/* Turn all power off then wait 1 sec. This ensures PHY is reset */
	out = 0xff & ~((0 << P0_EN_1V2_LBN) | (0 << P0_EN_2V5_LBN) |
		       (0 << P0_EN_3V3X_LBN) | (0 << P0_EN_5V_LBN) |
		       (0 << P0_EN_1V0X_LBN));
	rc = efx_i2c_write(i2c, PCA9539, P0_OUT, &out, 1);
	if (rc)
		goto fail3;

	schedule_timeout_uninterruptible(HZ);
	count = 0;
	do {
		/* Turn on 1.2V, 2.5V, 3.3V and 5V power rails */
		out = 0xff & ~((1 << P0_EN_1V2_LBN) | (1 << P0_EN_2V5_LBN) |
			       (1 << P0_EN_3V3X_LBN) | (1 << P0_EN_5V_LBN) |
			       (1 << P0_X_TRST_LBN));
		if (sfe4001_phy_flash_cfg)
			out |= 1 << P0_EN_3V3X_LBN;

		rc = efx_i2c_write(i2c, PCA9539, P0_OUT, &out, 1);
		if (rc)
			goto fail3;
		msleep(10);

		/* Turn on 1V power rail */
		out &= ~(1 << P0_EN_1V0X_LBN);
		rc = efx_i2c_write(i2c, PCA9539, P0_OUT, &out, 1);
		if (rc)
			goto fail3;

		EFX_INFO(efx, "waiting for power (attempt %d)...\n", count);

		schedule_timeout_uninterruptible(HZ);

		/* Check DSP is powered */
		rc = efx_i2c_read(i2c, PCA9539, P1_IN, &in, 1);
		if (rc)
			goto fail3;
		if (in & (1 << P1_AFE_PWD_LBN))
			goto done;

		/* DSP doesn't look powered in flash config mode */
		if (sfe4001_phy_flash_cfg)
			goto done;
	} while (++count < 20);

	EFX_INFO(efx, "timed out waiting for power\n");
	rc = -ETIMEDOUT;
	goto fail3;

done:
	EFX_INFO(efx, "PHY is powered on\n");
	return 0;

fail3:
	/* Turn off all power rails */
	out = 0xff;
	(void) efx_i2c_write(i2c, PCA9539, P0_OUT, &out, 1);
	/* Disable port 1 outputs on IO expander */
	out = 0xff;
	(void) efx_i2c_write(i2c, PCA9539, P1_CONFIG, &out, 1);
fail2:
	/* Disable port 0 outputs on IO expander */
	out = 0xff;
	(void) efx_i2c_write(i2c, PCA9539, P0_CONFIG, &out, 1);
fail1:
	return rc;
}
