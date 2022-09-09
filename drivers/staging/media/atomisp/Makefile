# SPDX-License-Identifier: GPL-2.0
#
# Makefile for camera drivers.
#
obj-$(CONFIG_INTEL_ATOMISP) += i2c/
obj-$(CONFIG_VIDEO_ATOMISP) += atomisp.o
obj-$(CONFIG_VIDEO_ATOMISP) += pci/atomisp_gmin_platform.o

# While on staging, keep debug enabled
DEFINES += -DDEBUG

atomisp = $(srctree)/drivers/staging/media/atomisp/

# SPDX-License-Identifier: GPL-2.0
atomisp-objs += \
	pci/atomisp_cmd.o \
	pci/atomisp_compat_css20.o \
	pci/atomisp_csi2.o \
	pci/atomisp_drvfs.o \
	pci/atomisp_file.o \
	pci/atomisp_fops.o \
	pci/atomisp_ioctl.o \
	pci/atomisp_subdev.o \
	pci/atomisp_tpg.o \
	pci/atomisp_v4l2.o \
	pci/sh_css_firmware.o \
	pci/sh_css_host_data.o \
	pci/sh_css_hrt.o \
	pci/sh_css_metrics.o \
	pci/sh_css_mipi.o \
	pci/sh_css_mmu.o \
	pci/sh_css.o \
	pci/sh_css_param_dvs.o \
	pci/sh_css_param_shading.o \
	pci/sh_css_params.o \
	pci/sh_css_properties.o \
	pci/sh_css_sp.o \
	pci/sh_css_stream_format.o \
	pci/sh_css_version.o \
	pci/base/circbuf/src/circbuf.o \
	pci/base/refcount/src/refcount.o \
	pci/camera/pipe/src/pipe_binarydesc.o \
	pci/camera/pipe/src/pipe_stagedesc.o \
	pci/camera/pipe/src/pipe_util.o \
	pci/camera/util/src/util.o \
	pci/hmm/hmm_bo.o \
	pci/hmm/hmm.o \
	pci/ia_css_device_access.o \
	pci/ia_css_isp_configs.o \
	pci/ia_css_isp_states.o \
	pci/ia_css_isp_params.o \
	pci/isp/kernels/aa/aa_2/ia_css_aa2.host.o \
	pci/isp/kernels/anr/anr_1.0/ia_css_anr.host.o \
	pci/isp/kernels/anr/anr_2/ia_css_anr2.host.o \
	pci/isp/kernels/anr/anr_2/ia_css_anr2_table.host.o \
	pci/isp/kernels/bh/bh_2/ia_css_bh.host.o \
	pci/isp/kernels/bnlm/ia_css_bnlm.host.o \
	pci/isp/kernels/bnr/bnr_1.0/ia_css_bnr.host.o \
	pci/isp/kernels/bnr/bnr2_2/ia_css_bnr2_2.host.o \
	pci/isp/kernels/cnr/cnr_1.0/ia_css_cnr.host.o \
	pci/isp/kernels/cnr/cnr_2/ia_css_cnr2.host.o \
	pci/isp/kernels/conversion/conversion_1.0/ia_css_conversion.host.o \
	pci/isp/kernels/copy_output/copy_output_1.0/ia_css_copy_output.host.o \
	pci/isp/kernels/crop/crop_1.0/ia_css_crop.host.o \
	pci/isp/kernels/csc/csc_1.0/ia_css_csc.host.o \
	pci/isp/kernels/ctc/ctc_1.0/ia_css_ctc.host.o \
	pci/isp/kernels/ctc/ctc_1.0/ia_css_ctc_table.host.o \
	pci/isp/kernels/ctc/ctc1_5/ia_css_ctc1_5.host.o \
	pci/isp/kernels/ctc/ctc2/ia_css_ctc2.host.o \
	pci/isp/kernels/de/de_1.0/ia_css_de.host.o \
	pci/isp/kernels/de/de_2/ia_css_de2.host.o \
	pci/isp/kernels/dpc2/ia_css_dpc2.host.o \
	pci/isp/kernels/dp/dp_1.0/ia_css_dp.host.o \
	pci/isp/kernels/dvs/dvs_1.0/ia_css_dvs.host.o \
	pci/isp/kernels/eed1_8/ia_css_eed1_8.host.o \
	pci/isp/kernels/fc/fc_1.0/ia_css_formats.host.o \
	pci/isp/kernels/fpn/fpn_1.0/ia_css_fpn.host.o \
	pci/isp/kernels/gc/gc_1.0/ia_css_gc.host.o \
	pci/isp/kernels/gc/gc_1.0/ia_css_gc_table.host.o \
	pci/isp/kernels/gc/gc_2/ia_css_gc2.host.o \
	pci/isp/kernels/gc/gc_2/ia_css_gc2_table.host.o \
	pci/isp/kernels/hdr/ia_css_hdr.host.o \
	pci/isp/kernels/ipu2_io_ls/bayer_io_ls/ia_css_bayer_io.host.o \
	pci/isp/kernels/ipu2_io_ls/bayer_io_ls/ia_css_bayer_io.host.o \
	pci/isp/kernels/ipu2_io_ls/yuv444_io_ls/ia_css_yuv444_io.host.o \
	pci/isp/kernels/iterator/iterator_1.0/ia_css_iterator.host.o \
	pci/isp/kernels/macc/macc_1.0/ia_css_macc.host.o \
	pci/isp/kernels/macc/macc_1.0/ia_css_macc_table.host.o \
	pci/isp/kernels/macc/macc1_5/ia_css_macc1_5.host.o \
	pci/isp/kernels/macc/macc1_5/ia_css_macc1_5_table.host.o \
	pci/isp/kernels/norm/norm_1.0/ia_css_norm.host.o \
	pci/isp/kernels/ob/ob_1.0/ia_css_ob.host.o \
	pci/isp/kernels/ob/ob2/ia_css_ob2.host.o \
	pci/isp/kernels/output/output_1.0/ia_css_output.host.o \
	pci/isp/kernels/qplane/qplane_2/ia_css_qplane.host.o \
	pci/isp/kernels/raw_aa_binning/raw_aa_binning_1.0/ia_css_raa.host.o \
	pci/isp/kernels/raw/raw_1.0/ia_css_raw.host.o \
	pci/isp/kernels/ref/ref_1.0/ia_css_ref.host.o \
	pci/isp/kernels/s3a/s3a_1.0/ia_css_s3a.host.o \
	pci/isp/kernels/sc/sc_1.0/ia_css_sc.host.o \
	pci/isp/kernels/sdis/sdis_1.0/ia_css_sdis.host.o \
	pci/isp/kernels/sdis/sdis_2/ia_css_sdis2.host.o \
	pci/isp/kernels/tdf/tdf_1.0/ia_css_tdf.host.o \
	pci/isp/kernels/tnr/tnr_1.0/ia_css_tnr.host.o \
	pci/isp/kernels/vf/vf_1.0/ia_css_vf.host.o \
	pci/isp/kernels/wb/wb_1.0/ia_css_wb.host.o \
	pci/isp/kernels/xnr/xnr_1.0/ia_css_xnr.host.o \
	pci/isp/kernels/xnr/xnr_1.0/ia_css_xnr_table.host.o \
	pci/isp/kernels/xnr/xnr_3.0/ia_css_xnr3.host.o \
	pci/isp/kernels/ynr/ynr_1.0/ia_css_ynr.host.o \
	pci/isp/kernels/ynr/ynr_2/ia_css_ynr2.host.o \
	pci/mmu/isp_mmu.o \
	pci/mmu/sh_mmu_mrfld.o \
	pci/runtime/binary/src/binary.o \
	pci/runtime/bufq/src/bufq.o \
	pci/runtime/debug/src/ia_css_debug.o \
	pci/runtime/eventq/src/eventq.o \
	pci/runtime/event/src/event.o \
	pci/runtime/frame/src/frame.o \
	pci/runtime/ifmtr/src/ifmtr.o \
	pci/runtime/inputfifo/src/inputfifo.o \
	pci/runtime/isp_param/src/isp_param.o \
	pci/runtime/isys/src/csi_rx_rmgr.o \
	pci/runtime/isys/src/isys_dma_rmgr.o \
	pci/runtime/isys/src/isys_init.o \
	pci/runtime/isys/src/isys_stream2mmio_rmgr.o \
	pci/runtime/isys/src/rx.o \
	pci/runtime/isys/src/virtual_isys.o \
	pci/runtime/pipeline/src/pipeline.o \
	pci/runtime/queue/src/queue_access.o \
	pci/runtime/queue/src/queue.o \
	pci/runtime/rmgr/src/rmgr.o \
	pci/runtime/rmgr/src/rmgr_vbuf.o \
	pci/runtime/spctrl/src/spctrl.o \
	pci/runtime/timer/src/timer.o \
	pci/hive_isp_css_common/host/debug.o \
	pci/hive_isp_css_common/host/dma.o \
	pci/hive_isp_css_common/host/event_fifo.o \
	pci/hive_isp_css_common/host/fifo_monitor.o \
	pci/hive_isp_css_common/host/gdc.o \
	pci/hive_isp_css_common/host/gp_device.o \
	pci/hive_isp_css_common/host/gp_timer.o \
	pci/hive_isp_css_common/host/hmem.o \
	pci/hive_isp_css_common/host/input_formatter.o \
	pci/hive_isp_css_common/host/input_system.o \
	pci/hive_isp_css_common/host/irq.o \
	pci/hive_isp_css_common/host/isp.o \
	pci/hive_isp_css_common/host/mmu.o \
	pci/hive_isp_css_common/host/sp.o \
	pci/hive_isp_css_common/host/timed_ctrl.o \
	pci/hive_isp_css_common/host/vmem.o \
	pci/hive_isp_css_shared/host/tag.o \
	pci/system_local.o

# These will be needed when clean merge CHT support nicely into the driver
# Keep them here handy for when we get to that point
#

obj-cht = \
	pci/css_2401_system/host/csi_rx.o \
	pci/css_2401_system/host/ibuf_ctrl.o \
	pci/css_2401_system/host/isys_dma.o \
	pci/css_2401_system/host/isys_irq.o \
	pci/css_2401_system/host/isys_stream2mmio.o

INCLUDES += \
	-I$(atomisp)/ \
	-I$(atomisp)/include/ \
	-I$(atomisp)/include/hmm/ \
	-I$(atomisp)/include/mmu/ \
	-I$(atomisp)/pci/ \
	-I$(atomisp)/pci/base/circbuf/interface/ \
	-I$(atomisp)/pci/base/refcount/interface/ \
	-I$(atomisp)/pci/camera/pipe/interface/ \
	-I$(atomisp)/pci/camera/util/interface/ \
	-I$(atomisp)/pci/hive_isp_css_common/ \
	-I$(atomisp)/pci/hive_isp_css_common/host/ \
	-I$(atomisp)/pci/hive_isp_css_include/ \
	-I$(atomisp)/pci/hive_isp_css_include/device_access/ \
	-I$(atomisp)/pci/hive_isp_css_include/host/ \
	-I$(atomisp)/pci/hive_isp_css_shared/ \
	-I$(atomisp)/pci/hive_isp_css_shared/host/ \
	-I$(atomisp)/pci/isp/kernels/ \
	-I$(atomisp)/pci/isp/kernels/aa/aa_2/ \
	-I$(atomisp)/pci/isp/kernels/anr/anr_1.0/ \
	-I$(atomisp)/pci/isp/kernels/anr/anr_2/ \
	-I$(atomisp)/pci/isp/kernels/bh/bh_2/ \
	-I$(atomisp)/pci/isp/kernels/bnlm/ \
	-I$(atomisp)/pci/isp/kernels/bnr/ \
	-I$(atomisp)/pci/isp/kernels/bnr/bnr_1.0/ \
	-I$(atomisp)/pci/isp/kernels/bnr/bnr2_2/ \
	-I$(atomisp)/pci/isp/kernels/cnr/ \
	-I$(atomisp)/pci/isp/kernels/cnr/cnr_1.0/ \
	-I$(atomisp)/pci/isp/kernels/cnr/cnr_2/ \
	-I$(atomisp)/pci/isp/kernels/conversion/ \
	-I$(atomisp)/pci/isp/kernels/conversion/conversion_1.0/ \
	-I$(atomisp)/pci/isp/kernels/copy_output/ \
	-I$(atomisp)/pci/isp/kernels/copy_output/copy_output_1.0/ \
	-I$(atomisp)/pci/isp/kernels/crop/ \
	-I$(atomisp)/pci/isp/kernels/crop/crop_1.0/ \
	-I$(atomisp)/pci/isp/kernels/csc/ \
	-I$(atomisp)/pci/isp/kernels/csc/csc_1.0/ \
	-I$(atomisp)/pci/isp/kernels/ctc/ \
	-I$(atomisp)/pci/isp/kernels/ctc/ctc_1.0/ \
	-I$(atomisp)/pci/isp/kernels/ctc/ctc1_5/ \
	-I$(atomisp)/pci/isp/kernels/ctc/ctc2/ \
	-I$(atomisp)/pci/isp/kernels/de/ \
	-I$(atomisp)/pci/isp/kernels/de/de_1.0/ \
	-I$(atomisp)/pci/isp/kernels/de/de_2/ \
	-I$(atomisp)/pci/isp/kernels/dp/ \
	-I$(atomisp)/pci/isp/kernels/dpc2/ \
	-I$(atomisp)/pci/isp/kernels/dp/dp_1.0/ \
	-I$(atomisp)/pci/isp/kernels/dvs/ \
	-I$(atomisp)/pci/isp/kernels/dvs/dvs_1.0/ \
	-I$(atomisp)/pci/isp/kernels/eed1_8/ \
	-I$(atomisp)/pci/isp/kernels/fc/ \
	-I$(atomisp)/pci/isp/kernels/fc/fc_1.0/ \
	-I$(atomisp)/pci/isp/kernels/fixedbds/ \
	-I$(atomisp)/pci/isp/kernels/fixedbds/fixedbds_1.0/ \
	-I$(atomisp)/pci/isp/kernels/fpn/ \
	-I$(atomisp)/pci/isp/kernels/fpn/fpn_1.0/ \
	-I$(atomisp)/pci/isp/kernels/gc/ \
	-I$(atomisp)/pci/isp/kernels/gc/gc_1.0/ \
	-I$(atomisp)/pci/isp/kernels/gc/gc_2/ \
	-I$(atomisp)/pci/isp/kernels/hdr/ \
	-I$(atomisp)/pci/isp/kernels/ipu2_io_ls/ \
	-I$(atomisp)/pci/isp/kernels/ipu2_io_ls/ \
	-I$(atomisp)/pci/isp/kernels/ipu2_io_ls/bayer_io_ls/ \
	-I$(atomisp)/pci/isp/kernels/ipu2_io_ls/bayer_io_ls/ \
	-I$(atomisp)/pci/isp/kernels/ipu2_io_ls/common/ \
	-I$(atomisp)/pci/isp/kernels/ipu2_io_ls/common/ \
	-I$(atomisp)/pci/isp/kernels/ipu2_io_ls/yuv444_io_ls/ \
	-I$(atomisp)/pci/isp/kernels/ipu2_io_ls/yuv444_io_ls/ \
	-I$(atomisp)/pci/isp/kernels/iterator/ \
	-I$(atomisp)/pci/isp/kernels/iterator/iterator_1.0/ \
	-I$(atomisp)/pci/isp/kernels/macc/ \
	-I$(atomisp)/pci/isp/kernels/macc/macc_1.0/ \
	-I$(atomisp)/pci/isp/kernels/macc/macc1_5/ \
	-I$(atomisp)/pci/isp/kernels/norm/ \
	-I$(atomisp)/pci/isp/kernels/norm/norm_1.0/ \
	-I$(atomisp)/pci/isp/kernels/ob/ \
	-I$(atomisp)/pci/isp/kernels/ob/ob_1.0/ \
	-I$(atomisp)/pci/isp/kernels/ob/ob2/ \
	-I$(atomisp)/pci/isp/kernels/output/ \
	-I$(atomisp)/pci/isp/kernels/output/output_1.0/ \
	-I$(atomisp)/pci/isp/kernels/qplane/ \
	-I$(atomisp)/pci/isp/kernels/qplane/qplane_2/ \
	-I$(atomisp)/pci/isp/kernels/raw/ \
	-I$(atomisp)/pci/isp/kernels/raw_aa_binning/ \
	-I$(atomisp)/pci/isp/kernels/raw_aa_binning/raw_aa_binning_1.0/ \
	-I$(atomisp)/pci/isp/kernels/raw/raw_1.0/ \
	-I$(atomisp)/pci/isp/kernels/ref/ \
	-I$(atomisp)/pci/isp/kernels/ref/ref_1.0/ \
	-I$(atomisp)/pci/isp/kernels/s3a/ \
	-I$(atomisp)/pci/isp/kernels/s3a/s3a_1.0/ \
	-I$(atomisp)/pci/isp/kernels/sc/ \
	-I$(atomisp)/pci/isp/kernels/sc/sc_1.0/ \
	-I$(atomisp)/pci/isp/kernels/sdis/ \
	-I$(atomisp)/pci/isp/kernels/sdis/common/ \
	-I$(atomisp)/pci/isp/kernels/sdis/sdis_1.0/ \
	-I$(atomisp)/pci/isp/kernels/sdis/sdis_2/ \
	-I$(atomisp)/pci/isp/kernels/tdf/ \
	-I$(atomisp)/pci/isp/kernels/tdf/tdf_1.0/ \
	-I$(atomisp)/pci/isp/kernels/tnr/ \
	-I$(atomisp)/pci/isp/kernels/tnr/tnr_1.0/ \
	-I$(atomisp)/pci/isp/kernels/tnr/tnr3/ \
	-I$(atomisp)/pci/isp/kernels/uds/ \
	-I$(atomisp)/pci/isp/kernels/uds/uds_1.0/ \
	-I$(atomisp)/pci/isp/kernels/vf/ \
	-I$(atomisp)/pci/isp/kernels/vf/vf_1.0/ \
	-I$(atomisp)/pci/isp/kernels/wb/ \
	-I$(atomisp)/pci/isp/kernels/wb/wb_1.0/ \
	-I$(atomisp)/pci/isp/kernels/xnr/ \
	-I$(atomisp)/pci/isp/kernels/xnr/xnr_1.0/ \
	-I$(atomisp)/pci/isp/kernels/xnr/xnr_3.0/ \
	-I$(atomisp)/pci/isp/kernels/ynr/ \
	-I$(atomisp)/pci/isp/kernels/ynr/ynr_1.0/ \
	-I$(atomisp)/pci/isp/kernels/ynr/ynr_2/ \
	-I$(atomisp)/pci/isp/modes/interface/ \
	-I$(atomisp)/pci/runtime/binary/interface/ \
	-I$(atomisp)/pci/runtime/bufq/interface/ \
	-I$(atomisp)/pci/runtime/debug/interface/ \
	-I$(atomisp)/pci/runtime/event/interface/ \
	-I$(atomisp)/pci/runtime/eventq/interface/ \
	-I$(atomisp)/pci/runtime/frame/interface/ \
	-I$(atomisp)/pci/runtime/ifmtr/interface/ \
	-I$(atomisp)/pci/runtime/inputfifo/interface/ \
	-I$(atomisp)/pci/runtime/isp_param/interface/ \
	-I$(atomisp)/pci/runtime/isys/interface/ \
	-I$(atomisp)/pci/runtime/isys/src/ \
	-I$(atomisp)/pci/runtime/pipeline/interface/ \
	-I$(atomisp)/pci/runtime/queue/interface/ \
	-I$(atomisp)/pci/runtime/queue/src/ \
	-I$(atomisp)/pci/runtime/rmgr/interface/ \
	-I$(atomisp)/pci/runtime/spctrl/interface/ \
	-I$(atomisp)/pci/runtime/tagger/interface/ \
	-I$(atomisp)/pci/css_2401_system/ \
	-I$(atomisp)/pci/css_2401_system/host/ \
	-I$(atomisp)/pci/css_2401_system/hrt/

DEFINES := -DHRT_HW -DHRT_ISP_CSS_CUSTOM_HOST -DHRT_USE_VIR_ADDRS -D__HOST__
#DEFINES += -DUSE_DYNAMIC_BIN
#DEFINES += -DISP_POWER_GATING
#DEFINES += -DUSE_INTERRUPTS
#DEFINES += -DUSE_SSSE3
#DEFINES += -DPUNIT_CAMERA_BUSY
#DEFINES += -DUSE_KMEM_CACHE

ifeq ($(CONFIG_VIDEO_ATOMISP_ISP2401),y)
atomisp-objs += \
	$(obj-cht) \
	pci/runtime/isys/src/ibuf_ctrl_rmgr.o
DEFINES += -DISP2401
endif

ccflags-y += $(INCLUDES) $(DEFINES) -fno-common
