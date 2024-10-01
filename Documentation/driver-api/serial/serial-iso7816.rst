=============================
ISO7816 Serial Communications
=============================

1. Introduction
===============

  ISO/IEC7816 is a series of standards specifying integrated circuit cards (ICC)
  also known as smart cards.

2. Hardware-related considerations
==================================

  Some CPUs/UARTs (e.g., Microchip AT91) contain a built-in mode capable of
  handling communication with a smart card.

  For these microcontrollers, the Linux driver should be made capable of
  working in both modes, and proper ioctls (see later) should be made
  available at user-level to allow switching from one mode to the other, and
  vice versa.

3. Data Structures Already Available in the Kernel
==================================================

  The Linux kernel provides the serial_iso7816 structure (see [1]) to handle
  ISO7816 communications. This data structure is used to set and configure
  ISO7816 parameters in ioctls.

  Any driver for devices capable of working both as RS232 and ISO7816 should
  implement the iso7816_config callback in the uart_port structure. The
  serial_core calls iso7816_config to do the device specific part in response
  to TIOCGISO7816 and TIOCSISO7816 ioctls (see below). The iso7816_config
  callback receives a pointer to struct serial_iso7816.

4. Usage from user-level
========================

  From user-level, ISO7816 configuration can be get/set using the previous
  ioctls. For instance, to set ISO7816 you can use the following code::

	#include <linux/serial.h>

	/* Include definition for ISO7816 ioctls: TIOCSISO7816 and TIOCGISO7816 */
	#include <sys/ioctl.h>

	/* Open your specific device (e.g., /dev/mydevice): */
	int fd = open ("/dev/mydevice", O_RDWR);
	if (fd < 0) {
		/* Error handling. See errno. */
	}

	struct serial_iso7816 iso7816conf;

	/* Reserved fields as to be zeroed */
	memset(&iso7816conf, 0, sizeof(iso7816conf));

	/* Enable ISO7816 mode: */
	iso7816conf.flags |= SER_ISO7816_ENABLED;

	/* Select the protocol: */
	/* T=0 */
	iso7816conf.flags |= SER_ISO7816_T(0);
	/* or T=1 */
	iso7816conf.flags |= SER_ISO7816_T(1);

	/* Set the guard time: */
	iso7816conf.tg = 2;

	/* Set the clock frequency*/
	iso7816conf.clk = 3571200;

	/* Set transmission factors: */
	iso7816conf.sc_fi = 372;
	iso7816conf.sc_di = 1;

	if (ioctl(fd_usart, TIOCSISO7816, &iso7816conf) < 0) {
		/* Error handling. See errno. */
	}

	/* Use read() and write() syscalls here... */

	/* Close the device when finished: */
	if (close (fd) < 0) {
		/* Error handling. See errno. */
	}

5. References
=============

 [1]    include/uapi/linux/serial.h
