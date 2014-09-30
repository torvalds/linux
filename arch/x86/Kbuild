obj-$(CONFIG_KVM) += kvm/

# Xen paravirtualization support
obj-$(CONFIG_XEN) += xen/

# lguest paravirtualization support
obj-$(CONFIG_LGUEST_GUEST) += lguest/

obj-y += realmode/
obj-y += kernel/
obj-y += mm/

obj-y += crypto/
obj-y += vdso/
obj-$(CONFIG_IA32_EMULATION) += ia32/

obj-y += platform/
obj-y += net/

obj-$(CONFIG_KEXEC_FILE) += purgatory/
