# SPDX-License-Identifier: GPL-2.0
#
# Arch-specific network modules
#
ifeq ($(CONFIG_PPC64),y)
obj-$(CONFIG_BPF_JIT) += bpf_jit_asm64.o bpf_jit_comp64.o
else
obj-$(CONFIG_BPF_JIT) += bpf_jit_asm.o bpf_jit_comp.o
endif
