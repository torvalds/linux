#
# USB peripheral controller drivers
#
subdir-ccflags-$(CONFIG_USB_GADGET_DEBUG)	:= -DDEBUG
subdir-ccflags-$(CONFIG_USB_GADGET_VERBOSE)	+= -DVERBOSE_DEBUG

obj-$(CONFIG_USB_GADGET)	+= udc-core.o
obj-$(CONFIG_USB_LIBCOMPOSITE)	+= libcomposite.o
libcomposite-y			:= usbstring.o config.o epautoconf.o
libcomposite-y			+= composite.o functions.o configfs.o u_f.o
obj-$(CONFIG_USB_DUMMY_HCD)	+= dummy_hcd.o
obj-$(CONFIG_USB_NET2272)	+= net2272.o
obj-$(CONFIG_USB_NET2280)	+= net2280.o
obj-$(CONFIG_USB_AMD5536UDC)	+= amd5536udc.o
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

# USB Functions
usb_f_acm-y			:= f_acm.o
obj-$(CONFIG_USB_F_ACM)		+= usb_f_acm.o
usb_f_ss_lb-y			:= f_loopback.o f_sourcesink.o
obj-$(CONFIG_USB_F_SS_LB)	+= usb_f_ss_lb.o
obj-$(CONFIG_USB_U_SERIAL)	+= u_serial.o
usb_f_serial-y			:= f_serial.o
obj-$(CONFIG_USB_F_SERIAL)	+= usb_f_serial.o
usb_f_obex-y			:= f_obex.o
obj-$(CONFIG_USB_F_OBEX)	+= usb_f_obex.o
obj-$(CONFIG_USB_U_ETHER)	+= u_ether.o
usb_f_ncm-y			:= f_ncm.o
obj-$(CONFIG_USB_F_NCM)		+= usb_f_ncm.o
usb_f_ecm-y			:= f_ecm.o
obj-$(CONFIG_USB_F_ECM)		+= usb_f_ecm.o
usb_f_phonet-y			:= f_phonet.o
obj-$(CONFIG_USB_F_PHONET)	+= usb_f_phonet.o
usb_f_eem-y			:= f_eem.o
obj-$(CONFIG_USB_F_EEM)		+= usb_f_eem.o
usb_f_ecm_subset-y		:= f_subset.o
obj-$(CONFIG_USB_F_SUBSET)	+= usb_f_ecm_subset.o
usb_f_rndis-y			:= f_rndis.o rndis.o
obj-$(CONFIG_USB_F_RNDIS)	+= usb_f_rndis.o
usb_f_mass_storage-y		:= f_mass_storage.o storage_common.o
obj-$(CONFIG_USB_F_MASS_STORAGE)+= usb_f_mass_storage.o
usb_f_fs-y			:= f_fs.o
obj-$(CONFIG_USB_F_FS)		+= usb_f_fs.o

obj-$(CONFIG_USB_GADGET)	+= legacy/
