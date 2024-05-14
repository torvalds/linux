.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/arm64/hugetlbpage.rst <hugetlbpage_index>`

Translator: Bailu Lin <bailu.lin@vivo.com>
            Hu Haowen <src.res@email.cn>

=====================
ARM64中的 HugeTLBpage
=====================

大頁依靠有效利用 TLBs 來提高地址翻譯的性能。這取決於以下
兩點 -

  - 大頁的大小
  - TLBs 支持的條目大小

ARM64 接口支持2種大頁方式。

1) pud/pmd 級別的塊映射
-----------------------

這是常規大頁，他們的 pmd 或 pud 頁面表條目指向一個內存塊。
不管 TLB 中支持的條目大小如何，塊映射可以減少翻譯大頁地址
所需遍歷的頁表深度。

2) 使用連續位
-------------

架構中轉換頁表條目(D4.5.3, ARM DDI 0487C.a)中提供一個連續
位告訴 MMU 這個條目是一個連續條目集的一員，它可以被緩存在單
個 TLB 條目中。

在 Linux 中連續位用來增加 pmd 和 pte(最後一級)級別映射的大
小。受支持的連續頁表條目數量因頁面大小和頁表級別而異。


支持以下大頁尺寸配置 -

  ====== ========   ====    ========    ===
  -      CONT PTE    PMD    CONT PMD    PUD
  ====== ========   ====    ========    ===
  4K:         64K     2M         32M     1G
  16K:         2M    32M          1G
  64K:         2M   512M         16G
  ====== ========   ====    ========    ===

