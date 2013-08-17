sfc-y			+= efx.o nic.o falcon.o siena.o tx.o rx.o filter.o \
			   falcon_xmac.o mcdi_mac.o \
			   selftest.o ethtool.o qt202x_phy.o mdio_10g.o \
			   tenxpress.o txc43128_phy.o falcon_boards.o \
			   mcdi.o mcdi_phy.o mcdi_mon.o
sfc-$(CONFIG_SFC_MTD)	+= mtd.o
sfc-$(CONFIG_SFC_SRIOV)	+= siena_sriov.o

obj-$(CONFIG_SFC)	+= sfc.o
