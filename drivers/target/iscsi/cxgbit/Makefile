# SPDX-License-Identifier: GPL-2.0
ccflags-y := -Idrivers/net/ethernet/chelsio/cxgb4
ccflags-y += -Idrivers/net/ethernet/chelsio/libcxgb
ccflags-y += -Idrivers/target/iscsi

obj-$(CONFIG_ISCSI_TARGET_CXGB4)  += cxgbit.o

cxgbit-y  := cxgbit_main.o cxgbit_cm.o cxgbit_target.o cxgbit_ddp.o
