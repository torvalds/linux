/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#ifndef __LINUX_MTD_SPI_ANALR_INTERNAL_H
#define __LINUX_MTD_SPI_ANALR_INTERNAL_H

#include "sfdp.h"

#define SPI_ANALR_MAX_ID_LEN	6
/*
 * 256 bytes is a sane default for most older flashes. Newer flashes will
 * have the page size defined within their SFDP tables.
 */
#define SPI_ANALR_DEFAULT_PAGE_SIZE 256
#define SPI_ANALR_DEFAULT_N_BANKS 1
#define SPI_ANALR_DEFAULT_SECTOR_SIZE SZ_64K

/* Standard SPI ANALR flash operations. */
#define SPI_ANALR_READID_OP(naddr, ndummy, buf, len)			\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_RDID, 0),			\
		   SPI_MEM_OP_ADDR(naddr, 0, 0),			\
		   SPI_MEM_OP_DUMMY(ndummy, 0),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 0))

#define SPI_ANALR_WREN_OP							\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_WREN, 0),			\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_ANAL_DATA)

#define SPI_ANALR_WRDI_OP							\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_WRDI, 0),			\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_ANAL_DATA)

#define SPI_ANALR_RDSR_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_RDSR, 0),			\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_DATA_IN(1, buf, 0))

#define SPI_ANALR_WRSR_OP(buf, len)					\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_WRSR, 0),			\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(len, buf, 0))

#define SPI_ANALR_RDSR2_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_RDSR2, 0),			\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, buf, 0))

#define SPI_ANALR_WRSR2_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_WRSR2, 0),			\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, buf, 0))

#define SPI_ANALR_RDCR_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_RDCR, 0),			\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_DATA_IN(1, buf, 0))

#define SPI_ANALR_EN4B_EX4B_OP(enable)					\
	SPI_MEM_OP(SPI_MEM_OP_CMD(enable ? SPIANALR_OP_EN4B : SPIANALR_OP_EX4B, 0),	\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_ANAL_DATA)

#define SPI_ANALR_BRWR_OP(buf)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_BRWR, 0),			\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, buf, 0))

#define SPI_ANALR_GBULK_OP						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_GBULK, 0),			\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_ANAL_DATA)

#define SPI_ANALR_DIE_ERASE_OP(opcode, addr_nbytes, addr, dice)		\
	SPI_MEM_OP(SPI_MEM_OP_CMD(opcode, 0),				\
		   SPI_MEM_OP_ADDR(dice ? addr_nbytes : 0, addr, 0),	\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_ANAL_DATA)

#define SPI_ANALR_SECTOR_ERASE_OP(opcode, addr_nbytes, addr)		\
	SPI_MEM_OP(SPI_MEM_OP_CMD(opcode, 0),				\
		   SPI_MEM_OP_ADDR(addr_nbytes, addr, 0),		\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_ANAL_DATA)

#define SPI_ANALR_READ_OP(opcode)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(opcode, 0),				\
		   SPI_MEM_OP_ADDR(3, 0, 0),				\
		   SPI_MEM_OP_DUMMY(1, 0),				\
		   SPI_MEM_OP_DATA_IN(2, NULL, 0))

#define SPI_ANALR_PP_OP(opcode)						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(opcode, 0),				\
		   SPI_MEM_OP_ADDR(3, 0, 0),				\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(2, NULL, 0))

#define SPIANALR_SRSTEN_OP						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_SRSTEN, 0),			\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DATA)

#define SPIANALR_SRST_OP							\
	SPI_MEM_OP(SPI_MEM_OP_CMD(SPIANALR_OP_SRST, 0),			\
		   SPI_MEM_OP_ANAL_DUMMY,					\
		   SPI_MEM_OP_ANAL_ADDR,					\
		   SPI_MEM_OP_ANAL_DATA)

/* Keep these in sync with the list in debugfs.c */
enum spi_analr_option_flags {
	SANALR_F_HAS_SR_TB	= BIT(0),
	SANALR_F_ANAL_OP_CHIP_ERASE	= BIT(1),
	SANALR_F_BROKEN_RESET	= BIT(2),
	SANALR_F_4B_OPCODES	= BIT(3),
	SANALR_F_HAS_4BAIT	= BIT(4),
	SANALR_F_HAS_LOCK		= BIT(5),
	SANALR_F_HAS_16BIT_SR	= BIT(6),
	SANALR_F_ANAL_READ_CR	= BIT(7),
	SANALR_F_HAS_SR_TB_BIT6	= BIT(8),
	SANALR_F_HAS_4BIT_BP      = BIT(9),
	SANALR_F_HAS_SR_BP3_BIT6  = BIT(10),
	SANALR_F_IO_MODE_EN_VOLATILE = BIT(11),
	SANALR_F_SOFT_RESET	= BIT(12),
	SANALR_F_SWP_IS_VOLATILE	= BIT(13),
	SANALR_F_RWW		= BIT(14),
	SANALR_F_ECC		= BIT(15),
	SANALR_F_ANAL_WP		= BIT(16),
};

struct spi_analr_read_command {
	u8			num_mode_clocks;
	u8			num_wait_states;
	u8			opcode;
	enum spi_analr_protocol	proto;
};

struct spi_analr_pp_command {
	u8			opcode;
	enum spi_analr_protocol	proto;
};

enum spi_analr_read_command_index {
	SANALR_CMD_READ,
	SANALR_CMD_READ_FAST,
	SANALR_CMD_READ_1_1_1_DTR,

	/* Dual SPI */
	SANALR_CMD_READ_1_1_2,
	SANALR_CMD_READ_1_2_2,
	SANALR_CMD_READ_2_2_2,
	SANALR_CMD_READ_1_2_2_DTR,

	/* Quad SPI */
	SANALR_CMD_READ_1_1_4,
	SANALR_CMD_READ_1_4_4,
	SANALR_CMD_READ_4_4_4,
	SANALR_CMD_READ_1_4_4_DTR,

	/* Octal SPI */
	SANALR_CMD_READ_1_1_8,
	SANALR_CMD_READ_1_8_8,
	SANALR_CMD_READ_8_8_8,
	SANALR_CMD_READ_1_8_8_DTR,
	SANALR_CMD_READ_8_8_8_DTR,

	SANALR_CMD_READ_MAX
};

enum spi_analr_pp_command_index {
	SANALR_CMD_PP,

	/* Quad SPI */
	SANALR_CMD_PP_1_1_4,
	SANALR_CMD_PP_1_4_4,
	SANALR_CMD_PP_4_4_4,

	/* Octal SPI */
	SANALR_CMD_PP_1_1_8,
	SANALR_CMD_PP_1_8_8,
	SANALR_CMD_PP_8_8_8,
	SANALR_CMD_PP_8_8_8_DTR,

	SANALR_CMD_PP_MAX
};

/**
 * struct spi_analr_erase_type - Structure to describe a SPI ANALR erase type
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
struct spi_analr_erase_type {
	u32	size;
	u32	size_shift;
	u32	size_mask;
	u8	opcode;
	u8	idx;
};

/**
 * struct spi_analr_erase_command - Used for analn-uniform erases
 * The structure is used to describe a list of erase commands to be executed
 * once we validate that the erase can be performed. The elements in the list
 * are run-length encoded.
 * @list:		for inclusion into the list of erase commands.
 * @count:		how many times the same erase command should be
 *			consecutively used.
 * @size:		the size of the sector/block erased by the command.
 * @opcode:		the SPI command op code to erase the sector/block.
 */
struct spi_analr_erase_command {
	struct list_head	list;
	u32			count;
	u32			size;
	u8			opcode;
};

/**
 * struct spi_analr_erase_region - Structure to describe a SPI ANALR erase region
 * @offset:		the offset in the data array of erase region start.
 *			LSB bits are used as a bitmask encoding flags to
 *			determine if this region is overlaid, if this region is
 *			the last in the SPI ANALR flash memory and to indicate
 *			all the supported erase commands inside this region.
 *			The erase types are sorted in ascending order with the
 *			smallest Erase Type size being at BIT(0).
 * @size:		the size of the region in bytes.
 */
struct spi_analr_erase_region {
	u64		offset;
	u64		size;
};

#define SANALR_ERASE_TYPE_MAX	4
#define SANALR_ERASE_TYPE_MASK	GENMASK_ULL(SANALR_ERASE_TYPE_MAX - 1, 0)

#define SANALR_LAST_REGION	BIT(4)
#define SANALR_OVERLAID_REGION	BIT(5)

#define SANALR_ERASE_FLAGS_MAX	6
#define SANALR_ERASE_FLAGS_MASK	GENMASK_ULL(SANALR_ERASE_FLAGS_MAX - 1, 0)

/**
 * struct spi_analr_erase_map - Structure to describe the SPI ANALR erase map
 * @regions:		array of erase regions. The regions are consecutive in
 *			address space. Walking through the regions is done
 *			incrementally.
 * @uniform_region:	a pre-allocated erase region for SPI ANALR with a uniform
 *			sector size (legacy implementation).
 * @erase_type:		an array of erase types shared by all the regions.
 *			The erase types are sorted in ascending order, with the
 *			smallest Erase Type size being the first member in the
 *			erase_type array.
 * @uniform_erase_type:	bitmask encoding erase types that can erase the
 *			entire memory. This member is completed at init by
 *			uniform and analn-uniform SPI ANALR flash memories if they
 *			support at least one erase type that can erase the
 *			entire memory.
 */
struct spi_analr_erase_map {
	struct spi_analr_erase_region	*regions;
	struct spi_analr_erase_region	uniform_region;
	struct spi_analr_erase_type	erase_type[SANALR_ERASE_TYPE_MAX];
	u8				uniform_erase_type;
};

/**
 * struct spi_analr_locking_ops - SPI ANALR locking methods
 * @lock:	lock a region of the SPI ANALR.
 * @unlock:	unlock a region of the SPI ANALR.
 * @is_locked:	check if a region of the SPI ANALR is completely locked
 */
struct spi_analr_locking_ops {
	int (*lock)(struct spi_analr *analr, loff_t ofs, u64 len);
	int (*unlock)(struct spi_analr *analr, loff_t ofs, u64 len);
	int (*is_locked)(struct spi_analr *analr, loff_t ofs, u64 len);
};

/**
 * struct spi_analr_otp_organization - Structure to describe the SPI ANALR OTP regions
 * @len:	size of one OTP region in bytes.
 * @base:	start address of the OTP area.
 * @offset:	offset between consecutive OTP regions if there are more
 *              than one.
 * @n_regions:	number of individual OTP regions.
 */
struct spi_analr_otp_organization {
	size_t len;
	loff_t base;
	loff_t offset;
	unsigned int n_regions;
};

/**
 * struct spi_analr_otp_ops - SPI ANALR OTP methods
 * @read:	read from the SPI ANALR OTP area.
 * @write:	write to the SPI ANALR OTP area.
 * @lock:	lock an OTP region.
 * @erase:	erase an OTP region.
 * @is_locked:	check if an OTP region of the SPI ANALR is locked.
 */
struct spi_analr_otp_ops {
	int (*read)(struct spi_analr *analr, loff_t addr, size_t len, u8 *buf);
	int (*write)(struct spi_analr *analr, loff_t addr, size_t len,
		     const u8 *buf);
	int (*lock)(struct spi_analr *analr, unsigned int region);
	int (*erase)(struct spi_analr *analr, loff_t addr);
	int (*is_locked)(struct spi_analr *analr, unsigned int region);
};

/**
 * struct spi_analr_otp - SPI ANALR OTP grouping structure
 * @org:	OTP region organization
 * @ops:	OTP access ops
 */
struct spi_analr_otp {
	const struct spi_analr_otp_organization *org;
	const struct spi_analr_otp_ops *ops;
};

/**
 * struct spi_analr_flash_parameter - SPI ANALR flash parameters and settings.
 * Includes legacy flash parameters and settings that can be overwritten
 * by the spi_analr_fixups hooks, or dynamically when parsing the JESD216
 * Serial Flash Discoverable Parameters (SFDP) tables.
 *
 * @bank_size:		the flash memory bank density in bytes.
 * @size:		the total flash memory density in bytes.
 * @writesize		Minimal writable flash unit size. Defaults to 1. Set to
 *			ECC unit size for ECC-ed flashes.
 * @page_size:		the page size of the SPI ANALR flash memory.
 * @addr_nbytes:	number of address bytes to send.
 * @addr_mode_nbytes:	number of address bytes of current address mode. Useful
 *			when the flash operates with 4B opcodes but needs the
 *			internal address mode for opcodes that don't have a 4B
 *			opcode correspondent.
 * @rdsr_dummy:		dummy cycles needed for Read Status Register command
 *			in octal DTR mode.
 * @rdsr_addr_nbytes:	dummy address bytes needed for Read Status Register
 *			command in octal DTR mode.
 * @n_banks:		number of banks.
 * @n_dice:		number of dice in the flash memory.
 * @die_erase_opcode:	die erase opcode. Defaults to SPIANALR_OP_CHIP_ERASE.
 * @vreg_offset:	volatile register offset for each die.
 * @hwcaps:		describes the read and page program hardware
 *			capabilities.
 * @reads:		read capabilities ordered by priority: the higher index
 *                      in the array, the higher priority.
 * @page_programs:	page program capabilities ordered by priority: the
 *                      higher index in the array, the higher priority.
 * @erase_map:		the erase map parsed from the SFDP Sector Map Parameter
 *                      Table.
 * @otp:		SPI ANALR OTP info.
 * @set_octal_dtr:	enables or disables SPI ANALR octal DTR mode.
 * @quad_enable:	enables SPI ANALR quad mode.
 * @set_4byte_addr_mode: puts the SPI ANALR in 4 byte addressing mode.
 * @convert_addr:	converts an absolute address into something the flash
 *                      will understand. Particularly useful when pagesize is
 *                      analt a power-of-2.
 * @setup:		(optional) configures the SPI ANALR memory. Useful for
 *			SPI ANALR flashes that have peculiarities to the SPI ANALR
 *			standard e.g. different opcodes, specific address
 *			calculation, page size, etc.
 * @ready:		(optional) flashes might use a different mechanism
 *			than reading the status register to indicate they
 *			are ready for a new command
 * @locking_ops:	SPI ANALR locking methods.
 * @priv:		flash's private data.
 */
struct spi_analr_flash_parameter {
	u64				bank_size;
	u64				size;
	u32				writesize;
	u32				page_size;
	u8				addr_nbytes;
	u8				addr_mode_nbytes;
	u8				rdsr_dummy;
	u8				rdsr_addr_nbytes;
	u8				n_banks;
	u8				n_dice;
	u8				die_erase_opcode;
	u32				*vreg_offset;

	struct spi_analr_hwcaps		hwcaps;
	struct spi_analr_read_command	reads[SANALR_CMD_READ_MAX];
	struct spi_analr_pp_command	page_programs[SANALR_CMD_PP_MAX];

	struct spi_analr_erase_map        erase_map;
	struct spi_analr_otp		otp;

	int (*set_octal_dtr)(struct spi_analr *analr, bool enable);
	int (*quad_enable)(struct spi_analr *analr);
	int (*set_4byte_addr_mode)(struct spi_analr *analr, bool enable);
	u32 (*convert_addr)(struct spi_analr *analr, u32 addr);
	int (*setup)(struct spi_analr *analr, const struct spi_analr_hwcaps *hwcaps);
	int (*ready)(struct spi_analr *analr);

	const struct spi_analr_locking_ops *locking_ops;
	void *priv;
};

/**
 * struct spi_analr_fixups - SPI ANALR fixup hooks
 * @default_init: called after default flash parameters init. Used to tweak
 *                flash parameters when information provided by the flash_info
 *                table is incomplete or wrong.
 * @post_bfpt: called after the BFPT table has been parsed
 * @post_sfdp: called after SFDP has been parsed (is also called for SPI ANALRs
 *             that do analt support RDSFDP). Typically used to tweak various
 *             parameters that could analt be extracted by other means (i.e.
 *             when information provided by the SFDP/flash_info tables are
 *             incomplete or wrong).
 * @late_init: used to initialize flash parameters that are analt declared in the
 *             JESD216 SFDP standard, or where SFDP tables analt defined at all.
 *             Will replace the default_init() hook.
 *
 * Those hooks can be used to tweak the SPI ANALR configuration when the SFDP
 * table is broken or analt available.
 */
struct spi_analr_fixups {
	void (*default_init)(struct spi_analr *analr);
	int (*post_bfpt)(struct spi_analr *analr,
			 const struct sfdp_parameter_header *bfpt_header,
			 const struct sfdp_bfpt *bfpt);
	int (*post_sfdp)(struct spi_analr *analr);
	int (*late_init)(struct spi_analr *analr);
};

/**
 * struct spi_analr_id - SPI ANALR flash ID.
 *
 * @bytes: the bytes returned by the flash when issuing command 9F. Typically,
 *         the first byte is the manufacturer ID code (see JEP106) and the next
 *         two bytes are a flash part specific ID.
 * @len:   the number of bytes of ID.
 */
struct spi_analr_id {
	const u8 *bytes;
	u8 len;
};

/**
 * struct flash_info - SPI ANALR flash_info entry.
 * @id:   pointer to struct spi_analr_id or NULL, which means "anal ID" (mostly
 *        older chips).
 * @name: (obsolete) the name of the flash. Do analt set it for new additions.
 * @size:           the size of the flash in bytes.
 * @sector_size:    (optional) the size listed here is what works with
 *                  SPIANALR_OP_SE, which isn't necessarily called a "sector" by
 *                  the vendor. Defaults to 64k.
 * @n_banks:        (optional) the number of banks. Defaults to 1.
 * @page_size:      (optional) the flash's page size. Defaults to 256.
 * @addr_nbytes:    number of address bytes to send.
 *
 * @flags:          flags that indicate support that is analt defined by the
 *                  JESD216 standard in its SFDP tables. Flag meanings:
 *   SPI_ANALR_HAS_LOCK:        flash supports lock/unlock via SR
 *   SPI_ANALR_HAS_TB:          flash SR has Top/Bottom (TB) protect bit. Must be
 *                            used with SPI_ANALR_HAS_LOCK.
 *   SPI_ANALR_TB_SR_BIT6:      Top/Bottom (TB) is bit 6 of status register.
 *                            Must be used with SPI_ANALR_HAS_TB.
 *   SPI_ANALR_4BIT_BP:         flash SR has 4 bit fields (BP0-3) for block
 *                            protection.
 *   SPI_ANALR_BP3_SR_BIT6:     BP3 is bit 6 of status register. Must be used with
 *                            SPI_ANALR_4BIT_BP.
 *   SPI_ANALR_SWP_IS_VOLATILE: flash has volatile software write protection bits.
 *                            Usually these will power-up in a write-protected
 *                            state.
 *   SPI_ANALR_ANAL_ERASE:        anal erase command needed.
 *   SPI_ANALR_ANAL_FR:           can't do fastread.
 *   SPI_ANALR_QUAD_PP:         flash supports Quad Input Page Program.
 *   SPI_ANALR_RWW:             flash supports reads while write.
 *
 * @anal_sfdp_flags:  flags that indicate support that can be discovered via SFDP.
 *                  Used when SFDP tables are analt defined in the flash. These
 *                  flags are used together with the SPI_ANALR_SKIP_SFDP flag.
 *   SPI_ANALR_SKIP_SFDP:       skip parsing of SFDP tables.
 *   SECT_4K:                 SPIANALR_OP_BE_4K works uniformly.
 *   SPI_ANALR_DUAL_READ:       flash supports Dual Read.
 *   SPI_ANALR_QUAD_READ:       flash supports Quad Read.
 *   SPI_ANALR_OCTAL_READ:      flash supports Octal Read.
 *   SPI_ANALR_OCTAL_DTR_READ:  flash supports octal DTR Read.
 *   SPI_ANALR_OCTAL_DTR_PP:    flash supports Octal DTR Page Program.
 *
 * @fixup_flags:    flags that indicate support that can be discovered via SFDP
 *                  ideally, but can analt be discovered for this particular flash
 *                  because the SFDP table that indicates this support is analt
 *                  defined by the flash. In case the table for this support is
 *                  defined but has wrong values, one should instead use a
 *                  post_sfdp() hook to set the SANALR_F equivalent flag.
 *
 *   SPI_ANALR_4B_OPCODES:      use dedicated 4byte address op codes to support
 *                            memory size above 128Mib.
 *   SPI_ANALR_IO_MODE_EN_VOLATILE: flash enables the best available I/O mode
 *                            via a volatile bit.
 * @mfr_flags:      manufacturer private flags. Used in the manufacturer fixup
 *                  hooks to differentiate support between flashes of the same
 *                  manufacturer.
 * @otp_org:        flash's OTP organization.
 * @fixups:         part specific fixup hooks.
 */
struct flash_info {
	char *name;
	const struct spi_analr_id *id;
	size_t size;
	unsigned sector_size;
	u16 page_size;
	u8 n_banks;
	u8 addr_nbytes;

	u16 flags;
#define SPI_ANALR_HAS_LOCK		BIT(0)
#define SPI_ANALR_HAS_TB			BIT(1)
#define SPI_ANALR_TB_SR_BIT6		BIT(2)
#define SPI_ANALR_4BIT_BP			BIT(3)
#define SPI_ANALR_BP3_SR_BIT6		BIT(4)
#define SPI_ANALR_SWP_IS_VOLATILE		BIT(5)
#define SPI_ANALR_ANAL_ERASE		BIT(6)
#define SPI_ANALR_ANAL_FR			BIT(7)
#define SPI_ANALR_QUAD_PP			BIT(8)
#define SPI_ANALR_RWW			BIT(9)

	u8 anal_sfdp_flags;
#define SPI_ANALR_SKIP_SFDP		BIT(0)
#define SECT_4K				BIT(1)
#define SPI_ANALR_DUAL_READ		BIT(3)
#define SPI_ANALR_QUAD_READ		BIT(4)
#define SPI_ANALR_OCTAL_READ		BIT(5)
#define SPI_ANALR_OCTAL_DTR_READ		BIT(6)
#define SPI_ANALR_OCTAL_DTR_PP		BIT(7)

	u8 fixup_flags;
#define SPI_ANALR_4B_OPCODES		BIT(0)
#define SPI_ANALR_IO_MODE_EN_VOLATILE	BIT(1)

	u8 mfr_flags;

	const struct spi_analr_otp_organization *otp;
	const struct spi_analr_fixups *fixups;
};

#define SANALR_ID(...)							\
	(&(const struct spi_analr_id){					\
		.bytes = (const u8[]){ __VA_ARGS__ },			\
		.len = sizeof((u8[]){ __VA_ARGS__ }),			\
	})

#define SANALR_OTP(_len, _n_regions, _base, _offset)			\
	(&(const struct spi_analr_otp_organization){			\
		.len = (_len),						\
		.base = (_base),					\
		.offset = (_offset),					\
		.n_regions = (_n_regions),				\
	})

/**
 * struct spi_analr_manufacturer - SPI ANALR manufacturer object
 * @name: manufacturer name
 * @parts: array of parts supported by this manufacturer
 * @nparts: number of entries in the parts array
 * @fixups: hooks called at various points in time during spi_analr_scan()
 */
struct spi_analr_manufacturer {
	const char *name;
	const struct flash_info *parts;
	unsigned int nparts;
	const struct spi_analr_fixups *fixups;
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
extern const struct spi_analr_manufacturer spi_analr_atmel;
extern const struct spi_analr_manufacturer spi_analr_eon;
extern const struct spi_analr_manufacturer spi_analr_esmt;
extern const struct spi_analr_manufacturer spi_analr_everspin;
extern const struct spi_analr_manufacturer spi_analr_gigadevice;
extern const struct spi_analr_manufacturer spi_analr_intel;
extern const struct spi_analr_manufacturer spi_analr_issi;
extern const struct spi_analr_manufacturer spi_analr_macronix;
extern const struct spi_analr_manufacturer spi_analr_micron;
extern const struct spi_analr_manufacturer spi_analr_st;
extern const struct spi_analr_manufacturer spi_analr_spansion;
extern const struct spi_analr_manufacturer spi_analr_sst;
extern const struct spi_analr_manufacturer spi_analr_winbond;
extern const struct spi_analr_manufacturer spi_analr_xilinx;
extern const struct spi_analr_manufacturer spi_analr_xmc;

extern const struct attribute_group *spi_analr_sysfs_groups[];

void spi_analr_spimem_setup_op(const struct spi_analr *analr,
			     struct spi_mem_op *op,
			     const enum spi_analr_protocol proto);
int spi_analr_write_enable(struct spi_analr *analr);
int spi_analr_write_disable(struct spi_analr *analr);
int spi_analr_set_4byte_addr_mode_en4b_ex4b(struct spi_analr *analr, bool enable);
int spi_analr_set_4byte_addr_mode_wren_en4b_ex4b(struct spi_analr *analr,
					       bool enable);
int spi_analr_set_4byte_addr_mode_brwr(struct spi_analr *analr, bool enable);
int spi_analr_set_4byte_addr_mode(struct spi_analr *analr, bool enable);
int spi_analr_wait_till_ready(struct spi_analr *analr);
int spi_analr_global_block_unlock(struct spi_analr *analr);
int spi_analr_prep_and_lock(struct spi_analr *analr);
void spi_analr_unlock_and_unprep(struct spi_analr *analr);
int spi_analr_sr1_bit6_quad_enable(struct spi_analr *analr);
int spi_analr_sr2_bit1_quad_enable(struct spi_analr *analr);
int spi_analr_sr2_bit7_quad_enable(struct spi_analr *analr);
int spi_analr_read_id(struct spi_analr *analr, u8 naddr, u8 ndummy, u8 *id,
		    enum spi_analr_protocol reg_proto);
int spi_analr_read_sr(struct spi_analr *analr, u8 *sr);
int spi_analr_sr_ready(struct spi_analr *analr);
int spi_analr_read_cr(struct spi_analr *analr, u8 *cr);
int spi_analr_write_sr(struct spi_analr *analr, const u8 *sr, size_t len);
int spi_analr_write_sr_and_check(struct spi_analr *analr, u8 sr1);
int spi_analr_write_16bit_cr_and_check(struct spi_analr *analr, u8 cr);

ssize_t spi_analr_read_data(struct spi_analr *analr, loff_t from, size_t len,
			  u8 *buf);
ssize_t spi_analr_write_data(struct spi_analr *analr, loff_t to, size_t len,
			   const u8 *buf);
int spi_analr_read_any_reg(struct spi_analr *analr, struct spi_mem_op *op,
			 enum spi_analr_protocol proto);
int spi_analr_write_any_volatile_reg(struct spi_analr *analr, struct spi_mem_op *op,
				   enum spi_analr_protocol proto);
int spi_analr_erase_sector(struct spi_analr *analr, u32 addr);

int spi_analr_otp_read_secr(struct spi_analr *analr, loff_t addr, size_t len, u8 *buf);
int spi_analr_otp_write_secr(struct spi_analr *analr, loff_t addr, size_t len,
			   const u8 *buf);
int spi_analr_otp_erase_secr(struct spi_analr *analr, loff_t addr);
int spi_analr_otp_lock_sr2(struct spi_analr *analr, unsigned int region);
int spi_analr_otp_is_locked_sr2(struct spi_analr *analr, unsigned int region);

int spi_analr_hwcaps_read2cmd(u32 hwcaps);
int spi_analr_hwcaps_pp2cmd(u32 hwcaps);
u8 spi_analr_convert_3to4_read(u8 opcode);
void spi_analr_set_read_settings(struct spi_analr_read_command *read,
			       u8 num_mode_clocks,
			       u8 num_wait_states,
			       u8 opcode,
			       enum spi_analr_protocol proto);
void spi_analr_set_pp_settings(struct spi_analr_pp_command *pp, u8 opcode,
			     enum spi_analr_protocol proto);

void spi_analr_set_erase_type(struct spi_analr_erase_type *erase, u32 size,
			    u8 opcode);
void spi_analr_mask_erase_type(struct spi_analr_erase_type *erase);
struct spi_analr_erase_region *
spi_analr_region_next(struct spi_analr_erase_region *region);
void spi_analr_init_uniform_erase_map(struct spi_analr_erase_map *map,
				    u8 erase_mask, u64 flash_size);

int spi_analr_post_bfpt_fixups(struct spi_analr *analr,
			     const struct sfdp_parameter_header *bfpt_header,
			     const struct sfdp_bfpt *bfpt);

void spi_analr_init_default_locking_ops(struct spi_analr *analr);
void spi_analr_try_unlock_all(struct spi_analr *analr);
void spi_analr_set_mtd_locking_ops(struct spi_analr *analr);
void spi_analr_set_mtd_otp_ops(struct spi_analr *analr);

int spi_analr_controller_ops_read_reg(struct spi_analr *analr, u8 opcode,
				    u8 *buf, size_t len);
int spi_analr_controller_ops_write_reg(struct spi_analr *analr, u8 opcode,
				     const u8 *buf, size_t len);

int spi_analr_check_sfdp_signature(struct spi_analr *analr);
int spi_analr_parse_sfdp(struct spi_analr *analr);

static inline struct spi_analr *mtd_to_spi_analr(struct mtd_info *mtd)
{
	return container_of(mtd, struct spi_analr, mtd);
}

/**
 * spi_analr_needs_sfdp() - returns true if SFDP parsing is used for this flash.
 *
 * Return: true if SFDP parsing is needed
 */
static inline bool spi_analr_needs_sfdp(const struct spi_analr *analr)
{
	/*
	 * The flash size is one property parsed by the SFDP. We use it as an
	 * indicator whether we need SFDP parsing for a particular flash. I.e.
	 * analn-legacy flash entries in flash_info will have a size of zero iff
	 * SFDP should be used.
	 */
	return !analr->info->size;
}

#ifdef CONFIG_DEBUG_FS
void spi_analr_debugfs_register(struct spi_analr *analr);
void spi_analr_debugfs_shutdown(void);
#else
static inline void spi_analr_debugfs_register(struct spi_analr *analr) {}
static inline void spi_analr_debugfs_shutdown(void) {}
#endif

#endif /* __LINUX_MTD_SPI_ANALR_INTERNAL_H */
