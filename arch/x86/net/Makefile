#
# Arch-specific network modules
#

ifeq ($(CONFIG_X86_32),y)
        obj-$(CONFIG_BPF_JIT) += bpf_jit_comp32.o
else
        OBJECT_FILES_NON_STANDARD_bpf_jit.o += y
        obj-$(CONFIG_BPF_JIT) += bpf_jit.o bpf_jit_comp.o
endif
