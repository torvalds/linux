/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2021 StarFive, Inc <huan.feng@starfivetech.com>
 */


#ifndef __JH7110_PL080_H__
#define __JH7110_PL080_H__

#define PL080_INT_STATUS            (0x00)
#define PL080_TC_STATUS             (0x04)
#define PL080_TC_CLEAR              (0x08)
#define PL080_ERR_STATUS            (0x0C)
#define PL080_ERR_CLEAR             (0x10)
#define PL080_RAW_TC_STATUS         (0x14)
#define PL080_RAW_ERR_STATUS        (0x18)
#define PL080_EN_CHAN               (0x1c)
#define PL080_SOFT_BREQ             (0x20)
#define PL080_SOFT_SREQ             (0x24)
#define PL080_SOFT_LBREQ            (0x28)
#define PL080_SOFT_LSREQ            (0x2C)
#define PL080_CONFIG                (0x30)
#define PL080_CONFIG_M2_BE          BIT(2)
#define PL080_CONFIG_M1_BE          BIT(1)
#define PL080_CONFIG_ENABLE         BIT(0)
#define PL080_SYNC                  (0x34)

/* Per channel configuration registers */
#define PL080_Cx_BASE(x)            ((0x100 + (x * 0x20)))
#define PL080_CH_SRC_ADDR           (0x00)
#define PL080_CH_DST_ADDR           (0x04)
#define PL080_CH_LLI                (0x08)
#define PL080_CH_CONTROL            (0x0C)
#define PL080_CH_CONFIG             (0x10)
#define PL080S_CH_CONTROL2          (0x10)
#define PL080S_CH_CONFIG            (0x14)

#define PL080_LLI_ADDR_SHIFT                (2)
#define PL080_CONTROL_PROT_SHIFT            (28)
#define PL080_CONTROL_DWIDTH_SHIFT          (21)
#define PL080_CONTROL_SWIDTH_SHIFT          (18)
#define PL080_CONTROL_DB_SIZE_SHIFT         (15)
#define PL080_CONTROL_SB_SIZE_SHIFT         (12)
#define PL080_CONTROL_TRANSFER_SIZE_SHIFT   (0)

#define PL080_LLI_LM_AHB2                   BIT(0)
#define PL080_CONTROL_TC_IRQ_EN             BIT(31)
#define PL080_CONTROL_PROT_CACHE            BIT(30)
#define PL080_CONTROL_PROT_BUFF             BIT(29)
#define PL080_CONTROL_PROT_SYS              BIT(28)
#define PL080_CONTROL_DST_INCR              BIT(27)
#define PL080_CONTROL_SRC_INCR              BIT(26)
#define PL080_CONTROL_DST_AHB2              BIT(25)
#define PL080_CONTROL_SRC_AHB2              BIT(24)

#define PL080_CONTROL_TRANSFER_SIZE_MASK    (0xfff<<0)
#define PL080N_CONFIG_ITPROT                BIT(20)
#define PL080N_CONFIG_SECPROT               BIT(19)
#define PL080_CONFIG_HALT                   BIT(18)
#define PL080_CONFIG_ACTIVE                 BIT(17)  /* RO */
#define PL080_CONFIG_LOCK                   BIT(16)
#define PL080_CONFIG_TC_IRQ_MASK            BIT(15)
#define PL080_CONFIG_ERR_IRQ_MASK           BIT(14)
#define PL080_CONFIG_ENABLE                 BIT(0)

#define PL080_CONFIG_FLOW_CONTROL_SHIFT     (11)
#define PL080_CONFIG_DST_SEL_SHIFT          (6)
#define PL080_CONFIG_SRC_SEL_SHIFT          (1)

#define PL080_CONFIG_FLOW_CONTROL_MASK      GENMASK(13, 11)
#define PL080_CONFIG_DST_SEL_MASK           GENMASK(9, 6)
#define PL080_CONFIG_SRC_SEL_MASK           GENMASK(4, 1)


#define PL080_CHANNELS_NUM      (8)
#define PL080_SIGNAL_NUM        (16)

/* Bitmasks for selecting AHB ports for DMA transfers */
enum jh7110_pl08x_ahb_master {
	PL08X_AHB1 = (0 << 0),
	PL08X_AHB2 = (1 << 0)
};

enum jh7110_pl08x_burst_size {
	PL08X_BURST_SZ_1,
	PL08X_BURST_SZ_4,
	PL08X_BURST_SZ_8,
	PL08X_BURST_SZ_16,
	PL08X_BURST_SZ_32,
	PL08X_BURST_SZ_64,
	PL08X_BURST_SZ_128,
	PL08X_BURST_SZ_256,
};

enum jh7110_pl08x_bus_width {
	PL08X_BUS_WIDTH_8_BITS,
	PL08X_BUS_WIDTH_16_BITS,
	PL08X_BUS_WIDTH_32_BITS,
};

enum jh7110_pl08x_flow_control {
	PL080_FLOW_MEM2MEM,
	PL080_FLOW_MEM2PER,
	PL080_FLOW_PER2MEM,
	PL080_FLOW_SRC2DST,
	PL080_FLOW_SRC2DST_DST,
	PL080_FLOW_MEM2PER_PER,
	PL080_FLOW_PER2MEM_PER,
	PL080_FLOW_SRC2DST_SRC
};

enum jh7110_pl08x_increment {
	PL08X_INCREMENT_FIX,
	PL08X_INCREMENT,
};

struct jh7110_pl080_chan_config {
	u8  src_peri;
	u8  dst_peri;
	u32 src_addr;
	u32 dst_addr;
	u32 xfer_size;
	u32 data_scattered;
	u32 src_distance;
	u32 dst_distance;
	enum jh7110_pl08x_increment si;
	enum jh7110_pl08x_increment di;
	enum jh7110_pl08x_ahb_master src_ahb;
	enum jh7110_pl08x_ahb_master dst_ahb;
	enum jh7110_pl08x_bus_width src_width;
	enum jh7110_pl08x_bus_width dst_width;
	enum jh7110_pl08x_burst_size src_bsize;
	enum jh7110_pl08x_burst_size dst_bsize;
	enum jh7110_pl08x_flow_control flow;
};

/* DMA linked list chain structure */
struct jh7110_pl080_lli {
	u32 src_addr;
	u32 dst_addr;
	u32 next_lli;
	u32 control0;
};

struct jh7110_pl080_lli_build_data {
	u32 src_addr;
	u32 dst_addr;
	struct jh7110_pl080_lli *llis;
	u32 llis_phy_addr;
	u32 tsize;
	u32 remainder;
};

struct jh7110_pl08x_phy_chan {
	unsigned int id;
	void  *base;
	void  *reg_config;
	void  *reg_control;
	void  *reg_src;
	void  *reg_dst;
	void  *reg_lli;
};

struct jh7110_pl08x_device {
	struct jh7110_pl08x_phy_chan phy_chans[PL080_CHANNELS_NUM];
};
#endif
