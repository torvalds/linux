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
#include <uapi/asm/sgx.h>
#include "sgx.h"

#define SGX_EINIT_SPIN_COUNT	20
#define SGX_EINIT_SLEEP_COUNT	50
#define SGX_EINIT_SLEEP_TIME	20

extern u64 sgx_attributes_reserved_mask;
extern u64 sgx_xfrm_reserved_mask;
extern u32 sgx_misc_reserved_mask;

extern const struct file_operations sgx_provision_fops;

long sgx_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);

int sgx_drv_init(void);

#endif /* __ARCH_X86_SGX_DRIVER_H__ */
