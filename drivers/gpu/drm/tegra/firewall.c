// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2010-2020 NVIDIA Corporation */

#include "drm.h"
#include "submit.h"
#include "uapi.h"

struct tegra_drm_firewall {
	struct tegra_drm_submit_data *submit;
	struct tegra_drm_client *client;
	u32 *data;
	u32 pos;
	u32 end;
	u32 class;
};

static int fw_next(struct tegra_drm_firewall *fw, u32 *word)
{
	if (fw->pos == fw->end)
		return -EINVAL;

	*word = fw->data[fw->pos++];

	return 0;
}

static bool fw_check_addr_valid(struct tegra_drm_firewall *fw, u32 offset)
{
	u32 i;

	for (i = 0; i < fw->submit->num_used_mappings; i++) {
		struct tegra_drm_mapping *m = fw->submit->used_mappings[i].mapping;

		if (offset >= m->iova && offset <= m->iova_end)
			return true;
	}

	return false;
}

static int fw_check_reg(struct tegra_drm_firewall *fw, u32 offset)
{
	bool is_addr;
	u32 word;
	int err;

	err = fw_next(fw, &word);
	if (err)
		return err;

	if (!fw->client->ops->is_addr_reg)
		return 0;

	is_addr = fw->client->ops->is_addr_reg(fw->client->base.dev, fw->class,
					       offset);

	if (!is_addr)
		return 0;

	if (!fw_check_addr_valid(fw, word))
		return -EINVAL;

	return 0;
}

static int fw_check_regs_seq(struct tegra_drm_firewall *fw, u32 offset,
			     u32 count, bool incr)
{
	u32 i;

	for (i = 0; i < count; i++) {
		if (fw_check_reg(fw, offset))
			return -EINVAL;

		if (incr)
			offset++;
	}

	return 0;
}

static int fw_check_regs_mask(struct tegra_drm_firewall *fw, u32 offset,
			      u16 mask)
{
	unsigned long bmask = mask;
	unsigned int bit;

	for_each_set_bit(bit, &bmask, 16) {
		if (fw_check_reg(fw, offset+bit))
			return -EINVAL;
	}

	return 0;
}

static int fw_check_regs_imm(struct tegra_drm_firewall *fw, u32 offset)
{
	bool is_addr;

	is_addr = fw->client->ops->is_addr_reg(fw->client->base.dev, fw->class,
					       offset);
	if (is_addr)
		return -EINVAL;

	return 0;
}

static int fw_check_class(struct tegra_drm_firewall *fw, u32 class)
{
	if (!fw->client->ops->is_valid_class) {
		if (class == fw->client->base.class)
			return 0;
		else
			return -EINVAL;
	}

	if (!fw->client->ops->is_valid_class(class))
		return -EINVAL;

	return 0;
}

enum {
	HOST1X_OPCODE_SETCLASS  = 0x00,
	HOST1X_OPCODE_INCR      = 0x01,
	HOST1X_OPCODE_NONINCR   = 0x02,
	HOST1X_OPCODE_MASK      = 0x03,
	HOST1X_OPCODE_IMM       = 0x04,
	HOST1X_OPCODE_RESTART   = 0x05,
	HOST1X_OPCODE_GATHER    = 0x06,
	HOST1X_OPCODE_SETSTRMID = 0x07,
	HOST1X_OPCODE_SETAPPID  = 0x08,
	HOST1X_OPCODE_SETPYLD   = 0x09,
	HOST1X_OPCODE_INCR_W    = 0x0a,
	HOST1X_OPCODE_NONINCR_W = 0x0b,
	HOST1X_OPCODE_GATHER_W  = 0x0c,
	HOST1X_OPCODE_RESTART_W = 0x0d,
	HOST1X_OPCODE_EXTEND    = 0x0e,
};

int tegra_drm_fw_validate(struct tegra_drm_client *client, u32 *data, u32 start,
			  u32 words, struct tegra_drm_submit_data *submit,
			  u32 *job_class)
{
	struct tegra_drm_firewall fw = {
		.submit = submit,
		.client = client,
		.data = data,
		.pos = start,
		.end = start+words,
		.class = *job_class,
	};
	bool payload_valid = false;
	u32 payload;
	int err;

	while (fw.pos != fw.end) {
		u32 word, opcode, offset, count, mask, class;

		err = fw_next(&fw, &word);
		if (err)
			return err;

		opcode = (word & 0xf0000000) >> 28;

		switch (opcode) {
		case HOST1X_OPCODE_SETCLASS:
			offset = word >> 16 & 0xfff;
			mask = word & 0x3f;
			class = (word >> 6) & 0x3ff;
			err = fw_check_class(&fw, class);
			fw.class = class;
			*job_class = class;
			if (!err)
				err = fw_check_regs_mask(&fw, offset, mask);
			if (err)
				dev_warn(client->base.dev,
					 "illegal SETCLASS(offset=0x%x, mask=0x%x, class=0x%x) at word %u",
					 offset, mask, class, fw.pos-1);
			break;
		case HOST1X_OPCODE_INCR:
			offset = (word >> 16) & 0xfff;
			count = word & 0xffff;
			err = fw_check_regs_seq(&fw, offset, count, true);
			if (err)
				dev_warn(client->base.dev,
					 "illegal INCR(offset=0x%x, count=%u) in class 0x%x at word %u",
					 offset, count, fw.class, fw.pos-1);
			break;
		case HOST1X_OPCODE_NONINCR:
			offset = (word >> 16) & 0xfff;
			count = word & 0xffff;
			err = fw_check_regs_seq(&fw, offset, count, false);
			if (err)
				dev_warn(client->base.dev,
					 "illegal NONINCR(offset=0x%x, count=%u) in class 0x%x at word %u",
					 offset, count, fw.class, fw.pos-1);
			break;
		case HOST1X_OPCODE_MASK:
			offset = (word >> 16) & 0xfff;
			mask = word & 0xffff;
			err = fw_check_regs_mask(&fw, offset, mask);
			if (err)
				dev_warn(client->base.dev,
					 "illegal MASK(offset=0x%x, mask=0x%x) in class 0x%x at word %u",
					 offset, mask, fw.class, fw.pos-1);
			break;
		case HOST1X_OPCODE_IMM:
			/* IMM cannot reasonably be used to write a pointer */
			offset = (word >> 16) & 0xfff;
			err = fw_check_regs_imm(&fw, offset);
			if (err)
				dev_warn(client->base.dev,
					 "illegal IMM(offset=0x%x) in class 0x%x at word %u",
					 offset, fw.class, fw.pos-1);
			break;
		case HOST1X_OPCODE_SETPYLD:
			payload = word & 0xffff;
			payload_valid = true;
			break;
		case HOST1X_OPCODE_INCR_W:
			if (!payload_valid)
				return -EINVAL;

			offset = word & 0x3fffff;
			err = fw_check_regs_seq(&fw, offset, payload, true);
			if (err)
				dev_warn(client->base.dev,
					 "illegal INCR_W(offset=0x%x) in class 0x%x at word %u",
					 offset, fw.class, fw.pos-1);
			break;
		case HOST1X_OPCODE_NONINCR_W:
			if (!payload_valid)
				return -EINVAL;

			offset = word & 0x3fffff;
			err = fw_check_regs_seq(&fw, offset, payload, false);
			if (err)
				dev_warn(client->base.dev,
					 "illegal NONINCR(offset=0x%x) in class 0x%x at word %u",
					 offset, fw.class, fw.pos-1);
			break;
		default:
			dev_warn(client->base.dev, "illegal opcode at word %u",
				 fw.pos-1);
			return -EINVAL;
		}

		if (err)
			return err;
	}

	return 0;
}
