.. SPDX-License-Identifier: GPL-2.0

.. raw:: latex

	\renewcommand\thesection*
	\renewcommand\thesubsection*
	\kerneldocCJKon
	\kerneldocBeginSC{

.. _linux_doc_zh:

中文翻译
========


.. note::

   **翻译计划:**
   内核中文文档欢迎任何翻译投稿，特别是关于内核用户和管理员指南部分。

这是中文内核文档树的顶级目录。内核文档，就像内核本身一样，在很大程度上是一
项正在进行的工作；当我们努力将许多分散的文件整合成一个连贯的整体时尤其如此。
另外，随时欢迎您对内核文档进行改进；如果您想提供帮助，请加入vger.kernel.org
上的linux-doc邮件列表。

顺便说下，中文文档也需要遵守内核编码风格，风格中中文和英文的主要不同就是中文
的字符标点占用两个英文字符宽度， 所以，当英文要求不要超过每行100个字符时，
中文就不要超过50个字符。另外，也要注意'-'，'=' 等符号与相关标题的对齐。在将
补丁提交到社区之前，一定要进行必要的checkpatch.pl检查和编译测试。

许可证文档
----------

下面的文档介绍了Linux内核源代码的许可证（GPLv2）、如何在源代码树中正确标记
单个文件的许可证、以及指向完整许可证文本的链接。

* Documentation/translations/zh_CN/process/license-rules.rst

用户文档
--------

下面的手册是为内核用户编写的——即那些试图让它在给定系统上以最佳方式工作的
用户。

.. toctree::
   :maxdepth: 2

   admin-guide/index

TODOList:

* kbuild/index

固件相关文档
------------

下列文档描述了内核需要的平台固件相关信息。

.. toctree::
   :maxdepth: 2

   devicetree/index

TODOList:

* firmware-guide/index

应用程序开发人员文档
--------------------

用户空间API手册涵盖了描述应用程序开发人员可见内核接口方面的文档。

TODOlist:

* userspace-api/index

内核开发简介
------------

这些手册包含有关如何开发内核的整体信息。内核社区非常庞大，一年下来有数千名
开发人员做出贡献。与任何大型社区一样，知道如何完成任务将使得更改合并的过程
变得更加容易。

.. toctree::
   :maxdepth: 2

   process/index
   dev-tools/index
   doc-guide/index
   kernel-hacking/index
   maintainer/index

TODOList:

* trace/index
* fault-injection/index
* livepatch/index
* rust/index

内核API文档
-----------

以下手册从内核开发人员的角度详细介绍了特定的内核子系统是如何工作的。这里的
大部分信息都是直接从内核源代码获取的，并根据需要添加补充材料（或者至少是在
我们设法添加的时候——可能不是所有的都是有需要的）。

.. toctree::
   :maxdepth: 2

   core-api/index
   locking/index
   accounting/index
   cpu-freq/index
   iio/index
   infiniband/index
   power/index
   virt/index
   sound/index
   filesystems/index
   scheduler/index
   mm/index
   peci/index

TODOList:

* driver-api/index
* block/index
* cdrom/index
* ide/index
* fb/index
* fpga/index
* hid/index
* i2c/index
* isdn/index
* leds/index
* netlabel/index
* networking/index
* pcmcia/index
* target/index
* timers/index
* spi/index
* w1/index
* watchdog/index
* input/index
* hwmon/index
* gpu/index
* security/index
* crypto/index
* bpf/index
* usb/index
* PCI/index
* scsi/index
* misc-devices/index
* mhi/index

体系结构无关文档
----------------

TODOList:

* asm-annotations

特定体系结构文档
----------------

.. toctree::
   :maxdepth: 2

   mips/index
   arm64/index
   riscv/index
   openrisc/index
   parisc/index
   loongarch/index

TODOList:

* arm/index
* ia64/index
* m68k/index
* nios2/index
* powerpc/index
* s390/index
* sh/index
* sparc/index
* x86/index
* xtensa/index

其他文档
--------

有几份未排序的文档似乎不适合放在文档的其他部分，或者可能需要进行一些调整和/或
转换为reStructureText格式，也有可能太旧。

TODOList:

* staging/index
* watch_queue

目录和表格
----------

* :ref:`genindex`

.. raw:: latex

	}\kerneldocEndSC
