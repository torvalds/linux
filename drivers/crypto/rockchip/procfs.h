/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2022 Rockchip Electronics Co., Ltd. */

#ifndef _RKCRYPTO_PROCFS_H
#define _RKCRYPTO_PROCFS_H

#include "rk_crypto_core.h"

#ifdef CONFIG_PROC_FS
int rkcrypto_proc_init(struct rk_crypto_dev *dev);
void rkcrypto_proc_cleanup(struct rk_crypto_dev *dev);
#else
static inline int rkcrypto_proc_init(struct rk_crypto_dev *dev)
{
	return 0;
}
static inline void rkcrypto_proc_cleanup(struct rk_crypto_dev *dev)
{

}
#endif

#endif
