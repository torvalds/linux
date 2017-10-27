#include <linux/mtd/mtd.h>
#include <linux/sched/signal.h>

static inline int mtdtest_relax(void)
{
	cond_resched();
	if (signal_pending(current)) {
		pr_info("aborting test due to pending signal!\n");
		return -EINTR;
	}

	return 0;
}

int mtdtest_erase_eraseblock(struct mtd_info *mtd, unsigned int ebnum);
int mtdtest_scan_for_bad_eraseblocks(struct mtd_info *mtd, unsigned char *bbt,
					unsigned int eb, int ebcnt);
int mtdtest_erase_good_eraseblocks(struct mtd_info *mtd, unsigned char *bbt,
				unsigned int eb, int ebcnt);

int mtdtest_read(struct mtd_info *mtd, loff_t addr, size_t size, void *buf);
int mtdtest_write(struct mtd_info *mtd, loff_t addr, size_t size,
		const void *buf);
