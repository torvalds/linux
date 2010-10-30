/*
 * Intel Wireless WiMAX Connection 2400m
 * Generic probe/disconnect, reset and message passing
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * See i2400m.h for driver documentation. This contains helpers for
 * the driver model glue [_setup()/_release()], handling device resets
 * [_dev_reset_handle()], and the backends for the WiMAX stack ops
 * reset [_op_reset()] and message from user [_op_msg_from_user()].
 *
 * ROADMAP:
 *
 * i2400m_op_msg_from_user()
 *   i2400m_msg_to_dev()
 *   wimax_msg_to_user_send()
 *
 * i2400m_op_reset()
 *   i240m->bus_reset()
 *
 * i2400m_dev_reset_handle()
 *   __i2400m_dev_reset_handle()
 *     __i2400m_dev_stop()
 *     __i2400m_dev_start()
 *
 * i2400m_setup()
 *   i2400m->bus_setup()
 *   i2400m_bootrom_init()
 *   register_netdev()
 *   wimax_dev_add()
 *   i2400m_dev_start()
 *     __i2400m_dev_start()
 *       i2400m_dev_bootstrap()
 *       i2400m_tx_setup()
 *       i2400m->bus_dev_start()
 *       i2400m_firmware_check()
 *       i2400m_check_mac_addr()
 *
 * i2400m_release()
 *   i2400m_dev_stop()
 *     __i2400m_dev_stop()
 *       i2400m_dev_shutdown()
 *       i2400m->bus_dev_stop()
 *       i2400m_tx_release()
 *   i2400m->bus_release()
 *   wimax_dev_rm()
 *   unregister_netdev()
 */
#include "i2400m.h"
#include <linux/etherdevice.h>
#include <linux/wimax/i2400m.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/suspend.h>
#include <linux/slab.h>

#define D_SUBMODULE driver
#include "debug-levels.h"


static char i2400m_debug_params[128];
module_param_string(debug, i2400m_debug_params, sizeof(i2400m_debug_params),
		    0644);
MODULE_PARM_DESC(debug,
		 "String of space-separated NAME:VALUE pairs, where NAMEs "
		 "are the different debug submodules and VALUE are the "
		 "initial debug value to set.");

static char i2400m_barkers_params[128];
module_param_string(barkers, i2400m_barkers_params,
		    sizeof(i2400m_barkers_params), 0644);
MODULE_PARM_DESC(barkers,
		 "String of comma-separated 32-bit values; each is "
		 "recognized as the value the device sends as a reboot "
		 "signal; values are appended to a list--setting one value "
		 "as zero cleans the existing list and starts a new one.");

static
struct i2400m_work *__i2400m_work_setup(
	struct i2400m *i2400m, void (*fn)(struct work_struct *),
	gfp_t gfp_flags, const void *pl, size_t pl_size)
{
	struct i2400m_work *iw;

	iw = kzalloc(sizeof(*iw) + pl_size, gfp_flags);
	if (iw == NULL)
		return NULL;
	iw->i2400m = i2400m_get(i2400m);
	iw->pl_size = pl_size;
	memcpy(iw->pl, pl, pl_size);
	INIT_WORK(&iw->ws, fn);
	return iw;
}


/*
 * Schedule i2400m's specific work on the system's queue.
 *
 * Used for a few cases where we really need it; otherwise, identical
 * to i2400m_queue_work().
 *
 * Returns < 0 errno code on error, 1 if ok.
 *
 * If it returns zero, something really bad happened, as it means the
 * works struct was already queued, but we have just allocated it, so
 * it should not happen.
 */
static int i2400m_schedule_work(struct i2400m *i2400m,
			 void (*fn)(struct work_struct *), gfp_t gfp_flags,
			 const void *pl, size_t pl_size)
{
	int result;
	struct i2400m_work *iw;

	result = -ENOMEM;
	iw = __i2400m_work_setup(i2400m, fn, gfp_flags, pl, pl_size);
	if (iw != NULL) {
		result = schedule_work(&iw->ws);
		if (WARN_ON(result == 0))
			result = -ENXIO;
	}
	return result;
}


/*
 * WiMAX stack operation: relay a message from user space
 *
 * @wimax_dev: device descriptor
 * @pipe_name: named pipe the message is for
 * @msg_buf: pointer to the message bytes
 * @msg_len: length of the buffer
 * @genl_info: passed by the generic netlink layer
 *
 * The WiMAX stack will call this function when a message was received
 * from user space.
 *
 * For the i2400m, this is an L3L4 message, as specified in
 * include/linux/wimax/i2400m.h, and thus prefixed with a 'struct
 * i2400m_l3l4_hdr'. Driver (and device) expect the messages to be
 * coded in Little Endian.
 *
 * This function just verifies that the header declaration and the
 * payload are consistent and then deals with it, either forwarding it
 * to the device or procesing it locally.
 *
 * In the i2400m, messages are basically commands that will carry an
 * ack, so we use i2400m_msg_to_dev() and then deliver the ack back to
 * user space. The rx.c code might intercept the response and use it
 * to update the driver's state, but then it will pass it on so it can
 * be relayed back to user space.
 *
 * Note that asynchronous events from the device are processed and
 * sent to user space in rx.c.
 */
static
int i2400m_op_msg_from_user(struct wimax_dev *wimax_dev,
			    const char *pipe_name,
			    const void *msg_buf, size_t msg_len,
			    const struct genl_info *genl_info)
{
	int result;
	struct i2400m *i2400m = wimax_dev_to_i2400m(wimax_dev);
	struct device *dev = i2400m_dev(i2400m);
	struct sk_buff *ack_skb;

	d_fnstart(4, dev, "(wimax_dev %p [i2400m %p] msg_buf %p "
		  "msg_len %zu genl_info %p)\n", wimax_dev, i2400m,
		  msg_buf, msg_len, genl_info);
	ack_skb = i2400m_msg_to_dev(i2400m, msg_buf, msg_len);
	result = PTR_ERR(ack_skb);
	if (IS_ERR(ack_skb))
		goto error_msg_to_dev;
	result = wimax_msg_send(&i2400m->wimax_dev, ack_skb);
error_msg_to_dev:
	d_fnend(4, dev, "(wimax_dev %p [i2400m %p] msg_buf %p msg_len %zu "
		"genl_info %p) = %d\n", wimax_dev, i2400m, msg_buf, msg_len,
		genl_info, result);
	return result;
}


/*
 * Context to wait for a reset to finalize
 */
struct i2400m_reset_ctx {
	struct completion completion;
	int result;
};


/*
 * WiMAX stack operation: reset a device
 *
 * @wimax_dev: device descriptor
 *
 * See the documentation for wimax_reset() and wimax_dev->op_reset for
 * the requirements of this function. The WiMAX stack guarantees
 * serialization on calls to this function.
 *
 * Do a warm reset on the device; if it fails, resort to a cold reset
 * and return -ENODEV. On successful warm reset, we need to block
 * until it is complete.
 *
 * The bus-driver implementation of reset takes care of falling back
 * to cold reset if warm fails.
 */
static
int i2400m_op_reset(struct wimax_dev *wimax_dev)
{
	int result;
	struct i2400m *i2400m = wimax_dev_to_i2400m(wimax_dev);
	struct device *dev = i2400m_dev(i2400m);
	struct i2400m_reset_ctx ctx = {
		.completion = COMPLETION_INITIALIZER_ONSTACK(ctx.completion),
		.result = 0,
	};

	d_fnstart(4, dev, "(wimax_dev %p)\n", wimax_dev);
	mutex_lock(&i2400m->init_mutex);
	i2400m->reset_ctx = &ctx;
	mutex_unlock(&i2400m->init_mutex);
	result = i2400m_reset(i2400m, I2400M_RT_WARM);
	if (result < 0)
		goto out;
	result = wait_for_completion_timeout(&ctx.completion, 4*HZ);
	if (result == 0)
		result = -ETIMEDOUT;
	else if (result > 0)
		result = ctx.result;
	/* if result < 0, pass it on */
	mutex_lock(&i2400m->init_mutex);
	i2400m->reset_ctx = NULL;
	mutex_unlock(&i2400m->init_mutex);
out:
	d_fnend(4, dev, "(wimax_dev %p) = %d\n", wimax_dev, result);
	return result;
}


/*
 * Check the MAC address we got from boot mode is ok
 *
 * @i2400m: device descriptor
 *
 * Returns: 0 if ok, < 0 errno code on error.
 */
static
int i2400m_check_mac_addr(struct i2400m *i2400m)
{
	int result;
	struct device *dev = i2400m_dev(i2400m);
	struct sk_buff *skb;
	const struct i2400m_tlv_detailed_device_info *ddi;
	struct net_device *net_dev = i2400m->wimax_dev.net_dev;
	const unsigned char zeromac[ETH_ALEN] = { 0 };

	d_fnstart(3, dev, "(i2400m %p)\n", i2400m);
	skb = i2400m_get_device_info(i2400m);
	if (IS_ERR(skb)) {
		result = PTR_ERR(skb);
		dev_err(dev, "Cannot verify MAC address, error reading: %d\n",
			result);
		goto error;
	}
	/* Extract MAC addresss */
	ddi = (void *) skb->data;
	BUILD_BUG_ON(ETH_ALEN != sizeof(ddi->mac_address));
	d_printf(2, dev, "GET DEVICE INFO: mac addr %pM\n",
		 ddi->mac_address);
	if (!memcmp(net_dev->perm_addr, ddi->mac_address,
		   sizeof(ddi->mac_address)))
		goto ok;
	dev_warn(dev, "warning: device reports a different MAC address "
		 "to that of boot mode's\n");
	dev_warn(dev, "device reports     %pM\n", ddi->mac_address);
	dev_warn(dev, "boot mode reported %pM\n", net_dev->perm_addr);
	if (!memcmp(zeromac, ddi->mac_address, sizeof(zeromac)))
		dev_err(dev, "device reports an invalid MAC address, "
			"not updating\n");
	else {
		dev_warn(dev, "updating MAC address\n");
		net_dev->addr_len = ETH_ALEN;
		memcpy(net_dev->perm_addr, ddi->mac_address, ETH_ALEN);
		memcpy(net_dev->dev_addr, ddi->mac_address, ETH_ALEN);
	}
ok:
	result = 0;
	kfree_skb(skb);
error:
	d_fnend(3, dev, "(i2400m %p) = %d\n", i2400m, result);
	return result;
}


/**
 * __i2400m_dev_start - Bring up driver communication with the device
 *
 * @i2400m: device descriptor
 * @flags: boot mode flags
 *
 * Returns: 0 if ok, < 0 errno code on error.
 *
 * Uploads firmware and brings up all the resources needed to be able
 * to communicate with the device.
 *
 * The workqueue has to be setup early, at least before RX handling
 * (it's only real user for now) so it can process reports as they
 * arrive. We also want to destroy it if we retry, to make sure it is
 * flushed...easier like this.
 *
 * TX needs to be setup before the bus-specific code (otherwise on
 * shutdown, the bus-tx code could try to access it).
 */
static
int __i2400m_dev_start(struct i2400m *i2400m, enum i2400m_bri flags)
{
	int result;
	struct wimax_dev *wimax_dev = &i2400m->wimax_dev;
	struct net_device *net_dev = wimax_dev->net_dev;
	struct device *dev = i2400m_dev(i2400m);
	int times = i2400m->bus_bm_retries;

	d_fnstart(3, dev, "(i2400m %p)\n", i2400m);
retry:
	result = i2400m_dev_bootstrap(i2400m, flags);
	if (result < 0) {
		dev_err(dev, "cannot bootstrap device: %d\n", result);
		goto error_bootstrap;
	}
	result = i2400m_tx_setup(i2400m);
	if (result < 0)
		goto error_tx_setup;
	result = i2400m_rx_setup(i2400m);
	if (result < 0)
		goto error_rx_setup;
	i2400m->work_queue = create_singlethread_workqueue(wimax_dev->name);
	if (i2400m->work_queue == NULL) {
		result = -ENOMEM;
		dev_err(dev, "cannot create workqueue\n");
		goto error_create_workqueue;
	}
	if (i2400m->bus_dev_start) {
		result = i2400m->bus_dev_start(i2400m);
		if (result < 0)
			goto error_bus_dev_start;
	}
	i2400m->ready = 1;
	wmb();		/* see i2400m->ready's documentation  */
	/* process pending reports from the device */
	queue_work(i2400m->work_queue, &i2400m->rx_report_ws);
	result = i2400m_firmware_check(i2400m);	/* fw versions ok? */
	if (result < 0)
		goto error_fw_check;
	/* At this point is ok to send commands to the device */
	result = i2400m_check_mac_addr(i2400m);
	if (result < 0)
		goto error_check_mac_addr;
	result = i2400m_dev_initialize(i2400m);
	if (result < 0)
		goto error_dev_initialize;

	/* We don't want any additional unwanted error recovery triggered
	 * from any other context so if anything went wrong before we come
	 * here, let's keep i2400m->error_recovery untouched and leave it to
	 * dev_reset_handle(). See dev_reset_handle(). */

	atomic_dec(&i2400m->error_recovery);
	/* Every thing works so far, ok, now we are ready to
	 * take error recovery if it's required. */

	/* At this point, reports will come for the device and set it
	 * to the right state if it is different than UNINITIALIZED */
	d_fnend(3, dev, "(net_dev %p [i2400m %p]) = %d\n",
		net_dev, i2400m, result);
	return result;

error_dev_initialize:
error_check_mac_addr:
error_fw_check:
	i2400m->ready = 0;
	wmb();		/* see i2400m->ready's documentation  */
	flush_workqueue(i2400m->work_queue);
	if (i2400m->bus_dev_stop)
		i2400m->bus_dev_stop(i2400m);
error_bus_dev_start:
	destroy_workqueue(i2400m->work_queue);
error_create_workqueue:
	i2400m_rx_release(i2400m);
error_rx_setup:
	i2400m_tx_release(i2400m);
error_tx_setup:
error_bootstrap:
	if (result == -EL3RST && times-- > 0) {
		flags = I2400M_BRI_SOFT|I2400M_BRI_MAC_REINIT;
		goto retry;
	}
	d_fnend(3, dev, "(net_dev %p [i2400m %p]) = %d\n",
		net_dev, i2400m, result);
	return result;
}


static
int i2400m_dev_start(struct i2400m *i2400m, enum i2400m_bri bm_flags)
{
	int result = 0;
	mutex_lock(&i2400m->init_mutex);	/* Well, start the device */
	if (i2400m->updown == 0) {
		result = __i2400m_dev_start(i2400m, bm_flags);
		if (result >= 0) {
			i2400m->updown = 1;
			i2400m->alive = 1;
			wmb();/* see i2400m->updown and i2400m->alive's doc */
		}
	}
	mutex_unlock(&i2400m->init_mutex);
	return result;
}


/**
 * i2400m_dev_stop - Tear down driver communication with the device
 *
 * @i2400m: device descriptor
 *
 * Returns: 0 if ok, < 0 errno code on error.
 *
 * Releases all the resources allocated to communicate with the
 * device. Note we cannot destroy the workqueue earlier as until RX is
 * fully destroyed, it could still try to schedule jobs.
 */
static
void __i2400m_dev_stop(struct i2400m *i2400m)
{
	struct wimax_dev *wimax_dev = &i2400m->wimax_dev;
	struct device *dev = i2400m_dev(i2400m);

	d_fnstart(3, dev, "(i2400m %p)\n", i2400m);
	wimax_state_change(wimax_dev, __WIMAX_ST_QUIESCING);
	i2400m_msg_to_dev_cancel_wait(i2400m, -EL3RST);
	complete(&i2400m->msg_completion);
	i2400m_net_wake_stop(i2400m);
	i2400m_dev_shutdown(i2400m);
	/*
	 * Make sure no report hooks are running *before* we stop the
	 * communication infrastructure with the device.
	 */
	i2400m->ready = 0;	/* nobody can queue work anymore */
	wmb();		/* see i2400m->ready's documentation  */
	flush_workqueue(i2400m->work_queue);

	if (i2400m->bus_dev_stop)
		i2400m->bus_dev_stop(i2400m);
	destroy_workqueue(i2400m->work_queue);
	i2400m_rx_release(i2400m);
	i2400m_tx_release(i2400m);
	wimax_state_change(wimax_dev, WIMAX_ST_DOWN);
	d_fnend(3, dev, "(i2400m %p) = 0\n", i2400m);
}


/*
 * Watch out -- we only need to stop if there is a need for it. The
 * device could have reset itself and failed to come up again (see
 * _i2400m_dev_reset_handle()).
 */
static
void i2400m_dev_stop(struct i2400m *i2400m)
{
	mutex_lock(&i2400m->init_mutex);
	if (i2400m->updown) {
		__i2400m_dev_stop(i2400m);
		i2400m->updown = 0;
		i2400m->alive = 0;
		wmb();	/* see i2400m->updown and i2400m->alive's doc */
	}
	mutex_unlock(&i2400m->init_mutex);
}


/*
 * Listen to PM events to cache the firmware before suspend/hibernation
 *
 * When the device comes out of suspend, it might go into reset and
 * firmware has to be uploaded again. At resume, most of the times, we
 * can't load firmware images from disk, so we need to cache it.
 *
 * i2400m_fw_cache() will allocate a kobject and attach the firmware
 * to it; that way we don't have to worry too much about the fw loader
 * hitting a race condition.
 *
 * Note: modus operandi stolen from the Orinoco driver; thx.
 */
static
int i2400m_pm_notifier(struct notifier_block *notifier,
		       unsigned long pm_event,
		       void *unused)
{
	struct i2400m *i2400m =
		container_of(notifier, struct i2400m, pm_notifier);
	struct device *dev = i2400m_dev(i2400m);

	d_fnstart(3, dev, "(i2400m %p pm_event %lx)\n", i2400m, pm_event);
	switch (pm_event) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		i2400m_fw_cache(i2400m);
		break;
	case PM_POST_RESTORE:
		/* Restore from hibernation failed. We need to clean
		 * up in exactly the same way, so fall through. */
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		i2400m_fw_uncache(i2400m);
		break;

	case PM_RESTORE_PREPARE:
	default:
		break;
	}
	d_fnend(3, dev, "(i2400m %p pm_event %lx) = void\n", i2400m, pm_event);
	return NOTIFY_DONE;
}


/*
 * pre-reset is called before a device is going on reset
 *
 * This has to be followed by a call to i2400m_post_reset(), otherwise
 * bad things might happen.
 */
int i2400m_pre_reset(struct i2400m *i2400m)
{
	int result;
	struct device *dev = i2400m_dev(i2400m);

	d_fnstart(3, dev, "(i2400m %p)\n", i2400m);
	d_printf(1, dev, "pre-reset shut down\n");

	result = 0;
	mutex_lock(&i2400m->init_mutex);
	if (i2400m->updown) {
		netif_tx_disable(i2400m->wimax_dev.net_dev);
		__i2400m_dev_stop(i2400m);
		result = 0;
		/* down't set updown to zero -- this way
		 * post_reset can restore properly */
	}
	mutex_unlock(&i2400m->init_mutex);
	if (i2400m->bus_release)
		i2400m->bus_release(i2400m);
	d_fnend(3, dev, "(i2400m %p) = %d\n", i2400m, result);
	return result;
}
EXPORT_SYMBOL_GPL(i2400m_pre_reset);


/*
 * Restore device state after a reset
 *
 * Do the work needed after a device reset to bring it up to the same
 * state as it was before the reset.
 *
 * NOTE: this requires i2400m->init_mutex taken
 */
int i2400m_post_reset(struct i2400m *i2400m)
{
	int result = 0;
	struct device *dev = i2400m_dev(i2400m);

	d_fnstart(3, dev, "(i2400m %p)\n", i2400m);
	d_printf(1, dev, "post-reset start\n");
	if (i2400m->bus_setup) {
		result = i2400m->bus_setup(i2400m);
		if (result < 0) {
			dev_err(dev, "bus-specific setup failed: %d\n",
				result);
			goto error_bus_setup;
		}
	}
	mutex_lock(&i2400m->init_mutex);
	if (i2400m->updown) {
		result = __i2400m_dev_start(
			i2400m, I2400M_BRI_SOFT | I2400M_BRI_MAC_REINIT);
		if (result < 0)
			goto error_dev_start;
	}
	mutex_unlock(&i2400m->init_mutex);
	d_fnend(3, dev, "(i2400m %p) = %d\n", i2400m, result);
	return result;

error_dev_start:
	if (i2400m->bus_release)
		i2400m->bus_release(i2400m);
	/* even if the device was up, it could not be recovered, so we
	 * mark it as down. */
	i2400m->updown = 0;
	wmb();		/* see i2400m->updown's documentation  */
	mutex_unlock(&i2400m->init_mutex);
error_bus_setup:
	d_fnend(3, dev, "(i2400m %p) = %d\n", i2400m, result);
	return result;
}
EXPORT_SYMBOL_GPL(i2400m_post_reset);


/*
 * The device has rebooted; fix up the device and the driver
 *
 * Tear down the driver communication with the device, reload the
 * firmware and reinitialize the communication with the device.
 *
 * If someone calls a reset when the device's firmware is down, in
 * theory we won't see it because we are not listening. However, just
 * in case, leave the code to handle it.
 *
 * If there is a reset context, use it; this means someone is waiting
 * for us to tell him when the reset operation is complete and the
 * device is ready to rock again.
 *
 * NOTE: if we are in the process of bringing up or down the
 *       communication with the device [running i2400m_dev_start() or
 *       _stop()], don't do anything, let it fail and handle it.
 *
 * This function is ran always in a thread context
 *
 * This function gets passed, as payload to i2400m_work() a 'const
 * char *' ptr with a "reason" why the reset happened (for messages).
 */
static
void __i2400m_dev_reset_handle(struct work_struct *ws)
{
	int result;
	struct i2400m_work *iw = container_of(ws, struct i2400m_work, ws);
	const char *reason;
	struct i2400m *i2400m = iw->i2400m;
	struct device *dev = i2400m_dev(i2400m);
	struct i2400m_reset_ctx *ctx = i2400m->reset_ctx;

	if (WARN_ON(iw->pl_size != sizeof(reason)))
		reason = "SW BUG: reason n/a";
	else
		memcpy(&reason, iw->pl, sizeof(reason));

	d_fnstart(3, dev, "(ws %p i2400m %p reason %s)\n", ws, i2400m, reason);

	i2400m->boot_mode = 1;
	wmb();		/* Make sure i2400m_msg_to_dev() sees boot_mode */

	result = 0;
	if (mutex_trylock(&i2400m->init_mutex) == 0) {
		/* We are still in i2400m_dev_start() [let it fail] or
		 * i2400m_dev_stop() [we are shutting down anyway, so
		 * ignore it] or we are resetting somewhere else. */
		dev_err(dev, "device rebooted somewhere else?\n");
		i2400m_msg_to_dev_cancel_wait(i2400m, -EL3RST);
		complete(&i2400m->msg_completion);
		goto out;
	}

	dev_err(dev, "%s: reinitializing driver\n", reason);
	rmb();
	if (i2400m->updown) {
		__i2400m_dev_stop(i2400m);
		i2400m->updown = 0;
		wmb();		/* see i2400m->updown's documentation  */
	}

	if (i2400m->alive) {
		result = __i2400m_dev_start(i2400m,
				    I2400M_BRI_SOFT | I2400M_BRI_MAC_REINIT);
		if (result < 0) {
			dev_err(dev, "%s: cannot start the device: %d\n",
				reason, result);
			result = -EUCLEAN;
			if (atomic_read(&i2400m->bus_reset_retries)
					>= I2400M_BUS_RESET_RETRIES) {
				result = -ENODEV;
				dev_err(dev, "tried too many times to "
					"reset the device, giving up\n");
			}
		}
	}

	if (i2400m->reset_ctx) {
		ctx->result = result;
		complete(&ctx->completion);
	}
	mutex_unlock(&i2400m->init_mutex);
	if (result == -EUCLEAN) {
		/*
		 * We come here because the reset during operational mode
		 * wasn't successully done and need to proceed to a bus
		 * reset. For the dev_reset_handle() to be able to handle
		 * the reset event later properly, we restore boot_mode back
		 * to the state before previous reset. ie: just like we are
		 * issuing the bus reset for the first time
		 */
		i2400m->boot_mode = 0;
		wmb();

		atomic_inc(&i2400m->bus_reset_retries);
		/* ops, need to clean up [w/ init_mutex not held] */
		result = i2400m_reset(i2400m, I2400M_RT_BUS);
		if (result >= 0)
			result = -ENODEV;
	} else {
		rmb();
		if (i2400m->alive) {
			/* great, we expect the device state up and
			 * dev_start() actually brings the device state up */
			i2400m->updown = 1;
			wmb();
			atomic_set(&i2400m->bus_reset_retries, 0);
		}
	}
out:
	i2400m_put(i2400m);
	kfree(iw);
	d_fnend(3, dev, "(ws %p i2400m %p reason %s) = void\n",
		ws, i2400m, reason);
}


/**
 * i2400m_dev_reset_handle - Handle a device's reset in a thread context
 *
 * Schedule a device reset handling out on a thread context, so it
 * is safe to call from atomic context. We can't use the i2400m's
 * queue as we are going to destroy it and reinitialize it as part of
 * the driver bringup/bringup process.
 *
 * See __i2400m_dev_reset_handle() for details; that takes care of
 * reinitializing the driver to handle the reset, calling into the
 * bus-specific functions ops as needed.
 */
int i2400m_dev_reset_handle(struct i2400m *i2400m, const char *reason)
{
	return i2400m_schedule_work(i2400m, __i2400m_dev_reset_handle,
				    GFP_ATOMIC, &reason, sizeof(reason));
}
EXPORT_SYMBOL_GPL(i2400m_dev_reset_handle);


 /*
 * The actual work of error recovery.
 *
 * The current implementation of error recovery is to trigger a bus reset.
 */
static
void __i2400m_error_recovery(struct work_struct *ws)
{
	struct i2400m_work *iw = container_of(ws, struct i2400m_work, ws);
	struct i2400m *i2400m = iw->i2400m;

	i2400m_reset(i2400m, I2400M_RT_BUS);

	i2400m_put(i2400m);
	kfree(iw);
	return;
}

/*
 * Schedule a work struct for error recovery.
 *
 * The intention of error recovery is to bring back the device to some
 * known state whenever TX sees -110 (-ETIMEOUT) on copying the data to
 * the device. The TX failure could mean a device bus stuck, so the current
 * error recovery implementation is to trigger a bus reset to the device
 * and hopefully it can bring back the device.
 *
 * The actual work of error recovery has to be in a thread context because
 * it is kicked off in the TX thread (i2400ms->tx_workqueue) which is to be
 * destroyed by the error recovery mechanism (currently a bus reset).
 *
 * Also, there may be already a queue of TX works that all hit
 * the -ETIMEOUT error condition because the device is stuck already.
 * Since bus reset is used as the error recovery mechanism and we don't
 * want consecutive bus resets simply because the multiple TX works
 * in the queue all hit the same device erratum, the flag "error_recovery"
 * is introduced for preventing unwanted consecutive bus resets.
 *
 * Error recovery shall only be invoked again if previous one was completed.
 * The flag error_recovery is set when error recovery mechanism is scheduled,
 * and is checked when we need to schedule another error recovery. If it is
 * in place already, then we shouldn't schedule another one.
 */
void i2400m_error_recovery(struct i2400m *i2400m)
{
	struct device *dev = i2400m_dev(i2400m);

	if (atomic_add_return(1, &i2400m->error_recovery) == 1) {
		if (i2400m_schedule_work(i2400m, __i2400m_error_recovery,
			GFP_ATOMIC, NULL, 0) < 0) {
			dev_err(dev, "run out of memory for "
				"scheduling an error recovery ?\n");
			atomic_dec(&i2400m->error_recovery);
		}
	} else
		atomic_dec(&i2400m->error_recovery);
	return;
}
EXPORT_SYMBOL_GPL(i2400m_error_recovery);

/*
 * Alloc the command and ack buffers for boot mode
 *
 * Get the buffers needed to deal with boot mode messages.  These
 * buffers need to be allocated before the sdio recieve irq is setup.
 */
static
int i2400m_bm_buf_alloc(struct i2400m *i2400m)
{
	int result;

	result = -ENOMEM;
	i2400m->bm_cmd_buf = kzalloc(I2400M_BM_CMD_BUF_SIZE, GFP_KERNEL);
	if (i2400m->bm_cmd_buf == NULL)
		goto error_bm_cmd_kzalloc;
	i2400m->bm_ack_buf = kzalloc(I2400M_BM_ACK_BUF_SIZE, GFP_KERNEL);
	if (i2400m->bm_ack_buf == NULL)
		goto error_bm_ack_buf_kzalloc;
	return 0;

error_bm_ack_buf_kzalloc:
	kfree(i2400m->bm_cmd_buf);
error_bm_cmd_kzalloc:
	return result;
}


/*
 * Free boot mode command and ack buffers.
 */
static
void i2400m_bm_buf_free(struct i2400m *i2400m)
{
	kfree(i2400m->bm_ack_buf);
	kfree(i2400m->bm_cmd_buf);
}


/**
 * i2400m_init - Initialize a 'struct i2400m' from all zeroes
 *
 * This is a bus-generic API call.
 */
void i2400m_init(struct i2400m *i2400m)
{
	wimax_dev_init(&i2400m->wimax_dev);

	i2400m->boot_mode = 1;
	i2400m->rx_reorder = 1;
	init_waitqueue_head(&i2400m->state_wq);

	spin_lock_init(&i2400m->tx_lock);
	i2400m->tx_pl_min = UINT_MAX;
	i2400m->tx_size_min = UINT_MAX;

	spin_lock_init(&i2400m->rx_lock);
	i2400m->rx_pl_min = UINT_MAX;
	i2400m->rx_size_min = UINT_MAX;
	INIT_LIST_HEAD(&i2400m->rx_reports);
	INIT_WORK(&i2400m->rx_report_ws, i2400m_report_hook_work);

	mutex_init(&i2400m->msg_mutex);
	init_completion(&i2400m->msg_completion);

	mutex_init(&i2400m->init_mutex);
	/* wake_tx_ws is initialized in i2400m_tx_setup() */
	atomic_set(&i2400m->bus_reset_retries, 0);

	i2400m->alive = 0;

	/* initialize error_recovery to 1 for denoting we
	 * are not yet ready to take any error recovery */
	atomic_set(&i2400m->error_recovery, 1);
}
EXPORT_SYMBOL_GPL(i2400m_init);


int i2400m_reset(struct i2400m *i2400m, enum i2400m_reset_type rt)
{
	struct net_device *net_dev = i2400m->wimax_dev.net_dev;

	/*
	 * Make sure we stop TXs and down the carrier before
	 * resetting; this is needed to avoid things like
	 * i2400m_wake_tx() scheduling stuff in parallel.
	 */
	if (net_dev->reg_state == NETREG_REGISTERED) {
		netif_tx_disable(net_dev);
		netif_carrier_off(net_dev);
	}
	return i2400m->bus_reset(i2400m, rt);
}
EXPORT_SYMBOL_GPL(i2400m_reset);


/**
 * i2400m_setup - bus-generic setup function for the i2400m device
 *
 * @i2400m: device descriptor (bus-specific parts have been initialized)
 *
 * Returns: 0 if ok, < 0 errno code on error.
 *
 * Sets up basic device comunication infrastructure, boots the ROM to
 * read the MAC address, registers with the WiMAX and network stacks
 * and then brings up the device.
 */
int i2400m_setup(struct i2400m *i2400m, enum i2400m_bri bm_flags)
{
	int result = -ENODEV;
	struct device *dev = i2400m_dev(i2400m);
	struct wimax_dev *wimax_dev = &i2400m->wimax_dev;
	struct net_device *net_dev = i2400m->wimax_dev.net_dev;

	d_fnstart(3, dev, "(i2400m %p)\n", i2400m);

	snprintf(wimax_dev->name, sizeof(wimax_dev->name),
		 "i2400m-%s:%s", dev->bus->name, dev_name(dev));

	result = i2400m_bm_buf_alloc(i2400m);
	if (result < 0) {
		dev_err(dev, "cannot allocate bootmode scratch buffers\n");
		goto error_bm_buf_alloc;
	}

	if (i2400m->bus_setup) {
		result = i2400m->bus_setup(i2400m);
		if (result < 0) {
			dev_err(dev, "bus-specific setup failed: %d\n",
				result);
			goto error_bus_setup;
		}
	}

	result = i2400m_bootrom_init(i2400m, bm_flags);
	if (result < 0) {
		dev_err(dev, "read mac addr: bootrom init "
			"failed: %d\n", result);
		goto error_bootrom_init;
	}
	result = i2400m_read_mac_addr(i2400m);
	if (result < 0)
		goto error_read_mac_addr;
	random_ether_addr(i2400m->src_mac_addr);

	i2400m->pm_notifier.notifier_call = i2400m_pm_notifier;
	register_pm_notifier(&i2400m->pm_notifier);

	result = register_netdev(net_dev);	/* Okey dokey, bring it up */
	if (result < 0) {
		dev_err(dev, "cannot register i2400m network device: %d\n",
			result);
		goto error_register_netdev;
	}
	netif_carrier_off(net_dev);

	i2400m->wimax_dev.op_msg_from_user = i2400m_op_msg_from_user;
	i2400m->wimax_dev.op_rfkill_sw_toggle = i2400m_op_rfkill_sw_toggle;
	i2400m->wimax_dev.op_reset = i2400m_op_reset;

	result = wimax_dev_add(&i2400m->wimax_dev, net_dev);
	if (result < 0)
		goto error_wimax_dev_add;

	/* Now setup all that requires a registered net and wimax device. */
	result = sysfs_create_group(&net_dev->dev.kobj, &i2400m_dev_attr_group);
	if (result < 0) {
		dev_err(dev, "cannot setup i2400m's sysfs: %d\n", result);
		goto error_sysfs_setup;
	}

	result = i2400m_debugfs_add(i2400m);
	if (result < 0) {
		dev_err(dev, "cannot setup i2400m's debugfs: %d\n", result);
		goto error_debugfs_setup;
	}

	result = i2400m_dev_start(i2400m, bm_flags);
	if (result < 0)
		goto error_dev_start;
	d_fnend(3, dev, "(i2400m %p) = %d\n", i2400m, result);
	return result;

error_dev_start:
	i2400m_debugfs_rm(i2400m);
error_debugfs_setup:
	sysfs_remove_group(&i2400m->wimax_dev.net_dev->dev.kobj,
			   &i2400m_dev_attr_group);
error_sysfs_setup:
	wimax_dev_rm(&i2400m->wimax_dev);
error_wimax_dev_add:
	unregister_netdev(net_dev);
error_register_netdev:
	unregister_pm_notifier(&i2400m->pm_notifier);
error_read_mac_addr:
error_bootrom_init:
	if (i2400m->bus_release)
		i2400m->bus_release(i2400m);
error_bus_setup:
	i2400m_bm_buf_free(i2400m);
error_bm_buf_alloc:
	d_fnend(3, dev, "(i2400m %p) = %d\n", i2400m, result);
	return result;
}
EXPORT_SYMBOL_GPL(i2400m_setup);


/**
 * i2400m_release - release the bus-generic driver resources
 *
 * Sends a disconnect message and undoes any setup done by i2400m_setup()
 */
void i2400m_release(struct i2400m *i2400m)
{
	struct device *dev = i2400m_dev(i2400m);

	d_fnstart(3, dev, "(i2400m %p)\n", i2400m);
	netif_stop_queue(i2400m->wimax_dev.net_dev);

	i2400m_dev_stop(i2400m);

	i2400m_debugfs_rm(i2400m);
	sysfs_remove_group(&i2400m->wimax_dev.net_dev->dev.kobj,
			   &i2400m_dev_attr_group);
	wimax_dev_rm(&i2400m->wimax_dev);
	unregister_netdev(i2400m->wimax_dev.net_dev);
	unregister_pm_notifier(&i2400m->pm_notifier);
	if (i2400m->bus_release)
		i2400m->bus_release(i2400m);
	i2400m_bm_buf_free(i2400m);
	d_fnend(3, dev, "(i2400m %p) = void\n", i2400m);
}
EXPORT_SYMBOL_GPL(i2400m_release);


/*
 * Debug levels control; see debug.h
 */
struct d_level D_LEVEL[] = {
	D_SUBMODULE_DEFINE(control),
	D_SUBMODULE_DEFINE(driver),
	D_SUBMODULE_DEFINE(debugfs),
	D_SUBMODULE_DEFINE(fw),
	D_SUBMODULE_DEFINE(netdev),
	D_SUBMODULE_DEFINE(rfkill),
	D_SUBMODULE_DEFINE(rx),
	D_SUBMODULE_DEFINE(sysfs),
	D_SUBMODULE_DEFINE(tx),
};
size_t D_LEVEL_SIZE = ARRAY_SIZE(D_LEVEL);


static
int __init i2400m_driver_init(void)
{
	d_parse_params(D_LEVEL, D_LEVEL_SIZE, i2400m_debug_params,
		       "i2400m.debug");
	return i2400m_barker_db_init(i2400m_barkers_params);
}
module_init(i2400m_driver_init);

static
void __exit i2400m_driver_exit(void)
{
	/* for scheds i2400m_dev_reset_handle() */
	flush_scheduled_work();
	i2400m_barker_db_exit();
}
module_exit(i2400m_driver_exit);

MODULE_AUTHOR("Intel Corporation <linux-wimax@intel.com>");
MODULE_DESCRIPTION("Intel 2400M WiMAX networking bus-generic driver");
MODULE_LICENSE("GPL");
