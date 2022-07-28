====================
Low Level Serial API
====================


This document is meant as a brief overview of some aspects of the new serial
driver.  It is not complete, any questions you have should be directed to
<rmk@arm.linux.org.uk>

The reference implementation is contained within amba-pl011.c.



Low Level Serial Hardware Driver
--------------------------------

The low level serial hardware driver is responsible for supplying port
information (defined by uart_port) and a set of control methods (defined
by uart_ops) to the core serial driver.  The low level driver is also
responsible for handling interrupts for the port, and providing any
console support.


Console Support
---------------

The serial core provides a few helper functions.  This includes identifing
the correct port structure (via uart_get_console) and decoding command line
arguments (uart_parse_options).

There is also a helper function (uart_console_write) which performs a
character by character write, translating newlines to CRLF sequences.
Driver writers are recommended to use this function rather than implementing
their own version.


Locking
-------

It is the responsibility of the low level hardware driver to perform the
necessary locking using port->lock.  There are some exceptions (which
are described in the uart_ops listing below.)

There are two locks.  A per-port spinlock, and an overall semaphore.

From the core driver perspective, the port->lock locks the following
data::

	port->mctrl
	port->icount
	port->state->xmit.head (circ_buf->head)
	port->state->xmit.tail (circ_buf->tail)

The low level driver is free to use this lock to provide any additional
locking.

The port_sem semaphore is used to protect against ports being added/
removed or reconfigured at inappropriate times. Since v2.6.27, this
semaphore has been the 'mutex' member of the tty_port struct, and
commonly referred to as the port mutex.


uart_ops
--------

.. kernel-doc:: include/linux/serial_core.h
   :identifiers: uart_ops

Other functions
---------------

uart_update_timeout(port,cflag,baud)
	Update the frame timing information according to the number of bits,
	parity, stop bits and baud rate. The FIFO drain timeout is derived
	from the frame timing information.

	Locking: caller is expected to take port->lock

	Interrupts: n/a

uart_get_baud_rate(port,termios,old,min,max)
	Return the numeric baud rate for the specified termios, taking
	account of the special 38400 baud "kludge".  The B0 baud rate
	is mapped to 9600 baud.

	If the baud rate is not within min..max, then if old is non-NULL,
	the original baud rate will be tried.  If that exceeds the
	min..max constraint, 9600 baud will be returned.  termios will
	be updated to the baud rate in use.

	Note: min..max must always allow 9600 baud to be selected.

	Locking: caller dependent.

	Interrupts: n/a

uart_get_divisor(port,baud)
	Return the divisor (baud_base / baud) for the specified baud
	rate, appropriately rounded.

	If 38400 baud and custom divisor is selected, return the
	custom divisor instead.

	Locking: caller dependent.

	Interrupts: n/a

uart_match_port(port1,port2)
	This utility function can be used to determine whether two
	uart_port structures describe the same port.

	Locking: n/a

	Interrupts: n/a

uart_write_wakeup(port)
	A driver is expected to call this function when the number of
	characters in the transmit buffer have dropped below a threshold.

	Locking: port->lock should be held.

	Interrupts: n/a

uart_register_driver(drv)
	Register a uart driver with the core driver.  We in turn register
	with the tty layer, and initialise the core driver per-port state.

	drv->port should be NULL, and the per-port structures should be
	registered using uart_add_one_port after this call has succeeded.

	Locking: none

	Interrupts: enabled

uart_unregister_driver()
	Remove all references to a driver from the core driver.  The low
	level driver must have removed all its ports via the
	uart_remove_one_port() if it registered them with uart_add_one_port().

	Locking: none

	Interrupts: enabled

**uart_suspend_port()**

**uart_resume_port()**

**uart_add_one_port()**

**uart_remove_one_port()**

Other notes
-----------

It is intended some day to drop the 'unused' entries from uart_port, and
allow low level drivers to register their own individual uart_port's with
the core.  This will allow drivers to use uart_port as a pointer to a
structure containing both the uart_port entry with their own extensions,
thus::

	struct my_port {
		struct uart_port	port;
		int			my_stuff;
	};

Modem control lines via GPIO
----------------------------

Some helpers are provided in order to set/get modem control lines via GPIO.

mctrl_gpio_init(port, idx):
	This will get the {cts,rts,...}-gpios from device tree if they are
	present and request them, set direction etc, and return an
	allocated structure. `devm_*` functions are used, so there's no need
	to call mctrl_gpio_free().
	As this sets up the irq handling make sure to not handle changes to the
	gpio input lines in your driver, too.

mctrl_gpio_free(dev, gpios):
	This will free the requested gpios in mctrl_gpio_init().
	As `devm_*` functions are used, there's generally no need to call
	this function.

mctrl_gpio_to_gpiod(gpios, gidx)
	This returns the gpio_desc structure associated to the modem line
	index.

mctrl_gpio_set(gpios, mctrl):
	This will sets the gpios according to the mctrl state.

mctrl_gpio_get(gpios, mctrl):
	This will update mctrl with the gpios values.

mctrl_gpio_enable_ms(gpios):
	Enables irqs and handling of changes to the ms lines.

mctrl_gpio_disable_ms(gpios):
	Disables irqs and handling of changes to the ms lines.
