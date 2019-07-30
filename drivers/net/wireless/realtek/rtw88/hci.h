/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef	__RTW_HCI_H__
#define __RTW_HCI_H__

/* ops for PCI, USB and SDIO */
struct rtw_hci_ops {
	int (*tx)(struct rtw_dev *rtwdev,
		  struct rtw_tx_pkt_info *pkt_info,
		  struct sk_buff *skb);
	int (*setup)(struct rtw_dev *rtwdev);
	int (*start)(struct rtw_dev *rtwdev);
	void (*stop)(struct rtw_dev *rtwdev);

	int (*write_data_rsvd_page)(struct rtw_dev *rtwdev, u8 *buf, u32 size);
	int (*write_data_h2c)(struct rtw_dev *rtwdev, u8 *buf, u32 size);

	u8 (*read8)(struct rtw_dev *rtwdev, u32 addr);
	u16 (*read16)(struct rtw_dev *rtwdev, u32 addr);
	u32 (*read32)(struct rtw_dev *rtwdev, u32 addr);
	void (*write8)(struct rtw_dev *rtwdev, u32 addr, u8 val);
	void (*write16)(struct rtw_dev *rtwdev, u32 addr, u16 val);
	void (*write32)(struct rtw_dev *rtwdev, u32 addr, u32 val);
};

static inline int rtw_hci_tx(struct rtw_dev *rtwdev,
			     struct rtw_tx_pkt_info *pkt_info,
			     struct sk_buff *skb)
{
	return rtwdev->hci.ops->tx(rtwdev, pkt_info, skb);
}

static inline int rtw_hci_setup(struct rtw_dev *rtwdev)
{
	return rtwdev->hci.ops->setup(rtwdev);
}

static inline int rtw_hci_start(struct rtw_dev *rtwdev)
{
	return rtwdev->hci.ops->start(rtwdev);
}

static inline void rtw_hci_stop(struct rtw_dev *rtwdev)
{
	rtwdev->hci.ops->stop(rtwdev);
}

static inline int
rtw_hci_write_data_rsvd_page(struct rtw_dev *rtwdev, u8 *buf, u32 size)
{
	return rtwdev->hci.ops->write_data_rsvd_page(rtwdev, buf, size);
}

static inline int
rtw_hci_write_data_h2c(struct rtw_dev *rtwdev, u8 *buf, u32 size)
{
	return rtwdev->hci.ops->write_data_h2c(rtwdev, buf, size);
}

static inline u8 rtw_read8(struct rtw_dev *rtwdev, u32 addr)
{
	return rtwdev->hci.ops->read8(rtwdev, addr);
}

static inline u16 rtw_read16(struct rtw_dev *rtwdev, u32 addr)
{
	return rtwdev->hci.ops->read16(rtwdev, addr);
}

static inline u32 rtw_read32(struct rtw_dev *rtwdev, u32 addr)
{
	return rtwdev->hci.ops->read32(rtwdev, addr);
}

static inline void rtw_write8(struct rtw_dev *rtwdev, u32 addr, u8 val)
{
	rtwdev->hci.ops->write8(rtwdev, addr, val);
}

static inline void rtw_write16(struct rtw_dev *rtwdev, u32 addr, u16 val)
{
	rtwdev->hci.ops->write16(rtwdev, addr, val);
}

static inline void rtw_write32(struct rtw_dev *rtwdev, u32 addr, u32 val)
{
	rtwdev->hci.ops->write32(rtwdev, addr, val);
}

static inline void rtw_write8_set(struct rtw_dev *rtwdev, u32 addr, u8 bit)
{
	u8 val;

	val = rtw_read8(rtwdev, addr);
	rtw_write8(rtwdev, addr, val | bit);
}

static inline void rtw_writ16_set(struct rtw_dev *rtwdev, u32 addr, u16 bit)
{
	u16 val;

	val = rtw_read16(rtwdev, addr);
	rtw_write16(rtwdev, addr, val | bit);
}

static inline void rtw_write32_set(struct rtw_dev *rtwdev, u32 addr, u32 bit)
{
	u32 val;

	val = rtw_read32(rtwdev, addr);
	rtw_write32(rtwdev, addr, val | bit);
}

static inline void rtw_write8_clr(struct rtw_dev *rtwdev, u32 addr, u8 bit)
{
	u8 val;

	val = rtw_read8(rtwdev, addr);
	rtw_write8(rtwdev, addr, val & ~bit);
}

static inline void rtw_write16_clr(struct rtw_dev *rtwdev, u32 addr, u16 bit)
{
	u16 val;

	val = rtw_read16(rtwdev, addr);
	rtw_write16(rtwdev, addr, val & ~bit);
}

static inline void rtw_write32_clr(struct rtw_dev *rtwdev, u32 addr, u32 bit)
{
	u32 val;

	val = rtw_read32(rtwdev, addr);
	rtw_write32(rtwdev, addr, val & ~bit);
}

static inline u32
rtw_read_rf(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
	    u32 addr, u32 mask)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&rtwdev->rf_lock, flags);
	val = rtwdev->chip->ops->read_rf(rtwdev, rf_path, addr, mask);
	spin_unlock_irqrestore(&rtwdev->rf_lock, flags);

	return val;
}

static inline void
rtw_write_rf(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
	     u32 addr, u32 mask, u32 data)
{
	unsigned long flags;

	spin_lock_irqsave(&rtwdev->rf_lock, flags);
	rtwdev->chip->ops->write_rf(rtwdev, rf_path, addr, mask, data);
	spin_unlock_irqrestore(&rtwdev->rf_lock, flags);
}

static inline u32
rtw_read32_mask(struct rtw_dev *rtwdev, u32 addr, u32 mask)
{
	u32 shift = __ffs(mask);
	u32 orig;
	u32 ret;

	orig = rtw_read32(rtwdev, addr);
	ret = (orig & mask) >> shift;

	return ret;
}

static inline void
rtw_write32_mask(struct rtw_dev *rtwdev, u32 addr, u32 mask, u32 data)
{
	u32 shift = __ffs(mask);
	u32 orig;
	u32 set;

	WARN(addr & 0x3, "should be 4-byte aligned, addr = 0x%08x\n", addr);

	orig = rtw_read32(rtwdev, addr);
	set = (orig & ~mask) | ((data << shift) & mask);
	rtw_write32(rtwdev, addr, set);
}

static inline void
rtw_write8_mask(struct rtw_dev *rtwdev, u32 addr, u32 mask, u8 data)
{
	u32 shift;
	u8 orig, set;

	mask &= 0xff;
	shift = __ffs(mask);

	orig = rtw_read8(rtwdev, addr);
	set = (orig & ~mask) | ((data << shift) & mask);
	rtw_write8(rtwdev, addr, set);
}

static inline enum rtw_hci_type rtw_hci_type(struct rtw_dev *rtwdev)
{
	return rtwdev->hci.type;
}

#endif
