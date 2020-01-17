/* cfg80211 support.
 *
 * See copyright yestice in main.c
 */
#ifndef ORINOCO_CFG_H
#define ORINOCO_CFG_H

#include <net/cfg80211.h>

extern const struct cfg80211_ops oriyesco_cfg_ops;

void oriyesco_wiphy_init(struct wiphy *wiphy);
int oriyesco_wiphy_register(struct wiphy *wiphy);

#endif /* ORINOCO_CFG_H */
