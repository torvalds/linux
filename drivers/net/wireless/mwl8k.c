/*
 * drivers/net/wireless/mwl8k.c
 * Driver for Marvell TOPDOG 802.11 Wireless cards
 *
 * Copyright (C) 2008-2009 Marvell Semiconductor Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/workqueue.h>

#define MWL8K_DESC	"Marvell TOPDOG(R) 802.11 Wireless Network Driver"
#define MWL8K_NAME	KBUILD_MODNAME
#define MWL8K_VERSION	"0.10"

static DEFINE_PCI_DEVICE_TABLE(mwl8k_table) = {
	{ PCI_VDEVICE(MARVELL, 0x2a2b), .driver_data = 8687, },
	{ PCI_VDEVICE(MARVELL, 0x2a30), .driver_data = 8687, },
	{ }
};
MODULE_DEVICE_TABLE(pci, mwl8k_table);

/* Register definitions */
#define MWL8K_HIU_GEN_PTR			0x00000c10
#define  MWL8K_MODE_STA				 0x0000005a
#define  MWL8K_MODE_AP				 0x000000a5
#define MWL8K_HIU_INT_CODE			0x00000c14
#define  MWL8K_FWSTA_READY			 0xf0f1f2f4
#define  MWL8K_FWAP_READY			 0xf1f2f4a5
#define  MWL8K_INT_CODE_CMD_FINISHED		 0x00000005
#define MWL8K_HIU_SCRATCH			0x00000c40

/* Host->device communications */
#define MWL8K_HIU_H2A_INTERRUPT_EVENTS		0x00000c18
#define MWL8K_HIU_H2A_INTERRUPT_STATUS		0x00000c1c
#define MWL8K_HIU_H2A_INTERRUPT_MASK		0x00000c20
#define MWL8K_HIU_H2A_INTERRUPT_CLEAR_SEL	0x00000c24
#define MWL8K_HIU_H2A_INTERRUPT_STATUS_MASK	0x00000c28
#define  MWL8K_H2A_INT_DUMMY			 (1 << 20)
#define  MWL8K_H2A_INT_RESET			 (1 << 15)
#define  MWL8K_H2A_INT_DOORBELL			 (1 << 1)
#define  MWL8K_H2A_INT_PPA_READY		 (1 << 0)

/* Device->host communications */
#define MWL8K_HIU_A2H_INTERRUPT_EVENTS		0x00000c2c
#define MWL8K_HIU_A2H_INTERRUPT_STATUS		0x00000c30
#define MWL8K_HIU_A2H_INTERRUPT_MASK		0x00000c34
#define MWL8K_HIU_A2H_INTERRUPT_CLEAR_SEL	0x00000c38
#define MWL8K_HIU_A2H_INTERRUPT_STATUS_MASK	0x00000c3c
#define  MWL8K_A2H_INT_DUMMY			 (1 << 20)
#define  MWL8K_A2H_INT_CHNL_SWITCHED		 (1 << 11)
#define  MWL8K_A2H_INT_QUEUE_EMPTY		 (1 << 10)
#define  MWL8K_A2H_INT_RADAR_DETECT		 (1 << 7)
#define  MWL8K_A2H_INT_RADIO_ON			 (1 << 6)
#define  MWL8K_A2H_INT_RADIO_OFF		 (1 << 5)
#define  MWL8K_A2H_INT_MAC_EVENT		 (1 << 3)
#define  MWL8K_A2H_INT_OPC_DONE			 (1 << 2)
#define  MWL8K_A2H_INT_RX_READY			 (1 << 1)
#define  MWL8K_A2H_INT_TX_DONE			 (1 << 0)

#define MWL8K_A2H_EVENTS	(MWL8K_A2H_INT_DUMMY | \
				 MWL8K_A2H_INT_CHNL_SWITCHED | \
				 MWL8K_A2H_INT_QUEUE_EMPTY | \
				 MWL8K_A2H_INT_RADAR_DETECT | \
				 MWL8K_A2H_INT_RADIO_ON | \
				 MWL8K_A2H_INT_RADIO_OFF | \
				 MWL8K_A2H_INT_MAC_EVENT | \
				 MWL8K_A2H_INT_OPC_DONE | \
				 MWL8K_A2H_INT_RX_READY | \
				 MWL8K_A2H_INT_TX_DONE)

#define MWL8K_RX_QUEUES		1
#define MWL8K_TX_QUEUES		4

struct mwl8k_rx_queue {
	int rx_desc_count;

	/* hw receives here */
	int rx_head;

	/* refill descs here */
	int rx_tail;

	struct mwl8k_rx_desc *rx_desc_area;
	dma_addr_t rx_desc_dma;
	struct sk_buff **rx_skb;
};

struct mwl8k_tx_queue {
	/* hw transmits here */
	int tx_head;

	/* sw appends here */
	int tx_tail;

	struct ieee80211_tx_queue_stats tx_stats;
	struct mwl8k_tx_desc *tx_desc_area;
	dma_addr_t tx_desc_dma;
	struct sk_buff **tx_skb;
};

/* Pointers to the firmware data and meta information about it.  */
struct mwl8k_firmware {
	/* Microcode */
	struct firmware *ucode;

	/* Boot helper code */
	struct firmware *helper;
};

struct mwl8k_priv {
	void __iomem *regs;
	struct ieee80211_hw *hw;

	struct pci_dev *pdev;

	/* firmware files and meta data */
	struct mwl8k_firmware fw;
	u32 part_num;

	/* firmware access */
	struct mutex fw_mutex;
	struct task_struct *fw_mutex_owner;
	int fw_mutex_depth;
	struct completion *hostcmd_wait;

	/* lock held over TX and TX reap */
	spinlock_t tx_lock;

	/* TX quiesce completion, protected by fw_mutex and tx_lock */
	struct completion *tx_wait;

	struct ieee80211_vif *vif;

	struct ieee80211_channel *current_channel;

	/* power management status cookie from firmware */
	u32 *cookie;
	dma_addr_t cookie_dma;

	u16 num_mcaddrs;
	u8 hw_rev;
	u32 fw_rev;

	/*
	 * Running count of TX packets in flight, to avoid
	 * iterating over the transmit rings each time.
	 */
	int pending_tx_pkts;

	struct mwl8k_rx_queue rxq[MWL8K_RX_QUEUES];
	struct mwl8k_tx_queue txq[MWL8K_TX_QUEUES];

	/* PHY parameters */
	struct ieee80211_supported_band band;
	struct ieee80211_channel channels[14];
	struct ieee80211_rate rates[13];

	bool radio_on;
	bool radio_short_preamble;
	bool sniffer_enabled;
	bool wmm_enabled;

	/* XXX need to convert this to handle multiple interfaces */
	bool capture_beacon;
	u8 capture_bssid[ETH_ALEN];
	struct sk_buff *beacon_skb;

	/*
	 * This FJ worker has to be global as it is scheduled from the
	 * RX handler.  At this point we don't know which interface it
	 * belongs to until the list of bssids waiting to complete join
	 * is checked.
	 */
	struct work_struct finalize_join_worker;

	/* Tasklet to reclaim TX descriptors and buffers after tx */
	struct tasklet_struct tx_reclaim_task;
};

/* Per interface specific private data */
struct mwl8k_vif {
	/* backpointer to parent config block */
	struct mwl8k_priv *priv;

	/* BSS config of AP or IBSS from mac80211*/
	struct ieee80211_bss_conf bss_info;

	/* BSSID of AP or IBSS */
	u8	bssid[ETH_ALEN];
	u8	mac_addr[ETH_ALEN];

	/*
	 * Subset of supported legacy rates.
	 * Intersection of AP and STA supported rates.
	 */
	struct ieee80211_rate legacy_rates[13];

	/* number of supported legacy rates */
	u8	legacy_nrates;

	 /* Index into station database.Returned by update_sta_db call */
	u8	peer_id;

	/* Non AMPDU sequence number assigned by driver */
	u16	seqno;
};

#define MWL8K_VIF(_vif) ((struct mwl8k_vif *)&((_vif)->drv_priv))

static const struct ieee80211_channel mwl8k_channels[] = {
	{ .center_freq = 2412, .hw_value = 1, },
	{ .center_freq = 2417, .hw_value = 2, },
	{ .center_freq = 2422, .hw_value = 3, },
	{ .center_freq = 2427, .hw_value = 4, },
	{ .center_freq = 2432, .hw_value = 5, },
	{ .center_freq = 2437, .hw_value = 6, },
	{ .center_freq = 2442, .hw_value = 7, },
	{ .center_freq = 2447, .hw_value = 8, },
	{ .center_freq = 2452, .hw_value = 9, },
	{ .center_freq = 2457, .hw_value = 10, },
	{ .center_freq = 2462, .hw_value = 11, },
};

static const struct ieee80211_rate mwl8k_rates[] = {
	{ .bitrate = 10, .hw_value = 2, },
	{ .bitrate = 20, .hw_value = 4, },
	{ .bitrate = 55, .hw_value = 11, },
	{ .bitrate = 110, .hw_value = 22, },
	{ .bitrate = 220, .hw_value = 44, },
	{ .bitrate = 60, .hw_value = 12, },
	{ .bitrate = 90, .hw_value = 18, },
	{ .bitrate = 120, .hw_value = 24, },
	{ .bitrate = 180, .hw_value = 36, },
	{ .bitrate = 240, .hw_value = 48, },
	{ .bitrate = 360, .hw_value = 72, },
	{ .bitrate = 480, .hw_value = 96, },
	{ .bitrate = 540, .hw_value = 108, },
};

/* Set or get info from Firmware */
#define MWL8K_CMD_SET			0x0001
#define MWL8K_CMD_GET			0x0000

/* Firmware command codes */
#define MWL8K_CMD_CODE_DNLD		0x0001
#define MWL8K_CMD_GET_HW_SPEC		0x0003
#define MWL8K_CMD_MAC_MULTICAST_ADR	0x0010
#define MWL8K_CMD_GET_STAT		0x0014
#define MWL8K_CMD_RADIO_CONTROL		0x001c
#define MWL8K_CMD_RF_TX_POWER		0x001e
#define MWL8K_CMD_SET_PRE_SCAN		0x0107
#define MWL8K_CMD_SET_POST_SCAN		0x0108
#define MWL8K_CMD_SET_RF_CHANNEL	0x010a
#define MWL8K_CMD_SET_AID		0x010d
#define MWL8K_CMD_SET_RATE		0x0110
#define MWL8K_CMD_SET_FINALIZE_JOIN	0x0111
#define MWL8K_CMD_RTS_THRESHOLD		0x0113
#define MWL8K_CMD_SET_SLOT		0x0114
#define MWL8K_CMD_SET_EDCA_PARAMS	0x0115
#define MWL8K_CMD_SET_WMM_MODE		0x0123
#define MWL8K_CMD_MIMO_CONFIG		0x0125
#define MWL8K_CMD_USE_FIXED_RATE	0x0126
#define MWL8K_CMD_ENABLE_SNIFFER	0x0150
#define MWL8K_CMD_SET_MAC_ADDR		0x0202
#define MWL8K_CMD_SET_RATEADAPT_MODE	0x0203
#define MWL8K_CMD_UPDATE_STADB		0x1123

static const char *mwl8k_cmd_name(u16 cmd, char *buf, int bufsize)
{
#define MWL8K_CMDNAME(x)	case MWL8K_CMD_##x: do {\
					snprintf(buf, bufsize, "%s", #x);\
					return buf;\
					} while (0)
	switch (cmd & ~0x8000) {
		MWL8K_CMDNAME(CODE_DNLD);
		MWL8K_CMDNAME(GET_HW_SPEC);
		MWL8K_CMDNAME(MAC_MULTICAST_ADR);
		MWL8K_CMDNAME(GET_STAT);
		MWL8K_CMDNAME(RADIO_CONTROL);
		MWL8K_CMDNAME(RF_TX_POWER);
		MWL8K_CMDNAME(SET_PRE_SCAN);
		MWL8K_CMDNAME(SET_POST_SCAN);
		MWL8K_CMDNAME(SET_RF_CHANNEL);
		MWL8K_CMDNAME(SET_AID);
		MWL8K_CMDNAME(SET_RATE);
		MWL8K_CMDNAME(SET_FINALIZE_JOIN);
		MWL8K_CMDNAME(RTS_THRESHOLD);
		MWL8K_CMDNAME(SET_SLOT);
		MWL8K_CMDNAME(SET_EDCA_PARAMS);
		MWL8K_CMDNAME(SET_WMM_MODE);
		MWL8K_CMDNAME(MIMO_CONFIG);
		MWL8K_CMDNAME(USE_FIXED_RATE);
		MWL8K_CMDNAME(ENABLE_SNIFFER);
		MWL8K_CMDNAME(SET_MAC_ADDR);
		MWL8K_CMDNAME(SET_RATEADAPT_MODE);
		MWL8K_CMDNAME(UPDATE_STADB);
	default:
		snprintf(buf, bufsize, "0x%x", cmd);
	}
#undef MWL8K_CMDNAME

	return buf;
}

/* Hardware and firmware reset */
static void mwl8k_hw_reset(struct mwl8k_priv *priv)
{
	iowrite32(MWL8K_H2A_INT_RESET,
		priv->regs + MWL8K_HIU_H2A_INTERRUPT_EVENTS);
	iowrite32(MWL8K_H2A_INT_RESET,
		priv->regs + MWL8K_HIU_H2A_INTERRUPT_EVENTS);
	msleep(20);
}

/* Release fw image */
static void mwl8k_release_fw(struct firmware **fw)
{
	if (*fw == NULL)
		return;
	release_firmware(*fw);
	*fw = NULL;
}

static void mwl8k_release_firmware(struct mwl8k_priv *priv)
{
	mwl8k_release_fw(&priv->fw.ucode);
	mwl8k_release_fw(&priv->fw.helper);
}

/* Request fw image */
static int mwl8k_request_fw(struct mwl8k_priv *priv,
			    const char *fname, struct firmware **fw)
{
	/* release current image */
	if (*fw != NULL)
		mwl8k_release_fw(fw);

	return request_firmware((const struct firmware **)fw,
				fname, &priv->pdev->dev);
}

static int mwl8k_request_firmware(struct mwl8k_priv *priv, u32 part_num)
{
	u8 filename[64];
	int rc;

	priv->part_num = part_num;

	snprintf(filename, sizeof(filename),
		 "mwl8k/helper_%u.fw", priv->part_num);

	rc = mwl8k_request_fw(priv, filename, &priv->fw.helper);
	if (rc) {
		printk(KERN_ERR "%s: Error requesting helper firmware "
		       "file %s\n", pci_name(priv->pdev), filename);
		return rc;
	}

	snprintf(filename, sizeof(filename),
		 "mwl8k/fmimage_%u.fw", priv->part_num);

	rc = mwl8k_request_fw(priv, filename, &priv->fw.ucode);
	if (rc) {
		printk(KERN_ERR "%s: Error requesting firmware file %s\n",
		       pci_name(priv->pdev), filename);
		mwl8k_release_fw(&priv->fw.helper);
		return rc;
	}

	return 0;
}

struct mwl8k_cmd_pkt {
	__le16	code;
	__le16	length;
	__le16	seq_num;
	__le16	result;
	char	payload[0];
} __attribute__((packed));

/*
 * Firmware loading.
 */
static int
mwl8k_send_fw_load_cmd(struct mwl8k_priv *priv, void *data, int length)
{
	void __iomem *regs = priv->regs;
	dma_addr_t dma_addr;
	int loops;

	dma_addr = pci_map_single(priv->pdev, data, length, PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(priv->pdev, dma_addr))
		return -ENOMEM;

	iowrite32(dma_addr, regs + MWL8K_HIU_GEN_PTR);
	iowrite32(0, regs + MWL8K_HIU_INT_CODE);
	iowrite32(MWL8K_H2A_INT_DOORBELL,
		regs + MWL8K_HIU_H2A_INTERRUPT_EVENTS);
	iowrite32(MWL8K_H2A_INT_DUMMY,
		regs + MWL8K_HIU_H2A_INTERRUPT_EVENTS);

	loops = 1000;
	do {
		u32 int_code;

		int_code = ioread32(regs + MWL8K_HIU_INT_CODE);
		if (int_code == MWL8K_INT_CODE_CMD_FINISHED) {
			iowrite32(0, regs + MWL8K_HIU_INT_CODE);
			break;
		}

		cond_resched();
		udelay(1);
	} while (--loops);

	pci_unmap_single(priv->pdev, dma_addr, length, PCI_DMA_TODEVICE);

	return loops ? 0 : -ETIMEDOUT;
}

static int mwl8k_load_fw_image(struct mwl8k_priv *priv,
				const u8 *data, size_t length)
{
	struct mwl8k_cmd_pkt *cmd;
	int done;
	int rc = 0;

	cmd = kmalloc(sizeof(*cmd) + 256, GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->code = cpu_to_le16(MWL8K_CMD_CODE_DNLD);
	cmd->seq_num = 0;
	cmd->result = 0;

	done = 0;
	while (length) {
		int block_size = length > 256 ? 256 : length;

		memcpy(cmd->payload, data + done, block_size);
		cmd->length = cpu_to_le16(block_size);

		rc = mwl8k_send_fw_load_cmd(priv, cmd,
						sizeof(*cmd) + block_size);
		if (rc)
			break;

		done += block_size;
		length -= block_size;
	}

	if (!rc) {
		cmd->length = 0;
		rc = mwl8k_send_fw_load_cmd(priv, cmd, sizeof(*cmd));
	}

	kfree(cmd);

	return rc;
}

static int mwl8k_feed_fw_image(struct mwl8k_priv *priv,
				const u8 *data, size_t length)
{
	unsigned char *buffer;
	int may_continue, rc = 0;
	u32 done, prev_block_size;

	buffer = kmalloc(1024, GFP_KERNEL);
	if (buffer == NULL)
		return -ENOMEM;

	done = 0;
	prev_block_size = 0;
	may_continue = 1000;
	while (may_continue > 0) {
		u32 block_size;

		block_size = ioread32(priv->regs + MWL8K_HIU_SCRATCH);
		if (block_size & 1) {
			block_size &= ~1;
			may_continue--;
		} else {
			done += prev_block_size;
			length -= prev_block_size;
		}

		if (block_size > 1024 || block_size > length) {
			rc = -EOVERFLOW;
			break;
		}

		if (length == 0) {
			rc = 0;
			break;
		}

		if (block_size == 0) {
			rc = -EPROTO;
			may_continue--;
			udelay(1);
			continue;
		}

		prev_block_size = block_size;
		memcpy(buffer, data + done, block_size);

		rc = mwl8k_send_fw_load_cmd(priv, buffer, block_size);
		if (rc)
			break;
	}

	if (!rc && length != 0)
		rc = -EREMOTEIO;

	kfree(buffer);

	return rc;
}

static int mwl8k_load_firmware(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;
	struct firmware *fw = priv->fw.ucode;
	int rc;
	int loops;

	if (!memcmp(fw->data, "\x01\x00\x00\x00", 4)) {
		struct firmware *helper = priv->fw.helper;

		if (helper == NULL) {
			printk(KERN_ERR "%s: helper image needed but none "
			       "given\n", pci_name(priv->pdev));
			return -EINVAL;
		}

		rc = mwl8k_load_fw_image(priv, helper->data, helper->size);
		if (rc) {
			printk(KERN_ERR "%s: unable to load firmware "
			       "helper image\n", pci_name(priv->pdev));
			return rc;
		}
		msleep(1);

		rc = mwl8k_feed_fw_image(priv, fw->data, fw->size);
	} else {
		rc = mwl8k_load_fw_image(priv, fw->data, fw->size);
	}

	if (rc) {
		printk(KERN_ERR "%s: unable to load firmware image\n",
		       pci_name(priv->pdev));
		return rc;
	}

	iowrite32(MWL8K_MODE_STA, priv->regs + MWL8K_HIU_GEN_PTR);
	msleep(1);

	loops = 200000;
	do {
		if (ioread32(priv->regs + MWL8K_HIU_INT_CODE)
						== MWL8K_FWSTA_READY)
			break;
		udelay(1);
	} while (--loops);

	return loops ? 0 : -ETIMEDOUT;
}


/*
 * Defines shared between transmission and reception.
 */
/* HT control fields for firmware */
struct ewc_ht_info {
	__le16	control1;
	__le16	control2;
	__le16	control3;
} __attribute__((packed));

/* Firmware Station database operations */
#define MWL8K_STA_DB_ADD_ENTRY		0
#define MWL8K_STA_DB_MODIFY_ENTRY	1
#define MWL8K_STA_DB_DEL_ENTRY		2
#define MWL8K_STA_DB_FLUSH		3

/* Peer Entry flags - used to define the type of the peer node */
#define MWL8K_PEER_TYPE_ACCESSPOINT	2

#define MWL8K_IEEE_LEGACY_DATA_RATES	13
#define MWL8K_MCS_BITMAP_SIZE		16

struct peer_capability_info {
	/* Peer type - AP vs. STA.  */
	__u8	peer_type;

	/* Basic 802.11 capabilities from assoc resp.  */
	__le16	basic_caps;

	/* Set if peer supports 802.11n high throughput (HT).  */
	__u8	ht_support;

	/* Valid if HT is supported.  */
	__le16	ht_caps;
	__u8	extended_ht_caps;
	struct ewc_ht_info	ewc_info;

	/* Legacy rate table. Intersection of our rates and peer rates.  */
	__u8	legacy_rates[MWL8K_IEEE_LEGACY_DATA_RATES];

	/* HT rate table. Intersection of our rates and peer rates.  */
	__u8	ht_rates[MWL8K_MCS_BITMAP_SIZE];
	__u8	pad[16];

	/* If set, interoperability mode, no proprietary extensions.  */
	__u8	interop;
	__u8	pad2;
	__u8	station_id;
	__le16	amsdu_enabled;
} __attribute__((packed));

/* Inline functions to manipulate QoS field in data descriptor.  */
static inline u16 mwl8k_qos_setbit_eosp(u16 qos)
{
	u16 val_mask = 1 << 4;

	/* End of Service Period Bit 4 */
	return qos | val_mask;
}

static inline u16 mwl8k_qos_setbit_ack(u16 qos, u8 ack_policy)
{
	u16 val_mask = 0x3;
	u8	shift = 5;
	u16 qos_mask = ~(val_mask << shift);

	/* Ack Policy Bit 5-6 */
	return (qos & qos_mask) | ((ack_policy & val_mask) << shift);
}

static inline u16 mwl8k_qos_setbit_amsdu(u16 qos)
{
	u16 val_mask = 1 << 7;

	/* AMSDU present Bit 7 */
	return qos | val_mask;
}

static inline u16 mwl8k_qos_setbit_qlen(u16 qos, u8 len)
{
	u16 val_mask = 0xff;
	u8	shift = 8;
	u16 qos_mask = ~(val_mask << shift);

	/* Queue Length Bits 8-15 */
	return (qos & qos_mask) | ((len & val_mask) << shift);
}

/* DMA header used by firmware and hardware.  */
struct mwl8k_dma_data {
	__le16 fwlen;
	struct ieee80211_hdr wh;
} __attribute__((packed));

/* Routines to add/remove DMA header from skb.  */
static inline void mwl8k_remove_dma_header(struct sk_buff *skb)
{
	struct mwl8k_dma_data *tr = (struct mwl8k_dma_data *)skb->data;
	void *dst, *src = &tr->wh;
	int hdrlen = ieee80211_hdrlen(tr->wh.frame_control);
	u16 space = sizeof(struct mwl8k_dma_data) - hdrlen;

	dst = (void *)tr + space;
	if (dst != src) {
		memmove(dst, src, hdrlen);
		skb_pull(skb, space);
	}
}

static inline void mwl8k_add_dma_header(struct sk_buff *skb)
{
	struct ieee80211_hdr *wh;
	u32 hdrlen, pktlen;
	struct mwl8k_dma_data *tr;

	wh = (struct ieee80211_hdr *)skb->data;
	hdrlen = ieee80211_hdrlen(wh->frame_control);
	pktlen = skb->len;

	/*
	 * Copy up/down the 802.11 header; the firmware requires
	 * we present a 2-byte payload length followed by a
	 * 4-address header (w/o QoS), followed (optionally) by
	 * any WEP/ExtIV header (but only filled in for CCMP).
	 */
	if (hdrlen != sizeof(struct mwl8k_dma_data))
		skb_push(skb, sizeof(struct mwl8k_dma_data) - hdrlen);

	tr = (struct mwl8k_dma_data *)skb->data;
	if (wh != &tr->wh)
		memmove(&tr->wh, wh, hdrlen);

	/* Clear addr4 */
	memset(tr->wh.addr4, 0, ETH_ALEN);

	/*
	 * Firmware length is the length of the fully formed "802.11
	 * payload".  That is, everything except for the 802.11 header.
	 * This includes all crypto material including the MIC.
	 */
	tr->fwlen = cpu_to_le16(pktlen - hdrlen);
}


/*
 * Packet reception.
 */
#define MWL8K_RX_CTRL_OWNED_BY_HOST	0x02

struct mwl8k_rx_desc {
	__le16 pkt_len;
	__u8 link_quality;
	__u8 noise_level;
	__le32 pkt_phys_addr;
	__le32 next_rx_desc_phys_addr;
	__le16 qos_control;
	__le16 rate_info;
	__le32 pad0[4];
	__u8 rssi;
	__u8 channel;
	__le16 pad1;
	__u8 rx_ctrl;
	__u8 rx_status;
	__u8 pad2[2];
} __attribute__((packed));

#define MWL8K_RX_DESCS		256
#define MWL8K_RX_MAXSZ		3800

#define RATE_INFO_SHORTPRE		0x8000
#define RATE_INFO_ANTSELECT(x)		(((x) >> 11) & 0x3)
#define RATE_INFO_RATEID(x)		(((x) >> 3) & 0x3f)
#define RATE_INFO_40MHZ			0x0004
#define RATE_INFO_SHORTGI		0x0002
#define RATE_INFO_MCS_FORMAT		0x0001

static int mwl8k_rxq_init(struct ieee80211_hw *hw, int index)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_rx_queue *rxq = priv->rxq + index;
	int size;
	int i;

	rxq->rx_desc_count = 0;
	rxq->rx_head = 0;
	rxq->rx_tail = 0;

	size = MWL8K_RX_DESCS * sizeof(struct mwl8k_rx_desc);

	rxq->rx_desc_area =
		pci_alloc_consistent(priv->pdev, size, &rxq->rx_desc_dma);
	if (rxq->rx_desc_area == NULL) {
		printk(KERN_ERR "%s: failed to alloc RX descriptors\n",
		       wiphy_name(hw->wiphy));
		return -ENOMEM;
	}
	memset(rxq->rx_desc_area, 0, size);

	rxq->rx_skb = kmalloc(MWL8K_RX_DESCS *
				sizeof(*rxq->rx_skb), GFP_KERNEL);
	if (rxq->rx_skb == NULL) {
		printk(KERN_ERR "%s: failed to alloc RX skbuff list\n",
		       wiphy_name(hw->wiphy));
		pci_free_consistent(priv->pdev, size,
				    rxq->rx_desc_area, rxq->rx_desc_dma);
		return -ENOMEM;
	}
	memset(rxq->rx_skb, 0, MWL8K_RX_DESCS * sizeof(*rxq->rx_skb));

	for (i = 0; i < MWL8K_RX_DESCS; i++) {
		struct mwl8k_rx_desc *rx_desc;
		int nexti;

		rx_desc = rxq->rx_desc_area + i;
		nexti = (i + 1) % MWL8K_RX_DESCS;

		rx_desc->next_rx_desc_phys_addr =
			cpu_to_le32(rxq->rx_desc_dma
						+ nexti * sizeof(*rx_desc));
		rx_desc->rx_ctrl = MWL8K_RX_CTRL_OWNED_BY_HOST;
	}

	return 0;
}

static int rxq_refill(struct ieee80211_hw *hw, int index, int limit)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_rx_queue *rxq = priv->rxq + index;
	int refilled;

	refilled = 0;
	while (rxq->rx_desc_count < MWL8K_RX_DESCS && limit--) {
		struct sk_buff *skb;
		int rx;

		skb = dev_alloc_skb(MWL8K_RX_MAXSZ);
		if (skb == NULL)
			break;

		rxq->rx_desc_count++;

		rx = rxq->rx_tail;
		rxq->rx_tail = (rx + 1) % MWL8K_RX_DESCS;

		rxq->rx_desc_area[rx].pkt_phys_addr =
			cpu_to_le32(pci_map_single(priv->pdev, skb->data,
					MWL8K_RX_MAXSZ, DMA_FROM_DEVICE));

		rxq->rx_desc_area[rx].pkt_len = cpu_to_le16(MWL8K_RX_MAXSZ);
		rxq->rx_skb[rx] = skb;
		wmb();
		rxq->rx_desc_area[rx].rx_ctrl = 0;

		refilled++;
	}

	return refilled;
}

/* Must be called only when the card's reception is completely halted */
static void mwl8k_rxq_deinit(struct ieee80211_hw *hw, int index)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_rx_queue *rxq = priv->rxq + index;
	int i;

	for (i = 0; i < MWL8K_RX_DESCS; i++) {
		if (rxq->rx_skb[i] != NULL) {
			unsigned long addr;

			addr = le32_to_cpu(rxq->rx_desc_area[i].pkt_phys_addr);
			pci_unmap_single(priv->pdev, addr, MWL8K_RX_MAXSZ,
					 PCI_DMA_FROMDEVICE);
			kfree_skb(rxq->rx_skb[i]);
			rxq->rx_skb[i] = NULL;
		}
	}

	kfree(rxq->rx_skb);
	rxq->rx_skb = NULL;

	pci_free_consistent(priv->pdev,
			    MWL8K_RX_DESCS * sizeof(struct mwl8k_rx_desc),
			    rxq->rx_desc_area, rxq->rx_desc_dma);
	rxq->rx_desc_area = NULL;
}


/*
 * Scan a list of BSSIDs to process for finalize join.
 * Allows for extension to process multiple BSSIDs.
 */
static inline int
mwl8k_capture_bssid(struct mwl8k_priv *priv, struct ieee80211_hdr *wh)
{
	return priv->capture_beacon &&
		ieee80211_is_beacon(wh->frame_control) &&
		!compare_ether_addr(wh->addr3, priv->capture_bssid);
}

static inline void mwl8k_save_beacon(struct ieee80211_hw *hw,
				     struct sk_buff *skb)
{
	struct mwl8k_priv *priv = hw->priv;

	priv->capture_beacon = false;
	memset(priv->capture_bssid, 0, ETH_ALEN);

	/*
	 * Use GFP_ATOMIC as rxq_process is called from
	 * the primary interrupt handler, memory allocation call
	 * must not sleep.
	 */
	priv->beacon_skb = skb_copy(skb, GFP_ATOMIC);
	if (priv->beacon_skb != NULL)
		ieee80211_queue_work(hw, &priv->finalize_join_worker);
}

static int rxq_process(struct ieee80211_hw *hw, int index, int limit)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_rx_queue *rxq = priv->rxq + index;
	int processed;

	processed = 0;
	while (rxq->rx_desc_count && limit--) {
		struct mwl8k_rx_desc *rx_desc;
		struct sk_buff *skb;
		struct ieee80211_rx_status status;
		unsigned long addr;
		struct ieee80211_hdr *wh;
		u16 rate_info;

		rx_desc = rxq->rx_desc_area + rxq->rx_head;
		if (!(rx_desc->rx_ctrl & MWL8K_RX_CTRL_OWNED_BY_HOST))
			break;
		rmb();

		skb = rxq->rx_skb[rxq->rx_head];
		if (skb == NULL)
			break;
		rxq->rx_skb[rxq->rx_head] = NULL;

		rxq->rx_head = (rxq->rx_head + 1) % MWL8K_RX_DESCS;
		rxq->rx_desc_count--;

		addr = le32_to_cpu(rx_desc->pkt_phys_addr);
		pci_unmap_single(priv->pdev, addr,
					MWL8K_RX_MAXSZ, PCI_DMA_FROMDEVICE);

		skb_put(skb, le16_to_cpu(rx_desc->pkt_len));
		mwl8k_remove_dma_header(skb);

		wh = (struct ieee80211_hdr *)skb->data;

		/*
		 * Check for a pending join operation.  Save a
		 * copy of the beacon and schedule a tasklet to
		 * send a FINALIZE_JOIN command to the firmware.
		 */
		if (mwl8k_capture_bssid(priv, wh))
			mwl8k_save_beacon(hw, skb);

		rate_info = le16_to_cpu(rx_desc->rate_info);

		memset(&status, 0, sizeof(status));
		status.mactime = 0;
		status.signal = -rx_desc->rssi;
		status.noise = -rx_desc->noise_level;
		status.qual = rx_desc->link_quality;
		status.antenna = RATE_INFO_ANTSELECT(rate_info);
		status.rate_idx = RATE_INFO_RATEID(rate_info);
		status.flag = 0;
		if (rate_info & RATE_INFO_SHORTPRE)
			status.flag |= RX_FLAG_SHORTPRE;
		if (rate_info & RATE_INFO_40MHZ)
			status.flag |= RX_FLAG_40MHZ;
		if (rate_info & RATE_INFO_SHORTGI)
			status.flag |= RX_FLAG_SHORT_GI;
		if (rate_info & RATE_INFO_MCS_FORMAT)
			status.flag |= RX_FLAG_HT;
		status.band = IEEE80211_BAND_2GHZ;
		status.freq = ieee80211_channel_to_frequency(rx_desc->channel);
		memcpy(IEEE80211_SKB_RXCB(skb), &status, sizeof(status));
		ieee80211_rx_irqsafe(hw, skb);

		processed++;
	}

	return processed;
}


/*
 * Packet transmission.
 */

/* Transmit packet ACK policy */
#define MWL8K_TXD_ACK_POLICY_NORMAL		0
#define MWL8K_TXD_ACK_POLICY_BLOCKACK		3

#define MWL8K_TXD_STATUS_OK			0x00000001
#define MWL8K_TXD_STATUS_OK_RETRY		0x00000002
#define MWL8K_TXD_STATUS_OK_MORE_RETRY		0x00000004
#define MWL8K_TXD_STATUS_MULTICAST_TX		0x00000008
#define MWL8K_TXD_STATUS_FW_OWNED		0x80000000

struct mwl8k_tx_desc {
	__le32 status;
	__u8 data_rate;
	__u8 tx_priority;
	__le16 qos_control;
	__le32 pkt_phys_addr;
	__le16 pkt_len;
	__u8 dest_MAC_addr[ETH_ALEN];
	__le32 next_tx_desc_phys_addr;
	__le32 reserved;
	__le16 rate_info;
	__u8 peer_id;
	__u8 tx_frag_cnt;
} __attribute__((packed));

#define MWL8K_TX_DESCS		128

static int mwl8k_txq_init(struct ieee80211_hw *hw, int index)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_tx_queue *txq = priv->txq + index;
	int size;
	int i;

	memset(&txq->tx_stats, 0, sizeof(struct ieee80211_tx_queue_stats));
	txq->tx_stats.limit = MWL8K_TX_DESCS;
	txq->tx_head = 0;
	txq->tx_tail = 0;

	size = MWL8K_TX_DESCS * sizeof(struct mwl8k_tx_desc);

	txq->tx_desc_area =
		pci_alloc_consistent(priv->pdev, size, &txq->tx_desc_dma);
	if (txq->tx_desc_area == NULL) {
		printk(KERN_ERR "%s: failed to alloc TX descriptors\n",
		       wiphy_name(hw->wiphy));
		return -ENOMEM;
	}
	memset(txq->tx_desc_area, 0, size);

	txq->tx_skb = kmalloc(MWL8K_TX_DESCS * sizeof(*txq->tx_skb),
								GFP_KERNEL);
	if (txq->tx_skb == NULL) {
		printk(KERN_ERR "%s: failed to alloc TX skbuff list\n",
		       wiphy_name(hw->wiphy));
		pci_free_consistent(priv->pdev, size,
				    txq->tx_desc_area, txq->tx_desc_dma);
		return -ENOMEM;
	}
	memset(txq->tx_skb, 0, MWL8K_TX_DESCS * sizeof(*txq->tx_skb));

	for (i = 0; i < MWL8K_TX_DESCS; i++) {
		struct mwl8k_tx_desc *tx_desc;
		int nexti;

		tx_desc = txq->tx_desc_area + i;
		nexti = (i + 1) % MWL8K_TX_DESCS;

		tx_desc->status = 0;
		tx_desc->next_tx_desc_phys_addr =
			cpu_to_le32(txq->tx_desc_dma +
						nexti * sizeof(*tx_desc));
	}

	return 0;
}

static inline void mwl8k_tx_start(struct mwl8k_priv *priv)
{
	iowrite32(MWL8K_H2A_INT_PPA_READY,
		priv->regs + MWL8K_HIU_H2A_INTERRUPT_EVENTS);
	iowrite32(MWL8K_H2A_INT_DUMMY,
		priv->regs + MWL8K_HIU_H2A_INTERRUPT_EVENTS);
	ioread32(priv->regs + MWL8K_HIU_INT_CODE);
}

struct mwl8k_txq_info {
	u32 fw_owned;
	u32 drv_owned;
	u32 unused;
	u32 len;
	u32 head;
	u32 tail;
};

static int mwl8k_scan_tx_ring(struct mwl8k_priv *priv,
				struct mwl8k_txq_info *txinfo)
{
	int count, desc, status;
	struct mwl8k_tx_queue *txq;
	struct mwl8k_tx_desc *tx_desc;
	int ndescs = 0;

	memset(txinfo, 0, MWL8K_TX_QUEUES * sizeof(struct mwl8k_txq_info));

	for (count = 0; count < MWL8K_TX_QUEUES; count++) {
		txq = priv->txq + count;
		txinfo[count].len = txq->tx_stats.len;
		txinfo[count].head = txq->tx_head;
		txinfo[count].tail = txq->tx_tail;
		for (desc = 0; desc < MWL8K_TX_DESCS; desc++) {
			tx_desc = txq->tx_desc_area + desc;
			status = le32_to_cpu(tx_desc->status);

			if (status & MWL8K_TXD_STATUS_FW_OWNED)
				txinfo[count].fw_owned++;
			else
				txinfo[count].drv_owned++;

			if (tx_desc->pkt_len == 0)
				txinfo[count].unused++;
		}
	}

	return ndescs;
}

/*
 * Must be called with priv->fw_mutex held and tx queues stopped.
 */
static int mwl8k_tx_wait_empty(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;
	DECLARE_COMPLETION_ONSTACK(tx_wait);
	u32 count;
	unsigned long timeout;

	might_sleep();

	spin_lock_bh(&priv->tx_lock);
	count = priv->pending_tx_pkts;
	if (count)
		priv->tx_wait = &tx_wait;
	spin_unlock_bh(&priv->tx_lock);

	if (count) {
		struct mwl8k_txq_info txinfo[MWL8K_TX_QUEUES];
		int index;
		int newcount;

		timeout = wait_for_completion_timeout(&tx_wait,
					msecs_to_jiffies(5000));
		if (timeout)
			return 0;

		spin_lock_bh(&priv->tx_lock);
		priv->tx_wait = NULL;
		newcount = priv->pending_tx_pkts;
		mwl8k_scan_tx_ring(priv, txinfo);
		spin_unlock_bh(&priv->tx_lock);

		printk(KERN_ERR "%s(%u) TIMEDOUT:5000ms Pend:%u-->%u\n",
		       __func__, __LINE__, count, newcount);

		for (index = 0; index < MWL8K_TX_QUEUES; index++)
			printk(KERN_ERR "TXQ:%u L:%u H:%u T:%u FW:%u "
			       "DRV:%u U:%u\n",
					index,
					txinfo[index].len,
					txinfo[index].head,
					txinfo[index].tail,
					txinfo[index].fw_owned,
					txinfo[index].drv_owned,
					txinfo[index].unused);

		return -ETIMEDOUT;
	}

	return 0;
}

#define MWL8K_TXD_SUCCESS(status)				\
	((status) & (MWL8K_TXD_STATUS_OK |			\
		     MWL8K_TXD_STATUS_OK_RETRY |		\
		     MWL8K_TXD_STATUS_OK_MORE_RETRY))

static void mwl8k_txq_reclaim(struct ieee80211_hw *hw, int index, int force)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_tx_queue *txq = priv->txq + index;
	int wake = 0;

	while (txq->tx_stats.len > 0) {
		int tx;
		struct mwl8k_tx_desc *tx_desc;
		unsigned long addr;
		int size;
		struct sk_buff *skb;
		struct ieee80211_tx_info *info;
		u32 status;

		tx = txq->tx_head;
		tx_desc = txq->tx_desc_area + tx;

		status = le32_to_cpu(tx_desc->status);

		if (status & MWL8K_TXD_STATUS_FW_OWNED) {
			if (!force)
				break;
			tx_desc->status &=
				~cpu_to_le32(MWL8K_TXD_STATUS_FW_OWNED);
		}

		txq->tx_head = (tx + 1) % MWL8K_TX_DESCS;
		BUG_ON(txq->tx_stats.len == 0);
		txq->tx_stats.len--;
		priv->pending_tx_pkts--;

		addr = le32_to_cpu(tx_desc->pkt_phys_addr);
		size = le16_to_cpu(tx_desc->pkt_len);
		skb = txq->tx_skb[tx];
		txq->tx_skb[tx] = NULL;

		BUG_ON(skb == NULL);
		pci_unmap_single(priv->pdev, addr, size, PCI_DMA_TODEVICE);

		mwl8k_remove_dma_header(skb);

		/* Mark descriptor as unused */
		tx_desc->pkt_phys_addr = 0;
		tx_desc->pkt_len = 0;

		info = IEEE80211_SKB_CB(skb);
		ieee80211_tx_info_clear_status(info);
		if (MWL8K_TXD_SUCCESS(status))
			info->flags |= IEEE80211_TX_STAT_ACK;

		ieee80211_tx_status_irqsafe(hw, skb);

		wake = 1;
	}

	if (wake && priv->radio_on && !mutex_is_locked(&priv->fw_mutex))
		ieee80211_wake_queue(hw, index);
}

/* must be called only when the card's transmit is completely halted */
static void mwl8k_txq_deinit(struct ieee80211_hw *hw, int index)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_tx_queue *txq = priv->txq + index;

	mwl8k_txq_reclaim(hw, index, 1);

	kfree(txq->tx_skb);
	txq->tx_skb = NULL;

	pci_free_consistent(priv->pdev,
			    MWL8K_TX_DESCS * sizeof(struct mwl8k_tx_desc),
			    txq->tx_desc_area, txq->tx_desc_dma);
	txq->tx_desc_area = NULL;
}

static int
mwl8k_txq_xmit(struct ieee80211_hw *hw, int index, struct sk_buff *skb)
{
	struct mwl8k_priv *priv = hw->priv;
	struct ieee80211_tx_info *tx_info;
	struct mwl8k_vif *mwl8k_vif;
	struct ieee80211_hdr *wh;
	struct mwl8k_tx_queue *txq;
	struct mwl8k_tx_desc *tx;
	dma_addr_t dma;
	u32 txstatus;
	u8 txdatarate;
	u16 qos;

	wh = (struct ieee80211_hdr *)skb->data;
	if (ieee80211_is_data_qos(wh->frame_control))
		qos = le16_to_cpu(*((__le16 *)ieee80211_get_qos_ctl(wh)));
	else
		qos = 0;

	mwl8k_add_dma_header(skb);
	wh = &((struct mwl8k_dma_data *)skb->data)->wh;

	tx_info = IEEE80211_SKB_CB(skb);
	mwl8k_vif = MWL8K_VIF(tx_info->control.vif);

	if (tx_info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		u16 seqno = mwl8k_vif->seqno;

		wh->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		wh->seq_ctrl |= cpu_to_le16(seqno << 4);
		mwl8k_vif->seqno = seqno++ % 4096;
	}

	/* Setup firmware control bit fields for each frame type.  */
	txstatus = 0;
	txdatarate = 0;
	if (ieee80211_is_mgmt(wh->frame_control) ||
	    ieee80211_is_ctl(wh->frame_control)) {
		txdatarate = 0;
		qos = mwl8k_qos_setbit_eosp(qos);
		/* Set Queue size to unspecified */
		qos = mwl8k_qos_setbit_qlen(qos, 0xff);
	} else if (ieee80211_is_data(wh->frame_control)) {
		txdatarate = 1;
		if (is_multicast_ether_addr(wh->addr1))
			txstatus |= MWL8K_TXD_STATUS_MULTICAST_TX;

		/* Send pkt in an aggregate if AMPDU frame.  */
		if (tx_info->flags & IEEE80211_TX_CTL_AMPDU)
			qos = mwl8k_qos_setbit_ack(qos,
				MWL8K_TXD_ACK_POLICY_BLOCKACK);
		else
			qos = mwl8k_qos_setbit_ack(qos,
				MWL8K_TXD_ACK_POLICY_NORMAL);

		if (qos & IEEE80211_QOS_CONTROL_A_MSDU_PRESENT)
			qos = mwl8k_qos_setbit_amsdu(qos);
	}

	dma = pci_map_single(priv->pdev, skb->data,
				skb->len, PCI_DMA_TODEVICE);

	if (pci_dma_mapping_error(priv->pdev, dma)) {
		printk(KERN_DEBUG "%s: failed to dma map skb, "
		       "dropping TX frame.\n", wiphy_name(hw->wiphy));
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	spin_lock_bh(&priv->tx_lock);

	txq = priv->txq + index;

	BUG_ON(txq->tx_skb[txq->tx_tail] != NULL);
	txq->tx_skb[txq->tx_tail] = skb;

	tx = txq->tx_desc_area + txq->tx_tail;
	tx->data_rate = txdatarate;
	tx->tx_priority = index;
	tx->qos_control = cpu_to_le16(qos);
	tx->pkt_phys_addr = cpu_to_le32(dma);
	tx->pkt_len = cpu_to_le16(skb->len);
	tx->rate_info = 0;
	tx->peer_id = mwl8k_vif->peer_id;
	wmb();
	tx->status = cpu_to_le32(MWL8K_TXD_STATUS_FW_OWNED | txstatus);

	txq->tx_stats.count++;
	txq->tx_stats.len++;
	priv->pending_tx_pkts++;

	txq->tx_tail++;
	if (txq->tx_tail == MWL8K_TX_DESCS)
		txq->tx_tail = 0;

	if (txq->tx_head == txq->tx_tail)
		ieee80211_stop_queue(hw, index);

	mwl8k_tx_start(priv);

	spin_unlock_bh(&priv->tx_lock);

	return NETDEV_TX_OK;
}


/*
 * Firmware access.
 *
 * We have the following requirements for issuing firmware commands:
 * - Some commands require that the packet transmit path is idle when
 *   the command is issued.  (For simplicity, we'll just quiesce the
 *   transmit path for every command.)
 * - There are certain sequences of commands that need to be issued to
 *   the hardware sequentially, with no other intervening commands.
 *
 * This leads to an implementation of a "firmware lock" as a mutex that
 * can be taken recursively, and which is taken by both the low-level
 * command submission function (mwl8k_post_cmd) as well as any users of
 * that function that require issuing of an atomic sequence of commands,
 * and quiesces the transmit path whenever it's taken.
 */
static int mwl8k_fw_lock(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;

	if (priv->fw_mutex_owner != current) {
		int rc;

		mutex_lock(&priv->fw_mutex);
		ieee80211_stop_queues(hw);

		rc = mwl8k_tx_wait_empty(hw);
		if (rc) {
			ieee80211_wake_queues(hw);
			mutex_unlock(&priv->fw_mutex);

			return rc;
		}

		priv->fw_mutex_owner = current;
	}

	priv->fw_mutex_depth++;

	return 0;
}

static void mwl8k_fw_unlock(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;

	if (!--priv->fw_mutex_depth) {
		ieee80211_wake_queues(hw);
		priv->fw_mutex_owner = NULL;
		mutex_unlock(&priv->fw_mutex);
	}
}


/*
 * Command processing.
 */

/* Timeout firmware commands after 2000ms */
#define MWL8K_CMD_TIMEOUT_MS	2000

static int mwl8k_post_cmd(struct ieee80211_hw *hw, struct mwl8k_cmd_pkt *cmd)
{
	DECLARE_COMPLETION_ONSTACK(cmd_wait);
	struct mwl8k_priv *priv = hw->priv;
	void __iomem *regs = priv->regs;
	dma_addr_t dma_addr;
	unsigned int dma_size;
	int rc;
	unsigned long timeout = 0;
	u8 buf[32];

	cmd->result = 0xffff;
	dma_size = le16_to_cpu(cmd->length);
	dma_addr = pci_map_single(priv->pdev, cmd, dma_size,
				  PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(priv->pdev, dma_addr))
		return -ENOMEM;

	rc = mwl8k_fw_lock(hw);
	if (rc) {
		pci_unmap_single(priv->pdev, dma_addr, dma_size,
						PCI_DMA_BIDIRECTIONAL);
		return rc;
	}

	priv->hostcmd_wait = &cmd_wait;
	iowrite32(dma_addr, regs + MWL8K_HIU_GEN_PTR);
	iowrite32(MWL8K_H2A_INT_DOORBELL,
		regs + MWL8K_HIU_H2A_INTERRUPT_EVENTS);
	iowrite32(MWL8K_H2A_INT_DUMMY,
		regs + MWL8K_HIU_H2A_INTERRUPT_EVENTS);

	timeout = wait_for_completion_timeout(&cmd_wait,
				msecs_to_jiffies(MWL8K_CMD_TIMEOUT_MS));

	priv->hostcmd_wait = NULL;

	mwl8k_fw_unlock(hw);

	pci_unmap_single(priv->pdev, dma_addr, dma_size,
					PCI_DMA_BIDIRECTIONAL);

	if (!timeout) {
		printk(KERN_ERR "%s: Command %s timeout after %u ms\n",
		       wiphy_name(hw->wiphy),
		       mwl8k_cmd_name(cmd->code, buf, sizeof(buf)),
		       MWL8K_CMD_TIMEOUT_MS);
		rc = -ETIMEDOUT;
	} else {
		rc = cmd->result ? -EINVAL : 0;
		if (rc)
			printk(KERN_ERR "%s: Command %s error 0x%x\n",
			       wiphy_name(hw->wiphy),
			       mwl8k_cmd_name(cmd->code, buf, sizeof(buf)),
			       le16_to_cpu(cmd->result));
	}

	return rc;
}

/*
 * GET_HW_SPEC.
 */
struct mwl8k_cmd_get_hw_spec {
	struct mwl8k_cmd_pkt header;
	__u8 hw_rev;
	__u8 host_interface;
	__le16 num_mcaddrs;
	__u8 perm_addr[ETH_ALEN];
	__le16 region_code;
	__le32 fw_rev;
	__le32 ps_cookie;
	__le32 caps;
	__u8 mcs_bitmap[16];
	__le32 rx_queue_ptr;
	__le32 num_tx_queues;
	__le32 tx_queue_ptrs[MWL8K_TX_QUEUES];
	__le32 caps2;
	__le32 num_tx_desc_per_queue;
	__le32 total_rx_desc;
} __attribute__((packed));

static int mwl8k_cmd_get_hw_spec(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_get_hw_spec *cmd;
	int rc;
	int i;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_GET_HW_SPEC);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));

	memset(cmd->perm_addr, 0xff, sizeof(cmd->perm_addr));
	cmd->ps_cookie = cpu_to_le32(priv->cookie_dma);
	cmd->rx_queue_ptr = cpu_to_le32(priv->rxq[0].rx_desc_dma);
	cmd->num_tx_queues = cpu_to_le32(MWL8K_TX_QUEUES);
	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		cmd->tx_queue_ptrs[i] = cpu_to_le32(priv->txq[i].tx_desc_dma);
	cmd->num_tx_desc_per_queue = cpu_to_le32(MWL8K_TX_DESCS);
	cmd->total_rx_desc = cpu_to_le32(MWL8K_RX_DESCS);

	rc = mwl8k_post_cmd(hw, &cmd->header);

	if (!rc) {
		SET_IEEE80211_PERM_ADDR(hw, cmd->perm_addr);
		priv->num_mcaddrs = le16_to_cpu(cmd->num_mcaddrs);
		priv->fw_rev = le32_to_cpu(cmd->fw_rev);
		priv->hw_rev = cmd->hw_rev;
	}

	kfree(cmd);
	return rc;
}

/*
 * CMD_MAC_MULTICAST_ADR.
 */
struct mwl8k_cmd_mac_multicast_adr {
	struct mwl8k_cmd_pkt header;
	__le16 action;
	__le16 numaddr;
	__u8 addr[0][ETH_ALEN];
};

#define MWL8K_ENABLE_RX_DIRECTED	0x0001
#define MWL8K_ENABLE_RX_MULTICAST	0x0002
#define MWL8K_ENABLE_RX_ALL_MULTICAST	0x0004
#define MWL8K_ENABLE_RX_BROADCAST	0x0008

static struct mwl8k_cmd_pkt *
__mwl8k_cmd_mac_multicast_adr(struct ieee80211_hw *hw, int allmulti,
			      int mc_count, struct dev_addr_list *mclist)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_mac_multicast_adr *cmd;
	int size;

	if (allmulti || mc_count > priv->num_mcaddrs) {
		allmulti = 1;
		mc_count = 0;
	}

	size = sizeof(*cmd) + mc_count * ETH_ALEN;

	cmd = kzalloc(size, GFP_ATOMIC);
	if (cmd == NULL)
		return NULL;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_MAC_MULTICAST_ADR);
	cmd->header.length = cpu_to_le16(size);
	cmd->action = cpu_to_le16(MWL8K_ENABLE_RX_DIRECTED |
				  MWL8K_ENABLE_RX_BROADCAST);

	if (allmulti) {
		cmd->action |= cpu_to_le16(MWL8K_ENABLE_RX_ALL_MULTICAST);
	} else if (mc_count) {
		int i;

		cmd->action |= cpu_to_le16(MWL8K_ENABLE_RX_MULTICAST);
		cmd->numaddr = cpu_to_le16(mc_count);
		for (i = 0; i < mc_count && mclist; i++) {
			if (mclist->da_addrlen != ETH_ALEN) {
				kfree(cmd);
				return NULL;
			}
			memcpy(cmd->addr[i], mclist->da_addr, ETH_ALEN);
			mclist = mclist->next;
		}
	}

	return &cmd->header;
}

/*
 * CMD_802_11_GET_STAT.
 */
struct mwl8k_cmd_802_11_get_stat {
	struct mwl8k_cmd_pkt header;
	__le32 stats[64];
} __attribute__((packed));

#define MWL8K_STAT_ACK_FAILURE	9
#define MWL8K_STAT_RTS_FAILURE	12
#define MWL8K_STAT_FCS_ERROR	24
#define MWL8K_STAT_RTS_SUCCESS	11

static int mwl8k_cmd_802_11_get_stat(struct ieee80211_hw *hw,
				struct ieee80211_low_level_stats *stats)
{
	struct mwl8k_cmd_802_11_get_stat *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_GET_STAT);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));

	rc = mwl8k_post_cmd(hw, &cmd->header);
	if (!rc) {
		stats->dot11ACKFailureCount =
			le32_to_cpu(cmd->stats[MWL8K_STAT_ACK_FAILURE]);
		stats->dot11RTSFailureCount =
			le32_to_cpu(cmd->stats[MWL8K_STAT_RTS_FAILURE]);
		stats->dot11FCSErrorCount =
			le32_to_cpu(cmd->stats[MWL8K_STAT_FCS_ERROR]);
		stats->dot11RTSSuccessCount =
			le32_to_cpu(cmd->stats[MWL8K_STAT_RTS_SUCCESS]);
	}
	kfree(cmd);

	return rc;
}

/*
 * CMD_802_11_RADIO_CONTROL.
 */
struct mwl8k_cmd_802_11_radio_control {
	struct mwl8k_cmd_pkt header;
	__le16 action;
	__le16 control;
	__le16 radio_on;
} __attribute__((packed));

static int
mwl8k_cmd_802_11_radio_control(struct ieee80211_hw *hw, bool enable, bool force)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_802_11_radio_control *cmd;
	int rc;

	if (enable == priv->radio_on && !force)
		return 0;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_RADIO_CONTROL);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le16(MWL8K_CMD_SET);
	cmd->control = cpu_to_le16(priv->radio_short_preamble ? 3 : 1);
	cmd->radio_on = cpu_to_le16(enable ? 0x0001 : 0x0000);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	if (!rc)
		priv->radio_on = enable;

	return rc;
}

static int mwl8k_cmd_802_11_radio_disable(struct ieee80211_hw *hw)
{
	return mwl8k_cmd_802_11_radio_control(hw, 0, 0);
}

static int mwl8k_cmd_802_11_radio_enable(struct ieee80211_hw *hw)
{
	return mwl8k_cmd_802_11_radio_control(hw, 1, 0);
}

static int
mwl8k_set_radio_preamble(struct ieee80211_hw *hw, bool short_preamble)
{
	struct mwl8k_priv *priv;

	if (hw == NULL || hw->priv == NULL)
		return -EINVAL;
	priv = hw->priv;

	priv->radio_short_preamble = short_preamble;

	return mwl8k_cmd_802_11_radio_control(hw, 1, 1);
}

/*
 * CMD_802_11_RF_TX_POWER.
 */
#define MWL8K_TX_POWER_LEVEL_TOTAL	8

struct mwl8k_cmd_802_11_rf_tx_power {
	struct mwl8k_cmd_pkt header;
	__le16 action;
	__le16 support_level;
	__le16 current_level;
	__le16 reserved;
	__le16 power_level_list[MWL8K_TX_POWER_LEVEL_TOTAL];
} __attribute__((packed));

static int mwl8k_cmd_802_11_rf_tx_power(struct ieee80211_hw *hw, int dBm)
{
	struct mwl8k_cmd_802_11_rf_tx_power *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_RF_TX_POWER);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le16(MWL8K_CMD_SET);
	cmd->support_level = cpu_to_le16(dBm);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_PRE_SCAN.
 */
struct mwl8k_cmd_set_pre_scan {
	struct mwl8k_cmd_pkt header;
} __attribute__((packed));

static int mwl8k_cmd_set_pre_scan(struct ieee80211_hw *hw)
{
	struct mwl8k_cmd_set_pre_scan *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_PRE_SCAN);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_POST_SCAN.
 */
struct mwl8k_cmd_set_post_scan {
	struct mwl8k_cmd_pkt header;
	__le32 isibss;
	__u8 bssid[ETH_ALEN];
} __attribute__((packed));

static int
mwl8k_cmd_set_post_scan(struct ieee80211_hw *hw, __u8 *mac)
{
	struct mwl8k_cmd_set_post_scan *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_POST_SCAN);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->isibss = 0;
	memcpy(cmd->bssid, mac, ETH_ALEN);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_RF_CHANNEL.
 */
struct mwl8k_cmd_set_rf_channel {
	struct mwl8k_cmd_pkt header;
	__le16 action;
	__u8 current_channel;
	__le32 channel_flags;
} __attribute__((packed));

static int mwl8k_cmd_set_rf_channel(struct ieee80211_hw *hw,
				    struct ieee80211_channel *channel)
{
	struct mwl8k_cmd_set_rf_channel *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_RF_CHANNEL);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le16(MWL8K_CMD_SET);
	cmd->current_channel = channel->hw_value;
	if (channel->band == IEEE80211_BAND_2GHZ)
		cmd->channel_flags = cpu_to_le32(0x00000081);
	else
		cmd->channel_flags = cpu_to_le32(0x00000000);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_SLOT.
 */
struct mwl8k_cmd_set_slot {
	struct mwl8k_cmd_pkt header;
	__le16 action;
	__u8 short_slot;
} __attribute__((packed));

static int mwl8k_cmd_set_slot(struct ieee80211_hw *hw, bool short_slot_time)
{
	struct mwl8k_cmd_set_slot *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_SLOT);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le16(MWL8K_CMD_SET);
	cmd->short_slot = short_slot_time;

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_MIMO_CONFIG.
 */
struct mwl8k_cmd_mimo_config {
	struct mwl8k_cmd_pkt header;
	__le32 action;
	__u8 rx_antenna_map;
	__u8 tx_antenna_map;
} __attribute__((packed));

static int mwl8k_cmd_mimo_config(struct ieee80211_hw *hw, __u8 rx, __u8 tx)
{
	struct mwl8k_cmd_mimo_config *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_MIMO_CONFIG);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le32((u32)MWL8K_CMD_SET);
	cmd->rx_antenna_map = rx;
	cmd->tx_antenna_map = tx;

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_ENABLE_SNIFFER.
 */
struct mwl8k_cmd_enable_sniffer {
	struct mwl8k_cmd_pkt header;
	__le32 action;
} __attribute__((packed));

static int mwl8k_enable_sniffer(struct ieee80211_hw *hw, bool enable)
{
	struct mwl8k_cmd_enable_sniffer *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_ENABLE_SNIFFER);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le32(!!enable);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_MAC_ADDR.
 */
struct mwl8k_cmd_set_mac_addr {
	struct mwl8k_cmd_pkt header;
	__u8 mac_addr[ETH_ALEN];
} __attribute__((packed));

static int mwl8k_set_mac_addr(struct ieee80211_hw *hw, u8 *mac)
{
	struct mwl8k_cmd_set_mac_addr *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_MAC_ADDR);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	memcpy(cmd->mac_addr, mac, ETH_ALEN);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}


/*
 * CMD_SET_RATEADAPT_MODE.
 */
struct mwl8k_cmd_set_rate_adapt_mode {
	struct mwl8k_cmd_pkt header;
	__le16 action;
	__le16 mode;
} __attribute__((packed));

static int mwl8k_cmd_setrateadaptmode(struct ieee80211_hw *hw, __u16 mode)
{
	struct mwl8k_cmd_set_rate_adapt_mode *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_RATEADAPT_MODE);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le16(MWL8K_CMD_SET);
	cmd->mode = cpu_to_le16(mode);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_WMM_MODE.
 */
struct mwl8k_cmd_set_wmm {
	struct mwl8k_cmd_pkt header;
	__le16 action;
} __attribute__((packed));

static int mwl8k_set_wmm(struct ieee80211_hw *hw, bool enable)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_set_wmm *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_WMM_MODE);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le16(!!enable);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	if (!rc)
		priv->wmm_enabled = enable;

	return rc;
}

/*
 * CMD_SET_RTS_THRESHOLD.
 */
struct mwl8k_cmd_rts_threshold {
	struct mwl8k_cmd_pkt header;
	__le16 action;
	__le16 threshold;
} __attribute__((packed));

static int mwl8k_rts_threshold(struct ieee80211_hw *hw,
			       u16 action, u16 threshold)
{
	struct mwl8k_cmd_rts_threshold *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_RTS_THRESHOLD);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le16(action);
	cmd->threshold = cpu_to_le16(threshold);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_EDCA_PARAMS.
 */
struct mwl8k_cmd_set_edca_params {
	struct mwl8k_cmd_pkt header;

	/* See MWL8K_SET_EDCA_XXX below */
	__le16 action;

	/* TX opportunity in units of 32 us */
	__le16 txop;

	/* Log exponent of max contention period: 0...15*/
	__u8 log_cw_max;

	/* Log exponent of min contention period: 0...15 */
	__u8 log_cw_min;

	/* Adaptive interframe spacing in units of 32us */
	__u8 aifs;

	/* TX queue to configure */
	__u8 txq;
} __attribute__((packed));

#define MWL8K_SET_EDCA_CW	0x01
#define MWL8K_SET_EDCA_TXOP	0x02
#define MWL8K_SET_EDCA_AIFS	0x04

#define MWL8K_SET_EDCA_ALL	(MWL8K_SET_EDCA_CW | \
				 MWL8K_SET_EDCA_TXOP | \
				 MWL8K_SET_EDCA_AIFS)

static int
mwl8k_set_edca_params(struct ieee80211_hw *hw, __u8 qnum,
		__u16 cw_min, __u16 cw_max,
		__u8 aifs, __u16 txop)
{
	struct mwl8k_cmd_set_edca_params *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	/*
	 * Queues 0 (BE) and 1 (BK) are swapped in hardware for
	 * this call.
	 */
	qnum ^= !(qnum >> 1);

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_EDCA_PARAMS);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le16(MWL8K_SET_EDCA_ALL);
	cmd->txop = cpu_to_le16(txop);
	cmd->log_cw_max = (u8)ilog2(cw_max + 1);
	cmd->log_cw_min = (u8)ilog2(cw_min + 1);
	cmd->aifs = aifs;
	cmd->txq = qnum;

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_FINALIZE_JOIN.
 */

/* FJ beacon buffer size is compiled into the firmware.  */
#define MWL8K_FJ_BEACON_MAXLEN	128

struct mwl8k_cmd_finalize_join {
	struct mwl8k_cmd_pkt header;
	__le32 sleep_interval;	/* Number of beacon periods to sleep */
	__u8 beacon_data[MWL8K_FJ_BEACON_MAXLEN];
} __attribute__((packed));

static int mwl8k_finalize_join(struct ieee80211_hw *hw, void *frame,
				__u16 framelen, __u16 dtim)
{
	struct mwl8k_cmd_finalize_join *cmd;
	struct ieee80211_mgmt *payload = frame;
	u16 hdrlen;
	u32 payload_len;
	int rc;

	if (frame == NULL)
		return -EINVAL;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_FINALIZE_JOIN);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->sleep_interval = cpu_to_le32(dtim ? dtim : 1);

	hdrlen = ieee80211_hdrlen(payload->frame_control);

	payload_len = framelen > hdrlen ? framelen - hdrlen : 0;

	/* XXX TBD Might just have to abort and return an error */
	if (payload_len > MWL8K_FJ_BEACON_MAXLEN)
		printk(KERN_ERR "%s(): WARNING: Incomplete beacon "
		       "sent to firmware. Sz=%u MAX=%u\n", __func__,
		       payload_len, MWL8K_FJ_BEACON_MAXLEN);

	if (payload_len > MWL8K_FJ_BEACON_MAXLEN)
		payload_len = MWL8K_FJ_BEACON_MAXLEN;

	if (payload && payload_len)
		memcpy(cmd->beacon_data, &payload->u.beacon, payload_len);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);
	return rc;
}

/*
 * CMD_UPDATE_STADB.
 */
struct mwl8k_cmd_update_sta_db {
	struct mwl8k_cmd_pkt header;

	/* See STADB_ACTION_TYPE */
	__le32	action;

	/* Peer MAC address */
	__u8	peer_addr[ETH_ALEN];

	__le32	reserved;

	/* Peer info - valid during add/update.  */
	struct peer_capability_info	peer_info;
} __attribute__((packed));

static int mwl8k_cmd_update_sta_db(struct ieee80211_hw *hw,
		struct ieee80211_vif *vif, __u32 action)
{
	struct mwl8k_vif *mv_vif = MWL8K_VIF(vif);
	struct ieee80211_bss_conf *info = &mv_vif->bss_info;
	struct mwl8k_cmd_update_sta_db *cmd;
	struct peer_capability_info *peer_info;
	struct ieee80211_rate *bitrates = mv_vif->legacy_rates;
	int rc;
	__u8 count, *rates;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_UPDATE_STADB);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));

	cmd->action = cpu_to_le32(action);
	peer_info = &cmd->peer_info;
	memcpy(cmd->peer_addr, mv_vif->bssid, ETH_ALEN);

	switch (action) {
	case MWL8K_STA_DB_ADD_ENTRY:
	case MWL8K_STA_DB_MODIFY_ENTRY:
		/* Build peer_info block */
		peer_info->peer_type = MWL8K_PEER_TYPE_ACCESSPOINT;
		peer_info->basic_caps = cpu_to_le16(info->assoc_capability);
		peer_info->interop = 1;
		peer_info->amsdu_enabled = 0;

		rates = peer_info->legacy_rates;
		for (count = 0; count < mv_vif->legacy_nrates; count++)
			rates[count] = bitrates[count].hw_value;

		rc = mwl8k_post_cmd(hw, &cmd->header);
		if (rc == 0)
			mv_vif->peer_id = peer_info->station_id;

		break;

	case MWL8K_STA_DB_DEL_ENTRY:
	case MWL8K_STA_DB_FLUSH:
	default:
		rc = mwl8k_post_cmd(hw, &cmd->header);
		if (rc == 0)
			mv_vif->peer_id = 0;
		break;
	}
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_AID.
 */
#define MWL8K_RATE_INDEX_MAX_ARRAY			14

#define MWL8K_FRAME_PROT_DISABLED			0x00
#define MWL8K_FRAME_PROT_11G				0x07
#define MWL8K_FRAME_PROT_11N_HT_40MHZ_ONLY		0x02
#define MWL8K_FRAME_PROT_11N_HT_ALL			0x06

struct mwl8k_cmd_update_set_aid {
	struct	mwl8k_cmd_pkt header;
	__le16	aid;

	 /* AP's MAC address (BSSID) */
	__u8	bssid[ETH_ALEN];
	__le16	protection_mode;
	__u8	supp_rates[MWL8K_RATE_INDEX_MAX_ARRAY];
} __attribute__((packed));

static int mwl8k_cmd_set_aid(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif)
{
	struct mwl8k_vif *mv_vif = MWL8K_VIF(vif);
	struct ieee80211_bss_conf *info = &mv_vif->bss_info;
	struct mwl8k_cmd_update_set_aid *cmd;
	struct ieee80211_rate *bitrates = mv_vif->legacy_rates;
	int count;
	u16 prot_mode;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_AID);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->aid = cpu_to_le16(info->aid);

	memcpy(cmd->bssid, mv_vif->bssid, ETH_ALEN);

	if (info->use_cts_prot) {
		prot_mode = MWL8K_FRAME_PROT_11G;
	} else {
		switch (info->ht_operation_mode &
			IEEE80211_HT_OP_MODE_PROTECTION) {
		case IEEE80211_HT_OP_MODE_PROTECTION_20MHZ:
			prot_mode = MWL8K_FRAME_PROT_11N_HT_40MHZ_ONLY;
			break;
		case IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED:
			prot_mode = MWL8K_FRAME_PROT_11N_HT_ALL;
			break;
		default:
			prot_mode = MWL8K_FRAME_PROT_DISABLED;
			break;
		}
	}
	cmd->protection_mode = cpu_to_le16(prot_mode);

	for (count = 0; count < mv_vif->legacy_nrates; count++)
		cmd->supp_rates[count] = bitrates[count].hw_value;

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_RATE.
 */
struct mwl8k_cmd_update_rateset {
	struct	mwl8k_cmd_pkt header;
	__u8	legacy_rates[MWL8K_RATE_INDEX_MAX_ARRAY];

	/* Bitmap for supported MCS codes.  */
	__u8	mcs_set[MWL8K_IEEE_LEGACY_DATA_RATES];
	__u8	reserved[MWL8K_IEEE_LEGACY_DATA_RATES];
} __attribute__((packed));

static int mwl8k_update_rateset(struct ieee80211_hw *hw,
		struct ieee80211_vif *vif)
{
	struct mwl8k_vif *mv_vif = MWL8K_VIF(vif);
	struct mwl8k_cmd_update_rateset *cmd;
	struct ieee80211_rate *bitrates = mv_vif->legacy_rates;
	int count;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_RATE);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));

	for (count = 0; count < mv_vif->legacy_nrates; count++)
		cmd->legacy_rates[count] = bitrates[count].hw_value;

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_USE_FIXED_RATE.
 */
#define MWL8K_RATE_TABLE_SIZE	8
#define MWL8K_UCAST_RATE	0
#define MWL8K_USE_AUTO_RATE	0x0002

struct mwl8k_rate_entry {
	/* Set to 1 if HT rate, 0 if legacy.  */
	__le32	is_ht_rate;

	/* Set to 1 to use retry_count field.  */
	__le32	enable_retry;

	/* Specified legacy rate or MCS.  */
	__le32	rate;

	/* Number of allowed retries.  */
	__le32	retry_count;
} __attribute__((packed));

struct mwl8k_rate_table {
	/* 1 to allow specified rate and below */
	__le32	allow_rate_drop;
	__le32	num_rates;
	struct mwl8k_rate_entry rate_entry[MWL8K_RATE_TABLE_SIZE];
} __attribute__((packed));

struct mwl8k_cmd_use_fixed_rate {
	struct	mwl8k_cmd_pkt header;
	__le32	action;
	struct mwl8k_rate_table rate_table;

	/* Unicast, Broadcast or Multicast */
	__le32	rate_type;
	__le32	reserved1;
	__le32	reserved2;
} __attribute__((packed));

static int mwl8k_cmd_use_fixed_rate(struct ieee80211_hw *hw,
	u32 action, u32 rate_type, struct mwl8k_rate_table *rate_table)
{
	struct mwl8k_cmd_use_fixed_rate *cmd;
	int count;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_USE_FIXED_RATE);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));

	cmd->action = cpu_to_le32(action);
	cmd->rate_type = cpu_to_le32(rate_type);

	if (rate_table != NULL) {
		/*
		 * Copy over each field manually so that endian
		 * conversion can be done.
		 */
		cmd->rate_table.allow_rate_drop =
				cpu_to_le32(rate_table->allow_rate_drop);
		cmd->rate_table.num_rates =
				cpu_to_le32(rate_table->num_rates);

		for (count = 0; count < rate_table->num_rates; count++) {
			struct mwl8k_rate_entry *dst =
				&cmd->rate_table.rate_entry[count];
			struct mwl8k_rate_entry *src =
				&rate_table->rate_entry[count];

			dst->is_ht_rate = cpu_to_le32(src->is_ht_rate);
			dst->enable_retry = cpu_to_le32(src->enable_retry);
			dst->rate = cpu_to_le32(src->rate);
			dst->retry_count = cpu_to_le32(src->retry_count);
		}
	}

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}


/*
 * Interrupt handling.
 */
static irqreturn_t mwl8k_interrupt(int irq, void *dev_id)
{
	struct ieee80211_hw *hw = dev_id;
	struct mwl8k_priv *priv = hw->priv;
	u32 status;

	status = ioread32(priv->regs + MWL8K_HIU_A2H_INTERRUPT_STATUS);
	iowrite32(~status, priv->regs + MWL8K_HIU_A2H_INTERRUPT_STATUS);

	if (!status)
		return IRQ_NONE;

	if (status & MWL8K_A2H_INT_TX_DONE)
		tasklet_schedule(&priv->tx_reclaim_task);

	if (status & MWL8K_A2H_INT_RX_READY) {
		while (rxq_process(hw, 0, 1))
			rxq_refill(hw, 0, 1);
	}

	if (status & MWL8K_A2H_INT_OPC_DONE) {
		if (priv->hostcmd_wait != NULL)
			complete(priv->hostcmd_wait);
	}

	if (status & MWL8K_A2H_INT_QUEUE_EMPTY) {
		if (!mutex_is_locked(&priv->fw_mutex) &&
		    priv->radio_on && priv->pending_tx_pkts)
			mwl8k_tx_start(priv);
	}

	return IRQ_HANDLED;
}


/*
 * Core driver operations.
 */
static int mwl8k_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct mwl8k_priv *priv = hw->priv;
	int index = skb_get_queue_mapping(skb);
	int rc;

	if (priv->current_channel == NULL) {
		printk(KERN_DEBUG "%s: dropped TX frame since radio "
		       "disabled\n", wiphy_name(hw->wiphy));
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	rc = mwl8k_txq_xmit(hw, index, skb);

	return rc;
}

static int mwl8k_start(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;
	int rc;

	rc = request_irq(priv->pdev->irq, &mwl8k_interrupt,
			 IRQF_SHARED, MWL8K_NAME, hw);
	if (rc) {
		printk(KERN_ERR "%s: failed to register IRQ handler\n",
		       wiphy_name(hw->wiphy));
		return -EIO;
	}

	/* Enable tx reclaim tasklet */
	tasklet_enable(&priv->tx_reclaim_task);

	/* Enable interrupts */
	iowrite32(MWL8K_A2H_EVENTS, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);

	rc = mwl8k_fw_lock(hw);
	if (!rc) {
		rc = mwl8k_cmd_802_11_radio_enable(hw);

		if (!rc)
			rc = mwl8k_cmd_set_pre_scan(hw);

		if (!rc)
			rc = mwl8k_cmd_set_post_scan(hw,
					"\x00\x00\x00\x00\x00\x00");

		if (!rc)
			rc = mwl8k_cmd_setrateadaptmode(hw, 0);

		if (!rc)
			rc = mwl8k_set_wmm(hw, 0);

		if (!rc)
			rc = mwl8k_enable_sniffer(hw, 0);

		mwl8k_fw_unlock(hw);
	}

	if (rc) {
		iowrite32(0, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);
		free_irq(priv->pdev->irq, hw);
		tasklet_disable(&priv->tx_reclaim_task);
	}

	return rc;
}

static void mwl8k_stop(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;
	int i;

	mwl8k_cmd_802_11_radio_disable(hw);

	ieee80211_stop_queues(hw);

	/* Disable interrupts */
	iowrite32(0, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);
	free_irq(priv->pdev->irq, hw);

	/* Stop finalize join worker */
	cancel_work_sync(&priv->finalize_join_worker);
	if (priv->beacon_skb != NULL)
		dev_kfree_skb(priv->beacon_skb);

	/* Stop tx reclaim tasklet */
	tasklet_disable(&priv->tx_reclaim_task);

	/* Return all skbs to mac80211 */
	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		mwl8k_txq_reclaim(hw, i, 1);
}

static int mwl8k_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_if_init_conf *conf)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_vif *mwl8k_vif;

	/*
	 * We only support one active interface at a time.
	 */
	if (priv->vif != NULL)
		return -EBUSY;

	/*
	 * We only support managed interfaces for now.
	 */
	if (conf->type != NL80211_IFTYPE_STATION)
		return -EINVAL;

	/*
	 * Reject interface creation if sniffer mode is active, as
	 * STA operation is mutually exclusive with hardware sniffer
	 * mode.
	 */
	if (priv->sniffer_enabled) {
		printk(KERN_INFO "%s: unable to create STA "
		       "interface due to sniffer mode being enabled\n",
		       wiphy_name(hw->wiphy));
		return -EINVAL;
	}

	/* Clean out driver private area */
	mwl8k_vif = MWL8K_VIF(conf->vif);
	memset(mwl8k_vif, 0, sizeof(*mwl8k_vif));

	/* Set and save the mac address */
	mwl8k_set_mac_addr(hw, conf->mac_addr);
	memcpy(mwl8k_vif->mac_addr, conf->mac_addr, ETH_ALEN);

	/* Back pointer to parent config block */
	mwl8k_vif->priv = priv;

	/* Setup initial PHY parameters */
	memcpy(mwl8k_vif->legacy_rates,
		priv->rates, sizeof(mwl8k_vif->legacy_rates));
	mwl8k_vif->legacy_nrates = ARRAY_SIZE(priv->rates);

	/* Set Initial sequence number to zero */
	mwl8k_vif->seqno = 0;

	priv->vif = conf->vif;
	priv->current_channel = NULL;

	return 0;
}

static void mwl8k_remove_interface(struct ieee80211_hw *hw,
				   struct ieee80211_if_init_conf *conf)
{
	struct mwl8k_priv *priv = hw->priv;

	if (priv->vif == NULL)
		return;

	mwl8k_set_mac_addr(hw, "\x00\x00\x00\x00\x00\x00");

	priv->vif = NULL;
}

static int mwl8k_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ieee80211_conf *conf = &hw->conf;
	struct mwl8k_priv *priv = hw->priv;
	int rc;

	if (conf->flags & IEEE80211_CONF_IDLE) {
		mwl8k_cmd_802_11_radio_disable(hw);
		priv->current_channel = NULL;
		return 0;
	}

	rc = mwl8k_fw_lock(hw);
	if (rc)
		return rc;

	rc = mwl8k_cmd_802_11_radio_enable(hw);
	if (rc)
		goto out;

	rc = mwl8k_cmd_set_rf_channel(hw, conf->channel);
	if (rc)
		goto out;

	priv->current_channel = conf->channel;

	if (conf->power_level > 18)
		conf->power_level = 18;
	rc = mwl8k_cmd_802_11_rf_tx_power(hw, conf->power_level);
	if (rc)
		goto out;

	if (mwl8k_cmd_mimo_config(hw, 0x7, 0x7))
		rc = -EINVAL;

out:
	mwl8k_fw_unlock(hw);

	return rc;
}

static void mwl8k_bss_info_changed(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *info,
				   u32 changed)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_vif *mwl8k_vif = MWL8K_VIF(vif);
	int rc;

	if (changed & BSS_CHANGED_BSSID)
		memcpy(mwl8k_vif->bssid, info->bssid, ETH_ALEN);

	if ((changed & BSS_CHANGED_ASSOC) == 0)
		return;

	priv->capture_beacon = false;

	rc = mwl8k_fw_lock(hw);
	if (rc)
		return;

	if (info->assoc) {
		memcpy(&mwl8k_vif->bss_info, info,
			sizeof(struct ieee80211_bss_conf));

		/* Install rates */
		rc = mwl8k_update_rateset(hw, vif);
		if (rc)
			goto out;

		/* Turn on rate adaptation */
		rc = mwl8k_cmd_use_fixed_rate(hw, MWL8K_USE_AUTO_RATE,
			MWL8K_UCAST_RATE, NULL);
		if (rc)
			goto out;

		/* Set radio preamble */
		rc = mwl8k_set_radio_preamble(hw, info->use_short_preamble);
		if (rc)
			goto out;

		/* Set slot time */
		rc = mwl8k_cmd_set_slot(hw, info->use_short_slot);
		if (rc)
			goto out;

		/* Update peer rate info */
		rc = mwl8k_cmd_update_sta_db(hw, vif,
				MWL8K_STA_DB_MODIFY_ENTRY);
		if (rc)
			goto out;

		/* Set AID */
		rc = mwl8k_cmd_set_aid(hw, vif);
		if (rc)
			goto out;

		/*
		 * Finalize the join.  Tell rx handler to process
		 * next beacon from our BSSID.
		 */
		memcpy(priv->capture_bssid, mwl8k_vif->bssid, ETH_ALEN);
		priv->capture_beacon = true;
	} else {
		rc = mwl8k_cmd_update_sta_db(hw, vif, MWL8K_STA_DB_DEL_ENTRY);
		memset(&mwl8k_vif->bss_info, 0,
			sizeof(struct ieee80211_bss_conf));
		memset(mwl8k_vif->bssid, 0, ETH_ALEN);
	}

out:
	mwl8k_fw_unlock(hw);
}

static u64 mwl8k_prepare_multicast(struct ieee80211_hw *hw,
				   int mc_count, struct dev_addr_list *mclist)
{
	struct mwl8k_cmd_pkt *cmd;

	/*
	 * Synthesize and return a command packet that programs the
	 * hardware multicast address filter.  At this point we don't
	 * know whether FIF_ALLMULTI is being requested, but if it is,
	 * we'll end up throwing this packet away and creating a new
	 * one in mwl8k_configure_filter().
	 */
	cmd = __mwl8k_cmd_mac_multicast_adr(hw, 0, mc_count, mclist);

	return (unsigned long)cmd;
}

static int
mwl8k_configure_filter_sniffer(struct ieee80211_hw *hw,
			       unsigned int changed_flags,
			       unsigned int *total_flags)
{
	struct mwl8k_priv *priv = hw->priv;

	/*
	 * Hardware sniffer mode is mutually exclusive with STA
	 * operation, so refuse to enable sniffer mode if a STA
	 * interface is active.
	 */
	if (priv->vif != NULL) {
		if (net_ratelimit())
			printk(KERN_INFO "%s: not enabling sniffer "
			       "mode because STA interface is active\n",
			       wiphy_name(hw->wiphy));
		return 0;
	}

	if (!priv->sniffer_enabled) {
		if (mwl8k_enable_sniffer(hw, 1))
			return 0;
		priv->sniffer_enabled = true;
	}

	*total_flags &=	FIF_PROMISC_IN_BSS | FIF_ALLMULTI |
			FIF_BCN_PRBRESP_PROMISC | FIF_CONTROL |
			FIF_OTHER_BSS;

	return 1;
}

static void mwl8k_configure_filter(struct ieee80211_hw *hw,
				   unsigned int changed_flags,
				   unsigned int *total_flags,
				   u64 multicast)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_pkt *cmd = (void *)(unsigned long)multicast;

	/*
	 * Enable hardware sniffer mode if FIF_CONTROL or
	 * FIF_OTHER_BSS is requested.
	 */
	if (*total_flags & (FIF_CONTROL | FIF_OTHER_BSS) &&
	    mwl8k_configure_filter_sniffer(hw, changed_flags, total_flags)) {
		kfree(cmd);
		return;
	}

	/* Clear unsupported feature flags */
	*total_flags &= FIF_ALLMULTI | FIF_BCN_PRBRESP_PROMISC;

	if (mwl8k_fw_lock(hw))
		return;

	if (priv->sniffer_enabled) {
		mwl8k_enable_sniffer(hw, 0);
		priv->sniffer_enabled = false;
	}

	if (changed_flags & FIF_BCN_PRBRESP_PROMISC) {
		if (*total_flags & FIF_BCN_PRBRESP_PROMISC) {
			/*
			 * Disable the BSS filter.
			 */
			mwl8k_cmd_set_pre_scan(hw);
		} else {
			u8 *bssid;

			/*
			 * Enable the BSS filter.
			 *
			 * If there is an active STA interface, use that
			 * interface's BSSID, otherwise use a dummy one
			 * (where the OUI part needs to be nonzero for
			 * the BSSID to be accepted by POST_SCAN).
			 */
			bssid = "\x01\x00\x00\x00\x00\x00";
			if (priv->vif != NULL)
				bssid = MWL8K_VIF(priv->vif)->bssid;

			mwl8k_cmd_set_post_scan(hw, bssid);
		}
	}

	/*
	 * If FIF_ALLMULTI is being requested, throw away the command
	 * packet that ->prepare_multicast() built and replace it with
	 * a command packet that enables reception of all multicast
	 * packets.
	 */
	if (*total_flags & FIF_ALLMULTI) {
		kfree(cmd);
		cmd = __mwl8k_cmd_mac_multicast_adr(hw, 1, 0, NULL);
	}

	if (cmd != NULL) {
		mwl8k_post_cmd(hw, cmd);
		kfree(cmd);
	}

	mwl8k_fw_unlock(hw);
}

static int mwl8k_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	return mwl8k_rts_threshold(hw, MWL8K_CMD_SET, value);
}

static int mwl8k_conf_tx(struct ieee80211_hw *hw, u16 queue,
			 const struct ieee80211_tx_queue_params *params)
{
	struct mwl8k_priv *priv = hw->priv;
	int rc;

	rc = mwl8k_fw_lock(hw);
	if (!rc) {
		if (!priv->wmm_enabled)
			rc = mwl8k_set_wmm(hw, 1);

		if (!rc)
			rc = mwl8k_set_edca_params(hw, queue,
						   params->cw_min,
						   params->cw_max,
						   params->aifs,
						   params->txop);

		mwl8k_fw_unlock(hw);
	}

	return rc;
}

static int mwl8k_get_tx_stats(struct ieee80211_hw *hw,
			      struct ieee80211_tx_queue_stats *stats)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_tx_queue *txq;
	int index;

	spin_lock_bh(&priv->tx_lock);
	for (index = 0; index < MWL8K_TX_QUEUES; index++) {
		txq = priv->txq + index;
		memcpy(&stats[index], &txq->tx_stats,
			sizeof(struct ieee80211_tx_queue_stats));
	}
	spin_unlock_bh(&priv->tx_lock);

	return 0;
}

static int mwl8k_get_stats(struct ieee80211_hw *hw,
			   struct ieee80211_low_level_stats *stats)
{
	return mwl8k_cmd_802_11_get_stat(hw, stats);
}

static const struct ieee80211_ops mwl8k_ops = {
	.tx			= mwl8k_tx,
	.start			= mwl8k_start,
	.stop			= mwl8k_stop,
	.add_interface		= mwl8k_add_interface,
	.remove_interface	= mwl8k_remove_interface,
	.config			= mwl8k_config,
	.bss_info_changed	= mwl8k_bss_info_changed,
	.prepare_multicast	= mwl8k_prepare_multicast,
	.configure_filter	= mwl8k_configure_filter,
	.set_rts_threshold	= mwl8k_set_rts_threshold,
	.conf_tx		= mwl8k_conf_tx,
	.get_tx_stats		= mwl8k_get_tx_stats,
	.get_stats		= mwl8k_get_stats,
};

static void mwl8k_tx_reclaim_handler(unsigned long data)
{
	int i;
	struct ieee80211_hw *hw = (struct ieee80211_hw *) data;
	struct mwl8k_priv *priv = hw->priv;

	spin_lock_bh(&priv->tx_lock);
	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		mwl8k_txq_reclaim(hw, i, 0);

	if (priv->tx_wait != NULL && !priv->pending_tx_pkts) {
		complete(priv->tx_wait);
		priv->tx_wait = NULL;
	}
	spin_unlock_bh(&priv->tx_lock);
}

static void mwl8k_finalize_join_worker(struct work_struct *work)
{
	struct mwl8k_priv *priv =
		container_of(work, struct mwl8k_priv, finalize_join_worker);
	struct sk_buff *skb = priv->beacon_skb;
	u8 dtim = MWL8K_VIF(priv->vif)->bss_info.dtim_period;

	mwl8k_finalize_join(priv->hw, skb->data, skb->len, dtim);
	dev_kfree_skb(skb);

	priv->beacon_skb = NULL;
}

static int __devinit mwl8k_probe(struct pci_dev *pdev,
				 const struct pci_device_id *id)
{
	static int printed_version = 0;
	struct ieee80211_hw *hw;
	struct mwl8k_priv *priv;
	int rc;
	int i;

	if (!printed_version) {
		printk(KERN_INFO "%s version %s\n", MWL8K_DESC, MWL8K_VERSION);
		printed_version = 1;
	}

	rc = pci_enable_device(pdev);
	if (rc) {
		printk(KERN_ERR "%s: Cannot enable new PCI device\n",
		       MWL8K_NAME);
		return rc;
	}

	rc = pci_request_regions(pdev, MWL8K_NAME);
	if (rc) {
		printk(KERN_ERR "%s: Cannot obtain PCI resources\n",
		       MWL8K_NAME);
		return rc;
	}

	pci_set_master(pdev);

	hw = ieee80211_alloc_hw(sizeof(*priv), &mwl8k_ops);
	if (hw == NULL) {
		printk(KERN_ERR "%s: ieee80211 alloc failed\n", MWL8K_NAME);
		rc = -ENOMEM;
		goto err_free_reg;
	}

	priv = hw->priv;
	priv->hw = hw;
	priv->pdev = pdev;
	priv->sniffer_enabled = false;
	priv->wmm_enabled = false;
	priv->pending_tx_pkts = 0;

	SET_IEEE80211_DEV(hw, &pdev->dev);
	pci_set_drvdata(pdev, hw);

	priv->regs = pci_iomap(pdev, 1, 0x10000);
	if (priv->regs == NULL) {
		printk(KERN_ERR "%s: Cannot map device memory\n",
		       wiphy_name(hw->wiphy));
		goto err_iounmap;
	}

	memcpy(priv->channels, mwl8k_channels, sizeof(mwl8k_channels));
	priv->band.band = IEEE80211_BAND_2GHZ;
	priv->band.channels = priv->channels;
	priv->band.n_channels = ARRAY_SIZE(mwl8k_channels);
	priv->band.bitrates = priv->rates;
	priv->band.n_bitrates = ARRAY_SIZE(mwl8k_rates);
	hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &priv->band;

	BUILD_BUG_ON(sizeof(priv->rates) != sizeof(mwl8k_rates));
	memcpy(priv->rates, mwl8k_rates, sizeof(mwl8k_rates));

	/*
	 * Extra headroom is the size of the required DMA header
	 * minus the size of the smallest 802.11 frame (CTS frame).
	 */
	hw->extra_tx_headroom =
		sizeof(struct mwl8k_dma_data) - sizeof(struct ieee80211_cts);

	hw->channel_change_time = 10;

	hw->queues = MWL8K_TX_QUEUES;

	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);

	/* Set rssi and noise values to dBm */
	hw->flags |= IEEE80211_HW_SIGNAL_DBM | IEEE80211_HW_NOISE_DBM;
	hw->vif_data_size = sizeof(struct mwl8k_vif);
	priv->vif = NULL;

	/* Set default radio state and preamble */
	priv->radio_on = 0;
	priv->radio_short_preamble = 0;

	/* Finalize join worker */
	INIT_WORK(&priv->finalize_join_worker, mwl8k_finalize_join_worker);

	/* TX reclaim tasklet */
	tasklet_init(&priv->tx_reclaim_task,
			mwl8k_tx_reclaim_handler, (unsigned long)hw);
	tasklet_disable(&priv->tx_reclaim_task);

	/* Power management cookie */
	priv->cookie = pci_alloc_consistent(priv->pdev, 4, &priv->cookie_dma);
	if (priv->cookie == NULL)
		goto err_iounmap;

	rc = mwl8k_rxq_init(hw, 0);
	if (rc)
		goto err_iounmap;
	rxq_refill(hw, 0, INT_MAX);

	mutex_init(&priv->fw_mutex);
	priv->fw_mutex_owner = NULL;
	priv->fw_mutex_depth = 0;
	priv->hostcmd_wait = NULL;

	spin_lock_init(&priv->tx_lock);

	priv->tx_wait = NULL;

	for (i = 0; i < MWL8K_TX_QUEUES; i++) {
		rc = mwl8k_txq_init(hw, i);
		if (rc)
			goto err_free_queues;
	}

	iowrite32(0, priv->regs + MWL8K_HIU_A2H_INTERRUPT_STATUS);
	iowrite32(0, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);
	iowrite32(0, priv->regs + MWL8K_HIU_A2H_INTERRUPT_CLEAR_SEL);
	iowrite32(0xffffffff, priv->regs + MWL8K_HIU_A2H_INTERRUPT_STATUS_MASK);

	rc = request_irq(priv->pdev->irq, &mwl8k_interrupt,
			 IRQF_SHARED, MWL8K_NAME, hw);
	if (rc) {
		printk(KERN_ERR "%s: failed to register IRQ handler\n",
		       wiphy_name(hw->wiphy));
		goto err_free_queues;
	}

	/* Reset firmware and hardware */
	mwl8k_hw_reset(priv);

	/* Ask userland hotplug daemon for the device firmware */
	rc = mwl8k_request_firmware(priv, (u32)id->driver_data);
	if (rc) {
		printk(KERN_ERR "%s: Firmware files not found\n",
		       wiphy_name(hw->wiphy));
		goto err_free_irq;
	}

	/* Load firmware into hardware */
	rc = mwl8k_load_firmware(hw);
	if (rc) {
		printk(KERN_ERR "%s: Cannot start firmware\n",
		       wiphy_name(hw->wiphy));
		goto err_stop_firmware;
	}

	/* Reclaim memory once firmware is successfully loaded */
	mwl8k_release_firmware(priv);

	/*
	 * Temporarily enable interrupts.  Initial firmware host
	 * commands use interrupts and avoids polling.  Disable
	 * interrupts when done.
	 */
	iowrite32(MWL8K_A2H_EVENTS, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);

	/* Get config data, mac addrs etc */
	rc = mwl8k_cmd_get_hw_spec(hw);
	if (rc) {
		printk(KERN_ERR "%s: Cannot initialise firmware\n",
		       wiphy_name(hw->wiphy));
		goto err_stop_firmware;
	}

	/* Turn radio off */
	rc = mwl8k_cmd_802_11_radio_disable(hw);
	if (rc) {
		printk(KERN_ERR "%s: Cannot disable\n", wiphy_name(hw->wiphy));
		goto err_stop_firmware;
	}

	/* Clear MAC address */
	rc = mwl8k_set_mac_addr(hw, "\x00\x00\x00\x00\x00\x00");
	if (rc) {
		printk(KERN_ERR "%s: Cannot clear MAC address\n",
		       wiphy_name(hw->wiphy));
		goto err_stop_firmware;
	}

	/* Disable interrupts */
	iowrite32(0, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);
	free_irq(priv->pdev->irq, hw);

	rc = ieee80211_register_hw(hw);
	if (rc) {
		printk(KERN_ERR "%s: Cannot register device\n",
		       wiphy_name(hw->wiphy));
		goto err_stop_firmware;
	}

	printk(KERN_INFO "%s: 88w%u v%d, %pM, firmware version %u.%u.%u.%u\n",
	       wiphy_name(hw->wiphy), priv->part_num, priv->hw_rev,
	       hw->wiphy->perm_addr,
	       (priv->fw_rev >> 24) & 0xff, (priv->fw_rev >> 16) & 0xff,
	       (priv->fw_rev >> 8) & 0xff, priv->fw_rev & 0xff);

	return 0;

err_stop_firmware:
	mwl8k_hw_reset(priv);
	mwl8k_release_firmware(priv);

err_free_irq:
	iowrite32(0, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);
	free_irq(priv->pdev->irq, hw);

err_free_queues:
	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		mwl8k_txq_deinit(hw, i);
	mwl8k_rxq_deinit(hw, 0);

err_iounmap:
	if (priv->cookie != NULL)
		pci_free_consistent(priv->pdev, 4,
				priv->cookie, priv->cookie_dma);

	if (priv->regs != NULL)
		pci_iounmap(pdev, priv->regs);

	pci_set_drvdata(pdev, NULL);
	ieee80211_free_hw(hw);

err_free_reg:
	pci_release_regions(pdev);
	pci_disable_device(pdev);

	return rc;
}

static void __devexit mwl8k_shutdown(struct pci_dev *pdev)
{
	printk(KERN_ERR "===>%s(%u)\n", __func__, __LINE__);
}

static void __devexit mwl8k_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct mwl8k_priv *priv;
	int i;

	if (hw == NULL)
		return;
	priv = hw->priv;

	ieee80211_stop_queues(hw);

	ieee80211_unregister_hw(hw);

	/* Remove tx reclaim tasklet */
	tasklet_kill(&priv->tx_reclaim_task);

	/* Stop hardware */
	mwl8k_hw_reset(priv);

	/* Return all skbs to mac80211 */
	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		mwl8k_txq_reclaim(hw, i, 1);

	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		mwl8k_txq_deinit(hw, i);

	mwl8k_rxq_deinit(hw, 0);

	pci_free_consistent(priv->pdev, 4, priv->cookie, priv->cookie_dma);

	pci_iounmap(pdev, priv->regs);
	pci_set_drvdata(pdev, NULL);
	ieee80211_free_hw(hw);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver mwl8k_driver = {
	.name		= MWL8K_NAME,
	.id_table	= mwl8k_table,
	.probe		= mwl8k_probe,
	.remove		= __devexit_p(mwl8k_remove),
	.shutdown	= __devexit_p(mwl8k_shutdown),
};

static int __init mwl8k_init(void)
{
	return pci_register_driver(&mwl8k_driver);
}

static void __exit mwl8k_exit(void)
{
	pci_unregister_driver(&mwl8k_driver);
}

module_init(mwl8k_init);
module_exit(mwl8k_exit);

MODULE_DESCRIPTION(MWL8K_DESC);
MODULE_VERSION(MWL8K_VERSION);
MODULE_AUTHOR("Lennert Buytenhek <buytenh@marvell.com>");
MODULE_LICENSE("GPL");
