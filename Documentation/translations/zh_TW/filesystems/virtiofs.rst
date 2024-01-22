.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/filesystems/virtiofs.rst <virtiofs_index>`

譯者
::

	中文版維護者： 王文虎 Wang Wenhu <wenhu.wang@vivo.com>
	中文版翻譯者： 王文虎 Wang Wenhu <wenhu.wang@vivo.com>
	中文版校譯者： 王文虎 Wang Wenhu <wenhu.wang@vivo.com>
	繁體中文版校譯者：胡皓文 Hu Haowen <2023002089@link.tyut.edu.cn>

===========================================
virtiofs: virtio-fs 主機<->客機共享文件系統
===========================================

- Copyright (C) 2020 Vivo Communication Technology Co. Ltd.

介紹
====
Linux的virtiofs文件系統實現了一個半虛擬化VIRTIO類型“virtio-fs”設備的驅動，通過該\
類型設備實現客機<->主機文件系統共享。它允許客機掛載一個已經導出到主機的目錄。

客機通常需要訪問主機或者遠程系統上的文件。使用場景包括：在新客機安裝時讓文件對其\
可見；從主機上的根文件系統啓動；對無狀態或臨時客機提供持久存儲和在客機之間共享目錄。

儘管在某些任務可能通過使用已有的網絡文件系統完成，但是卻需要非常難以自動化的配置\
步驟，且將存儲網絡暴露給客機。而virtio-fs設備通過提供不經過網絡的文件系統訪問文件\
的設計方式解決了這些問題。

另外，virto-fs設備發揮了主客機共存的優點提高了性能，並且提供了網絡文件系統所不具備
的一些語義功能。

用法
====
以``myfs``標籤將文件系統掛載到``/mnt``:

.. code-block:: sh

  guest# mount -t virtiofs myfs /mnt

請查閱 https://virtio-fs.gitlab.io/ 瞭解配置QEMU和virtiofsd守護程序的詳細信息。

內幕
====
由於virtio-fs設備將FUSE協議用於文件系統請求，因此Linux的virtiofs文件系統與FUSE文\
件系統客戶端緊密集成在一起。客機充當FUSE客戶端而主機充當FUSE服務器，內核與用戶空\
間之間的/dev/fuse接口由virtio-fs設備接口代替。

FUSE請求被置於虛擬隊列中由主機處理。主機填充緩衝區中的響應部分，而客機處理請求的完成部分。

將/dev/fuse映射到虛擬隊列需要解決/dev/fuse和虛擬隊列之間語義上的差異。每次讀取\
/dev/fuse設備時，FUSE客戶端都可以選擇要傳輸的請求，從而可以使某些請求優先於其他\
請求。虛擬隊列有其隊列語義，無法更改已入隊請求的順序。在虛擬隊列已滿的情況下尤
其關鍵，因爲此時不可能加入高優先級的請求。爲了解決此差異，virtio-fs設備採用“hiprio”\
（高優先級）虛擬隊列，專門用於有別於普通請求的高優先級請求。


