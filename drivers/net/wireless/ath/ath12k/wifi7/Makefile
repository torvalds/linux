# SPDX-License-Identifier: BSD-3-Clause-Clear
obj-$(CONFIG_ATH12K) += ath12k_wifi7.o
ath12k_wifi7-y += core.o \
	          pci.o \
	          wmi.o \
	          mhi.o \
	          ce.o \
	          hw.o \
		  hal_tx.o \
		  hal_rx.o \
		  dp_rx.o \
		  dp_tx.o \
		  dp.o \
		  dp_mon.o \
		  hal.o \
		  hal_qcn9274.o \
		  hal_wcn7850.o \
		  hal_qcc2072.o

ath12k_wifi7-$(CONFIG_ATH12K_AHB) += ahb.o
