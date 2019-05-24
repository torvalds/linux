#
# Makefile for the Hisilicon RoCE drivers.
#

ccflags-y :=  -I $(srctree)/drivers/net/ethernet/hisilicon/hns3

obj-$(CONFIG_INFINIBAND_HNS) += hns-roce.o
hns-roce-objs := hns_roce_main.o hns_roce_cmd.o hns_roce_pd.o \
	hns_roce_ah.o hns_roce_hem.o hns_roce_mr.o hns_roce_qp.o \
	hns_roce_cq.o hns_roce_alloc.o hns_roce_db.o hns_roce_srq.o hns_roce_restrack.o
obj-$(CONFIG_INFINIBAND_HNS_HIP06) += hns-roce-hw-v1.o
hns-roce-hw-v1-objs := hns_roce_hw_v1.o
obj-$(CONFIG_INFINIBAND_HNS_HIP08) += hns-roce-hw-v2.o
hns-roce-hw-v2-objs := hns_roce_hw_v2.o hns_roce_hw_v2_dfx.o
