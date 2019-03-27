/*
 * P2P - generic helper functions
 * Copyright (c) 2009, Atheros Communications
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "common/defs.h"
#include "common/ieee802_11_common.h"
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


/**
 * p2p_channel_to_freq - Convert channel info to frequency
 * @op_class: Operating class
 * @channel: Channel number
 * Returns: Frequency in MHz or -1 if the specified channel is unknown
 */
int p2p_channel_to_freq(int op_class, int channel)
{
	return ieee80211_chan_to_freq(NULL, op_class, channel);
}


/**
 * p2p_freq_to_channel - Convert frequency into channel info
 * @op_class: Buffer for returning operating class
 * @channel: Buffer for returning channel number
 * Returns: 0 on success, -1 if the specified frequency is unknown
 */
int p2p_freq_to_channel(unsigned int freq, u8 *op_class, u8 *channel)
{
	if (ieee80211_freq_to_channel_ext(freq, 0, 0, op_class, channel) ==
	    NUM_HOSTAPD_MODES)
		return -1;

	return 0;
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


static void p2p_op_class_union(struct p2p_reg_class *cl,
			       const struct p2p_reg_class *b_cl)
{
	size_t i, j;

	for (i = 0; i < b_cl->channels; i++) {
		for (j = 0; j < cl->channels; j++) {
			if (b_cl->channel[i] == cl->channel[j])
				break;
		}
		if (j == cl->channels) {
			if (cl->channels == P2P_MAX_REG_CLASS_CHANNELS)
				return;
			cl->channel[cl->channels++] = b_cl->channel[i];
		}
	}
}


/**
 * p2p_channels_union_inplace - Inplace union of channel lists
 * @res: Input data and place for returning union of the channel sets
 * @b: Second set of channels
 */
void p2p_channels_union_inplace(struct p2p_channels *res,
				const struct p2p_channels *b)
{
	size_t i, j;

	for (i = 0; i < res->reg_classes; i++) {
		struct p2p_reg_class *cl = &res->reg_class[i];
		for (j = 0; j < b->reg_classes; j++) {
			const struct p2p_reg_class *b_cl = &b->reg_class[j];
			if (cl->reg_class != b_cl->reg_class)
				continue;
			p2p_op_class_union(cl, b_cl);
		}
	}

	for (j = 0; j < b->reg_classes; j++) {
		const struct p2p_reg_class *b_cl = &b->reg_class[j];

		for (i = 0; i < res->reg_classes; i++) {
			struct p2p_reg_class *cl = &res->reg_class[i];
			if (cl->reg_class == b_cl->reg_class)
				break;
		}

		if (i == res->reg_classes) {
			if (res->reg_classes == P2P_MAX_REG_CLASSES)
				return;
			os_memcpy(&res->reg_class[res->reg_classes++],
				  b_cl, sizeof(struct p2p_reg_class));
		}
	}
}


/**
 * p2p_channels_union - Union of channel lists
 * @a: First set of channels
 * @b: Second set of channels
 * @res: Data structure for returning the union of channels
 */
void p2p_channels_union(const struct p2p_channels *a,
			const struct p2p_channels *b,
			struct p2p_channels *res)
{
	os_memcpy(res, a, sizeof(*res));
	p2p_channels_union_inplace(res, b);
}


void p2p_channels_remove_freqs(struct p2p_channels *chan,
			       const struct wpa_freq_range_list *list)
{
	size_t o, c;

	if (list == NULL)
		return;

	o = 0;
	while (o < chan->reg_classes) {
		struct p2p_reg_class *op = &chan->reg_class[o];

		c = 0;
		while (c < op->channels) {
			int freq = p2p_channel_to_freq(op->reg_class,
						       op->channel[c]);
			if (freq > 0 && freq_range_list_includes(list, freq)) {
				op->channels--;
				os_memmove(&op->channel[c],
					   &op->channel[c + 1],
					   op->channels - c);
			} else
				c++;
		}

		if (op->channels == 0) {
			chan->reg_classes--;
			os_memmove(&chan->reg_class[o], &chan->reg_class[o + 1],
				   (chan->reg_classes - o) *
				   sizeof(struct p2p_reg_class));
		} else
			o++;
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


int p2p_channels_includes_freq(const struct p2p_channels *channels,
			       unsigned int freq)
{
	size_t i, j;
	for (i = 0; i < channels->reg_classes; i++) {
		const struct p2p_reg_class *reg = &channels->reg_class[i];
		for (j = 0; j < reg->channels; j++) {
			if (p2p_channel_to_freq(reg->reg_class,
						reg->channel[j]) == (int) freq)
				return 1;
		}
	}
	return 0;
}


int p2p_supported_freq(struct p2p_data *p2p, unsigned int freq)
{
	u8 op_reg_class, op_channel;
	if (p2p_freq_to_channel(freq, &op_reg_class, &op_channel) < 0)
		return 0;
	return p2p_channels_includes(&p2p->cfg->channels, op_reg_class,
				     op_channel);
}


int p2p_supported_freq_go(struct p2p_data *p2p, unsigned int freq)
{
	u8 op_reg_class, op_channel;
	if (p2p_freq_to_channel(freq, &op_reg_class, &op_channel) < 0)
		return 0;
	return p2p_channels_includes(&p2p->cfg->channels, op_reg_class,
				     op_channel) &&
		!freq_range_list_includes(&p2p->no_go_freq, freq);
}


int p2p_supported_freq_cli(struct p2p_data *p2p, unsigned int freq)
{
	u8 op_reg_class, op_channel;
	if (p2p_freq_to_channel(freq, &op_reg_class, &op_channel) < 0)
		return 0;
	return p2p_channels_includes(&p2p->cfg->channels, op_reg_class,
				     op_channel) ||
		p2p_channels_includes(&p2p->cfg->cli_channels, op_reg_class,
				      op_channel);
}


unsigned int p2p_get_pref_freq(struct p2p_data *p2p,
			       const struct p2p_channels *channels)
{
	unsigned int i;
	int freq = 0;
	const struct p2p_channels *tmpc = channels ?
		channels : &p2p->cfg->channels;

	if (tmpc == NULL)
		return 0;

	for (i = 0; p2p->cfg->pref_chan && i < p2p->cfg->num_pref_chan; i++) {
		freq = p2p_channel_to_freq(p2p->cfg->pref_chan[i].op_class,
					   p2p->cfg->pref_chan[i].chan);
		if (p2p_channels_includes_freq(tmpc, freq))
			return freq;
	}
	return 0;
}


void p2p_channels_dump(struct p2p_data *p2p, const char *title,
		       const struct p2p_channels *chan)
{
	char buf[500], *pos, *end;
	size_t i, j;
	int ret;

	pos = buf;
	end = pos + sizeof(buf);

	for (i = 0; i < chan->reg_classes; i++) {
		const struct p2p_reg_class *c;
		c = &chan->reg_class[i];
		ret = os_snprintf(pos, end - pos, " %u:", c->reg_class);
		if (os_snprintf_error(end - pos, ret))
			break;
		pos += ret;

		for (j = 0; j < c->channels; j++) {
			ret = os_snprintf(pos, end - pos, "%s%u",
					  j == 0 ? "" : ",",
					  c->channel[j]);
			if (os_snprintf_error(end - pos, ret))
				break;
			pos += ret;
		}
	}
	*pos = '\0';

	p2p_dbg(p2p, "%s:%s", title, buf);
}


static u8 p2p_channel_pick_random(const u8 *channels, unsigned int num_channels)
{
	unsigned int r;
	if (os_get_random((u8 *) &r, sizeof(r)) < 0)
		r = 0;
	r %= num_channels;
	return channels[r];
}


int p2p_channel_select(struct p2p_channels *chans, const int *classes,
		       u8 *op_class, u8 *op_channel)
{
	unsigned int i, j;

	for (j = 0; classes == NULL || classes[j]; j++) {
		for (i = 0; i < chans->reg_classes; i++) {
			struct p2p_reg_class *c = &chans->reg_class[i];

			if (c->channels == 0)
				continue;

			if (classes == NULL || c->reg_class == classes[j]) {
				/*
				 * Pick one of the available channels in the
				 * operating class at random.
				 */
				*op_class = c->reg_class;
				*op_channel = p2p_channel_pick_random(
					c->channel, c->channels);
				return 0;
			}
		}
		if (classes == NULL)
			break;
	}

	return -1;
}


int p2p_channel_random_social(struct p2p_channels *chans, u8 *op_class,
			      u8 *op_channel)
{
	u8 chan[4];
	unsigned int num_channels = 0;

	/* Try to find available social channels from 2.4 GHz */
	if (p2p_channels_includes(chans, 81, 1))
		chan[num_channels++] = 1;
	if (p2p_channels_includes(chans, 81, 6))
		chan[num_channels++] = 6;
	if (p2p_channels_includes(chans, 81, 11))
		chan[num_channels++] = 11;

	/* Try to find available social channels from 60 GHz */
	if (p2p_channels_includes(chans, 180, 2))
		chan[num_channels++] = 2;

	if (num_channels == 0)
		return -1;

	*op_channel = p2p_channel_pick_random(chan, num_channels);
	if (*op_channel == 2)
		*op_class = 180;
	else
		*op_class = 81;

	return 0;
}


int p2p_channels_to_freqs(const struct p2p_channels *channels, int *freq_list,
			  unsigned int max_len)
{
	unsigned int i, idx;

	if (!channels || max_len == 0)
		return 0;

	for (i = 0, idx = 0; i < channels->reg_classes; i++) {
		const struct p2p_reg_class *c = &channels->reg_class[i];
		unsigned int j;

		if (idx + 1 == max_len)
			break;
		for (j = 0; j < c->channels; j++) {
			int freq;
			unsigned int k;

			if (idx + 1 == max_len)
				break;
			freq = p2p_channel_to_freq(c->reg_class,
						   c->channel[j]);
			if (freq < 0)
				continue;

			for (k = 0; k < idx; k++) {
				if (freq_list[k] == freq)
					break;
			}

			if (k < idx)
				continue;
			freq_list[idx++] = freq;
		}
	}

	freq_list[idx] = 0;

	return idx;
}
