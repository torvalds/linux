#
# USB peripheral controller drivers
#
ccflags-$(CONFIG_USB_GADGET_DEBUG) := -DDEBUG

obj-$(CONFIG_USB_GADGET)	+= udc-core.o
obj-$(CONFIG_USB_DUMMY_HCD)	+= dummy_hcd.o
obj-$(CONFIG_USB_NET2272)	+= net2272.o
obj-$(CONFIG_USB_NET2280)	+= net2280.o
obj-$(CONFIG_USB_AMD5536UDC)	+= amd5536udc.o
obj-$(CONFIG_USB_PXA25X)	+= pxa25x_udc.o
obj-$(CONFIG_USB_PXA27X)	+= pxa27x_udc.o
obj-$(CONFIG_USB_IMX)		+= imx_udc.o
obj-$(CONFIG_USB_GOKU)		+= goku_udc.o
obj-$(CONFIG_USB_OMAP)		+= omap_udc.o
obj-$(CONFIG_USB_S3C2410)	+= s3c2410_udc.o
obj-$(CONFIG_USB_AT91)		+= at91_udc.o
obj-$(CONFIG_USB_ATMEL_USBA)	+= atmel_usba_udc.o
obj-$(CONFIG_USB_FSL_USB2)	+= fsl_usb2_udc.o
fsl_usb2_udc-y			:= fsl_udc_core.o
fsl_usb2_udc-$(CONFIG_ARCH_MXC)	+= fsl_mxc_udc.o
obj-$(CONFIG_USB_M66592)	+= m66592-udc.o
obj-$(CONFIG_USB_R8A66597)	+= r8a66597-udc.o
obj-$(CONFIG_USB_FSL_QE)	+= fsl_qe_udc.o
obj-$(CONFIG_USB_CI13XXX_PCI)	+= ci13xxx_pci.o
obj-$(CONFIG_USB_S3C_HSOTG)	+= s3c-hsotg.o
obj-$(CONFIG_USB_S3C_HSUDC)	+= s3c-hsudc.o
obj-$(CONFIG_USB_LANGWELL)	+= langwell_udc.o
obj-$(CONFIG_USB_EG20T)		+= pch_udc.o
obj-$(CONFIG_USB_PXA_U2O)	+= mv_udc.o
mv_udc-y			:= mv_udc_core.o
obj-$(CONFIG_USB_CI13XXX_MSM)	+= ci13xxx_msm.o
obj-$(CONFIG_USB_FUSB300)	+= fusb300_udc.o

#
# USB gadget drivers
#
g_zero-y			:= zero.o
g_audio-y			:= audio.o
g_ether-y			:= ether.o
g_serial-y			:= serial.o
g_midi-y			:= gmidi.o
gadgetfs-y			:= inode.o
g_file_storage-y		:= file_storage.o
g_mass_storage-y		:= mass_storage.o
g_printer-y			:= printer.o
g_cdc-y				:= cdc2.o
g_multi-y			:= multi.o
g_hid-y				:= hid.o
g_dbgp-y			:= dbgp.o
g_nokia-y			:= nokia.o
g_webcam-y			:= webcam.o
g_ncm-y				:= ncm.o
g_acm_ms-y			:= acm_ms.o

obj-$(CONFIG_USB_ZERO)		+= g_zero.o
obj-$(CONFIG_USB_AUDIO)		+= g_audio.o
obj-$(CONFIG_USB_ETH)		+= g_ether.o
obj-$(CONFIG_USB_GADGETFS)	+= gadgetfs.o
obj-$(CONFIG_USB_FUNCTIONFS)	+= g_ffs.o
obj-$(CONFIG_USB_FILE_STORAGE)	+= g_file_storage.o
obj-$(CONFIG_USB_MASS_STORAGE)	+= g_mass_storage.o
obj-$(CONFIG_USB_G_SERIAL)	+= g_serial.o
obj-$(CONFIG_USB_G_PRINTER)	+= g_printer.o
obj-$(CONFIG_USB_MIDI_GADGET)	+= g_midi.o
obj-$(CONFIG_USB_CDC_COMPOSITE) += g_cdc.o
obj-$(CONFIG_USB_G_HID)		+= g_hid.o
obj-$(CONFIG_USB_G_DBGP)	+= g_dbgp.o
obj-$(CONFIG_USB_G_MULTI)	+= g_multi.o
obj-$(CONFIG_USB_G_NOKIA)	+= g_nokia.o
obj-$(CONFIG_USB_G_WEBCAM)	+= g_webcam.o
obj-$(CONFIG_USB_G_NCM)		+= g_ncm.o
obj-$(CONFIG_USB_G_ACM_MS)	+= g_acm_ms.o
