/* drivers/input/keyreset.c
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
#include <linux/keyreset.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/keycombo.h>

struct keyreset_state {
	int restart_requested;
	int (*reset_fn)(void);
	struct platform_device *pdev_child;
	struct work_struct restart_work;
};

static void do_restart(struct work_struct *unused)
{
	sys_sync();
	kernel_restart(NULL);
}

static void do_reset_fn(void *priv)
{
	struct keyreset_state *state = priv;
	if (state->restart_requested)
		panic("keyboard reset failed, %d", state->restart_requested);
	if (state->reset_fn) {
		state->restart_requested = state->reset_fn();
	} else {
		pr_info("keyboard reset\n");
		schedule_work(&state->restart_work);
		state->restart_requested = 1;
	}
}

static int keyreset_probe(struct platform_device *pdev)
{
	int ret = -ENOMEM;
	struct keycombo_platform_data *pdata_child;
	struct keyreset_platform_data *pdata = pdev->dev.platform_data;
	int up_size = 0, down_size = 0, size;
	int key, *keyp;
	struct keyreset_state *state;

	if (!pdata)
		return -EINVAL;
	state = devm_kzalloc(&pdev->dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->pdev_child = platform_device_alloc(KEYCOMBO_NAME,
							PLATFORM_DEVID_AUTO);
	if (!state->pdev_child)
		return -ENOMEM;
	state->pdev_child->dev.parent = &pdev->dev;
	INIT_WORK(&state->restart_work, do_restart);

	keyp = pdata->keys_down;
	while ((key = *keyp++)) {
		if (key >= KEY_MAX)
			continue;
		down_size++;
	}
	if (pdata->keys_up) {
		keyp = pdata->keys_up;
		while ((key = *keyp++)) {
			if (key >= KEY_MAX)
				continue;
			up_size++;
		}
	}
	size = sizeof(struct keycombo_platform_data)
			+ sizeof(int) * (down_size + 1);
	pdata_child = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!pdata_child)
		goto error;
	memcpy(pdata_child->keys_down, pdata->keys_down,
						sizeof(int) * down_size);
	if (up_size > 0) {
		pdata_child->keys_up = devm_kzalloc(&pdev->dev, up_size + 1,
								GFP_KERNEL);
		if (!pdata_child->keys_up)
			goto error;
		memcpy(pdata_child->keys_up, pdata->keys_up,
							sizeof(int) * up_size);
		if (!pdata_child->keys_up)
			goto error;
	}
	state->reset_fn = pdata->reset_fn;
	pdata_child->key_down_fn = do_reset_fn;
	pdata_child->priv = state;
	pdata_child->key_down_delay = pdata->key_down_delay;
	ret = platform_device_add_data(state->pdev_child, pdata_child, size);
	if (ret)
		goto error;
	platform_set_drvdata(pdev, state);
	return platform_device_add(state->pdev_child);
error:
	platform_device_put(state->pdev_child);
	return ret;
}

int keyreset_remove(struct platform_device *pdev)
{
	struct keyreset_state *state = platform_get_drvdata(pdev);
	platform_device_put(state->pdev_child);
	return 0;
}


struct platform_driver keyreset_driver = {
	.driver.name = KEYRESET_NAME,
	.probe = keyreset_probe,
	.remove = keyreset_remove,
};

static int __init keyreset_init(void)
{
	return platform_driver_register(&keyreset_driver);
}

static void __exit keyreset_exit(void)
{
	return platform_driver_unregister(&keyreset_driver);
}

module_init(keyreset_init);
module_exit(keyreset_exit);
