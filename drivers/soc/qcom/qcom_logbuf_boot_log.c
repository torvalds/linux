// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */


#include <linux/module.h>
#include <linux/kmsg_dump.h>
#include <linux/kthread.h>
#include <soc/qcom/minidump.h>

#define BOOT_LOG_SIZE    SZ_512K

static char *boot_log_buf;
static char *boot_log_pos;
static unsigned int boot_log_buf_size;
static unsigned int boot_log_buf_left;
static struct kmsg_dump_iter iter;
static struct task_struct *dump_thread;

int dump_thread_func(void *arg)
{
	size_t text_len;

	while (!kthread_should_stop()) {
		while (kmsg_dump_get_line(&iter, true, boot_log_pos,
			boot_log_buf_left, &text_len)) {
			boot_log_pos += text_len;
			boot_log_buf_left -= text_len;
			if (text_len == 0)
				goto out;
		}
		schedule_timeout_interruptible(HZ);
	}
out:
	return 0;
}

static int boot_log_init(void)
{
	void *start;
	int ret = 0;
	unsigned int size = BOOT_LOG_SIZE;
	struct md_region md_entry;

	start = kzalloc(size, GFP_KERNEL);
	if (!start) {
		ret = -ENOMEM;
		goto out;
	}

	strscpy(md_entry.name, "KBOOT_LOG", sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t)start;
	md_entry.phys_addr = virt_to_phys(start);
	md_entry.size = size;
	ret = msm_minidump_add_region(&md_entry);
	if (ret < 0) {
		pr_err("Failed to add boot_log entry in minidump table\n");
		kfree(start);
		goto out;
	}

	boot_log_buf_size = size;
	boot_log_buf = start;
	boot_log_pos = boot_log_buf;
	boot_log_buf_left = boot_log_buf_size;

	/*
	 * Ensure boot_log_buf and boot_log_buf initialization
	 * is visible to other CPU's
	 */
	smp_mb();
	return 0;

out:
	return ret;
}

static int __init boot_log_dump_init(void)
{
	int ret = 0;
	u64 dumped_line;
	size_t text_len;

	ret = boot_log_init();
	if (ret < 0)
		return ret;

	kmsg_dump_rewind(&iter);
	dumped_line = iter.next_seq;
	kmsg_dump_get_buffer(&iter, true, boot_log_buf, boot_log_buf_size, &text_len);
	boot_log_pos += text_len;
	boot_log_buf_left -= text_len;
	iter.cur_seq = dumped_line;
	dump_thread = kthread_run(dump_thread_func, NULL, "dump_thread");

	return 0;
}
late_initcall(boot_log_dump_init);

void boot_log_dump_remove(void)
{
	if (dump_thread)
		kthread_stop(dump_thread);
	kfree(boot_log_buf);
}
module_exit(boot_log_dump_remove);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Boot Log Dump Driver");
MODULE_LICENSE("GPL");
