/*
 * WiMedia Logical Link Control Protocol (WLP)
 * Message construction and parsing
 *
 * Copyright (C) 2007 Intel Corporation
 * Reinette Chatre <reinette.chatre@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: docs
 */

#include <linux/wlp.h>
#include <linux/slab.h>

#include "wlp-internal.h"

static
const char *__wlp_assoc_frame[] = {
	[WLP_ASSOC_D1] = "WLP_ASSOC_D1",
	[WLP_ASSOC_D2] = "WLP_ASSOC_D2",
	[WLP_ASSOC_M1] = "WLP_ASSOC_M1",
	[WLP_ASSOC_M2] = "WLP_ASSOC_M2",
	[WLP_ASSOC_M3] = "WLP_ASSOC_M3",
	[WLP_ASSOC_M4] = "WLP_ASSOC_M4",
	[WLP_ASSOC_M5] = "WLP_ASSOC_M5",
	[WLP_ASSOC_M6] = "WLP_ASSOC_M6",
	[WLP_ASSOC_M7] = "WLP_ASSOC_M7",
	[WLP_ASSOC_M8] = "WLP_ASSOC_M8",
	[WLP_ASSOC_F0] = "WLP_ASSOC_F0",
	[WLP_ASSOC_E1] = "WLP_ASSOC_E1",
	[WLP_ASSOC_E2] = "WLP_ASSOC_E2",
	[WLP_ASSOC_C1] = "WLP_ASSOC_C1",
	[WLP_ASSOC_C2] = "WLP_ASSOC_C2",
	[WLP_ASSOC_C3] = "WLP_ASSOC_C3",
	[WLP_ASSOC_C4] = "WLP_ASSOC_C4",
};

static const char *wlp_assoc_frame_str(unsigned id)
{
	if (id >= ARRAY_SIZE(__wlp_assoc_frame))
		return "unknown association frame";
	return __wlp_assoc_frame[id];
}

static const char *__wlp_assc_error[] = {
	"none",
	"Authenticator Failure",
	"Rogue activity suspected",
	"Device busy",
	"Setup Locked",
	"Registrar not ready",
	"Invalid WSS selection",
	"Message timeout",
	"Enrollment session timeout",
	"Device password invalid",
	"Unsupported version",
	"Internal error",
	"Undefined error",
	"Numeric comparison failure",
	"Waiting for user input",
};

static const char *wlp_assc_error_str(unsigned id)
{
	if (id >= ARRAY_SIZE(__wlp_assc_error))
		return "unknown WLP association error";
	return __wlp_assc_error[id];
}

static inline void wlp_set_attr_hdr(struct wlp_attr_hdr *hdr, unsigned type,
				    size_t len)
{
	hdr->type = cpu_to_le16(type);
	hdr->length = cpu_to_le16(len);
}

/*
 * Populate fields of a constant sized attribute
 *
 * @returns: total size of attribute including size of new value
 *
 * We have two instances of this function (wlp_pset and wlp_set): one takes
 * the value as a parameter, the other takes a pointer to the value as
 * parameter. They thus only differ in how the value is assigned to the
 * attribute.
 *
 * We use sizeof(*attr) - sizeof(struct wlp_attr_hdr) instead of
 * sizeof(type) to be able to use this same code for the structures that
 * contain 8bit enum values and be able to deal with pointer types.
 */
#define wlp_set(type, type_code, name)					\
static size_t wlp_set_##name(struct wlp_attr_##name *attr, type value)	\
{									\
	wlp_set_attr_hdr(&attr->hdr, type_code,				\
			 sizeof(*attr) - sizeof(struct wlp_attr_hdr));	\
	attr->name = value;						\
	return sizeof(*attr);						\
}

#define wlp_pset(type, type_code, name)					\
static size_t wlp_set_##name(struct wlp_attr_##name *attr, type value)	\
{									\
	wlp_set_attr_hdr(&attr->hdr, type_code,				\
			 sizeof(*attr) - sizeof(struct wlp_attr_hdr));	\
	attr->name = *value;						\
	return sizeof(*attr);						\
}

/**
 * Populate fields of a variable attribute
 *
 * @returns: total size of attribute including size of new value
 *
 * Provided with a pointer to the memory area reserved for the
 * attribute structure, the field is populated with the value. The
 * reserved memory has to contain enough space for the value.
 */
#define wlp_vset(type, type_code, name)					\
static size_t wlp_set_##name(struct wlp_attr_##name *attr, type value,	\
				size_t len)				\
{									\
	wlp_set_attr_hdr(&attr->hdr, type_code, len);			\
	memcpy(attr->name, value, len);					\
	return sizeof(*attr) + len;					\
}

wlp_vset(char *, WLP_ATTR_DEV_NAME, dev_name)
wlp_vset(char *, WLP_ATTR_MANUF, manufacturer)
wlp_set(enum wlp_assoc_type, WLP_ATTR_MSG_TYPE, msg_type)
wlp_vset(char *, WLP_ATTR_MODEL_NAME, model_name)
wlp_vset(char *, WLP_ATTR_MODEL_NR, model_nr)
wlp_vset(char *, WLP_ATTR_SERIAL, serial)
wlp_vset(char *, WLP_ATTR_WSS_NAME, wss_name)
wlp_pset(struct wlp_uuid *, WLP_ATTR_UUID_E, uuid_e)
wlp_pset(struct wlp_uuid *, WLP_ATTR_UUID_R, uuid_r)
wlp_pset(struct wlp_uuid *, WLP_ATTR_WSSID, wssid)
wlp_pset(struct wlp_dev_type *, WLP_ATTR_PRI_DEV_TYPE, prim_dev_type)
/*wlp_pset(struct wlp_dev_type *, WLP_ATTR_SEC_DEV_TYPE, sec_dev_type)*/
wlp_set(u8, WLP_ATTR_WLP_VER, version)
wlp_set(enum wlp_assc_error, WLP_ATTR_WLP_ASSC_ERR, wlp_assc_err)
wlp_set(enum wlp_wss_sel_mthd, WLP_ATTR_WSS_SEL_MTHD, wss_sel_mthd)
wlp_set(u8, WLP_ATTR_ACC_ENRL, accept_enrl)
wlp_set(u8, WLP_ATTR_WSS_SEC_STAT, wss_sec_status)
wlp_pset(struct uwb_mac_addr *, WLP_ATTR_WSS_BCAST, wss_bcast)
wlp_pset(struct wlp_nonce *, WLP_ATTR_ENRL_NONCE, enonce)
wlp_pset(struct wlp_nonce *, WLP_ATTR_REG_NONCE, rnonce)
wlp_set(u8, WLP_ATTR_WSS_TAG, wss_tag)
wlp_pset(struct uwb_mac_addr *, WLP_ATTR_WSS_VIRT, wss_virt)

/**
 * Fill in the WSS information attributes
 *
 * We currently only support one WSS, and this is assumed in this function
 * that can populate only one WSS information attribute.
 */
static size_t wlp_set_wss_info(struct wlp_attr_wss_info *attr,
			       struct wlp_wss *wss)
{
	size_t datalen;
	void *ptr = attr->wss_info;
	size_t used = sizeof(*attr);

	datalen = sizeof(struct wlp_wss_info) + strlen(wss->name);
	wlp_set_attr_hdr(&attr->hdr, WLP_ATTR_WSS_INFO, datalen);
	used = wlp_set_wssid(ptr, &wss->wssid);
	used += wlp_set_wss_name(ptr + used, wss->name, strlen(wss->name));
	used += wlp_set_accept_enrl(ptr + used, wss->accept_enroll);
	used += wlp_set_wss_sec_status(ptr + used, wss->secure_status);
	used += wlp_set_wss_bcast(ptr + used, &wss->bcast);
	return sizeof(*attr) + used;
}

/**
 * Verify attribute header
 *
 * @hdr:     Pointer to attribute header that will be verified.
 * @type:    Expected attribute type.
 * @len:     Expected length of attribute value (excluding header).
 *
 * Most attribute values have a known length even when they do have a
 * length field. This knowledge can be used via this function to verify
 * that the length field matches the expected value.
 */
static int wlp_check_attr_hdr(struct wlp *wlp, struct wlp_attr_hdr *hdr,
		       enum wlp_attr_type type, unsigned len)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;

	if (le16_to_cpu(hdr->type) != type) {
		dev_err(dev, "WLP: unexpected header type. Expected "
			"%u, got %u.\n", type, le16_to_cpu(hdr->type));
		return -EINVAL;
	}
	if (le16_to_cpu(hdr->length) != len) {
		dev_err(dev, "WLP: unexpected length in header. Expected "
			"%u, got %u.\n", len, le16_to_cpu(hdr->length));
		return -EINVAL;
	}
	return 0;
}

/**
 * Check if header of WSS information attribute valid
 *
 * @returns: length of WSS attributes (value of length attribute field) if
 *             valid WSS information attribute found
 *           -ENODATA if no WSS information attribute found
 *           -EIO other error occured
 *
 * The WSS information attribute is optional. The function will be provided
 * with a pointer to data that could _potentially_ be a WSS information
 * attribute. If a valid WSS information attribute is found it will return
 * 0, if no WSS information attribute is found it will return -ENODATA, and
 * another error will be returned if it is a WSS information attribute, but
 * some parsing failure occured.
 */
static int wlp_check_wss_info_attr_hdr(struct wlp *wlp,
				       struct wlp_attr_hdr *hdr, size_t buflen)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	size_t len;
	int result = 0;

	if (buflen < sizeof(*hdr)) {
		dev_err(dev, "WLP: Not enough space in buffer to parse"
			" WSS information attribute header.\n");
		result = -EIO;
		goto out;
	}
	if (le16_to_cpu(hdr->type) != WLP_ATTR_WSS_INFO) {
		/* WSS information is optional */
		result = -ENODATA;
		goto out;
	}
	len = le16_to_cpu(hdr->length);
	if (buflen < sizeof(*hdr) + len) {
		dev_err(dev, "WLP: Not enough space in buffer to parse "
			"variable data. Got %d, expected %d.\n",
			(int)buflen, (int)(sizeof(*hdr) + len));
		result = -EIO;
		goto out;
	}
	result = len;
out:
	return result;
}


static ssize_t wlp_get_attribute(struct wlp *wlp, u16 type_code,
	struct wlp_attr_hdr *attr_hdr, void *value, ssize_t value_len,
	ssize_t buflen)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	ssize_t attr_len = sizeof(*attr_hdr) + value_len;
	if (buflen < 0)
		return -EINVAL;
	if (buflen < attr_len) {
		dev_err(dev, "WLP: Not enough space in buffer to parse"
			" attribute field. Need %d, received %zu\n",
			(int)attr_len, buflen);
		return -EIO;
	}
	if (wlp_check_attr_hdr(wlp, attr_hdr, type_code, value_len) < 0) {
		dev_err(dev, "WLP: Header verification failed. \n");
		return -EINVAL;
	}
	memcpy(value, (void *)attr_hdr + sizeof(*attr_hdr), value_len);
	return attr_len;
}

static ssize_t wlp_vget_attribute(struct wlp *wlp, u16 type_code,
	struct wlp_attr_hdr *attr_hdr, void *value, ssize_t max_value_len,
	ssize_t buflen)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	size_t len;
	if (buflen < 0)
		return -EINVAL;
	if (buflen < sizeof(*attr_hdr)) {
		dev_err(dev, "WLP: Not enough space in buffer to parse"
			" header.\n");
		return -EIO;
	}
	if (le16_to_cpu(attr_hdr->type) != type_code) {
		dev_err(dev, "WLP: Unexpected attribute type. Got %u, "
			"expected %u.\n", le16_to_cpu(attr_hdr->type),
			type_code);
		return -EINVAL;
	}
	len = le16_to_cpu(attr_hdr->length);
	if (len > max_value_len) {
		dev_err(dev, "WLP: Attribute larger than maximum "
			"allowed. Received %zu, max is %d.\n", len,
			(int)max_value_len);
		return -EFBIG;
	}
	if (buflen < sizeof(*attr_hdr) + len) {
		dev_err(dev, "WLP: Not enough space in buffer to parse "
			"variable data.\n");
		return -EIO;
	}
	memcpy(value, (void *)attr_hdr + sizeof(*attr_hdr), len);
	return sizeof(*attr_hdr) + len;
}

/**
 * Get value of attribute from fixed size attribute field.
 *
 * @attr:    Pointer to attribute field.
 * @value:   Pointer to variable in which attribute value will be placed.
 * @buflen:  Size of buffer in which attribute field (including header)
 *           can be found.
 * @returns: Amount of given buffer consumed by parsing for this attribute.
 *
 * The size and type of the value is known by the type of the attribute.
 */
#define wlp_get(type, type_code, name)					\
ssize_t wlp_get_##name(struct wlp *wlp, struct wlp_attr_##name *attr,	\
		      type *value, ssize_t buflen)			\
{									\
	return wlp_get_attribute(wlp, (type_code), &attr->hdr,		\
				 value, sizeof(*value), buflen);	\
}

#define wlp_get_sparse(type, type_code, name) \
	static wlp_get(type, type_code, name)

/**
 * Get value of attribute from variable sized attribute field.
 *
 * @max:     The maximum size of this attribute. This value is dictated by
 *           the maximum value from the WLP specification.
 *
 * @attr:    Pointer to attribute field.
 * @value:   Pointer to variable that will contain the value. The memory
 *           must already have been allocated for this value.
 * @buflen:  Size of buffer in which attribute field (including header)
 *           can be found.
 * @returns: Amount of given bufferconsumed by parsing for this attribute.
 */
#define wlp_vget(type_val, type_code, name, max)			\
static ssize_t wlp_get_##name(struct wlp *wlp,				\
			      struct wlp_attr_##name *attr,		\
			      type_val *value, ssize_t buflen)		\
{									\
	return wlp_vget_attribute(wlp, (type_code), &attr->hdr, 	\
			      value, (max), buflen);			\
}

wlp_get(u8, WLP_ATTR_WLP_VER, version)
wlp_get_sparse(enum wlp_wss_sel_mthd, WLP_ATTR_WSS_SEL_MTHD, wss_sel_mthd)
wlp_get_sparse(struct wlp_dev_type, WLP_ATTR_PRI_DEV_TYPE, prim_dev_type)
wlp_get_sparse(enum wlp_assc_error, WLP_ATTR_WLP_ASSC_ERR, wlp_assc_err)
wlp_get_sparse(struct wlp_uuid, WLP_ATTR_UUID_E, uuid_e)
wlp_get_sparse(struct wlp_uuid, WLP_ATTR_UUID_R, uuid_r)
wlp_get(struct wlp_uuid, WLP_ATTR_WSSID, wssid)
wlp_get_sparse(u8, WLP_ATTR_ACC_ENRL, accept_enrl)
wlp_get_sparse(u8, WLP_ATTR_WSS_SEC_STAT, wss_sec_status)
wlp_get_sparse(struct uwb_mac_addr, WLP_ATTR_WSS_BCAST, wss_bcast)
wlp_get_sparse(u8, WLP_ATTR_WSS_TAG, wss_tag)
wlp_get_sparse(struct uwb_mac_addr, WLP_ATTR_WSS_VIRT, wss_virt)
wlp_get_sparse(struct wlp_nonce, WLP_ATTR_ENRL_NONCE, enonce)
wlp_get_sparse(struct wlp_nonce, WLP_ATTR_REG_NONCE, rnonce)

/* The buffers for the device info attributes can be found in the
 * wlp_device_info struct. These buffers contain one byte more than the
 * max allowed by the spec - this is done to be able to add the
 * terminating \0 for user display. This terminating byte is not required
 * in the actual attribute field (because it has a length field) so the
 * maximum allowed for this value is one less than its size in the
 * structure.
 */
wlp_vget(char, WLP_ATTR_WSS_NAME, wss_name,
	 FIELD_SIZEOF(struct wlp_wss, name) - 1)
wlp_vget(char, WLP_ATTR_DEV_NAME, dev_name,
	 FIELD_SIZEOF(struct wlp_device_info, name) - 1)
wlp_vget(char, WLP_ATTR_MANUF, manufacturer,
	 FIELD_SIZEOF(struct wlp_device_info, manufacturer) - 1)
wlp_vget(char, WLP_ATTR_MODEL_NAME, model_name,
	 FIELD_SIZEOF(struct wlp_device_info, model_name) - 1)
wlp_vget(char, WLP_ATTR_MODEL_NR, model_nr,
	 FIELD_SIZEOF(struct wlp_device_info, model_nr) - 1)
wlp_vget(char, WLP_ATTR_SERIAL, serial,
	 FIELD_SIZEOF(struct wlp_device_info, serial) - 1)

/**
 * Retrieve WSS Name, Accept enroll, Secure status, Broadcast from WSS info
 *
 * @attr: pointer to WSS name attribute in WSS information attribute field
 * @info: structure that will be populated with data from WSS information
 *        field (WSS name, Accept enroll, secure status, broadcast address)
 * @buflen: size of buffer
 *
 * Although the WSSID attribute forms part of the WSS info attribute it is
 * retrieved separately and stored in a different location.
 */
static ssize_t wlp_get_wss_info_attrs(struct wlp *wlp,
				      struct wlp_attr_hdr *attr,
				      struct wlp_wss_tmp_info *info,
				      ssize_t buflen)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	void *ptr = attr;
	size_t used = 0;
	ssize_t result = -EINVAL;

	result = wlp_get_wss_name(wlp, ptr, info->name, buflen);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WSS name from "
			"WSS info in D2 message.\n");
		goto error_parse;
	}
	used += result;

	result = wlp_get_accept_enrl(wlp, ptr + used, &info->accept_enroll,
				     buflen - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain accepting "
			"enrollment from WSS info in D2 message.\n");
		goto error_parse;
	}
	if (info->accept_enroll != 0 && info->accept_enroll != 1) {
		dev_err(dev, "WLP: invalid value for accepting "
			"enrollment in D2 message.\n");
		result = -EINVAL;
		goto error_parse;
	}
	used += result;

	result = wlp_get_wss_sec_status(wlp, ptr + used, &info->sec_status,
					buflen - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain secure "
			"status from WSS info in D2 message.\n");
		goto error_parse;
	}
	if (info->sec_status != 0 && info->sec_status != 1) {
		dev_err(dev, "WLP: invalid value for secure "
			"status in D2 message.\n");
		result = -EINVAL;
		goto error_parse;
	}
	used += result;

	result = wlp_get_wss_bcast(wlp, ptr + used, &info->bcast,
				   buflen - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain broadcast "
			"address from WSS info in D2 message.\n");
		goto error_parse;
	}
	used += result;
	result = used;
error_parse:
	return result;
}

/**
 * Create a new WSSID entry for the neighbor, allocate temporary storage
 *
 * Each neighbor can have many WSS active. We maintain a list of WSSIDs
 * advertised by neighbor. During discovery we also cache information about
 * these WSS in temporary storage.
 *
 * The temporary storage will be removed after it has been used (eg.
 * displayed to user), the wssid element will be removed from the list when
 * the neighbor is rediscovered or when it disappears.
 */
static struct wlp_wssid_e *wlp_create_wssid_e(struct wlp *wlp,
					      struct wlp_neighbor_e *neighbor)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_wssid_e *wssid_e;

	wssid_e = kzalloc(sizeof(*wssid_e), GFP_KERNEL);
	if (wssid_e == NULL) {
		dev_err(dev, "WLP: unable to allocate memory "
			"for WSS information.\n");
		goto error_alloc;
	}
	wssid_e->info = kzalloc(sizeof(struct wlp_wss_tmp_info), GFP_KERNEL);
	if (wssid_e->info == NULL) {
		dev_err(dev, "WLP: unable to allocate memory "
			"for temporary WSS information.\n");
		kfree(wssid_e);
		wssid_e = NULL;
		goto error_alloc;
	}
	list_add(&wssid_e->node, &neighbor->wssid);
error_alloc:
	return wssid_e;
}

/**
 * Parse WSS information attribute
 *
 * @attr: pointer to WSS information attribute header
 * @buflen: size of buffer in which WSS information attribute appears
 * @wssid: will place wssid from WSS info attribute in this location
 * @wss_info: will place other information from WSS information attribute
 * in this location
 *
 * memory for @wssid and @wss_info must be allocated when calling this
 */
static ssize_t wlp_get_wss_info(struct wlp *wlp, struct wlp_attr_wss_info *attr,
				size_t buflen, struct wlp_uuid *wssid,
				struct wlp_wss_tmp_info *wss_info)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	ssize_t result;
	size_t len;
	size_t used = 0;
	void *ptr;

	result = wlp_check_wss_info_attr_hdr(wlp, (struct wlp_attr_hdr *)attr,
					     buflen);
	if (result < 0)
		goto out;
	len = result;
	used = sizeof(*attr);
	ptr = attr;

	result = wlp_get_wssid(wlp, ptr + used, wssid, buflen - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WSSID from WSS info.\n");
		goto out;
	}
	used += result;
	result = wlp_get_wss_info_attrs(wlp, ptr + used, wss_info,
					buflen - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WSS information "
			"from WSS information attributes. \n");
		goto out;
	}
	used += result;
	if (len + sizeof(*attr) != used) {
		dev_err(dev, "WLP: Amount of data parsed does not "
			"match length field. Parsed %zu, length "
			"field %zu. \n", used, len);
		result = -EINVAL;
		goto out;
	}
	result = used;
out:
	return result;
}

/**
 * Retrieve WSS info from association frame
 *
 * @attr:     pointer to WSS information attribute
 * @neighbor: ptr to neighbor being discovered, NULL if enrollment in
 *            progress
 * @wss:      ptr to WSS being enrolled in, NULL if discovery in progress
 * @buflen:   size of buffer in which WSS information appears
 *
 * The WSS information attribute appears in the D2 association message.
 * This message is used in two ways: to discover all neighbors or to enroll
 * into a WSS activated by a neighbor. During discovery we only want to
 * store the WSS info in a cache, to be deleted right after it has been
 * used (eg. displayed to the user). During enrollment we store the WSS
 * information for the lifetime of enrollment.
 *
 * During discovery we are interested in all WSS information, during
 * enrollment we are only interested in the WSS being enrolled in. Even so,
 * when in enrollment we keep parsing the message after finding the WSS of
 * interest, this simplifies the calling routine in that it can be sure
 * that all WSS information attributes have been parsed out of the message.
 *
 * Association frame is process with nbmutex held. The list access is safe.
 */
static ssize_t wlp_get_all_wss_info(struct wlp *wlp,
				    struct wlp_attr_wss_info *attr,
				    struct wlp_neighbor_e *neighbor,
				    struct wlp_wss *wss, ssize_t buflen)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	size_t used = 0;
	ssize_t result = -EINVAL;
	struct wlp_attr_wss_info *cur;
	struct wlp_uuid wssid;
	struct wlp_wss_tmp_info wss_info;
	unsigned enroll; /* 0 - discovery to cache, 1 - enrollment */
	struct wlp_wssid_e *wssid_e;
	char buf[WLP_WSS_UUID_STRSIZE];

	if (buflen < 0)
		goto out;

	if (neighbor != NULL && wss == NULL)
		enroll = 0; /* discovery */
	else if (wss != NULL && neighbor == NULL)
		enroll = 1; /* enrollment */
	else
		goto out;

	cur = attr;
	while (buflen - used > 0) {
		memset(&wss_info, 0, sizeof(wss_info));
		cur = (void *)cur + used;
		result = wlp_get_wss_info(wlp, cur, buflen - used, &wssid,
					  &wss_info);
		if (result == -ENODATA) {
			result = used;
			goto out;
		} else if (result < 0) {
			dev_err(dev, "WLP: Unable to parse WSS information "
				"from WSS information attribute. \n");
			result = -EINVAL;
			goto error_parse;
		}
		if (enroll && !memcmp(&wssid, &wss->wssid, sizeof(wssid))) {
			if (wss_info.accept_enroll != 1) {
				dev_err(dev, "WLP: Requested WSS does "
					"not accept enrollment.\n");
				result = -EINVAL;
				goto out;
			}
			memcpy(wss->name, wss_info.name, sizeof(wss->name));
			wss->bcast = wss_info.bcast;
			wss->secure_status = wss_info.sec_status;
			wss->accept_enroll = wss_info.accept_enroll;
			wss->state = WLP_WSS_STATE_PART_ENROLLED;
			wlp_wss_uuid_print(buf, sizeof(buf), &wssid);
			dev_dbg(dev, "WLP: Found WSS %s. Enrolling.\n", buf);
		} else {
			wssid_e = wlp_create_wssid_e(wlp, neighbor);
			if (wssid_e == NULL) {
				dev_err(dev, "WLP: Cannot create new WSSID "
					"entry for neighbor %02x:%02x.\n",
					neighbor->uwb_dev->dev_addr.data[1],
					neighbor->uwb_dev->dev_addr.data[0]);
				result = -ENOMEM;
				goto out;
			}
			wssid_e->wssid = wssid;
			*wssid_e->info = wss_info;
		}
		used += result;
	}
	result = used;
error_parse:
	if (result < 0 && !enroll) /* this was a discovery */
		wlp_remove_neighbor_tmp_info(neighbor);
out:
	return result;

}

/**
 * Parse WSS information attributes into cache for discovery
 *
 * @attr: the first WSS information attribute in message
 * @neighbor: the neighbor whose cache will be populated
 * @buflen: size of the input buffer
 */
static ssize_t wlp_get_wss_info_to_cache(struct wlp *wlp,
					 struct wlp_attr_wss_info *attr,
					 struct wlp_neighbor_e *neighbor,
					 ssize_t buflen)
{
	return wlp_get_all_wss_info(wlp, attr, neighbor, NULL, buflen);
}

/**
 * Parse WSS information attributes into WSS struct for enrollment
 *
 * @attr: the first WSS information attribute in message
 * @wss: the WSS that will be enrolled
 * @buflen: size of the input buffer
 */
static ssize_t wlp_get_wss_info_to_enroll(struct wlp *wlp,
					  struct wlp_attr_wss_info *attr,
					  struct wlp_wss *wss, ssize_t buflen)
{
	return wlp_get_all_wss_info(wlp, attr, NULL, wss, buflen);
}

/**
 * Construct a D1 association frame
 *
 * We use the radio control functions to determine the values of the device
 * properties. These are of variable length and the total space needed is
 * tallied first before we start constructing the message. The radio
 * control functions return strings that are terminated with \0. This
 * character should not be included in the message (there is a length field
 * accompanying it in the attribute).
 */
static int wlp_build_assoc_d1(struct wlp *wlp, struct wlp_wss *wss,
			      struct sk_buff **skb)
{

	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result = 0;
	struct wlp_device_info *info;
	size_t used = 0;
	struct wlp_frame_assoc *_d1;
	struct sk_buff *_skb;
	void *d1_itr;

	if (wlp->dev_info == NULL) {
		result = __wlp_setup_device_info(wlp);
		if (result < 0) {
			dev_err(dev, "WLP: Unable to setup device "
				"information for D1 message.\n");
			goto error;
		}
	}
	info = wlp->dev_info;
	_skb = dev_alloc_skb(sizeof(*_d1)
		      + sizeof(struct wlp_attr_uuid_e)
		      + sizeof(struct wlp_attr_wss_sel_mthd)
		      + sizeof(struct wlp_attr_dev_name)
		      + strlen(info->name)
		      + sizeof(struct wlp_attr_manufacturer)
		      + strlen(info->manufacturer)
		      + sizeof(struct wlp_attr_model_name)
		      + strlen(info->model_name)
		      + sizeof(struct wlp_attr_model_nr)
		      + strlen(info->model_nr)
		      + sizeof(struct wlp_attr_serial)
		      + strlen(info->serial)
		      + sizeof(struct wlp_attr_prim_dev_type)
		      + sizeof(struct wlp_attr_wlp_assc_err));
	if (_skb == NULL) {
		dev_err(dev, "WLP: Cannot allocate memory for association "
			"message.\n");
		result = -ENOMEM;
		goto error;
	}
	_d1 = (void *) _skb->data;
	_d1->hdr.mux_hdr = cpu_to_le16(WLP_PROTOCOL_ID);
	_d1->hdr.type = WLP_FRAME_ASSOCIATION;
	_d1->type = WLP_ASSOC_D1;

	wlp_set_version(&_d1->version, WLP_VERSION);
	wlp_set_msg_type(&_d1->msg_type, WLP_ASSOC_D1);
	d1_itr = _d1->attr;
	used = wlp_set_uuid_e(d1_itr, &wlp->uuid);
	used += wlp_set_wss_sel_mthd(d1_itr + used, WLP_WSS_REG_SELECT);
	used += wlp_set_dev_name(d1_itr + used, info->name,
				 strlen(info->name));
	used += wlp_set_manufacturer(d1_itr + used, info->manufacturer,
				     strlen(info->manufacturer));
	used += wlp_set_model_name(d1_itr + used, info->model_name,
				   strlen(info->model_name));
	used += wlp_set_model_nr(d1_itr + used, info->model_nr,
				 strlen(info->model_nr));
	used += wlp_set_serial(d1_itr + used, info->serial,
			       strlen(info->serial));
	used += wlp_set_prim_dev_type(d1_itr + used, &info->prim_dev_type);
	used += wlp_set_wlp_assc_err(d1_itr + used, WLP_ASSOC_ERROR_NONE);
	skb_put(_skb, sizeof(*_d1) + used);
	*skb = _skb;
error:
	return result;
}

/**
 * Construct a D2 association frame
 *
 * We use the radio control functions to determine the values of the device
 * properties. These are of variable length and the total space needed is
 * tallied first before we start constructing the message. The radio
 * control functions return strings that are terminated with \0. This
 * character should not be included in the message (there is a length field
 * accompanying it in the attribute).
 */
static
int wlp_build_assoc_d2(struct wlp *wlp, struct wlp_wss *wss,
		       struct sk_buff **skb, struct wlp_uuid *uuid_e)
{

	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result = 0;
	struct wlp_device_info *info;
	size_t used = 0;
	struct wlp_frame_assoc *_d2;
	struct sk_buff *_skb;
	void *d2_itr;
	size_t mem_needed;

	if (wlp->dev_info == NULL) {
		result = __wlp_setup_device_info(wlp);
		if (result < 0) {
			dev_err(dev, "WLP: Unable to setup device "
				"information for D2 message.\n");
			goto error;
		}
	}
	info = wlp->dev_info;
	mem_needed = sizeof(*_d2)
		      + sizeof(struct wlp_attr_uuid_e)
		      + sizeof(struct wlp_attr_uuid_r)
		      + sizeof(struct wlp_attr_dev_name)
		      + strlen(info->name)
		      + sizeof(struct wlp_attr_manufacturer)
		      + strlen(info->manufacturer)
		      + sizeof(struct wlp_attr_model_name)
		      + strlen(info->model_name)
		      + sizeof(struct wlp_attr_model_nr)
		      + strlen(info->model_nr)
		      + sizeof(struct wlp_attr_serial)
		      + strlen(info->serial)
		      + sizeof(struct wlp_attr_prim_dev_type)
		      + sizeof(struct wlp_attr_wlp_assc_err);
	if (wlp->wss.state >= WLP_WSS_STATE_ACTIVE)
		mem_needed += sizeof(struct wlp_attr_wss_info)
			      + sizeof(struct wlp_wss_info)
			      + strlen(wlp->wss.name);
	_skb = dev_alloc_skb(mem_needed);
	if (_skb == NULL) {
		dev_err(dev, "WLP: Cannot allocate memory for association "
			"message.\n");
		result = -ENOMEM;
		goto error;
	}
	_d2 = (void *) _skb->data;
	_d2->hdr.mux_hdr = cpu_to_le16(WLP_PROTOCOL_ID);
	_d2->hdr.type = WLP_FRAME_ASSOCIATION;
	_d2->type = WLP_ASSOC_D2;

	wlp_set_version(&_d2->version, WLP_VERSION);
	wlp_set_msg_type(&_d2->msg_type, WLP_ASSOC_D2);
	d2_itr = _d2->attr;
	used = wlp_set_uuid_e(d2_itr, uuid_e);
	used += wlp_set_uuid_r(d2_itr + used, &wlp->uuid);
	if (wlp->wss.state >= WLP_WSS_STATE_ACTIVE)
		used += wlp_set_wss_info(d2_itr + used, &wlp->wss);
	used += wlp_set_dev_name(d2_itr + used, info->name,
				 strlen(info->name));
	used += wlp_set_manufacturer(d2_itr + used, info->manufacturer,
				     strlen(info->manufacturer));
	used += wlp_set_model_name(d2_itr + used, info->model_name,
				   strlen(info->model_name));
	used += wlp_set_model_nr(d2_itr + used, info->model_nr,
				 strlen(info->model_nr));
	used += wlp_set_serial(d2_itr + used, info->serial,
			       strlen(info->serial));
	used += wlp_set_prim_dev_type(d2_itr + used, &info->prim_dev_type);
	used += wlp_set_wlp_assc_err(d2_itr + used, WLP_ASSOC_ERROR_NONE);
	skb_put(_skb, sizeof(*_d2) + used);
	*skb = _skb;
error:
	return result;
}

/**
 * Allocate memory for and populate fields of F0 association frame
 *
 * Currently (while focusing on unsecure enrollment) we ignore the
 * nonce's that could be placed in the message. Only the error field is
 * populated by the value provided by the caller.
 */
static
int wlp_build_assoc_f0(struct wlp *wlp, struct sk_buff **skb,
		       enum wlp_assc_error error)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result = -ENOMEM;
	struct {
		struct wlp_frame_assoc f0_hdr;
		struct wlp_attr_enonce enonce;
		struct wlp_attr_rnonce rnonce;
		struct wlp_attr_wlp_assc_err assc_err;
	} *f0;
	struct sk_buff *_skb;
	struct wlp_nonce tmp;

	_skb = dev_alloc_skb(sizeof(*f0));
	if (_skb == NULL) {
		dev_err(dev, "WLP: Unable to allocate memory for F0 "
			"association frame. \n");
		goto error_alloc;
	}
	f0 = (void *) _skb->data;
	f0->f0_hdr.hdr.mux_hdr = cpu_to_le16(WLP_PROTOCOL_ID);
	f0->f0_hdr.hdr.type = WLP_FRAME_ASSOCIATION;
	f0->f0_hdr.type = WLP_ASSOC_F0;
	wlp_set_version(&f0->f0_hdr.version, WLP_VERSION);
	wlp_set_msg_type(&f0->f0_hdr.msg_type, WLP_ASSOC_F0);
	memset(&tmp, 0, sizeof(tmp));
	wlp_set_enonce(&f0->enonce, &tmp);
	wlp_set_rnonce(&f0->rnonce, &tmp);
	wlp_set_wlp_assc_err(&f0->assc_err, error);
	skb_put(_skb, sizeof(*f0));
	*skb = _skb;
	result = 0;
error_alloc:
	return result;
}

/**
 * Parse F0 frame
 *
 * We just retrieve the values and print it as an error to the user.
 * Calling function already knows an error occured (F0 indicates error), so
 * we just parse the content as debug for higher layers.
 */
int wlp_parse_f0(struct wlp *wlp, struct sk_buff *skb)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_frame_assoc *f0 = (void *) skb->data;
	void *ptr = skb->data;
	size_t len = skb->len;
	size_t used;
	ssize_t result;
	struct wlp_nonce enonce, rnonce;
	enum wlp_assc_error assc_err;
	char enonce_buf[WLP_WSS_NONCE_STRSIZE];
	char rnonce_buf[WLP_WSS_NONCE_STRSIZE];

	used = sizeof(*f0);
	result = wlp_get_enonce(wlp, ptr + used, &enonce, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain Enrollee nonce "
			"attribute from F0 message.\n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_rnonce(wlp, ptr + used, &rnonce, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain Registrar nonce "
			"attribute from F0 message.\n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_wlp_assc_err(wlp, ptr + used, &assc_err, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WLP Association error "
			"attribute from F0 message.\n");
		goto error_parse;
	}
	wlp_wss_nonce_print(enonce_buf, sizeof(enonce_buf), &enonce);
	wlp_wss_nonce_print(rnonce_buf, sizeof(rnonce_buf), &rnonce);
	dev_err(dev, "WLP: Received F0 error frame from neighbor. Enrollee "
		"nonce: %s, Registrar nonce: %s, WLP Association error: %s.\n",
		enonce_buf, rnonce_buf, wlp_assc_error_str(assc_err));
	result = 0;
error_parse:
	return result;
}

/**
 * Retrieve variable device information from association message
 *
 * The device information parsed is not required in any message. This
 * routine will thus not fail if an attribute is not present.
 * The attributes are expected in a certain order, even if all are not
 * present. The "attribute type" value is used to ensure the attributes
 * are parsed in the correct order.
 *
 * If an error is encountered during parsing the function will return an
 * error code, when this happens the given device_info structure may be
 * partially filled.
 */
static
int wlp_get_variable_info(struct wlp *wlp, void *data,
			  struct wlp_device_info *dev_info, ssize_t len)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	size_t used = 0;
	struct wlp_attr_hdr *hdr;
	ssize_t result = 0;
	unsigned last = 0;

	while (len - used > 0) {
		if (len - used < sizeof(*hdr)) {
			dev_err(dev, "WLP: Partial data in frame, cannot "
				"parse. \n");
			goto error_parse;
		}
		hdr = data + used;
		switch (le16_to_cpu(hdr->type)) {
		case WLP_ATTR_MANUF:
			if (last >= WLP_ATTR_MANUF) {
				dev_err(dev, "WLP: Incorrect order of "
					"attribute values in D1 msg.\n");
				goto error_parse;
			}
			result = wlp_get_manufacturer(wlp, data + used,
						      dev_info->manufacturer,
						      len - used);
			if (result < 0) {
				dev_err(dev, "WLP: Unable to obtain "
					"Manufacturer attribute from D1 "
					"message.\n");
				goto error_parse;
			}
			last = WLP_ATTR_MANUF;
			used += result;
			break;
		case WLP_ATTR_MODEL_NAME:
			if (last >= WLP_ATTR_MODEL_NAME) {
				dev_err(dev, "WLP: Incorrect order of "
					"attribute values in D1 msg.\n");
				goto error_parse;
			}
			result = wlp_get_model_name(wlp, data + used,
						    dev_info->model_name,
						    len - used);
			if (result < 0) {
				dev_err(dev, "WLP: Unable to obtain Model "
					"name attribute from D1 message.\n");
				goto error_parse;
			}
			last = WLP_ATTR_MODEL_NAME;
			used += result;
			break;
		case WLP_ATTR_MODEL_NR:
			if (last >= WLP_ATTR_MODEL_NR) {
				dev_err(dev, "WLP: Incorrect order of "
					"attribute values in D1 msg.\n");
				goto error_parse;
			}
			result = wlp_get_model_nr(wlp, data + used,
						  dev_info->model_nr,
						  len - used);
			if (result < 0) {
				dev_err(dev, "WLP: Unable to obtain Model "
					"number attribute from D1 message.\n");
				goto error_parse;
			}
			last = WLP_ATTR_MODEL_NR;
			used += result;
			break;
		case WLP_ATTR_SERIAL:
			if (last >= WLP_ATTR_SERIAL) {
				dev_err(dev, "WLP: Incorrect order of "
					"attribute values in D1 msg.\n");
				goto error_parse;
			}
			result = wlp_get_serial(wlp, data + used,
						dev_info->serial, len - used);
			if (result < 0) {
				dev_err(dev, "WLP: Unable to obtain Serial "
					"number attribute from D1 message.\n");
				goto error_parse;
			}
			last = WLP_ATTR_SERIAL;
			used += result;
			break;
		case WLP_ATTR_PRI_DEV_TYPE:
			if (last >= WLP_ATTR_PRI_DEV_TYPE) {
				dev_err(dev, "WLP: Incorrect order of "
					"attribute values in D1 msg.\n");
				goto error_parse;
			}
			result = wlp_get_prim_dev_type(wlp, data + used,
						       &dev_info->prim_dev_type,
						       len - used);
			if (result < 0) {
				dev_err(dev, "WLP: Unable to obtain Primary "
					"device type attribute from D1 "
					"message.\n");
				goto error_parse;
			}
			dev_info->prim_dev_type.category =
				le16_to_cpu(dev_info->prim_dev_type.category);
			dev_info->prim_dev_type.subID =
				le16_to_cpu(dev_info->prim_dev_type.subID);
			last = WLP_ATTR_PRI_DEV_TYPE;
			used += result;
			break;
		default:
			/* This is not variable device information. */
			goto out;
			break;
		}
	}
out:
	return used;
error_parse:
	return -EINVAL;
}

/**
 * Parse incoming D1 frame, populate attribute values
 *
 * Caller provides pointers to memory already allocated for attributes
 * expected in the D1 frame. These variables will be populated.
 */
static
int wlp_parse_d1_frame(struct wlp *wlp, struct sk_buff *skb,
		       struct wlp_uuid *uuid_e,
		       enum wlp_wss_sel_mthd *sel_mthd,
		       struct wlp_device_info *dev_info,
		       enum wlp_assc_error *assc_err)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_frame_assoc *d1 = (void *) skb->data;
	void *ptr = skb->data;
	size_t len = skb->len;
	size_t used;
	ssize_t result;

	used = sizeof(*d1);
	result = wlp_get_uuid_e(wlp, ptr + used, uuid_e, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain UUID-E attribute from D1 "
			"message.\n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_wss_sel_mthd(wlp, ptr + used, sel_mthd, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WSS selection method "
			"from D1 message.\n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_dev_name(wlp, ptr + used, dev_info->name,
				     len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain Device Name from D1 "
			"message.\n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_variable_info(wlp, ptr + used, dev_info, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain Device Information from "
			"D1 message.\n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_wlp_assc_err(wlp, ptr + used, assc_err, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WLP Association Error "
			"Information from D1 message.\n");
		goto error_parse;
	}
	result = 0;
error_parse:
	return result;
}
/**
 * Handle incoming D1 frame
 *
 * The frame has already been verified to contain an Association header with
 * the correct version number. Parse the incoming frame, construct and send
 * a D2 frame in response.
 *
 * It is not clear what to do with most fields in the incoming D1 frame. We
 * retrieve and discard the information here for now.
 */
void wlp_handle_d1_frame(struct work_struct *ws)
{
	struct wlp_assoc_frame_ctx *frame_ctx = container_of(ws,
						  struct wlp_assoc_frame_ctx,
						  ws);
	struct wlp *wlp = frame_ctx->wlp;
	struct wlp_wss *wss = &wlp->wss;
	struct sk_buff *skb = frame_ctx->skb;
	struct uwb_dev_addr *src = &frame_ctx->src;
	int result;
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_uuid uuid_e;
	enum wlp_wss_sel_mthd sel_mthd = 0;
	struct wlp_device_info dev_info;
	enum wlp_assc_error assc_err;
	struct sk_buff *resp = NULL;

	/* Parse D1 frame */
	mutex_lock(&wss->mutex);
	mutex_lock(&wlp->mutex); /* to access wlp->uuid */
	memset(&dev_info, 0, sizeof(dev_info));
	result = wlp_parse_d1_frame(wlp, skb, &uuid_e, &sel_mthd, &dev_info,
				    &assc_err);
	if (result < 0) {
		dev_err(dev, "WLP: Unable to parse incoming D1 frame.\n");
		kfree_skb(skb);
		goto out;
	}

	kfree_skb(skb);
	if (!wlp_uuid_is_set(&wlp->uuid)) {
		dev_err(dev, "WLP: UUID is not set. Set via sysfs to "
			"proceed. Respong to D1 message with error F0.\n");
		result = wlp_build_assoc_f0(wlp, &resp,
					    WLP_ASSOC_ERROR_NOT_READY);
		if (result < 0) {
			dev_err(dev, "WLP: Unable to construct F0 message.\n");
			goto out;
		}
	} else {
		/* Construct D2 frame */
		result = wlp_build_assoc_d2(wlp, wss, &resp, &uuid_e);
		if (result < 0) {
			dev_err(dev, "WLP: Unable to construct D2 message.\n");
			goto out;
		}
	}
	/* Send D2 frame */
	BUG_ON(wlp->xmit_frame == NULL);
	result = wlp->xmit_frame(wlp, resp, src);
	if (result < 0) {
		dev_err(dev, "WLP: Unable to transmit D2 association "
			"message: %d\n", result);
		if (result == -ENXIO)
			dev_err(dev, "WLP: Is network interface up? \n");
		/* We could try again ... */
		dev_kfree_skb_any(resp); /* we need to free if tx fails */
	}
out:
	kfree(frame_ctx);
	mutex_unlock(&wlp->mutex);
	mutex_unlock(&wss->mutex);
}

/**
 * Parse incoming D2 frame, create and populate temporary cache
 *
 * @skb: socket buffer in which D2 frame can be found
 * @neighbor: the neighbor that sent the D2 frame
 *
 * Will allocate memory for temporary storage of information learned during
 * discovery.
 */
int wlp_parse_d2_frame_to_cache(struct wlp *wlp, struct sk_buff *skb,
				struct wlp_neighbor_e *neighbor)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_frame_assoc *d2 = (void *) skb->data;
	void *ptr = skb->data;
	size_t len = skb->len;
	size_t used;
	ssize_t result;
	struct wlp_uuid uuid_e;
	struct wlp_device_info *nb_info;
	enum wlp_assc_error assc_err;

	used = sizeof(*d2);
	result = wlp_get_uuid_e(wlp, ptr + used, &uuid_e, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain UUID-E attribute from D2 "
			"message.\n");
		goto error_parse;
	}
	if (memcmp(&uuid_e, &wlp->uuid, sizeof(uuid_e))) {
		dev_err(dev, "WLP: UUID-E in incoming D2 does not match "
			"local UUID sent in D1. \n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_uuid_r(wlp, ptr + used, &neighbor->uuid, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain UUID-R attribute from D2 "
			"message.\n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_wss_info_to_cache(wlp, ptr + used, neighbor,
					   len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WSS information "
			"from D2 message.\n");
		goto error_parse;
	}
	used += result;
	neighbor->info = kzalloc(sizeof(struct wlp_device_info), GFP_KERNEL);
	if (neighbor->info == NULL) {
		dev_err(dev, "WLP: cannot allocate memory to store device "
			"info.\n");
		result = -ENOMEM;
		goto error_parse;
	}
	nb_info = neighbor->info;
	result = wlp_get_dev_name(wlp, ptr + used, nb_info->name,
				  len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain Device Name from D2 "
			"message.\n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_variable_info(wlp, ptr + used, nb_info, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain Device Information from "
			"D2 message.\n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_wlp_assc_err(wlp, ptr + used, &assc_err, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WLP Association Error "
			"Information from D2 message.\n");
		goto error_parse;
	}
	if (assc_err != WLP_ASSOC_ERROR_NONE) {
		dev_err(dev, "WLP: neighbor device returned association "
			"error %d\n", assc_err);
		result = -EINVAL;
		goto error_parse;
	}
	result = 0;
error_parse:
	if (result < 0)
		wlp_remove_neighbor_tmp_info(neighbor);
	return result;
}

/**
 * Parse incoming D2 frame, populate attribute values of WSS bein enrolled in
 *
 * @wss: our WSS that will be enrolled
 * @skb: socket buffer in which D2 frame can be found
 * @neighbor: the neighbor that sent the D2 frame
 * @wssid: the wssid of the WSS in which we want to enroll
 *
 * Forms part of enrollment sequence. We are trying to enroll in WSS with
 * @wssid by using @neighbor as registrar. A D1 message was sent to
 * @neighbor and now we need to parse the D2 response. The neighbor's
 * response is searched for the requested WSS and if found (and it accepts
 * enrollment), we store the information.
 */
int wlp_parse_d2_frame_to_enroll(struct wlp_wss *wss, struct sk_buff *skb,
				 struct wlp_neighbor_e *neighbor,
				 struct wlp_uuid *wssid)
{
	struct wlp *wlp = container_of(wss, struct wlp, wss);
	struct device *dev = &wlp->rc->uwb_dev.dev;
	void *ptr = skb->data;
	size_t len = skb->len;
	size_t used;
	ssize_t result;
	struct wlp_uuid uuid_e;
	struct wlp_uuid uuid_r;
	struct wlp_device_info nb_info;
	enum wlp_assc_error assc_err;
	char uuid_bufA[WLP_WSS_UUID_STRSIZE];
	char uuid_bufB[WLP_WSS_UUID_STRSIZE];

	used = sizeof(struct wlp_frame_assoc);
	result = wlp_get_uuid_e(wlp, ptr + used, &uuid_e, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain UUID-E attribute from D2 "
			"message.\n");
		goto error_parse;
	}
	if (memcmp(&uuid_e, &wlp->uuid, sizeof(uuid_e))) {
		dev_err(dev, "WLP: UUID-E in incoming D2 does not match "
			"local UUID sent in D1. \n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_uuid_r(wlp, ptr + used, &uuid_r, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain UUID-R attribute from D2 "
			"message.\n");
		goto error_parse;
	}
	if (memcmp(&uuid_r, &neighbor->uuid, sizeof(uuid_r))) {
		wlp_wss_uuid_print(uuid_bufA, sizeof(uuid_bufA),
				   &neighbor->uuid);
		wlp_wss_uuid_print(uuid_bufB, sizeof(uuid_bufB), &uuid_r);
		dev_err(dev, "WLP: UUID of neighbor does not match UUID "
			"learned during discovery. Originally discovered: %s, "
			"now from D2 message: %s\n", uuid_bufA, uuid_bufB);
		result = -EINVAL;
		goto error_parse;
	}
	used += result;
	wss->wssid = *wssid;
	result = wlp_get_wss_info_to_enroll(wlp, ptr + used, wss, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WSS information "
			"from D2 message.\n");
		goto error_parse;
	}
	if (wss->state != WLP_WSS_STATE_PART_ENROLLED) {
		dev_err(dev, "WLP: D2 message did not contain information "
			"for successful enrollment. \n");
		result = -EINVAL;
		goto error_parse;
	}
	used += result;
	/* Place device information on stack to continue parsing of message */
	result = wlp_get_dev_name(wlp, ptr + used, nb_info.name,
				  len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain Device Name from D2 "
			"message.\n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_variable_info(wlp, ptr + used, &nb_info, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain Device Information from "
			"D2 message.\n");
		goto error_parse;
	}
	used += result;
	result = wlp_get_wlp_assc_err(wlp, ptr + used, &assc_err, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WLP Association Error "
			"Information from D2 message.\n");
		goto error_parse;
	}
	if (assc_err != WLP_ASSOC_ERROR_NONE) {
		dev_err(dev, "WLP: neighbor device returned association "
			"error %d\n", assc_err);
		if (wss->state == WLP_WSS_STATE_PART_ENROLLED) {
			dev_err(dev, "WLP: Enrolled in WSS (should not "
				"happen according to spec). Undoing. \n");
			wlp_wss_reset(wss);
		}
		result = -EINVAL;
		goto error_parse;
	}
	result = 0;
error_parse:
	return result;
}

/**
 * Parse C3/C4 frame into provided variables
 *
 * @wssid: will point to copy of wssid retrieved from C3/C4 frame
 * @tag:   will point to copy of tag retrieved from C3/C4 frame
 * @virt_addr: will point to copy of virtual address retrieved from C3/C4
 * frame.
 *
 * Calling function has to allocate memory for these values.
 *
 * skb contains a valid C3/C4 frame, return the individual fields of this
 * frame in the provided variables.
 */
int wlp_parse_c3c4_frame(struct wlp *wlp, struct sk_buff *skb,
		       struct wlp_uuid *wssid, u8 *tag,
		       struct uwb_mac_addr *virt_addr)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result;
	void *ptr = skb->data;
	size_t len = skb->len;
	size_t used;
	struct wlp_frame_assoc *assoc = ptr;

	used = sizeof(*assoc);
	result = wlp_get_wssid(wlp, ptr + used, wssid, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WSSID attribute from "
			"%s message.\n", wlp_assoc_frame_str(assoc->type));
		goto error_parse;
	}
	used += result;
	result = wlp_get_wss_tag(wlp, ptr + used, tag, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WSS tag attribute from "
			"%s message.\n", wlp_assoc_frame_str(assoc->type));
		goto error_parse;
	}
	used += result;
	result = wlp_get_wss_virt(wlp, ptr + used, virt_addr, len - used);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WSS virtual address "
			"attribute from %s message.\n",
			wlp_assoc_frame_str(assoc->type));
		goto error_parse;
	}
error_parse:
	return result;
}

/**
 * Allocate memory for and populate fields of C1 or C2 association frame
 *
 * The C1 and C2 association frames appear identical - except for the type.
 */
static
int wlp_build_assoc_c1c2(struct wlp *wlp, struct wlp_wss *wss,
			 struct sk_buff **skb, enum wlp_assoc_type type)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result  = -ENOMEM;
	struct {
		struct wlp_frame_assoc c_hdr;
		struct wlp_attr_wssid wssid;
	} *c;
	struct sk_buff *_skb;

	_skb = dev_alloc_skb(sizeof(*c));
	if (_skb == NULL) {
		dev_err(dev, "WLP: Unable to allocate memory for C1/C2 "
			"association frame. \n");
		goto error_alloc;
	}
	c = (void *) _skb->data;
	c->c_hdr.hdr.mux_hdr = cpu_to_le16(WLP_PROTOCOL_ID);
	c->c_hdr.hdr.type = WLP_FRAME_ASSOCIATION;
	c->c_hdr.type = type;
	wlp_set_version(&c->c_hdr.version, WLP_VERSION);
	wlp_set_msg_type(&c->c_hdr.msg_type, type);
	wlp_set_wssid(&c->wssid, &wss->wssid);
	skb_put(_skb, sizeof(*c));
	*skb = _skb;
	result = 0;
error_alloc:
	return result;
}


static
int wlp_build_assoc_c1(struct wlp *wlp, struct wlp_wss *wss,
		       struct sk_buff **skb)
{
	return wlp_build_assoc_c1c2(wlp, wss, skb, WLP_ASSOC_C1);
}

static
int wlp_build_assoc_c2(struct wlp *wlp, struct wlp_wss *wss,
		       struct sk_buff **skb)
{
	return wlp_build_assoc_c1c2(wlp, wss, skb, WLP_ASSOC_C2);
}


/**
 * Allocate memory for and populate fields of C3 or C4 association frame
 *
 * The C3 and C4 association frames appear identical - except for the type.
 */
static
int wlp_build_assoc_c3c4(struct wlp *wlp, struct wlp_wss *wss,
			 struct sk_buff **skb, enum wlp_assoc_type type)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result  = -ENOMEM;
	struct {
		struct wlp_frame_assoc c_hdr;
		struct wlp_attr_wssid wssid;
		struct wlp_attr_wss_tag wss_tag;
		struct wlp_attr_wss_virt wss_virt;
	} *c;
	struct sk_buff *_skb;

	_skb = dev_alloc_skb(sizeof(*c));
	if (_skb == NULL) {
		dev_err(dev, "WLP: Unable to allocate memory for C3/C4 "
			"association frame. \n");
		goto error_alloc;
	}
	c = (void *) _skb->data;
	c->c_hdr.hdr.mux_hdr = cpu_to_le16(WLP_PROTOCOL_ID);
	c->c_hdr.hdr.type = WLP_FRAME_ASSOCIATION;
	c->c_hdr.type = type;
	wlp_set_version(&c->c_hdr.version, WLP_VERSION);
	wlp_set_msg_type(&c->c_hdr.msg_type, type);
	wlp_set_wssid(&c->wssid, &wss->wssid);
	wlp_set_wss_tag(&c->wss_tag, wss->tag);
	wlp_set_wss_virt(&c->wss_virt, &wss->virtual_addr);
	skb_put(_skb, sizeof(*c));
	*skb = _skb;
	result = 0;
error_alloc:
	return result;
}

static
int wlp_build_assoc_c3(struct wlp *wlp, struct wlp_wss *wss,
		       struct sk_buff **skb)
{
	return wlp_build_assoc_c3c4(wlp, wss, skb, WLP_ASSOC_C3);
}

static
int wlp_build_assoc_c4(struct wlp *wlp, struct wlp_wss *wss,
		       struct sk_buff **skb)
{
	return wlp_build_assoc_c3c4(wlp, wss, skb, WLP_ASSOC_C4);
}


#define wlp_send_assoc(type, id)					\
static int wlp_send_assoc_##type(struct wlp *wlp, struct wlp_wss *wss,	\
				 struct uwb_dev_addr *dev_addr)		\
{									\
	struct device *dev = &wlp->rc->uwb_dev.dev;			\
	int result;							\
	struct sk_buff *skb = NULL;					\
									\
	/* Build the frame */						\
	result = wlp_build_assoc_##type(wlp, wss, &skb);		\
	if (result < 0) {						\
		dev_err(dev, "WLP: Unable to construct %s association "	\
			"frame: %d\n", wlp_assoc_frame_str(id), result);\
		goto error_build_assoc;					\
	}								\
	/* Send the frame */						\
	BUG_ON(wlp->xmit_frame == NULL);				\
	result = wlp->xmit_frame(wlp, skb, dev_addr);			\
	if (result < 0) {						\
		dev_err(dev, "WLP: Unable to transmit %s association "	\
			"message: %d\n", wlp_assoc_frame_str(id),	\
			result);					\
		if (result == -ENXIO)					\
			dev_err(dev, "WLP: Is network interface "	\
				"up? \n");				\
		goto error_xmit;					\
	}								\
	return 0;							\
error_xmit:								\
	/* We could try again ... */					\
	dev_kfree_skb_any(skb);/*we need to free if tx fails*/		\
error_build_assoc:							\
	return result;							\
}

wlp_send_assoc(d1, WLP_ASSOC_D1)
wlp_send_assoc(c1, WLP_ASSOC_C1)
wlp_send_assoc(c3, WLP_ASSOC_C3)

int wlp_send_assoc_frame(struct wlp *wlp, struct wlp_wss *wss,
			 struct uwb_dev_addr *dev_addr,
			 enum wlp_assoc_type type)
{
	int result = 0;
	struct device *dev = &wlp->rc->uwb_dev.dev;
	switch (type) {
	case WLP_ASSOC_D1:
		result = wlp_send_assoc_d1(wlp, wss, dev_addr);
		break;
	case WLP_ASSOC_C1:
		result = wlp_send_assoc_c1(wlp, wss, dev_addr);
		break;
	case WLP_ASSOC_C3:
		result = wlp_send_assoc_c3(wlp, wss, dev_addr);
		break;
	default:
		dev_err(dev, "WLP: Received request to send unknown "
			"association message.\n");
		result = -EINVAL;
		break;
	}
	return result;
}

/**
 * Handle incoming C1 frame
 *
 * The frame has already been verified to contain an Association header with
 * the correct version number. Parse the incoming frame, construct and send
 * a C2 frame in response.
 */
void wlp_handle_c1_frame(struct work_struct *ws)
{
	struct wlp_assoc_frame_ctx *frame_ctx = container_of(ws,
						  struct wlp_assoc_frame_ctx,
						  ws);
	struct wlp *wlp = frame_ctx->wlp;
	struct wlp_wss *wss = &wlp->wss;
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_frame_assoc *c1 = (void *) frame_ctx->skb->data;
	unsigned int len = frame_ctx->skb->len;
	struct uwb_dev_addr *src = &frame_ctx->src;
	int result;
	struct wlp_uuid wssid;
	struct sk_buff *resp = NULL;

	/* Parse C1 frame */
	mutex_lock(&wss->mutex);
	result = wlp_get_wssid(wlp, (void *)c1 + sizeof(*c1), &wssid,
			       len - sizeof(*c1));
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WSSID from C1 frame.\n");
		goto out;
	}
	if (!memcmp(&wssid, &wss->wssid, sizeof(wssid))
	    && wss->state == WLP_WSS_STATE_ACTIVE) {
		/* Construct C2 frame */
		result = wlp_build_assoc_c2(wlp, wss, &resp);
		if (result < 0) {
			dev_err(dev, "WLP: Unable to construct C2 message.\n");
			goto out;
		}
	} else {
		/* Construct F0 frame */
		result = wlp_build_assoc_f0(wlp, &resp, WLP_ASSOC_ERROR_INV);
		if (result < 0) {
			dev_err(dev, "WLP: Unable to construct F0 message.\n");
			goto out;
		}
	}
	/* Send C2 frame */
	BUG_ON(wlp->xmit_frame == NULL);
	result = wlp->xmit_frame(wlp, resp, src);
	if (result < 0) {
		dev_err(dev, "WLP: Unable to transmit response association "
			"message: %d\n", result);
		if (result == -ENXIO)
			dev_err(dev, "WLP: Is network interface up? \n");
		/* We could try again ... */
		dev_kfree_skb_any(resp); /* we need to free if tx fails */
	}
out:
	kfree_skb(frame_ctx->skb);
	kfree(frame_ctx);
	mutex_unlock(&wss->mutex);
}

/**
 * Handle incoming C3 frame
 *
 * The frame has already been verified to contain an Association header with
 * the correct version number. Parse the incoming frame, construct and send
 * a C4 frame in response. If the C3 frame identifies a WSS that is locally
 * active then we connect to this neighbor (add it to our EDA cache).
 */
void wlp_handle_c3_frame(struct work_struct *ws)
{
	struct wlp_assoc_frame_ctx *frame_ctx = container_of(ws,
						  struct wlp_assoc_frame_ctx,
						  ws);
	struct wlp *wlp = frame_ctx->wlp;
	struct wlp_wss *wss = &wlp->wss;
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct sk_buff *skb = frame_ctx->skb;
	struct uwb_dev_addr *src = &frame_ctx->src;
	int result;
	struct sk_buff *resp = NULL;
	struct wlp_uuid wssid;
	u8 tag;
	struct uwb_mac_addr virt_addr;

	/* Parse C3 frame */
	mutex_lock(&wss->mutex);
	result = wlp_parse_c3c4_frame(wlp, skb, &wssid, &tag, &virt_addr);
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain values from C3 frame.\n");
		goto out;
	}
	if (!memcmp(&wssid, &wss->wssid, sizeof(wssid))
	    && wss->state >= WLP_WSS_STATE_ACTIVE) {
		result = wlp_eda_update_node(&wlp->eda, src, wss,
					     (void *) virt_addr.data, tag,
					     WLP_WSS_CONNECTED);
		if (result < 0) {
			dev_err(dev, "WLP: Unable to update EDA cache "
				"with new connected neighbor information.\n");
			result = wlp_build_assoc_f0(wlp, &resp,
						    WLP_ASSOC_ERROR_INT);
			if (result < 0) {
				dev_err(dev, "WLP: Unable to construct F0 "
					"message.\n");
				goto out;
			}
		} else {
			wss->state = WLP_WSS_STATE_CONNECTED;
			/* Construct C4 frame */
			result = wlp_build_assoc_c4(wlp, wss, &resp);
			if (result < 0) {
				dev_err(dev, "WLP: Unable to construct C4 "
					"message.\n");
				goto out;
			}
		}
	} else {
		/* Construct F0 frame */
		result = wlp_build_assoc_f0(wlp, &resp, WLP_ASSOC_ERROR_INV);
		if (result < 0) {
			dev_err(dev, "WLP: Unable to construct F0 message.\n");
			goto out;
		}
	}
	/* Send C4 frame */
	BUG_ON(wlp->xmit_frame == NULL);
	result = wlp->xmit_frame(wlp, resp, src);
	if (result < 0) {
		dev_err(dev, "WLP: Unable to transmit response association "
			"message: %d\n", result);
		if (result == -ENXIO)
			dev_err(dev, "WLP: Is network interface up? \n");
		/* We could try again ... */
		dev_kfree_skb_any(resp); /* we need to free if tx fails */
	}
out:
	kfree_skb(frame_ctx->skb);
	kfree(frame_ctx);
	mutex_unlock(&wss->mutex);
}


