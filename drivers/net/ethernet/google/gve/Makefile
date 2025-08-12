# Makefile for the Google virtual Ethernet (gve) driver

obj-$(CONFIG_GVE) += gve.o
gve-y := gve_main.o gve_tx.o gve_tx_dqo.o gve_rx.o gve_rx_dqo.o gve_ethtool.o gve_adminq.o gve_utils.o gve_flow_rule.o \
	    gve_buffer_mgmt_dqo.o

gve-$(CONFIG_PTP_1588_CLOCK) += gve_ptp.o
