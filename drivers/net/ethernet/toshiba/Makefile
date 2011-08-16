#
# Makefile for the Toshiba network device drivers.
#

obj-$(CONFIG_GELIC_NET) += ps3_gelic.o
gelic_wireless-$(CONFIG_GELIC_WIRELESS) += ps3_gelic_wireless.o
ps3_gelic-objs += ps3_gelic_net.o $(gelic_wireless-y)
spidernet-y += spider_net.o spider_net_ethtool.o
obj-$(CONFIG_SPIDER_NET) += spidernet.o
obj-$(CONFIG_TC35815) += tc35815.o
