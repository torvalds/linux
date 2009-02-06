obj-$(CONFIG_OTUS)	+= arusb_lnx.o

EXTRA_CFLAGS += -DAMAC
EXTRA_CFLAGS += -DGCCK
EXTRA_CFLAGS += -DOFDM
EXTRA_CFLAGS += -DTXQ_IN_ISR
EXTRA_CFLAGS += -DWLAN_HOSTIF=0 #0:USB, 1:PCI

#Test Mode
EXTRA_CFLAGS += -DZM_USB_STREAM_MODE=1
EXTRA_CFLAGS += -DZM_USB_TX_STREAM_MODE=0
EXTRA_CFLAGS += -DZM_PCI_DMA_TEST=0
EXTRA_CFLAGS += -DZM_LARGEPAYLOAD_TEST=0
EXTRA_CFLAGS += -DZM_FW_LOOP_BACK=0
EXTRA_CFLAGS += -DZM_LINUX_TPC=1
#EXTRA_CFLAGS += -DZM_DONT_COPY_RX_BUFFER

EXTRA_CFLAGS += -DZM_HOSTAPD_SUPPORT
#EXTRA_CFLAGS += -DfTX_GAIN_OFDM=0
#EXTRA_CFLAGS += -DZM_CONFIG_BIG_ENDIAN -DBIG_ENDIAN
EXTRA_CFLAGS += -DZM_HALPLUS_LOCK
EXTRA_CFLAGS += -DZM_OTUS_LINUX_PHASE_2

arusb_lnx-objs := \
	usbdrv.o \
	zdusb.o \
	ioctl.o \
	wrap_buf.o \
	wrap_mem.o \
	wrap_ev.o \
	wrap_usb.o \
	wrap_pkt.o \
	wrap_dbg.o \
	wrap_mis.o \
	wrap_sec.o \
	wwrap.o \
	80211core/ccmd.o \
	80211core/chb.o \
	80211core/cinit.o \
	80211core/cmm.o \
	80211core/cmmap.o \
	80211core/cmmsta.o \
	80211core/cfunc.o \
	80211core/coid.o \
	80211core/ctkip.o \
	80211core/ctxrx.o \
	80211core/cic.o \
	80211core/cpsmgr.o \
	80211core/cscanmgr.o \
	80211core/ratectrl.o \
	80211core/ledmgr.o \
	80211core/amsdu.o \
	80211core/cwm.o \
	80211core/cagg.o \
	80211core/queue.o \
	80211core/freqctrl.o \
	80211core/cwep.o \
	hal/hprw.o \
	hal/hpmain.o \
	hal/hpusb.o \
	hal/hpreg.o \
	hal/hpfwuinit.o \
	hal/hpfwbu.o \
	hal/hpfw2.o \
	hal/hpDKfwu.o \
	hal/hpfwspiu.o \
	hal/hpani.o
