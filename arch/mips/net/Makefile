# SPDX-License-Identifier: GPL-2.0-only
# MIPS networking code

obj-$(CONFIG_BPF_JIT) += bpf_jit_comp.o

ifeq ($(CONFIG_32BIT),y)
        obj-$(CONFIG_BPF_JIT) += bpf_jit_comp32.o
else
        obj-$(CONFIG_BPF_JIT) += bpf_jit_comp64.o
endif
