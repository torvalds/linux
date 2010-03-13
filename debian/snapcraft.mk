ifeq ($(ARCH),)
  arch := $(shell uname -m | sed -e s/i.86/i386/ -e s/x86_64/amd64/ \
            -e s/arm.*/armhf/ -e s/s390/s390x/ -e s/ppc.*/powerpc/ \
            -e s/aarch64.*/arm64/ )
else ifeq ($(ARCH),arm)
  arch := armhf
else
  arch := $(ARCH)
endif
config:
	cat debian.$(branch)/config/config.common.ubuntu debian.$(branch)/config/$(arch)/config.common.$(arch) debian.$(branch)/config/$(arch)/config.flavour.$(flavour) >.config
