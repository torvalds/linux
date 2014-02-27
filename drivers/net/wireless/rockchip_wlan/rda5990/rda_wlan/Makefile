obj-m := rda5890.o
rda5890-objs := \
	rda5890_if_sdio.o \
	rda5890_dev.o \
	rda5890_txrx.o \
	rda5890_wid.o \
	rda5890_wext.o \
	rda5890_debugfs.o\
	rda5890_sdio_patch.o\
	rda5890_nvram.o\
  rda5890_scan.o

KERNEL_DIR = /home/dxj/share/rk2928/kernel

all:
	make -C $(KERNEL_DIR) M=`pwd` modules

clean:
	rm -f Module.symvers *.o modules.order *.ko wifi_launcher.mod.c
	rm -rf .wifi* .tmp* wlan.mod.c