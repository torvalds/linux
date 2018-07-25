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

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <asm/string.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <linux/crypto.h>
#include <linux/module.h>
#include "sec.h"
#define PRINT_DEBUG 0
#define AES_BLOCK_LEN 16
#define CCMP_HDR_LEN 8
#define CCMP_MIC_LEN 8
#define CCMP_PN_LEN 6
#ifdef MULTI_THREAD_ENCRYPT
int prepare_mask = 0x0b0e0e0f;
#endif
struct lib80211_ccmp_data {
 u8 key[CCMP_TK_LEN];
 int key_set;
 u8 tx_pn[CCMP_PN_LEN];
 u8 rx_pn[CCMP_PN_LEN];
#ifdef MULTI_THREAD_ENCRYPT
    u8 pre_rx_pn[CCMP_PN_LEN];
#endif
 u32 dot11RSNAStatsCCMPFormatErrors;
 u32 dot11RSNAStatsCCMPReplays;
 u32 dot11RSNAStatsCCMPDecryptErrors;
 int key_idx;
 struct crypto_cipher *tfm;
#ifndef MULTI_THREAD_ENCRYPT
 u8 tx_b0[AES_BLOCK_LEN], tx_b[AES_BLOCK_LEN],
     tx_e[AES_BLOCK_LEN], tx_s0[AES_BLOCK_LEN];
    u8 rx_b0[AES_BLOCK_LEN], rx_b[AES_BLOCK_LEN], rx_a[AES_BLOCK_LEN];
#else
 u8 *tx_b0, *tx_b, *tx_e, *tx_s0;
    u8 *rx_b0, *rx_b, *rx_a;
#endif
};
static inline void lib80211_ccmp_aes_encrypt(struct crypto_cipher *tfm,
           const u8 pt[16], u8 ct[16])
{
 crypto_cipher_encrypt_one(tfm, ct, pt);
}
static void *lib80211_ccmp_init(int key_idx)
{
 struct lib80211_ccmp_data *priv;
 const char *cipher_name = "aes";
#ifdef MULTI_THREAD_ENCRYPT
    unsigned int buf_size = num_present_cpus()*AES_BLOCK_LEN*sizeof(u8);
#endif
 priv = kzalloc(sizeof(*priv), GFP_ATOMIC);
 if (priv == NULL)
  goto fail;
 priv->key_idx = key_idx;
 priv->tfm = crypto_alloc_cipher(cipher_name, 0, CRYPTO_ALG_ASYNC);
 if (IS_ERR(priv->tfm)) {
         printk(KERN_ERR "Failed to allocate cipher %s\n", cipher_name);
  priv->tfm = NULL;
  goto fail;
 }
 else
 {
     printk(KERN_ERR "Found %s in driver %s (M %s).\n",
             priv->tfm->base.__crt_alg->cra_name,
             priv->tfm->base.__crt_alg->cra_driver_name,
             priv->tfm->base.__crt_alg->cra_module->name);
 }
#ifdef MULTI_THREAD_ENCRYPT
        priv->tx_b0 = priv->tx_b = priv->tx_e = priv->tx_s0 = NULL;
        priv->tx_b0 = (u8 *)kzalloc(buf_size, GFP_ATOMIC);
        priv->tx_b = (u8 *)kzalloc(buf_size, GFP_ATOMIC);
        priv->tx_e = (u8 *)kzalloc(buf_size, GFP_ATOMIC);
        priv->tx_s0 = (u8 *)kzalloc(buf_size, GFP_ATOMIC);
        priv->rx_b0 = (u8 *)kzalloc(buf_size, GFP_ATOMIC);
        priv->rx_b = (u8 *)kzalloc(buf_size, GFP_ATOMIC);
        priv->rx_a = (u8 *)kzalloc(buf_size, GFP_ATOMIC);
        if( (priv->tx_b0 == NULL) || (priv->tx_b == NULL) || (priv->tx_e == NULL) ||
                (priv->tx_s0 == NULL) ||(priv->rx_b0 == NULL) || (priv->rx_b == NULL) || (priv->rx_a == NULL) )
        {
            printk("#######fail to create memory for ccmp!!!\n");
            goto fail;
        }
#endif
 return priv;
      fail:
 if (priv) {
  if (priv->tfm)
   crypto_free_cipher(priv->tfm);
#ifdef MULTI_THREAD_ENCRYPT
        if(priv->tx_b0 != NULL)
            kfree(priv->tx_b0);
        if(priv->tx_b != NULL)
            kfree(priv->tx_b);
        if(priv->tx_e != NULL)
            kfree(priv->tx_e);
        if(priv->tx_s0 != NULL)
            kfree(priv->tx_s0);
        if(priv->rx_b0 != NULL)
            kfree(priv->rx_b0);
        if(priv->rx_b != NULL)
            kfree(priv->rx_b);
        if(priv->rx_a != NULL)
            kfree(priv->rx_a);
#endif
  kfree(priv);
 }
 return NULL;
}
static void lib80211_ccmp_deinit(void *priv)
{
 struct lib80211_ccmp_data *_priv = priv;
 if (_priv && _priv->tfm)
  crypto_free_cipher(_priv->tfm);
#ifdef MULTI_THREAD_ENCRYPT
    if(_priv->tx_b0 != NULL)
        kfree(_priv->tx_b0);
    if(_priv->tx_b != NULL)
        kfree(_priv->tx_b);
    if(_priv->tx_e != NULL)
        kfree(_priv->tx_e);
    if(_priv->tx_s0 != NULL)
        kfree(_priv->tx_s0);
    if(_priv->rx_b0 != NULL)
        kfree(_priv->rx_b0);
    if(_priv->rx_b != NULL)
        kfree(_priv->rx_b);
    if(_priv->rx_a != NULL)
        kfree(_priv->rx_a);
#endif
 kfree(priv);
}
static inline void xor_block(u8 * b, u8 * a, size_t len)
{
 int i;
 for (i = 0; i < len; i++)
  b[i] ^= a[i];
}
static void ccmp_init_blocks(struct crypto_cipher *tfm,
        struct ieee80211_hdr *hdr,
        u8 * pn, size_t dlen, u8 * b0, u8 * auth, u8 * s0)
{
 u8 *pos, qc = 0;
 size_t aad_len;
 int a4_included, qc_included;
 u8 aad[2 * AES_BLOCK_LEN];
 a4_included = ieee80211_has_a4(hdr->frame_control);
 qc_included = ieee80211_is_data_qos(hdr->frame_control);
 aad_len = 22;
 if (a4_included)
  aad_len += 6;
 if (qc_included) {
  pos = (u8 *) & hdr->addr4;
  if (a4_included)
   pos += 6;
  qc = *pos & 0x0f;
  aad_len += 2;
 }
 b0[0] = 0x59;
 b0[1] = qc;
 memcpy(b0 + 2, hdr->addr2, ETH_ALEN);
 memcpy(b0 + 8, pn, CCMP_PN_LEN);
 b0[14] = (dlen >> 8) & 0xff;
 b0[15] = dlen & 0xff;
 pos = (u8 *) hdr;
 aad[0] = 0;
 aad[1] = aad_len & 0xff;
 aad[2] = pos[0] & 0x8f;
 aad[3] = pos[1] & 0xc7;
 memcpy(aad + 4, hdr->addr1, 3 * ETH_ALEN);
 pos = (u8 *) & hdr->seq_ctrl;
 aad[22] = pos[0] & 0x0f;
 aad[23] = 0;
 memset(aad + 24, 0, 8);
 if (a4_included)
  memcpy(aad + 24, hdr->addr4, ETH_ALEN);
 if (qc_included) {
  aad[a4_included ? 30 : 24] = qc;
 }
 lib80211_ccmp_aes_encrypt(tfm, b0, auth);
 xor_block(auth, aad, AES_BLOCK_LEN);
 lib80211_ccmp_aes_encrypt(tfm, auth, auth);
 xor_block(auth, &aad[AES_BLOCK_LEN], AES_BLOCK_LEN);
 lib80211_ccmp_aes_encrypt(tfm, auth, auth);
 b0[0] &= 0x07;
 b0[14] = b0[15] = 0;
 lib80211_ccmp_aes_encrypt(tfm, b0, s0);
}
static int lib80211_ccmp_hdr(struct sk_buff *skb, int hdr_len,
         u8 *aeskey, int keylen, void *priv)
{
 struct lib80211_ccmp_data *key = priv;
 int i;
 u8 *pos;
 if (skb_headroom(skb) < CCMP_HDR_LEN || skb->len < hdr_len)
  return -1;
 if (aeskey != NULL && keylen >= CCMP_TK_LEN)
  memcpy(aeskey, key->key, CCMP_TK_LEN);
 pos = skb_push(skb, CCMP_HDR_LEN);
 memmove(pos, pos + CCMP_HDR_LEN, hdr_len);
 pos += hdr_len;
 i = CCMP_PN_LEN - 1;
 while (i >= 0) {
  key->tx_pn[i]++;
  if (key->tx_pn[i] != 0)
   break;
  i--;
 }
 *pos++ = key->tx_pn[5];
 *pos++ = key->tx_pn[4];
 *pos++ = 0;
 *pos++ = (key->key_idx << 6) | (1 << 5) ;
 *pos++ = key->tx_pn[3];
 *pos++ = key->tx_pn[2];
 *pos++ = key->tx_pn[1];
 *pos++ = key->tx_pn[0];
 return CCMP_HDR_LEN;
}
static int lib80211_ccmp_encrypt(struct sk_buff *skb, int hdr_len, void *priv)
{
 struct lib80211_ccmp_data *key = priv;
 int data_len, i, blocks, last, len;
 u8 *pos, *mic;
 struct ieee80211_hdr *hdr;
#ifndef MULTI_THREAD_ENCRYPT
 u8 *b0 = key->tx_b0;
 u8 *b = key->tx_b;
 u8 *e = key->tx_e;
 u8 *s0 = key->tx_s0;
 int ret;
#else
 unsigned int offset = smp_processor_id()*AES_BLOCK_LEN*sizeof(u8);
 u8 *b0 = (key->tx_b0 + offset);
 u8 *b = (key->tx_b + offset);
 u8 *e = (key->tx_e + offset);
 u8 *s0 = (key->tx_s0 + offset);
 u8 tmp_tx_pn[CCMP_PN_LEN], *ccmp_hdr_ptr = NULL;
 void *mask_ptr = NULL;
#endif
#ifndef MULTI_THREAD_ENCRYPT
 ret = skb_padto(skb, skb->len + CCMP_MIC_LEN);
    if (ret)
    {
        printk(KERN_ERR "Failed to extand skb for CCMP encryption.");
        return -1;
    }
 if (skb->len < hdr_len)
  return -1;
#endif
#ifndef MULTI_THREAD_ENCRYPT
    data_len = skb->len - hdr_len;
 len = lib80211_ccmp_hdr(skb, hdr_len, NULL, 0, priv);
 if (len < 0)
  return -1;
#else
 mask_ptr = (void *)((size_t)skb_end_pointer(skb) - sizeof(prepare_mask));
 if(memcmp(mask_ptr, &prepare_mask, sizeof(prepare_mask)) != 0)
 {
  printk("no prepared skb\n");
  return -1;
 }
 data_len = skb->len - (hdr_len + CCMP_HDR_LEN);
 ccmp_hdr_ptr = (u8 *)(skb->data + hdr_len);
 tmp_tx_pn[5] = ccmp_hdr_ptr[0];
 tmp_tx_pn[4] = ccmp_hdr_ptr[1];
 tmp_tx_pn[3] = ccmp_hdr_ptr[4];
 tmp_tx_pn[2] = ccmp_hdr_ptr[5];
 tmp_tx_pn[1] = ccmp_hdr_ptr[6];
 tmp_tx_pn[0] = ccmp_hdr_ptr[7];
#endif
 pos = skb->data + hdr_len + CCMP_HDR_LEN;
 hdr = (struct ieee80211_hdr *)skb->data;
#ifndef MULTI_THREAD_ENCRYPT
 ccmp_init_blocks(key->tfm, hdr, key->tx_pn, data_len, b0, b, s0);
#else
 ccmp_init_blocks(key->tfm, hdr, tmp_tx_pn, data_len, b0, b, s0);
#endif
 blocks = DIV_ROUND_UP(data_len, AES_BLOCK_LEN);
 last = data_len % AES_BLOCK_LEN;
 for (i = 1; i <= blocks; i++) {
  len = (i == blocks && last) ? last : AES_BLOCK_LEN;
  xor_block(b, pos, len);
  lib80211_ccmp_aes_encrypt(key->tfm, b, b);
  b0[14] = (i >> 8) & 0xff;
  b0[15] = i & 0xff;
  lib80211_ccmp_aes_encrypt(key->tfm, b0, e);
  xor_block(pos, e, len);
  pos += len;
 }
 mic = skb_put(skb, CCMP_MIC_LEN);
 for (i = 0; i < CCMP_MIC_LEN; i++)
  mic[i] = b[i] ^ s0[i];
 return 0;
}
static inline int ccmp_replay_check(u8 *pn_n, u8 *pn_o)
{
 u32 iv32_n, iv16_n;
 u32 iv32_o, iv16_o;
 iv32_n = (pn_n[0] << 24) | (pn_n[1] << 16) | (pn_n[2] << 8) | pn_n[3];
 iv16_n = (pn_n[4] << 8) | pn_n[5];
 iv32_o = (pn_o[0] << 24) | (pn_o[1] << 16) | (pn_o[2] << 8) | pn_o[3];
 iv16_o = (pn_o[4] << 8) | pn_o[5];
 if ((s32)iv32_n - (s32)iv32_o < 0 ||
     (iv32_n == iv32_o && iv16_n <= iv16_o))
  return 1;
 return 0;
}
static int lib80211_ccmp_decrypt(struct sk_buff *skb, int hdr_len, void *priv)
{
 struct lib80211_ccmp_data *key = priv;
 u8 keyidx, *pos;
 struct ieee80211_hdr *hdr;
#ifndef MULTI_THREAD_ENCRYPT
 u8 *b0 = key->rx_b0;
 u8 *b = key->rx_b;
 u8 *a = key->rx_a;
#else
    unsigned int offset = smp_processor_id()*AES_BLOCK_LEN*sizeof(u8);
    u8 *b0 = (key->rx_b0 + offset);
 u8 *b = (key->rx_b + offset);
 u8 *a = (key->rx_a + offset);
#endif
 u8 pn[6];
 int i, blocks, last, len;
 size_t data_len = skb->len - hdr_len - CCMP_HDR_LEN - CCMP_MIC_LEN;
 u8 *mic = skb->data + skb->len - CCMP_MIC_LEN;
#ifndef MULTI_THREAD_ENCRYPT
 if (skb->len < hdr_len + CCMP_HDR_LEN + CCMP_MIC_LEN) {
  key->dot11RSNAStatsCCMPFormatErrors++;
  return -1;
 }
#endif
 hdr = (struct ieee80211_hdr *)skb->data;
 pos = skb->data + hdr_len;
 keyidx = pos[3];
#ifndef MULTI_THREAD_ENCRYPT
 if (!(keyidx & (1 << 5))) {
  if (net_ratelimit()) {
   printk(KERN_DEBUG "CCMP: received packet without ExtIV"
          " flag from %pM (%02X)\n", hdr->addr2, keyidx);
  }
  key->dot11RSNAStatsCCMPFormatErrors++;
  return -2;
 }
 keyidx >>= 6;
 if (key->key_idx != keyidx) {
  printk(KERN_DEBUG "CCMP: RX tkey->key_idx=%d frame "
         "keyidx=%d priv=%p\n", key->key_idx, keyidx, priv);
  return -6;
 }
 if (!key->key_set) {
  if (net_ratelimit()) {
   printk(KERN_DEBUG "CCMP: received packet from %pM"
          " with keyid=%d that does not have a configured"
          " key\n", hdr->addr2, keyidx);
  }
  return -3;
 }
#endif
 pn[0] = pos[7];
 pn[1] = pos[6];
 pn[2] = pos[5];
 pn[3] = pos[4];
 pn[4] = pos[1];
 pn[5] = pos[0];
 pos += 8;
#if 0
 if (ccmp_replay_check(pn, key->rx_pn)) {
#ifdef CONFIG_LIB80211_DEBUG
  if (net_ratelimit())
        {
   printk(KERN_DEBUG "CCMP: replay detected: STA=%pM "
     "previous PN %02x%02x%02x%02x%02x%02x "
     "received PN %02x%02x%02x%02x%02x%02x\n",
     hdr->addr2,
     key->rx_pn[0], key->rx_pn[1], key->rx_pn[2],
     key->rx_pn[3], key->rx_pn[4], key->rx_pn[5],
     pn[0], pn[1], pn[2], pn[3], pn[4], pn[5]);
  }
#endif
  key->dot11RSNAStatsCCMPReplays++;
  return -4;
 }
#endif
 ccmp_init_blocks(key->tfm, hdr, pn, data_len, b0, a, b);
 xor_block(mic, b, CCMP_MIC_LEN);
 blocks = DIV_ROUND_UP(data_len, AES_BLOCK_LEN);
 last = data_len % AES_BLOCK_LEN;
 for (i = 1; i <= blocks; i++) {
  len = (i == blocks && last) ? last : AES_BLOCK_LEN;
  b0[14] = (i >> 8) & 0xff;
  b0[15] = i & 0xff;
  lib80211_ccmp_aes_encrypt(key->tfm, b0, b);
  xor_block(pos, b, len);
  xor_block(a, pos, len);
  lib80211_ccmp_aes_encrypt(key->tfm, a, a);
  pos += len;
 }
 if (memcmp(mic, a, CCMP_MIC_LEN) != 0) {
  if (net_ratelimit()) {
   printk(KERN_DEBUG "CCMP: decrypt failed: STA="
          "%pM\n", hdr->addr2);
  }
  key->dot11RSNAStatsCCMPDecryptErrors++;
  return -5;
 }
#ifndef MULTI_THREAD_ENCRYPT
 memcpy(key->rx_pn, pn, CCMP_PN_LEN);
#else
    if (!ccmp_replay_check(pn, key->rx_pn))
        memcpy(key->rx_pn, pn, CCMP_PN_LEN);
#endif
 memmove(skb->data + CCMP_HDR_LEN, skb->data, hdr_len);
 skb_pull(skb, CCMP_HDR_LEN);
 skb_trim(skb, skb->len - CCMP_MIC_LEN);
 return keyidx;
}
static int lib80211_ccmp_set_key(void *key, int len, u8 * seq, void *priv)
{
 struct lib80211_ccmp_data *data = priv;
 int keyidx;
 struct crypto_cipher *tfm = data->tfm;
#ifdef MULTI_THREAD_ENCRYPT
 u8 *tx_b0 = data->tx_b0;
 u8 *tx_b = data->tx_b;
 u8 *tx_e = data->tx_e;
 u8 *tx_s0 = data->tx_s0;
    u8 *rx_b0 = data->rx_b0;
 u8 *rx_b = data->rx_b;
 u8 *rx_a = data->rx_a;
#endif
 keyidx = data->key_idx;
 memset(data, 0, sizeof(*data));
 data->key_idx = keyidx;
 data->tfm = tfm;
 if (len == CCMP_TK_LEN) {
  memcpy(data->key, key, CCMP_TK_LEN);
  data->key_set = 1;
  if (seq) {
   data->rx_pn[0] = seq[5];
   data->rx_pn[1] = seq[4];
   data->rx_pn[2] = seq[3];
   data->rx_pn[3] = seq[2];
   data->rx_pn[4] = seq[1];
   data->rx_pn[5] = seq[0];
#ifdef MULTI_THREAD_ENCRYPT
            memcpy(data->pre_rx_pn, data->rx_pn, CCMP_PN_LEN);
#endif
  }
  crypto_cipher_setkey(data->tfm, data->key, CCMP_TK_LEN);
#ifdef MULTI_THREAD_ENCRYPT
  data->tx_b0 = tx_b0;
  data->tx_b = tx_b;
  data->tx_e = tx_e;
  data->tx_s0 = tx_s0;
        data->rx_b0 = rx_b0;
  data->rx_b = rx_b;
  data->rx_a = rx_a;
#endif
 } else if (len == 0)
  data->key_set = 0;
 else
  return -1;
 return 0;
}
static int lib80211_ccmp_get_key(void *key, int len, u8 * seq, void *priv)
{
 struct lib80211_ccmp_data *data = priv;
 if (len < CCMP_TK_LEN)
  return -1;
 if (!data->key_set)
  return 0;
 memcpy(key, data->key, CCMP_TK_LEN);
 if (seq) {
  seq[0] = data->tx_pn[5];
  seq[1] = data->tx_pn[4];
  seq[2] = data->tx_pn[3];
  seq[3] = data->tx_pn[2];
  seq[4] = data->tx_pn[1];
  seq[5] = data->tx_pn[0];
 }
 return CCMP_TK_LEN;
}
static int lib80211_ccmp_set_tx_pn(u8 * seq, void *priv)
{
 struct lib80211_ccmp_data *data = priv;
 if (seq) {
  data->tx_pn[0] = seq[0];
  data->tx_pn[1] = seq[1];
  data->tx_pn[2] = seq[2];
  data->tx_pn[3] = seq[3];
  data->tx_pn[4] = seq[4];
  data->tx_pn[5] = seq[5];
 }
 return 0;
}
static char *lib80211_ccmp_print_stats(char *p, void *priv)
{
 struct lib80211_ccmp_data *ccmp = priv;
 p += sprintf(p, "key[%d] alg=CCMP key_set=%d "
       "tx_pn=%02x%02x%02x%02x%02x%02x "
       "rx_pn=%02x%02x%02x%02x%02x%02x "
       "format_errors=%d replays=%d decrypt_errors=%d\n",
       ccmp->key_idx, ccmp->key_set,
       ccmp->tx_pn[0], ccmp->tx_pn[1], ccmp->tx_pn[2],
       ccmp->tx_pn[3], ccmp->tx_pn[4], ccmp->tx_pn[5],
       ccmp->rx_pn[0], ccmp->rx_pn[1], ccmp->rx_pn[2],
       ccmp->rx_pn[3], ccmp->rx_pn[4], ccmp->rx_pn[5],
       ccmp->dot11RSNAStatsCCMPFormatErrors,
       ccmp->dot11RSNAStatsCCMPReplays,
       ccmp->dot11RSNAStatsCCMPDecryptErrors);
 return p;
}
#ifdef MULTI_THREAD_ENCRYPT
static int lib80211_ccmp_encrypt_prepare (struct sk_buff * skb, int hdr_len, void *priv)
{
    int data_len, len, ret;
    void *ptr = NULL;
    if (skb_tailroom(skb) < CCMP_MIC_LEN)
    {
        ret = skb_padto(skb, skb->len + CCMP_MIC_LEN);
        if (ret != 0)
        {
            printk(KERN_ERR "Failed to extand skb for CCMP encryption, ret = %d.", ret);
            return -1;
        }
    }
    if (skb->len < hdr_len)
  return -1;
    data_len = skb->len - hdr_len;
    len = lib80211_ccmp_hdr(skb, hdr_len, NULL, 0, priv);
    if (len < 0)
        return -1;
    ptr = (void *)((size_t)skb_end_pointer(skb) - sizeof(prepare_mask));
    memcpy(ptr, &prepare_mask, sizeof(prepare_mask));
    return 0;
}
static int lib80211_ccmp_decrypt_prepare (struct sk_buff * skb, int hdr_len, void *priv)
{
    struct lib80211_ccmp_data *key = priv;
 u8 keyidx, *pos;
 struct ieee80211_hdr *hdr;
 u8 pn[6];
 if (skb->len < hdr_len + CCMP_HDR_LEN + CCMP_MIC_LEN)
    {
  key->dot11RSNAStatsCCMPFormatErrors++;
  return -1;
 }
 hdr = (struct ieee80211_hdr *)skb->data;
 pos = skb->data + hdr_len;
 keyidx = pos[3];
 if (!(keyidx & (1 << 5)))
    {
        {
   printk(KERN_DEBUG "CCMP: received packet without ExtIV"
          " flag from %pM (%02X)\n", hdr->addr2, keyidx);
  }
  key->dot11RSNAStatsCCMPFormatErrors++;
  return -2;
 }
 keyidx >>= 6;
 if (key->key_idx != keyidx)
    {
  printk(KERN_DEBUG "CCMP: RX tkey->key_idx=%d frame "
         "keyidx=%d priv=%p\n", key->key_idx, keyidx, priv);
  return -6;
 }
 if (!key->key_set)
    {
        {
   printk(KERN_DEBUG "CCMP: received packet from %pM"
          " with keyid=%d that does not have a configured"
          " key\n", hdr->addr2, keyidx);
  }
  return -3;
 }
 pn[0] = pos[7];
 pn[1] = pos[6];
 pn[2] = pos[5];
 pn[3] = pos[4];
 pn[4] = pos[1];
 pn[5] = pos[0];
#if 0
    if (ccmp_replay_check(pn, key->pre_rx_pn))
    {
#if 1
        {
   printk(KERN_DEBUG "CCMP: replay detected: STA=%pM "
     "previous PN %02x%02x%02x%02x%02x%02x "
     "received PN %02x%02x%02x%02x%02x%02x\n",
     hdr->addr2,
     key->rx_pn[0], key->rx_pn[1], key->rx_pn[2],
     key->rx_pn[3], key->rx_pn[4], key->rx_pn[5],
     pn[0], pn[1], pn[2], pn[3], pn[4], pn[5]);
  }
#endif
  key->dot11RSNAStatsCCMPReplays++;
  return -4;
 }
#endif
    memcpy(key->pre_rx_pn, pn, CCMP_PN_LEN);
    return 0;
}
#endif
static struct ssv_crypto_ops ssv_crypt_ccmp = {
 .name = "CCMP",
 .init = lib80211_ccmp_init,
 .deinit = lib80211_ccmp_deinit,
 .encrypt_mpdu = lib80211_ccmp_encrypt,
 .decrypt_mpdu = lib80211_ccmp_decrypt,
 .encrypt_msdu = NULL,
 .decrypt_msdu = NULL,
 .set_tx_pn = lib80211_ccmp_set_tx_pn,
 .set_key = lib80211_ccmp_set_key,
 .get_key = lib80211_ccmp_get_key,
 .print_stats = lib80211_ccmp_print_stats,
 .extra_mpdu_prefix_len = CCMP_HDR_LEN,
 .extra_mpdu_postfix_len = CCMP_MIC_LEN,
#ifdef MULTI_THREAD_ENCRYPT
 .encrypt_prepare = lib80211_ccmp_encrypt_prepare,
    .decrypt_prepare = lib80211_ccmp_decrypt_prepare,
#endif
};
struct ssv_crypto_ops *get_crypto_ccmp_ops(void)
{
    return &ssv_crypt_ccmp;
}
#if 0
static inline int ccmp_replay_check(u8 *pn_n, u8 *pn_o)
{
 u32 iv32_n, iv16_n;
 u32 iv32_o, iv16_o;
 iv32_n = (pn_n[5] << 24) | (pn_n[4] << 16) | (pn_n[3] << 8) | pn_n[2];
 iv16_n = (pn_n[1] << 8) | pn_n[0];
 iv32_o = (pn_o[5] << 24) | (pn_o[4] << 16) | (pn_o[3] << 8) | pn_o[2];
 iv16_o = (pn_o[1] << 8) | pn_o[0];
 if (((u32)iv32_n < (u32)iv32_o) ||
     (iv32_n == iv32_o && iv16_n <= iv16_o))
  return 1;
 return 0;
}
static void ccmp_special_blocks(struct sk_buff *skb, u8 *pn, u8 *scratch, int encrypted)
{
 u16 mask_fc;
 u8 a4_included=0, mgmt=0;
 u8 qos_tid;
 u8 *b_0, *aad;
 u16 data_len, len_a;
 unsigned int hdrlen;
 struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
 mask_fc = hdr->frame_control;
 b_0 = scratch + 3 * AES_BLOCK_LEN;
 aad = scratch + 4 * AES_BLOCK_LEN;
 if((mask_fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT)
  mgmt = 1;
 else
  mgmt = 0;
 mask_fc &= ~IEEE80211_FCTL_RETRY;
 mask_fc &= ~IEEE80211_FCTL_PM;
 mask_fc &= ~IEEE80211_FCTL_MOREDATA;
 if (!mgmt)
  mask_fc &= ~0x0070;
 hdrlen = ieee80211_hdrlen(hdr->frame_control);
 len_a = hdrlen - 2;
 if( (mask_fc & (IEEE80211_FCTL_FROMDS|IEEE80211_FCTL_TODS)) == (IEEE80211_FCTL_FROMDS|IEEE80211_FCTL_TODS))
  a4_included = 1;
 else
  a4_included = 0;
 if (ieee80211_is_data_qos(hdr->frame_control))
  qos_tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;
 else
  qos_tid = 0;
#if 0
 if ((mask_fc & (IEEE80211_FCTL_FTYPE | IEEE80211_STYPE_QOS_DATA)) == (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA))
 {
  if(a4_included)
   qos_tid = (*((u8 *)ppkt + ppkt->hdr_offset+30)) & IEEE80211_QOS_CTL_TID_MASK;
  else
   qos_tid = (*((u8 *)ppkt + ppkt->hdr_offset+24)) & IEEE80211_QOS_CTL_TID_MASK;
 }
 else
  qos_tid = 0;
#endif
 data_len = skb->len - hdrlen;
 if (encrypted)
 {
  data_len -= CCMP_MIC_LEN;
  data_len -= CCMP_HDR_LEN;
 }
 b_0[0] = 0x59;
 b_0[1] = qos_tid | (mgmt << 4);
 memcpy(&b_0[2], hdr->addr2, ETH_ALEN);
 memcpy(&b_0[8], pn, CCMP_PN_LEN);
 put_unaligned_be16(data_len, &b_0[14]);
 put_unaligned_be16(len_a, &aad[0]);
 put_unaligned(mask_fc, (__le16 *)&aad[2]);
 memcpy(&aad[4], &hdr->addr1, 3 * ETH_ALEN);
 aad[22] = *((u8 *) &hdr->seq_ctrl) & 0x0f;
 aad[23] = 0;
 if (a4_included) {
  memcpy(&aad[24], hdr->addr4, ETH_ALEN);
  aad[30] = qos_tid;
  aad[31] = 0;
 } else {
  memset(&aad[24], 0, ETH_ALEN + IEEE80211_QOS_CTL_LEN);
  aad[24] = qos_tid;
 }
}
static void ccmp_pn2hdr(u8 *hdr, int key_id, u8 *pn)
{
#if 0
 hdr[0] = pn[0];
 hdr[1] = pn[1];
 hdr[2] = 0;
 hdr[3] = 0x20 | (key_id << 6);
 hdr[4] = pn[2];
 hdr[5] = pn[3];
 hdr[6] = pn[4];
 hdr[7] = pn[5];
#endif
    hdr[0] = pn[5];
    hdr[1] = pn[4];
    hdr[2] = 0;
    hdr[3] = 0x20 | (key_id << 6);
    hdr[4] = pn[3];
    hdr[5] = pn[2];
    hdr[6] = pn[1];
    hdr[7] = pn[0];
}
#if 0
static void ccmp_hdr2pn(u8 *hdr, u8 *pn)
{
 pn[0] = hdr[0];
 pn[1] = hdr[1];
 pn[2] = hdr[4];
 pn[3] = hdr[5];
 pn[4] = hdr[6];
 pn[5] = hdr[7];
}
#endif
int ieee80211_crypto_ccmp_encrypt(struct sk_buff *skb, u8 *key, u8 keyidx, u8 *tx_pn)
{
 u8 *data;
 u32 data_len;
 u8 crypto_buf[6 * AES_BLOCK_LEN];
 struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
    u32 hdrlen = ieee80211_hdrlen(hdr->frame_control);
    u64 pn64;
    u8 pn[6];
    data_len = skb->len - hdrlen;
 data = ((u8*)skb->data)+hdrlen;
#ifdef SECURITY_DUMP
 fpga_dump(ppkt,"case-",key,16,0);
#endif
#if PRINT_DEBUG
 printk("CCMP encrypt: PN =             0x%02x%02x%02x%02x%02x%02x\n",tx_pn[5],tx_pn[4],tx_pn[3],tx_pn[2],tx_pn[1],tx_pn[0]);
#endif
    hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
#if 0
 frame = (u16*)((u8 *)ppkt + ppkt->hdr_offset);
 *frame |= IEEE80211_FCTL_PROTECTED;
#endif
 pn64 = (*(u64*)tx_pn)++;
 pn[5] = pn64;
 pn[4] = pn64 >> 8;
 pn[3] = pn64 >> 16;
 pn[2] = pn64 >> 24;
 pn[1] = pn64 >> 32;
 pn[0] = pn64 >> 40;
    ccmp_special_blocks(skb, pn, crypto_buf, 0);
    data = skb_push(skb, CCMP_HDR_LEN);
 memmove(data, data + CCMP_HDR_LEN, hdrlen);
 ccmp_pn2hdr(data+hdrlen, keyidx, pn);
 ieee80211_aes_ccm_encrypt(crypto_buf ,key , data+CCMP_HDR_LEN+hdrlen , data_len, skb_put(skb, CCMP_MIC_LEN));
#ifdef SECURITY_DUMP
 fpga_dump(ppkt,"case-",key,16,1);
#endif
 return true;
}
#endif
