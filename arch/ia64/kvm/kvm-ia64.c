/*
 * kvm_ia64.c: Basic KVM suppport On Itanium series processors
 *
 *
 * 	Copyright (C) 2007, Intel Corporation.
 *  	Xiantao Zhang  (xiantao.zhang@intel.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/percpu.h>
#include <linux/gfp.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/bitops.h>
#include <linux/hrtimer.h>
#include <linux/uaccess.h>
#include <linux/iommu.h>
#include <linux/intel-iommu.h>

#include <asm/pgtable.h>
#include <asm/gcc_intrin.h>
#include <asm/pal.h>
#include <asm/cacheflush.h>
#include <asm/div64.h>
#include <asm/tlb.h>
#include <asm/elf.h>
#include <asm/sn/addrs.h>
#include <asm/sn/clksupport.h>
#include <asm/sn/shub_mmr.h>

#include "misc.h"
#include "vti.h"
#include "iodev.h"
#include "ioapic.h"
#include "lapic.h"
#include "irq.h"

static unsigned long kvm_vmm_base;
static unsigned long kvm_vsa_base;
static unsigned long kvm_vm_buffer;
static unsigned long kvm_vm_buffer_size;
unsigned long kvm_vmm_gp;

static long vp_env_info;

static struct kvm_vmm_info *kvm_vmm_info;

static DEFINE_PER_CPU(struct kvm_vcpu *, last_vcpu);

struct kvm_stats_debugfs_item debugfs_entries[] = {
	{ NULL }
};

static unsigned long kvm_get_itc(struct kvm_vcpu *vcpu)
{
#if defined(CONFIG_IA64_SGI_SN2) || defined(CONFIG_IA64_GENERIC)
	if (vcpu->kvm->arch.is_sn2)
		return rtc_time();
	else
#endif
		return ia64_getreg(_IA64_REG_AR_ITC);
}

static void kvm_flush_icache(unsigned long start, unsigned long len)
{
	int l;

	for (l = 0; l < (len + 32); l += 32)
		ia64_fc((void *)(start + l));

	ia64_sync_i();
	ia64_srlz_i();
}

static void kvm_flush_tlb_all(void)
{
	unsigned long i, j, count0, count1, stride0, stride1, addr;
	long flags;

	addr    = local_cpu_data->ptce_base;
	count0  = local_cpu_data->ptce_count[0];
	count1  = local_cpu_data->ptce_count[1];
	stride0 = local_cpu_data->ptce_stride[0];
	stride1 = local_cpu_data->ptce_stride[1];

	local_irq_save(flags);
	for (i = 0; i < count0; ++i) {
		for (j = 0; j < count1; ++j) {
			ia64_ptce(addr);
			addr += stride1;
		}
		addr += stride0;
	}
	local_irq_restore(flags);
	ia64_srlz_i();			/* srlz.i implies srlz.d */
}

long ia64_pal_vp_create(u64 *vpd, u64 *host_iva, u64 *opt_handler)
{
	struct ia64_pal_retval iprv;

	PAL_CALL_STK(iprv, PAL_VP_CREATE, (u64)vpd, (u64)host_iva,
			(u64)opt_handler);

	return iprv.status;
}

static  DEFINE_SPINLOCK(vp_lock);

int kvm_arch_hardware_enable(void *garbage)
{
	long  status;
	long  tmp_base;
	unsigned long pte;
	unsigned long saved_psr;
	int slot;

	pte = pte_val(mk_pte_phys(__pa(kvm_vmm_base), PAGE_KERNEL));
	local_irq_save(saved_psr);
	slot = ia64_itr_entry(0x3, KVM_VMM_BASE, pte, KVM_VMM_SHIFT);
	local_irq_restore(saved_psr);
	if (slot < 0)
		return -EINVAL;

	spin_lock(&vp_lock);
	status = ia64_pal_vp_init_env(kvm_vsa_base ?
				VP_INIT_ENV : VP_INIT_ENV_INITALIZE,
			__pa(kvm_vm_buffer), KVM_VM_BUFFER_BASE, &tmp_base);
	if (status != 0) {
		printk(KERN_WARNING"kvm: Failed to Enable VT Support!!!!\n");
		return -EINVAL;
	}

	if (!kvm_vsa_base) {
		kvm_vsa_base = tmp_base;
		printk(KERN_INFO"kvm: kvm_vsa_base:0x%lx\n", kvm_vsa_base);
	}
	spin_unlock(&vp_lock);
	ia64_ptr_entry(0x3, slot);

	return 0;
}

void kvm_arch_hardware_disable(void *garbage)
{

	long status;
	int slot;
	unsigned long pte;
	unsigned long saved_psr;
	unsigned long host_iva = ia64_getreg(_IA64_REG_CR_IVA);

	pte = pte_val(mk_pte_phys(__pa(kvm_vmm_base),
				PAGE_KERNEL));

	local_irq_save(saved_psr);
	slot = ia64_itr_entry(0x3, KVM_VMM_BASE, pte, KVM_VMM_SHIFT);
	local_irq_restore(saved_psr);
	if (slot < 0)
		return;

	status = ia64_pal_vp_exit_env(host_iva);
	if (status)
		printk(KERN_DEBUG"kvm: Failed to disable VT support! :%ld\n",
				status);
	ia64_ptr_entry(0x3, slot);
}

void kvm_arch_check_processor_compat(void *rtn)
{
	*(int *)rtn = 0;
}

int kvm_dev_ioctl_check_extension(long ext)
{

	int r;

	switch (ext) {
	case KVM_CAP_IRQCHIP:
	case KVM_CAP_MP_STATE:
	case KVM_CAP_IRQ_INJECT_STATUS:
		r = 1;
		break;
	case KVM_CAP_COALESCED_MMIO:
		r = KVM_COALESCED_MMIO_PAGE_OFFSET;
		break;
	case KVM_CAP_IOMMU:
		r = iommu_found();
		break;
	default:
		r = 0;
	}
	return r;

}

static int handle_vm_error(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	kvm_run->exit_reason = KVM_EXIT_UNKNOWN;
	kvm_run->hw.hardware_exit_reason = 1;
	return 0;
}

static int handle_mmio(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	struct kvm_mmio_req *p;
	struct kvm_io_device *mmio_dev;
	int r;

	p = kvm_get_vcpu_ioreq(vcpu);

	if ((p->addr & PAGE_MASK) == IOAPIC_DEFAULT_BASE_ADDRESS)
		goto mmio;
	vcpu->mmio_needed = 1;
	vcpu->mmio_phys_addr = kvm_run->mmio.phys_addr = p->addr;
	vcpu->mmio_size = kvm_run->mmio.len = p->size;
	vcpu->mmio_is_write = kvm_run->mmio.is_write = !p->dir;

	if (vcpu->mmio_is_write)
		memcpy(vcpu->mmio_data, &p->data, p->size);
	memcpy(kvm_run->mmio.data, &p->data, p->size);
	kvm_run->exit_reason = KVM_EXIT_MMIO;
	return 0;
mmio:
	if (p->dir)
		r = kvm_io_bus_read(&vcpu->kvm->mmio_bus, p->addr,
				    p->size, &p->data);
	else
		r = kvm_io_bus_write(&vcpu->kvm->mmio_bus, p->addr,
				     p->size, &p->data);
	if (r)
		printk(KERN_ERR"kvm: No iodevice found! addr:%lx\n", p->addr);
	p->state = STATE_IORESP_READY;

	return 1;
}

static int handle_pal_call(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	struct exit_ctl_data *p;

	p = kvm_get_exit_data(vcpu);

	if (p->exit_reason == EXIT_REASON_PAL_CALL)
		return kvm_pal_emul(vcpu, kvm_run);
	else {
		kvm_run->exit_reason = KVM_EXIT_UNKNOWN;
		kvm_run->hw.hardware_exit_reason = 2;
		return 0;
	}
}

static int handle_sal_call(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	struct exit_ctl_data *p;

	p = kvm_get_exit_data(vcpu);

	if (p->exit_reason == EXIT_REASON_SAL_CALL) {
		kvm_sal_emul(vcpu);
		return 1;
	} else {
		kvm_run->exit_reason = KVM_EXIT_UNKNOWN;
		kvm_run->hw.hardware_exit_reason = 3;
		return 0;
	}

}

static int __apic_accept_irq(struct kvm_vcpu *vcpu, uint64_t vector)
{
	struct vpd *vpd = to_host(vcpu->kvm, vcpu->arch.vpd);

	if (!test_and_set_bit(vector, &vpd->irr[0])) {
		vcpu->arch.irq_new_pending = 1;
		kvm_vcpu_kick(vcpu);
		return 1;
	}
	return 0;
}

/*
 *  offset: address offset to IPI space.
 *  value:  deliver value.
 */
static void vcpu_deliver_ipi(struct kvm_vcpu *vcpu, uint64_t dm,
				uint64_t vector)
{
	switch (dm) {
	case SAPIC_FIXED:
		break;
	case SAPIC_NMI:
		vector = 2;
		break;
	case SAPIC_EXTINT:
		vector = 0;
		break;
	case SAPIC_INIT:
	case SAPIC_PMI:
	default:
		printk(KERN_ERR"kvm: Unimplemented Deliver reserved IPI!\n");
		return;
	}
	__apic_accept_irq(vcpu, vector);
}

static struct kvm_vcpu *lid_to_vcpu(struct kvm *kvm, unsigned long id,
			unsigned long eid)
{
	union ia64_lid lid;
	int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		lid.val = VCPU_LID(vcpu);
		if (lid.id == id && lid.eid == eid)
			return vcpu;
	}

	return NULL;
}

static int handle_ipi(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	struct exit_ctl_data *p = kvm_get_exit_data(vcpu);
	struct kvm_vcpu *target_vcpu;
	struct kvm_pt_regs *regs;
	union ia64_ipi_a addr = p->u.ipi_data.addr;
	union ia64_ipi_d data = p->u.ipi_data.data;

	target_vcpu = lid_to_vcpu(vcpu->kvm, addr.id, addr.eid);
	if (!target_vcpu)
		return handle_vm_error(vcpu, kvm_run);

	if (!target_vcpu->arch.launched) {
		regs = vcpu_regs(target_vcpu);

		regs->cr_iip = vcpu->kvm->arch.rdv_sal_data.boot_ip;
		regs->r1 = vcpu->kvm->arch.rdv_sal_data.boot_gp;

		target_vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
		if (waitqueue_active(&target_vcpu->wq))
			wake_up_interruptible(&target_vcpu->wq);
	} else {
		vcpu_deliver_ipi(target_vcpu, data.dm, data.vector);
		if (target_vcpu != vcpu)
			kvm_vcpu_kick(target_vcpu);
	}

	return 1;
}

struct call_data {
	struct kvm_ptc_g ptc_g_data;
	struct kvm_vcpu *vcpu;
};

static void vcpu_global_purge(void *info)
{
	struct call_data *p = (struct call_data *)info;
	struct kvm_vcpu *vcpu = p->vcpu;

	if (test_bit(KVM_REQ_TLB_FLUSH, &vcpu->requests))
		return;

	set_bit(KVM_REQ_PTC_G, &vcpu->requests);
	if (vcpu->arch.ptc_g_count < MAX_PTC_G_NUM) {
		vcpu->arch.ptc_g_data[vcpu->arch.ptc_g_count++] =
							p->ptc_g_data;
	} else {
		clear_bit(KVM_REQ_PTC_G, &vcpu->requests);
		vcpu->arch.ptc_g_count = 0;
		set_bit(KVM_REQ_TLB_FLUSH, &vcpu->requests);
	}
}

static int handle_global_purge(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	struct exit_ctl_data *p = kvm_get_exit_data(vcpu);
	struct kvm *kvm = vcpu->kvm;
	struct call_data call_data;
	int i;
	struct kvm_vcpu *vcpui;

	call_data.ptc_g_data = p->u.ptc_g_data;

	kvm_for_each_vcpu(i, vcpui, kvm) {
		if (vcpui->arch.mp_state == KVM_MP_STATE_UNINITIALIZED ||
				vcpu == vcpui)
			continue;

		if (waitqueue_active(&vcpui->wq))
			wake_up_interruptible(&vcpui->wq);

		if (vcpui->cpu != -1) {
			call_data.vcpu = vcpui;
			smp_call_function_single(vcpui->cpu,
					vcpu_global_purge, &call_data, 1);
		} else
			printk(KERN_WARNING"kvm: Uninit vcpu received ipi!\n");

	}
	return 1;
}

static int handle_switch_rr6(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	return 1;
}

static int kvm_sn2_setup_mappings(struct kvm_vcpu *vcpu)
{
	unsigned long pte, rtc_phys_addr, map_addr;
	int slot;

	map_addr = KVM_VMM_BASE + (1UL << KVM_VMM_SHIFT);
	rtc_phys_addr = LOCAL_MMR_OFFSET | SH_RTC;
	pte = pte_val(mk_pte_phys(rtc_phys_addr, PAGE_KERNEL_UC));
	slot = ia64_itr_entry(0x3, map_addr, pte, PAGE_SHIFT);
	vcpu->arch.sn_rtc_tr_slot = slot;
	if (slot < 0) {
		printk(KERN_ERR "Mayday mayday! RTC mapping failed!\n");
		slot = 0;
	}
	return slot;
}

int kvm_emulate_halt(struct kvm_vcpu *vcpu)
{

	ktime_t kt;
	long itc_diff;
	unsigned long vcpu_now_itc;
	unsigned long expires;
	struct hrtimer *p_ht = &vcpu->arch.hlt_timer;
	unsigned long cyc_per_usec = local_cpu_data->cyc_per_usec;
	struct vpd *vpd = to_host(vcpu->kvm, vcpu->arch.vpd);

	if (irqchip_in_kernel(vcpu->kvm)) {

		vcpu_now_itc = kvm_get_itc(vcpu) + vcpu->arch.itc_offset;

		if (time_after(vcpu_now_itc, vpd->itm)) {
			vcpu->arch.timer_check = 1;
			return 1;
		}
		itc_diff = vpd->itm - vcpu_now_itc;
		if (itc_diff < 0)
			itc_diff = -itc_diff;

		expires = div64_u64(itc_diff, cyc_per_usec);
		kt = ktime_set(0, 1000 * expires);

		vcpu->arch.ht_active = 1;
		hrtimer_start(p_ht, kt, HRTIMER_MODE_ABS);

		vcpu->arch.mp_state = KVM_MP_STATE_HALTED;
		kvm_vcpu_block(vcpu);
		hrtimer_cancel(p_ht);
		vcpu->arch.ht_active = 0;

		if (test_and_clear_bit(KVM_REQ_UNHALT, &vcpu->requests) ||
				kvm_cpu_has_pending_timer(vcpu))
			if (vcpu->arch.mp_state == KVM_MP_STATE_HALTED)
				vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;

		if (vcpu->arch.mp_state != KVM_MP_STATE_RUNNABLE)
			return -EINTR;
		return 1;
	} else {
		printk(KERN_ERR"kvm: Unsupported userspace halt!");
		return 0;
	}
}

static int handle_vm_shutdown(struct kvm_vcpu *vcpu,
		struct kvm_run *kvm_run)
{
	kvm_run->exit_reason = KVM_EXIT_SHUTDOWN;
	return 0;
}

static int handle_external_interrupt(struct kvm_vcpu *vcpu,
		struct kvm_run *kvm_run)
{
	return 1;
}

static int handle_vcpu_debug(struct kvm_vcpu *vcpu,
				struct kvm_run *kvm_run)
{
	printk("VMM: %s", vcpu->arch.log_buf);
	return 1;
}

static int (*kvm_vti_exit_handlers[])(struct kvm_vcpu *vcpu,
		struct kvm_run *kvm_run) = {
	[EXIT_REASON_VM_PANIC]              = handle_vm_error,
	[EXIT_REASON_MMIO_INSTRUCTION]      = handle_mmio,
	[EXIT_REASON_PAL_CALL]              = handle_pal_call,
	[EXIT_REASON_SAL_CALL]              = handle_sal_call,
	[EXIT_REASON_SWITCH_RR6]            = handle_switch_rr6,
	[EXIT_REASON_VM_DESTROY]            = handle_vm_shutdown,
	[EXIT_REASON_EXTERNAL_INTERRUPT]    = handle_external_interrupt,
	[EXIT_REASON_IPI]		    = handle_ipi,
	[EXIT_REASON_PTC_G]		    = handle_global_purge,
	[EXIT_REASON_DEBUG]		    = handle_vcpu_debug,

};

static const int kvm_vti_max_exit_handlers =
		sizeof(kvm_vti_exit_handlers)/sizeof(*kvm_vti_exit_handlers);

static uint32_t kvm_get_exit_reason(struct kvm_vcpu *vcpu)
{
	struct exit_ctl_data *p_exit_data;

	p_exit_data = kvm_get_exit_data(vcpu);
	return p_exit_data->exit_reason;
}

/*
 * The guest has exited.  See if we can fix it or if we need userspace
 * assistance.
 */
static int kvm_handle_exit(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	u32 exit_reason = kvm_get_exit_reason(vcpu);
	vcpu->arch.last_exit = exit_reason;

	if (exit_reason < kvm_vti_max_exit_handlers
			&& kvm_vti_exit_handlers[exit_reason])
		return kvm_vti_exit_handlers[exit_reason](vcpu, kvm_run);
	else {
		kvm_run->exit_reason = KVM_EXIT_UNKNOWN;
		kvm_run->hw.hardware_exit_reason = exit_reason;
	}
	return 0;
}

static inline void vti_set_rr6(unsigned long rr6)
{
	ia64_set_rr(RR6, rr6);
	ia64_srlz_i();
}

static int kvm_insert_vmm_mapping(struct kvm_vcpu *vcpu)
{
	unsigned long pte;
	struct kvm *kvm = vcpu->kvm;
	int r;

	/*Insert a pair of tr to map vmm*/
	pte = pte_val(mk_pte_phys(__pa(kvm_vmm_base), PAGE_KERNEL));
	r = ia64_itr_entry(0x3, KVM_VMM_BASE, pte, KVM_VMM_SHIFT);
	if (r < 0)
		goto out;
	vcpu->arch.vmm_tr_slot = r;
	/*Insert a pairt of tr to map data of vm*/
	pte = pte_val(mk_pte_phys(__pa(kvm->arch.vm_base), PAGE_KERNEL));
	r = ia64_itr_entry(0x3, KVM_VM_DATA_BASE,
					pte, KVM_VM_DATA_SHIFT);
	if (r < 0)
		goto out;
	vcpu->arch.vm_tr_slot = r;

#if defined(CONFIG_IA64_SGI_SN2) || defined(CONFIG_IA64_GENERIC)
	if (kvm->arch.is_sn2) {
		r = kvm_sn2_setup_mappings(vcpu);
		if (r < 0)
			goto out;
	}
#endif

	r = 0;
out:
	return r;
}

static void kvm_purge_vmm_mapping(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	ia64_ptr_entry(0x3, vcpu->arch.vmm_tr_slot);
	ia64_ptr_entry(0x3, vcpu->arch.vm_tr_slot);
#if defined(CONFIG_IA64_SGI_SN2) || defined(CONFIG_IA64_GENERIC)
	if (kvm->arch.is_sn2)
		ia64_ptr_entry(0x3, vcpu->arch.sn_rtc_tr_slot);
#endif
}

static int kvm_vcpu_pre_transition(struct kvm_vcpu *vcpu)
{
	unsigned long psr;
	int r;
	int cpu = smp_processor_id();

	if (vcpu->arch.last_run_cpu != cpu ||
			per_cpu(last_vcpu, cpu) != vcpu) {
		per_cpu(last_vcpu, cpu) = vcpu;
		vcpu->arch.last_run_cpu = cpu;
		kvm_flush_tlb_all();
	}

	vcpu->arch.host_rr6 = ia64_get_rr(RR6);
	vti_set_rr6(vcpu->arch.vmm_rr);
	local_irq_save(psr);
	r = kvm_insert_vmm_mapping(vcpu);
	local_irq_restore(psr);
	return r;
}

static void kvm_vcpu_post_transition(struct kvm_vcpu *vcpu)
{
	kvm_purge_vmm_mapping(vcpu);
	vti_set_rr6(vcpu->arch.host_rr6);
}

static int __vcpu_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	union context *host_ctx, *guest_ctx;
	int r;

	/*
	 * down_read() may sleep and return with interrupts enabled
	 */
	down_read(&vcpu->kvm->slots_lock);

again:
	if (signal_pending(current)) {
		r = -EINTR;
		kvm_run->exit_reason = KVM_EXIT_INTR;
		goto out;
	}

	preempt_disable();
	local_irq_disable();

	/*Get host and guest context with guest address space.*/
	host_ctx = kvm_get_host_context(vcpu);
	guest_ctx = kvm_get_guest_context(vcpu);

	clear_bit(KVM_REQ_KICK, &vcpu->requests);

	r = kvm_vcpu_pre_transition(vcpu);
	if (r < 0)
		goto vcpu_run_fail;

	up_read(&vcpu->kvm->slots_lock);
	kvm_guest_enter();

	/*
	 * Transition to the guest
	 */
	kvm_vmm_info->tramp_entry(host_ctx, guest_ctx);

	kvm_vcpu_post_transition(vcpu);

	vcpu->arch.launched = 1;
	set_bit(KVM_REQ_KICK, &vcpu->requests);
	local_irq_enable();

	/*
	 * We must have an instruction between local_irq_enable() and
	 * kvm_guest_exit(), so the timer interrupt isn't delayed by
	 * the interrupt shadow.  The stat.exits increment will do nicely.
	 * But we need to prevent reordering, hence this barrier():
	 */
	barrier();
	kvm_guest_exit();
	preempt_enable();

	down_read(&vcpu->kvm->slots_lock);

	r = kvm_handle_exit(kvm_run, vcpu);

	if (r > 0) {
		if (!need_resched())
			goto again;
	}

out:
	up_read(&vcpu->kvm->slots_lock);
	if (r > 0) {
		kvm_resched(vcpu);
		down_read(&vcpu->kvm->slots_lock);
		goto again;
	}

	return r;

vcpu_run_fail:
	local_irq_enable();
	preempt_enable();
	kvm_run->exit_reason = KVM_EXIT_FAIL_ENTRY;
	goto out;
}

static void kvm_set_mmio_data(struct kvm_vcpu *vcpu)
{
	struct kvm_mmio_req *p = kvm_get_vcpu_ioreq(vcpu);

	if (!vcpu->mmio_is_write)
		memcpy(&p->data, vcpu->mmio_data, 8);
	p->state = STATE_IORESP_READY;
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	int r;
	sigset_t sigsaved;

	vcpu_load(vcpu);

	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &vcpu->sigset, &sigsaved);

	if (unlikely(vcpu->arch.mp_state == KVM_MP_STATE_UNINITIALIZED)) {
		kvm_vcpu_block(vcpu);
		clear_bit(KVM_REQ_UNHALT, &vcpu->requests);
		r = -EAGAIN;
		goto out;
	}

	if (vcpu->mmio_needed) {
		memcpy(vcpu->mmio_data, kvm_run->mmio.data, 8);
		kvm_set_mmio_data(vcpu);
		vcpu->mmio_read_completed = 1;
		vcpu->mmio_needed = 0;
	}
	r = __vcpu_run(vcpu, kvm_run);
out:
	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &sigsaved, NULL);

	vcpu_put(vcpu);
	return r;
}

static struct kvm *kvm_alloc_kvm(void)
{

	struct kvm *kvm;
	uint64_t  vm_base;

	BUG_ON(sizeof(struct kvm) > KVM_VM_STRUCT_SIZE);

	vm_base = __get_free_pages(GFP_KERNEL, get_order(KVM_VM_DATA_SIZE));

	if (!vm_base)
		return ERR_PTR(-ENOMEM);

	memset((void *)vm_base, 0, KVM_VM_DATA_SIZE);
	kvm = (struct kvm *)(vm_base +
			offsetof(struct kvm_vm_data, kvm_vm_struct));
	kvm->arch.vm_base = vm_base;
	printk(KERN_DEBUG"kvm: vm's data area:0x%lx\n", vm_base);

	return kvm;
}

struct kvm_io_range {
	unsigned long start;
	unsigned long size;
	unsigned long type;
};

static const struct kvm_io_range io_ranges[] = {
	{VGA_IO_START, VGA_IO_SIZE, GPFN_FRAME_BUFFER},
	{MMIO_START, MMIO_SIZE, GPFN_LOW_MMIO},
	{LEGACY_IO_START, LEGACY_IO_SIZE, GPFN_LEGACY_IO},
	{IO_SAPIC_START, IO_SAPIC_SIZE, GPFN_IOSAPIC},
	{PIB_START, PIB_SIZE, GPFN_PIB},
};

static void kvm_build_io_pmt(struct kvm *kvm)
{
	unsigned long i, j;

	/* Mark I/O ranges */
	for (i = 0; i < (sizeof(io_ranges) / sizeof(struct kvm_io_range));
							i++) {
		for (j = io_ranges[i].start;
				j < io_ranges[i].start + io_ranges[i].size;
				j += PAGE_SIZE)
			kvm_set_pmt_entry(kvm, j >> PAGE_SHIFT,
					io_ranges[i].type, 0);
	}

}

/*Use unused rids to virtualize guest rid.*/
#define GUEST_PHYSICAL_RR0	0x1739
#define GUEST_PHYSICAL_RR4	0x2739
#define VMM_INIT_RR		0x1660

static void kvm_init_vm(struct kvm *kvm)
{
	BUG_ON(!kvm);

	kvm->arch.metaphysical_rr0 = GUEST_PHYSICAL_RR0;
	kvm->arch.metaphysical_rr4 = GUEST_PHYSICAL_RR4;
	kvm->arch.vmm_init_rr = VMM_INIT_RR;

	/*
	 *Fill P2M entries for MMIO/IO ranges
	 */
	kvm_build_io_pmt(kvm);

	INIT_LIST_HEAD(&kvm->arch.assigned_dev_head);

	/* Reserve bit 0 of irq_sources_bitmap for userspace irq source */
	set_bit(KVM_USERSPACE_IRQ_SOURCE_ID, &kvm->arch.irq_sources_bitmap);
}

struct  kvm *kvm_arch_create_vm(void)
{
	struct kvm *kvm = kvm_alloc_kvm();

	if (IS_ERR(kvm))
		return ERR_PTR(-ENOMEM);

	kvm->arch.is_sn2 = ia64_platform_is("sn2");

	kvm_init_vm(kvm);

	return kvm;

}

static int kvm_vm_ioctl_get_irqchip(struct kvm *kvm,
					struct kvm_irqchip *chip)
{
	int r;

	r = 0;
	switch (chip->chip_id) {
	case KVM_IRQCHIP_IOAPIC:
		r = kvm_get_ioapic(kvm, &chip->chip.ioapic);
		break;
	default:
		r = -EINVAL;
		break;
	}
	return r;
}

static int kvm_vm_ioctl_set_irqchip(struct kvm *kvm, struct kvm_irqchip *chip)
{
	int r;

	r = 0;
	switch (chip->chip_id) {
	case KVM_IRQCHIP_IOAPIC:
		r = kvm_set_ioapic(kvm, &chip->chip.ioapic);
		break;
	default:
		r = -EINVAL;
		break;
	}
	return r;
}

#define RESTORE_REGS(_x) vcpu->arch._x = regs->_x

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	struct vpd *vpd = to_host(vcpu->kvm, vcpu->arch.vpd);
	int i;

	vcpu_load(vcpu);

	for (i = 0; i < 16; i++) {
		vpd->vgr[i] = regs->vpd.vgr[i];
		vpd->vbgr[i] = regs->vpd.vbgr[i];
	}
	for (i = 0; i < 128; i++)
		vpd->vcr[i] = regs->vpd.vcr[i];
	vpd->vhpi = regs->vpd.vhpi;
	vpd->vnat = regs->vpd.vnat;
	vpd->vbnat = regs->vpd.vbnat;
	vpd->vpsr = regs->vpd.vpsr;

	vpd->vpr = regs->vpd.vpr;

	memcpy(&vcpu->arch.guest, &regs->saved_guest, sizeof(union context));

	RESTORE_REGS(mp_state);
	RESTORE_REGS(vmm_rr);
	memcpy(vcpu->arch.itrs, regs->itrs, sizeof(struct thash_data) * NITRS);
	memcpy(vcpu->arch.dtrs, regs->dtrs, sizeof(struct thash_data) * NDTRS);
	RESTORE_REGS(itr_regions);
	RESTORE_REGS(dtr_regions);
	RESTORE_REGS(tc_regions);
	RESTORE_REGS(irq_check);
	RESTORE_REGS(itc_check);
	RESTORE_REGS(timer_check);
	RESTORE_REGS(timer_pending);
	RESTORE_REGS(last_itc);
	for (i = 0; i < 8; i++) {
		vcpu->arch.vrr[i] = regs->vrr[i];
		vcpu->arch.ibr[i] = regs->ibr[i];
		vcpu->arch.dbr[i] = regs->dbr[i];
	}
	for (i = 0; i < 4; i++)
		vcpu->arch.insvc[i] = regs->insvc[i];
	RESTORE_REGS(xtp);
	RESTORE_REGS(metaphysical_rr0);
	RESTORE_REGS(metaphysical_rr4);
	RESTORE_REGS(metaphysical_saved_rr0);
	RESTORE_REGS(metaphysical_saved_rr4);
	RESTORE_REGS(fp_psr);
	RESTORE_REGS(saved_gp);

	vcpu->arch.irq_new_pending = 1;
	vcpu->arch.itc_offset = regs->saved_itc - kvm_get_itc(vcpu);
	set_bit(KVM_REQ_RESUME, &vcpu->requests);

	vcpu_put(vcpu);

	return 0;
}

long kvm_arch_vm_ioctl(struct file *filp,
		unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r = -ENOTTY;

	switch (ioctl) {
	case KVM_SET_MEMORY_REGION: {
		struct kvm_memory_region kvm_mem;
		struct kvm_userspace_memory_region kvm_userspace_mem;

		r = -EFAULT;
		if (copy_from_user(&kvm_mem, argp, sizeof kvm_mem))
			goto out;
		kvm_userspace_mem.slot = kvm_mem.slot;
		kvm_userspace_mem.flags = kvm_mem.flags;
		kvm_userspace_mem.guest_phys_addr =
					kvm_mem.guest_phys_addr;
		kvm_userspace_mem.memory_size = kvm_mem.memory_size;
		r = kvm_vm_ioctl_set_memory_region(kvm,
					&kvm_userspace_mem, 0);
		if (r)
			goto out;
		break;
		}
	case KVM_CREATE_IRQCHIP:
		r = -EFAULT;
		r = kvm_ioapic_init(kvm);
		if (r)
			goto out;
		r = kvm_setup_default_irq_routing(kvm);
		if (r) {
			kfree(kvm->arch.vioapic);
			goto out;
		}
		break;
	case KVM_IRQ_LINE_STATUS:
	case KVM_IRQ_LINE: {
		struct kvm_irq_level irq_event;

		r = -EFAULT;
		if (copy_from_user(&irq_event, argp, sizeof irq_event))
			goto out;
		if (irqchip_in_kernel(kvm)) {
			__s32 status;
			status = kvm_set_irq(kvm, KVM_USERSPACE_IRQ_SOURCE_ID,
				    irq_event.irq, irq_event.level);
			if (ioctl == KVM_IRQ_LINE_STATUS) {
				irq_event.status = status;
				if (copy_to_user(argp, &irq_event,
							sizeof irq_event))
					goto out;
			}
			r = 0;
		}
		break;
		}
	case KVM_GET_IRQCHIP: {
		/* 0: PIC master, 1: PIC slave, 2: IOAPIC */
		struct kvm_irqchip chip;

		r = -EFAULT;
		if (copy_from_user(&chip, argp, sizeof chip))
				goto out;
		r = -ENXIO;
		if (!irqchip_in_kernel(kvm))
			goto out;
		r = kvm_vm_ioctl_get_irqchip(kvm, &chip);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &chip, sizeof chip))
				goto out;
		r = 0;
		break;
		}
	case KVM_SET_IRQCHIP: {
		/* 0: PIC master, 1: PIC slave, 2: IOAPIC */
		struct kvm_irqchip chip;

		r = -EFAULT;
		if (copy_from_user(&chip, argp, sizeof chip))
				goto out;
		r = -ENXIO;
		if (!irqchip_in_kernel(kvm))
			goto out;
		r = kvm_vm_ioctl_set_irqchip(kvm, &chip);
		if (r)
			goto out;
		r = 0;
		break;
		}
	default:
		;
	}
out:
	return r;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
		struct kvm_sregs *sregs)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
		struct kvm_sregs *sregs)
{
	return -EINVAL;

}
int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
		struct kvm_translation *tr)
{

	return -EINVAL;
}

static int kvm_alloc_vmm_area(void)
{
	if (!kvm_vmm_base && (kvm_vm_buffer_size < KVM_VM_BUFFER_SIZE)) {
		kvm_vmm_base = __get_free_pages(GFP_KERNEL,
				get_order(KVM_VMM_SIZE));
		if (!kvm_vmm_base)
			return -ENOMEM;

		memset((void *)kvm_vmm_base, 0, KVM_VMM_SIZE);
		kvm_vm_buffer = kvm_vmm_base + VMM_SIZE;

		printk(KERN_DEBUG"kvm:VMM's Base Addr:0x%lx, vm_buffer:0x%lx\n",
				kvm_vmm_base, kvm_vm_buffer);
	}

	return 0;
}

static void kvm_free_vmm_area(void)
{
	if (kvm_vmm_base) {
		/*Zero this area before free to avoid bits leak!!*/
		memset((void *)kvm_vmm_base, 0, KVM_VMM_SIZE);
		free_pages(kvm_vmm_base, get_order(KVM_VMM_SIZE));
		kvm_vmm_base  = 0;
		kvm_vm_buffer = 0;
		kvm_vsa_base = 0;
	}
}

static int vti_init_vpd(struct kvm_vcpu *vcpu)
{
	int i;
	union cpuid3_t cpuid3;
	struct vpd *vpd = to_host(vcpu->kvm, vcpu->arch.vpd);

	if (IS_ERR(vpd))
		return PTR_ERR(vpd);

	/* CPUID init */
	for (i = 0; i < 5; i++)
		vpd->vcpuid[i] = ia64_get_cpuid(i);

	/* Limit the CPUID number to 5 */
	cpuid3.value = vpd->vcpuid[3];
	cpuid3.number = 4;	/* 5 - 1 */
	vpd->vcpuid[3] = cpuid3.value;

	/*Set vac and vdc fields*/
	vpd->vac.a_from_int_cr = 1;
	vpd->vac.a_to_int_cr = 1;
	vpd->vac.a_from_psr = 1;
	vpd->vac.a_from_cpuid = 1;
	vpd->vac.a_cover = 1;
	vpd->vac.a_bsw = 1;
	vpd->vac.a_int = 1;
	vpd->vdc.d_vmsw = 1;

	/*Set virtual buffer*/
	vpd->virt_env_vaddr = KVM_VM_BUFFER_BASE;

	return 0;
}

static int vti_create_vp(struct kvm_vcpu *vcpu)
{
	long ret;
	struct vpd *vpd = vcpu->arch.vpd;
	unsigned long  vmm_ivt;

	vmm_ivt = kvm_vmm_info->vmm_ivt;

	printk(KERN_DEBUG "kvm: vcpu:%p,ivt: 0x%lx\n", vcpu, vmm_ivt);

	ret = ia64_pal_vp_create((u64 *)vpd, (u64 *)vmm_ivt, 0);

	if (ret) {
		printk(KERN_ERR"kvm: ia64_pal_vp_create failed!\n");
		return -EINVAL;
	}
	return 0;
}

static void init_ptce_info(struct kvm_vcpu *vcpu)
{
	ia64_ptce_info_t ptce = {0};

	ia64_get_ptce(&ptce);
	vcpu->arch.ptce_base = ptce.base;
	vcpu->arch.ptce_count[0] = ptce.count[0];
	vcpu->arch.ptce_count[1] = ptce.count[1];
	vcpu->arch.ptce_stride[0] = ptce.stride[0];
	vcpu->arch.ptce_stride[1] = ptce.stride[1];
}

static void kvm_migrate_hlt_timer(struct kvm_vcpu *vcpu)
{
	struct hrtimer *p_ht = &vcpu->arch.hlt_timer;

	if (hrtimer_cancel(p_ht))
		hrtimer_start_expires(p_ht, HRTIMER_MODE_ABS);
}

static enum hrtimer_restart hlt_timer_fn(struct hrtimer *data)
{
	struct kvm_vcpu *vcpu;
	wait_queue_head_t *q;

	vcpu  = container_of(data, struct kvm_vcpu, arch.hlt_timer);
	q = &vcpu->wq;

	if (vcpu->arch.mp_state != KVM_MP_STATE_HALTED)
		goto out;

	if (waitqueue_active(q))
		wake_up_interruptible(q);

out:
	vcpu->arch.timer_fired = 1;
	vcpu->arch.timer_check = 1;
	return HRTIMER_NORESTART;
}

#define PALE_RESET_ENTRY    0x80000000ffffffb0UL

int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu *v;
	int r;
	int i;
	long itc_offset;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_pt_regs *regs = vcpu_regs(vcpu);

	union context *p_ctx = &vcpu->arch.guest;
	struct kvm_vcpu *vmm_vcpu = to_guest(vcpu->kvm, vcpu);

	/*Init vcpu context for first run.*/
	if (IS_ERR(vmm_vcpu))
		return PTR_ERR(vmm_vcpu);

	if (kvm_vcpu_is_bsp(vcpu)) {
		vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;

		/*Set entry address for first run.*/
		regs->cr_iip = PALE_RESET_ENTRY;

		/*Initialize itc offset for vcpus*/
		itc_offset = 0UL - kvm_get_itc(vcpu);
		for (i = 0; i < KVM_MAX_VCPUS; i++) {
			v = (struct kvm_vcpu *)((char *)vcpu +
					sizeof(struct kvm_vcpu_data) * i);
			v->arch.itc_offset = itc_offset;
			v->arch.last_itc = 0;
		}
	} else
		vcpu->arch.mp_state = KVM_MP_STATE_UNINITIALIZED;

	r = -ENOMEM;
	vcpu->arch.apic = kzalloc(sizeof(struct kvm_lapic), GFP_KERNEL);
	if (!vcpu->arch.apic)
		goto out;
	vcpu->arch.apic->vcpu = vcpu;

	p_ctx->gr[1] = 0;
	p_ctx->gr[12] = (unsigned long)((char *)vmm_vcpu + KVM_STK_OFFSET);
	p_ctx->gr[13] = (unsigned long)vmm_vcpu;
	p_ctx->psr = 0x1008522000UL;
	p_ctx->ar[40] = FPSR_DEFAULT; /*fpsr*/
	p_ctx->caller_unat = 0;
	p_ctx->pr = 0x0;
	p_ctx->ar[36] = 0x0; /*unat*/
	p_ctx->ar[19] = 0x0; /*rnat*/
	p_ctx->ar[18] = (unsigned long)vmm_vcpu +
				((sizeof(struct kvm_vcpu)+15) & ~15);
	p_ctx->ar[64] = 0x0; /*pfs*/
	p_ctx->cr[0] = 0x7e04UL;
	p_ctx->cr[2] = (unsigned long)kvm_vmm_info->vmm_ivt;
	p_ctx->cr[8] = 0x3c;

	/*Initilize region register*/
	p_ctx->rr[0] = 0x30;
	p_ctx->rr[1] = 0x30;
	p_ctx->rr[2] = 0x30;
	p_ctx->rr[3] = 0x30;
	p_ctx->rr[4] = 0x30;
	p_ctx->rr[5] = 0x30;
	p_ctx->rr[7] = 0x30;

	/*Initilize branch register 0*/
	p_ctx->br[0] = *(unsigned long *)kvm_vmm_info->vmm_entry;

	vcpu->arch.vmm_rr = kvm->arch.vmm_init_rr;
	vcpu->arch.metaphysical_rr0 = kvm->arch.metaphysical_rr0;
	vcpu->arch.metaphysical_rr4 = kvm->arch.metaphysical_rr4;

	hrtimer_init(&vcpu->arch.hlt_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	vcpu->arch.hlt_timer.function = hlt_timer_fn;

	vcpu->arch.last_run_cpu = -1;
	vcpu->arch.vpd = (struct vpd *)VPD_BASE(vcpu->vcpu_id);
	vcpu->arch.vsa_base = kvm_vsa_base;
	vcpu->arch.__gp = kvm_vmm_gp;
	vcpu->arch.dirty_log_lock_pa = __pa(&kvm->arch.dirty_log_lock);
	vcpu->arch.vhpt.hash = (struct thash_data *)VHPT_BASE(vcpu->vcpu_id);
	vcpu->arch.vtlb.hash = (struct thash_data *)VTLB_BASE(vcpu->vcpu_id);
	init_ptce_info(vcpu);

	r = 0;
out:
	return r;
}

static int vti_vcpu_setup(struct kvm_vcpu *vcpu, int id)
{
	unsigned long psr;
	int r;

	local_irq_save(psr);
	r = kvm_insert_vmm_mapping(vcpu);
	local_irq_restore(psr);
	if (r)
		goto fail;
	r = kvm_vcpu_init(vcpu, vcpu->kvm, id);
	if (r)
		goto fail;

	r = vti_init_vpd(vcpu);
	if (r) {
		printk(KERN_DEBUG"kvm: vpd init error!!\n");
		goto uninit;
	}

	r = vti_create_vp(vcpu);
	if (r)
		goto uninit;

	kvm_purge_vmm_mapping(vcpu);

	return 0;
uninit:
	kvm_vcpu_uninit(vcpu);
fail:
	return r;
}

struct kvm_vcpu *kvm_arch_vcpu_create(struct kvm *kvm,
		unsigned int id)
{
	struct kvm_vcpu *vcpu;
	unsigned long vm_base = kvm->arch.vm_base;
	int r;
	int cpu;

	BUG_ON(sizeof(struct kvm_vcpu) > VCPU_STRUCT_SIZE/2);

	r = -EINVAL;
	if (id >= KVM_MAX_VCPUS) {
		printk(KERN_ERR"kvm: Can't configure vcpus > %ld",
				KVM_MAX_VCPUS);
		goto fail;
	}

	r = -ENOMEM;
	if (!vm_base) {
		printk(KERN_ERR"kvm: Create vcpu[%d] error!\n", id);
		goto fail;
	}
	vcpu = (struct kvm_vcpu *)(vm_base + offsetof(struct kvm_vm_data,
					vcpu_data[id].vcpu_struct));
	vcpu->kvm = kvm;

	cpu = get_cpu();
	r = vti_vcpu_setup(vcpu, id);
	put_cpu();

	if (r) {
		printk(KERN_DEBUG"kvm: vcpu_setup error!!\n");
		goto fail;
	}

	return vcpu;
fail:
	return ERR_PTR(r);
}

int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	return 0;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	return -EINVAL;
}

static void free_kvm(struct kvm *kvm)
{
	unsigned long vm_base = kvm->arch.vm_base;

	if (vm_base) {
		memset((void *)vm_base, 0, KVM_VM_DATA_SIZE);
		free_pages(vm_base, get_order(KVM_VM_DATA_SIZE));
	}

}

static void kvm_release_vm_pages(struct kvm *kvm)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int i, j;
	unsigned long base_gfn;

	slots = kvm->memslots;
	for (i = 0; i < slots->nmemslots; i++) {
		memslot = &slots->memslots[i];
		base_gfn = memslot->base_gfn;

		for (j = 0; j < memslot->npages; j++) {
			if (memslot->rmap[j])
				put_page((struct page *)memslot->rmap[j]);
		}
	}
}

void kvm_arch_sync_events(struct kvm *kvm)
{
}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	kvm_iommu_unmap_guest(kvm);
#ifdef  KVM_CAP_DEVICE_ASSIGNMENT
	kvm_free_all_assigned_devices(kvm);
#endif
	kfree(kvm->arch.vioapic);
	kvm_release_vm_pages(kvm);
	kvm_free_physmem(kvm);
	free_kvm(kvm);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	if (cpu != vcpu->cpu) {
		vcpu->cpu = cpu;
		if (vcpu->arch.ht_active)
			kvm_migrate_hlt_timer(vcpu);
	}
}

#define SAVE_REGS(_x) 	regs->_x = vcpu->arch._x

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	struct vpd *vpd = to_host(vcpu->kvm, vcpu->arch.vpd);
	int i;

	vcpu_load(vcpu);

	for (i = 0; i < 16; i++) {
		regs->vpd.vgr[i] = vpd->vgr[i];
		regs->vpd.vbgr[i] = vpd->vbgr[i];
	}
	for (i = 0; i < 128; i++)
		regs->vpd.vcr[i] = vpd->vcr[i];
	regs->vpd.vhpi = vpd->vhpi;
	regs->vpd.vnat = vpd->vnat;
	regs->vpd.vbnat = vpd->vbnat;
	regs->vpd.vpsr = vpd->vpsr;
	regs->vpd.vpr = vpd->vpr;

	memcpy(&regs->saved_guest, &vcpu->arch.guest, sizeof(union context));

	SAVE_REGS(mp_state);
	SAVE_REGS(vmm_rr);
	memcpy(regs->itrs, vcpu->arch.itrs, sizeof(struct thash_data) * NITRS);
	memcpy(regs->dtrs, vcpu->arch.dtrs, sizeof(struct thash_data) * NDTRS);
	SAVE_REGS(itr_regions);
	SAVE_REGS(dtr_regions);
	SAVE_REGS(tc_regions);
	SAVE_REGS(irq_check);
	SAVE_REGS(itc_check);
	SAVE_REGS(timer_check);
	SAVE_REGS(timer_pending);
	SAVE_REGS(last_itc);
	for (i = 0; i < 8; i++) {
		regs->vrr[i] = vcpu->arch.vrr[i];
		regs->ibr[i] = vcpu->arch.ibr[i];
		regs->dbr[i] = vcpu->arch.dbr[i];
	}
	for (i = 0; i < 4; i++)
		regs->insvc[i] = vcpu->arch.insvc[i];
	regs->saved_itc = vcpu->arch.itc_offset + kvm_get_itc(vcpu);
	SAVE_REGS(xtp);
	SAVE_REGS(metaphysical_rr0);
	SAVE_REGS(metaphysical_rr4);
	SAVE_REGS(metaphysical_saved_rr0);
	SAVE_REGS(metaphysical_saved_rr4);
	SAVE_REGS(fp_psr);
	SAVE_REGS(saved_gp);

	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_get_stack(struct kvm_vcpu *vcpu,
				  struct kvm_ia64_vcpu_stack *stack)
{
	memcpy(stack, vcpu, sizeof(struct kvm_ia64_vcpu_stack));
	return 0;
}

int kvm_arch_vcpu_ioctl_set_stack(struct kvm_vcpu *vcpu,
				  struct kvm_ia64_vcpu_stack *stack)
{
	memcpy(vcpu + 1, &stack->stack[0] + sizeof(struct kvm_vcpu),
	       sizeof(struct kvm_ia64_vcpu_stack) - sizeof(struct kvm_vcpu));

	vcpu->arch.exit_data = ((struct kvm_vcpu *)stack)->arch.exit_data;
	return 0;
}

void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu)
{

	hrtimer_cancel(&vcpu->arch.hlt_timer);
	kfree(vcpu->arch.apic);
}


long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	struct kvm_ia64_vcpu_stack *stack = NULL;
	long r;

	switch (ioctl) {
	case KVM_IA64_VCPU_GET_STACK: {
		struct kvm_ia64_vcpu_stack __user *user_stack;
	        void __user *first_p = argp;

		r = -EFAULT;
		if (copy_from_user(&user_stack, first_p, sizeof(void *)))
			goto out;

		if (!access_ok(VERIFY_WRITE, user_stack,
			       sizeof(struct kvm_ia64_vcpu_stack))) {
			printk(KERN_INFO "KVM_IA64_VCPU_GET_STACK: "
			       "Illegal user destination address for stack\n");
			goto out;
		}
		stack = kzalloc(sizeof(struct kvm_ia64_vcpu_stack), GFP_KERNEL);
		if (!stack) {
			r = -ENOMEM;
			goto out;
		}

		r = kvm_arch_vcpu_ioctl_get_stack(vcpu, stack);
		if (r)
			goto out;

		if (copy_to_user(user_stack, stack,
				 sizeof(struct kvm_ia64_vcpu_stack)))
			goto out;

		break;
	}
	case KVM_IA64_VCPU_SET_STACK: {
		struct kvm_ia64_vcpu_stack __user *user_stack;
	        void __user *first_p = argp;

		r = -EFAULT;
		if (copy_from_user(&user_stack, first_p, sizeof(void *)))
			goto out;

		if (!access_ok(VERIFY_READ, user_stack,
			    sizeof(struct kvm_ia64_vcpu_stack))) {
			printk(KERN_INFO "KVM_IA64_VCPU_SET_STACK: "
			       "Illegal user address for stack\n");
			goto out;
		}
		stack = kmalloc(sizeof(struct kvm_ia64_vcpu_stack), GFP_KERNEL);
		if (!stack) {
			r = -ENOMEM;
			goto out;
		}
		if (copy_from_user(stack, user_stack,
				   sizeof(struct kvm_ia64_vcpu_stack)))
			goto out;

		r = kvm_arch_vcpu_ioctl_set_stack(vcpu, stack);
		break;
	}

	default:
		r = -EINVAL;
	}

out:
	kfree(stack);
	return r;
}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
		struct kvm_memory_slot *memslot,
		struct kvm_memory_slot old,
		struct kvm_userspace_memory_region *mem,
		int user_alloc)
{
	unsigned long i;
	unsigned long pfn;
	int npages = memslot->npages;
	unsigned long base_gfn = memslot->base_gfn;

	if (base_gfn + npages > (KVM_MAX_MEM_SIZE >> PAGE_SHIFT))
		return -ENOMEM;

	for (i = 0; i < npages; i++) {
		pfn = gfn_to_pfn(kvm, base_gfn + i);
		if (!kvm_is_mmio_pfn(pfn)) {
			kvm_set_pmt_entry(kvm, base_gfn + i,
					pfn << PAGE_SHIFT,
				_PAGE_AR_RWX | _PAGE_MA_WB);
			memslot->rmap[i] = (unsigned long)pfn_to_page(pfn);
		} else {
			kvm_set_pmt_entry(kvm, base_gfn + i,
					GPFN_PHYS_MMIO | (pfn << PAGE_SHIFT),
					_PAGE_MA_UC);
			memslot->rmap[i] = 0;
			}
	}

	return 0;
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
		struct kvm_userspace_memory_region *mem,
		struct kvm_memory_slot old,
		int user_alloc)
{
	return;
}

void kvm_arch_flush_shadow(struct kvm *kvm)
{
	kvm_flush_remote_tlbs(kvm);
}

long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg)
{
	return -EINVAL;
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	kvm_vcpu_uninit(vcpu);
}

static int vti_cpu_has_kvm_support(void)
{
	long  avail = 1, status = 1, control = 1;
	long ret;

	ret = ia64_pal_proc_get_features(&avail, &status, &control, 0);
	if (ret)
		goto out;

	if (!(avail & PAL_PROC_VM_BIT))
		goto out;

	printk(KERN_DEBUG"kvm: Hardware Supports VT\n");

	ret = ia64_pal_vp_env_info(&kvm_vm_buffer_size, &vp_env_info);
	if (ret)
		goto out;
	printk(KERN_DEBUG"kvm: VM Buffer Size:0x%lx\n", kvm_vm_buffer_size);

	if (!(vp_env_info & VP_OPCODE)) {
		printk(KERN_WARNING"kvm: No opcode ability on hardware, "
				"vm_env_info:0x%lx\n", vp_env_info);
	}

	return 1;
out:
	return 0;
}


/*
 * On SN2, the ITC isn't stable, so copy in fast path code to use the
 * SN2 RTC, replacing the ITC based default verion.
 */
static void kvm_patch_vmm(struct kvm_vmm_info *vmm_info,
			  struct module *module)
{
	unsigned long new_ar, new_ar_sn2;
	unsigned long module_base;

	if (!ia64_platform_is("sn2"))
		return;

	module_base = (unsigned long)module->module_core;

	new_ar = kvm_vmm_base + vmm_info->patch_mov_ar - module_base;
	new_ar_sn2 = kvm_vmm_base + vmm_info->patch_mov_ar_sn2 - module_base;

	printk(KERN_INFO "kvm: Patching ITC emulation to use SGI SN2 RTC "
	       "as source\n");

	/*
	 * Copy the SN2 version of mov_ar into place. They are both
	 * the same size, so 6 bundles is sufficient (6 * 0x10).
	 */
	memcpy((void *)new_ar, (void *)new_ar_sn2, 0x60);
}

static int kvm_relocate_vmm(struct kvm_vmm_info *vmm_info,
			    struct module *module)
{
	unsigned long module_base;
	unsigned long vmm_size;

	unsigned long vmm_offset, func_offset, fdesc_offset;
	struct fdesc *p_fdesc;

	BUG_ON(!module);

	if (!kvm_vmm_base) {
		printk("kvm: kvm area hasn't been initilized yet!!\n");
		return -EFAULT;
	}

	/*Calculate new position of relocated vmm module.*/
	module_base = (unsigned long)module->module_core;
	vmm_size = module->core_size;
	if (unlikely(vmm_size > KVM_VMM_SIZE))
		return -EFAULT;

	memcpy((void *)kvm_vmm_base, (void *)module_base, vmm_size);
	kvm_patch_vmm(vmm_info, module);
	kvm_flush_icache(kvm_vmm_base, vmm_size);

	/*Recalculate kvm_vmm_info based on new VMM*/
	vmm_offset = vmm_info->vmm_ivt - module_base;
	kvm_vmm_info->vmm_ivt = KVM_VMM_BASE + vmm_offset;
	printk(KERN_DEBUG"kvm: Relocated VMM's IVT Base Addr:%lx\n",
			kvm_vmm_info->vmm_ivt);

	fdesc_offset = (unsigned long)vmm_info->vmm_entry - module_base;
	kvm_vmm_info->vmm_entry = (kvm_vmm_entry *)(KVM_VMM_BASE +
							fdesc_offset);
	func_offset = *(unsigned long *)vmm_info->vmm_entry - module_base;
	p_fdesc = (struct fdesc *)(kvm_vmm_base + fdesc_offset);
	p_fdesc->ip = KVM_VMM_BASE + func_offset;
	p_fdesc->gp = KVM_VMM_BASE+(p_fdesc->gp - module_base);

	printk(KERN_DEBUG"kvm: Relocated VMM's Init Entry Addr:%lx\n",
			KVM_VMM_BASE+func_offset);

	fdesc_offset = (unsigned long)vmm_info->tramp_entry - module_base;
	kvm_vmm_info->tramp_entry = (kvm_tramp_entry *)(KVM_VMM_BASE +
			fdesc_offset);
	func_offset = *(unsigned long *)vmm_info->tramp_entry - module_base;
	p_fdesc = (struct fdesc *)(kvm_vmm_base + fdesc_offset);
	p_fdesc->ip = KVM_VMM_BASE + func_offset;
	p_fdesc->gp = KVM_VMM_BASE + (p_fdesc->gp - module_base);

	kvm_vmm_gp = p_fdesc->gp;

	printk(KERN_DEBUG"kvm: Relocated VMM's Entry IP:%p\n",
						kvm_vmm_info->vmm_entry);
	printk(KERN_DEBUG"kvm: Relocated VMM's Trampoline Entry IP:0x%lx\n",
						KVM_VMM_BASE + func_offset);

	return 0;
}

int kvm_arch_init(void *opaque)
{
	int r;
	struct kvm_vmm_info *vmm_info = (struct kvm_vmm_info *)opaque;

	if (!vti_cpu_has_kvm_support()) {
		printk(KERN_ERR "kvm: No Hardware Virtualization Support!\n");
		r = -EOPNOTSUPP;
		goto out;
	}

	if (kvm_vmm_info) {
		printk(KERN_ERR "kvm: Already loaded VMM module!\n");
		r = -EEXIST;
		goto out;
	}

	r = -ENOMEM;
	kvm_vmm_info = kzalloc(sizeof(struct kvm_vmm_info), GFP_KERNEL);
	if (!kvm_vmm_info)
		goto out;

	if (kvm_alloc_vmm_area())
		goto out_free0;

	r = kvm_relocate_vmm(vmm_info, vmm_info->module);
	if (r)
		goto out_free1;

	return 0;

out_free1:
	kvm_free_vmm_area();
out_free0:
	kfree(kvm_vmm_info);
out:
	return r;
}

void kvm_arch_exit(void)
{
	kvm_free_vmm_area();
	kfree(kvm_vmm_info);
	kvm_vmm_info = NULL;
}

static int kvm_ia64_sync_dirty_log(struct kvm *kvm,
		struct kvm_dirty_log *log)
{
	struct kvm_memory_slot *memslot;
	int r, i;
	long n, base;
	unsigned long *dirty_bitmap = (unsigned long *)(kvm->arch.vm_base +
			offsetof(struct kvm_vm_data, kvm_mem_dirty_log));

	r = -EINVAL;
	if (log->slot >= KVM_MEMORY_SLOTS)
		goto out;

	memslot = &kvm->memslots->memslots[log->slot];
	r = -ENOENT;
	if (!memslot->dirty_bitmap)
		goto out;

	n = ALIGN(memslot->npages, BITS_PER_LONG) / 8;
	base = memslot->base_gfn / BITS_PER_LONG;

	for (i = 0; i < n/sizeof(long); ++i) {
		memslot->dirty_bitmap[i] = dirty_bitmap[base + i];
		dirty_bitmap[base + i] = 0;
	}
	r = 0;
out:
	return r;
}

int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm,
		struct kvm_dirty_log *log)
{
	int r;
	int n;
	struct kvm_memory_slot *memslot;
	int is_dirty = 0;

	spin_lock(&kvm->arch.dirty_log_lock);

	r = kvm_ia64_sync_dirty_log(kvm, log);
	if (r)
		goto out;

	r = kvm_get_dirty_log(kvm, log, &is_dirty);
	if (r)
		goto out;

	/* If nothing is dirty, don't bother messing with page tables. */
	if (is_dirty) {
		kvm_flush_remote_tlbs(kvm);
		memslot = &kvm->memslots->memslots[log->slot];
		n = ALIGN(memslot->npages, BITS_PER_LONG) / 8;
		memset(memslot->dirty_bitmap, 0, n);
	}
	r = 0;
out:
	spin_unlock(&kvm->arch.dirty_log_lock);
	return r;
}

int kvm_arch_hardware_setup(void)
{
	return 0;
}

void kvm_arch_hardware_unsetup(void)
{
}

void kvm_vcpu_kick(struct kvm_vcpu *vcpu)
{
	int me;
	int cpu = vcpu->cpu;

	if (waitqueue_active(&vcpu->wq))
		wake_up_interruptible(&vcpu->wq);

	me = get_cpu();
	if (cpu != me && (unsigned) cpu < nr_cpu_ids && cpu_online(cpu))
		if (!test_and_set_bit(KVM_REQ_KICK, &vcpu->requests))
			smp_send_reschedule(cpu);
	put_cpu();
}

int kvm_apic_set_irq(struct kvm_vcpu *vcpu, struct kvm_lapic_irq *irq)
{
	return __apic_accept_irq(vcpu, irq->vector);
}

int kvm_apic_match_physical_addr(struct kvm_lapic *apic, u16 dest)
{
	return apic->vcpu->vcpu_id == dest;
}

int kvm_apic_match_logical_addr(struct kvm_lapic *apic, u8 mda)
{
	return 0;
}

int kvm_apic_compare_prio(struct kvm_vcpu *vcpu1, struct kvm_vcpu *vcpu2)
{
	return vcpu1->arch.xtp - vcpu2->arch.xtp;
}

int kvm_apic_match_dest(struct kvm_vcpu *vcpu, struct kvm_lapic *source,
		int short_hand, int dest, int dest_mode)
{
	struct kvm_lapic *target = vcpu->arch.apic;
	return (dest_mode == 0) ?
		kvm_apic_match_physical_addr(target, dest) :
		kvm_apic_match_logical_addr(target, dest);
}

static int find_highest_bits(int *dat)
{
	u32  bits, bitnum;
	int i;

	/* loop for all 256 bits */
	for (i = 7; i >= 0 ; i--) {
		bits = dat[i];
		if (bits) {
			bitnum = fls(bits);
			return i * 32 + bitnum - 1;
		}
	}

	return -1;
}

int kvm_highest_pending_irq(struct kvm_vcpu *vcpu)
{
    struct vpd *vpd = to_host(vcpu->kvm, vcpu->arch.vpd);

    if (vpd->irr[0] & (1UL << NMI_VECTOR))
		return NMI_VECTOR;
    if (vpd->irr[0] & (1UL << ExtINT_VECTOR))
		return ExtINT_VECTOR;

    return find_highest_bits((int *)&vpd->irr[0]);
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.timer_fired;
}

gfn_t unalias_gfn(struct kvm *kvm, gfn_t gfn)
{
	return gfn;
}

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.mp_state == KVM_MP_STATE_RUNNABLE) ||
		(kvm_highest_pending_irq(vcpu) != -1);
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	vcpu_load(vcpu);
	mp_state->mp_state = vcpu->arch.mp_state;
	vcpu_put(vcpu);
	return 0;
}

static int vcpu_reset(struct kvm_vcpu *vcpu)
{
	int r;
	long psr;
	local_irq_save(psr);
	r = kvm_insert_vmm_mapping(vcpu);
	local_irq_restore(psr);
	if (r)
		goto fail;

	vcpu->arch.launched = 0;
	kvm_arch_vcpu_uninit(vcpu);
	r = kvm_arch_vcpu_init(vcpu);
	if (r)
		goto fail;

	kvm_purge_vmm_mapping(vcpu);
	r = 0;
fail:
	return r;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	int r = 0;

	vcpu_load(vcpu);
	vcpu->arch.mp_state = mp_state->mp_state;
	if (vcpu->arch.mp_state == KVM_MP_STATE_UNINITIALIZED)
		r = vcpu_reset(vcpu);
	vcpu_put(vcpu);
	return r;
}
