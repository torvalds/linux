/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2019-2020  Realtek Corporation
 */
#ifndef __RTW89_UTIL_H__
#define __RTW89_UTIL_H__

#include "core.h"

#define RTW89_KEY_PN_0 GENMASK_ULL(7, 0)
#define RTW89_KEY_PN_1 GENMASK_ULL(15, 8)
#define RTW89_KEY_PN_2 GENMASK_ULL(23, 16)
#define RTW89_KEY_PN_3 GENMASK_ULL(31, 24)
#define RTW89_KEY_PN_4 GENMASK_ULL(39, 32)
#define RTW89_KEY_PN_5 GENMASK_ULL(47, 40)

#define rtw89_iterate_vifs_bh(rtwdev, iterator, data)                          \
	ieee80211_iterate_active_interfaces_atomic((rtwdev)->hw,               \
			IEEE80211_IFACE_ITER_NORMAL, iterator, data)

/* call this function with wiphy mutex is held */
#define rtw89_for_each_rtwvif(rtwdev, rtwvif)				       \
	list_for_each_entry(rtwvif, &(rtwdev)->rtwvifs_list, list)

/* Before adding rtwvif to list, we need to check if it already exist, beacase
 * in some case such as SER L2 happen during WoWLAN flow, calling reconfig
 * twice cause the list to be added twice.
 */
static inline bool rtw89_rtwvif_in_list(struct rtw89_dev *rtwdev,
					struct rtw89_vif *new)
{
	struct rtw89_vif *rtwvif;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		if (rtwvif == new)
			return true;

	return false;
}

/* The result of negative dividend and positive divisor is undefined, but it
 * should be one case of round-down or round-up. So, make it round-down if the
 * result is round-up.
 * Note: the maximum value of divisor is 0x7FFF_FFFF, because we cast it to
 *       signed value to make compiler to use signed divide instruction.
 */
static inline s32 s32_div_u32_round_down(s32 dividend, u32 divisor, s32 *remainder)
{
	s32 i_divisor = (s32)divisor;
	s32 i_remainder;
	s32 quotient;

	quotient = dividend / i_divisor;
	i_remainder = dividend % i_divisor;

	if (i_remainder < 0) {
		quotient--;
		i_remainder += i_divisor;
	}

	if (remainder)
		*remainder = i_remainder;
	return quotient;
}

static inline s32 s32_div_u32_round_closest(s32 dividend, u32 divisor)
{
	return s32_div_u32_round_down(dividend + divisor / 2, divisor, NULL);
}

static inline void ether_addr_copy_mask(u8 *dst, const u8 *src, u8 mask)
{
	int i;

	eth_zero_addr(dst);
	for (i = 0; i < ETH_ALEN; i++) {
		if (mask & BIT(i))
			dst[i] = src[i];
	}
}

static inline void ccmp_hdr2pn(s64 *pn, const u8 *hdr)
{
	*pn = u64_encode_bits(hdr[0], RTW89_KEY_PN_0) |
	      u64_encode_bits(hdr[1], RTW89_KEY_PN_1) |
	      u64_encode_bits(hdr[4], RTW89_KEY_PN_2) |
	      u64_encode_bits(hdr[5], RTW89_KEY_PN_3) |
	      u64_encode_bits(hdr[6], RTW89_KEY_PN_4) |
	      u64_encode_bits(hdr[7], RTW89_KEY_PN_5);
}

s32 rtw89_linear_to_db_quarter(u64 val);
s32 rtw89_linear_to_db(u64 val);
u64 rtw89_db_quarter_to_linear(s32 db);
u64 rtw89_db_to_linear(s32 db);
void rtw89_might_trailing_ellipsis(char *buf, size_t size, ssize_t used);

#endif
