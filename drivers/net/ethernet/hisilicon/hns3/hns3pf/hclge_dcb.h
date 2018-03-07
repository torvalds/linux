/*
 * Copyright (c) 2016~2017 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __HCLGE_DCB_H__
#define __HCLGE_DCB_H__

#include "hclge_main.h"

#ifdef CONFIG_HNS3_DCB
void hclge_dcb_ops_set(struct hclge_dev *hdev);
#else
static inline void hclge_dcb_ops_set(struct hclge_dev *hdev) {}
#endif

#endif /* __HCLGE_DCB_H__ */
