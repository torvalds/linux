// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Mellanox Technologies. All rights reserved */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "netdevsim.h"

static int
nsim_dev_empty_reporter_dump(struct devlink_health_reporter *reporter,
			     struct devlink_fmsg *fmsg, void *priv_ctx,
			     struct netlink_ext_ack *extack)
{
	return 0;
}

static int
nsim_dev_empty_reporter_diagnose(struct devlink_health_reporter *reporter,
				 struct devlink_fmsg *fmsg,
				 struct netlink_ext_ack *extack)
{
	return 0;
}

static const
struct devlink_health_reporter_ops nsim_dev_empty_reporter_ops = {
	.name = "empty",
	.dump = nsim_dev_empty_reporter_dump,
	.diagnose = nsim_dev_empty_reporter_diagnose,
};

struct nsim_dev_dummy_reporter_ctx {
	char *break_msg;
};

static int
nsim_dev_dummy_reporter_recover(struct devlink_health_reporter *reporter,
				void *priv_ctx,
				struct netlink_ext_ack *extack)
{
	struct nsim_dev_health *health = devlink_health_reporter_priv(reporter);
	struct nsim_dev_dummy_reporter_ctx *ctx = priv_ctx;

	if (health->fail_recover) {
		/* For testing purposes, user set debugfs fail_recover
		 * value to true. Fail right away.
		 */
		NL_SET_ERR_MSG_MOD(extack, "User setup the recover to fail for testing purposes");
		return -EINVAL;
	}
	if (ctx) {
		kfree(health->recovered_break_msg);
		health->recovered_break_msg = kstrdup(ctx->break_msg,
						      GFP_KERNEL);
		if (!health->recovered_break_msg)
			return -ENOMEM;
	}
	return 0;
}

static int nsim_dev_dummy_fmsg_put(struct devlink_fmsg *fmsg, u32 binary_len)
{
	char *binary;
	int err;
	int i;

	err = devlink_fmsg_bool_pair_put(fmsg, "test_bool", true);
	if (err)
		return err;
	err = devlink_fmsg_u8_pair_put(fmsg, "test_u8", 1);
	if (err)
		return err;
	err = devlink_fmsg_u32_pair_put(fmsg, "test_u32", 3);
	if (err)
		return err;
	err = devlink_fmsg_u64_pair_put(fmsg, "test_u64", 4);
	if (err)
		return err;
	err = devlink_fmsg_string_pair_put(fmsg, "test_string", "somestring");
	if (err)
		return err;

	binary = kmalloc(binary_len, GFP_KERNEL | __GFP_NOWARN);
	if (!binary)
		return -ENOMEM;
	get_random_bytes(binary, binary_len);
	err = devlink_fmsg_binary_pair_put(fmsg, "test_binary", binary, binary_len);
	kfree(binary);
	if (err)
		return err;

	err = devlink_fmsg_pair_nest_start(fmsg, "test_nest");
	if (err)
		return err;
	err = devlink_fmsg_obj_nest_start(fmsg);
	if (err)
		return err;
	err = devlink_fmsg_bool_pair_put(fmsg, "nested_test_bool", false);
	if (err)
		return err;
	err = devlink_fmsg_u8_pair_put(fmsg, "nested_test_u8", false);
	if (err)
		return err;
	err = devlink_fmsg_obj_nest_end(fmsg);
	if (err)
		return err;
	err = devlink_fmsg_pair_nest_end(fmsg);
	if (err)
		return err;

	err = devlink_fmsg_arr_pair_nest_start(fmsg, "test_bool_array");
	if (err)
		return err;
	for (i = 0; i < 10; i++) {
		err = devlink_fmsg_bool_put(fmsg, true);
		if (err)
			return err;
	}
	err = devlink_fmsg_arr_pair_nest_end(fmsg);
	if (err)
		return err;

	err = devlink_fmsg_arr_pair_nest_start(fmsg, "test_u8_array");
	if (err)
		return err;
	for (i = 0; i < 10; i++) {
		err = devlink_fmsg_u8_put(fmsg, i);
		if (err)
			return err;
	}
	err = devlink_fmsg_arr_pair_nest_end(fmsg);
	if (err)
		return err;

	err = devlink_fmsg_arr_pair_nest_start(fmsg, "test_u32_array");
	if (err)
		return err;
	for (i = 0; i < 10; i++) {
		err = devlink_fmsg_u32_put(fmsg, i);
		if (err)
			return err;
	}
	err = devlink_fmsg_arr_pair_nest_end(fmsg);
	if (err)
		return err;

	err = devlink_fmsg_arr_pair_nest_start(fmsg, "test_u64_array");
	if (err)
		return err;
	for (i = 0; i < 10; i++) {
		err = devlink_fmsg_u64_put(fmsg, i);
		if (err)
			return err;
	}
	err = devlink_fmsg_arr_pair_nest_end(fmsg);
	if (err)
		return err;

	err = devlink_fmsg_arr_pair_nest_start(fmsg, "test_array_of_objects");
	if (err)
		return err;
	for (i = 0; i < 10; i++) {
		err = devlink_fmsg_obj_nest_start(fmsg);
		if (err)
			return err;
		err = devlink_fmsg_bool_pair_put(fmsg,
						 "in_array_nested_test_bool",
						 false);
		if (err)
			return err;
		err = devlink_fmsg_u8_pair_put(fmsg,
					       "in_array_nested_test_u8",
					       i);
		if (err)
			return err;
		err = devlink_fmsg_obj_nest_end(fmsg);
		if (err)
			return err;
	}
	return devlink_fmsg_arr_pair_nest_end(fmsg);
}

static int
nsim_dev_dummy_reporter_dump(struct devlink_health_reporter *reporter,
			     struct devlink_fmsg *fmsg, void *priv_ctx,
			     struct netlink_ext_ack *extack)
{
	struct nsim_dev_health *health = devlink_health_reporter_priv(reporter);
	struct nsim_dev_dummy_reporter_ctx *ctx = priv_ctx;
	int err;

	if (ctx) {
		err = devlink_fmsg_string_pair_put(fmsg, "break_message",
						   ctx->break_msg);
		if (err)
			return err;
	}
	return nsim_dev_dummy_fmsg_put(fmsg, health->binary_len);
}

static int
nsim_dev_dummy_reporter_diagnose(struct devlink_health_reporter *reporter,
				 struct devlink_fmsg *fmsg,
				 struct netlink_ext_ack *extack)
{
	struct nsim_dev_health *health = devlink_health_reporter_priv(reporter);
	int err;

	if (health->recovered_break_msg) {
		err = devlink_fmsg_string_pair_put(fmsg,
						   "recovered_break_message",
						   health->recovered_break_msg);
		if (err)
			return err;
	}
	return nsim_dev_dummy_fmsg_put(fmsg, health->binary_len);
}

static const
struct devlink_health_reporter_ops nsim_dev_dummy_reporter_ops = {
	.name = "dummy",
	.recover = nsim_dev_dummy_reporter_recover,
	.dump = nsim_dev_dummy_reporter_dump,
	.diagnose = nsim_dev_dummy_reporter_diagnose,
};

static ssize_t nsim_dev_health_break_write(struct file *file,
					   const char __user *data,
					   size_t count, loff_t *ppos)
{
	struct nsim_dev_health *health = file->private_data;
	struct nsim_dev_dummy_reporter_ctx ctx;
	char *break_msg;
	int err;

	break_msg = kmalloc(count + 1, GFP_KERNEL);
	if (!break_msg)
		return -ENOMEM;

	if (copy_from_user(break_msg, data, count)) {
		err = -EFAULT;
		goto out;
	}
	break_msg[count] = '\0';
	if (break_msg[count - 1] == '\n')
		break_msg[count - 1] = '\0';

	ctx.break_msg = break_msg;
	err = devlink_health_report(health->dummy_reporter, break_msg, &ctx);
	if (err)
		goto out;

out:
	kfree(break_msg);
	return err ?: count;
}

static const struct file_operations nsim_dev_health_break_fops = {
	.open = simple_open,
	.write = nsim_dev_health_break_write,
	.llseek = generic_file_llseek,
};

int nsim_dev_health_init(struct nsim_dev *nsim_dev, struct devlink *devlink)
{
	struct nsim_dev_health *health = &nsim_dev->health;
	int err;

	health->empty_reporter =
		devlink_health_reporter_create(devlink,
					       &nsim_dev_empty_reporter_ops,
					       0, false, health);
	if (IS_ERR(health->empty_reporter))
		return PTR_ERR(health->empty_reporter);

	health->dummy_reporter =
		devlink_health_reporter_create(devlink,
					       &nsim_dev_dummy_reporter_ops,
					       0, false, health);
	if (IS_ERR(health->dummy_reporter)) {
		err = PTR_ERR(health->dummy_reporter);
		goto err_empty_reporter_destroy;
	}

	health->ddir = debugfs_create_dir("health", nsim_dev->ddir);
	if (IS_ERR(health->ddir)) {
		err = PTR_ERR(health->ddir);
		goto err_dummy_reporter_destroy;
	}

	health->recovered_break_msg = NULL;
	debugfs_create_file("break_health", 0200, health->ddir, health,
			    &nsim_dev_health_break_fops);
	health->binary_len = 16;
	debugfs_create_u32("binary_len", 0600, health->ddir,
			   &health->binary_len);
	health->fail_recover = false;
	debugfs_create_bool("fail_recover", 0600, health->ddir,
			    &health->fail_recover);
	return 0;

err_dummy_reporter_destroy:
	devlink_health_reporter_destroy(health->dummy_reporter);
err_empty_reporter_destroy:
	devlink_health_reporter_destroy(health->empty_reporter);
	return err;
}

void nsim_dev_health_exit(struct nsim_dev *nsim_dev)
{
	struct nsim_dev_health *health = &nsim_dev->health;

	debugfs_remove_recursive(health->ddir);
	kfree(health->recovered_break_msg);
	devlink_health_reporter_destroy(health->dummy_reporter);
	devlink_health_reporter_destroy(health->empty_reporter);
}
