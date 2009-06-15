#
# Makefile for the Linux network (ethercard) device drivers.
#

obj-$(CONFIG_E1000) += e1000/
obj-$(CONFIG_E1000E) += e1000e/
obj-$(CONFIG_IBM_NEW_EMAC) += ibm_newemac/
obj-$(CONFIG_IGB) += igb/
obj-$(CONFIG_IGBVF) += igbvf/
obj-$(CONFIG_IXGBE) += ixgbe/
obj-$(CONFIG_IXGB) += ixgb/
obj-$(CONFIG_IP1000) += ipg.o
obj-$(CONFIG_CHELSIO_T1) += chelsio/
obj-$(CONFIG_CHELSIO_T3) += cxgb3/
obj-$(CONFIG_EHEA) += ehea/
obj-$(CONFIG_CAN) += can/
obj-$(CONFIG_BONDING) += bonding/
obj-$(CONFIG_ATL1) += atlx/
obj-$(CONFIG_ATL2) += atlx/
obj-$(CONFIG_ATL1E) += atl1e/
obj-$(CONFIG_ATL1C) += atl1c/
obj-$(CONFIG_GIANFAR) += gianfar_driver.o
obj-$(CONFIG_TEHUTI) += tehuti.o
obj-$(CONFIG_ENIC) += enic/
obj-$(CONFIG_JME) += jme.o
obj-$(CONFIG_BE2NET) += benet/

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

obj-$(CONFIG_HAPPYMEAL) += sunhme.o
obj-$(CONFIG_SUNLANCE) += sunlance.o
obj-$(CONFIG_SUNQE) += sunqe.o
obj-$(CONFIG_SUNBMAC) += sunbmac.o
obj-$(CONFIG_MYRI_SBUS) += myri_sbus.o
obj-$(CONFIG_SUNGEM) += sungem.o sungem_phy.o
obj-$(CONFIG_CASSINI) += cassini.o
obj-$(CONFIG_SUNVNET) += sunvnet.o

obj-$(CONFIG_MACE) += mace.o
obj-$(CONFIG_BMAC) += bmac.o

obj-$(CONFIG_VORTEX) += 3c59x.o
obj-$(CONFIG_TYPHOON) += typhoon.o
obj-$(CONFIG_NE2K_PCI) += ne2k-pci.o 8390.o
obj-$(CONFIG_PCNET32) += pcnet32.o
obj-$(CONFIG_E100) += e100.o
obj-$(CONFIG_TLAN) += tlan.o
obj-$(CONFIG_EPIC100) += epic100.o
obj-$(CONFIG_SMSC9420) += smsc9420.o
obj-$(CONFIG_SIS190) += sis190.o
obj-$(CONFIG_SIS900) += sis900.o
obj-$(CONFIG_R6040) += r6040.o
obj-$(CONFIG_YELLOWFIN) += yellowfin.o
obj-$(CONFIG_ACENIC) += acenic.o
obj-$(CONFIG_ISERIES_VETH) += iseries_veth.o
obj-$(CONFIG_NATSEMI) += natsemi.o
obj-$(CONFIG_NS83820) += ns83820.o
obj-$(CONFIG_STNIC) += stnic.o 8390.o
obj-$(CONFIG_FEALNX) += fealnx.o
obj-$(CONFIG_TIGON3) += tg3.o
obj-$(CONFIG_BNX2) += bnx2.o
obj-$(CONFIG_CNIC) += cnic.o
obj-$(CONFIG_BNX2X) += bnx2x.o
bnx2x-objs := bnx2x_main.o bnx2x_link.o
spidernet-y += spider_net.o spider_net_ethtool.o
obj-$(CONFIG_SPIDER_NET) += spidernet.o sungem_phy.o
obj-$(CONFIG_GELIC_NET) += ps3_gelic.o
gelic_wireless-$(CONFIG_GELIC_WIRELESS) += ps3_gelic_wireless.o
ps3_gelic-objs += ps3_gelic_net.o $(gelic_wireless-y)
obj-$(CONFIG_TC35815) += tc35815.o
obj-$(CONFIG_SKGE) += skge.o
obj-$(CONFIG_SKY2) += sky2.o
obj-$(CONFIG_SKFP) += skfp/
obj-$(CONFIG_VIA_RHINE) += via-rhine.o
obj-$(CONFIG_VIA_VELOCITY) += via-velocity.o
obj-$(CONFIG_ADAPTEC_STARFIRE) += starfire.o
obj-$(CONFIG_RIONET) += rionet.o
obj-$(CONFIG_SH_ETH) += sh_eth.o

#
# end link order section
#

obj-$(CONFIG_MII) += mii.o
obj-$(CONFIG_PHYLIB) += phy/

obj-$(CONFIG_SUNDANCE) += sundance.o
obj-$(CONFIG_HAMACHI) += hamachi.o
obj-$(CONFIG_NET) += Space.o loopback.o
obj-$(CONFIG_SEEQ8005) += seeq8005.o
obj-$(CONFIG_NET_SB1000) += sb1000.o
obj-$(CONFIG_MAC8390) += mac8390.o
obj-$(CONFIG_APNE) += apne.o 8390.o
obj-$(CONFIG_PCMCIA_PCNET) += 8390.o
obj-$(CONFIG_HP100) += hp100.o
obj-$(CONFIG_SMC9194) += smc9194.o
obj-$(CONFIG_FEC) += fec.o
obj-$(CONFIG_FEC_MPC52xx) += fec_mpc52xx.o
ifeq ($(CONFIG_FEC_MPC52xx_MDIO),y)
	obj-$(CONFIG_FEC_MPC52xx) += fec_mpc52xx_phy.o
endif
obj-$(CONFIG_68360_ENET) += 68360enet.o
obj-$(CONFIG_WD80x3) += wd.o 8390.o
obj-$(CONFIG_EL2) += 3c503.o 8390p.o
obj-$(CONFIG_NE2000) += ne.o 8390p.o
obj-$(CONFIG_NE2_MCA) += ne2.o 8390p.o
obj-$(CONFIG_HPLAN) += hp.o 8390p.o
obj-$(CONFIG_HPLAN_PLUS) += hp-plus.o 8390p.o
obj-$(CONFIG_ULTRA) += smc-ultra.o 8390.o
obj-$(CONFIG_ULTRAMCA) += smc-mca.o 8390.o
obj-$(CONFIG_ULTRA32) += smc-ultra32.o 8390.o
obj-$(CONFIG_E2100) += e2100.o 8390.o
obj-$(CONFIG_ES3210) += es3210.o 8390.o
obj-$(CONFIG_LNE390) += lne390.o 8390.o
obj-$(CONFIG_NE3210) += ne3210.o 8390.o
obj-$(CONFIG_SB1250_MAC) += sb1250-mac.o
obj-$(CONFIG_B44) += b44.o
obj-$(CONFIG_FORCEDETH) += forcedeth.o
obj-$(CONFIG_NE_H8300) += ne-h8300.o 8390.o
obj-$(CONFIG_AX88796) += ax88796.o

obj-$(CONFIG_TSI108_ETH) += tsi108_eth.o
obj-$(CONFIG_MV643XX_ETH) += mv643xx_eth.o
obj-$(CONFIG_QLA3XXX) += qla3xxx.o
obj-$(CONFIG_QLGE) += qlge/

obj-$(CONFIG_PPP) += ppp_generic.o
obj-$(CONFIG_PPP_ASYNC) += ppp_async.o
obj-$(CONFIG_PPP_SYNC_TTY) += ppp_synctty.o
obj-$(CONFIG_PPP_DEFLATE) += ppp_deflate.o
obj-$(CONFIG_PPP_BSDCOMP) += bsd_comp.o
obj-$(CONFIG_PPP_MPPE) += ppp_mppe.o
obj-$(CONFIG_PPPOE) += pppox.o pppoe.o
obj-$(CONFIG_PPPOL2TP) += pppox.o pppol2tp.o

obj-$(CONFIG_SLIP) += slip.o
obj-$(CONFIG_SLHC) += slhc.o

obj-$(CONFIG_XEN_NETDEV_FRONTEND) += xen-netfront.o

obj-$(CONFIG_DUMMY) += dummy.o
obj-$(CONFIG_IFB) += ifb.o
obj-$(CONFIG_MACVLAN) += macvlan.o
obj-$(CONFIG_DE600) += de600.o
obj-$(CONFIG_DE620) += de620.o
obj-$(CONFIG_LANCE) += lance.o
obj-$(CONFIG_SUN3_82586) += sun3_82586.o
obj-$(CONFIG_SUN3LANCE) += sun3lance.o
obj-$(CONFIG_DEFXX) += defxx.o
obj-$(CONFIG_SGISEEQ) += sgiseeq.o
obj-$(CONFIG_SGI_O2MACE_ETH) += meth.o
obj-$(CONFIG_AT1700) += at1700.o
obj-$(CONFIG_EL1) += 3c501.o
obj-$(CONFIG_EL16) += 3c507.o
obj-$(CONFIG_ELMC) += 3c523.o
obj-$(CONFIG_IBMLANA) += ibmlana.o
obj-$(CONFIG_ELMC_II) += 3c527.o
obj-$(CONFIG_EL3) += 3c509.o
obj-$(CONFIG_3C515) += 3c515.o
obj-$(CONFIG_EEXPRESS) += eexpress.o
obj-$(CONFIG_EEXPRESS_PRO) += eepro.o
obj-$(CONFIG_8139CP) += 8139cp.o
obj-$(CONFIG_8139TOO) += 8139too.o
obj-$(CONFIG_ZNET) += znet.o
obj-$(CONFIG_CPMAC) += cpmac.o
obj-$(CONFIG_DEPCA) += depca.o
obj-$(CONFIG_EWRK3) += ewrk3.o
obj-$(CONFIG_ATP) += atp.o
obj-$(CONFIG_NI5010) += ni5010.o
obj-$(CONFIG_NI52) += ni52.o
obj-$(CONFIG_NI65) += ni65.o
obj-$(CONFIG_ELPLUS) += 3c505.o
obj-$(CONFIG_AC3200) += ac3200.o 8390.o
obj-$(CONFIG_APRICOT) += 82596.o
obj-$(CONFIG_LASI_82596) += lasi_82596.o
obj-$(CONFIG_SNI_82596) += sni_82596.o
obj-$(CONFIG_MVME16x_NET) += 82596.o
obj-$(CONFIG_BVME6000_NET) += 82596.o
obj-$(CONFIG_SC92031) += sc92031.o

# This is also a 82596 and should probably be merged
obj-$(CONFIG_LP486E) += lp486e.o

obj-$(CONFIG_ETH16I) += eth16i.o
obj-$(CONFIG_ZORRO8390) += zorro8390.o 8390.o
obj-$(CONFIG_HPLANCE) += hplance.o 7990.o
obj-$(CONFIG_MVME147_NET) += mvme147.o 7990.o
obj-$(CONFIG_EQUALIZER) += eql.o
obj-$(CONFIG_KORINA) += korina.o
obj-$(CONFIG_MIPS_JAZZ_SONIC) += jazzsonic.o
obj-$(CONFIG_MIPS_AU1X00_ENET) += au1000_eth.o
obj-$(CONFIG_MIPS_SIM_NET) += mipsnet.o
obj-$(CONFIG_SGI_IOC3_ETH) += ioc3-eth.o
obj-$(CONFIG_DECLANCE) += declance.o
obj-$(CONFIG_ATARILANCE) += atarilance.o
obj-$(CONFIG_A2065) += a2065.o
obj-$(CONFIG_HYDRA) += hydra.o 8390.o
obj-$(CONFIG_ARIADNE) += ariadne.o
obj-$(CONFIG_CS89x0) += cs89x0.o
obj-$(CONFIG_MACSONIC) += macsonic.o
obj-$(CONFIG_MACMACE) += macmace.o
obj-$(CONFIG_MAC89x0) += mac89x0.o
obj-$(CONFIG_TUN) += tun.o
obj-$(CONFIG_VETH) += veth.o
obj-$(CONFIG_NET_NETX) += netx-eth.o
obj-$(CONFIG_DL2K) += dl2k.o
obj-$(CONFIG_R8169) += r8169.o
obj-$(CONFIG_AMD8111_ETH) += amd8111e.o
obj-$(CONFIG_IBMVETH) += ibmveth.o
obj-$(CONFIG_S2IO) += s2io.o
obj-$(CONFIG_VXGE) += vxge/
obj-$(CONFIG_MYRI10GE) += myri10ge/
obj-$(CONFIG_SMC91X) += smc91x.o
obj-$(CONFIG_SMC911X) += smc911x.o
obj-$(CONFIG_SMSC911X) += smsc911x.o
obj-$(CONFIG_BFIN_MAC) += bfin_mac.o
obj-$(CONFIG_DM9000) += dm9000.o
obj-$(CONFIG_PASEMI_MAC) += pasemi_mac_driver.o
pasemi_mac_driver-objs := pasemi_mac.o pasemi_mac_ethtool.o
obj-$(CONFIG_MLX4_CORE) += mlx4/
obj-$(CONFIG_ENC28J60) += enc28j60.o
obj-$(CONFIG_ETHOC) += ethoc.o

obj-$(CONFIG_XTENSA_XT2000_SONIC) += xtsonic.o

obj-$(CONFIG_DNET) += dnet.o
obj-$(CONFIG_MACB) += macb.o

obj-$(CONFIG_ARM) += arm/
obj-$(CONFIG_DEV_APPLETALK) += appletalk/
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

obj-y += wireless/
obj-$(CONFIG_NET_TULIP) += tulip/
obj-$(CONFIG_HAMRADIO) += hamradio/
obj-$(CONFIG_IRDA) += irda/
obj-$(CONFIG_ETRAX_ETHERNET) += cris/
obj-$(CONFIG_ENP2611_MSF_NET) += ixp2000/

obj-$(CONFIG_NETCONSOLE) += netconsole.o

obj-$(CONFIG_FS_ENET) += fs_enet/

obj-$(CONFIG_NETXEN_NIC) += netxen/
obj-$(CONFIG_NIU) += niu.o
obj-$(CONFIG_VIRTIO_NET) += virtio_net.o
obj-$(CONFIG_SFC) += sfc/

obj-$(CONFIG_WIMAX) += wimax/
