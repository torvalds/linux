obj-m := rtl8723ae.o


rtl8723ae-objs :=		\
		dm.o		\
		fw.o		\
		hal_btc.o	\
		hal_bt_coexist.o\
		hw.o		\
		led.o		\
		phy.o		\
		pwrseq.o	\
		pwrseqcmd.o	\
		rf.o		\
		sw.o		\
		table.o		\
		trx.o		\


obj-$(CONFIG_RTL8723AE) += rtl8723ae.o

ccflags-y += -D__CHECK_ENDIAN__
