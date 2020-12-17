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
 */

#define RKISP_DRIVER_VERSION RKISP_API_VERSION

#endif
