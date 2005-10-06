#
# Makefile for the linux kernel.
#

CFLAGS_prom_init.o      += -fPIC
CFLAGS_btext.o          += -fPIC

wdt-mpc8xx-$(CONFIG_8xx_WDT)	+= m8xx_wdt.o

obj-$(CONFIG_PPCBUG_NVRAM)	+= prep_nvram.o
obj-$(CONFIG_PPC_OCP)		+= ocp.o
obj-$(CONFIG_IBM_OCP)		+= ibm_ocp.o
obj-$(CONFIG_44x)		+= ibm44x_common.o
obj-$(CONFIG_440EP)		+= ibm440gx_common.o
obj-$(CONFIG_440GP)		+= ibm440gp_common.o
obj-$(CONFIG_440GX)		+= ibm440gx_common.o
obj-$(CONFIG_440SP)		+= ibm440gx_common.o ibm440sp_common.o
ifeq ($(CONFIG_4xx),y)
ifeq ($(CONFIG_VIRTEX_II_PRO),y)
obj-$(CONFIG_40x)		+= xilinx_pic.o
else
ifeq ($(CONFIG_403),y)
obj-$(CONFIG_40x)		+= ppc403_pic.o
else
obj-$(CONFIG_40x)		+= ppc4xx_pic.o
endif
endif
obj-$(CONFIG_44x)		+= ppc4xx_pic.o
obj-$(CONFIG_40x)		+= ppc4xx_setup.o
obj-$(CONFIG_GEN_RTC)		+= todc_time.o
obj-$(CONFIG_PPC4xx_DMA)	+= ppc4xx_dma.o
obj-$(CONFIG_PPC4xx_EDMA)	+= ppc4xx_sgdma.o
ifeq ($(CONFIG_40x),y)
obj-$(CONFIG_PCI)		+= indirect_pci.o pci_auto.o ppc405_pci.o
endif
endif
obj-$(CONFIG_8xx)		+= m8xx_setup.o ppc8xx_pic.o $(wdt-mpc8xx-y) \
				   ppc_sys.o mpc8xx_devices.o mpc8xx_sys.o
ifeq ($(CONFIG_8xx),y)
obj-$(CONFIG_PCI)		+= qspan_pci.o i8259.o
endif
obj-$(CONFIG_PPC_OF)		+= prom_init.o prom.o of_device.o
obj-$(CONFIG_PPC_PMAC)		+= open_pic.o indirect_pci.o
obj-$(CONFIG_POWER4)		+= open_pic2.o
obj-$(CONFIG_PPC_CHRP)		+= open_pic.o indirect_pci.o i8259.o
obj-$(CONFIG_PPC_PREP)		+= open_pic.o indirect_pci.o i8259.o todc_time.o
obj-$(CONFIG_BAMBOO)		+= indirect_pci.o pci_auto.o todc_time.o
obj-$(CONFIG_CPCI690)		+= todc_time.o pci_auto.o
obj-$(CONFIG_EBONY)		+= indirect_pci.o pci_auto.o todc_time.o
obj-$(CONFIG_EV64260)		+= todc_time.o pci_auto.o
obj-$(CONFIG_CHESTNUT)		+= mv64360_pic.o pci_auto.o
obj-$(CONFIG_GEMINI)		+= open_pic.o indirect_pci.o
obj-$(CONFIG_GT64260)		+= gt64260_pic.o
obj-$(CONFIG_LOPEC)		+= i8259.o pci_auto.o todc_time.o
obj-$(CONFIG_HDPU)		+= pci_auto.o
obj-$(CONFIG_LUAN)		+= indirect_pci.o pci_auto.o todc_time.o
obj-$(CONFIG_KATANA)		+= pci_auto.o
obj-$(CONFIG_MV64360)		+= mv64360_pic.o
obj-$(CONFIG_MV64X60)		+= mv64x60.o mv64x60_win.o indirect_pci.o
obj-$(CONFIG_MVME5100)		+= open_pic.o todc_time.o indirect_pci.o \
					pci_auto.o hawk_common.o
obj-$(CONFIG_MVME5100_IPMC761_PRESENT)	+= i8259.o
obj-$(CONFIG_OCOTEA)		+= indirect_pci.o pci_auto.o todc_time.o
obj-$(CONFIG_PAL4)		+= cpc700_pic.o
obj-$(CONFIG_POWERPMC250)	+= pci_auto.o
obj-$(CONFIG_PPLUS)		+= hawk_common.o open_pic.o i8259.o \
				   indirect_pci.o todc_time.o pci_auto.o
obj-$(CONFIG_PRPMC750)		+= open_pic.o indirect_pci.o pci_auto.o \
					hawk_common.o
obj-$(CONFIG_HARRIER)		+= harrier.o
obj-$(CONFIG_PRPMC800)		+= open_pic.o indirect_pci.o pci_auto.o
obj-$(CONFIG_RADSTONE_PPC7D)	+= i8259.o pci_auto.o
obj-$(CONFIG_SANDPOINT)		+= i8259.o pci_auto.o todc_time.o
obj-$(CONFIG_SBC82xx)		+= todc_time.o
obj-$(CONFIG_SPRUCE)		+= cpc700_pic.o indirect_pci.o pci_auto.o \
				   todc_time.o
obj-$(CONFIG_8260)		+= m8260_setup.o pq2_devices.o pq2_sys.o \
				   ppc_sys.o
obj-$(CONFIG_PCI_8260)		+= m82xx_pci.o indirect_pci.o pci_auto.o
obj-$(CONFIG_8260_PCI9)		+= m8260_pci_erratum9.o
obj-$(CONFIG_CPM2)		+= cpm2_common.o cpm2_pic.o
ifeq ($(CONFIG_PPC_GEN550),y)
obj-$(CONFIG_KGDB)		+= gen550_kgdb.o gen550_dbg.o
obj-$(CONFIG_SERIAL_TEXT_DEBUG)	+= gen550_dbg.o
endif
ifeq ($(CONFIG_SERIAL_MPSC_CONSOLE),y)
obj-$(CONFIG_SERIAL_TEXT_DEBUG)	+= mv64x60_dbg.o
endif
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o
obj-$(CONFIG_MPC10X_BRIDGE)	+= mpc10x_common.o indirect_pci.o ppc_sys.o
obj-$(CONFIG_MPC10X_OPENPIC)	+= open_pic.o
obj-$(CONFIG_40x)		+= dcr.o
obj-$(CONFIG_BOOKE)		+= dcr.o
obj-$(CONFIG_85xx)		+= open_pic.o ppc85xx_common.o ppc85xx_setup.o \
					ppc_sys.o i8259.o mpc85xx_sys.o \
					mpc85xx_devices.o
ifeq ($(CONFIG_85xx),y)
obj-$(CONFIG_PCI)		+= indirect_pci.o pci_auto.o
endif
obj-$(CONFIG_83xx)		+= ipic.o ppc83xx_setup.o ppc_sys.o \
					mpc83xx_sys.o mpc83xx_devices.o
ifeq ($(CONFIG_83xx),y)
obj-$(CONFIG_PCI)		+= indirect_pci.o pci_auto.o
endif
obj-$(CONFIG_MPC8548_CDS)	+= todc_time.o
obj-$(CONFIG_MPC8555_CDS)	+= todc_time.o
obj-$(CONFIG_PPC_MPC52xx)	+= mpc52xx_setup.o mpc52xx_pic.o \
					mpc52xx_sys.o mpc52xx_devices.o ppc_sys.o
ifeq ($(CONFIG_PPC_MPC52xx),y)
obj-$(CONFIG_PCI)		+= mpc52xx_pci.o
endif
