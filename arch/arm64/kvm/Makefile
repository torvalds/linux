# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module
#

ccflags-y += -I $(srctree)/$(src)

include $(srctree)/virt/kvm/Makefile.kvm

obj-$(CONFIG_KVM) += kvm.o
obj-$(CONFIG_KVM) += hyp/

kvm-y += arm.o mmu.o mmio.o psci.o hypercalls.o pvtime.o \
	 inject_fault.o va_layout.o handle_exit.o \
	 guest.o debug.o reset.o sys_regs.o stacktrace.o \
	 vgic-sys-reg-v3.o fpsimd.o pkvm.o \
	 arch_timer.o trng.o vmid.o emulate-nested.o nested.o \
	 vgic/vgic.o vgic/vgic-init.o \
	 vgic/vgic-irqfd.o vgic/vgic-v2.o \
	 vgic/vgic-v3.o vgic/vgic-v4.o \
	 vgic/vgic-mmio.o vgic/vgic-mmio-v2.o \
	 vgic/vgic-mmio-v3.o vgic/vgic-kvm-device.o \
	 vgic/vgic-its.o vgic/vgic-debug.o

kvm-$(CONFIG_HW_PERF_EVENTS)  += pmu-emul.o pmu.o

always-y := hyp_constants.h hyp-constants.s

define rule_gen_hyp_constants
	$(call filechk,offsets,__HYP_CONSTANTS_H__)
endef

CFLAGS_hyp-constants.o = -I $(srctree)/$(src)/hyp/include
$(obj)/hyp-constants.s: $(src)/hyp/hyp-constants.c FORCE
	$(call if_changed_dep,cc_s_c)

$(obj)/hyp_constants.h: $(obj)/hyp-constants.s FORCE
	$(call if_changed_rule,gen_hyp_constants)

obj-kvm := $(addprefix $(obj)/, $(kvm-y))
$(obj-kvm): $(obj)/hyp_constants.h
