rtl8188ee-objs :=		\
		dm.o		\
		fw.o		\
		hw.o		\
		led.o		\
		phy.o		\
		pwrseq.o	\
		pwrseqcmd.o	\
		rf.o		\
		sw.o		\
		table.o		\
		trx.o

obj-$(CONFIG_RTL8188EE) += rtl8188ee.o

ccflags-y += -Idrivers/net/wireless/rtlwifi -D__CHECK_ENDIAN__
