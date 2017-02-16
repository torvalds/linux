/*
 * Copyright (C) 2005, 2006 IBM Corporation
 * Copyright (C) 2014, 2015 Intel Corporation
 *
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This device driver implements the TPM interface as defined in
 * the TCG TPM Interface Spec version 1.2, revision 1.0.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pnp.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/acpi.h>
#include <linux/freezer.h>
#include "tpm.h"
#include "tpm_tis_core.h"

/* Before we attempt to access the TPM we must see that the valid bit is set.
 * The specification says that this bit is 0 at reset and remains 0 until the
 * 'TPM has gone through its self test and initialization and has established
 * correct values in the other bits.'
 */
static int wait_startup(struct tpm_chip *chip, int l)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	unsigned long stop = jiffies + chip->timeout_a;

	do {
		int rc;
		u8 access;

		rc = tpm_tis_read8(priv, TPM_ACCESS(l), &access);
		if (rc < 0)
			return rc;

		if (access & TPM_ACCESS_VALID)
			return 0;
		msleep(TPM_TIMEOUT);
	} while (time_before(jiffies, stop));
	return -1;
}

static int check_locality(struct tpm_chip *chip, int l)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	int rc;
	u8 access;

	rc = tpm_tis_read8(priv, TPM_ACCESS(l), &access);
	if (rc < 0)
		return rc;

	if ((access & (TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID)) ==
	    (TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID))
		return priv->locality = l;

	return -1;
}

static void release_locality(struct tpm_chip *chip, int l, int force)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	int rc;
	u8 access;

	rc = tpm_tis_read8(priv, TPM_ACCESS(l), &access);
	if (rc < 0)
		return;

	if (force || (access &
		      (TPM_ACCESS_REQUEST_PENDING | TPM_ACCESS_VALID)) ==
	    (TPM_ACCESS_REQUEST_PENDING | TPM_ACCESS_VALID))
		tpm_tis_write8(priv, TPM_ACCESS(l), TPM_ACCESS_ACTIVE_LOCALITY);

}

static int request_locality(struct tpm_chip *chip, int l)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	unsigned long stop, timeout;
	long rc;

	if (check_locality(chip, l) >= 0)
		return l;

	rc = tpm_tis_write8(priv, TPM_ACCESS(l), TPM_ACCESS_REQUEST_USE);
	if (rc < 0)
		return rc;

	stop = jiffies + chip->timeout_a;

	if (chip->flags & TPM_CHIP_FLAG_IRQ) {
again:
		timeout = stop - jiffies;
		if ((long)timeout <= 0)
			return -1;
		rc = wait_event_interruptible_timeout(priv->int_queue,
						      (check_locality
						       (chip, l) >= 0),
						      timeout);
		if (rc > 0)
			return l;
		if (rc == -ERESTARTSYS && freezing(current)) {
			clear_thread_flag(TIF_SIGPENDING);
			goto again;
		}
	} else {
		/* wait for burstcount */
		do {
			if (check_locality(chip, l) >= 0)
				return l;
			msleep(TPM_TIMEOUT);
		} while (time_before(jiffies, stop));
	}
	return -1;
}

static u8 tpm_tis_status(struct tpm_chip *chip)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	int rc;
	u8 status;

	rc = tpm_tis_read8(priv, TPM_STS(priv->locality), &status);
	if (rc < 0)
		return 0;

	return status;
}

static void tpm_tis_ready(struct tpm_chip *chip)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);

	/* this causes the current command to be aborted */
	tpm_tis_write8(priv, TPM_STS(priv->locality), TPM_STS_COMMAND_READY);
}

static int get_burstcount(struct tpm_chip *chip)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	unsigned long stop;
	int burstcnt, rc;
	u32 value;

	/* wait for burstcount */
	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		stop = jiffies + chip->timeout_a;
	else
		stop = jiffies + chip->timeout_d;
	do {
		rc = tpm_tis_read32(priv, TPM_STS(priv->locality), &value);
		if (rc < 0)
			return rc;

		burstcnt = (value >> 8) & 0xFFFF;
		if (burstcnt)
			return burstcnt;
		msleep(TPM_TIMEOUT);
	} while (time_before(jiffies, stop));
	return -EBUSY;
}

static int recv_data(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	int size = 0, burstcnt, rc;

	while (size < count) {
		rc = wait_for_tpm_stat(chip,
				 TPM_STS_DATA_AVAIL | TPM_STS_VALID,
				 chip->timeout_c,
				 &priv->read_queue, true);
		if (rc < 0)
			return rc;
		burstcnt = get_burstcount(chip);
		if (burstcnt < 0) {
			dev_err(&chip->dev, "Unable to read burstcount\n");
			return burstcnt;
		}
		burstcnt = min_t(int, burstcnt, count - size);

		rc = tpm_tis_read_bytes(priv, TPM_DATA_FIFO(priv->locality),
					burstcnt, buf + size);
		if (rc < 0)
			return rc;

		size += burstcnt;
	}
	return size;
}

static int tpm_tis_recv(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	int size = 0;
	int expected, status;

	if (count < TPM_HEADER_SIZE) {
		size = -EIO;
		goto out;
	}

	size = recv_data(chip, buf, TPM_HEADER_SIZE);
	/* read first 10 bytes, including tag, paramsize, and result */
	if (size < TPM_HEADER_SIZE) {
		dev_err(&chip->dev, "Unable to read header\n");
		goto out;
	}

	expected = be32_to_cpu(*(__be32 *) (buf + 2));
	if (expected > count) {
		size = -EIO;
		goto out;
	}

	size += recv_data(chip, &buf[TPM_HEADER_SIZE],
			  expected - TPM_HEADER_SIZE);
	if (size < expected) {
		dev_err(&chip->dev, "Unable to read remainder of result\n");
		size = -ETIME;
		goto out;
	}

	if (wait_for_tpm_stat(chip, TPM_STS_VALID, chip->timeout_c,
				&priv->int_queue, false) < 0) {
		size = -ETIME;
		goto out;
	}
	status = tpm_tis_status(chip);
	if (status & TPM_STS_DATA_AVAIL) {	/* retry? */
		dev_err(&chip->dev, "Error left over data\n");
		size = -EIO;
		goto out;
	}

out:
	tpm_tis_ready(chip);
	release_locality(chip, priv->locality, 0);
	return size;
}

/*
 * If interrupts are used (signaled by an irq set in the vendor structure)
 * tpm.c can skip polling for the data to be available as the interrupt is
 * waited for here
 */
static int tpm_tis_send_data(struct tpm_chip *chip, u8 *buf, size_t len)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	int rc, status, burstcnt;
	size_t count = 0;
	bool itpm = priv->flags & TPM_TIS_ITPM_WORKAROUND;

	if (request_locality(chip, 0) < 0)
		return -EBUSY;

	status = tpm_tis_status(chip);
	if ((status & TPM_STS_COMMAND_READY) == 0) {
		tpm_tis_ready(chip);
		if (wait_for_tpm_stat
		    (chip, TPM_STS_COMMAND_READY, chip->timeout_b,
		     &priv->int_queue, false) < 0) {
			rc = -ETIME;
			goto out_err;
		}
	}

	while (count < len - 1) {
		burstcnt = get_burstcount(chip);
		if (burstcnt < 0) {
			dev_err(&chip->dev, "Unable to read burstcount\n");
			rc = burstcnt;
			goto out_err;
		}
		burstcnt = min_t(int, burstcnt, len - count - 1);
		rc = tpm_tis_write_bytes(priv, TPM_DATA_FIFO(priv->locality),
					 burstcnt, buf + count);
		if (rc < 0)
			goto out_err;

		count += burstcnt;

		if (wait_for_tpm_stat(chip, TPM_STS_VALID, chip->timeout_c,
					&priv->int_queue, false) < 0) {
			rc = -ETIME;
			goto out_err;
		}
		status = tpm_tis_status(chip);
		if (!itpm && (status & TPM_STS_DATA_EXPECT) == 0) {
			rc = -EIO;
			goto out_err;
		}
	}

	/* write last byte */
	rc = tpm_tis_write8(priv, TPM_DATA_FIFO(priv->locality), buf[count]);
	if (rc < 0)
		goto out_err;

	if (wait_for_tpm_stat(chip, TPM_STS_VALID, chip->timeout_c,
				&priv->int_queue, false) < 0) {
		rc = -ETIME;
		goto out_err;
	}
	status = tpm_tis_status(chip);
	if (!itpm && (status & TPM_STS_DATA_EXPECT) != 0) {
		rc = -EIO;
		goto out_err;
	}

	return 0;

out_err:
	tpm_tis_ready(chip);
	release_locality(chip, priv->locality, 0);
	return rc;
}

static void disable_interrupts(struct tpm_chip *chip)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	u32 intmask;
	int rc;

	rc = tpm_tis_read32(priv, TPM_INT_ENABLE(priv->locality), &intmask);
	if (rc < 0)
		intmask = 0;

	intmask &= ~TPM_GLOBAL_INT_ENABLE;
	rc = tpm_tis_write32(priv, TPM_INT_ENABLE(priv->locality), intmask);

	devm_free_irq(chip->dev.parent, priv->irq, chip);
	priv->irq = 0;
	chip->flags &= ~TPM_CHIP_FLAG_IRQ;
}

/*
 * If interrupts are used (signaled by an irq set in the vendor structure)
 * tpm.c can skip polling for the data to be available as the interrupt is
 * waited for here
 */
static int tpm_tis_send_main(struct tpm_chip *chip, u8 *buf, size_t len)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	int rc;
	u32 ordinal;
	unsigned long dur;

	rc = tpm_tis_send_data(chip, buf, len);
	if (rc < 0)
		return rc;

	/* go and do it */
	rc = tpm_tis_write8(priv, TPM_STS(priv->locality), TPM_STS_GO);
	if (rc < 0)
		goto out_err;

	if (chip->flags & TPM_CHIP_FLAG_IRQ) {
		ordinal = be32_to_cpu(*((__be32 *) (buf + 6)));

		if (chip->flags & TPM_CHIP_FLAG_TPM2)
			dur = tpm2_calc_ordinal_duration(chip, ordinal);
		else
			dur = tpm_calc_ordinal_duration(chip, ordinal);

		if (wait_for_tpm_stat
		    (chip, TPM_STS_DATA_AVAIL | TPM_STS_VALID, dur,
		     &priv->read_queue, false) < 0) {
			rc = -ETIME;
			goto out_err;
		}
	}
	return len;
out_err:
	tpm_tis_ready(chip);
	release_locality(chip, priv->locality, 0);
	return rc;
}

static int tpm_tis_send(struct tpm_chip *chip, u8 *buf, size_t len)
{
	int rc, irq;
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);

	if (!(chip->flags & TPM_CHIP_FLAG_IRQ) || priv->irq_tested)
		return tpm_tis_send_main(chip, buf, len);

	/* Verify receipt of the expected IRQ */
	irq = priv->irq;
	priv->irq = 0;
	chip->flags &= ~TPM_CHIP_FLAG_IRQ;
	rc = tpm_tis_send_main(chip, buf, len);
	priv->irq = irq;
	chip->flags |= TPM_CHIP_FLAG_IRQ;
	if (!priv->irq_tested)
		msleep(1);
	if (!priv->irq_tested)
		disable_interrupts(chip);
	priv->irq_tested = true;
	return rc;
}

struct tis_vendor_timeout_override {
	u32 did_vid;
	unsigned long timeout_us[4];
};

static const struct tis_vendor_timeout_override vendor_timeout_overrides[] = {
	/* Atmel 3204 */
	{ 0x32041114, { (TIS_SHORT_TIMEOUT*1000), (TIS_LONG_TIMEOUT*1000),
			(TIS_SHORT_TIMEOUT*1000), (TIS_SHORT_TIMEOUT*1000) } },
};

static bool tpm_tis_update_timeouts(struct tpm_chip *chip,
				    unsigned long *timeout_cap)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	int i, rc;
	u32 did_vid;

	rc = tpm_tis_read32(priv, TPM_DID_VID(0), &did_vid);
	if (rc < 0)
		return rc;

	for (i = 0; i != ARRAY_SIZE(vendor_timeout_overrides); i++) {
		if (vendor_timeout_overrides[i].did_vid != did_vid)
			continue;
		memcpy(timeout_cap, vendor_timeout_overrides[i].timeout_us,
		       sizeof(vendor_timeout_overrides[i].timeout_us));
		return true;
	}

	return false;
}

/*
 * Early probing for iTPM with STS_DATA_EXPECT flaw.
 * Try sending command without itpm flag set and if that
 * fails, repeat with itpm flag set.
 */
static int probe_itpm(struct tpm_chip *chip)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	int rc = 0;
	u8 cmd_getticks[] = {
		0x00, 0xc1, 0x00, 0x00, 0x00, 0x0a,
		0x00, 0x00, 0x00, 0xf1
	};
	size_t len = sizeof(cmd_getticks);
	u16 vendor;

	if (priv->flags & TPM_TIS_ITPM_WORKAROUND)
		return 0;

	rc = tpm_tis_read16(priv, TPM_DID_VID(0), &vendor);
	if (rc < 0)
		return rc;

	/* probe only iTPMS */
	if (vendor != TPM_VID_INTEL)
		return 0;

	rc = tpm_tis_send_data(chip, cmd_getticks, len);
	if (rc == 0)
		goto out;

	tpm_tis_ready(chip);
	release_locality(chip, priv->locality, 0);

	priv->flags |= TPM_TIS_ITPM_WORKAROUND;

	rc = tpm_tis_send_data(chip, cmd_getticks, len);
	if (rc == 0)
		dev_info(&chip->dev, "Detected an iTPM.\n");
	else {
		priv->flags &= ~TPM_TIS_ITPM_WORKAROUND;
		rc = -EFAULT;
	}

out:
	tpm_tis_ready(chip);
	release_locality(chip, priv->locality, 0);

	return rc;
}

static bool tpm_tis_req_canceled(struct tpm_chip *chip, u8 status)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);

	switch (priv->manufacturer_id) {
	case TPM_VID_WINBOND:
		return ((status == TPM_STS_VALID) ||
			(status == (TPM_STS_VALID | TPM_STS_COMMAND_READY)));
	case TPM_VID_STM:
		return (status == (TPM_STS_VALID | TPM_STS_COMMAND_READY));
	default:
		return (status == TPM_STS_COMMAND_READY);
	}
}

static irqreturn_t tis_int_handler(int dummy, void *dev_id)
{
	struct tpm_chip *chip = dev_id;
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	u32 interrupt;
	int i, rc;

	rc = tpm_tis_read32(priv, TPM_INT_STATUS(priv->locality), &interrupt);
	if (rc < 0)
		return IRQ_NONE;

	if (interrupt == 0)
		return IRQ_NONE;

	priv->irq_tested = true;
	if (interrupt & TPM_INTF_DATA_AVAIL_INT)
		wake_up_interruptible(&priv->read_queue);
	if (interrupt & TPM_INTF_LOCALITY_CHANGE_INT)
		for (i = 0; i < 5; i++)
			if (check_locality(chip, i) >= 0)
				break;
	if (interrupt &
	    (TPM_INTF_LOCALITY_CHANGE_INT | TPM_INTF_STS_VALID_INT |
	     TPM_INTF_CMD_READY_INT))
		wake_up_interruptible(&priv->int_queue);

	/* Clear interrupts handled with TPM_EOI */
	rc = tpm_tis_write32(priv, TPM_INT_STATUS(priv->locality), interrupt);
	if (rc < 0)
		return IRQ_NONE;

	tpm_tis_read32(priv, TPM_INT_STATUS(priv->locality), &interrupt);
	return IRQ_HANDLED;
}

static int tpm_tis_gen_interrupt(struct tpm_chip *chip)
{
	const char *desc = "attempting to generate an interrupt";
	u32 cap2;
	cap_t cap;

	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		return tpm2_get_tpm_pt(chip, 0x100, &cap2, desc);
	else
		return tpm_getcap(chip, TPM_CAP_PROP_TIS_TIMEOUT, &cap, desc,
				  0);
}

/* Register the IRQ and issue a command that will cause an interrupt. If an
 * irq is seen then leave the chip setup for IRQ operation, otherwise reverse
 * everything and leave in polling mode. Returns 0 on success.
 */
static int tpm_tis_probe_irq_single(struct tpm_chip *chip, u32 intmask,
				    int flags, int irq)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	u8 original_int_vec;
	int rc;
	u32 int_status;

	if (devm_request_irq(chip->dev.parent, irq, tis_int_handler, flags,
			     dev_name(&chip->dev), chip) != 0) {
		dev_info(&chip->dev, "Unable to request irq: %d for probe\n",
			 irq);
		return -1;
	}
	priv->irq = irq;

	rc = tpm_tis_read8(priv, TPM_INT_VECTOR(priv->locality),
			   &original_int_vec);
	if (rc < 0)
		return rc;

	rc = tpm_tis_write8(priv, TPM_INT_VECTOR(priv->locality), irq);
	if (rc < 0)
		return rc;

	rc = tpm_tis_read32(priv, TPM_INT_STATUS(priv->locality), &int_status);
	if (rc < 0)
		return rc;

	/* Clear all existing */
	rc = tpm_tis_write32(priv, TPM_INT_STATUS(priv->locality), int_status);
	if (rc < 0)
		return rc;

	/* Turn on */
	rc = tpm_tis_write32(priv, TPM_INT_ENABLE(priv->locality),
			     intmask | TPM_GLOBAL_INT_ENABLE);
	if (rc < 0)
		return rc;

	priv->irq_tested = false;

	/* Generate an interrupt by having the core call through to
	 * tpm_tis_send
	 */
	rc = tpm_tis_gen_interrupt(chip);
	if (rc < 0)
		return rc;

	/* tpm_tis_send will either confirm the interrupt is working or it
	 * will call disable_irq which undoes all of the above.
	 */
	if (!(chip->flags & TPM_CHIP_FLAG_IRQ)) {
		rc = tpm_tis_write8(priv, original_int_vec,
				TPM_INT_VECTOR(priv->locality));
		if (rc < 0)
			return rc;

		return 1;
	}

	return 0;
}

/* Try to find the IRQ the TPM is using. This is for legacy x86 systems that
 * do not have ACPI/etc. We typically expect the interrupt to be declared if
 * present.
 */
static void tpm_tis_probe_irq(struct tpm_chip *chip, u32 intmask)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	u8 original_int_vec;
	int i, rc;

	rc = tpm_tis_read8(priv, TPM_INT_VECTOR(priv->locality),
			   &original_int_vec);
	if (rc < 0)
		return;

	if (!original_int_vec) {
		if (IS_ENABLED(CONFIG_X86))
			for (i = 3; i <= 15; i++)
				if (!tpm_tis_probe_irq_single(chip, intmask, 0,
							      i))
					return;
	} else if (!tpm_tis_probe_irq_single(chip, intmask, 0,
					     original_int_vec))
		return;
}

void tpm_tis_remove(struct tpm_chip *chip)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	u32 reg = TPM_INT_ENABLE(priv->locality);
	u32 interrupt;
	int rc;

	rc = tpm_tis_read32(priv, reg, &interrupt);
	if (rc < 0)
		interrupt = 0;

	tpm_tis_write32(priv, reg, ~TPM_GLOBAL_INT_ENABLE & interrupt);
	release_locality(chip, priv->locality, 1);
}
EXPORT_SYMBOL_GPL(tpm_tis_remove);

static const struct tpm_class_ops tpm_tis = {
	.flags = TPM_OPS_AUTO_STARTUP,
	.status = tpm_tis_status,
	.recv = tpm_tis_recv,
	.send = tpm_tis_send,
	.cancel = tpm_tis_ready,
	.update_timeouts = tpm_tis_update_timeouts,
	.req_complete_mask = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_complete_val = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_canceled = tpm_tis_req_canceled,
};

int tpm_tis_core_init(struct device *dev, struct tpm_tis_data *priv, int irq,
		      const struct tpm_tis_phy_ops *phy_ops,
		      acpi_handle acpi_dev_handle)
{
	u32 vendor, intfcaps, intmask;
	u8 rid;
	int rc, probe;
	struct tpm_chip *chip;

	chip = tpmm_chip_alloc(dev, &tpm_tis);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

#ifdef CONFIG_ACPI
	chip->acpi_dev_handle = acpi_dev_handle;
#endif

	/* Maximum timeouts */
	chip->timeout_a = msecs_to_jiffies(TIS_TIMEOUT_A_MAX);
	chip->timeout_b = msecs_to_jiffies(TIS_TIMEOUT_B_MAX);
	chip->timeout_c = msecs_to_jiffies(TIS_TIMEOUT_C_MAX);
	chip->timeout_d = msecs_to_jiffies(TIS_TIMEOUT_D_MAX);
	priv->phy_ops = phy_ops;
	dev_set_drvdata(&chip->dev, priv);

	if (wait_startup(chip, 0) != 0) {
		rc = -ENODEV;
		goto out_err;
	}

	/* Take control of the TPM's interrupt hardware and shut it off */
	rc = tpm_tis_read32(priv, TPM_INT_ENABLE(priv->locality), &intmask);
	if (rc < 0)
		goto out_err;

	intmask |= TPM_INTF_CMD_READY_INT | TPM_INTF_LOCALITY_CHANGE_INT |
		   TPM_INTF_DATA_AVAIL_INT | TPM_INTF_STS_VALID_INT;
	intmask &= ~TPM_GLOBAL_INT_ENABLE;
	tpm_tis_write32(priv, TPM_INT_ENABLE(priv->locality), intmask);

	if (request_locality(chip, 0) != 0) {
		rc = -ENODEV;
		goto out_err;
	}

	rc = tpm2_probe(chip);
	if (rc)
		goto out_err;

	rc = tpm_tis_read32(priv, TPM_DID_VID(0), &vendor);
	if (rc < 0)
		goto out_err;

	priv->manufacturer_id = vendor;

	rc = tpm_tis_read8(priv, TPM_RID(0), &rid);
	if (rc < 0)
		goto out_err;

	dev_info(dev, "%s TPM (device-id 0x%X, rev-id %d)\n",
		 (chip->flags & TPM_CHIP_FLAG_TPM2) ? "2.0" : "1.2",
		 vendor >> 16, rid);

	probe = probe_itpm(chip);
	if (probe < 0) {
		rc = -ENODEV;
		goto out_err;
	}

	/* Figure out the capabilities */
	rc = tpm_tis_read32(priv, TPM_INTF_CAPS(priv->locality), &intfcaps);
	if (rc < 0)
		goto out_err;

	dev_dbg(dev, "TPM interface capabilities (0x%x):\n",
		intfcaps);
	if (intfcaps & TPM_INTF_BURST_COUNT_STATIC)
		dev_dbg(dev, "\tBurst Count Static\n");
	if (intfcaps & TPM_INTF_CMD_READY_INT)
		dev_dbg(dev, "\tCommand Ready Int Support\n");
	if (intfcaps & TPM_INTF_INT_EDGE_FALLING)
		dev_dbg(dev, "\tInterrupt Edge Falling\n");
	if (intfcaps & TPM_INTF_INT_EDGE_RISING)
		dev_dbg(dev, "\tInterrupt Edge Rising\n");
	if (intfcaps & TPM_INTF_INT_LEVEL_LOW)
		dev_dbg(dev, "\tInterrupt Level Low\n");
	if (intfcaps & TPM_INTF_INT_LEVEL_HIGH)
		dev_dbg(dev, "\tInterrupt Level High\n");
	if (intfcaps & TPM_INTF_LOCALITY_CHANGE_INT)
		dev_dbg(dev, "\tLocality Change Int Support\n");
	if (intfcaps & TPM_INTF_STS_VALID_INT)
		dev_dbg(dev, "\tSts Valid Int Support\n");
	if (intfcaps & TPM_INTF_DATA_AVAIL_INT)
		dev_dbg(dev, "\tData Avail Int Support\n");

	/* INTERRUPT Setup */
	init_waitqueue_head(&priv->read_queue);
	init_waitqueue_head(&priv->int_queue);
	if (irq != -1) {
		/* Before doing irq testing issue a command to the TPM in polling mode
		 * to make sure it works. May as well use that command to set the
		 * proper timeouts for the driver.
		 */
		if (tpm_get_timeouts(chip)) {
			dev_err(dev, "Could not get TPM timeouts and durations\n");
			rc = -ENODEV;
			goto out_err;
		}

		if (irq) {
			tpm_tis_probe_irq_single(chip, intmask, IRQF_SHARED,
						 irq);
			if (!(chip->flags & TPM_CHIP_FLAG_IRQ))
				dev_err(&chip->dev, FW_BUG
					"TPM interrupt not working, polling instead\n");
		} else {
			tpm_tis_probe_irq(chip, intmask);
		}
	}

	return tpm_chip_register(chip);
out_err:
	tpm_tis_remove(chip);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_tis_core_init);

#ifdef CONFIG_PM_SLEEP
static void tpm_tis_reenable_interrupts(struct tpm_chip *chip)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	u32 intmask;
	int rc;

	/* reenable interrupts that device may have lost or
	 * BIOS/firmware may have disabled
	 */
	rc = tpm_tis_write8(priv, TPM_INT_VECTOR(priv->locality), priv->irq);
	if (rc < 0)
		return;

	rc = tpm_tis_read32(priv, TPM_INT_ENABLE(priv->locality), &intmask);
	if (rc < 0)
		return;

	intmask |= TPM_INTF_CMD_READY_INT
	    | TPM_INTF_LOCALITY_CHANGE_INT | TPM_INTF_DATA_AVAIL_INT
	    | TPM_INTF_STS_VALID_INT | TPM_GLOBAL_INT_ENABLE;

	tpm_tis_write32(priv, TPM_INT_ENABLE(priv->locality), intmask);
}

int tpm_tis_resume(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	int ret;

	if (chip->flags & TPM_CHIP_FLAG_IRQ)
		tpm_tis_reenable_interrupts(chip);

	ret = tpm_pm_resume(dev);
	if (ret)
		return ret;

	/* TPM 1.2 requires self-test on resume. This function actually returns
	 * an error code but for unknown reason it isn't handled.
	 */
	if (!(chip->flags & TPM_CHIP_FLAG_TPM2))
		tpm_do_selftest(chip);

	return 0;
}
EXPORT_SYMBOL_GPL(tpm_tis_resume);
#endif

MODULE_AUTHOR("Leendert van Doorn (leendert@watson.ibm.com)");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
