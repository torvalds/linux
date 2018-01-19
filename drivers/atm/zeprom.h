/* SPDX-License-Identifier: GPL-2.0 */
/* drivers/atm/zeprom.h - ZeitNet ZN122x EEPROM (NM93C46) declarations */

/* Written 1995,1996 by Werner Almesberger, EPFL LRC */


#ifndef DRIVER_ATM_ZEPROM_H
#define DRIVER_ATM_ZEPROM_H

/* Different versions use different control registers */

#define ZEPROM_V1_REG	PCI_VENDOR_ID	/* PCI register */
#define ZEPROM_V2_REG	0x40

/* Bits in contol register */

#define ZEPROM_SK	0x80000000	/* strobe (probably on raising edge) */
#define ZEPROM_CS	0x40000000	/* Chip Select */
#define ZEPROM_DI	0x20000000	/* Data Input */
#define ZEPROM_DO	0x10000000	/* Data Output */

#define ZEPROM_SIZE	32		/* 32 bytes */
#define ZEPROM_V1_ESI_OFF 24		/* ESI offset in EEPROM (V1) */
#define ZEPROM_V2_ESI_OFF 4		/* ESI offset in EEPROM (V2) */

#define ZEPROM_CMD_LEN	3		/* commands are three bits */
#define ZEPROM_ADDR_LEN	6		/* addresses are six bits */

/* Commands (3 bits) */

#define ZEPROM_CMD_READ	6

/* No other commands are needed. */

#endif
