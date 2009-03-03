ath9k-y +=	hw.o \
		eeprom.o \
		mac.o \
		calib.o \
		ani.o \
		phy.o \
		regd.o \
		beacon.o \
		main.o \
		recv.o \
		xmit.o \
		virtual.o \
		rc.o

ath9k-$(CONFIG_PCI) += pci.o
ath9k-$(CONFIG_ATHEROS_AR71XX) += ahb.o
ath9k-$(CONFIG_ATH9K_DEBUG) += debug.o

obj-$(CONFIG_ATH9K) += ath9k.o
