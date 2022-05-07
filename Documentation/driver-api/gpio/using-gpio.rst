=========================
Using GPIO Lines in Linux
=========================

The Linux kernel exists to abstract and present hardware to users. GPIO lines
as such are normally not user facing abstractions. The most obvious, natural
and preferred way to use GPIO lines is to let kernel hardware drivers deal
with them.

For examples of already existing generic drivers that will also be good
examples for any other kernel drivers you want to author, refer to
:doc:`drivers-on-gpio`

For any kind of mass produced system you want to support, such as servers,
laptops, phones, tablets, routers, and any consumer or office or business goods
using appropriate kernel drivers is paramount. Submit your code for inclusion
in the upstream Linux kernel when you feel it is mature enough and you will get
help to refine it, see :doc:`../../process/submitting-patches`.

In Linux GPIO lines also have a userspace ABI.

The userspace ABI is intended for one-off deployments. Examples are prototypes,
factory lines, maker community projects, workshop specimen, production tools,
industrial automation, PLC-type use cases, door controllers, in short a piece
of specialized equipment that is not produced by the numbers, requiring
operators to have a deep knowledge of the equipment and knows about the
software-hardware interface to be set up. They should not have a natural fit
to any existing kernel subsystem and not be a good fit for an operating system,
because of not being reusable or abstract enough, or involving a lot of non
computer hardware related policy.

Applications that have a good reason to use the industrial I/O (IIO) subsystem
from userspace will likely be a good fit for using GPIO lines from userspace as
well.

Do not under any circumstances abuse the GPIO userspace ABI to cut corners in
any product development projects. If you use it for prototyping, then do not
productify the prototype: rewrite it using proper kernel drivers. Do not under
any circumstances deploy any uniform products using GPIO from userspace.

The userspace ABI is a character device for each GPIO hardware unit (GPIO chip).
These devices will appear on the system as ``/dev/gpiochip0`` thru
``/dev/gpiochipN``. Examples of how to directly use the userspace ABI can be
found in the kernel tree ``tools/gpio`` subdirectory.

For structured and managed applications, we recommend that you make use of the
libgpiod_ library. This provides helper abstractions, command line utlities
and arbitration for multiple simultaneous consumers on the same GPIO chip.

.. _libgpiod: https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/
