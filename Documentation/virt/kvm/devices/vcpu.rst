.. SPDX-License-Identifier: GPL-2.0

======================
Generic vcpu interface
======================

The virtual cpu "device" also accepts the ioctls KVM_SET_DEVICE_ATTR,
KVM_GET_DEVICE_ATTR, and KVM_HAS_DEVICE_ATTR. The interface uses the same struct
kvm_device_attr as other devices, but targets VCPU-wide settings and controls.

The groups and attributes per virtual cpu, if any, are architecture specific.

1. GROUP: KVM_ARM_VCPU_PMU_V3_CTRL
==================================

:Architectures: ARM64

1.1. ATTRIBUTE: KVM_ARM_VCPU_PMU_V3_IRQ
---------------------------------------

:Parameters: in kvm_device_attr.addr the address for PMU overflow interrupt is a
	     pointer to an int

Returns:

	 =======  ========================================================
	 -EBUSY   The PMU overflow interrupt is already set
	 -EFAULT  Error reading interrupt number
	 -ENXIO   PMUv3 not supported or the overflow interrupt not set
		  when attempting to get it
	 -ENODEV  KVM_ARM_VCPU_PMU_V3 feature missing from VCPU
	 -EINVAL  Invalid PMU overflow interrupt number supplied or
		  trying to set the IRQ number without using an in-kernel
		  irqchip.
	 =======  ========================================================

A value describing the PMUv3 (Performance Monitor Unit v3) overflow interrupt
number for this vcpu. This interrupt could be a PPI or SPI, but the interrupt
type must be same for each vcpu. As a PPI, the interrupt number is the same for
all vcpus, while as an SPI it must be a separate number per vcpu.

1.2 ATTRIBUTE: KVM_ARM_VCPU_PMU_V3_INIT
---------------------------------------

:Parameters: no additional parameter in kvm_device_attr.addr

Returns:

	 =======  ======================================================
	 -EEXIST  Interrupt number already used
	 -ENODEV  PMUv3 not supported or GIC not initialized
	 -ENXIO   PMUv3 not supported, missing VCPU feature or interrupt
		  number not set
	 -EBUSY   PMUv3 already initialized
	 =======  ======================================================

Request the initialization of the PMUv3.  If using the PMUv3 with an in-kernel
virtual GIC implementation, this must be done after initializing the in-kernel
irqchip.

1.3 ATTRIBUTE: KVM_ARM_VCPU_PMU_V3_FILTER
-----------------------------------------

:Parameters: in kvm_device_attr.addr the address for a PMU event filter is a
             pointer to a struct kvm_pmu_event_filter

:Returns:

	 =======  ======================================================
	 -ENODEV: PMUv3 not supported or GIC not initialized
	 -ENXIO:  PMUv3 not properly configured or in-kernel irqchip not
	 	  configured as required prior to calling this attribute
	 -EBUSY:  PMUv3 already initialized
	 -EINVAL: Invalid filter range
	 =======  ======================================================

Request the installation of a PMU event filter described as follows:

struct kvm_pmu_event_filter {
	__u16	base_event;
	__u16	nevents;

#define KVM_PMU_EVENT_ALLOW	0
#define KVM_PMU_EVENT_DENY	1

	__u8	action;
	__u8	pad[3];
};

A filter range is defined as the range [@base_event, @base_event + @nevents),
together with an @action (KVM_PMU_EVENT_ALLOW or KVM_PMU_EVENT_DENY). The
first registered range defines the global policy (global ALLOW if the first
@action is DENY, global DENY if the first @action is ALLOW). Multiple ranges
can be programmed, and must fit within the event space defined by the PMU
architecture (10 bits on ARMv8.0, 16 bits from ARMv8.1 onwards).

Note: "Cancelling" a filter by registering the opposite action for the same
range doesn't change the default action. For example, installing an ALLOW
filter for event range [0:10) as the first filter and then applying a DENY
action for the same range will leave the whole range as disabled.

Restrictions: Event 0 (SW_INCR) is never filtered, as it doesn't count a
hardware event. Filtering event 0x1E (CHAIN) has no effect either, as it
isn't strictly speaking an event. Filtering the cycle counter is possible
using event 0x11 (CPU_CYCLES).


2. GROUP: KVM_ARM_VCPU_TIMER_CTRL
=================================

:Architectures: ARM, ARM64

2.1. ATTRIBUTES: KVM_ARM_VCPU_TIMER_IRQ_VTIMER, KVM_ARM_VCPU_TIMER_IRQ_PTIMER
-----------------------------------------------------------------------------

:Parameters: in kvm_device_attr.addr the address for the timer interrupt is a
	     pointer to an int

Returns:

	 =======  =================================
	 -EINVAL  Invalid timer interrupt number
	 -EBUSY   One or more VCPUs has already run
	 =======  =================================

A value describing the architected timer interrupt number when connected to an
in-kernel virtual GIC.  These must be a PPI (16 <= intid < 32).  Setting the
attribute overrides the default values (see below).

=============================  ==========================================
KVM_ARM_VCPU_TIMER_IRQ_VTIMER  The EL1 virtual timer intid (default: 27)
KVM_ARM_VCPU_TIMER_IRQ_PTIMER  The EL1 physical timer intid (default: 30)
=============================  ==========================================

Setting the same PPI for different timers will prevent the VCPUs from running.
Setting the interrupt number on a VCPU configures all VCPUs created at that
time to use the number provided for a given timer, overwriting any previously
configured values on other VCPUs.  Userspace should configure the interrupt
numbers on at least one VCPU after creating all VCPUs and before running any
VCPUs.

3. GROUP: KVM_ARM_VCPU_PVTIME_CTRL
==================================

:Architectures: ARM64

3.1 ATTRIBUTE: KVM_ARM_VCPU_PVTIME_IPA
--------------------------------------

:Parameters: 64-bit base address

Returns:

	 =======  ======================================
	 -ENXIO   Stolen time not implemented
	 -EEXIST  Base address already set for this VCPU
	 -EINVAL  Base address not 64 byte aligned
	 =======  ======================================

Specifies the base address of the stolen time structure for this VCPU. The
base address must be 64 byte aligned and exist within a valid guest memory
region. See Documentation/virt/kvm/arm/pvtime.rst for more information
including the layout of the stolen time structure.
