// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/sched/types.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <media/cec-pin.h>
#include "cec-pin-priv.h"

struct cec_error_inj_cmd {
	unsigned int mode_offset;
	int arg_idx;
	const char *cmd;
};

static const struct cec_error_inj_cmd cec_error_inj_cmds[] = {
	{ CEC_ERROR_INJ_RX_NACK_OFFSET, -1, "rx-nack" },
	{ CEC_ERROR_INJ_RX_LOW_DRIVE_OFFSET,
	  CEC_ERROR_INJ_RX_LOW_DRIVE_ARG_IDX, "rx-low-drive" },
	{ CEC_ERROR_INJ_RX_ADD_BYTE_OFFSET, -1, "rx-add-byte" },
	{ CEC_ERROR_INJ_RX_REMOVE_BYTE_OFFSET, -1, "rx-remove-byte" },
	{ CEC_ERROR_INJ_RX_ARB_LOST_OFFSET,
	  CEC_ERROR_INJ_RX_ARB_LOST_ARG_IDX, "rx-arb-lost" },

	{ CEC_ERROR_INJ_TX_NO_EOM_OFFSET, -1, "tx-no-eom" },
	{ CEC_ERROR_INJ_TX_EARLY_EOM_OFFSET, -1, "tx-early-eom" },
	{ CEC_ERROR_INJ_TX_ADD_BYTES_OFFSET,
	  CEC_ERROR_INJ_TX_ADD_BYTES_ARG_IDX, "tx-add-bytes" },
	{ CEC_ERROR_INJ_TX_REMOVE_BYTE_OFFSET, -1, "tx-remove-byte" },
	{ CEC_ERROR_INJ_TX_SHORT_BIT_OFFSET,
	  CEC_ERROR_INJ_TX_SHORT_BIT_ARG_IDX, "tx-short-bit" },
	{ CEC_ERROR_INJ_TX_LONG_BIT_OFFSET,
	  CEC_ERROR_INJ_TX_LONG_BIT_ARG_IDX, "tx-long-bit" },
	{ CEC_ERROR_INJ_TX_CUSTOM_BIT_OFFSET,
	  CEC_ERROR_INJ_TX_CUSTOM_BIT_ARG_IDX, "tx-custom-bit" },
	{ CEC_ERROR_INJ_TX_SHORT_START_OFFSET, -1, "tx-short-start" },
	{ CEC_ERROR_INJ_TX_LONG_START_OFFSET, -1, "tx-long-start" },
	{ CEC_ERROR_INJ_TX_CUSTOM_START_OFFSET, -1, "tx-custom-start" },
	{ CEC_ERROR_INJ_TX_LAST_BIT_OFFSET,
	  CEC_ERROR_INJ_TX_LAST_BIT_ARG_IDX, "tx-last-bit" },
	{ CEC_ERROR_INJ_TX_LOW_DRIVE_OFFSET,
	  CEC_ERROR_INJ_TX_LOW_DRIVE_ARG_IDX, "tx-low-drive" },
	{ 0, -1, NULL }
};

u16 cec_pin_rx_error_inj(struct cec_pin *pin)
{
	u16 cmd = CEC_ERROR_INJ_OP_ANY;

	/* Only when 18 bits have been received do we have a valid cmd */
	if (!(pin->error_inj[cmd] & CEC_ERROR_INJ_RX_MASK) &&
	    pin->rx_bit >= 18)
		cmd = pin->rx_msg.msg[1];
	return (pin->error_inj[cmd] & CEC_ERROR_INJ_RX_MASK) ? cmd :
		CEC_ERROR_INJ_OP_ANY;
}

u16 cec_pin_tx_error_inj(struct cec_pin *pin)
{
	u16 cmd = CEC_ERROR_INJ_OP_ANY;

	if (!(pin->error_inj[cmd] & CEC_ERROR_INJ_TX_MASK) &&
	    pin->tx_msg.len > 1)
		cmd = pin->tx_msg.msg[1];
	return (pin->error_inj[cmd] & CEC_ERROR_INJ_TX_MASK) ? cmd :
		CEC_ERROR_INJ_OP_ANY;
}

bool cec_pin_error_inj_parse_line(struct cec_adapter *adap, char *line)
{
	static const char *delims = " \t\r";
	struct cec_pin *pin = adap->pin;
	unsigned int i;
	bool has_pos = false;
	char *p = line;
	char *token;
	char *comma;
	u64 *error;
	u8 *args;
	bool has_op;
	u8 op;
	u8 mode;
	u8 pos;

	p = skip_spaces(p);
	token = strsep(&p, delims);
	if (!strcmp(token, "clear")) {
		memset(pin->error_inj, 0, sizeof(pin->error_inj));
		pin->rx_toggle = pin->tx_toggle = false;
		pin->tx_ignore_nack_until_eom = false;
		pin->tx_custom_pulse = false;
		pin->tx_custom_low_usecs = CEC_TIM_CUSTOM_DEFAULT;
		pin->tx_custom_high_usecs = CEC_TIM_CUSTOM_DEFAULT;
		return true;
	}
	if (!strcmp(token, "rx-clear")) {
		for (i = 0; i <= CEC_ERROR_INJ_OP_ANY; i++)
			pin->error_inj[i] &= ~CEC_ERROR_INJ_RX_MASK;
		pin->rx_toggle = false;
		return true;
	}
	if (!strcmp(token, "tx-clear")) {
		for (i = 0; i <= CEC_ERROR_INJ_OP_ANY; i++)
			pin->error_inj[i] &= ~CEC_ERROR_INJ_TX_MASK;
		pin->tx_toggle = false;
		pin->tx_ignore_nack_until_eom = false;
		pin->tx_custom_pulse = false;
		pin->tx_custom_low_usecs = CEC_TIM_CUSTOM_DEFAULT;
		pin->tx_custom_high_usecs = CEC_TIM_CUSTOM_DEFAULT;
		return true;
	}
	if (!strcmp(token, "tx-ignore-nack-until-eom")) {
		pin->tx_ignore_nack_until_eom = true;
		return true;
	}
	if (!strcmp(token, "tx-custom-pulse")) {
		pin->tx_custom_pulse = true;
		cec_pin_start_timer(pin);
		return true;
	}
	if (!p)
		return false;

	p = skip_spaces(p);
	if (!strcmp(token, "tx-custom-low-usecs")) {
		u32 usecs;

		if (kstrtou32(p, 0, &usecs) || usecs > 10000000)
			return false;
		pin->tx_custom_low_usecs = usecs;
		return true;
	}
	if (!strcmp(token, "tx-custom-high-usecs")) {
		u32 usecs;

		if (kstrtou32(p, 0, &usecs) || usecs > 10000000)
			return false;
		pin->tx_custom_high_usecs = usecs;
		return true;
	}

	comma = strchr(token, ',');
	if (comma)
		*comma++ = '\0';
	if (!strcmp(token, "any")) {
		has_op = false;
		error = pin->error_inj + CEC_ERROR_INJ_OP_ANY;
		args = pin->error_inj_args[CEC_ERROR_INJ_OP_ANY];
	} else if (!kstrtou8(token, 0, &op)) {
		has_op = true;
		error = pin->error_inj + op;
		args = pin->error_inj_args[op];
	} else {
		return false;
	}

	mode = CEC_ERROR_INJ_MODE_ONCE;
	if (comma) {
		if (!strcmp(comma, "off"))
			mode = CEC_ERROR_INJ_MODE_OFF;
		else if (!strcmp(comma, "once"))
			mode = CEC_ERROR_INJ_MODE_ONCE;
		else if (!strcmp(comma, "always"))
			mode = CEC_ERROR_INJ_MODE_ALWAYS;
		else if (!strcmp(comma, "toggle"))
			mode = CEC_ERROR_INJ_MODE_TOGGLE;
		else
			return false;
	}

	token = strsep(&p, delims);
	if (p) {
		p = skip_spaces(p);
		has_pos = !kstrtou8(p, 0, &pos);
	}

	if (!strcmp(token, "clear")) {
		*error = 0;
		return true;
	}
	if (!strcmp(token, "rx-clear")) {
		*error &= ~CEC_ERROR_INJ_RX_MASK;
		return true;
	}
	if (!strcmp(token, "tx-clear")) {
		*error &= ~CEC_ERROR_INJ_TX_MASK;
		return true;
	}

	for (i = 0; cec_error_inj_cmds[i].cmd; i++) {
		const char *cmd = cec_error_inj_cmds[i].cmd;
		unsigned int mode_offset;
		u64 mode_mask;
		int arg_idx;
		bool is_bit_pos = true;

		if (strcmp(token, cmd))
			continue;

		mode_offset = cec_error_inj_cmds[i].mode_offset;
		mode_mask = CEC_ERROR_INJ_MODE_MASK << mode_offset;
		arg_idx = cec_error_inj_cmds[i].arg_idx;

		if (mode_offset == CEC_ERROR_INJ_RX_ARB_LOST_OFFSET) {
			if (has_op)
				return false;
			if (!has_pos)
				pos = 0x0f;
			is_bit_pos = false;
		} else if (mode_offset == CEC_ERROR_INJ_TX_ADD_BYTES_OFFSET) {
			if (!has_pos || !pos)
				return false;
			is_bit_pos = false;
		}

		if (arg_idx >= 0 && is_bit_pos) {
			if (!has_pos || pos >= 160)
				return false;
			if (has_op && pos < 10 + 8)
				return false;
			/* Invalid bit position may not be the Ack bit */
			if ((mode_offset == CEC_ERROR_INJ_TX_SHORT_BIT_OFFSET ||
			     mode_offset == CEC_ERROR_INJ_TX_LONG_BIT_OFFSET ||
			     mode_offset == CEC_ERROR_INJ_TX_CUSTOM_BIT_OFFSET) &&
			    (pos % 10) == 9)
				return false;
		}
		*error &= ~mode_mask;
		*error |= (u64)mode << mode_offset;
		if (arg_idx >= 0)
			args[arg_idx] = pos;
		return true;
	}
	return false;
}

static void cec_pin_show_cmd(struct seq_file *sf, u32 cmd, u8 mode)
{
	if (cmd == CEC_ERROR_INJ_OP_ANY)
		seq_puts(sf, "any,");
	else
		seq_printf(sf, "0x%02x,", cmd);
	switch (mode) {
	case CEC_ERROR_INJ_MODE_ONCE:
		seq_puts(sf, "once ");
		break;
	case CEC_ERROR_INJ_MODE_ALWAYS:
		seq_puts(sf, "always ");
		break;
	case CEC_ERROR_INJ_MODE_TOGGLE:
		seq_puts(sf, "toggle ");
		break;
	default:
		seq_puts(sf, "off ");
		break;
	}
}

int cec_pin_error_inj_show(struct cec_adapter *adap, struct seq_file *sf)
{
	struct cec_pin *pin = adap->pin;
	unsigned int i, j;

	seq_puts(sf, "# Clear error injections:\n");
	seq_puts(sf, "#   clear          clear all rx and tx error injections\n");
	seq_puts(sf, "#   rx-clear       clear all rx error injections\n");
	seq_puts(sf, "#   tx-clear       clear all tx error injections\n");
	seq_puts(sf, "#   <op> clear     clear all rx and tx error injections for <op>\n");
	seq_puts(sf, "#   <op> rx-clear  clear all rx error injections for <op>\n");
	seq_puts(sf, "#   <op> tx-clear  clear all tx error injections for <op>\n");
	seq_puts(sf, "#\n");
	seq_puts(sf, "# RX error injection:\n");
	seq_puts(sf, "#   <op>[,<mode>] rx-nack              NACK the message instead of sending an ACK\n");
	seq_puts(sf, "#   <op>[,<mode>] rx-low-drive <bit>   force a low-drive condition at this bit position\n");
	seq_puts(sf, "#   <op>[,<mode>] rx-add-byte          add a spurious byte to the received CEC message\n");
	seq_puts(sf, "#   <op>[,<mode>] rx-remove-byte       remove the last byte from the received CEC message\n");
	seq_puts(sf, "#    any[,<mode>] rx-arb-lost [<poll>] generate a POLL message to trigger an arbitration lost\n");
	seq_puts(sf, "#\n");
	seq_puts(sf, "# TX error injection settings:\n");
	seq_puts(sf, "#   tx-ignore-nack-until-eom           ignore early NACKs until EOM\n");
	seq_puts(sf, "#   tx-custom-low-usecs <usecs>        define the 'low' time for the custom pulse\n");
	seq_puts(sf, "#   tx-custom-high-usecs <usecs>       define the 'high' time for the custom pulse\n");
	seq_puts(sf, "#   tx-custom-pulse                    transmit the custom pulse once the bus is idle\n");
	seq_puts(sf, "#\n");
	seq_puts(sf, "# TX error injection:\n");
	seq_puts(sf, "#   <op>[,<mode>] tx-no-eom            don't set the EOM bit\n");
	seq_puts(sf, "#   <op>[,<mode>] tx-early-eom         set the EOM bit one byte too soon\n");
	seq_puts(sf, "#   <op>[,<mode>] tx-add-bytes <num>   append <num> (1-255) spurious bytes to the message\n");
	seq_puts(sf, "#   <op>[,<mode>] tx-remove-byte       drop the last byte from the message\n");
	seq_puts(sf, "#   <op>[,<mode>] tx-short-bit <bit>   make this bit shorter than allowed\n");
	seq_puts(sf, "#   <op>[,<mode>] tx-long-bit <bit>    make this bit longer than allowed\n");
	seq_puts(sf, "#   <op>[,<mode>] tx-custom-bit <bit>  send the custom pulse instead of this bit\n");
	seq_puts(sf, "#   <op>[,<mode>] tx-short-start       send a start pulse that's too short\n");
	seq_puts(sf, "#   <op>[,<mode>] tx-long-start        send a start pulse that's too long\n");
	seq_puts(sf, "#   <op>[,<mode>] tx-custom-start      send the custom pulse instead of the start pulse\n");
	seq_puts(sf, "#   <op>[,<mode>] tx-last-bit <bit>    stop sending after this bit\n");
	seq_puts(sf, "#   <op>[,<mode>] tx-low-drive <bit>   force a low-drive condition at this bit position\n");
	seq_puts(sf, "#\n");
	seq_puts(sf, "# <op>       CEC message opcode (0-255) or 'any'\n");
	seq_puts(sf, "# <mode>     'once' (default), 'always', 'toggle' or 'off'\n");
	seq_puts(sf, "# <bit>      CEC message bit (0-159)\n");
	seq_puts(sf, "#            10 bits per 'byte': bits 0-7: data, bit 8: EOM, bit 9: ACK\n");
	seq_puts(sf, "# <poll>     CEC poll message used to test arbitration lost (0x00-0xff, default 0x0f)\n");
	seq_puts(sf, "# <usecs>    microseconds (0-10000000, default 1000)\n");

	seq_puts(sf, "\nclear\n");

	for (i = 0; i < ARRAY_SIZE(pin->error_inj); i++) {
		u64 e = pin->error_inj[i];

		for (j = 0; cec_error_inj_cmds[j].cmd; j++) {
			const char *cmd = cec_error_inj_cmds[j].cmd;
			unsigned int mode;
			unsigned int mode_offset;
			int arg_idx;

			mode_offset = cec_error_inj_cmds[j].mode_offset;
			arg_idx = cec_error_inj_cmds[j].arg_idx;
			mode = (e >> mode_offset) & CEC_ERROR_INJ_MODE_MASK;
			if (!mode)
				continue;
			cec_pin_show_cmd(sf, i, mode);
			seq_puts(sf, cmd);
			if (arg_idx >= 0)
				seq_printf(sf, " %u",
					   pin->error_inj_args[i][arg_idx]);
			seq_puts(sf, "\n");
		}
	}

	if (pin->tx_ignore_nack_until_eom)
		seq_puts(sf, "tx-ignore-nack-until-eom\n");
	if (pin->tx_custom_pulse)
		seq_puts(sf, "tx-custom-pulse\n");
	if (pin->tx_custom_low_usecs != CEC_TIM_CUSTOM_DEFAULT)
		seq_printf(sf, "tx-custom-low-usecs %u\n",
			   pin->tx_custom_low_usecs);
	if (pin->tx_custom_high_usecs != CEC_TIM_CUSTOM_DEFAULT)
		seq_printf(sf, "tx-custom-high-usecs %u\n",
			   pin->tx_custom_high_usecs);
	return 0;
}
