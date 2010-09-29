
ccflags-y += -DNV_IS_AVP=0
ccflags-y += -DNV_OAL=0
ccflags-y += -DNV_USE_FUSE_CLOCK_ENABLE=0
ifeq ($(CONFIG_MACH_TEGRA_GENERIC_DEBUG),y)
ccflags-y += -DNV_DEBUG=1
else
ccflags-y += -DNV_DEBUG=0
endif
ccflags-y += -Iarch/arm/mach-tegra/nv/include


obj-$(CONFIG_TEGRA_NVRM)	+= nvrm_user.o
obj-$(CONFIG_TEGRA_NVRM)	+= nvrpc_user.o
obj-$(CONFIG_TEGRA_NVRM)	+= nvrm/
obj-$(CONFIG_TEGRA_NVRM)	+= nvreftrack/

obj-$(CONFIG_TEGRA_NVOS)	+= nvos_user.o
obj-$(CONFIG_TEGRA_NVOS)	+= nvos/
