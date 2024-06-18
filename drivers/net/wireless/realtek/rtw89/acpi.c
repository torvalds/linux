// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2021-2023  Realtek Corporation
 */

#include <linux/acpi.h>
#include <linux/uuid.h>

#include "acpi.h"
#include "debug.h"

static const guid_t rtw89_guid = GUID_INIT(0xD2A8C3E8, 0x4B69, 0x4F00,
					   0x82, 0xBD, 0xFE, 0x86,
					   0x07, 0x80, 0x3A, 0xA7);

static
int rtw89_acpi_dsm_get_value(struct rtw89_dev *rtwdev, union acpi_object *obj,
			     u8 *value)
{
	if (obj->type != ACPI_TYPE_INTEGER) {
		rtw89_debug(rtwdev, RTW89_DBG_ACPI,
			    "acpi: expect integer but type: %d\n", obj->type);
		return -EINVAL;
	}

	*value = (u8)obj->integer.value;
	return 0;
}

static bool chk_acpi_policy_6ghz_sig(const struct rtw89_acpi_policy_6ghz *p)
{
	return p->signature[0] == 0x00 &&
	       p->signature[1] == 0xE0 &&
	       p->signature[2] == 0x4C;
}

static
int rtw89_acpi_dsm_get_policy_6ghz(struct rtw89_dev *rtwdev,
				   union acpi_object *obj,
				   struct rtw89_acpi_policy_6ghz **policy_6ghz)
{
	const struct rtw89_acpi_policy_6ghz *ptr;
	u32 expect_len;
	u32 len;

	if (obj->type != ACPI_TYPE_BUFFER) {
		rtw89_debug(rtwdev, RTW89_DBG_ACPI,
			    "acpi: expect buffer but type: %d\n", obj->type);
		return -EINVAL;
	}

	len = obj->buffer.length;
	if (len < sizeof(*ptr)) {
		rtw89_debug(rtwdev, RTW89_DBG_ACPI, "%s: invalid buffer length: %u\n",
			    __func__, len);
		return -EINVAL;
	}

	ptr = (typeof(ptr))obj->buffer.pointer;
	if (!chk_acpi_policy_6ghz_sig(ptr)) {
		rtw89_debug(rtwdev, RTW89_DBG_ACPI, "%s: bad signature\n", __func__);
		return -EINVAL;
	}

	expect_len = struct_size(ptr, country_list, ptr->country_count);
	if (len < expect_len) {
		rtw89_debug(rtwdev, RTW89_DBG_ACPI, "%s: expect %u but length: %u\n",
			    __func__, expect_len, len);
		return -EINVAL;
	}

	*policy_6ghz = kmemdup(ptr, expect_len, GFP_KERNEL);
	if (!*policy_6ghz)
		return -ENOMEM;

	rtw89_hex_dump(rtwdev, RTW89_DBG_ACPI, "policy_6ghz: ", *policy_6ghz,
		       expect_len);
	return 0;
}

static bool chk_acpi_policy_6ghz_sp_sig(const struct rtw89_acpi_policy_6ghz_sp *p)
{
	return p->signature[0] == 0x52 &&
	       p->signature[1] == 0x54 &&
	       p->signature[2] == 0x4B &&
	       p->signature[3] == 0x07;
}

static
int rtw89_acpi_dsm_get_policy_6ghz_sp(struct rtw89_dev *rtwdev,
				      union acpi_object *obj,
				      struct rtw89_acpi_policy_6ghz_sp **policy)
{
	const struct rtw89_acpi_policy_6ghz_sp *ptr;
	u32 buf_len;

	if (obj->type != ACPI_TYPE_BUFFER) {
		rtw89_debug(rtwdev, RTW89_DBG_ACPI,
			    "acpi: expect buffer but type: %d\n", obj->type);
		return -EINVAL;
	}

	buf_len = obj->buffer.length;
	if (buf_len < sizeof(*ptr)) {
		rtw89_debug(rtwdev, RTW89_DBG_ACPI, "%s: invalid buffer length: %u\n",
			    __func__, buf_len);
		return -EINVAL;
	}

	ptr = (typeof(ptr))obj->buffer.pointer;
	if (!chk_acpi_policy_6ghz_sp_sig(ptr)) {
		rtw89_debug(rtwdev, RTW89_DBG_ACPI, "%s: bad signature\n", __func__);
		return -EINVAL;
	}

	*policy = kmemdup(ptr, sizeof(*ptr), GFP_KERNEL);
	if (!*policy)
		return -ENOMEM;

	rtw89_hex_dump(rtwdev, RTW89_DBG_ACPI, "policy_6ghz_sp: ", *policy,
		       sizeof(*ptr));
	return 0;
}

int rtw89_acpi_evaluate_dsm(struct rtw89_dev *rtwdev,
			    enum rtw89_acpi_dsm_func func,
			    struct rtw89_acpi_dsm_result *res)
{
	union acpi_object *obj;
	int ret;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(rtwdev->dev), &rtw89_guid,
				0, func, NULL);
	if (!obj) {
		rtw89_debug(rtwdev, RTW89_DBG_ACPI,
			    "acpi dsm fail to evaluate func: %d\n", func);
		return -ENOENT;
	}

	if (func == RTW89_ACPI_DSM_FUNC_6G_BP)
		ret = rtw89_acpi_dsm_get_policy_6ghz(rtwdev, obj,
						     &res->u.policy_6ghz);
	else if (func == RTW89_ACPI_DSM_FUNC_6GHZ_SP_SUP)
		ret = rtw89_acpi_dsm_get_policy_6ghz_sp(rtwdev, obj,
							&res->u.policy_6ghz_sp);
	else
		ret = rtw89_acpi_dsm_get_value(rtwdev, obj, &res->u.value);

	ACPI_FREE(obj);
	return ret;
}
