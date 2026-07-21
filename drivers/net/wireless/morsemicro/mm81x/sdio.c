// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2026 Morse Micro
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sd.h>
#include "hw.h"
#include "core.h"
#include "bus.h"
#include "mac.h"
#include "fw.h"
#include "hif.h"

/*
 * Value to indicate that the base address for bulk/register
 * read/writes has yet to be set
 */
#define MM81X_SDIO_BASE_ADDR_UNSET 0xFFFFFFFF

#define MM81X_SDIO_ALIGNMENT (8)

#define MM81X_SDIO_REG_ADDRESS_BASE 0x10000
#define MM81X_SDIO_REG_ADDRESS_WINDOW_0 MM81X_SDIO_REG_ADDRESS_BASE
#define MM81X_SDIO_REG_ADDRESS_WINDOW_1 (MM81X_SDIO_REG_ADDRESS_BASE + 1)
#define MM81X_SDIO_REG_ADDRESS_CONFIG (MM81X_SDIO_REG_ADDRESS_BASE + 2)

struct mm81x_sdio {
	bool enabled;
	u32 bulk_addr_base;
	u32 register_addr_base;
	struct sdio_func *func;
	const struct sdio_device_id *id;
};

static void irq_handler(struct sdio_func *func1)
{
	struct sdio_func *func = func1->card->sdio_func[1];
	struct mm81x *mors = sdio_get_drvdata(func);

	mm81x_hw_irq_handle(mors);
}

static int mm81x_sdio_enable_irq(struct mm81x_sdio *sdio)
{
	int ret;
	struct sdio_func *func = sdio->func;
	struct sdio_func *func1 = func->card->sdio_func[0];
	struct mm81x *mors = sdio_get_drvdata(func);

	sdio_claim_host(func);
	ret = sdio_claim_irq(func1, irq_handler);
	if (ret)
		dev_err(mors->dev, "Failed to enable sdio irq: %d\n", ret);

	sdio_release_host(func);
	return ret;
}

static void mm81x_sdio_disable_irq(struct mm81x_sdio *sdio)
{
	struct sdio_func *func = sdio->func;
	struct sdio_func *func1 = func->card->sdio_func[0];

	sdio_claim_host(func);
	sdio_release_irq(func1);
	sdio_release_host(func);
}

static void mm81x_sdio_set_irq(struct mm81x *mors, bool enable)
{
	struct mm81x_sdio *sdio = (struct mm81x_sdio *)mors->drv_priv;

	if (enable)
		mm81x_sdio_enable_irq(sdio);
	else
		mm81x_sdio_disable_irq(sdio);
}

static u32 mm81x_sdio_calculate_base_address(u32 address, u8 access)
{
	return (address & MM81X_SDIO_RW_ADDR_BOUNDARY_MASK) | (access & 0x3);
}

static void mm81x_sdio_reset_base_address(struct mm81x_sdio *sdio)
{
	sdio->bulk_addr_base = MM81X_SDIO_BASE_ADDR_UNSET;
	sdio->register_addr_base = MM81X_SDIO_BASE_ADDR_UNSET;
}

static int mm81x_sdio_set_func_address_base(struct mm81x_sdio *sdio,
					    struct sdio_func *func, u32 address,
					    u8 access)
{
	int ret = 0;
	int retries = 0;
	static const int max_retries = 3;
	struct sdio_func *func2 = sdio->func;
	struct mm81x *mors = sdio_get_drvdata(sdio->func);
	s32 calculated_addr_base =
		mm81x_sdio_calculate_base_address(address, access);
	u32 *current_addr_base = func == func2 ? &sdio->bulk_addr_base :
						 &sdio->register_addr_base;

	if ((*current_addr_base) == calculated_addr_base &&
	    *current_addr_base != MM81X_SDIO_BASE_ADDR_UNSET)
		return ret;

retry:
	sdio_writeb(func, (u8)u32_get_bits(address, GENMASK(23, 16)),
		    MM81X_SDIO_REG_ADDRESS_WINDOW_0, &ret);
	if (ret)
		goto err;

	sdio_writeb(func, (u8)u32_get_bits(address, GENMASK(31, 24)),
		    MM81X_SDIO_REG_ADDRESS_WINDOW_1, &ret);
	if (ret)
		goto err;

	sdio_writeb(func, access & 0x3, MM81X_SDIO_REG_ADDRESS_CONFIG, &ret);
	if (ret)
		goto err;

	*current_addr_base = calculated_addr_base;
	if (retries)
		dev_dbg(mors->dev, "%s succeeded after %d retries\n", __func__,
			retries);

	return ret;
err:
	retries++;
	if (ret == -ETIMEDOUT && retries <= max_retries) {
		dev_dbg(mors->dev, "%s failed (%d), retrying (%d/%d)\n",
			__func__, ret, retries, max_retries);
		goto retry;
	}

	*current_addr_base = MM81X_SDIO_BASE_ADDR_UNSET;
	return ret;
}

static int mm81x_sdio_mem_write_block(struct mm81x_sdio *sdio, u32 address,
				      u8 *data, ssize_t size)
{
	int ret;
	struct sdio_func *func2 = sdio->func;
	struct mm81x *mors = sdio_get_drvdata(sdio->func);

	mm81x_sdio_set_func_address_base(sdio, func2, address,
					 MM81X_CONFIG_ACCESS_4BYTE);
	if (unlikely(!IS_ALIGNED((uintptr_t)data,
				 mors->bus_ops->bulk_alignment))) {
		ret = -EBADE;
		goto exit;
	}

	address &= 0x0000FFFF; /* remove base and keep offset */
	ret = sdio_memcpy_toio(func2, address, data, size);
	if (ret)
		goto exit;

	ret = size;
exit:
	return ret;
}

static int mm81x_sdio_mem_write_byte(struct mm81x_sdio *sdio, u32 address,
				     u8 *data, ssize_t size)
{
	int i, ret;
	struct sdio_func *func1 = sdio->func->card->sdio_func[0];

	mm81x_sdio_set_func_address_base(sdio, func1, address,
					 MM81X_CONFIG_ACCESS_1BYTE);

	address &= 0x0000FFFF; /* remove base and keep offset */
	for (i = 0; i < size; i++) {
		sdio_writeb(func1, data[i], address + i, (int *)&ret);
		if (ret)
			goto exit;
	}

	ret = size;
exit:
	return ret;
}

static void mm81x_sdio_claim_host(struct mm81x *mors)
{
	struct mm81x_sdio *sdio = (struct mm81x_sdio *)mors->drv_priv;
	struct sdio_func *func = sdio->func;

	sdio_claim_host(func);
}

static void mm81x_sdio_release_host(struct mm81x *mors)
{
	struct mm81x_sdio *sdio = (struct mm81x_sdio *)mors->drv_priv;
	struct sdio_func *func = sdio->func;

	sdio_release_host(func);
}

static int mm81x_sdio_mem_read_block(struct mm81x_sdio *sdio, u32 address,
				     u8 *data, ssize_t size)
{
	int ret;
	struct sdio_func *func2 = sdio->func;
	struct mm81x *mors = sdio_get_drvdata(sdio->func);

	mm81x_sdio_set_func_address_base(sdio, func2, address,
					 MM81X_CONFIG_ACCESS_4BYTE);
	if (unlikely(!IS_ALIGNED((uintptr_t)data,
				 mors->bus_ops->bulk_alignment))) {
		ret = -EBADE;
		goto exit;
	}

	address &= 0x0000FFFF; /* remove base and keep offset */
	ret = sdio_memcpy_fromio(func2, data, address, size);
	if (ret)
		goto exit;

	/*
	 * Observed sometimes that SDIO read repeats the first 4-bytes
	 * word twice, overwriting second word (hence, tail will be
	 * overwritten with 'sync' byte). When this happens, reading
	 * will fetch the correct word. NB: if repeated again, pass it
	 * anyway and upper layers will handle it
	 */

	if (size >= 8 && memcmp(data, data + 4, 4) == 0)
		sdio_memcpy_fromio(func2, data, address, 8);

	ret = size;
exit:
	return ret;
}

static int mm81x_sdio_mem_read_byte(struct mm81x_sdio *sdio, u32 address,
				    u8 *data, ssize_t size)
{
	int i, ret;
	struct sdio_func *func1 = sdio->func->card->sdio_func[0];

	mm81x_sdio_set_func_address_base(sdio, func1, address,
					 MM81X_CONFIG_ACCESS_1BYTE);

	address &= 0x0000FFFF; /* remove base and keep offset */
	for (i = 0; i < size; i++) {
		data[i] = sdio_readb(func1, address + i, (int *)&ret);
		if (ret)
			goto exit;
	}

	ret = size;
exit:
	return ret;
}

static int mm81x_sdio_dm_write(struct mm81x *mors, u32 address, const u8 *data,
			       int len)
{
	int ret = 0;
	int block_len, byte_len;
	struct mm81x_sdio *sdio = (struct mm81x_sdio *)mors->drv_priv;
	int remaining = len;
	int offset = 0;

	if (remaining > 0 && address & 0x3) {
		len = 4 - (address & 0x3);
		ret = mm81x_sdio_mem_write_byte(sdio, address, (u8 *)data, len);
		if (ret != len)
			return -EIO;

		offset += len;
		remaining -= len;
	}

	while ((remaining) > 0) {
		/*
		 * We can only write up to the end of a single window in
		 * each write operation.
		 */
		u32 window_end = (address + offset) |
				 ~MM81X_SDIO_RW_ADDR_BOUNDARY_MASK;

		len = min(remaining, (int)(window_end + 1 - address - offset));
		block_len = len & ~0x3;
		byte_len = len & 0x3;

		if (block_len) {
			ret = mm81x_sdio_mem_write_block(sdio, address + offset,
							 (u8 *)(data + offset),
							 block_len);
			if (ret != block_len)
				return -EIO;

			offset += block_len;
		}

		if (byte_len) {
			ret = mm81x_sdio_mem_write_byte(sdio, address + offset,
							(u8 *)(data + offset),
							byte_len);
			if (ret != byte_len)
				return -EIO;

			offset += byte_len;
		}

		remaining -= len;
	}

	return 0;
}

static int mm81x_sdio_dm_read(struct mm81x *mors, u32 address, u8 *data,
			      int len)
{
	int ret = 0;
	int block_len, byte_len;
	struct mm81x_sdio *sdio = (struct mm81x_sdio *)mors->drv_priv;
	int remaining = len;
	int offset = 0;

	if (remaining > 0 && address & 0x3) {
		len = 4 - (address & 0x3);
		ret = mm81x_sdio_mem_read_byte(sdio, address, data, len);
		if (ret != len)
			return -EIO;

		offset += len;
		remaining -= len;
	}

	while (remaining > 0) {
		/*
		 * We can only read up to the end of a single window in
		 * each read operation.
		 */
		u32 window_end = (address + offset) |
				 ~MM81X_SDIO_RW_ADDR_BOUNDARY_MASK;

		len = min(remaining, (int)(window_end + 1 - address - offset));
		block_len = len & ~0x3;
		byte_len = len & 0x3;

		if (block_len) {
			ret = mm81x_sdio_mem_read_block(sdio, address + offset,
							data + offset,
							block_len);
			if (ret != block_len)
				return -EIO;

			offset += block_len;
		}

		if (byte_len) {
			ret = mm81x_sdio_mem_read_byte(sdio, address + offset,
						       data + offset, byte_len);
			if (ret != byte_len)
				return -EIO;

			offset += byte_len;
		}

		remaining -= len;
	}

	return 0;
}

static int mm81x_sdio_reg32_write(struct mm81x *mors, u32 address, u32 val)
{
	ssize_t ret = 0;
	u32 original_address = address;
	struct mm81x_sdio *sdio = (struct mm81x_sdio *)mors->drv_priv;
	struct sdio_func *func1 = sdio->func->card->sdio_func[0];

	mm81x_sdio_set_func_address_base(sdio, func1, address,
					 MM81X_CONFIG_ACCESS_4BYTE);

	address &= 0x0000FFFF;
	sdio_writel(func1, (__force u32)cpu_to_le32(val),
		    (__force u32)cpu_to_le32(address), (int *)&ret);
	if (ret)
		goto error;

	return 0;

error:
	if (original_address == MM81X_REG_RESET(mors) &&
	    val == MM81X_REG_RESET_VALUE(mors)) {
		dev_dbg(mors->dev,
			"SDIO reset detected, invalidating base addr\n");
		mm81x_sdio_reset_base_address(sdio);
	}

	return -EIO;
}

static int mm81x_sdio_reg32_read(struct mm81x *mors, u32 address, u32 *val)
{
	u32 value;
	ssize_t ret = 0;
	struct mm81x_sdio *sdio = (struct mm81x_sdio *)mors->drv_priv;
	struct sdio_func *func1 = sdio->func->card->sdio_func[0];

	mm81x_sdio_set_func_address_base(sdio, func1, address,
					 MM81X_CONFIG_ACCESS_4BYTE);

	address &= 0x0000FFFF;
	value = sdio_readl(func1, (__force u32)cpu_to_le32(address),
			   (int *)&ret);
	if (ret)
		return ret;

	*val = le32_to_cpup((__le32 *)&value);
	return 0;
}

static void mm81x_sdio_bus_enable(struct mm81x *mors, bool enable)
{
	struct mm81x_sdio *sdio = (struct mm81x_sdio *)mors->drv_priv;
	struct sdio_func *func = sdio->func;
	struct mmc_host *host = func->card->host;

	sdio_claim_host(func);

	if (enable) {
		/*
		 * No need to do anything special to re-enable the sdio bus.
		 * This will happen automatically when a read/write is
		 * attempted and sdio->bulk_addr_base == 0.
		 */
		sdio->enabled = true;
		host->ops->enable_sdio_irq(host, 1);
		dev_dbg(mors->dev, "%s: enabling bus\n", __func__);
	} else {
		host->ops->enable_sdio_irq(host, 0);
		mm81x_sdio_reset_base_address(sdio);
		sdio->enabled = false;
		dev_dbg(mors->dev, "%s: disabling bus\n", __func__);
	}

	sdio_release_host(func);
}

static void mm81x_sdio_reset(struct sdio_func *func)
{
	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);

	mdelay(20);

	sdio_claim_host(func);
	sdio_disable_func(func);
	mmc_hw_reset(func->card);
	sdio_enable_func(func);
	sdio_release_host(func);
}

static void mm81x_sdio_config_burst_mode(struct mm81x *mors, bool enable_burst)
{
	u8 burst_mode = (enable_burst) ? SDIO_WORD_BURST_SIZE_16 :
					 SDIO_WORD_BURST_DISABLE;

	mm81x_hw_enable_burst_mode(mors, burst_mode);
}

static const struct mm81x_bus_ops mm81x_sdio_ops = {
	.dm_read = mm81x_sdio_dm_read,
	.dm_write = mm81x_sdio_dm_write,
	.reg32_read = mm81x_sdio_reg32_read,
	.reg32_write = mm81x_sdio_reg32_write,
	.set_bus_enable = mm81x_sdio_bus_enable,
	.claim = mm81x_sdio_claim_host,
	.release = mm81x_sdio_release_host,
	.config_burst_mode = mm81x_sdio_config_burst_mode,
	.set_irq = mm81x_sdio_set_irq,
	.bulk_alignment = MM81X_SDIO_ALIGNMENT
};

static int mm81x_sdio_enable(struct mm81x_sdio *sdio)
{
	int ret;
	struct sdio_func *func = sdio->func;
	struct mm81x *mors = sdio_get_drvdata(func);

	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	if (ret)
		dev_err(mors->dev, "sdio_enable_func failed: %d\n", ret);
	sdio_release_host(func);
	return ret;
}

static void mm81x_sdio_release(struct mm81x_sdio *sdio)
{
	struct sdio_func *func = sdio->func;

	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);
}

static int mm81x_sdio_probe(struct sdio_func *func,
			    const struct sdio_device_id *id)
{
	int ret = 0;
	struct mm81x *mors = NULL;
	struct mm81x_sdio *sdio;
	struct device *dev = &func->dev;

	if (func->num == 1)
		return 0;

	if (func->num != 2)
		return -ENODEV;

	mors = mm81x_core_alloc(sizeof(*sdio), dev);
	if (!mors)
		return -ENOMEM;

	mors->bus_ops = &mm81x_sdio_ops;
	mors->bus_type = MM81X_BUS_TYPE_SDIO;

	sdio = (struct mm81x_sdio *)mors->drv_priv;
	sdio->func = func;
	sdio->id = id;
	sdio->enabled = true;
	mm81x_sdio_reset_base_address(sdio);

	sdio_set_drvdata(func, mors);

	ret = mm81x_sdio_enable(sdio);
	if (ret)
		goto err_core_free;

	mm81x_sdio_config_burst_mode(mors, true);

	ret = mm81x_core_init(mors);
	if (ret)
		goto err_sdio_release;

	ret = mm81x_sdio_enable_irq(sdio);
	if (ret)
		goto err_core_deinit;

	ret = mm81x_core_register(mors);
	if (ret)
		goto err_disable_irq;

	return 0;

err_disable_irq:
	mm81x_sdio_disable_irq(sdio);
err_core_deinit:
	mm81x_core_deinit(mors);
err_sdio_release:
	mm81x_sdio_release(sdio);
err_core_free:
	mm81x_core_free(mors);
	return ret;
}

static void mm81x_sdio_remove(struct sdio_func *func)
{
	struct mm81x *mors = sdio_get_drvdata(func);
	struct mm81x_sdio *sdio = (struct mm81x_sdio *)mors->drv_priv;

	if (!mors)
		return;

	mm81x_core_unregister(mors);
	mm81x_sdio_disable_irq(sdio);
	mm81x_core_deinit(mors);
	mm81x_sdio_release(sdio);
	mm81x_sdio_reset(func);
	mm81x_core_free(mors);
	sdio_set_drvdata(func, NULL);
}

static const struct sdio_device_id mm81x_sdio_devices[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MORSEMICRO,
		      SDIO_DEVICE_ID_MORSEMICRO_MM8108) },
	{},
};

MODULE_DEVICE_TABLE(sdio, mm81x_sdio_devices);

static struct sdio_driver mm81x_sdio_driver = {
	.name = "mm81x_sdio",
	.id_table = mm81x_sdio_devices,
	.probe = mm81x_sdio_probe,
	.remove = mm81x_sdio_remove,
};

module_sdio_driver(mm81x_sdio_driver);

MODULE_AUTHOR("Morse Micro");
MODULE_DESCRIPTION("Driver support for Morse Micro MM81X SDIO devices");
MODULE_LICENSE("Dual BSD/GPL");
