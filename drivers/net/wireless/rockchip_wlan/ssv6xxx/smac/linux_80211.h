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

#ifndef _LINUX_80211_H_
#define _LINUX_80211_H_ 
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0)
#define INDEX_80211_BAND_2GHZ IEEE80211_BAND_2GHZ
#else
#define INDEX_80211_BAND_2GHZ NL80211_BAND_2GHZ
#endif
#endif
