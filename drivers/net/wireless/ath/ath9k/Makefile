ath9k-y +=	beacon.o \
		main.o \
		recv.o \
		xmit.o \
		virtual.o \
		rc.o

ath9k-$(CONFIG_PCI) += pci.o
ath9k-$(CONFIG_ATHEROS_AR71XX) += ahb.o
ath9k-$(CONFIG_ATH9K_DEBUG) += debug.o

obj-$(CONFIG_ATH9K) += ath9k.o

ath9k_hw-y:=	hw.o \
		eeprom.o \
		eeprom_def.o \
		eeprom_4k.o \
		eeprom_9287.o \
		calib.o \
		ani.o \
		phy.o \
		btcoex.o \
		mac.o \

obj-$(CONFIG_ATH9K_HW) += ath9k_hw.o
