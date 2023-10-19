===============================
Industrial IIO configfs support
===============================

1. Overview
===========

Configfs is a filesystem-based manager of kernel objects. IIO uses some
objects that could be easily configured using configfs (e.g.: devices,
triggers).

See Documentation/filesystems/configfs.rst for more information
about how configfs works.

2. Usage
========

In order to use configfs support in IIO we need to select it at compile
time via CONFIG_IIO_CONFIGFS config option.

Then, mount the configfs filesystem (usually under /config directory)::

  $ mkdir /config
  $ mount -t configfs none /config

At this point, all default IIO groups will be created and can be accessed
under /config/iio. Next chapters will describe available IIO configuration
objects.

3. Software triggers
====================

One of the IIO default configfs groups is the "triggers" group. It is
automagically accessible when the configfs is mounted and can be found
under /config/iio/triggers.

IIO software triggers implementation offers support for creating multiple
trigger types. A new trigger type is usually implemented as a separate
kernel module following the interface in include/linux/iio/sw_trigger.h::

  /*
   * drivers/iio/trigger/iio-trig-sample.c
   * sample kernel module implementing a new trigger type
   */
  #include <linux/iio/sw_trigger.h>


  static struct iio_sw_trigger *iio_trig_sample_probe(const char *name)
  {
	/*
	 * This allocates and registers an IIO trigger plus other
	 * trigger type specific initialization.
	 */
  }

  static int iio_trig_sample_remove(struct iio_sw_trigger *swt)
  {
	/*
	 * This undoes the actions in iio_trig_sample_probe
	 */
  }

  static const struct iio_sw_trigger_ops iio_trig_sample_ops = {
	.probe		= iio_trig_sample_probe,
	.remove		= iio_trig_sample_remove,
  };

  static struct iio_sw_trigger_type iio_trig_sample = {
	.name = "trig-sample",
	.owner = THIS_MODULE,
	.ops = &iio_trig_sample_ops,
  };

  module_iio_sw_trigger_driver(iio_trig_sample);

Each trigger type has its own directory under /config/iio/triggers. Loading
iio-trig-sample module will create 'trig-sample' trigger type directory
/config/iio/triggers/trig-sample.

We support the following interrupt sources (trigger types):

	* hrtimer, uses high resolution timers as interrupt source

3.1 Hrtimer triggers creation and destruction
---------------------------------------------

Loading iio-trig-hrtimer module will register hrtimer trigger types allowing
users to create hrtimer triggers under /config/iio/triggers/hrtimer.

e.g::

  $ mkdir /config/iio/triggers/hrtimer/instance1
  $ rmdir /config/iio/triggers/hrtimer/instance1

Each trigger can have one or more attributes specific to the trigger type.

3.2 "hrtimer" trigger types attributes
--------------------------------------

"hrtimer" trigger type doesn't have any configurable attribute from /config dir.
It does introduce the sampling_frequency attribute to trigger directory.
That attribute sets the polling frequency in Hz, with mHz precision.
