
sysfs - _The_ filesystem for exporting kernel objects. 

Patrick Mochel	<mochel@osdl.org>

10 January 2003


What it is:
~~~~~~~~~~~

sysfs is a ram-based filesystem initially based on ramfs. It provides
a means to export kernel data structures, their attributes, and the 
linkages between them to userspace. 

sysfs is tied inherently to the kobject infrastructure. Please read
Documentation/kobject.txt for more information concerning the kobject
interface. 


Using sysfs
~~~~~~~~~~~

sysfs is always compiled in. You can access it by doing:

    mount -t sysfs sysfs /sys 


Directory Creation
~~~~~~~~~~~~~~~~~~

For every kobject that is registered with the system, a directory is
created for it in sysfs. That directory is created as a subdirectory
of the kobject's parent, expressing internal object hierarchies to
userspace. Top-level directories in sysfs represent the common
ancestors of object hierarchies; i.e. the subsystems the objects
belong to. 

Sysfs internally stores the kobject that owns the directory in the
->d_fsdata pointer of the directory's dentry. This allows sysfs to do
reference counting directly on the kobject when the file is opened and
closed. 


Attributes
~~~~~~~~~~

Attributes can be exported for kobjects in the form of regular files in
the filesystem. Sysfs forwards file I/O operations to methods defined
for the attributes, providing a means to read and write kernel
attributes.

Attributes should be ASCII text files, preferably with only one value
per file. It is noted that it may not be efficient to contain only
value per file, so it is socially acceptable to express an array of
values of the same type. 

Mixing types, expressing multiple lines of data, and doing fancy
formatting of data is heavily frowned upon. Doing these things may get
you publically humiliated and your code rewritten without notice. 


An attribute definition is simply:

struct attribute {
        char                    * name;
        mode_t                  mode;
};


int sysfs_create_file(struct kobject * kobj, struct attribute * attr);
void sysfs_remove_file(struct kobject * kobj, struct attribute * attr);


A bare attribute contains no means to read or write the value of the
attribute. Subsystems are encouraged to define their own attribute
structure and wrapper functions for adding and removing attributes for
a specific object type. 

For example, the driver model defines struct device_attribute like:

struct device_attribute {
        struct attribute        attr;
        ssize_t (*show)(struct device * dev, char * buf);
        ssize_t (*store)(struct device * dev, const char * buf);
};

int device_create_file(struct device *, struct device_attribute *);
void device_remove_file(struct device *, struct device_attribute *);

It also defines this helper for defining device attributes: 

#define DEVICE_ATTR(_name,_mode,_show,_store)      \
struct device_attribute dev_attr_##_name = {            \
        .attr = {.name  = __stringify(_name) , .mode   = _mode },      \
        .show   = _show,                                \
        .store  = _store,                               \
};

For example, declaring

static DEVICE_ATTR(foo,0644,show_foo,store_foo);

is equivalent to doing:

static struct device_attribute dev_attr_foo = {
       .attr	= {
		.name = "foo",
		.mode = 0644,
	},
	.show = show_foo,
	.store = store_foo,
};


Subsystem-Specific Callbacks
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When a subsystem defines a new attribute type, it must implement a
set of sysfs operations for forwarding read and write calls to the
show and store methods of the attribute owners. 

struct sysfs_ops {
        ssize_t (*show)(struct kobject *, struct attribute *,char *);
        ssize_t (*store)(struct kobject *,struct attribute *,const char *);
};

[ Subsystems should have already defined a struct kobj_type as a
descriptor for this type, which is where the sysfs_ops pointer is
stored. See the kobject documentation for more information. ]

When a file is read or written, sysfs calls the appropriate method
for the type. The method then translates the generic struct kobject
and struct attribute pointers to the appropriate pointer types, and
calls the associated methods. 


To illustrate:

#define to_dev_attr(_attr) container_of(_attr,struct device_attribute,attr)
#define to_dev(d) container_of(d, struct device, kobj)

static ssize_t
dev_attr_show(struct kobject * kobj, struct attribute * attr, char * buf)
{
        struct device_attribute * dev_attr = to_dev_attr(attr);
        struct device * dev = to_dev(kobj);
        ssize_t ret = 0;

        if (dev_attr->show)
                ret = dev_attr->show(dev,buf);
        return ret;
}



Reading/Writing Attribute Data
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To read or write attributes, show() or store() methods must be
specified when declaring the attribute. The method types should be as
simple as those defined for device attributes:

        ssize_t (*show)(struct device * dev, char * buf);
        ssize_t (*store)(struct device * dev, const char * buf);

IOW, they should take only an object and a buffer as parameters. 


sysfs allocates a buffer of size (PAGE_SIZE) and passes it to the
method. Sysfs will call the method exactly once for each read or
write. This forces the following behavior on the method
implementations: 

- On read(2), the show() method should fill the entire buffer. 
  Recall that an attribute should only be exporting one value, or an
  array of similar values, so this shouldn't be that expensive. 

  This allows userspace to do partial reads and seeks arbitrarily over
  the entire file at will. 

- On write(2), sysfs expects the entire buffer to be passed during the
  first write. Sysfs then passes the entire buffer to the store()
  method. 
  
  When writing sysfs files, userspace processes should first read the
  entire file, modify the values it wishes to change, then write the
  entire buffer back. 

  Attribute method implementations should operate on an identical
  buffer when reading and writing values. 

Other notes:

- The buffer will always be PAGE_SIZE bytes in length. On i386, this
  is 4096. 

- show() methods should return the number of bytes printed into the
  buffer. This is the return value of snprintf().

- show() should always use snprintf(). 

- store() should return the number of bytes used from the buffer. This
  can be done using strlen().

- show() or store() can always return errors. If a bad value comes
  through, be sure to return an error.

- The object passed to the methods will be pinned in memory via sysfs
  referencing counting its embedded object. However, the physical 
  entity (e.g. device) the object represents may not be present. Be 
  sure to have a way to check this, if necessary. 


A very simple (and naive) implementation of a device attribute is:

static ssize_t show_name(struct device * dev, char * buf)
{
        return sprintf(buf,"%s\n",dev->name);
}

static ssize_t store_name(struct device * dev, const char * buf)
{
	sscanf(buf,"%20s",dev->name);
	return strlen(buf);
}

static DEVICE_ATTR(name,S_IRUGO,show_name,store_name);


(Note that the real implementation doesn't allow userspace to set the 
name for a device.)


Top Level Directory Layout
~~~~~~~~~~~~~~~~~~~~~~~~~~

The sysfs directory arrangement exposes the relationship of kernel
data structures. 

The top level sysfs diretory looks like:

block/
bus/
class/
devices/
firmware/
net/

devices/ contains a filesystem representation of the device tree. It maps
directly to the internal kernel device tree, which is a hierarchy of
struct device. 

bus/ contains flat directory layout of the various bus types in the
kernel. Each bus's directory contains two subdirectories:

	devices/
	drivers/

devices/ contains symlinks for each device discovered in the system
that point to the device's directory under root/.

drivers/ contains a directory for each device driver that is loaded
for devices on that particular bus (this assumes that drivers do not
span multiple bus types).


More information can driver-model specific features can be found in
Documentation/driver-model/. 


TODO: Finish this section.


Current Interfaces
~~~~~~~~~~~~~~~~~~

The following interface layers currently exist in sysfs:


- devices (include/linux/device.h)
----------------------------------
Structure:

struct device_attribute {
        struct attribute        attr;
        ssize_t (*show)(struct device * dev, char * buf);
        ssize_t (*store)(struct device * dev, const char * buf);
};

Declaring:

DEVICE_ATTR(_name,_str,_mode,_show,_store);

Creation/Removal:

int device_create_file(struct device *device, struct device_attribute * attr);
void device_remove_file(struct device * dev, struct device_attribute * attr);


- bus drivers (include/linux/device.h)
--------------------------------------
Structure:

struct bus_attribute {
        struct attribute        attr;
        ssize_t (*show)(struct bus_type *, char * buf);
        ssize_t (*store)(struct bus_type *, const char * buf);
};

Declaring:

BUS_ATTR(_name,_mode,_show,_store)

Creation/Removal:

int bus_create_file(struct bus_type *, struct bus_attribute *);
void bus_remove_file(struct bus_type *, struct bus_attribute *);


- device drivers (include/linux/device.h)
-----------------------------------------

Structure:

struct driver_attribute {
        struct attribute        attr;
        ssize_t (*show)(struct device_driver *, char * buf);
        ssize_t (*store)(struct device_driver *, const char * buf);
};

Declaring:

DRIVER_ATTR(_name,_mode,_show,_store)

Creation/Removal:

int driver_create_file(struct device_driver *, struct driver_attribute *);
void driver_remove_file(struct device_driver *, struct driver_attribute *);


