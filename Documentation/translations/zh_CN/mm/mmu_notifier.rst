:Original: Documentation/mm/mmu_notifier.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:



什么时候需要页表锁内通知？
==========================

当清除一个pte/pmd时，我们可以选择通过在页表锁下（通知版的\*_clear_flush调用
mmu_notifier_invalidate_range）通知事件。但这种通知并不是在所有情况下都需要的。

对于二级TLB（非CPU TLB），如IOMMU TLB或设备TLB（当设备使用类似ATS/PASID的东西让
IOMMU走CPU页表来访问进程的虚拟地址空间）。只有两种情况需要在清除pte/pmd时在持有页
表锁的同时通知这些二级TLB：

  A) 在mmu_notifier_invalidate_range_end()之前，支持页的地址被释放。
  B) 一个页表项被更新以指向一个新的页面（COW，零页上的写异常，__replace_page()，...）。

情况A很明显，你不想冒风险让设备写到一个现在可能被一些完全不同的任务使用的页面。

情况B更加微妙。为了正确起见，它需要按照以下序列发生:

  - 上页表锁
  - 清除页表项并通知 ([pmd/pte]p_huge_clear_flush_notify())
  - 设置页表项以指向新页

如果在设置新的pte/pmd值之前，清除页表项之后没有进行通知，那么你就会破坏设备的C11或
C++11等内存模型。

考虑以下情况（设备使用类似于ATS/PASID的功能）。

两个地址addrA和addrB，这样|addrA - addrB| >= PAGE_SIZE，我们假设它们是COW的
写保护（B的其他情况也适用）。

::

 [Time N] --------------------------------------------------------------------
 CPU-thread-0  {尝试写到addrA}
 CPU-thread-1  {尝试写到addrB}
 CPU-thread-2  {}
 CPU-thread-3  {}
 DEV-thread-0  {读取addrA并填充设备TLB}
 DEV-thread-2  {读取addrB并填充设备TLB}
 [Time N+1] ------------------------------------------------------------------
 CPU-thread-0  {COW_step0: {mmu_notifier_invalidate_range_start(addrA)}}
 CPU-thread-1  {COW_step0: {mmu_notifier_invalidate_range_start(addrB)}}
 CPU-thread-2  {}
 CPU-thread-3  {}
 DEV-thread-0  {}
 DEV-thread-2  {}
 [Time N+2] ------------------------------------------------------------------
 CPU-thread-0  {COW_step1: {更新页表以指向addrA的新页}}
 CPU-thread-1  {COW_step1: {更新页表以指向addrB的新页}}
 CPU-thread-2  {}
 CPU-thread-3  {}
 DEV-thread-0  {}
 DEV-thread-2  {}
 [Time N+3] ------------------------------------------------------------------
 CPU-thread-0  {preempted}
 CPU-thread-1  {preempted}
 CPU-thread-2  {写入addrA，这是对新页面的写入}
 CPU-thread-3  {}
 DEV-thread-0  {}
 DEV-thread-2  {}
 [Time N+3] ------------------------------------------------------------------
 CPU-thread-0  {preempted}
 CPU-thread-1  {preempted}
 CPU-thread-2  {}
 CPU-thread-3  {写入addrB，这是一个写入新页的过程}
 DEV-thread-0  {}
 DEV-thread-2  {}
 [Time N+4] ------------------------------------------------------------------
 CPU-thread-0  {preempted}
 CPU-thread-1  {COW_step3: {mmu_notifier_invalidate_range_end(addrB)}}
 CPU-thread-2  {}
 CPU-thread-3  {}
 DEV-thread-0  {}
 DEV-thread-2  {}
 [Time N+5] ------------------------------------------------------------------
 CPU-thread-0  {preempted}
 CPU-thread-1  {}
 CPU-thread-2  {}
 CPU-thread-3  {}
 DEV-thread-0  {从旧页中读取addrA}
 DEV-thread-2  {从新页面读取addrB}

所以在这里，因为在N+2的时候，清空页表项没有和通知一起作废二级TLB，设备在看到addrA的新值之前
就看到了addrB的新值。这就破坏了设备的总内存序。

当改变一个pte的写保护或指向一个新的具有相同内容的写保护页（KSM）时，将mmu_notifier_invalidate_range
调用延迟到页表锁外的mmu_notifier_invalidate_range_end()是可以的。即使做页表更新的线程
在释放页表锁后但在调用mmu_notifier_invalidate_range_end()前被抢占，也是如此。
