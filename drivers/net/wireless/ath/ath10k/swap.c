/*
 * Copyright (c) 2015 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* This file has implementation for code swap logic. With code swap feature,
 * target can run the fw binary with even smaller IRAM size by using host
 * memory to store some of the code segments.
 */

#include "core.h"
#include "bmi.h"
#include "debug.h"

static int ath10k_swap_code_seg_fill(struct ath10k *ar,
				     struct ath10k_swap_code_seg_info *seg_info,
				     const void *data, size_t data_len)
{
	u8 *virt_addr = seg_info->virt_address[0];
	u8 swap_magic[ATH10K_SWAP_CODE_SEG_MAGIC_BYTES_SZ] = {};
	const u8 *fw_data = data;
	union ath10k_swap_code_seg_item *swap_item;
	u32 length = 0;
	u32 payload_len;
	u32 total_payload_len = 0;
	u32 size_left = data_len;

	/* Parse swap bin and copy the content to host allocated memory.
	 * The format is Address, length and value. The last 4-bytes is
	 * target write address. Currently address field is not used.
	 */
	seg_info->target_addr = -1;
	while (size_left >= sizeof(*swap_item)) {
		swap_item = (union ath10k_swap_code_seg_item *)fw_data;
		payload_len = __le32_to_cpu(swap_item->tlv.length);
		if ((payload_len > size_left) ||
		    (payload_len == 0 &&
		     size_left != sizeof(struct ath10k_swap_code_seg_tail))) {
			ath10k_err(ar, "refusing to parse invalid tlv length %d\n",
				   payload_len);
			return -EINVAL;
		}

		if (payload_len == 0) {
			if (memcmp(swap_item->tail.magic_signature, swap_magic,
				   ATH10K_SWAP_CODE_SEG_MAGIC_BYTES_SZ)) {
				ath10k_err(ar, "refusing an invalid swap file\n");
				return -EINVAL;
			}
			seg_info->target_addr =
				__le32_to_cpu(swap_item->tail.bmi_write_addr);
			break;
		}

		memcpy(virt_addr, swap_item->tlv.data, payload_len);
		virt_addr += payload_len;
		length = payload_len +  sizeof(struct ath10k_swap_code_seg_tlv);
		size_left -= length;
		fw_data += length;
		total_payload_len += payload_len;
	}

	if (seg_info->target_addr == -1) {
		ath10k_err(ar, "failed to parse invalid swap file\n");
		return -EINVAL;
	}
	seg_info->seg_hw_info.swap_size = __cpu_to_le32(total_payload_len);

	return 0;
}

static void
ath10k_swap_code_seg_free(struct ath10k *ar,
			  struct ath10k_swap_code_seg_info *seg_info)
{
	u32 seg_size;

	if (!seg_info)
		return;

	if (!seg_info->virt_address[0])
		return;

	seg_size = __le32_to_cpu(seg_info->seg_hw_info.size);
	dma_free_coherent(ar->dev, seg_size, seg_info->virt_address[0],
			  seg_info->paddr[0]);
}

static struct ath10k_swap_code_seg_info *
ath10k_swap_code_seg_alloc(struct ath10k *ar, size_t swap_bin_len)
{
	struct ath10k_swap_code_seg_info *seg_info;
	void *virt_addr;
	dma_addr_t paddr;

	swap_bin_len = roundup(swap_bin_len, 2);
	if (swap_bin_len > ATH10K_SWAP_CODE_SEG_BIN_LEN_MAX) {
		ath10k_err(ar, "refusing code swap bin because it is too big %zu > %d\n",
			   swap_bin_len, ATH10K_SWAP_CODE_SEG_BIN_LEN_MAX);
		return NULL;
	}

	seg_info = devm_kzalloc(ar->dev, sizeof(*seg_info), GFP_KERNEL);
	if (!seg_info)
		return NULL;

	virt_addr = dma_alloc_coherent(ar->dev, swap_bin_len, &paddr,
				       GFP_KERNEL);
	if (!virt_addr) {
		ath10k_err(ar, "failed to allocate dma coherent memory\n");
		return NULL;
	}

	seg_info->seg_hw_info.bus_addr[0] = __cpu_to_le32(paddr);
	seg_info->seg_hw_info.size = __cpu_to_le32(swap_bin_len);
	seg_info->seg_hw_info.swap_size = __cpu_to_le32(swap_bin_len);
	seg_info->seg_hw_info.num_segs =
			__cpu_to_le32(ATH10K_SWAP_CODE_SEG_NUM_SUPPORTED);
	seg_info->seg_hw_info.size_log2 = __cpu_to_le32(ilog2(swap_bin_len));
	seg_info->virt_address[0] = virt_addr;
	seg_info->paddr[0] = paddr;

	return seg_info;
}

int ath10k_swap_code_seg_configure(struct ath10k *ar,
				   const struct ath10k_fw_file *fw_file)
{
	int ret;
	struct ath10k_swap_code_seg_info *seg_info = NULL;

	if (!fw_file->firmware_swap_code_seg_info)
		return 0;

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot found firmware code swap binary\n");

	seg_info = fw_file->firmware_swap_code_seg_info;

	ret = ath10k_bmi_write_memory(ar, seg_info->target_addr,
				      &seg_info->seg_hw_info,
				      sizeof(seg_info->seg_hw_info));
	if (ret) {
		ath10k_err(ar, "failed to write Code swap segment information (%d)\n",
			   ret);
		return ret;
	}

	return 0;
}

void ath10k_swap_code_seg_release(struct ath10k *ar,
				  struct ath10k_fw_file *fw_file)
{
	ath10k_swap_code_seg_free(ar, fw_file->firmware_swap_code_seg_info);

	/* FIXME: these two assignments look to bein wrong place! Shouldn't
	 * they be in ath10k_core_free_firmware_files() like the rest?
	 */
	fw_file->codeswap_data = NULL;
	fw_file->codeswap_len = 0;

	fw_file->firmware_swap_code_seg_info = NULL;
}

int ath10k_swap_code_seg_init(struct ath10k *ar, struct ath10k_fw_file *fw_file)
{
	int ret;
	struct ath10k_swap_code_seg_info *seg_info;
	const void *codeswap_data;
	size_t codeswap_len;

	codeswap_data = fw_file->codeswap_data;
	codeswap_len = fw_file->codeswap_len;

	if (!codeswap_len || !codeswap_data)
		return 0;

	seg_info = ath10k_swap_code_seg_alloc(ar, codeswap_len);
	if (!seg_info) {
		ath10k_err(ar, "failed to allocate fw code swap segment\n");
		return -ENOMEM;
	}

	ret = ath10k_swap_code_seg_fill(ar, seg_info,
					codeswap_data, codeswap_len);

	if (ret) {
		ath10k_warn(ar, "failed to initialize fw code swap segment: %d\n",
			    ret);
		ath10k_swap_code_seg_free(ar, seg_info);
		return ret;
	}

	fw_file->firmware_swap_code_seg_info = seg_info;

	return 0;
}
