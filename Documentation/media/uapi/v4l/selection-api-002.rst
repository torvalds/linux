.. -*- coding: utf-8; mode: rst -*-

************
Introduction
************

Some video capture devices can sample a subsection of a picture and
shrink or enlarge it to an image of arbitrary size. Next, the devices
can insert the image into larger one. Some video output devices can crop
part of an input image, scale it up or down and insert it at an
arbitrary scan line and horizontal offset into a video signal. We call
these abilities cropping, scaling and composing.

On a video *capture* device the source is a video signal, and the
cropping target determine the area actually sampled. The sink is an
image stored in a memory buffer. The composing area specifies which part
of the buffer is actually written to by the hardware.

On a video *output* device the source is an image in a memory buffer,
and the cropping target is a part of an image to be shown on a display.
The sink is the display or the graphics screen. The application may
select the part of display where the image should be displayed. The size
and position of such a window is controlled by the compose target.

Rectangles for all cropping and composing targets are defined even if
the device does supports neither cropping nor composing. Their size and
position will be fixed in such a case. If the device does not support
scaling then the cropping and composing rectangles have the same size.
