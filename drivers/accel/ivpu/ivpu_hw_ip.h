/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#ifndef __IVPU_HW_IP_H__
#define __IVPU_HW_IP_H__

#include "ivpu_drv.h"

int ivpu_hw_ip_host_ss_configure(struct ivpu_device *vdev);
void ivpu_hw_ip_idle_gen_enable(struct ivpu_device *vdev);
void ivpu_hw_ip_idle_gen_disable(struct ivpu_device *vdev);
int ivpu_hw_ip_pwr_domain_enable(struct ivpu_device *vdev);
int ivpu_hw_ip_host_ss_axi_enable(struct ivpu_device *vdev);
int ivpu_hw_ip_top_noc_enable(struct ivpu_device *vdev);
u64 ivpu_hw_ip_read_perf_timer_counter(struct ivpu_device *vdev);
void ivpu_hw_ip_snoop_disable(struct ivpu_device *vdev);
void ivpu_hw_ip_tbu_mmu_enable(struct ivpu_device *vdev);
int ivpu_hw_ip_soc_cpu_boot(struct ivpu_device *vdev);
void ivpu_hw_ip_wdt_disable(struct ivpu_device *vdev);
void ivpu_hw_ip_diagnose_failure(struct ivpu_device *vdev);
u32 ivpu_hw_ip_ipc_rx_count_get(struct ivpu_device *vdev);
void ivpu_hw_ip_irq_clear(struct ivpu_device *vdev);
bool ivpu_hw_ip_irq_handler_37xx(struct ivpu_device *vdev, int irq);
bool ivpu_hw_ip_irq_handler_40xx(struct ivpu_device *vdev, int irq);
void ivpu_hw_ip_db_set(struct ivpu_device *vdev, u32 db_id);
u32 ivpu_hw_ip_ipc_rx_addr_get(struct ivpu_device *vdev);
void ivpu_hw_ip_ipc_tx_set(struct ivpu_device *vdev, u32 vpu_addr);
void ivpu_hw_ip_irq_enable(struct ivpu_device *vdev);
void ivpu_hw_ip_irq_disable(struct ivpu_device *vdev);
void ivpu_hw_ip_diagnose_failure(struct ivpu_device *vdev);
void ivpu_hw_ip_fabric_req_override_enable_50xx(struct ivpu_device *vdev);
void ivpu_hw_ip_fabric_req_override_disable_50xx(struct ivpu_device *vdev);

#endif /* __IVPU_HW_IP_H__ */
