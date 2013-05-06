obj-$(CONFIG_VIDEO_GO7007) += go7007.o
obj-$(CONFIG_VIDEO_GO7007_USB) += go7007-usb.o
obj-$(CONFIG_VIDEO_GO7007_LOADER) += go7007-loader.o
obj-$(CONFIG_VIDEO_GO7007_USB_S2250_BOARD) += s2250.o

go7007-y := go7007-v4l2.o go7007-driver.o go7007-i2c.o go7007-fw.o \
		snd-go7007.o

s2250-y := s2250-board.o

# Uncomment when the saa7134 patches get into upstream
#obj-$(CONFIG_VIDEO_SAA7134) += saa7134-go7007.o
#ccflags-$(CONFIG_VIDEO_SAA7134:m=y) += -Idrivers/media/pci/saa7134

ccflags-$(CONFIG_VIDEO_GO7007_LOADER:m=y) += -Idrivers/media/common
