.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/networking/netif-msg.rst

:翻译:

   王亚鑫 Wang Yaxin <wang.yaxin@zte.com.cn>

================
网络接口消息级别
================

网络接口消息级别设置的设计方案。

历史背景
--------

调试消息接口的设计遵循并受制于向后兼容性及历史实践。理解其发展历史有助于把握
当前实践，并将其与旧版驱动代码相关联。

自Linux诞生之初，每个网络设备驱动均包含一个本地整型变量以控制调试消息级别。
消息级别范围为0至7，数值越大表示输出越详细。

消息级别的定义在3级之后未明确细化，但实际实现通常与指定级别相差±1。驱动程序
成熟后，冗余的详细级别消息常被移除。

  - 0  最简消息，仅显示致命错误的关键信息。
  - 1  标准消息，初始化状态。无运行时消息。
  - 2  特殊介质选择消息，通常由定时器驱动。
  - 3  接口开启和停止消息，包括正常状态信息。
  - 4  Tx/Rx帧错误消息及异常驱动操作。
  - 5  Tx数据包队列信息、中断事件。
  - 6  每个完成的Tx数据包和接收的Rx数据包状态。
  - 7  Tx/Rx数据包初始内容。

最初，该消息级别变量在各驱动中具有唯一名称（如"lance_debug"），便于通过
内核符号调试器定位和修改其设置。模块化内核出现后，变量统一重命名为"debug"，
并作为模块参数设置。

这种方法效果良好。然而，人们始终对附加功能存在需求。多年来，以下功能逐渐
成为合理且易于实现的增强方案：

  - 通过ioctl()调用修改消息级别。
  - 按接口而非驱动设置消息级别。
  - 对发出的消息类型进行更具选择性的控制。

netif_msg 建议添加了这些功能，仅带来了轻微的复杂性增加和代码规模增长。

推荐方案如下：

  - 保留驱动级整型变量"debug"作为模块参数，默认值为'1'。

  - 添加一个名为 "msg_enable" 的接口私有变量。该变量是位图而非级别，
    并按如下方式初始化::

       1 << debug

     或更精确地说::

	debug < 0 ? 0 : 1 << min(sizeof(int)-1, debug)

    消息应从以下形式更改::

      if (debug > 1)
	   printk(MSG_DEBUG "%s: ...

    改为::

      if (np->msg_enable & NETIF_MSG_LINK)
	   printk(MSG_DEBUG "%s: ...

消息级别命名对应关系


  =========   =================== ============
  旧级别       名称               	位位置
  =========   =================== ============
    1         NETIF_MSG_PROBE       0x0002
    2         NETIF_MSG_LINK        0x0004
    2         NETIF_MSG_TIMER       0x0004
    3         NETIF_MSG_IFDOWN      0x0008
    3         NETIF_MSG_IFUP        0x0008
    4         NETIF_MSG_RX_ERR      0x0010
    4         NETIF_MSG_TX_ERR      0x0010
    5         NETIF_MSG_TX_QUEUED   0x0020
    5         NETIF_MSG_INTR        0x0020
    6         NETIF_MSG_TX_DONE     0x0040
    6         NETIF_MSG_RX_STATUS   0x0040
    7         NETIF_MSG_PKTDATA     0x0080
  =========   =================== ============
