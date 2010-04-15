/*
 * Copyright (c) 2010 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "hw.h"
#include "hw-ops.h"
#include "ar9003_phy.h"

static void ar9003_hw_setup_calibration(struct ath_hw *ah,
					struct ath9k_cal_list *currCal)
{
	/* TODO */
}

static bool ar9003_hw_calibrate(struct ath_hw *ah,
				struct ath9k_channel *chan,
				u8 rxchainmask,
				bool longcal)
{
	/* TODO */
	return false;
}

static bool ar9003_hw_init_cal(struct ath_hw *ah,
			       struct ath9k_channel *chan)
{
	/* TODO */
	return false;
}

static void ar9003_hw_init_cal_settings(struct ath_hw *ah)
{
	/* TODO */
}

static bool ar9003_hw_iscal_supported(struct ath_hw *ah,
				      enum ath9k_cal_types calType)
{
	/* TODO */
	return false;
}

void ar9003_hw_attach_calib_ops(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);
	struct ath_hw_ops *ops = ath9k_hw_ops(ah);

	priv_ops->init_cal_settings = ar9003_hw_init_cal_settings;
	priv_ops->init_cal = ar9003_hw_init_cal;
	priv_ops->setup_calibration = ar9003_hw_setup_calibration;
	priv_ops->iscal_supported = ar9003_hw_iscal_supported;

	ops->calibrate = ar9003_hw_calibrate;
}
