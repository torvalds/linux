.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/vm/index.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

=================
Linux内存管理文档
=================

这是一个关于Linux内存管理（mm）子系统内部的文档集，其中有不同层次的细节，包括注释
和邮件列表的回复，用于阐述数据结构和算法的基本情况。如果你正在寻找关于简单分配内存的建
议，请参阅(Documentation/translations/zh_CN/core-api/memory-allocation.rst)。
对于控制和调整指南，请参阅(Documentation/admin-guide/mm/index)。
TODO:待引用文档集被翻译完毕后请及时修改此处）

.. toctree::
   :maxdepth: 1

   active_mm
   balance
   damon/index
   free_page_reporting
   highmem
   ksm

TODOLIST:
* arch_pgtable_helpers
* free_page_reporting
* frontswap
* hmm
* hwpoison
* hugetlbfs_reserv
* memory-model
* mmu_notifier
* numa
* overcommit-accounting
* page_migration
* page_frags
* page_owner
* page_table_check
* remap_file_pages
* slub
* split_page_table_lock
* transhuge
* unevictable-lru
* vmalloced-kernel-stacks
* z3fold
* zsmalloc
