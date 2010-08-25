ccflags-y += -DNV_IS_AVP=0
ccflags-y += -DNV_OAL=0
ccflags-y += -DNV_USE_FUSE_CLOCK_ENABLE=0
ifeq ($(CONFIG_MACH_TEGRA_GENERIC_DEBUG),y)
ccflags-y += -DNV_DEBUG=1
else
ccflags-y += -DNV_DEBUG=0
endif
ccflags-y += -Iarch/arm/mach-tegra/nv/include
ccflags-y += -Iarch/arm/mach-tegra/nv/nvrm/core/common
ccflags-y += -Iarch/arm/mach-tegra/nv/nvrm/core

obj-y += headavp.o
obj-y += nvrm_avp_cpu_rpc.o
obj-y += nvrm_moduleloader.o
obj-y += nvrm_rmctrace.o
obj-y += nvrm_transport.o
obj-y += nvrm_module_stub.o
obj-y += nvrm_power.o
obj-y += nvrm_init_stub.o

