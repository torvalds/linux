#
# Makefile for BestComm & co
#

bestcomm-core-objs	:= bestcomm.o sram.o
bestcomm-ata-objs	:= ata.o bcom_ata_task.o
bestcomm-fec-objs	:= fec.o bcom_fec_rx_task.o bcom_fec_tx_task.o
bestcomm-gen-bd-objs	:= gen_bd.o bcom_gen_bd_rx_task.o bcom_gen_bd_tx_task.o

obj-$(CONFIG_PPC_BESTCOMM)		+= bestcomm-core.o
obj-$(CONFIG_PPC_BESTCOMM_ATA)		+= bestcomm-ata.o
obj-$(CONFIG_PPC_BESTCOMM_FEC)		+= bestcomm-fec.o
obj-$(CONFIG_PPC_BESTCOMM_GEN_BD)	+= bestcomm-gen-bd.o
 
