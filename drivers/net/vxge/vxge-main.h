/******************************************************************************
 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 *
 * vxge-main.h: Driver for Neterion Inc's X3100 Series 10GbE PCIe I/O
 *              Virtualized Server Adapter.
 * Copyright(c) 2002-2009 Neterion Inc.
 ******************************************************************************/
#ifndef VXGE_MAIN_H
#define VXGE_MAIN_H

#include "vxge-traffic.h"
#include "vxge-config.h"
#include "vxge-version.h"
#include <linux/list.h>

#define VXGE_DRIVER_NAME		"vxge"
#define VXGE_DRIVER_VENDOR		"Neterion, Inc"
#define VXGE_DRIVER_FW_VERSION_MAJOR	1

#define DRV_VERSION	VXGE_VERSION_MAJOR"."VXGE_VERSION_MINOR"."\
	VXGE_VERSION_FIX"."VXGE_VERSION_BUILD"-"\
	VXGE_VERSION_FOR

#define PCI_DEVICE_ID_TITAN_WIN		0x5733
#define PCI_DEVICE_ID_TITAN_UNI		0x5833
#define	VXGE_USE_DEFAULT		0xffffffff
#define VXGE_HW_VPATH_MSIX_ACTIVE	4
#define VXGE_HW_RXSYNC_FREQ_CNT		4
#define VXGE_LL_WATCH_DOG_TIMEOUT	(15 * HZ)
#define VXGE_LL_RX_COPY_THRESHOLD	256
#define VXGE_DEF_FIFO_LENGTH		84

#define NO_STEERING		0
#define PORT_STEERING		0x1
#define RTH_STEERING		0x2
#define RX_TOS_STEERING		0x3
#define RX_VLAN_STEERING	0x4
#define RTH_BUCKET_SIZE		4

#define	TX_PRIORITY_STEERING	1
#define	TX_VLAN_STEERING	2
#define	TX_PORT_STEERING	3
#define	TX_MULTIQ_STEERING	4

#define VXGE_HW_MAC_ADDR_LEARN_DEFAULT VXGE_HW_RTS_MAC_DISABLE

#define VXGE_TTI_BTIMER_VAL 250000

#define VXGE_TTI_LTIMER_VAL 1000
#define VXGE_TTI_RTIMER_VAL 0
#define VXGE_RTI_BTIMER_VAL 250
#define VXGE_RTI_LTIMER_VAL 100
#define VXGE_RTI_RTIMER_VAL 0
#define VXGE_FIFO_INDICATE_MAX_PKTS VXGE_DEF_FIFO_LENGTH
#define VXGE_ISR_POLLING_CNT 	8
#define VXGE_MAX_CONFIG_DEV	0xFF
#define VXGE_EXEC_MODE_DISABLE	0
#define VXGE_EXEC_MODE_ENABLE	1
#define VXGE_MAX_CONFIG_PORT	1
#define VXGE_ALL_VID_DISABLE	0
#define VXGE_ALL_VID_ENABLE	1
#define VXGE_PAUSE_CTRL_DISABLE	0
#define VXGE_PAUSE_CTRL_ENABLE	1

#define TTI_TX_URANGE_A	5
#define TTI_TX_URANGE_B	15
#define TTI_TX_URANGE_C	40
#define TTI_TX_UFC_A	5
#define TTI_TX_UFC_B	40
#define TTI_TX_UFC_C	60
#define TTI_TX_UFC_D	100

#define RTI_RX_URANGE_A	5
#define RTI_RX_URANGE_B	15
#define RTI_RX_URANGE_C	40
#define RTI_RX_UFC_A	1
#define RTI_RX_UFC_B	5
#define RTI_RX_UFC_C	10
#define RTI_RX_UFC_D	15

/* Milli secs timer period */
#define VXGE_TIMER_DELAY		10000

#define VXGE_LL_MAX_FRAME_SIZE(dev) ((dev)->mtu + VXGE_HW_MAC_HEADER_MAX_SIZE)

enum vxge_reset_event {
	/* reset events */
	VXGE_LL_VPATH_RESET	= 0,
	VXGE_LL_DEVICE_RESET	= 1,
	VXGE_LL_FULL_RESET	= 2,
	VXGE_LL_START_RESET	= 3,
	VXGE_LL_COMPL_RESET	= 4
};
/* These flags represent the devices temporary state */
enum vxge_device_state_t {
__VXGE_STATE_RESET_CARD = 0,
__VXGE_STATE_CARD_UP
};

enum vxge_mac_addr_state {
	/* mac address states */
	VXGE_LL_MAC_ADDR_IN_LIST        = 0,
	VXGE_LL_MAC_ADDR_IN_DA_TABLE    = 1
};

struct vxge_drv_config {
	int config_dev_cnt;
	int total_dev_cnt;
	int g_no_cpus;
	unsigned int vpath_per_dev;
};

struct macInfo {
	unsigned char macaddr[ETH_ALEN];
	unsigned char macmask[ETH_ALEN];
	unsigned int vpath_no;
	enum vxge_mac_addr_state state;
};

struct vxge_config {
	int		tx_pause_enable;
	int		rx_pause_enable;

#define	NEW_NAPI_WEIGHT	64
	int		napi_weight;
#define VXGE_GRO_DONOT_AGGREGATE		0
#define VXGE_GRO_ALWAYS_AGGREGATE		1
	int		gro_enable;
	int		intr_type;
#define INTA	0
#define MSI	1
#define MSI_X	2

	int		addr_learn_en;

	int		rth_steering;
	int		rth_algorithm;
	int		rth_hash_type_tcpipv4;
	int		rth_hash_type_ipv4;
	int		rth_hash_type_tcpipv6;
	int		rth_hash_type_ipv6;
	int		rth_hash_type_tcpipv6ex;
	int		rth_hash_type_ipv6ex;
	int		rth_bkt_sz;
	int		rth_jhash_golden_ratio;
	int		tx_steering_type;
	int 	fifo_indicate_max_pkts;
	struct vxge_hw_device_hw_info device_hw_info;
};

struct vxge_msix_entry {
	/* Mimicing the msix_entry struct of Kernel. */
	u16 vector;
	u16 entry;
	u16 in_use;
	void *arg;
};

/* Software Statistics */

struct vxge_sw_stats {
	/* Network Stats (interface stats) */
	struct net_device_stats net_stats;

	/* Tx */
	u64 tx_frms;
	u64 tx_errors;
	u64 tx_bytes;
	u64 txd_not_free;
	u64 txd_out_of_desc;

	/* Virtual Path */
	u64 vpaths_open;
	u64 vpath_open_fail;

	/* Rx */
	u64 rx_frms;
	u64 rx_errors;
	u64 rx_bytes;
	u64 rx_mcast;

	/* Misc. */
	u64 link_up;
	u64 link_down;
	u64 pci_map_fail;
	u64 skb_alloc_fail;
};

struct vxge_mac_addrs {
	struct list_head item;
	u64 macaddr;
	u64 macmask;
	enum vxge_mac_addr_state state;
};

struct vxgedev;

struct vxge_fifo_stats {
	u64 tx_frms;
	u64 tx_errors;
	u64 tx_bytes;
	u64 txd_not_free;
	u64 txd_out_of_desc;
	u64 pci_map_fail;
};

struct vxge_fifo {
	struct net_device	*ndev;
	struct pci_dev		*pdev;
	struct __vxge_hw_fifo *handle;

	/* The vpath id maintained in the driver -
	 * 0 to 'maximum_vpaths_in_function - 1'
	 */
	int driver_id;
	int tx_steering_type;
	int indicate_max_pkts;
	spinlock_t tx_lock;
	/* flag used to maintain queue state when MULTIQ is not enabled */
#define VPATH_QUEUE_START       0
#define VPATH_QUEUE_STOP        1
	int queue_state;

	/* Tx stats */
	struct vxge_fifo_stats stats;
} ____cacheline_aligned;

struct vxge_ring_stats {
	u64 prev_rx_frms;
	u64 rx_frms;
	u64 rx_errors;
	u64 rx_dropped;
	u64 rx_bytes;
	u64 rx_mcast;
	u64 pci_map_fail;
	u64 skb_alloc_fail;
};

struct vxge_ring {
	struct net_device	*ndev;
	struct pci_dev		*pdev;
	struct __vxge_hw_ring	*handle;
	/* The vpath id maintained in the driver -
	 * 0 to 'maximum_vpaths_in_function - 1'
	 */
	int driver_id;

	 /* copy of the flag indicating whether rx_csum is to be used */
	u32 rx_csum;

	int pkts_processed;
	int budget;
	int gro_enable;

	struct napi_struct napi;
	struct napi_struct *napi_p;

#define VXGE_MAX_MAC_ADDR_COUNT		30

	int vlan_tag_strip;
	struct vlan_group *vlgrp;
	int rx_vector_no;
	enum vxge_hw_status last_status;

	/* Rx stats */
	struct vxge_ring_stats stats;
} ____cacheline_aligned;

struct vxge_vpath {

	struct vxge_fifo fifo;
	struct vxge_ring ring;

	struct __vxge_hw_vpath_handle *handle;

	/* Actual vpath id for this vpath in the device - 0 to 16 */
	int device_id;
	int max_mac_addr_cnt;
	int is_configured;
	int is_open;
	struct vxgedev *vdev;
	u8 (macaddr)[ETH_ALEN];
	u8 (macmask)[ETH_ALEN];

#define VXGE_MAX_LEARN_MAC_ADDR_CNT	2048
	/* mac addresses currently programmed into NIC */
	u16 mac_addr_cnt;
	u16 mcast_addr_cnt;
	struct list_head mac_addr_list;

	u32 level_err;
	u32 level_trace;
};
#define VXGE_COPY_DEBUG_INFO_TO_LL(vdev, err, trace) {	\
	for (i = 0; i < vdev->no_of_vpath; i++) {		\
		vdev->vpaths[i].level_err = err;		\
		vdev->vpaths[i].level_trace = trace;		\
	}							\
	vdev->level_err = err;					\
	vdev->level_trace = trace;				\
}

struct vxgedev {
	struct net_device	*ndev;
	struct pci_dev		*pdev;
	struct __vxge_hw_device *devh;
	struct vlan_group	*vlgrp;
	int vlan_tag_strip;
	struct vxge_config	config;
	unsigned long	state;

	/* Indicates which vpath to reset */
	unsigned long  vp_reset;

	/* Timer used for polling vpath resets */
	struct timer_list vp_reset_timer;

	/* Timer used for polling vpath lockup */
	struct timer_list vp_lockup_timer;

	/*
	 * Flags to track whether device is in All Multicast
	 * or in promiscuous mode.
	 */
	u16		all_multi_flg;

	 /* A flag indicating whether rx_csum is to be used or not. */
	u32	rx_csum;

	struct vxge_msix_entry *vxge_entries;
	struct msix_entry *entries;
	/*
	 * 4 for each vpath * 17;
	 * total is 68
	 */
#define	VXGE_MAX_REQUESTED_MSIX	68
#define VXGE_INTR_STRLEN 80
	char desc[VXGE_MAX_REQUESTED_MSIX][VXGE_INTR_STRLEN];

	enum vxge_hw_event cric_err_event;

	int max_vpath_supported;
	int no_of_vpath;

	struct napi_struct napi;
	/* A debug option, when enabled and if error condition occurs,
	 * the driver will do following steps:
	 * - mask all interrupts
	 * - Not clear the source of the alarm
	 * - gracefully stop all I/O
	 * A diagnostic dump of register and stats at this point
	 * reveals very useful information.
	 */
	int exec_mode;
	int max_config_port;
	struct vxge_vpath	*vpaths;

	struct __vxge_hw_vpath_handle *vp_handles[VXGE_HW_MAX_VIRTUAL_PATHS];
	void __iomem *bar0;
	struct vxge_sw_stats	stats;
	int		mtu;
	/* Below variables are used for vpath selection to transmit a packet */
	u8 		vpath_selector[VXGE_HW_MAX_VIRTUAL_PATHS];
	u64		vpaths_deployed;

	u32 		intr_cnt;
	u32 		level_err;
	u32 		level_trace;
	char		fw_version[VXGE_HW_FW_STRLEN];
};

struct vxge_rx_priv {
	struct sk_buff		*skb;
	unsigned char		*skb_data;
	dma_addr_t		data_dma;
	dma_addr_t		data_size;
};

struct vxge_tx_priv {
	struct sk_buff		*skb;
	dma_addr_t		dma_buffers[MAX_SKB_FRAGS+1];
};

#define VXGE_MODULE_PARAM_INT(p, val) \
	static int p = val; \
	module_param(p, int, 0)

#define vxge_os_bug(fmt...)		{ printk(fmt); BUG(); }

#define vxge_os_timer(timer, handle, arg, exp) do { \
		init_timer(&timer); \
		timer.function = handle; \
		timer.data = (unsigned long) arg; \
		mod_timer(&timer, (jiffies + exp)); \
	} while (0);

int __devinit vxge_device_register(struct __vxge_hw_device *devh,
				    struct vxge_config *config,
				    int high_dma, int no_of_vpath,
				    struct vxgedev **vdev);

void vxge_device_unregister(struct __vxge_hw_device *devh);

void vxge_vpath_intr_enable(struct vxgedev *vdev, int vp_id);

void vxge_vpath_intr_disable(struct vxgedev *vdev, int vp_id);

void vxge_callback_link_up(struct __vxge_hw_device *devh);

void vxge_callback_link_down(struct __vxge_hw_device *devh);

enum vxge_hw_status vxge_add_mac_addr(struct vxgedev *vdev,
	struct macInfo *mac);

int vxge_mac_list_del(struct vxge_vpath *vpath, struct macInfo *mac);

int vxge_reset(struct vxgedev *vdev);

enum vxge_hw_status
vxge_rx_1b_compl(struct __vxge_hw_ring *ringh, void *dtr,
	u8 t_code, void *userdata);

enum vxge_hw_status
vxge_xmit_compl(struct __vxge_hw_fifo *fifo_hw, void *dtr,
	enum vxge_hw_fifo_tcode t_code, void *userdata,
	struct sk_buff ***skb_ptr, int nr_skbs, int *more);

int vxge_close(struct net_device *dev);

int vxge_open(struct net_device *dev);

void vxge_close_vpaths(struct vxgedev *vdev, int index);

int vxge_open_vpaths(struct vxgedev *vdev);

enum vxge_hw_status vxge_reset_all_vpaths(struct vxgedev *vdev);

void vxge_stop_all_tx_queue(struct vxgedev *vdev);

void vxge_stop_tx_queue(struct vxge_fifo *fifo);

void vxge_start_all_tx_queue(struct vxgedev *vdev);

void vxge_wake_tx_queue(struct vxge_fifo *fifo, struct sk_buff *skb);

enum vxge_hw_status vxge_add_mac_addr(struct vxgedev *vdev,
	struct macInfo *mac);

enum vxge_hw_status vxge_del_mac_addr(struct vxgedev *vdev,
	struct macInfo *mac);

int vxge_mac_list_add(struct vxge_vpath *vpath,
	struct macInfo *mac);

void vxge_free_mac_add_list(struct vxge_vpath *vpath);

enum vxge_hw_status vxge_restore_vpath_mac_addr(struct vxge_vpath *vpath);

enum vxge_hw_status vxge_restore_vpath_vid_table(struct vxge_vpath *vpath);

int do_vxge_close(struct net_device *dev, int do_io);
extern void initialize_ethtool_ops(struct net_device *ndev);
/**
 * #define VXGE_DEBUG_INIT: debug for initialization functions
 * #define VXGE_DEBUG_TX	 : debug transmit related functions
 * #define VXGE_DEBUG_RX  : debug recevice related functions
 * #define VXGE_DEBUG_MEM : debug memory module
 * #define VXGE_DEBUG_LOCK: debug locks
 * #define VXGE_DEBUG_SEM : debug semaphore
 * #define VXGE_DEBUG_ENTRYEXIT: debug functions by adding entry exit statements
*/
#define VXGE_DEBUG_INIT		0x00000001
#define VXGE_DEBUG_TX		0x00000002
#define VXGE_DEBUG_RX		0x00000004
#define VXGE_DEBUG_MEM		0x00000008
#define VXGE_DEBUG_LOCK		0x00000010
#define VXGE_DEBUG_SEM		0x00000020
#define VXGE_DEBUG_ENTRYEXIT	0x00000040
#define VXGE_DEBUG_INTR		0x00000080
#define VXGE_DEBUG_LL_CONFIG	0x00000100

/* Debug tracing for VXGE driver */
#ifndef VXGE_DEBUG_MASK
#define VXGE_DEBUG_MASK	0x0
#endif

#if (VXGE_DEBUG_LL_CONFIG & VXGE_DEBUG_MASK)
#define vxge_debug_ll_config(level, fmt, ...) \
	vxge_debug_ll(level, VXGE_DEBUG_LL_CONFIG, fmt, __VA_ARGS__)
#else
#define vxge_debug_ll_config(level, fmt, ...)
#endif

#if (VXGE_DEBUG_INIT & VXGE_DEBUG_MASK)
#define vxge_debug_init(level, fmt, ...) \
	vxge_debug_ll(level, VXGE_DEBUG_INIT, fmt, __VA_ARGS__)
#else
#define vxge_debug_init(level, fmt, ...)
#endif

#if (VXGE_DEBUG_TX & VXGE_DEBUG_MASK)
#define vxge_debug_tx(level, fmt, ...) \
	vxge_debug_ll(level, VXGE_DEBUG_TX, fmt, __VA_ARGS__)
#else
#define vxge_debug_tx(level, fmt, ...)
#endif

#if (VXGE_DEBUG_RX & VXGE_DEBUG_MASK)
#define vxge_debug_rx(level, fmt, ...) \
	vxge_debug_ll(level, VXGE_DEBUG_RX, fmt, __VA_ARGS__)
#else
#define vxge_debug_rx(level, fmt, ...)
#endif

#if (VXGE_DEBUG_MEM & VXGE_DEBUG_MASK)
#define vxge_debug_mem(level, fmt, ...) \
	vxge_debug_ll(level, VXGE_DEBUG_MEM, fmt, __VA_ARGS__)
#else
#define vxge_debug_mem(level, fmt, ...)
#endif

#if (VXGE_DEBUG_ENTRYEXIT & VXGE_DEBUG_MASK)
#define vxge_debug_entryexit(level, fmt, ...) \
	vxge_debug_ll(level, VXGE_DEBUG_ENTRYEXIT, fmt, __VA_ARGS__)
#else
#define vxge_debug_entryexit(level, fmt, ...)
#endif

#if (VXGE_DEBUG_INTR & VXGE_DEBUG_MASK)
#define vxge_debug_intr(level, fmt, ...) \
	vxge_debug_ll(level, VXGE_DEBUG_INTR, fmt, __VA_ARGS__)
#else
#define vxge_debug_intr(level, fmt, ...)
#endif

#define VXGE_DEVICE_DEBUG_LEVEL_SET(level, mask, vdev) {\
	vxge_hw_device_debug_set((struct __vxge_hw_device  *)vdev->devh, \
		level, mask);\
	VXGE_COPY_DEBUG_INFO_TO_LL(vdev, \
		vxge_hw_device_error_level_get((struct __vxge_hw_device  *) \
			vdev->devh), \
		vxge_hw_device_trace_level_get((struct __vxge_hw_device  *) \
			vdev->devh));\
}

#ifdef NETIF_F_GSO
#define vxge_tcp_mss(skb) (skb_shinfo(skb)->gso_size)
#define vxge_udp_mss(skb) (skb_shinfo(skb)->gso_size)
#define vxge_offload_type(skb) (skb_shinfo(skb)->gso_type)
#endif

#endif
