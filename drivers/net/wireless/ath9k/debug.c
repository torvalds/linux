/*
 * Copyright (c) 2008 Atheros Communications Inc.
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

#include "core.h"

static unsigned int ath9k_debug = DBG_DEFAULT;
module_param_named(debug, ath9k_debug, uint, 0);

void DPRINTF(struct ath_softc *sc, int dbg_mask, const char *fmt, ...)
{
	if (!sc)
		return;

	if (sc->sc_debug.debug_mask & dbg_mask) {
		va_list args;

		va_start(args, fmt);
		printk(KERN_DEBUG "ath9k: ");
		vprintk(fmt, args);
		va_end(args);
	}
}

int ath9k_init_debug(struct ath_softc *sc)
{
	sc->sc_debug.debug_mask = ath9k_debug;

	sc->sc_debug.debugfs_root = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!sc->sc_debug.debugfs_root)
		goto err;

	sc->sc_debug.debugfs_phy = debugfs_create_dir(wiphy_name(sc->hw->wiphy),
						      sc->sc_debug.debugfs_root);
	if (!sc->sc_debug.debugfs_phy)
		goto err;

	return 0;
err:
	ath9k_exit_debug(sc);
	return -ENOMEM;
}

void ath9k_exit_debug(struct ath_softc *sc)
{
	debugfs_remove(sc->sc_debug.debugfs_phy);
	debugfs_remove(sc->sc_debug.debugfs_root);
}
