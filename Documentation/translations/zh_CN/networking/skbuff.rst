.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/networking/skbuff.rst

:翻译:

   王亚鑫 Wang Yaxin <wang.yaxin@zte.com.cn>

struct sk_buff
==============

:c:type:`sk_buff` 是表示数据包的主要网络结构体。

基本sk_buff几何结构
-------------------

.. kernel-doc:: include/linux/skbuff.h
   :doc: Basic sk_buff geometry

共享skb和skb克隆
----------------

:c:member:`sk_buff.users` 是一个简单的引用计数，允许
多个实体保持 struct sk_buff 存活。 ``sk_buff.users != 1``
的 skb 被称为共享 skb（参见 skb_shared()）。

skb_clone() 允许快速复制 skb。不会复制任何数据缓冲区，
但调用者会获得一个新的元数据结构体（struct sk_buff）。
&skb_shared_info.refcount 表示指向同一数据包数据的
skb 数量（即克隆）。

数据引用和无头skb
-----------------

.. kernel-doc:: include/linux/skbuff.h
   :doc: dataref and headerless skbs

校验和信息
----------

.. kernel-doc:: include/linux/skbuff.h
   :doc: skb checksums
