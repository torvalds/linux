/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#ifndef __LINUX_MTD_SPI_NOR_INTERNAL_H
#define __LINUX_MTD_SPI_NOR_INTERNAL_H

#include "sfdp.h"

#define SPI_NOR_MAX_ID_LEN	6

enum spi_nor_option_flags {
	SNOR_F_USE_FSR		= BIT(0),
	SNOR_F_HAS_SR_TB	= BIT(1),
	SNOR_F_NO_OP_CHIP_ERASE	= BIT(2),
	SNOR_F_READY_XSR_RDY	= BIT(3),
	SNOR_F_USE_CLSR		= BIT(4),
	SNOR_F_BROKEN_RESET	= BIT(5),
	SNOR_F_4B_OPCODES	= BIT(6),
	SNOR_F_HAS_4BAIT	= BIT(7),
	SNOR_F_HAS_LOCK		= BIT(8),
	SNOR_F_HAS_16BIT_SR	= BIT(9),
	SNOR_F_NO_READ_CR	= BIT(10),
	SNOR_F_HAS_SR_TB_BIT6	= BIT(11),
	SNOR_F_HAS_4BIT_BP      = BIT(12),
	SNOR_F_HAS_SR_BP3_BIT6  = BIT(13),
	SNOR_F_IO_MODE_EN_VOLATILE = BIT(14),
	SNOR_F_SOFT_RESET	= BIT(15),
	SNOR_F_SWP_IS_VOLATILE	= BIT(16),
};

struct spi_nor_read_command {
	u8			num_mode_clocks;
	u8			num_wait_states;
	u8			opcode;
	enum spi_nor_protocol	proto;
};

struct spi_nor_pp_command {
	u8			opcode;
	enum spi_nor_protocol	proto;
};

enum spi_nor_read_command_index {
	SNOR_CMD_READ,
	SNOR_CMD_READ_FAST,
	SNOR_CMD_READ_1_1_1_DTR,

	/* Dual SPI */
	SNOR_CMD_READ_1_1_2,
	SNOR_CMD_READ_1_2_2,
	SNOR_CMD_READ_2_2_2,
	SNOR_CMD_READ_1_2_2_DTR,

	/* Quad SPI */
	SNOR_CMD_READ_1_1_4,
	SNOR_CMD_READ_1_4_4,
	SNOR_CMD_READ_4_4_4,
	SNOR_CMD_READ_1_4_4_DTR,

	/* Octal SPI */
	SNOR_CMD_READ_1_1_8,
	SNOR_CMD_READ_1_8_8,
	SNOR_CMD_READ_8_8_8,
	SNOR_CMD_READ_1_8_8_DTR,
	SNOR_CMD_READ_8_8_8_DTR,

	SNOR_CMD_READ_MAX
};

enum spi_nor_pp_command_index {
	SNOR_CMD_PP,

	/* Quad SPI */
	SNOR_CMD_PP_1_1_4,
	SNOR_CMD_PP_1_4_4,
	SNOR_CMD_PP_4_4_4,

	/* Octal SPI */
	SNOR_CMD_PP_1_1_8,
	SNOR_CMD_PP_1_8_8,
	SNOR_CMD_PP_8_8_8,
	SNOR_CMD_PP_8_8_8_DTR,

	SNOR_CMD_PP_MAX
};

/**
 * struct spi_nor_erase_type - Structure to describe a SPI NOR erase type
 * @size:		the size of the sector/block erased by the erase type.
 *			JEDEC JESD216B imposes erase sizes to be a power of 2.
 * @size_shift:		@size is a power of 2, the shift is stored in
 *			@size_shift.
 * @size_mask:		the size mask based on @size_shift.
 * @opcode:		the SPI command op code to erase the sector/block.
 * @idx:		Erase Type index as sorted in the Basic Flash Parameter
 *			Table. It will be used to synchronize the supported
 *			Erase Types with the ones identified in the SFDP
 *			optional tables.
 */
struct spi_nor_erase_type {
	u32	size;
	u32	size_shift;
	u32	size_mask;
	u8	opcode;
	u8	idx;
};

/**
 * struct spi_nor_erase_command - Used for non-uniform erases
 * The structure is used to describe a list of erase commands to be executed
 * once we validate that the erase can be performed. The elements in the list
 * are run-length encoded.
 * @list:		for inclusion into the list of erase commands.
 * @count:		how many times the same erase command should be
 *			consecutively used.
 * @size:		the size of the sector/block erased by the command.
 * @opcode:		the SPI command op code to erase the sector/block.
 */
struct spi_nor_erase_command {
	struct list_head	list;
	u32			count;
	u32			size;
	u8			opcode;
};

/**
 * struct spi_nor_erase_region - Structure to describe a SPI NOR erase region
 * @offset:		the offset in the data array of erase region start.
 *			LSB bits are used as a bitmask encoding flags to
 *			determine if this region is overlaid, if this region is
 *			the last in the SPI NOR flash memory and to indicate
 *			all the supported erase commands inside this region.
 *			The erase types are sorted in ascending order with the
 *			smallest Erase Type size being at BIT(0).
 * @size:		the size of the region in bytes.
 */
struct spi_nor_erase_region {
	u64		offset;
	u64		size;
};

#define SNOR_ERASE_TYPE_MAX	4
#define SNOR_ERASE_TYPE_MASK	GENMASK_ULL(SNOR_ERASE_TYPE_MAX - 1, 0)

#define SNOR_LAST_REGION	BIT(4)
#define SNOR_OVERLAID_REGION	BIT(5)

#define SNOR_ERASE_FLAGS_MAX	6
#define SNOR_ERASE_FLAGS_MASK	GENMASK_ULL(SNOR_ERASE_FLAGS_MAX - 1, 0)

/**
 * struct spi_nor_erase_map - Structure to describe the SPI NOR erase map
 * @regions:		array of erase regions. The regions are consecutive in
 *			address space. Walking through the regions is done
 *			incrementally.
 * @uniform_region:	a pre-allocated erase region for SPI NOR with a uniform
 *			sector size (legacy implementation).
 * @erase_type:		an array of erase types shared by all the regions.
 *			The erase types are sorted in ascending order, with the
 *			smallest Erase Type size being the first member in the
 *			erase_type array.
 * @uniform_erase_type:	bitmask encoding erase types that can erase the
 *			entire memory. This member is completed at init by
 *			uniform and non-uniform SPI NOR flash memories if they
 *			support at least one erase type that can erase the
 *			entire memory.
 */
struct spi_nor_erase_map {
	struct spi_nor_erase_region	*regions;
	struct spi_nor_erase_region	uniform_region;
	struct spi_nor_erase_type	erase_type[SNOR_ERASE_TYPE_MAX];
	u8				uniform_erase_type;
};

/**
 * struct spi_nor_locking_ops - SPI NOR locking methods
 * @lock:	lock a region of the SPI NOR.
 * @unlock:	unlock a region of the SPI NOR.
 * @is_locked:	check if a region of the SPI NOR is completely locked
 */
struct spi_nor_locking_ops {
	int (*lock)(struct spi_nor *nor, loff_t ofs, uint64_t len);
	int (*unlock)(struct spi_nor *nor, loff_t ofs, uint64_t len);
	int (*is_locked)(struct spi_nor *nor, loff_t ofs, uint64_t len);
};

/**
 * struct spi_nor_otp_organization - Structure to describe the SPI NOR OTP regions
 * @len:	size of one OTP region in bytes.
 * @base:	start address of the OTP area.
 * @offset:	offset between consecutive OTP regions if there are more
 *              than one.
 * @n_regions:	number of individual OTP regions.
 */
struct spi_nor_otp_organization {
	size_t len;
	loff_t base;
	loff_t offset;
	unsigned int n_regions;
};

/**
 * struct spi_nor_otp_ops - SPI NOR OTP methods
 * @read:	read from the SPI NOR OTP area.
 * @write:	write to the SPI NOR OTP area.
 * @lock:	lock an OTP region.
 * @erase:	erase an OTP region.
 * @is_locked:	check if an OTP region of the SPI NOR is locked.
 */
struct spi_nor_otp_ops {
	int (*read)(struct spi_nor *nor, loff_t addr, size_t len, u8 *buf);
	int (*write)(struct spi_nor *nor, loff_t addr, size_t len,
		     const u8 *buf);
	int (*lock)(struct spi_nor *nor, unsigned int region);
	int (*erase)(struct spi_nor *nor, loff_t addr);
	int (*is_locked)(struct spi_nor *nor, unsigned int region);
};

/**
 * struct spi_nor_otp - SPI NOR OTP grouping structure
 * @org:	OTP region organization
 * @ops:	OTP access ops
 */
struct spi_nor_otp {
	const struct spi_nor_otp_organization *org;
	const struct spi_nor_otp_ops *ops;
};

/**
 * struct spi_nor_flash_parameter - SPI NOR flash parameters and settings.
 * Includes legacy flash parameters and settings that can be overwritten
 * by the spi_nor_fixups hooks, or dynamically when parsing the JESD216
 * Serial Flash Discoverable Parameters (SFDP) tables.
 *
 * @size:		the flash memory density in bytes.
 * @writesize		Minimal writable flash unit size. Defaults to 1. Set to
 *			ECC unit size for ECC-ed flashes.
 * @page_size:		the page size of the SPI NOR flash memory.
 * @rdsr_dummy:		dummy cycles needed for Read Status Register command.
 * @rdsr_addr_nbytes:	dummy address bytes needed for Read Status Register
 *			command.
 * @hwcaps:		describes the read and page program hardware
 *			capabilities.
 * @reads:		read capabilities ordered by priority: the higher index
 *                      in the array, the higher priority.
 * @page_programs:	page program capabilities ordered by priority: the
 *                      higher index in the array, the higher priority.
 * @erase_map:		the erase map parsed from the SFDP Sector Map Parameter
 *                      Table.
 * @otp_info:		describes the OTP regions.
 * @octal_dtr_enable:	enables SPI NOR octal DTR mode.
 * @quad_enable:	enables SPI NOR quad mode.
 * @set_4byte_addr_mode: puts the SPI NOR in 4 byte addressing mode.
 * @convert_addr:	converts an absolute address into something the flash
 *                      will understand. Particularly useful when pagesize is
 *                      not a power-of-2.
 * @setup:              configures the SPI NOR memory. Useful for SPI NOR
 *                      flashes that have peculiarities to the SPI NOR standard
 *                      e.g. different opcodes, specific address calculation,
 *                      page size, etc.
 * @locking_ops:	SPI NOR locking methods.
 * @otp:		SPI NOR OTP methods.
 */
struct spi_nor_flash_parameter {
	u64				size;
	u32				writesize;
	u32				page_size;
	u8				rdsr_dummy;
	u8				rdsr_addr_nbytes;

	struct spi_nor_hwcaps		hwcaps;
	struct spi_nor_read_command	reads[SNOR_CMD_READ_MAX];
	struct spi_nor_pp_command	page_programs[SNOR_CMD_PP_MAX];

	struct spi_nor_erase_map        erase_map;
	struct spi_nor_otp		otp;

	int (*octal_dtr_enable)(struct spi_nor *nor, bool enable);
	int (*quad_enable)(struct spi_nor *nor);
	int (*set_4byte_addr_mode)(struct spi_nor *nor, bool enable);
	u32 (*convert_addr)(struct spi_nor *nor, u32 addr);
	int (*setup)(struct spi_nor *nor, const struct spi_nor_hwcaps *hwcaps);

	const struct spi_nor_locking_ops *locking_ops;
};

/**
 * struct spi_nor_fixups - SPI NOR fixup hooks
 * @default_init: called after default flash parameters init. Used to tweak
 *                flash parameters when information provided by the flash_info
 *                table is incomplete or wrong.
 * @post_bfpt: called after the BFPT table has been parsed
 * @post_sfdp: called after SFDP has been parsed (is also called for SPI NORs
 *             that do not support RDSFDP). Typically used to tweak various
 *             parameters that could not be extracted by other means (i.e.
 *             when information provided by the SFDP/flash_info tables are
 *             incomplete or wrong).
 *
 * Those hooks can be used to tweak the SPI NOR configuration when the SFDP
 * table is broken or not available.
 */
struct spi_nor_fixups {
	void (*default_init)(struct spi_nor *nor);
	int (*post_bfpt)(struct spi_nor *nor,
			 const struct sfdp_parameter_header *bfpt_header,
			 const struct sfdp_bfpt *bfpt);
	void (*post_sfdp)(struct spi_nor *nor);
};

struct flash_info {
	char		*name;

	/*
	 * This array stores the ID bytes.
	 * The first three bytes are the JEDIC ID.
	 * JEDEC ID zero means "no ID" (mostly older chips).
	 */
	u8		id[SPI_NOR_MAX_ID_LEN];
	u8		id_len;

	/* The size listed here is what works with SPINOR_OP_SE, which isn't
	 * necessarily called a "sector" by the vendor.
	 */
	unsigned	sector_size;
	u16		n_sectors;

	u16		page_size;
	u16		addr_width;

	u32		flags;
#define SECT_4K			BIT(0)	/* SPINOR_OP_BE_4K works uniformly */
#define SPI_NOR_NO_ERASE	BIT(1)	/* No erase command needed */
#define SST_WRITE		BIT(2)	/* use SST byte programming */
#define SPI_NOR_NO_FR		BIT(3)	/* Can't do fastread */
#define SECT_4K_PMC		BIT(4)	/* SPINOR_OP_BE_4K_PMC works uniformly */
#define SPI_NOR_DUAL_READ	BIT(5)	/* Flash supports Dual Read */
#define SPI_NOR_QUAD_READ	BIT(6)	/* Flash supports Quad Read */
#define USE_FSR			BIT(7)	/* use flag status register */
#define SPI_NOR_HAS_LOCK	BIT(8)	/* Flash supports lock/unlock via SR */
#define SPI_NOR_HAS_TB		BIT(9)	/*
					 * Flash SR has Top/Bottom (TB) protect
					 * bit. Must be used with
					 * SPI_NOR_HAS_LOCK.
					 */
#define SPI_NOR_XSR_RDY		BIT(10)	/*
					 * S3AN flashes have specific opcode to
					 * read the status register.
					 */
#define SPI_NOR_4B_OPCODES	BIT(11)	/*
					 * Use dedicated 4byte address op codes
					 * to support memory size above 128Mib.
					 */
#define NO_CHIP_ERASE		BIT(12) /* Chip does not support chip erase */
#define SPI_NOR_SKIP_SFDP	BIT(13)	/* Skip parsing of SFDP tables */
#define USE_CLSR		BIT(14)	/* use CLSR command */
#define SPI_NOR_OCTAL_READ	BIT(15)	/* Flash supports Octal Read */
#define SPI_NOR_TB_SR_BIT6	BIT(16)	/*
					 * Top/Bottom (TB) is bit 6 of
					 * status register. Must be used with
					 * SPI_NOR_HAS_TB.
					 */
#define SPI_NOR_4BIT_BP		BIT(17) /*
					 * Flash SR has 4 bit fields (BP0-3)
					 * for block protection.
					 */
#define SPI_NOR_BP3_SR_BIT6	BIT(18) /*
					 * BP3 is bit 6 of status register.
					 * Must be used with SPI_NOR_4BIT_BP.
					 */
#define SPI_NOR_OCTAL_DTR_READ	BIT(19) /* Flash supports octal DTR Read. */
#define SPI_NOR_OCTAL_DTR_PP	BIT(20) /* Flash supports Octal DTR Page Program */
#define SPI_NOR_IO_MODE_EN_VOLATILE	BIT(21) /*
						 * Flash enables the best
						 * available I/O mode via a
						 * volatile bit.
						 */
#define SPI_NOR_SWP_IS_VOLATILE	BIT(22)	/*
					 * Flash has volatile software write
					 * protection bits. Usually these will
					 * power-up in a write-protected state.
					 */

	const struct spi_nor_otp_organization otp_org;

	/* Part specific fixup hooks. */
	const struct spi_nor_fixups *fixups;
};

/* Used when the "_ext_id" is two bytes at most */
#define INFO(_jedec_id, _ext_id, _sector_size, _n_sectors, _flags)	\
		.id = {							\
			((_jedec_id) >> 16) & 0xff,			\
			((_jedec_id) >> 8) & 0xff,			\
			(_jedec_id) & 0xff,				\
			((_ext_id) >> 8) & 0xff,			\
			(_ext_id) & 0xff,				\
			},						\
		.id_len = (!(_jedec_id) ? 0 : (3 + ((_ext_id) ? 2 : 0))),	\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = 256,					\
		.flags = (_flags),

#define INFO6(_jedec_id, _ext_id, _sector_size, _n_sectors, _flags)	\
		.id = {							\
			((_jedec_id) >> 16) & 0xff,			\
			((_jedec_id) >> 8) & 0xff,			\
			(_jedec_id) & 0xff,				\
			((_ext_id) >> 16) & 0xff,			\
			((_ext_id) >> 8) & 0xff,			\
			(_ext_id) & 0xff,				\
			},						\
		.id_len = 6,						\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = 256,					\
		.flags = (_flags),

#define CAT25_INFO(_sector_size, _n_sectors, _page_size, _addr_width, _flags)	\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = (_page_size),				\
		.addr_width = (_addr_width),				\
		.flags = (_flags),

#define S3AN_INFO(_jedec_id, _n_sectors, _page_size)			\
		.id = {							\
			((_jedec_id) >> 16) & 0xff,			\
			((_jedec_id) >> 8) & 0xff,			\
			(_jedec_id) & 0xff				\
			},						\
		.id_len = 3,						\
		.sector_size = (8*_page_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = _page_size,				\
		.addr_width = 3,					\
		.flags = SPI_NOR_NO_FR | SPI_NOR_XSR_RDY,

#define OTP_INFO(_len, _n_regions, _base, _offset)			\
		.otp_org = {						\
			.len = (_len),					\
			.base = (_base),				\
			.offset = (_offset),				\
			.n_regions = (_n_regions),			\
		},

/**
 * struct spi_nor_manufacturer - SPI NOR manufacturer object
 * @name: manufacturer name
 * @parts: array of parts supported by this manufacturer
 * @nparts: number of entries in the parts array
 * @fixups: hooks called at various points in time during spi_nor_scan()
 */
struct spi_nor_manufacturer {
	const char *name;
	const struct flash_info *parts;
	unsigned int nparts;
	const struct spi_nor_fixups *fixups;
};

/**
 * struct sfdp - SFDP data
 * @num_dwords: number of entries in the dwords array
 * @dwords: array of double words of the SFDP data
 */
struct sfdp {
	size_t	num_dwords;
	u32	*dwords;
};

/* Manufacturer drivers. */
extern const struct spi_nor_manufacturer spi_nor_atmel;
extern const struct spi_nor_manufacturer spi_nor_catalyst;
extern const struct spi_nor_manufacturer spi_nor_eon;
extern const struct spi_nor_manufacturer spi_nor_esmt;
extern const struct spi_nor_manufacturer spi_nor_everspin;
extern const struct spi_nor_manufacturer spi_nor_fujitsu;
extern const struct spi_nor_manufacturer spi_nor_gigadevice;
extern const struct spi_nor_manufacturer spi_nor_intel;
extern const struct spi_nor_manufacturer spi_nor_issi;
extern const struct spi_nor_manufacturer spi_nor_macronix;
extern const struct spi_nor_manufacturer spi_nor_micron;
extern const struct spi_nor_manufacturer spi_nor_st;
extern const struct spi_nor_manufacturer spi_nor_spansion;
extern const struct spi_nor_manufacturer spi_nor_sst;
extern const struct spi_nor_manufacturer spi_nor_winbond;
extern const struct spi_nor_manufacturer spi_nor_xilinx;
extern const struct spi_nor_manufacturer spi_nor_xmc;

extern const struct attribute_group *spi_nor_sysfs_groups[];

void spi_nor_spimem_setup_op(const struct spi_nor *nor,
			     struct spi_mem_op *op,
			     const enum spi_nor_protocol proto);
int spi_nor_write_enable(struct spi_nor *nor);
int spi_nor_write_disable(struct spi_nor *nor);
int spi_nor_set_4byte_addr_mode(struct spi_nor *nor, bool enable);
int spi_nor_write_ear(struct spi_nor *nor, u8 ear);
int spi_nor_wait_till_ready(struct spi_nor *nor);
int spi_nor_global_block_unlock(struct spi_nor *nor);
int spi_nor_lock_and_prep(struct spi_nor *nor);
void spi_nor_unlock_and_unprep(struct spi_nor *nor);
int spi_nor_sr1_bit6_quad_enable(struct spi_nor *nor);
int spi_nor_sr2_bit1_quad_enable(struct spi_nor *nor);
int spi_nor_sr2_bit7_quad_enable(struct spi_nor *nor);
int spi_nor_read_sr(struct spi_nor *nor, u8 *sr);
int spi_nor_read_cr(struct spi_nor *nor, u8 *cr);
int spi_nor_write_sr(struct spi_nor *nor, const u8 *sr, size_t len);
int spi_nor_write_sr_and_check(struct spi_nor *nor, u8 sr1);
int spi_nor_write_16bit_cr_and_check(struct spi_nor *nor, u8 cr);

int spi_nor_xread_sr(struct spi_nor *nor, u8 *sr);
ssize_t spi_nor_read_data(struct spi_nor *nor, loff_t from, size_t len,
			  u8 *buf);
ssize_t spi_nor_write_data(struct spi_nor *nor, loff_t to, size_t len,
			   const u8 *buf);
int spi_nor_erase_sector(struct spi_nor *nor, u32 addr);

int spi_nor_otp_read_secr(struct spi_nor *nor, loff_t addr, size_t len, u8 *buf);
int spi_nor_otp_write_secr(struct spi_nor *nor, loff_t addr, size_t len,
			   const u8 *buf);
int spi_nor_otp_erase_secr(struct spi_nor *nor, loff_t addr);
int spi_nor_otp_lock_sr2(struct spi_nor *nor, unsigned int region);
int spi_nor_otp_is_locked_sr2(struct spi_nor *nor, unsigned int region);

int spi_nor_hwcaps_read2cmd(u32 hwcaps);
u8 spi_nor_convert_3to4_read(u8 opcode);
void spi_nor_set_read_settings(struct spi_nor_read_command *read,
			       u8 num_mode_clocks,
			       u8 num_wait_states,
			       u8 opcode,
			       enum spi_nor_protocol proto);
void spi_nor_set_pp_settings(struct spi_nor_pp_command *pp, u8 opcode,
			     enum spi_nor_protocol proto);

void spi_nor_set_erase_type(struct spi_nor_erase_type *erase, u32 size,
			    u8 opcode);
struct spi_nor_erase_region *
spi_nor_region_next(struct spi_nor_erase_region *region);
void spi_nor_init_uniform_erase_map(struct spi_nor_erase_map *map,
				    u8 erase_mask, u64 flash_size);

int spi_nor_post_bfpt_fixups(struct spi_nor *nor,
			     const struct sfdp_parameter_header *bfpt_header,
			     const struct sfdp_bfpt *bfpt);

void spi_nor_init_default_locking_ops(struct spi_nor *nor);
void spi_nor_try_unlock_all(struct spi_nor *nor);
void spi_nor_register_locking_ops(struct spi_nor *nor);
void spi_nor_otp_init(struct spi_nor *nor);

static struct spi_nor __maybe_unused *mtd_to_spi_nor(struct mtd_info *mtd)
{
	return mtd->priv;
}

#endif /* __LINUX_MTD_SPI_NOR_INTERNAL_H */
