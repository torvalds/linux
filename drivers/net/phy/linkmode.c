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
