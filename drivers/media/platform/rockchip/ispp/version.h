/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_VERSION_H
#define _RKISPP_VERSION_H
#include <linux/version.h>
#include <linux/rkispp-config.h>

/*
 * RKISPP DRIVER VERSION NOTE
 *
 * v0.1.0:
 * 1. First version;
 *
 * v0.1.1:
 * 1. support fbc output format
 * 2. support fec function
 * 3. support oneframe mode
 *
 * v0.1.2:
 * 1. support multi virtual device;
 *
 * v0.1.3:
 * 1. fix reg write err for fec
 *
 * v0.1.4:
 * 1. add clk rate set for rv1126
 * 2. safe to enable shp/fec output
 * 3. tnr skip input buf if no output buf
 *
 * v0.1.5:
 * 1. add proc fs
 * 2. add iq part information to procfs
 * 3. fix config err for stream switch
 *
 * v0.1.6:
 * 1. tnr support dynamic switch
 *
 * v0.1.7:
 * 1. fix cannot change some shadow bits by only config function
 * 2. fix scl0 format check error
 * 3. vb2 support cache hints
 *
 * v0.1.8:
 * 1. add monitor to restart if abnormal
 * 2. isp/ispp procfs add work info
 * 3. scl add yuyv format
 * 4. fix config err for tnr init off
 *
 * v0.1.9:
 * 1. isp and ispp sync to power off
 * 2. fix error status of stream off
 * 3. use fec share buffer to reduce buffer size
 *
 * v1.2.0:
 * 1. waiting all modules to idle to free buf
 * 2. enable sharp dma to ddr default
 * 3. using common dummy buf to save memory
 * 4. monitor thread to alive during work
 * 5. fix monitor thread exit
 * 6. tnr/nr/fec sync to start
 * 7. fec read yuyv format
 *
 * v1.2.1:
 * 1. fix can't work due to last abnormal exit
 *
 * v1.2.2:
 * 1. isp/ispp add lock for multi dev runtime
 * 2. fix error state of monitor
 * 3. fix mmu err due to buf free for multi dev
 * 4. support output isp/ispp reg on each frame
 * 5. fix error detected by depmod
 *
 * v1.3.0:
 * 1. fec extend to independent video
 * 2. reduce buf count
 * 3. dummy buf map to one page if iommu enable
 * 4. vb2 dma sg for iommu enable
 *
 * v1.4.1
 * 1. support motion detection mode
 * 2. fix panic for vmap at interrupt
 * 3. add virtual video for iqtool
 *
 * v1.5.1
 * 1. add vb2_rdma_sg_memops to support contiguous page
 * 2. fix config of clk_dbg
 * 3. check frame id when apply params
 *
 * v1.6.0 (match aiq v1.66.0)
 * 1. limit min clk to 50
 * 2. check scl stop if fec enable
 * 3. sync to free buf for multi dev stream off
 * 4. support output isp/ispp reg in nv12 format
 * 5. isp and ispp add shutdown
 * 6. optimize the frame rate of fec en
 * 7. image input from user
 * 8. fix input video config
 * 9. add cru reset
 * 10. check SHARP_CORE_CTRL after update
 * 11. add uvnr sd32 self en control
 *
 * v1.6.1
 * 1. reserved memory using rdma_sg ops
 * 2. destory ispp buffers if start_stream failed
 *
 * v1.6.2
 * 1. fix isp and ispp share dmabuf release fail
 * 2. fix bug that ispp register isn't included in SEI
 * 3. frame buffer done early
 * 4. reset at frame end
 * 5. fix page fault due to scl exit early
 * 6. fbc error handle
 * 7. first frame handle for multi dev
 * 8. fix driver mode sync with ispserver
 *
 * v1.7.0
 * 1. off unused interrupt
 * 2. fix monitor switch if don't power off
 * 3. frame start to check stream output buffer
 * 4. add frame loss info to procfs
 * 5. fix monitor no working
 * 6. disable scl dma write if no output buffer
 *
 * v1.8.0
 * 1. sync alloc buf with dma sg case
 * 2. remove tnr iir first frame skip
 * 3. replace iommu detach/attach
 * 4. solving ispp compilation problems
 * 5. add the stream_v20
 * 6. add rk3588 config
 * 7. Make rkispp_module_work_event() static
 * 8. add the iqtool module
 */

#define RKISPP_DRIVER_VERSION ISPP_API_VERSION

#endif
