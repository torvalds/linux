==============================
GSM 0710 tty multiplexor HOWTO
==============================

.. contents:: :local:

This line discipline implements the GSM 07.10 multiplexing protocol
detailed in the following 3GPP document:

	https://www.3gpp.org/ftp/Specs/archive/07_series/07.10/0710-720.zip

This document give some hints on how to use this driver with GPRS and 3G
modems connected to a physical serial port.

How to use it
=============

Config Initiator
----------------

#. Initialize the modem in 0710 mux mode (usually ``AT+CMUX=`` command) through
   its serial port. Depending on the modem used, you can pass more or less
   parameters to this command.

#. Switch the serial line to using the n_gsm line discipline by using
   ``TIOCSETD`` ioctl.

#. Configure the mux using ``GSMIOC_GETCONF_EXT``/``GSMIOC_SETCONF_EXT`` ioctl if needed.

#. Configure the mux using ``GSMIOC_GETCONF``/``GSMIOC_SETCONF`` ioctl.

#. Configure DLCs using ``GSMIOC_GETCONF_DLCI``/``GSMIOC_SETCONF_DLCI`` ioctl for non-defaults.

#. Obtain base gsmtty number for the used serial port.

   Major parts of the initialization program
   (a good starting point is util-linux-ng/sys-utils/ldattach.c)::

      #include <stdio.h>
      #include <stdint.h>
      #include <linux/gsmmux.h>
      #include <linux/tty.h>

      #define DEFAULT_SPEED	B115200
      #define SERIAL_PORT	/dev/ttyS0

      int ldisc = N_GSM0710;
      struct gsm_config c;
      struct gsm_config_ext ce;
      struct gsm_dlci_config dc;
      struct termios configuration;
      uint32_t first;

      /* open the serial port connected to the modem */
      fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY | O_NDELAY);

      /* configure the serial port : speed, flow control ... */

      /* send the AT commands to switch the modem to CMUX mode
         and check that it's successful (should return OK) */
      write(fd, "AT+CMUX=0\r", 10);

      /* experience showed that some modems need some time before
         being able to answer to the first MUX packet so a delay
         may be needed here in some case */
      sleep(3);

      /* use n_gsm line discipline */
      ioctl(fd, TIOCSETD, &ldisc);

      /* get n_gsm extended configuration */
      ioctl(fd, GSMIOC_GETCONF_EXT, &ce);
      /* use keep-alive once every 5s for modem connection supervision */
      ce.keep_alive = 500;
      /* set the new extended configuration */
      ioctl(fd, GSMIOC_SETCONF_EXT, &ce);
      /* get n_gsm configuration */
      ioctl(fd, GSMIOC_GETCONF, &c);
      /* we are initiator and need encoding 0 (basic) */
      c.initiator = 1;
      c.encapsulation = 0;
      /* our modem defaults to a maximum size of 127 bytes */
      c.mru = 127;
      c.mtu = 127;
      /* set the new configuration */
      ioctl(fd, GSMIOC_SETCONF, &c);
      /* get DLC 1 configuration */
      dc.channel = 1;
      ioctl(fd, GSMIOC_GETCONF_DLCI, &dc);
      /* the first user channel gets a higher priority */
      dc.priority = 1;
      /* set the new DLC 1 specific configuration */
      ioctl(fd, GSMIOC_SETCONF_DLCI, &dc);
      /* get first gsmtty device node */
      ioctl(fd, GSMIOC_GETFIRST, &first);
      printf("first muxed line: /dev/gsmtty%i\n", first);

      /* and wait for ever to keep the line discipline enabled */
      daemon(0,0);
      pause();

#. Use these devices as plain serial ports.

   For example, it's possible:

   - to use *gnokii* to send / receive SMS on ``ttygsm1``
   - to use *ppp* to establish a datalink on ``ttygsm2``

#. First close all virtual ports before closing the physical port.

   Note that after closing the physical port the modem is still in multiplexing
   mode. This may prevent a successful re-opening of the port later. To avoid
   this situation either reset the modem if your hardware allows that or send
   a disconnect command frame manually before initializing the multiplexing mode
   for the second time. The byte sequence for the disconnect command frame is::

      0xf9, 0x03, 0xef, 0x03, 0xc3, 0x16, 0xf9

Config Requester
----------------

#. Receive ``AT+CMUX=`` command through its serial port, initialize mux mode
   config.

#. Switch the serial line to using the *n_gsm* line discipline by using
   ``TIOCSETD`` ioctl.

#. Configure the mux using ``GSMIOC_GETCONF_EXT``/``GSMIOC_SETCONF_EXT``
   ioctl if needed.

#. Configure the mux using ``GSMIOC_GETCONF``/``GSMIOC_SETCONF`` ioctl.

#. Configure DLCs using ``GSMIOC_GETCONF_DLCI``/``GSMIOC_SETCONF_DLCI`` ioctl for non-defaults.

#. Obtain base gsmtty number for the used serial port::

        #include <stdio.h>
        #include <stdint.h>
        #include <linux/gsmmux.h>
        #include <linux/tty.h>
        #define DEFAULT_SPEED	B115200
        #define SERIAL_PORT	/dev/ttyS0

	int ldisc = N_GSM0710;
	struct gsm_config c;
	struct gsm_config_ext ce;
	struct gsm_dlci_config dc;
	struct termios configuration;
	uint32_t first;

	/* open the serial port */
	fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY | O_NDELAY);

	/* configure the serial port : speed, flow control ... */

	/* get serial data and check "AT+CMUX=command" parameter ... */

	/* use n_gsm line discipline */
	ioctl(fd, TIOCSETD, &ldisc);

	/* get n_gsm extended configuration */
	ioctl(fd, GSMIOC_GETCONF_EXT, &ce);
	/* use keep-alive once every 5s for peer connection supervision */
	ce.keep_alive = 500;
	/* set the new extended configuration */
	ioctl(fd, GSMIOC_SETCONF_EXT, &ce);
	/* get n_gsm configuration */
	ioctl(fd, GSMIOC_GETCONF, &c);
	/* we are requester and need encoding 0 (basic) */
	c.initiator = 0;
	c.encapsulation = 0;
	/* our modem defaults to a maximum size of 127 bytes */
	c.mru = 127;
	c.mtu = 127;
	/* set the new configuration */
	ioctl(fd, GSMIOC_SETCONF, &c);
	/* get DLC 1 configuration */
	dc.channel = 1;
	ioctl(fd, GSMIOC_GETCONF_DLCI, &dc);
	/* the first user channel gets a higher priority */
	dc.priority = 1;
	/* set the new DLC 1 specific configuration */
	ioctl(fd, GSMIOC_SETCONF_DLCI, &dc);
	/* get first gsmtty device node */
	ioctl(fd, GSMIOC_GETFIRST, &first);
	printf("first muxed line: /dev/gsmtty%i\n", first);

	/* and wait for ever to keep the line discipline enabled */
	daemon(0,0);
	pause();

11-03-08 - Eric Bénard - <eric@eukrea.com>
