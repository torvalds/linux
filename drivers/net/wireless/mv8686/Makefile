#
# Makefile for MV8686 wifi driver.
#

mv8686-objs := main.o wext.o rx.o tx.o cmd.o cmdresp.o \
               scan.o 11d.o	assoc.o if_sdio.o \
			   ../wifi_power/wifi_power_ops.o mv8686_cis.o \
			   ../wifi_power/wifi_power_save.o

obj-$(CONFIG_MV8686)     += mv8686.o ../wifi_power/wifi_power.o ../wifi_power/wifi_config.o
