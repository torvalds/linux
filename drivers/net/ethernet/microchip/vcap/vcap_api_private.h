/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2022 Microchip Technology Inc. and its subsidiaries.
 * Microchip VCAP API
 */

#ifndef __VCAP_API_PRIVATE__
#define __VCAP_API_PRIVATE__

#include <linux/types.h>

#include "vcap_api.h"
#include "vcap_api_client.h"

#define to_intrule(rule) container_of((rule), struct vcap_rule_internal, data)

enum vcap_rule_state {
	VCAP_RS_PERMANENT, /* the rule is always stored in HW */
	VCAP_RS_ENABLED, /* enabled in HW but can be disabled */
	VCAP_RS_DISABLED, /* disabled (stored in SW) and can be enabled */
};

/* Private VCAP API rule data */
struct vcap_rule_internal {
	struct vcap_rule data; /* provided by the client */
	struct list_head list; /* the vcap admin list of rules */
	struct vcap_admin *admin; /* vcap hw instance */
	struct net_device *ndev;  /* the interface that the rule applies to */
	struct vcap_control *vctrl; /* the client control */
	u32 sort_key;  /* defines the position in the VCAP */
	int keyset_sw;  /* subwords in a keyset */
	int actionset_sw;  /* subwords in an actionset */
	int keyset_sw_regs;  /* registers in a subword in an keyset */
	int actionset_sw_regs;  /* registers in a subword in an actionset */
	int size; /* the size of the rule: max(entry, action) */
	u32 addr; /* address in the VCAP at insertion */
	u32 counter_id; /* counter id (if a dedicated counter is available) */
	struct vcap_counter counter; /* last read counter value */
	enum vcap_rule_state state;  /* rule storage state */
};

/* Bit iterator for the VCAP cache streams */
struct vcap_stream_iter {
	u32 offset; /* bit offset from the stream start */
	u32 sw_width; /* subword width in bits */
	u32 regs_per_sw; /* registers per subword */
	u32 reg_idx; /* current register index */
	u32 reg_bitpos; /* bit offset in current register */
	const struct vcap_typegroup *tg; /* current typegroup */
};

/* Check that the control has a valid set of callbacks */
int vcap_api_check(struct vcap_control *ctrl);
/* Erase the VCAP cache area used or encoding and decoding */
void vcap_erase_cache(struct vcap_rule_internal *ri);

/* Iterator functionality */

void vcap_iter_init(struct vcap_stream_iter *itr, int sw_width,
		    const struct vcap_typegroup *tg, u32 offset);
void vcap_iter_next(struct vcap_stream_iter *itr);
void vcap_iter_set(struct vcap_stream_iter *itr, int sw_width,
		   const struct vcap_typegroup *tg, u32 offset);
void vcap_iter_update(struct vcap_stream_iter *itr);

/* Keyset and keyfield functionality */

/* Return the number of keyfields in the keyset */
int vcap_keyfield_count(struct vcap_control *vctrl,
			enum vcap_type vt, enum vcap_keyfield_set keyset);
/* Return the typegroup table for the matching keyset (using subword size) */
const struct vcap_typegroup *
vcap_keyfield_typegroup(struct vcap_control *vctrl,
			enum vcap_type vt, enum vcap_keyfield_set keyset);
/* Return the list of keyfields for the keyset */
const struct vcap_field *vcap_keyfields(struct vcap_control *vctrl,
					enum vcap_type vt,
					enum vcap_keyfield_set keyset);

/* Actionset and actionfield functionality */

/* Return the actionset information for the actionset */
const struct vcap_set *
vcap_actionfieldset(struct vcap_control *vctrl,
		    enum vcap_type vt, enum vcap_actionfield_set actionset);
/* Return the number of actionfields in the actionset */
int vcap_actionfield_count(struct vcap_control *vctrl,
			   enum vcap_type vt,
			   enum vcap_actionfield_set actionset);
/* Return the typegroup table for the matching actionset (using subword size) */
const struct vcap_typegroup *
vcap_actionfield_typegroup(struct vcap_control *vctrl, enum vcap_type vt,
			   enum vcap_actionfield_set actionset);
/* Return the list of actionfields for the actionset */
const struct vcap_field *
vcap_actionfields(struct vcap_control *vctrl,
		  enum vcap_type vt, enum vcap_actionfield_set actionset);
/* Map actionset id to a string with the actionset name */
const char *vcap_actionset_name(struct vcap_control *vctrl,
				enum vcap_actionfield_set actionset);
/* Map key field id to a string with the key name */
const char *vcap_actionfield_name(struct vcap_control *vctrl,
				  enum vcap_action_field action);

/* Read key data from a VCAP address and discover if there are any rule keysets
 * here
 */
int vcap_addr_keysets(struct vcap_control *vctrl, struct net_device *ndev,
		      struct vcap_admin *admin, int addr,
		      struct vcap_keyset_list *kslist);

/* Verify that the typegroup information, subword count, keyset and type id
 * are in sync and correct, return the list of matchin keysets
 */
int vcap_find_keystream_keysets(struct vcap_control *vctrl, enum vcap_type vt,
				u32 *keystream, u32 *mskstream, bool mask,
				int sw_max, struct vcap_keyset_list *kslist);

/* Get the keysets that matches the rule key type/mask */
int vcap_rule_get_keysets(struct vcap_rule_internal *ri,
			  struct vcap_keyset_list *matches);
/* Decode a rule from the VCAP cache and return a copy */
struct vcap_rule *vcap_decode_rule(struct vcap_rule_internal *elem);

#endif /* __VCAP_API_PRIVATE__ */
