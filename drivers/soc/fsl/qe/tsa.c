// SPDX-License-Identifier: GPL-2.0
/*
 * TSA driver
 *
 * Copyright 2022 CS GROUP France
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */

#include "tsa.h"
#include <dt-bindings/soc/cpm1-fsl,tsa.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>


/* TSA SI RAM routing tables entry */
#define TSA_SIRAM_ENTRY_LAST		(1 << 16)
#define TSA_SIRAM_ENTRY_BYTE		(1 << 17)
#define TSA_SIRAM_ENTRY_CNT(x)		(((x) & 0x0f) << 18)
#define TSA_SIRAM_ENTRY_CSEL_MASK	(0x7 << 22)
#define TSA_SIRAM_ENTRY_CSEL_NU		(0x0 << 22)
#define TSA_SIRAM_ENTRY_CSEL_SCC2	(0x2 << 22)
#define TSA_SIRAM_ENTRY_CSEL_SCC3	(0x3 << 22)
#define TSA_SIRAM_ENTRY_CSEL_SCC4	(0x4 << 22)
#define TSA_SIRAM_ENTRY_CSEL_SMC1	(0x5 << 22)
#define TSA_SIRAM_ENTRY_CSEL_SMC2	(0x6 << 22)

/* SI mode register (32 bits) */
#define TSA_SIMODE	0x00
#define   TSA_SIMODE_SMC2			0x80000000
#define   TSA_SIMODE_SMC1			0x00008000
#define   TSA_SIMODE_TDMA(x)			((x) << 0)
#define   TSA_SIMODE_TDMB(x)			((x) << 16)
#define     TSA_SIMODE_TDM_MASK			0x0fff
#define     TSA_SIMODE_TDM_SDM_MASK		0x0c00
#define       TSA_SIMODE_TDM_SDM_NORM		0x0000
#define       TSA_SIMODE_TDM_SDM_ECHO		0x0400
#define       TSA_SIMODE_TDM_SDM_INTL_LOOP	0x0800
#define       TSA_SIMODE_TDM_SDM_LOOP_CTRL	0x0c00
#define     TSA_SIMODE_TDM_RFSD(x)		((x) << 8)
#define     TSA_SIMODE_TDM_DSC			0x0080
#define     TSA_SIMODE_TDM_CRT			0x0040
#define     TSA_SIMODE_TDM_STZ			0x0020
#define     TSA_SIMODE_TDM_CE			0x0010
#define     TSA_SIMODE_TDM_FE			0x0008
#define     TSA_SIMODE_TDM_GM			0x0004
#define     TSA_SIMODE_TDM_TFSD(x)		((x) << 0)

/* SI global mode register (8 bits) */
#define TSA_SIGMR	0x04
#define TSA_SIGMR_ENB			(1<<3)
#define TSA_SIGMR_ENA			(1<<2)
#define TSA_SIGMR_RDM_MASK		0x03
#define   TSA_SIGMR_RDM_STATIC_TDMA	0x00
#define   TSA_SIGMR_RDM_DYN_TDMA	0x01
#define   TSA_SIGMR_RDM_STATIC_TDMAB	0x02
#define   TSA_SIGMR_RDM_DYN_TDMAB	0x03

/* SI status register (8 bits) */
#define TSA_SISTR	0x06

/* SI command register (8 bits) */
#define TSA_SICMR	0x07

/* SI clock route register (32 bits) */
#define TSA_SICR	0x0C
#define   TSA_SICR_SCC2(x)		((x) << 8)
#define   TSA_SICR_SCC3(x)		((x) << 16)
#define   TSA_SICR_SCC4(x)		((x) << 24)
#define     TSA_SICR_SCC_MASK		0x0ff
#define     TSA_SICR_SCC_GRX		(1 << 7)
#define     TSA_SICR_SCC_SCX_TSA	(1 << 6)
#define     TSA_SICR_SCC_RXCS_MASK	(0x7 << 3)
#define       TSA_SICR_SCC_RXCS_BRG1	(0x0 << 3)
#define       TSA_SICR_SCC_RXCS_BRG2	(0x1 << 3)
#define       TSA_SICR_SCC_RXCS_BRG3	(0x2 << 3)
#define       TSA_SICR_SCC_RXCS_BRG4	(0x3 << 3)
#define       TSA_SICR_SCC_RXCS_CLK15	(0x4 << 3)
#define       TSA_SICR_SCC_RXCS_CLK26	(0x5 << 3)
#define       TSA_SICR_SCC_RXCS_CLK37	(0x6 << 3)
#define       TSA_SICR_SCC_RXCS_CLK48	(0x7 << 3)
#define     TSA_SICR_SCC_TXCS_MASK	(0x7 << 0)
#define       TSA_SICR_SCC_TXCS_BRG1	(0x0 << 0)
#define       TSA_SICR_SCC_TXCS_BRG2	(0x1 << 0)
#define       TSA_SICR_SCC_TXCS_BRG3	(0x2 << 0)
#define       TSA_SICR_SCC_TXCS_BRG4	(0x3 << 0)
#define       TSA_SICR_SCC_TXCS_CLK15	(0x4 << 0)
#define       TSA_SICR_SCC_TXCS_CLK26	(0x5 << 0)
#define       TSA_SICR_SCC_TXCS_CLK37	(0x6 << 0)
#define       TSA_SICR_SCC_TXCS_CLK48	(0x7 << 0)

/* Serial interface RAM pointer register (32 bits) */
#define TSA_SIRP	0x10

struct tsa_entries_area {
	void __iomem *entries_start;
	void __iomem *entries_next;
	void __iomem *last_entry;
};

struct tsa_tdm {
	bool is_enable;
	struct clk *l1rclk_clk;
	struct clk *l1rsync_clk;
	struct clk *l1tclk_clk;
	struct clk *l1tsync_clk;
	u32 simode_tdm;
};

#define TSA_TDMA	0
#define TSA_TDMB	1

struct tsa {
	struct device *dev;
	void __iomem *si_regs;
	void __iomem *si_ram;
	resource_size_t si_ram_sz;
	spinlock_t	lock;
	int tdms; /* TSA_TDMx ORed */
	struct tsa_tdm tdm[2]; /* TDMa and TDMb */
	struct tsa_serial {
		unsigned int id;
		struct tsa_serial_info info;
	} serials[6];
};

static inline struct tsa *tsa_serial_get_tsa(struct tsa_serial *tsa_serial)
{
	/* The serials table is indexed by the serial id */
	return container_of(tsa_serial, struct tsa, serials[tsa_serial->id]);
}

static inline void tsa_write32(void __iomem *addr, u32 val)
{
	iowrite32be(val, addr);
}

static inline void tsa_write8(void __iomem *addr, u32 val)
{
	iowrite8(val, addr);
}

static inline u32 tsa_read32(void __iomem *addr)
{
	return ioread32be(addr);
}

static inline void tsa_clrbits32(void __iomem *addr, u32 clr)
{
	tsa_write32(addr, tsa_read32(addr) & ~clr);
}

static inline void tsa_clrsetbits32(void __iomem *addr, u32 clr, u32 set)
{
	tsa_write32(addr, (tsa_read32(addr) & ~clr) | set);
}

int tsa_serial_connect(struct tsa_serial *tsa_serial)
{
	struct tsa *tsa = tsa_serial_get_tsa(tsa_serial);
	unsigned long flags;
	u32 clear;
	u32 set;

	switch (tsa_serial->id) {
	case FSL_CPM_TSA_SCC2:
		clear = TSA_SICR_SCC2(TSA_SICR_SCC_MASK);
		set = TSA_SICR_SCC2(TSA_SICR_SCC_SCX_TSA);
		break;
	case FSL_CPM_TSA_SCC3:
		clear = TSA_SICR_SCC3(TSA_SICR_SCC_MASK);
		set = TSA_SICR_SCC3(TSA_SICR_SCC_SCX_TSA);
		break;
	case FSL_CPM_TSA_SCC4:
		clear = TSA_SICR_SCC4(TSA_SICR_SCC_MASK);
		set = TSA_SICR_SCC4(TSA_SICR_SCC_SCX_TSA);
		break;
	default:
		dev_err(tsa->dev, "Unsupported serial id %u\n", tsa_serial->id);
		return -EINVAL;
	}

	spin_lock_irqsave(&tsa->lock, flags);
	tsa_clrsetbits32(tsa->si_regs + TSA_SICR, clear, set);
	spin_unlock_irqrestore(&tsa->lock, flags);

	return 0;
}
EXPORT_SYMBOL(tsa_serial_connect);

int tsa_serial_disconnect(struct tsa_serial *tsa_serial)
{
	struct tsa *tsa = tsa_serial_get_tsa(tsa_serial);
	unsigned long flags;
	u32 clear;

	switch (tsa_serial->id) {
	case FSL_CPM_TSA_SCC2:
		clear = TSA_SICR_SCC2(TSA_SICR_SCC_MASK);
		break;
	case FSL_CPM_TSA_SCC3:
		clear = TSA_SICR_SCC3(TSA_SICR_SCC_MASK);
		break;
	case FSL_CPM_TSA_SCC4:
		clear = TSA_SICR_SCC4(TSA_SICR_SCC_MASK);
		break;
	default:
		dev_err(tsa->dev, "Unsupported serial id %u\n", tsa_serial->id);
		return -EINVAL;
	}

	spin_lock_irqsave(&tsa->lock, flags);
	tsa_clrsetbits32(tsa->si_regs + TSA_SICR, clear, 0);
	spin_unlock_irqrestore(&tsa->lock, flags);

	return 0;
}
EXPORT_SYMBOL(tsa_serial_disconnect);

int tsa_serial_get_info(struct tsa_serial *tsa_serial, struct tsa_serial_info *info)
{
	memcpy(info, &tsa_serial->info, sizeof(*info));
	return 0;
}
EXPORT_SYMBOL(tsa_serial_get_info);

static void tsa_init_entries_area(struct tsa *tsa, struct tsa_entries_area *area,
				  u32 tdms, u32 tdm_id, bool is_rx)
{
	resource_size_t quarter;
	resource_size_t half;

	quarter = tsa->si_ram_sz/4;
	half = tsa->si_ram_sz/2;

	if (tdms == BIT(TSA_TDMA)) {
		/* Only TDMA */
		if (is_rx) {
			/* First half of si_ram */
			area->entries_start = tsa->si_ram;
			area->entries_next = area->entries_start + half;
			area->last_entry = NULL;
		} else {
			/* Second half of si_ram */
			area->entries_start = tsa->si_ram + half;
			area->entries_next = area->entries_start + half;
			area->last_entry = NULL;
		}
	} else {
		/* Only TDMB or both TDMs */
		if (tdm_id == TSA_TDMA) {
			if (is_rx) {
				/* First half of first half of si_ram */
				area->entries_start = tsa->si_ram;
				area->entries_next = area->entries_start + quarter;
				area->last_entry = NULL;
			} else {
				/* First half of second half of si_ram */
				area->entries_start = tsa->si_ram + (2 * quarter);
				area->entries_next = area->entries_start + quarter;
				area->last_entry = NULL;
			}
		} else {
			if (is_rx) {
				/* Second half of first half of si_ram */
				area->entries_start = tsa->si_ram + quarter;
				area->entries_next = area->entries_start + quarter;
				area->last_entry = NULL;
			} else {
				/* Second half of second half of si_ram */
				area->entries_start = tsa->si_ram + (3 * quarter);
				area->entries_next = area->entries_start + quarter;
				area->last_entry = NULL;
			}
		}
	}
}

static const char *tsa_serial_id2name(struct tsa *tsa, u32 serial_id)
{
	switch (serial_id) {
	case FSL_CPM_TSA_NU:	return "Not used";
	case FSL_CPM_TSA_SCC2:	return "SCC2";
	case FSL_CPM_TSA_SCC3:	return "SCC3";
	case FSL_CPM_TSA_SCC4:	return "SCC4";
	case FSL_CPM_TSA_SMC1:	return "SMC1";
	case FSL_CPM_TSA_SMC2:	return "SMC2";
	default:
		break;
	}
	return NULL;
}

static u32 tsa_serial_id2csel(struct tsa *tsa, u32 serial_id)
{
	switch (serial_id) {
	case FSL_CPM_TSA_SCC2:	return TSA_SIRAM_ENTRY_CSEL_SCC2;
	case FSL_CPM_TSA_SCC3:	return TSA_SIRAM_ENTRY_CSEL_SCC3;
	case FSL_CPM_TSA_SCC4:	return TSA_SIRAM_ENTRY_CSEL_SCC4;
	case FSL_CPM_TSA_SMC1:	return TSA_SIRAM_ENTRY_CSEL_SMC1;
	case FSL_CPM_TSA_SMC2:	return TSA_SIRAM_ENTRY_CSEL_SMC2;
	default:
		break;
	}
	return TSA_SIRAM_ENTRY_CSEL_NU;
}

static int tsa_add_entry(struct tsa *tsa, struct tsa_entries_area *area,
			 u32 count, u32 serial_id)
{
	void __iomem *addr;
	u32 left;
	u32 val;
	u32 cnt;
	u32 nb;

	addr = area->last_entry ? area->last_entry + 4 : area->entries_start;

	nb = DIV_ROUND_UP(count, 8);
	if ((addr + (nb * 4)) > area->entries_next) {
		dev_err(tsa->dev, "si ram area full\n");
		return -ENOSPC;
	}

	if (area->last_entry) {
		/* Clear last flag */
		tsa_clrbits32(area->last_entry, TSA_SIRAM_ENTRY_LAST);
	}

	left = count;
	while (left) {
		val = TSA_SIRAM_ENTRY_BYTE | tsa_serial_id2csel(tsa, serial_id);

		if (left > 16) {
			cnt = 16;
		} else {
			cnt = left;
			val |= TSA_SIRAM_ENTRY_LAST;
			area->last_entry = addr;
		}
		val |= TSA_SIRAM_ENTRY_CNT(cnt - 1);

		tsa_write32(addr, val);
		addr += 4;
		left -= cnt;
	}

	return 0;
}

static int tsa_of_parse_tdm_route(struct tsa *tsa, struct device_node *tdm_np,
				  u32 tdms, u32 tdm_id, bool is_rx)
{
	struct tsa_entries_area area;
	const char *route_name;
	u32 serial_id;
	int len, i;
	u32 count;
	const char *serial_name;
	struct tsa_serial_info *serial_info;
	struct tsa_tdm *tdm;
	int ret;
	u32 ts;

	route_name = is_rx ? "fsl,rx-ts-routes" : "fsl,tx-ts-routes";

	len = of_property_count_u32_elems(tdm_np,  route_name);
	if (len < 0) {
		dev_err(tsa->dev, "%pOF: failed to read %s\n", tdm_np, route_name);
		return len;
	}
	if (len % 2 != 0) {
		dev_err(tsa->dev, "%pOF: wrong %s format\n", tdm_np, route_name);
		return -EINVAL;
	}

	tsa_init_entries_area(tsa, &area, tdms, tdm_id, is_rx);
	ts = 0;
	for (i = 0; i < len; i += 2) {
		of_property_read_u32_index(tdm_np, route_name, i, &count);
		of_property_read_u32_index(tdm_np, route_name, i + 1, &serial_id);

		if (serial_id >= ARRAY_SIZE(tsa->serials)) {
			dev_err(tsa->dev, "%pOF: invalid serial id (%u)\n",
				tdm_np, serial_id);
			return -EINVAL;
		}

		serial_name = tsa_serial_id2name(tsa, serial_id);
		if (!serial_name) {
			dev_err(tsa->dev, "%pOF: unsupported serial id (%u)\n",
				tdm_np, serial_id);
			return -EINVAL;
		}

		dev_dbg(tsa->dev, "tdm_id=%u, %s ts %u..%u -> %s\n",
			tdm_id, route_name, ts, ts+count-1, serial_name);
		ts += count;

		ret = tsa_add_entry(tsa, &area, count, serial_id);
		if (ret)
			return ret;

		serial_info = &tsa->serials[serial_id].info;
		tdm = &tsa->tdm[tdm_id];
		if (is_rx) {
			serial_info->rx_fs_rate = clk_get_rate(tdm->l1rsync_clk);
			serial_info->rx_bit_rate = clk_get_rate(tdm->l1rclk_clk);
			serial_info->nb_rx_ts += count;
		} else {
			serial_info->tx_fs_rate = tdm->l1tsync_clk ?
				clk_get_rate(tdm->l1tsync_clk) :
				clk_get_rate(tdm->l1rsync_clk);
			serial_info->tx_bit_rate = tdm->l1tclk_clk ?
				clk_get_rate(tdm->l1tclk_clk) :
				clk_get_rate(tdm->l1rclk_clk);
			serial_info->nb_tx_ts += count;
		}
	}
	return 0;
}

static inline int tsa_of_parse_tdm_rx_route(struct tsa *tsa,
					    struct device_node *tdm_np,
					    u32 tdms, u32 tdm_id)
{
	return tsa_of_parse_tdm_route(tsa, tdm_np, tdms, tdm_id, true);
}

static inline int tsa_of_parse_tdm_tx_route(struct tsa *tsa,
					    struct device_node *tdm_np,
					    u32 tdms, u32 tdm_id)
{
	return tsa_of_parse_tdm_route(tsa, tdm_np, tdms, tdm_id, false);
}

static int tsa_of_parse_tdms(struct tsa *tsa, struct device_node *np)
{
	struct device_node *tdm_np;
	struct tsa_tdm *tdm;
	struct clk *clk;
	u32 tdm_id, val;
	int ret;
	int i;

	tsa->tdms = 0;
	tsa->tdm[0].is_enable = false;
	tsa->tdm[1].is_enable = false;

	for_each_available_child_of_node(np, tdm_np) {
		ret = of_property_read_u32(tdm_np, "reg", &tdm_id);
		if (ret) {
			dev_err(tsa->dev, "%pOF: failed to read reg\n", tdm_np);
			of_node_put(tdm_np);
			return ret;
		}
		switch (tdm_id) {
		case 0:
			tsa->tdms |= BIT(TSA_TDMA);
			break;
		case 1:
			tsa->tdms |= BIT(TSA_TDMB);
			break;
		default:
			dev_err(tsa->dev, "%pOF: Invalid tdm_id (%u)\n", tdm_np,
				tdm_id);
			of_node_put(tdm_np);
			return -EINVAL;
		}
	}

	for_each_available_child_of_node(np, tdm_np) {
		ret = of_property_read_u32(tdm_np, "reg", &tdm_id);
		if (ret) {
			dev_err(tsa->dev, "%pOF: failed to read reg\n", tdm_np);
			of_node_put(tdm_np);
			return ret;
		}

		tdm = &tsa->tdm[tdm_id];
		tdm->simode_tdm = TSA_SIMODE_TDM_SDM_NORM;

		val = 0;
		ret = of_property_read_u32(tdm_np, "fsl,rx-frame-sync-delay-bits",
					   &val);
		if (ret && ret != -EINVAL) {
			dev_err(tsa->dev,
				"%pOF: failed to read fsl,rx-frame-sync-delay-bits\n",
				tdm_np);
			of_node_put(tdm_np);
			return ret;
		}
		if (val > 3) {
			dev_err(tsa->dev,
				"%pOF: Invalid fsl,rx-frame-sync-delay-bits (%u)\n",
				tdm_np, val);
			of_node_put(tdm_np);
			return -EINVAL;
		}
		tdm->simode_tdm |= TSA_SIMODE_TDM_RFSD(val);

		val = 0;
		ret = of_property_read_u32(tdm_np, "fsl,tx-frame-sync-delay-bits",
					   &val);
		if (ret && ret != -EINVAL) {
			dev_err(tsa->dev,
				"%pOF: failed to read fsl,tx-frame-sync-delay-bits\n",
				tdm_np);
			of_node_put(tdm_np);
			return ret;
		}
		if (val > 3) {
			dev_err(tsa->dev,
				"%pOF: Invalid fsl,tx-frame-sync-delay-bits (%u)\n",
				tdm_np, val);
			of_node_put(tdm_np);
			return -EINVAL;
		}
		tdm->simode_tdm |= TSA_SIMODE_TDM_TFSD(val);

		if (of_property_read_bool(tdm_np, "fsl,common-rxtx-pins"))
			tdm->simode_tdm |= TSA_SIMODE_TDM_CRT;

		if (of_property_read_bool(tdm_np, "fsl,clock-falling-edge"))
			tdm->simode_tdm |= TSA_SIMODE_TDM_CE;

		if (of_property_read_bool(tdm_np, "fsl,fsync-rising-edge"))
			tdm->simode_tdm |= TSA_SIMODE_TDM_FE;

		if (of_property_read_bool(tdm_np, "fsl,double-speed-clock"))
			tdm->simode_tdm |= TSA_SIMODE_TDM_DSC;

		clk = of_clk_get_by_name(tdm_np, "l1rsync");
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			of_node_put(tdm_np);
			goto err;
		}
		ret = clk_prepare_enable(clk);
		if (ret) {
			clk_put(clk);
			of_node_put(tdm_np);
			goto err;
		}
		tdm->l1rsync_clk = clk;

		clk = of_clk_get_by_name(tdm_np, "l1rclk");
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			of_node_put(tdm_np);
			goto err;
		}
		ret = clk_prepare_enable(clk);
		if (ret) {
			clk_put(clk);
			of_node_put(tdm_np);
			goto err;
		}
		tdm->l1rclk_clk = clk;

		if (!(tdm->simode_tdm & TSA_SIMODE_TDM_CRT)) {
			clk = of_clk_get_by_name(tdm_np, "l1tsync");
			if (IS_ERR(clk)) {
				ret = PTR_ERR(clk);
				of_node_put(tdm_np);
				goto err;
			}
			ret = clk_prepare_enable(clk);
			if (ret) {
				clk_put(clk);
				of_node_put(tdm_np);
				goto err;
			}
			tdm->l1tsync_clk = clk;

			clk = of_clk_get_by_name(tdm_np, "l1tclk");
			if (IS_ERR(clk)) {
				ret = PTR_ERR(clk);
				of_node_put(tdm_np);
				goto err;
			}
			ret = clk_prepare_enable(clk);
			if (ret) {
				clk_put(clk);
				of_node_put(tdm_np);
				goto err;
			}
			tdm->l1tclk_clk = clk;
		}

		ret = tsa_of_parse_tdm_rx_route(tsa, tdm_np, tsa->tdms, tdm_id);
		if (ret) {
			of_node_put(tdm_np);
			goto err;
		}

		ret = tsa_of_parse_tdm_tx_route(tsa, tdm_np, tsa->tdms, tdm_id);
		if (ret) {
			of_node_put(tdm_np);
			goto err;
		}

		tdm->is_enable = true;
	}
	return 0;

err:
	for (i = 0; i < 2; i++) {
		if (tsa->tdm[i].l1rsync_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rsync_clk);
			clk_put(tsa->tdm[i].l1rsync_clk);
		}
		if (tsa->tdm[i].l1rclk_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rclk_clk);
			clk_put(tsa->tdm[i].l1rclk_clk);
		}
		if (tsa->tdm[i].l1tsync_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rsync_clk);
			clk_put(tsa->tdm[i].l1rsync_clk);
		}
		if (tsa->tdm[i].l1tclk_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rclk_clk);
			clk_put(tsa->tdm[i].l1rclk_clk);
		}
	}
	return ret;
}

static void tsa_init_si_ram(struct tsa *tsa)
{
	resource_size_t i;

	/* Fill all entries as the last one */
	for (i = 0; i < tsa->si_ram_sz; i += 4)
		tsa_write32(tsa->si_ram + i, TSA_SIRAM_ENTRY_LAST);
}

static int tsa_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct tsa *tsa;
	unsigned int i;
	u32 val;
	int ret;

	tsa = devm_kzalloc(&pdev->dev, sizeof(*tsa), GFP_KERNEL);
	if (!tsa)
		return -ENOMEM;

	tsa->dev = &pdev->dev;

	for (i = 0; i < ARRAY_SIZE(tsa->serials); i++)
		tsa->serials[i].id = i;

	spin_lock_init(&tsa->lock);

	tsa->si_regs = devm_platform_ioremap_resource_byname(pdev, "si_regs");
	if (IS_ERR(tsa->si_regs))
		return PTR_ERR(tsa->si_regs);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "si_ram");
	if (!res) {
		dev_err(tsa->dev, "si_ram resource missing\n");
		return -EINVAL;
	}
	tsa->si_ram_sz = resource_size(res);
	tsa->si_ram = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tsa->si_ram))
		return PTR_ERR(tsa->si_ram);

	tsa_init_si_ram(tsa);

	ret = tsa_of_parse_tdms(tsa, np);
	if (ret)
		return ret;

	/* Set SIMODE */
	val = 0;
	if (tsa->tdm[0].is_enable)
		val |= TSA_SIMODE_TDMA(tsa->tdm[0].simode_tdm);
	if (tsa->tdm[1].is_enable)
		val |= TSA_SIMODE_TDMB(tsa->tdm[1].simode_tdm);

	tsa_clrsetbits32(tsa->si_regs + TSA_SIMODE,
			 TSA_SIMODE_TDMA(TSA_SIMODE_TDM_MASK) |
			 TSA_SIMODE_TDMB(TSA_SIMODE_TDM_MASK),
			 val);

	/* Set SIGMR */
	val = (tsa->tdms == BIT(TSA_TDMA)) ?
		TSA_SIGMR_RDM_STATIC_TDMA : TSA_SIGMR_RDM_STATIC_TDMAB;
	if (tsa->tdms & BIT(TSA_TDMA))
		val |= TSA_SIGMR_ENA;
	if (tsa->tdms & BIT(TSA_TDMB))
		val |= TSA_SIGMR_ENB;
	tsa_write8(tsa->si_regs + TSA_SIGMR, val);

	platform_set_drvdata(pdev, tsa);

	return 0;
}

static void tsa_remove(struct platform_device *pdev)
{
	struct tsa *tsa = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < 2; i++) {
		if (tsa->tdm[i].l1rsync_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rsync_clk);
			clk_put(tsa->tdm[i].l1rsync_clk);
		}
		if (tsa->tdm[i].l1rclk_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rclk_clk);
			clk_put(tsa->tdm[i].l1rclk_clk);
		}
		if (tsa->tdm[i].l1tsync_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rsync_clk);
			clk_put(tsa->tdm[i].l1rsync_clk);
		}
		if (tsa->tdm[i].l1tclk_clk) {
			clk_disable_unprepare(tsa->tdm[i].l1rclk_clk);
			clk_put(tsa->tdm[i].l1rclk_clk);
		}
	}
}

static const struct of_device_id tsa_id_table[] = {
	{ .compatible = "fsl,cpm1-tsa" },
	{} /* sentinel */
};
MODULE_DEVICE_TABLE(of, tsa_id_table);

static struct platform_driver tsa_driver = {
	.driver = {
		.name = "fsl-tsa",
		.of_match_table = of_match_ptr(tsa_id_table),
	},
	.probe = tsa_probe,
	.remove_new = tsa_remove,
};
module_platform_driver(tsa_driver);

struct tsa_serial *tsa_serial_get_byphandle(struct device_node *np,
					    const char *phandle_name)
{
	struct of_phandle_args out_args;
	struct platform_device *pdev;
	struct tsa_serial *tsa_serial;
	struct tsa *tsa;
	int ret;

	ret = of_parse_phandle_with_fixed_args(np, phandle_name, 1, 0, &out_args);
	if (ret < 0)
		return ERR_PTR(ret);

	if (!of_match_node(tsa_driver.driver.of_match_table, out_args.np)) {
		of_node_put(out_args.np);
		return ERR_PTR(-EINVAL);
	}

	pdev = of_find_device_by_node(out_args.np);
	of_node_put(out_args.np);
	if (!pdev)
		return ERR_PTR(-ENODEV);

	tsa = platform_get_drvdata(pdev);
	if (!tsa) {
		platform_device_put(pdev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	if (out_args.args_count != 1) {
		platform_device_put(pdev);
		return ERR_PTR(-EINVAL);
	}

	if (out_args.args[0] >= ARRAY_SIZE(tsa->serials)) {
		platform_device_put(pdev);
		return ERR_PTR(-EINVAL);
	}

	tsa_serial = &tsa->serials[out_args.args[0]];

	/*
	 * Be sure that the serial id matches the phandle arg.
	 * The tsa_serials table is indexed by serial ids. The serial id is set
	 * during the probe() call and needs to be coherent.
	 */
	if (WARN_ON(tsa_serial->id != out_args.args[0])) {
		platform_device_put(pdev);
		return ERR_PTR(-EINVAL);
	}

	return tsa_serial;
}
EXPORT_SYMBOL(tsa_serial_get_byphandle);

void tsa_serial_put(struct tsa_serial *tsa_serial)
{
	struct tsa *tsa = tsa_serial_get_tsa(tsa_serial);

	put_device(tsa->dev);
}
EXPORT_SYMBOL(tsa_serial_put);

static void devm_tsa_serial_release(struct device *dev, void *res)
{
	struct tsa_serial **tsa_serial = res;

	tsa_serial_put(*tsa_serial);
}

struct tsa_serial *devm_tsa_serial_get_byphandle(struct device *dev,
						 struct device_node *np,
						 const char *phandle_name)
{
	struct tsa_serial *tsa_serial;
	struct tsa_serial **dr;

	dr = devres_alloc(devm_tsa_serial_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	tsa_serial = tsa_serial_get_byphandle(np, phandle_name);
	if (!IS_ERR(tsa_serial)) {
		*dr = tsa_serial;
		devres_add(dev, dr);
	} else {
		devres_free(dr);
	}

	return tsa_serial;
}
EXPORT_SYMBOL(devm_tsa_serial_get_byphandle);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("CPM TSA driver");
MODULE_LICENSE("GPL");
