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

#ifndef SEC_H
#define SEC_H 
#include <linux/types.h>
#include <linux/ieee80211.h>
#include <net/mac80211.h>
#define CCMP_TK_LEN 16
#define TKIP_KEY_LEN 32
#define WEP_KEY_LEN 13
struct ssv_crypto_ops {
 const char *name;
 struct list_head list;
 void *(*init) (int keyidx);
 void (*deinit) (void *priv);
 int (*encrypt_mpdu) (struct sk_buff * skb, int hdr_len, void *priv);
 int (*decrypt_mpdu) (struct sk_buff * skb, int hdr_len, void *priv);
 int (*encrypt_msdu) (struct sk_buff * skb, int hdr_len, void *priv);
 int (*decrypt_msdu) (struct sk_buff * skb, int keyidx, int hdr_len,
        void *priv);
 int (*set_tx_pn) (u8 * seq, void *priv);
 int (*set_key) (void *key, int len, u8 * seq, void *priv);
 int (*get_key) (void *key, int len, u8 * seq, void *priv);
 char *(*print_stats) (char *p, void *priv);
 unsigned long (*get_flags) (void *priv);
 unsigned long (*set_flags) (unsigned long flags, void *priv);
#ifdef MULTI_THREAD_ENCRYPT
 int (*encrypt_prepare) (struct sk_buff * skb, int hdr_len, void *priv);
 int (*decrypt_prepare) (struct sk_buff * skb, int hdr_len, void *priv);
#endif
 int extra_mpdu_prefix_len, extra_mpdu_postfix_len;
 int extra_msdu_prefix_len, extra_msdu_postfix_len;
};
struct ssv_crypto_data {
    struct ssv_crypto_ops *ops;
    void *priv;
    #ifdef HAS_CRYPTO_LOCK
    rwlock_t lock;
    #endif
};
struct ssv_crypto_ops *get_crypto_ccmp_ops(void);
struct ssv_crypto_ops *get_crypto_tkip_ops(void);
struct ssv_crypto_ops *get_crypto_wep_ops(void);
#ifdef CONFIG_SSV_WAPI
struct ssv_crypto_ops *get_crypto_wpi_ops(void);
#endif
#endif
