// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux hwmon driver for ARCTIC Fan Controller
 *
 * USB Custom HID device with 10 fan channels.
 * Exposes fan RPM (input) and PWM (0-255) via hwmon. Device pushes IN reports
 * at ~1 Hz; no GET_REPORT. OUT reports set PWM duty (bytes 1-10, 0-100%).
 * PWM is manual-only: the device does not change duty autonomously, only
 * when it receives an OUT report from the host.
 */

#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/unaligned.h>

#define ARCTIC_VID			0x3904
#define ARCTIC_PID			0xF001
#define ARCTIC_NUM_FANS			10
#define ARCTIC_OUTPUT_REPORT_ID		0x01
#define ARCTIC_REPORT_LEN		32
#define ARCTIC_RPM_OFFSET		11	/* bytes 11-30: 10 x uint16 LE */
/* ACK report: device sends Report ID 0x02, 2 bytes (ID + status) after applying OUT report */
#define ARCTIC_ACK_REPORT_ID		0x02
#define ARCTIC_ACK_REPORT_LEN		2
/*
 * Time to wait for ACK report after send.
 * Measured over 500 iterations: max ~563 ms. Keep 1 s as margin.
 */
#define ARCTIC_ACK_TIMEOUT_MS		1000

struct arctic_fan_data {
	struct hid_device *hdev;
	struct device *hwmon_dev;	/* stored for explicit unregister in remove() */
	spinlock_t in_report_lock;	/* protects fan_rpm, ack_status, write_pending, pwm_duty */
	struct completion in_report_received; /* ACK (ID 0x02) received in raw_event */
	int ack_status;			/* 0 = OK, negative errno on device error */
	bool write_pending;		/* true while an OUT report ACK is in flight */
	u32 fan_rpm[ARCTIC_NUM_FANS];
	u8 pwm_duty[ARCTIC_NUM_FANS];	/* 0-255 matching sysfs range; converted to 0-100 on send */
	/*
	 * OUT report buffer passed to hid_hw_output_report(). Embedded in the
	 * devm_kzalloc'd struct so it is heap-allocated and passes
	 * usb_hcd_map_urb_for_dma(). Exclusively accessed by write(), which
	 * the hwmon core serializes.
	 */
	__dma_from_device_group_begin();
	u8 buf[ARCTIC_REPORT_LEN];
	__dma_from_device_group_end();
};

/*
 * Parse RPM values from the periodic status report (10 x uint16 LE at rpm_off).
 * pwm_duty is not updated from the report: the device is manual-only, so the
 * host cache is the authoritative source for PWM.
 * Called from raw_event which may run in IRQ context; must not sleep.
 */
static void arctic_fan_parse_report(struct arctic_fan_data *priv, u8 *buf,
				    int len, int rpm_off)
{
	unsigned long flags;
	int i;

	if (len < rpm_off + 20)
		return;

	spin_lock_irqsave(&priv->in_report_lock, flags);
	for (i = 0; i < ARCTIC_NUM_FANS; i++)
		priv->fan_rpm[i] = get_unaligned_le16(&buf[rpm_off + i * 2]);
	spin_unlock_irqrestore(&priv->in_report_lock, flags);
}

/*
 * raw_event: IN reports.
 *
 * Status report: Report ID 0x01, 32 bytes:
 *   byte 0 = report ID, bytes 1-10 = PWM 0-100%, bytes 11-30 = 10 x RPM uint16 LE.
 *   Device pushes these at ~1 Hz; no GET_REPORT.
 *
 * ACK report: Report ID 0x02, 2 bytes:
 *   byte 0 = 0x02, byte 1 = status (0x00 = OK, 0x01 = ERROR).
 *   Sent once after accepting and applying an OUT report (ID 0x01).
 */
static int arctic_fan_raw_event(struct hid_device *hdev,
				struct hid_report *report, u8 *data, int size)
{
	struct arctic_fan_data *priv = hid_get_drvdata(hdev);
	unsigned long flags;

	hid_dbg(hdev, "arctic_fan: raw_event id=%u size=%d\n", report->id, size);

	if (report->id == ARCTIC_ACK_REPORT_ID && size == ARCTIC_ACK_REPORT_LEN) {
		spin_lock_irqsave(&priv->in_report_lock, flags);
		/*
		 * Only deliver if a write is in flight. This prevents a
		 * late-arriving ACK from a timed-out write from erroneously
		 * satisfying a subsequent write's completion wait.
		 */
		if (priv->write_pending) {
			priv->ack_status = data[1] == 0x00 ? 0 : -EIO;
			complete(&priv->in_report_received);
		}
		spin_unlock_irqrestore(&priv->in_report_lock, flags);
		return 0;
	}

	if (report->id != ARCTIC_OUTPUT_REPORT_ID || size != ARCTIC_REPORT_LEN) {
		hid_dbg(hdev, "arctic_fan: raw_event id=%u size=%d ignored\n",
			report->id, size);
		return 0;
	}

	arctic_fan_parse_report(priv, data, size, ARCTIC_RPM_OFFSET);
	return 0;
}

static umode_t arctic_fan_is_visible(const void *data,
				     enum hwmon_sensor_types type,
				     u32 attr, int channel)
{
	if (type == hwmon_fan && attr == hwmon_fan_input)
		return 0444;
	if (type == hwmon_pwm && attr == hwmon_pwm_input)
		return 0644;
	return 0;
}

static int arctic_fan_read(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	struct arctic_fan_data *priv = dev_get_drvdata(dev);
	unsigned long flags;

	if (type == hwmon_fan && attr == hwmon_fan_input) {
		spin_lock_irqsave(&priv->in_report_lock, flags);
		*val = priv->fan_rpm[channel];
		spin_unlock_irqrestore(&priv->in_report_lock, flags);
		return 0;
	}
	if (type == hwmon_pwm && attr == hwmon_pwm_input) {
		spin_lock_irqsave(&priv->in_report_lock, flags);
		*val = priv->pwm_duty[channel];
		spin_unlock_irqrestore(&priv->in_report_lock, flags);
		return 0;
	}
	return -EINVAL;
}

static int arctic_fan_write(struct device *dev, enum hwmon_sensor_types type,
			    u32 attr, int channel, long val)
{
	struct arctic_fan_data *priv = dev_get_drvdata(dev);
	u8 new_duty = (u8)clamp_val(val, 0, 255);
	unsigned long flags;
	unsigned long t;
	int i, ret;

	/*
	 * Build the buffer and arm write_pending under in_report_lock so that
	 * reset_resume() cannot clear pwm_duty[] between the pwm_duty[] read
	 * and the buffer write, and raw_event() cannot deliver a stale ACK
	 * from a previous write into this write's completion.
	 *
	 * priv->buf is heap-allocated (embedded in the devm_kzalloc'd struct),
	 * satisfying usb_hcd_map_urb_for_dma(). Exclusively accessed by
	 * write() which the hwmon core serializes.
	 *
	 * pwm_duty[channel] is committed only after a positive device ACK so a
	 * failed or timed-out write does not corrupt the cached state.
	 *
	 * Residual theoretical race: if write A times out (write_pending
	 * cleared), write B sets write_pending = true, and a late ACK from
	 * write A—delayed beyond ARCTIC_ACK_TIMEOUT_MS—arrives during write
	 * B's pending window, it would falsely satisfy write B's completion.
	 * This cannot be prevented in driver code without protocol support
	 * (for example, a correlation ID echoed in the device ACK report).
	 * In testing, observed ACK latency stayed below the 1 s timeout
	 * (maximum ~563 ms over 500 iterations).
	 *
	 * The wait is non-interruptible so that a signal cannot cause write()
	 * to return early while the OUT report is already in flight; an
	 * interruptible early return would create the same late-ACK window
	 * without even the timeout guard.
	 * Serialized by the hwmon core: only one arctic_fan_write() at a time.
	 * Use irqsave to match the IRQ context in which raw_event may run.
	 */
	spin_lock_irqsave(&priv->in_report_lock, flags);
	priv->buf[0] = ARCTIC_OUTPUT_REPORT_ID;
	for (i = 0; i < ARCTIC_NUM_FANS; i++) {
		u8 d = i == channel ? new_duty : priv->pwm_duty[i];

		priv->buf[1 + i] = DIV_ROUND_CLOSEST((unsigned int)d * 100, 255);
	}
	priv->ack_status = -ETIMEDOUT;
	priv->write_pending = true;
	reinit_completion(&priv->in_report_received);
	spin_unlock_irqrestore(&priv->in_report_lock, flags);

	ret = hid_hw_output_report(priv->hdev, priv->buf, ARCTIC_REPORT_LEN);
	if (ret < 0) {
		spin_lock_irqsave(&priv->in_report_lock, flags);
		priv->write_pending = false;
		spin_unlock_irqrestore(&priv->in_report_lock, flags);
		return ret;
	}

	t = wait_for_completion_timeout(&priv->in_report_received,
					msecs_to_jiffies(ARCTIC_ACK_TIMEOUT_MS));
	spin_lock_irqsave(&priv->in_report_lock, flags);
	priv->write_pending = false;
	/* Commit inside the lock so reset_resume() cannot race with this write */
	if (t && priv->ack_status == 0)
		priv->pwm_duty[channel] = new_duty;
	spin_unlock_irqrestore(&priv->in_report_lock, flags);

	if (!t)
		return -ETIMEDOUT;
	return priv->ack_status; /* 0=OK, -EIO=device error */
}

static const struct hwmon_ops arctic_fan_ops = {
	.is_visible = arctic_fan_is_visible,
	.read = arctic_fan_read,
	.write = arctic_fan_write,
};

static const struct hwmon_channel_info *arctic_fan_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT, HWMON_F_INPUT, HWMON_F_INPUT,
			   HWMON_F_INPUT, HWMON_F_INPUT, HWMON_F_INPUT,
			   HWMON_F_INPUT, HWMON_F_INPUT, HWMON_F_INPUT,
			   HWMON_F_INPUT),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT, HWMON_PWM_INPUT, HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT, HWMON_PWM_INPUT, HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT, HWMON_PWM_INPUT, HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_chip_info arctic_fan_chip_info = {
	.ops = &arctic_fan_ops,
	.info = arctic_fan_info,
};

static int arctic_fan_reset_resume(struct hid_device *hdev)
{
	struct arctic_fan_data *priv = hid_get_drvdata(hdev);
	unsigned long flags;

	/*
	 * The device resets its PWM channels to hardware defaults on power
	 * loss during suspend. Clear the cached duty values so they reflect
	 * the unknown hardware state, consistent with probe-time behaviour
	 * (the device has no GET_REPORT support). Hold in_report_lock so
	 * this does not race with a concurrent pwm read or write callback.
	 */
	spin_lock_irqsave(&priv->in_report_lock, flags);
	memset(priv->pwm_duty, 0, sizeof(priv->pwm_duty));
	spin_unlock_irqrestore(&priv->in_report_lock, flags);
	return 0;
}

static int arctic_fan_probe(struct hid_device *hdev,
			    const struct hid_device_id *id)
{
	struct arctic_fan_data *priv;
	int ret;

	if (!hid_is_usb(hdev))
		return -ENODEV;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	priv = devm_kzalloc(&hdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->hdev = hdev;
	spin_lock_init(&priv->in_report_lock);
	init_completion(&priv->in_report_received);
	hid_set_drvdata(hdev, priv);

	ret = hid_hw_start(hdev, HID_CONNECT_DRIVER);
	if (ret)
		return ret;

	ret = hid_hw_open(hdev);
	if (ret)
		goto out_stop;

	/*
	 * Start IO before registering with hwmon. If IO were started after
	 * hwmon registration, a sysfs write arriving in that narrow window
	 * would send an OUT report but the ACK could not be delivered (the HID
	 * core discards events until io_started), causing a spurious timeout.
	 */
	hid_device_io_start(hdev);

	/*
	 * Use the non-devm variant and store the pointer so remove() can
	 * call hwmon_device_unregister() before tearing down the HID
	 * transport. devm_hwmon_device_register_with_info() would defer
	 * unregistration until after remove() returns, leaving a window
	 * where a concurrent sysfs write could call hid_hw_output_report()
	 * on an already-stopped device (use-after-free).
	 */
	priv->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, "arctic_fan",
							  priv, &arctic_fan_chip_info,
							  NULL);
	if (IS_ERR(priv->hwmon_dev)) {
		ret = PTR_ERR(priv->hwmon_dev);
		goto out_close;
	}

	return 0;

out_close:
	hid_device_io_stop(hdev);
	hid_hw_close(hdev);
out_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void arctic_fan_remove(struct hid_device *hdev)
{
	struct arctic_fan_data *priv = hid_get_drvdata(hdev);

	/*
	 * Unregister hwmon before stopping the HID transport. This removes
	 * the sysfs files and waits for any in-progress write() callback to
	 * return, so no hwmon op can call hid_hw_output_report() after
	 * hid_hw_stop() frees the underlying USB resources.
	 * Matches the pattern used by nzxt-smart2 and aquacomputer_d5next.
	 *
	 * The HID core clears hdev->io_started before invoking ->remove(),
	 * so hid_device_io_stop() is not called here; doing so would emit
	 * a spurious "io already stopped" warning.
	 */
	hwmon_device_unregister(priv->hwmon_dev);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id arctic_fan_id_table[] = {
	{ HID_USB_DEVICE(ARCTIC_VID, ARCTIC_PID) },
	{ }
};
MODULE_DEVICE_TABLE(hid, arctic_fan_id_table);

static struct hid_driver arctic_fan_driver = {
	.name = "arctic_fan",
	.id_table = arctic_fan_id_table,
	.probe = arctic_fan_probe,
	.remove = arctic_fan_remove,
	.raw_event = arctic_fan_raw_event,
	.reset_resume = arctic_fan_reset_resume,
};

module_hid_driver(arctic_fan_driver);

MODULE_AUTHOR("Aureo Serrano de Souza <aureo.serrano@arctic.de>");
MODULE_DESCRIPTION("HID hwmon driver for ARCTIC Fan Controller");
MODULE_LICENSE("GPL");
