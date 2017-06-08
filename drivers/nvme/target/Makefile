
obj-$(CONFIG_NVME_TARGET)		+= nvmet.o
obj-$(CONFIG_NVME_TARGET_LOOP)		+= nvme-loop.o
obj-$(CONFIG_NVME_TARGET_RDMA)		+= nvmet-rdma.o
obj-$(CONFIG_NVME_TARGET_FC)		+= nvmet-fc.o
obj-$(CONFIG_NVME_TARGET_FCLOOP)	+= nvme-fcloop.o

nvmet-y		+= core.o configfs.o admin-cmd.o io-cmd.o fabrics-cmd.o \
			discovery.o
nvme-loop-y	+= loop.o
nvmet-rdma-y	+= rdma.o
nvmet-fc-y	+= fc.o
nvme-fcloop-y	+= fcloop.o
