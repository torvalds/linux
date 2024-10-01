.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/mm/hmm.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

==================
异构内存管理 (HMM)
==================

提供基础设施和帮助程序以将非常规内存（设备内存，如板上 GPU 内存）集成到常规内核路径中，其
基石是此类内存的专用struct page（请参阅本文档的第 5 至 7 节）。

HMM 还为 SVM（共享虚拟内存）提供了可选的帮助程序，即允许设备透明地访问与 CPU 一致的程序
地址，这意味着 CPU 上的任何有效指针也是该设备的有效指针。这对于简化高级异构计算的使用变得
必不可少，其中 GPU、DSP 或 FPGA 用于代表进程执行各种计算。

本文档分为以下部分：在第一部分中，我揭示了与使用特定于设备的内存分配器相关的问题。在第二
部分中，我揭示了许多平台固有的硬件限制。第三部分概述了 HMM 设计。第四部分解释了 CPU 页
表镜像的工作原理以及 HMM 在这种情况下的目的。第五部分处理内核中如何表示设备内存。最后，
最后一节介绍了一个新的迁移助手，它允许利用设备 DMA 引擎。

.. contents:: :local:

使用特定于设备的内存分配器的问题
================================

具有大量板载内存（几 GB）的设备（如 GPU）历来通过专用驱动程序特定 API 管理其内存。这会
造成设备驱动程序分配和管理的内存与常规应用程序内存（私有匿名、共享内存或常规文件支持内存）
之间的隔断。从这里开始，我将把这个方面称为分割的地址空间。我使用共享地址空间来指代相反的情况：
即，设备可以透明地使用任何应用程序内存区域。

分割的地址空间的发生是因为设备只能访问通过设备特定 API 分配的内存。这意味着从设备的角度来
看，程序中的所有内存对象并不平等，这使得依赖于广泛的库的大型程序变得复杂。

具体来说，这意味着想要利用像 GPU 这样的设备的代码需要在通用分配的内存（malloc、mmap
私有、mmap 共享）和通过设备驱动程序 API 分配的内存之间复制对象（这仍然以 mmap 结束，
但是是设备文件）。

对于平面数据集（数组、网格、图像……），这并不难实现，但对于复杂数据集（列表、树……），
很难做到正确。复制一个复杂的数据集需要重新映射其每个元素之间的所有指针关系。这很容易出错，
而且由于数据集和地址的重复，程序更难调试。

分割地址空间也意味着库不能透明地使用它们从核心程序或另一个库中获得的数据，因此每个库可能
不得不使用设备特定的内存分配器来重复其输入数据集。大型项目会因此受到影响，并因为各种内存
拷贝而浪费资源。

复制每个库的API以接受每个设备特定分配器分配的内存作为输入或输出，并不是一个可行的选择。
这将导致库入口点的组合爆炸。

最后，随着高级语言结构（在 C++ 中，当然也在其他语言中）的进步，编译器现在有可能在没有程
序员干预的情况下利用 GPU 和其他设备。某些编译器识别的模式仅适用于共享地址空间。对所有
其他模式，使用共享地址空间也更合理。


I/O 总线、设备内存特性
======================

由于一些限制，I/O 总线削弱了共享地址空间。大多数 I/O 总线只允许从设备到主内存的基本
内存访问；甚至缓存一致性通常是可选的。从 CPU 访问设备内存甚至更加有限。通常情况下，它
不是缓存一致的。

如果我们只考虑 PCIE 总线，那么设备可以访问主内存（通常通过 IOMMU）并与 CPU 缓存一
致。但是，它只允许设备对主存储器进行一组有限的原子操作。这在另一个方向上更糟：CPU
只能访问有限范围的设备内存，而不能对其执行原子操作。因此，从内核的角度来看，设备内存不
能被视为与常规内存等同。

另一个严重的因素是带宽有限（约 32GBytes/s，PCIE 4.0 和 16 通道）。这比最快的 GPU
内存 (1 TBytes/s) 慢 33 倍。最后一个限制是延迟。从设备访问主内存的延迟比设备访问自
己的内存时高一个数量级。

一些平台正在开发新的 I/O 总线或对 PCIE 的添加/修改以解决其中一些限制
（OpenCAPI、CCIX）。它们主要允许 CPU 和设备之间的双向缓存一致性，并允许架构支持的所
有原子操作。遗憾的是，并非所有平台都遵循这一趋势，并且一些主要架构没有针对这些问题的硬
件解决方案。

因此，为了使共享地址空间有意义，我们不仅必须允许设备访问任何内存，而且还必须允许任何内
存在设备使用时迁移到设备内存（在迁移时阻止 CPU 访问）。


共享地址空间和迁移
==================

HMM 打算提供两个主要功能。第一个是通过复制cpu页表到设备页表中来共享地址空间，因此对
于进程地址空间中的任何有效主内存地址，相同的地址指向相同的物理内存。

为了实现这一点，HMM 提供了一组帮助程序来填充设备页表，同时跟踪 CPU 页表更新。设备页表
更新不像 CPU 页表更新那么容易。要更新设备页表，您必须分配一个缓冲区（或使用预先分配的
缓冲区池）并在其中写入 GPU 特定命令以执行更新（取消映射、缓存失效和刷新等）。这不能通
过所有设备的通用代码来完成。因此，为什么HMM提供了帮助器，在把硬件的具体细节留给设备驱
动程序的同时，把一切可以考虑的因素都考虑进去了。

HMM 提供的第二种机制是一种新的 ZONE_DEVICE 内存，它允许为设备内存的每个页面分配一个
struct page。这些页面很特殊，因为 CPU 无法映射它们。然而，它们允许使用现有的迁移机
制将主内存迁移到设备内存，从 CPU 的角度来看，一切看起来都像是换出到磁盘的页面。使用
struct page可以与现有的 mm 机制进行最简单、最干净的集成。再次，HMM 仅提供帮助程序，
首先为设备内存热插拔新的 ZONE_DEVICE 内存，然后执行迁移。迁移内容和时间的策略决定留
给设备驱动程序。

请注意，任何 CPU 对设备页面的访问都会触发缺页异常并迁移回主内存。例如，当支持给定CPU
地址 A 的页面从主内存页面迁移到设备页面时，对地址 A 的任何 CPU 访问都会触发缺页异常
并启动向主内存的迁移。

凭借这两个特性，HMM 不仅允许设备镜像进程地址空间并保持 CPU 和设备页表同步，而且还通
过迁移设备正在使用的数据集部分来利用设备内存。


地址空间镜像实现和API
=====================

地址空间镜像的主要目标是允许将一定范围的 CPU 页表复制到一个设备页表中；HMM 有助于
保持两者同步。想要镜像进程地址空间的设备驱动程序必须从注册 mmu_interval_notifier
开始::

 int mmu_interval_notifier_insert(struct mmu_interval_notifier *interval_sub,
				  struct mm_struct *mm, unsigned long start,
				  unsigned long length,
				  const struct mmu_interval_notifier_ops *ops);

在 ops->invalidate() 回调期间，设备驱动程序必须对范围执行更新操作（将范围标记为只
读，或完全取消映射等）。设备必须在驱动程序回调返回之前完成更新。

当设备驱动程序想要填充一个虚拟地址范围时，它可以使用::

  int hmm_range_fault(struct hmm_range *range);

如果请求写访问，它将在丢失或只读条目上触发缺页异常（见下文）。缺页异常使用通用的 mm 缺
页异常代码路径，就像 CPU 缺页异常一样。

这两个函数都将 CPU 页表条目复制到它们的 pfns 数组参数中。该数组中的每个条目对应于虚拟
范围中的一个地址。HMM 提供了一组标志来帮助驱动程序识别特殊的 CPU 页表项。

在 sync_cpu_device_pagetables() 回调中锁定是驱动程序必须尊重的最重要的方面，以保
持事物正确同步。使用模式是::

 int driver_populate_range(...)
 {
      struct hmm_range range;
      ...

      range.notifier = &interval_sub;
      range.start = ...;
      range.end = ...;
      range.hmm_pfns = ...;

      if (!mmget_not_zero(interval_sub->notifier.mm))
          return -EFAULT;

 again:
      range.notifier_seq = mmu_interval_read_begin(&interval_sub);
      mmap_read_lock(mm);
      ret = hmm_range_fault(&range);
      if (ret) {
          mmap_read_unlock(mm);
          if (ret == -EBUSY)
                 goto again;
          return ret;
      }
      mmap_read_unlock(mm);

      take_lock(driver->update);
      if (mmu_interval_read_retry(&ni, range.notifier_seq) {
          release_lock(driver->update);
          goto again;
      }

      /* Use pfns array content to update device page table,
       * under the update lock */

      release_lock(driver->update);
      return 0;
 }

driver->update 锁与驱动程序在其 invalidate() 回调中使用的锁相同。该锁必须在调用
mmu_interval_read_retry() 之前保持，以避免与并发 CPU 页表更新发生任何竞争。

利用 default_flags 和 pfn_flags_mask
====================================

hmm_range 结构有 2 个字段，default_flags 和 pfn_flags_mask，它们指定整个范围
的故障或快照策略，而不必为 pfns 数组中的每个条目设置它们。

例如，如果设备驱动程序需要至少具有读取权限的范围的页面，它会设置::

    range->default_flags = HMM_PFN_REQ_FAULT;
    range->pfn_flags_mask = 0;

并如上所述调用 hmm_range_fault()。这将填充至少具有读取权限的范围内的所有页面。

现在假设驱动程序想要做同样的事情，除了它想要拥有写权限的范围内的一页。现在驱动程序设
置::

    range->default_flags = HMM_PFN_REQ_FAULT;
    range->pfn_flags_mask = HMM_PFN_REQ_WRITE;
    range->pfns[index_of_write] = HMM_PFN_REQ_WRITE;

有了这个，HMM 将在至少读取（即有效）的所有页面中异常，并且对于地址
== range->start + (index_of_write << PAGE_SHIFT) 它将异常写入权限，即，如果
CPU pte 没有设置写权限，那么HMM将调用handle_mm_fault()。

hmm_range_fault 完成后，标志位被设置为页表的当前状态，即 HMM_PFN_VALID | 如果页
面可写，将设置 HMM_PFN_WRITE。


从核心内核的角度表示和管理设备内存
==================================

尝试了几种不同的设计来支持设备内存。第一个使用特定于设备的数据结构来保存有关迁移内存
的信息，HMM 将自身挂接到 mm 代码的各个位置，以处理对设备内存支持的地址的任何访问。
事实证明，这最终复制了 struct page 的大部分字段，并且还需要更新许多内核代码路径才
能理解这种新的内存类型。

大多数内核代码路径从不尝试访问页面后面的内存，而只关心struct page的内容。正因为如此，
HMM 切换到直接使用 struct page 用于设备内存，这使得大多数内核代码路径不知道差异。
我们只需要确保没有人试图从 CPU 端映射这些页面。

移入和移出设备内存
==================

由于 CPU 无法直接访问设备内存，因此设备驱动程序必须使用硬件 DMA 或设备特定的加载/存
储指令来迁移数据。migrate_vma_setup()、migrate_vma_pages() 和
migrate_vma_finalize() 函数旨在使驱动程序更易于编写并集中跨驱动程序的通用代码。

在将页面迁移到设备私有内存之前，需要创建特殊的设备私有 ``struct page`` 。这些将用
作特殊的“交换”页表条目，以便 CPU 进程在尝试访问已迁移到设备专用内存的页面时会发生异常。

这些可以通过以下方式分配和释放::

    struct resource *res;
    struct dev_pagemap pagemap;

    res = request_free_mem_region(&iomem_resource, /* number of bytes */,
                                  "name of driver resource");
    pagemap.type = MEMORY_DEVICE_PRIVATE;
    pagemap.range.start = res->start;
    pagemap.range.end = res->end;
    pagemap.nr_range = 1;
    pagemap.ops = &device_devmem_ops;
    memremap_pages(&pagemap, numa_node_id());

    memunmap_pages(&pagemap);
    release_mem_region(pagemap.range.start, range_len(&pagemap.range));

还有devm_request_free_mem_region(), devm_memremap_pages(),
devm_memunmap_pages() 和 devm_release_mem_region() 当资源可以绑定到 ``struct device``.

整体迁移步骤类似于在系统内存中迁移 NUMA 页面(see :ref:`Page migration <page_migration>`) ，
但这些步骤分为设备驱动程序特定代码和共享公共代码:

1. ``mmap_read_lock()``

   设备驱动程序必须将 ``struct vm_area_struct`` 传递给migrate_vma_setup()，
   因此需要在迁移期间保留 mmap_read_lock() 或 mmap_write_lock()。

2. ``migrate_vma_setup(struct migrate_vma *args)``

   设备驱动初始化了 ``struct migrate_vma`` 的字段，并将该指针传递给
   migrate_vma_setup()。``args->flags`` 字段是用来过滤哪些源页面应该被迁移。
   例如，设置 ``MIGRATE_VMA_SELECT_SYSTEM`` 将只迁移系统内存，设置
   ``MIGRATE_VMA_SELECT_DEVICE_PRIVATE`` 将只迁移驻留在设备私有内存中的页
   面。如果后者被设置， ``args->pgmap_owner`` 字段被用来识别驱动所拥有的设备
   私有页。这就避免了试图迁移驻留在其他设备中的设备私有页。目前，只有匿名的私有VMA
   范围可以被迁移到系统内存和设备私有内存。

   migrate_vma_setup()所做的第一步是用 ``mmu_notifier_invalidate_range_start()``
   和 ``mmu_notifier_invalidate_range_end()`` 调用来遍历设备周围的页表，使
   其他设备的MMU无效，以便在 ``args->src`` 数组中填写要迁移的PFN。
   ``invalidate_range_start()`` 回调传递给一个``struct mmu_notifier_range`` ，
   其 ``event`` 字段设置为MMU_NOTIFY_MIGRATE， ``owner`` 字段设置为传递给
   migrate_vma_setup()的 ``args->pgmap_owner`` 字段。这允许设备驱动跳过无
   效化回调，只无效化那些实际正在迁移的设备私有MMU映射。这一点将在下一节详细解释。


   在遍历页表时，一个 ``pte_none()`` 或 ``is_zero_pfn()`` 条目导致一个有效
   的  “zero” PFN 存储在 ``args->src`` 阵列中。这让驱动分配设备私有内存并清
   除它，而不是复制一个零页。到系统内存或设备私有结构页的有效PTE条目将被
   ``lock_page()``锁定，与LRU隔离（如果系统内存和设备私有页不在LRU上），从进
   程中取消映射，并插入一个特殊的迁移PTE来代替原来的PTE。 migrate_vma_setup()
   还清除了 ``args->dst`` 数组。

3. 设备驱动程序分配目标页面并将源页面复制到目标页面。

   驱动程序检查每个 ``src`` 条目以查看该 ``MIGRATE_PFN_MIGRATE`` 位是否已
   设置并跳过未迁移的条目。设备驱动程序还可以通过不填充页面的 ``dst`` 数组来选
   择跳过页面迁移。

   然后，驱动程序分配一个设备私有 struct page 或一个系统内存页，用 ``lock_page()``
   锁定该页，并将 ``dst`` 数组条目填入::

     dst[i] = migrate_pfn(page_to_pfn(dpage));

   现在驱动程序知道这个页面正在被迁移，它可以使设备私有 MMU 映射无效并将设备私有
   内存复制到系统内存或另一个设备私有页面。由于核心 Linux 内核会处理 CPU 页表失
   效，因此设备驱动程序只需使其自己的 MMU 映射失效。

   驱动程序可以使用 ``migrate_pfn_to_page(src[i])`` 来获取源设备的
   ``struct page`` 面，并将源页面复制到目标设备上，如果指针为 ``NULL`` ，意
   味着源页面没有被填充到系统内存中，则清除目标设备的私有内存。

4. ``migrate_vma_pages()``

   这一步是实际“提交”迁移的地方。

   如果源页是 ``pte_none()`` 或 ``is_zero_pfn()`` 页，这时新分配的页会被插
   入到CPU的页表中。如果一个CPU线程在同一页面上发生异常，这可能会失败。然而，页
   表被锁定，只有一个新页会被插入。如果它失去了竞争，设备驱动将看到
   ``MIGRATE_PFN_MIGRATE`` 位被清除。

   如果源页被锁定、隔离等，源 ``struct page`` 信息现在被复制到目标
   ``struct page`` ，最终完成CPU端的迁移。

5. 设备驱动为仍在迁移的页面更新设备MMU页表，回滚未迁移的页面。

   如果 ``src`` 条目仍然有 ``MIGRATE_PFN_MIGRATE`` 位被设置，设备驱动可以
   更新设备MMU，如果 ``MIGRATE_PFN_WRITE`` 位被设置，则设置写启用位。

6. ``migrate_vma_finalize()``

   这一步用新页的页表项替换特殊的迁移页表项，并释放对源和目的 ``struct page``
   的引用。

7. ``mmap_read_unlock()``

   现在可以释放锁了。

独占访问存储器
==============

一些设备具有诸如原子PTE位的功能，可以用来实现对系统内存的原子访问。为了支持对一
个共享的虚拟内存页的原子操作，这样的设备需要对该页的访问是排他的，而不是来自CPU
的任何用户空间访问。  ``make_device_exclusive_range()`` 函数可以用来使一
个内存范围不能从用户空间访问。

这将用特殊的交换条目替换给定范围内的所有页的映射。任何试图访问交换条目的行为都会
导致一个异常，该异常会通过用原始映射替换该条目而得到恢复。驱动程序会被通知映射已
经被MMU通知器改变，之后它将不再有对该页的独占访问。独占访问被保证持续到驱动程序
放弃页面锁和页面引用为止，这时页面上的任何CPU异常都可以按所述进行。

内存 cgroup (memcg) 和 rss 统计
===============================

目前，设备内存被视为 rss 计数器中的任何常规页面（如果设备页面用于匿名，则为匿名，
如果设备页面用于文件支持页面，则为文件，如果设备页面用于共享内存，则为 shmem）。
这是为了保持现有应用程序的故意选择，这些应用程序可能在不知情的情况下开始使用设备
内存，运行不受影响。

一个缺点是 OOM 杀手可能会杀死使用大量设备内存而不是大量常规系统内存的应用程序，
因此不会释放太多系统内存。在决定以不同方式计算设备内存之前，我们希望收集更多关
于应用程序和系统在存在设备内存的情况下在内存压力下如何反应的实际经验。

对内存 cgroup 做出了相同的决定。设备内存页面根据相同的内存 cgroup 计算，常规
页面将被计算在内。这确实简化了进出设备内存的迁移。这也意味着从设备内存迁移回常规
内存不会失败，因为它会超过内存 cgroup 限制。一旦我们对设备内存的使用方式及其对
内存资源控制的影响有了更多的了解，我们可能会在后面重新考虑这个选择。

请注意，设备内存永远不能由设备驱动程序或通过 GUP 固定，因此此类内存在进程退出时
总是被释放的。或者在共享内存或文件支持内存的情况下，当删除最后一个引用时。
