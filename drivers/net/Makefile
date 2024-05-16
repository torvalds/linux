# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux network device drivers.
#

#
# Networking Core Drivers
#
obj-$(CONFIG_BONDING) += bonding/
obj-$(CONFIG_IPVLAN) += ipvlan/
obj-$(CONFIG_IPVTAP) += ipvlan/
obj-$(CONFIG_DUMMY) += dummy.o
obj-$(CONFIG_WIREGUARD) += wireguard/
obj-$(CONFIG_EQUALIZER) += eql.o
obj-$(CONFIG_IFB) += ifb.o
obj-$(CONFIG_MACSEC) += macsec.o
obj-$(CONFIG_AMT) += amt.o
obj-$(CONFIG_MACVLAN) += macvlan.o
obj-$(CONFIG_MACVTAP) += macvtap.o
obj-$(CONFIG_MII) += mii.o
obj-$(CONFIG_MDIO) += mdio.o
obj-$(CONFIG_NET) += loopback.o
obj-$(CONFIG_NETDEV_LEGACY_INIT) += Space.o
obj-$(CONFIG_NETCONSOLE) += netconsole.o
obj-y += phy/
obj-y += pse-pd/
obj-y += mdio/
obj-y += pcs/
obj-$(CONFIG_RIONET) += rionet.o
obj-$(CONFIG_NET_TEAM) += team/
obj-$(CONFIG_TUN) += tun.o
obj-$(CONFIG_TAP) += tap.o
obj-$(CONFIG_VETH) += veth.o
obj-$(CONFIG_VIRTIO_NET) += virtio_net.o
obj-$(CONFIG_VXLAN) += vxlan/
obj-$(CONFIG_GENEVE) += geneve.o
obj-$(CONFIG_BAREUDP) += bareudp.o
obj-$(CONFIG_GTP) += gtp.o
obj-$(CONFIG_NLMON) += nlmon.o
obj-$(CONFIG_NET_VRF) += vrf.o
obj-$(CONFIG_VSOCKMON) += vsockmon.o
obj-$(CONFIG_MHI_NET) += mhi_net.o

#
# Networking Drivers
#
obj-$(CONFIG_ARCNET) += arcnet/
obj-$(CONFIG_DEV_APPLETALK) += appletalk/
obj-$(CONFIG_CAIF) += caif/
obj-$(CONFIG_CAN) += can/
ifdef CONFIG_NET_DSA
obj-y += dsa/
endif
obj-$(CONFIG_ETHERNET) += ethernet/
obj-$(CONFIG_FDDI) += fddi/
obj-$(CONFIG_HIPPI) += hippi/
obj-$(CONFIG_HAMRADIO) += hamradio/
obj-$(CONFIG_QCOM_IPA) += ipa/
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
obj-$(CONFIG_IEEE802154) += ieee802154/
obj-$(CONFIG_WWAN) += wwan/
obj-$(CONFIG_MCTP) += mctp/

obj-$(CONFIG_VMXNET3) += vmxnet3/
obj-$(CONFIG_XEN_NETDEV_FRONTEND) += xen-netfront.o
obj-$(CONFIG_XEN_NETDEV_BACKEND) += xen-netback/

obj-$(CONFIG_USB_NET_DRIVERS) += usb/

obj-$(CONFIG_HYPERV_NET) += hyperv/
obj-$(CONFIG_NTB_NETDEV) += ntb_netdev.o

obj-$(CONFIG_FUJITSU_ES) += fjes/

thunderbolt-net-y += thunderbolt.o
obj-$(CONFIG_USB4_NET) += thunderbolt-net.o
obj-$(CONFIG_NETDEVSIM) += netdevsim/
obj-$(CONFIG_NET_FAILOVER) += net_failover.o
