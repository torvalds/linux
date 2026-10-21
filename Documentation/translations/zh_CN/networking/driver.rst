.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/networking/driver.rst

:翻译:

	陈思为 Siwei Chen <businiaoanka@anka1.top>

================
Softnet 驱动问题
================

探测指南
========

地址校验
--------

为设备获取的任何硬件层地址都应进行校验。例如，以太网地址应使用
linux/etherdevice.h 中的 is_valid_ether_addr() 进行检查。

关闭/停止指南
=============

静默
----

ndo_stop 例程被调用后，硬件不得再收发任何数据。所有在途报文都必须中止。
如有必要，应轮询或等待任何复位命令完成。

自动关闭
--------

若设备仍处于 UP 状态，ndo_stop 例程将由 unregister_netdevice 调用。

发送路径指南
============

提前停止队列
------------

ndo_start_xmit 方法在任何正常情况下都不得返回 NETDEV_TX_BUSY。
除非设备确实无法预先判断其发送功能何时会繁忙，否则返回该值被视为严重错误。

相反，驱动必须正确地维护队列。例如，对于实现分散-聚集
（scatter-gather）的驱动，这意味着：

.. code-block:: c

	static u32 drv_tx_avail(struct drv_ring *dr)
	{
		u32 used = READ_ONCE(dr->prod) - READ_ONCE(dr->cons);

		return dr->tx_ring_size - (used & dr->tx_ring_mask);
	}

	static netdev_tx_t drv_hard_start_xmit(struct sk_buff *skb,
					       struct net_device *dev)
	{
		struct drv *dp = netdev_priv(dev);
		struct netdev_queue *txq;
		struct drv_ring *dr;
		int idx;

		idx = skb_get_queue_mapping(skb);
		dr = dp->tx_rings[idx];
		txq = netdev_get_tx_queue(dev, idx);

		//...
		/* 这应当是极为罕见的竞争——记录下来。 */
		if (drv_tx_avail(dr) <= skb_shinfo(skb)->nr_frags + 1) {
			netif_tx_stop_queue(txq);
			netdev_warn(dev, "Tx Ring full when queue awake!\n");
			return NETDEV_TX_BUSY;
		}

		//... 将报文排队到网卡 ...

		netdev_tx_sent_queue(txq, skb->len);

		//... 使用 WRITE_ONCE() 更新 tx 生产者索引 ...

		if (!netif_txq_maybe_stop(txq, drv_tx_avail(dr),
					  MAX_SKB_FRAGS + 1, 2 * MAX_SKB_FRAGS))
			dr->stats.stopped++;

		//...
		return NETDEV_TX_OK;
	}

然后在 TX 回收事件处理的末尾：

.. code-block:: c

	//... 使用 WRITE_ONCE() 更新 tx 消费者索引 ...

	netif_txq_completed_wake(txq, cmpl_pkts, cmpl_bytes,
				 drv_tx_avail(dr), 2 * MAX_SKB_FRAGS);

无锁队列停止/唤醒辅助宏
~~~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: include/net/netdev_queues.h
   :doc: Lockless queue stopping / waking helpers.

netif_txq_maybe_stop()、netif_txq_try_stop() 等标准宏已经过充分测试，
请优先使用它们，而非本地同步方案。

无独占所有权
------------

ndo_start_xmit 方法不得修改被克隆 SKB 的共享部分。

及时完成
--------

请谨记：一旦 ndo_start_xmit 方法返回 NETDEV_TX_OK，
驱动就有责任在有限的时间内释放该 SKB。

例如，这意味着不允许你的 TX 缓解方案在没有新 TX 报文发送时，
让 TX 报文永远“滞留”在 TX 环中而不被回收。
这种错误会使等待发送缓冲区空间释放的套接字发生死锁。

若 ndo_start_xmit 方法返回 NETDEV_TX_BUSY，
则不得保留对该 SKB 的任何引用，也不得尝试释放它。

错误消息报告
============

许多驱动配置接口会向驱动传递一个 Netlink 扩展 ACK（``extack``）对象
（直接作为参数，或作为参数结构体的成员）。驱动应尽量通过 ``extack`` 对象
报告大多数错误。表示系统或设备行为异常、处于不良状态的系统级异常，则应继续
报告到系统日志。

消息应 **要么** 通过 ``extack`` 传递， **要么** 写入系统日志。驱动不应
试图将同一信息同时报告到两处。
