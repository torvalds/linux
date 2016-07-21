Video device creation
=====================

The actual device nodes in the /dev directory are created using the
video_device struct (v4l2-dev.h). This struct can either be allocated
dynamically or embedded in a larger struct.

To allocate it dynamically use:

.. code-block:: none

	struct video_device *vdev = video_device_alloc();

	if (vdev == NULL)
		return -ENOMEM;

	vdev->release = video_device_release;

If you embed it in a larger struct, then you must set the release()
callback to your own function:

.. code-block:: none

	struct video_device *vdev = &my_vdev->vdev;

	vdev->release = my_vdev_release;

The release callback must be set and it is called when the last user
of the video device exits.

The default video_device_release() callback just calls kfree to free the
allocated memory.

There is also a video_device_release_empty() function that does nothing
(is empty) and can be used if the struct is embedded and there is nothing
to do when it is released.

You should also set these fields:

- v4l2_dev: must be set to the v4l2_device parent device.

- name: set to something descriptive and unique.

- vfl_dir: set this to VFL_DIR_RX for capture devices (VFL_DIR_RX has value 0,
  so this is normally already the default), set to VFL_DIR_TX for output
  devices and VFL_DIR_M2M for mem2mem (codec) devices.

- fops: set to the v4l2_file_operations struct.

- ioctl_ops: if you use the v4l2_ioctl_ops to simplify ioctl maintenance
  (highly recommended to use this and it might become compulsory in the
  future!), then set this to your v4l2_ioctl_ops struct. The vfl_type and
  vfl_dir fields are used to disable ops that do not match the type/dir
  combination. E.g. VBI ops are disabled for non-VBI nodes, and output ops
  are disabled for a capture device. This makes it possible to provide
  just one v4l2_ioctl_ops struct for both vbi and video nodes.

- lock: leave to NULL if you want to do all the locking in the driver.
  Otherwise you give it a pointer to a struct mutex_lock and before the
  unlocked_ioctl file operation is called this lock will be taken by the
  core and released afterwards. See the next section for more details.

- queue: a pointer to the struct vb2_queue associated with this device node.
  If queue is non-NULL, and queue->lock is non-NULL, then queue->lock is
  used for the queuing ioctls (VIDIOC_REQBUFS, CREATE_BUFS, QBUF, DQBUF,
  QUERYBUF, PREPARE_BUF, STREAMON and STREAMOFF) instead of the lock above.
  That way the vb2 queuing framework does not have to wait for other ioctls.
  This queue pointer is also used by the vb2 helper functions to check for
  queuing ownership (i.e. is the filehandle calling it allowed to do the
  operation).

- prio: keeps track of the priorities. Used to implement VIDIOC_G/S_PRIORITY.
  If left to NULL, then it will use the struct v4l2_prio_state in v4l2_device.
  If you want to have a separate priority state per (group of) device node(s),
  then you can point it to your own struct v4l2_prio_state.

- dev_parent: you only set this if v4l2_device was registered with NULL as
  the parent device struct. This only happens in cases where one hardware
  device has multiple PCI devices that all share the same v4l2_device core.

  The cx88 driver is an example of this: one core v4l2_device struct, but
  it is used by both a raw video PCI device (cx8800) and a MPEG PCI device
  (cx8802). Since the v4l2_device cannot be associated with two PCI devices
  at the same time it is setup without a parent device. But when the struct
  video_device is initialized you *do* know which parent PCI device to use and
  so you set dev_device to the correct PCI device.

If you use v4l2_ioctl_ops, then you should set .unlocked_ioctl to video_ioctl2
in your v4l2_file_operations struct.

Do not use .ioctl! This is deprecated and will go away in the future.

In some cases you want to tell the core that a function you had specified in
your v4l2_ioctl_ops should be ignored. You can mark such ioctls by calling this
function before video_device_register is called:

.. code-block:: none

	void v4l2_disable_ioctl(struct video_device *vdev, unsigned int cmd);

This tends to be needed if based on external factors (e.g. which card is
being used) you want to turns off certain features in v4l2_ioctl_ops without
having to make a new struct.

The v4l2_file_operations struct is a subset of file_operations. The main
difference is that the inode argument is omitted since it is never used.

If integration with the media framework is needed, you must initialize the
media_entity struct embedded in the video_device struct (entity field) by
calling media_entity_pads_init():

.. code-block:: none

	struct media_pad *pad = &my_vdev->pad;
	int err;

	err = media_entity_pads_init(&vdev->entity, 1, pad);

The pads array must have been previously initialized. There is no need to
manually set the struct media_entity type and name fields.

A reference to the entity will be automatically acquired/released when the
video device is opened/closed.

ioctls and locking
------------------

The V4L core provides optional locking services. The main service is the
lock field in struct video_device, which is a pointer to a mutex. If you set
this pointer, then that will be used by unlocked_ioctl to serialize all ioctls.

If you are using the videobuf2 framework, then there is a second lock that you
can set: video_device->queue->lock. If set, then this lock will be used instead
of video_device->lock to serialize all queuing ioctls (see the previous section
for the full list of those ioctls).

The advantage of using a different lock for the queuing ioctls is that for some
drivers (particularly USB drivers) certain commands such as setting controls
can take a long time, so you want to use a separate lock for the buffer queuing
ioctls. That way your VIDIOC_DQBUF doesn't stall because the driver is busy
changing the e.g. exposure of the webcam.

Of course, you can always do all the locking yourself by leaving both lock
pointers at NULL.

If you use the old videobuf then you must pass the video_device lock to the
videobuf queue initialize function: if videobuf has to wait for a frame to
arrive, then it will temporarily unlock the lock and relock it afterwards. If
your driver also waits in the code, then you should do the same to allow other
processes to access the device node while the first process is waiting for
something.

In the case of videobuf2 you will need to implement the wait_prepare and
wait_finish callbacks to unlock/lock if applicable. If you use the queue->lock
pointer, then you can use the helper functions vb2_ops_wait_prepare/finish.

The implementation of a hotplug disconnect should also take the lock from
video_device before calling v4l2_device_disconnect. If you are also using
video_device->queue->lock, then you have to first lock video_device->queue->lock
followed by video_device->lock. That way you can be sure no ioctl is running
when you call v4l2_device_disconnect.

video_device registration
-------------------------

Next you register the video device: this will create the character device
for you.

.. code-block:: none

	err = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (err) {
		video_device_release(vdev); /* or kfree(my_vdev); */
		return err;
	}

If the v4l2_device parent device has a non-NULL mdev field, the video device
entity will be automatically registered with the media device.

Which device is registered depends on the type argument. The following
types exist:

VFL_TYPE_GRABBER: videoX for video input/output devices
VFL_TYPE_VBI: vbiX for vertical blank data (i.e. closed captions, teletext)
VFL_TYPE_RADIO: radioX for radio tuners
VFL_TYPE_SDR: swradioX for Software Defined Radio tuners

The last argument gives you a certain amount of control over the device
device node number used (i.e. the X in videoX). Normally you will pass -1
to let the v4l2 framework pick the first free number. But sometimes users
want to select a specific node number. It is common that drivers allow
the user to select a specific device node number through a driver module
option. That number is then passed to this function and video_register_device
will attempt to select that device node number. If that number was already
in use, then the next free device node number will be selected and it
will send a warning to the kernel log.

Another use-case is if a driver creates many devices. In that case it can
be useful to place different video devices in separate ranges. For example,
video capture devices start at 0, video output devices start at 16.
So you can use the last argument to specify a minimum device node number
and the v4l2 framework will try to pick the first free number that is equal
or higher to what you passed. If that fails, then it will just pick the
first free number.

Since in this case you do not care about a warning about not being able
to select the specified device node number, you can call the function
video_register_device_no_warn() instead.

Whenever a device node is created some attributes are also created for you.
If you look in /sys/class/video4linux you see the devices. Go into e.g.
video0 and you will see 'name', 'dev_debug' and 'index' attributes. The 'name'
attribute is the 'name' field of the video_device struct. The 'dev_debug' attribute
can be used to enable core debugging. See the next section for more detailed
information on this.

The 'index' attribute is the index of the device node: for each call to
video_register_device() the index is just increased by 1. The first video
device node you register always starts with index 0.

Users can setup udev rules that utilize the index attribute to make fancy
device names (e.g. 'mpegX' for MPEG video capture device nodes).

After the device was successfully registered, then you can use these fields:

- vfl_type: the device type passed to video_register_device.
- minor: the assigned device minor number.
- num: the device node number (i.e. the X in videoX).
- index: the device index number.

If the registration failed, then you need to call video_device_release()
to free the allocated video_device struct, or free your own struct if the
video_device was embedded in it. The vdev->release() callback will never
be called if the registration failed, nor should you ever attempt to
unregister the device if the registration failed.

video device debugging
----------------------

The 'dev_debug' attribute that is created for each video, vbi, radio or swradio
device in /sys/class/video4linux/<devX>/ allows you to enable logging of
file operations.

It is a bitmask and the following bits can be set:

.. code-block:: none

	0x01: Log the ioctl name and error code. VIDIOC_(D)QBUF ioctls are only logged
	      if bit 0x08 is also set.
	0x02: Log the ioctl name arguments and error code. VIDIOC_(D)QBUF ioctls are
	      only logged if bit 0x08 is also set.
	0x04: Log the file operations open, release, read, write, mmap and
	      get_unmapped_area. The read and write operations are only logged if
	      bit 0x08 is also set.
	0x08: Log the read and write file operations and the VIDIOC_QBUF and
	      VIDIOC_DQBUF ioctls.
	0x10: Log the poll file operation.

video_device cleanup
--------------------

When the video device nodes have to be removed, either during the unload
of the driver or because the USB device was disconnected, then you should
unregister them:

.. code-block:: none

	video_unregister_device(vdev);

This will remove the device nodes from sysfs (causing udev to remove them
from /dev).

After video_unregister_device() returns no new opens can be done. However,
in the case of USB devices some application might still have one of these
device nodes open. So after the unregister all file operations (except
release, of course) will return an error as well.

When the last user of the video device node exits, then the vdev->release()
callback is called and you can do the final cleanup there.

Don't forget to cleanup the media entity associated with the video device if
it has been initialized:

.. code-block:: none

	media_entity_cleanup(&vdev->entity);

This can be done from the release callback.


video_device helper functions
-----------------------------

There are a few useful helper functions:

- file/video_device private data

You can set/get driver private data in the video_device struct using:

.. code-block:: none

	void *video_get_drvdata(struct video_device *vdev);
	void video_set_drvdata(struct video_device *vdev, void *data);

Note that you can safely call video_set_drvdata() before calling
video_register_device().

And this function:

.. code-block:: none

	struct video_device *video_devdata(struct file *file);

returns the video_device belonging to the file struct.

The video_drvdata function combines video_get_drvdata with video_devdata:

.. code-block:: none

	void *video_drvdata(struct file *file);

You can go from a video_device struct to the v4l2_device struct using:

.. code-block:: none

	struct v4l2_device *v4l2_dev = vdev->v4l2_dev;

- Device node name

The video_device node kernel name can be retrieved using

.. code-block:: none

	const char *video_device_node_name(struct video_device *vdev);

The name is used as a hint by userspace tools such as udev. The function
should be used where possible instead of accessing the video_device::num and
video_device::minor fields.

video_device kAPI
-----------------

.. kernel-doc:: include/media/v4l2-dev.h
