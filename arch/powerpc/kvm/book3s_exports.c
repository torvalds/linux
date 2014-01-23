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
 * Copyright SUSE Linux Products GmbH 2009
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#include <linux/export.h>
#include <asm/kvm_book3s.h>

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
EXPORT_SYMBOL_GPL(kvmppc_hv_entry_trampoline);
#endif
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
EXPORT_SYMBOL_GPL(kvmppc_entry_trampoline);
EXPORT_SYMBOL_GPL(kvmppc_load_up_fpu);
#ifdef CONFIG_ALTIVEC
EXPORT_SYMBOL_GPL(kvmppc_load_up_altivec);
#endif
#endif

