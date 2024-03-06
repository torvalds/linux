.. include:: ../../disclaimer-zh_TW.rst

:Original:   Documentation/admin-guide/mm/index.rst

:翻譯:

  徐鑫 xu xin <xu.xin16@zte.com.cn>


========
內存管理
========

Linux內存管理子系統，顧名思義，是負責系統中的內存管理。它包括了虛擬內存與請求
分頁的實現，內核內部結構和用戶空間程序的內存分配、將文件映射到進程地址空間以
及許多其他很酷的事情。

Linux內存管理是一個具有許多可配置設置的複雜系統, 且這些設置中的大多數都可以通
過 ``/proc`` 文件系統獲得，並且可以使用 ``sysctl`` 進行查詢和調整。這些API接
口被描述在Documentation/admin-guide/sysctl/vm.rst文件和 `man 5 proc`_ 中。

.. _man 5 proc: http://man7.org/linux/man-pages/man5/proc.5.html

Linux內存管理有它自己的術語，如果你還不熟悉它，請考慮閱讀下面參考：
Documentation/admin-guide/mm/concepts.rst.

在此目錄下，我們詳細描述瞭如何與Linux內存管理中的各種機制交互。

.. toctree::
   :maxdepth: 1

   damon/index
   ksm

Todolist:
* concepts
* cma_debugfs
* hugetlbpage
* idle_page_tracking
* memory-hotplug
* nommu-mmap
* numa_memory_policy
* numaperf
* pagemap
* soft-dirty
* swap_numa
* transhuge
* userfaultfd
* zswap

