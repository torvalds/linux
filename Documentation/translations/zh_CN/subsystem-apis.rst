.. SPDX-License-Identifier: GPL-2.0

.. include:: ./disclaimer-zh_CN.rst

:Original: Documentation/subsystem-apis.rst

:翻译:

  唐艺舟 Tang Yizhou <tangyeechou@gmail.com>

==============
内核子系统文档
==============

这些书籍从内核开发者的角度，详细介绍了特定内核子系统
的如何工作。这里的大部分信息直接取自内核源代码，并
根据需要添加了补充材料（或者至少是我们设法添加的 - 可
能 *不是* 所有的材料都有需要）。

核心子系统
----------

.. toctree::
   :maxdepth: 1

   core-api/index
   driver-api/index
   mm/index
   power/index
   scheduler/index
   locking/index

TODOList:

* timers/index

人机接口
--------

.. toctree::
   :maxdepth: 1

   sound/index

TODOList:

* input/index
* hid/index
* gpu/index
* fb/index

网络接口
--------

.. toctree::
   :maxdepth: 1

   infiniband/index

TODOList:

* networking/index
* netlabel/index
* isdn/index
* mhi/index

存储接口
--------

.. toctree::
   :maxdepth: 1

   filesystems/index

TODOList:

* block/index
* cdrom/index
* scsi/index
* target/index

**Fixme**: 这里还需要更多的分类组织工作。

.. toctree::
   :maxdepth: 1

   accounting/index
   cpu-freq/index
   iio/index
   virt/index
   security/index
   PCI/index
   peci/index

TODOList:

* fpga/index
* i2c/index
* leds/index
* pcmcia/index
* spi/index
* w1/index
* watchdog/index
* hwmon/index
* accel/index
* crypto/index
* bpf/index
* usb/index
* misc-devices/index
* wmi/index
