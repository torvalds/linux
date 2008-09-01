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
#include "net_driver.h"
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

static void sfe4001_poweroff(struct efx_nic *efx)
{
	struct i2c_client *ioexp_client = efx->board_info.ioexp_client;
	struct i2c_client *hwmon_client = efx->board_info.hwmon_client;

	/* Turn off all power rails and disable outputs */
	i2c_smbus_write_byte_data(ioexp_client, P0_OUT, 0xff);
	i2c_smbus_write_byte_data(ioexp_client, P1_CONFIG, 0xff);
	i2c_smbus_write_byte_data(ioexp_client, P0_CONFIG, 0xff);

	/* Clear any over-temperature alert */
	i2c_smbus_read_byte_data(hwmon_client, RSL);
}

static int sfe4001_poweron(struct efx_nic *efx)
{
	struct i2c_client *hwmon_client = efx->board_info.hwmon_client;
	struct i2c_client *ioexp_client = efx->board_info.ioexp_client;
	unsigned int i, j;
	int rc;
	u8 out;
	efx_dword_t reg;

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

	/* Clear any previous over-temperature alert */
	rc = i2c_smbus_read_byte_data(hwmon_client, RSL);
	if (rc < 0)
		return rc;

	/* Enable port 0 and port 1 outputs on IO expander */
	rc = i2c_smbus_write_byte_data(ioexp_client, P0_CONFIG, 0x00);
	if (rc)
		return rc;
	rc = i2c_smbus_write_byte_data(ioexp_client, P1_CONFIG,
				       0xff & ~(1 << P1_SPARE_LBN));
	if (rc)
		goto fail_on;

	/* If PHY power is on, turn it all off and wait 1 second to
	 * ensure a full reset.
	 */
	rc = i2c_smbus_read_byte_data(ioexp_client, P0_OUT);
	if (rc < 0)
		goto fail_on;
	out = 0xff & ~((0 << P0_EN_1V2_LBN) | (0 << P0_EN_2V5_LBN) |
		       (0 << P0_EN_3V3X_LBN) | (0 << P0_EN_5V_LBN) |
		       (0 << P0_EN_1V0X_LBN));
	if (rc != out) {
		EFX_INFO(efx, "power-cycling PHY\n");
		rc = i2c_smbus_write_byte_data(ioexp_client, P0_OUT, out);
		if (rc)
			goto fail_on;
		schedule_timeout_uninterruptible(HZ);
	}

	for (i = 0; i < 20; ++i) {
		/* Turn on 1.2V, 2.5V, 3.3V and 5V power rails */
		out = 0xff & ~((1 << P0_EN_1V2_LBN) | (1 << P0_EN_2V5_LBN) |
			       (1 << P0_EN_3V3X_LBN) | (1 << P0_EN_5V_LBN) |
			       (1 << P0_X_TRST_LBN));
		if (efx->phy_mode & PHY_MODE_SPECIAL)
			out |= 1 << P0_EN_3V3X_LBN;

		rc = i2c_smbus_write_byte_data(ioexp_client, P0_OUT, out);
		if (rc)
			goto fail_on;
		msleep(10);

		/* Turn on 1V power rail */
		out &= ~(1 << P0_EN_1V0X_LBN);
		rc = i2c_smbus_write_byte_data(ioexp_client, P0_OUT, out);
		if (rc)
			goto fail_on;

		EFX_INFO(efx, "waiting for DSP boot (attempt %d)...\n", i);

		/* In flash config mode, DSP does not turn on AFE, so
		 * just wait 1 second.
		 */
		if (efx->phy_mode & PHY_MODE_SPECIAL) {
			schedule_timeout_uninterruptible(HZ);
			return 0;
		}

		for (j = 0; j < 10; ++j) {
			msleep(100);

			/* Check DSP has asserted AFE power line */
			rc = i2c_smbus_read_byte_data(ioexp_client, P1_IN);
			if (rc < 0)
				goto fail_on;
			if (rc & (1 << P1_AFE_PWD_LBN))
				return 0;
		}
	}

	EFX_INFO(efx, "timed out waiting for DSP boot\n");
	rc = -ETIMEDOUT;
fail_on:
	sfe4001_poweroff(efx);
	return rc;
}

/* On SFE4001 rev A2 and later, we can control the FLASH_CFG_1 pin
 * using the 3V3X output of the IO-expander.  Allow the user to set
 * this when the device is stopped, and keep it stopped then.
 */

static ssize_t show_phy_flash_cfg(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct efx_nic *efx = pci_get_drvdata(to_pci_dev(dev));
	return sprintf(buf, "%d\n", !!(efx->phy_mode & PHY_MODE_SPECIAL));
}

static ssize_t set_phy_flash_cfg(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct efx_nic *efx = pci_get_drvdata(to_pci_dev(dev));
	enum efx_phy_mode old_mode, new_mode;
	int err;

	rtnl_lock();
	old_mode = efx->phy_mode;
	if (count == 0 || *buf == '0')
		new_mode = old_mode & ~PHY_MODE_SPECIAL;
	else
		new_mode = PHY_MODE_SPECIAL;
	if (old_mode == new_mode) {
		err = 0;
	} else if (efx->state != STATE_RUNNING || netif_running(efx->net_dev)) {
		err = -EBUSY;
	} else {
		efx->phy_mode = new_mode;
		err = sfe4001_poweron(efx);
		efx_reconfigure_port(efx);
	}
	rtnl_unlock();

	return err ? err : count;
}

static DEVICE_ATTR(phy_flash_cfg, 0644, show_phy_flash_cfg, set_phy_flash_cfg);

static void sfe4001_fini(struct efx_nic *efx)
{
	EFX_INFO(efx, "%s\n", __func__);

	device_remove_file(&efx->pci_dev->dev, &dev_attr_phy_flash_cfg);
	sfe4001_poweroff(efx);
	i2c_unregister_device(efx->board_info.ioexp_client);
	i2c_unregister_device(efx->board_info.hwmon_client);
}

/* This board uses an I2C expander to provider power to the PHY, which needs to
 * be turned on before the PHY can be used.
 * Context: Process context, rtnl lock held
 */
int sfe4001_init(struct efx_nic *efx)
{
	struct i2c_client *hwmon_client;
	int rc;

	hwmon_client = i2c_new_dummy(&efx->i2c_adap, MAX6647);
	if (!hwmon_client)
		return -EIO;
	efx->board_info.hwmon_client = hwmon_client;

	/* Set DSP over-temperature alert threshold */
	EFX_INFO(efx, "DSP cut-out at %dC\n", xgphy_max_temperature);
	rc = i2c_smbus_write_byte_data(hwmon_client, WLHO,
				       xgphy_max_temperature);
	if (rc)
		goto fail_ioexp;

	/* Read it back and verify */
	rc = i2c_smbus_read_byte_data(hwmon_client, RLHN);
	if (rc < 0)
		goto fail_ioexp;
	if (rc != xgphy_max_temperature) {
		rc = -EFAULT;
		goto fail_ioexp;
	}

	efx->board_info.ioexp_client = i2c_new_dummy(&efx->i2c_adap, PCA9539);
	if (!efx->board_info.ioexp_client) {
		rc = -EIO;
		goto fail_hwmon;
	}

	/* 10Xpress has fixed-function LED pins, so there is no board-specific
	 * blink code. */
	efx->board_info.blink = tenxpress_phy_blink;

	efx->board_info.fini = sfe4001_fini;

	rc = sfe4001_poweron(efx);
	if (rc)
		goto fail_ioexp;

	rc = device_create_file(&efx->pci_dev->dev, &dev_attr_phy_flash_cfg);
	if (rc)
		goto fail_on;

	EFX_INFO(efx, "PHY is powered on\n");
	return 0;

fail_on:
	sfe4001_poweroff(efx);
fail_ioexp:
	i2c_unregister_device(efx->board_info.ioexp_client);
fail_hwmon:
	i2c_unregister_device(hwmon_client);
	return rc;
}
