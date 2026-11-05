.. SPDX-License-Identifier: GPL-2.0

.. raw:: latex

	\renewcommand\thesection*
	\renewcommand\thesubsection*

.. include:: ../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/process/index.rst <process_index>`
:Translator: Alex Shi <alex.shi@linux.alibaba.com>
             Hu Haowen <2023002089@link.tyut.edu.cn>
             Chen-Yu Yeh <chenyou910331@gmail.com>

.. _tw_process_index:

========================
與Linux核心社群一起工作
========================

你想成為Linux核心開發人員嗎？歡迎之至！在學習許多關於核心的技術知識的同時，
瞭解我們社群的工作方式也很重要。閱讀這些文件可以讓您以更輕鬆的、麻煩更少的
方式將更改合併到核心。

核心開發如何運作的介紹
----------------------

請先閱讀這些文件：理解這裡的內容將使你更順利地進入核心社群。

.. toctree::
   :maxdepth: 1

   howto
   development-process
   submitting-patches
   submit-checklist

核心開發者的工具與技術指南
--------------------------

這是核心開發者應該熟悉的材料集合。

.. toctree::
   :maxdepth: 1

   programming-language
   coding-style
   email-clients
   volatile-considered-harmful

TODOList:

* changes
* maintainer-pgp-guide
* applying-patches
* backporting
* adding-syscalls
* botching-up-ioctls

政策指南與開發者聲明
--------------------

這些是我們在核心社群（以及更廣範圍）中努力遵循的規則。

.. toctree::
   :maxdepth: 1

   license-rules
   code-of-conduct
   code-of-conduct-interpretation
   kernel-enforcement-statement
   kernel-driver-statement
   stable-api-nonsense
   stable-kernel-rules
   management-style

TODOList:

* contribution-maturity-model
* researcher-guidelines
* generated-content
* coding-assistants
* conclave

處理缺陷
--------

缺陷是無法避免的；正確地處理它們非常重要。下面的文件提供了關於除錯的一般
建議，並描述了我們處理幾類特殊缺陷——迴歸和安全問題——的政策。

.. toctree::
   :maxdepth: 1

   embargoed-hardware-issues

TODOList:

* debugging/index
* handling-regressions
* security-bugs
* threat-model
* cve

維護者資訊
----------

如何找到會接受你補丁的人。

TODOList:

* maintainer-handbooks
* maintainers

其他材料
--------

這裡是一些大多數開發者會感興趣的其他社群指南：

.. toctree::
   :maxdepth: 1

   magic-number

TODOList:

* kernel-docs
* deprecated

.. only::  subproject and html

   目錄
   ====

   * :ref:`genindex`
