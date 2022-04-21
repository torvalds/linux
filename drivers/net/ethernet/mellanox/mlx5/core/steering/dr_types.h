/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019, Mellanox Technologies */

#ifndef	_DR_TYPES_
#define	_DR_TYPES_

#include <linux/mlx5/vport.h>
#include <linux/refcount.h>
#include "fs_core.h"
#include "wq.h"
#include "lib/mlx5.h"
#include "mlx5_ifc_dr.h"
#include "mlx5dr.h"
#include "dr_dbg.h"

#define DR_RULE_MAX_STES 18
#define DR_ACTION_MAX_STES 5
#define DR_STE_SVLAN 0x1
#define DR_STE_CVLAN 0x2
#define DR_SZ_MATCH_PARAM (MLX5_ST_SZ_DW_MATCH_PARAM * 4)
#define DR_NUM_OF_FLEX_PARSERS 8
#define DR_STE_MAX_FLEX_0_ID 3
#define DR_STE_MAX_FLEX_1_ID 7

#define mlx5dr_err(dmn, arg...) mlx5_core_err((dmn)->mdev, ##arg)
#define mlx5dr_info(dmn, arg...) mlx5_core_info((dmn)->mdev, ##arg)
#define mlx5dr_dbg(dmn, arg...) mlx5_core_dbg((dmn)->mdev, ##arg)

static inline bool dr_is_flex_parser_0_id(u8 parser_id)
{
	return parser_id <= DR_STE_MAX_FLEX_0_ID;
}

static inline bool dr_is_flex_parser_1_id(u8 parser_id)
{
	return parser_id > DR_STE_MAX_FLEX_0_ID;
}

enum mlx5dr_icm_chunk_size {
	DR_CHUNK_SIZE_1,
	DR_CHUNK_SIZE_MIN = DR_CHUNK_SIZE_1, /* keep updated when changing */
	DR_CHUNK_SIZE_2,
	DR_CHUNK_SIZE_4,
	DR_CHUNK_SIZE_8,
	DR_CHUNK_SIZE_16,
	DR_CHUNK_SIZE_32,
	DR_CHUNK_SIZE_64,
	DR_CHUNK_SIZE_128,
	DR_CHUNK_SIZE_256,
	DR_CHUNK_SIZE_512,
	DR_CHUNK_SIZE_1K,
	DR_CHUNK_SIZE_2K,
	DR_CHUNK_SIZE_4K,
	DR_CHUNK_SIZE_8K,
	DR_CHUNK_SIZE_16K,
	DR_CHUNK_SIZE_32K,
	DR_CHUNK_SIZE_64K,
	DR_CHUNK_SIZE_128K,
	DR_CHUNK_SIZE_256K,
	DR_CHUNK_SIZE_512K,
	DR_CHUNK_SIZE_1024K,
	DR_CHUNK_SIZE_2048K,
	DR_CHUNK_SIZE_MAX,
};

enum mlx5dr_icm_type {
	DR_ICM_TYPE_STE,
	DR_ICM_TYPE_MODIFY_ACTION,
};

static inline enum mlx5dr_icm_chunk_size
mlx5dr_icm_next_higher_chunk(enum mlx5dr_icm_chunk_size chunk)
{
	chunk += 2;
	if (chunk < DR_CHUNK_SIZE_MAX)
		return chunk;

	return DR_CHUNK_SIZE_MAX;
}

enum {
	DR_STE_SIZE = 64,
	DR_STE_SIZE_CTRL = 32,
	DR_STE_SIZE_TAG = 16,
	DR_STE_SIZE_MASK = 16,
	DR_STE_SIZE_REDUCED = DR_STE_SIZE - DR_STE_SIZE_MASK,
};

enum mlx5dr_ste_ctx_action_cap {
	DR_STE_CTX_ACTION_CAP_NONE = 0,
	DR_STE_CTX_ACTION_CAP_TX_POP   = 1 << 0,
	DR_STE_CTX_ACTION_CAP_RX_PUSH  = 1 << 1,
	DR_STE_CTX_ACTION_CAP_RX_ENCAP = 1 << 2,
	DR_STE_CTX_ACTION_CAP_POP_MDFY = 1 << 3,
};

enum {
	DR_MODIFY_ACTION_SIZE = 8,
};

enum mlx5dr_matcher_criteria {
	DR_MATCHER_CRITERIA_EMPTY = 0,
	DR_MATCHER_CRITERIA_OUTER = 1 << 0,
	DR_MATCHER_CRITERIA_MISC = 1 << 1,
	DR_MATCHER_CRITERIA_INNER = 1 << 2,
	DR_MATCHER_CRITERIA_MISC2 = 1 << 3,
	DR_MATCHER_CRITERIA_MISC3 = 1 << 4,
	DR_MATCHER_CRITERIA_MISC4 = 1 << 5,
	DR_MATCHER_CRITERIA_MISC5 = 1 << 6,
	DR_MATCHER_CRITERIA_MAX = 1 << 7,
};

enum mlx5dr_action_type {
	DR_ACTION_TYP_TNL_L2_TO_L2,
	DR_ACTION_TYP_L2_TO_TNL_L2,
	DR_ACTION_TYP_TNL_L3_TO_L2,
	DR_ACTION_TYP_L2_TO_TNL_L3,
	DR_ACTION_TYP_DROP,
	DR_ACTION_TYP_QP,
	DR_ACTION_TYP_FT,
	DR_ACTION_TYP_CTR,
	DR_ACTION_TYP_TAG,
	DR_ACTION_TYP_MODIFY_HDR,
	DR_ACTION_TYP_VPORT,
	DR_ACTION_TYP_POP_VLAN,
	DR_ACTION_TYP_PUSH_VLAN,
	DR_ACTION_TYP_INSERT_HDR,
	DR_ACTION_TYP_REMOVE_HDR,
	DR_ACTION_TYP_SAMPLER,
	DR_ACTION_TYP_MAX,
};

enum mlx5dr_ipv {
	DR_RULE_IPV4,
	DR_RULE_IPV6,
	DR_RULE_IPV_MAX,
};

struct mlx5dr_icm_pool;
struct mlx5dr_icm_chunk;
struct mlx5dr_icm_buddy_mem;
struct mlx5dr_ste_htbl;
struct mlx5dr_match_param;
struct mlx5dr_cmd_caps;
struct mlx5dr_rule_rx_tx;
struct mlx5dr_matcher_rx_tx;
struct mlx5dr_ste_ctx;

struct mlx5dr_ste {
	/* refcount: indicates the num of rules that using this ste */
	u32 refcount;

	/* this ste is part of a rule, located in ste's chain */
	u8 ste_chain_location;

	/* attached to the miss_list head at each htbl entry */
	struct list_head miss_list_node;

	/* this ste is member of htbl */
	struct mlx5dr_ste_htbl *htbl;

	struct mlx5dr_ste_htbl *next_htbl;

	/* The rule this STE belongs to */
	struct mlx5dr_rule_rx_tx *rule_rx_tx;
};

struct mlx5dr_ste_htbl_ctrl {
	/* total number of valid entries belonging to this hash table. This
	 * includes the non collision and collision entries
	 */
	unsigned int num_of_valid_entries;

	/* total number of collisions entries attached to this table */
	unsigned int num_of_collisions;
};

struct mlx5dr_ste_htbl {
	u16 lu_type;
	u16 byte_mask;
	u32 refcount;
	struct mlx5dr_icm_chunk *chunk;
	struct mlx5dr_ste *pointing_ste;
	struct mlx5dr_ste_htbl_ctrl ctrl;
};

struct mlx5dr_ste_send_info {
	struct mlx5dr_ste *ste;
	struct list_head send_list;
	u16 size;
	u16 offset;
	u8 data_cont[DR_STE_SIZE];
	u8 *data;
};

void mlx5dr_send_fill_and_append_ste_send_info(struct mlx5dr_ste *ste, u16 size,
					       u16 offset, u8 *data,
					       struct mlx5dr_ste_send_info *ste_info,
					       struct list_head *send_list,
					       bool copy_data);

struct mlx5dr_ste_build {
	u8 inner:1;
	u8 rx:1;
	u8 vhca_id_valid:1;
	struct mlx5dr_domain *dmn;
	struct mlx5dr_cmd_caps *caps;
	u16 lu_type;
	u16 byte_mask;
	u8 bit_mask[DR_STE_SIZE_MASK];
	int (*ste_build_tag_func)(struct mlx5dr_match_param *spec,
				  struct mlx5dr_ste_build *sb,
				  u8 *tag);
};

struct mlx5dr_ste_htbl *
mlx5dr_ste_htbl_alloc(struct mlx5dr_icm_pool *pool,
		      enum mlx5dr_icm_chunk_size chunk_size,
		      u16 lu_type, u16 byte_mask);

int mlx5dr_ste_htbl_free(struct mlx5dr_ste_htbl *htbl);

static inline void mlx5dr_htbl_put(struct mlx5dr_ste_htbl *htbl)
{
	htbl->refcount--;
	if (!htbl->refcount)
		mlx5dr_ste_htbl_free(htbl);
}

static inline void mlx5dr_htbl_get(struct mlx5dr_ste_htbl *htbl)
{
	htbl->refcount++;
}

/* STE utils */
u32 mlx5dr_ste_calc_hash_index(u8 *hw_ste_p, struct mlx5dr_ste_htbl *htbl);
void mlx5dr_ste_set_miss_addr(struct mlx5dr_ste_ctx *ste_ctx,
			      u8 *hw_ste, u64 miss_addr);
void mlx5dr_ste_set_hit_addr(struct mlx5dr_ste_ctx *ste_ctx,
			     u8 *hw_ste, u64 icm_addr, u32 ht_size);
void mlx5dr_ste_set_hit_addr_by_next_htbl(struct mlx5dr_ste_ctx *ste_ctx,
					  u8 *hw_ste,
					  struct mlx5dr_ste_htbl *next_htbl);
void mlx5dr_ste_set_bit_mask(u8 *hw_ste_p, u8 *bit_mask);
bool mlx5dr_ste_is_last_in_rule(struct mlx5dr_matcher_rx_tx *nic_matcher,
				u8 ste_location);
u64 mlx5dr_ste_get_icm_addr(struct mlx5dr_ste *ste);
u64 mlx5dr_ste_get_mr_addr(struct mlx5dr_ste *ste);
struct list_head *mlx5dr_ste_get_miss_list(struct mlx5dr_ste *ste);

#define MLX5DR_MAX_VLANS 2

struct mlx5dr_ste_actions_attr {
	u32	modify_index;
	u16	modify_actions;
	u32	decap_index;
	u16	decap_actions;
	u8	decap_with_vlan:1;
	u64	final_icm_addr;
	u32	flow_tag;
	u32	ctr_id;
	u16	gvmi;
	u16	hit_gvmi;
	struct {
		u32	id;
		u32	size;
		u8	param_0;
		u8	param_1;
	} reformat;
	struct {
		int	count;
		u32	headers[MLX5DR_MAX_VLANS];
	} vlans;
};

void mlx5dr_ste_set_actions_rx(struct mlx5dr_ste_ctx *ste_ctx,
			       struct mlx5dr_domain *dmn,
			       u8 *action_type_set,
			       u8 *last_ste,
			       struct mlx5dr_ste_actions_attr *attr,
			       u32 *added_stes);
void mlx5dr_ste_set_actions_tx(struct mlx5dr_ste_ctx *ste_ctx,
			       struct mlx5dr_domain *dmn,
			       u8 *action_type_set,
			       u8 *last_ste,
			       struct mlx5dr_ste_actions_attr *attr,
			       u32 *added_stes);

void mlx5dr_ste_set_action_set(struct mlx5dr_ste_ctx *ste_ctx,
			       __be64 *hw_action,
			       u8 hw_field,
			       u8 shifter,
			       u8 length,
			       u32 data);
void mlx5dr_ste_set_action_add(struct mlx5dr_ste_ctx *ste_ctx,
			       __be64 *hw_action,
			       u8 hw_field,
			       u8 shifter,
			       u8 length,
			       u32 data);
void mlx5dr_ste_set_action_copy(struct mlx5dr_ste_ctx *ste_ctx,
				__be64 *hw_action,
				u8 dst_hw_field,
				u8 dst_shifter,
				u8 dst_len,
				u8 src_hw_field,
				u8 src_shifter);
int mlx5dr_ste_set_action_decap_l3_list(struct mlx5dr_ste_ctx *ste_ctx,
					void *data,
					u32 data_sz,
					u8 *hw_action,
					u32 hw_action_sz,
					u16 *used_hw_action_num);

const struct mlx5dr_ste_action_modify_field *
mlx5dr_ste_conv_modify_hdr_sw_field(struct mlx5dr_ste_ctx *ste_ctx, u16 sw_field);

struct mlx5dr_ste_ctx *mlx5dr_ste_get_ctx(u8 version);
void mlx5dr_ste_free(struct mlx5dr_ste *ste,
		     struct mlx5dr_matcher *matcher,
		     struct mlx5dr_matcher_rx_tx *nic_matcher);
static inline void mlx5dr_ste_put(struct mlx5dr_ste *ste,
				  struct mlx5dr_matcher *matcher,
				  struct mlx5dr_matcher_rx_tx *nic_matcher)
{
	ste->refcount--;
	if (!ste->refcount)
		mlx5dr_ste_free(ste, matcher, nic_matcher);
}

/* initial as 0, increased only when ste appears in a new rule */
static inline void mlx5dr_ste_get(struct mlx5dr_ste *ste)
{
	ste->refcount++;
}

static inline bool mlx5dr_ste_is_not_used(struct mlx5dr_ste *ste)
{
	return !ste->refcount;
}

bool mlx5dr_ste_equal_tag(void *src, void *dst);
int mlx5dr_ste_create_next_htbl(struct mlx5dr_matcher *matcher,
				struct mlx5dr_matcher_rx_tx *nic_matcher,
				struct mlx5dr_ste *ste,
				u8 *cur_hw_ste,
				enum mlx5dr_icm_chunk_size log_table_size);

/* STE build functions */
int mlx5dr_ste_build_pre_check(struct mlx5dr_domain *dmn,
			       u8 match_criteria,
			       struct mlx5dr_match_param *mask,
			       struct mlx5dr_match_param *value);
int mlx5dr_ste_build_ste_arr(struct mlx5dr_matcher *matcher,
			     struct mlx5dr_matcher_rx_tx *nic_matcher,
			     struct mlx5dr_match_param *value,
			     u8 *ste_arr);
void mlx5dr_ste_build_eth_l2_src_dst(struct mlx5dr_ste_ctx *ste_ctx,
				     struct mlx5dr_ste_build *builder,
				     struct mlx5dr_match_param *mask,
				     bool inner, bool rx);
void mlx5dr_ste_build_eth_l3_ipv4_5_tuple(struct mlx5dr_ste_ctx *ste_ctx,
					  struct mlx5dr_ste_build *sb,
					  struct mlx5dr_match_param *mask,
					  bool inner, bool rx);
void mlx5dr_ste_build_eth_l3_ipv4_misc(struct mlx5dr_ste_ctx *ste_ctx,
				       struct mlx5dr_ste_build *sb,
				       struct mlx5dr_match_param *mask,
				       bool inner, bool rx);
void mlx5dr_ste_build_eth_l3_ipv6_dst(struct mlx5dr_ste_ctx *ste_ctx,
				      struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask,
				      bool inner, bool rx);
void mlx5dr_ste_build_eth_l3_ipv6_src(struct mlx5dr_ste_ctx *ste_ctx,
				      struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask,
				      bool inner, bool rx);
void mlx5dr_ste_build_eth_l2_src(struct mlx5dr_ste_ctx *ste_ctx,
				 struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx);
void mlx5dr_ste_build_eth_l2_dst(struct mlx5dr_ste_ctx *ste_ctx,
				 struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx);
void mlx5dr_ste_build_eth_l2_tnl(struct mlx5dr_ste_ctx *ste_ctx,
				 struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx);
void mlx5dr_ste_build_eth_ipv6_l3_l4(struct mlx5dr_ste_ctx *ste_ctx,
				     struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask,
				     bool inner, bool rx);
void mlx5dr_ste_build_eth_l4_misc(struct mlx5dr_ste_ctx *ste_ctx,
				  struct mlx5dr_ste_build *sb,
				  struct mlx5dr_match_param *mask,
				  bool inner, bool rx);
void mlx5dr_ste_build_tnl_gre(struct mlx5dr_ste_ctx *ste_ctx,
			      struct mlx5dr_ste_build *sb,
			      struct mlx5dr_match_param *mask,
			      bool inner, bool rx);
void mlx5dr_ste_build_mpls(struct mlx5dr_ste_ctx *ste_ctx,
			   struct mlx5dr_ste_build *sb,
			   struct mlx5dr_match_param *mask,
			   bool inner, bool rx);
void mlx5dr_ste_build_tnl_mpls(struct mlx5dr_ste_ctx *ste_ctx,
			       struct mlx5dr_ste_build *sb,
			       struct mlx5dr_match_param *mask,
			       bool inner, bool rx);
void mlx5dr_ste_build_tnl_mpls_over_gre(struct mlx5dr_ste_ctx *ste_ctx,
					struct mlx5dr_ste_build *sb,
					struct mlx5dr_match_param *mask,
					struct mlx5dr_cmd_caps *caps,
					bool inner, bool rx);
void mlx5dr_ste_build_tnl_mpls_over_udp(struct mlx5dr_ste_ctx *ste_ctx,
					struct mlx5dr_ste_build *sb,
					struct mlx5dr_match_param *mask,
					struct mlx5dr_cmd_caps *caps,
					bool inner, bool rx);
void mlx5dr_ste_build_icmp(struct mlx5dr_ste_ctx *ste_ctx,
			   struct mlx5dr_ste_build *sb,
			   struct mlx5dr_match_param *mask,
			   struct mlx5dr_cmd_caps *caps,
			   bool inner, bool rx);
void mlx5dr_ste_build_tnl_vxlan_gpe(struct mlx5dr_ste_ctx *ste_ctx,
				    struct mlx5dr_ste_build *sb,
				    struct mlx5dr_match_param *mask,
				    bool inner, bool rx);
void mlx5dr_ste_build_tnl_geneve(struct mlx5dr_ste_ctx *ste_ctx,
				 struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx);
void mlx5dr_ste_build_tnl_geneve_tlv_opt(struct mlx5dr_ste_ctx *ste_ctx,
					 struct mlx5dr_ste_build *sb,
					 struct mlx5dr_match_param *mask,
					 struct mlx5dr_cmd_caps *caps,
					 bool inner, bool rx);
void mlx5dr_ste_build_tnl_geneve_tlv_opt_exist(struct mlx5dr_ste_ctx *ste_ctx,
					       struct mlx5dr_ste_build *sb,
					       struct mlx5dr_match_param *mask,
					       struct mlx5dr_cmd_caps *caps,
					       bool inner, bool rx);
void mlx5dr_ste_build_tnl_gtpu(struct mlx5dr_ste_ctx *ste_ctx,
			       struct mlx5dr_ste_build *sb,
			       struct mlx5dr_match_param *mask,
			       bool inner, bool rx);
void mlx5dr_ste_build_tnl_gtpu_flex_parser_0(struct mlx5dr_ste_ctx *ste_ctx,
					     struct mlx5dr_ste_build *sb,
					     struct mlx5dr_match_param *mask,
					     struct mlx5dr_cmd_caps *caps,
					     bool inner, bool rx);
void mlx5dr_ste_build_tnl_gtpu_flex_parser_1(struct mlx5dr_ste_ctx *ste_ctx,
					     struct mlx5dr_ste_build *sb,
					     struct mlx5dr_match_param *mask,
					     struct mlx5dr_cmd_caps *caps,
					     bool inner, bool rx);
void mlx5dr_ste_build_tnl_header_0_1(struct mlx5dr_ste_ctx *ste_ctx,
				     struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask,
				     bool inner, bool rx);
void mlx5dr_ste_build_general_purpose(struct mlx5dr_ste_ctx *ste_ctx,
				      struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask,
				      bool inner, bool rx);
void mlx5dr_ste_build_register_0(struct mlx5dr_ste_ctx *ste_ctx,
				 struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx);
void mlx5dr_ste_build_register_1(struct mlx5dr_ste_ctx *ste_ctx,
				 struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx);
void mlx5dr_ste_build_src_gvmi_qpn(struct mlx5dr_ste_ctx *ste_ctx,
				   struct mlx5dr_ste_build *sb,
				   struct mlx5dr_match_param *mask,
				   struct mlx5dr_domain *dmn,
				   bool inner, bool rx);
void mlx5dr_ste_build_flex_parser_0(struct mlx5dr_ste_ctx *ste_ctx,
				    struct mlx5dr_ste_build *sb,
				    struct mlx5dr_match_param *mask,
				    bool inner, bool rx);
void mlx5dr_ste_build_flex_parser_1(struct mlx5dr_ste_ctx *ste_ctx,
				    struct mlx5dr_ste_build *sb,
				    struct mlx5dr_match_param *mask,
				    bool inner, bool rx);
void mlx5dr_ste_build_empty_always_hit(struct mlx5dr_ste_build *sb, bool rx);

/* Actions utils */
int mlx5dr_actions_build_ste_arr(struct mlx5dr_matcher *matcher,
				 struct mlx5dr_matcher_rx_tx *nic_matcher,
				 struct mlx5dr_action *actions[],
				 u32 num_actions,
				 u8 *ste_arr,
				 u32 *new_hw_ste_arr_sz);

struct mlx5dr_match_spec {
	u32 smac_47_16;		/* Source MAC address of incoming packet */
	/* Incoming packet Ethertype - this is the Ethertype
	 * following the last VLAN tag of the packet
	 */
	u32 smac_15_0:16;	/* Source MAC address of incoming packet */
	u32 ethertype:16;

	u32 dmac_47_16;		/* Destination MAC address of incoming packet */

	u32 dmac_15_0:16;	/* Destination MAC address of incoming packet */
	/* Priority of first VLAN tag in the incoming packet.
	 * Valid only when cvlan_tag==1 or svlan_tag==1
	 */
	u32 first_prio:3;
	/* CFI bit of first VLAN tag in the incoming packet.
	 * Valid only when cvlan_tag==1 or svlan_tag==1
	 */
	u32 first_cfi:1;
	/* VLAN ID of first VLAN tag in the incoming packet.
	 * Valid only when cvlan_tag==1 or svlan_tag==1
	 */
	u32 first_vid:12;

	u32 ip_protocol:8;	/* IP protocol */
	/* Differentiated Services Code Point derived from
	 * Traffic Class/TOS field of IPv6/v4
	 */
	u32 ip_dscp:6;
	/* Explicit Congestion Notification derived from
	 * Traffic Class/TOS field of IPv6/v4
	 */
	u32 ip_ecn:2;
	/* The first vlan in the packet is c-vlan (0x8100).
	 * cvlan_tag and svlan_tag cannot be set together
	 */
	u32 cvlan_tag:1;
	/* The first vlan in the packet is s-vlan (0x8a88).
	 * cvlan_tag and svlan_tag cannot be set together
	 */
	u32 svlan_tag:1;
	u32 frag:1;		/* Packet is an IP fragment */
	u32 ip_version:4;	/* IP version */
	/* TCP flags. ;Bit 0: FIN;Bit 1: SYN;Bit 2: RST;Bit 3: PSH;Bit 4: ACK;
	 *             Bit 5: URG;Bit 6: ECE;Bit 7: CWR;Bit 8: NS
	 */
	u32 tcp_flags:9;

	/* TCP source port.;tcp and udp sport/dport are mutually exclusive */
	u32 tcp_sport:16;
	/* TCP destination port.
	 * tcp and udp sport/dport are mutually exclusive
	 */
	u32 tcp_dport:16;

	u32 reserved_auto1:16;
	u32 ipv4_ihl:4;
	u32 reserved_auto2:4;
	u32 ttl_hoplimit:8;

	/* UDP source port.;tcp and udp sport/dport are mutually exclusive */
	u32 udp_sport:16;
	/* UDP destination port.;tcp and udp sport/dport are mutually exclusive */
	u32 udp_dport:16;

	/* IPv6 source address of incoming packets
	 * For IPv4 address use bits 31:0 (rest of the bits are reserved)
	 * This field should be qualified by an appropriate ethertype
	 */
	u32 src_ip_127_96;
	/* IPv6 source address of incoming packets
	 * For IPv4 address use bits 31:0 (rest of the bits are reserved)
	 * This field should be qualified by an appropriate ethertype
	 */
	u32 src_ip_95_64;
	/* IPv6 source address of incoming packets
	 * For IPv4 address use bits 31:0 (rest of the bits are reserved)
	 * This field should be qualified by an appropriate ethertype
	 */
	u32 src_ip_63_32;
	/* IPv6 source address of incoming packets
	 * For IPv4 address use bits 31:0 (rest of the bits are reserved)
	 * This field should be qualified by an appropriate ethertype
	 */
	u32 src_ip_31_0;
	/* IPv6 destination address of incoming packets
	 * For IPv4 address use bits 31:0 (rest of the bits are reserved)
	 * This field should be qualified by an appropriate ethertype
	 */
	u32 dst_ip_127_96;
	/* IPv6 destination address of incoming packets
	 * For IPv4 address use bits 31:0 (rest of the bits are reserved)
	 * This field should be qualified by an appropriate ethertype
	 */
	u32 dst_ip_95_64;
	/* IPv6 destination address of incoming packets
	 * For IPv4 address use bits 31:0 (rest of the bits are reserved)
	 * This field should be qualified by an appropriate ethertype
	 */
	u32 dst_ip_63_32;
	/* IPv6 destination address of incoming packets
	 * For IPv4 address use bits 31:0 (rest of the bits are reserved)
	 * This field should be qualified by an appropriate ethertype
	 */
	u32 dst_ip_31_0;
};

struct mlx5dr_match_misc {
	/* used with GRE, checksum exist when gre_c_present == 1 */
	u32 gre_c_present:1;
	u32 reserved_auto1:1;
	/* used with GRE, key exist when gre_k_present == 1 */
	u32 gre_k_present:1;
	/* used with GRE, sequence number exist when gre_s_present == 1 */
	u32 gre_s_present:1;
	u32 source_vhca_port:4;
	u32 source_sqn:24;		/* Source SQN */

	u32 source_eswitch_owner_vhca_id:16;
	/* Source port.;0xffff determines wire port */
	u32 source_port:16;

	/* Priority of second VLAN tag in the outer header of the incoming packet.
	 * Valid only when outer_second_cvlan_tag ==1 or outer_second_svlan_tag ==1
	 */
	u32 outer_second_prio:3;
	/* CFI bit of first VLAN tag in the outer header of the incoming packet.
	 * Valid only when outer_second_cvlan_tag ==1 or outer_second_svlan_tag ==1
	 */
	u32 outer_second_cfi:1;
	/* VLAN ID of first VLAN tag the outer header of the incoming packet.
	 * Valid only when outer_second_cvlan_tag ==1 or outer_second_svlan_tag ==1
	 */
	u32 outer_second_vid:12;
	/* Priority of second VLAN tag in the inner header of the incoming packet.
	 * Valid only when inner_second_cvlan_tag ==1 or inner_second_svlan_tag ==1
	 */
	u32 inner_second_prio:3;
	/* CFI bit of first VLAN tag in the inner header of the incoming packet.
	 * Valid only when inner_second_cvlan_tag ==1 or inner_second_svlan_tag ==1
	 */
	u32 inner_second_cfi:1;
	/* VLAN ID of first VLAN tag the inner header of the incoming packet.
	 * Valid only when inner_second_cvlan_tag ==1 or inner_second_svlan_tag ==1
	 */
	u32 inner_second_vid:12;

	u32 outer_second_cvlan_tag:1;
	u32 inner_second_cvlan_tag:1;
	/* The second vlan in the outer header of the packet is c-vlan (0x8100).
	 * outer_second_cvlan_tag and outer_second_svlan_tag cannot be set together
	 */
	u32 outer_second_svlan_tag:1;
	/* The second vlan in the inner header of the packet is c-vlan (0x8100).
	 * inner_second_cvlan_tag and inner_second_svlan_tag cannot be set together
	 */
	u32 inner_second_svlan_tag:1;
	/* The second vlan in the outer header of the packet is s-vlan (0x8a88).
	 * outer_second_cvlan_tag and outer_second_svlan_tag cannot be set together
	 */
	u32 reserved_auto2:12;
	/* The second vlan in the inner header of the packet is s-vlan (0x8a88).
	 * inner_second_cvlan_tag and inner_second_svlan_tag cannot be set together
	 */
	u32 gre_protocol:16;		/* GRE Protocol (outer) */

	u32 gre_key_h:24;		/* GRE Key[31:8] (outer) */
	u32 gre_key_l:8;		/* GRE Key [7:0] (outer) */

	u32 vxlan_vni:24;		/* VXLAN VNI (outer) */
	u32 reserved_auto3:8;

	u32 geneve_vni:24;		/* GENEVE VNI field (outer) */
	u32 reserved_auto4:6;
	u32 geneve_tlv_option_0_exist:1;
	u32 geneve_oam:1;		/* GENEVE OAM field (outer) */

	u32 reserved_auto5:12;
	u32 outer_ipv6_flow_label:20;	/* Flow label of incoming IPv6 packet (outer) */

	u32 reserved_auto6:12;
	u32 inner_ipv6_flow_label:20;	/* Flow label of incoming IPv6 packet (inner) */

	u32 reserved_auto7:10;
	u32 geneve_opt_len:6;		/* GENEVE OptLen (outer) */
	u32 geneve_protocol_type:16;	/* GENEVE protocol type (outer) */

	u32 reserved_auto8:8;
	u32 bth_dst_qp:24;		/* Destination QP in BTH header */

	u32 reserved_auto9;
	u32 outer_esp_spi;
	u32 reserved_auto10[3];
};

struct mlx5dr_match_misc2 {
	u32 outer_first_mpls_label:20;		/* First MPLS LABEL (outer) */
	u32 outer_first_mpls_exp:3;		/* First MPLS EXP (outer) */
	u32 outer_first_mpls_s_bos:1;		/* First MPLS S_BOS (outer) */
	u32 outer_first_mpls_ttl:8;		/* First MPLS TTL (outer) */

	u32 inner_first_mpls_label:20;		/* First MPLS LABEL (inner) */
	u32 inner_first_mpls_exp:3;		/* First MPLS EXP (inner) */
	u32 inner_first_mpls_s_bos:1;		/* First MPLS S_BOS (inner) */
	u32 inner_first_mpls_ttl:8;		/* First MPLS TTL (inner) */

	u32 outer_first_mpls_over_gre_label:20;	/* last MPLS LABEL (outer) */
	u32 outer_first_mpls_over_gre_exp:3;	/* last MPLS EXP (outer) */
	u32 outer_first_mpls_over_gre_s_bos:1;	/* last MPLS S_BOS (outer) */
	u32 outer_first_mpls_over_gre_ttl:8;	/* last MPLS TTL (outer) */

	u32 outer_first_mpls_over_udp_label:20;	/* last MPLS LABEL (outer) */
	u32 outer_first_mpls_over_udp_exp:3;	/* last MPLS EXP (outer) */
	u32 outer_first_mpls_over_udp_s_bos:1;	/* last MPLS S_BOS (outer) */
	u32 outer_first_mpls_over_udp_ttl:8;	/* last MPLS TTL (outer) */

	u32 metadata_reg_c_7;			/* metadata_reg_c_7 */
	u32 metadata_reg_c_6;			/* metadata_reg_c_6 */
	u32 metadata_reg_c_5;			/* metadata_reg_c_5 */
	u32 metadata_reg_c_4;			/* metadata_reg_c_4 */
	u32 metadata_reg_c_3;			/* metadata_reg_c_3 */
	u32 metadata_reg_c_2;			/* metadata_reg_c_2 */
	u32 metadata_reg_c_1;			/* metadata_reg_c_1 */
	u32 metadata_reg_c_0;			/* metadata_reg_c_0 */
	u32 metadata_reg_a;			/* metadata_reg_a */
	u32 reserved_auto1[3];
};

struct mlx5dr_match_misc3 {
	u32 inner_tcp_seq_num;
	u32 outer_tcp_seq_num;
	u32 inner_tcp_ack_num;
	u32 outer_tcp_ack_num;

	u32 reserved_auto1:8;
	u32 outer_vxlan_gpe_vni:24;

	u32 outer_vxlan_gpe_next_protocol:8;
	u32 outer_vxlan_gpe_flags:8;
	u32 reserved_auto2:16;

	u32 icmpv4_header_data;
	u32 icmpv6_header_data;

	u8 icmpv4_type;
	u8 icmpv4_code;
	u8 icmpv6_type;
	u8 icmpv6_code;

	u32 geneve_tlv_option_0_data;

	u32 gtpu_teid;

	u8 gtpu_msg_type;
	u8 gtpu_msg_flags;
	u32 reserved_auto3:16;

	u32 gtpu_dw_2;
	u32 gtpu_first_ext_dw_0;
	u32 gtpu_dw_0;
	u32 reserved_auto4;
};

struct mlx5dr_match_misc4 {
	u32 prog_sample_field_value_0;
	u32 prog_sample_field_id_0;
	u32 prog_sample_field_value_1;
	u32 prog_sample_field_id_1;
	u32 prog_sample_field_value_2;
	u32 prog_sample_field_id_2;
	u32 prog_sample_field_value_3;
	u32 prog_sample_field_id_3;
	u32 reserved_auto1[8];
};

struct mlx5dr_match_misc5 {
	u32 macsec_tag_0;
	u32 macsec_tag_1;
	u32 macsec_tag_2;
	u32 macsec_tag_3;
	u32 tunnel_header_0;
	u32 tunnel_header_1;
	u32 tunnel_header_2;
	u32 tunnel_header_3;
};

struct mlx5dr_match_param {
	struct mlx5dr_match_spec outer;
	struct mlx5dr_match_misc misc;
	struct mlx5dr_match_spec inner;
	struct mlx5dr_match_misc2 misc2;
	struct mlx5dr_match_misc3 misc3;
	struct mlx5dr_match_misc4 misc4;
	struct mlx5dr_match_misc5 misc5;
};

#define DR_MASK_IS_ICMPV4_SET(_misc3) ((_misc3)->icmpv4_type || \
				       (_misc3)->icmpv4_code || \
				       (_misc3)->icmpv4_header_data)

#define DR_MASK_IS_SRC_IP_SET(_spec) ((_spec)->src_ip_127_96 || \
				      (_spec)->src_ip_95_64  || \
				      (_spec)->src_ip_63_32  || \
				      (_spec)->src_ip_31_0)

#define DR_MASK_IS_DST_IP_SET(_spec) ((_spec)->dst_ip_127_96 || \
				      (_spec)->dst_ip_95_64  || \
				      (_spec)->dst_ip_63_32  || \
				      (_spec)->dst_ip_31_0)

struct mlx5dr_esw_caps {
	u64 drop_icm_address_rx;
	u64 drop_icm_address_tx;
	u64 uplink_icm_address_rx;
	u64 uplink_icm_address_tx;
	u8 sw_owner:1;
	u8 sw_owner_v2:1;
};

struct mlx5dr_cmd_vport_cap {
	u16 vport_gvmi;
	u16 vhca_gvmi;
	u16 num;
	u64 icm_address_rx;
	u64 icm_address_tx;
};

struct mlx5dr_roce_cap {
	u8 roce_en:1;
	u8 fl_rc_qp_when_roce_disabled:1;
	u8 fl_rc_qp_when_roce_enabled:1;
};

struct mlx5dr_vports {
	struct mlx5dr_cmd_vport_cap esw_manager_caps;
	struct mlx5dr_cmd_vport_cap uplink_caps;
	struct xarray vports_caps_xa;
};

struct mlx5dr_cmd_caps {
	u16 gvmi;
	u64 nic_rx_drop_address;
	u64 nic_tx_drop_address;
	u64 nic_tx_allow_address;
	u64 esw_rx_drop_address;
	u64 esw_tx_drop_address;
	u32 log_icm_size;
	u64 hdr_modify_icm_addr;
	u32 flex_protocols;
	u8 flex_parser_id_icmp_dw0;
	u8 flex_parser_id_icmp_dw1;
	u8 flex_parser_id_icmpv6_dw0;
	u8 flex_parser_id_icmpv6_dw1;
	u8 flex_parser_id_geneve_tlv_option_0;
	u8 flex_parser_id_mpls_over_gre;
	u8 flex_parser_id_mpls_over_udp;
	u8 flex_parser_id_gtpu_dw_0;
	u8 flex_parser_id_gtpu_teid;
	u8 flex_parser_id_gtpu_dw_2;
	u8 flex_parser_id_gtpu_first_ext_dw_0;
	u8 flex_parser_ok_bits_supp;
	u8 max_ft_level;
	u16 roce_min_src_udp;
	u8 sw_format_ver;
	bool eswitch_manager;
	bool rx_sw_owner;
	bool tx_sw_owner;
	bool fdb_sw_owner;
	u8 rx_sw_owner_v2:1;
	u8 tx_sw_owner_v2:1;
	u8 fdb_sw_owner_v2:1;
	struct mlx5dr_esw_caps esw_caps;
	struct mlx5dr_vports vports;
	bool prio_tag_required;
	struct mlx5dr_roce_cap roce_caps;
	u8 is_ecpf:1;
	u8 isolate_vl_tc:1;
};

enum mlx5dr_domain_nic_type {
	DR_DOMAIN_NIC_TYPE_RX,
	DR_DOMAIN_NIC_TYPE_TX,
};

struct mlx5dr_domain_rx_tx {
	u64 drop_icm_addr;
	u64 default_icm_addr;
	enum mlx5dr_domain_nic_type type;
	struct mutex mutex; /* protect rx/tx domain */
};

struct mlx5dr_domain_info {
	bool supp_sw_steering;
	u32 max_inline_size;
	u32 max_send_wr;
	u32 max_log_sw_icm_sz;
	u32 max_log_action_icm_sz;
	struct mlx5dr_domain_rx_tx rx;
	struct mlx5dr_domain_rx_tx tx;
	struct mlx5dr_cmd_caps caps;
};

struct mlx5dr_domain {
	struct mlx5dr_domain *peer_dmn;
	struct mlx5_core_dev *mdev;
	u32 pdn;
	struct mlx5_uars_page *uar;
	enum mlx5dr_domain_type type;
	refcount_t refcount;
	struct mlx5dr_icm_pool *ste_icm_pool;
	struct mlx5dr_icm_pool *action_icm_pool;
	struct mlx5dr_send_ring *send_ring;
	struct mlx5dr_domain_info info;
	struct xarray csum_fts_xa;
	struct mlx5dr_ste_ctx *ste_ctx;
	struct list_head dbg_tbl_list;
	struct mlx5dr_dbg_dump_info dump_info;
};

struct mlx5dr_table_rx_tx {
	struct mlx5dr_ste_htbl *s_anchor;
	struct mlx5dr_domain_rx_tx *nic_dmn;
	u64 default_icm_addr;
	struct list_head nic_matcher_list;
};

struct mlx5dr_table {
	struct mlx5dr_domain *dmn;
	struct mlx5dr_table_rx_tx rx;
	struct mlx5dr_table_rx_tx tx;
	u32 level;
	u32 table_type;
	u32 table_id;
	u32 flags;
	struct list_head matcher_list;
	struct mlx5dr_action *miss_action;
	refcount_t refcount;
	struct list_head dbg_node;
};

struct mlx5dr_matcher_rx_tx {
	struct mlx5dr_ste_htbl *s_htbl;
	struct mlx5dr_ste_htbl *e_anchor;
	struct mlx5dr_ste_build *ste_builder;
	struct mlx5dr_ste_build ste_builder_arr[DR_RULE_IPV_MAX]
					       [DR_RULE_IPV_MAX]
					       [DR_RULE_MAX_STES];
	u8 num_of_builders;
	u8 num_of_builders_arr[DR_RULE_IPV_MAX][DR_RULE_IPV_MAX];
	u64 default_icm_addr;
	struct mlx5dr_table_rx_tx *nic_tbl;
	u32 prio;
	struct list_head list_node;
	u32 rules;
};

struct mlx5dr_matcher {
	struct mlx5dr_table *tbl;
	struct mlx5dr_matcher_rx_tx rx;
	struct mlx5dr_matcher_rx_tx tx;
	struct list_head list_node; /* Used for both matchers and dbg managing */
	u32 prio;
	struct mlx5dr_match_param mask;
	u8 match_criteria;
	refcount_t refcount;
	struct list_head dbg_rule_list;
};

struct mlx5dr_ste_action_modify_field {
	u16 hw_field;
	u8 start;
	u8 end;
	u8 l3_type;
	u8 l4_type;
};

struct mlx5dr_action_rewrite {
	struct mlx5dr_domain *dmn;
	struct mlx5dr_icm_chunk *chunk;
	u8 *data;
	u16 num_of_actions;
	u32 index;
	u8 allow_rx:1;
	u8 allow_tx:1;
	u8 modify_ttl:1;
};

struct mlx5dr_action_reformat {
	struct mlx5dr_domain *dmn;
	u32 id;
	u32 size;
	u8 param_0;
	u8 param_1;
};

struct mlx5dr_action_sampler {
	struct mlx5dr_domain *dmn;
	u64 rx_icm_addr;
	u64 tx_icm_addr;
	u32 sampler_id;
};

struct mlx5dr_action_dest_tbl {
	u8 is_fw_tbl:1;
	union {
		struct mlx5dr_table *tbl;
		struct {
			struct mlx5dr_domain *dmn;
			u32 id;
			u32 group_id;
			enum fs_flow_table_type type;
			u64 rx_icm_addr;
			u64 tx_icm_addr;
			struct mlx5dr_action **ref_actions;
			u32 num_of_ref_actions;
		} fw_tbl;
	};
};

struct mlx5dr_action_ctr {
	u32 ctr_id;
	u32 offset;
};

struct mlx5dr_action_vport {
	struct mlx5dr_domain *dmn;
	struct mlx5dr_cmd_vport_cap *caps;
};

struct mlx5dr_action_push_vlan {
	u32 vlan_hdr; /* tpid_pcp_dei_vid */
};

struct mlx5dr_action_flow_tag {
	u32 flow_tag;
};

struct mlx5dr_rule_action_member {
	struct mlx5dr_action *action;
	struct list_head list;
};

struct mlx5dr_action {
	enum mlx5dr_action_type action_type;
	refcount_t refcount;

	union {
		void *data;
		struct mlx5dr_action_rewrite *rewrite;
		struct mlx5dr_action_reformat *reformat;
		struct mlx5dr_action_sampler *sampler;
		struct mlx5dr_action_dest_tbl *dest_tbl;
		struct mlx5dr_action_ctr *ctr;
		struct mlx5dr_action_vport *vport;
		struct mlx5dr_action_push_vlan *push_vlan;
		struct mlx5dr_action_flow_tag *flow_tag;
	};
};

enum mlx5dr_connect_type {
	CONNECT_HIT	= 1,
	CONNECT_MISS	= 2,
};

struct mlx5dr_htbl_connect_info {
	enum mlx5dr_connect_type type;
	union {
		struct mlx5dr_ste_htbl *hit_next_htbl;
		u64 miss_icm_addr;
	};
};

struct mlx5dr_rule_rx_tx {
	struct mlx5dr_matcher_rx_tx *nic_matcher;
	struct mlx5dr_ste *last_rule_ste;
};

struct mlx5dr_rule {
	struct mlx5dr_matcher *matcher;
	struct mlx5dr_rule_rx_tx rx;
	struct mlx5dr_rule_rx_tx tx;
	struct list_head rule_actions_list;
	struct list_head dbg_node;
	u32 flow_source;
};

void mlx5dr_rule_set_last_member(struct mlx5dr_rule_rx_tx *nic_rule,
				 struct mlx5dr_ste *ste,
				 bool force);
int mlx5dr_rule_get_reverse_rule_members(struct mlx5dr_ste **ste_arr,
					 struct mlx5dr_ste *curr_ste,
					 int *num_of_stes);

struct mlx5dr_icm_chunk {
	struct mlx5dr_icm_buddy_mem *buddy_mem;
	struct list_head chunk_list;

	/* indicates the index of this chunk in the whole memory,
	 * used for deleting the chunk from the buddy
	 */
	unsigned int seg;
	enum mlx5dr_icm_chunk_size size;

	/* Memory optimisation */
	struct mlx5dr_ste *ste_arr;
	u8 *hw_ste_arr;
	struct list_head *miss_list;
};

static inline void mlx5dr_domain_nic_lock(struct mlx5dr_domain_rx_tx *nic_dmn)
{
	mutex_lock(&nic_dmn->mutex);
}

static inline void mlx5dr_domain_nic_unlock(struct mlx5dr_domain_rx_tx *nic_dmn)
{
	mutex_unlock(&nic_dmn->mutex);
}

static inline void mlx5dr_domain_lock(struct mlx5dr_domain *dmn)
{
	mlx5dr_domain_nic_lock(&dmn->info.rx);
	mlx5dr_domain_nic_lock(&dmn->info.tx);
}

static inline void mlx5dr_domain_unlock(struct mlx5dr_domain *dmn)
{
	mlx5dr_domain_nic_unlock(&dmn->info.tx);
	mlx5dr_domain_nic_unlock(&dmn->info.rx);
}

int mlx5dr_matcher_add_to_tbl_nic(struct mlx5dr_domain *dmn,
				  struct mlx5dr_matcher_rx_tx *nic_matcher);
int mlx5dr_matcher_remove_from_tbl_nic(struct mlx5dr_domain *dmn,
				       struct mlx5dr_matcher_rx_tx *nic_matcher);

int mlx5dr_matcher_select_builders(struct mlx5dr_matcher *matcher,
				   struct mlx5dr_matcher_rx_tx *nic_matcher,
				   enum mlx5dr_ipv outer_ipv,
				   enum mlx5dr_ipv inner_ipv);

u64 mlx5dr_icm_pool_get_chunk_mr_addr(struct mlx5dr_icm_chunk *chunk);
u32 mlx5dr_icm_pool_get_chunk_rkey(struct mlx5dr_icm_chunk *chunk);
u64 mlx5dr_icm_pool_get_chunk_icm_addr(struct mlx5dr_icm_chunk *chunk);
u32 mlx5dr_icm_pool_get_chunk_num_of_entries(struct mlx5dr_icm_chunk *chunk);
u32 mlx5dr_icm_pool_get_chunk_byte_size(struct mlx5dr_icm_chunk *chunk);
u8 *mlx5dr_ste_get_hw_ste(struct mlx5dr_ste *ste);

static inline int
mlx5dr_icm_pool_dm_type_to_entry_size(enum mlx5dr_icm_type icm_type)
{
	if (icm_type == DR_ICM_TYPE_STE)
		return DR_STE_SIZE;

	return DR_MODIFY_ACTION_SIZE;
}

static inline u32
mlx5dr_icm_pool_chunk_size_to_entries(enum mlx5dr_icm_chunk_size chunk_size)
{
	return 1 << chunk_size;
}

static inline int
mlx5dr_icm_pool_chunk_size_to_byte(enum mlx5dr_icm_chunk_size chunk_size,
				   enum mlx5dr_icm_type icm_type)
{
	int num_of_entries;
	int entry_size;

	entry_size = mlx5dr_icm_pool_dm_type_to_entry_size(icm_type);
	num_of_entries = mlx5dr_icm_pool_chunk_size_to_entries(chunk_size);

	return entry_size * num_of_entries;
}

static inline int
mlx5dr_ste_htbl_increase_threshold(struct mlx5dr_ste_htbl *htbl)
{
	int num_of_entries =
		mlx5dr_icm_pool_chunk_size_to_entries(htbl->chunk->size);

	/* Threshold is 50%, one is added to table of size 1 */
	return (num_of_entries + 1) / 2;
}

static inline bool
mlx5dr_ste_htbl_may_grow(struct mlx5dr_ste_htbl *htbl)
{
	if (htbl->chunk->size == DR_CHUNK_SIZE_MAX - 1 || !htbl->byte_mask)
		return false;

	return true;
}

struct mlx5dr_cmd_vport_cap *
mlx5dr_domain_get_vport_cap(struct mlx5dr_domain *dmn, u16 vport);

struct mlx5dr_cmd_query_flow_table_details {
	u8 status;
	u8 level;
	u64 sw_owner_icm_root_1;
	u64 sw_owner_icm_root_0;
};

struct mlx5dr_cmd_create_flow_table_attr {
	u32 table_type;
	u64 icm_addr_rx;
	u64 icm_addr_tx;
	u8 level;
	bool sw_owner;
	bool term_tbl;
	bool decap_en;
	bool reformat_en;
};

/* internal API functions */
int mlx5dr_cmd_query_device(struct mlx5_core_dev *mdev,
			    struct mlx5dr_cmd_caps *caps);
int mlx5dr_cmd_query_esw_vport_context(struct mlx5_core_dev *mdev,
				       bool other_vport, u16 vport_number,
				       u64 *icm_address_rx,
				       u64 *icm_address_tx);
int mlx5dr_cmd_query_gvmi(struct mlx5_core_dev *mdev,
			  bool other_vport, u16 vport_number, u16 *gvmi);
int mlx5dr_cmd_query_esw_caps(struct mlx5_core_dev *mdev,
			      struct mlx5dr_esw_caps *caps);
int mlx5dr_cmd_query_flow_sampler(struct mlx5_core_dev *dev,
				  u32 sampler_id,
				  u64 *rx_icm_addr,
				  u64 *tx_icm_addr);
int mlx5dr_cmd_sync_steering(struct mlx5_core_dev *mdev);
int mlx5dr_cmd_set_fte_modify_and_vport(struct mlx5_core_dev *mdev,
					u32 table_type,
					u32 table_id,
					u32 group_id,
					u32 modify_header_id,
					u16 vport_id);
int mlx5dr_cmd_del_flow_table_entry(struct mlx5_core_dev *mdev,
				    u32 table_type,
				    u32 table_id);
int mlx5dr_cmd_alloc_modify_header(struct mlx5_core_dev *mdev,
				   u32 table_type,
				   u8 num_of_actions,
				   u64 *actions,
				   u32 *modify_header_id);
int mlx5dr_cmd_dealloc_modify_header(struct mlx5_core_dev *mdev,
				     u32 modify_header_id);
int mlx5dr_cmd_create_empty_flow_group(struct mlx5_core_dev *mdev,
				       u32 table_type,
				       u32 table_id,
				       u32 *group_id);
int mlx5dr_cmd_destroy_flow_group(struct mlx5_core_dev *mdev,
				  u32 table_type,
				  u32 table_id,
				  u32 group_id);
int mlx5dr_cmd_create_flow_table(struct mlx5_core_dev *mdev,
				 struct mlx5dr_cmd_create_flow_table_attr *attr,
				 u64 *fdb_rx_icm_addr,
				 u32 *table_id);
int mlx5dr_cmd_destroy_flow_table(struct mlx5_core_dev *mdev,
				  u32 table_id,
				  u32 table_type);
int mlx5dr_cmd_query_flow_table(struct mlx5_core_dev *dev,
				enum fs_flow_table_type type,
				u32 table_id,
				struct mlx5dr_cmd_query_flow_table_details *output);
int mlx5dr_cmd_create_reformat_ctx(struct mlx5_core_dev *mdev,
				   enum mlx5_reformat_ctx_type rt,
				   u8 reformat_param_0,
				   u8 reformat_param_1,
				   size_t reformat_size,
				   void *reformat_data,
				   u32 *reformat_id);
void mlx5dr_cmd_destroy_reformat_ctx(struct mlx5_core_dev *mdev,
				     u32 reformat_id);

struct mlx5dr_cmd_gid_attr {
	u8 gid[16];
	u8 mac[6];
	u32 roce_ver;
};

struct mlx5dr_cmd_qp_create_attr {
	u32 page_id;
	u32 pdn;
	u32 cqn;
	u32 pm_state;
	u32 service_type;
	u32 buff_umem_id;
	u32 db_umem_id;
	u32 sq_wqe_cnt;
	u32 rq_wqe_cnt;
	u32 rq_wqe_shift;
	u8 isolate_vl_tc:1;
};

int mlx5dr_cmd_query_gid(struct mlx5_core_dev *mdev, u8 vhca_port_num,
			 u16 index, struct mlx5dr_cmd_gid_attr *attr);

struct mlx5dr_icm_pool *mlx5dr_icm_pool_create(struct mlx5dr_domain *dmn,
					       enum mlx5dr_icm_type icm_type);
void mlx5dr_icm_pool_destroy(struct mlx5dr_icm_pool *pool);

struct mlx5dr_icm_chunk *
mlx5dr_icm_alloc_chunk(struct mlx5dr_icm_pool *pool,
		       enum mlx5dr_icm_chunk_size chunk_size);
void mlx5dr_icm_free_chunk(struct mlx5dr_icm_chunk *chunk);

void mlx5dr_ste_prepare_for_postsend(struct mlx5dr_ste_ctx *ste_ctx,
				     u8 *hw_ste_p, u32 ste_size);
int mlx5dr_ste_htbl_init_and_postsend(struct mlx5dr_domain *dmn,
				      struct mlx5dr_domain_rx_tx *nic_dmn,
				      struct mlx5dr_ste_htbl *htbl,
				      struct mlx5dr_htbl_connect_info *connect_info,
				      bool update_hw_ste);
void mlx5dr_ste_set_formatted_ste(struct mlx5dr_ste_ctx *ste_ctx,
				  u16 gvmi,
				  enum mlx5dr_domain_nic_type nic_type,
				  struct mlx5dr_ste_htbl *htbl,
				  u8 *formatted_ste,
				  struct mlx5dr_htbl_connect_info *connect_info);
void mlx5dr_ste_copy_param(u8 match_criteria,
			   struct mlx5dr_match_param *set_param,
			   struct mlx5dr_match_parameters *mask,
			   bool clear);

struct mlx5dr_qp {
	struct mlx5_core_dev *mdev;
	struct mlx5_wq_qp wq;
	struct mlx5_uars_page *uar;
	struct mlx5_wq_ctrl wq_ctrl;
	u32 qpn;
	struct {
		unsigned int pc;
		unsigned int cc;
		unsigned int size;
		unsigned int *wqe_head;
		unsigned int wqe_cnt;
	} sq;
	struct {
		unsigned int pc;
		unsigned int cc;
		unsigned int size;
		unsigned int wqe_cnt;
	} rq;
	int max_inline_data;
};

struct mlx5dr_cq {
	struct mlx5_core_dev *mdev;
	struct mlx5_cqwq wq;
	struct mlx5_wq_ctrl wq_ctrl;
	struct mlx5_core_cq mcq;
	struct mlx5dr_qp *qp;
};

struct mlx5dr_mr {
	struct mlx5_core_dev *mdev;
	u32 mkey;
	dma_addr_t dma_addr;
	void *addr;
	size_t size;
};

#define MAX_SEND_CQE		64
#define MIN_READ_SYNC		64

struct mlx5dr_send_ring {
	struct mlx5dr_cq *cq;
	struct mlx5dr_qp *qp;
	struct mlx5dr_mr *mr;
	/* How much wqes are waiting for completion */
	u32 pending_wqe;
	/* Signal request per this trash hold value */
	u16 signal_th;
	/* Each post_send_size less than max_post_send_size */
	u32 max_post_send_size;
	/* manage the send queue */
	u32 tx_head;
	void *buf;
	u32 buf_size;
	u8 sync_buff[MIN_READ_SYNC];
	struct mlx5dr_mr *sync_mr;
	spinlock_t lock; /* Protect the data path of the send ring */
	bool err_state; /* send_ring is not usable in err state */
};

int mlx5dr_send_ring_alloc(struct mlx5dr_domain *dmn);
void mlx5dr_send_ring_free(struct mlx5dr_domain *dmn,
			   struct mlx5dr_send_ring *send_ring);
int mlx5dr_send_ring_force_drain(struct mlx5dr_domain *dmn);
int mlx5dr_send_postsend_ste(struct mlx5dr_domain *dmn,
			     struct mlx5dr_ste *ste,
			     u8 *data,
			     u16 size,
			     u16 offset);
int mlx5dr_send_postsend_htbl(struct mlx5dr_domain *dmn,
			      struct mlx5dr_ste_htbl *htbl,
			      u8 *formatted_ste, u8 *mask);
int mlx5dr_send_postsend_formatted_htbl(struct mlx5dr_domain *dmn,
					struct mlx5dr_ste_htbl *htbl,
					u8 *ste_init_data,
					bool update_hw_ste);
int mlx5dr_send_postsend_action(struct mlx5dr_domain *dmn,
				struct mlx5dr_action *action);

struct mlx5dr_cmd_ft_info {
	u32 id;
	u16 vport;
	enum fs_flow_table_type type;
};

struct mlx5dr_cmd_flow_destination_hw_info {
	enum mlx5_flow_destination_type type;
	union {
		u32 tir_num;
		u32 ft_num;
		u32 ft_id;
		u32 counter_id;
		u32 sampler_id;
		struct {
			u16 num;
			u16 vhca_id;
			u32 reformat_id;
			u8 flags;
		} vport;
	};
};

struct mlx5dr_cmd_fte_info {
	u32 dests_size;
	u32 index;
	struct mlx5_flow_context flow_context;
	u32 *val;
	struct mlx5_flow_act action;
	struct mlx5dr_cmd_flow_destination_hw_info *dest_arr;
	bool ignore_flow_level;
};

int mlx5dr_cmd_set_fte(struct mlx5_core_dev *dev,
		       int opmod, int modify_mask,
		       struct mlx5dr_cmd_ft_info *ft,
		       u32 group_id,
		       struct mlx5dr_cmd_fte_info *fte);

bool mlx5dr_ste_supp_ttl_cs_recalc(struct mlx5dr_cmd_caps *caps);

struct mlx5dr_fw_recalc_cs_ft {
	u64 rx_icm_addr;
	u32 table_id;
	u32 group_id;
	u32 modify_hdr_id;
};

struct mlx5dr_fw_recalc_cs_ft *
mlx5dr_fw_create_recalc_cs_ft(struct mlx5dr_domain *dmn, u16 vport_num);
void mlx5dr_fw_destroy_recalc_cs_ft(struct mlx5dr_domain *dmn,
				    struct mlx5dr_fw_recalc_cs_ft *recalc_cs_ft);
int mlx5dr_domain_get_recalc_cs_ft_addr(struct mlx5dr_domain *dmn,
					u16 vport_num,
					u64 *rx_icm_addr);
int mlx5dr_fw_create_md_tbl(struct mlx5dr_domain *dmn,
			    struct mlx5dr_cmd_flow_destination_hw_info *dest,
			    int num_dest,
			    bool reformat_req,
			    u32 *tbl_id,
			    u32 *group_id,
			    bool ignore_flow_level);
void mlx5dr_fw_destroy_md_tbl(struct mlx5dr_domain *dmn, u32 tbl_id,
			      u32 group_id);
#endif  /* _DR_TYPES_H_ */
