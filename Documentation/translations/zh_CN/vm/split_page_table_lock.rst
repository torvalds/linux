:Original: Documentation/vm/split_page_table_lock.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:


=================================
分页表锁（split page table lock）
=================================

最初，mm->page_table_lock spinlock保护了mm_struct的所有页表。但是这种方
法导致了多线程应用程序的缺页异常可扩展性差，因为对锁的争夺很激烈。为了提高可扩
展性，我们引入了分页表锁。

有了分页表锁，我们就有了单独的每张表锁来顺序化对表的访问。目前，我们对PTE和
PMD表使用分页锁。对高层表的访问由mm->page_table_lock保护。

有一些辅助工具来锁定/解锁一个表和其他访问器函数:

 - pte_offset_map_lock()
	映射pte并获取PTE表锁，返回所取锁的指针；
 - pte_unmap_unlock()
	解锁和解映射PTE表；
 - pte_alloc_map_lock()
	如果需要的话，分配PTE表并获取锁，如果分配失败，返回已获取的锁的指针
	或NULL；
 - pte_lockptr()
	返回指向PTE表锁的指针；
 - pmd_lock()
	取得PMD表锁，返回所取锁的指针。
 - pmd_lockptr()
	返回指向PMD表锁的指针；

如果CONFIG_SPLIT_PTLOCK_CPUS（通常为4）小于或等于NR_CPUS，则在编译
时启用PTE表的分页表锁。如果分页锁被禁用，所有的表都由mm->page_table_lock
来保护。

如果PMD表启用了分页锁，并且架构支持它，那么PMD表的分页锁就会被启用（见
下文）。

Hugetlb 和分页表锁
==================

Hugetlb可以支持多种页面大小。我们只对PMD级别使用分页锁，但不对PUD使用。

Hugetlb特定的辅助函数:

 - huge_pte_lock()
	对PMD_SIZE页面采取pmd分割锁，否则mm->page_table_lock；
 - huge_pte_lockptr()
	返回指向表锁的指针。

架构对分页表锁的支持
====================

没有必要特别启用PTE分页表锁：所有需要的东西都由pgtable_pte_page_ctor()
和pgtable_pte_page_dtor()完成，它们必须在PTE表分配/释放时被调用。

确保架构不使用slab分配器来分配页表：slab使用page->slab_cache来分配其页
面。这个区域与page->ptl共享存储。

PMD分页锁只有在你有两个以上的页表级别时才有意义。

启用PMD分页锁需要在PMD表分配时调用pgtable_pmd_page_ctor()，在释放时调
用pgtable_pmd_page_dtor()。

分配通常发生在pmd_alloc_one()中，释放发生在pmd_free()和pmd_free_tlb()
中，但要确保覆盖所有的PMD表分配/释放路径：即X86_PAE在pgd_alloc()中预先
分配一些PMD。

一切就绪后，你可以设置CONFIG_ARCH_ENABLE_SPLIT_PMD_PTLOCK。

注意：pgtable_pte_page_ctor()和pgtable_pmd_page_ctor()可能失败--必
须正确处理。

page->ptl
=========

page->ptl用于访问分割页表锁，其中'page'是包含该表的页面struct page。它
与page->private（以及union中的其他几个字段）共享存储。

为了避免增加struct page的大小并获得最佳性能，我们使用了一个技巧:

 - 如果spinlock_t适合于long，我们使用page->ptr作为spinlock，这样我们
   就可以避免间接访问并节省一个缓存行。
 - 如果spinlock_t的大小大于long的大小，我们使用page->ptl作为spinlock_t
   的指针并动态分配它。这允许在启用DEBUG_SPINLOCK或DEBUG_LOCK_ALLOC的
   情况下使用分页锁，但由于间接访问而多花了一个缓存行。

PTE表的spinlock_t分配在pgtable_pte_page_ctor()中，PMD表的spinlock_t
分配在pgtable_pmd_page_ctor()中。

请不要直接访问page->ptl - -使用适当的辅助函数。
