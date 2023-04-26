// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Silicon Labs Si2146/2147/2148/2157/2158 silicon tuner driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
 */

#include "si2157_priv.h"

static const struct dvb_tuner_ops si2157_ops;

static int tuner_lock_debug;
module_param(tuner_lock_debug, int, 0644);
MODULE_PARM_DESC(tuner_lock_debug, "if set, signal lock is briefly waited on after setting params");

/* execute firmware command */
static int si2157_cmd_execute(struct i2c_client *client, struct si2157_cmd *cmd)
{
	struct si2157_dev *dev = i2c_get_clientdata(client);
	int ret;
	unsigned long timeout;

	mutex_lock(&dev->i2c_mutex);

	if (cmd->wlen) {
		/* write cmd and args for firmware */
		ret = i2c_master_send(client, cmd->args, cmd->wlen);
		if (ret < 0) {
			goto err_mutex_unlock;
		} else if (ret != cmd->wlen) {
			ret = -EREMOTEIO;
			goto err_mutex_unlock;
		}
	}

	if (cmd->rlen) {
		/* wait cmd execution terminate */
		#define TIMEOUT 80
		timeout = jiffies + msecs_to_jiffies(TIMEOUT);
		while (!time_after(jiffies, timeout)) {
			ret = i2c_master_recv(client, cmd->args, cmd->rlen);
			if (ret < 0) {
				goto err_mutex_unlock;
			} else if (ret != cmd->rlen) {
				ret = -EREMOTEIO;
				goto err_mutex_unlock;
			}

			/* firmware ready? */
			if ((cmd->args[0] >> 7) & 0x01)
				break;
		}

		dev_dbg(&client->dev, "cmd execution took %d ms, status=%x\n",
			jiffies_to_msecs(jiffies) -
			(jiffies_to_msecs(timeout) - TIMEOUT),
			cmd->args[0]);

		if (!((cmd->args[0] >> 7) & 0x01)) {
			ret = -ETIMEDOUT;
			goto err_mutex_unlock;
		}
		/* check error status bit */
		if (cmd->args[0] & 0x40) {
			ret = -EAGAIN;
			goto err_mutex_unlock;
		}
	}

	mutex_unlock(&dev->i2c_mutex);
	return 0;

err_mutex_unlock:
	mutex_unlock(&dev->i2c_mutex);
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static const struct si2157_tuner_info si2157_tuners[] = {
	{ SI2141, 0x60, false, SI2141_60_FIRMWARE, SI2141_A10_FIRMWARE },
	{ SI2141, 0x61, false, SI2141_61_FIRMWARE, SI2141_A10_FIRMWARE },
	{ SI2146, 0x11, false, SI2146_11_FIRMWARE, NULL },
	{ SI2147, 0x50, false, SI2147_50_FIRMWARE, NULL },
	{ SI2148, 0x32, true,  SI2148_32_FIRMWARE, SI2158_A20_FIRMWARE },
	{ SI2148, 0x33, true,  SI2148_33_FIRMWARE, SI2158_A20_FIRMWARE },
	{ SI2157, 0x50, false, SI2157_50_FIRMWARE, SI2157_A30_FIRMWARE },
	{ SI2158, 0x50, false, SI2158_50_FIRMWARE, SI2158_A20_FIRMWARE },
	{ SI2158, 0x51, false, SI2158_51_FIRMWARE, SI2158_A20_FIRMWARE },
	{ SI2177, 0x50, false, SI2177_50_FIRMWARE, SI2157_A30_FIRMWARE },
};

static int si2157_load_firmware(struct dvb_frontend *fe,
				const char *fw_name)
{
	struct i2c_client *client = fe->tuner_priv;
	const struct firmware *fw;
	int ret, len, remaining;
	struct si2157_cmd cmd;

	/* request the firmware, this will block and timeout */
	ret = firmware_request_nowarn(&fw, fw_name, &client->dev);
	if (ret)
		return ret;

	/* firmware should be n chunks of 17 bytes */
	if (fw->size % 17 != 0) {
		dev_err(&client->dev, "firmware file '%s' is invalid\n",
			fw_name);
		ret = -EINVAL;
		goto err_release_firmware;
	}

	dev_info(&client->dev, "downloading firmware from file '%s'\n",
		 fw_name);

	for (remaining = fw->size; remaining > 0; remaining -= 17) {
		len = fw->data[fw->size - remaining];
		if (len > SI2157_ARGLEN) {
			dev_err(&client->dev, "Bad firmware length\n");
			ret = -EINVAL;
			goto err_release_firmware;
		}
		memcpy(cmd.args, &fw->data[(fw->size - remaining) + 1], len);
		cmd.wlen = len;
		cmd.rlen = 1;
		ret = si2157_cmd_execute(client, &cmd);
		if (ret) {
			dev_err(&client->dev, "firmware download failed %d\n",
					ret);
			goto err_release_firmware;
		}
	}

err_release_firmware:
	release_firmware(fw);

	return ret;
}

static int si2157_find_and_load_firmware(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->tuner_priv;
	struct si2157_dev *dev = i2c_get_clientdata(client);
	const char *fw_alt_name = NULL;
	unsigned char part_id, rom_id;
	const char *fw_name = NULL;
	struct si2157_cmd cmd;
	bool required = true;
	int ret, i;

	if (dev->dont_load_firmware) {
		dev_info(&client->dev,
			 "device is buggy, skipping firmware download\n");
		return 0;
	}

	/* query chip revision */
	memcpy(cmd.args, "\x02", 1);
	cmd.wlen = 1;
	cmd.rlen = 13;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		return ret;

	part_id = cmd.args[2];
	rom_id = cmd.args[12];

	for (i = 0; i < ARRAY_SIZE(si2157_tuners); i++) {
		if (si2157_tuners[i].part_id != part_id)
			continue;
		required = si2157_tuners[i].required;
		fw_alt_name = si2157_tuners[i].fw_alt_name;

		/* Both part and rom ID match */
		if (si2157_tuners[i].rom_id == rom_id) {
			fw_name = si2157_tuners[i].fw_name;
			break;
		}
	}

	if (required && !fw_name && !fw_alt_name) {
		dev_err(&client->dev,
			"unknown chip version Si21%d-%c%c%c ROM 0x%02x\n",
			part_id, cmd.args[1], cmd.args[3], cmd.args[4], rom_id);
		return -EINVAL;
	}

	/* Update the part id based on device's report */
	dev->part_id = part_id;

	dev_info(&client->dev,
		 "found a 'Silicon Labs Si21%d-%c%c%c ROM 0x%02x'\n",
		 part_id, cmd.args[1], cmd.args[3], cmd.args[4], rom_id);

	if (fw_name)
		ret = si2157_load_firmware(fe, fw_name);
	else
		ret = -ENOENT;

	/* Try alternate name, if any */
	if (ret == -ENOENT && fw_alt_name)
		ret = si2157_load_firmware(fe, fw_alt_name);

	if (ret == -ENOENT) {
		if (!required) {
			dev_info(&client->dev, "Using ROM firmware.\n");
			return 0;
		}
		dev_err(&client->dev, "Can't continue without a firmware.\n");
	} else if (ret < 0) {
		dev_err(&client->dev, "error %d when loading firmware\n", ret);
	}
	return ret;
}

static int si2157_init(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct i2c_client *client = fe->tuner_priv;
	struct si2157_dev *dev = i2c_get_clientdata(client);
	unsigned int xtal_trim;
	struct si2157_cmd cmd;
	int ret;

	dev_dbg(&client->dev, "\n");

	/* Try to get Xtal trim property, to verify tuner still running */
	memcpy(cmd.args, "\x15\x00\x02\x04", 4);
	cmd.wlen = 4;
	cmd.rlen = 4;
	ret = si2157_cmd_execute(client, &cmd);

	xtal_trim = cmd.args[2] | (cmd.args[3] << 8);

	if (ret == 0 && xtal_trim < 16)
		goto warm;

	dev->if_frequency = 0; /* we no longer know current tuner state */

	/* power up */
	if (dev->part_id == SI2146) {
		/* clock_mode = XTAL, clock_freq = 24MHz */
		memcpy(cmd.args, "\xc0\x05\x01\x00\x00\x0b\x00\x00\x01", 9);
		cmd.wlen = 9;
	} else if (dev->part_id == SI2141) {
		/* clock_mode: XTAL, xout enabled */
		memcpy(cmd.args, "\xc0\x00\x0d\x0e\x00\x01\x01\x01\x01\x03", 10);
		cmd.wlen = 10;
	} else {
		memcpy(cmd.args, "\xc0\x00\x0c\x00\x00\x01\x01\x01\x01\x01\x01\x02\x00\x00\x01", 15);
		cmd.wlen = 15;
	}
	cmd.rlen = 1;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret && (dev->part_id != SI2141 || ret != -EAGAIN))
		goto err;

	/* Si2141 needs a wake up command */
	if (dev->part_id == SI2141) {
		memcpy(cmd.args, "\xc0\x08\x01\x02\x00\x00\x01", 7);
		cmd.wlen = 7;
		ret = si2157_cmd_execute(client, &cmd);
		if (ret)
			goto err;
	}

	/* Try to load the firmware */
	ret = si2157_find_and_load_firmware(fe);
	if (ret < 0)
		goto err;

	/* reboot the tuner with new firmware? */
	memcpy(cmd.args, "\x01\x01", 2);
	cmd.wlen = 2;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	/* query firmware version */
	memcpy(cmd.args, "\x11", 1);
	cmd.wlen = 1;
	cmd.rlen = 10;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	dev_info(&client->dev, "firmware version: %c.%c.%d\n",
			cmd.args[6], cmd.args[7], cmd.args[8]);

	/* enable tuner status flags */
	memcpy(cmd.args, "\x14\x00\x01\x05\x01\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x01\x06\x01\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x01\x07\x01\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;
warm:
	/* init statistics in order signal app which are supported */
	c->strength.len = 1;
	c->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	/* start statistics polling */
	schedule_delayed_work(&dev->stat_work, msecs_to_jiffies(1000));

	dev->active = true;
	return 0;

err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int si2157_sleep(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->tuner_priv;
	struct si2157_dev *dev = i2c_get_clientdata(client);
	int ret;
	struct si2157_cmd cmd;

	dev_dbg(&client->dev, "\n");

	dev->active = false;

	/* stop statistics polling */
	cancel_delayed_work_sync(&dev->stat_work);

	/* standby */
	memcpy(cmd.args, "\x16\x00", 2);
	cmd.wlen = 2;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int si2157_tune_wait(struct i2c_client *client, u8 is_digital)
{
#define TUN_TIMEOUT 40
#define DIG_TIMEOUT 30
#define ANALOG_TIMEOUT 150
	struct si2157_dev *dev = i2c_get_clientdata(client);
	int ret;
	unsigned long timeout;
	unsigned long start_time;
	u8 wait_status;
	u8  tune_lock_mask;

	if (is_digital)
		tune_lock_mask = 0x04;
	else
		tune_lock_mask = 0x02;

	mutex_lock(&dev->i2c_mutex);

	/* wait tuner command complete */
	start_time = jiffies;
	timeout = start_time + msecs_to_jiffies(TUN_TIMEOUT);
	while (1) {
		ret = i2c_master_recv(client, &wait_status,
				      sizeof(wait_status));
		if (ret < 0) {
			goto err_mutex_unlock;
		} else if (ret != sizeof(wait_status)) {
			ret = -EREMOTEIO;
			goto err_mutex_unlock;
		}

		if (time_after(jiffies, timeout))
			break;

		/* tuner done? */
		if ((wait_status & 0x81) == 0x81)
			break;
		usleep_range(5000, 10000);
	}

	dev_dbg(&client->dev, "tuning took %d ms, status=0x%x\n",
		jiffies_to_msecs(jiffies) - jiffies_to_msecs(start_time),
		wait_status);

	/* if we tuned ok, wait a bit for tuner lock */
	if (tuner_lock_debug && (wait_status & 0x81) == 0x81) {
		if (is_digital)
			timeout = jiffies + msecs_to_jiffies(DIG_TIMEOUT);
		else
			timeout = jiffies + msecs_to_jiffies(ANALOG_TIMEOUT);

		while (!time_after(jiffies, timeout)) {
			ret = i2c_master_recv(client, &wait_status,
					      sizeof(wait_status));
			if (ret < 0) {
				goto err_mutex_unlock;
			} else if (ret != sizeof(wait_status)) {
				ret = -EREMOTEIO;
				goto err_mutex_unlock;
			}

			/* tuner locked? */
			if (wait_status & tune_lock_mask)
				break;
			usleep_range(5000, 10000);
		}

		dev_dbg(&client->dev, "tuning+lock took %d ms, status=0x%x\n",
			jiffies_to_msecs(jiffies) - jiffies_to_msecs(start_time),
			wait_status);
	}

	if ((wait_status & 0xc0) != 0x80) {
		ret = -ETIMEDOUT;
		goto err_mutex_unlock;
	}

	mutex_unlock(&dev->i2c_mutex);
	return 0;

err_mutex_unlock:
	mutex_unlock(&dev->i2c_mutex);
	dev_err(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int si2157_set_params(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->tuner_priv;
	struct si2157_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	struct si2157_cmd cmd;
	u8 bw, delivery_system;
	u32 bandwidth;
	u32 if_frequency = 5000000;

	dev_dbg(&client->dev,
			"delivery_system=%d frequency=%u bandwidth_hz=%u\n",
			c->delivery_system, c->frequency, c->bandwidth_hz);

	if (!dev->active) {
		ret = -EAGAIN;
		goto err;
	}

	if (SUPPORTS_1700KHz(dev) && c->bandwidth_hz <= 1700000) {
		bandwidth = 1700000;
		bw = 9;
	} else if (c->bandwidth_hz <= 6000000) {
		bandwidth = 6000000;
		bw = 6;
	} else if (SUPPORTS_1700KHz(dev) && c->bandwidth_hz <= 6100000) {
		bandwidth = 6100000;
		bw = 10;
	} else if (c->bandwidth_hz <= 7000000) {
		bandwidth = 7000000;
		bw = 7;
	} else {
		bandwidth = 8000000;
		bw = 8;
	}

	switch (c->delivery_system) {
	case SYS_ATSC:
			delivery_system = 0x00;
			if_frequency = 3250000;
			break;
	case SYS_DVBC_ANNEX_B:
			delivery_system = 0x10;
			if_frequency = 4000000;
			break;
	case SYS_DVBT:
	case SYS_DVBT2: /* it seems DVB-T and DVB-T2 both are 0x20 here */
			delivery_system = 0x20;
			break;
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_C:
			delivery_system = 0x30;
			break;
	case SYS_ISDBT:
			delivery_system = 0x40;
			break;
	case SYS_DTMB:
			delivery_system = 0x60;
			break;
	default:
			ret = -EINVAL;
			goto err;
	}

	memcpy(cmd.args, "\x14\x00\x03\x07\x00\x00", 6);
	cmd.args[4] = delivery_system | bw;
	if (dev->inversion)
		cmd.args[5] = 0x01;
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	/* On SI2146, set DTV AGC source to DLIF_AGC_3DB */
	if (dev->part_id == SI2146)
		memcpy(cmd.args, "\x14\x00\x02\x07\x00\x01", 6);
	else
		memcpy(cmd.args, "\x14\x00\x02\x07\x00\x00", 6);
	cmd.args[4] = dev->if_port;
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	/* set digital if frequency if needed */
	if (if_frequency != dev->if_frequency) {
		memcpy(cmd.args, "\x14\x00\x06\x07", 4);
		cmd.args[4] = (if_frequency / 1000) & 0xff;
		cmd.args[5] = ((if_frequency / 1000) >> 8) & 0xff;
		cmd.wlen = 6;
		cmd.rlen = 4;
		ret = si2157_cmd_execute(client, &cmd);
		if (ret)
			goto err;

		dev->if_frequency = if_frequency;
	}

	/* set digital frequency */
	memcpy(cmd.args, "\x41\x00\x00\x00\x00\x00\x00\x00", 8);
	cmd.args[4] = (c->frequency >>  0) & 0xff;
	cmd.args[5] = (c->frequency >>  8) & 0xff;
	cmd.args[6] = (c->frequency >> 16) & 0xff;
	cmd.args[7] = (c->frequency >> 24) & 0xff;
	cmd.wlen = 8;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	dev->bandwidth = bandwidth;
	dev->frequency = c->frequency;

	si2157_tune_wait(client, 1); /* wait to complete, ignore any errors */

	return 0;
err:
	dev->bandwidth = 0;
	dev->frequency = 0;
	dev->if_frequency = 0;
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int si2157_set_analog_params(struct dvb_frontend *fe,
				    struct analog_parameters *params)
{
	struct i2c_client *client = fe->tuner_priv;
	struct si2157_dev *dev = i2c_get_clientdata(client);
	char *std; /* for debugging */
	int ret;
	struct si2157_cmd cmd;
	u32 bandwidth = 0;
	u32 if_frequency = 0;
	u32 freq = 0;
	u64 tmp_lval = 0;
	u8 system = 0;
	u8 color = 0;    /* 0=NTSC/PAL, 0x10=SECAM */
	u8 invert_analog = 1; /* analog tuner spectrum; 0=normal, 1=inverted */

	if (!SUPPORTS_ATV_IF(dev)) {
		dev_info(&client->dev, "Analog tuning not supported yet for Si21%d\n",
			 dev->part_id);
		ret = -EINVAL;
		goto err;
	}

	if (!dev->active)
		si2157_init(fe);

	if (!dev->active) {
		ret = -EAGAIN;
		goto err;
	}
	if (params->mode == V4L2_TUNER_RADIO) {
	/*
	 * std = "fm";
	 * bandwidth = 1700000; //best can do for FM, AGC will be a mess though
	 * if_frequency = 1250000;  //HVR-225x(saa7164), HVR-12xx(cx23885)
	 * if_frequency = 6600000;  //HVR-9xx(cx231xx)
	 * if_frequency = 5500000;  //HVR-19xx(pvrusb2)
	 */
		dev_err(&client->dev, "si2157 does not currently support FM radio\n");
		ret = -EINVAL;
		goto err;
	}
	tmp_lval = params->frequency * 625LL;
	do_div(tmp_lval, 10); /* convert to HZ */
	freq = (u32)tmp_lval;

	if (freq < 1000000) /* is freq in KHz */
		freq = freq * 1000;
	dev->frequency = freq;

	/* if_frequency values based on tda187271C2 */
	if (params->std & (V4L2_STD_B | V4L2_STD_GH)) {
		if (freq >= 470000000) {
			std = "palGH";
			bandwidth = 8000000;
			if_frequency = 6000000;
			system = 1;
			if (params->std &
			    (V4L2_STD_SECAM_G | V4L2_STD_SECAM_H)) {
				std = "secamGH";
				color = 0x10;
			}
		} else {
			std = "palB";
			bandwidth = 7000000;
			if_frequency = 6000000;
			system = 0;
			if (params->std & V4L2_STD_SECAM_B) {
				std = "secamB";
				color = 0x10;
			}
		}
	} else if (params->std & V4L2_STD_MN) {
		std = "MN";
		bandwidth = 6000000;
		if_frequency = 5400000;
		system = 2;
	} else if (params->std & V4L2_STD_PAL_I) {
		std = "palI";
		bandwidth = 8000000;
		if_frequency = 7250000; /* TODO: does not work yet */
		system = 4;
	} else if (params->std & V4L2_STD_DK) {
		std = "palDK";
		bandwidth = 8000000;
		if_frequency = 6900000; /* TODO: does not work yet */
		system = 5;
		if (params->std & V4L2_STD_SECAM_DK) {
			std = "secamDK";
			color = 0x10;
		}
	} else if (params->std & V4L2_STD_SECAM_L) {
		std = "secamL";
		bandwidth = 8000000;
		if_frequency = 6750000; /* TODO: untested */
		system = 6;
		color = 0x10;
	} else if (params->std & V4L2_STD_SECAM_LC) {
		std = "secamL'";
		bandwidth = 7000000;
		if_frequency = 1250000; /* TODO: untested */
		system = 7;
		color = 0x10;
	} else {
		std = "unknown";
	}
	/* calc channel center freq */
	freq = freq - 1250000 + (bandwidth / 2);

	dev_dbg(&client->dev,
		"mode=%d system=%u std='%s' params->frequency=%u center freq=%u if=%u bandwidth=%u\n",
		params->mode, system, std, params->frequency,
		freq, if_frequency, bandwidth);

	/* set analog IF port */
	memcpy(cmd.args, "\x14\x00\x03\x06\x08\x02", 6);
	/* in using dev->if_port, we assume analog and digital IF's */
	/*   are always on different ports */
	/* assumes if_port definition is 0 or 1 for digital out */
	cmd.args[4] = (dev->if_port == 1) ? 8 : 10;
	/* Analog AGC assumed external */
	cmd.args[5] = (dev->if_port == 1) ? 2 : 1;
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	/* set analog IF output config */
	memcpy(cmd.args, "\x14\x00\x0d\x06\x94\x64", 6);
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	/* make this distinct from a digital IF */
	dev->if_frequency = if_frequency | 1;

	/* calc and set tuner analog if center frequency */
	if_frequency = if_frequency + 1250000 - (bandwidth / 2);
	dev_dbg(&client->dev, "IF Ctr freq=%d\n", if_frequency);

	memcpy(cmd.args, "\x14\x00\x0C\x06", 4);
	cmd.args[4] = (if_frequency / 1000) & 0xff;
	cmd.args[5] = ((if_frequency / 1000) >> 8) & 0xff;
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	/* set analog AGC config */
	memcpy(cmd.args, "\x14\x00\x07\x06\x32\xc8", 6);
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	/* set analog video mode */
	memcpy(cmd.args, "\x14\x00\x04\x06\x00\x00", 6);
	cmd.args[4] = system | color;
	/* can use dev->inversion if assumed applies to both digital/analog */
	if (invert_analog)
		cmd.args[5] |= 0x02;
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	/* set analog frequency */
	memcpy(cmd.args, "\x41\x01\x00\x00\x00\x00\x00\x00", 8);
	cmd.args[4] = (freq >>  0) & 0xff;
	cmd.args[5] = (freq >>  8) & 0xff;
	cmd.args[6] = (freq >> 16) & 0xff;
	cmd.args[7] = (freq >> 24) & 0xff;
	cmd.wlen = 8;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	dev->bandwidth = bandwidth;

	si2157_tune_wait(client, 0); /* wait to complete, ignore any errors */

	return 0;
err:
	dev->bandwidth = 0;
	dev->frequency = 0;
	dev->if_frequency = 0;
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int si2157_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct i2c_client *client = fe->tuner_priv;
	struct si2157_dev *dev = i2c_get_clientdata(client);

	*frequency = dev->frequency;
	dev_dbg(&client->dev, "freq=%u\n", dev->frequency);
	return 0;
}

static int si2157_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct i2c_client *client = fe->tuner_priv;
	struct si2157_dev *dev = i2c_get_clientdata(client);

	*bandwidth = dev->bandwidth;
	dev_dbg(&client->dev, "bandwidth=%u\n", dev->bandwidth);
	return 0;
}

static int si2157_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct i2c_client *client = fe->tuner_priv;
	struct si2157_dev *dev = i2c_get_clientdata(client);

	*frequency = dev->if_frequency & ~1; /* strip analog IF indicator bit */
	dev_dbg(&client->dev, "if_frequency=%u\n", *frequency);
	return 0;
}

static int si2157_get_rf_strength(struct dvb_frontend *fe, u16 *rssi)
{
	struct i2c_client *client = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct si2157_cmd cmd;
	int ret;
	int strength;

	dev_dbg(&client->dev, "\n");

	memcpy(cmd.args, "\x42\x00", 2);
	cmd.wlen = 2;
	cmd.rlen = 12;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	c->strength.stat[0].scale = FE_SCALE_DECIBEL;
	c->strength.stat[0].svalue = (s8)cmd.args[3] * 1000;

	/* normalize values based on Silicon Labs reference
	 * add 100, then anything > 80 is 100% signal
	 */
	strength = (s8)cmd.args[3] + 100;
	strength = clamp_val(strength, 0, 80);
	*rssi = (u16)(strength * 0xffff / 80);

	dev_dbg(&client->dev, "strength=%d rssi=%u\n",
		(s8)cmd.args[3], *rssi);

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static const struct dvb_tuner_ops si2157_ops = {
	.info = {
		.name             = "Silicon Labs Si2141/Si2146/2147/2148/2157/2158",
		.frequency_min_hz =  42 * MHz,
		.frequency_max_hz = 870 * MHz,
	},

	.init = si2157_init,
	.sleep = si2157_sleep,
	.set_params = si2157_set_params,
	.set_analog_params = si2157_set_analog_params,
	.get_frequency     = si2157_get_frequency,
	.get_bandwidth     = si2157_get_bandwidth,
	.get_if_frequency  = si2157_get_if_frequency,

	.get_rf_strength   = si2157_get_rf_strength,
};

static void si2157_stat_work(struct work_struct *work)
{
	struct si2157_dev *dev = container_of(work, struct si2157_dev, stat_work.work);
	struct dvb_frontend *fe = dev->fe;
	struct i2c_client *client = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct si2157_cmd cmd;
	int ret;

	dev_dbg(&client->dev, "\n");

	memcpy(cmd.args, "\x42\x00", 2);
	cmd.wlen = 2;
	cmd.rlen = 12;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	c->strength.stat[0].scale = FE_SCALE_DECIBEL;
	c->strength.stat[0].svalue = (s8) cmd.args[3] * 1000;

	schedule_delayed_work(&dev->stat_work, msecs_to_jiffies(2000));
	return;
err:
	c->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	dev_dbg(&client->dev, "failed=%d\n", ret);
}

static int si2157_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct si2157_config *cfg = client->dev.platform_data;
	struct dvb_frontend *fe = cfg->fe;
	struct si2157_dev *dev;
	struct si2157_cmd cmd;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		dev_err(&client->dev, "kzalloc() failed\n");
		goto err;
	}

	i2c_set_clientdata(client, dev);
	dev->fe = cfg->fe;
	dev->inversion = cfg->inversion;
	dev->dont_load_firmware = cfg->dont_load_firmware;
	dev->if_port = cfg->if_port;
	dev->part_id = (u8)id->driver_data;
	dev->if_frequency = 5000000; /* default value of property 0x0706 */
	mutex_init(&dev->i2c_mutex);
	INIT_DELAYED_WORK(&dev->stat_work, si2157_stat_work);

	/* check if the tuner is there */
	cmd.wlen = 0;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(client, &cmd);
	if (ret && ret != -EAGAIN)
		goto err_kfree;

	memcpy(&fe->ops.tuner_ops, &si2157_ops, sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = client;

#ifdef CONFIG_MEDIA_CONTROLLER
	if (cfg->mdev) {
		dev->mdev = cfg->mdev;

		dev->ent.name = KBUILD_MODNAME;
		dev->ent.function = MEDIA_ENT_F_TUNER;

		dev->pad[SI2157_PAD_RF_INPUT].flags = MEDIA_PAD_FL_SINK;
		dev->pad[SI2157_PAD_RF_INPUT].sig_type = PAD_SIGNAL_ANALOG;
		dev->pad[SI2157_PAD_VID_OUT].flags = MEDIA_PAD_FL_SOURCE;
		dev->pad[SI2157_PAD_VID_OUT].sig_type = PAD_SIGNAL_ANALOG;
		dev->pad[SI2157_PAD_AUD_OUT].flags = MEDIA_PAD_FL_SOURCE;
		dev->pad[SI2157_PAD_AUD_OUT].sig_type = PAD_SIGNAL_AUDIO;

		ret = media_entity_pads_init(&dev->ent, SI2157_NUM_PADS,
					     &dev->pad[0]);

		if (ret)
			goto err_kfree;

		ret = media_device_register_entity(cfg->mdev, &dev->ent);
		if (ret) {
			media_entity_cleanup(&dev->ent);
			goto err_kfree;
		}
	}
#endif

	dev_info(&client->dev, "Silicon Labs Si21%d successfully attached\n",
		 dev->part_id);

	return 0;

err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static void si2157_remove(struct i2c_client *client)
{
	struct si2157_dev *dev = i2c_get_clientdata(client);
	struct dvb_frontend *fe = dev->fe;

	dev_dbg(&client->dev, "\n");

	/* stop statistics polling */
	cancel_delayed_work_sync(&dev->stat_work);

#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	if (dev->mdev)
		media_device_unregister_entity(&dev->ent);
#endif

	memset(&fe->ops.tuner_ops, 0, sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = NULL;
	kfree(dev);
}

/*
 * The part_id used here will only be used on buggy devices that don't
 * accept firmware uploads. Non-buggy devices should just use "si2157" for
 * all SiLabs TER tuners, as the driver should auto-detect it.
 */
static const struct i2c_device_id si2157_id_table[] = {
	{"si2157", SI2157},
	{"si2146", SI2146},
	{"si2141", SI2141},
	{"si2177", SI2177},
	{}
};
MODULE_DEVICE_TABLE(i2c, si2157_id_table);

static struct i2c_driver si2157_driver = {
	.driver = {
		.name		     = "si2157",
		.suppress_bind_attrs = true,
	},
	.probe_new	= si2157_probe,
	.remove		= si2157_remove,
	.id_table	= si2157_id_table,
};

module_i2c_driver(si2157_driver);

MODULE_DESCRIPTION("Silicon Labs Si2141/Si2146/2147/2148/2157/2158 silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(SI2158_A20_FIRMWARE);
MODULE_FIRMWARE(SI2141_A10_FIRMWARE);
MODULE_FIRMWARE(SI2157_A30_FIRMWARE);
MODULE_FIRMWARE(SI2141_60_FIRMWARE);
MODULE_FIRMWARE(SI2141_61_FIRMWARE);
MODULE_FIRMWARE(SI2146_11_FIRMWARE);
MODULE_FIRMWARE(SI2147_50_FIRMWARE);
MODULE_FIRMWARE(SI2148_32_FIRMWARE);
MODULE_FIRMWARE(SI2148_33_FIRMWARE);
MODULE_FIRMWARE(SI2157_50_FIRMWARE);
MODULE_FIRMWARE(SI2158_50_FIRMWARE);
MODULE_FIRMWARE(SI2158_51_FIRMWARE);
MODULE_FIRMWARE(SI2177_50_FIRMWARE);
