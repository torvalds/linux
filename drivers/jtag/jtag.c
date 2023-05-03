// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018 Mellanox Technologies. All rights reserved.
// Copyright (c) 2018 Oleksandr Shamray <oleksandrs@mellanox.com>
// Copyright (c) 2019 Intel Corporation

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/jtag.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/spinlock.h>
#include <uapi/linux/jtag.h>

static char *end_status_str[] = { "tlr",   "idle",   "selDR", "capDR", "sDR",
				  "ex1DR", "pDR",    "ex2DR", "updDR", "selIR",
				  "capIR", "sIR",    "ex1IR", "pIR",   "ex2IR",
				  "updIR", "current" };

struct jtag {
	struct miscdevice miscdev;
	const struct jtag_ops *ops;
	int id;
	unsigned long priv[0];
};

static DEFINE_IDA(jtag_ida);

void *jtag_priv(struct jtag *jtag)
{
	return jtag->priv;
}
EXPORT_SYMBOL_GPL(jtag_priv);

static long jtag_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct jtag *jtag = file->private_data;
	struct jtag_tap_state tapstate;
	struct jtag_xfer xfer;
	struct bitbang_packet bitbang;
	struct tck_bitbang *bitbang_data;
	struct jtag_mode mode;
	u8 *xfer_data;
	u32 data_size;
	u32 value;
	int err;

	if (!arg)
		return -EINVAL;

	switch (cmd) {
	case JTAG_GIOCFREQ:
		if (!jtag->ops->freq_get)
			return -EOPNOTSUPP;

		err = jtag->ops->freq_get(jtag, &value);
		if (err)
			break;
		dev_dbg(jtag->miscdev.parent, "JTAG_GIOCFREQ: freq get = %d",
			value);

		if (put_user(value, (__u32 __user *)arg))
			err = -EFAULT;
		break;

	case JTAG_SIOCFREQ:
		if (!jtag->ops->freq_set)
			return -EOPNOTSUPP;

		if (get_user(value, (__u32 __user *)arg))
			return -EFAULT;
		if (value == 0)
			return -EINVAL;

		err = jtag->ops->freq_set(jtag, value);
		dev_dbg(jtag->miscdev.parent, "JTAG_SIOCFREQ: freq set = %d",
			value);
		break;

	case JTAG_SIOCSTATE:
		if (copy_from_user(&tapstate, (const void __user *)arg,
				   sizeof(struct jtag_tap_state)))
			return -EFAULT;

		if (tapstate.from > JTAG_STATE_CURRENT)
			return -EINVAL;

		if (tapstate.endstate > JTAG_STATE_CURRENT)
			return -EINVAL;

		if (tapstate.reset > JTAG_FORCE_RESET)
			return -EINVAL;

		dev_dbg(jtag->miscdev.parent,
			"JTAG_SIOCSTATE: status set from %s to %s reset %d tck %d",
			end_status_str[tapstate.from],
			end_status_str[tapstate.endstate], tapstate.reset,
			tapstate.tck);

		err = jtag->ops->status_set(jtag, &tapstate);
		break;

	case JTAG_IOCXFER:
	{
		u8 ubit_mask = GENMASK(7, 0);
		u8 remaining_bits = 0x0;
		union pad_config padding;

		if (copy_from_user(&xfer, (const void __user *)arg,
				   sizeof(struct jtag_xfer)))
			return -EFAULT;

		if (xfer.length >= JTAG_MAX_XFER_DATA_LEN)
			return -EINVAL;

		if (xfer.type > JTAG_SDR_XFER)
			return -EINVAL;

		if (xfer.direction > JTAG_READ_WRITE_XFER)
			return -EINVAL;

		if (xfer.from > JTAG_STATE_CURRENT)
			return -EINVAL;

		if (xfer.endstate > JTAG_STATE_CURRENT)
			return -EINVAL;

		data_size = DIV_ROUND_UP(xfer.length, BITS_PER_BYTE);
		xfer_data = memdup_user(u64_to_user_ptr(xfer.tdio), data_size);

		/* Save unused remaining bits in this transfer */
		if ((xfer.length % BITS_PER_BYTE)) {
			ubit_mask = GENMASK((xfer.length % BITS_PER_BYTE) - 1,
					    0);
			remaining_bits = xfer_data[data_size - 1] & ~ubit_mask;
		}

		if (IS_ERR(xfer_data))
			return -EFAULT;
		padding.int_value = xfer.padding;
		dev_dbg(jtag->miscdev.parent,
			"JTAG_IOCXFER: type: %s direction: %d, END : %s, padding: (value: %d) pre_pad: %d post_pad: %d, len: %d\n",
			xfer.type ? "DR" : "IR", xfer.direction,
			end_status_str[xfer.endstate], padding.pad_data,
			padding.pre_pad_number, padding.post_pad_number,
			xfer.length);

		print_hex_dump_debug("I:", DUMP_PREFIX_NONE, 16, 1, xfer_data,
				     data_size, false);

		err = jtag->ops->xfer(jtag, &xfer, xfer_data);
		if (err) {
			kfree(xfer_data);
			return err;
		}

		print_hex_dump_debug("O:", DUMP_PREFIX_NONE, 16, 1, xfer_data,
				     data_size, false);

		/* Restore unused remaining bits in this transfer */
		xfer_data[data_size - 1] = (xfer_data[data_size - 1]
					    & ubit_mask) | remaining_bits;

		err = copy_to_user(u64_to_user_ptr(xfer.tdio),
				   (void *)xfer_data, data_size);
		kfree(xfer_data);
		if (err)
			return -EFAULT;

		if (copy_to_user((void __user *)arg, (void *)&xfer,
				 sizeof(struct jtag_xfer)))
			return -EFAULT;
		break;
	}

	case JTAG_GIOCSTATUS:
		err = jtag->ops->status_get(jtag, &value);
		if (err)
			break;
		dev_dbg(jtag->miscdev.parent, "JTAG_GIOCSTATUS: status get %s",
			end_status_str[value]);

		err = put_user(value, (__u32 __user *)arg);
		break;
	case JTAG_IOCBITBANG:
		if (copy_from_user(&bitbang, (const void __user *)arg,
				   sizeof(struct bitbang_packet)))
			return -EFAULT;

		if (bitbang.length >= JTAG_MAX_XFER_DATA_LEN)
			return -EINVAL;

		data_size = bitbang.length * sizeof(struct tck_bitbang);
		bitbang_data = memdup_user((void __user *)bitbang.data,
					   data_size);
		if (IS_ERR(bitbang_data))
			return -EFAULT;

		err = jtag->ops->bitbang(jtag, &bitbang, bitbang_data);
		if (err) {
			kfree(bitbang_data);
			return err;
		}
		err = copy_to_user((void __user *)bitbang.data,
				   (void *)bitbang_data, data_size);
		kfree(bitbang_data);
		if (err)
			return -EFAULT;
		break;
	case JTAG_SIOCMODE:
		if (!jtag->ops->mode_set)
			return -EOPNOTSUPP;

		if (copy_from_user(&mode, (const void __user *)arg,
				   sizeof(struct jtag_mode)))
			return -EFAULT;

		dev_dbg(jtag->miscdev.parent,
			"JTAG_SIOCMODE: mode set feature %d mode %d",
			mode.feature, mode.mode);
		err = jtag->ops->mode_set(jtag, &mode);
		break;

	default:
		return -EINVAL;
	}
	return err;
}

static int jtag_open(struct inode *inode, struct file *file)
{
	struct jtag *jtag = container_of(file->private_data,
					 struct jtag,
					 miscdev);

	file->private_data = jtag;
	if (jtag->ops->enable(jtag))
		return -EBUSY;
	return nonseekable_open(inode, file);
}

static int jtag_release(struct inode *inode, struct file *file)
{
	struct jtag *jtag = file->private_data;

	if (jtag->ops->disable(jtag))
		return -EBUSY;
	return 0;
}

static const struct file_operations jtag_fops = {
	.owner		= THIS_MODULE,
	.open		= jtag_open,
	.llseek		= noop_llseek,
	.unlocked_ioctl	= jtag_ioctl,
	.release	= jtag_release,
};

struct jtag *jtag_alloc(struct device *host, size_t priv_size,
			const struct jtag_ops *ops)
{
	struct jtag *jtag;

	if (!host)
		return NULL;

	if (!ops)
		return NULL;

	if (!ops->status_set || !ops->status_get || !ops->xfer)
		return NULL;

	jtag = kzalloc(sizeof(*jtag) + priv_size, GFP_KERNEL);
	if (!jtag)
		return NULL;

	jtag->ops = ops;
	jtag->miscdev.parent = host;

	return jtag;
}
EXPORT_SYMBOL_GPL(jtag_alloc);

void jtag_free(struct jtag *jtag)
{
	kfree(jtag);
}
EXPORT_SYMBOL_GPL(jtag_free);

static int jtag_register(struct jtag *jtag)
{
	struct device *dev = jtag->miscdev.parent;
	int err;
	int id;

	if (!dev)
		return -ENODEV;

	id = ida_simple_get(&jtag_ida, 0, 0, GFP_KERNEL);
	if (id < 0)
		return id;

	jtag->id = id;

	jtag->miscdev.fops =  &jtag_fops;
	jtag->miscdev.minor = MISC_DYNAMIC_MINOR;
	jtag->miscdev.name = kasprintf(GFP_KERNEL, "jtag%d", id);
	if (!jtag->miscdev.name) {
		err = -ENOMEM;
		goto err_jtag_alloc;
	}

	err = misc_register(&jtag->miscdev);
	if (err) {
		dev_err(jtag->miscdev.parent, "Unable to register device\n");
		goto err_jtag_name;
	}
	return 0;

err_jtag_name:
	kfree(jtag->miscdev.name);
err_jtag_alloc:
	ida_simple_remove(&jtag_ida, id);
	return err;
}

static void jtag_unregister(struct jtag *jtag)
{
	misc_deregister(&jtag->miscdev);
	kfree(jtag->miscdev.name);
	ida_simple_remove(&jtag_ida, jtag->id);
}

static void devm_jtag_unregister(struct device *dev, void *res)
{
	jtag_unregister(*(struct jtag **)res);
}

int devm_jtag_register(struct device *dev, struct jtag *jtag)
{
	struct jtag **ptr;
	int ret;

	ptr = devres_alloc(devm_jtag_unregister, sizeof(struct jtag *),
			   GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = jtag_register(jtag);
	if (!ret) {
		*ptr = jtag;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(devm_jtag_register);

static void __exit jtag_exit(void)
{
	ida_destroy(&jtag_ida);
}

module_exit(jtag_exit);

MODULE_AUTHOR("Oleksandr Shamray <oleksandrs@mellanox.com>");
MODULE_DESCRIPTION("Generic jtag support");
MODULE_LICENSE("GPL v2");
