/*
 * Amlogic Meson HDMI Transmitter Driver
 * hdmitx driver-----------HDMI_TX
 * Copyright (C) 2013 Amlogic, Inc.
 * Author: zongdong.jiao@amlogic.com
 *
 * In order to get better HDMI TX compliance,
 * you can add special code here, such as clock configure.
 * 
 * Function hdmitx_special_operation() is called by
 * hdmitx_m3_set_dispmode() at the end
 *
 */

#ifndef __HDMI_TX_COMPLIANCE_H
#define __HDMI_TX_COMPLIANCE_H

#include "hdmi_info_global.h"
#include "hdmi_tx_module.h"

void hdmitx_special_handler_video(hdmitx_dev_t* hdmitx_device);
void hdmitx_special_handler_audio(hdmitx_dev_t* hdmitx_device);

// How to get RX's brand & product ?
// root@android:/ # cat /sys/class/amhdmitx/amhdmitx0/edid
// Receiver Brand Name: VSC
// Receiver Product Name: VX2433wm
// Then you will see VSC, ViewSonic's Abbr, and module is VX2433wm

///////////////////////////////////
// Brand: View Sonic
#define HDMI_RX_VIEWSONIC           "VSC"           // must be 3 characters
#define HDMI_RX_VIEWSONIC_MODEL     "VX2433wm"  // less than 13 characters

#endif

