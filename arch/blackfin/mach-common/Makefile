#
# arch/blackfin/mach-common/Makefile
#

obj-y := \
	cache.o cacheinit.o cplbhdlr.o cplbmgr.o entry.o \
	interrupt.o lock.o irqpanic.o arch_checks.o

obj-$(CONFIG_CPLB_INFO)          += cplbinfo.o
obj-$(CONFIG_BFIN_SINGLE_CORE)   += ints-priority-sc.o
obj-$(CONFIG_BFIN_DUAL_CORE)     += ints-priority-dc.o
obj-$(CONFIG_PM)                 += pm.o dpmc.o
