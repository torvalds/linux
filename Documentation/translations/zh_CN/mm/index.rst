.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/mm/index.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

=================
Linux内存管理文档
=================

这是一份关于了解Linux的内存管理子系统的指南。如果你正在寻找关于简单分配内存的
建议，请参阅内存分配指南
(Documentation/translations/zh_CN/core-api/memory-allocation.rst)。
关于控制和调整的指南，请看管理指南
(Documentation/translations/zh_CN/admin-guide/mm/index.rst)。


.. toctree::
   :maxdepth: 1

   highmem

该处剩余文档待原始文档有内容后翻译。


遗留文档
========

这是一个关于Linux内存管理（MM）子系统内部的旧文档的集合，其中有不同层次的细节，
包括注释和邮件列表的回复，用于阐述数据结构和算法的描述。它应该被很好地整合到上述
结构化的文档中，如果它已经完成了它的使命，可以删除。

.. toctree::
   :maxdepth: 1

   active_mm
   balance
   damon/index
   free_page_reporting
   ksm
   hmm
   hwpoison
   hugetlbfs_reserv
   memory-model
   mmu_notifier
   numa
   overcommit-accounting
   page_frags
   page_migration
   page_owner
   page_table_check
   remap_file_pages
   split_page_table_lock
   vmalloced-kernel-stacks
   z3fold
   zsmalloc

TODOLIST:
* arch_pgtable_helpers
* free_page_reporting
* hugetlbfs_reserv
* slub
* transhuge
* unevictable-lru
