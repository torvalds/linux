================================
GPIO Descriptor Driver Interface
================================

This document serves as a guide for GPIO chip drivers writers. Note that it
describes the new descriptor-based interface. For a description of the
deprecated integer-based GPIO interface please refer to gpio-legacy.txt.

Each GPIO controller driver needs to include the following header, which defines
the structures used to define a GPIO driver:

	#include <linux/gpio/driver.h>


Internal Representation of GPIOs
================================

Inside a GPIO driver, individual GPIOs are identified by their hardware number,
which is a unique number between 0 and n, n being the number of GPIOs managed by
the chip. This number is purely internal: the hardware number of a particular
GPIO descriptor is never made visible outside of the driver.

On top of this internal number, each GPIO also need to have a global number in
the integer GPIO namespace so that it can be used with the legacy GPIO
interface. Each chip must thus have a "base" number (which can be automatically
assigned), and for each GPIO the global number will be (base + hardware number).
Although the integer representation is considered deprecated, it still has many
users and thus needs to be maintained.

So for example one platform could use numbers 32-159 for GPIOs, with a
controller defining 128 GPIOs at a "base" of 32 ; while another platform uses
numbers 0..63 with one set of GPIO controllers, 64-79 with another type of GPIO
controller, and on one particular board 80-95 with an FPGA. The numbers need not
be contiguous; either of those platforms could also use numbers 2000-2063 to
identify GPIOs in a bank of I2C GPIO expanders.


Controller Drivers: gpio_chip
=============================

In the gpiolib framework each GPIO controller is packaged as a "struct
gpio_chip" (see linux/gpio/driver.h for its complete definition) with members
common to each controller of that type:

 - methods to establish GPIO line direction
 - methods used to access GPIO line values
 - method to set electrical configuration for a given GPIO line
 - method to return the IRQ number associated to a given GPIO line
 - flag saying whether calls to its methods may sleep
 - optional line names array to identify lines
 - optional debugfs dump method (showing extra state like pullup config)
 - optional base number (will be automatically assigned if omitted)
 - optional label for diagnostics and GPIO chip mapping using platform data

The code implementing a gpio_chip should support multiple instances of the
controller, possibly using the driver model. That code will configure each
gpio_chip and issue ``gpiochip_add[_data]()`` or ``devm_gpiochip_add_data()``.
Removing a GPIO controller should be rare; use ``[devm_]gpiochip_remove()``
when it is unavoidable.

Often a gpio_chip is part of an instance-specific structure with states not
exposed by the GPIO interfaces, such as addressing, power management, and more.
Chips such as audio codecs will have complex non-GPIO states.

Any debugfs dump method should normally ignore signals which haven't been
requested as GPIOs. They can use gpiochip_is_requested(), which returns either
NULL or the label associated with that GPIO when it was requested.

RT_FULL: the GPIO driver should not use spinlock_t or any sleepable APIs
(like PM runtime) in its gpio_chip implementation (.get/.set and direction
control callbacks) if it is expected to call GPIO APIs from atomic context
on -RT (inside hard IRQ handlers and similar contexts). Normally this should
not be required.


GPIO electrical configuration
-----------------------------

GPIOs can be configured for several electrical modes of operation by using the
.set_config() callback. Currently this API supports setting debouncing and
single-ended modes (open drain/open source). These settings are described
below.

The .set_config() callback uses the same enumerators and configuration
semantics as the generic pin control drivers. This is not a coincidence: it is
possible to assign the .set_config() to the function gpiochip_generic_config()
which will result in pinctrl_gpio_set_config() being called and eventually
ending up in the pin control back-end "behind" the GPIO controller, usually
closer to the actual pins. This way the pin controller can manage the below
listed GPIO configurations.

If a pin controller back-end is used, the GPIO controller or hardware
description needs to provide "GPIO ranges" mapping the GPIO line offsets to pin
numbers on the pin controller so they can properly cross-reference each other.


GPIOs with debounce support
---------------------------

Debouncing is a configuration set to a pin indicating that it is connected to
a mechanical switch or button, or similar that may bounce. Bouncing means the
line is pulled high/low quickly at very short intervals for mechanical
reasons. This can result in the value being unstable or irqs fireing repeatedly
unless the line is debounced.

Debouncing in practice involves setting up a timer when something happens on
the line, wait a little while and then sample the line again, so see if it
still has the same value (low or high). This could also be repeated by a clever
state machine, waiting for a line to become stable. In either case, it sets
a certain number of milliseconds for debouncing, or just "on/off" if that time
is not configurable.


GPIOs with open drain/source support
------------------------------------

Open drain (CMOS) or open collector (TTL) means the line is not actively driven
high: instead you provide the drain/collector as output, so when the transistor
is not open, it will present a high-impedance (tristate) to the external rail::


   CMOS CONFIGURATION      TTL CONFIGURATION

            ||--- out              +--- out
     in ----||                   |/
            ||--+         in ----|
                |                |\
               GND	           GND

This configuration is normally used as a way to achieve one of two things:

- Level-shifting: to reach a logical level higher than that of the silicon
  where the output resides.

- inverse wire-OR on an I/O line, for example a GPIO line, making it possible
  for any driving stage on the line to drive it low even if any other output
  to the same line is simultaneously driving it high. A special case of this
  is driving the SCL and SCA lines of an I2C bus, which is by definition a
  wire-OR bus.

Both usecases require that the line be equipped with a pull-up resistor. This
resistor will make the line tend to high level unless one of the transistors on
the rail actively pulls it down.

The level on the line will go as high as the VDD on the pull-up resistor, which
may be higher than the level supported by the transistor, achieving a
level-shift to the higher VDD.

Integrated electronics often have an output driver stage in the form of a CMOS
"totem-pole" with one N-MOS and one P-MOS transistor where one of them drives
the line high and one of them drives the line low. This is called a push-pull
output. The "totem-pole" looks like so::

                     VDD
                      |
            OD    ||--+
         +--/ ---o||     P-MOS-FET
         |        ||--+
    IN --+            +----- out
         |        ||--+
         +--/ ----||     N-MOS-FET
            OS    ||--+
                      |
                     GND

The desired output signal (e.g. coming directly from some GPIO output register)
arrives at IN. The switches named "OD" and "OS" are normally closed, creating
a push-pull circuit.

Consider the little "switches" named "OD" and "OS" that enable/disable the
P-MOS or N-MOS transistor right after the split of the input. As you can see,
either transistor will go totally numb if this switch is open. The totem-pole
is then halved and give high impedance instead of actively driving the line
high or low respectively. That is usually how software-controlled open
drain/source works.

Some GPIO hardware come in open drain / open source configuration. Some are
hard-wired lines that will only support open drain or open source no matter
what: there is only one transistor there. Some are software-configurable:
by flipping a bit in a register the output can be configured as open drain
or open source, in practice by flicking open the switches labeled "OD" and "OS"
in the drawing above.

By disabling the P-MOS transistor, the output can be driven between GND and
high impedance (open drain), and by disabling the N-MOS transistor, the output
can be driven between VDD and high impedance (open source). In the first case,
a pull-up resistor is needed on the outgoing rail to complete the circuit, and
in the second case, a pull-down resistor is needed on the rail.

Hardware that supports open drain or open source or both, can implement a
special callback in the gpio_chip: .set_config() that takes a generic
pinconf packed value telling whether to configure the line as open drain,
open source or push-pull. This will happen in response to the
GPIO_OPEN_DRAIN or GPIO_OPEN_SOURCE flag set in the machine file, or coming
from other hardware descriptions.

If this state can not be configured in hardware, i.e. if the GPIO hardware does
not support open drain/open source in hardware, the GPIO library will instead
use a trick: when a line is set as output, if the line is flagged as open
drain, and the IN output value is low, it will be driven low as usual. But
if the IN output value is set to high, it will instead *NOT* be driven high,
instead it will be switched to input, as input mode is high impedance, thus
achieveing an "open drain emulation" of sorts: electrically the behaviour will
be identical, with the exception of possible hardware glitches when switching
the mode of the line.

For open source configuration the same principle is used, just that instead
of actively driving the line low, it is set to input.


GPIO drivers providing IRQs
---------------------------
It is custom that GPIO drivers (GPIO chips) are also providing interrupts,
most often cascaded off a parent interrupt controller, and in some special
cases the GPIO logic is melded with a SoC's primary interrupt controller.

The IRQ portions of the GPIO block are implemented using an irqchip, using
the header <linux/irq.h>. So basically such a driver is utilizing two sub-
systems simultaneously: gpio and irq.

RT_FULL: a realtime compliant GPIO driver should not use spinlock_t or any
sleepable APIs (like PM runtime) as part of its irq_chip implementation.

* spinlock_t should be replaced with raw_spinlock_t [1].
* If sleepable APIs have to be used, these can be done from the .irq_bus_lock()
  and .irq_bus_unlock() callbacks, as these are the only slowpath callbacks
  on an irqchip. Create the callbacks if needed [2].

GPIO irqchips usually fall in one of two categories:

* CHAINED GPIO irqchips: these are usually the type that is embedded on
  an SoC. This means that there is a fast IRQ flow handler for the GPIOs that
  gets called in a chain from the parent IRQ handler, most typically the
  system interrupt controller. This means that the GPIO irqchip handler will
  be called immediately from the parent irqchip, while holding the IRQs
  disabled. The GPIO irqchip will then end up calling something like this
  sequence in its interrupt handler::

    static irqreturn_t foo_gpio_irq(int irq, void *data)
        chained_irq_enter(...);
        generic_handle_irq(...);
        chained_irq_exit(...);

  Chained GPIO irqchips typically can NOT set the .can_sleep flag on
  struct gpio_chip, as everything happens directly in the callbacks: no
  slow bus traffic like I2C can be used.

  RT_FULL: Note, chained IRQ handlers will not be forced threaded on -RT.
  As result, spinlock_t or any sleepable APIs (like PM runtime) can't be used
  in chained IRQ handler.
  If required (and if it can't be converted to the nested threaded GPIO irqchip)
  a chained IRQ handler can be converted to generic irq handler and this way
  it will be a threaded IRQ handler on -RT and a hard IRQ handler on non-RT
  (for example, see [3]).
  Know W/A: The generic_handle_irq() is expected to be called with IRQ disabled,
  so the IRQ core will complain if it is called from an IRQ handler which is
  forced to a thread. The "fake?" raw lock can be used to W/A this problem::

	raw_spinlock_t wa_lock;
	static irqreturn_t omap_gpio_irq_handler(int irq, void *gpiobank)
		unsigned long wa_lock_flags;
		raw_spin_lock_irqsave(&bank->wa_lock, wa_lock_flags);
		generic_handle_irq(irq_find_mapping(bank->chip.irq.domain, bit));
		raw_spin_unlock_irqrestore(&bank->wa_lock, wa_lock_flags);

* GENERIC CHAINED GPIO irqchips: these are the same as "CHAINED GPIO irqchips",
  but chained IRQ handlers are not used. Instead GPIO IRQs dispatching is
  performed by generic IRQ handler which is configured using request_irq().
  The GPIO irqchip will then end up calling something like this sequence in
  its interrupt handler::

    static irqreturn_t gpio_rcar_irq_handler(int irq, void *dev_id)
        for each detected GPIO IRQ
            generic_handle_irq(...);

  RT_FULL: Such kind of handlers will be forced threaded on -RT, as result IRQ
  core will complain that generic_handle_irq() is called with IRQ enabled and
  the same W/A as for "CHAINED GPIO irqchips" can be applied.

* NESTED THREADED GPIO irqchips: these are off-chip GPIO expanders and any
  other GPIO irqchip residing on the other side of a sleeping bus. Of course
  such drivers that need slow bus traffic to read out IRQ status and similar,
  traffic which may in turn incur other IRQs to happen, cannot be handled
  in a quick IRQ handler with IRQs disabled. Instead they need to spawn a
  thread and then mask the parent IRQ line until the interrupt is handled
  by the driver. The hallmark of this driver is to call something like
  this in its interrupt handler::

    static irqreturn_t foo_gpio_irq(int irq, void *data)
        ...
        handle_nested_irq(irq);

  The hallmark of threaded GPIO irqchips is that they set the .can_sleep
  flag on struct gpio_chip to true, indicating that this chip may sleep
  when accessing the GPIOs.

To help out in handling the set-up and management of GPIO irqchips and the
associated irqdomain and resource allocation callbacks, the gpiolib has
some helpers that can be enabled by selecting the GPIOLIB_IRQCHIP Kconfig
symbol:

* gpiochip_irqchip_add(): adds a chained irqchip to a gpiochip. It will pass
  the struct gpio_chip* for the chip to all IRQ callbacks, so the callbacks
  need to embed the gpio_chip in its state container and obtain a pointer
  to the container using container_of().
  (See Documentation/driver-model/design-patterns.txt)

* gpiochip_irqchip_add_nested(): adds a nested irqchip to a gpiochip.
  Apart from that it works exactly like the chained irqchip.

* gpiochip_set_chained_irqchip(): sets up a chained irq handler for a
  gpio_chip from a parent IRQ and passes the struct gpio_chip* as handler
  data. (Notice handler data, since the irqchip data is likely used by the
  parent irqchip!).

* gpiochip_set_nested_irqchip(): sets up a nested irq handler for a
  gpio_chip from a parent IRQ. As the parent IRQ has usually been
  explicitly requested by the driver, this does very little more than
  mark all the child IRQs as having the other IRQ as parent.

If there is a need to exclude certain GPIOs from the IRQ domain, you can
set .irq.need_valid_mask of the gpiochip before gpiochip_add_data() is
called. This allocates an .irq.valid_mask with as many bits set as there
are GPIOs in the chip. Drivers can exclude GPIOs by clearing bits from this
mask. The mask must be filled in before gpiochip_irqchip_add() or
gpiochip_irqchip_add_nested() is called.

To use the helpers please keep the following in mind:

- Make sure to assign all relevant members of the struct gpio_chip so that
  the irqchip can initialize. E.g. .dev and .can_sleep shall be set up
  properly.

- Nominally set all handlers to handle_bad_irq() in the setup call and pass
  handle_bad_irq() as flow handler parameter in gpiochip_irqchip_add() if it is
  expected for GPIO driver that irqchip .set_type() callback have to be called
  before using/enabling GPIO IRQ. Then set the handler to handle_level_irq()
  and/or handle_edge_irq() in the irqchip .set_type() callback depending on
  what your controller supports.

It is legal for any IRQ consumer to request an IRQ from any irqchip no matter
if that is a combined GPIO+IRQ driver. The basic premise is that gpio_chip and
irq_chip are orthogonal, and offering their services independent of each
other.

gpiod_to_irq() is just a convenience function to figure out the IRQ for a
certain GPIO line and should not be relied upon to have been called before
the IRQ is used.

So always prepare the hardware and make it ready for action in respective
callbacks from the GPIO and irqchip APIs. Do not rely on gpiod_to_irq() having
been called first.

This orthogonality leads to ambiguities that we need to solve: if there is
competition inside the subsystem which side is using the resource (a certain
GPIO line and register for example) it needs to deny certain operations and
keep track of usage inside of the gpiolib subsystem. This is why the API
below exists.


Locking IRQ usage
-----------------
Input GPIOs can be used as IRQ signals. When this happens, a driver is requested
to mark the GPIO as being used as an IRQ::

	int gpiochip_lock_as_irq(struct gpio_chip *chip, unsigned int offset)

This will prevent the use of non-irq related GPIO APIs until the GPIO IRQ lock
is released::

	void gpiochip_unlock_as_irq(struct gpio_chip *chip, unsigned int offset)

When implementing an irqchip inside a GPIO driver, these two functions should
typically be called in the .startup() and .shutdown() callbacks from the
irqchip.

When using the gpiolib irqchip helpers, these callback are automatically
assigned.

Real-Time compliance for GPIO IRQ chips
---------------------------------------

Any provider of irqchips needs to be carefully tailored to support Real Time
preemption. It is desirable that all irqchips in the GPIO subsystem keep this
in mind and do the proper testing to assure they are real time-enabled.
So, pay attention on above " RT_FULL:" notes, please.
The following is a checklist to follow when preparing a driver for real
time-compliance:

- ensure spinlock_t is not used as part irq_chip implementation;
- ensure that sleepable APIs are not used as part irq_chip implementation.
  If sleepable APIs have to be used, these can be done from the .irq_bus_lock()
  and .irq_bus_unlock() callbacks;
- Chained GPIO irqchips: ensure spinlock_t or any sleepable APIs are not used
  from chained IRQ handler;
- Generic chained GPIO irqchips: take care about generic_handle_irq() calls and
  apply corresponding W/A;
- Chained GPIO irqchips: get rid of chained IRQ handler and use generic irq
  handler if possible :)
- regmap_mmio: Sry, but you are in trouble :( if MMIO regmap is used as for
  GPIO IRQ chip implementation;
- Test your driver with the appropriate in-kernel real time test cases for both
  level and edge IRQs.


Requesting self-owned GPIO pins
-------------------------------

Sometimes it is useful to allow a GPIO chip driver to request its own GPIO
descriptors through the gpiolib API. Using gpio_request() for this purpose
does not help since it pins the module to the kernel forever (it calls
try_module_get()). A GPIO driver can use the following functions instead
to request and free descriptors without being pinned to the kernel forever::

	struct gpio_desc *gpiochip_request_own_desc(struct gpio_desc *desc,
						    const char *label)

	void gpiochip_free_own_desc(struct gpio_desc *desc)

Descriptors requested with gpiochip_request_own_desc() must be released with
gpiochip_free_own_desc().

These functions must be used with care since they do not affect module use
count. Do not use the functions to request gpio descriptors not owned by the
calling driver.

* [1] http://www.spinics.net/lists/linux-omap/msg120425.html
* [2] https://lkml.org/lkml/2015/9/25/494
* [3] https://lkml.org/lkml/2015/9/25/495
