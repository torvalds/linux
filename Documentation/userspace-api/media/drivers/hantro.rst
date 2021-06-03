.. SPDX-License-Identifier: GPL-2.0

Hantro video decoder driver
===========================

The Hantro video decoder driver implements the following driver-specific controls:

``V4L2_CID_HANTRO_HEVC_SLICE_HEADER_SKIP (integer)``
    Specifies to Hantro HEVC video decoder driver the number of data (in bits) to
    skip in the slice segment header.
    If non-IDR, the bits to be skipped go from syntax element "pic_output_flag"
    to before syntax element "slice_temporal_mvp_enabled_flag".
    If IDR, the skipped bits are just "pic_output_flag"
    (separate_colour_plane_flag is not supported).

.. note::

        This control is not yet part of the public kernel API and
        it is expected to change.
