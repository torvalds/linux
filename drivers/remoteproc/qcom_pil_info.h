/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QCOM_PIL_INFO_H__
#define __QCOM_PIL_INFO_H__

#include <linux/types.h>

int qcom_pil_info_store(const char *image, phys_addr_t base, size_t size);

#endif
