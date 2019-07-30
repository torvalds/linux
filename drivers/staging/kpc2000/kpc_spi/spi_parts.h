/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __KPC_SPI_SPI_PARTS_H__
#define __KPC_SPI_SPI_PARTS_H__

static struct mtd_partition p2kr0_spi0_parts[] = {
    { .name = "SLOT_0",    .size = 7798784,          .offset = 0,                 },
    { .name = "SLOT_1",    .size = 7798784,          .offset = MTDPART_OFS_NXTBLK },
    { .name = "SLOT_2",    .size = 7798784,          .offset = MTDPART_OFS_NXTBLK },
    { .name = "SLOT_3",    .size = 7798784,          .offset = MTDPART_OFS_NXTBLK },
    { .name = "CS0_EXTRA", .size = MTDPART_SIZ_FULL, .offset = MTDPART_OFS_NXTBLK }
};
static struct mtd_partition p2kr0_spi1_parts[] = {
    { .name = "SLOT_4",    .size   = 7798784,          .offset = 0,                 },
    { .name = "SLOT_5",    .size   = 7798784,          .offset = MTDPART_OFS_NXTBLK },
    { .name = "SLOT_6",    .size   = 7798784,          .offset = MTDPART_OFS_NXTBLK },
    { .name = "SLOT_7",    .size   = 7798784,          .offset = MTDPART_OFS_NXTBLK },
    { .name = "CS1_EXTRA", .size   = MTDPART_SIZ_FULL, .offset = MTDPART_OFS_NXTBLK }
};

static struct flash_platform_data p2kr0_spi0_pdata = {
    .name = "SPI0",
    .nr_parts = ARRAY_SIZE(p2kr0_spi0_parts),
    .parts = p2kr0_spi0_parts,
};
static struct flash_platform_data p2kr0_spi1_pdata = {
    .name = "SPI1",
    .nr_parts = ARRAY_SIZE(p2kr0_spi1_parts),
    .parts = p2kr0_spi1_parts,
};

static struct spi_board_info p2kr0_board_info[] = {
    {
        .modalias = "n25q256a11",
        .bus_num = 1,
        .chip_select = 0,
        .mode = SPI_MODE_0,
        .platform_data = &p2kr0_spi0_pdata
    },
    {
        .modalias = "n25q256a11",
        .bus_num = 1,
        .chip_select = 1,
        .mode = SPI_MODE_0,
        .platform_data = &p2kr0_spi1_pdata
    },
};

#endif
