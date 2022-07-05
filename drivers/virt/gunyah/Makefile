# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_GH_VIRT_WATCHDOG)+= gh_virt_wdt.o
obj-$(CONFIG_GH_CTRL)           += gh_ctrl.o
obj-$(CONFIG_GH_DBL)            += gh_dbl.o
obj-$(CONFIG_GH_MSGQ)           += gh_msgq.o
obj-$(CONFIG_GH_RM_DRV)         += gh_rm_drv.o
gh_rm_drv-y                     += gh_rm_core.o gh_rm_iface.o
obj-$(CONFIG_GH_IRQ_LEND)       += gh_irq_lend.o
obj-$(CONFIG_GH_MEM_NOTIFIER)   += gh_mem_notifier.o
obj-$(CONFIG_GH_GUEST_POPS)     += gh_guest_pops.o
obj-$(CONFIG_GUNYAH)            += gunyah.o
gunyah-y                        := gh_main.o gh_secure_vm_virtio_backend.o
gunyah-$(CONFIG_GH_SECURE_VM_LOADER) += gh_secure_vm_loader.o
gunyah-$(CONFIG_GH_PROXY_SCHED)	+= gh_proxy_sched.o
CFLAGS_gh_secure_vm_virtio_backend.o = -DDYNAMIC_DEBUG_MODULE
