ifeq ($(RELEASE_PACKAGE),1)
EXTRA_CFLAGS+=-DRELEASE_PACKAGE
endif
LBITS := $(shell getconf LONG_BIT)
ifeq ($(LBITS),64)
CCFLAGS += -m64
EXTRA_CFLAGS+=-DSYSTEM_IS_64
else
CCFLAGS += -m32
endif

ifeq ($(TC956X_PCIE_GEN3_SETTING),1)
EXTRA_CFLAGS+=-DTC956X_PCIE_GEN3_SETTING
endif

ifeq ($(TC956X_LOAD_FW_HEADER),1)
EXTRA_CFLAGS+=-DTC956X_LOAD_FW_HEADER
endif

DMA_OFFLOAD = 1

obj-m := tc956x_pcie_eth.o
tc956x_pcie_eth-y := tc956xmac_main.o tc956xmac_ethtool.o tc956xmac_mdio.o \
	      mmc_core.o tc956xmac_hwtstamp.o tc956xmac_ptp.o tc956x_xpcs.o tc956x_pma.o \
	      hwif.o  tc956xmac_tc.o dwxgmac2_core.o \
	      dwxgmac2_descs.o dwxgmac2_dma.o tc956x_pci.o \
	      tc956x_pcie_logstat.o

ifeq ($(TC956XMAC_SELFTESTS), 1)
	tc956x_pcie_eth-y += tc956xmac_selftests.o
endif	 

ifeq ($(DMA_OFFLOAD), 1)
	tc956x_pcie_eth-y += tc956x_ipa_intf.o
endif	   


all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
