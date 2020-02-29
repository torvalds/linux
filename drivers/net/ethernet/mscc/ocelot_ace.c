// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch driver
 * Copyright (c) 2019 Microsemi Corporation
 */

#include <linux/iopoll.h>
#include <linux/proc_fs.h>

#include "ocelot_ace.h"
#include "ocelot_vcap.h"
#include "ocelot_s2.h"

#define OCELOT_POLICER_DISCARD 0x17f

static struct ocelot_acl_block *acl_block;

struct vcap_props {
	const char *name; /* Symbolic name */
	u16 tg_width; /* Type-group width (in bits) */
	u16 sw_count; /* Sub word count */
	u16 entry_count; /* Entry count */
	u16 entry_words; /* Number of entry words */
	u16 entry_width; /* Entry width (in bits) */
	u16 action_count; /* Action count */
	u16 action_words; /* Number of action words */
	u16 action_width; /* Action width (in bits) */
	u16 action_type_width; /* Action type width (in bits) */
	struct {
		u16 width; /* Action type width (in bits) */
		u16 count; /* Action type sub word count */
	} action_table[2];
	u16 counter_words; /* Number of counter words */
	u16 counter_width; /* Counter width (in bits) */
};

#define ENTRY_WIDTH 32
#define BITS_TO_32BIT(x) (1 + (((x) - 1) / ENTRY_WIDTH))

static const struct vcap_props vcap_is2 = {
	.name = "IS2",
	.tg_width = 2,
	.sw_count = 4,
	.entry_count = VCAP_IS2_CNT,
	.entry_words = BITS_TO_32BIT(VCAP_IS2_ENTRY_WIDTH),
	.entry_width = VCAP_IS2_ENTRY_WIDTH,
	.action_count = (VCAP_IS2_CNT + VCAP_PORT_CNT + 2),
	.action_words = BITS_TO_32BIT(VCAP_IS2_ACTION_WIDTH),
	.action_width = (VCAP_IS2_ACTION_WIDTH),
	.action_type_width = 1,
	.action_table = {
		{
			.width = (IS2_AO_ACL_ID + IS2_AL_ACL_ID),
			.count = 2
		},
		{
			.width = 6,
			.count = 4
		},
	},
	.counter_words = BITS_TO_32BIT(4 * ENTRY_WIDTH),
	.counter_width = ENTRY_WIDTH,
};

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

static u32 vcap_s2_read_update_ctrl(struct ocelot *oc)
{
	return ocelot_read(oc, S2_CORE_UPDATE_CTRL);
}

static void vcap_cmd(struct ocelot *oc, u16 ix, int cmd, int sel)
{
	u32 value = (S2_CORE_UPDATE_CTRL_UPDATE_CMD(cmd) |
		     S2_CORE_UPDATE_CTRL_UPDATE_ADDR(ix) |
		     S2_CORE_UPDATE_CTRL_UPDATE_SHOT);

	if ((sel & VCAP_SEL_ENTRY) && ix >= vcap_is2.entry_count)
		return;

	if (!(sel & VCAP_SEL_ENTRY))
		value |= S2_CORE_UPDATE_CTRL_UPDATE_ENTRY_DIS;

	if (!(sel & VCAP_SEL_ACTION))
		value |= S2_CORE_UPDATE_CTRL_UPDATE_ACTION_DIS;

	if (!(sel & VCAP_SEL_COUNTER))
		value |= S2_CORE_UPDATE_CTRL_UPDATE_CNT_DIS;

	ocelot_write(oc, value, S2_CORE_UPDATE_CTRL);
	readx_poll_timeout(vcap_s2_read_update_ctrl, oc, value,
				(value & S2_CORE_UPDATE_CTRL_UPDATE_SHOT) == 0,
				10, 100000);
}

/* Convert from 0-based row to VCAP entry row and run command */
static void vcap_row_cmd(struct ocelot *oc, u32 row, int cmd, int sel)
{
	vcap_cmd(oc, vcap_is2.entry_count - row - 1, cmd, sel);
}

static void vcap_entry2cache(struct ocelot *oc, struct vcap_data *data)
{
	u32 i;

	for (i = 0; i < vcap_is2.entry_words; i++) {
		ocelot_write_rix(oc, data->entry[i], S2_CACHE_ENTRY_DAT, i);
		ocelot_write_rix(oc, ~data->mask[i], S2_CACHE_MASK_DAT, i);
	}
	ocelot_write(oc, data->tg, S2_CACHE_TG_DAT);
}

static void vcap_cache2entry(struct ocelot *oc, struct vcap_data *data)
{
	u32 i;

	for (i = 0; i < vcap_is2.entry_words; i++) {
		data->entry[i] = ocelot_read_rix(oc, S2_CACHE_ENTRY_DAT, i);
		// Invert mask
		data->mask[i] = ~ocelot_read_rix(oc, S2_CACHE_MASK_DAT, i);
	}
	data->tg = ocelot_read(oc, S2_CACHE_TG_DAT);
}

static void vcap_action2cache(struct ocelot *oc, struct vcap_data *data)
{
	u32 i, width, mask;

	/* Encode action type */
	width = vcap_is2.action_type_width;
	if (width) {
		mask = GENMASK(width, 0);
		data->action[0] = ((data->action[0] & ~mask) | data->type);
	}

	for (i = 0; i < vcap_is2.action_words; i++)
		ocelot_write_rix(oc, data->action[i], S2_CACHE_ACTION_DAT, i);

	for (i = 0; i < vcap_is2.counter_words; i++)
		ocelot_write_rix(oc, data->counter[i], S2_CACHE_CNT_DAT, i);
}

static void vcap_cache2action(struct ocelot *oc, struct vcap_data *data)
{
	u32 i, width;

	for (i = 0; i < vcap_is2.action_words; i++)
		data->action[i] = ocelot_read_rix(oc, S2_CACHE_ACTION_DAT, i);

	for (i = 0; i < vcap_is2.counter_words; i++)
		data->counter[i] = ocelot_read_rix(oc, S2_CACHE_CNT_DAT, i);

	/* Extract action type */
	width = vcap_is2.action_type_width;
	data->type = (width ? (data->action[0] & GENMASK(width, 0)) : 0);
}

/* Calculate offsets for entry */
static void is2_data_get(struct vcap_data *data, int ix)
{
	u32 i, col, offset, count, cnt, base, width = vcap_is2.tg_width;

	count = (data->tg_sw == VCAP_TG_HALF ? 2 : 4);
	col = (ix % 2);
	cnt = (vcap_is2.sw_count / count);
	base = (vcap_is2.sw_count - col * cnt - cnt);
	data->tg_value = 0;
	data->tg_mask = 0;
	for (i = 0; i < cnt; i++) {
		offset = ((base + i) * width);
		data->tg_value |= (data->tg_sw << offset);
		data->tg_mask |= GENMASK(offset + width - 1, offset);
	}

	/* Calculate key/action/counter offsets */
	col = (count - col - 1);
	data->key_offset = (base * vcap_is2.entry_width) / vcap_is2.sw_count;
	data->counter_offset = (cnt * col * vcap_is2.counter_width);
	i = data->type;
	width = vcap_is2.action_table[i].width;
	cnt = vcap_is2.action_table[i].count;
	data->action_offset =
		(((cnt * col * width) / count) + vcap_is2.action_type_width);
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

static void vcap_key_set(struct vcap_data *data, u32 offset, u32 width,
			 u32 value, u32 mask)
{
	vcap_data_set(data->entry, offset + data->key_offset, width, value);
	vcap_data_set(data->mask, offset + data->key_offset, width, mask);
}

static void vcap_key_bytes_set(struct vcap_data *data, u32 offset, u8 *val,
			       u8 *msk, u32 count)
{
	u32 i, j, n = 0, value = 0, mask = 0;

	/* Data wider than 32 bits are split up in chunks of maximum 32 bits.
	 * The 32 LSB of the data are written to the 32 MSB of the TCAM.
	 */
	offset += (count * 8);
	for (i = 0; i < count; i++) {
		j = (count - i - 1);
		value += (val[j] << n);
		mask += (msk[j] << n);
		n += 8;
		if (n == ENTRY_WIDTH || (i + 1) == count) {
			offset -= n;
			vcap_key_set(data, offset, n, value, mask);
			n = 0;
			value = 0;
			mask = 0;
		}
	}
}

static void vcap_key_l4_port_set(struct vcap_data *data, u32 offset,
				 struct ocelot_vcap_udp_tcp *port)
{
	vcap_key_set(data, offset, 16, port->value, port->mask);
}

static void vcap_key_bit_set(struct vcap_data *data, u32 offset,
			     enum ocelot_vcap_bit val)
{
	vcap_key_set(data, offset, 1, val == OCELOT_VCAP_BIT_1 ? 1 : 0,
		     val == OCELOT_VCAP_BIT_ANY ? 0 : 1);
}

#define VCAP_KEY_SET(fld, val, msk) \
	vcap_key_set(&data, IS2_HKO_##fld, IS2_HKL_##fld, val, msk)
#define VCAP_KEY_ANY_SET(fld) \
	vcap_key_set(&data, IS2_HKO_##fld, IS2_HKL_##fld, 0, 0)
#define VCAP_KEY_BIT_SET(fld, val) vcap_key_bit_set(&data, IS2_HKO_##fld, val)
#define VCAP_KEY_BYTES_SET(fld, val, msk) \
	vcap_key_bytes_set(&data, IS2_HKO_##fld, val, msk, IS2_HKL_##fld / 8)

static void vcap_action_set(struct vcap_data *data, u32 offset, u32 width,
			    u32 value)
{
	vcap_data_set(data->action, offset + data->action_offset, width, value);
}

#define VCAP_ACT_SET(fld, val) \
	vcap_action_set(data, IS2_AO_##fld, IS2_AL_##fld, val)

static void is2_action_set(struct vcap_data *data,
			   enum ocelot_ace_action action)
{
	switch (action) {
	case OCELOT_ACL_ACTION_DROP:
		VCAP_ACT_SET(PORT_MASK, 0x0);
		VCAP_ACT_SET(MASK_MODE, 0x1);
		VCAP_ACT_SET(POLICE_ENA, 0x1);
		VCAP_ACT_SET(POLICE_IDX, OCELOT_POLICER_DISCARD);
		VCAP_ACT_SET(CPU_QU_NUM, 0x0);
		VCAP_ACT_SET(CPU_COPY_ENA, 0x0);
		break;
	case OCELOT_ACL_ACTION_TRAP:
		VCAP_ACT_SET(PORT_MASK, 0x0);
		VCAP_ACT_SET(MASK_MODE, 0x1);
		VCAP_ACT_SET(POLICE_ENA, 0x0);
		VCAP_ACT_SET(POLICE_IDX, 0x0);
		VCAP_ACT_SET(CPU_QU_NUM, 0x0);
		VCAP_ACT_SET(CPU_COPY_ENA, 0x1);
		break;
	}
}

static void is2_entry_set(struct ocelot *ocelot, int ix,
			  struct ocelot_ace_rule *ace)
{
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
	is2_data_get(&data, ix);
	data.tg = (data.tg & ~data.tg_mask);
	if (ace->prio != 0)
		data.tg |= data.tg_value;

	data.type = IS2_ACTION_TYPE_NORMAL;

	VCAP_KEY_ANY_SET(PAG);
	VCAP_KEY_SET(IGR_PORT_MASK, 0, ~ace->ingress_port_mask);
	VCAP_KEY_BIT_SET(FIRST, OCELOT_VCAP_BIT_1);
	VCAP_KEY_BIT_SET(HOST_MATCH, OCELOT_VCAP_BIT_ANY);
	VCAP_KEY_BIT_SET(L2_MC, ace->dmac_mc);
	VCAP_KEY_BIT_SET(L2_BC, ace->dmac_bc);
	VCAP_KEY_BIT_SET(VLAN_TAGGED, tag->tagged);
	VCAP_KEY_SET(VID, tag->vid.value, tag->vid.mask);
	VCAP_KEY_SET(PCP, tag->pcp.value[0], tag->pcp.mask[0]);
	VCAP_KEY_BIT_SET(DEI, tag->dei);

	switch (ace->type) {
	case OCELOT_ACE_TYPE_ETYPE: {
		struct ocelot_ace_frame_etype *etype = &ace->frame.etype;

		type = IS2_TYPE_ETYPE;
		VCAP_KEY_BYTES_SET(L2_DMAC, etype->dmac.value,
				   etype->dmac.mask);
		VCAP_KEY_BYTES_SET(L2_SMAC, etype->smac.value,
				   etype->smac.mask);
		VCAP_KEY_BYTES_SET(MAC_ETYPE_ETYPE, etype->etype.value,
				   etype->etype.mask);
		VCAP_KEY_ANY_SET(MAC_ETYPE_L2_PAYLOAD); // Clear unused bits
		vcap_key_bytes_set(&data, IS2_HKO_MAC_ETYPE_L2_PAYLOAD,
				   etype->data.value, etype->data.mask, 2);
		break;
	}
	case OCELOT_ACE_TYPE_LLC: {
		struct ocelot_ace_frame_llc *llc = &ace->frame.llc;

		type = IS2_TYPE_LLC;
		VCAP_KEY_BYTES_SET(L2_DMAC, llc->dmac.value, llc->dmac.mask);
		VCAP_KEY_BYTES_SET(L2_SMAC, llc->smac.value, llc->smac.mask);
		for (i = 0; i < 4; i++) {
			payload.value[i] = llc->llc.value[i];
			payload.mask[i] = llc->llc.mask[i];
		}
		VCAP_KEY_BYTES_SET(MAC_LLC_L2_LLC, payload.value, payload.mask);
		break;
	}
	case OCELOT_ACE_TYPE_SNAP: {
		struct ocelot_ace_frame_snap *snap = &ace->frame.snap;

		type = IS2_TYPE_SNAP;
		VCAP_KEY_BYTES_SET(L2_DMAC, snap->dmac.value, snap->dmac.mask);
		VCAP_KEY_BYTES_SET(L2_SMAC, snap->smac.value, snap->smac.mask);
		VCAP_KEY_BYTES_SET(MAC_SNAP_L2_SNAP,
				   ace->frame.snap.snap.value,
				   ace->frame.snap.snap.mask);
		break;
	}
	case OCELOT_ACE_TYPE_ARP: {
		struct ocelot_ace_frame_arp *arp = &ace->frame.arp;

		type = IS2_TYPE_ARP;
		VCAP_KEY_BYTES_SET(MAC_ARP_L2_SMAC, arp->smac.value,
				   arp->smac.mask);
		VCAP_KEY_BIT_SET(MAC_ARP_ARP_ADDR_SPACE_OK, arp->ethernet);
		VCAP_KEY_BIT_SET(MAC_ARP_ARP_PROTO_SPACE_OK, arp->ip);
		VCAP_KEY_BIT_SET(MAC_ARP_ARP_LEN_OK, arp->length);
		VCAP_KEY_BIT_SET(MAC_ARP_ARP_TGT_MATCH, arp->dmac_match);
		VCAP_KEY_BIT_SET(MAC_ARP_ARP_SENDER_MATCH, arp->smac_match);
		VCAP_KEY_BIT_SET(MAC_ARP_ARP_OPCODE_UNKNOWN, arp->unknown);

		/* OPCODE is inverse, bit 0 is reply flag, bit 1 is RARP flag */
		val = ((arp->req == OCELOT_VCAP_BIT_0 ? 1 : 0) |
		       (arp->arp == OCELOT_VCAP_BIT_0 ? 2 : 0));
		msk = ((arp->req == OCELOT_VCAP_BIT_ANY ? 0 : 1) |
		       (arp->arp == OCELOT_VCAP_BIT_ANY ? 0 : 2));
		VCAP_KEY_SET(MAC_ARP_ARP_OPCODE, val, msk);
		vcap_key_bytes_set(&data, IS2_HKO_MAC_ARP_L3_IP4_DIP,
				   arp->dip.value.addr, arp->dip.mask.addr, 4);
		vcap_key_bytes_set(&data, IS2_HKO_MAC_ARP_L3_IP4_SIP,
				   arp->sip.value.addr, arp->sip.mask.addr, 4);
		VCAP_KEY_ANY_SET(MAC_ARP_DIP_EQ_SIP);
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

		VCAP_KEY_BIT_SET(IP4,
				 ipv4 ? OCELOT_VCAP_BIT_1 : OCELOT_VCAP_BIT_0);
		VCAP_KEY_BIT_SET(L3_FRAGMENT, fragment);
		VCAP_KEY_ANY_SET(L3_FRAG_OFS_GT0);
		VCAP_KEY_BIT_SET(L3_OPTIONS, options);
		VCAP_KEY_BIT_SET(L3_TTL_GT0, ttl);
		VCAP_KEY_BYTES_SET(L3_TOS, ds.value, ds.mask);
		vcap_key_bytes_set(&data, IS2_HKO_L3_IP4_DIP, dip.value.addr,
				   dip.mask.addr, 4);
		vcap_key_bytes_set(&data, IS2_HKO_L3_IP4_SIP, sip.value.addr,
				   sip.mask.addr, 4);
		VCAP_KEY_BIT_SET(DIP_EQ_SIP, sip_eq_dip);
		val = proto.value[0];
		msk = proto.mask[0];
		type = IS2_TYPE_IP_UDP_TCP;
		if (msk == 0xff && (val == 6 || val == 17)) {
			/* UDP/TCP protocol match */
			tcp = (val == 6 ?
			       OCELOT_VCAP_BIT_1 : OCELOT_VCAP_BIT_0);
			VCAP_KEY_BIT_SET(IP4_TCP_UDP_TCP, tcp);
			vcap_key_l4_port_set(&data,
					     IS2_HKO_IP4_TCP_UDP_L4_DPORT,
					     dport);
			vcap_key_l4_port_set(&data,
					     IS2_HKO_IP4_TCP_UDP_L4_SPORT,
					     sport);
			VCAP_KEY_ANY_SET(IP4_TCP_UDP_L4_RNG);
			VCAP_KEY_BIT_SET(IP4_TCP_UDP_SPORT_EQ_DPORT,
					 sport_eq_dport);
			VCAP_KEY_BIT_SET(IP4_TCP_UDP_SEQUENCE_EQ0, seq_zero);
			VCAP_KEY_BIT_SET(IP4_TCP_UDP_L4_FIN, tcp_fin);
			VCAP_KEY_BIT_SET(IP4_TCP_UDP_L4_SYN, tcp_syn);
			VCAP_KEY_BIT_SET(IP4_TCP_UDP_L4_RST, tcp_rst);
			VCAP_KEY_BIT_SET(IP4_TCP_UDP_L4_PSH, tcp_psh);
			VCAP_KEY_BIT_SET(IP4_TCP_UDP_L4_ACK, tcp_ack);
			VCAP_KEY_BIT_SET(IP4_TCP_UDP_L4_URG, tcp_urg);
			VCAP_KEY_ANY_SET(IP4_TCP_UDP_L4_1588_DOM);
			VCAP_KEY_ANY_SET(IP4_TCP_UDP_L4_1588_VER);
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
			VCAP_KEY_BYTES_SET(IP4_OTHER_L3_PROTO, proto.value,
					   proto.mask);
			VCAP_KEY_BYTES_SET(IP4_OTHER_L3_PAYLOAD, payload.value,
					   payload.mask);
		}
		break;
	}
	case OCELOT_ACE_TYPE_ANY:
	default:
		type = 0;
		type_mask = 0;
		count = (vcap_is2.entry_width / 2);
		for (i = (IS2_HKO_PCP + IS2_HKL_PCP); i < count;
		     i += ENTRY_WIDTH) {
			/* Clear entry data */
			vcap_key_set(&data, i, min(32u, count - i), 0, 0);
		}
		break;
	}

	VCAP_KEY_SET(TYPE, type, type_mask);
	is2_action_set(&data, ace->action);
	vcap_data_set(data.counter, data.counter_offset, vcap_is2.counter_width,
		      ace->stats.pkts);

	/* Write row */
	vcap_entry2cache(ocelot, &data);
	vcap_action2cache(ocelot, &data);
	vcap_row_cmd(ocelot, row, VCAP_CMD_WRITE, VCAP_SEL_ALL);
}

static void is2_entry_get(struct ocelot_ace_rule *rule, int ix)
{
	struct ocelot *op = rule->ocelot;
	struct vcap_data data;
	int row = (ix / 2);
	u32 cnt;

	vcap_row_cmd(op, row, VCAP_CMD_READ, VCAP_SEL_COUNTER);
	vcap_cache2action(op, &data);
	data.tg_sw = VCAP_TG_HALF;
	is2_data_get(&data, ix);
	cnt = vcap_data_get(data.counter, data.counter_offset,
			    vcap_is2.counter_width);

	rule->stats.pkts = cnt;
}

static void ocelot_ace_rule_add(struct ocelot_acl_block *block,
				struct ocelot_ace_rule *rule)
{
	struct ocelot_ace_rule *tmp;
	struct list_head *pos, *n;

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

int ocelot_ace_rule_offload_add(struct ocelot_ace_rule *rule)
{
	struct ocelot_ace_rule *ace;
	int i, index;

	/* Add rule to the linked list */
	ocelot_ace_rule_add(acl_block, rule);

	/* Get the index of the inserted rule */
	index = ocelot_ace_rule_get_index_id(acl_block, rule);

	/* Move down the rules to make place for the new rule */
	for (i = acl_block->count - 1; i > index; i--) {
		ace = ocelot_ace_rule_get_rule_index(acl_block, i);
		is2_entry_set(rule->ocelot, i, ace);
	}

	/* Now insert the new rule */
	is2_entry_set(rule->ocelot, index, rule);
	return 0;
}

static void ocelot_ace_rule_del(struct ocelot_acl_block *block,
				struct ocelot_ace_rule *rule)
{
	struct ocelot_ace_rule *tmp;
	struct list_head *pos, *q;

	list_for_each_safe(pos, q, &block->rules) {
		tmp = list_entry(pos, struct ocelot_ace_rule, list);
		if (tmp->id == rule->id) {
			list_del(pos);
			kfree(tmp);
		}
	}

	block->count--;
}

int ocelot_ace_rule_offload_del(struct ocelot_ace_rule *rule)
{
	struct ocelot_ace_rule del_ace;
	struct ocelot_ace_rule *ace;
	int i, index;

	memset(&del_ace, 0, sizeof(del_ace));

	/* Gets index of the rule */
	index = ocelot_ace_rule_get_index_id(acl_block, rule);

	/* Delete rule */
	ocelot_ace_rule_del(acl_block, rule);

	/* Move up all the blocks over the deleted rule */
	for (i = index; i < acl_block->count; i++) {
		ace = ocelot_ace_rule_get_rule_index(acl_block, i);
		is2_entry_set(rule->ocelot, i, ace);
	}

	/* Now delete the last rule, because it is duplicated */
	is2_entry_set(rule->ocelot, acl_block->count, &del_ace);

	return 0;
}

int ocelot_ace_rule_stats_update(struct ocelot_ace_rule *rule)
{
	struct ocelot_ace_rule *tmp;
	int index;

	index = ocelot_ace_rule_get_index_id(acl_block, rule);
	is2_entry_get(rule, index);

	/* After we get the result we need to clear the counters */
	tmp = ocelot_ace_rule_get_rule_index(acl_block, index);
	tmp->stats.pkts = 0;
	is2_entry_set(rule->ocelot, index, tmp);

	return 0;
}

static struct ocelot_acl_block *ocelot_acl_block_create(struct ocelot *ocelot)
{
	struct ocelot_acl_block *block;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		return NULL;

	INIT_LIST_HEAD(&block->rules);
	block->count = 0;
	block->ocelot = ocelot;

	return block;
}

static void ocelot_acl_block_destroy(struct ocelot_acl_block *block)
{
	kfree(block);
}

int ocelot_ace_init(struct ocelot *ocelot)
{
	struct vcap_data data;

	memset(&data, 0, sizeof(data));
	vcap_entry2cache(ocelot, &data);
	ocelot_write(ocelot, vcap_is2.entry_count, S2_CORE_MV_CFG);
	vcap_cmd(ocelot, 0, VCAP_CMD_INITIALIZE, VCAP_SEL_ENTRY);

	vcap_action2cache(ocelot, &data);
	ocelot_write(ocelot, vcap_is2.action_count, S2_CORE_MV_CFG);
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

	acl_block = ocelot_acl_block_create(ocelot);

	return 0;
}

void ocelot_ace_deinit(void)
{
	ocelot_acl_block_destroy(acl_block);
}
