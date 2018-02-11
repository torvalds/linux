/******************************************************************************
 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 *
 * vxge-main.h: Driver for Exar Corp's X3100 Series 10GbE PCIe I/O
 *              Virtualized Server Adapter.
 * Copyright(c) 2002-2010 Exar Corp.
 ******************************************************************************/
#ifndef VXGE_MAIN_H
#define VXGE_MAIN_H

#include "vxge-traffic.h"
#include "vxge-config.h"
#include "vxge-version.h"
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/if_vlan.h>

#define VXGE_DRIVER_NAME		"vxge"
#define VXGE_DRIVER_VENDOR		"Neterion, Inc"
#define VXGE_DRIVER_FW_VERSION_MAJOR	1

#define DRV_VERSION	VXGE_VERSION_MAJOR"."VXGE_VERSION_MINOR"."\
	VXGE_VERSION_FIX"."VXGE_VERSION_BUILD"-"\
	VXGE_VERSION_FOR

#define PCI_DEVICE_ID_TITAN_WIN		0x5733
#define PCI_DEVICE_ID_TITAN_UNI		0x5833
#define VXGE_HW_TITAN1_PCI_REVISION	1
#define VXGE_HW_TITAN1A_PCI_REVISION	2

#define	VXGE_USE_DEFAULT		0xffffffff
#define VXGE_HW_VPATH_MSIX_ACTIVE	4
#define VXGE_ALARM_MSIX_ID		2
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

#define VXGE_TTI_LTIMER_VAL	1000
#define VXGE_T1A_TTI_LTIMER_VAL	80
#define VXGE_TTI_RTIMER_VAL	0
#define VXGE_TTI_RTIMER_ADAPT_VAL	10
#define VXGE_T1A_TTI_RTIMER_VAL	400
#define VXGE_RTI_BTIMER_VAL	250
#define VXGE_RTI_LTIMER_VAL	100
#define VXGE_RTI_RTIMER_VAL	0
#define VXGE_RTI_RTIMER_ADAPT_VAL	15
#define VXGE_FIFO_INDICATE_MAX_PKTS	VXGE_DEF_FIFO_LENGTH
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
#define TTI_T1A_TX_UFC_A	30
#define TTI_T1A_TX_UFC_B	80
/* Slope - (max_mtu - min_mtu)/(max_mtu_ufc - min_mtu_ufc) */
/* Slope - 93 */
/* 60 - 9k Mtu, 140 - 1.5k mtu */
#define TTI_T1A_TX_UFC_C(mtu)	(60 + ((VXGE_HW_MAX_MTU - mtu) / 93))

/* Slope - 37 */
/* 100 - 9k Mtu, 300 - 1.5k mtu */
#define TTI_T1A_TX_UFC_D(mtu)	(100 + ((VXGE_HW_MAX_MTU - mtu) / 37))


#define RTI_RX_URANGE_A		5
#define RTI_RX_URANGE_B		15
#define RTI_RX_URANGE_C		40
#define RTI_T1A_RX_URANGE_A	1
#define RTI_T1A_RX_URANGE_B	20
#define RTI_T1A_RX_URANGE_C	50
#define RTI_RX_UFC_A		1
#define RTI_RX_UFC_B		5
#define RTI_RX_UFC_C		10
#define RTI_RX_UFC_D		15
#define RTI_T1A_RX_UFC_B	20
#define RTI_T1A_RX_UFC_C	50
#define RTI_T1A_RX_UFC_D	60

/*
 * The interrupt rate is maintained at 3k per second with the moderation
 * parameters for most traffic but not all. This is the maximum interrupt
 * count allowed per function with INTA or per vector in the case of
 * MSI-X in a 10 millisecond time period. Enabled only for Titan 1A.
 */
#define VXGE_T1A_MAX_INTERRUPT_COUNT	100
#define VXGE_T1A_MAX_TX_INTERRUPT_COUNT	200

/* Milli secs timer period */
#define VXGE_TIMER_DELAY		10000

#define VXGE_LL_MAX_FRAME_SIZE(dev) ((dev)->mtu + VXGE_HW_MAC_HEADER_MAX_SIZE)

#define is_sriov(function_mode) \
	((function_mode == VXGE_HW_FUNCTION_MODE_SRIOV) || \
	(function_mode == VXGE_HW_FUNCTION_MODE_SRIOV_8) || \
	(function_mode == VXGE_HW_FUNCTION_MODE_SRIOV_4))

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
	int		intr_type;
#define INTA	0
#define MSI	1
#define MSI_X	2

	int		addr_learn_en;

	u32		rth_steering:2,
			rth_algorithm:2,
			rth_hash_type_tcpipv4:1,
			rth_hash_type_ipv4:1,
			rth_hash_type_tcpipv6:1,
			rth_hash_type_ipv6:1,
			rth_hash_type_tcpipv6ex:1,
			rth_hash_type_ipv6ex:1,
			rth_bkt_sz:8;
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

	/* Virtual Path */
	unsigned long vpaths_open;
	unsigned long vpath_open_fail;

	/* Misc. */
	unsigned long link_up;
	unsigned long link_down;
};

struct vxge_mac_addrs {
	struct list_head item;
	u64 macaddr;
	u64 macmask;
	enum vxge_mac_addr_state state;
};

struct vxgedev;

struct vxge_fifo_stats {
	struct u64_stats_sync	syncp;
	u64 tx_frms;
	u64 tx_bytes;

	unsigned long tx_errors;
	unsigned long txd_not_free;
	unsigned long txd_out_of_desc;
	unsigned long pci_map_fail;
};

struct vxge_fifo {
	struct net_device *ndev;
	struct pci_dev *pdev;
	struct __vxge_hw_fifo *handle;
	struct netdev_queue *txq;

	int tx_steering_type;
	int indicate_max_pkts;

	/* Adaptive interrupt moderation parameters used in T1A */
	unsigned long interrupt_count;
	unsigned long jiffies;

	u32 tx_vector_no;
	/* Tx stats */
	struct vxge_fifo_stats stats;
} ____cacheline_aligned;

struct vxge_ring_stats {
	struct u64_stats_sync syncp;
	u64 rx_frms;
	u64 rx_mcast;
	u64 rx_bytes;

	unsigned long rx_errors;
	unsigned long rx_dropped;
	unsigned long prev_rx_frms;
	unsigned long pci_map_fail;
	unsigned long skb_alloc_fail;
};

struct vxge_ring {
	struct net_device	*ndev;
	struct pci_dev		*pdev;
	struct __vxge_hw_ring	*handle;
	/* The vpath id maintained in the driver -
	 * 0 to 'maximum_vpaths_in_function - 1'
	 */
	int driver_id;

	/* Adaptive interrupt moderation parameters used in T1A */
	unsigned long interrupt_count;
	unsigned long jiffies;

	/* copy of the flag indicating whether rx_hwts is to be used */
	u32 rx_hwts:1;

	int pkts_processed;
	int budget;

	struct napi_struct napi;
	struct napi_struct *napi_p;

#define VXGE_MAX_MAC_ADDR_COUNT		30

	int vlan_tag_strip;
	u32 rx_vector_no;
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
	u8 macaddr[ETH_ALEN];
	u8 macmask[ETH_ALEN];

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
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
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

	/* A flag indicating whether rx_hwts is to be used or not. */
	u32	rx_hwts:1,
		titan1:1;

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
	struct work_struct reset_task;
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

static inline
void vxge_os_timer(struct timer_list *timer, void (*func)(struct timer_list *),
		   unsigned long timeout)
{
	timer_setup(timer, func, 0);
	mod_timer(timer, jiffies + timeout);
}

void vxge_initialize_ethtool_ops(struct net_device *ndev);
int vxge_fw_upgrade(struct vxgedev *vdev, char *fw_name, int override);

/* #define VXGE_DEBUG_INIT: debug for initialization functions
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
