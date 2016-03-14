#ifndef B43_PHY_AC_H_
#define B43_PHY_AC_H_

#include "phy_common.h"

#define B43_PHY_AC_BBCFG			0x001
#define  B43_PHY_AC_BBCFG_RSTCCA		0x4000	/* Reset CCA */
#define B43_PHY_AC_BANDCTL			0x003	/* Band control */
#define  B43_PHY_AC_BANDCTL_5GHZ		0x0001
#define B43_PHY_AC_TABLE_ID			0x00d
#define B43_PHY_AC_TABLE_OFFSET			0x00e
#define B43_PHY_AC_TABLE_DATA1			0x00f
#define B43_PHY_AC_TABLE_DATA2			0x010
#define B43_PHY_AC_TABLE_DATA3			0x011
#define B43_PHY_AC_CLASSCTL			0x140	/* Classifier control */
#define  B43_PHY_AC_CLASSCTL_CCKEN		0x0001	/* CCK enable */
#define  B43_PHY_AC_CLASSCTL_OFDMEN		0x0002	/* OFDM enable */
#define  B43_PHY_AC_CLASSCTL_WAITEDEN		0x0004	/* Waited enable */
#define B43_PHY_AC_BW1A				0x371
#define B43_PHY_AC_BW2				0x372
#define B43_PHY_AC_BW3				0x373
#define B43_PHY_AC_BW4				0x374
#define B43_PHY_AC_BW5				0x375
#define B43_PHY_AC_BW6				0x376
#define B43_PHY_AC_RFCTL_CMD			0x408
#define B43_PHY_AC_C1_CLIP			0x6d4
#define  B43_PHY_AC_C1_CLIP_DIS			0x4000
#define B43_PHY_AC_C2_CLIP			0x8d4
#define  B43_PHY_AC_C2_CLIP_DIS			0x4000
#define B43_PHY_AC_C3_CLIP			0xad4
#define  B43_PHY_AC_C3_CLIP_DIS			0x4000

struct b43_phy_ac {
};

extern const struct b43_phy_operations b43_phyops_ac;

#endif /* B43_PHY_AC_H_ */
