// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for the OLPC XO-1.75 Embedded Controller.
 *
 * The EC protocol is documented at:
 * http://wiki.laptop.org/go/XO_1.75_HOST_to_EC_Protocol
 *
 * Copyright (C) 2010 One Laptop per Child Foundation.
 * Copyright (C) 2018 Lubomir Rintel <lkundrak@v3.sk>
 */

#include <linux/completion.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/olpc-ec.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/spi/spi.h>

struct ec_cmd_t {
	u8 cmd;
	u8 bytes_returned;
};

enum ec_chan_t {
	CHAN_NONE = 0,
	CHAN_SWITCH,
	CHAN_CMD_RESP,
	CHAN_KEYBOARD,
	CHAN_TOUCHPAD,
	CHAN_EVENT,
	CHAN_DEBUG,
	CHAN_CMD_ERROR,
};

/*
 * EC events
 */
#define EVENT_AC_CHANGE			1  /* AC plugged/unplugged */
#define EVENT_BATTERY_STATUS		2  /* Battery low/full/error/gone */
#define EVENT_BATTERY_CRITICAL		3  /* Battery critical voltage */
#define EVENT_BATTERY_SOC_CHANGE	4  /* 1% SOC Change */
#define EVENT_BATTERY_ERROR		5  /* Abnormal error, query for cause */
#define EVENT_POWER_PRESSED		6  /* Power button was pressed */
#define EVENT_POWER_PRESS_WAKE		7  /* Woken up with a power button */
#define EVENT_TIMED_HOST_WAKE		8  /* Host wake timer */
#define EVENT_OLS_HIGH_LIMIT		9  /* OLS crossed dark threshold */
#define EVENT_OLS_LOW_LIMIT		10 /* OLS crossed light threshold */

/*
 * EC commands
 * (from http://dev.laptop.org/git/users/rsmith/ec-1.75/tree/ec_cmd.h)
 */
#define CMD_GET_API_VERSION		0x08 /* out: u8 */
#define CMD_READ_VOLTAGE		0x10 /* out: u16, *9.76/32, mV */
#define CMD_READ_CURRENT		0x11 /* out: s16, *15.625/120, mA */
#define CMD_READ_ACR			0x12 /* out: s16, *6250/15, uAh */
#define CMD_READ_BATT_TEMPERATURE	0x13 /* out: u16, *100/256, deg C */
#define CMD_READ_AMBIENT_TEMPERATURE	0x14 /* unimplemented, no hardware */
#define CMD_READ_BATTERY_STATUS		0x15 /* out: u8, bitmask */
#define CMD_READ_SOC			0x16 /* out: u8, percentage */
#define CMD_READ_GAUGE_ID		0x17 /* out: u8 * 8 */
#define CMD_READ_GAUGE_DATA		0x18 /* in: u8 addr, out: u8 data */
#define CMD_READ_BOARD_ID		0x19 /* out: u16 (platform id) */
#define CMD_READ_BATT_ERR_CODE		0x1f /* out: u8, error bitmask */
#define CMD_SET_DCON_POWER		0x26 /* in: u8 */
#define CMD_RESET_EC			0x28 /* none */
#define CMD_READ_BATTERY_TYPE		0x2c /* out: u8 */
#define CMD_SET_AUTOWAK			0x33 /* out: u8 */
#define CMD_SET_EC_WAKEUP_TIMER		0x36 /* in: u32, out: ? */
#define CMD_READ_EXT_SCI_MASK		0x37 /* ? */
#define CMD_WRITE_EXT_SCI_MASK		0x38 /* ? */
#define CMD_CLEAR_EC_WAKEUP_TIMER	0x39 /* none */
#define CMD_ENABLE_RUNIN_DISCHARGE	0x3B /* none */
#define CMD_DISABLE_RUNIN_DISCHARGE	0x3C /* none */
#define CMD_READ_MPPT_ACTIVE		0x3d /* out: u8 */
#define CMD_READ_MPPT_LIMIT		0x3e /* out: u8 */
#define CMD_SET_MPPT_LIMIT		0x3f /* in: u8 */
#define CMD_DISABLE_MPPT		0x40 /* none */
#define CMD_ENABLE_MPPT			0x41 /* none */
#define CMD_READ_VIN			0x42 /* out: u16 */
#define CMD_EXT_SCI_QUERY		0x43 /* ? */
#define RSP_KEYBOARD_DATA		0x48 /* ? */
#define RSP_TOUCHPAD_DATA		0x49 /* ? */
#define CMD_GET_FW_VERSION		0x4a /* out: u8 * 16 */
#define CMD_POWER_CYCLE			0x4b /* none */
#define CMD_POWER_OFF			0x4c /* none */
#define CMD_RESET_EC_SOFT		0x4d /* none */
#define CMD_READ_GAUGE_U16		0x4e /* ? */
#define CMD_ENABLE_MOUSE		0x4f /* ? */
#define CMD_ECHO			0x52 /* in: u8 * 5, out: u8 * 5 */
#define CMD_GET_FW_DATE			0x53 /* out: u8 * 16 */
#define CMD_GET_FW_USER			0x54 /* out: u8 * 16 */
#define CMD_TURN_OFF_POWER		0x55 /* none (same as 0x4c) */
#define CMD_READ_OLS			0x56 /* out: u16 */
#define CMD_OLS_SMT_LEDON		0x57 /* none */
#define CMD_OLS_SMT_LEDOFF		0x58 /* none */
#define CMD_START_OLS_ASSY		0x59 /* none */
#define CMD_STOP_OLS_ASSY		0x5a /* none */
#define CMD_OLS_SMTTEST_STOP		0x5b /* none */
#define CMD_READ_VIN_SCALED		0x5c /* out: u16 */
#define CMD_READ_BAT_MIN_W		0x5d /* out: u16 */
#define CMD_READ_BAR_MAX_W		0x5e /* out: u16 */
#define CMD_RESET_BAT_MINMAX_W		0x5f /* none */
#define CMD_READ_LOCATION		0x60 /* in: u16 addr, out: u8 data */
#define CMD_WRITE_LOCATION		0x61 /* in: u16 addr, u8 data */
#define CMD_KEYBOARD_CMD		0x62 /* in: u8, out: ? */
#define CMD_TOUCHPAD_CMD		0x63 /* in: u8, out: ? */
#define CMD_GET_FW_HASH			0x64 /* out: u8 * 16 */
#define CMD_SUSPEND_HINT		0x65 /* in: u8 */
#define CMD_ENABLE_WAKE_TIMER		0x66 /* in: u8 */
#define CMD_SET_WAKE_TIMER		0x67 /* in: 32 */
#define CMD_ENABLE_WAKE_AUTORESET	0x68 /* in: u8 */
#define CMD_OLS_SET_LIMITS		0x69 /* in: u16, u16 */
#define CMD_OLS_GET_LIMITS		0x6a /* out: u16, u16 */
#define CMD_OLS_SET_CEILING		0x6b /* in: u16 */
#define CMD_OLS_GET_CEILING		0x6c /* out: u16 */

/*
 * Accepted EC commands, and how many bytes they return. There are plenty
 * of EC commands that are no longer implemented, or are implemented only on
 * certain older boards.
 */
static const struct ec_cmd_t olpc_xo175_ec_cmds[] = {
	{ CMD_GET_API_VERSION, 1 },
	{ CMD_READ_VOLTAGE, 2 },
	{ CMD_READ_CURRENT, 2 },
	{ CMD_READ_ACR, 2 },
	{ CMD_READ_BATT_TEMPERATURE, 2 },
	{ CMD_READ_BATTERY_STATUS, 1 },
	{ CMD_READ_SOC, 1 },
	{ CMD_READ_GAUGE_ID, 8 },
	{ CMD_READ_GAUGE_DATA, 1 },
	{ CMD_READ_BOARD_ID, 2 },
	{ CMD_READ_BATT_ERR_CODE, 1 },
	{ CMD_SET_DCON_POWER, 0 },
	{ CMD_RESET_EC, 0 },
	{ CMD_READ_BATTERY_TYPE, 1 },
	{ CMD_ENABLE_RUNIN_DISCHARGE, 0 },
	{ CMD_DISABLE_RUNIN_DISCHARGE, 0 },
	{ CMD_READ_MPPT_ACTIVE, 1 },
	{ CMD_READ_MPPT_LIMIT, 1 },
	{ CMD_SET_MPPT_LIMIT, 0 },
	{ CMD_DISABLE_MPPT, 0 },
	{ CMD_ENABLE_MPPT, 0 },
	{ CMD_READ_VIN, 2 },
	{ CMD_GET_FW_VERSION, 16 },
	{ CMD_POWER_CYCLE, 0 },
	{ CMD_POWER_OFF, 0 },
	{ CMD_RESET_EC_SOFT, 0 },
	{ CMD_ECHO, 5 },
	{ CMD_GET_FW_DATE, 16 },
	{ CMD_GET_FW_USER, 16 },
	{ CMD_TURN_OFF_POWER, 0 },
	{ CMD_READ_OLS, 2 },
	{ CMD_OLS_SMT_LEDON, 0 },
	{ CMD_OLS_SMT_LEDOFF, 0 },
	{ CMD_START_OLS_ASSY, 0 },
	{ CMD_STOP_OLS_ASSY, 0 },
	{ CMD_OLS_SMTTEST_STOP, 0 },
	{ CMD_READ_VIN_SCALED, 2 },
	{ CMD_READ_BAT_MIN_W, 2 },
	{ CMD_READ_BAR_MAX_W, 2 },
	{ CMD_RESET_BAT_MINMAX_W, 0 },
	{ CMD_READ_LOCATION, 1 },
	{ CMD_WRITE_LOCATION, 0 },
	{ CMD_GET_FW_HASH, 16 },
	{ CMD_SUSPEND_HINT, 0 },
	{ CMD_ENABLE_WAKE_TIMER, 0 },
	{ CMD_SET_WAKE_TIMER, 0 },
	{ CMD_ENABLE_WAKE_AUTORESET, 0 },
	{ CMD_OLS_SET_LIMITS, 0 },
	{ CMD_OLS_GET_LIMITS, 4 },
	{ CMD_OLS_SET_CEILING, 0 },
	{ CMD_OLS_GET_CEILING, 2 },
	{ CMD_READ_EXT_SCI_MASK, 2 },
	{ CMD_WRITE_EXT_SCI_MASK, 0 },

	{ }
};

#define EC_MAX_CMD_DATA_LEN	5
#define EC_MAX_RESP_LEN		16

#define LOG_BUF_SIZE		128

#define PM_WAKEUP_TIME		1000

#define EC_ALL_EVENTS		GENMASK(15, 0)

enum ec_state_t {
	CMD_STATE_IDLE = 0,
	CMD_STATE_WAITING_FOR_SWITCH,
	CMD_STATE_CMD_IN_TX_FIFO,
	CMD_STATE_CMD_SENT,
	CMD_STATE_RESP_RECEIVED,
	CMD_STATE_ERROR_RECEIVED,
};

struct olpc_xo175_ec_cmd {
	u8 command;
	u8 nr_args;
	u8 data_len;
	u8 args[EC_MAX_CMD_DATA_LEN];
};

struct olpc_xo175_ec_resp {
	u8 channel;
	u8 byte;
};

struct olpc_xo175_ec {
	bool suspended;

	/* SPI related stuff. */
	struct spi_device *spi;
	struct spi_transfer xfer;
	struct spi_message msg;
	union {
		struct olpc_xo175_ec_cmd cmd;
		struct olpc_xo175_ec_resp resp;
	} tx_buf, rx_buf;

	/* GPIO for the CMD signals. */
	struct gpio_desc *gpio_cmd;

	/* Command handling related state. */
	spinlock_t cmd_state_lock;
	int cmd_state;
	bool cmd_running;
	struct completion cmd_done;
	struct olpc_xo175_ec_cmd cmd;
	u8 resp_data[EC_MAX_RESP_LEN];
	int expected_resp_len;
	int resp_len;

	/* Power button. */
	struct input_dev *pwrbtn;

	/* Debug handling. */
	char logbuf[LOG_BUF_SIZE];
	int logbuf_len;
};

static struct platform_device *olpc_ec;

static int olpc_xo175_ec_resp_len(u8 cmd)
{
	const struct ec_cmd_t *p;

	for (p = olpc_xo175_ec_cmds; p->cmd; p++) {
		if (p->cmd == cmd)
			return p->bytes_returned;
	}

	return -EINVAL;
}

static void olpc_xo175_ec_flush_logbuf(struct olpc_xo175_ec *priv)
{
	dev_dbg(&priv->spi->dev, "got debug string [%*pE]\n",
				priv->logbuf_len, priv->logbuf);
	priv->logbuf_len = 0;
}

static void olpc_xo175_ec_complete(void *arg);

static void olpc_xo175_ec_send_command(struct olpc_xo175_ec *priv, void *cmd,
								size_t cmdlen)
{
	int ret;

	memcpy(&priv->tx_buf, cmd, cmdlen);
	priv->xfer.len = cmdlen;

	spi_message_init_with_transfers(&priv->msg, &priv->xfer, 1);

	priv->msg.complete = olpc_xo175_ec_complete;
	priv->msg.context = priv;

	ret = spi_async(priv->spi, &priv->msg);
	if (ret)
		dev_err(&priv->spi->dev, "spi_async() failed %d\n", ret);
}

static void olpc_xo175_ec_read_packet(struct olpc_xo175_ec *priv)
{
	u8 nonce[] = {0xA5, 0x5A};

	olpc_xo175_ec_send_command(priv, nonce, sizeof(nonce));
}

static void olpc_xo175_ec_complete(void *arg)
{
	struct olpc_xo175_ec *priv = arg;
	struct device *dev = &priv->spi->dev;
	struct power_supply *psy;
	unsigned long flags;
	u8 channel;
	u8 byte;
	int ret;

	ret = priv->msg.status;
	if (ret) {
		dev_err(dev, "SPI transfer failed: %d\n", ret);

		spin_lock_irqsave(&priv->cmd_state_lock, flags);
		if (priv->cmd_running) {
			priv->resp_len = 0;
			priv->cmd_state = CMD_STATE_ERROR_RECEIVED;
			complete(&priv->cmd_done);
		}
		spin_unlock_irqrestore(&priv->cmd_state_lock, flags);

		if (ret != -EINTR)
			olpc_xo175_ec_read_packet(priv);

		return;
	}

	channel = priv->rx_buf.resp.channel;
	byte = priv->rx_buf.resp.byte;

	switch (channel) {
	case CHAN_NONE:
		spin_lock_irqsave(&priv->cmd_state_lock, flags);

		if (!priv->cmd_running) {
			/* We can safely ignore these */
			dev_err(dev, "spurious FIFO read packet\n");
			spin_unlock_irqrestore(&priv->cmd_state_lock, flags);
			return;
		}

		priv->cmd_state = CMD_STATE_CMD_SENT;
		if (!priv->expected_resp_len)
			complete(&priv->cmd_done);
		olpc_xo175_ec_read_packet(priv);

		spin_unlock_irqrestore(&priv->cmd_state_lock, flags);
		return;

	case CHAN_SWITCH:
		spin_lock_irqsave(&priv->cmd_state_lock, flags);

		if (!priv->cmd_running) {
			/* Just go with the flow */
			dev_err(dev, "spurious SWITCH packet\n");
			memset(&priv->cmd, 0, sizeof(priv->cmd));
			priv->cmd.command = CMD_ECHO;
		}

		priv->cmd_state = CMD_STATE_CMD_IN_TX_FIFO;

		/* Throw command into TxFIFO */
		gpiod_set_value_cansleep(priv->gpio_cmd, 0);
		olpc_xo175_ec_send_command(priv, &priv->cmd, sizeof(priv->cmd));

		spin_unlock_irqrestore(&priv->cmd_state_lock, flags);
		return;

	case CHAN_CMD_RESP:
		spin_lock_irqsave(&priv->cmd_state_lock, flags);

		if (!priv->cmd_running) {
			dev_err(dev, "spurious response packet\n");
		} else if (priv->resp_len >= priv->expected_resp_len) {
			dev_err(dev, "too many response packets\n");
		} else {
			priv->resp_data[priv->resp_len++] = byte;
			if (priv->resp_len == priv->expected_resp_len) {
				priv->cmd_state = CMD_STATE_RESP_RECEIVED;
				complete(&priv->cmd_done);
			}
		}

		spin_unlock_irqrestore(&priv->cmd_state_lock, flags);
		break;

	case CHAN_CMD_ERROR:
		spin_lock_irqsave(&priv->cmd_state_lock, flags);

		if (!priv->cmd_running) {
			dev_err(dev, "spurious cmd error packet\n");
		} else {
			priv->resp_data[0] = byte;
			priv->resp_len = 1;
			priv->cmd_state = CMD_STATE_ERROR_RECEIVED;
			complete(&priv->cmd_done);
		}
		spin_unlock_irqrestore(&priv->cmd_state_lock, flags);
		break;

	case CHAN_KEYBOARD:
		dev_warn(dev, "keyboard is not supported\n");
		break;

	case CHAN_TOUCHPAD:
		dev_warn(dev, "touchpad is not supported\n");
		break;

	case CHAN_EVENT:
		dev_dbg(dev, "got event %.2x\n", byte);
		switch (byte) {
		case EVENT_AC_CHANGE:
			psy = power_supply_get_by_name("olpc_ac");
			if (psy) {
				power_supply_changed(psy);
				power_supply_put(psy);
			}
			break;
		case EVENT_BATTERY_STATUS:
		case EVENT_BATTERY_CRITICAL:
		case EVENT_BATTERY_SOC_CHANGE:
		case EVENT_BATTERY_ERROR:
			psy = power_supply_get_by_name("olpc_battery");
			if (psy) {
				power_supply_changed(psy);
				power_supply_put(psy);
			}
			break;
		case EVENT_POWER_PRESSED:
			input_report_key(priv->pwrbtn, KEY_POWER, 1);
			input_sync(priv->pwrbtn);
			input_report_key(priv->pwrbtn, KEY_POWER, 0);
			input_sync(priv->pwrbtn);
			fallthrough;
		case EVENT_POWER_PRESS_WAKE:
		case EVENT_TIMED_HOST_WAKE:
			pm_wakeup_event(priv->pwrbtn->dev.parent,
						PM_WAKEUP_TIME);
			break;
		default:
			dev_dbg(dev, "ignored unknown event %.2x\n", byte);
			break;
		}
		break;

	case CHAN_DEBUG:
		if (byte == '\n') {
			olpc_xo175_ec_flush_logbuf(priv);
		} else if (isprint(byte)) {
			priv->logbuf[priv->logbuf_len++] = byte;
			if (priv->logbuf_len == LOG_BUF_SIZE)
				olpc_xo175_ec_flush_logbuf(priv);
		}
		break;

	default:
		dev_warn(dev, "unknown channel: %d, %.2x\n", channel, byte);
		break;
	}

	/* Most non-command packets get the TxFIFO refilled and an ACK. */
	olpc_xo175_ec_read_packet(priv);
}

/*
 * This function is protected with a mutex. We can safely assume that
 * there will be only one instance of this function running at a time.
 * One of the ways in which we enforce this is by waiting until we get
 * all response bytes back from the EC, rather than just the number that
 * the caller requests (otherwise, we might start a new command while an
 * old command's response bytes are still incoming).
 */
static int olpc_xo175_ec_cmd(u8 cmd, u8 *inbuf, size_t inlen, u8 *resp,
					size_t resp_len, void *ec_cb_arg)
{
	struct olpc_xo175_ec *priv = ec_cb_arg;
	struct device *dev = &priv->spi->dev;
	unsigned long flags;
	size_t nr_bytes;
	int ret = 0;

	dev_dbg(dev, "CMD %x, %zd bytes expected\n", cmd, resp_len);

	if (inlen > 5) {
		dev_err(dev, "command len %zd too big!\n", resp_len);
		return -EOVERFLOW;
	}

	/* Suspending in the middle of an EC command hoses things badly! */
	if (WARN_ON(priv->suspended))
		return -EBUSY;

	/* Ensure a valid command and return bytes */
	ret = olpc_xo175_ec_resp_len(cmd);
	if (ret < 0) {
		dev_err_ratelimited(dev, "unknown command 0x%x\n", cmd);

		/*
		 * Assume the best in our callers, and allow unknown commands
		 * through. I'm not the charitable type, but it was beaten
		 * into me. Just maintain a minimum standard of sanity.
		 */
		if (resp_len > sizeof(priv->resp_data)) {
			dev_err(dev, "response too big: %zd!\n", resp_len);
			return -EOVERFLOW;
		}
		nr_bytes = resp_len;
	} else {
		nr_bytes = (size_t)ret;
		ret = 0;
	}
	resp_len = min(resp_len, nr_bytes);

	spin_lock_irqsave(&priv->cmd_state_lock, flags);

	/* Initialize the state machine */
	init_completion(&priv->cmd_done);
	priv->cmd_running = true;
	priv->cmd_state = CMD_STATE_WAITING_FOR_SWITCH;
	memset(&priv->cmd, 0, sizeof(priv->cmd));
	priv->cmd.command = cmd;
	priv->cmd.nr_args = inlen;
	priv->cmd.data_len = 0;
	memcpy(priv->cmd.args, inbuf, inlen);
	priv->expected_resp_len = nr_bytes;
	priv->resp_len = 0;

	/* Tickle the cmd gpio to get things started */
	gpiod_set_value_cansleep(priv->gpio_cmd, 1);

	spin_unlock_irqrestore(&priv->cmd_state_lock, flags);

	/* The irq handler should do the rest */
	if (!wait_for_completion_timeout(&priv->cmd_done,
			msecs_to_jiffies(4000))) {
		dev_err(dev, "EC cmd error: timeout in STATE %d\n",
				priv->cmd_state);
		gpiod_set_value_cansleep(priv->gpio_cmd, 0);
		spi_slave_abort(priv->spi);
		olpc_xo175_ec_read_packet(priv);
		return -ETIMEDOUT;
	}

	spin_lock_irqsave(&priv->cmd_state_lock, flags);

	/* Deal with the results. */
	if (priv->cmd_state == CMD_STATE_ERROR_RECEIVED) {
		/* EC-provided error is in the single response byte */
		dev_err(dev, "command 0x%x returned error 0x%x\n",
						cmd, priv->resp_data[0]);
		ret = -EREMOTEIO;
	} else if (priv->resp_len != nr_bytes) {
		dev_err(dev, "command 0x%x returned %d bytes, expected %zd bytes\n",
						cmd, priv->resp_len, nr_bytes);
		ret = -EREMOTEIO;
	} else {
		/*
		 * We may have 8 bytes in priv->resp, but we only care about
		 * what we've been asked for. If the caller asked for only 2
		 * bytes, give them that. We've guaranteed that
		 * resp_len <= priv->resp_len and priv->resp_len == nr_bytes.
		 */
		memcpy(resp, priv->resp_data, resp_len);
	}

	/* This should already be low, but just in case. */
	gpiod_set_value_cansleep(priv->gpio_cmd, 0);
	priv->cmd_running = false;

	spin_unlock_irqrestore(&priv->cmd_state_lock, flags);

	return ret;
}

static int olpc_xo175_ec_set_event_mask(unsigned int mask)
{
	u8 args[2];

	args[0] = mask >> 0;
	args[1] = mask >> 8;
	return olpc_ec_cmd(CMD_WRITE_EXT_SCI_MASK, args, 2, NULL, 0);
}

static void olpc_xo175_ec_power_off(void)
{
	while (1) {
		olpc_ec_cmd(CMD_POWER_OFF, NULL, 0, NULL, 0);
		mdelay(1000);
	}
}

static int __maybe_unused olpc_xo175_ec_suspend(struct device *dev)
{
	struct olpc_xo175_ec *priv = dev_get_drvdata(dev);
	static struct {
		u8 suspend;
		u32 suspend_count;
	} __packed hintargs;
	static unsigned int suspend_count;

	/*
	 * SOC_SLEEP is not wired to the EC on B3 and earlier boards.
	 * This command lets the EC know instead. The suspend count doesn't seem
	 * to be used anywhere but in the EC debug output.
	 */
	hintargs.suspend = 1;
	hintargs.suspend_count = suspend_count++;
	olpc_ec_cmd(CMD_SUSPEND_HINT, (void *)&hintargs, sizeof(hintargs),
								NULL, 0);

	/*
	 * After we've sent the suspend hint, don't allow further EC commands
	 * to be run until we've resumed. Userspace tasks should be frozen,
	 * but kernel threads and interrupts could still schedule EC commands.
	 */
	priv->suspended = true;

	return 0;
}

static int __maybe_unused olpc_xo175_ec_resume_noirq(struct device *dev)
{
	struct olpc_xo175_ec *priv = dev_get_drvdata(dev);

	priv->suspended = false;

	return 0;
}

static int __maybe_unused olpc_xo175_ec_resume(struct device *dev)
{
	u8 x = 0;

	/*
	 * The resume hint is only needed if no other commands are
	 * being sent during resume. all it does is tell the EC
	 * the SoC is definitely awake.
	 */
	olpc_ec_cmd(CMD_SUSPEND_HINT, &x, 1, NULL, 0);

	/* Enable all EC events while we're awake */
	olpc_xo175_ec_set_event_mask(EC_ALL_EVENTS);

	return 0;
}

static struct olpc_ec_driver olpc_xo175_ec_driver = {
	.ec_cmd = olpc_xo175_ec_cmd,
};

static int olpc_xo175_ec_remove(struct spi_device *spi)
{
	if (pm_power_off == olpc_xo175_ec_power_off)
		pm_power_off = NULL;

	spi_slave_abort(spi);

	platform_device_unregister(olpc_ec);
	olpc_ec = NULL;

	return 0;
}

static int olpc_xo175_ec_probe(struct spi_device *spi)
{
	struct olpc_xo175_ec *priv;
	int ret;

	if (olpc_ec) {
		dev_err(&spi->dev, "OLPC EC already registered.\n");
		return -EBUSY;
	}

	priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->gpio_cmd = devm_gpiod_get(&spi->dev, "cmd", GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpio_cmd)) {
		dev_err(&spi->dev, "failed to get cmd gpio: %ld\n",
					PTR_ERR(priv->gpio_cmd));
		return PTR_ERR(priv->gpio_cmd);
	}

	priv->spi = spi;

	spin_lock_init(&priv->cmd_state_lock);
	priv->cmd_state = CMD_STATE_IDLE;
	init_completion(&priv->cmd_done);

	priv->logbuf_len = 0;

	/* Set up power button input device */
	priv->pwrbtn = devm_input_allocate_device(&spi->dev);
	if (!priv->pwrbtn)
		return -ENOMEM;
	priv->pwrbtn->name = "Power Button";
	priv->pwrbtn->dev.parent = &spi->dev;
	input_set_capability(priv->pwrbtn, EV_KEY, KEY_POWER);
	ret = input_register_device(priv->pwrbtn);
	if (ret) {
		dev_err(&spi->dev, "error registering input device: %d\n", ret);
		return ret;
	}

	spi_set_drvdata(spi, priv);

	priv->xfer.rx_buf = &priv->rx_buf;
	priv->xfer.tx_buf = &priv->tx_buf;

	olpc_xo175_ec_read_packet(priv);

	olpc_ec_driver_register(&olpc_xo175_ec_driver, priv);
	olpc_ec = platform_device_register_resndata(&spi->dev, "olpc-ec", -1,
							NULL, 0, NULL, 0);

	/* Enable all EC events while we're awake */
	olpc_xo175_ec_set_event_mask(EC_ALL_EVENTS);

	if (pm_power_off == NULL)
		pm_power_off = olpc_xo175_ec_power_off;

	dev_info(&spi->dev, "OLPC XO-1.75 Embedded Controller driver\n");

	return 0;
}

static const struct dev_pm_ops olpc_xo175_ec_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(NULL, olpc_xo175_ec_resume_noirq)
	SET_RUNTIME_PM_OPS(olpc_xo175_ec_suspend, olpc_xo175_ec_resume, NULL)
};

static const struct of_device_id olpc_xo175_ec_of_match[] = {
	{ .compatible = "olpc,xo1.75-ec" },
	{ }
};
MODULE_DEVICE_TABLE(of, olpc_xo175_ec_of_match);

static const struct spi_device_id olpc_xo175_ec_id_table[] = {
	{ "xo1.75-ec", 0 },
	{}
};
MODULE_DEVICE_TABLE(spi, olpc_xo175_ec_id_table);

static struct spi_driver olpc_xo175_ec_spi_driver = {
	.driver = {
		.name	= "olpc-xo175-ec",
		.of_match_table = olpc_xo175_ec_of_match,
		.pm = &olpc_xo175_ec_pm_ops,
	},
	.probe		= olpc_xo175_ec_probe,
	.remove		= olpc_xo175_ec_remove,
};
module_spi_driver(olpc_xo175_ec_spi_driver);

MODULE_DESCRIPTION("OLPC XO-1.75 Embedded Controller driver");
MODULE_AUTHOR("Lennert Buytenhek <buytenh@wantstofly.org>"); /* Functionality */
MODULE_AUTHOR("Lubomir Rintel <lkundrak@v3.sk>"); /* Bugs */
MODULE_LICENSE("GPL");
