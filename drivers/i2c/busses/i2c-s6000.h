/*
 * drivers/i2c/busses/i2c-s6000.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Emlix GmbH <info@emlix.com>
 * Author:	Oskar Schirmer <os@emlix.com>
 */

#ifndef __DRIVERS_I2C_BUSSES_I2C_S6000_H
#define __DRIVERS_I2C_BUSSES_I2C_S6000_H

#define S6_I2C_CON		0x000
#define S6_I2C_CON_MASTER		0
#define S6_I2C_CON_SPEED		1
#define S6_I2C_CON_SPEED_NORMAL			1
#define S6_I2C_CON_SPEED_FAST			2
#define S6_I2C_CON_SPEED_MASK			3
#define S6_I2C_CON_10BITSLAVE		3
#define S6_I2C_CON_10BITMASTER		4
#define S6_I2C_CON_RESTARTENA		5
#define S6_I2C_CON_SLAVEDISABLE		6
#define S6_I2C_TAR		0x004
#define S6_I2C_TAR_GCORSTART		10
#define S6_I2C_TAR_SPECIAL		11
#define S6_I2C_SAR		0x008
#define S6_I2C_HSMADDR		0x00C
#define S6_I2C_DATACMD		0x010
#define S6_I2C_DATACMD_READ		8
#define S6_I2C_SSHCNT		0x014
#define S6_I2C_SSLCNT		0x018
#define S6_I2C_FSHCNT		0x01C
#define S6_I2C_FSLCNT		0x020
#define S6_I2C_INTRSTAT		0x02C
#define S6_I2C_INTRMASK		0x030
#define S6_I2C_RAWINTR		0x034
#define S6_I2C_INTR_RXUNDER		0
#define S6_I2C_INTR_RXOVER		1
#define S6_I2C_INTR_RXFULL		2
#define S6_I2C_INTR_TXOVER		3
#define S6_I2C_INTR_TXEMPTY		4
#define S6_I2C_INTR_RDREQ		5
#define S6_I2C_INTR_TXABRT		6
#define S6_I2C_INTR_RXDONE		7
#define S6_I2C_INTR_ACTIVITY		8
#define S6_I2C_INTR_STOPDET		9
#define S6_I2C_INTR_STARTDET		10
#define S6_I2C_INTR_GENCALL		11
#define S6_I2C_RXTL		0x038
#define S6_I2C_TXTL		0x03C
#define S6_I2C_CLRINTR		0x040
#define S6_I2C_CLRRXUNDER	0x044
#define S6_I2C_CLRRXOVER	0x048
#define S6_I2C_CLRTXOVER	0x04C
#define S6_I2C_CLRRDREQ		0x050
#define S6_I2C_CLRTXABRT	0x054
#define S6_I2C_CLRRXDONE	0x058
#define S6_I2C_CLRACTIVITY	0x05C
#define S6_I2C_CLRSTOPDET	0x060
#define S6_I2C_CLRSTARTDET	0x064
#define S6_I2C_CLRGENCALL	0x068
#define S6_I2C_ENABLE		0x06C
#define S6_I2C_STATUS		0x070
#define S6_I2C_STATUS_ACTIVITY		0
#define S6_I2C_STATUS_TFNF		1
#define S6_I2C_STATUS_TFE		2
#define S6_I2C_STATUS_RFNE		3
#define S6_I2C_STATUS_RFF		4
#define S6_I2C_TXFLR		0x074
#define S6_I2C_RXFLR		0x078
#define S6_I2C_SRESET		0x07C
#define S6_I2C_SRESET_IC_SRST		0
#define S6_I2C_SRESET_IC_MASTER_SRST	1
#define S6_I2C_SRESET_IC_SLAVE_SRST	2
#define S6_I2C_TXABRTSOURCE	0x080

#endif
