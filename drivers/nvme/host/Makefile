
obj-$(CONFIG_BLK_DEV_NVME)     += nvme.o

lightnvm-$(CONFIG_NVM)	:= lightnvm.o
nvme-y		+= pci.o scsi.o $(lightnvm-y)
