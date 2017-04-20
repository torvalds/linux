#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/skbuff.h>

enum dhd_prealloc_index {
	DHD_PREALLOC_PROT = 0,
	DHD_PREALLOC_RXBUF,
	DHD_PREALLOC_DATABUF,
	DHD_PREALLOC_OSL_BUF,
	DHD_PREALLOC_SKB_BUF,
	DHD_PREALLOC_WIPHY_ESCAN0 = 5,
	DHD_PREALLOC_WIPHY_ESCAN1 = 6,
	DHD_PREALLOC_DHD_INFO = 7,
	DHD_PREALLOC_DHD_WLFC_INFO = 8,
	DHD_PREALLOC_IF_FLOW_LKUP = 9,
	DHD_PREALLOC_FLOWRING = 10,
	DHD_PREALLOC_MAX
};

#define STATIC_BUF_MAX_NUM	20
#define STATIC_BUF_SIZE	(PAGE_SIZE*2)

#define DHD_PREALLOC_PROT_SIZE   	(16 * 1024)
#define DHD_PREALLOC_RXBUF_SIZE   	(24 * 1024)
#define DHD_PREALLOC_DATABUF_SIZE   	(64 * 1024)
#define DHD_PREALLOC_OSL_BUF_SIZE      (STATIC_BUF_MAX_NUM * STATIC_BUF_SIZE)
#define DHD_PREALLOC_WIPHY_ESCAN0_SIZE	(64 * 1024)
#define DHD_PREALLOC_DHD_INFO_SIZE		(24 * 1024)
#ifdef CONFIG_64BIT
#define DHD_PREALLOC_IF_FLOW_LKUP_SIZE	(20 * 1024 * 2)
#else
#define DHD_PREALLOC_IF_FLOW_LKUP_SIZE	(20 * 1024)
#endif

#if defined(CONFIG_64BIT)
#define WLAN_DHD_INFO_BUF_SIZE		(24 * 1024)
#define WLAN_DHD_WLFC_BUF_SIZE		(64 * 1024)
#define WLAN_DHD_IF_FLOW_LKUP_SIZE	(64 * 1024)
#else
#define WLAN_DHD_INFO_BUF_SIZE		(16 * 1024)
#define WLAN_DHD_WLFC_BUF_SIZE		(16 * 1024)
#define WLAN_DHD_IF_FLOW_LKUP_SIZE	(20 * 1024)
#endif /* CONFIG_64BIT */
#define WLAN_DHD_MEMDUMP_SIZE		(800 * 1024)

#ifdef CONFIG_BCMDHD_PCIE
#define DHD_SKB_1PAGE_BUFSIZE	(PAGE_SIZE*1)
#define DHD_SKB_2PAGE_BUFSIZE	(PAGE_SIZE*2)
#define DHD_SKB_4PAGE_BUFSIZE	(PAGE_SIZE*4)

#define DHD_SKB_1PAGE_BUF_NUM	0
#define DHD_SKB_2PAGE_BUF_NUM	64
#define DHD_SKB_4PAGE_BUF_NUM	0
#else
#define DHD_SKB_HDRSIZE		336
#define DHD_SKB_1PAGE_BUFSIZE	((PAGE_SIZE*1)-DHD_SKB_HDRSIZE)
#define DHD_SKB_2PAGE_BUFSIZE	((PAGE_SIZE*2)-DHD_SKB_HDRSIZE)
#define DHD_SKB_4PAGE_BUFSIZE	((PAGE_SIZE*4)-DHD_SKB_HDRSIZE)

#define DHD_SKB_1PAGE_BUF_NUM	8
#define DHD_SKB_2PAGE_BUF_NUM	8
#define DHD_SKB_4PAGE_BUF_NUM	1
#endif /* CONFIG_BCMDHD_PCIE */

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

static struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM];

void *dhd_wlan_mem_prealloc(int section, unsigned long size)
{
	printk("%s: sectoin %d, %ld\n", __FUNCTION__, section, size);
	if (section == DHD_PREALLOC_PROT)
		return wlan_static_prot;

	if (section == DHD_PREALLOC_RXBUF)
		return wlan_static_rxbuf;

	if (section == DHD_PREALLOC_DATABUF)
		return wlan_static_databuf;

	if (section == DHD_PREALLOC_SKB_BUF)
		return wlan_static_skb;

	if (section == DHD_PREALLOC_WIPHY_ESCAN0)
		return wlan_static_scan_buf0;

	if (section == DHD_PREALLOC_WIPHY_ESCAN1)
		return wlan_static_scan_buf1;

	if (section == DHD_PREALLOC_OSL_BUF) {
		if (size > DHD_PREALLOC_OSL_BUF_SIZE) {
			pr_err("request OSL_BUF(%lu) is bigger than static size(%ld).\n",
				size, DHD_PREALLOC_OSL_BUF_SIZE);
			return NULL;
		}
		return wlan_static_osl_buf;
	}

	if (section == DHD_PREALLOC_DHD_INFO) {
		if (size > DHD_PREALLOC_DHD_INFO_SIZE) {
			pr_err("request DHD_INFO size(%lu) is bigger than static size(%d).\n",
				size, DHD_PREALLOC_DHD_INFO_SIZE);
			return NULL;
		}
		return wlan_static_dhd_info_buf;
	}
	if (section == DHD_PREALLOC_DHD_WLFC_INFO) {
		if (size > WLAN_DHD_WLFC_BUF_SIZE) {
			pr_err("request DHD_INFO size(%lu) is bigger than static size(%d).\n",
				size, WLAN_DHD_WLFC_BUF_SIZE);
			return NULL;
		}
		return wlan_static_dhd_wlfc_info_buf;
	}
	if (section == DHD_PREALLOC_IF_FLOW_LKUP)  {
		if (size > DHD_PREALLOC_IF_FLOW_LKUP_SIZE) {
			pr_err("request DHD_IF_FLOW_LKUP size(%lu) is bigger than static size(%d).\n",
				size, DHD_PREALLOC_IF_FLOW_LKUP_SIZE);
			return NULL;
		}

		return wlan_static_if_flow_lkup;
	}
	if ((section < 0) || (section > DHD_PREALLOC_MAX))
		pr_err("request section id(%d) is out of max index %d\n",
				section, DHD_PREALLOC_MAX);

	pr_err("%s: failed to alloc section %d, size=%ld\n", __FUNCTION__, section, size);

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
		printk("%s: sectoin %d skb[%d], size=%ld\n", __FUNCTION__, DHD_PREALLOC_SKB_BUF, i, DHD_SKB_1PAGE_BUFSIZE);
	}

	for (i = DHD_SKB_1PAGE_BUF_NUM; i < WLAN_SKB_1_2PAGE_BUF_NUM; i++) {
		wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_2PAGE_BUFSIZE);
		if (!wlan_static_skb[i]) {
			goto err_skb_alloc;
		}
		printk("%s: sectoin %d skb[%d], size=%ld\n", __FUNCTION__, DHD_PREALLOC_SKB_BUF, i, DHD_SKB_2PAGE_BUFSIZE);
	}

#if !defined(CONFIG_BCMDHD_PCIE)
	wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_4PAGE_BUFSIZE);
	if (!wlan_static_skb[i]) {
		goto err_skb_alloc;
	}
#endif /* !CONFIG_BCMDHD_PCIE */

	wlan_static_prot = kmalloc(DHD_PREALLOC_PROT_SIZE, GFP_KERNEL);
	if (!wlan_static_prot) {
		pr_err("Failed to alloc wlan_static_prot\n");
		goto err_mem_alloc;
	}
	printk("%s: sectoin %d, size=%d\n", __FUNCTION__, DHD_PREALLOC_PROT, DHD_PREALLOC_PROT_SIZE);

#if defined(CONFIG_BCMDHD_SDIO)
	wlan_static_rxbuf = kmalloc(DHD_PREALLOC_RXBUF_SIZE, GFP_KERNEL);
	if (!wlan_static_rxbuf) {
		pr_err("Failed to alloc wlan_static_rxbuf\n");
		goto err_mem_alloc;
	}
	printk("%s: sectoin %d, size=%d\n", __FUNCTION__, DHD_PREALLOC_RXBUF, DHD_PREALLOC_RXBUF_SIZE);

	wlan_static_databuf = kmalloc(DHD_PREALLOC_DATABUF_SIZE, GFP_KERNEL);
	if (!wlan_static_databuf) {
		pr_err("Failed to alloc wlan_static_databuf\n");
		goto err_mem_alloc;
	}
	printk("%s: sectoin %d, size=%d\n", __FUNCTION__, DHD_PREALLOC_DATABUF, DHD_PREALLOC_DATABUF_SIZE);
#endif

	wlan_static_osl_buf = kmalloc(DHD_PREALLOC_OSL_BUF_SIZE, GFP_KERNEL);
	if (!wlan_static_osl_buf) {
		pr_err("Failed to alloc wlan_static_osl_buf\n");
		goto err_mem_alloc;
	}
	printk("%s: sectoin %d, size=%ld\n", __FUNCTION__, DHD_PREALLOC_OSL_BUF, DHD_PREALLOC_OSL_BUF_SIZE);

	wlan_static_scan_buf0 = kmalloc(DHD_PREALLOC_WIPHY_ESCAN0_SIZE, GFP_KERNEL);
	if (!wlan_static_scan_buf0) {
		pr_err("Failed to alloc wlan_static_scan_buf0\n");
		goto err_mem_alloc;
	}
	printk("%s: sectoin %d, size=%d\n", __FUNCTION__, DHD_PREALLOC_WIPHY_ESCAN0, DHD_PREALLOC_WIPHY_ESCAN0_SIZE);

	wlan_static_dhd_info_buf = kmalloc(DHD_PREALLOC_DHD_INFO_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_info_buf) {
		pr_err("Failed to alloc wlan_static_dhd_info_buf\n");
		goto err_mem_alloc;
	}
	printk("%s: sectoin %d, size=%d\n", __FUNCTION__, DHD_PREALLOC_DHD_INFO, DHD_PREALLOC_DHD_INFO_SIZE);

	wlan_static_dhd_wlfc_info_buf = kmalloc(WLAN_DHD_WLFC_BUF_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_wlfc_info_buf) {
		pr_err("Failed to alloc wlan_static_dhd_wlfc_info_buf\n");
		goto err_mem_alloc;
	}
	printk("%s: sectoin %d, size=%d\n", __FUNCTION__, DHD_PREALLOC_DHD_WLFC_INFO, WLAN_DHD_WLFC_BUF_SIZE);

#ifdef CONFIG_BCMDHD_PCIE
	wlan_static_if_flow_lkup = kmalloc(DHD_PREALLOC_IF_FLOW_LKUP_SIZE, GFP_KERNEL);
	if (!wlan_static_if_flow_lkup) {
		pr_err("Failed to alloc wlan_static_if_flow_lkup\n");
		goto err_mem_alloc;
	}
#endif /* CONFIG_BCMDHD_PCIE */

	return 0;

err_mem_alloc:

	if (wlan_static_prot)
		kfree(wlan_static_prot);

#if defined(CONFIG_BCMDHD_SDIO)
	if (wlan_static_rxbuf)
		kfree(wlan_static_rxbuf);

	if (wlan_static_databuf)
		kfree(wlan_static_databuf);
#endif

	if (wlan_static_dhd_info_buf)
		kfree(wlan_static_dhd_info_buf);

	if (wlan_static_dhd_wlfc_info_buf)
		kfree(wlan_static_dhd_wlfc_info_buf);

	if (wlan_static_scan_buf1)
		kfree(wlan_static_scan_buf1);

	if (wlan_static_scan_buf0)
		kfree(wlan_static_scan_buf0);

	if (wlan_static_osl_buf)
		kfree(wlan_static_osl_buf);

#ifdef CONFIG_BCMDHD_PCIE
	if (wlan_static_if_flow_lkup)
		kfree(wlan_static_if_flow_lkup);
#endif
	pr_err("Failed to mem_alloc for WLAN\n");

	i = WLAN_SKB_BUF_NUM;

err_skb_alloc:
	pr_err("Failed to skb_alloc for WLAN\n");
	for (j = 0; j < i; j++) {
		dev_kfree_skb(wlan_static_skb[j]);
	}

	return -ENOMEM;
}

static int __init
dhd_static_buf_init(void)
{
	printk(KERN_ERR "%s()\n", __FUNCTION__);

	dhd_init_wlan_mem();

	return 0;
}

static void __exit
dhd_static_buf_exit(void)
{
	int i;

	printk(KERN_ERR "%s()\n", __FUNCTION__);

	for (i = 0; i < DHD_SKB_1PAGE_BUF_NUM; i++) {
		if (wlan_static_skb[i])
			dev_kfree_skb(wlan_static_skb[i]);
	}

	for (i = DHD_SKB_1PAGE_BUF_NUM; i < WLAN_SKB_1_2PAGE_BUF_NUM; i++) {
		if (wlan_static_skb[i])
			dev_kfree_skb(wlan_static_skb[i]);
	}

#if !defined(CONFIG_BCMDHD_PCIE)
	if (wlan_static_skb[i])
		dev_kfree_skb(wlan_static_skb[i]);
#endif /* !CONFIG_BCMDHD_PCIE */

	if (wlan_static_prot)
		kfree(wlan_static_prot);

#if defined(CONFIG_BCMDHD_SDIO)
	if (wlan_static_rxbuf)
		kfree(wlan_static_rxbuf);

	if (wlan_static_databuf)
		kfree(wlan_static_databuf);
#endif

	if (wlan_static_osl_buf)
		kfree(wlan_static_osl_buf);

	if (wlan_static_scan_buf0)
		kfree(wlan_static_scan_buf0);

	if (wlan_static_dhd_info_buf)
		kfree(wlan_static_dhd_info_buf);

	if (wlan_static_dhd_wlfc_info_buf)
		kfree(wlan_static_dhd_wlfc_info_buf);

	if (wlan_static_scan_buf1)
		kfree(wlan_static_scan_buf1);

#ifdef CONFIG_BCMDHD_PCIE
	if (wlan_static_if_flow_lkup)
		kfree(wlan_static_if_flow_lkup);
#endif
	return;
}

module_init(dhd_static_buf_init);

module_exit(dhd_static_buf_exit);
