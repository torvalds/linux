/* drivers/input/keycombo.c
 *
 * Copyright (C) 2014 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/input.h>
#include <linux/keycombo.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/slab.h>

struct keycombo_state {
	struct input_handler input_handler;
	unsigned long keybit[BITS_TO_LONGS(KEY_CNT)];
	unsigned long upbit[BITS_TO_LONGS(KEY_CNT)];
	unsigned long key[BITS_TO_LONGS(KEY_CNT)];
	spinlock_t lock;
	struct  workqueue_struct *wq;
	int key_down_target;
	int key_down;
	int key_up;
	struct delayed_work key_down_work;
	int delay;
	struct work_struct key_up_work;
	void (*key_up_fn)(void *);
	void (*key_down_fn)(void *);
	void *priv;
	int key_is_down;
	struct wakeup_source combo_held_wake_source;
	struct wakeup_source combo_up_wake_source;
};

static void do_key_down(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work, struct delayed_work,
									work);
	struct keycombo_state *state = container_of(dwork,
					struct keycombo_state, key_down_work);
	if (state->key_down_fn)
		state->key_down_fn(state->priv);
}

static void do_key_up(struct work_struct *work)
{
	struct keycombo_state *state = container_of(work, struct keycombo_state,
								key_up_work);
	if (state->key_up_fn)
		state->key_up_fn(state->priv);
	__pm_relax(&state->combo_up_wake_source);
}

static void keycombo_event(struct input_handle *handle, unsigned int type,
		unsigned int code, int value)
{
	unsigned long flags;
	struct keycombo_state *state = handle->private;

	if (type != EV_KEY)
		return;

	if (code >= KEY_MAX)
		return;

	if (!test_bit(code, state->keybit))
		return;

	spin_lock_irqsave(&state->lock, flags);
	if (!test_bit(code, state->key) == !value)
		goto done;
	__change_bit(code, state->key);
	if (test_bit(code, state->upbit)) {
		if (value)
			state->key_up++;
		else
			state->key_up--;
	} else {
		if (value)
			state->key_down++;
		else
			state->key_down--;
	}
	if (state->key_down == state->key_down_target && state->key_up == 0) {
		__pm_stay_awake(&state->combo_held_wake_source);
		state->key_is_down = 1;
		if (queue_delayed_work(state->wq, &state->key_down_work,
								state->delay))
			pr_debug("Key down work already queued!");
	} else if (state->key_is_down) {
		if (!cancel_delayed_work(&state->key_down_work)) {
			__pm_stay_awake(&state->combo_up_wake_source);
			queue_work(state->wq, &state->key_up_work);
		}
		__pm_relax(&state->combo_held_wake_source);
		state->key_is_down = 0;
	}
done:
	spin_unlock_irqrestore(&state->lock, flags);
}

static int keycombo_connect(struct input_handler *handler,
		struct input_dev *dev,
		const struct input_device_id *id)
{
	int i;
	int ret;
	struct input_handle *handle;
	struct keycombo_state *state =
		container_of(handler, struct keycombo_state, input_handler);
	for (i = 0; i < KEY_MAX; i++) {
		if (test_bit(i, state->keybit) && test_bit(i, dev->keybit))
			break;
	}
	if (i == KEY_MAX)
		return -ENODEV;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = KEYCOMBO_NAME;
	handle->private = state;

	ret = input_register_handle(handle);
	if (ret)
		goto err_input_register_handle;

	ret = input_open_device(handle);
	if (ret)
		goto err_input_open_device;

	return 0;

err_input_open_device:
	input_unregister_handle(handle);
err_input_register_handle:
	kfree(handle);
	return ret;
}

static void keycombo_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id keycombo_ids[] = {
		{
				.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
				.evbit = { BIT_MASK(EV_KEY) },
		},
		{ },
};
MODULE_DEVICE_TABLE(input, keycombo_ids);

static int keycombo_probe(struct platform_device *pdev)
{
	int ret;
	int key, *keyp;
	struct keycombo_state *state;
	struct keycombo_platform_data *pdata = pdev->dev.platform_data;

	if (!pdata)
		return -EINVAL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	spin_lock_init(&state->lock);
	keyp = pdata->keys_down;
	while ((key = *keyp++)) {
		if (key >= KEY_MAX)
			continue;
		state->key_down_target++;
		__set_bit(key, state->keybit);
	}
	if (pdata->keys_up) {
		keyp = pdata->keys_up;
		while ((key = *keyp++)) {
			if (key >= KEY_MAX)
				continue;
			__set_bit(key, state->keybit);
			__set_bit(key, state->upbit);
		}
	}

	state->wq = alloc_ordered_workqueue("keycombo", 0);
	if (!state->wq)
		return -ENOMEM;

	state->priv = pdata->priv;

	if (pdata->key_down_fn)
		state->key_down_fn = pdata->key_down_fn;
	INIT_DELAYED_WORK(&state->key_down_work, do_key_down);

	if (pdata->key_up_fn)
		state->key_up_fn = pdata->key_up_fn;
	INIT_WORK(&state->key_up_work, do_key_up);

	wakeup_source_init(&state->combo_held_wake_source, "key combo");
	wakeup_source_init(&state->combo_up_wake_source, "key combo up");
	state->delay = msecs_to_jiffies(pdata->key_down_delay);

	state->input_handler.event = keycombo_event;
	state->input_handler.connect = keycombo_connect;
	state->input_handler.disconnect = keycombo_disconnect;
	state->input_handler.name = KEYCOMBO_NAME;
	state->input_handler.id_table = keycombo_ids;
	ret = input_register_handler(&state->input_handler);
	if (ret) {
		kfree(state);
		return ret;
	}
	platform_set_drvdata(pdev, state);
	return 0;
}

int keycombo_remove(struct platform_device *pdev)
{
	struct keycombo_state *state = platform_get_drvdata(pdev);
	input_unregister_handler(&state->input_handler);
	destroy_workqueue(state->wq);
	kfree(state);
	return 0;
}


struct platform_driver keycombo_driver = {
		.driver.name = KEYCOMBO_NAME,
		.probe = keycombo_probe,
		.remove = keycombo_remove,
};

static int __init keycombo_init(void)
{
	return platform_driver_register(&keycombo_driver);
}

static void __exit keycombo_exit(void)
{
	return platform_driver_unregister(&keycombo_driver);
}

module_init(keycombo_init);
module_exit(keycombo_exit);
