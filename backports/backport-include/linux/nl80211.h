#ifndef __BACKPORT_LINUX_NL80211_H
#define __BACKPORT_LINUX_NL80211_H
#include_next <linux/nl80211.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
#define NL80211_FEATURE_SK_TX_STATUS 0
#endif

#endif /* __BACKPORT_LINUX_NL80211_H */
