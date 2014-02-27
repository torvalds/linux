
obj-m := wlan.o

KERNEL_DIR = /smb/rk28/mid_yifang_a10_29/kernel

all:
	make -C $(KERNEL_DIR) M=`pwd` modules

clean:
	rm -f Module.symvers *.o modules.order *.ko wifi_launcher.mod.c
	rm -rf .wifi* .tmp* wlan.mod.c
