#include <linux/proc_fs.h>
#include <linux/seq_file.h>
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
 * /proc/acpi/sleep
 * /proc/acpi/alarm
 * /proc/acpi/wakeup
 */

ACPI_MODULE_NAME("sleep")
#ifdef	CONFIG_ACPI_PROCFS
static int acpi_system_sleep_seq_show(struct seq_file *seq, void *offset)
{
	int i;

	ACPI_FUNCTION_TRACE("acpi_system_sleep_seq_show");

	for (i = 0; i <= ACPI_STATE_S5; i++) {
		if (sleep_states[i]) {
			seq_printf(seq, "S%d ", i);
		}
	}

	seq_puts(seq, "\n");

	return 0;
}

static int acpi_system_sleep_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_system_sleep_seq_show, PDE(inode)->data);
}

static ssize_t
acpi_system_write_sleep(struct file *file,
			const char __user * buffer, size_t count, loff_t * ppos)
{
	char str[12];
	u32 state = 0;
	int error = 0;

	if (count > sizeof(str) - 1)
		goto Done;
	memset(str, 0, sizeof(str));
	if (copy_from_user(str, buffer, count))
		return -EFAULT;

	/* Check for S4 bios request */
	if (!strcmp(str, "4b")) {
		error = acpi_suspend(4);
		goto Done;
	}
	state = simple_strtoul(str, NULL, 0);
#ifdef CONFIG_HIBERNATION
	if (state == 4) {
		error = hibernate();
		goto Done;
	}
#endif
	error = acpi_suspend(state);
      Done:
	return error ? error : count;
}
#endif				/* CONFIG_ACPI_PROCFS */

#if defined(CONFIG_RTC_DRV_CMOS) || defined(CONFIG_RTC_DRV_CMOS_MODULE) || !defined(CONFIG_X86)
/* use /sys/class/rtc/rtcX/wakealarm instead; it's not ACPI-specific */
#else
#define	HAVE_ACPI_LEGACY_ALARM
#endif

#ifdef	HAVE_ACPI_LEGACY_ALARM

static int acpi_system_alarm_seq_show(struct seq_file *seq, void *offset)
{
	u32 sec, min, hr;
	u32 day, mo, yr, cent = 0;
	unsigned char rtc_control = 0;
	unsigned long flags;

	ACPI_FUNCTION_TRACE("acpi_system_alarm_seq_show");

	spin_lock_irqsave(&rtc_lock, flags);

	sec = CMOS_READ(RTC_SECONDS_ALARM);
	min = CMOS_READ(RTC_MINUTES_ALARM);
	hr = CMOS_READ(RTC_HOURS_ALARM);
	rtc_control = CMOS_READ(RTC_CONTROL);

	/* If we ever get an FACP with proper values... */
	if (acpi_gbl_FADT.day_alarm)
		/* ACPI spec: only low 6 its should be cared */
		day = CMOS_READ(acpi_gbl_FADT.day_alarm) & 0x3F;
	else
		day = CMOS_READ(RTC_DAY_OF_MONTH);
	if (acpi_gbl_FADT.month_alarm)
		mo = CMOS_READ(acpi_gbl_FADT.month_alarm);
	else
		mo = CMOS_READ(RTC_MONTH);
	if (acpi_gbl_FADT.century)
		cent = CMOS_READ(acpi_gbl_FADT.century);

	yr = CMOS_READ(RTC_YEAR);

	spin_unlock_irqrestore(&rtc_lock, flags);

	if (!(rtc_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		BCD_TO_BIN(sec);
		BCD_TO_BIN(min);
		BCD_TO_BIN(hr);
		BCD_TO_BIN(day);
		BCD_TO_BIN(mo);
		BCD_TO_BIN(yr);
		BCD_TO_BIN(cent);
	}

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
	return single_open(file, acpi_system_alarm_seq_show, PDE(inode)->data);
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
		BCD_TO_BIN(val);
	return val;
}

/* Write binary value into possibly BCD register */
static void cmos_bcd_write(u32 val, int offset, int rtc_control)
{
	if (!(rtc_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
		BIN_TO_BCD(val);
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

	ACPI_FUNCTION_TRACE("acpi_system_write_alarm");

	if (count > sizeof(alarm_string) - 1)
		return_VALUE(-EINVAL);

	if (copy_from_user(alarm_string, buffer, count))
		return_VALUE(-EFAULT);

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
	if (acpi_gbl_FADT.century)
		cmos_bcd_write(yr / 100, acpi_gbl_FADT.century, rtc_control);
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
	return_VALUE(result ? result : count);
}
#endif				/* HAVE_ACPI_LEGACY_ALARM */

extern struct list_head acpi_wakeup_device_list;
extern spinlock_t acpi_device_lock;

static int
acpi_system_wakeup_device_seq_show(struct seq_file *seq, void *offset)
{
	struct list_head *node, *next;

	seq_printf(seq, "Device\tS-state\t  Status   Sysfs node\n");

	spin_lock(&acpi_device_lock);
	list_for_each_safe(node, next, &acpi_wakeup_device_list) {
		struct acpi_device *dev =
		    container_of(node, struct acpi_device, wakeup_list);
		struct device *ldev;

		if (!dev->wakeup.flags.valid)
			continue;
		spin_unlock(&acpi_device_lock);

		ldev = acpi_get_physical_device(dev->handle);
		seq_printf(seq, "%s\t  S%d\t%c%-8s  ",
			   dev->pnp.bus_id,
			   (u32) dev->wakeup.sleep_state,
			   dev->wakeup.flags.run_wake ? '*' : ' ',
			   dev->wakeup.state.enabled ? "enabled" : "disabled");
		if (ldev)
			seq_printf(seq, "%s:%s",
				   ldev->bus ? ldev->bus->name : "no-bus",
				   ldev->bus_id);
		seq_printf(seq, "\n");
		put_device(ldev);

		spin_lock(&acpi_device_lock);
	}
	spin_unlock(&acpi_device_lock);
	return 0;
}

static ssize_t
acpi_system_write_wakeup_device(struct file *file,
				const char __user * buffer,
				size_t count, loff_t * ppos)
{
	struct list_head *node, *next;
	char strbuf[5];
	char str[5] = "";
	int len = count;
	struct acpi_device *found_dev = NULL;

	if (len > 4)
		len = 4;

	if (copy_from_user(strbuf, buffer, len))
		return -EFAULT;
	strbuf[len] = '\0';
	sscanf(strbuf, "%s", str);

	spin_lock(&acpi_device_lock);
	list_for_each_safe(node, next, &acpi_wakeup_device_list) {
		struct acpi_device *dev =
		    container_of(node, struct acpi_device, wakeup_list);
		if (!dev->wakeup.flags.valid)
			continue;

		if (!strncmp(dev->pnp.bus_id, str, 4)) {
			dev->wakeup.state.enabled =
			    dev->wakeup.state.enabled ? 0 : 1;
			found_dev = dev;
			break;
		}
	}
	if (found_dev) {
		list_for_each_safe(node, next, &acpi_wakeup_device_list) {
			struct acpi_device *dev = container_of(node,
							       struct
							       acpi_device,
							       wakeup_list);

			if ((dev != found_dev) &&
			    (dev->wakeup.gpe_number ==
			     found_dev->wakeup.gpe_number)
			    && (dev->wakeup.gpe_device ==
				found_dev->wakeup.gpe_device)) {
				printk(KERN_WARNING
				       "ACPI: '%s' and '%s' have the same GPE, "
				       "can't disable/enable one seperately\n",
				       dev->pnp.bus_id, found_dev->pnp.bus_id);
				dev->wakeup.state.enabled =
				    found_dev->wakeup.state.enabled;
			}
		}
	}
	spin_unlock(&acpi_device_lock);
	return count;
}

static int
acpi_system_wakeup_device_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_system_wakeup_device_seq_show,
			   PDE(inode)->data);
}

static const struct file_operations acpi_system_wakeup_device_fops = {
	.owner = THIS_MODULE,
	.open = acpi_system_wakeup_device_open_fs,
	.read = seq_read,
	.write = acpi_system_write_wakeup_device,
	.llseek = seq_lseek,
	.release = single_release,
};

#ifdef	CONFIG_ACPI_PROCFS
static const struct file_operations acpi_system_sleep_fops = {
	.owner = THIS_MODULE,
	.open = acpi_system_sleep_open_fs,
	.read = seq_read,
	.write = acpi_system_write_sleep,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif				/* CONFIG_ACPI_PROCFS */

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

static int __init acpi_sleep_proc_init(void)
{
	if (acpi_disabled)
		return 0;

#ifdef	CONFIG_ACPI_PROCFS
	/* 'sleep' [R/W] */
	proc_create("sleep", S_IFREG | S_IRUGO | S_IWUSR,
		    acpi_root_dir, &acpi_system_sleep_fops);
#endif				/* CONFIG_ACPI_PROCFS */

#ifdef	HAVE_ACPI_LEGACY_ALARM
	/* 'alarm' [R/W] */
	proc_create("alarm", S_IFREG | S_IRUGO | S_IWUSR,
		    acpi_root_dir, &acpi_system_alarm_fops);

	acpi_install_fixed_event_handler(ACPI_EVENT_RTC, rtc_handler, NULL);
#endif				/* HAVE_ACPI_LEGACY_ALARM */

	/* 'wakeup device' [R/W] */
	proc_create("wakeup", S_IFREG | S_IRUGO | S_IWUSR,
		    acpi_root_dir, &acpi_system_wakeup_device_fops);

	return 0;
}

late_initcall(acpi_sleep_proc_init);
