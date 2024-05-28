.. SPDX-License-Identifier: GPL-2.0

.. raw:: latex

	\renewcommand\thesection*
	\renewcommand\thesubsection*
	\kerneldocCJKon
	\kerneldocBeginTC{

.. _linux_doc_zh_tw:

繁體中文翻譯
============


.. note::
   內核文檔繁體中文版的翻譯工作正在進行中。如果您願意並且有時間參與這項工
   作，歡迎提交補丁給胡皓文 <2023002089@link.tyut.edu.cn>。

與Linux 內核社區一起工作
------------------------

與內核開發社區進行協作並將工作推向上游的基本指南。

.. toctree::
   :maxdepth: 1

   process/development-process
   process/submitting-patches
   行爲準則 <process/code-of-conduct>
   完整開發流程文檔 <process/index>

TODOList:

* maintainer/index

內部API文檔
-----------

開發人員使用的內核內部交互接口手冊。

TODOList:

* core-api/index
* driver-api/index
* 內核中的鎖 <locking/index>
* subsystem-apis

開發工具和流程
--------------

爲所有內核開發人員提供有用信息的各種其他手冊。

.. toctree::
   :maxdepth: 1

   process/license-rules
   dev-tools/index

TODOList:

* doc-guide/index
* dev-tools/testing-overview
* kernel-hacking/index
* rust/index
* trace/index
* fault-injection/index
* livepatch/index

面向用戶的文檔
--------------

下列手冊針對
希望內核在給定系統上以最佳方式工作的*用戶*，
和查找內核用戶空間API信息的程序開發人員。

.. toctree::
   :maxdepth: 1

   admin-guide/index
   admin-guide/reporting-issues.rst

TODOList:

* userspace-api/index
* 內核構建系統 <kbuild/index>
* 用戶空間工具 <tools/index>

也可參考獨立於內核文檔的 `Linux 手冊頁 <https://www.kernel.org/doc/man-pages/>`_ 。

固件相關文檔
------------

下列文檔描述了內核需要的平臺固件相關信息。

TODOList:

* devicetree/index
* firmware-guide/index

體系結構文檔
------------

.. toctree::
   :maxdepth: 1

   arch/index

其他文檔
--------

有幾份未分類的文檔似乎不適合放在文檔的其他部分，或者可能需要進行一些調整和/或
轉換爲reStructureText格式，也有可能太舊。

TODOList:

* staging/index

術語表
------

TODOList:

* glossary


索引和表格
----------

* :ref:`genindex`

.. raw:: latex

	}\kerneldocEndTC
