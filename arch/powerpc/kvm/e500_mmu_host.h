/*
 * Copyright (C) 2008-2013 Freescale Semiconductor, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#ifndef KVM_E500_MMU_HOST_H
#define KVM_E500_MMU_HOST_H

void inval_gtlbe_on_host(struct kvmppc_vcpu_e500 *vcpu_e500, int tlbsel,
			 int esel);

int e500_mmu_host_init(struct kvmppc_vcpu_e500 *vcpu_e500);
void e500_mmu_host_uninit(struct kvmppc_vcpu_e500 *vcpu_e500);

#endif /* KVM_E500_MMU_HOST_H */
