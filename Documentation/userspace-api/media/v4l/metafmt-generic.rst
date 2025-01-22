.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.1-no-invariants-or-later

********************************************************************************************************************************************************************************************************************************************************************************
V4L2_META_FMT_GENERIC_8 ('MET8'), V4L2_META_FMT_GENERIC_CSI2_10 ('MC1A'), V4L2_META_FMT_GENERIC_CSI2_12 ('MC1C'), V4L2_META_FMT_GENERIC_CSI2_14 ('MC1E'), V4L2_META_FMT_GENERIC_CSI2_16 ('MC1G'), V4L2_META_FMT_GENERIC_CSI2_20 ('MC1K'), V4L2_META_FMT_GENERIC_CSI2_24 ('MC1O')
********************************************************************************************************************************************************************************************************************************************************************************


Generic line-based metadata formats


Description
===========

These generic line-based metadata formats define the memory layout of the data
without defining the format or meaning of the metadata itself.

.. _v4l2-meta-fmt-generic-8:

V4L2_META_FMT_GENERIC_8
-----------------------

The V4L2_META_FMT_GENERIC_8 format is a plain 8-bit metadata format. This format
is used on CSI-2 for 8 bits per :term:`Data Unit`.

Additionally it is used for 16 bits per Data Unit when two bytes of metadata are
packed into one 16-bit Data Unit. Otherwise the 16 bits per pixel dataformat is
:ref:`V4L2_META_FMT_GENERIC_CSI2_16 <v4l2-meta-fmt-generic-csi2-16>`.

**Byte Order Of V4L2_META_FMT_GENERIC_8.**
Each cell is one byte. "M" denotes a byte of metadata.

.. tabularcolumns:: |p{2.4cm}|p{1.2cm}|p{1.2cm}|p{1.2cm}|p{1.2cm}|

.. flat-table:: Sample 4x2 Metadata Frame
    :header-rows:  0
    :stub-columns: 0
    :widths: 12 8 8 8 8

    * - start + 0:
      - M\ :sub:`00`
      - M\ :sub:`10`
      - M\ :sub:`20`
      - M\ :sub:`30`
    * - start + 4:
      - M\ :sub:`01`
      - M\ :sub:`11`
      - M\ :sub:`21`
      - M\ :sub:`31`

.. _v4l2-meta-fmt-generic-csi2-10:

V4L2_META_FMT_GENERIC_CSI2_10
-----------------------------

V4L2_META_FMT_GENERIC_CSI2_10 contains 8-bit generic metadata packed in 10-bit
Data Units, with one padding byte after every four bytes of metadata. This
format is typically used by CSI-2 receivers with a source that transmits
MEDIA_BUS_FMT_META_10 and the CSI-2 receiver writes the received data to memory
as-is.

The packing of the data follows the MIPI CSI-2 specification and the padding of
the data is defined in the MIPI CCS specification.

This format is also used in conjunction with 20 bits per :term:`Data Unit`
formats that pack two bytes of metadata into one Data Unit. Otherwise the
20 bits per pixel dataformat is :ref:`V4L2_META_FMT_GENERIC_CSI2_20
<v4l2-meta-fmt-generic-csi2-20>`.

This format is little endian.

**Byte Order Of V4L2_META_FMT_GENERIC_CSI2_10.**
Each cell is one byte. "M" denotes a byte of metadata and "x" a byte of padding.

.. tabularcolumns:: |p{2.4cm}|p{1.2cm}|p{1.2cm}|p{1.2cm}|p{1.2cm}|p{1.8cm}|

.. flat-table:: Sample 4x2 Metadata Frame
    :header-rows:  0
    :stub-columns: 0
    :widths: 12 8 8 8 8 8

    * - start + 0:
      - M\ :sub:`00`
      - M\ :sub:`10`
      - M\ :sub:`20`
      - M\ :sub:`30`
      - x
    * - start + 5:
      - M\ :sub:`01`
      - M\ :sub:`11`
      - M\ :sub:`21`
      - M\ :sub:`31`
      - x

.. _v4l2-meta-fmt-generic-csi2-12:

V4L2_META_FMT_GENERIC_CSI2_12
-----------------------------

V4L2_META_FMT_GENERIC_CSI2_12 contains 8-bit generic metadata packed in 12-bit
Data Units, with one padding byte after every two bytes of metadata. This format
is typically used by CSI-2 receivers with a source that transmits
MEDIA_BUS_FMT_META_12 and the CSI-2 receiver writes the received data to memory
as-is.

The packing of the data follows the MIPI CSI-2 specification and the padding of
the data is defined in the MIPI CCS specification.

This format is also used in conjunction with 24 bits per :term:`Data Unit`
formats that pack two bytes of metadata into one Data Unit. Otherwise the
24 bits per pixel dataformat is :ref:`V4L2_META_FMT_GENERIC_CSI2_24
<v4l2-meta-fmt-generic-csi2-24>`.

This format is little endian.

**Byte Order Of V4L2_META_FMT_GENERIC_CSI2_12.**
Each cell is one byte. "M" denotes a byte of metadata and "x" a byte of padding.

.. tabularcolumns:: |p{2.4cm}|p{1.2cm}|p{1.2cm}|p{1.8cm}|p{1.2cm}|p{1.2cm}|p{1.8cm}|

.. flat-table:: Sample 4x2 Metadata Frame
    :header-rows:  0
    :stub-columns: 0
    :widths: 12 8 8 8 8 8 8

    * - start + 0:
      - M\ :sub:`00`
      - M\ :sub:`10`
      - x
      - M\ :sub:`20`
      - M\ :sub:`30`
      - x
    * - start + 6:
      - M\ :sub:`01`
      - M\ :sub:`11`
      - x
      - M\ :sub:`21`
      - M\ :sub:`31`
      - x

.. _v4l2-meta-fmt-generic-csi2-14:

V4L2_META_FMT_GENERIC_CSI2_14
-----------------------------

V4L2_META_FMT_GENERIC_CSI2_14 contains 8-bit generic metadata packed in 14-bit
Data Units, with three padding bytes after every four bytes of metadata. This
format is typically used by CSI-2 receivers with a source that transmits
MEDIA_BUS_FMT_META_14 and the CSI-2 receiver writes the received data to memory
as-is.

The packing of the data follows the MIPI CSI-2 specification and the padding of
the data is defined in the MIPI CCS specification.

This format is little endian.

**Byte Order Of V4L2_META_FMT_GENERIC_CSI2_14.**
Each cell is one byte. "M" denotes a byte of metadata and "x" a byte of padding.

.. tabularcolumns:: |p{2.4cm}|p{1.2cm}|p{1.2cm}|p{1.2cm}|p{1.2cm}|p{1.8cm}|p{1.8cm}|p{1.8cm}|

.. flat-table:: Sample 4x2 Metadata Frame
    :header-rows:  0
    :stub-columns: 0
    :widths: 12 8 8 8 8 8 8 8

    * - start + 0:
      - M\ :sub:`00`
      - M\ :sub:`10`
      - M\ :sub:`20`
      - M\ :sub:`30`
      - x
      - x
      - x
    * - start + 7:
      - M\ :sub:`01`
      - M\ :sub:`11`
      - M\ :sub:`21`
      - M\ :sub:`31`
      - x
      - x
      - x

.. _v4l2-meta-fmt-generic-csi2-16:

V4L2_META_FMT_GENERIC_CSI2_16
-----------------------------

V4L2_META_FMT_GENERIC_CSI2_16 contains 8-bit generic metadata packed in 16-bit
Data Units, with one padding byte after every byte of metadata. This format is
typically used by CSI-2 receivers with a source that transmits
MEDIA_BUS_FMT_META_16 and the CSI-2 receiver writes the received data to memory
as-is.

The packing of the data follows the MIPI CSI-2 specification and the padding of
the data is defined in the MIPI CCS specification.

Some devices support more efficient packing of metadata in conjunction with
16-bit image data. In that case the dataformat is
:ref:`V4L2_META_FMT_GENERIC_8 <v4l2-meta-fmt-generic-8>`.

This format is little endian.

**Byte Order Of V4L2_META_FMT_GENERIC_CSI2_16.**
Each cell is one byte. "M" denotes a byte of metadata and "x" a byte of padding.

.. tabularcolumns:: |p{2.4cm}|p{1.2cm}|p{.8cm}|p{1.2cm}|p{.8cm}|p{1.2cm}|p{.8cm}|p{1.2cm}|p{.8cm}|

.. flat-table:: Sample 4x2 Metadata Frame
    :header-rows:  0
    :stub-columns: 0
    :widths: 12 8 8 8 8 8 8 8 8

    * - start + 0:
      - M\ :sub:`00`
      - x
      - M\ :sub:`10`
      - x
      - M\ :sub:`20`
      - x
      - M\ :sub:`30`
      - x
    * - start + 8:
      - M\ :sub:`01`
      - x
      - M\ :sub:`11`
      - x
      - M\ :sub:`21`
      - x
      - M\ :sub:`31`
      - x

.. _v4l2-meta-fmt-generic-csi2-20:

V4L2_META_FMT_GENERIC_CSI2_20
-----------------------------

V4L2_META_FMT_GENERIC_CSI2_20 contains 8-bit generic metadata packed in 20-bit
Data Units, with alternating one or two padding bytes after every byte of
metadata. This format is typically used by CSI-2 receivers with a source that
transmits MEDIA_BUS_FMT_META_20 and the CSI-2 receiver writes the received data
to memory as-is.

The packing of the data follows the MIPI CSI-2 specification and the padding of
the data is defined in the MIPI CCS specification.

Some devices support more efficient packing of metadata in conjunction with
16-bit image data. In that case the dataformat is
:ref:`V4L2_META_FMT_GENERIC_CSI2_10 <v4l2-meta-fmt-generic-csi2-10>`.

This format is little endian.

**Byte Order Of V4L2_META_FMT_GENERIC_CSI2_20.**
Each cell is one byte. "M" denotes a byte of metadata and "x" a byte of padding.

.. tabularcolumns:: |p{2.4cm}|p{1.2cm}|p{1.2cm}|p{1.2cm}|p{1.2cm}|p{1.8cm}|p{1.2cm}|p{1.2cm}|p{1.2cm}|p{1.2cm}|p{1.8cm}

.. flat-table:: Sample 4x2 Metadata Frame
    :header-rows:  0
    :stub-columns: 0
    :widths: 12 8 8 8 8 8 8 8 8 8 8

    * - start + 0:
      - M\ :sub:`00`
      - x
      - M\ :sub:`10`
      - x
      - x
      - M\ :sub:`20`
      - x
      - M\ :sub:`30`
      - x
      - x
    * - start + 10:
      - M\ :sub:`01`
      - x
      - M\ :sub:`11`
      - x
      - x
      - M\ :sub:`21`
      - x
      - M\ :sub:`31`
      - x
      - x

.. _v4l2-meta-fmt-generic-csi2-24:

V4L2_META_FMT_GENERIC_CSI2_24
-----------------------------

V4L2_META_FMT_GENERIC_CSI2_24 contains 8-bit generic metadata packed in 24-bit
Data Units, with two padding bytes after every byte of metadata. This format is
typically used by CSI-2 receivers with a source that transmits
MEDIA_BUS_FMT_META_24 and the CSI-2 receiver writes the received data to memory
as-is.

The packing of the data follows the MIPI CSI-2 specification and the padding of
the data is defined in the MIPI CCS specification.

Some devices support more efficient packing of metadata in conjunction with
16-bit image data. In that case the dataformat is
:ref:`V4L2_META_FMT_GENERIC_CSI2_12 <v4l2-meta-fmt-generic-csi2-12>`.

This format is little endian.

**Byte Order Of V4L2_META_FMT_GENERIC_CSI2_24.**
Each cell is one byte. "M" denotes a byte of metadata and "x" a byte of padding.

.. tabularcolumns:: |p{2.4cm}|p{1.2cm}|p{.8cm}|p{.8cm}|p{1.2cm}|p{.8cm}|p{.8cm}|p{1.2cm}|p{.8cm}|p{.8cm}|p{1.2cm}|p{.8cm}|p{.8cm}|

.. flat-table:: Sample 4x2 Metadata Frame
    :header-rows:  0
    :stub-columns: 0
    :widths: 12 8 8 8 8 8 8 8 8 8 8 8 8

    * - start + 0:
      - M\ :sub:`00`
      - x
      - x
      - M\ :sub:`10`
      - x
      - x
      - M\ :sub:`20`
      - x
      - x
      - M\ :sub:`30`
      - x
      - x
    * - start + 12:
      - M\ :sub:`01`
      - x
      - x
      - M\ :sub:`11`
      - x
      - x
      - M\ :sub:`21`
      - x
      - x
      - M\ :sub:`31`
      - x
      - x
