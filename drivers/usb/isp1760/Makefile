# SPDX-License-Identifier: GPL-2.0
isp1760-y := isp1760-core.o isp1760-if.o
isp1760-$(CONFIG_USB_ISP1760_HCD) += isp1760-hcd.o
isp1760-$(CONFIG_USB_ISP1761_UDC) += isp1760-udc.o

obj-$(CONFIG_USB_ISP1760)	+= isp1760.o
