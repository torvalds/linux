ccflags-y := -Idrivers/net/ethernet/chelsio/cxgb4
ccflags-y += -Idrivers/net/ethernet/chelsio/libcxgb

obj-$(CONFIG_INFINIBAND_CXGB4) += iw_cxgb4.o

iw_cxgb4-y :=  device.o cm.o provider.o mem.o cq.o qp.o resource.o ev.o id_table.o
