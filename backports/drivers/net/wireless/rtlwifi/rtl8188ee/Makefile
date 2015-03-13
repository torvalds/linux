rtl8188ee-objs :=		\
		dm.o		\
		fw.o		\
		hw.o		\
		led.o		\
		phy.o		\
		pwrseq.o	\
		rf.o		\
		sw.o		\
		table.o		\
		trx.o

obj-$(CONFIG_BACKPORT_RTL8188EE) += rtl8188ee.o

ccflags-y += -I$(backport_srctree)/drivers/net/wireless/rtlwifi -D__CHECK_ENDIAN__
