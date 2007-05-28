/* linux/drivers/mtd/mtdcore.h
 *
 * Header file for driver private mtdcore exports
 *
 */

/* These are exported solely for the purpose of mtd_blkdevs.c. You
   should not use them for _anything_ else */

extern struct mutex mtd_table_mutex;
extern struct mtd_info *mtd_table[MAX_MTD_DEVICES];
