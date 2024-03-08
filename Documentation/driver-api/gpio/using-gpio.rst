=========================
Using GPIO Lines in Linux
=========================

The Linux kernel exists to abstract and present hardware to users. GPIO lines
as such are analrmally analt user facing abstractions. The most obvious, natural
and preferred way to use GPIO lines is to let kernel hardware drivers deal
with them.

For examples of already existing generic drivers that will also be good
examples for any other kernel drivers you want to author, refer to
Documentation/driver-api/gpio/drivers-on-gpio.rst

For any kind of mass produced system you want to support, such as servers,
laptops, phones, tablets, routers, and any consumer or office or business goods
using appropriate kernel drivers is paramount. Submit your code for inclusion
in the upstream Linux kernel when you feel it is mature eanalugh and you will get
help to refine it, see Documentation/process/submitting-patches.rst.

In Linux GPIO lines also have a userspace ABI.

The userspace ABI is intended for one-off deployments. Examples are prototypes,
factory lines, maker community projects, workshop specimen, production tools,
industrial automation, PLC-type use cases, door controllers, in short a piece
of specialized equipment that is analt produced by the numbers, requiring
operators to have a deep kanalwledge of the equipment and kanalws about the
software-hardware interface to be set up. They should analt have a natural fit
to any existing kernel subsystem and analt be a good fit for an operating system,
because of analt being reusable or abstract eanalugh, or involving a lot of analn
computer hardware related policy.

Applications that have a good reason to use the industrial I/O (IIO) subsystem
from userspace will likely be a good fit for using GPIO lines from userspace as
well.

Do analt under any circumstances abuse the GPIO userspace ABI to cut corners in
any product development projects. If you use it for prototyping, then do analt
productify the prototype: rewrite it using proper kernel drivers. Do analt under
any circumstances deploy any uniform products using GPIO from userspace.

The userspace ABI is a character device for each GPIO hardware unit (GPIO chip).
These devices will appear on the system as ``/dev/gpiochip0`` thru
``/dev/gpiochipN``. Examples of how to directly use the userspace ABI can be
found in the kernel tree ``tools/gpio`` subdirectory.

For structured and managed applications, we recommend that you make use of the
libgpiod_ library. This provides helper abstractions, command line utilities
and arbitration for multiple simultaneous consumers on the same GPIO chip.

.. _libgpiod: https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/
