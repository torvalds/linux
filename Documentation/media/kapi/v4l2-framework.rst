Overview of the V4L2 driver framework
=====================================

This text documents the various structures provided by the V4L2 framework and
their relationships.


Introduction
------------

The V4L2 drivers tend to be very complex due to the complexity of the
hardware: most devices have multiple ICs, export multiple device nodes in
/dev, and create also non-V4L2 devices such as DVB, ALSA, FB, I2C and input
(IR) devices.

Especially the fact that V4L2 drivers have to setup supporting ICs to
do audio/video muxing/encoding/decoding makes it more complex than most.
Usually these ICs are connected to the main bridge driver through one or
more I2C busses, but other busses can also be used. Such devices are
called 'sub-devices'.

For a long time the framework was limited to the video_device struct for
creating V4L device nodes and video_buf for handling the video buffers
(note that this document does not discuss the video_buf framework).

This meant that all drivers had to do the setup of device instances and
connecting to sub-devices themselves. Some of this is quite complicated
to do right and many drivers never did do it correctly.

There is also a lot of common code that could never be refactored due to
the lack of a framework.

So this framework sets up the basic building blocks that all drivers
need and this same framework should make it much easier to refactor
common code into utility functions shared by all drivers.

A good example to look at as a reference is the v4l2-pci-skeleton.c
source that is available in samples/v4l/. It is a skeleton driver for
a PCI capture card, and demonstrates how to use the V4L2 driver
framework. It can be used as a template for real PCI video capture driver.

Structure of a driver
---------------------

All drivers have the following structure:

1) A struct for each device instance containing the device state.

2) A way of initializing and commanding sub-devices (if any).

3) Creating V4L2 device nodes (/dev/videoX, /dev/vbiX and /dev/radioX)
   and keeping track of device-node specific data.

4) Filehandle-specific structs containing per-filehandle data;

5) video buffer handling.

This is a rough schematic of how it all relates:

.. code-block:: none

    device instances
      |
      +-sub-device instances
      |
      \-V4L2 device nodes
	  |
	  \-filehandle instances


Structure of the framework
--------------------------

The framework closely resembles the driver structure: it has a v4l2_device
struct for the device instance data, a v4l2_subdev struct to refer to
sub-device instances, the video_device struct stores V4L2 device node data
and the v4l2_fh struct keeps track of filehandle instances.

The V4L2 framework also optionally integrates with the media framework. If a
driver sets the struct v4l2_device mdev field, sub-devices and video nodes
will automatically appear in the media framework as entities.

struct v4l2_fh
--------------

struct v4l2_fh provides a way to easily keep file handle specific data
that is used by the V4L2 framework. New drivers must use struct v4l2_fh
since it is also used to implement priority handling (VIDIOC_G/S_PRIORITY).

The users of v4l2_fh (in the V4L2 framework, not the driver) know
whether a driver uses v4l2_fh as its file->private_data pointer by
testing the V4L2_FL_USES_V4L2_FH bit in video_device->flags. This bit is
set whenever v4l2_fh_init() is called.

struct v4l2_fh is allocated as a part of the driver's own file handle
structure and file->private_data is set to it in the driver's open
function by the driver.

In many cases the struct v4l2_fh will be embedded in a larger structure.
In that case you should call v4l2_fh_init+v4l2_fh_add in open() and
v4l2_fh_del+v4l2_fh_exit in release().

Drivers can extract their own file handle structure by using the container_of
macro. Example:

.. code-block:: none

	struct my_fh {
		int blah;
		struct v4l2_fh fh;
	};

	...

	int my_open(struct file *file)
	{
		struct my_fh *my_fh;
		struct video_device *vfd;
		int ret;

		...

		my_fh = kzalloc(sizeof(*my_fh), GFP_KERNEL);

		...

		v4l2_fh_init(&my_fh->fh, vfd);

		...

		file->private_data = &my_fh->fh;
		v4l2_fh_add(&my_fh->fh);
		return 0;
	}

	int my_release(struct file *file)
	{
		struct v4l2_fh *fh = file->private_data;
		struct my_fh *my_fh = container_of(fh, struct my_fh, fh);

		...
		v4l2_fh_del(&my_fh->fh);
		v4l2_fh_exit(&my_fh->fh);
		kfree(my_fh);
		return 0;
	}

Below is a short description of the v4l2_fh functions used:

.. code-block:: none

	void v4l2_fh_init(struct v4l2_fh *fh, struct video_device *vdev)

  Initialise the file handle. This *MUST* be performed in the driver's
  v4l2_file_operations->open() handler.

.. code-block:: none

	void v4l2_fh_add(struct v4l2_fh *fh)

  Add a v4l2_fh to video_device file handle list. Must be called once the
  file handle is completely initialized.

.. code-block:: none

	void v4l2_fh_del(struct v4l2_fh *fh)

  Unassociate the file handle from video_device(). The file handle
  exit function may now be called.

.. code-block:: none

	void v4l2_fh_exit(struct v4l2_fh *fh)

  Uninitialise the file handle. After uninitialisation the v4l2_fh
  memory can be freed.


If struct v4l2_fh is not embedded, then you can use these helper functions:

.. code-block:: none

	int v4l2_fh_open(struct file *filp)

  This allocates a struct v4l2_fh, initializes it and adds it to the struct
  video_device associated with the file struct.

.. code-block:: none

	int v4l2_fh_release(struct file *filp)

  This deletes it from the struct video_device associated with the file
  struct, uninitialised the v4l2_fh and frees it.

These two functions can be plugged into the v4l2_file_operation's open() and
release() ops.


Several drivers need to do something when the first file handle is opened and
when the last file handle closes. Two helper functions were added to check
whether the v4l2_fh struct is the only open filehandle of the associated
device node:

.. code-block:: none

	int v4l2_fh_is_singular(struct v4l2_fh *fh)

  Returns 1 if the file handle is the only open file handle, else 0.

.. code-block:: none

	int v4l2_fh_is_singular_file(struct file *filp)

  Same, but it calls v4l2_fh_is_singular with filp->private_data.

V4L2 clocks
-----------

Many subdevices, like camera sensors, TV decoders and encoders, need a clock
signal to be supplied by the system. Often this clock is supplied by the
respective bridge device. The Linux kernel provides a Common Clock Framework for
this purpose. However, it is not (yet) available on all architectures. Besides,
the nature of the multi-functional (clock, data + synchronisation, I2C control)
connection of subdevices to the system might impose special requirements on the
clock API usage. E.g. V4L2 has to support clock provider driver unregistration
while a subdevice driver is holding a reference to the clock. For these reasons
a V4L2 clock helper API has been developed and is provided to bridge and
subdevice drivers.

The API consists of two parts: two functions to register and unregister a V4L2
clock source: v4l2_clk_register() and v4l2_clk_unregister() and calls to control
a clock object, similar to the respective generic clock API calls:
v4l2_clk_get(), v4l2_clk_put(), v4l2_clk_enable(), v4l2_clk_disable(),
v4l2_clk_get_rate(), and v4l2_clk_set_rate(). Clock suppliers have to provide
clock operations that will be called when clock users invoke respective API
methods.

It is expected that once the CCF becomes available on all relevant
architectures this API will be removed.
