#
# Arch-specific network modules
#

ifeq ($(CONFIG_X86_32),y)
        obj-$(CONFIG_BPF_JIT) += bpf_jit_comp32.o
else
        obj-$(CONFIG_BPF_JIT) += bpf_jit_comp.o
endif
