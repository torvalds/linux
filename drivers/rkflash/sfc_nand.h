/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __SFC_NAND_H
#define __SFC_NAND_H

#define SFC_NAND_STRESS_TEST_EN		0

#define SFC_NAND_PROG_ERASE_ERROR	-2
#define SFC_NAND_HW_ERROR		-1
#define SFC_NAND_ECC_ERROR		NAND_ERROR
#define SFC_NAND_ECC_REFRESH		NAND_STS_REFRESH
#define SFC_NAND_ECC_OK			NAND_STS_OK

#define SFC_NAND_PAGE_MAX_SIZE		4224
#define SFC_NAND_SECTOR_FULL_SIZE	528

#define FEA_READ_STATUE_MASK    (0x3 << 0)
#define FEA_STATUE_MODE1        0
#define FEA_STATUE_MODE2        1
#define FEA_4BIT_READ           BIT(2)
#define FEA_4BIT_PROG           BIT(3)
#define FEA_4BYTE_ADDR          BIT(4)
#define FEA_4BYTE_ADDR_MODE	BIT(5)
#define FEA_SOFT_QOP_BIT	BIT(6)

#define MID_WINBOND             0xEF
#define MID_GIGADEV             0xC8
#define MID_MICRON              0x2C
#define MID_MACRONIX            0xC2
#define MID_SPANSION            0x01
#define MID_EON                 0x1C
#define MID_ST                  0x20

/* Command Set */
#define CMD_READ_JEDECID        (0x9F)
#define CMD_READ_DATA           (0x03)
#define CMD_READ_STATUS         (0x05)
#define CMD_WRITE_STATUS        (0x01)
#define CMD_PAGE_PROG           (0x02)
#define CMD_SECTOR_ERASE        (0x20)
#define CMD_BLK64K_ERASE        (0xD8)
#define CMD_BLK32K_ERASE        (0x52)
#define CMD_CHIP_ERASE          (0xC7)
#define CMD_WRITE_EN            (0x06)
#define CMD_WRITE_DIS           (0x04)
#define CMD_PAGE_READ           (0x13)
#define CMD_GET_FEATURE         (0x0F)
#define CMD_SET_FEATURE         (0x1F)
#define CMD_PROG_LOAD           (0x02)
#define CMD_PROG_EXEC           (0x10)
#define CMD_BLOCK_ERASE         (0xD8)
#define CMD_READ_DATA_X2        (0x3B)
#define CMD_READ_DATA_X4        (0x6B)
#define CMD_PROG_LOAD_X4        (0x32)
#define CMD_READ_STATUS2        (0x35)
#define CMD_READ_STATUS3        (0x15)
#define CMD_WRITE_STATUS2       (0x31)
#define CMD_WRITE_STATUS3       (0x11)
#define CMD_FAST_READ_X1        (0x0B)  /* X1 cmd, X1 addr, X1 data */
#define CMD_FAST_READ_X2        (0x3B)  /* X1 cmd, X1 addr, X2 data */
/* X1 cmd, X1 addr, X4 data SUPPORT GD MARCONIX WINBOND */
#define CMD_FAST_READ_X4        (0x6B)
/* X1 cmd, X1 addr, X4 data SUPPORT GD MARCONIX WINBOND */
#define CMD_FAST_4READ_X4       (0x6C)
/* X1 cmd, X4 addr, X4 data SUPPORT EON GD MARCONIX WINBOND */
#define CMD_FAST_READ_A4        (0xEB)
/* X1 cmd, X1 addr, X4 data, SUPPORT GD WINBOND */
#define CMD_PAGE_PROG_X4        (0x32)
/* X1 cmd, X4 addr, X4 data, SUPPORT MARCONIX */
#define CMD_PAGE_PROG_A4        (0x38)
#define CMD_RESET_NAND          (0xFF)

#define CMD_ENTER_4BYTE_MODE    (0xB7)
#define CMD_EXIT_4BYTE_MODE     (0xE9)
#define CMD_ENABLE_RESER	(0x66)
#define CMD_RESET_DEVICE	(0x99)

struct SFNAND_DEV {
	u32 capacity;
	u32 block_size;
	u16 page_size;
	u8 manufacturer;
	u8 mem_type;
	u8 read_lines;
	u8 prog_lines;
	u8 page_read_cmd;
	u8 page_prog_cmd;
};

struct nand_info {
	u32 id;

	u16 sec_per_page;
	u16 page_per_blk;
	u16 plane_per_die;
	u16 blk_per_plane;

	u8 page_read_cmd;
	u8 page_prog_cmd;
	u8 read_cache_cmd_1;
	u8 prog_cache_cmd_1;

	u8 read_cache_cmd_4;
	u8 prog_cache_cmd_4;
	u8 block_erase_cmd;
	u8 feature;

	u8 density;  /* (1 << density) sectors*/
	u8 max_ecc_bits;
	u8 QE_address;
	u8 QE_bits;

	u8 spare_offs_1;
	u8 spare_offs_2;
	u32 (*ecc_status)(void);
};

extern struct nand_phy_info	g_nand_phy_info;
extern struct nand_ops		g_nand_ops;

u32 sfc_nand_init(void);
void sfc_nand_deinit(void);
int sfc_nand_read_id(u8 *buf);
u32 sfc_nand_ecc_status_sp1(void);
u32 sfc_nand_ecc_status_sp3(void);
u32 sfc_nand_ecc_status_sp4(void);
u32 sfc_nand_ecc_status_sp5(void);

#endif
