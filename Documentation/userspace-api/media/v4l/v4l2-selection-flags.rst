.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _v4l2-selection-flags:

***************
Selection flags
***************

.. tabularcolumns:: |p{5.2cm}|p{2.0cm}|p{6.5cm}|p{1.2cm}|p{1.6cm}|

.. _v4l2-selection-flags-table:

.. flat-table:: Selection flag definitions
    :header-rows:  1
    :stub-columns: 0

    * - Flag name
      - id
      - Definition
      - Valid for V4L2
      - Valid for V4L2 subdev
    * - ``V4L2_SEL_FLAG_GE``
      - (1 << 0)
      - Suggest the driver it should choose greater or equal rectangle (in
	size) than was requested. Albeit the driver may choose a lesser
	size, it will only do so due to hardware limitations. Without this
	flag (and ``V4L2_SEL_FLAG_LE``) the behaviour is to choose the
	closest possible rectangle.
      - Yes
      - Yes
    * - ``V4L2_SEL_FLAG_LE``
      - (1 << 1)
      - Suggest the driver it should choose lesser or equal rectangle (in
	size) than was requested. Albeit the driver may choose a greater
	size, it will only do so due to hardware limitations.
      - Yes
      - Yes
    * - ``V4L2_SEL_FLAG_KEEP_CONFIG``
      - (1 << 2)
      - The configuration must not be propagated to any further processing
	steps. If this flag is not given, the configuration is propagated
	inside the subdevice to all further processing steps.
      - No
      - Yes
