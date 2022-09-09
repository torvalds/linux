/*
 * Copyright (c) 2013 Eugene Krasnikov <k.eugene.e@gmail.com>
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "wcn36xx.h"

#define WCN36XX_BMPS_FAIL_THREHOLD 3

int wcn36xx_pmc_enter_bmps_state(struct wcn36xx *wcn,
				 struct ieee80211_vif *vif)
{
	int ret = 0;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	/* TODO: Make sure the TX chain clean */
	ret = wcn36xx_smd_enter_bmps(wcn, vif);
	if (!ret) {
		wcn36xx_dbg(WCN36XX_DBG_PMC, "Entered BMPS\n");
		vif_priv->pw_state = WCN36XX_BMPS;
		vif_priv->bmps_fail_ct = 0;
		vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER;
	} else {
		/*
		 * One of the reasons why HW will not enter BMPS is because
		 * driver is trying to enter bmps before first beacon was
		 * received just after auth complete
		 */
		wcn36xx_err("Can not enter BMPS!\n");

		if (vif_priv->bmps_fail_ct++ == WCN36XX_BMPS_FAIL_THREHOLD) {
			ieee80211_connection_loss(vif);
			vif_priv->bmps_fail_ct = 0;
		}
	}
	return ret;
}

int wcn36xx_pmc_exit_bmps_state(struct wcn36xx *wcn,
				struct ieee80211_vif *vif)
{
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);

	if (WCN36XX_BMPS != vif_priv->pw_state) {
		/* Unbalanced call or last BMPS enter failed */
		wcn36xx_dbg(WCN36XX_DBG_PMC,
			    "Not in BMPS mode, no need to exit\n");
		return -EALREADY;
	}
	wcn36xx_smd_exit_bmps(wcn, vif);
	vif_priv->pw_state = WCN36XX_FULL_POWER;
	vif->driver_flags &= ~IEEE80211_VIF_BEACON_FILTER;
	return 0;
}

int wcn36xx_enable_keep_alive_null_packet(struct wcn36xx *wcn,
					  struct ieee80211_vif *vif)
{
	wcn36xx_dbg(WCN36XX_DBG_PMC, "%s\n", __func__);
	return wcn36xx_smd_keep_alive_req(wcn, vif,
					  WCN36XX_HAL_KEEP_ALIVE_NULL_PKT);
}
