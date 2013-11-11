#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/bcd.h>
#include <asm/uaccess.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#ifdef CONFIG_X86
#include <linux/mc146818rtc.h>
#endif

#include "sleep.h"

#define _COMPONENT		ACPI_SYSTEM_COMPONENT

/*
 * this file provides support for:
 * /proc/acpi/alarm
 * /proc/acpi/wakeup
 */

ACPI_MODULE_NAME("sleep")

#if defined(CONFIG_RTC_DRV_CMOS) || defined(CONFIG_RTC_DRV_CMOS_MODULE) || !defined(CONFIG_X86)
/* use /sys/class/rtc/rtcX/wakealarm instead; it's not ACPI-specific */
#else
#define	HAVE_ACPI_LEGACY_ALARM
#endif

#ifdef	HAVE_ACPI_LEGACY_ALARM

static u32 cmos_bcd_read(int offset, int rtc_control);

static int acpi_system_alarm_seq_show(struct seq_file *seq, void *offset)
{
	u32 sec, min, hr;
	u32 day, mo, yr, cent = 0;
	u32 today = 0;
	unsigned char rtc_control = 0;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);

	rtc_control = CMOS_READ(RTC_CONTROL);
	sec = cmos_bcd_read(RTC_SECONDS_ALARM, rtc_control);
	min = cmos_bcd_read(RTC_MINUTES_ALARM, rtc_control);
	hr = cmos_bcd_read(RTC_HOURS_ALARM, rtc_control);

	/* If we ever get an FACP with proper values... */
	if (acpi_gbl_FADT.day_alarm) {
		/* ACPI spec: only low 6 its should be cared */
		day = CMOS_READ(acpi_gbl_FADT.day_alarm) & 0x3F;
		if (!(rtc_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
			day = bcd2bin(day);
	} else
		day = cmos_bcd_read(RTC_DAY_OF_MONTH, rtc_control);
	if (acpi_gbl_FADT.month_alarm)
		mo = cmos_bcd_read(acpi_gbl_FADT.month_alarm, rtc_control);
	else {
		mo = cmos_bcd_read(RTC_MONTH, rtc_control);
		today = cmos_bcd_read(RTC_DAY_OF_MONTH, rtc_control);
	}
	if (acpi_gbl_FADT.century)
		cent = cmos_bcd_read(acpi_gbl_FADT.century, rtc_control);

	yr = cmos_bcd_read(RTC_YEAR, rtc_control);

	spin_unlock_irqrestore(&rtc_lock, flags);

	/* we're trusting the FADT (see above) */
	if (!acpi_gbl_FADT.century)
		/* If we're not trusting the FADT, we should at least make it
		 * right for _this_ century... ehm, what is _this_ century?
		 *
		 * TBD:
		 *  ASAP: find piece of code in the kernel, e.g. star tracker driver,
		 *        which we can trust to determine the century correctly. Atom
		 *        watch driver would be nice, too...
		 *
		 *  if that has not happened, change for first release in 2050:
		 *        if (yr<50)
		 *                yr += 2100;
		 *        else
		 *                yr += 2000;   // current line of code
		 *
		 *  if that has not happened either, please do on 2099/12/31:23:59:59
		 *        s/2000/2100
		 *
		 */
		yr += 2000;
	else
		yr += cent * 100;

	/*
	 * Show correct dates for alarms up to a month into the future.
	 * This solves issues for nearly all situations with the common
	 * 30-day alarm clocks in PC hardware.
	 */
	if (day < today) {
		if (mo < 12) {
			mo += 1;
		} else {
			mo = 1;
			yr += 1;
		}
	}

	seq_printf(seq, "%4.4u-", yr);
	(mo > 12) ? seq_puts(seq, "**-") : seq_printf(seq, "%2.2u-", mo);
	(day > 31) ? seq_puts(seq, "** ") : seq_printf(seq, "%2.2u ", day);
	(hr > 23) ? seq_puts(seq, "**:") : seq_printf(seq, "%2.2u:", hr);
	(min > 59) ? seq_puts(seq, "**:") : seq_printf(seq, "%2.2u:", min);
	(sec > 59) ? seq_puts(seq, "**\n") : seq_printf(seq, "%2.2u\n", sec);

	return 0;
}

static int acpi_system_alarm_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_system_alarm_seq_show, PDE_DATA(inode));
}

static int get_date_field(char **p, u32 * value)
{
	char *next = NULL;
	char *string_end = NULL;
	int result = -EINVAL;

	/*
	 * Try to find delimeter, only to insert null.  The end of the
	 * string won't have one, but is still valid.
	 */
	if (*p == NULL)
		return result;

	next = strpbrk(*p, "- :");
	if (next)
		*next++ = '\0';

	*value = simple_strtoul(*p, &string_end, 10);

	/* Signal success if we got a good digit */
	if (string_end != *p)
		result = 0;

	if (next)
		*p = next;
	else
		*p = NULL;

	return result;
}

/* Read a possibly BCD register, always return binary */
static u32 cmos_bcd_read(int offset, int rtc_control)
{
	u32 val = CMOS_READ(offset);
	if (!(rtc_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
		val = bcd2bin(val);
	return val;
}

/* Write binary value into possibly BCD register */
static void cmos_bcd_write(u32 val, int offset, int rtc_control)
{
	if (!(rtc_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
		val = bin2bcd(val);
	CMOS_WRITE(val, offset);
}

static ssize_t
acpi_system_write_alarm(struct file *file,
			const char __user * buffer, size_t count, loff_t * ppos)
{
	int result = 0;
	char alarm_string[30] = { '\0' };
	char *p = alarm_string;
	u32 sec, min, hr, day, mo, yr;
	int adjust = 0;
	unsigned char rtc_control = 0;

	if (count > sizeof(alarm_string) - 1)
		return -EINVAL;

	if (copy_from_user(alarm_string, buffer, count))
		return -EFAULT;

	alarm_string[count] = '\0';

	/* check for time adjustment */
	if (alarm_string[0] == '+') {
		p++;
		adjust = 1;
	}

	if ((result = get_date_field(&p, &yr)))
		goto end;
	if ((result = get_date_field(&p, &mo)))
		goto end;
	if ((result = get_date_field(&p, &day)))
		goto end;
	if ((result = get_date_field(&p, &hr)))
		goto end;
	if ((result = get_date_field(&p, &min)))
		goto end;
	if ((result = get_date_field(&p, &sec)))
		goto end;

	spin_lock_irq(&rtc_lock);

	rtc_control = CMOS_READ(RTC_CONTROL);

	if (adjust) {
		yr += cmos_bcd_read(RTC_YEAR, rtc_control);
		mo += cmos_bcd_read(RTC_MONTH, rtc_control);
		day += cmos_bcd_read(RTC_DAY_OF_MONTH, rtc_control);
		hr += cmos_bcd_read(RTC_HOURS, rtc_control);
		min += cmos_bcd_read(RTC_MINUTES, rtc_control);
		sec += cmos_bcd_read(RTC_SECONDS, rtc_control);
	}

	spin_unlock_irq(&rtc_lock);

	if (sec > 59) {
		min += sec/60;
		sec = sec%60;
	}
	if (min > 59) {
		hr += min/60;
		min = min%60;
	}
	if (hr > 23) {
		day += hr/24;
		hr = hr%24;
	}
	if (day > 31) {
		mo += day/32;
		day = day%32;
	}
	if (mo > 12) {
		yr += mo/13;
		mo = mo%13;
	}

	spin_lock_irq(&rtc_lock);
	/*
	 * Disable alarm interrupt before setting alarm timer or else
	 * when ACPI_EVENT_RTC is enabled, a spurious ACPI interrupt occurs
	 */
	rtc_control &= ~RTC_AIE;
	CMOS_WRITE(rtc_control, RTC_CONTROL);
	CMOS_READ(RTC_INTR_FLAGS);

	/* write the fields the rtc knows about */
	cmos_bcd_write(hr, RTC_HOURS_ALARM, rtc_control);
	cmos_bcd_write(min, RTC_MINUTES_ALARM, rtc_control);
	cmos_bcd_write(sec, RTC_SECONDS_ALARM, rtc_control);

	/*
	 * If the system supports an enhanced alarm it will have non-zero
	 * offsets into the CMOS RAM here -- which for some reason are pointing
	 * to the RTC area of memory.
	 */
	if (acpi_gbl_FADT.day_alarm)
		cmos_bcd_write(day, acpi_gbl_FADT.day_alarm, rtc_control);
	if (acpi_gbl_FADT.month_alarm)
		cmos_bcd_write(mo, acpi_gbl_FADT.month_alarm, rtc_control);
	if (acpi_gbl_FADT.century) {
		if (adjust)
			yr += cmos_bcd_read(acpi_gbl_FADT.century, rtc_control) * 100;
		cmos_bcd_write(yr / 100, acpi_gbl_FADT.century, rtc_control);
	}
	/* enable the rtc alarm interrupt */
	rtc_control |= RTC_AIE;
	CMOS_WRITE(rtc_control, RTC_CONTROL);
	CMOS_READ(RTC_INTR_FLAGS);

	spin_unlock_irq(&rtc_lock);

	acpi_clear_event(ACPI_EVENT_RTC);
	acpi_enable_event(ACPI_EVENT_RTC, 0);

	*ppos += count;

	result = 0;
      end:
	return result ? result : count;
}
#endif				/* HAVE_ACPI_LEGACY_ALARM */

static int
acpi_system_wakeup_device_seq_show(struct seq_file *seq, void *offset)
{
	struct list_head *node, *next;

	seq_printf(seq, "Device\tS-state\t  Status   Sysfs node\n");

	mutex_lock(&acpi_device_lock);
	list_for_each_safe(node, next, &acpi_wakeup_device_list) {
		struct acpi_device *dev =
		    container_of(node, struct acpi_device, wakeup_list);
		struct acpi_device_physical_node *entry;

		if (!dev->wakeup.flags.valid)
			continue;

		seq_printf(seq, "%s\t  S%d\t",
			   dev->pnp.bus_id,
			   (u32) dev->wakeup.sleep_state);

		mutex_lock(&dev->physical_node_lock);

		if (!dev->physical_node_count) {
			seq_printf(seq, "%c%-8s\n",
				dev->wakeup.flags.run_wake ? '*' : ' ',
				device_may_wakeup(&dev->dev) ?
					"enabled" : "disabled");
		} else {
			struct device *ldev;
			list_for_each_entry(entry, &dev->physical_node_list,
					node) {
				ldev = get_device(entry->dev);
				if (!ldev)
					continue;

				if (&entry->node !=
						dev->physical_node_list.next)
					seq_printf(seq, "\t\t");

				seq_printf(seq, "%c%-8s  %s:%s\n",
					dev->wakeup.flags.run_wake ? '*' : ' ',
					(device_may_wakeup(&dev->dev) ||
					(ldev && device_may_wakeup(ldev))) ?
					"enabled" : "disabled",
					ldev->bus ? ldev->bus->name :
					"no-bus", dev_name(ldev));
				put_device(ldev);
			}
		}

		mutex_unlock(&dev->physical_node_lock);
	}
	mutex_unlock(&acpi_device_lock);
	return 0;
}

static void physical_device_enable_wakeup(struct acpi_device *adev)
{
	struct acpi_device_physical_node *entry;

	mutex_lock(&adev->physical_node_lock);

	list_for_each_entry(entry,
		&adev->physical_node_list, node)
		if (entry->dev && device_can_wakeup(entry->dev)) {
			bool enable = !device_may_wakeup(entry->dev);
			device_set_wakeup_enable(entry->dev, enable);
		}

	mutex_unlock(&adev->physical_node_lock);
}

static ssize_t
acpi_system_write_wakeup_device(struct file *file,
				const char __user * buffer,
				size_t count, loff_t * ppos)
{
	struct list_head *node, *next;
	char strbuf[5];
	char str[5] = "";

	if (count > 4)
		count = 4;

	if (copy_from_user(strbuf, buffer, count))
		return -EFAULT;
	strbuf[count] = '\0';
	sscanf(strbuf, "%s", str);

	mutex_lock(&acpi_device_lock);
	list_for_each_safe(node, next, &acpi_wakeup_device_list) {
		struct acpi_device *dev =
		    container_of(node, struct acpi_device, wakeup_list);
		if (!dev->wakeup.flags.valid)
			continue;

		if (!strncmp(dev->pnp.bus_id, str, 4)) {
			if (device_can_wakeup(&dev->dev)) {
				bool enable = !device_may_wakeup(&dev->dev);
				device_set_wakeup_enable(&dev->dev, enable);
			} else {
				physical_device_enable_wakeup(dev);
			}
			break;
		}
	}
	mutex_unlock(&acpi_device_lock);
	return count;
}

static int
acpi_system_wakeup_device_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_system_wakeup_device_seq_show,
			   PDE_DATA(inode));
}

static const struct file_operations acpi_system_wakeup_device_fops = {
	.owner = THIS_MODULE,
	.open = acpi_system_wakeup_device_open_fs,
	.read = seq_read,
	.write = acpi_system_write_wakeup_device,
	.llseek = seq_lseek,
	.release = single_release,
};

#ifdef	HAVE_ACPI_LEGACY_ALARM
static const struct file_operations acpi_system_alarm_fops = {
	.owner = THIS_MODULE,
	.open = acpi_system_alarm_open_fs,
	.read = seq_read,
	.write = acpi_system_write_alarm,
	.llseek = seq_lseek,
	.release = single_release,
};

static u32 rtc_handler(void *context)
{
	acpi_clear_event(ACPI_EVENT_RTC);
	acpi_disable_event(ACPI_EVENT_RTC, 0);

	return ACPI_INTERRUPT_HANDLED;
}
#endif				/* HAVE_ACPI_LEGACY_ALARM */

int __init acpi_sleep_proc_init(void)
{
#ifdef	HAVE_ACPI_LEGACY_ALARM
	/* 'alarm' [R/W] */
	proc_create("alarm", S_IFREG | S_IRUGO | S_IWUSR,
		    acpi_root_dir, &acpi_system_alarm_fops);

	acpi_install_fixed_event_handler(ACPI_EVENT_RTC, rtc_handler, NULL);
	/*
	 * Disable the RTC event after installing RTC handler.
	 * Only when RTC alarm is set will it be enabled.
	 */
	acpi_clear_event(ACPI_EVENT_RTC);
	acpi_disable_event(ACPI_EVENT_RTC, 0);
#endif				/* HAVE_ACPI_LEGACY_ALARM */

	/* 'wakeup device' [R/W] */
	proc_create("wakeup", S_IFREG | S_IRUGO | S_IWUSR,
		    acpi_root_dir, &acpi_system_wakeup_device_fops);

	return 0;
}
