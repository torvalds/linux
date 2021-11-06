/*
 * Platform Dependent file for usage of Preallocted Memory
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/workqueue.h>
#include <linux/unistd.h>
#include <linux/bug.h>
#include <linux/skbuff.h>
#include <linux/init.h>

#ifdef CONFIG_BROADCOM_WIFI_RESERVED_MEM

#define WLAN_STATIC_SCAN_BUF0		5
#define WLAN_STATIC_SCAN_BUF1		6
#define WLAN_STATIC_DHD_INFO_BUF	7
#define WLAN_STATIC_DHD_WLFC_BUF	8
#define WLAN_STATIC_DHD_IF_FLOW_LKUP	9
#define WLAN_STATIC_DHD_MEMDUMP_RAM	11
#define WLAN_STATIC_DHD_WLFC_HANGER	12
#define WLAN_STATIC_DHD_PKTID_MAP	13
#define WLAN_STATIC_DHD_PKTID_IOCTL_MAP	14
#define WLAN_STATIC_DHD_LOG_DUMP_BUF	15
#define WLAN_STATIC_DHD_LOG_DUMP_BUF_EX	16
#define WLAN_STATIC_DHD_PKTLOG_DUMP_BUF	17

#define WLAN_SCAN_BUF_SIZE		(64 * 1024)

#define WLAN_DHD_INFO_BUF_SIZE		(64 * 1024)
#define WLAN_DHD_WLFC_BUF_SIZE		(64 * 1024)
#define WLAN_DHD_IF_FLOW_LKUP_SIZE	(64 * 1024)
/* Have 2MB ramsize to accomodate future chips */
#define WLAN_DHD_MEMDUMP_SIZE		(3 * 1024 * 1024)

#define PREALLOC_WLAN_SEC_NUM		4
#define PREALLOC_WLAN_BUF_NUM		160
#define PREALLOC_WLAN_SECTION_HEADER	24

#ifdef CONFIG_BCMDHD_PCIE
#define DHD_SKB_1PAGE_BUFSIZE	(PAGE_SIZE*1)
#define DHD_SKB_2PAGE_BUFSIZE	(PAGE_SIZE*2)
#define DHD_SKB_4PAGE_BUFSIZE	(PAGE_SIZE*4)

#define WLAN_SECTION_SIZE_0	(PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_1	0
#define WLAN_SECTION_SIZE_2	0
#define WLAN_SECTION_SIZE_3	(PREALLOC_WLAN_BUF_NUM * 1024)

#define DHD_SKB_1PAGE_BUF_NUM	0
#define DHD_SKB_2PAGE_BUF_NUM	192
#define DHD_SKB_4PAGE_BUF_NUM	0

#else
#define DHD_SKB_HDRSIZE		336
#define DHD_SKB_1PAGE_BUFSIZE	((PAGE_SIZE*1)-DHD_SKB_HDRSIZE)
#define DHD_SKB_2PAGE_BUFSIZE	((PAGE_SIZE*2)-DHD_SKB_HDRSIZE)
#define DHD_SKB_4PAGE_BUFSIZE	((PAGE_SIZE*4)-DHD_SKB_HDRSIZE)

#define WLAN_SECTION_SIZE_0	(PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_1	(PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_2	(PREALLOC_WLAN_BUF_NUM * 512)
#define WLAN_SECTION_SIZE_3	(PREALLOC_WLAN_BUF_NUM * 1024)

#define DHD_SKB_1PAGE_BUF_NUM	8
#define DHD_SKB_2PAGE_BUF_NUM	8
#define DHD_SKB_4PAGE_BUF_NUM	1
#endif /* CONFIG_BCMDHD_PCIE */

#define WLAN_SKB_1_2PAGE_BUF_NUM	((DHD_SKB_1PAGE_BUF_NUM) + \
		(DHD_SKB_2PAGE_BUF_NUM))
#define WLAN_SKB_BUF_NUM	((WLAN_SKB_1_2PAGE_BUF_NUM) + \
		(DHD_SKB_4PAGE_BUF_NUM))

#define WLAN_MAX_PKTID_ITEMS		(8192)
#define WLAN_DHD_PKTID_MAP_HDR_SIZE	(20 + 4*(WLAN_MAX_PKTID_ITEMS + 1))
#define WLAN_DHD_PKTID_MAP_ITEM_SIZE	(32)
#define WLAN_DHD_PKTID_MAP_SIZE		((WLAN_DHD_PKTID_MAP_HDR_SIZE) + \
		((WLAN_MAX_PKTID_ITEMS+1) * WLAN_DHD_PKTID_MAP_ITEM_SIZE))

#define WLAN_MAX_PKTID_IOCTL_ITEMS	(32)
#define WLAN_DHD_PKTID_IOCTL_MAP_HDR_SIZE	(20 + 4*(WLAN_MAX_PKTID_IOCTL_ITEMS + 1))
#define WLAN_DHD_PKTID_IOCTL_MAP_ITEM_SIZE	(32)
#define WLAN_DHD_PKTID_IOCTL_MAP_SIZE		((WLAN_DHD_PKTID_IOCTL_MAP_HDR_SIZE) + \
		((WLAN_MAX_PKTID_IOCTL_ITEMS+1) * WLAN_DHD_PKTID_IOCTL_MAP_ITEM_SIZE))

#define DHD_LOG_DUMP_BUF_SIZE	(1024 * 1024 * 4)
#define DHD_LOG_DUMP_BUF_EX_SIZE	(1024 * 1024 * 2)

#define DHD_PKTLOG_DUMP_BUF_SIZE	(64 * 1024)

#define WLAN_DHD_WLFC_HANGER_MAXITEMS		3072
#define WLAN_DHD_WLFC_HANGER_ITEM_SIZE		32
#define WLAN_DHD_WLFC_HANGER_SIZE	((WLAN_DHD_WLFC_HANGER_ITEM_SIZE) + \
	((WLAN_DHD_WLFC_HANGER_MAXITEMS) * (WLAN_DHD_WLFC_HANGER_ITEM_SIZE)))

static struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM];

struct wlan_mem_prealloc {
	void *mem_ptr;
	unsigned long size;
};

static struct wlan_mem_prealloc wlan_mem_array[PREALLOC_WLAN_SEC_NUM] = {
	{NULL, (WLAN_SECTION_SIZE_0 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_1 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_2 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_3 + PREALLOC_WLAN_SECTION_HEADER)}
};

static void *wlan_static_scan_buf0 = NULL;
static void *wlan_static_scan_buf1 = NULL;
static void *wlan_static_dhd_info_buf = NULL;
static void *wlan_static_dhd_wlfc_buf = NULL;
static void *wlan_static_if_flow_lkup = NULL;
static void *wlan_static_dhd_memdump_ram = NULL;
static void *wlan_static_dhd_wlfc_hanger = NULL;
static void *wlan_static_dhd_pktid_map = NULL;
static void *wlan_static_dhd_pktid_ioctl_map = NULL;
static void *wlan_static_dhd_log_dump_buf = NULL;
static void *wlan_static_dhd_log_dump_buf_ex = NULL;
static void *wlan_static_dhd_pktlog_dump_buf = NULL;

void dhd_exit_wlan_mem(void);

void
*dhd_wlan_mem_prealloc(int section, unsigned long size)
{
	if (section == PREALLOC_WLAN_SEC_NUM) {
		return wlan_static_skb;
	}

	if (section == WLAN_STATIC_SCAN_BUF0) {
		return wlan_static_scan_buf0;
	}

	if (section == WLAN_STATIC_SCAN_BUF1) {
		return wlan_static_scan_buf1;
	}

	if (section == WLAN_STATIC_DHD_INFO_BUF) {
		if (size > WLAN_DHD_INFO_BUF_SIZE) {
			pr_err("request DHD_INFO size(%lu) is bigger than"
				" static size(%d).\n", size,
				WLAN_DHD_INFO_BUF_SIZE);
			return NULL;
		}
		return wlan_static_dhd_info_buf;
	}

	if (section == WLAN_STATIC_DHD_WLFC_BUF)  {
		if (size > WLAN_DHD_WLFC_BUF_SIZE) {
			pr_err("request DHD_WLFC size(%lu) is bigger than"
				" static size(%d).\n",
				size, WLAN_DHD_WLFC_BUF_SIZE);
			return NULL;
		}
		return wlan_static_dhd_wlfc_buf;
	}

	if (section == WLAN_STATIC_DHD_WLFC_HANGER) {
		if (size > WLAN_DHD_WLFC_HANGER_SIZE) {
			pr_err("request DHD_WLFC_HANGER size(%lu) is bigger than"
				" static size(%d).\n",
				size, WLAN_DHD_WLFC_HANGER_SIZE);
			return NULL;
		}
		return wlan_static_dhd_wlfc_hanger;
	}

	if (section == WLAN_STATIC_DHD_IF_FLOW_LKUP)  {
		if (size > WLAN_DHD_IF_FLOW_LKUP_SIZE) {
			pr_err("request DHD_WLFC size(%lu) is bigger than"
				" static size(%d).\n",
				size, WLAN_DHD_WLFC_BUF_SIZE);
			return NULL;
		}
		return wlan_static_if_flow_lkup;
	}

	if (section == WLAN_STATIC_DHD_MEMDUMP_RAM) {
		if (size > WLAN_DHD_MEMDUMP_SIZE) {
			pr_err("request DHD_MEMDUMP_RAM size(%lu) is bigger"
				" than static size(%d).\n",
				size, WLAN_DHD_MEMDUMP_SIZE);
			return NULL;
		}
		return wlan_static_dhd_memdump_ram;
	}

	if (section == WLAN_STATIC_DHD_PKTID_MAP)  {
		if (size > WLAN_DHD_PKTID_MAP_SIZE) {
			pr_err("request DHD_PKTID_MAP size(%lu) is bigger than"
				" static size(%d).\n",
				size, WLAN_DHD_PKTID_MAP_SIZE);
			return NULL;
		}
		return wlan_static_dhd_pktid_map;
	}

	if (section == WLAN_STATIC_DHD_PKTID_IOCTL_MAP)  {
		if (size > WLAN_DHD_PKTID_IOCTL_MAP_SIZE) {
			pr_err("request DHD_PKTID_IOCTL_MAP size(%lu) is bigger than"
				" static size(%d).\n",
				size, WLAN_DHD_PKTID_IOCTL_MAP_SIZE);
			return NULL;
		}
		return wlan_static_dhd_pktid_ioctl_map;
	}

	if (section == WLAN_STATIC_DHD_LOG_DUMP_BUF) {
		if (size > DHD_LOG_DUMP_BUF_SIZE) {
			pr_err("request DHD_LOG_DUMP_BUF size(%lu) is bigger then"
				" static size(%d).\n",
				size, DHD_LOG_DUMP_BUF_SIZE);
			return NULL;
		}
		return wlan_static_dhd_log_dump_buf;
	}

	if (section == WLAN_STATIC_DHD_LOG_DUMP_BUF_EX) {
		if (size > DHD_LOG_DUMP_BUF_EX_SIZE) {
			pr_err("request DHD_LOG_DUMP_BUF_EX size(%lu) is bigger then"
				" static size(%d).\n",
				size, DHD_LOG_DUMP_BUF_EX_SIZE);
			return NULL;
		}
		return wlan_static_dhd_log_dump_buf_ex;
	}

	if (section == WLAN_STATIC_DHD_PKTLOG_DUMP_BUF) {
		if (size > DHD_PKTLOG_DUMP_BUF_SIZE) {
			pr_err("request DHD_PKTLOG_DUMP_BUF size(%lu) is bigger then"
					" static size(%d).\n",
					size, DHD_PKTLOG_DUMP_BUF_SIZE);
			return NULL;
		}
		return wlan_static_dhd_pktlog_dump_buf;
	}

	if ((section < 0) || (section >= PREALLOC_WLAN_SEC_NUM)) {
		return NULL;
	}

	if (wlan_mem_array[section].size < size) {
		return NULL;
	}

	return wlan_mem_array[section].mem_ptr;
}
EXPORT_SYMBOL(dhd_wlan_mem_prealloc);

int
dhd_init_wlan_mem(void)
{
	int i;
	int j;

#if !defined(CONFIG_BCMDHD_PCIE)
	for (i = 0; i < DHD_SKB_1PAGE_BUF_NUM; i++) {
		wlan_static_skb[i] = __dev_alloc_skb(DHD_SKB_1PAGE_BUFSIZE, GFP_KERNEL);
		if (!wlan_static_skb[i]) {
			pr_err("Failed to alloc 1PAGE SKB BUF\n");
			goto err_skb_alloc;
		}
	}
#endif /* !CONFIG_BCMDHD_PCIE */

	for (i = DHD_SKB_1PAGE_BUF_NUM; i < WLAN_SKB_1_2PAGE_BUF_NUM; i++) {
		wlan_static_skb[i] = __dev_alloc_skb(DHD_SKB_2PAGE_BUFSIZE, GFP_KERNEL);
		if (!wlan_static_skb[i]) {
			pr_err("Failed to alloc 2PAGE SKB BUF\n");
			goto err_skb_alloc;
		}
	}

#if !defined(CONFIG_BCMDHD_PCIE)
	wlan_static_skb[i] = __dev_alloc_skb(DHD_SKB_4PAGE_BUFSIZE, GFP_KERNEL);
	if (!wlan_static_skb[i]) {
		pr_err("Failed to alloc 4PAGE SKB BUF\n");
		goto err_skb_alloc;
	}
#endif /* !CONFIG_BCMDHD_PCIE */

	for (i = 0; i < PREALLOC_WLAN_SEC_NUM; i++) {
		if (wlan_mem_array[i].size > 0) {
			wlan_mem_array[i].mem_ptr =
				kmalloc(wlan_mem_array[i].size, GFP_KERNEL);

			if (!wlan_mem_array[i].mem_ptr) {
				pr_err("Failed to mem_alloc for WLAN\n");
				goto err_mem_alloc;
			}
		}
	}

	wlan_static_scan_buf0 = kmalloc(WLAN_SCAN_BUF_SIZE, GFP_KERNEL);
	if (!wlan_static_scan_buf0) {
		pr_err("Failed to alloc wlan_static_scan_buf0\n");
		goto err_mem_alloc;
	}

	wlan_static_scan_buf1 = kmalloc(WLAN_SCAN_BUF_SIZE, GFP_KERNEL);
	if (!wlan_static_scan_buf1) {
		pr_err("Failed to alloc wlan_static_scan_buf1\n");
		goto err_mem_alloc;
	}

	wlan_static_dhd_log_dump_buf = kmalloc(DHD_LOG_DUMP_BUF_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_log_dump_buf) {
		pr_err("Failed to alloc wlan_static_dhd_log_dump_buf\n");
		goto err_mem_alloc;
	}

	wlan_static_dhd_log_dump_buf_ex = kmalloc(DHD_LOG_DUMP_BUF_EX_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_log_dump_buf_ex) {
		pr_err("Failed to alloc wlan_static_dhd_log_dump_buf_ex\n");
		goto err_mem_alloc;
	}

	wlan_static_dhd_info_buf = kmalloc(WLAN_DHD_INFO_BUF_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_info_buf) {
		pr_err("Failed to alloc wlan_static_dhd_info_buf\n");
		goto err_mem_alloc;
	}

#ifdef CONFIG_BCMDHD_PCIE
	wlan_static_if_flow_lkup = kmalloc(WLAN_DHD_IF_FLOW_LKUP_SIZE,
		GFP_KERNEL);
	if (!wlan_static_if_flow_lkup) {
		pr_err("Failed to alloc wlan_static_if_flow_lkup\n");
		goto err_mem_alloc;
	}

#ifdef CONFIG_BCMDHD_PREALLOC_PKTIDMAP
	wlan_static_dhd_pktid_map = kmalloc(WLAN_DHD_PKTID_MAP_SIZE,
		GFP_KERNEL);
	if (!wlan_static_dhd_pktid_map) {
		pr_err("Failed to alloc wlan_static_dhd_pktid_map\n");
		goto err_mem_alloc;
	}

	wlan_static_dhd_pktid_ioctl_map = kmalloc(WLAN_DHD_PKTID_IOCTL_MAP_SIZE,
		GFP_KERNEL);
	if (!wlan_static_dhd_pktid_ioctl_map) {
		pr_err("Failed to alloc wlan_static_dhd_pktid_ioctl_map\n");
		goto err_mem_alloc;
	}
#endif /* CONFIG_BCMDHD_PREALLOC_PKTIDMAP */
#else
	wlan_static_dhd_wlfc_buf = kmalloc(WLAN_DHD_WLFC_BUF_SIZE,
		GFP_KERNEL);
	if (!wlan_static_dhd_wlfc_buf) {
		pr_err("Failed to alloc wlan_static_dhd_wlfc_buf\n");
		goto err_mem_alloc;
	}

	wlan_static_dhd_wlfc_hanger = kmalloc(WLAN_DHD_WLFC_HANGER_SIZE,
		GFP_KERNEL);
	if (!wlan_static_dhd_wlfc_hanger) {
		pr_err("Failed to alloc wlan_static_dhd_wlfc_hanger\n");
		goto err_mem_alloc;
	}
#endif /* CONFIG_BCMDHD_PCIE */

#ifdef CONFIG_BCMDHD_PREALLOC_MEMDUMP
	wlan_static_dhd_memdump_ram = kmalloc(WLAN_DHD_MEMDUMP_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_memdump_ram) {
		pr_err("Failed to alloc wlan_static_dhd_memdump_ram\n");
		goto err_mem_alloc;
	}
#endif /* CONFIG_BCMDHD_PREALLOC_MEMDUMP */

	wlan_static_dhd_pktlog_dump_buf = kmalloc(DHD_PKTLOG_DUMP_BUF_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_pktlog_dump_buf) {
		pr_err("Failed to alloc wlan_static_dhd_pktlog_dump_buf\n");
		goto err_mem_alloc;
	}

	pr_err("%s: WIFI MEM Allocated\n", __FUNCTION__);
	return 0;

err_mem_alloc:
	dhd_exit_wlan_mem();
	return -ENOMEM;

err_skb_alloc:
	/*
	 * When all the skb alloc buf couldn't alloced, free these buf with alloced size
	 * dhd_exit_wlan_mem will free with total size (don't know alloced size)
	 */
	pr_err("Failed to skb_alloc for WLAN\n");
	for (j = 0; j < i; j++) {
		dev_kfree_skb(wlan_static_skb[j]);
	}
	return -ENOMEM;
}

EXPORT_SYMBOL(dhd_init_wlan_mem);

void
dhd_exit_wlan_mem(void)
{
	int i = 0;

#ifdef CONFIG_BCMDHD_PREALLOC_MEMDUMP
	if (wlan_static_dhd_memdump_ram) {
		kfree(wlan_static_dhd_memdump_ram);
	}

#endif /* CONFIG_BCMDHD_PREALLOC_MEMDUMP */

#ifdef CONFIG_BCMDHD_PCIE
	if (wlan_static_if_flow_lkup) {
		kfree(wlan_static_if_flow_lkup);
	}

#ifdef CONFIG_BCMDHD_PREALLOC_PKTIDMAP
	if (wlan_static_dhd_pktid_map) {
		kfree(wlan_static_dhd_pktid_map);
	}

	if (wlan_static_dhd_pktid_ioctl_map) {
		kfree(wlan_static_dhd_pktid_ioctl_map);
	}
#endif /* CONFIG_BCMDHD_PREALLOC_PKTIDMAP */
#else
	if (wlan_static_dhd_wlfc_buf) {
		kfree(wlan_static_dhd_wlfc_buf);
	}

	if (wlan_static_dhd_wlfc_hanger) {
		kfree(wlan_static_dhd_wlfc_hanger);
	}
#endif /* CONFIG_BCMDHD_PCIE */
	if (wlan_static_dhd_info_buf) {
		kfree(wlan_static_dhd_info_buf);
	}

	if (wlan_static_dhd_log_dump_buf) {
		kfree(wlan_static_dhd_log_dump_buf);
	}

	if (wlan_static_dhd_log_dump_buf_ex) {
		kfree(wlan_static_dhd_log_dump_buf_ex);
	}

	if (wlan_static_scan_buf1) {
		kfree(wlan_static_scan_buf1);
	}

	if (wlan_static_scan_buf0) {
		kfree(wlan_static_scan_buf0);
	}

	if (wlan_static_dhd_pktlog_dump_buf) {
		kfree(wlan_static_dhd_pktlog_dump_buf);
	}

	for (i = 0; i < PREALLOC_WLAN_SEC_NUM; i++) {
		if (wlan_mem_array[i].mem_ptr) {
			kfree(wlan_mem_array[i].mem_ptr);
		}
	}

	for (i = 0; i < WLAN_SKB_BUF_NUM; i++) {
		dev_kfree_skb(wlan_static_skb[i]);
	}

	return;
}
EXPORT_SYMBOL(dhd_exit_wlan_mem);
#endif /* CONFIG_BROADCOM_WIFI_RESERVED_MEM */
