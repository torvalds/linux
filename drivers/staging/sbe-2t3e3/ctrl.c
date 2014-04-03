/*
 * SBE 2T3E3 synchronous serial card driver for Linux
 *
 * Copyright (C) 2009-2010 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This code is based on a driver written by SBE Inc.
 */

#include <linux/types.h>
#include "2t3e3.h"
#include "ctrl.h"

void t3e3_set_frame_type(struct channel *sc, u32 mode)
{
	if (sc->p.frame_type == mode)
		return;

	if (sc->r.flags & SBE_2T3E3_FLAG_NETWORK_UP) {
		dev_err(&sc->pdev->dev, "SBE 2T3E3: changing frame type during active connection\n");
		return;
	}

	exar7300_set_frame_type(sc, mode);
	exar7250_set_frame_type(sc, mode);
	cpld_set_frame_type(sc, mode);

	sc->p.frame_type = mode;
}

static void t3e3_set_loopback(struct channel *sc, u32 mode)
{
	u32 tx, rx;

	if (sc->p.loopback == mode)
		return;

	tx = sc->p.transmitter_on;
	rx = sc->p.receiver_on;
	if (tx == SBE_2T3E3_ON)
		dc_transmitter_onoff(sc, SBE_2T3E3_OFF);
	if (rx == SBE_2T3E3_ON)
		dc_receiver_onoff(sc, SBE_2T3E3_OFF);

	/* stop current loopback if any exists */
	switch (sc->p.loopback) {
	case SBE_2T3E3_LOOPBACK_NONE:
		break;
	case SBE_2T3E3_LOOPBACK_ETHERNET:
		dc_set_loopback(sc, SBE_2T3E3_21143_VAL_LOOPBACK_OFF);
		break;
	case SBE_2T3E3_LOOPBACK_FRAMER:
		exar7250_set_loopback(sc, SBE_2T3E3_FRAMER_VAL_LOOPBACK_OFF);
		break;
	case SBE_2T3E3_LOOPBACK_LIU_DIGITAL:
	case SBE_2T3E3_LOOPBACK_LIU_ANALOG:
	case SBE_2T3E3_LOOPBACK_LIU_REMOTE:
		exar7300_set_loopback(sc, SBE_2T3E3_LIU_VAL_LOOPBACK_OFF);
		break;
	default:
		return;
	}

	switch (mode) {
	case SBE_2T3E3_LOOPBACK_NONE:
		break;
	case SBE_2T3E3_LOOPBACK_ETHERNET:
		dc_set_loopback(sc, SBE_2T3E3_21143_VAL_LOOPBACK_INTERNAL);
		break;
	case SBE_2T3E3_LOOPBACK_FRAMER:
		exar7250_set_loopback(sc, SBE_2T3E3_FRAMER_VAL_LOOPBACK_ON);
		break;
	case SBE_2T3E3_LOOPBACK_LIU_DIGITAL:
		exar7300_set_loopback(sc, SBE_2T3E3_LIU_VAL_LOOPBACK_DIGITAL);
		break;
	case SBE_2T3E3_LOOPBACK_LIU_ANALOG:
		exar7300_set_loopback(sc, SBE_2T3E3_LIU_VAL_LOOPBACK_ANALOG);
		break;
	case SBE_2T3E3_LOOPBACK_LIU_REMOTE:
		exar7300_set_loopback(sc, SBE_2T3E3_LIU_VAL_LOOPBACK_REMOTE);
		break;
	default:
		return;
	}

	sc->p.loopback = mode;

	if (tx == SBE_2T3E3_ON)
		dc_transmitter_onoff(sc, SBE_2T3E3_ON);
	if (rx == SBE_2T3E3_ON)
		dc_receiver_onoff(sc, SBE_2T3E3_ON);
}


static void t3e3_reg_read(struct channel *sc, u32 *reg, u32 *val)
{
	u32 i;

	*val = 0;

	switch (reg[0]) {
	case SBE_2T3E3_CHIP_21143:
		if (!(reg[1] & 7))
			*val = dc_read(sc->addr, reg[1] / 8);
		break;
	case SBE_2T3E3_CHIP_CPLD:
		for (i = 0; i < SBE_2T3E3_CPLD_REG_MAX; i++)
			if (cpld_reg_map[i][sc->h.slot] == reg[1]) {
				*val = cpld_read(sc, i);
				break;
			}
		break;
	case SBE_2T3E3_CHIP_FRAMER:
		for (i = 0; i < SBE_2T3E3_FRAMER_REG_MAX; i++)
			if (t3e3_framer_reg_map[i] == reg[1]) {
				*val = exar7250_read(sc, i);
				break;
			}
		break;
	case SBE_2T3E3_CHIP_LIU:
		for (i = 0; i < SBE_2T3E3_LIU_REG_MAX; i++)
			if (t3e3_liu_reg_map[i] == reg[1]) {
				*val = exar7300_read(sc, i);
				break;
			}
		break;
	default:
		break;
	}
}

static void t3e3_reg_write(struct channel *sc, u32 *reg)
{
	u32 i;

	switch (reg[0]) {
	case SBE_2T3E3_CHIP_21143:
		dc_write(sc->addr, reg[1], reg[2]);
		break;
	case SBE_2T3E3_CHIP_CPLD:
		for (i = 0; i < SBE_2T3E3_CPLD_REG_MAX; i++)
			if (cpld_reg_map[i][sc->h.slot] == reg[1]) {
				cpld_write(sc, i, reg[2]);
				break;
			}
		break;
	case SBE_2T3E3_CHIP_FRAMER:
		for (i = 0; i < SBE_2T3E3_FRAMER_REG_MAX; i++)
			if (t3e3_framer_reg_map[i] == reg[1]) {
				exar7250_write(sc, i, reg[2]);
				break;
			}
		break;
	case SBE_2T3E3_CHIP_LIU:
		for (i = 0; i < SBE_2T3E3_LIU_REG_MAX; i++)
			if (t3e3_liu_reg_map[i] == reg[1]) {
				exar7300_write(sc, i, reg[2]);
				break;
			}
		break;
	}
}

static void t3e3_port_get(struct channel *sc, t3e3_param_t *param)
{
	memcpy(param, &(sc->p), sizeof(t3e3_param_t));
}

static void t3e3_port_set(struct channel *sc, t3e3_param_t *param)
{
	if (param->frame_mode != 0xff)
		cpld_set_frame_mode(sc, param->frame_mode);

	if (param->fractional_mode != 0xff)
		cpld_set_fractional_mode(sc, param->fractional_mode,
					 param->bandwidth_start,
					 param->bandwidth_stop);

	if (param->pad_count != 0xff)
		cpld_set_pad_count(sc, param->pad_count);

	if (param->crc != 0xff)
		cpld_set_crc(sc, param->crc);

	if (param->receiver_on != 0xff)
		dc_receiver_onoff(sc, param->receiver_on);

	if (param->transmitter_on != 0xff)
		dc_transmitter_onoff(sc, param->transmitter_on);

	if (param->frame_type != 0xff)
		t3e3_set_frame_type(sc, param->frame_type);

	if (param->panel != 0xff)
		cpld_select_panel(sc, param->panel);

	if (param->line_build_out != 0xff)
		exar7300_line_build_out_onoff(sc, param->line_build_out);

	if (param->receive_equalization != 0xff)
		exar7300_receive_equalization_onoff(sc, param->receive_equalization);

	if (param->transmit_all_ones != 0xff)
		exar7300_transmit_all_ones_onoff(sc, param->transmit_all_ones);

	if (param->loopback != 0xff)
		t3e3_set_loopback(sc, param->loopback);

	if (param->clock_source != 0xff)
		cpld_set_clock(sc, param->clock_source);

	if (param->scrambler != 0xff)
		cpld_set_scrambler(sc, param->scrambler);
}

static void t3e3_port_get_stats(struct channel *sc,
			 t3e3_stats_t *stats)
{
	u32 result;

	sc->s.LOC = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_IO_CONTROL)
		& SBE_2T3E3_FRAMER_VAL_LOSS_OF_CLOCK_STATUS ? 1 : 0;

	switch (sc->p.frame_type) {
	case SBE_2T3E3_FRAME_TYPE_E3_G751:
	case SBE_2T3E3_FRAME_TYPE_E3_G832:
		result = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_E3_RX_CONFIGURATION_STATUS_2);
		sc->s.LOF = result & SBE_2T3E3_FRAMER_VAL_E3_RX_LOF ? 1 : 0;
		sc->s.OOF = result & SBE_2T3E3_FRAMER_VAL_E3_RX_OOF ? 1 : 0;

		cpld_LOS_update(sc);

		sc->s.AIS = result & SBE_2T3E3_FRAMER_VAL_E3_RX_AIS ? 1 : 0;
		sc->s.FERF = result & SBE_2T3E3_FRAMER_VAL_E3_RX_FERF ? 1 : 0;
		break;

	case SBE_2T3E3_FRAME_TYPE_T3_CBIT:
	case SBE_2T3E3_FRAME_TYPE_T3_M13:
		result = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_T3_RX_CONFIGURATION_STATUS);
		sc->s.AIS = result & SBE_2T3E3_FRAMER_VAL_T3_RX_AIS ? 1 : 0;

		cpld_LOS_update(sc);

		sc->s.IDLE = result & SBE_2T3E3_FRAMER_VAL_T3_RX_IDLE ? 1 : 0;
		sc->s.OOF = result & SBE_2T3E3_FRAMER_VAL_T3_RX_OOF ? 1 : 0;

		result = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_T3_RX_STATUS);
		sc->s.FERF = result & SBE_2T3E3_FRAMER_VAL_T3_RX_FERF ? 1 : 0;
		sc->s.AIC = result & SBE_2T3E3_FRAMER_VAL_T3_RX_AIC ? 1 : 0;
		sc->s.FEBE_code = result & SBE_2T3E3_FRAMER_VAL_T3_RX_FEBE;

		sc->s.FEAC = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_T3_RX_FEAC);
		break;

	default:
		break;
	}

	result = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_PMON_LCV_EVENT_COUNT_MSB) << 8;
	result += exar7250_read(sc, SBE_2T3E3_FRAMER_REG_PMON_HOLDING_REGISTER);
	sc->s.LCV += result;

	result = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_PMON_FRAMING_BIT_ERROR_EVENT_COUNT_MSB) << 8;
	result += exar7250_read(sc, SBE_2T3E3_FRAMER_REG_PMON_HOLDING_REGISTER);
	sc->s.FRAMING_BIT += result;

	result = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_PMON_PARITY_ERROR_EVENT_COUNT_MSB) << 8;
	result += exar7250_read(sc, SBE_2T3E3_FRAMER_REG_PMON_HOLDING_REGISTER);
	sc->s.PARITY_ERROR += result;

	result = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_PMON_FEBE_EVENT_COUNT_MSB) << 8;
	result += exar7250_read(sc, SBE_2T3E3_FRAMER_REG_PMON_HOLDING_REGISTER);
	sc->s.FEBE_count += result;

	result = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_PMON_CP_BIT_ERROR_EVENT_COUNT_MSB) << 8;
	result += exar7250_read(sc, SBE_2T3E3_FRAMER_REG_PMON_HOLDING_REGISTER);
	sc->s.CP_BIT += result;

	memcpy(stats, &(sc->s), sizeof(t3e3_stats_t));
}

static void t3e3_port_del_stats(struct channel *sc)
{
	memset(&(sc->s), 0, sizeof(t3e3_stats_t));
}

void t3e3_if_config(struct channel *sc, u32 cmd, char *set,
		    t3e3_resp_t *ret, int *rlen)
{
	t3e3_param_t *param = (t3e3_param_t *)set;
	u32 *data = (u32 *)set;

	/* turn off all interrupt */
	/* cpld_stop_intr(sc); */

	switch (cmd) {
	case SBE_2T3E3_PORT_GET:
		t3e3_port_get(sc, &(ret->u.param));
		*rlen = sizeof(ret->u.param);
		break;
	case SBE_2T3E3_PORT_SET:
		t3e3_port_set(sc, param);
		*rlen = 0;
		break;
	case SBE_2T3E3_PORT_GET_STATS:
		t3e3_port_get_stats(sc, &(ret->u.stats));
		*rlen = sizeof(ret->u.stats);
		break;
	case SBE_2T3E3_PORT_DEL_STATS:
		t3e3_port_del_stats(sc);
		*rlen = 0;
		break;
	case SBE_2T3E3_PORT_READ_REGS:
		t3e3_reg_read(sc, data, &(ret->u.data));
		*rlen = sizeof(ret->u.data);
		break;
	case SBE_2T3E3_PORT_WRITE_REGS:
		t3e3_reg_write(sc, data);
		*rlen = 0;
		break;
	case SBE_2T3E3_LOG_LEVEL:
		*rlen = 0;
		break;
	default:
		*rlen = 0;
		break;
	}
}

void t3e3_sc_init(struct channel *sc)
{
	memset(sc, 0, sizeof(*sc));

	sc->p.frame_mode = SBE_2T3E3_FRAME_MODE_HDLC;
	sc->p.fractional_mode = SBE_2T3E3_FRACTIONAL_MODE_NONE;
	sc->p.crc = SBE_2T3E3_CRC_32;
	sc->p.receiver_on = SBE_2T3E3_OFF;
	sc->p.transmitter_on = SBE_2T3E3_OFF;
	sc->p.frame_type = SBE_2T3E3_FRAME_TYPE_T3_CBIT;
	sc->p.panel = SBE_2T3E3_PANEL_FRONT;
	sc->p.line_build_out = SBE_2T3E3_OFF;
	sc->p.receive_equalization = SBE_2T3E3_OFF;
	sc->p.transmit_all_ones = SBE_2T3E3_OFF;
	sc->p.loopback = SBE_2T3E3_LOOPBACK_NONE;
	sc->p.clock_source = SBE_2T3E3_TIMING_LOCAL;
	sc->p.scrambler = SBE_2T3E3_SCRAMBLER_OFF;
	sc->p.pad_count = SBE_2T3E3_PAD_COUNT_1;
}
