ccflags-y += -DNV_IS_AVP=0
ccflags-y += -DNV_OAL=0
ccflags-y += -DNV_USE_FUSE_CLOCK_ENABLE=0
ifeq ($(CONFIG_MACH_TEGRA_GENERIC_DEBUG),y)
ccflags-y += -DNV_DEBUG=1
else
ccflags-y += -DNV_DEBUG=0
endif

obj-y += core/
#obj-y += core/ap20/
#obj-y += core/common/

#obj-y += io/common/
#obj-y += io/ap15/
#obj-y += io/ap20/



obj-y += dispatch/
