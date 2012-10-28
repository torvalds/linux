#
# Makefile for the kernel SAA7146 FULL TS DVB device driver
# and the AV7110 DVB device driver
#

dvb-ttpci-objs := av7110_hw.o av7110_v4l.o av7110_av.o av7110_ca.o av7110.o av7110_ipack.o

ifdef CONFIG_INPUT_EVDEV
dvb-ttpci-objs += av7110_ir.o
endif

obj-$(CONFIG_TTPCI_EEPROM) += ttpci-eeprom.o
obj-$(CONFIG_DVB_BUDGET_CORE) += budget-core.o
obj-$(CONFIG_DVB_BUDGET) += budget.o
obj-$(CONFIG_DVB_BUDGET_AV) += budget-av.o
obj-$(CONFIG_DVB_BUDGET_CI) += budget-ci.o
obj-$(CONFIG_DVB_BUDGET_PATCH) += budget-patch.o
obj-$(CONFIG_DVB_AV7110) += dvb-ttpci.o

ccflags-y += -Idrivers/media/dvb-core/ -Idrivers/media/dvb-frontends/
ccflags-y += -Idrivers/media/tuners
