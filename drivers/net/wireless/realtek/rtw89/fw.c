// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "cam.h"
#include "chan.h"
#include "coex.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"

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

static u8 _fw_get_rdy(struct rtw89_dev *rtwdev)
{
	u8 val = rtw89_read8(rtwdev, R_AX_WCPU_FW_CTRL);

	return FIELD_GET(B_AX_WCPU_FWDL_STS_MASK, val);
}

#define FWDL_WAIT_CNT 400000
int rtw89_fw_check_rdy(struct rtw89_dev *rtwdev)
{
	u8 val;
	int ret;

	ret = read_poll_timeout_atomic(_fw_get_rdy, val,
				       val == RTW89_FWDL_WCPU_FW_INIT_RDY,
				       1, FWDL_WAIT_CNT, false, rtwdev);
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
			return -EBUSY;
		}
	}

	set_bit(RTW89_FLAG_FW_RDY, rtwdev->flags);

	return 0;
}

static int rtw89_fw_hdr_parser(struct rtw89_dev *rtwdev, const u8 *fw, u32 len,
			       struct rtw89_fw_bin_info *info)
{
	struct rtw89_fw_hdr_section_info *section_info;
	const u8 *fw_end = fw + len;
	const u8 *bin;
	u32 i;

	if (!info)
		return -EINVAL;

	info->section_num = GET_FW_HDR_SEC_NUM(fw);
	info->hdr_len = RTW89_FW_HDR_SIZE +
			info->section_num * RTW89_FW_SECTION_HDR_SIZE;

	bin = fw + info->hdr_len;

	/* jump to section header */
	fw += RTW89_FW_HDR_SIZE;
	section_info = info->section_info;
	for (i = 0; i < info->section_num; i++) {
		section_info->len = GET_FWSECTION_HDR_SEC_SIZE(fw);
		if (GET_FWSECTION_HDR_CHECKSUM(fw))
			section_info->len += FWDL_SECTION_CHKSUM_LEN;
		section_info->redl = GET_FWSECTION_HDR_REDL(fw);
		section_info->dladdr =
				GET_FWSECTION_HDR_DL_ADDR(fw) & 0x1fffffff;
		section_info->addr = bin;
		bin += section_info->len;
		fw += RTW89_FW_SECTION_HDR_SIZE;
		section_info++;
	}

	if (fw_end != bin) {
		rtw89_err(rtwdev, "[ERR]fw bin size\n");
		return -EINVAL;
	}

	return 0;
}

static
int rtw89_mfw_recognize(struct rtw89_dev *rtwdev, enum rtw89_fw_type type,
			struct rtw89_fw_suit *fw_suit)
{
	struct rtw89_fw_info *fw_info = &rtwdev->fw;
	const u8 *mfw = fw_info->firmware->data;
	u32 mfw_len = fw_info->firmware->size;
	const struct rtw89_mfw_hdr *mfw_hdr = (const struct rtw89_mfw_hdr *)mfw;
	const struct rtw89_mfw_info *mfw_info;
	int i;

	if (mfw_hdr->sig != RTW89_MFW_SIG) {
		rtw89_debug(rtwdev, RTW89_DBG_FW, "use legacy firmware\n");
		/* legacy firmware support normal type only */
		if (type != RTW89_FW_NORMAL)
			return -EINVAL;
		fw_suit->data = mfw;
		fw_suit->size = mfw_len;
		return 0;
	}

	for (i = 0; i < mfw_hdr->fw_nr; i++) {
		mfw_info = &mfw_hdr->info[i];
		if (mfw_info->cv != rtwdev->hal.cv ||
		    mfw_info->type != type ||
		    mfw_info->mp)
			continue;

		fw_suit->data = mfw + le32_to_cpu(mfw_info->shift);
		fw_suit->size = le32_to_cpu(mfw_info->size);
		return 0;
	}

	rtw89_err(rtwdev, "no suitable firmware found\n");
	return -ENOENT;
}

static void rtw89_fw_update_ver(struct rtw89_dev *rtwdev,
				enum rtw89_fw_type type,
				struct rtw89_fw_suit *fw_suit)
{
	const u8 *hdr = fw_suit->data;

	fw_suit->major_ver = GET_FW_HDR_MAJOR_VERSION(hdr);
	fw_suit->minor_ver = GET_FW_HDR_MINOR_VERSION(hdr);
	fw_suit->sub_ver = GET_FW_HDR_SUBVERSION(hdr);
	fw_suit->sub_idex = GET_FW_HDR_SUBINDEX(hdr);
	fw_suit->build_year = GET_FW_HDR_YEAR(hdr);
	fw_suit->build_mon = GET_FW_HDR_MONTH(hdr);
	fw_suit->build_date = GET_FW_HDR_DATE(hdr);
	fw_suit->build_hour = GET_FW_HDR_HOUR(hdr);
	fw_suit->build_min = GET_FW_HDR_MIN(hdr);
	fw_suit->cmd_ver = GET_FW_HDR_CMD_VERSERION(hdr);

	rtw89_info(rtwdev,
		   "Firmware version %u.%u.%u.%u, cmd version %u, type %u\n",
		   fw_suit->major_ver, fw_suit->minor_ver, fw_suit->sub_ver,
		   fw_suit->sub_idex, fw_suit->cmd_ver, type);
}

static
int __rtw89_fw_recognize(struct rtw89_dev *rtwdev, enum rtw89_fw_type type)
{
	struct rtw89_fw_suit *fw_suit = rtw89_fw_suit_get(rtwdev, type);
	int ret;

	ret = rtw89_mfw_recognize(rtwdev, type, fw_suit);
	if (ret)
		return ret;

	rtw89_fw_update_ver(rtwdev, type, fw_suit);

	return 0;
}

#define __DEF_FW_FEAT_COND(__cond, __op) \
static bool __fw_feat_cond_ ## __cond(u32 suit_ver_code, u32 comp_ver_code) \
{ \
	return suit_ver_code __op comp_ver_code; \
}

__DEF_FW_FEAT_COND(ge, >=); /* greater or equal */
__DEF_FW_FEAT_COND(le, <=); /* less or equal */

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
	__CFG_FW_FEAT(RTL8852A, le, 0, 13, 29, 0, OLD_HT_RA_FORMAT),
	__CFG_FW_FEAT(RTL8852A, ge, 0, 13, 35, 0, SCAN_OFFLOAD),
	__CFG_FW_FEAT(RTL8852A, ge, 0, 13, 35, 0, TX_WAKE),
	__CFG_FW_FEAT(RTL8852A, ge, 0, 13, 36, 0, CRASH_TRIGGER),
	__CFG_FW_FEAT(RTL8852A, ge, 0, 13, 38, 0, PACKET_DROP),
	__CFG_FW_FEAT(RTL8852C, ge, 0, 27, 20, 0, PACKET_DROP),
	__CFG_FW_FEAT(RTL8852C, le, 0, 27, 33, 0, NO_DEEP_PS),
	__CFG_FW_FEAT(RTL8852C, ge, 0, 27, 34, 0, TX_WAKE),
	__CFG_FW_FEAT(RTL8852C, ge, 0, 27, 36, 0, SCAN_OFFLOAD),
	__CFG_FW_FEAT(RTL8852C, ge, 0, 27, 40, 0, CRASH_TRIGGER),
};

static void rtw89_fw_recognize_features(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct __fw_feat_cfg *ent;
	const struct rtw89_fw_suit *fw_suit;
	u32 suit_ver_code;
	int i;

	fw_suit = rtw89_fw_suit_get(rtwdev, RTW89_FW_NORMAL);
	suit_ver_code = RTW89_FW_SUIT_VER_CODE(fw_suit);

	for (i = 0; i < ARRAY_SIZE(fw_feat_tbl); i++) {
		ent = &fw_feat_tbl[i];
		if (chip->chip_id != ent->chip_id)
			continue;

		if (ent->cond(suit_ver_code, ent->ver_code))
			RTW89_SET_FW_FEATURE(ent->feature, &rtwdev->fw);
	}
}

void rtw89_early_fw_feature_recognize(struct device *device,
				      const struct rtw89_chip_info *chip,
				      u32 *early_feat_map)
{
	union {
		struct rtw89_mfw_hdr mfw_hdr;
		u8 fw_hdr[RTW89_FW_HDR_SIZE];
	} buf = {};
	const struct firmware *firmware;
	u32 ver_code;
	int ret;
	int i;

	ret = request_partial_firmware_into_buf(&firmware, chip->fw_name,
						device, &buf, sizeof(buf), 0);
	if (ret) {
		dev_err(device, "failed to early request firmware: %d\n", ret);
		return;
	}

	ver_code = buf.mfw_hdr.sig != RTW89_MFW_SIG ?
		   RTW89_FW_HDR_VER_CODE(&buf.fw_hdr) :
		   RTW89_MFW_HDR_VER_CODE(&buf.mfw_hdr);
	if (!ver_code)
		goto out;

	for (i = 0; i < ARRAY_SIZE(fw_feat_tbl); i++) {
		const struct __fw_feat_cfg *ent = &fw_feat_tbl[i];

		if (chip->chip_id != ent->chip_id)
			continue;

		if (ent->cond(ver_code, ent->ver_code))
			*early_feat_map |= BIT(ent->feature);
	}

out:
	release_firmware(firmware);
}

int rtw89_fw_recognize(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = __rtw89_fw_recognize(rtwdev, RTW89_FW_NORMAL);
	if (ret)
		return ret;

	/* It still works if wowlan firmware isn't existing. */
	__rtw89_fw_recognize(rtwdev, RTW89_FW_WOWLAN);

	rtw89_fw_recognize_features(rtwdev);

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

static int __rtw89_fw_download_hdr(struct rtw89_dev *rtwdev, const u8 *fw, u32 len)
{
	struct sk_buff *skb;
	u32 ret = 0;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw hdr dl\n");
		return -ENOMEM;
	}

	skb_put_data(skb, fw, len);
	SET_FW_HDR_PART_SIZE(skb->data, FWDL_SECTION_PER_PKT_LEN);
	rtw89_h2c_pkt_set_hdr_fwdl(rtwdev, skb, FWCMD_TYPE_H2C,
				   H2C_CAT_MAC, H2C_CL_MAC_FWDL,
				   H2C_FUNC_MAC_FWHDR_DL, len);

	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		ret = -1;
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return ret;
}

static int rtw89_fw_download_hdr(struct rtw89_dev *rtwdev, const u8 *fw, u32 len)
{
	u8 val;
	int ret;

	ret = __rtw89_fw_download_hdr(rtwdev, fw, len);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]FW header download\n");
		return ret;
	}

	ret = read_poll_timeout_atomic(rtw89_read8, val, val & B_AX_FWDL_PATH_RDY,
				       1, FWDL_WAIT_CNT, false,
				       rtwdev, R_AX_WCPU_FW_CTRL);
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
	u32 pkt_len;
	int ret;

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

		ret = rtw89_h2c_tx(rtwdev, skb, true);
		if (ret) {
			rtw89_err(rtwdev, "failed to send h2c\n");
			ret = -1;
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

static int rtw89_fw_download_main(struct rtw89_dev *rtwdev, const u8 *fw,
				  struct rtw89_fw_bin_info *info)
{
	struct rtw89_fw_hdr_section_info *section_info = info->section_info;
	u8 section_num = info->section_num;
	int ret;

	while (section_num--) {
		ret = __rtw89_fw_download_main(rtwdev, section_info);
		if (ret)
			return ret;
		section_info++;
	}

	mdelay(5);

	ret = rtw89_fw_check_rdy(rtwdev);
	if (ret) {
		rtw89_warn(rtwdev, "download firmware fail\n");
		return ret;
	}

	return 0;
}

static void rtw89_fw_prog_cnt_dump(struct rtw89_dev *rtwdev)
{
	u32 val32;
	u16 index;

	rtw89_write32(rtwdev, R_AX_DBG_CTRL,
		      FIELD_PREP(B_AX_DBG_SEL0, FW_PROG_CNTR_DBG_SEL) |
		      FIELD_PREP(B_AX_DBG_SEL1, FW_PROG_CNTR_DBG_SEL));
	rtw89_write32_mask(rtwdev, R_AX_SYS_STATUS1, B_AX_SEL_0XC0_MASK, MAC_DBG_SEL);

	for (index = 0; index < 15; index++) {
		val32 = rtw89_read32(rtwdev, R_AX_DBG_PORT_SEL);
		rtw89_err(rtwdev, "[ERR]fw PC = 0x%x\n", val32);
		fsleep(10);
	}
}

static void rtw89_fw_dl_fail_dump(struct rtw89_dev *rtwdev)
{
	u32 val32;
	u16 val16;

	val32 = rtw89_read32(rtwdev, R_AX_WCPU_FW_CTRL);
	rtw89_err(rtwdev, "[ERR]fwdl 0x1E0 = 0x%x\n", val32);

	val16 = rtw89_read16(rtwdev, R_AX_BOOT_DBG + 2);
	rtw89_err(rtwdev, "[ERR]fwdl 0x83F2 = 0x%x\n", val16);

	rtw89_fw_prog_cnt_dump(rtwdev);
}

int rtw89_fw_download(struct rtw89_dev *rtwdev, enum rtw89_fw_type type)
{
	struct rtw89_fw_info *fw_info = &rtwdev->fw;
	struct rtw89_fw_suit *fw_suit = rtw89_fw_suit_get(rtwdev, type);
	struct rtw89_fw_bin_info info;
	const u8 *fw = fw_suit->data;
	u32 len = fw_suit->size;
	u8 val;
	int ret;

	if (!fw || !len) {
		rtw89_err(rtwdev, "fw type %d isn't recognized\n", type);
		return -ENOENT;
	}

	ret = rtw89_fw_hdr_parser(rtwdev, fw, len, &info);
	if (ret) {
		rtw89_err(rtwdev, "parse fw header fail\n");
		goto fwdl_err;
	}

	ret = read_poll_timeout_atomic(rtw89_read8, val, val & B_AX_H2C_PATH_RDY,
				       1, FWDL_WAIT_CNT, false,
				       rtwdev, R_AX_WCPU_FW_CTRL);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]H2C path ready\n");
		goto fwdl_err;
	}

	ret = rtw89_fw_download_hdr(rtwdev, fw, info.hdr_len);
	if (ret) {
		ret = -EBUSY;
		goto fwdl_err;
	}

	ret = rtw89_fw_download_main(rtwdev, fw, &info);
	if (ret) {
		ret = -EBUSY;
		goto fwdl_err;
	}

	fw_info->h2c_seq = 0;
	fw_info->rec_seq = 0;
	rtwdev->mac.rpwm_seq_num = RPWM_SEQ_NUM_MAX;
	rtwdev->mac.cpwm_seq_num = CPWM_SEQ_NUM_MAX;

	return ret;

fwdl_err:
	rtw89_fw_dl_fail_dump(rtwdev);
	return ret;
}

int rtw89_wait_firmware_completion(struct rtw89_dev *rtwdev)
{
	struct rtw89_fw_info *fw = &rtwdev->fw;

	wait_for_completion(&fw->completion);
	if (!fw->firmware)
		return -EINVAL;

	return 0;
}

static void rtw89_load_firmware_cb(const struct firmware *firmware, void *context)
{
	struct rtw89_fw_info *fw = context;
	struct rtw89_dev *rtwdev = fw->rtwdev;

	if (!firmware || !firmware->data) {
		rtw89_err(rtwdev, "failed to request firmware\n");
		complete_all(&fw->completion);
		return;
	}

	fw->firmware = firmware;
	complete_all(&fw->completion);
}

int rtw89_load_firmware(struct rtw89_dev *rtwdev)
{
	struct rtw89_fw_info *fw = &rtwdev->fw;
	const char *fw_name = rtwdev->chip->fw_name;
	int ret;

	fw->rtwdev = rtwdev;
	init_completion(&fw->completion);

	ret = request_firmware_nowait(THIS_MODULE, true, fw_name, rtwdev->dev,
				      GFP_KERNEL, fw, rtw89_load_firmware_cb);
	if (ret) {
		rtw89_err(rtwdev, "failed to async firmware request\n");
		return ret;
	}

	return 0;
}

void rtw89_unload_firmware(struct rtw89_dev *rtwdev)
{
	struct rtw89_fw_info *fw = &rtwdev->fw;

	rtw89_wait_firmware_completion(rtwdev);

	if (fw->firmware)
		release_firmware(fw->firmware);
}

#define H2C_CAM_LEN 60
int rtw89_fw_h2c_cam(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
		     struct rtw89_sta *rtwsta, const u8 *scan_mac_addr)
{
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_CAM_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_CAM_LEN);
	rtw89_cam_fill_addr_cam_info(rtwdev, rtwvif, rtwsta, scan_mac_addr, skb->data);
	rtw89_cam_fill_bssid_cam_info(rtwdev, rtwvif, rtwsta, skb->data);

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

#define H2C_DCTL_SEC_CAM_LEN 68
int rtw89_fw_h2c_dctl_sec_cam_v1(struct rtw89_dev *rtwdev,
				 struct rtw89_vif *rtwvif,
				 struct rtw89_sta *rtwsta)
{
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_DCTL_SEC_CAM_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for dctl sec cam\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_DCTL_SEC_CAM_LEN);

	rtw89_cam_fill_dctl_sec_cam_info_v1(rtwdev, rtwvif, rtwsta, skb->data);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_DCTLINFO_UD_V1, 0, 0,
			      H2C_DCTL_SEC_CAM_LEN);

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

#define H2C_BA_CAM_LEN 8
int rtw89_fw_h2c_ba_cam(struct rtw89_dev *rtwdev, struct rtw89_sta *rtwsta,
			bool valid, struct ieee80211_ampdu_params *params)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_vif *rtwvif = rtwsta->rtwvif;
	u8 macid = rtwsta->mac_id;
	struct sk_buff *skb;
	u8 entry_idx;
	int ret;

	ret = valid ?
	      rtw89_core_acquire_sta_ba_entry(rtwdev, rtwsta, params->tid, &entry_idx) :
	      rtw89_core_release_sta_ba_entry(rtwdev, rtwsta, params->tid, &entry_idx);
	if (ret) {
		/* it still works even if we don't have static BA CAM, because
		 * hardware can create dynamic BA CAM automatically.
		 */
		rtw89_debug(rtwdev, RTW89_DBG_TXRX,
			    "failed to %s entry tid=%d for h2c ba cam\n",
			    valid ? "alloc" : "free", params->tid);
		return 0;
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_BA_CAM_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c ba cam\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_BA_CAM_LEN);
	SET_BA_CAM_MACID(skb->data, macid);
	if (chip->bacam_v1)
		SET_BA_CAM_ENTRY_IDX_V1(skb->data, entry_idx);
	else
		SET_BA_CAM_ENTRY_IDX(skb->data, entry_idx);
	if (!valid)
		goto end;
	SET_BA_CAM_VALID(skb->data, valid);
	SET_BA_CAM_TID(skb->data, params->tid);
	if (params->buf_size > 64)
		SET_BA_CAM_BMAP_SIZE(skb->data, 4);
	else
		SET_BA_CAM_BMAP_SIZE(skb->data, 0);
	/* If init req is set, hw will set the ssn */
	SET_BA_CAM_INIT_REQ(skb->data, 1);
	SET_BA_CAM_SSN(skb->data, params->ssn);

	if (chip->bacam_v1) {
		SET_BA_CAM_STD_EN(skb->data, 1);
		SET_BA_CAM_BAND(skb->data, rtwvif->mac_idx);
	}

end:
	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_BA_CAM,
			      H2C_FUNC_MAC_BA_CAM, 0, 1,
			      H2C_BA_CAM_LEN);

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

static int rtw89_fw_h2c_init_dynamic_ba_cam_v1(struct rtw89_dev *rtwdev,
					       u8 entry_idx, u8 uid)
{
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_BA_CAM_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for dynamic h2c ba cam\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_BA_CAM_LEN);

	SET_BA_CAM_VALID(skb->data, 1);
	SET_BA_CAM_ENTRY_IDX_V1(skb->data, entry_idx);
	SET_BA_CAM_UID(skb->data, uid);
	SET_BA_CAM_BAND(skb->data, 0);
	SET_BA_CAM_STD_EN(skb->data, 0);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_BA_CAM,
			      H2C_FUNC_MAC_BA_CAM, 0, 1,
			      H2C_BA_CAM_LEN);

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

void rtw89_fw_h2c_init_ba_cam_v1(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 entry_idx = chip->bacam_num;
	u8 uid = 0;
	int i;

	for (i = 0; i < chip->bacam_dynamic_num; i++) {
		rtw89_fw_h2c_init_dynamic_ba_cam_v1(rtwdev, entry_idx, uid);
		entry_idx++;
		uid++;
	}
}

#define H2C_LOG_CFG_LEN 12
int rtw89_fw_h2c_fw_log(struct rtw89_dev *rtwdev, bool enable)
{
	struct sk_buff *skb;
	u32 comp = enable ? BIT(RTW89_FW_LOG_COMP_INIT) | BIT(RTW89_FW_LOG_COMP_TASK) |
			    BIT(RTW89_FW_LOG_COMP_PS) | BIT(RTW89_FW_LOG_COMP_ERROR) : 0;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_LOG_CFG_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw log cfg\n");
		return -ENOMEM;
	}

	skb_put(skb, H2C_LOG_CFG_LEN);
	SET_LOG_CFG_LEVEL(skb->data, RTW89_FW_LOG_LEVEL_SER);
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

#define H2C_GENERAL_PKT_LEN 6
#define H2C_GENERAL_PKT_ID_UND 0xff
int rtw89_fw_h2c_general_pkt(struct rtw89_dev *rtwdev, u8 macid)
{
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_GENERAL_PKT_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_GENERAL_PKT_LEN);
	SET_GENERAL_PKT_MACID(skb->data, macid);
	SET_GENERAL_PKT_PROBRSP_ID(skb->data, H2C_GENERAL_PKT_ID_UND);
	SET_GENERAL_PKT_PSPOLL_ID(skb->data, H2C_GENERAL_PKT_ID_UND);
	SET_GENERAL_PKT_NULL_ID(skb->data, H2C_GENERAL_PKT_ID_UND);
	SET_GENERAL_PKT_QOS_NULL_ID(skb->data, H2C_GENERAL_PKT_ID_UND);
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
			      H2C_FUNC_MAC_LPS_PARM, 0, 1,
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

#define H2C_P2P_ACT_LEN 20
int rtw89_fw_h2c_p2p_act(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			 struct ieee80211_p2p_noa_desc *desc,
			 u8 act, u8 noa_id)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	bool p2p_type_gc = rtwvif->wifi_role == RTW89_WIFI_ROLE_P2P_CLIENT;
	u8 ctwindow_oppps = vif->bss_conf.p2p_noa_attr.oppps_ctwindow;
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

	RTW89_SET_FWCMD_P2P_MACID(cmd, rtwvif->mac_id);
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
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 ntx_path = hal->antenna_tx ? hal->antenna_tx : RF_B;
	u8 map_b = hal->antenna_tx == RF_AB ? 1 : 0;

	SET_CMC_TBL_NTX_PATH_EN(skb->data, ntx_path);
	SET_CMC_TBL_PATH_MAP_A(skb->data, 0);
	SET_CMC_TBL_PATH_MAP_B(skb->data, map_b);
	SET_CMC_TBL_PATH_MAP_C(skb->data, 0);
	SET_CMC_TBL_PATH_MAP_D(skb->data, 0);
}

#define H2C_CMC_TBL_LEN 68
int rtw89_fw_h2c_default_cmac_tbl(struct rtw89_dev *rtwdev,
				  struct rtw89_vif *rtwvif)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct sk_buff *skb;
	u8 macid = rtwvif->mac_id;
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
	if (rtwvif->net_type == RTW89_NET_TYPE_AP_MODE)
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

static void __get_sta_he_pkt_padding(struct rtw89_dev *rtwdev,
				     struct ieee80211_sta *sta, u8 *pads)
{
	bool ppe_th;
	u8 ppe16, ppe8;
	u8 nss = min(sta->deflink.rx_nss, rtwdev->hal.tx_nss) - 1;
	u8 ppe_thres_hdr = sta->deflink.he_cap.ppe_thres[0];
	u8 ru_bitmap;
	u8 n, idx, sh;
	u16 ppe;
	int i;

	if (!sta->deflink.he_cap.has_he)
		return;

	ppe_th = FIELD_GET(IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT,
			   sta->deflink.he_cap.he_cap_elem.phy_cap_info[6]);
	if (!ppe_th) {
		u8 pad;

		pad = FIELD_GET(IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK,
				sta->deflink.he_cap.he_cap_elem.phy_cap_info[9]);

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

		ppe = le16_to_cpu(*((__le16 *)&sta->deflink.he_cap.ppe_thres[idx]));
		ppe16 = (ppe >> sh) & IEEE80211_PPE_THRES_NSS_MASK;
		sh += IEEE80211_PPE_THRES_INFO_PPET_SIZE;
		ppe8 = (ppe >> sh) & IEEE80211_PPE_THRES_NSS_MASK;

		if (ppe16 != 7 && ppe8 == 7)
			pads[i] = 2;
		else if (ppe8 != 7)
			pads[i] = 1;
		else
			pads[i] = 0;
	}
}

int rtw89_fw_h2c_assoc_cmac_tbl(struct rtw89_dev *rtwdev,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_sta *rtwsta = sta_to_rtwsta_safe(sta);
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct sk_buff *skb;
	u8 pads[RTW89_PPE_BW_NUM];
	u8 mac_id = rtwsta ? rtwsta->mac_id : rtwvif->mac_id;
	u16 lowest_rate;
	int ret;

	memset(pads, 0, sizeof(pads));
	if (sta)
		__get_sta_he_pkt_padding(rtwdev, sta, pads);

	if (vif->p2p)
		lowest_rate = RTW89_HW_RATE_OFDM6;
	else if (chan->band_type == RTW89_BAND_2G)
		lowest_rate = RTW89_HW_RATE_CCK1;
	else
		lowest_rate = RTW89_HW_RATE_OFDM6;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_CMC_TBL_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}
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
	SET_CMC_TBL_MULTI_PORT_ID(skb->data, rtwvif->port);
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
	if (sta)
		SET_CMC_TBL_BSR_QUEUE_SIZE_FORMAT(skb->data,
						  sta->deflink.he_cap.has_he);
	if (rtwvif->net_type == RTW89_NET_TYPE_AP_MODE)
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

int rtw89_fw_h2c_txtime_cmac_tbl(struct rtw89_dev *rtwdev,
				 struct rtw89_sta *rtwsta)
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
	SET_CTRL_INFO_MACID(skb->data, rtwsta->mac_id);
	SET_CTRL_INFO_OPERATION(skb->data, 1);
	if (rtwsta->cctl_tx_time) {
		SET_CMC_TBL_AMPDU_TIME_SEL(skb->data, 1);
		SET_CMC_TBL_AMPDU_MAX_TIME(skb->data, rtwsta->ampdu_max_time);
	}
	if (rtwsta->cctl_tx_retry_limit) {
		SET_CMC_TBL_DATA_TXCNT_LMT_SEL(skb->data, 1);
		SET_CMC_TBL_DATA_TX_CNT_LMT(skb->data, rtwsta->data_tx_cnt_lmt);
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

int rtw89_fw_h2c_txpath_cmac_tbl(struct rtw89_dev *rtwdev,
				 struct rtw89_sta *rtwsta)
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
	SET_CTRL_INFO_MACID(skb->data, rtwsta->mac_id);
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

#define H2C_BCN_BASE_LEN 12
int rtw89_fw_h2c_update_beacon(struct rtw89_dev *rtwdev,
			       struct rtw89_vif *rtwvif)
{
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct sk_buff *skb;
	struct sk_buff *skb_beacon;
	u16 tim_offset;
	int bcn_total_len;
	u16 beacon_rate;
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

	bcn_total_len = H2C_BCN_BASE_LEN + skb_beacon->len;
	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, bcn_total_len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		dev_kfree_skb_any(skb_beacon);
		return -ENOMEM;
	}
	skb_put(skb, H2C_BCN_BASE_LEN);

	SET_BCN_UPD_PORT(skb->data, rtwvif->port);
	SET_BCN_UPD_MBSSID(skb->data, 0);
	SET_BCN_UPD_BAND(skb->data, rtwvif->mac_idx);
	SET_BCN_UPD_GRP_IE_OFST(skb->data, tim_offset);
	SET_BCN_UPD_MACID(skb->data, rtwvif->mac_id);
	SET_BCN_UPD_SSN_SEL(skb->data, RTW89_MGMT_HW_SSN_SEL);
	SET_BCN_UPD_SSN_MODE(skb->data, RTW89_MGMT_HW_SEQ_MODE);
	SET_BCN_UPD_RATE(skb->data, beacon_rate);

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

#define H2C_ROLE_MAINTAIN_LEN 4
int rtw89_fw_h2c_role_maintain(struct rtw89_dev *rtwdev,
			       struct rtw89_vif *rtwvif,
			       struct rtw89_sta *rtwsta,
			       enum rtw89_upd_mode upd_mode)
{
	struct sk_buff *skb;
	u8 mac_id = rtwsta ? rtwsta->mac_id : rtwvif->mac_id;
	u8 self_role;
	int ret;

	if (rtwvif->net_type == RTW89_NET_TYPE_AP_MODE) {
		if (rtwsta)
			self_role = RTW89_SELF_ROLE_AP_CLIENT;
		else
			self_role = rtwvif->self_role;
	} else {
		self_role = rtwvif->self_role;
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_ROLE_MAINTAIN_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c join\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_ROLE_MAINTAIN_LEN);
	SET_FWROLE_MAINTAIN_MACID(skb->data, mac_id);
	SET_FWROLE_MAINTAIN_SELF_ROLE(skb->data, self_role);
	SET_FWROLE_MAINTAIN_UPD_MODE(skb->data, upd_mode);
	SET_FWROLE_MAINTAIN_WIFI_ROLE(skb->data, rtwvif->wifi_role);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_MEDIA_RPT,
			      H2C_FUNC_MAC_FWROLE_MAINTAIN, 0, 1,
			      H2C_ROLE_MAINTAIN_LEN);

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

#define H2C_JOIN_INFO_LEN 4
int rtw89_fw_h2c_join_info(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			   struct rtw89_sta *rtwsta, bool dis_conn)
{
	struct sk_buff *skb;
	u8 mac_id = rtwsta ? rtwsta->mac_id : rtwvif->mac_id;
	u8 self_role = rtwvif->self_role;
	u8 net_type = rtwvif->net_type;
	int ret;

	if (net_type == RTW89_NET_TYPE_AP_MODE && rtwsta) {
		self_role = RTW89_SELF_ROLE_AP_CLIENT;
		net_type = dis_conn ? RTW89_NET_TYPE_NO_LINK : net_type;
	}

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_JOIN_INFO_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c join\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_JOIN_INFO_LEN);
	SET_JOININFO_MACID(skb->data, mac_id);
	SET_JOININFO_OP(skb->data, dis_conn);
	SET_JOININFO_BAND(skb->data, rtwvif->mac_idx);
	SET_JOININFO_WMM(skb->data, rtwvif->wmm);
	SET_JOININFO_TGR(skb->data, rtwvif->trigger);
	SET_JOININFO_ISHESTA(skb->data, 0);
	SET_JOININFO_DLBW(skb->data, 0);
	SET_JOININFO_TF_MAC_PAD(skb->data, 0);
	SET_JOININFO_DL_T_PE(skb->data, 0);
	SET_JOININFO_PORT_ID(skb->data, rtwvif->port);
	SET_JOININFO_NET_TYPE(skb->data, net_type);
	SET_JOININFO_WIFI_ROLE(skb->data, rtwvif->wifi_role);
	SET_JOININFO_SELF_ROLE(skb->data, self_role);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_MEDIA_RPT,
			      H2C_FUNC_MAC_JOININFO, 0, 1,
			      H2C_JOIN_INFO_LEN);

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
	struct rtw89_fw_macid_pause_grp h2c = {{0}};
	u8 len = sizeof(struct rtw89_fw_macid_pause_grp);
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_JOIN_INFO_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c join\n");
		return -ENOMEM;
	}
	h2c.mask_grp[grp] = cpu_to_le32(BIT(sh));
	if (pause)
		h2c.pause_grp[grp] = cpu_to_le32(BIT(sh));
	skb_put_data(skb, &h2c, len);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_MAC_MACID_PAUSE, 1, 0,
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
int rtw89_fw_h2c_set_edca(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
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
	RTW89_SET_EDCA_BAND(skb->data, rtwvif->mac_idx);
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
int rtw89_fw_h2c_tsf32_toggle(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
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

	RTW89_SET_FWCMD_TSF32_TOGL_BAND(cmd, rtwvif->mac_idx);
	RTW89_SET_FWCMD_TSF32_TOGL_EN(cmd, en);
	RTW89_SET_FWCMD_TSF32_TOGL_PORT(cmd, rtwvif->port);
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

#define H2C_RA_LEN 16
int rtw89_fw_h2c_ra(struct rtw89_dev *rtwdev, struct rtw89_ra_info *ra, bool csi)
{
	struct sk_buff *skb;
	u8 *cmd;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_RA_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c join\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_RA_LEN);
	cmd = skb->data;
	rtw89_debug(rtwdev, RTW89_DBG_RA,
		    "ra cmd msk: %llx ", ra->ra_mask);

	RTW89_SET_FWCMD_RA_MODE(cmd, ra->mode_ctrl);
	RTW89_SET_FWCMD_RA_BW_CAP(cmd, ra->bw_cap);
	RTW89_SET_FWCMD_RA_MACID(cmd, ra->macid);
	RTW89_SET_FWCMD_RA_DCM(cmd, ra->dcm_cap);
	RTW89_SET_FWCMD_RA_ER(cmd, ra->er_cap);
	RTW89_SET_FWCMD_RA_INIT_RATE_LV(cmd, ra->init_rate_lv);
	RTW89_SET_FWCMD_RA_UPD_ALL(cmd, ra->upd_all);
	RTW89_SET_FWCMD_RA_SGI(cmd, ra->en_sgi);
	RTW89_SET_FWCMD_RA_LDPC(cmd, ra->ldpc_cap);
	RTW89_SET_FWCMD_RA_STBC(cmd, ra->stbc_cap);
	RTW89_SET_FWCMD_RA_SS_NUM(cmd, ra->ss_num);
	RTW89_SET_FWCMD_RA_GILTF(cmd, ra->giltf);
	RTW89_SET_FWCMD_RA_UPD_BW_NSS_MASK(cmd, ra->upd_bw_nss_mask);
	RTW89_SET_FWCMD_RA_UPD_MASK(cmd, ra->upd_mask);
	RTW89_SET_FWCMD_RA_MASK_0(cmd, FIELD_GET(MASKBYTE0, ra->ra_mask));
	RTW89_SET_FWCMD_RA_MASK_1(cmd, FIELD_GET(MASKBYTE1, ra->ra_mask));
	RTW89_SET_FWCMD_RA_MASK_2(cmd, FIELD_GET(MASKBYTE2, ra->ra_mask));
	RTW89_SET_FWCMD_RA_MASK_3(cmd, FIELD_GET(MASKBYTE3, ra->ra_mask));
	RTW89_SET_FWCMD_RA_MASK_4(cmd, FIELD_GET(MASKBYTE4, ra->ra_mask));
	RTW89_SET_FWCMD_RA_FIX_GILTF_EN(cmd, ra->fix_giltf_en);
	RTW89_SET_FWCMD_RA_FIX_GILTF(cmd, ra->fix_giltf);

	if (csi) {
		RTW89_SET_FWCMD_RA_BFEE_CSI_CTL(cmd, 1);
		RTW89_SET_FWCMD_RA_BAND_NUM(cmd, ra->band_num);
		RTW89_SET_FWCMD_RA_CR_TBL_SEL(cmd, ra->cr_tbl_sel);
		RTW89_SET_FWCMD_RA_FIXED_CSI_RATE_EN(cmd, ra->fixed_csi_rate_en);
		RTW89_SET_FWCMD_RA_RA_CSI_RATE_EN(cmd, ra->ra_csi_rate_en);
		RTW89_SET_FWCMD_RA_FIXED_CSI_MCS_SS_IDX(cmd, ra->csi_mcs_ss_idx);
		RTW89_SET_FWCMD_RA_FIXED_CSI_MODE(cmd, ra->csi_mode);
		RTW89_SET_FWCMD_RA_FIXED_CSI_GI_LTF(cmd, ra->csi_gi_ltf);
		RTW89_SET_FWCMD_RA_FIXED_CSI_BW(cmd, ra->csi_bw);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, H2C_CL_OUTSRC_RA,
			      H2C_FUNC_OUTSRC_RA_MACIDCFG, 0, 0,
			      H2C_RA_LEN);

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

#define H2C_LEN_CXDRVHDR 2
#define H2C_LEN_CXDRVINFO_INIT (12 + H2C_LEN_CXDRVHDR)
int rtw89_fw_h2c_cxdrv_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_init_info *init_info = &dm->init_info;
	struct rtw89_btc_module *module = &init_info->module;
	struct rtw89_btc_ant_info *ant = &module->ant;
	struct sk_buff *skb;
	u8 *cmd;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_LEN_CXDRVINFO_INIT);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_init\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_LEN_CXDRVINFO_INIT);
	cmd = skb->data;

	RTW89_SET_FWCMD_CXHDR_TYPE(cmd, CXDRVINFO_INIT);
	RTW89_SET_FWCMD_CXHDR_LEN(cmd, H2C_LEN_CXDRVINFO_INIT - H2C_LEN_CXDRVHDR);

	RTW89_SET_FWCMD_CXINIT_ANT_TYPE(cmd, ant->type);
	RTW89_SET_FWCMD_CXINIT_ANT_NUM(cmd, ant->num);
	RTW89_SET_FWCMD_CXINIT_ANT_ISO(cmd, ant->isolation);
	RTW89_SET_FWCMD_CXINIT_ANT_POS(cmd, ant->single_pos);
	RTW89_SET_FWCMD_CXINIT_ANT_DIVERSITY(cmd, ant->diversity);

	RTW89_SET_FWCMD_CXINIT_MOD_RFE(cmd, module->rfe_type);
	RTW89_SET_FWCMD_CXINIT_MOD_CV(cmd, module->cv);
	RTW89_SET_FWCMD_CXINIT_MOD_BT_SOLO(cmd, module->bt_solo);
	RTW89_SET_FWCMD_CXINIT_MOD_BT_POS(cmd, module->bt_pos);
	RTW89_SET_FWCMD_CXINIT_MOD_SW_TYPE(cmd, module->switch_type);

	RTW89_SET_FWCMD_CXINIT_WL_GCH(cmd, init_info->wl_guard_ch);
	RTW89_SET_FWCMD_CXINIT_WL_ONLY(cmd, init_info->wl_only);
	RTW89_SET_FWCMD_CXINIT_WL_INITOK(cmd, init_info->wl_init_ok);
	RTW89_SET_FWCMD_CXINIT_DBCC_EN(cmd, init_info->dbcc_en);
	RTW89_SET_FWCMD_CXINIT_CX_OTHER(cmd, init_info->cx_other);
	RTW89_SET_FWCMD_CXINIT_BT_ONLY(cmd, init_info->bt_only);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      H2C_LEN_CXDRVINFO_INIT);

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
#define H2C_LEN_CXDRVINFO_ROLE (4 + 12 * RTW89_PORT_NUM + H2C_LEN_CXDRVHDR)
#define H2C_LEN_CXDRVINFO_ROLE_V1 (4 + 16 * RTW89_PORT_NUM + \
				   H2C_LEN_CXDRVINFO_ROLE_DBCC_LEN + \
				   H2C_LEN_CXDRVHDR)
int rtw89_fw_h2c_cxdrv_role(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_role_info *role_info = &wl->role_info;
	struct rtw89_btc_wl_role_info_bpos *bpos = &role_info->role_map.role;
	struct rtw89_btc_wl_active_role *active = role_info->active_role;
	struct sk_buff *skb;
	u8 offset = 0;
	u8 *cmd;
	int ret;
	int i;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_LEN_CXDRVINFO_ROLE);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_role\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_LEN_CXDRVINFO_ROLE);
	cmd = skb->data;

	RTW89_SET_FWCMD_CXHDR_TYPE(cmd, CXDRVINFO_ROLE);
	RTW89_SET_FWCMD_CXHDR_LEN(cmd, H2C_LEN_CXDRVINFO_ROLE - H2C_LEN_CXDRVHDR);

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
			      H2C_LEN_CXDRVINFO_ROLE);

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

int rtw89_fw_h2c_cxdrv_role_v1(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_role_info_v1 *role_info = &wl->role_info_v1;
	struct rtw89_btc_wl_role_info_bpos *bpos = &role_info->role_map.role;
	struct rtw89_btc_wl_active_role_v1 *active = role_info->active_role_v1;
	struct sk_buff *skb;
	u8 *cmd, offset;
	int ret;
	int i;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_LEN_CXDRVINFO_ROLE_V1);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_role\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_LEN_CXDRVINFO_ROLE_V1);
	cmd = skb->data;

	RTW89_SET_FWCMD_CXHDR_TYPE(cmd, CXDRVINFO_ROLE);
	RTW89_SET_FWCMD_CXHDR_LEN(cmd, H2C_LEN_CXDRVINFO_ROLE_V1 - H2C_LEN_CXDRVHDR);

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

	offset = H2C_LEN_CXDRVINFO_ROLE_V1 - H2C_LEN_CXDRVINFO_ROLE_DBCC_LEN;
	RTW89_SET_FWCMD_CXROLE_MROLE_TYPE(cmd, role_info->mrole_type, offset);
	RTW89_SET_FWCMD_CXROLE_MROLE_NOA(cmd, role_info->mrole_noa_duration, offset);
	RTW89_SET_FWCMD_CXROLE_DBCC_EN(cmd, role_info->dbcc_en, offset);
	RTW89_SET_FWCMD_CXROLE_DBCC_CHG(cmd, role_info->dbcc_chg, offset);
	RTW89_SET_FWCMD_CXROLE_DBCC_2G_PHY(cmd, role_info->dbcc_2g_phy, offset);
	RTW89_SET_FWCMD_CXROLE_LINK_MODE_CHG(cmd, role_info->link_mode_chg, offset);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      H2C_LEN_CXDRVINFO_ROLE_V1);

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
int rtw89_fw_h2c_cxdrv_ctrl(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_ctrl *ctrl = &btc->ctrl;
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

	RTW89_SET_FWCMD_CXHDR_TYPE(cmd, CXDRVINFO_CTRL);
	RTW89_SET_FWCMD_CXHDR_LEN(cmd, H2C_LEN_CXDRVINFO_CTRL - H2C_LEN_CXDRVHDR);

	RTW89_SET_FWCMD_CXCTRL_MANUAL(cmd, ctrl->manual);
	RTW89_SET_FWCMD_CXCTRL_IGNORE_BT(cmd, ctrl->igno_bt);
	RTW89_SET_FWCMD_CXCTRL_ALWAYS_FREERUN(cmd, ctrl->always_freerun);
	if (chip->chip_id == RTL8852A)
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

#define H2C_LEN_CXDRVINFO_RFK (4 + H2C_LEN_CXDRVHDR)
int rtw89_fw_h2c_cxdrv_rfk(struct rtw89_dev *rtwdev)
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

	RTW89_SET_FWCMD_CXHDR_TYPE(cmd, CXDRVINFO_RFK);
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
	struct sk_buff *skb;
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

int rtw89_fw_h2c_add_pkt_offload(struct rtw89_dev *rtwdev, u8 *id,
				 struct sk_buff *skb_ofld)
{
	struct sk_buff *skb;
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

#define H2C_LEN_SCAN_LIST_OFFLOAD 4
int rtw89_fw_h2c_scan_list_offload(struct rtw89_dev *rtwdev, int len,
				   struct list_head *chan_list)
{
	struct rtw89_mac_chinfo *ch_info;
	struct sk_buff *skb;
	int skb_len = H2C_LEN_SCAN_LIST_OFFLOAD + len * RTW89_MAC_CHINFO_SIZE;
	u8 *cmd;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, skb_len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c scan list\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_LEN_SCAN_LIST_OFFLOAD);
	cmd = skb->data;

	RTW89_SET_FWCMD_SCANOFLD_CH_NUM(cmd, len);
	/* in unit of 4 bytes */
	RTW89_SET_FWCMD_SCANOFLD_CH_SIZE(cmd, RTW89_MAC_CHINFO_SIZE / 4);

	list_for_each_entry(ch_info, chan_list, list) {
		cmd = skb_put(skb, RTW89_MAC_CHINFO_SIZE);

		RTW89_SET_FWCMD_CHINFO_PERIOD(cmd, ch_info->period);
		RTW89_SET_FWCMD_CHINFO_DWELL(cmd, ch_info->dwell_time);
		RTW89_SET_FWCMD_CHINFO_CENTER_CH(cmd, ch_info->central_ch);
		RTW89_SET_FWCMD_CHINFO_PRI_CH(cmd, ch_info->pri_ch);
		RTW89_SET_FWCMD_CHINFO_BW(cmd, ch_info->bw);
		RTW89_SET_FWCMD_CHINFO_ACTION(cmd, ch_info->notify_action);
		RTW89_SET_FWCMD_CHINFO_NUM_PKT(cmd, ch_info->num_pkt);
		RTW89_SET_FWCMD_CHINFO_TX(cmd, ch_info->tx_pkt);
		RTW89_SET_FWCMD_CHINFO_PAUSE_DATA(cmd, ch_info->pause_data);
		RTW89_SET_FWCMD_CHINFO_BAND(cmd, ch_info->ch_band);
		RTW89_SET_FWCMD_CHINFO_PKT_ID(cmd, ch_info->probe_id);
		RTW89_SET_FWCMD_CHINFO_DFS(cmd, ch_info->dfs_ch);
		RTW89_SET_FWCMD_CHINFO_TX_NULL(cmd, ch_info->tx_null);
		RTW89_SET_FWCMD_CHINFO_RANDOM(cmd, ch_info->rand_seq_num);
		RTW89_SET_FWCMD_CHINFO_PKT0(cmd, ch_info->pkt_id[0]);
		RTW89_SET_FWCMD_CHINFO_PKT1(cmd, ch_info->pkt_id[1]);
		RTW89_SET_FWCMD_CHINFO_PKT2(cmd, ch_info->pkt_id[2]);
		RTW89_SET_FWCMD_CHINFO_PKT3(cmd, ch_info->pkt_id[3]);
		RTW89_SET_FWCMD_CHINFO_PKT4(cmd, ch_info->pkt_id[4]);
		RTW89_SET_FWCMD_CHINFO_PKT5(cmd, ch_info->pkt_id[5]);
		RTW89_SET_FWCMD_CHINFO_PKT6(cmd, ch_info->pkt_id[6]);
		RTW89_SET_FWCMD_CHINFO_PKT7(cmd, ch_info->pkt_id[7]);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_ADD_SCANOFLD_CH, 1, 1, skb_len);

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

#define H2C_LEN_SCAN_OFFLOAD 28
int rtw89_fw_h2c_scan_offload(struct rtw89_dev *rtwdev,
			      struct rtw89_scan_option *option,
			      struct rtw89_vif *rtwvif)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct sk_buff *skb;
	u8 *cmd;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, H2C_LEN_SCAN_OFFLOAD);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c scan offload\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_LEN_SCAN_OFFLOAD);
	cmd = skb->data;

	RTW89_SET_FWCMD_SCANOFLD_MACID(cmd, rtwvif->mac_id);
	RTW89_SET_FWCMD_SCANOFLD_PORT_ID(cmd, rtwvif->port);
	RTW89_SET_FWCMD_SCANOFLD_BAND(cmd, RTW89_PHY_0);
	RTW89_SET_FWCMD_SCANOFLD_OPERATION(cmd, option->enable);
	RTW89_SET_FWCMD_SCANOFLD_NOTIFY_END(cmd, true);
	RTW89_SET_FWCMD_SCANOFLD_TARGET_CH_MODE(cmd, option->target_ch_mode);
	RTW89_SET_FWCMD_SCANOFLD_START_MODE(cmd, RTW89_SCAN_IMMEDIATE);
	RTW89_SET_FWCMD_SCANOFLD_SCAN_TYPE(cmd, RTW89_SCAN_ONCE);
	if (option->target_ch_mode) {
		RTW89_SET_FWCMD_SCANOFLD_TARGET_CH_BW(cmd, scan_info->op_bw);
		RTW89_SET_FWCMD_SCANOFLD_TARGET_PRI_CH(cmd,
						       scan_info->op_pri_ch);
		RTW89_SET_FWCMD_SCANOFLD_TARGET_CENTRAL_CH(cmd,
							   scan_info->op_chan);
		RTW89_SET_FWCMD_SCANOFLD_TARGET_CH_BAND(cmd,
							scan_info->op_band);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_SCANOFLD, 1, 1,
			      H2C_LEN_SCAN_OFFLOAD);

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
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct rtw89_mcc_info *mcc_info = &rtwdev->mcc;
	struct rtw89_fw_h2c_rf_get_mccch *mccch;
	struct sk_buff *skb;
	int ret;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, sizeof(*mccch));
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c cxdrv_ctrl\n");
		return -ENOMEM;
	}
	skb_put(skb, sizeof(*mccch));
	mccch = (struct rtw89_fw_h2c_rf_get_mccch *)skb->data;

	mccch->ch_0 = cpu_to_le32(mcc_info->ch[0]);
	mccch->ch_1 = cpu_to_le32(mcc_info->ch[1]);
	mccch->band_0 = cpu_to_le32(mcc_info->band[0]);
	mccch->band_1 = cpu_to_le32(mcc_info->band[1]);
	mccch->current_channel = cpu_to_le32(chan->channel);
	mccch->current_band_type = cpu_to_le32(chan->band_type);

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

	lockdep_assert_held(&rtwdev->mutex);

	list_for_each_entry(early_h2c, &rtwdev->early_h2c_list, list) {
		rtw89_fw_h2c_raw(rtwdev, early_h2c->h2c, early_h2c->h2c_len);
	}
}

void rtw89_fw_free_all_early_h2c(struct rtw89_dev *rtwdev)
{
	struct rtw89_early_h2c *early_h2c, *tmp;

	mutex_lock(&rtwdev->mutex);
	list_for_each_entry_safe(early_h2c, tmp, &rtwdev->early_h2c_list, list) {
		list_del(&early_h2c->list);
		kfree(early_h2c->h2c);
		kfree(early_h2c);
	}
	mutex_unlock(&rtwdev->mutex);
}

void rtw89_fw_c2h_irqsafe(struct rtw89_dev *rtwdev, struct sk_buff *c2h)
{
	skb_queue_tail(&rtwdev->c2h_queue, c2h);
	ieee80211_queue_work(rtwdev->hw, &rtwdev->c2h_work);
}

static void rtw89_fw_c2h_cmd_handle(struct rtw89_dev *rtwdev,
				    struct sk_buff *skb)
{
	u8 category = RTW89_GET_C2H_CATEGORY(skb->data);
	u8 class = RTW89_GET_C2H_CLASS(skb->data);
	u8 func = RTW89_GET_C2H_FUNC(skb->data);
	u16 len = RTW89_GET_C2H_LEN(skb->data);
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

void rtw89_fw_c2h_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						c2h_work);
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&rtwdev->c2h_queue, skb, tmp) {
		skb_unlink(skb, &rtwdev->c2h_queue);
		mutex_lock(&rtwdev->mutex);
		rtw89_fw_c2h_cmd_handle(rtwdev, skb);
		mutex_unlock(&rtwdev->mutex);
		dev_kfree_skb_any(skb);
	}
}

static int rtw89_fw_write_h2c_reg(struct rtw89_dev *rtwdev,
				  struct rtw89_mac_h2c_info *info)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
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
			   sizeof(info->h2creg[0]));

	RTW89_SET_H2CREG_HDR_FUNC(&info->h2creg[0], info->id);
	RTW89_SET_H2CREG_HDR_LEN(&info->h2creg[0], len);
	for (i = 0; i < RTW89_H2CREG_MAX; i++)
		rtw89_write32(rtwdev, h2c_reg[i], info->h2creg[i]);

	rtw89_write8(rtwdev, chip->h2c_ctrl_reg, B_AX_H2CREG_TRIGGER);

	return 0;
}

static int rtw89_fw_read_c2h_reg(struct rtw89_dev *rtwdev,
				 struct rtw89_mac_c2h_info *info)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
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
		info->c2hreg[i] = rtw89_read32(rtwdev, c2h_reg[i]);

	rtw89_write8(rtwdev, chip->c2h_ctrl_reg, 0);

	info->id = RTW89_GET_C2H_HDR_FUNC(*info->c2hreg);
	info->content_len = (RTW89_GET_C2H_HDR_LEN(*info->c2hreg) << 2) -
				RTW89_C2HREG_HDR_LEN;

	return 0;
}

int rtw89_fw_msg_reg(struct rtw89_dev *rtwdev,
		     struct rtw89_mac_h2c_info *h2c_info,
		     struct rtw89_mac_c2h_info *c2h_info)
{
	u32 ret;

	if (h2c_info && h2c_info->id != RTW89_FWCMD_H2CREG_FUNC_GET_FEATURE)
		lockdep_assert_held(&rtwdev->mutex);

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

static void rtw89_release_pkt_list(struct rtw89_dev *rtwdev)
{
	struct list_head *pkt_list = rtwdev->scan_info.pkt_list;
	struct rtw89_pktofld_info *info, *tmp;
	u8 idx;

	for (idx = NL80211_BAND_2GHZ; idx < NUM_NL80211_BANDS; idx++) {
		if (!(rtwdev->chip->support_bands & BIT(idx)))
			continue;

		list_for_each_entry_safe(info, tmp, &pkt_list[idx], list) {
			rtw89_fw_h2c_del_pkt_offload(rtwdev, info->id);
			rtw89_core_release_bit_map(rtwdev->pkt_offload,
						   info->id);
			list_del(&info->list);
			kfree(info);
		}
	}
}

static int rtw89_append_probe_req_ie(struct rtw89_dev *rtwdev,
				     struct rtw89_vif *rtwvif,
				     struct sk_buff *skb)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct ieee80211_scan_ies *ies = rtwvif->scan_ies;
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

		list_add_tail(&info->list, &scan_info->pkt_list[band]);
		ret = rtw89_fw_h2c_add_pkt_offload(rtwdev, &info->id, new);
		if (ret)
			goto out;

		kfree_skb(new);
	}
out:
	return ret;
}

static int rtw89_hw_scan_update_probe_req(struct rtw89_dev *rtwdev,
					  struct rtw89_vif *rtwvif)
{
	struct cfg80211_scan_request *req = rtwvif->scan_req;
	struct sk_buff *skb;
	u8 num = req->n_ssids, i;
	int ret;

	for (i = 0; i < num; i++) {
		skb = ieee80211_probereq_get(rtwdev->hw, rtwvif->mac_addr,
					     req->ssids[i].ssid,
					     req->ssids[i].ssid_len,
					     req->ie_len);
		if (!skb)
			return -ENOMEM;

		ret = rtw89_append_probe_req_ie(rtwdev, rtwvif, skb);
		kfree_skb(skb);

		if (ret)
			return ret;
	}

	return 0;
}

static void rtw89_hw_scan_add_chan(struct rtw89_dev *rtwdev, int chan_type,
				   int ssid_num,
				   struct rtw89_mac_chinfo *ch_info)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	struct rtw89_pktofld_info *info;
	u8 band, probe_count = 0;

	ch_info->notify_action = RTW89_SCANOFLD_DEBUG_MASK;
	ch_info->dfs_ch = chan_type == RTW89_CHAN_DFS;
	ch_info->bw = RTW89_SCAN_WIDTH;
	ch_info->tx_pkt = true;
	ch_info->cfg_tx_pwr = false;
	ch_info->tx_pwr_idx = 0;
	ch_info->tx_null = false;
	ch_info->pause_data = false;

	if (ssid_num) {
		ch_info->num_pkt = ssid_num;
		band = rtw89_hw_to_nl80211_band(ch_info->ch_band);

		list_for_each_entry(info, &scan_info->pkt_list[band], list) {
			ch_info->probe_id = info->id;
			ch_info->pkt_id[probe_count] = info->id;
			if (++probe_count >= ssid_num)
				break;
		}
		if (probe_count != ssid_num)
			rtw89_err(rtwdev, "SSID num differs from list len\n");
	}

	switch (chan_type) {
	case RTW89_CHAN_OPERATE:
		ch_info->probe_id = RTW89_SCANOFLD_PKT_NONE;
		ch_info->central_ch = scan_info->op_chan;
		ch_info->pri_ch = scan_info->op_pri_ch;
		ch_info->ch_band = scan_info->op_band;
		ch_info->bw = scan_info->op_bw;
		ch_info->tx_null = true;
		ch_info->num_pkt = 0;
		break;
	case RTW89_CHAN_DFS:
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

static int rtw89_hw_scan_add_chan_list(struct rtw89_dev *rtwdev,
				       struct rtw89_vif *rtwvif)
{
	struct cfg80211_scan_request *req = rtwvif->scan_req;
	struct rtw89_mac_chinfo	*ch_info, *tmp;
	struct ieee80211_channel *channel;
	struct list_head chan_list;
	bool random_seq = req->flags & NL80211_SCAN_FLAG_RANDOM_SN;
	int list_len, off_chan_time = 0;
	enum rtw89_chan_type type;
	int ret = 0;
	u32 idx;

	INIT_LIST_HEAD(&chan_list);
	for (idx = rtwdev->scan_info.last_chan_idx, list_len = 0;
	     idx < req->n_channels && list_len < RTW89_SCAN_LIST_LIMIT;
	     idx++, list_len++) {
		channel = req->channels[idx];
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			goto out;
		}

		ch_info->period = req->duration_mandatory ?
				  req->duration : RTW89_CHANNEL_TIME;
		ch_info->ch_band = rtw89_nl80211_to_hw_band(channel->band);
		ch_info->central_ch = channel->hw_value;
		ch_info->pri_ch = channel->hw_value;
		ch_info->rand_seq_num = random_seq;

		if (channel->flags &
		    (IEEE80211_CHAN_RADAR | IEEE80211_CHAN_NO_IR))
			type = RTW89_CHAN_DFS;
		else
			type = RTW89_CHAN_ACTIVE;
		rtw89_hw_scan_add_chan(rtwdev, type, req->n_ssids, ch_info);

		if (rtwvif->net_type != RTW89_NET_TYPE_NO_LINK &&
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
			rtw89_hw_scan_add_chan(rtwdev, type, 0, tmp);
			list_add_tail(&tmp->list, &chan_list);
			off_chan_time = 0;
			list_len++;
		}
		list_add_tail(&ch_info->list, &chan_list);
		off_chan_time += ch_info->period;
	}
	rtwdev->scan_info.last_chan_idx = idx;
	ret = rtw89_fw_h2c_scan_list_offload(rtwdev, list_len, &chan_list);

out:
	list_for_each_entry_safe(ch_info, tmp, &chan_list, list) {
		list_del(&ch_info->list);
		kfree(ch_info);
	}

	return ret;
}

static int rtw89_hw_scan_prehandle(struct rtw89_dev *rtwdev,
				   struct rtw89_vif *rtwvif)
{
	int ret;

	ret = rtw89_hw_scan_update_probe_req(rtwdev, rtwvif);
	if (ret) {
		rtw89_err(rtwdev, "Update probe request failed\n");
		goto out;
	}
	ret = rtw89_hw_scan_add_chan_list(rtwdev, rtwvif);
out:
	return ret;
}

void rtw89_hw_scan_start(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			 struct ieee80211_scan_request *scan_req)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	struct cfg80211_scan_request *req = &scan_req->req;
	u32 rx_fltr = rtwdev->hal.rx_fltr;
	u8 mac_addr[ETH_ALEN];

	rtwdev->scan_info.scanning_vif = vif;
	rtwdev->scan_info.last_chan_idx = 0;
	rtwvif->scan_ies = &scan_req->ies;
	rtwvif->scan_req = req;
	ieee80211_stop_queues(rtwdev->hw);

	if (req->flags & NL80211_SCAN_FLAG_RANDOM_ADDR)
		get_random_mask_addr(mac_addr, req->mac_addr,
				     req->mac_addr_mask);
	else
		ether_addr_copy(mac_addr, vif->addr);
	rtw89_core_scan_start(rtwdev, rtwvif, mac_addr, true);

	rx_fltr &= ~B_AX_A_BCN_CHK_EN;
	rx_fltr &= ~B_AX_A_BC;
	rx_fltr &= ~B_AX_A_A1_MATCH;
	rtw89_write32_mask(rtwdev,
			   rtw89_mac_reg_by_idx(R_AX_RX_FLTR_OPT, RTW89_MAC_0),
			   B_AX_RX_FLTR_CFG_MASK,
			   rx_fltr);
}

void rtw89_hw_scan_complete(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			    bool aborted)
{
	struct cfg80211_scan_info info = {
		.aborted = aborted,
	};
	struct rtw89_vif *rtwvif;

	if (!vif)
		return;

	rtw89_write32_mask(rtwdev,
			   rtw89_mac_reg_by_idx(R_AX_RX_FLTR_OPT, RTW89_MAC_0),
			   B_AX_RX_FLTR_CFG_MASK,
			   rtwdev->hal.rx_fltr);

	rtw89_core_scan_complete(rtwdev, vif, true);
	ieee80211_scan_completed(rtwdev->hw, &info);
	ieee80211_wake_queues(rtwdev->hw);

	rtw89_release_pkt_list(rtwdev);
	rtwvif = (struct rtw89_vif *)vif->drv_priv;
	rtwvif->scan_req = NULL;
	rtwvif->scan_ies = NULL;
	rtwdev->scan_info.last_chan_idx = 0;
	rtwdev->scan_info.scanning_vif = NULL;

	if (rtwvif->net_type != RTW89_NET_TYPE_NO_LINK)
		rtw89_store_op_chan(rtwdev, false);
}

void rtw89_hw_scan_abort(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif)
{
	rtw89_hw_scan_offload(rtwdev, vif, false);
	rtw89_hw_scan_complete(rtwdev, vif, true);
}

int rtw89_hw_scan_offload(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			  bool enable)
{
	struct rtw89_scan_option opt = {0};
	struct rtw89_vif *rtwvif;
	int ret = 0;

	rtwvif = vif ? (struct rtw89_vif *)vif->drv_priv : NULL;
	if (!rtwvif)
		return -EINVAL;

	opt.enable = enable;
	opt.target_ch_mode = rtwvif->net_type != RTW89_NET_TYPE_NO_LINK;
	if (enable) {
		ret = rtw89_hw_scan_prehandle(rtwdev, rtwvif);
		if (ret)
			goto out;
	}
	ret = rtw89_fw_h2c_scan_offload(rtwdev, &opt, rtwvif);
out:
	return ret;
}

void rtw89_store_op_chan(struct rtw89_dev *rtwdev, bool backup)
{
	struct rtw89_hw_scan_info *scan_info = &rtwdev->scan_info;
	const struct rtw89_chan *cur = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct rtw89_chan new;

	if (backup) {
		scan_info->op_pri_ch = cur->primary_channel;
		scan_info->op_chan = cur->channel;
		scan_info->op_bw = cur->band_width;
		scan_info->op_band = cur->band_type;
	} else {
		rtw89_chan_create(&new, scan_info->op_chan, scan_info->op_pri_ch,
				  scan_info->op_band, scan_info->op_bw);
		rtw89_assign_entity_chan(rtwdev, RTW89_SUB_ENTITY_0, &new);
	}
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
