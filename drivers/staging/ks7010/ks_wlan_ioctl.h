/*
 *   Driver for KeyStream 11b/g wireless LAN
 *   
 *   Copyright (c) 2005-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 */

#ifndef _KS_WLAN_IOCTL_H
#define _KS_WLAN_IOCTL_H

#include <linux/wireless.h>
/* The low order bit identify a SET (0) or a GET (1) ioctl.  */

/*					SIOCIWFIRSTPRIV+0 */
/* former KS_WLAN_GET_DRIVER_VERSION	SIOCIWFIRSTPRIV+1 */
/*					SIOCIWFIRSTPRIV+2 */
#define KS_WLAN_GET_FIRM_VERSION	SIOCIWFIRSTPRIV+3
#ifdef WPS
#define KS_WLAN_SET_WPS_ENABLE 		SIOCIWFIRSTPRIV+4
#define KS_WLAN_GET_WPS_ENABLE 		SIOCIWFIRSTPRIV+5
#define KS_WLAN_SET_WPS_PROBE_REQ	SIOCIWFIRSTPRIV+6
#endif
#define KS_WLAN_GET_EEPROM_CKSUM	SIOCIWFIRSTPRIV+7
#define KS_WLAN_SET_PREAMBLE		SIOCIWFIRSTPRIV+8
#define KS_WLAN_GET_PREAMBLE		SIOCIWFIRSTPRIV+9
#define KS_WLAN_SET_POWER_SAVE		SIOCIWFIRSTPRIV+10
#define KS_WLAN_GET_POWER_SAVE		SIOCIWFIRSTPRIV+11
#define KS_WLAN_SET_SCAN_TYPE		SIOCIWFIRSTPRIV+12
#define KS_WLAN_GET_SCAN_TYPE		SIOCIWFIRSTPRIV+13
#define KS_WLAN_SET_RX_GAIN		SIOCIWFIRSTPRIV+14
#define KS_WLAN_GET_RX_GAIN		SIOCIWFIRSTPRIV+15
#define KS_WLAN_HOSTT			SIOCIWFIRSTPRIV+16	/* unused */
//#define KS_WLAN_SET_REGION            SIOCIWFIRSTPRIV+17
#define KS_WLAN_SET_BEACON_LOST		SIOCIWFIRSTPRIV+18
#define KS_WLAN_GET_BEACON_LOST		SIOCIWFIRSTPRIV+19

#define KS_WLAN_SET_TX_GAIN		SIOCIWFIRSTPRIV+20
#define KS_WLAN_GET_TX_GAIN		SIOCIWFIRSTPRIV+21

/* for KS7010 */
#define KS_WLAN_SET_PHY_TYPE		SIOCIWFIRSTPRIV+22
#define KS_WLAN_GET_PHY_TYPE		SIOCIWFIRSTPRIV+23
#define KS_WLAN_SET_CTS_MODE		SIOCIWFIRSTPRIV+24
#define KS_WLAN_GET_CTS_MODE		SIOCIWFIRSTPRIV+25
/*					SIOCIWFIRSTPRIV+26 */
/*					SIOCIWFIRSTPRIV+27 */
#define KS_WLAN_SET_SLEEP_MODE		SIOCIWFIRSTPRIV+28	/* sleep mode */
#define KS_WLAN_GET_SLEEP_MODE		SIOCIWFIRSTPRIV+29	/* sleep mode */
/*					SIOCIWFIRSTPRIV+30 */
/*					SIOCIWFIRSTPRIV+31 */

#ifdef __KERNEL__

#include "ks_wlan.h"
#include <linux/netdevice.h>

int ks_wlan_read_config_file(struct ks_wlan_private *priv);
int ks_wlan_setup_parameter(struct ks_wlan_private *priv,
		             unsigned int commit_flag);

#endif /* __KERNEL__ */

#endif /* _KS_WLAN_IOCTL_H */
