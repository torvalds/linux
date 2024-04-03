.. SPDX-License-Identifier: GPL-2.0

.. include:: ../../disclaimer-zh_TW.rst

:Original: Documentation/arch/mips/booting.rst

:翻譯:

 司延騰 Yanteng Si <siyanteng@loongson.cn>

.. _tw_booting:

BMIPS設備樹引導
------------------------

  一些bootloaders只支持在內核鏡像開始地址處的單一入口點。而其它
  bootloaders將跳轉到ELF的開始地址處。兩種方案都支持的；因爲
  CONFIG_BOOT_RAW=y and CONFIG_NO_EXCEPT_FILL=y, 所以第一條指令
  會立即跳轉到kernel_entry()入口處執行。

  與arch/arm情況(b)類似，dt感知的引導加載程序需要設置以下寄存器:

         a0 : 0

         a1 : 0xffffffff

         a2 : RAM中指向設備樹塊的物理指針(在chapterII中定義)。
              設備樹可以位於前512MB物理地址空間(0x00000000 -
              0x1fffffff)的任何位置，以64位邊界對齊。

  傳統bootloaders不會使用這樣的約定，並且它們不傳入DT塊。
  在這種情況下，Linux將通過選中CONFIG_DT_*查找DTB。

  以上約定只在32位系統中定義，因爲目前沒有任何64位的BMIPS實現。

