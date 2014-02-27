obj-$(CONFIG_WIL6210) += wil6210.o

wil6210-objs := main.o
wil6210-objs += netdev.o
wil6210-objs += cfg80211.o
wil6210-objs += pcie_bus.o
wil6210-objs += debugfs.o
wil6210-objs += wmi.o
wil6210-objs += interrupt.o
wil6210-objs += txrx.o

ifeq (, $(findstring -W,$(EXTRA_CFLAGS)))
	subdir-ccflags-y += -Werror
endif
subdir-ccflags-y += -D__CHECK_ENDIAN__
