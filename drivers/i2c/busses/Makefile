#
# Makefile for the i2c bus drivers.
#

obj-$(CONFIG_I2C_ALI1535)	+= i2c-ali1535.o
obj-$(CONFIG_I2C_ALI1563)	+= i2c-ali1563.o
obj-$(CONFIG_I2C_ALI15X3)	+= i2c-ali15x3.o
obj-$(CONFIG_I2C_AMD756)	+= i2c-amd756.o
obj-$(CONFIG_I2C_AMD756_S4882)	+= i2c-amd756-s4882.o
obj-$(CONFIG_I2C_AMD8111)	+= i2c-amd8111.o
obj-$(CONFIG_I2C_AT91)		+= i2c-at91.o
obj-$(CONFIG_I2C_AU1550)	+= i2c-au1550.o
obj-$(CONFIG_I2C_BLACKFIN_TWI)	+= i2c-bfin-twi.o
obj-$(CONFIG_I2C_ELEKTOR)	+= i2c-elektor.o
obj-$(CONFIG_I2C_GPIO)		+= i2c-gpio.o
obj-$(CONFIG_I2C_HYDRA)		+= i2c-hydra.o
obj-$(CONFIG_I2C_I801)		+= i2c-i801.o
obj-$(CONFIG_I2C_I810)		+= i2c-i810.o
obj-$(CONFIG_I2C_IBM_IIC)	+= i2c-ibm_iic.o
obj-$(CONFIG_I2C_IOP3XX)	+= i2c-iop3xx.o
obj-$(CONFIG_I2C_IXP2000)	+= i2c-ixp2000.o
obj-$(CONFIG_I2C_IXP4XX)	+= i2c-ixp4xx.o
obj-$(CONFIG_I2C_POWERMAC)	+= i2c-powermac.o
obj-$(CONFIG_I2C_MPC)		+= i2c-mpc.o
obj-$(CONFIG_I2C_MV64XXX)	+= i2c-mv64xxx.o
obj-$(CONFIG_I2C_NFORCE2)	+= i2c-nforce2.o
obj-$(CONFIG_I2C_OCORES)	+= i2c-ocores.o
obj-$(CONFIG_I2C_OMAP)		+= i2c-omap.o
obj-$(CONFIG_I2C_PARPORT)	+= i2c-parport.o
obj-$(CONFIG_I2C_PARPORT_LIGHT)	+= i2c-parport-light.o
obj-$(CONFIG_I2C_PASEMI)	+= i2c-pasemi.o
obj-$(CONFIG_I2C_PCA_ISA)	+= i2c-pca-isa.o
obj-$(CONFIG_I2C_PIIX4)		+= i2c-piix4.o
obj-$(CONFIG_I2C_PMCMSP)	+= i2c-pmcmsp.o
obj-$(CONFIG_I2C_PNX)		+= i2c-pnx.o
obj-$(CONFIG_I2C_PROSAVAGE)	+= i2c-prosavage.o
obj-$(CONFIG_I2C_PXA)		+= i2c-pxa.o
obj-$(CONFIG_I2C_S3C2410)	+= i2c-s3c2410.o
obj-$(CONFIG_I2C_SAVAGE4)	+= i2c-savage4.o
obj-$(CONFIG_I2C_SIBYTE)	+= i2c-sibyte.o
obj-$(CONFIG_I2C_SIMTEC)	+= i2c-simtec.o
obj-$(CONFIG_I2C_SIS5595)	+= i2c-sis5595.o
obj-$(CONFIG_I2C_SIS630)	+= i2c-sis630.o
obj-$(CONFIG_I2C_SIS96X)	+= i2c-sis96x.o
obj-$(CONFIG_I2C_STUB)		+= i2c-stub.o
obj-$(CONFIG_I2C_TAOS_EVM)	+= i2c-taos-evm.o
obj-$(CONFIG_I2C_TINY_USB)	+= i2c-tiny-usb.o
obj-$(CONFIG_I2C_VERSATILE)	+= i2c-versatile.o
obj-$(CONFIG_I2C_ACORN)		+= i2c-acorn.o
obj-$(CONFIG_I2C_VIA)		+= i2c-via.o
obj-$(CONFIG_I2C_VIAPRO)	+= i2c-viapro.o
obj-$(CONFIG_I2C_VOODOO3)	+= i2c-voodoo3.o
obj-$(CONFIG_SCx200_ACB)	+= scx200_acb.o
obj-$(CONFIG_SCx200_I2C)	+= scx200_i2c.o

ifeq ($(CONFIG_I2C_DEBUG_BUS),y)
EXTRA_CFLAGS += -DDEBUG
endif
