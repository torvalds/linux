obj-y				=  mce_$(BITS).o therm_throt.o

obj-$(CONFIG_X86_32)		+= k7.o p4.o p5.o p6.o winchip.o
obj-$(CONFIG_X86_MCE_INTEL)	+= mce_intel_64.o
obj-$(CONFIG_X86_MCE_AMD)	+= mce_amd_64.o
obj-$(CONFIG_X86_MCE_NONFATAL)	+= non-fatal.o
