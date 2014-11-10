#
# Chelsio T4 driver
#

obj-$(CONFIG_CHELSIO_T4) += cxgb4.o

cxgb4-objs := cxgb4_main.o l2t.o t4_hw.o sge.o
cxgb4-$(CONFIG_CHELSIO_T4_DCB) +=  cxgb4_dcb.o
cxgb4-$(CONFIG_DEBUG_FS) += cxgb4_debugfs.o
