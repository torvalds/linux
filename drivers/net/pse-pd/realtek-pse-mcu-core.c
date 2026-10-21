// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the microcontroller (MCU) fronting PSE silicon on various
 * Realtek-based managed switches. The MCU speaks a 12-byte fixed-frame
 * management protocol; this driver covers two generations of the
 * protocol via a per-dialect opcode table and response parsers.
 *
 * Many PoE switch designs put a dedicated microcontroller in front of the
 * actual PSE silicon: the host CPU talks to the MCU over I2C/SMBus or
 * UART, and the MCU in turn manages the PSE chips on the board. The MCU
 * speaks a small message-based protocol. The PSE chips themselves are not
 * accessed directly; everything goes through MCU commands.
 *
 * This driver targets that architecture for the Realtek-family protocol.
 * Two generations are supported: Gen1 being used on older switches where
 * the MCU fronts and manages Broadcom PSE silicon; Gen2 being used with
 * Realtek PSE silicon. The two share frame format and a sum-mod-256
 * checksum but diverge on opcode numbers and on a few response layouts;
 * this is handled by the per-dialect opcode table and parser hooks.
 *
 * Out of scope: PSE chips that are interfaced directly from the host
 * without a management MCU, MCU designs that speak an unrelated protocol
 * family, and "dumb PSE" modes where no host control is wired up at all.
 *
 * This core module implements the protocol, decoding/encoding of MCU
 * responses, and the pse_controller_ops integration. Transport modules
 * (realtek-pse-mcu-i2c, realtek-pse-mcu-uart) provide the send/recv
 * callbacks.
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/jiffies.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/pse-pd/pse.h>
#include <linux/unaligned.h>

#include "realtek-pse-mcu.h"

#define RTPSE_MCU_DEVICE_ID_RTL8238B		0x0138
#define RTPSE_MCU_DEVICE_ID_RTL8239		0x0039
#define RTPSE_MCU_DEVICE_ID_RTL8239C		0x0139
#define RTPSE_MCU_DEVICE_ID_BCM59111		0xe111
#define RTPSE_MCU_DEVICE_ID_BCM59121		0xe121

#define RTPSE_MCU_PORT_STS_DISABLED		0x00
#define RTPSE_MCU_PORT_STS_SEARCHING		0x01
#define RTPSE_MCU_PORT_STS_DELIVERING		0x02
#define RTPSE_MCU_PORT_STS_TEST			0x03	/* Gen1-only; reserved on Gen2 */
#define RTPSE_MCU_PORT_STS_FAULT		0x04
#define RTPSE_MCU_PORT_STS_OTHER_FAULT		0x05	/* Gen1-only; reserved on Gen2 */
#define RTPSE_MCU_PORT_STS_REQUESTING		0x06

/* RTPSE_MCU_PORT_SET_POWER_LIMIT_TYPE values */
#define RTPSE_MCU_PORT_PW_LIMIT_TYPE_USER	0x02

#define RTPSE_MCU_MAX_PORTS			48
#define RTPSE_MCU_PORT_MAX_PRIORITY		3

/* Bounded resends when the MCU replies NOT_READY (busy). */
#define RTPSE_MCU_NOT_READY_RETRIES		3

/* Nominal PSE rail; 802.3at/bt operating range. */
#define RTPSE_MCU_PSE_VOLTAGE_UV		54000000

enum rtpse_mcu_cmd {
	RTPSE_MCU_CMD_SET_GLOBAL_STATE,
	RTPSE_MCU_CMD_GET_SYSTEM_INFO,
	RTPSE_MCU_CMD_GET_EXT_CONFIG,

	RTPSE_MCU_CMD_PORT_ENABLE,
	RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT_TYPE,
	RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT,
	RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT_EXT,
	RTPSE_MCU_CMD_PORT_SET_PRIORITY,
	RTPSE_MCU_CMD_PORT_GET_STATUS,
	RTPSE_MCU_CMD_PORT_GET_POWER_STATS,
	RTPSE_MCU_CMD_PORT_GET_CONFIG,
	RTPSE_MCU_CMD_PORT_GET_EXT_CONFIG,

	RTPSE_MCU_NUM_CMDS,
};

struct rtpse_mcu_opcode {
	u8 op;
	bool valid;
};

/* Shorthand for the designated-initializer entries in dialect opcode tables. */
#define RTPSE_MCU_OP(opc)	{ .op = (opc), .valid = true }

/* Parsed MCU response structures (decoded from rtpse_mcu_msg replies) */

struct rtpse_mcu_info {
	u8 max_ports;
	bool system_enable;
	u16 device_id;
	u8 mcu_type;
};

struct rtpse_mcu_ext_config {
	u8 num_of_pses;
};

struct rtpse_mcu_port_status {
	u8 sts1;
	u8 sts2;
	u8 sts3;
};

struct rtpse_mcu_port_measurement {
	u16 voltage_raw;	/* 64.45mV/LSB */
	u16 current_raw;	/* 1mA/LSB */
	u16 temperature_raw;	/* T(mC) = 1250 * (220 - raw) */
	u16 power_raw;		/* 100mW/LSB */
};

struct rtpse_mcu_port_config {
	bool enable;
};

struct rtpse_mcu_port_ext_config {
	u8 max_power;
	u8 priority;
};

struct rtpse_mcu_dialect {
	struct rtpse_mcu_opcode opcode[RTPSE_MCU_NUM_CMDS];

	/*
	 * Response parsers for the fields that differ between dialects; each
	 * dialect supplies its own. Other responses share one layout and are
	 * decoded directly - a dialect that diverges there must add a hook,
	 * as a mismatched layout cannot be detected (the checksum still passes).
	 */
	void (*parse_system_info)(const u8 *payload, struct rtpse_mcu_info *info);
	int (*parse_port_class)(const struct rtpse_mcu_port_status *status);
	const char *(*mcu_type_str)(unsigned int mcu_type);
};

struct rtpse_mcu_chip_info {
	const char *name;
	u32 max_mW_per_port;
	enum rtpse_mcu_cmd pw_set_cmd;	/* command used by set_pw_limit */
	u32 pw_set_lsb_mW;		/* LSB of pw_set_cmd value, in mW */
	u32 pw_read_lsb_mW;		/* LSB of ext_config.max_power read-back, in mW */
};

static const struct rtpse_mcu_chip_info rtl8238b_info = {
	.max_mW_per_port = 30000,
	.name = "RTL8238B",
	.pw_read_lsb_mW = 200,
	.pw_set_cmd = RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT,
	.pw_set_lsb_mW = 200,
};

static const struct rtpse_mcu_chip_info rtl8239_info = {
	.max_mW_per_port = 90000,
	.name = "RTL8239",
	.pw_read_lsb_mW = 400,
	.pw_set_cmd = RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT_EXT,
	.pw_set_lsb_mW = 400,
};

static const struct rtpse_mcu_chip_info rtl8239c_info = {
	.max_mW_per_port = 90000,
	.name = "RTL8239C",
	.pw_read_lsb_mW = 400,
	.pw_set_cmd = RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT_EXT,
	.pw_set_lsb_mW = 400,
};

static const struct rtpse_mcu_chip_info bcm59111_info = {
	.max_mW_per_port = 30000,
	.name = "BCM59111",
	.pw_read_lsb_mW = 200,
	.pw_set_cmd = RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT,
	.pw_set_lsb_mW = 200,
};

static const struct rtpse_mcu_chip_info bcm59121_info = {
	/*
	 * BCM59121 is a 60W Type-3 part, but known boards run it at 802.3at
	 * and the Gen1 dialect has only the 8-bit/0.2W set command (<=51W);
	 * cap at the 30W the hardware actually offers.
	 */
	.max_mW_per_port = 30000,
	.name = "BCM59121",
	.pw_read_lsb_mW = 200,
	.pw_set_cmd = RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT,
	.pw_set_lsb_mW = 200,
};

/* Helpers and basic functions */

static struct rtpse_mcu_ctrl *to_rtpse_mcu_ctrl(struct pse_controller_dev *pcdev)
{
	return container_of(pcdev, struct rtpse_mcu_ctrl, pcdev);
}

static void rtpse_mcu_msg_init(struct rtpse_mcu_msg *msg, u8 opcode)
{
	memset(msg, 0xff, sizeof(*msg));
	msg->opcode = opcode;
}

static u8 rtpse_mcu_checksum(const u8 *buf, size_t len)
{
	u8 sum = 0;

	while (len--)
		sum += *buf++;
	return sum;
}

static int rtpse_mcu_do_xfer(struct rtpse_mcu_ctrl *pse, struct rtpse_mcu_msg *req,
			     struct rtpse_mcu_msg *resp)
{
	unsigned int tries;
	int ret;

	for (tries = 0; ; tries++) {
		scoped_guard(mutex, &pse->mutex) {
			/* Rolling seq_num (skip 0) so a stale/all-zero reply can't match. */
			if (++pse->seq == 0)
				pse->seq = 1;
			req->seq_num = pse->seq;
			req->checksum = rtpse_mcu_checksum((u8 *)req, RTPSE_MCU_MSG_SIZE - 1);

			ret = pse->transport->send(pse, req);
			if (ret)
				return ret;

			/* Pace the base reply delay; the transport waits its own way. */
			msleep(RTPSE_MCU_RESPONSE_MS);

			memset(resp, 0, sizeof(*resp));
			ret = pse->transport->recv(pse, req, resp);
			if (ret)
				return ret;
		}

		/* NOT_READY: MCU busy, wants the command resent; bounded retry. */
		if (resp->opcode != RTPSE_MCU_OPCODE_NOT_READY ||
		    tries >= RTPSE_MCU_NOT_READY_RETRIES)
			break;
		msleep(RTPSE_MCU_RESPONSE_MS);
	}

	/* Explicit MCU error opcodes (Gen1); map to a meaningful errno. */
	switch (resp->opcode) {
	case RTPSE_MCU_OPCODE_INCOMPLETE:
		return -EBADE;
	case RTPSE_MCU_OPCODE_BAD_CSUM:
		return -EBADMSG;
	case RTPSE_MCU_OPCODE_NOT_READY:
		return -EAGAIN;
	}

	if (resp->opcode != req->opcode ||
	    resp->seq_num != req->seq_num ||
	    resp->checksum != rtpse_mcu_checksum((u8 *)resp, RTPSE_MCU_MSG_SIZE - 1))
		return -EBADMSG;

	return 0;
}

static int rtpse_mcu_port_query(struct rtpse_mcu_ctrl *pse, unsigned int port, u8 opcode,
				struct rtpse_mcu_msg *resp)
{
	struct rtpse_mcu_msg req;
	int ret;

	rtpse_mcu_msg_init(&req, opcode);
	req.payload[0] = port;

	ret = rtpse_mcu_do_xfer(pse, &req, resp);
	if (ret)
		return ret;

	if (resp->payload[0] != port)
		return -EIO;

	return 0;
}

static int rtpse_mcu_port_cmd(struct rtpse_mcu_ctrl *pse, unsigned int port, u8 opcode, u8 arg)
{
	struct rtpse_mcu_msg req, resp;
	int ret;

	rtpse_mcu_msg_init(&req, opcode);
	req.payload[0] = port;
	req.payload[1] = arg;

	ret = rtpse_mcu_do_xfer(pse, &req, &resp);
	if (ret)
		return ret;

	if (resp.payload[0] != port || resp.payload[1] != 0)
		return -EIO;

	return 0;
}

/* Global operations */

static int rtpse_mcu_get_info(struct rtpse_mcu_ctrl *pse, struct rtpse_mcu_info *info)
{
	struct rtpse_mcu_msg req, resp;
	const struct rtpse_mcu_opcode *opc;
	int ret;

	opc = &pse->dialect->opcode[RTPSE_MCU_CMD_GET_SYSTEM_INFO];
	if (!opc->valid)
		return -EOPNOTSUPP;

	rtpse_mcu_msg_init(&req, opc->op);
	ret = rtpse_mcu_do_xfer(pse, &req, &resp);
	if (ret)
		return ret;

	pse->dialect->parse_system_info(resp.payload, info);
	return 0;
}

static int rtpse_mcu_get_ext_config(struct rtpse_mcu_ctrl *pse, struct rtpse_mcu_ext_config *config)
{
	struct rtpse_mcu_msg req, resp;
	const struct rtpse_mcu_opcode *opc;
	int ret;

	opc = &pse->dialect->opcode[RTPSE_MCU_CMD_GET_EXT_CONFIG];
	if (!opc->valid)
		return -EOPNOTSUPP;

	rtpse_mcu_msg_init(&req, opc->op);
	ret = rtpse_mcu_do_xfer(pse, &req, &resp);
	if (ret)
		return ret;

	config->num_of_pses = resp.payload[6];

	return 0;
}

static int rtpse_mcu_set_global_state(struct rtpse_mcu_ctrl *pse, bool enable)
{
	struct rtpse_mcu_msg req, resp;
	const struct rtpse_mcu_opcode *opc;
	int ret;

	opc = &pse->dialect->opcode[RTPSE_MCU_CMD_SET_GLOBAL_STATE];
	if (!opc->valid)
		return -EOPNOTSUPP;

	rtpse_mcu_msg_init(&req, opc->op);
	req.payload[0] = enable ? 0x1 : 0x0;

	ret = rtpse_mcu_do_xfer(pse, &req, &resp);
	if (ret)
		return ret;

	return (resp.payload[0] == 0x0) ? 0 : -EIO;
}

/* Port operations */

static int rtpse_mcu_port_get_status(struct rtpse_mcu_ctrl *pse, unsigned int port,
				     struct rtpse_mcu_port_status *status)
{
	const struct rtpse_mcu_opcode *opc;
	struct rtpse_mcu_msg resp;
	int ret;

	opc = &pse->dialect->opcode[RTPSE_MCU_CMD_PORT_GET_STATUS];
	if (!opc->valid)
		return -EOPNOTSUPP;

	ret = rtpse_mcu_port_query(pse, port, opc->op, &resp);
	if (ret)
		return ret;

	status->sts1 = resp.payload[1];
	status->sts2 = resp.payload[2];
	status->sts3 = resp.payload[3];

	return 0;
}

static int rtpse_mcu_port_get_measurement(struct rtpse_mcu_ctrl *pse, unsigned int port,
					  struct rtpse_mcu_port_measurement *measurement)
{
	const struct rtpse_mcu_opcode *opc;
	struct rtpse_mcu_msg resp;
	int ret;

	opc = &pse->dialect->opcode[RTPSE_MCU_CMD_PORT_GET_POWER_STATS];
	if (!opc->valid)
		return -EOPNOTSUPP;

	ret = rtpse_mcu_port_query(pse, port, opc->op, &resp);
	if (ret)
		return ret;

	measurement->voltage_raw = get_unaligned_be16(&resp.payload[1]);
	measurement->current_raw = get_unaligned_be16(&resp.payload[3]);
	measurement->temperature_raw = get_unaligned_be16(&resp.payload[5]);
	measurement->power_raw = get_unaligned_be16(&resp.payload[7]);

	return 0;
}

static int rtpse_mcu_port_get_config(struct rtpse_mcu_ctrl *pse, unsigned int port,
				     struct rtpse_mcu_port_config *config)
{
	const struct rtpse_mcu_opcode *opc;
	struct rtpse_mcu_msg resp;
	int ret;

	opc = &pse->dialect->opcode[RTPSE_MCU_CMD_PORT_GET_CONFIG];
	if (!opc->valid)
		return -EOPNOTSUPP;

	ret = rtpse_mcu_port_query(pse, port, opc->op, &resp);
	if (ret)
		return ret;

	config->enable = (resp.payload[1] == 1);

	return 0;
}

static int rtpse_mcu_port_get_ext_config(struct rtpse_mcu_ctrl *pse, unsigned int port,
					 struct rtpse_mcu_port_ext_config *config)
{
	const struct rtpse_mcu_opcode *opc;
	struct rtpse_mcu_msg resp;
	int ret;

	opc = &pse->dialect->opcode[RTPSE_MCU_CMD_PORT_GET_EXT_CONFIG];
	if (!opc->valid)
		return -EOPNOTSUPP;

	ret = rtpse_mcu_port_query(pse, port, opc->op, &resp);
	if (ret)
		return ret;

	config->max_power = resp.payload[3];
	config->priority = resp.payload[4];

	return 0;
}

static int rtpse_mcu_port_set_state(struct rtpse_mcu_ctrl *pse, unsigned int port, bool enable)
{
	const struct rtpse_mcu_opcode *opc;

	opc = &pse->dialect->opcode[RTPSE_MCU_CMD_PORT_ENABLE];
	if (!opc->valid)
		return -EOPNOTSUPP;

	return rtpse_mcu_port_cmd(pse, port, opc->op, enable ? 0x1 : 0x0);
}

/* PSE controller ops */

static int rtpse_mcu_port_get_admin_state(struct pse_controller_dev *pcdev, int id,
					  struct pse_admin_state *admin_state)
{
	struct rtpse_mcu_ctrl *pse = to_rtpse_mcu_ctrl(pcdev);
	struct rtpse_mcu_port_config config;
	int ret;

	ret = rtpse_mcu_port_get_config(pse, id, &config);
	if (ret)
		return ret;

	admin_state->c33_admin_state = config.enable ? ETHTOOL_C33_PSE_ADMIN_STATE_ENABLED :
						       ETHTOOL_C33_PSE_ADMIN_STATE_DISABLED;
	return 0;
}

static int rtpse_mcu_port_get_pw_status(struct pse_controller_dev *pcdev, int id,
					struct pse_pw_status *pw_status)
{
	struct rtpse_mcu_ctrl *pse = to_rtpse_mcu_ctrl(pcdev);
	struct rtpse_mcu_port_status status;
	int ret;

	ret = rtpse_mcu_port_get_status(pse, id, &status);
	if (ret)
		return ret;

	switch (status.sts1) {
	case RTPSE_MCU_PORT_STS_DISABLED:
		pw_status->c33_pw_status = ETHTOOL_C33_PSE_PW_D_STATUS_DISABLED;
		break;
	case RTPSE_MCU_PORT_STS_SEARCHING:
	case RTPSE_MCU_PORT_STS_REQUESTING:
		pw_status->c33_pw_status = ETHTOOL_C33_PSE_PW_D_STATUS_SEARCHING;
		break;
	case RTPSE_MCU_PORT_STS_DELIVERING:
		pw_status->c33_pw_status = ETHTOOL_C33_PSE_PW_D_STATUS_DELIVERING;
		break;
	case RTPSE_MCU_PORT_STS_TEST:
		pw_status->c33_pw_status = ETHTOOL_C33_PSE_PW_D_STATUS_TEST;
		break;
	case RTPSE_MCU_PORT_STS_FAULT:
		pw_status->c33_pw_status = ETHTOOL_C33_PSE_PW_D_STATUS_FAULT;
		break;
	case RTPSE_MCU_PORT_STS_OTHER_FAULT:
		pw_status->c33_pw_status = ETHTOOL_C33_PSE_PW_D_STATUS_OTHERFAULT;
		break;
	default:
		pw_status->c33_pw_status = ETHTOOL_C33_PSE_PW_D_STATUS_UNKNOWN;
		break;
	}

	return 0;
}

static int rtpse_mcu_port_get_pw_class(struct pse_controller_dev *pcdev, int id)
{
	struct rtpse_mcu_ctrl *pse = to_rtpse_mcu_ctrl(pcdev);
	struct rtpse_mcu_port_status status;
	int ret;

	ret = rtpse_mcu_port_get_status(pse, id, &status);
	if (ret)
		return ret;

	/*
	 * As per datasheet, the classification result is only valid when in
	 * one of those operational modes, otherwise not.
	 */
	switch (status.sts1) {
	case RTPSE_MCU_PORT_STS_DISABLED:
	case RTPSE_MCU_PORT_STS_SEARCHING:
	case RTPSE_MCU_PORT_STS_DELIVERING:
	case RTPSE_MCU_PORT_STS_REQUESTING:
		return pse->dialect->parse_port_class(&status);
	default:
		/*
		 * No class to report, return 0 instead. This is indistinguishable
		 * from a real class-0 PD but userspace disambiguates via the
		 * power status.
		 */
		return 0;
	}
}

static int rtpse_mcu_port_get_actual_pw(struct pse_controller_dev *pcdev, int id)
{
	struct rtpse_mcu_ctrl *pse = to_rtpse_mcu_ctrl(pcdev);
	struct rtpse_mcu_port_measurement measurement;
	int ret;

	ret = rtpse_mcu_port_get_measurement(pse, id, &measurement);
	if (ret)
		return ret;

	/* 100mW per LSB */
	return measurement.power_raw * 100U;
}

static int rtpse_mcu_port_get_voltage(struct pse_controller_dev *pcdev, int id)
{
	struct rtpse_mcu_ctrl *pse = to_rtpse_mcu_ctrl(pcdev);
	struct rtpse_mcu_port_measurement measurement;
	int ret;
	u32 uV;

	ret = rtpse_mcu_port_get_measurement(pse, id, &measurement);
	if (ret)
		return ret;

	/* 64.45mV per LSB */
	uV = measurement.voltage_raw * 64450U;

	/*
	 * Idle ports measure 0V, which the core rejects when turning a power
	 * limit into a current limit. Fall back to the nominal rail so a limit
	 * can be set before a PD is attached.
	 */
	if (!uV)
		return RTPSE_MCU_PSE_VOLTAGE_UV;

	return min_t(u32, uV, INT_MAX);
}

static int rtpse_mcu_port_enable(struct pse_controller_dev *pcdev, int id)
{
	return rtpse_mcu_port_set_state(to_rtpse_mcu_ctrl(pcdev), id, true);
}

static int rtpse_mcu_port_disable(struct pse_controller_dev *pcdev, int id)
{
	return rtpse_mcu_port_set_state(to_rtpse_mcu_ctrl(pcdev), id, false);
}

static int rtpse_mcu_port_get_pw_limit(struct pse_controller_dev *pcdev, int id)
{
	struct rtpse_mcu_ctrl *pse = to_rtpse_mcu_ctrl(pcdev);
	struct rtpse_mcu_port_ext_config config;
	int ret;

	ret = rtpse_mcu_port_get_ext_config(pse, id, &config);
	if (ret)
		return ret;

	/*
	 * The MCU's raw max_power byte can scale above the chip's rated cap;
	 * clamp to the same bound set_pw_limit() and the advertised range use.
	 */
	return min_t(u32, config.max_power * pse->chip->pw_read_lsb_mW,
		     pse->chip->max_mW_per_port);
}

static int rtpse_mcu_port_set_pw_limit(struct pse_controller_dev *pcdev, int id, int max_mW)
{
	const struct rtpse_mcu_opcode *type_opc, *val_opc;
	struct rtpse_mcu_ctrl *pse = to_rtpse_mcu_ctrl(pcdev);
	const struct rtpse_mcu_chip_info *chip = pse->chip;
	u8 prg_val;
	int ret;

	if (max_mW < 0 || max_mW > chip->max_mW_per_port)
		return -ERANGE;

	type_opc = &pse->dialect->opcode[RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT_TYPE];
	val_opc = &pse->dialect->opcode[chip->pw_set_cmd];
	/* pw_set_lsb_mW is the divisor below; reject a chip that lacks it. */
	if (!type_opc->valid || !val_opc->valid || !chip->pw_set_lsb_mW)
		return -EOPNOTSUPP;

	/*
	 * Round up so a sub-LSB request maps to one LSB, not silently to 0;
	 * an explicit 0 still yields 0, and LSB-aligned maxima can't overshoot.
	 */
	prg_val = min_t(unsigned int, DIV_ROUND_UP(max_mW, chip->pw_set_lsb_mW), U8_MAX);

	/*
	 * Program the value before switching to user-defined mode. The two
	 * commands aren't atomic, but this order never leaves a stale cap: a
	 * failure keeps the previous cap, or (already user mode) the requested.
	 */
	ret = rtpse_mcu_port_cmd(pse, id, val_opc->op, prg_val);
	if (ret)
		return ret;

	return rtpse_mcu_port_cmd(pse, id, type_opc->op, RTPSE_MCU_PORT_PW_LIMIT_TYPE_USER);
}

static int rtpse_mcu_port_get_pw_limit_ranges(struct pse_controller_dev *pcdev, int id,
					      struct pse_pw_limit_ranges *out)
{
	struct rtpse_mcu_ctrl *pse = to_rtpse_mcu_ctrl(pcdev);
	struct ethtool_c33_pse_pw_limit_range *range;

	range = kzalloc_obj(*range);
	if (!range)
		return -ENOMEM;

	range[0].min = 0;
	range[0].max = pse->chip->max_mW_per_port;

	out->c33_pw_limit_ranges = range;
	return 1;
}

static int rtpse_mcu_port_get_prio(struct pse_controller_dev *pcdev, int id)
{
	struct rtpse_mcu_ctrl *pse = to_rtpse_mcu_ctrl(pcdev);
	struct rtpse_mcu_port_ext_config config;
	int ret;

	ret = rtpse_mcu_port_get_ext_config(pse, id, &config);
	if (ret)
		return ret;

	/* Clamp to the advertised max; set_prio() and pis_prio_max use the same bound. */
	return min_t(u8, config.priority, RTPSE_MCU_PORT_MAX_PRIORITY);
}

static int rtpse_mcu_port_set_prio(struct pse_controller_dev *pcdev, int id, unsigned int prio)
{
	struct rtpse_mcu_ctrl *pse = to_rtpse_mcu_ctrl(pcdev);
	const struct rtpse_mcu_opcode *opc;

	if (prio > RTPSE_MCU_PORT_MAX_PRIORITY)
		return -ERANGE;

	opc = &pse->dialect->opcode[RTPSE_MCU_CMD_PORT_SET_PRIORITY];
	if (!opc->valid)
		return -EOPNOTSUPP;

	return rtpse_mcu_port_cmd(pse, id, opc->op, prio);
}

static const struct pse_controller_ops rtpse_mcu_ops = {
	.pi_get_admin_state = rtpse_mcu_port_get_admin_state,
	.pi_get_pw_status = rtpse_mcu_port_get_pw_status,
	.pi_get_pw_class = rtpse_mcu_port_get_pw_class,
	.pi_get_actual_pw = rtpse_mcu_port_get_actual_pw,
	.pi_enable = rtpse_mcu_port_enable,
	.pi_disable = rtpse_mcu_port_disable,
	.pi_get_voltage = rtpse_mcu_port_get_voltage,
	.pi_get_pw_limit = rtpse_mcu_port_get_pw_limit,
	.pi_set_pw_limit = rtpse_mcu_port_set_pw_limit,
	.pi_get_pw_limit_ranges = rtpse_mcu_port_get_pw_limit_ranges,
	.pi_get_prio = rtpse_mcu_port_get_prio,
	.pi_set_prio = rtpse_mcu_port_set_prio,
};

static int rtpse_mcu_discover(struct rtpse_mcu_ctrl *pse, struct rtpse_mcu_info *info)
{
	struct rtpse_mcu_ext_config ext_config;
	unsigned long deadline;
	int ret;

	/*
	 * A booting MCU may stay silent (-ETIMEDOUT), not ACK its address
	 * (-ENXIO / -EREMOTEIO), report not-ready (-EAGAIN), or emit a
	 * corrupt/partial frame (-EBADMSG / -EBADE). Retry those within a
	 * bounded window; other errors (e.g. -EOPNOTSUPP) are fatal and fail
	 * immediately.
	 */
	deadline = jiffies + msecs_to_jiffies(RTPSE_MCU_BOOT_TIMEOUT_MS);
	do {
		ret = rtpse_mcu_get_info(pse, info);
		if (ret != -ETIMEDOUT && ret != -ENXIO && ret != -EREMOTEIO &&
		    ret != -EAGAIN && ret != -EBADMSG && ret != -EBADE)
			break;
		msleep(RTPSE_MCU_BOOT_RETRY_MS);
	} while (time_before(jiffies, deadline));
	if (ret)
		return dev_err_probe(pse->dev, ret, "failed to read MCU info\n");

	switch (info->device_id) {
	case RTPSE_MCU_DEVICE_ID_RTL8238B:
		pse->chip = &rtl8238b_info;
		break;
	case RTPSE_MCU_DEVICE_ID_RTL8239:
		pse->chip = &rtl8239_info;
		break;
	case RTPSE_MCU_DEVICE_ID_RTL8239C:
		pse->chip = &rtl8239c_info;
		break;
	case RTPSE_MCU_DEVICE_ID_BCM59111:
		pse->chip = &bcm59111_info;
		break;
	case RTPSE_MCU_DEVICE_ID_BCM59121:
		pse->chip = &bcm59121_info;
		break;
	default:
		return dev_err_probe(pse->dev, -EINVAL, "unknown PSE id 0x%x\n",
				     info->device_id);
	}

	if (!info->max_ports || info->max_ports > RTPSE_MCU_MAX_PORTS)
		return dev_err_probe(pse->dev, -EINVAL,
				     "MCU reports invalid port count %u\n", info->max_ports);

	ret = rtpse_mcu_get_ext_config(pse, &ext_config);
	if (ret)
		return dev_err_probe(pse->dev, ret, "failed to read MCU ext config\n");

	dev_info(pse->dev, "%s MCU, %s (id 0x%04x), %u ports across %u PSE chip(s)\n",
		 pse->dialect->mcu_type_str(info->mcu_type), pse->chip->name,
		 info->device_id, info->max_ports, ext_config.num_of_pses);
	return 0;
}

static void rtpse_mcu_global_disable(void *data)
{
	struct rtpse_mcu_ctrl *pse = data;

	rtpse_mcu_set_global_state(pse, false);
}

int rtpse_mcu_register(struct rtpse_mcu_ctrl *pse)
{
	const struct rtpse_mcu_match_data *match;
	struct rtpse_mcu_info info;
	struct gpio_desc *gpiod;
	int ret;

	BUILD_BUG_ON(sizeof(struct rtpse_mcu_msg) != RTPSE_MCU_MSG_SIZE);

	ret = devm_mutex_init(pse->dev, &pse->mutex);
	if (ret)
		return ret;

	match = device_get_match_data(pse->dev);
	if (!match)
		return dev_err_probe(pse->dev, -ENODEV, "missing match data\n");
	pse->dialect = match->dialect;

	/*
	 * Catch a dialect that forgot to set one of the required hooks at
	 * probe time, rather than NULL-deref'ing later from a fast path.
	 */
	if (!pse->dialect ||
	    !pse->dialect->parse_system_info ||
	    !pse->dialect->parse_port_class ||
	    !pse->dialect->mcu_type_str)
		return dev_err_probe(pse->dev, -EINVAL,
				     "dialect for chip is incomplete\n");

	/*
	 * Release the MCU from reset before the first transaction; the
	 * boot-retry loop in discover() waits for it to answer.
	 */
	gpiod = devm_gpiod_get_optional(pse->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod))
		return dev_err_probe(pse->dev, PTR_ERR(gpiod),
				     "failed to get reset gpio\n");

	ret = rtpse_mcu_discover(pse, &info);
	if (ret)
		return ret;

	/*
	 * Some boards gate all ports through a hardware line; deassert it only
	 * after the MCU is confirmed, so a discover failure never ungates the
	 * ports. It is then left to the MCU - not re-gated on unbind or a later
	 * probe error - so a driver reload doesn't black out PoE.
	 */
	gpiod = devm_gpiod_get_optional(pse->dev, "disable-ports", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod))
		return dev_err_probe(pse->dev, PTR_ERR(gpiod),
				     "failed to get disable-ports gpio\n");

	if (!info.system_enable) {
		ret = rtpse_mcu_set_global_state(pse, true);
		/* Dialects without a global-state concept (e.g. Gen1) return
		 * -EOPNOTSUPP; treat that as "no separate enable required".
		 */
		if (ret && ret != -EOPNOTSUPP)
			return dev_err_probe(pse->dev, ret,
					     "failed to enable PSE system\n");
		if (!ret) {
			ret = devm_add_action_or_reset(pse->dev,
						       rtpse_mcu_global_disable, pse);
			if (ret)
				return ret;
		}
	}

	/*
	 * Depending on the MCU firmware configuration (which might be different
	 * for every board), it isn't known whether the PoE subsystem is active or
	 * inactive by default. At this stage, the PSE chips might already deliver
	 * power to PDs without any explicit enable.
	 */

	/* pcdev.owner is set by the transport, so the registered controller
	 * pins the transport module that owns the live device, not the core.
	 */
	pse->pcdev.ops      = &rtpse_mcu_ops;
	pse->pcdev.dev      = pse->dev;
	pse->pcdev.types    = ETHTOOL_PSE_C33;
	pse->pcdev.nr_lines = info.max_ports;
	pse->pcdev.pis_prio_max = RTPSE_MCU_PORT_MAX_PRIORITY;
	pse->pcdev.supp_budget_eval_strategies = PSE_BUDGET_EVAL_STRAT_DYNAMIC;

	return devm_pse_controller_register(pse->dev, &pse->pcdev);
}
EXPORT_SYMBOL_GPL(rtpse_mcu_register);

static void rtpse_mcu_gen2_parse_system_info(const u8 *payload, struct rtpse_mcu_info *info)
{
	info->max_ports = payload[1];
	info->system_enable = (payload[2] == 0x1);
	info->device_id = get_unaligned_be16(&payload[3]);
	info->mcu_type = payload[6];
}

static int rtpse_mcu_gen2_parse_port_class(const struct rtpse_mcu_port_status *status)
{
	/* Class lives in the upper nibble of sts2. */
	return FIELD_GET(GENMASK(7, 4), status->sts2);
}

static const char *rtpse_mcu_gen2_mcu_type_str(unsigned int mcu_type)
{
	switch (mcu_type) {
	case 0x00:	return "GigaDevice GD32F310";
	case 0x01:	return "GigaDevice GD32F230";
	case 0x02:	return "GigaDevice GD32F303";
	case 0x03:	return "GigaDevice GD32F103";
	case 0x04:	return "GigaDevice GD32E103";
	case 0x10:	return "Nuvoton M0516";
	case 0x11:	return "Nuvoton M0564";
	case 0x12:	return "Nuvoton NUC029";
	default:	return "unknown";
	}
}

static void rtpse_mcu_gen1_parse_system_info(const u8 *payload, struct rtpse_mcu_info *info)
{
	info->max_ports = payload[1];
	/* Gen1 has no explicit system_enable byte; the closest analog is the
	 * "remote enable" bit in the system-status flags at payload[7].
	 */
	info->system_enable = !!(payload[7] & BIT(2));
	info->device_id = get_unaligned_be16(&payload[3]);
	info->mcu_type = payload[6];
}

static int rtpse_mcu_gen1_parse_port_class(const struct rtpse_mcu_port_status *status)
{
	/* Gen1 puts the detected class in payload[3] (== sts3) directly.
	 * Mask to the low nibble; class is 0..8 and any high bits would be
	 * noise.
	 */
	return status->sts3 & 0x0f;
}

static const char *rtpse_mcu_gen1_mcu_type_str(unsigned int mcu_type)
{
	switch (mcu_type) {
	case 0x00:	return "ST Micro ST32F100";
	case 0x01:	return "Nuvoton M05xx LAN";
	case 0x02:	return "ST Micro STF030C8";
	case 0x03:	return "Nuvoton M058SAN";
	case 0x04:	return "Nuvoton NUC122";
	default:	return "unknown";
	}
}

/* Map each logical command the core issues to its per-dialect opcode. */
static const struct rtpse_mcu_dialect rtpse_mcu_dialect_gen2 = {
	.parse_system_info = rtpse_mcu_gen2_parse_system_info,
	.parse_port_class  = rtpse_mcu_gen2_parse_port_class,
	.mcu_type_str      = rtpse_mcu_gen2_mcu_type_str,
	.opcode = {
		[RTPSE_MCU_CMD_SET_GLOBAL_STATE]	= RTPSE_MCU_OP(0x00),
		[RTPSE_MCU_CMD_GET_SYSTEM_INFO]		= RTPSE_MCU_OP(0x40),
		[RTPSE_MCU_CMD_GET_EXT_CONFIG]		= RTPSE_MCU_OP(0x4a),

		[RTPSE_MCU_CMD_PORT_ENABLE]		= RTPSE_MCU_OP(0x01),
		[RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT_TYPE] = RTPSE_MCU_OP(0x12),
		[RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT]	= RTPSE_MCU_OP(0x13),
		[RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT_EXT] = RTPSE_MCU_OP(0x14),
		[RTPSE_MCU_CMD_PORT_SET_PRIORITY]	= RTPSE_MCU_OP(0x15),
		[RTPSE_MCU_CMD_PORT_GET_STATUS]		= RTPSE_MCU_OP(0x42),
		[RTPSE_MCU_CMD_PORT_GET_POWER_STATS]	= RTPSE_MCU_OP(0x44),
		[RTPSE_MCU_CMD_PORT_GET_CONFIG]		= RTPSE_MCU_OP(0x48),
		[RTPSE_MCU_CMD_PORT_GET_EXT_CONFIG]	= RTPSE_MCU_OP(0x49),
	},
};

static const struct rtpse_mcu_dialect rtpse_mcu_dialect_gen1 = {
	.parse_system_info = rtpse_mcu_gen1_parse_system_info,
	.parse_port_class  = rtpse_mcu_gen1_parse_port_class,
	.mcu_type_str      = rtpse_mcu_gen1_mcu_type_str,
	.opcode = {
		[RTPSE_MCU_CMD_GET_SYSTEM_INFO]		= RTPSE_MCU_OP(0x20),
		[RTPSE_MCU_CMD_GET_EXT_CONFIG]		= RTPSE_MCU_OP(0x2b),

		[RTPSE_MCU_CMD_PORT_ENABLE]		= RTPSE_MCU_OP(0x00),
		[RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT_TYPE] = RTPSE_MCU_OP(0x15),
		[RTPSE_MCU_CMD_PORT_SET_POWER_LIMIT]	= RTPSE_MCU_OP(0x16),
		[RTPSE_MCU_CMD_PORT_SET_PRIORITY]	= RTPSE_MCU_OP(0x1a),
		[RTPSE_MCU_CMD_PORT_GET_STATUS]		= RTPSE_MCU_OP(0x21),
		[RTPSE_MCU_CMD_PORT_GET_POWER_STATS]	= RTPSE_MCU_OP(0x30),
		[RTPSE_MCU_CMD_PORT_GET_CONFIG]		= RTPSE_MCU_OP(0x25),
		[RTPSE_MCU_CMD_PORT_GET_EXT_CONFIG]	= RTPSE_MCU_OP(0x26),
	},
};

const struct rtpse_mcu_match_data rtpse_mcu_gen1_data = {
	.dialect = &rtpse_mcu_dialect_gen1,
};
EXPORT_SYMBOL_GPL(rtpse_mcu_gen1_data);

const struct rtpse_mcu_match_data rtpse_mcu_gen2_data = {
	.dialect = &rtpse_mcu_dialect_gen2,
};
EXPORT_SYMBOL_GPL(rtpse_mcu_gen2_data);

/* Same dialect as gen2, but the MCU expects raw-I2C framing. */
const struct rtpse_mcu_match_data rtpse_mcu_gen2_i2c_data = {
	.dialect = &rtpse_mcu_dialect_gen2,
	.native_i2c = true,
};
EXPORT_SYMBOL_GPL(rtpse_mcu_gen2_i2c_data);

MODULE_AUTHOR("Jonas Jelonek <jelonek.jonas@gmail.com>");
MODULE_DESCRIPTION("Realtek PSE MCU driver (core)");
MODULE_LICENSE("GPL");
