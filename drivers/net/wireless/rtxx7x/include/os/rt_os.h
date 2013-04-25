/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifndef _RT_OS_H_
#define _RT_OS_H_


#ifdef LINUX
#if WIRELESS_EXT <= 11
#ifndef SIOCDEVPRIVATE
#define SIOCDEVPRIVATE                              0x8BE0
#endif
#define SIOCIWFIRSTPRIV								SIOCDEVPRIVATE
#endif
#endif /* LINUX */


#ifdef CONFIG_STA_SUPPORT
#define RT_PRIV_IOCTL							(SIOCIWFIRSTPRIV + 0x01) /* Sync. with AP for wsc upnp daemon */
#define RTPRIV_IOCTL_SET							(SIOCIWFIRSTPRIV + 0x02)

#ifdef DBG
#define RTPRIV_IOCTL_BBP                            (SIOCIWFIRSTPRIV + 0x03)
#define RTPRIV_IOCTL_MAC                            (SIOCIWFIRSTPRIV + 0x05)

#ifdef RTMP_RF_RW_SUPPORT
#define RTPRIV_IOCTL_RF                             (SIOCIWFIRSTPRIV + 0x13)  /* edit by johnli, fix read rf register problem */
#endif /* RTMP_RF_RW_SUPPORT */
#define RTPRIV_IOCTL_E2P                            (SIOCIWFIRSTPRIV + 0x07)
#endif /* DBG */

#ifdef RALINK_ATE
#ifdef RALINK_QA
#define RTPRIV_IOCTL_ATE							(SIOCIWFIRSTPRIV + 0x08)
#endif /* RALINK_QA */
#endif /* RALINK_ATE */

#define RTPRIV_IOCTL_STATISTICS                     (SIOCIWFIRSTPRIV + 0x09)
#define RTPRIV_IOCTL_ADD_PMKID_CACHE                (SIOCIWFIRSTPRIV + 0x0A)
#define RTPRIV_IOCTL_RADIUS_DATA                    (SIOCIWFIRSTPRIV + 0x0C)
#define RTPRIV_IOCTL_GSITESURVEY					(SIOCIWFIRSTPRIV + 0x0D)
#define RT_PRIV_IOCTL_EXT							(SIOCIWFIRSTPRIV + 0x0E) /* Sync. with RT61 (for wpa_supplicant) */
#define RTPRIV_IOCTL_GET_MAC_TABLE					(SIOCIWFIRSTPRIV + 0x0F)
#define RTPRIV_IOCTL_GET_MAC_TABLE_STRUCT					(SIOCIWFIRSTPRIV + 0x1F)	/* modified by Red@Ralink, 2009/09/30 */

#define RTPRIV_IOCTL_SHOW							(SIOCIWFIRSTPRIV + 0x11)


#endif /* CONFIG_STA_SUPPORT */


#endif /* _RT_OS_H_ */
