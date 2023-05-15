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

static int rtw89_acpi_dsm_get(struct rtw89_dev *rtwdev, union acpi_object *obj,
			      u8 *value)
{
	switch (obj->type) {
	case ACPI_TYPE_INTEGER:
		*value = (u8)obj->integer.value;
		break;
	case ACPI_TYPE_BUFFER:
		*value = obj->buffer.pointer[0];
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_UNEXP,
			    "acpi dsm return unhandled type: %d\n", obj->type);
		return -EINVAL;
	}

	return 0;
}

int rtw89_acpi_evaluate_dsm(struct rtw89_dev *rtwdev,
			    enum rtw89_acpi_dsm_func func, u8 *value)
{
	union acpi_object *obj;
	int ret;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(rtwdev->dev), &rtw89_guid,
				0, func, NULL);
	if (!obj) {
		rtw89_debug(rtwdev, RTW89_DBG_UNEXP,
			    "acpi dsm fail to evaluate func: %d\n", func);
		return -ENOENT;
	}

	ret = rtw89_acpi_dsm_get(rtwdev, obj, value);

	ACPI_FREE(obj);
	return ret;
}
