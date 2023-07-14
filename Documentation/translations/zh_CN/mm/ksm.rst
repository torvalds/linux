.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/mm/ksm.rst

:翻译:

   徐鑫 xu xin <xu.xin16@zte.com.cn>

============
内核同页合并
============

KSM 是一种节省内存的数据去重功能，由CONFIG_KSM=y启用，并在2.6.32版本时被添加
到Linux内核。详见 ``mm/ksm.c`` 的实现，以及http://lwn.net/Articles/306704和
https://lwn.net/Articles/330589

KSM的用户空间的接口在Documentation/translations/zh_CN/admin-guide/mm/ksm.rst
文档中有描述。

设计
====

概述
----

概述内容请见mm/ksm.c文档中的“DOC: Overview”

逆映射
------
KSM维护着稳定树中的KSM页的逆映射信息。

当KSM页面的共享数小于 ``max_page_sharing`` 的虚拟内存区域(VMAs)时，则代表了
KSM页的稳定树其中的节点指向了一个ksm_rmap_item结构体类型的列表。同时，这个KSM页
的 ``page->mapping`` 指向了该稳定树节点。

如果共享数超过了阈值，KSM将给稳定树添加第二个维度。稳定树就变成链接一个或多
个稳定树"副本"的"链"。每个副本都保留KSM页的逆映射信息，其中 ``page->mapping``
指向该"副本"。

每个链以及链接到该链中的所有"副本"强制不变的是，它们代表了相同的写保护内存
内容，尽管任中一个"副本"是由同一片内存区的不同的KSM复制页所指向的。

这样一来，相比与无限的逆映射链表，稳定树的查找计算复杂性不受影响。但在稳定树
本身中不能有重复的KSM页面内容仍然是强制要求。

由 ``max_page_sharing`` 强制决定的数据去重限制是必要的，以此来避免虚拟内存
rmap链表变得过大。rmap的遍历具有O(N)的复杂度，其中N是共享页面的rmap_项（即
虚拟映射）的数量，而这个共享页面的节点数量又被 ``max_page_sharing`` 所限制。
因此，这有效地将线性O(N)计算复杂度从rmap遍历中分散到不同的KSM页面上。ksmd进
程在稳定节点"链"上的遍历也是O(N)，但这个N是稳定树"副本"的数量，而不是rmap项
的数量，因此它对ksmd性能没有显著影响。实际上，最佳稳定树"副本"的候选节点将
保留在"副本"列表的开头。

``max_page_sharing`` 的值设置得高了会促使更快的内存合并（因为将有更少的稳定
树副本排队进入稳定节点chain->hlist）和更高的数据去重系数，但代价是在交换、压
缩、NUMA平衡和页面迁移过程中可能导致KSM页的最大rmap遍历速度较慢。

``stable_node_dups/stable_node_chains`` 的比值还受 ``max_page_sharing`` 调控
的影响，高比值可能意味着稳定节点dup中存在碎片，这可以通过在ksmd中引入碎片算
法来解决，该算法将rmap项从一个稳定节点dup重定位到另一个稳定节点dup，以便释放
那些仅包含极少rmap项的稳定节点"dup"，但这可能会增加ksmd进程的CPU使用率，并可
能会减慢应用程序在KSM页面上的只读计算。

KSM会定期扫描稳定节点"链"中链接的所有稳定树"副本"，以便删减过时了的稳定节点。
这种扫描的频率由 ``stable_node_chains_prune_millisecs`` 这个sysfs 接口定义。

参考
====
内核代码请见mm/ksm.c。
涉及的函数(mm_slot  ksm_scan  stable_node  rmap_item)。
