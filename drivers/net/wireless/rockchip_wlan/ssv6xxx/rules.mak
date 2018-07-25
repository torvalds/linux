

$(KMODULE_NAME)-y += $(KERN_SRCS:.c=.o)
obj-$(CONFIG_SSV6200_CORE) += $(KMODULE_NAME).o


.PHONY: all clean install

all:
	@$(MAKE) -C /lib/modules/$(KVERSION)/build \
		SUBDIRS=$(KBUILD_DIR) CONFIG_DEBUG_SECTION_MISMATCH=y \
		modules

clean:
	@$(MAKE) -C /lib/modules/$(KVERSION)/build SUBDIRS=$(KBUILD_DIR) clean

install:
	@$(MAKE) INSTALL_MOD_DIR=$(DRVPATH) -C /lib/modules/$(KVERSION)/build \
	        M=$(KBUILD_DIR) modules_install
