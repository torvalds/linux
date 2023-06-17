# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_GH_VIRT_WATCHDOG)+= gh_virt_wdt.o
obj-$(CONFIG_GH_CTRL)           += gh_ctrl.o
obj-$(CONFIG_GH_DBL)            += gh_dbl.o
obj-$(CONFIG_GH_MSGQ)           += gh_msgq.o
obj-$(CONFIG_GH_RM_DRV)         += gh_rm_drv.o
gh_rm_drv-y                     += gh_rm_core.o gh_rm_iface.o
obj-$(CONFIG_GH_IRQ_LEND)       += gh_irq_lend.o
obj-$(CONFIG_GH_RM_BOOSTER)     += gh_rm_booster.o
obj-$(CONFIG_GH_MEM_NOTIFIER)   += gh_mem_notifier.o
obj-$(CONFIG_GH_GUEST_POPS)     += gh_guest_pops.o
obj-$(CONFIG_GH_SECURE_VM_LOADER)      += gunyah_loader.o
gunyah_loader-y                        := gh_main.o gh_secure_vm_virtio_backend.o
gunyah_loader-$(CONFIG_GH_SECURE_VM_LOADER) += gh_secure_vm_loader.o
gunyah_loader-$(CONFIG_GH_PROXY_SCHED)		+= gh_proxy_sched.o
CFLAGS_gh_secure_vm_virtio_backend.o = -DDYNAMIC_DEBUG_MODULE
obj-$(CONFIG_GH_PANIC_NOTIFIER) += gh_panic_notifier.o

obj-$(CONFIG_GUNYAH_PLATFORM_HOOKS) += gunyah_platform_hooks.o
obj-$(CONFIG_GUNYAH_QCOM_PLATFORM) += gunyah_qcom.o

gunyah-y += rsc_mgr.o rsc_mgr_rpc.o vm_mgr.o vm_mgr_mm.o
obj-$(CONFIG_GUNYAH) += gunyah.o

obj-$(CONFIG_GUNYAH_VCPU) += gunyah_vcpu.o
obj-$(CONFIG_GUNYAH_IRQFD) += gunyah_irqfd.o
obj-$(CONFIG_GUNYAH_IOEVENTFD) += gunyah_ioeventfd.o
