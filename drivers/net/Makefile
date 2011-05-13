#
# Makefile for the Linux network device drivers.
#

obj-$(CONFIG_MII) += mii.o
obj-$(CONFIG_MDIO) += mdio.o
obj-$(CONFIG_PHYLIB) += phy/

obj-$(CONFIG_TI_DAVINCI_EMAC) += davinci_emac.o
obj-$(CONFIG_TI_DAVINCI_MDIO) += davinci_mdio.o
obj-$(CONFIG_TI_DAVINCI_CPDMA) += davinci_cpdma.o

obj-$(CONFIG_IBM_NEW_EMAC) += ibm_newemac/
obj-$(CONFIG_IP1000) += ipg.o
obj-$(CONFIG_EHEA) += ehea/
obj-$(CONFIG_CAN) += can/
obj-$(CONFIG_BONDING) += bonding/
obj-$(CONFIG_ATL1) += atlx/
obj-$(CONFIG_ATL2) += atlx/
obj-$(CONFIG_ATL1E) += atl1e/
obj-$(CONFIG_ATL1C) += atl1c/
obj-$(CONFIG_GIANFAR) += gianfar_driver.o
obj-$(CONFIG_PTP_1588_CLOCK_GIANFAR) += gianfar_ptp.o
obj-$(CONFIG_TEHUTI) += tehuti.o
obj-$(CONFIG_ENIC) += enic/
obj-$(CONFIG_JME) += jme.o
obj-$(CONFIG_VMXNET3) += vmxnet3/

gianfar_driver-objs := gianfar.o \
		gianfar_ethtool.o \
		gianfar_sysfs.o

obj-$(CONFIG_UCC_GETH) += ucc_geth_driver.o
ucc_geth_driver-objs := ucc_geth.o ucc_geth_ethtool.o

obj-$(CONFIG_FSL_PQ_MDIO) += fsl_pq_mdio.o

#
# link order important here
#
obj-$(CONFIG_PLIP) += plip.o

obj-$(CONFIG_ROADRUNNER) += rrunner.o

obj-$(CONFIG_MACE) += mace.o
obj-$(CONFIG_BMAC) += bmac.o

obj-$(CONFIG_TLAN) += tlan.o
obj-$(CONFIG_SIS190) += sis190.o
obj-$(CONFIG_SIS900) += sis900.o
obj-$(CONFIG_R6040) += r6040.o
obj-$(CONFIG_YELLOWFIN) += yellowfin.o
obj-$(CONFIG_ISERIES_VETH) += iseries_veth.o
obj-$(CONFIG_NATSEMI) += natsemi.o
obj-$(CONFIG_NS83820) += ns83820.o
obj-$(CONFIG_FEALNX) += fealnx.o
spidernet-y += spider_net.o spider_net_ethtool.o
obj-$(CONFIG_SPIDER_NET) += spidernet.o ethernet/sun/sungem_phy.o
obj-$(CONFIG_GELIC_NET) += ps3_gelic.o
gelic_wireless-$(CONFIG_GELIC_WIRELESS) += ps3_gelic_wireless.o
ps3_gelic-objs += ps3_gelic_net.o $(gelic_wireless-y)
obj-$(CONFIG_TC35815) += tc35815.o
obj-$(CONFIG_SKGE) += skge.o
obj-$(CONFIG_SKY2) += sky2.o
obj-$(CONFIG_SKFP) += skfp/
obj-$(CONFIG_KS8842)	+= ks8842.o
obj-$(CONFIG_KS8851)	+= ks8851.o
obj-$(CONFIG_KS8851_MLL)	+= ks8851_mll.o
obj-$(CONFIG_KSZ884X_PCI)	+= ksz884x.o
obj-$(CONFIG_VIA_RHINE) += via-rhine.o
obj-$(CONFIG_VIA_VELOCITY) += via-velocity.o
obj-$(CONFIG_ADAPTEC_STARFIRE) += starfire.o
obj-$(CONFIG_RIONET) += rionet.o
obj-$(CONFIG_SH_ETH) += sh_eth.o
obj-$(CONFIG_STMMAC_ETH) += stmmac/

#
# end link order section
#

obj-$(CONFIG_SUNDANCE) += sundance.o
obj-$(CONFIG_HAMACHI) += hamachi.o
obj-$(CONFIG_NET) += Space.o loopback.o
obj-$(CONFIG_SEEQ8005) += seeq8005.o
obj-$(CONFIG_NET_SB1000) += sb1000.o
obj-$(CONFIG_HP100) += hp100.o
obj-$(CONFIG_FEC) += fec.o
obj-$(CONFIG_FEC_MPC52xx) += fec_mpc52xx.o
ifeq ($(CONFIG_FEC_MPC52xx_MDIO),y)
	obj-$(CONFIG_FEC_MPC52xx) += fec_mpc52xx_phy.o
endif
obj-$(CONFIG_FORCEDETH) += forcedeth.o
obj-$(CONFIG_FTGMAC100) += ftgmac100.o
obj-$(CONFIG_FTMAC100) += ftmac100.o

obj-$(CONFIG_TSI108_ETH) += tsi108_eth.o
obj-$(CONFIG_MV643XX_ETH) += mv643xx_eth.o
ll_temac-objs := ll_temac_main.o ll_temac_mdio.o
obj-$(CONFIG_XILINX_LL_TEMAC) += ll_temac.o
obj-$(CONFIG_XILINX_EMACLITE) += xilinx_emaclite.o

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
obj-$(CONFIG_DE600) += de600.o
obj-$(CONFIG_DE620) += de620.o
obj-$(CONFIG_DEFXX) += defxx.o
obj-$(CONFIG_SGISEEQ) += sgiseeq.o
obj-$(CONFIG_SGI_O2MACE_ETH) += meth.o
obj-$(CONFIG_AT1700) += at1700.o
obj-$(CONFIG_IBMLANA) += ibmlana.o
obj-$(CONFIG_8139CP) += 8139cp.o
obj-$(CONFIG_8139TOO) += 8139too.o
obj-$(CONFIG_CPMAC) += cpmac.o
obj-$(CONFIG_EWRK3) += ewrk3.o
obj-$(CONFIG_ATP) += atp.o
obj-$(CONFIG_SC92031) += sc92031.o

obj-$(CONFIG_ETH16I) += eth16i.o
obj-$(CONFIG_EQUALIZER) += eql.o
obj-$(CONFIG_KORINA) += korina.o
obj-$(CONFIG_MIPS_JAZZ_SONIC) += jazzsonic.o
obj-$(CONFIG_MIPS_SIM_NET) += mipsnet.o
obj-$(CONFIG_SGI_IOC3_ETH) += ioc3-eth.o
obj-$(CONFIG_CS89x0) += cs89x0.o
obj-$(CONFIG_MACSONIC) += macsonic.o
obj-$(CONFIG_MACMACE) += macmace.o
obj-$(CONFIG_MAC89x0) += mac89x0.o
obj-$(CONFIG_TUN) += tun.o
obj-$(CONFIG_VETH) += veth.o
obj-$(CONFIG_NET_NETX) += netx-eth.o
obj-$(CONFIG_DL2K) += dl2k.o
obj-$(CONFIG_R8169) += r8169.o
obj-$(CONFIG_IBMVETH) += ibmveth.o
obj-$(CONFIG_S2IO) += s2io.o
obj-$(CONFIG_VXGE) += vxge/
obj-$(CONFIG_PXA168_ETH) += pxa168_eth.o
obj-$(CONFIG_BFIN_MAC) += bfin_mac.o
obj-$(CONFIG_DM9000) += dm9000.o
obj-$(CONFIG_PASEMI_MAC) += pasemi_mac_driver.o
pasemi_mac_driver-objs := pasemi_mac.o pasemi_mac_ethtool.o
obj-$(CONFIG_ENC28J60) += enc28j60.o
obj-$(CONFIG_ETHOC) += ethoc.o
obj-$(CONFIG_GRETH) += greth.o
obj-$(CONFIG_LANTIQ_ETOP) += lantiq_etop.o

obj-$(CONFIG_XTENSA_XT2000_SONIC) += xtsonic.o

obj-$(CONFIG_DNET) += dnet.o
obj-$(CONFIG_MACB) += macb.o
obj-$(CONFIG_S6GMAC) += s6gmac.o

obj-$(CONFIG_ARM) += arm/
obj-$(CONFIG_DEV_APPLETALK) += appletalk/
obj-$(CONFIG_ETHERNET) += ethernet/
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
obj-$(CONFIG_NET_TULIP) += tulip/
obj-$(CONFIG_HAMRADIO) += hamradio/
obj-$(CONFIG_IRDA) += irda/
obj-$(CONFIG_ETRAX_ETHERNET) += cris/
obj-$(CONFIG_ENP2611_MSF_NET) += ixp2000/

obj-$(CONFIG_NETCONSOLE) += netconsole.o

obj-$(CONFIG_FS_ENET) += fs_enet/

obj-$(CONFIG_VIRTIO_NET) += virtio_net.o

obj-$(CONFIG_WIMAX) += wimax/
obj-$(CONFIG_CAIF) += caif/

obj-$(CONFIG_OCTEON_MGMT_ETHERNET) += octeon/
obj-$(CONFIG_PCH_GBE) += pch_gbe/
obj-$(CONFIG_TILE_NET) += tile/
