/*
 * rtc-ds1305.c -- driver for DS1305 and DS1306 SPI RTC chips
 *
 * Copyright (C) 2008 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bcd.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/workqueue.h>

#include <linux/spi/spi.h>
#include <linux/spi/ds1305.h>
#include <linux/module.h>


/*
 * Registers ... mask DS1305_WRITE into register address to write,
 * otherwise you're reading it.  All non-bitmask values are BCD.
 */
#define DS1305_WRITE		0x80


/* RTC date/time ... the main special cases are that we:
 *  - Need fancy "hours" encoding in 12hour mode
 *  - Don't rely on the "day-of-week" field (or tm_wday)
 *  - Are a 21st-century clock (2000 <= year < 2100)
 */
#define DS1305_RTC_LEN		7		/* bytes for RTC regs */

#define DS1305_SEC		0x00		/* register addresses */
#define DS1305_MIN		0x01
#define DS1305_HOUR		0x02
#	define DS1305_HR_12		0x40	/* set == 12 hr mode */
#	define DS1305_HR_PM		0x20	/* set == PM (12hr mode) */
#define DS1305_WDAY		0x03
#define DS1305_MDAY		0x04
#define DS1305_MON		0x05
#define DS1305_YEAR		0x06


/* The two alarms have only sec/min/hour/wday fields (ALM_LEN).
 * DS1305_ALM_DISABLE disables a match field (some combos are bad).
 *
 * NOTE that since we don't use WDAY, we limit ourselves to alarms
 * only one day into the future (vs potentially up to a week).
 *
 * NOTE ALSO that while we could generate once-a-second IRQs (UIE), we
 * don't currently support them.  We'd either need to do it only when
 * no alarm is pending (not the standard model), or to use the second
 * alarm (implying that this is a DS1305 not DS1306, *and* that either
 * it's wired up a second IRQ we know, or that INTCN is set)
 */
#define DS1305_ALM_LEN		4		/* bytes for ALM regs */
#define DS1305_ALM_DISABLE	0x80

#define DS1305_ALM0(r)		(0x07 + (r))	/* register addresses */
#define DS1305_ALM1(r)		(0x0b + (r))


/* three control registers */
#define DS1305_CONTROL_LEN	3		/* bytes of control regs */

#define DS1305_CONTROL		0x0f		/* register addresses */
#	define DS1305_nEOSC		0x80	/* low enables oscillator */
#	define DS1305_WP		0x40	/* write protect */
#	define DS1305_INTCN		0x04	/* clear == only int0 used */
#	define DS1306_1HZ		0x04	/* enable 1Hz output */
#	define DS1305_AEI1		0x02	/* enable ALM1 IRQ */
#	define DS1305_AEI0		0x01	/* enable ALM0 IRQ */
#define DS1305_STATUS		0x10
/* status has just AEIx bits, mirrored as IRQFx */
#define DS1305_TRICKLE		0x11
/* trickle bits are defined in <linux/spi/ds1305.h> */

/* a bunch of NVRAM */
#define DS1305_NVRAM_LEN	96		/* bytes of NVRAM */

#define DS1305_NVRAM		0x20		/* register addresses */


struct ds1305 {
	struct spi_device	*spi;
	struct rtc_device	*rtc;

	struct work_struct	work;

	unsigned long		flags;
#define FLAG_EXITING	0

	bool			hr12;
	u8			ctrl[DS1305_CONTROL_LEN];
};


/*----------------------------------------------------------------------*/

/*
 * Utilities ...  tolerate 12-hour AM/PM notation in case of non-Linux
 * software (like a bootloader) which may require it.
 */

static unsigned bcd2hour(u8 bcd)
{
	if (bcd & DS1305_HR_12) {
		unsigned	hour = 0;

		bcd &= ~DS1305_HR_12;
		if (bcd & DS1305_HR_PM) {
			hour = 12;
			bcd &= ~DS1305_HR_PM;
		}
		hour += bcd2bin(bcd);
		return hour - 1;
	}
	return bcd2bin(bcd);
}

static u8 hour2bcd(bool hr12, int hour)
{
	if (hr12) {
		hour++;
		if (hour <= 12)
			return DS1305_HR_12 | bin2bcd(hour);
		hour -= 12;
		return DS1305_HR_12 | DS1305_HR_PM | bin2bcd(hour);
	}
	return bin2bcd(hour);
}

/*----------------------------------------------------------------------*/

/*
 * Interface to RTC framework
 */

static int ds1305_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct ds1305	*ds1305 = dev_get_drvdata(dev);
	u8		buf[2];
	long		err = -EINVAL;

	buf[0] = DS1305_WRITE | DS1305_CONTROL;
	buf[1] = ds1305->ctrl[0];

	if (enabled) {
		if (ds1305->ctrl[0] & DS1305_AEI0)
			goto done;
		buf[1] |= DS1305_AEI0;
	} else {
		if (!(buf[1] & DS1305_AEI0))
			goto done;
		buf[1] &= ~DS1305_AEI0;
	}
	err = spi_write_then_read(ds1305->spi, buf, sizeof(buf), NULL, 0);
	if (err >= 0)
		ds1305->ctrl[0] = buf[1];
done:
	return err;

}


/*
 * Get/set of date and time is pretty normal.
 */

static int ds1305_get_time(struct device *dev, struct rtc_time *time)
{
	struct ds1305	*ds1305 = dev_get_drvdata(dev);
	u8		addr = DS1305_SEC;
	u8		buf[DS1305_RTC_LEN];
	int		status;

	/* Use write-then-read to get all the date/time registers
	 * since dma from stack is nonportable
	 */
	status = spi_write_then_read(ds1305->spi, &addr, sizeof(addr),
			buf, sizeof(buf));
	if (status < 0)
		return status;

	dev_vdbg(dev, "%s: %3ph, %4ph\n", "read", &buf[0], &buf[3]);

	/* Decode the registers */
	time->tm_sec = bcd2bin(buf[DS1305_SEC]);
	time->tm_min = bcd2bin(buf[DS1305_MIN]);
	time->tm_hour = bcd2hour(buf[DS1305_HOUR]);
	time->tm_wday = buf[DS1305_WDAY] - 1;
	time->tm_mday = bcd2bin(buf[DS1305_MDAY]);
	time->tm_mon = bcd2bin(buf[DS1305_MON]) - 1;
	time->tm_year = bcd2bin(buf[DS1305_YEAR]) + 100;

	dev_vdbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"read", time->tm_sec, time->tm_min,
		time->tm_hour, time->tm_mday,
		time->tm_mon, time->tm_year, time->tm_wday);

	return 0;
}

static int ds1305_set_time(struct device *dev, struct rtc_time *time)
{
	struct ds1305	*ds1305 = dev_get_drvdata(dev);
	u8		buf[1 + DS1305_RTC_LEN];
	u8		*bp = buf;

	dev_vdbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"write", time->tm_sec, time->tm_min,
		time->tm_hour, time->tm_mday,
		time->tm_mon, time->tm_year, time->tm_wday);

	/* Write registers starting at the first time/date address. */
	*bp++ = DS1305_WRITE | DS1305_SEC;

	*bp++ = bin2bcd(time->tm_sec);
	*bp++ = bin2bcd(time->tm_min);
	*bp++ = hour2bcd(ds1305->hr12, time->tm_hour);
	*bp++ = (time->tm_wday < 7) ? (time->tm_wday + 1) : 1;
	*bp++ = bin2bcd(time->tm_mday);
	*bp++ = bin2bcd(time->tm_mon + 1);
	*bp++ = bin2bcd(time->tm_year - 100);

	dev_dbg(dev, "%s: %3ph, %4ph\n", "write", &buf[1], &buf[4]);

	/* use write-then-read since dma from stack is nonportable */
	return spi_write_then_read(ds1305->spi, buf, sizeof(buf),
			NULL, 0);
}

/*
 * Get/set of alarm is a bit funky:
 *
 * - First there's the inherent raciness of getting the (partitioned)
 *   status of an alarm that could trigger while we're reading parts
 *   of that status.
 *
 * - Second there's its limited range (we could increase it a bit by
 *   relying on WDAY), which means it will easily roll over.
 *
 * - Third there's the choice of two alarms and alarm signals.
 *   Here we use ALM0 and expect that nINT0 (open drain) is used;
 *   that's the only real option for DS1306 runtime alarms, and is
 *   natural on DS1305.
 *
 * - Fourth, there's also ALM1, and a second interrupt signal:
 *     + On DS1305 ALM1 uses nINT1 (when INTCN=1) else nINT0;
 *     + On DS1306 ALM1 only uses INT1 (an active high pulse)
 *       and it won't work when VCC1 is active.
 *
 *   So to be most general, we should probably set both alarms to the
 *   same value, letting ALM1 be the wakeup event source on DS1306
 *   and handling several wiring options on DS1305.
 *
 * - Fifth, we support the polled mode (as well as possible; why not?)
 *   even when no interrupt line is wired to an IRQ.
 */

/*
 * Context: caller holds rtc->ops_lock (to protect ds1305->ctrl)
 */
static int ds1305_get_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct ds1305	*ds1305 = dev_get_drvdata(dev);
	struct spi_device *spi = ds1305->spi;
	u8		addr;
	int		status;
	u8		buf[DS1305_ALM_LEN];

	/* Refresh control register cache BEFORE reading ALM0 registers,
	 * since reading alarm registers acks any pending IRQ.  That
	 * makes returning "pending" status a bit of a lie, but that bit
	 * of EFI status is at best fragile anyway (given IRQ handlers).
	 */
	addr = DS1305_CONTROL;
	status = spi_write_then_read(spi, &addr, sizeof(addr),
			ds1305->ctrl, sizeof(ds1305->ctrl));
	if (status < 0)
		return status;

	alm->enabled = !!(ds1305->ctrl[0] & DS1305_AEI0);
	alm->pending = !!(ds1305->ctrl[1] & DS1305_AEI0);

	/* get and check ALM0 registers */
	addr = DS1305_ALM0(DS1305_SEC);
	status = spi_write_then_read(spi, &addr, sizeof(addr),
			buf, sizeof(buf));
	if (status < 0)
		return status;

	dev_vdbg(dev, "%s: %02x %02x %02x %02x\n",
		"alm0 read", buf[DS1305_SEC], buf[DS1305_MIN],
		buf[DS1305_HOUR], buf[DS1305_WDAY]);

	if ((DS1305_ALM_DISABLE & buf[DS1305_SEC])
			|| (DS1305_ALM_DISABLE & buf[DS1305_MIN])
			|| (DS1305_ALM_DISABLE & buf[DS1305_HOUR]))
		return -EIO;

	/* Stuff these values into alm->time and let RTC framework code
	 * fill in the rest ... and also handle rollover to tomorrow when
	 * that's needed.
	 */
	alm->time.tm_sec = bcd2bin(buf[DS1305_SEC]);
	alm->time.tm_min = bcd2bin(buf[DS1305_MIN]);
	alm->time.tm_hour = bcd2hour(buf[DS1305_HOUR]);

	return 0;
}

/*
 * Context: caller holds rtc->ops_lock (to protect ds1305->ctrl)
 */
static int ds1305_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct ds1305	*ds1305 = dev_get_drvdata(dev);
	struct spi_device *spi = ds1305->spi;
	unsigned long	now, later;
	struct rtc_time	tm;
	int		status;
	u8		buf[1 + DS1305_ALM_LEN];

	/* convert desired alarm to time_t */
	status = rtc_tm_to_time(&alm->time, &later);
	if (status < 0)
		return status;

	/* Read current time as time_t */
	status = ds1305_get_time(dev, &tm);
	if (status < 0)
		return status;
	status = rtc_tm_to_time(&tm, &now);
	if (status < 0)
		return status;

	/* make sure alarm fires within the next 24 hours */
	if (later <= now)
		return -EINVAL;
	if ((later - now) > 24 * 60 * 60)
		return -EDOM;

	/* disable alarm if needed */
	if (ds1305->ctrl[0] & DS1305_AEI0) {
		ds1305->ctrl[0] &= ~DS1305_AEI0;

		buf[0] = DS1305_WRITE | DS1305_CONTROL;
		buf[1] = ds1305->ctrl[0];
		status = spi_write_then_read(ds1305->spi, buf, 2, NULL, 0);
		if (status < 0)
			return status;
	}

	/* write alarm */
	buf[0] = DS1305_WRITE | DS1305_ALM0(DS1305_SEC);
	buf[1 + DS1305_SEC] = bin2bcd(alm->time.tm_sec);
	buf[1 + DS1305_MIN] = bin2bcd(alm->time.tm_min);
	buf[1 + DS1305_HOUR] = hour2bcd(ds1305->hr12, alm->time.tm_hour);
	buf[1 + DS1305_WDAY] = DS1305_ALM_DISABLE;

	dev_dbg(dev, "%s: %02x %02x %02x %02x\n",
		"alm0 write", buf[1 + DS1305_SEC], buf[1 + DS1305_MIN],
		buf[1 + DS1305_HOUR], buf[1 + DS1305_WDAY]);

	status = spi_write_then_read(spi, buf, sizeof(buf), NULL, 0);
	if (status < 0)
		return status;

	/* enable alarm if requested */
	if (alm->enabled) {
		ds1305->ctrl[0] |= DS1305_AEI0;

		buf[0] = DS1305_WRITE | DS1305_CONTROL;
		buf[1] = ds1305->ctrl[0];
		status = spi_write_then_read(ds1305->spi, buf, 2, NULL, 0);
	}

	return status;
}

#ifdef CONFIG_PROC_FS

static int ds1305_proc(struct device *dev, struct seq_file *seq)
{
	struct ds1305	*ds1305 = dev_get_drvdata(dev);
	char		*diodes = "no";
	char		*resistors = "";

	/* ctrl[2] is treated as read-only; no locking needed */
	if ((ds1305->ctrl[2] & 0xf0) == DS1305_TRICKLE_MAGIC) {
		switch (ds1305->ctrl[2] & 0x0c) {
		case DS1305_TRICKLE_DS2:
			diodes = "2 diodes, ";
			break;
		case DS1305_TRICKLE_DS1:
			diodes = "1 diode, ";
			break;
		default:
			goto done;
		}
		switch (ds1305->ctrl[2] & 0x03) {
		case DS1305_TRICKLE_2K:
			resistors = "2k Ohm";
			break;
		case DS1305_TRICKLE_4K:
			resistors = "4k Ohm";
			break;
		case DS1305_TRICKLE_8K:
			resistors = "8k Ohm";
			break;
		default:
			diodes = "no";
			break;
		}
	}

done:
	seq_printf(seq, "trickle_charge\t: %s%s\n", diodes, resistors);

	return 0;
}

#else
#define ds1305_proc	NULL
#endif

static const struct rtc_class_ops ds1305_ops = {
	.read_time	= ds1305_get_time,
	.set_time	= ds1305_set_time,
	.read_alarm	= ds1305_get_alarm,
	.set_alarm	= ds1305_set_alarm,
	.proc		= ds1305_proc,
	.alarm_irq_enable = ds1305_alarm_irq_enable,
};

static void ds1305_work(struct work_struct *work)
{
	struct ds1305	*ds1305 = container_of(work, struct ds1305, work);
	struct mutex	*lock = &ds1305->rtc->ops_lock;
	struct spi_device *spi = ds1305->spi;
	u8		buf[3];
	int		status;

	/* lock to protect ds1305->ctrl */
	mutex_lock(lock);

	/* Disable the IRQ, and clear its status ... for now, we "know"
	 * that if more than one alarm is active, they're in sync.
	 * Note that reading ALM data registers also clears IRQ status.
	 */
	ds1305->ctrl[0] &= ~(DS1305_AEI1 | DS1305_AEI0);
	ds1305->ctrl[1] = 0;

	buf[0] = DS1305_WRITE | DS1305_CONTROL;
	buf[1] = ds1305->ctrl[0];
	buf[2] = 0;

	status = spi_write_then_read(spi, buf, sizeof(buf),
			NULL, 0);
	if (status < 0)
		dev_dbg(&spi->dev, "clear irq --> %d\n", status);

	mutex_unlock(lock);

	if (!test_bit(FLAG_EXITING, &ds1305->flags))
		enable_irq(spi->irq);

	rtc_update_irq(ds1305->rtc, 1, RTC_AF | RTC_IRQF);
}

/*
 * This "real" IRQ handler hands off to a workqueue mostly to allow
 * mutex locking for ds1305->ctrl ... unlike I2C, we could issue async
 * I/O requests in IRQ context (to clear the IRQ status).
 */
static irqreturn_t ds1305_irq(int irq, void *p)
{
	struct ds1305		*ds1305 = p;

	disable_irq(irq);
	schedule_work(&ds1305->work);
	return IRQ_HANDLED;
}

/*----------------------------------------------------------------------*/

/*
 * Interface for NVRAM
 */

static void msg_init(struct spi_message *m, struct spi_transfer *x,
		u8 *addr, size_t count, char *tx, char *rx)
{
	spi_message_init(m);
	memset(x, 0, 2 * sizeof(*x));

	x->tx_buf = addr;
	x->len = 1;
	spi_message_add_tail(x, m);

	x++;

	x->tx_buf = tx;
	x->rx_buf = rx;
	x->len = count;
	spi_message_add_tail(x, m);
}

static int ds1305_nvram_read(void *priv, unsigned int off, void *buf,
			     size_t count)
{
	struct ds1305		*ds1305 = priv;
	struct spi_device	*spi = ds1305->spi;
	u8			addr;
	struct spi_message	m;
	struct spi_transfer	x[2];

	addr = DS1305_NVRAM + off;
	msg_init(&m, x, &addr, count, NULL, buf);

	return spi_sync(spi, &m);
}

static int ds1305_nvram_write(void *priv, unsigned int off, void *buf,
			      size_t count)
{
	struct ds1305		*ds1305 = priv;
	struct spi_device	*spi = ds1305->spi;
	u8			addr;
	struct spi_message	m;
	struct spi_transfer	x[2];

	addr = (DS1305_WRITE | DS1305_NVRAM) + off;
	msg_init(&m, x, &addr, count, buf, NULL);

	return spi_sync(spi, &m);
}

/*----------------------------------------------------------------------*/

/*
 * Interface to SPI stack
 */

static int ds1305_probe(struct spi_device *spi)
{
	struct ds1305			*ds1305;
	int				status;
	u8				addr, value;
	struct ds1305_platform_data	*pdata = dev_get_platdata(&spi->dev);
	bool				write_ctrl = false;
	struct nvmem_config ds1305_nvmem_cfg = {
		.name = "ds1305_nvram",
		.word_size = 1,
		.stride = 1,
		.size = DS1305_NVRAM_LEN,
		.reg_read = ds1305_nvram_read,
		.reg_write = ds1305_nvram_write,
	};

	/* Sanity check board setup data.  This may be hooked up
	 * in 3wire mode, but we don't care.  Note that unless
	 * there's an inverter in place, this needs SPI_CS_HIGH!
	 */
	if ((spi->bits_per_word && spi->bits_per_word != 8)
			|| (spi->max_speed_hz > 2000000)
			|| !(spi->mode & SPI_CPHA))
		return -EINVAL;

	/* set up driver data */
	ds1305 = devm_kzalloc(&spi->dev, sizeof(*ds1305), GFP_KERNEL);
	if (!ds1305)
		return -ENOMEM;
	ds1305->spi = spi;
	spi_set_drvdata(spi, ds1305);

	/* read and cache control registers */
	addr = DS1305_CONTROL;
	status = spi_write_then_read(spi, &addr, sizeof(addr),
			ds1305->ctrl, sizeof(ds1305->ctrl));
	if (status < 0) {
		dev_dbg(&spi->dev, "can't %s, %d\n",
				"read", status);
		return status;
	}

	dev_dbg(&spi->dev, "ctrl %s: %3ph\n", "read", ds1305->ctrl);

	/* Sanity check register values ... partially compensating for the
	 * fact that SPI has no device handshake.  A pullup on MISO would
	 * make these tests fail; but not all systems will have one.  If
	 * some register is neither 0x00 nor 0xff, a chip is likely there.
	 */
	if ((ds1305->ctrl[0] & 0x38) != 0 || (ds1305->ctrl[1] & 0xfc) != 0) {
		dev_dbg(&spi->dev, "RTC chip is not present\n");
		return -ENODEV;
	}
	if (ds1305->ctrl[2] == 0)
		dev_dbg(&spi->dev, "chip may not be present\n");

	/* enable writes if needed ... if we were paranoid it would
	 * make sense to enable them only when absolutely necessary.
	 */
	if (ds1305->ctrl[0] & DS1305_WP) {
		u8		buf[2];

		ds1305->ctrl[0] &= ~DS1305_WP;

		buf[0] = DS1305_WRITE | DS1305_CONTROL;
		buf[1] = ds1305->ctrl[0];
		status = spi_write_then_read(spi, buf, sizeof(buf), NULL, 0);

		dev_dbg(&spi->dev, "clear WP --> %d\n", status);
		if (status < 0)
			return status;
	}

	/* on DS1305, maybe start oscillator; like most low power
	 * oscillators, it may take a second to stabilize
	 */
	if (ds1305->ctrl[0] & DS1305_nEOSC) {
		ds1305->ctrl[0] &= ~DS1305_nEOSC;
		write_ctrl = true;
		dev_warn(&spi->dev, "SET TIME!\n");
	}

	/* ack any pending IRQs */
	if (ds1305->ctrl[1]) {
		ds1305->ctrl[1] = 0;
		write_ctrl = true;
	}

	/* this may need one-time (re)init */
	if (pdata) {
		/* maybe enable trickle charge */
		if (((ds1305->ctrl[2] & 0xf0) != DS1305_TRICKLE_MAGIC)) {
			ds1305->ctrl[2] = DS1305_TRICKLE_MAGIC
						| pdata->trickle;
			write_ctrl = true;
		}

		/* on DS1306, configure 1 Hz signal */
		if (pdata->is_ds1306) {
			if (pdata->en_1hz) {
				if (!(ds1305->ctrl[0] & DS1306_1HZ)) {
					ds1305->ctrl[0] |= DS1306_1HZ;
					write_ctrl = true;
				}
			} else {
				if (ds1305->ctrl[0] & DS1306_1HZ) {
					ds1305->ctrl[0] &= ~DS1306_1HZ;
					write_ctrl = true;
				}
			}
		}
	}

	if (write_ctrl) {
		u8		buf[4];

		buf[0] = DS1305_WRITE | DS1305_CONTROL;
		buf[1] = ds1305->ctrl[0];
		buf[2] = ds1305->ctrl[1];
		buf[3] = ds1305->ctrl[2];
		status = spi_write_then_read(spi, buf, sizeof(buf), NULL, 0);
		if (status < 0) {
			dev_dbg(&spi->dev, "can't %s, %d\n",
					"write", status);
			return status;
		}

		dev_dbg(&spi->dev, "ctrl %s: %3ph\n", "write", ds1305->ctrl);
	}

	/* see if non-Linux software set up AM/PM mode */
	addr = DS1305_HOUR;
	status = spi_write_then_read(spi, &addr, sizeof(addr),
				&value, sizeof(value));
	if (status < 0) {
		dev_dbg(&spi->dev, "read HOUR --> %d\n", status);
		return status;
	}

	ds1305->hr12 = (DS1305_HR_12 & value) != 0;
	if (ds1305->hr12)
		dev_dbg(&spi->dev, "AM/PM\n");

	/* register RTC ... from here on, ds1305->ctrl needs locking */
	ds1305->rtc = devm_rtc_allocate_device(&spi->dev);
	if (IS_ERR(ds1305->rtc)) {
		return PTR_ERR(ds1305->rtc);
	}

	ds1305->rtc->ops = &ds1305_ops;

	ds1305_nvmem_cfg.priv = ds1305;
	ds1305->rtc->nvram_old_abi = true;
	status = rtc_register_device(ds1305->rtc);
	if (status) {
		dev_dbg(&spi->dev, "register rtc --> %d\n", status);
		return status;
	}

	rtc_nvmem_register(ds1305->rtc, &ds1305_nvmem_cfg);

	/* Maybe set up alarm IRQ; be ready to handle it triggering right
	 * away.  NOTE that we don't share this.  The signal is active low,
	 * and we can't ack it before a SPI message delay.  We temporarily
	 * disable the IRQ until it's acked, which lets us work with more
	 * IRQ trigger modes (not all IRQ controllers can do falling edge).
	 */
	if (spi->irq) {
		INIT_WORK(&ds1305->work, ds1305_work);
		status = devm_request_irq(&spi->dev, spi->irq, ds1305_irq,
				0, dev_name(&ds1305->rtc->dev), ds1305);
		if (status < 0) {
			dev_err(&spi->dev, "request_irq %d --> %d\n",
					spi->irq, status);
		} else {
			device_set_wakeup_capable(&spi->dev, 1);
		}
	}

	return 0;
}

static int ds1305_remove(struct spi_device *spi)
{
	struct ds1305 *ds1305 = spi_get_drvdata(spi);

	/* carefully shut down irq and workqueue, if present */
	if (spi->irq) {
		set_bit(FLAG_EXITING, &ds1305->flags);
		devm_free_irq(&spi->dev, spi->irq, ds1305);
		cancel_work_sync(&ds1305->work);
	}

	return 0;
}

static struct spi_driver ds1305_driver = {
	.driver.name	= "rtc-ds1305",
	.probe		= ds1305_probe,
	.remove		= ds1305_remove,
	/* REVISIT add suspend/resume */
};

module_spi_driver(ds1305_driver);

MODULE_DESCRIPTION("RTC driver for DS1305 and DS1306 chips");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:rtc-ds1305");
