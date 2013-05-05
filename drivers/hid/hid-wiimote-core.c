/*
 * HID driver for Nintendo Wii / Wii U peripherals
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include "hid-ids.h"
#include "hid-wiimote.h"

/* output queue handling */

static int wiimote_hid_send(struct hid_device *hdev, __u8 *buffer,
			    size_t count)
{
	__u8 *buf;
	int ret;

	if (!hdev->hid_output_raw_report)
		return -ENODEV;

	buf = kmemdup(buffer, count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = hdev->hid_output_raw_report(hdev, buf, count, HID_OUTPUT_REPORT);

	kfree(buf);
	return ret;
}

static void wiimote_queue_worker(struct work_struct *work)
{
	struct wiimote_queue *queue = container_of(work, struct wiimote_queue,
						   worker);
	struct wiimote_data *wdata = container_of(queue, struct wiimote_data,
						  queue);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&wdata->queue.lock, flags);

	while (wdata->queue.head != wdata->queue.tail) {
		spin_unlock_irqrestore(&wdata->queue.lock, flags);
		ret = wiimote_hid_send(wdata->hdev,
				 wdata->queue.outq[wdata->queue.tail].data,
				 wdata->queue.outq[wdata->queue.tail].size);
		if (ret < 0) {
			spin_lock_irqsave(&wdata->state.lock, flags);
			wiimote_cmd_abort(wdata);
			spin_unlock_irqrestore(&wdata->state.lock, flags);
		}
		spin_lock_irqsave(&wdata->queue.lock, flags);

		wdata->queue.tail = (wdata->queue.tail + 1) % WIIMOTE_BUFSIZE;
	}

	spin_unlock_irqrestore(&wdata->queue.lock, flags);
}

static void wiimote_queue(struct wiimote_data *wdata, const __u8 *buffer,
								size_t count)
{
	unsigned long flags;
	__u8 newhead;

	if (count > HID_MAX_BUFFER_SIZE) {
		hid_warn(wdata->hdev, "Sending too large output report\n");

		spin_lock_irqsave(&wdata->queue.lock, flags);
		goto out_error;
	}

	/*
	 * Copy new request into our output queue and check whether the
	 * queue is full. If it is full, discard this request.
	 * If it is empty we need to start a new worker that will
	 * send out the buffer to the hid device.
	 * If the queue is not empty, then there must be a worker
	 * that is currently sending out our buffer and this worker
	 * will reschedule itself until the queue is empty.
	 */

	spin_lock_irqsave(&wdata->queue.lock, flags);

	memcpy(wdata->queue.outq[wdata->queue.head].data, buffer, count);
	wdata->queue.outq[wdata->queue.head].size = count;
	newhead = (wdata->queue.head + 1) % WIIMOTE_BUFSIZE;

	if (wdata->queue.head == wdata->queue.tail) {
		wdata->queue.head = newhead;
		schedule_work(&wdata->queue.worker);
	} else if (newhead != wdata->queue.tail) {
		wdata->queue.head = newhead;
	} else {
		hid_warn(wdata->hdev, "Output queue is full");
		goto out_error;
	}

	goto out_unlock;

out_error:
	wiimote_cmd_abort(wdata);
out_unlock:
	spin_unlock_irqrestore(&wdata->queue.lock, flags);
}

/*
 * This sets the rumble bit on the given output report if rumble is
 * currently enabled.
 * \cmd1 must point to the second byte in the output report => &cmd[1]
 * This must be called on nearly every output report before passing it
 * into the output queue!
 */
static inline void wiiproto_keep_rumble(struct wiimote_data *wdata, __u8 *cmd1)
{
	if (wdata->state.flags & WIIPROTO_FLAG_RUMBLE)
		*cmd1 |= 0x01;
}

void wiiproto_req_rumble(struct wiimote_data *wdata, __u8 rumble)
{
	__u8 cmd[2];

	rumble = !!rumble;
	if (rumble == !!(wdata->state.flags & WIIPROTO_FLAG_RUMBLE))
		return;

	if (rumble)
		wdata->state.flags |= WIIPROTO_FLAG_RUMBLE;
	else
		wdata->state.flags &= ~WIIPROTO_FLAG_RUMBLE;

	cmd[0] = WIIPROTO_REQ_RUMBLE;
	cmd[1] = 0;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

void wiiproto_req_leds(struct wiimote_data *wdata, int leds)
{
	__u8 cmd[2];

	leds &= WIIPROTO_FLAGS_LEDS;
	if ((wdata->state.flags & WIIPROTO_FLAGS_LEDS) == leds)
		return;
	wdata->state.flags = (wdata->state.flags & ~WIIPROTO_FLAGS_LEDS) | leds;

	cmd[0] = WIIPROTO_REQ_LED;
	cmd[1] = 0;

	if (leds & WIIPROTO_FLAG_LED1)
		cmd[1] |= 0x10;
	if (leds & WIIPROTO_FLAG_LED2)
		cmd[1] |= 0x20;
	if (leds & WIIPROTO_FLAG_LED3)
		cmd[1] |= 0x40;
	if (leds & WIIPROTO_FLAG_LED4)
		cmd[1] |= 0x80;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

/*
 * Check what peripherals of the wiimote are currently
 * active and select a proper DRM that supports all of
 * the requested data inputs.
 *
 * Not all combinations are actually supported. The following
 * combinations work only with limitations:
 *  - IR cam in extended or full mode disables any data transmission
 *    of extension controllers. There is no DRM mode that supports
 *    extension bytes plus extended/full IR.
 *  - IR cam with accelerometer and extension *_EXT8 is not supported.
 *    However, all extensions that need *_EXT8 are devices that don't
 *    support IR cameras. Hence, this shouldn't happen under normal
 *    operation.
 *  - *_EXT16 is only supported in combination with buttons and
 *    accelerometer. No IR or similar can be active simultaneously. As
 *    above, all modules that require it are mutually exclusive with
 *    IR/etc. so this doesn't matter.
 */
static __u8 select_drm(struct wiimote_data *wdata)
{
	__u8 ir = wdata->state.flags & WIIPROTO_FLAGS_IR;
	bool ext;

	ext = (wdata->state.flags & WIIPROTO_FLAG_EXT_USED) ||
	      (wdata->state.flags & WIIPROTO_FLAG_MP_USED);

	/* some 3rd-party balance-boards are hard-coded to KEE, *sigh* */
	if (wdata->state.devtype == WIIMOTE_DEV_BALANCE_BOARD) {
		if (ext)
			return WIIPROTO_REQ_DRM_KEE;
		else
			return WIIPROTO_REQ_DRM_K;
	}

	if (ir == WIIPROTO_FLAG_IR_BASIC) {
		if (wdata->state.flags & WIIPROTO_FLAG_ACCEL) {
			if (ext)
				return WIIPROTO_REQ_DRM_KAIE;
			else
				return WIIPROTO_REQ_DRM_KAI;
		} else {
			return WIIPROTO_REQ_DRM_KIE;
		}
	} else if (ir == WIIPROTO_FLAG_IR_EXT) {
		return WIIPROTO_REQ_DRM_KAI;
	} else if (ir == WIIPROTO_FLAG_IR_FULL) {
		return WIIPROTO_REQ_DRM_SKAI1;
	} else {
		if (wdata->state.flags & WIIPROTO_FLAG_ACCEL) {
			if (ext)
				return WIIPROTO_REQ_DRM_KAE;
			else
				return WIIPROTO_REQ_DRM_KA;
		} else {
			if (ext)
				return WIIPROTO_REQ_DRM_KEE;
			else
				return WIIPROTO_REQ_DRM_K;
		}
	}
}

void wiiproto_req_drm(struct wiimote_data *wdata, __u8 drm)
{
	__u8 cmd[3];

	if (drm == WIIPROTO_REQ_NULL)
		drm = select_drm(wdata);

	cmd[0] = WIIPROTO_REQ_DRM;
	cmd[1] = 0;
	cmd[2] = drm;

	wdata->state.drm = drm;
	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

void wiiproto_req_status(struct wiimote_data *wdata)
{
	__u8 cmd[2];

	cmd[0] = WIIPROTO_REQ_SREQ;
	cmd[1] = 0;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

void wiiproto_req_accel(struct wiimote_data *wdata, __u8 accel)
{
	accel = !!accel;
	if (accel == !!(wdata->state.flags & WIIPROTO_FLAG_ACCEL))
		return;

	if (accel)
		wdata->state.flags |= WIIPROTO_FLAG_ACCEL;
	else
		wdata->state.flags &= ~WIIPROTO_FLAG_ACCEL;

	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
}

void wiiproto_req_ir1(struct wiimote_data *wdata, __u8 flags)
{
	__u8 cmd[2];

	cmd[0] = WIIPROTO_REQ_IR1;
	cmd[1] = flags;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

void wiiproto_req_ir2(struct wiimote_data *wdata, __u8 flags)
{
	__u8 cmd[2];

	cmd[0] = WIIPROTO_REQ_IR2;
	cmd[1] = flags;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

#define wiiproto_req_wreg(wdata, os, buf, sz) \
			wiiproto_req_wmem((wdata), false, (os), (buf), (sz))

#define wiiproto_req_weeprom(wdata, os, buf, sz) \
			wiiproto_req_wmem((wdata), true, (os), (buf), (sz))

static void wiiproto_req_wmem(struct wiimote_data *wdata, bool eeprom,
				__u32 offset, const __u8 *buf, __u8 size)
{
	__u8 cmd[22];

	if (size > 16 || size == 0) {
		hid_warn(wdata->hdev, "Invalid length %d wmem request\n", size);
		return;
	}

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = WIIPROTO_REQ_WMEM;
	cmd[2] = (offset >> 16) & 0xff;
	cmd[3] = (offset >> 8) & 0xff;
	cmd[4] = offset & 0xff;
	cmd[5] = size;
	memcpy(&cmd[6], buf, size);

	if (!eeprom)
		cmd[1] |= 0x04;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

void wiiproto_req_rmem(struct wiimote_data *wdata, bool eeprom, __u32 offset,
								__u16 size)
{
	__u8 cmd[7];

	if (size == 0) {
		hid_warn(wdata->hdev, "Invalid length %d rmem request\n", size);
		return;
	}

	cmd[0] = WIIPROTO_REQ_RMEM;
	cmd[1] = 0;
	cmd[2] = (offset >> 16) & 0xff;
	cmd[3] = (offset >> 8) & 0xff;
	cmd[4] = offset & 0xff;
	cmd[5] = (size >> 8) & 0xff;
	cmd[6] = size & 0xff;

	if (!eeprom)
		cmd[1] |= 0x04;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

/* requries the cmd-mutex to be held */
int wiimote_cmd_write(struct wiimote_data *wdata, __u32 offset,
						const __u8 *wmem, __u8 size)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wiimote_cmd_set(wdata, WIIPROTO_REQ_WMEM, 0);
	wiiproto_req_wreg(wdata, offset, wmem, size);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	ret = wiimote_cmd_wait(wdata);
	if (!ret && wdata->state.cmd_err)
		ret = -EIO;

	return ret;
}

/* requries the cmd-mutex to be held */
ssize_t wiimote_cmd_read(struct wiimote_data *wdata, __u32 offset, __u8 *rmem,
								__u8 size)
{
	unsigned long flags;
	ssize_t ret;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.cmd_read_size = size;
	wdata->state.cmd_read_buf = rmem;
	wiimote_cmd_set(wdata, WIIPROTO_REQ_RMEM, offset & 0xffff);
	wiiproto_req_rreg(wdata, offset, size);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	ret = wiimote_cmd_wait(wdata);

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.cmd_read_buf = NULL;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	if (!ret) {
		if (wdata->state.cmd_read_size == 0)
			ret = -EIO;
		else
			ret = wdata->state.cmd_read_size;
	}

	return ret;
}

/* requires the cmd-mutex to be held */
static int wiimote_cmd_init_ext(struct wiimote_data *wdata)
{
	__u8 wmem;
	int ret;

	/* initialize extension */
	wmem = 0x55;
	ret = wiimote_cmd_write(wdata, 0xa400f0, &wmem, sizeof(wmem));
	if (ret)
		return ret;

	/* disable default encryption */
	wmem = 0x0;
	ret = wiimote_cmd_write(wdata, 0xa400fb, &wmem, sizeof(wmem));
	if (ret)
		return ret;

	return 0;
}

/* requires the cmd-mutex to be held */
static __u8 wiimote_cmd_read_ext(struct wiimote_data *wdata, __u8 *rmem)
{
	int ret;

	/* read extension ID */
	ret = wiimote_cmd_read(wdata, 0xa400fa, rmem, 6);
	if (ret != 6)
		return WIIMOTE_EXT_NONE;

	hid_dbg(wdata->hdev, "extension ID: %02x:%02x %02x:%02x %02x:%02x\n",
		rmem[0], rmem[1], rmem[2], rmem[3], rmem[4], rmem[5]);

	if (rmem[0] == 0xff && rmem[1] == 0xff && rmem[2] == 0xff &&
	    rmem[3] == 0xff && rmem[4] == 0xff && rmem[5] == 0xff)
		return WIIMOTE_EXT_NONE;

	if (rmem[4] == 0x04 && rmem[5] == 0x02)
		return WIIMOTE_EXT_BALANCE_BOARD;

	return WIIMOTE_EXT_UNKNOWN;
}

/* requires the cmd-mutex to be held */
static int wiimote_cmd_init_mp(struct wiimote_data *wdata)
{
	__u8 wmem;
	int ret;

	/* initialize MP */
	wmem = 0x55;
	ret = wiimote_cmd_write(wdata, 0xa600f0, &wmem, sizeof(wmem));
	if (ret)
		return ret;

	/* disable default encryption */
	wmem = 0x0;
	ret = wiimote_cmd_write(wdata, 0xa600fb, &wmem, sizeof(wmem));
	if (ret)
		return ret;

	return 0;
}

/* requires the cmd-mutex to be held */
static bool wiimote_cmd_map_mp(struct wiimote_data *wdata, __u8 exttype)
{
	__u8 wmem;

	/* map MP with correct pass-through mode */
	switch (exttype) {
	default:
		wmem = 0x04;
		break;
	}

	return wiimote_cmd_write(wdata, 0xa600fe, &wmem, sizeof(wmem));
}

/* requires the cmd-mutex to be held */
static bool wiimote_cmd_read_mp(struct wiimote_data *wdata, __u8 *rmem)
{
	int ret;

	/* read motion plus ID */
	ret = wiimote_cmd_read(wdata, 0xa600fa, rmem, 6);
	if (ret != 6)
		return false;

	hid_dbg(wdata->hdev, "motion plus ID: %02x:%02x %02x:%02x %02x:%02x\n",
		rmem[0], rmem[1], rmem[2], rmem[3], rmem[4], rmem[5]);

	if (rmem[5] == 0x05)
		return true;

	hid_info(wdata->hdev, "unknown motion plus ID: %02x:%02x %02x:%02x %02x:%02x\n",
		 rmem[0], rmem[1], rmem[2], rmem[3], rmem[4], rmem[5]);

	return false;
}

/* requires the cmd-mutex to be held */
static __u8 wiimote_cmd_read_mp_mapped(struct wiimote_data *wdata)
{
	int ret;
	__u8 rmem[6];

	/* read motion plus ID */
	ret = wiimote_cmd_read(wdata, 0xa400fa, rmem, 6);
	if (ret != 6)
		return WIIMOTE_MP_NONE;

	hid_dbg(wdata->hdev, "mapped motion plus ID: %02x:%02x %02x:%02x %02x:%02x\n",
		rmem[0], rmem[1], rmem[2], rmem[3], rmem[4], rmem[5]);

	if (rmem[0] == 0xff && rmem[1] == 0xff && rmem[2] == 0xff &&
	    rmem[3] == 0xff && rmem[4] == 0xff && rmem[5] == 0xff)
		return WIIMOTE_MP_NONE;

	if (rmem[4] == 0x04 && rmem[5] == 0x05)
		return WIIMOTE_MP_SINGLE;
	else if (rmem[4] == 0x05 && rmem[5] == 0x05)
		return WIIMOTE_MP_PASSTHROUGH_NUNCHUK;
	else if (rmem[4] == 0x07 && rmem[5] == 0x05)
		return WIIMOTE_MP_PASSTHROUGH_CLASSIC;

	return WIIMOTE_MP_UNKNOWN;
}

/* device module handling */

static const __u8 * const wiimote_devtype_mods[WIIMOTE_DEV_NUM] = {
	[WIIMOTE_DEV_PENDING] = (const __u8[]){
		WIIMOD_NULL,
	},
	[WIIMOTE_DEV_UNKNOWN] = (const __u8[]){
		WIIMOD_NULL,
	},
	[WIIMOTE_DEV_GENERIC] = (const __u8[]){
		WIIMOD_KEYS,
		WIIMOD_RUMBLE,
		WIIMOD_BATTERY,
		WIIMOD_LED1,
		WIIMOD_LED2,
		WIIMOD_LED3,
		WIIMOD_LED4,
		WIIMOD_ACCEL,
		WIIMOD_IR,
		WIIMOD_NULL,
	},
	[WIIMOTE_DEV_GEN10] = (const __u8[]){
		WIIMOD_KEYS,
		WIIMOD_RUMBLE,
		WIIMOD_BATTERY,
		WIIMOD_LED1,
		WIIMOD_LED2,
		WIIMOD_LED3,
		WIIMOD_LED4,
		WIIMOD_ACCEL,
		WIIMOD_IR,
		WIIMOD_NULL,
	},
	[WIIMOTE_DEV_GEN20] = (const __u8[]){
		WIIMOD_KEYS,
		WIIMOD_RUMBLE,
		WIIMOD_BATTERY,
		WIIMOD_LED1,
		WIIMOD_LED2,
		WIIMOD_LED3,
		WIIMOD_LED4,
		WIIMOD_ACCEL,
		WIIMOD_IR,
		WIIMOD_NULL,
	},
	[WIIMOTE_DEV_BALANCE_BOARD] = (const __u8[]) {
		WIIMOD_BATTERY,
		WIIMOD_LED1,
		WIIMOD_NULL,
	},
};

static void wiimote_modules_load(struct wiimote_data *wdata,
				 unsigned int devtype)
{
	bool need_input = false;
	const __u8 *mods, *iter;
	const struct wiimod_ops *ops;
	int ret;

	mods = wiimote_devtype_mods[devtype];

	for (iter = mods; *iter != WIIMOD_NULL; ++iter) {
		if (wiimod_table[*iter]->flags & WIIMOD_FLAG_INPUT) {
			need_input = true;
			break;
		}
	}

	if (need_input) {
		wdata->input = input_allocate_device();
		if (!wdata->input)
			return;

		input_set_drvdata(wdata->input, wdata);
		wdata->input->dev.parent = &wdata->hdev->dev;
		wdata->input->id.bustype = wdata->hdev->bus;
		wdata->input->id.vendor = wdata->hdev->vendor;
		wdata->input->id.product = wdata->hdev->product;
		wdata->input->id.version = wdata->hdev->version;
		wdata->input->name = WIIMOTE_NAME;
	}

	for (iter = mods; *iter != WIIMOD_NULL; ++iter) {
		ops = wiimod_table[*iter];
		if (!ops->probe)
			continue;

		ret = ops->probe(ops, wdata);
		if (ret)
			goto error;
	}

	if (wdata->input) {
		ret = input_register_device(wdata->input);
		if (ret)
			goto error;
	}

	spin_lock_irq(&wdata->state.lock);
	wdata->state.devtype = devtype;
	spin_unlock_irq(&wdata->state.lock);
	return;

error:
	for ( ; iter-- != mods; ) {
		ops = wiimod_table[*iter];
		if (ops->remove)
			ops->remove(ops, wdata);
	}

	if (wdata->input) {
		input_free_device(wdata->input);
		wdata->input = NULL;
	}
}

static void wiimote_modules_unload(struct wiimote_data *wdata)
{
	const __u8 *mods, *iter;
	const struct wiimod_ops *ops;
	unsigned long flags;

	mods = wiimote_devtype_mods[wdata->state.devtype];

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.devtype = WIIMOTE_DEV_UNKNOWN;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	/* find end of list */
	for (iter = mods; *iter != WIIMOD_NULL; ++iter)
		/* empty */ ;

	if (wdata->input) {
		input_get_device(wdata->input);
		input_unregister_device(wdata->input);
	}

	for ( ; iter-- != mods; ) {
		ops = wiimod_table[*iter];
		if (ops->remove)
			ops->remove(ops, wdata);
	}

	if (wdata->input) {
		input_put_device(wdata->input);
		wdata->input = NULL;
	}
}

/* device extension handling */

static void wiimote_ext_load(struct wiimote_data *wdata, unsigned int ext)
{
	unsigned long flags;
	const struct wiimod_ops *ops;
	int ret;

	ops = wiimod_ext_table[ext];

	if (ops->probe) {
		ret = ops->probe(ops, wdata);
		if (ret)
			ext = WIIMOTE_EXT_UNKNOWN;
	}

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.exttype = ext;
	spin_unlock_irqrestore(&wdata->state.lock, flags);
}

static void wiimote_ext_unload(struct wiimote_data *wdata)
{
	unsigned long flags;
	const struct wiimod_ops *ops;

	ops = wiimod_ext_table[wdata->state.exttype];

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.exttype = WIIMOTE_EXT_UNKNOWN;
	wdata->state.flags &= ~WIIPROTO_FLAG_EXT_USED;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	if (ops->remove)
		ops->remove(ops, wdata);
}

static void wiimote_mp_load(struct wiimote_data *wdata)
{
	unsigned long flags;
	const struct wiimod_ops *ops;
	int ret;
	__u8 mode = 2;

	ops = &wiimod_mp;
	if (ops->probe) {
		ret = ops->probe(ops, wdata);
		if (ret)
			mode = 1;
	}

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.mp = mode;
	spin_unlock_irqrestore(&wdata->state.lock, flags);
}

static void wiimote_mp_unload(struct wiimote_data *wdata)
{
	unsigned long flags;
	const struct wiimod_ops *ops;

	if (wdata->state.mp < 2)
		return;

	ops = &wiimod_mp;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.mp = 0;
	wdata->state.flags &= ~WIIPROTO_FLAG_MP_USED;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	if (ops->remove)
		ops->remove(ops, wdata);
}

/* device (re-)initialization and detection */

static const char *wiimote_devtype_names[WIIMOTE_DEV_NUM] = {
	[WIIMOTE_DEV_PENDING] = "Pending",
	[WIIMOTE_DEV_UNKNOWN] = "Unknown",
	[WIIMOTE_DEV_GENERIC] = "Generic",
	[WIIMOTE_DEV_GEN10] = "Nintendo Wii Remote (Gen 1)",
	[WIIMOTE_DEV_GEN20] = "Nintendo Wii Remote Plus (Gen 2)",
	[WIIMOTE_DEV_BALANCE_BOARD] = "Nintendo Wii Balance Board",
};

/* Try to guess the device type based on all collected information. We
 * first try to detect by static extension types, then VID/PID and the
 * device name. If we cannot detect the device, we use
 * WIIMOTE_DEV_GENERIC so all modules will get probed on the device. */
static void wiimote_init_set_type(struct wiimote_data *wdata,
				  __u8 exttype)
{
	__u8 devtype = WIIMOTE_DEV_GENERIC;
	__u16 vendor, product;
	const char *name;

	vendor = wdata->hdev->vendor;
	product = wdata->hdev->product;
	name = wdata->hdev->name;

	if (exttype == WIIMOTE_EXT_BALANCE_BOARD) {
		devtype = WIIMOTE_DEV_BALANCE_BOARD;
		goto done;
	}

	if (!strcmp(name, "Nintendo RVL-CNT-01")) {
		devtype = WIIMOTE_DEV_GEN10;
		goto done;
	} else if (!strcmp(name, "Nintendo RVL-CNT-01-TR")) {
		devtype = WIIMOTE_DEV_GEN20;
		goto done;
	} else if (!strcmp(name, "Nintendo RVL-WBC-01")) {
		devtype = WIIMOTE_DEV_BALANCE_BOARD;
		goto done;
	}

	if (vendor == USB_VENDOR_ID_NINTENDO) {
		if (product == USB_DEVICE_ID_NINTENDO_WIIMOTE) {
			devtype = WIIMOTE_DEV_GEN10;
			goto done;
		} else if (product == USB_DEVICE_ID_NINTENDO_WIIMOTE2) {
			devtype = WIIMOTE_DEV_GEN20;
			goto done;
		}
	}

done:
	if (devtype == WIIMOTE_DEV_GENERIC)
		hid_info(wdata->hdev, "cannot detect device; NAME: %s VID: %04x PID: %04x EXT: %04x\n",
			name, vendor, product, exttype);
	else
		hid_info(wdata->hdev, "detected device: %s\n",
			 wiimote_devtype_names[devtype]);

	wiimote_modules_load(wdata, devtype);
}

static void wiimote_init_detect(struct wiimote_data *wdata)
{
	__u8 exttype = WIIMOTE_EXT_NONE, extdata[6];
	bool ext;
	int ret;

	wiimote_cmd_acquire_noint(wdata);

	spin_lock_irq(&wdata->state.lock);
	wdata->state.devtype = WIIMOTE_DEV_UNKNOWN;
	wiimote_cmd_set(wdata, WIIPROTO_REQ_SREQ, 0);
	wiiproto_req_status(wdata);
	spin_unlock_irq(&wdata->state.lock);

	ret = wiimote_cmd_wait_noint(wdata);
	if (ret)
		goto out_release;

	spin_lock_irq(&wdata->state.lock);
	ext = wdata->state.flags & WIIPROTO_FLAG_EXT_PLUGGED;
	spin_unlock_irq(&wdata->state.lock);

	if (!ext)
		goto out_release;

	wiimote_cmd_init_ext(wdata);
	exttype = wiimote_cmd_read_ext(wdata, extdata);

out_release:
	wiimote_cmd_release(wdata);
	wiimote_init_set_type(wdata, exttype);
	/* schedule MP timer */
	mod_timer(&wdata->timer, jiffies + HZ * 4);
}

/*
 * MP hotplug events are not generated by the wiimote. Therefore, we need
 * polling to detect it. We use a 4s interval for polling MP registers. This
 * seems reasonable considering applications can trigger it manually via
 * sysfs requests.
 */
static void wiimote_init_poll_mp(struct wiimote_data *wdata)
{
	bool mp;
	__u8 mpdata[6];

	wiimote_cmd_acquire_noint(wdata);
	wiimote_cmd_init_mp(wdata);
	mp = wiimote_cmd_read_mp(wdata, mpdata);
	wiimote_cmd_release(wdata);

	/* load/unload MP module if it changed */
	if (mp) {
		if (!wdata->state.mp) {
			hid_info(wdata->hdev, "detected extension: Nintendo Wii Motion Plus\n");
			wiimote_mp_load(wdata);
		}
	} else if (wdata->state.mp) {
		wiimote_mp_unload(wdata);
	}

	mod_timer(&wdata->timer, jiffies + HZ * 4);
}

/*
 * Check whether the wiimote is in the expected state. The extension registers
 * may change during hotplug and initialization so we might get hotplug events
 * that we caused by remapping some memory.
 * We use some heuristics here to check known states. If the wiimote is in the
 * expected state, we can ignore the hotplug event.
 *
 * Returns "true" if the device is in expected state, "false" if we should
 * redo hotplug handling and extension initialization.
 */
static bool wiimote_init_check(struct wiimote_data *wdata)
{
	__u32 flags;
	__u8 type, data[6];
	bool ret, poll_mp;

	spin_lock_irq(&wdata->state.lock);
	flags = wdata->state.flags;
	spin_unlock_irq(&wdata->state.lock);

	wiimote_cmd_acquire_noint(wdata);

	/* If MP is used and active, but the extension is not, we expect:
	 *   read_mp_mapped() == WIIMOTE_MP_SINGLE
	 *   state.flags == !EXT_ACTIVE && !MP_PLUGGED && MP_ACTIVE
	 * We do not check EXT_PLUGGED because it might change during
	 * initialization of MP without extensions.
	 *  - If MP is unplugged/replugged, read_mp_mapped() fails
	 *  - If EXT is plugged, MP_PLUGGED will get set */
	if (wdata->state.exttype == WIIMOTE_EXT_NONE &&
	    wdata->state.mp > 0 && (flags & WIIPROTO_FLAG_MP_USED)) {
		type = wiimote_cmd_read_mp_mapped(wdata);
		ret = type == WIIMOTE_MP_SINGLE;

		spin_lock_irq(&wdata->state.lock);
		ret = ret && !(wdata->state.flags & WIIPROTO_FLAG_EXT_ACTIVE);
		ret = ret && !(wdata->state.flags & WIIPROTO_FLAG_MP_PLUGGED);
		ret = ret && (wdata->state.flags & WIIPROTO_FLAG_MP_ACTIVE);
		spin_unlock_irq(&wdata->state.lock);

		if (!ret)
			hid_dbg(wdata->hdev, "state left: !EXT && MP\n");

		/* while MP is mapped, we get EXT_PLUGGED events */
		poll_mp = false;

		goto out_release;
	}

	/* If MP is unused, but the extension port is used, we expect:
	 *   read_ext == state.exttype
	 *   state.flags == !MP_ACTIVE && EXT_ACTIVE
	 * - If MP is plugged/unplugged, our timer detects it
	 * - If EXT is unplugged/replugged, EXT_ACTIVE will become unset */
	if (!(flags & WIIPROTO_FLAG_MP_USED) &&
	    wdata->state.exttype != WIIMOTE_EXT_NONE) {
		type = wiimote_cmd_read_ext(wdata, data);
		ret = type == wdata->state.exttype;

		spin_lock_irq(&wdata->state.lock);
		ret = ret && !(wdata->state.flags & WIIPROTO_FLAG_MP_ACTIVE);
		ret = ret && (wdata->state.flags & WIIPROTO_FLAG_EXT_ACTIVE);
		spin_unlock_irq(&wdata->state.lock);

		if (!ret)
			hid_dbg(wdata->hdev, "state left: EXT && !MP\n");

		/* poll MP for hotplug events */
		poll_mp = true;

		goto out_release;
	}

	/* If neither MP nor an extension are used, we expect:
	 *   read_ext() == WIIMOTE_EXT_NONE
	 *   state.flags == !MP_ACTIVE && !EXT_ACTIVE && !EXT_PLUGGED
	 * No need to perform any action in this case as everything is
	 * disabled already.
	 * - If MP is plugged/unplugged, our timer detects it
	 * - If EXT is plugged, EXT_PLUGGED will be set */
	if (!(flags & WIIPROTO_FLAG_MP_USED) &&
	    wdata->state.exttype == WIIMOTE_EXT_NONE) {
		type = wiimote_cmd_read_ext(wdata, data);
		ret = type == wdata->state.exttype;

		spin_lock_irq(&wdata->state.lock);
		ret = ret && !(wdata->state.flags & WIIPROTO_FLAG_EXT_ACTIVE);
		ret = ret && !(wdata->state.flags & WIIPROTO_FLAG_MP_ACTIVE);
		ret = ret && !(wdata->state.flags & WIIPROTO_FLAG_EXT_PLUGGED);
		spin_unlock_irq(&wdata->state.lock);

		if (!ret)
			hid_dbg(wdata->hdev, "state left: !EXT && !MP\n");

		/* poll MP for hotplug events */
		poll_mp = true;

		goto out_release;
	}

	/* The trickiest part is if both EXT and MP are active. We cannot read
	 * the EXT ID, anymore, because MP is mapped over it. However, we use
	 * a handy trick here:
	 *   - EXT_ACTIVE is unset whenever !MP_PLUGGED is sent
	 * MP_PLUGGED might be re-sent again before we are scheduled, but
	 * EXT_ACTIVE will stay unset.
	 * So it is enough to check for mp_mapped() and MP_ACTIVE and
	 * EXT_ACTIVE. EXT_PLUGGED is a sanity check. */
	if (wdata->state.exttype != WIIMOTE_EXT_NONE &&
	    wdata->state.mp > 0 && (flags & WIIPROTO_FLAG_MP_USED)) {
		type = wiimote_cmd_read_mp_mapped(wdata);
		ret = type != WIIMOTE_MP_NONE;
		ret = ret && type != WIIMOTE_MP_UNKNOWN;
		ret = ret && type != WIIMOTE_MP_SINGLE;

		spin_lock_irq(&wdata->state.lock);
		ret = ret && (wdata->state.flags & WIIPROTO_FLAG_EXT_PLUGGED);
		ret = ret && (wdata->state.flags & WIIPROTO_FLAG_EXT_ACTIVE);
		ret = ret && (wdata->state.flags & WIIPROTO_FLAG_MP_ACTIVE);
		spin_unlock_irq(&wdata->state.lock);

		if (!ret)
			hid_dbg(wdata->hdev, "state left: EXT && MP\n");

		/* while MP is mapped, we get EXT_PLUGGED events */
		poll_mp = false;

		goto out_release;
	}

	/* unknown state */
	ret = false;

out_release:
	wiimote_cmd_release(wdata);

	/* only poll for MP if requested and if state didn't change */
	if (ret && poll_mp)
		wiimote_init_poll_mp(wdata);

	return ret;
}

static const char *wiimote_exttype_names[WIIMOTE_EXT_NUM] = {
	[WIIMOTE_EXT_NONE] = "None",
	[WIIMOTE_EXT_UNKNOWN] = "Unknown",
	[WIIMOTE_EXT_BALANCE_BOARD] = "Nintendo Wii Balance Board",
};

/*
 * Handle hotplug events
 * If we receive an hotplug event and the device-check failed, we deinitialize
 * the extension ports, re-read all extension IDs and set the device into
 * the desired state. This involves mapping MP into the main extension
 * registers, setting up extension passthrough modes and initializing the
 * requested extensions.
 */
static void wiimote_init_hotplug(struct wiimote_data *wdata)
{
	__u8 exttype, extdata[6], mpdata[6];
	__u32 flags;
	bool mp;

	hid_dbg(wdata->hdev, "detect extensions..\n");

	wiimote_cmd_acquire_noint(wdata);

	spin_lock_irq(&wdata->state.lock);

	/* get state snapshot that we will then work on */
	flags = wdata->state.flags;

	/* disable event forwarding temporarily */
	wdata->state.flags &= ~WIIPROTO_FLAG_EXT_ACTIVE;
	wdata->state.flags &= ~WIIPROTO_FLAG_MP_ACTIVE;

	spin_unlock_irq(&wdata->state.lock);

	/* init extension and MP (deactivates current extension or MP) */
	wiimote_cmd_init_ext(wdata);
	wiimote_cmd_init_mp(wdata);
	mp = wiimote_cmd_read_mp(wdata, mpdata);
	exttype = wiimote_cmd_read_ext(wdata, extdata);

	wiimote_cmd_release(wdata);

	/* load/unload extension module if it changed */
	if (exttype != wdata->state.exttype) {
		/* unload previous extension */
		wiimote_ext_unload(wdata);

		if (exttype == WIIMOTE_EXT_UNKNOWN) {
			hid_info(wdata->hdev, "cannot detect extension; %02x:%02x %02x:%02x %02x:%02x\n",
				 extdata[0], extdata[1], extdata[2],
				 extdata[3], extdata[4], extdata[5]);
		} else if (exttype == WIIMOTE_EXT_NONE) {
			spin_lock_irq(&wdata->state.lock);
			wdata->state.exttype = WIIMOTE_EXT_NONE;
			spin_unlock_irq(&wdata->state.lock);
		} else {
			hid_info(wdata->hdev, "detected extension: %s\n",
				 wiimote_exttype_names[exttype]);
			/* try loading new extension */
			wiimote_ext_load(wdata, exttype);
		}
	}

	/* load/unload MP module if it changed */
	if (mp) {
		if (!wdata->state.mp) {
			hid_info(wdata->hdev, "detected extension: Nintendo Wii Motion Plus\n");
			wiimote_mp_load(wdata);
		}
	} else if (wdata->state.mp) {
		wiimote_mp_unload(wdata);
	}

	/* if MP is not used, do not map or activate it */
	if (!(flags & WIIPROTO_FLAG_MP_USED))
		mp = false;

	/* map MP into main extension registers if used */
	if (mp) {
		wiimote_cmd_acquire_noint(wdata);
		wiimote_cmd_map_mp(wdata, exttype);
		wiimote_cmd_release(wdata);

		/* delete MP hotplug timer */
		del_timer_sync(&wdata->timer);
	} else {
		/* reschedule MP hotplug timer */
		mod_timer(&wdata->timer, jiffies + HZ * 4);
	}

	spin_lock_irq(&wdata->state.lock);

	/* enable data forwarding again and set expected hotplug state */
	if (mp) {
		wdata->state.flags |= WIIPROTO_FLAG_MP_ACTIVE;
		if (wdata->state.exttype == WIIMOTE_EXT_NONE) {
			wdata->state.flags &= ~WIIPROTO_FLAG_EXT_PLUGGED;
			wdata->state.flags &= ~WIIPROTO_FLAG_MP_PLUGGED;
		} else {
			wdata->state.flags &= ~WIIPROTO_FLAG_EXT_PLUGGED;
			wdata->state.flags |= WIIPROTO_FLAG_MP_PLUGGED;
			wdata->state.flags |= WIIPROTO_FLAG_EXT_ACTIVE;
		}
	} else if (wdata->state.exttype != WIIMOTE_EXT_NONE) {
		wdata->state.flags |= WIIPROTO_FLAG_EXT_ACTIVE;
	}

	/* request status report for hotplug state updates */
	wiiproto_req_status(wdata);

	spin_unlock_irq(&wdata->state.lock);

	hid_dbg(wdata->hdev, "detected extensions: MP: %d EXT: %d\n",
		wdata->state.mp, wdata->state.exttype);
}

static void wiimote_init_worker(struct work_struct *work)
{
	struct wiimote_data *wdata = container_of(work, struct wiimote_data,
						  init_worker);

	if (wdata->state.devtype == WIIMOTE_DEV_PENDING)
		wiimote_init_detect(wdata);
	if (!wiimote_init_check(wdata))
		wiimote_init_hotplug(wdata);
}

void __wiimote_schedule(struct wiimote_data *wdata)
{
	if (!(wdata->state.flags & WIIPROTO_FLAG_EXITING))
		schedule_work(&wdata->init_worker);
}

static void wiimote_schedule(struct wiimote_data *wdata)
{
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	__wiimote_schedule(wdata);
	spin_unlock_irqrestore(&wdata->state.lock, flags);
}

static void wiimote_init_timeout(unsigned long arg)
{
	struct wiimote_data *wdata = (void*)arg;

	wiimote_schedule(wdata);
}

/* protocol handlers */

static void handler_keys(struct wiimote_data *wdata, const __u8 *payload)
{
	const __u8 *iter, *mods;
	const struct wiimod_ops *ops;

	ops = wiimod_ext_table[wdata->state.exttype];
	if (ops->in_keys) {
		ops->in_keys(wdata, payload);
		return;
	}

	mods = wiimote_devtype_mods[wdata->state.devtype];
	for (iter = mods; *iter != WIIMOD_NULL; ++iter) {
		ops = wiimod_table[*iter];
		if (ops->in_keys) {
			ops->in_keys(wdata, payload);
			break;
		}
	}
}

static void handler_accel(struct wiimote_data *wdata, const __u8 *payload)
{
	const __u8 *iter, *mods;
	const struct wiimod_ops *ops;

	ops = wiimod_ext_table[wdata->state.exttype];
	if (ops->in_accel) {
		ops->in_accel(wdata, payload);
		return;
	}

	mods = wiimote_devtype_mods[wdata->state.devtype];
	for (iter = mods; *iter != WIIMOD_NULL; ++iter) {
		ops = wiimod_table[*iter];
		if (ops->in_accel) {
			ops->in_accel(wdata, payload);
			break;
		}
	}
}

static bool valid_ext_handler(const struct wiimod_ops *ops, size_t len)
{
	if (!ops->in_ext)
		return false;
	if ((ops->flags & WIIMOD_FLAG_EXT8) && len < 8)
		return false;
	if ((ops->flags & WIIMOD_FLAG_EXT16) && len < 16)
		return false;

	return true;
}

static void handler_ext(struct wiimote_data *wdata, const __u8 *payload,
			size_t len)
{
	const __u8 *iter, *mods;
	const struct wiimod_ops *ops;
	bool is_mp;

	if (len < 6)
		return;

	/* if MP is active, track MP slot hotplugging */
	if (wdata->state.flags & WIIPROTO_FLAG_MP_ACTIVE) {
		/* this bit is set for invalid events (eg. during hotplug) */
		if (payload[5] & 0x01)
			return;

		if (payload[4] & 0x01) {
			if (!(wdata->state.flags & WIIPROTO_FLAG_MP_PLUGGED)) {
				hid_dbg(wdata->hdev, "MP hotplug: 1\n");
				wdata->state.flags |= WIIPROTO_FLAG_MP_PLUGGED;
				__wiimote_schedule(wdata);
			}
		} else {
			if (wdata->state.flags & WIIPROTO_FLAG_MP_PLUGGED) {
				hid_dbg(wdata->hdev, "MP hotplug: 0\n");
				wdata->state.flags &= ~WIIPROTO_FLAG_MP_PLUGGED;
				wdata->state.flags &= ~WIIPROTO_FLAG_EXT_ACTIVE;
				__wiimote_schedule(wdata);
			}
		}

		/* detect MP data that is sent interleaved with EXT data */
		is_mp = payload[5] & 0x02;
	} else {
		is_mp = false;
	}

	/* ignore EXT events if no extension is active */
	if (!(wdata->state.flags & WIIPROTO_FLAG_EXT_ACTIVE) && !is_mp)
		return;

	/* try forwarding to extension handler, first */
	ops = wiimod_ext_table[wdata->state.exttype];
	if (is_mp && ops->in_mp) {
		ops->in_mp(wdata, payload);
		return;
	} else if (!is_mp && valid_ext_handler(ops, len)) {
		ops->in_ext(wdata, payload);
		return;
	}

	/* try forwarding to MP handler */
	ops = &wiimod_mp;
	if (is_mp && ops->in_mp) {
		ops->in_mp(wdata, payload);
		return;
	} else if (!is_mp && valid_ext_handler(ops, len)) {
		ops->in_ext(wdata, payload);
		return;
	}

	/* try forwarding to loaded modules */
	mods = wiimote_devtype_mods[wdata->state.devtype];
	for (iter = mods; *iter != WIIMOD_NULL; ++iter) {
		ops = wiimod_table[*iter];
		if (is_mp && ops->in_mp) {
			ops->in_mp(wdata, payload);
			return;
		} else if (!is_mp && valid_ext_handler(ops, len)) {
			ops->in_ext(wdata, payload);
			return;
		}
	}
}

#define ir_to_input0(wdata, ir, packed) handler_ir((wdata), (ir), (packed), 0)
#define ir_to_input1(wdata, ir, packed) handler_ir((wdata), (ir), (packed), 1)
#define ir_to_input2(wdata, ir, packed) handler_ir((wdata), (ir), (packed), 2)
#define ir_to_input3(wdata, ir, packed) handler_ir((wdata), (ir), (packed), 3)

static void handler_ir(struct wiimote_data *wdata, const __u8 *payload,
		       bool packed, unsigned int id)
{
	const __u8 *iter, *mods;
	const struct wiimod_ops *ops;

	ops = wiimod_ext_table[wdata->state.exttype];
	if (ops->in_ir) {
		ops->in_ir(wdata, payload, packed, id);
		return;
	}

	mods = wiimote_devtype_mods[wdata->state.devtype];
	for (iter = mods; *iter != WIIMOD_NULL; ++iter) {
		ops = wiimod_table[*iter];
		if (ops->in_ir) {
			ops->in_ir(wdata, payload, packed, id);
			break;
		}
	}
}

/* reduced status report with "BB BB" key data only */
static void handler_status_K(struct wiimote_data *wdata,
			     const __u8 *payload)
{
	handler_keys(wdata, payload);

	/* on status reports the drm is reset so we need to resend the drm */
	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
}

/* extended status report with "BB BB LF 00 00 VV" data */
static void handler_status(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_status_K(wdata, payload);

	/* update extension status */
	if (payload[2] & 0x02) {
		if (!(wdata->state.flags & WIIPROTO_FLAG_EXT_PLUGGED)) {
			hid_dbg(wdata->hdev, "EXT hotplug: 1\n");
			wdata->state.flags |= WIIPROTO_FLAG_EXT_PLUGGED;
			__wiimote_schedule(wdata);
		}
	} else {
		if (wdata->state.flags & WIIPROTO_FLAG_EXT_PLUGGED) {
			hid_dbg(wdata->hdev, "EXT hotplug: 0\n");
			wdata->state.flags &= ~WIIPROTO_FLAG_EXT_PLUGGED;
			wdata->state.flags &= ~WIIPROTO_FLAG_MP_PLUGGED;
			wdata->state.flags &= ~WIIPROTO_FLAG_EXT_ACTIVE;
			wdata->state.flags &= ~WIIPROTO_FLAG_MP_ACTIVE;
			__wiimote_schedule(wdata);
		}
	}

	wdata->state.cmd_battery = payload[5];
	if (wiimote_cmd_pending(wdata, WIIPROTO_REQ_SREQ, 0))
		wiimote_cmd_complete(wdata);
}

/* reduced generic report with "BB BB" key data only */
static void handler_generic_K(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
}

static void handler_data(struct wiimote_data *wdata, const __u8 *payload)
{
	__u16 offset = payload[3] << 8 | payload[4];
	__u8 size = (payload[2] >> 4) + 1;
	__u8 err = payload[2] & 0x0f;

	handler_keys(wdata, payload);

	if (wiimote_cmd_pending(wdata, WIIPROTO_REQ_RMEM, offset)) {
		if (err)
			size = 0;
		else if (size > wdata->state.cmd_read_size)
			size = wdata->state.cmd_read_size;

		wdata->state.cmd_read_size = size;
		if (wdata->state.cmd_read_buf)
			memcpy(wdata->state.cmd_read_buf, &payload[5], size);
		wiimote_cmd_complete(wdata);
	}
}

static void handler_return(struct wiimote_data *wdata, const __u8 *payload)
{
	__u8 err = payload[3];
	__u8 cmd = payload[2];

	handler_keys(wdata, payload);

	if (wiimote_cmd_pending(wdata, cmd, 0)) {
		wdata->state.cmd_err = err;
		wiimote_cmd_complete(wdata);
	} else if (err) {
		hid_warn(wdata->hdev, "Remote error %hhu on req %hhu\n", err,
									cmd);
	}
}

static void handler_drm_KA(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	handler_accel(wdata, payload);
}

static void handler_drm_KE(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	handler_ext(wdata, &payload[2], 8);
}

static void handler_drm_KAI(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	handler_accel(wdata, payload);
	ir_to_input0(wdata, &payload[5], false);
	ir_to_input1(wdata, &payload[8], false);
	ir_to_input2(wdata, &payload[11], false);
	ir_to_input3(wdata, &payload[14], false);
}

static void handler_drm_KEE(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	handler_ext(wdata, &payload[2], 19);
}

static void handler_drm_KIE(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	ir_to_input0(wdata, &payload[2], false);
	ir_to_input1(wdata, &payload[4], true);
	ir_to_input2(wdata, &payload[7], false);
	ir_to_input3(wdata, &payload[9], true);
	handler_ext(wdata, &payload[12], 9);
}

static void handler_drm_KAE(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	handler_accel(wdata, payload);
	handler_ext(wdata, &payload[5], 16);
}

static void handler_drm_KAIE(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	handler_accel(wdata, payload);
	ir_to_input0(wdata, &payload[5], false);
	ir_to_input1(wdata, &payload[7], true);
	ir_to_input2(wdata, &payload[10], false);
	ir_to_input3(wdata, &payload[12], true);
	handler_ext(wdata, &payload[15], 6);
}

static void handler_drm_E(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_ext(wdata, payload, 21);
}

static void handler_drm_SKAI1(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);

	wdata->state.accel_split[0] = payload[2];
	wdata->state.accel_split[1] = (payload[0] >> 1) & (0x10 | 0x20);
	wdata->state.accel_split[1] |= (payload[1] << 1) & (0x40 | 0x80);

	ir_to_input0(wdata, &payload[3], false);
	ir_to_input1(wdata, &payload[12], false);
}

static void handler_drm_SKAI2(struct wiimote_data *wdata, const __u8 *payload)
{
	__u8 buf[5];

	handler_keys(wdata, payload);

	wdata->state.accel_split[1] |= (payload[0] >> 5) & (0x01 | 0x02);
	wdata->state.accel_split[1] |= (payload[1] >> 3) & (0x04 | 0x08);

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = wdata->state.accel_split[0];
	buf[3] = payload[2];
	buf[4] = wdata->state.accel_split[1];
	handler_accel(wdata, buf);

	ir_to_input2(wdata, &payload[3], false);
	ir_to_input3(wdata, &payload[12], false);
}

struct wiiproto_handler {
	__u8 id;
	size_t size;
	void (*func)(struct wiimote_data *wdata, const __u8 *payload);
};

static struct wiiproto_handler handlers[] = {
	{ .id = WIIPROTO_REQ_STATUS, .size = 6, .func = handler_status },
	{ .id = WIIPROTO_REQ_STATUS, .size = 2, .func = handler_status_K },
	{ .id = WIIPROTO_REQ_DATA, .size = 21, .func = handler_data },
	{ .id = WIIPROTO_REQ_DATA, .size = 2, .func = handler_generic_K },
	{ .id = WIIPROTO_REQ_RETURN, .size = 4, .func = handler_return },
	{ .id = WIIPROTO_REQ_RETURN, .size = 2, .func = handler_generic_K },
	{ .id = WIIPROTO_REQ_DRM_K, .size = 2, .func = handler_keys },
	{ .id = WIIPROTO_REQ_DRM_KA, .size = 5, .func = handler_drm_KA },
	{ .id = WIIPROTO_REQ_DRM_KA, .size = 2, .func = handler_generic_K },
	{ .id = WIIPROTO_REQ_DRM_KE, .size = 10, .func = handler_drm_KE },
	{ .id = WIIPROTO_REQ_DRM_KE, .size = 2, .func = handler_generic_K },
	{ .id = WIIPROTO_REQ_DRM_KAI, .size = 17, .func = handler_drm_KAI },
	{ .id = WIIPROTO_REQ_DRM_KAI, .size = 2, .func = handler_generic_K },
	{ .id = WIIPROTO_REQ_DRM_KEE, .size = 21, .func = handler_drm_KEE },
	{ .id = WIIPROTO_REQ_DRM_KEE, .size = 2, .func = handler_generic_K },
	{ .id = WIIPROTO_REQ_DRM_KAE, .size = 21, .func = handler_drm_KAE },
	{ .id = WIIPROTO_REQ_DRM_KAE, .size = 2, .func = handler_generic_K },
	{ .id = WIIPROTO_REQ_DRM_KIE, .size = 21, .func = handler_drm_KIE },
	{ .id = WIIPROTO_REQ_DRM_KIE, .size = 2, .func = handler_generic_K },
	{ .id = WIIPROTO_REQ_DRM_KAIE, .size = 21, .func = handler_drm_KAIE },
	{ .id = WIIPROTO_REQ_DRM_KAIE, .size = 2, .func = handler_generic_K },
	{ .id = WIIPROTO_REQ_DRM_E, .size = 21, .func = handler_drm_E },
	{ .id = WIIPROTO_REQ_DRM_SKAI1, .size = 21, .func = handler_drm_SKAI1 },
	{ .id = WIIPROTO_REQ_DRM_SKAI2, .size = 21, .func = handler_drm_SKAI2 },
	{ .id = 0 }
};

static int wiimote_hid_event(struct hid_device *hdev, struct hid_report *report,
							u8 *raw_data, int size)
{
	struct wiimote_data *wdata = hid_get_drvdata(hdev);
	struct wiiproto_handler *h;
	int i;
	unsigned long flags;

	if (size < 1)
		return -EINVAL;

	spin_lock_irqsave(&wdata->state.lock, flags);

	for (i = 0; handlers[i].id; ++i) {
		h = &handlers[i];
		if (h->id == raw_data[0] && h->size < size) {
			h->func(wdata, &raw_data[1]);
			break;
		}
	}

	if (!handlers[i].id)
		hid_warn(hdev, "Unhandled report %hhu size %d\n", raw_data[0],
									size);

	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

static struct wiimote_data *wiimote_create(struct hid_device *hdev)
{
	struct wiimote_data *wdata;

	wdata = kzalloc(sizeof(*wdata), GFP_KERNEL);
	if (!wdata)
		return NULL;

	wdata->hdev = hdev;
	hid_set_drvdata(hdev, wdata);

	spin_lock_init(&wdata->queue.lock);
	INIT_WORK(&wdata->queue.worker, wiimote_queue_worker);

	spin_lock_init(&wdata->state.lock);
	init_completion(&wdata->state.ready);
	mutex_init(&wdata->state.sync);
	wdata->state.drm = WIIPROTO_REQ_DRM_K;
	wdata->state.cmd_battery = 0xff;

	INIT_WORK(&wdata->init_worker, wiimote_init_worker);
	setup_timer(&wdata->timer, wiimote_init_timeout, (long)wdata);

	return wdata;
}

static void wiimote_destroy(struct wiimote_data *wdata)
{
	unsigned long flags;

	wiidebug_deinit(wdata);

	/* prevent init_worker from being scheduled again */
	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags |= WIIPROTO_FLAG_EXITING;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	cancel_work_sync(&wdata->init_worker);
	del_timer_sync(&wdata->timer);

	wiimote_mp_unload(wdata);
	wiimote_ext_unload(wdata);
	wiimote_modules_unload(wdata);
	cancel_work_sync(&wdata->queue.worker);
	hid_hw_close(wdata->hdev);
	hid_hw_stop(wdata->hdev);

	kfree(wdata);
}

static int wiimote_hid_probe(struct hid_device *hdev,
				const struct hid_device_id *id)
{
	struct wiimote_data *wdata;
	int ret;

	hdev->quirks |= HID_QUIRK_NO_INIT_REPORTS;

	wdata = wiimote_create(hdev);
	if (!wdata) {
		hid_err(hdev, "Can't alloc device\n");
		return -ENOMEM;
	}

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "HID parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "HW start failed\n");
		goto err;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "cannot start hardware I/O\n");
		goto err_stop;
	}

	ret = wiidebug_init(wdata);
	if (ret)
		goto err_free;

	hid_info(hdev, "New device registered\n");

	/* schedule device detection */
	wiimote_schedule(wdata);

	return 0;

err_free:
	wiimote_destroy(wdata);
	return ret;

err_stop:
	hid_hw_stop(hdev);
err:
	input_free_device(wdata->ir);
	input_free_device(wdata->accel);
	kfree(wdata);
	return ret;
}

static void wiimote_hid_remove(struct hid_device *hdev)
{
	struct wiimote_data *wdata = hid_get_drvdata(hdev);

	hid_info(hdev, "Device removed\n");
	wiimote_destroy(wdata);
}

static const struct hid_device_id wiimote_hid_devices[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
				USB_DEVICE_ID_NINTENDO_WIIMOTE) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
				USB_DEVICE_ID_NINTENDO_WIIMOTE2) },
	{ }
};
MODULE_DEVICE_TABLE(hid, wiimote_hid_devices);

static struct hid_driver wiimote_hid_driver = {
	.name = "wiimote",
	.id_table = wiimote_hid_devices,
	.probe = wiimote_hid_probe,
	.remove = wiimote_hid_remove,
	.raw_event = wiimote_hid_event,
};
module_hid_driver(wiimote_hid_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Herrmann <dh.herrmann@gmail.com>");
MODULE_DESCRIPTION("Driver for Nintendo Wii / Wii U peripherals");
