# SPDX-License-Identifier: GPL-2.0
#
# Arch-specific network modules
#
obj-$(CONFIG_BPF_JIT) += bpf_jit_comp.o bpf_jit_comp$(BITS).o

ifdef CONFIG_PPC64
obj-$(CONFIG_BPF_JIT) += bpf_timed_may_goto.o
endif
