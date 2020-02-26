==================
Linux Device Model
==================

Overview
========

Plug and Play is a technology that offers support for automatically adding and
removing devices to your system. This reduces conflicts with the resources they
use by automatically configuring them at system startup. In order to achieve
these goals, the following features are required:

  * Automatic detection of adding and removing devices in the system (the  device
    and its bus must notify the appropriate driver that a configuration change
    occurred).
  * Resource management (addresses, irq lines, DMA channels, memory areas),
    including resource allocation to devices and solving conflicts that may arise.
  * Devices must allow for software configuration (device resources - ports,
    interrupts, DMA resources - must allow for driver assignment).
  * The drivers required for new devices must be loaded automatically by the
    operating system when needed.
  * When the device and its bus allow, the system should be able to add or
    remove the device from the system while it is running, without having to reboot
    the system ( hotplug ).

For a system to support plug and play, the BIOS , operating system and device
must support this technology. The device must have an ID that will provide the
driver for identification, and the operating system must be able to identify
these configuration changes as they appear.

Plug and play devices are: PCI devices (network cards), USB (keyboard, mouse,
printer), etc.

Prior to version 2.6, the kernel did not have a unified model to get
information about it. For this reason, a model for Linux devices, Linux Device
Model, was developed.

The primary purpose of this model is to maintain internal data structures that
reflect the state and structure of the system. Such information includes what
devices are in the system, how they are in terms of power management, what bus
they are attached to, what drivers they have, along with the structure of the
buses, devices, drivers in the system.

To maintain this information, the kernel uses the following entities:

  * device - a physical device that is attached to a bus
  * driver - a software entity that can be associated with a device and performs
    operations with it
  * bus - a device to which other devices can be attached
  * class - a type of device that has a similar behavior; There is a class for
    discs, partitions, serial ports, etc.
  * subsystem - a view on the structure of the system; Kernel subsystems
    include devices (hierarchical view of all devices in the system), buses (bus
    view of devices according to how they are attached to buses), classes, etc.


sysfs
=====

The kernel provides a representation of its model in userspace through the
sysfs virtual file system. It is usually mounted in the /sys directory and
contains the following subdirectories:

  * block - all block devices available in the system (disks, partitions)
  * bus - types of bus to which physical devices are connected (pci, ide, usb)
  * class - drivers classes that are available in the system (net, sound, usb)
  * devices - the hierarchical structure of devices connected to the system
  * firmware - information from system firmware (ACPI)
  * fs - information about mounted file systems
  * kernel - kernel status information (logged-in users, hotplug)
  * modules - the list of modules currently loaded
  * power - information related to the power management subsystem

As you can see, there is a correlation between the kernel data structures
within the described model and the subdirectories in the sysfs virtual file
system. Although this likeness may lead to confusion between the two concepts,
they are different. The kernel device model can work without the sysfs file
system, but the reciprocal is not true.

The sysfs information is found in files that contain an attribute. Some
standard attributes (represented by files or directories with the same name)
are as follows:

   * dev - Major and minor device identifier. It can be used to automatically
     create entries in the /dev directory
   * device - a symbolic link to the directory containing devices; It can be
     used to discover the hardware devices that provide a particular service (for
     example, the ethi PCI card)
   * driver - a symbolic link to the driver directory (located in
     /sys/bus/\*/drivers )

Other attributes are available, depending on the bus and driver used.

Basic Structures in Linux Devices
=================================

Linux Device Model provides a number of structures to ensure the interaction
between a hardware device and a device driver. The whole model is based on
kobject structure. With this structure, hierarchies are built and the following
structures are implemented:

  * structure bus_type
  * struct device
  * struct device_driver

The kobject structure
---------------------

A kobject structure does not perform a single function. Such a structure is
usually integrated into a larger structure. A kobject structure actually
incorporates a set of features that will be offered to a higher abstraction
object in the Linux Device Model hierarchy.

For example, the cdev structure has the following definition:

.. code-block:: c

  struct cdev {
	 struct kobject kobj ;
	 struct module * owner ;
	 const struct file_operations * ops ;
	 struct list_head list ;
	 dev_t dev ;
	 unsigned int count ;
 } ;


Note that this structure includes a kobject structure field.

A kobject structure structure is defined as follows:

.. code-block:: c

  struct kobject {
	 const char * name ;
	 struct list_head entry ;
	 struct kobject * parent ;
	 struct kset * kset ;
	 struct kobj_type * ktype ;
	 struct sysfs_dirent * sd ;
	 struct kref kref ;
	 unsigned int state_initialized : 1 ;
	 unsigned int state_in_sysfs : 1 ;
	 unsigned int state_add_uevent_sent : 1 ;
	 unsigned int state_remove_uevent_sent : 1 ;
	 unsigned int uevent_suppress : 1 ;
 };


As we can see, the kobject structures are in a hierarchy : an object has a
parent and holds a kset member, which contains objects on the same level.

Working with the structure involves initializing it with the kobject_init
function. Also in the initialization process it is necessary to establish the
name of the kobject structure, which will appear in sysfs, using the
kobject_set_name function.

Any operation on a kobject is done by incrementing its internal counter with
kobject_get, or decrementing if it is no longer used with kobject_put . Thus,
a kobject object will only be released when its internal counter reaches 0. A
method of notifying this is needed so that the resources associated with the
device structure are released Included kobject structure (for example, cdev ).
The method is called release and is associated with the object via the ktype
field (struct kobj_type).

The kobject structure structure is the basic structure of the Linux Device
Model. The structures in the higher levels of the model are struct bus_type ,
struct device and struct device_driver .

Buses
-----

A bus is a communication channel between the processor and an input / output
device. To ensure that the model is generic, all input / output devices are
connected to the processor via such a bus (even if it can be a virtual one
without a physical hardware correspondent).

When adding a system bus, it will appear in the sysfs file system in /sys/bus
As with kobjects, buses can be organized into hierarchies and will be represented
in sysfs.

In the Linux Device Model, a bus is represented by the struct bus_type:

.. code-block:: c

  struct bus_type {
	 const char *name;
	 const char *dev_name;
	 struct device *dev_root ;
	 struct bus_attribute *bus_attrs;
	 struct device_attribute *dev_attrs;
	 struct driver_attribute *drv_attrs;
	 structure subsys_private *p;

	 int (*match) (device structure *dev, struct device_driver *drv);
	 int (*uevent) (structure device *dev, struct kobj_uevent_env *env);
	 int (*probe) (struct device *dev);
	 int (*remove) (device structure * dev);
	 // ...
 };

It is noticed that a bus is associated with a name, lists of default
attributes, a number of specific functions, and the driver's private data. The
uevent function (formerly hotplug) is used with hotplug devices.

Bus operations are the registration operations, the implementation of the
operations described in the bus_type structure structure and the scrolling and
inspection operations of the devices connected to the bus.

Recording a bus is done using bus_register , and registering using bus_unregister.

Show example implementation

The functions that will normally be initialized within a bus_type structure are
match and uevent :

.. code-block:: c

  #include<linux/device.h>
  #include<linux/string.h>

  /* match devices to drivers;  Just do a simple name test */
  static int my_match (structure device *dev, struct device_driver *driver)
  {
     return !strncmp(dev_name(dev), driver->name, strlen(driver->name)) ;
  }

  /*  respond to hotplug user events;  Add environment variable DEV_NAME */
  static int my_uevent(struct device *dev, struct kobj_uevent_env *env)
  {
     add_uevent_var(env, "DEV_NAME =% s", dev_name(dev));
     return 0 ;
  }

The match function is used when a new device or a new driver is added to the
bus. Its role is to make a comparison between the device ID and the driver ID.
The uevent function is called before generating a hotplug in user-space and has
the role of adding environment variables.

Other possible operations on a bus are browsing the drivers or devices attached
to it. Although we can not directly access them (lists of drives and devices
being stored in the private data of the driver, the subsys_private * p field ),
these can be scanned using the bus_for_each_dev and bus_for_each_drv
macrodefines .

The Linux Device Model interface allows you to create attributes for the
associated objects. These attributes will have a corresponding file in the
subdirectory of the sysfs bus. The attributes associated with a bus are
described by the bus_attribute structure :

.. **
.. code-block:: c

  struct bus_attribute {
	  attribute attribute attr ;
	  ssize_t (*show) (struct bus_type *, char *buf);
	  ssize_t (*store) (struct bus_type *, const char *buf , size_t count);
  };

.. **

Typically, an attribute is defined by the BUS_ATTR macrodefine . To add /
delete an attribute within the bus structure, the bus_create_file and
bus_remove_file functions are used.

An example of defining an attribute for my_bus is shown below:

.. code-block:: c

   static ssize_t
   del_store(struct bus_type *bt, const char *buf, size_t count)
   {
	char name[32];
	int version;

	if (sscanf(buf, "%s", name) != 1)
		return -EINVAL;

	return bex_del_dev(name) ? 0 : count;

   }
   BUS_ATTR(del, S_IWUSR, NULL, del_store);

   static struct attribute *bex_bus_attrs[] = {
	&bus_attr_add.attr,
	&bus_attr_del.attr,
	NULL
   };
   ATTRIBUTE_GROUPS(bex_bus);

   struct bus_type bex_bus_type = {
       ...
       .bus_groups = bex_bus_groups,
   };


The bus is represented by both a bus_type object and a device object, as we
will see later (the bus is also a device).


Devices
-------

Any device in the system has a struct structure structure associated with it.
Devices are discovered by different kernel methods (hotplug, device drivers,
system initialization) and are recorded in the system. All devices present in
the kernel have an entry in /sys/devices .

At the bottom level, a device in Linux Device Model is a struct structure
device :

.. code-block:: c

   struct device {

	 struct device *parent ;
	 struct device_private *p;
	 struct kobject kobj;

	 const char *init_name;  /* Initial name of the device */

	 struct bus_type *bus ;  /* Type of bus device is on */
	 struct device_driver *driver ;  /* Which driver has assigned this Device */

	 void (*release) (struct device *dev);
   };

.. **

Structure fields include the parent device that is usually a controller, the
associated kobject object, the bus it is located on, the device driver, and a
called function when the device counter reaches 0.

As usual, we have registration_registration / registration functions
device_register and device_unregister.

To work with the attributes, we have structure structure_atribute_attribute ,
DEVICE_ATTR macrodefine for definition, and device_create_file and
device_remove_file functions to add the attribute to the device.

One important thing to note is that it usually does not work directly with a
struct device structure, but with a structure that contains it, like:

.. code-block:: c

  /* my device type */
   struct my_device {
	 char * name ;
	 struct my_driver *driver;
	 struct device dev;
   };

.. **

Typically, a bus driver will export function to add or remove such a
device, as shown below:

.. code-block:: c

   static int bex_add_dev(const char *name, const char *type, int version)
   {
	struct bex_device *bex_dev;

	bex_dev = kzalloc(sizeof(*bex_dev), GFP_KERNEL);
	if (!bex_dev)
		return -ENOMEM;

	bex_dev->type = kstrdup(type, GFP_KERNEL);
	bex_dev->version = version;

	bex_dev->dev.bus = &bex_bus_type;
	bex_dev->dev.type = &bex_device_type;
	bex_dev->dev.parent = NULL;

	dev_set_name(&bex_dev->dev, "%s", name);

	return device_register(&bex_dev->dev);
   }


   static int bex_del_dev(const char *name)
   {
	struct device *dev;

	dev = bus_find_device_by_name(&bex_bus_type, NULL, name);
	if (!dev)
		return -EINVAL;

	device_unregister(dev);
	put_device(dev);

	return 0;
   }


Drivers
-------

Linux Device Model is used to allow very easy association between system
devices and drivers. Drivers can export information independent of the physical
device.

In sysfs driver information has no single subdirectory associated; They can be
found in the directory structure in different places: in the /sys/module there
is the loaded module, in the devices you can find the driver associated with
each device, in the classes belonging to the drivers in the /sys/bus drivers
associated to each bus .

A device driver is identified by the structure structure of device_driver :

.. code-block:: c

  struct device_driver {
	  const char *name;
	  structure bus_type *bus;

	  struct driver_private *p;

	  struct module *owner;
	  const char *mod_name;  / * Used for built-in modules * /

	  int (*probe) (struct device *dev);
	  int (*remove) (struct device *dev);
	  void (*shutdown) (struct device *dev);
	  int (*suspend) (structure device * dev , pm_message_t state );
	  int (*resume) (struct device * dev );
  };

Among the structure fields we find the name of the driver (appears in sysfs ),
the bus with which the driver works, and functions called at various times in a
device's operation.

As before, we have the registration / registration functions of driver_register
and driver_unregister .

To work with attributes, we have the driver_attribute structure , the macro
definition of DRIVER_ATTR for definition, and the driver_create_file and
driver_remove_file functions for adding the attribute to the device.

As with devices, the device_driver structure structure is usually incorporated
into another structure specific to a particular bus (PCI, USB, etc.):

.. code-block:: c

   struct bex_driver {
	const char *type;

	int (*probe)(struct bex_device *dev);
	void (*remove)(struct bex_device *dev);

	struct device_driver driver;
   };


Driver registration / registration operations are exported for use in
other modules:

.. code-block:: c

   struct bex_driver bex_misc_driver = {
       .type = "misc",
       .probe = bex_misc_probe,
       .remove = bex_misc_remove,
       .driver = {
	   .owner = THIS_MODULE,
	   .name = "bex_misc",
       },
   };

   ...

   /* register driver */
   ret = bex_register_driver(&bex_misc_driver);
   if (ret) {
     ...
   }

   ...

   /* unregister driver */
   err = bex_register_driver(&bex_misc_driver);
   if(err) {
     ...
   }


Classes
-------

A class is a high-level view of the Linux Device Model, which abstracts
implementation details. For example, there are drivers for SCSI and ATA
drivers, but all belong to the class of drives. Classes provide a grouping of
devices based on functionality, not how they are connected or how they work.
Classes have a correspondent in /sys/classes.

There are two main structures that describe the classes: struct class and
struct device . The class structure describes a generic class, while the
structure struct device describes a class associated with a device. There are
functions for initializing / deinitiating and adding attributes for each of
these, include/linux/device.h in include/linux/device.h.

The advantage of using classes is that the udev program in userspace, which we
will discuss later, allows the automatic creation of devices in the /dev
directory based on class information.

For this reason, we will continue to present a small set of functions that work
with classes to simplify the use of the plug and play mechanism.

A generic class is described by structure class structure:

.. code-block:: c

  struct class {
	  const char * name ;
	  struct module *owner ;
	  struct kobject *dev_kobj ;

	  struct subsys_private *p;

	  struct class_attribute *class_attrs ;
	  struct class_device_attribute *class_dev_attrs ;
	  struct device_attribute *dev_attrs ;

	  int (*dev_uevent) (structure device * dev, struct kobj_uevent_env * env);
	  void (*class_release) (class class * class) ;
	  void ( dev_release) (struct device * dev) ;
	  // ...
 };

The class_register and class_unregister functions for initialization /
and cleanup :

.. code-block:: c

   static struct class my_class = {
       .name = "myclass",
   };

   static int __init my_init(void)
   {
       int err;
       ...
       err = class_register(&my_class);
       if (err < 0) {
	   /* handle error */
       }
       ...
   }

   static void __exit my_cleanup (void)
   {
       ...
       class_unregister(&my_class);
       ...
   }


A class associated with a device is described by the device structure. The
device_create and device_destroy functions are available for initialization /
deinterlacing . The device_create function initializes the device structure,
associates its generic class structure with the received device as a parameter;
In addition, it will create an attribute of the class, dev , which contains the
minor and major of the device ( minor:major ). Thus, udev utility in usermode
can read the necessary data from this attribute file to create a node in the
/dev makenod by calling makenod .

An example of initialization:

..code-block:: c
  struct device * my_classdev ;
  cdev cdev struct ;
  struct device dev ;

  // init class for device cdev.dev
  my_classdev = device_create (&my_class, NULL, cdev.dev, &dev, "myclass0");

  // destroy class for device cdev.dev
  device_destroy (&my_class, cdev.dev);

When a new device is discovered, a class and a node will be assigned to the
/dev directory. For the example above, a /dev/myclass0 node will be
/dev/myclass0.

Hotplug
-------

Hotplug describes the mechanism for adding or removing a device from the system
while it is running without having to reboot the system.

A hotplug is a notification from the kernel to the user-space when something
changes in the system configuration. These events are generated when creating
or removing a kobject from the kernel. Since these objects are the basis of the
Linux Device Model, they are included in all structures(struct bus_type,
struct device, struct device_driver, struct class, etc.), a hotplug event
will be created to create or remove any of these structures ( uevent ). When a
device is discovered in the system, an event is generated. Depending on the
point in the Linux Device Model , the functions associated with the occurrence
of an event (usually the case of the bus or class uevent function) are called.
The driver has the ability to set system variables for user-space through these
functions. The generated event reaches the user-space then. Here is the udev
utility that captures these events. There are configuration files for this
utility in the /etc/udev/ directory. Different rules can be specified to
capture only certain events and perform certain actions, depending on the
system variables set in the kernel or in uevent uevent .

An important consequence is that in this way the plug and play mechanism can be
achieved;with his help udevand classes described above may automatically create
entries in the directory /devdevice, and using udevit can automatically load
necessary drivers for a device. In this way, the entire process is automated.

Rules udevare located /etc/udev/rules.d. Any file that ends with .conf here
will be parsed when an event occurs. For more details on how to write rules in
these files see Writing udev rules . For testing, there are utilities
udevmonitor, udevinfoand udevtest.

For a quick example, consider the situation where we want to automatically load
a driver for a device at the time of an event. We can create a new file
/etc/udev/rules.d/myrules.rules, we will have the following line:

Subsystem == "PNP" , attrs {  id  } == "PNP0400" , RUN + = "/ sbin / insmod
/root/mydriver.ko"

This will choose between events generated only those belonging subsystem
pnp(connected to bus PNP) and an id attribute value PNP0400. When will find
this rule will execute the command that inserts the appropriate driver in the
kernel.


Plug and Play
-------------

As noted above, Linux Device Model all devices are connected by a bus, even if
it has the corresponding physical or virtual hardware.

The kernel is already implemented most buses by defining a structure bus_type
and recording functions / Unregistering drivers and appliances. To implement a
bus driver to be determined attaching supported devices and also used its
structures and functions. The main highways are PCI , USB , PNP , IDE , SCSI ,
platform , ACPI , etc.

PNP bus
-------

Plug and play mechanism provides a means of detecting and setting the resources
for legacy driver that may not be configured or otherwise. All plug and play
drivers, protocols, services based on level Plug and Play. It is responsible
for the exchange of information between drivers and protocols. The following
protocols are available:

    PNPBIOS - used for systems such as serial and parallel ports
    ISAPNP - supports ISA bus
    ACPI - offering, among other things, information about system-level devices

The kernel there is a bus pnp_busthat is used to connect many drivers.
Implementation and working with the bus follow the model Linux Device Modeland
is very similar to what thus far.

Main functions and structures exported by the bus, and can be used by drivers
are:

    pnp_driver type associated bus driver
    pnp_register_driver to record a PNP driver system
    pnp_unregister_driver to deînregistra a PNP driver system

As noted in previous sections, the bus has a function matchwith which the
devices associated with the appropriate drivers. For example, if a device
discovery will search for the driver who satisfies the condition given by the
function for the device. Usually this condition is a comparison of IDs and
device driver. One mechanism is to use a static tables spread each driver,
containing information about supported devices and driver bus will be used for
comparison. For example, a parallel port driver will be making
parport_pc_pnp_tbl:

.. code-block:: c

   static const struct pnp_device_id parport_pc_pnp_tbl[] = {
	    /* Standard LPT Printer Port */
	    {.id = "PNP0400", .driver_data = 0},
	    /* ECP Printer Port */
	    {.id = "PNP0401", .driver_data = 0},
   };

   MODULE_DEVICE_TABLE(pnp,parport_pc_pnp_tbl);


It declares and initializes a structure pnp_driver such as
parport_pc_pnp_driver:

.. code-block:: c

  static int parport_pc_pnp_probe(struct pnp_dev *dev,
				  const  struct pnp_id *card_id,
				  const  struct pnp_id *dev_id) ;

  static  void parport_pc_pnp_remove(struct pnp_dev *dev) ;

 static  struct pnp_driver parport_pc_pnp_driver =  {
	   .name  =  "parport_pc",
	   .id_table  = parport_pc_pnp_tbl,
	   .samples  = parport_pc_pnp_probe,
	   .remove  = parport_pc_pnp_remove,
 };

As can be seen, the structure has as parameters a pointer to the table above
stated two functions is called a detection device or to remove it from the
system. Like all layouts, the driver must be registered in the system:

.. code-block:: c

  static  int __init parport_pc_init(void)
  {
	err = pnp_register_driver(&parport_pc_pnp_driver);
	if  (err < 0)  {
		/ * handle error * /
	 }
  }

  static  void __exit parport_pc_exit (void)
  {
	pnp_unregister_driver(&parport_pc_pnp_driver);
  }

PNP operations
--------------

So far we have discussed the model Linux Device Modeland API CPC used. To
implement a driver plug and play, must be respected model Linux Device Model.

Most often, adding a main kernel is not necessary (bus), as already implemented
most highways ( PCI, USB, etc.). The first to be identified that is attached to
the device bus. In the examples below, we believe that this bus is bus PNP.
Thus, use of the above structures and functions.

Add driver
----------

In addition to the usual operations, a driver must obey Linux Device Model.
This will register in the system using functions provided by bus for this
purpose. Usually, the bus provides the driver a particular structure containing
a structure device_driver , that driver must initialize and record a function
\*_register_driver. For example, the bus PNPdriver must declare and initialize a
structure type pnp_driver which to register with pnp_register_driver :

.. code-block:: c

  static  struct pnp_driver my_pnp_driver =  {
	  .name     = "mydriver",
	  .id_table = my_pnp_tbl,
	  .samples  = my_pnp_probe,
	  .remove   = my_pnp_remove,
  };

  static  int __init my_init (void)
  {
	 err = pnp_register_driver(&my_pnp_driver )  ;
  }

Unlike legacy drivers, drivers, plug and play device initialization is not
recorded in the position my_init( register_device). As described above, each
bus has a function matchwhich is called when an associated manager application
to determine its driver. Therefore, there must be a way for each driver to
export information about which devices support in order to pass this comparison
and to be called his functions. In the examples shown in the laboratory to make
a simple comparison between the device name and driver name. Most drivers use a
table with information about the device, for which a structure pointer in the
driver. For example, one associated with a bus driver PNP, a table declares the
type pnp_device_id , and initializes the field id_tableof structure pnp_driver
with a pointer to it:

.. code-block::c

   static const struct pnp_device_id my_pnp_tbl[] = {
	    /* Standard LPT Printer Port */
	    {.id = "PNP0400", .driver_data = 0},
	    /* ECP Printer Port */
	    {.id = "PNP0401", .driver_data = 0},
	    { }
   };

   MODULE_DEVICE_TABLE(pnp,my_pnp_tbl);

   static struct pnp_driver my_pnp_driver = {
	    //...
	    .id_table       = my_pnp_tbl,
	    //...
   };

In the example above driver support parallel port operations. This information
is used by bus in function match_device. When adding a driver, bus driver will
assign and create entries sysfsbased on the driver name. Then call the function
matchbus for all devices associated to associate the driver with any connected
device that supports it.
Remove driver

To remove a driver in the kernel, in addition to operations required a legacy
driver must deînregistrată device_driver structure. If a bus driver for a
paired device PNP, it deînregistrată structure pnp_driver by using the tool
pnp_unregister_driver :

.. code-block::c

   static struct pnp_driver my_pnp_driver;

   static void __exit my_exit(void)
   {
	   pnp_unregister_driver (&my_pnp_driver);
   }

Unlike legacy drivers, plug and play drivers deînregistrează not
Unregistering driver devices to the function my_exit(unregister_device). When
you remove a driver, will remove all references to it for all devices it
supports and also deletes entries sysfs.
Add device

As we saw above, plug and play drivers do not register initialization devices.
This operation will take the position probethat will appeal to a detection
device. In the case of a driver for a device attached to the bus PNP, the
addition will be carried out in function probeof the structure pnp_driver :

.. code-block:: c

   static int my_pnp_probe (struct pnp_dev * dev,
			    const struct pnp_id *card_id,
			    const struct pnp_id *dev_id) {
	   int err, iobase, nr_ports, irq;

	   //get irq & ports
	   if (pnp_irq_valid(dev, 0))
		   irq = pnp_irq(dev, 0);
	   if (pnp_port_valid(dev, 0)) {
		   iobase = pnp_port_start(dev, 0);
	   } else
		   return -ENODEV;
	   nr_ports = pnp_port_len(dev, 0);

	   /* register device dev */
   }

   static struct pnp_driver my_pnp_driver = {
	    //...
	    .probe          = my_pnp_probe,
	    //...
   };

Upon detection of a device in the kernel (in the boot or by the addition of the
device hotplug), it transmits an interrupt to get to the bus system. The device
is recorded with the device_register and is attached to the bus (and will
generate a call userspace, which can be detected udev). Then will cycle through
the bus drivers and will call the function matchfor each of them. Function
matchtries to associate a driver with a device. After being determined
associated device driver will call the function probeof the driver. If the
function ends successfully, the device is added to the list of devices the
driver and creates corresponding entries sysfsbased on the device name.
Remove device

As we saw above, drivers deînregistrează not plug and play devices to
Unregistering driver. This operation will take the position removethat will
appeal to eliminate detection device in the kernel. In the case of a driver for
a device attached to the bus PNP, the addition will be carried out in function
removeof the structure pnp_driver :

.. code-block:: c

   static void my_pnp_remove(struct pnp_dev * dev) {
	    /* unregister device dev */
   }

   static struct pnp_driver my_pnp_driver = {
	    //...
	    .remove         = my_pnp_remove,
   };

As can be seen, the detection device disposal system will call the function
remove the driver will generate a call in user space, which can be detected
udevand dispose entries sysfs.

Exercises
=========

.. include:: exercises-summary.hrst
.. |LAB_NAME| replace:: device_model

0. Intro
---------

Find the definitions of the following symbols in the Linux kernel:

   * dev_name, dev_set_name
   * pnp_device_probe, pnp_bus_match , pnp_register_driver and pnp_bus_type


1. Bus implementation
---------------------

Analyze the contents of the **bex.c**, a modul that implements a bus
driver. Implement the missing functionality marked by **TODO 1**:
register the bus driver and add a new device named "root" with the
type "none" and version 1.

.. hint:: See :c:func:`bex_add_dev`.

Load the module and verify that the bus is visible in /sys/bus. Verify
that the device is visible int /sys/bus/bex/devices.

Remove the module and notice that the sysfs entries are removed.

2. Add type and version device attributes
-----------------------------------------

Add two read-only device attributes, type and version. Follow the
**TODO 2** markings.

Observe that two new attributes are visible in
/sys/bus/bex/devices/root. Check the contents of these attributes.

3. Add del and add bus attributes
---------------------------------

Add two write-only bus attributes, del and add. del expects the name
of a device to delete, while add expects the name, type and version to
create a new device. Follow the **TODO 3** markings and review
`Buses`_.

.. hint:: Use :c:func:`sscanf` to parse the input from sysfs and
	  :c:func:`bex_del_dev` and :c:func:`bex_add_dev` to delete
	  and create a new device.

Create a new device and observe that is visible in
/sys/bus/devices. Delete it and observe it disapears from sysfs.

.. hint:: Use echo to write into the bus attributes:

	  .. code-block:: shell

	     $ echo "name type 1" > /sys/bus/bex/add

	     $ echo "name" > /sys/bus/bex/del

4. Register the bex misc driver
-------------------------------

Modify **bex-misc.c** so that it registers the driver with the bex
bus. Insert the bmx_misc.ko module and create a new bex device from
sysfs with the name "test", type "misc", version 2. Follow the **TODO
4** markings.

Observe that the driver is visible in /sys/bus/bex/drivers.

Why isn't the probe function called?

.. hint:: notice that the bus match function in **bex.c** is not
	  implemented.


Implemet the bus matching function in **bex.c**. Follow the **TODO 5**
markings. Try again to create a new bex device and observ that this
time the bex_misc probe function is called.

5. Register misc device in the bex_misc probe function
------------------------------------------------------

Modify **bex.c** to refuse probing for versions > 1. Also register the
defined misc device in bex_misc_probe and deregister it in
bex_misc_remove. Follow the **TODO 6** markings.

.. hint:: Use :c:func:`misc_register` and :c:func:`misc_deregister`.


Create a new device with the name "test", type "misc" and version 2
and observe that the probe fails. Create a new device with the name
"test", type "misc" and version 1 and observe that the probe is
succesfull.

Inspect the /sys/bus/bex/devices/test and observe that we have a new
entry. Identify the major and minor for the misc device, create a
character device file and try to read and write from the misc device
buffer.

.. hint:: The major and minor should be visible in the dev attribute
	  of the misc device


6. Monitor uevent notifications
-------------------------------

Use the **udevadm monitor** command and observe what happens when:

* the bex.ko and bex_misc.ko modeules are inserted

* a new device with the type "test" is created

* a new device with the type "misc" and version 2 is created

* a new device with the type "misc" and version 1 is created

* all of the above devices are removed
