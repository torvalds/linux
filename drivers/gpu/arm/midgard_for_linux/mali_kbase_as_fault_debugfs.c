/*
 *
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include <linux/debugfs.h>

#include <mali_kbase.h>
#include <mali_kbase_as_fault_debugfs.h>

#ifdef CONFIG_DEBUG_FS
#ifdef CONFIG_MALI_DEBUG

static int kbase_as_fault_read(struct seq_file *sfile, void *data)
{
	uintptr_t as_no = (uintptr_t) sfile->private;

	struct list_head *entry;
	const struct list_head *kbdev_list;
	struct kbase_device *kbdev = NULL;

	kbdev_list = kbase_dev_list_get();

	list_for_each(entry, kbdev_list) {
		kbdev = list_entry(entry, struct kbase_device, entry);

		if(kbdev->debugfs_as_read_bitmap & (1ULL << as_no)) {

			/* don't show this one again until another fault occors */
			kbdev->debugfs_as_read_bitmap &= ~(1ULL << as_no);

			/* output the last page fault addr */
			seq_printf(sfile, "%llu\n", (u64) kbdev->as[as_no].fault_addr);
		}

	}

	kbase_dev_list_put(kbdev_list);

	return 0;
}

static int kbase_as_fault_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbase_as_fault_read , in->i_private);
}

static const struct file_operations as_fault_fops = {
	.open = kbase_as_fault_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

#endif /* CONFIG_MALI_DEBUG */
#endif /* CONFIG_DEBUG_FS */

/*
 *  Initialize debugfs entry for each address space
 */
void kbase_as_fault_debugfs_init(struct kbase_device *kbdev)
{
#ifdef CONFIG_DEBUG_FS
#ifdef CONFIG_MALI_DEBUG
	uint i;
	char as_name[64];
	struct dentry *debugfs_directory;

	kbdev->debugfs_as_read_bitmap = 0ULL;

	KBASE_DEBUG_ASSERT(kbdev->nr_hw_address_spaces);
	KBASE_DEBUG_ASSERT(sizeof(kbdev->as[0].fault_addr) == sizeof(u64));

	debugfs_directory = debugfs_create_dir("address_spaces",
		kbdev->mali_debugfs_directory);

	if(debugfs_directory) {
		for(i = 0; i < kbdev->nr_hw_address_spaces; i++) {
			snprintf(as_name, ARRAY_SIZE(as_name), "as%u", i);
			debugfs_create_file(as_name, S_IRUGO,
				debugfs_directory, (void*) ((uintptr_t) i), &as_fault_fops);
		}
	}
	else
		dev_warn(kbdev->dev, "unable to create address_spaces debugfs directory");

#endif /* CONFIG_MALI_DEBUG */
#endif /* CONFIG_DEBUG_FS */
	return;
}
