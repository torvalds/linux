/*
 * P2P - generic helper functions
 * Copyright (c) 2009, Atheros Communications
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "p2p_i.h"


/**
 * p2p_random - Generate random string for SSID and passphrase
 * @buf: Buffer for returning the result
 * @len: Number of octets to write to the buffer
 * Returns: 0 on success, -1 on failure
 *
 * This function generates a random string using the following character set:
 * 'A'-'Z', 'a'-'z', '0'-'9'.
 */
int p2p_random(char *buf, size_t len)
{
	u8 val;
	size_t i;
	u8 letters = 'Z' - 'A' + 1;
	u8 numbers = 10;

	if (os_get_random((unsigned char *) buf, len))
		return -1;
	/* Character set: 'A'-'Z', 'a'-'z', '0'-'9' */
	for (i = 0; i < len; i++) {
		val = buf[i];
		val %= 2 * letters + numbers;
		if (val < letters)
			buf[i] = 'A' + val;
		else if (val < 2 * letters)
			buf[i] = 'a' + (val - letters);
		else
			buf[i] = '0' + (val - 2 * letters);
	}

	return 0;
}


static int p2p_channel_to_freq_j4(int reg_class, int channel)
{
	/* Table J-4 in P802.11REVmb/D4.0 - Global operating classes */
	/* TODO: more regulatory classes */
	switch (reg_class) {
	case 81:
		/* channels 1..13 */
		if (channel < 1 || channel > 13)
			return -1;
		return 2407 + 5 * channel;
	case 82:
		/* channel 14 */
		if (channel != 14)
			return -1;
		return 2414 + 5 * channel;
	case 83: /* channels 1..9; 40 MHz */
	case 84: /* channels 5..13; 40 MHz */
		if (channel < 1 || channel > 13)
			return -1;
		return 2407 + 5 * channel;
	case 115: /* channels 36,40,44,48; indoor only */
	case 118: /* channels 52,56,60,64; dfs */
		if (channel < 36 || channel > 64)
			return -1;
		return 5000 + 5 * channel;
	case 124: /* channels 149,153,157,161 */
	case 125: /* channels 149,153,157,161,165,169 */
		if (channel < 149 || channel > 161)
			return -1;
		return 5000 + 5 * channel;
	case 116: /* channels 36,44; 40 MHz; indoor only */
	case 117: /* channels 40,48; 40 MHz; indoor only */
	case 119: /* channels 52,60; 40 MHz; dfs */
	case 120: /* channels 56,64; 40 MHz; dfs */
		if (channel < 36 || channel > 64)
			return -1;
		return 5000 + 5 * channel;
	case 126: /* channels 149,157; 40 MHz */
	case 127: /* channels 153,161; 40 MHz */
		if (channel < 149 || channel > 161)
			return -1;
		return 5000 + 5 * channel;
	}
	return -1;
}


/**
 * p2p_channel_to_freq - Convert channel info to frequency
 * @country: Country code
 * @reg_class: Regulatory class
 * @channel: Channel number
 * Returns: Frequency in MHz or -1 if the specified channel is unknown
 */
int p2p_channel_to_freq(const char *country, int reg_class, int channel)
{
	if (country[2] == 0x04)
		return p2p_channel_to_freq_j4(reg_class, channel);

	/* These are mainly for backwards compatibility; to be removed */
	switch (reg_class) {
	case 1: /* US/1, EU/1, JP/1 = 5 GHz, channels 36,40,44,48 */
		if (channel < 36 || channel > 48)
			return -1;
		return 5000 + 5 * channel;
	case 3: /* US/3 = 5 GHz, channels 149,153,157,161 */
	case 5: /* US/5 = 5 GHz, channels 149,153,157,161 */
		if (channel < 149 || channel > 161)
			return -1;
		return 5000 + 5 * channel;
	case 4: /* EU/4 = 2.407 GHz, channels 1..13 */
	case 12: /* US/12 = 2.407 GHz, channels 1..11 */
	case 30: /* JP/30 = 2.407 GHz, channels 1..13 */
		if (channel < 1 || channel > 13)
			return -1;
		return 2407 + 5 * channel;
	case 31: /* JP/31 = 2.414 GHz, channel 14 */
		if (channel != 14)
			return -1;
		return 2414 + 5 * channel;
	}

	return -1;
}


/**
 * p2p_freq_to_channel - Convert frequency into channel info
 * @country: Country code
 * @reg_class: Buffer for returning regulatory class
 * @channel: Buffer for returning channel number
 * Returns: 0 on success, -1 if the specified frequency is unknown
 */
int p2p_freq_to_channel(const char *country, unsigned int freq, u8 *reg_class,
			u8 *channel)
{
	/* TODO: more operating classes */
	if (freq >= 2412 && freq <= 2472) {
		*reg_class = 81; /* 2.407 GHz, channels 1..13 */
		*channel = (freq - 2407) / 5;
		return 0;
	}

	if (freq == 2484) {
		*reg_class = 82; /* channel 14 */
		*channel = 14;
		return 0;
	}

	if (freq >= 5180 && freq <= 5240) {
		*reg_class = 115; /* 5 GHz, channels 36..48 */
		*channel = (freq - 5000) / 5;
		return 0;
	}

	if (freq >= 5745 && freq <= 5805) {
		*reg_class = 124; /* 5 GHz, channels 149..161 */
		*channel = (freq - 5000) / 5;
		return 0;
	}

	return -1;
}


static void p2p_reg_class_intersect(const struct p2p_reg_class *a,
				    const struct p2p_reg_class *b,
				    struct p2p_reg_class *res)
{
	size_t i, j;

	res->reg_class = a->reg_class;

	for (i = 0; i < a->channels; i++) {
		for (j = 0; j < b->channels; j++) {
			if (a->channel[i] != b->channel[j])
				continue;
			res->channel[res->channels] = a->channel[i];
			res->channels++;
			if (res->channels == P2P_MAX_REG_CLASS_CHANNELS)
				return;
		}
	}
}


/**
 * p2p_channels_intersect - Intersection of supported channel lists
 * @a: First set of supported channels
 * @b: Second set of supported channels
 * @res: Data structure for returning the intersection of support channels
 *
 * This function can be used to find a common set of supported channels. Both
 * input channels sets are assumed to use the same country code. If different
 * country codes are used, the regulatory class numbers may not be matched
 * correctly and results are undefined.
 */
void p2p_channels_intersect(const struct p2p_channels *a,
			    const struct p2p_channels *b,
			    struct p2p_channels *res)
{
	size_t i, j;

	os_memset(res, 0, sizeof(*res));

	for (i = 0; i < a->reg_classes; i++) {
		const struct p2p_reg_class *a_reg = &a->reg_class[i];
		for (j = 0; j < b->reg_classes; j++) {
			const struct p2p_reg_class *b_reg = &b->reg_class[j];
			if (a_reg->reg_class != b_reg->reg_class)
				continue;
			p2p_reg_class_intersect(
				a_reg, b_reg,
				&res->reg_class[res->reg_classes]);
			if (res->reg_class[res->reg_classes].channels) {
				res->reg_classes++;
				if (res->reg_classes == P2P_MAX_REG_CLASSES)
					return;
			}
		}
	}
}


/**
 * p2p_channels_includes - Check whether a channel is included in the list
 * @channels: List of supported channels
 * @reg_class: Regulatory class of the channel to search
 * @channel: Channel number of the channel to search
 * Returns: 1 if channel was found or 0 if not
 */
int p2p_channels_includes(const struct p2p_channels *channels, u8 reg_class,
			  u8 channel)
{
	size_t i, j;
	for (i = 0; i < channels->reg_classes; i++) {
		const struct p2p_reg_class *reg = &channels->reg_class[i];
		if (reg->reg_class != reg_class)
			continue;
		for (j = 0; j < reg->channels; j++) {
			if (reg->channel[j] == channel)
				return 1;
		}
	}
	return 0;
}


int p2p_supported_freq(struct p2p_data *p2p, unsigned int freq)
{
	u8 op_reg_class, op_channel;
	if (p2p_freq_to_channel(p2p->cfg->country, freq,
				&op_reg_class, &op_channel) < 0)
		return 0;
	return p2p_channels_includes(&p2p->cfg->channels, op_reg_class,
				     op_channel);
}
