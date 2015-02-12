/*
 * Linux cfg80211 Vendor Extension Code
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: wl_cfgvendor.h 455257 2014-02-20 08:10:24Z $
 */

/*
 * New vendor interface additon to nl80211/cfg80211 to allow vendors
 * to implement proprietary features over the cfg80211 stack.
 */

#ifndef _wl_cfgvendor_h_
#define _wl_cfgvendor_h_

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 0)) && !defined(VENDOR_EXT_SUPPORT)
#define VENDOR_EXT_SUPPORT
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 0) && !VENDOR_EXT_SUPPORT */

enum wl_vendor_event {
	BRCM_VENDOR_EVENT_UNSPEC,
	BRCM_VENDOR_EVENT_PRIV_STR
};

/* Capture the BRCM_VENDOR_SUBCMD_PRIV_STRINGS* here */
#define BRCM_VENDOR_SCMD_CAPA	"cap"

#ifdef VENDOR_EXT_SUPPORT
extern int cfgvendor_attach(struct wiphy *wiphy);
extern int cfgvendor_detach(struct wiphy *wiphy);
#else
static INLINE int cfgvendor_attach(struct wiphy *wiphy) { return 0; }
static INLINE int cfgvendor_detach(struct wiphy *wiphy) { return 0; }
#endif /*  VENDOR_EXT_SUPPORT */

#endif /* _wl_cfgvendor_h_ */
