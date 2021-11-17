/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation.
 */

#ifndef IOSM_IPC_IRQ_H
#define IOSM_IPC_IRQ_H

struct iosm_pcie;

/**
 * ipc_doorbell_fire - fire doorbell to CP
 * @ipc_pcie:	Pointer to iosm_pcie
 * @irq_n:	Doorbell type
 * @data:	ipc state
 */
void ipc_doorbell_fire(struct iosm_pcie *ipc_pcie, int irq_n, u32 data);

/**
 * ipc_release_irq - Release the IRQ handler.
 * @ipc_pcie:	Pointer to iosm_pcie struct
 */
void ipc_release_irq(struct iosm_pcie *ipc_pcie);

/**
 * ipc_acquire_irq - acquire IRQ & register IRQ handler.
 * @ipc_pcie:	Pointer to iosm_pcie struct
 *
 * Return: 0 on success and failure value on error
 */
int ipc_acquire_irq(struct iosm_pcie *ipc_pcie);

#endif
