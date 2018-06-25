#
# Arch-specific network modules
#
obj-$(CONFIG_BPF_JIT) += bpf_jit_comp_$(BITS).o
ifeq ($(BITS),32)
obj-$(CONFIG_BPF_JIT) += bpf_jit_asm_32.o
endif
