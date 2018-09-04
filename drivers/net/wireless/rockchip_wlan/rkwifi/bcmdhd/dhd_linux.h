/*
 * DHD Linux header file (dhd_linux exports for cfg80211 and other components)
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_linux.h 699532 2017-05-15 11:00:39Z $
 */

/* wifi platform functions for power, interrupt and pre-alloc, either
 * from Android-like platform device data, or Broadcom wifi platform
 * device data.
 *
 */
#ifndef __DHD_LINUX_H__
#define __DHD_LINUX_H__

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <dngl_stats.h>
#include <dhd.h>
#ifdef DHD_WMF
#include <dhd_wmf_linux.h>
#endif
/* Linux wireless extension support */
#if defined(WL_WIRELESS_EXT)
#include <wl_iw.h>
#endif /* defined(WL_WIRELESS_EXT) */
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif /* defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND) */

/* dongle status */
enum wifi_adapter_status {
	WIFI_STATUS_POWER_ON = 0,
	WIFI_STATUS_ATTACH,
	WIFI_STATUS_FW_READY,
	WIFI_STATUS_DETTACH
};
#define wifi_chk_adapter_status(adapter, stat) (test_bit(stat, &(adapter)->status))
#define wifi_get_adapter_status(adapter, stat) (test_bit(stat, &(adapter)->status))
#define wifi_set_adapter_status(adapter, stat) (set_bit(stat, &(adapter)->status))
#define wifi_clr_adapter_status(adapter, stat) (clear_bit(stat, &(adapter)->status))
#define wifi_chg_adapter_status(adapter, stat) (change_bit(stat, &(adapter)->status))

#define DHD_REGISTRATION_TIMEOUT  12000  /* msec : allowed time to finished dhd registration */
#define DHD_FW_READY_TIMEOUT  5000  /* msec : allowed time to finished fw download */

typedef struct wifi_adapter_info {
	const char	*name;
	uint		irq_num;
	uint		intr_flags;
	const char	*fw_path;
	const char	*nv_path;
	const char	*clm_path;
	const char	*conf_path;
	void		*wifi_plat_data;	/* wifi ctrl func, for backward compatibility */
	uint		bus_type;
	uint		bus_num;
	uint		slot_num;
	wait_queue_head_t status_event;
	unsigned long status;
#if defined(BT_OVER_SDIO)
	const char	*btfw_path;
#endif /* defined (BT_OVER_SDIO) */
#ifdef BUS_POWER_RESTORE
#if defined(BCMSDIO)
	struct sdio_func *sdio_func;
#endif /* BCMSDIO */
#if defined(BCMPCIE)
	struct pci_dev *pci_dev;
	struct pci_saved_state *pci_saved_state;
#endif /* BCMPCIE */
#endif
} wifi_adapter_info_t;

#define WLAN_PLAT_NODFS_FLAG	0x01
#define WLAN_PLAT_AP_FLAG	0x02
struct wifi_platform_data {
#ifdef BUS_POWER_RESTORE
	int (*set_power)(int val, wifi_adapter_info_t *adapter);
#else
	int (*set_power)(int val);
#endif
	int (*set_reset)(int val);
	int (*set_carddetect)(int val);
	void *(*mem_prealloc)(int section, unsigned long size);
	int (*get_mac_addr)(unsigned char *buf);
#if defined(CUSTOM_COUNTRY_CODE)
	void *(*get_country_code)(char *ccode, u32 flags);
#else /* defined (CUSTOM_COUNTRY_CODE) */
	void *(*get_country_code)(char *ccode);
#endif
};

typedef struct bcmdhd_wifi_platdata {
	uint				num_adapters;
	wifi_adapter_info_t	*adapters;
} bcmdhd_wifi_platdata_t;

/** Per STA params. A list of dhd_sta objects are managed in dhd_if */
typedef struct dhd_sta {
	cumm_ctr_t cumm_ctr;    /* cummulative queue length of child flowrings */
	uint16 flowid[NUMPRIO]; /* allocated flow ring ids (by priority) */
	void * ifp;             /* associated dhd_if */
	struct ether_addr ea;   /* stations ethernet mac address */
	struct list_head list;  /* link into dhd_if::sta_list */
	int idx;                /* index of self in dhd_pub::sta_pool[] */
	int ifidx;              /* index of interface in dhd */
#ifdef DHD_WMF
	struct dhd_sta *psta_prim; /* primary index of psta interface */
#endif /* DHD_WMF */
} dhd_sta_t;
typedef dhd_sta_t dhd_sta_pool_t;

int dhd_wifi_platform_register_drv(void);
void dhd_wifi_platform_unregister_drv(void);
wifi_adapter_info_t* dhd_wifi_platform_attach_adapter(uint32 bus_type,
	uint32 bus_num, uint32 slot_num, unsigned long status);
wifi_adapter_info_t* dhd_wifi_platform_get_adapter(uint32 bus_type, uint32 bus_num,
	uint32 slot_num);
int wifi_platform_set_power(wifi_adapter_info_t *adapter, bool on, unsigned long msec);
int wifi_platform_bus_enumerate(wifi_adapter_info_t *adapter, bool device_present);
int wifi_platform_get_irq_number(wifi_adapter_info_t *adapter, unsigned long *irq_flags_ptr);
int wifi_platform_get_mac_addr(wifi_adapter_info_t *adapter, unsigned char *buf);
#ifdef CUSTOM_COUNTRY_CODE
void *wifi_platform_get_country_code(wifi_adapter_info_t *adapter, char *ccode,
	u32 flags);
#else
void *wifi_platform_get_country_code(wifi_adapter_info_t *adapter, char *ccode);
#endif /* CUSTOM_COUNTRY_CODE */
void* wifi_platform_prealloc(wifi_adapter_info_t *adapter, int section, unsigned long size);
void* wifi_platform_get_prealloc_func_ptr(wifi_adapter_info_t *adapter);

int dhd_get_fw_mode(struct dhd_info *dhdinfo);
bool dhd_update_fw_nv_path(struct dhd_info *dhdinfo);

#ifdef DHD_WMF
dhd_wmf_t* dhd_wmf_conf(dhd_pub_t *dhdp, uint32 idx);
int dhd_get_wmf_psta_disable(dhd_pub_t *dhdp, uint32 idx);
int dhd_set_wmf_psta_disable(dhd_pub_t *dhdp, uint32 idx, int val);
void dhd_update_psta_interface_for_sta(dhd_pub_t *dhdp, char* ifname,
		void* mac_addr, void* event_data);
#endif /* DHD_WMF */
#if defined(BT_OVER_SDIO)
int dhd_net_bus_get(struct net_device *dev);
int dhd_net_bus_put(struct net_device *dev);
#endif /* BT_OVER_SDIO */
#ifdef HOFFLOAD_MODULES
extern void dhd_free_module_memory(struct dhd_bus *bus, struct module_metadata *hmem);
extern void* dhd_alloc_module_memory(struct dhd_bus *bus, uint32_t size,
	struct module_metadata *hmem);
#endif /* HOFFLOAD_MODULES */
#if defined(WLADPS) || defined(WLADPS_PRIVATE_CMD)
#define ADPS_ENABLE	1
#define ADPS_DISABLE	0
typedef struct bcm_iov_buf {
	uint16 version;
	uint16 len;
	uint16 id;
	uint16 data[1];
} bcm_iov_buf_t;

int dhd_enable_adps(dhd_pub_t *dhd, uint8 on);
#endif /* WLADPS || WLADPS_PRIVATE_CMD */
#endif /* __DHD_LINUX_H__ */
