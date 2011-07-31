/*
 * Defines for zoom boards
 */
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#define ZOOM_NAND_CS    0

extern void __init board_nand_init(struct mtd_partition *, u8 nr_parts, u8 cs);
extern int __init zoom_debugboard_init(void);
extern void __init zoom_peripherals_init(void);
