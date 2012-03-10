/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2007-2010 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/rtnetlink.h>

#include "net_driver.h"
#include "phy.h"
#include "efx.h"
#include "nic.h"
#include "workarounds.h"

/* Macros for unpacking the board revision */
/* The revision info is in host byte order. */
#define FALCON_BOARD_TYPE(_rev) (_rev >> 8)
#define FALCON_BOARD_MAJOR(_rev) ((_rev >> 4) & 0xf)
#define FALCON_BOARD_MINOR(_rev) (_rev & 0xf)

/* Board types */
#define FALCON_BOARD_SFE4001 0x01
#define FALCON_BOARD_SFE4002 0x02
#define FALCON_BOARD_SFE4003 0x03
#define FALCON_BOARD_SFN4112F 0x52

/* Board temperature is about 15°C above ambient when air flow is
 * limited.  The maximum acceptable ambient temperature varies
 * depending on the PHY specifications but the critical temperature
 * above which we should shut down to avoid damage is 80°C. */
#define FALCON_BOARD_TEMP_BIAS	15
#define FALCON_BOARD_TEMP_CRIT	(80 + FALCON_BOARD_TEMP_BIAS)

/* SFC4000 datasheet says: 'The maximum permitted junction temperature
 * is 125°C; the thermal design of the environment for the SFC4000
 * should aim to keep this well below 100°C.' */
#define FALCON_JUNC_TEMP_MIN	0
#define FALCON_JUNC_TEMP_MAX	90
#define FALCON_JUNC_TEMP_CRIT	125

/*****************************************************************************
 * Support for LM87 sensor chip used on several boards
 */
#define LM87_REG_TEMP_HW_INT_LOCK	0x13
#define LM87_REG_TEMP_HW_EXT_LOCK	0x14
#define LM87_REG_TEMP_HW_INT		0x17
#define LM87_REG_TEMP_HW_EXT		0x18
#define LM87_REG_TEMP_EXT1		0x26
#define LM87_REG_TEMP_INT		0x27
#define LM87_REG_ALARMS1		0x41
#define LM87_REG_ALARMS2		0x42
#define LM87_IN_LIMITS(nr, _min, _max)			\
	0x2B + (nr) * 2, _max, 0x2C + (nr) * 2, _min
#define LM87_AIN_LIMITS(nr, _min, _max)			\
	0x3B + (nr), _max, 0x1A + (nr), _min
#define LM87_TEMP_INT_LIMITS(_min, _max)		\
	0x39, _max, 0x3A, _min
#define LM87_TEMP_EXT1_LIMITS(_min, _max)		\
	0x37, _max, 0x38, _min

#define LM87_ALARM_TEMP_INT		0x10
#define LM87_ALARM_TEMP_EXT1		0x20

#if defined(CONFIG_SENSORS_LM87) || defined(CONFIG_SENSORS_LM87_MODULE)

static int efx_poke_lm87(struct i2c_client *client, const u8 *reg_values)
{
	while (*reg_values) {
		u8 reg = *reg_values++;
		u8 value = *reg_values++;
		int rc = i2c_smbus_write_byte_data(client, reg, value);
		if (rc)
			return rc;
	}
	return 0;
}

static const u8 falcon_lm87_common_regs[] = {
	LM87_REG_TEMP_HW_INT_LOCK, FALCON_BOARD_TEMP_CRIT,
	LM87_REG_TEMP_HW_INT, FALCON_BOARD_TEMP_CRIT,
	LM87_TEMP_EXT1_LIMITS(FALCON_JUNC_TEMP_MIN, FALCON_JUNC_TEMP_MAX),
	LM87_REG_TEMP_HW_EXT_LOCK, FALCON_JUNC_TEMP_CRIT,
	LM87_REG_TEMP_HW_EXT, FALCON_JUNC_TEMP_CRIT,
	0
};

static int efx_init_lm87(struct efx_nic *efx, const struct i2c_board_info *info,
			 const u8 *reg_values)
{
	struct falcon_board *board = falcon_board(efx);
	struct i2c_client *client = i2c_new_device(&board->i2c_adap, info);
	int rc;

	if (!client)
		return -EIO;

	/* Read-to-clear alarm/interrupt status */
	i2c_smbus_read_byte_data(client, LM87_REG_ALARMS1);
	i2c_smbus_read_byte_data(client, LM87_REG_ALARMS2);

	rc = efx_poke_lm87(client, reg_values);
	if (rc)
		goto err;
	rc = efx_poke_lm87(client, falcon_lm87_common_regs);
	if (rc)
		goto err;

	board->hwmon_client = client;
	return 0;

err:
	i2c_unregister_device(client);
	return rc;
}

static void efx_fini_lm87(struct efx_nic *efx)
{
	i2c_unregister_device(falcon_board(efx)->hwmon_client);
}

static int efx_check_lm87(struct efx_nic *efx, unsigned mask)
{
	struct i2c_client *client = falcon_board(efx)->hwmon_client;
	bool temp_crit, elec_fault, is_failure;
	u16 alarms;
	s32 reg;

	/* If link is up then do not monitor temperature */
	if (EFX_WORKAROUND_7884(efx) && efx->link_state.up)
		return 0;

	reg = i2c_smbus_read_byte_data(client, LM87_REG_ALARMS1);
	if (reg < 0)
		return reg;
	alarms = reg;
	reg = i2c_smbus_read_byte_data(client, LM87_REG_ALARMS2);
	if (reg < 0)
		return reg;
	alarms |= reg << 8;
	alarms &= mask;

	temp_crit = false;
	if (alarms & LM87_ALARM_TEMP_INT) {
		reg = i2c_smbus_read_byte_data(client, LM87_REG_TEMP_INT);
		if (reg < 0)
			return reg;
		if (reg > FALCON_BOARD_TEMP_CRIT)
			temp_crit = true;
	}
	if (alarms & LM87_ALARM_TEMP_EXT1) {
		reg = i2c_smbus_read_byte_data(client, LM87_REG_TEMP_EXT1);
		if (reg < 0)
			return reg;
		if (reg > FALCON_JUNC_TEMP_CRIT)
			temp_crit = true;
	}
	elec_fault = alarms & ~(LM87_ALARM_TEMP_INT | LM87_ALARM_TEMP_EXT1);
	is_failure = temp_crit || elec_fault;

	if (alarms)
		netif_err(efx, hw, efx->net_dev,
			  "LM87 detected a hardware %s (status %02x:%02x)"
			  "%s%s%s%s\n",
			  is_failure ? "failure" : "problem",
			  alarms & 0xff, alarms >> 8,
			  (alarms & LM87_ALARM_TEMP_INT) ?
			  "; board is overheating" : "",
			  (alarms & LM87_ALARM_TEMP_EXT1) ?
			  "; controller is overheating" : "",
			  temp_crit ? "; reached critical temperature" : "",
			  elec_fault ? "; electrical fault" : "");

	return is_failure ? -ERANGE : 0;
}

#else /* !CONFIG_SENSORS_LM87 */

static inline int
efx_init_lm87(struct efx_nic *efx, const struct i2c_board_info *info,
	      const u8 *reg_values)
{
	return 0;
}
static inline void efx_fini_lm87(struct efx_nic *efx)
{
}
static inline int efx_check_lm87(struct efx_nic *efx, unsigned mask)
{
	return 0;
}

#endif /* CONFIG_SENSORS_LM87 */

/*****************************************************************************
 * Support for the SFE4001 NIC.
 *
 * The SFE4001 does not power-up fully at reset due to its high power
 * consumption.  We control its power via a PCA9539 I/O expander.
 * It also has a MAX6647 temperature monitor which we expose to
 * the lm90 driver.
 *
 * This also provides minimal support for reflashing the PHY, which is
 * initiated by resetting it with the FLASH_CFG_1 pin pulled down.
 * On SFE4001 rev A2 and later this is connected to the 3V3X output of
 * the IO-expander.
 * We represent reflash mode as PHY_MODE_SPECIAL and make it mutually
 * exclusive with the network device being open.
 */

/**************************************************************************
 * Support for I2C IO Expander device on SFE4001
 */
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
	struct i2c_client *ioexp_client = falcon_board(efx)->ioexp_client;
	struct i2c_client *hwmon_client = falcon_board(efx)->hwmon_client;

	/* Turn off all power rails and disable outputs */
	i2c_smbus_write_byte_data(ioexp_client, P0_OUT, 0xff);
	i2c_smbus_write_byte_data(ioexp_client, P1_CONFIG, 0xff);
	i2c_smbus_write_byte_data(ioexp_client, P0_CONFIG, 0xff);

	/* Clear any over-temperature alert */
	i2c_smbus_read_byte_data(hwmon_client, MAX664X_REG_RSL);
}

static int sfe4001_poweron(struct efx_nic *efx)
{
	struct i2c_client *ioexp_client = falcon_board(efx)->ioexp_client;
	struct i2c_client *hwmon_client = falcon_board(efx)->hwmon_client;
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
		netif_info(efx, hw, efx->net_dev, "power-cycling PHY\n");
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

		netif_info(efx, hw, efx->net_dev,
			   "waiting for DSP boot (attempt %d)...\n", i);

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

	netif_info(efx, hw, efx->net_dev, "timed out waiting for DSP boot\n");
	rc = -ETIMEDOUT;
fail_on:
	sfe4001_poweroff(efx);
	return rc;
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
	if (!((old_mode ^ new_mode) & PHY_MODE_SPECIAL)) {
		err = 0;
	} else if (efx->state != STATE_RUNNING || netif_running(efx->net_dev)) {
		err = -EBUSY;
	} else {
		/* Reset the PHY, reconfigure the MAC and enable/disable
		 * MAC stats accordingly. */
		efx->phy_mode = new_mode;
		if (new_mode & PHY_MODE_SPECIAL)
			falcon_stop_nic_stats(efx);
		err = sfe4001_poweron(efx);
		if (!err)
			err = efx_reconfigure_port(efx);
		if (!(new_mode & PHY_MODE_SPECIAL))
			falcon_start_nic_stats(efx);
	}
	rtnl_unlock();

	return err ? err : count;
}

static DEVICE_ATTR(phy_flash_cfg, 0644, show_phy_flash_cfg, set_phy_flash_cfg);

static void sfe4001_fini(struct efx_nic *efx)
{
	struct falcon_board *board = falcon_board(efx);

	netif_info(efx, drv, efx->net_dev, "%s\n", __func__);

	device_remove_file(&efx->pci_dev->dev, &dev_attr_phy_flash_cfg);
	sfe4001_poweroff(efx);
	i2c_unregister_device(board->ioexp_client);
	i2c_unregister_device(board->hwmon_client);
}

static int sfe4001_check_hw(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	s32 status;

	/* If XAUI link is up then do not monitor */
	if (EFX_WORKAROUND_7884(efx) && !nic_data->xmac_poll_required)
		return 0;

	/* Check the powered status of the PHY. Lack of power implies that
	 * the MAX6647 has shut down power to it, probably due to a temp.
	 * alarm. Reading the power status rather than the MAX6647 status
	 * directly because the later is read-to-clear and would thus
	 * start to power up the PHY again when polled, causing us to blip
	 * the power undesirably.
	 * We know we can read from the IO expander because we did
	 * it during power-on. Assume failure now is bad news. */
	status = i2c_smbus_read_byte_data(falcon_board(efx)->ioexp_client, P1_IN);
	if (status >= 0 &&
	    (status & ((1 << P1_AFE_PWD_LBN) | (1 << P1_DSP_PWD25_LBN))) != 0)
		return 0;

	/* Use board power control, not PHY power control */
	sfe4001_poweroff(efx);
	efx->phy_mode = PHY_MODE_OFF;

	return (status < 0) ? -EIO : -ERANGE;
}

static const struct i2c_board_info sfe4001_hwmon_info = {
	I2C_BOARD_INFO("max6647", 0x4e),
};

/* This board uses an I2C expander to provider power to the PHY, which needs to
 * be turned on before the PHY can be used.
 * Context: Process context, rtnl lock held
 */
static int sfe4001_init(struct efx_nic *efx)
{
	struct falcon_board *board = falcon_board(efx);
	int rc;

#if defined(CONFIG_SENSORS_LM90) || defined(CONFIG_SENSORS_LM90_MODULE)
	board->hwmon_client =
		i2c_new_device(&board->i2c_adap, &sfe4001_hwmon_info);
#else
	board->hwmon_client =
		i2c_new_dummy(&board->i2c_adap, sfe4001_hwmon_info.addr);
#endif
	if (!board->hwmon_client)
		return -EIO;

	/* Raise board/PHY high limit from 85 to 90 degrees Celsius */
	rc = i2c_smbus_write_byte_data(board->hwmon_client,
				       MAX664X_REG_WLHO, 90);
	if (rc)
		goto fail_hwmon;

	board->ioexp_client = i2c_new_dummy(&board->i2c_adap, PCA9539);
	if (!board->ioexp_client) {
		rc = -EIO;
		goto fail_hwmon;
	}

	if (efx->phy_mode & PHY_MODE_SPECIAL) {
		/* PHY won't generate a 156.25 MHz clock and MAC stats fetch
		 * will fail. */
		falcon_stop_nic_stats(efx);
	}
	rc = sfe4001_poweron(efx);
	if (rc)
		goto fail_ioexp;

	rc = device_create_file(&efx->pci_dev->dev, &dev_attr_phy_flash_cfg);
	if (rc)
		goto fail_on;

	netif_info(efx, hw, efx->net_dev, "PHY is powered on\n");
	return 0;

fail_on:
	sfe4001_poweroff(efx);
fail_ioexp:
	i2c_unregister_device(board->ioexp_client);
fail_hwmon:
	i2c_unregister_device(board->hwmon_client);
	return rc;
}

/*****************************************************************************
 * Support for the SFE4002
 *
 */
static u8 sfe4002_lm87_channel = 0x03; /* use AIN not FAN inputs */

static const u8 sfe4002_lm87_regs[] = {
	LM87_IN_LIMITS(0, 0x7c, 0x99),		/* 2.5V:  1.8V +/- 10% */
	LM87_IN_LIMITS(1, 0x4c, 0x5e),		/* Vccp1: 1.2V +/- 10% */
	LM87_IN_LIMITS(2, 0xac, 0xd4),		/* 3.3V:  3.3V +/- 10% */
	LM87_IN_LIMITS(3, 0xac, 0xd4),		/* 5V:    5.0V +/- 10% */
	LM87_IN_LIMITS(4, 0xac, 0xe0),		/* 12V:   10.8-14V */
	LM87_IN_LIMITS(5, 0x3f, 0x4f),		/* Vccp2: 1.0V +/- 10% */
	LM87_AIN_LIMITS(0, 0x98, 0xbb),		/* AIN1:  1.66V +/- 10% */
	LM87_AIN_LIMITS(1, 0x8a, 0xa9),		/* AIN2:  1.5V +/- 10% */
	LM87_TEMP_INT_LIMITS(0, 80 + FALCON_BOARD_TEMP_BIAS),
	LM87_TEMP_EXT1_LIMITS(0, FALCON_JUNC_TEMP_MAX),
	0
};

static const struct i2c_board_info sfe4002_hwmon_info = {
	I2C_BOARD_INFO("lm87", 0x2e),
	.platform_data	= &sfe4002_lm87_channel,
};

/****************************************************************************/
/* LED allocations. Note that on rev A0 boards the schematic and the reality
 * differ: red and green are swapped. Below is the fixed (A1) layout (there
 * are only 3 A0 boards in existence, so no real reason to make this
 * conditional).
 */
#define SFE4002_FAULT_LED (2)	/* Red */
#define SFE4002_RX_LED    (0)	/* Green */
#define SFE4002_TX_LED    (1)	/* Amber */

static void sfe4002_init_phy(struct efx_nic *efx)
{
	/* Set the TX and RX LEDs to reflect status and activity, and the
	 * fault LED off */
	falcon_qt202x_set_led(efx, SFE4002_TX_LED,
			      QUAKE_LED_TXLINK | QUAKE_LED_LINK_ACTSTAT);
	falcon_qt202x_set_led(efx, SFE4002_RX_LED,
			      QUAKE_LED_RXLINK | QUAKE_LED_LINK_ACTSTAT);
	falcon_qt202x_set_led(efx, SFE4002_FAULT_LED, QUAKE_LED_OFF);
}

static void sfe4002_set_id_led(struct efx_nic *efx, enum efx_led_mode mode)
{
	falcon_qt202x_set_led(
		efx, SFE4002_FAULT_LED,
		(mode == EFX_LED_ON) ? QUAKE_LED_ON : QUAKE_LED_OFF);
}

static int sfe4002_check_hw(struct efx_nic *efx)
{
	struct falcon_board *board = falcon_board(efx);

	/* A0 board rev. 4002s report a temperature fault the whole time
	 * (bad sensor) so we mask it out. */
	unsigned alarm_mask =
		(board->major == 0 && board->minor == 0) ?
		~LM87_ALARM_TEMP_EXT1 : ~0;

	return efx_check_lm87(efx, alarm_mask);
}

static int sfe4002_init(struct efx_nic *efx)
{
	return efx_init_lm87(efx, &sfe4002_hwmon_info, sfe4002_lm87_regs);
}

/*****************************************************************************
 * Support for the SFN4112F
 *
 */
static u8 sfn4112f_lm87_channel = 0x03; /* use AIN not FAN inputs */

static const u8 sfn4112f_lm87_regs[] = {
	LM87_IN_LIMITS(0, 0x7c, 0x99),		/* 2.5V:  1.8V +/- 10% */
	LM87_IN_LIMITS(1, 0x4c, 0x5e),		/* Vccp1: 1.2V +/- 10% */
	LM87_IN_LIMITS(2, 0xac, 0xd4),		/* 3.3V:  3.3V +/- 10% */
	LM87_IN_LIMITS(4, 0xac, 0xe0),		/* 12V:   10.8-14V */
	LM87_IN_LIMITS(5, 0x3f, 0x4f),		/* Vccp2: 1.0V +/- 10% */
	LM87_AIN_LIMITS(1, 0x8a, 0xa9),		/* AIN2:  1.5V +/- 10% */
	LM87_TEMP_INT_LIMITS(0, 60 + FALCON_BOARD_TEMP_BIAS),
	LM87_TEMP_EXT1_LIMITS(0, FALCON_JUNC_TEMP_MAX),
	0
};

static const struct i2c_board_info sfn4112f_hwmon_info = {
	I2C_BOARD_INFO("lm87", 0x2e),
	.platform_data	= &sfn4112f_lm87_channel,
};

#define SFN4112F_ACT_LED	0
#define SFN4112F_LINK_LED	1

static void sfn4112f_init_phy(struct efx_nic *efx)
{
	falcon_qt202x_set_led(efx, SFN4112F_ACT_LED,
			      QUAKE_LED_RXLINK | QUAKE_LED_LINK_ACT);
	falcon_qt202x_set_led(efx, SFN4112F_LINK_LED,
			      QUAKE_LED_RXLINK | QUAKE_LED_LINK_STAT);
}

static void sfn4112f_set_id_led(struct efx_nic *efx, enum efx_led_mode mode)
{
	int reg;

	switch (mode) {
	case EFX_LED_OFF:
		reg = QUAKE_LED_OFF;
		break;
	case EFX_LED_ON:
		reg = QUAKE_LED_ON;
		break;
	default:
		reg = QUAKE_LED_RXLINK | QUAKE_LED_LINK_STAT;
		break;
	}

	falcon_qt202x_set_led(efx, SFN4112F_LINK_LED, reg);
}

static int sfn4112f_check_hw(struct efx_nic *efx)
{
	/* Mask out unused sensors */
	return efx_check_lm87(efx, ~0x48);
}

static int sfn4112f_init(struct efx_nic *efx)
{
	return efx_init_lm87(efx, &sfn4112f_hwmon_info, sfn4112f_lm87_regs);
}

/*****************************************************************************
 * Support for the SFE4003
 *
 */
static u8 sfe4003_lm87_channel = 0x03; /* use AIN not FAN inputs */

static const u8 sfe4003_lm87_regs[] = {
	LM87_IN_LIMITS(0, 0x67, 0x7f),		/* 2.5V:  1.5V +/- 10% */
	LM87_IN_LIMITS(1, 0x4c, 0x5e),		/* Vccp1: 1.2V +/- 10% */
	LM87_IN_LIMITS(2, 0xac, 0xd4),		/* 3.3V:  3.3V +/- 10% */
	LM87_IN_LIMITS(4, 0xac, 0xe0),		/* 12V:   10.8-14V */
	LM87_IN_LIMITS(5, 0x3f, 0x4f),		/* Vccp2: 1.0V +/- 10% */
	LM87_TEMP_INT_LIMITS(0, 70 + FALCON_BOARD_TEMP_BIAS),
	0
};

static const struct i2c_board_info sfe4003_hwmon_info = {
	I2C_BOARD_INFO("lm87", 0x2e),
	.platform_data	= &sfe4003_lm87_channel,
};

/* Board-specific LED info. */
#define SFE4003_RED_LED_GPIO	11
#define SFE4003_LED_ON		1
#define SFE4003_LED_OFF		0

static void sfe4003_set_id_led(struct efx_nic *efx, enum efx_led_mode mode)
{
	struct falcon_board *board = falcon_board(efx);

	/* The LEDs were not wired to GPIOs before A3 */
	if (board->minor < 3 && board->major == 0)
		return;

	falcon_txc_set_gpio_val(
		efx, SFE4003_RED_LED_GPIO,
		(mode == EFX_LED_ON) ? SFE4003_LED_ON : SFE4003_LED_OFF);
}

static void sfe4003_init_phy(struct efx_nic *efx)
{
	struct falcon_board *board = falcon_board(efx);

	/* The LEDs were not wired to GPIOs before A3 */
	if (board->minor < 3 && board->major == 0)
		return;

	falcon_txc_set_gpio_dir(efx, SFE4003_RED_LED_GPIO, TXC_GPIO_DIR_OUTPUT);
	falcon_txc_set_gpio_val(efx, SFE4003_RED_LED_GPIO, SFE4003_LED_OFF);
}

static int sfe4003_check_hw(struct efx_nic *efx)
{
	struct falcon_board *board = falcon_board(efx);

	/* A0/A1/A2 board rev. 4003s  report a temperature fault the whole time
	 * (bad sensor) so we mask it out. */
	unsigned alarm_mask =
		(board->major == 0 && board->minor <= 2) ?
		~LM87_ALARM_TEMP_EXT1 : ~0;

	return efx_check_lm87(efx, alarm_mask);
}

static int sfe4003_init(struct efx_nic *efx)
{
	return efx_init_lm87(efx, &sfe4003_hwmon_info, sfe4003_lm87_regs);
}

static const struct falcon_board_type board_types[] = {
	{
		.id		= FALCON_BOARD_SFE4001,
		.init		= sfe4001_init,
		.init_phy	= efx_port_dummy_op_void,
		.fini		= sfe4001_fini,
		.set_id_led	= tenxpress_set_id_led,
		.monitor	= sfe4001_check_hw,
	},
	{
		.id		= FALCON_BOARD_SFE4002,
		.init		= sfe4002_init,
		.init_phy	= sfe4002_init_phy,
		.fini		= efx_fini_lm87,
		.set_id_led	= sfe4002_set_id_led,
		.monitor	= sfe4002_check_hw,
	},
	{
		.id		= FALCON_BOARD_SFE4003,
		.init		= sfe4003_init,
		.init_phy	= sfe4003_init_phy,
		.fini		= efx_fini_lm87,
		.set_id_led	= sfe4003_set_id_led,
		.monitor	= sfe4003_check_hw,
	},
	{
		.id		= FALCON_BOARD_SFN4112F,
		.init		= sfn4112f_init,
		.init_phy	= sfn4112f_init_phy,
		.fini		= efx_fini_lm87,
		.set_id_led	= sfn4112f_set_id_led,
		.monitor	= sfn4112f_check_hw,
	},
};

int falcon_probe_board(struct efx_nic *efx, u16 revision_info)
{
	struct falcon_board *board = falcon_board(efx);
	u8 type_id = FALCON_BOARD_TYPE(revision_info);
	int i;

	board->major = FALCON_BOARD_MAJOR(revision_info);
	board->minor = FALCON_BOARD_MINOR(revision_info);

	for (i = 0; i < ARRAY_SIZE(board_types); i++)
		if (board_types[i].id == type_id)
			board->type = &board_types[i];

	if (board->type) {
		return 0;
	} else {
		netif_err(efx, probe, efx->net_dev, "unknown board type %d\n",
			  type_id);
		return -ENODEV;
	}
}
