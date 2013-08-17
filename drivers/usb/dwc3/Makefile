ccflags-$(CONFIG_USB_DWC3_DEBUG)	:= -DDEBUG
ccflags-$(CONFIG_USB_DWC3_VERBOSE)	+= -DVERBOSE_DEBUG

obj-$(CONFIG_USB_DWC3)			+= dwc3.o

dwc3-y					:= core.o
dwc3-y					+= host.o
dwc3-y					+= gadget.o ep0.o

ifneq ($(CONFIG_DEBUG_FS),)
	dwc3-y				+= debugfs.o
endif

##
# Platform-specific glue layers go here
#
# NOTICE: Make sure your glue layer doesn't depend on anything
# which is arch-specific and that it compiles on all situations.
#
# We want to keep this requirement in order to be able to compile
# the entire driver (with all its glue layers) on several architectures
# and make sure it compiles fine. This will also help with allmodconfig
# and allyesconfig builds.
#
# The only exception is the PCI glue layer, but that's only because
# PCI doesn't provide nops if CONFIG_PCI isn't enabled.
##

obj-$(CONFIG_USB_DWC3)		+= dwc3-omap.o

##
# REVISIT Samsung Exynos platform needs the clk API which isn't
# defined on all architectures. If we allow dwc3-exynos.c compile
# always we will fail the linking phase on those architectures
# which don't provide clk api implementation and that's unnaceptable.
#
# When Samsung's platform start supporting pm_runtime, this check
# for HAVE_CLK should be removed.
##
ifneq ($(CONFIG_HAVE_CLK),)
	obj-$(CONFIG_USB_DWC3)		+= dwc3-exynos.o
endif

ifneq ($(CONFIG_PCI),)
	obj-$(CONFIG_USB_DWC3)		+= dwc3-pci.o
endif

##
# For Exynos USB3 DRD
##

ccflags-$(CONFIG_USB_EXYNOS_DRD) += -Idrivers/usb/host
obj-$(CONFIG_USB_EXYNOS_DRD)	+= exynos-drd.o exynos-drd-switch.o

