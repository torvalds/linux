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

#include <linux/delay.h>
#include "2t3e3.h"
#include "ctrl.h"

#define bootrom_set_bit(sc, reg, bit)				\
	bootrom_write((sc), (reg),				\
		      bootrom_read((sc), (reg)) | (bit))

#define bootrom_clear_bit(sc, reg, bit)				\
	bootrom_write((sc), (reg),				\
		      bootrom_read((sc), (reg)) & ~(bit))

static inline void cpld_set_bit(struct channel *channel, unsigned reg, u32 bit)
{
	unsigned long flags;
	spin_lock_irqsave(&channel->card->bootrom_lock, flags);
	bootrom_set_bit(channel, CPLD_MAP_REG(reg, channel), bit);
	spin_unlock_irqrestore(&channel->card->bootrom_lock, flags);
}

static inline void cpld_clear_bit(struct channel *channel, unsigned reg, u32 bit)
{
	unsigned long flags;
	spin_lock_irqsave(&channel->card->bootrom_lock, flags);
	bootrom_clear_bit(channel, CPLD_MAP_REG(reg, channel), bit);
	spin_unlock_irqrestore(&channel->card->bootrom_lock, flags);
}

void cpld_init(struct channel *sc)
{
	u32 val;

	/* PCRA */
	val = SBE_2T3E3_CPLD_VAL_CRC32 |
		cpld_val_map[SBE_2T3E3_CPLD_VAL_LOOP_TIMING_SOURCE][sc->h.slot];
	cpld_write(sc, SBE_2T3E3_CPLD_REG_PCRA, val);

	/* PCRB */
	val = 0;
	cpld_write(sc, SBE_2T3E3_CPLD_REG_PCRB, val);

	/* PCRC */
	val = 0;
	cpld_write(sc, SBE_2T3E3_CPLD_REG_PCRC, val);

	/* PBWF */
	val = 0;
	cpld_write(sc, SBE_2T3E3_CPLD_REG_PBWF, val);

	/* PBWL */
	val = 0;
	cpld_write(sc, SBE_2T3E3_CPLD_REG_PBWL, val);

	/* PLTR */
	val = SBE_2T3E3_CPLD_VAL_LCV_COUNTER;
	cpld_write(sc, SBE_2T3E3_CPLD_REG_PLTR, val);
	udelay(1000);

	/* PLCR */
	val = 0;
	cpld_write(sc, SBE_2T3E3_CPLD_REG_PLCR, val);
	udelay(1000);

	/* PPFR */
	val = 0x55;
	cpld_write(sc, SBE_2T3E3_CPLD_REG_PPFR, val);
	/* TODO: this doesn't work!!! */

	/* SERIAL_CHIP_SELECT */
	val = 0;
	cpld_write(sc, SBE_2T3E3_CPLD_REG_SERIAL_CHIP_SELECT, val);

	/* PICSR */
	val = SBE_2T3E3_CPLD_VAL_DMO_SIGNAL_DETECTED |
		SBE_2T3E3_CPLD_VAL_RECEIVE_LOSS_OF_LOCK_DETECTED |
		SBE_2T3E3_CPLD_VAL_RECEIVE_LOSS_OF_SIGNAL_DETECTED;
	cpld_write(sc, SBE_2T3E3_CPLD_REG_PICSR, val);

	cpld_start_intr(sc);

	udelay(1000);
}

void cpld_start_intr(struct channel *sc)
{
	u32 val;

	/* PIER */
	val = SBE_2T3E3_CPLD_VAL_INTERRUPT_FROM_ETHERNET_ENABLE |
		SBE_2T3E3_CPLD_VAL_INTERRUPT_FROM_FRAMER_ENABLE;
	cpld_write(sc, SBE_2T3E3_CPLD_REG_PIER, val);
}

void cpld_stop_intr(struct channel *sc)
{
	u32 val;

	/* PIER */
	val = 0;
	cpld_write(sc, SBE_2T3E3_CPLD_REG_PIER, val);
}

void cpld_set_frame_mode(struct channel *sc, u32 mode)
{
	if (sc->p.frame_mode == mode)
		return;

	switch (mode) {
	case SBE_2T3E3_FRAME_MODE_HDLC:
		cpld_clear_bit(sc, SBE_2T3E3_CPLD_REG_PCRA,
			       SBE_2T3E3_CPLD_VAL_TRANSPARENT_MODE |
			       SBE_2T3E3_CPLD_VAL_RAW_MODE);
		exar7250_unipolar_onoff(sc, SBE_2T3E3_OFF);
		exar7300_unipolar_onoff(sc, SBE_2T3E3_OFF);
		break;
	case SBE_2T3E3_FRAME_MODE_TRANSPARENT:
		cpld_clear_bit(sc, SBE_2T3E3_CPLD_REG_PCRA,
			       SBE_2T3E3_CPLD_VAL_RAW_MODE);
		cpld_set_bit(sc, SBE_2T3E3_CPLD_REG_PCRA,
			     SBE_2T3E3_CPLD_VAL_TRANSPARENT_MODE);
		exar7250_unipolar_onoff(sc, SBE_2T3E3_OFF);
		exar7300_unipolar_onoff(sc, SBE_2T3E3_OFF);
		break;
	case SBE_2T3E3_FRAME_MODE_RAW:
		cpld_set_bit(sc, SBE_2T3E3_CPLD_REG_PCRA,
			     SBE_2T3E3_CPLD_VAL_RAW_MODE);
		exar7250_unipolar_onoff(sc, SBE_2T3E3_ON);
		exar7300_unipolar_onoff(sc, SBE_2T3E3_ON);
		break;
	default:
		return;
	}

	sc->p.frame_mode = mode;
}

/* set rate of the local clock */
void cpld_set_frame_type(struct channel *sc, u32 type)
{
	switch (type) {
	case SBE_2T3E3_FRAME_TYPE_E3_G751:
	case SBE_2T3E3_FRAME_TYPE_E3_G832:
		cpld_set_bit(sc, SBE_2T3E3_CPLD_REG_PCRA,
			     SBE_2T3E3_CPLD_VAL_LOCAL_CLOCK_E3);
		break;
	case SBE_2T3E3_FRAME_TYPE_T3_CBIT:
	case SBE_2T3E3_FRAME_TYPE_T3_M13:
		cpld_clear_bit(sc, SBE_2T3E3_CPLD_REG_PCRA,
			       SBE_2T3E3_CPLD_VAL_LOCAL_CLOCK_E3);
		break;
	default:
		return;
	}
}

void cpld_set_scrambler(struct channel *sc, u32 mode)
{
	if (sc->p.scrambler == mode)
		return;

	switch (mode) {
	case SBE_2T3E3_SCRAMBLER_OFF:
		cpld_clear_bit(sc, SBE_2T3E3_CPLD_REG_PCRB,
			       SBE_2T3E3_CPLD_VAL_SCRAMBLER_ENABLE);
		break;
	case SBE_2T3E3_SCRAMBLER_LARSCOM:
		cpld_clear_bit(sc, SBE_2T3E3_CPLD_REG_PCRB,
			       SBE_2T3E3_CPLD_VAL_SCRAMBLER_TYPE);
		cpld_set_bit(sc, SBE_2T3E3_CPLD_REG_PCRB,
			     SBE_2T3E3_CPLD_VAL_SCRAMBLER_ENABLE);
		break;
	case SBE_2T3E3_SCRAMBLER_ADC_KENTROX_DIGITAL:
		cpld_set_bit(sc, SBE_2T3E3_CPLD_REG_PCRB,
			     SBE_2T3E3_CPLD_VAL_SCRAMBLER_TYPE);
		cpld_set_bit(sc, SBE_2T3E3_CPLD_REG_PCRB,
			     SBE_2T3E3_CPLD_VAL_SCRAMBLER_ENABLE);
		break;
	default:
		return;
	}

	sc->p.scrambler = mode;
}


void cpld_set_crc(struct channel *sc, u32 crc)
{
	if (sc->p.crc == crc)
		return;

	switch (crc) {
	case SBE_2T3E3_CRC_16:
		cpld_clear_bit(sc, SBE_2T3E3_CPLD_REG_PCRA,
			       SBE_2T3E3_CPLD_VAL_CRC32);
		break;
	case SBE_2T3E3_CRC_32:
		cpld_set_bit(sc, SBE_2T3E3_CPLD_REG_PCRA,
			     SBE_2T3E3_CPLD_VAL_CRC32);
		break;
	default:
		return;
	}

	sc->p.crc = crc;
}


void cpld_select_panel(struct channel *sc, u32 panel)
{
	if (sc->p.panel == panel)
		return;
	switch (panel) {
	case SBE_2T3E3_PANEL_FRONT:
		cpld_clear_bit(sc, SBE_2T3E3_CPLD_REG_PCRA,
			       SBE_2T3E3_CPLD_VAL_REAR_PANEL);
		break;
	case SBE_2T3E3_PANEL_REAR:
		cpld_set_bit(sc, SBE_2T3E3_CPLD_REG_PCRA,
			     SBE_2T3E3_CPLD_VAL_REAR_PANEL);
		break;
	default:
		return;
	}

	udelay(100);

	sc->p.panel = panel;
}


extern void cpld_set_clock(struct channel *sc, u32 mode)
{
	if (sc->p.clock_source == mode)
		return;

	switch (mode) {
	case SBE_2T3E3_TIMING_LOCAL:
		cpld_set_bit(sc, SBE_2T3E3_CPLD_REG_PCRA,
			     SBE_2T3E3_CPLD_VAL_ALT);
		break;
	case SBE_2T3E3_TIMING_LOOP:
		cpld_clear_bit(sc, SBE_2T3E3_CPLD_REG_PCRA,
			       SBE_2T3E3_CPLD_VAL_ALT);
		break;
	default:
		return;
	}

	sc->p.clock_source = mode;
}

void cpld_set_pad_count(struct channel *sc, u32 count)
{
	u32 val;

	if (sc->p.pad_count == count)
		return;

	switch (count) {
	case SBE_2T3E3_PAD_COUNT_1:
		val = SBE_2T3E3_CPLD_VAL_PAD_COUNT_1;
		break;
	case SBE_2T3E3_PAD_COUNT_2:
		val = SBE_2T3E3_CPLD_VAL_PAD_COUNT_2;
		break;
	case SBE_2T3E3_PAD_COUNT_3:
		val = SBE_2T3E3_CPLD_VAL_PAD_COUNT_3;
		break;
	case SBE_2T3E3_PAD_COUNT_4:
		val = SBE_2T3E3_CPLD_VAL_PAD_COUNT_4;
		break;
	default:
		return;
	}

	cpld_clear_bit(sc, SBE_2T3E3_CPLD_REG_PCRB,
		       SBE_2T3E3_CPLD_VAL_PAD_COUNT);
	cpld_set_bit(sc, SBE_2T3E3_CPLD_REG_PCRB, val);
	sc->p.pad_count = count;
}

void cpld_LOS_update(struct channel *sc)
{
	u_int8_t los;

	cpld_write(sc, SBE_2T3E3_CPLD_REG_PICSR,
		   SBE_2T3E3_CPLD_VAL_DMO_SIGNAL_DETECTED |
		   SBE_2T3E3_CPLD_VAL_RECEIVE_LOSS_OF_LOCK_DETECTED |
		   SBE_2T3E3_CPLD_VAL_RECEIVE_LOSS_OF_SIGNAL_DETECTED);
	los = cpld_read(sc, SBE_2T3E3_CPLD_REG_PICSR) &
		SBE_2T3E3_CPLD_VAL_RECEIVE_LOSS_OF_SIGNAL_DETECTED;

	if (los != sc->s.LOS)
		dev_info(&sc->pdev->dev, "SBE 2T3E3: LOS status: %s\n",
			 los ? "Loss of signal" : "Signal OK");
	sc->s.LOS = los;
}

void cpld_set_fractional_mode(struct channel *sc, u32 mode,
			      u32 start, u32 stop)
{
	if (mode == SBE_2T3E3_FRACTIONAL_MODE_NONE) {
		start = 0;
		stop = 0;
	}

	if (sc->p.fractional_mode == mode && sc->p.bandwidth_start == start &&
	    sc->p.bandwidth_stop == stop)
		return;

	switch (mode) {
	case SBE_2T3E3_FRACTIONAL_MODE_NONE:
		cpld_write(sc, SBE_2T3E3_CPLD_REG_PCRC,
			   SBE_2T3E3_CPLD_VAL_FRACTIONAL_MODE_NONE);
		break;
	case SBE_2T3E3_FRACTIONAL_MODE_0:
		cpld_write(sc, SBE_2T3E3_CPLD_REG_PCRC,
			   SBE_2T3E3_CPLD_VAL_FRACTIONAL_MODE_0);
		break;
	case SBE_2T3E3_FRACTIONAL_MODE_1:
		cpld_write(sc, SBE_2T3E3_CPLD_REG_PCRC,
			   SBE_2T3E3_CPLD_VAL_FRACTIONAL_MODE_1);
		break;
	case SBE_2T3E3_FRACTIONAL_MODE_2:
		cpld_write(sc, SBE_2T3E3_CPLD_REG_PCRC,
			   SBE_2T3E3_CPLD_VAL_FRACTIONAL_MODE_2);
		break;
	default:
		printk(KERN_ERR "wrong mode in set_fractional_mode\n");
		return;
	}

	cpld_write(sc, SBE_2T3E3_CPLD_REG_PBWF, start);
	cpld_write(sc, SBE_2T3E3_CPLD_REG_PBWL, stop);

	sc->p.fractional_mode = mode;
	sc->p.bandwidth_start = start;
	sc->p.bandwidth_stop = stop;
}
