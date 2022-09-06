.. SPDX-License-Identifier: GPL-2.0

:Original: Documentation/mm/damon/index.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:


==========================
DAMON:数据访问监视器
==========================

DAMON是Linux内核的一个数据访问监控框架子系统。DAMON的核心机制使其成为
（该核心机制详见(Documentation/translations/zh_CN/mm/damon/design.rst)）

 - *准确度* （监测输出对DRAM级别的内存管理足够有用；但可能不适合CPU Cache级别），
 - *轻量级* （监控开销低到可以在线应用），以及
 - *可扩展* （无论目标工作负载的大小，开销的上限值都在恒定范围内）。

因此，利用这个框架，内核的内存管理机制可以做出高级决策。会导致高数据访问监控开销的实
验性内存管理优化工作可以再次进行。同时，在用户空间，有一些特殊工作负载的用户可以编写
个性化的应用程序，以便更好地了解和优化他们的工作负载和系统。

.. toctree::
   :maxdepth: 2

   faq
   design
   api
