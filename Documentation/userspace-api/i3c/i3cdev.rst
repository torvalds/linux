====================
I3C Device Interface
====================

I3C devices have the flexibility of being accessed from userspace, as well
through the conventional use of kernel drivers. Userspace access, although
limited to private SDR I3C transfers, provides the advantage of simplifying
the implementation of straightforward communication protocols, applicable to
scenarios where transfers are dedicated such for sensor bring-up scenarios
(prototyping environments) or for microcontroller slave communication
implementation.

The major device number is dynamically attributed and it's all reserved for
the i3c devices. By default, the i3cdev module only exposes the i3c devices
without device driver bind and aren't of master type in sort of character
device file under /dev/bus/i3c/ folder. They are identified through its
<bus id>-<Provisional ID> same way they can be found in /sys/bus/i3c/devices/.
::

# ls -l /dev/bus/i3c/
total 0
crw-------    1 root     root      248,   0 Jan  1 00:22 0-6072303904d2
crw-------    1 root     root      248,   1 Jan  1 00:22 0-b7405ba00929

The simplest way to use this interface is to not have an I3C device bound to
a kernel driver, this can be achieved by not have the kernel driver loaded or
using the Sysfs to unbind the kernel driver from the device.

BASIC CHARACTER DEVICE API
===============================
For now, the API has only support private SDR read and write transfers.
Those transaction can be achieved by the following:

``read(file, buffer, sizeof(buffer))``
  The standard read() operation will work as a simple transaction of private
  SDR read data followed a stop.
  Return the number of bytes read on success, and a negative error otherwise.

``write(file, buffer, sizeof(buffer))``
  The standard write() operation will work as a simple transaction of private
  SDR write data followed a stop.
  Return the number of bytes written on success, and a negative error otherwise.

``ioctl(file, I3C_IOC_PRIV_XFER(nxfers), struct i3c_ioc_priv_xfer *xfers)``
  It combines read/write transactions without a stop in between.
  Return 0 on success, and a negative error otherwise.

NOTES:
  - According to the MIPI I3C Protocol is the I3C slave that terminates the read
    transaction otherwise Master can abort early on ninth (T) data bit of each
    SDR data word.

  - Normal open() and close() operations on /dev/bus/i3c/<bus>-<provisional id>
    files work as you would expect.

  - As documented in cdev_del() if a device was already open during
    i3cdev_detach, the read(), write() and ioctl() fops will still be callable
    yet they will return -EACCES.

C EXAMPLE
=========
Working with I3C devices is much like working with files. You will need to open
a file descriptor, do some I/O operations with it, and then close it.

The following header files should be included in an I3C program::

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/i3c/i3cdev.h>

To work with an I3C device, the application must open the driver, made
available at the device node::

  int file;

  file = open("/dev/bus/i3c/0-6072303904d2", O_RDWR);
  if (file < 0)
  exit(1);

Now the file is opened, we can perform the operations available::

  /* Write function */
  uint_t8  buf[] = {0x00, 0xde, 0xad, 0xbe, 0xef}
  if (write(file, buf, 5) != 5) {
    /* ERROR HANDLING: I3C transaction failed */
  }

  /*  Read function */
  ret = read(file, buf, 5);
  If (ret < 0) {
    /* ERROR HANDLING: I3C transaction failed */
  } else {
    /* Iterate over buf[] to get the read data */
  }

  /* IOCTL function */
  struct i3c_ioc_priv_xfer xfers[2];

  uint8_t tx_buf[] = {0x00, 0xde, 0xad, 0xbe, 0xef};
  uint8_t rx_buf[10];

  xfers[0].data = (uintptr_t)tx_buf;
  xfers[0].len = 5;
  xfers[0].rnw = 0;
  xfers[1].data = (uintptr_t)rx_buf;
  xfers[1].len = 10;
  xfers[1].rnw = 1;

  if (ioctl(file, I3C_IOC_PRIV_XFER(2), xfers) < 0)
    /* ERROR HANDLING: I3C transaction failed */

The device can be closed when the open file descriptor is no longer required::

  close(file);