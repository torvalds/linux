/*
 * CAN bus driver for Bosch C_CAN controller
 *
 * Copyright (C) 2010 ST Microelectronics
 * Bhupesh Sharma <bhupesh.sharma@st.com>
 *
 * Borrowed heavily from the C_CAN driver originally written by:
 * Copyright (C) 2007
 * - Sascha Hauer, Marc Kleine-Budde, Pengutronix <s.hauer@pengutronix.de>
 * - Simon Kallweit, intefo AG <simon.kallweit@intefo.ch>
 *
 * Bosch C_CAN controller is compliant to CAN protocol version 2.0 part A and B.
 * Bosch C_CAN user manual can be obtained from:
 * http://www.semiconductors.bosch.de/media/en/pdf/ipmodules_1/c_can/
 * users_manual_c_can.pdf
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef C_CAN_H
#define C_CAN_H

enum reg {
	C_CAN_CTRL_REG = 0,
	C_CAN_CTRL_EX_REG,
	C_CAN_STS_REG,
	C_CAN_ERR_CNT_REG,
	C_CAN_BTR_REG,
	C_CAN_INT_REG,
	C_CAN_TEST_REG,
	C_CAN_BRPEXT_REG,
	C_CAN_IF1_COMREQ_REG,
	C_CAN_IF1_COMMSK_REG,
	C_CAN_IF1_MASK1_REG,
	C_CAN_IF1_MASK2_REG,
	C_CAN_IF1_ARB1_REG,
	C_CAN_IF1_ARB2_REG,
	C_CAN_IF1_MSGCTRL_REG,
	C_CAN_IF1_DATA1_REG,
	C_CAN_IF1_DATA2_REG,
	C_CAN_IF1_DATA3_REG,
	C_CAN_IF1_DATA4_REG,
	C_CAN_IF2_COMREQ_REG,
	C_CAN_IF2_COMMSK_REG,
	C_CAN_IF2_MASK1_REG,
	C_CAN_IF2_MASK2_REG,
	C_CAN_IF2_ARB1_REG,
	C_CAN_IF2_ARB2_REG,
	C_CAN_IF2_MSGCTRL_REG,
	C_CAN_IF2_DATA1_REG,
	C_CAN_IF2_DATA2_REG,
	C_CAN_IF2_DATA3_REG,
	C_CAN_IF2_DATA4_REG,
	C_CAN_TXRQST1_REG,
	C_CAN_TXRQST2_REG,
	C_CAN_NEWDAT1_REG,
	C_CAN_NEWDAT2_REG,
	C_CAN_INTPND1_REG,
	C_CAN_INTPND2_REG,
	C_CAN_INTPND3_REG,
	C_CAN_MSGVAL1_REG,
	C_CAN_MSGVAL2_REG,
	C_CAN_FUNCTION_REG,
};

static const u16 __maybe_unused reg_map_c_can[] = {
	[C_CAN_CTRL_REG]	= 0x00,
	[C_CAN_STS_REG]		= 0x02,
	[C_CAN_ERR_CNT_REG]	= 0x04,
	[C_CAN_BTR_REG]		= 0x06,
	[C_CAN_INT_REG]		= 0x08,
	[C_CAN_TEST_REG]	= 0x0A,
	[C_CAN_BRPEXT_REG]	= 0x0C,
	[C_CAN_IF1_COMREQ_REG]	= 0x10,
	[C_CAN_IF1_COMMSK_REG]	= 0x12,
	[C_CAN_IF1_MASK1_REG]	= 0x14,
	[C_CAN_IF1_MASK2_REG]	= 0x16,
	[C_CAN_IF1_ARB1_REG]	= 0x18,
	[C_CAN_IF1_ARB2_REG]	= 0x1A,
	[C_CAN_IF1_MSGCTRL_REG]	= 0x1C,
	[C_CAN_IF1_DATA1_REG]	= 0x1E,
	[C_CAN_IF1_DATA2_REG]	= 0x20,
	[C_CAN_IF1_DATA3_REG]	= 0x22,
	[C_CAN_IF1_DATA4_REG]	= 0x24,
	[C_CAN_IF2_COMREQ_REG]	= 0x40,
	[C_CAN_IF2_COMMSK_REG]	= 0x42,
	[C_CAN_IF2_MASK1_REG]	= 0x44,
	[C_CAN_IF2_MASK2_REG]	= 0x46,
	[C_CAN_IF2_ARB1_REG]	= 0x48,
	[C_CAN_IF2_ARB2_REG]	= 0x4A,
	[C_CAN_IF2_MSGCTRL_REG]	= 0x4C,
	[C_CAN_IF2_DATA1_REG]	= 0x4E,
	[C_CAN_IF2_DATA2_REG]	= 0x50,
	[C_CAN_IF2_DATA3_REG]	= 0x52,
	[C_CAN_IF2_DATA4_REG]	= 0x54,
	[C_CAN_TXRQST1_REG]	= 0x80,
	[C_CAN_TXRQST2_REG]	= 0x82,
	[C_CAN_NEWDAT1_REG]	= 0x90,
	[C_CAN_NEWDAT2_REG]	= 0x92,
	[C_CAN_INTPND1_REG]	= 0xA0,
	[C_CAN_INTPND2_REG]	= 0xA2,
	[C_CAN_MSGVAL1_REG]	= 0xB0,
	[C_CAN_MSGVAL2_REG]	= 0xB2,
};

static const u16 __maybe_unused reg_map_d_can[] = {
	[C_CAN_CTRL_REG]	= 0x00,
	[C_CAN_CTRL_EX_REG]	= 0x02,
	[C_CAN_STS_REG]		= 0x04,
	[C_CAN_ERR_CNT_REG]	= 0x08,
	[C_CAN_BTR_REG]		= 0x0C,
	[C_CAN_BRPEXT_REG]	= 0x0E,
	[C_CAN_INT_REG]		= 0x10,
	[C_CAN_TEST_REG]	= 0x14,
	[C_CAN_FUNCTION_REG]	= 0x18,
	[C_CAN_TXRQST1_REG]	= 0x88,
	[C_CAN_TXRQST2_REG]	= 0x8A,
	[C_CAN_NEWDAT1_REG]	= 0x9C,
	[C_CAN_NEWDAT2_REG]	= 0x9E,
	[C_CAN_INTPND1_REG]	= 0xB0,
	[C_CAN_INTPND2_REG]	= 0xB2,
	[C_CAN_INTPND3_REG]	= 0xB4,
	[C_CAN_MSGVAL1_REG]	= 0xC4,
	[C_CAN_MSGVAL2_REG]	= 0xC6,
	[C_CAN_IF1_COMREQ_REG]	= 0x100,
	[C_CAN_IF1_COMMSK_REG]	= 0x102,
	[C_CAN_IF1_MASK1_REG]	= 0x104,
	[C_CAN_IF1_MASK2_REG]	= 0x106,
	[C_CAN_IF1_ARB1_REG]	= 0x108,
	[C_CAN_IF1_ARB2_REG]	= 0x10A,
	[C_CAN_IF1_MSGCTRL_REG]	= 0x10C,
	[C_CAN_IF1_DATA1_REG]	= 0x110,
	[C_CAN_IF1_DATA2_REG]	= 0x112,
	[C_CAN_IF1_DATA3_REG]	= 0x114,
	[C_CAN_IF1_DATA4_REG]	= 0x116,
	[C_CAN_IF2_COMREQ_REG]	= 0x120,
	[C_CAN_IF2_COMMSK_REG]	= 0x122,
	[C_CAN_IF2_MASK1_REG]	= 0x124,
	[C_CAN_IF2_MASK2_REG]	= 0x126,
	[C_CAN_IF2_ARB1_REG]	= 0x128,
	[C_CAN_IF2_ARB2_REG]	= 0x12A,
	[C_CAN_IF2_MSGCTRL_REG]	= 0x12C,
	[C_CAN_IF2_DATA1_REG]	= 0x130,
	[C_CAN_IF2_DATA2_REG]	= 0x132,
	[C_CAN_IF2_DATA3_REG]	= 0x134,
	[C_CAN_IF2_DATA4_REG]	= 0x136,
};

enum c_can_dev_id {
	BOSCH_C_CAN,
	BOSCH_D_CAN,
};

struct raminit_bits {
	u8 start;
	u8 done;
};

struct c_can_driver_data {
	enum c_can_dev_id id;
	unsigned int msg_obj_num;

	/* RAMINIT register description. Optional. */
	const struct raminit_bits *raminit_bits; /* Array of START/DONE bit positions */
	u8 raminit_num;		/* Number of CAN instances on the SoC */
	bool raminit_pulse;	/* If set, sets and clears START bit (pulse) */
};

/* Out of band RAMINIT register access via syscon regmap */
struct c_can_raminit {
	struct regmap *syscon;	/* for raminit ctrl. reg. access */
	unsigned int reg;	/* register index within syscon */
	struct raminit_bits bits;
	bool needs_pulse;
};

/* c_can tx ring structure */
struct c_can_tx_ring {
	unsigned int head;
	unsigned int tail;
	unsigned int obj_num;
};

/* c_can private data structure */
struct c_can_priv {
	struct can_priv can;	/* must be the first member */
	struct napi_struct napi;
	struct net_device *dev;
	struct device *device;
	unsigned int msg_obj_num;
	unsigned int msg_obj_rx_num;
	unsigned int msg_obj_tx_num;
	unsigned int msg_obj_rx_first;
	unsigned int msg_obj_rx_last;
	unsigned int msg_obj_tx_first;
	unsigned int msg_obj_tx_last;
	u32 msg_obj_rx_mask;
	atomic_t sie_pending;
	unsigned long tx_dir;
	int last_status;
	struct c_can_tx_ring tx;
	u16 (*read_reg)(const struct c_can_priv *priv, enum reg index);
	void (*write_reg)(const struct c_can_priv *priv, enum reg index, u16 val);
	u32 (*read_reg32)(const struct c_can_priv *priv, enum reg index);
	void (*write_reg32)(const struct c_can_priv *priv, enum reg index, u32 val);
	void __iomem *base;
	const u16 *regs;
	enum c_can_dev_id type;
	struct c_can_raminit raminit_sys;	/* RAMINIT via syscon regmap */
	void (*raminit)(const struct c_can_priv *priv, bool enable);
	u32 comm_rcv_high;
	u32 dlc[];
};

struct net_device *alloc_c_can_dev(int msg_obj_num);
void free_c_can_dev(struct net_device *dev);
int register_c_can_dev(struct net_device *dev);
void unregister_c_can_dev(struct net_device *dev);

#ifdef CONFIG_PM
int c_can_power_up(struct net_device *dev);
int c_can_power_down(struct net_device *dev);
#endif

void c_can_set_ethtool_ops(struct net_device *dev);

static inline u8 c_can_get_tx_head(const struct c_can_tx_ring *ring)
{
	return ring->head & (ring->obj_num - 1);
}

static inline u8 c_can_get_tx_tail(const struct c_can_tx_ring *ring)
{
	return ring->tail & (ring->obj_num - 1);
}

static inline u8 c_can_get_tx_free(const struct c_can_priv *priv,
				   const struct c_can_tx_ring *ring)
{
	u8 head = c_can_get_tx_head(ring);
	u8 tail = c_can_get_tx_tail(ring);

	if (priv->type == BOSCH_D_CAN)
		return ring->obj_num - (ring->head - ring->tail);

	/* This is not a FIFO. C/D_CAN sends out the buffers
	 * prioritized. The lowest buffer number wins.
	 */
	if (head < tail)
		return 0;

	return ring->obj_num - head;
}

#endif /* C_CAN_H */
