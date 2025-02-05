// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Crypto driver file to manage keys of NVIDIA Security Engine.
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <crypto/aes.h>

#include "tegra-se.h"

#define SE_KEY_FULL_MASK		GENMASK(SE_MAX_KEYSLOT, 0)

/* Reserve keyslot 0, 14, 15 */
#define SE_KEY_RSVD_MASK		(BIT(0) | BIT(14) | BIT(15))
#define SE_KEY_VALID_MASK		(SE_KEY_FULL_MASK & ~SE_KEY_RSVD_MASK)

/* Mutex lock to guard keyslots */
static DEFINE_MUTEX(kslt_lock);

/* Keyslot bitmask (0 = available, 1 = in use/not available) */
static u16 tegra_se_keyslots = SE_KEY_RSVD_MASK;

static u16 tegra_keyslot_alloc(void)
{
	u16 keyid;

	mutex_lock(&kslt_lock);
	/* Check if all key slots are full */
	if (tegra_se_keyslots == GENMASK(SE_MAX_KEYSLOT, 0)) {
		mutex_unlock(&kslt_lock);
		return 0;
	}

	keyid = ffz(tegra_se_keyslots);
	tegra_se_keyslots |= BIT(keyid);

	mutex_unlock(&kslt_lock);

	return keyid;
}

static void tegra_keyslot_free(u16 slot)
{
	mutex_lock(&kslt_lock);
	tegra_se_keyslots &= ~(BIT(slot));
	mutex_unlock(&kslt_lock);
}

static unsigned int tegra_key_prep_ins_cmd(struct tegra_se *se, u32 *cpuvaddr,
					   const u32 *key, u32 keylen, u16 slot, u32 alg)
{
	int i = 0, j;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->op);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_DUMMY;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->manifest);
	cpuvaddr[i++] = se->manifest(se->owner, alg, keylen);
	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->key_dst);

	cpuvaddr[i++] = SE_AES_KEY_DST_INDEX(slot);

	for (j = 0; j < keylen / 4; j++) {
		/* Set key address */
		cpuvaddr[i++] = host1x_opcode_setpayload(1);
		cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->key_addr);
		cpuvaddr[i++] = j;

		/* Set key data */
		cpuvaddr[i++] = host1x_opcode_setpayload(1);
		cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->key_data);
		cpuvaddr[i++] = key[j];
	}

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->config);
	cpuvaddr[i++] = SE_CFG_INS;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->op);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_START |
			SE_AES_OP_LASTBUF;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "key-slot %u key-manifest %#x\n",
		slot, se->manifest(se->owner, alg, keylen));

	return i;
}

static bool tegra_key_in_kslt(u32 keyid)
{
	bool ret;

	if (keyid > SE_MAX_KEYSLOT)
		return false;

	mutex_lock(&kslt_lock);
	ret = ((BIT(keyid) & SE_KEY_VALID_MASK) &&
		(BIT(keyid) & tegra_se_keyslots));
	mutex_unlock(&kslt_lock);

	return ret;
}

static int tegra_key_insert(struct tegra_se *se, const u8 *key,
			    u32 keylen, u16 slot, u32 alg)
{
	const u32 *keyval = (u32 *)key;
	u32 *addr = se->cmdbuf->addr, size;

	size = tegra_key_prep_ins_cmd(se, addr, keyval, keylen, slot, alg);

	return tegra_se_host1x_submit(se, size);
}

void tegra_key_invalidate(struct tegra_se *se, u32 keyid, u32 alg)
{
	u8 zkey[AES_MAX_KEY_SIZE] = {0};

	if (!keyid)
		return;

	/* Overwrite the key with 0s */
	tegra_key_insert(se, zkey, AES_MAX_KEY_SIZE, keyid, alg);

	tegra_keyslot_free(keyid);
}

int tegra_key_submit(struct tegra_se *se, const u8 *key, u32 keylen, u32 alg, u32 *keyid)
{
	int ret;

	/* Use the existing slot if it is already allocated */
	if (!tegra_key_in_kslt(*keyid)) {
		*keyid = tegra_keyslot_alloc();
		if (!(*keyid)) {
			dev_err(se->dev, "failed to allocate key slot\n");
			return -ENOMEM;
		}
	}

	ret = tegra_key_insert(se, key, keylen, *keyid, alg);
	if (ret)
		return ret;

	return 0;
}
