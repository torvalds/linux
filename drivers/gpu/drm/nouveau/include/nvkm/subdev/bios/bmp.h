/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVBIOS_BMP_H__
#define __NVBIOS_BMP_H__
static inline u16
bmp_version(struct nvkm_bios *bios)
{
	if (bios->bmp_offset) {
		return nvbios_rd08(bios, bios->bmp_offset + 5) << 8 |
		       nvbios_rd08(bios, bios->bmp_offset + 6);
	}

	return 0x0000;
}

static inline u16
bmp_mem_init_table(struct nvkm_bios *bios)
{
	if (bmp_version(bios) >= 0x0300)
		return nvbios_rd16(bios, bios->bmp_offset + 24);
	return 0x0000;
}

static inline u16
bmp_sdr_seq_table(struct nvkm_bios *bios)
{
	if (bmp_version(bios) >= 0x0300)
		return nvbios_rd16(bios, bios->bmp_offset + 26);
	return 0x0000;
}

static inline u16
bmp_ddr_seq_table(struct nvkm_bios *bios)
{
	if (bmp_version(bios) >= 0x0300)
		return nvbios_rd16(bios, bios->bmp_offset + 28);
	return 0x0000;
}
#endif
