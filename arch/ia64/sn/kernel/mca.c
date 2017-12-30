/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <asm/mca.h>
#include <asm/sal.h>
#include <asm/sn/sn_sal.h>

/*
 * Interval for calling SAL to poll for errors that do NOT cause error
 * interrupts. SAL will raise a CPEI if any errors are present that
 * need to be logged.
 */
#define CPEI_INTERVAL	(5*HZ)

struct timer_list sn_cpei_timer;
void sn_init_cpei_timer(void);

/* Printing oemdata from mca uses data that is not passed through SAL, it is
 * global.  Only one user at a time.
 */
static DEFINE_MUTEX(sn_oemdata_mutex);
static u8 **sn_oemdata;
static u64 *sn_oemdata_size, sn_oemdata_bufsize;

/*
 * print_hook
 *
 * This function is the callback routine that SAL calls to log error
 * info for platform errors.  buf is appended to sn_oemdata, resizing as
 * required.
 * Note: this is a SAL to OS callback, running under the same rules as the SAL
 * code.  SAL calls are run with preempt disabled so this routine must not
 * sleep.  vmalloc can sleep so print_hook cannot resize the output buffer
 * itself, instead it must set the required size and return to let the caller
 * resize the buffer then redrive the SAL call.
 */
static int print_hook(const char *fmt, ...)
{
	char buf[400];
	int len;
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	len = strlen(buf);
	if (*sn_oemdata_size + len <= sn_oemdata_bufsize)
		memcpy(*sn_oemdata + *sn_oemdata_size, buf, len);
	*sn_oemdata_size += len;
	return 0;
}

static void sn_cpei_handler(int irq, void *devid, struct pt_regs *regs)
{
	/*
	 * this function's sole purpose is to call SAL when we receive
	 * a CE interrupt from SHUB or when the timer routine decides
	 * we need to call SAL to check for CEs.
	 */

	/* CALL SAL_LOG_CE */

	ia64_sn_plat_cpei_handler();
}

static void sn_cpei_timer_handler(struct timer_list *unused)
{
	sn_cpei_handler(-1, NULL, NULL);
	mod_timer(&sn_cpei_timer, jiffies + CPEI_INTERVAL);
}

void sn_init_cpei_timer(void)
{
	timer_setup(&sn_cpei_timer, sn_cpei_timer_handler, 0);
	sn_cpei_timer.expires = jiffies + CPEI_INTERVAL;
	add_timer(&sn_cpei_timer);
}

static int
sn_platform_plat_specific_err_print(const u8 * sect_header, u8 ** oemdata,
				    u64 * oemdata_size)
{
	mutex_lock(&sn_oemdata_mutex);
	sn_oemdata = oemdata;
	sn_oemdata_size = oemdata_size;
	sn_oemdata_bufsize = 0;
	*sn_oemdata_size = PAGE_SIZE;	/* first guess at how much data will be generated */
	while (*sn_oemdata_size > sn_oemdata_bufsize) {
		u8 *newbuf = vmalloc(*sn_oemdata_size);
		if (!newbuf) {
			mutex_unlock(&sn_oemdata_mutex);
			printk(KERN_ERR "%s: unable to extend sn_oemdata\n",
			       __func__);
			return 1;
		}
		vfree(*sn_oemdata);
		*sn_oemdata = newbuf;
		sn_oemdata_bufsize = *sn_oemdata_size;
		*sn_oemdata_size = 0;
		ia64_sn_plat_specific_err_print(print_hook, (char *)sect_header);
	}
	mutex_unlock(&sn_oemdata_mutex);
	return 0;
}

/* Callback when userspace salinfo wants to decode oem data via the platform
 * kernel and/or prom.
 */
int sn_salinfo_platform_oemdata(const u8 *sect_header, u8 **oemdata, u64 *oemdata_size)
{
	efi_guid_t guid = *(efi_guid_t *)sect_header;
	int valid = 0;
	*oemdata_size = 0;
	vfree(*oemdata);
	*oemdata = NULL;
	if (efi_guidcmp(guid, SAL_PLAT_SPECIFIC_ERR_SECT_GUID) == 0) {
		sal_log_plat_specific_err_info_t *psei = (sal_log_plat_specific_err_info_t *)sect_header;
		valid = psei->valid.oem_data;
	} else if (efi_guidcmp(guid, SAL_PLAT_MEM_DEV_ERR_SECT_GUID) == 0) {
		sal_log_mem_dev_err_info_t *mdei = (sal_log_mem_dev_err_info_t *)sect_header;
		valid = mdei->valid.oem_data;
	}
	if (valid)
		return sn_platform_plat_specific_err_print(sect_header, oemdata, oemdata_size);
	else
		return 0;
}

static int __init sn_salinfo_init(void)
{
	if (ia64_platform_is("sn2"))
		salinfo_platform_oemdata = &sn_salinfo_platform_oemdata;
	return 0;
}
device_initcall(sn_salinfo_init);
