obj-m := rtl8723be.o


rtl8723be-objs :=		\
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
		trx.o		\


obj-$(CONFIG_RTL8723BE) += rtl8723be.o

ccflags-y += -D__CHECK_ENDIAN__
