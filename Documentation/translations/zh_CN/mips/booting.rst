.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: :doc:`../../../mips/booting`
:Translator: Yanteng Si <siyanteng@loongson.cn>

.. _cn_booting:

BMIPS设备树引导
------------------------

  一些bootloaders只支持在内核镜像开始地址处的单一入口点。而其它
  bootloaders将跳转到ELF的开始地址处。两种方案都支持的；因为
  CONFIG_BOOT_RAW=y and CONFIG_NO_EXCEPT_FILL=y, 所以第一条指令
  会立即跳转到kernel_entry()入口处执行。

  与arch/arm情况(b)类似，dt感知的引导加载程序需要设置以下寄存器:

         a0 : 0

         a1 : 0xffffffff

         a2 : RAM中指向设备树块的物理指针(在chapterII中定义)。
              设备树可以位于前512MB物理地址空间(0x00000000 -
              0x1fffffff)的任何位置，以64位边界对齐。

  传统bootloaders不会使用这样的约定，并且它们不传入DT块。
  在这种情况下，Linux将通过选中CONFIG_DT_*查找DTB。

  以上约定只在32位系统中定义，因为目前没有任何64位的BMIPS实现。
