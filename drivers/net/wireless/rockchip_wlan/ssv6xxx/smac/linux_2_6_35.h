/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LINUX_2_6_35_H_
#define _LINUX_2_6_35_H_ 
#include <net/mac80211.h>
enum ieee80211_ac_numbers {
 IEEE80211_AC_VO = 0,
 IEEE80211_AC_VI = 1,
 IEEE80211_AC_BE = 2,
 IEEE80211_AC_BK = 3,
};
#define IEEE80211_NUM_ACS 4
#define wiphy_info(wiphy,format,args...) \
 dev_info(&(wiphy)->dev, format, ##args)
#define IEEE80211_CONF_OFFCHANNEL (1<<30)
#define FIF_PROBE_REQ (1<<8)
#define BSS_CHANGED_SSID (1<<15)
#endif
