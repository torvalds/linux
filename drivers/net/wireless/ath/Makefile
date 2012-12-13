obj-$(CONFIG_ATH5K)		+= ath5k/
obj-$(CONFIG_ATH9K_HW)		+= ath9k/
obj-$(CONFIG_CARL9170)		+= carl9170/
obj-$(CONFIG_ATH6KL)		+= ath6kl/
obj-$(CONFIG_AR5523)		+= ar5523/

obj-$(CONFIG_ATH_COMMON)	+= ath.o

ath-objs :=	main.o \
		regd.o \
		hw.o \
		key.o

ath-$(CONFIG_ATH_DEBUG) += debug.o
ccflags-y += -D__CHECK_ENDIAN__
