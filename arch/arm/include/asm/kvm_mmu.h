/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __ARM_KVM_MMU_H__
#define __ARM_KVM_MMU_H__

int create_hyp_mappings(void *from, void *to);
int create_hyp_io_mappings(void *from, void *to, phys_addr_t);
void free_hyp_pmds(void);

phys_addr_t kvm_mmu_get_httbr(void);
int kvm_mmu_init(void);
void kvm_clear_hyp_idmap(void);
#endif /* __ARM_KVM_MMU_H__ */
