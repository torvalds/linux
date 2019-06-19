// SPDX-License-Identifier: GPL-2.0-only
/*
 * --------------------------------------------------------------------
 * Driver for ST NFC Transceiver ST95HF
 * --------------------------------------------------------------------
 * Copyright (C) 2015 STMicroelectronics Pvt. Ltd. All rights reserved.
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/nfc.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/wait.h>
#include <net/nfc/digital.h>
#include <net/nfc/nfc.h>

#include "spi.h"

/* supported protocols */
#define ST95HF_SUPPORTED_PROT		(NFC_PROTO_ISO14443_MASK | \
					NFC_PROTO_ISO14443_B_MASK | \
					NFC_PROTO_ISO15693_MASK)
/* driver capabilities */
#define ST95HF_CAPABILITIES		NFC_DIGITAL_DRV_CAPS_IN_CRC

/* Command Send Interface */
/* ST95HF_COMMAND_SEND CMD Ids */
#define ECHO_CMD			0x55
#define WRITE_REGISTER_CMD		0x9
#define PROTOCOL_SELECT_CMD		0x2
#define SEND_RECEIVE_CMD		0x4

/* Select protocol codes */
#define ISO15693_PROTOCOL_CODE		0x1
#define ISO14443A_PROTOCOL_CODE		0x2
#define ISO14443B_PROTOCOL_CODE		0x3

/*
 * head room len is 3
 * 1 byte for control byte
 * 1 byte for cmd
 * 1 byte for size
 */
#define ST95HF_HEADROOM_LEN		3

/*
 * tailroom is 1 for ISO14443A
 * and 0 for ISO14443B/ISO15693,
 * hence the max value 1 should be
 * taken.
 */
#define ST95HF_TAILROOM_LEN		1

/* Command Response interface */
#define MAX_RESPONSE_BUFFER_SIZE	280
#define ECHORESPONSE			0x55
#define ST95HF_ERR_MASK			0xF
#define ST95HF_TIMEOUT_ERROR		0x87
#define ST95HF_NFCA_CRC_ERR_MASK	0x20
#define ST95HF_NFCB_CRC_ERR_MASK	0x01

/* ST95HF transmission flag values */
#define TRFLAG_NFCA_SHORT_FRAME		0x07
#define TRFLAG_NFCA_STD_FRAME		0x08
#define TRFLAG_NFCA_STD_FRAME_CRC	0x28

/* Misc defs */
#define HIGH				1
#define LOW				0
#define ISO14443A_RATS_REQ		0xE0
#define RATS_TB1_PRESENT_MASK		0x20
#define RATS_TA1_PRESENT_MASK		0x10
#define TB1_FWI_MASK			0xF0
#define WTX_REQ_FROM_TAG		0xF2

#define MAX_CMD_LEN			0x7

#define MAX_CMD_PARAMS			4
struct cmd {
	int cmd_len;
	unsigned char cmd_id;
	unsigned char no_cmd_params;
	unsigned char cmd_params[MAX_CMD_PARAMS];
	enum req_type req;
};

struct param_list {
	int param_offset;
	int new_param_val;
};

/*
 * List of top-level cmds to be used internally by the driver.
 * All these commands are build on top of ST95HF basic commands
 * such as SEND_RECEIVE_CMD, PROTOCOL_SELECT_CMD, etc.
 * These top level cmds are used internally while implementing various ops of
 * digital layer/driver probe or extending the digital framework layer for
 * features that are not yet implemented there, for example, WTX cmd handling.
 */
enum st95hf_cmd_list {
	CMD_ECHO,
	CMD_ISO14443A_CONFIG,
	CMD_ISO14443A_DEMOGAIN,
	CMD_ISO14443B_DEMOGAIN,
	CMD_ISO14443A_PROTOCOL_SELECT,
	CMD_ISO14443B_PROTOCOL_SELECT,
	CMD_WTX_RESPONSE,
	CMD_FIELD_OFF,
	CMD_ISO15693_PROTOCOL_SELECT,
};

static const struct cmd cmd_array[] = {
	[CMD_ECHO] = {
		.cmd_len = 0x2,
		.cmd_id = ECHO_CMD,
		.no_cmd_params = 0,
		.req = SYNC,
	},
	[CMD_ISO14443A_CONFIG] = {
		.cmd_len = 0x7,
		.cmd_id = WRITE_REGISTER_CMD,
		.no_cmd_params = 0x4,
		.cmd_params = {0x3A, 0x00, 0x5A, 0x04},
		.req = SYNC,
	},
	[CMD_ISO14443A_DEMOGAIN] = {
		.cmd_len = 0x7,
		.cmd_id = WRITE_REGISTER_CMD,
		.no_cmd_params = 0x4,
		.cmd_params = {0x68, 0x01, 0x01, 0xDF},
		.req = SYNC,
	},
	[CMD_ISO14443B_DEMOGAIN] = {
		.cmd_len = 0x7,
		.cmd_id = WRITE_REGISTER_CMD,
		.no_cmd_params = 0x4,
		.cmd_params = {0x68, 0x01, 0x01, 0x51},
		.req = SYNC,
	},
	[CMD_ISO14443A_PROTOCOL_SELECT] = {
		.cmd_len = 0x7,
		.cmd_id = PROTOCOL_SELECT_CMD,
		.no_cmd_params = 0x4,
		.cmd_params = {ISO14443A_PROTOCOL_CODE, 0x00, 0x01, 0xA0},
		.req = SYNC,
	},
	[CMD_ISO14443B_PROTOCOL_SELECT] = {
		.cmd_len = 0x7,
		.cmd_id = PROTOCOL_SELECT_CMD,
		.no_cmd_params = 0x4,
		.cmd_params = {ISO14443B_PROTOCOL_CODE, 0x01, 0x03, 0xFF},
		.req = SYNC,
	},
	[CMD_WTX_RESPONSE] = {
		.cmd_len = 0x6,
		.cmd_id = SEND_RECEIVE_CMD,
		.no_cmd_params = 0x3,
		.cmd_params = {0xF2, 0x00, TRFLAG_NFCA_STD_FRAME_CRC},
		.req = ASYNC,
	},
	[CMD_FIELD_OFF] = {
		.cmd_len = 0x5,
		.cmd_id = PROTOCOL_SELECT_CMD,
		.no_cmd_params = 0x2,
		.cmd_params = {0x0, 0x0},
		.req = SYNC,
	},
	[CMD_ISO15693_PROTOCOL_SELECT] = {
		.cmd_len = 0x5,
		.cmd_id = PROTOCOL_SELECT_CMD,
		.no_cmd_params = 0x2,
		.cmd_params = {ISO15693_PROTOCOL_CODE, 0x0D},
		.req = SYNC,
	},
};

/* st95_digital_cmd_complete_arg stores client context */
struct st95_digital_cmd_complete_arg {
	struct sk_buff *skb_resp;
	nfc_digital_cmd_complete_t complete_cb;
	void *cb_usrarg;
	bool rats;
};

/*
 * structure containing ST95HF driver specific data.
 * @spicontext: structure containing information required
 *	for spi communication between st95hf and host.
 * @ddev: nfc digital device object.
 * @nfcdev: nfc device object.
 * @enable_gpio: gpio used to enable st95hf transceiver.
 * @complete_cb_arg: structure to store various context information
 *	that is passed from nfc requesting thread to the threaded ISR.
 * @st95hf_supply: regulator "consumer" for NFC device.
 * @sendrcv_trflag: last byte of frame send by sendrecv command
 *	of st95hf. This byte contains transmission flag info.
 * @exchange_lock: semaphore used for signaling the st95hf_remove
 *	function that the last outstanding async nfc request is finished.
 * @rm_lock: mutex for ensuring safe access of nfc digital object
 *	from threaded ISR. Usage of this mutex avoids any race between
 *	deletion of the object from st95hf_remove() and its access from
 *	the threaded ISR.
 * @nfcdev_free: flag to have the state of nfc device object.
 *	[alive | died]
 * @current_protocol: current nfc protocol.
 * @current_rf_tech: current rf technology.
 * @fwi: frame waiting index, received in reply of RATS according to
 *	digital protocol.
 */
struct st95hf_context {
	struct st95hf_spi_context spicontext;
	struct nfc_digital_dev *ddev;
	struct nfc_dev *nfcdev;
	unsigned int enable_gpio;
	struct st95_digital_cmd_complete_arg complete_cb_arg;
	struct regulator *st95hf_supply;
	unsigned char sendrcv_trflag;
	struct semaphore exchange_lock;
	struct mutex rm_lock;
	bool nfcdev_free;
	u8 current_protocol;
	u8 current_rf_tech;
	int fwi;
};

/*
 * st95hf_send_recv_cmd() is for sending commands to ST95HF
 * that are described in the cmd_array[]. It can optionally
 * receive the response if the cmd request is of type
 * SYNC. For that to happen caller must pass true to recv_res.
 * For ASYNC request, recv_res is ignored and the
 * function will never try to receive the response on behalf
 * of the caller.
 */
static int st95hf_send_recv_cmd(struct st95hf_context *st95context,
				enum st95hf_cmd_list cmd,
				int no_modif,
				struct param_list *list_array,
				bool recv_res)
{
	unsigned char spi_cmd_buffer[MAX_CMD_LEN];
	int i, ret;
	struct device *dev = &st95context->spicontext.spidev->dev;

	if (cmd_array[cmd].cmd_len > MAX_CMD_LEN)
		return -EINVAL;
	if (cmd_array[cmd].no_cmd_params < no_modif)
		return -EINVAL;
	if (no_modif && !list_array)
		return -EINVAL;

	spi_cmd_buffer[0] = ST95HF_COMMAND_SEND;
	spi_cmd_buffer[1] = cmd_array[cmd].cmd_id;
	spi_cmd_buffer[2] = cmd_array[cmd].no_cmd_params;

	memcpy(&spi_cmd_buffer[3], cmd_array[cmd].cmd_params,
	       spi_cmd_buffer[2]);

	for (i = 0; i < no_modif; i++) {
		if (list_array[i].param_offset >= cmd_array[cmd].no_cmd_params)
			return -EINVAL;
		spi_cmd_buffer[3 + list_array[i].param_offset] =
						list_array[i].new_param_val;
	}

	ret = st95hf_spi_send(&st95context->spicontext,
			      spi_cmd_buffer,
			      cmd_array[cmd].cmd_len,
			      cmd_array[cmd].req);
	if (ret) {
		dev_err(dev, "st95hf_spi_send failed with error %d\n", ret);
		return ret;
	}

	if (cmd_array[cmd].req == SYNC && recv_res) {
		unsigned char st95hf_response_arr[2];

		ret = st95hf_spi_recv_response(&st95context->spicontext,
					       st95hf_response_arr);
		if (ret < 0) {
			dev_err(dev, "spi error from st95hf_spi_recv_response(), err = 0x%x\n",
				ret);
			return ret;
		}

		if (st95hf_response_arr[0]) {
			dev_err(dev, "st95hf error from st95hf_spi_recv_response(), err = 0x%x\n",
				st95hf_response_arr[0]);
			return -EIO;
		}
	}

	return 0;
}

static int st95hf_echo_command(struct st95hf_context *st95context)
{
	int result = 0;
	unsigned char echo_response;

	result = st95hf_send_recv_cmd(st95context, CMD_ECHO, 0, NULL, false);
	if (result)
		return result;

	/* If control reached here, response can be taken */
	result = st95hf_spi_recv_echo_res(&st95context->spicontext,
					  &echo_response);
	if (result) {
		dev_err(&st95context->spicontext.spidev->dev,
			"err: echo response receieve error = 0x%x\n", result);
		return result;
	}

	if (echo_response == ECHORESPONSE)
		return 0;

	dev_err(&st95context->spicontext.spidev->dev, "err: echo res is 0x%x\n",
		echo_response);

	return -EIO;
}

static int secondary_configuration_type4a(struct st95hf_context *stcontext)
{
	int result = 0;
	struct device *dev = &stcontext->nfcdev->dev;

	/* 14443A config setting after select protocol */
	result = st95hf_send_recv_cmd(stcontext,
				      CMD_ISO14443A_CONFIG,
				      0,
				      NULL,
				      true);
	if (result) {
		dev_err(dev, "type a config cmd, err = 0x%x\n", result);
		return result;
	}

	/* 14443A demo gain setting */
	result = st95hf_send_recv_cmd(stcontext,
				      CMD_ISO14443A_DEMOGAIN,
				      0,
				      NULL,
				      true);
	if (result)
		dev_err(dev, "type a demogain cmd, err = 0x%x\n", result);

	return result;
}

static int secondary_configuration_type4b(struct st95hf_context *stcontext)
{
	int result = 0;
	struct device *dev = &stcontext->nfcdev->dev;

	result = st95hf_send_recv_cmd(stcontext,
				      CMD_ISO14443B_DEMOGAIN,
				      0,
				      NULL,
				      true);
	if (result)
		dev_err(dev, "type b demogain cmd, err = 0x%x\n", result);

	return result;
}

static int st95hf_select_protocol(struct st95hf_context *stcontext, int type)
{
	int result = 0;
	struct device *dev;

	dev = &stcontext->nfcdev->dev;

	switch (type) {
	case NFC_DIGITAL_RF_TECH_106A:
		stcontext->current_rf_tech = NFC_DIGITAL_RF_TECH_106A;
		result = st95hf_send_recv_cmd(stcontext,
					      CMD_ISO14443A_PROTOCOL_SELECT,
					      0,
					      NULL,
					      true);
		if (result) {
			dev_err(dev, "protocol sel, err = 0x%x\n",
				result);
			return result;
		}

		/* secondary config. for 14443Type 4A after protocol select */
		result = secondary_configuration_type4a(stcontext);
		if (result) {
			dev_err(dev, "type a secondary config, err = 0x%x\n",
				result);
			return result;
		}
		break;
	case NFC_DIGITAL_RF_TECH_106B:
		stcontext->current_rf_tech = NFC_DIGITAL_RF_TECH_106B;
		result = st95hf_send_recv_cmd(stcontext,
					      CMD_ISO14443B_PROTOCOL_SELECT,
					      0,
					      NULL,
					      true);
		if (result) {
			dev_err(dev, "protocol sel send, err = 0x%x\n",
				result);
			return result;
		}

		/*
		 * delay of 5-6 ms is required after select protocol
		 * command in case of ISO14443 Type B
		 */
		usleep_range(50000, 60000);

		/* secondary config. for 14443Type 4B after protocol select */
		result = secondary_configuration_type4b(stcontext);
		if (result) {
			dev_err(dev, "type b secondary config, err = 0x%x\n",
				result);
			return result;
		}
		break;
	case NFC_DIGITAL_RF_TECH_ISO15693:
		stcontext->current_rf_tech = NFC_DIGITAL_RF_TECH_ISO15693;
		result = st95hf_send_recv_cmd(stcontext,
					      CMD_ISO15693_PROTOCOL_SELECT,
					      0,
					      NULL,
					      true);
		if (result) {
			dev_err(dev, "protocol sel send, err = 0x%x\n",
				result);
			return result;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void st95hf_send_st95enable_negativepulse(struct st95hf_context *st95con)
{
	/* First make irq_in pin high */
	gpio_set_value(st95con->enable_gpio, HIGH);

	/* wait for 1 milisecond */
	usleep_range(1000, 2000);

	/* Make irq_in pin low */
	gpio_set_value(st95con->enable_gpio, LOW);

	/* wait for minimum interrupt pulse to make st95 active */
	usleep_range(1000, 2000);

	/* At end make it high */
	gpio_set_value(st95con->enable_gpio, HIGH);
}

/*
 * Send a reset sequence over SPI bus (Reset command + wait 3ms +
 * negative pulse on st95hf enable gpio
 */
static int st95hf_send_spi_reset_sequence(struct st95hf_context *st95context)
{
	int result = 0;
	unsigned char reset_cmd = ST95HF_COMMAND_RESET;

	result = st95hf_spi_send(&st95context->spicontext,
				 &reset_cmd,
				 ST95HF_RESET_CMD_LEN,
				 ASYNC);
	if (result) {
		dev_err(&st95context->spicontext.spidev->dev,
			"spi reset sequence cmd error = %d", result);
		return result;
	}

	/* wait for 3 milisecond to complete the controller reset process */
	usleep_range(3000, 4000);

	/* send negative pulse to make st95hf active */
	st95hf_send_st95enable_negativepulse(st95context);

	/* wait for 10 milisecond : HFO setup time */
	usleep_range(10000, 20000);

	return result;
}

static int st95hf_por_sequence(struct st95hf_context *st95context)
{
	int nth_attempt = 1;
	int result;

	st95hf_send_st95enable_negativepulse(st95context);

	usleep_range(5000, 6000);
	do {
		/* send an ECHO command and checks ST95HF response */
		result = st95hf_echo_command(st95context);

		dev_dbg(&st95context->spicontext.spidev->dev,
			"response from echo function = 0x%x, attempt = %d\n",
			result, nth_attempt);

		if (!result)
			return 0;

		/* send an pulse on IRQ in case of the chip is on sleep state */
		if (nth_attempt == 2)
			st95hf_send_st95enable_negativepulse(st95context);
		else
			st95hf_send_spi_reset_sequence(st95context);

		/* delay of 50 milisecond */
		usleep_range(50000, 51000);
	} while (nth_attempt++ < 3);

	return -ETIMEDOUT;
}

static int iso14443_config_fdt(struct st95hf_context *st95context, int wtxm)
{
	int result = 0;
	struct device *dev = &st95context->spicontext.spidev->dev;
	struct nfc_digital_dev *nfcddev = st95context->ddev;
	unsigned char pp_typeb;
	struct param_list new_params[2];

	pp_typeb = cmd_array[CMD_ISO14443B_PROTOCOL_SELECT].cmd_params[2];

	if (nfcddev->curr_protocol == NFC_PROTO_ISO14443 &&
	    st95context->fwi < 4)
		st95context->fwi = 4;

	new_params[0].param_offset = 2;
	if (nfcddev->curr_protocol == NFC_PROTO_ISO14443)
		new_params[0].new_param_val = st95context->fwi;
	else if (nfcddev->curr_protocol == NFC_PROTO_ISO14443_B)
		new_params[0].new_param_val = pp_typeb;

	new_params[1].param_offset = 3;
	new_params[1].new_param_val = wtxm;

	switch (nfcddev->curr_protocol) {
	case NFC_PROTO_ISO14443:
		result = st95hf_send_recv_cmd(st95context,
					      CMD_ISO14443A_PROTOCOL_SELECT,
					      2,
					      new_params,
					      true);
		if (result) {
			dev_err(dev, "WTX type a sel proto, err = 0x%x\n",
				result);
			return result;
		}

		/* secondary config. for 14443Type 4A after protocol select */
		result = secondary_configuration_type4a(st95context);
		if (result) {
			dev_err(dev, "WTX type a second. config, err = 0x%x\n",
				result);
			return result;
		}
		break;
	case NFC_PROTO_ISO14443_B:
		result = st95hf_send_recv_cmd(st95context,
					      CMD_ISO14443B_PROTOCOL_SELECT,
					      2,
					      new_params,
					      true);
		if (result) {
			dev_err(dev, "WTX type b sel proto, err = 0x%x\n",
				result);
			return result;
		}

		/* secondary config. for 14443Type 4B after protocol select */
		result = secondary_configuration_type4b(st95context);
		if (result) {
			dev_err(dev, "WTX type b second. config, err = 0x%x\n",
				result);
			return result;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int st95hf_handle_wtx(struct st95hf_context *stcontext,
			     bool new_wtx,
			     int wtx_val)
{
	int result = 0;
	unsigned char val_mm = 0;
	struct param_list new_params[1];
	struct nfc_digital_dev *nfcddev = stcontext->ddev;
	struct device *dev = &stcontext->nfcdev->dev;

	if (new_wtx) {
		result = iso14443_config_fdt(stcontext, wtx_val & 0x3f);
		if (result) {
			dev_err(dev, "Config. setting error on WTX req, err = 0x%x\n",
				result);
			return result;
		}

		/* Send response of wtx with ASYNC as no response expected */
		new_params[0].param_offset = 1;
		new_params[0].new_param_val = wtx_val;

		result = st95hf_send_recv_cmd(stcontext,
					      CMD_WTX_RESPONSE,
					      1,
					      new_params,
					      false);
		if (result)
			dev_err(dev, "WTX response send, err = 0x%x\n", result);
		return result;
	}

	/* if no new wtx, cofigure with default values */
	if (nfcddev->curr_protocol == NFC_PROTO_ISO14443)
		val_mm = cmd_array[CMD_ISO14443A_PROTOCOL_SELECT].cmd_params[3];
	else if (nfcddev->curr_protocol == NFC_PROTO_ISO14443_B)
		val_mm = cmd_array[CMD_ISO14443B_PROTOCOL_SELECT].cmd_params[3];

	result = iso14443_config_fdt(stcontext, val_mm);
	if (result)
		dev_err(dev, "Default config. setting error after WTX processing, err = 0x%x\n",
			result);

	return result;
}

static int st95hf_error_handling(struct st95hf_context *stcontext,
				 struct sk_buff *skb_resp,
				 int res_len)
{
	int result = 0;
	unsigned char error_byte;
	struct device *dev = &stcontext->nfcdev->dev;

	/* First check ST95HF specific error */
	if (skb_resp->data[0] & ST95HF_ERR_MASK) {
		if (skb_resp->data[0] == ST95HF_TIMEOUT_ERROR)
			result = -ETIMEDOUT;
		else
			result = -EIO;
	return  result;
	}

	/* Check for CRC err only if CRC is present in the tag response */
	switch (stcontext->current_rf_tech) {
	case NFC_DIGITAL_RF_TECH_106A:
		if (stcontext->sendrcv_trflag == TRFLAG_NFCA_STD_FRAME_CRC) {
			error_byte = skb_resp->data[res_len - 3];
			if (error_byte & ST95HF_NFCA_CRC_ERR_MASK) {
				/* CRC error occurred */
				dev_err(dev, "CRC error, byte received = 0x%x\n",
					error_byte);
				result = -EIO;
			}
		}
		break;
	case NFC_DIGITAL_RF_TECH_106B:
	case NFC_DIGITAL_RF_TECH_ISO15693:
		error_byte = skb_resp->data[res_len - 1];
		if (error_byte & ST95HF_NFCB_CRC_ERR_MASK) {
			/* CRC error occurred */
			dev_err(dev, "CRC error, byte received = 0x%x\n",
				error_byte);
			result = -EIO;
		}
		break;
	}

	return result;
}

static int st95hf_response_handler(struct st95hf_context *stcontext,
				   struct sk_buff *skb_resp,
				   int res_len)
{
	int result = 0;
	int skb_len;
	unsigned char val_mm;
	struct nfc_digital_dev *nfcddev = stcontext->ddev;
	struct device *dev = &stcontext->nfcdev->dev;
	struct st95_digital_cmd_complete_arg *cb_arg;

	cb_arg = &stcontext->complete_cb_arg;

	/* Process the response */
	skb_put(skb_resp, res_len);

	/* Remove st95 header */
	skb_pull(skb_resp, 2);

	skb_len = skb_resp->len;

	/* check if it is case of RATS request reply & FWI is present */
	if (nfcddev->curr_protocol == NFC_PROTO_ISO14443 && cb_arg->rats &&
	    (skb_resp->data[1] & RATS_TB1_PRESENT_MASK)) {
		if (skb_resp->data[1] & RATS_TA1_PRESENT_MASK)
			stcontext->fwi =
				(skb_resp->data[3] & TB1_FWI_MASK) >> 4;
		else
			stcontext->fwi =
				(skb_resp->data[2] & TB1_FWI_MASK) >> 4;

		val_mm = cmd_array[CMD_ISO14443A_PROTOCOL_SELECT].cmd_params[3];

		result = iso14443_config_fdt(stcontext, val_mm);
		if (result) {
			dev_err(dev, "error in config_fdt to handle fwi of ATS, error=%d\n",
				result);
			return result;
		}
	}
	cb_arg->rats = false;

	/* Remove CRC bytes only if received frames data has an eod (CRC) */
	switch (stcontext->current_rf_tech) {
	case NFC_DIGITAL_RF_TECH_106A:
		if (stcontext->sendrcv_trflag == TRFLAG_NFCA_STD_FRAME_CRC)
			skb_trim(skb_resp, (skb_len - 5));
		else
			skb_trim(skb_resp, (skb_len - 3));
		break;
	case NFC_DIGITAL_RF_TECH_106B:
	case NFC_DIGITAL_RF_TECH_ISO15693:
		skb_trim(skb_resp, (skb_len - 3));
		break;
	}

	return result;
}

static irqreturn_t st95hf_irq_handler(int irq, void  *st95hfcontext)
{
	struct st95hf_context *stcontext  =
		(struct st95hf_context *)st95hfcontext;

	if (stcontext->spicontext.req_issync) {
		complete(&stcontext->spicontext.done);
		stcontext->spicontext.req_issync = false;
		return IRQ_HANDLED;
	}

	return IRQ_WAKE_THREAD;
}

static irqreturn_t st95hf_irq_thread_handler(int irq, void  *st95hfcontext)
{
	int result = 0;
	int res_len;
	static bool wtx;
	struct device *spidevice;
	struct sk_buff *skb_resp;
	struct st95hf_context *stcontext  =
		(struct st95hf_context *)st95hfcontext;
	struct st95_digital_cmd_complete_arg *cb_arg;

	spidevice = &stcontext->spicontext.spidev->dev;

	/*
	 * check semaphore, if not down() already, then we don't
	 * know in which context the ISR is called and surely it
	 * will be a bug. Note that down() of the semaphore is done
	 * in the corresponding st95hf_in_send_cmd() and then
	 * only this ISR should be called. ISR will up() the
	 * semaphore before leaving. Hence when the ISR is called
	 * the correct behaviour is down_trylock() should always
	 * return 1 (indicating semaphore cant be taken and hence no
	 * change in semaphore count).
	 * If not, then we up() the semaphore and crash on
	 * a BUG() !
	 */
	if (!down_trylock(&stcontext->exchange_lock)) {
		up(&stcontext->exchange_lock);
		WARN(1, "unknown context in ST95HF ISR");
		return IRQ_NONE;
	}

	cb_arg = &stcontext->complete_cb_arg;
	skb_resp = cb_arg->skb_resp;

	mutex_lock(&stcontext->rm_lock);
	res_len = st95hf_spi_recv_response(&stcontext->spicontext,
					   skb_resp->data);
	if (res_len < 0) {
		dev_err(spidevice, "TISR spi response err = 0x%x\n", res_len);
		result = res_len;
		goto end;
	}

	/* if stcontext->nfcdev_free is true, it means remove already ran */
	if (stcontext->nfcdev_free) {
		result = -ENODEV;
		goto end;
	}

	if (skb_resp->data[2] == WTX_REQ_FROM_TAG) {
		/* Request for new FWT from tag */
		result = st95hf_handle_wtx(stcontext, true, skb_resp->data[3]);
		if (result)
			goto end;

		wtx = true;
		mutex_unlock(&stcontext->rm_lock);
		return IRQ_HANDLED;
	}

	result = st95hf_error_handling(stcontext, skb_resp, res_len);
	if (result)
		goto end;

	result = st95hf_response_handler(stcontext, skb_resp, res_len);
	if (result)
		goto end;

	/*
	 * If select protocol is done on wtx req. do select protocol
	 * again with default values
	 */
	if (wtx) {
		wtx = false;
		result = st95hf_handle_wtx(stcontext, false, 0);
		if (result)
			goto end;
	}

	/* call digital layer callback */
	cb_arg->complete_cb(stcontext->ddev, cb_arg->cb_usrarg, skb_resp);

	/* up the semaphore before returning */
	up(&stcontext->exchange_lock);
	mutex_unlock(&stcontext->rm_lock);

	return IRQ_HANDLED;

end:
	kfree_skb(skb_resp);
	wtx = false;
	cb_arg->rats = false;
	skb_resp = ERR_PTR(result);
	/* call of callback with error */
	cb_arg->complete_cb(stcontext->ddev, cb_arg->cb_usrarg, skb_resp);
	/* up the semaphore before returning */
	up(&stcontext->exchange_lock);
	mutex_unlock(&stcontext->rm_lock);
	return IRQ_HANDLED;
}

/* NFC ops functions definition */
static int st95hf_in_configure_hw(struct nfc_digital_dev *ddev,
				  int type,
				  int param)
{
	struct st95hf_context *stcontext = nfc_digital_get_drvdata(ddev);

	if (type == NFC_DIGITAL_CONFIG_RF_TECH)
		return st95hf_select_protocol(stcontext, param);

	if (type == NFC_DIGITAL_CONFIG_FRAMING) {
		switch (param) {
		case NFC_DIGITAL_FRAMING_NFCA_SHORT:
			stcontext->sendrcv_trflag = TRFLAG_NFCA_SHORT_FRAME;
			break;
		case NFC_DIGITAL_FRAMING_NFCA_STANDARD:
			stcontext->sendrcv_trflag = TRFLAG_NFCA_STD_FRAME;
			break;
		case NFC_DIGITAL_FRAMING_NFCA_T4T:
		case NFC_DIGITAL_FRAMING_NFCA_NFC_DEP:
		case NFC_DIGITAL_FRAMING_NFCA_STANDARD_WITH_CRC_A:
			stcontext->sendrcv_trflag = TRFLAG_NFCA_STD_FRAME_CRC;
			break;
		case NFC_DIGITAL_FRAMING_NFCB:
		case NFC_DIGITAL_FRAMING_ISO15693_INVENTORY:
		case NFC_DIGITAL_FRAMING_ISO15693_T5T:
			break;
		}
	}

	return 0;
}

static int rf_off(struct st95hf_context *stcontext)
{
	int rc;
	struct device *dev;

	dev = &stcontext->nfcdev->dev;

	rc = st95hf_send_recv_cmd(stcontext, CMD_FIELD_OFF, 0, NULL, true);
	if (rc)
		dev_err(dev, "protocol sel send field off, err = 0x%x\n", rc);

	return rc;
}

static int st95hf_in_send_cmd(struct nfc_digital_dev *ddev,
			      struct sk_buff *skb,
			      u16 timeout,
			      nfc_digital_cmd_complete_t cb,
			      void *arg)
{
	struct st95hf_context *stcontext = nfc_digital_get_drvdata(ddev);
	int rc;
	struct sk_buff *skb_resp;
	int len_data_to_tag = 0;

	skb_resp = nfc_alloc_recv_skb(MAX_RESPONSE_BUFFER_SIZE, GFP_KERNEL);
	if (!skb_resp) {
		rc = -ENOMEM;
		goto error;
	}

	switch (stcontext->current_rf_tech) {
	case NFC_DIGITAL_RF_TECH_106A:
		len_data_to_tag = skb->len + 1;
		skb_put_u8(skb, stcontext->sendrcv_trflag);
		break;
	case NFC_DIGITAL_RF_TECH_106B:
	case NFC_DIGITAL_RF_TECH_ISO15693:
		len_data_to_tag = skb->len;
		break;
	default:
		rc = -EINVAL;
		goto free_skb_resp;
	}

	skb_push(skb, 3);
	skb->data[0] = ST95HF_COMMAND_SEND;
	skb->data[1] = SEND_RECEIVE_CMD;
	skb->data[2] = len_data_to_tag;

	stcontext->complete_cb_arg.skb_resp = skb_resp;
	stcontext->complete_cb_arg.cb_usrarg = arg;
	stcontext->complete_cb_arg.complete_cb = cb;

	if ((skb->data[3] == ISO14443A_RATS_REQ) &&
	    ddev->curr_protocol == NFC_PROTO_ISO14443)
		stcontext->complete_cb_arg.rats = true;

	/*
	 * down the semaphore to indicate to remove func that an
	 * ISR is pending, note that it will not block here in any case.
	 * If found blocked, it is a BUG!
	 */
	rc = down_killable(&stcontext->exchange_lock);
	if (rc) {
		WARN(1, "Semaphore is not found up in st95hf_in_send_cmd\n");
		return rc;
	}

	rc = st95hf_spi_send(&stcontext->spicontext, skb->data,
			     skb->len,
			     ASYNC);
	if (rc) {
		dev_err(&stcontext->nfcdev->dev,
			"Error %d trying to perform data_exchange", rc);
		/* up the semaphore since ISR will never come in this case */
		up(&stcontext->exchange_lock);
		goto free_skb_resp;
	}

	kfree_skb(skb);

	return rc;

free_skb_resp:
	kfree_skb(skb_resp);
error:
	return rc;
}

/* p2p will be supported in a later release ! */
static int st95hf_tg_configure_hw(struct nfc_digital_dev *ddev,
				  int type,
				  int param)
{
	return 0;
}

static int st95hf_tg_send_cmd(struct nfc_digital_dev *ddev,
			      struct sk_buff *skb,
			      u16 timeout,
			      nfc_digital_cmd_complete_t cb,
			      void *arg)
{
	return 0;
}

static int st95hf_tg_listen(struct nfc_digital_dev *ddev,
			    u16 timeout,
			    nfc_digital_cmd_complete_t cb,
			    void *arg)
{
	return 0;
}

static int st95hf_tg_get_rf_tech(struct nfc_digital_dev *ddev, u8 *rf_tech)
{
	return 0;
}

static int st95hf_switch_rf(struct nfc_digital_dev *ddev, bool on)
{
	u8 rf_tech;
	struct st95hf_context *stcontext = nfc_digital_get_drvdata(ddev);

	rf_tech = ddev->curr_rf_tech;

	if (on)
		/* switch on RF field */
		return st95hf_select_protocol(stcontext, rf_tech);

	/* switch OFF RF field */
	return rf_off(stcontext);
}

/* TODO st95hf_abort_cmd */
static void st95hf_abort_cmd(struct nfc_digital_dev *ddev)
{
}

static struct nfc_digital_ops st95hf_nfc_digital_ops = {
	.in_configure_hw = st95hf_in_configure_hw,
	.in_send_cmd = st95hf_in_send_cmd,

	.tg_listen = st95hf_tg_listen,
	.tg_configure_hw = st95hf_tg_configure_hw,
	.tg_send_cmd = st95hf_tg_send_cmd,
	.tg_get_rf_tech = st95hf_tg_get_rf_tech,

	.switch_rf = st95hf_switch_rf,
	.abort_cmd = st95hf_abort_cmd,
};

static const struct spi_device_id st95hf_id[] = {
	{ "st95hf", 0 },
	{}
};
MODULE_DEVICE_TABLE(spi, st95hf_id);

static const struct of_device_id st95hf_spi_of_match[] = {
        { .compatible = "st,st95hf" },
        { },
};
MODULE_DEVICE_TABLE(of, st95hf_spi_of_match);

static int st95hf_probe(struct spi_device *nfc_spi_dev)
{
	int ret;

	struct st95hf_context *st95context;
	struct st95hf_spi_context *spicontext;

	nfc_info(&nfc_spi_dev->dev, "ST95HF driver probe called.\n");

	st95context = devm_kzalloc(&nfc_spi_dev->dev,
				   sizeof(struct st95hf_context),
				   GFP_KERNEL);
	if (!st95context)
		return -ENOMEM;

	spicontext = &st95context->spicontext;

	spicontext->spidev = nfc_spi_dev;

	st95context->fwi =
		cmd_array[CMD_ISO14443A_PROTOCOL_SELECT].cmd_params[2];

	if (device_property_present(&nfc_spi_dev->dev, "st95hfvin")) {
		st95context->st95hf_supply =
			devm_regulator_get(&nfc_spi_dev->dev,
					   "st95hfvin");
		if (IS_ERR(st95context->st95hf_supply)) {
			dev_err(&nfc_spi_dev->dev, "failed to acquire regulator\n");
			return PTR_ERR(st95context->st95hf_supply);
		}

		ret = regulator_enable(st95context->st95hf_supply);
		if (ret) {
			dev_err(&nfc_spi_dev->dev, "failed to enable regulator\n");
			return ret;
		}
	}

	init_completion(&spicontext->done);
	mutex_init(&spicontext->spi_lock);

	/*
	 * Store spicontext in spi device object for using it in
	 * remove function
	 */
	dev_set_drvdata(&nfc_spi_dev->dev, spicontext);

	st95context->enable_gpio =
		of_get_named_gpio(nfc_spi_dev->dev.of_node,
				  "enable-gpio",
				  0);
	if (!gpio_is_valid(st95context->enable_gpio)) {
		dev_err(&nfc_spi_dev->dev, "No valid enable gpio\n");
		ret = st95context->enable_gpio;
		goto err_disable_regulator;
	}

	ret = devm_gpio_request_one(&nfc_spi_dev->dev, st95context->enable_gpio,
				    GPIOF_DIR_OUT | GPIOF_INIT_HIGH,
				    "enable_gpio");
	if (ret)
		goto err_disable_regulator;

	if (nfc_spi_dev->irq > 0) {
		if (devm_request_threaded_irq(&nfc_spi_dev->dev,
					      nfc_spi_dev->irq,
					      st95hf_irq_handler,
					      st95hf_irq_thread_handler,
					      IRQF_TRIGGER_FALLING,
					      "st95hf",
					      (void *)st95context) < 0) {
			dev_err(&nfc_spi_dev->dev, "err: irq request for st95hf is failed\n");
			ret =  -EINVAL;
			goto err_disable_regulator;
		}
	} else {
		dev_err(&nfc_spi_dev->dev, "not a valid IRQ associated with ST95HF\n");
		ret = -EINVAL;
		goto err_disable_regulator;
	}

	/*
	 * First reset SPI to handle warm reset of the system.
	 * It will put the ST95HF device in Power ON state
	 * which make the state of device identical to state
	 * at the time of cold reset of the system.
	 */
	ret = st95hf_send_spi_reset_sequence(st95context);
	if (ret) {
		dev_err(&nfc_spi_dev->dev, "err: spi_reset_sequence failed\n");
		goto err_disable_regulator;
	}

	/* call PowerOnReset sequence of ST95hf to activate it */
	ret = st95hf_por_sequence(st95context);
	if (ret) {
		dev_err(&nfc_spi_dev->dev, "err: por seq failed for st95hf\n");
		goto err_disable_regulator;
	}

	/* create NFC dev object and register with NFC Subsystem */
	st95context->ddev = nfc_digital_allocate_device(&st95hf_nfc_digital_ops,
							ST95HF_SUPPORTED_PROT,
							ST95HF_CAPABILITIES,
							ST95HF_HEADROOM_LEN,
							ST95HF_TAILROOM_LEN);
	if (!st95context->ddev) {
		ret = -ENOMEM;
		goto err_disable_regulator;
	}

	st95context->nfcdev = st95context->ddev->nfc_dev;
	nfc_digital_set_parent_dev(st95context->ddev, &nfc_spi_dev->dev);

	ret =  nfc_digital_register_device(st95context->ddev);
	if (ret) {
		dev_err(&st95context->nfcdev->dev, "st95hf registration failed\n");
		goto err_free_digital_device;
	}

	/* store st95context in nfc device object */
	nfc_digital_set_drvdata(st95context->ddev, st95context);

	sema_init(&st95context->exchange_lock, 1);
	mutex_init(&st95context->rm_lock);

	return ret;

err_free_digital_device:
	nfc_digital_free_device(st95context->ddev);
err_disable_regulator:
	if (st95context->st95hf_supply)
		regulator_disable(st95context->st95hf_supply);

	return ret;
}

static int st95hf_remove(struct spi_device *nfc_spi_dev)
{
	int result = 0;
	unsigned char reset_cmd = ST95HF_COMMAND_RESET;
	struct st95hf_spi_context *spictx = dev_get_drvdata(&nfc_spi_dev->dev);

	struct st95hf_context *stcontext = container_of(spictx,
							struct st95hf_context,
							spicontext);

	mutex_lock(&stcontext->rm_lock);

	nfc_digital_unregister_device(stcontext->ddev);
	nfc_digital_free_device(stcontext->ddev);
	stcontext->nfcdev_free = true;

	mutex_unlock(&stcontext->rm_lock);

	/* if last in_send_cmd's ISR is pending, wait for it to finish */
	result = down_killable(&stcontext->exchange_lock);
	if (result == -EINTR)
		dev_err(&spictx->spidev->dev, "sleep for semaphore interrupted by signal\n");

	/* next reset the ST95HF controller */
	result = st95hf_spi_send(&stcontext->spicontext,
				 &reset_cmd,
				 ST95HF_RESET_CMD_LEN,
				 ASYNC);
	if (result) {
		dev_err(&spictx->spidev->dev,
			"ST95HF reset failed in remove() err = %d\n", result);
		return result;
	}

	/* wait for 3 ms to complete the controller reset process */
	usleep_range(3000, 4000);

	/* disable regulator */
	if (stcontext->st95hf_supply)
		regulator_disable(stcontext->st95hf_supply);

	return result;
}

/* Register as SPI protocol driver */
static struct spi_driver st95hf_driver = {
	.driver = {
		.name = "st95hf",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(st95hf_spi_of_match),
	},
	.id_table = st95hf_id,
	.probe = st95hf_probe,
	.remove = st95hf_remove,
};

module_spi_driver(st95hf_driver);

MODULE_AUTHOR("Shikha Singh <shikha.singh@st.com>");
MODULE_DESCRIPTION("ST NFC Transceiver ST95HF driver");
MODULE_LICENSE("GPL v2");
