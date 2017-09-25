/*
 * Copyright (c) 2013 Eugene Krasnikov <k.eugene.e@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _WCN36XX_H_
#define _WCN36XX_H_

#include <linux/completion.h>
#include <linux/printk.h>
#include <linux/spinlock.h>
#include <net/mac80211.h>

#include "hal.h"
#include "smd.h"
#include "txrx.h"
#include "dxe.h"
#include "pmc.h"
#include "debug.h"

#define WLAN_NV_FILE               "wlan/prima/WCNSS_qcom_wlan_nv.bin"
#define WCN36XX_AGGR_BUFFER_SIZE 64

/* How many frames until we start a-mpdu TX session */
#define WCN36XX_AMPDU_START_THRESH	20

#define WCN36XX_MAX_SCAN_SSIDS		9
#define WCN36XX_MAX_SCAN_IE_LEN		500

extern unsigned int wcn36xx_dbg_mask;

enum wcn36xx_debug_mask {
	WCN36XX_DBG_DXE		= 0x00000001,
	WCN36XX_DBG_DXE_DUMP	= 0x00000002,
	WCN36XX_DBG_SMD		= 0x00000004,
	WCN36XX_DBG_SMD_DUMP	= 0x00000008,
	WCN36XX_DBG_RX		= 0x00000010,
	WCN36XX_DBG_RX_DUMP	= 0x00000020,
	WCN36XX_DBG_TX		= 0x00000040,
	WCN36XX_DBG_TX_DUMP	= 0x00000080,
	WCN36XX_DBG_HAL		= 0x00000100,
	WCN36XX_DBG_HAL_DUMP	= 0x00000200,
	WCN36XX_DBG_MAC		= 0x00000400,
	WCN36XX_DBG_BEACON	= 0x00000800,
	WCN36XX_DBG_BEACON_DUMP	= 0x00001000,
	WCN36XX_DBG_PMC		= 0x00002000,
	WCN36XX_DBG_PMC_DUMP	= 0x00004000,
	WCN36XX_DBG_ANY		= 0xffffffff,
};

#define wcn36xx_err(fmt, arg...)				\
	printk(KERN_ERR pr_fmt("ERROR " fmt), ##arg)

#define wcn36xx_warn(fmt, arg...)				\
	printk(KERN_WARNING pr_fmt("WARNING " fmt), ##arg)

#define wcn36xx_info(fmt, arg...)		\
	printk(KERN_INFO pr_fmt(fmt), ##arg)

#define wcn36xx_dbg(mask, fmt, arg...) do {			\
	if (wcn36xx_dbg_mask & mask)					\
		printk(KERN_DEBUG pr_fmt(fmt), ##arg);	\
} while (0)

#define wcn36xx_dbg_dump(mask, prefix_str, buf, len) do {	\
	if (wcn36xx_dbg_mask & mask)					\
		print_hex_dump(KERN_DEBUG, pr_fmt(prefix_str),	\
			       DUMP_PREFIX_OFFSET, 32, 1,	\
			       buf, len, false);		\
} while (0)

enum wcn36xx_ampdu_state {
	WCN36XX_AMPDU_NONE,
	WCN36XX_AMPDU_INIT,
	WCN36XX_AMPDU_START,
	WCN36XX_AMPDU_OPERATIONAL,
};

#define WCN36XX_HW_CHANNEL(__wcn) (__wcn->hw->conf.chandef.chan->hw_value)
#define WCN36XX_BAND(__wcn) (__wcn->hw->conf.chandef.chan->band)
#define WCN36XX_CENTER_FREQ(__wcn) (__wcn->hw->conf.chandef.chan->center_freq)
#define WCN36XX_LISTEN_INTERVAL(__wcn) (__wcn->hw->conf.listen_interval)
#define WCN36XX_FLAGS(__wcn) (__wcn->hw->flags)
#define WCN36XX_MAX_POWER(__wcn) (__wcn->hw->conf.chandef.chan->max_power)

static inline void buff_to_be(u32 *buf, size_t len)
{
	int i;
	for (i = 0; i < len; i++)
		buf[i] = cpu_to_be32(buf[i]);
}

struct nv_data {
	int	is_valid;
	u8	table;
};

/**
 * struct wcn36xx_vif - holds VIF related fields
 *
 * @bss_index: bss_index is initially set to 0xFF. bss_index is received from
 * HW after first config_bss call and must be used in delete_bss and
 * enter/exit_bmps.
 */
struct wcn36xx_vif {
	struct list_head list;
	u8 dtim_period;
	enum ani_ed_type encrypt_type;
	bool is_joining;
	bool sta_assoc;
	struct wcn36xx_hal_mac_ssid ssid;

	/* Power management */
	enum wcn36xx_power_state pw_state;

	u8 bss_index;
	/* Returned from WCN36XX_HAL_ADD_STA_SELF_RSP */
	u8 self_sta_index;
	u8 self_dpu_desc_index;
	u8 self_ucast_dpu_sign;
};

/**
 * struct wcn36xx_sta - holds STA related fields
 *
 * @tid: traffic ID that is used during AMPDU and in TX BD.
 * @sta_index: STA index is returned from HW after config_sta call and is
 * used in both SMD channel and TX BD.
 * @dpu_desc_index: DPU descriptor index is returned from HW after config_sta
 * call and is used in TX BD.
 * @bss_sta_index: STA index is returned from HW after config_bss call and is
 * used in both SMD channel and TX BD. See table bellow when it is used.
 * @bss_dpu_desc_index: DPU descriptor index is returned from HW after
 * config_bss call and is used in TX BD.
 * ______________________________________________
 * |		  |	STA	|	AP	|
 * |______________|_____________|_______________|
 * |    TX BD     |bss_sta_index|   sta_index   |
 * |______________|_____________|_______________|
 * |all SMD calls |bss_sta_index|   sta_index	|
 * |______________|_____________|_______________|
 * |smd_delete_sta|  sta_index  |   sta_index	|
 * |______________|_____________|_______________|
 */
struct wcn36xx_sta {
	struct wcn36xx_vif *vif;
	u16 aid;
	u16 tid;
	u8 sta_index;
	u8 dpu_desc_index;
	u8 ucast_dpu_sign;
	u8 bss_sta_index;
	u8 bss_dpu_desc_index;
	bool is_data_encrypted;
	/* Rates */
	struct wcn36xx_hal_supported_rates supported_rates;

	spinlock_t ampdu_lock;		/* protects next two fields */
	enum wcn36xx_ampdu_state ampdu_state[16];
	int non_agg_frame_ct;
};
struct wcn36xx_dxe_ch;
struct wcn36xx {
	struct ieee80211_hw	*hw;
	struct device		*dev;
	struct list_head	vif_list;

	const struct firmware	*nv;

	u8			fw_revision;
	u8			fw_version;
	u8			fw_minor;
	u8			fw_major;
	u32			fw_feat_caps[WCN36XX_HAL_CAPS_SIZE];
	bool			is_pronto;

	/* extra byte for the NULL termination */
	u8			crm_version[WCN36XX_HAL_VERSION_LENGTH + 1];
	u8			wlan_version[WCN36XX_HAL_VERSION_LENGTH + 1];

	/* IRQs */
	int			tx_irq;
	int			rx_irq;
	void __iomem		*ccu_base;
	void __iomem		*dxe_base;

	struct rpmsg_endpoint	*smd_channel;

	struct qcom_smem_state  *tx_enable_state;
	unsigned		tx_enable_state_bit;
	struct qcom_smem_state	*tx_rings_empty_state;
	unsigned		tx_rings_empty_state_bit;

	/* prevents concurrent FW reconfiguration */
	struct mutex		conf_mutex;

	/*
	 * smd_buf must be protected with smd_mutex to garantee
	 * that all messages are sent one after another
	 */
	u8			*hal_buf;
	size_t			hal_rsp_len;
	struct mutex		hal_mutex;
	struct completion	hal_rsp_compl;
	struct workqueue_struct	*hal_ind_wq;
	struct work_struct	hal_ind_work;
	spinlock_t		hal_ind_lock;
	struct list_head	hal_ind_queue;

	struct work_struct	scan_work;
	struct cfg80211_scan_request *scan_req;
	int			scan_freq;
	int			scan_band;
	struct mutex		scan_lock;
	bool			scan_aborted;

	/* DXE channels */
	struct wcn36xx_dxe_ch	dxe_tx_l_ch;	/* TX low */
	struct wcn36xx_dxe_ch	dxe_tx_h_ch;	/* TX high */
	struct wcn36xx_dxe_ch	dxe_rx_l_ch;	/* RX low */
	struct wcn36xx_dxe_ch	dxe_rx_h_ch;	/* RX high */

	/* For synchronization of DXE resources from BH, IRQ and WQ contexts */
	spinlock_t	dxe_lock;
	bool                    queues_stopped;

	/* Memory pools */
	struct wcn36xx_dxe_mem_pool mgmt_mem_pool;
	struct wcn36xx_dxe_mem_pool data_mem_pool;

	struct sk_buff		*tx_ack_skb;

#ifdef CONFIG_WCN36XX_DEBUGFS
	/* Debug file system entry */
	struct wcn36xx_dfs_entry    dfs;
#endif /* CONFIG_WCN36XX_DEBUGFS */

};

static inline bool wcn36xx_is_fw_version(struct wcn36xx *wcn,
					 u8 major,
					 u8 minor,
					 u8 version,
					 u8 revision)
{
	return (wcn->fw_major == major &&
		wcn->fw_minor == minor &&
		wcn->fw_version == version &&
		wcn->fw_revision == revision);
}
void wcn36xx_set_default_rates(struct wcn36xx_hal_supported_rates *rates);

static inline
struct ieee80211_sta *wcn36xx_priv_to_sta(struct wcn36xx_sta *sta_priv)
{
	return container_of((void *)sta_priv, struct ieee80211_sta, drv_priv);
}

static inline
struct wcn36xx_vif *wcn36xx_vif_to_priv(struct ieee80211_vif *vif)
{
	return (struct wcn36xx_vif *) vif->drv_priv;
}

static inline
struct ieee80211_vif *wcn36xx_priv_to_vif(struct wcn36xx_vif *vif_priv)
{
	return container_of((void *) vif_priv, struct ieee80211_vif, drv_priv);
}

static inline
struct wcn36xx_sta *wcn36xx_sta_to_priv(struct ieee80211_sta *sta)
{
	return (struct wcn36xx_sta *)sta->drv_priv;
}

#endif	/* _WCN36XX_H_ */
