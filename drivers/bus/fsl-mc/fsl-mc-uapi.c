// SPDX-License-Identifier: GPL-2.0
/*
 * Management Complex (MC) userspace support
 *
 * Copyright 2021 NXP
 *
 */

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>

#include "fsl-mc-private.h"

struct uapi_priv_data {
	struct fsl_mc_uapi *uapi;
	struct fsl_mc_io *mc_io;
};

struct fsl_mc_cmd_desc {
	u16 cmdid_value;
	u16 cmdid_mask;
	int size;
	bool token;
	int flags;
};

#define FSL_MC_CHECK_MODULE_ID		BIT(0)
#define FSL_MC_CAP_NET_ADMIN_NEEDED	BIT(1)

enum fsl_mc_cmd_index {
	DPDBG_DUMP = 0,
	DPDBG_SET,
	DPRC_GET_CONTAINER_ID,
	DPRC_CREATE_CONT,
	DPRC_DESTROY_CONT,
	DPRC_ASSIGN,
	DPRC_UNASSIGN,
	DPRC_GET_OBJ_COUNT,
	DPRC_GET_OBJ,
	DPRC_GET_RES_COUNT,
	DPRC_GET_RES_IDS,
	DPRC_SET_OBJ_LABEL,
	DPRC_SET_LOCKED,
	DPRC_CONNECT,
	DPRC_DISCONNECT,
	DPRC_GET_POOL,
	DPRC_GET_POOL_COUNT,
	DPRC_GET_CONNECTION,
	DPRC_GET_MEM,
	DPCI_GET_LINK_STATE,
	DPCI_GET_PEER_ATTR,
	DPAIOP_GET_SL_VERSION,
	DPAIOP_GET_STATE,
	DPMNG_GET_VERSION,
	DPSECI_GET_TX_QUEUE,
	DPMAC_GET_COUNTER,
	DPMAC_GET_MAC_ADDR,
	DPNI_SET_PRIM_MAC,
	DPNI_GET_PRIM_MAC,
	DPNI_GET_STATISTICS,
	DPNI_GET_LINK_STATE,
	DPNI_GET_MAX_FRAME_LENGTH,
	DPSW_GET_TAILDROP,
	DPSW_SET_TAILDROP,
	DPSW_IF_GET_COUNTER,
	DPSW_IF_GET_MAX_FRAME_LENGTH,
	DPDMUX_GET_COUNTER,
	DPDMUX_IF_GET_MAX_FRAME_LENGTH,
	GET_ATTR,
	GET_IRQ_MASK,
	GET_IRQ_STATUS,
	CLOSE,
	OPEN,
	GET_API_VERSION,
	DESTROY,
	CREATE,
};

static struct fsl_mc_cmd_desc fsl_mc_accepted_cmds[] = {
	[DPDBG_DUMP] = {
		.cmdid_value = 0x1300,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 28,
	},
	[DPDBG_SET] = {
		.cmdid_value = 0x1400,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 28,
	},
	[DPRC_GET_CONTAINER_ID] = {
		.cmdid_value = 0x8300,
		.cmdid_mask = 0xFFF0,
		.token = false,
		.size = 8,
	},
	[DPRC_CREATE_CONT] = {
		.cmdid_value = 0x1510,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 40,
		.flags = FSL_MC_CAP_NET_ADMIN_NEEDED,
	},
	[DPRC_DESTROY_CONT] = {
		.cmdid_value = 0x1520,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 12,
		.flags = FSL_MC_CAP_NET_ADMIN_NEEDED,
	},
	[DPRC_ASSIGN] = {
		.cmdid_value = 0x1570,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 40,
		.flags = FSL_MC_CAP_NET_ADMIN_NEEDED,
	},
	[DPRC_UNASSIGN] = {
		.cmdid_value = 0x1580,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 40,
		.flags = FSL_MC_CAP_NET_ADMIN_NEEDED,
	},
	[DPRC_GET_OBJ_COUNT] = {
		.cmdid_value = 0x1590,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 16,
	},
	[DPRC_GET_OBJ] = {
		.cmdid_value = 0x15A0,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 12,
	},
	[DPRC_GET_RES_COUNT] = {
		.cmdid_value = 0x15B0,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 32,
	},
	[DPRC_GET_RES_IDS] = {
		.cmdid_value = 0x15C0,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 40,
	},
	[DPRC_SET_OBJ_LABEL] = {
		.cmdid_value = 0x1610,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 48,
		.flags = FSL_MC_CAP_NET_ADMIN_NEEDED,
	},
	[DPRC_SET_LOCKED] = {
		.cmdid_value = 0x16B0,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 16,
		.flags = FSL_MC_CAP_NET_ADMIN_NEEDED,
	},
	[DPRC_CONNECT] = {
		.cmdid_value = 0x1670,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 56,
		.flags = FSL_MC_CAP_NET_ADMIN_NEEDED,
	},
	[DPRC_DISCONNECT] = {
		.cmdid_value = 0x1680,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 32,
		.flags = FSL_MC_CAP_NET_ADMIN_NEEDED,
	},
	[DPRC_GET_POOL] = {
		.cmdid_value = 0x1690,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 12,
	},
	[DPRC_GET_POOL_COUNT] = {
		.cmdid_value = 0x16A0,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 8,
	},
	[DPRC_GET_CONNECTION] = {
		.cmdid_value = 0x16C0,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 32,
	},
	[DPRC_GET_MEM] = {
		.cmdid_value = 0x16D0,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 12,
	},

	[DPCI_GET_LINK_STATE] = {
		.cmdid_value = 0x0E10,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 8,
	},
	[DPCI_GET_PEER_ATTR] = {
		.cmdid_value = 0x0E20,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 8,
	},
	[DPAIOP_GET_SL_VERSION] = {
		.cmdid_value = 0x2820,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 8,
	},
	[DPAIOP_GET_STATE] = {
		.cmdid_value = 0x2830,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 8,
	},
	[DPMNG_GET_VERSION] = {
		.cmdid_value = 0x8310,
		.cmdid_mask = 0xFFF0,
		.token = false,
		.size = 8,
	},
	[DPSECI_GET_TX_QUEUE] = {
		.cmdid_value = 0x1970,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 14,
	},
	[DPMAC_GET_COUNTER] = {
		.cmdid_value = 0x0c40,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 9,
	},
	[DPMAC_GET_MAC_ADDR] = {
		.cmdid_value = 0x0c50,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 8,
	},
	[DPNI_SET_PRIM_MAC] = {
		.cmdid_value = 0x2240,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 16,
		.flags = FSL_MC_CAP_NET_ADMIN_NEEDED,
	},
	[DPNI_GET_PRIM_MAC] = {
		.cmdid_value = 0x2250,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 8,
	},
	[DPNI_GET_STATISTICS] = {
		.cmdid_value = 0x25D0,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 10,
	},
	[DPNI_GET_LINK_STATE] = {
		.cmdid_value = 0x2150,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 8,
	},
	[DPNI_GET_MAX_FRAME_LENGTH] = {
		.cmdid_value = 0x2170,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 8,
	},
	[DPSW_GET_TAILDROP] = {
		.cmdid_value = 0x0A90,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 14,
	},
	[DPSW_SET_TAILDROP] = {
		.cmdid_value = 0x0A80,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 24,
		.flags = FSL_MC_CAP_NET_ADMIN_NEEDED,
	},
	[DPSW_IF_GET_COUNTER] = {
		.cmdid_value = 0x0340,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 11,
	},
	[DPSW_IF_GET_MAX_FRAME_LENGTH] = {
		.cmdid_value = 0x0450,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 10,
	},
	[DPDMUX_GET_COUNTER] = {
		.cmdid_value = 0x0b20,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 11,
	},
	[DPDMUX_IF_GET_MAX_FRAME_LENGTH] = {
		.cmdid_value = 0x0a20,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 10,
	},
	[GET_ATTR] = {
		.cmdid_value = 0x0040,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 8,
	},
	[GET_IRQ_MASK] = {
		.cmdid_value = 0x0150,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 13,
	},
	[GET_IRQ_STATUS] = {
		.cmdid_value = 0x0160,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 13,
	},
	[CLOSE] = {
		.cmdid_value = 0x8000,
		.cmdid_mask = 0xFFF0,
		.token = true,
		.size = 8,
	},

	/* Common commands amongst all types of objects. Must be checked last. */
	[OPEN] = {
		.cmdid_value = 0x8000,
		.cmdid_mask = 0xFC00,
		.token = false,
		.size = 12,
		.flags = FSL_MC_CHECK_MODULE_ID,
	},
	[GET_API_VERSION] = {
		.cmdid_value = 0xA000,
		.cmdid_mask = 0xFC00,
		.token = false,
		.size = 8,
		.flags = FSL_MC_CHECK_MODULE_ID,
	},
	[DESTROY] = {
		.cmdid_value = 0x9800,
		.cmdid_mask = 0xFC00,
		.token = true,
		.size = 12,
		.flags = FSL_MC_CHECK_MODULE_ID | FSL_MC_CAP_NET_ADMIN_NEEDED,
	},
	[CREATE] = {
		.cmdid_value = 0x9000,
		.cmdid_mask = 0xFC00,
		.token = true,
		.size = 64,
		.flags = FSL_MC_CHECK_MODULE_ID | FSL_MC_CAP_NET_ADMIN_NEEDED,
	},
};

#define FSL_MC_NUM_ACCEPTED_CMDS ARRAY_SIZE(fsl_mc_accepted_cmds)

#define FSL_MC_MAX_MODULE_ID 0x10

static int fsl_mc_command_check(struct fsl_mc_device *mc_dev,
				struct fsl_mc_command *mc_cmd)
{
	struct fsl_mc_cmd_desc *desc = NULL;
	int mc_cmd_max_size, i;
	bool token_provided;
	u16 cmdid, module_id;
	char *mc_cmd_end;
	char sum = 0;

	/* Check if this is an accepted MC command */
	cmdid = mc_cmd_hdr_read_cmdid(mc_cmd);
	for (i = 0; i < FSL_MC_NUM_ACCEPTED_CMDS; i++) {
		desc = &fsl_mc_accepted_cmds[i];
		if ((cmdid & desc->cmdid_mask) == desc->cmdid_value)
			break;
	}
	if (i == FSL_MC_NUM_ACCEPTED_CMDS) {
		dev_err(&mc_dev->dev, "MC command 0x%04x: cmdid not accepted\n", cmdid);
		return -EACCES;
	}

	/* Check if the size of the command is honored. Anything beyond the
	 * last valid byte of the command should be zeroed.
	 */
	mc_cmd_max_size = sizeof(*mc_cmd);
	mc_cmd_end = ((char *)mc_cmd) + desc->size;
	for (i = desc->size; i < mc_cmd_max_size; i++)
		sum |= *mc_cmd_end++;
	if (sum) {
		dev_err(&mc_dev->dev, "MC command 0x%04x: garbage beyond max size of %d bytes!\n",
			cmdid, desc->size);
		return -EACCES;
	}

	/* Some MC commands request a token to be passed so that object
	 * identification is possible. Check if the token passed in the command
	 * is as expected.
	 */
	token_provided = mc_cmd_hdr_read_token(mc_cmd) ? true : false;
	if (token_provided != desc->token) {
		dev_err(&mc_dev->dev, "MC command 0x%04x: token 0x%04x is invalid!\n",
			cmdid, mc_cmd_hdr_read_token(mc_cmd));
		return -EACCES;
	}

	/* If needed, check if the module ID passed is valid */
	if (desc->flags & FSL_MC_CHECK_MODULE_ID) {
		/* The module ID is represented by bits [4:9] from the cmdid */
		module_id = (cmdid & GENMASK(9, 4)) >> 4;
		if (module_id == 0 || module_id > FSL_MC_MAX_MODULE_ID) {
			dev_err(&mc_dev->dev, "MC command 0x%04x: unknown module ID 0x%x\n",
				cmdid, module_id);
			return -EACCES;
		}
	}

	/* Some commands alter how hardware resources are managed. For these
	 * commands, check for CAP_NET_ADMIN.
	 */
	if (desc->flags & FSL_MC_CAP_NET_ADMIN_NEEDED) {
		if (!capable(CAP_NET_ADMIN)) {
			dev_err(&mc_dev->dev, "MC command 0x%04x: needs CAP_NET_ADMIN!\n",
				cmdid);
			return -EPERM;
		}
	}

	return 0;
}

static int fsl_mc_uapi_send_command(struct fsl_mc_device *mc_dev, unsigned long arg,
				    struct fsl_mc_io *mc_io)
{
	struct fsl_mc_command mc_cmd;
	int error;

	error = copy_from_user(&mc_cmd, (void __user *)arg, sizeof(mc_cmd));
	if (error)
		return -EFAULT;

	error = fsl_mc_command_check(mc_dev, &mc_cmd);
	if (error)
		return error;

	error = mc_send_command(mc_io, &mc_cmd);
	if (error)
		return error;

	error = copy_to_user((void __user *)arg, &mc_cmd, sizeof(mc_cmd));
	if (error)
		return -EFAULT;

	return 0;
}

static int fsl_mc_uapi_dev_open(struct inode *inode, struct file *filep)
{
	struct fsl_mc_device *root_mc_device;
	struct uapi_priv_data *priv_data;
	struct fsl_mc_io *dynamic_mc_io;
	struct fsl_mc_uapi *mc_uapi;
	struct fsl_mc_bus *mc_bus;
	int error;

	priv_data = kzalloc(sizeof(*priv_data), GFP_KERNEL);
	if (!priv_data)
		return -ENOMEM;

	mc_uapi = container_of(filep->private_data, struct fsl_mc_uapi, misc);
	mc_bus = container_of(mc_uapi, struct fsl_mc_bus, uapi_misc);
	root_mc_device = &mc_bus->mc_dev;

	mutex_lock(&mc_uapi->mutex);

	if (!mc_uapi->local_instance_in_use) {
		priv_data->mc_io = mc_uapi->static_mc_io;
		mc_uapi->local_instance_in_use = 1;
	} else {
		error = fsl_mc_portal_allocate(root_mc_device, 0,
					       &dynamic_mc_io);
		if (error) {
			dev_dbg(&root_mc_device->dev,
				"Could not allocate MC portal\n");
			goto error_portal_allocate;
		}

		priv_data->mc_io = dynamic_mc_io;
	}
	priv_data->uapi = mc_uapi;
	filep->private_data = priv_data;

	mutex_unlock(&mc_uapi->mutex);

	return 0;

error_portal_allocate:
	mutex_unlock(&mc_uapi->mutex);
	kfree(priv_data);

	return error;
}

static int fsl_mc_uapi_dev_release(struct inode *inode, struct file *filep)
{
	struct uapi_priv_data *priv_data;
	struct fsl_mc_uapi *mc_uapi;
	struct fsl_mc_io *mc_io;

	priv_data = filep->private_data;
	mc_uapi = priv_data->uapi;
	mc_io = priv_data->mc_io;

	mutex_lock(&mc_uapi->mutex);

	if (mc_io == mc_uapi->static_mc_io)
		mc_uapi->local_instance_in_use = 0;
	else
		fsl_mc_portal_free(mc_io);

	kfree(filep->private_data);
	filep->private_data =  NULL;

	mutex_unlock(&mc_uapi->mutex);

	return 0;
}

static long fsl_mc_uapi_dev_ioctl(struct file *file,
				  unsigned int cmd,
				  unsigned long arg)
{
	struct uapi_priv_data *priv_data = file->private_data;
	struct fsl_mc_device *root_mc_device;
	struct fsl_mc_bus *mc_bus;
	int error;

	mc_bus = container_of(priv_data->uapi, struct fsl_mc_bus, uapi_misc);
	root_mc_device = &mc_bus->mc_dev;

	switch (cmd) {
	case FSL_MC_SEND_MC_COMMAND:
		error = fsl_mc_uapi_send_command(root_mc_device, arg, priv_data->mc_io);
		break;
	default:
		dev_dbg(&root_mc_device->dev, "unexpected ioctl call number\n");
		error = -EINVAL;
	}

	return error;
}

static const struct file_operations fsl_mc_uapi_dev_fops = {
	.owner = THIS_MODULE,
	.open = fsl_mc_uapi_dev_open,
	.release = fsl_mc_uapi_dev_release,
	.unlocked_ioctl = fsl_mc_uapi_dev_ioctl,
};

int fsl_mc_uapi_create_device_file(struct fsl_mc_bus *mc_bus)
{
	struct fsl_mc_device *mc_dev = &mc_bus->mc_dev;
	struct fsl_mc_uapi *mc_uapi = &mc_bus->uapi_misc;
	int error;

	mc_uapi->misc.minor = MISC_DYNAMIC_MINOR;
	mc_uapi->misc.name = dev_name(&mc_dev->dev);
	mc_uapi->misc.fops = &fsl_mc_uapi_dev_fops;

	error = misc_register(&mc_uapi->misc);
	if (error)
		return error;

	mc_uapi->static_mc_io = mc_bus->mc_dev.mc_io;

	mutex_init(&mc_uapi->mutex);

	return 0;
}

void fsl_mc_uapi_remove_device_file(struct fsl_mc_bus *mc_bus)
{
	misc_deregister(&mc_bus->uapi_misc.misc);
}
