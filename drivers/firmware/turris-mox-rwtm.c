// SPDX-License-Identifier: GPL-2.0
/*
 * Turris Mox rWTM firmware driver
 *
 * Copyright (C) 2019, 2024, 2025 Marek Behún <kabel@kernel.org>
 */

#include <crypto/sha2.h>
#include <linux/align.h>
#include <linux/armada-37xx-rwtm-mailbox.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/if_ether.h>
#include <linux/key.h>
#include <linux/kobject.h>
#include <linux/mailbox_client.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/sysfs.h>
#include <linux/turris-signing-key.h>
#include <linux/types.h>

#define DRIVER_NAME		"turris-mox-rwtm"

#define RWTM_DMA_BUFFER_SIZE	SZ_4K

/*
 * The macros and constants below come from Turris Mox's rWTM firmware code.
 * This firmware is open source and it's sources can be found at
 * https://gitlab.labs.nic.cz/turris/mox-boot-builder/tree/master/wtmi.
 */

enum {
	MOX_ECC_NUM_BITS	= 521,
	MOX_ECC_NUM_LEN		= DIV_ROUND_UP(MOX_ECC_NUM_BITS, 8),
	MOX_ECC_NUM_WORDS	= DIV_ROUND_UP(MOX_ECC_NUM_BITS, 32),
	MOX_ECC_SIG_LEN		= 2 * MOX_ECC_NUM_LEN,
	MOX_ECC_PUBKEY_LEN	= 1 + MOX_ECC_NUM_LEN,
};

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

/**
 * struct mox_rwtm - driver private data structure
 * @mbox_client:	rWTM mailbox client
 * @mbox:		rWTM mailbox channel
 * @hwrng:		RNG driver structure
 * @reply:		last mailbox reply, filled in receive callback
 * @buf:		DMA buffer
 * @buf_phys:		physical address of the DMA buffer
 * @busy:		mutex to protect mailbox command execution
 * @cmd_done:		command done completion
 * @has_board_info:	whether board information is present
 * @serial_number:	serial number of the device
 * @board_version:	board version / revision of the device
 * @ram_size:		RAM size of the device
 * @mac_address1:	first MAC address of the device
 * @mac_address2:	second MAC address of the device
 * @pubkey:		board ECDSA public key
 */
struct mox_rwtm {
	struct mbox_client mbox_client;
	struct mbox_chan *mbox;
	struct hwrng hwrng;

	struct armada_37xx_rwtm_rx_msg reply;

	void *buf;
	dma_addr_t buf_phys;

	struct mutex busy;
	struct completion cmd_done;

	bool has_board_info;
	u64 serial_number;
	int board_version, ram_size;
	u8 mac_address1[ETH_ALEN], mac_address2[ETH_ALEN];

#ifdef CONFIG_TURRIS_MOX_RWTM_KEYCTL
	u8 pubkey[MOX_ECC_PUBKEY_LEN];
#endif
};

static inline struct device *rwtm_dev(struct mox_rwtm *rwtm)
{
	return rwtm->mbox_client.dev;
}

#define MOX_ATTR_RO(name, format)				\
static ssize_t							\
name##_show(struct device *dev, struct device_attribute *a,	\
	    char *buf)						\
{								\
	struct mox_rwtm *rwtm = dev_get_drvdata(dev);		\
	if (!rwtm->has_board_info)				\
		return -ENODATA;				\
	return sysfs_emit(buf, format, rwtm->name);		\
}								\
static DEVICE_ATTR_RO(name)

MOX_ATTR_RO(serial_number, "%016llX\n");
MOX_ATTR_RO(board_version, "%i\n");
MOX_ATTR_RO(ram_size, "%i\n");
MOX_ATTR_RO(mac_address1, "%pM\n");
MOX_ATTR_RO(mac_address2, "%pM\n");

static struct attribute *turris_mox_rwtm_attrs[] = {
	&dev_attr_serial_number.attr,
	&dev_attr_board_version.attr,
	&dev_attr_ram_size.attr,
	&dev_attr_mac_address1.attr,
	&dev_attr_mac_address2.attr,
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

#ifdef CONFIG_TURRIS_MOX_RWTM_KEYCTL

static void mox_ecc_number_to_bin(void *dst, const u32 *src)
{
	__be32 tmp[MOX_ECC_NUM_WORDS];

	cpu_to_be32_array(tmp, src, MOX_ECC_NUM_WORDS);

	memcpy(dst, (void *)tmp + 2, MOX_ECC_NUM_LEN);
}

static void mox_ecc_public_key_to_bin(void *dst, u32 src_first,
				      const u32 *src_rest)
{
	__be32 tmp[MOX_ECC_NUM_WORDS - 1];
	u8 *p = dst;

	/* take 3 bytes from the first word */
	*p++ = src_first >> 16;
	*p++ = src_first >> 8;
	*p++ = src_first;

	/* take the rest of the words */
	cpu_to_be32_array(tmp, src_rest, MOX_ECC_NUM_WORDS - 1);
	memcpy(p, tmp, sizeof(tmp));
}

static int mox_rwtm_sign(const struct key *key, const void *data, void *signature)
{
	struct mox_rwtm *rwtm = dev_get_drvdata(turris_signing_key_get_dev(key));
	struct armada_37xx_rwtm_tx_msg msg = {};
	u32 offset_r, offset_s;
	int ret;

	guard(mutex)(&rwtm->busy);

	/*
	 * For MBOX_CMD_SIGN command:
	 *   args[0] - must be 1
	 *   args[1] - address of message M to sign; message is a 521-bit number
	 *   args[2] - address where the R part of the signature will be stored
	 *   args[3] - address where the S part of the signature will be stored
	 *
	 * M, R and S are 521-bit numbers encoded as seventeen 32-bit words,
	 * most significat word first.
	 * Since the message in @data is a sha512 digest, the most significat
	 * word is always zero.
	 */

	offset_r = MOX_ECC_NUM_WORDS * sizeof(u32);
	offset_s = 2 * MOX_ECC_NUM_WORDS * sizeof(u32);

	memset(rwtm->buf, 0, sizeof(u32));
	memcpy(rwtm->buf + sizeof(u32), data, SHA512_DIGEST_SIZE);
	be32_to_cpu_array(rwtm->buf, rwtm->buf, MOX_ECC_NUM_WORDS);

	msg.args[0] = 1;
	msg.args[1] = rwtm->buf_phys;
	msg.args[2] = rwtm->buf_phys + offset_r;
	msg.args[3] = rwtm->buf_phys + offset_s;

	ret = mox_rwtm_exec(rwtm, MBOX_CMD_SIGN, &msg, true);
	if (ret < 0)
		return ret;

	/* convert R and S parts of the signature */
	mox_ecc_number_to_bin(signature, rwtm->buf + offset_r);
	mox_ecc_number_to_bin(signature + MOX_ECC_NUM_LEN, rwtm->buf + offset_s);

	return 0;
}

static const void *mox_rwtm_get_public_key(const struct key *key)
{
	struct mox_rwtm *rwtm = dev_get_drvdata(turris_signing_key_get_dev(key));

	return rwtm->pubkey;
}

static const struct turris_signing_key_subtype mox_signing_key_subtype = {
	.key_size		= MOX_ECC_NUM_BITS,
	.data_size		= SHA512_DIGEST_SIZE,
	.sig_size		= MOX_ECC_SIG_LEN,
	.public_key_size	= MOX_ECC_PUBKEY_LEN,
	.hash_algo		= "sha512",
	.get_public_key		= mox_rwtm_get_public_key,
	.sign			= mox_rwtm_sign,
};

static int mox_register_signing_key(struct mox_rwtm *rwtm)
{
	struct armada_37xx_rwtm_rx_msg *reply = &rwtm->reply;
	struct device *dev = rwtm_dev(rwtm);
	int ret;

	ret = mox_rwtm_exec(rwtm, MBOX_CMD_ECDSA_PUB_KEY, NULL, false);
	if (ret == -ENODATA) {
		dev_warn(dev, "Board has no public key burned!\n");
	} else if (ret == -EOPNOTSUPP) {
		dev_notice(dev,
			   "Firmware does not support the ECDSA_PUB_KEY command\n");
	} else if (ret < 0) {
		return ret;
	} else {
		char sn[17] = "unknown";
		char desc[46];

		if (rwtm->has_board_info)
			sprintf(sn, "%016llX", rwtm->serial_number);

		sprintf(desc, "Turris MOX SN %s rWTM ECDSA key", sn);

		mox_ecc_public_key_to_bin(rwtm->pubkey, ret, reply->status);

		ret = devm_turris_signing_key_create(dev,
						     &mox_signing_key_subtype,
						     desc);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Cannot create signing key\n");
	}

	return 0;
}

#else /* CONFIG_TURRIS_MOX_RWTM_KEYCTL */

static inline int mox_register_signing_key(struct mox_rwtm *rwtm)
{
	return 0;
}

#endif /* !CONFIG_TURRIS_MOX_RWTM_KEYCTL */

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

	ret = mox_register_signing_key(rwtm);
	if (ret < 0)
		return ret;

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
