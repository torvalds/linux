#ifndef LINUX_B43_PPR_H_
#define LINUX_B43_PPR_H_

#include <linux/types.h>

#define B43_PPR_CCK_RATES_NUM		4
#define B43_PPR_OFDM_RATES_NUM		8
#define B43_PPR_MCS_RATES_NUM		8

#define B43_PPR_RATES_NUM	(B43_PPR_CCK_RATES_NUM +	\
				 B43_PPR_OFDM_RATES_NUM * 2 +	\
				 B43_PPR_MCS_RATES_NUM * 4)

struct b43_ppr_rates {
	u8 cck[B43_PPR_CCK_RATES_NUM];
	u8 ofdm[B43_PPR_OFDM_RATES_NUM];
	u8 ofdm_20_cdd[B43_PPR_OFDM_RATES_NUM];
	u8 mcs_20[B43_PPR_MCS_RATES_NUM]; /* SISO */
	u8 mcs_20_cdd[B43_PPR_MCS_RATES_NUM];
	u8 mcs_20_stbc[B43_PPR_MCS_RATES_NUM];
	u8 mcs_20_sdm[B43_PPR_MCS_RATES_NUM];
};

struct b43_ppr {
	/* All powers are in qdbm (Q5.2) */
	union {
		u8 __all_rates[B43_PPR_RATES_NUM];
		struct b43_ppr_rates rates;
	};
};

struct b43_wldev;
enum b43_band;

void b43_ppr_clear(struct b43_wldev *dev, struct b43_ppr *ppr);

void b43_ppr_add(struct b43_wldev *dev, struct b43_ppr *ppr, int diff);
void b43_ppr_apply_max(struct b43_wldev *dev, struct b43_ppr *ppr, u8 max);
void b43_ppr_apply_min(struct b43_wldev *dev, struct b43_ppr *ppr, u8 min);
u8 b43_ppr_get_max(struct b43_wldev *dev, struct b43_ppr *ppr);

bool b43_ppr_load_max_from_sprom(struct b43_wldev *dev, struct b43_ppr *ppr,
				 enum b43_band band);

#endif /* LINUX_B43_PPR_H_ */
