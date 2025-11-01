===============================================
The irq_domain Interrupt Number Mapping Library
===============================================

The current design of the Linux kernel uses a single large number
space where each separate IRQ source is assigned a unique number.
This is simple when there is only one interrupt controller. But in
systems with multiple interrupt controllers, the kernel must ensure
that each one gets assigned non-overlapping allocations of Linux
IRQ numbers.

The number of interrupt controllers registered as unique irqchips
shows a rising tendency. For example, subdrivers of different kinds
such as GPIO controllers avoid reimplementing identical callback
mechanisms as the IRQ core system by modelling their interrupt
handlers as irqchips. I.e. in effect cascading interrupt controllers.

So in the past, IRQ numbers could be chosen so that they match the
hardware IRQ line into the root interrupt controller (i.e. the
component actually firing the interrupt line to the CPU). Nowadays,
this number is just a number and the number has no
relationship to hardware interrupt numbers.

For this reason, we need a mechanism to separate controller-local
interrupt numbers, called hardware IRQs, from Linux IRQ numbers.

The irq_alloc_desc*() and irq_free_desc*() APIs provide allocation of
IRQ numbers, but they don't provide any support for reverse mapping of
the controller-local IRQ (hwirq) number into the Linux IRQ number
space.

The irq_domain library adds a mapping between hwirq and IRQ numbers on
top of the irq_alloc_desc*() API. An irq_domain to manage the mapping
is preferred over interrupt controller drivers open coding their own
reverse mapping scheme.

irq_domain also implements a translation from an abstract struct
irq_fwspec to hwirq numbers (Device Tree, non-DT firmware node, ACPI
GSI, and software node so far), and can be easily extended to support
other IRQ topology data sources. The implementation is performed
without any extra platform support code.

irq_domain Usage
================
struct irq_domain could be defined as an irq domain controller. That
is, it handles the mapping between hardware and virtual interrupt
numbers for a given interrupt domain. The domain structure is
generally created by the PIC code for a given PIC instance (though a
domain can cover more than one PIC if they have a flat number model).
It is the domain callbacks that are responsible for setting the
irq_chip on a given irq_desc after it has been mapped.

The host code and data structures use a fwnode_handle pointer to
identify the domain. In some cases, and in order to preserve source
code compatibility, this fwnode pointer is "upgraded" to a DT
device_node. For those firmware infrastructures that do not provide a
unique identifier for an interrupt controller, the irq_domain code
offers a fwnode allocator.

An interrupt controller driver creates and registers a struct irq_domain
by calling one of the irq_domain_create_*() functions (each mapping
method has a different allocator function, more on that later). The
function will return a pointer to the struct irq_domain on success. The
caller must provide the allocator function with a struct irq_domain_ops
pointer.

In most cases, the irq_domain will begin empty without any mappings
between hwirq and IRQ numbers.  Mappings are added to the irq_domain
by calling irq_create_mapping() which accepts the irq_domain and a
hwirq number as arguments. If a mapping for the hwirq doesn't already
exist, irq_create_mapping() allocates a new Linux irq_desc, associates
it with the hwirq, and calls the :c:member:`irq_domain_ops.map()`
callback. In there, the driver can perform any required hardware
setup.

Once a mapping has been established, it can be retrieved or used via a
variety of methods:

- irq_resolve_mapping() returns a pointer to the irq_desc structure
  for a given domain and hwirq number, or NULL if there was no
  mapping.
- irq_find_mapping() returns a Linux IRQ number for a given domain and
  hwirq number, or 0 if there was no mapping
- generic_handle_domain_irq() handles an interrupt described by a
  domain and a hwirq number

Note that irq_domain lookups must happen in contexts that are
compatible with an RCU read-side critical section.

The irq_create_mapping() function must be called *at least once*
before any call to irq_find_mapping(), lest the descriptor will not
be allocated.

If the driver has the Linux IRQ number or the irq_data pointer, and
needs to know the associated hwirq number (such as in the irq_chip
callbacks) then it can be directly obtained from
:c:member:`irq_data.hwirq`.

Types of irq_domain Mappings
============================

There are several mechanisms available for reverse mapping from hwirq
to Linux IRQ, and each mechanism uses a different allocation function.
Which reverse map type should be used depends on the use case.  Each
of the reverse map types are described below:

Linear
------

::

	irq_domain_create_linear()

The linear reverse map maintains a fixed-size table indexed by the
hwirq number.  When a hwirq is mapped, an irq_desc is allocated for
the hwirq, and the IRQ number is stored in the table.

The Linear map is a good choice when the maximum number of hwirqs is
fixed and a relatively small number (~ < 256).  The advantages of this
map are fixed-time lookup for IRQ numbers, and irq_descs are only
allocated for in-use IRQs.  The disadvantage is that the table must be
as large as the largest possible hwirq number.

The majority of drivers should use the Linear map.

Tree
----

::

	irq_domain_create_tree()

The irq_domain maintains a radix tree map from hwirq numbers to Linux
IRQs.  When an hwirq is mapped, an irq_desc is allocated and the
hwirq is used as the lookup key for the radix tree.

The Tree map is a good choice if the hwirq number can be very large
since it doesn't need to allocate a table as large as the largest
hwirq number.  The disadvantage is that hwirq to IRQ number lookup is
dependent on how many entries are in the table.

Very few drivers should need this mapping.

No Map
------

::

	irq_domain_create_nomap()

The No Map mapping is to be used when the hwirq number is
programmable in the hardware.  In this case it is best to program the
Linux IRQ number into the hardware itself so that no mapping is
required.  Calling irq_create_direct_mapping() will allocate a Linux
IRQ number and call the .map() callback so that driver can program the
Linux IRQ number into the hardware.

Most drivers cannot use this mapping, and it is now gated on the
CONFIG_IRQ_DOMAIN_NOMAP option. Please refrain from introducing new
users of this API.

Legacy
------

::

	irq_domain_create_simple()
	irq_domain_create_legacy()

The Legacy mapping is a special case for drivers that already have a
range of irq_descs allocated for the hwirqs.  It is used when the
driver cannot be immediately converted to use the Linear mapping.  For
example, many embedded system board support files use a set of #defines
for IRQ numbers that are passed to struct device registrations.  In that
case the Linux IRQ numbers cannot be dynamically assigned and the Legacy
mapping should be used.

As the name implies, the \*_legacy() functions are deprecated and only
exist to ease the support of ancient platforms. No new users should be
added. Same goes for the \*_simple() functions when their use results
in the legacy behaviour.

The Legacy map assumes a contiguous range of IRQ numbers has already
been allocated for the controller and that the IRQ number can be
calculated by adding a fixed offset to the hwirq number, and
visa-versa.  The disadvantage is that it requires the interrupt
controller to manage IRQ allocations and it requires an irq_desc to be
allocated for every hwirq, even if it is unused.

The Legacy map should only be used if fixed IRQ mappings must be
supported.  For example, ISA controllers would use the Legacy map for
mapping Linux IRQs 0-15 so that existing ISA drivers get the correct IRQ
numbers.

Most users of legacy mappings should use irq_domain_create_simple()
which will use a legacy domain only if an IRQ range is supplied by the
system and will otherwise use a linear domain mapping. The semantics of
this call are such that if an IRQ range is specified then descriptors
will be allocated on-the-fly for it, and if no range is specified it
will fall through to irq_domain_create_linear() which means *no* IRQ
descriptors will be allocated.

A typical use case for simple domains is where an irqchip provider
is supporting both dynamic and static IRQ assignments.

In order to avoid ending up in a situation where a linear domain is
used and no descriptor gets allocated it is very important to make sure
that the driver using the simple domain call irq_create_mapping()
before any irq_find_mapping() since the latter will actually work
for the static IRQ assignment case.

Hierarchy IRQ Domain
--------------------

On some architectures, there may be multiple interrupt controllers
involved in delivering an interrupt from the device to the target CPU.
Let's look at a typical interrupt delivery path on x86 platforms::

  Device --> IOAPIC -> Interrupt remapping Controller -> Local APIC -> CPU

There are three interrupt controllers involved:

1) IOAPIC controller
2) Interrupt remapping controller
3) Local APIC controller

To support such a hardware topology and make software architecture match
hardware architecture, an irq_domain data structure is built for each
interrupt controller and those irq_domains are organized into hierarchy.
When building irq_domain hierarchy, the irq_domain nearest the device is
child and the irq_domain nearest the CPU is parent. So a hierarchy structure
as below will be built for the example above::

	CPU Vector irq_domain (root irq_domain to manage CPU vectors)
		^
		|
	Interrupt Remapping irq_domain (manage irq_remapping entries)
		^
		|
	IOAPIC irq_domain (manage IOAPIC delivery entries/pins)

There are four major interfaces to use hierarchy irq_domain:

1) irq_domain_alloc_irqs(): allocate IRQ descriptors and interrupt
   controller related resources to deliver these interrupts.
2) irq_domain_free_irqs(): free IRQ descriptors and interrupt controller
   related resources associated with these interrupts.
3) irq_domain_activate_irq(): activate interrupt controller hardware to
   deliver the interrupt.
4) irq_domain_deactivate_irq(): deactivate interrupt controller hardware
   to stop delivering the interrupt.

The following is needed to support hierarchy irq_domain:

1) The :c:member:`parent` field in struct irq_domain is used to
   maintain irq_domain hierarchy information.
2) The :c:member:`parent_data` field in struct irq_data is used to
   build hierarchy irq_data to match hierarchy irq_domains. The
   irq_data is used to store irq_domain pointer and hardware irq
   number.
3) The :c:member:`alloc()`, :c:member:`free()`, and other callbacks in
   struct irq_domain_ops to support hierarchy irq_domain operations.

With the support of hierarchy irq_domain and hierarchy irq_data ready,
an irq_domain structure is built for each interrupt controller, and an
irq_data structure is allocated for each irq_domain associated with an
IRQ.

For an interrupt controller driver to support hierarchy irq_domain, it
needs to:

1) Implement irq_domain_ops.alloc() and irq_domain_ops.free()
2) Optionally, implement irq_domain_ops.activate() and
   irq_domain_ops.deactivate().
3) Optionally, implement an irq_chip to manage the interrupt controller
   hardware.
4) There is no need to implement irq_domain_ops.map() and
   irq_domain_ops.unmap(). They are unused with hierarchy irq_domain.

Note the hierarchy irq_domain is in no way x86-specific, and is
heavily used to support other architectures, such as ARM, ARM64 etc.

Stacked irq_chip
~~~~~~~~~~~~~~~~

Now, we could go one step further to support stacked (hierarchy)
irq_chip. That is, an irq_chip is associated with each irq_data along
the hierarchy. A child irq_chip may implement a required action by
itself or by cooperating with its parent irq_chip.

With stacked irq_chip, interrupt controller driver only needs to deal
with the hardware managed by itself and may ask for services from its
parent irq_chip when needed. So we could achieve a much cleaner
software architecture.

Debugging
=========

Most of the internals of the IRQ subsystem are exposed in debugfs by
turning CONFIG_GENERIC_IRQ_DEBUGFS on.

Structures and Public Functions Provided
========================================

This chapter contains the autogenerated documentation of the structures
and exported kernel API functions which are used for IRQ domains.

.. kernel-doc:: include/linux/irqdomain.h

.. kernel-doc:: kernel/irq/irqdomain.c
   :export:

Internal Functions Provided
===========================

This chapter contains the autogenerated documentation of the internal
functions.

.. kernel-doc:: kernel/irq/irqdomain.c
   :internal:
