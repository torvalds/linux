#
# HFI driver
#
#
#
# Called from the kernel module build system.
#
obj-$(CONFIG_INFINIBAND_HFI1) += hfi1.o

hfi1-y := chip.o device.o diag.o driver.o efivar.o eprom.o file_ops.o firmware.o \
	init.o intr.o mad.o pcie.o pio.o pio_copy.o \
	qp.o qsfp.o rc.o ruc.o sdma.o srq.o sysfs.o trace.o twsi.o \
	uc.o ud.o user_exp_rcv.o user_pages.o user_sdma.o verbs_mcast.o verbs.o
hfi1-$(CONFIG_DEBUG_FS) += debugfs.o

CFLAGS_trace.o = -I$(src)
ifdef MVERSION
CFLAGS_driver.o = -DHFI_DRIVER_VERSION_BASE=\"$(MVERSION)\"
endif
