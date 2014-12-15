#
# Makefile for DWC_otg Highspeed USB controller driver
#

DWC_OTG_NAME	= dwc_otg

COMMON_PORT	= common_port
BUS_INTERFACE = -DLM_INTERFACE

KBUILD_CPPFLAGS	+= -DDEBUG 

KBUILD_CPPFLAGS		+= -Dlinux -DDWC_HS_ELECT_TST
#KBUILD_CPPFLAGS	+= -DDWC_EN_ISOC
KBUILD_CPPFLAGS   	+= -I$(srctree)/$(src)/$(COMMON_PORT)
#KBUILD_CPPFLAGS   	+= -I$(PORTLIB)
KBUILD_CPPFLAGS   	+= -DDWC_LINUX
KBUILD_CPPFLAGS   	+= $(CFI)
KBUILD_CPPFLAGS		+= $(BUS_INTERFACE)
#KBUILD_CPPFLAGS		+= -DDWC_DEV_SRPCAP



$(DWC_OTG_NAME)-objs	:= dwc_otg_driver.o dwc_otg_attr.o 
$(DWC_OTG_NAME)-objs	+= dwc_otg_cil.o dwc_otg_cil_intr.o 
$(DWC_OTG_NAME)-objs	+= dwc_otg_pcd_linux.o dwc_otg_pcd.o dwc_otg_pcd_intr.o 
$(DWC_OTG_NAME)-objs	+= dwc_otg_hcd.o dwc_otg_hcd_linux.o dwc_otg_hcd_intr.o dwc_otg_hcd_queue.o dwc_otg_hcd_ddma.o
$(DWC_OTG_NAME)-objs	+= dwc_otg_adp.o
ifneq ($(CFI),)
$(DWC_OTG_NAME)-objs	+= dwc_otg_cfi.o
endif
$(DWC_OTG_NAME)-objs	+= $(COMMON_PORT)/dwc_common_linux.o

obj-$(CONFIG_USB_DWC_OTG_HCD)	+= $(DWC_OTG_NAME).o