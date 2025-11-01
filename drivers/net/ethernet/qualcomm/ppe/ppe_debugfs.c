// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

/* PPE debugfs routines for display of PPE counters useful for debug. */

#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>

#include "ppe.h"
#include "ppe_config.h"
#include "ppe_debugfs.h"
#include "ppe_regs.h"

#define PPE_PKT_CNT_TBL_SIZE				3
#define PPE_DROP_PKT_CNT_TBL_SIZE			5

#define PPE_W0_PKT_CNT					GENMASK(31, 0)
#define PPE_W2_DROP_PKT_CNT_LOW				GENMASK(31, 8)
#define PPE_W3_DROP_PKT_CNT_HIGH			GENMASK(7, 0)

#define PPE_GET_PKT_CNT(tbl_cnt)			\
	FIELD_GET(PPE_W0_PKT_CNT, *(tbl_cnt))
#define PPE_GET_DROP_PKT_CNT_LOW(tbl_cnt)		\
	FIELD_GET(PPE_W2_DROP_PKT_CNT_LOW, *((tbl_cnt) + 0x2))
#define PPE_GET_DROP_PKT_CNT_HIGH(tbl_cnt)		\
	FIELD_GET(PPE_W3_DROP_PKT_CNT_HIGH, *((tbl_cnt) + 0x3))

/**
 * enum ppe_cnt_size_type - PPE counter size type
 * @PPE_PKT_CNT_SIZE_1WORD: Counter size with single register
 * @PPE_PKT_CNT_SIZE_3WORD: Counter size with table of 3 words
 * @PPE_PKT_CNT_SIZE_5WORD: Counter size with table of 5 words
 *
 * PPE takes the different register size to record the packet counters.
 * It uses single register, or register table with 3 words or 5 words.
 * The counter with table size 5 words also records the drop counter.
 * There are also some other counter types occupying sizes less than 32
 * bits, which is not covered by this enumeration type.
 */
enum ppe_cnt_size_type {
	PPE_PKT_CNT_SIZE_1WORD,
	PPE_PKT_CNT_SIZE_3WORD,
	PPE_PKT_CNT_SIZE_5WORD,
};

/**
 * enum ppe_cnt_type - PPE counter type.
 * @PPE_CNT_BM: Packet counter processed by BM.
 * @PPE_CNT_PARSE: Packet counter parsed on ingress.
 * @PPE_CNT_PORT_RX: Packet counter on the ingress port.
 * @PPE_CNT_VLAN_RX: VLAN packet counter received.
 * @PPE_CNT_L2_FWD: Packet counter processed by L2 forwarding.
 * @PPE_CNT_CPU_CODE: Packet counter marked with various CPU codes.
 * @PPE_CNT_VLAN_TX: VLAN packet counter transmitted.
 * @PPE_CNT_PORT_TX: Packet counter on the egress port.
 * @PPE_CNT_QM: Packet counter processed by QM.
 */
enum ppe_cnt_type {
	PPE_CNT_BM,
	PPE_CNT_PARSE,
	PPE_CNT_PORT_RX,
	PPE_CNT_VLAN_RX,
	PPE_CNT_L2_FWD,
	PPE_CNT_CPU_CODE,
	PPE_CNT_VLAN_TX,
	PPE_CNT_PORT_TX,
	PPE_CNT_QM,
};

/**
 * struct ppe_debugfs_entry - PPE debugfs entry.
 * @name: Debugfs file name.
 * @counter_type: PPE packet counter type.
 * @ppe: PPE device.
 *
 * The PPE debugfs entry is used to create the debugfs file and passed
 * to debugfs_create_file() as private data.
 */
struct ppe_debugfs_entry {
	const char *name;
	enum ppe_cnt_type counter_type;
	struct ppe_device *ppe;
};

static const struct ppe_debugfs_entry debugfs_files[] = {
	{
		.name			= "bm",
		.counter_type		= PPE_CNT_BM,
	},
	{
		.name			= "parse",
		.counter_type		= PPE_CNT_PARSE,
	},
	{
		.name			= "port_rx",
		.counter_type		= PPE_CNT_PORT_RX,
	},
	{
		.name			= "vlan_rx",
		.counter_type		= PPE_CNT_VLAN_RX,
	},
	{
		.name			= "l2_forward",
		.counter_type		= PPE_CNT_L2_FWD,
	},
	{
		.name			= "cpu_code",
		.counter_type		= PPE_CNT_CPU_CODE,
	},
	{
		.name			= "vlan_tx",
		.counter_type		= PPE_CNT_VLAN_TX,
	},
	{
		.name			= "port_tx",
		.counter_type		= PPE_CNT_PORT_TX,
	},
	{
		.name			= "qm",
		.counter_type		= PPE_CNT_QM,
	},
};

static int ppe_pkt_cnt_get(struct ppe_device *ppe_dev, u32 reg,
			   enum ppe_cnt_size_type cnt_type,
			   u32 *cnt, u32 *drop_cnt)
{
	u32 drop_pkt_cnt[PPE_DROP_PKT_CNT_TBL_SIZE];
	u32 pkt_cnt[PPE_PKT_CNT_TBL_SIZE];
	u32 value;
	int ret;

	switch (cnt_type) {
	case PPE_PKT_CNT_SIZE_1WORD:
		ret = regmap_read(ppe_dev->regmap, reg, &value);
		if (ret)
			return ret;

		*cnt = value;
		break;
	case PPE_PKT_CNT_SIZE_3WORD:
		ret = regmap_bulk_read(ppe_dev->regmap, reg,
				       pkt_cnt, ARRAY_SIZE(pkt_cnt));
		if (ret)
			return ret;

		*cnt = PPE_GET_PKT_CNT(pkt_cnt);
		break;
	case PPE_PKT_CNT_SIZE_5WORD:
		ret = regmap_bulk_read(ppe_dev->regmap, reg,
				       drop_pkt_cnt, ARRAY_SIZE(drop_pkt_cnt));
		if (ret)
			return ret;

		*cnt = PPE_GET_PKT_CNT(drop_pkt_cnt);

		/* Drop counter with low 24 bits. */
		value  = PPE_GET_DROP_PKT_CNT_LOW(drop_pkt_cnt);
		*drop_cnt = FIELD_PREP(GENMASK(23, 0), value);

		/* Drop counter with high 8 bits. */
		value  = PPE_GET_DROP_PKT_CNT_HIGH(drop_pkt_cnt);
		*drop_cnt |= FIELD_PREP(GENMASK(31, 24), value);
		break;
	}

	return 0;
}

static void ppe_tbl_pkt_cnt_clear(struct ppe_device *ppe_dev, u32 reg,
				  enum ppe_cnt_size_type cnt_type)
{
	u32 drop_pkt_cnt[PPE_DROP_PKT_CNT_TBL_SIZE] = {};
	u32 pkt_cnt[PPE_PKT_CNT_TBL_SIZE] = {};

	switch (cnt_type) {
	case PPE_PKT_CNT_SIZE_1WORD:
		regmap_write(ppe_dev->regmap, reg, 0);
		break;
	case PPE_PKT_CNT_SIZE_3WORD:
		regmap_bulk_write(ppe_dev->regmap, reg,
				  pkt_cnt, ARRAY_SIZE(pkt_cnt));
		break;
	case PPE_PKT_CNT_SIZE_5WORD:
		regmap_bulk_write(ppe_dev->regmap, reg,
				  drop_pkt_cnt, ARRAY_SIZE(drop_pkt_cnt));
		break;
	}
}

static int ppe_bm_counter_get(struct ppe_device *ppe_dev, struct seq_file *seq)
{
	u32 reg, val, pkt_cnt, pkt_cnt1;
	int ret, i, tag;

	seq_printf(seq, "%-24s", "BM SILENT_DROP:");
	tag = 0;
	for (i = 0; i < PPE_DROP_CNT_TBL_ENTRIES; i++) {
		reg = PPE_DROP_CNT_TBL_ADDR + i * PPE_DROP_CNT_TBL_INC;
		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_1WORD,
				      &pkt_cnt, NULL);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		if (pkt_cnt > 0) {
			if (!((++tag) % 4))
				seq_printf(seq, "\n%-24s", "");

			seq_printf(seq, "%10u(%s=%04d)", pkt_cnt, "port", i);
		}
	}

	seq_putc(seq, '\n');

	/* The number of packets dropped because hardware buffers were
	 * available only partially for the packet.
	 */
	seq_printf(seq, "%-24s", "BM OVERFLOW_DROP:");
	tag = 0;
	for (i = 0; i < PPE_DROP_STAT_TBL_ENTRIES; i++) {
		reg = PPE_DROP_STAT_TBL_ADDR + PPE_DROP_STAT_TBL_INC * i;

		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD,
				      &pkt_cnt, NULL);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		if (pkt_cnt > 0) {
			if (!((++tag) % 4))
				seq_printf(seq, "\n%-24s", "");

			seq_printf(seq, "%10u(%s=%04d)", pkt_cnt, "port", i);
		}
	}

	seq_putc(seq, '\n');

	/* The number of currently occupied buffers, that can't be flushed. */
	seq_printf(seq, "%-24s", "BM USED/REACT:");
	tag = 0;
	for (i = 0; i < PPE_BM_USED_CNT_TBL_ENTRIES; i++) {
		reg = PPE_BM_USED_CNT_TBL_ADDR + i * PPE_BM_USED_CNT_TBL_INC;
		ret = regmap_read(ppe_dev->regmap, reg, &val);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		/* The number of PPE buffers used for caching the received
		 * packets before the pause frame sent.
		 */
		pkt_cnt = FIELD_GET(PPE_BM_USED_CNT_VAL, val);

		reg = PPE_BM_REACT_CNT_TBL_ADDR + i * PPE_BM_REACT_CNT_TBL_INC;
		ret = regmap_read(ppe_dev->regmap, reg, &val);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		/* The number of PPE buffers used for caching the received
		 * packets after pause frame sent out.
		 */
		pkt_cnt1 = FIELD_GET(PPE_BM_REACT_CNT_VAL, val);

		if (pkt_cnt > 0 || pkt_cnt1 > 0) {
			if (!((++tag) % 4))
				seq_printf(seq, "\n%-24s", "");

			seq_printf(seq, "%10u/%u(%s=%04d)", pkt_cnt, pkt_cnt1,
				   "port", i);
		}
	}

	seq_putc(seq, '\n');

	return 0;
}

/* The number of packets processed by the ingress parser module of PPE. */
static int ppe_parse_pkt_counter_get(struct ppe_device *ppe_dev,
				     struct seq_file *seq)
{
	u32 reg, cnt = 0, tunnel_cnt = 0;
	int i, ret, tag = 0;

	seq_printf(seq, "%-24s", "PARSE TPRX/IPRX:");
	for (i = 0; i < PPE_IPR_PKT_CNT_TBL_ENTRIES; i++) {
		reg = PPE_TPR_PKT_CNT_TBL_ADDR + i * PPE_TPR_PKT_CNT_TBL_INC;
		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_1WORD,
				      &tunnel_cnt, NULL);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		reg = PPE_IPR_PKT_CNT_TBL_ADDR + i * PPE_IPR_PKT_CNT_TBL_INC;
		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_1WORD,
				      &cnt, NULL);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		if (tunnel_cnt > 0 || cnt > 0) {
			if (!((++tag) % 4))
				seq_printf(seq, "\n%-24s", "");

			seq_printf(seq, "%10u/%u(%s=%04d)", tunnel_cnt, cnt,
				   "port", i);
		}
	}

	seq_putc(seq, '\n');

	return 0;
}

/* The number of packets received or dropped on the ingress port. */
static int ppe_port_rx_counter_get(struct ppe_device *ppe_dev,
				   struct seq_file *seq)
{
	u32 reg, pkt_cnt = 0, drop_cnt = 0;
	int ret, i, tag;

	seq_printf(seq, "%-24s", "PORT RX/RX_DROP:");
	tag = 0;
	for (i = 0; i < PPE_PHY_PORT_RX_CNT_TBL_ENTRIES; i++) {
		reg = PPE_PHY_PORT_RX_CNT_TBL_ADDR + PPE_PHY_PORT_RX_CNT_TBL_INC * i;
		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_5WORD,
				      &pkt_cnt, &drop_cnt);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		if (pkt_cnt > 0) {
			if (!((++tag) % 4))
				seq_printf(seq, "\n%-24s", "");

			seq_printf(seq, "%10u/%u(%s=%04d)", pkt_cnt, drop_cnt,
				   "port", i);
		}
	}

	seq_putc(seq, '\n');

	seq_printf(seq, "%-24s", "VPORT RX/RX_DROP:");
	tag = 0;
	for (i = 0; i < PPE_PORT_RX_CNT_TBL_ENTRIES; i++) {
		reg = PPE_PORT_RX_CNT_TBL_ADDR + PPE_PORT_RX_CNT_TBL_INC * i;
		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_5WORD,
				      &pkt_cnt, &drop_cnt);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		if (pkt_cnt > 0) {
			if (!((++tag) % 4))
				seq_printf(seq, "\n%-24s", "");

			seq_printf(seq, "%10u/%u(%s=%04d)", pkt_cnt, drop_cnt,
				   "port", i);
		}
	}

	seq_putc(seq, '\n');

	return 0;
}

/* The number of packets received or dropped by layer 2 processing. */
static int ppe_l2_counter_get(struct ppe_device *ppe_dev,
			      struct seq_file *seq)
{
	u32 reg, pkt_cnt = 0, drop_cnt = 0;
	int ret, i, tag = 0;

	seq_printf(seq, "%-24s", "L2 RX/RX_DROP:");
	for (i = 0; i < PPE_PRE_L2_CNT_TBL_ENTRIES; i++) {
		reg = PPE_PRE_L2_CNT_TBL_ADDR + PPE_PRE_L2_CNT_TBL_INC * i;
		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_5WORD,
				      &pkt_cnt, &drop_cnt);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		if (pkt_cnt > 0) {
			if (!((++tag) % 4))
				seq_printf(seq, "\n%-24s", "");

			seq_printf(seq, "%10u/%u(%s=%04d)", pkt_cnt, drop_cnt,
				   "vsi", i);
		}
	}

	seq_putc(seq, '\n');

	return 0;
}

/* The number of VLAN packets received by PPE. */
static int ppe_vlan_rx_counter_get(struct ppe_device *ppe_dev,
				   struct seq_file *seq)
{
	u32 reg, pkt_cnt = 0;
	int ret, i, tag = 0;

	seq_printf(seq, "%-24s", "VLAN RX:");
	for (i = 0; i < PPE_VLAN_CNT_TBL_ENTRIES; i++) {
		reg = PPE_VLAN_CNT_TBL_ADDR + PPE_VLAN_CNT_TBL_INC * i;

		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD,
				      &pkt_cnt, NULL);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		if (pkt_cnt > 0) {
			if (!((++tag) % 4))
				seq_printf(seq, "\n%-24s", "");

			seq_printf(seq, "%10u(%s=%04d)", pkt_cnt, "vsi", i);
		}
	}

	seq_putc(seq, '\n');

	return 0;
}

/* The number of packets handed to CPU by PPE. */
static int ppe_cpu_code_counter_get(struct ppe_device *ppe_dev,
				    struct seq_file *seq)
{
	u32 reg, pkt_cnt = 0;
	int ret, i;

	seq_printf(seq, "%-24s", "CPU CODE:");
	for (i = 0; i < PPE_DROP_CPU_CNT_TBL_ENTRIES; i++) {
		reg = PPE_DROP_CPU_CNT_TBL_ADDR + PPE_DROP_CPU_CNT_TBL_INC * i;

		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD,
				      &pkt_cnt, NULL);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		if (!pkt_cnt)
			continue;

		/* There are 256 CPU codes saved in the first 256 entries
		 * of register table, and 128 drop codes for each PPE port
		 * (0-7), the total entries is 256 + 8 * 128.
		 */
		if (i < 256)
			seq_printf(seq, "%10u(cpucode:%d)", pkt_cnt, i);
		else
			seq_printf(seq, "%10u(port=%04d),dropcode:%d", pkt_cnt,
				   (i - 256) % 8, (i - 256) / 8);
		seq_putc(seq, '\n');
		seq_printf(seq, "%-24s", "");
	}

	seq_putc(seq, '\n');

	return 0;
}

/* The number of packets forwarded by VLAN on the egress direction. */
static int ppe_vlan_tx_counter_get(struct ppe_device *ppe_dev,
				   struct seq_file *seq)
{
	u32 reg, pkt_cnt = 0;
	int ret, i, tag = 0;

	seq_printf(seq, "%-24s", "VLAN TX:");
	for (i = 0; i < PPE_EG_VSI_COUNTER_TBL_ENTRIES; i++) {
		reg = PPE_EG_VSI_COUNTER_TBL_ADDR + PPE_EG_VSI_COUNTER_TBL_INC * i;

		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD,
				      &pkt_cnt, NULL);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		if (pkt_cnt > 0) {
			if (!((++tag) % 4))
				seq_printf(seq, "\n%-24s", "");

			seq_printf(seq, "%10u(%s=%04d)", pkt_cnt, "vsi", i);
		}
	}

	seq_putc(seq, '\n');

	return 0;
}

/* The number of packets transmitted or dropped on the egress port. */
static int ppe_port_tx_counter_get(struct ppe_device *ppe_dev,
				   struct seq_file *seq)
{
	u32 reg, pkt_cnt = 0, drop_cnt = 0;
	int ret, i, tag;

	seq_printf(seq, "%-24s", "VPORT TX/TX_DROP:");
	tag = 0;
	for (i = 0; i < PPE_VPORT_TX_COUNTER_TBL_ENTRIES; i++) {
		reg = PPE_VPORT_TX_COUNTER_TBL_ADDR + PPE_VPORT_TX_COUNTER_TBL_INC * i;
		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD,
				      &pkt_cnt, NULL);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		reg = PPE_VPORT_TX_DROP_CNT_TBL_ADDR + PPE_VPORT_TX_DROP_CNT_TBL_INC * i;
		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD,
				      &drop_cnt, NULL);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		if (pkt_cnt > 0 || drop_cnt > 0) {
			if (!((++tag) % 4))
				seq_printf(seq, "\n%-24s", "");

			seq_printf(seq, "%10u/%u(%s=%04d)", pkt_cnt, drop_cnt,
				   "port", i);
		}
	}

	seq_putc(seq, '\n');

	seq_printf(seq, "%-24s", "PORT TX/TX_DROP:");
	tag = 0;
	for (i = 0; i < PPE_PORT_TX_COUNTER_TBL_ENTRIES; i++) {
		reg = PPE_PORT_TX_COUNTER_TBL_ADDR + PPE_PORT_TX_COUNTER_TBL_INC * i;
		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD,
				      &pkt_cnt, NULL);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		reg = PPE_PORT_TX_DROP_CNT_TBL_ADDR + PPE_PORT_TX_DROP_CNT_TBL_INC * i;
		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD,
				      &drop_cnt, NULL);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		if (pkt_cnt > 0 || drop_cnt > 0) {
			if (!((++tag) % 4))
				seq_printf(seq, "\n%-24s", "");

			seq_printf(seq, "%10u/%u(%s=%04d)", pkt_cnt, drop_cnt,
				   "port", i);
		}
	}

	seq_putc(seq, '\n');

	return 0;
}

/* The number of packets transmitted or pending by the PPE queue. */
static int ppe_queue_counter_get(struct ppe_device *ppe_dev,
				 struct seq_file *seq)
{
	u32 reg, val, pkt_cnt = 0, pend_cnt = 0, drop_cnt = 0;
	int ret, i, tag = 0;

	seq_printf(seq, "%-24s", "QUEUE TX/PEND/DROP:");
	for (i = 0; i < PPE_QUEUE_TX_COUNTER_TBL_ENTRIES; i++) {
		reg = PPE_QUEUE_TX_COUNTER_TBL_ADDR + PPE_QUEUE_TX_COUNTER_TBL_INC * i;
		ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD,
				      &pkt_cnt, NULL);
		if (ret) {
			dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
			return ret;
		}

		if (i < PPE_AC_UNICAST_QUEUE_CFG_TBL_ENTRIES) {
			reg = PPE_AC_UNICAST_QUEUE_CNT_TBL_ADDR +
			      PPE_AC_UNICAST_QUEUE_CNT_TBL_INC * i;
			ret = regmap_read(ppe_dev->regmap, reg, &val);
			if (ret) {
				dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
				return ret;
			}

			pend_cnt = FIELD_GET(PPE_AC_UNICAST_QUEUE_CNT_TBL_PEND_CNT, val);

			reg = PPE_UNICAST_DROP_CNT_TBL_ADDR +
			      PPE_AC_UNICAST_QUEUE_CNT_TBL_INC *
			      (i * PPE_UNICAST_DROP_TYPES + PPE_UNICAST_DROP_FORCE_OFFSET);

			ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD,
					      &drop_cnt, NULL);
			if (ret) {
				dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
				return ret;
			}
		} else {
			int mq_offset = i - PPE_AC_UNICAST_QUEUE_CFG_TBL_ENTRIES;

			reg = PPE_AC_MULTICAST_QUEUE_CNT_TBL_ADDR +
			      PPE_AC_MULTICAST_QUEUE_CNT_TBL_INC * mq_offset;
			ret = regmap_read(ppe_dev->regmap, reg, &val);
			if (ret) {
				dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
				return ret;
			}

			pend_cnt = FIELD_GET(PPE_AC_MULTICAST_QUEUE_CNT_TBL_PEND_CNT, val);

			if (mq_offset < PPE_P0_MULTICAST_QUEUE_NUM) {
				reg = PPE_CPU_PORT_MULTICAST_FORCE_DROP_CNT_TBL_ADDR(mq_offset);
			} else {
				mq_offset -= PPE_P0_MULTICAST_QUEUE_NUM;

				reg = PPE_P1_MULTICAST_DROP_CNT_TBL_ADDR;
				reg += (mq_offset / PPE_MULTICAST_QUEUE_NUM) *
					PPE_MULTICAST_QUEUE_PORT_ADDR_INC;
				reg += (mq_offset % PPE_MULTICAST_QUEUE_NUM) *
					PPE_MULTICAST_DROP_CNT_TBL_INC *
					PPE_MULTICAST_DROP_TYPES;
			}

			ret = ppe_pkt_cnt_get(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD,
					      &drop_cnt, NULL);
			if (ret) {
				dev_err(ppe_dev->dev, "CNT ERROR %d\n", ret);
				return ret;
			}
		}

		if (pkt_cnt > 0 || pend_cnt > 0 || drop_cnt > 0) {
			if (!((++tag) % 4))
				seq_printf(seq, "\n%-24s", "");

			seq_printf(seq, "%10u/%u/%u(%s=%04d)",
				   pkt_cnt, pend_cnt, drop_cnt, "queue", i);
		}
	}

	seq_putc(seq, '\n');

	return 0;
}

/* Display the various packet counters of PPE. */
static int ppe_packet_counter_show(struct seq_file *seq, void *v)
{
	struct ppe_debugfs_entry *entry = seq->private;
	struct ppe_device *ppe_dev = entry->ppe;
	int ret;

	switch (entry->counter_type) {
	case PPE_CNT_BM:
		ret = ppe_bm_counter_get(ppe_dev, seq);
		break;
	case PPE_CNT_PARSE:
		ret = ppe_parse_pkt_counter_get(ppe_dev, seq);
		break;
	case PPE_CNT_PORT_RX:
		ret = ppe_port_rx_counter_get(ppe_dev, seq);
		break;
	case PPE_CNT_VLAN_RX:
		ret = ppe_vlan_rx_counter_get(ppe_dev, seq);
		break;
	case PPE_CNT_L2_FWD:
		ret = ppe_l2_counter_get(ppe_dev, seq);
		break;
	case PPE_CNT_CPU_CODE:
		ret = ppe_cpu_code_counter_get(ppe_dev, seq);
		break;
	case PPE_CNT_VLAN_TX:
		ret = ppe_vlan_tx_counter_get(ppe_dev, seq);
		break;
	case PPE_CNT_PORT_TX:
		ret = ppe_port_tx_counter_get(ppe_dev, seq);
		break;
	case PPE_CNT_QM:
		ret = ppe_queue_counter_get(ppe_dev, seq);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/* Flush the various packet counters of PPE. */
static ssize_t ppe_packet_counter_write(struct file *file,
					const char __user *buf,
					size_t count, loff_t *pos)
{
	struct ppe_debugfs_entry *entry = file_inode(file)->i_private;
	struct ppe_device *ppe_dev = entry->ppe;
	u32 reg;
	int i;

	switch (entry->counter_type) {
	case PPE_CNT_BM:
		for (i = 0; i < PPE_DROP_CNT_TBL_ENTRIES; i++) {
			reg = PPE_DROP_CNT_TBL_ADDR + i * PPE_DROP_CNT_TBL_INC;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_1WORD);
		}

		for (i = 0; i < PPE_DROP_STAT_TBL_ENTRIES; i++) {
			reg = PPE_DROP_STAT_TBL_ADDR + PPE_DROP_STAT_TBL_INC * i;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD);
		}

		break;
	case PPE_CNT_PARSE:
		for (i = 0; i < PPE_IPR_PKT_CNT_TBL_ENTRIES; i++) {
			reg = PPE_IPR_PKT_CNT_TBL_ADDR + i * PPE_IPR_PKT_CNT_TBL_INC;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_1WORD);

			reg = PPE_TPR_PKT_CNT_TBL_ADDR + i * PPE_TPR_PKT_CNT_TBL_INC;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_1WORD);
		}

		break;
	case PPE_CNT_PORT_RX:
		for (i = 0; i < PPE_PORT_RX_CNT_TBL_ENTRIES; i++) {
			reg = PPE_PORT_RX_CNT_TBL_ADDR + PPE_PORT_RX_CNT_TBL_INC * i;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_5WORD);
		}

		for (i = 0; i < PPE_PHY_PORT_RX_CNT_TBL_ENTRIES; i++) {
			reg = PPE_PHY_PORT_RX_CNT_TBL_ADDR + PPE_PHY_PORT_RX_CNT_TBL_INC * i;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_5WORD);
		}

		break;
	case PPE_CNT_VLAN_RX:
		for (i = 0; i < PPE_VLAN_CNT_TBL_ENTRIES; i++) {
			reg = PPE_VLAN_CNT_TBL_ADDR + PPE_VLAN_CNT_TBL_INC * i;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD);
		}

		break;
	case PPE_CNT_L2_FWD:
		for (i = 0; i < PPE_PRE_L2_CNT_TBL_ENTRIES; i++) {
			reg = PPE_PRE_L2_CNT_TBL_ADDR + PPE_PRE_L2_CNT_TBL_INC * i;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_5WORD);
		}

		break;
	case PPE_CNT_CPU_CODE:
		for (i = 0; i < PPE_DROP_CPU_CNT_TBL_ENTRIES; i++) {
			reg = PPE_DROP_CPU_CNT_TBL_ADDR + PPE_DROP_CPU_CNT_TBL_INC * i;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD);
		}

		break;
	case PPE_CNT_VLAN_TX:
		for (i = 0; i < PPE_EG_VSI_COUNTER_TBL_ENTRIES; i++) {
			reg = PPE_EG_VSI_COUNTER_TBL_ADDR + PPE_EG_VSI_COUNTER_TBL_INC * i;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD);
		}

		break;
	case PPE_CNT_PORT_TX:
		for (i = 0; i < PPE_PORT_TX_COUNTER_TBL_ENTRIES; i++) {
			reg = PPE_PORT_TX_DROP_CNT_TBL_ADDR + PPE_PORT_TX_DROP_CNT_TBL_INC * i;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD);

			reg = PPE_PORT_TX_COUNTER_TBL_ADDR + PPE_PORT_TX_COUNTER_TBL_INC * i;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD);
		}

		for (i = 0; i < PPE_VPORT_TX_COUNTER_TBL_ENTRIES; i++) {
			reg = PPE_VPORT_TX_COUNTER_TBL_ADDR + PPE_VPORT_TX_COUNTER_TBL_INC * i;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD);

			reg = PPE_VPORT_TX_DROP_CNT_TBL_ADDR + PPE_VPORT_TX_DROP_CNT_TBL_INC * i;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD);
		}

		break;
	case PPE_CNT_QM:
		for (i = 0; i < PPE_QUEUE_TX_COUNTER_TBL_ENTRIES; i++) {
			reg = PPE_QUEUE_TX_COUNTER_TBL_ADDR + PPE_QUEUE_TX_COUNTER_TBL_INC * i;
			ppe_tbl_pkt_cnt_clear(ppe_dev, reg, PPE_PKT_CNT_SIZE_3WORD);
		}

		break;
	default:
		break;
	}

	return count;
}
DEFINE_SHOW_STORE_ATTRIBUTE(ppe_packet_counter);

void ppe_debugfs_setup(struct ppe_device *ppe_dev)
{
	struct ppe_debugfs_entry *entry;
	int i;

	ppe_dev->debugfs_root = debugfs_create_dir("ppe", NULL);
	if (IS_ERR(ppe_dev->debugfs_root))
		return;

	for (i = 0; i < ARRAY_SIZE(debugfs_files); i++) {
		entry = devm_kzalloc(ppe_dev->dev, sizeof(*entry), GFP_KERNEL);
		if (!entry)
			return;

		entry->ppe = ppe_dev;
		entry->counter_type = debugfs_files[i].counter_type;

		debugfs_create_file(debugfs_files[i].name, 0444,
				    ppe_dev->debugfs_root, entry,
				    &ppe_packet_counter_fops);
	}
}

void ppe_debugfs_teardown(struct ppe_device *ppe_dev)
{
	debugfs_remove_recursive(ppe_dev->debugfs_root);
	ppe_dev->debugfs_root = NULL;
}
