// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (C) 2021 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 * Copyright (C) 2021 Jernej Skrabec <jernej.skrabec@gmail.com>
 *
 * Based on rtw88/pci.c:
 *   Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include "main.h"
#include "mac.h"
#include "debug.h"
#include "fw.h"
#include "ps.h"
#include "reg.h"
#include "rx.h"
#include "sdio.h"
#include "tx.h"

#define RTW_SDIO_INDIRECT_RW_RETRIES			50
#define RTW_SDIO_OQT_TIMEOUT_MS				1000

/* Bounds the queueing latency of the unbounded per-AC software FIFO. */
#define RTW_SDIO_TX_FIFO_HIWATER			16
#define RTW_SDIO_TX_FIFO_LOWATER			8
#define RTW_SDIO_TX_RETRY_DELAY			msecs_to_jiffies(1)

static bool rtw_sdio_is_bus_addr(u32 addr)
{
	return !!(addr & RTW_SDIO_BUS_MSK);
}

static bool rtw_sdio_bus_claim_needed(struct rtw_sdio *rtwsdio)
{
	return !rtwsdio->irq_thread ||
	       rtwsdio->irq_thread != current;
}

static u32 rtw_sdio_to_bus_offset(struct rtw_dev *rtwdev, u32 addr)
{
	switch (addr & RTW_SDIO_BUS_MSK) {
	case WLAN_IOREG_OFFSET:
		addr &= WLAN_IOREG_REG_MSK;
		addr |= FIELD_PREP(REG_SDIO_CMD_ADDR_MSK,
				   REG_SDIO_CMD_ADDR_MAC_REG);
		break;
	case SDIO_LOCAL_OFFSET:
		addr &= SDIO_LOCAL_REG_MSK;
		addr |= FIELD_PREP(REG_SDIO_CMD_ADDR_MSK,
				   REG_SDIO_CMD_ADDR_SDIO_REG);
		break;
	default:
		rtw_warn(rtwdev, "Cannot convert addr 0x%08x to bus offset",
			 addr);
	}

	return addr;
}

static bool rtw_sdio_use_memcpy_io(struct rtw_dev *rtwdev, u32 addr,
				   u8 alignment)
{
	return IS_ALIGNED(addr, alignment) &&
	       test_bit(RTW_FLAG_POWERON, rtwdev->flags);
}

static void rtw_sdio_writel(struct rtw_dev *rtwdev, u32 val, u32 addr,
			    int *err_ret)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	u8 buf[4];
	int i;

	if (rtw_sdio_use_memcpy_io(rtwdev, addr, 4)) {
		sdio_writel(rtwsdio->sdio_func, val, addr, err_ret);
		return;
	}

	*(__le32 *)buf = cpu_to_le32(val);

	for (i = 0; i < 4; i++) {
		sdio_writeb(rtwsdio->sdio_func, buf[i], addr + i, err_ret);
		if (*err_ret)
			return;
	}
}

static void rtw_sdio_writew(struct rtw_dev *rtwdev, u16 val, u32 addr,
			    int *err_ret)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	u8 buf[2];
	int i;

	*(__le16 *)buf = cpu_to_le16(val);

	for (i = 0; i < 2; i++) {
		sdio_writeb(rtwsdio->sdio_func, buf[i], addr + i, err_ret);
		if (*err_ret)
			return;
	}
}

static u32 rtw_sdio_readl(struct rtw_dev *rtwdev, u32 addr, int *err_ret)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	u8 buf[4];
	int i;

	if (rtw_sdio_use_memcpy_io(rtwdev, addr, 4))
		return sdio_readl(rtwsdio->sdio_func, addr, err_ret);

	for (i = 0; i < 4; i++) {
		buf[i] = sdio_readb(rtwsdio->sdio_func, addr + i, err_ret);
		if (*err_ret)
			return 0;
	}

	return le32_to_cpu(*(__le32 *)buf);
}

static u16 rtw_sdio_readw(struct rtw_dev *rtwdev, u32 addr, int *err_ret)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	u8 buf[2];
	int i;

	for (i = 0; i < 2; i++) {
		buf[i] = sdio_readb(rtwsdio->sdio_func, addr + i, err_ret);
		if (*err_ret)
			return 0;
	}

	return le16_to_cpu(*(__le16 *)buf);
}

static u32 rtw_sdio_to_io_address(struct rtw_dev *rtwdev, u32 addr,
				  bool direct)
{
	if (!direct)
		return addr;

	if (!rtw_sdio_is_bus_addr(addr))
		addr |= WLAN_IOREG_OFFSET;

	return rtw_sdio_to_bus_offset(rtwdev, addr);
}

static bool rtw_sdio_use_direct_io(struct rtw_dev *rtwdev, u32 addr)
{
	bool might_indirect_under_power_off = rtwdev->chip->id == RTW_CHIP_TYPE_8822C;

	if (!test_bit(RTW_FLAG_POWERON, rtwdev->flags) &&
	    !rtw_sdio_is_bus_addr(addr) && might_indirect_under_power_off)
		return false;

	return !rtw_sdio_is_sdio30_supported(rtwdev) ||
		rtw_sdio_is_bus_addr(addr);
}

static int rtw_sdio_indirect_reg_cfg(struct rtw_dev *rtwdev, u32 addr, u32 cfg)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	unsigned int retry;
	u32 reg_cfg;
	int ret;
	u8 tmp;

	reg_cfg = rtw_sdio_to_bus_offset(rtwdev, REG_SDIO_INDIRECT_REG_CFG);

	rtw_sdio_writel(rtwdev, addr | cfg | BIT_SDIO_INDIRECT_REG_CFG_UNK20,
			reg_cfg, &ret);
	if (ret)
		return ret;

	for (retry = 0; retry < RTW_SDIO_INDIRECT_RW_RETRIES; retry++) {
		tmp = sdio_readb(rtwsdio->sdio_func, reg_cfg + 2, &ret);
		if (!ret && (tmp & BIT(4)))
			return 0;
	}

	return -ETIMEDOUT;
}

static u8 rtw_sdio_indirect_read8(struct rtw_dev *rtwdev, u32 addr,
				  int *err_ret)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	u32 reg_data;

	*err_ret = rtw_sdio_indirect_reg_cfg(rtwdev, addr,
					     BIT_SDIO_INDIRECT_REG_CFG_READ);
	if (*err_ret)
		return 0;

	reg_data = rtw_sdio_to_bus_offset(rtwdev, REG_SDIO_INDIRECT_REG_DATA);
	return sdio_readb(rtwsdio->sdio_func, reg_data, err_ret);
}

static int rtw_sdio_indirect_read_bytes(struct rtw_dev *rtwdev, u32 addr,
					u8 *buf, int count)
{
	int i, ret = 0;

	for (i = 0; i < count; i++) {
		buf[i] = rtw_sdio_indirect_read8(rtwdev, addr + i, &ret);
		if (ret)
			break;
	}

	return ret;
}

static u16 rtw_sdio_indirect_read16(struct rtw_dev *rtwdev, u32 addr,
				    int *err_ret)
{
	u32 reg_data;
	u8 buf[2];

	if (!IS_ALIGNED(addr, 2)) {
		*err_ret = rtw_sdio_indirect_read_bytes(rtwdev, addr, buf, 2);
		if (*err_ret)
			return 0;

		return le16_to_cpu(*(__le16 *)buf);
	}

	*err_ret = rtw_sdio_indirect_reg_cfg(rtwdev, addr,
					     BIT_SDIO_INDIRECT_REG_CFG_READ);
	if (*err_ret)
		return 0;

	reg_data = rtw_sdio_to_bus_offset(rtwdev, REG_SDIO_INDIRECT_REG_DATA);
	return rtw_sdio_readw(rtwdev, reg_data, err_ret);
}

static u32 rtw_sdio_indirect_read32(struct rtw_dev *rtwdev, u32 addr,
				    int *err_ret)
{
	u32 reg_data;
	u8 buf[4];

	if (!IS_ALIGNED(addr, 4)) {
		*err_ret = rtw_sdio_indirect_read_bytes(rtwdev, addr, buf, 4);
		if (*err_ret)
			return 0;

		return le32_to_cpu(*(__le32 *)buf);
	}

	*err_ret = rtw_sdio_indirect_reg_cfg(rtwdev, addr,
					     BIT_SDIO_INDIRECT_REG_CFG_READ);
	if (*err_ret)
		return 0;

	reg_data = rtw_sdio_to_bus_offset(rtwdev, REG_SDIO_INDIRECT_REG_DATA);
	return rtw_sdio_readl(rtwdev, reg_data, err_ret);
}

static u8 rtw_sdio_read8(struct rtw_dev *rtwdev, u32 addr)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	bool direct, bus_claim;
	int ret;
	u8 val;

	direct = rtw_sdio_use_direct_io(rtwdev, addr);
	addr = rtw_sdio_to_io_address(rtwdev, addr, direct);
	bus_claim = rtw_sdio_bus_claim_needed(rtwsdio);

	if (bus_claim)
		sdio_claim_host(rtwsdio->sdio_func);

	if (direct)
		val = sdio_readb(rtwsdio->sdio_func, addr, &ret);
	else
		val = rtw_sdio_indirect_read8(rtwdev, addr, &ret);

	if (bus_claim)
		sdio_release_host(rtwsdio->sdio_func);

	if (ret)
		rtw_warn(rtwdev, "sdio read8 failed (0x%x): %d", addr, ret);

	return val;
}

static u16 rtw_sdio_read16(struct rtw_dev *rtwdev, u32 addr)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	bool direct, bus_claim;
	int ret;
	u16 val;

	direct = rtw_sdio_use_direct_io(rtwdev, addr);
	addr = rtw_sdio_to_io_address(rtwdev, addr, direct);
	bus_claim = rtw_sdio_bus_claim_needed(rtwsdio);

	if (bus_claim)
		sdio_claim_host(rtwsdio->sdio_func);

	if (direct)
		val = rtw_sdio_readw(rtwdev, addr, &ret);
	else
		val = rtw_sdio_indirect_read16(rtwdev, addr, &ret);

	if (bus_claim)
		sdio_release_host(rtwsdio->sdio_func);

	if (ret)
		rtw_warn(rtwdev, "sdio read16 failed (0x%x): %d", addr, ret);

	return val;
}

static u32 rtw_sdio_read32(struct rtw_dev *rtwdev, u32 addr)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	bool direct, bus_claim;
	u32 val;
	int ret;

	direct = rtw_sdio_use_direct_io(rtwdev, addr);
	addr = rtw_sdio_to_io_address(rtwdev, addr, direct);
	bus_claim = rtw_sdio_bus_claim_needed(rtwsdio);

	if (bus_claim)
		sdio_claim_host(rtwsdio->sdio_func);

	if (direct)
		val = rtw_sdio_readl(rtwdev, addr, &ret);
	else
		val = rtw_sdio_indirect_read32(rtwdev, addr, &ret);

	if (bus_claim)
		sdio_release_host(rtwsdio->sdio_func);

	if (ret)
		rtw_warn(rtwdev, "sdio read32 failed (0x%x): %d", addr, ret);

	return val;
}

static void rtw_sdio_indirect_write8(struct rtw_dev *rtwdev, u8 val, u32 addr,
				     int *err_ret)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	u32 reg_data;

	reg_data = rtw_sdio_to_bus_offset(rtwdev, REG_SDIO_INDIRECT_REG_DATA);
	sdio_writeb(rtwsdio->sdio_func, val, reg_data, err_ret);
	if (*err_ret)
		return;

	*err_ret = rtw_sdio_indirect_reg_cfg(rtwdev, addr,
					     BIT_SDIO_INDIRECT_REG_CFG_WRITE);
}

static void rtw_sdio_indirect_write16(struct rtw_dev *rtwdev, u16 val, u32 addr,
				      int *err_ret)
{
	u32 reg_data;

	if (!IS_ALIGNED(addr, 2)) {
		addr = rtw_sdio_to_io_address(rtwdev, addr, true);
		rtw_sdio_writew(rtwdev, val, addr, err_ret);
		return;
	}

	reg_data = rtw_sdio_to_bus_offset(rtwdev, REG_SDIO_INDIRECT_REG_DATA);
	rtw_sdio_writew(rtwdev, val, reg_data, err_ret);
	if (*err_ret)
		return;

	*err_ret = rtw_sdio_indirect_reg_cfg(rtwdev, addr,
					     BIT_SDIO_INDIRECT_REG_CFG_WRITE |
					     BIT_SDIO_INDIRECT_REG_CFG_WORD);
}

static void rtw_sdio_indirect_write32(struct rtw_dev *rtwdev, u32 val,
				      u32 addr, int *err_ret)
{
	u32 reg_data;

	if (!IS_ALIGNED(addr, 4)) {
		addr = rtw_sdio_to_io_address(rtwdev, addr, true);
		rtw_sdio_writel(rtwdev, val, addr, err_ret);
		return;
	}

	reg_data = rtw_sdio_to_bus_offset(rtwdev, REG_SDIO_INDIRECT_REG_DATA);
	rtw_sdio_writel(rtwdev, val, reg_data, err_ret);

	*err_ret = rtw_sdio_indirect_reg_cfg(rtwdev, addr,
					     BIT_SDIO_INDIRECT_REG_CFG_WRITE |
					     BIT_SDIO_INDIRECT_REG_CFG_DWORD);
}

static void rtw_sdio_write8(struct rtw_dev *rtwdev, u32 addr, u8 val)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	bool direct, bus_claim;
	int ret;

	direct = rtw_sdio_use_direct_io(rtwdev, addr);
	addr = rtw_sdio_to_io_address(rtwdev, addr, direct);
	bus_claim = rtw_sdio_bus_claim_needed(rtwsdio);

	if (bus_claim)
		sdio_claim_host(rtwsdio->sdio_func);

	if (direct)
		sdio_writeb(rtwsdio->sdio_func, val, addr, &ret);
	else
		rtw_sdio_indirect_write8(rtwdev, val, addr, &ret);

	if (bus_claim)
		sdio_release_host(rtwsdio->sdio_func);

	if (ret)
		rtw_warn(rtwdev, "sdio write8 failed (0x%x): %d", addr, ret);
}

static void rtw_sdio_write16(struct rtw_dev *rtwdev, u32 addr, u16 val)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	bool direct, bus_claim;
	int ret;

	direct = rtw_sdio_use_direct_io(rtwdev, addr);
	addr = rtw_sdio_to_io_address(rtwdev, addr, direct);
	bus_claim = rtw_sdio_bus_claim_needed(rtwsdio);

	if (bus_claim)
		sdio_claim_host(rtwsdio->sdio_func);

	if (direct)
		rtw_sdio_writew(rtwdev, val, addr, &ret);
	else
		rtw_sdio_indirect_write16(rtwdev, val, addr, &ret);

	if (bus_claim)
		sdio_release_host(rtwsdio->sdio_func);

	if (ret)
		rtw_warn(rtwdev, "sdio write16 failed (0x%x): %d", addr, ret);
}

static void rtw_sdio_write32(struct rtw_dev *rtwdev, u32 addr, u32 val)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	bool direct, bus_claim;
	int ret;

	direct = rtw_sdio_use_direct_io(rtwdev, addr);
	addr = rtw_sdio_to_io_address(rtwdev, addr, direct);
	bus_claim = rtw_sdio_bus_claim_needed(rtwsdio);

	if (bus_claim)
		sdio_claim_host(rtwsdio->sdio_func);

	if (direct)
		rtw_sdio_writel(rtwdev, val, addr, &ret);
	else
		rtw_sdio_indirect_write32(rtwdev, val, addr, &ret);

	if (bus_claim)
		sdio_release_host(rtwsdio->sdio_func);

	if (ret)
		rtw_warn(rtwdev, "sdio write32 failed (0x%x): %d", addr, ret);
}

static u32 rtw_sdio_get_tx_addr(struct rtw_dev *rtwdev, size_t size,
				enum rtw_tx_queue_type queue)
{
	u32 txaddr;

	switch (queue) {
	case RTW_TX_QUEUE_BCN:
	case RTW_TX_QUEUE_H2C:
	case RTW_TX_QUEUE_HI0:
		txaddr = FIELD_PREP(REG_SDIO_CMD_ADDR_MSK,
				    REG_SDIO_CMD_ADDR_TXFF_HIGH);
		break;
	case RTW_TX_QUEUE_VI:
	case RTW_TX_QUEUE_VO:
		txaddr = FIELD_PREP(REG_SDIO_CMD_ADDR_MSK,
				    REG_SDIO_CMD_ADDR_TXFF_NORMAL);
		break;
	case RTW_TX_QUEUE_BE:
	case RTW_TX_QUEUE_BK:
		txaddr = FIELD_PREP(REG_SDIO_CMD_ADDR_MSK,
				    REG_SDIO_CMD_ADDR_TXFF_LOW);
		break;
	case RTW_TX_QUEUE_MGMT:
		txaddr = FIELD_PREP(REG_SDIO_CMD_ADDR_MSK,
				    REG_SDIO_CMD_ADDR_TXFF_EXTRA);
		break;
	default:
		rtw_warn(rtwdev, "Unsupported queue for TX addr: 0x%02x\n",
			 queue);
		return 0;
	}

	txaddr += DIV_ROUND_UP(size, 4);

	return txaddr;
};

static int rtw_sdio_read_port(struct rtw_dev *rtwdev, u8 *buf, size_t count)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	struct mmc_host *host = rtwsdio->sdio_func->card->host;
	bool bus_claim = rtw_sdio_bus_claim_needed(rtwsdio);
	u32 rxaddr = rtwsdio->rx_addr++;
	int ret = 0, err;
	size_t bytes;

	if (bus_claim)
		sdio_claim_host(rtwsdio->sdio_func);

	while (count > 0) {
		bytes = min_t(size_t, host->max_req_size, count);

		err = sdio_memcpy_fromio(rtwsdio->sdio_func, buf,
					 RTW_SDIO_ADDR_RX_RX0FF_GEN(rxaddr),
					 bytes);
		if (err) {
			rtw_warn(rtwdev,
				 "Failed to read %zu byte(s) from SDIO port 0x%08x: %d",
				 bytes, rxaddr, err);

			 /* Signal to the caller that reading did not work and
			  * that the data in the buffer is short/corrupted.
			  */
			ret = err;

			/* Don't stop here - instead drain the remaining data
			 * from the card's buffer, else the card will return
			 * corrupt data for the next rtw_sdio_read_port() call.
			 */
		}

		count -= bytes;
		buf += bytes;
	}

	if (bus_claim)
		sdio_release_host(rtwsdio->sdio_func);

	return ret;
}

/*
 * The counters are atomic_t because rtw_sdio_start() seeds them outside
 * tx_credit_lock, which the transmit paths hold while using them.
 */
static void rtw_sdio_8723bs_store_free_txpg(struct rtw_dev *rtwdev,
					    u32 free_txpg)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;

	atomic_set(&rtwsdio->free_pg_high,
		   u32_get_bits(free_txpg, BIT_FREE_TXPG_HIGH));
	atomic_set(&rtwsdio->free_pg_normal,
		   u32_get_bits(free_txpg, BIT_FREE_TXPG_NORMAL));
	atomic_set(&rtwsdio->free_pg_low,
		   u32_get_bits(free_txpg, BIT_FREE_TXPG_LOW));
	atomic_set(&rtwsdio->free_pg_pub,
		   u32_get_bits(free_txpg, BIT_FREE_TXPG_PUB));
}

static bool rtw_sdio_8723bs_sync_free_txpg(struct rtw_dev *rtwdev)
{
	u32 free_txpg = rtw_read32(rtwdev, REG_SDIO_FREE_TXPG);

	if (!free_txpg)
		return false;

	rtw_sdio_8723bs_store_free_txpg(rtwdev, free_txpg);

	return true;
}

static int rtw_sdio_8723bs_pubq_num(struct rtw_dev *rtwdev, u16 *pubq_num)
{
	const struct rtw_page_table *pg_tbl = &rtwdev->chip->page_table[0];
	u16 acq_pg_num = rtwdev->fifo.acq_pg_num;
	u16 reserved_num;

	reserved_num = pg_tbl->hq_num + pg_tbl->lq_num + pg_tbl->nq_num +
		       pg_tbl->exq_num + pg_tbl->gapq_num;
	if (acq_pg_num <= reserved_num) {
		rtw_err(rtwdev,
			"no transmit pages left for the public queue: %u of %u reserved\n",
			reserved_num, acq_pg_num);
		return -EINVAL;
	}

	*pubq_num = acq_pg_num - reserved_num;

	return 0;
}

static int rtw_sdio_8723bs_init_free_txpg(struct rtw_dev *rtwdev)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	const struct rtw_page_table *pg_tbl;
	u16 pubq_num;
	int ret;

	/* Seed from the page table when the chip has nothing to report yet. */
	if (!rtw_sdio_8723bs_sync_free_txpg(rtwdev)) {
		ret = rtw_sdio_8723bs_pubq_num(rtwdev, &pubq_num);
		if (ret)
			return ret;

		pg_tbl = &rtwdev->chip->page_table[0];
		atomic_set(&rtwsdio->free_pg_high, pg_tbl->hq_num);
		atomic_set(&rtwsdio->free_pg_normal, pg_tbl->nq_num);
		atomic_set(&rtwsdio->free_pg_low, pg_tbl->lq_num);
		atomic_set(&rtwsdio->free_pg_pub, pubq_num);
	}

	atomic_set(&rtwsdio->tx_oqt_free,
		   rtw_read8(rtwdev, REG_SDIO_OQT_FREE_PG));

	return 0;
}

/*
 * Clamped at zero: a lost update against rtw_sdio_8723bs_consume_txpg() can
 * briefly drive a counter negative, and wrapping would hide the shortage
 * instead of triggering a resync from the chip.
 */
static unsigned int rtw_sdio_8723bs_pages_free(struct rtw_dev *rtwdev,
					       atomic_t *dedicated)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	int free;

	free = atomic_read(dedicated) + atomic_read(&rtwsdio->free_pg_pub);

	return free > 0 ? free : 0;
}

/*
 * The pool a queue draws from follows the transmit FIFO that
 * rtw_sdio_get_tx_addr() writes into, since that is the one the chip charges.
 * RTW_TX_QUEUE_MGMT is the exception: it goes to the extra FIFO, which
 * REG_SDIO_FREE_TXPG has no counter for and this chip allocates no pages to,
 * so it is accounted against the high pool and served from the public one.
 */
static atomic_t *rtw_sdio_8723bs_free_txpg(struct rtw_dev *rtwdev, u8 queue)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;

	switch (queue) {
	case RTW_TX_QUEUE_VI:
	case RTW_TX_QUEUE_VO:
		return &rtwsdio->free_pg_normal;
	case RTW_TX_QUEUE_BE:
	case RTW_TX_QUEUE_BK:
		return &rtwsdio->free_pg_low;
	case RTW_TX_QUEUE_BCN:
	case RTW_TX_QUEUE_H2C:
	case RTW_TX_QUEUE_HI0:
	case RTW_TX_QUEUE_MGMT:
		return &rtwsdio->free_pg_high;
	default:
		return NULL;
	}
}

static int rtw_sdio_check_free_txpg(struct rtw_dev *rtwdev, u8 queue,
				    size_t count)
{
	unsigned int pages_free, pages_needed;

	if (rtw_is_8723bs(rtwdev)) {
		atomic_t *dedicated;

		dedicated = rtw_sdio_8723bs_free_txpg(rtwdev, queue);
		if (!dedicated) {
			rtw_warn(rtwdev, "Unknown mapping for queue %u\n", queue);
			return -EINVAL;
		}

		pages_free = rtw_sdio_8723bs_pages_free(rtwdev, dedicated);
		pages_needed = DIV_ROUND_UP(count, rtwdev->chip->page_size);
		if (pages_needed <= pages_free)
			return 0;

		rtw_sdio_8723bs_sync_free_txpg(rtwdev);
		pages_free = rtw_sdio_8723bs_pages_free(rtwdev, dedicated);
	} else if (rtw_chip_wcpu_8051(rtwdev)) {
		u32 free_txpg;

		free_txpg = rtw_sdio_read32(rtwdev, REG_SDIO_FREE_TXPG);

		switch (queue) {
		case RTW_TX_QUEUE_BCN:
		case RTW_TX_QUEUE_H2C:
		case RTW_TX_QUEUE_HI0:
		case RTW_TX_QUEUE_MGMT:
			/* high */
			pages_free = free_txpg & 0xff;
			break;
		case RTW_TX_QUEUE_VI:
		case RTW_TX_QUEUE_VO:
			/* normal */
			pages_free = (free_txpg >> 8) & 0xff;
			break;
		case RTW_TX_QUEUE_BE:
		case RTW_TX_QUEUE_BK:
			/* low */
			pages_free = (free_txpg >> 16) & 0xff;
			break;
		default:
			rtw_warn(rtwdev, "Unknown mapping for queue %u\n", queue);
			return -EINVAL;
		}

		/* add the pages from the public queue */
		pages_free += (free_txpg >> 24) & 0xff;
	} else {
		u32 free_txpg[3];

		free_txpg[0] = rtw_sdio_read32(rtwdev, REG_SDIO_FREE_TXPG);
		free_txpg[1] = rtw_sdio_read32(rtwdev, REG_SDIO_FREE_TXPG + 4);
		free_txpg[2] = rtw_sdio_read32(rtwdev, REG_SDIO_FREE_TXPG + 8);

		switch (queue) {
		case RTW_TX_QUEUE_BCN:
		case RTW_TX_QUEUE_H2C:
		case RTW_TX_QUEUE_HI0:
			/* high */
			pages_free = free_txpg[0] & 0xfff;
			break;
		case RTW_TX_QUEUE_VI:
		case RTW_TX_QUEUE_VO:
			/* normal */
			pages_free = (free_txpg[0] >> 16) & 0xfff;
			break;
		case RTW_TX_QUEUE_BE:
		case RTW_TX_QUEUE_BK:
			/* low */
			pages_free = free_txpg[1] & 0xfff;
			break;
		case RTW_TX_QUEUE_MGMT:
			/* extra */
			pages_free = free_txpg[2] & 0xfff;
			break;
		default:
			rtw_warn(rtwdev, "Unknown mapping for queue %u\n", queue);
			return -EINVAL;
		}

		/* add the pages from the public queue */
		pages_free += (free_txpg[1] >> 16) & 0xfff;
	}

	pages_needed = DIV_ROUND_UP(count, rtwdev->chip->page_size);

	if (pages_needed > pages_free) {
		rtw_dbg(rtwdev, RTW_DBG_SDIO,
			"Not enough free pages (%u needed, %u free) in queue %u for %zu bytes\n",
			pages_needed, pages_free, queue, count);
		return -EBUSY;
	}

	return 0;
}

static int rtw_sdio_8723bs_wait_tx_oqt(struct rtw_dev *rtwdev)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	unsigned long deadline;
	u8 free;

	lockdep_assert_held(&rtwsdio->tx_credit_lock);

	if (atomic_add_unless(&rtwsdio->tx_oqt_free, -1, 0))
		return 0;

	deadline = jiffies + msecs_to_jiffies(RTW_SDIO_OQT_TIMEOUT_MS);
	do {
		free = rtw_read8(rtwdev, REG_SDIO_OQT_FREE_PG);
		if (free) {
			atomic_set(&rtwsdio->tx_oqt_free, free - 1);
			return 0;
		}
		usleep_range(1000, 2000);
	} while (time_before(jiffies, deadline));

	return -EBUSY;
}

static void rtw_sdio_8723bs_consume_txpg(struct rtw_dev *rtwdev, u8 queue,
					 unsigned int pages)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	atomic_t *dedicated;
	unsigned int taken;
	int free;

	lockdep_assert_held(&rtwsdio->tx_credit_lock);

	dedicated = rtw_sdio_8723bs_free_txpg(rtwdev, queue);
	if (!dedicated)
		return;

	free = atomic_read(dedicated);
	taken = min_t(unsigned int, pages, free > 0 ? free : 0);
	atomic_sub(taken, dedicated);

	pages -= taken;
	if (pages && atomic_sub_return(pages, &rtwsdio->free_pg_pub) < 0)
		atomic_set(&rtwsdio->free_pg_pub, 0);
}

static int rtw_sdio_write_to_port(struct rtw_dev *rtwdev, struct sk_buff *skb,
				  enum rtw_tx_queue_type queue, u32 txaddr,
				  size_t write_size)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	bool bus_claim;
	int ret;

	if (!IS_ALIGNED((unsigned long)skb->data, RTW_SDIO_DATA_PTR_ALIGN))
		rtw_warn(rtwdev, "Got unaligned SKB in %s() for queue %u\n",
			 __func__, queue);

	bus_claim = rtw_sdio_bus_claim_needed(rtwsdio);

	if (bus_claim)
		sdio_claim_host(rtwsdio->sdio_func);

	ret = sdio_memcpy_toio(rtwsdio->sdio_func, txaddr, skb->data,
			       write_size);

	if (bus_claim)
		sdio_release_host(rtwsdio->sdio_func);

	if (ret)
		rtw_warn(rtwdev,
			 "Failed to write %zu byte(s) to SDIO port 0x%08x",
			 write_size, txaddr);

	return ret;
}

static int rtw_sdio_write_port_8723bs(struct rtw_dev *rtwdev,
				      struct sk_buff *skb,
				      enum rtw_tx_queue_type queue, u32 txaddr,
				      size_t write_size)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	unsigned int pages;
	size_t txsize;
	int ret;

	/* the chip charges pages by frame length, not by the padded transfer */
	txsize = round_up(skb->len, 4);

	guard(mutex)(&rtwsdio->tx_credit_lock);

	ret = rtw_sdio_check_free_txpg(rtwdev, queue, txsize);
	if (ret)
		return ret;

	ret = rtw_sdio_8723bs_wait_tx_oqt(rtwdev);
	if (ret)
		return ret;

	ret = rtw_sdio_write_to_port(rtwdev, skb, queue, txaddr, write_size);
	if (ret) {
		/* nothing was queued, so hand the output queue entry back */
		atomic_inc(&rtwsdio->tx_oqt_free);
		return ret;
	}

	pages = DIV_ROUND_UP(txsize, rtwdev->chip->page_size);
	rtw_sdio_8723bs_consume_txpg(rtwdev, queue, pages);

	return 0;
}

static int rtw_sdio_write_port_generic(struct rtw_dev *rtwdev,
				       struct sk_buff *skb,
				       enum rtw_tx_queue_type queue, u32 txaddr,
				       size_t write_size)
{
	int ret;

	ret = rtw_sdio_check_free_txpg(rtwdev, queue, write_size);
	if (ret)
		return ret;

	return rtw_sdio_write_to_port(rtwdev, skb, queue, txaddr, write_size);
}

static int rtw_sdio_write_port(struct rtw_dev *rtwdev, struct sk_buff *skb,
			       enum rtw_tx_queue_type queue)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	size_t write_size;
	size_t pad_size;
	u32 txaddr;
	int ret;

	txaddr = rtw_sdio_get_tx_addr(rtwdev, skb->len, queue);
	if (!txaddr)
		return -EINVAL;

	write_size = sdio_align_size(rtwsdio->sdio_func, skb->len);
	pad_size = write_size - skb->len;

	if (pad_size > 0) {
		/*
		 * __skb_pad() must not free the skb on failure: both callers
		 * still own it, one requeues it and the other frees it.
		 */
		ret = __skb_pad(skb, pad_size, false);
		if (ret)
			return ret;
	}

	if (rtw_is_8723bs(rtwdev))
		return rtw_sdio_write_port_8723bs(rtwdev, skb, queue, txaddr,
						  write_size);

	return rtw_sdio_write_port_generic(rtwdev, skb, queue, txaddr,
					   write_size);
}

static void rtw_sdio_init(struct rtw_dev *rtwdev)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;

	if (rtw_is_8723bs(rtwdev))
		rtwsdio->irq_mask = REG_SDIO_HIMR_RX_REQUEST;
	else
		rtwsdio->irq_mask = REG_SDIO_HIMR_RX_REQUEST |
				    REG_SDIO_HIMR_CPWM1;
}

static void rtw_sdio_enable_rx_aggregation(struct rtw_dev *rtwdev)
{
	u8 size, timeout;

	switch (rtwdev->chip->id) {
	case RTW_CHIP_TYPE_8723B:
	case RTW_CHIP_TYPE_8703B:
	case RTW_CHIP_TYPE_8821A:
	case RTW_CHIP_TYPE_8812A:
		size = 0x6;
		timeout = 0x6;
		break;
	case RTW_CHIP_TYPE_8723D:
		size = 0xa;
		timeout = 0x3;
		rtw_write8_set(rtwdev, REG_RXDMA_AGG_PG_TH + 3, BIT(7));
		break;
	default:
		size = 0xff;
		timeout = 0x1;
		break;
	}

	/* Make the firmware honor the size limit configured below */
	rtw_write32_set(rtwdev, REG_RXDMA_AGG_PG_TH, BIT_EN_PRE_CALC);

	rtw_write8_set(rtwdev, REG_TXDMA_PQ_MAP, BIT_RXDMA_AGG_EN);

	rtw_write16(rtwdev, REG_RXDMA_AGG_PG_TH,
		    FIELD_PREP(BIT_RXDMA_AGG_PG_TH, size) |
		    FIELD_PREP(BIT_DMA_AGG_TO_V1, timeout));

	rtw_write8_set(rtwdev, REG_RXDMA_MODE, BIT_DMA_MODE);
	if (rtw_is_8723bs(rtwdev))
		rtw_write8_mask(rtwdev, REG_RXDMA_MODE, BIT_DMA_BURST_CNT, 0x3);
}

static void rtw_sdio_enable_interrupt(struct rtw_dev *rtwdev)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;

	rtw_write32(rtwdev, REG_SDIO_HIMR, rtwsdio->irq_mask);
}

static void rtw_sdio_disable_interrupt(struct rtw_dev *rtwdev)
{
	rtw_write32(rtwdev, REG_SDIO_HIMR, 0x0);
}

static u8 rtw_sdio_get_tx_qsel(struct rtw_dev *rtwdev, struct sk_buff *skb,
			       u8 queue)
{
	switch (queue) {
	case RTW_TX_QUEUE_BCN:
		return TX_DESC_QSEL_BEACON;
	case RTW_TX_QUEUE_H2C:
		return TX_DESC_QSEL_H2C;
	case RTW_TX_QUEUE_MGMT:
		return TX_DESC_QSEL_MGMT;
	case RTW_TX_QUEUE_HI0:
		return TX_DESC_QSEL_HIGH;
	default:
		return skb->priority;
	}
}

static int rtw_sdio_setup(struct rtw_dev *rtwdev)
{
	/* nothing to do */
	return 0;
}

/*
 * Reprogram the queue page allocation if the chip came up with none. This is
 * a repair path, not part of the normal start sequence: a non-zero free page
 * count means the allocation latched during power on and must be left alone,
 * and without a transmit page pool there is nothing to divide up either.
 */
static int rtw_sdio_8723bs_check_rqpn(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_fifo_conf *fifo = &rtwdev->fifo;
	const struct rtw_page_table *pg_tbl;
	u32 free_txpg;
	u16 pubq_num;
	int ret;

	free_txpg = rtw_read32(rtwdev, REG_SDIO_FREE_TXPG);
	if (free_txpg || !fifo->acq_pg_num)
		return 0;

	ret = rtw_sdio_8723bs_pubq_num(rtwdev, &pubq_num);
	if (ret)
		return ret;

	pg_tbl = &chip->page_table[0];
	rtw_write32(rtwdev, REG_RQPN_NPQ,
		    BIT_RQPN_NE(pg_tbl->nq_num, pg_tbl->exq_num));
	rtw_write32(rtwdev, REG_RQPN,
		    BIT_RQPN_HLP(pg_tbl->hq_num, pg_tbl->lq_num, pubq_num));

	return 0;
}

static int rtw_sdio_start(struct rtw_dev *rtwdev)
{
	u32 clear;

	if (rtw_is_8723bs(rtwdev)) {
		int ret = rtw_sdio_8723bs_check_rqpn(rtwdev);

		if (ret)
			return ret;

		ret = rtw_sdio_8723bs_init_free_txpg(rtwdev);
		if (ret)
			return ret;
	}

	rtw_sdio_enable_rx_aggregation(rtwdev);

	if (rtw_is_8723bs(rtwdev)) {
		clear = rtw_read32(rtwdev, REG_SDIO_HISR) & RTW_SDIO_HISR_CLEAR_MASK;
		if (clear)
			rtw_write32(rtwdev, REG_SDIO_HISR, clear);
	}

	rtw_sdio_enable_interrupt(rtwdev);

	return 0;
}

static void rtw_sdio_stop(struct rtw_dev *rtwdev)
{
	rtw_sdio_disable_interrupt(rtwdev);
}

static void rtw_sdio_deep_ps_enter(struct rtw_dev *rtwdev)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	bool tx_empty = true;
	u8 queue;

	if (!rtw_fw_feature_check(&rtwdev->fw, FW_FEATURE_TX_WAKE)) {
		/* Deep PS state is not allowed to TX-DMA */
		for (queue = 0; queue < RTK_MAX_TX_QUEUE_NUM; queue++) {
			/* BCN queue is rsvd page, does not have DMA interrupt
			 * H2C queue is managed by firmware
			 */
			if (queue == RTW_TX_QUEUE_BCN ||
			    queue == RTW_TX_QUEUE_H2C)
				continue;

			/* check if there is any skb DMAing */
			if (skb_queue_len(&rtwsdio->tx_queue[queue])) {
				tx_empty = false;
				break;
			}
		}
	}

	if (!tx_empty) {
		rtw_dbg(rtwdev, RTW_DBG_PS,
			"TX path not empty, cannot enter deep power save state\n");
		return;
	}

	set_bit(RTW_FLAG_LEISURE_PS_DEEP, rtwdev->flags);
	rtw_power_mode_change(rtwdev, true);
}

static void rtw_sdio_deep_ps_leave(struct rtw_dev *rtwdev)
{
	if (test_and_clear_bit(RTW_FLAG_LEISURE_PS_DEEP, rtwdev->flags))
		rtw_power_mode_change(rtwdev, false);
}

static void rtw_sdio_deep_ps(struct rtw_dev *rtwdev, bool enter)
{
	if (enter && !test_bit(RTW_FLAG_LEISURE_PS_DEEP, rtwdev->flags))
		rtw_sdio_deep_ps_enter(rtwdev);

	if (!enter && test_bit(RTW_FLAG_LEISURE_PS_DEEP, rtwdev->flags))
		rtw_sdio_deep_ps_leave(rtwdev);
}

static void rtw_sdio_tx_kick_off(struct rtw_dev *rtwdev)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;

	/*
	 * A retry may already be pending with a delay; re-arm it so a newly
	 * queued frame is not held back by it.
	 */
	mod_delayed_work(rtwsdio->txwq, &rtwsdio->tx_handler_data->work, 0);
}

static void rtw_sdio_link_ps(struct rtw_dev *rtwdev, bool enter)
{
	/* nothing to do */
}

static void rtw_sdio_interface_cfg(struct rtw_dev *rtwdev)
{
	u32 val;

	rtw_read32(rtwdev, REG_SDIO_FREE_TXPG);

	val = rtw_read32(rtwdev, REG_SDIO_TX_CTRL);
	val &= 0xfff8;
	if (rtw_is_8723bs(rtwdev))
		val |= BIT_SDIO_TX_CTRL_ALWAYS_RECOGNIZE;
	rtw_write32(rtwdev, REG_SDIO_TX_CTRL, val);
}

static struct rtw_sdio_tx_data *rtw_sdio_get_tx_data(struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	BUILD_BUG_ON(sizeof(struct rtw_sdio_tx_data) >
		     sizeof(info->status.status_driver_data));

	return (struct rtw_sdio_tx_data *)info->status.status_driver_data;
}

static void rtw_sdio_tx_skb_prepare(struct rtw_dev *rtwdev,
				    struct rtw_tx_pkt_info *pkt_info,
				    struct sk_buff *skb,
				    enum rtw_tx_queue_type queue)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	unsigned long data_addr, aligned_addr;
	size_t offset;
	u8 *pkt_desc;

	pkt_desc = skb_push(skb, chip->tx_pkt_desc_sz);

	data_addr = (unsigned long)pkt_desc;
	aligned_addr = ALIGN(data_addr, RTW_SDIO_DATA_PTR_ALIGN);

	if (data_addr != aligned_addr) {
		/* Ensure that the start of the pkt_desc is always aligned at
		 * RTW_SDIO_DATA_PTR_ALIGN.
		 */
		offset = RTW_SDIO_DATA_PTR_ALIGN - (aligned_addr - data_addr);

		pkt_desc = skb_push(skb, offset);

		/* By inserting padding to align the start of the pkt_desc we
		 * need to inform the firmware that the actual data starts at
		 * a different offset than normal.
		 */
		pkt_info->offset += offset;
	}

	memset(pkt_desc, 0, chip->tx_pkt_desc_sz);

	pkt_info->qsel = rtw_sdio_get_tx_qsel(rtwdev, skb, queue);

	rtw_tx_fill_tx_desc(rtwdev, pkt_info, skb);
	rtw_tx_fill_txdesc_checksum(rtwdev, pkt_info, pkt_desc);
}

static int rtw_sdio_write_data(struct rtw_dev *rtwdev,
			       struct rtw_tx_pkt_info *pkt_info,
			       struct sk_buff *skb,
			       enum rtw_tx_queue_type queue)
{
	int ret;

	rtw_sdio_tx_skb_prepare(rtwdev, pkt_info, skb, queue);

	ret = rtw_sdio_write_port(rtwdev, skb, queue);
	dev_kfree_skb_any(skb);

	return ret;
}

static int rtw_sdio_write_data_rsvd_page(struct rtw_dev *rtwdev, u8 *buf,
					 u32 size)
{
	struct rtw_tx_pkt_info pkt_info = {};
	struct sk_buff *skb;

	skb = rtw_tx_write_data_rsvd_page_get(rtwdev, &pkt_info, buf, size);
	if (!skb)
		return -ENOMEM;

	return rtw_sdio_write_data(rtwdev, &pkt_info, skb, RTW_TX_QUEUE_BCN);
}

static int rtw_sdio_write_data_h2c(struct rtw_dev *rtwdev, u8 *buf, u32 size)
{
	struct rtw_tx_pkt_info pkt_info = {};
	struct sk_buff *skb;

	skb = rtw_tx_write_data_h2c_get(rtwdev, &pkt_info, buf, size);
	if (!skb)
		return -ENOMEM;

	return rtw_sdio_write_data(rtwdev, &pkt_info, skb, RTW_TX_QUEUE_H2C);
}

/*
 * Both this and the wake below hold the TX queue lock across the length check
 * and the flag update, so a queue is only ever stopped while it really is
 * above the watermark, and the drain path always sees the flag the producer
 * set.
 */
static void rtw_sdio_8723bs_stop_tx_queue(struct rtw_dev *rtwdev,
					  enum rtw_tx_queue_type queue,
					  u16 q_map)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;

	if (!rtw_is_8723bs(rtwdev) || queue >= RTW_TX_QUEUE_BCN)
		return;

	guard(spinlock_irqsave)(&rtwsdio->tx_queue[queue].lock);

	if (rtwsdio->tx_queue_stopped[queue])
		return;

	if (skb_queue_len(&rtwsdio->tx_queue[queue]) < RTW_SDIO_TX_FIFO_HIWATER)
		return;

	rtwsdio->tx_queue_stopped[queue] = true;
	ieee80211_stop_queue(rtwdev->hw, q_map);
}

static void rtw_sdio_8723bs_wake_tx_queue(struct rtw_dev *rtwdev,
					  enum rtw_tx_queue_type queue,
					  u16 q_map)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;

	if (!rtw_is_8723bs(rtwdev) || queue >= RTW_TX_QUEUE_BCN)
		return;

	guard(spinlock_irqsave)(&rtwsdio->tx_queue[queue].lock);

	if (!rtwsdio->tx_queue_stopped[queue])
		return;

	if (skb_queue_len(&rtwsdio->tx_queue[queue]) > RTW_SDIO_TX_FIFO_LOWATER)
		return;

	rtwsdio->tx_queue_stopped[queue] = false;
	ieee80211_wake_queue(rtwdev->hw, q_map);
}

static int rtw_sdio_tx_write(struct rtw_dev *rtwdev,
			     struct rtw_tx_pkt_info *pkt_info,
			     struct sk_buff *skb)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	enum rtw_tx_queue_type queue = rtw_tx_queue_mapping(skb);
	u16 q_map = skb_get_queue_mapping(skb);
	struct rtw_sdio_tx_data *tx_data;

	rtw_sdio_tx_skb_prepare(rtwdev, pkt_info, skb, queue);

	tx_data = rtw_sdio_get_tx_data(skb);
	tx_data->sn = pkt_info->sn;

	/* The TX worker may already have freed the skb, so do not touch it. */
	skb_queue_tail(&rtwsdio->tx_queue[queue], skb);

	rtw_sdio_8723bs_stop_tx_queue(rtwdev, queue, q_map);

	return 0;
}

static void rtw_sdio_tx_err_isr(struct rtw_dev *rtwdev)
{
	u32 val = rtw_read32(rtwdev, REG_TXDMA_STATUS);

	rtw_write32(rtwdev, REG_TXDMA_STATUS, val);
}

static void rtw_sdio_rx_skb(struct rtw_dev *rtwdev, struct sk_buff *skb,
			    u32 pkt_offset, struct rtw_rx_pkt_stat *pkt_stat,
			    struct ieee80211_rx_status *rx_status)
{
	*IEEE80211_SKB_RXCB(skb) = *rx_status;

	if (pkt_stat->is_c2h) {
		skb_put(skb, pkt_stat->pkt_len + pkt_offset);
		rtw_fw_c2h_cmd_rx_irqsafe(rtwdev, pkt_offset, skb);
		return;
	}

	skb_put(skb, pkt_stat->pkt_len);
	skb_reserve(skb, pkt_offset);

	rtw_update_rx_freq_for_invalid(rtwdev, skb, rx_status, pkt_stat);
	rtw_rx_stats(rtwdev, pkt_stat->vif, skb);

	ieee80211_rx_irqsafe(rtwdev->hw, skb);
}

static void rtw_sdio_rxfifo_recv(struct rtw_dev *rtwdev, u32 rx_len)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	const struct rtw_chip_info *chip = rtwdev->chip;
	u32 pkt_desc_sz = chip->rx_pkt_desc_sz;
	struct ieee80211_rx_status rx_status;
	struct rtw_rx_pkt_stat pkt_stat;
	struct sk_buff *skb, *split_skb;
	u32 pkt_offset, curr_pkt_len;
	size_t bufsz;
	u8 *rx_desc;
	int ret;

	bufsz = sdio_align_size(rtwsdio->sdio_func, rx_len);

	skb = dev_alloc_skb(bufsz);
	if (!skb)
		return;

	ret = rtw_sdio_read_port(rtwdev, skb->data, bufsz);
	if (ret) {
		dev_kfree_skb_any(skb);
		return;
	}

	while (true) {
		rx_desc = skb->data;
		ret = rtw_rx_query_rx_desc(rtwdev, rx_desc,
					   &pkt_stat, &rx_status);
		if (ret) {
			dev_kfree_skb_any(skb);
			return;
		}

		pkt_offset = pkt_desc_sz + pkt_stat.drv_info_sz +
			     pkt_stat.shift;

		curr_pkt_len = ALIGN(pkt_offset + pkt_stat.pkt_len,
				     RTW_SDIO_DATA_PTR_ALIGN);

		if ((curr_pkt_len + pkt_desc_sz) >= rx_len) {
			/* Use the original skb (with it's adjusted offset)
			 * when processing the last (or even the only) entry to
			 * have it's memory freed automatically.
			 */
			rtw_sdio_rx_skb(rtwdev, skb, pkt_offset, &pkt_stat,
					&rx_status);
			break;
		}

		split_skb = dev_alloc_skb(curr_pkt_len);
		if (!split_skb) {
			rtw_sdio_rx_skb(rtwdev, skb, pkt_offset, &pkt_stat,
					&rx_status);
			break;
		}

		skb_copy_header(split_skb, skb);
		memcpy(split_skb->data, skb->data, curr_pkt_len);

		rtw_sdio_rx_skb(rtwdev, split_skb, pkt_offset, &pkt_stat,
				&rx_status);

		/* Move to the start of the next RX descriptor */
		skb_reserve(skb, curr_pkt_len);
		rx_len -= curr_pkt_len;
	}
}

static void rtw_sdio_rx_isr(struct rtw_dev *rtwdev)
{
	u32 rx_len, hisr, total_rx_bytes = 0;

	do {
		if (rtw_chip_wcpu_8051(rtwdev))
			rx_len = rtw_read16(rtwdev, REG_SDIO_RX0_REQ_LEN);
		else
			rx_len = rtw_read32(rtwdev, REG_SDIO_RX0_REQ_LEN);

		if (!rx_len)
			break;

		rtw_sdio_rxfifo_recv(rtwdev, rx_len);

		total_rx_bytes += rx_len;

		if (rtw_chip_wcpu_8051(rtwdev)) {
			/* Stop if no more RX requests are pending, even if
			 * rx_len could be greater than zero in the next
			 * iteration. This is needed because the RX buffer may
			 * already contain data while either HW or FW are not
			 * done filling that buffer yet. Still reading the
			 * buffer can result in packets where
			 * rtw_rx_pkt_stat.pkt_len is zero or points beyond the
			 * end of the buffer.
			 */
			hisr = rtw_read32(rtwdev, REG_SDIO_HISR);
		} else {
			/* RTW_WCPU_3081 chips have improved hardware or
			 * firmware and can use rx_len unconditionally.
			 */
			hisr = REG_SDIO_HISR_RX_REQUEST;
		}
	} while (total_rx_bytes < SZ_64K && hisr & REG_SDIO_HISR_RX_REQUEST);
}

static void rtw_sdio_handle_interrupt(struct sdio_func *sdio_func)
{
	struct ieee80211_hw *hw = sdio_get_drvdata(sdio_func);
	struct rtw_sdio *rtwsdio;
	struct rtw_dev *rtwdev;
	u32 hisr;

	rtwdev = hw->priv;
	rtwsdio = (struct rtw_sdio *)rtwdev->priv;

	rtwsdio->irq_thread = current;

	hisr = rtw_read32(rtwdev, REG_SDIO_HISR);

	if (hisr & REG_SDIO_HISR_TXERR)
		rtw_sdio_tx_err_isr(rtwdev);
	if (hisr & REG_SDIO_HISR_RX_REQUEST) {
		hisr &= ~REG_SDIO_HISR_RX_REQUEST;
		rtw_sdio_rx_isr(rtwdev);
	}

	/*
	 * RTL8723BS keeps raising the interrupt after resume if undefined
	 * status bits are written back, so acknowledge only the bits this
	 * driver defines. Other chips keep the existing behaviour.
	 */
	if (rtw_is_8723bs(rtwdev))
		hisr &= RTW_SDIO_HISR_CLEAR_MASK;

	rtw_write32(rtwdev, REG_SDIO_HISR, hisr);

	rtwsdio->irq_thread = NULL;
}

static int __maybe_unused rtw_sdio_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct ieee80211_hw *hw = dev_get_drvdata(dev);
	struct rtw_dev *rtwdev = hw->priv;
	int ret;

	ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	if (ret)
		rtw_err(rtwdev, "Failed to host PM flag MMC_PM_KEEP_POWER");

	return ret;
}

static int __maybe_unused rtw_sdio_resume(struct device *dev)
{
	return 0;
}

SIMPLE_DEV_PM_OPS(rtw_sdio_pm_ops, rtw_sdio_suspend, rtw_sdio_resume);
EXPORT_SYMBOL(rtw_sdio_pm_ops);

static int rtw_sdio_claim(struct rtw_dev *rtwdev, struct sdio_func *sdio_func)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	int ret;

	sdio_claim_host(sdio_func);

	ret = sdio_enable_func(sdio_func);
	if (ret) {
		rtw_err(rtwdev, "Failed to enable SDIO func");
		goto err_release_host;
	}

	ret = sdio_set_block_size(sdio_func, RTW_SDIO_BLOCK_SIZE);
	if (ret) {
		rtw_err(rtwdev, "Failed to set SDIO block size to 512");
		goto err_disable_func;
	}

	rtwsdio->sdio_func = sdio_func;

	rtwsdio->sdio3_bus_mode = mmc_card_uhs(sdio_func->card);

	sdio_set_drvdata(sdio_func, rtwdev->hw);
	SET_IEEE80211_DEV(rtwdev->hw, &sdio_func->dev);

	sdio_release_host(sdio_func);

	return 0;

err_disable_func:
	sdio_disable_func(sdio_func);
err_release_host:
	sdio_release_host(sdio_func);
	return ret;
}

static void rtw_sdio_declaim(struct rtw_dev *rtwdev,
			     struct sdio_func *sdio_func)
{
	sdio_claim_host(sdio_func);
	sdio_disable_func(sdio_func);
	sdio_release_host(sdio_func);
}

static const struct rtw_hci_ops rtw_sdio_ops = {
	.tx_write = rtw_sdio_tx_write,
	.tx_kick_off = rtw_sdio_tx_kick_off,
	.setup = rtw_sdio_setup,
	.start = rtw_sdio_start,
	.stop = rtw_sdio_stop,
	.deep_ps = rtw_sdio_deep_ps,
	.link_ps = rtw_sdio_link_ps,
	.interface_cfg = rtw_sdio_interface_cfg,
	.dynamic_rx_agg = NULL,
	.write_firmware_page = rtw_write_firmware_page,

	.read8 = rtw_sdio_read8,
	.read16 = rtw_sdio_read16,
	.read32 = rtw_sdio_read32,
	.write8 = rtw_sdio_write8,
	.write16 = rtw_sdio_write16,
	.write32 = rtw_sdio_write32,
	.write_data_rsvd_page = rtw_sdio_write_data_rsvd_page,
	.write_data_h2c = rtw_sdio_write_data_h2c,
};

static int rtw_sdio_request_irq(struct rtw_dev *rtwdev,
				struct sdio_func *sdio_func)
{
	int ret;

	sdio_claim_host(sdio_func);
	ret = sdio_claim_irq(sdio_func, &rtw_sdio_handle_interrupt);
	sdio_release_host(sdio_func);

	if (ret) {
		rtw_err(rtwdev, "failed to claim SDIO IRQ");
		return ret;
	}

	return 0;
}

static void rtw_sdio_indicate_tx_status(struct rtw_dev *rtwdev,
					struct sk_buff *skb)
{
	struct rtw_sdio_tx_data *tx_data = rtw_sdio_get_tx_data(skb);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hw *hw = rtwdev->hw;

	skb_pull(skb, rtwdev->chip->tx_pkt_desc_sz);

	/* enqueue to wait for tx report */
	if (info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS) {
		rtw_tx_report_enqueue(rtwdev, skb, tx_data->sn);
		return;
	}

	/* always ACK for others, then they won't be marked as drop */
	ieee80211_tx_info_clear_status(info);
	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;
	else
		info->flags |= IEEE80211_TX_STAT_ACK;

	ieee80211_tx_status_irqsafe(hw, skb);
}

/*
 * Send one frame from @queue. Returns 0 when a frame was written, 1 when the
 * queue was empty and a negative errno when the write failed, in which case
 * the frame is put back at the head of the queue.
 */
static int rtw_sdio_process_tx_queue(struct rtw_dev *rtwdev,
				     enum rtw_tx_queue_type queue)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	struct sk_buff *skb;
	u16 q_map;
	int ret;

	skb = skb_dequeue(&rtwsdio->tx_queue[queue]);
	if (!skb)
		return 1;

	ret = rtw_sdio_write_port(rtwdev, skb, queue);
	if (ret) {
		skb_queue_head(&rtwsdio->tx_queue[queue], skb);
		return ret;
	}

	/* rtw_sdio_indicate_tx_status() consumes the skb, so read this first */
	q_map = skb_get_queue_mapping(skb);

	rtw_sdio_indicate_tx_status(rtwdev, skb);

	rtw_sdio_8723bs_wake_tx_queue(rtwdev, queue, q_map);

	return 0;
}

/*
 * A page or output queue shortage, and a failed skb expansion, are transient
 * and leave the frame queued, so come back for it shortly. That matters once
 * the mac80211 queue can be stopped: a stopped queue is handed no further
 * frames, so nothing else would kick this work item and the access category
 * would stay stopped for good. The remaining errors are logged where they
 * happen and are not retried.
 *
 * After a management frame, restart from the highest priority queue so the
 * join sequence is not held up behind a data backlog.
 */
static bool rtw_sdio_8723bs_reschedule_tx(struct rtw_dev *rtwdev,
					  struct rtw_sdio_work_data *work_data,
					  enum rtw_tx_queue_type queue, int ret)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	unsigned long delay;

	if (!rtw_is_8723bs(rtwdev))
		return false;

	if (ret == -EBUSY || ret == -ENOMEM)
		delay = RTW_SDIO_TX_RETRY_DELAY;
	else if (ret == 0 && queue == RTW_TX_QUEUE_MGMT)
		delay = 0;
	else
		return false;

	queue_delayed_work(rtwsdio->txwq, &work_data->work, delay);

	return true;
}

static void rtw_sdio_tx_handler(struct work_struct *work)
{
	struct rtw_sdio_work_data *work_data =
		container_of(to_delayed_work(work), struct rtw_sdio_work_data,
			     work);
	struct rtw_sdio *rtwsdio;
	struct rtw_dev *rtwdev;
	int limit, queue, ret;

	rtwdev = work_data->rtwdev;
	rtwsdio = (struct rtw_sdio *)rtwdev->priv;

	if (!rtw_fw_feature_check(&rtwdev->fw, FW_FEATURE_TX_WAKE))
		rtw_sdio_deep_ps_leave(rtwdev);

	for (queue = RTK_MAX_TX_QUEUE_NUM - 1; queue >= 0; queue--) {
		for (limit = 0; limit < 1000; limit++) {
			ret = rtw_sdio_process_tx_queue(rtwdev, queue);
			if (ret > 0)
				break;

			if (rtw_sdio_8723bs_reschedule_tx(rtwdev, work_data,
							  queue, ret))
				return;

			if (skb_queue_empty(&rtwsdio->tx_queue[queue]))
				break;
		}
	}
}

static void rtw_sdio_free_irq(struct rtw_dev *rtwdev,
			      struct sdio_func *sdio_func)
{
	sdio_claim_host(sdio_func);
	sdio_release_irq(sdio_func);
	sdio_release_host(sdio_func);
}

static int rtw_sdio_init_tx(struct rtw_dev *rtwdev)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	int i;

	rtwsdio->txwq = create_singlethread_workqueue("rtw88_sdio: tx wq");
	if (!rtwsdio->txwq) {
		rtw_err(rtwdev, "failed to create TX work queue\n");
		return -ENOMEM;
	}

	mutex_init(&rtwsdio->tx_credit_lock);

	for (i = 0; i < RTK_MAX_TX_QUEUE_NUM; i++) {
		skb_queue_head_init(&rtwsdio->tx_queue[i]);
		rtwsdio->tx_queue_stopped[i] = false;
	}
	rtwsdio->tx_handler_data = kmalloc_obj(*rtwsdio->tx_handler_data);
	if (!rtwsdio->tx_handler_data)
		goto err_destroy_wq;

	rtwsdio->tx_handler_data->rtwdev = rtwdev;
	INIT_DELAYED_WORK(&rtwsdio->tx_handler_data->work, rtw_sdio_tx_handler);

	return 0;

err_destroy_wq:
	mutex_destroy(&rtwsdio->tx_credit_lock);
	destroy_workqueue(rtwsdio->txwq);
	return -ENOMEM;
}

static void rtw_sdio_deinit_tx(struct rtw_dev *rtwdev)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;
	int i;

	cancel_delayed_work_sync(&rtwsdio->tx_handler_data->work);
	destroy_workqueue(rtwsdio->txwq);
	kfree(rtwsdio->tx_handler_data);

	for (i = 0; i < RTK_MAX_TX_QUEUE_NUM; i++)
		ieee80211_purge_tx_queue(rtwdev->hw, &rtwsdio->tx_queue[i]);

	mutex_destroy(&rtwsdio->tx_credit_lock);
}

int rtw_sdio_probe(struct sdio_func *sdio_func,
		   const struct sdio_device_id *id)
{
	struct ieee80211_hw *hw;
	struct rtw_dev *rtwdev;
	int drv_data_size;
	int ret;

	drv_data_size = sizeof(struct rtw_dev) + sizeof(struct rtw_sdio);
	hw = ieee80211_alloc_hw(drv_data_size, &rtw_ops);
	if (!hw) {
		dev_err(&sdio_func->dev, "failed to allocate hw");
		return -ENOMEM;
	}

	rtwdev = hw->priv;
	rtwdev->hw = hw;
	rtwdev->dev = &sdio_func->dev;
	rtwdev->chip = (struct rtw_chip_info *)id->driver_data;
	rtwdev->hci.ops = &rtw_sdio_ops;
	rtwdev->hci.type = RTW_HCI_TYPE_SDIO;

	ret = rtw_core_init(rtwdev);
	if (ret)
		goto err_release_hw;

	rtw_dbg(rtwdev, RTW_DBG_SDIO,
		"rtw88 SDIO probe: vendor=0x%04x device=%04x class=%02x",
		id->vendor, id->device, id->class);

	ret = rtw_sdio_claim(rtwdev, sdio_func);
	if (ret) {
		rtw_err(rtwdev, "failed to claim SDIO device");
		goto err_deinit_core;
	}

	rtw_sdio_init(rtwdev);

	ret = rtw_sdio_init_tx(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to init SDIO TX queue\n");
		goto err_sdio_declaim;
	}

	ret = rtw_chip_info_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup chip information");
		goto err_destroy_txwq;
	}

	ret = rtw_sdio_request_irq(rtwdev, sdio_func);
	if (ret)
		goto err_destroy_txwq;

	ret = rtw_register_hw(rtwdev, hw);
	if (ret) {
		rtw_err(rtwdev, "failed to register hw");
		goto err_free_irq;
	}

	return 0;

err_free_irq:
	rtw_sdio_free_irq(rtwdev, sdio_func);
err_destroy_txwq:
	rtw_sdio_deinit_tx(rtwdev);
err_sdio_declaim:
	rtw_sdio_declaim(rtwdev, sdio_func);
err_deinit_core:
	rtw_core_deinit(rtwdev);
err_release_hw:
	ieee80211_free_hw(hw);

	return ret;
}
EXPORT_SYMBOL(rtw_sdio_probe);

void rtw_sdio_remove(struct sdio_func *sdio_func)
{
	struct ieee80211_hw *hw = sdio_get_drvdata(sdio_func);
	struct rtw_dev *rtwdev;

	if (!hw)
		return;

	rtwdev = hw->priv;

	rtw_unregister_hw(rtwdev, hw);
	rtw_sdio_disable_interrupt(rtwdev);
	rtw_sdio_free_irq(rtwdev, sdio_func);
	rtw_sdio_declaim(rtwdev, sdio_func);
	rtw_sdio_deinit_tx(rtwdev);
	rtw_core_deinit(rtwdev);
	ieee80211_free_hw(hw);
}
EXPORT_SYMBOL(rtw_sdio_remove);

void rtw_sdio_shutdown(struct sdio_func *sdio_func)
{
	const struct rtw_chip_info *chip;
	struct ieee80211_hw *hw;
	struct rtw_dev *rtwdev;

	hw = sdio_get_drvdata(sdio_func);
	if (!hw)
		return;

	rtwdev = hw->priv;
	chip = rtwdev->chip;

	if (chip->ops->shutdown)
		chip->ops->shutdown(rtwdev);
}
EXPORT_SYMBOL(rtw_sdio_shutdown);

MODULE_AUTHOR("Martin Blumenstingl");
MODULE_AUTHOR("Jernej Skrabec");
MODULE_DESCRIPTION("Realtek 802.11ac wireless SDIO driver");
MODULE_LICENSE("Dual BSD/GPL");
