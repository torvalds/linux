// SPDX-License-Identifier: GPL-2.0+
#include <linux/linkmode.h>

/**
 * linkmode_resolve_pause - resolve the allowable pause modes
 * @local_adv: local advertisement in ethtool format
 * @partner_adv: partner advertisement in ethtool format
 * @tx_pause: pointer to bool to indicate whether transmit pause should be
 * enabled.
 * @rx_pause: pointer to bool to indicate whether receive pause should be
 * enabled.
 *
 * Flow control is resolved according to our and the link partners
 * advertisements using the following drawn from the 802.3 specs:
 *  Local device  Link partner
 *  Pause AsymDir Pause AsymDir Result
 *    0     X       0     X     Disabled
 *    0     1       1     0     Disabled
 *    0     1       1     1     TX
 *    1     0       0     X     Disabled
 *    1     X       1     X     TX+RX
 *    1     1       0     1     RX
 */
void linkmode_resolve_pause(const unsigned long *local_adv,
			    const unsigned long *partner_adv,
			    bool *tx_pause, bool *rx_pause)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(m);

	linkmode_and(m, local_adv, partner_adv);
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT, m)) {
		*tx_pause = true;
		*rx_pause = true;
	} else if (linkmode_test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, m)) {
		*tx_pause = linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT,
					      partner_adv);
		*rx_pause = linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT,
					      local_adv);
	} else {
		*tx_pause = false;
		*rx_pause = false;
	}
}
EXPORT_SYMBOL_GPL(linkmode_resolve_pause);

/**
 * linkmode_set_pause - set the pause mode advertisement
 * @advertisement: advertisement in ethtool format
 * @tx: boolean from ethtool struct ethtool_pauseparam tx_pause member
 * @rx: boolean from ethtool struct ethtool_pauseparam rx_pause member
 *
 * Configure the advertised Pause and Asym_Pause bits according to the
 * capabilities of provided in @tx and @rx.
 *
 * We convert as follows:
 *  tx rx  Pause AsymDir
 *  0  0   0     0
 *  0  1   1     1
 *  1  0   0     1
 *  1  1   1     0
 *
 * Note: this translation from ethtool tx/rx notation to the advertisement
 * is actually very problematical. Here are some examples:
 *
 * For tx=0 rx=1, meaning transmit is unsupported, receive is supported:
 *
 *  Local device  Link partner
 *  Pause AsymDir Pause AsymDir Result
 *    1     1       1     0     TX + RX - but we have no TX support.
 *    1     1       0     1	Only this gives RX only
 *
 * For tx=1 rx=1, meaning we have the capability to transmit and receive
 * pause frames:
 *
 *  Local device  Link partner
 *  Pause AsymDir Pause AsymDir Result
 *    1     0       0     1     Disabled - but since we do support tx and rx,
 *				this should resolve to RX only.
 *
 * Hence, asking for:
 *  rx=1 tx=0 gives Pause+AsymDir advertisement, but we may end up
 *            resolving to tx+rx pause or only rx pause depending on
 *            the partners advertisement.
 *  rx=0 tx=1 gives AsymDir only, which will only give tx pause if
 *            the partners advertisement allows it.
 *  rx=1 tx=1 gives Pause only, which will only allow tx+rx pause
 *            if the other end also advertises Pause.
 */
void linkmode_set_pause(unsigned long *advertisement, bool tx, bool rx)
{
	linkmode_mod_bit(ETHTOOL_LINK_MODE_Pause_BIT, advertisement, rx);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, advertisement,
			 rx ^ tx);
}
EXPORT_SYMBOL_GPL(linkmode_set_pause);
