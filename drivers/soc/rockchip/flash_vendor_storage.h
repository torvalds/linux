/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */

/* Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd */

#ifndef _FLASH_VENDOR_STORAGE
#define _FLASH_VENDOR_STORAGE

int flash_vendor_dev_ops_register(int (*read)(u32 sec,
					      u32 n_sec,
					      void *p_data),
				  int (*write)(u32 sec,
					       u32 n_sec,
					       void *p_data));

#endif

