/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/slab.h>
#include <linux/export.h>

#include "debug.h"
#include "acx.h"
#include "boot.h"
#include "io.h"
#include "event.h"
#include "rx.h"
#include "hw_ops.h"

static int wl1271_boot_set_ecpu_ctrl(struct wl1271 *wl, u32 flag)
{
	u32 cpu_ctrl;
	int ret;

	/* 10.5.0 run the firmware (I) */
	ret = wlcore_read_reg(wl, REG_ECPU_CONTROL, &cpu_ctrl);
	if (ret < 0)
		goto out;

	/* 10.5.1 run the firmware (II) */
	cpu_ctrl |= flag;
	ret = wlcore_write_reg(wl, REG_ECPU_CONTROL, cpu_ctrl);

out:
	return ret;
}

static int wlcore_boot_parse_fw_ver(struct wl1271 *wl,
				    struct wl1271_static_data *static_data)
{
	int ret;

	strncpy(wl->chip.fw_ver_str, static_data->fw_version,
		sizeof(wl->chip.fw_ver_str));

	/* make sure the string is NULL-terminated */
	wl->chip.fw_ver_str[sizeof(wl->chip.fw_ver_str) - 1] = '\0';

	ret = sscanf(wl->chip.fw_ver_str + 4, "%u.%u.%u.%u.%u",
		     &wl->chip.fw_ver[0], &wl->chip.fw_ver[1],
		     &wl->chip.fw_ver[2], &wl->chip.fw_ver[3],
		     &wl->chip.fw_ver[4]);

	if (ret != 5) {
		wl1271_warning("fw version incorrect value");
		memset(wl->chip.fw_ver, 0, sizeof(wl->chip.fw_ver));
		ret = -EINVAL;
		goto out;
	}

	ret = wlcore_identify_fw(wl);
	if (ret < 0)
		goto out;
out:
	return ret;
}

static int wlcore_validate_fw_ver(struct wl1271 *wl)
{
	unsigned int *fw_ver = wl->chip.fw_ver;
	unsigned int *min_ver = (wl->fw_type == WL12XX_FW_TYPE_MULTI) ?
		wl->min_mr_fw_ver : wl->min_sr_fw_ver;
	char min_fw_str[32] = "";
	int i;

	/* the chip must be exactly equal */
	if ((min_ver[FW_VER_CHIP] != WLCORE_FW_VER_IGNORE) &&
	    (min_ver[FW_VER_CHIP] != fw_ver[FW_VER_CHIP]))
		goto fail;

	/* the firmware type must be equal */
	if ((min_ver[FW_VER_IF_TYPE] != WLCORE_FW_VER_IGNORE) &&
	    (min_ver[FW_VER_IF_TYPE] != fw_ver[FW_VER_IF_TYPE]))
		goto fail;

	/* the project number must be equal */
	if ((min_ver[FW_VER_SUBTYPE] != WLCORE_FW_VER_IGNORE) &&
	    (min_ver[FW_VER_SUBTYPE] != fw_ver[FW_VER_SUBTYPE]))
		goto fail;

	/* the API version must be greater or equal */
	if ((min_ver[FW_VER_MAJOR] != WLCORE_FW_VER_IGNORE) &&
		 (min_ver[FW_VER_MAJOR] > fw_ver[FW_VER_MAJOR]))
		goto fail;

	/* if the API version is equal... */
	if (((min_ver[FW_VER_MAJOR] == WLCORE_FW_VER_IGNORE) ||
	     (min_ver[FW_VER_MAJOR] == fw_ver[FW_VER_MAJOR])) &&
	    /* ...the minor must be greater or equal */
	    ((min_ver[FW_VER_MINOR] != WLCORE_FW_VER_IGNORE) &&
	     (min_ver[FW_VER_MINOR] > fw_ver[FW_VER_MINOR])))
		goto fail;

	return 0;

fail:
	for (i = 0; i < NUM_FW_VER; i++)
		if (min_ver[i] == WLCORE_FW_VER_IGNORE)
			snprintf(min_fw_str, sizeof(min_fw_str),
				  "%s*.", min_fw_str);
		else
			snprintf(min_fw_str, sizeof(min_fw_str),
				  "%s%u.", min_fw_str, min_ver[i]);

	wl1271_error("Your WiFi FW version (%u.%u.%u.%u.%u) is invalid.\n"
		     "Please use at least FW %s\n"
		     "You can get the latest firmwares at:\n"
		     "git://git.ti.com/wilink8-wlan/wl18xx_fw.git",
		     fw_ver[FW_VER_CHIP], fw_ver[FW_VER_IF_TYPE],
		     fw_ver[FW_VER_MAJOR], fw_ver[FW_VER_SUBTYPE],
		     fw_ver[FW_VER_MINOR], min_fw_str);
	return -EINVAL;
}

static int wlcore_boot_static_data(struct wl1271 *wl)
{
	struct wl1271_static_data *static_data;
	size_t len = sizeof(*static_data) + wl->static_data_priv_len;
	int ret;

	static_data = kmalloc(len, GFP_KERNEL);
	if (!static_data) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wlcore_read(wl, wl->cmd_box_addr, static_data, len, false);
	if (ret < 0)
		goto out_free;

	ret = wlcore_boot_parse_fw_ver(wl, static_data);
	if (ret < 0)
		goto out_free;

	ret = wlcore_validate_fw_ver(wl);
	if (ret < 0)
		goto out_free;

	ret = wlcore_handle_static_data(wl, static_data);
	if (ret < 0)
		goto out_free;

out_free:
	kfree(static_data);
out:
	return ret;
}

static int wl1271_boot_upload_firmware_chunk(struct wl1271 *wl, void *buf,
					     size_t fw_data_len, u32 dest)
{
	struct wlcore_partition_set partition;
	int addr, chunk_num, partition_limit;
	u8 *p, *chunk;
	int ret;

	/* whal_FwCtrl_LoadFwImageSm() */

	wl1271_debug(DEBUG_BOOT, "starting firmware upload");

	wl1271_debug(DEBUG_BOOT, "fw_data_len %zd chunk_size %d",
		     fw_data_len, CHUNK_SIZE);

	if ((fw_data_len % 4) != 0) {
		wl1271_error("firmware length not multiple of four");
		return -EIO;
	}

	chunk = kmalloc(CHUNK_SIZE, GFP_KERNEL);
	if (!chunk) {
		wl1271_error("allocation for firmware upload chunk failed");
		return -ENOMEM;
	}

	memcpy(&partition, &wl->ptable[PART_DOWN], sizeof(partition));
	partition.mem.start = dest;
	ret = wlcore_set_partition(wl, &partition);
	if (ret < 0)
		goto out;

	/* 10.1 set partition limit and chunk num */
	chunk_num = 0;
	partition_limit = wl->ptable[PART_DOWN].mem.size;

	while (chunk_num < fw_data_len / CHUNK_SIZE) {
		/* 10.2 update partition, if needed */
		addr = dest + (chunk_num + 2) * CHUNK_SIZE;
		if (addr > partition_limit) {
			addr = dest + chunk_num * CHUNK_SIZE;
			partition_limit = chunk_num * CHUNK_SIZE +
				wl->ptable[PART_DOWN].mem.size;
			partition.mem.start = addr;
			ret = wlcore_set_partition(wl, &partition);
			if (ret < 0)
				goto out;
		}

		/* 10.3 upload the chunk */
		addr = dest + chunk_num * CHUNK_SIZE;
		p = buf + chunk_num * CHUNK_SIZE;
		memcpy(chunk, p, CHUNK_SIZE);
		wl1271_debug(DEBUG_BOOT, "uploading fw chunk 0x%p to 0x%x",
			     p, addr);
		ret = wlcore_write(wl, addr, chunk, CHUNK_SIZE, false);
		if (ret < 0)
			goto out;

		chunk_num++;
	}

	/* 10.4 upload the last chunk */
	addr = dest + chunk_num * CHUNK_SIZE;
	p = buf + chunk_num * CHUNK_SIZE;
	memcpy(chunk, p, fw_data_len % CHUNK_SIZE);
	wl1271_debug(DEBUG_BOOT, "uploading fw last chunk (%zd B) 0x%p to 0x%x",
		     fw_data_len % CHUNK_SIZE, p, addr);
	ret = wlcore_write(wl, addr, chunk, fw_data_len % CHUNK_SIZE, false);

out:
	kfree(chunk);
	return ret;
}

int wlcore_boot_upload_firmware(struct wl1271 *wl)
{
	u32 chunks, addr, len;
	int ret = 0;
	u8 *fw;

	fw = wl->fw;
	chunks = be32_to_cpup((__be32 *) fw);
	fw += sizeof(u32);

	wl1271_debug(DEBUG_BOOT, "firmware chunks to be uploaded: %u", chunks);

	while (chunks--) {
		addr = be32_to_cpup((__be32 *) fw);
		fw += sizeof(u32);
		len = be32_to_cpup((__be32 *) fw);
		fw += sizeof(u32);

		if (len > 300000) {
			wl1271_info("firmware chunk too long: %u", len);
			return -EINVAL;
		}
		wl1271_debug(DEBUG_BOOT, "chunk %d addr 0x%x len %u",
			     chunks, addr, len);
		ret = wl1271_boot_upload_firmware_chunk(wl, fw, len, addr);
		if (ret != 0)
			break;
		fw += len;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(wlcore_boot_upload_firmware);

int wlcore_boot_upload_nvs(struct wl1271 *wl)
{
	size_t nvs_len, burst_len;
	int i;
	u32 dest_addr, val;
	u8 *nvs_ptr, *nvs_aligned;
	int ret;

	if (wl->nvs == NULL) {
		wl1271_error("NVS file is needed during boot");
		return -ENODEV;
	}

	if (wl->quirks & WLCORE_QUIRK_LEGACY_NVS) {
		struct wl1271_nvs_file *nvs =
			(struct wl1271_nvs_file *)wl->nvs;
		/*
		 * FIXME: the LEGACY NVS image support (NVS's missing the 5GHz
		 * band configurations) can be removed when those NVS files stop
		 * floating around.
		 */
		if (wl->nvs_len == sizeof(struct wl1271_nvs_file) ||
		    wl->nvs_len == WL1271_INI_LEGACY_NVS_FILE_SIZE) {
			if (nvs->general_params.dual_mode_select)
				wl->enable_11a = true;
		}

		if (wl->nvs_len != sizeof(struct wl1271_nvs_file) &&
		    (wl->nvs_len != WL1271_INI_LEGACY_NVS_FILE_SIZE ||
		     wl->enable_11a)) {
			wl1271_error("nvs size is not as expected: %zu != %zu",
				wl->nvs_len, sizeof(struct wl1271_nvs_file));
			kfree(wl->nvs);
			wl->nvs = NULL;
			wl->nvs_len = 0;
			return -EILSEQ;
		}

		/* only the first part of the NVS needs to be uploaded */
		nvs_len = sizeof(nvs->nvs);
		nvs_ptr = (u8 *) nvs->nvs;
	} else {
		struct wl128x_nvs_file *nvs = (struct wl128x_nvs_file *)wl->nvs;

		if (wl->nvs_len == sizeof(struct wl128x_nvs_file)) {
			if (nvs->general_params.dual_mode_select)
				wl->enable_11a = true;
		} else {
			wl1271_error("nvs size is not as expected: %zu != %zu",
				     wl->nvs_len,
				     sizeof(struct wl128x_nvs_file));
			kfree(wl->nvs);
			wl->nvs = NULL;
			wl->nvs_len = 0;
			return -EILSEQ;
		}

		/* only the first part of the NVS needs to be uploaded */
		nvs_len = sizeof(nvs->nvs);
		nvs_ptr = (u8 *)nvs->nvs;
	}

	/* update current MAC address to NVS */
	nvs_ptr[11] = wl->addresses[0].addr[0];
	nvs_ptr[10] = wl->addresses[0].addr[1];
	nvs_ptr[6] = wl->addresses[0].addr[2];
	nvs_ptr[5] = wl->addresses[0].addr[3];
	nvs_ptr[4] = wl->addresses[0].addr[4];
	nvs_ptr[3] = wl->addresses[0].addr[5];

	/*
	 * Layout before the actual NVS tables:
	 * 1 byte : burst length.
	 * 2 bytes: destination address.
	 * n bytes: data to burst copy.
	 *
	 * This is ended by a 0 length, then the NVS tables.
	 */

	/* FIXME: Do we need to check here whether the LSB is 1? */
	while (nvs_ptr[0]) {
		burst_len = nvs_ptr[0];
		dest_addr = (nvs_ptr[1] & 0xfe) | ((u32)(nvs_ptr[2] << 8));

		/*
		 * Due to our new wl1271_translate_reg_addr function,
		 * we need to add the register partition start address
		 * to the destination
		 */
		dest_addr += wl->curr_part.reg.start;

		/* We move our pointer to the data */
		nvs_ptr += 3;

		for (i = 0; i < burst_len; i++) {
			if (nvs_ptr + 3 >= (u8 *) wl->nvs + nvs_len)
				goto out_badnvs;

			val = (nvs_ptr[0] | (nvs_ptr[1] << 8)
			       | (nvs_ptr[2] << 16) | (nvs_ptr[3] << 24));

			wl1271_debug(DEBUG_BOOT,
				     "nvs burst write 0x%x: 0x%x",
				     dest_addr, val);
			ret = wlcore_write32(wl, dest_addr, val);
			if (ret < 0)
				return ret;

			nvs_ptr += 4;
			dest_addr += 4;
		}

		if (nvs_ptr >= (u8 *) wl->nvs + nvs_len)
			goto out_badnvs;
	}

	/*
	 * We've reached the first zero length, the first NVS table
	 * is located at an aligned offset which is at least 7 bytes further.
	 * NOTE: The wl->nvs->nvs element must be first, in order to
	 * simplify the casting, we assume it is at the beginning of
	 * the wl->nvs structure.
	 */
	nvs_ptr = (u8 *)wl->nvs +
			ALIGN(nvs_ptr - (u8 *)wl->nvs + 7, 4);

	if (nvs_ptr >= (u8 *) wl->nvs + nvs_len)
		goto out_badnvs;

	nvs_len -= nvs_ptr - (u8 *)wl->nvs;

	/* Now we must set the partition correctly */
	ret = wlcore_set_partition(wl, &wl->ptable[PART_WORK]);
	if (ret < 0)
		return ret;

	/* Copy the NVS tables to a new block to ensure alignment */
	nvs_aligned = kmemdup(nvs_ptr, nvs_len, GFP_KERNEL);
	if (!nvs_aligned)
		return -ENOMEM;

	/* And finally we upload the NVS tables */
	ret = wlcore_write_data(wl, REG_CMD_MBOX_ADDRESS, nvs_aligned, nvs_len,
				false);

	kfree(nvs_aligned);
	return ret;

out_badnvs:
	wl1271_error("nvs data is malformed");
	return -EILSEQ;
}
EXPORT_SYMBOL_GPL(wlcore_boot_upload_nvs);

int wlcore_boot_run_firmware(struct wl1271 *wl)
{
	int loop, ret;
	u32 chip_id, intr;

	/* Make sure we have the boot partition */
	ret = wlcore_set_partition(wl, &wl->ptable[PART_BOOT]);
	if (ret < 0)
		return ret;

	ret = wl1271_boot_set_ecpu_ctrl(wl, ECPU_CONTROL_HALT);
	if (ret < 0)
		return ret;

	ret = wlcore_read_reg(wl, REG_CHIP_ID_B, &chip_id);
	if (ret < 0)
		return ret;

	wl1271_debug(DEBUG_BOOT, "chip id after firmware boot: 0x%x", chip_id);

	if (chip_id != wl->chip.id) {
		wl1271_error("chip id doesn't match after firmware boot");
		return -EIO;
	}

	/* wait for init to complete */
	loop = 0;
	while (loop++ < INIT_LOOP) {
		udelay(INIT_LOOP_DELAY);
		ret = wlcore_read_reg(wl, REG_INTERRUPT_NO_CLEAR, &intr);
		if (ret < 0)
			return ret;

		if (intr == 0xffffffff) {
			wl1271_error("error reading hardware complete "
				     "init indication");
			return -EIO;
		}
		/* check that ACX_INTR_INIT_COMPLETE is enabled */
		else if (intr & WL1271_ACX_INTR_INIT_COMPLETE) {
			ret = wlcore_write_reg(wl, REG_INTERRUPT_ACK,
					       WL1271_ACX_INTR_INIT_COMPLETE);
			if (ret < 0)
				return ret;
			break;
		}
	}

	if (loop > INIT_LOOP) {
		wl1271_error("timeout waiting for the hardware to "
			     "complete initialization");
		return -EIO;
	}

	/* get hardware config command mail box */
	ret = wlcore_read_reg(wl, REG_COMMAND_MAILBOX_PTR, &wl->cmd_box_addr);
	if (ret < 0)
		return ret;

	wl1271_debug(DEBUG_MAILBOX, "cmd_box_addr 0x%x", wl->cmd_box_addr);

	/* get hardware config event mail box */
	ret = wlcore_read_reg(wl, REG_EVENT_MAILBOX_PTR, &wl->mbox_ptr[0]);
	if (ret < 0)
		return ret;

	wl->mbox_ptr[1] = wl->mbox_ptr[0] + wl->mbox_size;

	wl1271_debug(DEBUG_MAILBOX, "MBOX ptrs: 0x%x 0x%x",
		     wl->mbox_ptr[0], wl->mbox_ptr[1]);

	ret = wlcore_boot_static_data(wl);
	if (ret < 0) {
		wl1271_error("error getting static data");
		return ret;
	}

	/*
	 * in case of full asynchronous mode the firmware event must be
	 * ready to receive event from the command mailbox
	 */

	/* unmask required mbox events  */
	ret = wl1271_event_unmask(wl);
	if (ret < 0) {
		wl1271_error("EVENT mask setting failed");
		return ret;
	}

	/* set the working partition to its "running" mode offset */
	ret = wlcore_set_partition(wl, &wl->ptable[PART_WORK]);

	/* firmware startup completed */
	return ret;
}
EXPORT_SYMBOL_GPL(wlcore_boot_run_firmware);
