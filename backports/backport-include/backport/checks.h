#ifndef __BACKPORT_CHECKS
#define __BACKPORT_CHECKS

#if defined(CONFIG_MAC80211) && defined(CPTCFG_MAC80211)
#error "You must not have mac80211 built into your kernel if you want to enable it"
#endif

#if defined(CONFIG_CFG80211) && defined(CPTCFG_CFG80211)
#error "You must not have cfg80211 built into your kernel if you want to enable it"
#endif

#endif /* __BACKPORT_CHECKS */
