/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef __HBG_HW_H
#define __HBG_HW_H

#include <linux/bitfield.h>
#include <linux/io-64-nonatomic-lo-hi.h>

static inline u32 hbg_reg_read(struct hbg_priv *priv, u32 addr)
{
	return readl(priv->io_base + addr);
}

static inline void hbg_reg_write(struct hbg_priv *priv, u32 addr, u32 value)
{
	writel(value, priv->io_base + addr);
}

static inline u64 hbg_reg_read64(struct hbg_priv *priv, u32 addr)
{
	return lo_hi_readq(priv->io_base + addr);
}

static inline void hbg_reg_write64(struct hbg_priv *priv, u32 addr, u64 value)
{
	lo_hi_writeq(value, priv->io_base + addr);
}

#define hbg_reg_read_field(priv, addr, mask) \
		FIELD_GET(mask, hbg_reg_read(priv, addr))

#define hbg_field_modify(reg_value, mask, value) ({	\
		(reg_value) &= ~(mask);			\
		(reg_value) |= FIELD_PREP(mask, value); })

#define hbg_reg_write_field(priv, addr, mask, val) ({		\
		typeof(priv) _priv = (priv);			\
		typeof(addr) _addr = (addr);			\
		u32 _value = hbg_reg_read(_priv, _addr);	\
		hbg_field_modify(_value, mask, val);		\
		hbg_reg_write(_priv, _addr, _value); })

int hbg_hw_event_notify(struct hbg_priv *priv,
			enum hbg_hw_event_type event_type);
int hbg_hw_init(struct hbg_priv *priv);
void hbg_hw_adjust_link(struct hbg_priv *priv, u32 speed, u32 duplex);
u32 hbg_hw_get_irq_status(struct hbg_priv *priv);
void hbg_hw_irq_clear(struct hbg_priv *priv, u32 mask);
bool hbg_hw_irq_is_enabled(struct hbg_priv *priv, u32 mask);
void hbg_hw_irq_enable(struct hbg_priv *priv, u32 mask, bool enable);
void hbg_hw_set_mtu(struct hbg_priv *priv, u16 mtu);
void hbg_hw_mac_enable(struct hbg_priv *priv, u32 enable);
void hbg_hw_set_uc_addr(struct hbg_priv *priv, u64 mac_addr);
u32 hbg_hw_get_fifo_used_num(struct hbg_priv *priv, enum hbg_dir dir);
void hbg_hw_set_tx_desc(struct hbg_priv *priv, struct hbg_tx_desc *tx_desc);
void hbg_hw_fill_buffer(struct hbg_priv *priv, u32 buffer_dma_addr);

#endif
