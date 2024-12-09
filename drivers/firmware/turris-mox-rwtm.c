// SPDX-License-Identifier: GPL-2.0
/*
 * Turris Mox rWTM firmware driver
 *
 * Copyright (C) 2019, 2024 Marek Behún <kabel@kernel.org>
 */

#include <crypto/sha2.h>
#include <linux/align.h>
#include <linux/armada-37xx-rwtm-mailbox.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/hw_random.h>
#include <linux/if_ether.h>
#include <linux/kobject.h>
#include <linux/mailbox_client.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#define DRIVER_NAME		"turris-mox-rwtm"

#define RWTM_DMA_BUFFER_SIZE	SZ_4K

/*
 * The macros and constants below come from Turris Mox's rWTM firmware code.
 * This firmware is open source and it's sources can be found at
 * https://gitlab.labs.nic.cz/turris/mox-boot-builder/tree/master/wtmi.
 */

#define MOX_ECC_NUMBER_WORDS	17
#define MOX_ECC_NUMBER_LEN	(MOX_ECC_NUMBER_WORDS * sizeof(u32))

#define MOX_ECC_SIGNATURE_WORDS	(2 * MOX_ECC_NUMBER_WORDS)

#define MBOX_STS_SUCCESS	(0 << 30)
#define MBOX_STS_FAIL		(1 << 30)
#define MBOX_STS_BADCMD		(2 << 30)
#define MBOX_STS_ERROR(s)	((s) & (3 << 30))
#define MBOX_STS_VALUE(s)	(((s) >> 10) & 0xfffff)
#define MBOX_STS_CMD(s)		((s) & 0x3ff)

enum mbox_cmd {
	MBOX_CMD_GET_RANDOM	= 1,
	MBOX_CMD_BOARD_INFO	= 2,
	MBOX_CMD_ECDSA_PUB_KEY	= 3,
	MBOX_CMD_HASH		= 4,
	MBOX_CMD_SIGN		= 5,
	MBOX_CMD_VERIFY		= 6,

	MBOX_CMD_OTP_READ	= 7,
	MBOX_CMD_OTP_WRITE	= 8,
};

struct mox_rwtm {
	struct mbox_client mbox_client;
	struct mbox_chan *mbox;
	struct hwrng hwrng;

	struct armada_37xx_rwtm_rx_msg reply;

	void *buf;
	dma_addr_t buf_phys;

	struct mutex busy;
	struct completion cmd_done;

	/* board information */
	bool has_board_info;
	u64 serial_number;
	int board_version, ram_size;
	u8 mac_address1[ETH_ALEN], mac_address2[ETH_ALEN];

	/* public key burned in eFuse */
	bool has_pubkey;
	u8 pubkey[135];

#ifdef CONFIG_DEBUG_FS
	/*
	 * Signature process. This is currently done via debugfs, because it
	 * does not conform to the sysfs standard "one file per attribute".
	 * It should be rewritten via crypto API once akcipher API is available
	 * from userspace.
	 */
	u32 last_sig[MOX_ECC_SIGNATURE_WORDS];
	bool last_sig_done;
#endif
};

static inline struct device *rwtm_dev(struct mox_rwtm *rwtm)
{
	return rwtm->mbox_client.dev;
}

#define MOX_ATTR_RO(name, format, cat)				\
static ssize_t							\
name##_show(struct device *dev, struct device_attribute *a,	\
	    char *buf)						\
{								\
	struct mox_rwtm *rwtm = dev_get_drvdata(dev);		\
	if (!rwtm->has_##cat)					\
		return -ENODATA;				\
	return sysfs_emit(buf, format, rwtm->name);		\
}								\
static DEVICE_ATTR_RO(name)

MOX_ATTR_RO(serial_number, "%016llX\n", board_info);
MOX_ATTR_RO(board_version, "%i\n", board_info);
MOX_ATTR_RO(ram_size, "%i\n", board_info);
MOX_ATTR_RO(mac_address1, "%pM\n", board_info);
MOX_ATTR_RO(mac_address2, "%pM\n", board_info);
MOX_ATTR_RO(pubkey, "%s\n", pubkey);

static struct attribute *turris_mox_rwtm_attrs[] = {
	&dev_attr_serial_number.attr,
	&dev_attr_board_version.attr,
	&dev_attr_ram_size.attr,
	&dev_attr_mac_address1.attr,
	&dev_attr_mac_address2.attr,
	&dev_attr_pubkey.attr,
	NULL
};
ATTRIBUTE_GROUPS(turris_mox_rwtm);

static int mox_get_status(enum mbox_cmd cmd, u32 retval)
{
	if (MBOX_STS_CMD(retval) != cmd)
		return -EIO;
	else if (MBOX_STS_ERROR(retval) == MBOX_STS_FAIL)
		return -(int)MBOX_STS_VALUE(retval);
	else if (MBOX_STS_ERROR(retval) == MBOX_STS_BADCMD)
		return -EOPNOTSUPP;
	else if (MBOX_STS_ERROR(retval) != MBOX_STS_SUCCESS)
		return -EIO;
	else
		return MBOX_STS_VALUE(retval);
}

static void mox_rwtm_rx_callback(struct mbox_client *cl, void *data)
{
	struct mox_rwtm *rwtm = dev_get_drvdata(cl->dev);
	struct armada_37xx_rwtm_rx_msg *msg = data;

	if (completion_done(&rwtm->cmd_done))
		return;

	rwtm->reply = *msg;
	complete(&rwtm->cmd_done);
}

static int mox_rwtm_exec(struct mox_rwtm *rwtm, enum mbox_cmd cmd,
			 struct armada_37xx_rwtm_tx_msg *msg,
			 bool interruptible)
{
	struct armada_37xx_rwtm_tx_msg _msg = {};
	int ret;

	if (!msg)
		msg = &_msg;

	msg->command = cmd;

	ret = mbox_send_message(rwtm->mbox, msg);
	if (ret < 0)
		return ret;

	if (interruptible) {
		ret = wait_for_completion_interruptible(&rwtm->cmd_done);
		if (ret < 0)
			return ret;
	} else {
		if (!wait_for_completion_timeout(&rwtm->cmd_done, HZ / 2))
			return -ETIMEDOUT;
	}

	return mox_get_status(cmd, rwtm->reply.retval);
}

static void reply_to_mac_addr(u8 *mac, u32 t1, u32 t2)
{
	mac[0] = t1 >> 8;
	mac[1] = t1;
	mac[2] = t2 >> 24;
	mac[3] = t2 >> 16;
	mac[4] = t2 >> 8;
	mac[5] = t2;
}

static int mox_get_board_info(struct mox_rwtm *rwtm)
{
	struct device *dev = rwtm_dev(rwtm);
	struct armada_37xx_rwtm_rx_msg *reply = &rwtm->reply;
	int ret;

	ret = mox_rwtm_exec(rwtm, MBOX_CMD_BOARD_INFO, NULL, false);
	if (ret == -ENODATA) {
		dev_warn(dev,
			 "Board does not have manufacturing information burned!\n");
	} else if (ret == -EOPNOTSUPP) {
		dev_notice(dev,
			   "Firmware does not support the BOARD_INFO command\n");
	} else if (ret < 0) {
		return ret;
	} else {
		rwtm->serial_number = reply->status[1];
		rwtm->serial_number <<= 32;
		rwtm->serial_number |= reply->status[0];
		rwtm->board_version = reply->status[2];
		rwtm->ram_size = reply->status[3];
		reply_to_mac_addr(rwtm->mac_address1, reply->status[4],
				  reply->status[5]);
		reply_to_mac_addr(rwtm->mac_address2, reply->status[6],
				  reply->status[7]);
		rwtm->has_board_info = true;

		pr_info("Turris Mox serial number %016llX\n",
			rwtm->serial_number);
		pr_info("           board version %i\n", rwtm->board_version);
		pr_info("           burned RAM size %i MiB\n", rwtm->ram_size);
	}

	ret = mox_rwtm_exec(rwtm, MBOX_CMD_ECDSA_PUB_KEY, NULL, false);
	if (ret == -ENODATA) {
		dev_warn(dev, "Board has no public key burned!\n");
	} else if (ret == -EOPNOTSUPP) {
		dev_notice(dev,
			   "Firmware does not support the ECDSA_PUB_KEY command\n");
	} else if (ret < 0) {
		return ret;
	} else {
		u32 *s = reply->status;

		rwtm->has_pubkey = true;
		sprintf(rwtm->pubkey,
			"%06x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
			ret, s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7],
			s[8], s[9], s[10], s[11], s[12], s[13], s[14], s[15]);
	}

	return 0;
}

static int check_get_random_support(struct mox_rwtm *rwtm)
{
	struct armada_37xx_rwtm_tx_msg msg = {
		.args = { 1, rwtm->buf_phys, 4 },
	};

	return mox_rwtm_exec(rwtm, MBOX_CMD_GET_RANDOM, &msg, false);
}

static int mox_hwrng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	struct mox_rwtm *rwtm = container_of(rng, struct mox_rwtm, hwrng);
	struct armada_37xx_rwtm_tx_msg msg = {
		.args = { 1, rwtm->buf_phys, ALIGN(max, 4) },
	};
	int ret;

	max = min(max, RWTM_DMA_BUFFER_SIZE);

	if (!wait) {
		if (!mutex_trylock(&rwtm->busy))
			return -EBUSY;
	} else {
		mutex_lock(&rwtm->busy);
	}

	ret = mox_rwtm_exec(rwtm, MBOX_CMD_GET_RANDOM, &msg, true);
	if (ret < 0)
		goto unlock_mutex;

	memcpy(data, rwtm->buf, max);
	ret = max;

unlock_mutex:
	mutex_unlock(&rwtm->busy);
	return ret;
}

#ifdef CONFIG_DEBUG_FS
static int rwtm_debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return nonseekable_open(inode, file);
}

static ssize_t do_sign_read(struct file *file, char __user *buf, size_t len,
			    loff_t *ppos)
{
	struct mox_rwtm *rwtm = file->private_data;
	ssize_t ret;

	/* only allow one read, of whole signature, from position 0 */
	if (*ppos != 0)
		return 0;

	if (len < sizeof(rwtm->last_sig))
		return -EINVAL;

	if (!rwtm->last_sig_done)
		return -ENODATA;

	ret = simple_read_from_buffer(buf, len, ppos, rwtm->last_sig,
				      sizeof(rwtm->last_sig));
	rwtm->last_sig_done = false;

	return ret;
}

static ssize_t do_sign_write(struct file *file, const char __user *buf,
			     size_t len, loff_t *ppos)
{
	struct mox_rwtm *rwtm = file->private_data;
	struct armada_37xx_rwtm_tx_msg msg;
	loff_t dummy = 0;
	ssize_t ret;

	if (len != SHA512_DIGEST_SIZE)
		return -EINVAL;

	/* if last result is not zero user has not read that information yet */
	if (rwtm->last_sig_done)
		return -EBUSY;

	if (!mutex_trylock(&rwtm->busy))
		return -EBUSY;

	/*
	 * Here we have to send:
	 *   1. Address of the input to sign.
	 *      The input is an array of 17 32-bit words, the first (most
	 *      significat) is 0, the rest 16 words are copied from the SHA-512
	 *      hash given by the user and converted from BE to LE.
	 *   2. Address of the buffer where ECDSA signature value R shall be
	 *      stored by the rWTM firmware.
	 *   3. Address of the buffer where ECDSA signature value S shall be
	 *      stored by the rWTM firmware.
	 */
	memset(rwtm->buf, 0, sizeof(u32));
	ret = simple_write_to_buffer(rwtm->buf + sizeof(u32),
				     SHA512_DIGEST_SIZE, &dummy, buf, len);
	if (ret < 0)
		goto unlock_mutex;
	be32_to_cpu_array(rwtm->buf, rwtm->buf, MOX_ECC_NUMBER_WORDS);

	msg.args[0] = 1;
	msg.args[1] = rwtm->buf_phys;
	msg.args[2] = rwtm->buf_phys + MOX_ECC_NUMBER_LEN;
	msg.args[3] = rwtm->buf_phys + 2 * MOX_ECC_NUMBER_LEN;

	ret = mox_rwtm_exec(rwtm, MBOX_CMD_SIGN, &msg, true);
	if (ret < 0)
		goto unlock_mutex;

	/*
	 * Here we read the R and S values of the ECDSA signature
	 * computed by the rWTM firmware and convert their words from
	 * LE to BE.
	 */
	memcpy(rwtm->last_sig, rwtm->buf + MOX_ECC_NUMBER_LEN,
	       sizeof(rwtm->last_sig));
	cpu_to_be32_array(rwtm->last_sig, rwtm->last_sig,
			  MOX_ECC_SIGNATURE_WORDS);
	rwtm->last_sig_done = true;

	mutex_unlock(&rwtm->busy);
	return len;
unlock_mutex:
	mutex_unlock(&rwtm->busy);
	return ret;
}

static const struct file_operations do_sign_fops = {
	.owner	= THIS_MODULE,
	.open	= rwtm_debug_open,
	.read	= do_sign_read,
	.write	= do_sign_write,
};

static void rwtm_debugfs_release(void *root)
{
	debugfs_remove_recursive(root);
}

static void rwtm_register_debugfs(struct mox_rwtm *rwtm)
{
	struct dentry *root;

	root = debugfs_create_dir("turris-mox-rwtm", NULL);

	debugfs_create_file_unsafe("do_sign", 0600, root, rwtm, &do_sign_fops);

	devm_add_action_or_reset(rwtm_dev(rwtm), rwtm_debugfs_release, root);
}
#else
static inline void rwtm_register_debugfs(struct mox_rwtm *rwtm)
{
}
#endif

static void rwtm_devm_mbox_release(void *mbox)
{
	mbox_free_channel(mbox);
}

static void rwtm_firmware_symlink_drop(void *parent)
{
	sysfs_remove_link(parent, DRIVER_NAME);
}

static int turris_mox_rwtm_probe(struct platform_device *pdev)
{
	struct mox_rwtm *rwtm;
	struct device *dev = &pdev->dev;
	int ret;

	rwtm = devm_kzalloc(dev, sizeof(*rwtm), GFP_KERNEL);
	if (!rwtm)
		return -ENOMEM;

	rwtm->buf = dmam_alloc_coherent(dev, RWTM_DMA_BUFFER_SIZE,
					&rwtm->buf_phys, GFP_KERNEL);
	if (!rwtm->buf)
		return -ENOMEM;

	platform_set_drvdata(pdev, rwtm);

	ret = devm_mutex_init(dev, &rwtm->busy);
	if (ret)
		return ret;

	init_completion(&rwtm->cmd_done);

	rwtm->mbox_client.dev = dev;
	rwtm->mbox_client.rx_callback = mox_rwtm_rx_callback;

	rwtm->mbox = mbox_request_channel(&rwtm->mbox_client, 0);
	if (IS_ERR(rwtm->mbox))
		return dev_err_probe(dev, PTR_ERR(rwtm->mbox),
				     "Cannot request mailbox channel!\n");

	ret = devm_add_action_or_reset(dev, rwtm_devm_mbox_release, rwtm->mbox);
	if (ret)
		return ret;

	ret = mox_get_board_info(rwtm);
	if (ret < 0)
		dev_warn(dev, "Cannot read board information: %i\n", ret);

	ret = check_get_random_support(rwtm);
	if (ret < 0) {
		dev_notice(dev,
			   "Firmware does not support the GET_RANDOM command\n");
		return ret;
	}

	rwtm->hwrng.name = DRIVER_NAME "_hwrng";
	rwtm->hwrng.read = mox_hwrng_read;

	ret = devm_hwrng_register(dev, &rwtm->hwrng);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot register HWRNG!\n");

	rwtm_register_debugfs(rwtm);

	dev_info(dev, "HWRNG successfully registered\n");

	/*
	 * For sysfs ABI compatibility, create symlink
	 * /sys/firmware/turris-mox-rwtm to this device's sysfs directory.
	 */
	ret = sysfs_create_link(firmware_kobj, &dev->kobj, DRIVER_NAME);
	if (!ret)
		devm_add_action_or_reset(dev, rwtm_firmware_symlink_drop,
					 firmware_kobj);

	return 0;
}

static const struct of_device_id turris_mox_rwtm_match[] = {
	{ .compatible = "cznic,turris-mox-rwtm", },
	{ .compatible = "marvell,armada-3700-rwtm-firmware", },
	{ },
};

MODULE_DEVICE_TABLE(of, turris_mox_rwtm_match);

static struct platform_driver turris_mox_rwtm_driver = {
	.probe	= turris_mox_rwtm_probe,
	.driver	= {
		.name		= DRIVER_NAME,
		.of_match_table	= turris_mox_rwtm_match,
		.dev_groups	= turris_mox_rwtm_groups,
	},
};
module_platform_driver(turris_mox_rwtm_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Turris Mox rWTM firmware driver");
MODULE_AUTHOR("Marek Behun <kabel@kernel.org>");
