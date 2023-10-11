/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (C) 2022 Microchip Technology Inc. and its subsidiaries.
 * Microchip VCAP API
 */

#ifndef __VCAP_API__
#define __VCAP_API__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/netdevice.h>

/* Use the generated API model */
#include "vcap_ag_api.h"

#define VCAP_CID_LOOKUP_SIZE          100000 /* Chains in a lookup */
#define VCAP_CID_INGRESS_L0          1000000 /* Ingress Stage 1 Lookup 0 */
#define VCAP_CID_INGRESS_L1          1100000 /* Ingress Stage 1 Lookup 1 */
#define VCAP_CID_INGRESS_L2          1200000 /* Ingress Stage 1 Lookup 2 */
#define VCAP_CID_INGRESS_L3          1300000 /* Ingress Stage 1 Lookup 3 */
#define VCAP_CID_INGRESS_L4          1400000 /* Ingress Stage 1 Lookup 4 */
#define VCAP_CID_INGRESS_L5          1500000 /* Ingress Stage 1 Lookup 5 */

#define VCAP_CID_PREROUTING_IPV6     3000000 /* Prerouting Stage */
#define VCAP_CID_PREROUTING          6000000 /* Prerouting Stage */

#define VCAP_CID_INGRESS_STAGE2_L0   8000000 /* Ingress Stage 2 Lookup 0 */
#define VCAP_CID_INGRESS_STAGE2_L1   8100000 /* Ingress Stage 2 Lookup 1 */
#define VCAP_CID_INGRESS_STAGE2_L2   8200000 /* Ingress Stage 2 Lookup 2 */
#define VCAP_CID_INGRESS_STAGE2_L3   8300000 /* Ingress Stage 2 Lookup 3 */

#define VCAP_CID_EGRESS_L0           10000000 /* Egress Lookup 0 */
#define VCAP_CID_EGRESS_L1           10100000 /* Egress Lookup 1 */

#define VCAP_CID_EGRESS_STAGE2_L0    20000000 /* Egress Stage 2 Lookup 0 */
#define VCAP_CID_EGRESS_STAGE2_L1    20100000 /* Egress Stage 2 Lookup 1 */

/* Known users of the VCAP API */
enum vcap_user {
	VCAP_USER_PTP,
	VCAP_USER_MRP,
	VCAP_USER_CFM,
	VCAP_USER_VLAN,
	VCAP_USER_QOS,
	VCAP_USER_VCAP_UTIL,
	VCAP_USER_TC,
	VCAP_USER_TC_EXTRA,

	/* add new users above here */

	/* used to define VCAP_USER_MAX below */
	__VCAP_USER_AFTER_LAST,
	VCAP_USER_MAX = __VCAP_USER_AFTER_LAST - 1,
};

/* VCAP information used for displaying data */
struct vcap_statistics {
	char *name;
	int count;
	const char * const *keyfield_set_names;
	const char * const *actionfield_set_names;
	const char * const *keyfield_names;
	const char * const *actionfield_names;
};

/* VCAP key/action field type, position and width */
struct vcap_field {
	u16 type;
	u16 width;
	u16 offset;
};

/* VCAP keyset or actionset type and width */
struct vcap_set {
	u8 type_id;
	u8 sw_per_item;
	u8 sw_cnt;
};

/* VCAP typegroup position and bitvalue */
struct vcap_typegroup {
	u16 offset;
	u16 width;
	u16 value;
};

/* VCAP model data */
struct vcap_info {
	char *name; /* user-friendly name */
	u16 rows; /* number of row in instance */
	u16 sw_count; /* maximum subwords used per rule */
	u16 sw_width; /* bits per subword in a keyset */
	u16 sticky_width; /* sticky bits per rule */
	u16 act_width;  /* bits per subword in an actionset */
	u16 default_cnt; /* number of default rules */
	u16 require_cnt_dis; /* not used */
	u16 version; /* vcap rtl version */
	const struct vcap_set *keyfield_set; /* keysets */
	int keyfield_set_size; /* number of keysets */
	const struct vcap_set *actionfield_set; /* actionsets */
	int actionfield_set_size; /* number of actionsets */
	/* map of keys per keyset */
	const struct vcap_field **keyfield_set_map;
	/* number of entries in the above map */
	int *keyfield_set_map_size;
	/* map of actions per actionset */
	const struct vcap_field **actionfield_set_map;
	/* number of entries in the above map */
	int *actionfield_set_map_size;
	/* map of keyset typegroups per subword size */
	const struct vcap_typegroup **keyfield_set_typegroups;
	/* map of actionset typegroups per subword size */
	const struct vcap_typegroup **actionfield_set_typegroups;
};

enum vcap_field_type {
	VCAP_FIELD_BIT,
	VCAP_FIELD_U32,
	VCAP_FIELD_U48,
	VCAP_FIELD_U56,
	VCAP_FIELD_U64,
	VCAP_FIELD_U72,
	VCAP_FIELD_U112,
	VCAP_FIELD_U128,
};

/* VCAP rule data towards the VCAP cache */
struct vcap_cache_data {
	u32 *keystream;
	u32 *maskstream;
	u32 *actionstream;
	u32 counter;
	bool sticky;
};

/* Selects which part of the rule must be updated */
enum vcap_selection {
	VCAP_SEL_ENTRY = 0x01,
	VCAP_SEL_ACTION = 0x02,
	VCAP_SEL_COUNTER = 0x04,
	VCAP_SEL_ALL = 0xff,
};

/* Commands towards the VCAP cache */
enum vcap_command {
	VCAP_CMD_WRITE = 0,
	VCAP_CMD_READ = 1,
	VCAP_CMD_MOVE_DOWN = 2,
	VCAP_CMD_MOVE_UP = 3,
	VCAP_CMD_INITIALIZE = 4,
};

enum vcap_rule_error {
	VCAP_ERR_NONE = 0,  /* No known error */
	VCAP_ERR_NO_ADMIN,  /* No admin instance */
	VCAP_ERR_NO_NETDEV,  /* No netdev instance */
	VCAP_ERR_NO_KEYSET_MATCH, /* No keyset matched the rule keys */
	VCAP_ERR_NO_ACTIONSET_MATCH, /* No actionset matched the rule actions */
	VCAP_ERR_NO_PORT_KEYSET_MATCH, /* No port keyset matched the rule keys */
};

/* Administration of each VCAP instance */
struct vcap_admin {
	struct list_head list; /* for insertion in vcap_control */
	struct list_head rules; /* list of rules */
	struct list_head enabled; /* list of enabled ports */
	struct mutex lock; /* control access to rules */
	enum vcap_type vtype;  /* type of vcap */
	int vinst; /* instance number within the same type */
	int first_cid; /* first chain id in this vcap */
	int last_cid; /* last chain id in this vcap */
	int tgt_inst; /* hardware instance number */
	int lookups; /* number of lookups in this vcap type */
	int lookups_per_instance; /* number of lookups in this instance */
	int last_valid_addr; /* top of address range to be used */
	int first_valid_addr; /* bottom of address range to be used */
	int last_used_addr;  /* address of lowest added rule */
	bool w32be; /* vcap uses "32bit-word big-endian" encoding */
	bool ingress; /* chain traffic direction */
	struct vcap_cache_data cache; /* encoded rule data */
};

/* Client supplied VCAP rule data */
struct vcap_rule {
	int vcap_chain_id; /* chain used for this rule */
	enum vcap_user user; /* rule owner */
	u16 priority;
	u32 id;  /* vcap rule id, must be unique, 0 will auto-generate a value */
	u64 cookie;  /* used by the client to identify the rule */
	struct list_head keyfields;  /* list of vcap_client_keyfield */
	struct list_head actionfields;  /* list of vcap_client_actionfield */
	enum vcap_keyfield_set keyset; /* keyset used: may be derived from fields */
	enum vcap_actionfield_set actionset; /* actionset used: may be derived from fields */
	enum vcap_rule_error exterr; /* extended error - used by TC */
	u64 client; /* space for client defined data */
};

/* List of keysets */
struct vcap_keyset_list {
	int max; /* size of the keyset list */
	int cnt; /* count of keysets actually in the list */
	enum vcap_keyfield_set *keysets; /* the list of keysets */
};

/* List of actionsets */
struct vcap_actionset_list {
	int max; /* size of the actionset list */
	int cnt; /* count of actionsets actually in the list */
	enum vcap_actionfield_set *actionsets; /* the list of actionsets */
};

/* Client output printf-like function with destination */
struct vcap_output_print {
	__printf(2, 3)
	void (*prf)(void *out, const char *fmt, ...);
	void *dst;
};

/* Client supplied VCAP callback operations */
struct vcap_operations {
	/* validate port keyset operation */
	enum vcap_keyfield_set (*validate_keyset)
		(struct net_device *ndev,
		 struct vcap_admin *admin,
		 struct vcap_rule *rule,
		 struct vcap_keyset_list *kslist,
		 u16 l3_proto);
	/* add default rule fields for the selected keyset operations */
	void (*add_default_fields)
		(struct net_device *ndev,
		 struct vcap_admin *admin,
		 struct vcap_rule *rule);
	/* cache operations */
	void (*cache_erase)
		(struct vcap_admin *admin);
	void (*cache_write)
		(struct net_device *ndev,
		 struct vcap_admin *admin,
		 enum vcap_selection sel,
		 u32 idx, u32 count);
	void (*cache_read)
		(struct net_device *ndev,
		 struct vcap_admin *admin,
		 enum vcap_selection sel,
		 u32 idx,
		 u32 count);
	/* block operations */
	void (*init)
		(struct net_device *ndev,
		 struct vcap_admin *admin,
		 u32 addr,
		 u32 count);
	void (*update)
		(struct net_device *ndev,
		 struct vcap_admin *admin,
		 enum vcap_command cmd,
		 enum vcap_selection sel,
		 u32 addr);
	void (*move)
		(struct net_device *ndev,
		 struct vcap_admin *admin,
		 u32 addr,
		 int offset,
		 int count);
	/* informational */
	int (*port_info)
		(struct net_device *ndev,
		 struct vcap_admin *admin,
		 struct vcap_output_print *out);
};

/* VCAP API Client control interface */
struct vcap_control {
	struct vcap_operations *ops;  /* client supplied operations */
	const struct vcap_info *vcaps; /* client supplied vcap models */
	const struct vcap_statistics *stats; /* client supplied vcap stats */
	struct list_head list; /* list of vcap instances */
};

#endif /* __VCAP_API__ */
