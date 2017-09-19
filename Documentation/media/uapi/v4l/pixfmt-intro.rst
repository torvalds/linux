.. -*- coding: utf-8; mode: rst -*-

**********************
Standard Image Formats
**********************

In order to exchange images between drivers and applications, it is
necessary to have standard image data formats which both sides will
interpret the same way. V4L2 includes several such formats, and this
section is intended to be an unambiguous specification of the standard
image data formats in V4L2.

V4L2 drivers are not limited to these formats, however. Driver-specific
formats are possible. In that case the application may depend on a codec
to convert images to one of the standard formats when needed. But the
data can still be stored and retrieved in the proprietary format. For
example, a device may support a proprietary compressed format.
Applications can still capture and save the data in the compressed
format, saving much disk space, and later use a codec to convert the
images to the X Windows screen format when the video is to be displayed.

Even so, ultimately, some standard formats are needed, so the V4L2
specification would not be complete without well-defined standard
formats.

The V4L2 standard formats are mainly uncompressed formats. The pixels
are always arranged in memory from left to right, and from top to
bottom. The first byte of data in the image buffer is always for the
leftmost pixel of the topmost row. Following that is the pixel
immediately to its right, and so on until the end of the top row of
pixels. Following the rightmost pixel of the row there may be zero or
more bytes of padding to guarantee that each row of pixel data has a
certain alignment. Following the pad bytes, if any, is data for the
leftmost pixel of the second row from the top, and so on. The last row
has just as many pad bytes after it as the other rows.

In V4L2 each format has an identifier which looks like ``PIX_FMT_XXX``,
defined in the :ref:`videodev2.h <videodev>` header file. These
identifiers represent
:ref:`four character (FourCC) codes <v4l2-fourcc>` which are also
listed below, however they are not the same as those used in the Windows
world.

For some formats, data is stored in separate, discontiguous memory
buffers. Those formats are identified by a separate set of FourCC codes
and are referred to as "multi-planar formats". For example, a
:ref:`YUV422 <V4L2-PIX-FMT-YUV422M>` frame is normally stored in one
memory buffer, but it can also be placed in two or three separate
buffers, with Y component in one buffer and CbCr components in another
in the 2-planar version or with each component in its own buffer in the
3-planar case. Those sub-buffers are referred to as "*planes*".
