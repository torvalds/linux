obj-y	+= grant-table.o features.o events.o manage.o balloon.o
obj-y	+= xenbus/

nostackp := $(call cc-option, -fno-stack-protector)
CFLAGS_features.o			:= $(nostackp)

obj-$(CONFIG_BLOCK)		+= biomerge.o
obj-$(CONFIG_HOTPLUG_CPU)	+= cpu_hotplug.o
obj-$(CONFIG_XEN_XENCOMM)	+= xencomm.o
obj-$(CONFIG_XEN_BALLOON)	+= xen-balloon.o
obj-$(CONFIG_XEN_DEV_EVTCHN)	+= xen-evtchn.o
obj-$(CONFIG_XEN_GNTDEV)	+= xen-gntdev.o
obj-$(CONFIG_XEN_GRANT_DEV_ALLOC)	+= xen-gntalloc.o
obj-$(CONFIG_XENFS)		+= xenfs/
obj-$(CONFIG_XEN_SYS_HYPERVISOR)	+= sys-hypervisor.o
obj-$(CONFIG_XEN_PLATFORM_PCI)	+= xen-platform-pci.o
obj-$(CONFIG_SWIOTLB_XEN)	+= swiotlb-xen.o
obj-$(CONFIG_XEN_DOM0)		+= pci.o

xen-evtchn-y			:= evtchn.o
xen-gntdev-y				:= gntdev.o
xen-gntalloc-y				:= gntalloc.o

xen-platform-pci-y		:= platform-pci.o
