#
# Makefile for RapidIO devices
#

obj-$(CONFIG_RAPIDIO_TSI721)	+= tsi721_mport.o
tsi721_mport-y			:= tsi721.o
tsi721_mport-$(CONFIG_RAPIDIO_DMA_ENGINE) += tsi721_dma.o
obj-$(CONFIG_RAPIDIO_MPORT_CDEV) += rio_mport_cdev.o
