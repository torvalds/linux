/*
 * PAL/SAL call delegation
 *
 * Copyright (c) 2004 Li Susie <susie.li@intel.com>
 * Copyright (c) 2005 Yu Ke <ke.yu@intel.com>
 * Copyright (c) 2007 Xiantao Zhang <xiantao.zhang@intel.com>
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
 */

#include <linux/kvm_host.h>
#include <linux/smp.h>
#include <asm/sn/addrs.h>
#include <asm/sn/clksupport.h>
#include <asm/sn/shub_mmr.h>

#include "vti.h"
#include "misc.h"

#include <asm/pal.h>
#include <asm/sal.h>
#include <asm/tlb.h>

/*
 * Handy macros to make sure that the PAL return values start out
 * as something meaningful.
 */
#define INIT_PAL_STATUS_UNIMPLEMENTED(x)		\
	{						\
		x.status = PAL_STATUS_UNIMPLEMENTED;	\
		x.v0 = 0;				\
		x.v1 = 0;				\
		x.v2 = 0;				\
	}

#define INIT_PAL_STATUS_SUCCESS(x)			\
	{						\
		x.status = PAL_STATUS_SUCCESS;		\
		x.v0 = 0;				\
		x.v1 = 0;				\
		x.v2 = 0;				\
    }

static void kvm_get_pal_call_data(struct kvm_vcpu *vcpu,
		u64 *gr28, u64 *gr29, u64 *gr30, u64 *gr31) {
	struct exit_ctl_data *p;

	if (vcpu) {
		p = &vcpu->arch.exit_data;
		if (p->exit_reason == EXIT_REASON_PAL_CALL) {
			*gr28 = p->u.pal_data.gr28;
			*gr29 = p->u.pal_data.gr29;
			*gr30 = p->u.pal_data.gr30;
			*gr31 = p->u.pal_data.gr31;
			return ;
		}
	}
	printk(KERN_DEBUG"Failed to get vcpu pal data!!!\n");
}

static void set_pal_result(struct kvm_vcpu *vcpu,
		struct ia64_pal_retval result) {

	struct exit_ctl_data *p;

	p = kvm_get_exit_data(vcpu);
	if (p->exit_reason == EXIT_REASON_PAL_CALL) {
		p->u.pal_data.ret = result;
		return ;
	}
	INIT_PAL_STATUS_UNIMPLEMENTED(p->u.pal_data.ret);
}

static void set_sal_result(struct kvm_vcpu *vcpu,
		struct sal_ret_values result) {
	struct exit_ctl_data *p;

	p = kvm_get_exit_data(vcpu);
	if (p->exit_reason == EXIT_REASON_SAL_CALL) {
		p->u.sal_data.ret = result;
		return ;
	}
	printk(KERN_WARNING"Failed to set sal result!!\n");
}

struct cache_flush_args {
	u64 cache_type;
	u64 operation;
	u64 progress;
	long status;
};

cpumask_t cpu_cache_coherent_map;

static void remote_pal_cache_flush(void *data)
{
	struct cache_flush_args *args = data;
	long status;
	u64 progress = args->progress;

	status = ia64_pal_cache_flush(args->cache_type, args->operation,
					&progress, NULL);
	if (status != 0)
	args->status = status;
}

static struct ia64_pal_retval pal_cache_flush(struct kvm_vcpu *vcpu)
{
	u64 gr28, gr29, gr30, gr31;
	struct ia64_pal_retval result = {0, 0, 0, 0};
	struct cache_flush_args args = {0, 0, 0, 0};
	long psr;

	gr28 = gr29 = gr30 = gr31 = 0;
	kvm_get_pal_call_data(vcpu, &gr28, &gr29, &gr30, &gr31);

	if (gr31 != 0)
		printk(KERN_ERR"vcpu:%p called cache_flush error!\n", vcpu);

	/* Always call Host Pal in int=1 */
	gr30 &= ~PAL_CACHE_FLUSH_CHK_INTRS;
	args.cache_type = gr29;
	args.operation = gr30;
	smp_call_function(remote_pal_cache_flush,
				(void *)&args, 1);
	if (args.status != 0)
		printk(KERN_ERR"pal_cache_flush error!,"
				"status:0x%lx\n", args.status);
	/*
	 * Call Host PAL cache flush
	 * Clear psr.ic when call PAL_CACHE_FLUSH
	 */
	local_irq_save(psr);
	result.status = ia64_pal_cache_flush(gr29, gr30, &result.v1,
						&result.v0);
	local_irq_restore(psr);
	if (result.status != 0)
		printk(KERN_ERR"vcpu:%p crashed due to cache_flush err:%ld"
				"in1:%lx,in2:%lx\n",
				vcpu, result.status, gr29, gr30);

#if 0
	if (gr29 == PAL_CACHE_TYPE_COHERENT) {
		cpus_setall(vcpu->arch.cache_coherent_map);
		cpu_clear(vcpu->cpu, vcpu->arch.cache_coherent_map);
		cpus_setall(cpu_cache_coherent_map);
		cpu_clear(vcpu->cpu, cpu_cache_coherent_map);
	}
#endif
	return result;
}

struct ia64_pal_retval pal_cache_summary(struct kvm_vcpu *vcpu)
{

	struct ia64_pal_retval result;

	PAL_CALL(result, PAL_CACHE_SUMMARY, 0, 0, 0);
	return result;
}

static struct ia64_pal_retval pal_freq_base(struct kvm_vcpu *vcpu)
{

	struct ia64_pal_retval result;

	PAL_CALL(result, PAL_FREQ_BASE, 0, 0, 0);

	/*
	 * PAL_FREQ_BASE may not be implemented in some platforms,
	 * call SAL instead.
	 */
	if (result.v0 == 0) {
		result.status = ia64_sal_freq_base(SAL_FREQ_BASE_PLATFORM,
							&result.v0,
							&result.v1);
		result.v2 = 0;
	}

	return result;
}

/*
 * On the SGI SN2, the ITC isn't stable. Emulation backed by the SN2
 * RTC is used instead. This function patches the ratios from SAL
 * to match the RTC before providing them to the guest.
 */
static void sn2_patch_itc_freq_ratios(struct ia64_pal_retval *result)
{
	struct pal_freq_ratio *ratio;
	unsigned long sal_freq, sal_drift, factor;

	result->status = ia64_sal_freq_base(SAL_FREQ_BASE_PLATFORM,
					    &sal_freq, &sal_drift);
	ratio = (struct pal_freq_ratio *)&result->v2;
	factor = ((sal_freq * 3) + (sn_rtc_cycles_per_second / 2)) /
		sn_rtc_cycles_per_second;

	ratio->num = 3;
	ratio->den = factor;
}

static struct ia64_pal_retval pal_freq_ratios(struct kvm_vcpu *vcpu)
{
	struct ia64_pal_retval result;

	PAL_CALL(result, PAL_FREQ_RATIOS, 0, 0, 0);

	if (vcpu->kvm->arch.is_sn2)
		sn2_patch_itc_freq_ratios(&result);

	return result;
}

static struct ia64_pal_retval pal_logical_to_physica(struct kvm_vcpu *vcpu)
{
	struct ia64_pal_retval result;

	INIT_PAL_STATUS_UNIMPLEMENTED(result);
	return result;
}

static struct ia64_pal_retval pal_platform_addr(struct kvm_vcpu *vcpu)
{

	struct ia64_pal_retval result;

	INIT_PAL_STATUS_SUCCESS(result);
	return result;
}

static struct ia64_pal_retval pal_proc_get_features(struct kvm_vcpu *vcpu)
{

	struct ia64_pal_retval result = {0, 0, 0, 0};
	long in0, in1, in2, in3;

	kvm_get_pal_call_data(vcpu, &in0, &in1, &in2, &in3);
	result.status = ia64_pal_proc_get_features(&result.v0, &result.v1,
			&result.v2, in2);

	return result;
}

static struct ia64_pal_retval pal_register_info(struct kvm_vcpu *vcpu)
{

	struct ia64_pal_retval result = {0, 0, 0, 0};
	long in0, in1, in2, in3;

	kvm_get_pal_call_data(vcpu, &in0, &in1, &in2, &in3);
	result.status = ia64_pal_register_info(in1, &result.v1, &result.v2);

	return result;
}

static struct ia64_pal_retval pal_cache_info(struct kvm_vcpu *vcpu)
{

	pal_cache_config_info_t ci;
	long status;
	unsigned long in0, in1, in2, in3, r9, r10;

	kvm_get_pal_call_data(vcpu, &in0, &in1, &in2, &in3);
	status = ia64_pal_cache_config_info(in1, in2, &ci);
	r9 = ci.pcci_info_1.pcci1_data;
	r10 = ci.pcci_info_2.pcci2_data;
	return ((struct ia64_pal_retval){status, r9, r10, 0});
}

#define GUEST_IMPL_VA_MSB	59
#define GUEST_RID_BITS		18

static struct ia64_pal_retval pal_vm_summary(struct kvm_vcpu *vcpu)
{

	pal_vm_info_1_u_t vminfo1;
	pal_vm_info_2_u_t vminfo2;
	struct ia64_pal_retval result;

	PAL_CALL(result, PAL_VM_SUMMARY, 0, 0, 0);
	if (!result.status) {
		vminfo1.pvi1_val = result.v0;
		vminfo1.pal_vm_info_1_s.max_itr_entry = 8;
		vminfo1.pal_vm_info_1_s.max_dtr_entry = 8;
		result.v0 = vminfo1.pvi1_val;
		vminfo2.pal_vm_info_2_s.impl_va_msb = GUEST_IMPL_VA_MSB;
		vminfo2.pal_vm_info_2_s.rid_size = GUEST_RID_BITS;
		result.v1 = vminfo2.pvi2_val;
	}

	return result;
}

static struct ia64_pal_retval pal_vm_info(struct kvm_vcpu *vcpu)
{
	struct ia64_pal_retval result;
	unsigned long in0, in1, in2, in3;

	kvm_get_pal_call_data(vcpu, &in0, &in1, &in2, &in3);

	result.status = ia64_pal_vm_info(in1, in2,
			(pal_tc_info_u_t *)&result.v1, &result.v2);

	return result;
}

static  u64 kvm_get_pal_call_index(struct kvm_vcpu *vcpu)
{
	u64 index = 0;
	struct exit_ctl_data *p;

	p = kvm_get_exit_data(vcpu);
	if (p->exit_reason == EXIT_REASON_PAL_CALL)
		index = p->u.pal_data.gr28;

	return index;
}

static void prepare_for_halt(struct kvm_vcpu *vcpu)
{
	vcpu->arch.timer_pending = 1;
	vcpu->arch.timer_fired = 0;
}

static struct ia64_pal_retval pal_perf_mon_info(struct kvm_vcpu *vcpu)
{
	long status;
	unsigned long in0, in1, in2, in3, r9;
	unsigned long pm_buffer[16];

	kvm_get_pal_call_data(vcpu, &in0, &in1, &in2, &in3);
	status = ia64_pal_perf_mon_info(pm_buffer,
				(pal_perf_mon_info_u_t *) &r9);
	if (status != 0) {
		printk(KERN_DEBUG"PAL_PERF_MON_INFO fails ret=%ld\n", status);
	} else {
		if (in1)
			memcpy((void *)in1, pm_buffer, sizeof(pm_buffer));
		else {
			status = PAL_STATUS_EINVAL;
			printk(KERN_WARNING"Invalid parameters "
						"for PAL call:0x%lx!\n", in0);
		}
	}
	return (struct ia64_pal_retval){status, r9, 0, 0};
}

static struct ia64_pal_retval pal_halt_info(struct kvm_vcpu *vcpu)
{
	unsigned long in0, in1, in2, in3;
	long status;
	unsigned long res = 1000UL | (1000UL << 16) | (10UL << 32)
					| (1UL << 61) | (1UL << 60);

	kvm_get_pal_call_data(vcpu, &in0, &in1, &in2, &in3);
	if (in1) {
		memcpy((void *)in1, &res, sizeof(res));
		status = 0;
	} else{
		status = PAL_STATUS_EINVAL;
		printk(KERN_WARNING"Invalid parameters "
					"for PAL call:0x%lx!\n", in0);
	}

	return (struct ia64_pal_retval){status, 0, 0, 0};
}

static struct ia64_pal_retval pal_mem_attrib(struct kvm_vcpu *vcpu)
{
	unsigned long r9;
	long status;

	status = ia64_pal_mem_attrib(&r9);

	return (struct ia64_pal_retval){status, r9, 0, 0};
}

static void remote_pal_prefetch_visibility(void *v)
{
	s64 trans_type = (s64)v;
	ia64_pal_prefetch_visibility(trans_type);
}

static struct ia64_pal_retval pal_prefetch_visibility(struct kvm_vcpu *vcpu)
{
	struct ia64_pal_retval result = {0, 0, 0, 0};
	unsigned long in0, in1, in2, in3;
	kvm_get_pal_call_data(vcpu, &in0, &in1, &in2, &in3);
	result.status = ia64_pal_prefetch_visibility(in1);
	if (result.status == 0) {
		/* Must be performed on all remote processors
		in the coherence domain. */
		smp_call_function(remote_pal_prefetch_visibility,
					(void *)in1, 1);
		/* Unnecessary on remote processor for other vcpus!*/
		result.status = 1;
	}
	return result;
}

static void remote_pal_mc_drain(void *v)
{
	ia64_pal_mc_drain();
}

static struct ia64_pal_retval pal_get_brand_info(struct kvm_vcpu *vcpu)
{
	struct ia64_pal_retval result = {0, 0, 0, 0};
	unsigned long in0, in1, in2, in3;

	kvm_get_pal_call_data(vcpu, &in0, &in1, &in2, &in3);

	if (in1 == 0 && in2) {
		char brand_info[128];
		result.status = ia64_pal_get_brand_info(brand_info);
		if (result.status == PAL_STATUS_SUCCESS)
			memcpy((void *)in2, brand_info, 128);
	} else {
		result.status = PAL_STATUS_REQUIRES_MEMORY;
		printk(KERN_WARNING"Invalid parameters for "
					"PAL call:0x%lx!\n", in0);
	}

	return result;
}

int kvm_pal_emul(struct kvm_vcpu *vcpu, struct kvm_run *run)
{

	u64 gr28;
	struct ia64_pal_retval result;
	int ret = 1;

	gr28 = kvm_get_pal_call_index(vcpu);
	switch (gr28) {
	case PAL_CACHE_FLUSH:
		result = pal_cache_flush(vcpu);
		break;
	case PAL_MEM_ATTRIB:
		result = pal_mem_attrib(vcpu);
		break;
	case PAL_CACHE_SUMMARY:
		result = pal_cache_summary(vcpu);
		break;
	case PAL_PERF_MON_INFO:
		result = pal_perf_mon_info(vcpu);
		break;
	case PAL_HALT_INFO:
		result = pal_halt_info(vcpu);
		break;
	case PAL_HALT_LIGHT:
	{
		INIT_PAL_STATUS_SUCCESS(result);
		prepare_for_halt(vcpu);
		if (kvm_highest_pending_irq(vcpu) == -1)
			ret = kvm_emulate_halt(vcpu);
	}
		break;

	case PAL_PREFETCH_VISIBILITY:
		result = pal_prefetch_visibility(vcpu);
		break;
	case PAL_MC_DRAIN:
		result.status = ia64_pal_mc_drain();
		/* FIXME: All vcpus likely call PAL_MC_DRAIN.
		   That causes the congestion. */
		smp_call_function(remote_pal_mc_drain, NULL, 1);
		break;

	case PAL_FREQ_RATIOS:
		result = pal_freq_ratios(vcpu);
		break;

	case PAL_FREQ_BASE:
		result = pal_freq_base(vcpu);
		break;

	case PAL_LOGICAL_TO_PHYSICAL :
		result = pal_logical_to_physica(vcpu);
		break;

	case PAL_VM_SUMMARY :
		result = pal_vm_summary(vcpu);
		break;

	case PAL_VM_INFO :
		result = pal_vm_info(vcpu);
		break;
	case PAL_PLATFORM_ADDR :
		result = pal_platform_addr(vcpu);
		break;
	case PAL_CACHE_INFO:
		result = pal_cache_info(vcpu);
		break;
	case PAL_PTCE_INFO:
		INIT_PAL_STATUS_SUCCESS(result);
		result.v1 = (1L << 32) | 1L;
		break;
	case PAL_REGISTER_INFO:
		result = pal_register_info(vcpu);
		break;
	case PAL_VM_PAGE_SIZE:
		result.status = ia64_pal_vm_page_size(&result.v0,
							&result.v1);
		break;
	case PAL_RSE_INFO:
		result.status = ia64_pal_rse_info(&result.v0,
					(pal_hints_u_t *)&result.v1);
		break;
	case PAL_PROC_GET_FEATURES:
		result = pal_proc_get_features(vcpu);
		break;
	case PAL_DEBUG_INFO:
		result.status = ia64_pal_debug_info(&result.v0,
							&result.v1);
		break;
	case PAL_VERSION:
		result.status = ia64_pal_version(
				(pal_version_u_t *)&result.v0,
				(pal_version_u_t *)&result.v1);
		break;
	case PAL_FIXED_ADDR:
		result.status = PAL_STATUS_SUCCESS;
		result.v0 = vcpu->vcpu_id;
		break;
	case PAL_BRAND_INFO:
		result = pal_get_brand_info(vcpu);
		break;
	case PAL_GET_PSTATE:
	case PAL_CACHE_SHARED_INFO:
		INIT_PAL_STATUS_UNIMPLEMENTED(result);
		break;
	default:
		INIT_PAL_STATUS_UNIMPLEMENTED(result);
		printk(KERN_WARNING"kvm: Unsupported pal call,"
					" index:0x%lx\n", gr28);
	}
	set_pal_result(vcpu, result);
	return ret;
}

static struct sal_ret_values sal_emulator(struct kvm *kvm,
				long index, unsigned long in1,
				unsigned long in2, unsigned long in3,
				unsigned long in4, unsigned long in5,
				unsigned long in6, unsigned long in7)
{
	unsigned long r9  = 0;
	unsigned long r10 = 0;
	long r11 = 0;
	long status;

	status = 0;
	switch (index) {
	case SAL_FREQ_BASE:
		status = ia64_sal_freq_base(in1, &r9, &r10);
		break;
	case SAL_PCI_CONFIG_READ:
		printk(KERN_WARNING"kvm: Not allowed to call here!"
			" SAL_PCI_CONFIG_READ\n");
		break;
	case SAL_PCI_CONFIG_WRITE:
		printk(KERN_WARNING"kvm: Not allowed to call here!"
			" SAL_PCI_CONFIG_WRITE\n");
		break;
	case SAL_SET_VECTORS:
		if (in1 == SAL_VECTOR_OS_BOOT_RENDEZ) {
			if (in4 != 0 || in5 != 0 || in6 != 0 || in7 != 0) {
				status = -2;
			} else {
				kvm->arch.rdv_sal_data.boot_ip = in2;
				kvm->arch.rdv_sal_data.boot_gp = in3;
			}
			printk("Rendvous called! iip:%lx\n\n", in2);
		} else
			printk(KERN_WARNING"kvm: CALLED SAL_SET_VECTORS %lu."
							"ignored...\n", in1);
		break;
	case SAL_GET_STATE_INFO:
		/* No more info.  */
		status = -5;
		r9 = 0;
		break;
	case SAL_GET_STATE_INFO_SIZE:
		/* Return a dummy size.  */
		status = 0;
		r9 = 128;
		break;
	case SAL_CLEAR_STATE_INFO:
		/* Noop.  */
		break;
	case SAL_MC_RENDEZ:
		printk(KERN_WARNING
			"kvm: called SAL_MC_RENDEZ. ignored...\n");
		break;
	case SAL_MC_SET_PARAMS:
		printk(KERN_WARNING
			"kvm: called  SAL_MC_SET_PARAMS.ignored!\n");
		break;
	case SAL_CACHE_FLUSH:
		if (1) {
			/*Flush using SAL.
			This method is faster but has a side
			effect on other vcpu running on
			this cpu.  */
			status = ia64_sal_cache_flush(in1);
		} else {
			/*Maybe need to implement the method
			without side effect!*/
			status = 0;
		}
		break;
	case SAL_CACHE_INIT:
		printk(KERN_WARNING
			"kvm: called SAL_CACHE_INIT.  ignored...\n");
		break;
	case SAL_UPDATE_PAL:
		printk(KERN_WARNING
			"kvm: CALLED SAL_UPDATE_PAL.  ignored...\n");
		break;
	default:
		printk(KERN_WARNING"kvm: called SAL_CALL with unknown index."
						" index:%ld\n", index);
		status = -1;
		break;
	}
	return ((struct sal_ret_values) {status, r9, r10, r11});
}

static void kvm_get_sal_call_data(struct kvm_vcpu *vcpu, u64 *in0, u64 *in1,
		u64 *in2, u64 *in3, u64 *in4, u64 *in5, u64 *in6, u64 *in7){

	struct exit_ctl_data *p;

	p = kvm_get_exit_data(vcpu);

	if (p->exit_reason == EXIT_REASON_SAL_CALL) {
		*in0 = p->u.sal_data.in0;
		*in1 = p->u.sal_data.in1;
		*in2 = p->u.sal_data.in2;
		*in3 = p->u.sal_data.in3;
		*in4 = p->u.sal_data.in4;
		*in5 = p->u.sal_data.in5;
		*in6 = p->u.sal_data.in6;
		*in7 = p->u.sal_data.in7;
		return ;
	}
	*in0 = 0;
}

void kvm_sal_emul(struct kvm_vcpu *vcpu)
{

	struct sal_ret_values result;
	u64 index, in1, in2, in3, in4, in5, in6, in7;

	kvm_get_sal_call_data(vcpu, &index, &in1, &in2,
			&in3, &in4, &in5, &in6, &in7);
	result = sal_emulator(vcpu->kvm, index, in1, in2, in3,
					in4, in5, in6, in7);
	set_sal_result(vcpu, result);
}
