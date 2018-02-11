/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVBIOS_I2C_H__
#define __NVBIOS_I2C_H__
enum dcb_i2c_type {
	/* matches bios type field prior to ccb 4.1 */
	DCB_I2C_NV04_BIT = 0x00,
	DCB_I2C_NV4E_BIT = 0x04,
	DCB_I2C_NVIO_BIT = 0x05,
	DCB_I2C_NVIO_AUX = 0x06,
	/* made up - mostly */
	DCB_I2C_PMGR     = 0x80,
	DCB_I2C_UNUSED   = 0xff
};

struct dcb_i2c_entry {
	enum dcb_i2c_type type;
	u8 drive;
	u8 sense;
	u8 share;
	u8 auxch;
};

u16 dcb_i2c_table(struct nvkm_bios *, u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 dcb_i2c_entry(struct nvkm_bios *, u8 index, u8 *ver, u8 *len);
int dcb_i2c_parse(struct nvkm_bios *, u8 index, struct dcb_i2c_entry *);
#endif
