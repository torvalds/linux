#ifndef __NVBIOS_I2C_H__
#define __NVBIOS_I2C_H__

struct nouveau_bios;

enum dcb_i2c_type {
	DCB_I2C_NV04_BIT = 0,
	DCB_I2C_NV4E_BIT = 4,
	DCB_I2C_NVIO_BIT = 5,
	DCB_I2C_NVIO_AUX = 6,
	DCB_I2C_UNUSED = 0xff
};

struct dcb_i2c_entry {
	enum dcb_i2c_type type;
	u8 drive;
	u8 sense;
	u32 data;
};

u16 dcb_i2c_table(struct nouveau_bios *, u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 dcb_i2c_entry(struct nouveau_bios *, u8 index, u8 *ver, u8 *len);
int dcb_i2c_parse(struct nouveau_bios *, u8 index, struct dcb_i2c_entry *);

#endif
