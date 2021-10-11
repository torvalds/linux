// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "cam.h"
#include "coex.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"

static struct sk_buff *rtw89_fw_h2c_alloc_skb(u32 len, bool header)
{
	struct sk_buff *skb;
	u32 header_len = 0;

	if (header)
		header_len = H2C_HEADER_LEN;

	skb = dev_alloc_skb(len + header_len + 24);
	if (!skb)
		return NULL;
	skb_reserve(skb, header_len + 24);
	memset(skb->data, 0, len);

	return skb;
}

struct sk_buff *rtw89_fw_h2c_alloc_skb_with_hdr(u32 len)
{
	return rtw89_fw_h2c_alloc_skb(len, true);
}

struct sk_buff *rtw89_fw_h2c_alloc_skb_no_hdr(u32 len)
{
	return rtw89_fw_h2c_alloc_skb(len, false);
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
	SET_FW_HDR_PART_SIZE(fw, FWDL_SECTION_PER_PKT_LEN);

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

static void rtw89_fw_recognize_features(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_fw_suit *fw_suit = rtw89_fw_suit_get(rtwdev, RTW89_FW_NORMAL);

	if (chip->chip_id == RTL8852A &&
	    RTW89_FW_SUIT_VER_CODE(fw_suit) <= RTW89_FW_VER_CODE(0, 13, 29, 0))
		rtwdev->fw.old_ht_ra_format = true;
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

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw hdr dl\n");
		return -ENOMEM;
	}

	skb_put_data(skb, fw, len);
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

		skb = rtw89_fw_h2c_alloc_skb_no_hdr(pkt_len);
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
int rtw89_fw_h2c_cam(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_CAM_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_CAM_LEN);
	rtw89_cam_fill_addr_cam_info(rtwdev, rtwvif, skb->data);
	rtw89_cam_fill_bssid_cam_info(rtwdev, rtwvif, skb->data);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_ADDR_CAM_UPDATE,
			      H2C_FUNC_MAC_ADDR_CAM_UPD, 0, 1,
			      H2C_CAM_LEN);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_BA_CAM_LEN 4
int rtw89_fw_h2c_ba_cam(struct rtw89_dev *rtwdev, bool valid, u8 macid,
			struct ieee80211_ampdu_params *params)
{
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_BA_CAM_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c ba cam\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_BA_CAM_LEN);
	SET_BA_CAM_MACID(skb->data, macid);
	if (!valid)
		goto end;
	SET_BA_CAM_VALID(skb->data, valid);
	SET_BA_CAM_TID(skb->data, params->tid);
	if (params->buf_size > 64)
		SET_BA_CAM_BMAP_SIZE(skb->data, 4);
	else
		SET_BA_CAM_BMAP_SIZE(skb->data, 0);
	/* If init req is set, hw will set the ssn */
	SET_BA_CAM_INIT_REQ(skb->data, 0);
	SET_BA_CAM_SSN(skb->data, params->ssn);

end:
	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_BA_CAM,
			      H2C_FUNC_MAC_BA_CAM, 0, 1,
			      H2C_BA_CAM_LEN);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_LOG_CFG_LEN 12
int rtw89_fw_h2c_fw_log(struct rtw89_dev *rtwdev, bool enable)
{
	struct sk_buff *skb;
	u32 comp = enable ? BIT(RTW89_FW_LOG_COMP_INIT) | BIT(RTW89_FW_LOG_COMP_TASK) |
			    BIT(RTW89_FW_LOG_COMP_PS) | BIT(RTW89_FW_LOG_COMP_ERROR) : 0;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_LOG_CFG_LEN);
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

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_GENERAL_PKT_LEN 6
#define H2C_GENERAL_PKT_ID_UND 0xff
int rtw89_fw_h2c_general_pkt(struct rtw89_dev *rtwdev, u8 macid)
{
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_GENERAL_PKT_LEN);
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

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_LPS_PARM_LEN 8
int rtw89_fw_h2c_lps_parm(struct rtw89_dev *rtwdev,
			  struct rtw89_lps_parm *lps_param)
{
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_LPS_PARM_LEN);
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

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_CMC_TBL_LEN 68
int rtw89_fw_h2c_default_cmac_tbl(struct rtw89_dev *rtwdev, u8 macid)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct sk_buff *skb;
	u8 ntx_path = hal->antenna_tx ? hal->antenna_tx : RF_B;
	u8 map_b = hal->antenna_tx == RF_AB ? 1 : 0;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_CMC_TBL_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_CMC_TBL_LEN);
	SET_CTRL_INFO_MACID(skb->data, macid);
	SET_CTRL_INFO_OPERATION(skb->data, 1);
	SET_CMC_TBL_TXPWR_MODE(skb->data, 0);
	SET_CMC_TBL_NTX_PATH_EN(skb->data, ntx_path);
	SET_CMC_TBL_PATH_MAP_A(skb->data, 0);
	SET_CMC_TBL_PATH_MAP_B(skb->data, map_b);
	SET_CMC_TBL_PATH_MAP_C(skb->data, 0);
	SET_CMC_TBL_PATH_MAP_D(skb->data, 0);
	SET_CMC_TBL_ANTSEL_A(skb->data, 0);
	SET_CMC_TBL_ANTSEL_B(skb->data, 0);
	SET_CMC_TBL_ANTSEL_C(skb->data, 0);
	SET_CMC_TBL_ANTSEL_D(skb->data, 0);
	SET_CMC_TBL_DOPPLER_CTRL(skb->data, 0);
	SET_CMC_TBL_TXPWR_TOLERENCE(skb->data, 0);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_CCTLINFO_UD, 0, 1,
			      H2C_CMC_TBL_LEN);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

static void __get_sta_he_pkt_padding(struct rtw89_dev *rtwdev,
				     struct ieee80211_sta *sta, u8 *pads)
{
	bool ppe_th;
	u8 ppe16, ppe8;
	u8 nss = min(sta->rx_nss, rtwdev->hal.tx_nss) - 1;
	u8 ppe_thres_hdr = sta->he_cap.ppe_thres[0];
	u8 ru_bitmap;
	u8 n, idx, sh;
	u16 ppe;
	int i;

	if (!sta->he_cap.has_he)
		return;

	ppe_th = FIELD_GET(IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT,
			   sta->he_cap.he_cap_elem.phy_cap_info[6]);
	if (!ppe_th) {
		u8 pad;

		pad = FIELD_GET(IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_MASK,
				sta->he_cap.he_cap_elem.phy_cap_info[9]);

		for (i = 0; i < RTW89_PPE_BW_NUM; i++)
			pads[i] = pad;
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

		ppe = le16_to_cpu(*((__le16 *)&sta->he_cap.ppe_thres[idx]));
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
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	struct sk_buff *skb;
	u8 pads[RTW89_PPE_BW_NUM];

	memset(pads, 0, sizeof(pads));
	__get_sta_he_pkt_padding(rtwdev, sta, pads);

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_CMC_TBL_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for fw dl\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_CMC_TBL_LEN);
	SET_CTRL_INFO_MACID(skb->data, rtwsta->mac_id);
	SET_CTRL_INFO_OPERATION(skb->data, 1);
	SET_CMC_TBL_DISRTSFB(skb->data, 1);
	SET_CMC_TBL_DISDATAFB(skb->data, 1);
	if (hal->current_band_type == RTW89_BAND_2G)
		SET_CMC_TBL_RTS_RTY_LOWEST_RATE(skb->data, RTW89_HW_RATE_CCK1);
	else
		SET_CMC_TBL_RTS_RTY_LOWEST_RATE(skb->data, RTW89_HW_RATE_OFDM6);
	SET_CMC_TBL_RTS_TXCNT_LMT_SEL(skb->data, 0);
	SET_CMC_TBL_DATA_TXCNT_LMT_SEL(skb->data, 0);
	if (vif->type == NL80211_IFTYPE_STATION)
		SET_CMC_TBL_ULDL(skb->data, 1);
	else
		SET_CMC_TBL_ULDL(skb->data, 0);
	SET_CMC_TBL_MULTI_PORT_ID(skb->data, rtwvif->port);
	SET_CMC_TBL_NOMINAL_PKT_PADDING(skb->data, pads[RTW89_CHANNEL_WIDTH_20]);
	SET_CMC_TBL_NOMINAL_PKT_PADDING40(skb->data, pads[RTW89_CHANNEL_WIDTH_40]);
	SET_CMC_TBL_NOMINAL_PKT_PADDING80(skb->data, pads[RTW89_CHANNEL_WIDTH_80]);
	SET_CMC_TBL_BSR_QUEUE_SIZE_FORMAT(skb->data, sta->he_cap.has_he);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FR_EXCHG,
			      H2C_FUNC_MAC_CCTLINFO_UD, 0, 1,
			      H2C_CMC_TBL_LEN);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

int rtw89_fw_h2c_txtime_cmac_tbl(struct rtw89_dev *rtwdev,
				 struct rtw89_sta *rtwsta)
{
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_CMC_TBL_LEN);
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
			      H2C_FUNC_MAC_CCTLINFO_UD, 0, 1,
			      H2C_CMC_TBL_LEN);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_VIF_MAINTAIN_LEN 4
int rtw89_fw_h2c_vif_maintain(struct rtw89_dev *rtwdev,
			      struct rtw89_vif *rtwvif,
			      enum rtw89_upd_mode upd_mode)
{
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_VIF_MAINTAIN_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c join\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_VIF_MAINTAIN_LEN);
	SET_FWROLE_MAINTAIN_MACID(skb->data, rtwvif->mac_id);
	SET_FWROLE_MAINTAIN_SELF_ROLE(skb->data, rtwvif->self_role);
	SET_FWROLE_MAINTAIN_UPD_MODE(skb->data, upd_mode);
	SET_FWROLE_MAINTAIN_WIFI_ROLE(skb->data, rtwvif->wifi_role);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_MEDIA_RPT,
			      H2C_FUNC_MAC_FWROLE_MAINTAIN, 0, 1,
			      H2C_VIF_MAINTAIN_LEN);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_JOIN_INFO_LEN 4
int rtw89_fw_h2c_join_info(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			   u8 dis_conn)
{
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_JOIN_INFO_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c join\n");
		return -ENOMEM;
	}
	skb_put(skb, H2C_JOIN_INFO_LEN);
	SET_JOININFO_MACID(skb->data, rtwvif->mac_id);
	SET_JOININFO_OP(skb->data, dis_conn);
	SET_JOININFO_BAND(skb->data, rtwvif->mac_idx);
	SET_JOININFO_WMM(skb->data, rtwvif->wmm);
	SET_JOININFO_TGR(skb->data, rtwvif->trigger);
	SET_JOININFO_ISHESTA(skb->data, 0);
	SET_JOININFO_DLBW(skb->data, 0);
	SET_JOININFO_TF_MAC_PAD(skb->data, 0);
	SET_JOININFO_DL_T_PE(skb->data, 0);
	SET_JOININFO_PORT_ID(skb->data, rtwvif->port);
	SET_JOININFO_NET_TYPE(skb->data, rtwvif->net_type);
	SET_JOININFO_WIFI_ROLE(skb->data, rtwvif->wifi_role);
	SET_JOININFO_SELF_ROLE(skb->data, rtwvif->self_role);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_MEDIA_RPT,
			      H2C_FUNC_MAC_JOININFO, 0, 1,
			      H2C_JOIN_INFO_LEN);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

int rtw89_fw_h2c_macid_pause(struct rtw89_dev *rtwdev, u8 sh, u8 grp,
			     bool pause)
{
	struct rtw89_fw_macid_pause_grp h2c = {{0}};
	u8 len = sizeof(struct rtw89_fw_macid_pause_grp);
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_JOIN_INFO_LEN);
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

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_EDCA_LEN 12
int rtw89_fw_h2c_set_edca(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			  u8 ac, u32 val)
{
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_EDCA_LEN);
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

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_OFLD_CFG_LEN 8
int rtw89_fw_h2c_set_ofld_cfg(struct rtw89_dev *rtwdev)
{
	static const u8 cfg[] = {0x09, 0x00, 0x00, 0x00, 0x5e, 0x00, 0x00, 0x00};
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_OFLD_CFG_LEN);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c ofld\n");
		return -ENOMEM;
	}
	skb_put_data(skb, cfg, H2C_OFLD_CFG_LEN);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_MAC, H2C_CL_MAC_FW_OFLD,
			      H2C_FUNC_OFLD_CFG, 0, 1,
			      H2C_OFLD_CFG_LEN);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_RA_LEN 16
int rtw89_fw_h2c_ra(struct rtw89_dev *rtwdev, struct rtw89_ra_info *ra, bool csi)
{
	struct sk_buff *skb;
	u8 *cmd;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_RA_LEN);
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

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
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

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_LEN_CXDRVINFO_INIT);
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

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_LEN_CXDRVINFO_ROLE (4 + 12 * RTW89_MAX_HW_PORT_NUM + H2C_LEN_CXDRVHDR)
int rtw89_fw_h2c_cxdrv_role(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_role_info *role_info = &wl->role_info;
	struct rtw89_btc_wl_role_info_bpos *bpos = &role_info->role_map.role;
	struct rtw89_btc_wl_active_role *active = role_info->active_role;
	struct sk_buff *skb;
	u8 *cmd;
	int i;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_LEN_CXDRVINFO_ROLE);
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

	for (i = 0; i < RTW89_MAX_HW_PORT_NUM; i++, active++) {
		RTW89_SET_FWCMD_CXROLE_ACT_CONNECTED(cmd, active->connected, i);
		RTW89_SET_FWCMD_CXROLE_ACT_PID(cmd, active->pid, i);
		RTW89_SET_FWCMD_CXROLE_ACT_PHY(cmd, active->phy, i);
		RTW89_SET_FWCMD_CXROLE_ACT_NOA(cmd, active->noa, i);
		RTW89_SET_FWCMD_CXROLE_ACT_BAND(cmd, active->band, i);
		RTW89_SET_FWCMD_CXROLE_ACT_CLIENT_PS(cmd, active->client_ps, i);
		RTW89_SET_FWCMD_CXROLE_ACT_BW(cmd, active->bw, i);
		RTW89_SET_FWCMD_CXROLE_ACT_ROLE(cmd, active->role, i);
		RTW89_SET_FWCMD_CXROLE_ACT_CH(cmd, active->ch, i);
		RTW89_SET_FWCMD_CXROLE_ACT_TX_LVL(cmd, active->tx_lvl, i);
		RTW89_SET_FWCMD_CXROLE_ACT_RX_LVL(cmd, active->rx_lvl, i);
		RTW89_SET_FWCMD_CXROLE_ACT_TX_RATE(cmd, active->tx_rate, i);
		RTW89_SET_FWCMD_CXROLE_ACT_RX_RATE(cmd, active->rx_rate, i);
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      H2C_LEN_CXDRVINFO_ROLE);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_LEN_CXDRVINFO_CTRL (4 + H2C_LEN_CXDRVHDR)
int rtw89_fw_h2c_cxdrv_ctrl(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_ctrl *ctrl = &btc->ctrl;
	struct sk_buff *skb;
	u8 *cmd;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_LEN_CXDRVINFO_CTRL);
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
	RTW89_SET_FWCMD_CXCTRL_TRACE_STEP(cmd, ctrl->trace_step);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, BTFC_SET,
			      SET_DRV_INFO, 0, 0,
			      H2C_LEN_CXDRVINFO_CTRL);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

#define H2C_LEN_CXDRVINFO_RFK (4 + H2C_LEN_CXDRVHDR)
int rtw89_fw_h2c_cxdrv_rfk(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_rfk_info *rfk_info = &wl->rfk_info;
	struct sk_buff *skb;
	u8 *cmd;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(H2C_LEN_CXDRVINFO_RFK);
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

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

int rtw89_fw_h2c_rf_reg(struct rtw89_dev *rtwdev,
			struct rtw89_fw_h2c_rf_reg_info *info,
			u16 len, u8 page)
{
	struct sk_buff *skb;
	u8 class = info->rf_path == RF_PATH_A ?
		   H2C_CL_OUTSRC_RF_REG_A : H2C_CL_OUTSRC_RF_REG_B;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c rf reg\n");
		return -ENOMEM;
	}
	skb_put_data(skb, info->rtw89_phy_config_rf_h2c[page], len);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, class, page, 0, 0,
			      len);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

int rtw89_fw_h2c_raw_with_hdr(struct rtw89_dev *rtwdev,
			      u8 h2c_class, u8 h2c_func, u8 *buf, u16 len,
			      bool rack, bool dack)
{
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for raw with hdr\n");
		return -ENOMEM;
	}
	skb_put_data(skb, buf, len);

	rtw89_h2c_pkt_set_hdr(rtwdev, skb, FWCMD_TYPE_H2C,
			      H2C_CAT_OUTSRC, h2c_class, h2c_func, rack, dack,
			      len);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
}

int rtw89_fw_h2c_raw(struct rtw89_dev *rtwdev, const u8 *buf, u16 len)
{
	struct sk_buff *skb;

	skb = rtw89_fw_h2c_alloc_skb_no_hdr(len);
	if (!skb) {
		rtw89_err(rtwdev, "failed to alloc skb for h2c raw\n");
		return -ENOMEM;
	}
	skb_put_data(skb, buf, len);

	if (rtw89_h2c_tx(rtwdev, skb, false)) {
		rtw89_err(rtwdev, "failed to send h2c\n");
		goto fail;
	}

	return 0;
fail:
	dev_kfree_skb_any(skb);

	return -EBUSY;
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
	static const u32 h2c_reg[RTW89_H2CREG_MAX] = {
		R_AX_H2CREG_DATA0, R_AX_H2CREG_DATA1,
		R_AX_H2CREG_DATA2, R_AX_H2CREG_DATA3
	};
	u8 i, val, len;
	int ret;

	ret = read_poll_timeout(rtw89_read8, val, val == 0, 1000, 5000, false,
				rtwdev, R_AX_H2CREG_CTRL);
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

	rtw89_write8(rtwdev, R_AX_H2CREG_CTRL, B_AX_H2CREG_TRIGGER);

	return 0;
}

static int rtw89_fw_read_c2h_reg(struct rtw89_dev *rtwdev,
				 struct rtw89_mac_c2h_info *info)
{
	static const u32 c2h_reg[RTW89_C2HREG_MAX] = {
		R_AX_C2HREG_DATA0, R_AX_C2HREG_DATA1,
		R_AX_C2HREG_DATA2, R_AX_C2HREG_DATA3
	};
	u32 ret;
	u8 i, val;

	info->id = RTW89_FWCMD_C2HREG_FUNC_NULL;

	ret = read_poll_timeout_atomic(rtw89_read8, val, val, 1,
				       RTW89_C2H_TIMEOUT, false, rtwdev,
				       R_AX_C2HREG_CTRL);
	if (ret) {
		rtw89_warn(rtwdev, "c2h reg timeout\n");
		return ret;
	}

	for (i = 0; i < RTW89_C2HREG_MAX; i++)
		info->c2hreg[i] = rtw89_read32(rtwdev, c2h_reg[i]);

	rtw89_write8(rtwdev, R_AX_C2HREG_CTRL, 0);

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
