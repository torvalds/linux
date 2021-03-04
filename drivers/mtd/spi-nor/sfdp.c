// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/mtd/spi-nor.h>

#include "core.h"

#define SFDP_PARAM_HEADER_ID(p)	(((p)->id_msb << 8) | (p)->id_lsb)
#define SFDP_PARAM_HEADER_PTP(p) \
	(((p)->parameter_table_pointer[2] << 16) | \
	 ((p)->parameter_table_pointer[1] <<  8) | \
	 ((p)->parameter_table_pointer[0] <<  0))

#define SFDP_BFPT_ID		0xff00	/* Basic Flash Parameter Table */
#define SFDP_SECTOR_MAP_ID	0xff81	/* Sector Map Table */
#define SFDP_4BAIT_ID		0xff84  /* 4-byte Address Instruction Table */

#define SFDP_SIGNATURE		0x50444653U

struct sfdp_header {
	u32		signature; /* Ox50444653U <=> "SFDP" */
	u8		minor;
	u8		major;
	u8		nph; /* 0-base number of parameter headers */
	u8		unused;

	/* Basic Flash Parameter Table. */
	struct sfdp_parameter_header	bfpt_header;
};

/* Fast Read settings. */
struct sfdp_bfpt_read {
	/* The Fast Read x-y-z hardware capability in params->hwcaps.mask. */
	u32			hwcaps;

	/*
	 * The <supported_bit> bit in <supported_dword> BFPT DWORD tells us
	 * whether the Fast Read x-y-z command is supported.
	 */
	u32			supported_dword;
	u32			supported_bit;

	/*
	 * The half-word at offset <setting_shift> in <setting_dword> BFPT DWORD
	 * encodes the op code, the number of mode clocks and the number of wait
	 * states to be used by Fast Read x-y-z command.
	 */
	u32			settings_dword;
	u32			settings_shift;

	/* The SPI protocol for this Fast Read x-y-z command. */
	enum spi_nor_protocol	proto;
};

struct sfdp_bfpt_erase {
	/*
	 * The half-word at offset <shift> in DWORD <dwoard> encodes the
	 * op code and erase sector size to be used by Sector Erase commands.
	 */
	u32			dword;
	u32			shift;
};

#define SMPT_CMD_ADDRESS_LEN_MASK		GENMASK(23, 22)
#define SMPT_CMD_ADDRESS_LEN_0			(0x0UL << 22)
#define SMPT_CMD_ADDRESS_LEN_3			(0x1UL << 22)
#define SMPT_CMD_ADDRESS_LEN_4			(0x2UL << 22)
#define SMPT_CMD_ADDRESS_LEN_USE_CURRENT	(0x3UL << 22)

#define SMPT_CMD_READ_DUMMY_MASK		GENMASK(19, 16)
#define SMPT_CMD_READ_DUMMY_SHIFT		16
#define SMPT_CMD_READ_DUMMY(_cmd) \
	(((_cmd) & SMPT_CMD_READ_DUMMY_MASK) >> SMPT_CMD_READ_DUMMY_SHIFT)
#define SMPT_CMD_READ_DUMMY_IS_VARIABLE		0xfUL

#define SMPT_CMD_READ_DATA_MASK			GENMASK(31, 24)
#define SMPT_CMD_READ_DATA_SHIFT		24
#define SMPT_CMD_READ_DATA(_cmd) \
	(((_cmd) & SMPT_CMD_READ_DATA_MASK) >> SMPT_CMD_READ_DATA_SHIFT)

#define SMPT_CMD_OPCODE_MASK			GENMASK(15, 8)
#define SMPT_CMD_OPCODE_SHIFT			8
#define SMPT_CMD_OPCODE(_cmd) \
	(((_cmd) & SMPT_CMD_OPCODE_MASK) >> SMPT_CMD_OPCODE_SHIFT)

#define SMPT_MAP_REGION_COUNT_MASK		GENMASK(23, 16)
#define SMPT_MAP_REGION_COUNT_SHIFT		16
#define SMPT_MAP_REGION_COUNT(_header) \
	((((_header) & SMPT_MAP_REGION_COUNT_MASK) >> \
	  SMPT_MAP_REGION_COUNT_SHIFT) + 1)

#define SMPT_MAP_ID_MASK			GENMASK(15, 8)
#define SMPT_MAP_ID_SHIFT			8
#define SMPT_MAP_ID(_header) \
	(((_header) & SMPT_MAP_ID_MASK) >> SMPT_MAP_ID_SHIFT)

#define SMPT_MAP_REGION_SIZE_MASK		GENMASK(31, 8)
#define SMPT_MAP_REGION_SIZE_SHIFT		8
#define SMPT_MAP_REGION_SIZE(_region) \
	(((((_region) & SMPT_MAP_REGION_SIZE_MASK) >> \
	   SMPT_MAP_REGION_SIZE_SHIFT) + 1) * 256)

#define SMPT_MAP_REGION_ERASE_TYPE_MASK		GENMASK(3, 0)
#define SMPT_MAP_REGION_ERASE_TYPE(_region) \
	((_region) & SMPT_MAP_REGION_ERASE_TYPE_MASK)

#define SMPT_DESC_TYPE_MAP			BIT(1)
#define SMPT_DESC_END				BIT(0)

#define SFDP_4BAIT_DWORD_MAX	2

struct sfdp_4bait {
	/* The hardware capability. */
	u32		hwcaps;

	/*
	 * The <supported_bit> bit in DWORD1 of the 4BAIT tells us whether
	 * the associated 4-byte address op code is supported.
	 */
	u32		supported_bit;
};

/**
 * spi_nor_read_raw() - raw read of serial flash memory. read_opcode,
 *			addr_width and read_dummy members of the struct spi_nor
 *			should be previously
 * set.
 * @nor:	pointer to a 'struct spi_nor'
 * @addr:	offset in the serial flash memory
 * @len:	number of bytes to read
 * @buf:	buffer where the data is copied into (dma-safe memory)
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_read_raw(struct spi_nor *nor, u32 addr, size_t len, u8 *buf)
{
	ssize_t ret;

	while (len) {
		ret = spi_nor_read_data(nor, addr, len, buf);
		if (ret < 0)
			return ret;
		if (!ret || ret > len)
			return -EIO;

		buf += ret;
		addr += ret;
		len -= ret;
	}
	return 0;
}

/**
 * spi_nor_read_sfdp() - read Serial Flash Discoverable Parameters.
 * @nor:	pointer to a 'struct spi_nor'
 * @addr:	offset in the SFDP area to start reading data from
 * @len:	number of bytes to read
 * @buf:	buffer where the SFDP data are copied into (dma-safe memory)
 *
 * Whatever the actual numbers of bytes for address and dummy cycles are
 * for (Fast) Read commands, the Read SFDP (5Ah) instruction is always
 * followed by a 3-byte address and 8 dummy clock cycles.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_read_sfdp(struct spi_nor *nor, u32 addr,
			     size_t len, void *buf)
{
	u8 addr_width, read_opcode, read_dummy;
	int ret;

	read_opcode = nor->read_opcode;
	addr_width = nor->addr_width;
	read_dummy = nor->read_dummy;

	nor->read_opcode = SPINOR_OP_RDSFDP;
	nor->addr_width = 3;
	nor->read_dummy = 8;

	ret = spi_nor_read_raw(nor, addr, len, buf);

	nor->read_opcode = read_opcode;
	nor->addr_width = addr_width;
	nor->read_dummy = read_dummy;

	return ret;
}

/**
 * spi_nor_read_sfdp_dma_unsafe() - read Serial Flash Discoverable Parameters.
 * @nor:	pointer to a 'struct spi_nor'
 * @addr:	offset in the SFDP area to start reading data from
 * @len:	number of bytes to read
 * @buf:	buffer where the SFDP data are copied into
 *
 * Wrap spi_nor_read_sfdp() using a kmalloc'ed bounce buffer as @buf is now not
 * guaranteed to be dma-safe.
 *
 * Return: -ENOMEM if kmalloc() fails, the return code of spi_nor_read_sfdp()
 *          otherwise.
 */
static int spi_nor_read_sfdp_dma_unsafe(struct spi_nor *nor, u32 addr,
					size_t len, void *buf)
{
	void *dma_safe_buf;
	int ret;

	dma_safe_buf = kmalloc(len, GFP_KERNEL);
	if (!dma_safe_buf)
		return -ENOMEM;

	ret = spi_nor_read_sfdp(nor, addr, len, dma_safe_buf);
	memcpy(buf, dma_safe_buf, len);
	kfree(dma_safe_buf);

	return ret;
}

static void
spi_nor_set_read_settings_from_bfpt(struct spi_nor_read_command *read,
				    u16 half,
				    enum spi_nor_protocol proto)
{
	read->num_mode_clocks = (half >> 5) & 0x07;
	read->num_wait_states = (half >> 0) & 0x1f;
	read->opcode = (half >> 8) & 0xff;
	read->proto = proto;
}

static const struct sfdp_bfpt_read sfdp_bfpt_reads[] = {
	/* Fast Read 1-1-2 */
	{
		SNOR_HWCAPS_READ_1_1_2,
		BFPT_DWORD(1), BIT(16),	/* Supported bit */
		BFPT_DWORD(4), 0,	/* Settings */
		SNOR_PROTO_1_1_2,
	},

	/* Fast Read 1-2-2 */
	{
		SNOR_HWCAPS_READ_1_2_2,
		BFPT_DWORD(1), BIT(20),	/* Supported bit */
		BFPT_DWORD(4), 16,	/* Settings */
		SNOR_PROTO_1_2_2,
	},

	/* Fast Read 2-2-2 */
	{
		SNOR_HWCAPS_READ_2_2_2,
		BFPT_DWORD(5),  BIT(0),	/* Supported bit */
		BFPT_DWORD(6), 16,	/* Settings */
		SNOR_PROTO_2_2_2,
	},

	/* Fast Read 1-1-4 */
	{
		SNOR_HWCAPS_READ_1_1_4,
		BFPT_DWORD(1), BIT(22),	/* Supported bit */
		BFPT_DWORD(3), 16,	/* Settings */
		SNOR_PROTO_1_1_4,
	},

	/* Fast Read 1-4-4 */
	{
		SNOR_HWCAPS_READ_1_4_4,
		BFPT_DWORD(1), BIT(21),	/* Supported bit */
		BFPT_DWORD(3), 0,	/* Settings */
		SNOR_PROTO_1_4_4,
	},

	/* Fast Read 4-4-4 */
	{
		SNOR_HWCAPS_READ_4_4_4,
		BFPT_DWORD(5), BIT(4),	/* Supported bit */
		BFPT_DWORD(7), 16,	/* Settings */
		SNOR_PROTO_4_4_4,
	},
};

static const struct sfdp_bfpt_erase sfdp_bfpt_erases[] = {
	/* Erase Type 1 in DWORD8 bits[15:0] */
	{BFPT_DWORD(8), 0},

	/* Erase Type 2 in DWORD8 bits[31:16] */
	{BFPT_DWORD(8), 16},

	/* Erase Type 3 in DWORD9 bits[15:0] */
	{BFPT_DWORD(9), 0},

	/* Erase Type 4 in DWORD9 bits[31:16] */
	{BFPT_DWORD(9), 16},
};

/**
 * spi_nor_set_erase_settings_from_bfpt() - set erase type settings from BFPT
 * @erase:	pointer to a structure that describes a SPI NOR erase type
 * @size:	the size of the sector/block erased by the erase type
 * @opcode:	the SPI command op code to erase the sector/block
 * @i:		erase type index as sorted in the Basic Flash Parameter Table
 *
 * The supported Erase Types will be sorted at init in ascending order, with
 * the smallest Erase Type size being the first member in the erase_type array
 * of the spi_nor_erase_map structure. Save the Erase Type index as sorted in
 * the Basic Flash Parameter Table since it will be used later on to
 * synchronize with the supported Erase Types defined in SFDP optional tables.
 */
static void
spi_nor_set_erase_settings_from_bfpt(struct spi_nor_erase_type *erase,
				     u32 size, u8 opcode, u8 i)
{
	erase->idx = i;
	spi_nor_set_erase_type(erase, size, opcode);
}

/**
 * spi_nor_map_cmp_erase_type() - compare the map's erase types by size
 * @l:	member in the left half of the map's erase_type array
 * @r:	member in the right half of the map's erase_type array
 *
 * Comparison function used in the sort() call to sort in ascending order the
 * map's erase types, the smallest erase type size being the first member in the
 * sorted erase_type array.
 *
 * Return: the result of @l->size - @r->size
 */
static int spi_nor_map_cmp_erase_type(const void *l, const void *r)
{
	const struct spi_nor_erase_type *left = l, *right = r;

	return left->size - right->size;
}

/**
 * spi_nor_sort_erase_mask() - sort erase mask
 * @map:	the erase map of the SPI NOR
 * @erase_mask:	the erase type mask to be sorted
 *
 * Replicate the sort done for the map's erase types in BFPT: sort the erase
 * mask in ascending order with the smallest erase type size starting from
 * BIT(0) in the sorted erase mask.
 *
 * Return: sorted erase mask.
 */
static u8 spi_nor_sort_erase_mask(struct spi_nor_erase_map *map, u8 erase_mask)
{
	struct spi_nor_erase_type *erase_type = map->erase_type;
	int i;
	u8 sorted_erase_mask = 0;

	if (!erase_mask)
		return 0;

	/* Replicate the sort done for the map's erase types. */
	for (i = 0; i < SNOR_ERASE_TYPE_MAX; i++)
		if (erase_type[i].size && erase_mask & BIT(erase_type[i].idx))
			sorted_erase_mask |= BIT(i);

	return sorted_erase_mask;
}

/**
 * spi_nor_regions_sort_erase_types() - sort erase types in each region
 * @map:	the erase map of the SPI NOR
 *
 * Function assumes that the erase types defined in the erase map are already
 * sorted in ascending order, with the smallest erase type size being the first
 * member in the erase_type array. It replicates the sort done for the map's
 * erase types. Each region's erase bitmask will indicate which erase types are
 * supported from the sorted erase types defined in the erase map.
 * Sort the all region's erase type at init in order to speed up the process of
 * finding the best erase command at runtime.
 */
static void spi_nor_regions_sort_erase_types(struct spi_nor_erase_map *map)
{
	struct spi_nor_erase_region *region = map->regions;
	u8 region_erase_mask, sorted_erase_mask;

	while (region) {
		region_erase_mask = region->offset & SNOR_ERASE_TYPE_MASK;

		sorted_erase_mask = spi_nor_sort_erase_mask(map,
							    region_erase_mask);

		/* Overwrite erase mask. */
		region->offset = (region->offset & ~SNOR_ERASE_TYPE_MASK) |
				 sorted_erase_mask;

		region = spi_nor_region_next(region);
	}
}

/**
 * spi_nor_parse_bfpt() - read and parse the Basic Flash Parameter Table.
 * @nor:		pointer to a 'struct spi_nor'
 * @bfpt_header:	pointer to the 'struct sfdp_parameter_header' describing
 *			the Basic Flash Parameter Table length and version
 * @params:		pointer to the 'struct spi_nor_flash_parameter' to be
 *			filled
 *
 * The Basic Flash Parameter Table is the main and only mandatory table as
 * defined by the SFDP (JESD216) specification.
 * It provides us with the total size (memory density) of the data array and
 * the number of address bytes for Fast Read, Page Program and Sector Erase
 * commands.
 * For Fast READ commands, it also gives the number of mode clock cycles and
 * wait states (regrouped in the number of dummy clock cycles) for each
 * supported instruction op code.
 * For Page Program, the page size is now available since JESD216 rev A, however
 * the supported instruction op codes are still not provided.
 * For Sector Erase commands, this table stores the supported instruction op
 * codes and the associated sector sizes.
 * Finally, the Quad Enable Requirements (QER) are also available since JESD216
 * rev A. The QER bits encode the manufacturer dependent procedure to be
 * executed to set the Quad Enable (QE) bit in some internal register of the
 * Quad SPI memory. Indeed the QE bit, when it exists, must be set before
 * sending any Quad SPI command to the memory. Actually, setting the QE bit
 * tells the memory to reassign its WP# and HOLD#/RESET# pins to functions IO2
 * and IO3 hence enabling 4 (Quad) I/O lines.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_parse_bfpt(struct spi_nor *nor,
			      const struct sfdp_parameter_header *bfpt_header,
			      struct spi_nor_flash_parameter *params)
{
	struct spi_nor_erase_map *map = &params->erase_map;
	struct spi_nor_erase_type *erase_type = map->erase_type;
	struct sfdp_bfpt bfpt;
	size_t len;
	int i, cmd, err;
	u32 addr, val;
	u16 half;
	u8 erase_mask;

	/* JESD216 Basic Flash Parameter Table length is at least 9 DWORDs. */
	if (bfpt_header->length < BFPT_DWORD_MAX_JESD216)
		return -EINVAL;

	/* Read the Basic Flash Parameter Table. */
	len = min_t(size_t, sizeof(bfpt),
		    bfpt_header->length * sizeof(u32));
	addr = SFDP_PARAM_HEADER_PTP(bfpt_header);
	memset(&bfpt, 0, sizeof(bfpt));
	err = spi_nor_read_sfdp_dma_unsafe(nor,  addr, len, &bfpt);
	if (err < 0)
		return err;

	/* Fix endianness of the BFPT DWORDs. */
	le32_to_cpu_array(bfpt.dwords, BFPT_DWORD_MAX);

	/* Number of address bytes. */
	switch (bfpt.dwords[BFPT_DWORD(1)] & BFPT_DWORD1_ADDRESS_BYTES_MASK) {
	case BFPT_DWORD1_ADDRESS_BYTES_3_ONLY:
	case BFPT_DWORD1_ADDRESS_BYTES_3_OR_4:
		nor->addr_width = 3;
		break;

	case BFPT_DWORD1_ADDRESS_BYTES_4_ONLY:
		nor->addr_width = 4;
		break;

	default:
		break;
	}

	/* Flash Memory Density (in bits). */
	val = bfpt.dwords[BFPT_DWORD(2)];
	if (val & BIT(31)) {
		val &= ~BIT(31);

		/*
		 * Prevent overflows on params->size. Anyway, a NOR of 2^64
		 * bits is unlikely to exist so this error probably means
		 * the BFPT we are reading is corrupted/wrong.
		 */
		if (val > 63)
			return -EINVAL;

		params->size = 1ULL << val;
	} else {
		params->size = val + 1;
	}
	params->size >>= 3; /* Convert to bytes. */

	/* Fast Read settings. */
	for (i = 0; i < ARRAY_SIZE(sfdp_bfpt_reads); i++) {
		const struct sfdp_bfpt_read *rd = &sfdp_bfpt_reads[i];
		struct spi_nor_read_command *read;

		if (!(bfpt.dwords[rd->supported_dword] & rd->supported_bit)) {
			params->hwcaps.mask &= ~rd->hwcaps;
			continue;
		}

		params->hwcaps.mask |= rd->hwcaps;
		cmd = spi_nor_hwcaps_read2cmd(rd->hwcaps);
		read = &params->reads[cmd];
		half = bfpt.dwords[rd->settings_dword] >> rd->settings_shift;
		spi_nor_set_read_settings_from_bfpt(read, half, rd->proto);
	}

	/*
	 * Sector Erase settings. Reinitialize the uniform erase map using the
	 * Erase Types defined in the bfpt table.
	 */
	erase_mask = 0;
	memset(&params->erase_map, 0, sizeof(params->erase_map));
	for (i = 0; i < ARRAY_SIZE(sfdp_bfpt_erases); i++) {
		const struct sfdp_bfpt_erase *er = &sfdp_bfpt_erases[i];
		u32 erasesize;
		u8 opcode;

		half = bfpt.dwords[er->dword] >> er->shift;
		erasesize = half & 0xff;

		/* erasesize == 0 means this Erase Type is not supported. */
		if (!erasesize)
			continue;

		erasesize = 1U << erasesize;
		opcode = (half >> 8) & 0xff;
		erase_mask |= BIT(i);
		spi_nor_set_erase_settings_from_bfpt(&erase_type[i], erasesize,
						     opcode, i);
	}
	spi_nor_init_uniform_erase_map(map, erase_mask, params->size);
	/*
	 * Sort all the map's Erase Types in ascending order with the smallest
	 * erase size being the first member in the erase_type array.
	 */
	sort(erase_type, SNOR_ERASE_TYPE_MAX, sizeof(erase_type[0]),
	     spi_nor_map_cmp_erase_type, NULL);
	/*
	 * Sort the erase types in the uniform region in order to update the
	 * uniform_erase_type bitmask. The bitmask will be used later on when
	 * selecting the uniform erase.
	 */
	spi_nor_regions_sort_erase_types(map);
	map->uniform_erase_type = map->uniform_region.offset &
				  SNOR_ERASE_TYPE_MASK;

	/* Stop here if not JESD216 rev A or later. */
	if (bfpt_header->length == BFPT_DWORD_MAX_JESD216)
		return spi_nor_post_bfpt_fixups(nor, bfpt_header, &bfpt,
						params);

	/* Page size: this field specifies 'N' so the page size = 2^N bytes. */
	val = bfpt.dwords[BFPT_DWORD(11)];
	val &= BFPT_DWORD11_PAGE_SIZE_MASK;
	val >>= BFPT_DWORD11_PAGE_SIZE_SHIFT;
	params->page_size = 1U << val;

	/* Quad Enable Requirements. */
	switch (bfpt.dwords[BFPT_DWORD(15)] & BFPT_DWORD15_QER_MASK) {
	case BFPT_DWORD15_QER_NONE:
		params->quad_enable = NULL;
		break;

	case BFPT_DWORD15_QER_SR2_BIT1_BUGGY:
		/*
		 * Writing only one byte to the Status Register has the
		 * side-effect of clearing Status Register 2.
		 */
	case BFPT_DWORD15_QER_SR2_BIT1_NO_RD:
		/*
		 * Read Configuration Register (35h) instruction is not
		 * supported.
		 */
		nor->flags |= SNOR_F_HAS_16BIT_SR | SNOR_F_NO_READ_CR;
		params->quad_enable = spi_nor_sr2_bit1_quad_enable;
		break;

	case BFPT_DWORD15_QER_SR1_BIT6:
		nor->flags &= ~SNOR_F_HAS_16BIT_SR;
		params->quad_enable = spi_nor_sr1_bit6_quad_enable;
		break;

	case BFPT_DWORD15_QER_SR2_BIT7:
		nor->flags &= ~SNOR_F_HAS_16BIT_SR;
		params->quad_enable = spi_nor_sr2_bit7_quad_enable;
		break;

	case BFPT_DWORD15_QER_SR2_BIT1:
		/*
		 * JESD216 rev B or later does not specify if writing only one
		 * byte to the Status Register clears or not the Status
		 * Register 2, so let's be cautious and keep the default
		 * assumption of a 16-bit Write Status (01h) command.
		 */
		nor->flags |= SNOR_F_HAS_16BIT_SR;

		params->quad_enable = spi_nor_sr2_bit1_quad_enable;
		break;

	default:
		dev_dbg(nor->dev, "BFPT QER reserved value used\n");
		break;
	}

	/* Stop here if not JESD216 rev C or later. */
	if (bfpt_header->length == BFPT_DWORD_MAX_JESD216B)
		return spi_nor_post_bfpt_fixups(nor, bfpt_header, &bfpt,
						params);

	return spi_nor_post_bfpt_fixups(nor, bfpt_header, &bfpt, params);
}

/**
 * spi_nor_smpt_addr_width() - return the address width used in the
 *			       configuration detection command.
 * @nor:	pointer to a 'struct spi_nor'
 * @settings:	configuration detection command descriptor, dword1
 */
static u8 spi_nor_smpt_addr_width(const struct spi_nor *nor, const u32 settings)
{
	switch (settings & SMPT_CMD_ADDRESS_LEN_MASK) {
	case SMPT_CMD_ADDRESS_LEN_0:
		return 0;
	case SMPT_CMD_ADDRESS_LEN_3:
		return 3;
	case SMPT_CMD_ADDRESS_LEN_4:
		return 4;
	case SMPT_CMD_ADDRESS_LEN_USE_CURRENT:
	default:
		return nor->addr_width;
	}
}

/**
 * spi_nor_smpt_read_dummy() - return the configuration detection command read
 *			       latency, in clock cycles.
 * @nor:	pointer to a 'struct spi_nor'
 * @settings:	configuration detection command descriptor, dword1
 *
 * Return: the number of dummy cycles for an SMPT read
 */
static u8 spi_nor_smpt_read_dummy(const struct spi_nor *nor, const u32 settings)
{
	u8 read_dummy = SMPT_CMD_READ_DUMMY(settings);

	if (read_dummy == SMPT_CMD_READ_DUMMY_IS_VARIABLE)
		return nor->read_dummy;
	return read_dummy;
}

/**
 * spi_nor_get_map_in_use() - get the configuration map in use
 * @nor:	pointer to a 'struct spi_nor'
 * @smpt:	pointer to the sector map parameter table
 * @smpt_len:	sector map parameter table length
 *
 * Return: pointer to the map in use, ERR_PTR(-errno) otherwise.
 */
static const u32 *spi_nor_get_map_in_use(struct spi_nor *nor, const u32 *smpt,
					 u8 smpt_len)
{
	const u32 *ret;
	u8 *buf;
	u32 addr;
	int err;
	u8 i;
	u8 addr_width, read_opcode, read_dummy;
	u8 read_data_mask, map_id;

	/* Use a kmalloc'ed bounce buffer to guarantee it is DMA-able. */
	buf = kmalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	addr_width = nor->addr_width;
	read_dummy = nor->read_dummy;
	read_opcode = nor->read_opcode;

	map_id = 0;
	/* Determine if there are any optional Detection Command Descriptors */
	for (i = 0; i < smpt_len; i += 2) {
		if (smpt[i] & SMPT_DESC_TYPE_MAP)
			break;

		read_data_mask = SMPT_CMD_READ_DATA(smpt[i]);
		nor->addr_width = spi_nor_smpt_addr_width(nor, smpt[i]);
		nor->read_dummy = spi_nor_smpt_read_dummy(nor, smpt[i]);
		nor->read_opcode = SMPT_CMD_OPCODE(smpt[i]);
		addr = smpt[i + 1];

		err = spi_nor_read_raw(nor, addr, 1, buf);
		if (err) {
			ret = ERR_PTR(err);
			goto out;
		}

		/*
		 * Build an index value that is used to select the Sector Map
		 * Configuration that is currently in use.
		 */
		map_id = map_id << 1 | !!(*buf & read_data_mask);
	}

	/*
	 * If command descriptors are provided, they always precede map
	 * descriptors in the table. There is no need to start the iteration
	 * over smpt array all over again.
	 *
	 * Find the matching configuration map.
	 */
	ret = ERR_PTR(-EINVAL);
	while (i < smpt_len) {
		if (SMPT_MAP_ID(smpt[i]) == map_id) {
			ret = smpt + i;
			break;
		}

		/*
		 * If there are no more configuration map descriptors and no
		 * configuration ID matched the configuration identifier, the
		 * sector address map is unknown.
		 */
		if (smpt[i] & SMPT_DESC_END)
			break;

		/* increment the table index to the next map */
		i += SMPT_MAP_REGION_COUNT(smpt[i]) + 1;
	}

	/* fall through */
out:
	kfree(buf);
	nor->addr_width = addr_width;
	nor->read_dummy = read_dummy;
	nor->read_opcode = read_opcode;
	return ret;
}

static void spi_nor_region_mark_end(struct spi_nor_erase_region *region)
{
	region->offset |= SNOR_LAST_REGION;
}

static void spi_nor_region_mark_overlay(struct spi_nor_erase_region *region)
{
	region->offset |= SNOR_OVERLAID_REGION;
}

/**
 * spi_nor_region_check_overlay() - set overlay bit when the region is overlaid
 * @region:	pointer to a structure that describes a SPI NOR erase region
 * @erase:	pointer to a structure that describes a SPI NOR erase type
 * @erase_type:	erase type bitmask
 */
static void
spi_nor_region_check_overlay(struct spi_nor_erase_region *region,
			     const struct spi_nor_erase_type *erase,
			     const u8 erase_type)
{
	int i;

	for (i = 0; i < SNOR_ERASE_TYPE_MAX; i++) {
		if (!(erase[i].size && erase_type & BIT(erase[i].idx)))
			continue;
		if (region->size & erase[i].size_mask) {
			spi_nor_region_mark_overlay(region);
			return;
		}
	}
}

/**
 * spi_nor_init_non_uniform_erase_map() - initialize the non-uniform erase map
 * @nor:	pointer to a 'struct spi_nor'
 * @params:     pointer to a duplicate 'struct spi_nor_flash_parameter' that is
 *              used for storing SFDP parsed data
 * @smpt:	pointer to the sector map parameter table
 *
 * Return: 0 on success, -errno otherwise.
 */
static int
spi_nor_init_non_uniform_erase_map(struct spi_nor *nor,
				   struct spi_nor_flash_parameter *params,
				   const u32 *smpt)
{
	struct spi_nor_erase_map *map = &params->erase_map;
	struct spi_nor_erase_type *erase = map->erase_type;
	struct spi_nor_erase_region *region;
	u64 offset;
	u32 region_count;
	int i, j;
	u8 uniform_erase_type, save_uniform_erase_type;
	u8 erase_type, regions_erase_type;

	region_count = SMPT_MAP_REGION_COUNT(*smpt);
	/*
	 * The regions will be freed when the driver detaches from the
	 * device.
	 */
	region = devm_kcalloc(nor->dev, region_count, sizeof(*region),
			      GFP_KERNEL);
	if (!region)
		return -ENOMEM;
	map->regions = region;

	uniform_erase_type = 0xff;
	regions_erase_type = 0;
	offset = 0;
	/* Populate regions. */
	for (i = 0; i < region_count; i++) {
		j = i + 1; /* index for the region dword */
		region[i].size = SMPT_MAP_REGION_SIZE(smpt[j]);
		erase_type = SMPT_MAP_REGION_ERASE_TYPE(smpt[j]);
		region[i].offset = offset | erase_type;

		spi_nor_region_check_overlay(&region[i], erase, erase_type);

		/*
		 * Save the erase types that are supported in all regions and
		 * can erase the entire flash memory.
		 */
		uniform_erase_type &= erase_type;

		/*
		 * regions_erase_type mask will indicate all the erase types
		 * supported in this configuration map.
		 */
		regions_erase_type |= erase_type;

		offset = (region[i].offset & ~SNOR_ERASE_FLAGS_MASK) +
			 region[i].size;
	}
	spi_nor_region_mark_end(&region[i - 1]);

	save_uniform_erase_type = map->uniform_erase_type;
	map->uniform_erase_type = spi_nor_sort_erase_mask(map,
							  uniform_erase_type);

	if (!regions_erase_type) {
		/*
		 * Roll back to the previous uniform_erase_type mask, SMPT is
		 * broken.
		 */
		map->uniform_erase_type = save_uniform_erase_type;
		return -EINVAL;
	}

	/*
	 * BFPT advertises all the erase types supported by all the possible
	 * map configurations. Mask out the erase types that are not supported
	 * by the current map configuration.
	 */
	for (i = 0; i < SNOR_ERASE_TYPE_MAX; i++)
		if (!(regions_erase_type & BIT(erase[i].idx)))
			spi_nor_set_erase_type(&erase[i], 0, 0xFF);

	return 0;
}

/**
 * spi_nor_parse_smpt() - parse Sector Map Parameter Table
 * @nor:		pointer to a 'struct spi_nor'
 * @smpt_header:	sector map parameter table header
 * @params:		pointer to a duplicate 'struct spi_nor_flash_parameter'
 *                      that is used for storing SFDP parsed data
 *
 * This table is optional, but when available, we parse it to identify the
 * location and size of sectors within the main data array of the flash memory
 * device and to identify which Erase Types are supported by each sector.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_parse_smpt(struct spi_nor *nor,
			      const struct sfdp_parameter_header *smpt_header,
			      struct spi_nor_flash_parameter *params)
{
	const u32 *sector_map;
	u32 *smpt;
	size_t len;
	u32 addr;
	int ret;

	/* Read the Sector Map Parameter Table. */
	len = smpt_header->length * sizeof(*smpt);
	smpt = kmalloc(len, GFP_KERNEL);
	if (!smpt)
		return -ENOMEM;

	addr = SFDP_PARAM_HEADER_PTP(smpt_header);
	ret = spi_nor_read_sfdp(nor, addr, len, smpt);
	if (ret)
		goto out;

	/* Fix endianness of the SMPT DWORDs. */
	le32_to_cpu_array(smpt, smpt_header->length);

	sector_map = spi_nor_get_map_in_use(nor, smpt, smpt_header->length);
	if (IS_ERR(sector_map)) {
		ret = PTR_ERR(sector_map);
		goto out;
	}

	ret = spi_nor_init_non_uniform_erase_map(nor, params, sector_map);
	if (ret)
		goto out;

	spi_nor_regions_sort_erase_types(&params->erase_map);
	/* fall through */
out:
	kfree(smpt);
	return ret;
}

/**
 * spi_nor_parse_4bait() - parse the 4-Byte Address Instruction Table
 * @nor:		pointer to a 'struct spi_nor'.
 * @param_header:	pointer to the 'struct sfdp_parameter_header' describing
 *			the 4-Byte Address Instruction Table length and version.
 * @params:		pointer to the 'struct spi_nor_flash_parameter' to be.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int spi_nor_parse_4bait(struct spi_nor *nor,
			       const struct sfdp_parameter_header *param_header,
			       struct spi_nor_flash_parameter *params)
{
	static const struct sfdp_4bait reads[] = {
		{ SNOR_HWCAPS_READ,		BIT(0) },
		{ SNOR_HWCAPS_READ_FAST,	BIT(1) },
		{ SNOR_HWCAPS_READ_1_1_2,	BIT(2) },
		{ SNOR_HWCAPS_READ_1_2_2,	BIT(3) },
		{ SNOR_HWCAPS_READ_1_1_4,	BIT(4) },
		{ SNOR_HWCAPS_READ_1_4_4,	BIT(5) },
		{ SNOR_HWCAPS_READ_1_1_1_DTR,	BIT(13) },
		{ SNOR_HWCAPS_READ_1_2_2_DTR,	BIT(14) },
		{ SNOR_HWCAPS_READ_1_4_4_DTR,	BIT(15) },
	};
	static const struct sfdp_4bait programs[] = {
		{ SNOR_HWCAPS_PP,		BIT(6) },
		{ SNOR_HWCAPS_PP_1_1_4,		BIT(7) },
		{ SNOR_HWCAPS_PP_1_4_4,		BIT(8) },
	};
	static const struct sfdp_4bait erases[SNOR_ERASE_TYPE_MAX] = {
		{ 0u /* not used */,		BIT(9) },
		{ 0u /* not used */,		BIT(10) },
		{ 0u /* not used */,		BIT(11) },
		{ 0u /* not used */,		BIT(12) },
	};
	struct spi_nor_pp_command *params_pp = params->page_programs;
	struct spi_nor_erase_map *map = &params->erase_map;
	struct spi_nor_erase_type *erase_type = map->erase_type;
	u32 *dwords;
	size_t len;
	u32 addr, discard_hwcaps, read_hwcaps, pp_hwcaps, erase_mask;
	int i, ret;

	if (param_header->major != SFDP_JESD216_MAJOR ||
	    param_header->length < SFDP_4BAIT_DWORD_MAX)
		return -EINVAL;

	/* Read the 4-byte Address Instruction Table. */
	len = sizeof(*dwords) * SFDP_4BAIT_DWORD_MAX;

	/* Use a kmalloc'ed bounce buffer to guarantee it is DMA-able. */
	dwords = kmalloc(len, GFP_KERNEL);
	if (!dwords)
		return -ENOMEM;

	addr = SFDP_PARAM_HEADER_PTP(param_header);
	ret = spi_nor_read_sfdp(nor, addr, len, dwords);
	if (ret)
		goto out;

	/* Fix endianness of the 4BAIT DWORDs. */
	le32_to_cpu_array(dwords, SFDP_4BAIT_DWORD_MAX);

	/*
	 * Compute the subset of (Fast) Read commands for which the 4-byte
	 * version is supported.
	 */
	discard_hwcaps = 0;
	read_hwcaps = 0;
	for (i = 0; i < ARRAY_SIZE(reads); i++) {
		const struct sfdp_4bait *read = &reads[i];

		discard_hwcaps |= read->hwcaps;
		if ((params->hwcaps.mask & read->hwcaps) &&
		    (dwords[0] & read->supported_bit))
			read_hwcaps |= read->hwcaps;
	}

	/*
	 * Compute the subset of Page Program commands for which the 4-byte
	 * version is supported.
	 */
	pp_hwcaps = 0;
	for (i = 0; i < ARRAY_SIZE(programs); i++) {
		const struct sfdp_4bait *program = &programs[i];

		/*
		 * The 4 Byte Address Instruction (Optional) Table is the only
		 * SFDP table that indicates support for Page Program Commands.
		 * Bypass the params->hwcaps.mask and consider 4BAIT the biggest
		 * authority for specifying Page Program support.
		 */
		discard_hwcaps |= program->hwcaps;
		if (dwords[0] & program->supported_bit)
			pp_hwcaps |= program->hwcaps;
	}

	/*
	 * Compute the subset of Sector Erase commands for which the 4-byte
	 * version is supported.
	 */
	erase_mask = 0;
	for (i = 0; i < SNOR_ERASE_TYPE_MAX; i++) {
		const struct sfdp_4bait *erase = &erases[i];

		if (dwords[0] & erase->supported_bit)
			erase_mask |= BIT(i);
	}

	/* Replicate the sort done for the map's erase types in BFPT. */
	erase_mask = spi_nor_sort_erase_mask(map, erase_mask);

	/*
	 * We need at least one 4-byte op code per read, program and erase
	 * operation; the .read(), .write() and .erase() hooks share the
	 * nor->addr_width value.
	 */
	if (!read_hwcaps || !pp_hwcaps || !erase_mask)
		goto out;

	/*
	 * Discard all operations from the 4-byte instruction set which are
	 * not supported by this memory.
	 */
	params->hwcaps.mask &= ~discard_hwcaps;
	params->hwcaps.mask |= (read_hwcaps | pp_hwcaps);

	/* Use the 4-byte address instruction set. */
	for (i = 0; i < SNOR_CMD_READ_MAX; i++) {
		struct spi_nor_read_command *read_cmd = &params->reads[i];

		read_cmd->opcode = spi_nor_convert_3to4_read(read_cmd->opcode);
	}

	/* 4BAIT is the only SFDP table that indicates page program support. */
	if (pp_hwcaps & SNOR_HWCAPS_PP)
		spi_nor_set_pp_settings(&params_pp[SNOR_CMD_PP],
					SPINOR_OP_PP_4B, SNOR_PROTO_1_1_1);
	if (pp_hwcaps & SNOR_HWCAPS_PP_1_1_4)
		spi_nor_set_pp_settings(&params_pp[SNOR_CMD_PP_1_1_4],
					SPINOR_OP_PP_1_1_4_4B,
					SNOR_PROTO_1_1_4);
	if (pp_hwcaps & SNOR_HWCAPS_PP_1_4_4)
		spi_nor_set_pp_settings(&params_pp[SNOR_CMD_PP_1_4_4],
					SPINOR_OP_PP_1_4_4_4B,
					SNOR_PROTO_1_4_4);

	for (i = 0; i < SNOR_ERASE_TYPE_MAX; i++) {
		if (erase_mask & BIT(i))
			erase_type[i].opcode = (dwords[1] >>
						erase_type[i].idx * 8) & 0xFF;
		else
			spi_nor_set_erase_type(&erase_type[i], 0u, 0xFF);
	}

	/*
	 * We set SNOR_F_HAS_4BAIT in order to skip spi_nor_set_4byte_opcodes()
	 * later because we already did the conversion to 4byte opcodes. Also,
	 * this latest function implements a legacy quirk for the erase size of
	 * Spansion memory. However this quirk is no longer needed with new
	 * SFDP compliant memories.
	 */
	nor->addr_width = 4;
	nor->flags |= SNOR_F_4B_OPCODES | SNOR_F_HAS_4BAIT;

	/* fall through */
out:
	kfree(dwords);
	return ret;
}

/**
 * spi_nor_parse_sfdp() - parse the Serial Flash Discoverable Parameters.
 * @nor:		pointer to a 'struct spi_nor'
 * @params:		pointer to the 'struct spi_nor_flash_parameter' to be
 *			filled
 *
 * The Serial Flash Discoverable Parameters are described by the JEDEC JESD216
 * specification. This is a standard which tends to supported by almost all
 * (Q)SPI memory manufacturers. Those hard-coded tables allow us to learn at
 * runtime the main parameters needed to perform basic SPI flash operations such
 * as Fast Read, Page Program or Sector Erase commands.
 *
 * Return: 0 on success, -errno otherwise.
 */
int spi_nor_parse_sfdp(struct spi_nor *nor,
		       struct spi_nor_flash_parameter *params)
{
	const struct sfdp_parameter_header *param_header, *bfpt_header;
	struct sfdp_parameter_header *param_headers = NULL;
	struct sfdp_header header;
	struct device *dev = nor->dev;
	size_t psize;
	int i, err;

	/* Get the SFDP header. */
	err = spi_nor_read_sfdp_dma_unsafe(nor, 0, sizeof(header), &header);
	if (err < 0)
		return err;

	/* Check the SFDP header version. */
	if (le32_to_cpu(header.signature) != SFDP_SIGNATURE ||
	    header.major != SFDP_JESD216_MAJOR)
		return -EINVAL;

	/*
	 * Verify that the first and only mandatory parameter header is a
	 * Basic Flash Parameter Table header as specified in JESD216.
	 */
	bfpt_header = &header.bfpt_header;
	if (SFDP_PARAM_HEADER_ID(bfpt_header) != SFDP_BFPT_ID ||
	    bfpt_header->major != SFDP_JESD216_MAJOR)
		return -EINVAL;

	/*
	 * Allocate memory then read all parameter headers with a single
	 * Read SFDP command. These parameter headers will actually be parsed
	 * twice: a first time to get the latest revision of the basic flash
	 * parameter table, then a second time to handle the supported optional
	 * tables.
	 * Hence we read the parameter headers once for all to reduce the
	 * processing time. Also we use kmalloc() instead of devm_kmalloc()
	 * because we don't need to keep these parameter headers: the allocated
	 * memory is always released with kfree() before exiting this function.
	 */
	if (header.nph) {
		psize = header.nph * sizeof(*param_headers);

		param_headers = kmalloc(psize, GFP_KERNEL);
		if (!param_headers)
			return -ENOMEM;

		err = spi_nor_read_sfdp(nor, sizeof(header),
					psize, param_headers);
		if (err < 0) {
			dev_dbg(dev, "failed to read SFDP parameter headers\n");
			goto exit;
		}
	}

	/*
	 * Check other parameter headers to get the latest revision of
	 * the basic flash parameter table.
	 */
	for (i = 0; i < header.nph; i++) {
		param_header = &param_headers[i];

		if (SFDP_PARAM_HEADER_ID(param_header) == SFDP_BFPT_ID &&
		    param_header->major == SFDP_JESD216_MAJOR &&
		    (param_header->minor > bfpt_header->minor ||
		     (param_header->minor == bfpt_header->minor &&
		      param_header->length > bfpt_header->length)))
			bfpt_header = param_header;
	}

	err = spi_nor_parse_bfpt(nor, bfpt_header, params);
	if (err)
		goto exit;

	/* Parse optional parameter tables. */
	for (i = 0; i < header.nph; i++) {
		param_header = &param_headers[i];

		switch (SFDP_PARAM_HEADER_ID(param_header)) {
		case SFDP_SECTOR_MAP_ID:
			err = spi_nor_parse_smpt(nor, param_header, params);
			break;

		case SFDP_4BAIT_ID:
			err = spi_nor_parse_4bait(nor, param_header, params);
			break;

		default:
			break;
		}

		if (err) {
			dev_warn(dev, "Failed to parse optional parameter table: %04x\n",
				 SFDP_PARAM_HEADER_ID(param_header));
			/*
			 * Let's not drop all information we extracted so far
			 * if optional table parsers fail. In case of failing,
			 * each optional parser is responsible to roll back to
			 * the previously known spi_nor data.
			 */
			err = 0;
		}
	}

exit:
	kfree(param_headers);
	return err;
}
