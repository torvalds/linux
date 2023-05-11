/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#ifndef __LINUX_MTD_SPI_NOR_INTERNAL_H
#define __LINUX_MTD_SPI_NOR_INTERNAL_H

#include "sfdp.h"

#define SPI_NOR_MAX_ID_LEN	6

/* Standard SPI NOR flash operations. */
#define SPI_NOR_READID_OP(naddr, ndummy, buf, len)			\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RDID, 0),			\
		   SPI_MEM_OP_ADDR(naddr, 0, 0),			\
		   SPI_MEM_OP_DUMMY(ndummy, 0),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 0))

#define SPI_NOR_WREN_OP							\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_WREN, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

#define SPI_NOR_WRDI_OP							\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_WRDI, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

#define SPI_NOR_RDSR_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RDSR, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_IN(1, buf, 0))

#define SPI_NOR_WRSR_OP(buf, len)					\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_WRSR, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(len, buf, 0))

#define SPI_NOR_RDSR2_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RDSR2, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, buf, 0))

#define SPI_NOR_WRSR2_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_WRSR2, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, buf, 0))

#define SPI_NOR_RDCR_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RDCR, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_IN(1, buf, 0))

#define SPI_NOR_EN4B_EX4B_OP(enable)					\
	SPI_MEM_OP(SPI_MEM_OP_CMD(enable ? SPINOR_OP_EN4B : SPINOR_OP_EX4B, 0),	\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

#define SPI_NOR_BRWR_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_BRWR, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, buf, 0))

#define SPI_NOR_GBULK_OP						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_GBULK, 0),			\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

#define SPI_NOR_CHIP_ERASE_OP						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_CHIP_ERASE, 0),		\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

#define SPI_NOR_SECTOR_ERASE_OP(opcode, addr_nbytes, addr)		\
	SPI_MEM_OP(SPI_MEM_OP_CMD(opcode, 0),				\
		   SPI_MEM_OP_ADDR(addr_nbytes, addr, 0),		\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

#define SPI_NOR_READ_OP(opcode)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(opcode, 0),				\
		   SPI_MEM_OP_ADDR(3, 0, 0),				\
		   SPI_MEM_OP_DUMMY(1, 0),				\
		   SPI_MEM_OP_DATA_IN(2, NULL, 0))

#define SPI_NOR_PP_OP(opcode)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(opcode, 0),				\
		   SPI_MEM_OP_ADDR(3, 0, 0),				\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(2, NULL, 0))

#define SPINOR_SRSTEN_OP						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_SRSTEN, 0),			\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DATA)

#define SPINOR_SRST_OP							\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_SRST, 0),			\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DATA)

/* Keep these in sync with the list in debugfs.c */
enum spi_nor_option_flags {
	SNOR_F_HAS_SR_TB	= BIT(0),
	SNOR_F_NO_OP_CHIP_ERASE	= BIT(1),
	SNOR_F_BROKEN_RESET	= BIT(2),
	SNOR_F_4B_OPCODES	= BIT(3),
	SNOR_F_HAS_4BAIT	= BIT(4),
	SNOR_F_HAS_LOCK		= BIT(5),
	SNOR_F_HAS_16BIT_SR	= BIT(6),
	SNOR_F_NO_READ_CR	= BIT(7),
	SNOR_F_HAS_SR_TB_BIT6	= BIT(8),
	SNOR_F_HAS_4BIT_BP      = BIT(9),
	SNOR_F_HAS_SR_BP3_BIT6  = BIT(10),
	SNOR_F_IO_MODE_EN_VOLATILE = BIT(11),
	SNOR_F_SOFT_RESET	= BIT(12),
	SNOR_F_SWP_IS_VOLATILE	= BIT(13),
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
 * @addr_nbytes:	number of address bytes to send.
 * @addr_mode_nbytes:	number of address bytes of current address mode. Useful
 *			when the flash operates with 4B opcodes but needs the
 *			internal address mode for opcodes that don't have a 4B
 *			opcode correspondent.
 * @rdsr_dummy:		dummy cycles needed for Read Status Register command
 *			in octal DTR mode.
 * @rdsr_addr_nbytes:	dummy address bytes needed for Read Status Register
 *			command in octal DTR mode.
 * @hwcaps:		describes the read and page program hardware
 *			capabilities.
 * @reads:		read capabilities ordered by priority: the higher index
 *                      in the array, the higher priority.
 * @page_programs:	page program capabilities ordered by priority: the
 *                      higher index in the array, the higher priority.
 * @erase_map:		the erase map parsed from the SFDP Sector Map Parameter
 *                      Table.
 * @otp:		SPI NOR OTP info.
 * @octal_dtr_enable:	enables SPI NOR octal DTR mode.
 * @quad_enable:	enables SPI NOR quad mode.
 * @set_4byte_addr_mode: puts the SPI NOR in 4 byte addressing mode.
 * @convert_addr:	converts an absolute address into something the flash
 *                      will understand. Particularly useful when pagesize is
 *                      not a power-of-2.
 * @setup:		(optional) configures the SPI NOR memory. Useful for
 *			SPI NOR flashes that have peculiarities to the SPI NOR
 *			standard e.g. different opcodes, specific address
 *			calculation, page size, etc.
 * @ready:		(optional) flashes might use a different mechanism
 *			than reading the status register to indicate they
 *			are ready for a new command
 * @locking_ops:	SPI NOR locking methods.
 */
struct spi_nor_flash_parameter {
	u64				size;
	u32				writesize;
	u32				page_size;
	u8				addr_nbytes;
	u8				addr_mode_nbytes;
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
	int (*ready)(struct spi_nor *nor);

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
 * @late_init: used to initialize flash parameters that are not declared in the
 *             JESD216 SFDP standard, or where SFDP tables not defined at all.
 *             Will replace the default_init() hook.
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
	void (*late_init)(struct spi_nor *nor);
};

/**
 * struct flash_info - SPI NOR flash_info entry.
 * @name: the name of the flash.
 * @id:             the flash's ID bytes. The first three bytes are the
 *                  JEDIC ID. JEDEC ID zero means "no ID" (mostly older chips).
 * @id_len:         the number of bytes of ID.
 * @sector_size:    the size listed here is what works with SPINOR_OP_SE, which
 *                  isn't necessarily called a "sector" by the vendor.
 * @n_sectors:      the number of sectors.
 * @page_size:      the flash's page size.
 * @addr_nbytes:    number of address bytes to send.
 *
 * @parse_sfdp:     true when flash supports SFDP tables. The false value has no
 *                  meaning. If one wants to skip the SFDP tables, one should
 *                  instead use the SPI_NOR_SKIP_SFDP sfdp_flag.
 * @flags:          flags that indicate support that is not defined by the
 *                  JESD216 standard in its SFDP tables. Flag meanings:
 *   SPI_NOR_HAS_LOCK:        flash supports lock/unlock via SR
 *   SPI_NOR_HAS_TB:          flash SR has Top/Bottom (TB) protect bit. Must be
 *                            used with SPI_NOR_HAS_LOCK.
 *   SPI_NOR_TB_SR_BIT6:      Top/Bottom (TB) is bit 6 of status register.
 *                            Must be used with SPI_NOR_HAS_TB.
 *   SPI_NOR_4BIT_BP:         flash SR has 4 bit fields (BP0-3) for block
 *                            protection.
 *   SPI_NOR_BP3_SR_BIT6:     BP3 is bit 6 of status register. Must be used with
 *                            SPI_NOR_4BIT_BP.
 *   SPI_NOR_SWP_IS_VOLATILE: flash has volatile software write protection bits.
 *                            Usually these will power-up in a write-protected
 *                            state.
 *   SPI_NOR_NO_ERASE:        no erase command needed.
 *   NO_CHIP_ERASE:           chip does not support chip erase.
 *   SPI_NOR_NO_FR:           can't do fastread.
 *
 * @no_sfdp_flags:  flags that indicate support that can be discovered via SFDP.
 *                  Used when SFDP tables are not defined in the flash. These
 *                  flags are used together with the SPI_NOR_SKIP_SFDP flag.
 *   SPI_NOR_SKIP_SFDP:       skip parsing of SFDP tables.
 *   SECT_4K:                 SPINOR_OP_BE_4K works uniformly.
 *   SPI_NOR_DUAL_READ:       flash supports Dual Read.
 *   SPI_NOR_QUAD_READ:       flash supports Quad Read.
 *   SPI_NOR_OCTAL_READ:      flash supports Octal Read.
 *   SPI_NOR_OCTAL_DTR_READ:  flash supports octal DTR Read.
 *   SPI_NOR_OCTAL_DTR_PP:    flash supports Octal DTR Page Program.
 *
 * @fixup_flags:    flags that indicate support that can be discovered via SFDP
 *                  ideally, but can not be discovered for this particular flash
 *                  because the SFDP table that indicates this support is not
 *                  defined by the flash. In case the table for this support is
 *                  defined but has wrong values, one should instead use a
 *                  post_sfdp() hook to set the SNOR_F equivalent flag.
 *
 *   SPI_NOR_4B_OPCODES:      use dedicated 4byte address op codes to support
 *                            memory size above 128Mib.
 *   SPI_NOR_IO_MODE_EN_VOLATILE: flash enables the best available I/O mode
 *                            via a volatile bit.
 * @mfr_flags:      manufacturer private flags. Used in the manufacturer fixup
 *                  hooks to differentiate support between flashes of the same
 *                  manufacturer.
 * @otp_org:        flash's OTP organization.
 * @fixups:         part specific fixup hooks.
 */
struct flash_info {
	char *name;
	u8 id[SPI_NOR_MAX_ID_LEN];
	u8 id_len;
	unsigned sector_size;
	u16 n_sectors;
	u16 page_size;
	u8 addr_nbytes;

	bool parse_sfdp;
	u16 flags;
#define SPI_NOR_HAS_LOCK		BIT(0)
#define SPI_NOR_HAS_TB			BIT(1)
#define SPI_NOR_TB_SR_BIT6		BIT(2)
#define SPI_NOR_4BIT_BP			BIT(3)
#define SPI_NOR_BP3_SR_BIT6		BIT(4)
#define SPI_NOR_SWP_IS_VOLATILE		BIT(5)
#define SPI_NOR_NO_ERASE		BIT(6)
#define NO_CHIP_ERASE			BIT(7)
#define SPI_NOR_NO_FR			BIT(8)

	u8 no_sfdp_flags;
#define SPI_NOR_SKIP_SFDP		BIT(0)
#define SECT_4K				BIT(1)
#define SPI_NOR_DUAL_READ		BIT(3)
#define SPI_NOR_QUAD_READ		BIT(4)
#define SPI_NOR_OCTAL_READ		BIT(5)
#define SPI_NOR_OCTAL_DTR_READ		BIT(6)
#define SPI_NOR_OCTAL_DTR_PP		BIT(7)

	u8 fixup_flags;
#define SPI_NOR_4B_OPCODES		BIT(0)
#define SPI_NOR_IO_MODE_EN_VOLATILE	BIT(1)

	u8 mfr_flags;

	const struct spi_nor_otp_organization otp_org;
	const struct spi_nor_fixups *fixups;
};

/* Used when the "_ext_id" is two bytes at most */
#define INFO(_jedec_id, _ext_id, _sector_size, _n_sectors)		\
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

#define INFO6(_jedec_id, _ext_id, _sector_size, _n_sectors)		\
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

#define CAT25_INFO(_sector_size, _n_sectors, _page_size, _addr_nbytes)	\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = (_page_size),				\
		.addr_nbytes = (_addr_nbytes),				\
		.flags = SPI_NOR_NO_ERASE | SPI_NOR_NO_FR,		\

#define OTP_INFO(_len, _n_regions, _base, _offset)			\
		.otp_org = {						\
			.len = (_len),					\
			.base = (_base),				\
			.offset = (_offset),				\
			.n_regions = (_n_regions),			\
		},

#define PARSE_SFDP							\
	.parse_sfdp = true,						\

#define FLAGS(_flags)							\
		.flags = (_flags),					\

#define NO_SFDP_FLAGS(_no_sfdp_flags)					\
		.no_sfdp_flags = (_no_sfdp_flags),			\

#define FIXUP_FLAGS(_fixup_flags)					\
		.fixup_flags = (_fixup_flags),				\

#define MFR_FLAGS(_mfr_flags)						\
		.mfr_flags = (_mfr_flags),				\

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
int spi_nor_wait_till_ready(struct spi_nor *nor);
int spi_nor_global_block_unlock(struct spi_nor *nor);
int spi_nor_lock_and_prep(struct spi_nor *nor);
void spi_nor_unlock_and_unprep(struct spi_nor *nor);
int spi_nor_sr1_bit6_quad_enable(struct spi_nor *nor);
int spi_nor_sr2_bit1_quad_enable(struct spi_nor *nor);
int spi_nor_sr2_bit7_quad_enable(struct spi_nor *nor);
int spi_nor_read_id(struct spi_nor *nor, u8 naddr, u8 ndummy, u8 *id,
		    enum spi_nor_protocol reg_proto);
int spi_nor_read_sr(struct spi_nor *nor, u8 *sr);
int spi_nor_sr_ready(struct spi_nor *nor);
int spi_nor_read_cr(struct spi_nor *nor, u8 *cr);
int spi_nor_write_sr(struct spi_nor *nor, const u8 *sr, size_t len);
int spi_nor_write_sr_and_check(struct spi_nor *nor, u8 sr1);
int spi_nor_write_16bit_cr_and_check(struct spi_nor *nor, u8 cr);

ssize_t spi_nor_read_data(struct spi_nor *nor, loff_t from, size_t len,
			  u8 *buf);
ssize_t spi_nor_write_data(struct spi_nor *nor, loff_t to, size_t len,
			   const u8 *buf);
int spi_nor_read_any_reg(struct spi_nor *nor, struct spi_mem_op *op,
			 enum spi_nor_protocol proto);
int spi_nor_write_any_volatile_reg(struct spi_nor *nor, struct spi_mem_op *op,
				   enum spi_nor_protocol proto);
int spi_nor_erase_sector(struct spi_nor *nor, u32 addr);

int spi_nor_otp_read_secr(struct spi_nor *nor, loff_t addr, size_t len, u8 *buf);
int spi_nor_otp_write_secr(struct spi_nor *nor, loff_t addr, size_t len,
			   const u8 *buf);
int spi_nor_otp_erase_secr(struct spi_nor *nor, loff_t addr);
int spi_nor_otp_lock_sr2(struct spi_nor *nor, unsigned int region);
int spi_nor_otp_is_locked_sr2(struct spi_nor *nor, unsigned int region);

int spi_nor_hwcaps_read2cmd(u32 hwcaps);
int spi_nor_hwcaps_pp2cmd(u32 hwcaps);
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
void spi_nor_mask_erase_type(struct spi_nor_erase_type *erase);
struct spi_nor_erase_region *
spi_nor_region_next(struct spi_nor_erase_region *region);
void spi_nor_init_uniform_erase_map(struct spi_nor_erase_map *map,
				    u8 erase_mask, u64 flash_size);

int spi_nor_post_bfpt_fixups(struct spi_nor *nor,
			     const struct sfdp_parameter_header *bfpt_header,
			     const struct sfdp_bfpt *bfpt);

void spi_nor_init_default_locking_ops(struct spi_nor *nor);
void spi_nor_try_unlock_all(struct spi_nor *nor);
void spi_nor_set_mtd_locking_ops(struct spi_nor *nor);
void spi_nor_set_mtd_otp_ops(struct spi_nor *nor);

int spi_nor_controller_ops_read_reg(struct spi_nor *nor, u8 opcode,
				    u8 *buf, size_t len);
int spi_nor_controller_ops_write_reg(struct spi_nor *nor, u8 opcode,
				     const u8 *buf, size_t len);

static inline struct spi_nor *mtd_to_spi_nor(struct mtd_info *mtd)
{
	return container_of(mtd, struct spi_nor, mtd);
}

#ifdef CONFIG_DEBUG_FS
void spi_nor_debugfs_register(struct spi_nor *nor);
void spi_nor_debugfs_shutdown(void);
#else
static inline void spi_nor_debugfs_register(struct spi_nor *nor) {}
static inline void spi_nor_debugfs_shutdown(void) {}
#endif

#endif /* __LINUX_MTD_SPI_NOR_INTERNAL_H */
