/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _WLAN_PARAM_H_
#define _WLAN_PARAM_H_

typedef struct {
  char ifname[32]; // wlan0, wl0.1
  char devmode[32]; // ap, sta
  // reserve
} wlan_param;

#endif


