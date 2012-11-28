/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2010 Solarflare Communications Inc.
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

#define FALCON_SPI_VERIFY_BUF_LEN 16

struct efx_mtd_partition {
	struct list_head node;
	struct mtd_info mtd;
	const char *dev_type_name;
	const char *type_name;
	char name[IFNAMSIZ + 20];
};

struct falcon_mtd_partition {
	struct efx_mtd_partition common;
	const struct falcon_spi_device *spi;
	size_t offset;
};

struct efx_mtd_ops {
	void (*rename)(struct efx_mtd_partition *part);
	int (*read)(struct mtd_info *mtd, loff_t start, size_t len,
		    size_t *retlen, u8 *buffer);
	int (*erase)(struct mtd_info *mtd, loff_t start, size_t len);
	int (*write)(struct mtd_info *mtd, loff_t start, size_t len,
		     size_t *retlen, const u8 *buffer);
	int (*sync)(struct mtd_info *mtd);
};

#define to_efx_mtd_partition(mtd)				\
	container_of(mtd, struct efx_mtd_partition, mtd)

#define to_falcon_mtd_partition(mtd)				\
	container_of(mtd, struct falcon_mtd_partition, common.mtd)

static int falcon_mtd_probe(struct efx_nic *efx);
static int siena_mtd_probe(struct efx_nic *efx);

/* SPI utilities */

static int
falcon_spi_slow_wait(struct falcon_mtd_partition *part, bool uninterruptible)
{
	const struct falcon_spi_device *spi = part->spi;
	struct efx_nic *efx = part->common.mtd.priv;
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
	pr_err("%s: timed out waiting for %s\n",
	       part->common.name, part->common.dev_type_name);
	return -ETIMEDOUT;
}

static int
falcon_spi_unlock(struct efx_nic *efx, const struct falcon_spi_device *spi)
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
falcon_spi_erase(struct falcon_mtd_partition *part, loff_t start, size_t len)
{
	const struct falcon_spi_device *spi = part->spi;
	struct efx_nic *efx = part->common.mtd.priv;
	unsigned pos, block_len;
	u8 empty[FALCON_SPI_VERIFY_BUF_LEN];
	u8 buffer[FALCON_SPI_VERIFY_BUF_LEN];
	int rc;

	if (len != spi->erase_size)
		return -EINVAL;

	if (spi->erase_command == 0)
		return -EOPNOTSUPP;

	rc = falcon_spi_unlock(efx, spi);
	if (rc)
		return rc;
	rc = falcon_spi_cmd(efx, spi, SPI_WREN, -1, NULL, NULL, 0);
	if (rc)
		return rc;
	rc = falcon_spi_cmd(efx, spi, spi->erase_command, start, NULL,
			    NULL, 0);
	if (rc)
		return rc;
	rc = falcon_spi_slow_wait(part, false);

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
	struct efx_nic *efx = mtd->priv;
	int rc;

	rc = efx->mtd_ops->erase(mtd, erase->addr, erase->len);
	if (rc == 0) {
		erase->state = MTD_ERASE_DONE;
	} else {
		erase->state = MTD_ERASE_FAILED;
		erase->fail_addr = MTD_FAIL_ADDR_UNKNOWN;
	}
	mtd_erase_callback(erase);
	return rc;
}

static void efx_mtd_sync(struct mtd_info *mtd)
{
	struct efx_mtd_partition *part = to_efx_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	int rc;

	rc = efx->mtd_ops->sync(mtd);
	if (rc)
		pr_err("%s: %s sync failed (%d)\n",
		       part->name, part->dev_type_name, rc);
}

static void efx_mtd_remove_partition(struct efx_mtd_partition *part)
{
	int rc;

	for (;;) {
		rc = mtd_device_unregister(&part->mtd);
		if (rc != -EBUSY)
			break;
		ssleep(1);
	}
	WARN_ON(rc);
	list_del(&part->node);
}

static void efx_mtd_rename_partition(struct efx_mtd_partition *part)
{
	struct efx_nic *efx = part->mtd.priv;

	efx->mtd_ops->rename(part);
}

static int efx_mtd_add(struct efx_nic *efx, struct efx_mtd_partition *parts,
		       size_t n_parts, size_t sizeof_part)
{
	struct efx_mtd_partition *part;
	size_t i;

	for (i = 0; i < n_parts; i++) {
		part = (struct efx_mtd_partition *)((char *)parts +
						    i * sizeof_part);

		part->mtd.writesize = 1;

		part->mtd.owner = THIS_MODULE;
		part->mtd.priv = efx;
		part->mtd.name = part->name;
		part->mtd._erase = efx_mtd_erase;
		part->mtd._read = efx->mtd_ops->read;
		part->mtd._write = efx->mtd_ops->write;
		part->mtd._sync = efx_mtd_sync;

		efx_mtd_rename_partition(part);

		if (mtd_device_register(&part->mtd, NULL, 0))
			goto fail;

		/* Add to list in order - efx_mtd_remove() depends on this */
		list_add_tail(&part->node, &efx->mtd_list);
	}

	return 0;

fail:
	while (i--) {
		part = (struct efx_mtd_partition *)((char *)parts +
						    i * sizeof_part);
		efx_mtd_remove_partition(part);
	}
	/* Failure is unlikely here, but probably means we're out of memory */
	return -ENOMEM;
}

void efx_mtd_remove(struct efx_nic *efx)
{
	struct efx_mtd_partition *parts, *part, *next;

	WARN_ON(efx_dev_registered(efx));

	if (list_empty(&efx->mtd_list))
		return;

	parts = list_first_entry(&efx->mtd_list, struct efx_mtd_partition,
				 node);

	list_for_each_entry_safe(part, next, &efx->mtd_list, node)
		efx_mtd_remove_partition(part);

	kfree(parts);
}

void efx_mtd_rename(struct efx_nic *efx)
{
	struct efx_mtd_partition *part;

	ASSERT_RTNL();

	list_for_each_entry(part, &efx->mtd_list, node)
		efx_mtd_rename_partition(part);
}

int efx_mtd_probe(struct efx_nic *efx)
{
	if (efx_nic_rev(efx) >= EFX_REV_SIENA_A0)
		return siena_mtd_probe(efx);
	else
		return falcon_mtd_probe(efx);
}

/* Implementation of MTD operations for Falcon */

static void falcon_mtd_rename(struct efx_mtd_partition *part)
{
	struct efx_nic *efx = part->mtd.priv;

	snprintf(part->name, sizeof(part->name), "%s %s",
		 efx->name, part->type_name);
}

static int falcon_mtd_read(struct mtd_info *mtd, loff_t start,
			   size_t len, size_t *retlen, u8 *buffer)
{
	struct falcon_mtd_partition *part = to_falcon_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	struct falcon_nic_data *nic_data = efx->nic_data;
	int rc;

	rc = mutex_lock_interruptible(&nic_data->spi_lock);
	if (rc)
		return rc;
	rc = falcon_spi_read(efx, part->spi, part->offset + start,
			     len, retlen, buffer);
	mutex_unlock(&nic_data->spi_lock);
	return rc;
}

static int falcon_mtd_erase(struct mtd_info *mtd, loff_t start, size_t len)
{
	struct falcon_mtd_partition *part = to_falcon_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	struct falcon_nic_data *nic_data = efx->nic_data;
	int rc;

	rc = mutex_lock_interruptible(&nic_data->spi_lock);
	if (rc)
		return rc;
	rc = falcon_spi_erase(part, part->offset + start, len);
	mutex_unlock(&nic_data->spi_lock);
	return rc;
}

static int falcon_mtd_write(struct mtd_info *mtd, loff_t start,
			    size_t len, size_t *retlen, const u8 *buffer)
{
	struct falcon_mtd_partition *part = to_falcon_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	struct falcon_nic_data *nic_data = efx->nic_data;
	int rc;

	rc = mutex_lock_interruptible(&nic_data->spi_lock);
	if (rc)
		return rc;
	rc = falcon_spi_write(efx, part->spi, part->offset + start,
			      len, retlen, buffer);
	mutex_unlock(&nic_data->spi_lock);
	return rc;
}

static int falcon_mtd_sync(struct mtd_info *mtd)
{
	struct falcon_mtd_partition *part = to_falcon_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	struct falcon_nic_data *nic_data = efx->nic_data;
	int rc;

	mutex_lock(&nic_data->spi_lock);
	rc = falcon_spi_slow_wait(part, true);
	mutex_unlock(&nic_data->spi_lock);
	return rc;
}

static const struct efx_mtd_ops falcon_mtd_ops = {
	.rename	= falcon_mtd_rename,
	.read	= falcon_mtd_read,
	.erase	= falcon_mtd_erase,
	.write	= falcon_mtd_write,
	.sync	= falcon_mtd_sync,
};

static int falcon_mtd_probe(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	struct falcon_mtd_partition *parts;
	struct falcon_spi_device *spi;
	size_t n_parts;
	int rc = -ENODEV;

	ASSERT_RTNL();

	efx->mtd_ops = &falcon_mtd_ops;

	/* Allocate space for maximum number of partitions */
	parts = kcalloc(2, sizeof(*parts), GFP_KERNEL);
	n_parts = 0;

	spi = &nic_data->spi_flash;
	if (falcon_spi_present(spi) && spi->size > FALCON_FLASH_BOOTCODE_START) {
		parts[n_parts].spi = spi;
		parts[n_parts].offset = FALCON_FLASH_BOOTCODE_START;
		parts[n_parts].common.dev_type_name = "flash";
		parts[n_parts].common.type_name = "sfc_flash_bootrom";
		parts[n_parts].common.mtd.type = MTD_NORFLASH;
		parts[n_parts].common.mtd.flags = MTD_CAP_NORFLASH;
		parts[n_parts].common.mtd.size = spi->size - FALCON_FLASH_BOOTCODE_START;
		parts[n_parts].common.mtd.erasesize = spi->erase_size;
		n_parts++;
	}

	spi = &nic_data->spi_eeprom;
	if (falcon_spi_present(spi) && spi->size > FALCON_EEPROM_BOOTCONFIG_START) {
		parts[n_parts].spi = spi;
		parts[n_parts].offset = FALCON_EEPROM_BOOTCONFIG_START;
		parts[n_parts].common.dev_type_name = "EEPROM";
		parts[n_parts].common.type_name = "sfc_bootconfig";
		parts[n_parts].common.mtd.type = MTD_RAM;
		parts[n_parts].common.mtd.flags = MTD_CAP_RAM;
		parts[n_parts].common.mtd.size =
			min(spi->size, FALCON_EEPROM_BOOTCONFIG_END) -
			FALCON_EEPROM_BOOTCONFIG_START;
		parts[n_parts].common.mtd.erasesize = spi->erase_size;
		n_parts++;
	}

	rc = efx_mtd_add(efx, &parts[0].common, n_parts, sizeof(*parts));
	if (rc)
		kfree(parts);
	return rc;
}

/* Implementation of MTD operations for Siena */

struct efx_mcdi_mtd_partition {
	struct efx_mtd_partition common;
	bool updating;
	u8 nvram_type;
	u16 fw_subtype;
};

#define to_efx_mcdi_mtd_partition(mtd)				\
	container_of(mtd, struct efx_mcdi_mtd_partition, common.mtd)

static void siena_mtd_rename(struct efx_mtd_partition *part)
{
	struct efx_mcdi_mtd_partition *mcdi_part =
		container_of(part, struct efx_mcdi_mtd_partition, common);
	struct efx_nic *efx = part->mtd.priv;

	snprintf(part->name, sizeof(part->name), "%s %s:%02x",
		 efx->name, part->type_name, mcdi_part->fw_subtype);
}

static int siena_mtd_read(struct mtd_info *mtd, loff_t start,
			  size_t len, size_t *retlen, u8 *buffer)
{
	struct efx_mcdi_mtd_partition *part = to_efx_mcdi_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	loff_t offset = start;
	loff_t end = min_t(loff_t, start + len, mtd->size);
	size_t chunk;
	int rc = 0;

	while (offset < end) {
		chunk = min_t(size_t, end - offset, EFX_MCDI_NVRAM_LEN_MAX);
		rc = efx_mcdi_nvram_read(efx, part->nvram_type, offset,
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
	struct efx_mcdi_mtd_partition *part = to_efx_mcdi_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	loff_t offset = start & ~((loff_t)(mtd->erasesize - 1));
	loff_t end = min_t(loff_t, start + len, mtd->size);
	size_t chunk = part->common.mtd.erasesize;
	int rc = 0;

	if (!part->updating) {
		rc = efx_mcdi_nvram_update_start(efx, part->nvram_type);
		if (rc)
			goto out;
		part->updating = true;
	}

	/* The MCDI interface can in fact do multiple erase blocks at once;
	 * but erasing may be slow, so we make multiple calls here to avoid
	 * tripping the MCDI RPC timeout. */
	while (offset < end) {
		rc = efx_mcdi_nvram_erase(efx, part->nvram_type, offset,
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
	struct efx_mcdi_mtd_partition *part = to_efx_mcdi_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	loff_t offset = start;
	loff_t end = min_t(loff_t, start + len, mtd->size);
	size_t chunk;
	int rc = 0;

	if (!part->updating) {
		rc = efx_mcdi_nvram_update_start(efx, part->nvram_type);
		if (rc)
			goto out;
		part->updating = true;
	}

	while (offset < end) {
		chunk = min_t(size_t, end - offset, EFX_MCDI_NVRAM_LEN_MAX);
		rc = efx_mcdi_nvram_write(efx, part->nvram_type, offset,
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
	struct efx_mcdi_mtd_partition *part = to_efx_mcdi_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	int rc = 0;

	if (part->updating) {
		part->updating = false;
		rc = efx_mcdi_nvram_update_finish(efx, part->nvram_type);
	}

	return rc;
}

static const struct efx_mtd_ops siena_mtd_ops = {
	.rename	= siena_mtd_rename,
	.read	= siena_mtd_read,
	.erase	= siena_mtd_erase,
	.write	= siena_mtd_write,
	.sync	= siena_mtd_sync,
};

struct siena_nvram_type_info {
	int port;
	const char *name;
};

static const struct siena_nvram_type_info siena_nvram_types[] = {
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
	[MC_CMD_NVRAM_TYPE_FPGA]		= { 0, "sfc_fpga" },
};

static int siena_mtd_probe_partition(struct efx_nic *efx,
				     struct efx_mcdi_mtd_partition *part,
				     unsigned int type)
{
	const struct siena_nvram_type_info *info;
	size_t size, erase_size;
	bool protected;
	int rc;

	if (type >= ARRAY_SIZE(siena_nvram_types) ||
	    siena_nvram_types[type].name == NULL)
		return -ENODEV;

	info = &siena_nvram_types[type];

	if (info->port != efx_port_num(efx))
		return -ENODEV;

	rc = efx_mcdi_nvram_info(efx, type, &size, &erase_size, &protected);
	if (rc)
		return rc;
	if (protected)
		return -ENODEV; /* hide it */

	part->nvram_type = type;
	part->common.dev_type_name = "Siena NVRAM manager";
	part->common.type_name = info->name;

	part->common.mtd.type = MTD_NORFLASH;
	part->common.mtd.flags = MTD_CAP_NORFLASH;
	part->common.mtd.size = size;
	part->common.mtd.erasesize = erase_size;

	return 0;
}

static int siena_mtd_get_fw_subtypes(struct efx_nic *efx,
				     struct efx_mcdi_mtd_partition *parts,
				     size_t n_parts)
{
	uint16_t fw_subtype_list[
		MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_MAXNUM];
	size_t i;
	int rc;

	rc = efx_mcdi_get_board_cfg(efx, NULL, fw_subtype_list, NULL);
	if (rc)
		return rc;

	for (i = 0; i < n_parts; i++)
		parts[i].fw_subtype = fw_subtype_list[parts[i].nvram_type];

	return 0;
}

static int siena_mtd_probe(struct efx_nic *efx)
{
	struct efx_mcdi_mtd_partition *parts;
	u32 nvram_types;
	unsigned int type;
	size_t n_parts;
	int rc;

	ASSERT_RTNL();

	efx->mtd_ops = &siena_mtd_ops;

	rc = efx_mcdi_nvram_types(efx, &nvram_types);
	if (rc)
		return rc;

	parts = kcalloc(hweight32(nvram_types), sizeof(*parts), GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	type = 0;
	n_parts = 0;

	while (nvram_types != 0) {
		if (nvram_types & 1) {
			rc = siena_mtd_probe_partition(efx, &parts[n_parts],
						       type);
			if (rc == 0)
				n_parts++;
			else if (rc != -ENODEV)
				goto fail;
		}
		type++;
		nvram_types >>= 1;
	}

	rc = siena_mtd_get_fw_subtypes(efx, parts, n_parts);
	if (rc)
		goto fail;

	rc = efx_mtd_add(efx, &parts[0].common, n_parts, sizeof(*parts));
fail:
	if (rc)
		kfree(parts);
	return rc;
}

