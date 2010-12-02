/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/rtnetlink.h>

#include "net_driver.h"
#include "spi.h"
#include "efx.h"
#include "nic.h"
#include "mcdi.h"
#include "mcdi_pcol.h"

#define EFX_SPI_VERIFY_BUF_LEN 16

struct efx_mtd_partition {
	struct mtd_info mtd;
	union {
		struct {
			bool updating;
			u8 nvram_type;
			u16 fw_subtype;
		} mcdi;
		size_t offset;
	};
	const char *type_name;
	char name[IFNAMSIZ + 20];
};

struct efx_mtd_ops {
	int (*read)(struct mtd_info *mtd, loff_t start, size_t len,
		    size_t *retlen, u8 *buffer);
	int (*erase)(struct mtd_info *mtd, loff_t start, size_t len);
	int (*write)(struct mtd_info *mtd, loff_t start, size_t len,
		     size_t *retlen, const u8 *buffer);
	int (*sync)(struct mtd_info *mtd);
};

struct efx_mtd {
	struct list_head node;
	struct efx_nic *efx;
	const struct efx_spi_device *spi;
	const char *name;
	const struct efx_mtd_ops *ops;
	size_t n_parts;
	struct efx_mtd_partition part[0];
};

#define efx_for_each_partition(part, efx_mtd)			\
	for ((part) = &(efx_mtd)->part[0];			\
	     (part) != &(efx_mtd)->part[(efx_mtd)->n_parts];	\
	     (part)++)

#define to_efx_mtd_partition(mtd)				\
	container_of(mtd, struct efx_mtd_partition, mtd)

static int falcon_mtd_probe(struct efx_nic *efx);
static int siena_mtd_probe(struct efx_nic *efx);

/* SPI utilities */

static int
efx_spi_slow_wait(struct efx_mtd_partition *part, bool uninterruptible)
{
	struct efx_mtd *efx_mtd = part->mtd.priv;
	const struct efx_spi_device *spi = efx_mtd->spi;
	struct efx_nic *efx = efx_mtd->efx;
	u8 status;
	int rc, i;

	/* Wait up to 4s for flash/EEPROM to finish a slow operation. */
	for (i = 0; i < 40; i++) {
		__set_current_state(uninterruptible ?
				    TASK_UNINTERRUPTIBLE : TASK_INTERRUPTIBLE);
		schedule_timeout(HZ / 10);
		rc = falcon_spi_cmd(efx, spi, SPI_RDSR, -1, NULL,
				    &status, sizeof(status));
		if (rc)
			return rc;
		if (!(status & SPI_STATUS_NRDY))
			return 0;
		if (signal_pending(current))
			return -EINTR;
	}
	pr_err("%s: timed out waiting for %s\n", part->name, efx_mtd->name);
	return -ETIMEDOUT;
}

static int
efx_spi_unlock(struct efx_nic *efx, const struct efx_spi_device *spi)
{
	const u8 unlock_mask = (SPI_STATUS_BP2 | SPI_STATUS_BP1 |
				SPI_STATUS_BP0);
	u8 status;
	int rc;

	rc = falcon_spi_cmd(efx, spi, SPI_RDSR, -1, NULL,
			    &status, sizeof(status));
	if (rc)
		return rc;

	if (!(status & unlock_mask))
		return 0; /* already unlocked */

	rc = falcon_spi_cmd(efx, spi, SPI_WREN, -1, NULL, NULL, 0);
	if (rc)
		return rc;
	rc = falcon_spi_cmd(efx, spi, SPI_SST_EWSR, -1, NULL, NULL, 0);
	if (rc)
		return rc;

	status &= ~unlock_mask;
	rc = falcon_spi_cmd(efx, spi, SPI_WRSR, -1, &status,
			    NULL, sizeof(status));
	if (rc)
		return rc;
	rc = falcon_spi_wait_write(efx, spi);
	if (rc)
		return rc;

	return 0;
}

static int
efx_spi_erase(struct efx_mtd_partition *part, loff_t start, size_t len)
{
	struct efx_mtd *efx_mtd = part->mtd.priv;
	const struct efx_spi_device *spi = efx_mtd->spi;
	struct efx_nic *efx = efx_mtd->efx;
	unsigned pos, block_len;
	u8 empty[EFX_SPI_VERIFY_BUF_LEN];
	u8 buffer[EFX_SPI_VERIFY_BUF_LEN];
	int rc;

	if (len != spi->erase_size)
		return -EINVAL;

	if (spi->erase_command == 0)
		return -EOPNOTSUPP;

	rc = efx_spi_unlock(efx, spi);
	if (rc)
		return rc;
	rc = falcon_spi_cmd(efx, spi, SPI_WREN, -1, NULL, NULL, 0);
	if (rc)
		return rc;
	rc = falcon_spi_cmd(efx, spi, spi->erase_command, start, NULL,
			    NULL, 0);
	if (rc)
		return rc;
	rc = efx_spi_slow_wait(part, false);

	/* Verify the entire region has been wiped */
	memset(empty, 0xff, sizeof(empty));
	for (pos = 0; pos < len; pos += block_len) {
		block_len = min(len - pos, sizeof(buffer));
		rc = falcon_spi_read(efx, spi, start + pos, block_len,
				     NULL, buffer);
		if (rc)
			return rc;
		if (memcmp(empty, buffer, block_len))
			return -EIO;

		/* Avoid locking up the system */
		cond_resched();
		if (signal_pending(current))
			return -EINTR;
	}

	return rc;
}

/* MTD interface */

static int efx_mtd_erase(struct mtd_info *mtd, struct erase_info *erase)
{
	struct efx_mtd *efx_mtd = mtd->priv;
	int rc;

	rc = efx_mtd->ops->erase(mtd, erase->addr, erase->len);
	if (rc == 0) {
		erase->state = MTD_ERASE_DONE;
	} else {
		erase->state = MTD_ERASE_FAILED;
		erase->fail_addr = 0xffffffff;
	}
	mtd_erase_callback(erase);
	return rc;
}

static void efx_mtd_sync(struct mtd_info *mtd)
{
	struct efx_mtd_partition *part = to_efx_mtd_partition(mtd);
	struct efx_mtd *efx_mtd = mtd->priv;
	int rc;

	rc = efx_mtd->ops->sync(mtd);
	if (rc)
		pr_err("%s: %s sync failed (%d)\n",
		       part->name, efx_mtd->name, rc);
}

static void efx_mtd_remove_partition(struct efx_mtd_partition *part)
{
	int rc;

	for (;;) {
		rc = del_mtd_device(&part->mtd);
		if (rc != -EBUSY)
			break;
		ssleep(1);
	}
	WARN_ON(rc);
}

static void efx_mtd_remove_device(struct efx_mtd *efx_mtd)
{
	struct efx_mtd_partition *part;

	efx_for_each_partition(part, efx_mtd)
		efx_mtd_remove_partition(part);
	list_del(&efx_mtd->node);
	kfree(efx_mtd);
}

static void efx_mtd_rename_device(struct efx_mtd *efx_mtd)
{
	struct efx_mtd_partition *part;

	efx_for_each_partition(part, efx_mtd)
		if (efx_nic_rev(efx_mtd->efx) >= EFX_REV_SIENA_A0)
			snprintf(part->name, sizeof(part->name),
				 "%s %s:%02x", efx_mtd->efx->name,
				 part->type_name, part->mcdi.fw_subtype);
		else
			snprintf(part->name, sizeof(part->name),
				 "%s %s", efx_mtd->efx->name,
				 part->type_name);
}

static int efx_mtd_probe_device(struct efx_nic *efx, struct efx_mtd *efx_mtd)
{
	struct efx_mtd_partition *part;

	efx_mtd->efx = efx;

	efx_mtd_rename_device(efx_mtd);

	efx_for_each_partition(part, efx_mtd) {
		part->mtd.writesize = 1;

		part->mtd.owner = THIS_MODULE;
		part->mtd.priv = efx_mtd;
		part->mtd.name = part->name;
		part->mtd.erase = efx_mtd_erase;
		part->mtd.read = efx_mtd->ops->read;
		part->mtd.write = efx_mtd->ops->write;
		part->mtd.sync = efx_mtd_sync;

		if (add_mtd_device(&part->mtd))
			goto fail;
	}

	list_add(&efx_mtd->node, &efx->mtd_list);
	return 0;

fail:
	while (part != &efx_mtd->part[0]) {
		--part;
		efx_mtd_remove_partition(part);
	}
	/* add_mtd_device() returns 1 if the MTD table is full */
	return -ENOMEM;
}

void efx_mtd_remove(struct efx_nic *efx)
{
	struct efx_mtd *efx_mtd, *next;

	WARN_ON(efx_dev_registered(efx));

	list_for_each_entry_safe(efx_mtd, next, &efx->mtd_list, node)
		efx_mtd_remove_device(efx_mtd);
}

void efx_mtd_rename(struct efx_nic *efx)
{
	struct efx_mtd *efx_mtd;

	ASSERT_RTNL();

	list_for_each_entry(efx_mtd, &efx->mtd_list, node)
		efx_mtd_rename_device(efx_mtd);
}

int efx_mtd_probe(struct efx_nic *efx)
{
	if (efx_nic_rev(efx) >= EFX_REV_SIENA_A0)
		return siena_mtd_probe(efx);
	else
		return falcon_mtd_probe(efx);
}

/* Implementation of MTD operations for Falcon */

static int falcon_mtd_read(struct mtd_info *mtd, loff_t start,
			   size_t len, size_t *retlen, u8 *buffer)
{
	struct efx_mtd_partition *part = to_efx_mtd_partition(mtd);
	struct efx_mtd *efx_mtd = mtd->priv;
	const struct efx_spi_device *spi = efx_mtd->spi;
	struct efx_nic *efx = efx_mtd->efx;
	int rc;

	rc = mutex_lock_interruptible(&efx->spi_lock);
	if (rc)
		return rc;
	rc = falcon_spi_read(efx, spi, part->offset + start, len,
			     retlen, buffer);
	mutex_unlock(&efx->spi_lock);
	return rc;
}

static int falcon_mtd_erase(struct mtd_info *mtd, loff_t start, size_t len)
{
	struct efx_mtd_partition *part = to_efx_mtd_partition(mtd);
	struct efx_mtd *efx_mtd = mtd->priv;
	struct efx_nic *efx = efx_mtd->efx;
	int rc;

	rc = mutex_lock_interruptible(&efx->spi_lock);
	if (rc)
		return rc;
	rc = efx_spi_erase(part, part->offset + start, len);
	mutex_unlock(&efx->spi_lock);
	return rc;
}

static int falcon_mtd_write(struct mtd_info *mtd, loff_t start,
			    size_t len, size_t *retlen, const u8 *buffer)
{
	struct efx_mtd_partition *part = to_efx_mtd_partition(mtd);
	struct efx_mtd *efx_mtd = mtd->priv;
	const struct efx_spi_device *spi = efx_mtd->spi;
	struct efx_nic *efx = efx_mtd->efx;
	int rc;

	rc = mutex_lock_interruptible(&efx->spi_lock);
	if (rc)
		return rc;
	rc = falcon_spi_write(efx, spi, part->offset + start, len,
			      retlen, buffer);
	mutex_unlock(&efx->spi_lock);
	return rc;
}

static int falcon_mtd_sync(struct mtd_info *mtd)
{
	struct efx_mtd_partition *part = to_efx_mtd_partition(mtd);
	struct efx_mtd *efx_mtd = mtd->priv;
	struct efx_nic *efx = efx_mtd->efx;
	int rc;

	mutex_lock(&efx->spi_lock);
	rc = efx_spi_slow_wait(part, true);
	mutex_unlock(&efx->spi_lock);
	return rc;
}

static struct efx_mtd_ops falcon_mtd_ops = {
	.read	= falcon_mtd_read,
	.erase	= falcon_mtd_erase,
	.write	= falcon_mtd_write,
	.sync	= falcon_mtd_sync,
};

static int falcon_mtd_probe(struct efx_nic *efx)
{
	struct efx_spi_device *spi;
	struct efx_mtd *efx_mtd;
	int rc = -ENODEV;

	ASSERT_RTNL();

	spi = efx->spi_flash;
	if (spi && spi->size > FALCON_FLASH_BOOTCODE_START) {
		efx_mtd = kzalloc(sizeof(*efx_mtd) + sizeof(efx_mtd->part[0]),
				  GFP_KERNEL);
		if (!efx_mtd)
			return -ENOMEM;

		efx_mtd->spi = spi;
		efx_mtd->name = "flash";
		efx_mtd->ops = &falcon_mtd_ops;

		efx_mtd->n_parts = 1;
		efx_mtd->part[0].mtd.type = MTD_NORFLASH;
		efx_mtd->part[0].mtd.flags = MTD_CAP_NORFLASH;
		efx_mtd->part[0].mtd.size = spi->size - FALCON_FLASH_BOOTCODE_START;
		efx_mtd->part[0].mtd.erasesize = spi->erase_size;
		efx_mtd->part[0].offset = FALCON_FLASH_BOOTCODE_START;
		efx_mtd->part[0].type_name = "sfc_flash_bootrom";

		rc = efx_mtd_probe_device(efx, efx_mtd);
		if (rc) {
			kfree(efx_mtd);
			return rc;
		}
	}

	spi = efx->spi_eeprom;
	if (spi && spi->size > EFX_EEPROM_BOOTCONFIG_START) {
		efx_mtd = kzalloc(sizeof(*efx_mtd) + sizeof(efx_mtd->part[0]),
				  GFP_KERNEL);
		if (!efx_mtd)
			return -ENOMEM;

		efx_mtd->spi = spi;
		efx_mtd->name = "EEPROM";
		efx_mtd->ops = &falcon_mtd_ops;

		efx_mtd->n_parts = 1;
		efx_mtd->part[0].mtd.type = MTD_RAM;
		efx_mtd->part[0].mtd.flags = MTD_CAP_RAM;
		efx_mtd->part[0].mtd.size =
			min(spi->size, EFX_EEPROM_BOOTCONFIG_END) -
			EFX_EEPROM_BOOTCONFIG_START;
		efx_mtd->part[0].mtd.erasesize = spi->erase_size;
		efx_mtd->part[0].offset = EFX_EEPROM_BOOTCONFIG_START;
		efx_mtd->part[0].type_name = "sfc_bootconfig";

		rc = efx_mtd_probe_device(efx, efx_mtd);
		if (rc) {
			kfree(efx_mtd);
			return rc;
		}
	}

	return rc;
}

/* Implementation of MTD operations for Siena */

static int siena_mtd_read(struct mtd_info *mtd, loff_t start,
			  size_t len, size_t *retlen, u8 *buffer)
{
	struct efx_mtd_partition *part = to_efx_mtd_partition(mtd);
	struct efx_mtd *efx_mtd = mtd->priv;
	struct efx_nic *efx = efx_mtd->efx;
	loff_t offset = start;
	loff_t end = min_t(loff_t, start + len, mtd->size);
	size_t chunk;
	int rc = 0;

	while (offset < end) {
		chunk = min_t(size_t, end - offset, EFX_MCDI_NVRAM_LEN_MAX);
		rc = efx_mcdi_nvram_read(efx, part->mcdi.nvram_type, offset,
					 buffer, chunk);
		if (rc)
			goto out;
		offset += chunk;
		buffer += chunk;
	}
out:
	*retlen = offset - start;
	return rc;
}

static int siena_mtd_erase(struct mtd_info *mtd, loff_t start, size_t len)
{
	struct efx_mtd_partition *part = to_efx_mtd_partition(mtd);
	struct efx_mtd *efx_mtd = mtd->priv;
	struct efx_nic *efx = efx_mtd->efx;
	loff_t offset = start & ~((loff_t)(mtd->erasesize - 1));
	loff_t end = min_t(loff_t, start + len, mtd->size);
	size_t chunk = part->mtd.erasesize;
	int rc = 0;

	if (!part->mcdi.updating) {
		rc = efx_mcdi_nvram_update_start(efx, part->mcdi.nvram_type);
		if (rc)
			goto out;
		part->mcdi.updating = 1;
	}

	/* The MCDI interface can in fact do multiple erase blocks at once;
	 * but erasing may be slow, so we make multiple calls here to avoid
	 * tripping the MCDI RPC timeout. */
	while (offset < end) {
		rc = efx_mcdi_nvram_erase(efx, part->mcdi.nvram_type, offset,
					  chunk);
		if (rc)
			goto out;
		offset += chunk;
	}
out:
	return rc;
}

static int siena_mtd_write(struct mtd_info *mtd, loff_t start,
			   size_t len, size_t *retlen, const u8 *buffer)
{
	struct efx_mtd_partition *part = to_efx_mtd_partition(mtd);
	struct efx_mtd *efx_mtd = mtd->priv;
	struct efx_nic *efx = efx_mtd->efx;
	loff_t offset = start;
	loff_t end = min_t(loff_t, start + len, mtd->size);
	size_t chunk;
	int rc = 0;

	if (!part->mcdi.updating) {
		rc = efx_mcdi_nvram_update_start(efx, part->mcdi.nvram_type);
		if (rc)
			goto out;
		part->mcdi.updating = 1;
	}

	while (offset < end) {
		chunk = min_t(size_t, end - offset, EFX_MCDI_NVRAM_LEN_MAX);
		rc = efx_mcdi_nvram_write(efx, part->mcdi.nvram_type, offset,
					  buffer, chunk);
		if (rc)
			goto out;
		offset += chunk;
		buffer += chunk;
	}
out:
	*retlen = offset - start;
	return rc;
}

static int siena_mtd_sync(struct mtd_info *mtd)
{
	struct efx_mtd_partition *part = to_efx_mtd_partition(mtd);
	struct efx_mtd *efx_mtd = mtd->priv;
	struct efx_nic *efx = efx_mtd->efx;
	int rc = 0;

	if (part->mcdi.updating) {
		part->mcdi.updating = 0;
		rc = efx_mcdi_nvram_update_finish(efx, part->mcdi.nvram_type);
	}

	return rc;
}

static struct efx_mtd_ops siena_mtd_ops = {
	.read	= siena_mtd_read,
	.erase	= siena_mtd_erase,
	.write	= siena_mtd_write,
	.sync	= siena_mtd_sync,
};

struct siena_nvram_type_info {
	int port;
	const char *name;
};

static struct siena_nvram_type_info siena_nvram_types[] = {
	[MC_CMD_NVRAM_TYPE_DISABLED_CALLISTO]	= { 0, "sfc_dummy_phy" },
	[MC_CMD_NVRAM_TYPE_MC_FW]		= { 0, "sfc_mcfw" },
	[MC_CMD_NVRAM_TYPE_MC_FW_BACKUP]	= { 0, "sfc_mcfw_backup" },
	[MC_CMD_NVRAM_TYPE_STATIC_CFG_PORT0]	= { 0, "sfc_static_cfg" },
	[MC_CMD_NVRAM_TYPE_STATIC_CFG_PORT1]	= { 1, "sfc_static_cfg" },
	[MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT0]	= { 0, "sfc_dynamic_cfg" },
	[MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT1]	= { 1, "sfc_dynamic_cfg" },
	[MC_CMD_NVRAM_TYPE_EXP_ROM]		= { 0, "sfc_exp_rom" },
	[MC_CMD_NVRAM_TYPE_EXP_ROM_CFG_PORT0]	= { 0, "sfc_exp_rom_cfg" },
	[MC_CMD_NVRAM_TYPE_EXP_ROM_CFG_PORT1]	= { 1, "sfc_exp_rom_cfg" },
	[MC_CMD_NVRAM_TYPE_PHY_PORT0]		= { 0, "sfc_phy_fw" },
	[MC_CMD_NVRAM_TYPE_PHY_PORT1]		= { 1, "sfc_phy_fw" },
};

static int siena_mtd_probe_partition(struct efx_nic *efx,
				     struct efx_mtd *efx_mtd,
				     unsigned int part_id,
				     unsigned int type)
{
	struct efx_mtd_partition *part = &efx_mtd->part[part_id];
	struct siena_nvram_type_info *info;
	size_t size, erase_size;
	bool protected;
	int rc;

	if (type >= ARRAY_SIZE(siena_nvram_types))
		return -ENODEV;

	info = &siena_nvram_types[type];

	if (info->port != efx_port_num(efx))
		return -ENODEV;

	rc = efx_mcdi_nvram_info(efx, type, &size, &erase_size, &protected);
	if (rc)
		return rc;
	if (protected)
		return -ENODEV; /* hide it */

	part->mcdi.nvram_type = type;
	part->type_name = info->name;

	part->mtd.type = MTD_NORFLASH;
	part->mtd.flags = MTD_CAP_NORFLASH;
	part->mtd.size = size;
	part->mtd.erasesize = erase_size;

	return 0;
}

static int siena_mtd_get_fw_subtypes(struct efx_nic *efx,
				     struct efx_mtd *efx_mtd)
{
	struct efx_mtd_partition *part;
	uint16_t fw_subtype_list[MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_LEN /
				 sizeof(uint16_t)];
	int rc;

	rc = efx_mcdi_get_board_cfg(efx, NULL, fw_subtype_list);
	if (rc)
		return rc;

	efx_for_each_partition(part, efx_mtd)
		part->mcdi.fw_subtype = fw_subtype_list[part->mcdi.nvram_type];

	return 0;
}

static int siena_mtd_probe(struct efx_nic *efx)
{
	struct efx_mtd *efx_mtd;
	int rc = -ENODEV;
	u32 nvram_types;
	unsigned int type;

	ASSERT_RTNL();

	rc = efx_mcdi_nvram_types(efx, &nvram_types);
	if (rc)
		return rc;

	efx_mtd = kzalloc(sizeof(*efx_mtd) +
			  hweight32(nvram_types) * sizeof(efx_mtd->part[0]),
			  GFP_KERNEL);
	if (!efx_mtd)
		return -ENOMEM;

	efx_mtd->name = "Siena NVRAM manager";

	efx_mtd->ops = &siena_mtd_ops;

	type = 0;
	efx_mtd->n_parts = 0;

	while (nvram_types != 0) {
		if (nvram_types & 1) {
			rc = siena_mtd_probe_partition(efx, efx_mtd,
						       efx_mtd->n_parts, type);
			if (rc == 0)
				efx_mtd->n_parts++;
			else if (rc != -ENODEV)
				goto fail;
		}
		type++;
		nvram_types >>= 1;
	}

	rc = siena_mtd_get_fw_subtypes(efx, efx_mtd);
	if (rc)
		goto fail;

	rc = efx_mtd_probe_device(efx, efx_mtd);
fail:
	if (rc)
		kfree(efx_mtd);
	return rc;
}

