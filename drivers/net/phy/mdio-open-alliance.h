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

#endif /* __MDIO_OPEN_ALLIANCE__ */
