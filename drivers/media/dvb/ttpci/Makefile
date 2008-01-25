#
# Makefile for the kernel SAA7146 FULL TS DVB device driver
# and the AV7110 DVB device driver
#

dvb-ttpci-objs := av7110_hw.o av7110_v4l.o av7110_av.o av7110_ca.o av7110.o av7110_ipack.o av7110_ir.o

obj-$(CONFIG_TTPCI_EEPROM) += ttpci-eeprom.o
obj-$(CONFIG_DVB_BUDGET_CORE) += budget-core.o
obj-$(CONFIG_DVB_BUDGET) += budget.o
obj-$(CONFIG_DVB_BUDGET_AV) += budget-av.o
obj-$(CONFIG_DVB_BUDGET_CI) += budget-ci.o
obj-$(CONFIG_DVB_BUDGET_PATCH) += budget-patch.o
obj-$(CONFIG_DVB_AV7110) += dvb-ttpci.o

EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core/ -Idrivers/media/dvb/frontends/

hostprogs-y	:= fdump

ifeq ($(CONFIG_DVB_AV7110_FIRMWARE),y)
$(obj)/av7110.o: $(obj)/av7110_firm.h

$(obj)/av7110_firm.h: $(obj)/fdump
	$(obj)/fdump $(CONFIG_DVB_AV7110_FIRMWARE_FILE) dvb_ttpci_fw $@
endif
