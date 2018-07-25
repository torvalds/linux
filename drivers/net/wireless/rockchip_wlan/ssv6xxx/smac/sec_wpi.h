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

#ifndef WAPI_WPI_H
#define WAPI_WPI_H 
#define WAPI_KEYID_LEN 1
#define WAPI_RESERVD_LEN 1
#define WAPI_PN_LEN 16
#define WAPI_IV_LEN (WAPI_KEYID_LEN + WAPI_RESERVD_LEN + WAPI_PN_LEN)
#define WAPI_MIC_LEN 16
#define ADDID_LEN (ETH_ALEN + ETH_ALEN)
#define WAPI_IV_ICV_OFFSET (WAPI_IV_LEN + WAPI_MIC_LEN)
typedef enum {BFALSE = 0,
              BTRUE = 1
} BOOL_T;
typedef enum {TV_TRUE = 1,
              TV_FALSE = 2
} TRUTH_VALUE_T;
int lib80211_wpi_set_key(void *key, int len, u8 *seq, void *priv);
int lib80211_wpi_encrypt(struct sk_buff *mpdu, int hdr_len, void *priv);
int lib80211_wpi_decrypt(struct sk_buff *mpdu, int hdr_len, void *priv);
void *lib80211_wpi_init(int key_idx);
void lib80211_wpi_deinit(void *priv);
#ifdef MULTI_THREAD_ENCRYPT
int lib80211_wpi_encrypt_prepare(struct sk_buff *mpdu, int hdr_len, void *priv);
#endif
#endif
