#
# Makefile for the Linux IEEE 1394 implementation
#

ieee1394-objs := ieee1394_core.o ieee1394_transactions.o hosts.o \
		 highlevel.o csr.o nodemgr.o oui.o dma.o iso.o \
		 csr1212.o config_roms.o

obj-$(CONFIG_IEEE1394) += ieee1394.o
obj-$(CONFIG_IEEE1394_PCILYNX) += pcilynx.o
obj-$(CONFIG_IEEE1394_OHCI1394) += ohci1394.o
obj-$(CONFIG_IEEE1394_VIDEO1394) += video1394.o
obj-$(CONFIG_IEEE1394_RAWIO) += raw1394.o
obj-$(CONFIG_IEEE1394_SBP2) += sbp2.o
obj-$(CONFIG_IEEE1394_DV1394) += dv1394.o
obj-$(CONFIG_IEEE1394_ETH1394) += eth1394.o
obj-$(CONFIG_IEEE1394_AMDTP) += amdtp.o
obj-$(CONFIG_IEEE1394_CMP) += cmp.o

quiet_cmd_oui2c = OUI2C   $@
      cmd_oui2c = $(CONFIG_SHELL) $(srctree)/$(src)/oui2c.sh < $< > $@

targets := oui.c
$(obj)/oui.o: $(obj)/oui.c
$(obj)/oui.c: $(src)/oui.db $(src)/oui2c.sh FORCE
	$(call if_changed,oui2c)
