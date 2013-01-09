EXTRA_CFLAGS += -Os
ifneq ($(CONFIG_RK_FPGA),y)
obj-y += ../plat-rk/clock.o
obj-y += clock_data.o
obj-y += ../mach-rk30/ddr.o
CFLAGS_ddr.o += -mthumb
obj-y += ../mach-rk30/reset.o
endif
obj-y += ../mach-rk30/common.o
CFLAGS_common.o += -DTEXT_OFFSET=$(TEXT_OFFSET)
obj-y += ../mach-rk30/devices.o
obj-y += io.o

obj-$(CONFIG_MACH_RK3188_FPGA) += board-rk3188-fpga.o
