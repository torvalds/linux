/* Exports from main to helper modules
 *
 * See copyright notice in main.c
 */
#ifndef _ORINOCO_MAIN_H_
#define _ORINOCO_MAIN_H_

#include <linux/ieee80211.h>
#include "orinoco.h"

/********************************************************************/
/* Compile time configuration and compatibility stuff               */
/********************************************************************/

/* We do this this way to avoid ifdefs in the actual code */
#ifdef WIRELESS_SPY
#define SPY_NUMBER(priv)	(priv->spy_data.spy_number)
#else
#define SPY_NUMBER(priv)	0
#endif /* WIRELESS_SPY */

/********************************************************************/

/* Export module parameter */
extern int force_monitor;

/* Forward declarations */
struct net_device;
struct work_struct;

void set_port_type(struct orinoco_private *priv);
int orinoco_commit(struct orinoco_private *priv);
void orinoco_reset(struct work_struct *work);

/* Information element helpers - find a home for these... */
static inline u8 *orinoco_get_ie(u8 *data, size_t len,
				 enum ieee80211_eid eid)
{
	u8 *p = data;
	while ((p + 2) < (data + len)) {
		if (p[0] == eid)
			return p;
		p += p[1] + 2;
	}
	return NULL;
}

#define WPA_OUI_TYPE	"\x00\x50\xF2\x01"
#define WPA_SELECTOR_LEN 4
static inline u8 *orinoco_get_wpa_ie(u8 *data, size_t len)
{
	u8 *p = data;
	while ((p + 2 + WPA_SELECTOR_LEN) < (data + len)) {
		if ((p[0] == WLAN_EID_GENERIC) &&
		    (memcmp(&p[2], WPA_OUI_TYPE, WPA_SELECTOR_LEN) == 0))
			return p;
		p += p[1] + 2;
	}
	return NULL;
}

#endif /* _ORINOCO_MAIN_H_ */
