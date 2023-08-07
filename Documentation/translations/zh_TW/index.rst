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
   作，歡迎提交補丁給胡皓文 <src.res@email.cn>。

許可證文檔
----------

下面的文檔介紹了Linux內核原始碼的許可證（GPLv2）、如何在原始碼樹中正確標記
單個文件的許可證、以及指向完整許可證文本的連結。

Documentation/translations/zh_TW/process/license-rules.rst

用戶文檔
--------

下面的手冊是爲內核用戶編寫的——即那些試圖讓它在給定系統上以最佳方式工作的
用戶。

.. toctree::
   :maxdepth: 2

   admin-guide/index

TODOList:

* kbuild/index

固件相關文檔
------------

下列文檔描述了內核需要的平台固件相關信息。

TODOList:

* firmware-guide/index
* devicetree/index

應用程式開發人員文檔
--------------------

用戶空間API手冊涵蓋了描述應用程式開發人員可見內核接口方面的文檔。

TODOlist:

* userspace-api/index

內核開發簡介
------------

這些手冊包含有關如何開發內核的整體信息。內核社區非常龐大，一年下來有數千名
開發人員做出貢獻。與任何大型社區一樣，知道如何完成任務將使得更改合併的過程
變得更加容易。

.. toctree::
   :maxdepth: 2

   process/index

TODOList:

* dev-tools/index
* doc-guide/index
* kernel-hacking/index
* trace/index
* maintainer/index
* fault-injection/index
* livepatch/index
* rust/index

內核API文檔
-----------

以下手冊從內核開發人員的角度詳細介紹了特定的內核子系統是如何工作的。這裡的
大部分信息都是直接從內核原始碼獲取的，並根據需要添加補充材料（或者至少是在
我們設法添加的時候——可能不是所有的都是有需要的）。

.. toctree::
   :maxdepth: 2

   cpu-freq/index
   filesystems/index

TODOList:

* driver-api/index
* core-api/index
* locking/index
* accounting/index
* block/index
* cdrom/index
* ide/index
* fb/index
* fpga/index
* hid/index
* i2c/index
* iio/index
* isdn/index
* infiniband/index
* leds/index
* netlabel/index
* networking/index
* pcmcia/index
* power/index
* target/index
* timers/index
* spi/index
* w1/index
* watchdog/index
* virt/index
* input/index
* hwmon/index
* gpu/index
* security/index
* sound/index
* crypto/index
* mm/index
* bpf/index
* usb/index
* PCI/index
* scsi/index
* misc-devices/index
* scheduler/index
* mhi/index

體系結構無關文檔
----------------

TODOList:

* asm-annotations

特定體系結構文檔
----------------

.. toctree::
   :maxdepth: 2

   arch/arm64/index

TODOList:

* arch

其他文檔
--------

有幾份未排序的文檔似乎不適合放在文檔的其他部分，或者可能需要進行一些調整和/或
轉換爲reStructureText格式，也有可能太舊。

TODOList:

* staging/index
* watch_queue

目錄和表格
----------

* :ref:`genindex`

.. raw:: latex

	}\kerneldocEndTC
