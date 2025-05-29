===============
What is an IRQ?
===============

An IRQ is an interrupt request from a device. Currently, they can come
in over a pin, or over a packet. Several devices may be connected to
the same pin thus sharing an IRQ. Such as on legacy PCI bus: All devices
typically share 4 lanes/pins. Note that each device can request an
interrupt on each of the lanes.

An IRQ number is a kernel identifier used to talk about a hardware
interrupt source. Typically, this is an index into the global irq_desc
array or sparse_irqs tree. But except for what linux/interrupt.h
implements, the details are architecture specific.

An IRQ number is an enumeration of the possible interrupt sources on a
machine. Typically, what is enumerated is the number of input pins on
all of the interrupt controllers in the system. In the case of ISA,
what is enumerated are the 8 input pins on each of the two i8259
interrupt controllers.

Architectures can assign additional meaning to the IRQ numbers, and
are encouraged to in the case where there is any manual configuration
of the hardware involved. The ISA IRQs are a classic example of
assigning this kind of additional meaning.
