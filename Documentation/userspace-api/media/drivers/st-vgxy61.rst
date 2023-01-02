.. SPDX-License-Identifier: GPL-2.0

ST VGXY61 camera sensor driver
==============================

The ST VGXY61 driver implements the following controls:

``V4L2_CID_HDR_SENSOR_MODE``
-------------------------------
    Change the sensor HDR mode. A HDR picture is obtained by merging two
    captures of the same scene using two different exposure periods.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 4

    * - HDR linearize
      - The merger outputs a long exposure capture as long as it is not
        saturated.
    * - HDR substraction
      - This involves subtracting the short exposure frame from the long
        exposure frame.
    * - No HDR
      - This mode is used for standard dynamic range (SDR) exposures.
