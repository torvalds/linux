#ifndef RTL8180_H
#define RTL8180_H

#include "rtl818x.h"

#define MAX_RX_SIZE IEEE80211_MAX_RTS_THRESHOLD

#define RF_PARAM_ANALOGPHY	(1 << 0)
#define RF_PARAM_ANTBDEFAULT	(1 << 1)
#define RF_PARAM_CARRIERSENSE1	(1 << 2)
#define RF_PARAM_CARRIERSENSE2	(1 << 3)

#define BB_ANTATTEN_CHAN14	0x0C
#define BB_ANTENNA_B 		0x40

#define BB_HOST_BANG 		(1 << 30)
#define BB_HOST_BANG_EN 	(1 << 2)
#define BB_HOST_BANG_CLK 	(1 << 1)
#define BB_HOST_BANG_DATA	1

#define ANAPARAM_TXDACOFF_SHIFT	27
#define ANAPARAM_PWR0_SHIFT	28
#define ANAPARAM_PWR0_MASK 	(0x07 << ANAPARAM_PWR0_SHIFT)
#define ANAPARAM_PWR1_SHIFT	20
#define ANAPARAM_PWR1_MASK	(0x7F << ANAPARAM_PWR1_SHIFT)

struct rtl8180_tx_desc {
	__le32 flags;
	__le16 rts_duration;
	__le16 plcp_len;
	__le32 tx_buf;
	__le32 frame_len;
	__le32 next_tx_desc;
	u8 cw;
	u8 retry_limit;
	u8 agc;
	u8 flags2;
	u32 reserved[2];
} __attribute__ ((packed));

struct rtl8180_rx_desc {
	__le32 flags;
	__le32 flags2;
	union {
		__le32 rx_buf;
		__le64 tsft;
	};
} __attribute__ ((packed));

struct rtl8180_tx_ring {
	struct rtl8180_tx_desc *desc;
	dma_addr_t dma;
	unsigned int idx;
	unsigned int entries;
	struct sk_buff_head queue;
};

struct rtl8180_priv {
	/* common between rtl818x drivers */
	struct rtl818x_csr __iomem *map;
	const struct rtl818x_rf_ops *rf;
	struct ieee80211_vif *vif;

	/* rtl8180 driver specific */
	spinlock_t lock;
	struct rtl8180_rx_desc *rx_ring;
	dma_addr_t rx_ring_dma;
	unsigned int rx_idx;
	struct sk_buff *rx_buf[32];
	struct rtl8180_tx_ring tx_ring[4];
	struct ieee80211_channel channels[14];
	struct ieee80211_rate rates[12];
	struct ieee80211_supported_band band;
	struct pci_dev *pdev;
	u32 rx_conf;

	int r8185;
	u32 anaparam;
	u16 rfparam;
	u8 csthreshold;
};

void rtl8180_write_phy(struct ieee80211_hw *dev, u8 addr, u32 data);
void rtl8180_set_anaparam(struct rtl8180_priv *priv, u32 anaparam);

static inline u8 rtl818x_ioread8(struct rtl8180_priv *priv, u8 __iomem *addr)
{
	return ioread8(addr);
}

static inline u16 rtl818x_ioread16(struct rtl8180_priv *priv, __le16 __iomem *addr)
{
	return ioread16(addr);
}

static inline u32 rtl818x_ioread32(struct rtl8180_priv *priv, __le32 __iomem *addr)
{
	return ioread32(addr);
}

static inline void rtl818x_iowrite8(struct rtl8180_priv *priv,
				    u8 __iomem *addr, u8 val)
{
	iowrite8(val, addr);
}

static inline void rtl818x_iowrite16(struct rtl8180_priv *priv,
				     __le16 __iomem *addr, u16 val)
{
	iowrite16(val, addr);
}

static inline void rtl818x_iowrite32(struct rtl8180_priv *priv,
				     __le32 __iomem *addr, u32 val)
{
	iowrite32(val, addr);
}

#endif /* RTL8180_H */
