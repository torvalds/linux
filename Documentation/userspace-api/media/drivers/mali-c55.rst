.. SPDX-License-Identifier: GPL-2.0-only

Arm Mali-C55 ISP driver
=======================

The Arm Mali-C55 ISP driver implements a single driver-specific control:

``V4L2_CID_MALI_C55_CAPABILITIES (bitmask)``
    Detail the capabilities of the ISP by giving detail about the fitted blocks.

    .. flat-table:: Bitmask meaning definitions
	:header-rows: 1
	:widths: 2 4 8

	* - Bit
	  - Macro
	  - Meaning
        * - 0
          - MALI_C55_PONG
          - Pong configuration space is fitted in the ISP
        * - 1
          - MALI_C55_WDR
          - WDR Framestitch, offset and gain is fitted in the ISP
        * - 2
          - MALI_C55_COMPRESSION
          - Temper compression is fitted in the ISP
        * - 3
          - MALI_C55_TEMPER
          - Temper is fitted in the ISP
        * - 4
          - MALI_C55_SINTER_LITE
          - Sinter Lite is fitted in the ISP instead of the full Sinter version
        * - 5
          - MALI_C55_SINTER
          - Sinter is fitted in the ISP
        * - 6
          - MALI_C55_IRIDIX_LTM
          - Iridix local tone mappine is fitted in the ISP
        * - 7
          - MALI_C55_IRIDIX_GTM
          - Iridix global tone mapping is fitted in the ISP
        * - 8
          - MALI_C55_CNR
          - Colour noise reduction is fitted in the ISP
        * - 9
          - MALI_C55_FRSCALER
          - The full resolution pipe scaler is fitted in the ISP
        * - 10
          - MALI_C55_DS_PIPE
          - The downscale pipe is fitted in the ISP

    The Mali-C55 ISP can be configured in a number of ways to include or exclude
    blocks which may not be necessary. This control provides a way for the
    driver to communicate to userspace which of the blocks are fitted in the
    design.