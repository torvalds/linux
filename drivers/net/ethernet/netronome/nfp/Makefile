obj-$(CONFIG_NFP)	+= nfp.o

nfp-objs := \
	    nfp_main.o \
	    nfp_net_common.o \
	    nfp_net_ethtool.o \
	    nfp_net_offload.o \
	    nfp_netvf_main.o

ifeq ($(CONFIG_BPF_SYSCALL),y)
nfp-objs += \
	    nfp_bpf_verifier.o \
	    nfp_bpf_jit.o
endif

nfp-$(CONFIG_NFP_DEBUG) += nfp_net_debugfs.o
