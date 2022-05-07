// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2020 Marvell International Ltd. All rights reserved */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/string.h>

#include "prestera_dsa.h"

#define PRESTERA_DSA_W0_CMD		GENMASK(31, 30)
#define PRESTERA_DSA_W0_IS_TAGGED	BIT(29)
#define PRESTERA_DSA_W0_DEV_NUM		GENMASK(28, 24)
#define PRESTERA_DSA_W0_PORT_NUM	GENMASK(23, 19)
#define PRESTERA_DSA_W0_VPT		GENMASK(15, 13)
#define PRESTERA_DSA_W0_EXT_BIT		BIT(12)
#define PRESTERA_DSA_W0_VID		GENMASK(11, 0)

#define PRESTERA_DSA_W1_EXT_BIT		BIT(31)
#define PRESTERA_DSA_W1_CFI_BIT		BIT(30)
#define PRESTERA_DSA_W1_PORT_NUM	GENMASK(11, 10)

#define PRESTERA_DSA_W2_EXT_BIT		BIT(31)
#define PRESTERA_DSA_W2_PORT_NUM	BIT(20)

#define PRESTERA_DSA_W3_VID		GENMASK(30, 27)
#define PRESTERA_DSA_W3_DST_EPORT	GENMASK(23, 7)
#define PRESTERA_DSA_W3_DEV_NUM		GENMASK(6, 0)

#define PRESTERA_DSA_VID		GENMASK(15, 12)
#define PRESTERA_DSA_DEV_NUM		GENMASK(11, 5)

int prestera_dsa_parse(struct prestera_dsa *dsa, const u8 *dsa_buf)
{
	__be32 *dsa_words = (__be32 *)dsa_buf;
	enum prestera_dsa_cmd cmd;
	u32 words[4];
	u32 field;

	words[0] = ntohl(dsa_words[0]);
	words[1] = ntohl(dsa_words[1]);
	words[2] = ntohl(dsa_words[2]);
	words[3] = ntohl(dsa_words[3]);

	/* set the common parameters */
	cmd = (enum prestera_dsa_cmd)FIELD_GET(PRESTERA_DSA_W0_CMD, words[0]);

	/* only to CPU is supported */
	if (unlikely(cmd != PRESTERA_DSA_CMD_TO_CPU))
		return -EINVAL;

	if (FIELD_GET(PRESTERA_DSA_W0_EXT_BIT, words[0]) == 0)
		return -EINVAL;
	if (FIELD_GET(PRESTERA_DSA_W1_EXT_BIT, words[1]) == 0)
		return -EINVAL;
	if (FIELD_GET(PRESTERA_DSA_W2_EXT_BIT, words[2]) == 0)
		return -EINVAL;

	field = FIELD_GET(PRESTERA_DSA_W3_VID, words[3]);

	dsa->vlan.is_tagged = FIELD_GET(PRESTERA_DSA_W0_IS_TAGGED, words[0]);
	dsa->vlan.cfi_bit = FIELD_GET(PRESTERA_DSA_W1_CFI_BIT, words[1]);
	dsa->vlan.vpt = FIELD_GET(PRESTERA_DSA_W0_VPT, words[0]);
	dsa->vlan.vid = FIELD_GET(PRESTERA_DSA_W0_VID, words[0]);
	dsa->vlan.vid &= ~PRESTERA_DSA_VID;
	dsa->vlan.vid |= FIELD_PREP(PRESTERA_DSA_VID, field);

	field = FIELD_GET(PRESTERA_DSA_W3_DEV_NUM, words[3]);

	dsa->hw_dev_num = FIELD_GET(PRESTERA_DSA_W0_DEV_NUM, words[0]);
	dsa->hw_dev_num |= FIELD_PREP(PRESTERA_DSA_DEV_NUM, field);

	dsa->port_num = (FIELD_GET(PRESTERA_DSA_W0_PORT_NUM, words[0]) << 0) |
			(FIELD_GET(PRESTERA_DSA_W1_PORT_NUM, words[1]) << 5) |
			(FIELD_GET(PRESTERA_DSA_W2_PORT_NUM, words[2]) << 7);

	return 0;
}

int prestera_dsa_build(const struct prestera_dsa *dsa, u8 *dsa_buf)
{
	__be32 *dsa_words = (__be32 *)dsa_buf;
	u32 dev_num = dsa->hw_dev_num;
	u32 words[4] = { 0 };

	words[0] |= FIELD_PREP(PRESTERA_DSA_W0_CMD, PRESTERA_DSA_CMD_FROM_CPU);

	words[0] |= FIELD_PREP(PRESTERA_DSA_W0_DEV_NUM, dev_num);
	dev_num = FIELD_GET(PRESTERA_DSA_DEV_NUM, dev_num);
	words[3] |= FIELD_PREP(PRESTERA_DSA_W3_DEV_NUM, dev_num);

	words[3] |= FIELD_PREP(PRESTERA_DSA_W3_DST_EPORT, dsa->port_num);

	words[0] |= FIELD_PREP(PRESTERA_DSA_W0_EXT_BIT, 1);
	words[1] |= FIELD_PREP(PRESTERA_DSA_W1_EXT_BIT, 1);
	words[2] |= FIELD_PREP(PRESTERA_DSA_W2_EXT_BIT, 1);

	dsa_words[0] = htonl(words[0]);
	dsa_words[1] = htonl(words[1]);
	dsa_words[2] = htonl(words[2]);
	dsa_words[3] = htonl(words[3]);

	return 0;
}
