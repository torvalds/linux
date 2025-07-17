// SPDX-License-Identifier: GPL-2.0
/*
 * CZ.NIC's Turris Omnia MCU ECDSA message signing via keyctl
 *
 * 2025 by Marek Behún <kabel@kernel.org>
 */

#include <crypto/sha2.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/key.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/types.h>

#include <linux/turris-omnia-mcu-interface.h>
#include <linux/turris-signing-key.h>
#include "turris-omnia-mcu.h"

static irqreturn_t omnia_msg_signed_irq_handler(int irq, void *dev_id)
{
	u8 reply[1 + OMNIA_MCU_CRYPTO_SIGNATURE_LEN];
	struct omnia_mcu *mcu = dev_id;
	int err;

	err = omnia_cmd_read(mcu->client, OMNIA_CMD_CRYPTO_COLLECT_SIGNATURE,
			     reply, sizeof(reply));
	if (!err && reply[0] != OMNIA_MCU_CRYPTO_SIGNATURE_LEN)
		err = -EIO;

	guard(mutex)(&mcu->sign_lock);

	if (mcu->sign_requested) {
		mcu->sign_err = err;
		if (!err)
			memcpy(mcu->signature, &reply[1],
			       OMNIA_MCU_CRYPTO_SIGNATURE_LEN);
		mcu->sign_requested = false;
		complete(&mcu->msg_signed);
	}

	return IRQ_HANDLED;
}

static int omnia_mcu_sign(const struct key *key, const void *msg,
			  void *signature)
{
	struct omnia_mcu *mcu = dev_get_drvdata(turris_signing_key_get_dev(key));
	u8 cmd[1 + SHA256_DIGEST_SIZE], reply;
	int err;

	scoped_guard(mutex, &mcu->sign_lock) {
		if (mcu->sign_requested)
			return -EBUSY;

		cmd[0] = OMNIA_CMD_CRYPTO_SIGN_MESSAGE;
		memcpy(&cmd[1], msg, SHA256_DIGEST_SIZE);

		err = omnia_cmd_write_read(mcu->client, cmd, sizeof(cmd),
					   &reply, 1);
		if (err)
			return err;

		if (!reply)
			return -EBUSY;

		mcu->sign_requested = true;
	}

	if (wait_for_completion_interruptible(&mcu->msg_signed))
		return -EINTR;

	guard(mutex)(&mcu->sign_lock);

	if (mcu->sign_err)
		return mcu->sign_err;

	memcpy(signature, mcu->signature, OMNIA_MCU_CRYPTO_SIGNATURE_LEN);

	/* forget the signature, for security */
	memzero_explicit(mcu->signature, sizeof(mcu->signature));

	return OMNIA_MCU_CRYPTO_SIGNATURE_LEN;
}

static const void *omnia_mcu_get_public_key(const struct key *key)
{
	struct omnia_mcu *mcu = dev_get_drvdata(turris_signing_key_get_dev(key));

	return mcu->board_public_key;
}

static const struct turris_signing_key_subtype omnia_signing_key_subtype = {
	.key_size		= 256,
	.data_size		= SHA256_DIGEST_SIZE,
	.sig_size		= OMNIA_MCU_CRYPTO_SIGNATURE_LEN,
	.public_key_size	= OMNIA_MCU_CRYPTO_PUBLIC_KEY_LEN,
	.hash_algo		= "sha256",
	.get_public_key		= omnia_mcu_get_public_key,
	.sign			= omnia_mcu_sign,
};

static int omnia_mcu_read_public_key(struct omnia_mcu *mcu)
{
	u8 reply[1 + OMNIA_MCU_CRYPTO_PUBLIC_KEY_LEN];
	int err;

	err = omnia_cmd_read(mcu->client, OMNIA_CMD_CRYPTO_GET_PUBLIC_KEY,
			     reply, sizeof(reply));
	if (err)
		return err;

	if (reply[0] != OMNIA_MCU_CRYPTO_PUBLIC_KEY_LEN)
		return -EIO;

	memcpy(mcu->board_public_key, &reply[1],
	       OMNIA_MCU_CRYPTO_PUBLIC_KEY_LEN);

	return 0;
}

int omnia_mcu_register_keyctl(struct omnia_mcu *mcu)
{
	struct device *dev = &mcu->client->dev;
	char desc[48];
	int err;

	if (!(mcu->features & OMNIA_FEAT_CRYPTO))
		return 0;

	err = omnia_mcu_read_public_key(mcu);
	if (err)
		return dev_err_probe(dev, err,
				     "Cannot read board public key\n");

	err = devm_mutex_init(dev, &mcu->sign_lock);
	if (err)
		return err;

	init_completion(&mcu->msg_signed);

	err = omnia_mcu_request_irq(mcu, OMNIA_INT_MESSAGE_SIGNED,
				    omnia_msg_signed_irq_handler,
				    "turris-omnia-mcu-keyctl");
	if (err)
		return dev_err_probe(dev, err,
				     "Cannot request MESSAGE_SIGNED IRQ\n");

	sprintf(desc, "Turris Omnia SN %016llX MCU ECDSA key",
		mcu->board_serial_number);

	err = devm_turris_signing_key_create(dev, &omnia_signing_key_subtype,
					     desc);
	if (err)
		return dev_err_probe(dev, err, "Cannot create signing key\n");

	return 0;
}
