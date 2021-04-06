====================
Block Device Drivers
====================

Lab objectives
==============

  * acquiring knowledge about the behavior of the I/O subsystem on Linux
  * hands-on activities in structures and functions of block devices
  * acquiring basic skills for utilizing the API for block devices, by solving
    exercises

Overview
========

Block devices are characterized by random access to data organized in fixed-size
blocks. Examples of such devices are hard drives, CD-ROM drives, RAM disks, etc.
The speed of block devices is generally much higher than the speed of character
devices, and their performance is also important. This is why the Linux kernel
handles differently these 2 types of devices (it uses a specialized API).

Working with block devices is therefore more complicated than working with
character devices. Character devices have a single current position, while block
devices must be able to move to any position in the device to provide random
access to data. To simplify work with block devices, the Linux kernel provides
an entire subsystem called the block I/O (or block layer) subsystem.

From the kernel perspective, the smallest logical unit of addressing is the
block. Although the physical device can be addressed at sector level, the kernel
performs all disk operations using blocks. Since the smallest unit of physical
addressing is the sector, the size of the block must be a multiple of the size
of the sector. Additionally, the block size must be a power of 2 and can not
exceed the size of a page. The size of the block may vary depending on the file
system used, the most common values being 512 bytes, 1 kilobytes and 4
kilobytes.


Register a block I/O device
===========================

To register a block I/O device, function :c:func:`register_blkdev` is used.
To deregister a block I/O device, function :c:func:`unregister_blkdev` is
used.

Starting with version 4.9 of the Linux kernel, the call to
:c:func:`register_blkdev` is optional. The only operations performed by this
function are the dynamic allocation of a major (if the major argument is 0 when
calling the function) and creating an entry in :file:`/proc/devices`. In
future kernel versions it may be removed; however, most drivers still call it.

Usually, the call to the register function is performed in the module
initialization function, and the call to the deregister function is performed in
the module exit function. A typical scenario is presented below:


.. code-block:: c

   #include <linux/fs.h>

   #define MY_BLOCK_MAJOR           240
   #define MY_BLKDEV_NAME          "mybdev"

   static int my_block_init(void)
   {
       int status;

       status = register_blkdev(MY_BLOCK_MAJOR, MY_BLKDEV_NAME);
       if (status < 0) {
                printk(KERN_ERR "unable to register mybdev block device\n");
                return -EBUSY;
        }
        //...
   }

   static void my_block_exit(void)
   {
        //...
        unregister_blkdev(MY_BLOCK_MAJOR, MY_BLKDEV_NAME);
   }


Register a disk
===============

Although the :c:func:`register_blkdev` function obtains a major, it does not
provide a device (disk) to the system. For creating and using block devices
(disks), a specialized interface defined in :file:`linux/genhd.h` is used.

The useful functions defined in :file:`linux/genhd.h` are to register /allocate
a disk, add it to the system, and de-register /unmount the disk.

The :c:func:`alloc_disk` function is used to allocate a disk, and the
:c:func:`del_gendisk` function is used to deallocate it. Adding the disk to the
system is done using the :c:func:`add_disk` function.

The :c:func:`alloc_disk` and :c:func:`add_disk` functions are typically used in
the module initialization function, and the :c:func:`del_gendisk` function in
the module exit function.

.. code-block:: c

   #include <linux/fs.h>
   #include <linux/genhd.h>

   #define MY_BLOCK_MINORS	 1

   static struct my_block_dev {
       struct gendisk *gd;
       //...
   } dev;

   static int create_block_device(struct my_block_dev *dev)
   {
       dev->gd = alloc_disk(MY_BLOCK_MINORS);
       //...
       add_disk(dev->gd);
   }

   static int my_block_init(void)
   {
       //...
       create_block_device(&dev);
   }

   static void delete_block_device(struct my_block_dev *dev)
   {
       if (dev->gd)
           del_gendisk(dev->gd);
       //...
   }

   static void my_block_exit(void)
   {
       delete_block_device(&dev);
       //...
   }

As with character devices, it is recommended to use :c:type:`my_block_dev`
structure to store important elements describing the block device.

Note that immediately after calling the :c:func:`add_disk` function (actually
even during the call), the disk is active and its methods can be called at any
time. As a result, this function should not be called before the driver is fully
initialized and ready to respond to requests for the registered disk.


It can be noticed that the basic structure in working with block devices (disks)
is the :c:type:`struct gendisk` structure.

After a call to :c:func:`del_gendisk`, the :c:type:`struct gendisk` structure
may continue to exist (and the device operations may still be called) if there
are still users (an open operation was called on the device but the associated
release operation has not been called). One solution is to keep the number of
users of the device and call the :c:func:`del_gendisk` function only when there
are no users left of the device.

:c:type:`struct gendisk` structure
==================================

The :c:type:`struct gendisk` structure stores information about a disk. As
stated above, such a structure is obtained from the :c:func:`alloc_disk` call
and its fields must be filled before it is sent to the :c:func:`add_disk`
function.

The :c:type:`struct gendisk` structure has the following important fields:

   * :c:member:`major`, :c:member:`first_minor`, :c:member:`minor`, describing
     the identifiers used by the disk; a disk must have at least one minor; if
     the disk allows the partitioning operation, a minor must be allocated for
     each possible partition
   * :c:member:`disk_name`, which represents the disk name as it appears in
     :file:`/proc/partitions` and in sysfs (:file:`/sys/block`)
   * :c:member:`fops`, representing operations associated with the disk
   * :c:member:`queue`, which represents the queue of requests
   * :c:member:`capacity`, which is disk capacity in 512 byte sectors;
     it is initialized using the :c:func:`set_capacity` function
   * :c:member:`private_data`, which is a pointer to private data

An example of filling a :c:type:`struct gendisk` structure is presented below:

.. code-block:: c

   #include <linux/genhd.h>
   #include <linux/fs.h>
   #include <linux/blkdev.h>

   #define NR_SECTORS			1024

   #define KERNEL_SECTOR_SIZE		512

   static struct my_block_dev {
       //...
       spinlock_t lock;                /* For mutual exclusion */
       struct request_queue *queue;    /* The device request queue */
       struct gendisk *gd;             /* The gendisk structure */
       //...
   } dev;

   static int create_block_device(struct my_block_dev *dev)
   {
       ...
       /* Initialize the gendisk structure */
       dev->gd = alloc_disk(MY_BLOCK_MINORS);
       if (!dev->gd) {
           printk (KERN_NOTICE "alloc_disk failure\n");
           return -ENOMEM;
       }

       dev->gd->major = MY_BLOCK_MAJOR;
       dev->gd->first_minor = 0;
       dev->gd->fops = &my_block_ops;
       dev->gd->queue = dev->queue;
       dev->gd->private_data = dev;
       snprintf (dev->gd->disk_name, 32, "myblock");
       set_capacity(dev->gd, NR_SECTORS);

       add_disk(dev->gd);

       return 0;
   }

   static int my_block_init(void)
   {
       int status;
       //...
       status = create_block_device(&dev);
       if (status < 0)
           return status;
       //...
   }

   static void delete_block_device(struct my_block_dev *dev)
   {
       if (dev->gd) {
           del_gendisk(dev->gd);
       }
       //...
   }

   static void my_block_exit(void)
   {
       delete_block_device(&dev);
       //...
   }

As stated before, the kernel considers a disk as a vector of 512 byte sectors.
In reality, the devices may have a different size of the sector. To work with
these devices, the kernel needs to be informed about the real size of a sector,
and for all operations the necessary conversions must be made.

To inform the kernel about the device sector size, a parameter of the request
queue must be set just after the request queue is allocated, using the
:c:func:`blk_queue_logical_block_size` function. All requests generated by the
kernel will be multiple of this sector size and will be aligned accordingly.
However, communication between the device and the driver will still be performed
in sectors of 512 bytes in size, so conversion should be done each time (an
example of such conversion is when calling the :c:func:`set_capacity` function
in the code above).

:c:type:`struct block_device_operations` structure
==================================================

Just as for a character device, operations in :c:type:`struct file_operations`
should be completed, so for a block device, the operations in
:c:type:`struct block_device_operations` should be completed. The association
of operations is done through the :c:member:`fops` field in the
:c:type:`struct gendisk`
structure.

Some of the fields of the :c:type:`struct block_device_operations` structure
are presented below:

.. code-block:: c

   struct block_device_operations {
       int (*open) (struct block_device *, fmode_t);
       int (*release) (struct gendisk *, fmode_t);
       int (*locked_ioctl) (struct block_device *, fmode_t, unsigned,
                            unsigned long);
       int (*ioctl) (struct block_device *, fmode_t, unsigned, unsigned long);
       int (*compat_ioctl) (struct block_device *, fmode_t, unsigned,
                            unsigned long);
       int (*direct_access) (struct block_device *, sector_t,
                             void **, unsigned long *);
       int (*media_changed) (struct gendisk *);
       int (*revalidate_disk) (struct gendisk *);
       int (*getgeo)(struct block_device *, struct hd_geometry *);
       blk_qc_t (*submit_bio) (struct bio *bio);
       struct module *owner;
   }

:c:func:`open` and :c:func:`release` operations are called directly from user
space by utilities that may perform the following tasks: partitioning, file
system creation, file system verification. In a :c:func:`mount` operation, the
:c:func:`open` function is called directly from the kernel space, the file
descriptor being stored by the kernel. A driver for a block device can not
differentiate between :c:func:`open` calls performed from user space and kernel
space.

An example of how to use these two functions is given below:

.. code-block:: c

   #include <linux/fs.h>
   #include <linux/genhd.h>

   static struct my_block_dev {
       //...
       struct gendisk * gd;
       //...
   } dev;

   static int my_block_open(struct block_device *bdev, fmode_t mode)
   {
       //...

       return 0;
   }

   static int my_block_release(struct gendisk *gd, fmode_t mode)
   {
       //...

       return 0;
   }

   struct block_device_operations my_block_ops = {
       .owner = THIS_MODULE,
       .open = my_block_open,
       .release = my_block_release
   };

   static int create_block_device(struct my_block_dev *dev)
   {
       //....
       dev->gd->fops = &my_block_ops;
       dev->gd->private_data = dev;
       //...
   }

Please notice that there are no read or write operations. These operations are
performed by the :c:func:`request` function associated with the request queue
of the disk.

Request Queues - Multi-Queue Block Layer
========================================

Drivers for block devices use queues to store the block I/O requests that will
be processed. A request queue is represented by the
:c:type:`struct request_queue` structure. The request queue is made up of a
double-linked list of requests and their associated control information. The
requests are added to the queue by higher-level kernel code (for example, file
systems).

The block device driver associates each queue with a handling function, which
will be called for each request in the queue
(the :c:type:`struct request` structure).

In earlier version of the Linux kernel, each device driver had associated one or
more request queues (:c:type:`struct request_queue`), where any client could add
requests, while also being able to reorder them.
The problem with this approach is that it requires a per-queue lock, making it
inefficient in distributed systems.

The `Multi-Queue Block Queing Mechanism <https://www.kernel.org/doc/html/latest/block/blk-mq.html>`_
solves this issue by splitting the device driver queue in two parts:
 1. Software staging queues
 2. Hardware dispatch queues

Software staging queues
-----------------------

The staging queues hold requests from the clients before sending them to the
block device driver. To prevent the waiting for a per-queue lock, a staging
queue is allocated for each CPU or node. A software queue is associated to
only one hardware queue.

While in this queue, the requests can be merged or reordered, according to an
I/O Scheduler, in order to maximize performance. This means that only the
requests coming from the same CPU or node can be optimized.

Staging queues are usually not used by the block device drivers, but only
internally by the I/O subsystem to optimize requests before sending them to the
device drivers.

Hardware dispatch queues
------------------------

The hardware queues (:c:type:`struct blk_mq_hw_ctx`) are used to send the
requests from the staging queues to the block device driver.
Once in this queue, the requests can't be merged or reordered.

Depending on the underlying hardware, a block device driver can create multiple
hardware queues in order to improve parallelism and maximize performance.

Tag sets
--------

A block device driver can accept a request before the previous one is completed.
As a consequence, the upper layers need a way to know when a request is
completed. For this, a "tag" is added to each request upon submission and sent
back using a completion notification after the request is completed.

The tags are part of a tag set (:c:type:`struct blk_mq_tag_set`), which is
unique to a device.
The tag set structure is allocated and initialized before the request queues
and also stores some of the queues properties.

.. code-block:: c

    struct blk_mq_tag_set {
      ...
      const struct blk_mq_ops   *ops;
      unsigned int               nr_hw_queues;
      unsigned int               queue_depth;
      unsigned int               cmd_size;
      int                        numa_node;
      void                      *driver_data;
      struct blk_mq_tags       **tags;
      struct list_head           tag_list;
      ...
    };

Some of the fields in :c:type:`struct blk_mq_tag_set` are:

 * ``ops`` - Queue operations, most notably the request handling function.
 * ``nr_hw_queues`` - The number of hardware queues allocated for the device
 * ``queue_depth`` - Hardware queues size
 * ``cmd_size`` - Number of extra bytes allocated at the end of the device, to
   be used by the block device driver, if needed.
 * ``numa_node`` - In NUMA systems, the index of the node the storage device is
   connected to.
 * ``driver_data`` - Data private to the driver, if needed.
 * ``tags`` - Pointer to an array of ``nr_hw_queues`` tag sets.
 * ``tag_list`` - List of request queues using this tag set.

Create and delete a request queue
---------------------------------

Request queues are created using the :c:func:`blk_mq_init_queue` function and
are deleted using :c:func:`blk_cleanup_queue`. The first function creates both
the hardware and the software queues and initializes their structures.

Queue properties, including the number of hardware queues, their capacity and
request handling function are configured using the :c:type:`blk_mq_tag_set`
structure, as described above.

An example of using these functions is as follows:

.. code-block:: c

   #include <linux/fs.h>
   #include <linux/genhd.h>
   #include <linux/blkdev.h>

   static struct my_block_dev {
       //...
       struct blk_mq_tag_set tag_set;
       struct request_queue *queue;
       //...
   } dev;

   static blk_status_t my_block_request(struct blk_mq_hw_ctx *hctx,
                                        const struct blk_mq_queue_data *bd)
   //...

   static struct blk_mq_ops my_queue_ops = {
      .queue_rq = my_block_request,
   };

   static int create_block_device(struct my_block_dev *dev)
   {
       /* Initialize tag set. */
       dev->tag_set.ops = &my_queue_ops;
       dev->tag_set.nr_hw_queues = 1;
       dev->tag_set.queue_depth = 128;
       dev->tag_set.numa_node = NUMA_NO_NODE;
       dev->tag_set.cmd_size = 0;
       dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
       err = blk_mq_alloc_tag_set(&dev->tag_set);
       if (err) {
           goto out_err;
       }

       /* Allocate queue. */
       dev->queue = blk_mq_init_queue(&dev->tag_set);
       if (IS_ERR(dev->queue)) {
           goto out_blk_init;
       }

       blk_queue_logical_block_size(dev->queue, KERNEL_SECTOR_SIZE);

        /* Assign private data to queue structure. */
       dev->queue->queuedata = dev;
       //...

   out_blk_init:
       blk_mq_free_tag_set(&dev->tag_set);
   out_err:
       return -ENOMEM;
   }

   static int my_block_init(void)
   {
       int status;
       //...
       status = create_block_device(&dev);
       if (status < 0)
           return status;
       //...
   }

   static void delete_block_device(struct block_dev *dev)
   {
       //...
       blk_mq_free_tag_set(&dev->tag_set);
       blk_cleanup_queue(dev->queue);
   }

   static void my_block_exit(void)
   {
       delete_block_device(&dev);
       //...
   }

After initializing the tag set structure, the tag lists are allocated using the
:c:func:`blk_mq_alloc_tag_set` function.
The pointer to the function which will process the requests
(:c:func:`my_block_request`) is filled in the ``my_queue_ops`` structure and
then the pointer to this structure is added to the tag set.

The queue is created using the :c:func:`blk_mq_init_queue` function, based on
the information added in the tag set.

As part of the request queue initialization, you can configure the
:c:member:`queuedata` field, which is equivalent to the :c:member:`private_data`
field in other structures.

Useful functions for processing request queues
----------------------------------------------

The ``queue_rq`` function from :c:type:`struct blk_mq_ops` is used to handle
requests for working with the block device.
This function is the equivalent of read and write functions encountered on
character devices. The function receives the requests for the device as
arguments and can use various functions for processing them.

The functions used to process the requests in the handler are described below:

   * :c:func:`blk_mq_start_request` - must be called before starting processing
     a request;
   * :c:func:`blk_mq_requeue_request` - to re-send the request in the queue;
   * :c:func:`blk_mq_end_request` - to end request processing and notify the
     upper layers.

Requests for block devices
==========================

A request for a block device is described by :c:type:`struct request`
structure.

The fields of :c:type:`struct request` structure include:

   * :c:member:`cmd_flags`: a series of flags including direction (reading or
     writing); to find out the direction, the macrodefinition
     :c:macro:`rq_data_dir` is used, which returns 0 for a read request and 1
     for a write request on the device;
   * :c:member:`__sector`: the first sector of the transfer request; if the
     device sector has a different size, the appropriate conversion should be
     done. To access this field, use the :c:macro:`blk_rq_pos` macro;
   * :c:member:`__data_len`: the total number of bytes to be transferred; to
     access this field the :c:macro:`blk_rq_bytes` macro is used;
   * generally, data from the current :c:type:`struct bio` will be
     transferred; the data size is obtained using the
     :c:macro:`blk_rq_cur_bytes` macro;
   * :c:member:`bio`, a dynamic list of :c:type:`struct bio` structures that
     is a set of buffers associated to the request; this field is accessed by
     macrodefinition :c:macro:`rq_for_each_segment` if there are multiple
     buffers, or by :c:macro:`bio_data` macrodefinition in case there is only
     one associated buffer;

We will discuss more about the :c:type:`struct bio` structure and its
associated operations in the :ref:`bio_structure` section.

Create a request
----------------

Read /write requests are created by code layers superior to the kernel I/O
subsystem. Typically, the subsystem that creates requests for block devices is
the file management subsystem. The I/O subsystem acts as an interface between
the file management subsystem and the block device driver. The main operations
under the responsibility of the I/O subsystem are adding requests to the queue
of the specific block device and sorting and merging requests according to
performance considerations.

Process a request
-----------------

The central part of a block device driver is the request handling function
(``queue_rq``). In previous examples, the function that fulfilled this role was
:c:func:`my_block_request`. As stated in the
`Create and delete a request queue`_ section, this function is associated to the
driver when creating the tag set structure.

This function is called when the kernel considers that the driver should process
I/O requests. The function must start processing the requests from the queue,
but it is not mandatory to finish them, as requests may be finished by other
parts of the driver.

The request function runs in an atomic context and must follow the rules for
atomic code (it does not need to call functions that can cause sleep, etc.).

Calling the function that processes the requests is asynchronous relative
to the actions of any userspace process and no assumptions about the process
in which the respective function is running should be made. Also, it should not
be assumed that the buffer provided by a request is from kernel space or user
space, any operation that accesses the userspace being erroneous.

One of the simplest request handling function is presented below:

.. code-block:: c

    static blk_status_t my_block_request(struct blk_mq_hw_ctx *hctx,
                                         const struct blk_mq_queue_data *bd)
    {
        struct request *rq = bd->rq;
        struct my_block_dev *dev = q->queuedata;

        blk_mq_start_request(rq);

        if (blk_rq_is_passthrough(rq)) {
            printk (KERN_NOTICE "Skip non-fs request\n");
            blk_mq_end_request(rq, BLK_STS_IOERR);
            goto out;
        }

        /* do work */
        ...

        blk_mq_end_request(rq, BLK_STS_OK);

    out:
        return BLK_STS_OK;
    }

The :c:func:`my_block_request` function performs the following operations:

   * Get a pointer to the request structure from the ``bd`` argument and start
     its processing using the :c:func:`blk_mq_start_request` function.
   * A block device can receive calls which do not transfer data blocks (e.g.
     low level operations on the disk, instructions referring to special ways of
     accessing the device). Most drivers do not know how to handle these
     requests and return an error.
   * To return an error, :c:func:`blk_mq_end_request` function is called,
     ``BLK_STS_IOERR`` being the second argument.
   * The request is processed according to the needs of the associated device.
   * The request ends. In this case, :c:func:`blk_mq_end_request` function is
     called in order to complete the request.

.. bio_structure:

:c:type:`struct bio` structure
==============================

Each :c:type:`struct request` structure is an I/O block request, but may come
from combining more independent requests from a higher level. The sectors to be
transferred for a request can be scattered into the main memory but they always
correspond to a set of consecutive sectors on the device. The request is
represented as a series of segments, each corresponding to a buffer in memory.
The kernel can combine requests that refer to adjacent sectors but will not
combine write requests with read requests into a single
:c:type:`struct request` structure.

A :c:type:`struct request` structure is implemented as a linked list of
:c:type:`struct bio` structures together with information that allows the
driver to retain its current position while processing the request.

The :c:type:`struct bio` structure is a low-level description of a portion of
a block I/O request.

.. code-block:: c

   struct bio {
       //...
       struct gendisk          *bi_disk;
       unsigned int            bi_opf;         /* bottom bits req flags, top bits REQ_OP. Use accessors. */
       //...
       struct bio_vec          *bi_io_vec;     /* the actual vec list */
       //...
       struct bvec_iter        bi_iter;
       /...
       void                    *bi_private;
       //...
   };

In turn, the :c:type:`struct bio` structure contains a :c:member:`bi_io_vec`
vector of :c:type:`struct bio_vec` structures. It consists of the individual
pages in the physical memory to be transferred, the offset within the page and
the size of the buffer. To iterate through a :c:type:`struct bio` structure,
we need to iterate through the vector of :c:type:`struct bio_vec` and transfer
the data from every physical page. To simplify vector iteration, the
:c:type:`struct bvec_iter` structure is used. This structure maintains
information about how many buffers and sectors were consumed during the
iteration. The request type is encoded in the :c:member:`bi_opf` field; to
determine it, use the :c:func:`bio_data_dir` function.

Create a :c:type:`struct bio` structure
---------------------------------------

Two functions can be used to create a :c:type:`struct bio` structure:

   * :c:func:`bio_alloc`: allocates space for a new structure; the structure
     must be initialized;
   * :c:func:`bio_clone`: makes a copy of an existing :c:type:`struct bio`
     structure; the newly obtained structure is initialized with the values of
     the cloned structure fields; the buffers are shared with the
     :c:type:`struct bio` structure that has been cloned so that access to the
     buffers has to be done carefully to avoid access to the same memory area
     from the two clones;

Both functions return a new :c:type:`struct bio` structure.

Submit a :c:type:`struct bio` structure
---------------------------------------

Usually, a :c:type:`struct bio` structure is created by the higher levels of
the kernel (usually the file system). A structure thus created is then
transmitted to the I/O subsystem that gathers more :c:type:`struct bio`
structures into a request.

For submitting a :c:type:`struct bio` structure to the associated I/O device
driver, the :c:func:`submit_bio` function is used. The function receives as
argument an initialized :c:type:`struct bio` structure that will be added to
a request from the request queue of an I/O device. From that queue, it can be
processed by the I/O device driver using a specialized function.


.. _bio_completion:

Wait for the completion of a :c:type:`struct bio` structure
-----------------------------------------------------------

Submitting a :c:type:`struct bio` structure to a driver has the effect of
adding it to a request from the request queue from where it will be further
processed. Thus, when the :c:func:`submit_bio` function returns, it is not
guaranteed that the processing of the structure has finished. If you want to
wait for the processing of the request to be finished, use the
:c:func:`submit_bio_wait` function.

To be notified when the processing of a :c:type:`struct bio` structure ends
(when we do not use :c:func:`submit_bio_wait` function), the
:c:member:`bi_end_io` field of the structure should be used. This field
specifies the function that will be called at the end of the
:c:type:`struct bio` structure processing. You can use the
:c:member:`bi_private` field of the structure to pass information to the
function.

Initialize a :c:type:`struct bio` structure
-------------------------------------------

Once a :c:type:`struct bio` structure has been allocated and before being
transmitted, it must be initialized.

Initializing the structure involves filling in its important fields. As
mentioned above, the :c:member:`bi_end_io` field is used to specify the function
called when the processing of the structure is finished. The
:c:member:`bi_private` field is used to store useful data that can be accessed
in the function pointed by :c:member:`bi_end_io`.

The :c:member:`bi_opf` field specifies the type of operation.

.. code-block:: c

   struct bio *bio = bio_alloc(GFP_NOIO, 1);
   //...
   bio->bi_disk = bdev->bd_disk;
   bio->bi_iter.bi_sector = sector;
   bio->bi_opf = REQ_OP_READ;
   bio_add_page(bio, page, size, offset);
   //...

In the code snippet above we specified the block device to which we sent the
following: :c:type:`struct bio` structure, startup sector, operation
(:c:data:`REQ_OP_READ` or :c:data:`REQ_OP_WRITE`) and content. The content of a
:c:type:`struct bio` structure is a buffer described by: a physical page,
the offset in the page and the size of the bufer. A page can be assigned using
the :c:func:`alloc_page` call.

.. note:: The :c:data:`size` field of the :c:func:`bio_add_page` call must be
          a multiple of the device sector size.

.. _bio_content:

How to use the content of a :c:type:`struct bio` structure
----------------------------------------------------------

To use the content of a :c:type:`struct bio` structure, the structure's
support pages must be mapped to the kernel address space from where they can be
accessed. For mapping /unmapping, use the :c:macro:`kmap_atomic` and
the :c:macro:`kunmap_atomic` macros.

A typical example of use is:

.. code-block:: c

   static void my_block_transfer(struct my_block_dev *dev, size_t start,
                                 size_t len, char *buffer, int dir);


   static int my_xfer_bio(struct my_block_dev *dev, struct bio *bio)
   {
       struct bio_vec bvec;
       struct bvec_iter i;
       int dir = bio_data_dir(bio);

       /* Do each segment independently. */
       bio_for_each_segment(bvec, bio, i) {
           sector_t sector = i.bi_sector;
           char *buffer = kmap_atomic(bvec.bv_page);
           unsigned long offset = bvec.bv_offset;
           size_t len = bvec.bv_len;

           /* process mapped buffer */
           my_block_transfer(dev, sector, len, buffer + offset, dir);

           kunmap_atomic(buffer);
       }

       return 0;
   }

As it can be seen from the example above, iterating through a
:c:type:`struct bio` requires iterating through all of its segments. A segment
(:c:type:`struct bio_vec`) is defined by the physical address page, the offset
in the page and its size.

To simplify the processing of a :c:type:`struct bio`, use the
:c:macro:`bio_for_each_segment` macrodefinition. It will iterate through all
segments, and will also update global information stored in an iterator
(:c:type:`struct bvec_iter`) such as the current sector as well as other
internal information (segment vector index, number of bytes left to be
processed, etc.) .

You can store information in the mapped buffer, or extract information.

In case request queues are used and you needed to process the requests
at :c:type:`struct bio` level, use the :c:macro:`rq_for_each_segment`
macrodefinition instead of the :c:macro:`bio_for_each_segment` macrodefinition.
This macrodefinition iterates through each segment of each
:c:type:`struct bio` structure of a :c:type:`struct request` structure and
updates a :c:type:`struct req_iterator` structure. The
:c:type:`struct req_iterator` contains the current :c:type:`struct bio`
structure and the iterator that traverses its segments.

A typical example of use is:

.. code-block:: c

   struct bio_vec bvec;
   struct req_iterator iter;

   rq_for_each_segment(bvec, req, iter) {
       sector_t sector = iter.iter.bi_sector;
       char *buffer = kmap_atomic(bvec.bv_page);
       unsigned long offset = bvec.bv_offset;
       size_t len = bvec.bv_len;
       int dir = bio_data_dir(iter.bio);

       my_block_transfer(dev, sector, len, buffer + offset, dir);

       kunmap_atomic(buffer);
   }

Free a :c:type:`struct bio` structure
-------------------------------------

Once a kernel subsystem uses a :c:type:`struct bio` structure, it will have to
release the reference to it. This is done by calling :c:func:`bio_put` function.

Set up a request queue at :c:type:`struct bio` level
----------------------------------------------------

We have previously seen how we can specify a function to be used to process
requests sent to the driver. The function receives as argument the requests and
carries out processing at :c:type:`struct request` level.

If, for flexibility reasons, we need to specify a function that carries
out processing at :c:type:`struct bio` structure level, we no longer
use request queues and we will need to fill the ``submit_bio`` field in the
:c:type:`struct block_device_operations` associated to the driver.

Below is a typical example of initializing a function that carries out
processing at :c:type:`struct bio` structure level:

.. code-block:: c

    // the declaration of the function that carries out processing
    // :c:type:`struct bio` structures
    static blk_qc_t my_submit_bio(struct bio *bio);

    struct block_device_operations my_block_ops = {
       .owner = THIS_MODULE,
       .submit_bio = my_submit_bio
       ...
    };

Further reading
===============

* `Linux Device Drivers 3rd Edition, Chapter 16. Block Drivers <http://static.lwn.net/images/pdf/LDD3/ch16.pdf>`_
* Linux Kernel Development, Second Edition – Chapter 13. The Block I/O Layer
* `A simple block driver <https://lwn.net/Articles/58719/>`_
* `The gendisk interface <https://lwn.net/Articles/25711/>`_
* `The bio structure <https://lwn.net/Articles/26404/>`_
* `Request queues <https://lwn.net/Articles/27055/>`_
* `Documentation/block/request.txt - Struct request documentation <https://elixir.bootlin.com/linux/v4.15/source/Documentation/block/request.txt>`_
* `Documentation/block/biodoc.txt - Notes on the Generic Block Layer <https://elixir.bootlin.com/linux/v4.15/source/Documentation/block/biodoc.txt>`_
* `drivers/block/brd/c - RAM backed block disk driver <https://elixir.bootlin.com/linux/v4.15/source/drivers/block/brd.c>`_
* `I/O Schedulers <https://www.linuxjournal.com/article/6931>`_


Exercises
=========

.. include:: ../labs/exercises-summary.hrst
.. |LAB_NAME| replace:: block_device_drivers

0. Intro
--------

Using |LXR|_ find the definitions of the following symbols in the Linux kernel:

   * :c:type:`struct bio`
   * :c:type:`struct bio_vec`
   * :c:macro:`bio_for_each_segment`
   * :c:type:`struct gendisk`
   * :c:type:`struct block_device_operations`
   * :c:type:`struct request`

1. Block device
---------------

Create a kernel module that allows you to register or deregister a block device.
Start from the files in the :file:`1-2-3-6-ram-disk/kernel` directory in the
lab skeleton.

Follow the comments marked with **TODO 1** in the laboratory skeleton. Use the
existing macrodefinitions (:c:macro:`MY_BLOCK_MAJOR`,
:c:macro:`MY_BLKDEV_NAME`). Check the value returned by the register function,
and in case of error, return the error code.

Compile the module, copy it to the virtual machine and insert it into the
kernel. Verify that your device was successfully created inside the
:file:`/proc/devices`.
You will see a device with major 240.

Unload the kernel module and check that the device was unregistered.

.. hint:: Review the `Register a block I/O device`_ section.

Change the :c:macro:`MY_BLOCK_MAJOR` value to 7. Compile the module, copy it to
the virtual machine, and insert it into the kernel. Notice that the insertion
fails because there is already another driver/device registered in the kernel
with the major 7.

Restore the 240 value for the :c:macro:`MY_BLOCK_MAJOR` macro.

2. Disk registration
--------------------

Modify the previous module to add a disk associated with the driver. Analyze the
macrodefinitions, :c:type:`my_block_dev` structure and existing functions from
the :file:`ram-disk.c` file.

Follow the comments marked with **TODO 2**. Use the
:c:func:`create_block_device` and the :c:func:`delete_block_device` functions.

.. hint:: Review the `Register a disk`_ and `Process a request`_ sections.

Fill in the :c:func:`my_block_request` function to process the request
without actually processing your request: display the "request received" message
and the following information: start sector, total size, data size from the
current :c:type:`struct bio` structure, direction. To validate a request type,
use the :c:func:`blk_rq_is_passthrough` (the function returns 0 in the case in
which we are interested, i.e. when the request is generated by the file system).

.. hint:: To find the needed info, review the `Requests for block devices`_
          section.

Use the :c:func:`blk_mq_end_request` function to finish processing the
request.

Insert the module into the kernel and inspect the messages printed
by the module. When a device is added, a request is sent to the device. Check
the presence of :file:`/dev/myblock` and if it doesn't exist, create the device
using the command:

.. code-block:: shell

   mknod /dev/myblock b 240 0

To generate writing requests, use the command:

.. code-block:: shell

   echo "abc"> /dev/myblock

Notice that a write request is preceded by a read request. The request
is done to read the block from the disk and "update" its content with the
data provided by the user, without overwriting the rest. After reading and
updating, writing takes place.

3. RAM disk
-----------

Modify the previous module to create a RAM disk: requests to the device will
result in reads/writes in a memory area.

The memory area :c:data:`dev->data` is already allocated in the source code of
the module using :c:func:`vmalloc` and deallocated using :c:func:`vfree`.

.. note:: Review the `Process a request`_ section.

Follow the comments marked with **TODO 3** to complete the
:c:func:`my_block_transfer` function to write/read the request information
in/from the memory area. The function will be called for each request within
the queue processing function: :c:func:`my_block_request`. To write/read
to/from the memory area, use :c:func:`memcpy`. To determine the write/read
information, use the fields of the :c:type:`struct request` structure.

.. hint:: To find out the size of the request data, use the
          :c:macro:`blk_rq_cur_bytes` macro. Do not use the
          :c:macro:`blk_rq_bytes` macro.

.. hint:: To find out the buffer associated to the request, use
          :c:data:`bio_data`(:c:data:`rq->bio`).

.. hint:: A description of useful macros is in the `Requests for block devices`_
          section.

.. hint:: You can find useful information in the
          `block device driver example
          <https://github.com/martinezjavier/ldd3/blob/master/sbull/sbull.c>`_
          from `Linux Device Driver <http://lwn.net/Kernel/LDD3/>`_.

For testing, use the test file :file:`user/ram-disk-test.c`.
The test program is compiled automatically at ``make build``, copied to the
virtual machine at ``make copy`` and can be run on the QEMU virtual machine
using the command:

.. code-block:: shell

   ./ram-disk-test

There is no need to insert the module into the kernel, it will be inserted by
the ``ram-disk-test`` command.

Some tests may fail because of lack of synchronization between the transmitted
data (flush).

4. Read data from the disk
--------------------------

The purpose of this exercise is to read data from the
:c:macro:`PHYSICAL_DISK_NAME` disk (:file:`/dev/vdb`) directly from the kernel.

.. attention:: Before solving the exercise, we need to make sure the disk is
               added to the virtual machine.

               Check the variable ``QEMU_OPTS`` from :file:`qemu/Makefile`.
               There should already be two extra disks added using ``-drive ...``.

               If there are not, generate a file that we will use as
               the disk image using the command:
               :command:`dd if=/dev/zero of=qemu/mydisk.img bs=1024 count=1`
               and add the following option:
               :command:`-drive file=qemu/mydisk.img,if=virtio,format=raw`
               to :file:`qemu/Makefile` (in the :c:data:`QEMU_OPTS` variable,
               after the root disk).

Follow the comments marked with **TODO 4** in the directory :file:`4-5-relay/`
and implement :c:func:`open_disk` and :c:func:`close_disk`.
Use the :c:func:`blkdev_get_by_path` and :c:func:`blkdev_put` functions. The
device must be opened in read-write mode exclusively
(:c:macro:`FMODE_READ` | :c:macro:`FMODE_WRITE` | :c:macro:`FMODE_EXCL`), and
as holder you must use the current module (:c:macro:`THIS_MODULE`).

Implement the :c:func:`send_test_bio` function. You will have to create a new
:c:type:`struct bio` structure and fill it, submit it and wait for it. Read the
first sector of the disk. To wait, call the :c:func:`submit_bio_wait` function.

.. hint:: The first sector of the disk is the sector with the index 0.
          This value must be used to initialize the field
          :c:member:`bi_iter.bi_sector` of the :c:type:`struct bio`.

          For the read operation, use the :c:macro:`REQ_OP_READ` macro to
          initialize the :c:member:`bi_opf` field of the :c:type:`struct bio`.

After finishing the operation, display the first 3 bytes of data read by
:c:type:`struct bio` structure. Use the format ``"% 02x"`` for :c:func:`printk`
to display the data and the :c:macro:`kmap_atomic` and :c:macro:`kunmap_atomic`
macros respectively.

.. hint:: As an argument for the :c:func:`kmap_atomic` function, just use the
          page which is allocated above in the code, in the :c:data:`page`
          variable.

.. hint:: Review the sections :ref:`bio_content` and :ref:`bio_completion`.

For testing, use the :file:`test-relay-disk` script, which is copied on the
virtual machine when running :command:`make copy`. If it is not copied, make
sure it is executable:

.. code-block:: shell

   chmod +x test-relay-disk

There is no need to load the module into the kernel, it will be loaded by
:command:`test-relay-disk`.

Use the command below to run the script:

.. code-block:: shell

   ./test-relay-disk

The script writes "abc" at the beginning of the disk indicated by
:c:macro:`PHYSICAL_DISK_NAME`. After running, the module will display 61 62 63
(the corresponding hexadecimal values of letters "a", "b" and "c").

5. Write data to the disk
-------------------------

Follow the comments marked with **TODO 5** to write a message
(:c:macro:`BIO_WRITE_MESSAGE`) on the disk.

The :c:func:`send_test_bio` function receives as argument the operation type
(read or write). Call in the :c:func:`relay_init` function the function for
reading and in the :c:func:`relay_exit` function the function for writing. We
recommend using the :c:macro:`REQ_OP_READ` and the :c:macro:`REQ_OP_WRITE`
macros.

Inside the :c:func:`send_test_bio` function, if the operation is write, fill in
the buffer associated to the :c:type:`struct bio` structure with the message
:c:macro:`BIO_WRITE_MESSAGE`. Use the :c:macro:`kmap_atomic` and the
:c:macro:`kunmap_atomic` macros to work with the buffer associated to the
:c:type:`struct bio` structure.

.. hint:: You need to update the type of the operation associated to the
          :c:type:`struct bio` structure by setting the :c:member:`bi_opf` field
          accordingly.

For testing, run the :file:`test-relay-disk` script using the command:

.. code-block:: shell

   ./test-relay-disk

The script will display the ``"read from /dev/sdb: 64 65 66"`` message at the
standard output.

6. Processing requests from the request queue at :c:type:`struct bio` level
---------------------------------------------------------------------------

In the implementation from Exercise 3, we have only processed a
:c:type:`struct bio_vec` of the current :c:type:`struct bio` from the request.
We want to process all :c:type:`struct bio_vec` structures from all
:c:type:`struct bio` structures.
For this, we will iterate through all :c:type:`struct bio` requests and through
all :c:type:`struct bio_vec` structures (also called segments) of each
:c:type:`struct bio`.

Add, within the ramdisk implementation (:file:`1-2-3-6-ram-disk/` directory),
support for processing the requests from the request queue at
:c:type:`struct bio` level. Follow the comments marked with **TODO 6**.

Set the :c:macro:`USE_BIO_TRANSFER` macro to 1.

Implement the :c:func:`my_xfer_request` function. Use the
:c:macro:`rq_for_each_segment` macro to iterate through the :c:type:`bio_vec`
structures of each :c:type:`struct bio` from the request.

.. hint:: Review the indications and the code snippets from the
          :ref:`bio_content` section.

.. hint:: Use the :c:type:`struct bio` segment iterator to get the current
          sector (:c:member:`iter.iter.bi_sector`).

.. hint:: Use the request iterator to get the reference to the current
          :c:type:`struct bio` (:c:member:`iter.bio`).

.. hint:: Use the :c:macro:`bio_data_dir` macro to find the reading or writing
          direction for a :c:type:`struct bio`.

Use the :c:macro:`kmap_atomic` or the :c:macro:`kunmap_atomic` macros to map
the pages of each :c:type:`struct bio` structure and access its associated
buffers. For the actual transfer, call the :c:func:`my_block_transfer` function
implemented in the previous exercise.

For testing, use the :file:`ram-disk-test.c` test file:

.. code-block:: shell

   ./ram-disk-test

There is no need to insert the module into the kernel, it will be inserted by
the :command:`ram-disk-test` executable.

Some tests may crash because of lack of synchronization between the transmitted
data (flush).
