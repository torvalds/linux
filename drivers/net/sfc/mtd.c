/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/delay.h>

#define EFX_DRIVER_NAME "sfc_mtd"
#include "net_driver.h"
#include "spi.h"

#define EFX_SPI_VERIFY_BUF_LEN 16

struct efx_mtd {
	const struct efx_spi_device *spi;
	struct mtd_info mtd;
	char name[IFNAMSIZ + 20];
};

/* SPI utilities */

static int efx_spi_slow_wait(struct efx_mtd *efx_mtd, bool uninterruptible)
{
	const struct efx_spi_device *spi = efx_mtd->spi;
	struct efx_nic *efx = spi->efx;
	u8 status;
	int rc, i;

	/* Wait up to 4s for flash/EEPROM to finish a slow operation. */
	for (i = 0; i < 40; i++) {
		__set_current_state(uninterruptible ?
				    TASK_UNINTERRUPTIBLE : TASK_INTERRUPTIBLE);
		schedule_timeout(HZ / 10);
		rc = falcon_spi_cmd(spi, SPI_RDSR, -1, NULL,
				    &status, sizeof(status));
		if (rc)
			return rc;
		if (!(status & SPI_STATUS_NRDY))
			return 0;
		if (signal_pending(current))
			return -EINTR;
	}
	EFX_ERR(efx, "timed out waiting for %s\n", efx_mtd->name);
	return -ETIMEDOUT;
}

static int efx_spi_unlock(const struct efx_spi_device *spi)
{
	const u8 unlock_mask = (SPI_STATUS_BP2 | SPI_STATUS_BP1 |
				SPI_STATUS_BP0);
	u8 status;
	int rc;

	rc = falcon_spi_cmd(spi, SPI_RDSR, -1, NULL, &status, sizeof(status));
	if (rc)
		return rc;

	if (!(status & unlock_mask))
		return 0; /* already unlocked */

	rc = falcon_spi_cmd(spi, SPI_WREN, -1, NULL, NULL, 0);
	if (rc)
		return rc;
	rc = falcon_spi_cmd(spi, SPI_SST_EWSR, -1, NULL, NULL, 0);
	if (rc)
		return rc;

	status &= ~unlock_mask;
	rc = falcon_spi_cmd(spi, SPI_WRSR, -1, &status, NULL, sizeof(status));
	if (rc)
		return rc;
	rc = falcon_spi_wait_write(spi);
	if (rc)
		return rc;

	return 0;
}

static int efx_spi_erase(struct efx_mtd *efx_mtd, loff_t start, size_t len)
{
	const struct efx_spi_device *spi = efx_mtd->spi;
	unsigned pos, block_len;
	u8 empty[EFX_SPI_VERIFY_BUF_LEN];
	u8 buffer[EFX_SPI_VERIFY_BUF_LEN];
	int rc;

	if (len != spi->erase_size)
		return -EINVAL;

	if (spi->erase_command == 0)
		return -EOPNOTSUPP;

	rc = efx_spi_unlock(spi);
	if (rc)
		return rc;
	rc = falcon_spi_cmd(spi, SPI_WREN, -1, NULL, NULL, 0);
	if (rc)
		return rc;
	rc = falcon_spi_cmd(spi, spi->erase_command, start, NULL, NULL, 0);
	if (rc)
		return rc;
	rc = efx_spi_slow_wait(efx_mtd, false);

	/* Verify the entire region has been wiped */
	memset(empty, 0xff, sizeof(empty));
	for (pos = 0; pos < len; pos += block_len) {
		block_len = min(len - pos, sizeof(buffer));
		rc = falcon_spi_read(spi, start + pos, block_len, NULL, buffer);
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

static int efx_mtd_read(struct mtd_info *mtd, loff_t start, size_t len,
			size_t *retlen, u8 *buffer)
{
	struct efx_mtd *efx_mtd = mtd->priv;
	const struct efx_spi_device *spi = efx_mtd->spi;
	struct efx_nic *efx = spi->efx;
	int rc;

	rc = mutex_lock_interruptible(&efx->spi_lock);
	if (rc)
		return rc;
	rc = falcon_spi_read(spi, FALCON_FLASH_BOOTCODE_START + start,
			     len, retlen, buffer);
	mutex_unlock(&efx->spi_lock);
	return rc;
}

static int efx_mtd_erase(struct mtd_info *mtd, struct erase_info *erase)
{
	struct efx_mtd *efx_mtd = mtd->priv;
	struct efx_nic *efx = efx_mtd->spi->efx;
	int rc;

	rc = mutex_lock_interruptible(&efx->spi_lock);
	if (rc)
		return rc;
	rc = efx_spi_erase(efx_mtd, FALCON_FLASH_BOOTCODE_START + erase->addr,
			   erase->len);
	mutex_unlock(&efx->spi_lock);

	if (rc == 0) {
		erase->state = MTD_ERASE_DONE;
	} else {
		erase->state = MTD_ERASE_FAILED;
		erase->fail_addr = 0xffffffff;
	}
	mtd_erase_callback(erase);
	return rc;
}

static int efx_mtd_write(struct mtd_info *mtd, loff_t start,
			 size_t len, size_t *retlen, const u8 *buffer)
{
	struct efx_mtd *efx_mtd = mtd->priv;
	const struct efx_spi_device *spi = efx_mtd->spi;
	struct efx_nic *efx = spi->efx;
	int rc;

	rc = mutex_lock_interruptible(&efx->spi_lock);
	if (rc)
		return rc;
	rc = falcon_spi_write(spi, FALCON_FLASH_BOOTCODE_START + start,
			      len, retlen, buffer);
	mutex_unlock(&efx->spi_lock);
	return rc;
}

static void efx_mtd_sync(struct mtd_info *mtd)
{
	struct efx_mtd *efx_mtd = mtd->priv;
	struct efx_nic *efx = efx_mtd->spi->efx;
	int rc;

	mutex_lock(&efx->spi_lock);
	rc = efx_spi_slow_wait(efx_mtd, true);
	mutex_unlock(&efx->spi_lock);

	if (rc)
		EFX_ERR(efx, "%s sync failed (%d)\n", efx_mtd->name, rc);
	return;
}

void efx_mtd_remove(struct efx_nic *efx)
{
	if (efx->spi_flash && efx->spi_flash->mtd) {
		struct efx_mtd *efx_mtd = efx->spi_flash->mtd;
		int rc;

		for (;;) {
			rc = del_mtd_device(&efx_mtd->mtd);
			if (rc != -EBUSY)
				break;
			ssleep(1);
		}
		WARN_ON(rc);
		kfree(efx_mtd);
	}
}

void efx_mtd_rename(struct efx_nic *efx)
{
	if (efx->spi_flash && efx->spi_flash->mtd) {
		struct efx_mtd *efx_mtd = efx->spi_flash->mtd;
		snprintf(efx_mtd->name, sizeof(efx_mtd->name),
			 "%s sfc_flash_bootrom", efx->name);
	}
}

int efx_mtd_probe(struct efx_nic *efx)
{
	struct efx_spi_device *spi = efx->spi_flash;
	struct efx_mtd *efx_mtd;

	if (!spi || spi->size <= FALCON_FLASH_BOOTCODE_START)
		return -ENODEV;

	efx_mtd = kzalloc(sizeof(*efx_mtd), GFP_KERNEL);
	if (!efx_mtd)
		return -ENOMEM;

	efx_mtd->spi = spi;
	spi->mtd = efx_mtd;

	efx_mtd->mtd.type = MTD_NORFLASH;
	efx_mtd->mtd.flags = MTD_CAP_NORFLASH;
	efx_mtd->mtd.size = spi->size - FALCON_FLASH_BOOTCODE_START;
	efx_mtd->mtd.erasesize = spi->erase_size;
	efx_mtd->mtd.writesize = 1;
	efx_mtd_rename(efx);

	efx_mtd->mtd.owner = THIS_MODULE;
	efx_mtd->mtd.priv = efx_mtd;
	efx_mtd->mtd.name = efx_mtd->name;
	efx_mtd->mtd.erase = efx_mtd_erase;
	efx_mtd->mtd.read = efx_mtd_read;
	efx_mtd->mtd.write = efx_mtd_write;
	efx_mtd->mtd.sync = efx_mtd_sync;

	if (add_mtd_device(&efx_mtd->mtd)) {
		kfree(efx_mtd);
		spi->mtd = NULL;
		/* add_mtd_device() returns 1 if the MTD table is full */
		return -ENOMEM;
	}

	return 0;
}
