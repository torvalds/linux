The Soc-Camera Drivers
======================

Author: Guennadi Liakhovetski <g.liakhovetski@gmx.de>

Terminology
-----------

The following terms are used in this document:
 - camera / camera device / camera sensor - a video-camera sensor chip, capable
   of connecting to a variety of systems and interfaces, typically uses i2c for
   control and configuration, and a parallel or a serial bus for data.
 - camera host - an interface, to which a camera is connected. Typically a
   specialised interface, present on many SoCs, e.g. PXA27x and PXA3xx, SuperH,
   AVR32, i.MX27, i.MX31.
 - camera host bus - a connection between a camera host and a camera. Can be
   parallel or serial, consists of data and control lines, e.g. clock, vertical
   and horizontal synchronization signals.

Purpose of the soc-camera subsystem
-----------------------------------

The soc-camera subsystem initially provided a unified API between camera host
drivers and camera sensor drivers. Later the soc-camera sensor API has been
replaced with the V4L2 standard subdev API. This also made camera driver re-use
with non-soc-camera hosts possible. The camera host API to the soc-camera core
has been preserved.

Soc-camera implements a V4L2 interface to the user, currently only the "mmap"
method is supported by host drivers. However, the soc-camera core also provides
support for the "read" method.

The subsystem has been designed to support multiple camera host interfaces and
multiple cameras per interface, although most applications have only one camera
sensor.

Existing drivers
----------------

As of 3.7 there are seven host drivers in the mainline: atmel-isi.c,
mx1_camera.c (broken, scheduled for removal), mx2_camera.c, mx3_camera.c,
omap1_camera.c, pxa_camera.c, sh_mobile_ceu_camera.c, and multiple sensor
drivers under drivers/media/i2c/soc_camera/.

Camera host API
---------------

A host camera driver is registered using the

.. code-block:: none

	soc_camera_host_register(struct soc_camera_host *);

function. The host object can be initialized as follows:

.. code-block:: none

	struct soc_camera_host	*ici;
	ici->drv_name		= DRV_NAME;
	ici->ops		= &camera_host_ops;
	ici->priv		= pcdev;
	ici->v4l2_dev.dev	= &pdev->dev;
	ici->nr			= pdev->id;

All camera host methods are passed in a struct soc_camera_host_ops:

.. code-block:: none

	static struct soc_camera_host_ops camera_host_ops = {
		.owner		= THIS_MODULE,
		.add		= camera_add_device,
		.remove		= camera_remove_device,
		.set_fmt	= camera_set_fmt_cap,
		.try_fmt	= camera_try_fmt_cap,
		.init_videobuf2	= camera_init_videobuf2,
		.poll		= camera_poll,
		.querycap	= camera_querycap,
		.set_bus_param	= camera_set_bus_param,
		/* The rest of host operations are optional */
	};

.add and .remove methods are called when a sensor is attached to or detached
from the host. .set_bus_param is used to configure physical connection
parameters between the host and the sensor. .init_videobuf2 is called by
soc-camera core when a video-device is opened, the host driver would typically
call vb2_queue_init() in this method. Further video-buffer management is
implemented completely by the specific camera host driver. If the host driver
supports non-standard pixel format conversion, it should implement a
.get_formats and, possibly, a .put_formats operations. See below for more
details about format conversion. The rest of the methods are called from
respective V4L2 operations.

Camera API
----------

Sensor drivers can use struct soc_camera_link, typically provided by the
platform, and used to specify to which camera host bus the sensor is connected,
and optionally provide platform .power and .reset methods for the camera. This
struct is provided to the camera driver via the I2C client device platform data
and can be obtained, using the soc_camera_i2c_to_link() macro. Care should be
taken, when using soc_camera_vdev_to_subdev() and when accessing struct
soc_camera_device, using v4l2_get_subdev_hostdata(): both only work, when
running on an soc-camera host. The actual camera driver operation is implemented
using the V4L2 subdev API. Additionally soc-camera camera drivers can use
auxiliary soc-camera helper functions like soc_camera_power_on() and
soc_camera_power_off(), which switch regulators, provided by the platform and call
board-specific power switching methods. soc_camera_apply_board_flags() takes
camera bus configuration capability flags and applies any board transformations,
e.g. signal polarity inversion. soc_mbus_get_fmtdesc() can be used to obtain a
pixel format descriptor, corresponding to a certain media-bus pixel format code.
soc_camera_limit_side() can be used to restrict beginning and length of a frame
side, based on camera capabilities.

VIDIOC_S_CROP and VIDIOC_S_FMT behaviour
----------------------------------------

Above user ioctls modify image geometry as follows:

VIDIOC_S_CROP: sets location and sizes of the sensor window. Unit is one sensor
pixel. Changing sensor window sizes preserves any scaling factors, therefore
user window sizes change as well.

VIDIOC_S_FMT: sets user window. Should preserve previously set sensor window as
much as possible by modifying scaling factors. If the sensor window cannot be
preserved precisely, it may be changed too.

In soc-camera there are two locations, where scaling and cropping can take
place: in the camera driver and in the host driver. User ioctls are first passed
to the host driver, which then generally passes them down to the camera driver.
It is more efficient to perform scaling and cropping in the camera driver to
save camera bus bandwidth and maximise the framerate. However, if the camera
driver failed to set the required parameters with sufficient precision, the host
driver may decide to also use its own scaling and cropping to fulfill the user's
request.

Camera drivers are interfaced to the soc-camera core and to host drivers over
the v4l2-subdev API, which is completely functional, it doesn't pass any data.
Therefore all camera drivers shall reply to .g_fmt() requests with their current
output geometry. This is necessary to correctly configure the camera bus.
.s_fmt() and .try_fmt() have to be implemented too. Sensor window and scaling
factors have to be maintained by camera drivers internally. According to the
V4L2 API all capture drivers must support the VIDIOC_CROPCAP ioctl, hence we
rely on camera drivers implementing .cropcap(). If the camera driver does not
support cropping, it may choose to not implement .s_crop(), but to enable
cropping support by the camera host driver at least the .g_crop method must be
implemented.

User window geometry is kept in .user_width and .user_height fields in struct
soc_camera_device and used by the soc-camera core and host drivers. The core
updates these fields upon successful completion of a .s_fmt() call, but if these
fields change elsewhere, e.g. during .s_crop() processing, the host driver is
responsible for updating them.

Format conversion
-----------------

V4L2 distinguishes between pixel formats, as they are stored in memory, and as
they are transferred over a media bus. Soc-camera provides support to
conveniently manage these formats. A table of standard transformations is
maintained by soc-camera core, which describes, what FOURCC pixel format will
be obtained, if a media-bus pixel format is stored in memory according to
certain rules. E.g. if MEDIA_BUS_FMT_YUYV8_2X8 data is sampled with 8 bits per
sample and stored in memory in the little-endian order with no gaps between
bytes, data in memory will represent the V4L2_PIX_FMT_YUYV FOURCC format. These
standard transformations will be used by soc-camera or by camera host drivers to
configure camera drivers to produce the FOURCC format, requested by the user,
using the VIDIOC_S_FMT ioctl(). Apart from those standard format conversions,
host drivers can also provide their own conversion rules by implementing a
.get_formats and, if required, a .put_formats methods.
