/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_VERSION_H
#define _RKISP_VERSION_H
#include <linux/version.h>
#include <linux/rkisp21-config.h>

/*
 * RKISP DRIVER VERSION NOTE
 *
 * v0.1.0:
 * 1. First version;
 *
 * v0.1.1:
 * 1. support lvds interface
 *
 * v0.1.2:
 * support multi virtual device;
 *
 * v0.1.3:
 * 1. support link with interface of cif
 * 2. fix picture of hdr is abnormal in multi-visual isp when run second time
 * 3. adjust hurry priority to middle
 * 4. mi lum burst to 16 for dmatx
 * 5. add cru reset for rv1126
 *
 * v0.1.4:
 * 1. add more clk rate for rv1126
 * 2. support hal to control hdrtmo on/off
 * 3. switch hdr frame mode for read back
 *
 * v0.1.5:
 * 1. add proc fs
 * 2. add iq part information to procfs
 * 3. fix stream failure in thunderboot mode
 *
 * v0.1.6:
 * 1. raw length 256 align
 * 2. soft reset for Dehaze
 *
 * v0.1.7:
 * 1. fix rawaf is disabled in config function
 * 2. clear csi rdbk fifo when first open
 * 3. vb2 support cache hints
 *
 * v0.1.8:
 * 1. add monitor to restart if abnormal
 * 2. isp/ispp procfs add work info
 * 3. fix scr clock is not disabled after app run
 * 4. request buf to alloc dummy buf
 * 5. set tmo bit in gain by tmo enable
 * 6. only rx mode can use when link with cif
 *
 * v0.1.9:
 * 1. isp and ispp sync to power off
 * 2. fix lsc error when ldch is on
 * 3. fix error status of stream off
 * 4. skip frame when change hdr/normal mode
 * 5. use ldch share buffer to reduce buffer size
 *
 * v1.2.0:
 * 1. resolution write directly to reg for first dev
 * 2. normal read back to enable hdr merge
 * 3. enable LDCH in 2th frame
 *
 * v1.2.1:
 * 1. fix normal merge enable config
 * 2. fix size no update for multi sensor switch
 * 3. dmatx add yuyv format
 *
 * v1.2.2:
 * 1. isp/ispp add lock for multi dev runtime
 * 2. support output isp/ispp reg on each frame
 * 3. fix error detected by depmod
 *
 * v1.3.0:
 * 1. capture to different version
 * 2. add isp21
 * 3. add rk3568 config
 * 4. support iq part of isp21
 * 5. remove hdrtmo to fix crash when connect to yuv sensor
 * 6. fix enable function of ynr/cnr/bay3d/dhaz/adrc is not correct
 * 7. fix can not get correct awb rawdata
 * 8. add get awb data from ddr function
 * 9. fix frame id error for isp21
 * 10. config lsc by sram in rdbk mode
 * 11. add force update to enable dehaze
 * 12. fix bug of scheduling while atomic
 * 13. fix setting drc register is not correct
 * 14. extend line to fix merge bypass bug for isp20
 * 15. vb2 dma sg for iommu enable
 * 16. config dmatx to valid buf addr
 *
 * v1.4.1:
 * 1. support motion detection mode
 * 2. get stats only when meas done is on
 * 3. fix lsc lut error in start/stop test
 *
 * v1.5.1:
 * 1. support to set format if no streaming
 * 2. add vb2_rdma_sg_memops to support contiguous page
 * 3. fix gain buf update
 * 4. 64 align y size for fbcgain format
 * 5. add trigger mode ioctl
 * 6. fix config of clk_dbg
 * 7. fix path select of cif input
 * 8. fix mpfbc buf update if readback off
 * 9. fix array overflow
 * 10. use force big mode when auto big mode is incorrect
 * 11. fix extend line with isp input crop case
 * 12. set lgmean related regs for tmo in hdr isr
 *
 * v1.6.0:
 * 1. reorder of subdev stream
 * 2. fix media link err for name don't match
 * 3. switch hdr_done interrupt according to hdrtmo cnt mode
 * 4. support output isp/ispp reg in nv12 format
 * 5. isp and ispp add shutdown
 * 6. image input from user
 * 7. import dma API for memory synchronisation for thunderboot
 * 8. don't start ldch asynchronously in multi-isp mode
 * 9. fix err of mp dump raw for isp20
 * 10. make sure 3dlut no continuous read twice
 * 11. adjust rdbk times with mulit dev for isp2.0
 *
 * v1.6.1:
 * 1.fix multi dev refcnt
 * 2.update procfs info
 * 3.add check for params subscribe event
 * 4.resolution limit for isp21
 * 5.dma buf alloc limit to DMA32
 * 6.add monitor to restart if abnormal
 * 7.adjust probe order
 * 8.max height 3072 for isp21
 * 9.dmatx support embedded and shield pixels data
 * 10.separate rdbk from csi subdev
 * 11.add bt601/bt709/bt2020 colorspace
 * 12.apply en params if no match for isp20
 * 13.apply en params if no match for isp21
 * 14.isp21 get 3a stats from ddr
 * 15.reserved memory using rdma_sg ops
 *
 * v1.6.2:
 * 1.hdr direct for isp21
 * 2.fix same frame id
 * 3.fix isp and ispp share dmabuf release fail
 * 4.clear rdbk fifo at dmarx stop
 * 5.add lock for isp stream
 * 6.disable params when it stream off
 * 7.dmarx support yuv format
 * 8.frame buffer done early
 * 9.fix set pdaf in dpcc error
 * 10.add v-blank to procfs
 *
 * v1.7.0:
 * 1.off unused interrupt of csi
 * 2.fix sp no output when hdr dynamic switch
 * 3.check the output status of statistics v2x
 * 4.selfpath bytesperline 16 align
 * 5.compiled with differe hardware version
 * 6.add frame loss info to procfs
 * 7.remove associated of cproc and ie
 * 8.fix input crop config for isp21 multi device
 * 9.enable soft reset for other isp version
 * 10.rawrd support uncompact mode
 * 11.fix default params config for mode switch
 * 12.before frame start to update bridge mi
 * 13.disable tmo interrupt
 * 14.fix bottom image for debayer with extend line
 * 15.unregister dmarx at driver remove
 *
 * v1.8.0:
 * 1.sync alloc buf with dma sg case
 * 2.sync multi vir dev stream on/off
 * 3.replace iommu detach/attach
 * 4.adjust params common api
 * 5.add isp3.0
 * 6.params and stats for isp3.0
 * 7.vicap direct to isp3.0
 * 8.bridge v30 connect to ispp
 * 9.add rk3588 config
 * 10.add cmsk config for isp30
 * 11.dual isp unite process image
 * 12.params and stats for dual isp unite
 * 13.sync dhaz params for dual isp unite
 * 14.fbc support crop
 * 15.add dual isp unite config
 * 16.useless version return -EINVAL
 * 17.fix first frame abnormal
 * 18.fix isp30 config for cnr with gain off
 * 19.fix NULL Pointer for stats v3x
 * 20.add constraint to gaus_en/viir_en/v1_fir_sel of rawaf
 * 21.fix dhaz config with dual unite isp
 * 22.fix isp30 fbc config
 * 23.isp3 max clk to 702M
 * 24.fix fbc iommu err with multi device case
 * 25.fix first params config two times for readback mode
 * 26.fix ynr/cnr/baynr reg config
 * 27.fix rawhist weight config error for multi device
 * 28.bigmode by max width and size for isp30
 * 29.add isp30 debug to procfs
 * 30.fix scale resolution limit
 * 31.fix bigmode for multi device
 * 32.fix fbc stop iommu page fault for isp30
 * 33.fix rawawb with rawlsc no stats
 * 34.fix bay3d mi no update
 * 35.dynamic memory alloc for params and stats function
 * 36.limit ldch and gain for isp30
 * 37.fix multi stream mpfbc reg config error
 * 38.support stream crop for unite isp
 * 39.fix hdrmge config error for isp30 read back mode
 * 40.lsc table from sram for isp30
 * 41.3a params config first
 * 42.config aebig by af when aemode is on
 * 43.add missing highlight in af stats
 * 44.add the iqtool module
 * 45.add csm params config for isp3
 * 46.fix CSI2RX_DATA_IDS_1 config err
 *
 * v1.9.0:
 * 1.fix config for isp_params_v3x
 * 2.clean rdbk kfifo for isp32
 * 3.fix awb raw data config for multi device
 * 4.increase v4l2 events length
 * 5.check virtual isp link to hw
 * 6.fix isp30 uyvy format error
 * 7.add API to get stream information
 * 8.3a params config first for isp21
 * 9.fix bigmode for multi device for isp21
 * 10.fix reg config for multi device
 * 11.add version to querycap
 * 12.fix mp uyvy format error for isp30
 * 13.add isp32 for rv1106
 * 14.isp32 bls2 remove to awb
 * 15.isp32 support mirror and flip
 * 16.isp32 support raw data compression
 * 17.add cgc config
 * 18.add get isp information api
 * 19.build depends on CPU config
 * 20.isp32 mi switch according to output buf
 * 21.add luma stream for isp32
 * 22.scale up and down for some stream
 * 23.isp32 add vsm
 * 24.fix config of capture_v30
 * 25.wrap mode for dvb
 * 26.use videobuf2-cma-sg
 * 27.remove vb2_dma_contig and vb2_dma_sg
 * 28.Revert "dynamic memory alloc for params and stats function"
 * 29.isp32 fix nv12 error
 * 30.fix using of vb2_cma_sg
 * 31.isp32 fix MI_WR_WRAP_CTRL default value
 * 32.fix params v32 drc and bay3d config
 * 33.add the rockit buff
 * 34.isp32 fix cac config
 * 35.isp32 support bay3d cur write to system sram
 * 36.isp32 fix frame id to dvbm
 * 37.set isp subdev crop and also check stream crop
 * 38.power on to set pipeline default format
 * 39.add isp reg cache read/write api
 * 40.Solve the problem of invalid mirror
 * 41.Solve the wrap_line frame rate problem
 * 42.vicap->isp online set clk according to sensor rate
 * 43.enable mipi drop interrupt
 * 44.isp32 fix bay3d config
 * 45.isp32 fix bay3d config
 * 46.Solve the cmsk problem
 * 47.fix stream link error
 * 48.update procfs for isp32
 * 49.support soft dvbm for vepu
 * 50.disable ISP_FRAME_IN irq
 * 51.fix rv1106 clk to 350
 * 52.fix init format for struct no clean
 * 53.procfs build with different isp version
 * 54.fix isp debug time for fe/fs irq together
 * 55.awb or gain debug info to ddr for isp32
 */

#define RKISP_DRIVER_VERSION RKISP_API_VERSION

#endif
