// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2002 Thomas Gleixner (tglx@linutronix.de)
 */

#include <linux/sizes.h>

#include "internals.h"

#define LP_OPTIONS 0
#define LP_OPTIONS16 (LP_OPTIONS | NAND_BUSWIDTH_16)

#define SP_OPTIONS NAND_NEED_READRDY
#define SP_OPTIONS16 (SP_OPTIONS | NAND_BUSWIDTH_16)

/*
 * The chip ID list:
 *    name, device ID, page size, chip size in MiB, eraseblock size, options
 *
 * If page size and eraseblock size are 0, the sizes are taken from the
 * extended chip ID.
 */
struct nand_flash_dev nand_flash_ids[] = {
	/*
	 * Some incompatible NAND chips share device ID's and so must be
	 * listed by full ID. We list them first so that we can easily identify
	 * the most specific match.
	 */
	{"TC58NVG0S3E 1G 3.3V 8-bit",
		{ .id = {0x98, 0xd1, 0x90, 0x15, 0x76, 0x14, 0x01, 0x00} },
		  SZ_2K, SZ_128, SZ_128K, 0, 8, 64, NAND_ECC_INFO(1, SZ_512), },
	{"TC58NVG2S0F 4G 3.3V 8-bit",
		{ .id = {0x98, 0xdc, 0x90, 0x26, 0x76, 0x15, 0x01, 0x08} },
		  SZ_4K, SZ_512, SZ_256K, 0, 8, 224, NAND_ECC_INFO(4, SZ_512) },
	{"TC58NVG2S0H 4G 3.3V 8-bit",
		{ .id = {0x98, 0xdc, 0x90, 0x26, 0x76, 0x16, 0x08, 0x00} },
		  SZ_4K, SZ_512, SZ_256K, 0, 8, 256, NAND_ECC_INFO(8, SZ_512) },
	{"TC58NVG3S0F 8G 3.3V 8-bit",
		{ .id = {0x98, 0xd3, 0x90, 0x26, 0x76, 0x15, 0x02, 0x08} },
		  SZ_4K, SZ_1K, SZ_256K, 0, 8, 232, NAND_ECC_INFO(4, SZ_512) },
	{"TC58NVG5D2 32G 3.3V 8-bit",
		{ .id = {0x98, 0xd7, 0x94, 0x32, 0x76, 0x56, 0x09, 0x00} },
		  SZ_8K, SZ_4K, SZ_1M, 0, 8, 640, NAND_ECC_INFO(40, SZ_1K) },
	{"TC58NVG6D2 64G 3.3V 8-bit",
		{ .id = {0x98, 0xde, 0x94, 0x82, 0x76, 0x56, 0x04, 0x20} },
		  SZ_8K, SZ_8K, SZ_2M, 0, 8, 640, NAND_ECC_INFO(40, SZ_1K) },
	{"SDTNRGAMA 64G 3.3V 8-bit",
		{ .id = {0x45, 0xde, 0x94, 0x93, 0x76, 0x50} },
		  SZ_16K, SZ_8K, SZ_4M, 0, 6, 1280, NAND_ECC_INFO(40, SZ_1K) },
	{"H27UCG8T2ATR-BC 64G 3.3V 8-bit",
		{ .id = {0xad, 0xde, 0x94, 0xda, 0x74, 0xc4} },
		  SZ_8K, SZ_8K, SZ_2M, NAND_NEED_SCRAMBLING, 6, 640,
		  NAND_ECC_INFO(40, SZ_1K) },
	{"H27UCG8T2ETR-BC 64G 3.3V 8-bit",
		{ .id = {0xad, 0xde, 0x14, 0xa7, 0x42, 0x4a} },
		  SZ_16K, SZ_8K, SZ_4M, NAND_NEED_SCRAMBLING, 6, 1664,
		  NAND_ECC_INFO(40, SZ_1K) },
	{"TH58NVG2S3HBAI4 4G 3.3V 8-bit",
		{ .id = {0x98, 0xdc, 0x91, 0x15, 0x76} },
		  SZ_2K, SZ_512, SZ_128K, 0, 5, 128, NAND_ECC_INFO(8, SZ_512) },
	{"TH58NVG3S0HBAI4 8G 3.3V 8-bit",
		{ .id = {0x98, 0xd3, 0x91, 0x26, 0x76} },
		  SZ_4K, SZ_1K, SZ_256K, 0, 5, 256, NAND_ECC_INFO(8, SZ_512)},

	LEGACY_ID_NAND("NAND 4MiB 5V 8-bit",   0x6B, 4, SZ_8K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 4MiB 3,3V 8-bit", 0xE3, 4, SZ_8K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 4MiB 3,3V 8-bit", 0xE5, 4, SZ_8K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 8MiB 3,3V 8-bit", 0xD6, 8, SZ_8K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 8MiB 3,3V 8-bit", 0xE6, 8, SZ_8K, SP_OPTIONS),

	LEGACY_ID_NAND("NAND 16MiB 1,8V 8-bit",  0x33, 16, SZ_16K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 16MiB 3,3V 8-bit",  0x73, 16, SZ_16K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 16MiB 1,8V 16-bit", 0x43, 16, SZ_16K, SP_OPTIONS16),
	LEGACY_ID_NAND("NAND 16MiB 3,3V 16-bit", 0x53, 16, SZ_16K, SP_OPTIONS16),

	LEGACY_ID_NAND("NAND 32MiB 1,8V 8-bit",  0x35, 32, SZ_16K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 32MiB 3,3V 8-bit",  0x75, 32, SZ_16K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 32MiB 1,8V 16-bit", 0x45, 32, SZ_16K, SP_OPTIONS16),
	LEGACY_ID_NAND("NAND 32MiB 3,3V 16-bit", 0x55, 32, SZ_16K, SP_OPTIONS16),

	LEGACY_ID_NAND("NAND 64MiB 1,8V 8-bit",  0x36, 64, SZ_16K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 64MiB 3,3V 8-bit",  0x76, 64, SZ_16K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 64MiB 1,8V 16-bit", 0x46, 64, SZ_16K, SP_OPTIONS16),
	LEGACY_ID_NAND("NAND 64MiB 3,3V 16-bit", 0x56, 64, SZ_16K, SP_OPTIONS16),

	LEGACY_ID_NAND("NAND 128MiB 1,8V 8-bit",  0x78, 128, SZ_16K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 128MiB 1,8V 8-bit",  0x39, 128, SZ_16K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 128MiB 3,3V 8-bit",  0x79, 128, SZ_16K, SP_OPTIONS),
	LEGACY_ID_NAND("NAND 128MiB 1,8V 16-bit", 0x72, 128, SZ_16K, SP_OPTIONS16),
	LEGACY_ID_NAND("NAND 128MiB 1,8V 16-bit", 0x49, 128, SZ_16K, SP_OPTIONS16),
	LEGACY_ID_NAND("NAND 128MiB 3,3V 16-bit", 0x74, 128, SZ_16K, SP_OPTIONS16),
	LEGACY_ID_NAND("NAND 128MiB 3,3V 16-bit", 0x59, 128, SZ_16K, SP_OPTIONS16),

	LEGACY_ID_NAND("NAND 256MiB 3,3V 8-bit", 0x71, 256, SZ_16K, SP_OPTIONS),

	/*
	 * These are the new chips with large page size. Their page size and
	 * eraseblock size are determined from the extended ID bytes.
	 */

	/* 512 Megabit */
	EXTENDED_ID_NAND("NAND 64MiB 1,8V 8-bit",  0xA2,  64, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 64MiB 1,8V 8-bit",  0xA0,  64, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 64MiB 3,3V 8-bit",  0xF2,  64, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 64MiB 3,3V 8-bit",  0xD0,  64, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 64MiB 3,3V 8-bit",  0xF0,  64, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 64MiB 1,8V 16-bit", 0xB2,  64, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 64MiB 1,8V 16-bit", 0xB0,  64, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 64MiB 3,3V 16-bit", 0xC2,  64, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 64MiB 3,3V 16-bit", 0xC0,  64, LP_OPTIONS16),

	/* 1 Gigabit */
	EXTENDED_ID_NAND("NAND 128MiB 1,8V 8-bit",  0xA1, 128, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 128MiB 3,3V 8-bit",  0xF1, 128, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 128MiB 3,3V 8-bit",  0xD1, 128, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 128MiB 1,8V 16-bit", 0xB1, 128, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 128MiB 3,3V 16-bit", 0xC1, 128, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 128MiB 1,8V 16-bit", 0xAD, 128, LP_OPTIONS16),

	/* 2 Gigabit */
	EXTENDED_ID_NAND("NAND 256MiB 1,8V 8-bit",  0xAA, 256, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 256MiB 3,3V 8-bit",  0xDA, 256, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 256MiB 1,8V 16-bit", 0xBA, 256, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 256MiB 3,3V 16-bit", 0xCA, 256, LP_OPTIONS16),

	/* 4 Gigabit */
	EXTENDED_ID_NAND("NAND 512MiB 1,8V 8-bit",  0xAC, 512, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 512MiB 3,3V 8-bit",  0xDC, 512, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 512MiB 1,8V 16-bit", 0xBC, 512, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 512MiB 3,3V 16-bit", 0xCC, 512, LP_OPTIONS16),

	/* 8 Gigabit */
	EXTENDED_ID_NAND("NAND 1GiB 1,8V 8-bit",  0xA3, 1024, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 1GiB 3,3V 8-bit",  0xD3, 1024, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 1GiB 1,8V 16-bit", 0xB3, 1024, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 1GiB 3,3V 16-bit", 0xC3, 1024, LP_OPTIONS16),

	/* 16 Gigabit */
	EXTENDED_ID_NAND("NAND 2GiB 1,8V 8-bit",  0xA5, 2048, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 2GiB 3,3V 8-bit",  0xD5, 2048, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 2GiB 1,8V 16-bit", 0xB5, 2048, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 2GiB 3,3V 16-bit", 0xC5, 2048, LP_OPTIONS16),

	/* 32 Gigabit */
	EXTENDED_ID_NAND("NAND 4GiB 1,8V 8-bit",  0xA7, 4096, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 4GiB 3,3V 8-bit",  0xD7, 4096, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 4GiB 1,8V 16-bit", 0xB7, 4096, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 4GiB 3,3V 16-bit", 0xC7, 4096, LP_OPTIONS16),

	/* 64 Gigabit */
	EXTENDED_ID_NAND("NAND 8GiB 1,8V 8-bit",  0xAE, 8192, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 8GiB 3,3V 8-bit",  0xDE, 8192, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 8GiB 1,8V 16-bit", 0xBE, 8192, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 8GiB 3,3V 16-bit", 0xCE, 8192, LP_OPTIONS16),

	/* 128 Gigabit */
	EXTENDED_ID_NAND("NAND 16GiB 1,8V 8-bit",  0x1A, 16384, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 16GiB 3,3V 8-bit",  0x3A, 16384, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 16GiB 1,8V 16-bit", 0x2A, 16384, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 16GiB 3,3V 16-bit", 0x4A, 16384, LP_OPTIONS16),

	/* 256 Gigabit */
	EXTENDED_ID_NAND("NAND 32GiB 1,8V 8-bit",  0x1C, 32768, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 32GiB 3,3V 8-bit",  0x3C, 32768, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 32GiB 1,8V 16-bit", 0x2C, 32768, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 32GiB 3,3V 16-bit", 0x4C, 32768, LP_OPTIONS16),

	/* 512 Gigabit */
	EXTENDED_ID_NAND("NAND 64GiB 1,8V 8-bit",  0x1E, 65536, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 64GiB 3,3V 8-bit",  0x3E, 65536, LP_OPTIONS),
	EXTENDED_ID_NAND("NAND 64GiB 1,8V 16-bit", 0x2E, 65536, LP_OPTIONS16),
	EXTENDED_ID_NAND("NAND 64GiB 3,3V 16-bit", 0x4E, 65536, LP_OPTIONS16),

	{NULL}
};

/* Manufacturer IDs */
static const struct nand_manufacturer_desc nand_manufacturer_descs[] = {
	{NAND_MFR_AMD, "AMD/Spansion", &amd_nand_manuf_ops},
	{NAND_MFR_ATO, "ATO"},
	{NAND_MFR_EON, "Eon"},
	{NAND_MFR_ESMT, "ESMT", &esmt_nand_manuf_ops},
	{NAND_MFR_FUJITSU, "Fujitsu"},
	{NAND_MFR_HYNIX, "Hynix", &hynix_nand_manuf_ops},
	{NAND_MFR_INTEL, "Intel"},
	{NAND_MFR_MACRONIX, "Macronix", &macronix_nand_manuf_ops},
	{NAND_MFR_MICRON, "Micron", &micron_nand_manuf_ops},
	{NAND_MFR_NATIONAL, "National"},
	{NAND_MFR_RENESAS, "Renesas"},
	{NAND_MFR_SAMSUNG, "Samsung", &samsung_nand_manuf_ops},
	{NAND_MFR_SANDISK, "SanDisk"},
	{NAND_MFR_STMICRO, "ST Micro"},
	{NAND_MFR_TOSHIBA, "Toshiba", &toshiba_nand_manuf_ops},
	{NAND_MFR_WINBOND, "Winbond"},
};

/**
 * nand_get_manufacturer_desc - Get manufacturer information from the
 *                              manufacturer ID
 * @id: manufacturer ID
 *
 * Returns a nand_manufacturer_desc object if the manufacturer is defined
 * in the NAND manufacturers database, NULL otherwise.
 */
const struct nand_manufacturer_desc *nand_get_manufacturer_desc(u8 id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nand_manufacturer_descs); i++)
		if (nand_manufacturer_descs[i].id == id)
			return &nand_manufacturer_descs[i];

	return NULL;
}
