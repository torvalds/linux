/*
 * 1-Wire implementation for the ds2760 chip
 *
 * Copyright © 2004-2005, Szabolcs Gyurko <szabolcs.gyurko@tlt.hu>
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 */

#ifndef __w1_ds2760_h__
#define __w1_ds2760_h__

/* Known commands to the DS2760 chip */
#define W1_DS2760_SWAP			0xAA
#define W1_DS2760_READ_DATA		0x69
#define W1_DS2760_WRITE_DATA		0x6C
#define W1_DS2760_COPY_DATA		0x48
#define W1_DS2760_RECALL_DATA		0xB8
#define W1_DS2760_LOCK			0x6A

/* Number of valid register addresses */
#define DS2760_DATA_SIZE		0x40

#define DS2760_PROTECTION_REG		0x00

#define DS2760_STATUS_REG		0x01
#define DS2760_STATUS_IE		(1 << 2)
#define DS2760_STATUS_SWEN		(1 << 3)
#define DS2760_STATUS_RNAOP		(1 << 4)
#define DS2760_STATUS_PMOD		(1 << 5)

#define DS2760_EEPROM_REG		0x07
#define DS2760_SPECIAL_FEATURE_REG	0x08
#define DS2760_VOLTAGE_MSB		0x0c
#define DS2760_VOLTAGE_LSB		0x0d
#define DS2760_CURRENT_MSB		0x0e
#define DS2760_CURRENT_LSB		0x0f
#define DS2760_CURRENT_ACCUM_MSB	0x10
#define DS2760_CURRENT_ACCUM_LSB	0x11
#define DS2760_TEMP_MSB			0x18
#define DS2760_TEMP_LSB			0x19
#define DS2760_EEPROM_BLOCK0		0x20
#define DS2760_ACTIVE_FULL		0x20
#define DS2760_EEPROM_BLOCK1		0x30
#define DS2760_STATUS_WRITE_REG		0x31
#define DS2760_RATED_CAPACITY		0x32
#define DS2760_CURRENT_OFFSET_BIAS	0x33
#define DS2760_ACTIVE_EMPTY		0x3b

extern int w1_ds2760_read(struct device *dev, char *buf, int addr,
			  size_t count);
extern int w1_ds2760_write(struct device *dev, char *buf, int addr,
			   size_t count);
extern int w1_ds2760_store_eeprom(struct device *dev, int addr);
extern int w1_ds2760_recall_eeprom(struct device *dev, int addr);

#endif /* !__w1_ds2760_h__ */
