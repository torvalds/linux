.. SPDX-License-Identifier: GPL-2.0

.. include:: <isonum.txt>

The Samsung S5P/EXYNOS4 FIMC driver
===================================

Copyright |copy| 2012 - 2013 Samsung Electronics Co., Ltd.

Files partitioning
------------------

- media device driver

  drivers/media/platform/samsung/exynos4-is/media-dev.[ch]

- camera capture video device driver

  drivers/media/platform/samsung/exynos4-is/fimc-capture.c

- MIPI-CSI2 receiver subdev

  drivers/media/platform/samsung/exynos4-is/mipi-csis.[ch]

- video post-processor (mem-to-mem)

  drivers/media/platform/samsung/exynos4-is/fimc-core.c

- common files

  drivers/media/platform/samsung/exynos4-is/fimc-core.h
  drivers/media/platform/samsung/exynos4-is/fimc-reg.h
  drivers/media/platform/samsung/exynos4-is/regs-fimc.h
