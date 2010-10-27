/*
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
 *
 * Copyright SUSE Linux Products GmbH 2010
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#ifndef __ASM_KVM_BOOK3S_64_H__
#define __ASM_KVM_BOOK3S_64_H__

static inline struct kvmppc_book3s_shadow_vcpu *to_svcpu(struct kvm_vcpu *vcpu)
{
	return &get_paca()->shadow_vcpu;
}

#endif /* __ASM_KVM_BOOK3S_64_H__ */
