# SPDX-License-Identifier: GPL-2.0
# define_trace.h needs to know how to find our header
CFLAGS_trace.o			:= -I$(src)

udc-core-y			:= core.o trace.o

#
# USB peripheral controller drivers
#
obj-$(CONFIG_USB_GADGET)	+= udc-core.o
obj-$(CONFIG_USB_DUMMY_HCD)	+= dummy_hcd.o
obj-$(CONFIG_USB_NET2272)	+= net2272.o
obj-$(CONFIG_USB_NET2280)	+= net2280.o
obj-$(CONFIG_USB_SNP_CORE)	+= snps_udc_core.o
obj-$(CONFIG_USB_AMD5536UDC)	+= amd5536udc_pci.o
obj-$(CONFIG_USB_PXA25X)	+= pxa25x_udc.o
obj-$(CONFIG_USB_PXA27X)	+= pxa27x_udc.o
obj-$(CONFIG_USB_GOKU)		+= goku_udc.o
obj-$(CONFIG_USB_OMAP)		+= omap_udc.o
obj-$(CONFIG_USB_S3C2410)	+= s3c2410_udc.o
obj-$(CONFIG_USB_AT91)		+= at91_udc.o
obj-$(CONFIG_USB_ATMEL_USBA)	+= atmel_usba_udc.o
obj-$(CONFIG_USB_BCM63XX_UDC)	+= bcm63xx_udc.o
obj-$(CONFIG_USB_FSL_USB2)	+= fsl_usb2_udc.o
fsl_usb2_udc-y			:= fsl_udc_core.o
fsl_usb2_udc-$(CONFIG_ARCH_MXC)	+= fsl_mxc_udc.o
obj-$(CONFIG_USB_M66592)	+= m66592-udc.o
obj-$(CONFIG_USB_R8A66597)	+= r8a66597-udc.o
obj-$(CONFIG_USB_RENESAS_USB3)	+= renesas_usb3.o
obj-$(CONFIG_USB_FSL_QE)	+= fsl_qe_udc.o
obj-$(CONFIG_USB_S3C_HSUDC)	+= s3c-hsudc.o
obj-$(CONFIG_USB_LPC32XX)	+= lpc32xx_udc.o
obj-$(CONFIG_USB_EG20T)		+= pch_udc.o
obj-$(CONFIG_USB_MV_UDC)	+= mv_udc.o
mv_udc-y			:= mv_udc_core.o
obj-$(CONFIG_USB_FUSB300)	+= fusb300_udc.o
obj-$(CONFIG_USB_FOTG210_UDC)	+= fotg210-udc.o
obj-$(CONFIG_USB_MV_U3D)	+= mv_u3d_core.o
obj-$(CONFIG_USB_GR_UDC)	+= gr_udc.o
obj-$(CONFIG_USB_GADGET_XILINX)	+= udc-xilinx.o
obj-$(CONFIG_USB_SNP_UDC_PLAT) += snps_udc_plat.o
obj-$(CONFIG_USB_BDC_UDC)	+= bdc/
