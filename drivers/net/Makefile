#
# Makefile for the Linux network device drivers.
#

obj-$(CONFIG_MII) += mii.o
obj-$(CONFIG_MDIO) += mdio.o
obj-$(CONFIG_PHYLIB) += phy/
obj-$(CONFIG_CAN) += can/
obj-$(CONFIG_BONDING) += bonding/
obj-$(CONFIG_VMXNET3) += vmxnet3/

#
# link order important here
#
obj-$(CONFIG_PLIP) += plip.o
obj-$(CONFIG_ROADRUNNER) += rrunner.o
obj-$(CONFIG_RIONET) += rionet.o

#
# end link order section
#

obj-$(CONFIG_NET) += Space.o loopback.o
obj-$(CONFIG_NET_SB1000) += sb1000.o
obj-$(CONFIG_PPP) += ppp_generic.o
obj-$(CONFIG_PPP_ASYNC) += ppp_async.o
obj-$(CONFIG_PPP_SYNC_TTY) += ppp_synctty.o
obj-$(CONFIG_PPP_DEFLATE) += ppp_deflate.o
obj-$(CONFIG_PPP_BSDCOMP) += bsd_comp.o
obj-$(CONFIG_PPP_MPPE) += ppp_mppe.o
obj-$(CONFIG_PPPOE) += pppox.o pppoe.o
obj-$(CONFIG_PPPOL2TP) += pppox.o
obj-$(CONFIG_PPTP) += pppox.o pptp.o
obj-$(CONFIG_SLIP) += slip.o
obj-$(CONFIG_SLHC) += slhc.o

obj-$(CONFIG_XEN_NETDEV_FRONTEND) += xen-netfront.o
obj-$(CONFIG_XEN_NETDEV_BACKEND) += xen-netback/

obj-$(CONFIG_DUMMY) += dummy.o
obj-$(CONFIG_IFB) += ifb.o
obj-$(CONFIG_MACVLAN) += macvlan.o
obj-$(CONFIG_MACVTAP) += macvtap.o
obj-$(CONFIG_EQUALIZER) += eql.o
obj-$(CONFIG_TUN) += tun.o
obj-$(CONFIG_VETH) += veth.o

obj-$(CONFIG_DEV_APPLETALK) += appletalk/
obj-$(CONFIG_ETHERNET) += ethernet/
obj-$(CONFIG_FDDI) += fddi/
obj-$(CONFIG_TR) += tokenring/
obj-$(CONFIG_WAN) += wan/
obj-$(CONFIG_ARCNET) += arcnet/
obj-$(CONFIG_NET_PCMCIA) += pcmcia/

obj-$(CONFIG_USB_CATC)          += usb/
obj-$(CONFIG_USB_KAWETH)        += usb/
obj-$(CONFIG_USB_PEGASUS)       += usb/
obj-$(CONFIG_USB_RTL8150)       += usb/
obj-$(CONFIG_USB_HSO)		+= usb/
obj-$(CONFIG_USB_USBNET)        += usb/
obj-$(CONFIG_USB_ZD1201)        += usb/
obj-$(CONFIG_USB_IPHETH)        += usb/
obj-$(CONFIG_USB_CDC_PHONET)   += usb/

obj-$(CONFIG_WLAN) += wireless/
obj-$(CONFIG_HAMRADIO) += hamradio/
obj-$(CONFIG_IRDA) += irda/
obj-$(CONFIG_ETRAX_ETHERNET) += cris/

obj-$(CONFIG_NETCONSOLE) += netconsole.o

obj-$(CONFIG_VIRTIO_NET) += virtio_net.o

obj-$(CONFIG_WIMAX) += wimax/
obj-$(CONFIG_CAIF) += caif/

obj-$(CONFIG_SUNGEM_PHY) += sungem_phy.o
