/*
 * Intel Wireless UWB Link 1480
 * MAC Firmware upload implementation
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
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
 * Implementation of the code for parsing the firmware file (extract
 * the headers and binary code chunks) in the fw_*() functions. The
 * code to upload pre and mac firmwares is the same, so it uses a
 * common entry point in __mac_fw_upload(), which uses the i1480
 * function pointers to push the firmware to the device.
 */
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/uwb.h>
#include "i1480-dfu.h"

/*
 * Descriptor for a continuous segment of MAC fw data
 */
struct fw_hdr {
	unsigned long address;
	size_t length;
	const u32 *bin;
	struct fw_hdr *next;
};


/* Free a chain of firmware headers */
static
void fw_hdrs_free(struct fw_hdr *hdr)
{
	struct fw_hdr *next;

	while (hdr) {
		next = hdr->next;
		kfree(hdr);
		hdr = next;
	}
}


/* Fill a firmware header descriptor from a memory buffer */
static
int fw_hdr_load(struct i1480 *i1480, struct fw_hdr *hdr, unsigned hdr_cnt,
		const char *_data, const u32 *data_itr, const u32 *data_top)
{
	size_t hdr_offset =  (const char *) data_itr - _data;
	size_t remaining_size = (void *) data_top - (void *) data_itr;
	if (data_itr + 2 > data_top) {
		dev_err(i1480->dev, "fw hdr #%u/%zu: EOF reached in header at "
		       "offset %zu, limit %zu\n",
		       hdr_cnt, hdr_offset,
		       (const char *) data_itr + 2 - _data,
		       (const char *) data_top - _data);
		return -EINVAL;
	}
	hdr->next = NULL;
	hdr->address = le32_to_cpu(*data_itr++);
	hdr->length = le32_to_cpu(*data_itr++);
	hdr->bin = data_itr;
	if (hdr->length > remaining_size) {
		dev_err(i1480->dev, "fw hdr #%u/%zu: EOF reached in data; "
		       "chunk too long (%zu bytes), only %zu left\n",
		       hdr_cnt, hdr_offset, hdr->length, remaining_size);
		return -EINVAL;
	}
	return 0;
}


/**
 * Get a buffer where the firmware is supposed to be and create a
 * chain of headers linking them together.
 *
 * @phdr: where to place the pointer to the first header (headers link
 *        to the next via the @hdr->next ptr); need to free the whole
 *        chain when done.
 *
 * @_data: Pointer to the data buffer.
 *
 * @_data_size: Size of the data buffer (bytes); data size has to be a
 *              multiple of 4. Function will fail if not.
 *
 * Goes over the whole binary blob; reads the first chunk and creates
 * a fw hdr from it (which points to where the data is in @_data and
 * the length of the chunk); then goes on to the next chunk until
 * done. Each header is linked to the next.
 */
static
int fw_hdrs_load(struct i1480 *i1480, struct fw_hdr **phdr,
		 const char *_data, size_t data_size)
{
	int result;
	unsigned hdr_cnt = 0;
	u32 *data = (u32 *) _data, *data_itr, *data_top;
	struct fw_hdr *hdr, **prev_hdr = phdr;

	result = -EINVAL;
	/* Check size is ok and pointer is aligned */
	if (data_size % sizeof(u32) != 0)
		goto error;
	if ((unsigned long) _data % sizeof(u16) != 0)
		goto error;
	*phdr = NULL;
	data_itr = data;
	data_top = (u32 *) (_data + data_size);
	while (data_itr < data_top) {
		result = -ENOMEM;
		hdr = kmalloc(sizeof(*hdr), GFP_KERNEL);
		if (hdr == NULL) {
			dev_err(i1480->dev, "Cannot allocate fw header "
			       "for chunk #%u\n", hdr_cnt);
			goto error_alloc;
		}
		result = fw_hdr_load(i1480, hdr, hdr_cnt,
				     _data, data_itr, data_top);
		if (result < 0)
			goto error_load;
		data_itr += 2 + hdr->length;
		*prev_hdr = hdr;
		prev_hdr = &hdr->next;
		hdr_cnt++;
	};
	*prev_hdr = NULL;
	return 0;

error_load:
	kfree(hdr);
error_alloc:
	fw_hdrs_free(*phdr);
error:
	return result;
}


/**
 * Compares a chunk of fw with one in the devices's memory
 *
 * @i1480:     Device instance
 * @hdr:     Pointer to the firmware chunk
 * @returns: 0 if equal, < 0 errno on error. If > 0, it is the offset
 *           where the difference was found (plus one).
 *
 * Kind of dirty and simplistic, but does the trick in both the PCI
 * and USB version. We do a quick[er] memcmp(), and if it fails, we do
 * a byte-by-byte to find the offset.
 */
static
ssize_t i1480_fw_cmp(struct i1480 *i1480, struct fw_hdr *hdr)
{
	ssize_t result = 0;
	u32 src_itr = 0, cnt;
	size_t size = hdr->length*sizeof(hdr->bin[0]);
	size_t chunk_size;
	u8 *bin = (u8 *) hdr->bin;

	while (size > 0) {
		chunk_size = size < i1480->buf_size ? size : i1480->buf_size;
		result = i1480->read(i1480, hdr->address + src_itr, chunk_size);
		if (result < 0) {
			dev_err(i1480->dev, "error reading for verification: "
				"%zd\n", result);
			goto error;
		}
		if (memcmp(i1480->cmd_buf, bin + src_itr, result)) {
			u8 *buf = i1480->cmd_buf;
			for (cnt = 0; cnt < result; cnt++)
				if (bin[src_itr + cnt] != buf[cnt]) {
					dev_err(i1480->dev, "byte failed at "
						"src_itr %u cnt %u [0x%02x "
						"vs 0x%02x]\n", src_itr, cnt,
						bin[src_itr + cnt], buf[cnt]);
					result = src_itr + cnt + 1;
					goto cmp_failed;
				}
		}
		src_itr += result;
		size -= result;
	}
	result = 0;
error:
cmp_failed:
	return result;
}


/**
 * Writes firmware headers to the device.
 *
 * @prd:     PRD instance
 * @hdr:     Processed firmware
 * @returns: 0 if ok, < 0 errno on error.
 */
static
int mac_fw_hdrs_push(struct i1480 *i1480, struct fw_hdr *hdr,
		     const char *fw_name, const char *fw_tag)
{
	struct device *dev = i1480->dev;
	ssize_t result = 0;
	struct fw_hdr *hdr_itr;
	int verif_retry_count;

	/* Now, header by header, push them to the hw */
	for (hdr_itr = hdr; hdr_itr != NULL; hdr_itr = hdr_itr->next) {
		verif_retry_count = 0;
retry:
		dev_dbg(dev, "fw chunk (%zu @ 0x%08lx)\n",
			hdr_itr->length * sizeof(hdr_itr->bin[0]),
			hdr_itr->address);
		result = i1480->write(i1480, hdr_itr->address, hdr_itr->bin,
				    hdr_itr->length*sizeof(hdr_itr->bin[0]));
		if (result < 0) {
			dev_err(dev, "%s fw '%s': write failed (%zuB @ 0x%lx):"
				" %zd\n", fw_tag, fw_name,
				hdr_itr->length * sizeof(hdr_itr->bin[0]),
				hdr_itr->address, result);
			break;
		}
		result = i1480_fw_cmp(i1480, hdr_itr);
		if (result < 0) {
			dev_err(dev, "%s fw '%s': verification read "
				"failed (%zuB @ 0x%lx): %zd\n",
				fw_tag, fw_name,
				hdr_itr->length * sizeof(hdr_itr->bin[0]),
				hdr_itr->address, result);
			break;
		}
		if (result > 0) {	/* Offset where it failed + 1 */
			result--;
			dev_err(dev, "%s fw '%s': WARNING: verification "
				"failed at 0x%lx: retrying\n",
				fw_tag, fw_name, hdr_itr->address + result);
			if (++verif_retry_count < 3)
				goto retry;	/* write this block again! */
			dev_err(dev, "%s fw '%s': verification failed at 0x%lx: "
				"tried %d times\n", fw_tag, fw_name,
				hdr_itr->address + result, verif_retry_count);
			result = -EINVAL;
			break;
		}
	}
	return result;
}


/** Puts the device in firmware upload mode.*/
static
int mac_fw_upload_enable(struct i1480 *i1480)
{
	int result;
	u32 reg = 0x800000c0;
	u32 *buffer = (u32 *)i1480->cmd_buf;

	if (i1480->hw_rev > 1)
		reg = 0x8000d0d4;
	result = i1480->read(i1480, reg, sizeof(u32));
	if (result < 0)
		goto error_cmd;
	*buffer &= ~i1480_FW_UPLOAD_MODE_MASK;
	result = i1480->write(i1480, reg, buffer, sizeof(u32));
	if (result < 0)
		goto error_cmd;
	return 0;
error_cmd:
	dev_err(i1480->dev, "can't enable fw upload mode: %d\n", result);
	return result;
}


/** Gets the device out of firmware upload mode. */
static
int mac_fw_upload_disable(struct i1480 *i1480)
{
	int result;
	u32 reg = 0x800000c0;
	u32 *buffer = (u32 *)i1480->cmd_buf;

	if (i1480->hw_rev > 1)
		reg = 0x8000d0d4;
	result = i1480->read(i1480, reg, sizeof(u32));
	if (result < 0)
		goto error_cmd;
	*buffer |= i1480_FW_UPLOAD_MODE_MASK;
	result = i1480->write(i1480, reg, buffer, sizeof(u32));
	if (result < 0)
		goto error_cmd;
	return 0;
error_cmd:
	dev_err(i1480->dev, "can't disable fw upload mode: %d\n", result);
	return result;
}



/**
 * Generic function for uploading a MAC firmware.
 *
 * @i1480:     Device instance
 * @fw_name: Name of firmware file to upload.
 * @fw_tag:  Name of the firmware type (for messages)
 *           [eg: MAC, PRE]
 * @do_wait: Wait for device to emit initialization done message (0
 *           for PRE fws, 1 for MAC fws).
 * @returns: 0 if ok, < 0 errno on error.
 */
static
int __mac_fw_upload(struct i1480 *i1480, const char *fw_name,
		    const char *fw_tag)
{
	int result;
	const struct firmware *fw;
	struct fw_hdr *fw_hdrs;

	result = request_firmware(&fw, fw_name, i1480->dev);
	if (result < 0)	/* Up to caller to complain on -ENOENT */
		goto out;
	result = fw_hdrs_load(i1480, &fw_hdrs, fw->data, fw->size);
	if (result < 0) {
		dev_err(i1480->dev, "%s fw '%s': failed to parse firmware "
			"file: %d\n", fw_tag, fw_name, result);
		goto out_release;
	}
	result = mac_fw_upload_enable(i1480);
	if (result < 0)
		goto out_hdrs_release;
	result = mac_fw_hdrs_push(i1480, fw_hdrs, fw_name, fw_tag);
	mac_fw_upload_disable(i1480);
out_hdrs_release:
	if (result >= 0)
		dev_info(i1480->dev, "%s fw '%s': uploaded\n", fw_tag, fw_name);
	else
		dev_err(i1480->dev, "%s fw '%s': failed to upload (%d), "
			"power cycle device\n", fw_tag, fw_name, result);
	fw_hdrs_free(fw_hdrs);
out_release:
	release_firmware(fw);
out:
	return result;
}


/**
 * Upload a pre-PHY firmware
 *
 */
int i1480_pre_fw_upload(struct i1480 *i1480)
{
	int result;
	result = __mac_fw_upload(i1480, i1480->pre_fw_name, "PRE");
	if (result == 0)
		msleep(400);
	return result;
}


/**
 * Reset a the MAC and PHY
 *
 * @i1480:     Device's instance
 * @returns: 0 if ok, < 0 errno code on error
 *
 * We put the command on kmalloc'ed memory as some arches cannot do
 * USB from the stack. The reply event is copied from an stage buffer,
 * so it can be in the stack. See WUSB1.0[8.6.2.4] for more details.
 *
 * We issue the reset to make sure the UWB controller reinits the PHY;
 * this way we can now if the PHY init went ok.
 */
static
int i1480_cmd_reset(struct i1480 *i1480)
{
	int result;
	struct uwb_rccb *cmd = (void *) i1480->cmd_buf;
	struct i1480_evt_reset {
		struct uwb_rceb rceb;
		u8 bResultCode;
	} __attribute__((packed)) *reply = (void *) i1480->evt_buf;

	result = -ENOMEM;
	cmd->bCommandType = UWB_RC_CET_GENERAL;
	cmd->wCommand = cpu_to_le16(UWB_RC_CMD_RESET);
	reply->rceb.bEventType = UWB_RC_CET_GENERAL;
	reply->rceb.wEvent = UWB_RC_CMD_RESET;
	result = i1480_cmd(i1480, "RESET", sizeof(*cmd), sizeof(*reply));
	if (result < 0)
		goto out;
	if (reply->bResultCode != UWB_RC_RES_SUCCESS) {
		dev_err(i1480->dev, "RESET: command execution failed: %u\n",
			reply->bResultCode);
		result = -EIO;
	}
out:
	return result;

}


/* Wait for the MAC FW to start running */
static
int i1480_fw_is_running_q(struct i1480 *i1480)
{
	int cnt = 0;
	int result;
	u32 *val = (u32 *) i1480->cmd_buf;

	for (cnt = 0; cnt < 10; cnt++) {
		msleep(100);
		result = i1480->read(i1480, 0x80080000, 4);
		if (result < 0) {
			dev_err(i1480->dev, "Can't read 0x8008000: %d\n", result);
			goto out;
		}
		if (*val == 0x55555555UL)	/* fw running? cool */
			goto out;
	}
	dev_err(i1480->dev, "Timed out waiting for fw to start\n");
	result = -ETIMEDOUT;
out:
	return result;

}


/**
 * Upload MAC firmware, wait for it to start
 *
 * @i1480:     Device instance
 * @fw_name: Name of the file that contains the firmware
 *
 * This has to be called after the pre fw has been uploaded (if
 * there is any).
 */
int i1480_mac_fw_upload(struct i1480 *i1480)
{
	int result = 0, deprecated_name = 0;
	struct i1480_rceb *rcebe = (void *) i1480->evt_buf;

	result = __mac_fw_upload(i1480, i1480->mac_fw_name, "MAC");
	if (result == -ENOENT) {
		result = __mac_fw_upload(i1480, i1480->mac_fw_name_deprecate,
					 "MAC");
		deprecated_name = 1;
	}
	if (result < 0)
		return result;
	if (deprecated_name == 1)
		dev_warn(i1480->dev,
			 "WARNING: firmware file name %s is deprecated, "
			 "please rename to %s\n",
			 i1480->mac_fw_name_deprecate, i1480->mac_fw_name);
	result = i1480_fw_is_running_q(i1480);
	if (result < 0)
		goto error_fw_not_running;
	result = i1480->rc_setup ? i1480->rc_setup(i1480) : 0;
	if (result < 0) {
		dev_err(i1480->dev, "Cannot setup after MAC fw upload: %d\n",
			result);
		goto error_setup;
	}
	result = i1480->wait_init_done(i1480);	/* wait init'on */
	if (result < 0) {
		dev_err(i1480->dev, "MAC fw '%s': Initialization timed out "
			"(%d)\n", i1480->mac_fw_name, result);
		goto error_init_timeout;
	}
	/* verify we got the right initialization done event */
	if (i1480->evt_result != sizeof(*rcebe)) {
		dev_err(i1480->dev, "MAC fw '%s': initialization event returns "
			"wrong size (%zu bytes vs %zu needed)\n",
			i1480->mac_fw_name, i1480->evt_result, sizeof(*rcebe));
		goto error_size;
	}
	result = -EIO;
	if (i1480_rceb_check(i1480, &rcebe->rceb, NULL, 0, i1480_CET_VS1,
			     i1480_EVT_RM_INIT_DONE) < 0) {
		dev_err(i1480->dev, "wrong initialization event 0x%02x/%04x/%02x "
			"received; expected 0x%02x/%04x/00\n",
			rcebe->rceb.bEventType, le16_to_cpu(rcebe->rceb.wEvent),
			rcebe->rceb.bEventContext, i1480_CET_VS1,
			i1480_EVT_RM_INIT_DONE);
		goto error_init_timeout;
	}
	result = i1480_cmd_reset(i1480);
	if (result < 0)
		dev_err(i1480->dev, "MAC fw '%s': MBOA reset failed (%d)\n",
			i1480->mac_fw_name, result);
error_fw_not_running:
error_init_timeout:
error_size:
error_setup:
	return result;
}
