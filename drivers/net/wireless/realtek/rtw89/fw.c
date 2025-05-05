// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include <linux/if_arp.h>
#include "cam.h"
#include "chan.h"
#include "coex.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "ps.h"
#include "reg.h"
#include "util.h"
#include "wow.h"

static bool rtw89_is_any_vif_connected_or_connecting(struct rtw89_dev *rtwdev);

struct rtw89_eapol_2_of_2 {
	u8 gtkbody[14];
	u8 key_des_ver;
	u8 rsvd[92];
} __packed;

struct rtw89_sa_query {
	u8 category;
	u8 action;
} __packed;

struct rtw89_arp_rsp {
	u8 llc_hdr[sizeof(rfc1042_header)];
	__be16 llc_type;
	struct arphdr arp_hdr;
	u8 sender_hw[ETH_ALEN];
	__be32 sender_ip;
	u8 target_hw[ETH_ALEN];
	__be32 target_ip;
} __packed;

static const u8 mss_signature[] = {0x4D, 0x53, 0x53, 0x4B, 0x50, 0x4F, 0x4F, 0x4C};

const struct rtw89_fw_blacklist rtw89_fw_blacklist_default = {
	.ver = 0x00,
	.list = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
		 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
		 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
		 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	},
};
EXPORT_SYMBOL(rtw89_fw_blacklist_default);

union rtw89_fw_element_arg {
	size_t offset;
	enum rtw89_rf_path rf_path;
	enum rtw89_fw_type fw_type;
};

struct rtw89_fw_element_handler {
	int (*fn)(struct rtw89_dev *rtwdev,
		  const struct rtw89_fw_element_hdr *elm,
		  const union rtw89_fw_element_arg arg);
	const union rtw89_fw_element_arg arg;
	const char *name;
};

static void rtw89_fw_c2h_cmd_handle(struct rtw89_dev *rtwdev,
				    struct sk_buff *skb);
static int rtw89_h2c_tx_and_wait(struct rtw89_dev *rtwdev, struct sk_buff *skb,
				 struct rtw89_wait_info *wait, unsigned int cond);
static int __parse_security_section(struct rtw89_dev *rtwdev,
				    struct rtw89_fw_bin_info *info,
				    struct rtw89_fw_hdr_section_info *section_info,
				    const void *content,
				    u32 *mssc_len);

static struct sk_buff *rtw89_fw_h2c_alloc_skb(struct rtw89_dev *rtwdev, u32 len,
					      bool header)
{
	struct sk_buff *skb;
	u32 header_len = 0;
	u32 h2c_desc_size = rtwdev->chip->h2c_desc_size;

	if (header)
		header_len = H2C_HEADER_LEN;

	skb = dev_alloc_skb(len + header_len + h2c_desc_size);
	if (!skb)
		return NULL;
	skb_reserve(skb, header_len + h2c_desc_size);
	memset(skb->data, 0, len);

	return skb;
}

struct sk_buff *rtw89_fw_h2c_alloc_skb_with_hdr(struct rtw89_dev *rtwdev, u32 len)
{
	return rtw89_fw_h2c_alloc_skb(rtwdev, len, true);
}

struct sk_buff *rtw89_fw_h2c_alloc_skb_no_hdr(struct rtw89_dev *rtwdev, u32 len)
{
	return rtw89_fw_h2c_alloc_skb(rtwdev, len, false);
}

int rtw89_fw_check_rdy(struct rtw89_dev *rtwdev, enum rtw89_fwdl_check_type type)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	u8 val;
	int ret;

	ret = read_poll_timeout_atomic(mac->fwdl_get_status, val,
				       val == RTW89_FWDL_WCPU_FW_INIT_RDY,
				       1, FWDL_WAIT_CNT, false, rtwdev, type);
	if (ret) {
		switch (val) {
		case RTW89_FWDL_CHECKSUM_FAIL:
			rtw89_err(rtwdev, "fw checksum fail\n");
			return -EINVAL;

		case RTW89_FWDL_SECURITY_FAIL:
			rtw89_err(rtwdev, "fw security fail\n");
			return -EINVAL;

		case RTW89_FWDL_CV_NOT_MATCH:
			rtw89_err(rtwdev, "fw cv not match\n");
			return -EINVAL;

		default:
			rtw89_err(rtwdev, "fw unexpected status %d\n", val);
			return -EBUSY;
		}
	}

	set_bit(RTW89_FLAG_FW_RDY, rtwdev->flags);

	return 0;
}

static int rtw89_fw_hdr_parser_v0(struct rtw89_dev *rtwdev, const u8 *fw, u32 len,
				  struct rtw89_fw_bin_info *info)
{
	const struct rtw89_fw_hdr *fw_hdr = (const struct rtw89_fw_hdr *)fw;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_fw_hdr_section_info *section_info;
	struct rtw89_fw_secure *sec = &rtwdev->fw.sec;
	const struct rtw89_fw_dynhdr_hdr *fwdynhdr;
	const struct rtw89_fw_hdr_section *section;
	const u8 *fw_end = fw + len;
	const u8 *bin;
	u32 base_hdr_len;
	u32 mssc_len;
	int ret;
	u32 i;

	if (!info)
		return -EINVAL;

	info->section_num = le32_get_bits(fw_hdr->w6, FW_HDR_W6_SEC_NUM);
	base_hdr_len = struct_size(fw_hdr, sections, info->section_num);
	info->dynamic_hdr_en = le32_get_bits(fw_hdr->w7, FW_HDR_W7_DYN_HDR);
	info->idmem_share_mode = le32_get_bits(fw_hdr->w7, FW_HDR_W7_IDMEM_SHARE_MODE);

	if (info->dynamic_hdr_en) {
		info->hdr_len = le32_get_bits(fw_hdr->w3, FW_HDR_W3_LEN);
		info->dynamic_hdr_len = info->hdr_len - base_hdr_len;
		fwdynhdr = (const struct rtw89_fw_dynhdr_hdr *)(fw + base_hdr_len);
		if (le32_to_cpu(fwdynhdr->hdr_len) != info->dynamic_hdr_len) {
			rtw89_err(rtwdev, "[ERR]invalid fw dynamic header len\n");
			return -EINVAL;
		}
	} else {
		info->hdr_len = base_hdr_len;
		info->dynamic_hdr_len = 0;
	}

	bin = fw + info->hdr_len;

	/* jump to section header */
	section_info = info->section_info;
	for (i = 0; i < info->section_num; i++) {
		section = &fw_hdr->sections[i];
		section_info->type =
			le32_get_bits(section->w1, FWSECTION_HDR_W1_SECTIONTYPE);
		section_info->len = le32_get_bits(section->w1, FWSECTION_HDR_W1_SEC_SIZE);

		if (le32_get_bits(section->w1, FWSECTION_HDR_W1_CHECKSUM))
			section_info->len += FWDL_SECTION_CHKSUM_LEN;
		section_info->redl = le32_get_bits(section->w1, FWSECTION_HDR_W1_REDL);
		section_info->dladdr =
			le32_get_bits(section->w0, FWSECTION_HDR_W0_DL_ADDR) & 0x1fffffff;
		section_info->addr = bin;

		if (section_info->type == FWDL_SECURITY_SECTION_TYPE) {
			section_info->mssc =
				le32_get_bits(section->w2, FWSECTION_HDR_W2_MSSC);

			ret = __parse_security_section(rtwdev, info, section_info,
						       bin, &mssc_len);
			if (ret)
				return ret;

			if (sec->secure_boot && chip->chip_id == RTL8852B)
				section_info->len_override = 960;
		} else {
			section_info->mssc = 0;
			mssc_len = 0;
		}

		rtw89_debug(rtwdev, RTW89_DBG_FW,
			    "section[%d] type=%d len=0x%-6x mssc=%d mssc_len=%d addr=%tx\n",
			    i, section_info->type, section_info->len,
			    section_info->mssc, mssc_len, bin - fw);
		rtw89_debug(rtwdev, RTW89_DBG_FW,
			    "           ignore=%d key_addr=%p (0x%tx) key_len=%d key_idx=%d\n",
			    section_info->ignore, section_info->key_addr,
			    section_info->key_addr ?
			    section_info->key_addr - section_info->addr : 0,
			    section_info->key_len, section_info->key_idx);

		bin += section_info->len + mssc_len;
		section_info++;
	}

	if (fw_end != bin) {
		rtw89_err(rtwdev, "[ERR]fw bin size\n");
		return -EINVAL;
	}

	return 0;
}

static int __get_mssc_key_idx(struct rtw89_dev *rtwdev,
			      const struct rtw89_fw_mss_pool_hdr *mss_hdr,
			      u32 rmp_tbl_size, u32 *key_idx)
{
	struct rtw89_fw_secure *sec = &rtwdev->fw.sec;
	u32 sel_byte_idx;
	u32 mss_sel_idx;
	u8 sel_bit_idx;
	int i;

	if (sec->mss_dev_type == RTW89_FW_MSS_DEV_TYPE_FWSEC_DEF) {
		if (!mss_hdr->defen)
			return -ENOENT;

		mss_sel_idx = sec->mss_cust_idx * le16_to_cpu(mss_hdr->msskey_num_max) +
			      sec->mss_key_num;
	} else {
		if (mss_hdr->defen)
			mss_sel_idx = FWDL_MSS_POOL_DEFKEYSETS_SIZE << 3;
		else
			mss_sel_idx = 0;
		mss_sel_idx += sec->mss_dev_type * le16_to_cpu(mss_hdr->msskey_num_max) *
						   le16_to_cpu(mss_hdr->msscust_max) +
			       sec->mss_cust_idx * le16_to_cpu(mss_hdr->msskey_num_max) +
			       sec->mss_key_num;
	}

	sel_byte_idx = mss_sel_idx >> 3;
	sel_bit_idx = mss_sel_idx & 0x7;

	if (sel_byte_idx >= rmp_tbl_size)
		return -EFAULT;

	if (!(mss_hdr->rmp_tbl[sel_byte_idx] & BIT(sel_bit_idx)))
		return -ENOENT;

	*key_idx = hweight8(mss_hdr->rmp_tbl[sel_byte_idx] & (BIT(sel_bit_idx) - 1));

	for (i = 0; i < sel_byte_idx; i++)
		*key_idx += hweight8(mss_hdr->rmp_tbl[i]);

	return 0;
}

static int __parse_formatted_mssc(struct rtw89_dev *rtwdev,
				  struct rtw89_fw_bin_info *info,
				  struct rtw89_fw_hdr_section_info *section_info,
				  const void *content,
				  u32 *mssc_len)
{
	const struct rtw89_fw_mss_pool_hdr *mss_hdr = content + section_info->len;
	const union rtw89_fw_section_mssc_content *section_content = content;
	struct rtw89_fw_secure *sec = &rtwdev->fw.sec;
	u32 rmp_tbl_size;
	u32 key_sign_len;
	u32 real_key_idx;
	u32 sb_sel_ver;
	int ret;

	if (memcmp(mss_signature, mss_hdr->signature, sizeof(mss_signature)) != 0) {
		rtw89_err(rtwdev, "[ERR] wrong MSS signature\n");
		return -ENOENT;
	}

	if (mss_hdr->rmpfmt == MSS_POOL_RMP_TBL_BITMASK) {
		rmp_tbl_size = (le16_to_cpu(mss_hdr->msskey_num_max) *
				le16_to_cpu(mss_hdr->msscust_max) *
				mss_hdr->mssdev_max) >> 3;
		if (mss_hdr->defen)
			rmp_tbl_size += FWDL_MSS_POOL_DEFKEYSETS_SIZE;
	} else {
		rtw89_err(rtwdev, "[ERR] MSS Key Pool Remap Table Format Unsupport:%X\n",
			  mss_hdr->rmpfmt);
		return -EINVAL;
	}

	if (rmp_tbl_size + sizeof(*mss_hdr) != le32_to_cpu(mss_hdr->key_raw_offset)) {
		rtw89_err(rtwdev, "[ERR] MSS Key Pool Format Error:0x%X + 0x%X != 0x%X\n",
			  rmp_tbl_size, (int)sizeof(*mss_hdr),
			  le32_to_cpu(mss_hdr->key_raw_offset));
		return -EINVAL;
	}

	key_sign_len = le16_to_cpu(section_content->key_sign_len.v) >> 2;
	if (!key_sign_len)
		key_sign_len = 512;

	if (info->dsp_checksum)
		key_sign_len += FWDL_SECURITY_CHKSUM_LEN;

	*mssc_len = sizeof(*mss_hdr) + rmp_tbl_size +
		    le16_to_cpu(mss_hdr->keypair_num) * key_sign_len;

	if (!sec->secure_boot)
		goto out;

	sb_sel_ver = get_unaligned_le32(&section_content->sb_sel_ver.v);
	if (sb_sel_ver && sb_sel_ver != sec->sb_sel_mgn)
		goto ignore;

	ret = __get_mssc_key_idx(rtwdev, mss_hdr, rmp_tbl_size, &real_key_idx);
	if (ret)
		goto ignore;

	section_info->key_addr = content + section_info->len +
				le32_to_cpu(mss_hdr->key_raw_offset) +
				key_sign_len * real_key_idx;
	section_info->key_len = key_sign_len;
	section_info->key_idx = real_key_idx;

out:
	if (info->secure_section_exist) {
		section_info->ignore = true;
		return 0;
	}

	info->secure_section_exist = true;

	return 0;

ignore:
	section_info->ignore = true;

	return 0;
}

static int __check_secure_blacklist(struct rtw89_dev *rtwdev,
				    struct rtw89_fw_bin_info *info,
				    struct rtw89_fw_hdr_section_info *section_info,
				    const void *content)
{
	const struct rtw89_fw_blacklist *chip_blacklist = rtwdev->chip->fw_blacklist;
	const union rtw89_fw_section_mssc_content *section_content = content;
	struct rtw89_fw_secure *sec = &rtwdev->fw.sec;
	u8 byte_idx;
	u8 bit_mask;

	if (!sec->secure_boot)
		return 0;

	if (!info->secure_section_exist || section_info->ignore)
		return 0;

	if (!chip_blacklist) {
		rtw89_warn(rtwdev, "chip no blacklist for secure firmware\n");
		return -ENOENT;
	}

	byte_idx = section_content->blacklist.bit_in_chip_list >> 3;
	bit_mask = BIT(section_content->blacklist.bit_in_chip_list & 0x7);

	if (section_content->blacklist.ver > chip_blacklist->ver) {
		rtw89_warn(rtwdev, "chip blacklist out of date (%u, %u)\n",
			   section_content->blacklist.ver, chip_blacklist->ver);
		return -EINVAL;
	}

	if (chip_blacklist->list[byte_idx] & bit_mask) {
		rtw89_warn(rtwdev, "firmware %u in chip blacklist\n",
			   section_content->blacklist.ver);
		return -EPERM;
	}

	return 0;
}

static int __parse_security_section(struct rtw89_dev *rtwdev,
				    struct rtw89_fw_bin_info *info,
				    struct rtw89_fw_hdr_section_info *section_info,
				    const void *content,
				    u32 *mssc_len)
{
	struct rtw89_fw_secure *sec = &rtwdev->fw.sec;
	int ret;

	if ((section_info->mssc & FORMATTED_MSSC_MASK) == FORMATTED_MSSC) {
		ret = __parse_formatted_mssc(rtwdev, info, section_info,
					     content, mssc_len);
		if (ret)
			return -EINVAL;
	} else {
		*mssc_len = section_info->mssc * FWDL_SECURITY_SIGLEN;
		if (info->dsp_checksum)
			*mssc_len += section_info->mssc * FWDL_SECURITY_CHKSUM_LEN;

		if (sec->secure_boot) {
			if (sec->mss_idx >= section_info->mssc) {
				rtw89_err(rtwdev, "unexpected MSS %d >= %d\n",
					  sec->mss_idx, section_info->mssc);
				return -EFAULT;
			}
			section_info->key_addr = content + section_info->len +
						 sec->mss_idx * FWDL_SECURITY_SIGLEN;
			section_info->key_len = FWDL_SECURITY_SIGLEN;
		}

		info->secure_section_exist = true;
	}

	ret = __check_secure_blacklist(rtwdev, info, section_info, content);
	WARN_ONCE(ret, "Current firmware in blacklist. Please update firmware.\n");

	return 0;
}

static int rtw89_fw_hdr_parser_v1(struct rtw89_dev *rtwdev, const u8 *fw, u32 len,
				  struct rtw89_fw_bin_info *info)
{
	const struct rtw89_fw_hdr_v1 *fw_hdr = (const struct rtw89_fw_hdr_v1 *)fw;
	struct rtw89_fw_hdr_section_info *section_info;
	const struct rtw89_fw_dynhdr_hdr *fwdynhdr;
	const struct rtw89_fw_hdr_section_v1 *section;
	const u8 *fw_end = fw + len;
	const u8 *bin;
	u32 base_hdr_len;
	u32 mssc_len;
	int ret;
	u32 i;

	info->section_num = le32_get_bits(fw_hdr->w6, FW_HDR_V1_W6_SEC_NUM);
	info->dsp_checksum = le32_get_bits(fw_hdr->w6, FW_HDR_V1_W6_DSP_CHKSUM);
	base_hdr_len = struct_size(fw_hdr, sections, info->section_num);
	info->dynamic_hdr_en = le32_get_bits(fw_hdr->w7, FW_HDR_V1_W7_DYN_HDR);
	info->idmem_share_mode = le32_get_bits(fw_hdr->w7, FW_HDR_V1_W7_IDMEM_SHARE_MODE);

	if (info->dynamic_hdr_en) {
		info->hdr_len = le32_get_bits(fw_hdr->w5, FW_HDR_V1_W5_HDR_SIZE);
		info->dynamic_hdr_len = info->hdr_len - base_hdr_len;
		fwdynhdr = (const struct rtw89_fw_dynhdr_hdr *)(fw + base_hdr_len);
		if (le32_to_cpu(fwdynhdr->hdr_len) != info->dynamic_hdr_len) {
			rtw89_err(rtwdev, "[ERR]invalid fw dynamic header len\n");
			return -EINVAL;
		}
	} else {
		info->hdr_len = base_hdr_len;
		info->dynamic_hdr_len = 0;
	}

	bin = fw + info->hdr_len;

	/* jump to section header */
	section_info = info->section_info;
	for (i = 0; i < info->section_num; i++) {
		section = &fw_hdr->sections[i];

		section_info->type =
			le32_get_bits(section->w1, FWSECTION_HDR_V1_W1_SECTIONTYPE);
		section_info->len =
			le32_get_bits(section->w1, FWSECTION_HDR_V1_W1_SEC_SIZE);
		if (le32_get_bits(section->w1, FWSECTION_HDR_V1_W1_CHECKSUM))
			section_info->len += FWDL_SECTION_CHKSUM_LEN;
		section_info->redl = le32_get_bits(section->w1, FWSECTION_HDR_V1_W1_REDL);
		section_info->dladdr =
			le32_get_bits(section->w0, FWSECTION_HDR_V1_W0_DL_ADDR);
		section_info->addr = bin;

		if (section_info->type == FWDL_SECURITY_SECTION_TYPE) {
			section_info->mssc =
				le32_get_bits(section->w2, FWSECTION_HDR_V1_W2_MSSC);

			ret = __parse_security_section(rtwdev, info, section_info,
						       bin, &mssc_len);
			if (ret)
				return ret;
		} else {
			section_info->mssc = 0;
			mssc_len = 0;
		}

		rtw89_debug(rtwdev, RTW89_DBG_FW,
			    "section[%d] type=%d len=0x%-6x mssc=%d mssc_len=%d addr=%tx\n",
			    i, section_info->type, section_info->len,
			    section_info->mssc, mssc_len, bin - fw);
		rtw89_debug(rtwdev, RTW89_DBG_FW,
			    "           ignore=%d key_addr=%p (0x%tx) key_len=%d key_idx=%d\n",
			    section_info->ignore, section_info->key_addr,
			    section_info->key_addr ?
			    section_info->key_addr - section_info->addr : 0,
			    section_info->key_len, section_info->key_idx);

		bin += section_info->len + mssc_len;
		section_info++;
	}

	if (fw_end != bin) {
		rtw89_err(rtwdev, "[ERR]fw bin size\n");
		return -EINVAL;
	}

	if (!info->secure_section_exist)
		rtw89_warn(rtwdev, "no firmware secure section\n");

	return 0;
}

static int rtw89_fw_hdr_parser(struct rtw89_dev *rtwdev,
			       const struct rtw89_fw_suit *fw_suit,
			       struct rtw89_fw_bin_info *info)
{
	const u8 *fw = fw_suit->data;
	u32 len = fw_suit->size;

	if (!fw || !len) {
		rtw89_err(rtwdev, "fw type %d isn't recognized\n", fw_suit->type);
		return -ENOENT;
	}

	switch (fw_suit->hdr_ver) {
	case 0:
		return rtw89_fw_hdr_parser_v0(rtwdev, fw, len, info);
	case 1:
		return rtw89_fw_hdr_parser_v1(rtwdev, fw, len, info);
	default:
		return -ENOENT;
	}
}

static
const struct rtw89_mfw_hdr *rtw89_mfw_get_hdr_ptr(struct rtw89_dev *rtwdev,
						  const struct firmware *firmware)
{
	const struct rtw89_mfw_hdr *mfw_hdr;

	if (sizeof(*mfw_hdr) > firmware->size)
		return NULL;

	mfw_hdr = (const struct rtw89_mfw_hdr *)&firmware->data[0];

	if (mfw_hdr->sig != RTW89_MFW_SIG)
		return NULL;

	return mfw_hdr;
}

static int rtw89_mfw_validate_hdr(struct rtw89_dev *rtwdev,
				  const struct firmware *firmware,
				  const struct rtw89_mfw_hdr *mfw_hdr)
{
	const void *mfw = firmware->data;
	u32 mfw_len = firmware->size;
	u8 fw_nr = mfw_hdr->fw_nr;
	const void *ptr;

	if (fw_nr == 0) {
		rtw89_err(rtwdev, "mfw header has no fw entry\n");
		return -ENOENT;
	}

	ptr = &mfw_hdr->info[fw_nr];

	if (ptr > mfw + mfw_len) {
		rtw89_err(rtwdev, "mfw header out of address\n");
		return -EFAULT;
	}

	return 0;
}

static
int rtw89_mfw_recognize(struct rtw89_dev *rtwdev, enum rtw89_fw_type type,
			struct rtw89_fw_suit *fw_suit, bool nowarn)
{
	struct rtw89_fw_info *fw_info = &rtwdev->fw;
	const struct firmware *firmware = fw_info->req.firmware;
	const struct rtw89_mfw_info *mfw_info = NULL, *tmp;
	const struct rtw89_mfw_hdr *mfw_hdr;
	const u8 *mfw = firmware->data;
	u32 mfw_len = firmware->size;
	int ret;
	int i;

	mfw_hdr = rtw89_mfw_get_hdr_ptr(rtwdev, firmware);
	if (!mfw_hdr) {
		rtw89_debug(rtwdev, RTW89_DBG_FW, "use legacy firmware\n");
		/* legacy firmware support normal type only */
		if (type != RTW89_FW_NORMAL)
			return -EINVAL;
		fw_suit->data = mfw;
		fw_suit->size = mfw_len;
		return 0;
	}

	ret = rtw89_mfw_validate_hdr(rtwdev, firmware, mfw_hdr);
	if (ret)
		return ret;

	for (i = 0; i < mfw_hdr->fw_nr; i++) {
		tmp = &mfw_hdr->info[i];
		if (tmp->type != type)
			continue;

		if (type == RTW89_FW_LOGFMT) {
			mfw_info = tmp;
			goto found;
		}

		/* Version order of WiFi firmware in firmware file are not in order,
		 * pass all firmware to find the equal or less but closest version.
		 */
		if (tmp->cv <= rtwdev->hal.cv && !tmp->mp) {
			if (!mfw_info || mfw_info->cv < tmp->cv)
				mfw_info = tmp;
		}
	}

	if (mfw_info)
		goto found;

	if (!nowarn)
		rtw89_err(rtwdev, "no suitable firmware found\n");
	return -ENOENT;

found:
	fw_suit->data = mfw + le32_to_cpu(mfw_info->shift);
	fw_suit->size = le32_to_cpu(mfw_info->size);

	if (fw_suit->data + fw_suit->size > mfw + mfw_len) {
		rtw89_err(rtwdev, "fw_suit %d out of address\n", type);
		return -EFAULT;
	}

	return 0;
}

static u32 rtw89_mfw_get_size(struct rtw89_dev *rtwdev)
{
	struct rtw89_fw_info *fw_info = &rtwdev->fw;
	const struct firmware *firmware = fw_info->req.firmware;
	const struct rtw89_mfw_info *mfw_info;
	const struct rtw89_mfw_hdr *mfw_hdr;
	u32 size;
	int ret;

	mfw_hdr = rtw89_mfw_get_hdr_ptr(rtwdev, firmware);
	if (!mfw_hdr) {
		rtw89_warn(rtwdev, "not mfw format\n");
		return 0;
	}

	ret = rtw89_mfw_validate_hdr(rtwdev, firmware, mfw_hdr);
	if (ret)
		return ret;

	mfw_info = &mfw_hdr->info[mfw_hdr->fw_nr - 1];
	size = le32_to_cpu(mfw_info->shift) + le32_to_cpu(mfw_info->size);

	return size;
}

static void rtw89_fw_update_ver_v0(struct rtw89_dev *rtwdev,
				   struct rtw89_fw_suit *fw_suit,
				   const struct rtw89_fw_hdr *hdr)
{
	fw_suit->major_ver = le32_get_bits(hdr->w1, FW_HDR_W1_MAJOR_VERSION);
	fw_suit->minor_ver = le32_get_bits(hdr->w1, FW_HDR_W1_MINOR_VERSION);
	fw_suit->sub_ver = le32_get_bits(hdr->w1, FW_HDR_W1_SUBVERSION);
	fw_suit->sub_idex = le32_get_bits(hdr->w1, FW_HDR_W1_SUBINDEX);
	fw_suit->commitid = le32_get_bits(hdr->w2, FW_HDR_W2_COMMITID);
	fw_suit->build_year = le32_get_bits(hdr->w5, FW_HDR_W5_YEAR);
	fw_suit->build_mon = le32_get_bits(hdr->w4, FW_HDR_W4_MONTH);
	fw_suit->build_date = le32_get_bits(hdr->w4, FW_HDR_W4_DATE);
	fw_suit->build_hour = le32_get_bits(hdr->w4, FW_HDR_W4_HOUR);
	fw_suit->build_min = le32_get_bits(hdr->w4, FW_HDR_W4_MIN);
	fw_suit->cmd_ver = le32_get_bits(hdr->w7, FW_HDR_W7_CMD_VERSERION);
}

static void rtw89_fw_update_ver_v1(struct rtw89_dev *rtwdev,
				   struct rtw89_fw_suit *fw_suit,
				   const struct rtw89_fw_hdr_v1 *hdr)
{
	fw_suit->major_ver = le32_get_bits(hdr->w1, FW_HDR_V1_W1_MAJOR_VERSION);
	fw_suit->minor_ver = le32_get_bits(hdr->w1, FW_HDR_V1_W1_MINOR_VERSION);
	fw_suit->sub_ver = le32_get_bits(hdr->w1, FW_HDR_V1_W1_SUBVERSION);
	fw_suit->sub_idex = le32_get_bits(hdr->w1, FW_HDR_V1_W1_SUBINDEX);
	fw_suit->commitid = le32_get_bits(hdr->w2, FW_HDR_V1_W2_COMMITID);
	fw_suit->build_year = le32_get_bits(hdr->w5, FW_HDR_V1_W5_YEAR);
	fw_suit->build_mon = le32_get_bits(hdr->w4, FW_HDR_V1_W4_MONTH);
	fw_suit->build_date = le32_get_bits(hdr->w4, FW_HDR_V1_W4_DATE);
	fw_suit->build_hour = le32_get_bits(hdr->w4, FW_HDR_V1_W4_HOUR);
	fw_suit->build_min = le32_get_bits(hdr->w4, FW_HDR_V1_W4_MIN);
	fw_suit->cmd_ver = le32_get_bits(hdr->w7, FW_HDR_V1_W3_CMD_VERSERION);
}

static int rtw89_fw_update_ver(struct rtw89_dev *rtwdev,
			       enum rtw89_fw_type type,
			       struct rtw89_fw_suit *fw_suit)
{
	const struct rtw89_fw_hdr *v0 = (const struct rtw89_fw_hdr *)fw_suit->data;
	const struct rtw89_fw_hdr_v1 *v1 = (const struct rtw89_fw_hdr_v1 *)fw_suit->data;

	if (type == RTW89_FW_LOGFMT)
		return 0;

	fw_suit->type = type;
	fw_suit->hdr_ver = le32_get_bits(v0->w3, FW_HDR_W3_HDR_VER);

	switch (fw_suit->hdr_ver) {
	case 0:
		rtw89_fw_update_ver_v0(rtwdev, fw_suit, v0);
		break;
	case 1:
		rtw89_fw_update_ver_v1(rtwdev, fw_suit, v1);
		break;
	default:
		rtw89_err(rtwdev, "Unknown firmware header version %u\n",
			  fw_suit->hdr_ver);
		return -ENOENT;
	}

	rtw89_info(rtwdev,
		   "Firmware version %u.%u.%u.%u (%08x), cmd version %u, type %u\n",
		   fw_suit->major_ver, fw_suit->minor_ver, fw_suit->sub_ver,
		   fw_suit->sub_idex, fw_suit->commitid, fw_suit->cmd_ver, type);

	return 0;
}

static
int __rtw89_fw_recognize(struct rtw89_dev *rtwdev, enum rtw89_fw_type type,
			 bool nowarn)
{
	struct rtw89_fw_suit *fw_suit = rtw89_fw_suit_get(rtwdev, type);
	int ret;

	ret = rtw89_mfw_recognize(rtwdev, type, fw_suit, nowarn);
	if (ret)
		return ret;

	return rtw89_fw_update_ver(rtwdev, type, fw_suit);
}

static
int __rtw89_fw_recognize_from_elm(struct rtw89_dev *rtwdev,
				  const struct rtw89_fw_element_hdr *elm,
				  const union rtw89_fw_element_arg arg)
{
	enum rtw89_fw_type type = arg.fw_type;
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_fw_suit *fw_suit;

	/* Version of BB MCU is in decreasing order in firmware file, so take
	 * first equal or less version, which is equal or less but closest version.
	 */
	if (hal->cv < elm->u.bbmcu.cv)
		return 1; /* ignore this element */

	fw_suit = rtw89_fw_suit_get(rtwdev, type);
	if (fw_suit->data)
		return 1; /* ignore this element (a firmware is taken already) */

	fw_suit->data = elm->u.bbmcu.contents;
	fw_suit->size = le32_to_cpu(elm->size);

	return rtw89_fw_update_ver(rtwdev, type, fw_suit);
}

#define __DEF_FW_FEAT_COND(__cond, __op) \
static bool __fw_feat_cond_ ## __cond(u32 suit_ver_code, u32 comp_ver_code) \
{ \
	return suit_ver_code __op comp_ver_code; \
}

__DEF_FW_FEAT_COND(ge, >=); /* greater or equal */
__DEF_FW_FEAT_COND(le, <=); /* less or equal */
__DEF_FW_FEAT_COND(lt, <); /* less than */

struct __fw_feat_cfg {
	enum rtw89_core_chip_id chip_id;
	enum rtw89_fw_feature feature;
	u32 ver_code;
	bool (*cond)(u32 suit_ver_code, u32 comp_ver_code);
};

#define __CFG_FW_FEAT(_chip, _cond, _maj, _min, _sub, _idx, _feat) \
	{ \
		.chip_id = _chip, \
		.feature = RTW89_FW_FEATURE_ ## _feat, \
		.ver_code = RTW89_FW_VER_CODE(_maj, _min, _sub, _idx), \
		.cond = __fw_feat_cond_ ## _cond, \
	}

static const struct __fw_feat_cfg fw_feat_tbl[] = {
	__CFG_FW_FEAT(RTL8851B, ge, 0, 29, 37, 1, TX_WAKE),
	__CFG_FW_FEAT(RTL8851B, ge, 0, 29, 37, 1, SCAN_OFFLOAD),
	__CFG_FW_FEAT(RTL8851B, ge, 0, 29, 41, 0, CRASH_TRIGGER),
	__CFG_FW_FEAT(RTL8852A, le, 0, 13, 29, 0, OLD_HT_RA_FORMAT),
	__CFG_FW_FEAT(RTL8852A, ge, 0, 13, 35, 0, SCAN_OFFLOAD),
	__CFG_FW_FEAT(RTL8852A, ge, 0, 13, 35, 0, TX_WAKE),
	__CFG_FW_FEAT(RTL8852A, ge, 0, 13, 36, 0, CRASH_TRIGGER),
	__CFG_FW_FEAT(RTL8852A, lt, 0, 13, 37, 0, NO_WOW_CPU_IO_RX),
	__CFG_FW_FEAT(RTL8852A, lt, 0, 13, 38, 0, NO_PACKET_DROP),
	__CFG_FW_FEAT(RTL8852B, ge, 0, 29, 26, 0, NO_LPS_PG),
	__CFG_FW_FEAT(RTL8852B, ge, 0, 29, 26, 0, TX_WAKE),
	__CFG_FW_FEAT(RTL8852B, ge, 0, 29, 29, 0, CRASH_TRIGGER),
	__CFG_FW_FEAT(RTL8852B, ge, 0, 29, 29, 0, SCAN_OFFLOAD),
	__CFG_FW_FEAT(RTL8852B, ge, 0, 29, 29, 7, BEACON_FILTER),
	__CFG_FW_FEAT(RTL8852B, lt, 0, 29, 30, 0, NO_WOW_CPU_IO_RX),
	__CFG_FW_FEAT(RTL8852BT, ge, 0, 29, 74, 0, NO_LPS_PG),
	__CFG_FW_FEAT(RTL8852BT, ge, 0, 29, 74, 0, TX_WAKE),
	__CFG_FW_FEAT(RTL8852BT, ge, 0, 29, 90, 0, CRASH_TRIGGER),
	__CFG_FW_FEAT(RTL8852BT, ge, 0, 29, 91, 0, SCAN_OFFLOAD),
	__CFG_FW_FEAT(RTL8852BT, ge, 0, 29, 110, 0, BEACON_FILTER),
	__CFG_FW_FEAT(RTL8852C, le, 0, 27, 33, 0, NO_DEEP_PS),
	__CFG_FW_FEAT(RTL8852C, ge, 0, 27, 34, 0, TX_WAKE),
	__CFG_FW_FEAT(RTL8852C, ge, 0, 27, 36, 0, SCAN_OFFLOAD),
	__CFG_FW_FEAT(RTL8852C, ge, 0, 27, 40, 0, CRASH_TRIGGER),
	__CFG_FW_FEAT(RTL8852C, ge, 0, 27, 56, 10, BEACON_FILTER),
	__CFG_FW_FEAT(RTL8852C, ge, 0, 27, 80, 0, WOW_REASON_V1),
	__CFG_FW_FEAT(RTL8922A, ge, 0, 34, 30, 0, CRASH_TRIGGER),
	__CFG_FW_FEAT(RTL8922A, ge, 0, 34, 11, 0, MACID_PAUSE_SLEEP),
	__CFG_FW_FEAT(RTL8922A, ge, 0, 34, 35, 0, SCAN_OFFLOAD),
	__CFG_FW_FEAT(RTL8922A, lt, 0, 35, 21, 0, SCAN_OFFLOAD_BE_V0),
	__CFG_FW_FEAT(RTL8922A, ge, 0, 35, 12, 0, BEACON_FILTER),
	__CFG_FW_FEAT(RTL8922A, ge, 0, 35, 22, 0, WOW_REASON_V1),
	__CFG_FW_FEAT(RTL8922A, lt, 0, 35, 31, 0, RFK_PRE_NOTIFY_V0),
	__CFG_FW_FEAT(RTL8922A, lt, 0, 35, 31, 0, LPS_CH_INFO),
	__CFG_FW_FEAT(RTL8922A, lt, 0, 35, 42, 0, RFK_RXDCK_V0),
	__CFG_FW_FEAT(RTL8922A, ge, 0, 35, 46, 0, NOTIFY_AP_INFO),
	__CFG_FW_FEAT(RTL8922A, lt, 0, 35, 47, 0, CH_INFO_BE_V0),
	__CFG_FW_FEAT(RTL8922A, lt, 0, 35, 49, 0, RFK_PRE_NOTIFY_V1),
	__CFG_FW_FEAT(RTL8922A, lt, 0, 35, 51, 0, NO_PHYCAP_P1),
	__CFG_FW_FEAT(RTL8922A, lt, 0, 35, 64, 0, NO_POWER_DIFFERENCE),
	__CFG_FW_FEAT(RTL8922A, ge, 0, 35, 71, 0, BEACON_LOSS_COUNT_V1),
};

static void rtw89_fw_iterate_feature_cfg(struct rtw89_fw_info *fw,
					 const struct rtw89_chip_info *chip,
					 u32 ver_code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fw_feat_tbl); i++) {
		const struct __fw_feat_cfg *ent = &fw_feat_tbl[i];

		if (chip->chip_id != ent->chip_id)
			continue;

		if (ent->cond(ver_code, ent->ver_code))
			RTW89_SET_FW_FEATURE(ent->feature, fw);
	}
}

static void rtw89_fw_recognize_features(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_fw_suit *fw_suit;
	u32 suit_ver_code;

	fw_suit = rtw89_fw_suit_get(rtwdev, RTW89_FW_NORMAL);
	suit_ver_code = RTW89_FW_SUIT_VER_CODE(fw_suit);

	rtw89_fw_iterate_feature_cfg(&rtwdev->fw, chip, suit_ver_code);
}

const struct firmware *
rtw89_early_fw_feature_recognize(struct device *device,
				 const struct rtw89_chip_info *chip,
				 struct rtw89_fw_info *early_fw,
				 int *used_fw_format)
{
	const struct firmware *firmware;
	char fw_name[64];
	int fw_format;
	u32 ver_code;
	int ret;

	for (fw_format = chip->fw_format_max; fw_format >= 0; fw_format--) {
		rtw89_fw_get_filename(fw_name, sizeof(fw_name),
				      chip->fw_basename, fw_format);

		ret = request_firmware(&firmware, fw_name, device);
		if (!ret) {
			dev_info(device, "loaded firmware %s\n", fw_name);
			*used_fw_format = fw_format;
			break;
		}
	}

	if (ret) {
		dev_err(device, "failed to early request firmware: %d\n", ret);
		return NULL;
	}

	ver_code = rtw89_compat_fw_hdr_ver_code(firmware->data);

	if (!ver_code)
		goto out;

	rtw89_fw_iterate_feature_cfg(early_fw, chip, ver_code);

out:
	return firmware;
}

static int rtw89_fw_validate_ver_required(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_variant *variant = rtwdev->variant;
	const struct rtw89_fw_suit *fw_suit;
	u32 suit_ver_code;

	if (!variant)
		return 0;

	fw_suit = rtw89_fw_suit_get(rtwdev, RTW89_FW_NORMAL);
	suit_ver_code = RTW89_FW_SUIT_VER_CODE(fw_suit);

	if (variant->fw_min_ver_code > suit_ver_code) {
		rtw89_err(rtwdev, "minimum required firmware version is 0x%x\n",
			  variant->fw_min_ver_code);
		return -ENOENT;
	}

	return 0;
}

int rtw89_fw_recognize(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	int ret;

	if (chip->try_ce_fw) {
		ret = __rtw89_fw_recognize(rtwdev, RTW89_FW_NORMAL_CE, true);
		if (!ret)
			goto normal_done;
	}

	ret = __rtw89_fw_recognize(rtwdev, RTW89_FW_NORMAL, false);
	if (ret)
		return ret;

normal_done:
	ret = rtw89_fw_validate_ver_required(rtwdev);
	if (ret)
		return ret;

	/* It still works if wowlan firmware isn't existing. */
	__rtw89_fw_recognize(rtwdev, RTW89_FW_WOWLAN, false);

	/* It still works if log format file isn't existing. */
	__rtw89_fw_recognize(rtwdev, RTW89_FW_LOGFMT, true);

	rtw89_fw_recognize_features(rtwdev);

	rtw89_coex_recognize_ver(rtwdev);

	return 0;
}

static
int rtw89_build_phy_tbl_from_elm(struct rtw89_dev *rtwdev,
				 const struct rtw89_fw_element_hdr *elm,
				 const union rtw89_fw_element_arg arg)
{
	struct rtw89_fw_elm_info *elm_info = &rtwdev->fw.elm_info;
	struct rtw89_phy_table *tbl;
	struct rtw89_reg2_def *regs;
	enum rtw89_rf_path rf_path;
	u32 n_regs, i;
	u8 idx;

	tbl = kzalloc(sizeof(*tbl), GFP_KERNEL);
	if (!tbl)
		return -ENOMEM;

	switch (le32_to_cpu(elm->id)) {
	case RTW89_FW_ELEMENT_ID_BB_REG:
		elm_info->bb_tbl = tbl;
		break;
	case RTW89_FW_ELEMENT_ID_BB_GAIN:
		elm_info->bb_gain = tbl;
		break;
	case RTW89_FW_ELEMENT_ID_RADIO_A:
	case RTW89_FW_ELEMENT_ID_RADIO_B:
	case RTW89_FW_ELEMENT_ID_RADIO_C:
	case RTW89_FW_ELEMENT_ID_RADIO_D:
		rf_path = arg.rf_path;
		idx = elm->u.reg2.idx;

		elm_info->rf_radio[idx] = tbl;
		tbl->rf_path = rf_path;
		tbl->config = rtw89_phy_config_rf_reg_v1;
		break;
	case RTW89_FW_ELEMENT_ID_RF_NCTL:
		elm_info->rf_nctl = tbl;
		break;
	default:
		kfree(tbl);
		return -ENOENT;
	}

	n_regs = le32_to_cpu(elm->size) / sizeof(tbl->regs[0]);
	regs = kcalloc(n_regs, sizeof(*regs), GFP_KERNEL);
	if (!regs)
		goto out;

	for (i = 0; i < n_regs; i++) {
		regs[i].addr = le32_to_cpu(elm->u.reg2.regs[i].addr);
		regs[i].data = le32_to_cpu(elm->u.reg2.regs[i].data);
	}

	tbl->n_regs = n_regs;
	tbl->regs = regs;

	return 0;

out:
	kfree(tbl);
	return -ENOMEM;
}

static
int rtw89_fw_recognize_txpwr_from_elm(struct rtw89_dev *rtwdev,
				      const struct rtw89_fw_element_hdr *elm,
				      const union rtw89_fw_element_arg arg)
{
	const struct __rtw89_fw_txpwr_element *txpwr_elm = &elm->u.txpwr;
	const unsigned long offset = arg.offset;
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	struct rtw89_txpwr_conf *conf;

	if (!rtwdev->rfe_data) {
		rtwdev->rfe_data = kzalloc(sizeof(*rtwdev->rfe_data), GFP_KERNEL);
		if (!rtwdev->rfe_data)
			return -ENOMEM;
	}

	conf = (void *)rtwdev->rfe_data + offset;

	/* if multiple matched, take the last eventually */
	if (txpwr_elm->rfe_type == efuse->rfe_type)
		goto setup;

	/* without one is matched, accept default */
	if (txpwr_elm->rfe_type == RTW89_TXPWR_CONF_DFLT_RFE_TYPE &&
	    (!rtw89_txpwr_conf_valid(conf) ||
	     conf->rfe_type == RTW89_TXPWR_CONF_DFLT_RFE_TYPE))
		goto setup;

	rtw89_debug(rtwdev, RTW89_DBG_FW, "skip txpwr element ID %u RFE %u\n",
		    elm->id, txpwr_elm->rfe_type);
	return 0;

setup:
	rtw89_debug(rtwdev, RTW89_DBG_FW, "take txpwr element ID %u RFE %u\n",
		    elm->id, txpwr_elm->rfe_type);

	conf->rfe_type = txpwr_elm->rfe_type;
	conf->ent_sz = txpwr_elm->ent_sz;
	conf->num_ents = le32_to_cpu(txpwr_elm->num_ents);
	conf->data = txpwr_elm->content;
	return 0;
}

static
int rtw89_build_txpwr_trk_tbl_from_elm(struct rtw89_dev *rtwdev,
				       const struct rtw89_fw_element_hdr *elm,
				       const union rtw89_fw_element_arg arg)
{
	struct rtw89_fw_elm_info *elm_info = &rtwdev->fw.elm_info;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u32 needed_bitmap = 0;
	u32 offset = 0;
	int subband;
	u32 bitmap;
	int type;

	if (chip->support_bands & BIT(NL80211_BAND_6GHZ))
		needed_bitmap |= RTW89_DEFAULT_NEEDED_FW_TXPWR_TRK_6GHZ;
	if (chip->support_bands & BIT(NL80211_BAND_5GHZ))
		needed_bitmap |= RTW89_DEFAULT_NEEDED_FW_TXPWR_TRK_5GHZ;
	if (chip->support_bands & BIT(NL80211_BAND_2GHZ))
		needed_bitmap |= RTW89_DEFAULT_NEEDED_FW_TXPWR_TRK_2GHZ;

	bitmap = le32_to_cpu(elm->u.txpwr_trk.bitmap);

	if ((bitmap & needed_bitmap) != needed_bitmap) {
		rtw89_warn(rtwdev, "needed txpwr trk bitmap %08x but %08x\n",
			   needed_bitmap, bitmap);
		return -ENOENT;
	}

	elm_info->txpwr_trk = kzalloc(sizeof(*elm_info->txpwr_trk), GFP_KERNEL);
	if (!elm_info->txpwr_trk)
		return -ENOMEM;

	for (type = 0; bitmap; type++, bitmap >>= 1) {
		if (!(bitmap & BIT(0)))
			continue;

		if (type >= __RTW89_FW_TXPWR_TRK_TYPE_6GHZ_START &&
		    type <= __RTW89_FW_TXPWR_TRK_TYPE_6GHZ_MAX)
			subband = 4;
		else if (type >= __RTW89_FW_TXPWR_TRK_TYPE_5GHZ_START &&
			 type <= __RTW89_FW_TXPWR_TRK_TYPE_5GHZ_MAX)
			subband = 3;
		else if (type >= __RTW89_FW_TXPWR_TRK_TYPE_2GHZ_START &&
			 type <= __RTW89_FW_TXPWR_TRK_TYPE_2GHZ_MAX)
			subband = 1;
		else
			break;

		elm_info->txpwr_trk->delta[type] = &elm->u.txpwr_trk.contents[offset];

		offset += subband;
		if (offset * DELTA_SWINGIDX_SIZE > le32_to_cpu(elm->size))
			goto err;
	}

	return 0;

err:
	rtw89_warn(rtwdev, "unexpected txpwr trk offset %d over size %d\n",
		   offset, le32_to_cpu(elm->size));
	kfree(elm_info->txpwr_trk);
	elm_info->txpwr_trk = NULL;

	return -EFAULT;
}

static
int rtw89_build_rfk_log_fmt_from_elm(struct rtw89_dev *rtwdev,
				     const struct rtw89_fw_element_hdr *elm,
				     const union rtw89_fw_element_arg arg)
{
	struct rtw89_fw_elm_info *elm_info = &rtwdev->fw.elm_info;
	u8 rfk_id;

	if (elm_info->rfk_log_fmt)
		goto allocated;

	elm_info->rfk_log_fmt = kzalloc(sizeof(*elm_info->rfk_log_fmt), GFP_KERNEL);
	if (!elm_info->rfk_log_fmt)
		return 1; /* this is an optional element, so just ignore this */

allocated:
	rfk_id = elm->u.rfk_log_fmt.rfk_id;
	if (rfk_id >= RTW89_PHY_C2H_RFK_LOG_FUNC_NUM)
		return 1;

	elm_info->rfk_log_fmt->elm[rfk_id] = elm;

	return 0;
}

static bool rtw89_regd_entcpy(struct rtw89_regd *regd, const void *cursor,
			      u8 cursor_size)
{
	/* fill default values if needed for backward compatibility */
	struct rtw89_fw_regd_entry entry = {
		.rule_2ghz = RTW89_NA,
		.rule_5ghz = RTW89_NA,
		.rule_6ghz = RTW89_NA,
		.fmap = cpu_to_le32(0x0),
	};
	u8 valid_size = min_t(u8, sizeof(entry), cursor_size);
	unsigned int i;
	u32 fmap;

	memcpy(&entry, cursor, valid_size);
	memset(regd, 0, sizeof(*regd));

	regd->alpha2[0] = entry.alpha2_0;
	regd->alpha2[1] = entry.alpha2_1;
	regd->alpha2[2] = '\0';

	/* also need to consider forward compatibility */
	regd->txpwr_regd[RTW89_BAND_2G] = entry.rule_2ghz < RTW89_REGD_NUM ?
					  entry.rule_2ghz : RTW89_NA;
	regd->txpwr_regd[RTW89_BAND_5G] = entry.rule_5ghz < RTW89_REGD_NUM ?
					  entry.rule_5ghz : RTW89_NA;
	regd->txpwr_regd[RTW89_BAND_6G] = entry.rule_6ghz < RTW89_REGD_NUM ?
					  entry.rule_6ghz : RTW89_NA;

	BUILD_BUG_ON(sizeof(fmap) != sizeof(entry.fmap));
	BUILD_BUG_ON(sizeof(fmap) * 8 < NUM_OF_RTW89_REGD_FUNC);

	fmap = le32_to_cpu(entry.fmap);
	for (i = 0; i < NUM_OF_RTW89_REGD_FUNC; i++) {
		if (fmap & BIT(i))
			set_bit(i, regd->func_bitmap);
	}

	return true;
}

#define rtw89_for_each_in_regd_element(regd, element) \
	for (const void *cursor = (element)->content, \
	     *end = (element)->content + \
		    le32_to_cpu((element)->num_ents) * (element)->ent_sz; \
	     cursor < end; cursor += (element)->ent_sz) \
		if (rtw89_regd_entcpy(regd, cursor, (element)->ent_sz))

static
int rtw89_recognize_regd_from_elm(struct rtw89_dev *rtwdev,
				  const struct rtw89_fw_element_hdr *elm,
				  const union rtw89_fw_element_arg arg)
{
	const struct __rtw89_fw_regd_element *regd_elm = &elm->u.regd;
	struct rtw89_fw_elm_info *elm_info = &rtwdev->fw.elm_info;
	u32 num_ents = le32_to_cpu(regd_elm->num_ents);
	struct rtw89_regd_data *p;
	struct rtw89_regd regd;
	u32 i = 0;

	if (num_ents > RTW89_REGD_MAX_COUNTRY_NUM) {
		rtw89_warn(rtwdev,
			   "regd element ents (%d) are over max num (%d)\n",
			   num_ents, RTW89_REGD_MAX_COUNTRY_NUM);
		rtw89_warn(rtwdev,
			   "regd element ignore and take another/common\n");
		return 1;
	}

	if (elm_info->regd) {
		rtw89_debug(rtwdev, RTW89_DBG_REGD,
			    "regd element take the latter\n");
		devm_kfree(rtwdev->dev, elm_info->regd);
		elm_info->regd = NULL;
	}

	p = devm_kzalloc(rtwdev->dev, struct_size(p, map, num_ents), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->nr = num_ents;
	rtw89_for_each_in_regd_element(&regd, regd_elm)
		p->map[i++] = regd;

	if (i != num_ents) {
		rtw89_err(rtwdev, "regd element has %d invalid ents\n",
			  num_ents - i);
		devm_kfree(rtwdev->dev, p);
		return -EINVAL;
	}

	elm_info->regd = p;
	return 0;
}

static const struct rtw89_fw_element_handler __fw_element_handlers[] = {
	[RTW89_FW_ELEMENT_ID_BBMCU0] = {__rtw89_fw_recognize_from_elm,
					{ .fw_type = RTW89_FW_BBMCU0 }, NULL},
	[RTW89_FW_ELEMENT_ID_BBMCU1] = {__rtw89_fw_recognize_from_elm,
					{ .fw_type = RTW89_FW_BBMCU1 }, NULL},
	[RTW89_FW_ELEMENT_ID_BB_REG] = {rtw89_build_phy_tbl_from_elm, {}, "BB"},
	[RTW89_FW_ELEMENT_ID_BB_GAIN] = {rtw89_build_phy_tbl_from_elm, {}, NULL},
	[RTW89_FW_ELEMENT_ID_RADIO_A] = {rtw89_build_phy_tbl_from_elm,
					 { .rf_path =  RF_PATH_A }, "radio A"},
	[RTW89_FW_ELEMENT_ID_RADIO_B] = {rtw89_build_phy_tbl_from_elm,
					 { .rf_path =  RF_PATH_B }, NULL},
	[RTW89_FW_ELEMENT_ID_RADIO_C] = {rtw89_build_phy_tbl_from_elm,
					 { .rf_path =  RF_PATH_C }, NULL},
	[RTW89_FW_ELEMENT_ID_RADIO_D] = {rtw89_build_phy_tbl_from_elm,
					 { .rf_path =  RF_PATH_D }, NULL},
	[RTW89_FW_ELEMENT_ID_RF_NCTL] = {rtw89_build_phy_tbl_from_elm, {}, "NCTL"},
	[RTW89_FW_ELEMENT_ID_TXPWR_BYRATE] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, byrate.conf) }, "TXPWR",
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_LMT_2GHZ] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, lmt_2ghz.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_LMT_5GHZ] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, lmt_5ghz.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_LMT_6GHZ] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, lmt_6ghz.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_DA_LMT_2GHZ] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, da_lmt_2ghz.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_DA_LMT_5GHZ] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, da_lmt_5ghz.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_DA_LMT_6GHZ] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, da_lmt_6ghz.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_LMT_RU_2GHZ] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, lmt_ru_2ghz.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_LMT_RU_5GHZ] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, lmt_ru_5ghz.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_LMT_RU_6GHZ] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, lmt_ru_6ghz.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_DA_LMT_RU_2GHZ] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, da_lmt_ru_2ghz.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_DA_LMT_RU_5GHZ] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, da_lmt_ru_5ghz.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_DA_LMT_RU_6GHZ] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, da_lmt_ru_6ghz.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TX_SHAPE_LMT] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, tx_shape_lmt.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TX_SHAPE_LMT_RU] = {
		rtw89_fw_recognize_txpwr_from_elm,
		{ .offset = offsetof(struct rtw89_rfe_data, tx_shape_lmt_ru.conf) }, NULL,
	},
	[RTW89_FW_ELEMENT_ID_TXPWR_TRK] = {
		rtw89_build_txpwr_trk_tbl_from_elm, {}, "PWR_TRK",
	},
	[RTW89_FW_ELEMENT_ID_RFKLOG_FMT] = {
		rtw89_build_rfk_log_fmt_from_elm, {}, NULL,
	},
	[RTW89_FW_ELEMENT_ID_REGD] = {
		rtw89_recognize_regd_from_elm, {}, "REGD",
	},
};

int rtw89_fw_recognize_elements(struct rtw89_dev *rtwdev)
{
	struct rtw89_fw_info *fw_info = &rtwdev->fw;
	const struct firmware *firmware = fw_info->req.firmware;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u32 unrecognized_elements = chip->needed_fw_elms;
	const struct rtw89_fw_element_handler *handler;
	const struct rtw89_fw_element_hdr *hdr;
	u32 elm_size;
	u32 elem_id;
	u32 offset;
	int ret;

	BUILD_BUG_ON(sizeof(chip->needed_fw_elms) * 8 < RTW89_FW_ELEMENT_ID_NUM);

	offset = rtw89_mfw_get_size(rtwdev);
	offset = ALIGN(offset, RTW89_FW_ELEMENT_ALIGN);
	if (offset == 0)
		return -EINVAL;

	while (offset + sizeof(*hdr) < firmware->size) {
		hdr = (const struct rtw89_fw_element_hdr *)(firmware->data + offset);

		elm_size = le32_to_cpu(hdr->size);
		if (offset + elm_size >= firmware->size) {
			rtw89_warn(rtwdev, "firmware element size exceeds\n");
			break;
		}

		elem_id = le32_to_cpu(hdr->id);
		if (elem_id >= ARRAY_SIZE(__fw_element_handlers))
			goto next;

		handler = &__fw_element_handlers[elem_id];
		if (!handler->fn)
			goto next;

		ret = handler->fn(rtwdev, hdr, handler->arg);
		if (ret == 1) /* ignore this element */
			goto next;
		if (ret)
			return ret;

		if (handler->name)
			rtw89_info(rtwdev, "Firmware element %s version: %4ph\n",
				   handler->name, hdr->ver);

		unrecognized_elements &= ~BIT(elem_id);
next:
		offset += sizeof(*hdr) + elm_size;
		offset = ALIGN(offset, RTW89_FW_ELEMENT_ALIGN);
	}

	if (unrecognized_elements) {
		rtw89_err(rtwdev, "Firmware elements 0x%08x are unrecognized\n",
			  unrecognized_elements);
		return -ENOENT;
	}

	return 0;
}

void rtw89_h2c_pkt_set_hdr(struct rtw89_dev *rtwdev, struct sk_buff *skb,
			   u8 type, u8 cat, u8 class, u8 func,
			   bool rack, bool dack, u32 len)
{
	struct fwcmd_hdr *hdr;

	hdr = (struct fwcmd_hdr *)skb_push(skb, 8);

	if (!(rtwdev->fw.h2c_seq % 4))
		rack = true;
	hdr->hdr0 = cpu_to_le32(FIELD_PREP(H2C_HDR_DEL_TYPE, type) |
				FIELD_PREP(H2C_HDR_CAT, cat) |
				FIELD_PREP(H2C_HDR_CLASS, class) |
				FIELD_PREP(H2C_HDR_FUNC, func) |
				FIELD_PREP(H2C_HDR_H2C_SEQ, rtwdev->fw.h2c_seq));

	hdr->hdr1 = cpu_to_le32(FIELD_PREP(H2C_HDR_TOTAL_LEN,
					   len + H2C_HEADER_LEN) |
				(rack ? H2C_HDR_REC_ACK : 0) |
				(dack ? H2C_HDR_DONE_ACK : 0));

	rtwdev->fw.h2c_seq++;
}

static void rtw89_h2c_pkt_set_hdr_fwdl(struct rtw89_dev *rtwdev,
				       struct sk_buff *skb,
				       u8 type, u8 cat, u8 class, u8 func,
				       u32 len)
{
	struct fwcmd_hdr *hdr;

	hdr = (struct fwcmd_hdr *)skb_push(skb, 8);

	hdr->hdr0 = cpu_to_le32(FIELD_PREP(H2C_HDR_DEL_TYPE, type) |
				FIELD_PREP(H2C_HDR_CAT, cat) |
				FIELD_PREP(H2C_HDR_CLASS, class) |
				FIELD_PREP(H2C_HDR_FUNC, func) |
				FIELD_PREP(H2C_HDR_H2C_SEQ, rtwdev->fw.h2c_seq));

	hdr->hdr1 = cpu_to_le32(FIELD_PREP(H2C_HDR_TOTAL_LEN,
					   len + H2C_HEADER_LEN));
}

static u32 __rtw89_fw_download_tweak_hdr_v0(struct rtw89_dev *rtwdev,
					    struct rtw89_fw_bin_info *info,
					    struct rtw89_fw_hdr *fw_hdr)
{
	struct rtw89_fw_hdr_section_info *section_info;
	struct rtw89_fw_hdr_section *section;
	int i;

	le32p_replace_bits(&fw_hdr->w7, FWDL_SECTION_PER_PKT_LEN,
			   FW_HDR_W7_PART_SIZE);

	for (i = 0; i < info->section_num; i++) {
		section_info = &info->section_info[i];

		if (!section_info->len_override)
			continue;

		section = &fw_hdr->sections[i];
		le32p_replace_bits(&section->w1, section_info->len_override,
				   FWSECTION_HDR_W1_SEC_SIZE);
	}

	return 0;
}

static u32 __rtw89_fw_download_tweak_hdr_v1(struct rtw89_dev *rtwdev,
					    struct rtw89_fw_bin_info *info,
					    struct rtw89_fw_hdr_v1 *fw_hdr)
{
	struct rtw89_fw_hdr_section_info *section_info;
	struct rtw89_fw_hdr_section_v1 *section;
	u8 dst_sec_idx = 0;
	u8 sec_idx;

	le32p_replace_bits(&fw_hdr->w7, FWDL_SECTION_PER_PKT_LEN,
			   FW_HDR_V1_W7_PART_SIZE);

	for (sec_idx = 0; sec_idx < info->section_num; sec_idx++) {
		section_info = &info->section_info[sec_idx];
		section = &fw_hdr->sections[sec_idx];

		if (section_info->ignore)
			continue;

		if (dst_sec_idx != sec_idx)
			fw_hdr->sections[dst_sec_idx] = *section;

		dst_sec_idx++;
	}

	le32p_replace_bits(&fw_hdr->w6, dst_sec_idx, FW_HDR_V1_W6_SEC_NUM);

	return (info->section_num - dst_sec_idx) * sizeof(*section);
}

static int __rtw89_fw_download_hdr(struct rtw89_dev *rtwdev,
				   const struct rtw89_fw_suit *fw_suit,
				   struct rtw89_fw_bin_info *info)
{
	u32 len = info->hdr_len - info->dynamic_hdr_len;
	struct rtw89_fw_hdr_v1 *fw_hdr_v1;
	const u8 *fw = fw_suit->data;
	struct rtw89_fw_hdr *fw_hdr;
	struct sk_buff *skb;
	u32 truncated;
	u32 ret = 0;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw hdr dl\n");
		return -ENOMEM;
	}

	skb_put_data(skb, fw, len);

	switch (fw_suit->hdr_ver) {
	case 0:
		fw_hdr = (struct rtw89_fw_hdr *)skb->data;
		truncated = __rtw89_fw_download_tweak_hdr_v0(rtwdev, info, fw_hdr);
		break;
	case 1:
		fw_hdr_v1 = (struct rtw89_fw_hdr_v1 *)skb->data;
		truncated = __rtw89_fw_download_tweak_hdr_v1(rtwdev, info, fw_hdr_v1);
		break;
	default:
		ret = -EOPNOTSUPP;
		goto fail;
	}

	if (truncated) {
		len -= truncated;
		skb_trim(skb, len);
	}

	rtw89_h2c_pkt_set_hdr_fwdl(rtwdev, skb, FWCMD_TYPE_H2C,
				   H2C_CAT_MAC, H2C_CL_MAC_FWDL,
				   H2C_FUNC_MAC_FWHDR_DL, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

static int rtw89_fw_download_hdr(struct rtw89_dev *rtwdev,
				 const struct rtw89_fw_suit *fw_suit,
				 struct rtw89_fw_bin_info *info)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	int ret;

	ret = __rtw89_fw_download_hdr(rtwdev, fw_suit, info);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]FW header download\n");
		return ret;
	}

	ret = mac->fwdl_check_path_ready(rtwdev, false);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]FWDL path ready\n");
		return ret;
	}

	rtw89_write32(rtwdev, R_AX_HALT_H2C_CTRL, 0);
	rtw89_write32(rtwdev, R_AX_HALT_C2H_CTRL, 0);

	return 0;
}

static int __rtw89_fw_download_main(struct rtw89_dev *rtwdev,
				    struct rtw89_fw_hdr_section_info *info)
{
	struct sk_buff *skb;
	const u8 *section = info->addr;
	u32 residue_len = info->len;
	bool copy_key = false;
	u32 pkt_len;
	int ret;

	if (info->ignore)
		return 0;

	if (info->len_override) {
		if (info->len_override > info->len)
			rtw89_warn(rtwdev, "override length %u larger than original %u\n",
				   info->len_override, info->len);
		else
			residue_len = info->len_override;
	}

	if (info->key_addr && info->key_len) {
		if (residue_len > FWDL_SECTION_PER_PKT_LEN || info->len < info->key_len)
			rtw89_warn(rtwdev,
				   "ignore to copy key data because of len %d, %d, %d, %d\n",
				   info->len, FWDL_SECTION_PER_PKT_LEN,
				   info->key_len, residue_len);
		else
			copy_key = true;
	}

	while (residue_len) {
		if (residue_len >= FWDL_SECTION_PER_PKT_LEN)
			pkt_len = FWDL_SECTION_PER_PKT_LEN;
		else
			pkt_len = residue_len;

		skb = rtw89_fw_h2c_alloc_skb_no_hdr(rtwdev, pkt_len);
		if (!skb) {
			rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
			return -ENOMEM;
		}
		skb_put_data(skb, section, pkt_len);

		if (copy_key)
			memcpy(skb->data + pkt_len - info->key_len,
			       info->key_addr, info->key_len);

		ret = rtw89_h2c_tx(rtwdev, skb, true);
		if (ret) {
			rtw89_err(rtwdev, "failed to send h2c\n");
			goto fail;
		}

		section += pkt_len;
		residue_len -= pkt_len;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

static enum rtw89_fwdl_check_type
rtw89_fw_get_fwdl_chk_type_from_suit(struct rtw89_dev *rtwdev,
				     const struct rtw89_fw_suit *fw_suit)
{
	switch (fw_suit->type) {
	case RTW89_FW_BBMCU0:
		return RTW89_FWDL_CHECK_BB0_FWDL_DONE;
	case RTW89_FW_BBMCU1:
		return RTW89_FWDL_CHECK_BB1_FWDL_DONE;
	default:
		return RTW89_FWDL_CHECK_WCPU_FWDL_DONE;
	}
}

static int rtw89_fw_download_main(struct rtw89_dev *rtwdev,
				  const struct rtw89_fw_suit *fw_suit,
				  struct rtw89_fw_bin_info *info)
{
	struct rtw89_fw_hdr_section_info *section_info = info->section_info;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	enum rtw89_fwdl_check_type chk_type;
	u8 section_num = info->section_num;
	int ret;

	while (section_num--) {
		ret = __rtw89_fw_download_main(rtwdev, section_info);
		if (ret)
			return ret;
		section_info++;
	}

	if (chip->chip_gen == RTW89_CHIP_AX)
		return 0;

	chk_type = rtw89_fw_get_fwdl_chk_type_from_suit(rtwdev, fw_suit);
	ret = rtw89_fw_check_rdy(rtwdev, chk_type);
	if (ret) {
		rtw89_warn(rtwdev, "failed to download firmware type %u\n",
			   fw_suit->type);
		return ret;
	}

	return 0;
}

static void rtw89_fw_prog_cnt_dump(struct rtw89_dev *rtwdev)
{
	enum rtw89_chip_gen chip_gen = rtwdev->chip->chip_gen;
	u32 addr = R_AX_DBG_PORT_SEL;
	u32 val32;
	u16 index;

	if (chip_gen == RTW89_CHIP_BE) {
		addr = R_BE_WLCPU_PORT_PC;
		goto dump;
	}

	rtw89_write32(rtwdev, R_AX_DBG_CTRL,
		      FIELD_PREP(B_AX_DBG_SEL0, FW_PROG_CNTR_DBG_SEL) |
		      FIELD_PREP(B_AX_DBG_SEL1, FW_PROG_CNTR_DBG_SEL));
	rtw89_write32_mask(rtwdev, R_AX_SYS_STATUS1, B_AX_SEL_0XC0_MASK, MAC_DBG_SEL);

dump:
	for (index = 0; index < 15; index++) {
		val32 = rtw89_read32(rtwdev, addr);
		rtw89_err(rtwdev, "[ERR]fw PC = 0x%x\n", val32);
		fsleep(10);
	}
}

static void rtw89_fw_dl_fail_dump(struct rtw89_dev *rtwdev)
{
	u32 val32;

	val32 = rtw89_read32(rtwdev, R_AX_WCPU_FW_CTRL);
	rtw89_err(rtwdev, "[ERR]fwdl 0x1E0 = 0x%x\n", val32);

	val32 = rtw89_read32(rtwdev, R_AX_BOOT_DBG);
	rtw89_err(rtwdev, "[ERR]fwdl 0x83F0 = 0x%x\n", val32);

	rtw89_fw_prog_cnt_dump(rtwdev);
}

static int rtw89_fw_download_suit(struct rtw89_dev *rtwdev,
				  struct rtw89_fw_suit *fw_suit)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct rtw89_fw_bin_info info = {};
	int ret;

	ret = rtw89_fw_hdr_parser(rtwdev, fw_suit, &info);
	if (ret) {
		rtw89_err(rtwdev, "parse fw header fail\n");
		return ret;
	}

	rtw89_fwdl_secure_idmem_share_mode(rtwdev, info.idmem_share_mode);

	if (rtwdev->chip->chip_id == RTL8922A &&
	    (fw_suit->type == RTW89_FW_NORMAL || fw_suit->type == RTW89_FW_WOWLAN))
		rtw89_write32(rtwdev, R_BE_SECURE_BOOT_MALLOC_INFO, 0x20248000);

	ret = mac->fwdl_check_path_ready(rtwdev, true);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]H2C path ready\n");
		return ret;
	}

	ret = rtw89_fw_download_hdr(rtwdev, fw_suit, &info);
	if (ret)
		return ret;

	ret = rtw89_fw_download_main(rtwdev, fw_suit, &info);
	if (ret)
		return ret;

	return 0;
}

static
int __rtw89_fw_download(struct rtw89_dev *rtwdev, enum rtw89_fw_type type,
			bool include_bb)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct rtw89_fw_info *fw_info = &rtwdev->fw;
	struct rtw89_fw_suit *fw_suit = rtw89_fw_suit_get(rtwdev, type);
	u8 bbmcu_nr = rtwdev->chip->bbmcu_nr;
	int ret;
	int i;

	mac->disable_cpu(rtwdev);
	ret = mac->fwdl_enable_wcpu(rtwdev, 0, true, include_bb);
	if (ret)
		return ret;

	ret = rtw89_fw_download_suit(rtwdev, fw_suit);
	if (ret)
		goto fwdl_err;

	for (i = 0; i < bbmcu_nr && include_bb; i++) {
		fw_suit = rtw89_fw_suit_get(rtwdev, RTW89_FW_BBMCU0 + i);

		ret = rtw89_fw_download_suit(rtwdev, fw_suit);
		if (ret)
			goto fwdl_err;
	}

	fw_info->h2c_seq = 0;
	fw_info->rec_seq = 0;
	fw_info->h2c_counter = 0;
	fw_info->c2h_counter = 0;
	rtwdev->mac.rpwm_seq_num = RPWM_SEQ_NUM_MAX;
	rtwdev->mac.cpwm_seq_num = CPWM_SEQ_NUM_MAX;

	mdelay(5);

	ret = rtw89_fw_check_rdy(rtwdev, RTW89_FWDL_CHECK_FREERTOS_DONE);
	if (ret) {
		rtw89_warn(rtwdev, "download firmware fail\n");
		goto fwdl_err;
	}

	return ret;

fwdl_err:
	rtw89_fw_dl_fail_dump(rtwdev);
	return ret;
}

int rtw89_fw_download(struct rtw89_dev *rtwdev, enum rtw89_fw_type type,
		      bool include_bb)
{
	int retry;
	int ret;

	for (retry = 0; retry < 5; retry++) {
		ret = __rtw89_fw_download(rtwdev, type, include_bb);
		if (!ret)
			return 0;
	}

	return ret;
}

int rtw89_wait_firmware_completion(struct rtw89_dev *rtwdev)
{
	struct rtw89_fw_info *fw = &rtwdev->fw;

	wait_for_completion(&fw->req.completion);
	if (!fw->req.firmware)
		return -EINVAL;

	return 0;
}

static int rtw89_load_firmware_req(struct rtw89_dev *rtwdev,
				   struct rtw89_fw_req_info *req,
				   const char *fw_name, bool nowarn)
{
	int ret;

	if (req->firmware) {
		rtw89_debug(rtwdev, RTW89_DBG_FW,
			    "full firmware has been early requested\n");
		complete_all(&req->completion);
		return 0;
	}

	if (nowarn)
		ret = firmware_request_nowarn(&req->firmware, fw_name, rtwdev->dev);
	else
		ret = request_firmware(&req->firmware, fw_name, rtwdev->dev);

	complete_all(&req->completion);

	return ret;
}

void rtw89_load_firmware_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev =
		container_of(work, struct rtw89_dev, load_firmware_work);
	const struct rtw89_chip_info *chip = rtwdev->chip;
	char fw_name[64];

	rtw89_fw_get_filename(fw_name, sizeof(fw_name),
			      chip->fw_basename, rtwdev->fw.fw_format);

	rtw89_load_firmware_req(rtwdev, &rtwdev->fw.req, fw_name, false);
}

static void rtw89_free_phy_tbl_from_elm(struct rtw89_phy_table *tbl)
{
	if (!tbl)
		return;

	kfree(tbl->regs);
	kfree(tbl);
}

static void rtw89_unload_firmware_elements(struct rtw89_dev *rtwdev)
{
	struct rtw89_fw_elm_info *elm_info = &rtwdev->fw.elm_info;
	int i;

	rtw89_free_phy_tbl_from_elm(elm_info->bb_tbl);
	rtw89_free_phy_tbl_from_elm(elm_info->bb_gain);
	for (i = 0; i < ARRAY_SIZE(elm_info->rf_radio); i++)
		rtw89_free_phy_tbl_from_elm(elm_info->rf_radio[i]);
	rtw89_free_phy_tbl_from_elm(elm_info->rf_nctl);

	kfree(elm_info->txpwr_trk);
	kfree(elm_info->rfk_log_fmt);
}

void rtw89_unload_firmware(struct rtw89_dev *rtwdev)
{
	struct rtw89_fw_info *fw = &rtwdev->fw;

	cancel_work_sync(&rtwdev->load_firmware_work);

	if (fw->req.firmware) {
		release_firmware(fw->req.firmware);

		/* assign NULL back in case rtw89_free_ieee80211_hw()
		 * try to release the same one again.
		 */
		fw->req.firmware = NULL;
	}

	kfree(fw->log.fmts);
	rtw89_unload_firmware_elements(rtwdev);
}

static u32 rtw89_fw_log_get_fmt_idx(struct rtw89_dev *rtwdev, u32 fmt_id)
{
	struct rtw89_fw_log *fw_log = &rtwdev->fw.log;
	u32 i;

	if (fmt_id > fw_log->last_fmt_id)
		return 0;

	for (i = 0; i < fw_log->fmt_count; i++) {
		if (le32_to_cpu(fw_log->fmt_ids[i]) == fmt_id)
			return i;
	}
	return 0;
}

static int rtw89_fw_log_create_fmts_dict(struct rtw89_dev *rtwdev)
{
	struct rtw89_fw_log *log = &rtwdev->fw.log;
	const struct rtw89_fw_logsuit_hdr *suit_hdr;
	struct rtw89_fw_suit *suit = &log->suit;
	const void *fmts_ptr, *fmts_end_ptr;
	u32 fmt_count;
	int i;

	suit_hdr = (const struct rtw89_fw_logsuit_hdr *)suit->data;
	fmt_count = le32_to_cpu(suit_hdr->count);
	log->fmt_ids = suit_hdr->ids;
	fmts_ptr = &suit_hdr->ids[fmt_count];
	fmts_end_ptr = suit->data + suit->size;
	log->fmts = kcalloc(fmt_count, sizeof(char *), GFP_KERNEL);
	if (!log->fmts)
		return -ENOMEM;

	for (i = 0; i < fmt_count; i++) {
		fmts_ptr = memchr_inv(fmts_ptr, 0, fmts_end_ptr - fmts_ptr);
		if (!fmts_ptr)
			break;

		(*log->fmts)[i] = fmts_ptr;
		log->last_fmt_id = le32_to_cpu(log->fmt_ids[i]);
		log->fmt_count++;
		fmts_ptr += strlen(fmts_ptr);
	}

	return 0;
}

int rtw89_fw_log_prepare(struct rtw89_dev *rtwdev)
{
	struct rtw89_fw_log *log = &rtwdev->fw.log;
	struct rtw89_fw_suit *suit = &log->suit;

	if (!suit || !suit->data) {
		rtw89_debug(rtwdev, RTW89_DBG_FW, "no log format file\n");
		return -EINVAL;
	}
	if (log->fmts)
		return 0;

	return rtw89_fw_log_create_fmts_dict(rtwdev);
}

static void rtw89_fw_log_dump_data(struct rtw89_dev *rtwdev,
				   const struct rtw89_fw_c2h_log_fmt *log_fmt,
				   u32 fmt_idx, u8 para_int, bool raw_data)
{
	const char *(*fmts)[] = rtwdev->fw.log.fmts;
	char str_buf[RTW89_C2H_FW_LOG_STR_BUF_SIZE];
	u32 args[RTW89_C2H_FW_LOG_MAX_PARA_NUM] = {0};
	int i;

	if (log_fmt->argc > RTW89_C2H_FW_LOG_MAX_PARA_NUM) {
		rtw89_warn(rtwdev, "C2H log: Arg count is unexpected %d\n",
			   log_fmt->argc);
		return;
	}

	if (para_int)
		for (i = 0 ; i < log_fmt->argc; i++)
			args[i] = le32_to_cpu(log_fmt->u.argv[i]);

	if (raw_data) {
		if (para_int)
			snprintf(str_buf, RTW89_C2H_FW_LOG_STR_BUF_SIZE,
				 "fw_enc(%d, %d, %d) %*ph", le32_to_cpu(log_fmt->fmt_id),
				 para_int, log_fmt->argc, (int)sizeof(args), args);
		else
			snprintf(str_buf, RTW89_C2H_FW_LOG_STR_BUF_SIZE,
				 "fw_enc(%d, %d, %d, %s)", le32_to_cpu(log_fmt->fmt_id),
				 para_int, log_fmt->argc, log_fmt->u.raw);
	} else {
		snprintf(str_buf, RTW89_C2H_FW_LOG_STR_BUF_SIZE, (*fmts)[fmt_idx],
			 args[0x0], args[0x1], args[0x2], args[0x3], args[0x4],
			 args[0x5], args[0x6], args[0x7], args[0x8], args[0x9],
			 args[0xa], args[0xb], args[0xc], args[0xd], args[0xe],
			 args[0xf]);
	}

	rtw89_info(rtwdev, "C2H log: %s", str_buf);
}

void rtw89_fw_log_dump(struct rtw89_dev *rtwdev, u8 *buf, u32 len)
{
	const struct rtw89_fw_c2h_log_fmt *log_fmt;
	u8 para_int;
	u32 fmt_idx;

	if (len < RTW89_C2H_HEADER_LEN) {
		rtw89_err(rtwdev, "c2h log length is wrong!\n");
		return;
	}

	buf += RTW89_C2H_HEADER_LEN;
	len -= RTW89_C2H_HEADER_LEN;
	log_fmt = (const struct rtw89_fw_c2h_log_fmt *)buf;

	if (len < RTW89_C2H_FW_FORMATTED_LOG_MIN_LEN)
		goto plain_log;

	if (log_fmt->signature != cpu_to_le16(RTW89_C2H_FW_LOG_SIGNATURE))
		goto plain_log;

	if (!rtwdev->fw.log.fmts)
		return;

	para_int = u8_get_bits(log_fmt->feature, RTW89_C2H_FW_LOG_FEATURE_PARA_INT);
	fmt_idx = rtw89_fw_log_get_fmt_idx(rtwdev, le32_to_cpu(log_fmt->fmt_id));

	if (!para_int && log_fmt->argc != 0 && fmt_idx != 0)
		rtw89_info(rtwdev, "C2H log: %s%s",
			   (*rtwdev->fw.log.fmts)[fmt_idx], log_fmt->u.raw);
	else if (fmt_idx != 0 && para_int)
		rtw89_fw_log_dump_data(rtwdev, log_fmt, fmt_idx, para_int, false);
	else
		rtw89_fw_log_dump_data(rtwdev, log_fmt, fmt_idx, para_int, true);
	return;

plain_log:
	rtw89_info(rtwdev, "C2H log: %.*s", len, buf);

}

#define H2C_CAM_LEN 60
int rtw89_fw_h2c_cam(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
		     struct rtw89_sta_link *rtwsta_link, const u8 *scan_mac_addr)
{
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_CAM_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_CAM_LEN);
	rtw89_cam_fill_addr_cam_info(rtwdev, rtwvif_link, rtwsta_link, scan_mac_addr,
				     skb->data);
	rtw89_cam_fill_bssid_cam_info(rtwdev, rtwvif_link, rtwsta_link, skb->data);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_ADDR_CAM_UPDATE,
			      H2C_FUNC_MAC_ADDR_CAM_UPD, 0, 1,
			      H2C_CAM_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_dctl_sec_cam_v1(struct rtw89_dev *rtwdev,
				 struct rtw89_vif_link *rtwvif_link,
				 struct rtw89_sta_link *rtwsta_link)
{
	struct rtw89_h2c_dctlinfo_ud_v1 *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for dctl sec cam\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_dctlinfo_ud_v1 *)skb->data;

	rtw89_cam_fill_dctl_sec_cam_info_v1(rtwdev, rtwvif_link, rtwsta_link, h2c);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_DCTLINFO_UD_V1, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_dctl_sec_cam_v1);

int rtw89_fw_h2c_dctl_sec_cam_v2(struct rtw89_dev *rtwdev,
				 struct rtw89_vif_link *rtwvif_link,
				 struct rtw89_sta_link *rtwsta_link)
{
	struct rtw89_h2c_dctlinfo_ud_v2 *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for dctl sec cam\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_dctlinfo_ud_v2 *)skb->data;

	rtw89_cam_fill_dctl_sec_cam_info_v2(rtwdev, rtwvif_link, rtwsta_link, h2c);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_DCTLINFO_UD_V2, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_dctl_sec_cam_v2);

int rtw89_fw_h2c_default_dmac_tbl_v2(struct rtw89_dev *rtwdev,
				     struct rtw89_vif_link *rtwvif_link,
				     struct rtw89_sta_link *rtwsta_link)
{
	u8 mac_id = rtwsta_link ? rtwsta_link->mac_id : rtwvif_link->mac_id;
	struct rtw89_h2c_dctlinfo_ud_v2 *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for dctl v2\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_dctlinfo_ud_v2 *)skb->data;

	h2c->c0 = le32_encode_bits(mac_id, DCTLINFO_V2_C0_MACID) |
		  le32_encode_bits(1, DCTLINFO_V2_C0_OP);

	h2c->m0 = cpu_to_le32(DCTLINFO_V2_W0_ALL);
	h2c->m1 = cpu_to_le32(DCTLINFO_V2_W1_ALL);
	h2c->m2 = cpu_to_le32(DCTLINFO_V2_W2_ALL);
	h2c->m3 = cpu_to_le32(DCTLINFO_V2_W3_ALL);
	h2c->m4 = cpu_to_le32(DCTLINFO_V2_W4_ALL);
	h2c->m5 = cpu_to_le32(DCTLINFO_V2_W5_ALL);
	h2c->m6 = cpu_to_le32(DCTLINFO_V2_W6_ALL);
	h2c->m7 = cpu_to_le32(DCTLINFO_V2_W7_ALL);
	h2c->m8 = cpu_to_le32(DCTLINFO_V2_W8_ALL);
	h2c->m9 = cpu_to_le32(DCTLINFO_V2_W9_ALL);
	h2c->m10 = cpu_to_le32(DCTLINFO_V2_W10_ALL);
	h2c->m11 = cpu_to_le32(DCTLINFO_V2_W11_ALL);
	h2c->m12 = cpu_to_le32(DCTLINFO_V2_W12_ALL);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_DCTLINFO_UD_V2, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_default_dmac_tbl_v2);

int rtw89_fw_h2c_ba_cam(struct rtw89_dev *rtwdev,
			struct rtw89_vif_link *rtwvif_link,
			struct rtw89_sta_link *rtwsta_link,
			bool valid, struct ieee80211_ampdu_params *params)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_h2c_ba_cam *h2c;
	u8 macid = rtwsta_link->mac_id;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	u8 entry_idx;
	int ret;

	ret = valid ?
	      rtw89_core_acquire_sta_ba_entry(rtwdev, rtwsta_link, params->tid,
					      &entry_idx) :
	      rtw89_core_release_sta_ba_entry(rtwdev, rtwsta_link, params->tid,
					      &entry_idx);
	if (ret) {
		/* it still works even if we don't have static BA CAM, because
		 * hardware can create dynamic BA CAM automatically.
		 */
		rtw89_debug(rtwdev, RTW89_DBG_TXRX,
			    "failed to %s entry tid=%d for h2c ba cam\n",
			    valid ? "alloc" : "free", params->tid);
		return 0;
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c ba cam\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_ba_cam *)skb->data;

	h2c->w0 = le32_encode_bits(macid, RTW89_H2C_BA_CAM_W0_MACID);
	if (chip->bacam_ver == RTW89_BACAM_V0_EXT)
		h2c->w1 |= le32_encode_bits(entry_idx, RTW89_H2C_BA_CAM_W1_ENTRY_IDX_V1);
	else
		h2c->w0 |= le32_encode_bits(entry_idx, RTW89_H2C_BA_CAM_W0_ENTRY_IDX);
	if (!valid)
		goto end;
	h2c->w0 |= le32_encode_bits(valid, RTW89_H2C_BA_CAM_W0_VALID) |
		   le32_encode_bits(params->tid, RTW89_H2C_BA_CAM_W0_TID);
	if (params->buf_size > 64)
		h2c->w0 |= le32_encode_bits(4, RTW89_H2C_BA_CAM_W0_BMAP_SIZE);
	else
		h2c->w0 |= le32_encode_bits(0, RTW89_H2C_BA_CAM_W0_BMAP_SIZE);
	/* If init req is set, hw will set the ssn */
	h2c->w0 |= le32_encode_bits(1, RTW89_H2C_BA_CAM_W0_INIT_REQ) |
		   le32_encode_bits(params->ssn, RTW89_H2C_BA_CAM_W0_SSN);

	if (chip->bacam_ver == RTW89_BACAM_V0_EXT) {
		h2c->w1 |= le32_encode_bits(1, RTW89_H2C_BA_CAM_W1_STD_EN) |
			   le32_encode_bits(rtwvif_link->mac_idx,
					    RTW89_H2C_BA_CAM_W1_BAND);
	}

end:
	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_BA_CAM,
			      H2C_FUNC_MAC_BA_CAM, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_ba_cam);

static int rtw89_fw_h2c_init_ba_cam_v0_ext(struct rtw89_dev *rtwdev,
					   u8 entry_idx, u8 uid)
{
	struct rtw89_h2c_ba_cam *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for dynamic h2c ba cam\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_ba_cam *)skb->data;

	h2c->w0 = le32_encode_bits(1, RTW89_H2C_BA_CAM_W0_VALID);
	h2c->w1 = le32_encode_bits(entry_idx, RTW89_H2C_BA_CAM_W1_ENTRY_IDX_V1) |
		  le32_encode_bits(uid, RTW89_H2C_BA_CAM_W1_UID) |
		  le32_encode_bits(0, RTW89_H2C_BA_CAM_W1_BAND) |
		  le32_encode_bits(0, RTW89_H2C_BA_CAM_W1_STD_EN);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_BA_CAM,
			      H2C_FUNC_MAC_BA_CAM, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

void rtw89_fw_h2c_init_dynamic_ba_cam_v0_ext(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 entry_idx = chip->bacam_num;
	u8 uid = 0;
	int i;

	for (i = 0; i < chip->bacam_dynamic_num; i++) {
		rtw89_fw_h2c_init_ba_cam_v0_ext(rtwdev, entry_idx, uid);
		entry_idx++;
		uid++;
	}
}

int rtw89_fw_h2c_ba_cam_v1(struct rtw89_dev *rtwdev,
			   struct rtw89_vif_link *rtwvif_link,
			   struct rtw89_sta_link *rtwsta_link,
			   bool valid, struct ieee80211_ampdu_params *params)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_h2c_ba_cam_v1 *h2c;
	u8 macid = rtwsta_link->mac_id;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	u8 entry_idx;
	u8 bmap_size;
	int ret;

	ret = valid ?
	      rtw89_core_acquire_sta_ba_entry(rtwdev, rtwsta_link, params->tid,
					      &entry_idx) :
	      rtw89_core_release_sta_ba_entry(rtwdev, rtwsta_link, params->tid,
					      &entry_idx);
	if (ret) {
		/* it still works even if we don't have static BA CAM, because
		 * hardware can create dynamic BA CAM automatically.
		 */
		rtw89_debug(rtwdev, RTW89_DBG_TXRX,
			    "failed to %s entry tid=%d for h2c ba cam\n",
			    valid ? "alloc" : "free", params->tid);
		return 0;
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c ba cam\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_ba_cam_v1 *)skb->data;

	if (params->buf_size > 512)
		bmap_size = 10;
	else if (params->buf_size > 256)
		bmap_size = 8;
	else if (params->buf_size > 64)
		bmap_size = 4;
	else
		bmap_size = 0;

	h2c->w0 = le32_encode_bits(valid, RTW89_H2C_BA_CAM_V1_W0_VALID) |
		  le32_encode_bits(1, RTW89_H2C_BA_CAM_V1_W0_INIT_REQ) |
		  le32_encode_bits(macid, RTW89_H2C_BA_CAM_V1_W0_MACID_MASK) |
		  le32_encode_bits(params->tid, RTW89_H2C_BA_CAM_V1_W0_TID_MASK) |
		  le32_encode_bits(bmap_size, RTW89_H2C_BA_CAM_V1_W0_BMAP_SIZE_MASK) |
		  le32_encode_bits(params->ssn, RTW89_H2C_BA_CAM_V1_W0_SSN_MASK);

	entry_idx += chip->bacam_dynamic_num; /* std entry right after dynamic ones */
	h2c->w1 = le32_encode_bits(entry_idx, RTW89_H2C_BA_CAM_V1_W1_ENTRY_IDX_MASK) |
		  le32_encode_bits(1, RTW89_H2C_BA_CAM_V1_W1_STD_ENTRY_EN) |
		  le32_encode_bits(!!rtwvif_link->mac_idx,
				   RTW89_H2C_BA_CAM_V1_W1_BAND_SEL);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_BA_CAM,
			      H2C_FUNC_MAC_BA_CAM_V1, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_ba_cam_v1);

int rtw89_fw_h2c_init_ba_cam_users(struct rtw89_dev *rtwdev, u8 users,
				   u8 offset, u8 mac_idx)
{
	struct rtw89_h2c_ba_cam_init *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c ba cam init\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_ba_cam_init *)skb->data;

	h2c->w0 = le32_encode_bits(users, RTW89_H2C_BA_CAM_INIT_USERS_MASK) |
		  le32_encode_bits(offset, RTW89_H2C_BA_CAM_INIT_OFFSET_MASK) |
		  le32_encode_bits(mac_idx, RTW89_H2C_BA_CAM_INIT_BAND_SEL);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_BA_CAM,
			      H2C_FUNC_MAC_BA_CAM_INIT, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_LOG_CFG_LEN 12
int rtw89_fw_h2c_fw_log(struct rtw89_dev *rtwdev, bool enable)
{
	struct sk_buff *skb;
	u32 comp = 0;
	int ret;

	if (enable)
		comp = BIT(RTW89_FW_LOG_COMP_INIT) | BIT(RTW89_FW_LOG_COMP_TASK) |
		       BIT(RTW89_FW_LOG_COMP_PS) | BIT(RTW89_FW_LOG_COMP_ERROR) |
		       BIT(RTW89_FW_LOG_COMP_SCAN);

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_LOG_CFG_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw log cfg\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_LOG_CFG_LEN);
	SET_LOG_CFG_LEVEL(skb->data, RTW89_FW_LOG_LEVEL_LOUD);
	SET_LOG_CFG_PATH(skb->data, BIT(RTW89_FW_LOG_LEVEL_C2H));
	SET_LOG_CFG_COMP(skb->data, comp);
	SET_LOG_CFG_COMP_EXT(skb->data, 0);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_FW_INFO,
			      H2C_FUNC_LOG_CFG, 0, 0,
			      H2C_LOG_CFG_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

static struct sk_buff *rtw89_eapol_get(struct rtw89_dev *rtwdev,
				       struct rtw89_vif_link *rtwvif_link)
{
	static const u8 gtkbody[] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88,
				     0x8E, 0x01, 0x03, 0x00, 0x5F, 0x02, 0x03};
	u8 sec_hdr_len = rtw89_wow_get_sec_hdr_len(rtwdev);
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_eapol_2_of_2 *eapol_pkt;
	struct ieee80211_bss_conf *bss_conf;
	struct ieee80211_hdr_3addr *hdr;
	struct sk_buff *skb;
	u8 key_des_ver;

	if (rtw_wow->ptk_alg == 3)
		key_des_ver = 1;
	else if (rtw_wow->akm == 1 || rtw_wow->akm == 2)
		key_des_ver = 2;
	else if (rtw_wow->akm > 2 && rtw_wow->akm < 7)
		key_des_ver = 3;
	else
		key_des_ver = 0;

	skb = dev_alloc_skb(sizeof(*hdr) + sec_hdr_len + sizeof(*eapol_pkt));
	if (!skb)
		return NULL;

	hdr = skb_put_zero(skb, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					 IEEE80211_FCTL_TODS |
					 IEEE80211_FCTL_PROTECTED);

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);

	ether_addr_copy(hdr->addr1, bss_conf->bssid);
	ether_addr_copy(hdr->addr2, bss_conf->addr);
	ether_addr_copy(hdr->addr3, bss_conf->bssid);

	rcu_read_unlock();

	skb_put_zero(skb, sec_hdr_len);

	eapol_pkt = skb_put_zero(skb, sizeof(*eapol_pkt));
	memcpy(eapol_pkt->gtkbody, gtkbody, sizeof(gtkbody));
	eapol_pkt->key_des_ver = key_des_ver;

	return skb;
}

static struct sk_buff *rtw89_sa_query_get(struct rtw89_dev *rtwdev,
					  struct rtw89_vif_link *rtwvif_link)
{
	u8 sec_hdr_len = rtw89_wow_get_sec_hdr_len(rtwdev);
	struct ieee80211_bss_conf *bss_conf;
	struct ieee80211_hdr_3addr *hdr;
	struct rtw89_sa_query *sa_query;
	struct sk_buff *skb;

	skb = dev_alloc_skb(sizeof(*hdr) + sec_hdr_len + sizeof(*sa_query));
	if (!skb)
		return NULL;

	hdr = skb_put_zero(skb, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					 IEEE80211_STYPE_ACTION |
					 IEEE80211_FCTL_PROTECTED);

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);

	ether_addr_copy(hdr->addr1, bss_conf->bssid);
	ether_addr_copy(hdr->addr2, bss_conf->addr);
	ether_addr_copy(hdr->addr3, bss_conf->bssid);

	rcu_read_unlock();

	skb_put_zero(skb, sec_hdr_len);

	sa_query = skb_put_zero(skb, sizeof(*sa_query));
	sa_query->category = WLAN_CATEGORY_SA_QUERY;
	sa_query->action = WLAN_ACTION_SA_QUERY_RESPONSE;

	return skb;
}

static struct sk_buff *rtw89_arp_response_get(struct rtw89_dev *rtwdev,
					      struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	u8 sec_hdr_len = rtw89_wow_get_sec_hdr_len(rtwdev);
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_hdr_3addr *hdr;
	struct rtw89_arp_rsp *arp_skb;
	struct arphdr *arp_hdr;
	struct sk_buff *skb;
	__le16 fc;

	skb = dev_alloc_skb(sizeof(*hdr) + sec_hdr_len + sizeof(*arp_skb));
	if (!skb)
		return NULL;

	hdr = skb_put_zero(skb, sizeof(*hdr));

	if (rtw_wow->ptk_alg)
		fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_FCTL_TODS |
				 IEEE80211_FCTL_PROTECTED);
	else
		fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_FCTL_TODS);

	hdr->frame_control = fc;
	ether_addr_copy(hdr->addr1, rtwvif_link->bssid);
	ether_addr_copy(hdr->addr2, rtwvif_link->mac_addr);
	ether_addr_copy(hdr->addr3, rtwvif_link->bssid);

	skb_put_zero(skb, sec_hdr_len);

	arp_skb = skb_put_zero(skb, sizeof(*arp_skb));
	memcpy(arp_skb->llc_hdr, rfc1042_header, sizeof(rfc1042_header));
	arp_skb->llc_type = htons(ETH_P_ARP);

	arp_hdr = &arp_skb->arp_hdr;
	arp_hdr->ar_hrd = htons(ARPHRD_ETHER);
	arp_hdr->ar_pro = htons(ETH_P_IP);
	arp_hdr->ar_hln = ETH_ALEN;
	arp_hdr->ar_pln = 4;
	arp_hdr->ar_op = htons(ARPOP_REPLY);

	ether_addr_copy(arp_skb->sender_hw, rtwvif_link->mac_addr);
	arp_skb->sender_ip = rtwvif->ip_addr;

	return skb;
}

static int rtw89_fw_h2c_add_general_pkt(struct rtw89_dev *rtwdev,
					struct rtw89_vif_link *rtwvif_link,
					enum rtw89_fw_pkt_ofld_type type,
					u8 *id)
{
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	int link_id = ieee80211_vif_is_mld(vif) ? rtwvif_link->link_id : -1;
	struct rtw89_pktofld_info *info;
	struct sk_buff *skb;
	int ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	switch (type) {
	case RTW89_PKT_OFLD_TYPE_PS_POLL:
		skb = ieee80211_pspoll_get(rtwdev->hw, vif);
		break;
	case RTW89_PKT_OFLD_TYPE_PROBE_RSP:
		skb = ieee80211_proberesp_get(rtwdev->hw, vif);
		break;
	case RTW89_PKT_OFLD_TYPE_NULL_DATA:
		skb = ieee80211_nullfunc_get(rtwdev->hw, vif, link_id, false);
		break;
	case RTW89_PKT_OFLD_TYPE_QOS_NULL:
		skb = ieee80211_nullfunc_get(rtwdev->hw, vif, link_id, true);
		break;
	case RTW89_PKT_OFLD_TYPE_EAPOL_KEY:
		skb = rtw89_eapol_get(rtwdev, rtwvif_link);
		break;
	case RTW89_PKT_OFLD_TYPE_SA_QUERY:
		skb = rtw89_sa_query_get(rtwdev, rtwvif_link);
		break;
	case RTW89_PKT_OFLD_TYPE_ARP_RSP:
		skb = rtw89_arp_response_get(rtwdev, rtwvif_link);
		break;
	default:
		goto err;
	}

	if (!skb)
		goto err;

	ret = rtw89_fw_h2c_add_pkt_offload(rtwdev, &info->id, skb);
	kfree_skb(skb);

	if (ret)
		goto err;

	list_add_tail(&info->list, &rtwvif_link->general_pkt_list);
	*id = info->id;
	return 0;

err:
	kfree(info);
	return -ENOMEM;
}

void rtw89_fw_release_general_pkt_list_vif(struct rtw89_dev *rtwdev,
					   struct rtw89_vif_link *rtwvif_link,
					   bool notify_fw)
{
	struct list_head *pkt_list = &rtwvif_link->general_pkt_list;
	struct rtw89_pktofld_info *info, *tmp;

	list_for_each_entry_safe(info, tmp, pkt_list, list) {
		if (notify_fw)
			rtw89_fw_h2c_del_pkt_offload(rtwdev, info->id);
		else
			rtw89_core_release_bit_map(rtwdev->pkt_offload, info->id);
		list_del(&info->list);
		kfree(info);
	}
}

void rtw89_fw_release_general_pkt_list(struct rtw89_dev *rtwdev, bool notify_fw)
{
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	unsigned int link_id;

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
			rtw89_fw_release_general_pkt_list_vif(rtwdev, rtwvif_link,
							      notify_fw);
}

#define H2C_GENERAL_PKT_LEN 6
#define H2C_GENERAL_PKT_ID_UND 0xff
int rtw89_fw_h2c_general_pkt(struct rtw89_dev *rtwdev,
			     struct rtw89_vif_link *rtwvif_link, u8 macid)
{
	u8 pkt_id_ps_poll = H2C_GENERAL_PKT_ID_UND;
	u8 pkt_id_null = H2C_GENERAL_PKT_ID_UND;
	u8 pkt_id_qos_null = H2C_GENERAL_PKT_ID_UND;
	struct sk_buff *skb;
	int ret;

	rtw89_fw_h2c_add_general_pkt(rtwdev, rtwvif_link,
				     RTW89_PKT_OFLD_TYPE_PS_POLL, &pkt_id_ps_poll);
	rtw89_fw_h2c_add_general_pkt(rtwdev, rtwvif_link,
				     RTW89_PKT_OFLD_TYPE_NULL_DATA, &pkt_id_null);
	rtw89_fw_h2c_add_general_pkt(rtwdev, rtwvif_link,
				     RTW89_PKT_OFLD_TYPE_QOS_NULL, &pkt_id_qos_null);

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_GENERAL_PKT_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_GENERAL_PKT_LEN);
	SET_GENERAL_PKT_MACID(skb->data, macid);
	SET_GENERAL_PKT_PROBRSP_ID(skb->data, H2C_GENERAL_PKT_ID_UND);
	SET_GENERAL_PKT_PSPOLL_ID(skb->data, pkt_id_ps_poll);
	SET_GENERAL_PKT_NULL_ID(skb->data, pkt_id_null);
	SET_GENERAL_PKT_QOS_NULL_ID(skb->data, pkt_id_qos_null);
	SET_GENERAL_PKT_CTS2SELF_ID(skb->data, H2C_GENERAL_PKT_ID_UND);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_FW_INFO,
			      H2C_FUNC_MAC_GENERAL_PKT, 0, 1,
			      H2C_GENERAL_PKT_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_LPS_PARM_LEN 8
int rtw89_fw_h2c_lps_parm(struct rtw89_dev *rtwdev,
			  struct rtw89_lps_parm *lps_param)
{
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_LPS_PARM_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_LPS_PARM_LEN);

	SET_LPS_PARM_MACID(skb->data, lps_param->macid);
	SET_LPS_PARM_PSMODE(skb->data, lps_param->psmode);
	SET_LPS_PARM_LASTRPWM(skb->data, lps_param->lastrpwm);
	SET_LPS_PARM_RLBM(skb->data, 1);
	SET_LPS_PARM_SMARTPS(skb->data, 1);
	SET_LPS_PARM_AWAKEINTERVAL(skb->data, 1);
	SET_LPS_PARM_VOUAPSD(skb->data, 0);
	SET_LPS_PARM_VIUAPSD(skb->data, 0);
	SET_LPS_PARM_BEUAPSD(skb->data, 0);
	SET_LPS_PARM_BKUAPSD(skb->data, 0);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_PS,
			      H2C_FUNC_MAC_LPS_PARM, 0, !lps_param->psmode,
			      H2C_LPS_PARM_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_lps_ch_info(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_chan *chan;
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_h2c_lps_ch_info *h2c;
	u32 len = sizeof(*h2c);
	unsigned int link_id;
	struct sk_buff *skb;
	bool no_chan = true;
	u8 phy_idx;
	u32 done;
	int ret;

	if (chip->chip_gen != RTW89_CHIP_BE)
		return 0;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c lps_ch_info\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_lps_ch_info *)skb->data;

	rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id) {
		phy_idx = rtwvif_link->phy_idx;
		if (phy_idx >= ARRAY_SIZE(h2c->info))
			continue;

		chan = rtw89_chan_get(rtwdev, rtwvif_link->chanctx_idx);
		no_chan = false;

		h2c->info[phy_idx].central_ch = chan->channel;
		h2c->info[phy_idx].pri_ch = chan->primary_channel;
		h2c->info[phy_idx].band = chan->band_type;
		h2c->info[phy_idx].bw = chan->band_width;
	}

	if (no_chan) {
		rtw89_err(rtwdev, "no chan for h2c lps_ch_info\n");
		ret = -ENOENT;
		goto fail;
	}

	h2c->mlo_dbcc_mode_lps = cpu_to_le32(rtwdev->mlo_dbcc_mode);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, H2C_CL_OUTSRC_DM,
			      H2C_FUNC_FW_LPS_CH_INFO, 0, 0, len);

	rtw89_phy_write32_mask(rtwdev, R_CHK_LPS_STAT, B_CHK_LPS_STAT, 0);
	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	ret = read_poll_timeout(rtw89_phy_read32_mask, done, done, 50, 5000,
				true, rtwdev, R_CHK_LPS_STAT, B_CHK_LPS_STAT);
	if (ret)
		rtw89_warn(rtwdev, "h2c_lps_ch_info done polling timeout\n");

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_lps_ml_cmn_info(struct rtw89_dev *rtwdev,
				 struct rtw89_vif *rtwvif)
{
	const struct rtw89_phy_bb_gain_info_be *gain = &rtwdev->bb_gain.be;
	struct rtw89_pkt_stat *pkt_stat = &rtwdev->phystat.cur_pkt_stat;
	static const u8 bcn_bw_ofst[] = {0, 0, 0, 3, 6, 9, 0, 12};
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	struct rtw89_h2c_lps_ml_cmn_info *h2c;
	struct rtw89_vif_link *rtwvif_link;
	const struct rtw89_chan *chan;
	u8 bw_idx = RTW89_BB_BW_20_40;
	u32 len = sizeof(*h2c);
	unsigned int link_id;
	struct sk_buff *skb;
	u8 beacon_bw_ofst;
	u8 gain_band;
	u32 done;
	u8 path;
	int ret;
	int i;

	if (chip->chip_gen != RTW89_CHIP_BE)
		return 0;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c lps_ml_cmn_info\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_lps_ml_cmn_info *)skb->data;

	h2c->fmt_id = 0x3;

	h2c->mlo_dbcc_mode = cpu_to_le32(rtwdev->mlo_dbcc_mode);
	h2c->rfe_type = efuse->rfe_type;

	rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id) {
		path = rtwvif_link->phy_idx == RTW89_PHY_1 ? RF_PATH_B : RF_PATH_A;
		chan = rtw89_chan_get(rtwdev, rtwvif_link->chanctx_idx);
		gain_band = rtw89_subband_to_gain_band_be(chan->subband_type);

		h2c->central_ch[rtwvif_link->phy_idx] = chan->channel;
		h2c->pri_ch[rtwvif_link->phy_idx] = chan->primary_channel;
		h2c->band[rtwvif_link->phy_idx] = chan->band_type;
		h2c->bw[rtwvif_link->phy_idx] = chan->band_width;
		if (pkt_stat->beacon_rate < RTW89_HW_RATE_OFDM6)
			h2c->bcn_rate_type[rtwvif_link->phy_idx] = 0x1;
		else
			h2c->bcn_rate_type[rtwvif_link->phy_idx] = 0x2;

		/* Fill BW20 RX gain table for beacon mode */
		for (i = 0; i < TIA_GAIN_NUM; i++) {
			h2c->tia_gain[rtwvif_link->phy_idx][i] =
				cpu_to_le16(gain->tia_gain[gain_band][bw_idx][path][i]);
		}

		if (rtwvif_link->bcn_bw_idx < ARRAY_SIZE(bcn_bw_ofst)) {
			beacon_bw_ofst = bcn_bw_ofst[rtwvif_link->bcn_bw_idx];
			h2c->dup_bcn_ofst[rtwvif_link->phy_idx] = beacon_bw_ofst;
		}

		memcpy(h2c->lna_gain[rtwvif_link->phy_idx],
		       gain->lna_gain[gain_band][bw_idx][path],
		       LNA_GAIN_NUM);
		memcpy(h2c->tia_lna_op1db[rtwvif_link->phy_idx],
		       gain->tia_lna_op1db[gain_band][bw_idx][path],
		       LNA_GAIN_NUM + 1);
		memcpy(h2c->lna_op1db[rtwvif_link->phy_idx],
		       gain->lna_op1db[gain_band][bw_idx][path],
		       LNA_GAIN_NUM);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, H2C_CL_OUTSRC_DM,
			      H2C_FUNC_FW_LPS_ML_CMN_INFO, 0, 0, len);

	rtw89_phy_write32_mask(rtwdev, R_CHK_LPS_STAT, B_CHK_LPS_STAT, 0);
	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	ret = read_poll_timeout(rtw89_phy_read32_mask, done, done, 50, 5000,
				true, rtwdev, R_CHK_LPS_STAT, B_CHK_LPS_STAT);
	if (ret)
		rtw89_warn(rtwdev, "h2c_lps_ml_cmn_info done polling timeout\n");

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_P2P_ACT_LEN 20
int rtw89_fw_h2c_p2p_act(struct rtw89_dev *rtwdev,
			 struct rtw89_vif_link *rtwvif_link,
			 struct ieee80211_bss_conf *bss_conf,
			 struct ieee80211_p2p_noa_desc *desc,
			 u8 act, u8 noa_id)
{
	bool p2p_type_gc = rtwvif_link->wifi_role == RTW89_WIFI_ROLE_P2P_CLIENT;
	u8 ctwindow_oppps = bss_conf->p2p_noa_attr.oppps_ctwindow;
	struct sk_buff *skb;
	u8 *cmd;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_P2P_ACT_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c p2p act\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_P2P_ACT_LEN);
	cmd = skb->data;

	RTW89_SET_FWCMD_P2P_MACID(cmd, rtwvif_link->mac_id);
	RTW89_SET_FWCMD_P2P_P2PID(cmd, 0);
	RTW89_SET_FWCMD_P2P_NOAID(cmd, noa_id);
	RTW89_SET_FWCMD_P2P_ACT(cmd, act);
	RTW89_SET_FWCMD_P2P_TYPE(cmd, p2p_type_gc);
	RTW89_SET_FWCMD_P2P_ALL_SLEP(cmd, 0);
	if (desc) {
		RTW89_SET_FWCMD_NOA_START_TIME(cmd, desc->start_time);
		RTW89_SET_FWCMD_NOA_INTERVAL(cmd, desc->interval);
		RTW89_SET_FWCMD_NOA_DURATION(cmd, desc->duration);
		RTW89_SET_FWCMD_NOA_COUNT(cmd, desc->count);
		RTW89_SET_FWCMD_NOA_CTWINDOW(cmd, ctwindow_oppps);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_PS,
			      H2C_FUNC_P2P_ACT, 0, 0,
			      H2C_P2P_ACT_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

static void __rtw89_fw_h2c_set_tx_path(struct rtw89_dev *rtwdev,
				       struct sk_buff *skb)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 ntx_path;
	u8 map_b;

	if (chip->rf_path_num == 1) {
		ntx_path = RF_A;
		map_b = 0;
	} else {
		ntx_path = hal->antenna_tx ? hal->antenna_tx : RF_AB;
		map_b = ntx_path == RF_AB ? 1 : 0;
	}

	SET_CMC_TBL_NTX_PATH_EN(skb->data, ntx_path);
	SET_CMC_TBL_PATH_MAP_A(skb->data, 0);
	SET_CMC_TBL_PATH_MAP_B(skb->data, map_b);
	SET_CMC_TBL_PATH_MAP_C(skb->data, 0);
	SET_CMC_TBL_PATH_MAP_D(skb->data, 0);
}

#define H2C_CMC_TBL_LEN 68
int rtw89_fw_h2c_default_cmac_tbl(struct rtw89_dev *rtwdev,
				  struct rtw89_vif_link *rtwvif_link,
				  struct rtw89_sta_link *rtwsta_link)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 macid = rtwsta_link ? rtwsta_link->mac_id : rtwvif_link->mac_id;
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_CMC_TBL_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_CMC_TBL_LEN);
	SET_CTRL_INFO_MACID(skb->data, macid);
	SET_CTRL_INFO_OPERATION(skb->data, 1);
	if (chip->h2c_cctl_func_id == H2C_FUNC_MAC_CCTLINFO_UD) {
		SET_CMC_TBL_TXPWR_MODE(skb->data, 0);
		__rtw89_fw_h2c_set_tx_path(rtwdev, skb);
		SET_CMC_TBL_ANTSEL_A(skb->data, 0);
		SET_CMC_TBL_ANTSEL_B(skb->data, 0);
		SET_CMC_TBL_ANTSEL_C(skb->data, 0);
		SET_CMC_TBL_ANTSEL_D(skb->data, 0);
	}
	SET_CMC_TBL_DOPPLER_CTRL(skb->data, 0);
	SET_CMC_TBL_TXPWR_TOLERENCE(skb->data, 0);
	if (rtwvif_link->net_type == RTW89_NET_TYPE_AP_MODE)
		SET_CMC_TBL_DATA_DCM(skb->data, 0);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FR_EXCHG,
			      chip->h2c_cctl_func_id, 0, 1,
			      H2C_CMC_TBL_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_default_cmac_tbl);

int rtw89_fw_h2c_default_cmac_tbl_g7(struct rtw89_dev *rtwdev,
				     struct rtw89_vif_link *rtwvif_link,
				     struct rtw89_sta_link *rtwsta_link)
{
	u8 mac_id = rtwsta_link ? rtwsta_link->mac_id : rtwvif_link->mac_id;
	struct rtw89_h2c_cctlinfo_ud_g7 *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for cmac g7\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_cctlinfo_ud_g7 *)skb->data;

	h2c->c0 = le32_encode_bits(mac_id, CCTLINFO_G7_C0_MACID) |
		  le32_encode_bits(1, CCTLINFO_G7_C0_OP);

	h2c->w0 = le32_encode_bits(4, CCTLINFO_G7_W0_DATARATE);
	h2c->m0 = cpu_to_le32(CCTLINFO_G7_W0_ALL);

	h2c->w1 = le32_encode_bits(4, CCTLINFO_G7_W1_DATA_RTY_LOWEST_RATE) |
		  le32_encode_bits(0xa, CCTLINFO_G7_W1_RTSRATE) |
		  le32_encode_bits(4, CCTLINFO_G7_W1_RTS_RTY_LOWEST_RATE);
	h2c->m1 = cpu_to_le32(CCTLINFO_G7_W1_ALL);

	h2c->m2 = cpu_to_le32(CCTLINFO_G7_W2_ALL);

	h2c->m3 = cpu_to_le32(CCTLINFO_G7_W3_ALL);

	h2c->w4 = le32_encode_bits(0xFFFF, CCTLINFO_G7_W4_ACT_SUBCH_CBW);
	h2c->m4 = cpu_to_le32(CCTLINFO_G7_W4_ALL);

	h2c->w5 = le32_encode_bits(2, CCTLINFO_G7_W5_NOMINAL_PKT_PADDING0) |
		  le32_encode_bits(2, CCTLINFO_G7_W5_NOMINAL_PKT_PADDING1) |
		  le32_encode_bits(2, CCTLINFO_G7_W5_NOMINAL_PKT_PADDING2) |
		  le32_encode_bits(2, CCTLINFO_G7_W5_NOMINAL_PKT_PADDING3) |
		  le32_encode_bits(2, CCTLINFO_G7_W5_NOMINAL_PKT_PADDING4);
	h2c->m5 = cpu_to_le32(CCTLINFO_G7_W5_ALL);

	h2c->w6 = le32_encode_bits(0xb, CCTLINFO_G7_W6_RESP_REF_RATE);
	h2c->m6 = cpu_to_le32(CCTLINFO_G7_W6_ALL);

	h2c->w7 = le32_encode_bits(1, CCTLINFO_G7_W7_NC) |
		  le32_encode_bits(1, CCTLINFO_G7_W7_NR) |
		  le32_encode_bits(1, CCTLINFO_G7_W7_CB) |
		  le32_encode_bits(0x1, CCTLINFO_G7_W7_CSI_PARA_EN) |
		  le32_encode_bits(0xb, CCTLINFO_G7_W7_CSI_FIX_RATE);
	h2c->m7 = cpu_to_le32(CCTLINFO_G7_W7_ALL);

	h2c->m8 = cpu_to_le32(CCTLINFO_G7_W8_ALL);

	h2c->w14 = le32_encode_bits(0, CCTLINFO_G7_W14_VO_CURR_RATE) |
		   le32_encode_bits(0, CCTLINFO_G7_W14_VI_CURR_RATE) |
		   le32_encode_bits(0, CCTLINFO_G7_W14_BE_CURR_RATE_L);
	h2c->m14 = cpu_to_le32(CCTLINFO_G7_W14_ALL);

	h2c->w15 = le32_encode_bits(0, CCTLINFO_G7_W15_BE_CURR_RATE_H) |
		   le32_encode_bits(0, CCTLINFO_G7_W15_BK_CURR_RATE) |
		   le32_encode_bits(0, CCTLINFO_G7_W15_MGNT_CURR_RATE);
	h2c->m15 = cpu_to_le32(CCTLINFO_G7_W15_ALL);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_CCTLINFO_UD_G7, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_default_cmac_tbl_g7);

static void __get_sta_he_pkt_padding(struct rtw89_dev *rtwdev,
				     struct ieee80211_link_sta *link_sta,
				     u8 *pads)
{
	bool ppe_th;
	u8 ppe16, ppe8;
	u8 nss = min(link_sta->rx_nss, rtwdev->hal.tx_nss) - 1;
	u8 ppe_thres_hdr = link_sta->he_cap.ppe_thres[0];
	u8 ru_bitmap;
	u8 n, idx, sh;
	u16 ppe;
	int i;

	ppe_th = FIELD_GET(IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT,
			   link_sta->he_cap.he_cap_elem.phy_cap_info[6]);
	if (!ppe_th) {
		u8 pad;

		pad = FIELD_GET(IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK,
				link_sta->he_cap.he_cap_elem.phy_cap_info[9]);

		for (i = 0; i < RTW89_PPE_BW_NUM; i++)
			pads[i] = pad;

		return;
	}

	ru_bitmap = FIELD_GET(IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK, ppe_thres_hdr);
	n = hweight8(ru_bitmap);
	n = 7 + (n * IEEE80211_PPE_THRES_INFO_PPET_SIZE * 2) * nss;

	for (i = 0; i < RTW89_PPE_BW_NUM; i++) {
		if (!(ru_bitmap & BIT(i))) {
			pads[i] = 1;
			continue;
		}

		idx = n >> 3;
		sh = n & 7;
		n += IEEE80211_PPE_THRES_INFO_PPET_SIZE * 2;

		ppe = le16_to_cpu(*((__le16 *)&link_sta->he_cap.ppe_thres[idx]));
		ppe16 = (ppe >> sh) & IEEE80211_PPE_THRES_NSS_MASK;
		sh += IEEE80211_PPE_THRES_INFO_PPET_SIZE;
		ppe8 = (ppe >> sh) & IEEE80211_PPE_THRES_NSS_MASK;

		if (ppe16 != 7 && ppe8 == 7)
			pads[i] = RTW89_PE_DURATION_16;
		else if (ppe8 != 7)
			pads[i] = RTW89_PE_DURATION_8;
		else
			pads[i] = RTW89_PE_DURATION_0;
	}
}

int rtw89_fw_h2c_assoc_cmac_tbl(struct rtw89_dev *rtwdev,
				struct rtw89_vif_link *rtwvif_link,
				struct rtw89_sta_link *rtwsta_link)
{
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev,
						       rtwvif_link->chanctx_idx);
	struct ieee80211_link_sta *link_sta;
	struct sk_buff *skb;
	u8 pads[RTW89_PPE_BW_NUM];
	u8 mac_id = rtwsta_link ? rtwsta_link->mac_id : rtwvif_link->mac_id;
	u16 lowest_rate;
	int ret;

	memset(pads, 0, sizeof(pads));

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_CMC_TBL_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}

	rcu_read_lock();

	if (rtwsta_link)
		link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, true);

	if (rtwsta_link && link_sta->he_cap.has_he)
		__get_sta_he_pkt_padding(rtwdev, link_sta, pads);

	if (vif->p2p)
		lowest_rate = RTW89_HW_RATE_OFDM6;
	else if (chan->band_type == RTW89_BAND_2G)
		lowest_rate = RTW89_HW_RATE_CCK1;
	else
		lowest_rate = RTW89_HW_RATE_OFDM6;

	skb_put(skb, H2C_CMC_TBL_LEN);
	SET_CTRL_INFO_MACID(skb->data, mac_id);
	SET_CTRL_INFO_OPERATION(skb->data, 1);
	SET_CMC_TBL_DISRTSFB(skb->data, 1);
	SET_CMC_TBL_DISDATAFB(skb->data, 1);
	SET_CMC_TBL_RTS_RTY_LOWEST_RATE(skb->data, lowest_rate);
	SET_CMC_TBL_RTS_TXCNT_LMT_SEL(skb->data, 0);
	SET_CMC_TBL_DATA_TXCNT_LMT_SEL(skb->data, 0);
	if (vif->type == NL80211_IFTYPE_STATION)
		SET_CMC_TBL_ULDL(skb->data, 1);
	else
		SET_CMC_TBL_ULDL(skb->data, 0);
	SET_CMC_TBL_MULTI_PORT_ID(skb->data, rtwvif_link->port);
	if (chip->h2c_cctl_func_id == H2C_FUNC_MAC_CCTLINFO_UD_V1) {
		SET_CMC_TBL_NOMINAL_PKT_PADDING_V1(skb->data, pads[RTW89_CHANNEL_WIDTH_20]);
		SET_CMC_TBL_NOMINAL_PKT_PADDING40_V1(skb->data, pads[RTW89_CHANNEL_WIDTH_40]);
		SET_CMC_TBL_NOMINAL_PKT_PADDING80_V1(skb->data, pads[RTW89_CHANNEL_WIDTH_80]);
		SET_CMC_TBL_NOMINAL_PKT_PADDING160_V1(skb->data, pads[RTW89_CHANNEL_WIDTH_160]);
	} else if (chip->h2c_cctl_func_id == H2C_FUNC_MAC_CCTLINFO_UD) {
		SET_CMC_TBL_NOMINAL_PKT_PADDING(skb->data, pads[RTW89_CHANNEL_WIDTH_20]);
		SET_CMC_TBL_NOMINAL_PKT_PADDING40(skb->data, pads[RTW89_CHANNEL_WIDTH_40]);
		SET_CMC_TBL_NOMINAL_PKT_PADDING80(skb->data, pads[RTW89_CHANNEL_WIDTH_80]);
		SET_CMC_TBL_NOMINAL_PKT_PADDING160(skb->data, pads[RTW89_CHANNEL_WIDTH_160]);
	}
	if (rtwsta_link)
		SET_CMC_TBL_BSR_QUEUE_SIZE_FORMAT(skb->data,
						  link_sta->he_cap.has_he);
	if (rtwvif_link->net_type == RTW89_NET_TYPE_AP_MODE)
		SET_CMC_TBL_DATA_DCM(skb->data, 0);

	rcu_read_unlock();

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FR_EXCHG,
			      chip->h2c_cctl_func_id, 0, 1,
			      H2C_CMC_TBL_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_assoc_cmac_tbl);

static void __get_sta_eht_pkt_padding(struct rtw89_dev *rtwdev,
				      struct ieee80211_link_sta *link_sta,
				      u8 *pads)
{
	u8 nss = min(link_sta->rx_nss, rtwdev->hal.tx_nss) - 1;
	u16 ppe_thres_hdr;
	u8 ppe16, ppe8;
	u8 n, idx, sh;
	u8 ru_bitmap;
	bool ppe_th;
	u16 ppe;
	int i;

	ppe_th = !!u8_get_bits(link_sta->eht_cap.eht_cap_elem.phy_cap_info[5],
			       IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT);
	if (!ppe_th) {
		u8 pad;

		pad = u8_get_bits(link_sta->eht_cap.eht_cap_elem.phy_cap_info[5],
				  IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK);

		for (i = 0; i < RTW89_PPE_BW_NUM; i++)
			pads[i] = pad;

		return;
	}

	ppe_thres_hdr = get_unaligned_le16(link_sta->eht_cap.eht_ppe_thres);
	ru_bitmap = u16_get_bits(ppe_thres_hdr,
				 IEEE80211_EHT_PPE_THRES_RU_INDEX_BITMASK_MASK);
	n = hweight8(ru_bitmap);
	n = IEEE80211_EHT_PPE_THRES_INFO_HEADER_SIZE +
	    (n * IEEE80211_EHT_PPE_THRES_INFO_PPET_SIZE * 2) * nss;

	for (i = 0; i < RTW89_PPE_BW_NUM; i++) {
		if (!(ru_bitmap & BIT(i))) {
			pads[i] = 1;
			continue;
		}

		idx = n >> 3;
		sh = n & 7;
		n += IEEE80211_EHT_PPE_THRES_INFO_PPET_SIZE * 2;

		ppe = get_unaligned_le16(link_sta->eht_cap.eht_ppe_thres + idx);
		ppe16 = (ppe >> sh) & IEEE80211_PPE_THRES_NSS_MASK;
		sh += IEEE80211_EHT_PPE_THRES_INFO_PPET_SIZE;
		ppe8 = (ppe >> sh) & IEEE80211_PPE_THRES_NSS_MASK;

		if (ppe16 != 7 && ppe8 == 7)
			pads[i] = RTW89_PE_DURATION_16_20;
		else if (ppe8 != 7)
			pads[i] = RTW89_PE_DURATION_8;
		else
			pads[i] = RTW89_PE_DURATION_0;
	}
}

int rtw89_fw_h2c_assoc_cmac_tbl_g7(struct rtw89_dev *rtwdev,
				   struct rtw89_vif_link *rtwvif_link,
				   struct rtw89_sta_link *rtwsta_link)
{
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, rtwvif_link->chanctx_idx);
	u8 mac_id = rtwsta_link ? rtwsta_link->mac_id : rtwvif_link->mac_id;
	struct rtw89_h2c_cctlinfo_ud_g7 *h2c;
	struct ieee80211_bss_conf *bss_conf;
	struct ieee80211_link_sta *link_sta;
	u8 pads[RTW89_PPE_BW_NUM];
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	u16 lowest_rate;
	int ret;

	memset(pads, 0, sizeof(pads));

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for cmac g7\n");
		return -ENOMEM;
	}

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);

	if (rtwsta_link) {
		link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, true);

		if (link_sta->eht_cap.has_eht)
			__get_sta_eht_pkt_padding(rtwdev, link_sta, pads);
		else if (link_sta->he_cap.has_he)
			__get_sta_he_pkt_padding(rtwdev, link_sta, pads);
	}

	if (vif->p2p)
		lowest_rate = RTW89_HW_RATE_OFDM6;
	else if (chan->band_type == RTW89_BAND_2G)
		lowest_rate = RTW89_HW_RATE_CCK1;
	else
		lowest_rate = RTW89_HW_RATE_OFDM6;

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_cctlinfo_ud_g7 *)skb->data;

	h2c->c0 = le32_encode_bits(mac_id, CCTLINFO_G7_C0_MACID) |
		  le32_encode_bits(1, CCTLINFO_G7_C0_OP);

	h2c->w0 = le32_encode_bits(1, CCTLINFO_G7_W0_DISRTSFB) |
		  le32_encode_bits(1, CCTLINFO_G7_W0_DISDATAFB);
	h2c->m0 = cpu_to_le32(CCTLINFO_G7_W0_DISRTSFB |
			      CCTLINFO_G7_W0_DISDATAFB);

	h2c->w1 = le32_encode_bits(lowest_rate, CCTLINFO_G7_W1_RTS_RTY_LOWEST_RATE);
	h2c->m1 = cpu_to_le32(CCTLINFO_G7_W1_RTS_RTY_LOWEST_RATE);

	h2c->w2 = le32_encode_bits(0, CCTLINFO_G7_W2_DATA_TXCNT_LMT_SEL);
	h2c->m2 = cpu_to_le32(CCTLINFO_G7_W2_DATA_TXCNT_LMT_SEL);

	h2c->w3 = le32_encode_bits(0, CCTLINFO_G7_W3_RTS_TXCNT_LMT_SEL);
	h2c->m3 = cpu_to_le32(CCTLINFO_G7_W3_RTS_TXCNT_LMT_SEL);

	h2c->w4 = le32_encode_bits(rtwvif_link->port, CCTLINFO_G7_W4_MULTI_PORT_ID);
	h2c->m4 = cpu_to_le32(CCTLINFO_G7_W4_MULTI_PORT_ID);

	if (rtwvif_link->net_type == RTW89_NET_TYPE_AP_MODE) {
		h2c->w4 |= le32_encode_bits(0, CCTLINFO_G7_W4_DATA_DCM);
		h2c->m4 |= cpu_to_le32(CCTLINFO_G7_W4_DATA_DCM);
	}

	if (bss_conf->eht_support) {
		u16 punct = bss_conf->chanreq.oper.punctured;

		h2c->w4 |= le32_encode_bits(~punct,
					    CCTLINFO_G7_W4_ACT_SUBCH_CBW);
		h2c->m4 |= cpu_to_le32(CCTLINFO_G7_W4_ACT_SUBCH_CBW);
	}

	h2c->w5 = le32_encode_bits(pads[RTW89_CHANNEL_WIDTH_20],
				   CCTLINFO_G7_W5_NOMINAL_PKT_PADDING0) |
		  le32_encode_bits(pads[RTW89_CHANNEL_WIDTH_40],
				   CCTLINFO_G7_W5_NOMINAL_PKT_PADDING1) |
		  le32_encode_bits(pads[RTW89_CHANNEL_WIDTH_80],
				   CCTLINFO_G7_W5_NOMINAL_PKT_PADDING2) |
		  le32_encode_bits(pads[RTW89_CHANNEL_WIDTH_160],
				   CCTLINFO_G7_W5_NOMINAL_PKT_PADDING3) |
		  le32_encode_bits(pads[RTW89_CHANNEL_WIDTH_320],
				   CCTLINFO_G7_W5_NOMINAL_PKT_PADDING4);
	h2c->m5 = cpu_to_le32(CCTLINFO_G7_W5_NOMINAL_PKT_PADDING0 |
			      CCTLINFO_G7_W5_NOMINAL_PKT_PADDING1 |
			      CCTLINFO_G7_W5_NOMINAL_PKT_PADDING2 |
			      CCTLINFO_G7_W5_NOMINAL_PKT_PADDING3 |
			      CCTLINFO_G7_W5_NOMINAL_PKT_PADDING4);

	h2c->w6 = le32_encode_bits(vif->cfg.aid, CCTLINFO_G7_W6_AID12_PAID) |
		  le32_encode_bits(vif->type == NL80211_IFTYPE_STATION ? 1 : 0,
				   CCTLINFO_G7_W6_ULDL);
	h2c->m6 = cpu_to_le32(CCTLINFO_G7_W6_AID12_PAID | CCTLINFO_G7_W6_ULDL);

	if (rtwsta_link) {
		h2c->w8 = le32_encode_bits(link_sta->he_cap.has_he,
					   CCTLINFO_G7_W8_BSR_QUEUE_SIZE_FORMAT);
		h2c->m8 = cpu_to_le32(CCTLINFO_G7_W8_BSR_QUEUE_SIZE_FORMAT);
	}

	rcu_read_unlock();

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_CCTLINFO_UD_G7, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_assoc_cmac_tbl_g7);

int rtw89_fw_h2c_ampdu_cmac_tbl_g7(struct rtw89_dev *rtwdev,
				   struct rtw89_vif_link *rtwvif_link,
				   struct rtw89_sta_link *rtwsta_link)
{
	struct rtw89_sta *rtwsta = rtwsta_link->rtwsta;
	struct rtw89_h2c_cctlinfo_ud_g7 *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	u16 agg_num = 0;
	u8 ba_bmap = 0;
	int ret;
	u8 tid;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for ampdu cmac g7\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_cctlinfo_ud_g7 *)skb->data;

	for_each_set_bit(tid, rtwsta->ampdu_map, IEEE80211_NUM_TIDS) {
		if (agg_num == 0)
			agg_num = rtwsta->ampdu_params[tid].agg_num;
		else
			agg_num = min(agg_num, rtwsta->ampdu_params[tid].agg_num);
	}

	if (agg_num <= 0x20)
		ba_bmap = 3;
	else if (agg_num > 0x20 && agg_num <= 0x40)
		ba_bmap = 0;
	else if (agg_num > 0x40 && agg_num <= 0x80)
		ba_bmap = 1;
	else if (agg_num > 0x80 && agg_num <= 0x100)
		ba_bmap = 2;
	else if (agg_num > 0x100 && agg_num <= 0x200)
		ba_bmap = 4;
	else if (agg_num > 0x200 && agg_num <= 0x400)
		ba_bmap = 5;

	h2c->c0 = le32_encode_bits(rtwsta_link->mac_id, CCTLINFO_G7_C0_MACID) |
		  le32_encode_bits(1, CCTLINFO_G7_C0_OP);

	h2c->w3 = le32_encode_bits(ba_bmap, CCTLINFO_G7_W3_BA_BMAP);
	h2c->m3 = cpu_to_le32(CCTLINFO_G7_W3_BA_BMAP);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_CCTLINFO_UD_G7, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_ampdu_cmac_tbl_g7);

int rtw89_fw_h2c_txtime_cmac_tbl(struct rtw89_dev *rtwdev,
				 struct rtw89_sta_link *rtwsta_link)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_CMC_TBL_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_CMC_TBL_LEN);
	SET_CTRL_INFO_MACID(skb->data, rtwsta_link->mac_id);
	SET_CTRL_INFO_OPERATION(skb->data, 1);
	if (rtwsta_link->cctl_tx_time) {
		SET_CMC_TBL_AMPDU_TIME_SEL(skb->data, 1);
		SET_CMC_TBL_AMPDU_MAX_TIME(skb->data, rtwsta_link->ampdu_max_time);
	}
	if (rtwsta_link->cctl_tx_retry_limit) {
		SET_CMC_TBL_DATA_TXCNT_LMT_SEL(skb->data, 1);
		SET_CMC_TBL_DATA_TX_CNT_LMT(skb->data, rtwsta_link->data_tx_cnt_lmt);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FR_EXCHG,
			      chip->h2c_cctl_func_id, 0, 1,
			      H2C_CMC_TBL_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_txtime_cmac_tbl);

int rtw89_fw_h2c_txtime_cmac_tbl_g7(struct rtw89_dev *rtwdev,
				    struct rtw89_sta_link *rtwsta_link)
{
	struct rtw89_h2c_cctlinfo_ud_g7 *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for txtime_cmac_g7\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_cctlinfo_ud_g7 *)skb->data;

	h2c->c0 = le32_encode_bits(rtwsta_link->mac_id, CCTLINFO_G7_C0_MACID) |
		  le32_encode_bits(1, CCTLINFO_G7_C0_OP);

	if (rtwsta_link->cctl_tx_time) {
		h2c->w3 |= le32_encode_bits(1, CCTLINFO_G7_W3_AMPDU_TIME_SEL);
		h2c->m3 |= cpu_to_le32(CCTLINFO_G7_W3_AMPDU_TIME_SEL);

		h2c->w2 |= le32_encode_bits(rtwsta_link->ampdu_max_time,
					   CCTLINFO_G7_W2_AMPDU_MAX_TIME);
		h2c->m2 |= cpu_to_le32(CCTLINFO_G7_W2_AMPDU_MAX_TIME);
	}
	if (rtwsta_link->cctl_tx_retry_limit) {
		h2c->w2 |= le32_encode_bits(1, CCTLINFO_G7_W2_DATA_TXCNT_LMT_SEL) |
			   le32_encode_bits(rtwsta_link->data_tx_cnt_lmt,
					    CCTLINFO_G7_W2_DATA_TX_CNT_LMT);
		h2c->m2 |= cpu_to_le32(CCTLINFO_G7_W2_DATA_TXCNT_LMT_SEL |
				       CCTLINFO_G7_W2_DATA_TX_CNT_LMT);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_CCTLINFO_UD_G7, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_txtime_cmac_tbl_g7);

int rtw89_fw_h2c_txpath_cmac_tbl(struct rtw89_dev *rtwdev,
				 struct rtw89_sta_link *rtwsta_link)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct sk_buff *skb;
	int ret;

	if (chip->h2c_cctl_func_id != H2C_FUNC_MAC_CCTLINFO_UD)
		return 0;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_CMC_TBL_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_CMC_TBL_LEN);
	SET_CTRL_INFO_MACID(skb->data, rtwsta_link->mac_id);
	SET_CTRL_INFO_OPERATION(skb->data, 1);

	__rtw89_fw_h2c_set_tx_path(rtwdev, skb);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_CCTLINFO_UD, 0, 1,
			      H2C_CMC_TBL_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_update_beacon(struct rtw89_dev *rtwdev,
			       struct rtw89_vif_link *rtwvif_link)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev,
						       rtwvif_link->chanctx_idx);
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	struct rtw89_h2c_bcn_upd *h2c;
	struct sk_buff *skb_beacon;
	struct ieee80211_hdr *hdr;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int bcn_total_len;
	u16 beacon_rate;
	u16 tim_offset;
	void *noa_data;
	u8 noa_len;
	int ret;

	if (vif->p2p)
		beacon_rate = RTW89_HW_RATE_OFDM6;
	else if (chan->band_type == RTW89_BAND_2G)
		beacon_rate = RTW89_HW_RATE_CCK1;
	else
		beacon_rate = RTW89_HW_RATE_OFDM6;

	skb_beacon = ieee80211_beacon_get_tim(rtwdev->hw, vif, &tim_offset,
					      NULL, 0);
	if (!skb_beacon) {
		rtw89_err(rtwdev, "failed to get beacon skb\n");
		return -ENOMEM;
	}

	noa_len = rtw89_p2p_noa_fetch(rtwvif_link, &noa_data);
	if (noa_len &&
	    (noa_len <= skb_tailroom(skb_beacon) ||
	     pskb_expand_head(skb_beacon, 0, noa_len, GFP_KERNEL) == 0)) {
		skb_put_data(skb_beacon, noa_data, noa_len);
	}

	hdr = (struct ieee80211_hdr *)skb_beacon;
	tim_offset -= ieee80211_hdrlen(hdr->frame_control);

	bcn_total_len = len + skb_beacon->len;
	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, bcn_total_len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		dev_kfree_skb_any(skb_beacon);
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_bcn_upd *)skb->data;

	h2c->w0 = le32_encode_bits(rtwvif_link->port, RTW89_H2C_BCN_UPD_W0_PORT) |
		  le32_encode_bits(0, RTW89_H2C_BCN_UPD_W0_MBSSID) |
		  le32_encode_bits(rtwvif_link->mac_idx, RTW89_H2C_BCN_UPD_W0_BAND) |
		  le32_encode_bits(tim_offset | BIT(7), RTW89_H2C_BCN_UPD_W0_GRP_IE_OFST);
	h2c->w1 = le32_encode_bits(rtwvif_link->mac_id, RTW89_H2C_BCN_UPD_W1_MACID) |
		  le32_encode_bits(RTW89_MGMT_HW_SSN_SEL, RTW89_H2C_BCN_UPD_W1_SSN_SEL) |
		  le32_encode_bits(RTW89_MGMT_HW_SEQ_MODE, RTW89_H2C_BCN_UPD_W1_SSN_MODE) |
		  le32_encode_bits(beacon_rate, RTW89_H2C_BCN_UPD_W1_RATE);

	skb_put_data(skb, skb_beacon->data, skb_beacon->len);
	dev_kfree_skb_any(skb_beacon);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_BCN_UPD, 0, 1,
			      bcn_total_len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		dev_kfree_skb_any(skb);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(rtw89_fw_h2c_update_beacon);

int rtw89_fw_h2c_update_beacon_be(struct rtw89_dev *rtwdev,
				  struct rtw89_vif_link *rtwvif_link)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, rtwvif_link->chanctx_idx);
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	struct rtw89_h2c_bcn_upd_be *h2c;
	struct sk_buff *skb_beacon;
	struct ieee80211_hdr *hdr;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int bcn_total_len;
	u16 beacon_rate;
	u16 tim_offset;
	void *noa_data;
	u8 noa_len;
	int ret;

	if (vif->p2p)
		beacon_rate = RTW89_HW_RATE_OFDM6;
	else if (chan->band_type == RTW89_BAND_2G)
		beacon_rate = RTW89_HW_RATE_CCK1;
	else
		beacon_rate = RTW89_HW_RATE_OFDM6;

	skb_beacon = ieee80211_beacon_get_tim(rtwdev->hw, vif, &tim_offset,
					      NULL, 0);
	if (!skb_beacon) {
		rtw89_err(rtwdev, "failed to get beacon skb\n");
		return -ENOMEM;
	}

	noa_len = rtw89_p2p_noa_fetch(rtwvif_link, &noa_data);
	if (noa_len &&
	    (noa_len <= skb_tailroom(skb_beacon) ||
	     pskb_expand_head(skb_beacon, 0, noa_len, GFP_KERNEL) == 0)) {
		skb_put_data(skb_beacon, noa_data, noa_len);
	}

	hdr = (struct ieee80211_hdr *)skb_beacon;
	tim_offset -= ieee80211_hdrlen(hdr->frame_control);

	bcn_total_len = len + skb_beacon->len;
	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, bcn_total_len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		dev_kfree_skb_any(skb_beacon);
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_bcn_upd_be *)skb->data;

	h2c->w0 = le32_encode_bits(rtwvif_link->port, RTW89_H2C_BCN_UPD_BE_W0_PORT) |
		  le32_encode_bits(0, RTW89_H2C_BCN_UPD_BE_W0_MBSSID) |
		  le32_encode_bits(rtwvif_link->mac_idx, RTW89_H2C_BCN_UPD_BE_W0_BAND) |
		  le32_encode_bits(tim_offset | BIT(7), RTW89_H2C_BCN_UPD_BE_W0_GRP_IE_OFST);
	h2c->w1 = le32_encode_bits(rtwvif_link->mac_id, RTW89_H2C_BCN_UPD_BE_W1_MACID) |
		  le32_encode_bits(RTW89_MGMT_HW_SSN_SEL, RTW89_H2C_BCN_UPD_BE_W1_SSN_SEL) |
		  le32_encode_bits(RTW89_MGMT_HW_SEQ_MODE, RTW89_H2C_BCN_UPD_BE_W1_SSN_MODE) |
		  le32_encode_bits(beacon_rate, RTW89_H2C_BCN_UPD_BE_W1_RATE);

	skb_put_data(skb, skb_beacon->data, skb_beacon->len);
	dev_kfree_skb_any(skb_beacon);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_BCN_UPD_BE, 0, 1,
			      bcn_total_len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;

fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_update_beacon_be);

int rtw89_fw_h2c_role_maintain(struct rtw89_dev *rtwdev,
			       struct rtw89_vif_link *rtwvif_link,
			       struct rtw89_sta_link *rtwsta_link,
			       enum rtw89_upd_mode upd_mode)
{
	u8 mac_id = rtwsta_link ? rtwsta_link->mac_id : rtwvif_link->mac_id;
	struct rtw89_h2c_role_maintain *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	u8 self_role;
	int ret;

	if (rtwvif_link->net_type == RTW89_NET_TYPE_AP_MODE) {
		if (rtwsta_link)
			self_role = RTW89_SELF_ROLE_AP_CLIENT;
		else
			self_role = rtwvif_link->self_role;
	} else {
		self_role = rtwvif_link->self_role;
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c join\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_role_maintain *)skb->data;

	h2c->w0 = le32_encode_bits(mac_id, RTW89_H2C_ROLE_MAINTAIN_W0_MACID) |
		  le32_encode_bits(self_role, RTW89_H2C_ROLE_MAINTAIN_W0_SELF_ROLE) |
		  le32_encode_bits(upd_mode, RTW89_H2C_ROLE_MAINTAIN_W0_UPD_MODE) |
		  le32_encode_bits(rtwvif_link->wifi_role,
				   RTW89_H2C_ROLE_MAINTAIN_W0_WIFI_ROLE) |
		  le32_encode_bits(rtwvif_link->mac_idx,
				   RTW89_H2C_ROLE_MAINTAIN_W0_BAND) |
		  le32_encode_bits(rtwvif_link->port, RTW89_H2C_ROLE_MAINTAIN_W0_PORT);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_MEDIA_RPT,
			      H2C_FUNC_MAC_FWROLE_MAINTAIN, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

static enum rtw89_fw_sta_type
rtw89_fw_get_sta_type(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
		      struct rtw89_sta_link *rtwsta_link)
{
	struct ieee80211_bss_conf *bss_conf;
	struct ieee80211_link_sta *link_sta;
	enum rtw89_fw_sta_type type;

	rcu_read_lock();

	if (!rtwsta_link)
		goto by_vif;

	link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, true);

	if (link_sta->eht_cap.has_eht)
		type = RTW89_FW_BE_STA;
	else if (link_sta->he_cap.has_he)
		type = RTW89_FW_AX_STA;
	else
		type = RTW89_FW_N_AC_STA;

	goto out;

by_vif:
	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);

	if (bss_conf->eht_support)
		type = RTW89_FW_BE_STA;
	else if (bss_conf->he_support)
		type = RTW89_FW_AX_STA;
	else
		type = RTW89_FW_N_AC_STA;

out:
	rcu_read_unlock();

	return type;
}

int rtw89_fw_h2c_join_info(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			   struct rtw89_sta_link *rtwsta_link, bool dis_conn)
{
	u8 mac_id = rtwsta_link ? rtwsta_link->mac_id : rtwvif_link->mac_id;
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	bool is_mld = ieee80211_vif_is_mld(vif);
	u8 self_role = rtwvif_link->self_role;
	enum rtw89_fw_sta_type sta_type;
	u8 net_type = rtwvif_link->net_type;
	struct rtw89_h2c_join_v1 *h2c_v1;
	struct rtw89_h2c_join *h2c;
	u32 len = sizeof(*h2c);
	bool format_v1 = false;
	struct sk_buff *skb;
	u8 main_mac_id;
	int ret;

	if (rtwdev->chip->chip_gen == RTW89_CHIP_BE) {
		len = sizeof(*h2c_v1);
		format_v1 = true;
	}

	if (net_type == RTW89_NET_TYPE_AP_MODE && rtwsta_link) {
		self_role = RTW89_SELF_ROLE_AP_CLIENT;
		net_type = dis_conn ? RTW89_NET_TYPE_NO_LINK : net_type;
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c join\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_join *)skb->data;

	h2c->w0 = le32_encode_bits(mac_id, RTW89_H2C_JOININFO_W0_MACID) |
		  le32_encode_bits(dis_conn, RTW89_H2C_JOININFO_W0_OP) |
		  le32_encode_bits(rtwvif_link->mac_idx, RTW89_H2C_JOININFO_W0_BAND) |
		  le32_encode_bits(rtwvif_link->wmm, RTW89_H2C_JOININFO_W0_WMM) |
		  le32_encode_bits(rtwvif_link->trigger, RTW89_H2C_JOININFO_W0_TGR) |
		  le32_encode_bits(0, RTW89_H2C_JOININFO_W0_ISHESTA) |
		  le32_encode_bits(0, RTW89_H2C_JOININFO_W0_DLBW) |
		  le32_encode_bits(0, RTW89_H2C_JOININFO_W0_TF_MAC_PAD) |
		  le32_encode_bits(0, RTW89_H2C_JOININFO_W0_DL_T_PE) |
		  le32_encode_bits(rtwvif_link->port, RTW89_H2C_JOININFO_W0_PORT_ID) |
		  le32_encode_bits(net_type, RTW89_H2C_JOININFO_W0_NET_TYPE) |
		  le32_encode_bits(rtwvif_link->wifi_role,
				   RTW89_H2C_JOININFO_W0_WIFI_ROLE) |
		  le32_encode_bits(self_role, RTW89_H2C_JOININFO_W0_SELF_ROLE);

	if (!format_v1)
		goto done;

	h2c_v1 = (struct rtw89_h2c_join_v1 *)skb->data;

	sta_type = rtw89_fw_get_sta_type(rtwdev, rtwvif_link, rtwsta_link);

	if (rtwsta_link)
		main_mac_id = rtw89_sta_get_main_macid(rtwsta_link->rtwsta);
	else
		main_mac_id = rtw89_vif_get_main_macid(rtwvif_link->rtwvif);

	h2c_v1->w1 = le32_encode_bits(sta_type, RTW89_H2C_JOININFO_W1_STA_TYPE) |
		     le32_encode_bits(is_mld, RTW89_H2C_JOININFO_W1_IS_MLD) |
		     le32_encode_bits(main_mac_id, RTW89_H2C_JOININFO_W1_MAIN_MACID) |
		     le32_encode_bits(RTW89_H2C_JOININFO_MLO_MODE_MLSR,
				      RTW89_H2C_JOININFO_W1_MLO_MODE) |
		     le32_encode_bits(0, RTW89_H2C_JOININFO_W1_EMLSR_CAB) |
		     le32_encode_bits(0, RTW89_H2C_JOININFO_W1_NSTR_EN) |
		     le32_encode_bits(0, RTW89_H2C_JOININFO_W1_INIT_PWR_STATE) |
		     le32_encode_bits(IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_256US,
				      RTW89_H2C_JOININFO_W1_EMLSR_PADDING) |
		     le32_encode_bits(IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_256US,
				      RTW89_H2C_JOININFO_W1_EMLSR_TRANS_DELAY) |
		     le32_encode_bits(0, RTW89_H2C_JOININFO_W2_MACID_EXT) |
		     le32_encode_bits(0, RTW89_H2C_JOININFO_W2_MAIN_MACID_EXT);

	h2c_v1->w2 = 0;

done:
	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_MEDIA_RPT,
			      H2C_FUNC_MAC_JOININFO, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_notify_dbcc(struct rtw89_dev *rtwdev, bool en)
{
	struct rtw89_h2c_notify_dbcc *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c notify dbcc\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_notify_dbcc *)skb->data;

	h2c->w0 = le32_encode_bits(en, RTW89_H2C_NOTIFY_DBCC_EN);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_MEDIA_RPT,
			      H2C_FUNC_NOTIFY_DBCC, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_macid_pause(struct rtw89_dev *rtwdev, u8 sh, u8 grp,
			     bool pause)
{
	struct rtw89_fw_macid_pause_sleep_grp *h2c_new;
	struct rtw89_fw_macid_pause_grp *h2c;
	__le32 set = cpu_to_le32(BIT(sh));
	u8 h2c_macid_pause_id;
	struct sk_buff *skb;
	u32 len;
	int ret;

	if (RTW89_CHK_FW_FEATURE(MACID_PAUSE_SLEEP, &rtwdev->fw)) {
		h2c_macid_pause_id = H2C_FUNC_MAC_MACID_PAUSE_SLEEP;
		len = sizeof(*h2c_new);
	} else {
		h2c_macid_pause_id = H2C_FUNC_MAC_MACID_PAUSE;
		len = sizeof(*h2c);
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c macid pause\n");
		return -ENOMEM;
	}
	skb_put(skb, len);

	if (h2c_macid_pause_id == H2C_FUNC_MAC_MACID_PAUSE_SLEEP) {
		h2c_new = (struct rtw89_fw_macid_pause_sleep_grp *)skb->data;

		h2c_new->n[0].pause_mask_grp[grp] = set;
		h2c_new->n[0].sleep_mask_grp[grp] = set;
		if (pause) {
			h2c_new->n[0].pause_grp[grp] = set;
			h2c_new->n[0].sleep_grp[grp] = set;
		}
	} else {
		h2c = (struct rtw89_fw_macid_pause_grp *)skb->data;

		h2c->mask_grp[grp] = set;
		if (pause)
			h2c->pause_grp[grp] = set;
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      h2c_macid_pause_id, 1, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_EDCA_LEN 12
int rtw89_fw_h2c_set_edca(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			  u8 ac, u32 val)
{
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_EDCA_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c edca\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_EDCA_LEN);
	RTW89_SET_EDCA_SEL(skb->data, 0);
	RTW89_SET_EDCA_BAND(skb->data, rtwvif_link->mac_idx);
	RTW89_SET_EDCA_WMM(skb->data, 0);
	RTW89_SET_EDCA_AC(skb->data, ac);
	RTW89_SET_EDCA_PARAM(skb->data, val);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_USR_EDCA, 0, 1,
			      H2C_EDCA_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_TSF32_TOGL_LEN 4
int rtw89_fw_h2c_tsf32_toggle(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link,
			      bool en)
{
	struct sk_buff *skb;
	u16 early_us = en ? 2000 : 0;
	u8 *cmd;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_TSF32_TOGL_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c p2p act\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_TSF32_TOGL_LEN);
	cmd = skb->data;

	RTW89_SET_FWCMD_TSF32_TOGL_BAND(cmd, rtwvif_link->mac_idx);
	RTW89_SET_FWCMD_TSF32_TOGL_EN(cmd, en);
	RTW89_SET_FWCMD_TSF32_TOGL_PORT(cmd, rtwvif_link->port);
	RTW89_SET_FWCMD_TSF32_TOGL_EARLY(cmd, early_us);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_TSF32_TOGL, 0, 0,
			      H2C_TSF32_TOGL_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_OFLD_CFG_LEN 8
int rtw89_fw_h2c_set_ofld_cfg(struct rtw89_dev *rtwdev)
{
	static const u8 cfg[] = {0x09, 0x00, 0x00, 0x00, 0x5e, 0x00, 0x00, 0x00};
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_OFLD_CFG_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c ofld\n");
		return -ENOMEM;
	}
	skb_put_data(skb, cfg, H2C_OFLD_CFG_LEN);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_OFLD_CFG, 0, 1,
			      H2C_OFLD_CFG_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_tx_duty(struct rtw89_dev *rtwdev, u8 lv)
{
	struct rtw89_h2c_tx_duty *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	u16 pause, active;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c tx duty\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_tx_duty *)skb->data;

	static_assert(RTW89_THERMAL_PROT_LV_MAX * RTW89_THERMAL_PROT_STEP < 100);

	if (lv == 0 || lv > RTW89_THERMAL_PROT_LV_MAX) {
		h2c->w1 = le32_encode_bits(1, RTW89_H2C_TX_DUTY_W1_STOP);
	} else {
		active = 100 - lv * RTW89_THERMAL_PROT_STEP;
		pause = 100 - active;

		h2c->w0 = le32_encode_bits(pause, RTW89_H2C_TX_DUTY_W0_PAUSE_INTVL_MASK) |
			  le32_encode_bits(active, RTW89_H2C_TX_DUTY_W0_TX_INTVL_MASK);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_TX_DUTY, 0, 0, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_set_bcn_fltr_cfg(struct rtw89_dev *rtwdev,
				  struct rtw89_vif_link *rtwvif_link,
				  bool connect)
{
	struct ieee80211_bss_conf *bss_conf;
	s32 thold = RTW89_DEFAULT_CQM_THOLD;
	u32 hyst = RTW89_DEFAULT_CQM_HYST;
	struct rtw89_h2c_bcnfltr *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	u8 max_cnt, cnt;
	int ret;

	if (!RTW89_CHK_FW_FEATURE(BEACON_FILTER, &rtwdev->fw))
		return -EINVAL;

	if (!rtwvif_link || rtwvif_link->net_type != RTW89_NET_TYPE_INFRA)
		return -EINVAL;

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, false);

	if (bss_conf->cqm_rssi_hyst)
		hyst = bss_conf->cqm_rssi_hyst;
	if (bss_conf->cqm_rssi_thold)
		thold = bss_conf->cqm_rssi_thold;

	rcu_read_unlock();

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c bcn filter\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_bcnfltr *)skb->data;

	if (RTW89_CHK_FW_FEATURE(BEACON_LOSS_COUNT_V1, &rtwdev->fw))
		max_cnt = BIT(7) - 1;
	else
		max_cnt = BIT(4) - 1;

	cnt = min(RTW89_BCN_LOSS_CNT, max_cnt);

	h2c->w0 = le32_encode_bits(connect, RTW89_H2C_BCNFLTR_W0_MON_RSSI) |
		  le32_encode_bits(connect, RTW89_H2C_BCNFLTR_W0_MON_BCN) |
		  le32_encode_bits(connect, RTW89_H2C_BCNFLTR_W0_MON_EN) |
		  le32_encode_bits(RTW89_BCN_FLTR_OFFLOAD_MODE_DEFAULT,
				   RTW89_H2C_BCNFLTR_W0_MODE) |
		  le32_encode_bits(cnt >> 4, RTW89_H2C_BCNFLTR_W0_BCN_LOSS_CNT_H3) |
		  le32_encode_bits(cnt & 0xf, RTW89_H2C_BCNFLTR_W0_BCN_LOSS_CNT_L4) |
		  le32_encode_bits(hyst, RTW89_H2C_BCNFLTR_W0_RSSI_HYST) |
		  le32_encode_bits(thold + MAX_RSSI,
				   RTW89_H2C_BCNFLTR_W0_RSSI_THRESHOLD) |
		  le32_encode_bits(rtwvif_link->mac_id, RTW89_H2C_BCNFLTR_W0_MAC_ID);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_CFG_BCNFLTR, 0, 1, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_rssi_offload(struct rtw89_dev *rtwdev,
			      struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	struct rtw89_h2c_ofld_rssi *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	s8 rssi;
	int ret;

	if (!RTW89_CHK_FW_FEATURE(BEACON_FILTER, &rtwdev->fw))
		return -EINVAL;

	if (!phy_ppdu)
		return -EINVAL;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c rssi\n");
		return -ENOMEM;
	}

	rssi = phy_ppdu->rssi_avg >> RSSI_FACTOR;
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_ofld_rssi *)skb->data;

	h2c->w0 = le32_encode_bits(phy_ppdu->mac_id, RTW89_H2C_OFLD_RSSI_W0_MACID) |
		  le32_encode_bits(1, RTW89_H2C_OFLD_RSSI_W0_NUM);
	h2c->w1 = le32_encode_bits(rssi, RTW89_H2C_OFLD_RSSI_W1_VAL);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_OFLD_RSSI, 0, 1, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_tp_offload(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct rtw89_traffic_stats *stats = &rtwvif->stats;
	struct rtw89_h2c_ofld *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	if (rtwvif_link->net_type != RTW89_NET_TYPE_INFRA)
		return -EINVAL;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c tp\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_ofld *)skb->data;

	h2c->w0 = le32_encode_bits(rtwvif_link->mac_id, RTW89_H2C_OFLD_W0_MAC_ID) |
		  le32_encode_bits(stats->tx_throughput, RTW89_H2C_OFLD_W0_TX_TP) |
		  le32_encode_bits(stats->rx_throughput, RTW89_H2C_OFLD_W0_RX_TP);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_OFLD_TP, 0, 1, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_ra(struct rtw89_dev *rtwdev, struct rtw89_ra_info *ra, bool csi)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_h2c_ra_v1 *h2c_v1;
	struct rtw89_h2c_ra *h2c;
	u32 len = sizeof(*h2c);
	bool format_v1 = false;
	struct sk_buff *skb;
	int ret;

	if (chip->chip_gen == RTW89_CHIP_BE) {
		len = sizeof(*h2c_v1);
		format_v1 = true;
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c join\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_ra *)skb->data;
	rtw89_debug(rtwdev, RTW89_DBG_RA,
		    "ra cmd msk: %llx ", ra->ra_mask);

	h2c->w0 = le32_encode_bits(ra->mode_ctrl, RTW89_H2C_RA_W0_MODE) |
		  le32_encode_bits(ra->bw_cap, RTW89_H2C_RA_W0_BW_CAP) |
		  le32_encode_bits(ra->macid, RTW89_H2C_RA_W0_MACID) |
		  le32_encode_bits(ra->dcm_cap, RTW89_H2C_RA_W0_DCM) |
		  le32_encode_bits(ra->er_cap, RTW89_H2C_RA_W0_ER) |
		  le32_encode_bits(ra->init_rate_lv, RTW89_H2C_RA_W0_INIT_RATE_LV) |
		  le32_encode_bits(ra->upd_all, RTW89_H2C_RA_W0_UPD_ALL) |
		  le32_encode_bits(ra->en_sgi, RTW89_H2C_RA_W0_SGI) |
		  le32_encode_bits(ra->ldpc_cap, RTW89_H2C_RA_W0_LDPC) |
		  le32_encode_bits(ra->stbc_cap, RTW89_H2C_RA_W0_STBC) |
		  le32_encode_bits(ra->ss_num, RTW89_H2C_RA_W0_SS_NUM) |
		  le32_encode_bits(ra->giltf, RTW89_H2C_RA_W0_GILTF) |
		  le32_encode_bits(ra->upd_bw_nss_mask, RTW89_H2C_RA_W0_UPD_BW_NSS_MASK) |
		  le32_encode_bits(ra->upd_mask, RTW89_H2C_RA_W0_UPD_MASK);
	h2c->w1 = le32_encode_bits(ra->ra_mask, RTW89_H2C_RA_W1_RAMASK_LO32);
	h2c->w2 = le32_encode_bits(ra->ra_mask >> 32, RTW89_H2C_RA_W2_RAMASK_HI32);
	h2c->w3 = le32_encode_bits(ra->fix_giltf_en, RTW89_H2C_RA_W3_FIX_GILTF_EN) |
		  le32_encode_bits(ra->fix_giltf, RTW89_H2C_RA_W3_FIX_GILTF);

	if (!format_v1)
		goto csi;

	h2c_v1 = (struct rtw89_h2c_ra_v1 *)h2c;
	h2c_v1->w4 = le32_encode_bits(ra->mode_ctrl, RTW89_H2C_RA_V1_W4_MODE_EHT) |
		     le32_encode_bits(ra->bw_cap, RTW89_H2C_RA_V1_W4_BW_EHT);

csi:
	if (!csi)
		goto done;

	h2c->w2 |= le32_encode_bits(1, RTW89_H2C_RA_W2_BFEE_CSI_CTL);
	h2c->w3 |= le32_encode_bits(ra->band_num, RTW89_H2C_RA_W3_BAND_NUM) |
		   le32_encode_bits(ra->cr_tbl_sel, RTW89_H2C_RA_W3_CR_TBL_SEL) |
		   le32_encode_bits(ra->fixed_csi_rate_en, RTW89_H2C_RA_W3_FIXED_CSI_RATE_EN) |
		   le32_encode_bits(ra->ra_csi_rate_en, RTW89_H2C_RA_W3_RA_CSI_RATE_EN) |
		   le32_encode_bits(ra->csi_mcs_ss_idx, RTW89_H2C_RA_W3_FIXED_CSI_MCS_SS_IDX) |
		   le32_encode_bits(ra->csi_mode, RTW89_H2C_RA_W3_FIXED_CSI_MODE) |
		   le32_encode_bits(ra->csi_gi_ltf, RTW89_H2C_RA_W3_FIXED_CSI_GI_LTF) |
		   le32_encode_bits(ra->csi_bw, RTW89_H2C_RA_W3_FIXED_CSI_BW);

done:
	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, H2C_CL_OUTSRC_RA,
			      H2C_FUNC_OUTSRC_RA_MACIDCFG, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_cxdrv_init(struct rtw89_dev *rtwdev, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_init_info *init_info = &dm->init_info.init;
	struct rtw89_btc_module *module = &init_info->module;
	struct rtw89_btc_ant_info *ant = &module->ant;
	struct rtw89_h2c_cxinit *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_init\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_cxinit *)skb->data;

	h2c->hdr.type = type;
	h2c->hdr.len = len - H2C_LEN_CXDRVHDR;

	h2c->ant_type = ant->type;
	h2c->ant_num = ant->num;
	h2c->ant_iso = ant->isolation;
	h2c->ant_info =
		u8_encode_bits(ant->single_pos, RTW89_H2C_CXINIT_ANT_INFO_POS) |
		u8_encode_bits(ant->diversity, RTW89_H2C_CXINIT_ANT_INFO_DIVERSITY) |
		u8_encode_bits(ant->btg_pos, RTW89_H2C_CXINIT_ANT_INFO_BTG_POS) |
		u8_encode_bits(ant->stream_cnt, RTW89_H2C_CXINIT_ANT_INFO_STREAM_CNT);

	h2c->mod_rfe = module->rfe_type;
	h2c->mod_cv = module->cv;
	h2c->mod_info =
		u8_encode_bits(module->bt_solo, RTW89_H2C_CXINIT_MOD_INFO_BT_SOLO) |
		u8_encode_bits(module->bt_pos, RTW89_H2C_CXINIT_MOD_INFO_BT_POS) |
		u8_encode_bits(module->switch_type, RTW89_H2C_CXINIT_MOD_INFO_SW_TYPE) |
		u8_encode_bits(module->wa_type, RTW89_H2C_CXINIT_MOD_INFO_WA_TYPE);
	h2c->mod_adie_kt = module->kt_ver_adie;
	h2c->wl_gch = init_info->wl_guard_ch;

	h2c->info =
		u8_encode_bits(init_info->wl_only, RTW89_H2C_CXINIT_INFO_WL_ONLY) |
		u8_encode_bits(init_info->wl_init_ok, RTW89_H2C_CXINIT_INFO_WL_INITOK) |
		u8_encode_bits(init_info->dbcc_en, RTW89_H2C_CXINIT_INFO_DBCC_EN) |
		u8_encode_bits(init_info->cx_other, RTW89_H2C_CXINIT_INFO_CX_OTHER) |
		u8_encode_bits(init_info->bt_only, RTW89_H2C_CXINIT_INFO_BT_ONLY);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_cxdrv_init_v7(struct rtw89_dev *rtwdev, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_init_info_v7 *init_info = &dm->init_info.init_v7;
	struct rtw89_h2c_cxinit_v7 *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_init_v7\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_cxinit_v7 *)skb->data;

	h2c->hdr.type = type;
	h2c->hdr.ver = btc->ver->fcxinit;
	h2c->hdr.len = len - H2C_LEN_CXDRVHDR_V7;
	h2c->init = *init_info;

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define PORT_DATA_OFFSET 4
#define H2C_LEN_CXDRVINFO_ROLE_DBCC_LEN 12
#define H2C_LEN_CXDRVINFO_ROLE_SIZE(max_role_num) \
	(4 + 12 * (max_role_num) + H2C_LEN_CXDRVHDR)

int rtw89_fw_h2c_cxdrv_role(struct rtw89_dev *rtwdev, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	const struct rtw89_btc_ver *ver = btc->ver;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_role_info *role_info = &wl->role_info;
	struct rtw89_btc_wl_role_info_bpos *bpos = &role_info->role_map.role;
	struct rtw89_btc_wl_active_role *active = role_info->active_role;
	struct sk_buff *skb;
	u32 len;
	u8 offset = 0;
	u8 *cmd;
	int ret;
	int i;

	len = H2C_LEN_CXDRVINFO_ROLE_SIZE(ver->max_role_num);

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_role\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	cmd = skb->data;

	RTW89_SET_FWCMD_CXHDR_TYPE(cmd, type);
	RTW89_SET_FWCMD_CXHDR_LEN(cmd, len - H2C_LEN_CXDRVHDR);

	RTW89_SET_FWCMD_CXROLE_CONNECT_CNT(cmd, role_info->connect_cnt);
	RTW89_SET_FWCMD_CXROLE_LINK_MODE(cmd, role_info->link_mode);

	RTW89_SET_FWCMD_CXROLE_ROLE_NONE(cmd, bpos->none);
	RTW89_SET_FWCMD_CXROLE_ROLE_STA(cmd, bpos->station);
	RTW89_SET_FWCMD_CXROLE_ROLE_AP(cmd, bpos->ap);
	RTW89_SET_FWCMD_CXROLE_ROLE_VAP(cmd, bpos->vap);
	RTW89_SET_FWCMD_CXROLE_ROLE_ADHOC(cmd, bpos->adhoc);
	RTW89_SET_FWCMD_CXROLE_ROLE_ADHOC_MASTER(cmd, bpos->adhoc_master);
	RTW89_SET_FWCMD_CXROLE_ROLE_MESH(cmd, bpos->mesh);
	RTW89_SET_FWCMD_CXROLE_ROLE_MONITOR(cmd, bpos->moniter);
	RTW89_SET_FWCMD_CXROLE_ROLE_P2P_DEV(cmd, bpos->p2p_device);
	RTW89_SET_FWCMD_CXROLE_ROLE_P2P_GC(cmd, bpos->p2p_gc);
	RTW89_SET_FWCMD_CXROLE_ROLE_P2P_GO(cmd, bpos->p2p_go);
	RTW89_SET_FWCMD_CXROLE_ROLE_NAN(cmd, bpos->nan);

	for (i = 0; i < RTW89_PORT_NUM; i++, active++) {
		RTW89_SET_FWCMD_CXROLE_ACT_CONNECTED(cmd, active->connected, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_PID(cmd, active->pid, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_PHY(cmd, active->phy, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_NOA(cmd, active->noa, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_BAND(cmd, active->band, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_CLIENT_PS(cmd, active->client_ps, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_BW(cmd, active->bw, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_ROLE(cmd, active->role, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_CH(cmd, active->ch, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_TX_LVL(cmd, active->tx_lvl, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_RX_LVL(cmd, active->rx_lvl, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_TX_RATE(cmd, active->tx_rate, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_RX_RATE(cmd, active->rx_rate, i, offset);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_LEN_CXDRVINFO_ROLE_SIZE_V1(max_role_num) \
	(4 + 16 * (max_role_num) + H2C_LEN_CXDRVINFO_ROLE_DBCC_LEN + H2C_LEN_CXDRVHDR)

int rtw89_fw_h2c_cxdrv_role_v1(struct rtw89_dev *rtwdev, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	const struct rtw89_btc_ver *ver = btc->ver;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_role_info_v1 *role_info = &wl->role_info_v1;
	struct rtw89_btc_wl_role_info_bpos *bpos = &role_info->role_map.role;
	struct rtw89_btc_wl_active_role_v1 *active = role_info->active_role_v1;
	struct sk_buff *skb;
	u32 len;
	u8 *cmd, offset;
	int ret;
	int i;

	len = H2C_LEN_CXDRVINFO_ROLE_SIZE_V1(ver->max_role_num);

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_role\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	cmd = skb->data;

	RTW89_SET_FWCMD_CXHDR_TYPE(cmd, type);
	RTW89_SET_FWCMD_CXHDR_LEN(cmd, len - H2C_LEN_CXDRVHDR);

	RTW89_SET_FWCMD_CXROLE_CONNECT_CNT(cmd, role_info->connect_cnt);
	RTW89_SET_FWCMD_CXROLE_LINK_MODE(cmd, role_info->link_mode);

	RTW89_SET_FWCMD_CXROLE_ROLE_NONE(cmd, bpos->none);
	RTW89_SET_FWCMD_CXROLE_ROLE_STA(cmd, bpos->station);
	RTW89_SET_FWCMD_CXROLE_ROLE_AP(cmd, bpos->ap);
	RTW89_SET_FWCMD_CXROLE_ROLE_VAP(cmd, bpos->vap);
	RTW89_SET_FWCMD_CXROLE_ROLE_ADHOC(cmd, bpos->adhoc);
	RTW89_SET_FWCMD_CXROLE_ROLE_ADHOC_MASTER(cmd, bpos->adhoc_master);
	RTW89_SET_FWCMD_CXROLE_ROLE_MESH(cmd, bpos->mesh);
	RTW89_SET_FWCMD_CXROLE_ROLE_MONITOR(cmd, bpos->moniter);
	RTW89_SET_FWCMD_CXROLE_ROLE_P2P_DEV(cmd, bpos->p2p_device);
	RTW89_SET_FWCMD_CXROLE_ROLE_P2P_GC(cmd, bpos->p2p_gc);
	RTW89_SET_FWCMD_CXROLE_ROLE_P2P_GO(cmd, bpos->p2p_go);
	RTW89_SET_FWCMD_CXROLE_ROLE_NAN(cmd, bpos->nan);

	offset = PORT_DATA_OFFSET;
	for (i = 0; i < RTW89_PORT_NUM; i++, active++) {
		RTW89_SET_FWCMD_CXROLE_ACT_CONNECTED(cmd, active->connected, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_PID(cmd, active->pid, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_PHY(cmd, active->phy, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_NOA(cmd, active->noa, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_BAND(cmd, active->band, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_CLIENT_PS(cmd, active->client_ps, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_BW(cmd, active->bw, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_ROLE(cmd, active->role, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_CH(cmd, active->ch, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_TX_LVL(cmd, active->tx_lvl, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_RX_LVL(cmd, active->rx_lvl, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_TX_RATE(cmd, active->tx_rate, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_RX_RATE(cmd, active->rx_rate, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_NOA_DUR(cmd, active->noa_duration, i, offset);
	}

	offset = len - H2C_LEN_CXDRVINFO_ROLE_DBCC_LEN;
	RTW89_SET_FWCMD_CXROLE_MROLE_TYPE(cmd, role_info->mrole_type, offset);
	RTW89_SET_FWCMD_CXROLE_MROLE_NOA(cmd, role_info->mrole_noa_duration, offset);
	RTW89_SET_FWCMD_CXROLE_DBCC_EN(cmd, role_info->dbcc_en, offset);
	RTW89_SET_FWCMD_CXROLE_DBCC_CHG(cmd, role_info->dbcc_chg, offset);
	RTW89_SET_FWCMD_CXROLE_DBCC_2G_PHY(cmd, role_info->dbcc_2g_phy, offset);
	RTW89_SET_FWCMD_CXROLE_LINK_MODE_CHG(cmd, role_info->link_mode_chg, offset);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_LEN_CXDRVINFO_ROLE_SIZE_V2(max_role_num) \
	(4 + 8 * (max_role_num) + H2C_LEN_CXDRVINFO_ROLE_DBCC_LEN + H2C_LEN_CXDRVHDR)

int rtw89_fw_h2c_cxdrv_role_v2(struct rtw89_dev *rtwdev, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	const struct rtw89_btc_ver *ver = btc->ver;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_role_info_v2 *role_info = &wl->role_info_v2;
	struct rtw89_btc_wl_role_info_bpos *bpos = &role_info->role_map.role;
	struct rtw89_btc_wl_active_role_v2 *active = role_info->active_role_v2;
	struct sk_buff *skb;
	u32 len;
	u8 *cmd, offset;
	int ret;
	int i;

	len = H2C_LEN_CXDRVINFO_ROLE_SIZE_V2(ver->max_role_num);

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_role\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	cmd = skb->data;

	RTW89_SET_FWCMD_CXHDR_TYPE(cmd, type);
	RTW89_SET_FWCMD_CXHDR_LEN(cmd, len - H2C_LEN_CXDRVHDR);

	RTW89_SET_FWCMD_CXROLE_CONNECT_CNT(cmd, role_info->connect_cnt);
	RTW89_SET_FWCMD_CXROLE_LINK_MODE(cmd, role_info->link_mode);

	RTW89_SET_FWCMD_CXROLE_ROLE_NONE(cmd, bpos->none);
	RTW89_SET_FWCMD_CXROLE_ROLE_STA(cmd, bpos->station);
	RTW89_SET_FWCMD_CXROLE_ROLE_AP(cmd, bpos->ap);
	RTW89_SET_FWCMD_CXROLE_ROLE_VAP(cmd, bpos->vap);
	RTW89_SET_FWCMD_CXROLE_ROLE_ADHOC(cmd, bpos->adhoc);
	RTW89_SET_FWCMD_CXROLE_ROLE_ADHOC_MASTER(cmd, bpos->adhoc_master);
	RTW89_SET_FWCMD_CXROLE_ROLE_MESH(cmd, bpos->mesh);
	RTW89_SET_FWCMD_CXROLE_ROLE_MONITOR(cmd, bpos->moniter);
	RTW89_SET_FWCMD_CXROLE_ROLE_P2P_DEV(cmd, bpos->p2p_device);
	RTW89_SET_FWCMD_CXROLE_ROLE_P2P_GC(cmd, bpos->p2p_gc);
	RTW89_SET_FWCMD_CXROLE_ROLE_P2P_GO(cmd, bpos->p2p_go);
	RTW89_SET_FWCMD_CXROLE_ROLE_NAN(cmd, bpos->nan);

	offset = PORT_DATA_OFFSET;
	for (i = 0; i < RTW89_PORT_NUM; i++, active++) {
		RTW89_SET_FWCMD_CXROLE_ACT_CONNECTED_V2(cmd, active->connected, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_PID_V2(cmd, active->pid, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_PHY_V2(cmd, active->phy, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_NOA_V2(cmd, active->noa, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_BAND_V2(cmd, active->band, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_CLIENT_PS_V2(cmd, active->client_ps, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_BW_V2(cmd, active->bw, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_ROLE_V2(cmd, active->role, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_CH_V2(cmd, active->ch, i, offset);
		RTW89_SET_FWCMD_CXROLE_ACT_NOA_DUR_V2(cmd, active->noa_duration, i, offset);
	}

	offset = len - H2C_LEN_CXDRVINFO_ROLE_DBCC_LEN;
	RTW89_SET_FWCMD_CXROLE_MROLE_TYPE(cmd, role_info->mrole_type, offset);
	RTW89_SET_FWCMD_CXROLE_MROLE_NOA(cmd, role_info->mrole_noa_duration, offset);
	RTW89_SET_FWCMD_CXROLE_DBCC_EN(cmd, role_info->dbcc_en, offset);
	RTW89_SET_FWCMD_CXROLE_DBCC_CHG(cmd, role_info->dbcc_chg, offset);
	RTW89_SET_FWCMD_CXROLE_DBCC_2G_PHY(cmd, role_info->dbcc_2g_phy, offset);
	RTW89_SET_FWCMD_CXROLE_LINK_MODE_CHG(cmd, role_info->link_mode_chg, offset);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_cxdrv_role_v7(struct rtw89_dev *rtwdev, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_role_info_v7 *role = &btc->cx.wl.role_info_v7;
	struct rtw89_h2c_cxrole_v7 *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_ctrl\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_cxrole_v7 *)skb->data;

	h2c->hdr.type = type;
	h2c->hdr.ver = btc->ver->fwlrole;
	h2c->hdr.len = len - H2C_LEN_CXDRVHDR_V7;
	memcpy(&h2c->_u8, role, sizeof(h2c->_u8));
	h2c->_u32.role_map = cpu_to_le32(role->role_map);
	h2c->_u32.mrole_type = cpu_to_le32(role->mrole_type);
	h2c->_u32.mrole_noa_duration = cpu_to_le32(role->mrole_noa_duration);
	h2c->_u32.dbcc_en = cpu_to_le32(role->dbcc_en);
	h2c->_u32.dbcc_chg = cpu_to_le32(role->dbcc_chg);
	h2c->_u32.dbcc_2g_phy = cpu_to_le32(role->dbcc_2g_phy);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_cxdrv_role_v8(struct rtw89_dev *rtwdev, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_role_info_v8 *role = &btc->cx.wl.role_info_v8;
	struct rtw89_h2c_cxrole_v8 *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_ctrl\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_cxrole_v8 *)skb->data;

	h2c->hdr.type = type;
	h2c->hdr.ver = btc->ver->fwlrole;
	h2c->hdr.len = len - H2C_LEN_CXDRVHDR_V7;
	memcpy(&h2c->_u8, role, sizeof(h2c->_u8));
	h2c->_u32.role_map = cpu_to_le32(role->role_map);
	h2c->_u32.mrole_type = cpu_to_le32(role->mrole_type);
	h2c->_u32.mrole_noa_duration = cpu_to_le32(role->mrole_noa_duration);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_LEN_CXDRVINFO_CTRL (4 + H2C_LEN_CXDRVHDR)
int rtw89_fw_h2c_cxdrv_ctrl(struct rtw89_dev *rtwdev, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	const struct rtw89_btc_ver *ver = btc->ver;
	struct rtw89_btc_ctrl *ctrl = &btc->ctrl.ctrl;
	struct sk_buff *skb;
	u8 *cmd;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_LEN_CXDRVINFO_CTRL);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_ctrl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_LEN_CXDRVINFO_CTRL);
	cmd = skb->data;

	RTW89_SET_FWCMD_CXHDR_TYPE(cmd, type);
	RTW89_SET_FWCMD_CXHDR_LEN(cmd, H2C_LEN_CXDRVINFO_CTRL - H2C_LEN_CXDRVHDR);

	RTW89_SET_FWCMD_CXCTRL_MANUAL(cmd, ctrl->manual);
	RTW89_SET_FWCMD_CXCTRL_IGNORE_BT(cmd, ctrl->igno_bt);
	RTW89_SET_FWCMD_CXCTRL_ALWAYS_FREERUN(cmd, ctrl->always_freerun);
	if (ver->fcxctrl == 0)
		RTW89_SET_FWCMD_CXCTRL_TRACE_STEP(cmd, ctrl->trace_step);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      H2C_LEN_CXDRVINFO_CTRL);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_cxdrv_ctrl_v7(struct rtw89_dev *rtwdev, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_ctrl_v7 *ctrl = &btc->ctrl.ctrl_v7;
	struct rtw89_h2c_cxctrl_v7 *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_ctrl_v7\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_cxctrl_v7 *)skb->data;

	h2c->hdr.type = type;
	h2c->hdr.ver = btc->ver->fcxctrl;
	h2c->hdr.len = sizeof(*h2c) - H2C_LEN_CXDRVHDR_V7;
	h2c->ctrl = *ctrl;

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_LEN_CXDRVINFO_TRX (28 + H2C_LEN_CXDRVHDR)
int rtw89_fw_h2c_cxdrv_trx(struct rtw89_dev *rtwdev, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_trx_info *trx = &btc->dm.trx_info;
	struct sk_buff *skb;
	u8 *cmd;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_LEN_CXDRVINFO_TRX);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_trx\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_LEN_CXDRVINFO_TRX);
	cmd = skb->data;

	RTW89_SET_FWCMD_CXHDR_TYPE(cmd, type);
	RTW89_SET_FWCMD_CXHDR_LEN(cmd, H2C_LEN_CXDRVINFO_TRX - H2C_LEN_CXDRVHDR);

	RTW89_SET_FWCMD_CXTRX_TXLV(cmd, trx->tx_lvl);
	RTW89_SET_FWCMD_CXTRX_RXLV(cmd, trx->rx_lvl);
	RTW89_SET_FWCMD_CXTRX_WLRSSI(cmd, trx->wl_rssi);
	RTW89_SET_FWCMD_CXTRX_BTRSSI(cmd, trx->bt_rssi);
	RTW89_SET_FWCMD_CXTRX_TXPWR(cmd, trx->tx_power);
	RTW89_SET_FWCMD_CXTRX_RXGAIN(cmd, trx->rx_gain);
	RTW89_SET_FWCMD_CXTRX_BTTXPWR(cmd, trx->bt_tx_power);
	RTW89_SET_FWCMD_CXTRX_BTRXGAIN(cmd, trx->bt_rx_gain);
	RTW89_SET_FWCMD_CXTRX_CN(cmd, trx->cn);
	RTW89_SET_FWCMD_CXTRX_NHM(cmd, trx->nhm);
	RTW89_SET_FWCMD_CXTRX_BTPROFILE(cmd, trx->bt_profile);
	RTW89_SET_FWCMD_CXTRX_RSVD2(cmd, trx->rsvd2);
	RTW89_SET_FWCMD_CXTRX_TXRATE(cmd, trx->tx_rate);
	RTW89_SET_FWCMD_CXTRX_RXRATE(cmd, trx->rx_rate);
	RTW89_SET_FWCMD_CXTRX_TXTP(cmd, trx->tx_tp);
	RTW89_SET_FWCMD_CXTRX_RXTP(cmd, trx->rx_tp);
	RTW89_SET_FWCMD_CXTRX_RXERRRA(cmd, trx->rx_err_ratio);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      H2C_LEN_CXDRVINFO_TRX);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_LEN_CXDRVINFO_RFK (4 + H2C_LEN_CXDRVHDR)
int rtw89_fw_h2c_cxdrv_rfk(struct rtw89_dev *rtwdev, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_rfk_info *rfk_info = &wl->rfk_info;
	struct sk_buff *skb;
	u8 *cmd;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_LEN_CXDRVINFO_RFK);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_ctrl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_LEN_CXDRVINFO_RFK);
	cmd = skb->data;

	RTW89_SET_FWCMD_CXHDR_TYPE(cmd, type);
	RTW89_SET_FWCMD_CXHDR_LEN(cmd, H2C_LEN_CXDRVINFO_RFK - H2C_LEN_CXDRVHDR);

	RTW89_SET_FWCMD_CXRFK_STATE(cmd, rfk_info->state);
	RTW89_SET_FWCMD_CXRFK_PATH_MAP(cmd, rfk_info->path_map);
	RTW89_SET_FWCMD_CXRFK_PHY_MAP(cmd, rfk_info->phy_map);
	RTW89_SET_FWCMD_CXRFK_BAND(cmd, rfk_info->band);
	RTW89_SET_FWCMD_CXRFK_TYPE(cmd, rfk_info->type);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      H2C_LEN_CXDRVINFO_RFK);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_LEN_PKT_OFLD 4
int rtw89_fw_h2c_del_pkt_offload(struct rtw89_dev *rtwdev, u8 id)
{
	struct rtw89_wait_info *wait = &rtwdev->mac.fw_ofld_wait;
	struct sk_buff *skb;
	unsigned int cond;
	u8 *cmd;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_LEN_PKT_OFLD);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c pkt offload\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_LEN_PKT_OFLD);
	cmd = skb->data;

	RTW89_SET_FWCMD_PACKET_OFLD_PKT_IDX(cmd, id);
	RTW89_SET_FWCMD_PACKET_OFLD_PKT_OP(cmd, RTW89_PKT_OFLD_OP_DEL);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_PACKET_OFLD, 1, 1,
			      H2C_LEN_PKT_OFLD);

	cond = RTW89_FW_OFLD_WAIT_COND_PKT_OFLD(id, RTW89_PKT_OFLD_OP_DEL);

	ret = rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
	if (ret < 0) {
		rtw89_debug(rtwdev, RTW89_DBG_FW,
			    "failed to del pkt ofld: id %d, ret %d\n",
			    id, ret);
		return ret;
	}

	rtw89_core_release_bit_map(rtwdev->pkt_offload, id);
	return 0;
}

int rtw89_fw_h2c_add_pkt_offload(struct rtw89_dev *rtwdev, u8 *id,
				 struct sk_buff *skb_ofld)
{
	struct rtw89_wait_info *wait = &rtwdev->mac.fw_ofld_wait;
	struct sk_buff *skb;
	unsigned int cond;
	u8 *cmd;
	u8 alloc_id;
	int ret;

	alloc_id = rtw89_core_acquire_bit_map(rtwdev->pkt_offload,
					      RTW89_MAX_PKT_OFLD_NUM);
	if (alloc_id == RTW89_MAX_PKT_OFLD_NUM)
		return -ENOSPC;

	*id = alloc_id;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_LEN_PKT_OFLD + skb_ofld->len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c pkt offload\n");
		rtw89_core_release_bit_map(rtwdev->pkt_offload, alloc_id);
		return -ENOMEM;
	}
	skb_put(skb, H2C_LEN_PKT_OFLD);
	cmd = skb->data;

	RTW89_SET_FWCMD_PACKET_OFLD_PKT_IDX(cmd, alloc_id);
	RTW89_SET_FWCMD_PACKET_OFLD_PKT_OP(cmd, RTW89_PKT_OFLD_OP_ADD);
	RTW89_SET_FWCMD_PACKET_OFLD_PKT_LENGTH(cmd, skb_ofld->len);
	skb_put_data(skb, skb_ofld->data, skb_ofld->len);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_PACKET_OFLD, 1, 1,
			      H2C_LEN_PKT_OFLD + skb_ofld->len);

	cond = RTW89_FW_OFLD_WAIT_COND_PKT_OFLD(alloc_id, RTW89_PKT_OFLD_OP_ADD);

	ret = rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
	if (ret < 0) {
		rtw89_debug(rtwdev, RTW89_DBG_FW,
			    "failed to add pkt ofld: id %d, ret %d\n",
			    alloc_id, ret);
		rtw89_core_release_bit_map(rtwdev->pkt_offload, alloc_id);
		return ret;
	}

	return 0;
}

static
int rtw89_fw_h2c_scan_list_offload_ax(struct rtw89_dev *rtwdev, int ch_num,
				      struct list_head *chan_list)
{
	struct rtw89_wait_info *wait = &rtwdev->mac.fw_ofld_wait;
	struct rtw89_h2c_chinfo_elem *elem;
	struct rtw89_mac_chinfo_ax *ch_info;
	struct rtw89_h2c_chinfo *h2c;
	struct sk_buff *skb;
	unsigned int cond;
	int skb_len;
	int ret;

	static_assert(sizeof(*elem) == RTW89_MAC_CHINFO_SIZE);

	skb_len = struct_size(h2c, elem, ch_num);
	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, skb_len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c scan list\n");
		return -ENOMEM;
	}
	skb_put(skb, sizeof(*h2c));
	h2c = (struct rtw89_h2c_chinfo *)skb->data;

	h2c->ch_num = ch_num;
	h2c->elem_size = sizeof(*elem) / 4; /* in unit of 4 bytes */

	list_for_each_entry(ch_info, chan_list, list) {
		elem = (struct rtw89_h2c_chinfo_elem *)skb_put(skb, sizeof(*elem));

		elem->w0 = le32_encode_bits(ch_info->period, RTW89_H2C_CHINFO_W0_PERIOD) |
			   le32_encode_bits(ch_info->dwell_time, RTW89_H2C_CHINFO_W0_DWELL) |
			   le32_encode_bits(ch_info->central_ch, RTW89_H2C_CHINFO_W0_CENTER_CH) |
			   le32_encode_bits(ch_info->pri_ch, RTW89_H2C_CHINFO_W0_PRI_CH);

		elem->w1 = le32_encode_bits(ch_info->bw, RTW89_H2C_CHINFO_W1_BW) |
			   le32_encode_bits(ch_info->notify_action, RTW89_H2C_CHINFO_W1_ACTION) |
			   le32_encode_bits(ch_info->num_pkt, RTW89_H2C_CHINFO_W1_NUM_PKT) |
			   le32_encode_bits(ch_info->tx_pkt, RTW89_H2C_CHINFO_W1_TX) |
			   le32_encode_bits(ch_info->pause_data, RTW89_H2C_CHINFO_W1_PAUSE_DATA) |
			   le32_encode_bits(ch_info->ch_band, RTW89_H2C_CHINFO_W1_BAND) |
			   le32_encode_bits(ch_info->probe_id, RTW89_H2C_CHINFO_W1_PKT_ID) |
			   le32_encode_bits(ch_info->dfs_ch, RTW89_H2C_CHINFO_W1_DFS) |
			   le32_encode_bits(ch_info->tx_null, RTW89_H2C_CHINFO_W1_TX_NULL) |
			   le32_encode_bits(ch_info->rand_seq_num, RTW89_H2C_CHINFO_W1_RANDOM);

		elem->w2 = le32_encode_bits(ch_info->pkt_id[0], RTW89_H2C_CHINFO_W2_PKT0) |
			   le32_encode_bits(ch_info->pkt_id[1], RTW89_H2C_CHINFO_W2_PKT1) |
			   le32_encode_bits(ch_info->pkt_id[2], RTW89_H2C_CHINFO_W2_PKT2) |
			   le32_encode_bits(ch_info->pkt_id[3], RTW89_H2C_CHINFO_W2_PKT3);

		elem->w3 = le32_encode_bits(ch_info->pkt_id[4], RTW89_H2C_CHINFO_W3_PKT4) |
			   le32_encode_bits(ch_info->pkt_id[5], RTW89_H2C_CHINFO_W3_PKT5) |
			   le32_encode_bits(ch_info->pkt_id[6], RTW89_H2C_CHINFO_W3_PKT6) |
			   le32_encode_bits(ch_info->pkt_id[7], RTW89_H2C_CHINFO_W3_PKT7);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_ADD_SCANOFLD_CH, 1, 1, skb_len);

	cond = RTW89_SCANOFLD_WAIT_COND_ADD_CH;

	ret = rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_FW, "failed to add scan ofld ch\n");
		return ret;
	}

	return 0;
}

static
int rtw89_fw_h2c_scan_list_offload_be(struct rtw89_dev *rtwdev, int ch_num,
				      struct list_head *chan_list,
				      struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_wait_info *wait = &rtwdev->mac.fw_ofld_wait;
	struct rtw89_h2c_chinfo_elem_be *elem;
	struct rtw89_mac_chinfo_be *ch_info;
	struct rtw89_h2c_chinfo_be *h2c;
	struct sk_buff *skb;
	unsigned int cond;
	u8 ver = U8_MAX;
	int skb_len;
	int ret;

	static_assert(sizeof(*elem) == RTW89_MAC_CHINFO_SIZE_BE);

	skb_len = struct_size(h2c, elem, ch_num);
	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, skb_len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c scan list\n");
		return -ENOMEM;
	}

	if (RTW89_CHK_FW_FEATURE(CH_INFO_BE_V0, &rtwdev->fw))
		ver = 0;

	skb_put(skb, sizeof(*h2c));
	h2c = (struct rtw89_h2c_chinfo_be *)skb->data;

	h2c->ch_num = ch_num;
	h2c->elem_size = sizeof(*elem) / 4; /* in unit of 4 bytes */
	h2c->arg = u8_encode_bits(rtwvif_link->mac_idx,
				  RTW89_H2C_CHINFO_ARG_MAC_IDX_MASK);

	list_for_each_entry(ch_info, chan_list, list) {
		elem = (struct rtw89_h2c_chinfo_elem_be *)skb_put(skb, sizeof(*elem));

		elem->w0 = le32_encode_bits(ch_info->dwell_time, RTW89_H2C_CHINFO_BE_W0_DWELL) |
			   le32_encode_bits(ch_info->central_ch,
					    RTW89_H2C_CHINFO_BE_W0_CENTER_CH) |
			   le32_encode_bits(ch_info->pri_ch, RTW89_H2C_CHINFO_BE_W0_PRI_CH);

		elem->w1 = le32_encode_bits(ch_info->bw, RTW89_H2C_CHINFO_BE_W1_BW) |
			   le32_encode_bits(ch_info->ch_band, RTW89_H2C_CHINFO_BE_W1_CH_BAND) |
			   le32_encode_bits(ch_info->dfs_ch, RTW89_H2C_CHINFO_BE_W1_DFS) |
			   le32_encode_bits(ch_info->pause_data,
					    RTW89_H2C_CHINFO_BE_W1_PAUSE_DATA) |
			   le32_encode_bits(ch_info->tx_null, RTW89_H2C_CHINFO_BE_W1_TX_NULL) |
			   le32_encode_bits(ch_info->rand_seq_num,
					    RTW89_H2C_CHINFO_BE_W1_RANDOM) |
			   le32_encode_bits(ch_info->notify_action,
					    RTW89_H2C_CHINFO_BE_W1_NOTIFY) |
			   le32_encode_bits(ch_info->probe_id != 0xff ? 1 : 0,
					    RTW89_H2C_CHINFO_BE_W1_PROBE) |
			   le32_encode_bits(ch_info->leave_crit,
					    RTW89_H2C_CHINFO_BE_W1_EARLY_LEAVE_CRIT) |
			   le32_encode_bits(ch_info->chkpt_timer,
					    RTW89_H2C_CHINFO_BE_W1_CHKPT_TIMER);

		elem->w2 = le32_encode_bits(ch_info->leave_time,
					    RTW89_H2C_CHINFO_BE_W2_EARLY_LEAVE_TIME) |
			   le32_encode_bits(ch_info->leave_th,
					    RTW89_H2C_CHINFO_BE_W2_EARLY_LEAVE_TH) |
			   le32_encode_bits(ch_info->tx_pkt_ctrl,
					    RTW89_H2C_CHINFO_BE_W2_TX_PKT_CTRL);

		elem->w3 = le32_encode_bits(ch_info->pkt_id[0], RTW89_H2C_CHINFO_BE_W3_PKT0) |
			   le32_encode_bits(ch_info->pkt_id[1], RTW89_H2C_CHINFO_BE_W3_PKT1) |
			   le32_encode_bits(ch_info->pkt_id[2], RTW89_H2C_CHINFO_BE_W3_PKT2) |
			   le32_encode_bits(ch_info->pkt_id[3], RTW89_H2C_CHINFO_BE_W3_PKT3);

		elem->w4 = le32_encode_bits(ch_info->pkt_id[4], RTW89_H2C_CHINFO_BE_W4_PKT4) |
			   le32_encode_bits(ch_info->pkt_id[5], RTW89_H2C_CHINFO_BE_W4_PKT5) |
			   le32_encode_bits(ch_info->pkt_id[6], RTW89_H2C_CHINFO_BE_W4_PKT6) |
			   le32_encode_bits(ch_info->pkt_id[7], RTW89_H2C_CHINFO_BE_W4_PKT7);

		elem->w5 = le32_encode_bits(ch_info->sw_def, RTW89_H2C_CHINFO_BE_W5_SW_DEF) |
			   le32_encode_bits(ch_info->fw_probe0_ssids,
					    RTW89_H2C_CHINFO_BE_W5_FW_PROBE0_SSIDS);

		elem->w6 = le32_encode_bits(ch_info->fw_probe0_shortssids,
					    RTW89_H2C_CHINFO_BE_W6_FW_PROBE0_SHORTSSIDS) |
			   le32_encode_bits(ch_info->fw_probe0_bssids,
					    RTW89_H2C_CHINFO_BE_W6_FW_PROBE0_BSSIDS);
		if (ver == 0)
			elem->w0 |=
			   le32_encode_bits(ch_info->period, RTW89_H2C_CHINFO_BE_W0_PERIOD);
		else
			elem->w7 = le32_encode_bits(ch_info->period,
						    RTW89_H2C_CHINFO_BE_W7_PERIOD_V1);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_ADD_SCANOFLD_CH, 1, 1, skb_len);

	cond = RTW89_SCANOFLD_WAIT_COND_ADD_CH;

	ret = rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_FW, "failed to add scan ofld ch\n");
		return ret;
	}

	return 0;
}

#define RTW89_SCAN_DELAY_TSF_UNIT 104800
int rtw89_fw_h2c_scan_offload_ax(struct rtw89_dev *rtwdev,
				 struct rtw89_scan_option *option,
				 struct rtw89_vif_link *rtwvif_link,
				 bool wowlan)
{
	struct rtw89_wait_info *wait = &rtwdev->mac.fw_ofld_wait;
	struct rtw89_chan *op = &rtwdev->scan_info.op_chan;
	enum rtw89_scan_mode scan_mode = RTW89_SCAN_IMMEDIATE;
	struct rtw89_h2c_scanofld *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	unsigned int cond;
	u64 tsf = 0;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c scan offload\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_scanofld *)skb->data;

	if (option->delay) {
		ret = rtw89_mac_port_get_tsf(rtwdev, rtwvif_link, &tsf);
		if (ret) {
			rtw89_warn(rtwdev, "NLO failed to get port tsf: %d\n", ret);
			scan_mode = RTW89_SCAN_IMMEDIATE;
		} else {
			scan_mode = RTW89_SCAN_DELAY;
			tsf += (u64)option->delay * RTW89_SCAN_DELAY_TSF_UNIT;
		}
	}

	h2c->w0 = le32_encode_bits(rtwvif_link->mac_id, RTW89_H2C_SCANOFLD_W0_MACID) |
		  le32_encode_bits(rtwvif_link->port, RTW89_H2C_SCANOFLD_W0_PORT_ID) |
		  le32_encode_bits(rtwvif_link->mac_idx, RTW89_H2C_SCANOFLD_W0_BAND) |
		  le32_encode_bits(option->enable, RTW89_H2C_SCANOFLD_W0_OPERATION);

	h2c->w1 = le32_encode_bits(true, RTW89_H2C_SCANOFLD_W1_NOTIFY_END) |
		  le32_encode_bits(option->target_ch_mode,
				   RTW89_H2C_SCANOFLD_W1_TARGET_CH_MODE) |
		  le32_encode_bits(scan_mode, RTW89_H2C_SCANOFLD_W1_START_MODE) |
		  le32_encode_bits(option->repeat, RTW89_H2C_SCANOFLD_W1_SCAN_TYPE);

	h2c->w2 = le32_encode_bits(option->norm_pd, RTW89_H2C_SCANOFLD_W2_NORM_PD) |
		  le32_encode_bits(option->slow_pd, RTW89_H2C_SCANOFLD_W2_SLOW_PD);

	if (option->target_ch_mode) {
		h2c->w1 |= le32_encode_bits(op->band_width,
					    RTW89_H2C_SCANOFLD_W1_TARGET_CH_BW) |
			   le32_encode_bits(op->primary_channel,
					    RTW89_H2C_SCANOFLD_W1_TARGET_PRI_CH) |
			   le32_encode_bits(op->channel,
					    RTW89_H2C_SCANOFLD_W1_TARGET_CENTRAL_CH);
		h2c->w0 |= le32_encode_bits(op->band_type,
					    RTW89_H2C_SCANOFLD_W0_TARGET_CH_BAND);
	}

	h2c->tsf_high = le32_encode_bits(upper_32_bits(tsf),
					 RTW89_H2C_SCANOFLD_W3_TSF_HIGH);
	h2c->tsf_low = le32_encode_bits(lower_32_bits(tsf),
					RTW89_H2C_SCANOFLD_W4_TSF_LOW);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_SCANOFLD, 1, 1,
			      len);

	if (option->enable)
		cond = RTW89_SCANOFLD_WAIT_COND_START;
	else
		cond = RTW89_SCANOFLD_WAIT_COND_STOP;

	ret = rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_FW, "failed to scan ofld\n");
		return ret;
	}

	return 0;
}

static void rtw89_scan_get_6g_disabled_chan(struct rtw89_dev *rtwdev,
					    struct rtw89_scan_option *option)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	u8 i, idx;

	sband = rtwdev->hw->wiphy->bands[NL80211_BAND_6GHZ];
	if (!sband) {
		option->prohib_chan = U64_MAX;
		return;
	}

	for (i = 0; i < sband->n_channels; i++) {
		chan = &sband->channels[i];
		if (chan->flags & IEEE80211_CHAN_DISABLED) {
			idx = (chan->hw_value - 1) / 4;
			option->prohib_chan |= BIT(idx);
		}
	}
}

int rtw89_fw_h2c_scan_offload_be(struct rtw89_dev *rtwdev,
				 struct rtw89_scan_option *option,
				 struct rtw89_vif_link *rtwvif_link,
				 bool wowlan)
{
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct rtw89_wait_info *wait = &rtwdev->mac.fw_ofld_wait;
	struct cfg80211_scan_request *req = rtwvif->scan_req;
	struct rtw89_h2c_scanofld_be_macc_role *macc_role;
	struct rtw89_chan *op = &scan_info->op_chan;
	struct rtw89_h2c_scanofld_be_opch *opch;
	struct rtw89_pktofld_info *pkt_info;
	struct rtw89_h2c_scanofld_be *h2c;
	struct sk_buff *skb;
	u8 macc_role_size = sizeof(*macc_role) * option->num_macc_role;
	u8 opch_size = sizeof(*opch) * option->num_opch;
	u8 probe_id[NUM_NL80211_BANDS];
	u8 scan_offload_ver = U8_MAX;
	u8 cfg_len = sizeof(*h2c);
	unsigned int cond;
	u8 ver = U8_MAX;
	void *ptr;
	int ret;
	u32 len;
	u8 i;

	rtw89_scan_get_6g_disabled_chan(rtwdev, option);

	if (RTW89_CHK_FW_FEATURE(SCAN_OFFLOAD_BE_V0, &rtwdev->fw)) {
		cfg_len = offsetofend(typeof(*h2c), w8);
		scan_offload_ver = 0;
	}

	len = cfg_len + macc_role_size + opch_size;
	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c scan offload\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_scanofld_be *)skb->data;
	ptr = skb->data;

	memset(probe_id, RTW89_SCANOFLD_PKT_NONE, sizeof(probe_id));

	if (RTW89_CHK_FW_FEATURE(CH_INFO_BE_V0, &rtwdev->fw))
		ver = 0;

	if (!wowlan) {
		list_for_each_entry(pkt_info, &scan_info->pkt_list[NL80211_BAND_6GHZ], list) {
			if (pkt_info->wildcard_6ghz) {
				/* Provide wildcard as template */
				probe_id[NL80211_BAND_6GHZ] = pkt_info->id;
				break;
			}
		}
	}

	h2c->w0 = le32_encode_bits(option->operation, RTW89_H2C_SCANOFLD_BE_W0_OP) |
		  le32_encode_bits(option->scan_mode,
				   RTW89_H2C_SCANOFLD_BE_W0_SCAN_MODE) |
		  le32_encode_bits(option->repeat, RTW89_H2C_SCANOFLD_BE_W0_REPEAT) |
		  le32_encode_bits(true, RTW89_H2C_SCANOFLD_BE_W0_NOTIFY_END) |
		  le32_encode_bits(true, RTW89_H2C_SCANOFLD_BE_W0_LEARN_CH) |
		  le32_encode_bits(rtwvif_link->mac_id, RTW89_H2C_SCANOFLD_BE_W0_MACID) |
		  le32_encode_bits(rtwvif_link->port, RTW89_H2C_SCANOFLD_BE_W0_PORT) |
		  le32_encode_bits(option->band, RTW89_H2C_SCANOFLD_BE_W0_BAND);

	h2c->w1 = le32_encode_bits(option->num_macc_role, RTW89_H2C_SCANOFLD_BE_W1_NUM_MACC_ROLE) |
		  le32_encode_bits(option->num_opch, RTW89_H2C_SCANOFLD_BE_W1_NUM_OP) |
		  le32_encode_bits(option->norm_pd, RTW89_H2C_SCANOFLD_BE_W1_NORM_PD);

	h2c->w2 = le32_encode_bits(option->slow_pd, RTW89_H2C_SCANOFLD_BE_W2_SLOW_PD) |
		  le32_encode_bits(option->norm_cy, RTW89_H2C_SCANOFLD_BE_W2_NORM_CY) |
		  le32_encode_bits(option->opch_end, RTW89_H2C_SCANOFLD_BE_W2_OPCH_END);

	h2c->w3 = le32_encode_bits(0, RTW89_H2C_SCANOFLD_BE_W3_NUM_SSID) |
		  le32_encode_bits(0, RTW89_H2C_SCANOFLD_BE_W3_NUM_SHORT_SSID) |
		  le32_encode_bits(0, RTW89_H2C_SCANOFLD_BE_W3_NUM_BSSID) |
		  le32_encode_bits(probe_id[NL80211_BAND_2GHZ], RTW89_H2C_SCANOFLD_BE_W3_PROBEID);

	h2c->w4 = le32_encode_bits(probe_id[NL80211_BAND_5GHZ],
				   RTW89_H2C_SCANOFLD_BE_W4_PROBE_5G) |
		  le32_encode_bits(probe_id[NL80211_BAND_6GHZ],
				   RTW89_H2C_SCANOFLD_BE_W4_PROBE_6G) |
		  le32_encode_bits(option->delay, RTW89_H2C_SCANOFLD_BE_W4_DELAY_START);

	h2c->w5 = le32_encode_bits(option->mlo_mode, RTW89_H2C_SCANOFLD_BE_W5_MLO_MODE);

	h2c->w6 = le32_encode_bits(option->prohib_chan,
				   RTW89_H2C_SCANOFLD_BE_W6_CHAN_PROHIB_LOW);
	h2c->w7 = le32_encode_bits(option->prohib_chan >> 32,
				   RTW89_H2C_SCANOFLD_BE_W7_CHAN_PROHIB_HIGH);
	if (!wowlan && req->no_cck) {
		h2c->w0 |= le32_encode_bits(true, RTW89_H2C_SCANOFLD_BE_W0_PROBE_WITH_RATE);
		h2c->w8 = le32_encode_bits(RTW89_HW_RATE_OFDM6,
					   RTW89_H2C_SCANOFLD_BE_W8_PROBE_RATE_2GHZ) |
			  le32_encode_bits(RTW89_HW_RATE_OFDM6,
					   RTW89_H2C_SCANOFLD_BE_W8_PROBE_RATE_5GHZ) |
			  le32_encode_bits(RTW89_HW_RATE_OFDM6,
					   RTW89_H2C_SCANOFLD_BE_W8_PROBE_RATE_6GHZ);
	}

	if (scan_offload_ver == 0)
		goto flex_member;

	h2c->w9 = le32_encode_bits(sizeof(*h2c) / sizeof(h2c->w0),
				   RTW89_H2C_SCANOFLD_BE_W9_SIZE_CFG) |
		  le32_encode_bits(sizeof(*macc_role) / sizeof(macc_role->w0),
				   RTW89_H2C_SCANOFLD_BE_W9_SIZE_MACC) |
		  le32_encode_bits(sizeof(*opch) / sizeof(opch->w0),
				   RTW89_H2C_SCANOFLD_BE_W9_SIZE_OP);

flex_member:
	ptr += cfg_len;

	for (i = 0; i < option->num_macc_role; i++) {
		macc_role = ptr;
		macc_role->w0 =
			le32_encode_bits(0, RTW89_H2C_SCANOFLD_BE_MACC_ROLE_W0_BAND) |
			le32_encode_bits(0, RTW89_H2C_SCANOFLD_BE_MACC_ROLE_W0_PORT) |
			le32_encode_bits(0, RTW89_H2C_SCANOFLD_BE_MACC_ROLE_W0_MACID) |
			le32_encode_bits(0, RTW89_H2C_SCANOFLD_BE_MACC_ROLE_W0_OPCH_END);
		ptr += sizeof(*macc_role);
	}

	for (i = 0; i < option->num_opch; i++) {
		opch = ptr;
		opch->w0 = le32_encode_bits(rtwvif_link->mac_id,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W0_MACID) |
			   le32_encode_bits(option->band,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W0_BAND) |
			   le32_encode_bits(rtwvif_link->port,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W0_PORT) |
			   le32_encode_bits(RTW89_SCAN_OPMODE_INTV,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W0_POLICY) |
			   le32_encode_bits(true,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W0_TXNULL) |
			   le32_encode_bits(RTW89_OFF_CHAN_TIME / 10,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W0_POLICY_VAL);

		opch->w1 = le32_encode_bits(op->band_type,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W1_CH_BAND) |
			   le32_encode_bits(op->band_width,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W1_BW) |
			   le32_encode_bits(0x3,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W1_NOTIFY) |
			   le32_encode_bits(op->primary_channel,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W1_PRI_CH) |
			   le32_encode_bits(op->channel,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W1_CENTRAL_CH);

		opch->w2 = le32_encode_bits(0,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W2_PKTS_CTRL) |
			   le32_encode_bits(0,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W2_SW_DEF) |
			   le32_encode_bits(rtw89_is_mlo_1_1(rtwdev) ? 1 : 2,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W2_SS);

		opch->w3 = le32_encode_bits(RTW89_SCANOFLD_PKT_NONE,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W3_PKT0) |
			   le32_encode_bits(RTW89_SCANOFLD_PKT_NONE,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W3_PKT1) |
			   le32_encode_bits(RTW89_SCANOFLD_PKT_NONE,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W3_PKT2) |
			   le32_encode_bits(RTW89_SCANOFLD_PKT_NONE,
					    RTW89_H2C_SCANOFLD_BE_OPCH_W3_PKT3);

		if (ver == 0)
			opch->w1 |= le32_encode_bits(RTW89_CHANNEL_TIME,
						     RTW89_H2C_SCANOFLD_BE_OPCH_W1_DURATION);
		else
			opch->w4 = le32_encode_bits(RTW89_CHANNEL_TIME,
						    RTW89_H2C_SCANOFLD_BE_OPCH_W4_DURATION_V1);
		ptr += sizeof(*opch);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_SCANOFLD_BE, 1, 1,
			      len);

	if (option->enable)
		cond = RTW89_SCANOFLD_BE_WAIT_COND_START;
	else
		cond = RTW89_SCANOFLD_BE_WAIT_COND_STOP;

	ret = rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_FW, "failed to scan be ofld\n");
		return ret;
	}

	return 0;
}

int rtw89_fw_h2c_rf_reg(struct rtw89_dev *rtwdev,
			struct rtw89_fw_h2c_rf_reg_info *info,
			u16 len, u8 page)
{
	struct sk_buff *skb;
	u8 class = info->rf_path == RF_PATH_A ?
		   H2C_CL_OUTSRC_RF_REG_A : H2C_CL_OUTSRC_RF_REG_B;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c rf reg\n");
		return -ENOMEM;
	}
	skb_put_data(skb, info->rtw89_phy_config_rf_h2c[page], len);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, class, page, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_rf_ntfy_mcc(struct rtw89_dev *rtwdev)
{
	struct rtw89_rfk_mcc_info_data *rfk_mcc = rtwdev->rfk_mcc.data;
	struct rtw89_fw_h2c_rf_get_mccch *mccch;
	struct sk_buff *skb;
	int ret;
	u8 idx;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, sizeof(*mccch));
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_ctrl\n");
		return -ENOMEM;
	}
	skb_put(skb, sizeof(*mccch));
	mccch = (struct rtw89_fw_h2c_rf_get_mccch *)skb->data;

	idx = rfk_mcc->table_idx;
	mccch->ch_0 = cpu_to_le32(rfk_mcc->ch[0]);
	mccch->ch_1 = cpu_to_le32(rfk_mcc->ch[1]);
	mccch->band_0 = cpu_to_le32(rfk_mcc->band[0]);
	mccch->band_1 = cpu_to_le32(rfk_mcc->band[1]);
	mccch->current_channel = cpu_to_le32(rfk_mcc->ch[idx]);
	mccch->current_band_type = cpu_to_le32(rfk_mcc->band[idx]);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, H2C_CL_OUTSRC_RF_FW_NOTIFY,
			      H2C_FUNC_OUTSRC_RF_GET_MCCCH, 0, 0,
			      sizeof(*mccch));

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}
EXPORT_SYMBOL(rtw89_fw_h2c_rf_ntfy_mcc);

int rtw89_fw_h2c_rf_pre_ntfy(struct rtw89_dev *rtwdev,
			     enum rtw89_phy_idx phy_idx)
{
	struct rtw89_rfk_mcc_info *rfk_mcc = &rtwdev->rfk_mcc;
	struct rtw89_fw_h2c_rfk_pre_info_common *common;
	struct rtw89_fw_h2c_rfk_pre_info_v0 *h2c_v0;
	struct rtw89_fw_h2c_rfk_pre_info_v1 *h2c_v1;
	struct rtw89_fw_h2c_rfk_pre_info *h2c;
	u8 tbl_sel[NUM_OF_RTW89_FW_RFK_PATH];
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	u8 ver = U8_MAX;
	u8 tbl, path;
	u32 val32;
	int ret;

	if (RTW89_CHK_FW_FEATURE(RFK_PRE_NOTIFY_V1, &rtwdev->fw)) {
		len = sizeof(*h2c_v1);
		ver = 1;
	} else if (RTW89_CHK_FW_FEATURE(RFK_PRE_NOTIFY_V0, &rtwdev->fw)) {
		len = sizeof(*h2c_v0);
		ver = 0;
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c rfk_pre_ntfy\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_fw_h2c_rfk_pre_info *)skb->data;
	common = &h2c->base_v1.common;

	common->mlo_mode = cpu_to_le32(rtwdev->mlo_dbcc_mode);

	BUILD_BUG_ON(NUM_OF_RTW89_FW_RFK_TBL > RTW89_RFK_CHS_NR);
	BUILD_BUG_ON(ARRAY_SIZE(rfk_mcc->data) < NUM_OF_RTW89_FW_RFK_PATH);

	for (tbl = 0; tbl < NUM_OF_RTW89_FW_RFK_TBL; tbl++) {
		for (path = 0; path < NUM_OF_RTW89_FW_RFK_PATH; path++) {
			common->dbcc.ch[path][tbl] =
				cpu_to_le32(rfk_mcc->data[path].ch[tbl]);
			common->dbcc.band[path][tbl] =
				cpu_to_le32(rfk_mcc->data[path].band[tbl]);
		}
	}

	for (path = 0; path < NUM_OF_RTW89_FW_RFK_PATH; path++) {
		tbl_sel[path] = rfk_mcc->data[path].table_idx;

		common->tbl.cur_ch[path] =
			cpu_to_le32(rfk_mcc->data[path].ch[tbl_sel[path]]);
		common->tbl.cur_band[path] =
			cpu_to_le32(rfk_mcc->data[path].band[tbl_sel[path]]);

		if (ver <= 1)
			continue;

		h2c->cur_bandwidth[path] =
			cpu_to_le32(rfk_mcc->data[path].bw[tbl_sel[path]]);
	}

	common->phy_idx = cpu_to_le32(phy_idx);

	if (ver == 0) { /* RFK_PRE_NOTIFY_V0 */
		h2c_v0 = (struct rtw89_fw_h2c_rfk_pre_info_v0 *)skb->data;

		h2c_v0->cur_band = cpu_to_le32(rfk_mcc->data[0].band[tbl_sel[0]]);
		h2c_v0->cur_bw = cpu_to_le32(rfk_mcc->data[0].bw[tbl_sel[0]]);
		h2c_v0->cur_center_ch = cpu_to_le32(rfk_mcc->data[0].ch[tbl_sel[0]]);

		val32 = rtw89_phy_read32_mask(rtwdev, R_COEF_SEL, B_COEF_SEL_IQC_V1);
		h2c_v0->ktbl_sel0 = cpu_to_le32(val32);
		val32 = rtw89_phy_read32_mask(rtwdev, R_COEF_SEL_C1, B_COEF_SEL_IQC_V1);
		h2c_v0->ktbl_sel1 = cpu_to_le32(val32);
		val32 = rtw89_read_rf(rtwdev, RF_PATH_A, RR_CFGCH, RFREG_MASK);
		h2c_v0->rfmod0 = cpu_to_le32(val32);
		val32 = rtw89_read_rf(rtwdev, RF_PATH_B, RR_CFGCH, RFREG_MASK);
		h2c_v0->rfmod1 = cpu_to_le32(val32);

		if (rtw89_is_mlo_1_1(rtwdev))
			h2c_v0->mlo_1_1 = cpu_to_le32(1);

		h2c_v0->rfe_type = cpu_to_le32(rtwdev->efuse.rfe_type);

		goto done;
	}

	if (rtw89_is_mlo_1_1(rtwdev)) {
		h2c_v1 = &h2c->base_v1;
		h2c_v1->mlo_1_1 = cpu_to_le32(1);
	}
done:
	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, H2C_CL_OUTSRC_RF_FW_RFK,
			      H2C_FUNC_RFK_PRE_NOTIFY, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_rf_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			 const struct rtw89_chan *chan, enum rtw89_tssi_mode tssi_mode)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_h2c_rf_tssi *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c RF TSSI\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_rf_tssi *)skb->data;

	h2c->len = cpu_to_le16(len);
	h2c->phy = phy_idx;
	h2c->ch = chan->channel;
	h2c->bw = chan->band_width;
	h2c->band = chan->band_type;
	h2c->hwtx_en = true;
	h2c->cv = hal->cv;
	h2c->tssi_mode = tssi_mode;

	rtw89_phy_rfk_tssi_fill_fwcmd_efuse_to_de(rtwdev, phy_idx, chan, h2c);
	rtw89_phy_rfk_tssi_fill_fwcmd_tmeter_tbl(rtwdev, phy_idx, chan, h2c);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, H2C_CL_OUTSRC_RF_FW_RFK,
			      H2C_FUNC_RFK_TSSI_OFFLOAD, 0, 0, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_rf_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			const struct rtw89_chan *chan)
{
	struct rtw89_h2c_rf_iqk *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c RF IQK\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_rf_iqk *)skb->data;

	h2c->phy_idx = cpu_to_le32(phy_idx);
	h2c->dbcc = cpu_to_le32(rtwdev->dbcc_en);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, H2C_CL_OUTSRC_RF_FW_RFK,
			      H2C_FUNC_RFK_IQK_OFFLOAD, 0, 0, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_rf_dpk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			const struct rtw89_chan *chan)
{
	struct rtw89_h2c_rf_dpk *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c RF DPK\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_rf_dpk *)skb->data;

	h2c->len = len;
	h2c->phy = phy_idx;
	h2c->dpk_enable = true;
	h2c->kpath = RF_AB;
	h2c->cur_band = chan->band_type;
	h2c->cur_bw = chan->band_width;
	h2c->cur_ch = chan->channel;
	h2c->dpk_dbg_en = rtw89_debug_is_enabled(rtwdev, RTW89_DBG_RFK);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, H2C_CL_OUTSRC_RF_FW_RFK,
			      H2C_FUNC_RFK_DPK_OFFLOAD, 0, 0, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_rf_txgapk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			   const struct rtw89_chan *chan)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_h2c_rf_txgapk *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c RF TXGAPK\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_rf_txgapk *)skb->data;

	h2c->len = len;
	h2c->ktype = 2;
	h2c->phy = phy_idx;
	h2c->kpath = RF_AB;
	h2c->band = chan->band_type;
	h2c->bw = chan->band_width;
	h2c->ch = chan->channel;
	h2c->cv = hal->cv;

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, H2C_CL_OUTSRC_RF_FW_RFK,
			      H2C_FUNC_RFK_TXGAPK_OFFLOAD, 0, 0, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_rf_dack(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			 const struct rtw89_chan *chan)
{
	struct rtw89_h2c_rf_dack *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c RF DACK\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_rf_dack *)skb->data;

	h2c->len = cpu_to_le32(len);
	h2c->phy = cpu_to_le32(phy_idx);
	h2c->type = cpu_to_le32(0);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, H2C_CL_OUTSRC_RF_FW_RFK,
			      H2C_FUNC_RFK_DACK_OFFLOAD, 0, 0, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_rf_rxdck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			  const struct rtw89_chan *chan, bool is_chl_k)
{
	struct rtw89_h2c_rf_rxdck_v0 *v0;
	struct rtw89_h2c_rf_rxdck *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ver = -1;
	int ret;

	if (RTW89_CHK_FW_FEATURE(RFK_RXDCK_V0, &rtwdev->fw)) {
		len = sizeof(*v0);
		ver = 0;
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c RF RXDCK\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	v0 = (struct rtw89_h2c_rf_rxdck_v0 *)skb->data;

	v0->len = len;
	v0->phy = phy_idx;
	v0->is_afe = false;
	v0->kpath = RF_AB;
	v0->cur_band = chan->band_type;
	v0->cur_bw = chan->band_width;
	v0->cur_ch = chan->channel;
	v0->rxdck_dbg_en = rtw89_debug_is_enabled(rtwdev, RTW89_DBG_RFK);

	if (ver == 0)
		goto hdr;

	h2c = (struct rtw89_h2c_rf_rxdck *)skb->data;
	h2c->is_chl_k = is_chl_k;

hdr:
	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, H2C_CL_OUTSRC_RF_FW_RFK,
			      H2C_FUNC_RFK_RXDCK_OFFLOAD, 0, 0, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_raw_with_hdr(struct rtw89_dev *rtwdev,
			      u8 h2c_class, u8 h2c_func, u8 *buf, u16 len,
			      bool rack, bool dack)
{
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for raw with hdr\n");
		return -ENOMEM;
	}
	skb_put_data(skb, buf, len);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, h2c_class, h2c_func, rack, dack,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_raw(struct rtw89_dev *rtwdev, const u8 *buf, u16 len)
{
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_no_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c raw\n");
		return -ENOMEM;
	}
	skb_put_data(skb, buf, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

void rtw89_fw_send_all_early_h2c(struct rtw89_dev *rtwdev)
{
	struct rtw89_early_h2c *early_h2c;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	list_for_each_entry(early_h2c, &rtwdev->early_h2c_list, list) {
		rtw89_fw_h2c_raw(rtwdev, early_h2c->h2c, early_h2c->h2c_len);
	}
}

void __rtw89_fw_free_all_early_h2c(struct rtw89_dev *rtwdev)
{
	struct rtw89_early_h2c *early_h2c, *tmp;

	list_for_each_entry_safe(early_h2c, tmp, &rtwdev->early_h2c_list, list) {
		list_del(&early_h2c->list);
		kfree(early_h2c->h2c);
		kfree(early_h2c);
	}
}

void rtw89_fw_free_all_early_h2c(struct rtw89_dev *rtwdev)
{
	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	__rtw89_fw_free_all_early_h2c(rtwdev);
}

static void rtw89_fw_c2h_parse_attr(struct sk_buff *c2h)
{
	const struct rtw89_c2h_hdr *hdr = (const struct rtw89_c2h_hdr *)c2h->data;
	struct rtw89_fw_c2h_attr *attr = RTW89_SKB_C2H_CB(c2h);

	attr->category = le32_get_bits(hdr->w0, RTW89_C2H_HDR_W0_CATEGORY);
	attr->class = le32_get_bits(hdr->w0, RTW89_C2H_HDR_W0_CLASS);
	attr->func = le32_get_bits(hdr->w0, RTW89_C2H_HDR_W0_FUNC);
	attr->len = le32_get_bits(hdr->w1, RTW89_C2H_HDR_W1_LEN);
}

static bool rtw89_fw_c2h_chk_atomic(struct rtw89_dev *rtwdev,
				    struct sk_buff *c2h)
{
	struct rtw89_fw_c2h_attr *attr = RTW89_SKB_C2H_CB(c2h);
	u8 category = attr->category;
	u8 class = attr->class;
	u8 func = attr->func;

	switch (category) {
	default:
		return false;
	case RTW89_C2H_CAT_MAC:
		return rtw89_mac_c2h_chk_atomic(rtwdev, c2h, class, func);
	case RTW89_C2H_CAT_OUTSRC:
		return rtw89_phy_c2h_chk_atomic(rtwdev, class, func);
	}
}

void rtw89_fw_c2h_irqsafe(struct rtw89_dev *rtwdev, struct sk_buff *c2h)
{
	rtw89_fw_c2h_parse_attr(c2h);
	if (!rtw89_fw_c2h_chk_atomic(rtwdev, c2h))
		goto enqueue;

	rtw89_fw_c2h_cmd_handle(rtwdev, c2h);
	dev_kfree_skb_any(c2h);
	return;

enqueue:
	skb_queue_tail(&rtwdev->c2h_queue, c2h);
	wiphy_work_queue(rtwdev->hw->wiphy, &rtwdev->c2h_work);
}

static void rtw89_fw_c2h_cmd_handle(struct rtw89_dev *rtwdev,
				    struct sk_buff *skb)
{
	struct rtw89_fw_c2h_attr *attr = RTW89_SKB_C2H_CB(skb);
	u8 category = attr->category;
	u8 class = attr->class;
	u8 func = attr->func;
	u16 len = attr->len;
	bool dump = true;

	if (!test_bit(RTW89_FLAG_RUNNING, rtwdev->flags))
		return;

	switch (category) {
	case RTW89_C2H_CAT_TEST:
		break;
	case RTW89_C2H_CAT_MAC:
		rtw89_mac_c2h_handle(rtwdev, skb, len, class, func);
		if (class == RTW89_MAC_C2H_CLASS_INFO &&
		    func == RTW89_MAC_C2H_FUNC_C2H_LOG)
			dump = false;
		break;
	case RTW89_C2H_CAT_OUTSRC:
		if (class >= RTW89_PHY_C2H_CLASS_BTC_MIN &&
		    class <= RTW89_PHY_C2H_CLASS_BTC_MAX)
			rtw89_btc_c2h_handle(rtwdev, skb, len, class, func);
		else
			rtw89_phy_c2h_handle(rtwdev, skb, len, class, func);
		break;
	}

	if (dump)
		rtw89_hex_dump(rtwdev, RTW89_DBG_FW, "C2H: ", skb->data, skb->len);
}

void rtw89_fw_c2h_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						c2h_work);
	struct sk_buff *skb, *tmp;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	skb_queue_walk_safe(&rtwdev->c2h_queue, skb, tmp) {
		skb_unlink(skb, &rtwdev->c2h_queue);
		rtw89_fw_c2h_cmd_handle(rtwdev, skb);
		dev_kfree_skb_any(skb);
	}
}

static int rtw89_fw_write_h2c_reg(struct rtw89_dev *rtwdev,
				  struct rtw89_mac_h2c_info *info)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_fw_info *fw_info = &rtwdev->fw;
	const u32 *h2c_reg = chip->h2c_regs;
	u8 i, val, len;
	int ret;

	ret = read_poll_timeout(rtw89_read8, val, val == 0, 1000, 5000, false,
				rtwdev, chip->h2c_ctrl_reg);
	if (ret) {
		rtw89_warn(rtwdev, "FW does not process h2c registers\n");
		return ret;
	}

	len = DIV_ROUND_UP(info->content_len + RTW89_H2CREG_HDR_LEN,
			   sizeof(info->u.h2creg[0]));

	u32p_replace_bits(&info->u.hdr.w0, info->id, RTW89_H2CREG_HDR_FUNC_MASK);
	u32p_replace_bits(&info->u.hdr.w0, len, RTW89_H2CREG_HDR_LEN_MASK);

	for (i = 0; i < RTW89_H2CREG_MAX; i++)
		rtw89_write32(rtwdev, h2c_reg[i], info->u.h2creg[i]);

	fw_info->h2c_counter++;
	rtw89_write8_mask(rtwdev, chip->h2c_counter_reg.addr,
			  chip->h2c_counter_reg.mask, fw_info->h2c_counter);
	rtw89_write8(rtwdev, chip->h2c_ctrl_reg, B_AX_H2CREG_TRIGGER);

	return 0;
}

static int rtw89_fw_read_c2h_reg(struct rtw89_dev *rtwdev,
				 struct rtw89_mac_c2h_info *info)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_fw_info *fw_info = &rtwdev->fw;
	const u32 *c2h_reg = chip->c2h_regs;
	u32 ret;
	u8 i, val;

	info->id = RTW89_FWCMD_C2HREG_FUNC_NULL;

	ret = read_poll_timeout_atomic(rtw89_read8, val, val, 1,
				       RTW89_C2H_TIMEOUT, false, rtwdev,
				       chip->c2h_ctrl_reg);
	if (ret) {
		rtw89_warn(rtwdev, "c2h reg timeout\n");
		return ret;
	}

	for (i = 0; i < RTW89_C2HREG_MAX; i++)
		info->u.c2hreg[i] = rtw89_read32(rtwdev, c2h_reg[i]);

	rtw89_write8(rtwdev, chip->c2h_ctrl_reg, 0);

	info->id = u32_get_bits(info->u.hdr.w0, RTW89_C2HREG_HDR_FUNC_MASK);
	info->content_len =
		(u32_get_bits(info->u.hdr.w0, RTW89_C2HREG_HDR_LEN_MASK) << 2) -
		RTW89_C2HREG_HDR_LEN;

	fw_info->c2h_counter++;
	rtw89_write8_mask(rtwdev, chip->c2h_counter_reg.addr,
			  chip->c2h_counter_reg.mask, fw_info->c2h_counter);

	return 0;
}

int rtw89_fw_msg_reg(struct rtw89_dev *rtwdev,
		     struct rtw89_mac_h2c_info *h2c_info,
		     struct rtw89_mac_c2h_info *c2h_info)
{
	u32 ret;

	if (h2c_info && h2c_info->id != RTW89_FWCMD_H2CREG_FUNC_GET_FEATURE)
		lockdep_assert_wiphy(rtwdev->hw->wiphy);

	if (!h2c_info && !c2h_info)
		return -EINVAL;

	if (!h2c_info)
		goto recv_c2h;

	ret = rtw89_fw_write_h2c_reg(rtwdev, h2c_info);
	if (ret)
		return ret;

recv_c2h:
	if (!c2h_info)
		return 0;

	ret = rtw89_fw_read_c2h_reg(rtwdev, c2h_info);
	if (ret)
		return ret;

	return 0;
}

void rtw89_fw_st_dbg_dump(struct rtw89_dev *rtwdev)
{
	if (!test_bit(RTW89_FLAG_POWERON, rtwdev->flags)) {
		rtw89_err(rtwdev, "[ERR]pwr is off\n");
		return;
	}

	rtw89_info(rtwdev, "FW status = 0x%x\n", rtw89_read32(rtwdev, R_AX_UDM0));
	rtw89_info(rtwdev, "FW BADADDR = 0x%x\n", rtw89_read32(rtwdev, R_AX_UDM1));
	rtw89_info(rtwdev, "FW EPC/RA = 0x%x\n", rtw89_read32(rtwdev, R_AX_UDM2));
	rtw89_info(rtwdev, "FW MISC = 0x%x\n", rtw89_read32(rtwdev, R_AX_UDM3));
	rtw89_info(rtwdev, "R_AX_HALT_C2H = 0x%x\n",
		   rtw89_read32(rtwdev, R_AX_HALT_C2H));
	rtw89_info(rtwdev, "R_AX_SER_DBG_INFO = 0x%x\n",
		   rtw89_read32(rtwdev, R_AX_SER_DBG_INFO));

	rtw89_fw_prog_cnt_dump(rtwdev);
}

static void rtw89_hw_scan_release_pkt_list(struct rtw89_dev *rtwdev)
{
	struct list_head *pkt_list = rtwdev->scan_info.pkt_list;
	struct rtw89_pktofld_info *info, *tmp;
	u8 idx;

	for (idx = NL80211_BAND_2GHZ; idx < NUM_NL80211_BANDS; idx++) {
		if (!(rtwdev->chip->support_bands & BIT(idx)))
			continue;

		list_for_each_entry_safe(info, tmp, &pkt_list[idx], list) {
			if (test_bit(info->id, rtwdev->pkt_offload))
				rtw89_fw_h2c_del_pkt_offload(rtwdev, info->id);
			list_del(&info->list);
			kfree(info);
		}
	}
}

static void rtw89_hw_scan_cleanup(struct rtw89_dev *rtwdev,
				  struct rtw89_vif_link *rtwvif_link)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;

	mac->free_chan_list(rtwdev);
	rtw89_hw_scan_release_pkt_list(rtwdev);

	rtwvif->scan_req = NULL;
	rtwvif->scan_ies = NULL;
	scan_info->scanning_vif = NULL;
	scan_info->abort = false;
	scan_info->connected = false;
}

static bool rtw89_is_6ghz_wildcard_probe_req(struct rtw89_dev *rtwdev,
					     struct cfg80211_scan_request *req,
					     struct rtw89_pktofld_info *info,
					     enum nl80211_band band, u8 ssid_idx)
{
	if (band != NL80211_BAND_6GHZ)
		return false;

	if (req->ssids[ssid_idx].ssid_len) {
		memcpy(info->ssid, req->ssids[ssid_idx].ssid,
		       req->ssids[ssid_idx].ssid_len);
		info->ssid_len = req->ssids[ssid_idx].ssid_len;
		return false;
	} else {
		info->wildcard_6ghz = true;
		return true;
	}
}

static int rtw89_append_probe_req_ie(struct rtw89_dev *rtwdev,
				     struct rtw89_vif_link *rtwvif_link,
				     struct sk_buff *skb, u8 ssid_idx)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct ieee80211_scan_ies *ies = rtwvif->scan_ies;
	struct cfg80211_scan_request *req = rtwvif->scan_req;
	struct rtw89_pktofld_info *info;
	struct sk_buff *new;
	int ret = 0;
	u8 band;

	for (band = NL80211_BAND_2GHZ; band < NUM_NL80211_BANDS; band++) {
		if (!(rtwdev->chip->support_bands & BIT(band)))
			continue;

		new = skb_copy(skb, GFP_KERNEL);
		if (!new) {
			ret = -ENOMEM;
			goto out;
		}
		skb_put_data(new, ies->ies[band], ies->len[band]);
		skb_put_data(new, ies->common_ies, ies->common_ie_len);

		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info) {
			ret = -ENOMEM;
			kfree_skb(new);
			goto out;
		}

		rtw89_is_6ghz_wildcard_probe_req(rtwdev, req, info, band, ssid_idx);

		ret = rtw89_fw_h2c_add_pkt_offload(rtwdev, &info->id, new);
		if (ret) {
			kfree_skb(new);
			kfree(info);
			goto out;
		}

		list_add_tail(&info->list, &scan_info->pkt_list[band]);
		kfree_skb(new);
	}
out:
	return ret;
}

static int rtw89_hw_scan_update_probe_req(struct rtw89_dev *rtwdev,
					  struct rtw89_vif_link *rtwvif_link,
					  const u8 *mac_addr)
{
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct cfg80211_scan_request *req = rtwvif->scan_req;
	struct sk_buff *skb;
	u8 num = req->n_ssids, i;
	int ret;

	for (i = 0; i < num; i++) {
		skb = ieee80211_probereq_get(rtwdev->hw, mac_addr,
					     req->ssids[i].ssid,
					     req->ssids[i].ssid_len,
					     req->ie_len);
		if (!skb)
			return -ENOMEM;

		ret = rtw89_append_probe_req_ie(rtwdev, rtwvif_link, skb, i);
		kfree_skb(skb);

		if (ret)
			return ret;
	}

	return 0;
}

static int rtw89_update_6ghz_rnr_chan_ax(struct rtw89_dev *rtwdev,
					 struct ieee80211_scan_ies *ies,
					 struct cfg80211_scan_request *req,
					 struct rtw89_mac_chinfo_ax *ch_info)
{
	struct rtw89_vif_link *rtwvif_link = rtwdev->scan_info.scanning_vif;
	struct list_head *pkt_list = rtwdev->scan_info.pkt_list;
	struct cfg80211_scan_6ghz_params *params;
	struct rtw89_pktofld_info *info, *tmp;
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb;
	bool found;
	int ret = 0;
	u8 i;

	if (!req->n_6ghz_params)
		return 0;

	for (i = 0; i < req->n_6ghz_params; i++) {
		params = &req->scan_6ghz_params[i];

		if (req->channels[params->channel_idx]->hw_value !=
		    ch_info->pri_ch)
			continue;

		found = false;
		list_for_each_entry(tmp, &pkt_list[NL80211_BAND_6GHZ], list) {
			if (ether_addr_equal(tmp->bssid, params->bssid)) {
				found = true;
				break;
			}
		}
		if (found)
			continue;

		skb = ieee80211_probereq_get(rtwdev->hw, rtwvif_link->mac_addr,
					     NULL, 0, req->ie_len);
		if (!skb)
			return -ENOMEM;

		skb_put_data(skb, ies->ies[NL80211_BAND_6GHZ], ies->len[NL80211_BAND_6GHZ]);
		skb_put_data(skb, ies->common_ies, ies->common_ie_len);
		hdr = (struct ieee80211_hdr *)skb->data;
		ether_addr_copy(hdr->addr3, params->bssid);

		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info) {
			ret = -ENOMEM;
			kfree_skb(skb);
			goto out;
		}

		ret = rtw89_fw_h2c_add_pkt_offload(rtwdev, &info->id, skb);
		if (ret) {
			kfree_skb(skb);
			kfree(info);
			goto out;
		}

		ether_addr_copy(info->bssid, params->bssid);
		info->channel_6ghz = req->channels[params->channel_idx]->hw_value;
		list_add_tail(&info->list, &rtwdev->scan_info.pkt_list[NL80211_BAND_6GHZ]);

		ch_info->tx_pkt = true;
		ch_info->period = RTW89_CHANNEL_TIME_6G + RTW89_DWELL_TIME_6G;

		kfree_skb(skb);
	}

out:
	return ret;
}

static void rtw89_pno_scan_add_chan_ax(struct rtw89_dev *rtwdev,
				       int chan_type, int ssid_num,
				       struct rtw89_mac_chinfo_ax *ch_info)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_pktofld_info *info;
	u8 probe_count = 0;

	ch_info->notify_action = RTW89_SCANOFLD_DEBUG_MASK;
	ch_info->dfs_ch = chan_type == RTW89_CHAN_DFS;
	ch_info->bw = RTW89_SCAN_WIDTH;
	ch_info->tx_pkt = true;
	ch_info->cfg_tx_pwr = false;
	ch_info->tx_pwr_idx = 0;
	ch_info->tx_null = false;
	ch_info->pause_data = false;
	ch_info->probe_id = RTW89_SCANOFLD_PKT_NONE;

	if (ssid_num) {
		list_for_each_entry(info, &rtw_wow->pno_pkt_list, list) {
			if (info->channel_6ghz &&
			    ch_info->pri_ch != info->channel_6ghz)
				continue;
			else if (info->channel_6ghz && probe_count != 0)
				ch_info->period += RTW89_CHANNEL_TIME_6G;

			if (info->wildcard_6ghz)
				continue;

			ch_info->pkt_id[probe_count++] = info->id;
			if (probe_count >= RTW89_SCANOFLD_MAX_SSID)
				break;
		}
		ch_info->num_pkt = probe_count;
	}

	switch (chan_type) {
	case RTW89_CHAN_DFS:
		if (ch_info->ch_band != RTW89_BAND_6G)
			ch_info->period = max_t(u8, ch_info->period,
						RTW89_DFS_CHAN_TIME);
		ch_info->dwell_time = RTW89_DWELL_TIME;
		break;
	case RTW89_CHAN_ACTIVE:
		break;
	default:
		rtw89_err(rtwdev, "Channel type out of bound\n");
	}
}

static void rtw89_hw_scan_add_chan_ax(struct rtw89_dev *rtwdev, int chan_type,
				      int ssid_num,
				      struct rtw89_mac_chinfo_ax *ch_info)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct rtw89_vif_link *rtwvif_link = rtwdev->scan_info.scanning_vif;
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct ieee80211_scan_ies *ies = rtwvif->scan_ies;
	struct cfg80211_scan_request *req = rtwvif->scan_req;
	struct rtw89_chan *op = &rtwdev->scan_info.op_chan;
	struct rtw89_pktofld_info *info;
	u8 band, probe_count = 0;
	int ret;

	ch_info->notify_action = RTW89_SCANOFLD_DEBUG_MASK;
	ch_info->dfs_ch = chan_type == RTW89_CHAN_DFS;
	ch_info->bw = RTW89_SCAN_WIDTH;
	ch_info->tx_pkt = true;
	ch_info->cfg_tx_pwr = false;
	ch_info->tx_pwr_idx = 0;
	ch_info->tx_null = false;
	ch_info->pause_data = false;
	ch_info->probe_id = RTW89_SCANOFLD_PKT_NONE;

	if (ch_info->ch_band == RTW89_BAND_6G) {
		if ((ssid_num == 1 && req->ssids[0].ssid_len == 0) ||
		    !ch_info->is_psc) {
			ch_info->tx_pkt = false;
			if (!req->duration_mandatory)
				ch_info->period -= RTW89_DWELL_TIME_6G;
		}
	}

	ret = rtw89_update_6ghz_rnr_chan_ax(rtwdev, ies, req, ch_info);
	if (ret)
		rtw89_warn(rtwdev, "RNR fails: %d\n", ret);

	if (ssid_num) {
		band = rtw89_hw_to_nl80211_band(ch_info->ch_band);

		list_for_each_entry(info, &scan_info->pkt_list[band], list) {
			if (info->channel_6ghz &&
			    ch_info->pri_ch != info->channel_6ghz)
				continue;
			else if (info->channel_6ghz && probe_count != 0)
				ch_info->period += RTW89_CHANNEL_TIME_6G;

			if (info->wildcard_6ghz)
				continue;

			ch_info->pkt_id[probe_count++] = info->id;
			if (probe_count >= RTW89_SCANOFLD_MAX_SSID)
				break;
		}
		ch_info->num_pkt = probe_count;
	}

	switch (chan_type) {
	case RTW89_CHAN_OPERATE:
		ch_info->central_ch = op->channel;
		ch_info->pri_ch = op->primary_channel;
		ch_info->ch_band = op->band_type;
		ch_info->bw = op->band_width;
		ch_info->tx_null = true;
		ch_info->num_pkt = 0;
		break;
	case RTW89_CHAN_DFS:
		if (ch_info->ch_band != RTW89_BAND_6G)
			ch_info->period = max_t(u8, ch_info->period,
						RTW89_DFS_CHAN_TIME);
		ch_info->dwell_time = RTW89_DWELL_TIME;
		ch_info->pause_data = true;
		break;
	case RTW89_CHAN_ACTIVE:
		ch_info->pause_data = true;
		break;
	default:
		rtw89_err(rtwdev, "Channel type out of bound\n");
	}
}

static void rtw89_pno_scan_add_chan_be(struct rtw89_dev *rtwdev, int chan_type,
				       int ssid_num,
				       struct rtw89_mac_chinfo_be *ch_info)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_pktofld_info *info;
	u8 probe_count = 0, i;

	ch_info->notify_action = RTW89_SCANOFLD_DEBUG_MASK;
	ch_info->dfs_ch = chan_type == RTW89_CHAN_DFS;
	ch_info->bw = RTW89_SCAN_WIDTH;
	ch_info->tx_null = false;
	ch_info->pause_data = false;
	ch_info->probe_id = RTW89_SCANOFLD_PKT_NONE;

	if (ssid_num) {
		list_for_each_entry(info, &rtw_wow->pno_pkt_list, list) {
			ch_info->pkt_id[probe_count++] = info->id;
			if (probe_count >= RTW89_SCANOFLD_MAX_SSID)
				break;
		}
	}

	for (i = probe_count; i < RTW89_SCANOFLD_MAX_SSID; i++)
		ch_info->pkt_id[i] = RTW89_SCANOFLD_PKT_NONE;

	switch (chan_type) {
	case RTW89_CHAN_DFS:
		ch_info->period = max_t(u8, ch_info->period, RTW89_DFS_CHAN_TIME);
		ch_info->dwell_time = RTW89_DWELL_TIME;
		break;
	case RTW89_CHAN_ACTIVE:
		break;
	default:
		rtw89_warn(rtwdev, "Channel type out of bound\n");
		break;
	}
}

static void rtw89_hw_scan_add_chan_be(struct rtw89_dev *rtwdev, int chan_type,
				      int ssid_num,
				      struct rtw89_mac_chinfo_be *ch_info)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct rtw89_vif_link *rtwvif_link = rtwdev->scan_info.scanning_vif;
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct cfg80211_scan_request *req = rtwvif->scan_req;
	struct rtw89_pktofld_info *info;
	u8 band, probe_count = 0, i;

	ch_info->notify_action = RTW89_SCANOFLD_DEBUG_MASK;
	ch_info->dfs_ch = chan_type == RTW89_CHAN_DFS;
	ch_info->bw = RTW89_SCAN_WIDTH;
	ch_info->tx_null = false;
	ch_info->pause_data = false;
	ch_info->probe_id = RTW89_SCANOFLD_PKT_NONE;

	if (ssid_num) {
		band = rtw89_hw_to_nl80211_band(ch_info->ch_band);

		list_for_each_entry(info, &scan_info->pkt_list[band], list) {
			if (info->channel_6ghz &&
			    ch_info->pri_ch != info->channel_6ghz)
				continue;

			if (info->wildcard_6ghz)
				continue;

			ch_info->pkt_id[probe_count++] = info->id;
			if (probe_count >= RTW89_SCANOFLD_MAX_SSID)
				break;
		}
	}

	if (ch_info->ch_band == RTW89_BAND_6G) {
		if ((ssid_num == 1 && req->ssids[0].ssid_len == 0) ||
		    !ch_info->is_psc) {
			ch_info->probe_id = RTW89_SCANOFLD_PKT_NONE;
			if (!req->duration_mandatory)
				ch_info->period -= RTW89_DWELL_TIME_6G;
		}
	}

	for (i = probe_count; i < RTW89_SCANOFLD_MAX_SSID; i++)
		ch_info->pkt_id[i] = RTW89_SCANOFLD_PKT_NONE;

	switch (chan_type) {
	case RTW89_CHAN_DFS:
		if (ch_info->ch_band != RTW89_BAND_6G)
			ch_info->period =
				max_t(u8, ch_info->period, RTW89_DFS_CHAN_TIME);
		ch_info->dwell_time = RTW89_DWELL_TIME;
		ch_info->pause_data = true;
		break;
	case RTW89_CHAN_ACTIVE:
		ch_info->pause_data = true;
		break;
	default:
		rtw89_warn(rtwdev, "Channel type out of bound\n");
		break;
	}
}

int rtw89_pno_scan_add_chan_list_ax(struct rtw89_dev *rtwdev,
				    struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct cfg80211_sched_scan_request *nd_config = rtw_wow->nd_config;
	struct rtw89_mac_chinfo_ax *ch_info, *tmp;
	struct ieee80211_channel *channel;
	struct list_head chan_list;
	int list_len;
	enum rtw89_chan_type type;
	int ret = 0;
	u32 idx;

	INIT_LIST_HEAD(&chan_list);
	for (idx = 0, list_len = 0;
	     idx < nd_config->n_channels && list_len < RTW89_SCAN_LIST_LIMIT_AX;
	     idx++, list_len++) {
		channel = nd_config->channels[idx];
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			goto out;
		}

		ch_info->period = RTW89_CHANNEL_TIME;
		ch_info->ch_band = rtw89_nl80211_to_hw_band(channel->band);
		ch_info->central_ch = channel->hw_value;
		ch_info->pri_ch = channel->hw_value;
		ch_info->is_psc = cfg80211_channel_is_psc(channel);

		if (channel->flags &
		    (IEEE80211_CHAN_RADAR | IEEE80211_CHAN_NO_IR))
			type = RTW89_CHAN_DFS;
		else
			type = RTW89_CHAN_ACTIVE;

		rtw89_pno_scan_add_chan_ax(rtwdev, type, nd_config->n_match_sets, ch_info);
		list_add_tail(&ch_info->list, &chan_list);
	}
	ret = rtw89_fw_h2c_scan_list_offload_ax(rtwdev, list_len, &chan_list);

out:
	list_for_each_entry_safe(ch_info, tmp, &chan_list, list) {
		list_del(&ch_info->list);
		kfree(ch_info);
	}

	return ret;
}

int rtw89_hw_scan_prep_chan_list_ax(struct rtw89_dev *rtwdev,
				    struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct cfg80211_scan_request *req = rtwvif->scan_req;
	struct rtw89_mac_chinfo_ax *ch_info, *tmp;
	struct ieee80211_channel *channel;
	struct list_head chan_list;
	bool random_seq = req->flags & NL80211_SCAN_FLAG_RANDOM_SN;
	enum rtw89_chan_type type;
	int off_chan_time = 0;
	int ret;
	u32 idx;

	INIT_LIST_HEAD(&chan_list);

	for (idx = 0; idx < req->n_channels; idx++) {
		channel = req->channels[idx];
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			goto out;
		}

		if (req->duration)
			ch_info->period = req->duration;
		else if (channel->band == NL80211_BAND_6GHZ)
			ch_info->period = RTW89_CHANNEL_TIME_6G +
					  RTW89_DWELL_TIME_6G;
		else
			ch_info->period = RTW89_CHANNEL_TIME;

		ch_info->ch_band = rtw89_nl80211_to_hw_band(channel->band);
		ch_info->central_ch = channel->hw_value;
		ch_info->pri_ch = channel->hw_value;
		ch_info->rand_seq_num = random_seq;
		ch_info->is_psc = cfg80211_channel_is_psc(channel);

		if (channel->flags &
		    (IEEE80211_CHAN_RADAR | IEEE80211_CHAN_NO_IR))
			type = RTW89_CHAN_DFS;
		else
			type = RTW89_CHAN_ACTIVE;
		rtw89_hw_scan_add_chan_ax(rtwdev, type, req->n_ssids, ch_info);

		if (scan_info->connected &&
		    off_chan_time + ch_info->period > RTW89_OFF_CHAN_TIME) {
			tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
			if (!tmp) {
				ret = -ENOMEM;
				kfree(ch_info);
				goto out;
			}

			type = RTW89_CHAN_OPERATE;
			tmp->period = req->duration_mandatory ?
				      req->duration : RTW89_CHANNEL_TIME;
			rtw89_hw_scan_add_chan_ax(rtwdev, type, 0, tmp);
			list_add_tail(&tmp->list, &chan_list);
			off_chan_time = 0;
		}
		list_add_tail(&ch_info->list, &chan_list);
		off_chan_time += ch_info->period;
	}

	list_splice_tail(&chan_list, &scan_info->chan_list);
	return 0;

out:
	list_for_each_entry_safe(ch_info, tmp, &chan_list, list) {
		list_del(&ch_info->list);
		kfree(ch_info);
	}

	return ret;
}

void rtw89_hw_scan_free_chan_list_ax(struct rtw89_dev *rtwdev)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct rtw89_mac_chinfo_ax *ch_info, *tmp;

	list_for_each_entry_safe(ch_info, tmp, &scan_info->chan_list, list) {
		list_del(&ch_info->list);
		kfree(ch_info);
	}
}

int rtw89_hw_scan_add_chan_list_ax(struct rtw89_dev *rtwdev,
				   struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct rtw89_mac_chinfo_ax *ch_info, *tmp;
	unsigned int list_len = 0;
	struct list_head list;
	int ret;

	INIT_LIST_HEAD(&list);

	list_for_each_entry_safe(ch_info, tmp, &scan_info->chan_list, list) {
		list_move_tail(&ch_info->list, &list);

		list_len++;
		if (list_len == RTW89_SCAN_LIST_LIMIT_AX)
			break;
	}

	ret = rtw89_fw_h2c_scan_list_offload_ax(rtwdev, list_len, &list);

	list_for_each_entry_safe(ch_info, tmp, &list, list) {
		list_del(&ch_info->list);
		kfree(ch_info);
	}

	return ret;
}

int rtw89_pno_scan_add_chan_list_be(struct rtw89_dev *rtwdev,
				    struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct cfg80211_sched_scan_request *nd_config = rtw_wow->nd_config;
	struct rtw89_mac_chinfo_be *ch_info, *tmp;
	struct ieee80211_channel *channel;
	struct list_head chan_list;
	enum rtw89_chan_type type;
	int list_len, ret;
	u32 idx;

	INIT_LIST_HEAD(&chan_list);

	for (idx = 0, list_len = 0;
	     idx < nd_config->n_channels && list_len < RTW89_SCAN_LIST_LIMIT_BE;
	     idx++, list_len++) {
		channel = nd_config->channels[idx];
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			goto out;
		}

		ch_info->period = RTW89_CHANNEL_TIME;
		ch_info->ch_band = rtw89_nl80211_to_hw_band(channel->band);
		ch_info->central_ch = channel->hw_value;
		ch_info->pri_ch = channel->hw_value;
		ch_info->is_psc = cfg80211_channel_is_psc(channel);

		if (channel->flags &
		    (IEEE80211_CHAN_RADAR | IEEE80211_CHAN_NO_IR))
			type = RTW89_CHAN_DFS;
		else
			type = RTW89_CHAN_ACTIVE;

		rtw89_pno_scan_add_chan_be(rtwdev, type,
					   nd_config->n_match_sets, ch_info);
		list_add_tail(&ch_info->list, &chan_list);
	}

	ret = rtw89_fw_h2c_scan_list_offload_be(rtwdev, list_len, &chan_list,
						rtwvif_link);

out:
	list_for_each_entry_safe(ch_info, tmp, &chan_list, list) {
		list_del(&ch_info->list);
		kfree(ch_info);
	}

	return ret;
}

int rtw89_hw_scan_prep_chan_list_be(struct rtw89_dev *rtwdev,
				    struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct cfg80211_scan_request *req = rtwvif->scan_req;
	struct rtw89_mac_chinfo_be *ch_info, *tmp;
	struct ieee80211_channel *channel;
	struct list_head chan_list;
	enum rtw89_chan_type type;
	bool random_seq;
	int ret;
	u32 idx;

	random_seq = !!(req->flags & NL80211_SCAN_FLAG_RANDOM_SN);
	INIT_LIST_HEAD(&chan_list);

	for (idx = 0; idx < req->n_channels; idx++) {
		channel = req->channels[idx];
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			goto out;
		}

		if (req->duration)
			ch_info->period = req->duration;
		else if (channel->band == NL80211_BAND_6GHZ)
			ch_info->period = RTW89_CHANNEL_TIME_6G + RTW89_DWELL_TIME_6G;
		else
			ch_info->period = RTW89_CHANNEL_TIME;

		ch_info->ch_band = rtw89_nl80211_to_hw_band(channel->band);
		ch_info->central_ch = channel->hw_value;
		ch_info->pri_ch = channel->hw_value;
		ch_info->rand_seq_num = random_seq;
		ch_info->is_psc = cfg80211_channel_is_psc(channel);

		if (channel->flags & (IEEE80211_CHAN_RADAR | IEEE80211_CHAN_NO_IR))
			type = RTW89_CHAN_DFS;
		else
			type = RTW89_CHAN_ACTIVE;
		rtw89_hw_scan_add_chan_be(rtwdev, type, req->n_ssids, ch_info);

		list_add_tail(&ch_info->list, &chan_list);
	}

	list_splice_tail(&chan_list, &scan_info->chan_list);
	return 0;

out:
	list_for_each_entry_safe(ch_info, tmp, &chan_list, list) {
		list_del(&ch_info->list);
		kfree(ch_info);
	}

	return ret;
}

void rtw89_hw_scan_free_chan_list_be(struct rtw89_dev *rtwdev)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct rtw89_mac_chinfo_be *ch_info, *tmp;

	list_for_each_entry_safe(ch_info, tmp, &scan_info->chan_list, list) {
		list_del(&ch_info->list);
		kfree(ch_info);
	}
}

int rtw89_hw_scan_add_chan_list_be(struct rtw89_dev *rtwdev,
				   struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct rtw89_mac_chinfo_be *ch_info, *tmp;
	unsigned int list_len = 0;
	struct list_head list;
	int ret;

	INIT_LIST_HEAD(&list);

	list_for_each_entry_safe(ch_info, tmp, &scan_info->chan_list, list) {
		list_move_tail(&ch_info->list, &list);

		list_len++;
		if (list_len == RTW89_SCAN_LIST_LIMIT_BE)
			break;
	}

	ret = rtw89_fw_h2c_scan_list_offload_be(rtwdev, list_len, &list,
						rtwvif_link);

	list_for_each_entry_safe(ch_info, tmp, &list, list) {
		list_del(&ch_info->list);
		kfree(ch_info);
	}

	return ret;
}

static int rtw89_hw_scan_prehandle(struct rtw89_dev *rtwdev,
				   struct rtw89_vif_link *rtwvif_link,
				   const u8 *mac_addr)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	int ret;

	ret = rtw89_hw_scan_update_probe_req(rtwdev, rtwvif_link, mac_addr);
	if (ret) {
		rtw89_err(rtwdev, "Update probe request failed\n");
		goto out;
	}
	ret = mac->prep_chan_list(rtwdev, rtwvif_link);
out:
	return ret;
}

static void rtw89_hw_scan_update_link_beacon_noa(struct rtw89_dev *rtwdev,
						 struct rtw89_vif_link *rtwvif_link,
						 u16 tu)
{
	struct ieee80211_p2p_noa_desc noa_desc = {};
	u64 tsf;
	int ret;

	ret = rtw89_mac_port_get_tsf(rtwdev, rtwvif_link, &tsf);
	if (ret) {
		rtw89_warn(rtwdev, "%s: failed to get tsf\n", __func__);
		return;
	}

	noa_desc.start_time = cpu_to_le32(tsf);
	noa_desc.interval = cpu_to_le32(ieee80211_tu_to_usec(tu));
	noa_desc.duration = cpu_to_le32(ieee80211_tu_to_usec(tu));
	noa_desc.count = 1;

	rtw89_p2p_noa_renew(rtwvif_link);
	rtw89_p2p_noa_append(rtwvif_link, &noa_desc);
	rtw89_chip_h2c_update_beacon(rtwdev, rtwvif_link);
}

static void rtw89_hw_scan_update_beacon_noa(struct rtw89_dev *rtwdev,
					    const struct cfg80211_scan_request *req)
{
	const struct rtw89_entity_mgnt *mgnt = &rtwdev->hal.entity_mgnt;
	const struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_mac_chinfo_ax *chinfo_ax;
	struct rtw89_mac_chinfo_be *chinfo_be;
	struct rtw89_vif_link *rtwvif_link;
	struct list_head *pos, *tmp;
	struct ieee80211_vif *vif;
	struct rtw89_vif *rtwvif;
	u16 tu = 0;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	list_for_each_safe(pos, tmp, &scan_info->chan_list) {
		switch (chip->chip_gen) {
		case RTW89_CHIP_AX:
			chinfo_ax = list_entry(pos, typeof(*chinfo_ax), list);
			tu += chinfo_ax->period;
			break;
		case RTW89_CHIP_BE:
			chinfo_be = list_entry(pos, typeof(*chinfo_be), list);
			tu += chinfo_be->period;
			break;
		default:
			rtw89_warn(rtwdev, "%s: invalid chip gen %d\n",
				   __func__, chip->chip_gen);
			return;
		}
	}

	if (unlikely(tu == 0)) {
		rtw89_debug(rtwdev, RTW89_DBG_HW_SCAN,
			    "%s: cannot estimate needed TU\n", __func__);
		return;
	}

	list_for_each_entry(rtwvif, &mgnt->active_list, mgnt_entry) {
		unsigned int link_id;

		vif = rtwvif_to_vif(rtwvif);
		if (vif->type != NL80211_IFTYPE_AP || !vif->p2p)
			continue;

		rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
			rtw89_hw_scan_update_link_beacon_noa(rtwdev, rtwvif_link, tu);
	}
}

int rtw89_hw_scan_start(struct rtw89_dev *rtwdev,
			struct rtw89_vif_link *rtwvif_link,
			struct ieee80211_scan_request *scan_req)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	enum rtw89_entity_mode mode = rtw89_get_entity_mode(rtwdev);
	struct cfg80211_scan_request *req = &scan_req->req;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev,
						       rtwvif_link->chanctx_idx);
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	u32 rx_fltr = rtwdev->hal.rx_fltr;
	u8 mac_addr[ETH_ALEN];
	u32 reg;
	int ret;

	/* clone op and keep it during scan */
	rtwdev->scan_info.op_chan = *chan;

	rtwdev->scan_info.connected = rtw89_is_any_vif_connected_or_connecting(rtwdev);
	rtwdev->scan_info.scanning_vif = rtwvif_link;
	rtwdev->scan_info.abort = false;
	rtwvif->scan_ies = &scan_req->ies;
	rtwvif->scan_req = req;

	if (req->flags & NL80211_SCAN_FLAG_RANDOM_ADDR)
		get_random_mask_addr(mac_addr, req->mac_addr,
				     req->mac_addr_mask);
	else
		ether_addr_copy(mac_addr, rtwvif_link->mac_addr);

	ret = rtw89_hw_scan_prehandle(rtwdev, rtwvif_link, mac_addr);
	if (ret) {
		rtw89_hw_scan_cleanup(rtwdev, rtwvif_link);
		return ret;
	}

	ieee80211_stop_queues(rtwdev->hw);
	rtw89_mac_port_cfg_rx_sync(rtwdev, rtwvif_link, false);

	rtw89_core_scan_start(rtwdev, rtwvif_link, mac_addr, true);

	rx_fltr &= ~B_AX_A_BCN_CHK_EN;
	rx_fltr &= ~B_AX_A_BC;
	rx_fltr &= ~B_AX_A_A1_MATCH;

	reg = rtw89_mac_reg_by_idx(rtwdev, mac->rx_fltr, rtwvif_link->mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_RX_FLTR_CFG_MASK, rx_fltr);

	rtw89_chanctx_pause(rtwdev, RTW89_CHANCTX_PAUSE_REASON_HW_SCAN);

	if (mode == RTW89_ENTITY_MODE_MCC)
		rtw89_hw_scan_update_beacon_noa(rtwdev, req);

	return 0;
}

struct rtw89_hw_scan_complete_cb_data {
	struct rtw89_vif_link *rtwvif_link;
	bool aborted;
};

static int rtw89_hw_scan_complete_cb(struct rtw89_dev *rtwdev, void *data)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct rtw89_hw_scan_complete_cb_data *cb_data = data;
	struct rtw89_vif_link *rtwvif_link = cb_data->rtwvif_link;
	struct cfg80211_scan_info info = {
		.aborted = cb_data->aborted,
	};
	u32 reg;

	if (!rtwvif_link)
		return -EINVAL;

	reg = rtw89_mac_reg_by_idx(rtwdev, mac->rx_fltr, rtwvif_link->mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_RX_FLTR_CFG_MASK, rtwdev->hal.rx_fltr);

	rtw89_core_scan_complete(rtwdev, rtwvif_link, true);
	ieee80211_scan_completed(rtwdev->hw, &info);
	ieee80211_wake_queues(rtwdev->hw);
	rtw89_mac_port_cfg_rx_sync(rtwdev, rtwvif_link, true);
	rtw89_mac_enable_beacon_for_ap_vifs(rtwdev, true);

	rtw89_hw_scan_cleanup(rtwdev, rtwvif_link);

	return 0;
}

void rtw89_hw_scan_complete(struct rtw89_dev *rtwdev,
			    struct rtw89_vif_link *rtwvif_link,
			    bool aborted)
{
	struct rtw89_hw_scan_complete_cb_data cb_data = {
		.rtwvif_link = rtwvif_link,
		.aborted = aborted,
	};
	const struct rtw89_chanctx_cb_parm cb_parm = {
		.cb = rtw89_hw_scan_complete_cb,
		.data = &cb_data,
		.caller = __func__,
	};

	/* The things here needs to be done after setting channel (for coex)
	 * and before proceeding entity mode (for MCC). So, pass a callback
	 * of them for the right sequence rather than doing them directly.
	 */
	rtw89_chanctx_proceed(rtwdev, &cb_parm);
}

void rtw89_hw_scan_abort(struct rtw89_dev *rtwdev,
			 struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	int ret;

	scan_info->abort = true;

	ret = rtw89_hw_scan_offload(rtwdev, rtwvif_link, false);
	if (ret)
		rtw89_warn(rtwdev, "rtw89_hw_scan_offload failed ret %d\n", ret);

	/* Indicate ieee80211_scan_completed() before returning, which is safe
	 * because scan abort command always waits for completion of
	 * RTW89_SCAN_END_SCAN_NOTIFY, so that ieee80211_stop() can flush scan
	 * work properly.
	 */
	rtw89_hw_scan_complete(rtwdev, rtwvif_link, true);
}

static bool rtw89_is_any_vif_connected_or_connecting(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	unsigned int link_id;

	rtw89_for_each_rtwvif(rtwdev, rtwvif) {
		rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id) {
			/* This variable implies connected or during attempt to connect */
			if (!is_zero_ether_addr(rtwvif_link->bssid))
				return true;
		}
	}

	return false;
}

int rtw89_hw_scan_offload(struct rtw89_dev *rtwdev,
			  struct rtw89_vif_link *rtwvif_link,
			  bool enable)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct rtw89_scan_option opt = {0};
	bool connected;
	int ret = 0;

	if (!rtwvif_link)
		return -EINVAL;

	connected = rtwdev->scan_info.connected;
	opt.enable = enable;
	opt.target_ch_mode = connected;
	if (enable) {
		ret = mac->add_chan_list(rtwdev, rtwvif_link);
		if (ret)
			goto out;
	}

	if (rtwdev->chip->chip_gen == RTW89_CHIP_BE) {
		opt.operation = enable ? RTW89_SCAN_OP_START : RTW89_SCAN_OP_STOP;
		opt.scan_mode = RTW89_SCAN_MODE_SA;
		opt.band = rtwvif_link->mac_idx;
		opt.num_macc_role = 0;
		opt.mlo_mode = rtwdev->mlo_dbcc_mode;
		opt.num_opch = connected ? 1 : 0;
		opt.opch_end = connected ? 0 : RTW89_CHAN_INVALID;
	}

	ret = mac->scan_offload(rtwdev, &opt, rtwvif_link, false);
out:
	return ret;
}

#define H2C_FW_CPU_EXCEPTION_LEN 4
#define H2C_FW_CPU_EXCEPTION_TYPE_DEF 0x5566
int rtw89_fw_h2c_trigger_cpu_exception(struct rtw89_dev *rtwdev)
{
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_FW_CPU_EXCEPTION_LEN);
	if (!skb) {
		rtw89_err(rtwdev,
			  "failed to alloc skb for fw cpu exception\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_FW_CPU_EXCEPTION_LEN);
	RTW89_SET_FWCMD_CPU_EXCEPTION_TYPE(skb->data,
					   H2C_FW_CPU_EXCEPTION_TYPE_DEF);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_TEST,
			      H2C_CL_FW_STATUS_TEST,
			      H2C_FUNC_CPU_EXCEPTION, 0, 0,
			      H2C_FW_CPU_EXCEPTION_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;

fail:
	dev_kfree_skb_any(skb);
	return ret;
}

#define H2C_PKT_DROP_LEN 24
int rtw89_fw_h2c_pkt_drop(struct rtw89_dev *rtwdev,
			  const struct rtw89_pkt_drop_params *params)
{
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_PKT_DROP_LEN);
	if (!skb) {
		rtw89_err(rtwdev,
			  "failed to alloc skb for packet drop\n");
		return -ENOMEM;
	}

	switch (params->sel) {
	case RTW89_PKT_DROP_SEL_MACID_BE_ONCE:
	case RTW89_PKT_DROP_SEL_MACID_BK_ONCE:
	case RTW89_PKT_DROP_SEL_MACID_VI_ONCE:
	case RTW89_PKT_DROP_SEL_MACID_VO_ONCE:
	case RTW89_PKT_DROP_SEL_BAND_ONCE:
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_FW,
			    "H2C of pkt drop might not fully support sel: %d yet\n",
			    params->sel);
		break;
	}

	skb_put(skb, H2C_PKT_DROP_LEN);
	RTW89_SET_FWCMD_PKT_DROP_SEL(skb->data, params->sel);
	RTW89_SET_FWCMD_PKT_DROP_MACID(skb->data, params->macid);
	RTW89_SET_FWCMD_PKT_DROP_BAND(skb->data, params->mac_band);
	RTW89_SET_FWCMD_PKT_DROP_PORT(skb->data, params->port);
	RTW89_SET_FWCMD_PKT_DROP_MBSSID(skb->data, params->mbssid);
	RTW89_SET_FWCMD_PKT_DROP_ROLE_A_INFO_TF_TRS(skb->data, params->tf_trs);
	RTW89_SET_FWCMD_PKT_DROP_MACID_BAND_SEL_0(skb->data,
						  params->macid_band_sel[0]);
	RTW89_SET_FWCMD_PKT_DROP_MACID_BAND_SEL_1(skb->data,
						  params->macid_band_sel[1]);
	RTW89_SET_FWCMD_PKT_DROP_MACID_BAND_SEL_2(skb->data,
						  params->macid_band_sel[2]);
	RTW89_SET_FWCMD_PKT_DROP_MACID_BAND_SEL_3(skb->data,
						  params->macid_band_sel[3]);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_PKT_DROP, 0, 0,
			      H2C_PKT_DROP_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;

fail:
	dev_kfree_skb_any(skb);
	return ret;
}

#define H2C_KEEP_ALIVE_LEN 4
int rtw89_fw_h2c_keep_alive(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			    bool enable)
{
	struct sk_buff *skb;
	u8 pkt_id = 0;
	int ret;

	if (enable) {
		ret = rtw89_fw_h2c_add_general_pkt(rtwdev, rtwvif_link,
						   RTW89_PKT_OFLD_TYPE_NULL_DATA,
						   &pkt_id);
		if (ret)
			return -EPERM;
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_KEEP_ALIVE_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for keep alive\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_KEEP_ALIVE_LEN);

	RTW89_SET_KEEP_ALIVE_ENABLE(skb->data, enable);
	RTW89_SET_KEEP_ALIVE_PKT_NULL_ID(skb->data, pkt_id);
	RTW89_SET_KEEP_ALIVE_PERIOD(skb->data, 5);
	RTW89_SET_KEEP_ALIVE_MACID(skb->data, rtwvif_link->mac_id);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_WOW,
			      H2C_FUNC_KEEP_ALIVE, 0, 1,
			      H2C_KEEP_ALIVE_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;

fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_arp_offload(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			     bool enable)
{
	struct rtw89_h2c_arp_offload *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	u8 pkt_id = 0;
	int ret;

	if (enable) {
		ret = rtw89_fw_h2c_add_general_pkt(rtwdev, rtwvif_link,
						   RTW89_PKT_OFLD_TYPE_ARP_RSP,
						   &pkt_id);
		if (ret)
			return ret;
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for arp offload\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_arp_offload *)skb->data;

	h2c->w0 = le32_encode_bits(enable, RTW89_H2C_ARP_OFFLOAD_W0_ENABLE) |
		  le32_encode_bits(0, RTW89_H2C_ARP_OFFLOAD_W0_ACTION) |
		  le32_encode_bits(rtwvif_link->mac_id, RTW89_H2C_ARP_OFFLOAD_W0_MACID) |
		  le32_encode_bits(pkt_id, RTW89_H2C_ARP_OFFLOAD_W0_PKT_ID);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_WOW,
			      H2C_FUNC_ARP_OFLD, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;

fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_DISCONNECT_DETECT_LEN 8
int rtw89_fw_h2c_disconnect_detect(struct rtw89_dev *rtwdev,
				   struct rtw89_vif_link *rtwvif_link, bool enable)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct sk_buff *skb;
	u8 macid = rtwvif_link->mac_id;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_DISCONNECT_DETECT_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for keep alive\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_DISCONNECT_DETECT_LEN);

	if (test_bit(RTW89_WOW_FLAG_EN_DISCONNECT, rtw_wow->flags)) {
		RTW89_SET_DISCONNECT_DETECT_ENABLE(skb->data, enable);
		RTW89_SET_DISCONNECT_DETECT_DISCONNECT(skb->data, !enable);
		RTW89_SET_DISCONNECT_DETECT_MAC_ID(skb->data, macid);
		RTW89_SET_DISCONNECT_DETECT_CHECK_PERIOD(skb->data, 100);
		RTW89_SET_DISCONNECT_DETECT_TRY_PKT_COUNT(skb->data, 5);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_WOW,
			      H2C_FUNC_DISCONNECT_DETECT, 0, 1,
			      H2C_DISCONNECT_DETECT_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;

fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_cfg_pno(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			 bool enable)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct cfg80211_sched_scan_request *nd_config = rtw_wow->nd_config;
	struct rtw89_h2c_cfg_nlo *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret, i;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for nlo\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_cfg_nlo *)skb->data;

	h2c->w0 = le32_encode_bits(enable, RTW89_H2C_NLO_W0_ENABLE) |
		  le32_encode_bits(enable, RTW89_H2C_NLO_W0_IGNORE_CIPHER) |
		  le32_encode_bits(rtwvif_link->mac_id, RTW89_H2C_NLO_W0_MACID);

	if (enable) {
		h2c->nlo_cnt = nd_config->n_match_sets;
		for (i = 0 ; i < nd_config->n_match_sets; i++) {
			h2c->ssid_len[i] = nd_config->match_sets[i].ssid.ssid_len;
			memcpy(h2c->ssid[i], nd_config->match_sets[i].ssid.ssid,
			       nd_config->match_sets[i].ssid.ssid_len);
		}
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_WOW,
			      H2C_FUNC_NLO, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;

fail:
	dev_kfree_skb_any(skb);
	return ret;
}

int rtw89_fw_h2c_wow_global(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			    bool enable)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_h2c_wow_global *h2c;
	u8 macid = rtwvif_link->mac_id;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for wow global\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_wow_global *)skb->data;

	h2c->w0 = le32_encode_bits(enable, RTW89_H2C_WOW_GLOBAL_W0_ENABLE) |
		  le32_encode_bits(macid, RTW89_H2C_WOW_GLOBAL_W0_MAC_ID) |
		  le32_encode_bits(rtw_wow->ptk_alg,
				   RTW89_H2C_WOW_GLOBAL_W0_PAIRWISE_SEC_ALGO) |
		  le32_encode_bits(rtw_wow->gtk_alg,
				   RTW89_H2C_WOW_GLOBAL_W0_GROUP_SEC_ALGO);
	h2c->key_info = rtw_wow->key_info;

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_WOW,
			      H2C_FUNC_WOW_GLOBAL, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;

fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_WAKEUP_CTRL_LEN 4
int rtw89_fw_h2c_wow_wakeup_ctrl(struct rtw89_dev *rtwdev,
				 struct rtw89_vif_link *rtwvif_link,
				 bool enable)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct sk_buff *skb;
	u8 macid = rtwvif_link->mac_id;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_WAKEUP_CTRL_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for wakeup ctrl\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_WAKEUP_CTRL_LEN);

	if (rtw_wow->pattern_cnt)
		RTW89_SET_WOW_WAKEUP_CTRL_PATTERN_MATCH_ENABLE(skb->data, enable);
	if (test_bit(RTW89_WOW_FLAG_EN_MAGIC_PKT, rtw_wow->flags))
		RTW89_SET_WOW_WAKEUP_CTRL_MAGIC_ENABLE(skb->data, enable);
	if (test_bit(RTW89_WOW_FLAG_EN_DISCONNECT, rtw_wow->flags))
		RTW89_SET_WOW_WAKEUP_CTRL_DEAUTH_ENABLE(skb->data, enable);

	RTW89_SET_WOW_WAKEUP_CTRL_MAC_ID(skb->data, macid);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_WOW,
			      H2C_FUNC_WAKEUP_CTRL, 0, 1,
			      H2C_WAKEUP_CTRL_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;

fail:
	dev_kfree_skb_any(skb);

	return ret;
}

#define H2C_WOW_CAM_UPD_LEN 24
int rtw89_fw_wow_cam_update(struct rtw89_dev *rtwdev,
			    struct rtw89_wow_cam_info *cam_info)
{
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_WOW_CAM_UPD_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for keep alive\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_WOW_CAM_UPD_LEN);

	RTW89_SET_WOW_CAM_UPD_R_W(skb->data, cam_info->r_w);
	RTW89_SET_WOW_CAM_UPD_IDX(skb->data, cam_info->idx);
	if (cam_info->valid) {
		RTW89_SET_WOW_CAM_UPD_WKFM1(skb->data, cam_info->mask[0]);
		RTW89_SET_WOW_CAM_UPD_WKFM2(skb->data, cam_info->mask[1]);
		RTW89_SET_WOW_CAM_UPD_WKFM3(skb->data, cam_info->mask[2]);
		RTW89_SET_WOW_CAM_UPD_WKFM4(skb->data, cam_info->mask[3]);
		RTW89_SET_WOW_CAM_UPD_CRC(skb->data, cam_info->crc);
		RTW89_SET_WOW_CAM_UPD_NEGATIVE_PATTERN_MATCH(skb->data,
							     cam_info->negative_pattern_match);
		RTW89_SET_WOW_CAM_UPD_SKIP_MAC_HDR(skb->data,
						   cam_info->skip_mac_hdr);
		RTW89_SET_WOW_CAM_UPD_UC(skb->data, cam_info->uc);
		RTW89_SET_WOW_CAM_UPD_MC(skb->data, cam_info->mc);
		RTW89_SET_WOW_CAM_UPD_BC(skb->data, cam_info->bc);
	}
	RTW89_SET_WOW_CAM_UPD_VALID(skb->data, cam_info->valid);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_WOW,
			      H2C_FUNC_WOW_CAM_UPD, 0, 1,
			      H2C_WOW_CAM_UPD_LEN);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_wow_gtk_ofld(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link,
			      bool enable)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_gtk_info *gtk_info = &rtw_wow->gtk_info;
	struct rtw89_h2c_wow_gtk_ofld *h2c;
	u8 macid = rtwvif_link->mac_id;
	u32 len = sizeof(*h2c);
	u8 pkt_id_sa_query = 0;
	struct sk_buff *skb;
	u8 pkt_id_eapol = 0;
	int ret;

	if (!rtw_wow->gtk_alg)
		return 0;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for gtk ofld\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_wow_gtk_ofld *)skb->data;

	if (!enable)
		goto hdr;

	ret = rtw89_fw_h2c_add_general_pkt(rtwdev, rtwvif_link,
					   RTW89_PKT_OFLD_TYPE_EAPOL_KEY,
					   &pkt_id_eapol);
	if (ret)
		goto fail;

	if (gtk_info->igtk_keyid) {
		ret = rtw89_fw_h2c_add_general_pkt(rtwdev, rtwvif_link,
						   RTW89_PKT_OFLD_TYPE_SA_QUERY,
						   &pkt_id_sa_query);
		if (ret)
			goto fail;
	}

	/* not support TKIP yet */
	h2c->w0 = le32_encode_bits(enable, RTW89_H2C_WOW_GTK_OFLD_W0_EN) |
		  le32_encode_bits(0, RTW89_H2C_WOW_GTK_OFLD_W0_TKIP_EN) |
		  le32_encode_bits(gtk_info->igtk_keyid ? 1 : 0,
				   RTW89_H2C_WOW_GTK_OFLD_W0_IEEE80211W_EN) |
		  le32_encode_bits(macid, RTW89_H2C_WOW_GTK_OFLD_W0_MAC_ID) |
		  le32_encode_bits(pkt_id_eapol, RTW89_H2C_WOW_GTK_OFLD_W0_GTK_RSP_ID);
	h2c->w1 = le32_encode_bits(gtk_info->igtk_keyid ? pkt_id_sa_query : 0,
				   RTW89_H2C_WOW_GTK_OFLD_W1_PMF_SA_QUERY_ID) |
		  le32_encode_bits(rtw_wow->akm, RTW89_H2C_WOW_GTK_OFLD_W1_ALGO_AKM_SUIT);
	h2c->gtk_info = rtw_wow->gtk_info;

hdr:
	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_WOW,
			      H2C_FUNC_GTK_OFLD, 0, 1,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}
	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

int rtw89_fw_h2c_fwips(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
		       bool enable)
{
	struct rtw89_wait_info *wait = &rtwdev->mac.ps_wait;
	struct rtw89_h2c_fwips *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw ips\n");
		return -ENOMEM;
	}
	skb_put(skb, len);
	h2c = (struct rtw89_h2c_fwips *)skb->data;

	h2c->w0 = le32_encode_bits(rtwvif_link->mac_id, RTW89_H2C_FW_IPS_W0_MACID) |
		  le32_encode_bits(enable, RTW89_H2C_FW_IPS_W0_ENABLE);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_PS,
			      H2C_FUNC_IPS_CFG, 0, 1,
			      len);

	return rtw89_h2c_tx_and_wait(rtwdev, skb, wait, RTW89_PS_WAIT_COND_IPS_CFG);
}

int rtw89_fw_h2c_wow_request_aoac(struct rtw89_dev *rtwdev)
{
	struct rtw89_wait_info *wait = &rtwdev->wow.wait;
	struct rtw89_h2c_wow_aoac *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for aoac\n");
		return -ENOMEM;
	}

	skb_put(skb, len);

	/* This H2C only nofity firmware to generate AOAC report C2H,
	 * no need any parameter.
	 */
	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_WOW,
			      H2C_FUNC_AOAC_REPORT_REQ, 1, 0,
			      len);

	return rtw89_h2c_tx_and_wait(rtwdev, skb, wait, RTW89_WOW_WAIT_COND_AOAC);
}

/* Return < 0, if failures happen during waiting for the condition.
 * Return 0, when waiting for the condition succeeds.
 * Return > 0, if the wait is considered unreachable due to driver/FW design,
 * where 1 means during SER.
 */
static int rtw89_h2c_tx_and_wait(struct rtw89_dev *rtwdev, struct sk_buff *skb,
				 struct rtw89_wait_info *wait, unsigned int cond)
{
	int ret;

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		dev_kfree_skb_any(skb);
		return -EBUSY;
	}

	if (test_bit(RTW89_FLAG_SER_HANDLING, rtwdev->flags))
		return 1;

	return rtw89_wait_for_cond(wait, cond);
}

#define H2C_ADD_MCC_LEN 16
int rtw89_fw_h2c_add_mcc(struct rtw89_dev *rtwdev,
			 const struct rtw89_fw_mcc_add_req *p)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	struct sk_buff *skb;
	unsigned int cond;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_ADD_MCC_LEN);
	if (!skb) {
		rtw89_err(rtwdev,
			  "failed to alloc skb for add mcc\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_ADD_MCC_LEN);
	RTW89_SET_FWCMD_ADD_MCC_MACID(skb->data, p->macid);
	RTW89_SET_FWCMD_ADD_MCC_CENTRAL_CH_SEG0(skb->data, p->central_ch_seg0);
	RTW89_SET_FWCMD_ADD_MCC_CENTRAL_CH_SEG1(skb->data, p->central_ch_seg1);
	RTW89_SET_FWCMD_ADD_MCC_PRIMARY_CH(skb->data, p->primary_ch);
	RTW89_SET_FWCMD_ADD_MCC_BANDWIDTH(skb->data, p->bandwidth);
	RTW89_SET_FWCMD_ADD_MCC_GROUP(skb->data, p->group);
	RTW89_SET_FWCMD_ADD_MCC_C2H_RPT(skb->data, p->c2h_rpt);
	RTW89_SET_FWCMD_ADD_MCC_DIS_TX_NULL(skb->data, p->dis_tx_null);
	RTW89_SET_FWCMD_ADD_MCC_DIS_SW_RETRY(skb->data, p->dis_sw_retry);
	RTW89_SET_FWCMD_ADD_MCC_IN_CURR_CH(skb->data, p->in_curr_ch);
	RTW89_SET_FWCMD_ADD_MCC_SW_RETRY_COUNT(skb->data, p->sw_retry_count);
	RTW89_SET_FWCMD_ADD_MCC_TX_NULL_EARLY(skb->data, p->tx_null_early);
	RTW89_SET_FWCMD_ADD_MCC_BTC_IN_2G(skb->data, p->btc_in_2g);
	RTW89_SET_FWCMD_ADD_MCC_PTA_EN(skb->data, p->pta_en);
	RTW89_SET_FWCMD_ADD_MCC_RFK_BY_PASS(skb->data, p->rfk_by_pass);
	RTW89_SET_FWCMD_ADD_MCC_CH_BAND_TYPE(skb->data, p->ch_band_type);
	RTW89_SET_FWCMD_ADD_MCC_DURATION(skb->data, p->duration);
	RTW89_SET_FWCMD_ADD_MCC_COURTESY_EN(skb->data, p->courtesy_en);
	RTW89_SET_FWCMD_ADD_MCC_COURTESY_NUM(skb->data, p->courtesy_num);
	RTW89_SET_FWCMD_ADD_MCC_COURTESY_TARGET(skb->data, p->courtesy_target);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MCC,
			      H2C_FUNC_ADD_MCC, 0, 0,
			      H2C_ADD_MCC_LEN);

	cond = RTW89_MCC_WAIT_COND(p->group, H2C_FUNC_ADD_MCC);
	return rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
}

#define H2C_START_MCC_LEN 12
int rtw89_fw_h2c_start_mcc(struct rtw89_dev *rtwdev,
			   const struct rtw89_fw_mcc_start_req *p)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	struct sk_buff *skb;
	unsigned int cond;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_START_MCC_LEN);
	if (!skb) {
		rtw89_err(rtwdev,
			  "failed to alloc skb for start mcc\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_START_MCC_LEN);
	RTW89_SET_FWCMD_START_MCC_GROUP(skb->data, p->group);
	RTW89_SET_FWCMD_START_MCC_BTC_IN_GROUP(skb->data, p->btc_in_group);
	RTW89_SET_FWCMD_START_MCC_OLD_GROUP_ACTION(skb->data, p->old_group_action);
	RTW89_SET_FWCMD_START_MCC_OLD_GROUP(skb->data, p->old_group);
	RTW89_SET_FWCMD_START_MCC_NOTIFY_CNT(skb->data, p->notify_cnt);
	RTW89_SET_FWCMD_START_MCC_NOTIFY_RXDBG_EN(skb->data, p->notify_rxdbg_en);
	RTW89_SET_FWCMD_START_MCC_MACID(skb->data, p->macid);
	RTW89_SET_FWCMD_START_MCC_TSF_LOW(skb->data, p->tsf_low);
	RTW89_SET_FWCMD_START_MCC_TSF_HIGH(skb->data, p->tsf_high);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MCC,
			      H2C_FUNC_START_MCC, 0, 0,
			      H2C_START_MCC_LEN);

	cond = RTW89_MCC_WAIT_COND(p->group, H2C_FUNC_START_MCC);
	return rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
}

#define H2C_STOP_MCC_LEN 4
int rtw89_fw_h2c_stop_mcc(struct rtw89_dev *rtwdev, u8 group, u8 macid,
			  bool prev_groups)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	struct sk_buff *skb;
	unsigned int cond;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_STOP_MCC_LEN);
	if (!skb) {
		rtw89_err(rtwdev,
			  "failed to alloc skb for stop mcc\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_STOP_MCC_LEN);
	RTW89_SET_FWCMD_STOP_MCC_MACID(skb->data, macid);
	RTW89_SET_FWCMD_STOP_MCC_GROUP(skb->data, group);
	RTW89_SET_FWCMD_STOP_MCC_PREV_GROUPS(skb->data, prev_groups);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MCC,
			      H2C_FUNC_STOP_MCC, 0, 0,
			      H2C_STOP_MCC_LEN);

	cond = RTW89_MCC_WAIT_COND(group, H2C_FUNC_STOP_MCC);
	return rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
}

#define H2C_DEL_MCC_GROUP_LEN 4
int rtw89_fw_h2c_del_mcc_group(struct rtw89_dev *rtwdev, u8 group,
			       bool prev_groups)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	struct sk_buff *skb;
	unsigned int cond;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_DEL_MCC_GROUP_LEN);
	if (!skb) {
		rtw89_err(rtwdev,
			  "failed to alloc skb for del mcc group\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_DEL_MCC_GROUP_LEN);
	RTW89_SET_FWCMD_DEL_MCC_GROUP_GROUP(skb->data, group);
	RTW89_SET_FWCMD_DEL_MCC_GROUP_PREV_GROUPS(skb->data, prev_groups);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MCC,
			      H2C_FUNC_DEL_MCC_GROUP, 0, 0,
			      H2C_DEL_MCC_GROUP_LEN);

	cond = RTW89_MCC_WAIT_COND(group, H2C_FUNC_DEL_MCC_GROUP);
	return rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
}

#define H2C_RESET_MCC_GROUP_LEN 4
int rtw89_fw_h2c_reset_mcc_group(struct rtw89_dev *rtwdev, u8 group)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	struct sk_buff *skb;
	unsigned int cond;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_RESET_MCC_GROUP_LEN);
	if (!skb) {
		rtw89_err(rtwdev,
			  "failed to alloc skb for reset mcc group\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_RESET_MCC_GROUP_LEN);
	RTW89_SET_FWCMD_RESET_MCC_GROUP_GROUP(skb->data, group);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MCC,
			      H2C_FUNC_RESET_MCC_GROUP, 0, 0,
			      H2C_RESET_MCC_GROUP_LEN);

	cond = RTW89_MCC_WAIT_COND(group, H2C_FUNC_RESET_MCC_GROUP);
	return rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
}

#define H2C_MCC_REQ_TSF_LEN 4
int rtw89_fw_h2c_mcc_req_tsf(struct rtw89_dev *rtwdev,
			     const struct rtw89_fw_mcc_tsf_req *req,
			     struct rtw89_mac_mcc_tsf_rpt *rpt)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	struct rtw89_mac_mcc_tsf_rpt *tmp;
	struct sk_buff *skb;
	unsigned int cond;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_MCC_REQ_TSF_LEN);
	if (!skb) {
		rtw89_err(rtwdev,
			  "failed to alloc skb for mcc req tsf\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_MCC_REQ_TSF_LEN);
	RTW89_SET_FWCMD_MCC_REQ_TSF_GROUP(skb->data, req->group);
	RTW89_SET_FWCMD_MCC_REQ_TSF_MACID_X(skb->data, req->macid_x);
	RTW89_SET_FWCMD_MCC_REQ_TSF_MACID_Y(skb->data, req->macid_y);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MCC,
			      H2C_FUNC_MCC_REQ_TSF, 0, 0,
			      H2C_MCC_REQ_TSF_LEN);

	cond = RTW89_MCC_WAIT_COND(req->group, H2C_FUNC_MCC_REQ_TSF);
	ret = rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
	if (ret)
		return ret;

	tmp = (struct rtw89_mac_mcc_tsf_rpt *)wait->data.buf;
	*rpt = *tmp;

	return 0;
}

#define H2C_MCC_MACID_BITMAP_DSC_LEN 4
int rtw89_fw_h2c_mcc_macid_bitmap(struct rtw89_dev *rtwdev, u8 group, u8 macid,
				  u8 *bitmap)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	struct sk_buff *skb;
	unsigned int cond;
	u8 map_len;
	u8 h2c_len;

	BUILD_BUG_ON(RTW89_MAX_MAC_ID_NUM % 8);
	map_len = RTW89_MAX_MAC_ID_NUM / 8;
	h2c_len = H2C_MCC_MACID_BITMAP_DSC_LEN + map_len;
	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, h2c_len);
	if (!skb) {
		rtw89_err(rtwdev,
			  "failed to alloc skb for mcc macid bitmap\n");
		return -ENOMEM;
	}

	skb_put(skb, h2c_len);
	RTW89_SET_FWCMD_MCC_MACID_BITMAP_GROUP(skb->data, group);
	RTW89_SET_FWCMD_MCC_MACID_BITMAP_MACID(skb->data, macid);
	RTW89_SET_FWCMD_MCC_MACID_BITMAP_BITMAP_LENGTH(skb->data, map_len);
	RTW89_SET_FWCMD_MCC_MACID_BITMAP_BITMAP(skb->data, bitmap, map_len);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MCC,
			      H2C_FUNC_MCC_MACID_BITMAP, 0, 0,
			      h2c_len);

	cond = RTW89_MCC_WAIT_COND(group, H2C_FUNC_MCC_MACID_BITMAP);
	return rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
}

#define H2C_MCC_SYNC_LEN 4
int rtw89_fw_h2c_mcc_sync(struct rtw89_dev *rtwdev, u8 group, u8 source,
			  u8 target, u8 offset)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	struct sk_buff *skb;
	unsigned int cond;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_MCC_SYNC_LEN);
	if (!skb) {
		rtw89_err(rtwdev,
			  "failed to alloc skb for mcc sync\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_MCC_SYNC_LEN);
	RTW89_SET_FWCMD_MCC_SYNC_GROUP(skb->data, group);
	RTW89_SET_FWCMD_MCC_SYNC_MACID_SOURCE(skb->data, source);
	RTW89_SET_FWCMD_MCC_SYNC_MACID_TARGET(skb->data, target);
	RTW89_SET_FWCMD_MCC_SYNC_SYNC_OFFSET(skb->data, offset);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MCC,
			      H2C_FUNC_MCC_SYNC, 0, 0,
			      H2C_MCC_SYNC_LEN);

	cond = RTW89_MCC_WAIT_COND(group, H2C_FUNC_MCC_SYNC);
	return rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
}

#define H2C_MCC_SET_DURATION_LEN 20
int rtw89_fw_h2c_mcc_set_duration(struct rtw89_dev *rtwdev,
				  const struct rtw89_fw_mcc_duration *p)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	struct sk_buff *skb;
	unsigned int cond;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_MCC_SET_DURATION_LEN);
	if (!skb) {
		rtw89_err(rtwdev,
			  "failed to alloc skb for mcc set duration\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_MCC_SET_DURATION_LEN);
	RTW89_SET_FWCMD_MCC_SET_DURATION_GROUP(skb->data, p->group);
	RTW89_SET_FWCMD_MCC_SET_DURATION_BTC_IN_GROUP(skb->data, p->btc_in_group);
	RTW89_SET_FWCMD_MCC_SET_DURATION_START_MACID(skb->data, p->start_macid);
	RTW89_SET_FWCMD_MCC_SET_DURATION_MACID_X(skb->data, p->macid_x);
	RTW89_SET_FWCMD_MCC_SET_DURATION_MACID_Y(skb->data, p->macid_y);
	RTW89_SET_FWCMD_MCC_SET_DURATION_START_TSF_LOW(skb->data,
						       p->start_tsf_low);
	RTW89_SET_FWCMD_MCC_SET_DURATION_START_TSF_HIGH(skb->data,
							p->start_tsf_high);
	RTW89_SET_FWCMD_MCC_SET_DURATION_DURATION_X(skb->data, p->duration_x);
	RTW89_SET_FWCMD_MCC_SET_DURATION_DURATION_Y(skb->data, p->duration_y);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MCC,
			      H2C_FUNC_MCC_SET_DURATION, 0, 0,
			      H2C_MCC_SET_DURATION_LEN);

	cond = RTW89_MCC_WAIT_COND(p->group, H2C_FUNC_MCC_SET_DURATION);
	return rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
}

static
u32 rtw89_fw_h2c_mrc_add_slot(struct rtw89_dev *rtwdev,
			      const struct rtw89_fw_mrc_add_slot_arg *slot_arg,
			      struct rtw89_h2c_mrc_add_slot *slot_h2c)
{
	bool fill_h2c = !!slot_h2c;
	unsigned int i;

	if (!fill_h2c)
		goto calc_len;

	slot_h2c->w0 = le32_encode_bits(slot_arg->duration,
					RTW89_H2C_MRC_ADD_SLOT_W0_DURATION) |
		       le32_encode_bits(slot_arg->courtesy_en,
					RTW89_H2C_MRC_ADD_SLOT_W0_COURTESY_EN) |
		       le32_encode_bits(slot_arg->role_num,
					RTW89_H2C_MRC_ADD_SLOT_W0_ROLE_NUM);
	slot_h2c->w1 = le32_encode_bits(slot_arg->courtesy_period,
					RTW89_H2C_MRC_ADD_SLOT_W1_COURTESY_PERIOD) |
		       le32_encode_bits(slot_arg->courtesy_target,
					RTW89_H2C_MRC_ADD_SLOT_W1_COURTESY_TARGET);

	for (i = 0; i < slot_arg->role_num; i++) {
		slot_h2c->roles[i].w0 =
			le32_encode_bits(slot_arg->roles[i].macid,
					 RTW89_H2C_MRC_ADD_ROLE_W0_MACID) |
			le32_encode_bits(slot_arg->roles[i].role_type,
					 RTW89_H2C_MRC_ADD_ROLE_W0_ROLE_TYPE) |
			le32_encode_bits(slot_arg->roles[i].is_master,
					 RTW89_H2C_MRC_ADD_ROLE_W0_IS_MASTER) |
			le32_encode_bits(slot_arg->roles[i].en_tx_null,
					 RTW89_H2C_MRC_ADD_ROLE_W0_TX_NULL_EN) |
			le32_encode_bits(false,
					 RTW89_H2C_MRC_ADD_ROLE_W0_IS_ALT_ROLE) |
			le32_encode_bits(false,
					 RTW89_H2C_MRC_ADD_ROLE_W0_ROLE_ALT_EN);
		slot_h2c->roles[i].w1 =
			le32_encode_bits(slot_arg->roles[i].central_ch,
					 RTW89_H2C_MRC_ADD_ROLE_W1_CENTRAL_CH_SEG) |
			le32_encode_bits(slot_arg->roles[i].primary_ch,
					 RTW89_H2C_MRC_ADD_ROLE_W1_PRI_CH) |
			le32_encode_bits(slot_arg->roles[i].bw,
					 RTW89_H2C_MRC_ADD_ROLE_W1_BW) |
			le32_encode_bits(slot_arg->roles[i].band,
					 RTW89_H2C_MRC_ADD_ROLE_W1_CH_BAND_TYPE) |
			le32_encode_bits(slot_arg->roles[i].null_early,
					 RTW89_H2C_MRC_ADD_ROLE_W1_NULL_EARLY) |
			le32_encode_bits(false,
					 RTW89_H2C_MRC_ADD_ROLE_W1_RFK_BY_PASS) |
			le32_encode_bits(true,
					 RTW89_H2C_MRC_ADD_ROLE_W1_CAN_BTC);
		slot_h2c->roles[i].macid_main_bitmap =
			cpu_to_le32(slot_arg->roles[i].macid_main_bitmap);
		slot_h2c->roles[i].macid_paired_bitmap =
			cpu_to_le32(slot_arg->roles[i].macid_paired_bitmap);
	}

calc_len:
	return struct_size(slot_h2c, roles, slot_arg->role_num);
}

int rtw89_fw_h2c_mrc_add(struct rtw89_dev *rtwdev,
			 const struct rtw89_fw_mrc_add_arg *arg)
{
	struct rtw89_h2c_mrc_add *h2c_head;
	struct sk_buff *skb;
	unsigned int i;
	void *tmp;
	u32 len;
	int ret;

	len = sizeof(*h2c_head);
	for (i = 0; i < arg->slot_num; i++)
		len += rtw89_fw_h2c_mrc_add_slot(rtwdev, &arg->slots[i], NULL);

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for mrc add\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	tmp = skb->data;

	h2c_head = tmp;
	h2c_head->w0 = le32_encode_bits(arg->sch_idx,
					RTW89_H2C_MRC_ADD_W0_SCH_IDX) |
		       le32_encode_bits(arg->sch_type,
					RTW89_H2C_MRC_ADD_W0_SCH_TYPE) |
		       le32_encode_bits(arg->slot_num,
					RTW89_H2C_MRC_ADD_W0_SLOT_NUM) |
		       le32_encode_bits(arg->btc_in_sch,
					RTW89_H2C_MRC_ADD_W0_BTC_IN_SCH);

	tmp += sizeof(*h2c_head);
	for (i = 0; i < arg->slot_num; i++)
		tmp += rtw89_fw_h2c_mrc_add_slot(rtwdev, &arg->slots[i], tmp);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MRC,
			      H2C_FUNC_ADD_MRC, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		dev_kfree_skb_any(skb);
		return -EBUSY;
	}

	return 0;
}

int rtw89_fw_h2c_mrc_start(struct rtw89_dev *rtwdev,
			   const struct rtw89_fw_mrc_start_arg *arg)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	struct rtw89_h2c_mrc_start *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	unsigned int cond;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for mrc start\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_mrc_start *)skb->data;

	h2c->w0 = le32_encode_bits(arg->sch_idx,
				   RTW89_H2C_MRC_START_W0_SCH_IDX) |
		  le32_encode_bits(arg->old_sch_idx,
				   RTW89_H2C_MRC_START_W0_OLD_SCH_IDX) |
		  le32_encode_bits(arg->action,
				   RTW89_H2C_MRC_START_W0_ACTION);

	h2c->start_tsf_high = cpu_to_le32(arg->start_tsf >> 32);
	h2c->start_tsf_low = cpu_to_le32(arg->start_tsf);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MRC,
			      H2C_FUNC_START_MRC, 0, 0,
			      len);

	cond = RTW89_MRC_WAIT_COND(arg->sch_idx, H2C_FUNC_START_MRC);
	return rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
}

int rtw89_fw_h2c_mrc_del(struct rtw89_dev *rtwdev, u8 sch_idx, u8 slot_idx)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	struct rtw89_h2c_mrc_del *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	unsigned int cond;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for mrc del\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_mrc_del *)skb->data;

	h2c->w0 = le32_encode_bits(sch_idx, RTW89_H2C_MRC_DEL_W0_SCH_IDX) |
		  le32_encode_bits(slot_idx, RTW89_H2C_MRC_DEL_W0_STOP_SLOT_IDX);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MRC,
			      H2C_FUNC_DEL_MRC, 0, 0,
			      len);

	cond = RTW89_MRC_WAIT_COND(sch_idx, H2C_FUNC_DEL_MRC);
	return rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
}

int rtw89_fw_h2c_mrc_req_tsf(struct rtw89_dev *rtwdev,
			     const struct rtw89_fw_mrc_req_tsf_arg *arg,
			     struct rtw89_mac_mrc_tsf_rpt *rpt)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	struct rtw89_h2c_mrc_req_tsf *h2c;
	struct rtw89_mac_mrc_tsf_rpt *tmp;
	struct sk_buff *skb;
	unsigned int i;
	u32 len;
	int ret;

	len = struct_size(h2c, infos, arg->num);
	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for mrc req tsf\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_mrc_req_tsf *)skb->data;

	h2c->req_tsf_num = arg->num;
	for (i = 0; i < arg->num; i++)
		h2c->infos[i] =
			u8_encode_bits(arg->infos[i].band,
				       RTW89_H2C_MRC_REQ_TSF_INFO_BAND) |
			u8_encode_bits(arg->infos[i].port,
				       RTW89_H2C_MRC_REQ_TSF_INFO_PORT);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MRC,
			      H2C_FUNC_MRC_REQ_TSF, 0, 0,
			      len);

	ret = rtw89_h2c_tx_and_wait(rtwdev, skb, wait, RTW89_MRC_WAIT_COND_REQ_TSF);
	if (ret)
		return ret;

	tmp = (struct rtw89_mac_mrc_tsf_rpt *)wait->data.buf;
	*rpt = *tmp;

	return 0;
}

int rtw89_fw_h2c_mrc_upd_bitmap(struct rtw89_dev *rtwdev,
				const struct rtw89_fw_mrc_upd_bitmap_arg *arg)
{
	struct rtw89_h2c_mrc_upd_bitmap *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for mrc upd bitmap\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_mrc_upd_bitmap *)skb->data;

	h2c->w0 = le32_encode_bits(arg->sch_idx,
				   RTW89_H2C_MRC_UPD_BITMAP_W0_SCH_IDX) |
		  le32_encode_bits(arg->action,
				   RTW89_H2C_MRC_UPD_BITMAP_W0_ACTION) |
		  le32_encode_bits(arg->macid,
				   RTW89_H2C_MRC_UPD_BITMAP_W0_MACID);
	h2c->w1 = le32_encode_bits(arg->client_macid,
				   RTW89_H2C_MRC_UPD_BITMAP_W1_CLIENT_MACID);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MRC,
			      H2C_FUNC_MRC_UPD_BITMAP, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		dev_kfree_skb_any(skb);
		return -EBUSY;
	}

	return 0;
}

int rtw89_fw_h2c_mrc_sync(struct rtw89_dev *rtwdev,
			  const struct rtw89_fw_mrc_sync_arg *arg)
{
	struct rtw89_h2c_mrc_sync *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for mrc sync\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_mrc_sync *)skb->data;

	h2c->w0 = le32_encode_bits(true, RTW89_H2C_MRC_SYNC_W0_SYNC_EN) |
		  le32_encode_bits(arg->src.port,
				   RTW89_H2C_MRC_SYNC_W0_SRC_PORT) |
		  le32_encode_bits(arg->src.band,
				   RTW89_H2C_MRC_SYNC_W0_SRC_BAND) |
		  le32_encode_bits(arg->dest.port,
				   RTW89_H2C_MRC_SYNC_W0_DEST_PORT) |
		  le32_encode_bits(arg->dest.band,
				   RTW89_H2C_MRC_SYNC_W0_DEST_BAND);
	h2c->w1 = le32_encode_bits(arg->offset, RTW89_H2C_MRC_SYNC_W1_OFFSET);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MRC,
			      H2C_FUNC_MRC_SYNC, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		dev_kfree_skb_any(skb);
		return -EBUSY;
	}

	return 0;
}

int rtw89_fw_h2c_mrc_upd_duration(struct rtw89_dev *rtwdev,
				  const struct rtw89_fw_mrc_upd_duration_arg *arg)
{
	struct rtw89_h2c_mrc_upd_duration *h2c;
	struct sk_buff *skb;
	unsigned int i;
	u32 len;
	int ret;

	len = struct_size(h2c, slots, arg->slot_num);
	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for mrc upd duration\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_mrc_upd_duration *)skb->data;

	h2c->w0 = le32_encode_bits(arg->sch_idx,
				   RTW89_H2C_MRC_UPD_DURATION_W0_SCH_IDX) |
		  le32_encode_bits(arg->slot_num,
				   RTW89_H2C_MRC_UPD_DURATION_W0_SLOT_NUM) |
		  le32_encode_bits(false,
				   RTW89_H2C_MRC_UPD_DURATION_W0_BTC_IN_SCH);

	h2c->start_tsf_high = cpu_to_le32(arg->start_tsf >> 32);
	h2c->start_tsf_low = cpu_to_le32(arg->start_tsf);

	for (i = 0; i < arg->slot_num; i++) {
		h2c->slots[i] =
			le32_encode_bits(arg->slots[i].slot_idx,
					 RTW89_H2C_MRC_UPD_DURATION_SLOT_SLOT_IDX) |
			le32_encode_bits(arg->slots[i].duration,
					 RTW89_H2C_MRC_UPD_DURATION_SLOT_DURATION);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MRC,
			      H2C_FUNC_MRC_UPD_DURATION, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		dev_kfree_skb_any(skb);
		return -EBUSY;
	}

	return 0;
}

static int rtw89_fw_h2c_ap_info(struct rtw89_dev *rtwdev, bool en)
{
	struct rtw89_h2c_ap_info *h2c;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for ap info\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_ap_info *)skb->data;

	h2c->w0 = le32_encode_bits(en, RTW89_H2C_AP_INFO_W0_PWR_INT_EN);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_AP,
			      H2C_FUNC_AP_INFO, 0, 0,
			      len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		dev_kfree_skb_any(skb);
		return -EBUSY;
	}

	return 0;
}

int rtw89_fw_h2c_ap_info_refcount(struct rtw89_dev *rtwdev, bool en)
{
	int ret;

	if (en) {
		if (refcount_inc_not_zero(&rtwdev->refcount_ap_info))
			return 0;
	} else {
		if (!refcount_dec_and_test(&rtwdev->refcount_ap_info))
			return 0;
	}

	ret = rtw89_fw_h2c_ap_info(rtwdev, en);
	if (ret) {
		if (!test_bit(RTW89_FLAG_SER_HANDLING, rtwdev->flags))
			return ret;

		/* During recovery, neither driver nor stack has full error
		 * handling, so show a warning, but return 0 with refcount
		 * increased normally. It can avoid underflow when calling
		 * with @en == false later.
		 */
		rtw89_warn(rtwdev, "h2c ap_info failed during SER\n");
	}

	if (en)
		refcount_set(&rtwdev->refcount_ap_info, 1);

	return 0;
}

int rtw89_fw_h2c_mlo_link_cfg(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			      bool enable)
{
	struct rtw89_wait_info *wait = &rtwdev->mlo.wait;
	struct rtw89_h2c_mlo_link_cfg *h2c;
	u8 mac_id = rtwvif_link->mac_id;
	u32 len = sizeof(*h2c);
	struct sk_buff *skb;
	unsigned int cond;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for mlo link cfg\n");
		return -ENOMEM;
	}

	skb_put(skb, len);
	h2c = (struct rtw89_h2c_mlo_link_cfg *)skb->data;

	h2c->w0 = le32_encode_bits(mac_id, RTW89_H2C_MLO_LINK_CFG_W0_MACID) |
		  le32_encode_bits(enable, RTW89_H2C_MLO_LINK_CFG_W0_OPTION);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MLO,
			      H2C_FUNC_MLO_LINK_CFG, 0, 0,
			      len);

	cond = RTW89_MLO_WAIT_COND(mac_id, H2C_FUNC_MLO_LINK_CFG);

	ret = rtw89_h2c_tx_and_wait(rtwdev, skb, wait, cond);
	if (ret) {
		rtw89_err(rtwdev, "mlo link cfg (%s link id %u) failed: %d\n",
			  str_enable_disable(enable), rtwvif_link->link_id, ret);
		return ret;
	}

	return 0;
}

static bool __fw_txpwr_entry_zero_ext(const void *ext_ptr, u8 ext_len)
{
	static const u8 zeros[U8_MAX] = {};

	return memcmp(ext_ptr, zeros, ext_len) == 0;
}

#define __fw_txpwr_entry_acceptable(e, cursor, ent_sz)	\
({							\
	u8 __var_sz = sizeof(*(e));			\
	bool __accept;					\
	if (__var_sz >= (ent_sz))			\
		__accept = true;			\
	else						\
		__accept = __fw_txpwr_entry_zero_ext((cursor) + __var_sz,\
						     (ent_sz) - __var_sz);\
	__accept;					\
})

static bool
fw_txpwr_byrate_entry_valid(const struct rtw89_fw_txpwr_byrate_entry *e,
			    const void *cursor,
			    const struct rtw89_txpwr_conf *conf)
{
	if (!__fw_txpwr_entry_acceptable(e, cursor, conf->ent_sz))
		return false;

	if (e->band >= RTW89_BAND_NUM || e->bw >= RTW89_BYR_BW_NUM)
		return false;

	switch (e->rs) {
	case RTW89_RS_CCK:
		if (e->shf + e->len > RTW89_RATE_CCK_NUM)
			return false;
		break;
	case RTW89_RS_OFDM:
		if (e->shf + e->len > RTW89_RATE_OFDM_NUM)
			return false;
		break;
	case RTW89_RS_MCS:
		if (e->shf + e->len > __RTW89_RATE_MCS_NUM ||
		    e->nss >= RTW89_NSS_NUM ||
		    e->ofdma >= RTW89_OFDMA_NUM)
			return false;
		break;
	case RTW89_RS_HEDCM:
		if (e->shf + e->len > RTW89_RATE_HEDCM_NUM ||
		    e->nss >= RTW89_NSS_HEDCM_NUM ||
		    e->ofdma >= RTW89_OFDMA_NUM)
			return false;
		break;
	case RTW89_RS_OFFSET:
		if (e->shf + e->len > __RTW89_RATE_OFFSET_NUM)
			return false;
		break;
	default:
		return false;
	}

	return true;
}

static
void rtw89_fw_load_txpwr_byrate(struct rtw89_dev *rtwdev,
				const struct rtw89_txpwr_table *tbl)
{
	const struct rtw89_txpwr_conf *conf = tbl->data;
	struct rtw89_fw_txpwr_byrate_entry entry = {};
	struct rtw89_txpwr_byrate *byr_head;
	struct rtw89_rate_desc desc = {};
	const void *cursor;
	u32 data;
	s8 *byr;
	int i;

	rtw89_for_each_in_txpwr_conf(entry, cursor, conf) {
		if (!fw_txpwr_byrate_entry_valid(&entry, cursor, conf))
			continue;

		byr_head = &rtwdev->byr[entry.band][entry.bw];
		data = le32_to_cpu(entry.data);
		desc.ofdma = entry.ofdma;
		desc.nss = entry.nss;
		desc.rs = entry.rs;

		for (i = 0; i < entry.len; i++, data >>= 8) {
			desc.idx = entry.shf + i;
			byr = rtw89_phy_raw_byr_seek(rtwdev, byr_head, &desc);
			*byr = data & 0xff;
		}
	}
}

static bool
fw_txpwr_lmt_2ghz_entry_valid(const struct rtw89_fw_txpwr_lmt_2ghz_entry *e,
			      const void *cursor,
			      const struct rtw89_txpwr_conf *conf)
{
	if (!__fw_txpwr_entry_acceptable(e, cursor, conf->ent_sz))
		return false;

	if (e->bw >= RTW89_2G_BW_NUM)
		return false;
	if (e->nt >= RTW89_NTX_NUM)
		return false;
	if (e->rs >= RTW89_RS_LMT_NUM)
		return false;
	if (e->bf >= RTW89_BF_NUM)
		return false;
	if (e->regd >= RTW89_REGD_NUM)
		return false;
	if (e->ch_idx >= RTW89_2G_CH_NUM)
		return false;

	return true;
}

static
void rtw89_fw_load_txpwr_lmt_2ghz(struct rtw89_txpwr_lmt_2ghz_data *data)
{
	const struct rtw89_txpwr_conf *conf = &data->conf;
	struct rtw89_fw_txpwr_lmt_2ghz_entry entry = {};
	const void *cursor;

	rtw89_for_each_in_txpwr_conf(entry, cursor, conf) {
		if (!fw_txpwr_lmt_2ghz_entry_valid(&entry, cursor, conf))
			continue;

		data->v[entry.bw][entry.nt][entry.rs][entry.bf][entry.regd]
		       [entry.ch_idx] = entry.v;
	}
}

static bool
fw_txpwr_lmt_5ghz_entry_valid(const struct rtw89_fw_txpwr_lmt_5ghz_entry *e,
			      const void *cursor,
			      const struct rtw89_txpwr_conf *conf)
{
	if (!__fw_txpwr_entry_acceptable(e, cursor, conf->ent_sz))
		return false;

	if (e->bw >= RTW89_5G_BW_NUM)
		return false;
	if (e->nt >= RTW89_NTX_NUM)
		return false;
	if (e->rs >= RTW89_RS_LMT_NUM)
		return false;
	if (e->bf >= RTW89_BF_NUM)
		return false;
	if (e->regd >= RTW89_REGD_NUM)
		return false;
	if (e->ch_idx >= RTW89_5G_CH_NUM)
		return false;

	return true;
}

static
void rtw89_fw_load_txpwr_lmt_5ghz(struct rtw89_txpwr_lmt_5ghz_data *data)
{
	const struct rtw89_txpwr_conf *conf = &data->conf;
	struct rtw89_fw_txpwr_lmt_5ghz_entry entry = {};
	const void *cursor;

	rtw89_for_each_in_txpwr_conf(entry, cursor, conf) {
		if (!fw_txpwr_lmt_5ghz_entry_valid(&entry, cursor, conf))
			continue;

		data->v[entry.bw][entry.nt][entry.rs][entry.bf][entry.regd]
		       [entry.ch_idx] = entry.v;
	}
}

static bool
fw_txpwr_lmt_6ghz_entry_valid(const struct rtw89_fw_txpwr_lmt_6ghz_entry *e,
			      const void *cursor,
			      const struct rtw89_txpwr_conf *conf)
{
	if (!__fw_txpwr_entry_acceptable(e, cursor, conf->ent_sz))
		return false;

	if (e->bw >= RTW89_6G_BW_NUM)
		return false;
	if (e->nt >= RTW89_NTX_NUM)
		return false;
	if (e->rs >= RTW89_RS_LMT_NUM)
		return false;
	if (e->bf >= RTW89_BF_NUM)
		return false;
	if (e->regd >= RTW89_REGD_NUM)
		return false;
	if (e->reg_6ghz_power >= NUM_OF_RTW89_REG_6GHZ_POWER)
		return false;
	if (e->ch_idx >= RTW89_6G_CH_NUM)
		return false;

	return true;
}

static
void rtw89_fw_load_txpwr_lmt_6ghz(struct rtw89_txpwr_lmt_6ghz_data *data)
{
	const struct rtw89_txpwr_conf *conf = &data->conf;
	struct rtw89_fw_txpwr_lmt_6ghz_entry entry = {};
	const void *cursor;

	rtw89_for_each_in_txpwr_conf(entry, cursor, conf) {
		if (!fw_txpwr_lmt_6ghz_entry_valid(&entry, cursor, conf))
			continue;

		data->v[entry.bw][entry.nt][entry.rs][entry.bf][entry.regd]
		       [entry.reg_6ghz_power][entry.ch_idx] = entry.v;
	}
}

static bool
fw_txpwr_lmt_ru_2ghz_entry_valid(const struct rtw89_fw_txpwr_lmt_ru_2ghz_entry *e,
				 const void *cursor,
				 const struct rtw89_txpwr_conf *conf)
{
	if (!__fw_txpwr_entry_acceptable(e, cursor, conf->ent_sz))
		return false;

	if (e->ru >= RTW89_RU_NUM)
		return false;
	if (e->nt >= RTW89_NTX_NUM)
		return false;
	if (e->regd >= RTW89_REGD_NUM)
		return false;
	if (e->ch_idx >= RTW89_2G_CH_NUM)
		return false;

	return true;
}

static
void rtw89_fw_load_txpwr_lmt_ru_2ghz(struct rtw89_txpwr_lmt_ru_2ghz_data *data)
{
	const struct rtw89_txpwr_conf *conf = &data->conf;
	struct rtw89_fw_txpwr_lmt_ru_2ghz_entry entry = {};
	const void *cursor;

	rtw89_for_each_in_txpwr_conf(entry, cursor, conf) {
		if (!fw_txpwr_lmt_ru_2ghz_entry_valid(&entry, cursor, conf))
			continue;

		data->v[entry.ru][entry.nt][entry.regd][entry.ch_idx] = entry.v;
	}
}

static bool
fw_txpwr_lmt_ru_5ghz_entry_valid(const struct rtw89_fw_txpwr_lmt_ru_5ghz_entry *e,
				 const void *cursor,
				 const struct rtw89_txpwr_conf *conf)
{
	if (!__fw_txpwr_entry_acceptable(e, cursor, conf->ent_sz))
		return false;

	if (e->ru >= RTW89_RU_NUM)
		return false;
	if (e->nt >= RTW89_NTX_NUM)
		return false;
	if (e->regd >= RTW89_REGD_NUM)
		return false;
	if (e->ch_idx >= RTW89_5G_CH_NUM)
		return false;

	return true;
}

static
void rtw89_fw_load_txpwr_lmt_ru_5ghz(struct rtw89_txpwr_lmt_ru_5ghz_data *data)
{
	const struct rtw89_txpwr_conf *conf = &data->conf;
	struct rtw89_fw_txpwr_lmt_ru_5ghz_entry entry = {};
	const void *cursor;

	rtw89_for_each_in_txpwr_conf(entry, cursor, conf) {
		if (!fw_txpwr_lmt_ru_5ghz_entry_valid(&entry, cursor, conf))
			continue;

		data->v[entry.ru][entry.nt][entry.regd][entry.ch_idx] = entry.v;
	}
}

static bool
fw_txpwr_lmt_ru_6ghz_entry_valid(const struct rtw89_fw_txpwr_lmt_ru_6ghz_entry *e,
				 const void *cursor,
				 const struct rtw89_txpwr_conf *conf)
{
	if (!__fw_txpwr_entry_acceptable(e, cursor, conf->ent_sz))
		return false;

	if (e->ru >= RTW89_RU_NUM)
		return false;
	if (e->nt >= RTW89_NTX_NUM)
		return false;
	if (e->regd >= RTW89_REGD_NUM)
		return false;
	if (e->reg_6ghz_power >= NUM_OF_RTW89_REG_6GHZ_POWER)
		return false;
	if (e->ch_idx >= RTW89_6G_CH_NUM)
		return false;

	return true;
}

static
void rtw89_fw_load_txpwr_lmt_ru_6ghz(struct rtw89_txpwr_lmt_ru_6ghz_data *data)
{
	const struct rtw89_txpwr_conf *conf = &data->conf;
	struct rtw89_fw_txpwr_lmt_ru_6ghz_entry entry = {};
	const void *cursor;

	rtw89_for_each_in_txpwr_conf(entry, cursor, conf) {
		if (!fw_txpwr_lmt_ru_6ghz_entry_valid(&entry, cursor, conf))
			continue;

		data->v[entry.ru][entry.nt][entry.regd][entry.reg_6ghz_power]
		       [entry.ch_idx] = entry.v;
	}
}

static bool
fw_tx_shape_lmt_entry_valid(const struct rtw89_fw_tx_shape_lmt_entry *e,
			    const void *cursor,
			    const struct rtw89_txpwr_conf *conf)
{
	if (!__fw_txpwr_entry_acceptable(e, cursor, conf->ent_sz))
		return false;

	if (e->band >= RTW89_BAND_NUM)
		return false;
	if (e->tx_shape_rs >= RTW89_RS_TX_SHAPE_NUM)
		return false;
	if (e->regd >= RTW89_REGD_NUM)
		return false;

	return true;
}

static
void rtw89_fw_load_tx_shape_lmt(struct rtw89_tx_shape_lmt_data *data)
{
	const struct rtw89_txpwr_conf *conf = &data->conf;
	struct rtw89_fw_tx_shape_lmt_entry entry = {};
	const void *cursor;

	rtw89_for_each_in_txpwr_conf(entry, cursor, conf) {
		if (!fw_tx_shape_lmt_entry_valid(&entry, cursor, conf))
			continue;

		data->v[entry.band][entry.tx_shape_rs][entry.regd] = entry.v;
	}
}

static bool
fw_tx_shape_lmt_ru_entry_valid(const struct rtw89_fw_tx_shape_lmt_ru_entry *e,
			       const void *cursor,
			       const struct rtw89_txpwr_conf *conf)
{
	if (!__fw_txpwr_entry_acceptable(e, cursor, conf->ent_sz))
		return false;

	if (e->band >= RTW89_BAND_NUM)
		return false;
	if (e->regd >= RTW89_REGD_NUM)
		return false;

	return true;
}

static
void rtw89_fw_load_tx_shape_lmt_ru(struct rtw89_tx_shape_lmt_ru_data *data)
{
	const struct rtw89_txpwr_conf *conf = &data->conf;
	struct rtw89_fw_tx_shape_lmt_ru_entry entry = {};
	const void *cursor;

	rtw89_for_each_in_txpwr_conf(entry, cursor, conf) {
		if (!fw_tx_shape_lmt_ru_entry_valid(&entry, cursor, conf))
			continue;

		data->v[entry.band][entry.regd] = entry.v;
	}
}

static bool rtw89_fw_has_da_txpwr_table(struct rtw89_dev *rtwdev,
					const struct rtw89_rfe_parms *parms)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->support_bands & BIT(NL80211_BAND_2GHZ) &&
	    !(parms->rule_da_2ghz.lmt && parms->rule_da_2ghz.lmt_ru))
		return false;

	if (chip->support_bands & BIT(NL80211_BAND_5GHZ) &&
	    !(parms->rule_da_5ghz.lmt && parms->rule_da_5ghz.lmt_ru))
		return false;

	if (chip->support_bands & BIT(NL80211_BAND_6GHZ) &&
	    !(parms->rule_da_6ghz.lmt && parms->rule_da_6ghz.lmt_ru))
		return false;

	return true;
}

const struct rtw89_rfe_parms *
rtw89_load_rfe_data_from_fw(struct rtw89_dev *rtwdev,
			    const struct rtw89_rfe_parms *init)
{
	struct rtw89_rfe_data *rfe_data = rtwdev->rfe_data;
	struct rtw89_rfe_parms *parms;

	if (!rfe_data)
		return init;

	parms = &rfe_data->rfe_parms;
	if (init)
		*parms = *init;

	if (rtw89_txpwr_conf_valid(&rfe_data->byrate.conf)) {
		rfe_data->byrate.tbl.data = &rfe_data->byrate.conf;
		rfe_data->byrate.tbl.size = 0; /* don't care here */
		rfe_data->byrate.tbl.load = rtw89_fw_load_txpwr_byrate;
		parms->byr_tbl = &rfe_data->byrate.tbl;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->lmt_2ghz.conf)) {
		rtw89_fw_load_txpwr_lmt_2ghz(&rfe_data->lmt_2ghz);
		parms->rule_2ghz.lmt = &rfe_data->lmt_2ghz.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->lmt_5ghz.conf)) {
		rtw89_fw_load_txpwr_lmt_5ghz(&rfe_data->lmt_5ghz);
		parms->rule_5ghz.lmt = &rfe_data->lmt_5ghz.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->lmt_6ghz.conf)) {
		rtw89_fw_load_txpwr_lmt_6ghz(&rfe_data->lmt_6ghz);
		parms->rule_6ghz.lmt = &rfe_data->lmt_6ghz.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->da_lmt_2ghz.conf)) {
		rtw89_fw_load_txpwr_lmt_2ghz(&rfe_data->da_lmt_2ghz);
		parms->rule_da_2ghz.lmt = &rfe_data->da_lmt_2ghz.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->da_lmt_5ghz.conf)) {
		rtw89_fw_load_txpwr_lmt_5ghz(&rfe_data->da_lmt_5ghz);
		parms->rule_da_5ghz.lmt = &rfe_data->da_lmt_5ghz.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->da_lmt_6ghz.conf)) {
		rtw89_fw_load_txpwr_lmt_6ghz(&rfe_data->da_lmt_6ghz);
		parms->rule_da_6ghz.lmt = &rfe_data->da_lmt_6ghz.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->lmt_ru_2ghz.conf)) {
		rtw89_fw_load_txpwr_lmt_ru_2ghz(&rfe_data->lmt_ru_2ghz);
		parms->rule_2ghz.lmt_ru = &rfe_data->lmt_ru_2ghz.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->lmt_ru_5ghz.conf)) {
		rtw89_fw_load_txpwr_lmt_ru_5ghz(&rfe_data->lmt_ru_5ghz);
		parms->rule_5ghz.lmt_ru = &rfe_data->lmt_ru_5ghz.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->lmt_ru_6ghz.conf)) {
		rtw89_fw_load_txpwr_lmt_ru_6ghz(&rfe_data->lmt_ru_6ghz);
		parms->rule_6ghz.lmt_ru = &rfe_data->lmt_ru_6ghz.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->da_lmt_ru_2ghz.conf)) {
		rtw89_fw_load_txpwr_lmt_ru_2ghz(&rfe_data->da_lmt_ru_2ghz);
		parms->rule_da_2ghz.lmt_ru = &rfe_data->da_lmt_ru_2ghz.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->da_lmt_ru_5ghz.conf)) {
		rtw89_fw_load_txpwr_lmt_ru_5ghz(&rfe_data->da_lmt_ru_5ghz);
		parms->rule_da_5ghz.lmt_ru = &rfe_data->da_lmt_ru_5ghz.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->da_lmt_ru_6ghz.conf)) {
		rtw89_fw_load_txpwr_lmt_ru_6ghz(&rfe_data->da_lmt_ru_6ghz);
		parms->rule_da_6ghz.lmt_ru = &rfe_data->da_lmt_ru_6ghz.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->tx_shape_lmt.conf)) {
		rtw89_fw_load_tx_shape_lmt(&rfe_data->tx_shape_lmt);
		parms->tx_shape.lmt = &rfe_data->tx_shape_lmt.v;
	}

	if (rtw89_txpwr_conf_valid(&rfe_data->tx_shape_lmt_ru.conf)) {
		rtw89_fw_load_tx_shape_lmt_ru(&rfe_data->tx_shape_lmt_ru);
		parms->tx_shape.lmt_ru = &rfe_data->tx_shape_lmt_ru.v;
	}

	parms->has_da = rtw89_fw_has_da_txpwr_table(rtwdev, parms);

	return parms;
}
