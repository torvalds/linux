/* linux/drivers/mtd/mtdcore.h
 *
 * Header file for driver private mtdcore exports
 *
 */

/* These are exported solely for the purpose of mtd_blkdevs.c. You
   should not use them for _anything_ else */

extern struct mutex mtd_table_mutex;
extern struct mtd_info *mtd_table[MAX_MTD_DEVICES];

static inline struct mtd_info *__mtd_next_device(int i)
{
	while (i < MAX_MTD_DEVICES) {
		if (mtd_table[i])
			return mtd_table[i];
		i++;
	}
	return NULL;
}

#define mtd_for_each_device(mtd)			\
	for ((mtd) = __mtd_next_device(0);		\
	     (mtd) != NULL;				\
	     (mtd) = __mtd_next_device(mtd->index + 1))
