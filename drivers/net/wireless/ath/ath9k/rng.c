/*
 * Copyright (c) 2015 Qualcomm Atheros, Inc.
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

#include "ath9k.h"
#include "hw.h"
#include "ar9003_phy.h"

static int ath9k_rng_data_read(struct hwrng *rng, u32 *data)
{
	u32 v1, v2;
	struct ath_softc *sc = (struct ath_softc *)rng->priv;
	struct ath_hw *ah = sc->sc_ah;

	ath9k_ps_wakeup(sc);

	REG_RMW_FIELD(ah, AR_PHY_TEST, AR_PHY_TEST_BBB_OBS_SEL, 5);
	REG_CLR_BIT(ah, AR_PHY_TEST, AR_PHY_TEST_RX_OBS_SEL_BIT5);
	REG_RMW_FIELD(ah, AR_PHY_TEST_CTL_STATUS, AR_PHY_TEST_CTL_RX_OBS_SEL, 0);

	v1 = REG_READ(ah, AR_PHY_TST_ADC);
	v2 = REG_READ(ah, AR_PHY_TST_ADC);

	ath9k_ps_restore(sc);

	/* wait for data ready */
	if (v1 && v2 && sc->rng_last != v1 && v1 != v2) {
		*data = (v1 & 0xffff) | (v2 << 16);
		sc->rng_last = v2;

		return sizeof(u32);
	}

	sc->rng_last = v2;

	return 0;
}

void ath9k_rng_register(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;

	if (WARN_ON(sc->rng_initialized))
		return;

	if (!AR_SREV_9300_20_OR_LATER(ah))
		return;

	sc->rng.name = "ath9k";
	sc->rng.data_read = ath9k_rng_data_read;
	sc->rng.priv = (unsigned long)sc;

	if (!hwrng_register(&sc->rng))
		sc->rng_initialized = true;
}

void ath9k_rng_unregister(struct ath_softc *sc)
{
	if (sc->rng_initialized) {
		hwrng_unregister(&sc->rng);
		sc->rng_initialized = false;
	}
}
