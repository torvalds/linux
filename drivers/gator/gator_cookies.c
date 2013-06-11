/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define COOKIEMAP_ENTRIES	1024	/* must be power of 2 */
#define TRANSLATE_SIZE		256
#define MAX_COLLISIONS		2

static uint32_t *gator_crc32_table;
static unsigned int translate_buffer_mask;

static DEFINE_PER_CPU(char *, translate_text);
static DEFINE_PER_CPU(uint32_t, cookie_next_key);
static DEFINE_PER_CPU(uint64_t *, cookie_keys);
static DEFINE_PER_CPU(uint32_t *, cookie_values);
static DEFINE_PER_CPU(int, translate_buffer_read);
static DEFINE_PER_CPU(int, translate_buffer_write);
static DEFINE_PER_CPU(void **, translate_buffer);

static inline uint32_t get_cookie(int cpu, struct task_struct *task, const char *text, bool from_wq);
static void wq_cookie_handler(struct work_struct *unused);
DECLARE_WORK(cookie_work, wq_cookie_handler);
static struct timer_list app_process_wake_up_timer;
static void app_process_wake_up_handler(unsigned long unused_data);

static uint32_t cookiemap_code(uint64_t value64)
{
	uint32_t value = (uint32_t)((value64 >> 32) + value64);
	uint32_t cookiecode = (value >> 24) & 0xff;
	cookiecode = cookiecode * 31 + ((value >> 16) & 0xff);
	cookiecode = cookiecode * 31 + ((value >> 8) & 0xff);
	cookiecode = cookiecode * 31 + ((value >> 0) & 0xff);
	cookiecode &= (COOKIEMAP_ENTRIES - 1);
	return cookiecode * MAX_COLLISIONS;
}

static uint32_t gator_chksum_crc32(const char *data)
{
	register unsigned long crc;
	const unsigned char *block = data;
	int i, length = strlen(data);

	crc = 0xFFFFFFFF;
	for (i = 0; i < length; i++) {
		crc = ((crc >> 8) & 0x00FFFFFF) ^ gator_crc32_table[(crc ^ *block++) & 0xFF];
	}

	return (crc ^ 0xFFFFFFFF);
}

/*
 * Exists
 *  Pre:  [0][1][v][3]..[n-1]
 *  Post: [v][0][1][3]..[n-1]
 */
static uint32_t cookiemap_exists(uint64_t key)
{
	unsigned long x, flags, retval = 0;
	int cpu = get_physical_cpu();
	uint32_t cookiecode = cookiemap_code(key);
	uint64_t *keys = &(per_cpu(cookie_keys, cpu)[cookiecode]);
	uint32_t *values = &(per_cpu(cookie_values, cpu)[cookiecode]);

	// Can be called from interrupt handler or from work queue
	local_irq_save(flags);
	for (x = 0; x < MAX_COLLISIONS; x++) {
		if (keys[x] == key) {
			uint32_t value = values[x];
			for (; x > 0; x--) {
				keys[x] = keys[x - 1];
				values[x] = values[x - 1];
			}
			keys[0] = key;
			values[0] = value;
			retval = value;
			break;
		}
	}
	local_irq_restore(flags);

	return retval;
}

/*
 * Add
 *  Pre:  [0][1][2][3]..[n-1]
 *  Post: [v][0][1][2]..[n-2]
 */
static void cookiemap_add(uint64_t key, uint32_t value)
{
	int cpu = get_physical_cpu();
	int cookiecode = cookiemap_code(key);
	uint64_t *keys = &(per_cpu(cookie_keys, cpu)[cookiecode]);
	uint32_t *values = &(per_cpu(cookie_values, cpu)[cookiecode]);
	int x;

	for (x = MAX_COLLISIONS - 1; x > 0; x--) {
		keys[x] = keys[x - 1];
		values[x] = values[x - 1];
	}
	keys[0] = key;
	values[0] = value;
}

static void translate_buffer_write_ptr(int cpu, void *x)
{
	per_cpu(translate_buffer, cpu)[per_cpu(translate_buffer_write, cpu)++] = x;
	per_cpu(translate_buffer_write, cpu) &= translate_buffer_mask;
}

static void *translate_buffer_read_ptr(int cpu)
{
	void *value = per_cpu(translate_buffer, cpu)[per_cpu(translate_buffer_read, cpu)++];
	per_cpu(translate_buffer_read, cpu) &= translate_buffer_mask;
	return value;
}

static void wq_cookie_handler(struct work_struct *unused)
{
	struct task_struct *task;
	char *text;
	int cpu = get_physical_cpu();
	unsigned int commit;

	mutex_lock(&start_mutex);

	if (gator_started != 0) {
		commit = per_cpu(translate_buffer_write, cpu);
		while (per_cpu(translate_buffer_read, cpu) != commit) {
			task = (struct task_struct *)translate_buffer_read_ptr(cpu);
			text = (char *)translate_buffer_read_ptr(cpu);
			get_cookie(cpu, task, text, true);
		}
	}

	mutex_unlock(&start_mutex);
}

static void app_process_wake_up_handler(unsigned long unused_data)
{
	// had to delay scheduling work as attempting to schedule work during the context switch is illegal in kernel versions 3.5 and greater
	schedule_work(&cookie_work);
}

// Retrieve full name from proc/pid/cmdline for java processes on Android
static int translate_app_process(const char **text, int cpu, struct task_struct *task, bool from_wq)
{
	void *maddr;
	unsigned int len;
	unsigned long addr;
	struct mm_struct *mm;
	struct page *page = NULL;
	struct vm_area_struct *page_vma;
	int bytes, offset, retval = 0, ptr;
	char *buf = per_cpu(translate_text, cpu);

	// Push work into a work queue if in atomic context as the kernel functions below might sleep
	// Rely on the in_interrupt variable rather than in_irq() or in_interrupt() kernel functions, as the value of these functions seems
	//   inconsistent during a context switch between android/linux versions
	if (!from_wq) {
		// Check if already in buffer
		ptr = per_cpu(translate_buffer_read, cpu);
		while (ptr != per_cpu(translate_buffer_write, cpu)) {
			if (per_cpu(translate_buffer, cpu)[ptr] == (void *)task)
				goto out;
			ptr = (ptr + 2) & translate_buffer_mask;
		}

		translate_buffer_write_ptr(cpu, (void *)task);
		translate_buffer_write_ptr(cpu, (void *)*text);

		mod_timer(&app_process_wake_up_timer, jiffies + 1);
		goto out;
	}

	mm = get_task_mm(task);
	if (!mm)
		goto out;
	if (!mm->arg_end)
		goto outmm;
	addr = mm->arg_start;
	len = mm->arg_end - mm->arg_start;

	if (len > TRANSLATE_SIZE)
		len = TRANSLATE_SIZE;

	down_read(&mm->mmap_sem);
	while (len) {
		if (get_user_pages(task, mm, addr, 1, 0, 1, &page, &page_vma) <= 0)
			goto outsem;

		maddr = kmap(page);
		offset = addr & (PAGE_SIZE - 1);
		bytes = len;
		if (bytes > PAGE_SIZE - offset)
			bytes = PAGE_SIZE - offset;

		copy_from_user_page(page_vma, page, addr, buf, maddr + offset, bytes);

		kunmap(page);	// release page allocated by get_user_pages()
		page_cache_release(page);

		len -= bytes;
		buf += bytes;
		addr += bytes;

		*text = per_cpu(translate_text, cpu);
		retval = 1;
	}

	// On app_process startup, /proc/pid/cmdline is initially "zygote" then "<pre-initialized>" but changes after an initial startup period
	if (strcmp(*text, "zygote") == 0 || strcmp(*text, "<pre-initialized>") == 0)
		retval = 0;

outsem:
	up_read(&mm->mmap_sem);
outmm:
	mmput(mm);
out:
	return retval;
}

static inline uint32_t get_cookie(int cpu, struct task_struct *task, const char *text, bool from_wq)
{
	unsigned long flags, cookie;
	uint64_t key;

	key = gator_chksum_crc32(text);
	key = (key << 32) | (uint32_t)task->tgid;

	cookie = cookiemap_exists(key);
	if (cookie) {
		return cookie;
	}

	if (strcmp(text, "app_process") == 0) {
		if (!translate_app_process(&text, cpu, task, from_wq))
			return INVALID_COOKIE;
	}

	// Can be called from interrupt handler or from work queue or from scheduler trace
	local_irq_save(flags);

	cookie = INVALID_COOKIE;
	if (marshal_cookie_header(text)) {
		cookie = per_cpu(cookie_next_key, cpu) += nr_cpu_ids;
		cookiemap_add(key, cookie);
		marshal_cookie(cookie, text);
	}

	local_irq_restore(flags);

	return cookie;
}

static int get_exec_cookie(int cpu, struct task_struct *task)
{
	struct mm_struct *mm = task->mm;
	const char *text;

	// kernel threads have no address space
	if (!mm)
		return NO_COOKIE;

	if (task && task->mm && task->mm->exe_file) {
		text = task->mm->exe_file->f_path.dentry->d_name.name;
		return get_cookie(cpu, task, text, false);
	}

	return INVALID_COOKIE;
}

static unsigned long get_address_cookie(int cpu, struct task_struct *task, unsigned long addr, off_t *offset)
{
	unsigned long cookie = NO_COOKIE;
	struct mm_struct *mm = task->mm;
	struct vm_area_struct *vma;
	const char *text;

	if (!mm)
		return cookie;

	for (vma = find_vma(mm, addr); vma; vma = vma->vm_next) {
		if (addr < vma->vm_start || addr >= vma->vm_end)
			continue;

		if (vma->vm_file) {
			text = vma->vm_file->f_path.dentry->d_name.name;
			cookie = get_cookie(cpu, task, text, false);
			*offset = (vma->vm_pgoff << PAGE_SHIFT) + addr - vma->vm_start;
		} else {
			/* must be an anonymous map */
			*offset = addr;
		}

		break;
	}

	if (!vma)
		cookie = INVALID_COOKIE;

	return cookie;
}

static int cookies_initialize(void)
{
	uint32_t crc, poly;
	int i, j, cpu, size, err = 0;

	int translate_buffer_size = 512;	// must be a power of 2
	translate_buffer_mask = translate_buffer_size / sizeof(per_cpu(translate_buffer, 0)[0]) - 1;

	for_each_present_cpu(cpu) {
		per_cpu(cookie_next_key, cpu) = nr_cpu_ids + cpu;

		size = COOKIEMAP_ENTRIES * MAX_COLLISIONS * sizeof(uint64_t);
		per_cpu(cookie_keys, cpu) = (uint64_t *)kmalloc(size, GFP_KERNEL);
		if (!per_cpu(cookie_keys, cpu)) {
			err = -ENOMEM;
			goto cookie_setup_error;
		}
		memset(per_cpu(cookie_keys, cpu), 0, size);

		size = COOKIEMAP_ENTRIES * MAX_COLLISIONS * sizeof(uint32_t);
		per_cpu(cookie_values, cpu) = (uint32_t *)kmalloc(size, GFP_KERNEL);
		if (!per_cpu(cookie_values, cpu)) {
			err = -ENOMEM;
			goto cookie_setup_error;
		}
		memset(per_cpu(cookie_values, cpu), 0, size);

		per_cpu(translate_buffer, cpu) = (void **)kmalloc(translate_buffer_size, GFP_KERNEL);
		if (!per_cpu(translate_buffer, cpu)) {
			err = -ENOMEM;
			goto cookie_setup_error;
		}

		per_cpu(translate_buffer_write, cpu) = 0;
		per_cpu(translate_buffer_read, cpu) = 0;

		per_cpu(translate_text, cpu) = (char *)kmalloc(TRANSLATE_SIZE, GFP_KERNEL);
		if (!per_cpu(translate_text, cpu)) {
			err = -ENOMEM;
			goto cookie_setup_error;
		}
	}

	// build CRC32 table
	poly = 0x04c11db7;
	gator_crc32_table = (uint32_t *)kmalloc(256 * sizeof(uint32_t), GFP_KERNEL);
	for (i = 0; i < 256; i++) {
		crc = i;
		for (j = 8; j > 0; j--) {
			if (crc & 1) {
				crc = (crc >> 1) ^ poly;
			} else {
				crc >>= 1;
			}
		}
		gator_crc32_table[i] = crc;
	}

	setup_timer(&app_process_wake_up_timer, app_process_wake_up_handler, 0);

cookie_setup_error:
	return err;
}

static void cookies_release(void)
{
	int cpu;

	for_each_present_cpu(cpu) {
		kfree(per_cpu(cookie_keys, cpu));
		per_cpu(cookie_keys, cpu) = NULL;

		kfree(per_cpu(cookie_values, cpu));
		per_cpu(cookie_values, cpu) = NULL;

		kfree(per_cpu(translate_buffer, cpu));
		per_cpu(translate_buffer, cpu) = NULL;
		per_cpu(translate_buffer_read, cpu) = 0;
		per_cpu(translate_buffer_write, cpu) = 0;

		kfree(per_cpu(translate_text, cpu));
		per_cpu(translate_text, cpu) = NULL;
	}

	del_timer_sync(&app_process_wake_up_timer);
	kfree(gator_crc32_table);
	gator_crc32_table = NULL;
}
