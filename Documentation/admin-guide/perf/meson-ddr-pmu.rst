.. SPDX-License-Identifier: GPL-2.0

===========================================================
Amlogic SoC DDR Bandwidth Performance Monitoring Unit (PMU)
===========================================================

The Amlogic Meson G12 SoC contains a bandwidth monitor inside DRAM controller.
The monitor includes 4 channels. Each channel can count the request accessing
DRAM. The channel can count up to 3 AXI port simultaneously. It can be helpful
to show if the performance bottleneck is on DDR bandwidth.

Currently, this driver supports the following 5 perf events:

+ meson_ddr_bw/total_rw_bytes/
+ meson_ddr_bw/chan_1_rw_bytes/
+ meson_ddr_bw/chan_2_rw_bytes/
+ meson_ddr_bw/chan_3_rw_bytes/
+ meson_ddr_bw/chan_4_rw_bytes/

meson_ddr_bw/chan_{1,2,3,4}_rw_bytes/ events are channel-specific events.
Each channel support filtering, which can let the channel to monitor
individual IP module in SoC.

Below are DDR access request event filter keywords:

+ arm             - from CPU
+ vpu_read1       - from OSD + VPP read
+ gpu             - from 3D GPU
+ pcie            - from PCIe controller
+ hdcp            - from HDCP controller
+ hevc_front      - from HEVC codec front end
+ usb3_0          - from USB3.0 controller
+ hevc_back       - from HEVC codec back end
+ h265enc         - from HEVC encoder
+ vpu_read2       - from DI read
+ vpu_write1      - from VDIN write
+ vpu_write2      - from di write
+ vdec            - from legacy codec video decoder
+ hcodec          - from H264 encoder
+ ge2d            - from ge2d
+ spicc1          - from SPI controller 1
+ usb0            - from USB2.0 controller 0
+ dma             - from system DMA controller 1
+ arb0            - from arb0
+ sd_emmc_b       - from SD eMMC b controller
+ usb1            - from USB2.0 controller 1
+ audio           - from Audio module
+ sd_emmc_c       - from SD eMMC c controller
+ spicc2          - from SPI controller 2
+ ethernet        - from Ethernet controller


Examples:

  + Show the total DDR bandwidth per seconds:

    .. code-block:: bash

       perf stat -a -e meson_ddr_bw/total_rw_bytes/ -I 1000 sleep 10


  + Show individual DDR bandwidth from CPU and GPU respectively, as well as
    sum of them:

    .. code-block:: bash

       perf stat -a -e meson_ddr_bw/chan_1_rw_bytes,arm=1/ -I 1000 sleep 10
       perf stat -a -e meson_ddr_bw/chan_2_rw_bytes,gpu=1/ -I 1000 sleep 10
       perf stat -a -e meson_ddr_bw/chan_3_rw_bytes,arm=1,gpu=1/ -I 1000 sleep 10

