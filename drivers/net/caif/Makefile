ifeq ($(CONFIG_CAIF_DEBUG),1)
CAIF_DBG_FLAGS := -DDEBUG
endif

KBUILD_EXTRA_SYMBOLS=net/caif/Module.symvers

ccflags-y := $(CAIF_FLAGS) $(CAIF_DBG_FLAGS)
clean-dirs:= .tmp_versions
clean-files:= Module.symvers modules.order *.cmd *~ \

# Serial interface
obj-$(CONFIG_CAIF_TTY) += caif_serial.o
