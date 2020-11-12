/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_SGX_DRIVER_H__
#define __ARCH_SGX_DRIVER_H__

#include <crypto/hash.h>
#include <linux/kref.h>
#include <linux/mmu_notifier.h>
#include <linux/radix-tree.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include "sgx.h"

int sgx_drv_init(void);

#endif /* __ARCH_X86_SGX_DRIVER_H__ */
