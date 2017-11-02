# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux AX.25 and HFMODEM device drivers.
#
#
# 19971130 	Moved the amateur radio related network drivers from 
#		drivers/net/ to drivers/hamradio for easier maintenance.
#               Joerg Reuter DL1BKE <jreuter@yaina.de>
#
# 20000806	Rewritten to use lists instead of if-statements.
#		Christoph Hellwig <hch@infradead.org>
#

obj-$(CONFIG_DMASCC)		+= dmascc.o
obj-$(CONFIG_SCC)		+= scc.o
obj-$(CONFIG_MKISS)		+= mkiss.o
obj-$(CONFIG_6PACK)		+= 6pack.o
obj-$(CONFIG_YAM)		+= yam.o
obj-$(CONFIG_BPQETHER)		+= bpqether.o
obj-$(CONFIG_BAYCOM_SER_FDX)	+= baycom_ser_fdx.o	hdlcdrv.o
obj-$(CONFIG_BAYCOM_SER_HDX)	+= baycom_ser_hdx.o	hdlcdrv.o
obj-$(CONFIG_BAYCOM_PAR)	+= baycom_par.o		hdlcdrv.o
obj-$(CONFIG_BAYCOM_EPP)	+= baycom_epp.o		hdlcdrv.o
