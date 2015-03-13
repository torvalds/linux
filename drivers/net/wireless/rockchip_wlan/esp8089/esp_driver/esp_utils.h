/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _ESP_UTILS_H_
#define _ESP_UTILS_H_

#include "linux/types.h"
#include <linux/version.h>

#ifndef BIT
#define BIT(x) (0x1 << (x))
#endif

u32 esp_ieee2mhz(u8 chan);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
enum ieee80211_key_alg {
        ALG_WEP,
        ALG_TKIP,
        ALG_CCMP,
        ALG_AES_CMAC
};

int esp_cipher2alg(int cipher);

void esp_rx_checksum_test(struct sk_buff *skb);
void esp_gen_err_checksum(struct sk_buff *skb);

#endif /* NEW_KERNEL */

bool esp_is_ip_pkt(struct sk_buff *skb);

#endif
