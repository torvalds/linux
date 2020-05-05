# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module, HYP part
#

ccflags-y += -fno-stack-protector -DDISABLE_BRANCH_PROFILING \
		$(DISABLE_STACKLEAK_PLUGIN)

obj-$(CONFIG_KVM) += vgic-v3-sr.o
obj-$(CONFIG_KVM) += timer-sr.o
obj-$(CONFIG_KVM) += aarch32.o
obj-$(CONFIG_KVM) += vgic-v2-cpuif-proxy.o
obj-$(CONFIG_KVM) += sysreg-sr.o
obj-$(CONFIG_KVM) += debug-sr.o
obj-$(CONFIG_KVM) += entry.o
obj-$(CONFIG_KVM) += switch.o
obj-$(CONFIG_KVM) += fpsimd.o
obj-$(CONFIG_KVM) += tlb.o
obj-$(CONFIG_KVM) += hyp-entry.o

# KVM code is run at a different exception code with a different map, so
# compiler instrumentation that inserts callbacks or checks into the code may
# cause crashes. Just disable it.
GCOV_PROFILE	:= n
KASAN_SANITIZE	:= n
UBSAN_SANITIZE	:= n
KCOV_INSTRUMENT	:= n
