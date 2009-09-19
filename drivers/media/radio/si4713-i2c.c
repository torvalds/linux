/*
 * drivers/media/radio/si4713-i2c.c
 *
 * Silicon Labs Si4713 FM Radio Transmitter I2C commands.
 *
 * Copyright (c) 2009 Nokia Corporation
 * Contact: Eduardo Valentin <eduardo.valentin@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>

#include "si4713-i2c.h"

/* module parameters */
static int debug;
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug level (0 - 2)");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eduardo Valentin <eduardo.valentin@nokia.com>");
MODULE_DESCRIPTION("I2C driver for Si4713 FM Radio Transmitter");
MODULE_VERSION("0.0.1");

#define DEFAULT_RDS_PI			0x00
#define DEFAULT_RDS_PTY			0x00
#define DEFAULT_RDS_PS_NAME		""
#define DEFAULT_RDS_RADIO_TEXT		DEFAULT_RDS_PS_NAME
#define DEFAULT_RDS_DEVIATION		0x00C8
#define DEFAULT_RDS_PS_REPEAT_COUNT	0x0003
#define DEFAULT_LIMITER_RTIME		0x1392
#define DEFAULT_LIMITER_DEV		0x102CA
#define DEFAULT_PILOT_FREQUENCY 	0x4A38
#define DEFAULT_PILOT_DEVIATION		0x1A5E
#define DEFAULT_ACOMP_ATIME		0x0000
#define DEFAULT_ACOMP_RTIME		0xF4240L
#define DEFAULT_ACOMP_GAIN		0x0F
#define DEFAULT_ACOMP_THRESHOLD 	(-0x28)
#define DEFAULT_MUTE			0x01
#define DEFAULT_POWER_LEVEL		88
#define DEFAULT_FREQUENCY		8800
#define DEFAULT_PREEMPHASIS		FMPE_EU
#define DEFAULT_TUNE_RNL		0xFF

#define to_si4713_device(sd)	container_of(sd, struct si4713_device, sd)

/* frequency domain transformation (using times 10 to avoid floats) */
#define FREQDEV_UNIT	100000
#define FREQV4L2_MULTI	625
#define si4713_to_v4l2(f)	((f * FREQDEV_UNIT) / FREQV4L2_MULTI)
#define v4l2_to_si4713(f)	((f * FREQV4L2_MULTI) / FREQDEV_UNIT)
#define FREQ_RANGE_LOW			7600
#define FREQ_RANGE_HIGH			10800

#define MAX_ARGS 7

#define RDS_BLOCK			8
#define RDS_BLOCK_CLEAR			0x03
#define RDS_BLOCK_LOAD			0x04
#define RDS_RADIOTEXT_2A		0x20
#define RDS_RADIOTEXT_BLK_SIZE		4
#define RDS_RADIOTEXT_INDEX_MAX		0x0F
#define RDS_CARRIAGE_RETURN		0x0D

#define rds_ps_nblocks(len)	((len / RDS_BLOCK) + (len % RDS_BLOCK ? 1 : 0))

#define get_status_bit(p, b, m)	(((p) & (m)) >> (b))
#define set_bits(p, v, b, m)	(((p) & ~(m)) | ((v) << (b)))

#define ATTACK_TIME_UNIT	500

#define POWER_OFF			0x00
#define POWER_ON			0x01

#define msb(x)                  ((u8)((u16) x >> 8))
#define lsb(x)                  ((u8)((u16) x &  0x00FF))
#define compose_u16(msb, lsb)	(((u16)msb << 8) | lsb)
#define check_command_failed(status)	(!(status & SI4713_CTS) || \
					(status & SI4713_ERR))
/* mute definition */
#define set_mute(p)	((p & 1) | ((p & 1) << 1));
#define get_mute(p)	(p & 0x01)

#ifdef DEBUG
#define DBG_BUFFER(device, message, buffer, size)			\
	{								\
		int i;							\
		char str[(size)*5];					\
		for (i = 0; i < size; i++)				\
			sprintf(str + i * 5, " 0x%02x", buffer[i]);	\
		v4l2_dbg(2, debug, device, "%s:%s\n", message, str);	\
	}
#else
#define DBG_BUFFER(device, message, buffer, size)
#endif

/*
 * Values for limiter release time (sorted by second column)
 *	device	release
 *	value	time (us)
 */
static long limiter_times[] = {
	2000,	250,
	1000,	500,
	510,	1000,
	255,	2000,
	170,	3000,
	127,	4020,
	102,	5010,
	85,	6020,
	73,	7010,
	64,	7990,
	57,	8970,
	51,	10030,
	25,	20470,
	17,	30110,
	13,	39380,
	10,	51190,
	8,	63690,
	7,	73140,
	6,	85330,
	5,	102390,
};

/*
 * Values for audio compression release time (sorted by second column)
 *	device	release
 *	value	time (us)
 */
static unsigned long acomp_rtimes[] = {
	0,	100000,
	1,	200000,
	2,	350000,
	3,	525000,
	4,	1000000,
};

/*
 * Values for preemphasis (sorted by second column)
 *	device	preemphasis
 *	value	value (v4l2)
 */
static unsigned long preemphasis_values[] = {
	FMPE_DISABLED,	V4L2_PREEMPHASIS_DISABLED,
	FMPE_EU,	V4L2_PREEMPHASIS_50_uS,
	FMPE_USA,	V4L2_PREEMPHASIS_75_uS,
};

static int usecs_to_dev(unsigned long usecs, unsigned long const array[],
			int size)
{
	int i;
	int rval = -EINVAL;

	for (i = 0; i < size / 2; i++)
		if (array[(i * 2) + 1] >= usecs) {
			rval = array[i * 2];
			break;
		}

	return rval;
}

static unsigned long dev_to_usecs(int value, unsigned long const array[],
			int size)
{
	int i;
	int rval = -EINVAL;

	for (i = 0; i < size / 2; i++)
		if (array[i * 2] == value) {
			rval = array[(i * 2) + 1];
			break;
		}

	return rval;
}

/* si4713_handler: IRQ handler, just complete work */
static irqreturn_t si4713_handler(int irq, void *dev)
{
	struct si4713_device *sdev = dev;

	v4l2_dbg(2, debug, &sdev->sd,
			"%s: sending signal to completion work.\n", __func__);
	complete(&sdev->work);

	return IRQ_HANDLED;
}

/*
 * si4713_send_command - sends a command to si4713 and waits its response
 * @sdev: si4713_device structure for the device we are communicating
 * @command: command id
 * @args: command arguments we are sending (up to 7)
 * @argn: actual size of @args
 * @response: buffer to place the expected response from the device (up to 15)
 * @respn: actual size of @response
 * @usecs: amount of time to wait before reading the response (in usecs)
 */
static int si4713_send_command(struct si4713_device *sdev, const u8 command,
				const u8 args[], const int argn,
				u8 response[], const int respn, const int usecs)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sdev->sd);
	u8 data1[MAX_ARGS + 1];
	int err;

	if (!client->adapter)
		return -ENODEV;

	/* First send the command and its arguments */
	data1[0] = command;
	memcpy(data1 + 1, args, argn);
	DBG_BUFFER(&sdev->sd, "Parameters", data1, argn + 1);

	err = i2c_master_send(client, data1, argn + 1);
	if (err != argn + 1) {
		v4l2_err(&sdev->sd, "Error while sending command 0x%02x\n",
			command);
		return (err > 0) ? -EIO : err;
	}

	/* Wait response from interrupt */
	if (!wait_for_completion_timeout(&sdev->work,
				usecs_to_jiffies(usecs) + 1))
		v4l2_warn(&sdev->sd,
				"(%s) Device took too much time to answer.\n",
				__func__);

	/* Then get the response */
	err = i2c_master_recv(client, response, respn);
	if (err != respn) {
		v4l2_err(&sdev->sd,
			"Error while reading response for command 0x%02x\n",
			command);
		return (err > 0) ? -EIO : err;
	}

	DBG_BUFFER(&sdev->sd, "Response", response, respn);
	if (check_command_failed(response[0]))
		return -EBUSY;

	return 0;
}

/*
 * si4713_read_property - reads a si4713 property
 * @sdev: si4713_device structure for the device we are communicating
 * @prop: property identification number
 * @pv: property value to be returned on success
 */
static int si4713_read_property(struct si4713_device *sdev, u16 prop, u32 *pv)
{
	int err;
	u8 val[SI4713_GET_PROP_NRESP];
	/*
	 * 	.First byte = 0
	 * 	.Second byte = property's MSB
	 * 	.Third byte = property's LSB
	 */
	const u8 args[SI4713_GET_PROP_NARGS] = {
		0x00,
		msb(prop),
		lsb(prop),
	};

	err = si4713_send_command(sdev, SI4713_CMD_GET_PROPERTY,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (err < 0)
		return err;

	*pv = compose_u16(val[2], val[3]);

	v4l2_dbg(1, debug, &sdev->sd,
			"%s: property=0x%02x value=0x%02x status=0x%02x\n",
			__func__, prop, *pv, val[0]);

	return err;
}

/*
 * si4713_write_property - modifies a si4713 property
 * @sdev: si4713_device structure for the device we are communicating
 * @prop: property identification number
 * @val: new value for that property
 */
static int si4713_write_property(struct si4713_device *sdev, u16 prop, u16 val)
{
	int rval;
	u8 resp[SI4713_SET_PROP_NRESP];
	/*
	 * 	.First byte = 0
	 * 	.Second byte = property's MSB
	 * 	.Third byte = property's LSB
	 * 	.Fourth byte = value's MSB
	 * 	.Fifth byte = value's LSB
	 */
	const u8 args[SI4713_SET_PROP_NARGS] = {
		0x00,
		msb(prop),
		lsb(prop),
		msb(val),
		lsb(val),
	};

	rval = si4713_send_command(sdev, SI4713_CMD_SET_PROPERTY,
					args, ARRAY_SIZE(args),
					resp, ARRAY_SIZE(resp),
					DEFAULT_TIMEOUT);

	if (rval < 0)
		return rval;

	v4l2_dbg(1, debug, &sdev->sd,
			"%s: property=0x%02x value=0x%02x status=0x%02x\n",
			__func__, prop, val, resp[0]);

	/*
	 * As there is no command response for SET_PROPERTY,
	 * wait Tcomp time to finish before proceed, in order
	 * to have property properly set.
	 */
	msleep(TIMEOUT_SET_PROPERTY);

	return rval;
}

/*
 * si4713_powerup - Powers the device up
 * @sdev: si4713_device structure for the device we are communicating
 */
static int si4713_powerup(struct si4713_device *sdev)
{
	int err;
	u8 resp[SI4713_PWUP_NRESP];
	/*
	 * 	.First byte = Enabled interrupts and boot function
	 * 	.Second byte = Input operation mode
	 */
	const u8 args[SI4713_PWUP_NARGS] = {
		SI4713_PWUP_CTSIEN | SI4713_PWUP_GPO2OEN | SI4713_PWUP_FUNC_TX,
		SI4713_PWUP_OPMOD_ANALOG,
	};

	if (sdev->power_state)
		return 0;

	sdev->platform_data->set_power(1);
	err = si4713_send_command(sdev, SI4713_CMD_POWER_UP,
					args, ARRAY_SIZE(args),
					resp, ARRAY_SIZE(resp),
					TIMEOUT_POWER_UP);

	if (!err) {
		v4l2_dbg(1, debug, &sdev->sd, "Powerup response: 0x%02x\n",
				resp[0]);
		v4l2_dbg(1, debug, &sdev->sd, "Device in power up mode\n");
		sdev->power_state = POWER_ON;

		err = si4713_write_property(sdev, SI4713_GPO_IEN,
						SI4713_STC_INT | SI4713_CTS);
	} else {
		sdev->platform_data->set_power(0);
	}

	return err;
}

/*
 * si4713_powerdown - Powers the device down
 * @sdev: si4713_device structure for the device we are communicating
 */
static int si4713_powerdown(struct si4713_device *sdev)
{
	int err;
	u8 resp[SI4713_PWDN_NRESP];

	if (!sdev->power_state)
		return 0;

	err = si4713_send_command(sdev, SI4713_CMD_POWER_DOWN,
					NULL, 0,
					resp, ARRAY_SIZE(resp),
					DEFAULT_TIMEOUT);

	if (!err) {
		v4l2_dbg(1, debug, &sdev->sd, "Power down response: 0x%02x\n",
				resp[0]);
		v4l2_dbg(1, debug, &sdev->sd, "Device in reset mode\n");
		sdev->platform_data->set_power(0);
		sdev->power_state = POWER_OFF;
	}

	return err;
}

/*
 * si4713_checkrev - Checks if we are treating a device with the correct rev.
 * @sdev: si4713_device structure for the device we are communicating
 */
static int si4713_checkrev(struct si4713_device *sdev)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sdev->sd);
	int rval;
	u8 resp[SI4713_GETREV_NRESP];

	mutex_lock(&sdev->mutex);

	rval = si4713_send_command(sdev, SI4713_CMD_GET_REV,
					NULL, 0,
					resp, ARRAY_SIZE(resp),
					DEFAULT_TIMEOUT);

	if (rval < 0)
		goto unlock;

	if (resp[1] == SI4713_PRODUCT_NUMBER) {
		v4l2_info(&sdev->sd, "chip found @ 0x%02x (%s)\n",
				client->addr << 1, client->adapter->name);
	} else {
		v4l2_err(&sdev->sd, "Invalid product number\n");
		rval = -EINVAL;
	}

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

/*
 * si4713_wait_stc - Waits STC interrupt and clears status bits. Usefull
 *		     for TX_TUNE_POWER, TX_TUNE_FREQ and TX_TUNE_MEAS
 * @sdev: si4713_device structure for the device we are communicating
 * @usecs: timeout to wait for STC interrupt signal
 */
static int si4713_wait_stc(struct si4713_device *sdev, const int usecs)
{
	int err;
	u8 resp[SI4713_GET_STATUS_NRESP];

	/* Wait response from STC interrupt */
	if (!wait_for_completion_timeout(&sdev->work,
			usecs_to_jiffies(usecs) + 1))
		v4l2_warn(&sdev->sd,
			"%s: device took too much time to answer (%d usec).\n",
				__func__, usecs);

	/* Clear status bits */
	err = si4713_send_command(sdev, SI4713_CMD_GET_INT_STATUS,
					NULL, 0,
					resp, ARRAY_SIZE(resp),
					DEFAULT_TIMEOUT);

	if (err < 0)
		goto exit;

	v4l2_dbg(1, debug, &sdev->sd,
			"%s: status bits: 0x%02x\n", __func__, resp[0]);

	if (!(resp[0] & SI4713_STC_INT))
		err = -EIO;

exit:
	return err;
}

/*
 * si4713_tx_tune_freq - Sets the state of the RF carrier and sets the tuning
 * 			frequency between 76 and 108 MHz in 10 kHz units and
 * 			steps of 50 kHz.
 * @sdev: si4713_device structure for the device we are communicating
 * @frequency: desired frequency (76 - 108 MHz, unit 10 KHz, step 50 kHz)
 */
static int si4713_tx_tune_freq(struct si4713_device *sdev, u16 frequency)
{
	int err;
	u8 val[SI4713_TXFREQ_NRESP];
	/*
	 * 	.First byte = 0
	 * 	.Second byte = frequency's MSB
	 * 	.Third byte = frequency's LSB
	 */
	const u8 args[SI4713_TXFREQ_NARGS] = {
		0x00,
		msb(frequency),
		lsb(frequency),
	};

	err = si4713_send_command(sdev, SI4713_CMD_TX_TUNE_FREQ,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (err < 0)
		return err;

	v4l2_dbg(1, debug, &sdev->sd,
			"%s: frequency=0x%02x status=0x%02x\n", __func__,
			frequency, val[0]);

	err = si4713_wait_stc(sdev, TIMEOUT_TX_TUNE);
	if (err < 0)
		return err;

	return compose_u16(args[1], args[2]);
}

/*
 * si4713_tx_tune_power - Sets the RF voltage level between 88 and 115 dBuV in
 * 			1 dB units. A value of 0x00 indicates off. The command
 * 			also sets the antenna tuning capacitance. A value of 0
 * 			indicates autotuning, and a value of 1 - 191 indicates
 * 			a manual override, which results in a tuning
 * 			capacitance of 0.25 pF x @antcap.
 * @sdev: si4713_device structure for the device we are communicating
 * @power: tuning power (88 - 115 dBuV, unit/step 1 dB)
 * @antcap: value of antenna tuning capacitor (0 - 191)
 */
static int si4713_tx_tune_power(struct si4713_device *sdev, u8 power,
				u8 antcap)
{
	int err;
	u8 val[SI4713_TXPWR_NRESP];
	/*
	 * 	.First byte = 0
	 * 	.Second byte = 0
	 * 	.Third byte = power
	 * 	.Fourth byte = antcap
	 */
	const u8 args[SI4713_TXPWR_NARGS] = {
		0x00,
		0x00,
		power,
		antcap,
	};

	if (((power > 0) && (power < SI4713_MIN_POWER)) ||
		power > SI4713_MAX_POWER || antcap > SI4713_MAX_ANTCAP)
		return -EDOM;

	err = si4713_send_command(sdev, SI4713_CMD_TX_TUNE_POWER,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (err < 0)
		return err;

	v4l2_dbg(1, debug, &sdev->sd,
			"%s: power=0x%02x antcap=0x%02x status=0x%02x\n",
			__func__, power, antcap, val[0]);

	return si4713_wait_stc(sdev, TIMEOUT_TX_TUNE_POWER);
}

/*
 * si4713_tx_tune_measure - Enters receive mode and measures the received noise
 * 			level in units of dBuV on the selected frequency.
 * 			The Frequency must be between 76 and 108 MHz in 10 kHz
 * 			units and steps of 50 kHz. The command also sets the
 * 			antenna	tuning capacitance. A value of 0 means
 * 			autotuning, and a value of 1 to 191 indicates manual
 * 			override.
 * @sdev: si4713_device structure for the device we are communicating
 * @frequency: desired frequency (76 - 108 MHz, unit 10 KHz, step 50 kHz)
 * @antcap: value of antenna tuning capacitor (0 - 191)
 */
static int si4713_tx_tune_measure(struct si4713_device *sdev, u16 frequency,
					u8 antcap)
{
	int err;
	u8 val[SI4713_TXMEA_NRESP];
	/*
	 * 	.First byte = 0
	 * 	.Second byte = frequency's MSB
	 * 	.Third byte = frequency's LSB
	 * 	.Fourth byte = antcap
	 */
	const u8 args[SI4713_TXMEA_NARGS] = {
		0x00,
		msb(frequency),
		lsb(frequency),
		antcap,
	};

	sdev->tune_rnl = DEFAULT_TUNE_RNL;

	if (antcap > SI4713_MAX_ANTCAP)
		return -EDOM;

	err = si4713_send_command(sdev, SI4713_CMD_TX_TUNE_MEASURE,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (err < 0)
		return err;

	v4l2_dbg(1, debug, &sdev->sd,
			"%s: frequency=0x%02x antcap=0x%02x status=0x%02x\n",
			__func__, frequency, antcap, val[0]);

	return si4713_wait_stc(sdev, TIMEOUT_TX_TUNE);
}

/*
 * si4713_tx_tune_status- Returns the status of the tx_tune_freq, tx_tune_mea or
 * 			tx_tune_power commands. This command return the current
 * 			frequency, output voltage in dBuV, the antenna tunning
 * 			capacitance value and the received noise level. The
 * 			command also clears the stcint interrupt bit when the
 * 			first bit of its arguments is high.
 * @sdev: si4713_device structure for the device we are communicating
 * @intack: 0x01 to clear the seek/tune complete interrupt status indicator.
 * @frequency: returned frequency
 * @power: returned power
 * @antcap: returned antenna capacitance
 * @noise: returned noise level
 */
static int si4713_tx_tune_status(struct si4713_device *sdev, u8 intack,
					u16 *frequency,	u8 *power,
					u8 *antcap, u8 *noise)
{
	int err;
	u8 val[SI4713_TXSTATUS_NRESP];
	/*
	 * 	.First byte = intack bit
	 */
	const u8 args[SI4713_TXSTATUS_NARGS] = {
		intack & SI4713_INTACK_MASK,
	};

	err = si4713_send_command(sdev, SI4713_CMD_TX_TUNE_STATUS,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (!err) {
		v4l2_dbg(1, debug, &sdev->sd,
			"%s: status=0x%02x\n", __func__, val[0]);
		*frequency = compose_u16(val[2], val[3]);
		sdev->frequency = *frequency;
		*power = val[5];
		*antcap = val[6];
		*noise = val[7];
		v4l2_dbg(1, debug, &sdev->sd, "%s: response: %d x 10 kHz "
				"(power %d, antcap %d, rnl %d)\n", __func__,
				*frequency, *power, *antcap, *noise);
	}

	return err;
}

/*
 * si4713_tx_rds_buff - Loads the RDS group buffer FIFO or circular buffer.
 * @sdev: si4713_device structure for the device we are communicating
 * @mode: the buffer operation mode.
 * @rdsb: RDS Block B
 * @rdsc: RDS Block C
 * @rdsd: RDS Block D
 * @cbleft: returns the number of available circular buffer blocks minus the
 *          number of used circular buffer blocks.
 */
static int si4713_tx_rds_buff(struct si4713_device *sdev, u8 mode, u16 rdsb,
				u16 rdsc, u16 rdsd, s8 *cbleft)
{
	int err;
	u8 val[SI4713_RDSBUFF_NRESP];

	const u8 args[SI4713_RDSBUFF_NARGS] = {
		mode & SI4713_RDSBUFF_MODE_MASK,
		msb(rdsb),
		lsb(rdsb),
		msb(rdsc),
		lsb(rdsc),
		msb(rdsd),
		lsb(rdsd),
	};

	err = si4713_send_command(sdev, SI4713_CMD_TX_RDS_BUFF,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (!err) {
		v4l2_dbg(1, debug, &sdev->sd,
			"%s: status=0x%02x\n", __func__, val[0]);
		*cbleft = (s8)val[2] - val[3];
		v4l2_dbg(1, debug, &sdev->sd, "%s: response: interrupts"
				" 0x%02x cb avail: %d cb used %d fifo avail"
				" %d fifo used %d\n", __func__, val[1],
				val[2], val[3], val[4], val[5]);
	}

	return err;
}

/*
 * si4713_tx_rds_ps - Loads the program service buffer.
 * @sdev: si4713_device structure for the device we are communicating
 * @psid: program service id to be loaded.
 * @pschar: assumed 4 size char array to be loaded into the program service
 */
static int si4713_tx_rds_ps(struct si4713_device *sdev, u8 psid,
				unsigned char *pschar)
{
	int err;
	u8 val[SI4713_RDSPS_NRESP];

	const u8 args[SI4713_RDSPS_NARGS] = {
		psid & SI4713_RDSPS_PSID_MASK,
		pschar[0],
		pschar[1],
		pschar[2],
		pschar[3],
	};

	err = si4713_send_command(sdev, SI4713_CMD_TX_RDS_PS,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (err < 0)
		return err;

	v4l2_dbg(1, debug, &sdev->sd, "%s: status=0x%02x\n", __func__, val[0]);

	return err;
}

static int si4713_set_power_state(struct si4713_device *sdev, u8 value)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (value)
		rval = si4713_powerup(sdev);
	else
		rval = si4713_powerdown(sdev);

	mutex_unlock(&sdev->mutex);
	return rval;
}

static int si4713_set_mute(struct si4713_device *sdev, u16 mute)
{
	int rval = 0;

	mute = set_mute(mute);

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev,
				SI4713_TX_LINE_INPUT_MUTE, mute);

	if (rval >= 0)
		sdev->mute = get_mute(mute);

	mutex_unlock(&sdev->mutex);

	return rval;
}

static int si4713_set_rds_ps_name(struct si4713_device *sdev, char *ps_name)
{
	int rval = 0, i;
	u8 len = 0;

	/* We want to clear the whole thing */
	if (!strlen(ps_name))
		memset(ps_name, 0, MAX_RDS_PS_NAME + 1);

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		/* Write the new ps name and clear the padding */
		for (i = 0; i < MAX_RDS_PS_NAME; i += (RDS_BLOCK / 2)) {
			rval = si4713_tx_rds_ps(sdev, (i / (RDS_BLOCK / 2)),
						ps_name + i);
			if (rval < 0)
				goto unlock;
		}

		/* Setup the size to be sent */
		if (strlen(ps_name))
			len = strlen(ps_name) - 1;
		else
			len = 1;

		rval = si4713_write_property(sdev,
				SI4713_TX_RDS_PS_MESSAGE_COUNT,
				rds_ps_nblocks(len));
		if (rval < 0)
			goto unlock;

		rval = si4713_write_property(sdev,
				SI4713_TX_RDS_PS_REPEAT_COUNT,
				DEFAULT_RDS_PS_REPEAT_COUNT * 2);
		if (rval < 0)
			goto unlock;
	}

	strncpy(sdev->rds_info.ps_name, ps_name, MAX_RDS_PS_NAME);

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

static int si4713_set_rds_radio_text(struct si4713_device *sdev, char *rt)
{
	int rval = 0, i;
	u16 t_index = 0;
	u8 b_index = 0, cr_inserted = 0;
	s8 left;

	mutex_lock(&sdev->mutex);

	if (!sdev->power_state)
		goto copy;

	rval = si4713_tx_rds_buff(sdev, RDS_BLOCK_CLEAR, 0, 0, 0, &left);
	if (rval < 0)
		goto unlock;

	if (!strlen(rt))
		goto copy;

	do {
		/* RDS spec says that if the last block isn't used,
		 * then apply a carriage return
		 */
		if (t_index < (RDS_RADIOTEXT_INDEX_MAX *
			RDS_RADIOTEXT_BLK_SIZE)) {
			for (i = 0; i < RDS_RADIOTEXT_BLK_SIZE; i++) {
				if (!rt[t_index + i] || rt[t_index + i] ==
					RDS_CARRIAGE_RETURN) {
					rt[t_index + i] = RDS_CARRIAGE_RETURN;
					cr_inserted = 1;
					break;
				}
			}
		}

		rval = si4713_tx_rds_buff(sdev, RDS_BLOCK_LOAD,
				compose_u16(RDS_RADIOTEXT_2A, b_index++),
				compose_u16(rt[t_index], rt[t_index + 1]),
				compose_u16(rt[t_index + 2], rt[t_index + 3]),
				&left);
		if (rval < 0)
			goto unlock;

		t_index += RDS_RADIOTEXT_BLK_SIZE;

		if (cr_inserted)
			break;
	} while (left > 0);

copy:
	strncpy(sdev->rds_info.radio_text, rt, MAX_RDS_RADIO_TEXT);

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

static int si4713_choose_econtrol_action(struct si4713_device *sdev, u32 id,
		u32 **shadow, s32 *bit, s32 *mask, u16 *property, int *mul,
		unsigned long **table, int *size)
{
	s32 rval = 0;

	switch (id) {
	/* FM_TX class controls */
	case V4L2_CID_RDS_TX_PI:
		*property = SI4713_TX_RDS_PI;
		*mul = 1;
		*shadow = &sdev->rds_info.pi;
		break;
	case V4L2_CID_AUDIO_COMPRESSION_THRESHOLD:
		*property = SI4713_TX_ACOMP_THRESHOLD;
		*mul = 1;
		*shadow = &sdev->acomp_info.threshold;
		break;
	case V4L2_CID_AUDIO_COMPRESSION_GAIN:
		*property = SI4713_TX_ACOMP_GAIN;
		*mul = 1;
		*shadow = &sdev->acomp_info.gain;
		break;
	case V4L2_CID_PILOT_TONE_FREQUENCY:
		*property = SI4713_TX_PILOT_FREQUENCY;
		*mul = 1;
		*shadow = &sdev->pilot_info.frequency;
		break;
	case V4L2_CID_AUDIO_COMPRESSION_ATTACK_TIME:
		*property = SI4713_TX_ACOMP_ATTACK_TIME;
		*mul = ATTACK_TIME_UNIT;
		*shadow = &sdev->acomp_info.attack_time;
		break;
	case V4L2_CID_PILOT_TONE_DEVIATION:
		*property = SI4713_TX_PILOT_DEVIATION;
		*mul = 10;
		*shadow = &sdev->pilot_info.deviation;
		break;
	case V4L2_CID_AUDIO_LIMITER_DEVIATION:
		*property = SI4713_TX_AUDIO_DEVIATION;
		*mul = 10;
		*shadow = &sdev->limiter_info.deviation;
		break;
	case V4L2_CID_RDS_TX_DEVIATION:
		*property = SI4713_TX_RDS_DEVIATION;
		*mul = 1;
		*shadow = &sdev->rds_info.deviation;
		break;

	case V4L2_CID_RDS_TX_PTY:
		*property = SI4713_TX_RDS_PS_MISC;
		*bit = 5;
		*mask = 0x1F << 5;
		*shadow = &sdev->rds_info.pty;
		break;
	case V4L2_CID_AUDIO_LIMITER_ENABLED:
		*property = SI4713_TX_ACOMP_ENABLE;
		*bit = 1;
		*mask = 1 << 1;
		*shadow = &sdev->limiter_info.enabled;
		break;
	case V4L2_CID_AUDIO_COMPRESSION_ENABLED:
		*property = SI4713_TX_ACOMP_ENABLE;
		*bit = 0;
		*mask = 1 << 0;
		*shadow = &sdev->acomp_info.enabled;
		break;
	case V4L2_CID_PILOT_TONE_ENABLED:
		*property = SI4713_TX_COMPONENT_ENABLE;
		*bit = 0;
		*mask = 1 << 0;
		*shadow = &sdev->pilot_info.enabled;
		break;

	case V4L2_CID_AUDIO_LIMITER_RELEASE_TIME:
		*property = SI4713_TX_LIMITER_RELEASE_TIME;
		*table = limiter_times;
		*size = ARRAY_SIZE(limiter_times);
		*shadow = &sdev->limiter_info.release_time;
		break;
	case V4L2_CID_AUDIO_COMPRESSION_RELEASE_TIME:
		*property = SI4713_TX_ACOMP_RELEASE_TIME;
		*table = acomp_rtimes;
		*size = ARRAY_SIZE(acomp_rtimes);
		*shadow = &sdev->acomp_info.release_time;
		break;
	case V4L2_CID_TUNE_PREEMPHASIS:
		*property = SI4713_TX_PREEMPHASIS;
		*table = preemphasis_values;
		*size = ARRAY_SIZE(preemphasis_values);
		*shadow = &sdev->preemphasis;
		break;

	default:
		rval = -EINVAL;
	};

	return rval;
}

static int si4713_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc);

/* write string property */
static int si4713_write_econtrol_string(struct si4713_device *sdev,
				struct v4l2_ext_control *control)
{
	struct v4l2_queryctrl vqc;
	int len;
	s32 rval = 0;

	vqc.id = control->id;
	rval = si4713_queryctrl(&sdev->sd, &vqc);
	if (rval < 0)
		goto exit;

	switch (control->id) {
	case V4L2_CID_RDS_TX_PS_NAME: {
		char ps_name[MAX_RDS_PS_NAME + 1];

		len = control->size - 1;
		if (len > MAX_RDS_PS_NAME) {
			rval = -ERANGE;
			goto exit;
		}
		rval = copy_from_user(ps_name, control->string, len);
		if (rval < 0)
			goto exit;
		ps_name[len] = '\0';

		if (strlen(ps_name) % vqc.step) {
			rval = -ERANGE;
			goto exit;
		}

		rval = si4713_set_rds_ps_name(sdev, ps_name);
	}
		break;

	case V4L2_CID_RDS_TX_RADIO_TEXT: {
		char radio_text[MAX_RDS_RADIO_TEXT + 1];

		len = control->size - 1;
		if (len > MAX_RDS_RADIO_TEXT) {
			rval = -ERANGE;
			goto exit;
		}
		rval = copy_from_user(radio_text, control->string, len);
		if (rval < 0)
			goto exit;
		radio_text[len] = '\0';

		if (strlen(radio_text) % vqc.step) {
			rval = -ERANGE;
			goto exit;
		}

		rval = si4713_set_rds_radio_text(sdev, radio_text);
	}
		break;

	default:
		rval = -EINVAL;
		break;
	};

exit:
	return rval;
}

static int validate_range(struct v4l2_subdev *sd,
					struct v4l2_ext_control *control)
{
	struct v4l2_queryctrl vqc;
	int rval;

	vqc.id = control->id;
	rval = si4713_queryctrl(sd, &vqc);
	if (rval < 0)
		goto exit;

	if (control->value < vqc.minimum || control->value > vqc.maximum)
		rval = -ERANGE;

exit:
	return rval;
}

/* properties which use tx_tune_power*/
static int si4713_write_econtrol_tune(struct si4713_device *sdev,
				struct v4l2_ext_control *control)
{
	s32 rval = 0;
	u8 power, antcap;

	rval = validate_range(&sdev->sd, control);
	if (rval < 0)
		goto exit;

	mutex_lock(&sdev->mutex);

	switch (control->id) {
	case V4L2_CID_TUNE_POWER_LEVEL:
		power = control->value;
		antcap = sdev->antenna_capacitor;
		break;
	case V4L2_CID_TUNE_ANTENNA_CAPACITOR:
		power = sdev->power_level;
		antcap = control->value;
		break;
	default:
		rval = -EINVAL;
		goto unlock;
	};

	if (sdev->power_state)
		rval = si4713_tx_tune_power(sdev, power, antcap);

	if (rval == 0) {
		sdev->power_level = power;
		sdev->antenna_capacitor = antcap;
	}

unlock:
	mutex_unlock(&sdev->mutex);
exit:
	return rval;
}

static int si4713_write_econtrol_integers(struct si4713_device *sdev,
					struct v4l2_ext_control *control)
{
	s32 rval;
	u32 *shadow = NULL, val = 0;
	s32 bit = 0, mask = 0;
	u16 property = 0;
	int mul = 0;
	unsigned long *table = NULL;
	int size = 0;

	rval = validate_range(&sdev->sd, control);
	if (rval < 0)
		goto exit;

	rval = si4713_choose_econtrol_action(sdev, control->id, &shadow, &bit,
			&mask, &property, &mul, &table, &size);
	if (rval < 0)
		goto exit;

	val = control->value;
	if (mul) {
		val = control->value / mul;
	} else if (table) {
		rval = usecs_to_dev(control->value, table, size);
		if (rval < 0)
			goto exit;
		val = rval;
		rval = 0;
	}

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		if (mask) {
			rval = si4713_read_property(sdev, property, &val);
			if (rval < 0)
				goto unlock;
			val = set_bits(val, control->value, bit, mask);
		}

		rval = si4713_write_property(sdev, property, val);
		if (rval < 0)
			goto unlock;
		if (mask)
			val = control->value;
	}

	if (mul) {
		*shadow = val * mul;
	} else if (table) {
		rval = dev_to_usecs(val, table, size);
		if (rval < 0)
			goto unlock;
		*shadow = rval;
		rval = 0;
	} else {
		*shadow = val;
	}

unlock:
	mutex_unlock(&sdev->mutex);
exit:
	return rval;
}

static int si4713_s_frequency(struct v4l2_subdev *sd, struct v4l2_frequency *f);
static int si4713_s_modulator(struct v4l2_subdev *sd, struct v4l2_modulator *);
/*
 * si4713_setup - Sets the device up with current configuration.
 * @sdev: si4713_device structure for the device we are communicating
 */
static int si4713_setup(struct si4713_device *sdev)
{
	struct v4l2_ext_control ctrl;
	struct v4l2_frequency f;
	struct v4l2_modulator vm;
	struct si4713_device *tmp;
	int rval = 0;

	tmp = kmalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	/* Get a local copy to avoid race */
	mutex_lock(&sdev->mutex);
	memcpy(tmp, sdev, sizeof(*sdev));
	mutex_unlock(&sdev->mutex);

	ctrl.id = V4L2_CID_RDS_TX_PI;
	ctrl.value = tmp->rds_info.pi;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_AUDIO_COMPRESSION_THRESHOLD;
	ctrl.value = tmp->acomp_info.threshold;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_AUDIO_COMPRESSION_GAIN;
	ctrl.value = tmp->acomp_info.gain;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_PILOT_TONE_FREQUENCY;
	ctrl.value = tmp->pilot_info.frequency;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_AUDIO_COMPRESSION_ATTACK_TIME;
	ctrl.value = tmp->acomp_info.attack_time;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_PILOT_TONE_DEVIATION;
	ctrl.value = tmp->pilot_info.deviation;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_AUDIO_LIMITER_DEVIATION;
	ctrl.value = tmp->limiter_info.deviation;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_RDS_TX_DEVIATION;
	ctrl.value = tmp->rds_info.deviation;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_RDS_TX_PTY;
	ctrl.value = tmp->rds_info.pty;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_AUDIO_LIMITER_ENABLED;
	ctrl.value = tmp->limiter_info.enabled;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_AUDIO_COMPRESSION_ENABLED;
	ctrl.value = tmp->acomp_info.enabled;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_PILOT_TONE_ENABLED;
	ctrl.value = tmp->pilot_info.enabled;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_AUDIO_LIMITER_RELEASE_TIME;
	ctrl.value = tmp->limiter_info.release_time;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_AUDIO_COMPRESSION_RELEASE_TIME;
	ctrl.value = tmp->acomp_info.release_time;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_TUNE_PREEMPHASIS;
	ctrl.value = tmp->preemphasis;
	rval |= si4713_write_econtrol_integers(sdev, &ctrl);

	ctrl.id = V4L2_CID_RDS_TX_PS_NAME;
	rval |= si4713_set_rds_ps_name(sdev, tmp->rds_info.ps_name);

	ctrl.id = V4L2_CID_RDS_TX_RADIO_TEXT;
	rval |= si4713_set_rds_radio_text(sdev, tmp->rds_info.radio_text);

	/* Device procedure needs to set frequency first */
	f.frequency = tmp->frequency ? tmp->frequency : DEFAULT_FREQUENCY;
	f.frequency = si4713_to_v4l2(f.frequency);
	rval |= si4713_s_frequency(&sdev->sd, &f);

	ctrl.id = V4L2_CID_TUNE_POWER_LEVEL;
	ctrl.value = tmp->power_level;
	rval |= si4713_write_econtrol_tune(sdev, &ctrl);

	ctrl.id = V4L2_CID_TUNE_ANTENNA_CAPACITOR;
	ctrl.value = tmp->antenna_capacitor;
	rval |= si4713_write_econtrol_tune(sdev, &ctrl);

	vm.index = 0;
	if (tmp->stereo)
		vm.txsubchans = V4L2_TUNER_SUB_STEREO;
	else
		vm.txsubchans = V4L2_TUNER_SUB_MONO;
	if (tmp->rds_info.enabled)
		vm.txsubchans |= V4L2_TUNER_SUB_RDS;
	si4713_s_modulator(&sdev->sd, &vm);

	kfree(tmp);

	return rval;
}

/*
 * si4713_initialize - Sets the device up with default configuration.
 * @sdev: si4713_device structure for the device we are communicating
 */
static int si4713_initialize(struct si4713_device *sdev)
{
	int rval;

	rval = si4713_set_power_state(sdev, POWER_ON);
	if (rval < 0)
		goto exit;

	rval = si4713_checkrev(sdev);
	if (rval < 0)
		goto exit;

	rval = si4713_set_power_state(sdev, POWER_OFF);
	if (rval < 0)
		goto exit;

	mutex_lock(&sdev->mutex);

	sdev->rds_info.pi = DEFAULT_RDS_PI;
	sdev->rds_info.pty = DEFAULT_RDS_PTY;
	sdev->rds_info.deviation = DEFAULT_RDS_DEVIATION;
	strlcpy(sdev->rds_info.ps_name, DEFAULT_RDS_PS_NAME, MAX_RDS_PS_NAME);
	strlcpy(sdev->rds_info.radio_text, DEFAULT_RDS_RADIO_TEXT,
							MAX_RDS_RADIO_TEXT);
	sdev->rds_info.enabled = 1;

	sdev->limiter_info.release_time = DEFAULT_LIMITER_RTIME;
	sdev->limiter_info.deviation = DEFAULT_LIMITER_DEV;
	sdev->limiter_info.enabled = 1;

	sdev->pilot_info.deviation = DEFAULT_PILOT_DEVIATION;
	sdev->pilot_info.frequency = DEFAULT_PILOT_FREQUENCY;
	sdev->pilot_info.enabled = 1;

	sdev->acomp_info.release_time = DEFAULT_ACOMP_RTIME;
	sdev->acomp_info.attack_time = DEFAULT_ACOMP_ATIME;
	sdev->acomp_info.threshold = DEFAULT_ACOMP_THRESHOLD;
	sdev->acomp_info.gain = DEFAULT_ACOMP_GAIN;
	sdev->acomp_info.enabled = 1;

	sdev->frequency = DEFAULT_FREQUENCY;
	sdev->preemphasis = DEFAULT_PREEMPHASIS;
	sdev->mute = DEFAULT_MUTE;
	sdev->power_level = DEFAULT_POWER_LEVEL;
	sdev->antenna_capacitor = 0;
	sdev->stereo = 1;
	sdev->tune_rnl = DEFAULT_TUNE_RNL;

	mutex_unlock(&sdev->mutex);

exit:
	return rval;
}

/* read string property */
static int si4713_read_econtrol_string(struct si4713_device *sdev,
				struct v4l2_ext_control *control)
{
	s32 rval = 0;

	switch (control->id) {
	case V4L2_CID_RDS_TX_PS_NAME:
		if (strlen(sdev->rds_info.ps_name) + 1 > control->size) {
			control->size = MAX_RDS_PS_NAME + 1;
			rval = -ENOSPC;
			goto exit;
		}
		rval = copy_to_user(control->string, sdev->rds_info.ps_name,
					strlen(sdev->rds_info.ps_name) + 1);
		break;

	case V4L2_CID_RDS_TX_RADIO_TEXT:
		if (strlen(sdev->rds_info.radio_text) + 1 > control->size) {
			control->size = MAX_RDS_RADIO_TEXT + 1;
			rval = -ENOSPC;
			goto exit;
		}
		rval = copy_to_user(control->string, sdev->rds_info.radio_text,
					strlen(sdev->rds_info.radio_text) + 1);
		break;

	default:
		rval = -EINVAL;
		break;
	};

exit:
	return rval;
}

/*
 * si4713_update_tune_status - update properties from tx_tune_status
 * command. Must be called with sdev->mutex held.
 * @sdev: si4713_device structure for the device we are communicating
 */
static int si4713_update_tune_status(struct si4713_device *sdev)
{
	int rval;
	u16 f = 0;
	u8 p = 0, a = 0, n = 0;

	rval = si4713_tx_tune_status(sdev, 0x00, &f, &p, &a, &n);

	if (rval < 0)
		goto exit;

	sdev->power_level = p;
	sdev->antenna_capacitor = a;
	sdev->tune_rnl = n;

exit:
	return rval;
}

/* properties which use tx_tune_status */
static int si4713_read_econtrol_tune(struct si4713_device *sdev,
				struct v4l2_ext_control *control)
{
	s32 rval = 0;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_update_tune_status(sdev);
		if (rval < 0)
			goto unlock;
	}

	switch (control->id) {
	case V4L2_CID_TUNE_POWER_LEVEL:
		control->value = sdev->power_level;
		break;
	case V4L2_CID_TUNE_ANTENNA_CAPACITOR:
		control->value = sdev->antenna_capacitor;
		break;
	default:
		rval = -EINVAL;
	};

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

static int si4713_read_econtrol_integers(struct si4713_device *sdev,
				struct v4l2_ext_control *control)
{
	s32 rval;
	u32 *shadow = NULL, val = 0;
	s32 bit = 0, mask = 0;
	u16 property = 0;
	int mul = 0;
	unsigned long *table = NULL;
	int size = 0;

	rval = si4713_choose_econtrol_action(sdev, control->id, &shadow, &bit,
			&mask, &property, &mul, &table, &size);
	if (rval < 0)
		goto exit;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, property, &val);
		if (rval < 0)
			goto unlock;

		/* Keep negative values for threshold */
		if (control->id == V4L2_CID_AUDIO_COMPRESSION_THRESHOLD)
			*shadow = (s16)val;
		else if (mask)
			*shadow = get_status_bit(val, bit, mask);
		else if (mul)
			*shadow = val * mul;
		else
			*shadow = dev_to_usecs(val, table, size);
	}

	control->value = *shadow;

unlock:
	mutex_unlock(&sdev->mutex);
exit:
	return rval;
}

/*
 * Video4Linux Subdev Interface
 */
/* si4713_s_ext_ctrls - set extended controls value */
static int si4713_s_ext_ctrls(struct v4l2_subdev *sd,
				struct v4l2_ext_controls *ctrls)
{
	struct si4713_device *sdev = to_si4713_device(sd);
	int i;

	if (ctrls->ctrl_class != V4L2_CTRL_CLASS_FM_TX)
		return -EINVAL;

	for (i = 0; i < ctrls->count; i++) {
		int err;

		switch ((ctrls->controls + i)->id) {
		case V4L2_CID_RDS_TX_PS_NAME:
		case V4L2_CID_RDS_TX_RADIO_TEXT:
			err = si4713_write_econtrol_string(sdev,
							ctrls->controls + i);
			break;
		case V4L2_CID_TUNE_ANTENNA_CAPACITOR:
		case V4L2_CID_TUNE_POWER_LEVEL:
			err = si4713_write_econtrol_tune(sdev,
							ctrls->controls + i);
			break;
		default:
			err = si4713_write_econtrol_integers(sdev,
							ctrls->controls + i);
		}

		if (err < 0) {
			ctrls->error_idx = i;
			return err;
		}
	}

	return 0;
}

/* si4713_g_ext_ctrls - get extended controls value */
static int si4713_g_ext_ctrls(struct v4l2_subdev *sd,
				struct v4l2_ext_controls *ctrls)
{
	struct si4713_device *sdev = to_si4713_device(sd);
	int i;

	if (ctrls->ctrl_class != V4L2_CTRL_CLASS_FM_TX)
		return -EINVAL;

	for (i = 0; i < ctrls->count; i++) {
		int err;

		switch ((ctrls->controls + i)->id) {
		case V4L2_CID_RDS_TX_PS_NAME:
		case V4L2_CID_RDS_TX_RADIO_TEXT:
			err = si4713_read_econtrol_string(sdev,
							ctrls->controls + i);
			break;
		case V4L2_CID_TUNE_ANTENNA_CAPACITOR:
		case V4L2_CID_TUNE_POWER_LEVEL:
			err = si4713_read_econtrol_tune(sdev,
							ctrls->controls + i);
			break;
		default:
			err = si4713_read_econtrol_integers(sdev,
							ctrls->controls + i);
		}

		if (err < 0) {
			ctrls->error_idx = i;
			return err;
		}
	}

	return 0;
}

/* si4713_queryctrl - enumerate control items */
static int si4713_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	int rval = 0;

	switch (qc->id) {
	/* User class controls */
	case V4L2_CID_AUDIO_MUTE:
		rval = v4l2_ctrl_query_fill(qc, 0, 1, 1, DEFAULT_MUTE);
		break;
	/* FM_TX class controls */
	case V4L2_CID_RDS_TX_PI:
		rval = v4l2_ctrl_query_fill(qc, 0, 0xFFFF, 1, DEFAULT_RDS_PI);
		break;
	case V4L2_CID_RDS_TX_PTY:
		rval = v4l2_ctrl_query_fill(qc, 0, 31, 1, DEFAULT_RDS_PTY);
		break;
	case V4L2_CID_RDS_TX_DEVIATION:
		rval = v4l2_ctrl_query_fill(qc, 0, MAX_RDS_DEVIATION,
						10, DEFAULT_RDS_DEVIATION);
		break;
	case V4L2_CID_RDS_TX_PS_NAME:
		/*
		 * Report step as 8. From RDS spec, psname
		 * should be 8. But there are receivers which scroll strings
		 * sized as 8xN.
		 */
		rval = v4l2_ctrl_query_fill(qc, 0, MAX_RDS_PS_NAME, 8, 0);
		break;
	case V4L2_CID_RDS_TX_RADIO_TEXT:
		/*
		 * Report step as 32 (2A block). From RDS spec,
		 * radio text should be 32 for 2A block. But there are receivers
		 * which scroll strings sized as 32xN. Setting default to 32.
		 */
		rval = v4l2_ctrl_query_fill(qc, 0, MAX_RDS_RADIO_TEXT, 32, 0);
		break;

	case V4L2_CID_AUDIO_LIMITER_ENABLED:
		rval = v4l2_ctrl_query_fill(qc, 0, 1, 1, 1);
		break;
	case V4L2_CID_AUDIO_LIMITER_RELEASE_TIME:
		rval = v4l2_ctrl_query_fill(qc, 250, MAX_LIMITER_RELEASE_TIME,
						50, DEFAULT_LIMITER_RTIME);
		break;
	case V4L2_CID_AUDIO_LIMITER_DEVIATION:
		rval = v4l2_ctrl_query_fill(qc, 0, MAX_LIMITER_DEVIATION,
						10, DEFAULT_LIMITER_DEV);
		break;

	case V4L2_CID_AUDIO_COMPRESSION_ENABLED:
		rval = v4l2_ctrl_query_fill(qc, 0, 1, 1, 1);
		break;
	case V4L2_CID_AUDIO_COMPRESSION_GAIN:
		rval = v4l2_ctrl_query_fill(qc, 0, MAX_ACOMP_GAIN, 1,
						DEFAULT_ACOMP_GAIN);
		break;
	case V4L2_CID_AUDIO_COMPRESSION_THRESHOLD:
		rval = v4l2_ctrl_query_fill(qc, MIN_ACOMP_THRESHOLD,
						MAX_ACOMP_THRESHOLD, 1,
						DEFAULT_ACOMP_THRESHOLD);
		break;
	case V4L2_CID_AUDIO_COMPRESSION_ATTACK_TIME:
		rval = v4l2_ctrl_query_fill(qc, 0, MAX_ACOMP_ATTACK_TIME,
						500, DEFAULT_ACOMP_ATIME);
		break;
	case V4L2_CID_AUDIO_COMPRESSION_RELEASE_TIME:
		rval = v4l2_ctrl_query_fill(qc, 100000, MAX_ACOMP_RELEASE_TIME,
						100000, DEFAULT_ACOMP_RTIME);
		break;

	case V4L2_CID_PILOT_TONE_ENABLED:
		rval = v4l2_ctrl_query_fill(qc, 0, 1, 1, 1);
		break;
	case V4L2_CID_PILOT_TONE_DEVIATION:
		rval = v4l2_ctrl_query_fill(qc, 0, MAX_PILOT_DEVIATION,
						10, DEFAULT_PILOT_DEVIATION);
		break;
	case V4L2_CID_PILOT_TONE_FREQUENCY:
		rval = v4l2_ctrl_query_fill(qc, 0, MAX_PILOT_FREQUENCY,
						1, DEFAULT_PILOT_FREQUENCY);
		break;

	case V4L2_CID_TUNE_PREEMPHASIS:
		rval = v4l2_ctrl_query_fill(qc, V4L2_PREEMPHASIS_DISABLED,
						V4L2_PREEMPHASIS_75_uS, 1,
						V4L2_PREEMPHASIS_50_uS);
		break;
	case V4L2_CID_TUNE_POWER_LEVEL:
		rval = v4l2_ctrl_query_fill(qc, 0, 120, 1, DEFAULT_POWER_LEVEL);
		break;
	case V4L2_CID_TUNE_ANTENNA_CAPACITOR:
		rval = v4l2_ctrl_query_fill(qc, 0, 191, 1, 0);
		break;
	default:
		rval = -EINVAL;
		break;
	};

	return rval;
}

/* si4713_g_ctrl - get the value of a control */
static int si4713_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct si4713_device *sdev = to_si4713_device(sd);
	int rval = 0;

	if (!sdev)
		return -ENODEV;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_LINE_INPUT_MUTE,
						&sdev->mute);

		if (rval < 0)
			goto unlock;
	}

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = get_mute(sdev->mute);
		break;
	}

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

/* si4713_s_ctrl - set the value of a control */
static int si4713_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct si4713_device *sdev = to_si4713_device(sd);
	int rval = 0;

	if (!sdev)
		return -ENODEV;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value) {
			rval = si4713_set_mute(sdev, ctrl->value);
			if (rval < 0)
				goto exit;

			rval = si4713_set_power_state(sdev, POWER_DOWN);
		} else {
			rval = si4713_set_power_state(sdev, POWER_UP);
			if (rval < 0)
				goto exit;

			rval = si4713_setup(sdev);
			if (rval < 0)
				goto exit;

			rval = si4713_set_mute(sdev, ctrl->value);
		}
		break;
	}

exit:
	return rval;
}

/* si4713_ioctl - deal with private ioctls (only rnl for now) */
long si4713_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct si4713_device *sdev = to_si4713_device(sd);
	struct si4713_rnl *rnl = arg;
	u16 frequency;
	int rval = 0;

	if (!arg)
		return -EINVAL;

	mutex_lock(&sdev->mutex);
	switch (cmd) {
	case SI4713_IOC_MEASURE_RNL:
		frequency = v4l2_to_si4713(rnl->frequency);

		if (sdev->power_state) {
			/* Set desired measurement frequency */
			rval = si4713_tx_tune_measure(sdev, frequency, 0);
			if (rval < 0)
				goto unlock;
			/* get results from tune status */
			rval = si4713_update_tune_status(sdev);
			if (rval < 0)
				goto unlock;
		}
		rnl->rnl = sdev->tune_rnl;
		break;

	default:
		/* nothing */
		rval = -ENOIOCTLCMD;
	}

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

static const struct v4l2_subdev_core_ops si4713_subdev_core_ops = {
	.queryctrl	= si4713_queryctrl,
	.g_ext_ctrls	= si4713_g_ext_ctrls,
	.s_ext_ctrls	= si4713_s_ext_ctrls,
	.g_ctrl		= si4713_g_ctrl,
	.s_ctrl		= si4713_s_ctrl,
	.ioctl		= si4713_ioctl,
};

/* si4713_g_modulator - get modulator attributes */
static int si4713_g_modulator(struct v4l2_subdev *sd, struct v4l2_modulator *vm)
{
	struct si4713_device *sdev = to_si4713_device(sd);
	int rval = 0;

	if (!sdev) {
		rval = -ENODEV;
		goto exit;
	}

	if (vm->index > 0) {
		rval = -EINVAL;
		goto exit;
	}

	strncpy(vm->name, "FM Modulator", 32);
	vm->capability = V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_LOW |
						V4L2_TUNER_CAP_RDS;

	/* Report current frequency range limits */
	vm->rangelow = si4713_to_v4l2(FREQ_RANGE_LOW);
	vm->rangehigh = si4713_to_v4l2(FREQ_RANGE_HIGH);

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		u32 comp_en = 0;

		rval = si4713_read_property(sdev, SI4713_TX_COMPONENT_ENABLE,
						&comp_en);
		if (rval < 0)
			goto unlock;

		sdev->stereo = get_status_bit(comp_en, 1, 1 << 1);
		sdev->rds_info.enabled = get_status_bit(comp_en, 2, 1 << 2);
	}

	/* Report current audio mode: mono or stereo */
	if (sdev->stereo)
		vm->txsubchans = V4L2_TUNER_SUB_STEREO;
	else
		vm->txsubchans = V4L2_TUNER_SUB_MONO;

	/* Report rds feature status */
	if (sdev->rds_info.enabled)
		vm->txsubchans |= V4L2_TUNER_SUB_RDS;
	else
		vm->txsubchans &= ~V4L2_TUNER_SUB_RDS;

unlock:
	mutex_unlock(&sdev->mutex);
exit:
	return rval;
}

/* si4713_s_modulator - set modulator attributes */
static int si4713_s_modulator(struct v4l2_subdev *sd, struct v4l2_modulator *vm)
{
	struct si4713_device *sdev = to_si4713_device(sd);
	int rval = 0;
	u16 stereo, rds;
	u32 p;

	if (!sdev)
		return -ENODEV;

	if (vm->index > 0)
		return -EINVAL;

	/* Set audio mode: mono or stereo */
	if (vm->txsubchans & V4L2_TUNER_SUB_STEREO)
		stereo = 1;
	else if (vm->txsubchans & V4L2_TUNER_SUB_MONO)
		stereo = 0;
	else
		return -EINVAL;

	rds = !!(vm->txsubchans & V4L2_TUNER_SUB_RDS);

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev,
						SI4713_TX_COMPONENT_ENABLE, &p);
		if (rval < 0)
			goto unlock;

		p = set_bits(p, stereo, 1, 1 << 1);
		p = set_bits(p, rds, 2, 1 << 2);

		rval = si4713_write_property(sdev,
						SI4713_TX_COMPONENT_ENABLE, p);
		if (rval < 0)
			goto unlock;
	}

	sdev->stereo = stereo;
	sdev->rds_info.enabled = rds;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

/* si4713_g_frequency - get tuner or modulator radio frequency */
static int si4713_g_frequency(struct v4l2_subdev *sd, struct v4l2_frequency *f)
{
	struct si4713_device *sdev = to_si4713_device(sd);
	int rval = 0;

	f->type = V4L2_TUNER_RADIO;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		u16 freq;
		u8 p, a, n;

		rval = si4713_tx_tune_status(sdev, 0x00, &freq, &p, &a, &n);
		if (rval < 0)
			goto unlock;

		sdev->frequency = freq;
	}

	f->frequency = si4713_to_v4l2(sdev->frequency);

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

/* si4713_s_frequency - set tuner or modulator radio frequency */
static int si4713_s_frequency(struct v4l2_subdev *sd, struct v4l2_frequency *f)
{
	struct si4713_device *sdev = to_si4713_device(sd);
	int rval = 0;
	u16 frequency = v4l2_to_si4713(f->frequency);

	/* Check frequency range */
	if (frequency < FREQ_RANGE_LOW || frequency > FREQ_RANGE_HIGH)
		return -EDOM;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_tx_tune_freq(sdev, frequency);
		if (rval < 0)
			goto unlock;
		frequency = rval;
		rval = 0;
	}
	sdev->frequency = frequency;
	f->frequency = si4713_to_v4l2(frequency);

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

static const struct v4l2_subdev_tuner_ops si4713_subdev_tuner_ops = {
	.g_frequency	= si4713_g_frequency,
	.s_frequency	= si4713_s_frequency,
	.g_modulator	= si4713_g_modulator,
	.s_modulator	= si4713_s_modulator,
};

static const struct v4l2_subdev_ops si4713_subdev_ops = {
	.core		= &si4713_subdev_core_ops,
	.tuner		= &si4713_subdev_tuner_ops,
};

/*
 * I2C driver interface
 */
/* si4713_probe - probe for the device */
static int si4713_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct si4713_device *sdev;
	int rval;

	sdev = kzalloc(sizeof *sdev, GFP_KERNEL);
	if (!sdev) {
		dev_err(&client->dev, "Failed to alloc video device.\n");
		rval = -ENOMEM;
		goto exit;
	}

	sdev->platform_data = client->dev.platform_data;
	if (!sdev->platform_data) {
		v4l2_err(&sdev->sd, "No platform data registered.\n");
		rval = -ENODEV;
		goto free_sdev;
	}

	v4l2_i2c_subdev_init(&sdev->sd, client, &si4713_subdev_ops);

	mutex_init(&sdev->mutex);
	init_completion(&sdev->work);

	if (client->irq) {
		rval = request_irq(client->irq,
			si4713_handler, IRQF_TRIGGER_FALLING | IRQF_DISABLED,
			client->name, sdev);
		if (rval < 0) {
			v4l2_err(&sdev->sd, "Could not request IRQ\n");
			goto free_sdev;
		}
		v4l2_dbg(1, debug, &sdev->sd, "IRQ requested.\n");
	} else {
		v4l2_warn(&sdev->sd, "IRQ not configured. Using timeouts.\n");
	}

	rval = si4713_initialize(sdev);
	if (rval < 0) {
		v4l2_err(&sdev->sd, "Failed to probe device information.\n");
		goto free_irq;
	}

	return 0;

free_irq:
	if (client->irq)
		free_irq(client->irq, sdev);
free_sdev:
	kfree(sdev);
exit:
	return rval;
}

/* si4713_remove - remove the device */
static int si4713_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct si4713_device *sdev = to_si4713_device(sd);

	if (sdev->power_state)
		si4713_set_power_state(sdev, POWER_DOWN);

	if (client->irq > 0)
		free_irq(client->irq, sdev);

	v4l2_device_unregister_subdev(sd);

	kfree(sdev);

	return 0;
}

/* si4713_i2c_driver - i2c driver interface */
static const struct i2c_device_id si4713_id[] = {
	{ "si4713" , 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, si4713_id);

static struct i2c_driver si4713_i2c_driver = {
	.driver		= {
		.name	= "si4713",
	},
	.probe		= si4713_probe,
	.remove         = si4713_remove,
	.id_table       = si4713_id,
};

/* Module Interface */
static int __init si4713_module_init(void)
{
	return i2c_add_driver(&si4713_i2c_driver);
}

static void __exit si4713_module_exit(void)
{
	i2c_del_driver(&si4713_i2c_driver);
}

module_init(si4713_module_init);
module_exit(si4713_module_exit);

