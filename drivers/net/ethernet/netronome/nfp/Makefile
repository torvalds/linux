# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_NFP)	+= nfp.o

nfp-objs := \
	    nfpcore/nfp6000_pcie.o \
	    nfpcore/nfp_cppcore.o \
	    nfpcore/nfp_cpplib.o \
	    nfpcore/nfp_hwinfo.o \
	    nfpcore/nfp_mip.o \
	    nfpcore/nfp_mutex.o \
	    nfpcore/nfp_nffw.o \
	    nfpcore/nfp_nsp.o \
	    nfpcore/nfp_nsp_cmds.o \
	    nfpcore/nfp_nsp_eth.o \
	    nfpcore/nfp_resource.o \
	    nfpcore/nfp_rtsym.o \
	    nfpcore/nfp_target.o \
	    nfp_app.o \
	    nfp_app_nic.o \
	    nfp_devlink.o \
	    nfp_hwmon.o \
	    nfp_main.o \
	    nfp_net_common.o \
	    nfp_net_ethtool.o \
	    nfp_net_main.o \
	    nfp_net_repr.o \
	    nfp_net_sriov.o \
	    nfp_netvf_main.o \
	    nfp_port.o \
	    bpf/main.o \
	    bpf/offload.o \
	    nic/main.o

ifeq ($(CONFIG_NFP_APP_FLOWER),y)
nfp-objs += \
	    flower/action.o \
	    flower/cmsg.o \
	    flower/main.o \
	    flower/match.o \
	    flower/metadata.o \
	    flower/offload.o
endif

ifeq ($(CONFIG_BPF_SYSCALL),y)
nfp-objs += \
	    bpf/verifier.o \
	    bpf/jit.o
endif

nfp-$(CONFIG_NFP_DEBUG) += nfp_net_debugfs.o
