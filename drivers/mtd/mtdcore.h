/* linux/drivers/mtd/mtdcore.h
 *
 * Header file for driver private mtdcore exports
 *
 */

/* These are exported solely for the purpose of mtd_blkdevs.c. You
   should not use them for _anything_ else */

extern struct mutex mtd_table_mutex;
extern struct mtd_info *__mtd_next_device(int i);

extern int add_mtd_device(struct mtd_info *mtd);
extern int del_mtd_device(struct mtd_info *mtd);
extern int add_mtd_partitions(struct mtd_info *, const struct mtd_partition *,
			      int);
extern int del_mtd_partitions(struct mtd_info *);

#define mtd_for_each_device(mtd)			\
	for ((mtd) = __mtd_next_device(0);		\
	     (mtd) != NULL;				\
	     (mtd) = __mtd_next_device(mtd->index + 1))
