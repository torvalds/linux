#
# Copyright 2022 Advanced Micro Devices, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# Makefile for the link sub-component of DAL.
# It abstracts the control and status of back end pipe such as DIO, HPO, DPIA,
# PHY, HPD, DDC and etc).

LINK = link_detection.o link_dpms.o link_factory.o link_resource.o \
link_validation.o

AMD_DAL_LINK = $(addprefix $(AMDDALPATH)/dc/link/, \
$(LINK))

AMD_DISPLAY_FILES += $(AMD_DAL_LINK)
###############################################################################
# accessories
###############################################################################
LINK_ACCESSORIES = link_dp_trace.o link_dp_cts.o link_fpga.o

AMD_DAL_LINK_ACCESSORIES = $(addprefix $(AMDDALPATH)/dc/link/accessories/, \
$(LINK_ACCESSORIES))

AMD_DISPLAY_FILES += $(AMD_DAL_LINK_ACCESSORIES)
###############################################################################
# hwss
###############################################################################
LINK_HWSS = link_hwss_dio.o link_hwss_dpia.o link_hwss_hpo_dp.o

AMD_DAL_LINK_HWSS = $(addprefix $(AMDDALPATH)/dc/link/hwss/, \
$(LINK_HWSS))

AMD_DISPLAY_FILES += $(AMD_DAL_LINK_HWSS)
###############################################################################
# protocols
###############################################################################
LINK_PROTOCOLS = link_hpd.o link_ddc.o link_dpcd.o link_dp_dpia.o \
link_dp_training.o link_dp_training_8b_10b.o link_dp_training_128b_132b.o \
link_dp_training_dpia.o link_dp_training_auxless.o \
link_dp_training_fixed_vs_pe_retimer.o link_dp_phy.o link_dp_capability.o \
link_edp_panel_control.o link_dp_irq_handler.o link_dp_dpia_bw.o

AMD_DAL_LINK_PROTOCOLS = $(addprefix $(AMDDALPATH)/dc/link/protocols/, \
$(LINK_PROTOCOLS))

AMD_DISPLAY_FILES += $(AMD_DAL_LINK_PROTOCOLS)