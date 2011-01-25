#
# Makefile for drivers/staging/appletalk
#
obj-$(CONFIG_ATALK) += appletalk.o

appletalk-y			:= aarp.o ddp.o dev.o
appletalk-$(CONFIG_PROC_FS)	+= atalk_proc.o
appletalk-$(CONFIG_SYSCTL)	+= sysctl_net_atalk.o

obj-$(CONFIG_IPDDP) += ipddp.o
obj-$(CONFIG_COPS) += cops.o
obj-$(CONFIG_LTPC) += ltpc.o
