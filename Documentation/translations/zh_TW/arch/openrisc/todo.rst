.. include:: ../../disclaimer-zh_TW.rst

:Original: Documentation/arch/openrisc/todo.rst

:翻譯:

 司延騰 Yanteng Si <siyanteng@loongson.cn>

.. _tw_openrisc_todo.rst:

========
待辦事項
========

OpenRISC Linux的移植已經完全投入使用，並且從 2.6.35 開始就一直在上游同步。
然而，還有一些剩餘的項目需要在未來幾個月內完成。 下面是一個即將進行調查的已知
不盡完美的項目列表，即我們的待辦事項列表。

-   實現其餘的DMA API……dma_map_sg等。

-   完成重命名清理工作……代碼中提到了or32，這是架構的一個老名字。 我們
    已經確定的名字是or1k，這個改變正在以緩慢積累的方式進行。 目前，or32相當
    於or1k。

