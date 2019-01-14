# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_NET_DSA_BCM_SF2)	+= bcm-sf2.o
bcm-sf2-objs			:= bcm_sf2.o bcm_sf2_cfp.o
obj-$(CONFIG_NET_DSA_LOOP)	+= dsa_loop.o
ifdef CONFIG_NET_DSA_LOOP
obj-$(CONFIG_FIXED_PHY)		+= dsa_loop_bdinfo.o
endif
obj-$(CONFIG_NET_DSA_LANTIQ_GSWIP) += lantiq_gswip.o
obj-$(CONFIG_NET_DSA_MT7530)	+= mt7530.o
obj-$(CONFIG_NET_DSA_MV88E6060) += mv88e6060.o
obj-$(CONFIG_NET_DSA_QCA8K)	+= qca8k.o
obj-$(CONFIG_NET_DSA_REALTEK_SMI) += realtek.o
realtek-objs			:= realtek-smi.o rtl8366.o rtl8366rb.o
obj-$(CONFIG_NET_DSA_SMSC_LAN9303) += lan9303-core.o
obj-$(CONFIG_NET_DSA_SMSC_LAN9303_I2C) += lan9303_i2c.o
obj-$(CONFIG_NET_DSA_SMSC_LAN9303_MDIO) += lan9303_mdio.o
obj-$(CONFIG_NET_DSA_VITESSE_VSC73XX) += vitesse-vsc73xx.o
obj-y				+= b53/
obj-y				+= microchip/
obj-y				+= mv88e6xxx/
