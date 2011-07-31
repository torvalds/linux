/* arch/arm/mach-tegra/board-stingray-wlan_nvs.c
 *
 * Code to extract WiFi calibration information from ATAG set up 
 * by the bootloader.
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Dmitry Shmidt <dimitrysh@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>

#include <asm/setup.h>

/* configuration tags specific to msm */
#define ATAG_WLAN_NVS	0x57494649 /* Wlan ATAG */

#define NVS_MAX_SIZE	0x800U
#define NVS_LEN_OFFSET	0x0C
/* #define NVS_DATA_OFFSET	0x40*/
#define NVS_DATA_OFFSET	0x0

static unsigned char wifi_nvs_ram[NVS_MAX_SIZE];
static unsigned wifi_nvs_size = 0;
static struct proc_dir_entry *wifi_calibration;

unsigned char *get_wifi_nvs_ram( void )
{
	return wifi_nvs_ram;
}
EXPORT_SYMBOL(get_wifi_nvs_ram);

static int __init parse_tag_wlan_nvs(const struct tag *tag)
{
	unsigned char *dptr = (unsigned char *)(&tag->u);
	unsigned size;
#ifdef ATAG_WLAN_NVS_DEBUG
	unsigned i;
#endif

	size = min((tag->hdr.size - 2) * sizeof(__u32), NVS_MAX_SIZE);
#ifdef ATAG_WLAN_NVS_DEBUG
	printk("WiFi Data size = %d , 0x%x\n", tag->hdr.size, tag->hdr.tag);
	for(i=0;( i < size );i++) {
		printk("%02x ", *dptr++);
	}
#endif
	memcpy(wifi_nvs_ram, dptr, size);
	wifi_nvs_size = size;
	return 0;
}

__tagtable(ATAG_WLAN_NVS, parse_tag_wlan_nvs);

static unsigned wifi_get_nvs_size( void )
{
#if 0
	unsigned char *ptr;
	unsigned len;

	ptr = get_wifi_nvs_ram();
	/* Size in format LE assumed */
	memcpy(&len, ptr + NVS_LEN_OFFSET, sizeof(len));
	len = min(len, (NVS_MAX_SIZE - NVS_DATA_OFFSET));
	return len;
#endif
	return wifi_nvs_size;
}

int wifi_calibration_size_set(void)
{
	if (wifi_calibration != NULL)
		wifi_calibration->size = wifi_get_nvs_size();
	return 0;
}

static int wifi_calibration_read_proc(char *page, char **start, off_t off,
					int count, int *eof, void *data)
{
	unsigned char *ptr;
	unsigned len;

	ptr = get_wifi_nvs_ram();
	len = min(wifi_get_nvs_size(), (unsigned)count);
	memcpy(page, ptr + NVS_DATA_OFFSET, len);
	return len;
}

static int __init wifi_nvs_init(void)
{
	wifi_calibration = create_proc_entry("calibration", 0444, NULL);
	if (wifi_calibration != NULL) {
		wifi_calibration->size = wifi_get_nvs_size();
		wifi_calibration->read_proc = wifi_calibration_read_proc;
		wifi_calibration->write_proc = NULL;
	}
	return 0;
}

device_initcall(wifi_nvs_init);
