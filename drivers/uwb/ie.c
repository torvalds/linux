/*
 * Ultra Wide Band
 * Information Element Handling
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
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

#include "uwb-internal.h"
#define D_LOCAL 0
#include <linux/uwb/debug.h>

/**
 * uwb_ie_next - get the next IE in a buffer
 * @ptr: start of the buffer containing the IE data
 * @len: length of the buffer
 *
 * Both @ptr and @len are updated so subsequent calls to uwb_ie_next()
 * will get the next IE.
 *
 * NULL is returned (and @ptr and @len will not be updated) if there
 * are no more IEs in the buffer or the buffer is too short.
 */
struct uwb_ie_hdr *uwb_ie_next(void **ptr, size_t *len)
{
	struct uwb_ie_hdr *hdr;
	size_t ie_len;

	if (*len < sizeof(struct uwb_ie_hdr))
		return NULL;

	hdr = *ptr;
	ie_len = sizeof(struct uwb_ie_hdr) + hdr->length;

	if (*len < ie_len)
		return NULL;

	*ptr += ie_len;
	*len -= ie_len;

	return hdr;
}
EXPORT_SYMBOL_GPL(uwb_ie_next);

/**
 * Get the IEs that a radio controller is sending in its beacon
 *
 * @uwb_rc:  UWB Radio Controller
 * @returns: Size read from the system
 *
 * We don't need to lock the uwb_rc's mutex because we don't modify
 * anything. Once done with the iedata buffer, call
 * uwb_rc_ie_release(iedata). Don't call kfree on it.
 */
ssize_t uwb_rc_get_ie(struct uwb_rc *uwb_rc, struct uwb_rc_evt_get_ie **pget_ie)
{
	ssize_t result;
	struct device *dev = &uwb_rc->uwb_dev.dev;
	struct uwb_rccb *cmd = NULL;
	struct uwb_rceb *reply = NULL;
	struct uwb_rc_evt_get_ie *get_ie;

	d_fnstart(3, dev, "(%p, %p)\n", uwb_rc, pget_ie);
	result = -ENOMEM;
	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		goto error_kzalloc;
	cmd->bCommandType = UWB_RC_CET_GENERAL;
	cmd->wCommand = cpu_to_le16(UWB_RC_CMD_GET_IE);
	result = uwb_rc_vcmd(uwb_rc, "GET_IE", cmd, sizeof(*cmd),
			     UWB_RC_CET_GENERAL, UWB_RC_CMD_GET_IE,
			     &reply);
	if (result < 0)
		goto error_cmd;
	get_ie = container_of(reply, struct uwb_rc_evt_get_ie, rceb);
	if (result < sizeof(*get_ie)) {
		dev_err(dev, "not enough data returned for decoding GET IE "
			"(%zu bytes received vs %zu needed)\n",
			result, sizeof(*get_ie));
		result = -EINVAL;
	} else if (result < sizeof(*get_ie) + le16_to_cpu(get_ie->wIELength)) {
		dev_err(dev, "not enough data returned for decoding GET IE "
			"payload (%zu bytes received vs %zu needed)\n", result,
			sizeof(*get_ie) + le16_to_cpu(get_ie->wIELength));
		result = -EINVAL;
	} else
		*pget_ie = get_ie;
error_cmd:
	kfree(cmd);
error_kzalloc:
	d_fnend(3, dev, "(%p, %p) = %d\n", uwb_rc, pget_ie, (int)result);
	return result;
}
EXPORT_SYMBOL_GPL(uwb_rc_get_ie);


/*
 * Given a pointer to an IE, print it in ASCII/hex followed by a new line
 *
 * @ie_hdr: pointer to the IE header. Length is in there, and it is
 *          guaranteed that the ie_hdr->length bytes following it are
 *          safely accesible.
 *
 * @_data: context data passed from uwb_ie_for_each(), an struct output_ctx
 */
int uwb_ie_dump_hex(struct uwb_dev *uwb_dev, const struct uwb_ie_hdr *ie_hdr,
		    size_t offset, void *_ctx)
{
	struct uwb_buf_ctx *ctx = _ctx;
	const u8 *pl = (void *)(ie_hdr + 1);
	u8 pl_itr;

	ctx->bytes += scnprintf(ctx->buf + ctx->bytes, ctx->size - ctx->bytes,
				"%02x %02x ", (unsigned) ie_hdr->element_id,
				(unsigned) ie_hdr->length);
	pl_itr = 0;
	while (pl_itr < ie_hdr->length && ctx->bytes < ctx->size)
		ctx->bytes += scnprintf(ctx->buf + ctx->bytes,
					ctx->size - ctx->bytes,
					"%02x ", (unsigned) pl[pl_itr++]);
	if (ctx->bytes < ctx->size)
		ctx->buf[ctx->bytes++] = '\n';
	return 0;
}
EXPORT_SYMBOL_GPL(uwb_ie_dump_hex);


/**
 * Verify that a pointer in a buffer points to valid IE
 *
 * @start: pointer to start of buffer in which IE appears
 * @itr:   pointer to IE inside buffer that will be verified
 * @top:   pointer to end of buffer
 *
 * @returns: 0 if IE is valid, <0 otherwise
 *
 * Verification involves checking that the buffer can contain a
 * header and the amount of data reported in the IE header can be found in
 * the buffer.
 */
static
int uwb_rc_ie_verify(struct uwb_dev *uwb_dev, const void *start,
		     const void *itr, const void *top)
{
	struct device *dev = &uwb_dev->dev;
	const struct uwb_ie_hdr *ie_hdr;

	if (top - itr < sizeof(*ie_hdr)) {
		dev_err(dev, "Bad IE: no data to decode header "
			"(%zu bytes left vs %zu needed) at offset %zu\n",
			top - itr, sizeof(*ie_hdr), itr - start);
		return -EINVAL;
	}
	ie_hdr = itr;
	itr += sizeof(*ie_hdr);
	if (top - itr < ie_hdr->length) {
		dev_err(dev, "Bad IE: not enough data for payload "
			"(%zu bytes left vs %zu needed) at offset %zu\n",
			top - itr, (size_t)ie_hdr->length,
			(void *)ie_hdr - start);
		return -EINVAL;
	}
	return 0;
}


/**
 * Walk a buffer filled with consecutive IE's a buffer
 *
 * @uwb_dev: UWB device this IEs belong to (for err messages mainly)
 *
 * @fn: function to call with each IE; if it returns 0, we keep
 *      traversing the buffer. If it returns !0, we'll stop and return
 *      that value.
 *
 * @data: pointer passed to @fn
 *
 * @buf: buffer where the consecutive IEs are located
 *
 * @size: size of @buf
 *
 * Each IE is checked for basic correctness (there is space left for
 * the header and the payload). If that test is failed, we stop
 * processing. For every good IE, @fn is called.
 */
ssize_t uwb_ie_for_each(struct uwb_dev *uwb_dev, uwb_ie_f fn, void *data,
			const void *buf, size_t size)
{
	ssize_t result = 0;
	const struct uwb_ie_hdr *ie_hdr;
	const void *itr = buf, *top = itr + size;

	while (itr < top) {
		if (uwb_rc_ie_verify(uwb_dev, buf, itr, top) != 0)
			break;
		ie_hdr = itr;
		itr += sizeof(*ie_hdr) + ie_hdr->length;
		result = fn(uwb_dev, ie_hdr, itr - buf, data);
		if (result != 0)
			break;
	}
	return result;
}
EXPORT_SYMBOL_GPL(uwb_ie_for_each);


/**
 * Replace all IEs currently being transmitted by a device
 *
 * @cmd:    pointer to the SET-IE command with the IEs to set
 * @size:   size of @buf
 */
int uwb_rc_set_ie(struct uwb_rc *rc, struct uwb_rc_cmd_set_ie *cmd)
{
	int result;
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_rc_evt_set_ie reply;

	reply.rceb.bEventType = UWB_RC_CET_GENERAL;
	reply.rceb.wEvent = UWB_RC_CMD_SET_IE;
	result = uwb_rc_cmd(rc, "SET-IE", &cmd->rccb,
			    sizeof(*cmd) + le16_to_cpu(cmd->wIELength),
			    &reply.rceb, sizeof(reply));
	if (result < 0)
		goto error_cmd;
	else if (result != sizeof(reply)) {
		dev_err(dev, "SET-IE: not enough data to decode reply "
			"(%d bytes received vs %zu needed)\n",
			result, sizeof(reply));
		result = -EIO;
	} else if (reply.bResultCode != UWB_RC_RES_SUCCESS) {
		dev_err(dev, "SET-IE: command execution failed: %s (%d)\n",
			uwb_rc_strerror(reply.bResultCode), reply.bResultCode);
		result = -EIO;
	} else
		result = 0;
error_cmd:
	return result;
}

/**
 * Determine by IE id if IE is host settable
 * WUSB 1.0 [8.6.2.8 Table 8.85]
 *
 * EXCEPTION:
 * All but UWB_IE_WLP appears in Table 8.85 from WUSB 1.0. Setting this IE
 * is required for the WLP substack to perform association with its WSS so
 * we hope that the WUSB spec will be changed to reflect this.
 */
static
int uwb_rc_ie_is_host_settable(enum uwb_ie element_id)
{
	if (element_id == UWB_PCA_AVAILABILITY ||
	    element_id == UWB_BP_SWITCH_IE ||
	    element_id == UWB_MAC_CAPABILITIES_IE ||
	    element_id == UWB_PHY_CAPABILITIES_IE ||
	    element_id == UWB_APP_SPEC_PROBE_IE ||
	    element_id == UWB_IDENTIFICATION_IE ||
	    element_id == UWB_MASTER_KEY_ID_IE ||
	    element_id == UWB_IE_WLP ||
	    element_id == UWB_APP_SPEC_IE)
		return 1;
	return 0;
}


/**
 * Extract Host Settable IEs from IE
 *
 * @ie_data: pointer to buffer containing all IEs
 * @size:    size of buffer
 *
 * @returns: length of buffer that only includes host settable IEs
 *
 * Given a buffer of IEs we move all Host Settable IEs to front of buffer
 * by overwriting the IEs that are not Host Settable.
 * Buffer length is adjusted accordingly.
 */
static
ssize_t uwb_rc_parse_host_settable_ie(struct uwb_dev *uwb_dev,
				      void *ie_data, size_t size)
{
	size_t new_len = size;
	struct uwb_ie_hdr *ie_hdr;
	size_t ie_length;
	void *itr = ie_data, *top = itr + size;

	while (itr < top) {
		if (uwb_rc_ie_verify(uwb_dev, ie_data, itr, top) != 0)
			break;
		ie_hdr = itr;
		ie_length = sizeof(*ie_hdr) + ie_hdr->length;
		if (uwb_rc_ie_is_host_settable(ie_hdr->element_id)) {
			itr += ie_length;
		} else {
			memmove(itr, itr + ie_length, top - (itr + ie_length));
			new_len -= ie_length;
			top -= ie_length;
		}
	}
	return new_len;
}


/* Cleanup the whole IE management subsystem */
void uwb_rc_ie_init(struct uwb_rc *uwb_rc)
{
	mutex_init(&uwb_rc->ies_mutex);
}


/**
 * Set up cache for host settable IEs currently being transmitted
 *
 * First we just call GET-IE to get the current IEs being transmitted
 * (or we workaround and pretend we did) and (because the format is
 * the same) reuse that as the IE cache (with the command prefix, as
 * explained in 'struct uwb_rc').
 *
 * @returns: size of cache created
 */
ssize_t uwb_rc_ie_setup(struct uwb_rc *uwb_rc)
{
	struct device *dev = &uwb_rc->uwb_dev.dev;
	ssize_t result;
	size_t capacity;
	struct uwb_rc_evt_get_ie *ie_info;

	d_fnstart(3, dev, "(%p)\n", uwb_rc);
	mutex_lock(&uwb_rc->ies_mutex);
	result = uwb_rc_get_ie(uwb_rc, &ie_info);
	if (result < 0)
		goto error_get_ie;
	capacity = result;
	d_printf(5, dev, "Got IEs %zu bytes (%zu long at %p)\n", result,
		 (size_t)le16_to_cpu(ie_info->wIELength), ie_info);

	/* Remove IEs that host should not set. */
	result = uwb_rc_parse_host_settable_ie(&uwb_rc->uwb_dev,
			ie_info->IEData, le16_to_cpu(ie_info->wIELength));
	if (result < 0)
		goto error_parse;
	d_printf(5, dev, "purged non-settable IEs to %zu bytes\n", result);
	uwb_rc->ies = (void *) ie_info;
	uwb_rc->ies->rccb.bCommandType = UWB_RC_CET_GENERAL;
	uwb_rc->ies->rccb.wCommand = cpu_to_le16(UWB_RC_CMD_SET_IE);
	uwb_rc->ies_capacity = capacity;
	d_printf(5, dev, "IE cache at %p %zu bytes, %zu capacity\n",
		 ie_info, result, capacity);
	result = 0;
error_parse:
error_get_ie:
	mutex_unlock(&uwb_rc->ies_mutex);
	d_fnend(3, dev, "(%p) = %zu\n", uwb_rc, result);
	return result;
}


/* Cleanup the whole IE management subsystem */
void uwb_rc_ie_release(struct uwb_rc *uwb_rc)
{
	kfree(uwb_rc->ies);
	uwb_rc->ies = NULL;
	uwb_rc->ies_capacity = 0;
}


static
int __acc_size(struct uwb_dev *uwb_dev, const struct uwb_ie_hdr *ie_hdr,
	       size_t offset, void *_ctx)
{
	size_t *acc_size = _ctx;
	*acc_size += sizeof(*ie_hdr) + ie_hdr->length;
	d_printf(6, &uwb_dev->dev, "new acc size %zu\n", *acc_size);
	return 0;
}


/**
 * Add a new IE to IEs currently being transmitted by device
 *
 * @ies: the buffer containing the new IE or IEs to be added to
 *       the device's beacon. The buffer will be verified for
 *       consistence (meaning the headers should be right) and
 *       consistent with the buffer size.
 * @size: size of @ies (in bytes, total buffer size)
 * @returns: 0 if ok, <0 errno code on error
 *
 * According to WHCI 0.95 [4.13.6] the driver will only receive the RCEB
 * after the device sent the first beacon that includes the IEs specified
 * in the SET IE command. We thus cannot send this command if the device is
 * not beaconing. Instead, a SET IE command will be sent later right after
 * we start beaconing.
 *
 * Setting an IE on the device will overwrite all current IEs in device. So
 * we take the current IEs being transmitted by the device, append the
 * new one, and call SET IE with all the IEs needed.
 *
 * The local IE cache will only be updated with the new IE if SET IE
 * completed successfully.
 */
int uwb_rc_ie_add(struct uwb_rc *uwb_rc,
		  const struct uwb_ie_hdr *ies, size_t size)
{
	int result = 0;
	struct device *dev = &uwb_rc->uwb_dev.dev;
	struct uwb_rc_cmd_set_ie *new_ies;
	size_t ies_size, total_size, acc_size = 0;

	if (uwb_rc->ies == NULL)
		return -ESHUTDOWN;
	uwb_ie_for_each(&uwb_rc->uwb_dev, __acc_size, &acc_size, ies, size);
	if (acc_size != size) {
		dev_err(dev, "BUG: bad IEs, misconstructed headers "
			"[%zu bytes reported vs %zu calculated]\n",
			size, acc_size);
		WARN_ON(1);
		return -EINVAL;
	}
	mutex_lock(&uwb_rc->ies_mutex);
	ies_size = le16_to_cpu(uwb_rc->ies->wIELength);
	total_size = sizeof(*uwb_rc->ies) + ies_size;
	if (total_size + size > uwb_rc->ies_capacity) {
		d_printf(4, dev, "Reallocating IE cache from %p capacity %zu "
			 "to capacity %zu\n", uwb_rc->ies, uwb_rc->ies_capacity,
			 total_size + size);
		new_ies = kzalloc(total_size + size, GFP_KERNEL);
		if (new_ies == NULL) {
			dev_err(dev, "No memory for adding new IE\n");
			result = -ENOMEM;
			goto error_alloc;
		}
		memcpy(new_ies, uwb_rc->ies, total_size);
		uwb_rc->ies_capacity = total_size + size;
		kfree(uwb_rc->ies);
		uwb_rc->ies = new_ies;
		d_printf(4, dev, "New IE cache at %p capacity %zu\n",
			 uwb_rc->ies, uwb_rc->ies_capacity);
	}
	memcpy((void *)uwb_rc->ies + total_size, ies, size);
	uwb_rc->ies->wIELength = cpu_to_le16(ies_size + size);
	if (uwb_rc->beaconing != -1) {
		result = uwb_rc_set_ie(uwb_rc, uwb_rc->ies);
		if (result < 0) {
			dev_err(dev, "Cannot set new IE on device: %d\n",
				result);
			uwb_rc->ies->wIELength = cpu_to_le16(ies_size);
		} else
			result = 0;
	}
	d_printf(4, dev, "IEs now occupy %hu bytes of %zu capacity at %p\n",
		 le16_to_cpu(uwb_rc->ies->wIELength), uwb_rc->ies_capacity,
		 uwb_rc->ies);
error_alloc:
	mutex_unlock(&uwb_rc->ies_mutex);
	return result;
}
EXPORT_SYMBOL_GPL(uwb_rc_ie_add);


/*
 * Remove an IE from internal cache
 *
 * We are dealing with our internal IE cache so no need to verify that the
 * IEs are valid (it has been done already).
 *
 * Should be called with ies_mutex held
 *
 * We do not break out once an IE is found in the cache. It is currently
 * possible to have more than one IE with the same ID included in the
 * beacon. We don't reallocate, we just mark the size smaller.
 */
static
int uwb_rc_ie_cache_rm(struct uwb_rc *uwb_rc, enum uwb_ie to_remove)
{
	struct uwb_ie_hdr *ie_hdr;
	size_t new_len = le16_to_cpu(uwb_rc->ies->wIELength);
	void *itr = uwb_rc->ies->IEData;
	void *top = itr + new_len;

	while (itr < top) {
		ie_hdr = itr;
		if (ie_hdr->element_id != to_remove) {
			itr += sizeof(*ie_hdr) + ie_hdr->length;
		} else {
			int ie_length;
			ie_length = sizeof(*ie_hdr) + ie_hdr->length;
			if (top - itr != ie_length)
				memmove(itr, itr + ie_length, top - itr + ie_length);
			top -= ie_length;
			new_len -= ie_length;
		}
	}
	uwb_rc->ies->wIELength = cpu_to_le16(new_len);
	return 0;
}


/**
 * Remove an IE currently being transmitted by device
 *
 * @element_id: id of IE to be removed from device's beacon
 */
int uwb_rc_ie_rm(struct uwb_rc *uwb_rc, enum uwb_ie element_id)
{
	struct device *dev = &uwb_rc->uwb_dev.dev;
	int result;

	if (uwb_rc->ies == NULL)
		return -ESHUTDOWN;
	mutex_lock(&uwb_rc->ies_mutex);
	result = uwb_rc_ie_cache_rm(uwb_rc, element_id);
	if (result < 0)
		dev_err(dev, "Cannot remove IE from cache.\n");
	if (uwb_rc->beaconing != -1) {
		result = uwb_rc_set_ie(uwb_rc, uwb_rc->ies);
		if (result < 0)
			dev_err(dev, "Cannot set new IE on device.\n");
	}
	mutex_unlock(&uwb_rc->ies_mutex);
	return result;
}
EXPORT_SYMBOL_GPL(uwb_rc_ie_rm);
