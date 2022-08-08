.. include:: ../disclaimer-zh_CN.rst

:Original: :doc:`../../../openrisc/todo`
:Translator: Yanteng Si <siyanteng@loongson.cn>

.. _cn_openrisc_todo.rst:

========
待办事项
========

OpenRISC Linux的移植已经完全投入使用，并且从 2.6.35 开始就一直在上游同步。
然而，还有一些剩余的项目需要在未来几个月内完成。 下面是一个即将进行调查的已知
不尽完美的项目列表，即我们的待办事项列表。

-   实现其余的DMA API……dma_map_sg等。

-   完成重命名清理工作……代码中提到了or32，这是架构的一个老名字。 我们
    已经确定的名字是or1k，这个改变正在以缓慢积累的方式进行。 目前，or32相当
    于or1k。
