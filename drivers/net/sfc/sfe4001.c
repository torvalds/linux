/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2007-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

/*****************************************************************************
 * Support for the SFE4001 and SFN4111T NICs.
 *
 * The SFE4001 does not power-up fully at reset due to its high power
 * consumption.  We control its power via a PCA9539 I/O expander.
 * Both boards have a MAX6647 temperature monitor which we expose to
 * the lm90 driver.
 *
 * This also provides minimal support for reflashing the PHY, which is
 * initiated by resetting it with the FLASH_CFG_1 pin pulled down.
 * On SFE4001 rev A2 and later this is connected to the 3V3X output of
 * the IO-expander; on the SFN4111T it is connected to Falcon's GPIO3.
 * We represent reflash mode as PHY_MODE_SPECIAL and make it mutually
 * exclusive with the network device being open.
 */

#include <linux/delay.h>
#include "net_driver.h"
#include "efx.h"
#include "phy.h"
#include "boards.h"
#include "falcon.h"
#include "falcon_hwdefs.h"
#include "falcon_io.h"
#include "mac.h"
#include "workarounds.h"

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

/* Temperature Sensor */
#define MAX664X_REG_RSL		0x02
#define MAX664X_REG_WLHO	0x0B

static void sfe4001_poweroff(struct efx_nic *efx)
{
	struct i2c_client *ioexp_client = efx->board_info.ioexp_client;
	struct i2c_client *hwmon_client = efx->board_info.hwmon_client;

	/* Turn off all power rails and disable outputs */
	i2c_smbus_write_byte_data(ioexp_client, P0_OUT, 0xff);
	i2c_smbus_write_byte_data(ioexp_client, P1_CONFIG, 0xff);
	i2c_smbus_write_byte_data(ioexp_client, P0_CONFIG, 0xff);

	/* Clear any over-temperature alert */
	i2c_smbus_read_byte_data(hwmon_client, MAX664X_REG_RSL);
}

static int sfe4001_poweron(struct efx_nic *efx)
{
	struct i2c_client *hwmon_client = efx->board_info.hwmon_client;
	struct i2c_client *ioexp_client = efx->board_info.ioexp_client;
	unsigned int i, j;
	int rc;
	u8 out;

	/* Clear any previous over-temperature alert */
	rc = i2c_smbus_read_byte_data(hwmon_client, MAX664X_REG_RSL);
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

static int sfn4111t_reset(struct efx_nic *efx)
{
	efx_oword_t reg;

	/* GPIO 3 and the GPIO register are shared with I2C, so block that */
	mutex_lock(&efx->i2c_adap.bus_lock);

	/* Pull RST_N (GPIO 2) low then let it up again, setting the
	 * FLASH_CFG_1 strap (GPIO 3) appropriately.  Only change the
	 * output enables; the output levels should always be 0 (low)
	 * and we rely on external pull-ups. */
	falcon_read(efx, &reg, GPIO_CTL_REG_KER);
	EFX_SET_OWORD_FIELD(reg, GPIO2_OEN, true);
	falcon_write(efx, &reg, GPIO_CTL_REG_KER);
	msleep(1000);
	EFX_SET_OWORD_FIELD(reg, GPIO2_OEN, false);
	EFX_SET_OWORD_FIELD(reg, GPIO3_OEN,
			    !!(efx->phy_mode & PHY_MODE_SPECIAL));
	falcon_write(efx, &reg, GPIO_CTL_REG_KER);
	msleep(1);

	mutex_unlock(&efx->i2c_adap.bus_lock);

	ssleep(1);
	return 0;
}

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
		/* Reset the PHY, reconfigure the MAC and enable/disable
		 * MAC stats accordingly. */
		efx->phy_mode = new_mode;
		if (new_mode & PHY_MODE_SPECIAL)
			efx_stats_disable(efx);
		if (efx->board_info.type == EFX_BOARD_SFE4001)
			err = sfe4001_poweron(efx);
		else
			err = sfn4111t_reset(efx);
		efx_reconfigure_port(efx);
		if (!(new_mode & PHY_MODE_SPECIAL))
			efx_stats_enable(efx);
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

static int sfe4001_check_hw(struct efx_nic *efx)
{
	s32 status;

	/* If XAUI link is up then do not monitor */
	if (EFX_WORKAROUND_7884(efx) && efx->mac_up)
		return 0;

	/* Check the powered status of the PHY. Lack of power implies that
	 * the MAX6647 has shut down power to it, probably due to a temp.
	 * alarm. Reading the power status rather than the MAX6647 status
	 * directly because the later is read-to-clear and would thus
	 * start to power up the PHY again when polled, causing us to blip
	 * the power undesirably.
	 * We know we can read from the IO expander because we did
	 * it during power-on. Assume failure now is bad news. */
	status = i2c_smbus_read_byte_data(efx->board_info.ioexp_client, P1_IN);
	if (status >= 0 &&
	    (status & ((1 << P1_AFE_PWD_LBN) | (1 << P1_DSP_PWD25_LBN))) != 0)
		return 0;

	/* Use board power control, not PHY power control */
	sfe4001_poweroff(efx);
	efx->phy_mode = PHY_MODE_OFF;

	return (status < 0) ? -EIO : -ERANGE;
}

static struct i2c_board_info sfe4001_hwmon_info = {
	I2C_BOARD_INFO("max6647", 0x4e),
	.irq		= -1,
};

/* This board uses an I2C expander to provider power to the PHY, which needs to
 * be turned on before the PHY can be used.
 * Context: Process context, rtnl lock held
 */
int sfe4001_init(struct efx_nic *efx)
{
	int rc;

#if defined(CONFIG_SENSORS_LM90) || defined(CONFIG_SENSORS_LM90_MODULE)
	efx->board_info.hwmon_client =
		i2c_new_device(&efx->i2c_adap, &sfe4001_hwmon_info);
#else
	efx->board_info.hwmon_client =
		i2c_new_dummy(&efx->i2c_adap, sfe4001_hwmon_info.addr);
#endif
	if (!efx->board_info.hwmon_client)
		return -EIO;

	/* Raise board/PHY high limit from 85 to 90 degrees Celsius */
	rc = i2c_smbus_write_byte_data(efx->board_info.hwmon_client,
				       MAX664X_REG_WLHO, 90);
	if (rc)
		goto fail_hwmon;

	efx->board_info.ioexp_client = i2c_new_dummy(&efx->i2c_adap, PCA9539);
	if (!efx->board_info.ioexp_client) {
		rc = -EIO;
		goto fail_hwmon;
	}

	/* 10Xpress has fixed-function LED pins, so there is no board-specific
	 * blink code. */
	efx->board_info.blink = tenxpress_phy_blink;

	efx->board_info.monitor = sfe4001_check_hw;
	efx->board_info.fini = sfe4001_fini;

	if (efx->phy_mode & PHY_MODE_SPECIAL) {
		/* PHY won't generate a 156.25 MHz clock and MAC stats fetch
		 * will fail. */
		efx_stats_disable(efx);
	}
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
	i2c_unregister_device(efx->board_info.hwmon_client);
	return rc;
}

static int sfn4111t_check_hw(struct efx_nic *efx)
{
	s32 status;

	/* If XAUI link is up then do not monitor */
	if (EFX_WORKAROUND_7884(efx) && efx->mac_up)
		return 0;

	/* Test LHIGH, RHIGH, FAULT, EOT and IOT alarms */
	status = i2c_smbus_read_byte_data(efx->board_info.hwmon_client,
					  MAX664X_REG_RSL);
	if (status < 0)
		return -EIO;
	if (status & 0x57)
		return -ERANGE;
	return 0;
}

static void sfn4111t_fini(struct efx_nic *efx)
{
	EFX_INFO(efx, "%s\n", __func__);

	device_remove_file(&efx->pci_dev->dev, &dev_attr_phy_flash_cfg);
	i2c_unregister_device(efx->board_info.hwmon_client);
}

static struct i2c_board_info sfn4111t_a0_hwmon_info = {
	I2C_BOARD_INFO("max6647", 0x4e),
	.irq		= -1,
};

static struct i2c_board_info sfn4111t_r5_hwmon_info = {
	I2C_BOARD_INFO("max6646", 0x4d),
	.irq		= -1,
};

int sfn4111t_init(struct efx_nic *efx)
{
	int rc;

	efx->board_info.hwmon_client =
		i2c_new_device(&efx->i2c_adap,
			       (efx->board_info.minor < 5) ?
			       &sfn4111t_a0_hwmon_info :
			       &sfn4111t_r5_hwmon_info);
	if (!efx->board_info.hwmon_client)
		return -EIO;

	efx->board_info.blink = tenxpress_phy_blink;
	efx->board_info.monitor = sfn4111t_check_hw;
	efx->board_info.fini = sfn4111t_fini;

	rc = device_create_file(&efx->pci_dev->dev, &dev_attr_phy_flash_cfg);
	if (rc)
		goto fail_hwmon;

	if (efx->phy_mode & PHY_MODE_SPECIAL) {
		efx_stats_disable(efx);
		sfn4111t_reset(efx);
	}

	return 0;

fail_hwmon:
	i2c_unregister_device(efx->board_info.hwmon_client);
	return rc;
}
