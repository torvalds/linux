ifdef CONFIG_RELOCATABLE
# We can't use PIC/PIE when handling early-boot errata parsing, as the kernel
# doesn't have a GOT setup at that point.  So instead just use medany: it's
# usually position-independent, so it should be good enough for the errata
# handling.
KBUILD_CFLAGS += -fno-pie -mcmodel=medany
endif

ifdef CONFIG_RISCV_ALTERNATIVE_EARLY
ifdef CONFIG_FORTIFY_SOURCE
KBUILD_CFLAGS += -D__NO_FORTIFY
endif
endif

obj-$(CONFIG_ERRATA_ANDES) += andes/
obj-$(CONFIG_ERRATA_MIPS) += mips/
obj-$(CONFIG_ERRATA_SIFIVE) += sifive/
obj-$(CONFIG_ERRATA_THEAD) += thead/
