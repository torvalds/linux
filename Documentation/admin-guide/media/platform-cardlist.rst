.. SPDX-License-Identifier: GPL-2.0

Platform drivers
================

There are several drivers that are focused on providing support for
functionality that are already included at the main board, and don't
use neither USB nor PCI bus. Those drivers are called platform
drivers, and are very popular on embedded devices.

The current supported of platform drivers (not including staging drivers) are
listed below

=================  ============================================================
Driver             Name
=================  ============================================================
am437x-vpfe        TI AM437x VPFE
aspeed-video       Aspeed AST2400 and AST2500
atmel-isc          ATMEL Image Sensor Controller (ISC)
atmel-isi          ATMEL Image Sensor Interface (ISI)
c8sectpfe          SDR platform devices
c8sectpfe          SDR platform devices
cafe_ccic          Marvell 88ALP01 (Cafe) CMOS Camera Controller
cdns-csi2rx        Cadence MIPI-CSI2 RX Controller
cdns-csi2tx        Cadence MIPI-CSI2 TX Controller
coda-vpu           Chips&Media Coda multi-standard codec IP
dm355_ccdc         TI DM355 CCDC video capture
dm644x_ccdc        TI DM6446 CCDC video capture
exynos-fimc-is     EXYNOS4x12 FIMC-IS (Imaging Subsystem)
exynos-fimc-lite   EXYNOS FIMC-LITE camera interface
exynos-gsc         Samsung Exynos G-Scaler
exy                Samsung S5P/EXYNOS4 SoC series Camera Subsystem
fsl-viu            Freescale VIU
imx-pxp            i.MX Pixel Pipeline (PXP)
isdf               TI DM365 ISIF video capture
mmp_camera         Marvell Armada 610 integrated camera controller
mtk_jpeg           Mediatek JPEG Codec
mtk-mdp            Mediatek MDP
mtk-vcodec-dec     Mediatek Video Codec
mtk-vpu            Mediatek Video Processor Unit
mx2_emmaprp        MX2 eMMa-PrP
omap3-isp          OMAP 3 Camera
omap-vout          OMAP2/OMAP3 V4L2-Display
pxa_camera         PXA27x Quick Capture Interface
qcom-camss         Qualcomm V4L2 Camera Subsystem
rcar-csi2          R-Car MIPI CSI-2 Receiver
rcar_drif          Renesas Digital Radio Interface (DRIF)
rcar-fcp           Renesas Frame Compression Processor
rcar_fdp1          Renesas Fine Display Processor
rcar_jpu           Renesas JPEG Processing Unit
rcar-vin           R-Car Video Input (VIN)
renesas-ceu        Renesas Capture Engine Unit (CEU)
rockchip-rga       Rockchip Raster 2d Graphic Acceleration Unit
s3c-camif          Samsung S3C24XX/S3C64XX SoC Camera Interface
s5p-csis           S5P/EXYNOS MIPI-CSI2 receiver (MIPI-CSIS)
s5p-fimc           S5P/EXYNOS4 FIMC/CAMIF camera interface
s5p-g2d            Samsung S5P and EXYNOS4 G2D 2d graphics accelerator
s5p-jpeg           Samsung S5P/Exynos3250/Exynos4 JPEG codec
s5p-mfc            Samsung S5P MFC Video Codec
sh_veu             SuperH VEU mem2mem video processing
sh_vou             SuperH VOU video output
stm32-dcmi         STM32 Digital Camera Memory Interface (DCMI)
stm32-dma2d        STM32 Chrom-Art Accelerator Unit
sun4i-csi          Allwinner A10 CMOS Sensor Interface Support
sun6i-csi          Allwinner V3s Camera Sensor Interface
sun8i-di           Allwinner Deinterlace
sun8i-rotate       Allwinner DE2 rotation
ti-cal             TI Memory-to-memory multimedia devices
ti-csc             TI DVB platform devices
ti-vpe             TI VPE (Video Processing Engine)
venus-enc          Qualcomm Venus V4L2 encoder/decoder
via-camera         VIAFB camera controller
video-mux          Video Multiplexer
vpif_display       TI DaVinci VPIF V4L2-Display
vpif_capture       TI DaVinci VPIF video capture
vpss               TI DaVinci VPBE V4L2-Display
vsp1               Renesas VSP1 Video Processing Engine
xilinx-tpg         Xilinx Video Test Pattern Generator
xilinx-video       Xilinx Video IP (EXPERIMENTAL)
xilinx-vtc         Xilinx Video Timing Controller
=================  ============================================================

MMC/SDIO DVB adapters
---------------------

=======  ===========================================
Driver   Name
=======  ===========================================
smssdio  Siano SMS1xxx based MDTV via SDIO interface
=======  ===========================================

