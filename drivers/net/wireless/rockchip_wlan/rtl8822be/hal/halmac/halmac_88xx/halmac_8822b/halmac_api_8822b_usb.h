/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _HALMAC_API_8822B_USB_H_
#define _HALMAC_API_8822B_USB_H_

#include "../../halmac_2_platform.h"
#include "../../halmac_type.h"

HALMAC_RET_STATUS
halmac_mac_power_switch_8822b_usb(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_MAC_POWER halmac_power
);
#endif/* _HALMAC_API_8822B_USB_H_ */
