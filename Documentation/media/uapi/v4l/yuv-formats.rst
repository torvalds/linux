.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _yuv-formats:

***********
YUV Formats
***********

YUV is the format native to TV broadcast and composite video signals. It
separates the brightness information (Y) from the color information (U
and V or Cb and Cr). The color information consists of red and blue
*color difference* signals, this way the green component can be
reconstructed by subtracting from the brightness component. See
:ref:`colorspaces` for conversion examples. YUV was chosen because
early television would only transmit brightness information. To add
color in a way compatible with existing receivers a new signal carrier
was added to transmit the color difference signals. Secondary in the YUV
format the U and V components usually have lower resolution than the Y
component. This is an analog video compression technique taking
advantage of a property of the human visual system, being more sensitive
to brightness information.


.. toctree::
    :maxdepth: 1

    pixfmt-packed-yuv
    pixfmt-grey
    pixfmt-y10
    pixfmt-y12
    pixfmt-y10b
    pixfmt-y10p
    pixfmt-y16
    pixfmt-y16-be
    pixfmt-y8i
    pixfmt-y12i
    pixfmt-uv8
    pixfmt-yuyv
    pixfmt-uyvy
    pixfmt-yvyu
    pixfmt-vyuy
    pixfmt-y41p
    pixfmt-yuv420
    pixfmt-yuv420m
    pixfmt-yuv422m
    pixfmt-yuv444m
    pixfmt-yuv410
    pixfmt-yuv422p
    pixfmt-yuv411p
    pixfmt-nv12
    pixfmt-nv12m
    pixfmt-nv12mt
    pixfmt-nv16
    pixfmt-nv16m
    pixfmt-nv24
    pixfmt-m420
