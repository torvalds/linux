#
# Makefile for arch/m68knommu/platform/68VZ328.
#

obj-y		:= config.o
extra-$(DRAGEN2):= screen.h

$(obj)/screen.h: $(src)/screen.xbm $(src)/xbm2lcd.pl
	perl $(src)/xbm2lcd.pl < $(src)/screen.xbm > $(obj)/screen.h

clean-files := $(obj)/screen.h
