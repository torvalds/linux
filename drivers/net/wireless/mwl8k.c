/*
 * drivers/net/wireless/mwl8k.c
 * Driver for Marvell TOPDOG 802.11 Wireless cards
 *
 * Copyright (C) 2008, 2009, 2010 Marvell Semiconductor Inc.
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
#include <linux/slab.h>
#include <net/mac80211.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/workqueue.h>

#define MWL8K_DESC	"Marvell TOPDOG(R) 802.11 Wireless Network Driver"
#define MWL8K_NAME	KBUILD_MODNAME
#define MWL8K_VERSION	"0.12"

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

struct rxd_ops {
	int rxd_size;
	void (*rxd_init)(void *rxd, dma_addr_t next_dma_addr);
	void (*rxd_refill)(void *rxd, dma_addr_t addr, int len);
	int (*rxd_process)(void *rxd, struct ieee80211_rx_status *status,
			   __le16 *qos);
};

struct mwl8k_device_info {
	char *part_name;
	char *helper_image;
	char *fw_image;
	struct rxd_ops *ap_rxd_ops;
};

struct mwl8k_rx_queue {
	int rxd_count;

	/* hw receives here */
	int head;

	/* refill descs here */
	int tail;

	void *rxd;
	dma_addr_t rxd_dma;
	struct {
		struct sk_buff *skb;
		DECLARE_PCI_UNMAP_ADDR(dma)
	} *buf;
};

struct mwl8k_tx_queue {
	/* hw transmits here */
	int head;

	/* sw appends here */
	int tail;

	unsigned int len;
	struct mwl8k_tx_desc *txd;
	dma_addr_t txd_dma;
	struct sk_buff **skb;
};

struct mwl8k_priv {
	struct ieee80211_hw *hw;
	struct pci_dev *pdev;

	struct mwl8k_device_info *device_info;

	void __iomem *sram;
	void __iomem *regs;

	/* firmware */
	struct firmware *fw_helper;
	struct firmware *fw_ucode;

	/* hardware/firmware parameters */
	bool ap_fw;
	struct rxd_ops *rxd_ops;
	struct ieee80211_supported_band band_24;
	struct ieee80211_channel channels_24[14];
	struct ieee80211_rate rates_24[14];
	struct ieee80211_supported_band band_50;
	struct ieee80211_channel channels_50[4];
	struct ieee80211_rate rates_50[9];
	u32 ap_macids_supported;
	u32 sta_macids_supported;

	/* firmware access */
	struct mutex fw_mutex;
	struct task_struct *fw_mutex_owner;
	int fw_mutex_depth;
	struct completion *hostcmd_wait;

	/* lock held over TX and TX reap */
	spinlock_t tx_lock;

	/* TX quiesce completion, protected by fw_mutex and tx_lock */
	struct completion *tx_wait;

	/* List of interfaces.  */
	u32 macids_used;
	struct list_head vif_list;

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

	/* Tasklet to perform TX reclaim.  */
	struct tasklet_struct poll_tx_task;

	/* Tasklet to perform RX.  */
	struct tasklet_struct poll_rx_task;
};

/* Per interface specific private data */
struct mwl8k_vif {
	struct list_head list;
	struct ieee80211_vif *vif;

	/* Firmware macid for this vif.  */
	int macid;

	/* Non AMPDU sequence number assigned by driver.  */
	u16 seqno;
};
#define MWL8K_VIF(_vif) ((struct mwl8k_vif *)&((_vif)->drv_priv))

struct mwl8k_sta {
	/* Index into station database. Returned by UPDATE_STADB.  */
	u8 peer_id;
};
#define MWL8K_STA(_sta) ((struct mwl8k_sta *)&((_sta)->drv_priv))

static const struct ieee80211_channel mwl8k_channels_24[] = {
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
	{ .center_freq = 2467, .hw_value = 12, },
	{ .center_freq = 2472, .hw_value = 13, },
	{ .center_freq = 2484, .hw_value = 14, },
};

static const struct ieee80211_rate mwl8k_rates_24[] = {
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
	{ .bitrate = 720, .hw_value = 144, },
};

static const struct ieee80211_channel mwl8k_channels_50[] = {
	{ .center_freq = 5180, .hw_value = 36, },
	{ .center_freq = 5200, .hw_value = 40, },
	{ .center_freq = 5220, .hw_value = 44, },
	{ .center_freq = 5240, .hw_value = 48, },
};

static const struct ieee80211_rate mwl8k_rates_50[] = {
	{ .bitrate = 60, .hw_value = 12, },
	{ .bitrate = 90, .hw_value = 18, },
	{ .bitrate = 120, .hw_value = 24, },
	{ .bitrate = 180, .hw_value = 36, },
	{ .bitrate = 240, .hw_value = 48, },
	{ .bitrate = 360, .hw_value = 72, },
	{ .bitrate = 480, .hw_value = 96, },
	{ .bitrate = 540, .hw_value = 108, },
	{ .bitrate = 720, .hw_value = 144, },
};

/* Set or get info from Firmware */
#define MWL8K_CMD_SET			0x0001
#define MWL8K_CMD_GET			0x0000

/* Firmware command codes */
#define MWL8K_CMD_CODE_DNLD		0x0001
#define MWL8K_CMD_GET_HW_SPEC		0x0003
#define MWL8K_CMD_SET_HW_SPEC		0x0004
#define MWL8K_CMD_MAC_MULTICAST_ADR	0x0010
#define MWL8K_CMD_GET_STAT		0x0014
#define MWL8K_CMD_RADIO_CONTROL		0x001c
#define MWL8K_CMD_RF_TX_POWER		0x001e
#define MWL8K_CMD_RF_ANTENNA		0x0020
#define MWL8K_CMD_SET_BEACON		0x0100		/* per-vif */
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
#define MWL8K_CMD_SET_MAC_ADDR		0x0202		/* per-vif */
#define MWL8K_CMD_SET_RATEADAPT_MODE	0x0203
#define MWL8K_CMD_BSS_START		0x1100		/* per-vif */
#define MWL8K_CMD_SET_NEW_STN		0x1111		/* per-vif */
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
		MWL8K_CMDNAME(SET_HW_SPEC);
		MWL8K_CMDNAME(MAC_MULTICAST_ADR);
		MWL8K_CMDNAME(GET_STAT);
		MWL8K_CMDNAME(RADIO_CONTROL);
		MWL8K_CMDNAME(RF_TX_POWER);
		MWL8K_CMDNAME(RF_ANTENNA);
		MWL8K_CMDNAME(SET_BEACON);
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
		MWL8K_CMDNAME(BSS_START);
		MWL8K_CMDNAME(SET_NEW_STN);
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
	mwl8k_release_fw(&priv->fw_ucode);
	mwl8k_release_fw(&priv->fw_helper);
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

static int mwl8k_request_firmware(struct mwl8k_priv *priv)
{
	struct mwl8k_device_info *di = priv->device_info;
	int rc;

	if (di->helper_image != NULL) {
		rc = mwl8k_request_fw(priv, di->helper_image, &priv->fw_helper);
		if (rc) {
			printk(KERN_ERR "%s: Error requesting helper "
			       "firmware file %s\n", pci_name(priv->pdev),
			       di->helper_image);
			return rc;
		}
	}

	rc = mwl8k_request_fw(priv, di->fw_image, &priv->fw_ucode);
	if (rc) {
		printk(KERN_ERR "%s: Error requesting firmware file %s\n",
		       pci_name(priv->pdev), di->fw_image);
		mwl8k_release_fw(&priv->fw_helper);
		return rc;
	}

	return 0;
}

struct mwl8k_cmd_pkt {
	__le16	code;
	__le16	length;
	__u8	seq_num;
	__u8	macid;
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
	cmd->macid = 0;
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
	struct firmware *fw = priv->fw_ucode;
	int rc;
	int loops;

	if (!memcmp(fw->data, "\x01\x00\x00\x00", 4)) {
		struct firmware *helper = priv->fw_helper;

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
		msleep(5);

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

	loops = 500000;
	do {
		u32 ready_code;

		ready_code = ioread32(priv->regs + MWL8K_HIU_INT_CODE);
		if (ready_code == MWL8K_FWAP_READY) {
			priv->ap_fw = 1;
			break;
		} else if (ready_code == MWL8K_FWSTA_READY) {
			priv->ap_fw = 0;
			break;
		}

		cond_resched();
		udelay(1);
	} while (--loops);

	return loops ? 0 : -ETIMEDOUT;
}


/* DMA header used by firmware and hardware.  */
struct mwl8k_dma_data {
	__le16 fwlen;
	struct ieee80211_hdr wh;
	char data[0];
} __attribute__((packed));

/* Routines to add/remove DMA header from skb.  */
static inline void mwl8k_remove_dma_header(struct sk_buff *skb, __le16 qos)
{
	struct mwl8k_dma_data *tr;
	int hdrlen;

	tr = (struct mwl8k_dma_data *)skb->data;
	hdrlen = ieee80211_hdrlen(tr->wh.frame_control);

	if (hdrlen != sizeof(tr->wh)) {
		if (ieee80211_is_data_qos(tr->wh.frame_control)) {
			memmove(tr->data - hdrlen, &tr->wh, hdrlen - 2);
			*((__le16 *)(tr->data - 2)) = qos;
		} else {
			memmove(tr->data - hdrlen, &tr->wh, hdrlen);
		}
	}

	if (hdrlen != sizeof(*tr))
		skb_pull(skb, sizeof(*tr) - hdrlen);
}

static inline void mwl8k_add_dma_header(struct sk_buff *skb)
{
	struct ieee80211_hdr *wh;
	int hdrlen;
	struct mwl8k_dma_data *tr;

	/*
	 * Add a firmware DMA header; the firmware requires that we
	 * present a 2-byte payload length followed by a 4-address
	 * header (without QoS field), followed (optionally) by any
	 * WEP/ExtIV header (but only filled in for CCMP).
	 */
	wh = (struct ieee80211_hdr *)skb->data;

	hdrlen = ieee80211_hdrlen(wh->frame_control);
	if (hdrlen != sizeof(*tr))
		skb_push(skb, sizeof(*tr) - hdrlen);

	if (ieee80211_is_data_qos(wh->frame_control))
		hdrlen -= 2;

	tr = (struct mwl8k_dma_data *)skb->data;
	if (wh != &tr->wh)
		memmove(&tr->wh, wh, hdrlen);
	if (hdrlen != sizeof(tr->wh))
		memset(((void *)&tr->wh) + hdrlen, 0, sizeof(tr->wh) - hdrlen);

	/*
	 * Firmware length is the length of the fully formed "802.11
	 * payload".  That is, everything except for the 802.11 header.
	 * This includes all crypto material including the MIC.
	 */
	tr->fwlen = cpu_to_le16(skb->len - sizeof(*tr));
}


/*
 * Packet reception for 88w8366 AP firmware.
 */
struct mwl8k_rxd_8366_ap {
	__le16 pkt_len;
	__u8 sq2;
	__u8 rate;
	__le32 pkt_phys_addr;
	__le32 next_rxd_phys_addr;
	__le16 qos_control;
	__le16 htsig2;
	__le32 hw_rssi_info;
	__le32 hw_noise_floor_info;
	__u8 noise_floor;
	__u8 pad0[3];
	__u8 rssi;
	__u8 rx_status;
	__u8 channel;
	__u8 rx_ctrl;
} __attribute__((packed));

#define MWL8K_8366_AP_RATE_INFO_MCS_FORMAT	0x80
#define MWL8K_8366_AP_RATE_INFO_40MHZ		0x40
#define MWL8K_8366_AP_RATE_INFO_RATEID(x)	((x) & 0x3f)

#define MWL8K_8366_AP_RX_CTRL_OWNED_BY_HOST	0x80

static void mwl8k_rxd_8366_ap_init(void *_rxd, dma_addr_t next_dma_addr)
{
	struct mwl8k_rxd_8366_ap *rxd = _rxd;

	rxd->next_rxd_phys_addr = cpu_to_le32(next_dma_addr);
	rxd->rx_ctrl = MWL8K_8366_AP_RX_CTRL_OWNED_BY_HOST;
}

static void mwl8k_rxd_8366_ap_refill(void *_rxd, dma_addr_t addr, int len)
{
	struct mwl8k_rxd_8366_ap *rxd = _rxd;

	rxd->pkt_len = cpu_to_le16(len);
	rxd->pkt_phys_addr = cpu_to_le32(addr);
	wmb();
	rxd->rx_ctrl = 0;
}

static int
mwl8k_rxd_8366_ap_process(void *_rxd, struct ieee80211_rx_status *status,
			  __le16 *qos)
{
	struct mwl8k_rxd_8366_ap *rxd = _rxd;

	if (!(rxd->rx_ctrl & MWL8K_8366_AP_RX_CTRL_OWNED_BY_HOST))
		return -1;
	rmb();

	memset(status, 0, sizeof(*status));

	status->signal = -rxd->rssi;

	if (rxd->rate & MWL8K_8366_AP_RATE_INFO_MCS_FORMAT) {
		status->flag |= RX_FLAG_HT;
		if (rxd->rate & MWL8K_8366_AP_RATE_INFO_40MHZ)
			status->flag |= RX_FLAG_40MHZ;
		status->rate_idx = MWL8K_8366_AP_RATE_INFO_RATEID(rxd->rate);
	} else {
		int i;

		for (i = 0; i < ARRAY_SIZE(mwl8k_rates_24); i++) {
			if (mwl8k_rates_24[i].hw_value == rxd->rate) {
				status->rate_idx = i;
				break;
			}
		}
	}

	if (rxd->channel > 14) {
		status->band = IEEE80211_BAND_5GHZ;
		if (!(status->flag & RX_FLAG_HT))
			status->rate_idx -= 5;
	} else {
		status->band = IEEE80211_BAND_2GHZ;
	}
	status->freq = ieee80211_channel_to_frequency(rxd->channel);

	*qos = rxd->qos_control;

	return le16_to_cpu(rxd->pkt_len);
}

static struct rxd_ops rxd_8366_ap_ops = {
	.rxd_size	= sizeof(struct mwl8k_rxd_8366_ap),
	.rxd_init	= mwl8k_rxd_8366_ap_init,
	.rxd_refill	= mwl8k_rxd_8366_ap_refill,
	.rxd_process	= mwl8k_rxd_8366_ap_process,
};

/*
 * Packet reception for STA firmware.
 */
struct mwl8k_rxd_sta {
	__le16 pkt_len;
	__u8 link_quality;
	__u8 noise_level;
	__le32 pkt_phys_addr;
	__le32 next_rxd_phys_addr;
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

#define MWL8K_STA_RATE_INFO_SHORTPRE		0x8000
#define MWL8K_STA_RATE_INFO_ANTSELECT(x)	(((x) >> 11) & 0x3)
#define MWL8K_STA_RATE_INFO_RATEID(x)		(((x) >> 3) & 0x3f)
#define MWL8K_STA_RATE_INFO_40MHZ		0x0004
#define MWL8K_STA_RATE_INFO_SHORTGI		0x0002
#define MWL8K_STA_RATE_INFO_MCS_FORMAT		0x0001

#define MWL8K_STA_RX_CTRL_OWNED_BY_HOST		0x02

static void mwl8k_rxd_sta_init(void *_rxd, dma_addr_t next_dma_addr)
{
	struct mwl8k_rxd_sta *rxd = _rxd;

	rxd->next_rxd_phys_addr = cpu_to_le32(next_dma_addr);
	rxd->rx_ctrl = MWL8K_STA_RX_CTRL_OWNED_BY_HOST;
}

static void mwl8k_rxd_sta_refill(void *_rxd, dma_addr_t addr, int len)
{
	struct mwl8k_rxd_sta *rxd = _rxd;

	rxd->pkt_len = cpu_to_le16(len);
	rxd->pkt_phys_addr = cpu_to_le32(addr);
	wmb();
	rxd->rx_ctrl = 0;
}

static int
mwl8k_rxd_sta_process(void *_rxd, struct ieee80211_rx_status *status,
		       __le16 *qos)
{
	struct mwl8k_rxd_sta *rxd = _rxd;
	u16 rate_info;

	if (!(rxd->rx_ctrl & MWL8K_STA_RX_CTRL_OWNED_BY_HOST))
		return -1;
	rmb();

	rate_info = le16_to_cpu(rxd->rate_info);

	memset(status, 0, sizeof(*status));

	status->signal = -rxd->rssi;
	status->antenna = MWL8K_STA_RATE_INFO_ANTSELECT(rate_info);
	status->rate_idx = MWL8K_STA_RATE_INFO_RATEID(rate_info);

	if (rate_info & MWL8K_STA_RATE_INFO_SHORTPRE)
		status->flag |= RX_FLAG_SHORTPRE;
	if (rate_info & MWL8K_STA_RATE_INFO_40MHZ)
		status->flag |= RX_FLAG_40MHZ;
	if (rate_info & MWL8K_STA_RATE_INFO_SHORTGI)
		status->flag |= RX_FLAG_SHORT_GI;
	if (rate_info & MWL8K_STA_RATE_INFO_MCS_FORMAT)
		status->flag |= RX_FLAG_HT;

	if (rxd->channel > 14) {
		status->band = IEEE80211_BAND_5GHZ;
		if (!(status->flag & RX_FLAG_HT))
			status->rate_idx -= 5;
	} else {
		status->band = IEEE80211_BAND_2GHZ;
	}
	status->freq = ieee80211_channel_to_frequency(rxd->channel);

	*qos = rxd->qos_control;

	return le16_to_cpu(rxd->pkt_len);
}

static struct rxd_ops rxd_sta_ops = {
	.rxd_size	= sizeof(struct mwl8k_rxd_sta),
	.rxd_init	= mwl8k_rxd_sta_init,
	.rxd_refill	= mwl8k_rxd_sta_refill,
	.rxd_process	= mwl8k_rxd_sta_process,
};


#define MWL8K_RX_DESCS		256
#define MWL8K_RX_MAXSZ		3800

static int mwl8k_rxq_init(struct ieee80211_hw *hw, int index)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_rx_queue *rxq = priv->rxq + index;
	int size;
	int i;

	rxq->rxd_count = 0;
	rxq->head = 0;
	rxq->tail = 0;

	size = MWL8K_RX_DESCS * priv->rxd_ops->rxd_size;

	rxq->rxd = pci_alloc_consistent(priv->pdev, size, &rxq->rxd_dma);
	if (rxq->rxd == NULL) {
		printk(KERN_ERR "%s: failed to alloc RX descriptors\n",
		       wiphy_name(hw->wiphy));
		return -ENOMEM;
	}
	memset(rxq->rxd, 0, size);

	rxq->buf = kmalloc(MWL8K_RX_DESCS * sizeof(*rxq->buf), GFP_KERNEL);
	if (rxq->buf == NULL) {
		printk(KERN_ERR "%s: failed to alloc RX skbuff list\n",
		       wiphy_name(hw->wiphy));
		pci_free_consistent(priv->pdev, size, rxq->rxd, rxq->rxd_dma);
		return -ENOMEM;
	}
	memset(rxq->buf, 0, MWL8K_RX_DESCS * sizeof(*rxq->buf));

	for (i = 0; i < MWL8K_RX_DESCS; i++) {
		int desc_size;
		void *rxd;
		int nexti;
		dma_addr_t next_dma_addr;

		desc_size = priv->rxd_ops->rxd_size;
		rxd = rxq->rxd + (i * priv->rxd_ops->rxd_size);

		nexti = i + 1;
		if (nexti == MWL8K_RX_DESCS)
			nexti = 0;
		next_dma_addr = rxq->rxd_dma + (nexti * desc_size);

		priv->rxd_ops->rxd_init(rxd, next_dma_addr);
	}

	return 0;
}

static int rxq_refill(struct ieee80211_hw *hw, int index, int limit)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_rx_queue *rxq = priv->rxq + index;
	int refilled;

	refilled = 0;
	while (rxq->rxd_count < MWL8K_RX_DESCS && limit--) {
		struct sk_buff *skb;
		dma_addr_t addr;
		int rx;
		void *rxd;

		skb = dev_alloc_skb(MWL8K_RX_MAXSZ);
		if (skb == NULL)
			break;

		addr = pci_map_single(priv->pdev, skb->data,
				      MWL8K_RX_MAXSZ, DMA_FROM_DEVICE);

		rxq->rxd_count++;
		rx = rxq->tail++;
		if (rxq->tail == MWL8K_RX_DESCS)
			rxq->tail = 0;
		rxq->buf[rx].skb = skb;
		pci_unmap_addr_set(&rxq->buf[rx], dma, addr);

		rxd = rxq->rxd + (rx * priv->rxd_ops->rxd_size);
		priv->rxd_ops->rxd_refill(rxd, addr, MWL8K_RX_MAXSZ);

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
		if (rxq->buf[i].skb != NULL) {
			pci_unmap_single(priv->pdev,
					 pci_unmap_addr(&rxq->buf[i], dma),
					 MWL8K_RX_MAXSZ, PCI_DMA_FROMDEVICE);
			pci_unmap_addr_set(&rxq->buf[i], dma, 0);

			kfree_skb(rxq->buf[i].skb);
			rxq->buf[i].skb = NULL;
		}
	}

	kfree(rxq->buf);
	rxq->buf = NULL;

	pci_free_consistent(priv->pdev,
			    MWL8K_RX_DESCS * priv->rxd_ops->rxd_size,
			    rxq->rxd, rxq->rxd_dma);
	rxq->rxd = NULL;
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
	while (rxq->rxd_count && limit--) {
		struct sk_buff *skb;
		void *rxd;
		int pkt_len;
		struct ieee80211_rx_status status;
		__le16 qos;

		skb = rxq->buf[rxq->head].skb;
		if (skb == NULL)
			break;

		rxd = rxq->rxd + (rxq->head * priv->rxd_ops->rxd_size);

		pkt_len = priv->rxd_ops->rxd_process(rxd, &status, &qos);
		if (pkt_len < 0)
			break;

		rxq->buf[rxq->head].skb = NULL;

		pci_unmap_single(priv->pdev,
				 pci_unmap_addr(&rxq->buf[rxq->head], dma),
				 MWL8K_RX_MAXSZ, PCI_DMA_FROMDEVICE);
		pci_unmap_addr_set(&rxq->buf[rxq->head], dma, 0);

		rxq->head++;
		if (rxq->head == MWL8K_RX_DESCS)
			rxq->head = 0;

		rxq->rxd_count--;

		skb_put(skb, pkt_len);
		mwl8k_remove_dma_header(skb, qos);

		/*
		 * Check for a pending join operation.  Save a
		 * copy of the beacon and schedule a tasklet to
		 * send a FINALIZE_JOIN command to the firmware.
		 */
		if (mwl8k_capture_bssid(priv, (void *)skb->data))
			mwl8k_save_beacon(hw, skb);

		memcpy(IEEE80211_SKB_RXCB(skb), &status, sizeof(status));
		ieee80211_rx_irqsafe(hw, skb);

		processed++;
	}

	return processed;
}


/*
 * Packet transmission.
 */

#define MWL8K_TXD_STATUS_OK			0x00000001
#define MWL8K_TXD_STATUS_OK_RETRY		0x00000002
#define MWL8K_TXD_STATUS_OK_MORE_RETRY		0x00000004
#define MWL8K_TXD_STATUS_MULTICAST_TX		0x00000008
#define MWL8K_TXD_STATUS_FW_OWNED		0x80000000

#define MWL8K_QOS_QLEN_UNSPEC			0xff00
#define MWL8K_QOS_ACK_POLICY_MASK		0x0060
#define MWL8K_QOS_ACK_POLICY_NORMAL		0x0000
#define MWL8K_QOS_ACK_POLICY_BLOCKACK		0x0060
#define MWL8K_QOS_EOSP				0x0010

struct mwl8k_tx_desc {
	__le32 status;
	__u8 data_rate;
	__u8 tx_priority;
	__le16 qos_control;
	__le32 pkt_phys_addr;
	__le16 pkt_len;
	__u8 dest_MAC_addr[ETH_ALEN];
	__le32 next_txd_phys_addr;
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

	txq->len = 0;
	txq->head = 0;
	txq->tail = 0;

	size = MWL8K_TX_DESCS * sizeof(struct mwl8k_tx_desc);

	txq->txd = pci_alloc_consistent(priv->pdev, size, &txq->txd_dma);
	if (txq->txd == NULL) {
		printk(KERN_ERR "%s: failed to alloc TX descriptors\n",
		       wiphy_name(hw->wiphy));
		return -ENOMEM;
	}
	memset(txq->txd, 0, size);

	txq->skb = kmalloc(MWL8K_TX_DESCS * sizeof(*txq->skb), GFP_KERNEL);
	if (txq->skb == NULL) {
		printk(KERN_ERR "%s: failed to alloc TX skbuff list\n",
		       wiphy_name(hw->wiphy));
		pci_free_consistent(priv->pdev, size, txq->txd, txq->txd_dma);
		return -ENOMEM;
	}
	memset(txq->skb, 0, MWL8K_TX_DESCS * sizeof(*txq->skb));

	for (i = 0; i < MWL8K_TX_DESCS; i++) {
		struct mwl8k_tx_desc *tx_desc;
		int nexti;

		tx_desc = txq->txd + i;
		nexti = (i + 1) % MWL8K_TX_DESCS;

		tx_desc->status = 0;
		tx_desc->next_txd_phys_addr =
			cpu_to_le32(txq->txd_dma + nexti * sizeof(*tx_desc));
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

static void mwl8k_dump_tx_rings(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;
	int i;

	for (i = 0; i < MWL8K_TX_QUEUES; i++) {
		struct mwl8k_tx_queue *txq = priv->txq + i;
		int fw_owned = 0;
		int drv_owned = 0;
		int unused = 0;
		int desc;

		for (desc = 0; desc < MWL8K_TX_DESCS; desc++) {
			struct mwl8k_tx_desc *tx_desc = txq->txd + desc;
			u32 status;

			status = le32_to_cpu(tx_desc->status);
			if (status & MWL8K_TXD_STATUS_FW_OWNED)
				fw_owned++;
			else
				drv_owned++;

			if (tx_desc->pkt_len == 0)
				unused++;
		}

		printk(KERN_ERR "%s: txq[%d] len=%d head=%d tail=%d "
		       "fw_owned=%d drv_owned=%d unused=%d\n",
		       wiphy_name(hw->wiphy), i,
		       txq->len, txq->head, txq->tail,
		       fw_owned, drv_owned, unused);
	}
}

/*
 * Must be called with priv->fw_mutex held and tx queues stopped.
 */
#define MWL8K_TX_WAIT_TIMEOUT_MS	5000

static int mwl8k_tx_wait_empty(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;
	DECLARE_COMPLETION_ONSTACK(tx_wait);
	int retry;
	int rc;

	might_sleep();

	/*
	 * The TX queues are stopped at this point, so this test
	 * doesn't need to take ->tx_lock.
	 */
	if (!priv->pending_tx_pkts)
		return 0;

	retry = 0;
	rc = 0;

	spin_lock_bh(&priv->tx_lock);
	priv->tx_wait = &tx_wait;
	while (!rc) {
		int oldcount;
		unsigned long timeout;

		oldcount = priv->pending_tx_pkts;

		spin_unlock_bh(&priv->tx_lock);
		timeout = wait_for_completion_timeout(&tx_wait,
			    msecs_to_jiffies(MWL8K_TX_WAIT_TIMEOUT_MS));
		spin_lock_bh(&priv->tx_lock);

		if (timeout) {
			WARN_ON(priv->pending_tx_pkts);
			if (retry) {
				printk(KERN_NOTICE "%s: tx rings drained\n",
				       wiphy_name(hw->wiphy));
			}
			break;
		}

		if (priv->pending_tx_pkts < oldcount) {
			printk(KERN_NOTICE "%s: waiting for tx rings "
			       "to drain (%d -> %d pkts)\n",
			       wiphy_name(hw->wiphy), oldcount,
			       priv->pending_tx_pkts);
			retry = 1;
			continue;
		}

		priv->tx_wait = NULL;

		printk(KERN_ERR "%s: tx rings stuck for %d ms\n",
		       wiphy_name(hw->wiphy), MWL8K_TX_WAIT_TIMEOUT_MS);
		mwl8k_dump_tx_rings(hw);

		rc = -ETIMEDOUT;
	}
	spin_unlock_bh(&priv->tx_lock);

	return rc;
}

#define MWL8K_TXD_SUCCESS(status)				\
	((status) & (MWL8K_TXD_STATUS_OK |			\
		     MWL8K_TXD_STATUS_OK_RETRY |		\
		     MWL8K_TXD_STATUS_OK_MORE_RETRY))

static int
mwl8k_txq_reclaim(struct ieee80211_hw *hw, int index, int limit, int force)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_tx_queue *txq = priv->txq + index;
	int processed;

	processed = 0;
	while (txq->len > 0 && limit--) {
		int tx;
		struct mwl8k_tx_desc *tx_desc;
		unsigned long addr;
		int size;
		struct sk_buff *skb;
		struct ieee80211_tx_info *info;
		u32 status;

		tx = txq->head;
		tx_desc = txq->txd + tx;

		status = le32_to_cpu(tx_desc->status);

		if (status & MWL8K_TXD_STATUS_FW_OWNED) {
			if (!force)
				break;
			tx_desc->status &=
				~cpu_to_le32(MWL8K_TXD_STATUS_FW_OWNED);
		}

		txq->head = (tx + 1) % MWL8K_TX_DESCS;
		BUG_ON(txq->len == 0);
		txq->len--;
		priv->pending_tx_pkts--;

		addr = le32_to_cpu(tx_desc->pkt_phys_addr);
		size = le16_to_cpu(tx_desc->pkt_len);
		skb = txq->skb[tx];
		txq->skb[tx] = NULL;

		BUG_ON(skb == NULL);
		pci_unmap_single(priv->pdev, addr, size, PCI_DMA_TODEVICE);

		mwl8k_remove_dma_header(skb, tx_desc->qos_control);

		/* Mark descriptor as unused */
		tx_desc->pkt_phys_addr = 0;
		tx_desc->pkt_len = 0;

		info = IEEE80211_SKB_CB(skb);
		ieee80211_tx_info_clear_status(info);
		if (MWL8K_TXD_SUCCESS(status))
			info->flags |= IEEE80211_TX_STAT_ACK;

		ieee80211_tx_status_irqsafe(hw, skb);

		processed++;
	}

	if (processed && priv->radio_on && !mutex_is_locked(&priv->fw_mutex))
		ieee80211_wake_queue(hw, index);

	return processed;
}

/* must be called only when the card's transmit is completely halted */
static void mwl8k_txq_deinit(struct ieee80211_hw *hw, int index)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_tx_queue *txq = priv->txq + index;

	mwl8k_txq_reclaim(hw, index, INT_MAX, 1);

	kfree(txq->skb);
	txq->skb = NULL;

	pci_free_consistent(priv->pdev,
			    MWL8K_TX_DESCS * sizeof(struct mwl8k_tx_desc),
			    txq->txd, txq->txd_dma);
	txq->txd = NULL;
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
		wh->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		wh->seq_ctrl |= cpu_to_le16(mwl8k_vif->seqno);
		mwl8k_vif->seqno += 0x10;
	}

	/* Setup firmware control bit fields for each frame type.  */
	txstatus = 0;
	txdatarate = 0;
	if (ieee80211_is_mgmt(wh->frame_control) ||
	    ieee80211_is_ctl(wh->frame_control)) {
		txdatarate = 0;
		qos |= MWL8K_QOS_QLEN_UNSPEC | MWL8K_QOS_EOSP;
	} else if (ieee80211_is_data(wh->frame_control)) {
		txdatarate = 1;
		if (is_multicast_ether_addr(wh->addr1))
			txstatus |= MWL8K_TXD_STATUS_MULTICAST_TX;

		qos &= ~MWL8K_QOS_ACK_POLICY_MASK;
		if (tx_info->flags & IEEE80211_TX_CTL_AMPDU)
			qos |= MWL8K_QOS_ACK_POLICY_BLOCKACK;
		else
			qos |= MWL8K_QOS_ACK_POLICY_NORMAL;
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

	BUG_ON(txq->skb[txq->tail] != NULL);
	txq->skb[txq->tail] = skb;

	tx = txq->txd + txq->tail;
	tx->data_rate = txdatarate;
	tx->tx_priority = index;
	tx->qos_control = cpu_to_le16(qos);
	tx->pkt_phys_addr = cpu_to_le32(dma);
	tx->pkt_len = cpu_to_le16(skb->len);
	tx->rate_info = 0;
	if (!priv->ap_fw && tx_info->control.sta != NULL)
		tx->peer_id = MWL8K_STA(tx_info->control.sta)->peer_id;
	else
		tx->peer_id = 0;
	wmb();
	tx->status = cpu_to_le32(MWL8K_TXD_STATUS_FW_OWNED | txstatus);

	txq->len++;
	priv->pending_tx_pkts++;

	txq->tail++;
	if (txq->tail == MWL8K_TX_DESCS)
		txq->tail = 0;

	if (txq->head == txq->tail)
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

/* Timeout firmware commands after 10s */
#define MWL8K_CMD_TIMEOUT_MS	10000

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
		int ms;

		ms = MWL8K_CMD_TIMEOUT_MS - jiffies_to_msecs(timeout);

		rc = cmd->result ? -EINVAL : 0;
		if (rc)
			printk(KERN_ERR "%s: Command %s error 0x%x\n",
			       wiphy_name(hw->wiphy),
			       mwl8k_cmd_name(cmd->code, buf, sizeof(buf)),
			       le16_to_cpu(cmd->result));
		else if (ms > 2000)
			printk(KERN_NOTICE "%s: Command %s took %d ms\n",
			       wiphy_name(hw->wiphy),
			       mwl8k_cmd_name(cmd->code, buf, sizeof(buf)),
			       ms);
	}

	return rc;
}

static int mwl8k_post_pervif_cmd(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct mwl8k_cmd_pkt *cmd)
{
	if (vif != NULL)
		cmd->macid = MWL8K_VIF(vif)->macid;
	return mwl8k_post_cmd(hw, cmd);
}

/*
 * Setup code shared between STA and AP firmware images.
 */
static void mwl8k_setup_2ghz_band(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;

	BUILD_BUG_ON(sizeof(priv->channels_24) != sizeof(mwl8k_channels_24));
	memcpy(priv->channels_24, mwl8k_channels_24, sizeof(mwl8k_channels_24));

	BUILD_BUG_ON(sizeof(priv->rates_24) != sizeof(mwl8k_rates_24));
	memcpy(priv->rates_24, mwl8k_rates_24, sizeof(mwl8k_rates_24));

	priv->band_24.band = IEEE80211_BAND_2GHZ;
	priv->band_24.channels = priv->channels_24;
	priv->band_24.n_channels = ARRAY_SIZE(mwl8k_channels_24);
	priv->band_24.bitrates = priv->rates_24;
	priv->band_24.n_bitrates = ARRAY_SIZE(mwl8k_rates_24);

	hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &priv->band_24;
}

static void mwl8k_setup_5ghz_band(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;

	BUILD_BUG_ON(sizeof(priv->channels_50) != sizeof(mwl8k_channels_50));
	memcpy(priv->channels_50, mwl8k_channels_50, sizeof(mwl8k_channels_50));

	BUILD_BUG_ON(sizeof(priv->rates_50) != sizeof(mwl8k_rates_50));
	memcpy(priv->rates_50, mwl8k_rates_50, sizeof(mwl8k_rates_50));

	priv->band_50.band = IEEE80211_BAND_5GHZ;
	priv->band_50.channels = priv->channels_50;
	priv->band_50.n_channels = ARRAY_SIZE(mwl8k_channels_50);
	priv->band_50.bitrates = priv->rates_50;
	priv->band_50.n_bitrates = ARRAY_SIZE(mwl8k_rates_50);

	hw->wiphy->bands[IEEE80211_BAND_5GHZ] = &priv->band_50;
}

/*
 * CMD_GET_HW_SPEC (STA version).
 */
struct mwl8k_cmd_get_hw_spec_sta {
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
	__le32 total_rxd;
} __attribute__((packed));

#define MWL8K_CAP_MAX_AMSDU		0x20000000
#define MWL8K_CAP_GREENFIELD		0x08000000
#define MWL8K_CAP_AMPDU			0x04000000
#define MWL8K_CAP_RX_STBC		0x01000000
#define MWL8K_CAP_TX_STBC		0x00800000
#define MWL8K_CAP_SHORTGI_40MHZ		0x00400000
#define MWL8K_CAP_SHORTGI_20MHZ		0x00200000
#define MWL8K_CAP_RX_ANTENNA_MASK	0x000e0000
#define MWL8K_CAP_TX_ANTENNA_MASK	0x0001c000
#define MWL8K_CAP_DELAY_BA		0x00003000
#define MWL8K_CAP_MIMO			0x00000200
#define MWL8K_CAP_40MHZ			0x00000100
#define MWL8K_CAP_BAND_MASK		0x00000007
#define MWL8K_CAP_5GHZ			0x00000004
#define MWL8K_CAP_2GHZ4			0x00000001

static void
mwl8k_set_ht_caps(struct ieee80211_hw *hw,
		  struct ieee80211_supported_band *band, u32 cap)
{
	int rx_streams;
	int tx_streams;

	band->ht_cap.ht_supported = 1;

	if (cap & MWL8K_CAP_MAX_AMSDU)
		band->ht_cap.cap |= IEEE80211_HT_CAP_MAX_AMSDU;
	if (cap & MWL8K_CAP_GREENFIELD)
		band->ht_cap.cap |= IEEE80211_HT_CAP_GRN_FLD;
	if (cap & MWL8K_CAP_AMPDU) {
		hw->flags |= IEEE80211_HW_AMPDU_AGGREGATION;
		band->ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
		band->ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE;
	}
	if (cap & MWL8K_CAP_RX_STBC)
		band->ht_cap.cap |= IEEE80211_HT_CAP_RX_STBC;
	if (cap & MWL8K_CAP_TX_STBC)
		band->ht_cap.cap |= IEEE80211_HT_CAP_TX_STBC;
	if (cap & MWL8K_CAP_SHORTGI_40MHZ)
		band->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;
	if (cap & MWL8K_CAP_SHORTGI_20MHZ)
		band->ht_cap.cap |= IEEE80211_HT_CAP_SGI_20;
	if (cap & MWL8K_CAP_DELAY_BA)
		band->ht_cap.cap |= IEEE80211_HT_CAP_DELAY_BA;
	if (cap & MWL8K_CAP_40MHZ)
		band->ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;

	rx_streams = hweight32(cap & MWL8K_CAP_RX_ANTENNA_MASK);
	tx_streams = hweight32(cap & MWL8K_CAP_TX_ANTENNA_MASK);

	band->ht_cap.mcs.rx_mask[0] = 0xff;
	if (rx_streams >= 2)
		band->ht_cap.mcs.rx_mask[1] = 0xff;
	if (rx_streams >= 3)
		band->ht_cap.mcs.rx_mask[2] = 0xff;
	band->ht_cap.mcs.rx_mask[4] = 0x01;
	band->ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;

	if (rx_streams != tx_streams) {
		band->ht_cap.mcs.tx_params |= IEEE80211_HT_MCS_TX_RX_DIFF;
		band->ht_cap.mcs.tx_params |= (tx_streams - 1) <<
				IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT;
	}
}

static void
mwl8k_set_caps(struct ieee80211_hw *hw, u32 caps)
{
	struct mwl8k_priv *priv = hw->priv;

	if ((caps & MWL8K_CAP_2GHZ4) || !(caps & MWL8K_CAP_BAND_MASK)) {
		mwl8k_setup_2ghz_band(hw);
		if (caps & MWL8K_CAP_MIMO)
			mwl8k_set_ht_caps(hw, &priv->band_24, caps);
	}

	if (caps & MWL8K_CAP_5GHZ) {
		mwl8k_setup_5ghz_band(hw);
		if (caps & MWL8K_CAP_MIMO)
			mwl8k_set_ht_caps(hw, &priv->band_50, caps);
	}
}

static int mwl8k_cmd_get_hw_spec_sta(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_get_hw_spec_sta *cmd;
	int rc;
	int i;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_GET_HW_SPEC);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));

	memset(cmd->perm_addr, 0xff, sizeof(cmd->perm_addr));
	cmd->ps_cookie = cpu_to_le32(priv->cookie_dma);
	cmd->rx_queue_ptr = cpu_to_le32(priv->rxq[0].rxd_dma);
	cmd->num_tx_queues = cpu_to_le32(MWL8K_TX_QUEUES);
	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		cmd->tx_queue_ptrs[i] = cpu_to_le32(priv->txq[i].txd_dma);
	cmd->num_tx_desc_per_queue = cpu_to_le32(MWL8K_TX_DESCS);
	cmd->total_rxd = cpu_to_le32(MWL8K_RX_DESCS);

	rc = mwl8k_post_cmd(hw, &cmd->header);

	if (!rc) {
		SET_IEEE80211_PERM_ADDR(hw, cmd->perm_addr);
		priv->num_mcaddrs = le16_to_cpu(cmd->num_mcaddrs);
		priv->fw_rev = le32_to_cpu(cmd->fw_rev);
		priv->hw_rev = cmd->hw_rev;
		mwl8k_set_caps(hw, le32_to_cpu(cmd->caps));
		priv->ap_macids_supported = 0x00000000;
		priv->sta_macids_supported = 0x00000001;
	}

	kfree(cmd);
	return rc;
}

/*
 * CMD_GET_HW_SPEC (AP version).
 */
struct mwl8k_cmd_get_hw_spec_ap {
	struct mwl8k_cmd_pkt header;
	__u8 hw_rev;
	__u8 host_interface;
	__le16 num_wcb;
	__le16 num_mcaddrs;
	__u8 perm_addr[ETH_ALEN];
	__le16 region_code;
	__le16 num_antenna;
	__le32 fw_rev;
	__le32 wcbbase0;
	__le32 rxwrptr;
	__le32 rxrdptr;
	__le32 ps_cookie;
	__le32 wcbbase1;
	__le32 wcbbase2;
	__le32 wcbbase3;
} __attribute__((packed));

static int mwl8k_cmd_get_hw_spec_ap(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_get_hw_spec_ap *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_GET_HW_SPEC);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));

	memset(cmd->perm_addr, 0xff, sizeof(cmd->perm_addr));
	cmd->ps_cookie = cpu_to_le32(priv->cookie_dma);

	rc = mwl8k_post_cmd(hw, &cmd->header);

	if (!rc) {
		int off;

		SET_IEEE80211_PERM_ADDR(hw, cmd->perm_addr);
		priv->num_mcaddrs = le16_to_cpu(cmd->num_mcaddrs);
		priv->fw_rev = le32_to_cpu(cmd->fw_rev);
		priv->hw_rev = cmd->hw_rev;
		mwl8k_setup_2ghz_band(hw);
		priv->ap_macids_supported = 0x000000ff;
		priv->sta_macids_supported = 0x00000000;

		off = le32_to_cpu(cmd->wcbbase0) & 0xffff;
		iowrite32(cpu_to_le32(priv->txq[0].txd_dma), priv->sram + off);

		off = le32_to_cpu(cmd->rxwrptr) & 0xffff;
		iowrite32(cpu_to_le32(priv->rxq[0].rxd_dma), priv->sram + off);

		off = le32_to_cpu(cmd->rxrdptr) & 0xffff;
		iowrite32(cpu_to_le32(priv->rxq[0].rxd_dma), priv->sram + off);

		off = le32_to_cpu(cmd->wcbbase1) & 0xffff;
		iowrite32(cpu_to_le32(priv->txq[1].txd_dma), priv->sram + off);

		off = le32_to_cpu(cmd->wcbbase2) & 0xffff;
		iowrite32(cpu_to_le32(priv->txq[2].txd_dma), priv->sram + off);

		off = le32_to_cpu(cmd->wcbbase3) & 0xffff;
		iowrite32(cpu_to_le32(priv->txq[3].txd_dma), priv->sram + off);
	}

	kfree(cmd);
	return rc;
}

/*
 * CMD_SET_HW_SPEC.
 */
struct mwl8k_cmd_set_hw_spec {
	struct mwl8k_cmd_pkt header;
	__u8 hw_rev;
	__u8 host_interface;
	__le16 num_mcaddrs;
	__u8 perm_addr[ETH_ALEN];
	__le16 region_code;
	__le32 fw_rev;
	__le32 ps_cookie;
	__le32 caps;
	__le32 rx_queue_ptr;
	__le32 num_tx_queues;
	__le32 tx_queue_ptrs[MWL8K_TX_QUEUES];
	__le32 flags;
	__le32 num_tx_desc_per_queue;
	__le32 total_rxd;
} __attribute__((packed));

#define MWL8K_SET_HW_SPEC_FLAG_HOST_DECR_MGMT		0x00000080
#define MWL8K_SET_HW_SPEC_FLAG_HOSTFORM_PROBERESP	0x00000020
#define MWL8K_SET_HW_SPEC_FLAG_HOSTFORM_BEACON		0x00000010

static int mwl8k_cmd_set_hw_spec(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_set_hw_spec *cmd;
	int rc;
	int i;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_HW_SPEC);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));

	cmd->ps_cookie = cpu_to_le32(priv->cookie_dma);
	cmd->rx_queue_ptr = cpu_to_le32(priv->rxq[0].rxd_dma);
	cmd->num_tx_queues = cpu_to_le32(MWL8K_TX_QUEUES);
	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		cmd->tx_queue_ptrs[i] = cpu_to_le32(priv->txq[i].txd_dma);
	cmd->flags = cpu_to_le32(MWL8K_SET_HW_SPEC_FLAG_HOST_DECR_MGMT |
				 MWL8K_SET_HW_SPEC_FLAG_HOSTFORM_PROBERESP |
				 MWL8K_SET_HW_SPEC_FLAG_HOSTFORM_BEACON);
	cmd->num_tx_desc_per_queue = cpu_to_le32(MWL8K_TX_DESCS);
	cmd->total_rxd = cpu_to_le32(MWL8K_RX_DESCS);

	rc = mwl8k_post_cmd(hw, &cmd->header);
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
			      struct netdev_hw_addr_list *mc_list)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_mac_multicast_adr *cmd;
	int size;
	int mc_count = 0;

	if (mc_list)
		mc_count = netdev_hw_addr_list_count(mc_list);

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
		struct netdev_hw_addr *ha;
		int i = 0;

		cmd->action |= cpu_to_le16(MWL8K_ENABLE_RX_MULTICAST);
		cmd->numaddr = cpu_to_le16(mc_count);
		netdev_hw_addr_list_for_each(ha, mc_list) {
			memcpy(cmd->addr[i], ha->addr, ETH_ALEN);
		}
	}

	return &cmd->header;
}

/*
 * CMD_GET_STAT.
 */
struct mwl8k_cmd_get_stat {
	struct mwl8k_cmd_pkt header;
	__le32 stats[64];
} __attribute__((packed));

#define MWL8K_STAT_ACK_FAILURE	9
#define MWL8K_STAT_RTS_FAILURE	12
#define MWL8K_STAT_FCS_ERROR	24
#define MWL8K_STAT_RTS_SUCCESS	11

static int mwl8k_cmd_get_stat(struct ieee80211_hw *hw,
			      struct ieee80211_low_level_stats *stats)
{
	struct mwl8k_cmd_get_stat *cmd;
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
 * CMD_RADIO_CONTROL.
 */
struct mwl8k_cmd_radio_control {
	struct mwl8k_cmd_pkt header;
	__le16 action;
	__le16 control;
	__le16 radio_on;
} __attribute__((packed));

static int
mwl8k_cmd_radio_control(struct ieee80211_hw *hw, bool enable, bool force)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_radio_control *cmd;
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

static int mwl8k_cmd_radio_disable(struct ieee80211_hw *hw)
{
	return mwl8k_cmd_radio_control(hw, 0, 0);
}

static int mwl8k_cmd_radio_enable(struct ieee80211_hw *hw)
{
	return mwl8k_cmd_radio_control(hw, 1, 0);
}

static int
mwl8k_set_radio_preamble(struct ieee80211_hw *hw, bool short_preamble)
{
	struct mwl8k_priv *priv = hw->priv;

	priv->radio_short_preamble = short_preamble;

	return mwl8k_cmd_radio_control(hw, 1, 1);
}

/*
 * CMD_RF_TX_POWER.
 */
#define MWL8K_TX_POWER_LEVEL_TOTAL	8

struct mwl8k_cmd_rf_tx_power {
	struct mwl8k_cmd_pkt header;
	__le16 action;
	__le16 support_level;
	__le16 current_level;
	__le16 reserved;
	__le16 power_level_list[MWL8K_TX_POWER_LEVEL_TOTAL];
} __attribute__((packed));

static int mwl8k_cmd_rf_tx_power(struct ieee80211_hw *hw, int dBm)
{
	struct mwl8k_cmd_rf_tx_power *cmd;
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
 * CMD_RF_ANTENNA.
 */
struct mwl8k_cmd_rf_antenna {
	struct mwl8k_cmd_pkt header;
	__le16 antenna;
	__le16 mode;
} __attribute__((packed));

#define MWL8K_RF_ANTENNA_RX		1
#define MWL8K_RF_ANTENNA_TX		2

static int
mwl8k_cmd_rf_antenna(struct ieee80211_hw *hw, int antenna, int mask)
{
	struct mwl8k_cmd_rf_antenna *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_RF_ANTENNA);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->antenna = cpu_to_le16(antenna);
	cmd->mode = cpu_to_le16(mask);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_BEACON.
 */
struct mwl8k_cmd_set_beacon {
	struct mwl8k_cmd_pkt header;
	__le16 beacon_len;
	__u8 beacon[0];
};

static int mwl8k_cmd_set_beacon(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif, u8 *beacon, int len)
{
	struct mwl8k_cmd_set_beacon *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd) + len, GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_BEACON);
	cmd->header.length = cpu_to_le16(sizeof(*cmd) + len);
	cmd->beacon_len = cpu_to_le16(len);
	memcpy(cmd->beacon, beacon, len);

	rc = mwl8k_post_pervif_cmd(hw, vif, &cmd->header);
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
mwl8k_cmd_set_post_scan(struct ieee80211_hw *hw, const __u8 *mac)
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
				    struct ieee80211_conf *conf)
{
	struct ieee80211_channel *channel = conf->channel;
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
		cmd->channel_flags |= cpu_to_le32(0x00000001);
	else if (channel->band == IEEE80211_BAND_5GHZ)
		cmd->channel_flags |= cpu_to_le32(0x00000004);

	if (conf->channel_type == NL80211_CHAN_NO_HT ||
	    conf->channel_type == NL80211_CHAN_HT20)
		cmd->channel_flags |= cpu_to_le32(0x00000080);
	else if (conf->channel_type == NL80211_CHAN_HT40MINUS)
		cmd->channel_flags |= cpu_to_le32(0x000001900);
	else if (conf->channel_type == NL80211_CHAN_HT40PLUS)
		cmd->channel_flags |= cpu_to_le32(0x000000900);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_AID.
 */
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
	__u8	supp_rates[14];
} __attribute__((packed));

static void legacy_rate_mask_to_array(u8 *rates, u32 mask)
{
	int i;
	int j;

	/*
	 * Clear nonstandard rates 4 and 13.
	 */
	mask &= 0x1fef;

	for (i = 0, j = 0; i < 14; i++) {
		if (mask & (1 << i))
			rates[j++] = mwl8k_rates_24[i].hw_value;
	}
}

static int
mwl8k_cmd_set_aid(struct ieee80211_hw *hw,
		  struct ieee80211_vif *vif, u32 legacy_rate_mask)
{
	struct mwl8k_cmd_update_set_aid *cmd;
	u16 prot_mode;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_AID);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->aid = cpu_to_le16(vif->bss_conf.aid);
	memcpy(cmd->bssid, vif->bss_conf.bssid, ETH_ALEN);

	if (vif->bss_conf.use_cts_prot) {
		prot_mode = MWL8K_FRAME_PROT_11G;
	} else {
		switch (vif->bss_conf.ht_operation_mode &
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

	legacy_rate_mask_to_array(cmd->supp_rates, legacy_rate_mask);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_RATE.
 */
struct mwl8k_cmd_set_rate {
	struct	mwl8k_cmd_pkt header;
	__u8	legacy_rates[14];

	/* Bitmap for supported MCS codes.  */
	__u8	mcs_set[16];
	__u8	reserved[16];
} __attribute__((packed));

static int
mwl8k_cmd_set_rate(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   u32 legacy_rate_mask, u8 *mcs_rates)
{
	struct mwl8k_cmd_set_rate *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_RATE);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	legacy_rate_mask_to_array(cmd->legacy_rates, legacy_rate_mask);
	memcpy(cmd->mcs_set, mcs_rates, 16);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_FINALIZE_JOIN.
 */
#define MWL8K_FJ_BEACON_MAXLEN	128

struct mwl8k_cmd_finalize_join {
	struct mwl8k_cmd_pkt header;
	__le32 sleep_interval;	/* Number of beacon periods to sleep */
	__u8 beacon_data[MWL8K_FJ_BEACON_MAXLEN];
} __attribute__((packed));

static int mwl8k_cmd_finalize_join(struct ieee80211_hw *hw, void *frame,
				   int framelen, int dtim)
{
	struct mwl8k_cmd_finalize_join *cmd;
	struct ieee80211_mgmt *payload = frame;
	int payload_len;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_FINALIZE_JOIN);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->sleep_interval = cpu_to_le32(dtim ? dtim : 1);

	payload_len = framelen - ieee80211_hdrlen(payload->frame_control);
	if (payload_len < 0)
		payload_len = 0;
	else if (payload_len > MWL8K_FJ_BEACON_MAXLEN)
		payload_len = MWL8K_FJ_BEACON_MAXLEN;

	memcpy(cmd->beacon_data, &payload->u.beacon, payload_len);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_RTS_THRESHOLD.
 */
struct mwl8k_cmd_set_rts_threshold {
	struct mwl8k_cmd_pkt header;
	__le16 action;
	__le16 threshold;
} __attribute__((packed));

static int
mwl8k_cmd_set_rts_threshold(struct ieee80211_hw *hw, int rts_thresh)
{
	struct mwl8k_cmd_set_rts_threshold *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_RTS_THRESHOLD);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le16(MWL8K_CMD_SET);
	cmd->threshold = cpu_to_le16(rts_thresh);

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
 * CMD_SET_EDCA_PARAMS.
 */
struct mwl8k_cmd_set_edca_params {
	struct mwl8k_cmd_pkt header;

	/* See MWL8K_SET_EDCA_XXX below */
	__le16 action;

	/* TX opportunity in units of 32 us */
	__le16 txop;

	union {
		struct {
			/* Log exponent of max contention period: 0...15 */
			__le32 log_cw_max;

			/* Log exponent of min contention period: 0...15 */
			__le32 log_cw_min;

			/* Adaptive interframe spacing in units of 32us */
			__u8 aifs;

			/* TX queue to configure */
			__u8 txq;
		} ap;
		struct {
			/* Log exponent of max contention period: 0...15 */
			__u8 log_cw_max;

			/* Log exponent of min contention period: 0...15 */
			__u8 log_cw_min;

			/* Adaptive interframe spacing in units of 32us */
			__u8 aifs;

			/* TX queue to configure */
			__u8 txq;
		} sta;
	};
} __attribute__((packed));

#define MWL8K_SET_EDCA_CW	0x01
#define MWL8K_SET_EDCA_TXOP	0x02
#define MWL8K_SET_EDCA_AIFS	0x04

#define MWL8K_SET_EDCA_ALL	(MWL8K_SET_EDCA_CW | \
				 MWL8K_SET_EDCA_TXOP | \
				 MWL8K_SET_EDCA_AIFS)

static int
mwl8k_cmd_set_edca_params(struct ieee80211_hw *hw, __u8 qnum,
			  __u16 cw_min, __u16 cw_max,
			  __u8 aifs, __u16 txop)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_set_edca_params *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_EDCA_PARAMS);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le16(MWL8K_SET_EDCA_ALL);
	cmd->txop = cpu_to_le16(txop);
	if (priv->ap_fw) {
		cmd->ap.log_cw_max = cpu_to_le32(ilog2(cw_max + 1));
		cmd->ap.log_cw_min = cpu_to_le32(ilog2(cw_min + 1));
		cmd->ap.aifs = aifs;
		cmd->ap.txq = qnum;
	} else {
		cmd->sta.log_cw_max = (u8)ilog2(cw_max + 1);
		cmd->sta.log_cw_min = (u8)ilog2(cw_min + 1);
		cmd->sta.aifs = aifs;
		cmd->sta.txq = qnum;
	}

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_WMM_MODE.
 */
struct mwl8k_cmd_set_wmm_mode {
	struct mwl8k_cmd_pkt header;
	__le16 action;
} __attribute__((packed));

static int mwl8k_cmd_set_wmm_mode(struct ieee80211_hw *hw, bool enable)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_set_wmm_mode *cmd;
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
 * CMD_USE_FIXED_RATE (STA version).
 */
struct mwl8k_cmd_use_fixed_rate_sta {
	struct mwl8k_cmd_pkt header;
	__le32 action;
	__le32 allow_rate_drop;
	__le32 num_rates;
	struct {
		__le32 is_ht_rate;
		__le32 enable_retry;
		__le32 rate;
		__le32 retry_count;
	} rate_entry[8];
	__le32 rate_type;
	__le32 reserved1;
	__le32 reserved2;
} __attribute__((packed));

#define MWL8K_USE_AUTO_RATE	0x0002
#define MWL8K_UCAST_RATE	0

static int mwl8k_cmd_use_fixed_rate_sta(struct ieee80211_hw *hw)
{
	struct mwl8k_cmd_use_fixed_rate_sta *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_USE_FIXED_RATE);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le32(MWL8K_USE_AUTO_RATE);
	cmd->rate_type = cpu_to_le32(MWL8K_UCAST_RATE);

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_USE_FIXED_RATE (AP version).
 */
struct mwl8k_cmd_use_fixed_rate_ap {
	struct mwl8k_cmd_pkt header;
	__le32 action;
	__le32 allow_rate_drop;
	__le32 num_rates;
	struct mwl8k_rate_entry_ap {
		__le32 is_ht_rate;
		__le32 enable_retry;
		__le32 rate;
		__le32 retry_count;
	} rate_entry[4];
	u8 multicast_rate;
	u8 multicast_rate_type;
	u8 management_rate;
} __attribute__((packed));

static int
mwl8k_cmd_use_fixed_rate_ap(struct ieee80211_hw *hw, int mcast, int mgmt)
{
	struct mwl8k_cmd_use_fixed_rate_ap *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_USE_FIXED_RATE);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le32(MWL8K_USE_AUTO_RATE);
	cmd->multicast_rate = mcast;
	cmd->management_rate = mgmt;

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

static int mwl8k_cmd_enable_sniffer(struct ieee80211_hw *hw, bool enable)
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
	union {
		struct {
			__le16 mac_type;
			__u8 mac_addr[ETH_ALEN];
		} mbss;
		__u8 mac_addr[ETH_ALEN];
	};
} __attribute__((packed));

#define MWL8K_MAC_TYPE_PRIMARY_CLIENT		0
#define MWL8K_MAC_TYPE_SECONDARY_CLIENT		1
#define MWL8K_MAC_TYPE_PRIMARY_AP		2
#define MWL8K_MAC_TYPE_SECONDARY_AP		3

static int mwl8k_cmd_set_mac_addr(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif, u8 *mac)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_vif *mwl8k_vif = MWL8K_VIF(vif);
	struct mwl8k_cmd_set_mac_addr *cmd;
	int mac_type;
	int rc;

	mac_type = MWL8K_MAC_TYPE_PRIMARY_AP;
	if (vif != NULL && vif->type == NL80211_IFTYPE_STATION) {
		if (mwl8k_vif->macid + 1 == ffs(priv->sta_macids_supported))
			mac_type = MWL8K_MAC_TYPE_PRIMARY_CLIENT;
		else
			mac_type = MWL8K_MAC_TYPE_SECONDARY_CLIENT;
	} else if (vif != NULL && vif->type == NL80211_IFTYPE_AP) {
		if (mwl8k_vif->macid + 1 == ffs(priv->ap_macids_supported))
			mac_type = MWL8K_MAC_TYPE_PRIMARY_AP;
		else
			mac_type = MWL8K_MAC_TYPE_SECONDARY_AP;
	}

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_MAC_ADDR);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	if (priv->ap_fw) {
		cmd->mbss.mac_type = cpu_to_le16(mac_type);
		memcpy(cmd->mbss.mac_addr, mac, ETH_ALEN);
	} else {
		memcpy(cmd->mac_addr, mac, ETH_ALEN);
	}

	rc = mwl8k_post_pervif_cmd(hw, vif, &cmd->header);
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

static int mwl8k_cmd_set_rateadapt_mode(struct ieee80211_hw *hw, __u16 mode)
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
 * CMD_BSS_START.
 */
struct mwl8k_cmd_bss_start {
	struct mwl8k_cmd_pkt header;
	__le32 enable;
} __attribute__((packed));

static int mwl8k_cmd_bss_start(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif, int enable)
{
	struct mwl8k_cmd_bss_start *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_BSS_START);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->enable = cpu_to_le32(enable);

	rc = mwl8k_post_pervif_cmd(hw, vif, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_SET_NEW_STN.
 */
struct mwl8k_cmd_set_new_stn {
	struct mwl8k_cmd_pkt header;
	__le16 aid;
	__u8 mac_addr[6];
	__le16 stn_id;
	__le16 action;
	__le16 rsvd;
	__le32 legacy_rates;
	__u8 ht_rates[4];
	__le16 cap_info;
	__le16 ht_capabilities_info;
	__u8 mac_ht_param_info;
	__u8 rev;
	__u8 control_channel;
	__u8 add_channel;
	__le16 op_mode;
	__le16 stbc;
	__u8 add_qos_info;
	__u8 is_qos_sta;
	__le32 fw_sta_ptr;
} __attribute__((packed));

#define MWL8K_STA_ACTION_ADD		0
#define MWL8K_STA_ACTION_REMOVE		2

static int mwl8k_cmd_set_new_stn_add(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta)
{
	struct mwl8k_cmd_set_new_stn *cmd;
	u32 rates;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_NEW_STN);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->aid = cpu_to_le16(sta->aid);
	memcpy(cmd->mac_addr, sta->addr, ETH_ALEN);
	cmd->stn_id = cpu_to_le16(sta->aid);
	cmd->action = cpu_to_le16(MWL8K_STA_ACTION_ADD);
	if (hw->conf.channel->band == IEEE80211_BAND_2GHZ)
		rates = sta->supp_rates[IEEE80211_BAND_2GHZ];
	else
		rates = sta->supp_rates[IEEE80211_BAND_5GHZ] << 5;
	cmd->legacy_rates = cpu_to_le32(rates);
	if (sta->ht_cap.ht_supported) {
		cmd->ht_rates[0] = sta->ht_cap.mcs.rx_mask[0];
		cmd->ht_rates[1] = sta->ht_cap.mcs.rx_mask[1];
		cmd->ht_rates[2] = sta->ht_cap.mcs.rx_mask[2];
		cmd->ht_rates[3] = sta->ht_cap.mcs.rx_mask[3];
		cmd->ht_capabilities_info = cpu_to_le16(sta->ht_cap.cap);
		cmd->mac_ht_param_info = (sta->ht_cap.ampdu_factor & 3) |
			((sta->ht_cap.ampdu_density & 7) << 2);
		cmd->is_qos_sta = 1;
	}

	rc = mwl8k_post_pervif_cmd(hw, vif, &cmd->header);
	kfree(cmd);

	return rc;
}

static int mwl8k_cmd_set_new_stn_add_self(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif)
{
	struct mwl8k_cmd_set_new_stn *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_NEW_STN);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	memcpy(cmd->mac_addr, vif->addr, ETH_ALEN);

	rc = mwl8k_post_pervif_cmd(hw, vif, &cmd->header);
	kfree(cmd);

	return rc;
}

static int mwl8k_cmd_set_new_stn_del(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif, u8 *addr)
{
	struct mwl8k_cmd_set_new_stn *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_SET_NEW_STN);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	memcpy(cmd->mac_addr, addr, ETH_ALEN);
	cmd->action = cpu_to_le16(MWL8K_STA_ACTION_REMOVE);

	rc = mwl8k_post_pervif_cmd(hw, vif, &cmd->header);
	kfree(cmd);

	return rc;
}

/*
 * CMD_UPDATE_STADB.
 */
struct ewc_ht_info {
	__le16	control1;
	__le16	control2;
	__le16	control3;
} __attribute__((packed));

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
	__u8	legacy_rates[12];

	/* HT rate table. Intersection of our rates and peer rates.  */
	__u8	ht_rates[16];
	__u8	pad[16];

	/* If set, interoperability mode, no proprietary extensions.  */
	__u8	interop;
	__u8	pad2;
	__u8	station_id;
	__le16	amsdu_enabled;
} __attribute__((packed));

struct mwl8k_cmd_update_stadb {
	struct mwl8k_cmd_pkt header;

	/* See STADB_ACTION_TYPE */
	__le32	action;

	/* Peer MAC address */
	__u8	peer_addr[ETH_ALEN];

	__le32	reserved;

	/* Peer info - valid during add/update.  */
	struct peer_capability_info	peer_info;
} __attribute__((packed));

#define MWL8K_STA_DB_MODIFY_ENTRY	1
#define MWL8K_STA_DB_DEL_ENTRY		2

/* Peer Entry flags - used to define the type of the peer node */
#define MWL8K_PEER_TYPE_ACCESSPOINT	2

static int mwl8k_cmd_update_stadb_add(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta)
{
	struct mwl8k_cmd_update_stadb *cmd;
	struct peer_capability_info *p;
	u32 rates;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_UPDATE_STADB);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le32(MWL8K_STA_DB_MODIFY_ENTRY);
	memcpy(cmd->peer_addr, sta->addr, ETH_ALEN);

	p = &cmd->peer_info;
	p->peer_type = MWL8K_PEER_TYPE_ACCESSPOINT;
	p->basic_caps = cpu_to_le16(vif->bss_conf.assoc_capability);
	p->ht_support = sta->ht_cap.ht_supported;
	p->ht_caps = sta->ht_cap.cap;
	p->extended_ht_caps = (sta->ht_cap.ampdu_factor & 3) |
		((sta->ht_cap.ampdu_density & 7) << 2);
	if (hw->conf.channel->band == IEEE80211_BAND_2GHZ)
		rates = sta->supp_rates[IEEE80211_BAND_2GHZ];
	else
		rates = sta->supp_rates[IEEE80211_BAND_5GHZ] << 5;
	legacy_rate_mask_to_array(p->legacy_rates, rates);
	memcpy(p->ht_rates, sta->ht_cap.mcs.rx_mask, 16);
	p->interop = 1;
	p->amsdu_enabled = 0;

	rc = mwl8k_post_cmd(hw, &cmd->header);
	kfree(cmd);

	return rc ? rc : p->station_id;
}

static int mwl8k_cmd_update_stadb_del(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif, u8 *addr)
{
	struct mwl8k_cmd_update_stadb *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	cmd->header.code = cpu_to_le16(MWL8K_CMD_UPDATE_STADB);
	cmd->header.length = cpu_to_le16(sizeof(*cmd));
	cmd->action = cpu_to_le32(MWL8K_STA_DB_DEL_ENTRY);
	memcpy(cmd->peer_addr, addr, ETH_ALEN);

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
	if (!status)
		return IRQ_NONE;

	if (status & MWL8K_A2H_INT_TX_DONE) {
		status &= ~MWL8K_A2H_INT_TX_DONE;
		tasklet_schedule(&priv->poll_tx_task);
	}

	if (status & MWL8K_A2H_INT_RX_READY) {
		status &= ~MWL8K_A2H_INT_RX_READY;
		tasklet_schedule(&priv->poll_rx_task);
	}

	if (status)
		iowrite32(~status, priv->regs + MWL8K_HIU_A2H_INTERRUPT_STATUS);

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

static void mwl8k_tx_poll(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;
	struct mwl8k_priv *priv = hw->priv;
	int limit;
	int i;

	limit = 32;

	spin_lock_bh(&priv->tx_lock);

	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		limit -= mwl8k_txq_reclaim(hw, i, limit, 0);

	if (!priv->pending_tx_pkts && priv->tx_wait != NULL) {
		complete(priv->tx_wait);
		priv->tx_wait = NULL;
	}

	spin_unlock_bh(&priv->tx_lock);

	if (limit) {
		writel(~MWL8K_A2H_INT_TX_DONE,
		       priv->regs + MWL8K_HIU_A2H_INTERRUPT_STATUS);
	} else {
		tasklet_schedule(&priv->poll_tx_task);
	}
}

static void mwl8k_rx_poll(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;
	struct mwl8k_priv *priv = hw->priv;
	int limit;

	limit = 32;
	limit -= rxq_process(hw, 0, limit);
	limit -= rxq_refill(hw, 0, limit);

	if (limit) {
		writel(~MWL8K_A2H_INT_RX_READY,
		       priv->regs + MWL8K_HIU_A2H_INTERRUPT_STATUS);
	} else {
		tasklet_schedule(&priv->poll_rx_task);
	}
}


/*
 * Core driver operations.
 */
static int mwl8k_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct mwl8k_priv *priv = hw->priv;
	int index = skb_get_queue_mapping(skb);
	int rc;

	if (!priv->radio_on) {
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

	rc = request_irq(priv->pdev->irq, mwl8k_interrupt,
			 IRQF_SHARED, MWL8K_NAME, hw);
	if (rc) {
		printk(KERN_ERR "%s: failed to register IRQ handler\n",
		       wiphy_name(hw->wiphy));
		return -EIO;
	}

	/* Enable TX reclaim and RX tasklets.  */
	tasklet_enable(&priv->poll_tx_task);
	tasklet_enable(&priv->poll_rx_task);

	/* Enable interrupts */
	iowrite32(MWL8K_A2H_EVENTS, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);

	rc = mwl8k_fw_lock(hw);
	if (!rc) {
		rc = mwl8k_cmd_radio_enable(hw);

		if (!priv->ap_fw) {
			if (!rc)
				rc = mwl8k_cmd_enable_sniffer(hw, 0);

			if (!rc)
				rc = mwl8k_cmd_set_pre_scan(hw);

			if (!rc)
				rc = mwl8k_cmd_set_post_scan(hw,
						"\x00\x00\x00\x00\x00\x00");
		}

		if (!rc)
			rc = mwl8k_cmd_set_rateadapt_mode(hw, 0);

		if (!rc)
			rc = mwl8k_cmd_set_wmm_mode(hw, 0);

		mwl8k_fw_unlock(hw);
	}

	if (rc) {
		iowrite32(0, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);
		free_irq(priv->pdev->irq, hw);
		tasklet_disable(&priv->poll_tx_task);
		tasklet_disable(&priv->poll_rx_task);
	}

	return rc;
}

static void mwl8k_stop(struct ieee80211_hw *hw)
{
	struct mwl8k_priv *priv = hw->priv;
	int i;

	mwl8k_cmd_radio_disable(hw);

	ieee80211_stop_queues(hw);

	/* Disable interrupts */
	iowrite32(0, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);
	free_irq(priv->pdev->irq, hw);

	/* Stop finalize join worker */
	cancel_work_sync(&priv->finalize_join_worker);
	if (priv->beacon_skb != NULL)
		dev_kfree_skb(priv->beacon_skb);

	/* Stop TX reclaim and RX tasklets.  */
	tasklet_disable(&priv->poll_tx_task);
	tasklet_disable(&priv->poll_rx_task);

	/* Return all skbs to mac80211 */
	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		mwl8k_txq_reclaim(hw, i, INT_MAX, 1);
}

static int mwl8k_add_interface(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_vif *mwl8k_vif;
	u32 macids_supported;
	int macid;

	/*
	 * Reject interface creation if sniffer mode is active, as
	 * STA operation is mutually exclusive with hardware sniffer
	 * mode.  (Sniffer mode is only used on STA firmware.)
	 */
	if (priv->sniffer_enabled) {
		printk(KERN_INFO "%s: unable to create STA "
		       "interface due to sniffer mode being enabled\n",
		       wiphy_name(hw->wiphy));
		return -EINVAL;
	}


	switch (vif->type) {
	case NL80211_IFTYPE_AP:
		macids_supported = priv->ap_macids_supported;
		break;
	case NL80211_IFTYPE_STATION:
		macids_supported = priv->sta_macids_supported;
		break;
	default:
		return -EINVAL;
	}

	macid = ffs(macids_supported & ~priv->macids_used);
	if (!macid--)
		return -EBUSY;

	/* Setup driver private area. */
	mwl8k_vif = MWL8K_VIF(vif);
	memset(mwl8k_vif, 0, sizeof(*mwl8k_vif));
	mwl8k_vif->vif = vif;
	mwl8k_vif->macid = macid;
	mwl8k_vif->seqno = 0;

	/* Set the mac address.  */
	mwl8k_cmd_set_mac_addr(hw, vif, vif->addr);

	if (priv->ap_fw)
		mwl8k_cmd_set_new_stn_add_self(hw, vif);

	priv->macids_used |= 1 << mwl8k_vif->macid;
	list_add_tail(&mwl8k_vif->list, &priv->vif_list);

	return 0;
}

static void mwl8k_remove_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_vif *mwl8k_vif = MWL8K_VIF(vif);

	if (priv->ap_fw)
		mwl8k_cmd_set_new_stn_del(hw, vif, vif->addr);

	mwl8k_cmd_set_mac_addr(hw, vif, "\x00\x00\x00\x00\x00\x00");

	priv->macids_used &= ~(1 << mwl8k_vif->macid);
	list_del(&mwl8k_vif->list);
}

static int mwl8k_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ieee80211_conf *conf = &hw->conf;
	struct mwl8k_priv *priv = hw->priv;
	int rc;

	if (conf->flags & IEEE80211_CONF_IDLE) {
		mwl8k_cmd_radio_disable(hw);
		return 0;
	}

	rc = mwl8k_fw_lock(hw);
	if (rc)
		return rc;

	rc = mwl8k_cmd_radio_enable(hw);
	if (rc)
		goto out;

	rc = mwl8k_cmd_set_rf_channel(hw, conf);
	if (rc)
		goto out;

	if (conf->power_level > 18)
		conf->power_level = 18;
	rc = mwl8k_cmd_rf_tx_power(hw, conf->power_level);
	if (rc)
		goto out;

	if (priv->ap_fw) {
		rc = mwl8k_cmd_rf_antenna(hw, MWL8K_RF_ANTENNA_RX, 0x7);
		if (!rc)
			rc = mwl8k_cmd_rf_antenna(hw, MWL8K_RF_ANTENNA_TX, 0x7);
	} else {
		rc = mwl8k_cmd_mimo_config(hw, 0x7, 0x7);
	}

out:
	mwl8k_fw_unlock(hw);

	return rc;
}

static void
mwl8k_bss_info_changed_sta(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_bss_conf *info, u32 changed)
{
	struct mwl8k_priv *priv = hw->priv;
	u32 ap_legacy_rates;
	u8 ap_mcs_rates[16];
	int rc;

	if (mwl8k_fw_lock(hw))
		return;

	/*
	 * No need to capture a beacon if we're no longer associated.
	 */
	if ((changed & BSS_CHANGED_ASSOC) && !vif->bss_conf.assoc)
		priv->capture_beacon = false;

	/*
	 * Get the AP's legacy and MCS rates.
	 */
	if (vif->bss_conf.assoc) {
		struct ieee80211_sta *ap;

		rcu_read_lock();

		ap = ieee80211_find_sta(vif, vif->bss_conf.bssid);
		if (ap == NULL) {
			rcu_read_unlock();
			goto out;
		}

		if (hw->conf.channel->band == IEEE80211_BAND_2GHZ) {
			ap_legacy_rates = ap->supp_rates[IEEE80211_BAND_2GHZ];
		} else {
			ap_legacy_rates =
				ap->supp_rates[IEEE80211_BAND_5GHZ] << 5;
		}
		memcpy(ap_mcs_rates, ap->ht_cap.mcs.rx_mask, 16);

		rcu_read_unlock();
	}

	if ((changed & BSS_CHANGED_ASSOC) && vif->bss_conf.assoc) {
		rc = mwl8k_cmd_set_rate(hw, vif, ap_legacy_rates, ap_mcs_rates);
		if (rc)
			goto out;

		rc = mwl8k_cmd_use_fixed_rate_sta(hw);
		if (rc)
			goto out;
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		rc = mwl8k_set_radio_preamble(hw,
				vif->bss_conf.use_short_preamble);
		if (rc)
			goto out;
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		rc = mwl8k_cmd_set_slot(hw, vif->bss_conf.use_short_slot);
		if (rc)
			goto out;
	}

	if (vif->bss_conf.assoc &&
	    (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_ERP_CTS_PROT |
			BSS_CHANGED_HT))) {
		rc = mwl8k_cmd_set_aid(hw, vif, ap_legacy_rates);
		if (rc)
			goto out;
	}

	if (vif->bss_conf.assoc &&
	    (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_BEACON_INT))) {
		/*
		 * Finalize the join.  Tell rx handler to process
		 * next beacon from our BSSID.
		 */
		memcpy(priv->capture_bssid, vif->bss_conf.bssid, ETH_ALEN);
		priv->capture_beacon = true;
	}

out:
	mwl8k_fw_unlock(hw);
}

static void
mwl8k_bss_info_changed_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *info, u32 changed)
{
	int rc;

	if (mwl8k_fw_lock(hw))
		return;

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		rc = mwl8k_set_radio_preamble(hw,
				vif->bss_conf.use_short_preamble);
		if (rc)
			goto out;
	}

	if (changed & BSS_CHANGED_BASIC_RATES) {
		int idx;
		int rate;

		/*
		 * Use lowest supported basic rate for multicasts
		 * and management frames (such as probe responses --
		 * beacons will always go out at 1 Mb/s).
		 */
		idx = ffs(vif->bss_conf.basic_rates);
		if (idx)
			idx--;

		if (hw->conf.channel->band == IEEE80211_BAND_2GHZ)
			rate = mwl8k_rates_24[idx].hw_value;
		else
			rate = mwl8k_rates_50[idx].hw_value;

		mwl8k_cmd_use_fixed_rate_ap(hw, rate, rate);
	}

	if (changed & (BSS_CHANGED_BEACON_INT | BSS_CHANGED_BEACON)) {
		struct sk_buff *skb;

		skb = ieee80211_beacon_get(hw, vif);
		if (skb != NULL) {
			mwl8k_cmd_set_beacon(hw, vif, skb->data, skb->len);
			kfree_skb(skb);
		}
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED)
		mwl8k_cmd_bss_start(hw, vif, info->enable_beacon);

out:
	mwl8k_fw_unlock(hw);
}

static void
mwl8k_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       struct ieee80211_bss_conf *info, u32 changed)
{
	struct mwl8k_priv *priv = hw->priv;

	if (!priv->ap_fw)
		mwl8k_bss_info_changed_sta(hw, vif, info, changed);
	else
		mwl8k_bss_info_changed_ap(hw, vif, info, changed);
}

static u64 mwl8k_prepare_multicast(struct ieee80211_hw *hw,
				   struct netdev_hw_addr_list *mc_list)
{
	struct mwl8k_cmd_pkt *cmd;

	/*
	 * Synthesize and return a command packet that programs the
	 * hardware multicast address filter.  At this point we don't
	 * know whether FIF_ALLMULTI is being requested, but if it is,
	 * we'll end up throwing this packet away and creating a new
	 * one in mwl8k_configure_filter().
	 */
	cmd = __mwl8k_cmd_mac_multicast_adr(hw, 0, mc_list);

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
	if (!list_empty(&priv->vif_list)) {
		if (net_ratelimit())
			printk(KERN_INFO "%s: not enabling sniffer "
			       "mode because STA interface is active\n",
			       wiphy_name(hw->wiphy));
		return 0;
	}

	if (!priv->sniffer_enabled) {
		if (mwl8k_cmd_enable_sniffer(hw, 1))
			return 0;
		priv->sniffer_enabled = true;
	}

	*total_flags &=	FIF_PROMISC_IN_BSS | FIF_ALLMULTI |
			FIF_BCN_PRBRESP_PROMISC | FIF_CONTROL |
			FIF_OTHER_BSS;

	return 1;
}

static struct mwl8k_vif *mwl8k_first_vif(struct mwl8k_priv *priv)
{
	if (!list_empty(&priv->vif_list))
		return list_entry(priv->vif_list.next, struct mwl8k_vif, list);

	return NULL;
}

static void mwl8k_configure_filter(struct ieee80211_hw *hw,
				   unsigned int changed_flags,
				   unsigned int *total_flags,
				   u64 multicast)
{
	struct mwl8k_priv *priv = hw->priv;
	struct mwl8k_cmd_pkt *cmd = (void *)(unsigned long)multicast;

	/*
	 * AP firmware doesn't allow fine-grained control over
	 * the receive filter.
	 */
	if (priv->ap_fw) {
		*total_flags &= FIF_ALLMULTI | FIF_BCN_PRBRESP_PROMISC;
		kfree(cmd);
		return;
	}

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

	if (mwl8k_fw_lock(hw)) {
		kfree(cmd);
		return;
	}

	if (priv->sniffer_enabled) {
		mwl8k_cmd_enable_sniffer(hw, 0);
		priv->sniffer_enabled = false;
	}

	if (changed_flags & FIF_BCN_PRBRESP_PROMISC) {
		if (*total_flags & FIF_BCN_PRBRESP_PROMISC) {
			/*
			 * Disable the BSS filter.
			 */
			mwl8k_cmd_set_pre_scan(hw);
		} else {
			struct mwl8k_vif *mwl8k_vif;
			const u8 *bssid;

			/*
			 * Enable the BSS filter.
			 *
			 * If there is an active STA interface, use that
			 * interface's BSSID, otherwise use a dummy one
			 * (where the OUI part needs to be nonzero for
			 * the BSSID to be accepted by POST_SCAN).
			 */
			mwl8k_vif = mwl8k_first_vif(priv);
			if (mwl8k_vif != NULL)
				bssid = mwl8k_vif->vif->bss_conf.bssid;
			else
				bssid = "\x01\x00\x00\x00\x00\x00";

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
		cmd = __mwl8k_cmd_mac_multicast_adr(hw, 1, NULL);
	}

	if (cmd != NULL) {
		mwl8k_post_cmd(hw, cmd);
		kfree(cmd);
	}

	mwl8k_fw_unlock(hw);
}

static int mwl8k_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	return mwl8k_cmd_set_rts_threshold(hw, value);
}

static int mwl8k_sta_remove(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta)
{
	struct mwl8k_priv *priv = hw->priv;

	if (priv->ap_fw)
		return mwl8k_cmd_set_new_stn_del(hw, vif, sta->addr);
	else
		return mwl8k_cmd_update_stadb_del(hw, vif, sta->addr);
}

static int mwl8k_sta_add(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta)
{
	struct mwl8k_priv *priv = hw->priv;
	int ret;

	if (!priv->ap_fw) {
		ret = mwl8k_cmd_update_stadb_add(hw, vif, sta);
		if (ret >= 0) {
			MWL8K_STA(sta)->peer_id = ret;
			return 0;
		}

		return ret;
	}

	return mwl8k_cmd_set_new_stn_add(hw, vif, sta);
}

static int mwl8k_conf_tx(struct ieee80211_hw *hw, u16 queue,
			 const struct ieee80211_tx_queue_params *params)
{
	struct mwl8k_priv *priv = hw->priv;
	int rc;

	rc = mwl8k_fw_lock(hw);
	if (!rc) {
		if (!priv->wmm_enabled)
			rc = mwl8k_cmd_set_wmm_mode(hw, 1);

		if (!rc)
			rc = mwl8k_cmd_set_edca_params(hw, queue,
						       params->cw_min,
						       params->cw_max,
						       params->aifs,
						       params->txop);

		mwl8k_fw_unlock(hw);
	}

	return rc;
}

static int mwl8k_get_stats(struct ieee80211_hw *hw,
			   struct ieee80211_low_level_stats *stats)
{
	return mwl8k_cmd_get_stat(hw, stats);
}

static int
mwl8k_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   enum ieee80211_ampdu_mlme_action action,
		   struct ieee80211_sta *sta, u16 tid, u16 *ssn)
{
	switch (action) {
	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
		if (!(hw->flags & IEEE80211_HW_AMPDU_AGGREGATION))
			return -ENOTSUPP;
		return 0;
	default:
		return -ENOTSUPP;
	}
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
	.sta_add		= mwl8k_sta_add,
	.sta_remove		= mwl8k_sta_remove,
	.conf_tx		= mwl8k_conf_tx,
	.get_stats		= mwl8k_get_stats,
	.ampdu_action		= mwl8k_ampdu_action,
};

static void mwl8k_finalize_join_worker(struct work_struct *work)
{
	struct mwl8k_priv *priv =
		container_of(work, struct mwl8k_priv, finalize_join_worker);
	struct sk_buff *skb = priv->beacon_skb;
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	int len = skb->len - offsetof(struct ieee80211_mgmt, u.beacon.variable);
	const u8 *tim = cfg80211_find_ie(WLAN_EID_TIM,
					 mgmt->u.beacon.variable, len);
	int dtim_period = 1;

	if (tim && tim[1] >= 2)
		dtim_period = tim[3];

	mwl8k_cmd_finalize_join(priv->hw, skb->data, skb->len, dtim_period);

	dev_kfree_skb(skb);
	priv->beacon_skb = NULL;
}

enum {
	MWL8363 = 0,
	MWL8687,
	MWL8366,
};

static struct mwl8k_device_info mwl8k_info_tbl[] __devinitdata = {
	[MWL8363] = {
		.part_name	= "88w8363",
		.helper_image	= "mwl8k/helper_8363.fw",
		.fw_image	= "mwl8k/fmimage_8363.fw",
	},
	[MWL8687] = {
		.part_name	= "88w8687",
		.helper_image	= "mwl8k/helper_8687.fw",
		.fw_image	= "mwl8k/fmimage_8687.fw",
	},
	[MWL8366] = {
		.part_name	= "88w8366",
		.helper_image	= "mwl8k/helper_8366.fw",
		.fw_image	= "mwl8k/fmimage_8366.fw",
		.ap_rxd_ops	= &rxd_8366_ap_ops,
	},
};

MODULE_FIRMWARE("mwl8k/helper_8363.fw");
MODULE_FIRMWARE("mwl8k/fmimage_8363.fw");
MODULE_FIRMWARE("mwl8k/helper_8687.fw");
MODULE_FIRMWARE("mwl8k/fmimage_8687.fw");
MODULE_FIRMWARE("mwl8k/helper_8366.fw");
MODULE_FIRMWARE("mwl8k/fmimage_8366.fw");

static DEFINE_PCI_DEVICE_TABLE(mwl8k_pci_id_table) = {
	{ PCI_VDEVICE(MARVELL, 0x2a0a), .driver_data = MWL8363, },
	{ PCI_VDEVICE(MARVELL, 0x2a0c), .driver_data = MWL8363, },
	{ PCI_VDEVICE(MARVELL, 0x2a24), .driver_data = MWL8363, },
	{ PCI_VDEVICE(MARVELL, 0x2a2b), .driver_data = MWL8687, },
	{ PCI_VDEVICE(MARVELL, 0x2a30), .driver_data = MWL8687, },
	{ PCI_VDEVICE(MARVELL, 0x2a40), .driver_data = MWL8366, },
	{ PCI_VDEVICE(MARVELL, 0x2a43), .driver_data = MWL8366, },
	{ },
};
MODULE_DEVICE_TABLE(pci, mwl8k_pci_id_table);

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
		goto err_disable_device;
	}

	pci_set_master(pdev);


	hw = ieee80211_alloc_hw(sizeof(*priv), &mwl8k_ops);
	if (hw == NULL) {
		printk(KERN_ERR "%s: ieee80211 alloc failed\n", MWL8K_NAME);
		rc = -ENOMEM;
		goto err_free_reg;
	}

	SET_IEEE80211_DEV(hw, &pdev->dev);
	pci_set_drvdata(pdev, hw);

	priv = hw->priv;
	priv->hw = hw;
	priv->pdev = pdev;
	priv->device_info = &mwl8k_info_tbl[id->driver_data];


	priv->sram = pci_iomap(pdev, 0, 0x10000);
	if (priv->sram == NULL) {
		printk(KERN_ERR "%s: Cannot map device SRAM\n",
		       wiphy_name(hw->wiphy));
		goto err_iounmap;
	}

	/*
	 * If BAR0 is a 32 bit BAR, the register BAR will be BAR1.
	 * If BAR0 is a 64 bit BAR, the register BAR will be BAR2.
	 */
	priv->regs = pci_iomap(pdev, 1, 0x10000);
	if (priv->regs == NULL) {
		priv->regs = pci_iomap(pdev, 2, 0x10000);
		if (priv->regs == NULL) {
			printk(KERN_ERR "%s: Cannot map device registers\n",
			       wiphy_name(hw->wiphy));
			goto err_iounmap;
		}
	}


	/* Reset firmware and hardware */
	mwl8k_hw_reset(priv);

	/* Ask userland hotplug daemon for the device firmware */
	rc = mwl8k_request_firmware(priv);
	if (rc) {
		printk(KERN_ERR "%s: Firmware files not found\n",
		       wiphy_name(hw->wiphy));
		goto err_stop_firmware;
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


	if (priv->ap_fw) {
		priv->rxd_ops = priv->device_info->ap_rxd_ops;
		if (priv->rxd_ops == NULL) {
			printk(KERN_ERR "%s: Driver does not have AP "
			       "firmware image support for this hardware\n",
			       wiphy_name(hw->wiphy));
			goto err_stop_firmware;
		}
	} else {
		priv->rxd_ops = &rxd_sta_ops;
	}

	priv->sniffer_enabled = false;
	priv->wmm_enabled = false;
	priv->pending_tx_pkts = 0;


	/*
	 * Extra headroom is the size of the required DMA header
	 * minus the size of the smallest 802.11 frame (CTS frame).
	 */
	hw->extra_tx_headroom =
		sizeof(struct mwl8k_dma_data) - sizeof(struct ieee80211_cts);

	hw->channel_change_time = 10;

	hw->queues = MWL8K_TX_QUEUES;

	/* Set rssi values to dBm */
	hw->flags |= IEEE80211_HW_SIGNAL_DBM;
	hw->vif_data_size = sizeof(struct mwl8k_vif);
	hw->sta_data_size = sizeof(struct mwl8k_sta);

	priv->macids_used = 0;
	INIT_LIST_HEAD(&priv->vif_list);

	/* Set default radio state and preamble */
	priv->radio_on = 0;
	priv->radio_short_preamble = 0;

	/* Finalize join worker */
	INIT_WORK(&priv->finalize_join_worker, mwl8k_finalize_join_worker);

	/* TX reclaim and RX tasklets.  */
	tasklet_init(&priv->poll_tx_task, mwl8k_tx_poll, (unsigned long)hw);
	tasklet_disable(&priv->poll_tx_task);
	tasklet_init(&priv->poll_rx_task, mwl8k_rx_poll, (unsigned long)hw);
	tasklet_disable(&priv->poll_rx_task);

	/* Power management cookie */
	priv->cookie = pci_alloc_consistent(priv->pdev, 4, &priv->cookie_dma);
	if (priv->cookie == NULL)
		goto err_stop_firmware;

	rc = mwl8k_rxq_init(hw, 0);
	if (rc)
		goto err_free_cookie;
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
	iowrite32(MWL8K_A2H_INT_TX_DONE | MWL8K_A2H_INT_RX_READY,
		  priv->regs + MWL8K_HIU_A2H_INTERRUPT_CLEAR_SEL);
	iowrite32(0xffffffff, priv->regs + MWL8K_HIU_A2H_INTERRUPT_STATUS_MASK);

	rc = request_irq(priv->pdev->irq, mwl8k_interrupt,
			 IRQF_SHARED, MWL8K_NAME, hw);
	if (rc) {
		printk(KERN_ERR "%s: failed to register IRQ handler\n",
		       wiphy_name(hw->wiphy));
		goto err_free_queues;
	}

	/*
	 * Temporarily enable interrupts.  Initial firmware host
	 * commands use interrupts and avoid polling.  Disable
	 * interrupts when done.
	 */
	iowrite32(MWL8K_A2H_EVENTS, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);

	/* Get config data, mac addrs etc */
	if (priv->ap_fw) {
		rc = mwl8k_cmd_get_hw_spec_ap(hw);
		if (!rc)
			rc = mwl8k_cmd_set_hw_spec(hw);
	} else {
		rc = mwl8k_cmd_get_hw_spec_sta(hw);
	}
	if (rc) {
		printk(KERN_ERR "%s: Cannot initialise firmware\n",
		       wiphy_name(hw->wiphy));
		goto err_free_irq;
	}

	hw->wiphy->interface_modes = 0;
	if (priv->ap_macids_supported)
		hw->wiphy->interface_modes |= BIT(NL80211_IFTYPE_AP);
	if (priv->sta_macids_supported)
		hw->wiphy->interface_modes |= BIT(NL80211_IFTYPE_STATION);


	/* Turn radio off */
	rc = mwl8k_cmd_radio_disable(hw);
	if (rc) {
		printk(KERN_ERR "%s: Cannot disable\n", wiphy_name(hw->wiphy));
		goto err_free_irq;
	}

	/* Clear MAC address */
	rc = mwl8k_cmd_set_mac_addr(hw, NULL, "\x00\x00\x00\x00\x00\x00");
	if (rc) {
		printk(KERN_ERR "%s: Cannot clear MAC address\n",
		       wiphy_name(hw->wiphy));
		goto err_free_irq;
	}

	/* Disable interrupts */
	iowrite32(0, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);
	free_irq(priv->pdev->irq, hw);

	rc = ieee80211_register_hw(hw);
	if (rc) {
		printk(KERN_ERR "%s: Cannot register device\n",
		       wiphy_name(hw->wiphy));
		goto err_free_queues;
	}

	printk(KERN_INFO "%s: %s v%d, %pM, %s firmware %u.%u.%u.%u\n",
	       wiphy_name(hw->wiphy), priv->device_info->part_name,
	       priv->hw_rev, hw->wiphy->perm_addr,
	       priv->ap_fw ? "AP" : "STA",
	       (priv->fw_rev >> 24) & 0xff, (priv->fw_rev >> 16) & 0xff,
	       (priv->fw_rev >> 8) & 0xff, priv->fw_rev & 0xff);

	return 0;

err_free_irq:
	iowrite32(0, priv->regs + MWL8K_HIU_A2H_INTERRUPT_MASK);
	free_irq(priv->pdev->irq, hw);

err_free_queues:
	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		mwl8k_txq_deinit(hw, i);
	mwl8k_rxq_deinit(hw, 0);

err_free_cookie:
	if (priv->cookie != NULL)
		pci_free_consistent(priv->pdev, 4,
				priv->cookie, priv->cookie_dma);

err_stop_firmware:
	mwl8k_hw_reset(priv);
	mwl8k_release_firmware(priv);

err_iounmap:
	if (priv->regs != NULL)
		pci_iounmap(pdev, priv->regs);

	if (priv->sram != NULL)
		pci_iounmap(pdev, priv->sram);

	pci_set_drvdata(pdev, NULL);
	ieee80211_free_hw(hw);

err_free_reg:
	pci_release_regions(pdev);

err_disable_device:
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

	/* Remove TX reclaim and RX tasklets.  */
	tasklet_kill(&priv->poll_tx_task);
	tasklet_kill(&priv->poll_rx_task);

	/* Stop hardware */
	mwl8k_hw_reset(priv);

	/* Return all skbs to mac80211 */
	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		mwl8k_txq_reclaim(hw, i, INT_MAX, 1);

	for (i = 0; i < MWL8K_TX_QUEUES; i++)
		mwl8k_txq_deinit(hw, i);

	mwl8k_rxq_deinit(hw, 0);

	pci_free_consistent(priv->pdev, 4, priv->cookie, priv->cookie_dma);

	pci_iounmap(pdev, priv->regs);
	pci_iounmap(pdev, priv->sram);
	pci_set_drvdata(pdev, NULL);
	ieee80211_free_hw(hw);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver mwl8k_driver = {
	.name		= MWL8K_NAME,
	.id_table	= mwl8k_pci_id_table,
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
