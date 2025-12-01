/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mdio-open-alliance.h - definition of OPEN Alliance SIG standard registers
 */

#ifndef __MDIO_OPEN_ALLIANCE__
#define __MDIO_OPEN_ALLIANCE__

#include <linux/mdio.h>

/* NOTE: all OATC14 registers are located in MDIO_MMD_VEND2 */

/* Open Alliance TC14 (10BASE-T1S) registers */
#define MDIO_OATC14_PLCA_IDVER	0xca00  /* PLCA ID and version */
#define MDIO_OATC14_PLCA_CTRL0	0xca01	/* PLCA Control register 0 */
#define MDIO_OATC14_PLCA_CTRL1	0xca02	/* PLCA Control register 1 */
#define MDIO_OATC14_PLCA_STATUS	0xca03	/* PLCA Status register */
#define MDIO_OATC14_PLCA_TOTMR	0xca04	/* PLCA TO Timer register */
#define MDIO_OATC14_PLCA_BURST	0xca05	/* PLCA BURST mode register */

/* Open Alliance TC14 PLCA IDVER register */
#define MDIO_OATC14_PLCA_IDM	0xff00	/* PLCA MAP ID */
#define MDIO_OATC14_PLCA_VER	0x00ff	/* PLCA MAP version */

/* Open Alliance TC14 PLCA CTRL0 register */
#define MDIO_OATC14_PLCA_EN	BIT(15) /* PLCA enable */
#define MDIO_OATC14_PLCA_RST	BIT(14) /* PLCA reset */

/* Open Alliance TC14 PLCA CTRL1 register */
#define MDIO_OATC14_PLCA_NCNT	0xff00	/* PLCA node count */
#define MDIO_OATC14_PLCA_ID	0x00ff	/* PLCA local node ID */

/* Open Alliance TC14 PLCA STATUS register */
#define MDIO_OATC14_PLCA_PST	BIT(15)	/* PLCA status indication */

/* Open Alliance TC14 PLCA TOTMR register */
#define MDIO_OATC14_PLCA_TOT	0x00ff

/* Open Alliance TC14 PLCA BURST register */
#define MDIO_OATC14_PLCA_MAXBC	0xff00
#define MDIO_OATC14_PLCA_BTMR	0x00ff

/* Version Identifiers */
#define OATC14_IDM		0x0a00

/*
 * Open Alliance TC14 (10BASE-T1S) - Advanced Diagnostic Features Registers
 *
 * Refer to the OPEN Alliance documentation:
 *   https://opensig.org/automotive-ethernet-specifications/
 *
 * Specification:
 *   "10BASE-T1S Advanced Diagnostic PHY Features"
 *   https://opensig.org/wp-content/uploads/2025/06/OPEN_Alliance_10BASE-T1S_Advanced_PHY_features_for-automotive_Ethernet_V2.1b.pdf
 */
/* Advanced Diagnostic Features Capability Register*/
#define MDIO_OATC14_ADFCAP		0xcc00
#define OATC14_ADFCAP_HDD_CAPABILITY	GENMASK(10, 8)
#define OATC14_ADFCAP_SQIPLUS_CAPABILITY	GENMASK(4, 1)
#define OATC14_ADFCAP_SQI_CAPABILITY	BIT(0)

/* Harness Defect Detection Register */
#define MDIO_OATC14_HDD			0xcc01
#define OATC14_HDD_CONTROL		BIT(15)
#define OATC14_HDD_READY		BIT(14)
#define OATC14_HDD_START_CONTROL	BIT(13)
#define OATC14_HDD_VALID		BIT(2)
#define OATC14_HDD_SHORT_OPEN_STATUS	GENMASK(1, 0)

/* Dynamic Channel Quality SQI Register */
#define MDIO_OATC14_DCQ_SQI		0xcc03
#define OATC14_DCQ_SQI_VALUE		GENMASK(2, 0)

/* Dynamic Channel Quality SQI Plus Register */
#define MDIO_OATC14_DCQ_SQIPLUS		0xcc04
#define OATC14_DCQ_SQIPLUS_VALUE	GENMASK(7, 0)

/* SQI is supported using 3 bits means 8 levels (0-7) */
#define OATC14_SQI_MAX_LEVEL		7

/* Bus Short/Open Status:
 * 0 0 - no fault; everything is ok. (Default)
 * 0 1 - detected as an open or missing termination(s)
 * 1 0 - detected as a short or extra termination(s)
 * 1 1 - fault but fault type not detectable. More details can be available by
 *       vender specific register if supported.
 */
enum oatc14_hdd_status {
	OATC14_HDD_STATUS_CABLE_OK = 0,
	OATC14_HDD_STATUS_OPEN,
	OATC14_HDD_STATUS_SHORT,
	OATC14_HDD_STATUS_NOT_DETECTABLE,
};

#endif /* __MDIO_OPEN_ALLIANCE__ */
