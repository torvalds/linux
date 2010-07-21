ccflags-y += -DNV_IS_AVP=0
ccflags-y += -DNV_OAL=0
ccflags-y += -DNV_USE_FUSE_CLOCK_ENABLE=0
ifeq ($(CONFIG_MACH_TEGRA_GENERIC_DEBUG),y)
ccflags-y += -DNV_DEBUG=1
else
ccflags-y += -DNV_DEBUG=0
endif
ccflags-y += -Iarch/arm/mach-tegra/nv/include

obj-y += NvRm_Dispatch.o
#obj-y += nvrm_analog_dispatch.o
#obj-y += nvrm_diag_dispatch.o
#obj-y += nvrm_dma_dispatch.o
#obj-y += nvrm_gpio_dispatch.o
#obj-y += nvrm_i2c_dispatch.o
#obj-y += nvrm_owr_dispatch.o
#obj-y += nvrm_pwm_dispatch.o
#obj-y += nvrm_init_dispatch.o
#obj-y += nvrm_interrupt_dispatch.o
#obj-y += nvrm_memmgr_dispatch.o
obj-y += nvrm_module_dispatch.o
#obj-y += nvrm_pinmux_dispatch.o
obj-y += nvrm_power_dispatch.o
#obj-y += nvrm_spi_dispatch.o
#obj-y += nvrm_pmu_dispatch.o
#obj-y += nvrm_keylist_dispatch.o
#obj-y += nvrm_pcie_dispatch.o
#obj-y += nvrm_memctrl_dispatch.o
obj-y += nvrm_transport_dispatch.o
#obj-y += nvrm_xpc_dispatch.o
