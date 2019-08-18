/*
 *  c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * /dev/nvram driver for PPC64
 */


#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>
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

/* See clobbering_unread_rtas_event() */
#define NVRAM_RTAS_READ_TIMEOUT 5		/* seconds */
static time64_t last_unread_rtas_event;		/* timestamp */

#ifdef CONFIG_PSTORE
time64_t last_rtas_event;
#endif

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
 * the failure information to decode.
 */
int nvram_write_error_log(char * buff, int length,
                          unsigned int err_type, unsigned int error_log_cnt)
{
	int rc = nvram_write_os_partition(&rtas_log_partition, buff, length,
						err_type, error_log_cnt);
	if (!rc) {
		last_unread_rtas_event = ktime_get_real_seconds();
#ifdef CONFIG_PSTORE
		last_rtas_event = ktime_get_real_seconds();
#endif
	}

	return rc;
}

/* nvram_read_error_log
 *
 * Reads nvram for error log for at most 'length'
 */
int nvram_read_error_log(char *buff, int length,
			unsigned int *err_type, unsigned int *error_log_cnt)
{
	return nvram_read_partition(&rtas_log_partition, buff, length,
						err_type, error_log_cnt);
}

/* This doesn't actually zero anything, but it sets the event_logged
 * word to tell that this event is safely in syslog.
 */
int nvram_clear_error_log(void)
{
	loff_t tmp_index;
	int clear_word = ERR_FLAG_ALREADY_LOGGED;
	int rc;

	if (rtas_log_partition.index == -1)
		return -1;

	tmp_index = rtas_log_partition.index;
	
	rc = ppc_md.nvram_write((char *)&clear_word, sizeof(int), &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_clear_error_log: Failed nvram_write (%d)\n", rc);
		return rc;
	}
	last_unread_rtas_event = 0;

	return 0;
}

/*
 * Are we using the ibm,rtas-log for oops/panic reports?  And if so,
 * would logging this oops/panic overwrite an RTAS event that rtas_errd
 * hasn't had a chance to read and process?  Return 1 if so, else 0.
 *
 * We assume that if rtas_errd hasn't read the RTAS event in
 * NVRAM_RTAS_READ_TIMEOUT seconds, it's probably not going to.
 */
int clobbering_unread_rtas_event(void)
{
	return (oops_log_partition.index == rtas_log_partition.index
		&& last_unread_rtas_event
		&& ktime_get_real_seconds() - last_unread_rtas_event <=
						NVRAM_RTAS_READ_TIMEOUT);
}

static int __init pseries_nvram_init_log_partitions(void)
{
	int rc;

	/* Scan nvram for partitions */
	nvram_scan_partitions();

	rc = nvram_init_os_partition(&rtas_log_partition);
	nvram_init_oops_partition(rc == 0);
	return 0;
}
machine_arch_initcall(pseries, pseries_nvram_init_log_partitions);

int __init pSeries_nvram_init(void)
{
	struct device_node *nvram;
	const __be32 *nbytes_p;
	unsigned int proplen;

	nvram = of_find_node_by_type(NULL, "nvram");
	if (nvram == NULL)
		return -ENODEV;

	nbytes_p = of_get_property(nvram, "#bytes", &proplen);
	if (nbytes_p == NULL || proplen != sizeof(unsigned int)) {
		of_node_put(nvram);
		return -EIO;
	}

	nvram_size = be32_to_cpup(nbytes_p);

	nvram_fetch = rtas_token("nvram-fetch");
	nvram_store = rtas_token("nvram-store");
	printk(KERN_INFO "PPC64 nvram contains %d bytes\n", nvram_size);
	of_node_put(nvram);

	ppc_md.nvram_read	= pSeries_nvram_read;
	ppc_md.nvram_write	= pSeries_nvram_write;
	ppc_md.nvram_size	= pSeries_nvram_get_size;

	return 0;
}

