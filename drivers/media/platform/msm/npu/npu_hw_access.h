/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _NPU_HW_ACCESS_H
#define _NPU_HW_ACCESS_H

/* -------------------------------------------------------------------------
 * Includes
 * -------------------------------------------------------------------------
 */
#include "npu_common.h"

/* -------------------------------------------------------------------------
 * Defines
 * -------------------------------------------------------------------------
 */
#define IPC_MEM_OFFSET_FROM_SSTCM 0x00010000
#define SYS_CACHE_SCID 23

#define QFPROM_FMAX_REG_OFFSET 0x000001C8
#define QFPROM_FMAX_BITS_MASK  0x0000000C
#define QFPROM_FMAX_BITS_SHIFT 2

#define REGW(npu_dev, off, val) npu_core_reg_write(npu_dev, off, val)
#define REGR(npu_dev, off) npu_core_reg_read(npu_dev, off)
#define MEMW(npu_dev, dst, src, size) npu_mem_write(npu_dev, (void *)(dst),\
	(void *)(src), size)
#define MEMR(npu_dev, src, dst, size) npu_mem_read(npu_dev, (void *)(src),\
	(void *)(dst), size)
#define IPC_ADDR npu_ipc_addr()
#define INTERRUPT_ACK(npu_dev, num) npu_interrupt_ack(npu_dev, num)
#define INTERRUPT_RAISE_NPU(npu_dev) npu_interrupt_raise_m0(npu_dev)
#define INTERRUPT_RAISE_DSP(npu_dev) npu_interrupt_raise_dsp(npu_dev)

/* -------------------------------------------------------------------------
 * Data Structures
 * -------------------------------------------------------------------------
 */
struct npu_device;
struct npu_ion_buf_t;
struct npu_host_ctx;
struct npu_client;
typedef irqreturn_t (*intr_hdlr_fn)(int32_t irq, void *ptr);
typedef void (*wq_hdlr_fn) (struct work_struct *work);

/* -------------------------------------------------------------------------
 * Function Prototypes
 * -------------------------------------------------------------------------
 */
uint32_t npu_core_reg_read(struct npu_device *npu_dev, uint32_t off);
void npu_core_reg_write(struct npu_device *npu_dev, uint32_t off, uint32_t val);
uint32_t npu_bwmon_reg_read(struct npu_device *npu_dev, uint32_t off);
void npu_bwmon_reg_write(struct npu_device *npu_dev, uint32_t off,
	uint32_t val);
void npu_mem_write(struct npu_device *npu_dev, void *dst, void *src,
	uint32_t size);
int32_t npu_mem_read(struct npu_device *npu_dev, void *src, void *dst,
	uint32_t size);
uint32_t npu_qfprom_reg_read(struct npu_device *npu_dev, uint32_t off);

int npu_mem_map(struct npu_client *client, int buf_hdl, uint32_t size,
	uint64_t *addr);
void npu_mem_unmap(struct npu_client *client, int buf_hdl, uint64_t addr);
void npu_mem_invalidate(struct npu_client *client, int buf_hdl);
bool npu_mem_verify_addr(struct npu_client *client, uint64_t addr);

void *npu_ipc_addr(void);
void npu_interrupt_ack(struct npu_device *npu_dev, uint32_t intr_num);
int32_t npu_interrupt_raise_m0(struct npu_device *npu_dev);
int32_t npu_interrupt_raise_dsp(struct npu_device *npu_dev);

uint8_t npu_hw_clk_gating_enabled(void);
uint8_t npu_hw_log_enabled(void);

int npu_enable_irq(struct npu_device *npu_dev);
void npu_disable_irq(struct npu_device *npu_dev);

int npu_enable_sys_cache(struct npu_device *npu_dev);
void npu_disable_sys_cache(struct npu_device *npu_dev);

int npu_subsystem_get(struct npu_device *npu_dev, const char *fw_name);
void npu_subsystem_put(struct npu_device *npu_dev);

#endif /* _NPU_HW_ACCESS_H*/
