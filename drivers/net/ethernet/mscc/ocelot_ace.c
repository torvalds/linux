// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch driver
 * Copyright (c) 2019 Microsemi Corporation
 */

#include <linux/iopoll.h>
#include <linux/proc_fs.h>

#include <soc/mscc/ocelot_vcap.h>
#include "ocelot_police.h"
#include "ocelot_ace.h"
#include "ocelot_s2.h"

#define OCELOT_POLICER_DISCARD 0x17f
#define ENTRY_WIDTH 32

enum vcap_sel {
	VCAP_SEL_ENTRY = 0x1,
	VCAP_SEL_ACTION = 0x2,
	VCAP_SEL_COUNTER = 0x4,
	VCAP_SEL_ALL = 0x7,
};

enum vcap_cmd {
	VCAP_CMD_WRITE = 0, /* Copy from Cache to TCAM */
	VCAP_CMD_READ = 1, /* Copy from TCAM to Cache */
	VCAP_CMD_MOVE_UP = 2, /* Move <count> up */
	VCAP_CMD_MOVE_DOWN = 3, /* Move <count> down */
	VCAP_CMD_INITIALIZE = 4, /* Write all (from cache) */
};

#define VCAP_ENTRY_WIDTH 12 /* Max entry width (32bit words) */
#define VCAP_COUNTER_WIDTH 4 /* Max counter width (32bit words) */

struct vcap_data {
	u32 entry[VCAP_ENTRY_WIDTH]; /* ENTRY_DAT */
	u32 mask[VCAP_ENTRY_WIDTH]; /* MASK_DAT */
	u32 action[VCAP_ENTRY_WIDTH]; /* ACTION_DAT */
	u32 counter[VCAP_COUNTER_WIDTH]; /* CNT_DAT */
	u32 tg; /* TG_DAT */
	u32 type; /* Action type */
	u32 tg_sw; /* Current type-group */
	u32 cnt; /* Current counter */
	u32 key_offset; /* Current entry offset */
	u32 action_offset; /* Current action offset */
	u32 counter_offset; /* Current counter offset */
	u32 tg_value; /* Current type-group value */
	u32 tg_mask; /* Current type-group mask */
};

static u32 vcap_s2_read_update_ctrl(struct ocelot *ocelot)
{
	return ocelot_read(ocelot, S2_CORE_UPDATE_CTRL);
}

static void vcap_cmd(struct ocelot *ocelot, u16 ix, int cmd, int sel)
{
	const struct vcap_props *vcap_is2 = &ocelot->vcap[VCAP_IS2];

	u32 value = (S2_CORE_UPDATE_CTRL_UPDATE_CMD(cmd) |
		     S2_CORE_UPDATE_CTRL_UPDATE_ADDR(ix) |
		     S2_CORE_UPDATE_CTRL_UPDATE_SHOT);

	if ((sel & VCAP_SEL_ENTRY) && ix >= vcap_is2->entry_count)
		return;

	if (!(sel & VCAP_SEL_ENTRY))
		value |= S2_CORE_UPDATE_CTRL_UPDATE_ENTRY_DIS;

	if (!(sel & VCAP_SEL_ACTION))
		value |= S2_CORE_UPDATE_CTRL_UPDATE_ACTION_DIS;

	if (!(sel & VCAP_SEL_COUNTER))
		value |= S2_CORE_UPDATE_CTRL_UPDATE_CNT_DIS;

	ocelot_write(ocelot, value, S2_CORE_UPDATE_CTRL);
	readx_poll_timeout(vcap_s2_read_update_ctrl, ocelot, value,
				(value & S2_CORE_UPDATE_CTRL_UPDATE_SHOT) == 0,
				10, 100000);
}

/* Convert from 0-based row to VCAP entry row and run command */
static void vcap_row_cmd(struct ocelot *ocelot, u32 row, int cmd, int sel)
{
	const struct vcap_props *vcap_is2 = &ocelot->vcap[VCAP_IS2];

	vcap_cmd(ocelot, vcap_is2->entry_count - row - 1, cmd, sel);
}

static void vcap_entry2cache(struct ocelot *ocelot, struct vcap_data *data)
{
	const struct vcap_props *vcap_is2 = &ocelot->vcap[VCAP_IS2];
	u32 entry_words, i;

	entry_words = DIV_ROUND_UP(vcap_is2->entry_width, ENTRY_WIDTH);

	for (i = 0; i < entry_words; i++) {
		ocelot_write_rix(ocelot, data->entry[i], S2_CACHE_ENTRY_DAT, i);
		ocelot_write_rix(ocelot, ~data->mask[i], S2_CACHE_MASK_DAT, i);
	}
	ocelot_write(ocelot, data->tg, S2_CACHE_TG_DAT);
}

static void vcap_cache2entry(struct ocelot *ocelot, struct vcap_data *data)
{
	const struct vcap_props *vcap_is2 = &ocelot->vcap[VCAP_IS2];
	u32 entry_words, i;

	entry_words = DIV_ROUND_UP(vcap_is2->entry_width, ENTRY_WIDTH);

	for (i = 0; i < entry_words; i++) {
		data->entry[i] = ocelot_read_rix(ocelot, S2_CACHE_ENTRY_DAT, i);
		// Invert mask
		data->mask[i] = ~ocelot_read_rix(ocelot, S2_CACHE_MASK_DAT, i);
	}
	data->tg = ocelot_read(ocelot, S2_CACHE_TG_DAT);
}

static void vcap_action2cache(struct ocelot *ocelot, struct vcap_data *data)
{
	const struct vcap_props *vcap_is2 = &ocelot->vcap[VCAP_IS2];
	u32 action_words, i, width, mask;

	/* Encode action type */
	width = vcap_is2->action_type_width;
	if (width) {
		mask = GENMASK(width, 0);
		data->action[0] = ((data->action[0] & ~mask) | data->type);
	}

	action_words = DIV_ROUND_UP(vcap_is2->action_width, ENTRY_WIDTH);

	for (i = 0; i < action_words; i++)
		ocelot_write_rix(ocelot, data->action[i], S2_CACHE_ACTION_DAT,
				 i);

	for (i = 0; i < vcap_is2->counter_words; i++)
		ocelot_write_rix(ocelot, data->counter[i], S2_CACHE_CNT_DAT, i);
}

static void vcap_cache2action(struct ocelot *ocelot, struct vcap_data *data)
{
	const struct vcap_props *vcap_is2 = &ocelot->vcap[VCAP_IS2];
	u32 action_words, i, width;

	action_words = DIV_ROUND_UP(vcap_is2->action_width, ENTRY_WIDTH);

	for (i = 0; i < action_words; i++)
		data->action[i] = ocelot_read_rix(ocelot, S2_CACHE_ACTION_DAT,
						  i);

	for (i = 0; i < vcap_is2->counter_words; i++)
		data->counter[i] = ocelot_read_rix(ocelot, S2_CACHE_CNT_DAT, i);

	/* Extract action type */
	width = vcap_is2->action_type_width;
	data->type = (width ? (data->action[0] & GENMASK(width, 0)) : 0);
}

/* Calculate offsets for entry */
static void is2_data_get(struct ocelot *ocelot, struct vcap_data *data, int ix)
{
	const struct vcap_props *vcap_is2 = &ocelot->vcap[VCAP_IS2];
	u32 i, col, offset, count, cnt, base;
	u32 width = vcap_is2->tg_width;

	count = (data->tg_sw == VCAP_TG_HALF ? 2 : 4);
	col = (ix % 2);
	cnt = (vcap_is2->sw_count / count);
	base = (vcap_is2->sw_count - col * cnt - cnt);
	data->tg_value = 0;
	data->tg_mask = 0;
	for (i = 0; i < cnt; i++) {
		offset = ((base + i) * width);
		data->tg_value |= (data->tg_sw << offset);
		data->tg_mask |= GENMASK(offset + width - 1, offset);
	}

	/* Calculate key/action/counter offsets */
	col = (count - col - 1);
	data->key_offset = (base * vcap_is2->entry_width) / vcap_is2->sw_count;
	data->counter_offset = (cnt * col * vcap_is2->counter_width);
	i = data->type;
	width = vcap_is2->action_table[i].width;
	cnt = vcap_is2->action_table[i].count;
	data->action_offset =
		(((cnt * col * width) / count) + vcap_is2->action_type_width);
}

static void vcap_data_set(u32 *data, u32 offset, u32 len, u32 value)
{
	u32 i, v, m;

	for (i = 0; i < len; i++, offset++) {
		v = data[offset / ENTRY_WIDTH];
		m = (1 << (offset % ENTRY_WIDTH));
		if (value & (1 << i))
			v |= m;
		else
			v &= ~m;
		data[offset / ENTRY_WIDTH] = v;
	}
}

static u32 vcap_data_get(u32 *data, u32 offset, u32 len)
{
	u32 i, v, m, value = 0;

	for (i = 0; i < len; i++, offset++) {
		v = data[offset / ENTRY_WIDTH];
		m = (1 << (offset % ENTRY_WIDTH));
		if (v & m)
			value |= (1 << i);
	}
	return value;
}

static void vcap_key_field_set(struct vcap_data *data, u32 offset, u32 width,
			       u32 value, u32 mask)
{
	vcap_data_set(data->entry, offset + data->key_offset, width, value);
	vcap_data_set(data->mask, offset + data->key_offset, width, mask);
}

static void vcap_key_set(struct ocelot *ocelot, struct vcap_data *data,
			 enum vcap_is2_half_key_field field,
			 u32 value, u32 mask)
{
	u32 offset = ocelot->vcap_is2_keys[field].offset;
	u32 length = ocelot->vcap_is2_keys[field].length;

	vcap_key_field_set(data, offset, length, value, mask);
}

static void vcap_key_bytes_set(struct ocelot *ocelot, struct vcap_data *data,
			       enum vcap_is2_half_key_field field,
			       u8 *val, u8 *msk)
{
	u32 offset = ocelot->vcap_is2_keys[field].offset;
	u32 count  = ocelot->vcap_is2_keys[field].length;
	u32 i, j, n = 0, value = 0, mask = 0;

	WARN_ON(count % 8);

	/* Data wider than 32 bits are split up in chunks of maximum 32 bits.
	 * The 32 LSB of the data are written to the 32 MSB of the TCAM.
	 */
	offset += count;
	count /= 8;

	for (i = 0; i < count; i++) {
		j = (count - i - 1);
		value += (val[j] << n);
		mask += (msk[j] << n);
		n += 8;
		if (n == ENTRY_WIDTH || (i + 1) == count) {
			offset -= n;
			vcap_key_field_set(data, offset, n, value, mask);
			n = 0;
			value = 0;
			mask = 0;
		}
	}
}

static void vcap_key_l4_port_set(struct ocelot *ocelot, struct vcap_data *data,
				 enum vcap_is2_half_key_field field,
				 struct ocelot_vcap_udp_tcp *port)
{
	u32 offset = ocelot->vcap_is2_keys[field].offset;
	u32 length = ocelot->vcap_is2_keys[field].length;

	WARN_ON(length != 16);

	vcap_key_field_set(data, offset, length, port->value, port->mask);
}

static void vcap_key_bit_set(struct ocelot *ocelot, struct vcap_data *data,
			     enum vcap_is2_half_key_field field,
			     enum ocelot_vcap_bit val)
{
	u32 offset = ocelot->vcap_is2_keys[field].offset;
	u32 length = ocelot->vcap_is2_keys[field].length;
	u32 value = (val == OCELOT_VCAP_BIT_1 ? 1 : 0);
	u32 msk = (val == OCELOT_VCAP_BIT_ANY ? 0 : 1);

	WARN_ON(length != 1);

	vcap_key_field_set(data, offset, length, value, msk);
}

static void vcap_action_set(struct ocelot *ocelot, struct vcap_data *data,
			    enum vcap_is2_action_field field, u32 value)
{
	int offset = ocelot->vcap_is2_actions[field].offset;
	int length = ocelot->vcap_is2_actions[field].length;

	vcap_data_set(data->action, offset + data->action_offset, length,
		      value);
}

static void is2_action_set(struct ocelot *ocelot, struct vcap_data *data,
			   struct ocelot_ace_rule *ace)
{
	switch (ace->action) {
	case OCELOT_ACL_ACTION_DROP:
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_PORT_MASK, 0);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_MASK_MODE, 1);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_POLICE_ENA, 1);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_POLICE_IDX,
				OCELOT_POLICER_DISCARD);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_CPU_QU_NUM, 0);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_CPU_COPY_ENA, 0);
		break;
	case OCELOT_ACL_ACTION_TRAP:
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_PORT_MASK, 0);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_MASK_MODE, 1);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_POLICE_ENA, 0);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_POLICE_IDX, 0);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_CPU_QU_NUM, 0);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_CPU_COPY_ENA, 1);
		break;
	case OCELOT_ACL_ACTION_POLICE:
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_PORT_MASK, 0);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_MASK_MODE, 0);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_POLICE_ENA, 1);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_POLICE_IDX,
				ace->pol_ix);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_CPU_QU_NUM, 0);
		vcap_action_set(ocelot, data, VCAP_IS2_ACT_CPU_COPY_ENA, 0);
		break;
	}
}

static void is2_entry_set(struct ocelot *ocelot, int ix,
			  struct ocelot_ace_rule *ace)
{
	const struct vcap_props *vcap_is2 = &ocelot->vcap[VCAP_IS2];
	u32 val, msk, type, type_mask = 0xf, i, count;
	struct ocelot_ace_vlan *tag = &ace->vlan;
	struct ocelot_vcap_u64 payload;
	struct vcap_data data;
	int row = (ix / 2);

	memset(&payload, 0, sizeof(payload));
	memset(&data, 0, sizeof(data));

	/* Read row */
	vcap_row_cmd(ocelot, row, VCAP_CMD_READ, VCAP_SEL_ALL);
	vcap_cache2entry(ocelot, &data);
	vcap_cache2action(ocelot, &data);

	data.tg_sw = VCAP_TG_HALF;
	is2_data_get(ocelot, &data, ix);
	data.tg = (data.tg & ~data.tg_mask);
	if (ace->prio != 0)
		data.tg |= data.tg_value;

	data.type = IS2_ACTION_TYPE_NORMAL;

	vcap_key_set(ocelot, &data, VCAP_IS2_HK_PAG, 0, 0);
	vcap_key_set(ocelot, &data, VCAP_IS2_HK_IGR_PORT_MASK, 0,
		     ~ace->ingress_port_mask);
	vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_FIRST, OCELOT_VCAP_BIT_1);
	vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_HOST_MATCH,
			 OCELOT_VCAP_BIT_ANY);
	vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_L2_MC, ace->dmac_mc);
	vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_L2_BC, ace->dmac_bc);
	vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_VLAN_TAGGED, tag->tagged);
	vcap_key_set(ocelot, &data, VCAP_IS2_HK_VID,
		     tag->vid.value, tag->vid.mask);
	vcap_key_set(ocelot, &data, VCAP_IS2_HK_PCP,
		     tag->pcp.value[0], tag->pcp.mask[0]);
	vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_DEI, tag->dei);

	switch (ace->type) {
	case OCELOT_ACE_TYPE_ETYPE: {
		struct ocelot_ace_frame_etype *etype = &ace->frame.etype;

		type = IS2_TYPE_ETYPE;
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_L2_DMAC,
				   etype->dmac.value, etype->dmac.mask);
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_L2_SMAC,
				   etype->smac.value, etype->smac.mask);
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_MAC_ETYPE_ETYPE,
				   etype->etype.value, etype->etype.mask);
		/* Clear unused bits */
		vcap_key_set(ocelot, &data, VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD0,
			     0, 0);
		vcap_key_set(ocelot, &data, VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD1,
			     0, 0);
		vcap_key_set(ocelot, &data, VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD2,
			     0, 0);
		vcap_key_bytes_set(ocelot, &data,
				   VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD0,
				   etype->data.value, etype->data.mask);
		break;
	}
	case OCELOT_ACE_TYPE_LLC: {
		struct ocelot_ace_frame_llc *llc = &ace->frame.llc;

		type = IS2_TYPE_LLC;
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_L2_DMAC,
				   llc->dmac.value, llc->dmac.mask);
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_L2_SMAC,
				   llc->smac.value, llc->smac.mask);
		for (i = 0; i < 4; i++) {
			payload.value[i] = llc->llc.value[i];
			payload.mask[i] = llc->llc.mask[i];
		}
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_MAC_LLC_L2_LLC,
				   payload.value, payload.mask);
		break;
	}
	case OCELOT_ACE_TYPE_SNAP: {
		struct ocelot_ace_frame_snap *snap = &ace->frame.snap;

		type = IS2_TYPE_SNAP;
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_L2_DMAC,
				   snap->dmac.value, snap->dmac.mask);
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_L2_SMAC,
				   snap->smac.value, snap->smac.mask);
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_MAC_SNAP_L2_SNAP,
				   ace->frame.snap.snap.value,
				   ace->frame.snap.snap.mask);
		break;
	}
	case OCELOT_ACE_TYPE_ARP: {
		struct ocelot_ace_frame_arp *arp = &ace->frame.arp;

		type = IS2_TYPE_ARP;
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_MAC_ARP_SMAC,
				   arp->smac.value, arp->smac.mask);
		vcap_key_bit_set(ocelot, &data,
				 VCAP_IS2_HK_MAC_ARP_ADDR_SPACE_OK,
				 arp->ethernet);
		vcap_key_bit_set(ocelot, &data,
				 VCAP_IS2_HK_MAC_ARP_PROTO_SPACE_OK,
				 arp->ip);
		vcap_key_bit_set(ocelot, &data,
				 VCAP_IS2_HK_MAC_ARP_LEN_OK,
				 arp->length);
		vcap_key_bit_set(ocelot, &data,
				 VCAP_IS2_HK_MAC_ARP_TARGET_MATCH,
				 arp->dmac_match);
		vcap_key_bit_set(ocelot, &data,
				 VCAP_IS2_HK_MAC_ARP_SENDER_MATCH,
				 arp->smac_match);
		vcap_key_bit_set(ocelot, &data,
				 VCAP_IS2_HK_MAC_ARP_OPCODE_UNKNOWN,
				 arp->unknown);

		/* OPCODE is inverse, bit 0 is reply flag, bit 1 is RARP flag */
		val = ((arp->req == OCELOT_VCAP_BIT_0 ? 1 : 0) |
		       (arp->arp == OCELOT_VCAP_BIT_0 ? 2 : 0));
		msk = ((arp->req == OCELOT_VCAP_BIT_ANY ? 0 : 1) |
		       (arp->arp == OCELOT_VCAP_BIT_ANY ? 0 : 2));
		vcap_key_set(ocelot, &data, VCAP_IS2_HK_MAC_ARP_OPCODE,
			     val, msk);
		vcap_key_bytes_set(ocelot, &data,
				   VCAP_IS2_HK_MAC_ARP_L3_IP4_DIP,
				   arp->dip.value.addr, arp->dip.mask.addr);
		vcap_key_bytes_set(ocelot, &data,
				   VCAP_IS2_HK_MAC_ARP_L3_IP4_SIP,
				   arp->sip.value.addr, arp->sip.mask.addr);
		vcap_key_set(ocelot, &data, VCAP_IS2_HK_MAC_ARP_DIP_EQ_SIP,
			     0, 0);
		break;
	}
	case OCELOT_ACE_TYPE_IPV4:
	case OCELOT_ACE_TYPE_IPV6: {
		enum ocelot_vcap_bit sip_eq_dip, sport_eq_dport, seq_zero, tcp;
		enum ocelot_vcap_bit ttl, fragment, options, tcp_ack, tcp_urg;
		enum ocelot_vcap_bit tcp_fin, tcp_syn, tcp_rst, tcp_psh;
		struct ocelot_ace_frame_ipv4 *ipv4 = NULL;
		struct ocelot_ace_frame_ipv6 *ipv6 = NULL;
		struct ocelot_vcap_udp_tcp *sport, *dport;
		struct ocelot_vcap_ipv4 sip, dip;
		struct ocelot_vcap_u8 proto, ds;
		struct ocelot_vcap_u48 *ip_data;

		if (ace->type == OCELOT_ACE_TYPE_IPV4) {
			ipv4 = &ace->frame.ipv4;
			ttl = ipv4->ttl;
			fragment = ipv4->fragment;
			options = ipv4->options;
			proto = ipv4->proto;
			ds = ipv4->ds;
			ip_data = &ipv4->data;
			sip = ipv4->sip;
			dip = ipv4->dip;
			sport = &ipv4->sport;
			dport = &ipv4->dport;
			tcp_fin = ipv4->tcp_fin;
			tcp_syn = ipv4->tcp_syn;
			tcp_rst = ipv4->tcp_rst;
			tcp_psh = ipv4->tcp_psh;
			tcp_ack = ipv4->tcp_ack;
			tcp_urg = ipv4->tcp_urg;
			sip_eq_dip = ipv4->sip_eq_dip;
			sport_eq_dport = ipv4->sport_eq_dport;
			seq_zero = ipv4->seq_zero;
		} else {
			ipv6 = &ace->frame.ipv6;
			ttl = ipv6->ttl;
			fragment = OCELOT_VCAP_BIT_ANY;
			options = OCELOT_VCAP_BIT_ANY;
			proto = ipv6->proto;
			ds = ipv6->ds;
			ip_data = &ipv6->data;
			for (i = 0; i < 8; i++) {
				val = ipv6->sip.value[i + 8];
				msk = ipv6->sip.mask[i + 8];
				if (i < 4) {
					dip.value.addr[i] = val;
					dip.mask.addr[i] = msk;
				} else {
					sip.value.addr[i - 4] = val;
					sip.mask.addr[i - 4] = msk;
				}
			}
			sport = &ipv6->sport;
			dport = &ipv6->dport;
			tcp_fin = ipv6->tcp_fin;
			tcp_syn = ipv6->tcp_syn;
			tcp_rst = ipv6->tcp_rst;
			tcp_psh = ipv6->tcp_psh;
			tcp_ack = ipv6->tcp_ack;
			tcp_urg = ipv6->tcp_urg;
			sip_eq_dip = ipv6->sip_eq_dip;
			sport_eq_dport = ipv6->sport_eq_dport;
			seq_zero = ipv6->seq_zero;
		}

		vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_IP4,
				 ipv4 ? OCELOT_VCAP_BIT_1 : OCELOT_VCAP_BIT_0);
		vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_L3_FRAGMENT,
				 fragment);
		vcap_key_set(ocelot, &data, VCAP_IS2_HK_L3_FRAG_OFS_GT0, 0, 0);
		vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_L3_OPTIONS,
				 options);
		vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_IP4_L3_TTL_GT0,
				 ttl);
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_L3_TOS,
				   ds.value, ds.mask);
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_L3_IP4_DIP,
				   dip.value.addr, dip.mask.addr);
		vcap_key_bytes_set(ocelot, &data, VCAP_IS2_HK_L3_IP4_SIP,
				   sip.value.addr, sip.mask.addr);
		vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_DIP_EQ_SIP,
				 sip_eq_dip);
		val = proto.value[0];
		msk = proto.mask[0];
		type = IS2_TYPE_IP_UDP_TCP;
		if (msk == 0xff && (val == 6 || val == 17)) {
			/* UDP/TCP protocol match */
			tcp = (val == 6 ?
			       OCELOT_VCAP_BIT_1 : OCELOT_VCAP_BIT_0);
			vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_TCP, tcp);
			vcap_key_l4_port_set(ocelot, &data,
					     VCAP_IS2_HK_L4_DPORT, dport);
			vcap_key_l4_port_set(ocelot, &data,
					     VCAP_IS2_HK_L4_SPORT, sport);
			vcap_key_set(ocelot, &data, VCAP_IS2_HK_L4_RNG, 0, 0);
			vcap_key_bit_set(ocelot, &data,
					 VCAP_IS2_HK_L4_SPORT_EQ_DPORT,
					 sport_eq_dport);
			vcap_key_bit_set(ocelot, &data,
					 VCAP_IS2_HK_L4_SEQUENCE_EQ0,
					 seq_zero);
			vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_L4_FIN,
					 tcp_fin);
			vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_L4_SYN,
					 tcp_syn);
			vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_L4_RST,
					 tcp_rst);
			vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_L4_PSH,
					 tcp_psh);
			vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_L4_ACK,
					 tcp_ack);
			vcap_key_bit_set(ocelot, &data, VCAP_IS2_HK_L4_URG,
					 tcp_urg);
			vcap_key_set(ocelot, &data, VCAP_IS2_HK_L4_1588_DOM,
				     0, 0);
			vcap_key_set(ocelot, &data, VCAP_IS2_HK_L4_1588_VER,
				     0, 0);
		} else {
			if (msk == 0) {
				/* Any IP protocol match */
				type_mask = IS2_TYPE_MASK_IP_ANY;
			} else {
				/* Non-UDP/TCP protocol match */
				type = IS2_TYPE_IP_OTHER;
				for (i = 0; i < 6; i++) {
					payload.value[i] = ip_data->value[i];
					payload.mask[i] = ip_data->mask[i];
				}
			}
			vcap_key_bytes_set(ocelot, &data,
					   VCAP_IS2_HK_IP4_L3_PROTO,
					   proto.value, proto.mask);
			vcap_key_bytes_set(ocelot, &data,
					   VCAP_IS2_HK_L3_PAYLOAD,
					   payload.value, payload.mask);
		}
		break;
	}
	case OCELOT_ACE_TYPE_ANY:
	default:
		type = 0;
		type_mask = 0;
		count = vcap_is2->entry_width / 2;
		/* Iterate over the non-common part of the key and
		 * clear entry data
		 */
		for (i = ocelot->vcap_is2_keys[VCAP_IS2_HK_L2_DMAC].offset;
		     i < count; i += ENTRY_WIDTH) {
			vcap_key_field_set(&data, i, min(32u, count - i), 0, 0);
		}
		break;
	}

	vcap_key_set(ocelot, &data, VCAP_IS2_TYPE, type, type_mask);
	is2_action_set(ocelot, &data, ace);
	vcap_data_set(data.counter, data.counter_offset,
		      vcap_is2->counter_width, ace->stats.pkts);

	/* Write row */
	vcap_entry2cache(ocelot, &data);
	vcap_action2cache(ocelot, &data);
	vcap_row_cmd(ocelot, row, VCAP_CMD_WRITE, VCAP_SEL_ALL);
}

static void is2_entry_get(struct ocelot *ocelot, struct ocelot_ace_rule *rule,
			  int ix)
{
	const struct vcap_props *vcap_is2 = &ocelot->vcap[VCAP_IS2];
	struct vcap_data data;
	int row = (ix / 2);
	u32 cnt;

	vcap_row_cmd(ocelot, row, VCAP_CMD_READ, VCAP_SEL_COUNTER);
	vcap_cache2action(ocelot, &data);
	data.tg_sw = VCAP_TG_HALF;
	is2_data_get(ocelot, &data, ix);
	cnt = vcap_data_get(data.counter, data.counter_offset,
			    vcap_is2->counter_width);

	rule->stats.pkts = cnt;
}

static void ocelot_ace_rule_add(struct ocelot *ocelot,
				struct ocelot_acl_block *block,
				struct ocelot_ace_rule *rule)
{
	struct ocelot_ace_rule *tmp;
	struct list_head *pos, *n;

	if (rule->action == OCELOT_ACL_ACTION_POLICE) {
		block->pol_lpr--;
		rule->pol_ix = block->pol_lpr;
		ocelot_ace_policer_add(ocelot, rule->pol_ix, &rule->pol);
	}

	block->count++;

	if (list_empty(&block->rules)) {
		list_add(&rule->list, &block->rules);
		return;
	}

	list_for_each_safe(pos, n, &block->rules) {
		tmp = list_entry(pos, struct ocelot_ace_rule, list);
		if (rule->prio < tmp->prio)
			break;
	}
	list_add(&rule->list, pos->prev);
}

static int ocelot_ace_rule_get_index_id(struct ocelot_acl_block *block,
					struct ocelot_ace_rule *rule)
{
	struct ocelot_ace_rule *tmp;
	int index = -1;

	list_for_each_entry(tmp, &block->rules, list) {
		++index;
		if (rule->id == tmp->id)
			break;
	}
	return index;
}

static struct ocelot_ace_rule*
ocelot_ace_rule_get_rule_index(struct ocelot_acl_block *block, int index)
{
	struct ocelot_ace_rule *tmp;
	int i = 0;

	list_for_each_entry(tmp, &block->rules, list) {
		if (i == index)
			return tmp;
		++i;
	}

	return NULL;
}

int ocelot_ace_rule_offload_add(struct ocelot *ocelot,
				struct ocelot_ace_rule *rule)
{
	struct ocelot_acl_block *block = &ocelot->acl_block;
	struct ocelot_ace_rule *ace;
	int i, index;

	/* Add rule to the linked list */
	ocelot_ace_rule_add(ocelot, block, rule);

	/* Get the index of the inserted rule */
	index = ocelot_ace_rule_get_index_id(block, rule);

	/* Move down the rules to make place for the new rule */
	for (i = block->count - 1; i > index; i--) {
		ace = ocelot_ace_rule_get_rule_index(block, i);
		is2_entry_set(ocelot, i, ace);
	}

	/* Now insert the new rule */
	is2_entry_set(ocelot, index, rule);
	return 0;
}

static void ocelot_ace_police_del(struct ocelot *ocelot,
				  struct ocelot_acl_block *block,
				  u32 ix)
{
	struct ocelot_ace_rule *ace;
	int index = -1;

	if (ix < block->pol_lpr)
		return;

	list_for_each_entry(ace, &block->rules, list) {
		index++;
		if (ace->action == OCELOT_ACL_ACTION_POLICE &&
		    ace->pol_ix < ix) {
			ace->pol_ix += 1;
			ocelot_ace_policer_add(ocelot, ace->pol_ix,
					       &ace->pol);
			is2_entry_set(ocelot, index, ace);
		}
	}

	ocelot_ace_policer_del(ocelot, block->pol_lpr);
	block->pol_lpr++;
}

static void ocelot_ace_rule_del(struct ocelot *ocelot,
				struct ocelot_acl_block *block,
				struct ocelot_ace_rule *rule)
{
	struct ocelot_ace_rule *tmp;
	struct list_head *pos, *q;

	list_for_each_safe(pos, q, &block->rules) {
		tmp = list_entry(pos, struct ocelot_ace_rule, list);
		if (tmp->id == rule->id) {
			if (tmp->action == OCELOT_ACL_ACTION_POLICE)
				ocelot_ace_police_del(ocelot, block,
						      tmp->pol_ix);

			list_del(pos);
			kfree(tmp);
		}
	}

	block->count--;
}

int ocelot_ace_rule_offload_del(struct ocelot *ocelot,
				struct ocelot_ace_rule *rule)
{
	struct ocelot_acl_block *block = &ocelot->acl_block;
	struct ocelot_ace_rule del_ace;
	struct ocelot_ace_rule *ace;
	int i, index;

	memset(&del_ace, 0, sizeof(del_ace));

	/* Gets index of the rule */
	index = ocelot_ace_rule_get_index_id(block, rule);

	/* Delete rule */
	ocelot_ace_rule_del(ocelot, block, rule);

	/* Move up all the blocks over the deleted rule */
	for (i = index; i < block->count; i++) {
		ace = ocelot_ace_rule_get_rule_index(block, i);
		is2_entry_set(ocelot, i, ace);
	}

	/* Now delete the last rule, because it is duplicated */
	is2_entry_set(ocelot, block->count, &del_ace);

	return 0;
}

int ocelot_ace_rule_stats_update(struct ocelot *ocelot,
				 struct ocelot_ace_rule *rule)
{
	struct ocelot_acl_block *block = &ocelot->acl_block;
	struct ocelot_ace_rule *tmp;
	int index;

	index = ocelot_ace_rule_get_index_id(block, rule);
	is2_entry_get(ocelot, rule, index);

	/* After we get the result we need to clear the counters */
	tmp = ocelot_ace_rule_get_rule_index(block, index);
	tmp->stats.pkts = 0;
	is2_entry_set(ocelot, index, tmp);

	return 0;
}

int ocelot_ace_init(struct ocelot *ocelot)
{
	const struct vcap_props *vcap_is2 = &ocelot->vcap[VCAP_IS2];
	struct ocelot_acl_block *block = &ocelot->acl_block;
	struct vcap_data data;

	memset(&data, 0, sizeof(data));

	vcap_entry2cache(ocelot, &data);
	ocelot_write(ocelot, vcap_is2->entry_count, S2_CORE_MV_CFG);
	vcap_cmd(ocelot, 0, VCAP_CMD_INITIALIZE, VCAP_SEL_ENTRY);

	vcap_action2cache(ocelot, &data);
	ocelot_write(ocelot, vcap_is2->action_count, S2_CORE_MV_CFG);
	vcap_cmd(ocelot, 0, VCAP_CMD_INITIALIZE,
		 VCAP_SEL_ACTION | VCAP_SEL_COUNTER);

	/* Create a policer that will drop the frames for the cpu.
	 * This policer will be used as action in the acl rules to drop
	 * frames.
	 */
	ocelot_write_gix(ocelot, 0x299, ANA_POL_MODE_CFG,
			 OCELOT_POLICER_DISCARD);
	ocelot_write_gix(ocelot, 0x1, ANA_POL_PIR_CFG,
			 OCELOT_POLICER_DISCARD);
	ocelot_write_gix(ocelot, 0x3fffff, ANA_POL_PIR_STATE,
			 OCELOT_POLICER_DISCARD);
	ocelot_write_gix(ocelot, 0x0, ANA_POL_CIR_CFG,
			 OCELOT_POLICER_DISCARD);
	ocelot_write_gix(ocelot, 0x3fffff, ANA_POL_CIR_STATE,
			 OCELOT_POLICER_DISCARD);

	block->pol_lpr = OCELOT_POLICER_DISCARD - 1;

	INIT_LIST_HEAD(&ocelot->acl_block.rules);

	return 0;
}
