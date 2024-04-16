.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/virt/acrn/cpuid.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

 时奎亮 Alex Shi <alexs@kernel.org>

.. _cn_virt_acrn_cpuid:

==============
ACRN CPUID位域
==============

在ACRN超级管理器上运行的客户虚拟机可以使用CPUID检查其一些功能。

ACRN的cpuid函数是:

函数: 0x40000000

返回::

   eax = 0x40000010
   ebx = 0x4e524341
   ecx = 0x4e524341
   edx = 0x4e524341

注意，ebx，ecx和edx中的这个值对应于字符串“ACRNACRNACRN”。eax中的值对应于这个叶子
中存在的最大cpuid函数，如果将来有更多的函数加入，将被更新。

函数: define ACRN_CPUID_FEATURES (0x40000001)

返回::

          ebx, ecx, edx
          eax = an OR'ed group of (1 << flag)

其中 ``flag`` 的定义如下:

================================= =========== ================================
标志                              值          描述
================================= =========== ================================
ACRN_FEATURE_PRIVILEGED_VM        0           客户虚拟机是一个有特权的虚拟机
================================= =========== ================================

函数: 0x40000010

返回::

          ebx, ecx, edx
          eax = (Virtual) TSC frequency in kHz.
