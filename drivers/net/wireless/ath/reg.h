/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef ATH_REGISTERS_H
#define ATH_REGISTERS_H

#define AR_MIBC			0x0040
#define AR_MIBC_COW		0x00000001
#define AR_MIBC_FMC		0x00000002
#define AR_MIBC_CMC		0x00000004
#define AR_MIBC_MCS		0x00000008

#define AR_STA_ID0		0x8000
#define AR_STA_ID1		0x8004
#define AR_STA_ID1_SADH_MASK	0x0000ffff

/*
 * BSSID mask registers. See ath_hw_set_bssid_mask()
 * for detailed documentation about these registers.
 */
#define AR_BSSMSKL		0x80e0
#define AR_BSSMSKU		0x80e4

#define AR_TFCNT		0x80ec
#define AR_RFCNT		0x80f0
#define AR_RCCNT		0x80f4
#define AR_CCCNT		0x80f8

#define AR_KEYTABLE_0           0x8800
#define AR_KEYTABLE(_n)         (AR_KEYTABLE_0 + ((_n)*32))
#define AR_KEY_CACHE_SIZE       128
#define AR_RSVD_KEYTABLE_ENTRIES 4
#define AR_KEY_TYPE             0x00000007
#define AR_KEYTABLE_TYPE_40     0x00000000
#define AR_KEYTABLE_TYPE_104    0x00000001
#define AR_KEYTABLE_TYPE_128    0x00000003
#define AR_KEYTABLE_TYPE_TKIP   0x00000004
#define AR_KEYTABLE_TYPE_AES    0x00000005
#define AR_KEYTABLE_TYPE_CCM    0x00000006
#define AR_KEYTABLE_TYPE_CLR    0x00000007
#define AR_KEYTABLE_ANT         0x00000008
#define AR_KEYTABLE_VALID       0x00008000
#define AR_KEYTABLE_KEY0(_n)    (AR_KEYTABLE(_n) + 0)
#define AR_KEYTABLE_KEY1(_n)    (AR_KEYTABLE(_n) + 4)
#define AR_KEYTABLE_KEY2(_n)    (AR_KEYTABLE(_n) + 8)
#define AR_KEYTABLE_KEY3(_n)    (AR_KEYTABLE(_n) + 12)
#define AR_KEYTABLE_KEY4(_n)    (AR_KEYTABLE(_n) + 16)
#define AR_KEYTABLE_TYPE(_n)    (AR_KEYTABLE(_n) + 20)
#define AR_KEYTABLE_MAC0(_n)    (AR_KEYTABLE(_n) + 24)
#define AR_KEYTABLE_MAC1(_n)    (AR_KEYTABLE(_n) + 28)

#endif /* ATH_REGISTERS_H */
