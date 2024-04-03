# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the fpga framework and fpga manager drivers.
#

# Core FPGA Manager Framework
obj-$(CONFIG_FPGA)			+= fpga-mgr.o

# FPGA Manager Drivers
obj-$(CONFIG_FPGA_MGR_ALTERA_CVP)	+= altera-cvp.o
obj-$(CONFIG_FPGA_MGR_ALTERA_PS_SPI)	+= altera-ps-spi.o
obj-$(CONFIG_FPGA_MGR_ICE40_SPI)	+= ice40-spi.o
obj-$(CONFIG_FPGA_MGR_MACHXO2_SPI)	+= machxo2-spi.o
obj-$(CONFIG_FPGA_MGR_SOCFPGA)		+= socfpga.o
obj-$(CONFIG_FPGA_MGR_SOCFPGA_A10)	+= socfpga-a10.o
obj-$(CONFIG_FPGA_MGR_STRATIX10_SOC)	+= stratix10-soc.o
obj-$(CONFIG_FPGA_MGR_TS73XX)		+= ts73xx-fpga.o
obj-$(CONFIG_FPGA_MGR_XILINX_SPI)	+= xilinx-spi.o
obj-$(CONFIG_FPGA_MGR_ZYNQ_FPGA)	+= zynq-fpga.o
obj-$(CONFIG_FPGA_MGR_ZYNQMP_FPGA)	+= zynqmp-fpga.o
obj-$(CONFIG_FPGA_MGR_VERSAL_FPGA)	+= versal-fpga.o
obj-$(CONFIG_FPGA_MGR_MICROCHIP_SPI)	+= microchip-spi.o
obj-$(CONFIG_FPGA_MGR_LATTICE_SYSCONFIG)	+= lattice-sysconfig.o
obj-$(CONFIG_FPGA_MGR_LATTICE_SYSCONFIG_SPI)	+= lattice-sysconfig-spi.o
obj-$(CONFIG_ALTERA_PR_IP_CORE)		+= altera-pr-ip-core.o
obj-$(CONFIG_ALTERA_PR_IP_CORE_PLAT)	+= altera-pr-ip-core-plat.o

# FPGA Secure Update Drivers
obj-$(CONFIG_FPGA_M10_BMC_SEC_UPDATE)	+= intel-m10-bmc-sec-update.o

# FPGA Bridge Drivers
obj-$(CONFIG_FPGA_BRIDGE)		+= fpga-bridge.o
obj-$(CONFIG_SOCFPGA_FPGA_BRIDGE)	+= altera-hps2fpga.o altera-fpga2sdram.o
obj-$(CONFIG_ALTERA_FREEZE_BRIDGE)	+= altera-freeze-bridge.o
obj-$(CONFIG_XILINX_PR_DECOUPLER)	+= xilinx-pr-decoupler.o

# High Level Interfaces
obj-$(CONFIG_FPGA_REGION)		+= fpga-region.o
obj-$(CONFIG_OF_FPGA_REGION)		+= of-fpga-region.o

# FPGA Device Feature List Support
obj-$(CONFIG_FPGA_DFL)			+= dfl.o
obj-$(CONFIG_FPGA_DFL_FME)		+= dfl-fme.o
obj-$(CONFIG_FPGA_DFL_FME_MGR)		+= dfl-fme-mgr.o
obj-$(CONFIG_FPGA_DFL_FME_BRIDGE)	+= dfl-fme-br.o
obj-$(CONFIG_FPGA_DFL_FME_REGION)	+= dfl-fme-region.o
obj-$(CONFIG_FPGA_DFL_AFU)		+= dfl-afu.o

dfl-fme-objs := dfl-fme-main.o dfl-fme-pr.o dfl-fme-error.o
dfl-fme-objs += dfl-fme-perf.o
dfl-afu-objs := dfl-afu-main.o dfl-afu-region.o dfl-afu-dma-region.o
dfl-afu-objs += dfl-afu-error.o

obj-$(CONFIG_FPGA_DFL_NIOS_INTEL_PAC_N3000)	+= dfl-n3000-nios.o

# Drivers for FPGAs which implement DFL
obj-$(CONFIG_FPGA_DFL_PCI)		+= dfl-pci.o

# KUnit tests
obj-$(CONFIG_FPGA_KUNIT_TESTS)		+= tests/
