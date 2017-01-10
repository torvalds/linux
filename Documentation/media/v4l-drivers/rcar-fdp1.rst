Renesas R-Car Fine Display Processor (FDP1) Driver
==================================================

The R-Car FDP1 driver implements driver-specific controls as follows.

``V4L2_CID_DEINTERLACING_MODE (menu)``
    The video deinterlacing mode (such as Bob, Weave, ...). The R-Car FDP1
    driver implements the following modes.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 4

    * - ``"Progressive" (0)``
      - The input image video stream is progressive (not interlaced). No
        deinterlacing is performed. Apart from (optional) format and encoding
        conversion output frames are identical to the input frames.
    * - ``"Adaptive 2D/3D" (1)``
      - Motion adaptive version of 2D and 3D deinterlacing. Use 3D deinterlacing
        in the presence of fast motion and 2D deinterlacing with diagonal
        interpolation otherwise.
    * - ``"Fixed 2D" (2)``
      - The current field is scaled vertically by averaging adjacent lines to
        recover missing lines. This method is also known as blending or Line
        Averaging (LAV).
    * - ``"Fixed 3D" (3)``
      - The previous and next fields are averaged to recover lines missing from
        the current field. This method is also known as Field Averaging (FAV).
    * - ``"Previous field" (4)``
      - The current field is weaved with the previous field, i.e. the previous
        field is used to fill missing lines from the current field. This method
        is also known as weave deinterlacing.
    * - ``"Next field" (5)``
      - The current field is weaved with the next field, i.e. the next field is
        used to fill missing lines from the current field. This method is also
        known as weave deinterlacing.
