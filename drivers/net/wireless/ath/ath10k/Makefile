obj-$(CONFIG_ATH10K) += ath10k_core.o
ath10k_core-y += mac.o \
		 debug.o \
		 core.o \
		 htc.o \
		 htt.o \
		 htt_rx.o \
		 htt_tx.o \
		 txrx.o \
		 wmi.o \
		 wmi-tlv.o \
		 bmi.o

ath10k_core-$(CONFIG_ATH10K_DEBUGFS) += spectral.o
ath10k_core-$(CONFIG_NL80211_TESTMODE) += testmode.o
ath10k_core-$(CONFIG_ATH10K_TRACING) += trace.o

obj-$(CONFIG_ATH10K_PCI) += ath10k_pci.o
ath10k_pci-y += pci.o \
		ce.o

# for tracing framework to find trace.h
CFLAGS_trace.o := -I$(src)
