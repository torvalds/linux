
obj-$(CONFIG_NVME_TARGET)		+= nvmet.o
obj-$(CONFIG_NVME_TARGET_LOOP)		+= nvme-loop.o
obj-$(CONFIG_NVME_TARGET_RDMA)		+= nvmet-rdma.o

nvmet-y		+= core.o configfs.o admin-cmd.o io-cmd.o fabrics-cmd.o \
			discovery.o
nvme-loop-y	+= loop.o
nvmet-rdma-y	+= rdma.o
