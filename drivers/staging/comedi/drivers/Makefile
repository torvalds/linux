# SPDX-License-Identifier: GPL-2.0
# Makefile for individual comedi drivers
#
ccflags-$(CONFIG_COMEDI_DEBUG)		:= -DDEBUG

# Comedi "helper" modules
obj-$(CONFIG_COMEDI_8254)		+= comedi_8254.o
obj-$(CONFIG_COMEDI_ISADMA)		+= comedi_isadma.o

# Comedi misc drivers
obj-$(CONFIG_COMEDI_BOND)		+= comedi_bond.o
obj-$(CONFIG_COMEDI_TEST)		+= comedi_test.o
obj-$(CONFIG_COMEDI_PARPORT)		+= comedi_parport.o

# Comedi ISA drivers
obj-$(CONFIG_COMEDI_AMPLC_DIO200_ISA)	+= amplc_dio200.o
obj-$(CONFIG_COMEDI_AMPLC_PC236_ISA)	+= amplc_pc236.o
obj-$(CONFIG_COMEDI_AMPLC_PC263_ISA)	+= amplc_pc263.o
obj-$(CONFIG_COMEDI_PCL711)		+= pcl711.o
obj-$(CONFIG_COMEDI_PCL724)		+= pcl724.o
obj-$(CONFIG_COMEDI_PCL726)		+= pcl726.o
obj-$(CONFIG_COMEDI_PCL730)		+= pcl730.o
obj-$(CONFIG_COMEDI_PCL812)		+= pcl812.o
obj-$(CONFIG_COMEDI_PCL816)		+= pcl816.o
obj-$(CONFIG_COMEDI_PCL818)		+= pcl818.o
obj-$(CONFIG_COMEDI_PCM3724)		+= pcm3724.o
obj-$(CONFIG_COMEDI_RTI800)		+= rti800.o
obj-$(CONFIG_COMEDI_RTI802)		+= rti802.o
obj-$(CONFIG_COMEDI_DAC02)		+= dac02.o
obj-$(CONFIG_COMEDI_DAS16M1)		+= das16m1.o
obj-$(CONFIG_COMEDI_DAS08_ISA)		+= das08_isa.o
obj-$(CONFIG_COMEDI_DAS16)		+= das16.o
obj-$(CONFIG_COMEDI_DAS800)		+= das800.o
obj-$(CONFIG_COMEDI_DAS1800)		+= das1800.o
obj-$(CONFIG_COMEDI_DAS6402)		+= das6402.o
obj-$(CONFIG_COMEDI_DT2801)		+= dt2801.o
obj-$(CONFIG_COMEDI_DT2811)		+= dt2811.o
obj-$(CONFIG_COMEDI_DT2814)		+= dt2814.o
obj-$(CONFIG_COMEDI_DT2815)		+= dt2815.o
obj-$(CONFIG_COMEDI_DT2817)		+= dt2817.o
obj-$(CONFIG_COMEDI_DT282X)		+= dt282x.o
obj-$(CONFIG_COMEDI_DMM32AT)		+= dmm32at.o
obj-$(CONFIG_COMEDI_FL512)		+= fl512.o
obj-$(CONFIG_COMEDI_AIO_AIO12_8)	+= aio_aio12_8.o
obj-$(CONFIG_COMEDI_AIO_IIRO_16)	+= aio_iiro_16.o
obj-$(CONFIG_COMEDI_II_PCI20KC)		+= ii_pci20kc.o
obj-$(CONFIG_COMEDI_C6XDIGIO)		+= c6xdigio.o
obj-$(CONFIG_COMEDI_MPC624)		+= mpc624.o
obj-$(CONFIG_COMEDI_ADQ12B)		+= adq12b.o
obj-$(CONFIG_COMEDI_NI_AT_A2150)	+= ni_at_a2150.o
obj-$(CONFIG_COMEDI_NI_AT_AO)		+= ni_at_ao.o
obj-$(CONFIG_COMEDI_NI_ATMIO)		+= ni_atmio.o
obj-$(CONFIG_COMEDI_NI_ATMIO16D)	+= ni_atmio16d.o
obj-$(CONFIG_COMEDI_NI_LABPC_ISA)	+= ni_labpc.o
obj-$(CONFIG_COMEDI_PCMAD)		+= pcmad.o
obj-$(CONFIG_COMEDI_PCMDA12)		+= pcmda12.o
obj-$(CONFIG_COMEDI_PCMMIO)		+= pcmmio.o
obj-$(CONFIG_COMEDI_PCMUIO)		+= pcmuio.o
obj-$(CONFIG_COMEDI_MULTIQ3)		+= multiq3.o
obj-$(CONFIG_COMEDI_S526)		+= s526.o

# Comedi PCI drivers
obj-$(CONFIG_COMEDI_8255_PCI)		+= 8255_pci.o
obj-$(CONFIG_COMEDI_ADDI_WATCHDOG)	+= addi_watchdog.o
obj-$(CONFIG_COMEDI_ADDI_APCI_1032)	+= addi_apci_1032.o
obj-$(CONFIG_COMEDI_ADDI_APCI_1500)	+= addi_apci_1500.o
obj-$(CONFIG_COMEDI_ADDI_APCI_1516)	+= addi_apci_1516.o
obj-$(CONFIG_COMEDI_ADDI_APCI_1564)	+= addi_apci_1564.o
obj-$(CONFIG_COMEDI_ADDI_APCI_16XX)	+= addi_apci_16xx.o
obj-$(CONFIG_COMEDI_ADDI_APCI_2032)	+= addi_apci_2032.o
obj-$(CONFIG_COMEDI_ADDI_APCI_2200)	+= addi_apci_2200.o
obj-$(CONFIG_COMEDI_ADDI_APCI_3120)	+= addi_apci_3120.o
obj-$(CONFIG_COMEDI_ADDI_APCI_3501)	+= addi_apci_3501.o
obj-$(CONFIG_COMEDI_ADDI_APCI_3XXX)	+= addi_apci_3xxx.o
obj-$(CONFIG_COMEDI_ADL_PCI6208)	+= adl_pci6208.o
obj-$(CONFIG_COMEDI_ADL_PCI7X3X)	+= adl_pci7x3x.o
obj-$(CONFIG_COMEDI_ADL_PCI8164)	+= adl_pci8164.o
obj-$(CONFIG_COMEDI_ADL_PCI9111)	+= adl_pci9111.o
obj-$(CONFIG_COMEDI_ADL_PCI9118)	+= adl_pci9118.o
obj-$(CONFIG_COMEDI_ADV_PCI1710)	+= adv_pci1710.o
obj-$(CONFIG_COMEDI_ADV_PCI1720)	+= adv_pci1720.o
obj-$(CONFIG_COMEDI_ADV_PCI1723)	+= adv_pci1723.o
obj-$(CONFIG_COMEDI_ADV_PCI1724)	+= adv_pci1724.o
obj-$(CONFIG_COMEDI_ADV_PCI1760)	+= adv_pci1760.o
obj-$(CONFIG_COMEDI_ADV_PCI_DIO)	+= adv_pci_dio.o
obj-$(CONFIG_COMEDI_AMPLC_DIO200_PCI)	+= amplc_dio200_pci.o
obj-$(CONFIG_COMEDI_AMPLC_PC236_PCI)	+= amplc_pci236.o
obj-$(CONFIG_COMEDI_AMPLC_PC263_PCI)	+= amplc_pci263.o
obj-$(CONFIG_COMEDI_AMPLC_PCI224)	+= amplc_pci224.o
obj-$(CONFIG_COMEDI_AMPLC_PCI230)	+= amplc_pci230.o
obj-$(CONFIG_COMEDI_CONTEC_PCI_DIO)	+= contec_pci_dio.o
obj-$(CONFIG_COMEDI_DAS08_PCI)		+= das08_pci.o
obj-$(CONFIG_COMEDI_DT3000)		+= dt3000.o
obj-$(CONFIG_COMEDI_DYNA_PCI10XX)	+= dyna_pci10xx.o
obj-$(CONFIG_COMEDI_GSC_HPDI)		+= gsc_hpdi.o
obj-$(CONFIG_COMEDI_ICP_MULTI)		+= icp_multi.o
obj-$(CONFIG_COMEDI_DAQBOARD2000)	+= daqboard2000.o
obj-$(CONFIG_COMEDI_JR3_PCI)		+= jr3_pci.o
obj-$(CONFIG_COMEDI_KE_COUNTER)		+= ke_counter.o
obj-$(CONFIG_COMEDI_CB_PCIDAS64)	+= cb_pcidas64.o
obj-$(CONFIG_COMEDI_CB_PCIDAS)		+= cb_pcidas.o
obj-$(CONFIG_COMEDI_CB_PCIDDA)		+= cb_pcidda.o
obj-$(CONFIG_COMEDI_CB_PCIMDAS)		+= cb_pcimdas.o
obj-$(CONFIG_COMEDI_CB_PCIMDDA)		+= cb_pcimdda.o
obj-$(CONFIG_COMEDI_ME4000)		+= me4000.o
obj-$(CONFIG_COMEDI_ME_DAQ)		+= me_daq.o
obj-$(CONFIG_COMEDI_NI_6527)		+= ni_6527.o
obj-$(CONFIG_COMEDI_NI_65XX)		+= ni_65xx.o
obj-$(CONFIG_COMEDI_NI_660X)		+= ni_660x.o
obj-$(CONFIG_COMEDI_NI_670X)		+= ni_670x.o
obj-$(CONFIG_COMEDI_NI_LABPC_PCI)	+= ni_labpc_pci.o
obj-$(CONFIG_COMEDI_NI_PCIDIO)		+= ni_pcidio.o
obj-$(CONFIG_COMEDI_NI_PCIMIO)		+= ni_pcimio.o
obj-$(CONFIG_COMEDI_RTD520)		+= rtd520.o
obj-$(CONFIG_COMEDI_S626)		+= s626.o
obj-$(CONFIG_COMEDI_SSV_DNP)		+= ssv_dnp.o
obj-$(CONFIG_COMEDI_MF6X4)		+= mf6x4.o

# Comedi PCMCIA drivers
obj-$(CONFIG_COMEDI_CB_DAS16_CS)	+= cb_das16_cs.o
obj-$(CONFIG_COMEDI_DAS08_CS)		+= das08_cs.o
obj-$(CONFIG_COMEDI_NI_DAQ_700_CS)	+= ni_daq_700.o
obj-$(CONFIG_COMEDI_NI_DAQ_DIO24_CS)	+= ni_daq_dio24.o
obj-$(CONFIG_COMEDI_NI_LABPC_CS)	+= ni_labpc_cs.o
obj-$(CONFIG_COMEDI_NI_MIO_CS)		+= ni_mio_cs.o
obj-$(CONFIG_COMEDI_QUATECH_DAQP_CS)	+= quatech_daqp_cs.o

# Comedi USB drivers
obj-$(CONFIG_COMEDI_DT9812)		+= dt9812.o
obj-$(CONFIG_COMEDI_NI_USB6501)		+= ni_usb6501.o
obj-$(CONFIG_COMEDI_USBDUX)		+= usbdux.o
obj-$(CONFIG_COMEDI_USBDUXFAST)		+= usbduxfast.o
obj-$(CONFIG_COMEDI_USBDUXSIGMA)	+= usbduxsigma.o
obj-$(CONFIG_COMEDI_VMK80XX)		+= vmk80xx.o

# Comedi NI drivers
obj-$(CONFIG_COMEDI_MITE)		+= mite.o
obj-$(CONFIG_COMEDI_NI_TIO)		+= ni_tio.o
obj-$(CONFIG_COMEDI_NI_TIOCMD)		+= ni_tiocmd.o
obj-$(CONFIG_COMEDI_NI_LABPC)		+= ni_labpc_common.o
obj-$(CONFIG_COMEDI_NI_LABPC_ISADMA)	+= ni_labpc_isadma.o

obj-$(CONFIG_COMEDI_8255)		+= comedi_8255.o
obj-$(CONFIG_COMEDI_8255_SA)		+= 8255.o
obj-$(CONFIG_COMEDI_AMPLC_DIO200)	+= amplc_dio200_common.o
obj-$(CONFIG_COMEDI_AMPLC_PC236)	+= amplc_pc236_common.o
obj-$(CONFIG_COMEDI_DAS08)		+= das08.o
