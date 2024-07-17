# SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

ccflags-y += -I$(src)/../funcore -I$(src)

obj-$(CONFIG_FUN_ETH) += funeth.o

funeth-y := funeth_main.o funeth_rx.o funeth_tx.o funeth_devlink.o \
	    funeth_ethtool.o

funeth-$(CONFIG_TLS_DEVICE) += funeth_ktls.o
