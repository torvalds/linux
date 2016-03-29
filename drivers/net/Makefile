#
# Makefile for the Linux network device drivers.
#

#
# Networking Core Drivers
#
obj-$(CONFIG_BONDING) += bonding/
obj-$(CONFIG_IPVLAN) += ipvlan/
obj-$(CONFIG_DUMMY) += dummy.o
obj-$(CONFIG_EQUALIZER) += eql.o
obj-$(CONFIG_IFB) += ifb.o
obj-$(CONFIG_MACSEC) += macsec.o
obj-$(CONFIG_MACVLAN) += macvlan.o
obj-$(CONFIG_MACVTAP) += macvtap.o
obj-$(CONFIG_MII) += mii.o
obj-$(CONFIG_MDIO) += mdio.o
obj-$(CONFIG_NET) += Space.o loopback.o
obj-$(CONFIG_NETCONSOLE) += netconsole.o
obj-$(CONFIG_PHYLIB) += phy/
obj-$(CONFIG_RIONET) += rionet.o
obj-$(CONFIG_NET_TEAM) += team/
obj-$(CONFIG_TUN) += tun.o
obj-$(CONFIG_VETH) += veth.o
obj-$(CONFIG_VIRTIO_NET) += virtio_net.o
obj-$(CONFIG_VXLAN) += vxlan.o
obj-$(CONFIG_GENEVE) += geneve.o
obj-$(CONFIG_NLMON) += nlmon.o
obj-$(CONFIG_NET_VRF) += vrf.o

#
# Networking Drivers
#
obj-$(CONFIG_ARCNET) += arcnet/
obj-$(CONFIG_DEV_APPLETALK) += appletalk/
obj-$(CONFIG_CAIF) += caif/
obj-$(CONFIG_CAN) += can/
obj-$(CONFIG_ETRAX_ETHERNET) += cris/
obj-$(CONFIG_NET_DSA) += dsa/
obj-$(CONFIG_ETHERNET) += ethernet/
obj-$(CONFIG_FDDI) += fddi/
obj-$(CONFIG_HIPPI) += hippi/
obj-$(CONFIG_HAMRADIO) += hamradio/
obj-$(CONFIG_IRDA) += irda/
obj-$(CONFIG_PLIP) += plip/
obj-$(CONFIG_PPP) += ppp/
obj-$(CONFIG_PPP_ASYNC) += ppp/
obj-$(CONFIG_PPP_BSDCOMP) += ppp/
obj-$(CONFIG_PPP_DEFLATE) += ppp/
obj-$(CONFIG_PPP_MPPE) += ppp/
obj-$(CONFIG_PPP_SYNC_TTY) += ppp/
obj-$(CONFIG_PPPOE) += ppp/
obj-$(CONFIG_PPPOL2TP) += ppp/
obj-$(CONFIG_PPTP) += ppp/
obj-$(CONFIG_SLIP) += slip/
obj-$(CONFIG_SLHC) += slip/
obj-$(CONFIG_NET_SB1000) += sb1000.o
obj-$(CONFIG_SUNGEM_PHY) += sungem_phy.o
obj-$(CONFIG_WAN) += wan/
obj-$(CONFIG_WLAN) += wireless/
obj-$(CONFIG_WIMAX) += wimax/
obj-$(CONFIG_IEEE802154) += ieee802154/

obj-$(CONFIG_VMXNET3) += vmxnet3/
obj-$(CONFIG_XEN_NETDEV_FRONTEND) += xen-netfront.o
obj-$(CONFIG_XEN_NETDEV_BACKEND) += xen-netback/

obj-$(CONFIG_USB_NET_DRIVERS) += usb/

obj-$(CONFIG_HYPERV_NET) += hyperv/
obj-$(CONFIG_NTB_NETDEV) += ntb_netdev.o

obj-$(CONFIG_FUJITSU_ES) += fjes/
