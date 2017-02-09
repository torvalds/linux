obj-$(CONFIG_NFP)	+= nfp.o

nfp-objs := \
	    nfpcore/nfp6000_pcie.o \
	    nfpcore/nfp_cppcore.o \
	    nfpcore/nfp_cpplib.o \
	    nfpcore/nfp_hwinfo.o \
	    nfpcore/nfp_mip.o \
	    nfpcore/nfp_nffw.o \
	    nfpcore/nfp_nsp.o \
	    nfpcore/nfp_nsp_eth.o \
	    nfpcore/nfp_resource.o \
	    nfpcore/nfp_rtsym.o \
	    nfpcore/nfp_target.o \
	    nfp_main.o \
	    nfp_net_common.o \
	    nfp_net_ethtool.o \
	    nfp_net_offload.o \
	    nfp_net_main.o \
	    nfp_netvf_main.o

ifeq ($(CONFIG_BPF_SYSCALL),y)
nfp-objs += \
	    nfp_bpf_verifier.o \
	    nfp_bpf_jit.o
endif

nfp-$(CONFIG_NFP_DEBUG) += nfp_net_debugfs.o
