// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Intel Corporation. */

#include "ice_common.h"
#include "ice_flex_pipe.h"
#include "ice_flow.h"

/* To support tunneling entries by PF, the package will append the PF number to
 * the label; for example TNL_VXLAN_PF0, TNL_VXLAN_PF1, TNL_VXLAN_PF2, etc.
 */
static const struct ice_tunnel_type_scan tnls[] = {
	{ TNL_VXLAN,		"TNL_VXLAN_PF" },
	{ TNL_GENEVE,		"TNL_GENEVE_PF" },
	{ TNL_LAST,		"" }
};

static const u32 ice_sect_lkup[ICE_BLK_COUNT][ICE_SECT_COUNT] = {
	/* SWITCH */
	{
		ICE_SID_XLT0_SW,
		ICE_SID_XLT_KEY_BUILDER_SW,
		ICE_SID_XLT1_SW,
		ICE_SID_XLT2_SW,
		ICE_SID_PROFID_TCAM_SW,
		ICE_SID_PROFID_REDIR_SW,
		ICE_SID_FLD_VEC_SW,
		ICE_SID_CDID_KEY_BUILDER_SW,
		ICE_SID_CDID_REDIR_SW
	},

	/* ACL */
	{
		ICE_SID_XLT0_ACL,
		ICE_SID_XLT_KEY_BUILDER_ACL,
		ICE_SID_XLT1_ACL,
		ICE_SID_XLT2_ACL,
		ICE_SID_PROFID_TCAM_ACL,
		ICE_SID_PROFID_REDIR_ACL,
		ICE_SID_FLD_VEC_ACL,
		ICE_SID_CDID_KEY_BUILDER_ACL,
		ICE_SID_CDID_REDIR_ACL
	},

	/* FD */
	{
		ICE_SID_XLT0_FD,
		ICE_SID_XLT_KEY_BUILDER_FD,
		ICE_SID_XLT1_FD,
		ICE_SID_XLT2_FD,
		ICE_SID_PROFID_TCAM_FD,
		ICE_SID_PROFID_REDIR_FD,
		ICE_SID_FLD_VEC_FD,
		ICE_SID_CDID_KEY_BUILDER_FD,
		ICE_SID_CDID_REDIR_FD
	},

	/* RSS */
	{
		ICE_SID_XLT0_RSS,
		ICE_SID_XLT_KEY_BUILDER_RSS,
		ICE_SID_XLT1_RSS,
		ICE_SID_XLT2_RSS,
		ICE_SID_PROFID_TCAM_RSS,
		ICE_SID_PROFID_REDIR_RSS,
		ICE_SID_FLD_VEC_RSS,
		ICE_SID_CDID_KEY_BUILDER_RSS,
		ICE_SID_CDID_REDIR_RSS
	},

	/* PE */
	{
		ICE_SID_XLT0_PE,
		ICE_SID_XLT_KEY_BUILDER_PE,
		ICE_SID_XLT1_PE,
		ICE_SID_XLT2_PE,
		ICE_SID_PROFID_TCAM_PE,
		ICE_SID_PROFID_REDIR_PE,
		ICE_SID_FLD_VEC_PE,
		ICE_SID_CDID_KEY_BUILDER_PE,
		ICE_SID_CDID_REDIR_PE
	}
};

/**
 * ice_sect_id - returns section ID
 * @blk: block type
 * @sect: section type
 *
 * This helper function returns the proper section ID given a block type and a
 * section type.
 */
static u32 ice_sect_id(enum ice_block blk, enum ice_sect sect)
{
	return ice_sect_lkup[blk][sect];
}

/**
 * ice_pkg_val_buf
 * @buf: pointer to the ice buffer
 *
 * This helper function validates a buffer's header.
 */
static struct ice_buf_hdr *ice_pkg_val_buf(struct ice_buf *buf)
{
	struct ice_buf_hdr *hdr;
	u16 section_count;
	u16 data_end;

	hdr = (struct ice_buf_hdr *)buf->buf;
	/* verify data */
	section_count = le16_to_cpu(hdr->section_count);
	if (section_count < ICE_MIN_S_COUNT || section_count > ICE_MAX_S_COUNT)
		return NULL;

	data_end = le16_to_cpu(hdr->data_end);
	if (data_end < ICE_MIN_S_DATA_END || data_end > ICE_MAX_S_DATA_END)
		return NULL;

	return hdr;
}

/**
 * ice_find_buf_table
 * @ice_seg: pointer to the ice segment
 *
 * Returns the address of the buffer table within the ice segment.
 */
static struct ice_buf_table *ice_find_buf_table(struct ice_seg *ice_seg)
{
	struct ice_nvm_table *nvms;

	nvms = (struct ice_nvm_table *)
		(ice_seg->device_table +
		 le32_to_cpu(ice_seg->device_table_count));

	return (__force struct ice_buf_table *)
		(nvms->vers + le32_to_cpu(nvms->table_count));
}

/**
 * ice_pkg_enum_buf
 * @ice_seg: pointer to the ice segment (or NULL on subsequent calls)
 * @state: pointer to the enum state
 *
 * This function will enumerate all the buffers in the ice segment. The first
 * call is made with the ice_seg parameter non-NULL; on subsequent calls,
 * ice_seg is set to NULL which continues the enumeration. When the function
 * returns a NULL pointer, then the end of the buffers has been reached, or an
 * unexpected value has been detected (for example an invalid section count or
 * an invalid buffer end value).
 */
static struct ice_buf_hdr *
ice_pkg_enum_buf(struct ice_seg *ice_seg, struct ice_pkg_enum *state)
{
	if (ice_seg) {
		state->buf_table = ice_find_buf_table(ice_seg);
		if (!state->buf_table)
			return NULL;

		state->buf_idx = 0;
		return ice_pkg_val_buf(state->buf_table->buf_array);
	}

	if (++state->buf_idx < le32_to_cpu(state->buf_table->buf_count))
		return ice_pkg_val_buf(state->buf_table->buf_array +
				       state->buf_idx);
	else
		return NULL;
}

/**
 * ice_pkg_advance_sect
 * @ice_seg: pointer to the ice segment (or NULL on subsequent calls)
 * @state: pointer to the enum state
 *
 * This helper function will advance the section within the ice segment,
 * also advancing the buffer if needed.
 */
static bool
ice_pkg_advance_sect(struct ice_seg *ice_seg, struct ice_pkg_enum *state)
{
	if (!ice_seg && !state->buf)
		return false;

	if (!ice_seg && state->buf)
		if (++state->sect_idx < le16_to_cpu(state->buf->section_count))
			return true;

	state->buf = ice_pkg_enum_buf(ice_seg, state);
	if (!state->buf)
		return false;

	/* start of new buffer, reset section index */
	state->sect_idx = 0;
	return true;
}

/**
 * ice_pkg_enum_section
 * @ice_seg: pointer to the ice segment (or NULL on subsequent calls)
 * @state: pointer to the enum state
 * @sect_type: section type to enumerate
 *
 * This function will enumerate all the sections of a particular type in the
 * ice segment. The first call is made with the ice_seg parameter non-NULL;
 * on subsequent calls, ice_seg is set to NULL which continues the enumeration.
 * When the function returns a NULL pointer, then the end of the matching
 * sections has been reached.
 */
static void *
ice_pkg_enum_section(struct ice_seg *ice_seg, struct ice_pkg_enum *state,
		     u32 sect_type)
{
	u16 offset, size;

	if (ice_seg)
		state->type = sect_type;

	if (!ice_pkg_advance_sect(ice_seg, state))
		return NULL;

	/* scan for next matching section */
	while (state->buf->section_entry[state->sect_idx].type !=
	       cpu_to_le32(state->type))
		if (!ice_pkg_advance_sect(NULL, state))
			return NULL;

	/* validate section */
	offset = le16_to_cpu(state->buf->section_entry[state->sect_idx].offset);
	if (offset < ICE_MIN_S_OFF || offset > ICE_MAX_S_OFF)
		return NULL;

	size = le16_to_cpu(state->buf->section_entry[state->sect_idx].size);
	if (size < ICE_MIN_S_SZ || size > ICE_MAX_S_SZ)
		return NULL;

	/* make sure the section fits in the buffer */
	if (offset + size > ICE_PKG_BUF_SIZE)
		return NULL;

	state->sect_type =
		le32_to_cpu(state->buf->section_entry[state->sect_idx].type);

	/* calc pointer to this section */
	state->sect = ((u8 *)state->buf) +
		le16_to_cpu(state->buf->section_entry[state->sect_idx].offset);

	return state->sect;
}

/**
 * ice_pkg_enum_entry
 * @ice_seg: pointer to the ice segment (or NULL on subsequent calls)
 * @state: pointer to the enum state
 * @sect_type: section type to enumerate
 * @offset: pointer to variable that receives the offset in the table (optional)
 * @handler: function that handles access to the entries into the section type
 *
 * This function will enumerate all the entries in particular section type in
 * the ice segment. The first call is made with the ice_seg parameter non-NULL;
 * on subsequent calls, ice_seg is set to NULL which continues the enumeration.
 * When the function returns a NULL pointer, then the end of the entries has
 * been reached.
 *
 * Since each section may have a different header and entry size, the handler
 * function is needed to determine the number and location entries in each
 * section.
 *
 * The offset parameter is optional, but should be used for sections that
 * contain an offset for each section table. For such cases, the section handler
 * function must return the appropriate offset + index to give the absolution
 * offset for each entry. For example, if the base for a section's header
 * indicates a base offset of 10, and the index for the entry is 2, then
 * section handler function should set the offset to 10 + 2 = 12.
 */
static void *
ice_pkg_enum_entry(struct ice_seg *ice_seg, struct ice_pkg_enum *state,
		   u32 sect_type, u32 *offset,
		   void *(*handler)(u32 sect_type, void *section,
				    u32 index, u32 *offset))
{
	void *entry;

	if (ice_seg) {
		if (!handler)
			return NULL;

		if (!ice_pkg_enum_section(ice_seg, state, sect_type))
			return NULL;

		state->entry_idx = 0;
		state->handler = handler;
	} else {
		state->entry_idx++;
	}

	if (!state->handler)
		return NULL;

	/* get entry */
	entry = state->handler(state->sect_type, state->sect, state->entry_idx,
			       offset);
	if (!entry) {
		/* end of a section, look for another section of this type */
		if (!ice_pkg_enum_section(NULL, state, 0))
			return NULL;

		state->entry_idx = 0;
		entry = state->handler(state->sect_type, state->sect,
				       state->entry_idx, offset);
	}

	return entry;
}

/**
 * ice_boost_tcam_handler
 * @sect_type: section type
 * @section: pointer to section
 * @index: index of the boost TCAM entry to be returned
 * @offset: pointer to receive absolute offset, always 0 for boost TCAM sections
 *
 * This is a callback function that can be passed to ice_pkg_enum_entry.
 * Handles enumeration of individual boost TCAM entries.
 */
static void *
ice_boost_tcam_handler(u32 sect_type, void *section, u32 index, u32 *offset)
{
	struct ice_boost_tcam_section *boost;

	if (!section)
		return NULL;

	if (sect_type != ICE_SID_RXPARSER_BOOST_TCAM)
		return NULL;

	if (index > ICE_MAX_BST_TCAMS_IN_BUF)
		return NULL;

	if (offset)
		*offset = 0;

	boost = section;
	if (index >= le16_to_cpu(boost->count))
		return NULL;

	return boost->tcam + index;
}

/**
 * ice_find_boost_entry
 * @ice_seg: pointer to the ice segment (non-NULL)
 * @addr: Boost TCAM address of entry to search for
 * @entry: returns pointer to the entry
 *
 * Finds a particular Boost TCAM entry and returns a pointer to that entry
 * if it is found. The ice_seg parameter must not be NULL since the first call
 * to ice_pkg_enum_entry requires a pointer to an actual ice_segment structure.
 */
static enum ice_status
ice_find_boost_entry(struct ice_seg *ice_seg, u16 addr,
		     struct ice_boost_tcam_entry **entry)
{
	struct ice_boost_tcam_entry *tcam;
	struct ice_pkg_enum state;

	memset(&state, 0, sizeof(state));

	if (!ice_seg)
		return ICE_ERR_PARAM;

	do {
		tcam = ice_pkg_enum_entry(ice_seg, &state,
					  ICE_SID_RXPARSER_BOOST_TCAM, NULL,
					  ice_boost_tcam_handler);
		if (tcam && le16_to_cpu(tcam->addr) == addr) {
			*entry = tcam;
			return 0;
		}

		ice_seg = NULL;
	} while (tcam);

	*entry = NULL;
	return ICE_ERR_CFG;
}

/**
 * ice_label_enum_handler
 * @sect_type: section type
 * @section: pointer to section
 * @index: index of the label entry to be returned
 * @offset: pointer to receive absolute offset, always zero for label sections
 *
 * This is a callback function that can be passed to ice_pkg_enum_entry.
 * Handles enumeration of individual label entries.
 */
static void *
ice_label_enum_handler(u32 __always_unused sect_type, void *section, u32 index,
		       u32 *offset)
{
	struct ice_label_section *labels;

	if (!section)
		return NULL;

	if (index > ICE_MAX_LABELS_IN_BUF)
		return NULL;

	if (offset)
		*offset = 0;

	labels = section;
	if (index >= le16_to_cpu(labels->count))
		return NULL;

	return labels->label + index;
}

/**
 * ice_enum_labels
 * @ice_seg: pointer to the ice segment (NULL on subsequent calls)
 * @type: the section type that will contain the label (0 on subsequent calls)
 * @state: ice_pkg_enum structure that will hold the state of the enumeration
 * @value: pointer to a value that will return the label's value if found
 *
 * Enumerates a list of labels in the package. The caller will call
 * ice_enum_labels(ice_seg, type, ...) to start the enumeration, then call
 * ice_enum_labels(NULL, 0, ...) to continue. When the function returns a NULL
 * the end of the list has been reached.
 */
static char *
ice_enum_labels(struct ice_seg *ice_seg, u32 type, struct ice_pkg_enum *state,
		u16 *value)
{
	struct ice_label *label;

	/* Check for valid label section on first call */
	if (type && !(type >= ICE_SID_LBL_FIRST && type <= ICE_SID_LBL_LAST))
		return NULL;

	label = ice_pkg_enum_entry(ice_seg, state, type, NULL,
				   ice_label_enum_handler);
	if (!label)
		return NULL;

	*value = le16_to_cpu(label->value);
	return label->name;
}

/**
 * ice_init_pkg_hints
 * @hw: pointer to the HW structure
 * @ice_seg: pointer to the segment of the package scan (non-NULL)
 *
 * This function will scan the package and save off relevant information
 * (hints or metadata) for driver use. The ice_seg parameter must not be NULL
 * since the first call to ice_enum_labels requires a pointer to an actual
 * ice_seg structure.
 */
static void ice_init_pkg_hints(struct ice_hw *hw, struct ice_seg *ice_seg)
{
	struct ice_pkg_enum state;
	char *label_name;
	u16 val;
	int i;

	memset(&hw->tnl, 0, sizeof(hw->tnl));
	memset(&state, 0, sizeof(state));

	if (!ice_seg)
		return;

	label_name = ice_enum_labels(ice_seg, ICE_SID_LBL_RXPARSER_TMEM, &state,
				     &val);

	while (label_name && hw->tnl.count < ICE_TUNNEL_MAX_ENTRIES) {
		for (i = 0; tnls[i].type != TNL_LAST; i++) {
			size_t len = strlen(tnls[i].label_prefix);

			/* Look for matching label start, before continuing */
			if (strncmp(label_name, tnls[i].label_prefix, len))
				continue;

			/* Make sure this label matches our PF. Note that the PF
			 * character ('0' - '7') will be located where our
			 * prefix string's null terminator is located.
			 */
			if ((label_name[len] - '0') == hw->pf_id) {
				hw->tnl.tbl[hw->tnl.count].type = tnls[i].type;
				hw->tnl.tbl[hw->tnl.count].valid = false;
				hw->tnl.tbl[hw->tnl.count].in_use = false;
				hw->tnl.tbl[hw->tnl.count].marked = false;
				hw->tnl.tbl[hw->tnl.count].boost_addr = val;
				hw->tnl.tbl[hw->tnl.count].port = 0;
				hw->tnl.count++;
				break;
			}
		}

		label_name = ice_enum_labels(NULL, 0, &state, &val);
	}

	/* Cache the appropriate boost TCAM entry pointers */
	for (i = 0; i < hw->tnl.count; i++) {
		ice_find_boost_entry(ice_seg, hw->tnl.tbl[i].boost_addr,
				     &hw->tnl.tbl[i].boost_entry);
		if (hw->tnl.tbl[i].boost_entry)
			hw->tnl.tbl[i].valid = true;
	}
}

/* Key creation */

#define ICE_DC_KEY	0x1	/* don't care */
#define ICE_DC_KEYINV	0x1
#define ICE_NM_KEY	0x0	/* never match */
#define ICE_NM_KEYINV	0x0
#define ICE_0_KEY	0x1	/* match 0 */
#define ICE_0_KEYINV	0x0
#define ICE_1_KEY	0x0	/* match 1 */
#define ICE_1_KEYINV	0x1

/**
 * ice_gen_key_word - generate 16-bits of a key/mask word
 * @val: the value
 * @valid: valid bits mask (change only the valid bits)
 * @dont_care: don't care mask
 * @nvr_mtch: never match mask
 * @key: pointer to an array of where the resulting key portion
 * @key_inv: pointer to an array of where the resulting key invert portion
 *
 * This function generates 16-bits from a 8-bit value, an 8-bit don't care mask
 * and an 8-bit never match mask. The 16-bits of output are divided into 8 bits
 * of key and 8 bits of key invert.
 *
 *     '0' =    b01, always match a 0 bit
 *     '1' =    b10, always match a 1 bit
 *     '?' =    b11, don't care bit (always matches)
 *     '~' =    b00, never match bit
 *
 * Input:
 *          val:         b0  1  0  1  0  1
 *          dont_care:   b0  0  1  1  0  0
 *          never_mtch:  b0  0  0  0  1  1
 *          ------------------------------
 * Result:  key:        b01 10 11 11 00 00
 */
static enum ice_status
ice_gen_key_word(u8 val, u8 valid, u8 dont_care, u8 nvr_mtch, u8 *key,
		 u8 *key_inv)
{
	u8 in_key = *key, in_key_inv = *key_inv;
	u8 i;

	/* 'dont_care' and 'nvr_mtch' masks cannot overlap */
	if ((dont_care ^ nvr_mtch) != (dont_care | nvr_mtch))
		return ICE_ERR_CFG;

	*key = 0;
	*key_inv = 0;

	/* encode the 8 bits into 8-bit key and 8-bit key invert */
	for (i = 0; i < 8; i++) {
		*key >>= 1;
		*key_inv >>= 1;

		if (!(valid & 0x1)) { /* change only valid bits */
			*key |= (in_key & 0x1) << 7;
			*key_inv |= (in_key_inv & 0x1) << 7;
		} else if (dont_care & 0x1) { /* don't care bit */
			*key |= ICE_DC_KEY << 7;
			*key_inv |= ICE_DC_KEYINV << 7;
		} else if (nvr_mtch & 0x1) { /* never match bit */
			*key |= ICE_NM_KEY << 7;
			*key_inv |= ICE_NM_KEYINV << 7;
		} else if (val & 0x01) { /* exact 1 match */
			*key |= ICE_1_KEY << 7;
			*key_inv |= ICE_1_KEYINV << 7;
		} else { /* exact 0 match */
			*key |= ICE_0_KEY << 7;
			*key_inv |= ICE_0_KEYINV << 7;
		}

		dont_care >>= 1;
		nvr_mtch >>= 1;
		valid >>= 1;
		val >>= 1;
		in_key >>= 1;
		in_key_inv >>= 1;
	}

	return 0;
}

/**
 * ice_bits_max_set - determine if the number of bits set is within a maximum
 * @mask: pointer to the byte array which is the mask
 * @size: the number of bytes in the mask
 * @max: the max number of set bits
 *
 * This function determines if there are at most 'max' number of bits set in an
 * array. Returns true if the number for bits set is <= max or will return false
 * otherwise.
 */
static bool ice_bits_max_set(const u8 *mask, u16 size, u16 max)
{
	u16 count = 0;
	u16 i;

	/* check each byte */
	for (i = 0; i < size; i++) {
		/* if 0, go to next byte */
		if (!mask[i])
			continue;

		/* We know there is at least one set bit in this byte because of
		 * the above check; if we already have found 'max' number of
		 * bits set, then we can return failure now.
		 */
		if (count == max)
			return false;

		/* count the bits in this byte, checking threshold */
		count += hweight8(mask[i]);
		if (count > max)
			return false;
	}

	return true;
}

/**
 * ice_set_key - generate a variable sized key with multiples of 16-bits
 * @key: pointer to where the key will be stored
 * @size: the size of the complete key in bytes (must be even)
 * @val: array of 8-bit values that makes up the value portion of the key
 * @upd: array of 8-bit masks that determine what key portion to update
 * @dc: array of 8-bit masks that make up the don't care mask
 * @nm: array of 8-bit masks that make up the never match mask
 * @off: the offset of the first byte in the key to update
 * @len: the number of bytes in the key update
 *
 * This function generates a key from a value, a don't care mask and a never
 * match mask.
 * upd, dc, and nm are optional parameters, and can be NULL:
 *	upd == NULL --> udp mask is all 1's (update all bits)
 *	dc == NULL --> dc mask is all 0's (no don't care bits)
 *	nm == NULL --> nm mask is all 0's (no never match bits)
 */
static enum ice_status
ice_set_key(u8 *key, u16 size, u8 *val, u8 *upd, u8 *dc, u8 *nm, u16 off,
	    u16 len)
{
	u16 half_size;
	u16 i;

	/* size must be a multiple of 2 bytes. */
	if (size % 2)
		return ICE_ERR_CFG;

	half_size = size / 2;
	if (off + len > half_size)
		return ICE_ERR_CFG;

	/* Make sure at most one bit is set in the never match mask. Having more
	 * than one never match mask bit set will cause HW to consume excessive
	 * power otherwise; this is a power management efficiency check.
	 */
#define ICE_NVR_MTCH_BITS_MAX	1
	if (nm && !ice_bits_max_set(nm, len, ICE_NVR_MTCH_BITS_MAX))
		return ICE_ERR_CFG;

	for (i = 0; i < len; i++)
		if (ice_gen_key_word(val[i], upd ? upd[i] : 0xff,
				     dc ? dc[i] : 0, nm ? nm[i] : 0,
				     key + off + i, key + half_size + off + i))
			return ICE_ERR_CFG;

	return 0;
}

/**
 * ice_acquire_global_cfg_lock
 * @hw: pointer to the HW structure
 * @access: access type (read or write)
 *
 * This function will request ownership of the global config lock for reading
 * or writing of the package. When attempting to obtain write access, the
 * caller must check for the following two return values:
 *
 * ICE_SUCCESS        - Means the caller has acquired the global config lock
 *                      and can perform writing of the package.
 * ICE_ERR_AQ_NO_WORK - Indicates another driver has already written the
 *                      package or has found that no update was necessary; in
 *                      this case, the caller can just skip performing any
 *                      update of the package.
 */
static enum ice_status
ice_acquire_global_cfg_lock(struct ice_hw *hw,
			    enum ice_aq_res_access_type access)
{
	enum ice_status status;

	status = ice_acquire_res(hw, ICE_GLOBAL_CFG_LOCK_RES_ID, access,
				 ICE_GLOBAL_CFG_LOCK_TIMEOUT);

	if (!status)
		mutex_lock(&ice_global_cfg_lock_sw);
	else if (status == ICE_ERR_AQ_NO_WORK)
		ice_debug(hw, ICE_DBG_PKG,
			  "Global config lock: No work to do\n");

	return status;
}

/**
 * ice_release_global_cfg_lock
 * @hw: pointer to the HW structure
 *
 * This function will release the global config lock.
 */
static void ice_release_global_cfg_lock(struct ice_hw *hw)
{
	mutex_unlock(&ice_global_cfg_lock_sw);
	ice_release_res(hw, ICE_GLOBAL_CFG_LOCK_RES_ID);
}

/**
 * ice_acquire_change_lock
 * @hw: pointer to the HW structure
 * @access: access type (read or write)
 *
 * This function will request ownership of the change lock.
 */
static enum ice_status
ice_acquire_change_lock(struct ice_hw *hw, enum ice_aq_res_access_type access)
{
	return ice_acquire_res(hw, ICE_CHANGE_LOCK_RES_ID, access,
			       ICE_CHANGE_LOCK_TIMEOUT);
}

/**
 * ice_release_change_lock
 * @hw: pointer to the HW structure
 *
 * This function will release the change lock using the proper Admin Command.
 */
static void ice_release_change_lock(struct ice_hw *hw)
{
	ice_release_res(hw, ICE_CHANGE_LOCK_RES_ID);
}

/**
 * ice_aq_download_pkg
 * @hw: pointer to the hardware structure
 * @pkg_buf: the package buffer to transfer
 * @buf_size: the size of the package buffer
 * @last_buf: last buffer indicator
 * @error_offset: returns error offset
 * @error_info: returns error information
 * @cd: pointer to command details structure or NULL
 *
 * Download Package (0x0C40)
 */
static enum ice_status
ice_aq_download_pkg(struct ice_hw *hw, struct ice_buf_hdr *pkg_buf,
		    u16 buf_size, bool last_buf, u32 *error_offset,
		    u32 *error_info, struct ice_sq_cd *cd)
{
	struct ice_aqc_download_pkg *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	if (error_offset)
		*error_offset = 0;
	if (error_info)
		*error_info = 0;

	cmd = &desc.params.download_pkg;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_download_pkg);
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	if (last_buf)
		cmd->flags |= ICE_AQC_DOWNLOAD_PKG_LAST_BUF;

	status = ice_aq_send_cmd(hw, &desc, pkg_buf, buf_size, cd);
	if (status == ICE_ERR_AQ_ERROR) {
		/* Read error from buffer only when the FW returned an error */
		struct ice_aqc_download_pkg_resp *resp;

		resp = (struct ice_aqc_download_pkg_resp *)pkg_buf;
		if (error_offset)
			*error_offset = le32_to_cpu(resp->error_offset);
		if (error_info)
			*error_info = le32_to_cpu(resp->error_info);
	}

	return status;
}

/**
 * ice_aq_update_pkg
 * @hw: pointer to the hardware structure
 * @pkg_buf: the package cmd buffer
 * @buf_size: the size of the package cmd buffer
 * @last_buf: last buffer indicator
 * @error_offset: returns error offset
 * @error_info: returns error information
 * @cd: pointer to command details structure or NULL
 *
 * Update Package (0x0C42)
 */
static enum ice_status
ice_aq_update_pkg(struct ice_hw *hw, struct ice_buf_hdr *pkg_buf, u16 buf_size,
		  bool last_buf, u32 *error_offset, u32 *error_info,
		  struct ice_sq_cd *cd)
{
	struct ice_aqc_download_pkg *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	if (error_offset)
		*error_offset = 0;
	if (error_info)
		*error_info = 0;

	cmd = &desc.params.download_pkg;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_update_pkg);
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	if (last_buf)
		cmd->flags |= ICE_AQC_DOWNLOAD_PKG_LAST_BUF;

	status = ice_aq_send_cmd(hw, &desc, pkg_buf, buf_size, cd);
	if (status == ICE_ERR_AQ_ERROR) {
		/* Read error from buffer only when the FW returned an error */
		struct ice_aqc_download_pkg_resp *resp;

		resp = (struct ice_aqc_download_pkg_resp *)pkg_buf;
		if (error_offset)
			*error_offset = le32_to_cpu(resp->error_offset);
		if (error_info)
			*error_info = le32_to_cpu(resp->error_info);
	}

	return status;
}

/**
 * ice_find_seg_in_pkg
 * @hw: pointer to the hardware structure
 * @seg_type: the segment type to search for (i.e., SEGMENT_TYPE_CPK)
 * @pkg_hdr: pointer to the package header to be searched
 *
 * This function searches a package file for a particular segment type. On
 * success it returns a pointer to the segment header, otherwise it will
 * return NULL.
 */
static struct ice_generic_seg_hdr *
ice_find_seg_in_pkg(struct ice_hw *hw, u32 seg_type,
		    struct ice_pkg_hdr *pkg_hdr)
{
	u32 i;

	ice_debug(hw, ICE_DBG_PKG, "Package format version: %d.%d.%d.%d\n",
		  pkg_hdr->pkg_format_ver.major, pkg_hdr->pkg_format_ver.minor,
		  pkg_hdr->pkg_format_ver.update,
		  pkg_hdr->pkg_format_ver.draft);

	/* Search all package segments for the requested segment type */
	for (i = 0; i < le32_to_cpu(pkg_hdr->seg_count); i++) {
		struct ice_generic_seg_hdr *seg;

		seg = (struct ice_generic_seg_hdr *)
			((u8 *)pkg_hdr + le32_to_cpu(pkg_hdr->seg_offset[i]));

		if (le32_to_cpu(seg->seg_type) == seg_type)
			return seg;
	}

	return NULL;
}

/**
 * ice_update_pkg
 * @hw: pointer to the hardware structure
 * @bufs: pointer to an array of buffers
 * @count: the number of buffers in the array
 *
 * Obtains change lock and updates package.
 */
static enum ice_status
ice_update_pkg(struct ice_hw *hw, struct ice_buf *bufs, u32 count)
{
	enum ice_status status;
	u32 offset, info, i;

	status = ice_acquire_change_lock(hw, ICE_RES_WRITE);
	if (status)
		return status;

	for (i = 0; i < count; i++) {
		struct ice_buf_hdr *bh = (struct ice_buf_hdr *)(bufs + i);
		bool last = ((i + 1) == count);

		status = ice_aq_update_pkg(hw, bh, le16_to_cpu(bh->data_end),
					   last, &offset, &info, NULL);

		if (status) {
			ice_debug(hw, ICE_DBG_PKG,
				  "Update pkg failed: err %d off %d inf %d\n",
				  status, offset, info);
			break;
		}
	}

	ice_release_change_lock(hw);

	return status;
}

/**
 * ice_dwnld_cfg_bufs
 * @hw: pointer to the hardware structure
 * @bufs: pointer to an array of buffers
 * @count: the number of buffers in the array
 *
 * Obtains global config lock and downloads the package configuration buffers
 * to the firmware. Metadata buffers are skipped, and the first metadata buffer
 * found indicates that the rest of the buffers are all metadata buffers.
 */
static enum ice_status
ice_dwnld_cfg_bufs(struct ice_hw *hw, struct ice_buf *bufs, u32 count)
{
	enum ice_status status;
	struct ice_buf_hdr *bh;
	u32 offset, info, i;

	if (!bufs || !count)
		return ICE_ERR_PARAM;

	/* If the first buffer's first section has its metadata bit set
	 * then there are no buffers to be downloaded, and the operation is
	 * considered a success.
	 */
	bh = (struct ice_buf_hdr *)bufs;
	if (le32_to_cpu(bh->section_entry[0].type) & ICE_METADATA_BUF)
		return 0;

	/* reset pkg_dwnld_status in case this function is called in the
	 * reset/rebuild flow
	 */
	hw->pkg_dwnld_status = ICE_AQ_RC_OK;

	status = ice_acquire_global_cfg_lock(hw, ICE_RES_WRITE);
	if (status) {
		if (status == ICE_ERR_AQ_NO_WORK)
			hw->pkg_dwnld_status = ICE_AQ_RC_EEXIST;
		else
			hw->pkg_dwnld_status = hw->adminq.sq_last_status;
		return status;
	}

	for (i = 0; i < count; i++) {
		bool last = ((i + 1) == count);

		if (!last) {
			/* check next buffer for metadata flag */
			bh = (struct ice_buf_hdr *)(bufs + i + 1);

			/* A set metadata flag in the next buffer will signal
			 * that the current buffer will be the last buffer
			 * downloaded
			 */
			if (le16_to_cpu(bh->section_count))
				if (le32_to_cpu(bh->section_entry[0].type) &
				    ICE_METADATA_BUF)
					last = true;
		}

		bh = (struct ice_buf_hdr *)(bufs + i);

		status = ice_aq_download_pkg(hw, bh, ICE_PKG_BUF_SIZE, last,
					     &offset, &info, NULL);

		/* Save AQ status from download package */
		hw->pkg_dwnld_status = hw->adminq.sq_last_status;
		if (status) {
			ice_debug(hw, ICE_DBG_PKG,
				  "Pkg download failed: err %d off %d inf %d\n",
				  status, offset, info);

			break;
		}

		if (last)
			break;
	}

	ice_release_global_cfg_lock(hw);

	return status;
}

/**
 * ice_aq_get_pkg_info_list
 * @hw: pointer to the hardware structure
 * @pkg_info: the buffer which will receive the information list
 * @buf_size: the size of the pkg_info information buffer
 * @cd: pointer to command details structure or NULL
 *
 * Get Package Info List (0x0C43)
 */
static enum ice_status
ice_aq_get_pkg_info_list(struct ice_hw *hw,
			 struct ice_aqc_get_pkg_info_resp *pkg_info,
			 u16 buf_size, struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_pkg_info_list);

	return ice_aq_send_cmd(hw, &desc, pkg_info, buf_size, cd);
}

/**
 * ice_download_pkg
 * @hw: pointer to the hardware structure
 * @ice_seg: pointer to the segment of the package to be downloaded
 *
 * Handles the download of a complete package.
 */
static enum ice_status
ice_download_pkg(struct ice_hw *hw, struct ice_seg *ice_seg)
{
	struct ice_buf_table *ice_buf_tbl;

	ice_debug(hw, ICE_DBG_PKG, "Segment format version: %d.%d.%d.%d\n",
		  ice_seg->hdr.seg_format_ver.major,
		  ice_seg->hdr.seg_format_ver.minor,
		  ice_seg->hdr.seg_format_ver.update,
		  ice_seg->hdr.seg_format_ver.draft);

	ice_debug(hw, ICE_DBG_PKG, "Seg: type 0x%X, size %d, name %s\n",
		  le32_to_cpu(ice_seg->hdr.seg_type),
		  le32_to_cpu(ice_seg->hdr.seg_size), ice_seg->hdr.seg_id);

	ice_buf_tbl = ice_find_buf_table(ice_seg);

	ice_debug(hw, ICE_DBG_PKG, "Seg buf count: %d\n",
		  le32_to_cpu(ice_buf_tbl->buf_count));

	return ice_dwnld_cfg_bufs(hw, ice_buf_tbl->buf_array,
				  le32_to_cpu(ice_buf_tbl->buf_count));
}

/**
 * ice_init_pkg_info
 * @hw: pointer to the hardware structure
 * @pkg_hdr: pointer to the driver's package hdr
 *
 * Saves off the package details into the HW structure.
 */
static enum ice_status
ice_init_pkg_info(struct ice_hw *hw, struct ice_pkg_hdr *pkg_hdr)
{
	struct ice_global_metadata_seg *meta_seg;
	struct ice_generic_seg_hdr *seg_hdr;

	if (!pkg_hdr)
		return ICE_ERR_PARAM;

	meta_seg = (struct ice_global_metadata_seg *)
		   ice_find_seg_in_pkg(hw, SEGMENT_TYPE_METADATA, pkg_hdr);
	if (meta_seg) {
		hw->pkg_ver = meta_seg->pkg_ver;
		memcpy(hw->pkg_name, meta_seg->pkg_name, sizeof(hw->pkg_name));

		ice_debug(hw, ICE_DBG_PKG, "Pkg: %d.%d.%d.%d, %s\n",
			  meta_seg->pkg_ver.major, meta_seg->pkg_ver.minor,
			  meta_seg->pkg_ver.update, meta_seg->pkg_ver.draft,
			  meta_seg->pkg_name);
	} else {
		ice_debug(hw, ICE_DBG_INIT,
			  "Did not find metadata segment in driver package\n");
		return ICE_ERR_CFG;
	}

	seg_hdr = ice_find_seg_in_pkg(hw, SEGMENT_TYPE_ICE, pkg_hdr);
	if (seg_hdr) {
		hw->ice_pkg_ver = seg_hdr->seg_format_ver;
		memcpy(hw->ice_pkg_name, seg_hdr->seg_id,
		       sizeof(hw->ice_pkg_name));

		ice_debug(hw, ICE_DBG_PKG, "Ice Seg: %d.%d.%d.%d, %s\n",
			  seg_hdr->seg_format_ver.major,
			  seg_hdr->seg_format_ver.minor,
			  seg_hdr->seg_format_ver.update,
			  seg_hdr->seg_format_ver.draft,
			  seg_hdr->seg_id);
	} else {
		ice_debug(hw, ICE_DBG_INIT,
			  "Did not find ice segment in driver package\n");
		return ICE_ERR_CFG;
	}

	return 0;
}

/**
 * ice_get_pkg_info
 * @hw: pointer to the hardware structure
 *
 * Store details of the package currently loaded in HW into the HW structure.
 */
static enum ice_status ice_get_pkg_info(struct ice_hw *hw)
{
	struct ice_aqc_get_pkg_info_resp *pkg_info;
	enum ice_status status;
	u16 size;
	u32 i;

	size = sizeof(*pkg_info) + (sizeof(pkg_info->pkg_info[0]) *
				    (ICE_PKG_CNT - 1));
	pkg_info = kzalloc(size, GFP_KERNEL);
	if (!pkg_info)
		return ICE_ERR_NO_MEMORY;

	status = ice_aq_get_pkg_info_list(hw, pkg_info, size, NULL);
	if (status)
		goto init_pkg_free_alloc;

	for (i = 0; i < le32_to_cpu(pkg_info->count); i++) {
#define ICE_PKG_FLAG_COUNT	4
		char flags[ICE_PKG_FLAG_COUNT + 1] = { 0 };
		u8 place = 0;

		if (pkg_info->pkg_info[i].is_active) {
			flags[place++] = 'A';
			hw->active_pkg_ver = pkg_info->pkg_info[i].ver;
			hw->active_track_id =
				le32_to_cpu(pkg_info->pkg_info[i].track_id);
			memcpy(hw->active_pkg_name,
			       pkg_info->pkg_info[i].name,
			       sizeof(pkg_info->pkg_info[i].name));
			hw->active_pkg_in_nvm = pkg_info->pkg_info[i].is_in_nvm;
		}
		if (pkg_info->pkg_info[i].is_active_at_boot)
			flags[place++] = 'B';
		if (pkg_info->pkg_info[i].is_modified)
			flags[place++] = 'M';
		if (pkg_info->pkg_info[i].is_in_nvm)
			flags[place++] = 'N';

		ice_debug(hw, ICE_DBG_PKG, "Pkg[%d]: %d.%d.%d.%d,%s,%s\n",
			  i, pkg_info->pkg_info[i].ver.major,
			  pkg_info->pkg_info[i].ver.minor,
			  pkg_info->pkg_info[i].ver.update,
			  pkg_info->pkg_info[i].ver.draft,
			  pkg_info->pkg_info[i].name, flags);
	}

init_pkg_free_alloc:
	kfree(pkg_info);

	return status;
}

/**
 * ice_verify_pkg - verify package
 * @pkg: pointer to the package buffer
 * @len: size of the package buffer
 *
 * Verifies various attributes of the package file, including length, format
 * version, and the requirement of at least one segment.
 */
static enum ice_status ice_verify_pkg(struct ice_pkg_hdr *pkg, u32 len)
{
	u32 seg_count;
	u32 i;

	if (len < sizeof(*pkg))
		return ICE_ERR_BUF_TOO_SHORT;

	if (pkg->pkg_format_ver.major != ICE_PKG_FMT_VER_MAJ ||
	    pkg->pkg_format_ver.minor != ICE_PKG_FMT_VER_MNR ||
	    pkg->pkg_format_ver.update != ICE_PKG_FMT_VER_UPD ||
	    pkg->pkg_format_ver.draft != ICE_PKG_FMT_VER_DFT)
		return ICE_ERR_CFG;

	/* pkg must have at least one segment */
	seg_count = le32_to_cpu(pkg->seg_count);
	if (seg_count < 1)
		return ICE_ERR_CFG;

	/* make sure segment array fits in package length */
	if (len < sizeof(*pkg) + ((seg_count - 1) * sizeof(pkg->seg_offset)))
		return ICE_ERR_BUF_TOO_SHORT;

	/* all segments must fit within length */
	for (i = 0; i < seg_count; i++) {
		u32 off = le32_to_cpu(pkg->seg_offset[i]);
		struct ice_generic_seg_hdr *seg;

		/* segment header must fit */
		if (len < off + sizeof(*seg))
			return ICE_ERR_BUF_TOO_SHORT;

		seg = (struct ice_generic_seg_hdr *)((u8 *)pkg + off);

		/* segment body must fit */
		if (len < off + le32_to_cpu(seg->seg_size))
			return ICE_ERR_BUF_TOO_SHORT;
	}

	return 0;
}

/**
 * ice_free_seg - free package segment pointer
 * @hw: pointer to the hardware structure
 *
 * Frees the package segment pointer in the proper manner, depending on if the
 * segment was allocated or just the passed in pointer was stored.
 */
void ice_free_seg(struct ice_hw *hw)
{
	if (hw->pkg_copy) {
		devm_kfree(ice_hw_to_dev(hw), hw->pkg_copy);
		hw->pkg_copy = NULL;
		hw->pkg_size = 0;
	}
	hw->seg = NULL;
}

/**
 * ice_init_pkg_regs - initialize additional package registers
 * @hw: pointer to the hardware structure
 */
static void ice_init_pkg_regs(struct ice_hw *hw)
{
#define ICE_SW_BLK_INP_MASK_L 0xFFFFFFFF
#define ICE_SW_BLK_INP_MASK_H 0x0000FFFF
#define ICE_SW_BLK_IDX	0

	/* setup Switch block input mask, which is 48-bits in two parts */
	wr32(hw, GL_PREEXT_L2_PMASK0(ICE_SW_BLK_IDX), ICE_SW_BLK_INP_MASK_L);
	wr32(hw, GL_PREEXT_L2_PMASK1(ICE_SW_BLK_IDX), ICE_SW_BLK_INP_MASK_H);
}

/**
 * ice_chk_pkg_version - check package version for compatibility with driver
 * @pkg_ver: pointer to a version structure to check
 *
 * Check to make sure that the package about to be downloaded is compatible with
 * the driver. To be compatible, the major and minor components of the package
 * version must match our ICE_PKG_SUPP_VER_MAJ and ICE_PKG_SUPP_VER_MNR
 * definitions.
 */
static enum ice_status ice_chk_pkg_version(struct ice_pkg_ver *pkg_ver)
{
	if (pkg_ver->major != ICE_PKG_SUPP_VER_MAJ ||
	    pkg_ver->minor != ICE_PKG_SUPP_VER_MNR)
		return ICE_ERR_NOT_SUPPORTED;

	return 0;
}

/**
 * ice_chk_pkg_compat
 * @hw: pointer to the hardware structure
 * @ospkg: pointer to the package hdr
 * @seg: pointer to the package segment hdr
 *
 * This function checks the package version compatibility with driver and NVM
 */
static enum ice_status
ice_chk_pkg_compat(struct ice_hw *hw, struct ice_pkg_hdr *ospkg,
		   struct ice_seg **seg)
{
	struct ice_aqc_get_pkg_info_resp *pkg;
	enum ice_status status;
	u16 size;
	u32 i;

	/* Check package version compatibility */
	status = ice_chk_pkg_version(&hw->pkg_ver);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "Package version check failed.\n");
		return status;
	}

	/* find ICE segment in given package */
	*seg = (struct ice_seg *)ice_find_seg_in_pkg(hw, SEGMENT_TYPE_ICE,
						     ospkg);
	if (!*seg) {
		ice_debug(hw, ICE_DBG_INIT, "no ice segment in package.\n");
		return ICE_ERR_CFG;
	}

	/* Check if FW is compatible with the OS package */
	size = struct_size(pkg, pkg_info, ICE_PKG_CNT - 1);
	pkg = kzalloc(size, GFP_KERNEL);
	if (!pkg)
		return ICE_ERR_NO_MEMORY;

	status = ice_aq_get_pkg_info_list(hw, pkg, size, NULL);
	if (status)
		goto fw_ddp_compat_free_alloc;

	for (i = 0; i < le32_to_cpu(pkg->count); i++) {
		/* loop till we find the NVM package */
		if (!pkg->pkg_info[i].is_in_nvm)
			continue;
		if ((*seg)->hdr.seg_format_ver.major !=
			pkg->pkg_info[i].ver.major ||
		    (*seg)->hdr.seg_format_ver.minor >
			pkg->pkg_info[i].ver.minor) {
			status = ICE_ERR_FW_DDP_MISMATCH;
			ice_debug(hw, ICE_DBG_INIT,
				  "OS package is not compatible with NVM.\n");
		}
		/* done processing NVM package so break */
		break;
	}
fw_ddp_compat_free_alloc:
	kfree(pkg);
	return status;
}

/**
 * ice_init_pkg - initialize/download package
 * @hw: pointer to the hardware structure
 * @buf: pointer to the package buffer
 * @len: size of the package buffer
 *
 * This function initializes a package. The package contains HW tables
 * required to do packet processing. First, the function extracts package
 * information such as version. Then it finds the ice configuration segment
 * within the package; this function then saves a copy of the segment pointer
 * within the supplied package buffer. Next, the function will cache any hints
 * from the package, followed by downloading the package itself. Note, that if
 * a previous PF driver has already downloaded the package successfully, then
 * the current driver will not have to download the package again.
 *
 * The local package contents will be used to query default behavior and to
 * update specific sections of the HW's version of the package (e.g. to update
 * the parse graph to understand new protocols).
 *
 * This function stores a pointer to the package buffer memory, and it is
 * expected that the supplied buffer will not be freed immediately. If the
 * package buffer needs to be freed, such as when read from a file, use
 * ice_copy_and_init_pkg() instead of directly calling ice_init_pkg() in this
 * case.
 */
enum ice_status ice_init_pkg(struct ice_hw *hw, u8 *buf, u32 len)
{
	struct ice_pkg_hdr *pkg;
	enum ice_status status;
	struct ice_seg *seg;

	if (!buf || !len)
		return ICE_ERR_PARAM;

	pkg = (struct ice_pkg_hdr *)buf;
	status = ice_verify_pkg(pkg, len);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "failed to verify pkg (err: %d)\n",
			  status);
		return status;
	}

	/* initialize package info */
	status = ice_init_pkg_info(hw, pkg);
	if (status)
		return status;

	/* before downloading the package, check package version for
	 * compatibility with driver
	 */
	status = ice_chk_pkg_compat(hw, pkg, &seg);
	if (status)
		return status;

	/* initialize package hints and then download package */
	ice_init_pkg_hints(hw, seg);
	status = ice_download_pkg(hw, seg);
	if (status == ICE_ERR_AQ_NO_WORK) {
		ice_debug(hw, ICE_DBG_INIT,
			  "package previously loaded - no work.\n");
		status = 0;
	}

	/* Get information on the package currently loaded in HW, then make sure
	 * the driver is compatible with this version.
	 */
	if (!status) {
		status = ice_get_pkg_info(hw);
		if (!status)
			status = ice_chk_pkg_version(&hw->active_pkg_ver);
	}

	if (!status) {
		hw->seg = seg;
		/* on successful package download update other required
		 * registers to support the package and fill HW tables
		 * with package content.
		 */
		ice_init_pkg_regs(hw);
		ice_fill_blk_tbls(hw);
	} else {
		ice_debug(hw, ICE_DBG_INIT, "package load failed, %d\n",
			  status);
	}

	return status;
}

/**
 * ice_copy_and_init_pkg - initialize/download a copy of the package
 * @hw: pointer to the hardware structure
 * @buf: pointer to the package buffer
 * @len: size of the package buffer
 *
 * This function copies the package buffer, and then calls ice_init_pkg() to
 * initialize the copied package contents.
 *
 * The copying is necessary if the package buffer supplied is constant, or if
 * the memory may disappear shortly after calling this function.
 *
 * If the package buffer resides in the data segment and can be modified, the
 * caller is free to use ice_init_pkg() instead of ice_copy_and_init_pkg().
 *
 * However, if the package buffer needs to be copied first, such as when being
 * read from a file, the caller should use ice_copy_and_init_pkg().
 *
 * This function will first copy the package buffer, before calling
 * ice_init_pkg(). The caller is free to immediately destroy the original
 * package buffer, as the new copy will be managed by this function and
 * related routines.
 */
enum ice_status ice_copy_and_init_pkg(struct ice_hw *hw, const u8 *buf, u32 len)
{
	enum ice_status status;
	u8 *buf_copy;

	if (!buf || !len)
		return ICE_ERR_PARAM;

	buf_copy = devm_kmemdup(ice_hw_to_dev(hw), buf, len, GFP_KERNEL);

	status = ice_init_pkg(hw, buf_copy, len);
	if (status) {
		/* Free the copy, since we failed to initialize the package */
		devm_kfree(ice_hw_to_dev(hw), buf_copy);
	} else {
		/* Track the copied pkg so we can free it later */
		hw->pkg_copy = buf_copy;
		hw->pkg_size = len;
	}

	return status;
}

/**
 * ice_pkg_buf_alloc
 * @hw: pointer to the HW structure
 *
 * Allocates a package buffer and returns a pointer to the buffer header.
 * Note: all package contents must be in Little Endian form.
 */
static struct ice_buf_build *ice_pkg_buf_alloc(struct ice_hw *hw)
{
	struct ice_buf_build *bld;
	struct ice_buf_hdr *buf;

	bld = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*bld), GFP_KERNEL);
	if (!bld)
		return NULL;

	buf = (struct ice_buf_hdr *)bld;
	buf->data_end = cpu_to_le16(offsetof(struct ice_buf_hdr,
					     section_entry));
	return bld;
}

/**
 * ice_pkg_buf_free
 * @hw: pointer to the HW structure
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 *
 * Frees a package buffer
 */
static void ice_pkg_buf_free(struct ice_hw *hw, struct ice_buf_build *bld)
{
	devm_kfree(ice_hw_to_dev(hw), bld);
}

/**
 * ice_pkg_buf_reserve_section
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 * @count: the number of sections to reserve
 *
 * Reserves one or more section table entries in a package buffer. This routine
 * can be called multiple times as long as they are made before calling
 * ice_pkg_buf_alloc_section(). Once ice_pkg_buf_alloc_section()
 * is called once, the number of sections that can be allocated will not be able
 * to be increased; not using all reserved sections is fine, but this will
 * result in some wasted space in the buffer.
 * Note: all package contents must be in Little Endian form.
 */
static enum ice_status
ice_pkg_buf_reserve_section(struct ice_buf_build *bld, u16 count)
{
	struct ice_buf_hdr *buf;
	u16 section_count;
	u16 data_end;

	if (!bld)
		return ICE_ERR_PARAM;

	buf = (struct ice_buf_hdr *)&bld->buf;

	/* already an active section, can't increase table size */
	section_count = le16_to_cpu(buf->section_count);
	if (section_count > 0)
		return ICE_ERR_CFG;

	if (bld->reserved_section_table_entries + count > ICE_MAX_S_COUNT)
		return ICE_ERR_CFG;
	bld->reserved_section_table_entries += count;

	data_end = le16_to_cpu(buf->data_end) +
		   (count * sizeof(buf->section_entry[0]));
	buf->data_end = cpu_to_le16(data_end);

	return 0;
}

/**
 * ice_pkg_buf_alloc_section
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 * @type: the section type value
 * @size: the size of the section to reserve (in bytes)
 *
 * Reserves memory in the buffer for a section's content and updates the
 * buffers' status accordingly. This routine returns a pointer to the first
 * byte of the section start within the buffer, which is used to fill in the
 * section contents.
 * Note: all package contents must be in Little Endian form.
 */
static void *
ice_pkg_buf_alloc_section(struct ice_buf_build *bld, u32 type, u16 size)
{
	struct ice_buf_hdr *buf;
	u16 sect_count;
	u16 data_end;

	if (!bld || !type || !size)
		return NULL;

	buf = (struct ice_buf_hdr *)&bld->buf;

	/* check for enough space left in buffer */
	data_end = le16_to_cpu(buf->data_end);

	/* section start must align on 4 byte boundary */
	data_end = ALIGN(data_end, 4);

	if ((data_end + size) > ICE_MAX_S_DATA_END)
		return NULL;

	/* check for more available section table entries */
	sect_count = le16_to_cpu(buf->section_count);
	if (sect_count < bld->reserved_section_table_entries) {
		void *section_ptr = ((u8 *)buf) + data_end;

		buf->section_entry[sect_count].offset = cpu_to_le16(data_end);
		buf->section_entry[sect_count].size = cpu_to_le16(size);
		buf->section_entry[sect_count].type = cpu_to_le32(type);

		data_end += size;
		buf->data_end = cpu_to_le16(data_end);

		buf->section_count = cpu_to_le16(sect_count + 1);
		return section_ptr;
	}

	/* no free section table entries */
	return NULL;
}

/**
 * ice_pkg_buf_get_active_sections
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 *
 * Returns the number of active sections. Before using the package buffer
 * in an update package command, the caller should make sure that there is at
 * least one active section - otherwise, the buffer is not legal and should
 * not be used.
 * Note: all package contents must be in Little Endian form.
 */
static u16 ice_pkg_buf_get_active_sections(struct ice_buf_build *bld)
{
	struct ice_buf_hdr *buf;

	if (!bld)
		return 0;

	buf = (struct ice_buf_hdr *)&bld->buf;
	return le16_to_cpu(buf->section_count);
}

/**
 * ice_pkg_buf
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 *
 * Return a pointer to the buffer's header
 */
static struct ice_buf *ice_pkg_buf(struct ice_buf_build *bld)
{
	if (!bld)
		return NULL;

	return &bld->buf;
}

/**
 * ice_tunnel_port_in_use_hlpr - helper function to determine tunnel usage
 * @hw: pointer to the HW structure
 * @port: port to search for
 * @index: optionally returns index
 *
 * Returns whether a port is already in use as a tunnel, and optionally its
 * index
 */
static bool ice_tunnel_port_in_use_hlpr(struct ice_hw *hw, u16 port, u16 *index)
{
	u16 i;

	for (i = 0; i < hw->tnl.count && i < ICE_TUNNEL_MAX_ENTRIES; i++)
		if (hw->tnl.tbl[i].in_use && hw->tnl.tbl[i].port == port) {
			if (index)
				*index = i;
			return true;
		}

	return false;
}

/**
 * ice_tunnel_port_in_use
 * @hw: pointer to the HW structure
 * @port: port to search for
 * @index: optionally returns index
 *
 * Returns whether a port is already in use as a tunnel, and optionally its
 * index
 */
bool ice_tunnel_port_in_use(struct ice_hw *hw, u16 port, u16 *index)
{
	bool res;

	mutex_lock(&hw->tnl_lock);
	res = ice_tunnel_port_in_use_hlpr(hw, port, index);
	mutex_unlock(&hw->tnl_lock);

	return res;
}

/**
 * ice_find_free_tunnel_entry
 * @hw: pointer to the HW structure
 * @type: tunnel type
 * @index: optionally returns index
 *
 * Returns whether there is a free tunnel entry, and optionally its index
 */
static bool
ice_find_free_tunnel_entry(struct ice_hw *hw, enum ice_tunnel_type type,
			   u16 *index)
{
	u16 i;

	for (i = 0; i < hw->tnl.count && i < ICE_TUNNEL_MAX_ENTRIES; i++)
		if (hw->tnl.tbl[i].valid && !hw->tnl.tbl[i].in_use &&
		    hw->tnl.tbl[i].type == type) {
			if (index)
				*index = i;
			return true;
		}

	return false;
}

/**
 * ice_get_open_tunnel_port - retrieve an open tunnel port
 * @hw: pointer to the HW structure
 * @type: tunnel type (TNL_ALL will return any open port)
 * @port: returns open port
 */
bool
ice_get_open_tunnel_port(struct ice_hw *hw, enum ice_tunnel_type type,
			 u16 *port)
{
	bool res = false;
	u16 i;

	mutex_lock(&hw->tnl_lock);

	for (i = 0; i < hw->tnl.count && i < ICE_TUNNEL_MAX_ENTRIES; i++)
		if (hw->tnl.tbl[i].valid && hw->tnl.tbl[i].in_use &&
		    (type == TNL_ALL || hw->tnl.tbl[i].type == type)) {
			*port = hw->tnl.tbl[i].port;
			res = true;
			break;
		}

	mutex_unlock(&hw->tnl_lock);

	return res;
}

/**
 * ice_create_tunnel
 * @hw: pointer to the HW structure
 * @type: type of tunnel
 * @port: port of tunnel to create
 *
 * Create a tunnel by updating the parse graph in the parser. We do that by
 * creating a package buffer with the tunnel info and issuing an update package
 * command.
 */
enum ice_status
ice_create_tunnel(struct ice_hw *hw, enum ice_tunnel_type type, u16 port)
{
	struct ice_boost_tcam_section *sect_rx, *sect_tx;
	enum ice_status status = ICE_ERR_MAX_LIMIT;
	struct ice_buf_build *bld;
	u16 index;

	mutex_lock(&hw->tnl_lock);

	if (ice_tunnel_port_in_use_hlpr(hw, port, &index)) {
		hw->tnl.tbl[index].ref++;
		status = 0;
		goto ice_create_tunnel_end;
	}

	if (!ice_find_free_tunnel_entry(hw, type, &index)) {
		status = ICE_ERR_OUT_OF_RANGE;
		goto ice_create_tunnel_end;
	}

	bld = ice_pkg_buf_alloc(hw);
	if (!bld) {
		status = ICE_ERR_NO_MEMORY;
		goto ice_create_tunnel_end;
	}

	/* allocate 2 sections, one for Rx parser, one for Tx parser */
	if (ice_pkg_buf_reserve_section(bld, 2))
		goto ice_create_tunnel_err;

	sect_rx = ice_pkg_buf_alloc_section(bld, ICE_SID_RXPARSER_BOOST_TCAM,
					    sizeof(*sect_rx));
	if (!sect_rx)
		goto ice_create_tunnel_err;
	sect_rx->count = cpu_to_le16(1);

	sect_tx = ice_pkg_buf_alloc_section(bld, ICE_SID_TXPARSER_BOOST_TCAM,
					    sizeof(*sect_tx));
	if (!sect_tx)
		goto ice_create_tunnel_err;
	sect_tx->count = cpu_to_le16(1);

	/* copy original boost entry to update package buffer */
	memcpy(sect_rx->tcam, hw->tnl.tbl[index].boost_entry,
	       sizeof(*sect_rx->tcam));

	/* over-write the never-match dest port key bits with the encoded port
	 * bits
	 */
	ice_set_key((u8 *)&sect_rx->tcam[0].key, sizeof(sect_rx->tcam[0].key),
		    (u8 *)&port, NULL, NULL, NULL,
		    (u16)offsetof(struct ice_boost_key_value, hv_dst_port_key),
		    sizeof(sect_rx->tcam[0].key.key.hv_dst_port_key));

	/* exact copy of entry to Tx section entry */
	memcpy(sect_tx->tcam, sect_rx->tcam, sizeof(*sect_tx->tcam));

	status = ice_update_pkg(hw, ice_pkg_buf(bld), 1);
	if (!status) {
		hw->tnl.tbl[index].port = port;
		hw->tnl.tbl[index].in_use = true;
		hw->tnl.tbl[index].ref = 1;
	}

ice_create_tunnel_err:
	ice_pkg_buf_free(hw, bld);

ice_create_tunnel_end:
	mutex_unlock(&hw->tnl_lock);

	return status;
}

/**
 * ice_destroy_tunnel
 * @hw: pointer to the HW structure
 * @port: port of tunnel to destroy (ignored if the all parameter is true)
 * @all: flag that states to destroy all tunnels
 *
 * Destroys a tunnel or all tunnels by creating an update package buffer
 * targeting the specific updates requested and then performing an update
 * package.
 */
enum ice_status ice_destroy_tunnel(struct ice_hw *hw, u16 port, bool all)
{
	struct ice_boost_tcam_section *sect_rx, *sect_tx;
	enum ice_status status = ICE_ERR_MAX_LIMIT;
	struct ice_buf_build *bld;
	u16 count = 0;
	u16 index;
	u16 size;
	u16 i;

	mutex_lock(&hw->tnl_lock);

	if (!all && ice_tunnel_port_in_use_hlpr(hw, port, &index))
		if (hw->tnl.tbl[index].ref > 1) {
			hw->tnl.tbl[index].ref--;
			status = 0;
			goto ice_destroy_tunnel_end;
		}

	/* determine count */
	for (i = 0; i < hw->tnl.count && i < ICE_TUNNEL_MAX_ENTRIES; i++)
		if (hw->tnl.tbl[i].valid && hw->tnl.tbl[i].in_use &&
		    (all || hw->tnl.tbl[i].port == port))
			count++;

	if (!count) {
		status = ICE_ERR_PARAM;
		goto ice_destroy_tunnel_end;
	}

	/* size of section - there is at least one entry */
	size = struct_size(sect_rx, tcam, count - 1);

	bld = ice_pkg_buf_alloc(hw);
	if (!bld) {
		status = ICE_ERR_NO_MEMORY;
		goto ice_destroy_tunnel_end;
	}

	/* allocate 2 sections, one for Rx parser, one for Tx parser */
	if (ice_pkg_buf_reserve_section(bld, 2))
		goto ice_destroy_tunnel_err;

	sect_rx = ice_pkg_buf_alloc_section(bld, ICE_SID_RXPARSER_BOOST_TCAM,
					    size);
	if (!sect_rx)
		goto ice_destroy_tunnel_err;
	sect_rx->count = cpu_to_le16(1);

	sect_tx = ice_pkg_buf_alloc_section(bld, ICE_SID_TXPARSER_BOOST_TCAM,
					    size);
	if (!sect_tx)
		goto ice_destroy_tunnel_err;
	sect_tx->count = cpu_to_le16(1);

	/* copy original boost entry to update package buffer, one copy to Rx
	 * section, another copy to the Tx section
	 */
	for (i = 0; i < hw->tnl.count && i < ICE_TUNNEL_MAX_ENTRIES; i++)
		if (hw->tnl.tbl[i].valid && hw->tnl.tbl[i].in_use &&
		    (all || hw->tnl.tbl[i].port == port)) {
			memcpy(sect_rx->tcam + i, hw->tnl.tbl[i].boost_entry,
			       sizeof(*sect_rx->tcam));
			memcpy(sect_tx->tcam + i, hw->tnl.tbl[i].boost_entry,
			       sizeof(*sect_tx->tcam));
			hw->tnl.tbl[i].marked = true;
		}

	status = ice_update_pkg(hw, ice_pkg_buf(bld), 1);
	if (!status)
		for (i = 0; i < hw->tnl.count &&
		     i < ICE_TUNNEL_MAX_ENTRIES; i++)
			if (hw->tnl.tbl[i].marked) {
				hw->tnl.tbl[i].ref = 0;
				hw->tnl.tbl[i].port = 0;
				hw->tnl.tbl[i].in_use = false;
				hw->tnl.tbl[i].marked = false;
			}

ice_destroy_tunnel_err:
	ice_pkg_buf_free(hw, bld);

ice_destroy_tunnel_end:
	mutex_unlock(&hw->tnl_lock);

	return status;
}

/* PTG Management */

/**
 * ice_ptg_find_ptype - Search for packet type group using packet type (ptype)
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @ptype: the ptype to search for
 * @ptg: pointer to variable that receives the PTG
 *
 * This function will search the PTGs for a particular ptype, returning the
 * PTG ID that contains it through the PTG parameter, with the value of
 * ICE_DEFAULT_PTG (0) meaning it is part the default PTG.
 */
static enum ice_status
ice_ptg_find_ptype(struct ice_hw *hw, enum ice_block blk, u16 ptype, u8 *ptg)
{
	if (ptype >= ICE_XLT1_CNT || !ptg)
		return ICE_ERR_PARAM;

	*ptg = hw->blk[blk].xlt1.ptypes[ptype].ptg;
	return 0;
}

/**
 * ice_ptg_alloc_val - Allocates a new packet type group ID by value
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @ptg: the PTG to allocate
 *
 * This function allocates a given packet type group ID specified by the PTG
 * parameter.
 */
static void ice_ptg_alloc_val(struct ice_hw *hw, enum ice_block blk, u8 ptg)
{
	hw->blk[blk].xlt1.ptg_tbl[ptg].in_use = true;
}

/**
 * ice_ptg_remove_ptype - Removes ptype from a particular packet type group
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @ptype: the ptype to remove
 * @ptg: the PTG to remove the ptype from
 *
 * This function will remove the ptype from the specific PTG, and move it to
 * the default PTG (ICE_DEFAULT_PTG).
 */
static enum ice_status
ice_ptg_remove_ptype(struct ice_hw *hw, enum ice_block blk, u16 ptype, u8 ptg)
{
	struct ice_ptg_ptype **ch;
	struct ice_ptg_ptype *p;

	if (ptype > ICE_XLT1_CNT - 1)
		return ICE_ERR_PARAM;

	if (!hw->blk[blk].xlt1.ptg_tbl[ptg].in_use)
		return ICE_ERR_DOES_NOT_EXIST;

	/* Should not happen if .in_use is set, bad config */
	if (!hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype)
		return ICE_ERR_CFG;

	/* find the ptype within this PTG, and bypass the link over it */
	p = hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype;
	ch = &hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype;
	while (p) {
		if (ptype == (p - hw->blk[blk].xlt1.ptypes)) {
			*ch = p->next_ptype;
			break;
		}

		ch = &p->next_ptype;
		p = p->next_ptype;
	}

	hw->blk[blk].xlt1.ptypes[ptype].ptg = ICE_DEFAULT_PTG;
	hw->blk[blk].xlt1.ptypes[ptype].next_ptype = NULL;

	return 0;
}

/**
 * ice_ptg_add_mv_ptype - Adds/moves ptype to a particular packet type group
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @ptype: the ptype to add or move
 * @ptg: the PTG to add or move the ptype to
 *
 * This function will either add or move a ptype to a particular PTG depending
 * on if the ptype is already part of another group. Note that using a
 * a destination PTG ID of ICE_DEFAULT_PTG (0) will move the ptype to the
 * default PTG.
 */
static enum ice_status
ice_ptg_add_mv_ptype(struct ice_hw *hw, enum ice_block blk, u16 ptype, u8 ptg)
{
	enum ice_status status;
	u8 original_ptg;

	if (ptype > ICE_XLT1_CNT - 1)
		return ICE_ERR_PARAM;

	if (!hw->blk[blk].xlt1.ptg_tbl[ptg].in_use && ptg != ICE_DEFAULT_PTG)
		return ICE_ERR_DOES_NOT_EXIST;

	status = ice_ptg_find_ptype(hw, blk, ptype, &original_ptg);
	if (status)
		return status;

	/* Is ptype already in the correct PTG? */
	if (original_ptg == ptg)
		return 0;

	/* Remove from original PTG and move back to the default PTG */
	if (original_ptg != ICE_DEFAULT_PTG)
		ice_ptg_remove_ptype(hw, blk, ptype, original_ptg);

	/* Moving to default PTG? Then we're done with this request */
	if (ptg == ICE_DEFAULT_PTG)
		return 0;

	/* Add ptype to PTG at beginning of list */
	hw->blk[blk].xlt1.ptypes[ptype].next_ptype =
		hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype;
	hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype =
		&hw->blk[blk].xlt1.ptypes[ptype];

	hw->blk[blk].xlt1.ptypes[ptype].ptg = ptg;
	hw->blk[blk].xlt1.t[ptype] = ptg;

	return 0;
}

/* Block / table size info */
struct ice_blk_size_details {
	u16 xlt1;			/* # XLT1 entries */
	u16 xlt2;			/* # XLT2 entries */
	u16 prof_tcam;			/* # profile ID TCAM entries */
	u16 prof_id;			/* # profile IDs */
	u8 prof_cdid_bits;		/* # CDID one-hot bits used in key */
	u16 prof_redir;			/* # profile redirection entries */
	u16 es;				/* # extraction sequence entries */
	u16 fvw;			/* # field vector words */
	u8 overwrite;			/* overwrite existing entries allowed */
	u8 reverse;			/* reverse FV order */
};

static const struct ice_blk_size_details blk_sizes[ICE_BLK_COUNT] = {
	/**
	 * Table Definitions
	 * XLT1 - Number of entries in XLT1 table
	 * XLT2 - Number of entries in XLT2 table
	 * TCAM - Number of entries Profile ID TCAM table
	 * CDID - Control Domain ID of the hardware block
	 * PRED - Number of entries in the Profile Redirection Table
	 * FV   - Number of entries in the Field Vector
	 * FVW  - Width (in WORDs) of the Field Vector
	 * OVR  - Overwrite existing table entries
	 * REV  - Reverse FV
	 */
	/*          XLT1        , XLT2        ,TCAM, PID,CDID,PRED,   FV, FVW */
	/*          Overwrite   , Reverse FV */
	/* SW  */ { ICE_XLT1_CNT, ICE_XLT2_CNT, 512, 256,   0,  256, 256,  48,
		    false, false },
	/* ACL */ { ICE_XLT1_CNT, ICE_XLT2_CNT, 512, 128,   0,  128, 128,  32,
		    false, false },
	/* FD  */ { ICE_XLT1_CNT, ICE_XLT2_CNT, 512, 128,   0,  128, 128,  24,
		    false, true  },
	/* RSS */ { ICE_XLT1_CNT, ICE_XLT2_CNT, 512, 128,   0,  128, 128,  24,
		    true,  true  },
	/* PE  */ { ICE_XLT1_CNT, ICE_XLT2_CNT,  64,  32,   0,   32,  32,  24,
		    false, false },
};

enum ice_sid_all {
	ICE_SID_XLT1_OFF = 0,
	ICE_SID_XLT2_OFF,
	ICE_SID_PR_OFF,
	ICE_SID_PR_REDIR_OFF,
	ICE_SID_ES_OFF,
	ICE_SID_OFF_COUNT,
};

/* Characteristic handling */

/**
 * ice_match_prop_lst - determine if properties of two lists match
 * @list1: first properties list
 * @list2: second properties list
 *
 * Count, cookies and the order must match in order to be considered equivalent.
 */
static bool
ice_match_prop_lst(struct list_head *list1, struct list_head *list2)
{
	struct ice_vsig_prof *tmp1;
	struct ice_vsig_prof *tmp2;
	u16 chk_count = 0;
	u16 count = 0;

	/* compare counts */
	list_for_each_entry(tmp1, list1, list)
		count++;
	list_for_each_entry(tmp2, list2, list)
		chk_count++;
	if (!count || count != chk_count)
		return false;

	tmp1 = list_first_entry(list1, struct ice_vsig_prof, list);
	tmp2 = list_first_entry(list2, struct ice_vsig_prof, list);

	/* profile cookies must compare, and in the exact same order to take
	 * into account priority
	 */
	while (count--) {
		if (tmp2->profile_cookie != tmp1->profile_cookie)
			return false;

		tmp1 = list_next_entry(tmp1, list);
		tmp2 = list_next_entry(tmp2, list);
	}

	return true;
}

/* VSIG Management */

/**
 * ice_vsig_find_vsi - find a VSIG that contains a specified VSI
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsi: VSI of interest
 * @vsig: pointer to receive the VSI group
 *
 * This function will lookup the VSI entry in the XLT2 list and return
 * the VSI group its associated with.
 */
static enum ice_status
ice_vsig_find_vsi(struct ice_hw *hw, enum ice_block blk, u16 vsi, u16 *vsig)
{
	if (!vsig || vsi >= ICE_MAX_VSI)
		return ICE_ERR_PARAM;

	/* As long as there's a default or valid VSIG associated with the input
	 * VSI, the functions returns a success. Any handling of VSIG will be
	 * done by the following add, update or remove functions.
	 */
	*vsig = hw->blk[blk].xlt2.vsis[vsi].vsig;

	return 0;
}

/**
 * ice_vsig_alloc_val - allocate a new VSIG by value
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsig: the VSIG to allocate
 *
 * This function will allocate a given VSIG specified by the VSIG parameter.
 */
static u16 ice_vsig_alloc_val(struct ice_hw *hw, enum ice_block blk, u16 vsig)
{
	u16 idx = vsig & ICE_VSIG_IDX_M;

	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use) {
		INIT_LIST_HEAD(&hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst);
		hw->blk[blk].xlt2.vsig_tbl[idx].in_use = true;
	}

	return ICE_VSIG_VALUE(idx, hw->pf_id);
}

/**
 * ice_vsig_alloc - Finds a free entry and allocates a new VSIG
 * @hw: pointer to the hardware structure
 * @blk: HW block
 *
 * This function will iterate through the VSIG list and mark the first
 * unused entry for the new VSIG entry as used and return that value.
 */
static u16 ice_vsig_alloc(struct ice_hw *hw, enum ice_block blk)
{
	u16 i;

	for (i = 1; i < ICE_MAX_VSIGS; i++)
		if (!hw->blk[blk].xlt2.vsig_tbl[i].in_use)
			return ice_vsig_alloc_val(hw, blk, i);

	return ICE_DEFAULT_VSIG;
}

/**
 * ice_find_dup_props_vsig - find VSI group with a specified set of properties
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @chs: characteristic list
 * @vsig: returns the VSIG with the matching profiles, if found
 *
 * Each VSIG is associated with a characteristic set; i.e. all VSIs under
 * a group have the same characteristic set. To check if there exists a VSIG
 * which has the same characteristics as the input characteristics; this
 * function will iterate through the XLT2 list and return the VSIG that has a
 * matching configuration. In order to make sure that priorities are accounted
 * for, the list must match exactly, including the order in which the
 * characteristics are listed.
 */
static enum ice_status
ice_find_dup_props_vsig(struct ice_hw *hw, enum ice_block blk,
			struct list_head *chs, u16 *vsig)
{
	struct ice_xlt2 *xlt2 = &hw->blk[blk].xlt2;
	u16 i;

	for (i = 0; i < xlt2->count; i++)
		if (xlt2->vsig_tbl[i].in_use &&
		    ice_match_prop_lst(chs, &xlt2->vsig_tbl[i].prop_lst)) {
			*vsig = ICE_VSIG_VALUE(i, hw->pf_id);
			return 0;
		}

	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_vsig_free - free VSI group
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsig: VSIG to remove
 *
 * The function will remove all VSIs associated with the input VSIG and move
 * them to the DEFAULT_VSIG and mark the VSIG available.
 */
static enum ice_status
ice_vsig_free(struct ice_hw *hw, enum ice_block blk, u16 vsig)
{
	struct ice_vsig_prof *dtmp, *del;
	struct ice_vsig_vsi *vsi_cur;
	u16 idx;

	idx = vsig & ICE_VSIG_IDX_M;
	if (idx >= ICE_MAX_VSIGS)
		return ICE_ERR_PARAM;

	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use)
		return ICE_ERR_DOES_NOT_EXIST;

	hw->blk[blk].xlt2.vsig_tbl[idx].in_use = false;

	vsi_cur = hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	/* If the VSIG has at least 1 VSI then iterate through the
	 * list and remove the VSIs before deleting the group.
	 */
	if (vsi_cur) {
		/* remove all vsis associated with this VSIG XLT2 entry */
		do {
			struct ice_vsig_vsi *tmp = vsi_cur->next_vsi;

			vsi_cur->vsig = ICE_DEFAULT_VSIG;
			vsi_cur->changed = 1;
			vsi_cur->next_vsi = NULL;
			vsi_cur = tmp;
		} while (vsi_cur);

		/* NULL terminate head of VSI list */
		hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi = NULL;
	}

	/* free characteristic list */
	list_for_each_entry_safe(del, dtmp,
				 &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
				 list) {
		list_del(&del->list);
		devm_kfree(ice_hw_to_dev(hw), del);
	}

	/* if VSIG characteristic list was cleared for reset
	 * re-initialize the list head
	 */
	INIT_LIST_HEAD(&hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst);

	return 0;
}

/**
 * ice_vsig_remove_vsi - remove VSI from VSIG
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsi: VSI to remove
 * @vsig: VSI group to remove from
 *
 * The function will remove the input VSI from its VSI group and move it
 * to the DEFAULT_VSIG.
 */
static enum ice_status
ice_vsig_remove_vsi(struct ice_hw *hw, enum ice_block blk, u16 vsi, u16 vsig)
{
	struct ice_vsig_vsi **vsi_head, *vsi_cur, *vsi_tgt;
	u16 idx;

	idx = vsig & ICE_VSIG_IDX_M;

	if (vsi >= ICE_MAX_VSI || idx >= ICE_MAX_VSIGS)
		return ICE_ERR_PARAM;

	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use)
		return ICE_ERR_DOES_NOT_EXIST;

	/* entry already in default VSIG, don't have to remove */
	if (idx == ICE_DEFAULT_VSIG)
		return 0;

	vsi_head = &hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	if (!(*vsi_head))
		return ICE_ERR_CFG;

	vsi_tgt = &hw->blk[blk].xlt2.vsis[vsi];
	vsi_cur = (*vsi_head);

	/* iterate the VSI list, skip over the entry to be removed */
	while (vsi_cur) {
		if (vsi_tgt == vsi_cur) {
			(*vsi_head) = vsi_cur->next_vsi;
			break;
		}
		vsi_head = &vsi_cur->next_vsi;
		vsi_cur = vsi_cur->next_vsi;
	}

	/* verify if VSI was removed from group list */
	if (!vsi_cur)
		return ICE_ERR_DOES_NOT_EXIST;

	vsi_cur->vsig = ICE_DEFAULT_VSIG;
	vsi_cur->changed = 1;
	vsi_cur->next_vsi = NULL;

	return 0;
}

/**
 * ice_vsig_add_mv_vsi - add or move a VSI to a VSI group
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsi: VSI to move
 * @vsig: destination VSI group
 *
 * This function will move or add the input VSI to the target VSIG.
 * The function will find the original VSIG the VSI belongs to and
 * move the entry to the DEFAULT_VSIG, update the original VSIG and
 * then move entry to the new VSIG.
 */
static enum ice_status
ice_vsig_add_mv_vsi(struct ice_hw *hw, enum ice_block blk, u16 vsi, u16 vsig)
{
	struct ice_vsig_vsi *tmp;
	enum ice_status status;
	u16 orig_vsig, idx;

	idx = vsig & ICE_VSIG_IDX_M;

	if (vsi >= ICE_MAX_VSI || idx >= ICE_MAX_VSIGS)
		return ICE_ERR_PARAM;

	/* if VSIG not in use and VSIG is not default type this VSIG
	 * doesn't exist.
	 */
	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use &&
	    vsig != ICE_DEFAULT_VSIG)
		return ICE_ERR_DOES_NOT_EXIST;

	status = ice_vsig_find_vsi(hw, blk, vsi, &orig_vsig);
	if (status)
		return status;

	/* no update required if vsigs match */
	if (orig_vsig == vsig)
		return 0;

	if (orig_vsig != ICE_DEFAULT_VSIG) {
		/* remove entry from orig_vsig and add to default VSIG */
		status = ice_vsig_remove_vsi(hw, blk, vsi, orig_vsig);
		if (status)
			return status;
	}

	if (idx == ICE_DEFAULT_VSIG)
		return 0;

	/* Create VSI entry and add VSIG and prop_mask values */
	hw->blk[blk].xlt2.vsis[vsi].vsig = vsig;
	hw->blk[blk].xlt2.vsis[vsi].changed = 1;

	/* Add new entry to the head of the VSIG list */
	tmp = hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi =
		&hw->blk[blk].xlt2.vsis[vsi];
	hw->blk[blk].xlt2.vsis[vsi].next_vsi = tmp;
	hw->blk[blk].xlt2.t[vsi] = vsig;

	return 0;
}

/**
 * ice_find_prof_id - find profile ID for a given field vector
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @fv: field vector to search for
 * @prof_id: receives the profile ID
 */
static enum ice_status
ice_find_prof_id(struct ice_hw *hw, enum ice_block blk,
		 struct ice_fv_word *fv, u8 *prof_id)
{
	struct ice_es *es = &hw->blk[blk].es;
	u16 off;
	u8 i;

	/* For FD, we don't want to re-use a existed profile with the same
	 * field vector and mask. This will cause rule interference.
	 */
	if (blk == ICE_BLK_FD)
		return ICE_ERR_DOES_NOT_EXIST;

	for (i = 0; i < (u8)es->count; i++) {
		off = i * es->fvw;

		if (memcmp(&es->t[off], fv, es->fvw * sizeof(*fv)))
			continue;

		*prof_id = i;
		return 0;
	}

	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_prof_id_rsrc_type - get profile ID resource type for a block type
 * @blk: the block type
 * @rsrc_type: pointer to variable to receive the resource type
 */
static bool ice_prof_id_rsrc_type(enum ice_block blk, u16 *rsrc_type)
{
	switch (blk) {
	case ICE_BLK_FD:
		*rsrc_type = ICE_AQC_RES_TYPE_FD_PROF_BLDR_PROFID;
		break;
	case ICE_BLK_RSS:
		*rsrc_type = ICE_AQC_RES_TYPE_HASH_PROF_BLDR_PROFID;
		break;
	default:
		return false;
	}
	return true;
}

/**
 * ice_tcam_ent_rsrc_type - get TCAM entry resource type for a block type
 * @blk: the block type
 * @rsrc_type: pointer to variable to receive the resource type
 */
static bool ice_tcam_ent_rsrc_type(enum ice_block blk, u16 *rsrc_type)
{
	switch (blk) {
	case ICE_BLK_FD:
		*rsrc_type = ICE_AQC_RES_TYPE_FD_PROF_BLDR_TCAM;
		break;
	case ICE_BLK_RSS:
		*rsrc_type = ICE_AQC_RES_TYPE_HASH_PROF_BLDR_TCAM;
		break;
	default:
		return false;
	}
	return true;
}

/**
 * ice_alloc_tcam_ent - allocate hardware TCAM entry
 * @hw: pointer to the HW struct
 * @blk: the block to allocate the TCAM for
 * @tcam_idx: pointer to variable to receive the TCAM entry
 *
 * This function allocates a new entry in a Profile ID TCAM for a specific
 * block.
 */
static enum ice_status
ice_alloc_tcam_ent(struct ice_hw *hw, enum ice_block blk, u16 *tcam_idx)
{
	u16 res_type;

	if (!ice_tcam_ent_rsrc_type(blk, &res_type))
		return ICE_ERR_PARAM;

	return ice_alloc_hw_res(hw, res_type, 1, true, tcam_idx);
}

/**
 * ice_free_tcam_ent - free hardware TCAM entry
 * @hw: pointer to the HW struct
 * @blk: the block from which to free the TCAM entry
 * @tcam_idx: the TCAM entry to free
 *
 * This function frees an entry in a Profile ID TCAM for a specific block.
 */
static enum ice_status
ice_free_tcam_ent(struct ice_hw *hw, enum ice_block blk, u16 tcam_idx)
{
	u16 res_type;

	if (!ice_tcam_ent_rsrc_type(blk, &res_type))
		return ICE_ERR_PARAM;

	return ice_free_hw_res(hw, res_type, 1, &tcam_idx);
}

/**
 * ice_alloc_prof_id - allocate profile ID
 * @hw: pointer to the HW struct
 * @blk: the block to allocate the profile ID for
 * @prof_id: pointer to variable to receive the profile ID
 *
 * This function allocates a new profile ID, which also corresponds to a Field
 * Vector (Extraction Sequence) entry.
 */
static enum ice_status
ice_alloc_prof_id(struct ice_hw *hw, enum ice_block blk, u8 *prof_id)
{
	enum ice_status status;
	u16 res_type;
	u16 get_prof;

	if (!ice_prof_id_rsrc_type(blk, &res_type))
		return ICE_ERR_PARAM;

	status = ice_alloc_hw_res(hw, res_type, 1, false, &get_prof);
	if (!status)
		*prof_id = (u8)get_prof;

	return status;
}

/**
 * ice_free_prof_id - free profile ID
 * @hw: pointer to the HW struct
 * @blk: the block from which to free the profile ID
 * @prof_id: the profile ID to free
 *
 * This function frees a profile ID, which also corresponds to a Field Vector.
 */
static enum ice_status
ice_free_prof_id(struct ice_hw *hw, enum ice_block blk, u8 prof_id)
{
	u16 tmp_prof_id = (u16)prof_id;
	u16 res_type;

	if (!ice_prof_id_rsrc_type(blk, &res_type))
		return ICE_ERR_PARAM;

	return ice_free_hw_res(hw, res_type, 1, &tmp_prof_id);
}

/**
 * ice_prof_inc_ref - increment reference count for profile
 * @hw: pointer to the HW struct
 * @blk: the block from which to free the profile ID
 * @prof_id: the profile ID for which to increment the reference count
 */
static enum ice_status
ice_prof_inc_ref(struct ice_hw *hw, enum ice_block blk, u8 prof_id)
{
	if (prof_id > hw->blk[blk].es.count)
		return ICE_ERR_PARAM;

	hw->blk[blk].es.ref_count[prof_id]++;

	return 0;
}

/**
 * ice_write_es - write an extraction sequence to hardware
 * @hw: pointer to the HW struct
 * @blk: the block in which to write the extraction sequence
 * @prof_id: the profile ID to write
 * @fv: pointer to the extraction sequence to write - NULL to clear extraction
 */
static void
ice_write_es(struct ice_hw *hw, enum ice_block blk, u8 prof_id,
	     struct ice_fv_word *fv)
{
	u16 off;

	off = prof_id * hw->blk[blk].es.fvw;
	if (!fv) {
		memset(&hw->blk[blk].es.t[off], 0,
		       hw->blk[blk].es.fvw * sizeof(*fv));
		hw->blk[blk].es.written[prof_id] = false;
	} else {
		memcpy(&hw->blk[blk].es.t[off], fv,
		       hw->blk[blk].es.fvw * sizeof(*fv));
	}
}

/**
 * ice_prof_dec_ref - decrement reference count for profile
 * @hw: pointer to the HW struct
 * @blk: the block from which to free the profile ID
 * @prof_id: the profile ID for which to decrement the reference count
 */
static enum ice_status
ice_prof_dec_ref(struct ice_hw *hw, enum ice_block blk, u8 prof_id)
{
	if (prof_id > hw->blk[blk].es.count)
		return ICE_ERR_PARAM;

	if (hw->blk[blk].es.ref_count[prof_id] > 0) {
		if (!--hw->blk[blk].es.ref_count[prof_id]) {
			ice_write_es(hw, blk, prof_id, NULL);
			return ice_free_prof_id(hw, blk, prof_id);
		}
	}

	return 0;
}

/* Block / table section IDs */
static const u32 ice_blk_sids[ICE_BLK_COUNT][ICE_SID_OFF_COUNT] = {
	/* SWITCH */
	{	ICE_SID_XLT1_SW,
		ICE_SID_XLT2_SW,
		ICE_SID_PROFID_TCAM_SW,
		ICE_SID_PROFID_REDIR_SW,
		ICE_SID_FLD_VEC_SW
	},

	/* ACL */
	{	ICE_SID_XLT1_ACL,
		ICE_SID_XLT2_ACL,
		ICE_SID_PROFID_TCAM_ACL,
		ICE_SID_PROFID_REDIR_ACL,
		ICE_SID_FLD_VEC_ACL
	},

	/* FD */
	{	ICE_SID_XLT1_FD,
		ICE_SID_XLT2_FD,
		ICE_SID_PROFID_TCAM_FD,
		ICE_SID_PROFID_REDIR_FD,
		ICE_SID_FLD_VEC_FD
	},

	/* RSS */
	{	ICE_SID_XLT1_RSS,
		ICE_SID_XLT2_RSS,
		ICE_SID_PROFID_TCAM_RSS,
		ICE_SID_PROFID_REDIR_RSS,
		ICE_SID_FLD_VEC_RSS
	},

	/* PE */
	{	ICE_SID_XLT1_PE,
		ICE_SID_XLT2_PE,
		ICE_SID_PROFID_TCAM_PE,
		ICE_SID_PROFID_REDIR_PE,
		ICE_SID_FLD_VEC_PE
	}
};

/**
 * ice_init_sw_xlt1_db - init software XLT1 database from HW tables
 * @hw: pointer to the hardware structure
 * @blk: the HW block to initialize
 */
static void ice_init_sw_xlt1_db(struct ice_hw *hw, enum ice_block blk)
{
	u16 pt;

	for (pt = 0; pt < hw->blk[blk].xlt1.count; pt++) {
		u8 ptg;

		ptg = hw->blk[blk].xlt1.t[pt];
		if (ptg != ICE_DEFAULT_PTG) {
			ice_ptg_alloc_val(hw, blk, ptg);
			ice_ptg_add_mv_ptype(hw, blk, pt, ptg);
		}
	}
}

/**
 * ice_init_sw_xlt2_db - init software XLT2 database from HW tables
 * @hw: pointer to the hardware structure
 * @blk: the HW block to initialize
 */
static void ice_init_sw_xlt2_db(struct ice_hw *hw, enum ice_block blk)
{
	u16 vsi;

	for (vsi = 0; vsi < hw->blk[blk].xlt2.count; vsi++) {
		u16 vsig;

		vsig = hw->blk[blk].xlt2.t[vsi];
		if (vsig) {
			ice_vsig_alloc_val(hw, blk, vsig);
			ice_vsig_add_mv_vsi(hw, blk, vsi, vsig);
			/* no changes at this time, since this has been
			 * initialized from the original package
			 */
			hw->blk[blk].xlt2.vsis[vsi].changed = 0;
		}
	}
}

/**
 * ice_init_sw_db - init software database from HW tables
 * @hw: pointer to the hardware structure
 */
static void ice_init_sw_db(struct ice_hw *hw)
{
	u16 i;

	for (i = 0; i < ICE_BLK_COUNT; i++) {
		ice_init_sw_xlt1_db(hw, (enum ice_block)i);
		ice_init_sw_xlt2_db(hw, (enum ice_block)i);
	}
}

/**
 * ice_fill_tbl - Reads content of a single table type into database
 * @hw: pointer to the hardware structure
 * @block_id: Block ID of the table to copy
 * @sid: Section ID of the table to copy
 *
 * Will attempt to read the entire content of a given table of a single block
 * into the driver database. We assume that the buffer will always
 * be as large or larger than the data contained in the package. If
 * this condition is not met, there is most likely an error in the package
 * contents.
 */
static void ice_fill_tbl(struct ice_hw *hw, enum ice_block block_id, u32 sid)
{
	u32 dst_len, sect_len, offset = 0;
	struct ice_prof_redir_section *pr;
	struct ice_prof_id_section *pid;
	struct ice_xlt1_section *xlt1;
	struct ice_xlt2_section *xlt2;
	struct ice_sw_fv_section *es;
	struct ice_pkg_enum state;
	u8 *src, *dst;
	void *sect;

	/* if the HW segment pointer is null then the first iteration of
	 * ice_pkg_enum_section() will fail. In this case the HW tables will
	 * not be filled and return success.
	 */
	if (!hw->seg) {
		ice_debug(hw, ICE_DBG_PKG, "hw->seg is NULL, tables are not filled\n");
		return;
	}

	memset(&state, 0, sizeof(state));

	sect = ice_pkg_enum_section(hw->seg, &state, sid);

	while (sect) {
		switch (sid) {
		case ICE_SID_XLT1_SW:
		case ICE_SID_XLT1_FD:
		case ICE_SID_XLT1_RSS:
		case ICE_SID_XLT1_ACL:
		case ICE_SID_XLT1_PE:
			xlt1 = (struct ice_xlt1_section *)sect;
			src = xlt1->value;
			sect_len = le16_to_cpu(xlt1->count) *
				sizeof(*hw->blk[block_id].xlt1.t);
			dst = hw->blk[block_id].xlt1.t;
			dst_len = hw->blk[block_id].xlt1.count *
				sizeof(*hw->blk[block_id].xlt1.t);
			break;
		case ICE_SID_XLT2_SW:
		case ICE_SID_XLT2_FD:
		case ICE_SID_XLT2_RSS:
		case ICE_SID_XLT2_ACL:
		case ICE_SID_XLT2_PE:
			xlt2 = (struct ice_xlt2_section *)sect;
			src = (__force u8 *)xlt2->value;
			sect_len = le16_to_cpu(xlt2->count) *
				sizeof(*hw->blk[block_id].xlt2.t);
			dst = (u8 *)hw->blk[block_id].xlt2.t;
			dst_len = hw->blk[block_id].xlt2.count *
				sizeof(*hw->blk[block_id].xlt2.t);
			break;
		case ICE_SID_PROFID_TCAM_SW:
		case ICE_SID_PROFID_TCAM_FD:
		case ICE_SID_PROFID_TCAM_RSS:
		case ICE_SID_PROFID_TCAM_ACL:
		case ICE_SID_PROFID_TCAM_PE:
			pid = (struct ice_prof_id_section *)sect;
			src = (u8 *)pid->entry;
			sect_len = le16_to_cpu(pid->count) *
				sizeof(*hw->blk[block_id].prof.t);
			dst = (u8 *)hw->blk[block_id].prof.t;
			dst_len = hw->blk[block_id].prof.count *
				sizeof(*hw->blk[block_id].prof.t);
			break;
		case ICE_SID_PROFID_REDIR_SW:
		case ICE_SID_PROFID_REDIR_FD:
		case ICE_SID_PROFID_REDIR_RSS:
		case ICE_SID_PROFID_REDIR_ACL:
		case ICE_SID_PROFID_REDIR_PE:
			pr = (struct ice_prof_redir_section *)sect;
			src = pr->redir_value;
			sect_len = le16_to_cpu(pr->count) *
				sizeof(*hw->blk[block_id].prof_redir.t);
			dst = hw->blk[block_id].prof_redir.t;
			dst_len = hw->blk[block_id].prof_redir.count *
				sizeof(*hw->blk[block_id].prof_redir.t);
			break;
		case ICE_SID_FLD_VEC_SW:
		case ICE_SID_FLD_VEC_FD:
		case ICE_SID_FLD_VEC_RSS:
		case ICE_SID_FLD_VEC_ACL:
		case ICE_SID_FLD_VEC_PE:
			es = (struct ice_sw_fv_section *)sect;
			src = (u8 *)es->fv;
			sect_len = (u32)(le16_to_cpu(es->count) *
					 hw->blk[block_id].es.fvw) *
				sizeof(*hw->blk[block_id].es.t);
			dst = (u8 *)hw->blk[block_id].es.t;
			dst_len = (u32)(hw->blk[block_id].es.count *
					hw->blk[block_id].es.fvw) *
				sizeof(*hw->blk[block_id].es.t);
			break;
		default:
			return;
		}

		/* if the section offset exceeds destination length, terminate
		 * table fill.
		 */
		if (offset > dst_len)
			return;

		/* if the sum of section size and offset exceed destination size
		 * then we are out of bounds of the HW table size for that PF.
		 * Changing section length to fill the remaining table space
		 * of that PF.
		 */
		if ((offset + sect_len) > dst_len)
			sect_len = dst_len - offset;

		memcpy(dst + offset, src, sect_len);
		offset += sect_len;
		sect = ice_pkg_enum_section(NULL, &state, sid);
	}
}

/**
 * ice_fill_blk_tbls - Read package context for tables
 * @hw: pointer to the hardware structure
 *
 * Reads the current package contents and populates the driver
 * database with the data iteratively for all advanced feature
 * blocks. Assume that the HW tables have been allocated.
 */
void ice_fill_blk_tbls(struct ice_hw *hw)
{
	u8 i;

	for (i = 0; i < ICE_BLK_COUNT; i++) {
		enum ice_block blk_id = (enum ice_block)i;

		ice_fill_tbl(hw, blk_id, hw->blk[blk_id].xlt1.sid);
		ice_fill_tbl(hw, blk_id, hw->blk[blk_id].xlt2.sid);
		ice_fill_tbl(hw, blk_id, hw->blk[blk_id].prof.sid);
		ice_fill_tbl(hw, blk_id, hw->blk[blk_id].prof_redir.sid);
		ice_fill_tbl(hw, blk_id, hw->blk[blk_id].es.sid);
	}

	ice_init_sw_db(hw);
}

/**
 * ice_free_prof_map - free profile map
 * @hw: pointer to the hardware structure
 * @blk_idx: HW block index
 */
static void ice_free_prof_map(struct ice_hw *hw, u8 blk_idx)
{
	struct ice_es *es = &hw->blk[blk_idx].es;
	struct ice_prof_map *del, *tmp;

	mutex_lock(&es->prof_map_lock);
	list_for_each_entry_safe(del, tmp, &es->prof_map, list) {
		list_del(&del->list);
		devm_kfree(ice_hw_to_dev(hw), del);
	}
	INIT_LIST_HEAD(&es->prof_map);
	mutex_unlock(&es->prof_map_lock);
}

/**
 * ice_free_flow_profs - free flow profile entries
 * @hw: pointer to the hardware structure
 * @blk_idx: HW block index
 */
static void ice_free_flow_profs(struct ice_hw *hw, u8 blk_idx)
{
	struct ice_flow_prof *p, *tmp;

	mutex_lock(&hw->fl_profs_locks[blk_idx]);
	list_for_each_entry_safe(p, tmp, &hw->fl_profs[blk_idx], l_entry) {
		struct ice_flow_entry *e, *t;

		list_for_each_entry_safe(e, t, &p->entries, l_entry)
			ice_flow_rem_entry(hw, (enum ice_block)blk_idx,
					   ICE_FLOW_ENTRY_HNDL(e));

		list_del(&p->l_entry);
		devm_kfree(ice_hw_to_dev(hw), p);
	}
	mutex_unlock(&hw->fl_profs_locks[blk_idx]);

	/* if driver is in reset and tables are being cleared
	 * re-initialize the flow profile list heads
	 */
	INIT_LIST_HEAD(&hw->fl_profs[blk_idx]);
}

/**
 * ice_free_vsig_tbl - free complete VSIG table entries
 * @hw: pointer to the hardware structure
 * @blk: the HW block on which to free the VSIG table entries
 */
static void ice_free_vsig_tbl(struct ice_hw *hw, enum ice_block blk)
{
	u16 i;

	if (!hw->blk[blk].xlt2.vsig_tbl)
		return;

	for (i = 1; i < ICE_MAX_VSIGS; i++)
		if (hw->blk[blk].xlt2.vsig_tbl[i].in_use)
			ice_vsig_free(hw, blk, i);
}

/**
 * ice_free_hw_tbls - free hardware table memory
 * @hw: pointer to the hardware structure
 */
void ice_free_hw_tbls(struct ice_hw *hw)
{
	struct ice_rss_cfg *r, *rt;
	u8 i;

	for (i = 0; i < ICE_BLK_COUNT; i++) {
		if (hw->blk[i].is_list_init) {
			struct ice_es *es = &hw->blk[i].es;

			ice_free_prof_map(hw, i);
			mutex_destroy(&es->prof_map_lock);

			ice_free_flow_profs(hw, i);
			mutex_destroy(&hw->fl_profs_locks[i]);

			hw->blk[i].is_list_init = false;
		}
		ice_free_vsig_tbl(hw, (enum ice_block)i);
		devm_kfree(ice_hw_to_dev(hw), hw->blk[i].xlt1.ptypes);
		devm_kfree(ice_hw_to_dev(hw), hw->blk[i].xlt1.ptg_tbl);
		devm_kfree(ice_hw_to_dev(hw), hw->blk[i].xlt1.t);
		devm_kfree(ice_hw_to_dev(hw), hw->blk[i].xlt2.t);
		devm_kfree(ice_hw_to_dev(hw), hw->blk[i].xlt2.vsig_tbl);
		devm_kfree(ice_hw_to_dev(hw), hw->blk[i].xlt2.vsis);
		devm_kfree(ice_hw_to_dev(hw), hw->blk[i].prof.t);
		devm_kfree(ice_hw_to_dev(hw), hw->blk[i].prof_redir.t);
		devm_kfree(ice_hw_to_dev(hw), hw->blk[i].es.t);
		devm_kfree(ice_hw_to_dev(hw), hw->blk[i].es.ref_count);
		devm_kfree(ice_hw_to_dev(hw), hw->blk[i].es.written);
	}

	list_for_each_entry_safe(r, rt, &hw->rss_list_head, l_entry) {
		list_del(&r->l_entry);
		devm_kfree(ice_hw_to_dev(hw), r);
	}
	mutex_destroy(&hw->rss_locks);
	memset(hw->blk, 0, sizeof(hw->blk));
}

/**
 * ice_init_flow_profs - init flow profile locks and list heads
 * @hw: pointer to the hardware structure
 * @blk_idx: HW block index
 */
static void ice_init_flow_profs(struct ice_hw *hw, u8 blk_idx)
{
	mutex_init(&hw->fl_profs_locks[blk_idx]);
	INIT_LIST_HEAD(&hw->fl_profs[blk_idx]);
}

/**
 * ice_clear_hw_tbls - clear HW tables and flow profiles
 * @hw: pointer to the hardware structure
 */
void ice_clear_hw_tbls(struct ice_hw *hw)
{
	u8 i;

	for (i = 0; i < ICE_BLK_COUNT; i++) {
		struct ice_prof_redir *prof_redir = &hw->blk[i].prof_redir;
		struct ice_prof_tcam *prof = &hw->blk[i].prof;
		struct ice_xlt1 *xlt1 = &hw->blk[i].xlt1;
		struct ice_xlt2 *xlt2 = &hw->blk[i].xlt2;
		struct ice_es *es = &hw->blk[i].es;

		if (hw->blk[i].is_list_init) {
			ice_free_prof_map(hw, i);
			ice_free_flow_profs(hw, i);
		}

		ice_free_vsig_tbl(hw, (enum ice_block)i);

		memset(xlt1->ptypes, 0, xlt1->count * sizeof(*xlt1->ptypes));
		memset(xlt1->ptg_tbl, 0,
		       ICE_MAX_PTGS * sizeof(*xlt1->ptg_tbl));
		memset(xlt1->t, 0, xlt1->count * sizeof(*xlt1->t));

		memset(xlt2->vsis, 0, xlt2->count * sizeof(*xlt2->vsis));
		memset(xlt2->vsig_tbl, 0,
		       xlt2->count * sizeof(*xlt2->vsig_tbl));
		memset(xlt2->t, 0, xlt2->count * sizeof(*xlt2->t));

		memset(prof->t, 0, prof->count * sizeof(*prof->t));
		memset(prof_redir->t, 0,
		       prof_redir->count * sizeof(*prof_redir->t));

		memset(es->t, 0, es->count * sizeof(*es->t));
		memset(es->ref_count, 0, es->count * sizeof(*es->ref_count));
		memset(es->written, 0, es->count * sizeof(*es->written));
	}
}

/**
 * ice_init_hw_tbls - init hardware table memory
 * @hw: pointer to the hardware structure
 */
enum ice_status ice_init_hw_tbls(struct ice_hw *hw)
{
	u8 i;

	mutex_init(&hw->rss_locks);
	INIT_LIST_HEAD(&hw->rss_list_head);
	for (i = 0; i < ICE_BLK_COUNT; i++) {
		struct ice_prof_redir *prof_redir = &hw->blk[i].prof_redir;
		struct ice_prof_tcam *prof = &hw->blk[i].prof;
		struct ice_xlt1 *xlt1 = &hw->blk[i].xlt1;
		struct ice_xlt2 *xlt2 = &hw->blk[i].xlt2;
		struct ice_es *es = &hw->blk[i].es;
		u16 j;

		if (hw->blk[i].is_list_init)
			continue;

		ice_init_flow_profs(hw, i);
		mutex_init(&es->prof_map_lock);
		INIT_LIST_HEAD(&es->prof_map);
		hw->blk[i].is_list_init = true;

		hw->blk[i].overwrite = blk_sizes[i].overwrite;
		es->reverse = blk_sizes[i].reverse;

		xlt1->sid = ice_blk_sids[i][ICE_SID_XLT1_OFF];
		xlt1->count = blk_sizes[i].xlt1;

		xlt1->ptypes = devm_kcalloc(ice_hw_to_dev(hw), xlt1->count,
					    sizeof(*xlt1->ptypes), GFP_KERNEL);

		if (!xlt1->ptypes)
			goto err;

		xlt1->ptg_tbl = devm_kcalloc(ice_hw_to_dev(hw), ICE_MAX_PTGS,
					     sizeof(*xlt1->ptg_tbl),
					     GFP_KERNEL);

		if (!xlt1->ptg_tbl)
			goto err;

		xlt1->t = devm_kcalloc(ice_hw_to_dev(hw), xlt1->count,
				       sizeof(*xlt1->t), GFP_KERNEL);
		if (!xlt1->t)
			goto err;

		xlt2->sid = ice_blk_sids[i][ICE_SID_XLT2_OFF];
		xlt2->count = blk_sizes[i].xlt2;

		xlt2->vsis = devm_kcalloc(ice_hw_to_dev(hw), xlt2->count,
					  sizeof(*xlt2->vsis), GFP_KERNEL);

		if (!xlt2->vsis)
			goto err;

		xlt2->vsig_tbl = devm_kcalloc(ice_hw_to_dev(hw), xlt2->count,
					      sizeof(*xlt2->vsig_tbl),
					      GFP_KERNEL);
		if (!xlt2->vsig_tbl)
			goto err;

		for (j = 0; j < xlt2->count; j++)
			INIT_LIST_HEAD(&xlt2->vsig_tbl[j].prop_lst);

		xlt2->t = devm_kcalloc(ice_hw_to_dev(hw), xlt2->count,
				       sizeof(*xlt2->t), GFP_KERNEL);
		if (!xlt2->t)
			goto err;

		prof->sid = ice_blk_sids[i][ICE_SID_PR_OFF];
		prof->count = blk_sizes[i].prof_tcam;
		prof->max_prof_id = blk_sizes[i].prof_id;
		prof->cdid_bits = blk_sizes[i].prof_cdid_bits;
		prof->t = devm_kcalloc(ice_hw_to_dev(hw), prof->count,
				       sizeof(*prof->t), GFP_KERNEL);

		if (!prof->t)
			goto err;

		prof_redir->sid = ice_blk_sids[i][ICE_SID_PR_REDIR_OFF];
		prof_redir->count = blk_sizes[i].prof_redir;
		prof_redir->t = devm_kcalloc(ice_hw_to_dev(hw),
					     prof_redir->count,
					     sizeof(*prof_redir->t),
					     GFP_KERNEL);

		if (!prof_redir->t)
			goto err;

		es->sid = ice_blk_sids[i][ICE_SID_ES_OFF];
		es->count = blk_sizes[i].es;
		es->fvw = blk_sizes[i].fvw;
		es->t = devm_kcalloc(ice_hw_to_dev(hw),
				     (u32)(es->count * es->fvw),
				     sizeof(*es->t), GFP_KERNEL);
		if (!es->t)
			goto err;

		es->ref_count = devm_kcalloc(ice_hw_to_dev(hw), es->count,
					     sizeof(*es->ref_count),
					     GFP_KERNEL);

		es->written = devm_kcalloc(ice_hw_to_dev(hw), es->count,
					   sizeof(*es->written), GFP_KERNEL);
		if (!es->ref_count)
			goto err;
	}
	return 0;

err:
	ice_free_hw_tbls(hw);
	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_prof_gen_key - generate profile ID key
 * @hw: pointer to the HW struct
 * @blk: the block in which to write profile ID to
 * @ptg: packet type group (PTG) portion of key
 * @vsig: VSIG portion of key
 * @cdid: CDID portion of key
 * @flags: flag portion of key
 * @vl_msk: valid mask
 * @dc_msk: don't care mask
 * @nm_msk: never match mask
 * @key: output of profile ID key
 */
static enum ice_status
ice_prof_gen_key(struct ice_hw *hw, enum ice_block blk, u8 ptg, u16 vsig,
		 u8 cdid, u16 flags, u8 vl_msk[ICE_TCAM_KEY_VAL_SZ],
		 u8 dc_msk[ICE_TCAM_KEY_VAL_SZ], u8 nm_msk[ICE_TCAM_KEY_VAL_SZ],
		 u8 key[ICE_TCAM_KEY_SZ])
{
	struct ice_prof_id_key inkey;

	inkey.xlt1 = ptg;
	inkey.xlt2_cdid = cpu_to_le16(vsig);
	inkey.flags = cpu_to_le16(flags);

	switch (hw->blk[blk].prof.cdid_bits) {
	case 0:
		break;
	case 2:
#define ICE_CD_2_M 0xC000U
#define ICE_CD_2_S 14
		inkey.xlt2_cdid &= ~cpu_to_le16(ICE_CD_2_M);
		inkey.xlt2_cdid |= cpu_to_le16(BIT(cdid) << ICE_CD_2_S);
		break;
	case 4:
#define ICE_CD_4_M 0xF000U
#define ICE_CD_4_S 12
		inkey.xlt2_cdid &= ~cpu_to_le16(ICE_CD_4_M);
		inkey.xlt2_cdid |= cpu_to_le16(BIT(cdid) << ICE_CD_4_S);
		break;
	case 8:
#define ICE_CD_8_M 0xFF00U
#define ICE_CD_8_S 16
		inkey.xlt2_cdid &= ~cpu_to_le16(ICE_CD_8_M);
		inkey.xlt2_cdid |= cpu_to_le16(BIT(cdid) << ICE_CD_8_S);
		break;
	default:
		ice_debug(hw, ICE_DBG_PKG, "Error in profile config\n");
		break;
	}

	return ice_set_key(key, ICE_TCAM_KEY_SZ, (u8 *)&inkey, vl_msk, dc_msk,
			   nm_msk, 0, ICE_TCAM_KEY_SZ / 2);
}

/**
 * ice_tcam_write_entry - write TCAM entry
 * @hw: pointer to the HW struct
 * @blk: the block in which to write profile ID to
 * @idx: the entry index to write to
 * @prof_id: profile ID
 * @ptg: packet type group (PTG) portion of key
 * @vsig: VSIG portion of key
 * @cdid: CDID portion of key
 * @flags: flag portion of key
 * @vl_msk: valid mask
 * @dc_msk: don't care mask
 * @nm_msk: never match mask
 */
static enum ice_status
ice_tcam_write_entry(struct ice_hw *hw, enum ice_block blk, u16 idx,
		     u8 prof_id, u8 ptg, u16 vsig, u8 cdid, u16 flags,
		     u8 vl_msk[ICE_TCAM_KEY_VAL_SZ],
		     u8 dc_msk[ICE_TCAM_KEY_VAL_SZ],
		     u8 nm_msk[ICE_TCAM_KEY_VAL_SZ])
{
	struct ice_prof_tcam_entry;
	enum ice_status status;

	status = ice_prof_gen_key(hw, blk, ptg, vsig, cdid, flags, vl_msk,
				  dc_msk, nm_msk, hw->blk[blk].prof.t[idx].key);
	if (!status) {
		hw->blk[blk].prof.t[idx].addr = cpu_to_le16(idx);
		hw->blk[blk].prof.t[idx].prof_id = prof_id;
	}

	return status;
}

/**
 * ice_vsig_get_ref - returns number of VSIs belong to a VSIG
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsig: VSIG to query
 * @refs: pointer to variable to receive the reference count
 */
static enum ice_status
ice_vsig_get_ref(struct ice_hw *hw, enum ice_block blk, u16 vsig, u16 *refs)
{
	u16 idx = vsig & ICE_VSIG_IDX_M;
	struct ice_vsig_vsi *ptr;

	*refs = 0;

	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use)
		return ICE_ERR_DOES_NOT_EXIST;

	ptr = hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	while (ptr) {
		(*refs)++;
		ptr = ptr->next_vsi;
	}

	return 0;
}

/**
 * ice_has_prof_vsig - check to see if VSIG has a specific profile
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsig: VSIG to check against
 * @hdl: profile handle
 */
static bool
ice_has_prof_vsig(struct ice_hw *hw, enum ice_block blk, u16 vsig, u64 hdl)
{
	u16 idx = vsig & ICE_VSIG_IDX_M;
	struct ice_vsig_prof *ent;

	list_for_each_entry(ent, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
			    list)
		if (ent->profile_cookie == hdl)
			return true;

	ice_debug(hw, ICE_DBG_INIT,
		  "Characteristic list for VSI group %d not found.\n",
		  vsig);
	return false;
}

/**
 * ice_prof_bld_es - build profile ID extraction sequence changes
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @bld: the update package buffer build to add to
 * @chgs: the list of changes to make in hardware
 */
static enum ice_status
ice_prof_bld_es(struct ice_hw *hw, enum ice_block blk,
		struct ice_buf_build *bld, struct list_head *chgs)
{
	u16 vec_size = hw->blk[blk].es.fvw * sizeof(struct ice_fv_word);
	struct ice_chs_chg *tmp;

	list_for_each_entry(tmp, chgs, list_entry)
		if (tmp->type == ICE_PTG_ES_ADD && tmp->add_prof) {
			u16 off = tmp->prof_id * hw->blk[blk].es.fvw;
			struct ice_pkg_es *p;
			u32 id;

			id = ice_sect_id(blk, ICE_VEC_TBL);
			p = (struct ice_pkg_es *)
				ice_pkg_buf_alloc_section(bld, id, sizeof(*p) +
							  vec_size -
							  sizeof(p->es[0]));

			if (!p)
				return ICE_ERR_MAX_LIMIT;

			p->count = cpu_to_le16(1);
			p->offset = cpu_to_le16(tmp->prof_id);

			memcpy(p->es, &hw->blk[blk].es.t[off], vec_size);
		}

	return 0;
}

/**
 * ice_prof_bld_tcam - build profile ID TCAM changes
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @bld: the update package buffer build to add to
 * @chgs: the list of changes to make in hardware
 */
static enum ice_status
ice_prof_bld_tcam(struct ice_hw *hw, enum ice_block blk,
		  struct ice_buf_build *bld, struct list_head *chgs)
{
	struct ice_chs_chg *tmp;

	list_for_each_entry(tmp, chgs, list_entry)
		if (tmp->type == ICE_TCAM_ADD && tmp->add_tcam_idx) {
			struct ice_prof_id_section *p;
			u32 id;

			id = ice_sect_id(blk, ICE_PROF_TCAM);
			p = (struct ice_prof_id_section *)
				ice_pkg_buf_alloc_section(bld, id, sizeof(*p));

			if (!p)
				return ICE_ERR_MAX_LIMIT;

			p->count = cpu_to_le16(1);
			p->entry[0].addr = cpu_to_le16(tmp->tcam_idx);
			p->entry[0].prof_id = tmp->prof_id;

			memcpy(p->entry[0].key,
			       &hw->blk[blk].prof.t[tmp->tcam_idx].key,
			       sizeof(hw->blk[blk].prof.t->key));
		}

	return 0;
}

/**
 * ice_prof_bld_xlt1 - build XLT1 changes
 * @blk: hardware block
 * @bld: the update package buffer build to add to
 * @chgs: the list of changes to make in hardware
 */
static enum ice_status
ice_prof_bld_xlt1(enum ice_block blk, struct ice_buf_build *bld,
		  struct list_head *chgs)
{
	struct ice_chs_chg *tmp;

	list_for_each_entry(tmp, chgs, list_entry)
		if (tmp->type == ICE_PTG_ES_ADD && tmp->add_ptg) {
			struct ice_xlt1_section *p;
			u32 id;

			id = ice_sect_id(blk, ICE_XLT1);
			p = (struct ice_xlt1_section *)
				ice_pkg_buf_alloc_section(bld, id, sizeof(*p));

			if (!p)
				return ICE_ERR_MAX_LIMIT;

			p->count = cpu_to_le16(1);
			p->offset = cpu_to_le16(tmp->ptype);
			p->value[0] = tmp->ptg;
		}

	return 0;
}

/**
 * ice_prof_bld_xlt2 - build XLT2 changes
 * @blk: hardware block
 * @bld: the update package buffer build to add to
 * @chgs: the list of changes to make in hardware
 */
static enum ice_status
ice_prof_bld_xlt2(enum ice_block blk, struct ice_buf_build *bld,
		  struct list_head *chgs)
{
	struct ice_chs_chg *tmp;

	list_for_each_entry(tmp, chgs, list_entry) {
		struct ice_xlt2_section *p;
		u32 id;

		switch (tmp->type) {
		case ICE_VSIG_ADD:
		case ICE_VSI_MOVE:
		case ICE_VSIG_REM:
			id = ice_sect_id(blk, ICE_XLT2);
			p = (struct ice_xlt2_section *)
				ice_pkg_buf_alloc_section(bld, id, sizeof(*p));

			if (!p)
				return ICE_ERR_MAX_LIMIT;

			p->count = cpu_to_le16(1);
			p->offset = cpu_to_le16(tmp->vsi);
			p->value[0] = cpu_to_le16(tmp->vsig);
			break;
		default:
			break;
		}
	}

	return 0;
}

/**
 * ice_upd_prof_hw - update hardware using the change list
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @chgs: the list of changes to make in hardware
 */
static enum ice_status
ice_upd_prof_hw(struct ice_hw *hw, enum ice_block blk,
		struct list_head *chgs)
{
	struct ice_buf_build *b;
	struct ice_chs_chg *tmp;
	enum ice_status status;
	u16 pkg_sects;
	u16 xlt1 = 0;
	u16 xlt2 = 0;
	u16 tcam = 0;
	u16 es = 0;
	u16 sects;

	/* count number of sections we need */
	list_for_each_entry(tmp, chgs, list_entry) {
		switch (tmp->type) {
		case ICE_PTG_ES_ADD:
			if (tmp->add_ptg)
				xlt1++;
			if (tmp->add_prof)
				es++;
			break;
		case ICE_TCAM_ADD:
			tcam++;
			break;
		case ICE_VSIG_ADD:
		case ICE_VSI_MOVE:
		case ICE_VSIG_REM:
			xlt2++;
			break;
		default:
			break;
		}
	}
	sects = xlt1 + xlt2 + tcam + es;

	if (!sects)
		return 0;

	/* Build update package buffer */
	b = ice_pkg_buf_alloc(hw);
	if (!b)
		return ICE_ERR_NO_MEMORY;

	status = ice_pkg_buf_reserve_section(b, sects);
	if (status)
		goto error_tmp;

	/* Preserve order of table update: ES, TCAM, PTG, VSIG */
	if (es) {
		status = ice_prof_bld_es(hw, blk, b, chgs);
		if (status)
			goto error_tmp;
	}

	if (tcam) {
		status = ice_prof_bld_tcam(hw, blk, b, chgs);
		if (status)
			goto error_tmp;
	}

	if (xlt1) {
		status = ice_prof_bld_xlt1(blk, b, chgs);
		if (status)
			goto error_tmp;
	}

	if (xlt2) {
		status = ice_prof_bld_xlt2(blk, b, chgs);
		if (status)
			goto error_tmp;
	}

	/* After package buffer build check if the section count in buffer is
	 * non-zero and matches the number of sections detected for package
	 * update.
	 */
	pkg_sects = ice_pkg_buf_get_active_sections(b);
	if (!pkg_sects || pkg_sects != sects) {
		status = ICE_ERR_INVAL_SIZE;
		goto error_tmp;
	}

	/* update package */
	status = ice_update_pkg(hw, ice_pkg_buf(b), 1);
	if (status == ICE_ERR_AQ_ERROR)
		ice_debug(hw, ICE_DBG_INIT, "Unable to update HW profile\n");

error_tmp:
	ice_pkg_buf_free(hw, b);
	return status;
}

/**
 * ice_update_fd_mask - set Flow Director Field Vector mask for a profile
 * @hw: pointer to the HW struct
 * @prof_id: profile ID
 * @mask_sel: mask select
 *
 * This function enable any of the masks selected by the mask select parameter
 * for the profile specified.
 */
static void ice_update_fd_mask(struct ice_hw *hw, u16 prof_id, u32 mask_sel)
{
	wr32(hw, GLQF_FDMASK_SEL(prof_id), mask_sel);

	ice_debug(hw, ICE_DBG_INIT, "fd mask(%d): %x = %x\n", prof_id,
		  GLQF_FDMASK_SEL(prof_id), mask_sel);
}

struct ice_fd_src_dst_pair {
	u8 prot_id;
	u8 count;
	u16 off;
};

static const struct ice_fd_src_dst_pair ice_fd_pairs[] = {
	/* These are defined in pairs */
	{ ICE_PROT_IPV4_OF_OR_S, 2, 12 },
	{ ICE_PROT_IPV4_OF_OR_S, 2, 16 },

	{ ICE_PROT_IPV4_IL, 2, 12 },
	{ ICE_PROT_IPV4_IL, 2, 16 },

	{ ICE_PROT_IPV6_OF_OR_S, 8, 8 },
	{ ICE_PROT_IPV6_OF_OR_S, 8, 24 },

	{ ICE_PROT_IPV6_IL, 8, 8 },
	{ ICE_PROT_IPV6_IL, 8, 24 },

	{ ICE_PROT_TCP_IL, 1, 0 },
	{ ICE_PROT_TCP_IL, 1, 2 },

	{ ICE_PROT_UDP_OF, 1, 0 },
	{ ICE_PROT_UDP_OF, 1, 2 },

	{ ICE_PROT_UDP_IL_OR_S, 1, 0 },
	{ ICE_PROT_UDP_IL_OR_S, 1, 2 },

	{ ICE_PROT_SCTP_IL, 1, 0 },
	{ ICE_PROT_SCTP_IL, 1, 2 }
};

#define ICE_FD_SRC_DST_PAIR_COUNT	ARRAY_SIZE(ice_fd_pairs)

/**
 * ice_update_fd_swap - set register appropriately for a FD FV extraction
 * @hw: pointer to the HW struct
 * @prof_id: profile ID
 * @es: extraction sequence (length of array is determined by the block)
 */
static enum ice_status
ice_update_fd_swap(struct ice_hw *hw, u16 prof_id, struct ice_fv_word *es)
{
	DECLARE_BITMAP(pair_list, ICE_FD_SRC_DST_PAIR_COUNT);
	u8 pair_start[ICE_FD_SRC_DST_PAIR_COUNT] = { 0 };
#define ICE_FD_FV_NOT_FOUND (-2)
	s8 first_free = ICE_FD_FV_NOT_FOUND;
	u8 used[ICE_MAX_FV_WORDS] = { 0 };
	s8 orig_free, si;
	u32 mask_sel = 0;
	u8 i, j, k;

	bitmap_zero(pair_list, ICE_FD_SRC_DST_PAIR_COUNT);

	/* This code assumes that the Flow Director field vectors are assigned
	 * from the end of the FV indexes working towards the zero index, that
	 * only complete fields will be included and will be consecutive, and
	 * that there are no gaps between valid indexes.
	 */

	/* Determine swap fields present */
	for (i = 0; i < hw->blk[ICE_BLK_FD].es.fvw; i++) {
		/* Find the first free entry, assuming right to left population.
		 * This is where we can start adding additional pairs if needed.
		 */
		if (first_free == ICE_FD_FV_NOT_FOUND && es[i].prot_id !=
		    ICE_PROT_INVALID)
			first_free = i - 1;

		for (j = 0; j < ICE_FD_SRC_DST_PAIR_COUNT; j++)
			if (es[i].prot_id == ice_fd_pairs[j].prot_id &&
			    es[i].off == ice_fd_pairs[j].off) {
				set_bit(j, pair_list);
				pair_start[j] = i;
			}
	}

	orig_free = first_free;

	/* determine missing swap fields that need to be added */
	for (i = 0; i < ICE_FD_SRC_DST_PAIR_COUNT; i += 2) {
		u8 bit1 = test_bit(i + 1, pair_list);
		u8 bit0 = test_bit(i, pair_list);

		if (bit0 ^ bit1) {
			u8 index;

			/* add the appropriate 'paired' entry */
			if (!bit0)
				index = i;
			else
				index = i + 1;

			/* check for room */
			if (first_free + 1 < (s8)ice_fd_pairs[index].count)
				return ICE_ERR_MAX_LIMIT;

			/* place in extraction sequence */
			for (k = 0; k < ice_fd_pairs[index].count; k++) {
				es[first_free - k].prot_id =
					ice_fd_pairs[index].prot_id;
				es[first_free - k].off =
					ice_fd_pairs[index].off + (k * 2);

				if (k > first_free)
					return ICE_ERR_OUT_OF_RANGE;

				/* keep track of non-relevant fields */
				mask_sel |= BIT(first_free - k);
			}

			pair_start[index] = first_free;
			first_free -= ice_fd_pairs[index].count;
		}
	}

	/* fill in the swap array */
	si = hw->blk[ICE_BLK_FD].es.fvw - 1;
	while (si >= 0) {
		u8 indexes_used = 1;

		/* assume flat at this index */
#define ICE_SWAP_VALID	0x80
		used[si] = si | ICE_SWAP_VALID;

		if (orig_free == ICE_FD_FV_NOT_FOUND || si <= orig_free) {
			si -= indexes_used;
			continue;
		}

		/* check for a swap location */
		for (j = 0; j < ICE_FD_SRC_DST_PAIR_COUNT; j++)
			if (es[si].prot_id == ice_fd_pairs[j].prot_id &&
			    es[si].off == ice_fd_pairs[j].off) {
				u8 idx;

				/* determine the appropriate matching field */
				idx = j + ((j % 2) ? -1 : 1);

				indexes_used = ice_fd_pairs[idx].count;
				for (k = 0; k < indexes_used; k++) {
					used[si - k] = (pair_start[idx] - k) |
						ICE_SWAP_VALID;
				}

				break;
			}

		si -= indexes_used;
	}

	/* for each set of 4 swap and 4 inset indexes, write the appropriate
	 * register
	 */
	for (j = 0; j < hw->blk[ICE_BLK_FD].es.fvw / 4; j++) {
		u32 raw_swap = 0;
		u32 raw_in = 0;

		for (k = 0; k < 4; k++) {
			u8 idx;

			idx = (j * 4) + k;
			if (used[idx] && !(mask_sel & BIT(idx))) {
				raw_swap |= used[idx] << (k * BITS_PER_BYTE);
#define ICE_INSET_DFLT 0x9f
				raw_in |= ICE_INSET_DFLT << (k * BITS_PER_BYTE);
			}
		}

		/* write the appropriate swap register set */
		wr32(hw, GLQF_FDSWAP(prof_id, j), raw_swap);

		ice_debug(hw, ICE_DBG_INIT, "swap wr(%d, %d): %x = %08x\n",
			  prof_id, j, GLQF_FDSWAP(prof_id, j), raw_swap);

		/* write the appropriate inset register set */
		wr32(hw, GLQF_FDINSET(prof_id, j), raw_in);

		ice_debug(hw, ICE_DBG_INIT, "inset wr(%d, %d): %x = %08x\n",
			  prof_id, j, GLQF_FDINSET(prof_id, j), raw_in);
	}

	/* initially clear the mask select for this profile */
	ice_update_fd_mask(hw, prof_id, 0);

	return 0;
}

/**
 * ice_add_prof - add profile
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @id: profile tracking ID
 * @ptypes: array of bitmaps indicating ptypes (ICE_FLOW_PTYPE_MAX bits)
 * @es: extraction sequence (length of array is determined by the block)
 *
 * This function registers a profile, which matches a set of PTGs with a
 * particular extraction sequence. While the hardware profile is allocated
 * it will not be written until the first call to ice_add_flow that specifies
 * the ID value used here.
 */
enum ice_status
ice_add_prof(struct ice_hw *hw, enum ice_block blk, u64 id, u8 ptypes[],
	     struct ice_fv_word *es)
{
	u32 bytes = DIV_ROUND_UP(ICE_FLOW_PTYPE_MAX, BITS_PER_BYTE);
	DECLARE_BITMAP(ptgs_used, ICE_XLT1_CNT);
	struct ice_prof_map *prof;
	enum ice_status status;
	u8 byte = 0;
	u8 prof_id;

	bitmap_zero(ptgs_used, ICE_XLT1_CNT);

	mutex_lock(&hw->blk[blk].es.prof_map_lock);

	/* search for existing profile */
	status = ice_find_prof_id(hw, blk, es, &prof_id);
	if (status) {
		/* allocate profile ID */
		status = ice_alloc_prof_id(hw, blk, &prof_id);
		if (status)
			goto err_ice_add_prof;
		if (blk == ICE_BLK_FD) {
			/* For Flow Director block, the extraction sequence may
			 * need to be altered in the case where there are paired
			 * fields that have no match. This is necessary because
			 * for Flow Director, src and dest fields need to paired
			 * for filter programming and these values are swapped
			 * during Tx.
			 */
			status = ice_update_fd_swap(hw, prof_id, es);
			if (status)
				goto err_ice_add_prof;
		}

		/* and write new es */
		ice_write_es(hw, blk, prof_id, es);
	}

	ice_prof_inc_ref(hw, blk, prof_id);

	/* add profile info */
	prof = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*prof), GFP_KERNEL);
	if (!prof) {
		status = ICE_ERR_NO_MEMORY;
		goto err_ice_add_prof;
	}

	prof->profile_cookie = id;
	prof->prof_id = prof_id;
	prof->ptg_cnt = 0;
	prof->context = 0;

	/* build list of ptgs */
	while (bytes && prof->ptg_cnt < ICE_MAX_PTG_PER_PROFILE) {
		u8 bit;

		if (!ptypes[byte]) {
			bytes--;
			byte++;
			continue;
		}

		/* Examine 8 bits per byte */
		for_each_set_bit(bit, (unsigned long *)&ptypes[byte],
				 BITS_PER_BYTE) {
			u16 ptype;
			u8 ptg;
			u8 m;

			ptype = byte * BITS_PER_BYTE + bit;

			/* The package should place all ptypes in a non-zero
			 * PTG, so the following call should never fail.
			 */
			if (ice_ptg_find_ptype(hw, blk, ptype, &ptg))
				continue;

			/* If PTG is already added, skip and continue */
			if (test_bit(ptg, ptgs_used))
				continue;

			set_bit(ptg, ptgs_used);
			prof->ptg[prof->ptg_cnt] = ptg;

			if (++prof->ptg_cnt >= ICE_MAX_PTG_PER_PROFILE)
				break;

			/* nothing left in byte, then exit */
			m = ~(u8)((1 << (bit + 1)) - 1);
			if (!(ptypes[byte] & m))
				break;
		}

		bytes--;
		byte++;
	}

	list_add(&prof->list, &hw->blk[blk].es.prof_map);
	status = 0;

err_ice_add_prof:
	mutex_unlock(&hw->blk[blk].es.prof_map_lock);
	return status;
}

/**
 * ice_search_prof_id_low - Search for a profile tracking ID low level
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @id: profile tracking ID
 *
 * This will search for a profile tracking ID which was previously added. This
 * version assumes that the caller has already acquired the prof map lock.
 */
static struct ice_prof_map *
ice_search_prof_id_low(struct ice_hw *hw, enum ice_block blk, u64 id)
{
	struct ice_prof_map *entry = NULL;
	struct ice_prof_map *map;

	list_for_each_entry(map, &hw->blk[blk].es.prof_map, list)
		if (map->profile_cookie == id) {
			entry = map;
			break;
		}

	return entry;
}

/**
 * ice_search_prof_id - Search for a profile tracking ID
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @id: profile tracking ID
 *
 * This will search for a profile tracking ID which was previously added.
 */
static struct ice_prof_map *
ice_search_prof_id(struct ice_hw *hw, enum ice_block blk, u64 id)
{
	struct ice_prof_map *entry;

	mutex_lock(&hw->blk[blk].es.prof_map_lock);
	entry = ice_search_prof_id_low(hw, blk, id);
	mutex_unlock(&hw->blk[blk].es.prof_map_lock);

	return entry;
}

/**
 * ice_vsig_prof_id_count - count profiles in a VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsig: VSIG to remove the profile from
 */
static u16
ice_vsig_prof_id_count(struct ice_hw *hw, enum ice_block blk, u16 vsig)
{
	u16 idx = vsig & ICE_VSIG_IDX_M, count = 0;
	struct ice_vsig_prof *p;

	list_for_each_entry(p, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
			    list)
		count++;

	return count;
}

/**
 * ice_rel_tcam_idx - release a TCAM index
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @idx: the index to release
 */
static enum ice_status
ice_rel_tcam_idx(struct ice_hw *hw, enum ice_block blk, u16 idx)
{
	/* Masks to invoke a never match entry */
	u8 vl_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	u8 dc_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFE, 0xFF, 0xFF, 0xFF, 0xFF };
	u8 nm_msk[ICE_TCAM_KEY_VAL_SZ] = { 0x01, 0x00, 0x00, 0x00, 0x00 };
	enum ice_status status;

	/* write the TCAM entry */
	status = ice_tcam_write_entry(hw, blk, idx, 0, 0, 0, 0, 0, vl_msk,
				      dc_msk, nm_msk);
	if (status)
		return status;

	/* release the TCAM entry */
	status = ice_free_tcam_ent(hw, blk, idx);

	return status;
}

/**
 * ice_rem_prof_id - remove one profile from a VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @prof: pointer to profile structure to remove
 */
static enum ice_status
ice_rem_prof_id(struct ice_hw *hw, enum ice_block blk,
		struct ice_vsig_prof *prof)
{
	enum ice_status status;
	u16 i;

	for (i = 0; i < prof->tcam_count; i++)
		if (prof->tcam[i].in_use) {
			prof->tcam[i].in_use = false;
			status = ice_rel_tcam_idx(hw, blk,
						  prof->tcam[i].tcam_idx);
			if (status)
				return ICE_ERR_HW_TABLE;
		}

	return 0;
}

/**
 * ice_rem_vsig - remove VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsig: the VSIG to remove
 * @chg: the change list
 */
static enum ice_status
ice_rem_vsig(struct ice_hw *hw, enum ice_block blk, u16 vsig,
	     struct list_head *chg)
{
	u16 idx = vsig & ICE_VSIG_IDX_M;
	struct ice_vsig_vsi *vsi_cur;
	struct ice_vsig_prof *d, *t;
	enum ice_status status;

	/* remove TCAM entries */
	list_for_each_entry_safe(d, t,
				 &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
				 list) {
		status = ice_rem_prof_id(hw, blk, d);
		if (status)
			return status;

		list_del(&d->list);
		devm_kfree(ice_hw_to_dev(hw), d);
	}

	/* Move all VSIS associated with this VSIG to the default VSIG */
	vsi_cur = hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	/* If the VSIG has at least 1 VSI then iterate through the list
	 * and remove the VSIs before deleting the group.
	 */
	if (vsi_cur)
		do {
			struct ice_vsig_vsi *tmp = vsi_cur->next_vsi;
			struct ice_chs_chg *p;

			p = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*p),
					 GFP_KERNEL);
			if (!p)
				return ICE_ERR_NO_MEMORY;

			p->type = ICE_VSIG_REM;
			p->orig_vsig = vsig;
			p->vsig = ICE_DEFAULT_VSIG;
			p->vsi = vsi_cur - hw->blk[blk].xlt2.vsis;

			list_add(&p->list_entry, chg);

			vsi_cur = tmp;
		} while (vsi_cur);

	return ice_vsig_free(hw, blk, vsig);
}

/**
 * ice_rem_prof_id_vsig - remove a specific profile from a VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsig: VSIG to remove the profile from
 * @hdl: profile handle indicating which profile to remove
 * @chg: list to receive a record of changes
 */
static enum ice_status
ice_rem_prof_id_vsig(struct ice_hw *hw, enum ice_block blk, u16 vsig, u64 hdl,
		     struct list_head *chg)
{
	u16 idx = vsig & ICE_VSIG_IDX_M;
	struct ice_vsig_prof *p, *t;
	enum ice_status status;

	list_for_each_entry_safe(p, t,
				 &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
				 list)
		if (p->profile_cookie == hdl) {
			if (ice_vsig_prof_id_count(hw, blk, vsig) == 1)
				/* this is the last profile, remove the VSIG */
				return ice_rem_vsig(hw, blk, vsig, chg);

			status = ice_rem_prof_id(hw, blk, p);
			if (!status) {
				list_del(&p->list);
				devm_kfree(ice_hw_to_dev(hw), p);
			}
			return status;
		}

	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_rem_flow_all - remove all flows with a particular profile
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @id: profile tracking ID
 */
static enum ice_status
ice_rem_flow_all(struct ice_hw *hw, enum ice_block blk, u64 id)
{
	struct ice_chs_chg *del, *tmp;
	enum ice_status status;
	struct list_head chg;
	u16 i;

	INIT_LIST_HEAD(&chg);

	for (i = 1; i < ICE_MAX_VSIGS; i++)
		if (hw->blk[blk].xlt2.vsig_tbl[i].in_use) {
			if (ice_has_prof_vsig(hw, blk, i, id)) {
				status = ice_rem_prof_id_vsig(hw, blk, i, id,
							      &chg);
				if (status)
					goto err_ice_rem_flow_all;
			}
		}

	status = ice_upd_prof_hw(hw, blk, &chg);

err_ice_rem_flow_all:
	list_for_each_entry_safe(del, tmp, &chg, list_entry) {
		list_del(&del->list_entry);
		devm_kfree(ice_hw_to_dev(hw), del);
	}

	return status;
}

/**
 * ice_rem_prof - remove profile
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @id: profile tracking ID
 *
 * This will remove the profile specified by the ID parameter, which was
 * previously created through ice_add_prof. If any existing entries
 * are associated with this profile, they will be removed as well.
 */
enum ice_status ice_rem_prof(struct ice_hw *hw, enum ice_block blk, u64 id)
{
	struct ice_prof_map *pmap;
	enum ice_status status;

	mutex_lock(&hw->blk[blk].es.prof_map_lock);

	pmap = ice_search_prof_id_low(hw, blk, id);
	if (!pmap) {
		status = ICE_ERR_DOES_NOT_EXIST;
		goto err_ice_rem_prof;
	}

	/* remove all flows with this profile */
	status = ice_rem_flow_all(hw, blk, pmap->profile_cookie);
	if (status)
		goto err_ice_rem_prof;

	/* dereference profile, and possibly remove */
	ice_prof_dec_ref(hw, blk, pmap->prof_id);

	list_del(&pmap->list);
	devm_kfree(ice_hw_to_dev(hw), pmap);

err_ice_rem_prof:
	mutex_unlock(&hw->blk[blk].es.prof_map_lock);
	return status;
}

/**
 * ice_get_prof - get profile
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @hdl: profile handle
 * @chg: change list
 */
static enum ice_status
ice_get_prof(struct ice_hw *hw, enum ice_block blk, u64 hdl,
	     struct list_head *chg)
{
	struct ice_prof_map *map;
	struct ice_chs_chg *p;
	u16 i;

	/* Get the details on the profile specified by the handle ID */
	map = ice_search_prof_id(hw, blk, hdl);
	if (!map)
		return ICE_ERR_DOES_NOT_EXIST;

	for (i = 0; i < map->ptg_cnt; i++)
		if (!hw->blk[blk].es.written[map->prof_id]) {
			/* add ES to change list */
			p = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*p),
					 GFP_KERNEL);
			if (!p)
				goto err_ice_get_prof;

			p->type = ICE_PTG_ES_ADD;
			p->ptype = 0;
			p->ptg = map->ptg[i];
			p->add_ptg = 0;

			p->add_prof = 1;
			p->prof_id = map->prof_id;

			hw->blk[blk].es.written[map->prof_id] = true;

			list_add(&p->list_entry, chg);
		}

	return 0;

err_ice_get_prof:
	/* let caller clean up the change list */
	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_get_profs_vsig - get a copy of the list of profiles from a VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsig: VSIG from which to copy the list
 * @lst: output list
 *
 * This routine makes a copy of the list of profiles in the specified VSIG.
 */
static enum ice_status
ice_get_profs_vsig(struct ice_hw *hw, enum ice_block blk, u16 vsig,
		   struct list_head *lst)
{
	struct ice_vsig_prof *ent1, *ent2;
	u16 idx = vsig & ICE_VSIG_IDX_M;

	list_for_each_entry(ent1, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
			    list) {
		struct ice_vsig_prof *p;

		/* copy to the input list */
		p = devm_kmemdup(ice_hw_to_dev(hw), ent1, sizeof(*p),
				 GFP_KERNEL);
		if (!p)
			goto err_ice_get_profs_vsig;

		list_add_tail(&p->list, lst);
	}

	return 0;

err_ice_get_profs_vsig:
	list_for_each_entry_safe(ent1, ent2, lst, list) {
		list_del(&ent1->list);
		devm_kfree(ice_hw_to_dev(hw), ent1);
	}

	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_add_prof_to_lst - add profile entry to a list
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @lst: the list to be added to
 * @hdl: profile handle of entry to add
 */
static enum ice_status
ice_add_prof_to_lst(struct ice_hw *hw, enum ice_block blk,
		    struct list_head *lst, u64 hdl)
{
	struct ice_prof_map *map;
	struct ice_vsig_prof *p;
	u16 i;

	map = ice_search_prof_id(hw, blk, hdl);
	if (!map)
		return ICE_ERR_DOES_NOT_EXIST;

	p = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*p), GFP_KERNEL);
	if (!p)
		return ICE_ERR_NO_MEMORY;

	p->profile_cookie = map->profile_cookie;
	p->prof_id = map->prof_id;
	p->tcam_count = map->ptg_cnt;

	for (i = 0; i < map->ptg_cnt; i++) {
		p->tcam[i].prof_id = map->prof_id;
		p->tcam[i].tcam_idx = ICE_INVALID_TCAM;
		p->tcam[i].ptg = map->ptg[i];
	}

	list_add(&p->list, lst);

	return 0;
}

/**
 * ice_move_vsi - move VSI to another VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsi: the VSI to move
 * @vsig: the VSIG to move the VSI to
 * @chg: the change list
 */
static enum ice_status
ice_move_vsi(struct ice_hw *hw, enum ice_block blk, u16 vsi, u16 vsig,
	     struct list_head *chg)
{
	enum ice_status status;
	struct ice_chs_chg *p;
	u16 orig_vsig;

	p = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*p), GFP_KERNEL);
	if (!p)
		return ICE_ERR_NO_MEMORY;

	status = ice_vsig_find_vsi(hw, blk, vsi, &orig_vsig);
	if (!status)
		status = ice_vsig_add_mv_vsi(hw, blk, vsi, vsig);

	if (status) {
		devm_kfree(ice_hw_to_dev(hw), p);
		return status;
	}

	p->type = ICE_VSI_MOVE;
	p->vsi = vsi;
	p->orig_vsig = orig_vsig;
	p->vsig = vsig;

	list_add(&p->list_entry, chg);

	return 0;
}

/**
 * ice_rem_chg_tcam_ent - remove a specific TCAM entry from change list
 * @hw: pointer to the HW struct
 * @idx: the index of the TCAM entry to remove
 * @chg: the list of change structures to search
 */
static void
ice_rem_chg_tcam_ent(struct ice_hw *hw, u16 idx, struct list_head *chg)
{
	struct ice_chs_chg *pos, *tmp;

	list_for_each_entry_safe(tmp, pos, chg, list_entry)
		if (tmp->type == ICE_TCAM_ADD && tmp->tcam_idx == idx) {
			list_del(&tmp->list_entry);
			devm_kfree(ice_hw_to_dev(hw), tmp);
		}
}

/**
 * ice_prof_tcam_ena_dis - add enable or disable TCAM change
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @enable: true to enable, false to disable
 * @vsig: the VSIG of the TCAM entry
 * @tcam: pointer the TCAM info structure of the TCAM to disable
 * @chg: the change list
 *
 * This function appends an enable or disable TCAM entry in the change log
 */
static enum ice_status
ice_prof_tcam_ena_dis(struct ice_hw *hw, enum ice_block blk, bool enable,
		      u16 vsig, struct ice_tcam_inf *tcam,
		      struct list_head *chg)
{
	enum ice_status status;
	struct ice_chs_chg *p;

	u8 vl_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	u8 dc_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0x00, 0x00, 0x00 };
	u8 nm_msk[ICE_TCAM_KEY_VAL_SZ] = { 0x00, 0x00, 0x00, 0x00, 0x00 };

	/* if disabling, free the TCAM */
	if (!enable) {
		status = ice_rel_tcam_idx(hw, blk, tcam->tcam_idx);

		/* if we have already created a change for this TCAM entry, then
		 * we need to remove that entry, in order to prevent writing to
		 * a TCAM entry we no longer will have ownership of.
		 */
		ice_rem_chg_tcam_ent(hw, tcam->tcam_idx, chg);
		tcam->tcam_idx = 0;
		tcam->in_use = 0;
		return status;
	}

	/* for re-enabling, reallocate a TCAM */
	status = ice_alloc_tcam_ent(hw, blk, &tcam->tcam_idx);
	if (status)
		return status;

	/* add TCAM to change list */
	p = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*p), GFP_KERNEL);
	if (!p)
		return ICE_ERR_NO_MEMORY;

	status = ice_tcam_write_entry(hw, blk, tcam->tcam_idx, tcam->prof_id,
				      tcam->ptg, vsig, 0, 0, vl_msk, dc_msk,
				      nm_msk);
	if (status)
		goto err_ice_prof_tcam_ena_dis;

	tcam->in_use = 1;

	p->type = ICE_TCAM_ADD;
	p->add_tcam_idx = true;
	p->prof_id = tcam->prof_id;
	p->ptg = tcam->ptg;
	p->vsig = 0;
	p->tcam_idx = tcam->tcam_idx;

	/* log change */
	list_add(&p->list_entry, chg);

	return 0;

err_ice_prof_tcam_ena_dis:
	devm_kfree(ice_hw_to_dev(hw), p);
	return status;
}

/**
 * ice_adj_prof_priorities - adjust profile based on priorities
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsig: the VSIG for which to adjust profile priorities
 * @chg: the change list
 */
static enum ice_status
ice_adj_prof_priorities(struct ice_hw *hw, enum ice_block blk, u16 vsig,
			struct list_head *chg)
{
	DECLARE_BITMAP(ptgs_used, ICE_XLT1_CNT);
	struct ice_vsig_prof *t;
	enum ice_status status;
	u16 idx;

	bitmap_zero(ptgs_used, ICE_XLT1_CNT);
	idx = vsig & ICE_VSIG_IDX_M;

	/* Priority is based on the order in which the profiles are added. The
	 * newest added profile has highest priority and the oldest added
	 * profile has the lowest priority. Since the profile property list for
	 * a VSIG is sorted from newest to oldest, this code traverses the list
	 * in order and enables the first of each PTG that it finds (that is not
	 * already enabled); it also disables any duplicate PTGs that it finds
	 * in the older profiles (that are currently enabled).
	 */

	list_for_each_entry(t, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
			    list) {
		u16 i;

		for (i = 0; i < t->tcam_count; i++) {
			/* Scan the priorities from newest to oldest.
			 * Make sure that the newest profiles take priority.
			 */
			if (test_bit(t->tcam[i].ptg, ptgs_used) &&
			    t->tcam[i].in_use) {
				/* need to mark this PTG as never match, as it
				 * was already in use and therefore duplicate
				 * (and lower priority)
				 */
				status = ice_prof_tcam_ena_dis(hw, blk, false,
							       vsig,
							       &t->tcam[i],
							       chg);
				if (status)
					return status;
			} else if (!test_bit(t->tcam[i].ptg, ptgs_used) &&
				   !t->tcam[i].in_use) {
				/* need to enable this PTG, as it in not in use
				 * and not enabled (highest priority)
				 */
				status = ice_prof_tcam_ena_dis(hw, blk, true,
							       vsig,
							       &t->tcam[i],
							       chg);
				if (status)
					return status;
			}

			/* keep track of used ptgs */
			set_bit(t->tcam[i].ptg, ptgs_used);
		}
	}

	return 0;
}

/**
 * ice_add_prof_id_vsig - add profile to VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsig: the VSIG to which this profile is to be added
 * @hdl: the profile handle indicating the profile to add
 * @rev: true to add entries to the end of the list
 * @chg: the change list
 */
static enum ice_status
ice_add_prof_id_vsig(struct ice_hw *hw, enum ice_block blk, u16 vsig, u64 hdl,
		     bool rev, struct list_head *chg)
{
	/* Masks that ignore flags */
	u8 vl_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	u8 dc_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0x00, 0x00, 0x00 };
	u8 nm_msk[ICE_TCAM_KEY_VAL_SZ] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
	struct ice_prof_map *map;
	struct ice_vsig_prof *t;
	struct ice_chs_chg *p;
	u16 vsig_idx, i;

	/* Get the details on the profile specified by the handle ID */
	map = ice_search_prof_id(hw, blk, hdl);
	if (!map)
		return ICE_ERR_DOES_NOT_EXIST;

	/* Error, if this VSIG already has this profile */
	if (ice_has_prof_vsig(hw, blk, vsig, hdl))
		return ICE_ERR_ALREADY_EXISTS;

	/* new VSIG profile structure */
	t = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*t), GFP_KERNEL);
	if (!t)
		return ICE_ERR_NO_MEMORY;

	t->profile_cookie = map->profile_cookie;
	t->prof_id = map->prof_id;
	t->tcam_count = map->ptg_cnt;

	/* create TCAM entries */
	for (i = 0; i < map->ptg_cnt; i++) {
		enum ice_status status;
		u16 tcam_idx;

		/* add TCAM to change list */
		p = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*p), GFP_KERNEL);
		if (!p)
			goto err_ice_add_prof_id_vsig;

		/* allocate the TCAM entry index */
		status = ice_alloc_tcam_ent(hw, blk, &tcam_idx);
		if (status) {
			devm_kfree(ice_hw_to_dev(hw), p);
			goto err_ice_add_prof_id_vsig;
		}

		t->tcam[i].ptg = map->ptg[i];
		t->tcam[i].prof_id = map->prof_id;
		t->tcam[i].tcam_idx = tcam_idx;
		t->tcam[i].in_use = true;

		p->type = ICE_TCAM_ADD;
		p->add_tcam_idx = true;
		p->prof_id = t->tcam[i].prof_id;
		p->ptg = t->tcam[i].ptg;
		p->vsig = vsig;
		p->tcam_idx = t->tcam[i].tcam_idx;

		/* write the TCAM entry */
		status = ice_tcam_write_entry(hw, blk, t->tcam[i].tcam_idx,
					      t->tcam[i].prof_id,
					      t->tcam[i].ptg, vsig, 0, 0,
					      vl_msk, dc_msk, nm_msk);
		if (status) {
			devm_kfree(ice_hw_to_dev(hw), p);
			goto err_ice_add_prof_id_vsig;
		}

		/* log change */
		list_add(&p->list_entry, chg);
	}

	/* add profile to VSIG */
	vsig_idx = vsig & ICE_VSIG_IDX_M;
	if (rev)
		list_add_tail(&t->list,
			      &hw->blk[blk].xlt2.vsig_tbl[vsig_idx].prop_lst);
	else
		list_add(&t->list,
			 &hw->blk[blk].xlt2.vsig_tbl[vsig_idx].prop_lst);

	return 0;

err_ice_add_prof_id_vsig:
	/* let caller clean up the change list */
	devm_kfree(ice_hw_to_dev(hw), t);
	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_create_prof_id_vsig - add a new VSIG with a single profile
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsi: the initial VSI that will be in VSIG
 * @hdl: the profile handle of the profile that will be added to the VSIG
 * @chg: the change list
 */
static enum ice_status
ice_create_prof_id_vsig(struct ice_hw *hw, enum ice_block blk, u16 vsi, u64 hdl,
			struct list_head *chg)
{
	enum ice_status status;
	struct ice_chs_chg *p;
	u16 new_vsig;

	p = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*p), GFP_KERNEL);
	if (!p)
		return ICE_ERR_NO_MEMORY;

	new_vsig = ice_vsig_alloc(hw, blk);
	if (!new_vsig) {
		status = ICE_ERR_HW_TABLE;
		goto err_ice_create_prof_id_vsig;
	}

	status = ice_move_vsi(hw, blk, vsi, new_vsig, chg);
	if (status)
		goto err_ice_create_prof_id_vsig;

	status = ice_add_prof_id_vsig(hw, blk, new_vsig, hdl, false, chg);
	if (status)
		goto err_ice_create_prof_id_vsig;

	p->type = ICE_VSIG_ADD;
	p->vsi = vsi;
	p->orig_vsig = ICE_DEFAULT_VSIG;
	p->vsig = new_vsig;

	list_add(&p->list_entry, chg);

	return 0;

err_ice_create_prof_id_vsig:
	/* let caller clean up the change list */
	devm_kfree(ice_hw_to_dev(hw), p);
	return status;
}

/**
 * ice_create_vsig_from_lst - create a new VSIG with a list of profiles
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsi: the initial VSI that will be in VSIG
 * @lst: the list of profile that will be added to the VSIG
 * @new_vsig: return of new VSIG
 * @chg: the change list
 */
static enum ice_status
ice_create_vsig_from_lst(struct ice_hw *hw, enum ice_block blk, u16 vsi,
			 struct list_head *lst, u16 *new_vsig,
			 struct list_head *chg)
{
	struct ice_vsig_prof *t;
	enum ice_status status;
	u16 vsig;

	vsig = ice_vsig_alloc(hw, blk);
	if (!vsig)
		return ICE_ERR_HW_TABLE;

	status = ice_move_vsi(hw, blk, vsi, vsig, chg);
	if (status)
		return status;

	list_for_each_entry(t, lst, list) {
		/* Reverse the order here since we are copying the list */
		status = ice_add_prof_id_vsig(hw, blk, vsig, t->profile_cookie,
					      true, chg);
		if (status)
			return status;
	}

	*new_vsig = vsig;

	return 0;
}

/**
 * ice_find_prof_vsig - find a VSIG with a specific profile handle
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @hdl: the profile handle of the profile to search for
 * @vsig: returns the VSIG with the matching profile
 */
static bool
ice_find_prof_vsig(struct ice_hw *hw, enum ice_block blk, u64 hdl, u16 *vsig)
{
	struct ice_vsig_prof *t;
	enum ice_status status;
	struct list_head lst;

	INIT_LIST_HEAD(&lst);

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return false;

	t->profile_cookie = hdl;
	list_add(&t->list, &lst);

	status = ice_find_dup_props_vsig(hw, blk, &lst, vsig);

	list_del(&t->list);
	kfree(t);

	return !status;
}

/**
 * ice_add_prof_id_flow - add profile flow
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsi: the VSI to enable with the profile specified by ID
 * @hdl: profile handle
 *
 * Calling this function will update the hardware tables to enable the
 * profile indicated by the ID parameter for the VSIs specified in the VSI
 * array. Once successfully called, the flow will be enabled.
 */
enum ice_status
ice_add_prof_id_flow(struct ice_hw *hw, enum ice_block blk, u16 vsi, u64 hdl)
{
	struct ice_vsig_prof *tmp1, *del1;
	struct ice_chs_chg *tmp, *del;
	struct list_head union_lst;
	enum ice_status status;
	struct list_head chg;
	u16 vsig;

	INIT_LIST_HEAD(&union_lst);
	INIT_LIST_HEAD(&chg);

	/* Get profile */
	status = ice_get_prof(hw, blk, hdl, &chg);
	if (status)
		return status;

	/* determine if VSI is already part of a VSIG */
	status = ice_vsig_find_vsi(hw, blk, vsi, &vsig);
	if (!status && vsig) {
		bool only_vsi;
		u16 or_vsig;
		u16 ref;

		/* found in VSIG */
		or_vsig = vsig;

		/* make sure that there is no overlap/conflict between the new
		 * characteristics and the existing ones; we don't support that
		 * scenario
		 */
		if (ice_has_prof_vsig(hw, blk, vsig, hdl)) {
			status = ICE_ERR_ALREADY_EXISTS;
			goto err_ice_add_prof_id_flow;
		}

		/* last VSI in the VSIG? */
		status = ice_vsig_get_ref(hw, blk, vsig, &ref);
		if (status)
			goto err_ice_add_prof_id_flow;
		only_vsi = (ref == 1);

		/* create a union of the current profiles and the one being
		 * added
		 */
		status = ice_get_profs_vsig(hw, blk, vsig, &union_lst);
		if (status)
			goto err_ice_add_prof_id_flow;

		status = ice_add_prof_to_lst(hw, blk, &union_lst, hdl);
		if (status)
			goto err_ice_add_prof_id_flow;

		/* search for an existing VSIG with an exact charc match */
		status = ice_find_dup_props_vsig(hw, blk, &union_lst, &vsig);
		if (!status) {
			/* move VSI to the VSIG that matches */
			status = ice_move_vsi(hw, blk, vsi, vsig, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;

			/* VSI has been moved out of or_vsig. If the or_vsig had
			 * only that VSI it is now empty and can be removed.
			 */
			if (only_vsi) {
				status = ice_rem_vsig(hw, blk, or_vsig, &chg);
				if (status)
					goto err_ice_add_prof_id_flow;
			}
		} else if (only_vsi) {
			/* If the original VSIG only contains one VSI, then it
			 * will be the requesting VSI. In this case the VSI is
			 * not sharing entries and we can simply add the new
			 * profile to the VSIG.
			 */
			status = ice_add_prof_id_vsig(hw, blk, vsig, hdl, false,
						      &chg);
			if (status)
				goto err_ice_add_prof_id_flow;

			/* Adjust priorities */
			status = ice_adj_prof_priorities(hw, blk, vsig, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;
		} else {
			/* No match, so we need a new VSIG */
			status = ice_create_vsig_from_lst(hw, blk, vsi,
							  &union_lst, &vsig,
							  &chg);
			if (status)
				goto err_ice_add_prof_id_flow;

			/* Adjust priorities */
			status = ice_adj_prof_priorities(hw, blk, vsig, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;
		}
	} else {
		/* need to find or add a VSIG */
		/* search for an existing VSIG with an exact charc match */
		if (ice_find_prof_vsig(hw, blk, hdl, &vsig)) {
			/* found an exact match */
			/* add or move VSI to the VSIG that matches */
			status = ice_move_vsi(hw, blk, vsi, vsig, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;
		} else {
			/* we did not find an exact match */
			/* we need to add a VSIG */
			status = ice_create_prof_id_vsig(hw, blk, vsi, hdl,
							 &chg);
			if (status)
				goto err_ice_add_prof_id_flow;
		}
	}

	/* update hardware */
	if (!status)
		status = ice_upd_prof_hw(hw, blk, &chg);

err_ice_add_prof_id_flow:
	list_for_each_entry_safe(del, tmp, &chg, list_entry) {
		list_del(&del->list_entry);
		devm_kfree(ice_hw_to_dev(hw), del);
	}

	list_for_each_entry_safe(del1, tmp1, &union_lst, list) {
		list_del(&del1->list);
		devm_kfree(ice_hw_to_dev(hw), del1);
	}

	return status;
}

/**
 * ice_rem_prof_from_list - remove a profile from list
 * @hw: pointer to the HW struct
 * @lst: list to remove the profile from
 * @hdl: the profile handle indicating the profile to remove
 */
static enum ice_status
ice_rem_prof_from_list(struct ice_hw *hw, struct list_head *lst, u64 hdl)
{
	struct ice_vsig_prof *ent, *tmp;

	list_for_each_entry_safe(ent, tmp, lst, list)
		if (ent->profile_cookie == hdl) {
			list_del(&ent->list);
			devm_kfree(ice_hw_to_dev(hw), ent);
			return 0;
		}

	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_rem_prof_id_flow - remove flow
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsi: the VSI from which to remove the profile specified by ID
 * @hdl: profile tracking handle
 *
 * Calling this function will update the hardware tables to remove the
 * profile indicated by the ID parameter for the VSIs specified in the VSI
 * array. Once successfully called, the flow will be disabled.
 */
enum ice_status
ice_rem_prof_id_flow(struct ice_hw *hw, enum ice_block blk, u16 vsi, u64 hdl)
{
	struct ice_vsig_prof *tmp1, *del1;
	struct ice_chs_chg *tmp, *del;
	struct list_head chg, copy;
	enum ice_status status;
	u16 vsig;

	INIT_LIST_HEAD(&copy);
	INIT_LIST_HEAD(&chg);

	/* determine if VSI is already part of a VSIG */
	status = ice_vsig_find_vsi(hw, blk, vsi, &vsig);
	if (!status && vsig) {
		bool last_profile;
		bool only_vsi;
		u16 ref;

		/* found in VSIG */
		last_profile = ice_vsig_prof_id_count(hw, blk, vsig) == 1;
		status = ice_vsig_get_ref(hw, blk, vsig, &ref);
		if (status)
			goto err_ice_rem_prof_id_flow;
		only_vsi = (ref == 1);

		if (only_vsi) {
			/* If the original VSIG only contains one reference,
			 * which will be the requesting VSI, then the VSI is not
			 * sharing entries and we can simply remove the specific
			 * characteristics from the VSIG.
			 */

			if (last_profile) {
				/* If there are no profiles left for this VSIG,
				 * then simply remove the the VSIG.
				 */
				status = ice_rem_vsig(hw, blk, vsig, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;
			} else {
				status = ice_rem_prof_id_vsig(hw, blk, vsig,
							      hdl, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;

				/* Adjust priorities */
				status = ice_adj_prof_priorities(hw, blk, vsig,
								 &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;
			}

		} else {
			/* Make a copy of the VSIG's list of Profiles */
			status = ice_get_profs_vsig(hw, blk, vsig, &copy);
			if (status)
				goto err_ice_rem_prof_id_flow;

			/* Remove specified profile entry from the list */
			status = ice_rem_prof_from_list(hw, &copy, hdl);
			if (status)
				goto err_ice_rem_prof_id_flow;

			if (list_empty(&copy)) {
				status = ice_move_vsi(hw, blk, vsi,
						      ICE_DEFAULT_VSIG, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;

			} else if (!ice_find_dup_props_vsig(hw, blk, &copy,
							    &vsig)) {
				/* found an exact match */
				/* add or move VSI to the VSIG that matches */
				/* Search for a VSIG with a matching profile
				 * list
				 */

				/* Found match, move VSI to the matching VSIG */
				status = ice_move_vsi(hw, blk, vsi, vsig, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;
			} else {
				/* since no existing VSIG supports this
				 * characteristic pattern, we need to create a
				 * new VSIG and TCAM entries
				 */
				status = ice_create_vsig_from_lst(hw, blk, vsi,
								  &copy, &vsig,
								  &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;

				/* Adjust priorities */
				status = ice_adj_prof_priorities(hw, blk, vsig,
								 &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;
			}
		}
	} else {
		status = ICE_ERR_DOES_NOT_EXIST;
	}

	/* update hardware tables */
	if (!status)
		status = ice_upd_prof_hw(hw, blk, &chg);

err_ice_rem_prof_id_flow:
	list_for_each_entry_safe(del, tmp, &chg, list_entry) {
		list_del(&del->list_entry);
		devm_kfree(ice_hw_to_dev(hw), del);
	}

	list_for_each_entry_safe(del1, tmp1, &copy, list) {
		list_del(&del1->list);
		devm_kfree(ice_hw_to_dev(hw), del1);
	}

	return status;
}
