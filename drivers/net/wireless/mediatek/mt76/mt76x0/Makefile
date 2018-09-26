obj-$(CONFIG_MT76x0U)    += mt76x0.o

mt76x0-objs	= \
	usb.o init.o main.o mcu.o trace.o dma.o eeprom.o phy.o \
	mac.o util.o debugfs.o tx.o core.o
# ccflags-y := -DDEBUG
CFLAGS_trace.o := -I$(src)
