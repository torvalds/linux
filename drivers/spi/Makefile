#
# Makefile for kernel SPI drivers.
#

ccflags-$(CONFIG_SPI_DEBUG) := -DDEBUG

# small core, mostly translating board-specific
# config declarations into driver model code
obj-$(CONFIG_SPI_MASTER)		+= spi.o
obj-$(CONFIG_SPI_SPIDEV)		+= spidev.o

# SPI master controller drivers (bus)
obj-$(CONFIG_SPI_ALTERA)		+= spi-altera.o
obj-$(CONFIG_SPI_ATMEL)			+= spi-atmel.o
obj-$(CONFIG_SPI_ATH79)			+= spi-ath79.o
obj-$(CONFIG_SPI_AU1550)		+= spi-au1550.o
obj-$(CONFIG_SPI_BCM2835)		+= spi-bcm2835.o
obj-$(CONFIG_SPI_BCM63XX)		+= spi-bcm63xx.o
obj-$(CONFIG_SPI_BCM63XX_HSSPI)		+= spi-bcm63xx-hsspi.o
obj-$(CONFIG_SPI_BFIN5XX)		+= spi-bfin5xx.o
obj-$(CONFIG_SPI_ADI_V3)                += spi-adi-v3.o
obj-$(CONFIG_SPI_BFIN_SPORT)		+= spi-bfin-sport.o
obj-$(CONFIG_SPI_BITBANG)		+= spi-bitbang.o
obj-$(CONFIG_SPI_BUTTERFLY)		+= spi-butterfly.o
obj-$(CONFIG_SPI_CADENCE)		+= spi-cadence.o
obj-$(CONFIG_SPI_CLPS711X)		+= spi-clps711x.o
obj-$(CONFIG_SPI_COLDFIRE_QSPI)		+= spi-coldfire-qspi.o
obj-$(CONFIG_SPI_DAVINCI)		+= spi-davinci.o
obj-$(CONFIG_SPI_DESIGNWARE)		+= spi-dw.o
obj-$(CONFIG_SPI_DW_MMIO)		+= spi-dw-mmio.o
obj-$(CONFIG_SPI_DW_PCI)		+= spi-dw-midpci.o
spi-dw-midpci-objs			:= spi-dw-pci.o spi-dw-mid.o
obj-$(CONFIG_SPI_EFM32)			+= spi-efm32.o
obj-$(CONFIG_SPI_EP93XX)		+= spi-ep93xx.o
obj-$(CONFIG_SPI_FALCON)		+= spi-falcon.o
obj-$(CONFIG_SPI_FSL_CPM)		+= spi-fsl-cpm.o
obj-$(CONFIG_SPI_FSL_DSPI)		+= spi-fsl-dspi.o
obj-$(CONFIG_SPI_FSL_LIB)		+= spi-fsl-lib.o
obj-$(CONFIG_SPI_FSL_ESPI)		+= spi-fsl-espi.o
obj-$(CONFIG_SPI_FSL_SPI)		+= spi-fsl-spi.o
obj-$(CONFIG_SPI_GPIO)			+= spi-gpio.o
obj-$(CONFIG_SPI_IMX)			+= spi-imx.o
obj-$(CONFIG_SPI_LM70_LLP)		+= spi-lm70llp.o
obj-$(CONFIG_SPI_MPC512x_PSC)		+= spi-mpc512x-psc.o
obj-$(CONFIG_SPI_MPC52xx_PSC)		+= spi-mpc52xx-psc.o
obj-$(CONFIG_SPI_MPC52xx)		+= spi-mpc52xx.o
obj-$(CONFIG_SPI_MXS)			+= spi-mxs.o
obj-$(CONFIG_SPI_NUC900)		+= spi-nuc900.o
obj-$(CONFIG_SPI_OC_TINY)		+= spi-oc-tiny.o
obj-$(CONFIG_SPI_OCTEON)		+= spi-octeon.o
obj-$(CONFIG_SPI_OMAP_UWIRE)		+= spi-omap-uwire.o
obj-$(CONFIG_SPI_OMAP_100K)		+= spi-omap-100k.o
obj-$(CONFIG_SPI_OMAP24XX)		+= spi-omap2-mcspi.o
obj-$(CONFIG_SPI_TI_QSPI)		+= spi-ti-qspi.o
obj-$(CONFIG_SPI_ORION)			+= spi-orion.o
obj-$(CONFIG_SPI_PL022)			+= spi-pl022.o
obj-$(CONFIG_SPI_PPC4xx)		+= spi-ppc4xx.o
spi-pxa2xx-platform-objs		:= spi-pxa2xx.o
spi-pxa2xx-platform-$(CONFIG_SPI_PXA2XX_PXADMA)	+= spi-pxa2xx-pxadma.o
spi-pxa2xx-platform-$(CONFIG_SPI_PXA2XX_DMA)	+= spi-pxa2xx-dma.o
obj-$(CONFIG_SPI_PXA2XX)		+= spi-pxa2xx-platform.o
obj-$(CONFIG_SPI_PXA2XX_PCI)		+= spi-pxa2xx-pci.o
obj-$(CONFIG_SPI_QUP)			+= spi-qup.o
obj-$(CONFIG_SPI_ROCKCHIP)		+= spi-rockchip.o
obj-$(CONFIG_SPI_RSPI)			+= spi-rspi.o
obj-$(CONFIG_SPI_S3C24XX)		+= spi-s3c24xx-hw.o
spi-s3c24xx-hw-y			:= spi-s3c24xx.o
spi-s3c24xx-hw-$(CONFIG_SPI_S3C24XX_FIQ) += spi-s3c24xx-fiq.o
obj-$(CONFIG_SPI_S3C64XX)		+= spi-s3c64xx.o
obj-$(CONFIG_SPI_SC18IS602)		+= spi-sc18is602.o
obj-$(CONFIG_SPI_SH)			+= spi-sh.o
obj-$(CONFIG_SPI_SH_HSPI)		+= spi-sh-hspi.o
obj-$(CONFIG_SPI_SH_MSIOF)		+= spi-sh-msiof.o
obj-$(CONFIG_SPI_SH_SCI)		+= spi-sh-sci.o
obj-$(CONFIG_SPI_SIRF)		+= spi-sirf.o
obj-$(CONFIG_SPI_SUN4I)			+= spi-sun4i.o
obj-$(CONFIG_SPI_SUN6I)			+= spi-sun6i.o
obj-$(CONFIG_SPI_TEGRA114)		+= spi-tegra114.o
obj-$(CONFIG_SPI_TEGRA20_SFLASH)	+= spi-tegra20-sflash.o
obj-$(CONFIG_SPI_TEGRA20_SLINK)		+= spi-tegra20-slink.o
obj-$(CONFIG_SPI_TLE62X0)		+= spi-tle62x0.o
obj-$(CONFIG_SPI_TOPCLIFF_PCH)		+= spi-topcliff-pch.o
obj-$(CONFIG_SPI_TXX9)			+= spi-txx9.o
obj-$(CONFIG_SPI_XCOMM)		+= spi-xcomm.o
obj-$(CONFIG_SPI_XILINX)		+= spi-xilinx.o
obj-$(CONFIG_SPI_XTENSA_XTFPGA)		+= spi-xtensa-xtfpga.o
