# DVM
obj-$(CONFIG_IWLDVM)	+= iwldvm.o
iwldvm-objs		+= main.o rs.o mac80211.o ucode.o tx.o
iwldvm-objs		+= lib.o calib.o tt.o sta.o rx.o

iwldvm-objs		+= power.o
iwldvm-objs		+= scan.o led.o
iwldvm-objs		+= rxon.o devices.o

iwldvm-$(CONFIG_IWLWIFI_DEBUGFS) += debugfs.o
iwldvm-$(CONFIG_IWLWIFI_DEVICE_TESTMODE) += testmode.o

ccflags-y += -D__CHECK_ENDIAN__ -I$(src)/../
