# SPDX-License-Identifier: MIT
#
# Copyright 2025 Advanced Micro Devices, Inc.
# Makefile for bounding box component.
# Floating point required due to nature of bounding box values

soc_and_ip_translator_ccflags := $(CC_FLAGS_FPU)
soc_and_ip_translator_rcflags := $(CC_FLAGS_NO_FPU)

CFLAGS_$(AMDDALPATH)/dc/soc_and_ip_translator/dcn401/dcn401_soc_and_ip_translator.o := $(soc_and_ip_translator_ccflags)

CFLAGS_REMOVE_$(AMDDALPATH)/dc/soc_and_ip_translator/dcn401/dcn401_soc_and_ip_translator.o := $(soc_and_ip_translator_rcflags)

soc_and_ip_translator := soc_and_ip_translator.o
soc_and_ip_translator += dcn401/dcn401_soc_and_ip_translator.o

AMD_DAL_soc_and_ip_translator := $(addprefix $(AMDDALPATH)/dc/soc_and_ip_translator/, $(soc_and_ip_translator))

AMD_DISPLAY_FILES += $(AMD_DAL_soc_and_ip_translator)
