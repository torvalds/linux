/*
 *  c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * /dev/nvram driver for PPC64
 *
 * This perhaps should live in drivers/char
 */


#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <asm/nvram.h>
#include <asm/rtas.h>
#include <asm/prom.h>
#include <asm/machdep.h>

/* Max bytes to read/write in one go */
#define NVRW_CNT 0x20

static unsigned int nvram_size;
static int nvram_fetch, nvram_store;
static char nvram_buf[NVRW_CNT];	/* assume this is in the first 4GB */
static DEFINE_SPINLOCK(nvram_lock);

static long nvram_error_log_index = -1;
static long nvram_error_log_size = 0;

struct err_log_info {
	int error_type;
	unsigned int seq_num;
};
#define NVRAM_MAX_REQ		2079
#define NVRAM_MIN_REQ		1055

#define NVRAM_LOG_PART_NAME	"ibm,rtas-log"

static ssize_t pSeries_nvram_read(char *buf, size_t count, loff_t *index)
{
	unsigned int i;
	unsigned long len;
	int done;
	unsigned long flags;
	char *p = buf;


	if (nvram_size == 0 || nvram_fetch == RTAS_UNKNOWN_SERVICE)
		return -ENODEV;

	if (*index >= nvram_size)
		return 0;

	i = *index;
	if (i + count > nvram_size)
		count = nvram_size - i;

	spin_lock_irqsave(&nvram_lock, flags);

	for (; count != 0; count -= len) {
		len = count;
		if (len > NVRW_CNT)
			len = NVRW_CNT;
		
		if ((rtas_call(nvram_fetch, 3, 2, &done, i, __pa(nvram_buf),
			       len) != 0) || len != done) {
			spin_unlock_irqrestore(&nvram_lock, flags);
			return -EIO;
		}
		
		memcpy(p, nvram_buf, len);

		p += len;
		i += len;
	}

	spin_unlock_irqrestore(&nvram_lock, flags);
	
	*index = i;
	return p - buf;
}

static ssize_t pSeries_nvram_write(char *buf, size_t count, loff_t *index)
{
	unsigned int i;
	unsigned long len;
	int done;
	unsigned long flags;
	const char *p = buf;

	if (nvram_size == 0 || nvram_store == RTAS_UNKNOWN_SERVICE)
		return -ENODEV;

	if (*index >= nvram_size)
		return 0;

	i = *index;
	if (i + count > nvram_size)
		count = nvram_size - i;

	spin_lock_irqsave(&nvram_lock, flags);

	for (; count != 0; count -= len) {
		len = count;
		if (len > NVRW_CNT)
			len = NVRW_CNT;

		memcpy(nvram_buf, p, len);

		if ((rtas_call(nvram_store, 3, 2, &done, i, __pa(nvram_buf),
			       len) != 0) || len != done) {
			spin_unlock_irqrestore(&nvram_lock, flags);
			return -EIO;
		}
		
		p += len;
		i += len;
	}
	spin_unlock_irqrestore(&nvram_lock, flags);
	
	*index = i;
	return p - buf;
}

static ssize_t pSeries_nvram_get_size(void)
{
	return nvram_size ? nvram_size : -ENODEV;
}


/* nvram_write_error_log
 *
 * We need to buffer the error logs into nvram to ensure that we have
 * the failure information to decode.  If we have a severe error there
 * is no way to guarantee that the OS or the machine is in a state to
 * get back to user land and write the error to disk.  For example if
 * the SCSI device driver causes a Machine Check by writing to a bad
 * IO address, there is no way of guaranteeing that the device driver
 * is in any state that is would also be able to write the error data
 * captured to disk, thus we buffer it in NVRAM for analysis on the
 * next boot.
 *
 * In NVRAM the partition containing the error log buffer will looks like:
 * Header (in bytes):
 * +-----------+----------+--------+------------+------------------+
 * | signature | checksum | length | name       | data             |
 * |0          |1         |2      3|4         15|16        length-1|
 * +-----------+----------+--------+------------+------------------+
 *
 * The 'data' section would look like (in bytes):
 * +--------------+------------+-----------------------------------+
 * | event_logged | sequence # | error log                         |
 * |0            3|4          7|8            nvram_error_log_size-1|
 * +--------------+------------+-----------------------------------+
 *
 * event_logged: 0 if event has not been logged to syslog, 1 if it has
 * sequence #: The unique sequence # for each event. (until it wraps)
 * error log: The error log from event_scan
 */
int nvram_write_error_log(char * buff, int length,
                          unsigned int err_type, unsigned int error_log_cnt)
{
	int rc;
	loff_t tmp_index;
	struct err_log_info info;
	
	if (nvram_error_log_index == -1) {
		return -ESPIPE;
	}

	if (length > nvram_error_log_size) {
		length = nvram_error_log_size;
	}

	info.error_type = err_type;
	info.seq_num = error_log_cnt;

	tmp_index = nvram_error_log_index;

	rc = ppc_md.nvram_write((char *)&info, sizeof(struct err_log_info), &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_write_error_log: Failed nvram_write (%d)\n", rc);
		return rc;
	}

	rc = ppc_md.nvram_write(buff, length, &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_write_error_log: Failed nvram_write (%d)\n", rc);
		return rc;
	}
	
	return 0;
}

/* nvram_read_error_log
 *
 * Reads nvram for error log for at most 'length'
 */
int nvram_read_error_log(char * buff, int length,
                         unsigned int * err_type, unsigned int * error_log_cnt)
{
	int rc;
	loff_t tmp_index;
	struct err_log_info info;
	
	if (nvram_error_log_index == -1)
		return -1;

	if (length > nvram_error_log_size)
		length = nvram_error_log_size;

	tmp_index = nvram_error_log_index;

	rc = ppc_md.nvram_read((char *)&info, sizeof(struct err_log_info), &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_read_error_log: Failed nvram_read (%d)\n", rc);
		return rc;
	}

	rc = ppc_md.nvram_read(buff, length, &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_read_error_log: Failed nvram_read (%d)\n", rc);
		return rc;
	}

	*error_log_cnt = info.seq_num;
	*err_type = info.error_type;

	return 0;
}

/* This doesn't actually zero anything, but it sets the event_logged
 * word to tell that this event is safely in syslog.
 */
int nvram_clear_error_log(void)
{
	loff_t tmp_index;
	int clear_word = ERR_FLAG_ALREADY_LOGGED;
	int rc;

	if (nvram_error_log_index == -1)
		return -1;

	tmp_index = nvram_error_log_index;
	
	rc = ppc_md.nvram_write((char *)&clear_word, sizeof(int), &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_clear_error_log: Failed nvram_write (%d)\n", rc);
		return rc;
	}

	return 0;
}

/* pseries_nvram_init_log_partition
 *
 * This will setup the partition we need for buffering the
 * error logs and cleanup partitions if needed.
 *
 * The general strategy is the following:
 * 1.) If there is log partition large enough then use it.
 * 2.) If there is none large enough, search
 * for a free partition that is large enough.
 * 3.) If there is not a free partition large enough remove 
 * _all_ OS partitions and consolidate the space.
 * 4.) Will first try getting a chunk that will satisfy the maximum
 * error log size (NVRAM_MAX_REQ).
 * 5.) If the max chunk cannot be allocated then try finding a chunk
 * that will satisfy the minum needed (NVRAM_MIN_REQ).
 */
static int __init pseries_nvram_init_log_partition(void)
{
	loff_t p;
	int size;

	/* Scan nvram for partitions */
	nvram_scan_partitions();

	/* Lookg for ours */
	p = nvram_find_partition(NVRAM_LOG_PART_NAME, NVRAM_SIG_OS, &size);

	/* Found one but too small, remove it */
	if (p && size < NVRAM_MIN_REQ) {
		pr_info("nvram: Found too small "NVRAM_LOG_PART_NAME" partition"
			",removing it...");
		nvram_remove_partition(NVRAM_LOG_PART_NAME, NVRAM_SIG_OS);
		p = 0;
	}

	/* Create one if we didn't find */
	if (!p) {
		p = nvram_create_partition(NVRAM_LOG_PART_NAME, NVRAM_SIG_OS,
					   NVRAM_MAX_REQ, NVRAM_MIN_REQ);
		/* No room for it, try to get rid of any OS partition
		 * and try again
		 */
		if (p == -ENOSPC) {
			pr_info("nvram: No room to create "NVRAM_LOG_PART_NAME
				" partition, deleting all OS partitions...");
			nvram_remove_partition(NULL, NVRAM_SIG_OS);
			p = nvram_create_partition(NVRAM_LOG_PART_NAME,
						   NVRAM_SIG_OS, NVRAM_MAX_REQ,
						   NVRAM_MIN_REQ);
		}
	}

	if (p <= 0) {
		pr_err("nvram: Failed to find or create "NVRAM_LOG_PART_NAME
		       " partition, err %d\n", (int)p);
		return 0;
	}

	nvram_error_log_index = p;
	nvram_error_log_size = nvram_get_partition_size(p) -
		sizeof(struct err_log_info);
	
	return 0;
}
machine_arch_initcall(pseries, pseries_nvram_init_log_partition);

int __init pSeries_nvram_init(void)
{
	struct device_node *nvram;
	const unsigned int *nbytes_p;
	unsigned int proplen;

	nvram = of_find_node_by_type(NULL, "nvram");
	if (nvram == NULL)
		return -ENODEV;

	nbytes_p = of_get_property(nvram, "#bytes", &proplen);
	if (nbytes_p == NULL || proplen != sizeof(unsigned int)) {
		of_node_put(nvram);
		return -EIO;
	}

	nvram_size = *nbytes_p;

	nvram_fetch = rtas_token("nvram-fetch");
	nvram_store = rtas_token("nvram-store");
	printk(KERN_INFO "PPC64 nvram contains %d bytes\n", nvram_size);
	of_node_put(nvram);

	ppc_md.nvram_read	= pSeries_nvram_read;
	ppc_md.nvram_write	= pSeries_nvram_write;
	ppc_md.nvram_size	= pSeries_nvram_get_size;

	return 0;
}
