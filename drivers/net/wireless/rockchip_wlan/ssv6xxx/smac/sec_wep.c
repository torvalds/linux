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

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/skbuff.h>
#include <linux/mm.h>
#include <asm/string.h>
#include <net/lib80211.h>
#include <linux/crypto.h>
#include <linux/crc32.h>
#include "sec.h"
struct lib80211_wep_data
{
    u32 iv;
    u8 key[WEP_KEY_LEN + 1];
    u8 key_len;
    u8 key_idx;
    struct crypto_blkcipher *tx_tfm;
    struct crypto_blkcipher *rx_tfm;
};
static void *lib80211_wep_init (int keyidx)
{
    struct lib80211_wep_data *priv;
    priv = kzalloc (sizeof (*priv), GFP_ATOMIC);
    if (priv == NULL)
        goto fail;
    priv->key_idx = keyidx;
    priv->tx_tfm = crypto_alloc_blkcipher ("ecb(arc4)", 0, CRYPTO_ALG_ASYNC);
    if (IS_ERR (priv->tx_tfm))
    {
        priv->tx_tfm = NULL;
        goto fail;
    }
    priv->rx_tfm = crypto_alloc_blkcipher ("ecb(arc4)", 0, CRYPTO_ALG_ASYNC);
    if (IS_ERR (priv->rx_tfm))
    {
        priv->rx_tfm = NULL;
        goto fail;
    }
    get_random_bytes (&priv->iv, 4);
    return priv;
fail:
    if (priv)
    {
        if (priv->tx_tfm)
            crypto_free_blkcipher (priv->tx_tfm);
        if (priv->rx_tfm)
            crypto_free_blkcipher (priv->rx_tfm);
        kfree (priv);
    }
    return NULL;
}
static void lib80211_wep_deinit (void *priv)
{
    struct lib80211_wep_data *_priv = priv;
    if (_priv)
    {
        if (_priv->tx_tfm)
            crypto_free_blkcipher (_priv->tx_tfm);
        if (_priv->rx_tfm)
            crypto_free_blkcipher (_priv->rx_tfm);
    }
    kfree (priv);
}
static int lib80211_wep_build_iv (struct sk_buff *skb, int hdr_len,
                                  u8 *key, int keylen, void *priv)
{
    struct lib80211_wep_data *wep = priv;
    u32 klen;
    u8 *pos;
    if (skb_headroom (skb) < 4 || skb->len < hdr_len)
        return -1;
    pos = skb_push (skb, 4);
    memmove (pos, pos + 4, hdr_len);
    pos += hdr_len;
    klen = 3 + wep->key_len;
    wep->iv++;
    if ((wep->iv & 0xff00) == 0xff00)
    {
        u8 B = (wep->iv >> 16) & 0xff;
        if (B >= 3 && B < klen)
            wep->iv += 0x0100;
    }
    *pos++ = (wep->iv >> 16) & 0xff;
    *pos++ = (wep->iv >> 8) & 0xff;
    *pos++ = wep->iv & 0xff;
    *pos++ = wep->key_idx << 6;
    return 0;
}
static int lib80211_wep_encrypt (struct sk_buff *skb, int hdr_len, void *priv)
{
    struct lib80211_wep_data *wep = priv;
    struct blkcipher_desc desc = {.tfm = wep->tx_tfm};
    u32 crc, klen, len;
    u8 *pos, *icv;
    struct scatterlist sg;
    u8 key[WEP_KEY_LEN + 3];
    if (skb_tailroom (skb) < 4)
    {
        printk("####%s: too few tailroom\n", __FUNCTION__);
        return -1;
    }
    if (lib80211_wep_build_iv (skb, hdr_len, NULL, 0, priv))
    {
        printk("####%s: build iv failure\n", __FUNCTION__);
        return -1;
    }
    skb_copy_from_linear_data_offset (skb, hdr_len, key, 3);
    memcpy (key + 3, wep->key, wep->key_len);
    len = skb->len - hdr_len - 4;
    pos = skb->data + hdr_len + 4;
    klen = 3 + wep->key_len;
    crc = ~crc32_le (~0, pos, len);
    icv = skb_put (skb, 4);
    icv[0] = crc;
    icv[1] = crc >> 8;
    icv[2] = crc >> 16;
    icv[3] = crc >> 24;
    crypto_blkcipher_setkey (wep->tx_tfm, key, klen);
    sg_init_one (&sg, pos, len + 4);
    return crypto_blkcipher_encrypt (&desc, &sg, &sg, len + 4);
}
static int lib80211_wep_decrypt (struct sk_buff *skb, int hdr_len, void *priv)
{
    struct lib80211_wep_data *wep = priv;
    struct blkcipher_desc desc = {.tfm = wep->rx_tfm};
    u32 crc, klen, plen;
    u8 key[WEP_KEY_LEN + 3];
    u8 keyidx, *pos, icv[4], *pos2;
    struct scatterlist sg;
    if (skb->len < hdr_len + 8)
    {
        printk ("%s::skb->len = %d\n", __FUNCTION__, skb->len);
        return -1;
    }
    pos = skb->data + hdr_len;
    key[0] = *pos++;
    key[1] = *pos++;
    key[2] = *pos++;
    keyidx = *pos++ >> 6;
    if (keyidx != wep->key_idx)
        return -1;
    klen = 3 + wep->key_len;
    memcpy (key + 3, wep->key, wep->key_len);
    plen = skb->len - hdr_len - 8;
    crypto_blkcipher_setkey (wep->rx_tfm, key, klen);
    sg_init_one (&sg, pos, plen + 4);
    if (crypto_blkcipher_decrypt (&desc, &sg, &sg, plen + 4))
        return -7;
    crc = ~crc32_le (~0, pos, plen);
    icv[0] = crc;
    icv[1] = crc >> 8;
    icv[2] = crc >> 16;
    icv[3] = crc >> 24;
    pos2 = (pos + plen);
    if (memcmp (icv, pos + plen, 4) != 0)
    {
        return -2;
    }
    memmove (skb->data + 4, skb->data, hdr_len);
    skb_pull (skb, 4);
    skb_trim (skb, skb->len - 4);
    return 0;
}
static int lib80211_wep_set_key (void *key, int len, u8 * seq, void *priv)
{
    struct lib80211_wep_data *wep = priv;
    if (len < 0 || len > WEP_KEY_LEN)
        return -1;
    memcpy (wep->key, key, len);
    wep->key_len = len;
    return 0;
}
static int lib80211_wep_get_key (void *key, int len, u8 * seq, void *priv)
{
    struct lib80211_wep_data *wep = priv;
    if (len < wep->key_len)
        return -1;
    memcpy (key, wep->key, wep->key_len);
    return wep->key_len;
}
static char *lib80211_wep_print_stats (char *p, void *priv)
{
    struct lib80211_wep_data *wep = priv;
    p += sprintf (p, "key[%d] alg=WEP len=%d\n", wep->key_idx, wep->key_len);
    return p;
}
static struct ssv_crypto_ops ssv_crypt_wep = {
    .name = "WEP",
    .init = lib80211_wep_init,
    .deinit = lib80211_wep_deinit,
    .encrypt_mpdu = lib80211_wep_encrypt,
    .decrypt_mpdu = lib80211_wep_decrypt,
    .encrypt_msdu = NULL,
    .decrypt_msdu = NULL,
    .set_key = lib80211_wep_set_key,
    .get_key = lib80211_wep_get_key,
    .print_stats = lib80211_wep_print_stats,
    .extra_mpdu_prefix_len = 4,
    .extra_mpdu_postfix_len = 4,
#ifdef MULTI_THREAD_ENCRYPT
 .encrypt_prepare = NULL,
    .decrypt_prepare = NULL,
#endif
};
struct ssv_crypto_ops *get_crypto_wep_ops (void)
{
    return &ssv_crypt_wep;
}
