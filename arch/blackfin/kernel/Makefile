#
# arch/blackfin/kernel/Makefile
#

extra-y := init_task.o vmlinux.lds

obj-y := \
	entry.o process.o bfin_ksyms.o ptrace.o setup.o signal.o \
	sys_bfin.o traps.o irqchip.o dma-mapping.o flat.o \
	fixed_code.o reboot.o bfin_gpio.o bfin_dma_5xx.o

ifeq ($(CONFIG_GENERIC_CLOCKEVENTS),y)
    obj-y += time-ts.o
else
    obj-y += time.o
endif

obj-$(CONFIG_IPIPE)                  += ipipe.o
obj-$(CONFIG_IPIPE_TRACE_MCOUNT)     += mcount.o
obj-$(CONFIG_BFIN_GPTIMERS)          += gptimers.o
obj-$(CONFIG_CPLB_INFO)              += cplbinfo.o
obj-$(CONFIG_MODULES)                += module.o
obj-$(CONFIG_KGDB)                   += kgdb.o
obj-$(CONFIG_KGDB_TESTCASE)          += kgdb_test.o
obj-$(CONFIG_EARLY_PRINTK)           += early_printk.o
