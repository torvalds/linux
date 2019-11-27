.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _v4l2-selection-targets:

*****************
Selection targets
*****************

The precise meaning of the selection targets may be dependent on which
of the two interfaces they are used.


.. _v4l2-selection-targets-table:

.. tabularcolumns:: |p{6.0cm}|p{1.4cm}|p{7.4cm}|p{1.2cm}|p{1.4cm}|

.. flat-table:: Selection target definitions
    :header-rows:  1
    :stub-columns: 0

    * - Target name
      - id
      - Definition
      - Valid for V4L2
      - Valid for V4L2 subdev
    * - ``V4L2_SEL_TGT_CROP``
      - 0x0000
      - Crop rectangle. Defines the cropped area.
      - Yes
      - Yes
    * - ``V4L2_SEL_TGT_CROP_DEFAULT``
      - 0x0001
      - Suggested cropping rectangle that covers the "whole picture".
        This includes only active pixels and excludes other non-active
        pixels such as black pixels.
      - Yes
      - Yes
    * - ``V4L2_SEL_TGT_CROP_BOUNDS``
      - 0x0002
      - Bounds of the crop rectangle. All valid crop rectangles fit inside
	the crop bounds rectangle.
      - Yes
      - Yes
    * - ``V4L2_SEL_TGT_NATIVE_SIZE``
      - 0x0003
      - The native size of the device, e.g. a sensor's pixel array.
	``left`` and ``top`` fields are zero for this target.
      - Yes
      - Yes
    * - ``V4L2_SEL_TGT_COMPOSE``
      - 0x0100
      - Compose rectangle. Used to configure scaling and composition.
      - Yes
      - Yes
    * - ``V4L2_SEL_TGT_COMPOSE_DEFAULT``
      - 0x0101
      - Suggested composition rectangle that covers the "whole picture".
      - Yes
      - No
    * - ``V4L2_SEL_TGT_COMPOSE_BOUNDS``
      - 0x0102
      - Bounds of the compose rectangle. All valid compose rectangles fit
	inside the compose bounds rectangle.
      - Yes
      - Yes
    * - ``V4L2_SEL_TGT_COMPOSE_PADDED``
      - 0x0103
      - The active area and all padding pixels that are inserted or
	modified by hardware.
      - Yes
      - No
