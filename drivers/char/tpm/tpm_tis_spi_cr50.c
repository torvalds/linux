// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 Google, Inc
 *
 * This device driver implements a TCG PTP FIFO interface over SPI for chips
 * with Cr50 firmware.
 * It is based on tpm_tis_spi driver by Peter Huewe and Christophe Ricard.
 */

#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/spi/spi.h>
#include <linux/wait.h>

#include "tpm_tis_core.h"
#include "tpm_tis_spi.h"

/*
 * Cr50 timing constants:
 * - can go to sleep not earlier than after CR50_SLEEP_DELAY_MSEC.
 * - needs up to CR50_WAKE_START_DELAY_USEC to wake after sleep.
 * - requires waiting for "ready" IRQ, if supported; or waiting for at least
 *   CR50_NOIRQ_ACCESS_DELAY_MSEC between transactions, if IRQ is not supported.
 * - waits for up to CR50_FLOW_CONTROL for flow control 'ready' indication.
 */
#define CR50_SLEEP_DELAY_MSEC			1000
#define CR50_WAKE_START_DELAY_USEC		1000
#define CR50_NOIRQ_ACCESS_DELAY			msecs_to_jiffies(2)
#define CR50_READY_IRQ_TIMEOUT			msecs_to_jiffies(TPM2_TIMEOUT_A)
#define CR50_FLOW_CONTROL			msecs_to_jiffies(TPM2_TIMEOUT_A)
#define MAX_IRQ_CONFIRMATION_ATTEMPTS		3

#define TPM_CR50_FW_VER(l)			(0x0f90 | ((l) << 12))
#define TPM_CR50_MAX_FW_VER_LEN			64

/* Default quality for hwrng. */
#define TPM_CR50_DEFAULT_RNG_QUALITY		700

struct cr50_spi_phy {
	struct tpm_tis_spi_phy spi_phy;

	struct mutex time_track_mutex;
	unsigned long last_access;

	unsigned long access_delay;

	unsigned int irq_confirmation_attempt;
	bool irq_needs_confirmation;
	bool irq_confirmed;
};

static inline struct cr50_spi_phy *to_cr50_spi_phy(struct tpm_tis_spi_phy *phy)
{
	return container_of(phy, struct cr50_spi_phy, spi_phy);
}

/*
 * The cr50 interrupt handler just signals waiting threads that the
 * interrupt was asserted.  It does not do any processing triggered
 * by interrupts but is instead used to avoid fixed delays.
 */
static irqreturn_t cr50_spi_irq_handler(int dummy, void *dev_id)
{
	struct cr50_spi_phy *cr50_phy = dev_id;

	cr50_phy->irq_confirmed = true;
	complete(&cr50_phy->spi_phy.ready);

	return IRQ_HANDLED;
}

/*
 * Cr50 needs to have at least some delay between consecutive
 * transactions. Make sure we wait.
 */
static void cr50_ensure_access_delay(struct cr50_spi_phy *phy)
{
	unsigned long allowed_access = phy->last_access + phy->access_delay;
	unsigned long time_now = jiffies;
	struct device *dev = &phy->spi_phy.spi_device->dev;

	/*
	 * Note: There is a small chance, if Cr50 is not accessed in a few days,
	 * that time_in_range will not provide the correct result after the wrap
	 * around for jiffies. In this case, we'll have an unneeded short delay,
	 * which is fine.
	 */
	if (time_in_range_open(time_now, phy->last_access, allowed_access)) {
		unsigned long remaining, timeout = allowed_access - time_now;

		remaining = wait_for_completion_timeout(&phy->spi_phy.ready,
							timeout);
		if (!remaining && phy->irq_confirmed)
			dev_warn(dev, "Timeout waiting for TPM ready IRQ\n");
	}

	if (phy->irq_needs_confirmation) {
		unsigned int attempt = ++phy->irq_confirmation_attempt;

		if (phy->irq_confirmed) {
			phy->irq_needs_confirmation = false;
			phy->access_delay = CR50_READY_IRQ_TIMEOUT;
			dev_info(dev, "TPM ready IRQ confirmed on attempt %u\n",
				 attempt);
		} else if (attempt > MAX_IRQ_CONFIRMATION_ATTEMPTS) {
			phy->irq_needs_confirmation = false;
			dev_warn(dev, "IRQ not confirmed - will use delays\n");
		}
	}
}

/*
 * Cr50 might go to sleep if there is no SPI activity for some time and
 * miss the first few bits/bytes on the bus. In such case, wake it up
 * by asserting CS and give it time to start up.
 */
static bool cr50_needs_waking(struct cr50_spi_phy *phy)
{
	/*
	 * Note: There is a small chance, if Cr50 is not accessed in a few days,
	 * that time_in_range will not provide the correct result after the wrap
	 * around for jiffies. In this case, we'll probably timeout or read
	 * incorrect value from TPM_STS and just retry the operation.
	 */
	return !time_in_range_open(jiffies, phy->last_access,
				   phy->spi_phy.wake_after);
}

static void cr50_wake_if_needed(struct cr50_spi_phy *cr50_phy)
{
	struct tpm_tis_spi_phy *phy = &cr50_phy->spi_phy;

	if (cr50_needs_waking(cr50_phy)) {
		/* Assert CS, wait 1 msec, deassert CS */
		struct spi_transfer spi_cs_wake = {
			.delay = {
				.value = 1000,
				.unit = SPI_DELAY_UNIT_USECS
			}
		};

		spi_sync_transfer(phy->spi_device, &spi_cs_wake, 1);
		/* Wait for it to fully wake */
		usleep_range(CR50_WAKE_START_DELAY_USEC,
			     CR50_WAKE_START_DELAY_USEC * 2);
	}

	/* Reset the time when we need to wake Cr50 again */
	phy->wake_after = jiffies + msecs_to_jiffies(CR50_SLEEP_DELAY_MSEC);
}

/*
 * Flow control: clock the bus and wait for cr50 to set LSB before
 * sending/receiving data. TCG PTP spec allows it to happen during
 * the last byte of header, but cr50 never does that in practice,
 * and earlier versions had a bug when it was set too early, so don't
 * check for it during header transfer.
 */
static int cr50_spi_flow_control(struct tpm_tis_spi_phy *phy,
				 struct spi_transfer *spi_xfer)
{
	struct device *dev = &phy->spi_device->dev;
	unsigned long timeout = jiffies + CR50_FLOW_CONTROL;
	struct spi_message m;
	int ret;

	spi_xfer->len = 1;

	do {
		spi_message_init(&m);
		spi_message_add_tail(spi_xfer, &m);
		ret = spi_sync_locked(phy->spi_device, &m);
		if (ret < 0)
			return ret;

		if (time_after(jiffies, timeout)) {
			dev_warn(dev, "Timeout during flow control\n");
			return -EBUSY;
		}
	} while (!(phy->iobuf[0] & 0x01));

	return 0;
}

static bool tpm_cr50_spi_is_firmware_power_managed(struct device *dev)
{
	u8 val;
	int ret;

	/* This flag should default true when the device property is not present */
	ret = device_property_read_u8(dev, "firmware-power-managed", &val);
	if (ret)
		return true;

	return val;
}

static int tpm_tis_spi_cr50_transfer(struct tpm_tis_data *data, u32 addr, u16 len,
				     u8 *in, const u8 *out)
{
	struct tpm_tis_spi_phy *phy = to_tpm_tis_spi_phy(data);
	struct cr50_spi_phy *cr50_phy = to_cr50_spi_phy(phy);
	int ret;

	mutex_lock(&cr50_phy->time_track_mutex);
	/*
	 * Do this outside of spi_bus_lock in case cr50 is not the
	 * only device on that spi bus.
	 */
	cr50_ensure_access_delay(cr50_phy);
	cr50_wake_if_needed(cr50_phy);

	ret = tpm_tis_spi_transfer(data, addr, len, in, out);

	cr50_phy->last_access = jiffies;
	mutex_unlock(&cr50_phy->time_track_mutex);

	return ret;
}

static int tpm_tis_spi_cr50_read_bytes(struct tpm_tis_data *data, u32 addr,
				       u16 len, u8 *result, enum tpm_tis_io_mode io_mode)
{
	return tpm_tis_spi_cr50_transfer(data, addr, len, result, NULL);
}

static int tpm_tis_spi_cr50_write_bytes(struct tpm_tis_data *data, u32 addr,
					u16 len, const u8 *value, enum tpm_tis_io_mode io_mode)
{
	return tpm_tis_spi_cr50_transfer(data, addr, len, NULL, value);
}

static const struct tpm_tis_phy_ops tpm_spi_cr50_phy_ops = {
	.read_bytes = tpm_tis_spi_cr50_read_bytes,
	.write_bytes = tpm_tis_spi_cr50_write_bytes,
};

static void cr50_print_fw_version(struct tpm_tis_data *data)
{
	struct tpm_tis_spi_phy *phy = to_tpm_tis_spi_phy(data);
	int i, len = 0;
	char fw_ver[TPM_CR50_MAX_FW_VER_LEN + 1];
	char fw_ver_block[4];

	/*
	 * Write anything to TPM_CR50_FW_VER to start from the beginning
	 * of the version string
	 */
	tpm_tis_write8(data, TPM_CR50_FW_VER(data->locality), 0);

	/* Read the string, 4 bytes at a time, until we get '\0' */
	do {
		tpm_tis_read_bytes(data, TPM_CR50_FW_VER(data->locality), 4,
				   fw_ver_block);
		for (i = 0; i < 4 && fw_ver_block[i]; ++len, ++i)
			fw_ver[len] = fw_ver_block[i];
	} while (i == 4 && len < TPM_CR50_MAX_FW_VER_LEN);
	fw_ver[len] = '\0';

	dev_info(&phy->spi_device->dev, "Cr50 firmware version: %s\n", fw_ver);
}

int cr50_spi_probe(struct spi_device *spi)
{
	struct tpm_tis_spi_phy *phy;
	struct cr50_spi_phy *cr50_phy;
	int ret;
	struct tpm_chip *chip;

	cr50_phy = devm_kzalloc(&spi->dev, sizeof(*cr50_phy), GFP_KERNEL);
	if (!cr50_phy)
		return -ENOMEM;

	phy = &cr50_phy->spi_phy;
	phy->flow_control = cr50_spi_flow_control;
	phy->wake_after = jiffies;
	phy->priv.rng_quality = TPM_CR50_DEFAULT_RNG_QUALITY;
	init_completion(&phy->ready);

	cr50_phy->access_delay = CR50_NOIRQ_ACCESS_DELAY;
	cr50_phy->last_access = jiffies;
	mutex_init(&cr50_phy->time_track_mutex);

	if (spi->irq > 0) {
		ret = devm_request_irq(&spi->dev, spi->irq,
				       cr50_spi_irq_handler,
				       IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				       "cr50_spi", cr50_phy);
		if (ret < 0) {
			if (ret == -EPROBE_DEFER)
				return ret;
			dev_warn(&spi->dev, "Requesting IRQ %d failed: %d\n",
				 spi->irq, ret);
			/*
			 * This is not fatal, the driver will fall back to
			 * delays automatically, since ready will never
			 * be completed without a registered irq handler.
			 * So, just fall through.
			 */
		} else {
			/*
			 * IRQ requested, let's verify that it is actually
			 * triggered, before relying on it.
			 */
			cr50_phy->irq_needs_confirmation = true;
		}
	} else {
		dev_warn(&spi->dev,
			 "No IRQ - will use delays between transactions.\n");
	}

	ret = tpm_tis_spi_init(spi, phy, -1, &tpm_spi_cr50_phy_ops);
	if (ret)
		return ret;

	cr50_print_fw_version(&phy->priv);

	chip = dev_get_drvdata(&spi->dev);
	if (tpm_cr50_spi_is_firmware_power_managed(&spi->dev))
		chip->flags |= TPM_CHIP_FLAG_FIRMWARE_POWER_MANAGED;

	return 0;
}

#ifdef CONFIG_PM_SLEEP
int tpm_tis_spi_resume(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	struct tpm_tis_data *data = dev_get_drvdata(&chip->dev);
	struct tpm_tis_spi_phy *phy = to_tpm_tis_spi_phy(data);
	/*
	 * Jiffies not increased during suspend, so we need to reset
	 * the time to wake Cr50 after resume.
	 */
	phy->wake_after = jiffies;

	return tpm_tis_resume(dev);
}
#endif
