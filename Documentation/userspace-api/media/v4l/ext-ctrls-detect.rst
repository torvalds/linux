.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _detect-controls:

************************
Detect Control Reference
************************

The Detect class includes controls for common features of various motion
or object detection capable devices.


.. _detect-control-id:

Detect Control IDs
==================

``V4L2_CID_DETECT_CLASS (class)``
    The Detect class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

``V4L2_CID_DETECT_MD_MODE (menu)``
    Sets the motion detection mode.

.. tabularcolumns:: |p{7.7cm}|p{9.8cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_DETECT_MD_MODE_DISABLED``
      - Disable motion detection.
    * - ``V4L2_DETECT_MD_MODE_GLOBAL``
      - Use a single motion detection threshold.
    * - ``V4L2_DETECT_MD_MODE_THRESHOLD_GRID``
      - The image is divided into a grid, each cell with its own motion
	detection threshold. These thresholds are set through the
	``V4L2_CID_DETECT_MD_THRESHOLD_GRID`` matrix control.
    * - ``V4L2_DETECT_MD_MODE_REGION_GRID``
      - The image is divided into a grid, each cell with its own region
	value that specifies which per-region motion detection thresholds
	should be used. Each region has its own thresholds. How these
	per-region thresholds are set up is driver-specific. The region
	values for the grid are set through the
	``V4L2_CID_DETECT_MD_REGION_GRID`` matrix control.



``V4L2_CID_DETECT_MD_GLOBAL_THRESHOLD (integer)``
    Sets the global motion detection threshold to be used with the
    ``V4L2_DETECT_MD_MODE_GLOBAL`` motion detection mode.

``V4L2_CID_DETECT_MD_THRESHOLD_GRID (__u16 matrix)``
    Sets the motion detection thresholds for each cell in the grid. To
    be used with the ``V4L2_DETECT_MD_MODE_THRESHOLD_GRID`` motion
    detection mode. Matrix element (0, 0) represents the cell at the
    top-left of the grid.

``V4L2_CID_DETECT_MD_REGION_GRID (__u8 matrix)``
    Sets the motion detection region value for each cell in the grid. To
    be used with the ``V4L2_DETECT_MD_MODE_REGION_GRID`` motion
    detection mode. Matrix element (0, 0) represents the cell at the
    top-left of the grid.
