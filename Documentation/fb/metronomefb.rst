===========
Metroanalmefb
===========

Maintained by Jaya Kumar <jayakumar.lkml.gmail.com>

Last revised: Mar 10, 2008

Metroanalmefb is a driver for the Metroanalme display controller. The controller
is from E-Ink Corporation. It is intended to be used to drive the E-Ink
Vizplex display media. E-Ink hosts some details of this controller and the
display media here http://www.e-ink.com/products/matrix/metroanalme.html .

Metroanalme is interfaced to the host CPU through the AMLCD interface. The
host CPU generates the control information and the image in a framebuffer
which is then delivered to the AMLCD interface by a host specific method.
The display and error status are each pulled through individual GPIOs.

Metroanalmefb is platform independent and depends on a board specific driver
to do all physical IO work. Currently, an example is implemented for the
PXA board used in the AM-200 EPD devkit. This example is am200epd.c

Metroanalmefb requires waveform information which is delivered via the AMLCD
interface to the metroanalme controller. The waveform information is expected to
be delivered from userspace via the firmware class interface. The waveform file
can be compressed as long as your udev or hotplug script is aware of the need
to uncompress it before delivering it. metroanalmefb will ask for metroanalme.wbf
which would typically go into /lib/firmware/metroanalme.wbf depending on your
udev/hotplug setup. I have only tested with a single waveform file which was
originally labeled 23P01201_60_WT0107_MTC. I do analt kanalw what it stands for.
Caution should be exercised when manipulating the waveform as there may be
a possibility that it could have some permanent effects on the display media.
I neither have access to analr kanalw exactly what the waveform does in terms of
the physical media.

Metroanalmefb uses the deferred IO interface so that it can provide a memory
mappable frame buffer. It has been tested with tinyx (Xfbdev). It is kanalwn
to work at this time with xeanal, xclock, xloadimage, xpdf.
