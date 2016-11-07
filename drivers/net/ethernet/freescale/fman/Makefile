subdir-ccflags-y +=  -I$(srctree)/drivers/net/ethernet/freescale/fman

obj-$(CONFIG_FSL_FMAN) += fsl_fman.o
obj-$(CONFIG_FSL_FMAN) += fsl_fman_port.o
obj-$(CONFIG_FSL_FMAN) += fsl_mac.o

fsl_fman-objs	:= fman_muram.o fman.o fman_sp.o
fsl_fman_port-objs := fman_port.o
fsl_mac-objs:= mac.o fman_dtsec.o fman_memac.o fman_tgec.o
