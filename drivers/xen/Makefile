ifeq ($(filter y, $(CONFIG_ARM) $(CONFIG_ARM64)),)
obj-$(CONFIG_HOTPLUG_CPU)		+= cpu_hotplug.o
endif
obj-$(CONFIG_X86)			+= fallback.o
obj-y	+= grant-table.o features.o balloon.o manage.o preempt.o
obj-y	+= events/
obj-y	+= xenbus/

nostackp := $(call cc-option, -fno-stack-protector)
CFLAGS_features.o			:= $(nostackp)

CFLAGS_efi.o				+= -fshort-wchar

dom0-$(CONFIG_PCI) += pci.o
dom0-$(CONFIG_USB_SUPPORT) += dbgp.o
dom0-$(CONFIG_ACPI) += acpi.o $(xen-pad-y)
xen-pad-$(CONFIG_X86) += xen-acpi-pad.o
dom0-$(CONFIG_X86) += pcpu.o
obj-$(CONFIG_XEN_DOM0)			+= $(dom0-y)
obj-$(CONFIG_BLOCK)			+= biomerge.o
obj-$(CONFIG_XEN_BALLOON)		+= xen-balloon.o
obj-$(CONFIG_XEN_SELFBALLOONING)	+= xen-selfballoon.o
obj-$(CONFIG_XEN_DEV_EVTCHN)		+= xen-evtchn.o
obj-$(CONFIG_XEN_GNTDEV)		+= xen-gntdev.o
obj-$(CONFIG_XEN_GRANT_DEV_ALLOC)	+= xen-gntalloc.o
obj-$(CONFIG_XENFS)			+= xenfs/
obj-$(CONFIG_XEN_SYS_HYPERVISOR)	+= sys-hypervisor.o
obj-$(CONFIG_XEN_PVHVM)			+= platform-pci.o
obj-$(CONFIG_XEN_TMEM)			+= tmem.o
obj-$(CONFIG_SWIOTLB_XEN)		+= swiotlb-xen.o
obj-$(CONFIG_XEN_MCE_LOG)		+= mcelog.o
obj-$(CONFIG_XEN_PCIDEV_BACKEND)	+= xen-pciback/
obj-$(CONFIG_XEN_PRIVCMD)		+= xen-privcmd.o
obj-$(CONFIG_XEN_STUB)			+= xen-stub.o
obj-$(CONFIG_XEN_ACPI_HOTPLUG_MEMORY)	+= xen-acpi-memhotplug.o
obj-$(CONFIG_XEN_ACPI_HOTPLUG_CPU)	+= xen-acpi-cpuhotplug.o
obj-$(CONFIG_XEN_ACPI_PROCESSOR)	+= xen-acpi-processor.o
obj-$(CONFIG_XEN_EFI)			+= efi.o
obj-$(CONFIG_XEN_SCSI_BACKEND)		+= xen-scsiback.o
xen-evtchn-y				:= evtchn.o
xen-gntdev-y				:= gntdev.o
xen-gntalloc-y				:= gntalloc.o
xen-privcmd-y				:= privcmd.o
