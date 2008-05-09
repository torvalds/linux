/*
 * Blackfin On-Chip Real Time Clock Driver
 *  Supports BF52[257]/BF53[123]/BF53[467]/BF54[24789]
 *
 * Copyright 2004-2007 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

/* The biggest issue we deal with in this driver is that register writes are
 * synced to the RTC frequency of 1Hz.  So if you write to a register and
 * attempt to write again before the first write has completed, the new write
 * is simply discarded.  This can easily be troublesome if userspace disables
 * one event (say periodic) and then right after enables an event (say alarm).
 * Since all events are maintained in the same interrupt mask register, if
 * we wrote to it to disable the first event and then wrote to it again to
 * enable the second event, that second event would not be enabled as the
 * write would be discarded and things quickly fall apart.
 *
 * To keep this delay from significantly degrading performance (we, in theory,
 * would have to sleep for up to 1 second everytime we wanted to write a
 * register), we only check the write pending status before we start to issue
 * a new write.  We bank on the idea that it doesnt matter when the sync
 * happens so long as we don't attempt another write before it does.  The only
 * time userspace would take this penalty is when they try and do multiple
 * operations right after another ... but in this case, they need to take the
 * sync penalty, so we should be OK.
 *
 * Also note that the RTC_ISTAT register does not suffer this penalty; its
 * writes to clear status registers complete immediately.
 */

#include <linux/bcd.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/seq_file.h>

#include <asm/blackfin.h>

#define dev_dbg_stamp(dev) dev_dbg(dev, "%s:%i: here i am\n", __func__, __LINE__)

struct bfin_rtc {
	struct rtc_device *rtc_dev;
	struct rtc_time rtc_alarm;
	u16 rtc_wrote_regs;
};

/* Bit values for the ISTAT / ICTL registers */
#define RTC_ISTAT_WRITE_COMPLETE  0x8000
#define RTC_ISTAT_WRITE_PENDING   0x4000
#define RTC_ISTAT_ALARM_DAY       0x0040
#define RTC_ISTAT_24HR            0x0020
#define RTC_ISTAT_HOUR            0x0010
#define RTC_ISTAT_MIN             0x0008
#define RTC_ISTAT_SEC             0x0004
#define RTC_ISTAT_ALARM           0x0002
#define RTC_ISTAT_STOPWATCH       0x0001

/* Shift values for RTC_STAT register */
#define DAY_BITS_OFF    17
#define HOUR_BITS_OFF   12
#define MIN_BITS_OFF    6
#define SEC_BITS_OFF    0

/* Some helper functions to convert between the common RTC notion of time
 * and the internal Blackfin notion that is encoded in 32bits.
 */
static inline u32 rtc_time_to_bfin(unsigned long now)
{
	u32 sec  = (now % 60);
	u32 min  = (now % (60 * 60)) / 60;
	u32 hour = (now % (60 * 60 * 24)) / (60 * 60);
	u32 days = (now / (60 * 60 * 24));
	return (sec  << SEC_BITS_OFF) +
	       (min  << MIN_BITS_OFF) +
	       (hour << HOUR_BITS_OFF) +
	       (days << DAY_BITS_OFF);
}
static inline unsigned long rtc_bfin_to_time(u32 rtc_bfin)
{
	return (((rtc_bfin >> SEC_BITS_OFF)  & 0x003F)) +
	       (((rtc_bfin >> MIN_BITS_OFF)  & 0x003F) * 60) +
	       (((rtc_bfin >> HOUR_BITS_OFF) & 0x001F) * 60 * 60) +
	       (((rtc_bfin >> DAY_BITS_OFF)  & 0x7FFF) * 60 * 60 * 24);
}
static inline void rtc_bfin_to_tm(u32 rtc_bfin, struct rtc_time *tm)
{
	rtc_time_to_tm(rtc_bfin_to_time(rtc_bfin), tm);
}

/**
 *	bfin_rtc_sync_pending - make sure pending writes have complete
 *
 * Wait for the previous write to a RTC register to complete.
 * Unfortunately, we can't sleep here as that introduces a race condition when
 * turning on interrupt events.  Consider this:
 *  - process sets alarm
 *  - process enables alarm
 *  - process sleeps while waiting for rtc write to sync
 *  - interrupt fires while process is sleeping
 *  - interrupt acks the event by writing to ISTAT
 *  - interrupt sets the WRITE PENDING bit
 *  - interrupt handler finishes
 *  - process wakes up, sees WRITE PENDING bit set, goes to sleep
 *  - interrupt fires while process is sleeping
 * If anyone can point out the obvious solution here, i'm listening :).  This
 * shouldn't be an issue on an SMP or preempt system as this function should
 * only be called with the rtc lock held.
 *
 * Other options:
 *  - disable PREN so the sync happens at 32.768kHZ ... but this changes the
 *    inc rate for all RTC registers from 1HZ to 32.768kHZ ...
 *  - use the write complete IRQ
 */
/*
static void bfin_rtc_sync_pending_polled(void)
{
	while (!(bfin_read_RTC_ISTAT() & RTC_ISTAT_WRITE_COMPLETE))
		if (!(bfin_read_RTC_ISTAT() & RTC_ISTAT_WRITE_PENDING))
			break;
	bfin_write_RTC_ISTAT(RTC_ISTAT_WRITE_COMPLETE);
}
*/
static DECLARE_COMPLETION(bfin_write_complete);
static void bfin_rtc_sync_pending(struct device *dev)
{
	dev_dbg_stamp(dev);
	while (bfin_read_RTC_ISTAT() & RTC_ISTAT_WRITE_PENDING)
		wait_for_completion_timeout(&bfin_write_complete, HZ * 5);
	dev_dbg_stamp(dev);
}

/**
 *	bfin_rtc_reset - set RTC to sane/known state
 *
 * Initialize the RTC.  Enable pre-scaler to scale RTC clock
 * to 1Hz and clear interrupt/status registers.
 */
static void bfin_rtc_reset(struct device *dev)
{
	struct bfin_rtc *rtc = dev_get_drvdata(dev);
	dev_dbg_stamp(dev);
	bfin_rtc_sync_pending(dev);
	bfin_write_RTC_PREN(0x1);
	bfin_write_RTC_ICTL(RTC_ISTAT_WRITE_COMPLETE);
	bfin_write_RTC_SWCNT(0);
	bfin_write_RTC_ALARM(0);
	bfin_write_RTC_ISTAT(0xFFFF);
	rtc->rtc_wrote_regs = 0;
}

/**
 *	bfin_rtc_interrupt - handle interrupt from RTC
 *
 * Since we handle all RTC events here, we have to make sure the requested
 * interrupt is enabled (in RTC_ICTL) as the event status register (RTC_ISTAT)
 * always gets updated regardless of the interrupt being enabled.  So when one
 * even we care about (e.g. stopwatch) goes off, we don't want to turn around
 * and say that other events have happened as well (e.g. second).  We do not
 * have to worry about pending writes to the RTC_ICTL register as interrupts
 * only fire if they are enabled in the RTC_ICTL register.
 */
static irqreturn_t bfin_rtc_interrupt(int irq, void *dev_id)
{
	struct device *dev = dev_id;
	struct bfin_rtc *rtc = dev_get_drvdata(dev);
	unsigned long events = 0;
	bool write_complete = false;
	u16 rtc_istat, rtc_ictl;

	dev_dbg_stamp(dev);

	rtc_istat = bfin_read_RTC_ISTAT();
	rtc_ictl = bfin_read_RTC_ICTL();

	if (rtc_istat & RTC_ISTAT_WRITE_COMPLETE) {
		bfin_write_RTC_ISTAT(RTC_ISTAT_WRITE_COMPLETE);
		write_complete = true;
		complete(&bfin_write_complete);
	}

	if (rtc_ictl & (RTC_ISTAT_ALARM | RTC_ISTAT_ALARM_DAY)) {
		if (rtc_istat & (RTC_ISTAT_ALARM | RTC_ISTAT_ALARM_DAY)) {
			bfin_write_RTC_ISTAT(RTC_ISTAT_ALARM | RTC_ISTAT_ALARM_DAY);
			events |= RTC_AF | RTC_IRQF;
		}
	}

	if (rtc_ictl & RTC_ISTAT_STOPWATCH) {
		if (rtc_istat & RTC_ISTAT_STOPWATCH) {
			bfin_write_RTC_ISTAT(RTC_ISTAT_STOPWATCH);
			events |= RTC_PF | RTC_IRQF;
			bfin_write_RTC_SWCNT(rtc->rtc_dev->irq_freq);
		}
	}

	if (rtc_ictl & RTC_ISTAT_SEC) {
		if (rtc_istat & RTC_ISTAT_SEC) {
			bfin_write_RTC_ISTAT(RTC_ISTAT_SEC);
			events |= RTC_UF | RTC_IRQF;
		}
	}

	if (events)
		rtc_update_irq(rtc->rtc_dev, 1, events);

	if (write_complete || events)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static int bfin_rtc_open(struct device *dev)
{
	int ret;

	dev_dbg_stamp(dev);

	ret = request_irq(IRQ_RTC, bfin_rtc_interrupt, IRQF_SHARED, to_platform_device(dev)->name, dev);
	if (!ret)
		bfin_rtc_reset(dev);

	return ret;
}

static void bfin_rtc_release(struct device *dev)
{
	dev_dbg_stamp(dev);
	bfin_rtc_reset(dev);
	free_irq(IRQ_RTC, dev);
}

static void bfin_rtc_int_set(struct bfin_rtc *rtc, u16 rtc_int)
{
	bfin_write_RTC_ISTAT(rtc_int);
	bfin_write_RTC_ICTL(bfin_read_RTC_ICTL() | rtc_int);
}
static void bfin_rtc_int_clear(struct bfin_rtc *rtc, u16 rtc_int)
{
	bfin_write_RTC_ICTL(bfin_read_RTC_ICTL() & rtc_int);
}
static void bfin_rtc_int_set_alarm(struct bfin_rtc *rtc)
{
	/* Blackfin has different bits for whether the alarm is
	 * more than 24 hours away.
	 */
	bfin_rtc_int_set(rtc, (rtc->rtc_alarm.tm_yday == -1 ? RTC_ISTAT_ALARM : RTC_ISTAT_ALARM_DAY));
}
static int bfin_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct bfin_rtc *rtc = dev_get_drvdata(dev);
	int ret = 0;

	dev_dbg_stamp(dev);

	bfin_rtc_sync_pending(dev);

	switch (cmd) {
	case RTC_PIE_ON:
		dev_dbg_stamp(dev);
		bfin_rtc_int_set(rtc, RTC_ISTAT_STOPWATCH);
		bfin_write_RTC_SWCNT(rtc->rtc_dev->irq_freq);
		break;
	case RTC_PIE_OFF:
		dev_dbg_stamp(dev);
		bfin_rtc_int_clear(rtc, ~RTC_ISTAT_STOPWATCH);
		break;

	case RTC_UIE_ON:
		dev_dbg_stamp(dev);
		bfin_rtc_int_set(rtc, RTC_ISTAT_SEC);
		break;
	case RTC_UIE_OFF:
		dev_dbg_stamp(dev);
		bfin_rtc_int_clear(rtc, ~RTC_ISTAT_SEC);
		break;

	case RTC_AIE_ON:
		dev_dbg_stamp(dev);
		bfin_rtc_int_set_alarm(rtc);
		break;
	case RTC_AIE_OFF:
		dev_dbg_stamp(dev);
		bfin_rtc_int_clear(rtc, ~(RTC_ISTAT_ALARM | RTC_ISTAT_ALARM_DAY));
		break;

	default:
		dev_dbg_stamp(dev);
		ret = -ENOIOCTLCMD;
	}

	return ret;
}

static int bfin_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct bfin_rtc *rtc = dev_get_drvdata(dev);

	dev_dbg_stamp(dev);

	if (rtc->rtc_wrote_regs & 0x1)
		bfin_rtc_sync_pending(dev);

	rtc_bfin_to_tm(bfin_read_RTC_STAT(), tm);

	return 0;
}

static int bfin_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct bfin_rtc *rtc = dev_get_drvdata(dev);
	int ret;
	unsigned long now;

	dev_dbg_stamp(dev);

	ret = rtc_tm_to_time(tm, &now);
	if (ret == 0) {
		if (rtc->rtc_wrote_regs & 0x1)
			bfin_rtc_sync_pending(dev);
		bfin_write_RTC_STAT(rtc_time_to_bfin(now));
		rtc->rtc_wrote_regs = 0x1;
	}

	return ret;
}

static int bfin_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct bfin_rtc *rtc = dev_get_drvdata(dev);
	dev_dbg_stamp(dev);
	alrm->time = rtc->rtc_alarm;
	bfin_rtc_sync_pending(dev);
	alrm->enabled = !!(bfin_read_RTC_ICTL() & (RTC_ISTAT_ALARM | RTC_ISTAT_ALARM_DAY));
	return 0;
}

static int bfin_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct bfin_rtc *rtc = dev_get_drvdata(dev);
	unsigned long rtc_alarm;

	dev_dbg_stamp(dev);

	if (rtc_tm_to_time(&alrm->time, &rtc_alarm))
		return -EINVAL;

	rtc->rtc_alarm = alrm->time;

	bfin_rtc_sync_pending(dev);
	bfin_write_RTC_ALARM(rtc_time_to_bfin(rtc_alarm));
	if (alrm->enabled)
		bfin_rtc_int_set_alarm(rtc);

	return 0;
}

static int bfin_rtc_proc(struct device *dev, struct seq_file *seq)
{
#define yesno(x) ((x) ? "yes" : "no")
	u16 ictl = bfin_read_RTC_ICTL();
	dev_dbg_stamp(dev);
	seq_printf(seq,
		"alarm_IRQ\t: %s\n"
		"wkalarm_IRQ\t: %s\n"
		"seconds_IRQ\t: %s\n"
		"periodic_IRQ\t: %s\n",
		yesno(ictl & RTC_ISTAT_ALARM),
		yesno(ictl & RTC_ISTAT_ALARM_DAY),
		yesno(ictl & RTC_ISTAT_SEC),
		yesno(ictl & RTC_ISTAT_STOPWATCH));
	return 0;
#undef yesno
}

/**
 *	bfin_irq_set_freq - make sure hardware supports requested freq
 *	@dev: pointer to RTC device structure
 *	@freq: requested frequency rate
 *
 *	The Blackfin RTC can only generate periodic events at 1 per
 *	second (1 Hz), so reject any attempt at changing it.
 */
static int bfin_irq_set_freq(struct device *dev, int freq)
{
	dev_dbg_stamp(dev);
	return -ENOTTY;
}

static struct rtc_class_ops bfin_rtc_ops = {
	.open          = bfin_rtc_open,
	.release       = bfin_rtc_release,
	.ioctl         = bfin_rtc_ioctl,
	.read_time     = bfin_rtc_read_time,
	.set_time      = bfin_rtc_set_time,
	.read_alarm    = bfin_rtc_read_alarm,
	.set_alarm     = bfin_rtc_set_alarm,
	.proc          = bfin_rtc_proc,
	.irq_set_freq  = bfin_irq_set_freq,
};

static int __devinit bfin_rtc_probe(struct platform_device *pdev)
{
	struct bfin_rtc *rtc;
	int ret = 0;

	dev_dbg_stamp(&pdev->dev);

	rtc = kzalloc(sizeof(*rtc), GFP_KERNEL);
	if (unlikely(!rtc))
		return -ENOMEM;

	rtc->rtc_dev = rtc_device_register(pdev->name, &pdev->dev, &bfin_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		ret = PTR_ERR(rtc->rtc_dev);
		goto err;
	}
	rtc->rtc_dev->irq_freq = 1;

	platform_set_drvdata(pdev, rtc);

	return 0;

 err:
	kfree(rtc);
	return ret;
}

static int __devexit bfin_rtc_remove(struct platform_device *pdev)
{
	struct bfin_rtc *rtc = platform_get_drvdata(pdev);

	rtc_device_unregister(rtc->rtc_dev);
	platform_set_drvdata(pdev, NULL);
	kfree(rtc);

	return 0;
}

static struct platform_driver bfin_rtc_driver = {
	.driver		= {
		.name	= "rtc-bfin",
		.owner	= THIS_MODULE,
	},
	.probe		= bfin_rtc_probe,
	.remove		= __devexit_p(bfin_rtc_remove),
};

static int __init bfin_rtc_init(void)
{
	return platform_driver_register(&bfin_rtc_driver);
}

static void __exit bfin_rtc_exit(void)
{
	platform_driver_unregister(&bfin_rtc_driver);
}

module_init(bfin_rtc_init);
module_exit(bfin_rtc_exit);

MODULE_DESCRIPTION("Blackfin On-Chip Real Time Clock Driver");
MODULE_AUTHOR("Mike Frysinger <vapier@gentoo.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtc-bfin");
