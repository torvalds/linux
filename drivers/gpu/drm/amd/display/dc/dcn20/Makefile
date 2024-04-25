# SPDX-License-Identifier: MIT
# Copyright © 2019-2024 Advanced Micro Devices, Inc. All rights reserved.

DCN20 = dcn20_mpc.o dcn20_opp.o dcn20_mmhubbub.o \
		dcn20_stream_encoder.o dcn20_link_encoder.o \
		dcn20_vmid.o dcn20_dwb.o dcn20_dwb_scl.o

AMD_DAL_DCN20 = $(addprefix $(AMDDALPATH)/dc/dcn20/,$(DCN20))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN20)
