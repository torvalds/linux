// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/suspend.h>
#include <soc/qcom/memory_dump.h>
#include <soc/qcom/minidump.h>
#include <dt-bindings/soc/qcom,dcc_v2.h>

#define TIMEOUT_US		(100)

#define BM(lsb, msb)		((BIT(msb) - BIT(lsb)) + BIT(msb))
#define BMVAL(val, lsb, msb)	((val & BM(lsb, msb)) >> lsb)
#define BVAL(val, n)		((val & BIT(n)) >> n)

#define dcc_writel(drvdata, val, off)					\
	__raw_writel((val), drvdata->base + dcc_offset_conv(drvdata, off))
#define dcc_readl(drvdata, off)						\
	__raw_readl(drvdata->base + dcc_offset_conv(drvdata, off))

#define dcc_sram_readl(drvdata, off)					\
	__raw_readl(drvdata->ram_base + off)

#define HLOS_LIST_START			0

/* DCC registers */
#define DCC_HW_VERSION			(0x00)
#define DCC_HW_INFO			(0x04)
#define DCC_SRAM_SIZE_INFO		(0x08)
#define DCC_APU_INFO			(0x0C)
#define DCC_LL_NUM_INFO			(0x10)
#define DCC_TIMEOUT_SIGNATURE		(0x14)
#define DCC_EXEC_CTRL			(0x18)
#define DCC_STATUS			(0x1C)
#define DCC_CFG				(0x20)
#define DCC_FDA_CURR			(0x24)
#define DCC_LLA_CURR			(0x28)
#define DCC_QAD_VALUE			(0x2C)
#define DCC_LL_TO_CNTR_VAL		(0x30)
#define DCC_LL_LOCK(m)			(0x34 + 0x80 * (m + HLOS_LIST_START))
#define DCC_LL_CFG(m)			(0x38 + 0x80 * (m + HLOS_LIST_START))
#define DCC_LL_BASE(m)			(0x3c + 0x80 * (m + HLOS_LIST_START))
#define DCC_FD_BASE(m)			(0x40 + 0x80 * (m + HLOS_LIST_START))
#define DCC_LL_TIMEOUT(m)		(0x44 + 0x80 * (m + HLOS_LIST_START))
#define DCC_TRANS_TIMEOUT(m)		(0x48 + 0x80 * (m + HLOS_LIST_START))
#define DCC_LL_INT_ENABLE(m)		(0x4C + 0x80 * (m + HLOS_LIST_START))
#define DCC_LL_INT_STATUS(m)		(0x50 + 0x80 * (m + HLOS_LIST_START))
#define DCC_FDA_CAPTURED(m)		(0x54 + 0x80 * (m + HLOS_LIST_START))
#define DCC_LLA_CAPTURED(m)		(0x58 + 0x80 * (m + HLOS_LIST_START))
#define DCC_LL_CRC_CAPTURED(m)		(0x5C + 0x80 * (m + HLOS_LIST_START))
#define DCC_LL_SW_TRIGGER(m)		(0x60 + 0x80 * (m + HLOS_LIST_START))
#define DCC_LL_BUS_ACCESS_STATUS(m)	(0x64 + 0x80 * (m + HLOS_LIST_START))
#define DCC_CTI_TRIG(m)			(0x68 + 0x80 * (m + HLOS_LIST_START))
#define DCC_QAD_OUTPUT(m)		(0x6C + 0x80 * (m + HLOS_LIST_START))

#define DCC_MAP_LEVEL1			(0x18)
#define DCC_MAP_LEVEL2			(0x34)
#define DCC_MAP_LEVEL3			(0x4C)

#define DCC_MAP_OFFSET1			(0x10)
#define DCC_MAP_OFFSET2			(0x18)
#define DCC_MAP_OFFSET3			(0x1C)
#define DCC_MAP_OFFSET4			(0x8)

#define DCC_FIX_LOOP_OFFSET		(16)

#define DCC_REG_DUMP_MAGIC_V2		(0x42445953)
#define DCC_REG_DUMP_VER		(1)

#define MAX_DCC_OFFSET		(0xFF * 4)
#define MAX_DCC_LEN		0x7F
#define MAX_LOOP_CNT		0xFF

#define DCC_ADDR_DESCRIPTOR		0x00
#define DCC_LOOP_DESCRIPTOR		(BIT(30))
#define DCC_RD_MOD_WR_DESCRIPTOR	(BIT(31))
#define DCC_LINK_DESCRIPTOR		(BIT(31) | BIT(30))

#define DCC_READ_IND			0x00
#define DCC_WRITE_IND			(BIT(28))

#define DCC_AHB_IND			0x00
#define DCC_APB_IND			BIT(29)

#define DCC_MAX_LINK_LIST		8
#define DCC_INVALID_LINK_LIST		0xFF

#define DEFAULT_TRANSACTION_TIMEOUT			0x3F

enum dcc_func_type {
	DCC_FUNC_TYPE_CAPTURE,
	DCC_FUNC_TYPE_CRC,
};

static const char * const str_dcc_func_type[] = {
	[DCC_FUNC_TYPE_CAPTURE]		= "cap",
	[DCC_FUNC_TYPE_CRC]		= "crc",
};

enum dcc_data_sink {
	DCC_DATA_SINK_SRAM,
	DCC_DATA_SINK_ATB
};

enum dcc_descriptor_type {
	DCC_ADDR_TYPE,
	DCC_LOOP_TYPE,
	DCC_READ_WRITE_TYPE,
	DCC_WRITE_TYPE
};

enum dcc_mem_map_ver {
	DCC_MEM_MAP_VER1,
	DCC_MEM_MAP_VER2,
	DCC_MEM_MAP_VER3
};

static const char * const str_dcc_data_sink[] = {
	[DCC_DATA_SINK_SRAM]		= "sram",
	[DCC_DATA_SINK_ATB]		= "atb",
};

struct rpm_trig_req {
	uint32_t    enable;
	uint32_t    reserved;
};

struct dcc_config_entry {
	uint32_t			base;
	uint32_t			offset;
	uint32_t			len;
	uint32_t			index;
	uint32_t			loop_cnt;
	uint32_t			write_val;
	uint32_t			mask;
	bool				apb_bus;
	enum dcc_descriptor_type	desc_type;
	struct list_head		list;
};

/*
 * struct reg_state
 * offset: the offset of the reg to be preserved when dcc is without power
 * val   : the val of the reg to be preserved when dcc is without power
 */
struct reg_state {
	uint32_t		offset;
	uint32_t		val;
};

struct dcc_drvdata {
	void __iomem		*base;
	uint32_t		reg_size;
	struct device		*dev;
	struct mutex		mutex;
	void __iomem		*ram_base;
	uint32_t		ram_size;
	uint32_t		ram_offset;
	enum dcc_data_sink	*data_sink;
	enum dcc_func_type	*func_type;
	enum dcc_mem_map_ver	mem_map_ver;
	uint32_t		ram_cfg;
	uint32_t		ram_start;
	bool			*enable;
	bool			*hw_trig;
	bool			*sw_trig;
	bool			*configured;
	bool			interrupt_disable;
	char			*sram_node;
	struct cdev		sram_dev;
	struct class		*sram_class;
	struct list_head	*cfg_head;
	uint32_t		*nr_config;
	uint32_t		nr_link_list;
	uint8_t			curr_list;
	uint8_t			*cti_trig;
	uint8_t			loopoff;
	uint32_t		ram_cpy_len;
	uint32_t		per_ll_reg_cnt;
	int32_t			ll_state_cnt;
	struct reg_state	*ll_state;
	void			*sram_state;
	uint8_t			*qad_output;
};

static uint32_t dcc_offset_conv(struct dcc_drvdata *drvdata, uint32_t off)
{
	if (drvdata->mem_map_ver == DCC_MEM_MAP_VER1) {
		if ((off & 0x7F) >= DCC_MAP_LEVEL3)
			return (off - DCC_MAP_OFFSET3);
		if ((off & 0x7F) >= DCC_MAP_LEVEL2)
			return (off - DCC_MAP_OFFSET2);
		else if ((off & 0x7F) >= DCC_MAP_LEVEL1)
			return (off - DCC_MAP_OFFSET1);
	} else if (drvdata->mem_map_ver == DCC_MEM_MAP_VER2) {
		if ((off & 0x7F) >= DCC_MAP_LEVEL2)
			return (off - DCC_MAP_OFFSET4);
	}
	return (off);
}

static int dcc_sram_writel(struct dcc_drvdata *drvdata,
					uint32_t val, uint32_t off)
{
	if (unlikely(off > (drvdata->ram_size - 4)))
		return -EINVAL;

	__raw_writel((val), drvdata->ram_base + off);

	return 0;
}

static int dcc_sram_memcpy(void *to, const void __iomem *from,
							size_t count)
{
	if (!count || (!IS_ALIGNED((unsigned long)from, 4) ||
			!IS_ALIGNED((unsigned long)to, 4) ||
			!IS_ALIGNED((unsigned long)count, 4))) {
		return -EINVAL;
	}

	while (count >= 4) {
		*(unsigned int *)to = __raw_readl(from);
		to += 4;
		from += 4;
		count -= 4;
	}

	return 0;
}

static bool dcc_ready(struct dcc_drvdata *drvdata)
{
	uint32_t val;

	/* poll until DCC ready */
	if (!readl_poll_timeout((drvdata->base + DCC_STATUS), val,
				(BMVAL(val, 0, 1) == 0), 1, TIMEOUT_US))
		return true;

	return false;
}

static int dcc_read_status(struct dcc_drvdata *drvdata)
{
	int curr_list;
	uint32_t bus_status;
	uint32_t ll_cfg = 0;
	uint32_t tmp_ll_cfg = 0;

	for (curr_list = 0; curr_list < drvdata->nr_link_list; curr_list++) {
		if (!drvdata->enable[curr_list])
			continue;

		bus_status = dcc_readl(drvdata,
				       DCC_LL_BUS_ACCESS_STATUS(curr_list));

		if (bus_status) {

			dev_err(drvdata->dev,
				"Read access error for list %d err: 0x%x.\n",
				curr_list, bus_status);

			ll_cfg = dcc_readl(drvdata, DCC_LL_CFG(curr_list));
			if (drvdata->mem_map_ver == DCC_MEM_MAP_VER3)
				tmp_ll_cfg = ll_cfg & ~BIT(8);
			else
				tmp_ll_cfg = ll_cfg & ~BIT(9);
			dcc_writel(drvdata, tmp_ll_cfg, DCC_LL_CFG(curr_list));
			dcc_writel(drvdata, 0x3,
				   DCC_LL_BUS_ACCESS_STATUS(curr_list));
			dcc_writel(drvdata, ll_cfg, DCC_LL_CFG(curr_list));
			return -ENODATA;
		}
	}

	return 0;
}

static int dcc_sw_trigger(struct dcc_drvdata *drvdata)
{
	int ret = 0;
	int curr_list;
	uint32_t ll_cfg = 0;
	uint32_t tmp_ll_cfg = 0;

	mutex_lock(&drvdata->mutex);

	for (curr_list = 0; curr_list < drvdata->nr_link_list; curr_list++) {
		if ((!drvdata->enable[curr_list]) || (!drvdata->sw_trig[curr_list]))
			continue;
		dev_info(drvdata->dev, "DCC SW trigger link list %d\n", curr_list);
		ll_cfg = dcc_readl(drvdata, DCC_LL_CFG(curr_list));
		if (drvdata->mem_map_ver == DCC_MEM_MAP_VER3)
			tmp_ll_cfg = ll_cfg & ~BIT(8);
		else
			tmp_ll_cfg = ll_cfg & ~BIT(9);
		dcc_writel(drvdata, tmp_ll_cfg, DCC_LL_CFG(curr_list));
		dcc_writel(drvdata, 1, DCC_LL_SW_TRIGGER(curr_list));
		dcc_writel(drvdata, ll_cfg, DCC_LL_CFG(curr_list));
	}

	if (!dcc_ready(drvdata)) {
		dev_err(drvdata->dev,
			"DCC is busy after receiving sw tigger.\n");
		ret = -EBUSY;
		goto err;
	}

	ret = dcc_read_status(drvdata);

err:
	mutex_unlock(&drvdata->mutex);
	return ret;
}

static int __dcc_ll_cfg(struct dcc_drvdata *drvdata, int curr_list)
{
	int ret = 0;
	uint32_t sram_offset = drvdata->ram_cfg * 4;
	uint32_t prev_addr, addr;
	uint32_t prev_off = 0, off;
	uint32_t loop_off = 0;
	uint32_t link;
	uint32_t pos, total_len = 0, loop_len = 0;
	uint32_t loop, loop_cnt = 0;
	bool loop_start = false;
	struct dcc_config_entry *entry;

	prev_addr = 0;
	addr = 0;
	link = 0;

	list_for_each_entry(entry, &drvdata->cfg_head[curr_list], list) {
		switch (entry->desc_type) {
		case DCC_READ_WRITE_TYPE:
		{
			if (link) {
				/* write new offset = 1 to continue
				 * processing the list
				 */
				ret = dcc_sram_writel(drvdata,
							link, sram_offset);
				if (ret)
					goto overstep;
				sram_offset += 4;
				/* Reset link and prev_off */
				addr = 0x00;
				link = 0;
				prev_off = 0;
				prev_addr = addr;
			}

			addr = DCC_RD_MOD_WR_DESCRIPTOR;
			ret = dcc_sram_writel(drvdata, addr, sram_offset);
			if (ret)
				goto overstep;
			sram_offset += 4;

			ret = dcc_sram_writel(drvdata,
					entry->mask, sram_offset);
			if (ret)
				goto overstep;
			sram_offset += 4;

			ret = dcc_sram_writel(drvdata,
					entry->write_val, sram_offset);
			if (ret)
				goto overstep;
			sram_offset += 4;
			addr = 0;
			break;
		}

		case DCC_LOOP_TYPE:
		{
			/* Check if we need to write link of prev entry */
			if (link) {
				ret = dcc_sram_writel(drvdata,
						link, sram_offset);
				if (ret)
					goto overstep;
				sram_offset += 4;
			}

			if (loop_start) {
				loop = (sram_offset - loop_off) / 4;
				loop |= (loop_cnt << drvdata->loopoff) &
					BM(drvdata->loopoff, 27);
				loop |= DCC_LOOP_DESCRIPTOR;
				total_len += (total_len - loop_len) * loop_cnt;

				ret = dcc_sram_writel(drvdata,
						loop, sram_offset);
				if (ret)
					goto overstep;
				sram_offset += 4;

				loop_start = false;
				loop_len = 0;
				loop_off = 0;
			} else {
				loop_start = true;
				loop_cnt = entry->loop_cnt - 1;
				loop_len = total_len;
				loop_off = sram_offset;
			}

			/* Reset link and prev_off */
			addr = 0x00;
			link = 0;
			prev_off = 0;
			prev_addr = addr;

			break;
		}

		case DCC_WRITE_TYPE:
		{
			if (link) {
				/* write new offset = 1 to continue
				 * processing the list
				 */
				ret = dcc_sram_writel(drvdata,
						link, sram_offset);
				if (ret)
					goto overstep;
				sram_offset += 4;
				/* Reset link and prev_off */
				addr = 0x00;
				prev_off = 0;
				prev_addr = addr;
			}

			off = entry->offset/4;
			/* write new offset-length pair to correct position */
			link |= ((off & BM(0, 7)) | BIT(15) |
				 ((entry->len << 8) & BM(8, 14)));
			link |= DCC_LINK_DESCRIPTOR;

			/* Address type */
			addr = (entry->base >> 4) & BM(0, 27);
			if (entry->apb_bus)
				addr |= DCC_ADDR_DESCRIPTOR | DCC_WRITE_IND
					| DCC_APB_IND;
			else
				addr |= DCC_ADDR_DESCRIPTOR | DCC_WRITE_IND
					| DCC_AHB_IND;

			ret = dcc_sram_writel(drvdata, addr, sram_offset);
			if (ret)
				goto overstep;
			sram_offset += 4;

			ret = dcc_sram_writel(drvdata, link, sram_offset);
			if (ret)
				goto overstep;
			sram_offset += 4;

			ret = dcc_sram_writel(drvdata,
				entry->write_val, sram_offset);
			if (ret)
				goto overstep;
			sram_offset += 4;
			addr = 0x00;
			link = 0;
			break;
		}

		default:
		{
			/* Address type */
			addr = (entry->base >> 4) & BM(0, 27);
			if (entry->apb_bus)
				addr |= DCC_ADDR_DESCRIPTOR | DCC_READ_IND
					| DCC_APB_IND;
			else
				addr |= DCC_ADDR_DESCRIPTOR | DCC_READ_IND
					| DCC_AHB_IND;

			off = entry->offset/4;
			total_len += entry->len * 4;

			if (!prev_addr || prev_addr != addr || prev_off > off) {
				/* Check if we need to write prev link entry */
				if (link) {
					ret = dcc_sram_writel(drvdata,
							link, sram_offset);
					if (ret)
						goto overstep;
					sram_offset += 4;
				}
				dev_dbg(drvdata->dev,
					"DCC: sram address 0x%x\n",
					sram_offset);

				/* Write address */
				ret = dcc_sram_writel(drvdata,
						addr, sram_offset);
				if (ret)
					goto overstep;
				sram_offset += 4;

				/* Reset link and prev_off */
				link = 0;
				prev_off = 0;
			}

			if ((off - prev_off) > 0xFF ||
			    entry->len > MAX_DCC_LEN) {
				dev_err(drvdata->dev,
					"DCC: Programming error Base: 0x%x, offset 0x%x\n",
					entry->base, entry->offset);
				ret = -EINVAL;
				goto err;
			}

			if (link) {
				/*
				 * link already has one offset-length so new
				 * offset-length needs to be placed at
				 * bits [29:15]
				 */
				pos = 15;

				/* Clear bits [31:16] */
				link &= BM(0, 14);
			} else {
				/*
				 * link is empty, so new offset-length needs
				 * to be placed at bits [15:0]
				 */
				pos = 0;
				link = 1 << 15;
			}

			/* write new offset-length pair to correct position */
			link |= (((off-prev_off) & BM(0, 7)) |
				 ((entry->len << 8) & BM(8, 14))) << pos;

			link |= DCC_LINK_DESCRIPTOR;

			if (pos) {
				ret = dcc_sram_writel(drvdata,
						link, sram_offset);
				if (ret)
					goto overstep;
				sram_offset += 4;
				link = 0;
			}

			prev_off  = off + entry->len - 1;
			prev_addr = addr;
			}
		}
	}

	if (link) {
		ret = dcc_sram_writel(drvdata, link, sram_offset);
		if (ret)
			goto overstep;
		sram_offset += 4;
	}

	if (loop_start) {
		dev_err(drvdata->dev,
			"DCC: Programming error: Loop unterminated\n");
		ret = -EINVAL;
		goto err;
	}

	/* Handling special case of list ending with a rd_mod_wr */
	if (addr == DCC_RD_MOD_WR_DESCRIPTOR) {
		addr = (0xC105E) & BM(0, 27);
		addr |= DCC_ADDR_DESCRIPTOR;

		ret = dcc_sram_writel(drvdata, addr, sram_offset);
		if (ret)
			goto overstep;
		sram_offset += 4;
	}

	/* Setting zero to indicate end of the list */
	link = DCC_LINK_DESCRIPTOR;
	ret = dcc_sram_writel(drvdata, link, sram_offset);
	if (ret)
		goto overstep;
	sram_offset += 4;

	/* Update ram_cfg and check if the data will overstep */
	if (drvdata->data_sink[curr_list] == DCC_DATA_SINK_SRAM &&
	    drvdata->func_type[curr_list] == DCC_FUNC_TYPE_CAPTURE) {
		drvdata->ram_cfg = (sram_offset + total_len) / 4;

		if (sram_offset + total_len > drvdata->ram_size) {
			sram_offset += total_len;
			goto overstep;
		}
	} else {
		drvdata->ram_cfg = sram_offset / 4;

		if (sram_offset > drvdata->ram_size)
			goto overstep;
	}

	drvdata->ram_start = sram_offset/4;
	return 0;
overstep:
	ret = -EINVAL;
	memset_io(drvdata->ram_base, 0, drvdata->ram_size);
	dev_err(drvdata->dev, "DCC SRAM oversteps, 0x%x (0x%x)\n",
		sram_offset, drvdata->ram_size);
err:
	return ret;
}

static void __dcc_first_crc(struct dcc_drvdata *drvdata)
{
	int i;

	/*
	 * Need to send 2 triggers to DCC. First trigger sets CRC error status
	 * bit. So need second trigger to reset this bit.
	 */
	for (i = 0; i < 2; i++) {
		if (!dcc_ready(drvdata))
			dev_err(drvdata->dev, "DCC is not ready\n");

		dcc_writel(drvdata, 1,
			   DCC_LL_SW_TRIGGER(drvdata->curr_list));
	}

	/* Clear CRC error interrupt */
	dcc_writel(drvdata, BIT(1),
		   DCC_LL_INT_STATUS(drvdata->curr_list));
}

static int dcc_valid_list(struct dcc_drvdata *drvdata, int curr_list)
{
	uint32_t lock_reg;

	if (list_empty(&drvdata->cfg_head[curr_list]))
		return -EINVAL;

	if (drvdata->enable[curr_list]) {
		dev_err(drvdata->dev, "List %d is already enabled\n",
				curr_list);
		return -EINVAL;
	}

	lock_reg = dcc_readl(drvdata, DCC_LL_LOCK(curr_list));
	if (lock_reg & 0x1) {
		dev_err(drvdata->dev, "List %d is already locked\n",
				curr_list);
		return -EINVAL;
	}

	dev_info(drvdata->dev, "DCC list passed %d\n", curr_list);
	return 0;
}

static bool is_dcc_enabled(struct dcc_drvdata *drvdata)
{
	bool dcc_enable = false;
	int list;

	for (list = 0; list < drvdata->nr_link_list; list++) {
		if (drvdata->enable[list]) {
			dcc_enable = true;
			break;
		}
	}

	return dcc_enable;
}

static int dcc_enable(struct dcc_drvdata *drvdata)
{
	int ret = 0;
	int list;
	uint32_t ram_cfg_base;
	uint32_t hw_info;
	uint32_t transaction_timeout;
	struct device_node *node = drvdata->dev->of_node;

	mutex_lock(&drvdata->mutex);

	if (!is_dcc_enabled(drvdata))
		memset_io(drvdata->ram_base, 0xDE, drvdata->ram_size);

	for (list = 0; list < drvdata->nr_link_list; list++) {

		if (dcc_valid_list(drvdata, list))
			continue;

		/* 1. Take ownership of the list */
		dcc_writel(drvdata, BIT(0), DCC_LL_LOCK(list));

		/* 2. Program linked-list in the SRAM */
		ram_cfg_base = drvdata->ram_cfg;
		ret = __dcc_ll_cfg(drvdata, list);
		if (ret) {
			dcc_writel(drvdata, 0, DCC_LL_LOCK(list));
			dev_info(drvdata->dev, "DCC ram programming failed\n");
			goto err;
		}

		/* 3. program DCC_RAM_CFG reg */
		dcc_writel(drvdata, ram_cfg_base +
			   drvdata->ram_offset/4, DCC_LL_BASE(list));
		dcc_writel(drvdata, drvdata->ram_start +
				drvdata->ram_offset/4, DCC_FD_BASE(list));
		dcc_writel(drvdata, 0xFFF, DCC_LL_TIMEOUT(list));

		hw_info = dcc_readl(drvdata, DCC_HW_INFO);
		if (hw_info & 0x80) {
			ret = of_property_read_u32(node,
						"qcom,transaction_timeout",
						&transaction_timeout);
			if (ret)
				transaction_timeout =
						DEFAULT_TRANSACTION_TIMEOUT;
			if (transaction_timeout)
				dcc_writel(drvdata, transaction_timeout,
						DCC_TRANS_TIMEOUT(list));
		}

		/* 4. Clears interrupt status register */
		dcc_writel(drvdata, 0, DCC_LL_INT_ENABLE(list));
		dcc_writel(drvdata, (BIT(0) | BIT(1) | BIT(2)),
			   DCC_LL_INT_STATUS(list));

		dev_info(drvdata->dev, "All values written to enable.\n");
		/* Make sure all config is written in sram */
		mb();

		drvdata->enable[list] = true;

		if (drvdata->func_type[list] == DCC_FUNC_TYPE_CRC) {
			__dcc_first_crc(drvdata);

			/* Enable CRC error interrupt */
			if (!drvdata->interrupt_disable)
				dcc_writel(drvdata, BIT(1),
					   DCC_LL_INT_ENABLE(list));
		}

		/* 5. Configure trigger */
		if (drvdata->mem_map_ver == DCC_MEM_MAP_VER3) {
			dcc_writel(drvdata, drvdata->qad_output[list],
					DCC_QAD_OUTPUT(list));
			dcc_writel(drvdata, drvdata->cti_trig[list],
					DCC_CTI_TRIG(list));
			if (drvdata->hw_trig[list])
				dcc_writel(drvdata, BIT(8) | ((drvdata->data_sink[list] << 4) |
					   (drvdata->func_type[list])), DCC_LL_CFG(list));
			else
				dcc_writel(drvdata, ~BIT(8) & ((drvdata->data_sink[list] << 4) |
					   (drvdata->func_type[list])), DCC_LL_CFG(list));
		} else {
			if (drvdata->hw_trig[list])
				dcc_writel(drvdata, BIT(9) | ((drvdata->cti_trig[list] << 8) |
					   (drvdata->data_sink[list] << 4) |
					   (drvdata->func_type[list])), DCC_LL_CFG(list));
			else
				dcc_writel(drvdata, ~BIT(9) & ((drvdata->cti_trig[list] << 8) |
					   (drvdata->data_sink[list] << 4) |
					   (drvdata->func_type[list])), DCC_LL_CFG(list));
		}
	}

	drvdata->ram_cpy_len = drvdata->ram_cfg * 4;
err:
	mutex_unlock(&drvdata->mutex);
	return ret;
}

static void dcc_disable(struct dcc_drvdata *drvdata)
{
	int curr_list;

	mutex_lock(&drvdata->mutex);

	if (!dcc_ready(drvdata))
		dev_err(drvdata->dev, "DCC is not ready Disabling DCC...\n");

	for (curr_list = 0; curr_list < drvdata->nr_link_list; curr_list++) {
		if (!drvdata->enable[curr_list])
			continue;
		dcc_writel(drvdata, 0, DCC_LL_CFG(curr_list));
		dcc_writel(drvdata, 0, DCC_LL_BASE(curr_list));
		dcc_writel(drvdata, 0, DCC_FD_BASE(curr_list));
		dcc_writel(drvdata, 0, DCC_LL_LOCK(curr_list));
		drvdata->enable[curr_list] = false;
	}
	memset_io(drvdata->ram_base, 0, drvdata->ram_size);
	drvdata->ram_cfg = 0;
	drvdata->ram_cpy_len = 0;
	drvdata->ram_start = 0;
	mutex_unlock(&drvdata->mutex);
}

static ssize_t curr_list_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int ret;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	mutex_lock(&drvdata->mutex);
	if (drvdata->curr_list == DCC_INVALID_LINK_LIST) {
		dev_err(dev, "curr_list is not set.\n");
		ret = -EINVAL;
		goto err;
	}

	ret = scnprintf(buf, PAGE_SIZE, "%d\n",	drvdata->curr_list);
err:
	mutex_unlock(&drvdata->mutex);
	return ret;
}

static ssize_t curr_list_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned long val;
	uint32_t lock_reg;
	bool dcc_enable = false;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	if (val >= drvdata->nr_link_list)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);

	dcc_enable = is_dcc_enabled(drvdata);
	if (drvdata->curr_list != DCC_INVALID_LINK_LIST	&& dcc_enable) {
		dev_err(drvdata->dev, "DCC is enabled, please disable it first.\n");
		mutex_unlock(&drvdata->mutex);
		return -EINVAL;
	}

	lock_reg = dcc_readl(drvdata, DCC_LL_LOCK(val));
	if (lock_reg & 0x1) {
		dev_err(drvdata->dev, "DCC linked list is already configured\n");
		mutex_unlock(&drvdata->mutex);
		return -EINVAL;
	}
	drvdata->curr_list = val;
	mutex_unlock(&drvdata->mutex);

	return size;
}
static DEVICE_ATTR_RW(curr_list);

static ssize_t func_type_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);
	ssize_t len = 0;
	unsigned int i;

	for (i = 0; i < drvdata->nr_link_list; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%u :%s\n",
				 i, str_dcc_func_type[drvdata->func_type[i]]);

	return len;
}

static ssize_t func_type_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);
	char str[10] = "";
	int ret;

	if (strlen(buf) >= 10)
		return -EINVAL;
	if (sscanf(buf, "%8s", str) != 1)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev,
			"Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto out;
	}

	if (drvdata->enable[drvdata->curr_list]) {
		ret = -EBUSY;
		goto out;
	}

	if (!strcmp(str, str_dcc_func_type[DCC_FUNC_TYPE_CAPTURE]))
		drvdata->func_type[drvdata->curr_list] =
							DCC_FUNC_TYPE_CAPTURE;
	else if (!strcmp(str, str_dcc_func_type[DCC_FUNC_TYPE_CRC]))
		drvdata->func_type[drvdata->curr_list] =
							DCC_FUNC_TYPE_CRC;
	else {
		ret = -EINVAL;
		goto out;
	}

	ret = size;
out:
	mutex_unlock(&drvdata->mutex);
	return ret;
}
static DEVICE_ATTR_RW(func_type);

static ssize_t data_sink_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);
	ssize_t len = 0;
	unsigned int i;

	for (i = 0; i < drvdata->nr_link_list; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%u :%s\n",
				 i, str_dcc_data_sink[drvdata->data_sink[i]]);

	return len;
}

static ssize_t data_sink_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);
	char str[10] = "";
	int ret;

	if (strlen(buf) >= 10)
		return -EINVAL;
	if (sscanf(buf, "%8s", str) != 1)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev,
			"Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto out;
	}

	if (drvdata->enable[drvdata->curr_list]) {
		ret = -EBUSY;
		goto out;
	}

	if (!strcmp(str, str_dcc_data_sink[DCC_DATA_SINK_SRAM]))
		drvdata->data_sink[drvdata->curr_list] = DCC_DATA_SINK_SRAM;
	else if (!strcmp(str, str_dcc_data_sink[DCC_DATA_SINK_ATB]))
		drvdata->data_sink[drvdata->curr_list] = DCC_DATA_SINK_ATB;
	else {
		ret = -EINVAL;
		goto out;
	}

	ret = size;
out:
	mutex_unlock(&drvdata->mutex);
	return ret;
}
static DEVICE_ATTR_RW(data_sink);

static ssize_t trigger_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	int ret = 0;
	unsigned long val;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (val != 1)
		return -EINVAL;

	ret = dcc_sw_trigger(drvdata);
	if (!ret)
		ret = size;

	return ret;
}
static DEVICE_ATTR_WO(trigger);

static ssize_t enable_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int ret;
	bool dcc_enable = false;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	mutex_lock(&drvdata->mutex);
	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto err;
	}

	dcc_enable = is_dcc_enabled(drvdata);

	ret = scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)dcc_enable);
err:
	mutex_unlock(&drvdata->mutex);
	return ret;
}

static ssize_t enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret = 0;
	unsigned long val;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	if (kstrtoul(buf, 16, &val) || val > 1)
		return -EINVAL;

	if (val)
		ret = dcc_enable(drvdata);
	else
		dcc_disable(drvdata);

	if (!ret)
		ret = size;

	return ret;

}
static DEVICE_ATTR_RW(enable);

static ssize_t ap_ns_qad_override_en_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata->mem_map_ver != DCC_MEM_MAP_VER3) {
		dev_err(dev, "QAD output is not supported\n");
		return -EINVAL;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", drvdata->qad_output[drvdata->curr_list]);
}

static ssize_t ap_ns_qad_override_en_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret = 0;
	unsigned long val;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata->mem_map_ver != DCC_MEM_MAP_VER3) {
		dev_err(dev, "QAD output is not supported\n");
		return -EINVAL;
	}

	if (kstrtoul(buf, 16, &val) || val > 1)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);

	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto out;
	}

	if (drvdata->enable[drvdata->curr_list]) {
		ret = -EBUSY;
		goto out;
	}


	if (val)
		drvdata->qad_output[drvdata->curr_list] = 1;
	else
		drvdata->qad_output[drvdata->curr_list] = 0;
	ret = size;
out:
	mutex_unlock(&drvdata->mutex);
	return ret;

}
static DEVICE_ATTR_RW(ap_ns_qad_override_en);

static ssize_t hw_trig_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;

	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto err;
	}

	ret = scnprintf(buf, PAGE_SIZE, "%u\n",
			(unsigned int)drvdata->hw_trig[drvdata->curr_list]);

err:
	return ret;
}

static ssize_t hw_trig_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret = 0;
	unsigned long val;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	if ((kstrtoul(buf, 16, &val)) || val > 1)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);

	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto err;
	}

	if (drvdata->enable[drvdata->curr_list]) {
		ret = -EBUSY;
		goto err;
	}

	if (val)
		drvdata->hw_trig[drvdata->curr_list] = true;
	else
		drvdata->hw_trig[drvdata->curr_list] = false;

	ret = size;
err:
	mutex_unlock(&drvdata->mutex);
	return ret;
}
static DEVICE_ATTR_RW(hw_trig);

static ssize_t sw_trig_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;

	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto err;
	}

	ret = scnprintf(buf, PAGE_SIZE, "%u\n",
			(unsigned int)drvdata->sw_trig[drvdata->curr_list]);

err:
	return ret;
}

static ssize_t sw_trig_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret = 0;
	unsigned long val;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	if ((kstrtoul(buf, 16, &val)) || val > 1)
		return -EINVAL;

	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto err;
	}

	if (val)
		drvdata->sw_trig[drvdata->curr_list] = true;
	else
		drvdata->sw_trig[drvdata->curr_list] = false;

	ret = size;
err:
	return ret;
}
static DEVICE_ATTR_RW(sw_trig);

static ssize_t config_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);
	struct dcc_config_entry *entry;
	char local_buf[64];
	int len = 0, count = 0;

	buf[0] = '\0';

	mutex_lock(&drvdata->mutex);
	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev, "Select link list to program using curr_list\n");
		count = -EINVAL;
		goto err;
	}

	list_for_each_entry(entry,
			    &drvdata->cfg_head[drvdata->curr_list], list) {
		switch (entry->desc_type) {
		case DCC_READ_WRITE_TYPE:
			len = scnprintf(local_buf, 64,
				       "Index: 0x%x, mask: 0x%x, val: 0x%x\n",
				       entry->index, entry->mask,
				       entry->write_val);
			break;
		case DCC_LOOP_TYPE:
			len = scnprintf(local_buf, 64, "Index: 0x%x, Loop: %d\n",
				       entry->index, entry->loop_cnt);
			break;
		case DCC_WRITE_TYPE:
			len = scnprintf(local_buf, 64,
				       "Write Index: 0x%x, Base: 0x%x, Offset: 0x%x, len: 0x%x APB: %d\n",
				       entry->index, entry->base,
				       entry->offset, entry->len,
				       entry->apb_bus);
			break;
		default:
			len = scnprintf(local_buf, 64,
				       "Read Index: 0x%x, Base: 0x%x, Offset: 0x%x, len: 0x%x APB: %d\n",
				       entry->index, entry->base,
				       entry->offset, entry->len,
				       entry->apb_bus);
		}

		if ((count + len) > PAGE_SIZE) {
			dev_err(dev, "DCC: Couldn't write complete config\n");
			break;
		}
		strlcat(buf, local_buf, PAGE_SIZE);
		count += len;
	}

err:
	mutex_unlock(&drvdata->mutex);
	return count;
}

static int dcc_config_add(struct dcc_drvdata *drvdata, unsigned int addr,
			  unsigned int len, int apb_bus)
{
	int ret;
	struct dcc_config_entry *entry, *pentry;
	unsigned int base, offset;

	mutex_lock(&drvdata->mutex);

	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(drvdata->dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto err;
	}

	/* Check the len to avoid allocate huge memory */
	if (!len || len > (drvdata->ram_size / 8)) {
		dev_err(drvdata->dev, "DCC: Invalid length\n");
		ret = -EINVAL;
		goto err;
	}

	base = addr & BM(4, 31);

	if (!list_empty(&drvdata->cfg_head[drvdata->curr_list])) {
		pentry = list_last_entry(&drvdata->cfg_head[drvdata->curr_list],
					 struct dcc_config_entry, list);

		if (pentry->desc_type == DCC_ADDR_TYPE &&
		    addr >= (pentry->base + pentry->offset) &&
		    addr <= (pentry->base + pentry->offset + MAX_DCC_OFFSET)) {

			/* Re-use base address from last entry */
			base = pentry->base;

			/*
			 * Check if new address is contiguous to last entry's
			 * addresses. If yes then we can re-use last entry and
			 * just need to update its length.
			 */
			if ((pentry->len * 4 + pentry->base + pentry->offset)
			    == addr) {
				len += pentry->len;

				/*
				 * Check if last entry can hold additional new
				 * length. If yes then we don't need to create
				 * a new entry else we need to add a new entry
				 * with same base but updated offset.
				 */
				if (len > MAX_DCC_LEN)
					pentry->len = MAX_DCC_LEN;
				else
					pentry->len = len;

				/*
				 * Update start addr and len for remaining
				 * addresses, which will be part of new
				 * entry.
				 */
				addr = pentry->base + pentry->offset +
					pentry->len * 4;
				len -= pentry->len;
			}
		}
	}

	offset = addr - base;

	while (len) {
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry) {
			ret = -ENOMEM;
			goto err;
		}

		entry->base = base;
		entry->offset = offset;
		entry->len = min_t(uint32_t, len, MAX_DCC_LEN);
		entry->index = drvdata->nr_config[drvdata->curr_list]++;
		entry->desc_type = DCC_ADDR_TYPE;
		entry->apb_bus = apb_bus;
		INIT_LIST_HEAD(&entry->list);
		list_add_tail(&entry->list,
			      &drvdata->cfg_head[drvdata->curr_list]);

		len -= entry->len;
		offset += MAX_DCC_LEN * 4;
	}

	mutex_unlock(&drvdata->mutex);
	return 0;
err:
	mutex_unlock(&drvdata->mutex);
	return ret;
}

static ssize_t config_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret, len, apb_bus;
	unsigned int base;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);
	int nval;

	nval = sscanf(buf, "%x %i %d", &base, &len, &apb_bus);
	if ((nval <= 0 || nval > 3) || (apb_bus < 0 || apb_bus > 1))
		return -EINVAL;

	if (nval == 1) {
		len = 1;
		apb_bus = 0;
	} else if (nval == 3 && apb_bus == 1) {
		apb_bus = 1;
	} else {
		apb_bus = 0;
	}

	ret = dcc_config_add(drvdata, base, len, apb_bus);
	if (ret)
		return ret;

	return size;

}
static DEVICE_ATTR_RW(config);

static void dcc_config_reset(struct dcc_drvdata *drvdata)
{
	struct dcc_config_entry *entry, *temp;
	int curr_list;

	mutex_lock(&drvdata->mutex);

	for (curr_list = 0; curr_list < drvdata->nr_link_list; curr_list++) {

		list_for_each_entry_safe(entry, temp,
					 &drvdata->cfg_head[curr_list], list) {
			list_del(&entry->list);
			kfree(entry);
			drvdata->nr_config[curr_list]--;
		}
	}
	drvdata->ram_start = 0;
	drvdata->ram_cfg = 0;
	drvdata->ram_cpy_len = 0;
	mutex_unlock(&drvdata->mutex);
}

static ssize_t config_reset_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	unsigned long val;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	if (kstrtoul(buf, 16, &val) || val > 1)
		return -EINVAL;

	if (val)
		dcc_config_reset(drvdata);

	return size;
}
static DEVICE_ATTR_WO(config_reset);

static ssize_t crc_error_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	mutex_lock(&drvdata->mutex);
	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto err;
	}

	if (!drvdata->enable[drvdata->curr_list]) {
		ret = -EINVAL;
		goto err;
	}

	ret = scnprintf(buf, PAGE_SIZE, "%u\n",
			(unsigned int)BVAL(dcc_readl(
			drvdata, DCC_LL_INT_STATUS(drvdata->curr_list)), 1));
err:
	mutex_unlock(&drvdata->mutex);
	return ret;
}
static DEVICE_ATTR_RO(crc_error);

static ssize_t ready_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	int ret;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	mutex_lock(&drvdata->mutex);

	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto err;
	}

	if (!drvdata->enable[drvdata->curr_list]) {
		ret = -EINVAL;
		goto err;
	}

	ret = scnprintf(buf, PAGE_SIZE, "%u\n",
			(unsigned int)BVAL(dcc_readl(drvdata, DCC_STATUS), 1));
err:
	mutex_unlock(&drvdata->mutex);
	return ret;
}
static DEVICE_ATTR_RO(ready);

static ssize_t interrupt_disable_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->interrupt_disable);
}

static ssize_t interrupt_disable_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t size)
{
	unsigned long val;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	if (kstrtoul(buf, 16, &val) || val > 1)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	drvdata->interrupt_disable = (val ? 1:0);
	mutex_unlock(&drvdata->mutex);
	return size;
}
static DEVICE_ATTR_RW(interrupt_disable);

static int dcc_add_loop(struct dcc_drvdata *drvdata, unsigned long loop_cnt)
{
	struct dcc_config_entry *entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->loop_cnt = min_t(uint32_t, loop_cnt, MAX_LOOP_CNT);
	entry->index = drvdata->nr_config[drvdata->curr_list]++;
	entry->desc_type = DCC_LOOP_TYPE;
	INIT_LIST_HEAD(&entry->list);
	list_add_tail(&entry->list, &drvdata->cfg_head[drvdata->curr_list]);

	return 0;
}

static ssize_t loop_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret;
	unsigned long loop_cnt;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	mutex_lock(&drvdata->mutex);

	if (kstrtoul(buf, 16, &loop_cnt)) {
		ret = -EINVAL;
		goto err;
	}

	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto err;
	}

	ret = dcc_add_loop(drvdata, loop_cnt);
	if (ret)
		goto err;

	mutex_unlock(&drvdata->mutex);
	return size;
err:
	mutex_unlock(&drvdata->mutex);
	return ret;
}
static DEVICE_ATTR_WO(loop);

static int dcc_rd_mod_wr_add(struct dcc_drvdata *drvdata, unsigned int mask,
			  unsigned int val)
{
	int ret = 0;
	struct dcc_config_entry *entry;

	mutex_lock(&drvdata->mutex);

	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(drvdata->dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto err;
	}

	if (list_empty(&drvdata->cfg_head[drvdata->curr_list])) {
		dev_err(drvdata->dev, "DCC: No read address programmed\n");
		ret = -EPERM;
		goto err;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		ret = -ENOMEM;
		goto err;
	}

	entry->desc_type = DCC_READ_WRITE_TYPE;
	entry->mask = mask;
	entry->write_val = val;
	entry->index = drvdata->nr_config[drvdata->curr_list]++;
	INIT_LIST_HEAD(&entry->list);
	list_add_tail(&entry->list, &drvdata->cfg_head[drvdata->curr_list]);
err:
	mutex_unlock(&drvdata->mutex);
	return ret;
}

static ssize_t rd_mod_wr_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret;
	int nval;
	unsigned int mask, val;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	nval = sscanf(buf, "%x %x", &mask, &val);

	if (nval <= 1 || nval > 2)
		return -EINVAL;

	ret = dcc_rd_mod_wr_add(drvdata, mask, val);
	if (ret)
		return ret;

	return size;

}
static DEVICE_ATTR_WO(rd_mod_wr);

static int dcc_add_write(struct dcc_drvdata *drvdata, unsigned int addr,
			 unsigned int write_val, int apb_bus)
{
	struct dcc_config_entry *entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->desc_type = DCC_WRITE_TYPE;
	entry->base = addr & BM(4, 31);
	entry->offset = addr - entry->base;
	entry->write_val = write_val;
	entry->index = drvdata->nr_config[drvdata->curr_list]++;
	entry->len = 1;
	entry->apb_bus = apb_bus;
	INIT_LIST_HEAD(&entry->list);
	list_add_tail(&entry->list, &drvdata->cfg_head[drvdata->curr_list]);

	return 0;
}

static ssize_t config_write_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret;
	int nval;
	unsigned int addr, write_val;
	int apb_bus = 0;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	mutex_lock(&drvdata->mutex);

	nval = sscanf(buf, "%x %x %d", &addr, &write_val, &apb_bus);

	if ((nval <= 1 || nval > 3) || (apb_bus < 0 || apb_bus > 1)) {
		ret = -EINVAL;
		goto err;
	}

	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto err;
	}

	if (nval == 3 && apb_bus == 1)
		apb_bus = 1;

	ret = dcc_add_write(drvdata, addr, write_val, apb_bus);
	if (ret)
		goto err;

	mutex_unlock(&drvdata->mutex);
	return size;
err:
	mutex_unlock(&drvdata->mutex);
	return ret;
}
static DEVICE_ATTR_WO(config_write);

static ssize_t cti_trig_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", drvdata->cti_trig[drvdata->curr_list]);
}

static ssize_t cti_trig_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	unsigned long val;
	int ret = 0;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	if (kstrtoul(buf, 16, &val) || val > 1)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);

	if (drvdata->curr_list >= drvdata->nr_link_list) {
		dev_err(dev, "Select link list to program using curr_list\n");
		ret = -EINVAL;
		goto out;
	}

	if (drvdata->enable[drvdata->curr_list]) {
		ret = -EBUSY;
		goto out;
	}

	if (val)
		drvdata->cti_trig[drvdata->curr_list] = 1;
	else
		drvdata->cti_trig[drvdata->curr_list] = 0;

	ret = size;
out:
	mutex_unlock(&drvdata->mutex);
	return ret;
}
static DEVICE_ATTR_RW(cti_trig);

static const struct device_attribute *dcc_attrs[] = {
	&dev_attr_func_type,
	&dev_attr_data_sink,
	&dev_attr_trigger,
	&dev_attr_enable,
	&dev_attr_hw_trig,
	&dev_attr_sw_trig,
	&dev_attr_config,
	&dev_attr_config_reset,
	&dev_attr_ready,
	&dev_attr_crc_error,
	&dev_attr_interrupt_disable,
	&dev_attr_loop,
	&dev_attr_rd_mod_wr,
	&dev_attr_curr_list,
	&dev_attr_config_write,
	&dev_attr_cti_trig,
	&dev_attr_ap_ns_qad_override_en,
	NULL,
};

static int dcc_create_files(struct device *dev,
			    const struct device_attribute **attrs)
{
	int ret = 0, i;

	for (i = 0; attrs[i] != NULL; i++) {
		ret = device_create_file(dev, attrs[i]);
		if (ret) {
			dev_err(dev, "DCC: Couldn't create sysfs attribute: %s\n",
				attrs[i]->attr.name);
			break;
		}
	}
	return ret;
}

static int dcc_sram_open(struct inode *inode, struct file *file)
{
	struct dcc_drvdata *drvdata = container_of(inode->i_cdev,
						   struct dcc_drvdata,
						   sram_dev);
	file->private_data = drvdata;

	return  0;
}

static ssize_t dcc_sram_read(struct file *file, char __user *data,
			     size_t len, loff_t *ppos)
{
	unsigned char *buf;
	struct dcc_drvdata *drvdata = file->private_data;
	int ret;

	/* EOF check */
	if (drvdata->ram_size <= *ppos)
		return 0;

	if ((*ppos + len) < len
		|| (*ppos + len) > drvdata->ram_size)
		len = (drvdata->ram_size - *ppos);

	buf = kzalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = dcc_sram_memcpy(buf, (drvdata->ram_base + *ppos), len);
	if (ret) {
		dev_err(drvdata->dev,
			"Target address or size not aligned with 4 bytes\n");
		kfree(buf);
		return ret;
	}

	if (copy_to_user(data, buf, len)) {
		dev_err(drvdata->dev,
			"DCC: Couldn't copy all data to user\n");
		kfree(buf);
		return -EFAULT;
	}

	*ppos += len;

	kfree(buf);

	return len;
}

static const struct file_operations dcc_sram_fops = {
	.owner		= THIS_MODULE,
	.open		= dcc_sram_open,
	.read		= dcc_sram_read,
	.llseek		= no_llseek,
};

static int dcc_sram_dev_register(struct dcc_drvdata *drvdata)
{
	int ret;
	struct device *device;
	dev_t dev;

	ret = alloc_chrdev_region(&dev, 0, 1, drvdata->sram_node);
	if (ret)
		goto err_alloc;

	cdev_init(&drvdata->sram_dev, &dcc_sram_fops);

	drvdata->sram_dev.owner = THIS_MODULE;
	ret = cdev_add(&drvdata->sram_dev, dev, 1);
	if (ret)
		goto err_cdev_add;

	drvdata->sram_class = class_create(THIS_MODULE,
					   drvdata->sram_node);
	if (IS_ERR(drvdata->sram_class)) {
		ret = PTR_ERR(drvdata->sram_class);
		goto err_class_create;
	}

	device = device_create(drvdata->sram_class, NULL,
			       drvdata->sram_dev.dev, drvdata,
			       drvdata->sram_node);
	if (IS_ERR(device)) {
		ret = PTR_ERR(device);
		goto err_dev_create;
	}

	return 0;
err_dev_create:
	class_destroy(drvdata->sram_class);
err_class_create:
	cdev_del(&drvdata->sram_dev);
err_cdev_add:
	unregister_chrdev_region(drvdata->sram_dev.dev, 1);
err_alloc:
	return ret;
}

static void dcc_sram_dev_deregister(struct dcc_drvdata *drvdata)
{
	device_destroy(drvdata->sram_class, drvdata->sram_dev.dev);
	class_destroy(drvdata->sram_class);
	cdev_del(&drvdata->sram_dev);
	unregister_chrdev_region(drvdata->sram_dev.dev, 1);
}

static int dcc_sram_dev_init(struct dcc_drvdata *drvdata)
{
	int ret = 0;
	size_t node_size;
	char *node_name = "dcc_sram";
	struct device *dev = drvdata->dev;

	node_size = strlen(node_name) + 1;

	drvdata->sram_node = devm_kzalloc(dev, node_size, GFP_KERNEL);
	if (!drvdata->sram_node)
		return -ENOMEM;

	strscpy(drvdata->sram_node, node_name, node_size);
	ret = dcc_sram_dev_register(drvdata);
	if (ret)
		dev_err(drvdata->dev, "DCC: sram node not registered.\n");

	return ret;
}

static void dcc_sram_dev_exit(struct dcc_drvdata *drvdata)
{
	dcc_sram_dev_deregister(drvdata);
}

static int dcc_dt_parse(struct dcc_drvdata *drvdata, struct device_node *np)
{
	int i, ret = -1;
	const __be32 *prop;
	uint32_t len, entry, val1, val2, apb_bus;
	uint32_t curr_link_list;
	const char *data_sink;

	ret = of_property_read_u32(np, "qcom,curr-link-list",
				&curr_link_list);
	if (ret)
		return ret;

	if (curr_link_list >= drvdata->nr_link_list) {
		dev_err(drvdata->dev, "List configuration failed.\n");
		return ret;
	}
	drvdata->curr_list = curr_link_list;

	if (of_property_read_bool(np, "qcom,ap-qad-override"))
		drvdata->qad_output[drvdata->curr_list] = 1;

	drvdata->data_sink[curr_link_list] = DCC_DATA_SINK_SRAM;
	ret = of_property_read_string(np, "qcom,data-sink",
					&data_sink);
	if (!ret) {
		for (i = 0; i < ARRAY_SIZE(str_dcc_data_sink); i++)
			if (!strcmp(data_sink, str_dcc_data_sink[i])) {
				drvdata->data_sink[curr_link_list] = i;
				break;
			}

		if (i == ARRAY_SIZE(str_dcc_data_sink)) {
			dev_err(drvdata->dev, "Unknown sink type for DCC Using '%s' as data sink\n",
			str_dcc_data_sink[drvdata->data_sink[curr_link_list]]);
		}
	}

	prop = of_get_property(np, "qcom,link-list", &len);
	if (prop) {
		len /= sizeof(__be32);
		i = 0;
		while (i < len) {
			entry = be32_to_cpu(prop[i++]);
			val1 = be32_to_cpu(prop[i++]);
			val2 = be32_to_cpu(prop[i++]);
			apb_bus = be32_to_cpu(prop[i++]);

			switch (entry) {
			case DCC_READ:
				ret = dcc_config_add(drvdata, val1,
							val2, apb_bus);
				break;
			case DCC_READ_WRITE:
				ret = dcc_rd_mod_wr_add(drvdata, val1,
							val2);
				break;
			case DCC_WRITE:
				ret = dcc_add_write(drvdata, val1,
							val2, apb_bus);
				break;
			case DCC_LOOP:
				ret = dcc_add_loop(drvdata, val1);
				break;
			default:
				ret = -EINVAL;
			}

			if (ret) {
				dev_err(drvdata->dev,
					"DCC init time config failed err:%d\n",
					ret);
				break;
			}
		}
	}
	return ret;
}

static void dcc_configure_list(struct dcc_drvdata *drvdata,
			       struct device_node *np)
{
	int ret = -1;
	struct device_node *link_node = NULL;

	for_each_available_child_of_node(np, link_node) {
		ret = dcc_dt_parse(drvdata, link_node);
		if (ret) {
			dev_err(drvdata->dev,
				"DCC link list config failed err:%d\n", ret);
			break;
		}
	}

	if (ret == -1)
		ret = dcc_dt_parse(drvdata, np);

	if (!ret)
		dcc_enable(drvdata);
}

static int dcc_probe(struct platform_device *pdev)
{
	int ret, i;
	struct device *dev = &pdev->dev;
	struct dcc_drvdata *drvdata;
	struct resource *res;
	struct md_region md_entry;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &pdev->dev;
	platform_set_drvdata(pdev, drvdata);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dcc-base");
	if (!res)
		return -EINVAL;

	drvdata->reg_size = resource_size(res);
	drvdata->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!drvdata->base)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "dcc-ram-base");
	if (!res)
		return -EINVAL;

	drvdata->ram_size = resource_size(res);
	drvdata->ram_base = devm_ioremap(dev, res->start, resource_size(res));
	if (!drvdata->ram_base)
		return -ENOMEM;

	ret = of_property_read_u32(pdev->dev.of_node, "dcc-ram-offset",
				   &drvdata->ram_offset);
	if (ret)
		return -EINVAL;

	drvdata->ll_state_cnt = of_property_count_elems_of_size(dev->of_node,
					"ll-reg-offsets", sizeof(u32)); /* optional */
	if (drvdata->ll_state_cnt <= 0) {
		dev_info(dev, "ll-reg-offsets property doesn't exist\n");
		drvdata->ll_state_cnt = 0;
	} else {
		ret = of_property_read_u32(pdev->dev.of_node, "per-ll-reg-cnt",
					&drvdata->per_ll_reg_cnt);
		if (ret)
			return -EINVAL;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "dcc-mem-map-ver",
					&drvdata->mem_map_ver);
	if (ret) {
		if (BVAL(dcc_readl(drvdata, DCC_HW_INFO), 9))
			drvdata->mem_map_ver = DCC_MEM_MAP_VER3;
		else if ((dcc_readl(drvdata, DCC_HW_INFO) & 0x3F) == 0x3F)
			drvdata->mem_map_ver = DCC_MEM_MAP_VER2;
		else
			drvdata->mem_map_ver = DCC_MEM_MAP_VER1;
	}

	if (drvdata->mem_map_ver < DCC_MEM_MAP_VER1
			|| drvdata->mem_map_ver > DCC_MEM_MAP_VER3)
		return  -EINVAL;

	if (drvdata->mem_map_ver) {
		drvdata->nr_link_list = dcc_readl(drvdata, DCC_LL_NUM_INFO);
		if (drvdata->nr_link_list == 0)
			return  -EINVAL;
	} else {
		drvdata->nr_link_list = DCC_MAX_LINK_LIST;
	}

	if ((dcc_readl(drvdata, DCC_HW_INFO) & BIT(6)) == BIT(6))
		drvdata->loopoff = DCC_FIX_LOOP_OFFSET;
	else
		drvdata->loopoff = get_bitmask_order((drvdata->ram_size +
				drvdata->ram_offset) / 4 - 1);
	mutex_init(&drvdata->mutex);
	drvdata->data_sink = devm_kzalloc(dev, drvdata->nr_link_list *
			sizeof(enum dcc_data_sink), GFP_KERNEL);
	if (!drvdata->data_sink)
		return -ENOMEM;
	drvdata->func_type = devm_kzalloc(dev, drvdata->nr_link_list *
			sizeof(enum dcc_func_type), GFP_KERNEL);
	if (!drvdata->func_type)
		return -ENOMEM;
	drvdata->enable = devm_kzalloc(dev, drvdata->nr_link_list *
			sizeof(bool), GFP_KERNEL);
	if (!drvdata->enable)
		return -ENOMEM;
	drvdata->hw_trig = devm_kzalloc(dev, drvdata->nr_link_list *
			sizeof(bool), GFP_KERNEL);
	if (!drvdata->hw_trig)
		return -ENOMEM;
	drvdata->sw_trig = devm_kzalloc(dev, drvdata->nr_link_list *
			sizeof(bool), GFP_KERNEL);
	if (!drvdata->sw_trig)
		return -ENOMEM;
	drvdata->configured = devm_kzalloc(dev, drvdata->nr_link_list *
			sizeof(bool), GFP_KERNEL);
	if (!drvdata->configured)
		return -ENOMEM;
	drvdata->nr_config = devm_kzalloc(dev, drvdata->nr_link_list *
			sizeof(uint32_t), GFP_KERNEL);
	if (!drvdata->nr_config)
		return -ENOMEM;
	drvdata->cti_trig = devm_kzalloc(dev, drvdata->nr_link_list *
			sizeof(uint8_t), GFP_KERNEL);
	if (!drvdata->cti_trig)
		return -ENOMEM;
	drvdata->qad_output = devm_kzalloc(dev, drvdata->nr_link_list *
			sizeof(uint8_t), GFP_KERNEL);
	if (!drvdata->qad_output)
		return -ENOMEM;
	drvdata->cfg_head = devm_kzalloc(dev, drvdata->nr_link_list *
			sizeof(struct list_head), GFP_KERNEL);
	if (!drvdata->cfg_head)
		return -ENOMEM;

	for (i = 0; i < drvdata->nr_link_list; i++) {
		INIT_LIST_HEAD(&drvdata->cfg_head[i]);
		drvdata->nr_config[i] = 0;
		drvdata->hw_trig[i] = true;
		drvdata->sw_trig[i] = false;
	}

	memset_io(drvdata->ram_base, 0, drvdata->ram_size);

	drvdata->curr_list = DCC_INVALID_LINK_LIST;

	ret = dcc_sram_dev_init(drvdata);
	if (ret)
		goto err;

	ret = dcc_create_files(dev, dcc_attrs);
	if (ret)
		goto err;

	dcc_configure_list(drvdata, pdev->dev.of_node);

	/* Add dcc info to minidump table */
	strscpy(md_entry.name, "KDCCDATA", sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t)drvdata->ram_base;
	md_entry.phys_addr = res->start;
	md_entry.size = drvdata->ram_size;
	if (msm_minidump_add_region(&md_entry) < 0)
		dev_err(drvdata->dev, "Failed to add DCC data in Minidump\n");

	return 0;
err:
	return ret;
}

static int dcc_remove(struct platform_device *pdev)
{
	struct dcc_drvdata *drvdata = platform_get_drvdata(pdev);

	dcc_sram_dev_exit(drvdata);

	dcc_config_reset(drvdata);

	return 0;
}

#if defined(CONFIG_DEEPSLEEP) || defined(CONFIG_HIBERNATION)
static int dcc_state_store(struct device *dev)
{
	int ret = 0, n, i;
	u32 *ll_reg_offsets;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	if (!drvdata) {
		dev_dbg(dev, "Invalid drvdata\n");
		return -EINVAL;
	}

	if (!is_dcc_enabled(drvdata)) {
		dev_dbg(dev, "DCC is not enabled.\n");
		return 0;
	}

	if (!drvdata->ll_state_cnt) {
		dev_dbg(dev, "reg-offsets property doesn't exist\n");
		return 0;
	}

	n = drvdata->ll_state_cnt;
	ll_reg_offsets = kcalloc(n, sizeof(u32), GFP_KERNEL);
	if (!ll_reg_offsets) {
		dev_err(dev, "Failed to alloc memory for reg_offsets\n");
		return -ENOMEM;
	}

	ret = of_property_read_variable_u32_array(dev->of_node,
					"ll-reg-offsets", ll_reg_offsets, n, n);
	if (ret < 0) {
		dev_dbg(dev, "Not found reg-offsets property\n");
		goto out;
	}

	drvdata->ll_state = kzalloc(n * sizeof(struct reg_state), GFP_KERNEL);
	if (!drvdata->ll_state) {
		ret = -ENOMEM;
		goto out;
	}

	drvdata->sram_state = kzalloc(drvdata->ram_size, GFP_KERNEL);
	if (!drvdata->sram_state) {
		ret = -ENOMEM;
		goto sram_alloc_err;
	}

	if (dcc_sram_memcpy(drvdata->sram_state, drvdata->ram_base,
				drvdata->ram_cpy_len)) {
		dev_err(dev, "Failed to copy DCC SRAM contents\n");
		ret = -EINVAL;
		goto sram_cpy_err;
	}

	mutex_lock(&drvdata->mutex);

	for (i = 0; i < n; i++) {
		drvdata->ll_state[i].offset = ll_reg_offsets[i];
		drvdata->ll_state[i].val    = __raw_readl(drvdata->base + ll_reg_offsets[i]);
	}

	mutex_unlock(&drvdata->mutex);

	kfree(ll_reg_offsets);
	return 0;

sram_cpy_err:
	kfree(drvdata->sram_state);
	drvdata->sram_state = NULL;
sram_alloc_err:
	kfree(drvdata->ll_state);
	drvdata->ll_state = NULL;
out:
	kfree(ll_reg_offsets);
	return ret;
}

static int dcc_state_restore(struct device *dev)
{
	int n, i, j, dcc_ll_index;
	int ret = 0;
	int *sram_state;
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);
	uint32_t ram_cpy_wlen;

	if (!drvdata) {
		dev_err(dev, "Err: %s Invalid argument\n", __func__);
		return -EINVAL;
	}

	if (!is_dcc_enabled(drvdata)) {
		dev_dbg(dev, "DCC is not enabled.\n");
		ret = 0;
		goto out;
	}

	if (!drvdata->ll_state_cnt) {
		dev_dbg(dev, "reg-offsets property doesn't exist\n");
		ret = 0;
		goto out;
	}

	if (!drvdata->sram_state || !drvdata->ll_state) {
		dev_err(dev, "Err: Restore state is NULL\n");
		ret = -EINVAL;
		goto out;
	}

	ram_cpy_wlen = drvdata->ram_cpy_len / 4;
	sram_state = drvdata->sram_state;
	n = drvdata->ll_state_cnt;

	for (i = 0; i < ram_cpy_wlen; i++)
		dcc_sram_writel(drvdata, sram_state[i], i * 4);

	mutex_lock(&drvdata->mutex);

	for (i = 0, dcc_ll_index = 0;
		 (dcc_ll_index < drvdata->nr_link_list) && (i < n);
		 dcc_ll_index++) {

		if (list_empty(&drvdata->cfg_head[dcc_ll_index])) {
			i += drvdata->per_ll_reg_cnt;
			continue;
		}

		for (j = 0; j < drvdata->per_ll_reg_cnt; i++, j++)
			__raw_writel(drvdata->ll_state[i].val,
						drvdata->base + drvdata->ll_state[i].offset);
	}

	mutex_unlock(&drvdata->mutex);
out:
	kfree(drvdata->sram_state);
	drvdata->sram_state = NULL;

	kfree(drvdata->ll_state);
	drvdata->ll_state = NULL;

	return ret;
}
#endif

#ifdef CONFIG_DEEPSLEEP
static int dcc_v2_suspend(struct device *dev)
{
	if (pm_suspend_via_firmware())
		return dcc_state_store(dev);

	return 0;
}

static int dcc_v2_resume(struct device *dev)
{
	if (pm_suspend_via_firmware())
		return dcc_state_restore(dev);

	return 0;
}
#endif

#ifdef CONFIG_HIBERNATION
static int dcc_v2_freeze(struct device *dev)
{
	return dcc_state_store(dev);
}

static int dcc_v2_restore(struct device *dev)
{
	return dcc_state_restore(dev);
}

static int dcc_v2_thaw(struct device *dev)
{
	struct dcc_drvdata *drvdata = dev_get_drvdata(dev);

	if (!drvdata || !drvdata->ll_state || !drvdata->sram_state) {
		dev_err(dev, "Err: %s Invalid argument\n", __func__);
		return -EINVAL;
	}

	kfree(drvdata->sram_state);
	kfree(drvdata->ll_state);
	drvdata->sram_state = NULL;
	drvdata->ll_state = NULL;

	return 0;
}
#endif

static const struct dev_pm_ops dcc_v2_pm_ops = {
#ifdef CONFIG_DEEPSLEEP
	.suspend         = dcc_v2_suspend,
	.resume          = dcc_v2_resume,
#endif
#ifdef CONFIG_HIBERNATION
	.freeze          = dcc_v2_freeze,
	.restore         = dcc_v2_restore,
	.thaw            = dcc_v2_thaw,
#endif
};

static const struct of_device_id msm_dcc_match[] = {
	{ .compatible = "qcom,dcc-v2"},
	{}
};

static struct platform_driver dcc_driver = {
	.probe          = dcc_probe,
	.remove         = dcc_remove,
	.driver         = {
		.name   = "msm-dcc",
		.of_match_table	= msm_dcc_match,
		.pm     = &dcc_v2_pm_ops,
	},
};

module_platform_driver(dcc_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MSM data capture and compare engine");
