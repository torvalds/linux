/*
 * debugfs ops for the L1 cache
 *
 *  Copyright (C) 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <asm/processor.h>
#include <linux/uaccess.h>
#include <asm/cache.h>
#include <asm/io.h>

enum cache_type {
	CACHE_TYPE_ICACHE,
	CACHE_TYPE_DCACHE,
	CACHE_TYPE_UNIFIED,
};

static int cache_seq_show(struct seq_file *file, void *iter)
{
	unsigned int cache_type = (unsigned int)file->private;
	struct cache_info *cache;
	unsigned int waysize, way;
	unsigned long ccr;
	unsigned long addrstart = 0;

	/*
	 * Go uncached immediately so we don't skew the results any
	 * more than we already are..
	 */
	jump_to_uncached();

	ccr = __raw_readl(SH_CCR);
	if ((ccr & CCR_CACHE_ENABLE) == 0) {
		back_to_cached();

		seq_printf(file, "disabled\n");
		return 0;
	}

	if (cache_type == CACHE_TYPE_DCACHE) {
		addrstart = CACHE_OC_ADDRESS_ARRAY;
		cache = &current_cpu_data.dcache;
	} else {
		addrstart = CACHE_IC_ADDRESS_ARRAY;
		cache = &current_cpu_data.icache;
	}

	waysize = cache->sets;

	/*
	 * If the OC is already in RAM mode, we only have
	 * half of the entries to consider..
	 */
	if ((ccr & CCR_CACHE_ORA) && cache_type == CACHE_TYPE_DCACHE)
		waysize >>= 1;

	waysize <<= cache->entry_shift;

	for (way = 0; way < cache->ways; way++) {
		unsigned long addr;
		unsigned int line;

		seq_printf(file, "-----------------------------------------\n");
		seq_printf(file, "Way %d\n", way);
		seq_printf(file, "-----------------------------------------\n");

		for (addr = addrstart, line = 0;
		     addr < addrstart + waysize;
		     addr += cache->linesz, line++) {
			unsigned long data = __raw_readl(addr);

			/* Check the V bit, ignore invalid cachelines */
			if ((data & 1) == 0)
				continue;

			/* U: Dirty, cache tag is 10 bits up */
			seq_printf(file, "%3d: %c 0x%lx\n",
				   line, data & 2 ? 'U' : ' ',
				   data & 0x1ffffc00);
		}

		addrstart += cache->way_incr;
	}

	back_to_cached();

	return 0;
}

static int cache_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, cache_seq_show, inode->i_private);
}

static const struct file_operations cache_debugfs_fops = {
	.owner		= THIS_MODULE,
	.open		= cache_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init cache_debugfs_init(void)
{
	struct dentry *dcache_dentry, *icache_dentry;

	dcache_dentry = debugfs_create_file("dcache", S_IRUSR, arch_debugfs_dir,
					    (unsigned int *)CACHE_TYPE_DCACHE,
					    &cache_debugfs_fops);
	if (!dcache_dentry)
		return -ENOMEM;

	icache_dentry = debugfs_create_file("icache", S_IRUSR, arch_debugfs_dir,
					    (unsigned int *)CACHE_TYPE_ICACHE,
					    &cache_debugfs_fops);
	if (!icache_dentry) {
		debugfs_remove(dcache_dentry);
		return -ENOMEM;
	}

	return 0;
}
module_init(cache_debugfs_init);

MODULE_LICENSE("GPL v2");
