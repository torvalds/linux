/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
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

#ifndef SPECTRAL_H
#define SPECTRAL_H

#include "../spectral_common.h"

/**
 * struct ath10k_spec_scan - parameters for Atheros spectral scan
 *
 * @count: number of scan results requested for manual mode
 * @fft_size: number of bins to be requested = 2^(fft_size - bin_scale)
 */
struct ath10k_spec_scan {
	u8 count;
	u8 fft_size;
};

/* enum ath10k_spectral_mode:
 *
 * @SPECTRAL_DISABLED: spectral mode is disabled
 * @SPECTRAL_BACKGROUND: hardware sends samples when it is not busy with
 *	something else.
 * @SPECTRAL_MANUAL: spectral scan is enabled, triggering for samples
 *	is performed manually.
 */
enum ath10k_spectral_mode {
	SPECTRAL_DISABLED = 0,
	SPECTRAL_BACKGROUND,
	SPECTRAL_MANUAL,
};

#ifdef CONFIG_ATH10K_DEBUGFS

int ath10k_spectral_process_fft(struct ath10k *ar,
				struct wmi_single_phyerr_rx_event *event,
				struct phyerr_fft_report *fftr,
				size_t bin_len, u64 tsf);
int ath10k_spectral_start(struct ath10k *ar);
int ath10k_spectral_vif_stop(struct ath10k_vif *arvif);
int ath10k_spectral_create(struct ath10k *ar);
void ath10k_spectral_destroy(struct ath10k *ar);

#else

static inline int
ath10k_spectral_process_fft(struct ath10k *ar,
			    struct wmi_single_phyerr_rx_event *event,
			    struct phyerr_fft_report *fftr,
			    size_t bin_len, u64 tsf)
{
	return 0;
}

static inline int ath10k_spectral_start(struct ath10k *ar)
{
	return 0;
}

static inline int ath10k_spectral_vif_stop(struct ath10k_vif *arvif)
{
	return 0;
}

static inline int ath10k_spectral_create(struct ath10k *ar)
{
	return 0;
}

static inline void ath10k_spectral_destroy(struct ath10k *ar)
{
}

#endif /* CONFIG_ATH10K_DEBUGFS */

#endif /* SPECTRAL_H */
