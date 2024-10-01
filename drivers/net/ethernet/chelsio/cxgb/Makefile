# SPDX-License-Identifier: GPL-2.0-only
#
# Chelsio T1 driver
#

obj-$(CONFIG_CHELSIO_T1) += cxgb.o

cxgb-$(CONFIG_CHELSIO_T1_1G) += mv88e1xxx.o vsc7326.o
cxgb-objs := cxgb2.o espi.o tp.o pm3393.o sge.o subr.o \
	     mv88x201x.o my3126.o $(cxgb-y)
