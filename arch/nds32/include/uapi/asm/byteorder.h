// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __NDS32_BYTEORDER_H__
#define __NDS32_BYTEORDER_H__

#ifdef __NDS32_EB__
#include <linux/byteorder/big_endian.h>
#else
#include <linux/byteorder/little_endian.h>
#endif

#endif /* __NDS32_BYTEORDER_H__ */
