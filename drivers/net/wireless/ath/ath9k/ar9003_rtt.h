/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
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

#ifndef AR9003_RTT_H
#define AR9003_RTT_H

void ar9003_hw_rtt_enable(struct ath_hw *ah);
void ar9003_hw_rtt_disable(struct ath_hw *ah);
void ar9003_hw_rtt_set_mask(struct ath_hw *ah, u32 rtt_mask);
bool ar9003_hw_rtt_force_restore(struct ath_hw *ah);
void ar9003_hw_rtt_load_hist(struct ath_hw *ah, u8 chain, u32 *table);
void ar9003_hw_rtt_fill_hist(struct ath_hw *ah, u8 chain, u32 *table);
void ar9003_hw_rtt_clear_hist(struct ath_hw *ah);

#endif
