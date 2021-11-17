Crystal SoundFusion CS4610/CS4612/CS461 joystick
================================================

This is a new low-level driver to support analog joystick attached to
Crystal SoundFusion CS4610/CS4612/CS4615. This code is based upon
Vortex/Solo drivers as an example of decoration style, and ALSA
0.5.8a kernel drivers as an chipset documentation and samples.

This version does not have cooked mode support; the basic code
is present here, but have not tested completely. The button analysis
is completed in this mode, but the axis movement is not.

Raw mode works fine with analog joystick front-end driver and cs461x
driver as a backend. I've tested this driver with CS4610, 4-axis and
4-button joystick; I mean the jstest utility. Also I've tried to
play in xracer game using joystick, and the result is better than
keyboard only mode.

The sensitivity and calibrate quality have not been tested; the two
reasons are performed: the same hardware cannot work under Win95 (blue
screen in VJOYD); I have no documentation on my chip; and the existing
behavior in my case was not raised the requirement of joystick calibration.
So the driver have no code to perform hardware related calibration.

This driver have the basic support for PCI devices only; there is no
ISA or PnP ISA cards supported.

The driver works with ALSA drivers simultaneously. For example, the xracer
uses joystick as input device and PCM device as sound output in one time.
There are no sound or input collisions detected. The source code have
comments about them; but I've found the joystick can be initialized
separately of ALSA modules. So, you can use only one joystick driver
without ALSA drivers. The ALSA drivers are not needed to compile or
run this driver.

There are no debug information print have been placed in source, and no
specific options required to work this driver. The found chipset parameters
are printed via printk(KERN_INFO "..."), see the /var/log/messages to
inspect cs461x: prefixed messages to determine possible card detection
errors.

Regards,
Viktor
