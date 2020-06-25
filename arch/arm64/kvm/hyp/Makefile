# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module, HYP part
#

incdir := $(srctree)/$(src)/include
subdir-asflags-y := -I$(incdir)
subdir-ccflags-y := -I$(incdir)				\
		    -fno-stack-protector		\
		    -DDISABLE_BRANCH_PROFILING		\
		    $(DISABLE_STACKLEAK_PLUGIN)

obj-$(CONFIG_KVM) += hyp.o vhe/ nvhe/
obj-$(CONFIG_KVM_INDIRECT_VECTORS) += smccc_wa.o

hyp-y := vgic-v3-sr.o timer-sr.o aarch32.o vgic-v2-cpuif-proxy.o entry.o \
	 fpsimd.o

# KVM code is run at a different exception code with a different map, so
# compiler instrumentation that inserts callbacks or checks into the code may
# cause crashes. Just disable it.
GCOV_PROFILE	:= n
KASAN_SANITIZE	:= n
UBSAN_SANITIZE	:= n
KCOV_INSTRUMENT	:= n
