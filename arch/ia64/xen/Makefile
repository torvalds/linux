#
# Makefile for Xen components
#

obj-y := hypercall.o xenivt.o xensetup.o xen_pv_ops.o irq_xen.o \
	 hypervisor.o xencomm.o xcom_hcall.o grant-table.o time.o suspend.o

obj-$(CONFIG_IA64_GENERIC) += machvec.o

AFLAGS_xenivt.o += -D__IA64_ASM_PARAVIRTUALIZED_XEN

# xen multi compile
ASM_PARAVIRT_MULTI_COMPILE_SRCS = ivt.S entry.S
ASM_PARAVIRT_OBJS = $(addprefix xen-,$(ASM_PARAVIRT_MULTI_COMPILE_SRCS:.S=.o))
obj-y += $(ASM_PARAVIRT_OBJS)
define paravirtualized_xen
AFLAGS_$(1) += -D__IA64_ASM_PARAVIRTUALIZED_XEN
endef
$(foreach o,$(ASM_PARAVIRT_OBJS),$(eval $(call paravirtualized_xen,$(o))))

$(obj)/xen-%.o: $(src)/../kernel/%.S FORCE
	$(call if_changed_dep,as_o_S)
