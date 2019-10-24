=================
What is sa1100fb?
=================

.. [This file is cloned from VesaFB/matroxfb]


This is a driver for a graphic framebuffer for the SA-1100 LCD
controller.

Configuration
==============

For most common passive displays, giving the option::

  video=sa1100fb:bpp:<value>,lccr0:<value>,lccr1:<value>,lccr2:<value>,lccr3:<value>

on the kernel command line should be enough to configure the
controller. The bits per pixel (bpp) value should be 4, 8, 12, or
16. LCCR values are display-specific and should be computed as
documented in the SA-1100 Developer's Manual, Section 11.7. Dual-panel
displays are supported as long as the SDS bit is set in LCCR0; GPIO<9:2>
are used for the lower panel.

For active displays or displays requiring additional configuration
(controlling backlights, powering on the LCD, etc.), the command line
options may not be enough to configure the display. Adding sections to
sa1100fb_init_fbinfo(), sa1100fb_activate_var(),
sa1100fb_disable_lcd_controller(), and sa1100fb_enable_lcd_controller()
will probably be necessary.

Accepted options::

	bpp:<value>	Configure for <value> bits per pixel
	lccr0:<value>	Configure LCD control register 0 (11.7.3)
	lccr1:<value>	Configure LCD control register 1 (11.7.4)
	lccr2:<value>	Configure LCD control register 2 (11.7.5)
	lccr3:<value>	Configure LCD control register 3 (11.7.6)

Mark Huang <mhuang@livetoy.com>
