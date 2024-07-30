# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2023 Intel Corporation

# Makefile for Intel(R) Infrastructure Data Path Function Linux Driver

obj-$(CONFIG_IDPF) += idpf.o

idpf-y := \
	idpf_controlq.o		\
	idpf_controlq_setup.o	\
	idpf_dev.o		\
	idpf_ethtool.o		\
	idpf_lib.o		\
	idpf_main.o		\
	idpf_txrx.o		\
	idpf_virtchnl.o 	\
	idpf_vf_dev.o

idpf-$(CONFIG_IDPF_SINGLEQ)	+= idpf_singleq_txrx.o
