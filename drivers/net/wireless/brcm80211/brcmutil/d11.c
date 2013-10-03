/*
 * Copyright (c) 2013 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*********************channel spec common functions*********************/

#include <linux/module.h>

#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include <brcmu_d11.h>

static void brcmu_d11n_encchspec(struct brcmu_chan *ch)
{
	ch->chspec = ch->chnum & BRCMU_CHSPEC_CH_MASK;

	switch (ch->bw) {
	case BRCMU_CHAN_BW_20:
		ch->chspec |= BRCMU_CHSPEC_D11N_BW_20 | BRCMU_CHSPEC_D11N_SB_N;
		break;
	case BRCMU_CHAN_BW_40:
	default:
		WARN_ON_ONCE(1);
		break;
	}

	if (ch->chnum <= CH_MAX_2G_CHANNEL)
		ch->chspec |= BRCMU_CHSPEC_D11N_BND_2G;
	else
		ch->chspec |= BRCMU_CHSPEC_D11N_BND_5G;
}

static void brcmu_d11ac_encchspec(struct brcmu_chan *ch)
{
	ch->chspec = ch->chnum & BRCMU_CHSPEC_CH_MASK;

	switch (ch->bw) {
	case BRCMU_CHAN_BW_20:
		ch->chspec |= BRCMU_CHSPEC_D11AC_BW_20;
		break;
	case BRCMU_CHAN_BW_40:
	case BRCMU_CHAN_BW_80:
	case BRCMU_CHAN_BW_80P80:
	case BRCMU_CHAN_BW_160:
	default:
		WARN_ON_ONCE(1);
		break;
	}

	if (ch->chnum <= CH_MAX_2G_CHANNEL)
		ch->chspec |= BRCMU_CHSPEC_D11AC_BND_2G;
	else
		ch->chspec |= BRCMU_CHSPEC_D11AC_BND_5G;
}

static void brcmu_d11n_decchspec(struct brcmu_chan *ch)
{
	u16 val;

	ch->chnum = (u8)(ch->chspec & BRCMU_CHSPEC_CH_MASK);

	switch (ch->chspec & BRCMU_CHSPEC_D11N_BW_MASK) {
	case BRCMU_CHSPEC_D11N_BW_20:
		ch->bw = BRCMU_CHAN_BW_20;
		break;
	case BRCMU_CHSPEC_D11N_BW_40:
		ch->bw = BRCMU_CHAN_BW_40;
		val = ch->chspec & BRCMU_CHSPEC_D11N_SB_MASK;
		if (val == BRCMU_CHSPEC_D11N_SB_L) {
			ch->sb = BRCMU_CHAN_SB_L;
			ch->chnum -= CH_10MHZ_APART;
		} else {
			ch->sb = BRCMU_CHAN_SB_U;
			ch->chnum += CH_10MHZ_APART;
		}
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	switch (ch->chspec & BRCMU_CHSPEC_D11N_BND_MASK) {
	case BRCMU_CHSPEC_D11N_BND_5G:
		ch->band = BRCMU_CHAN_BAND_5G;
		break;
	case BRCMU_CHSPEC_D11N_BND_2G:
		ch->band = BRCMU_CHAN_BAND_2G;
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}
}

static void brcmu_d11ac_decchspec(struct brcmu_chan *ch)
{
	u16 val;

	ch->chnum = (u8)(ch->chspec & BRCMU_CHSPEC_CH_MASK);

	switch (ch->chspec & BRCMU_CHSPEC_D11AC_BW_MASK) {
	case BRCMU_CHSPEC_D11AC_BW_20:
		ch->bw = BRCMU_CHAN_BW_20;
		break;
	case BRCMU_CHSPEC_D11AC_BW_40:
		ch->bw = BRCMU_CHAN_BW_40;
		val = ch->chspec & BRCMU_CHSPEC_D11AC_SB_MASK;
		if (val == BRCMU_CHSPEC_D11AC_SB_L) {
			ch->sb = BRCMU_CHAN_SB_L;
			ch->chnum -= CH_10MHZ_APART;
		} else if (val == BRCMU_CHSPEC_D11AC_SB_U) {
			ch->sb = BRCMU_CHAN_SB_U;
			ch->chnum += CH_10MHZ_APART;
		} else {
			WARN_ON_ONCE(1);
		}
		break;
	case BRCMU_CHSPEC_D11AC_BW_80:
		ch->bw = BRCMU_CHAN_BW_80;
		break;
	case BRCMU_CHSPEC_D11AC_BW_8080:
	case BRCMU_CHSPEC_D11AC_BW_160:
	default:
		WARN_ON_ONCE(1);
		break;
	}

	switch (ch->chspec & BRCMU_CHSPEC_D11AC_BND_MASK) {
	case BRCMU_CHSPEC_D11AC_BND_5G:
		ch->band = BRCMU_CHAN_BAND_5G;
		break;
	case BRCMU_CHSPEC_D11AC_BND_2G:
		ch->band = BRCMU_CHAN_BAND_2G;
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}
}

void brcmu_d11_attach(struct brcmu_d11inf *d11inf)
{
	if (d11inf->io_type == BRCMU_D11N_IOTYPE) {
		d11inf->encchspec = brcmu_d11n_encchspec;
		d11inf->decchspec = brcmu_d11n_decchspec;
	} else {
		d11inf->encchspec = brcmu_d11ac_encchspec;
		d11inf->decchspec = brcmu_d11ac_decchspec;
	}
}
EXPORT_SYMBOL(brcmu_d11_attach);
