/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/skbuff.h>

#define	DHD_STATIC_VERSION_STR		"1.579.77.41.1"

#define BCMDHD_SDIO
#define BCMDHD_PCIE

enum dhd_prealloc_index {
	DHD_PREALLOC_PROT = 0,
#if defined(BCMDHD_SDIO)
	DHD_PREALLOC_RXBUF = 1,
	DHD_PREALLOC_DATABUF = 2,
#endif
	DHD_PREALLOC_OSL_BUF = 3,
	DHD_PREALLOC_SKB_BUF = 4,
	DHD_PREALLOC_WIPHY_ESCAN0 = 5,
	DHD_PREALLOC_WIPHY_ESCAN1 = 6,
	DHD_PREALLOC_DHD_INFO = 7,
	DHD_PREALLOC_DHD_WLFC_INFO = 8,
#ifdef BCMDHD_PCIE
	DHD_PREALLOC_IF_FLOW_LKUP = 9,
#endif
	DHD_PREALLOC_MEMDUMP_BUF = 10,
	DHD_PREALLOC_MEMDUMP_RAM = 11,
	DHD_PREALLOC_DHD_WLFC_HANGER = 12,
	DHD_PREALLOC_PKTID_MAP = 13,
	DHD_PREALLOC_PKTID_MAP_IOCTL = 14,
	DHD_PREALLOC_DHD_LOG_DUMP_BUF = 15,
	DHD_PREALLOC_DHD_LOG_DUMP_BUF_EX = 16,
	DHD_PREALLOC_DHD_PKTLOG_DUMP_BUF = 17,
	DHD_PREALLOC_STAT_REPORT_BUF = 18,
	DHD_PREALLOC_WL_ESCAN_INFO = 19,
	DHD_PREALLOC_FW_VERBOSE_RING = 20,
	DHD_PREALLOC_FW_EVENT_RING = 21,
	DHD_PREALLOC_DHD_EVENT_RING = 22,
	DHD_PREALLOC_NAN_EVENT_RING = 23,
	DHD_PREALLOC_MAX
};

#define STATIC_BUF_MAX_NUM	20
#define STATIC_BUF_SIZE	(PAGE_SIZE*2)

#define DHD_PREALLOC_PROT_SIZE	(16 * 1024)
#define DHD_PREALLOC_RXBUF_SIZE	(24 * 1024)
#define DHD_PREALLOC_DATABUF_SIZE	(64 * 1024)
#define DHD_PREALLOC_OSL_BUF_SIZE	(STATIC_BUF_MAX_NUM * STATIC_BUF_SIZE)
#define DHD_PREALLOC_WIPHY_ESCAN0_SIZE	(64 * 1024)
#define DHD_PREALLOC_DHD_INFO_SIZE	(30 * 1024)
#define DHD_PREALLOC_MEMDUMP_RAM_SIZE	(770 * 1024)
#define DHD_PREALLOC_DHD_WLFC_HANGER_SIZE	(73 * 1024)
#define DHD_PREALLOC_WL_ESCAN_INFO_SIZE	(66 * 1024)
#ifdef CONFIG_64BIT
#define DHD_PREALLOC_IF_FLOW_LKUP_SIZE	(20 * 1024 * 2)
#else
#define DHD_PREALLOC_IF_FLOW_LKUP_SIZE	(20 * 1024)
#endif
#define FW_VERBOSE_RING_SIZE		(64 * 1024)
#define FW_EVENT_RING_SIZE		(64 * 1024)
#define DHD_EVENT_RING_SIZE		(64 * 1024)
#define NAN_EVENT_RING_SIZE		(64 * 1024)

#if defined(CONFIG_64BIT)
#define WLAN_DHD_INFO_BUF_SIZE	(24 * 1024)
#define WLAN_DHD_WLFC_BUF_SIZE	(64 * 1024)
#define WLAN_DHD_IF_FLOW_LKUP_SIZE	(64 * 1024)
#else
#define WLAN_DHD_INFO_BUF_SIZE	(16 * 1024)
#define WLAN_DHD_WLFC_BUF_SIZE	(24 * 1024)
#define WLAN_DHD_IF_FLOW_LKUP_SIZE	(20 * 1024)
#endif /* CONFIG_64BIT */
#define WLAN_DHD_MEMDUMP_SIZE	(800 * 1024)

#define DHD_SKB_1PAGE_BUFSIZE	(PAGE_SIZE*1)
#define DHD_SKB_2PAGE_BUFSIZE	(PAGE_SIZE*2)
#define DHD_SKB_4PAGE_BUFSIZE	(PAGE_SIZE*4)

#define DHD_SKB_1PAGE_BUF_NUM	8
#ifdef BCMDHD_PCIE
#define DHD_SKB_2PAGE_BUF_NUM	64
#elif defined(BCMDHD_SDIO)
#define DHD_SKB_2PAGE_BUF_NUM	8
#endif
#define DHD_SKB_4PAGE_BUF_NUM	1

/* The number is defined in linux_osl.c
 * WLAN_SKB_1_2PAGE_BUF_NUM => STATIC_PKT_1_2PAGE_NUM
 * WLAN_SKB_BUF_NUM => STATIC_PKT_MAX_NUM
 */
#define WLAN_SKB_1_2PAGE_BUF_NUM ((DHD_SKB_1PAGE_BUF_NUM) + \
		(DHD_SKB_2PAGE_BUF_NUM))
#define WLAN_SKB_BUF_NUM ((WLAN_SKB_1_2PAGE_BUF_NUM) + (DHD_SKB_4PAGE_BUF_NUM))

void *wlan_static_prot = NULL;
void *wlan_static_rxbuf = NULL;
void *wlan_static_databuf = NULL;
void *wlan_static_osl_buf = NULL;
void *wlan_static_scan_buf0 = NULL;
void *wlan_static_scan_buf1 = NULL;
void *wlan_static_dhd_info_buf = NULL;
void *wlan_static_dhd_wlfc_info_buf = NULL;
void *wlan_static_if_flow_lkup = NULL;
void *wlan_static_dhd_memdump_ram_buf = NULL;
void *wlan_static_dhd_wlfc_hanger_buf = NULL;
void *wlan_static_wl_escan_info_buf = NULL;
void *wlan_static_fw_verbose_ring_buf = NULL;
void *wlan_static_fw_event_ring_buf = NULL;
void *wlan_static_dhd_event_ring_buf = NULL;
void *wlan_static_nan_event_ring_buf = NULL;

static struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM];

void *dhd_wlan_mem_prealloc(int section, unsigned long size)
{
	pr_err("%s: sectoin %d, %ld\n", __func__, section, size);
	if (section == DHD_PREALLOC_PROT)
		return wlan_static_prot;

#if defined(BCMDHD_SDIO)
	if (section == DHD_PREALLOC_RXBUF)
		return wlan_static_rxbuf;

	if (section == DHD_PREALLOC_DATABUF)
		return wlan_static_databuf;
#endif /* BCMDHD_SDIO */

	if (section == DHD_PREALLOC_SKB_BUF)
		return wlan_static_skb;

	if (section == DHD_PREALLOC_WIPHY_ESCAN0)
		return wlan_static_scan_buf0;

	if (section == DHD_PREALLOC_WIPHY_ESCAN1)
		return wlan_static_scan_buf1;

	if (section == DHD_PREALLOC_OSL_BUF) {
		if (size > DHD_PREALLOC_OSL_BUF_SIZE) {
			pr_err("request OSL_BUF(%lu) > %ld\n",
				size, DHD_PREALLOC_OSL_BUF_SIZE);
			return NULL;
		}
		return wlan_static_osl_buf;
	}

	if (section == DHD_PREALLOC_DHD_INFO) {
		if (size > DHD_PREALLOC_DHD_INFO_SIZE) {
			pr_err("request DHD_INFO size(%lu) > %d\n",
				size, DHD_PREALLOC_DHD_INFO_SIZE);
			return NULL;
		}
		return wlan_static_dhd_info_buf;
	}
	if (section == DHD_PREALLOC_DHD_WLFC_INFO) {
		if (size > WLAN_DHD_WLFC_BUF_SIZE) {
			pr_err("request DHD_WLFC_INFO size(%lu) > %d\n",
				size, WLAN_DHD_WLFC_BUF_SIZE);
			return NULL;
		}
		return wlan_static_dhd_wlfc_info_buf;
	}
#ifdef BCMDHD_PCIE
	if (section == DHD_PREALLOC_IF_FLOW_LKUP)  {
		if (size > DHD_PREALLOC_IF_FLOW_LKUP_SIZE) {
			pr_err("request DHD_IF_FLOW_LKUP size(%lu) > %d\n",
				size, DHD_PREALLOC_IF_FLOW_LKUP_SIZE);
			return NULL;
		}

		return wlan_static_if_flow_lkup;
	}
#endif /* BCMDHD_PCIE */
	if (section == DHD_PREALLOC_MEMDUMP_RAM) {
		if (size > DHD_PREALLOC_MEMDUMP_RAM_SIZE) {
			pr_err("request DHD_PREALLOC_MEMDUMP_RAM_SIZE(%lu) > %d\n",
				size, DHD_PREALLOC_MEMDUMP_RAM_SIZE);
			return NULL;
		}

		return wlan_static_dhd_memdump_ram_buf;
	}
	if (section == DHD_PREALLOC_DHD_WLFC_HANGER) {
		if (size > DHD_PREALLOC_DHD_WLFC_HANGER_SIZE) {
			pr_err("request DHD_WLFC_HANGER size(%lu) > %d\n",
				size, DHD_PREALLOC_DHD_WLFC_HANGER_SIZE);
			return NULL;
		}
		return wlan_static_dhd_wlfc_hanger_buf;
	}
	if (section == DHD_PREALLOC_WL_ESCAN_INFO) {
		if (size > DHD_PREALLOC_WL_ESCAN_INFO_SIZE) {
			pr_err("request DHD_PREALLOC_WL_ESCAN_INFO_SIZE(%lu) > %d\n",
				size, DHD_PREALLOC_WL_ESCAN_INFO_SIZE);
			return NULL;
		}

		return wlan_static_wl_escan_info_buf;
	}
	if (section == DHD_PREALLOC_FW_VERBOSE_RING) {
		if (size > FW_VERBOSE_RING_SIZE) {
			pr_err("request DHD_PREALLOC_FW_VERBOSE_RING(%lu) > %d\n",
				size, FW_VERBOSE_RING_SIZE);
			return NULL;
		}

		return wlan_static_fw_verbose_ring_buf;
	}
	if (section == DHD_PREALLOC_FW_EVENT_RING) {
		if (size > FW_EVENT_RING_SIZE) {
			pr_err("request DHD_PREALLOC_FW_EVENT_RING(%lu) > %d\n",
				size, FW_EVENT_RING_SIZE);
			return NULL;
		}

		return wlan_static_fw_event_ring_buf;
	}
	if (section == DHD_PREALLOC_DHD_EVENT_RING) {
		if (size > DHD_EVENT_RING_SIZE) {
			pr_err("request DHD_PREALLOC_DHD_EVENT_RING(%lu) > %d\n",
				size, DHD_EVENT_RING_SIZE);
			return NULL;
		}

		return wlan_static_dhd_event_ring_buf;
	}
	if (section == DHD_PREALLOC_NAN_EVENT_RING) {
		if (size > NAN_EVENT_RING_SIZE) {
			pr_err("request DHD_PREALLOC_NAN_EVENT_RING(%lu) > %d\n",
				size, NAN_EVENT_RING_SIZE);
			return NULL;
		}

		return wlan_static_nan_event_ring_buf;
	}
	if ((section < 0) || (section > DHD_PREALLOC_MAX))
		pr_err("request section id(%d) is out of max index %d\n",
				section, DHD_PREALLOC_MAX);

	pr_err("%s: failed to alloc section %d, size=%ld\n",
		__func__, section, size);

	return NULL;
}
EXPORT_SYMBOL(dhd_wlan_mem_prealloc);

static int dhd_init_wlan_mem(void)
{
	int i;
	int j;

	for (i = 0; i < DHD_SKB_1PAGE_BUF_NUM; i++) {
		wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_1PAGE_BUFSIZE);
		if (!wlan_static_skb[i]) {
			goto err_skb_alloc;
		}
		pr_err("%s: sectoin %d skb[%d], size=%ld\n", __func__,
			DHD_PREALLOC_SKB_BUF, i, DHD_SKB_1PAGE_BUFSIZE);
	}

	for (i = DHD_SKB_1PAGE_BUF_NUM; i < WLAN_SKB_1_2PAGE_BUF_NUM; i++) {
		wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_2PAGE_BUFSIZE);
		if (!wlan_static_skb[i]) {
			goto err_skb_alloc;
		}
		pr_err("%s: sectoin %d skb[%d], size=%ld\n", __func__,
			DHD_PREALLOC_SKB_BUF, i, DHD_SKB_2PAGE_BUFSIZE);
	}

#if defined(BCMDHD_SDIO)
	wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_4PAGE_BUFSIZE);
	if (!wlan_static_skb[i])
		goto err_skb_alloc;
	pr_err("%s: sectoin %d skb[%d], size=%ld\n", __func__,
		DHD_PREALLOC_SKB_BUF, i, DHD_SKB_4PAGE_BUFSIZE);
#endif /* BCMDHD_SDIO */

	wlan_static_prot = kmalloc(DHD_PREALLOC_PROT_SIZE, GFP_KERNEL);
	if (!wlan_static_prot)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_PROT, DHD_PREALLOC_PROT_SIZE);

#if defined(BCMDHD_SDIO)
	wlan_static_rxbuf = kmalloc(DHD_PREALLOC_RXBUF_SIZE, GFP_KERNEL);
	if (!wlan_static_rxbuf)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_RXBUF, DHD_PREALLOC_RXBUF_SIZE);

	wlan_static_databuf = kmalloc(DHD_PREALLOC_DATABUF_SIZE, GFP_KERNEL);
	if (!wlan_static_databuf)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_DATABUF, DHD_PREALLOC_DATABUF_SIZE);
#endif /* BCMDHD_SDIO */

	wlan_static_osl_buf = kmalloc(DHD_PREALLOC_OSL_BUF_SIZE, GFP_KERNEL);
	if (!wlan_static_osl_buf)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%ld\n", __func__,
		DHD_PREALLOC_OSL_BUF, DHD_PREALLOC_OSL_BUF_SIZE);

	wlan_static_scan_buf0 = kmalloc(DHD_PREALLOC_WIPHY_ESCAN0_SIZE, GFP_KERNEL);
	if (!wlan_static_scan_buf0)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_WIPHY_ESCAN0, DHD_PREALLOC_WIPHY_ESCAN0_SIZE);

	wlan_static_dhd_info_buf = kmalloc(DHD_PREALLOC_DHD_INFO_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_info_buf)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_DHD_INFO, DHD_PREALLOC_DHD_INFO_SIZE);

	wlan_static_dhd_wlfc_info_buf = kmalloc(WLAN_DHD_WLFC_BUF_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_wlfc_info_buf)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_DHD_WLFC_INFO, WLAN_DHD_WLFC_BUF_SIZE);

#ifdef BCMDHD_PCIE
	wlan_static_if_flow_lkup = kmalloc(DHD_PREALLOC_IF_FLOW_LKUP_SIZE, GFP_KERNEL);
	if (!wlan_static_if_flow_lkup)
		goto err_mem_alloc;
#endif /* BCMDHD_PCIE */

	wlan_static_dhd_memdump_ram_buf = kmalloc(DHD_PREALLOC_MEMDUMP_RAM_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_memdump_ram_buf) 
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_MEMDUMP_RAM, DHD_PREALLOC_MEMDUMP_RAM_SIZE);

	wlan_static_dhd_wlfc_hanger_buf = kmalloc(DHD_PREALLOC_DHD_WLFC_HANGER_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_wlfc_hanger_buf)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_DHD_WLFC_HANGER, DHD_PREALLOC_DHD_WLFC_HANGER_SIZE);

	wlan_static_wl_escan_info_buf = kmalloc(DHD_PREALLOC_WL_ESCAN_INFO_SIZE, GFP_KERNEL);
	if (!wlan_static_wl_escan_info_buf)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_WL_ESCAN_INFO, DHD_PREALLOC_WL_ESCAN_INFO_SIZE);

	wlan_static_fw_verbose_ring_buf = kmalloc(
						DHD_PREALLOC_WIPHY_ESCAN0_SIZE,
						GFP_KERNEL);
	if (!wlan_static_fw_verbose_ring_buf)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_FW_VERBOSE_RING, DHD_PREALLOC_WL_ESCAN_INFO_SIZE);

	wlan_static_fw_event_ring_buf = kmalloc(DHD_PREALLOC_WIPHY_ESCAN0_SIZE, GFP_KERNEL);
	if (!wlan_static_fw_event_ring_buf)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_FW_EVENT_RING, DHD_PREALLOC_WL_ESCAN_INFO_SIZE);

	wlan_static_dhd_event_ring_buf = kmalloc(DHD_PREALLOC_WIPHY_ESCAN0_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_event_ring_buf)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_DHD_EVENT_RING, DHD_PREALLOC_WL_ESCAN_INFO_SIZE);

	wlan_static_nan_event_ring_buf = kmalloc(DHD_PREALLOC_WIPHY_ESCAN0_SIZE, GFP_KERNEL);
	if (!wlan_static_nan_event_ring_buf)
		goto err_mem_alloc;
	pr_err("%s: sectoin %d, size=%d\n", __func__,
		DHD_PREALLOC_NAN_EVENT_RING, DHD_PREALLOC_WL_ESCAN_INFO_SIZE);

	return 0;

err_mem_alloc:

	if (wlan_static_prot)
		kfree(wlan_static_prot);

#if defined(BCMDHD_SDIO)
	if (wlan_static_rxbuf)
		kfree(wlan_static_rxbuf);

	if (wlan_static_databuf)
		kfree(wlan_static_databuf);
#endif /* BCMDHD_SDIO */

	if (wlan_static_osl_buf)
		kfree(wlan_static_osl_buf);

	if (wlan_static_scan_buf0)
		kfree(wlan_static_scan_buf0);

	if (wlan_static_scan_buf1)
		kfree(wlan_static_scan_buf1);

	if (wlan_static_dhd_info_buf)
		kfree(wlan_static_dhd_info_buf);

	if (wlan_static_dhd_wlfc_info_buf)
		kfree(wlan_static_dhd_wlfc_info_buf);

#ifdef BCMDHD_PCIE
	if (wlan_static_if_flow_lkup)
		kfree(wlan_static_if_flow_lkup);
#endif /* BCMDHD_PCIE */

	if (wlan_static_dhd_memdump_ram_buf)
		kfree(wlan_static_dhd_memdump_ram_buf);

	if (wlan_static_dhd_wlfc_hanger_buf)
		kfree(wlan_static_dhd_wlfc_hanger_buf);

	if (wlan_static_wl_escan_info_buf)
		kfree(wlan_static_wl_escan_info_buf);
	
#ifdef BCMDHD_PCIE
	if (wlan_static_fw_verbose_ring_buf)
		kfree(wlan_static_fw_verbose_ring_buf);

	if (wlan_static_fw_event_ring_buf)
		kfree(wlan_static_fw_event_ring_buf);

	if (wlan_static_dhd_event_ring_buf)
		kfree(wlan_static_dhd_event_ring_buf);

	if (wlan_static_nan_event_ring_buf)
		kfree(wlan_static_nan_event_ring_buf);
#endif /* BCMDHD_PCIE */

	pr_err("%s: Failed to mem_alloc for WLAN\n", __func__);

	i = WLAN_SKB_BUF_NUM;

err_skb_alloc:
	pr_err("%s: Failed to skb_alloc for WLAN\n", __func__);
	for (j = 0; j < i; j++)
		dev_kfree_skb(wlan_static_skb[j]);

	return -ENOMEM;
}

static int __init
dhd_static_buf_init(void)
{
	printk(KERN_ERR "%s(): %s\n", __func__, DHD_STATIC_VERSION_STR);

	dhd_init_wlan_mem();

	return 0;
}

static void __exit
dhd_static_buf_exit(void)
{
	int i;

	pr_err("%s()\n", __FUNCTION__);

	for (i = 0; i < DHD_SKB_1PAGE_BUF_NUM; i++) {
		if (wlan_static_skb[i])
			dev_kfree_skb(wlan_static_skb[i]);
	}

	for (i = DHD_SKB_1PAGE_BUF_NUM; i < WLAN_SKB_1_2PAGE_BUF_NUM; i++) {
		if (wlan_static_skb[i])
			dev_kfree_skb(wlan_static_skb[i]);
	}

#if defined(BCMDHD_SDIO)
	if (wlan_static_skb[i])
		dev_kfree_skb(wlan_static_skb[i]);
#endif /* BCMDHD_SDIO */

	if (wlan_static_prot)
		kfree(wlan_static_prot);

#if defined(BCMDHD_SDIO)
	if (wlan_static_rxbuf)
		kfree(wlan_static_rxbuf);

	if (wlan_static_databuf)
		kfree(wlan_static_databuf);
#endif /* BCMDHD_SDIO */

	if (wlan_static_osl_buf)
		kfree(wlan_static_osl_buf);

	if (wlan_static_scan_buf0)
		kfree(wlan_static_scan_buf0);

	if (wlan_static_scan_buf1)
		kfree(wlan_static_scan_buf1);

	if (wlan_static_dhd_info_buf)
		kfree(wlan_static_dhd_info_buf);

	if (wlan_static_dhd_wlfc_info_buf)
		kfree(wlan_static_dhd_wlfc_info_buf);

#ifdef BCMDHD_PCIE
	if (wlan_static_if_flow_lkup)
		kfree(wlan_static_if_flow_lkup);
#endif /* BCMDHD_PCIE */

	if (wlan_static_dhd_memdump_ram_buf)
		kfree(wlan_static_dhd_memdump_ram_buf);

	if (wlan_static_dhd_wlfc_hanger_buf)
		kfree(wlan_static_dhd_wlfc_hanger_buf);

	if (wlan_static_wl_escan_info_buf)
		kfree(wlan_static_wl_escan_info_buf);
	
#ifdef BCMDHD_PCIE
	if (wlan_static_fw_verbose_ring_buf)
		kfree(wlan_static_fw_verbose_ring_buf);

	if (wlan_static_fw_event_ring_buf)
		kfree(wlan_static_fw_event_ring_buf);

	if (wlan_static_dhd_event_ring_buf)
		kfree(wlan_static_dhd_event_ring_buf);

	if (wlan_static_nan_event_ring_buf)
		kfree(wlan_static_nan_event_ring_buf);
#endif

	return;
}

module_init(dhd_static_buf_init);

module_exit(dhd_static_buf_exit);
